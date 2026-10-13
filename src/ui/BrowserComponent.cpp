#include "ui/BrowserComponent.h"

#include "core/FlpImport.h"
#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
bool isAudio (const juce::File& f)
{
    return f.hasFileExtension ("wav;flac;ogg;mp3;aif;aiff;wv");
}
} // namespace

BrowserComponent::BrowserComponent (AudioEngine& e)
    : engine (e)
{
    // pastas do FL (se instalado) e do usuário
    const auto programs = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    juce::File fl;

    for (const auto& dir : programs.getChildFile ("Image-Line").findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
        if (dir.getChildFile ("Data").isDirectory() && dir.getFileName() > fl.getFileName()) // "FL Studio ASIO" é o driver
            fl = dir;

    auto add = [this] (const juce::String& name, const juce::File& folder) {
        if (folder.isDirectory())
        {
            roots.add (folder);
            rootBox.addItem (name, roots.size());
        }
    };

    const auto data = fl.getChildFile ("Data");
    add ("Samples do FL (Packs)", data.getChildFile ("Patches").getChildFile ("Packs"));
    add ("Projetos de exemplo do FL", data.getChildFile ("Demo projects"));
    add ("MIDI do FL", data.getChildFile ("Patches").getChildFile ("Scores"));
    add ("Presets do 3x Osc (FL)", data.getChildFile ("Patches").getChildFile ("Channel presets").getChildFile ("3x Osc"));
    add ("Presets de sintetizadores (FL)", data.getChildFile ("Patches").getChildFile ("Plugin presets").getChildFile ("Generators"));
    add ("Documentos", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    add (utf8 ("Música"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    add (utf8 ("Área de trabalho"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    add ("Downloads", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));

    rootBox.onChange = [this] { setRoot (rootBox.getSelectedId() - 1); };
    rootBox.setWantsKeyboardFocus (false);
    rootBox.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (rootBox);

    tree.setItemHeight (20);
    tree.setDragAndDropDescription (dragDescription);
    tree.addListener (this);
    tree.setColour (juce::TreeView::backgroundColourId, theme::panel);
    tree.setColour (juce::FileTreeComponent::highlightColourId, theme::accentOrange.withAlpha (0.4f));
    tree.setWantsKeyboardFocus (false);
    addAndMakeVisible (tree);

    thread.startThread (juce::Thread::Priority::low);

    if (! roots.isEmpty())
        rootBox.setSelectedId (1);
}

BrowserComponent::~BrowserComponent()
{
    tree.removeListener (this);
    engine.previewFile ({});
    thread.stopThread (2000);
}

void BrowserComponent::setRoot (int index)
{
    if (juce::isPositiveAndBelow (index, roots.size()))
        list.setDirectory (roots[index], true, true);
}

void BrowserComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    rootBox.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    tree.setBounds (area);
}

void BrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void BrowserComponent::selectionChanged()
{
    const auto file = tree.getSelectedFile();

    if (isAudio (file))
        engine.previewFile (file);
}

void BrowserComponent::fileDoubleClicked (const juce::File& file)
{
    if (isAudio (file))
    {
        const auto path = file.getFullPathName().toStdString();
        const auto name = file.getFileNameWithoutExtension().toStdString();
        engine.editSong ([&] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::sampler;
            c.samplePath = path;
            c.mixerTrack = s.nextFreeInsert();
            s.channels.push_back (c);
        });
        engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        return;
    }

    // preset de um synth do FL que o LYNX converte (3x Osc, Sytrus, GMS...): canal novo com o synth interno
    if (file.hasFileExtension ("fst;gmsynth"))
    {
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data))
            return;

        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());

        if (auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
        {
            const auto name = file.getFileNameWithoutExtension().toStdString();
            engine.editSong ([&] (Song& s) {
                ChannelDef c;
                c.name = name;
                c.instrument = InstrumentKind::synth;
                c.synth = *synth;
                c.mixerTrack = s.nextFreeInsert();
                s.channels.push_back (c);
            });
            engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Preset",
                                                    utf8 ("O LYNX lê presets do 3x Osc, Sytrus, Harmor, GMS, Harmless, MiniSynth, DX10, Fruit Kick, Ogun e Sakura."));
        return;
    }

    if (file.hasFileExtension ("lynx;flp;mid;midi") && onOpenProject)
        onOpenProject (file);
}
} // namespace lynx
