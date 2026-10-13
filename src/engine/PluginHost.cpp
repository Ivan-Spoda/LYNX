#include "engine/PluginHost.h"

#include "engine/ClapFormat.h"

namespace lynx
{
namespace
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
