#pragma once

#include <functional>
#include <map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/ValueControl.h"

namespace lynx
{
// The built-in effects offered in the slot menus and the quick-add palette, in menu order.
const std::vector<EffectKind>& addableEffects();

// FL-style mixer: one vertical strip per track (Master first).
//  - fader and pan: the shared control contract (ui/ValueControl.h): drag (Ctrl: fine), wheel,
//    Alt/middle-click or double-click resets, right-click: type value / reset / automation
//  - green LED = mute (click), "S" = solo
//  - clicking a strip selects it; right-click opens the menu (rename, route the selected
//    channel to this track); "+" at the end adds an insert
//  - below each strip are the channels routed into it
//  - send button (arrow) at the bottom: toggles the send from the selected track to this one;
//    dragging vertically changes the send level
class MixerStrips final : public juce::Component,
                          public ui::HintSource,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit MixerStrips (AudioEngine& engine);
    ~MixerStrips() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    void updateSize();
    void setVisibleHeight (int height);

    int getSelectedTrack() const noexcept { return selected; }
    void selectTrack (int track);
    std::function<void (int)> onSelectionChanged;

    // Multi-selection (docs/WORKFLOW_FL.md P2, M1): Ctrl+click on a strip's header toggles it, Shift+click
    // selects a range; selected faders and pans move together. Always contains the selected track.
    const std::vector<int>& getSelection() const noexcept { return selection; }
    void setSelection (std::vector<int> tracks, int primary);
    bool moveSelected (int delta);           // Alt+Left/Right
    void linkSelectedChannels (bool startingHere); // Ctrl+L / Ctrl+Shift+L
    void selectLinkedChannels();             // Alt+L

    static constexpr int stripWidth = 70;
    static constexpr int masterWidth = 86;

private:
    enum class Part { none, pan, fader, mute, solo, route, body, add, arm };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int stripX (int track) const noexcept;
    int stripWidthOf (int track) const noexcept { return track == 0 ? masterWidth : stripWidth; }
    int trackAt (int x) const noexcept;
    Part partAt (int track, juce::Point<int> p) const;
    juce::Rectangle<int> faderArea (int track) const;
    juce::Rectangle<int> panArea (int track) const;
    juce::Rectangle<int> muteArea (int track) const;
    juce::Rectangle<int> soloArea (int track) const;
    juce::Rectangle<int> routeArea (int track) const;
    juce::Rectangle<int> armArea (int track) const;
    void showSendMenu (int track);
    void soloExclusive (int track);
    std::vector<int> selection { 0 };
    std::vector<std::pair<int, float>> groupStart; // (track, value) of the selection when a group drag starts
    float groupGrab = 0.0f;
    void showMenu (int track);
    void showTrackEq (int track);
    ui::ValueSpec specFor (Part part, int track) const;
    void setValue (Part part, int track, double value);
    double valueOf (Part part, int track) const;

    AudioEngine& engine;
    int selected = 0;
    int visibleHeight = 400;

    Part dragPart = Part::none;
    int dragTrack = -1;
    float dragStartValue = 0.0f;
    ui::ValueDrag drag;

    std::vector<float> levels; // displayed level per track*2, with smooth falloff

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrips)
};

// Effects panel for the selected track: 10 slots and the knobs of the chosen slot.
//  - click an empty slot: choose an effect; click an occupied one: edit its knobs
//  - slot LED: on/off (bypass); right-click: replace, move, delete
//  - knob to the right of the slot: dry/wet (drag; double-click resets to 100%)
class EffectsPanel final : public juce::Component,
                           public ui::HintSource,
                           public juce::DragAndDropTarget,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener
{
public:
    // Effect presets (.fst) dragged from the Browser or Explorer: dropped on a slot they replace it,
    // anywhere else they go into the first free slot.
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool dropPreset (const juce::File& file, juce::Point<int> position);

    explicit EffectsPanel (AudioEngine& engine);
    ~EffectsPanel() override;

    void setTrack (int track);
    int getSelectedSlot() const noexcept { return selectedSlot; }
    void selectSlot (int slot)
    {
        selectedSlot = slot;
        repaint();
    }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    ui::ValueSpec specFor (int param, int slot) const; // param = mixParam: the slot's dry/wet
    double valueOf (int param, int slot) const;
    void setValue (int param, int slot, double value);
    int mixKnobAt (juce::Point<int> p) const; // slot whose dry/wet knob is at p, or -1
    juce::Rectangle<int> slotBounds (int slot) const;
    juce::Rectangle<int> mixBounds (int slot) const; // dry/wet knob on the slot's row
    static constexpr int mixParam = -2;
    static constexpr int gateStepParam = -3; // drawing the volume gate steps
    juce::Rectangle<int> knobBounds (int param) const;
    bool gateSelected() const;
    juce::Rectangle<int> gateStepsBounds() const;
    void setGateStep (juce::Point<int> p);
    void showGatePatterns();
    int knobAt (juce::Point<int> p) const;
    const std::vector<EffectSlot>* slots() const;
    void showAddMenu (int slot);
    void showSlotMenu (int slot);
    juce::Rectangle<int> pluginButtonBounds() const;
    void openPluginWindow (int slot);
    juce::Rectangle<int> fxSwitchBounds() const; // all effects on/off; right-click opens/closes their windows
    void toggleAllWindows();
    int slotAt (juce::Point<int> p) const;
    int dragSlot = -1, dropSlot = -1; // dragging a slot to reorder it
    // A/B compare: a spare state per (insert, slot), swapped with the live one on request (not saved)
    std::map<std::pair<int, int>, EffectSlot> spare;

    AudioEngine& engine;
    int track = 0;
    int selectedSlot = -1;
    int dragParam = -1;
    float dragStart = 0.0f;
    ui::ValueDrag drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

class MixerComponent final : public juce::Component
{
public:
    explicit MixerComponent (AudioEngine& engine);
    void resized() override;

    // Selects the first track that has effects (used by --snapshot mode).
    void selectFirstTrackWithEffects();
    void selectTrack (int track) { strips.selectTrack (track); }
    void selectSlot (int slot) { effects.selectSlot (slot); }

    // The key map's mixer.* actions.
    bool runAction (const std::string& id);

    // Adds an effect to the first free slot of the selected insert (quick-add palette). False when full.
    bool addEffectToSelected (EffectSlot slot);
    MixerStrips& getStripsForSnapshot() { return strips; }

    static constexpr int effectsWidth = 300;

private:
    AudioEngine& engine;
    MixerStrips strips;
    juce::Viewport viewport;
    EffectsPanel effects;
};
} // namespace lynx
