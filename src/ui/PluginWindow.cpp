#include "ui/WindowOwner.h"
#include "ui/PluginWindow.h"

#include <algorithm>
#include <map>

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
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          owner (std::move (keepAlive)),
          plugin (p)
    {
        setUsingNativeTitleBar (true);

        crashguard::note (("opening the editor of " + plugin.getName()).toStdString()); // session breadcrumb (round 111)
        if (auto* editor = plugin.createEditorIfNeeded())
            setContentOwned (editor, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (plugin), true); // plugin without a UI
        crashguard::note (("editor open: " + plugin.getName()).toStdString());

        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }

    ~PluginWindow() override
    {
        crashguard::note (("closing the editor of " + plugin.getName()).toStdString());
        // the UI must be destroyed before the plugin (which may die together with `owner`)
        clearContentComponent();
    }

    void closeButtonPressed() override;

    juce::AudioPluginInstance* key() const noexcept { return &plugin; }

private:
    std::shared_ptr<void> owner;
    juce::AudioPluginInstance& plugin;
};

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
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title)
{
    auto& open = windows();

    if (auto it = open.find (&plugin); it != open.end())
    {
        it->second->toFront (true);
        return;
    }

    open[&plugin] = std::make_unique<PluginWindow> (std::move (owner), plugin, title);
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
    // with Options › "Run plugins in a separate process" on, every plugin already runs in one
    const auto all = pluginSandboxEnabled();
    menu.addItem (all ? tr ("Run in a separate process (on for all plugins in Options)") : tr ("Run in a separate process"), ! all,
                  all || current.separateProcess, [toggle] { toggle (&PluginRef::separateProcess); });

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
