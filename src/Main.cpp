#include <juce_gui_basics/juce_gui_basics.h>
#include "platform/Paths.h"

#include "core/LoadTrace.h"
#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FlpImport.h"
#include "core/NoteEdit.h"
#include "core/MaximusFx.h"
#include "core/Harmor.h"
#include "flp/Flp.h"
#include "core/SynthPresets.h"
#include "core/FlWrapper.h"
#include "engine/AudioEngine.h"
#include "engine/DllAsioDevice.h"
#include "engine/Exporter.h"
#include "engine/MidiFileIo.h"
#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"
#include "engine/ProjectFile.h"
#include "engine/SampleLibrary.h"
#include "engine/Text.h"
#include "platform/FileTypes.h"
#include "ui/ClipProperties.h"
#include "ui/NewProjectDialog.h"
#include "ui/PluginMonitorWindow.h"
#include "ui/TouchKeyboard.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/Dialogs.h"
#include "core/RackOps.h"
#include "ui/ExportDialog.h"
#include "ui/I18n.h"
#include "ui/MainComponent.h"
#include "engine/ProjectPackage.h"
#include "ui/SetupWizard.h"
#include "engine/FlStudio.h"
#include "ui/HistoryPanel.h"
#include "ui/MissingFilesDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/PluginManager.h"
#include "ui/ShortcutManager.h"
#include "ui/NoteToolDialogs.h"
#include "ui/EventEditor.h"
#include "ui/AudioEditor.h"
#include "core/EventEdit.h"
#include "core/FacetEq.h"
#include "ui/FacetEqEditor.h"
#include "core/RackRenderer.h"
#include "ui/ProEditors.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginEditors.h"
#include "core/PluginLayout.h"
#include "ui/Splash.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"
#include "ui/WindowOwner.h"
#include "platform/CrashGuard.h"

#include <chrono>
#include <cstdlib>

namespace
{
lynx::AudioEngine* crashEngine = nullptr;

// Startup trace (REA round 116): with LYNX_TRACE_STARTUP set, the time of each startup step since the C++ runtime
// started is written to %APPDATA%\LYNX\startup.log (the loader's own time before that is not included).
struct StartupTrace
{
    using clock = std::chrono::steady_clock;
    const clock::time_point t0 = clock::now(); // static initialisation: no JUCE yet, standard library only
    const bool on = std::getenv ("LYNX_TRACE_STARTUP") != nullptr;
    juce::String text;
    clock::time_point last = t0;

    double seconds() const { return std::chrono::duration<double> (clock::now() - t0).count(); } // since the process started

    void mark (const char* step)
    {
        if (! on)
            return;
        const auto now = clock::now();
        const auto ms = [] (clock::duration d) { return std::chrono::duration<double, std::milli> (d).count(); };
        text << juce::String (ms (now - t0), 1).paddedLeft (' ', 8) << " ms  +" << juce::String (ms (now - last), 1).paddedLeft (' ', 7) << "  " << step << "\n";
        last = now;
        lynx::platform::settingsFile ("startup.log").replaceWithText (text);
    }
};
StartupTrace startupTrace;

// Crashed: try to save the project and write the backtrace (like FL's "emergency save").
void onCrash (void*)
{
    const auto folder = lynx::platform::settingsFile ("autosave");
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    folder.getChildFile ("crash " + stamp + ".txt").replaceWithText (juce::SystemStats::getStackBacktrace());
    lynx::crashguard::note (("crashed: backtrace in autosave\\crash " + stamp + ".txt").toStdString());

    if (crashEngine != nullptr)
    {
        juce::String error;
        lynx::ProjectFile::save (crashEngine->getSong(), folder.getChildFile ("recovered " + stamp + lynx::ProjectFile::extension), error);
    }
}
juce::File sessionsFolder()
{
    return lynx::PluginHost::settingsFolder().getChildFile ("sessions");
}

// Sessions of earlier runs whose process is gone without closing (a fail-fast, a crash, a forced end): each is appended
// to %APPDATA%\LYNX\crash-history.log and its file removed. Returns the last step of the newest one ("" when none).
juce::String collectUncleanSessions()
{
    juce::String lastStep;
    juce::Time newest;
    for (const auto& file : sessionsFolder().findChildFiles (juce::File::findFiles, false, "session-*.txt"))
    {
        const auto text = file.loadFileAsString();
        juce::StringPairArray values;
        juce::StringArray steps;
        for (const auto& line : juce::StringArray::fromLines (text))
        {
            if (line.startsWith ("step="))
                steps.add (line.fromFirstOccurrenceOf ("=", false, false));
            else if (line.contains ("="))
                values.set (line.upToFirstOccurrenceOf ("=", false, false), line.fromFirstOccurrenceOf ("=", false, false));
        }
        const auto pid = (std::uint32_t) values["pid"].getLargeIntValue();
        const auto start = (std::uint64_t) values["start"].getLargeIntValue();
        if (pid == lynx::crashguard::processId() || lynx::crashguard::processRunning (pid, start))
            continue; // this run, or another LYNX window still open

        lynx::PluginHost::settingsFolder().getChildFile ("crash-history.log")
            .appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  previous session ended without closing\n" + text + "\n");
        file.deleteFile();

        if (const auto modified = file.getLastModificationTime(); lastStep.isEmpty() || modified > newest)
        {
            newest = modified;
            lastStep = steps.isEmpty() ? juce::String ("start-up") : steps[steps.size() - 1].fromFirstOccurrenceOf (" ", false, false);
        }
    }
    return lastStep;
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
        {
            lynx::crashguard::quietCrashReports();
            sandboxChild = true;
            return;
        }

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

        if (args.contains ("--rtstress"))
        {
            // LYNX.exe --rtstress project.(flp|lynx) report.txt [seconds] [--buffer N] [--type "driver type"] [--device "output"]
            // [--no-window] [--record take.wav]: the real app (window, audio device) plays the project and reports the callbacks
            // that missed their deadline (REA round 87); --type/--device pick another driver for this run only (round 97);
            // --record also records the input during the run, like Rec + Play, and moves the take to take.wav (round 106)
            const auto i = args.indexOf ("--rtstress");
            const auto seconds = args[i + 3].getDoubleValue();
            const auto buffer = args.contains ("--buffer") ? args[args.indexOf ("--buffer") + 1].getIntValue() : 0;
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            const auto option = [&args] (const char* name) { return args.contains (name) ? args[args.indexOf (name) + 1].unquoted() : juce::String(); };
            runRealtimeStress (juce::File (args[i + 1].unquoted()), juce::File (args[i + 2].unquoted()), seconds > 0.0 ? seconds : 30.0, buffer,
                               option ("--type"), option ("--device"), option ("--record"));
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
            // no crash dialogs from the plugin being examined; and once the result is written, the process ends without
            // unloading it: a Waves shell crashed in its own teardown at exit (Intel GPU driver fail-fast, 0xC0000409),
            // which showed Windows' crash dialog and made the scan reject the whole shell (round 111)
            lynx::crashguard::quietCrashReports();
            lynx::PluginHost::scanOneFile (args[i + 1].unquoted(), args[i + 2].unquoted(), juce::File (args[i + 3].unquoted()));
            lynx::crashguard::exitNow (0);
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
                for (const auto& name : type->getDeviceNames (true))
                    report << "  input=" << name << "\n";
            }

            const auto flag = args.contains ("--devices") ? args.indexOf ("--devices") : args.indexOf ("--dispositivos");
            juce::File (args[flag + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--devicetest"))
        {
            // LYNX.exe --devicetest "type" "device" report.txt ["input device"]: opens that output device (and, with the
            // 4th argument, an input: "*" = the same device / the driver's default input), receives 1 s of callbacks
            // writing silence, closes it and reports rate, block size, latency, callbacks and the input channels with
            // their peak level (driver check, no sound)
            const auto i = args.indexOf ("--devicetest");
            const auto typeName = args[i + 1].unquoted(), deviceName = args[i + 2].unquoted();
            auto inputName = args.size() > i + 4 && ! args[i + 4].startsWith ("--") ? args[i + 4].unquoted() : juce::String();
            const auto wantInput = inputName.isNotEmpty();
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
                    if (wantInput && type->hasSeparateInputsAndOutputs())
                    {
                        const auto inputs = type->getDeviceNames (true);
                        if (inputName == "*" || ! inputs.contains (inputName))
                        {
                            const auto index = type->getDefaultDeviceIndex (true);
                            inputName = juce::isPositiveAndBelow (index, inputs.size()) ? inputs[index] : juce::String();
                        }
                        for (const auto& n : inputs)
                            step ("input_device=" + n);
                        step ("input_chosen=" + inputName);
                    }
                    else if (wantInput)
                        inputName = deviceName;
                    step ("step=create");
                    device.reset (type->createDevice (deviceName, type->hasSeparateInputsAndOutputs() ? inputName : juce::String()));
                    step ("step=created");
                }
            struct Silence final : juce::AudioIODeviceCallback
            {
                std::atomic<int> calls { 0 }, inputs { 0 };
                std::array<std::atomic<float>, 64> peaks {};
                void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int channels, int n,
                                                       const juce::AudioIODeviceCallbackContext&) override
                {
                    for (int c = 0; c < channels; ++c)
                        if (out[c] != nullptr)
                            juce::FloatVectorOperations::clear (out[c], n);
                    inputs.store (numIn);
                    for (int c = 0; c < juce::jmin (numIn, 64); ++c)
                        if (in[c] != nullptr)
                        {
                            const auto range = juce::FloatVectorOperations::findMinAndMax (in[c], n);
                            const auto peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
                            if (peak > peaks[(std::size_t) c].load())
                                peaks[(std::size_t) c].store (peak);
                        }
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
                juce::BigInteger inputs;
                if (wantInput)
                {
                    const auto names = device->getInputChannelNames();
                    step ("input_channels=" + juce::String (names.size()));
                    for (int c = 0; c < names.size(); ++c)
                        step ("  in" + juce::String (c + 1) + "=" + names[c]);
                    inputs.setRange (0, juce::jmin (64, names.size()), true);
                }
                // --buffer N: ask for that block size (REA round 97 latency survey); default: the driver's default
                const auto wantedBlock = args.contains ("--buffer") ? args[args.indexOf ("--buffer") + 1].getIntValue() : 0;
                const auto error = device->open (inputs, outputs, rate, wantedBlock > 0 ? wantedBlock : device->getDefaultBufferSize());
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
                    if (wantInput)
                    {
                        report << "input_latency=" << device->getInputLatencyInSamples() << "\n"
                               << "active_inputs=" << device->getActiveInputChannels().countNumberOfSetBits() << "\n"
                               << "callback_inputs=" << silence.inputs.load() << "\n";
                        for (int c = 0; c < juce::jmin (64, silence.inputs.load()); ++c)
                            report << "  peak_in" << (c + 1) << "=" << juce::Decibels::gainToDecibels (silence.peaks[(std::size_t) c].load(), -150.0f) << " dB\n";
                    }
                    ok = silence.calls.load() > 0 && (! wantInput || silence.inputs.load() > 0);
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

        if (args.contains ("--vst2test"))
        {
            const auto i = args.indexOf ("--vst2test");
            runVst2Test (args[i + 1].unquoted(), args[i + 2].unquoted());
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
            // optional 3rd argument: the plugin's name inside a shell (e.g. "CLA-2A Stereo" in a WaveShell)
            const auto only = i + 3 < args.size() && ! args[i + 3].startsWith ("--") ? args[i + 3].unquoted() : juce::String();
            runPluginTest (args[i + 1].unquoted(), args[i + 2].unquoted(), only);
            return;
        }

        if (args.contains ("--plugui"))
        {
            // LYNX.exe --plugui <folder>: the native effect editors at FL's size (one PNG per editor and page), each
            // loaded with the FL plugin's Default preset when the FL installation has one
            const auto i = args.indexOf ("--plugui");
            runPluginEditorSnapshots (args[i + 1].unquoted());
            return;
        }

        if (args.contains ("--fxfuzz"))
        {
            // LYNX.exe --fxfuzz report.txt [rounds] [--drags]: every built-in effect in its editor, parameters swept (and
            // dragged with the mouse), audio rendered at several rates; a crash ends the process (round 111)
            const auto i = args.indexOf ("--fxfuzz");
            const auto rounds = args.size() > i + 2 ? juce::jmax (1, args[i + 2].getIntValue()) : 6;
            // --drags: also mouse drags over the editors (their menus may flash on screen: not for the selftest)
            const auto ok = runEffectFuzz (rounds, args[i + 1].unquoted(), args.contains ("--drags"));
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
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

        // session breadcrumb: a fail-fast (no crash handler runs) still leaves the last steps behind (round 111)
        uncleanLastStep = collectUncleanSessions();
        sessionsFolder().createDirectory();
        lynx::crashguard::beginSession (
            sessionsFolder().getChildFile ("session-" + juce::String (lynx::crashguard::processId()) + ".txt").getFullPathName().toStdString(),
            ("LYNX session\nversion=" + getApplicationVersion() + "\npid=" + juce::String (lynx::crashguard::processId())
             + "\nstart=" + juce::String ((juce::int64) lynx::crashguard::processStartTime())
             + "\nstarted=" + juce::Time::getCurrentTime().toISO8601 (true) + "\n").toStdString());

        startupTrace.mark ("initialise (normal start)");
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        startupOptions = lynx::StartupOptions::load();
        lynx::registerFileTypes();
        startupTrace.mark ("options + file types");
        // Double-clicking a .lynx/.flp opens LYNX with the file as an argument.
        if (args.size() > 0 && juce::File::isAbsolutePath (args[0].unquoted()))
            fileToOpen = juce::File (args[0].unquoted());

        if (! startupOptions.splash)
        {
            startEngine();
            createMainWindow();
            mainWindow->reveal();
            showUncleanNotice();
            startupTrace.mark ("window revealed");
            openStartupFile();
            startupTrace.mark ("startup file opened");
            juce::MessageManager::callAsync ([] { startupTrace.mark ("first message loop turn"); });
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
            // the minimum counts from the process start (the audio device took part of it already)
            splashDuration = lynx::SplashScreen::remainingDuration (startupTrace.seconds());
            splash->setBarDuration (splashDuration);

            juce::Timer::callAfterDelay (1050, [this] {
                if (splash == nullptr || engine == nullptr)
                    return;

                createMainWindow();
                const auto wait = juce::jmax (0.0, splashDuration - splash->getElapsed());

                juce::Timer::callAfterDelay ((int) (wait * 1000.0), [this] {
                    if (splash == nullptr || mainWindow == nullptr)
                        return;

                    mainWindow->reveal();
                    startupTrace.mark ("window revealed (after the splash)");
                    splash->toFront (false);
                    splash->finish ([this] { splash = nullptr; });
                    showUncleanNotice();
                    openStartupFile();
                    startupTrace.mark ("startup file opened");
                    juce::MessageManager::callAsync ([] { startupTrace.mark ("first message loop turn"); });
                    // first run: language, audio, plugin folders (FL Studio's included) and a background scan
                    if (! lynx::setupWizardDone())
                        mainWindow->showSetupWizard();
                });
            });
        });
    }

    void startEngine()
    {
        startupTrace.mark ("before the audio engine");
        engine = std::make_unique<lynx::AudioEngine>();
        startupTrace.mark ("audio engine + device");
        crashEngine = engine.get();
        addDriverFileDeviceType();

        if (startupOptions.sound)
            engine->previewSample (lynx::makeStartupSample());
        startupTrace.mark ("startup sound");

        // a project given on the command line: its slow plugin modules (a Waves shell) start now, on their own
        // thread, while the splash and the main window come up (round 121)
        if (fileToOpen.hasFileExtension ("flp"))
            if (juce::MemoryBlock data; fileToOpen.loadFileAsData (data))
                try
                {
                    engine->getPluginHost().startModulesFor (lynx::flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() })));
                    startupTrace.mark ("plugin modules started ahead");
                }
                catch (const flp::Error&)
                {
                }
    }

    void createMainWindow()
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
        lynx::setMainWindowForOwnership (mainWindow.get());
        startupTrace.mark ("main window built");
    }

    // the previous run ended without closing (see collectUncleanSessions): say so once, with its last step
    void showUncleanNotice()
    {
        if (uncleanLastStep.isEmpty())
            return;
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, lynx::tr ("LYNX closed unexpectedly"),
            lynx::tr ("The last session ended without closing normally. Last step: {0}. Backup copies of your project are in "
                      "LYNX's autosave folder; details in crash-history.log.",
                      { uncleanLastStep }));
        uncleanLastStep.clear();
    }

    void openStartupFile()
    {
        if (fileToOpen != juce::File() && mainWindow != nullptr)
            mainWindow->openFile (fileToOpen);
    }

    void shutdown() override
    {
        lynx::crashguard::note ("closing");
        lynx::shutdownPluginSandboxWorker();
        splash = nullptr;
        mainWindow = nullptr; // the UI goes before the engine
        crashEngine = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lynx::crashguard::endSession(); // a clean exit: no breadcrumb left behind

        // everything is saved and closed: a process that loaded third-party plugins ends here, before the DLLs' own
        // teardown runs (some crash in it at exit, round 111)
        if (sandboxChild || lynx::PluginHost::pluginsWereLoaded())
            lynx::crashguard::exitNow (getApplicationReturnValue());
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->confirmDiscard ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

private:
    bool sandboxChild = false;
    juce::String uncleanLastStep; // last step of a previous run that ended without closing

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
        original.playlistTracks.resize (3);
        original.playlistTracks[2].colour = 0xFF2A6FDB; // a track colour (round 90)
        original.playlistTracks[2].grouped = true;      // track groups and rack filter groups (round 104)
        original.playlistTracks[1].collapsed = true;
        original.channelGroups = { "Drums", "Keys" };
        original.channels[1].group = 1;
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
                               && reloaded->playlistTracks.size() == 3 && reloaded->playlistTracks[2].colour == 0xFF2A6FDB
                               && reloaded->playlistTracks[1].colour == 0
                               && reloaded->playlistTracks[2].grouped && reloaded->playlistTracks[1].collapsed && ! reloaded->playlistTracks[1].grouped
                               && reloaded->channelGroups == original.channelGroups && reloaded->channels[1].group == 1 && reloaded->channels[0].group == -1
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

        // LYNX substitutes (round 98): a missing plugin's slot plays a built-in equivalent and the project keeps the
        // original reference byte for byte (.flp record and LYNX plugin state)
        bool substituteOk = false;
        {
            auto song = lynx::Song::makeDefault();
            lynx::ForeignPlugin vv; // an .flp VST3 record (ValhallaVintageVerb's class ID), opaque state
            vv.format = lynx::ForeignPlugin::Format::vst3;
            const std::uint8_t cid[] = { 0x76, 0x54, 0x53, 0x56, 0x65, 0x65, 0x76, 0x33, 0x61, 0x6c, 0x68, 0x61, 0x6c, 0x6c, 0x61, 0x76 };
            std::copy (std::begin (cid), std::end (cid), vv.classId.begin());
            vv.hasClassId = true;
            vv.name = "ValhallaVintageVerb";
            vv.path = "C:\\x\\ValhallaVintageVerb.vst3";
            for (int i = 0; i < 777; ++i)
                vv.state.push_back ((std::uint8_t) (i * 13 + 1));
            lynx::EffectSlot fromFlp;
            fromFlp.kind = lynx::EffectKind::plugin;
            fromFlp.plugin.foreign = vv;
            lynx::EffectSlot fromLynx; // a LYNX reference whose plugin isn't installed here
            fromLynx.kind = lynx::EffectKind::plugin;
            fromLynx.plugin.id = "VST3-FabFilter Pro-Q 3-1a2b3c4d-5e6f7a8b";
            fromLynx.plugin.name = "FabFilter Pro-Q 3";
            fromLynx.plugin.state = "AAECAwQFBgcICQ==";
            const auto flpRef = fromFlp.plugin, lynxRef = fromLynx.plugin;
            song.mixer[1].effects = { fromFlp, fromLynx };
            const auto substitutesOn = lynx::pluginSubstitutesEnabled();
            const auto changed = offline.getPluginHost().applySubstitutes (song);
            auto& fx = song.mixer[1].effects;
            // only the LYNX reference is substituted here (an .flp record is the import's job); do the .flp one directly
            const auto flpSubstituted = lynx::substituteEffectSlot (fx[0], lynx::pluginIdentity (vv));
            const auto back = lynx::ProjectFile::fromJson (lynx::ProjectFile::toJson (song), error);
            substituteOk = flpSubstituted && fx[0].kind == lynx::EffectKind::epochVerb
                           && (! substitutesOn || (changed == 1 && fx[1].kind == lynx::EffectKind::facetEq && fx[1].substituteFor == lynxRef))
                           && back && back->mixer[1].effects.size() == 2 && back->mixer[1].effects[0].kind == lynx::EffectKind::epochVerb
                           && back->mixer[1].effects[0].substituteFor == flpRef && back->mixer[1].effects[0].substituteFor.foreign.state == vv.state
                           && back->mixer[1].effects[1].kind == fx[1].kind && back->mixer[1].effects[1].substituteFor == fx[1].substituteFor;
        }

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

        // project package (REA round 118): a project with a sample exported as .zip, extracted and opened again,
        // finds its sample inside the extracted folder
        bool packageOk = false;
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-selftest-package");
            dir.deleteRecursively();
            dir.getChildFile ("src").createDirectory();
            const auto wav = dir.getChildFile ("src/hit.wav");
            {
                juce::WavAudioFormat format;
                juce::AudioBuffer<float> hit (1, 480);
                for (int i = 0; i < 480; ++i)
                    hit.setSample (0, i, 0.5f * std::sin (0.1f * (float) i));
                std::unique_ptr<juce::OutputStream> out (wav.createOutputStream().release());
                if (auto* w = format.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0))
                {
                    out.release();
                    std::unique_ptr<juce::AudioFormatWriter> writer (w);
                    writer->writeFromAudioSampleBuffer (hit, 0, 480);
                }
            }
            auto source = lynx::Song::makeDefault();
            lynx::ChannelDef c;
            c.name = "Hit";
            c.instrument = lynx::InstrumentKind::sampler;
            c.samplePath = wav.getFullPathName().toStdString();
            source.channels.push_back (c);
            lynx::ProjectPackage::Result packed;
            juce::String packageError;
            const auto zip = dir.getChildFile ("Beat.zip");
            if (wav.existsAsFile() && lynx::ProjectPackage::exportZip (source, "Beat", zip, packed, packageError) && packed.files == 1)
            {
                const auto project = lynx::ProjectPackage::extractZip (zip, dir.getChildFile ("out"), packageError);
                const auto song = project.existsAsFile() ? lynx::ProjectFile::load (project, packageError) : std::nullopt;
                if (song && ! song->channels.empty())
                {
                    const juce::File back (juce::String::fromUTF8 (song->channels.back().samplePath.c_str()));
                    packageOk = back.existsAsFile() && back.isAChildOf (project.getParentDirectory()) && back.getSize() == wav.getSize();
                }
            }
            dir.deleteRecursively();
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

        // every built-in effect's editor and engine with extreme settings (round 111): a crash or NaN output fails here
        const auto effectsOk = runEffectFuzz (4, {}); // (no mouse drags: nothing may open on screen)

        const auto ok = audioOk && projectOk && richOk && substituteOk && undoOk && recordOk && recordUndoOk && midiOk && packageOk
                        && wavpack != "ERROR" && effectsOk;

        juce::String report;
        report << "connections=" << offline.getNumGraphConnections() << "\n"
               << "peak=" << peak << "\n"
               << "project=" << (projectOk ? "OK" : "ERROR " + error) << "\n"
               << "project_features=" << (richOk ? "OK" : "ERROR") << "\n"
               << "plugin_substitutes=" << (substituteOk ? "OK" : "ERROR") << "\n"
               << "undo=" << (undoOk ? "OK" : "ERROR") << "\n"
               << "recording=" << (recordOk && recordUndoOk ? "OK" : "ERROR") << "\n"
               << "midi=" << (midiOk ? "OK" : "ERROR") << "\n"
               << "package=" << (packageOk ? "OK" : "ERROR") << "\n"
               << "wavpack=" << wavpack << "\n"
               << "effects=" << (effectsOk ? "OK" : "NONFINITE") << "\n"
               << "result=" << (ok ? "OK" : "FAILED") << "\n";

        if (reportPath.isNotEmpty())
            juce::File (reportPath).replaceWithText (report);

        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // Every built-in effect in its native editor: random / extreme parameters, mouse drags over the editor, the editor
    // painted, audio rendered at 44.1 / 48 / 96 kHz. Returns false when an effect output NaN / inf (muted by the
    // renderer's guard). The report lists each effect. Used by --fxfuzz and (a short run) by --selftest.
    bool runEffectFuzz (int rounds, const juce::String& reportPath, bool drags = false)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        lynx::setNativeEditorDemo (true);
        juce::String report;
        auto allFinite = true;
        {
            lynx::AudioEngine offline (false);
            juce::Random random (111);
            const auto value = [&] { const auto r = random.nextInt (6); return r == 0 ? 0.0f : r == 1 ? 1.0f : random.nextFloat(); };

            for (int k = 0; k <= (int) lynx::EffectKind::geminiDoubler; ++k)
            {
                const auto kind = (lynx::EffectKind) k;
                if (! lynx::NativeEffectWindows::canOpen (kind))
                    continue;
                const auto before = lynx::RackRenderer::nonFiniteEffectBlocks();
                const auto& info = lynx::effectInfo (kind);
                auto fx = lynx::makeEffectSlot (kind);
                int dragged = 0; // parameter changes made by the mouse drags

                for (int round = 0; round < rounds; ++round)
                {
                    if (round > 0)
                        for (std::size_t p = 0; p < info.params.size(); ++p)
                            fx.params[p] = round == 1 ? 0.0f : round == 2 ? 1.0f : value();
                    auto song = lynx::Song::makeDefault(); // its demo beat feeds the effect
                    song.mixer.resize (2);
                    song.mixer[0].name = "Master";
                    song.mixer[1].name = "Insert 1";
                    song.mixer[1].effects.assign (1, fx);
                    for (auto& channel : song.channels)
                        channel.mixerTrack = 1;
                    offline.setSong (std::move (song));
                    offline.dispatchPendingMessages();

                    const auto pages = juce::jmax (1, (int) lynx::plugui::layoutFor (kind).pages.size());
                    for (int page = 0; page < pages; ++page)
                    {
                        auto editor = kind == lynx::EffectKind::facetEq ? lynx::makeFacetEqEditor (offline, 1, 0, false)
                                                                        : lynx::makeNativeEffectEditor (offline, 1, 0, 1.0f, page);
                        if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0)
                            continue;
                        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                        const auto paint = [&] {
                            juce::Graphics g (image);
                            editor->paintEntireComponent (g, false);
                        };
                        paint();

                        // drags from a grid of points, as a user moving every control (plain left button)
                        if (round >= 3 && drags)
                        {
                            const auto source = juce::Desktop::getInstance().getMainMouseSource();
                            const auto step = juce::jmax (12, juce::jmin (editor->getWidth(), editor->getHeight()) / 8);
                            for (int y = step / 2; y < editor->getHeight(); y += step)
                                for (int x = step / 2; x < editor->getWidth(); x += step)
                                {
                                    auto* target = editor->getComponentAt (x, y);
                                    if (target == nullptr)
                                        continue;
                                    const auto at = target->getLocalPoint (editor.get(), juce::Point<float> ((float) x, (float) y));
                                    const auto now = juce::Time::getCurrentTime();
                                    const auto event = [&] (juce::Point<float> p, juce::ModifierKeys mods) {
                                        return juce::MouseEvent (source, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, target, target, now, at, now, 1, false);
                                    };
                                    const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                                    const juce::Component::SafePointer<juce::Component> alive (target);
                                    target->mouseDown (event (at, down));
                                    const auto dy = (float) (random.nextBool() ? 400 : -400), dx = (float) (random.nextInt (401) - 200);
                                    if (alive != nullptr)
                                        target->mouseDrag (event (at + juce::Point<float> (dx * 0.5f, dy * 0.5f), down));
                                    if (alive != nullptr)
                                        target->mouseDrag (event (at + juce::Point<float> (dx, dy), down));
                                    if (alive != nullptr)
                                        target->mouseUp (event (at + juce::Point<float> (dx, dy), juce::ModifierKeys()));
                                    juce::PopupMenu::dismissAllActiveMenus();
                                    offline.dispatchPendingMessages();
                                }
                            paint();
                        }
                    }

                    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
                        offline.renderOffline (0.05, rate);
                    if (round == 0 || round >= 3) // keep what the drags set for the next round
                        if (offline.getSong().mixer.size() > 1 && ! offline.getSong().mixer[1].effects.empty())
                        {
                            const auto& now = offline.getSong().mixer[1].effects[0];
                            for (std::size_t p = 0; p < fx.params.size(); ++p)
                                dragged += now.params[p] != fx.params[p] ? 1 : 0;
                            dragged += now.state != fx.state ? 1 : 0;
                            fx = now;
                        }
                }

                const auto broken = lynx::RackRenderer::nonFiniteEffectBlocks() - before;
                allFinite = allFinite && broken == 0;
                report << info.name << ": " << (broken == 0 ? juce::String ("OK") : "non-finite output in " + juce::String ((juce::int64) broken) + " blocks")
                       << " (" << dragged << " changes by mouse)\n";
                if (reportPath.isNotEmpty())
                    juce::File (reportPath).replaceWithText (report + "(running)\n"); // what was reached, if it crashes
            }
        }
        lynx::setNativeEditorDemo (false);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        report << "result=" << (allFinite ? "OK" : "NONFINITE") << "\n";
        if (reportPath.isNotEmpty())
            juce::File (reportPath).replaceWithText (report);
        return allFinite;
    }

    // LYNX.exe --plugintest plugin.vst3 report.txt ["name"]: loads the plugin (or the one named in a shell), plays notes if it is an
    // instrument (and runs audio through it if it is an effect) and measures the audio.
    void runPluginTest (const juce::String& pluginPath, const juce::String& reportPath, const juce::String& only = {})
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
                if (juce::File (type.fileOrIdentifier).getFileName() != juce::File (pluginPath).getFileName()
                    || (only.isNotEmpty() && type.name != only))
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

            const auto effectSlot = (int) song.mixer[0].effects.size() - 1; // (the default master may hold effects already)
            offline.setSong (song);

            // after a render: the rack (and its plugins) is built when the engine is prepared
            const auto peak = offline.renderOffline (3.0, 48000.0);
            const auto* loadedInstrument = dynamic_cast<lynx::PluginInstrument*> (offline.getChannelInstrument (4).get());
            const auto* loadedEffect = dynamic_cast<lynx::PluginEffect*> (offline.getEffect (0, effectSlot).get());
            report << "instrument_loaded=" << (loadedInstrument != nullptr ? "yes" : "no") << "\n"
                   << "effect_loaded=" << (loadedEffect != nullptr ? "yes" : "no") << "\n";
            if (host.getLoadFailures().size() > 0) // why: the host's own message (license, missing file, load failure)
                report << "load_failures=" << host.getLoadFailures().joinIntoString (" | ") << "\n";
            report << "peak=" << peak << "\n";
            // plugins that crashed inside a call, were caught and switched off (round 111)
            report << "plugin_crashes=" << lynx::pluginCrashCount() << "\n";
            for (const auto& c : lynx::pluginCrashes())
                report << "  crashed: " << c.plugin << " (" << lynx::crashguard::describe (c.code) << ")\n";

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

    // LYNX.exe --vst2test "LYNX Test Synth.dll" report.txt (tools/testplugin/lynx_test_vst2.c): LYNX's own VST2 host.
    // Scans the DLL, imports it the way an .flp holds it (Fruity Wrapper VST2 state with a chunk, REA_rodada89 §2),
    // renders with two saved gains (the level must follow the restored state) and checks the plugin saw the song tempo.
    void runVst2Test (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        {
            lynx::AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            report << "plugins_in_file=" << host.addPluginFile (juce::File (pluginPath)) << "\n";
            std::optional<juce::PluginDescription> vst2;
            for (const auto& type : host.getKnownPlugins().getTypes())
                if (type.pluginFormatName == "VST")
                    vst2 = type;

            auto render = [&] (float gain, float& tempoSeen, int& adopted) {
                lynx::FlVst2State state;
                state.flags = 8;
                state.chunk = { 'L', 'X', 'V', '2', 0, 0, 0, 0 };
                std::memcpy (state.chunk.data() + 4, &gain, 4);
                auto song = lynx::Song::makeDefault();
                song.bpm = 137.0;
                auto& f = song.channels[4].plugin.foreign;
                f.format = lynx::ForeignPlugin::Format::vst2;
                f.vst2Id = (std::uint32_t) vst2->uniqueId;
                f.name = "Some other name"; // found by the VST2 ID
                f.state = lynx::writeFlVst2State (state);
                song.channels[4].volume = 1.0f;
                song.patterns[0].notes.clear();
                song.patterns[0].notes.push_back ({ 0, 4 * lynx::ppq, 4, 69, 1.0f });
                for (std::size_t c = 0; c < song.channels.size(); ++c)
                    if (c != 4)
                        song.channels[c].muted = true;
                adopted = host.adoptFlPlugins (song);
                offline.setSong (song);
                const auto peak = offline.renderOffline (1.0, 48000.0);
                offline.capturePluginStates();
                juce::MemoryBlock saved;
                saved.fromBase64Encoding (juce::String (offline.getSong().channels[4].plugin.state));
                lynx::FlVst2State back;
                tempoSeen = 0.0f;
                if (lynx::parseFlVst2State (std::vector<std::uint8_t> ((const std::uint8_t*) saved.getData(), (const std::uint8_t*) saved.getData() + saved.getSize()), back)
                    && back.chunk.size() >= 12)
                    std::memcpy (&tempoSeen, back.chunk.data() + 8, 4);
                return peak;
            };

            if (vst2)
            {
                report << "plugin=" << vst2->name << " id=" << vst2->uniqueId << " instrument=" << (int) vst2->isInstrument << "\n";
                float tempoA = 0, tempoB = 0;
                int adoptedA = 0, adoptedB = 0;
                const auto quiet = render (0.1f, tempoA, adoptedA);
                const auto loud = render (0.4f, tempoB, adoptedB);
                report << "adopted=" << adoptedA << "," << adoptedB << " peak_gain_0.1=" << quiet << " peak_gain_0.4=" << loud
                       << " tempo_seen=" << tempoB * 1000.0f << "\n";
                ok = adoptedA == 1 && adoptedB == 1 && quiet > 0.01f && std::abs (loud / quiet - 4.0f) < 0.2f
                     && std::abs (tempoB * 1000.0f - 137.0f) < 0.01f;
            }
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

    // --rtstress: opens the project in the normal window with the saved audio device, plays it from the start for
    // `seconds` and writes the real-time statistics (deadline misses, late device calls, load histogram, heaviest units)
    void runRealtimeStress (const juce::File& project, const juce::File& report, double seconds, int bufferSize,
                            const juce::String& deviceType, const juce::String& deviceName, const juce::String& recordTo = {})
    {
        startupOptions = lynx::StartupOptions::load();
        engine = std::make_unique<lynx::AudioEngine>();
        crashEngine = engine.get();
        // the user's saved device stays as it was: this run's choices are undone before quitting
        savedDeviceState = engine->getDeviceManager().createStateXml();
        auto& devices = engine->getDeviceManager();
        const auto driverFile = addDriverFileDeviceType();
        const auto type = deviceType.isEmpty() && driverFile ? juce::String (lynx::dllAsioTypeName) : deviceType;
        if (type.isNotEmpty() && devices.getCurrentAudioDeviceType() != type)
            devices.setCurrentAudioDeviceType (type, true);
        if (bufferSize > 0 || deviceName.isNotEmpty())
        {
            auto setup = devices.getAudioDeviceSetup();
            if (bufferSize > 0)
                setup.bufferSize = bufferSize;
            if (deviceName.isNotEmpty())
                setup.outputDeviceName = deviceName;
            const auto error = devices.setAudioDeviceSetup (setup, true);
            if (error.isNotEmpty())
                report.replaceWithText ("device error: " + error + "\n");
        }
        createMainWindow();
        if (! juce::JUCEApplication::getCommandLineParameters().contains ("--no-window")) // --no-window: is it the UI?
            mainWindow->reveal();

        juce::String error;
        auto ok = false;
        const auto loadStart = juce::Time::getMillisecondCounterHiRes();
        if (project.hasFileExtension ("flp"))
            ok = engine->importFlpFile (project, error);
        else if (auto song = lynx::ProjectFile::load (project, error))
        {
            engine->setSong (std::move (*song));
            ok = true;
        }
        const auto loadMs = juce::Time::getMillisecondCounterHiRes() - loadStart;

        if (! ok)
        {
            report.replaceWithText ("project: " + project.getFullPathName() + "\nresult=ERROR " + error + "\n");
            restoreSavedDeviceState();
            setApplicationReturnValue (1);
            quit();
            return;
        }

        if (recordTo.isNotEmpty())
            engine->setAudioRecordingArmed (true); // opens the input of the driver

        // let the window draw the project, then play from the start with fresh statistics
        juce::Timer::callAfterDelay (1500, [this, project, report, seconds, loadMs, recordTo] {
            if (engine == nullptr)
                return;
            engine->stop();
            engine->rewind();
            engine->getRtStats().reset();
            engine->takeUiPulse();
            if (recordTo.isNotEmpty())
                engine->setRecording (true);
            engine->play();
            juce::Timer::callAfterDelay ((int) (seconds * 1000.0), [this, project, report, seconds, loadMs, recordTo] {
                if (engine == nullptr)
                    return;
                engine->stop();
                juce::String recording;
                if (recordTo.isNotEmpty())
                    recording = finishStressTake (juce::File (recordTo), seconds);
                const auto s = engine->getRtStats().snapshot();
                juce::String text;
                text << "project: " << project.getFullPathName() << "\n"
                     << "LYNX " << getApplicationVersion() << ", " << juce::SystemStats::getOperatingSystemName() << ", "
                     << juce::SystemStats::getNumCpus() << " logical CPUs (" << juce::SystemStats::getNumPhysicalCpus() << " cores), "
                     << juce::SystemStats::getCpuModel() << "\n"
                     << "load " << juce::String (loadMs / 1000.0, 1) << " s, played " << juce::String (seconds, 0) << " s\n"
                     << engine->describeRealtime();
                const auto pulse = engine->takeUiPulse();
                text << "  UI: the 30 Hz engine timer ran " << pulse.calls << " times (" << juce::String (pulse.calls / seconds, 1)
                     << "/s), longest gap " << juce::String (pulse.maxGapMs, 0) << " ms" << (mainWindow == nullptr || ! mainWindow->isVisible() ? " (window hidden)" : "") << "\n";
                text << "  load histogram (share of the deadline: callbacks):";
                for (std::size_t b = 0; b < s.histogram.size(); ++b)
                    if (s.histogram[b] > 0)
                        text << " " << (int) (b * 10) << (b + 1 == s.histogram.size() ? juce::String ("+") : "-" + juce::String ((int) (b * 10 + 10))) << "%:" << (juce::int64) s.histogram[b];
                text << "\n";
                text << recording;
                text << "result=" << (s.callbacks == 0 ? "ERROR no audio callbacks" : s.overBudget == 0 && s.lateCalls == 0 ? "OK" : "GLITCHES") << "\n";
                report.replaceWithText (text);
                restoreSavedDeviceState();
                setApplicationReturnValue (s.callbacks == 0 ? 1 : 0);
                quit();
            });
        });
    }

    // --rtstress --record: stops recording (the take becomes a clip of the song, as in the app), moves the take's WAV to
    // `target` and describes it: length against the time played, channels, peak, the latencies the driver reported.
    juce::String finishStressTake (const juce::File& target, double seconds)
    {
        juce::String text;
        auto* device = engine->getDeviceManager().getCurrentAudioDevice();
        text << "  recording: input " << (device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0) << " channel(s)";
        if (device != nullptr)
            text << ", input latency " << device->getInputLatencyInSamples() << " + output latency " << device->getOutputLatencyInSamples() << " samples";
        engine->setRecording (false); // closes the take: a clip of the song
        juce::File take;
        for (const auto& c : engine->getSong().channels)
            if (c.instrument == lynx::InstrumentKind::audioClip)
                take = juce::File (juce::String::fromUTF8 (c.samplePath.c_str()));
        if (! take.existsAsFile())
            return text + "\n  recording: no take (" + engine->getLastRecordingError() + ")\n";
        target.deleteFile();
        if (! take.moveFileTo (target))
            take.copyFileTo (target);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (target)); reader != nullptr)
        {
            juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
            const auto peak = audio.getMagnitude (0, audio.getNumSamples());
            const auto length = (double) reader->lengthInSamples / reader->sampleRate;
            text << "\n  recording: take " << juce::String (length, 3) << " s of " << juce::String (seconds, 0) << " s played, "
                 << (int) reader->numChannels << " ch @ " << (int) reader->sampleRate << " Hz, peak "
                 << juce::String (juce::Decibels::gainToDecibels (peak, -150.0f), 1) << " dBFS -> " << target.getFullPathName() << "\n";
        }
        else
            text << "\n  recording: take unreadable (" << target.getFullPathName() << ")\n";
        return text;
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

    // LYNX.exe --export project.(flp|lynx) output.(wav|flac|ogg|mp3) [--pattern] [--insert N]
    // Same path as "Export" in the UI: the whole song (or the current pattern) with its tail.
    void runExport (const juce::String& inputPath, const juce::String& outputPath, bool patternOnly, int insert)
    {
        juce::String error, report;
        bool ok = false;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        lynx::loadtrace::begin (inputPath.toStdString()); // phase times (round 112): report lines + load.log
        {
            lynx::AudioEngine loader (false);
            const juce::File input (inputPath);

            // --find-plugins: first the targeted scan opening the project in the app runs (its plugins not known yet)
            if (input.hasFileExtension ("flp") && juce::JUCEApplication::getCommandLineParameters().contains ("--find-plugins"))
            {
                juce::MemoryBlock data;
                if (input.loadFileAsData (data))
                    try
                    {
                        const auto uses = lynx::flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }));
                        auto& host = loader.getPluginHost();
                        const auto listed = std::chrono::steady_clock::now();
                        const auto files = host.filesToScanFor (uses);
                        lynx::loadtrace::add ("plugin_scan_list", lynx::loadtrace::msSince (listed));
                        report << "scanned=" << files.joinIntoString (";") << "\n";
                        const auto scanned = std::chrono::steady_clock::now();
                        host.scanFiles (files, {});
                        lynx::loadtrace::add ("plugin_scan", lynx::loadtrace::msSince (scanned), files.size());
                    }
                    catch (const flp::Error&)
                    {
                    }
            }

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
                // .mp3: the Export window's MP3 (320 kbps); everything else as before
                auto options = settings.toOptions();
                if (output.hasFileExtension ("mp3"))
                {
                    options.format = lynx::exportopts::Format::mp3;
                    options.mp3Kbps = 320;
                }
                lynx::ExportJob job (insert > 0 ? lynx::makeBounceSong (loader.getSong(), insert) : loader.getSong(), options, output);
                const auto prepared = juce::Time::getMillisecondCounterHiRes();
                lynx::loadtrace::end();
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
                // third-party plugins: how each was found, or what plays in its place (diagnostics for level differences)
                for (const auto& line : loader.getMatchedPlugins())
                    report << "plugin_found=" << line << "\n";
                for (const auto& m : loader.getMissingPluginDetails())
                    report << "plugin_missing=" << juce::String::fromUTF8 (m.name.c_str()) << " -> "
                           << juce::String::fromUTF8 (m.replacement.c_str()).replace ("{0}", juce::String::fromUTF8 (m.replacementArg.c_str())) << "\n";
                {
                    const auto& song = loader.getSong();
                    for (std::size_t t = 0; t < song.mixer.size(); ++t)
                    {
                        const auto& track = song.mixer[t];
                        if (track.effects.empty() && track.sends.empty())
                            continue;
                        report << "insert " << (int) t << " vol=" << juce::String (track.volume / lynx::unityFader, 3)
                               << (track.muted ? " muted" : "") << (track.toMaster ? "" : " noMaster");
                        for (const auto& s : track.sends)
                            report << " send>" << s.target << "@" << juce::String (s.level, 3);
                        report << " |";
                        for (const auto& fx : track.effects)
                            report << " [" << (fx.kind == lynx::EffectKind::plugin ? juce::String::fromUTF8 (fx.plugin.name.c_str())
                                                                                 : juce::String::fromUTF8 (lynx::effectInfo (fx.kind).name))
                                   << (fx.isSubstitute() ? " (substitute)" : "") << (fx.enabled ? "" : " off")
                                   << (fx.mix < 0.999f ? " mix=" + juce::String (fx.mix, 2) : juce::String()) << "]";
                        report << "\n";
                    }
                }
                report << juce::String (lynx::loadtrace::reportLines());
                lynx::PluginHost::settingsFolder().getChildFile ("load.log").replaceWithText (juce::String (lynx::loadtrace::table()));
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
    void runPluginEditorSnapshots (const juce::String& folder)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        {
            const juce::File out (folder);
            out.createDirectory();
            lynx::AudioEngine offline (false);
            const auto roots = lynx::findFlStudioRoots();
            const auto presets = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin presets/Effects");

            auto kinds = lynx::plugui::flLayoutKinds();
            kinds.push_back (lynx::EffectKind::maximus); // Soundgoodizer (below)
            for (const auto own : lynx::plugui::ownLayoutKinds()) // LYNX's own panels (mixing suite, round 96)
                kinds.push_back (own);
            kinds.push_back (lynx::EffectKind::apexLimiter); // round 114: display-first editor
            const auto database = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin database/Effects");

            auto maximusPasses = 0; // Maximus has its own page (first pass); the second pass shows Soundgoodizer
            for (const auto kind : kinds)
            {
                auto fx = lynx::makeEffectSlot (kind);
                juce::String name (lynx::effectInfo (kind).name);
                for (const auto& plugin : lynx::flPluginsForEffect (kind))
                {
                    const auto dir = presets.getChildFile (juce::String::fromUTF8 (plugin.c_str()));
                    auto file = dir.getChildFile ("Default.fst");
                    if (! file.existsAsFile()) // plugins without presets: their entry of FL's plugin database
                        for (const auto& f : database.findChildFiles (juce::File::findFiles, true, juce::String::fromUTF8 (plugin.c_str()) + ".fst"))
                            file = f;
                    if (! file.existsAsFile())
                    {
                        auto files = dir.findChildFiles (juce::File::findFiles, true, "*.fst");
                        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
                        file = files.isEmpty() ? juce::File() : files[0];
                    }
                    juce::MemoryBlock data;
                    if (file.existsAsFile() && file.loadFileAsData (data))
                    {
                        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                        if (auto loaded = lynx::effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), 130.0); loaded && loaded->kind == kind)
                            fx = *loaded;
                    }
                    name = juce::String::fromUTF8 (plugin.c_str());
                    break;
                }

                if (kind == lynx::EffectKind::maximus && ++maximusPasses == 2) // Soundgoodizer: preset A of the installation, Amount 60 %
                {
                    juce::MemoryBlock data;
                    juce::File (juce::String::fromUTF8 (roots.factoryData.c_str())).getChildFile ("Plugins/Fruity/Effects/Soundgoodizer/Data/Presets/a.dat").loadFileAsData (data);
                    lynx::maximusfx::Blob blob;
                    blob.build = lynx::hlfx::MaximusBuild::soundgoodizer;
                    blob.mix = 600;
                    const auto* b = static_cast<const std::uint8_t*> (data.getData());
                    blob.state.assign (b, b + data.getSize());
                    fx = lynx::makeEffectSlot (kind);
                    fx.state = lynx::maximusfx::pack (blob);
                    name = "Soundgoodizer";
                }

                if (kind == lynx::EffectKind::ridgeStrip) // a vocal setting, so the graphs show something
                    for (const auto& [param, value] : { std::pair<const char*, float> { "HPF", 0.3f }, { "LMF gain", 0.35f }, { "HF gain", 0.7f },
                                                        { "Ratio", 0.3f }, { "Comp. threshold", 0.55f } })
                        fx.params[(std::size_t) lynx::plugui::paramIndex (kind, param)] = value;
                if (kind == lynx::EffectKind::apexLimiter) // round 114: gain into the ceiling, so the display shows reduction
                    fx.params[0] = 0.33f;
                if (kind == lynx::EffectKind::ember)
                {
                    fx.params[(std::size_t) lynx::plugui::paramIndex (kind, "Drive")] = 0.5f;
                    fx.params[(std::size_t) lynx::plugui::paramIndex (kind, "Bands")] = 1.0f; // round 108: the band rows
                    fx.params[(std::size_t) lynx::plugui::paramIndex (kind, "B3 drive")] = 0.6f;
                }
                if (kind == lynx::EffectKind::crestCompressor) // round 108: a curve with a knee and a side-chain low cut
                    for (const auto& [param, value] : { std::pair<const char*, float> { "Threshold", 0.55f }, { "Knee", 0.15f },
                                                        { "SC1 shape", 0.5f }, { "SC1 freq.", 0.3f } })
                        fx.params[(std::size_t) lynx::plugui::paramIndex (kind, param)] = value;

                lynx::Song song;
                song.mixer.resize (2);
                song.mixer[0].name = "Master";
                song.mixer[1].name = "Insert 1";
                song.mixer[1].effects.push_back (fx);
                offline.setSong (std::move (song));
                offline.dispatchPendingMessages();

                if (lynx::ProEditorWindows::handles (kind)) // round 114: the display-first editors, with a demo signal
                {
                    auto editor = lynx::makeProEditor (offline, 1, 0, true);
                    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                    {
                        juce::Graphics g (image);
                        editor->paintEntireComponent (g, false);
                    }
                    const auto file = out.getChildFile (name + ".png");
                    file.deleteFile();
                    juce::PNGImageFormat png;
                    if (auto stream = file.createOutputStream())
                        png.writeImageToStream (image, *stream);
                    continue;
                }

                const auto pages = name == "Soundgoodizer" ? 1 : juce::jmax (1, (int) lynx::plugui::layoutFor (kind).pages.size());
                for (int page = 0; page < pages; ++page)
                {
                    auto editor = lynx::makeNativeEffectEditor (offline, 1, 0, 1.0f, page);
                    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                    {
                        juce::Graphics g (image);
                        g.fillAll (lynx::theme::panel);
                        editor->paintEntireComponent (g, false);
                    }
                    const auto file = out.getChildFile (name + (page > 0 ? "-p" + juce::String (page + 1) : juce::String()) + ".png");
                    file.deleteFile();
                    juce::PNGImageFormat png;
                    if (auto stream = file.createOutputStream())
                        png.writeImageToStream (image, *stream);
                }
            }

            // LYNX's own editors with live displays (round 103): Facet EQ (a mixing curve, one dynamic band, demo analyser)
            // and LYNX Tune (demo pitch phrase)
            {
                lynx::setNativeEditorDemo (true);
                using namespace lynx::facet;
                auto eq = lynx::makeEffectSlot (lynx::EffectKind::facetEq);
                const struct { Shape s; double f, g, q; int slope; double range; } bandsDemo[] = {
                    { Shape::lowCut, 32, 0, 1, 24, 0 }, { Shape::bell, 110, 3, 1.2, 12, 0 }, { Shape::bell, 380, -3.5, 1.6, 12, 0 },
                    { Shape::bell, 3200, 0, 2.5, 12, -6 }, { Shape::highShelf, 9000, 2.5, 0.8, 12, 0 }
                };
                for (int b = 0; b < 5; ++b)
                {
                    eq.params[(std::size_t) param (b, 0)] = shapeValue (bandsDemo[b].s);
                    eq.params[(std::size_t) param (b, 1)] = hzValue (bandsDemo[b].f);
                    eq.params[(std::size_t) param (b, 2)] = gainValue (bandsDemo[b].g);
                    eq.params[(std::size_t) param (b, 3)] = qValue (bandsDemo[b].q);
                    eq.params[(std::size_t) param (b, 4)] = slopeValue (bandsDemo[b].slope);
                    eq.params[(std::size_t) (rangeParam0 + b)] = rangeValue (bandsDemo[b].range);
                }
                eq.params[(std::size_t) phaseParam] = phaseValue (Phase::natural);
                const auto own = [&] (const lynx::EffectSlot& fx, const juce::String& file, bool facetEditor) {
                    lynx::Song song;
                    song.mixer.resize (2);
                    song.mixer[0].name = "Master";
                    song.mixer[1].name = "Vocal";
                    song.mixer[1].effects.push_back (fx);
                    offline.setSong (std::move (song));
                    offline.dispatchPendingMessages();
                    auto editor = facetEditor ? lynx::makeFacetEqEditor (offline, 1, 0, true) : lynx::makeNativeEffectEditor (offline, 1, 0, 1.25f, 0);
                    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                    {
                        juce::Graphics g (image);
                        g.fillAll (lynx::theme::panel);
                        editor->paintEntireComponent (g, false);
                    }
                    const auto f = out.getChildFile (file);
                    f.deleteFile();
                    juce::PNGImageFormat png;
                    if (auto stream = f.createOutputStream())
                        png.writeImageToStream (image, *stream);
                };
                own (eq, "Facet EQ.png", true);
                own (lynx::makeEffectSlot (lynx::EffectKind::lynxTune), "LYNX Tune.png", false);
                lynx::setNativeEditorDemo (false);
            }

            // generators in FL's arrangement: the channel's plugin page with the FL preset's settings
            const auto generators = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin presets/Generators");
            for (auto [folder, preset] : std::vector<std::pair<const char*, const char*>> { { "3x Osc", "Default.fst" }, { "FL Keys", "Grand Piano.fst" }, { "BooBass", "" }, { "Sytrus", "Default.fst" }, { "Harmor", "Default.fst" } })
            {
                juce::MemoryBlock data;
                if (juce::String (folder) == "Harmor") // the first preset whose state the engine reads
                {
                    auto files = generators.getChildFile (folder).findChildFiles (juce::File::findFiles, true, "*.fst");
                    files.sort();
                    for (const auto& f : files)
                    {
                        juce::MemoryBlock block;
                        if (! f.loadFileAsData (block))
                            continue;
                        const auto* b = static_cast<const std::uint8_t*> (block.getData());
                        try
                        {
                            const auto p = flp::parseProject (std::vector<std::uint8_t> (b, b + block.getSize()), true);
                            if (! p.channels.empty() && lynx::harmorStateUsable (p.channels.front().pluginState))
                            {
                                preset = nullptr;
                                data = block;
                                break;
                            }
                        }
                        catch (const std::exception&)
                        {
                        }
                    }
                }
                auto file = preset == nullptr ? juce::File() : juce::String (preset).isEmpty() ? database.getSiblingFile ("Generators").getChildFile ("Misc").getChildFile (juce::String (folder) + ".fst")
                                                            : generators.getChildFile (folder).getChildFile (preset);
                if (! file.existsAsFile())
                {
                    auto files = generators.getChildFile (folder).findChildFiles (juce::File::findFiles, true, "*.fst");
                    files.sort();
                    file = files.isEmpty() ? juce::File() : files[0];
                }
                if (data.isEmpty() && ! file.loadFileAsData (data))
                    continue;
                try
                {
                    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                    auto project = flp::parseProject (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), true);
                    if (project.channels.empty())
                        continue;
                    project.channels.front().type = flp::ChannelType::native;
                    lynx::FlpImportOptions options;
                    options.roots = roots;
                    options.fileExists = [] (const std::string& path) { return lynx::platform::localFile (path).existsAsFile(); };
                    options.readFile = [] (const std::string& path) {
                        juce::MemoryBlock block;
                        lynx::platform::localFile (path).loadFileAsData (block);
                        const auto* b = static_cast<const std::uint8_t*> (block.getData());
                        return std::vector<std::uint8_t> (b, b + block.getSize());
                    };
                    options.saveEmbedded = [out] (const std::vector<std::uint8_t>& wav, const std::string& name) {
                        const auto f = out.getChildFile ("samples").getNonexistentChildFile (juce::String::fromUTF8 (name.c_str()), ".wav");
                        f.getParentDirectory().createDirectory();
                        f.replaceWithData (wav.data(), wav.size());
                        return f.getFullPathName().toStdString();
                    };
                    auto song = lynx::importFlp (project, options);
                    // the page only needs the plugin's saved state (FL Keys' sample zones need the installed wavetables)
                    if (! song.channels.empty() && project.channels.front().pluginName == "BooBass" && song.channels.front().instrument != lynx::InstrumentKind::boobass)
                    {
                        song.channels.front().instrument = lynx::InstrumentKind::boobass; // (its sound needs the engine DLL; the page does not)
                        song.channels.front().modelParams.assign (3, 32768.0f);
                    }
                    if (! song.channels.empty() && project.channels.front().pluginName == "FL Keys")
                    {
                        song.channels.front().instrument = lynx::InstrumentKind::flkeys;
                        song.channels.front().pluginState = project.channels.front().pluginState;
                    }
                    offline.setSong (std::move (song));
                    offline.dispatchPendingMessages();
                    if (offline.getSong().channels.empty())
                        continue;
                    auto panel = lynx::makeChannelSettingsPanel (offline, 0);
                    juce::Image image (juce::Image::ARGB, panel->getWidth(), panel->getHeight(), true);
                    {
                        juce::Graphics g (image);
                        g.fillAll (lynx::theme::panel);
                        panel->paintEntireComponent (g, false);
                    }
                    const auto out2 = out.getChildFile (juce::String (folder) + ".png");
                    out2.deleteFile();
                    juce::PNGImageFormat png;
                    if (auto stream = out2.createOutputStream())
                        png.writeImageToStream (image, *stream);
                }
                catch (const std::exception&)
                {
                }
            }
        }
        quit();
    }

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

                    if (w == 1920) // the rack scrolled to its last steps: the controls column stays in view (round 90)
                    {
                        ui.scrollRackForSnapshot (100000);
                        offline.dispatchPendingMessages();
                        saveSnapshot (ui, prefix + "-desktop-rack-scrolled.png");
                        ui.scrollRackForSnapshot (0);
                    }
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
                {
                    // the toolbar's edit mode (round 115): outlined panels and the tray of panels under it
                    ui.setToolbarLayoutForSnapshot (lynx::toolbarlayout::Layout::makeDefault());
                    auto layout = lynx::toolbarlayout::Layout::makeDefault();
                    layout.setShown (lynx::toolbarlayout::Group::meters, false);
                    ui.setToolbarLayoutForSnapshot (layout);
                    ui.setToolbarEditingForSnapshot (true);
                    offline.dispatchPendingMessages();
                    saveArea (ui, ui.getToolbarBoundsForSnapshot().withHeight (ui.getToolbarAndTrayHeightForSnapshot()), prefix + "-toolbar-edit-1600.png");
                    ui.setToolbarEditingForSnapshot (false);
                    // Ctrl+F11: only the menu and the hint bar
                    layout = lynx::toolbarlayout::Layout::makeDefault();
                    layout.visible = false;
                    ui.setToolbarLayoutForSnapshot (layout);
                    saveArea (ui, ui.getLocalBounds().withHeight (120), prefix + "-toolbar-hidden-1600.png");
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
            {
                // a pattern with piano-roll notes: those rows show a miniature instead of steps (round 90)
                int found = -1;
                const auto before = offline.getSong().currentPattern;
                {
                    const auto& song = offline.getSong();
                    for (int p = 0; p < (int) song.patterns.size() && found < 0; ++p)
                        for (int c = 0; c < (int) song.channels.size() && found < 0; ++c)
                            if (lynx::showsNotePreview (song.patterns[(std::size_t) p], c))
                                found = p;
                }
                if (found >= 0)
                {
                    offline.editSong ([found] (lynx::Song& s) { s.currentPattern = found; }, false);
                    offline.dispatchPendingMessages();
                    saveSnapshot (ui, prefix + "-rack-preview.png");
                    offline.editSong ([before] (lynx::Song& s) { s.currentPattern = before; }, false);
                    offline.dispatchPendingMessages();
                }
            }
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
            // round 104: a folded track group (FL's thin member rows) and the Picker's audio tab
            {
                int header = -1;
                for (int t = 0; t < (int) offline.getSong().playlistTracks.size() && header < 0; ++t)
                    if (lynx::noteedit::isGroupHeader (offline.getSong(), t))
                        header = t;
                if (header >= 0)
                {
                    offline.editSong ([header] (lynx::Song& s) { lynx::noteedit::setGroupCollapsed (s, header, true); }, false);
                    offline.dispatchPendingMessages();
                    saveSnapshot (ui, prefix + "-playlist-group-folded.png");
                    offline.editSong ([header] (lynx::Song& s) { lynx::noteedit::setGroupCollapsed (s, header, false); }, false);
                }
                ui.getPickerForSnapshot().setView (1);
                offline.dispatchPendingMessages();
                saveSnapshot (ui, prefix + "-playlist-picker-audio.png");
                ui.getPickerForSnapshot().setView (0);
            }
            // round 104: the Channel Rack filtered to its first filter group
            if (! offline.getSong().channelGroups.empty())
            {
                ui.selectTabForSnapshot (0);
                ui.getRackForSnapshot().setFilterGroup (0);
                offline.dispatchPendingMessages();
                saveSnapshot (ui, prefix + "-rack-filter.png");
                ui.getRackForSnapshot().setFilterGroup (-1);
            }
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
            saveWindow (lynx::makeExportPanel (offline, 1), "-export-mp3.png");
            saveWindow (lynx::makeExportPanel (offline, 3), "-export-flac-stems.png");
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
            saveAlert (lynx::makeRecordChoiceDialog(), "-record-dialog.png");
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
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::general), "-settings-general.png");
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::audio), "-settings-audio.png");
                saveWindow (lynx::makeSettingsPanel (offline, lynx::SettingsPage::midi), "-settings-midi.png");
                saveWindow (lynx::makePluginManagerPanel (offline, false), "-plugin-manager.png");
                saveWindow (lynx::makeShortcutManagerPanel (false), "-shortcuts.png");
            }
            {
                // round 84: New project, Clip properties, Arrangements, Plugin performance monitor, Touch keyboard
                saveWindow (lynx::makeNewProjectPanel(), "-new-project.png");
                const auto& clips = offline.getSong().playlist;
                for (int i = 0; i < (int) clips.size(); ++i)
                    if (clips[(std::size_t) i].kind == lynx::ClipKind::pattern)
                    {
                        saveWindow (lynx::makeClipPropertiesPanel (offline, i, { i, (i + 1) % (int) clips.size() }), "-clip-properties.png");
                        break;
                    }
                for (int i = 0; i < (int) clips.size(); ++i)
                    if (clips[(std::size_t) i].kind == lynx::ClipKind::audio)
                    {
                        saveWindow (lynx::makeClipPropertiesPanel (offline, i, { i }), "-clip-properties-audio.png");
                        break;
                    }
                if (offline.getSong().arrangements.size() < 2)
                    offline.editSong ([] (lynx::Song& s) {
                        const auto current = s.currentArrangement;
                        s.addArrangement ("Radio edit", true);
                        s.addArrangement ("Intro loop");
                        s.selectArrangement (current);
                    }, false);
                saveWindow (lynx::makeArrangementsPanel (offline), "-arrangements.png");
                saveWindow (lynx::makePluginMonitorPanel (offline), "-plugin-monitor.png");
                saveWindow (lynx::makeTouchKeyboardPanel (offline), "-touch-keyboard.png");
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
                                                                         { lynx::NoteTool::limit, "-tool-limit.png" },
                                                                         { lynx::NoteTool::quantize, "-tool-quantize.png" },
                                                                         { lynx::NoteTool::chop, "-tool-chop.png" },
                                                                         { lynx::NoteTool::flam, "-tool-flam.png" },
                                                                         { lynx::NoteTool::lfo, "-tool-lfo.png" },
                                                                         { lynx::NoteTool::claw, "-tool-claw.png" },
                                                                         { lynx::NoteTool::articulate, "-tool-articulate.png" },
                                                                         { lynx::NoteTool::riffMachine, "-tool-riff.png" },
                                                                         { lynx::NoteTool::chordProgression, "-tool-chords.png" } };
                for (const auto& [tool, name] : tools)
                    saveWindow (lynx::makeNoteToolPanel (offline, tool, roll.noteIndicesForSnapshot(), roll.gridForSnapshot()), name);
            }

            // round 83: the event editor, with sample events on the selected channel's volume (removed afterwards)
            {
                const lynx::AutomationTarget volume { lynx::AutomationTargetKind::channelVolume, offline.getSelectedChannel() };
                const auto before = offline.getSong().patterns[(std::size_t) offline.getSong().currentPattern].events;
                offline.editSong ([&] (lynx::Song& s) {
                    auto& lane = lynx::eventedit::laneFor (s.patterns[(std::size_t) s.currentPattern], volume);
                    lynx::eventedit::line (lane, 0, lynx::ppq * 2, 0.2f, 0.9f, lynx::ticksPerStep);
                    lynx::prtools::LfoOptions lfo;
                    lfo.startSpeed = 7 * 4096; // one beat
                    lynx::eventedit::applyLfo (lane, lynx::ppq * 2, lynx::ppq * 4, lfo, lynx::ticksPerStep);
                }, false);
                saveWindow (lynx::makeEventEditor (offline, volume), "-eventeditor.png");
                offline.editSong ([&] (lynx::Song& s) { s.patterns[(std::size_t) s.currentPattern].events = before; }, false);
            }

            // round 86: the audio editor with a synthetic drum loop (markers, a region, a loop), waveform and spectrogram,
            // and its menu items (-audioeditor-menu.txt)
            {
                namespace ae = lynx::audioedit;
                ae::Document d;
                d.title = "Drum loop";
                d.audio.sampleRate = 44100.0;
                const auto frames = (std::size_t) (44100 * 2);
                d.audio.channels.assign (2, std::vector<float> (frames, 0.0f));
                std::uint32_t seed = 7;
                for (int hit = 0; hit < 8; ++hit)
                {
                    const auto at = (std::size_t) hit * frames / 8;
                    const auto kick = hit % 2 == 0;
                    double phase = 0.0;
                    for (std::size_t i = 0; i < 12000 && at + i < frames; ++i)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        const auto noise = (float) (seed >> 8) / 16777216.0f - 0.5f;
                        const auto t = (double) i / 44100.0;
                        phase += 2.0 * 3.141592653589793 * (kick ? 45.0 + 160.0 * std::exp (-t * 30.0) : 190.0) / 44100.0;
                        const auto body = (float) std::sin (phase) * (float) std::exp (-t * (kick ? 9.0 : 22.0));
                        const auto v = kick ? 0.85f * body : 0.35f * body + 0.6f * noise * (float) std::exp (-t * 18.0);
                        d.audio.channels[0][at + i] += v;
                        d.audio.channels[1][at + i] += kick ? v : v * 0.8f + 0.1f * noise * (float) std::exp (-t * 30.0);
                    }
                }
                ae::addMarker (d, (std::int64_t) frames / 8, "Snare");
                ae::addRegion (d, { (std::int64_t) frames / 2, (std::int64_t) frames * 3 / 4 }, "Fill");
                ae::setLoop (d, { 0, (std::int64_t) frames });
                auto wave = lynx::makeAudioEditorForSnapshot (offline, d, false);
                juce::File (prefix + "-audioeditor-menu.txt").replaceWithText (lynx::audioEditorMenuForSnapshot (*wave).joinIntoString ("\n") + "\n");
                saveWindow (std::move (wave), "-audioeditor.png");
                saveWindow (lynx::makeAudioEditorForSnapshot (offline, d, true), "-audioeditor-spectrum.png");
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
                // the armed insert with an audio input (its input row in the side panel, the level bar on the strip)
                offline.editSong ([] (lynx::Song& s) { s.mixer[3].inputChannel = 0; });
                ui.getMixerForSnapshot().selectTrack (3);
                offline.dispatchPendingMessages();
                saveSnapshot (ui, prefix + "-mixer-input.png");
                offline.undo();
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
            // the Browser's PROJECT tab: the patterns, generators and audio files of the project in their colours
            ui.getBrowserForSnapshot().showProjectForSnapshot();
            offline.dispatchPendingMessages();
            saveSnapshot (ui, prefix + "-browser-project.png");
            ui.getBrowserForSnapshot().setTab (lynx::BrowserComponent::allTab);

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
    std::unique_ptr<juce::XmlElement> savedDeviceState; // --rtstress: the user's device before this run's --type/--buffer

    // Hidden test mode --asiodll "<driver.dll>[|{CLSID}]" (REA round 97): an ASIO driver loaded straight from its file,
    // without registering it, listed as the driver type "ASIO driver file". True when one was added.
    bool addDriverFileDeviceType()
    {
        const auto args = juce::JUCEApplication::getCommandLineParameterArray();
        const auto i = args.indexOf ("--asiodll");
        if (i < 0 || i + 1 >= args.size() || engine == nullptr)
            return false;
        auto& devices = engine->getDeviceManager();
        devices.getAvailableDeviceTypes(); // creates the standard types first
        for (auto* t : devices.getAvailableDeviceTypes())
            if (t->getTypeName() == lynx::dllAsioTypeName)
                return true;
        auto type = lynx::makeDllAsioDeviceType (args[i + 1].unquoted());
        if (type == nullptr)
            return false;
        devices.addAudioDeviceType (std::move (type));
        return true;
    }
    void restoreSavedDeviceState()
    {
        if (engine != nullptr)
            engine->restoreAudioDeviceState (savedDeviceState.get());
    }
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<lynx::SplashScreen> splash;
    double splashDuration = lynx::SplashScreen::minimumDuration; // seconds of its clock (round 121: from the process start)
    lynx::StartupOptions startupOptions;
    juce::File fileToOpen;
};

START_JUCE_APPLICATION (LynxApplication)
