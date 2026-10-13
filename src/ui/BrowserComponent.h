#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// File browser (like FL's Browser): FL and user sample folders.
//  - click an audio file: preview; double-click: new sampler channel with it
//  - drag onto the Channel Rack or the Playlist (like dragging from Explorer)
//  - double-click a .lynx/.flp/.mid: opens it (onOpenProject)
class BrowserComponent final : public juce::Component,
                               private juce::FileBrowserListener
{
public:
    explicit BrowserComponent (AudioEngine& engine);
    ~BrowserComponent() override;

    std::function<void (const juce::File&)> onOpenProject;

    void resized() override;
    void paint (juce::Graphics& g) override;

    static constexpr const char* dragDescription = "lynx-arquivo";

private:
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}
    void setRoot (int index);

    AudioEngine& engine;
    juce::ComboBox rootBox;
    juce::Array<juce::File> roots;
    juce::TimeSliceThread thread { "LYNX browser" };
    juce::WildcardFileFilter filter { "*.wav;*.flac;*.ogg;*.mp3;*.aif;*.aiff;*.mid;*.midi;*.lynx;*.flp;*.fst", "*", "files" };
    juce::DirectoryContentsList list { &filter, thread };
    juce::FileTreeComponent tree { list };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserComponent)
};
} // namespace lynx
