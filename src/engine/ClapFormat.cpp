#include "engine/ClapFormat.h"

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>

#include <juce_gui_extra/juce_gui_extra.h>

#include <clap/clap.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace lynx
{
namespace
{
// --- module (the .clap DLL) --------------------------------------------------
// clap_entry.init/deinit apply once per DLL: a loaded module is shared by
// all instances and unloaded when the last one goes away.
class ClapModule
{
public:
    static std::shared_ptr<ClapModule> open (const juce::String& path)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<ClapModule>> modules;
        const std::lock_guard<std::mutex> lock (mutex);

        if (auto alive = modules[path].lock())
            return alive;

        auto module = std::shared_ptr<ClapModule> (new ClapModule());

        if (! module->library.open (path))
            return nullptr;

        const auto* entry = static_cast<const clap_plugin_entry_t*> (module->library.getFunction ("clap_entry"));

        if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version) || ! entry->init (path.toRawUTF8()))
            return nullptr;

        module->entry = entry;
        module->factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

        if (module->factory == nullptr)
            return nullptr;

        modules[path] = module;
        return module;
    }

    ~ClapModule()
    {
        if (entry != nullptr)
            entry->deinit();

        library.close();
    }

    const clap_plugin_factory_t* factory = nullptr;

private:
    ClapModule() = default;

    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
};

bool hasFeature (const clap_plugin_descriptor_t& d, const char* feature)
{
    for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
        if (std::strcmp (*f, feature) == 0)
            return true;

    return false;
}

int idHash (const char* pluginId)
{
    return juce::String::fromUTF8 (pluginId).hashCode();
}

juce::PluginDescription describe (const clap_plugin_descriptor_t& d, const juce::File& file, int pluginsInFile)
{
    juce::PluginDescription p;
    p.name = juce::String::fromUTF8 (d.name);
    p.descriptiveName = juce::String::fromUTF8 (d.description != nullptr ? d.description : d.name);
    p.pluginFormatName = ClapPluginFormat::formatName;
    p.manufacturerName = juce::String::fromUTF8 (d.vendor != nullptr ? d.vendor : "");
    p.version = juce::String::fromUTF8 (d.version != nullptr ? d.version : "");
    p.fileOrIdentifier = file.getFullPathName();
    p.lastFileModTime = file.getLastModificationTime();
    p.lastInfoUpdateTime = juce::Time::getCurrentTime();
    p.uniqueId = p.deprecatedUid = idHash (d.id);
    p.isInstrument = hasFeature (d, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    p.numInputChannels = p.isInstrument ? 0 : 2;
    p.numOutputChannels = 2;
    p.hasSharedContainer = pluginsInFile > 1;

    juce::StringArray features;
    for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
        features.add (juce::String::fromUTF8 (*f));
    p.category = features.joinIntoString ("|");
    return p;
}

// --- input event list (no allocation on the audio thread) -------------------------
class EventList
{
public:
    EventList()
    {
        pool.resize (64 * 1024);
        offsets.reserve (4096);
        list.ctx = this;
        list.size = [] (const clap_input_events_t* l) { return (uint32_t) static_cast<EventList*> (l->ctx)->offsets.size(); };
        list.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* {
            auto* self = static_cast<EventList*> (l->ctx);
            return i < self->offsets.size() ? reinterpret_cast<const clap_event_header_t*> (self->pool.data() + self->offsets[i])
                                            : nullptr;
        };
    }

    void clear() noexcept
    {
        offsets.clear();
        used = 0;
    }

    template <typename Event>
    void add (const Event& e) noexcept
    {
        const auto size = (sizeof (Event) + 7) & ~(std::size_t) 7;

        if (used + size > pool.size() || offsets.size() == offsets.capacity())
            return; // full: drop it (never allocates)

        std::memcpy (pool.data() + used, &e, sizeof (Event));
        offsets.push_back ((uint32_t) used);
        used += size;
    }

    const clap_input_events_t* get() const noexcept { return &list; }

private:
    std::vector<std::uint8_t> pool;
    std::vector<uint32_t> offsets;
    std::size_t used = 0;
    clap_input_events_t list {};
};

template <typename Event>
Event makeEvent (uint16_t type, uint32_t time)
{
    Event e {};
    e.header.size = sizeof (Event);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = type;
    e.header.flags = 0;
    return e;
}

class ClapInstance;

// --- parameter ---------------------------------------------------------------------
class ClapParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapInstance& o, const clap_param_info_t& info, double current)
        : owner (o),
          id (info.id),
          cookie (info.cookie),
          minimum (info.min_value),
          maximum (info.max_value),
          defaultValue (info.default_value),
          flags (info.flags),
          name (juce::String::fromUTF8 (info.name))
    {
        normalised.store (toNormalised (current));
    }

    juce::String getParameterID() const override { return juce::String ((juce::int64) id); }
    float getValue() const override { return normalised.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override { return toNormalised (defaultValue); }
    juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
    juce::String getLabel() const override { return {}; }
    float getValueForText (const juce::String& text) const override { return toNormalised (text.getDoubleValue()); }
    juce::String getText (float v, int) const override;
    bool isAutomatable() const override { return (flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
    bool isDiscrete() const override { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
    int getNumSteps() const override
    {
        return isDiscrete() ? juce::jmax (2, (int) (maximum - minimum) + 1) : AudioProcessorParameter::getNumSteps();
    }

    double toPlain (float v) const noexcept { return minimum + (maximum - minimum) * (double) v; }
    float toNormalised (double plain) const noexcept
    {
        return maximum > minimum ? (float) juce::jlimit (0.0, 1.0, (plain - minimum) / (maximum - minimum)) : 0.0f;
    }

    void setFromPlugin (double plain) noexcept { normalised.store (toNormalised (plain)); }

    ClapInstance& owner;
    const clap_id id;
    void* const cookie;

private:
    const double minimum, maximum, defaultValue;
    const uint32_t flags;
    const juce::String name;
    std::atomic<float> normalised { 0.0f };
};

// --- plugin UI window ----------------------------------------------------------------
class ClapEditor;

// --- instance ------------------------------------------------------------------------
class ClapInstance final : public juce::AudioPluginInstance
{
public:
    ClapInstance (std::shared_ptr<ClapModule> m, const clap_plugin_t* p, const juce::PluginDescription& d)
        : AudioPluginInstance (busesFor (d)),
          plugin (p),
          module (std::move (m)),
          description (d)
    {
    }

    // Second phase (needs `this` ready as host_data): init + extensions + parameters.
    bool initialise()
    {
        if (! plugin->init (plugin))
            return false;

        initialised = true;
        params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
        gui = static_cast<const clap_plugin_gui_t*> (plugin->get_extension (plugin, CLAP_EXT_GUI));
        latency = static_cast<const clap_plugin_latency_t*> (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
        audioPorts = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));

        if (const auto* notes = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS)))
        {
            clap_note_port_info_t info {};

            if (notes->count (plugin, true) > 0 && notes->get (plugin, 0, true, &info))
                midiDialect = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0;
        }

        inputChannels = mainPortChannels (true, description.isInstrument ? 0 : 2);
        outputChannels = juce::jmax (1, mainPortChannels (false, 2));

        if (gui != nullptr && ! gui->is_api_supported (plugin, CLAP_WINDOW_API_WIN32, false))
            gui = nullptr;

        if (params != nullptr)
        {
            const auto count = params->count (plugin);

            for (uint32_t i = 0; i < count; ++i)
            {
                clap_param_info_t info {};

                if (! params->get_info (plugin, i, &info) || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
                    continue;

                double value = info.default_value;
                params->get_value (plugin, info.id, &value);
                auto parameter = std::make_unique<ClapParameter> (*this, info, value);
                byId[info.id] = parameter.get();
                addHostedParameter (std::move (parameter));
            }
        }

        return true;
    }

    ~ClapInstance() override
    {
        *alive = false;

        if (active)
        {
            if (processing)
                plugin->stop_processing (plugin);
            plugin->deactivate (plugin);
        }

        if (plugin != nullptr)
            plugin->destroy (plugin);
    }

    static const clap_host_t* hostFor (ClapInstance* self) { return &self->host; }

    // --- AudioProcessor -------------------------------------------------------------
    const juce::String getName() const override { return description.name; }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        releaseResources();

        if (! plugin->activate (plugin, sampleRate, 1, (uint32_t) juce::jmax (1, blockSize)))
            return;

        active = true;
        maxBlock = juce::jmax (1, blockSize);
        scratch.setSize (juce::jmax (2, outputChannels), maxBlock);

        if (latency != nullptr)
            setLatencySamples ((int) latency->get (plugin));
    }

    void releaseResources() override
    {
        if (! active)
            return;

        if (processing)
            plugin->stop_processing (plugin);

        plugin->deactivate (plugin);
        processing = false;
        active = false;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const auto n = buffer.getNumSamples();
        audioThread.store (std::this_thread::get_id());

        if (! active || n > maxBlock)
        {
            buffer.clear();
            return;
        }

        if (! processing)
            processing = plugin->start_processing (plugin);

        events.clear();

        // parameters changed by the host (automation, UI)
        PendingParam change;
        while (pendingParams.pop (change))
        {
            auto e = makeEvent<clap_event_param_value_t> (CLAP_EVENT_PARAM_VALUE, 0);
            e.param_id = change.id;
            e.cookie = change.cookie;
            e.note_id = -1;
            e.port_index = e.channel = e.key = -1;
            e.value = change.value;
            events.add (e);
        }

        for (const auto m : midi)
            addMidi (m.getMessage(), (uint32_t) juce::jlimit (0, n - 1, m.samplePosition));

        std::array<float*, 16> inputs {};
        std::array<float*, 16> outputs {};

        for (int ch = 0; ch < inputChannels && ch < (int) inputs.size(); ++ch)
            inputs[(std::size_t) ch] = buffer.getWritePointer (juce::jmin (ch, buffer.getNumChannels() - 1));

        for (int ch = 0; ch < outputChannels && ch < (int) outputs.size(); ++ch)
        {
            outputs[(std::size_t) ch] = scratch.getWritePointer (ch);
            juce::FloatVectorOperations::clear (outputs[(std::size_t) ch], n);
        }

        clap_audio_buffer_t in {};
        in.data32 = inputs.data();
        in.channel_count = (uint32_t) inputChannels;
        clap_audio_buffer_t out {};
        out.data32 = outputs.data();
        out.channel_count = (uint32_t) outputChannels;

        clap_output_events_t outEvents {};
        outEvents.ctx = this;
        outEvents.try_push = [] (const clap_output_events_t* list, const clap_event_header_t* e) {
            auto* self = static_cast<ClapInstance*> (list->ctx);

            if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
            {
                const auto* v = reinterpret_cast<const clap_event_param_value_t*> (e);

                if (auto it = self->byId.find (v->param_id); it != self->byId.end())
                    it->second->setFromPlugin (v->value);
            }

            return true;
        };

        clap_process_t process {};
        process.steady_time = steadyTime;
        process.frames_count = (uint32_t) n;
        process.audio_inputs = inputChannels > 0 ? &in : nullptr;
        process.audio_inputs_count = inputChannels > 0 ? 1u : 0u;
        process.audio_outputs = &out;
        process.audio_outputs_count = 1;
        process.in_events = events.get();
        process.out_events = &outEvents;

        plugin->process (plugin, &process);
        steadyTime += n;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, 0, scratch, juce::jmin (ch, outputChannels - 1), 0, n);
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return description.isInstrument; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return gui != nullptr; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        if (state == nullptr)
            return;

        juce::MemoryOutputStream out (data, false);
        clap_ostream_t stream {};
        stream.ctx = &out;
        stream.write = [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t {
            return static_cast<juce::MemoryOutputStream*> (s->ctx)->write (buffer, (size_t) size) ? (int64_t) size : -1;
        };
        state->save (plugin, &stream);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (state == nullptr)
            return;

        juce::MemoryInputStream in (data, (size_t) size, false);
        clap_istream_t stream {};
        stream.ctx = &in;
        stream.read = [] (const clap_istream_t* s, void* buffer, uint64_t bytes) -> int64_t {
            return static_cast<juce::MemoryInputStream*> (s->ctx)->read (buffer, (int) juce::jmin<uint64_t> (bytes, 1 << 30));
        };

        if (state->load (plugin, &stream) && params != nullptr)
            for (auto& [paramId, parameter] : byId)
            {
                double value = 0.0;

                if (params->get_value (plugin, paramId, &value))
                    parameter->setFromPlugin (value);
            }
    }

    // --- used by the parameters and the editor ---------------------------------------------
    void queueParam (clap_id id, void* cookie, double value) { pendingParams.push ({ id, cookie, value }); }

    juce::String paramText (clap_id id, double value) const
    {
        char text[256] {};

        if (params != nullptr && juce::MessageManager::existsAndIsCurrentThread() && params->value_to_text (plugin, id, value, text, sizeof (text)))
            return juce::String::fromUTF8 (text);

        return juce::String (value, 3);
    }

    const clap_plugin_t* plugin;
    const clap_plugin_gui_t* gui = nullptr;
    juce::Component::SafePointer<juce::Component> editor;

private:
    struct PendingParam
    {
        clap_id id = 0;
        void* cookie = nullptr;
        double value = 0.0;
    };

    // single-producer queue (UI thread) to the audio thread
    class ParamQueue
    {
    public:
        bool push (const PendingParam& p) noexcept
        {
            const auto w = write.load();
            const auto next = (w + 1) % items.size();

            if (next == read.load())
                return false;

            items[w] = p;
            write.store (next);
            return true;
        }

        bool pop (PendingParam& p) noexcept
        {
            const auto r = read.load();

            if (r == write.load())
                return false;

            p = items[r];
            read.store ((r + 1) % items.size());
            return true;
        }

    private:
        std::array<PendingParam, 1024> items {};
        std::atomic<std::size_t> write { 0 }, read { 0 };
    };

    static BusesProperties busesFor (const juce::PluginDescription& d)
    {
        BusesProperties buses;

        if (! d.isInstrument)
            buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(), true);

        return buses.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    int mainPortChannels (bool input, int fallback) const
    {
        if (audioPorts == nullptr)
            return fallback;

        if (audioPorts->count (plugin, input) == 0)
            return 0;

        clap_audio_port_info_t info {};
        return audioPorts->get (plugin, 0, input, &info) ? juce::jmin (16, (int) info.channel_count) : fallback;
    }

    void addMidi (const juce::MidiMessage& m, uint32_t time)
    {
        const auto channel = (int16_t) juce::jlimit (0, 15, m.getChannel() - 1);

        if (! midiDialect && (m.isNoteOn() || m.isNoteOff()))
        {
            const auto on = m.isNoteOn();
            auto e = makeEvent<clap_event_note_t> (on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, time);
            e.note_id = -1;
            e.port_index = 0;
            e.channel = channel;
            e.key = (int16_t) m.getNoteNumber();
            e.velocity = on ? m.getFloatVelocity() : 0.0;
            events.add (e);
            heldNotes[(std::size_t) channel * 128 + (std::size_t) m.getNoteNumber()] = on;
            return;
        }

        if (! midiDialect && (m.isAllNotesOff() || m.isAllSoundOff()))
        {
            // CLAP has no "all notes off": release each held note
            for (std::size_t i = 0; i < heldNotes.size(); ++i)
                if (heldNotes[i])
                {
                    auto e = makeEvent<clap_event_note_t> (m.isAllSoundOff() ? CLAP_EVENT_NOTE_CHOKE : CLAP_EVENT_NOTE_OFF, time);
                    e.note_id = -1;
                    e.port_index = 0;
                    e.channel = (int16_t) (i / 128);
                    e.key = (int16_t) (i % 128);
                    events.add (e);
                    heldNotes[i] = false;
                }
            return;
        }

        if (m.getRawDataSize() <= 3)
        {
            auto e = makeEvent<clap_event_midi_t> (CLAP_EVENT_MIDI, time);
            e.port_index = 0;
            std::memcpy (e.data, m.getRawData(), (std::size_t) m.getRawDataSize());
            events.add (e);
        }
    }

    // --- the host as seen by the plugin -----------------------------------------------------
    static ClapInstance* from (const clap_host_t* h) { return static_cast<ClapInstance*> (h->host_data); }

    void onMainThread (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([flag = alive, fn = std::move (fn)] {
            if (*flag)
                fn();
        });
    }

    clap_host_t makeHost()
    {
        clap_host_t h {};
        h.clap_version = CLAP_VERSION;
        h.host_data = this;
        h.name = "LYNX";
        h.vendor = "LYNX";
        h.url = "";
        h.version = JUCE_APPLICATION_VERSION_STRING;
        h.get_extension = [] (const clap_host_t* hh, const char* id) -> const void* {
            auto* self = from (hh);

            if (std::strcmp (id, CLAP_EXT_LOG) == 0) return &self->hostLog;
            if (std::strcmp (id, CLAP_EXT_THREAD_CHECK) == 0) return &self->hostThreadCheck;
            if (std::strcmp (id, CLAP_EXT_PARAMS) == 0) return &self->hostParams;
            if (std::strcmp (id, CLAP_EXT_STATE) == 0) return &self->hostState;
            if (std::strcmp (id, CLAP_EXT_LATENCY) == 0) return &self->hostLatency;
            if (std::strcmp (id, CLAP_EXT_GUI) == 0) return &self->hostGui;
            return nullptr;
        };
        h.request_restart = [] (const clap_host_t* hh) {
            // restart on the UI thread with the same settings (new latency, ports...)
            auto* self = from (hh);
            self->onMainThread ([self] {
                if (self->active)
                {
                    const auto rate = self->getSampleRate();
                    const auto block = self->maxBlock;
                    self->suspendProcessing (true);
                    self->prepareToPlay (rate, block);
                    self->suspendProcessing (false);
                }
            });
        };
        h.request_process = [] (const clap_host_t*) {};
        h.request_callback = [] (const clap_host_t* hh) {
            auto* self = from (hh);
            self->onMainThread ([self] { self->plugin->on_main_thread (self->plugin); });
        };
        return h;
    }

    std::shared_ptr<ClapModule> module;
    juce::PluginDescription description;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

public:
    clap_host_t host = makeHost();

private:
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    const clap_plugin_audio_ports_t* audioPorts = nullptr;
    bool midiDialect = false;
    bool initialised = false;

    bool active = false, processing = false;
    int maxBlock = 0;
    int inputChannels = 0, outputChannels = 2;
    int64_t steadyTime = 0;
    juce::AudioBuffer<float> scratch;
    EventList events;
    ParamQueue pendingParams;
    std::unordered_map<clap_id, ClapParameter*> byId;
    std::array<bool, 16 * 128> heldNotes {};
    std::atomic<std::thread::id> audioThread {};

    clap_host_log_t hostLog { [] (const clap_host_t*, clap_log_severity, const char* msg) { juce::ignoreUnused (msg); DBG ("CLAP: " << msg); } };
    clap_host_thread_check_t hostThreadCheck {
        [] (const clap_host_t*) { return juce::MessageManager::existsAndIsCurrentThread(); },
        [] (const clap_host_t* hh) { return from (hh)->audioThread.load() == std::this_thread::get_id(); }
    };
    clap_host_params_t hostParams {
        [] (const clap_host_t*, clap_param_rescan_flags) {},
        [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
        [] (const clap_host_t*) {}
    };
    clap_host_state_t hostState { [] (const clap_host_t*) {} };
    clap_host_latency_t hostLatency { [] (const clap_host_t* hh) {
        auto* self = from (hh);
        self->onMainThread ([self] {
            if (self->latency != nullptr && self->active)
                self->setLatencySamples ((int) self->latency->get (self->plugin));
        });
    } };
    clap_host_gui_t hostGui {
        [] (const clap_host_t*) {},
        [] (const clap_host_t* hh, uint32_t w, uint32_t h) {
            auto* self = from (hh);
            self->onMainThread ([self, w, h] {
                if (auto* e = self->editor.getComponent())
                {
                    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
                    e->setSize (juce::roundToInt (w / scale), juce::roundToInt (h / scale));
                }
            });
            return true;
        },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*) { return false; },
        [] (const clap_host_t*, bool) {}
    };
};

void ClapParameter::setValue (float v)
{
    normalised.store (v);
    owner.queueParam (id, cookie, toPlain (v));
}

juce::String ClapParameter::getText (float v, int) const
{
    return owner.paramText (id, toPlain (v));
}

// The plugin's native UI inside a child window (HWND) embedded in the component.
class ClapEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit ClapEditor (ClapInstance& p)
        : AudioProcessorEditor (p),
          owner (p)
    {
        const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
        setSize (480, 320);

        if (owner.gui != nullptr && owner.gui->create (owner.plugin, CLAP_WINDOW_API_WIN32, false))
        {
            created = true;
            owner.gui->set_scale (owner.plugin, scale);
            uint32_t w = 0, h = 0;

            if (owner.gui->get_size (owner.plugin, &w, &h) && w > 0 && h > 0)
                setSize (juce::roundToInt (w / scale), juce::roundToInt (h / scale));
        }

        addAndMakeVisible (holder);
        owner.editor = this;
        startTimer (20);
    }

    ~ClapEditor() override
    {
        stopTimer();

        if (created)
        {
            owner.gui->hide (owner.plugin);
            owner.gui->destroy (owner.plugin);
        }

        holder.setHWND (nullptr);

       #if JUCE_WINDOWS
        if (container != nullptr)
            DestroyWindow ((HWND) container);
       #endif
    }

    void resized() override { holder.setBounds (getLocalBounds()); }

    void paint (juce::Graphics& g) override
    {
        if (! created)
        {
            g.fillAll (juce::Colours::black);
            g.setColour (juce::Colours::white);
            g.drawText (juce::String::fromUTF8 ("This plugin has no UI of its own"), getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
       #if JUCE_WINDOWS
        // the child window needs a real window as parent: wait until on screen
        auto* peer = getPeer();

        if (! created || peer == nullptr || container != nullptr)
            return;

        stopTimer();
        const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
        container = CreateWindowExW (0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                                     juce::roundToInt (getWidth() * scale), juce::roundToInt (getHeight() * scale),
                                     (HWND) peer->getNativeHandle(), nullptr, nullptr, nullptr);
        holder.setHWND (container);
        holder.setBounds (getLocalBounds());

        clap_window_t window {};
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = container;
        owner.gui->set_parent (owner.plugin, &window);
        owner.gui->show (owner.plugin);
       #else
        stopTimer();
       #endif
    }

    ClapInstance& owner;
    juce::HWNDComponent holder;
    void* container = nullptr;
    bool created = false;
};

juce::AudioProcessorEditor* ClapInstance::createEditor()
{
    return gui != nullptr ? new ClapEditor (*this) : nullptr;
}
} // namespace

// --- format ----------------------------------------------------------------------------

void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path)
{
    if (! fileMightContainThisPluginType (path))
        return;

    const auto module = ClapModule::open (path);

    if (module == nullptr)
        return;

    const auto count = module->factory->get_plugin_count (module->factory);

    for (uint32_t i = 0; i < count; ++i)
        if (const auto* d = module->factory->get_plugin_descriptor (module->factory, i))
            results.add (new juce::PluginDescription (describe (*d, juce::File (path), (int) count)));
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& path)
{
    const juce::File f (path);
    return f.hasFileExtension (".clap") && f.exists();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& path)
{
    return juce::File (path).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool)
{
    juce::StringArray found;

    for (int i = 0; i < directories.getNumPaths(); ++i)
        for (const auto& f : directories[i].findChildFiles (juce::File::findFilesAndDirectories, recursive, "*.clap"))
            found.addIfNotAlreadyThere (f.getFullPathName());

    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
   #if JUCE_WINDOWS
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs/Common/CLAP"));
   #endif
    return paths;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& d, double sampleRate, int blockSize,
                                             PluginCreationCallback callback)
{
    auto module = ClapModule::open (d.fileOrIdentifier);

    if (module == nullptr)
    {
        callback (nullptr, juce::String::fromUTF8 ("could not load ") + d.fileOrIdentifier);
        return;
    }

    const auto count = module->factory->get_plugin_count (module->factory);

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* desc = module->factory->get_plugin_descriptor (module->factory, i);

        if (desc == nullptr || idHash (desc->id) != d.uniqueId)
            continue;

        // the host must exist before create_plugin; the instance is created with the plugin afterwards
        auto instance = std::unique_ptr<ClapInstance> (new ClapInstance (module, nullptr, d));
        const auto* plugin = module->factory->create_plugin (module->factory, ClapInstance::hostFor (instance.get()), desc->id);

        if (plugin == nullptr)
            break;

        instance->plugin = plugin;

        if (! instance->initialise())
        {
            callback (nullptr, juce::String::fromUTF8 ("the plugin refused to start"));
            return;
        }

        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        callback (std::move (instance), {});
        return;
    }

    callback (nullptr, juce::String::fromUTF8 ("plugin not found in file ") + d.fileOrIdentifier);
}

void addPluginFormats (juce::AudioPluginFormatManager& manager)
{
    manager.addDefaultFormats();
    manager.addFormat (new ClapPluginFormat());
}
} // namespace lynx
