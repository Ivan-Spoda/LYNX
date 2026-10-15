#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

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
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    static std::int64_t processMemoryBytes(); // working set of this process (0 if unknown)

private:
    double cpu = 0.0, cpuSmoothed = 0.0;
    std::int64_t memory = 0;
    int voices = 0, peakVoices = 0;
};

// The output: an oscilloscope of what is heard and a peak meter (red after clipping, for a while).
class OutputMonitor final : public juce::Component,
                            public ui::HintSource
{
public:
    explicit OutputMonitor (AudioEngine& e) : engine (e) {}
    void update();
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    AudioEngine& engine;
    std::array<float, 1024> samples {};
    float peak = 0.0f, held = 0.0f;
    int holdFrames = 0, clipFrames = 0;
};
} // namespace lynx::toolbar
