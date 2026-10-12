#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Effects.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "engine/Text.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

class LynxApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);

        if (args.contains ("--selftest"))
        {
            runSelfTest (args[args.indexOf ("--selftest") + 1].unquoted());
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

        if (args.contains ("--render"))
        {
            const auto i = args.indexOf ("--render");
            runRender (args[i + 1].unquoted(), args[i + 2].unquoted(), args[i + 3].getDoubleValue());
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        engine = std::make_unique<lynx::AudioEngine>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);

        // Duplo clique num .lynx/.flp abre o LYNX com o arquivo como argumento.
        if (args.size() > 0 && juce::File::isAbsolutePath (args[0].unquoted()))
            mainWindow->openFile (juce::File (args[0].unquoted()));
    }

    void shutdown() override
    {
        mainWindow = nullptr; // a UI sai antes do engine
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    // LYNX.exe --selftest relatorio.txt: renderiza 2 s sem placa de som e grava o resultado.
    // Código de saída 0 = saiu áudio, 1 = silêncio.
    void runSelfTest (const juce::String& reportPath)
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
        juce::String error;
        const auto reloaded = lynx::ProjectFile::fromJson (lynx::ProjectFile::toJson (original), error);
        const auto projectOk = reloaded && reloaded->channels.size() == original.channels.size()
                               && reloaded->channels[0].name == original.channels[0].name
                               && reloaded->patterns[0].notes.size() == original.patterns[0].notes.size()
                               && reloaded->patterns[0].notes[3].start == original.patterns[0].notes[3].start
                               && reloaded->mode == lynx::PlayMode::song && reloaded->playlist.size() == 1
                               && reloaded->playlist[0].offset == 960 && reloaded->playlist[0].track == 2
                               && reloaded->mixer[3].effects.size() == 1
                               && reloaded->mixer[3].effects[0].kind == lynx::EffectKind::delay
                               && reloaded->mixer[3].effects[0].params[1] == 0.25f;
        // desfazer/refazer
        offline.editSong ([] (lynx::Song& s) { s.bpm = 99.0; });
        offline.editSong ([] (lynx::Song& s) { s.bpm = 101.0; });
        offline.undo();
        const auto afterUndo = offline.getSong().bpm;
        offline.redo();
        const auto undoOk = afterUndo == 99.0 && offline.getSong().bpm == 101.0 && offline.canUndo() && ! offline.canRedo();

        const auto ok = audioOk && projectOk && undoOk;

        juce::String report;
        report << "conexoes=" << offline.getNumGraphConnections() << "\n"
               << "pico=" << peak << "\n"
               << "projeto=" << (projectOk ? "OK" : "ERRO " + error) << "\n"
               << "desfazer=" << (undoOk ? "OK" : "ERRO") << "\n"
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
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void openFile (const juce::File& file) { content->openFile (file); }

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
