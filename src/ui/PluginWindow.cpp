#include "ui/WindowOwner.h"
#include "ui/PluginWindow.h"

#include <algorithm>
#include <map>

#include "core/FlWrapper.h"
#include "engine/PluginSandbox.h"
#include "engine/Text.h"
#include "platform/CrashGuard.h"
#include "ui/I18n.h"
#include "platform/Paths.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
void toggleWrapperOption (AudioEngine& engine, const std::function<PluginRef* (Song&)>& find, juce::AudioPluginInstance* instance,
                          bool PluginRef::*option, bool recreates);

// FL's plugin wrapper settings page (REA round 146a), in LYNX's style: what the plugin reports, then the wrapper
// options LYNX applies in FL's sections (Processing, Bridging, MIDI). Options that re-create the plugin keep its state
// and close this window (open it again from the channel or the slot).
class WrapperSettingsPage final : public juce::Component, private juce::ChangeListener
{
public:
    WrapperSettingsPage (AudioEngine& e, std::function<PluginRef* (Song&)> f, juce::AudioPluginInstance& p)
        : engine (e), find (std::move (f)), plugin (p)
    {
        const auto& options = wrapperOptions();
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            auto* t = toggles.add (new juce::ToggleButton (tr (options[i].label)));
            t->setTooltip (tr (options[i].hint));
            t->setColour (juce::ToggleButton::textColourId, theme::colour::textPrimary);
            t->setColour (juce::ToggleButton::tickColourId, theme::colour::accent);
            t->onClick = [this, i] {
                auto* en = &engine;
                auto fn = find;
                auto* inst = &plugin;
                const auto& o = wrapperOptions()[i];
                juce::MessageManager::callAsync ([en, fn, inst, member = o.member, re = o.recreates] { toggleWrapperOption (*en, fn, inst, member, re); });
            };
            addAndMakeVisible (t);
        }
        port.addItem (tr ("None"), 1);
        for (int i = 0; i < 32; ++i)
            port.addItem (tr ("Port {0}", { juce::String (i) }), i + 2);
        port.setTooltip (tr ("MIDI input port number"));
        port.onChange = [this] {
            const auto value = port.getSelectedId() - 2;
            engine.editSong ([fn = find, value] (Song& song) {
                if (auto* ref = fn (song))
                    ref->midiInPort = value;
            });
        };
        addAndMakeVisible (port);
        engine.addChangeListener (this);
        refresh();
        setSize (460, preferredHeight());
    }

    ~WrapperSettingsPage() override { engine.removeChangeListener (this); }

    int preferredHeight() const { return top() + (int) wrapperOptions().size() * rowHeight + 4 * headerHeight + rowHeight + 24; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        auto header = [&g] (const juce::String& text, int y) {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption, true));
            g.drawText (caps (text), 16, y, 400, headerHeight - 4, juce::Justification::bottomLeft);
            g.setColour (colour::border);
            g.fillRect (16, y + headerHeight - 2, 428, 1);
        };
        header (tr ("Plugin"), 8);
        g.setFont (font (type::body));
        int y = 8 + headerHeight + 2;
        for (const auto& [label, value] : infoLines())
        {
            g.setColour (colour::textSecondary);
            g.drawText (label, 16, y, 130, infoHeight, juce::Justification::centredLeft);
            g.setColour (colour::textPrimary);
            g.drawFittedText (value, 150, y, getWidth() - 166, infoHeight, juce::Justification::centredLeft, 1);
            y += infoHeight;
        }
        juce::String last;
        for (std::size_t i = 0; i < wrapperOptions().size(); ++i)
            if (const juce::String section = wrapperOptions()[i].section; section != last)
            {
                header (tr (section), toggles[(int) i]->getY() - headerHeight);
                last = section;
            }
        header (tr ("MIDI"), port.getY() - headerHeight);
        g.setColour (colour::textSecondary);
        g.drawText (tr ("Input port"), 16, port.getY(), 130, port.getHeight(), juce::Justification::centredLeft);
    }

    void resized() override
    {
        int y = top();
        juce::String last;
        for (std::size_t i = 0; i < wrapperOptions().size(); ++i)
        {
            if (const juce::String section = wrapperOptions()[i].section; section != last)
            {
                y += headerHeight;
                last = section;
            }
            toggles[(int) i]->setBounds (24, y, getWidth() - 40, rowHeight);
            y += rowHeight;
        }
        y += headerHeight;
        port.setBounds (150, y + 2, 140, rowHeight - 4);
    }

private:
    static constexpr int headerHeight = 24, rowHeight = 24, infoHeight = 18;

    int top() const { return 8 + headerHeight + 2 + (int) infoLines().size() * infoHeight + 6; }

    std::vector<std::pair<juce::String, juce::String>> infoLines() const
    {
        const auto d = plugin.getPluginDescription();
        const auto rate = plugin.getSampleRate() > 0 ? plugin.getSampleRate() : 44100.0;
        const auto latency = plugin.getLatencySamples();
        return {
            { tr ("Name"), d.name + (d.version.isNotEmpty() ? "  " + d.version : juce::String()) },
            { tr ("Vendor"), d.manufacturerName },
            { tr ("Format"), d.pluginFormatName + (d.isInstrument ? "  (" + tr ("instrument") + ")" : juce::String()) },
            { tr ("File"), d.fileOrIdentifier },
            { tr ("Latency"), tr ("{0} samples ({1} ms)", { juce::String (latency), juce::String (latency * 1000.0 / rate, 1) }) },
            { tr ("Inputs / outputs"), juce::String (plugin.getTotalNumInputChannels()) + " / " + juce::String (plugin.getTotalNumOutputChannels()) },
            { tr ("Parameters"), juce::String (plugin.getParameters().size()) },
        };
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        auto* ref = find (const_cast<Song&> (engine.getSong()));
        if (ref == nullptr)
            return;
        const auto& options = wrapperOptions();
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            toggles[(int) i]->setToggleState (ref->*(options[i].member), juce::dontSendNotification);
            toggles[(int) i]->setEnabled (! options[i].needsFixed || ref->fixedBuffers);
        }
        // with Options › "Run plugins in a separate process" every plugin already runs bridged
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].member == &PluginRef::separateProcess && pluginSandboxEnabled())
                toggles[(int) i]->setToggleState (true, juce::dontSendNotification), toggles[(int) i]->setEnabled (false);
        port.setSelectedId (ref->midiInPort + 2, juce::dontSendNotification);
    }

    AudioEngine& engine;
    std::function<PluginRef* (Song&)> find;
    juce::AudioPluginInstance& plugin;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::ComboBox port;
};

// The window's content with FL's wrapper header: the plugin's editor, or the wrapper settings page (gear).
class WrappedEditor final : public juce::Component
{
public:
    WrappedEditor (juce::AudioProcessorEditor* e, AudioEngine& engine, std::function<PluginRef* (Song&)> find, juce::AudioPluginInstance& plugin)
        : editor (e), page (engine, std::move (find), plugin)
    {
        for (auto* b : { &pluginTab, &settingsTab })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonColourId, theme::colour::bg2);
            b->setColour (juce::TextButton::buttonOnColourId, theme::colour::bg4);
            b->setColour (juce::TextButton::textColourOffId, theme::colour::textSecondary);
            b->setColour (juce::TextButton::textColourOnId, theme::colour::textPrimary);
            addAndMakeVisible (b);
        }
        pluginTab.setTooltip (tr ("The plugin's own editor"));
        settingsTab.setTooltip (tr ("Plugin wrapper settings: processing, bridging and MIDI options"));
        pluginTab.onClick = [this] { show (false); };
        settingsTab.onClick = [this] { show (true); };
        addAndMakeVisible (editor.get());
        addChildComponent (page);
        pluginTab.setToggleState (true, juce::dontSendNotification);
        fitEditor();
    }

    ~WrappedEditor() override
    {
        editor = nullptr; // the editor goes first (the plugin may be released right after the window)
    }

    bool isEditorResizable() const { return editor != nullptr && editor->isResizable(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (headerHeight).reduced (4, 3);
        pluginTab.setBounds (header.removeFromLeft (80));
        header.removeFromLeft (4);
        settingsTab.setBounds (header.removeFromLeft (130));
        page.setBounds (area);
        if (editor != nullptr && ! settings)
        {
            const juce::ScopedValueSetter<bool> guard (laying, true);
            if (editor->isResizable())
                editor->setBounds (area);
            else
                editor->setTopLeftPosition (area.getPosition());
        }
    }

    void childBoundsChanged (juce::Component* child) override
    {
        if (child == editor.get() && ! laying && ! settings)
            fitEditor();
    }

private:
    static constexpr int headerHeight = 30;

    void fitEditor()
    {
        if (editor != nullptr)
            setSize (juce::jmax (220, editor->getWidth()), editor->getHeight() + headerHeight);
    }

    void show (bool showSettings)
    {
        settings = showSettings;
        pluginTab.setToggleState (! settings, juce::dontSendNotification);
        settingsTab.setToggleState (settings, juce::dontSendNotification);
        page.setVisible (settings);
        if (editor != nullptr)
            editor->setVisible (! settings);
        if (settings)
            setSize (juce::jmax (getWidth(), page.getWidth()), juce::jmax (getHeight(), page.preferredHeight() + headerHeight));
        else
            fitEditor();
        resized();
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    WrapperSettingsPage page;
    juce::TextButton pluginTab { tr ("Plugin") }, settingsTab { juce::String::fromUTF8 ("\xe2\x9a\x99 ") + tr ("Wrapper settings") };
    bool settings = false, laying = false;
};

class PluginWindow final : public juce::DocumentWindow, private juce::AudioProcessorListener
{
public:
    PluginWindow (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title, PluginWindows::WrapperContext context)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          owner (std::move (keepAlive)),
          plugin (p)
    {
        setUsingNativeTitleBar (true);

        crashguard::note (("opening the editor of " + plugin.getName()).toStdString()); // session breadcrumb (round 111)
        juce::AudioProcessorEditor* editor = plugin.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin); // plugin without a UI
        crashguard::note (("editor open: " + plugin.getName()).toStdString());

        const auto resizable = editor->isResizable();
        if (context.engine != nullptr && context.find) // FL's wrapper header and settings page (round 146a)
            setContentOwned (new WrappedEditor (editor, *context.engine, std::move (context.find), plugin), true);
        else
            setContentOwned (editor, true);
        setResizable (resizable, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
        plugin.addListener (this); // the editor's gestures: Last tweaked, automation recording (REA round 143)
    }

    ~PluginWindow() override
    {
        crashguard::note (("closing the editor of " + plugin.getName()).toStdString());
        plugin.removeListener (this);
        // the UI must be destroyed before the plugin (which may die together with `owner`)
        clearContentComponent();
    }

    void closeButtonPressed() override;

    juce::AudioPluginInstance* key() const noexcept { return &plugin; }

private:
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { gesture (index, true); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { gesture (index, false); }
    void gesture (int index, bool starting)
    {
        auto* p = &plugin;
        juce::MessageManager::callAsync ([p, index, starting] {
            if (PluginWindows::onGesture)
                PluginWindows::onGesture (*p, index, starting);
        });
    }

    std::shared_ptr<void> owner;
    juce::AudioPluginInstance& plugin;
};

} // namespace

std::function<void (const juce::AudioPluginInstance&, int, bool)> PluginWindows::onGesture;

namespace
{
std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>>& windows()
{
    static std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>> open;
    return open;
}

void PluginWindow::closeButtonPressed()
{
    const auto* k = key();
    // close after leaving this callback (the window deletes itself)
    juce::MessageManager::callAsync ([k] { windows().erase (const_cast<juce::AudioPluginInstance*> (k)); });
}

class ScanThread final : public juce::ThreadWithProgressWindow
{
public:
    ScanThread (AudioEngine& e, std::function<void (int)> done)
        : juce::ThreadWithProgressWindow (tr ("Scanning for plugins ({0})...", { platform::pluginFormatNames() }), true, true),
          engine (e),
          onDone (std::move (done))
    {
    }

    // called on the UI thread when the scan finishes (or is cancelled)
    void threadComplete (bool) override
    {
        if (onDone)
            onDone (found);

        juce::MessageManager::callAsync ([this] { delete this; });
    }

    void run() override
    {
        found = engine.getPluginHost().scan (engine.getPluginHost().pluginFolders(), [this] (float progress, const juce::String& file) {
            setProgress (progress);
            setStatusMessage (juce::File (file).getFileName());
            return ! threadShouldExit();
        });
    }

    int found = 0;

private:
    AudioEngine& engine;
    std::function<void (int)> onDone;
};
} // namespace

namespace PluginWindows
{
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title, WrapperContext context)
{
    auto& open = windows();

    if (auto it = open.find (&plugin); it != open.end())
    {
        it->second->toFront (true);
        return;
    }

    open[&plugin] = std::make_unique<PluginWindow> (std::move (owner), plugin, title, std::move (context));
}

void closeAll()
{
    windows().clear();
}

bool isOpen (const juce::AudioPluginInstance& plugin)
{
    return windows().count (const_cast<juce::AudioPluginInstance*> (&plugin)) > 0;
}

void close (const juce::AudioPluginInstance& plugin)
{
    windows().erase (const_cast<juce::AudioPluginInstance*> (&plugin));
}
} // namespace PluginWindows

void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone)
{
    (new ScanThread (engine, std::move (onDone)))->launchThread(); // deletes itself when done
}

namespace
{
void toggleWrapperOption (AudioEngine& engine, const std::function<PluginRef* (Song&)>& find, juce::AudioPluginInstance* instance,
                          bool PluginRef::*option, bool recreates)
{
    if (recreates)
    {
        if (instance != nullptr && PluginWindows::isOpen (*instance))
            PluginWindows::close (*instance);
        engine.capturePluginStates(); // the new instance starts from the current settings
    }
    engine.editSong ([find, option] (Song& song) {
        if (auto* ref = find (song))
            ref->*option = ! (ref->*option);
    });
}
} // namespace

juce::PopupMenu pluginProcessingMenu (AudioEngine& engine, const PluginRef& current, juce::AudioPluginInstance* instance,
                                      std::function<PluginRef* (Song&)> find)
{
    auto toggle = [&engine, find, instance] (bool PluginRef::*option) {
        if (instance != nullptr)
            PluginWindows::close (*instance);
        engine.capturePluginStates(); // the new instance starts from the current settings
        engine.editSong ([find, option] (Song& song) {
            if (auto* ref = find (song))
                ref->*option = ! (ref->*option);
        });
    };

    juce::PopupMenu menu;
    menu.addItem (tr ("Use fixed size buffers (adds latency)"), true, current.fixedBuffers, [toggle] { toggle (&PluginRef::fixedBuffers); });
    // FL's options of fixed buffers (REA round 134): 2 ms blocks, or the whole device buffer
    menu.addItem (tr ("Process maximum size buffers (more latency, only when necessary)"), current.fixedBuffers, current.fixedBuffers && current.maxSizeBuffers,
                  [toggle] { toggle (&PluginRef::maxSizeBuffers); });
    // FL's "Use maximum buffer size from host" (REA round 139): the plugin is prepared for the host's largest block
    // instead of the fixed block (for plugins that need it to size their buffers)
    menu.addItem (tr ("Use maximum buffer size from host"), current.fixedBuffers, current.fixedBuffers && current.hostMaxBuffer,
                  [toggle] { toggle (&PluginRef::hostMaxBuffer); });
    // with Options › "Run plugins in a separate process" on, every plugin already runs in one
    const auto all = pluginSandboxEnabled();
    menu.addItem (all ? tr ("Run in a separate process (on for all plugins in Options)") : tr ("Run in a separate process"), ! all,
                  all || current.separateProcess, [toggle] { toggle (&PluginRef::separateProcess); });
    // FL's "Allow threaded processing" (on by default): off keeps this plugin on the audio thread itself, for plugins
    // that misbehave when called from several threads. Nothing is re-created.
    menu.addItem (tr ("Allow threaded processing"), true, current.threaded, [&engine, find] {
        engine.editSong ([find] (Song& song) {
            if (auto* ref = find (song))
                ref->threaded = ! ref->threaded;
        });
    });

    // FL's wrapper "Allow smart disable" (on by default): off = never skipped in silence, even with the slot's
    // "Smart disable" on (for plugins that misbehave when they are not called)
    menu.addItem (tr ("Allow smart disable"), true, current.allowSmartDisable, [&engine, find] {
        engine.editSong ([find] (Song& song) {
            if (auto* ref = find (song))
                ref->allowSmartDisable = ! ref->allowSmartDisable;
        });
    });

    // FL's wrapper "MIDI input port": the MIDI Out channels on this port play the plugin (REA_rodada133 §4)
    juce::PopupMenu ports;
    auto setPort = [&engine, find] (int port) {
        engine.editSong ([find, port] (Song& song) {
            if (auto* ref = find (song))
                ref->midiInPort = port;
        });
    };
    ports.addItem (tr ("None"), true, current.midiInPort < 0, [setPort] { setPort (-1); });
    for (int port = 0; port < 32; ++port)
        ports.addItem (tr ("Port {0}", { juce::String (port) }), true, current.midiInPort == port, [setPort, port] { setPort (port); });
    menu.addSubMenu (current.midiInPort >= 0 ? tr ("MIDI input port ({0})", { juce::String (current.midiInPort) }) : tr ("MIDI input port"), ports);
    return menu;
}

void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen)
{
    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    const auto& favourites = engine.getPluginHost().favourites();
    int added = 0;

    // favourites first (Plugin manager), then all of them
    for (const auto favouritesPass : { true, false })
    {
        const auto before = added;
        for (const auto& type : types)
        {
            if (type.isInstrument != instruments)
                continue;

            PluginRef ref;
            ref.id = type.createIdentifierString().toStdString();
            ref.name = type.name.toStdString();
            if (favouritesPass && favourites.count (ref.id) == 0)
                continue;
            menu.addItem ((favouritesPass ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + type.name + " (" + type.manufacturerName + ")  ["
                              + type.pluginFormatName + "]",
                          [ref, onChosen] { onChosen (ref); });
            ++added;
        }
        if (favouritesPass && added > before)
            menu.addSeparator();
    }
    added = 0;
    for (const auto& type : types)
        added += type.isInstrument == instruments ? 1 : 0;

    if (added == 0)
        menu.addItem (tr ("(no plugins found; use \"Scan for plugins\")"), false, false, [] {});

    menu.addSeparator();
    menu.addItem (tr ("Scan for plugins ({0})...", { platform::pluginFormatNames() }), [&engine] { engine.getPluginHost().startBackgroundScan(); });
}
} // namespace lynx
