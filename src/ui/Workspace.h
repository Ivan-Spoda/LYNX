#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Workspace.h"
#include "ui/Icons.h"

namespace lynx
{
class PanelFrame;
class DetachedPanelWindow;

// The workspace desktop (docs/WORKFLOW_FL.md, P1 item 1): the Playlist, Channel Rack, Piano Roll,
// Mixer and Browser are floating panels inside the main window. Each has a title strip with detach,
// maximize and close; it is dragged by the strip (snapping to the other panels and the edges; Alt
// turns snapping off), resized by its edges, and double-clicking the strip maximizes it. A detached
// panel lives in its own native window (for a second monitor) until it is docked again.
//
// The layout model is core/Workspace.h; it is saved in %APPDATA%\LYNX\workspace.txt, and named
// layouts in %APPDATA%\LYNX\layouts\*.txt.
class Workspace final : public juce::Component
{
public:
    using Panel = workspace::Panel;

    Workspace();
    ~Workspace() override;

    // `content` is not owned (it must outlive the workspace).
    void setContent (Panel panel, juce::Component& content, const juce::String& title, Icon icon);
    void setTitle (Panel panel, const juce::String& title);
    void setTitleTooltip (Panel panel, const juce::String& tooltip);

    // F-key behaviour (see Layout::toggle). Returns true if the panel is visible afterwards.
    bool toggle (Panel panel);
    void show (Panel panel); // shows, brings to front and focuses
    void hide (Panel panel);
    void toggleMaximised (Panel panel);
    void setDetached (Panel panel, bool detached);

    bool isPanelVisible (Panel panel) const;
    bool isMaximised (Panel panel) const;
    bool isDetached (Panel panel) const;
    Panel getFocused() const noexcept { return layout.focused; }

    // Layouts
    void resetLayout();
    int bringBackOffscreen();
    juce::String getLayoutText() const;
    bool applyLayoutText (const juce::String& text); // false if unreadable
    static juce::File layoutsFolder();
    bool saveLayoutAs (const juce::String& name);
    bool loadLayout (const juce::File& file);

    // Persistence of the current layout in the user settings (off for --snapshot).
    void setPersistent (bool shouldPersist);
    void saveState();

    // True while one of the detached windows is the active window.
    bool isDetachedWindowActive() const;

    std::function<void (Panel)> onFocusChanged;
    std::function<bool (const juce::KeyPress&)> onKeyInDetachedWindow;
    std::function<void (Panel, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onTitleWheel;
    std::function<void (Panel, const juce::MouseEvent&)> onTitleRightClick;
    std::function<void()> onDefaultLayout; // after the default layout was (re)built: fit panels to their content

    // Resizes a floating panel's content area to `contentWidth` x `contentHeight` (0 = keep that side),
    // keeping its top-left corner and staying inside the desktop. Ignored while maximized or detached.
    void fitPanelContent (Panel panel, int contentWidth, int contentHeight);

    // The Playlist and the Browser are the background of the desktop: the floating panels (Channel Rack,
    // Piano Roll, Mixer) always stay above them, as pop-up windows.
    static bool isBackgroundPanel (Panel panel) noexcept { return panel == Panel::playlist || panel == Panel::browser; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    // used by the frames
    void activate (Panel panel);
    void frameMoved (Panel panel, juce::Rectangle<int> bounds);
    std::vector<juce::Rectangle<int>> otherPanelBounds (Panel panel) const;
    void detachedWindowMoved (Panel panel, juce::Rectangle<int> screenBounds);

    static constexpr int titleHeight = 28;

private:
    void applyLayout();
    void setFocusedPanel (Panel panel);

    workspace::Layout layout;
    bool layoutReady = false, persistent = false;
    juce::String pendingState;
    std::array<std::unique_ptr<PanelFrame>, workspace::panelCount> frames;
    std::array<std::unique_ptr<DetachedPanelWindow>, workspace::panelCount> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Workspace)
};
} // namespace lynx
