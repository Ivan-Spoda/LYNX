#include "engine/PluginHost.h"

#include "core/Preferences.h"
#include "core/PluginScanPolicy.h"

#include "core/FlWrapper.h"

#include "engine/ClapFormat.h"
#include "engine/Vst2Format.h"
#include "engine/PluginSandbox.h"
#include "engine/FlStudio.h"
#include "platform/CrashGuard.h"
#include "ui/I18n.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx
{
void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize)
{
    // Try stereo output; plugins that don't accept it keep whatever layout they have.
    plugin.enableAllBuses();
    auto layout = plugin.getBusesLayout();

    if (! layout.outputBuses.isEmpty())
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (! layout.inputBuses.isEmpty())
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();

        if (plugin.checkBusesLayoutSupported (layout))
            plugin.setBusesLayout (layout);
    }

    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin.prepareToPlay (sampleRate, blockSize);
}

namespace
{
void setPluginParameter (juce::AudioPluginInstance& plugin, int index, float value) noexcept
{
    const auto& params = plugin.getParameters();

    if (index >= 0 && index < params.size())
        if (auto* p = params[index]; p->getValue() != value)
            p->setValue (value);
}

// The song's transport as hosted plugins see it (tempo, position, play state), from the renderer's timeline.
class TimelinePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        const auto& t = hostTimeline();
        const auto bpm = t.bpm.load (std::memory_order_relaxed);
        const auto rate = t.sampleRate.load (std::memory_order_relaxed);
        const auto quarters = t.tick.load (std::memory_order_relaxed) / (double) ppq;
        const auto beatsPerBar = juce::jmax (1, t.beatsPerBar.load (std::memory_order_relaxed));
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { beatsPerBar, 4 });
        info.setPpqPosition (quarters);
        info.setPpqPositionOfLastBarStart (std::floor (quarters / beatsPerBar) * beatsPerBar);
        info.setTimeInSeconds (quarters * 60.0 / juce::jmax (1.0, bpm));
        info.setTimeInSamples ((juce::int64) std::llround (quarters * 60.0 / juce::jmax (1.0, bpm) * rate));
        info.setIsPlaying (t.playing.load (std::memory_order_relaxed));
        return info;
    }
};

juce::AudioPlayHead& timelinePlayHead()
{
    static TimelinePlayHead head;
    return head;
}

int bufferChannels (const juce::AudioPluginInstance& plugin)
{
    return juce::jmax (2, plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
}
} // namespace

// ---------------------------------------------------------------------------

// Crashes of hosted plugins this session. The audio thread only writes a fixed slot (no allocation, no lock): a bounded
// copy of the instance's name, taken when the instance was created.
namespace
{
constexpr int crashSlots = 16;
struct CrashSlot
{
    char name[128] {};
    std::uint32_t code = 0;
};
CrashSlot crashLog[crashSlots];
std::atomic<int> crashCount { 0 };

void recordCrash (const std::string& name, std::uint32_t code) noexcept
{
    const auto index = crashCount.load (std::memory_order_relaxed);
    auto& slot = crashLog[index % crashSlots];
    std::size_t i = 0;
    for (; i + 1 < sizeof (slot.name) && i < name.size(); ++i)
        slot.name[i] = name[i];
    slot.name[i] = 0;
    slot.code = code;
    crashCount.store (index + 1, std::memory_order_release); // (two plugins crashing in the same instant may share a slot)
}

// The calls of one audio block, as a guarded unit
struct BlockCall
{
    juce::AudioPluginInstance* plugin;
    juce::AudioBuffer<float>* view;
    juce::MidiBuffer* midi;
    void operator()() const { plugin->processBlock (*view, *midi); }
};
} // namespace

int pluginCrashCount() noexcept
{
    return crashCount.load (std::memory_order_acquire);
}

std::vector<PluginCrash> pluginCrashes()
{
    std::vector<PluginCrash> list;
    const auto count = pluginCrashCount();
    for (int i = std::max (0, count - crashSlots); i < count; ++i)
        list.push_back ({ crashLog[i % crashSlots].name, crashLog[i % crashSlots].code });
    return list;
}

namespace
{
std::optional<std::string> guardedState (juce::AudioPluginInstance& plugin, std::atomic<bool>& crashed, const std::string& name)
{
    if (crashed.load())
        return std::nullopt;
    juce::MemoryBlock state;
    if (const auto code = crashguard::run ([&] { plugin.getStateInformation (state); }); code != 0)
    {
        crashed.store (true);
        recordCrash (name, code);
        return std::nullopt;
    }
    return state.toBase64Encoding().toStdString();
}

void guardedUiParameter (juce::AudioPluginInstance& plugin, std::atomic<bool>& crashed, const std::string& name, int index, float value)
{
    if (crashed.load())
        return;
    if (const auto code = crashguard::run ([&] {
            const auto& params = plugin.getParameters();
            if (index >= 0 && index < params.size())
                params[index]->setValueNotifyingHost (value);
        });
        code != 0)
    {
        crashed.store (true);
        recordCrash (name, code);
    }
}
} // namespace

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize)
    : plugin (std::move (instance)),
      blockSize (maxBlockSize),
      name (plugin->getName().toStdString())
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
    midi.ensureSize (8192); // the audio thread only adds events within this capacity
    plugin->setPlayHead (&timelinePlayHead());
}

PluginInstrument::~PluginInstrument()
{
    // a plugin that crashed is not called again, not even to release it (its memory is left as it is)
    if (crashed.load())
        (void) plugin.release();
    else
        crashguard::run ([this] { plugin->releaseResources(); });
}

void PluginInstrument::failed (std::uint32_t code) noexcept
{
    crashed.store (true, std::memory_order_relaxed);
    recordCrash (name, code);
}

std::optional<std::string> PluginInstrument::captureState()
{
    return guardedState (*plugin, crashed, name);
}

void PluginInstrument::setParameterFromUi (int index, float value)
{
    guardedUiParameter (*plugin, crashed, name, index, value);
}

void PluginInstrument::noteOn (int key, float velocity) noexcept
{
    midi.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, key), juce::jlimit (0.0f, 1.0f, velocity)), 0);
}

void PluginInstrument::noteOff (int key) noexcept
{
    midi.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, key)), 0);
}

void PluginInstrument::allNotesOff() noexcept
{
    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
}

void PluginInstrument::setParameter (int index, float value) noexcept
{
    if (crashed.load (std::memory_order_relaxed))
        return;
    if (const auto code = crashguard::run ([&] { setPluginParameter (*plugin, index, value); }); code != 0)
        failed (code);
}

void PluginInstrument::render (float* left, float* right, int n, float gl, float gr) noexcept
{
    if (crashed.load (std::memory_order_relaxed))
    {
        midi.clear();
        return; // silent
    }

    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        if (const auto code = crashguard::run (BlockCall { plugin.get(), &view, &midi }); code != 0)
        {
            midi.clear();
            failed (code);
            return;
        }
        midi.clear();

        const auto* l = view.getReadPointer (0);
        const auto* r = view.getNumChannels() > 1 ? view.getReadPointer (1) : l;

        for (int i = 0; i < count; ++i)
        {
            left[done + i] += l[i] * gl;
            right[done + i] += r[i] * gr;
        }

        done += count;
    }
}

// ---------------------------------------------------------------------------

PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize)
    : plugin (std::move (instance)),
      blockSize (maxBlockSize),
      name (plugin->getName().toStdString())
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
    plugin->setPlayHead (&timelinePlayHead());
}

PluginEffect::~PluginEffect()
{
    if (crashed.load())
        (void) plugin.release();
    else
        crashguard::run ([this] { plugin->releaseResources(); });
}

void PluginEffect::failed (std::uint32_t code) noexcept
{
    crashed.store (true, std::memory_order_relaxed);
    recordCrash (name, code);
}

std::optional<std::string> PluginEffect::captureState()
{
    return guardedState (*plugin, crashed, name);
}

void PluginEffect::setParameterFromUi (int index, float value)
{
    guardedUiParameter (*plugin, crashed, name, index, value);
}

void PluginEffect::setParameter (int index, float value) noexcept
{
    if (crashed.load (std::memory_order_relaxed))
        return;
    if (const auto code = crashguard::run ([&] { setPluginParameter (*plugin, index, value); }); code != 0)
        failed (code);
}

void PluginEffect::process (float* left, float* right, int n, const EffectParams&, double) noexcept
{
    if (crashed.load (std::memory_order_relaxed))
        return; // bypassed: the input plays through

    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        view.copyFrom (0, 0, left + done, count);
        view.copyFrom (1, 0, right + done, count);
        if (const auto code = crashguard::run (BlockCall { plugin.get(), &view, &midi }); code != 0)
        {
            midi.clear();
            failed (code); // this block keeps its dry input
            return;
        }
        midi.clear();

        std::memcpy (left + done, view.getReadPointer (0), sizeof (float) * (size_t) count);
        std::memcpy (right + done, view.getReadPointer (view.getNumChannels() > 1 ? 1 : 0), sizeof (float) * (size_t) count);
        done += count;
    }
}

// ---------------------------------------------------------------------------

// Background scan: a thread that runs PluginHost::scan and publishes its progress in PluginHost::state.
class PluginHost::BackgroundScan final : public juce::Thread
{
public:
    BackgroundScan (PluginHost& h, juce::FileSearchPath p, juce::StringArray f = {})
        : juce::Thread ("LYNX plugin scan"),
          host (h),
          paths (std::move (p)),
          files (std::move (f))
    {
    }

    ~BackgroundScan() override
    {
        host.stopScan = true;
        stopThread (15000); // (listing a large folder can take a moment; a running child is killed at once)
    }

    void run() override
    {
        const auto progress = [this] (float done, const juce::String& file) {
            const std::scoped_lock lock (host.stateLock);
            host.state.progress = done;
            host.state.file = juce::File::createFileWithoutCheckingPath (file).getFileName();
            return ! threadShouldExit();
        };
        const auto found = files.isEmpty() ? host.scan (paths, progress) : host.scanFiles (files, progress);
        const std::scoped_lock lock (host.stateLock);
        host.state.running = false;
        host.state.found = found;
        host.state.progress = 1.0f;
        host.state.file = {};
        ++host.state.finished;
    }

private:
    PluginHost& host;
    juce::FileSearchPath paths;
    juce::StringArray files; // a targeted scan of these files instead of the folders
};

PluginHost::PluginHost (bool keep)
    : persistent (keep)
{
    addPluginFormats (formats);
    if (persistent)
    {
        if (auto xml = juce::XmlDocument::parse (settingsFolder().getChildFile ("plugins.xml")))
            known.recreateFromXml (*xml);
        favouriteIds = prefs::parseFavourites (settingsFolder().getChildFile ("plugin-favourites.txt").loadFileAsString().toStdString());
        // once: files marked as failed by scans before the class-count timeout (REA_rodada107 §2) get another try,
        // since a large bundle (Waves shell) was marked "hung" only for taking longer than 30 s
        const auto retried = settingsFolder().getChildFile ("scan-retry.txt");
        // round 113: once more, as the scan now keeps a description whose plugin only hung while closing (or, round
        // 111, crashed in its teardown at exit after writing it)
        if (retried.loadFileAsString().getIntValue() < 2)
        {
            if (! known.getBlacklistedFiles().isEmpty())
            {
                known.clearBlacklistedFiles();
                save();
            }
            retried.replaceWithText ("2\n");
        }
    }
}

PluginHost::~PluginHost()
{
    background = nullptr; // stops the scan (the child being examined is killed)
}

juce::File PluginHost::settingsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX");
    folder.createDirectory();
    return folder;
}

juce::FileSearchPath PluginHost::defaultSearchPath()
{
    juce::FileSearchPath paths;
    // Same locations JUCE/Steinberg use by default on Windows.
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
    // VST2 (Vst2Format): the usual folders installers use
    for (const auto* sub : { "VSTPlugins", "Steinberg/VSTPlugins", "Common Files/VST2", "Common Files/Steinberg/VST2" })
        paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile (sub));
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs/Common/CLAP"));
    paths.add (settingsFolder().getChildFile ("Plugins"));
    return paths;
}

juce::FileSearchPath PluginHost::suggestedFolders()
{
    juce::FileSearchPath paths;
    auto add = [&paths] (const juce::File& folder) {
        if (folder.isDirectory())
            paths.addIfNotAlreadyThere (folder);
    };
    // the standard folders even before they exist (plugins installed later land there)
    for (int i = 0; i < defaultSearchPath().getNumPaths(); ++i)
        paths.addIfNotAlreadyThere (defaultSearchPath()[i]);
    const auto fl = findFlStudio();
    for (const auto& folder : fl.vstFolders)
        add (juce::File (folder));
    for (const auto& folder : flPluginDatabase().pluginFolders())
    {
        const auto path = juce::String::fromUTF8 (folder.c_str());
        if (juce::File::isAbsolutePath (path))
            add (juce::File (path));
    }
    return paths;
}

juce::FileSearchPath PluginHost::pluginFolders() const
{
    const auto file = settingsFolder().getChildFile ("plugin-folders.txt");
    if (persistent && file.existsAsFile())
    {
        juce::FileSearchPath paths;
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());
        for (const auto& line : lines)
            if (line.trim().isNotEmpty() && juce::File::isAbsolutePath (line.trim()))
                paths.add (juce::File (line.trim()));
        return paths;
    }
    return const_cast<PluginHost*> (this)->suggestedFolders();
}

void PluginHost::setPluginFolders (const juce::FileSearchPath& folders)
{
    if (! persistent)
        return;
    juce::StringArray lines;
    for (int i = 0; i < folders.getNumPaths(); ++i)
        lines.add (folders[i].getFullPathName());
    settingsFolder().getChildFile ("plugin-folders.txt").replaceWithText (lines.joinIntoString ("\n") + "\n");
}

const FlPluginDb& PluginHost::flPluginDatabase()
{
    const std::scoped_lock lock (flDbLock);
    if (! flDbLoaded)
    {
        flDb = readFlPluginDatabase (findFlStudio(), &flDbFiles);
        flDbLoaded = true;
    }
    return flDb;
}

void PluginHost::save()
{
    if (! persistent)
        return;
    const std::scoped_lock lock (saveLock);
    if (auto xml = known.createXml())
        xml->writeTo (settingsFolder().getChildFile ("plugins.xml"));
}

namespace
{
// Child side of the scan: how many classes a VST3 module's factory declares (0 = unknown). The module stays loaded
// (and initialised) for the description that follows, which loads the same module again. Windows only for now.
int countVst3Classes (const juce::String& path)
{
   #if JUCE_WINDOWS
    juce::File binary (path);
    if (binary.isDirectory()) // bundle: Contents/x86_64-win/<name>.vst3
        binary = binary.getChildFile ("Contents").getChildFile ("x86_64-win").getChildFile (binary.getFileName());
    if (! binary.existsAsFile())
        return 0;
    // the leading part of IPluginFactory's vtable (FUnknown, then getFactoryInfo, countClasses)
    struct Factory
    {
        virtual int queryInterface (const char*, void**) = 0;
        virtual unsigned addRef() = 0;
        virtual unsigned release() = 0;
        virtual int getFactoryInfo (void*) = 0;
        virtual int countClasses() = 0;
    };
    static juce::DynamicLibrary module; // kept open until the child exits
    if (! module.open (binary.getFullPathName()))
        return 0;
    if (auto* init = (bool (*)()) module.getFunction ("InitDll"))
        if (! init())
            return 0;
    auto* get = (Factory* (*)()) module.getFunction ("GetPluginFactory");
    auto* factory = get != nullptr ? get() : nullptr;
    if (factory == nullptr)
        return 0;
    const auto classes = factory->countClasses();
    factory->release();
    return juce::jmax (0, classes);
   #else
    juce::ignoreUnused (path);
    return 0;
   #endif
}

juce::File classCountFile (const juce::File& result)
{
    return result.withFileExtension ("classes");
}

// the child wrote its whole answer (the file closes with the root element): a hang after it is the plugin's shutdown
bool scanResultComplete (const juce::File& result)
{
    if (! result.existsAsFile())
        return false;
    const auto xml = juce::XmlDocument::parse (result);
    return xml != nullptr && xml->hasTagName ("PLUGINS") && xml->getNumChildElements() > 0;
}

// why a plugin file was not described (%APPDATA%\LYNX\scan-failures.log), so a "missing" plugin can be explained
void logScanFailure (const juce::String& file, const juce::String& why, juce::uint32 elapsedMs)
{
    const auto log = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX").getChildFile ("scan-failures.log");
    if (log.getSize() > 256 * 1024)
        log.deleteFile();
    log.appendText (juce::Time::getCurrentTime().toISO8601 (true) + " | " + file + " | " + why + " | " + juce::String (elapsedMs / 1000.0, 1) + " s\n");
}
} // namespace

bool PluginHost::scanOne (juce::AudioPluginFormat& format, const juce::String& file)
{
    // a result file of our own: several LYNX processes (and scans) may run at once
    const auto result = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("lynx-scan-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".xml");
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::ChildProcess child;
    auto ok = child.start (juce::StringArray { exe.getFullPathName(), "--scan-one", format.getName(), file, result.getFullPathName() }, 0);
    if (ok)
    {
        // wait in short steps so a cancelled scan (or LYNX closing) doesn't wait for a slow plugin; a file holding
        // many plugins (Waves shell) gets more time once the child has said how many (core/PluginScanPolicy.h)
        const auto started = juce::Time::getMillisecondCounter();
        auto timeout = (juce::uint32) scanpolicy::baseTimeoutMs;
        auto classesKnown = false;
        while (child.isRunning() && ! stopScan && juce::Time::getMillisecondCounter() - started < timeout)
        {
            child.waitForProcessToFinish (100);
            if (! classesKnown && classCountFile (result).existsAsFile())
            {
                classesKnown = true;
                timeout = (juce::uint32) scanpolicy::timeoutMs (classCountFile (result).loadFileAsString().getIntValue());
            }
        }
        if (child.isRunning())
        {
            child.kill();
            if (stopScan) // not the plugin's fault: try it again next time
            {
                result.deleteFile();
                classCountFile (result).deleteFile();
                return false;
            }
            // the description may be complete and only the plugin's shutdown hung (round 113): keep it then
            ok = scanResultComplete (result);
            logScanFailure (file, ok ? "result kept, child killed after it" : (classesKnown ? "timeout while describing" : "timeout before the class count"),
                            juce::Time::getMillisecondCounter() - started);
        }
        else if (child.getExitCode() != 0)
            logScanFailure (file, "exit code " + juce::String ((int) child.getExitCode()), juce::Time::getMillisecondCounter() - started);
    }
    ok = ok && (child.isRunning() || child.getExitCode() == 0 || scanResultComplete (result));
    if (auto xml = ok ? juce::XmlDocument::parse (result) : nullptr)
    {
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e))
                known.addType (d);
        }
    }
    else if (! ok)
        known.addToBlacklist (file);
    result.deleteFile();
    classCountFile (result).deleteFile();
    return true;
}

int PluginHost::scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress)
{
    juce::StringArray files;
    for (auto* format : formats.getFormats())
        if (format->canScanForPlugins())
            files.addArray (format->searchPathsForPlugins (paths, true, false));
    return scanFiles (files, progress);
}

int PluginHost::scanFiles (const juce::StringArray& files, const std::function<bool (float, const juce::String&)>& progress)
{
    const std::scoped_lock lock (scanLock);
    stopScan = false;
    const auto before = known.getNumTypes();
    for (int i = 0; i < files.size() && ! stopScan; ++i)
    {
        const auto& file = files[i];
        if (progress && ! progress ((float) i / (float) juce::jmax (1, files.size()), file))
            break;
        if (known.getBlacklistedFiles().contains (file))
            continue;
        for (auto* format : formats.getFormats())
        {
            if (! format->canScanForPlugins() || ! format->fileMightContainThisPluginType (file))
                continue;
            if (! known.isListingUpToDate (file, *format))
                scanOne (*format, file);
            break;
        }
    }
    save();
    return known.getNumTypes() - before;
}

void PluginHost::startBackgroundScanOfFiles (const juce::StringArray& files)
{
    if (backgroundScan().running || files.isEmpty())
        return;
    background = nullptr;
    {
        const std::scoped_lock lock (stateLock);
        state.running = true;
        state.progress = 0.0f;
        state.found = 0;
        state.file = {};
    }
    background = std::make_unique<BackgroundScan> (*this, juce::FileSearchPath(), files);
    background->startThread (juce::Thread::Priority::low);
}

void PluginHost::forgetFile (const juce::String& file)
{
    for (const auto& t : known.getTypes())
        if (t.fileOrIdentifier == file)
            known.removeType (t);
    known.removeFromBlacklist (file);
    save();
}

void PluginHost::forgetAll()
{
    known.clear();
    known.clearBlacklistedFiles();
    save();
}

void PluginHost::setFavourite (const std::string& id, bool favourite)
{
    if (favourite)
        favouriteIds.insert (id);
    else
        favouriteIds.erase (id);
    if (persistent)
        settingsFolder().getChildFile ("plugin-favourites.txt").replaceWithText (juce::String (prefs::formatFavourites (favouriteIds)));
}

void PluginHost::startBackgroundScan (const juce::FileSearchPath& paths)
{
    if (backgroundScan().running)
        return;
    background = nullptr; // the previous, finished thread
    {
        const std::scoped_lock lock (stateLock);
        state.running = true;
        state.progress = 0.0f;
        state.found = 0;
        state.file = {};
    }
    background = std::make_unique<BackgroundScan> (*this, paths);
    background->startThread (juce::Thread::Priority::low);
}

PluginHost::ScanState PluginHost::backgroundScan() const
{
    const std::scoped_lock lock (stateLock);
    return state;
}

void PluginHost::cancelBackgroundScan()
{
    if (background != nullptr)
    {
        stopScan = true;
        background->signalThreadShouldExit();
    }
}


int PluginHost::scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output)
{
    // first tell the parent how many plugins this file holds, so it waits long enough for a large bundle
    if (formatName == "VST3")
        if (const auto classes = countVst3Classes (file); classes > 1)
            classCountFile (output).replaceWithText (juce::String (classes));

    juce::AudioPluginFormatManager manager;
    addPluginFormats (manager);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : manager.getFormats())
        if (format->getName() == formatName)
            format->findAllTypesForFile (found, file);
    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    xml.writeTo (output);
   #if JUCE_WINDOWS
    // the answer is written: the child ends here, without unloading the plugin modules, whose shutdown can hang (a
    // Waves shell in a desktop session) and get the whole file marked as hung (round 113)
    if (! found.isEmpty())
        TerminateProcess (GetCurrentProcess(), 0);
   #endif
    return found.size();
}

namespace
{
std::atomic<bool> loadedAny { false };
}

bool PluginHost::pluginsWereLoaded() noexcept
{
    return loadedAny.load();
}

int PluginHost::addPluginFile (const juce::File& file)
{
    loadedAny.store (true);
    const auto before = known.getNumTypes();
    juce::OwnedArray<juce::PluginDescription> found;

    for (auto* format : formats.getFormats())
        known.scanAndAddFile (file.getFullPathName(), true, found, *format);

    save();
    return known.getNumTypes() - before;
}

std::unique_ptr<juce::PluginDescription> PluginHost::find (const juce::String& id) const
{
    return known.getTypeForIdentifierString (id);
}

namespace
{
// The deprecatedUid JUCE assigns to a VST3 class: hash-31 over the TUID's 16 bytes as `char`
// (signed, like the SDK's TUID). FL stores the TUID in the same memory order.
int vst3Hash (const std::array<std::uint8_t, 16>& tuid)
{
    juce::uint32 value = 0;

    for (auto b : tuid)
        value = value * 31 + (juce::uint32) (int) (signed char) b;

    return (int) value;
}

// JUCE's "normalised" uniqueId: the same hash over the FUID's 4 integers (COM layout on Windows).
int vst3NormalisedHash (const std::array<std::uint8_t, 16>& d)
{
    auto make = [] (juce::uint32 a, juce::uint32 b, juce::uint32 c, juce::uint32 e) { return (a << 24) | (b << 16) | (c << 8) | e; };
    const juce::uint32 longs[] = { make (d[3], d[2], d[1], d[0]), make (d[5], d[4], d[7], d[6]),
                                   make (d[8], d[9], d[10], d[11]), make (d[12], d[13], d[14], d[15]) };
    juce::uint32 value = 0;

    for (auto l : longs)
        value = value * 31 + l;

    return (int) value;
}

juce::String plainName (const juce::String& s)
{
    return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
}
} // namespace

std::optional<juce::PluginDescription> PluginHost::findForeign (const ForeignPlugin& plugin, bool instrument, juce::String* how)
{
    const auto types = known.getTypes(); // a copy: a background scan may add types meanwhile
    auto found = [how] (const juce::PluginDescription& d, const char* reason) {
        if (how != nullptr)
            *how = reason;
        return std::optional<juce::PluginDescription> (d);
    };
    auto fileName = plainName (juce::File::createFileWithoutCheckingPath (
        juce::String::fromUTF8 (plugin.path.c_str()).replaceCharacter ('\\', '/')).getFileNameWithoutExtension());
    auto name = plainName (juce::String::fromUTF8 (plugin.name.c_str()));
    // a Waves shell holds many plugins: its file name says nothing; the sub-plugin's name is in its state
    const auto shell = name.startsWith ("waveshell") || fileName.startsWith ("waveshell");
    if (shell)
    {
        fileName = {};
        name = plainName (juce::String::fromUTF8 (wavesPluginName (pluginIdentity (plugin).state).c_str()));
    }
    // a list entry whose file is gone (an uninstalled older version next to the current one, round 113) never matches:
    // loading it fails and the slot would play nothing
    auto present = [] (const juce::PluginDescription& d) {
        return ! juce::File::isAbsolutePath (d.fileOrIdentifier) || juce::File (d.fileOrIdentifier).exists();
    };
    // 1) the same plugin, by its format identifier
    for (const auto& d : types)
    {
        if (! present (d))
            continue;
        if (plugin.format == ForeignPlugin::Format::vst3 && plugin.hasClassId && d.pluginFormatName == "VST3"
            && (d.deprecatedUid == vst3Hash (plugin.classId) || d.uniqueId == vst3NormalisedHash (plugin.classId)))
            return found (d, TR_NOOP ("same VST3 class ID"));
        if (plugin.format == ForeignPlugin::Format::vst2 && plugin.vst2Id != 0 && d.pluginFormatName == Vst2PluginFormat::formatName
            && (juce::uint32) d.uniqueId == plugin.vst2Id)
            return found (d, TR_NOOP ("same VST2 ID"));
        if (plugin.format == ForeignPlugin::Format::clap && ! plugin.clapId.empty() && d.pluginFormatName == ClapPluginFormat::formatName
            && d.uniqueId == juce::String::fromUTF8 (plugin.clapId.c_str()).hashCode())
            return found (d, TR_NOOP ("same CLAP id"));
    }
    // 2) the file FL Studio uses for it on this PC (its plugin database), or the very file the project saved
    auto sameFile = [&] (const std::string& path) -> const juce::PluginDescription* {
        const auto p = juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', '\\');
        if (p.isEmpty() || ! juce::File::isAbsolutePath (p))
            return nullptr;
        const juce::File file (p);
        for (const auto& d : types)
            if (d.isInstrument == instrument && juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier) == file && present (d))
                return &d;
        return nullptr;
    };
    // (not for a Waves shell: its file holds many plugins, the first one of the file would be the wrong one)
    if (! shell)
    {
        if (const auto* entry = flPluginDatabase().match (plugin, instrument))
            if (const auto* d = sameFile (entry->path))
                return found (*d, TR_NOOP ("FL Studio's plugin database"));
        if (const auto* d = sameFile (plugin.path))
            return found (*d, TR_NOOP ("same file as in the project"));
    }
    // 3) fallback, since FL swaps VST2 <-> VST3: same name (plugin or file), same role
    if (name.isEmpty() && fileName.isEmpty())
        return std::nullopt;
    // a plugin installed in several formats: prefer the one FL used (VST3 → VST3, CLAP → CLAP), whose state carries over
    const auto sameFormat = plugin.format == ForeignPlugin::Format::vst3   ? juce::String ("VST3")
                            : plugin.format == ForeignPlugin::Format::clap ? juce::String (ClapPluginFormat::formatName)
                            : plugin.format == ForeignPlugin::Format::vst2 ? juce::String (Vst2PluginFormat::formatName)
                                                                           : juce::String();
    std::optional<juce::PluginDescription> byName;
    for (const auto& d : types)
    {
        if (d.isInstrument != instrument || ! present (d))
            continue;
        const auto candidate = plainName (d.name);
        const auto candidateFile = plainName (juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier).getFileNameWithoutExtension());
        if (shell)
        {
            // "RComp" -> "RComp Stereo" / "RComp Mono"; the project's channel layout isn't known: stereo first
            if (name.isNotEmpty() && (candidate == name + "stereo" || candidate == name))
                return found (d, TR_NOOP ("same Waves plugin"));
            if (name.isNotEmpty() && candidate == name + "mono" && ! byName)
                byName = d;
            continue;
        }
        if ((name.isNotEmpty() && (candidate == name || candidateFile == name))
            || (fileName.isNotEmpty() && (candidate == fileName || candidateFile == fileName)))
        {
            if (d.pluginFormatName == sameFormat)
                return found (d, TR_NOOP ("same name"));
            if (! byName)
                byName = d;
        }
    }
    if (byName && how != nullptr)
        *how = TR_NOOP ("same name, other format");
    return byName;
}

juce::StringArray PluginHost::filesToScanFor (const std::vector<FlpPluginUse>& uses)
{
    juce::StringArray files;
    std::vector<std::pair<const FlpPluginUse*, const FlPluginDbEntry*>> needed;
    for (const auto& use : uses)
        if (! findForeign (use.plugin, use.instrument))
            needed.push_back ({ &use, flPluginDatabase().match (use.plugin, use.instrument) });
    if (needed.empty())
        return files;

    auto hosted = [] (const juce::String& path) {
        return path.endsWithIgnoreCase (".vst3") || path.endsWithIgnoreCase (".clap") || path.endsWithIgnoreCase (".dll");
    };
    auto addExisting = [&] (const std::string& path) {
        const auto p = juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', '\\');
        if (p.isNotEmpty() && juce::File::isAbsolutePath (p) && hosted (p) && juce::File (p).exists())
            files.addIfNotAlreadyThere (juce::File (p).getFullPathName(), true);
    };
    for (const auto& [use, entry] : needed)
    {
        addExisting (use->plugin.path);
        if (entry != nullptr)
            addExisting (entry->path);
    }
    // same-named plugin files in the scan folders (listing only: nothing is loaded here)
    const auto folders = pluginFolders();
    for (auto* format : formats.getFormats())
        if (format->canScanForPlugins())
            for (const auto& file : format->searchPathsForPlugins (folders, true, false))
                for (const auto& [use, entry] : needed)
                    if (pluginFileMatches (file.toStdString(), use->plugin, entry))
                        files.addIfNotAlreadyThere (file, true);
    // known to crash or hang: nothing to learn
    juce::StringArray worth;
    for (const auto& file : files)
        if (! known.getBlacklistedFiles().contains (file))
            worth.add (file);
    worth.removeRange (32, worth.size()); // a project rarely needs more; the rest waits for the background scan
    return worth;
}

int PluginHost::adoptFlPlugins (Song& song, juce::StringArray* missing, juce::StringArray* matched, juce::StringArray* withoutState,
                                juce::StringArray* substituted)
{
    int found = 0;

    // returns the state in JUCE's format, or empty when the state can't be carried over
    auto convertState = [] (const ForeignPlugin& f, const juce::PluginDescription& d) -> std::string {
        // CLAP: u32 0 + the clap_plugin_state.save bytes (REA_rodada22 §2); LYNX's CLAP host stores the same
        if (f.format == ForeignPlugin::Format::clap && d.pluginFormatName == "CLAP")
            return f.state.size() > 4 ? juce::MemoryBlock (f.state.data() + 4, f.state.size() - 4).toBase64Encoding().toStdString() : std::string {};

        // VST2: LYNX's VST2 host keeps FL's own state layout (REA_rodada89 §2)
        if (f.format == ForeignPlugin::Format::vst2 && d.pluginFormatName == Vst2PluginFormat::formatName)
        {
            FlVst2State check;
            return parseFlVst2State (f.state, check) ? juce::MemoryBlock (f.state.data(), f.state.size()).toBase64Encoding().toStdString()
                                                     : std::string {};
        }

        if (f.format != ForeignPlugin::Format::vst3 || d.pluginFormatName != "VST3")
            return {}; // VST2 <-> VST3: different state formats; plugin at defaults

        std::vector<std::uint8_t> component, controller;

        if (! splitFlVst3State (f.state, component, controller))
            return {};

        juce::XmlElement xml ("VST3PluginState");

        if (! component.empty())
            xml.createNewChildElement ("IComponent")->addTextElement (juce::MemoryBlock (component.data(), component.size()).toBase64Encoding());
        if (! controller.empty())
            xml.createNewChildElement ("IEditController")->addTextElement (juce::MemoryBlock (controller.data(), controller.size()).toBase64Encoding());

        juce::MemoryBlock binary;
        juce::AudioProcessor::copyXmlToBinary (xml, binary);
        return binary.toBase64Encoding().toStdString();
    };

    auto adopt = [&] (PluginRef& ref, bool instrument) {
        juce::String how;
        const auto d = findForeign (ref.foreign, instrument, &how);

        if (! d)
        {
            if (missing != nullptr)
                missing->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()));
            ref.foreign = {};
            return false;
        }

        if (matched != nullptr)
            matched->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()) + " -> " + d->fileOrIdentifier + " (" + tr (how) + ")");
        ref.id = d->createIdentifierString().toStdString();
        ref.name = d->name.toStdString();
        ref.state = convertState (ref.foreign, *d);
        // found, but the project's settings could not be carried over: it plays its default sound
        if (ref.state.empty() && ! ref.foreign.state.empty() && withoutState != nullptr)
            withoutState->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()) + " (" + d->pluginFormatName + ")");
        ref.foreign = {};
        ++found;
        return true;
    };

    // FL automates a VST3 by "slot" (record 4: slot → ParamID); LYNX by position in JUCE's list, which is
    // getParameterInfo order (REA_rodada22 §2.3). Needs the plugin's current list: only when something is
    // automated, in-process (skipped with the sandbox on, where k stays the position — FL's default for a new instance).
    auto remapVst3Slots = [&] (const ForeignPlugin& f, const juce::PluginDescription& d, auto&& isThisPlugin) {
        if (f.format != ForeignPlugin::Format::vst3 || d.pluginFormatName != "VST3" || pluginSandboxEnabled())
            return;
        const auto saved = flVst3ParamSlots (f.state);
        auto automated = false;
        song.forEachTarget ([&] (AutomationTarget& t) { automated = automated || isThisPlugin (t); });
        if (saved.empty() || ! automated)
            return;

        juce::String error;
        const auto instance = formats.createPluginInstance (d, 44100.0, 512, error);
        if (instance == nullptr)
            return;
        std::vector<std::uint32_t> ids;
        for (auto* p : instance->getParameters())
        {
            const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (p);
            ids.push_back (hosted != nullptr ? (std::uint32_t) hosted->getParameterID().getLargeIntValue() : 0xffffffffu);
        }
        const auto positions = flVst3SlotPositions (saved, ids);
        song.forEachTarget ([&] (AutomationTarget& t) {
            if (isThisPlugin (t))
                t.param = t.param >= 0 && t.param < (int) positions.size() ? positions[(std::size_t) t.param] : -1;
        });
    };

    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        auto& channel = song.channels[(std::size_t) c];
        if (channel.plugin.foreign.format == ForeignPlugin::Format::none)
            continue;
        const auto foreign = channel.plugin.foreign;
        if (adopt (channel.plugin, true))
        {
            channel.instrument = InstrumentKind::plugin;
            if (const auto d = findForeign (foreign, true))
                remapVst3Slots (foreign, *d, [c] (const AutomationTarget& t) { return t.kind == AutomationTargetKind::channelParam && t.index == c; });
        }
    }

    // the song's key etc., for substitutes whose own original doesn't say (round 113)
    const auto context = substituteContextOf (song);
    // backwards: removing an unmatched slot shifts only the slots after it (their automations follow)
    for (int track = 0; track < (int) song.mixer.size(); ++track)
        for (int slot = (int) song.mixer[(std::size_t) track].effects.size() - 1; slot >= 0; --slot)
        {
            auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
            if (fx.kind != EffectKind::plugin || fx.plugin.foreign.format == ForeignPlugin::Format::none)
                continue;
            const auto foreign = fx.plugin.foreign;
            // not installed: a LYNX substitute plays in its place and keeps the original record for later
            if (pluginSubstitutesEnabled() && ! findForeign (foreign, false) && substituteEffectSlot (fx, pluginIdentity (foreign), context))
            {
                if (substituted != nullptr)
                    substituted->addIfNotAlreadyThere (juce::String::fromUTF8 (substituteOriginalName (fx).c_str()) + " -> "
                                                       + juce::String::fromUTF8 (effectInfo (fx.kind).name));
                continue;
            }
            if (! adopt (fx.plugin, false))
                song.removeEffectSlot (track, slot);
            else if (const auto d = findForeign (foreign, false))
                remapVst3Slots (foreign, *d, [track, slot] (const AutomationTarget& t) {
                    return t.kind == AutomationTargetKind::effectParam && t.index == track && t.slot == slot;
                });
        }

    return found;
}

PluginIdentity PluginHost::identityOf (const PluginRef& ref)
{
    if (ref.foreign.format != ForeignPlugin::Format::none)
        return pluginIdentity (ref.foreign);

    PluginIdentity id;
    id.name = ref.name;
    // JUCE's identifier: "<format>-<name>-<file hash>-<unique id>" (hex)
    const auto text = juce::String::fromUTF8 (ref.id.c_str());
    const auto format = text.upToFirstOccurrenceOf ("-", false, false);
    id.format = format == "VST3"                           ? ForeignPlugin::Format::vst3
                : format == Vst2PluginFormat::formatName   ? ForeignPlugin::Format::vst2
                : format == ClapPluginFormat::formatName   ? ForeignPlugin::Format::clap
                                                           : ForeignPlugin::Format::other;
    if (id.format == ForeignPlugin::Format::vst2)
        id.vst2Id = (std::uint32_t) text.fromLastOccurrenceOf ("-", false, false).getHexValue64();

    juce::MemoryBlock state;
    if (ref.state.empty() || ! state.fromBase64Encoding (juce::String::fromUTF8 (ref.state.c_str())))
        return id;
    const auto* bytes = static_cast<const std::uint8_t*> (state.getData());
    if (id.format == ForeignPlugin::Format::vst3)
    {
        // JUCE's VST3 state: XML "VST3PluginState" whose IComponent element holds the component state in base64
        if (const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            if (const auto* component = xml->getChildByName ("IComponent"))
            {
                juce::MemoryBlock raw;
                if (raw.fromBase64Encoding (component->getAllSubText().trim()))
                    id.state.assign (static_cast<const std::uint8_t*> (raw.getData()), static_cast<const std::uint8_t*> (raw.getData()) + raw.getSize());
            }
    }
    else
        id.state.assign (bytes, bytes + state.getSize()); // VST2: FL's layout (LYNX's VST2 host); CLAP: the plugin's own
    return id;
}

int PluginHost::applySubstitutes (Song& song, int* restored, int* substituted)
{
    int back = 0, replaced = 0;
    bool foreignBack = false;
    const auto substitute = pluginSubstitutesEnabled();
    const auto context = substituteContextOf (song);
    for (auto& track : song.mixer)
        for (auto& fx : track.effects)
        {
            if (fx.isSubstitute())
            {
                const auto& original = fx.substituteFor;
                const auto installed = original.foreign.format != ForeignPlugin::Format::none
                                           ? findForeign (original.foreign, false).has_value()
                                           : find (juce::String::fromUTF8 (original.id.c_str())) != nullptr;
                if (installed)
                {
                    foreignBack = foreignBack || original.foreign.format != ForeignPlugin::Format::none;
                    restoreOriginalPlugin (fx);
                    ++back;
                }
            }
            else if (substitute && fx.kind == EffectKind::plugin && fx.plugin.foreign.format == ForeignPlugin::Format::none
                     && ! fx.plugin.id.empty() && find (juce::String::fromUTF8 (fx.plugin.id.c_str())) == nullptr)
            {
                if (substituteEffectSlot (fx, identityOf (fx.plugin), context))
                    ++replaced;
            }
        }
    // originals that came from an .flp: their FL record still needs converting to the installed plugin's state
    if (foreignBack)
        adoptFlPlugins (song);
    if (restored != nullptr)
        *restored = back;
    if (substituted != nullptr)
        *substituted = replaced;
    return back + replaced;
}

bool pluginSubstitutesEnabled()
{
    return PluginHost::settingsFolder().getChildFile ("substitutes.txt").loadFileAsString().trim() != "0";
}

void setPluginSubstitutesEnabled (bool enabled)
{
    PluginHost::settingsFolder().getChildFile ("substitutes.txt").replaceWithText (enabled ? "1" : "0");
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiate (const PluginRef& ref, double sampleRate, juce::String* error)
{
    const auto description = find (juce::String::fromUTF8 (ref.id.c_str()));

    if (description == nullptr)
    {
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + juce::String::fromUTF8 (": plugin not installed or not scanned");
        return nullptr;
    }

    loadedAny.store (true); // (even a failed load may leave the module mapped)
    juce::String message;
    if (pluginSandboxEnabled())
    {
        // separate process: the child prepares the plugin and applies the state
        if (auto remote = createSandboxedPlugin (*description, sampleRate, maxBlockSize, ref.state, message))
            return remote;
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + ": " + message;
        return nullptr;
    }

    auto instance = formats.createPluginInstance (*description, sampleRate, maxBlockSize, message);

    if (instance == nullptr)
    {
        if (error != nullptr)
            *error = message;
        return nullptr;
    }

    preparePlugin (*instance, sampleRate, maxBlockSize);

    if (! ref.state.empty())
    {
        juce::MemoryBlock state;

        if (state.fromBase64Encoding (juce::String::fromUTF8 (ref.state.c_str())))
            instance->setStateInformation (state.getData(), (int) state.getSize());
    }

    return instance;
}

namespace
{
juce::String failureLine (const PluginRef& ref, const juce::String& why)
{
    const auto name = juce::String::fromUTF8 (ref.name.c_str());
    if (why.isEmpty())
        return name + ": " + tr ("the plugin could not start");
    return why.startsWith (name) ? why : name + ": " + why;
}
} // namespace

juce::StringArray PluginHost::getLoadFailures() const
{
    const std::scoped_lock lock (failuresLock);
    return loadFailures;
}

void PluginHost::clearLoadFailures()
{
    const std::scoped_lock lock (failuresLock);
    loadFailures.clear();
}

std::shared_ptr<Instrument> PluginHost::makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error)
{
    juce::String why;
    if (auto instance = instantiate (ref, sampleRate, &why))
        return std::make_shared<PluginInstrument> (std::move (instance), maxBlockSize);

    {
        const std::scoped_lock lock (failuresLock);
        loadFailures.addIfNotAlreadyThere (failureLine (ref, why));
    }
    if (error != nullptr)
        *error = why;
    return nullptr;
}

std::shared_ptr<Effect> PluginHost::makeEffect (const PluginRef& ref, double sampleRate, juce::String* error)
{
    juce::String why;
    if (auto instance = instantiate (ref, sampleRate, &why))
        return std::make_shared<PluginEffect> (std::move (instance), maxBlockSize);

    {
        const std::scoped_lock lock (failuresLock);
        loadFailures.addIfNotAlreadyThere (failureLine (ref, why));
    }
    if (error != nullptr)
        *error = why;
    return nullptr;
}

std::string capturePluginState (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    return state.toBase64Encoding().toStdString();
}
} // namespace lynx
