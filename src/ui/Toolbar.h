#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ToolbarLayout.h"
#include "engine/AudioEngine.h"
#include "ui/ValueControl.h"

// Widgets of the main toolbar (docs/WORKFLOW_FL.md section 5.1), each with its own hint: the song position
// slider, the time display, master volume and pitch, the CPU / memory / polyphony meter and the output
// monitor. Laid out by MainComponent::resized().
namespace lynx::toolbar
{
// A thin slider under the time display: where the playhead is in the song (Song mode) or in the
// current pattern (Pattern mode). Click or drag to move the playhead; the wheel moves it by a bar.
class SongPositionBar final : public juce::Component,
                              public ui::HintSource
{
public:
    explicit SongPositionBar (AudioEngine& e);
    void update(); // from the main window's timer
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    double length() const;
    double position() const;
    void seekTo (int x);
    juce::Rectangle<float> track() const;
    AudioEngine& engine;
    double shownFraction = -1.0;
    bool dragging = false;
};

// The time display: bar : beat : step, or minutes : seconds : hundredths (a click switches).
class TimeDisplay final : public juce::Component,
                          public ui::HintSource
{
public:
    explicit TimeDisplay (AudioEngine& e) : engine (e)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void update();
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    bool showsTime() const noexcept { return clockMode; }

private:
    juce::String text() const;
    AudioEngine& engine;
    juce::String shown;
    bool clockMode = false;
};

// Master volume (the Master insert's fader) or master pitch (cents, Song::masterPitch): a small knob with
// its name and value beside it.
class MasterKnob final : public juce::Component,
                         public ui::HintSource
{
public:
    enum class Kind
    {
        volume,
        pitch
    };

    MasterKnob (AudioEngine& e, Kind k);
    void update(); // repaints when the song's value changed
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    ui::ValueSpec spec() const;
    double value() const;
    void setValue (double v);
    bool available() const;
    AudioEngine& engine;
    Kind kind;
    ui::ValueDrag drag;
    double shown = -1.0;
};

// CPU load, memory used by LYNX and the polyphony (notes sounding now), in two rows.
class PerformanceMeter final : public juce::Component,
                               public ui::HintSource
{
public:
    void setValues (double cpu01, std::int64_t memoryBytes, int polyphony);
    // Real-time health (REA round 87): callbacks that missed their deadline (the crackles), the highest load since
    // the last update and the heaviest channels / effects ("name 12%").
    // `deviceUnderruns`: the driver's own count (−1: it does not report one), shown like FL's "N underruns".
    void setRealtime (std::uint64_t overDeadline, std::uint64_t lateCalls, double peakLoad, const juce::StringArray& heaviest,
                      int deviceUnderruns = -1);
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    static std::int64_t processMemoryBytes(); // working set of this process (0 if unknown)

private:
    std::uint64_t overDeadline = 0, lateCalls = 0, underruns = 0;
    int deviceUnderruns = -1;
    double peakLoad = 0.0;
    juce::StringArray heaviest;
    juce::uint32 lastGlitchAt = 0; // ms counter of the last new over-deadline callback (0 = none)
    double cpu = 0.0, cpuSmoothed = 0.0;
    std::int64_t memory = 0;
    int voices = 0, peakVoices = 0;
};

// The output: a display of what is heard and a peak meter (red after clipping, for a while). Like FL's
// output panel, the display has modes (right-click menu, or a click steps through them): the oscilloscope,
// a long scrolling view of the level, and a spectrum (low frequencies on the left).
class OutputMonitor final : public juce::Component,
                            public ui::HintSource
{
public:
    explicit OutputMonitor (AudioEngine& e) : engine (e) {}
    void update();
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    void mouseDown (const juce::MouseEvent& e) override;

    enum Mode
    {
        oscilloscope,
        longView,
        spectrum,
        modeCount_
    };
    static constexpr int modeCount = (int) modeCount_;
    static inline int mode = oscilloscope; // shared by every instance; saved in the UI options
    static juce::String modeName (int m);
    std::function<void()> onModeChanged;   // the main window saves the choice

private:
    void setMode (int newMode);
    AudioEngine& engine;
    std::array<float, 1024> samples {};
    std::array<float, 256> history {};     // long view: one peak per update, oldest first
    std::array<float, 128> bands {};       // spectrum: smoothed level per band, 0..1
    float peak = 0.0f, held = 0.0f;
    int holdFrames = 0, clipFrames = 0;
};

// "Customize toolbar": the groups in toolbar order, each with a check box (shown / hidden), Move up / Move
// down for the selected group (also Alt+Up / Alt+Down), the icon size, the UI scale and Reset to default.
// Every change applies at once through onChange.
class CustomizePanel final : public juce::Component
{
public:
    explicit CustomizePanel (const toolbarlayout::Layout& layout);
    std::function<void (const toolbarlayout::Layout&)> onChange;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void select (toolbarlayout::Group g);

private:
    void rebuild();
    void changed();
    void move (int delta);
    juce::Rectangle<int> listArea() const;
    static constexpr int rowHeight = 30;
    toolbarlayout::Layout layout;
    toolbarlayout::Group selected = toolbarlayout::Group::transport;
    juce::OwnedArray<juce::ToggleButton> checks;
    juce::TextButton upButton, downButton, resetButton;
    juce::ComboBox iconSizeBox, scaleBox;
};
} // namespace lynx::toolbar
