#include "ui/Workspace.h"

#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
using workspace::Panel;

namespace
{
constexpr int edge = 4; // frame border that resizes the panel

juce::Rectangle<int> toJuce (const workspace::Rect& r)
{
    return { r.x, r.y, r.w, r.h };
}

workspace::Rect toRect (juce::Rectangle<int> r)
{
    return { r.getX(), r.getY(), r.getWidth(), r.getHeight() };
}

juce::File stateFile()
{
    return PluginHost::settingsFolder().getChildFile ("workspace.txt");
}
} // namespace

// --- a panel: title strip + content ------------------------------------------------------------

class PanelFrame final : public juce::Component,
                         public ui::HintSource
{
public:
    PanelFrame (Workspace& w, Panel p) : owner (w), panel (p)
    {
        for (auto* b : { &detachButton, &maximiseButton, &closeButton })
        {
            b->setFlat (true);
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        detachButton.onClick = [this] { owner.setDetached (panel, ! owner.isDetached (panel)); };
        maximiseButton.onClick = [this] { owner.toggleMaximised (panel); };
        closeButton.onClick = [this] { owner.hide (panel); };
        closeButton.setTooltip (tr ("Close the panel (its F-key shows it again)"));
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        refreshButtons();
    }

    ~PanelFrame() override
    {
        if (content != nullptr)
            content->removeMouseListener (this);
    }

    void setContent (juce::Component& c, const juce::String& t, Icon i)
    {
        content = &c;
        title = t;
        icon = i;
        addAndMakeVisible (c);
        c.addMouseListener (this, true); // a click anywhere in the panel activates it
        resized();
    }

    void setTitle (const juce::String& t)
    {
        if (title != t)
        {
            title = t;
            repaint (0, 0, getWidth(), Workspace::titleHeight);
        }
    }

    void setTitleTooltip (const juce::String& t) { titleTooltip = t; }

    void setFocused (bool f)
    {
        if (focused != f)
        {
            focused = f;
            repaint();
        }
    }

    void refreshButtons()
    {
        const auto detached = owner.isDetached (panel);
        detachButton.setIcon (detached ? Icon::dock : Icon::detach);
        detachButton.setTooltip (detached ? tr ("Dock the panel back into the main window") : tr ("Detach the panel into its own window"));
        maximiseButton.setVisible (! detached);
        maximiseButton.setIcon (owner.isMaximised (panel) ? Icon::restore : Icon::maximise);
        maximiseButton.setTooltip (owner.isMaximised (panel) ? tr ("Restore the panel size (double-click the title)")
                                                              : tr ("Maximize the panel (double-click the title)"));
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto detached = owner.isDetached (panel);
        g.fillAll (colour::bg2);

        auto strip = getLocalBounds().removeFromTop (Workspace::titleHeight);
        g.setColour (focused ? colour::bg4 : colour::bg3);
        g.fillRect (strip);
        g.setColour (colour::border);
        g.fillRect (strip.removeFromBottom (1));

        if (focused) // the focused panel gets the accent along its title
        {
            g.setColour (colour::accent);
            g.fillRect (0, 0, getWidth(), 2);
        }

        auto text = juce::Rectangle<int> (8, 0, detachButton.getX() - 12, Workspace::titleHeight - 1);
        icons::draw (g, icon, text.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                     focused ? colour::textPrimary : colour::textSecondary);
        text.removeFromLeft (8);
        g.setColour (focused ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::body, true));
        g.drawFittedText (title, text, juce::Justification::centredLeft, 1, 0.9f);

        if (! detached)
        {
            g.setColour (focused ? colour::borderStrong.brighter (0.15f) : colour::borderStrong);
            g.drawRect (getLocalBounds(), 1);
        }
    }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (Workspace::titleHeight).reduced (4, 3);
        closeButton.setBounds (strip.removeFromRight (strip.getHeight()));
        strip.removeFromRight (2);

        if (maximiseButton.isVisible())
        {
            maximiseButton.setBounds (strip.removeFromRight (strip.getHeight()));
            strip.removeFromRight (2);
        }

        detachButton.setBounds (strip.removeFromRight (strip.getHeight()));

        if (content != nullptr)
        {
            const auto inset = owner.isDetached (panel) || owner.isMaximised (panel) ? 0 : edge;
            content->setBounds (getLocalBounds().withTrimmedTop (Workspace::titleHeight).reduced (inset, 0).withTrimmedBottom (inset));
        }
    }

    // --- mouse: activation, title drag, edge resize -----------------------------------------------

    int edgesAt (juce::Point<int> p) const
    {
        if (owner.isDetached (panel) || owner.isMaximised (panel))
            return 0;

        int edges = 0;
        if (p.x < edge)
            edges |= workspace::left;
        if (p.x >= getWidth() - edge)
            edges |= workspace::right;
        if (p.y >= getHeight() - edge)
            edges |= workspace::bottom;
        if (p.y < 3)
            edges |= workspace::top;
        return edges;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
            return;

        const auto edges = edgesAt (e.getPosition());
        using C = juce::MouseCursor;
        const auto horizontal = (edges & (workspace::left | workspace::right)) != 0;
        const auto vertical = (edges & (workspace::top | workspace::bottom)) != 0;

        if (horizontal && vertical)
            setMouseCursor ((edges & workspace::left) != 0 ? ((edges & workspace::top) ? C::TopLeftCornerResizeCursor : C::BottomLeftCornerResizeCursor)
                                                           : ((edges & workspace::top) ? C::TopRightCornerResizeCursor : C::BottomRightCornerResizeCursor));
        else if (horizontal)
            setMouseCursor (C::LeftRightResizeCursor);
        else if (vertical)
            setMouseCursor (C::UpDownResizeCursor);
        else
            setMouseCursor (C::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.activate (panel);

        if (e.eventComponent != this)
            return;

        if (e.mods.isPopupMenu() && e.y < Workspace::titleHeight)
        {
            if (owner.onTitleRightClick)
                owner.onTitleRightClick (panel, e);
            return;
        }

        dragEdges = edgesAt (e.getPosition());
        dragging = dragEdges != 0 || (e.y < Workspace::titleHeight && ! owner.isDetached (panel) && ! owner.isMaximised (panel));
        startBounds = getBounds();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this || ! dragging || getParentComponent() == nullptr)
            return;

        const auto delta = e.getEventRelativeTo (getParentComponent()).getOffsetFromDragStart();
        auto r = startBounds;

        if (dragEdges == 0)
            r = r.translated (delta.x, delta.y);
        else
        {
            if (dragEdges & workspace::left)
                r.setLeft (juce::jmin (r.getRight() - workspace::minWidth, r.getX() + delta.x));
            if (dragEdges & workspace::right)
                r.setRight (juce::jmax (r.getX() + workspace::minWidth, r.getRight() + delta.x));
            if (dragEdges & workspace::top)
                r.setTop (juce::jmin (r.getBottom() - workspace::minHeight, r.getY() + delta.y));
            if (dragEdges & workspace::bottom)
                r.setBottom (juce::jmax (r.getY() + workspace::minHeight, r.getBottom() + delta.y));
        }

        // keep the title strip reachable
        const auto parent = getParentComponent()->getLocalBounds();
        r.setY (juce::jlimit (0, juce::jmax (0, parent.getHeight() - Workspace::titleHeight), r.getY()));

        if (! e.mods.isAltDown()) // Alt: no snapping
        {
            std::vector<workspace::Rect> others;
            for (const auto& o : owner.otherPanelBounds (panel))
                others.push_back (toRect (o));
            r = toJuce (workspace::snap (toRect (r), others, parent.getWidth(), parent.getHeight(), 8, dragEdges));
        }

        setBounds (r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && dragging)
            owner.frameMoved (panel, getBounds());

        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && e.y < Workspace::titleHeight && ! owner.isDetached (panel))
            owner.toggleMaximised (panel);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.eventComponent == this && e.y < Workspace::titleHeight && owner.onTitleWheel)
            owner.onTitleWheel (panel, e, wheel);
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;

        if (edgesAt (p) != 0)
            h.text = tr ("Drag the edge to resize the panel");
        else if (p.y < Workspace::titleHeight)
            h.text = title + ": " + (titleTooltip.isNotEmpty() ? titleTooltip + "   " : juce::String())
                     + (owner.isDetached (panel) ? tr ("Docks back with the button on the right")
                                                 : tr ("Drag to move (Alt: no snapping), double-click to maximize"));
        return h;
    }

    juce::String getTitle() const { return title; }
    juce::String getTitleTooltip() const { return titleTooltip; }

private:
    Workspace& owner;
    Panel panel;
    juce::Component* content = nullptr;
    juce::String title, titleTooltip;
    Icon icon = Icon::none;
    bool focused = false, dragging = false;
    int dragEdges = 0;
    juce::Rectangle<int> startBounds;
    IconButton detachButton { "detach", Icon::detach }, maximiseButton { "maximise", Icon::maximise }, closeButton { "close", Icon::close };
};

// --- a detached panel's native window -----------------------------------------------------------

class DetachedPanelWindow final : public juce::DocumentWindow
{
public:
    DetachedPanelWindow (Workspace& w, Panel p, PanelFrame& frame, juce::Rectangle<int> screenBounds)
        : juce::DocumentWindow ("LYNX - " + frame.getTitle(), theme::colour::bg2, DocumentWindow::closeButton | DocumentWindow::maximiseButton),
          owner (w),
          panel (p)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (workspace::minWidth, workspace::minHeight + Workspace::titleHeight, 10000, 10000);
        setContentNonOwned (&frame, false);

        if (screenBounds.isEmpty() || juce::Desktop::getInstance().getDisplays().getDisplayForRect (screenBounds) == nullptr)
            centreWithSize (juce::jmax (500, frame.getWidth()), juce::jmax (300, frame.getHeight()));
        else
            setBounds (screenBounds);

        setVisible (true);
        keepAboveMainWindow (*this);
    }

    void closeButtonPressed() override { owner.hide (panel); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        return owner.onKeyInDetachedWindow != nullptr && owner.onKeyInDetachedWindow (key);
    }

    void moved() override
    {
        juce::DocumentWindow::moved();
        owner.detachedWindowMoved (panel, getBounds());
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        owner.detachedWindowMoved (panel, getBounds());
    }

    void activeWindowStatusChanged() override
    {
        if (isActiveWindow())
            owner.activate (panel);
    }

private:
    Workspace& owner;
    Panel panel;
};

// --- the desktop -------------------------------------------------------------------------------

Workspace::Workspace()
{
    for (int i = 0; i < workspace::panelCount; ++i)
    {
        frames[(std::size_t) i] = std::make_unique<PanelFrame> (*this, (Panel) i);
        addChildComponent (*frames[(std::size_t) i]);
    }

    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

Workspace::~Workspace()
{
    saveState();

    for (auto& w : windows)
        if (w != nullptr)
            w->clearContentComponent();

    for (auto& w : windows)
        w.reset();
}

void Workspace::setContent (Panel panel, juce::Component& content, const juce::String& title, Icon icon)
{
    frames[(std::size_t) panel]->setContent (content, title, icon);
}

void Workspace::setTitle (Panel panel, const juce::String& title)
{
    frames[(std::size_t) panel]->setTitle (title);

    if (auto& w = windows[(std::size_t) panel])
        w->setName ("LYNX - " + title);
}

void Workspace::setTitleTooltip (Panel panel, const juce::String& tooltip)
{
    frames[(std::size_t) panel]->setTitleTooltip (tooltip);
}

bool Workspace::isPanelVisible (Panel panel) const { return layout[panel].visible; }
bool Workspace::isMaximised (Panel panel) const { return layout[panel].maximised; }
bool Workspace::isDetached (Panel panel) const { return layout[panel].detached; }

bool Workspace::toggle (Panel panel)
{
    const auto visible = layout.toggle (panel);
    applyLayout();
    return visible;
}

void Workspace::show (Panel panel)
{
    layout.show (panel);
    applyLayout();
}

void Workspace::hide (Panel panel)
{
    layout.hide (panel);
    applyLayout();
}

void Workspace::toggleMaximised (Panel panel)
{
    layout.toggleMaximised (panel);
    applyLayout();
}

void Workspace::setDetached (Panel panel, bool detached)
{
    layout[panel].detached = detached;

    if (detached)
    {
        layout[panel].maximised = false;

        if (layout[panel].windowBounds.w <= 0 && isShowing()) // first time: where the panel is, on screen
        {
            const auto screen = localAreaToGlobal (frames[(std::size_t) panel]->getBounds());
            layout[panel].windowBounds = toRect (screen);
        }
    }

    layout.show (panel);
    applyLayout();
}

void Workspace::activate (Panel panel)
{
    if (layout.focused == panel && (layout[panel].detached || layout.isFrontmost (panel)))
        return;

    layout.bringToFront (panel);

    if (! layout[panel].detached && ! isBackgroundPanel (panel))
        frames[(std::size_t) panel]->toFront (false);

    setFocusedPanel (panel);
}

void Workspace::fitPanelContent (Panel panel, int contentWidth, int contentHeight)
{
    auto& state = layout[panel];

    if (! layoutReady || state.maximised || state.detached)
        return;

    auto r = state.bounds;
    if (contentWidth > 0)
        r.w = contentWidth + 2 * edge;
    if (contentHeight > 0)
        r.h = contentHeight + titleHeight + edge;

    r.w = juce::jlimit (workspace::minWidth, juce::jmax (workspace::minWidth, getWidth() - r.x), r.w);
    r.h = juce::jlimit (workspace::minHeight, juce::jmax (workspace::minHeight, getHeight() - r.y), r.h);
    state.bounds = r;
    applyLayout();
}

void Workspace::setFocusedPanel (Panel panel)
{
    const auto changed = layout.focused != panel;
    layout.focused = panel;

    for (int i = 0; i < workspace::panelCount; ++i)
        frames[(std::size_t) i]->setFocused ((Panel) i == panel);

    if (changed && onFocusChanged)
        onFocusChanged (panel);
}

void Workspace::frameMoved (Panel panel, juce::Rectangle<int> bounds)
{
    layout[panel].bounds = toRect (bounds);
}

void Workspace::detachedWindowMoved (Panel panel, juce::Rectangle<int> screenBounds)
{
    if (layout[panel].detached)
        layout[panel].windowBounds = toRect (screenBounds);
}

std::vector<juce::Rectangle<int>> Workspace::otherPanelBounds (Panel panel) const
{
    std::vector<juce::Rectangle<int>> out;

    for (int i = 0; i < workspace::panelCount; ++i)
        if ((Panel) i != panel && layout[(Panel) i].visible && ! layout[(Panel) i].detached && ! layout[(Panel) i].maximised)
            out.push_back (frames[(std::size_t) i]->getBounds());

    return out;
}

bool Workspace::isDetachedWindowActive() const
{
    for (const auto& w : windows)
        if (w != nullptr && w->isVisible() && w->isActiveWindow())
            return true;

    return false;
}

void Workspace::applyLayout()
{
    if (! layoutReady)
        return;

    for (int i = 0; i < workspace::panelCount; ++i)
    {
        const auto panel = (Panel) i;
        const auto& state = layout[panel];
        auto& frame = *frames[(std::size_t) i];
        auto& window = windows[(std::size_t) i];

        if (state.detached)
        {
            if (window == nullptr)
            {
                removeChildComponent (&frame);
                frame.setVisible (true);
                frame.setSize (state.bounds.w, state.bounds.h);
                window = std::make_unique<DetachedPanelWindow> (*this, panel, frame, toJuce (state.windowBounds));
            }

            if (window->isVisible() != state.visible)
                window->setVisible (state.visible);

            if (state.visible && layout.focused == panel)
                window->toFront (false);
        }
        else
        {
            if (window != nullptr)
            {
                window->clearContentComponent();
                window.reset();
                addChildComponent (frame);
            }

            frame.setBounds (state.maximised ? getLocalBounds() : toJuce (state.bounds));
            frame.setVisible (state.visible);
        }

        frame.refreshButtons();
    }

    // stacking order (back to front): the background panels first, then the floating ones
    for (auto background : { true, false })
        for (auto panel : layout.order)
            if (! layout[panel].detached && isBackgroundPanel (panel) == background)
                frames[(std::size_t) panel]->toFront (false);

    setFocusedPanel (layout.focused);
    repaint();
}

void Workspace::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    if (! layoutReady)
    {
        layout = workspace::Layout::makeDefault (getWidth(), getHeight());

        bool restored = false;

        if (pendingState.isNotEmpty())
            if (auto saved = workspace::Layout::parse (pendingState.toStdString(), getWidth(), getHeight()))
            {
                layout = *saved;
                restored = true;
            }

        layoutReady = true;
        applyLayout();

        if (! restored && onDefaultLayout)
            onDefaultLayout();
        return;
    }

    layout.resize (getWidth(), getHeight());
    applyLayout();
}

void Workspace::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg0);

    // an empty desktop says how to bring the panels back
    bool any = false;
    for (int i = 0; i < workspace::panelCount; ++i)
        any = any || (layout[(Panel) i].visible && ! layout[(Panel) i].detached);

    if (! any)
    {
        g.setColour (colour::textDisabled);
        g.setFont (font (type::body));
        g.drawFittedText (tr ("F5 Playlist   F6 Channel Rack   F7 Piano Roll   F8 Browser   F9 Mixer"), getLocalBounds().reduced (20),
                          juce::Justification::centred, 2);
    }
}

void Workspace::resetLayout()
{
    layout = workspace::Layout::makeDefault (juce::jmax (1, getWidth()), juce::jmax (1, getHeight()));
    layoutReady = true;
    applyLayout();

    if (onDefaultLayout)
        onDefaultLayout();
}

int Workspace::bringBackOffscreen()
{
    const auto moved = layout.bringBackOffscreen();
    applyLayout();
    return moved;
}

juce::String Workspace::getLayoutText() const
{
    return juce::String (layout.serialise());
}

bool Workspace::applyLayoutText (const juce::String& text)
{
    if (! layoutReady)
    {
        if (! workspace::Layout::parse (text.toStdString(), 1000, 1000))
            return false;

        pendingState = text;
        return true;
    }

    const auto parsed = workspace::Layout::parse (text.toStdString(), getWidth(), getHeight());

    if (! parsed)
        return false;

    layout = *parsed;
    applyLayout();
    return true;
}

juce::File Workspace::layoutsFolder()
{
    return PluginHost::settingsFolder().getChildFile ("layouts");
}

bool Workspace::saveLayoutAs (const juce::String& name)
{
    const auto folder = layoutsFolder();
    folder.createDirectory();
    return folder.getChildFile (juce::File::createLegalFileName (name) + ".txt").replaceWithText (getLayoutText());
}

bool Workspace::loadLayout (const juce::File& file)
{
    return applyLayoutText (file.loadFileAsString());
}

void Workspace::setPersistent (bool shouldPersist)
{
    persistent = shouldPersist;

    if (persistent && ! layoutReady)
        pendingState = stateFile().loadFileAsString();
}

void Workspace::saveState()
{
    if (persistent && layoutReady)
        stateFile().replaceWithText (getLayoutText());
}
} // namespace lynx
