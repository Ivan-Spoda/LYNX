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

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth,
    sampler,
    audioClip, // sampler sem transposição que para no fim do clip (canal de clip de áudio, como no FL)
    plugin     // instrumento externo (VST3), criado pelo app
};

// Plugin externo: identificador do formato (o app sabe achar o arquivo) + estado salvo.
struct PluginRef
{
    std::string id;    // identificador único do plugin (formato-nome-hash do JUCE)
    std::string name;  // para mostrar mesmo quando o plugin não estiver instalado
    std::string state; // estado do plugin em base64 (o que o plugin devolve ao salvar)

    bool operator== (const PluginRef& o) const { return id == o.id && state == o.state; }
};

// Ajustes do sampler do canal (a janela "Channel settings" do FL).
struct SamplerSettings
{
    float pitch = 0.0f;   // semitons, -24..24
    float attack = 0.0f;  // segundos, 0..2
    float release = 0.0f; // segundos, 0..4; 0 = toca o sample inteiro (one-shot)
    bool reverse = false;
    bool cut = false;     // nota nova corta a anterior ("cut itself")

    bool operator== (const SamplerSettings&) const = default;
};

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.8f;   // 0..1
    float pan = 0.0f;      // -1..1
    bool muted = false;
    std::string samplePath; // usado quando instrument == sampler
    int mixerTrack = 0;     // 0 = direto no Master
    PluginRef plugin;       // usado quando instrument == plugin
    SamplerSettings sampler; // usado quando instrument == sampler
};

enum class EffectKind : std::uint8_t
{
    eq,
    compressor,
    delay,
    reverb,
    plugin // efeito externo (VST3), criado pelo app
};

constexpr int maxEffectParams = 6;
constexpr int maxEffectSlots = 10; // como no FL
using EffectParams = std::array<float, maxEffectParams>;

struct EffectSlot
{
    EffectKind kind = EffectKind::eq;
    bool enabled = true;
    EffectParams params {}; // 0..1; o significado depende do efeito
    float mix = 1.0f;       // dry/wet do slot (como no FL): 0 = só o sinal seco
    PluginRef plugin;                             // usado quando kind == plugin
};

// Envio de uma trilha para outra (pós-fader), como o roteamento entre inserts do FL.
struct MixerSend
{
    int target = 0;      // trilha de destino (> 0; o Master é `toMaster`)
    float level = 1.0f;  // 0..1 (ganho linear)

    bool operator== (const MixerSend& o) const noexcept { return target == o.target && level == o.level; }
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
    std::vector<MixerSend> sends;    // para outros inserts
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
};

// Atraso de swing para uma nota que começa em `start` (ticks na pattern): só as que caem
// exatamente num step ímpar (o "e" de cada colcheia) andam, até meio step com swing 1.
std::int64_t swingDelay (std::int64_t start, float swing) noexcept;

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
    effectParam    // index = trilha, slot, param (efeito interno ou plugin)
};

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

    std::int64_t end() const noexcept { return start + length; }
};

enum class PlayMode : std::uint8_t
{
    pattern, // toca a pattern atual em loop (como o "PAT" do FL)
    song     // toca a Playlist (como o "SONG" do FL)
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
struct Arrangement
{
    std::string name;
    std::vector<Clip> clips;               // vazio no arrangement atual: os clips dele moram em Song::playlist
    std::int64_t loopStart = 0, loopEnd = 0;
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
    std::vector<Arrangement> arrangements { Arrangement { "Arrangement 1", {}, 0, 0 } };
    int currentArrangement = 0;

    void selectArrangement (int index);
    int addArrangement (const std::string& name, bool copyCurrent = false); // retorna o índice (já selecionado)
    void removeArrangement (int index);
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

    std::vector<MixerTrack> mixer; // [0] = Master; vazio = tudo direto na saída
    std::vector<Automation> automations;
    std::vector<PlaylistTrack> playlistTracks; // índice = trilha; trilhas além do fim usam o padrão

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

    static Song makeDefault();
};

// Cópia da música pronta para exportar: sempre em modo song (a pattern atual vira um clip
// se `wholeSong` for false ou a Playlist estiver vazia), com `tailTicks` de silêncio no fim
// para releases e reverbs soarem sem a música recomeçar. `musicLength` recebe o fim da
// parte musical (sem a cauda), em ticks.
Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength);
} // namespace lynx
