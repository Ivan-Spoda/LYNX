#include "engine/PluginSandbox.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

#include <juce_gui_extra/juce_gui_extra.h>

#include "core/Denormals.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx
{
bool pluginSandboxEnabled()
{
    return PluginHost::settingsFolder().getChildFile ("sandbox.txt").loadFileAsString().trim() == "1";
}

void setPluginSandboxEnabled (bool enabled)
{
    PluginHost::settingsFolder().getChildFile ("sandbox.txt").replaceWithText (enabled ? "1" : "0");
}

namespace
{
juce::File separateListFile() { return PluginHost::settingsFolder().getChildFile ("separate-process.txt"); }
juce::StringArray separateList()
{
    auto lines = juce::StringArray::fromLines (separateListFile().loadFileAsString());
    lines.trim();
    lines.removeEmptyStrings();
    return lines;
}
} // namespace

bool pluginAlwaysSeparate (const juce::String& name)
{
    return name.isNotEmpty() && separateList().contains (name.trim(), true);
}

void setPluginAlwaysSeparate (const juce::String& name, bool on)
{
    if (name.trim().isEmpty())
        return;
    auto list = separateList();
    list.removeString (name.trim(), true);
    if (on)
        list.add (name.trim());
    separateListFile().replaceWithText (list.joinIntoString ("\n") + (list.isEmpty() ? "" : "\n"));
}

namespace
{
std::atomic<bool> showAlerts { true };

int processTag()
{
    static const int tag = juce::Random::getSystemRandom().nextInt (1000000);
    return tag;
}

// diagnostics: LYNX_SANDBOX_LOG=file logs the steps (one file per process)
void trace (const juce::String& what)
{
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("LYNX_SANDBOX_LOG", {}); path.isNotEmpty())
        juce::File (path + "." + juce::String (processTag())).appendText (juce::String (juce::Time::getMillisecondCounter()) + " " + what + "\n");
}
}

void setPluginSandboxAlerts (bool show)
{
    showAlerts = show;
}

#if JUCE_WINDOWS
namespace
{
// Block shared between LYNX and the child. The parent writes the input and signals `request`; the child
// processes in place and signals `response`.
struct SharedBlock
{
    static constexpr std::uint32_t magicValue = 0x4C594E58; // "LYNX"
    static constexpr int maxChannels = 8, maxFrames = PluginHost::maxBlockSize, maxMidi = 1024, maxParams = 512;

    struct Midi
    {
        std::int32_t offset;
        std::uint8_t size, bytes[3];
    };
    struct Param
    {
        std::int32_t index;
        float value;
    };

    std::uint32_t magic;
    std::int32_t frames, channels, midiCount, paramCount, paramOutCount, latency;
    Midi midi[maxMidi];
    Param params[maxParams], paramsOut[maxParams];
    float audio[maxChannels][maxFrames];
};

juce::String objectName (const juce::String& base, const char* suffix)
{
    return "Local\\" + base + suffix;
}

std::wstring wide (const juce::String& s)
{
    return std::wstring (s.toWideCharPointer());
}

juce::MemoryBlock encode (const juce::ValueTree& message)
{
    juce::MemoryOutputStream out;
    message.writeToStream (out);
    return out.getMemoryBlock();
}

// Parameter-change queue with multiple producers (UI and audio thread) and one consumer.
class ParamQueue
{
public:
    void push (int index, float value) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (spin);
        if (count < (int) items.size())
            items[(std::size_t) count++] = { index, value };
    }

    int drain (SharedBlock::Param* out, int max) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (spin);
        const auto n = std::min (count, max);
        std::memcpy (out, items.data(), sizeof (SharedBlock::Param) * (std::size_t) n);
        std::memmove (items.data(), items.data() + n, sizeof (SharedBlock::Param) * (std::size_t) (count - n));
        count -= n;
        return n;
    }

private:
    juce::SpinLock spin;
    std::array<SharedBlock::Param, 4096> items {};
    int count = 0;
};

// ---------------------------------------------------------------------------
// LYNX side: connection to the child, shared memory and events.

class SandboxSession final : private juce::ChildProcessCoordinator
{
public:
    SandboxSession()
    {
        static std::atomic<int> counter { 0 };
        baseName = "LYNX-sandbox-" + juce::String ((int) GetCurrentProcessId()) + "-" + juce::String (++counter);
        mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (SharedBlock),
                                      wide (objectName (baseName, "-mem")).c_str());
        if (mapping != nullptr)
            shared = static_cast<SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBlock)));
        request = CreateEventW (nullptr, FALSE, FALSE, wide (objectName (baseName, "-req")).c_str());
        response = CreateEventW (nullptr, FALSE, FALSE, wide (objectName (baseName, "-resp")).c_str());
        if (shared != nullptr)
        {
            std::memset (shared, 0, sizeof (SharedBlock));
            shared->magic = SharedBlock::magicValue;
        }
    }

    ~SandboxSession() override
    {
        closing = true;
        killWorkerProcess();
        if (shared != nullptr)
            UnmapViewOfFile (shared);
        for (auto* h : { mapping, request, response })
            if (h != nullptr)
                CloseHandle (h);
    }

    bool start()
    {
        if (shared == nullptr || request == nullptr || response == nullptr)
            return false;
        const auto launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), pluginSandboxId, 10000);
        trace ("parent: child launched " + juce::String ((int) launched));
        return launched;
    }

    const juce::String& name() const noexcept { return baseName; }
    SharedBlock* block() noexcept { return shared; }
    bool isDead() const noexcept { return dead.load(); }

    void markDead()
    {
        if (dead.exchange (true))
            return;
        replyReady.signal();
        if (closing || ! showAlerts)
            return;
        juce::MessageManager::callAsync ([title = pluginName] {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, lynx::tr ("Plugin"),
                                                    lynx::tr ("{0} stopped responding (separate process). "
                                                              "It has been muted; save the project and reopen it to reload.", { title }));
        });
    }

    void setPluginName (const juce::String& n) { pluginName = n; }

    // request with reply (UI thread)
    juce::ValueTree ask (const juce::ValueTree& message, int timeoutMs)
    {
        const std::scoped_lock lock (askMutex);
        if (dead)
            return {};
        {
            const std::scoped_lock l (replyMutex);
            reply = {};
        }
        replyReady.reset();
        if (! sendMessageToWorker (encode (message)))
            return {};
        if (! replyReady.wait (timeoutMs))
        {
            markDead();
            return {};
        }
        const std::scoped_lock l (replyMutex);
        return reply;
    }

    void tell (const juce::ValueTree& message)
    {
        if (! dead)
            sendMessageToWorker (encode (message));
    }

    // one audio block (audio thread): false = the child didn't respond
    bool runBlock (int timeoutMs) noexcept
    {
        if (dead)
            return false;
        SetEvent (request);
        if (WaitForSingleObject (response, (DWORD) timeoutMs) != WAIT_OBJECT_0)
        {
            markDead();
            return false;
        }
        return true;
    }

private:
    void handleMessageFromWorker (const juce::MemoryBlock& data) override
    {
        {
            const std::scoped_lock l (replyMutex);
            reply = juce::ValueTree::readFromData (data.getData(), data.getSize());
        }
        trace ("parent: reply received " + reply.getType().toString());
        replyReady.signal();
    }

    void handleConnectionLost() override
    {
        trace ("parent: connection lost");
        markDead();
    }

    juce::String baseName, pluginName;
    HANDLE mapping = nullptr, request = nullptr, response = nullptr;
    SharedBlock* shared = nullptr;
    std::atomic<bool> dead { false }, closing { false };
    std::mutex askMutex, replyMutex;
    juce::ValueTree reply;
    juce::WaitableEvent replyReady;
};

class RemotePlugin;

class RemoteParameter final : public juce::AudioPluginInstance::HostedParameter
{
public:
    RemoteParameter (RemotePlugin& o, int i, const juce::ValueTree& info)
        : owner (o), index (i),
          id (info["id"].toString()), name (info["name"].toString()), label (info["label"].toString()),
          defaultValue ((float) (double) info["default"]), steps ((int) info["steps"]), discrete ((bool) info["discrete"]),
          value ((float) (double) info["value"])
    {
    }

    float getValue() const override { return value.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override { return defaultValue; }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override { return label; }
    float getValueForText (const juce::String& text) const override { return juce::jlimit (0.0f, 1.0f, text.getFloatValue()); }
    juce::String getText (float v, int) const override { return juce::String (v, 3); }
    int getNumSteps() const override { return steps > 0 ? steps : AudioProcessorParameter::getNumSteps(); }
    bool isDiscrete() const override { return discrete; }
    juce::String getParameterID() const override { return id; }

    void setFromWorker (float v)
    {
        value = v;
        sendValueChangedMessageToListeners (v);
    }

private:
    RemotePlugin& owner;
    const int index;
    const juce::String id, name, label;
    const float defaultValue;
    const int steps;
    const bool discrete;
    std::atomic<float> value;
};

class RemoteEditor final : public juce::AudioProcessorEditor
{
public:
    RemoteEditor (juce::AudioProcessor& p, std::function<void()> show)
        : AudioProcessorEditor (p), showWindow (std::move (show))
    {
        text.setText (tr ("This plugin runs in a separate process: its interface opens in its own window."),
                      juce::dontSendNotification);
        text.setJustificationType (juce::Justification::centred);
        button.setButtonText (tr ("Show the plugin window"));
        button.onClick = [this] { showWindow(); };
        addAndMakeVisible (text);
        addAndMakeVisible (button);
        setSize (380, 96);
        showWindow();
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg1); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        button.setBounds (r.removeFromBottom (28).withSizeKeepingCentre (220, 28));
        text.setBounds (r);
    }

private:
    std::function<void()> showWindow;
    juce::Label text;
    juce::TextButton button;
};

class RemotePlugin final : public juce::AudioPluginInstance
{
public:
    RemotePlugin (std::unique_ptr<SandboxSession> s, const juce::PluginDescription& d, const juce::ValueTree& info)
        : AudioPluginInstance (busesFor ((int) info["ins"], (int) info["outs"])),
          session (std::move (s)), description (d),
          midiIn ((bool) info["midiIn"]), midiOut ((bool) info["midiOut"]), editor ((bool) info["editor"]),
          tail ((double) info["tail"]), lastState (info["state"].toString())
    {
        session->setPluginName (description.name);
        setLatencySamples ((int) info["latency"]);
        const auto params = info.getChildWithName ("params");
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            auto p = std::make_unique<RemoteParameter> (*this, i, params.getChild (i));
            remoteParams.push_back (p.get());
            addHostedParameter (std::move (p));
        }
    }

    ~RemotePlugin() override { session.reset(); }

    bool dead() const noexcept { return session->isDead(); }
    void hangForTest() { session->tell (juce::ValueTree ("hang")); }

    void queueParameter (int index, float value) noexcept { pending.push (index, value); }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }
    const juce::String getName() const override { return description.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return tail; }
    bool acceptsMidi() const override { return midiIn; }
    bool producesMidi() const override { return midiOut; }
    bool hasEditor() const override { return editor; }
    juce::AudioProcessorEditor* createEditor() override
    {
        return new RemoteEditor (*this, [this] { session->tell (juce::ValueTree ("showEditor")); });
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (const auto r = session->ask (juce::ValueTree ("getState"), 10000); r.isValid())
            lastState = r["state"].toString();
        dest.fromBase64Encoding (lastState);
    }

    void setStateInformation (const void* data, int size) override
    {
        const juce::MemoryBlock block (data, (std::size_t) juce::jmax (0, size));
        lastState = block.toBase64Encoding();
        session->ask (juce::ValueTree ("setState").setProperty ("state", lastState, nullptr), 10000);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        auto* shared = session->block();
        const auto frames = std::min (buffer.getNumSamples(), SharedBlock::maxFrames);
        const auto channels = std::min (buffer.getNumChannels(), SharedBlock::maxChannels);
        if (session->isDead() || shared == nullptr || frames <= 0)
        {
            buffer.clear();
            return;
        }

        shared->frames = frames;
        shared->channels = channels;
        for (int c = 0; c < channels; ++c)
            std::memcpy (shared->audio[c], buffer.getReadPointer (c), sizeof (float) * (std::size_t) frames);

        int count = 0;
        for (const auto m : midi)
            if (m.numBytes >= 1 && m.numBytes <= 3 && count < SharedBlock::maxMidi)
            {
                auto& e = shared->midi[count++];
                e.offset = juce::jlimit (0, frames - 1, m.samplePosition);
                e.size = (std::uint8_t) m.numBytes;
                std::memcpy (e.bytes, m.data, (std::size_t) m.numBytes);
            }
        shared->midiCount = count;
        shared->paramCount = pending.drain (shared->params, SharedBlock::maxParams);
        shared->paramOutCount = 0;

        // timeout: 0.5 s (a healthy plugin responds in well under one block)
        if (! session->runBlock (500))
        {
            buffer.clear();
            return;
        }

        for (int c = 0; c < channels; ++c)
            std::memcpy (buffer.getWritePointer (c), shared->audio[c], sizeof (float) * (std::size_t) frames);
        for (int c = channels; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, frames);

        for (int i = 0; i < std::min (shared->paramOutCount, SharedBlock::maxParams); ++i)
            if (const auto k = shared->paramsOut[i].index; k >= 0 && k < (int) remoteParams.size())
                remoteParams[(std::size_t) k]->setFromWorker (shared->paramsOut[i].value);

        if (shared->latency != getLatencySamples())
            setLatencySamples (shared->latency);
    }

private:
    static BusesProperties busesFor (int ins, int outs)
    {
        auto set = [] (int n) { return n <= 8 ? juce::AudioChannelSet::canonicalChannelSet (n) : juce::AudioChannelSet::discreteChannels (n); };
        BusesProperties buses;
        if (ins > 0)
            buses = buses.withInput ("Input", set (ins), true);
        if (outs > 0)
            buses = buses.withOutput ("Output", set (outs), true);
        return buses;
    }

    std::unique_ptr<SandboxSession> session;
    juce::PluginDescription description;
    bool midiIn, midiOut, editor;
    double tail;
    juce::String lastState;
    std::vector<RemoteParameter*> remoteParams;
    ParamQueue pending;
};

void RemoteParameter::setValue (float v)
{
    value = v;
    owner.queueParameter (index, v);
}

// ---------------------------------------------------------------------------
// Child side.

class SandboxWindow final : public juce::DocumentWindow
{
public:
    explicit SandboxWindow (const juce::String& title)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setAlwaysOnTop (true);
        setIcon (logoImage());
    }

    void closeButtonPressed() override { setVisible (false); }
};

class SandboxWorker final : public juce::ChildProcessWorker,
                            private juce::Thread,
                            private juce::AudioProcessorParameter::Listener
{
public:
    SandboxWorker() : Thread ("LYNX sandbox audio") {}

    ~SandboxWorker() override
    {
        stopThread (2000);
        window.reset();
        if (plugin != nullptr)
            for (auto* p : plugin->getParameters())
                p->removeListener (this);
        plugin.reset();
        if (shared != nullptr)
            UnmapViewOfFile (shared);
        for (auto* h : { mapping, request, response })
            if (h != nullptr)
                CloseHandle (h);
    }

    void handleMessageFromCoordinator (const juce::MemoryBlock& data) override
    {
        auto message = juce::ValueTree::readFromData (data.getData(), data.getSize());
        trace ("child: message " + message.getType().toString());
        juce::MessageManager::callAsync ([this, message] { handle (message); });
    }

    void handleConnectionLost() override
    {
        trace ("child: connection lost");
        juce::MessageManager::callAsync ([] { juce::JUCEApplicationBase::quit(); });
    }

private:
    void send (const juce::ValueTree& message) { sendMessageToCoordinator (encode (message)); }

    void handle (const juce::ValueTree& message)
    {
        if (message.hasType ("open"))
            send (open (message));
        else if (message.hasType ("getState"))
        {
            juce::ValueTree reply ("state");
            if (plugin != nullptr)
                reply.setProperty ("state", juce::String (capturePluginState (*plugin)), nullptr);
            send (reply);
        }
        else if (message.hasType ("setState"))
        {
            if (plugin != nullptr)
            {
                juce::MemoryBlock state;
                if (state.fromBase64Encoding (message["state"].toString()))
                    plugin->setStateInformation (state.getData(), (int) state.getSize());
            }
            send (juce::ValueTree ("ok"));
        }
        else if (message.hasType ("hang"))
            hang = true;
        else if (message.hasType ("showEditor") && plugin != nullptr && plugin->hasEditor())
        {
            if (window == nullptr)
            {
                window = std::make_unique<SandboxWindow> (plugin->getName());
                if (auto* ed = plugin->createEditorIfNeeded())
                    window->setContentOwned (ed, true);
                window->centreWithSize (window->getWidth(), window->getHeight());
            }
            window->setVisible (true);
            window->toFront (true);
        }
    }

    juce::ValueTree open (const juce::ValueTree& message)
    {
        juce::ValueTree reply ("opened");
        auto fail = [&] (const juce::String& why) {
            reply.setProperty ("error", why, nullptr);
            return reply;
        };

        const auto base = message["shm"].toString();
        mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, wide (objectName (base, "-mem")).c_str());
        if (mapping != nullptr)
            shared = static_cast<SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBlock)));
        request = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wide (objectName (base, "-req")).c_str());
        response = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wide (objectName (base, "-resp")).c_str());
        if (shared == nullptr || request == nullptr || response == nullptr || shared->magic != SharedBlock::magicValue)
            return fail ("shared memory unavailable");

        juce::PluginDescription description;
        const auto xml = juce::parseXML (message["description"].toString());
        if (xml == nullptr || ! description.loadFromXml (*xml))
            return fail ("invalid plugin description");

        const auto sampleRate = (double) message["sampleRate"];
        blockSize = juce::jlimit (16, SharedBlock::maxFrames, (int) message["blockSize"]);
        PluginHost host (false);
        juce::String error;
        plugin = createRightInstance (host.getFormats(), description, sampleRate, blockSize, error);
        if (plugin == nullptr)
            return fail (error.isEmpty() ? juce::String ("the plugin failed to open") : error);

        preparePlugin (*plugin, sampleRate, blockSize);
        if (const auto state = message["state"].toString(); state.isNotEmpty())
        {
            juce::MemoryBlock block;
            if (block.fromBase64Encoding (state))
                plugin->setStateInformation (block.getData(), (int) block.getSize());
        }

        reply.setProperty ("ins", plugin->getTotalNumInputChannels(), nullptr);
        reply.setProperty ("outs", plugin->getTotalNumOutputChannels(), nullptr);
        reply.setProperty ("midiIn", plugin->acceptsMidi(), nullptr);
        reply.setProperty ("midiOut", plugin->producesMidi(), nullptr);
        reply.setProperty ("editor", plugin->hasEditor(), nullptr);
        reply.setProperty ("tail", plugin->getTailLengthSeconds(), nullptr);
        reply.setProperty ("latency", plugin->getLatencySamples(), nullptr);
        reply.setProperty ("state", juce::String (capturePluginState (*plugin)), nullptr);

        juce::ValueTree params ("params");
        const auto& list = plugin->getParameters();
        for (int i = 0; i < list.size(); ++i)
        {
            auto* p = list[i];
            juce::ValueTree info ("p");
            info.setProperty ("name", p->getName (128), nullptr);
            info.setProperty ("label", p->getLabel(), nullptr);
            info.setProperty ("default", p->getDefaultValue(), nullptr);
            info.setProperty ("value", p->getValue(), nullptr);
            info.setProperty ("steps", p->getNumSteps(), nullptr);
            info.setProperty ("discrete", p->isDiscrete(), nullptr);
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
                info.setProperty ("id", hosted->getParameterID(), nullptr);
            else
                info.setProperty ("id", juce::String (i), nullptr);
            params.appendChild (info, nullptr);
            p->addListener (this);
        }
        reply.appendChild (params, nullptr);

        channels = juce::jmin (SharedBlock::maxChannels, juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()));
        midiBuffer.ensureSize (8192);
        startThread (juce::Thread::Priority::highest);
        reply.setProperty ("ok", true, nullptr);
        return reply;
    }

    void run() override
    {
        // the sandbox's audio thread gets the same FTZ/DAZ as LYNX's own audio threads (round 160: it had none)
        const lynx::ScopedFlushDenormals noDenormals;
        while (! threadShouldExit())
        {
            if (WaitForSingleObject (request, 100) != WAIT_OBJECT_0)
                continue;
            process();
            SetEvent (response);
        }
    }

    void process() noexcept
    {
        if (hang) // only in the timeout test
            Thread::sleep (3000);
        const auto frames = juce::jlimit (0, blockSize, (int) shared->frames);
        const auto& list = plugin->getParameters();
        for (int i = 0; i < std::min ((int) shared->paramCount, SharedBlock::maxParams); ++i)
            if (const auto k = shared->params[i].index; k >= 0 && k < list.size())
                list[k]->setValue (shared->params[i].value);

        midiBuffer.clear();
        for (int i = 0; i < std::min ((int) shared->midiCount, SharedBlock::maxMidi); ++i)
        {
            const auto& e = shared->midi[i];
            if (e.size >= 1 && e.size <= 3)
                midiBuffer.addEvent (e.bytes, e.size, juce::jlimit (0, juce::jmax (0, frames - 1), (int) e.offset));
        }

        float* pointers[SharedBlock::maxChannels];
        for (int c = 0; c < channels; ++c)
        {
            pointers[c] = shared->audio[c];
            if (c >= shared->channels)
                std::memset (shared->audio[c], 0, sizeof (float) * (std::size_t) frames);
        }
        if (frames > 0)
        {
            juce::AudioBuffer<float> buffer (pointers, channels, frames);
            plugin->processBlock (buffer, midiBuffer);
        }

        shared->paramOutCount = changed.drain (shared->paramsOut, SharedBlock::maxParams);
        shared->latency = plugin->getLatencySamples();
    }

    void parameterValueChanged (int index, float value) override { changed.push (index, value); }
    void parameterGestureChanged (int, bool) override {}

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    std::unique_ptr<SandboxWindow> window;
    HANDLE mapping = nullptr, request = nullptr, response = nullptr;
    SharedBlock* shared = nullptr;
    int blockSize = 512, channels = 2;
    std::atomic<bool> hang { false };
    juce::MidiBuffer midiBuffer;
    ParamQueue changed;
};

std::unique_ptr<SandboxWorker>& worker()
{
    static std::unique_ptr<SandboxWorker> w;
    return w;
}
} // namespace

std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error)
{
    trace ("parent: session");
    auto session = std::make_unique<SandboxSession>();
    trace ("parent: starting child");
    if (! session->start())
    {
        error = juce::String::fromUTF8 ("could not start the plugin process");
        return nullptr;
    }

    juce::ValueTree open ("open");
    open.setProperty ("shm", session->name(), nullptr);
    open.setProperty ("description", description.createXml()->toString(), nullptr);
    open.setProperty ("sampleRate", sampleRate, nullptr);
    open.setProperty ("blockSize", blockSize, nullptr);
    open.setProperty ("state", juce::String::fromUTF8 (stateBase64.c_str()), nullptr);

    trace ("parent: open");
    const auto info = session->ask (open, 30000);
    trace ("parent: reply " + juce::String ((int) info.isValid()));
    if (! info.isValid() || ! (bool) info["ok"])
    {
        error = info.isValid() ? info["error"].toString() : juce::String::fromUTF8 ("the plugin process did not respond");
        return nullptr;
    }
    auto remote = std::make_unique<RemotePlugin> (std::move (session), description, info);
    trace ("parent: proxy ready");
    return remote;
}

bool isSandboxedPluginDead (const juce::AudioPluginInstance& plugin)
{
    const auto* remote = dynamic_cast<const RemotePlugin*> (&plugin);
    return remote != nullptr && remote->dead();
}

void hangSandboxedPluginForTest (juce::AudioPluginInstance& plugin)
{
    if (auto* remote = dynamic_cast<RemotePlugin*> (&plugin))
        remote->hangForTest();
}

bool runPluginSandboxWorker (const juce::String& commandLine)
{
    if (! commandLine.contains (pluginSandboxId))
        return false;
    trace ("child: " + commandLine);
    worker() = std::make_unique<SandboxWorker>();
    if (! worker()->initialiseFromCommandLine (commandLine, pluginSandboxId, 10000))
    {
        worker().reset();
        return false;
    }
    return true;
}

void shutdownPluginSandboxWorker()
{
    worker().reset();
}

#else // ! JUCE_WINDOWS

std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription&, double, int, const std::string&, juce::String& error)
{
    error = juce::String::fromUTF8 ("out-of-process plugins: Windows only");
    return nullptr;
}

bool isSandboxedPluginDead (const juce::AudioPluginInstance&) { return false; }
void hangSandboxedPluginForTest (juce::AudioPluginInstance&) {}
bool runPluginSandboxWorker (const juce::String&) { return false; }
void shutdownPluginSandboxWorker() {}

#endif
} // namespace lynx
