#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Effects.h"
#include "core/SynthPresets.h"
#include "core/FlWrapper.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/MidiFileIo.h"
#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"
#include "engine/ProjectFile.h"
#include "engine/SampleLibrary.h"
#include "engine/Text.h"
#include "platform/FileTypes.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/Dialogs.h"
#include "core/RackOps.h"
#include "ui/ExportDialog.h"
#include "ui/I18n.h"
#include "ui/MainComponent.h"
#include "ui/SetupWizard.h"
#include "engine/FlStudio.h"
#include "ui/HistoryPanel.h"
#include "ui/MissingFilesDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/NoteToolDialogs.h"
#include "ui/ParametricEqEditor.h"
#include "ui/Splash.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "ui/WindowOwner.h"

namespace
{
lynx::AudioEngine* crashEngine = nullptr;

// Crashed: try to save the project and write the backtrace (like FL's "emergency save").
void onCrash (void*)
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave");
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    folder.getChildFile ("crash " + stamp + ".txt").replaceWithText (juce::SystemStats::getStackBacktrace());

    if (crashEngine != nullptr)
    {
        juce::String error;
        lynx::ProjectFile::save (crashEngine->getSong(), folder.getChildFile ("recovered " + stamp + lynx::ProjectFile::extension), error);
    }
}
} // namespace

class LynxApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // the plugin scan runs copies of LYNX as child processes
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // a crash in any mode (UI, command line) writes its backtrace to %APPDATA%\LYNX\autosave
        juce::SystemStats::setApplicationCrashHandler (onCrash);

        // child process of a sandboxed plugin: no window or audio of its own
        if (lynx::runPluginSandboxWorker (commandLine))
            return;

        const auto args = juce::StringArray::fromTokens (commandLine, true);

        // interface language: saved in %APPDATA%\LYNX\language.txt; --lang pt-BR overrides it for this run
        lynx::i18n::initialise (args);

        if (args.contains ("--showcase"))
        {
            // LYNX's own demo project (README screenshots): --showcase output.lynx [pattern]
            const auto i = args.indexOf ("--showcase");
            runShowcase (args[i + 1].unquoted(), args[i + 2].getIntValue());
            return;
        }

        if (args.contains ("--sandboxtest"))
        {
            const auto i = args.indexOf ("--sandboxtest");
            runSandboxTest (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--selftest"))
        {
            const auto i = args.indexOf ("--selftest");
            runSelfTest (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--scan-one"))
        {
            // child process of the plugin scan (see PluginHost::scan)
            const auto i = args.indexOf ("--scan-one");
            lynx::PluginHost::scanOneFile (args[i + 1].unquoted(), args[i + 2].unquoted(), juce::File (args[i + 3].unquoted()));
            setApplicationReturnValue (0);
            quit();
            return;
        }

        if (args.contains ("--paths"))
        {
            // diagnostics: folders LYNX searches
            const auto programs = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
            juce::String report;
            report << "programs=" << programs.getFullPathName() << "\n";
            for (const auto& dir : programs.getChildFile ("Image-Line").findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
                report << "fl=" << dir.getFullPathName() << " packs=" << (int) dir.getChildFile ("Data").getChildFile ("Patches").getChildFile ("Packs").isDirectory() << "\n";
            juce::File (args[args.indexOf ("--paths") + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--fldb"))
        {
            // diagnostics: LYNX.exe --fldb report.txt [project.flp]: the FL Studio installation, its plugin database,
            // the suggested plugin folders and, for a project, how each of its plugins resolves (read-only)
            const auto i = args.indexOf ("--fldb");
            const auto fl = lynx::findFlStudio();
            lynx::PluginHost host (false);
            juce::String report;
            report << "install=" << fl.installDir.getFullPathName() << "\nuserdata=" << fl.userData.getFullPathName() << "\n";
            for (const auto& f : fl.vstFolders)
                report << "flvstfolder=" << f << "\n";
            report << "dbfiles=" << host.flPluginDatabaseFiles() << " entries=" << (int) host.flPluginDatabase().entries().size() << "\n";
            for (const auto& e : host.flPluginDatabase().entries())
                report << "  entry: " << juce::String::fromUTF8 (e.name.c_str()) << " | " << juce::String::fromUTF8 (e.path.c_str()) << " | role=" << e.role << "\n";
            const auto suggested = host.suggestedFolders();
            for (int k = 0; k < suggested.getNumPaths(); ++k)
                report << "folder=" << suggested[k].getFullPathName() << "\n";
            if (const juce::File project (args[i + 2].unquoted()); project.existsAsFile())
            {
                juce::MemoryBlock data;
                project.loadFileAsData (data);
                try
                {
                    for (const auto& use : lynx::flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() })))
                    {
                        juce::String how;
                        const auto d = host.findForeign (use.plugin, use.instrument, &how);
                        const auto* entry = host.flPluginDatabase().match (use.plugin, use.instrument);
                        report << "plugin: " << juce::String::fromUTF8 (use.plugin.name.c_str()) << " -> "
                               << (d ? d->fileOrIdentifier + " (" + how + ")" : juce::String ("not known"))
                               << (entry != nullptr ? " | fl db: " + juce::String::fromUTF8 (entry->path.c_str()) : juce::String()) << "\n";
                    }
                    const auto uses = lynx::flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }));
                    const auto candidates = host.filesToScanFor (uses);
                    report << "scan_candidates=" << candidates.joinIntoString (";") << "\n";
                    if (args.contains ("--scan"))
                    {
                        // the targeted scan an open would run (child processes; the user's plugin list is not touched)
                        report << "scanned_new=" << host.scanFiles (candidates, {}) << "\n";
                        for (const auto& use : uses)
                        {
                            juce::String how;
                            const auto d = host.findForeign (use.plugin, use.instrument, &how);
                            report << "after_scan: " << juce::String::fromUTF8 (use.plugin.name.c_str()) << " -> "
                                   << (d ? d->fileOrIdentifier + " (" + how + ")" : juce::String ("not known")) << "\n";
                        }
                    }
                }
                catch (const flp::Error& e)
                {
                    report << "error=" << e.what() << "\n";
                }
            }
            juce::File (args[i + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--devices") || args.contains ("--dispositivos")) // --dispositivos: legacy alias
        {
            // diagnostics: driver types and output devices JUCE offers (without opening any)
            juce::AudioDeviceManager manager;
            juce::String report;

            for (auto* type : manager.getAvailableDeviceTypes())
            {
                type->scanForDevices();
                report << "type=" << type->getTypeName() << "\n";
                for (const auto& name : type->getDeviceNames (false))
                    report << "  output=" << name << "\n";
            }

            const auto flag = args.contains ("--devices") ? args.indexOf ("--devices") : args.indexOf ("--dispositivos");
            juce::File (args[flag + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--devicetest"))
        {
            // LYNX.exe --devicetest "type" "device" report.txt: opens that output device, receives 1 s of callbacks
            // writing silence, closes it and reports rate, block size, latency and callbacks (driver check, no sound)
            const auto i = args.indexOf ("--devicetest");
            const auto typeName = args[i + 1].unquoted(), deviceName = args[i + 2].unquoted();
            juce::String report;
            const juce::File reportFile (args[i + 3].unquoted());
            auto step = [&] (const juce::String& line) { report << line << "\n"; reportFile.replaceWithText (report); }; // progress survives a driver that ends the process
            step ("step=start");
            juce::AudioDeviceManager manager;
            std::unique_ptr<juce::AudioIODevice> device;
            for (auto* type : manager.getAvailableDeviceTypes())
                if (type->getTypeName() == typeName)
                {
                    type->scanForDevices();
                    step ("step=create");
                    device.reset (type->createDevice (deviceName, {}));
                    step ("step=created");
                }
            struct Silence final : juce::AudioIODeviceCallback
            {
                std::atomic<int> calls { 0 };
                void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int channels, int n,
                                                       const juce::AudioIODeviceCallbackContext&) override
                {
                    for (int c = 0; c < channels; ++c)
                        if (out[c] != nullptr)
                            juce::FloatVectorOperations::clear (out[c], n);
                    ++calls;
                }
                void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
                void audioDeviceStopped() override {}
            } silence;
            bool ok = false;
            if (device == nullptr)
                step ("error=device not found");
            else
            {
                step ("step=open");
                juce::BigInteger outputs;
                outputs.setRange (0, 2, true);
                const auto rates = device->getAvailableSampleRates();
                const auto rate = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate()
                                  : rates.contains (48000.0) ? 48000.0 : rates.isEmpty() ? 44100.0 : rates[0];
                step ("rates=" + juce::String (rates.size()) + " chosen=" + juce::String (rate) + " default_block=" + juce::String (device->getDefaultBufferSize()));
                const auto error = device->open ({}, outputs, rate, device->getDefaultBufferSize());
                if (error.isNotEmpty())
                    step ("error=" + error);
                else
                {
                    step ("step=start_callbacks");
                    device->start (&silence);
                    juce::Thread::sleep (1000);
                    device->stop();
                    report << "rate=" << device->getCurrentSampleRate() << "\n"
                           << "block=" << device->getCurrentBufferSizeSamples() << "\n"
                           << "output_latency=" << device->getOutputLatencyInSamples() << "\n"
                           << "callbacks=" << silence.calls.load() << "\n";
                    ok = silence.calls.load() > 0;
                    device->close();
                }
            }
            step (juce::String ("result=") + (ok ? "OK" : "FAILED"));
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        if (args.contains ("--scantest"))
        {
            // LYNX.exe --scantest folder report.txt: scan (in child processes) without touching the user's list
            const auto i = args.indexOf ("--scantest");
            lynx::PluginHost host (false);
            const auto found = host.scan (juce::FileSearchPath (args[i + 1].unquoted()), {});
            juce::String report;
            report << "found=" << found << "\n";

            for (const auto& t : host.getKnownPlugins().getTypes())
                report << "  " << (t.isInstrument ? "instrument: " : "effect: ") << t.name << "\n";

            for (const auto& f : host.getKnownPlugins().getBlacklistedFiles())
                report << "  blacklisted: " << f << "\n";

            juce::File (args[i + 2].unquoted()).replaceWithText (report);
            setApplicationReturnValue (found > 0 ? 0 : 1);
            quit();
            return;
        }

        if (args.contains ("--wrappertest"))
        {
            const auto i = args.indexOf ("--wrappertest");
            runWrapperTest (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--plugintest"))
        {
            const auto i = args.indexOf ("--plugintest");
            runPluginTest (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--snapshot"))
        {
            const auto i = args.indexOf ("--snapshot");
            runSnapshot (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--stems"))
        {
            // LYNX.exe --stems project base.wav: one WAV per insert that has sound (like "Export stems")
            const auto i = args.indexOf ("--stems");
            juce::String error, report;
            int written = 0;
            {
                lynx::AudioEngine loader (false);
                const juce::File input (args[i + 1].unquoted()), base (args[i + 2].unquoted());
                auto ok = input.hasFileExtension ("flp") ? loader.importFlpFile (input, error) : false;

                if (! input.hasFileExtension ("flp"))
                    if (auto song = lynx::ProjectFile::load (input, error))
                    {
                        loader.setSong (std::move (*song));
                        ok = true;
                    }

                const auto& song = loader.getSong();

                for (int t = 1; ok && t < (int) song.mixer.size(); ++t)
                {
                    if (std::none_of (song.channels.begin(), song.channels.end(), [t] (const lynx::ChannelDef& c) { return c.mixerTrack == t; }))
                        continue;

                    const auto file = base.getSiblingFile (base.getFileNameWithoutExtension() + " - " + juce::String (t) + ".wav");
                    lynx::ExportSettings settings;
                    lynx::ExportJob job (lynx::makeBounceSong (song, t), settings, file);

                    if (job.run ({}, error))
                    {
                        ++written;
                        report << file.getFileName() << " peak=" << job.getPeak() << "\n";
                    }
                }
            }

            report << "stems=" << written << "\n";
            juce::File (args[i + 2].unquoted() + ".txt").replaceWithText (report);
            setApplicationReturnValue (written > 0 ? 0 : 1);
            quit();
            return;
        }

        if (args.contains ("--export"))
        {
            const auto i = args.indexOf ("--export");
            const auto insert = args.contains ("--insert") ? args[args.indexOf ("--insert") + 1].getIntValue() : 0;
            runExport (args[i + 1].unquoted(), args[i + 2].unquoted(), args.contains ("--pattern"), insert);
            return;
        }

        if (args.contains ("--render"))
        {
            const auto i = args.indexOf ("--render");
            runRender (args[i + 1].unquoted(), args[i + 2].unquoted(), args[i + 3].getDoubleValue());
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        startupOptions = lynx::StartupOptions::load();
        lynx::registerFileTypes();
        // Double-clicking a .lynx/.flp opens LYNX with the file as an argument.
        if (args.size() > 0 && juce::File::isAbsolutePath (args[0].unquoted()))
            fileToOpen = juce::File (args[0].unquoted());

        if (! startupOptions.splash)
        {
            startEngine();
            createMainWindow();
            mainWindow->reveal();
            openStartupFile();
            return;
        }

        // the splash first, so it is on screen while the audio device opens; the main window is built
        // once the pepper has landed (the scene is still for a moment then) and shown when the splash ends
        splash = std::make_unique<lynx::SplashScreen>();
        splash->showOnDesktop();

        juce::Timer::callAfterDelay (40, [this] {
            startEngine();

            if (splash == nullptr) // quit meanwhile
                return;

            splash->setStatus (lynx::tr ("Loading the workspace..."));
            splash->startClock();

            juce::Timer::callAfterDelay (1050, [this] {
                if (splash == nullptr || engine == nullptr)
                    return;

                createMainWindow();
                const auto wait = juce::jmax (0.0, lynx::SplashScreen::minimumDuration - splash->getElapsed());

                juce::Timer::callAfterDelay ((int) (wait * 1000.0), [this] {
                    if (splash == nullptr || mainWindow == nullptr)
                        return;

                    mainWindow->reveal();
                    splash->toFront (false);
                    splash->finish ([this] { splash = nullptr; });
                    openStartupFile();
                    // first run: language, audio, plugin folders (FL Studio's included) and a background scan
                    if (! lynx::setupWizardDone())
                        mainWindow->showSetupWizard();
                });
            });
        });
    }

    void startEngine()
    {
        engine = std::make_unique<lynx::AudioEngine>();
        crashEngine = engine.get();

        if (startupOptions.sound)
            engine->previewSample (lynx::makeStartupSample());
    }

    void createMainWindow()
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
        lynx::setMainWindowForOwnership (mainWindow.get());
    }

    void openStartupFile()
    {
        if (fileToOpen != juce::File() && mainWindow != nullptr)
            mainWindow->openFile (fileToOpen);
    }

    void shutdown() override
    {
        lynx::shutdownPluginSandboxWorker();
        splash = nullptr;
        mainWindow = nullptr; // the UI goes before the engine
        crashEngine = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->confirmDiscard ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

private:
    // LYNX.exe --selftest report.txt: renders 2 s without a sound card and writes the result.
    // Exit code 0 = audio came out, 1 = silence.
    void runSelfTest (const juce::String& reportPath, const juce::String& demoProjectPath)
    {
        lynx::AudioEngine offline (false);
        const auto peak = offline.renderOffline (2.0, 48000.0);
        const auto audioOk = peak > 0.01f && peak < 10.0f;

        // project format round trip, with accents in the name
        auto original = lynx::Song::makeDefault();
        original.channels[0].name = "Bumbo ação";
        original.playlist.push_back ({ 0, 2, 3840, 1920, 960 });
        original.mode = lynx::PlayMode::song;
        original.mixer[3].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::delay));
        original.mixer[3].effects[0].params[1] = 0.25f;
        lynx::Automation automation;
        automation.name = "Pan";
        automation.target = { lynx::AutomationTargetKind::effectParam, 3, 0, 1 };
        automation.points = { { 0, 0.25f, 0.5f }, { 3840, 1.0f, 0.0f } };
        original.automations.push_back (automation);
        lynx::Clip automationClip;
        automationClip.kind = lynx::ClipKind::automation;
        automationClip.automation = 0;
        automationClip.track = 4;
        automationClip.length = 3840;
        original.playlist.push_back (automationClip);
        original.loopStart = 960;
        original.loopEnd = 7680;
        juce::String error;

        if (demoProjectPath.isNotEmpty()) // sample project for checking the UI with --snapshot
            lynx::ProjectFile::save (original, juce::File (demoProjectPath), error);

        const auto reloaded = lynx::ProjectFile::fromJson (lynx::ProjectFile::toJson (original), error);
        const auto projectOk = reloaded && reloaded->channels.size() == original.channels.size()
                               && reloaded->channels[0].name == original.channels[0].name
                               && reloaded->patterns[0].notes.size() == original.patterns[0].notes.size()
                               && reloaded->patterns[0].notes[3].start == original.patterns[0].notes[3].start
                               && reloaded->mode == lynx::PlayMode::song && reloaded->playlist.size() == 2
                               && reloaded->playlist[1].kind == lynx::ClipKind::automation && reloaded->playlist[1].automation == 0
                               && reloaded->automations.size() == 1 && reloaded->automations[0].target == automation.target
                               && reloaded->automations[0].points.size() == 2 && reloaded->automations[0].points[0].tension == 0.5f
                               && reloaded->automations[0].points[1].tick == 3840
                               && reloaded->loopStart == 960 && reloaded->loopEnd == 7680
                               && reloaded->playlist[0].offset == 960 && reloaded->playlist[0].track == 2
                               && reloaded->mixer[3].effects.size() == 1
                               && reloaded->mixer[3].effects[0].kind == lynx::EffectKind::delay
                               && reloaded->mixer[3].effects[0].params[1] == 0.25f;
        // round trip of the newer features: synth/sampler with modulation, EQs, utility, markers
        auto rich = lynx::Song::makeDefault();
        auto& synth = rich.channels[4].synth;
        synth.osc[1] = { lynx::Waveform::square, -1, 7.0f, 0.4f, 5, -0.25f };
        synth.filterType = 2;
        synth.mono = true;
        synth.portaBeats = 0.125f;
        synth.am = true;
        synth.filterEnv = { 3.0f, 0.01f, 0.02f, 0.3f, 0.25f, 0.4f };
        synth.lfo[lynx::lfoPitch] = { 0.5f, 0.25f, true, 1, 0.1f, 0.2f };
        auto& sampler = rich.channels[0].sampler;
        sampler.cutoff = 1200.0f;
        sampler.resonance = 0.3f;
        sampler.filterType = 1;
        sampler.pitchEnv = { -12.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.1f };
        sampler.lfo[lynx::lfoVolume] = { 0.5f, 0.5f, false, 2, 0.0f, 0.0f };
        rich.mixer[2].eq.gainDb = { 3.0f, -2.5f, 1.0f };
        rich.mixer[2].eq.hz[1] = 2200.0f;
        auto peqSlot = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
        peqSlot.params[(std::size_t) lynx::peq::param (2, 2)] = lynx::peq::gainValue (6.0);
        peqSlot.params[(std::size_t) (lynx::peq::slopeParam + 0)] = lynx::peq::slopeValue (3);
        rich.mixer[2].effects.push_back (peqSlot);
        auto utilitySlot = lynx::makeEffectSlot (lynx::EffectKind::utility);
        utilitySlot.params[1] = 0.2f;
        rich.mixer[2].effects.push_back (utilitySlot);
        auto vocodexSlot = lynx::makeEffectSlot (lynx::EffectKind::vocodex); // FL's raw state travels as base64
        for (int i = 0; i < 300; ++i)
            vocodexSlot.state.push_back ((std::uint8_t) (i * 37));
        vocodexSlot.sidechain = 1;
        vocodexSlot.sidechain2 = 2;
        rich.mixer[3].effects.push_back (vocodexSlot);
        rich.addMarker ({ 3840, "Refrão", 0, 4 });
        rich.addMarker ({ 7680, {}, 7, 8 });
        const auto richBack = lynx::ProjectFile::fromJson (lynx::ProjectFile::toJson (rich), error);
        auto paramsClose = [] (const lynx::EffectParams& a, const lynx::EffectParams& b) {
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::abs (a[i] - b[i]) > 1.0e-3f)
                    return false;
            return true;
        };
        const auto richOk = richBack && richBack->channels[4].synth == synth && richBack->channels[0].sampler == sampler
                            && richBack->mixer[2].eq == rich.mixer[2].eq && richBack->mixer[2].effects.size() == 2
                            && richBack->mixer[2].effects[0].kind == lynx::EffectKind::parametricEq
                            && paramsClose (richBack->mixer[2].effects[0].params, peqSlot.params)
                            && richBack->mixer[2].effects[1].kind == lynx::EffectKind::utility
                            && paramsClose (richBack->mixer[2].effects[1].params, utilitySlot.params)
                            && richBack->mixer[3].effects.size() == 1 && richBack->mixer[3].effects[0].kind == lynx::EffectKind::vocodex
                            && richBack->mixer[3].effects[0].state == vocodexSlot.state && richBack->mixer[3].effects[0].sidechain2 == 2
                            && richBack->markers() == rich.markers();

        // undo/redo
        offline.editSong ([] (lynx::Song& s) { s.bpm = 99.0; });
        offline.editSong ([] (lynx::Song& s) { s.bpm = 101.0; });
        offline.undo();
        const auto afterUndo = offline.getSong().bpm;
        offline.redo();
        const auto undoOk = afterUndo == 99.0 && offline.getSong().bpm == 101.0 && offline.canUndo() && ! offline.canRedo()
                            && offline.isModified();

        // recording from the PC keyboard: a one-beat note lands in the pattern, in a single undo step
        const auto notesBefore = offline.getSong().patterns[0].notes.size();
        offline.setRecording (true);
        offline.play();
        offline.getTransport().positionTicks.store (960.0);
        offline.sendLiveNote (2, 72, 0.9f, true, true);
        offline.getTransport().positionTicks.store (1920.0);
        offline.sendLiveNote (2, 72, 0.0f, false, true);
        offline.setRecording (false);
        offline.stop();
        const auto& recorded = offline.getSong().patterns[0].notes;
        const auto recordOk = recorded.size() == notesBefore + 1
                              && std::any_of (recorded.begin(), recorded.end(), [] (const lynx::Note& n) {
                                     return n.key == 72 && n.channel == 2 && n.length == 960 && n.start >= 960 && n.start < 3840;
                                 });
        offline.undo();
        const auto recordUndoOk = offline.getSong().patterns[0].notes.size() == notesBefore;

        // MIDI: exporting the pattern and importing it back gives the same notes
        bool midiOk = false;
        {
            auto source = lynx::Song::makeDefault();
            source.patterns[0].notes.push_back ({ 1000, 480, 4, 67, 0.5f });
            source.patterns[0].sortNotes();
            const auto midiFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-selftest.mid");
            juce::String midiError;
            const auto saved = lynx::MidiFileIo::savePattern (source, 0, midiFile, midiError);
            const auto song = saved ? lynx::MidiFileIo::loadSong (midiFile, midiError) : std::nullopt;
            midiFile.deleteFile();

            if (song && song->patterns.size() == 1 && song->patterns[0].notes.size() == source.patterns[0].notes.size()
                && std::abs (song->bpm - source.bpm) < 0.01)
            {
                midiOk = true;
                const auto& a = source.patterns[0].notes;
                const auto& b = song->patterns[0].notes;
                for (std::size_t i = 0; i < a.size(); ++i)
                    midiOk = midiOk && a[i].start == b[i].start && a[i].key == b[i].key && a[i].length == b[i].length;
            }
        }

        // WavPack: a .wv from the FL packs (if installed) opens through the sample loader
        juce::String wavpack = "NO_FL";
        {
            const auto roots = lynx::findFlStudioRoots();
            const auto packs = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Packs");
            const auto found = packs.isDirectory() ? packs.findChildFiles (juce::File::findFiles, true, "*.wv") : juce::Array<juce::File> {};
            if (! found.isEmpty())
            {
                lynx::SampleLibrary library;
                const auto sample = library.load (found.getFirst().getFullPathName());
                float peakWv = 0.0f;
                if (sample != nullptr)
                    for (std::size_t i = 0; i < sample->frames(); ++i)
                        peakWv = std::max (peakWv, std::abs (sample->leftData()[i]));
                wavpack = sample != nullptr && sample->frames() > 0 && peakWv > 1e-4f ? "OK" : "ERROR";
            }
        }

        const auto ok = audioOk && projectOk && richOk && undoOk && recordOk && recordUndoOk && midiOk && wavpack != "ERROR";

        juce::String report;
        report << "connections=" << offline.getNumGraphConnections() << "\n"
               << "peak=" << peak << "\n"
               << "project=" << (projectOk ? "OK" : "ERROR " + error) << "\n"
               << "project_features=" << (richOk ? "OK" : "ERROR") << "\n"
               << "undo=" << (undoOk ? "OK" : "ERROR") << "\n"
               << "recording=" << (recordOk && recordUndoOk ? "OK" : "ERROR") << "\n"
               << "midi=" << (midiOk ? "OK" : "ERROR") << "\n"
               << "wavpack=" << wavpack << "\n"
               << "result=" << (ok ? "OK" : "FAILED") << "\n";

        if (reportPath.isNotEmpty())
            juce::File (reportPath).replaceWithText (report);

        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --plugintest plugin.vst3 report.txt: loads the plugin, plays notes if it is an
    // instrument (and runs audio through it if it is an effect) and measures the audio.
    void runPluginTest (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        {
            lynx::AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            const auto added = host.addPluginFile (juce::File (pluginPath));
            report << "plugins_in_file=" << added << "\n";

            juce::PluginDescription instrument, effect;
            bool hasInstrument = false, hasEffect = false;

            for (const auto& type : host.getKnownPlugins().getTypes())
            {
                if (juce::File (type.fileOrIdentifier).getFileName() != juce::File (pluginPath).getFileName())
                    continue;

                report << "  " << (type.isInstrument ? "instrument: " : "effect: ") << type.name << " (" << type.manufacturerName
                       << ") in " << type.fileOrIdentifier << "\n";

                if (type.isInstrument && ! hasInstrument) { instrument = type; hasInstrument = true; }
                if (! type.isInstrument && ! hasEffect) { effect = type; hasEffect = true; }
            }

            auto song = lynx::Song::makeDefault();

            if (hasInstrument)
            {
                auto& c = song.channels[4]; // the "Synth" channel becomes the plugin
                c.instrument = lynx::InstrumentKind::plugin;
                c.plugin.id = instrument.createIdentifierString().toStdString();
                c.plugin.name = instrument.name.toStdString();
                c.volume = 1.0f;

                for (int i = 0; i < 4; ++i) // one chord per beat
                    for (int key : { 60, 64, 67 })
                        song.patterns[0].notes.push_back ({ (std::int64_t) i * lynx::ppq, lynx::ppq / 2, 4, key, 0.9f });
                song.patterns[0].sortNotes();
            }

            if (hasEffect)
            {
                lynx::EffectSlot slot;
                slot.kind = lynx::EffectKind::plugin;
                slot.plugin.id = effect.createIdentifierString().toStdString();
                slot.plugin.name = effect.name.toStdString();
                song.mixer[0].effects.push_back (slot);
            }

            offline.setSong (song);

            const auto* loadedInstrument = dynamic_cast<lynx::PluginInstrument*> (offline.getChannelInstrument (4).get());
            const auto* loadedEffect = dynamic_cast<lynx::PluginEffect*> (offline.getEffect (0, 0).get());
            report << "instrument_loaded=" << (loadedInstrument != nullptr ? "yes" : "no") << "\n"
                   << "effect_loaded=" << (loadedEffect != nullptr ? "yes" : "no") << "\n";

            const auto peak = offline.renderOffline (3.0, 48000.0);
            report << "peak=" << peak << "\n";

            offline.capturePluginStates();
            const auto stateSize = hasInstrument ? offline.getSong().channels[4].plugin.state.size() : (size_t) 0;
            report << "state_bytes_base64=" << (int) stateSize << "\n";

            ok = (hasInstrument || hasEffect) && (! hasInstrument || loadedInstrument != nullptr)
                 && (! hasEffect || loadedEffect != nullptr) && peak > 0.001f && peak < 10.0f;
        }

        report << "result=" << (ok ? "OK" : "FAILED") << "\n";
        juce::File (reportPath).replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --wrappertest plugin.vst3 report.txt: builds a VST3 state the way FL's Fruity
    // Wrapper stores it (REA_rodada5 §3.2), runs it through the import (adoptFlPlugins) and checks
    // that the plugin comes back with the changed parameter.
    void runWrapperTest (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        {
            lynx::AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            host.addPluginFile (juce::File (pluginPath));
            const juce::PluginDescription* vst3 = nullptr;
            const auto types = host.getKnownPlugins().getTypes(); // copy: the pointer below points into it

            for (const auto& type : types)
                if (type.pluginFormatName == "VST3" && type.isInstrument)
                    vst3 = &type;

            if (vst3 != nullptr)
            {
                juce::String error;
                auto plugin = host.getFormats().createPluginInstance (*vst3, 48000.0, 512, error);
                auto* parameter = plugin != nullptr && ! plugin->getParameters().isEmpty() ? plugin->getParameters()[0] : nullptr;

                if (parameter != nullptr)
                {
                    const auto target = parameter->getValue() > 0.5f ? 0.13f : 0.87f;
                    parameter->setValueNotifyingHost (target);
                    juce::MemoryBlock juceState;
                    plugin->getStateInformation (juceState);
                    report << "parameter=" << parameter->getName (64) << " target=" << target << "\n";

                    // JUCE state -> FL container: u32 1 + records 1 (flags), 3 (component), 2 (controller)
                    std::vector<std::uint8_t> flState;
                    auto put32 = [&] (std::uint32_t v) { for (int b = 0; b < 4; ++b) flState.push_back ((std::uint8_t) (v >> (8 * b))); };
                    auto record = [&] (std::uint32_t id, const juce::MemoryBlock& data) {
                        put32 (id);
                        put32 ((std::uint32_t) data.getSize());
                        put32 (0);
                        flState.insert (flState.end(), (const std::uint8_t*) data.getData(), (const std::uint8_t*) data.getData() + data.getSize());
                    };
                    put32 (1);
                    record (1, juce::MemoryBlock (64, true));

                    // record 4 (REA_rodada22 §2.3): slots 0/1 hold the ParamIDs of parameters 2 and 0, so FL's
                    // automations k = 0, 1, 2 mean positions 2, 0 and 1 (the first parameter without a slot)
                    juce::MemoryBlock slots;
                    const auto paramId = [&] (int i) {
                        const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (plugin->getParameters()[i]);
                        return hosted != nullptr ? (std::uint32_t) hosted->getParameterID().getLargeIntValue() : 0u;
                    };
                    const bool reorder = plugin->getParameters().size() >= 3;
                    if (reorder)
                    {
                        for (auto v : { 2u, paramId (2), paramId (0) })
                            slots.append (&v, 4);
                        report << "slot_param_ids=" << (juce::int64) paramId (2) << "," << (juce::int64) paramId (0) << "\n";
                    }

                    if (auto xml = juce::AudioProcessor::getXmlFromBinary (juceState.getData(), (int) juceState.getSize()))
                        for (auto [tag, id] : { std::pair { "IComponent", 3u }, { "IEditController", 2u } })
                            if (auto* child = xml->getChildByName (tag))
                            {
                                juce::MemoryBlock data;
                                data.fromBase64Encoding (child->getAllSubText());
                                record (id, data);
                                report << tag << "_bytes=" << (int) data.getSize() << "\n";
                            }
                    if (reorder)
                        record (4, slots);

                    plugin.reset();

                    auto song = lynx::Song::makeDefault();
                    auto& f = song.channels[4].plugin.foreign;
                    f.format = lynx::ForeignPlugin::Format::vst3;
                    f.name = vst3->name.toStdString(); // no class ID: find it by name (fallback)
                    f.state = flState;
                    {
                        std::vector<std::uint8_t> comp, ctrl;
                        report << "split=" << (int) lynx::splitFlVst3State (flState, comp, ctrl) << " component=" << (int) comp.size()
                               << " saved_slots=" << (int) lynx::flVst3ParamSlots (flState).size() << "\n";
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        lynx::Automation a;
                        a.target = { lynx::AutomationTargetKind::channelParam, 4, 0, k };
                        song.automations.push_back (a);
                    }
                    const auto adopted = host.adoptFlPlugins (song);
                    report << "adopted=" << adopted << "\n";
                    juce::String mapped;
                    for (const auto& a : song.automations)
                        if (a.target.kind == lynx::AutomationTargetKind::channelParam && a.target.index == 4)
                            mapped << (mapped.isEmpty() ? "" : ",") << a.target.param;
                    report << "slot_positions=" << mapped << "\n";
                    const auto slotsOk = ! reorder || mapped == "2,0,1";

                    offline.setSong (song);

                    if (auto* loaded = dynamic_cast<lynx::PluginInstrument*> (offline.getChannelInstrument (4).get()))
                    {
                        const auto value = loaded->getInstance().getParameters()[0]->getValue();
                        report << "restored_value=" << value << "\n";
                        ok = adopted == 1 && std::abs (value - target) < 0.02f && slotsOk;
                    }
                }
            }
        }

        report << "result=" << (ok ? "OK" : "FAILED") << "\n";
        juce::File (reportPath).replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    void runShowcase (const juce::String& outputPath, int currentPattern)
    {
        using namespace lynx;
        Song song;
        song.bpm = 124.0;
        song.mixer = Song::defaultMixer();
        const auto presets = synthPresets();
        auto preset = [&] (const char* name) {
            for (const auto& [n, sset] : presets)
                if (n == name)
                    return sset;
            return SynthSettings {};
        };
        auto channel = [&] (const char* name, InstrumentKind kind, float volume, int track, std::uint32_t colour) {
            ChannelDef c;
            c.name = name;
            c.instrument = kind;
            c.volume = volume;
            c.mixerTrack = track;
            c.colour = colour;
            song.channels.push_back (c);
            return (int) song.channels.size() - 1;
        };
        const auto kick = channel ("Kick", InstrumentKind::kick, 0.9f, 1, 0xffd9534f);
        const auto clap = channel ("Clap", InstrumentKind::clap, 0.75f, 2, 0xfff0ad4e);
        const auto hat = channel ("Hat", InstrumentKind::hat, 0.6f, 3, 0xfff7d774);
        const auto snare = channel ("Snare", InstrumentKind::snare, 0.7f, 4, 0xffe8835a);
        const auto bass = channel ("Reese Bass", InstrumentKind::synth, 0.8f, 5, 0xff5bc0de);
        song.channels[(std::size_t) bass].synth = preset ("Reese Bass"); // preset name as defined in core/SynthPresets
        const auto pad = channel ("Pad", InstrumentKind::synth, 0.7f, 6, 0xff9b7fd6);
        song.channels[(std::size_t) pad].synth = preset ("Pad");
        const auto pluck = channel ("Pluck", InstrumentKind::synth, 0.72f, 7, 0xff5cb85c);
        song.channels[(std::size_t) pluck].synth = preset ("Pluck");
        const auto lead = channel ("Supersaw", InstrumentKind::synth, 0.6f, 8, 0xff4fc3a1);
        song.channels[(std::size_t) lead].synth = preset ("Supersaw");

        auto addPattern = [&] (const char* name, std::uint32_t colour) {
            auto& p = song.patterns.emplace_back();
            p.name = name;
            p.colour = colour;
            return (int) song.patterns.size() - 1;
        };
        auto note = [&] (int pattern, int ch, double beat, double beats, int key, float velocity = 0.8f) {
            Note n;
            n.channel = ch;
            n.start = (std::int64_t) (beat * ppq);
            n.length = (std::int64_t) (beats * ppq);
            n.key = key;
            n.velocity = velocity;
            song.patterns[(std::size_t) pattern].notes.push_back (n);
        };

        const auto beat = addPattern ("Beat", 0xffd9534f);
        for (int s = 0; s < 16; s += 4)
            song.patterns[(std::size_t) beat].toggleStep (kick, s);
        for (int s : { 4, 12 })
            song.patterns[(std::size_t) beat].toggleStep (clap, s);
        for (int s = 2; s < 16; s += 4)
            song.patterns[(std::size_t) beat].toggleStep (hat, s);
        for (int s : { 7, 15 })
            song.patterns[(std::size_t) beat].toggleStep (snare, s);

        const auto groove = addPattern ("Groove", 0xfff0ad4e);
        for (int s = 0; s < 16; ++s)
            song.patterns[(std::size_t) groove].toggleStep (hat, s);
        for (int s : { 4, 12 })
            song.patterns[(std::size_t) groove].toggleStep (clap, s);

        // Am – F – C – G, one bar each
        const int roots[4] = { 45, 41, 48, 43 };
        const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
        const auto bassline = addPattern ("Bass", 0xff5bc0de);
        for (int bar = 0; bar < 4; ++bar)
            for (int e = 0; e < 8; ++e)
                if (e != 3)
                    note (bassline, bass, bar * 4 + e * 0.5, 0.4, roots[bar] - 12 + (e == 6 ? 12 : 0), e % 2 == 0 ? 0.9f : 0.65f);
        const auto chordsPattern = addPattern ("Chords", 0xff9b7fd6);
        for (int bar = 0; bar < 4; ++bar)
            for (int k = 0; k < 3; ++k)
                note (chordsPattern, pad, bar * 4, 3.9, chords[bar][k], 0.7f);
        const auto melody = addPattern ("Melody", 0xff5cb85c);
        const int line[] = { 69, 72, 76, 74, 72, 69, 67, 69, 65, 69, 72, 74, 72, 69, 67, 64,
                             72, 76, 79, 76, 74, 72, 74, 76, 71, 74, 79, 78, 76, 74, 71, 67 };
        for (int k = 0; k < 32; ++k)
            if (k % 8 != 7)
                note (melody, pluck, k * 0.5, k % 4 == 3 ? 0.9 : 0.4, line[k], k % 2 == 0 ? 0.85f : 0.65f);
        const auto hook = addPattern ("Hook", 0xff4fc3a1);
        for (int bar = 0; bar < 4; ++bar)
            for (int k = 0; k < 3; ++k)
                note (hook, lead, bar * 4 + k * 1.5, k == 2 ? 1.0 : 1.4, chords[bar][2] + 12 - k * 3, 0.8f);

        // Playlist: intro, verse, drop, breakdown, final drop
        song.playlistTracks = { { "Drums" }, { "Groove" }, { "Bass" }, { "Chords" }, { "Melody" }, { "Hook" }, { "Automation" } };
        const auto bar = (std::int64_t) ppq * 4;
        auto clip = [&] (int pattern, int track, int fromBar, int bars) {
            const auto length = song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar);
            for (int b = fromBar; b < fromBar + bars;)
            {
                Clip c;
                c.pattern = pattern;
                c.track = track;
                c.start = b * bar;
                c.length = length;
                song.playlist.push_back (c);
                b += (int) std::max<std::int64_t> (1, length / bar);
            }
        };
        clip (chordsPattern, 3, 0, 8);
        clip (melody, 4, 4, 4);
        clip (beat, 0, 8, 16);
        clip (groove, 1, 12, 12);
        clip (bassline, 2, 8, 8);
        clip (melody, 4, 8, 8);
        clip (chordsPattern, 3, 16, 4);
        clip (bassline, 2, 20, 4);
        clip (hook, 5, 20, 4);
        clip (melody, 4, 20, 4);
        song.addMarker ({ 0, "Intro" });
        song.addMarker ({ 8 * bar, "Drop" });
        song.addMarker ({ 16 * bar, "Breakdown" });
        song.addMarker ({ 20 * bar, "Final" });

        // automation: the pad volume opens up during the intro
        Automation a;
        a.name = "Pad Volume";
        a.target = { AutomationTargetKind::mixerVolume, 6, 0, 0 };
        a.points = { { 0, 0.15f, 0.4f }, { 8 * bar, 0.8f, 0.0f } };
        song.automations.push_back (a);
        Clip ac;
        ac.kind = ClipKind::automation;
        ac.automation = 0;
        ac.track = 6;
        ac.start = 0;
        ac.length = 8 * bar;
        song.playlist.push_back (ac);

        // mixer: names, colours and effects (bass compressor sidechained to the kick)
        const char* names[] = { "Master", "Kick", "Clap", "Hat", "Snare", "Bass", "Pad", "Pluck", "Lead" };
        for (int t = 0; t < 9; ++t)
        {
            song.mixer[(std::size_t) t].name = names[t];
            if (t > 0)
                song.mixer[(std::size_t) t].colour = song.channels[(std::size_t) t - 1].colour;
        }
        auto fx = [&] (int track, EffectKind kind) -> EffectSlot& {
            song.mixer[(std::size_t) track].effects.push_back (makeEffectSlot (kind));
            return song.mixer[(std::size_t) track].effects.back();
        };
        {
            // kick EQ: cut below 30 Hz, weight at 60 Hz, remove the "boxiness" at 350 Hz, brightness at 4 kHz
            auto& eq = fx (1, EffectKind::parametricEq);
            auto band = [&] (int b, peq::BandType type, double hz, double db, double q) {
                eq.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (type);
                eq.params[(std::size_t) peq::param (b, 1)] = peq::hzValue (hz);
                eq.params[(std::size_t) peq::param (b, 2)] = peq::gainValue (db);
                eq.params[(std::size_t) peq::param (b, 3)] = peq::qValue (q);
            };
            band (0, peq::BandType::lowCut, 30.0, 0.0, 0.7);
            band (1, peq::BandType::peak, 62.0, 5.0, 1.2);
            band (2, peq::BandType::peak, 350.0, -6.0, 2.0);
            band (3, peq::BandType::peak, 4200.0, 3.5, 1.5);
            band (4, peq::BandType::highCut, 14000.0, 0.0, 0.7);
            for (int b = 5; b < peq::bands; ++b)
                eq.params[(std::size_t) peq::param (b, 0)] = peq::typeValue (peq::BandType::off);
        }
        fx (1, EffectKind::distortion).mix = 0.3f;
        auto& comp = fx (5, EffectKind::compressor);
        comp.params[0] = 0.55f;
        comp.params[1] = 0.8f;
        comp.sidechain = 1;
        fx (5, EffectKind::parametricEq);
        fx (6, EffectKind::chorus);
        fx (6, EffectKind::reverb);
        fx (7, EffectKind::delay);
        fx (7, EffectKind::reverb).mix = 0.4f;
        fx (8, EffectKind::effector);
        fx (8, EffectKind::delay);
        fx (0, EffectKind::parametricEq);
        auto& limiter = fx (0, EffectKind::limiter);
        limiter.params = { 11.7f / 12.0f, 0.1f, 0.5f };
        song.mixer[1].volume = 0.78f;
        song.mixer[5].volume = 0.72f;
        song.mixer[6].volume = 0.6f;
        song.mixer[6].pan = -0.2f;
        song.mixer[7].pan = 0.25f;

        song.mode = PlayMode::song;
        song.currentPattern = std::clamp (currentPattern, 0, (int) song.patterns.size() - 1);
        juce::String error;
        const auto ok = ProjectFile::save (song, juce::File (outputPath), error);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --sandboxtest plugin.vst3 report.txt: opens the instrument in a separate process, plays a
    // note, changes a parameter, checks the state (back in an in-process plugin) and makes the child hang
    // to check that LYNX keeps going (silence, without waiting longer than the timeout).
    void runSandboxTest (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        lynx::setPluginSandboxAlerts (false);
        const juce::File log (reportPath + ".log");
        auto step = [&] (const char* what) { log.appendText (juce::String (what) + "\n"); };
        log.deleteFile();
        {
            lynx::PluginHost host (false);
            host.addPluginFile (juce::File (pluginPath));
            const auto types = host.getKnownPlugins().getTypes(); // getTypes() returns a copy
            const juce::PluginDescription* instrument = nullptr;
            for (const auto& type : types)
                if (type.isInstrument)
                    instrument = &type;

            step ("host");
            juce::String error;
            auto remote = instrument != nullptr ? lynx::createSandboxedPlugin (*instrument, 48000.0, 512, {}, error) : nullptr;
            report << "opened=" << (remote != nullptr ? 1 : 0) << " error=" << error << "\n";

            step ("created");
            if (remote != nullptr)
            {
                auto& proxy = *remote;
                lynx::PluginInstrument voice (std::move (remote), 512);
                std::vector<float> l (48000), r (48000);
                auto peakOf = [&] {
                    float peak = 0.0f;
                    for (std::size_t k = 0; k < l.size(); ++k)
                        peak = std::max ({ peak, std::abs (l[k]), std::abs (r[k]) });
                    return peak;
                };

                const auto start = juce::Time::getMillisecondCounterHiRes();
                voice.noteOn (60, 0.9f);
                voice.render (l.data(), r.data(), 48000, 1.0f, 1.0f);
                const auto renderMs = juce::Time::getMillisecondCounterHiRes() - start;
                const auto peak = peakOf();
                step ("played");
                report << "parameters=" << proxy.getParameters().size() << " peak=" << peak << " render_1s_ms=" << renderMs << "\n";

                // parameter -> state -> in-process plugin
                bool stateOk = false;
                if (! proxy.getParameters().isEmpty())
                {
                    auto* p = proxy.getParameters()[0];
                    const auto target = p->getValue() > 0.5f ? 0.13f : 0.87f;
                    p->setValue (target);
                    std::fill (l.begin(), l.end(), 0.0f);
                    voice.render (l.data(), r.data(), 512, 1.0f, 1.0f); // carries the change to the child
                    juce::MemoryBlock state;
                    proxy.getStateInformation (state);
                    if (auto local = host.getFormats().createPluginInstance (*instrument, 48000.0, 512, error))
                    {
                        local->setStateInformation (state.getData(), (int) state.getSize());
                        const auto back = local->getParameters()[0]->getValue();
                        report << "state_bytes=" << (int) state.getSize() << " target=" << target << " in_process=" << back << "\n";
                        stateOk = std::abs (back - target) < 0.02f;
                    }
                }

                step ("state");
                // hung child: the block returns in ~0.5 s, muted, and the plugin is marked dead
                lynx::hangSandboxedPluginForTest (proxy);
                juce::Thread::sleep (200);
                std::fill (l.begin(), l.end(), 1.0f);
                std::fill (r.begin(), r.end(), 1.0f);
                const auto hangStart = juce::Time::getMillisecondCounterHiRes();
                voice.render (l.data(), r.data(), 4096, 0.0f, 0.0f);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                voice.render (l.data(), r.data(), 4096, 1.0f, 1.0f);
                const auto hangMs = juce::Time::getMillisecondCounterHiRes() - hangStart;
                const auto dead = lynx::isSandboxedPluginDead (proxy);
                report << "hung_ms=" << hangMs << " dead=" << (dead ? 1 : 0) << " peak_after=" << peakOf() << "\n";

                ok = peak > 1e-4f && stateOk && dead && hangMs < 1500.0 && peakOf() == 0.0f;
            }
        }

        report << "result=" << (ok ? "OK" : "FAILED") << "\n";
        juce::File (reportPath).replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --render project.(flp|lynx) output.wav [seconds]
    // Exports the current pattern to 24-bit WAV (for an imported .flp, the one with the most notes).
    void runRender (const juce::String& inputPath, const juce::String& outputPath, double seconds)
    {
        lynx::AudioEngine offline (false);
        const juce::File input (inputPath);
        juce::String error, report;
        bool ok = false;

        if (input.hasFileExtension ("flp"))
            ok = offline.importFlpFile (input, error);
        else if (auto song = lynx::ProjectFile::load (input, error))
        {
            offline.setSong (std::move (*song));
            ok = true;
        }

        int samplers = 0, samplersLoaded = 0;

        if (ok)
        {
            for (const auto& c : offline.getSong().channels)
                if (c.instrument == lynx::InstrumentKind::sampler || c.instrument == lynx::InstrumentKind::audioClip)
                {
                    ++samplers;
                    samplersLoaded += offline.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str())) != nullptr;
                }

            const juce::File output (outputPath);
            output.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (output.createOutputStream().release(), 44100.0, 2, 24, {}, 0));

            if (writer == nullptr)
            {
                ok = false;
                error = lynx::utf8 ("could not create ") + outputPath;
            }
            else
            {
                const auto peak = offline.renderOffline (seconds > 0.0 ? seconds : 10.0, 44100.0, writer.get());
                report << "peak=" << peak << "\n";
                ok = peak > 0.001f;
            }
        }

        report << "samples=" << samplersLoaded << "/" << samplers << "\n"
               << "result=" << (ok ? "OK" : "ERROR " + error) << "\n";
        juce::File (outputPath + ".txt").replaceWithText (report);

        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --export project.(flp|lynx) output.(wav|flac|ogg) [--pattern] [--insert N]
    // Same path as "Export" in the UI: the whole song (or the current pattern) with its tail.
    void runExport (const juce::String& inputPath, const juce::String& outputPath, bool patternOnly, int insert)
    {
        juce::String error, report;
        bool ok = false;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        {
            lynx::AudioEngine loader (false);
            const juce::File input (inputPath);

            if (input.hasFileExtension ("flp"))
                ok = loader.importFlpFile (input, error);
            else if (auto song = lynx::ProjectFile::load (input, error))
            {
                loader.setSong (std::move (*song));
                ok = true;
            }

            if (ok)
            {
                const juce::File output (outputPath);
                lynx::ExportSettings settings;
                settings.wholeSong = ! patternOnly;
                settings.format = output.hasFileExtension ("flac") ? lynx::ExportSettings::Format::flac24
                                  : output.hasFileExtension ("ogg") ? lynx::ExportSettings::Format::ogg
                                                                    : lynx::ExportSettings::Format::wav24;
                // --insert N: only that insert's output (what "Render to audio" records)
                if (juce::JUCEApplication::getCommandLineParameters().contains ("--no-effects")
                    || juce::JUCEApplication::getCommandLineParameters().contains ("--sem-efeitos")) // performance diagnostics; --sem-efeitos: legacy alias
                    loader.editSong ([] (lynx::Song& s) {
                        for (auto& t : s.mixer)
                            t.effects.clear();
                    }, false);

                if (juce::JUCEApplication::getCommandLineParameters().contains ("--no-automation")
                    || juce::JUCEApplication::getCommandLineParameters().contains ("--sem-automacao")) // --sem-automacao: legacy alias
                    loader.editSong ([] (lynx::Song& s) { s.automations.clear(); }, false);

                const auto loaded = juce::Time::getMillisecondCounterHiRes();
                lynx::ExportJob job (insert > 0 ? lynx::makeBounceSong (loader.getSong(), insert) : loader.getSong(), settings, output);
                const auto prepared = juce::Time::getMillisecondCounterHiRes();
                int lastPercent = -1;
                ok = job.run ([&lastPercent] (double p) {
                    lastPercent = (int) (p * 100.0);
                    return true;
                }, error);
                auto joined = [] (const std::map<std::string, int>& items) {
                    juce::StringArray names;
                    for (const auto& [name, count] : items)
                        names.add (juce::String::fromUTF8 (name.c_str()) + "x" + juce::String (count));
                    return names.joinIntoString (", ");
                };
                const auto& importReport = loader.getImportReport();
                report << "converted=" << joined (importReport.converted) << "\n"
                       << "approximated=" << joined (importReport.approximated) << "\n"
                       << "unsupported=" << joined (importReport.unsupported) << "\n"
                       << "dropped_effects=" << joined (importReport.droppedEffects) << "\n"
                       << "missing_samples=" << importReport.missingSamples << "\n";
                report << "open_ms=" << juce::roundToInt (loaded - started) << "\n"
                       << "prepare_ms=" << juce::roundToInt (prepared - loaded) << "\n"
                       << "render_ms=" << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - prepared) << "\n"
                       << "graph_ms=" << juce::roundToInt (job.getEngine().getLastRenderStats().graphMs) << "\n"
                       << "write_ms=" << juce::roundToInt (job.getEngine().getLastRenderStats().writeMs) << "\n"
                       << "song_seconds=" << job.getMusicSeconds() << "\n"
                       << "peak=" << job.getPeak() << "\n"
                       << "final_progress=" << lastPercent << "\n"
                       << "bytes=" << output.getSize() << "\n";
                ok = ok && job.getPeak() > 0.001f;
            }
        }

        report << "result=" << (ok ? "OK" : "ERROR " + error) << "\n";
        juce::File (outputPath + ".txt").replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --snapshot prefix [project]: draws the window off-screen and writes
    // prefix-rack.png, -pianoroll.png and -playlist.png (to check the UI without touching the screen).
    void runSnapshot (const juce::String& prefix, const juce::String& projectPath)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        {
            lynx::AudioEngine offline (false);
            lynx::MainComponent ui (offline, false); // default layout and keys, nothing saved
            // LYNX_SNAPSHOT_SIZE=WxH (default 1280x720)
            const auto size = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_SIZE", "1280x720"), "x", "");
            ui.setSize (juce::jlimit (800, 3840, size[0].getIntValue()), juce::jlimit (450, 2160, size[1].getIntValue()));

            if (projectPath.isNotEmpty())
                ui.openFile (juce::File (projectPath));

            offline.dispatchPendingMessages(); // change notifications are asynchronous

            ui.waitForBrowserForSnapshot();

            // the workspace desktop: the default layout, then with the Piano Roll and Mixer floating too
            // (and a sample hint in the status strip, as when hovering a knob)
            ui.showDesktopForSnapshot (false);
            ui.setMetersForSnapshot (0.23, (std::int64_t) 412 << 20, 6);
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-desktop.png");

            // the start-up desktop and the toolbar at the usual window sizes: the untouched default layout
            // follows the window (default rects, not scaled ones); panels drop from the toolbar by priority
            {
                const auto size = ui.getBounds();
                for (const auto& [w, h] : std::vector<std::pair<int, int>> { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 } })
                {
                    ui.setSize (w, h);
                    offline.dispatchPendingMessages();
                    saveSnapshot (ui, prefix + "-desktop-" + juce::String (w) + ".png");
                    saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-toolbar-" + juce::String (w) + ".png");
                }
                // the toolbar with small and large icons, and a customized one (groups hidden and reordered)
                ui.setSize (1600, 900);
                for (auto [iconSize, name] : { std::pair { lynx::toolbarlayout::IconSize::small, "small" }, { lynx::toolbarlayout::IconSize::large, "large" } })
                {
                    auto layout = lynx::toolbarlayout::Layout::makeDefault();
                    layout.iconSize = iconSize;
                    ui.setToolbarLayoutForSnapshot (layout);
                    saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-toolbar-" + name + "-1600.png");
                }
                {
                    auto layout = lynx::toolbarlayout::Layout::makeDefault();
                    layout.move (lynx::toolbarlayout::Group::windows, -3);
                    layout.setShown (lynx::toolbarlayout::Group::record, false);
                    layout.setShown (lynx::toolbarlayout::Group::meters, false);
                    ui.setToolbarLayoutForSnapshot (layout);
                    saveArea (ui, ui.getToolbarBoundsForSnapshot(), prefix + "-toolbar-custom-1600.png");
                }
                ui.setToolbarLayoutForSnapshot (lynx::toolbarlayout::Layout::makeDefault());
                // quick tips: the toolbar and title bars with a few name bubbles, and every tip on one sheet
                offline.dispatchPendingMessages();
                saveQuickTips (ui, prefix);
                ui.setBounds (size);
                offline.dispatchPendingMessages();
            }

            // the hint bar: a value control while it is dragged (live value), a gesture line with mouse and key
            // glyphs, and a text too long for one line (two lines)
            {
                const auto toolbar = ui.getToolbarBoundsForSnapshot().withWidth (juce::jmin (ui.getWidth(), 900));
                const lynx::ui::Hint hints[] = {
                    { lynx::tr ("Volume") + " - " + lynx::tr ("drag (Ctrl: fine), wheel, Alt+click resets"), "-3.2 dB", true, true },
                    { lynx::tr ("Piano Roll: [LMB] add a note, drag to move  [RMB] delete  [Ctrl][LMB] drag to select  [Ctrl][Wheel] zoom"), {}, false, false },
                    { lynx::tr ("Global snap: the grid of the Piano Roll and Playlist (when they follow it) and the quantise of recorded notes  "
                                "[LMB] choose  [Wheel] step  [RMB] length of new notes  [Backspace] snap on/off  [Alt] while dragging: no snap"), {}, false, true },
                };
                for (int i = 0; i < 3; ++i)
                {
                    ui.setHintForSnapshot (hints[i], i == 0);
                    saveArea (ui, toolbar, prefix + "-hint-" + juce::String (i + 1) + ".png");
                }
            }

            ui.showDesktopForSnapshot (true);
            ui.setHintForSnapshot ({ lynx::tr ("Volume") + " - " + lynx::tr ("drag (Ctrl: fine), wheel, Alt+click resets"), "78%", true, true });
            saveSnapshot (ui, prefix + "-desktop-all.png");

            ui.selectTabForSnapshot (0);
            saveSnapshot (ui, prefix + "-rack.png");
            ui.selectTabForSnapshot (1);
            saveSnapshot (ui, prefix + "-pianoroll.png");
            if (const auto strip = ui.getToolStripAreaForSnapshot (false); ! strip.isEmpty())
                saveArea (ui, strip, prefix + "-pianoroll-tools.png");
            // the editors' mouse feel: the hovered note / clip edge (grip, outline) and its hint in the hint bar
            ui.setHintForSnapshot (ui.getPianoRollForSnapshot().hoverForSnapshot());
            saveSnapshot (ui, prefix + "-pianoroll-hover.png");
            ui.selectTabForSnapshot (2);
            saveSnapshot (ui, prefix + "-playlist.png");
            if (const auto strip = ui.getToolStripAreaForSnapshot (true); ! strip.isEmpty())
                saveArea (ui, strip, prefix + "-playlist-tools.png");
            ui.setHintForSnapshot (ui.getPlaylistForSnapshot().hoverForSnapshot());
            saveSnapshot (ui, prefix + "-playlist-hover.png");
            ui.setHintForSnapshot ({ lynx::tr ("Volume") + " - " + lynx::tr ("drag (Ctrl: fine), wheel, Alt+click resets"), "78%", true, true });
            // the Playlist with a time selection over bars 2-3 (it selects the clips it touches) and the Knife tool
            ui.getPlaylistForSnapshot().setTimeSelectionForSnapshot (1, 3);
            ui.getPlaylistForSnapshot().setToolForSnapshot (lynx::EditTool::knife);
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-playlist-timesel.png");
            ui.getPlaylistForSnapshot().setToolForSnapshot (lynx::EditTool::pencil);
            ui.selectTabForSnapshot (3);
            // LYNX_SNAPSHOT_TRACK=n: shows the effects of that mixer track
            if (const auto track = juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_TRACK", {}); track.isNotEmpty())
            {
                ui.selectMixerTrackForSnapshot (track.getIntValue(),
                                                juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_SLOT", "-1").getIntValue());
                offline.dispatchPendingMessages();
            }
            saveSnapshot (ui, prefix + "-mixer.png");

            // standalone windows, with the theme background
            auto saveWindow = [&prefix] (std::unique_ptr<juce::Component> c, const juce::String& name) {
                juce::Component frame;
                frame.setSize (c->getWidth(), c->getHeight());
                frame.setOpaque (true);
                frame.addAndMakeVisible (*c);
                c->setTopLeftPosition (0, 0);
                juce::Image image (juce::Image::ARGB, frame.getWidth(), frame.getHeight(), true);
                {
                    juce::Graphics g (image);
                    g.fillAll (lynx::theme::panel);
                    frame.paintEntireComponent (g, false);
                }
                const juce::File file (prefix + name);
                file.deleteFile();
                juce::PNGImageFormat png;
                if (auto out = file.createOutputStream())
                    png.writeImageToStream (image, *out);
            };

            saveWindow (lynx::makeExportPanel (offline), "-export.png");
            saveSplashFrames (prefix);
            saveIconSheet (prefix + "-icons.png", 1.0f);
            saveIconSheet (prefix + "-icons-150.png", 1.5f);
            saveWindow (lynx::MainComponent::makeHelpContent(), "-help.png");
            saveWindow (std::make_unique<lynx::toolbar::CustomizePanel> (lynx::toolbarlayout::Layout::makeDefault()), "-customize-toolbar.png");
            saveMenus (ui, prefix + "-menus.png");
            {
                // the windows' own menus (title bars) and the toolbar's recording menu
                juce::StringArray names;
                std::vector<juce::PopupMenu> menus;
                for (auto [panel, name] : { std::pair { lynx::workspace::Panel::playlist, "Playlist" }, { lynx::workspace::Panel::channelRack, "Channel Rack" },
                                            { lynx::workspace::Panel::pianoRoll, "Piano Roll" }, { lynx::workspace::Panel::mixer, "Mixer" },
                                            { lynx::workspace::Panel::browser, "Browser" } })
                {
                    names.add (name);
                    menus.push_back (ui.getPanelMenuForSnapshot (panel));
                }
                names.add ("Recording");
                menus.push_back (ui.getRecordMenuForSnapshot());
                names.add ("Toolbar");
                menus.push_back (ui.getToolbarMenuForSnapshot());
                saveMenus (names, menus, prefix + "-panel-menus.png");
            }

            auto saveAlert = [&] (std::unique_ptr<juce::AlertWindow> alert, const juce::String& name) {
                alert->setOpaque (true);
                saveWindow (std::move (alert), name);
            };
            saveAlert (ui.makeConfirmDiscardWindow(), "-confirm.png");
            saveAlert (lynx::MainComponent::makeAboutWindow(), "-about.png");
            saveAlert (lynx::makeNameAndColourDialog (lynx::tr ("Rename pattern"), "Pattern 2", lynx::lynxPalette()[6]), "-rename.png");
            saveWindow (lynx::makeSetupWizardContent (offline, {}, {}), "-setup.png");
            saveWindow (lynx::makeMissingFilesPanel (lynx::missingFilesSnapshotInfo(), 0, 1), "-missing-files.png");
            saveWindow (lynx::makeMissingFilesPanel (lynx::missingFilesSnapshotInfo(), 1), "-missing-plugins.png");
            saveWindow (lynx::makeMissingFilesPanel (lynx::missingFilesSnapshotInfo(), 2), "-missing-found.png");
            {
                // the Settings pages, with credits like a project imported from FL would carry
                offline.editSong ([] (lynx::Song& s) {
                    s.info = { "Night Drive", "LYNX demo", "Synthwave", "https://lynx-share.com",
                               "Made with LYNX.\nDrums and bass from the Channel Rack, pads from the Browser.", false };
                }, false);
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::project), "-settings-project.png");
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::info), "-settings-info.png");
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::files), "-settings-files.png");
            }

            if (! offline.getSong().automations.empty())
            {
                auto editor = lynx::makeAutomationEditor (offline, 0);
                editor->setSize (760, 300);
                saveWindow (std::move (editor), "-automation.png");
            }

            saveWindow (lynx::makeChannelSettingsPanel (offline, 0), "-sampler.png");

            for (int c = 0; c < (int) offline.getSong().channels.size(); ++c)
                if (offline.getSong().channels[(std::size_t) c].instrument == lynx::InstrumentKind::synth)
                {
                    saveWindow (lynx::makeChannelSettingsPanel (offline, c), "-synth.png");
                    saveWindow (lynx::makeChannelModulationPanel (offline, c), "-modulation.png");
                    break;
                }

            for (int c = 0; c < (int) offline.getSong().channels.size(); ++c)
                if (offline.getSong().channels[(std::size_t) c].instrument == lynx::InstrumentKind::controller)
                {
                    saveWindow (lynx::makeChannelSettingsPanel (offline, c), "-controller.png");
                    break;
                }

            for (const auto kind : { lynx::InstrumentKind::sakura, lynx::InstrumentKind::drumpad })
                for (int c = 0; c < (int) offline.getSong().channels.size(); ++c)
                    if (offline.getSong().channels[(std::size_t) c].instrument == kind)
                    {
                        saveWindow (lynx::makeChannelSettingsPanel (offline, c), kind == lynx::InstrumentKind::sakura ? "-sakura.png" : "-drumpad.png");
                        break;
                    }

            // parametric EQ: the one with the most pronounced curve (in an imported .flp, with FL's settings)
            {
                int bestTrack = -1, bestSlot = -1;
                double bestShape = -1.0;
                const auto& mixer = offline.getSong().mixer;

                for (int t = 0; t < (int) mixer.size(); ++t)
                    for (int s = 0; s < (int) mixer[(std::size_t) t].effects.size(); ++s)
                        if (const auto& fx = mixer[(std::size_t) t].effects[(std::size_t) s]; fx.kind == lynx::EffectKind::parametricEq)
                        {
                            double shape = 0.0;
                            for (double hz = 30.0; hz < 18000.0; hz *= 1.5)
                                shape += std::abs (lynx::peq::responseDb (fx.params, 48000.0, hz));
                            if (shape > bestShape)
                            {
                                bestShape = shape;
                                bestTrack = t;
                                bestSlot = s;
                            }
                        }

                if (bestTrack >= 0)
                    saveWindow (lynx::makeParametricEqEditor (offline, bestTrack, bestSlot), "-peq.png");
            }

            // Note inspector for the Piano Roll's notes (its selection, or all of the channel's notes)
            if (auto inspector = ui.getPianoRollForSnapshot().makeNoteInspectorForSnapshot())
                saveWindow (std::move (inspector), "-noteinspector.png");
            if (auto inspector = ui.getPianoRollForSnapshot().makeNoteInspectorForSnapshot (true))
                saveWindow (std::move (inspector), "-noteinspector-one.png");

            // workflow P2: note tool dialogs (they preview on the pattern; each one puts the notes back when destroyed)
            {
                auto& roll = ui.getPianoRollForSnapshot();
                const std::pair<lynx::NoteTool, const char*> tools[] = { { lynx::NoteTool::strum, "-tool-strum.png" },
                                                                         { lynx::NoteTool::arpeggiate, "-tool-arpeggiate.png" },
                                                                         { lynx::NoteTool::flip, "-tool-flip.png" },
                                                                         { lynx::NoteTool::randomise, "-tool-randomise.png" },
                                                                         { lynx::NoteTool::scaleLevels, "-tool-scalelevels.png" },
                                                                         { lynx::NoteTool::limit, "-tool-limit.png" } };
                for (const auto& [tool, name] : tools)
                    saveWindow (lynx::makeNoteToolPanel (offline, tool, roll.noteIndicesForSnapshot(), roll.gridForSnapshot()), name);
            }

            // the Channel Rack with the step graph lane (velocity), and the Euclidean fill window
            ui.selectTabForSnapshot (0);
            ui.getRackForSnapshot().setGraphLevelForSnapshot (0);
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-rack-graph.png");
            ui.getRackForSnapshot().setGraphLevelForSnapshot (1);
            saveSnapshot (ui, prefix + "-rack-graph-pan.png");
            ui.getRackForSnapshot().toggleGraphLanes();
            saveWindow (ui.getRackForSnapshot().makeEuclideanPanel (0), "-euclidean.png");

            // the Mixer with three inserts selected and one armed for recording
            ui.selectTabForSnapshot (3);
            if (offline.getSong().mixer.size() > 4)
            {
                ui.getMixerForSnapshot().getStripsForSnapshot().setSelection ({ 1, 2, 3 }, 2);
                offline.setInsertArmed (3, true);
                offline.dispatchPendingMessages();
                saveSnapshot (ui, prefix + "-mixer-multi.png");
                offline.setInsertArmed (3, false);
                ui.getMixerForSnapshot().getStripsForSnapshot().setSelection ({}, 0);
            }

            // the Mixer's routing lane: an insert with sends selected (send knobs and wires)
            if (offline.getSong().mixer.size() > 6)
            {
                int source = -1;
                for (int t = 1; t < (int) offline.getSong().mixer.size() && source < 0; ++t)
                    if (! offline.getSong().mixer[(std::size_t) t].sends.empty())
                        source = t;
                const auto temporary = source < 0;
                if (temporary) // the demo has no sends: add two for the picture
                {
                    source = 1;
                    offline.editSong ([] (lynx::Song& s) { s.setRoute (1, 5, true, 0.6f); s.setRoute (1, 6, true, 0.3f); s.setRoute (4, 1, true, 0.8f); });
                }
                ui.getMixerForSnapshot().selectTrack (source);
                offline.dispatchPendingMessages();
                saveSnapshot (ui, prefix + "-mixer-sends.png");
                if (temporary)
                    offline.editSong ([] (lynx::Song& s) { s.setRoute (1, 5, false); s.setRoute (1, 6, false); s.setRoute (4, 1, false); });
                ui.getMixerForSnapshot().selectTrack (0);
            }

            // the Playlist with ripple editing on
            ui.selectTabForSnapshot (2);
            ui.getPlaylistForSnapshot().setRipple (true);
            saveSnapshot (ui, prefix + "-playlist-ripple.png");
            ui.getPlaylistForSnapshot().setRipple (false);

            // quick-add palette, and the Browser searching
            saveWindow (ui.makeQuickAddForSnapshot(), "-quickadd.png");
            ui.showDesktopForSnapshot (false);
            ui.getBrowserForSnapshot().setSearchForSnapshot ("kick");
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-browser-search.png");
            ui.getBrowserForSnapshot().setSearchForSnapshot ({});
            ui.getBrowserForSnapshot().selectFirstSampleForSnapshot(); // the tree with a sample selected and its waveform
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-browser-tree.png");

            // the undo history after a few edits
            offline.editSong ([] (lynx::Song& s) { s.bpm += 1.0; });
            offline.editSong ([] (lynx::Song& s) { if (! s.channels.empty()) s.channels[0].muted = ! s.channels[0].muted; });
            offline.editSong ([] (lynx::Song& s) { if (! s.patterns.empty()) s.patterns[(std::size_t) s.currentPattern].notes.push_back ({}); });
            offline.undo();
            saveWindow (lynx::makeHistoryPanel (offline), "-history.png");

            // spectral waveforms: a synthetic beat (kick, hats, a chord, a noise riser) as audio clips in spectral
            // colours, zoomed in, classic and stereo; the same file in the Sampler editor and the Browser preview
            {
                const juce::File wav (prefix + "-wavedemo.wav");
                writeWaveDemo (wav, offline.getSong().bpm);
                const auto path = wav.getFullPathName();
                const auto pathUtf8 = path.toStdString();
                int samplerChannel = -1, firstTrack = 0;
                offline.editSong ([&] (lynx::Song& s) {
                    for (const auto& clip : s.playlist)
                        firstTrack = std::max (firstTrack, clip.track + 1);
                    lynx::ChannelDef audio;
                    audio.name = "Beat (audio)";
                    audio.instrument = lynx::InstrumentKind::audioClip;
                    audio.samplePath = pathUtf8;
                    s.channels.push_back (audio);
                    const auto audioChannel = (int) s.channels.size() - 1;
                    lynx::ChannelDef sampler = audio;
                    sampler.name = "Beat (sampler)";
                    sampler.instrument = lynx::InstrumentKind::sampler;
                    sampler.sampler.regionStart = 0.06f;
                    sampler.sampler.regionEnd = 0.94f;
                    s.channels.push_back (sampler);
                    samplerChannel = (int) s.channels.size() - 1;

                    const auto bar = (std::int64_t) lynx::ppq * std::max (1, s.beatsPerBar);
                    const auto whole = (std::int64_t) std::llround (8.0 * s.bpm / 60.0 * lynx::ppq);
                    lynx::Clip clip;
                    clip.kind = lynx::ClipKind::audio;
                    clip.channel = audioChannel;
                    clip.track = firstTrack;
                    clip.length = whole;
                    s.playlist.push_back (clip);
                    clip.track = firstTrack + 1; // a slice of it, later in the song
                    clip.start = bar;
                    clip.offset = bar;
                    clip.length = 2 * bar;
                    s.playlist.push_back (clip);
                    // taller rows for the audio (tracks can be resized: P18)
                    if ((int) s.playlistTracks.size() < firstTrack + 2)
                        s.playlistTracks.resize ((std::size_t) firstTrack + 2);
                    s.playlistTracks[(std::size_t) firstTrack].height = 80;
                    s.playlistTracks[(std::size_t) firstTrack + 1].height = 60;
                });
                offline.dispatchPendingMessages();
                auto& cache = *lynx::ui::WaveformCache::getInstance();
                cache.find (path, [&offline, path] { return offline.getSampleLibrary().load (path); });
                cache.waitUntilIdle (30000);

                ui.selectTabForSnapshot (2);
                auto& playlist = ui.getPlaylistForSnapshot();
                playlist.setTimeSelectionForSnapshot (0, 0);
                {
                    // the Playlist alone (the floating Channel Rack would cover the clips)
                    auto saveSnapshot = [&playlist] (juce::Component&, const juce::String& file) { saveArea (playlist, playlist.getLocalBounds(), file); };
                    playlist.setViewForSnapshot (180.0, 0, std::max (0, firstTrack - 3));
                    saveSnapshot (ui, prefix + "-playlist-audio.png");
                    playlist.setViewForSnapshot (1400.0, 0, std::max (0, firstTrack - 3));
                    saveSnapshot (ui, prefix + "-playlist-audio-zoom.png");
                    lynx::ui::WaveformCache::stereo = true;
                    saveSnapshot (ui, prefix + "-playlist-audio-stereo.png");
                    lynx::ui::WaveformCache::stereo = false;
                    lynx::ui::WaveformCache::spectral = true;
                    playlist.setViewForSnapshot (180.0, 0, std::max (0, firstTrack - 3));
                    saveSnapshot (ui, prefix + "-playlist-audio-spectral.png");
                    lynx::ui::WaveformCache::spectral = false;
                    playlist.setViewForSnapshot (40.0, 0, 0);
                    saveSnapshot (ui, prefix + "-playlist-audio-far.png");

                    // LYNX_SNAPSHOT_AUDIO=a.wav;b.wav...: real audio as clips (default-height tracks, one tall,
                    // a fade, a reversed copy), at three zoom levels
                    if (const auto files = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("LYNX_SNAPSHOT_AUDIO", {}), ";", "");
                        ! files.isEmpty())
                    {
                        const auto realTrack = firstTrack + 2;
                        std::vector<juce::String> paths;
                        for (const auto& f : files)
                            if (juce::File (f.trim()).existsAsFile())
                                paths.push_back (f.trim());

                        offline.editSong ([&] (lynx::Song& s) {
                            const auto bar = (std::int64_t) lynx::ppq * std::max (1, s.beatsPerBar);
                            const std::uint32_t colours[] = { 0, 0xffd9a441, 0xff6fb36a, 0xffb06fd0, 0xff5fb7c9, 0xffd06f6f };
                            for (std::size_t i = 0; i < paths.size(); ++i)
                            {
                                lynx::ChannelDef audio;
                                audio.name = juce::File (paths[i]).getFileNameWithoutExtension().toStdString();
                                audio.instrument = lynx::InstrumentKind::audioClip;
                                audio.samplePath = paths[i].toStdString();
                                audio.colour = colours[i % 6];
                                audio.sampler.stretchBeats = i % 2 == 0 ? 16.0f : 0.0f; // fitted to 4 bars, or as recorded
                                audio.sampler.keepPitch = true;
                                s.channels.push_back (audio);
                                lynx::Clip clip;
                                clip.kind = lynx::ClipKind::audio;
                                clip.channel = (int) s.channels.size() - 1;
                                clip.track = realTrack + (int) i;
                                clip.length = 4 * bar;
                                clip.fadeIn = i == 1 ? bar / 2 : 0;
                                s.playlist.push_back (clip);
                                clip.start = 4 * bar;
                                clip.fadeIn = 0;
                                clip.fadeOut = i == 0 ? bar : 0;
                                s.playlist.push_back (clip);
                            }
                            if ((int) s.playlistTracks.size() < realTrack + (int) paths.size())
                                s.playlistTracks.resize ((std::size_t) realTrack + paths.size());
                            if (! paths.empty())
                                s.playlistTracks[(std::size_t) realTrack].height = 90;
                        });
                        offline.dispatchPendingMessages();
                        for (const auto& path2 : paths)
                            cache.find (path2, [&offline, path2] { return offline.getSampleLibrary().load (path2); });
                        cache.waitUntilIdle (60000);
                        playlist.setViewForSnapshot (160.0, 0, std::max (0, realTrack - 4));
                        saveSnapshot (ui, prefix + "-playlist-real.png");
                        playlist.setViewForSnapshot (700.0, 0, std::max (0, realTrack - 1));
                        saveSnapshot (ui, prefix + "-playlist-real-zoom.png");
                        playlist.setViewForSnapshot (2400.0, 0, std::max (0, realTrack - 1));
                        saveSnapshot (ui, prefix + "-playlist-real-close.png");
                        playlist.setViewForSnapshot (50.0, 0, std::max (0, realTrack - 4));
                        saveSnapshot (ui, prefix + "-playlist-real-far.png");
                        // the clip menu of the first real clip
                        if (const auto menu = playlist.getClipMenuForSnapshot ((int) offline.getSong().playlist.size() - (int) paths.size() * 2); menu.getNumItems() > 0)
                            saveMenus (juce::StringArray { "Audio clip" }, std::vector<juce::PopupMenu> { menu }, prefix + "-clip-menu-audio.png");
                        if (const auto menu = playlist.getClipMenuForSnapshot (0); menu.getNumItems() > 0)
                            saveMenus (juce::StringArray { "Pattern clip" }, std::vector<juce::PopupMenu> { menu }, prefix + "-clip-menu-pattern.png");

                        // the clip menu's actions, run through the menu itself; results in -clip-actions.txt
                        {
                            juce::StringArray log;
                            auto clipOf = [&offline] (const std::string& channelName, int nth) {
                                const auto& sg = offline.getSong();
                                for (int i = 0; i < (int) sg.playlist.size(); ++i)
                                {
                                    const auto& c = sg.playlist[(std::size_t) i];
                                    if (c.kind == lynx::ClipKind::audio && c.channel >= 0 && sg.channels[(std::size_t) c.channel].name == channelName && nth-- == 0)
                                        return i;
                                }
                                return -1;
                            };
                            std::function<bool (const juce::PopupMenu&, const juce::String&)> run = [&run] (const juce::PopupMenu& m, const juce::String& text) {
                                for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                                {
                                    auto& item = it.getItem();
                                    if (item.subMenu != nullptr && run (*item.subMenu, text))
                                        return true;
                                    if (item.text.startsWith (text) && item.isEnabled && item.action)
                                    {
                                        item.action();
                                        return true;
                                    }
                                }
                                return false;
                            };
                            auto act = [&] (const std::string& name, int nth, const juce::String& text) {
                                const auto clip = clipOf (name, nth);
                                const auto ok = clip >= 0 && run (playlist.getClipMenuForSnapshot (clip), text);
                                offline.dispatchPendingMessages();
                                return ok;
                            };
                            auto channelOf = [&] (const std::string& name) -> const lynx::ChannelDef* {
                                for (const auto& c : offline.getSong().channels)
                                    if (c.name == name)
                                        return &c;
                                return nullptr;
                            };
                            auto check = [&log] (const juce::String& what, bool ok) { log.add ((ok ? "OK    " : "FAIL  ") + what); };
                            const auto names = [&] { std::vector<std::string> n; for (auto& p2 : paths) n.push_back (juce::File (p2).getFileNameWithoutExtension().toStdString()); return n; }();
                            if (names.size() >= 4)
                            {
                                const auto channelsBefore = offline.getSong().channels.size();
                                check ("Reverse", act (names[2], 0, lynx::tr ("Reverse")) && channelOf (names[2])->sampler.reverse);
                                offline.undo();
                                offline.dispatchPendingMessages();
                                check ("Reverse undone", ! channelOf (names[2])->sampler.reverse);
                                act (names[2], 0, lynx::tr ("Reverse"));
                                check ("Make unique", act (names[3], 1, lynx::tr ("Make unique")) && offline.getSong().channels.size() == channelsBefore + 1
                                                          && channelOf (names[3] + " #2") != nullptr);
                                check ("Fit to tempo, resample", act (names[1], 0, lynx::tr ("Fit to tempo, resample ({0})", { juce::String() }).upToFirstOccurrenceOf ("(", false, false))
                                                                     && channelOf (names[1])->sampler.stretchBeats > 0.0f && ! channelOf (names[1])->sampler.keepPitch);
                                const auto gainBefore = offline.getSong().playlist[(std::size_t) clipOf (names[0], 1)].gain;
                                check ("Normalize", act (names[0], 1, lynx::tr ("Normalize"))
                                                        && offline.getSong().playlist[(std::size_t) clipOf (names[0], 1)].gain != gainBefore);
                                const auto clipsBefore = offline.getSong().playlist.size();
                                check ("Split here", act (names[3], 0, lynx::tr ("Split here")) && offline.getSong().playlist.size() == clipsBefore + 1);
                                check ("Mute", act (names[0], 0, lynx::tr ("Mute")) && offline.getSong().playlist[(std::size_t) clipOf (names[0], 0)].muted);
                                check ("Copy + Paste here", act (names[0], 0, lynx::tr ("Copy")) && act (names[0], 1, lynx::tr ("Paste here"))
                                                                && offline.getSong().playlist.size() == clipsBefore + 2);
                                check ("Delete", act (names[0], 2, lynx::tr ("Delete")) && offline.getSong().playlist.size() == clipsBefore + 1);
                                check ("Chop to grid", act (names[3], 0, lynx::tr ("Chop to grid")) && offline.getSong().playlist.size() > clipsBefore + 1);
                            }
                            playlist.setViewForSnapshot (160.0, 0, std::max (0, realTrack - 4));
                            saveSnapshot (ui, prefix + "-playlist-real-actions.png");
                            juce::File (prefix + "-clip-actions.txt").replaceWithText (log.joinIntoString ("\n") + "\n");
                        }
                    }
                }

                saveWindow (lynx::makeChannelSettingsPanel (offline, samplerChannel), "-sampler-wave.png");

                ui.showDesktopForSnapshot (false);
                ui.getBrowserForSnapshot().showPreviewForSnapshot (wav);
                saveSnapshot (ui, prefix + "-browser-preview.png");
            }
        }
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        quit();
    }

    // --snapshot: 8 s of a synthetic stereo beat whose parts sit in different bands (kick = low, chord = mid,
    // hats and the noise riser = high), to show the spectral waveform colours
    static void writeWaveDemo (const juce::File& file, double bpm)
    {
        constexpr double rate = 44100.0;
        const auto frames = (int) (8.0 * rate);
        juce::AudioBuffer<float> buffer (2, frames);
        buffer.clear();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        const auto beat = 60.0 / juce::jlimit (40.0, 300.0, bpm);
        std::uint32_t noise = 0x9e3779b9u;
        auto white = [&noise] {
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            return (float) (noise & 0xffffff) / (float) 0x800000 - 1.0f;
        };
        float previous = 0.0f;

        for (int i = 0; i < frames; ++i)
        {
            const auto t = i / rate;
            const auto inBeat = std::fmod (t, beat);
            const auto bar = (int) (t / (beat * 4.0));
            float left = 0.0f, right = 0.0f;

            // kick on every beat: a falling sine
            {
                const auto phase = 2.0 * juce::MathConstants<double>::pi * (48.0 * inBeat + 4.0 * (1.0 - std::exp (-inBeat * 30.0)));
                const auto k = (float) (std::sin (phase) * std::exp (-inBeat * 7.0)) * 0.85f;
                left += k;
                right += k;
            }
            // hats on the off-beats: high-passed noise bursts, a little to the right
            {
                const auto h = std::fmod (t + beat * 0.5, beat);
                const auto n = white();
                const auto hp = (n - previous) * 0.5f;
                previous = n;
                const auto hat = hp * (float) std::exp (-h * 45.0) * 0.55f;
                left += hat * 0.6f;
                right += hat;
            }
            // a chord in the second and third bars (mid band)
            if (bar == 1 || bar == 2)
            {
                const auto env = (float) std::min (1.0, std::min (t - beat * 4.0, beat * 12.0 - t) * 8.0);
                float chord = 0.0f;
                for (const auto hz : { 523.25, 659.25, 783.99 })
                    chord += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * hz * t) + 0.3 * std::sin (4.0 * juce::MathConstants<double>::pi * hz * t));
                left += chord * 0.09f * std::max (0.0f, env);
                right += chord * 0.09f * std::max (0.0f, env);
            }
            // a white-noise riser over the last two seconds (high band)
            if (t > 6.0)
            {
                const auto rise = (float) ((t - 6.0) / 2.0);
                left += white() * 0.4f * rise * rise;
                right += white() * 0.4f * rise * rise;
            }
            l[i] = left;
            r[i] = right;
        }

        buffer.applyGain (0.9f / juce::jmax (1.0e-6f, buffer.getMagnitude (0, frames)));
        file.deleteFile();
        juce::WavAudioFormat wav;
        if (std::unique_ptr<juce::AudioFormatWriter> writer { wav.createWriterFor (file.createOutputStream().release(), rate, 2, 24, {}, 0) })
            writer->writeFromAudioSampleBuffer (buffer, 0, frames);
    }

    // the menu bar's menus side by side, drawn with the LookAndFeel as they appear when opened
    static void saveMenus (lynx::MainComponent& ui, const juce::String& path)
    {
        const auto names = ui.getMenuNamesForSnapshot();
        std::vector<juce::PopupMenu> source;
        for (int m = 0; m < names.size(); ++m)
            source.push_back (ui.getMenuForSnapshot (m));
        saveMenus (names, source, path);
    }

    // menus side by side, each under its name
    static void saveMenus (const juce::StringArray& names, const std::vector<juce::PopupMenu>& source, const juce::String& path)
    {
        auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        struct Laid
        {
            juce::PopupMenu menu;
            int width = 0, height = 0;
        };
        std::vector<Laid> menus;
        const auto border = lf.getPopupMenuBorderSize();
        const auto itemHeight = 26;

        for (int m = 0; m < names.size(); ++m)
        {
            Laid laid { source[(std::size_t) m] };
            int width = 160, height = 2 * border;

            for (juce::PopupMenu::MenuItemIterator it (laid.menu); it.next();)
            {
                const auto& item = it.getItem();
                int w = 0, h = 0;
                lf.getIdealPopupMenuItemSize (item.text, item.isSeparator, item.isSectionHeader ? 22 : itemHeight, w, h);
                if (item.shortcutKeyDescription.isNotEmpty() || item.text.containsChar ('\t'))
                    w += 90;
                width = juce::jmax (width, w + 10);
                height += h;
            }

            laid.width = width;
            laid.height = height;
            menus.push_back (std::move (laid));
        }

        int totalWidth = 16, totalHeight = 0;
        for (const auto& l : menus)
        {
            totalWidth += l.width + 16;
            totalHeight = juce::jmax (totalHeight, l.height + 32);
        }

        juce::Image image (juce::Image::ARGB, totalWidth, totalHeight, true);
        {
            juce::Graphics g (image);
            g.fillAll (lynx::theme::colour::bg1);
            int x = 16;

            for (int m = 0; m < (int) menus.size(); ++m)
            {
                const auto& l = menus[(std::size_t) m];
                g.setColour (lynx::theme::colour::textSecondary);
                g.setFont (lynx::theme::font (lynx::theme::type::small, true));
                g.drawText (names[m], x, 0, l.width, 24, juce::Justification::centredLeft);
                {
                    juce::Graphics::ScopedSaveState state (g);
                    g.reduceClipRegion (x, 24, l.width, l.height);
                    g.setOrigin (x, 24);
                    lf.drawPopupMenuBackground (g, l.width, l.height);
                    int y = border;
                    bool first = true;

                    for (juce::PopupMenu::MenuItemIterator it (l.menu); it.next();)
                    {
                        const auto& item = it.getItem();
                        int w = 0, h = 0;
                        lf.getIdealPopupMenuItemSize (item.text, item.isSeparator, item.isSectionHeader ? 22 : itemHeight, w, h);
                        const juce::Rectangle<int> r (0, y, l.width, h);
                        auto text = item.text, shortcut = item.shortcutKeyDescription;
                        if (text.containsChar ('\t'))
                        {
                            shortcut = text.fromFirstOccurrenceOf ("\t", false, false);
                            text = text.upToFirstOccurrenceOf ("\t", false, false);
                        }

                        if (item.isSectionHeader)
                            lf.drawPopupMenuSectionHeader (g, r, text);
                        else
                            lf.drawPopupMenuItem (g, r, item.isSeparator, item.isEnabled, first && ! item.isSeparator && m == 0, item.isTicked,
                                                  item.subMenu != nullptr, text, shortcut, item.image.get(), nullptr);
                        first = false;
                        y += h;
                    }
                }
                x += l.width + 16;
            }
        }

        const juce::File file (path);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream (image, *out);
    }

    static void writePng (const juce::Image& image, const juce::String& path)
    {
        const juce::File file (path);
        file.deleteFile();
        juce::PNGImageFormat png;

        if (auto out = file.createOutputStream())
            png.writeImageToStream (image, *out);
    }

    // the splash screen: one settled frame, and a strip of frames through its animation
    static void saveSplashFrames (const juce::String& prefix)
    {
        lynx::SplashScreen splash;
        splash.setStatus (lynx::tr ("Loading the workspace..."));
        splash.setTimeForSnapshot (1.6);
        writePng (splash.createComponentSnapshot (splash.getLocalBounds()), prefix + "-splash.png");

        const double times[] = { 0.10, 0.22, 0.29, 0.40, 0.58, 0.75, 1.00, 1.30 };
        const auto w = lynx::SplashScreen::width / 2, h = lynx::SplashScreen::height / 2;
        juce::Image strip (juce::Image::ARGB, 4 * (w + 8) + 8, 2 * (h + 8) + 8, true);
        {
            juce::Graphics g (strip);
            g.fillAll (lynx::theme::colour::bg1);

            for (int i = 0; i < 8; ++i)
            {
                splash.setTimeForSnapshot (times[i]);
                const auto frame = splash.createComponentSnapshot (splash.getLocalBounds());
                g.drawImage (frame, 8 + (i % 4) * (w + 8), 8 + (i / 4) * (h + 8), w, h, 0, 0, frame.getWidth(), frame.getHeight());
            }
        }
        writePng (strip, prefix + "-splash-frames.png");
    }

    // every icon at 16, 20 and 24 px (at `scale`, e.g. 1.5 for a 150% display), in the toolbar's
    // colours: at rest, hovered and switched on
    static void saveIconSheet (const juce::String& path, float scale)
    {
        const int sizes[] = { 16, 20, 24 };
        const int cell = 34, columns = 3 * 3, labelWidth = 110;
        const auto w = labelWidth + columns * cell + 16, h = lynx::iconCount * cell + 16;
        juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
        {
        juce::Graphics g (image); // (drawing is flushed when it goes out of scope)
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (lynx::theme::colour::bg3);
        g.setFont (lynx::theme::font (lynx::theme::type::small));

        for (int i = 1; i < lynx::iconCount; ++i)
        {
            const auto y = 8 + (i - 1) * cell;
            g.setColour (lynx::theme::colour::textDisabled);
            g.drawText (juce::String (i), 8, y, labelWidth - 8, cell, juce::Justification::centredLeft);

            for (int v = 0; v < 3; ++v)
                for (int s = 0; s < 3; ++s)
                {
                    const juce::Rectangle<float> box ((float) (labelWidth + (v * 3 + s) * cell), (float) y, (float) cell, (float) cell);
                    const auto bg = v == 2 ? lynx::theme::colour::accent : v == 1 ? lynx::theme::colour::bg5 : lynx::theme::colour::bg3;
                    g.setColour (bg);
                    g.fillRoundedRectangle (box.reduced (2.0f), 4.0f);
                    const auto fg = v == 2 ? lynx::theme::colour::onAccent : v == 1 ? lynx::theme::colour::textPrimary
                                                                                   : lynx::theme::colour::textSecondary.brighter (0.25f);
                    lynx::icons::draw (g, (lynx::Icon) i, box.withSizeKeepingCentre ((float) sizes[s], (float) sizes[s]), fg);
                }
        }
        }

        writePng (image, path);
    }

    // a part of a component (e.g. the main toolbar)
    static void saveArea (juce::Component& c, juce::Rectangle<int> area, const juce::String& path)
    {
        const juce::File file (path);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream (c.createComponentSnapshot (area), *out);
    }

    static void savePng (const juce::Image& image, const juce::String& path)
    {
        const juce::File file (path);
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream (image, *out);
    }

    // -toolbar-tips.png: the top of the window with the bubbles of a few controls as they appear under the
    // mouse; -tips-sheet.png: every quick tip of the window, to check names, shortcuts and translations
    static void saveQuickTips (lynx::MainComponent& ui, const juce::String& prefix)
    {
        const auto tips = ui.getQuickTipsForSnapshot();
        const auto top = juce::Rectangle<int> (0, 0, ui.getWidth(), juce::jmin (ui.getHeight(), ui.getToolbarBoundsForSnapshot().getHeight() + 72));
        auto image = ui.createComponentSnapshot (top);
        {
            juce::Graphics g (image);
            const juce::String shown[] = { lynx::tr ("Pattern mode"), lynx::tr ("Play"), lynx::tr ("Metronome"), lynx::tr ("Mixer"), lynx::tr ("Save project"),
                                           lynx::tr ("Window options"), lynx::tr ("Close") };
            std::vector<juce::Rectangle<int>> used;
            for (const auto& [area, tip] : tips)
            {
                const auto name = tip.upToFirstOccurrenceOf ("\t", false, false);
                if (std::find (std::begin (shown), std::end (shown), name) == std::end (shown))
                    continue;
                const auto bubble = lynx::ui::quickTipBounds (tip, area, top);
                if (std::any_of (used.begin(), used.end(), [&] (const auto& r) { return r.intersects (bubble); }))
                    continue;
                used.push_back (bubble);
                g.setColour (juce::Colours::white.withAlpha (0.075f)); // the hovered control
                g.fillRoundedRectangle (area.toFloat().reduced (0.5f), lynx::theme::radius::md);
                lynx::ui::paintQuickTip (g, bubble.toFloat(), tip);
            }
        }
        savePng (image, prefix + "-toolbar-tips.png");

        // the sheet: bubbles in rows
        const int width = 900;
        int x = 12, y = 12;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> placed;
        for (const auto& [area, tip] : tips)
        {
            auto r = lynx::ui::quickTipSize (tip);
            if (x + r.getWidth() > width - 12)
            {
                x = 12;
                y += r.getHeight() + 10;
            }
            placed.emplace_back (r.withPosition (x, y), tip);
            x += r.getWidth() + 10;
        }
        juce::Image sheet (juce::Image::ARGB, width, y + 20 + 12, true);
        {
            juce::Graphics g (sheet);
            g.fillAll (lynx::theme::colour::bg3);
            for (const auto& [r, tip] : placed)
                lynx::ui::paintQuickTip (g, r.toFloat(), tip);
        }
        savePng (sheet, prefix + "-tips-sheet.png");
    }

    static void saveSnapshot (juce::Component& c, const juce::String& path)
    {
        const juce::File file (path);
        file.deleteFile();
        juce::PNGImageFormat png;

        if (auto out = file.createOutputStream())
            png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds()), *out);
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, lynx::AudioEngine& engine)
            : DocumentWindow (name, lynx::theme::window, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setIcon (lynx::logoImage());
            content = new lynx::MainComponent (engine);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (800, 400, 10000, 10000);
            // ~88% of the main display's usable area (at least the UI's design size)
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = display->userArea;
                setSize (juce::jmax (getWidth(), (int) (area.getWidth() * 0.88)), juce::jmax (getHeight(), (int) (area.getHeight() * 0.88)));
            }

            centreWithSize (getWidth(), getHeight());
        }

        // Shows the window (built hidden while the splash screen is up).
        void reveal()
        {
            setVisible (true);
            lynx::applyWindowChrome (*this);
            toFront (true);
            content->grabKeyboardFocus();
        }

        void openFile (const juce::File& file) { content->openFile (file); }
        void showSetupWizard() { content->showSetupWizard(); }
        void confirmDiscard (std::function<void()> then) { content->confirmDiscard (std::move (then)); }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        lynx::MainComponent* content = nullptr; // owned by the window

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    lynx::theme::LookAndFeel lookAndFeel;
    std::unique_ptr<lynx::AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<lynx::SplashScreen> splash;
    lynx::StartupOptions startupOptions;
    juce::File fileToOpen;
};

START_JUCE_APPLICATION (LynxApplication)
