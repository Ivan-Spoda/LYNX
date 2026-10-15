#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// LYNX's own vector icons, drawn with juce::Path on a 24x24 grid. Original designs, one family:
// - live area 3..21 (a 3-unit margin), coordinates on half units so 24 px and 16 px stay crisp;
// - line icons share one stroke (1.75 units at 24 px, never thinner than 1.2 px), round caps and joins;
// - transport symbols and small accents (a lit step, a note, a dot) are solid fills.
namespace lynx
{
enum class Icon
{
    none,
    play, pause, stop, record, chevronDown, more, plus, check, close, minimise, maximise,
    pattern, song, metronome,
    newFile, open, save, exportAudio, settings, help, undo, redo,
    channelRack, pianoRoll, playlist, mixer, browser,
    folder, file, audioFile, plugin, preset, project,
    draw, erase, select, slice, mute, link, magnet, scissors,
    // workspace and control contract
    detach, dock, restore, keyboard, automate, rightClick, tap,
    brush, knife, zoom, listen, slip, ghost, // editor tools (Piano Roll / Playlist)
    panelMenu, pluginPicker, mic, // window options menu, plugin picker, recording options
    countIn, waitInput, // recording: count-in and wait for input
    mouseLeft, mouseMiddle, mouseWheel, // hint bar gestures (with rightClick)
    tuningFork // master pitch
};

constexpr int iconCount = (int) Icon::tuningFork + 1;

namespace icons
{
struct Shape
{
    juce::Path stroke; // drawn with a line (stroke width scales with the size)
    juce::Path fill;   // solid parts
};

const Shape& get (Icon icon);

// Draws `icon` fitted (centred, square) into `area`.
void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);
} // namespace icons

// A flat button that shows an icon, optionally followed by text. Uses the theme colours:
// neutral at rest, red accent when toggled on (or when `primary`).
//
// Hover and press states fade in and out (about 120 ms) instead of switching at once.
class IconButton : public juce::Button,
                   private juce::Timer
{
public:
    IconButton (const juce::String& name, Icon icon, const juce::String& text = {});

    void setIcon (Icon newIcon);
    void setText (const juce::String& newText);
    void setPrimary (bool shouldBePrimary);              // filled with the accent even when off
    void setOnColour (juce::Colour colour);              // background when toggled on (default: accent)
    void setIconColourWhenOn (juce::Colour colour);      // icon/text colour when on (default: white)
    void setIconColour (juce::Colour colour);            // icon colour at rest (default: the theme's secondary text)
    void setFlat (bool shouldBeFlat);                    // no background at rest (toolbar style)
    void setCorners (bool roundLeft, bool roundRight);   // for segmented groups
    int getIdealWidth (int height) const;

    // Optional extra gestures: when set, a right-click calls onRightClick instead of clicking the button,
    // and a double-click calls onDoubleClick (after the two single clicks).
    std::function<void()> onRightClick, onDoubleClick;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override;

private:
    void timerCallback() override;
    float hover = 0.0f; // 0 = at rest, 1 = under the mouse (animated)
    Icon icon;
    juce::String text;
    bool primary = false, flat = false, roundLeft = true, roundRight = true;
    juce::Colour onColour, iconOnColour, iconColour;
};
} // namespace lynx
