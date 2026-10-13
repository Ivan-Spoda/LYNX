#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Effects.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "engine/MidiFileIo.h"
#include "engine/ProjectFile.h"
#include "engine/Text.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/ExportDialog.h"
#include "ui/MainComponent.h"
#include "ui/ParametricEqEditor.h"
#include "ui/Theme.h"

namespace
{
lynx::AudioEngine* crashEngine = nullptr;

// Travou: tenta salvar o projeto e grava o backtrace (como o "emergency save" do FL).
void onCrash (void*)
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave");
    folder.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    folder.getChildFile ("travamento " + stamp + ".txt").replaceWithText (juce::SystemStats::getStackBacktrace());

    if (crashEngine != nullptr)
    {
        juce::String error;
        lynx::ProjectFile::save (crashEngine->getSong(), folder.getChildFile ("recuperado " + stamp + lynx::ProjectFile::extension), error);
    }
}
} // namespace

class LynxApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // a busca de plugins roda cópias do LYNX como processos filhos
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);

        if (args.contains ("--selftest"))
        {
            const auto i = args.indexOf ("--selftest");
            runSelfTest (args[i + 1].unquoted(), args[i + 2].unquoted());
            return;
        }

        if (args.contains ("--scan-one"))
        {
            // processo filho da busca de plugins (ver PluginHost::scan)
            const auto i = args.indexOf ("--scan-one");
            lynx::PluginHost::scanOneFile (args[i + 1].unquoted(), args[i + 2].unquoted(), juce::File (args[i + 3].unquoted()));
            setApplicationReturnValue (0);
            quit();
            return;
        }

        if (args.contains ("--paths"))
        {
            // diagnóstico: pastas que o LYNX procura
            const auto programs = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
            juce::String report;
            report << "programas=" << programs.getFullPathName() << "\n";
            for (const auto& dir : programs.getChildFile ("Image-Line").findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
                report << "fl=" << dir.getFullPathName() << " packs=" << (int) dir.getChildFile ("Data").getChildFile ("Patches").getChildFile ("Packs").isDirectory() << "\n";
            juce::File (args[args.indexOf ("--paths") + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--dispositivos"))
        {
            // diagnóstico: tipos de driver e dispositivos de saída que o JUCE oferece (sem abrir nenhum)
            juce::AudioDeviceManager manager;
            juce::String report;

            for (auto* type : manager.getAvailableDeviceTypes())
            {
                type->scanForDevices();
                report << "tipo=" << type->getTypeName() << "\n";
                for (const auto& name : type->getDeviceNames (false))
                    report << "  saida=" << name << "\n";
            }

            juce::File (args[args.indexOf ("--dispositivos") + 1].unquoted()).replaceWithText (report);
            quit();
            return;
        }

        if (args.contains ("--scantest"))
        {
            // LYNX.exe --scantest pasta relatorio.txt: busca (em processos filhos) sem tocar na lista do usuário
            const auto i = args.indexOf ("--scantest");
            lynx::PluginHost host (false);
            const auto found = host.scan (juce::FileSearchPath (args[i + 1].unquoted()), {});
            juce::String report;
            report << "encontrados=" << found << "\n";

            for (const auto& t : host.getKnownPlugins().getTypes())
                report << "  " << (t.isInstrument ? "instrumento: " : "efeito: ") << t.name << "\n";

            for (const auto& f : host.getKnownPlugins().getBlacklistedFiles())
                report << "  lista negra: " << f << "\n";

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
            // LYNX.exe --stems projeto base.wav: um WAV por insert com som (como o "Exportar stems")
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
                        report << file.getFileName() << " pico=" << job.getPeak() << "\n";
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
        engine = std::make_unique<lynx::AudioEngine>();
        crashEngine = engine.get();
        juce::SystemStats::setApplicationCrashHandler (onCrash);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);

        // Duplo clique num .lynx/.flp abre o LYNX com o arquivo como argumento.
        if (args.size() > 0 && juce::File::isAbsolutePath (args[0].unquoted()))
            mainWindow->openFile (juce::File (args[0].unquoted()));
    }

    void shutdown() override
    {
        mainWindow = nullptr; // a UI sai antes do engine
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
    // LYNX.exe --selftest relatorio.txt: renderiza 2 s sem placa de som e grava o resultado.
    // Código de saída 0 = saiu áudio, 1 = silêncio.
    void runSelfTest (const juce::String& reportPath, const juce::String& demoProjectPath)
    {
        lynx::AudioEngine offline (false);
        const auto peak = offline.renderOffline (2.0, 48000.0);
        const auto audioOk = peak > 0.01f && peak < 10.0f;

        // ida e volta do formato de projeto, com acentos no nome
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

        if (demoProjectPath.isNotEmpty()) // projeto de exemplo para conferir a UI com --snapshot
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
        // ida e volta dos recursos novos: synth/sampler com modulação, EQs, utilitário, marcadores
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
                            && richBack->markers() == rich.markers();

        // desfazer/refazer
        offline.editSong ([] (lynx::Song& s) { s.bpm = 99.0; });
        offline.editSong ([] (lynx::Song& s) { s.bpm = 101.0; });
        offline.undo();
        const auto afterUndo = offline.getSong().bpm;
        offline.redo();
        const auto undoOk = afterUndo == 99.0 && offline.getSong().bpm == 101.0 && offline.canUndo() && ! offline.canRedo()
                            && offline.isModified();

        // gravação pelo teclado do PC: uma nota de um tempo entra na pattern, num passo de desfazer
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

        // MIDI: exportar a pattern e importar de volta dá as mesmas notas
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

        const auto ok = audioOk && projectOk && richOk && undoOk && recordOk && recordUndoOk && midiOk;

        juce::String report;
        report << "conexoes=" << offline.getNumGraphConnections() << "\n"
               << "pico=" << peak << "\n"
               << "projeto=" << (projectOk ? "OK" : "ERRO " + error) << "\n"
               << "projeto_recursos=" << (richOk ? "OK" : "ERRO") << "\n"
               << "desfazer=" << (undoOk ? "OK" : "ERRO") << "\n"
               << "gravacao=" << (recordOk && recordUndoOk ? "OK" : "ERRO") << "\n"
               << "midi=" << (midiOk ? "OK" : "ERRO") << "\n"
               << "resultado=" << (ok ? "OK" : "FALHOU") << "\n";

        if (reportPath.isNotEmpty())
            juce::File (reportPath).replaceWithText (report);

        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --plugintest plugin.vst3 relatorio.txt: carrega o plugin, toca notas se for
    // instrumento (e passa o som por ele se for efeito) e mede o áudio.
    void runPluginTest (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        {
            lynx::AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            const auto added = host.addPluginFile (juce::File (pluginPath));
            report << "plugins_no_arquivo=" << added << "\n";

            juce::PluginDescription instrument, effect;
            bool hasInstrument = false, hasEffect = false;

            for (const auto& type : host.getKnownPlugins().getTypes())
            {
                if (juce::File (type.fileOrIdentifier).getFileName() != juce::File (pluginPath).getFileName())
                    continue;

                report << "  " << (type.isInstrument ? "instrumento: " : "efeito: ") << type.name << " (" << type.manufacturerName
                       << ") em " << type.fileOrIdentifier << "\n";

                if (type.isInstrument && ! hasInstrument) { instrument = type; hasInstrument = true; }
                if (! type.isInstrument && ! hasEffect) { effect = type; hasEffect = true; }
            }

            auto song = lynx::Song::makeDefault();

            if (hasInstrument)
            {
                auto& c = song.channels[4]; // o canal "Synth" vira o plugin
                c.instrument = lynx::InstrumentKind::plugin;
                c.plugin.id = instrument.createIdentifierString().toStdString();
                c.plugin.name = instrument.name.toStdString();
                c.volume = 1.0f;

                for (int i = 0; i < 4; ++i) // um acorde por tempo
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
            report << "instrumento_carregado=" << (loadedInstrument != nullptr ? "sim" : "nao") << "\n"
                   << "efeito_carregado=" << (loadedEffect != nullptr ? "sim" : "nao") << "\n";

            const auto peak = offline.renderOffline (3.0, 48000.0);
            report << "pico=" << peak << "\n";

            offline.capturePluginStates();
            const auto stateSize = hasInstrument ? offline.getSong().channels[4].plugin.state.size() : (size_t) 0;
            report << "estado_bytes_base64=" << (int) stateSize << "\n";

            ok = (hasInstrument || hasEffect) && (! hasInstrument || loadedInstrument != nullptr)
                 && (! hasEffect || loadedEffect != nullptr) && peak > 0.001f && peak < 10.0f;
        }

        report << "resultado=" << (ok ? "OK" : "FALHOU") << "\n";
        juce::File (reportPath).replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --wrappertest plugin.vst3 relatorio.txt: monta o estado de um VST3 como o Fruity
    // Wrapper do FL guarda (REA_rodada5 §3.2), passa pelo import (adoptFlPlugins) e confere se o
    // plugin volta com o parâmetro mudado.
    void runWrapperTest (const juce::String& pluginPath, const juce::String& reportPath)
    {
        juce::String report;
        bool ok = false;
        {
            lynx::AudioEngine offline (false);
            auto& host = offline.getPluginHost();
            host.addPluginFile (juce::File (pluginPath));
            const juce::PluginDescription* vst3 = nullptr;

            for (const auto& type : host.getKnownPlugins().getTypes())
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
                    report << "parametro=" << parameter->getName (64) << " alvo=" << target << "\n";

                    // estado do JUCE -> contêiner do FL: u32 1 + registros 1 (flags), 3 (componente), 2 (controlador)
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

                    if (auto xml = juce::AudioProcessor::getXmlFromBinary (juceState.getData(), (int) juceState.getSize()))
                        for (auto [tag, id] : { std::pair { "IComponent", 3u }, { "IEditController", 2u } })
                            if (auto* child = xml->getChildByName (tag))
                            {
                                juce::MemoryBlock data;
                                data.fromBase64Encoding (child->getAllSubText());
                                record (id, data);
                                report << tag << "_bytes=" << (int) data.getSize() << "\n";
                            }

                    plugin.reset();

                    auto song = lynx::Song::makeDefault();
                    auto& f = song.channels[4].plugin.foreign;
                    f.format = lynx::ForeignPlugin::Format::vst3;
                    f.name = vst3->name.toStdString(); // sem class ID: acha pelo nome (plano B)
                    f.state = flState;
                    const auto adopted = host.adoptFlPlugins (song);
                    report << "adotados=" << adopted << "\n";

                    offline.setSong (song);

                    if (auto* loaded = dynamic_cast<lynx::PluginInstrument*> (offline.getChannelInstrument (4).get()))
                    {
                        const auto value = loaded->getInstance().getParameters()[0]->getValue();
                        report << "valor_restaurado=" << value << "\n";
                        ok = adopted == 1 && std::abs (value - target) < 0.02f;
                    }
                }
            }
        }

        report << "resultado=" << (ok ? "OK" : "FALHOU") << "\n";
        juce::File (reportPath).replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --render projeto.(flp|lynx) saida.wav [segundos]
    // Exporta a pattern atual para WAV 24 bits (num .flp importado, a que tem mais notas).
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
                error = lynx::utf8 ("não foi possível criar ") + outputPath;
            }
            else
            {
                const auto peak = offline.renderOffline (seconds > 0.0 ? seconds : 10.0, 44100.0, writer.get());
                report << "pico=" << peak << "\n";
                ok = peak > 0.001f;
            }
        }

        report << "samples=" << samplersLoaded << "/" << samplers << "\n"
               << "resultado=" << (ok ? "OK" : "ERRO " + error) << "\n";
        juce::File (outputPath + ".txt").replaceWithText (report);

        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --export projeto.(flp|lynx) saida.(wav|flac|ogg) [--pattern] [--insert N]
    // O mesmo caminho do "Exportar" da interface: música inteira (ou a pattern atual) com cauda.
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
                // --insert N: só a saída daquele insert (o que o "Renderizar para áudio" grava)
                if (juce::JUCEApplication::getCommandLineParameters().contains ("--sem-efeitos")) // diagnóstico de desempenho
                    loader.editSong ([] (lynx::Song& s) {
                        for (auto& t : s.mixer)
                            t.effects.clear();
                    }, false);

                if (juce::JUCEApplication::getCommandLineParameters().contains ("--sem-automacao"))
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
                report << "convertidos=" << joined (importReport.converted) << "\n"
                       << "aproximados=" << joined (importReport.approximated) << "\n"
                       << "sem_equivalente=" << joined (importReport.unsupported) << "\n"
                       << "efeitos_fora=" << joined (importReport.droppedEffects) << "\n"
                       << "samples_faltando=" << importReport.missingSamples << "\n";
                report << "abrir_ms=" << juce::roundToInt (loaded - started) << "\n"
                       << "preparar_ms=" << juce::roundToInt (prepared - loaded) << "\n"
                       << "render_ms=" << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - prepared) << "\n"
                       << "grafo_ms=" << juce::roundToInt (job.getEngine().getLastRenderStats().graphMs) << "\n"
                       << "gravar_ms=" << juce::roundToInt (job.getEngine().getLastRenderStats().writeMs) << "\n"
                       << "musica_segundos=" << job.getMusicSeconds() << "\n"
                       << "pico=" << job.getPeak() << "\n"
                       << "progresso_final=" << lastPercent << "\n"
                       << "bytes=" << output.getSize() << "\n";
                ok = ok && job.getPeak() > 0.001f;
            }
        }

        report << "resultado=" << (ok ? "OK" : "ERRO " + error) << "\n";
        juce::File (outputPath + ".txt").replaceWithText (report);
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    // LYNX.exe --snapshot prefixo [projeto]: desenha a janela fora da tela e grava
    // prefixo-rack.png, -pianoroll.png e -playlist.png (para conferir a UI sem mexer na tela).
    void runSnapshot (const juce::String& prefix, const juce::String& projectPath)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        {
            lynx::AudioEngine offline (false);
            lynx::MainComponent ui (offline);
            ui.setSize (1280, 720);

            if (projectPath.isNotEmpty())
                ui.openFile (juce::File (projectPath));

            offline.dispatchPendingMessages(); // as notificações de mudança são assíncronas

            ui.selectTabForSnapshot (0);
            saveSnapshot (ui, prefix + "-rack.png");
            ui.selectTabForSnapshot (1);
            saveSnapshot (ui, prefix + "-pianoroll.png");
            ui.selectTabForSnapshot (2);
            saveSnapshot (ui, prefix + "-playlist.png");
            ui.selectTabForSnapshot (3);
            saveSnapshot (ui, prefix + "-mixer.png");

            // janelas avulsas, com fundo do tema
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

            // EQ paramétrico: o de curva mais marcada (no .flp importado, com os ajustes do FL)
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
        }
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        quit();
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
            content = new lynx::MainComponent (engine);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (800, 400, 10000, 10000);
            // ~88% da área útil da tela principal (mínimo o tamanho de projeto da interface)
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = display->userArea;
                setSize (juce::jmax (getWidth(), (int) (area.getWidth() * 0.88)), juce::jmax (getHeight(), (int) (area.getHeight() * 0.88)));
            }

            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void openFile (const juce::File& file) { content->openFile (file); }
        void confirmDiscard (std::function<void()> then) { content->confirmDiscard (std::move (then)); }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        lynx::MainComponent* content = nullptr; // pertence à janela

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    lynx::theme::LookAndFeel lookAndFeel;
    std::unique_ptr<lynx::AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LynxApplication)
