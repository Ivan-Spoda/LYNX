#include "ui/PluginWindow.h"

#include <map>

#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title)
        : DocumentWindow (title, theme::window, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          owner (std::move (keepAlive)),
          plugin (p)
    {
        setUsingNativeTitleBar (true);

        if (auto* editor = plugin.createEditorIfNeeded())
            setContentOwned (editor, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (plugin), true); // plugin sem interface

        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~PluginWindow() override
    {
        // a interface precisa morrer antes do plugin (que pode morrer junto com `owner`)
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
    // fecha depois de sair deste callback (a janela se apaga)
    juce::MessageManager::callAsync ([k] { windows().erase (const_cast<juce::AudioPluginInstance*> (k)); });
}

class ScanThread final : public juce::ThreadWithProgressWindow
{
public:
    ScanThread (AudioEngine& e, std::function<void (int)> done)
        : juce::ThreadWithProgressWindow (utf8 ("Procurando plugins (VST3 e CLAP)..."), true, true),
          engine (e),
          onDone (std::move (done))
    {
    }

    // chamado na thread da UI quando a varredura termina (ou é cancelada)
    void threadComplete (bool) override
    {
        if (onDone)
            onDone (found);

        juce::MessageManager::callAsync ([this] { delete this; });
    }

    void run() override
    {
        found = engine.getPluginHost().scan (PluginHost::defaultSearchPath(), [this] (float progress, const juce::String& file) {
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
} // namespace PluginWindows

void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone)
{
    (new ScanThread (engine, std::move (onDone)))->launchThread(); // se apaga sozinho no fim
}

void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen)
{
    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    int added = 0;

    for (const auto& type : types)
    {
        if (type.isInstrument != instruments)
            continue;

        PluginRef ref;
        ref.id = type.createIdentifierString().toStdString();
        ref.name = type.name.toStdString();
        menu.addItem (type.name + " (" + type.manufacturerName + ")  [" + type.pluginFormatName + "]", [ref, onChosen] { onChosen (ref); });
        ++added;
    }

    if (added == 0)
        menu.addItem (utf8 ("(nenhum plugin encontrado; use \"Procurar plugins\")"), false, false, [] {});

    menu.addSeparator();
    menu.addItem (utf8 ("Procurar plugins (VST3 e CLAP)..."), [&engine] {
        scanPluginsWithProgress (engine, [] (int found) {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Plugins",
                                                    juce::String (found) + utf8 (" plugin(s) novo(s) encontrado(s)."));
        });
    });
}
} // namespace lynx
