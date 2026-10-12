#include "engine/PluginHost.h"

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

void PluginEffect::process (float* left, float* right, int n, const EffectSlot&, double) noexcept
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

PluginHost::PluginHost()
{
    formats.addDefaultFormats();

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
    paths.add (settingsFolder().getChildFile ("Plugins"));
    return paths;
}

void PluginHost::save()
{
    if (auto xml = known.createXml())
        xml->writeTo (settingsFolder().getChildFile ("plugins.xml"));
}

int PluginHost::scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress)
{
    const auto before = known.getNumTypes();
    const auto pedal = settingsFolder().getChildFile ("scan-em-andamento.txt");

    for (auto* format : formats.getFormats())
    {
        if (! format->canScanForPlugins())
            continue;

        juce::PluginDirectoryScanner scanner (known, *format, paths, true, pedal, false);
        juce::String name;

        while (true)
        {
            if (progress && ! progress (scanner.getProgress(), scanner.getNextPluginFileThatWillBeScanned()))
                break;

            if (! scanner.scanNextFile (true, name))
                break;
        }
    }

    save();
    return known.getNumTypes() - before;
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
