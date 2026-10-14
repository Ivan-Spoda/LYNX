#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// LYNX's own vector icons, drawn with juce::Path on a 24x24 grid (line icons with rounded
// joins; transport symbols are solid). Original designs.
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
    detach, dock, restore, keyboard, automate, rightClick, tap
};

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
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon icon, const juce::String& text = {});

    void setIcon (Icon newIcon);
    void setText (const juce::String& newText);
    void setPrimary (bool shouldBePrimary);              // filled with the accent even when off
    void setOnColour (juce::Colour colour);              // background when toggled on (default: accent)
    void setIconColourWhenOn (juce::Colour colour);      // icon/text colour when on (default: white)
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

private:
    Icon icon;
    juce::String text;
    bool primary = false, flat = false, roundLeft = true, roundRight = true;
    juce::Colour onColour, iconOnColour;
};
} // namespace lynx
