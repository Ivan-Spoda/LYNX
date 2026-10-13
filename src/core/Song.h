#pragma once

// Modelo de dados da música. Não depende de JUCE.
// A UI edita uma cópia e publica um snapshot imutável para a thread de áudio
// (veja SnapshotExchange.h), então nada aqui precisa ser thread-safe.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx
{
constexpr int ppq = 960;              // ticks por semínima
constexpr int stepsPerBeat = 4;       // step sequencer em semicolcheias
constexpr int ticksPerStep = ppq / stepsPerBeat;
constexpr int maxMixerTracks = 128;   // 0 = Master, 1..127 = inserts (o FL tem 125 + master)
constexpr float unityFader = 0.8f;    // posição do fader que dá 0 dB, como no FL

// Curva do fader do mixer do FL: g = (11^v - 1) / 10 com v = posição * 1,25 (0 dB em 0,8;
// +5,6 dB no máximo). docs/analise/REA_rodada2.md §2.2.
float faderGain (float position) noexcept;

// Volume do canal com a mesma curva, mas 1,0 = 0 dB (o padrão do FL, 0,78, dá -5,2 dB).
// docs/analise/REA_rodada3.md §2.
float channelGain (float volume) noexcept;

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth,
    sampler,
    audioClip, // sampler sem transposição que para no fim do clip (canal de clip de áudio, como no FL)
    plugin,    // instrumento externo (VST3), criado pelo app
    controller // não toca som: as notas disparam envelopes que modulam outros parâmetros (Envelope Controller do FL)
};

// Plugin externo: identificador do formato (o app sabe achar o arquivo) + estado salvo.
// Plugin de terceiros vindo de um projeto do FL ("Fruity Wrapper"), antes de achar o mesmo
// plugin instalado (docs/analise/REA_rodada5.md). Não é salvo no .lynx: o app resolve ao importar.
struct ForeignPlugin
{
    enum class Format : std::uint8_t { none, vst2, vst3, clap, other };

    Format format = Format::none;
    std::uint32_t vst2Id = 0;                 // AEffect::uniqueID
    std::array<std::uint8_t, 16> classId {};  // TUID da classe VST3, como na memória
    bool hasClassId = false;
    std::string clapId, name, path;           // path: binário na máquina que salvou
    std::vector<std::uint8_t> state;          // registro 0x35 cru (formato depende de `format`)
    int midiInPort = -1, midiOutPort = -1;     // portas MIDI do wrapper (registro 0x01; −1 = nenhuma)

    bool operator== (const ForeignPlugin&) const = default;
};

struct PluginRef
{
    std::string id;    // identificador único do plugin (formato-nome-hash do JUCE)
    std::string name;  // para mostrar mesmo quando o plugin não estiver instalado
    std::string state; // estado do plugin em base64 (o que o plugin devolve ao salvar)
    ForeignPlugin foreign; // import do FL ainda não resolvido (format == none: nada pendente)

    bool operator== (const PluginRef& o) const { return id == o.id && state == o.state && foreign == o.foreign; }
};

// Ajustes do sampler do canal (a janela "Channel settings" do FL).
// Envelope de modulação (corte ou afinação), como os envelopes do canal no FL.
struct ModEnvelope
{
    float amount = 0.0f;  // filtro: oitavas (±); afinação: semitons (±). 0 = desligado
    float attack = 0.0f;  // segundos
    float hold = 0.0f;
    float decay = 0.3f;
    float sustain = 0.0f; // 0..1
    float release = 0.2f;

    bool operator== (const ModEnvelope&) const = default;
};

// LFO do canal (como os do FL, REA_rodada6 §2): forma, período, atraso e fade-in; a quantidade
// é na unidade do destino. Recomeça a cada nota.
struct ChannelLfo
{
    float amount = 0.0f;  // pan: −1..1; volume: profundidade 0..1; corte: oitavas; afinação: semitons. 0 = desligado
    float period = 0.5f;  // segundos, ou tempos com `sync`
    bool sync = false;
    int shape = 0;        // 0 senoide, 1 triângulo, 2 pulso
    float delay = 0.0f;   // segundos (ou tempos com `sync`) antes de começar
    float attack = 0.0f;  // fade-in, na mesma unidade
    bool global = false;  // fase presa à posição da música (o botão "Global" do FL) em vez de recomeçar por nota

    bool operator== (const ChannelLfo&) const = default;
};

enum LfoTarget { lfoPan, lfoVolume, lfoCutoff, lfoPitch, lfoTargets };

struct SamplerSettings
{
    float pitch = 0.0f;   // semitons, -24..24
    float attack = 0.0f;  // segundos, 0..2
    float decay = 0.0f;   // segundos até o sustain (0 = sem decaimento)
    float sustain = 1.0f; // 0..1
    float release = 0.0f; // segundos, 0..4; 0 = toca o sample inteiro (one-shot)
    bool reverse = false;
    bool cut = false;     // nota nova corta a anterior ("cut itself")
    float stretchBeats = 0.0f; // > 0: o sample inteiro dura isto (em tempos), acompanhando o BPM
    bool keepPitch = false;    // com stretch: estica sem mudar a altura (WSOLA); senão reamostra
    float regionStart = 0.0f;  // trecho tocado do sample (frações 0..1)
    float regionEnd = 1.0f;
    std::vector<float> slices; // fatias (inícios em frações): a tecla 60 + i toca a fatia i, na altura original
    bool porta = false;        // portamento (com "cut", o glide do 808): a nota parte da altura da anterior
    float portaBeats = 0.125f; // tempo do deslize, em tempos
    float cutoff = 20000.0f;   // filtro do canal (Hz); passa-baixas em 20 kHz sem ressonância = desligado
    float resonance = 0.0f;    // 0..1
    int filterType = 0;        // 0 passa-baixas, 1 passa-banda, 2 passa-altas, 3 rejeita-faixa
    ModEnvelope filterEnv;     // desloca o corte (oitavas)
    ModEnvelope pitchEnv;      // desloca a afinação (semitons)
    std::array<ChannelLfo, lfoTargets> lfo {}; // por LfoTarget

    bool operator== (const SamplerSettings&) const = default;
};

// Arpejador do canal (Channel settings do FL).
struct ArpSettings
{
    int direction = 0;       // 0 desligado, 1 sobe, 2 desce, 3 sobe e desce, 4 aleatório
    int range = 1;           // oitavas, 1..4
    float stepBeats = 0.25f; // duração de cada passo em tempos (0,25 = semicolcheia)
    float gate = 0.5f;       // fração do passo em que a nota fica soando
    int repeat = 1;          // vezes que cada nota toca antes da próxima

    bool enabled() const noexcept { return direction > 0; }
    bool operator== (const ArpSettings&) const = default;
};

// Synth interno de 3 osciladores (no espírito do 3xOsc do FL).
enum class Waveform : std::uint8_t
{
    sine,
    triangle,
    saw,
    square,
    noise
};

struct SynthOscillator
{
    Waveform wave = Waveform::saw;
    int octave = 0;      // -3..3
    float detune = 0.0f; // centésimos, -100..100
    float level = 0.0f;  // 0..1
    int semitones = 0;   // -11..11, somados à oitava (o "coarse" do 3x Osc vai até ±24)
    float pan = 0.0f;    // -1..1, balanço linear como no FL

    bool operator== (const SynthOscillator&) const = default;
};

struct SynthSettings
{
    std::array<SynthOscillator, 3> osc { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    float attack = 0.005f; // segundos
    float decay = 0.3f;
    float sustain = 1.0f;  // 0..1
    float release = 0.25f;
    float cutoff = 2500.0f;  // Hz
    float resonance = 0.0f;  // 0..1
    int filterType = 0;      // 0 passa-baixas, 1 passa-banda, 2 passa-altas, 3 rejeita-faixa
    float keyTrack = 0.0f;   // o corte acompanha a nota: oitavas por oitava a partir da tecla 48 (C3, 130,81 Hz)
    // varredura de afinação de bumbo: f = f_nota·(1 + sweep·s⁴), s = e^(−t/sweepTime) (o "Peak"/"Slide" do BassDrum)
    float sweep = 0.0f;      // 0 = desligada
    float sweepTime = 0.1f;  // segundos (constante de tempo de s)
    // tonalidade de 3 bandas depois do filtro (o EQ do BooBass): grave < ~178 Hz < médio < ~1,18 kHz < agudo,
    // separados por passa-baixas de 1 polo; ganhos lineares (1, 1, 1 = desligado)
    std::array<float, 3> bandGain { 1.0f, 1.0f, 1.0f };
    ModEnvelope filterEnv;   // desloca o corte (oitavas)
    ModEnvelope pitchEnv;    // desloca a afinação (semitons)
    std::array<ChannelLfo, lfoTargets> lfo {}; // por LfoTarget
    bool mono = false;       // uma voz só: a nota nova assume a voz que está soando (sem estalo)
    bool porta = false;       // portamento: a nota nova desliza a partir da anterior
    float portaBeats = 0.125f; // tempo do deslize, em tempos (o padrão do FL: meio step)
    bool am = false;
    bool ring = false;       // com `am`: modulação em anel do Sytrus, (osc 1 + 2)·(1 − g3 + g3·osc 3) em vez de 1 − g3·osc 3         // osc 3 modula a amplitude de osc 1 + osc 2 (o "AM" do 3x Osc)
    float fm = 0.0f;         // índice de FM: o osc 3 (calado) desloca a fase de osc 1 + 2 em fm·saída ciclos; 0 = desligado
    float fmDecay = 0.0f;    // segundos para o índice cair até fmSustain (0 = índice constante)
    float fmSustain = 1.0f;  // fração do índice que fica depois do decay

    bool operator== (const SynthSettings&) const = default;
};

// Zona de um sampler multi-sample (como as do DirectWave/FPC do FL): o sample toca nas teclas e
// velocities da faixa, afinado a partir da nota-raiz.
struct SampleZone
{
    std::string path;
    int rootKey = 60;
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127; // 0..127
    float tune = 0.0f;  // semitons
    float gain = 1.0f;
    std::int64_t loopStart = 0, loopEnd = 0; // em quadros do sample; loopEnd <= loopStart = sem loop
    int loopMode = 0;   // 0 sem loop, 1 contínuo, 2 enquanto a nota está pressionada

    bool operator== (const SampleZone&) const = default;
};

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.78f;  // 0..1 (curva channelGain; 0,78 = padrão do FL)
    float pan = 0.0f;      // -1..1
    bool muted = false;
    std::string samplePath; // usado quando instrument == sampler
    std::vector<SampleZone> zones; // sampler multi-sample (vazio: só samplePath)
    int mixerTrack = 0;     // 0 = direto no Master
    PluginRef plugin;       // usado quando instrument == plugin
    std::vector<std::uint8_t> controllerState; // instrument == controller: estado do Envelope Controller do FL
    SamplerSettings sampler; // usado quando instrument == sampler
    SynthSettings synth;     // usado quando instrument == synth
    float swingMix = 1.0f;   // quanto do swing global vale para este canal (0..1), como no FL
    ArpSettings arp;
    int cutGroup = 0;        // tocar este canal corta os canais com cutBy igual (0 = nenhum)
    int cutBy = 0;
    std::uint32_t colour = 0; // 0xFFRRGGBB; 0 = padrão do tema
};

enum class EffectKind : std::uint8_t
{
    eq,
    compressor,
    delay,
    reverb,
    plugin, // efeito externo (VST3/CLAP), criado pelo app
    limiter,
    filter,
    distortion,
    chorus,
    parametricEq, // 7 bandas (como o Parametric EQ 2 do FL)
    utility,      // ganho, pan e matriz estéreo (Balance, PanOMatic, Stereo Shaper do FL)
    volumeGate,   // ganho por passo sincronizado ao compasso (o volume do Gross Beat)
    peakController, // não muda o som: mede o pico e serve de fonte para automações ao vivo (Peak Controller do FL)
    pitchShift,   // muda a afinação sem mudar a duração (duas cabeças de leitura em crossfade)
    freqShift,    // desloca todas as frequências em Hz (banda lateral única, sem preservar harmonia)
    parallel,     // contêiner: ramos de efeitos em paralelo somados (o Patcher do FL); ver EffectSlot::branches
    lofi,         // redução de taxa (segura amostras) e de bits (o Lo-fi do Effector do FL)
    effector,     // o Effector do FL: 12 algoritmos com X/Y, LFO e Mod Rate (core/Effector.h)
    crossover,    // banda de um divisor de frequências (Frequency Splitter do FL): passa-altas e/ou passa-baixas Linkwitz-Riley
    rhythmGain,   // ganho que segue uma curva de pontos em tempos, em loop, travada na música (envelopes do Love Philter)
    autoPan       // pan movido por um LFO triangular travado na música (LFO de pan do Love Philter)
};

constexpr int maxEffectParams = 40; // o EQ paramétrico de 7 bandas usa a maior parte
constexpr int maxEffectSlots = 10; // como no FL
using EffectParams = std::array<float, maxEffectParams>;

struct EffectSlot;

// Um ramo do contêiner `parallel`: uma cadeia de efeitos internos e o ganho com que entra na soma
// (cadeia vazia = o sinal seco).
struct EffectBranch
{
    std::vector<EffectSlot> chain;
    float gain = 1.0f;
};

struct EffectSlot
{
    EffectKind kind = EffectKind::eq;
    bool enabled = true;
    EffectParams params {}; // 0..1; o significado depende do efeito
    float mix = 1.0f;       // dry/wet do slot (como no FL): 0 = só o sinal seco
    PluginRef plugin;                             // usado quando kind == plugin
    std::vector<EffectBranch> branches;           // usado quando kind == parallel
};

// Envio de uma trilha para outra (pós-fader), como o roteamento entre inserts do FL.
struct MixerSend
{
    int target = 0;      // trilha de destino (> 0; o Master é `toMaster`)
    float level = 1.0f;  // 0..1 (ganho linear)

    bool operator== (const MixerSend& o) const noexcept { return target == o.target && level == o.level; }
};

// EQ embutido de cada trilha do mixer (as 3 bandas do FL: prateleira grave, pico, prateleira aguda),
// depois dos slots de efeito e antes do fader (REA_rodada6 §1).
struct TrackEq
{
    std::array<float, 3> gainDb { 0.0f, 0.0f, 0.0f };          // ±18
    std::array<float, 3> hz { 90.0f, 1500.0f, 8000.0f };       // 10 Hz .. 16 kHz
    std::array<float, 3> width { 0.267f, 0.267f, 0.267f };     // 0..1: Q do pico / inclinação das prateleiras

    bool active() const noexcept { return gainDb[0] != 0.0f || gainDb[1] != 0.0f || gainDb[2] != 0.0f; }
    bool operator== (const TrackEq&) const = default;

    // conversões do FL: pico Q = 0,0999·(1001^w − 1) + 0,1; prateleiras S = 1,9·w + 0,1
    static double peakQ (float w) noexcept;
    static double shelfSlope (float w) noexcept;
};

struct MixerTrack
{
    std::string name;
    float volume = unityFader; // 0..1; ganho = volume / unityFader
    float pan = 0.0f;          // -1..1
    bool muted = false;
    bool solo = false;
    std::vector<EffectSlot> effects; // até maxEffectSlots, processados em ordem
    bool toMaster = true;            // inserts: sai no Master (além dos envios)
    float stereo = 0.0f;             // separação: -1 = alarga ao máximo, 0 = normal, +1 = mono (como no FL)
    std::uint32_t colour = 0;        // 0xFFRRGGBB; 0 = padrão do tema
    bool invertPhase = false;
    bool swapChannels = false;
    std::vector<MixerSend> sends;    // para outros inserts
    TrackEq eq;
};

struct Note
{
    std::int64_t start = 0;   // ticks
    std::int64_t length = ticksPerStep;
    int channel = 0;
    int key = 60;             // MIDI; 60 = C4 (o C5 do FL)
    float velocity = 0.78f;   // 0..1
    float pan = 0.0f;         // -1..1 (por nota, como no FL)
    float finePitch = 0.0f;   // centésimos de semitom, -120..120
    bool muted = false;       // nota desligada (não toca, continua na pattern)
    bool slide = false;       // desliza a nota que está soando até esta altura (sem reatacar), como no FL
    bool porta = false;       // inverte o porta do canal nesta nota (a nota desliza da anterior; como no FL)
};

// Atraso de swing (fórmula do FL, docs/analise/REA_rodada3.md §3): só notas cujo step mais
// próximo é ímpar andam, com peso triangular pela distância à grade; meio step com swing 1.
std::int64_t swingDelay (std::int64_t start, float swing, float channelMix = 1.0f) noexcept;

struct Pattern
{
    std::string name;
    std::vector<Note> notes;  // ordenadas por start (mantido por sortNotes)
    std::int64_t lengthTicks = 0; // 0 = automático (arredonda para compassos)
    std::uint32_t colour = 0;     // 0xFFRRGGBB; 0 = automática (pelo índice)

    void sortNotes();
    std::int64_t effectiveLength (int beatsPerBar) const noexcept;

    bool hasStep (int channel, int step) const noexcept;
    void toggleStep (int channel, int step);
};

enum class ClipKind : std::uint8_t
{
    pattern,
    audio,
    automation
};

// O que um clip de automação controla. Valores sempre 0..1 (como os knobs do modelo).
enum class AutomationTargetKind : std::uint8_t
{
    none,          // alvo removido (o clip fica mudo)
    channelVolume, // index = canal
    channelPan,    // index = canal (0..1 -> -1..1)
    channelParam,  // index = canal com plugin; param = índice do parâmetro do plugin
    mixerVolume,   // index = trilha do mixer (0 = Master)
    mixerPan,      // index = trilha
    effectParam,   // index = trilha, slot, param (efeito interno ou plugin)
    tempo,         // andamento da música: 0..1 = minTempo..maxTempo BPM
    effectMix      // index = trilha, slot: dry/wet do slot (0..1)
};

constexpr double minTempo = 10.0, maxTempo = 522.0; // a faixa do FL
inline double tempoFromValue (float v) noexcept { return minTempo + (maxTempo - minTempo) * (double) v; }
inline float valueFromTempo (double bpm) noexcept { return (float) ((bpm - minTempo) / (maxTempo - minTempo)); }

struct AutomationTarget
{
    AutomationTargetKind kind = AutomationTargetKind::none;
    int index = 0;
    int slot = 0;
    int param = 0;

    bool operator== (const AutomationTarget& o) const noexcept
    {
        return kind == o.kind && index == o.index && slot == o.slot && param == o.param;
    }
};

struct AutomationPoint
{
    std::int64_t tick = 0; // a partir do início da automação
    float value = 0.5f;    // 0..1
    float tension = 0.0f;  // curva até o próximo ponto: -1..1 (0 = reta)
};

struct Automation
{
    std::string name;
    AutomationTarget target;
    std::vector<AutomationPoint> points; // ordenados por tick (mantido por quem edita)

    // Fonte ao vivo (link de controlador do FL, REA_rodada14 §7): o valor vem da saída `output` do efeito
    // controlador no slot (track, slot), passado pela curva (pontos igualmente espaçados em 0..1; vazia =
    // direto). Não usa clips nem pontos.
    struct LiveSource
    {
        int track = -1, slot = 0, output = 0;
        int channel = -1; // em vez de um efeito: o instrumento "controlador" deste canal
    } source;
    std::vector<float> curve;
    bool isLive() const noexcept { return source.track >= 0 || source.channel >= 0; }
    float mapLive (float v) const noexcept; // aplica a curva

    // Valor em `tick` (antes do primeiro ponto = primeiro valor; depois do último = último).
    float valueAt (double tick) const noexcept;
    std::int64_t length() const noexcept; // tick do último ponto (mínimo 1)
};

// Um bloco na Playlist: um trecho de uma pattern, ou um trecho do áudio de um canal audioClip.
struct Clip
{
    int pattern = 0;
    int track = 0;
    std::int64_t start = 0;   // ticks na música
    std::int64_t length = 0;  // ticks
    std::int64_t offset = 0;  // quanto do começo da pattern/áudio é pulado (ticks)
    ClipKind kind = ClipKind::pattern;
    int channel = -1;         // clips de áudio: canal (audioClip) que tem o sample
    int automation = -1;      // clips de automação: índice em Song::automations
    std::int64_t fadeIn = 0, fadeOut = 0; // clips de áudio: fades (ticks)
    float gain = 1.0f;        // clips de áudio: volume do clip

    std::int64_t end() const noexcept { return start + length; }
};

enum class PlayMode : std::uint8_t
{
    pattern, // toca a pattern atual em loop (como o "PAT" do FL)
    song     // toca a Playlist (como o "SONG" do FL)
};

// Controlador MIDI (CC) ligado a um parâmetro ("Link to controller" do FL).
struct MidiLink
{
    int cc = 0;       // 0..127
    int channel = -1; // canal MIDI 0..15, -1 = qualquer
    AutomationTarget target;
};

// Cabeçalho de uma trilha da Playlist.
struct PlaylistTrack
{
    std::string name;  // vazio = "Track N"
    bool muted = false; // clips desta trilha não tocam
};

// Divide um clip em `tick` (absoluto): o original fica com o começo e o retorno é o resto.
// `tick` precisa estar dentro do clip.
Clip splitClip (Clip& clip, std::int64_t tick);

// Um arranjo da Playlist (o FL permite vários por projeto).
// Marcador da Playlist (como os "time markers" do FL). Com numerador > 0 é uma mudança de
// compasso: um compasso novo começa ali, com numerador/denominador.
struct Marker
{
    std::int64_t tick = 0;
    std::string name;
    int numerator = 0, denominator = 4;

    bool isSignature() const noexcept { return numerator > 0; }
    bool operator== (const Marker&) const = default;
};

// Um compasso da régua: onde começa, quantos tempos (semínimas) tem e o número (1, 2, ...).
struct Bar
{
    std::int64_t tick = 0, length = 0;
    int number = 1;
};

struct Arrangement
{
    std::string name;
    std::vector<Clip> clips;               // vazio no arrangement atual: os clips dele moram em Song::playlist
    std::int64_t loopStart = 0, loopEnd = 0;
    std::vector<Marker> markers;           // ordenados por tick (ficam aqui mesmo no arrangement atual)
};

struct Song
{
    double bpm = 130.0;
    int beatsPerBar = 4;
    float swing = 0.0f; // 0..1: atrasa as semicolcheias dos contratempos (como o swing do Channel Rack do FL)
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;

    std::vector<Clip> playlist;
    PlayMode mode = PlayMode::pattern;
    std::int64_t loopStart = 0, loopEnd = 0; // trecho em loop na Playlist (ticks); loopEnd <= loopStart = desligado

    // Arrangements: `playlist` e o loop acima são os do atual; os outros ficam guardados aqui.
    std::vector<Arrangement> arrangements { Arrangement { "Arrangement 1", {}, 0, 0, {} } };
    int currentArrangement = 0;

    void selectArrangement (int index);
    int addArrangement (const std::string& name, bool copyCurrent = false); // retorna o índice (já selecionado)
    void removeArrangement (int index);
    // Aplica `fn` a todos os alvos (automações e controladores MIDI).
    template <typename Fn>
    void forEachTarget (Fn&& fn)
    {
        for (auto& a : automations)
            fn (a.target);
        for (auto& l : midiLinks)
            fn (l.target);
    }
    void removeDeadMidiLinks();

    // Aplica `fn` a todas as listas de clips (a atual e as guardadas).
    template <typename Fn>
    void forEachClipList (Fn&& fn)
    {
        fn (playlist);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].clips);
    }

    bool hasLoop() const noexcept { return loopEnd > loopStart; }

    // Marcadores do arrangement atual.
    std::vector<Marker>& markers();
    const std::vector<Marker>& markers() const;
    void addMarker (Marker marker); // mantém a ordem

    // Compassos que tocam [from, to), levando em conta as mudanças de compasso dos marcadores
    // (antes da primeira vale beatsPerBar/4).
    std::vector<Bar> barsBetween (std::int64_t from, std::int64_t to) const;
    Bar barAt (std::int64_t tick) const;

    std::vector<MixerTrack> mixer; // [0] = Master; vazio = tudo direto na saída
    std::vector<Automation> automations;
    std::vector<PlaylistTrack> playlistTracks; // índice = trilha; trilhas além do fim usam o padrão
    std::vector<MidiLink> midiLinks;

    bool isTrackMuted (int track) const noexcept
    {
        return track >= 0 && track < (int) playlistTracks.size() && playlistTracks[(std::size_t) track].muted;
    }

    static std::vector<MixerTrack> defaultMixer (int inserts = 16);
    // Primeiro insert que nenhum canal usa (ou o último, se todos estiverem ocupados).
    int nextFreeInsert() const;

    // Fim do último clip, arredondado para compasso (mínimo 1 compasso).
    std::int64_t songLength() const noexcept;

    // Remove uma pattern e conserta os clips e o índice atual.
    void removePattern (int index);

    // Remove um canal com as notas e clips de áudio dele, e reindexa os demais.
    // Automações desse canal ficam sem alvo.
    void removeChannel (int index);

    // Roteamento entre inserts. canRoute = destino válido e sem criar ciclo.
    bool canRoute (int from, int to) const;
    // Liga/desliga o envio from -> to (to == 0 = Master). Não faz nada se criar ciclo.
    void setRoute (int from, int to, bool enabled, float level = 1.0f);
    bool routes (int from, int to) const noexcept;
    // Inserts (1..N) em ordem de processamento: quem envia vem antes de quem recebe.
    std::vector<int> mixerOrder() const;
    // Trilhas que soam com o solo atual: as solo, as que chegam nelas e o caminho até o Master.
    // Sem solo, todas. Índice = trilha.
    std::vector<bool> soloAudible() const;

    // Troca/remove slots de efeito mantendo as automações apontando para o efeito certo.
    void swapEffectSlots (int track, int a, int b);
    void removeEffectSlot (int track, int slot);

    // Remove uma automação e os clips dela.
    void removeAutomation (int index);

    // Valor atual (do modelo, sem automação) do alvo, 0..1; -1 se o alvo não existir.
    float targetValue (const AutomationTarget& target) const noexcept;
    // Muda o valor do alvo no modelo (0..1). Parâmetros de plugin não moram aqui: retorna false.
    bool setTargetValue (const AutomationTarget& target, float value) noexcept;

    // Valor de uma automação numa posição da música (o mesmo que a reprodução usa); NaN sem clips.
    float automationValueAt (int automation, double tick) const;
    // Andamento em `tick`, considerando automação de tempo (no modo song).
    double tempoAt (double tick) const;
    // Duração em segundos dos primeiros `ticks` da música, integrando a automação de tempo.
    double secondsForTicks (std::int64_t ticks) const;

    static Song makeDefault();
};

// Cópia da música pronta para exportar: sempre em modo song (a pattern atual vira um clip
// se `wholeSong` for false ou a Playlist estiver vazia), com `tailTicks` de silêncio no fim
// para releases e reverbs soarem sem a música recomeçar. `musicLength` recebe o fim da
// parte musical (sem a cauda), em ticks.
Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength);

// Cópia da música em que só se ouve a saída (pós-fader) do insert `track`, direto no Master
// sem efeitos nem fader: é o que se renderiza para "transformar o insert em áudio".
Song makeBounceSong (const Song& song, int track);

// Depois de renderizar o insert para `samplePath` (`length` ticks a partir do começo): cria um
// canal de clip de áudio num insert novo com o mesmo roteamento, põe o clip na Playlist e muta
// o insert original. Retorna o insert novo.
int applyBounce (Song& song, int track, const std::string& samplePath, std::int64_t length);
} // namespace lynx
