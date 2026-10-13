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

struct EffectSlot
{
    EffectKind kind = EffectKind::eq;
    bool enabled = true;
    std::array<float, maxEffectParams> params {}; // 0..1; o significado depende do efeito
    PluginRef plugin;                             // usado quando kind == plugin
};

struct MixerTrack
{
    std::string name;
    float volume = unityFader; // 0..1; ganho = volume / unityFader
    float pan = 0.0f;          // -1..1
    bool muted = false;
    bool solo = false;
    std::vector<EffectSlot> effects; // até maxEffectSlots, processados em ordem
};

struct Note
{
    std::int64_t start = 0;   // ticks
    std::int64_t length = ticksPerStep;
    int channel = 0;
    int key = 60;             // MIDI; 60 = C4 (o C5 do FL)
    float velocity = 0.78f;   // 0..1
};

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
    audio
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

    std::int64_t end() const noexcept { return start + length; }
};

enum class PlayMode : std::uint8_t
{
    pattern, // toca a pattern atual em loop (como o "PAT" do FL)
    song     // toca a Playlist (como o "SONG" do FL)
};

struct Song
{
    double bpm = 130.0;
    int beatsPerBar = 4;
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;

    std::vector<Clip> playlist;
    PlayMode mode = PlayMode::pattern;
    std::int64_t loopStart = 0, loopEnd = 0; // trecho em loop na Playlist (ticks); loopEnd <= loopStart = desligado

    bool hasLoop() const noexcept { return loopEnd > loopStart; }

    std::vector<MixerTrack> mixer; // [0] = Master; vazio = tudo direto na saída

    static std::vector<MixerTrack> defaultMixer (int inserts = 16);
    // Primeiro insert que nenhum canal usa (ou o último, se todos estiverem ocupados).
    int nextFreeInsert() const;

    // Fim do último clip, arredondado para compasso (mínimo 1 compasso).
    std::int64_t songLength() const noexcept;

    // Remove uma pattern e conserta os clips e o índice atual.
    void removePattern (int index);

    // Remove um canal com as notas e clips de áudio dele, e reindexa os demais.
    void removeChannel (int index);

    static Song makeDefault();
};

// Cópia da música pronta para exportar: sempre em modo song (a pattern atual vira um clip
// se `wholeSong` for false ou a Playlist estiver vazia), com `tailTicks` de silêncio no fim
// para releases e reverbs soarem sem a música recomeçar. `musicLength` recebe o fim da
// parte musical (sem a cauda), em ticks.
Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength);
} // namespace lynx
