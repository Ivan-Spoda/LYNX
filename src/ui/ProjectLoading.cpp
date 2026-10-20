#include "ui/ProjectLoading.h"

#include <atomic>
#include <thread>

#include "core/FlPluginDb.h"
#include "core/LoadTrace.h"
#include "flp/Flp.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
std::atomic<bool> running { false };
class LoadPanel;
LoadPanel* current = nullptr; // message thread

class LoadPanel final : public juce::Component, private juce::Timer
{
public:
    LoadPanel (juce::Component& parent, AudioEngine& e, juce::File f, std::function<void (bool, const juce::String&)> d)
        : engine (e), file (std::move (f)), done (std::move (d))
    {
        running = true;
        current = this;
        loadtrace::begin (file.getFullPathName().toStdString());
        loadtrace::stage (0.0, 0.02, TR_NOOP ("Reading the project..."));
        setWantsKeyboardFocus (true);
        setAlwaysOnTop (true);
        parent.addAndMakeVisible (this);
        setBounds (parent.getLocalBounds());
        grabKeyboardFocus();
        engine.stop();
        startScan();
        startTimerHz (30);
    }

    ~LoadPanel() override
    {
        stopTimer();
        abort = true;
        if (worker.joinable())
            worker.join();
        running = false;
        if (current == this)
            current = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0.withAlpha (0.72f));
        const auto box = panelBounds();
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (box.toFloat(), 8.0f);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 8.0f, 1.0f);

        auto area = box.reduced (20, 16);
        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::Font (theme::type::font (15.0f, true)));
        g.drawFittedText (tr ("Opening {0}", { file.getFileNameWithoutExtension() }), area.removeFromTop (22), juce::Justification::centredLeft, 1);
        area.removeFromTop (12);

        const auto fraction = juce::jlimit (0.0, 1.0, shown);
        auto bar = area.removeFromTop (8).toFloat();
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (theme::colour::accent);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * (float) fraction)), 4.0f);
        area.removeFromTop (10);

        auto line = area.removeFromTop (18);
        g.setFont (juce::Font (theme::type::font (theme::type::small)));
        g.setColour (theme::colour::textSecondary);
        g.drawText (juce::String (juce::roundToInt (fraction * 100.0)) + " %", line.removeFromRight (48), juce::Justification::centredRight);
        g.drawFittedText (statusText, line, juce::Justification::centredLeft, 1);
    }

    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override {}
    bool keyPressed (const juce::KeyPress&) override { return true; } // nothing reaches the project being replaced

private:
    enum class Step
    {
        scanning,
        preparing,
        plugins,
        finishing,
        finished
    };

    juce::Rectangle<int> panelBounds() const
    {
        return juce::Rectangle<int> (0, 0, juce::jmin (480, getWidth() - 32), 104).withCentre (getLocalBounds().getCentre());
    }

    // 1. the project's plugins LYNX doesn't know yet, and has not searched for already: a scan of just their files
    void startScan()
    {
        std::vector<FlpPluginUse> uses;
        {
            const loadtrace::Scope timing ("plugin_lookup");
            if (juce::MemoryBlock data; file.loadFileAsData (data))
                try
                {
                    uses = flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }));
                }
                catch (const flp::Error&)
                {
                }
            auto& host = engine.getPluginHost();
            uses.erase (std::remove_if (uses.begin(), uses.end(),
                                        [&host] (const FlpPluginUse& u) { return host.findForeign (u.plugin, u.instrument) || host.recentlyMissed (u); }),
                        uses.end());
        }
        step = Step::scanning;
        if (uses.empty())
        {
            startPrepare();
            return;
        }
        auto& host = engine.getPluginHost();
        if (host.backgroundScan().running)
        {
            host.cancelBackgroundScan(); // the targeted scan goes first; the full one resumes afterwards
            resumeBackground = true;
        }
        loadtrace::stage (0.02, 0.20, TR_NOOP ("Looking for this project's plugins..."), TR_NOOP ("Checking {0}... {1}/{2}"));
        worker = std::thread ([this, uses] {
            auto& h = engine.getPluginHost();
            auto t = std::chrono::steady_clock::now();
            const auto files = h.filesToScanFor (uses);
            loadtrace::add ("plugin_scan_list", loadtrace::msSince (t));
            t = std::chrono::steady_clock::now();
            h.scanFiles (files, [this, &files] (float, const juce::String& f) {
                const auto index = files.indexOf (f);
                loadtrace::step (index, files.size(), juce::File::createFileWithoutCheckingPath (f).getFileName().toStdString(), index + 1);
                return ! abort.load();
            });
            loadtrace::add ("plugin_scan", loadtrace::msSince (t), files.size());
            // still not found: not searched for again on the next opens (until a full scan or a week passes)
            std::vector<FlpPluginUse> missed;
            for (const auto& u : uses)
                if (! h.findForeign (u.plugin, u.instrument))
                    missed.push_back (u);
            if (! abort)
                h.rememberMisses (missed);
            stageDone = true;
        });
    }

    // 2. read, parse, import, decode the samples (no plugin touched): a thread
    void startPrepare()
    {
        if (worker.joinable())
            worker.join();
        if (resumeBackground)
            engine.getPluginHost().startBackgroundScan();
        resumeBackground = false;
        step = Step::preparing;
        stageDone = false;
        worker = std::thread ([this] {
            AudioEngine::prepareFlpImport (file, {}, engine.getSampleLibrary(), prepared);
            stageDone = true;
        });
    }

    void timerCallback() override
    {
        if (auto* parent = getParentComponent(); parent != nullptr && getBounds() != parent->getLocalBounds())
            setBounds (parent->getLocalBounds()); // (the main window may still be settling its size at start-up)
        if (step == Step::scanning && stageDone)
            startPrepare();
        else if (step == Step::preparing && stageDone)
        {
            worker.join();
            if (! prepared.ok)
            {
                finish (false, prepared.error);
                return;
            }
            step = Step::plugins;
            loadtrace::stage (0.55, 0.90, TR_NOOP ("Loading plugins..."), TR_NOOP ("Loading {0}... {1}/{2}"));
            refresh();
            return; // (the panel shows the stage before the first plugin blocks the thread)
        }
        else if (step == Step::plugins)
        {
            // 3. one hosted plugin per turn, so the panel repaints between them
            if (! engine.preloadNextPlugin (prepared))
            {
                step = Step::finishing;
                loadtrace::stage (0.90, 0.99, TR_NOOP ("Building the mixer..."));
            }
            refresh();
            return;
        }
        else if (step == Step::finishing)
        {
            // 4. the rack (everything decoded and created already)
            const auto ok = engine.finishFlpImport (std::move (prepared), false);
            finish (ok, ok ? juce::String() : juce::String ("could not import the project"));
            return;
        }
        refresh();
    }

    void refresh()
    {
        const auto target = loadtrace::progress();
        shown = juce::jmax (shown, target);
        const auto st = loadtrace::status();
        statusText = juce::String::fromUTF8 (loadtrace::describe (st, [] (const std::string& s) { return tr (juce::String::fromUTF8 (s.c_str())).toStdString(); }).c_str());
        repaint();
        // the next plugin blocks this thread: paint now, not when the message queue happens to be empty
        if (step == Step::plugins || step == Step::finishing)
            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
    }

    void finish (bool ok, const juce::String& error)
    {
        step = Step::finished;
        stopTimer();
        loadtrace::end();
        writeLoadLog();
        auto callback = std::move (done);
        // the panel goes first (the callback may open dialogs over the main window)
        juce::MessageManager::callAsync ([callback, ok, error] {
            if (callback)
                callback (ok, error);
        });
        running = false;
        delete this;
    }

    AudioEngine& engine;
    juce::File file;
    std::function<void (bool, const juce::String&)> done;
    AudioEngine::PreparedFlp prepared;
    std::thread worker;
    std::atomic<bool> stageDone { false }, abort { false };
    bool resumeBackground = false;
    Step step = Step::scanning;
    double shown = 0.0;
    juce::String statusText;
};
} // namespace

void openFlpWithProgress (juce::Component& parent, AudioEngine& engine, const juce::File& file,
                          std::function<void (bool ok, const juce::String& error)> done)
{
    if (running)
        return;
    new LoadPanel (parent, engine, file, std::move (done)); // owned by itself (deleted when finished)
}

bool projectLoadRunning()
{
    return running;
}

void cancelProjectLoad()
{
    delete current; // (joins its thread; the project being opened is dropped)
}

void writeLoadLog()
{
    const auto log = juce::File (PluginHost::settingsFolder()).getChildFile ("load.log");
    // the last few opens, newest at the end
    auto text = log.loadFileAsString();
    if (text.length() > 60000)
        text = text.substring (text.length() - 40000);
    text << juce::Time::getCurrentTime().toString (true, true) << "\n" << juce::String::fromUTF8 (loadtrace::table().c_str()) << "\n";
    log.replaceWithText (text);
}
} // namespace lynx
