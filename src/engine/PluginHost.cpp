#include "engine/PluginHost.h"

#include "core/FlWrapper.h"

#include "engine/ClapFormat.h"
#include "engine/PluginSandbox.h"

namespace lynx
{
void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize)
{
    // Tenta saída estéreo; plugins que não aceitam ficam com o layout que tiverem.
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

int bufferChannels (const juce::AudioPluginInstance& plugin)
{
    return juce::jmax (2, plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
}
} // namespace

// ---------------------------------------------------------------------------

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize)
    : plugin (std::move (instance)),
      blockSize (maxBlockSize)
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
    midi.ensureSize (8192); // a thread de áudio só adiciona eventos dentro dessa capacidade
}

PluginInstrument::~PluginInstrument()
{
    plugin->releaseResources();
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
    setPluginParameter (*plugin, index, value);
}

void PluginInstrument::render (float* left, float* right, int n, float gl, float gr) noexcept
{
    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        plugin->processBlock (view, midi);
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
      blockSize (maxBlockSize)
{
    buffer.setSize (bufferChannels (*plugin), blockSize);
}

PluginEffect::~PluginEffect()
{
    plugin->releaseResources();
}

void PluginEffect::setParameter (int index, float value) noexcept
{
    setPluginParameter (*plugin, index, value);
}

void PluginEffect::process (float* left, float* right, int n, const EffectParams&, double) noexcept
{
    for (int done = 0; done < n;)
    {
        const auto count = juce::jmin (blockSize, n - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        view.clear();
        view.copyFrom (0, 0, left + done, count);
        view.copyFrom (1, 0, right + done, count);
        plugin->processBlock (view, midi);
        midi.clear();

        std::memcpy (left + done, view.getReadPointer (0), sizeof (float) * (size_t) count);
        std::memcpy (right + done, view.getReadPointer (view.getNumChannels() > 1 ? 1 : 0), sizeof (float) * (size_t) count);
        done += count;
    }
}

// ---------------------------------------------------------------------------

PluginHost::PluginHost (bool keep)
    : persistent (keep)
{
    addPluginFormats (formats);

    if (persistent)
        if (auto xml = juce::XmlDocument::parse (settingsFolder().getChildFile ("plugins.xml")))
            known.recreateFromXml (*xml);
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

    // Mesmo lugar que o JUCE/Steinberg usam por padrão no Windows.
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/CLAP"));
    paths.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs/Common/CLAP"));
    paths.add (settingsFolder().getChildFile ("Plugins"));
    return paths;
}

void PluginHost::save()
{
    if (! persistent)
        return;

    if (auto xml = known.createXml())
        xml->writeTo (settingsFolder().getChildFile ("plugins.xml"));
}

int PluginHost::scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress)
{
    const auto before = known.getNumTypes();
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto result = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-scan.xml");

    for (auto* format : formats.getFormats())
    {
        if (! format->canScanForPlugins())
            continue;

        const auto files = format->searchPathsForPlugins (paths, true, false);

        for (int i = 0; i < files.size(); ++i)
        {
            const auto& file = files[i];

            if (progress && ! progress ((float) i / (float) juce::jmax (1, files.size()), file))
                break;

            if (known.isListingUpToDate (file, *format) || known.getBlacklistedFiles().contains (file))
                continue;

            result.deleteFile();
            juce::ChildProcess child;
            auto ok = child.start (juce::StringArray { exe.getFullPathName(), "--scan-one", format->getName(), file, result.getFullPathName() },
                                   0);

            if (ok && ! child.waitForProcessToFinish (scanTimeoutMs))
            {
                child.kill(); // travou: lista negra
                ok = false;
            }

            ok = ok && child.getExitCode() == 0;

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
        }
    }

    result.deleteFile();
    save();
    return known.getNumTypes() - before;
}

int PluginHost::scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output)
{
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
    return found.size();
}

int PluginHost::addPluginFile (const juce::File& file)
{
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
// O deprecatedUid que o JUCE dá a uma classe VST3: hash 31 sobre os 16 bytes do TUID como `char`
// (com sinal, como o TUID do SDK). O FL guarda o TUID na mesma ordem de memória.
int vst3Hash (const std::array<std::uint8_t, 16>& tuid)
{
    juce::uint32 value = 0;

    for (auto b : tuid)
        value = value * 31 + (juce::uint32) (int) (signed char) b;

    return (int) value;
}

// O uniqueId "normalizado" do JUCE: o mesmo hash sobre os 4 inteiros do FUID (layout COM no Windows).
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

const juce::PluginDescription* PluginHost::findForeign (const ForeignPlugin& plugin, bool instrument) const
{
    const auto& types = known.getTypes();
    const auto fileName = plainName (juce::File::createFileWithoutCheckingPath (
        juce::String::fromUTF8 (plugin.path.c_str()).replaceCharacter ('\\', '/')).getFileNameWithoutExtension());
    const auto name = plainName (juce::String::fromUTF8 (plugin.name.c_str()));

    // 1) o mesmo plugin, pelo identificador do formato
    for (const auto& d : types)
    {
        if (plugin.format == ForeignPlugin::Format::vst3 && plugin.hasClassId && d.pluginFormatName == "VST3"
            && (d.deprecatedUid == vst3Hash (plugin.classId) || d.uniqueId == vst3NormalisedHash (plugin.classId)))
            return &d;

        if (plugin.format == ForeignPlugin::Format::clap && ! plugin.clapId.empty() && d.pluginFormatName == ClapPluginFormat::formatName
            && d.uniqueId == juce::String::fromUTF8 (plugin.clapId.c_str()).hashCode())
            return &d;
    }

    // 2) plano B, como o FL troca VST2 <-> VST3: mesmo nome (do plugin ou do arquivo), mesmo papel
    if (name.isEmpty() && fileName.isEmpty())
        return nullptr;

    for (const auto& d : types)
    {
        if (d.isInstrument != instrument)
            continue;

        const auto candidate = plainName (d.name);
        const auto candidateFile = plainName (juce::File (d.fileOrIdentifier).getFileNameWithoutExtension());

        if ((name.isNotEmpty() && (candidate == name || candidateFile == name))
            || (fileName.isNotEmpty() && (candidate == fileName || candidateFile == fileName)))
            return &d;
    }

    return nullptr;
}

int PluginHost::adoptFlPlugins (Song& song, juce::StringArray* missing) const
{
    int found = 0;

    // devolve o estado no formato do JUCE, ou vazio quando não dá para levar o estado
    auto convertState = [] (const ForeignPlugin& f, const juce::PluginDescription& d) -> std::string {
        // CLAP: u32 0 + os bytes de clap_plugin_state.save (REA_rodada22 §2); o host CLAP do LYNX guarda o mesmo
        if (f.format == ForeignPlugin::Format::clap && d.pluginFormatName == "CLAP")
            return f.state.size() > 4 ? juce::MemoryBlock (f.state.data() + 4, f.state.size() - 4).toBase64Encoding().toStdString() : std::string {};

        if (f.format != ForeignPlugin::Format::vst3 || d.pluginFormatName != "VST3")
            return {}; // VST2 -> VST3: formato do estado diferente; plugin no padrão

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
        const auto* d = findForeign (ref.foreign, instrument);

        if (d == nullptr)
        {
            if (missing != nullptr)
                missing->addIfNotAlreadyThere (juce::String::fromUTF8 (ref.foreign.name.c_str()));
            ref.foreign = {};
            return false;
        }

        ref.id = d->createIdentifierString().toStdString();
        ref.name = d->name.toStdString();
        ref.state = convertState (ref.foreign, *d);
        ref.foreign = {};
        ++found;
        return true;
    };

    for (auto& channel : song.channels)
        if (channel.plugin.foreign.format != ForeignPlugin::Format::none && adopt (channel.plugin, true))
            channel.instrument = InstrumentKind::plugin;

    for (auto& track : song.mixer)
        std::erase_if (track.effects, [&] (EffectSlot& slot) {
            return slot.kind == EffectKind::plugin && slot.plugin.foreign.format != ForeignPlugin::Format::none
                   && ! adopt (slot.plugin, false);
        });

    return found;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::instantiate (const PluginRef& ref, double sampleRate, juce::String* error)
{
    const auto description = find (juce::String::fromUTF8 (ref.id.c_str()));

    if (description == nullptr)
    {
        if (error != nullptr)
            *error = juce::String::fromUTF8 (ref.name.c_str()) + juce::String::fromUTF8 (": plugin não instalado ou não escaneado");
        return nullptr;
    }

    juce::String message;
    if (pluginSandboxEnabled())
    {
        // processo separado: o filho prepara o plugin e aplica o estado
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

std::shared_ptr<Instrument> PluginHost::makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error)
{
    if (auto instance = instantiate (ref, sampleRate, error))
        return std::make_shared<PluginInstrument> (std::move (instance), maxBlockSize);

    return nullptr;
}

std::shared_ptr<Effect> PluginHost::makeEffect (const PluginRef& ref, double sampleRate, juce::String* error)
{
    if (auto instance = instantiate (ref, sampleRate, error))
        return std::make_shared<PluginEffect> (std::move (instance), maxBlockSize);

    return nullptr;
}

std::string capturePluginState (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    return state.toBase64Encoding().toStdString();
}
} // namespace lynx
