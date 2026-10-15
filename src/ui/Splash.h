#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Instruments.h"

// The start-up splash screen: the pepper drops in with a bounce on a dark card, "LYNX" and the
// version slide in, a thin accent bar fills while the app initialises, then it fades out. It is shown
// only by the normal UI start (never in the command-line modes) and can be turned off, as can the
// start-up sound, in Options (stored in %APPDATA%\LYNX\ui-options.txt).
namespace lynx
{
struct StartupOptions
{
    bool splash = true; // show the splash screen
    bool sound = true;  // play the start-up sound

    static StartupOptions load(); // from ui-options.txt (keys "splash" and "startupSound")
};

// LYNX's start-up sound (core/StartupSound.h) as a sample the preview player can play.
std::shared_ptr<const SampleData> makeStartupSample();

class SplashScreen final : public juce::Component,
                           private juce::Timer
{
public:
    SplashScreen();
    ~SplashScreen() override;

    static constexpr int width = 560, height = 340;
    static constexpr double minimumDuration = 2.1; // seconds on screen before it may close
    static constexpr double fadeOutTime = 0.25;

    // Shows the card centred on the main display, above other windows (the clock has not started yet).
    void showOnDesktop();
    // t = 0 of the animation (start the start-up sound at the same moment).
    void startClock();
    double getElapsed() const;

    void setStatus (const juce::String& text);
    // Fades out, removes itself from the desktop and calls `whenGone`.
    void finish (std::function<void()> whenGone);

    // --snapshot: draws the frame at `seconds` (no desktop, no timer).
    void setTimeForSnapshot (double seconds);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    juce::Image pepper;
    juce::String status;
    double time = 0.0;
    double clockStartMs = -1.0, fadeStartMs = -1.0;
    std::function<void()> onGone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashScreen)
};
} // namespace lynx
