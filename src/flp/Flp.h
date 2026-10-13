#pragma once

// Leitor de projetos .flp (FL Studio), escrito do zero a partir da análise
// comportamental em docs/ANALISE_FL_STUDIO.md e de testes com arquivos reais.
// Não depende de JUCE para poder ser testado isoladamente.

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace flp
{
struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Nível 1: eventos crus

struct Event
{
    std::uint32_t id = 0;        // ID de 1 byte, ou ID estendido de 32 bits (escape 0xAC)
    std::uint32_t value = 0;     // payload de u8/u16/u32 (vazio para eventos de dados)
    std::span<const std::uint8_t> data; // payload de tamanho variável (vista sobre o buffer)

    // Classe de tamanho: 0 = u8, 1 = u16, 2 = u32, 3 = dados
    int sizeClass() const noexcept { return (int) ((id > 0xFF ? id >> 24 : id) >> 6); }
    bool isData() const noexcept { return sizeClass() == 3; }
};

struct Header
{
    std::uint16_t format = 0;    // 0 = projeto
    std::uint16_t channelCount = 0;
    std::uint16_t ppq = 96;
};

// Percorre os eventos de um buffer .flp. O buffer precisa viver mais que o leitor.
class EventReader
{
public:
    explicit EventReader (std::span<const std::uint8_t> file);

    const Header& header() const noexcept { return hdr; }

    // Retorna false no fim dos dados. Lança flp::Error se o arquivo estiver truncado.
    bool next (Event& out);

private:
    std::span<const std::uint8_t> bytes;
    std::size_t pos = 0;
    Header hdr;
};

// ---------------------------------------------------------------------------
// Nível 2: modelo de projeto

enum class ChannelType : std::uint8_t
{
    sampler = 0,
    native = 1,
    instrument = 2,
    layer = 3,
    audioClip = 4,
    automationClip = 5,
    unknown = 255
};

// Ponto de um clip de automação (evento 0xEA). Posições em tempos (semínimas).
struct AutomationPoint
{
    double position = 0.0;  // absoluta, já somando os deltas do arquivo
    double value = 0.0;     // 0..1
    float tension = 0.0f;   // -1..1
    std::uint8_t curve = 0; // 0 = single curve, 1 = double, 2 = hold, 10 = smooth...
};

struct Channel
{
    int index = 0;
    ChannelType type = ChannelType::unknown;
    std::string name;            // nome mostrado no Channel Rack
    std::string pluginName;      // nome interno do gerador (ex.: "FLEX")
    std::string samplePath;
    int mixerTrack = -1;
    std::vector<AutomationPoint> automation; // canais do tipo automationClip

    // evento 0xDB ("níveis" do canal): pan 0..12800 (6400 = centro), volume 0..12800
    // (10000 = 78%, o padrão), afinação em centésimos
    std::uint32_t colour = 0; // 0xRRGGBB (0 = sem cor gravada)
    bool hasLevels = false;
    int pan = 6400, volume = 10000, pitchCents = 0;
};

// Endereço de um parâmetro automatizável (controlID do FL), já no formato atual:
//  < 0x10000000: canal (id >> 16), parâmetro (id & 0xFFFF)
//  0x70000000 + (trilha << 22) + (slot << 16) + p: mixer
struct ControlId
{
    std::uint32_t raw = 0;

    bool isChannel() const noexcept { return raw < 0x10000000u; }
    bool isMixer() const noexcept { return raw >= 0x70000000u; }
    int channel() const noexcept { return (int) (raw >> 16); }
    int track() const noexcept { return (int) ((raw - 0x70000000u) >> 22); }
    int slot() const noexcept { return (int) ((raw >> 16) & 0x3F); }
    int param() const noexcept { return (int) (raw & 0xFFFF); }

    static constexpr int mixerVolume = 0x1FC0;
    static constexpr int mixerPan = 0x1FC1;
    static constexpr int mixerStereo = 0x1FC2; // separação ±64
    static constexpr int mixerSendBase = 0x2000; // + trilha de destino
    static constexpr int pluginParamBase = 0x8000;
    static constexpr std::uint32_t tempo = 0x40000005; // automação de tempo: 0..1 = 60..180 BPM (conferido nos demos)

    bool isTempo() const noexcept { return raw == tempo; }
};

// Valor gravado de um parâmetro (evento 0xE1).
struct ControlValue
{
    ControlId id;
    std::int32_t value = 0;
};

// Liga um clip de automação (canal) a um parâmetro (evento 0xE3).
struct AutomationLink
{
    int channel = 0;
    ControlId target;
};

struct Note
{
    std::uint32_t position = 0;  // em ticks do PPQ do arquivo
    std::uint32_t length = 0;    // 0 = nota de step sequencer (sem duração própria)
    std::uint16_t channel = 0;   // índice no Channel Rack
    std::uint16_t key = 60;      // 60 = C5 no FL
    std::uint16_t flags = 0;
    std::uint8_t finePitch = 120; // 120 = centro
    std::uint8_t release = 64;
    std::uint8_t pan = 64;       // 0..128, 64 = centro
    std::uint8_t velocity = 100; // 0..128
    std::uint8_t modX = 128;
    std::uint8_t modY = 128;
    std::uint8_t extra = 0;      // bits 0-3 cor/canal MIDI; 0x10 porta; 0x20 mutada; 0x80 selecionada
    std::uint8_t repeats = 0;

    bool muted() const noexcept { return (extra & 0x20) != 0; }
    bool slide() const noexcept { return (flags & 0xF) == 8; }
};

struct Pattern
{
    int index = 0;
    std::string name;
    std::optional<std::uint32_t> colour; // 0xRRGGBB (no arquivo vem como 0x00BBGGRR)
    std::vector<Note> notes;
};

struct PlaylistItem
{
    enum class Kind { pattern, channelClip };

    std::uint32_t position = 0;  // ticks
    std::uint32_t length = 0;    // ticks
    int track = 0;               // 0 = primeira trilha da playlist
    Kind kind = Kind::pattern;
    int index = 0;               // índice da pattern, ou do canal (clip de áudio/automação)
    std::uint16_t flags = 0;

    // Os mesmos 8 bytes têm significados diferentes por tipo de clip:
    //  - pattern: dois int32 em ticks (início/fim do trecho da pattern); -1 = sem recorte
    //  - clip de canal (áudio/automação): dois float
    std::int32_t patternStartTick = -1;
    std::int32_t patternEndTick = -1;
    float startOffset = 0.0f;
    float endOffset = 0.0f;
};

struct PlaylistTrackInfo
{
    int index = 0;        // 0 = primeira trilha
    std::string name;
    bool enabled = true;  // false = trilha mutada
};

struct Arrangement
{
    int index = 0;
    std::string name;
    std::vector<PlaylistItem> items;
    std::vector<PlaylistTrackInfo> tracks; // só as que o arquivo descreve
};

struct MixerInsert
{
    int index = 0;
    std::string name;
    std::uint32_t flags = 0x4C;  // 0x08 = habilitada (apagado = mutada), 0x1000 = solo
    std::vector<int> routes;      // trilhas de destino (0 = Master)
    bool hasRoutes = false;
};

struct Project
{
    Header header;
    std::string version;
    std::uint32_t versionNumber = 0; // major*1e6 + minor*1e3 + patch
    double tempo = 140.0;
    std::string title, artist, genre, url;

    std::vector<Channel> channels;
    std::vector<Pattern> patterns;
    std::vector<Arrangement> arrangements;
    std::vector<MixerInsert> mixer;
    std::vector<ControlValue> controls;       // volumes, pans, níveis de envio...
    std::vector<AutomationLink> automationLinks;

    std::vector<std::string> warnings; // coisas ignoradas ou suspeitas, para diagnóstico

    const Pattern* findPattern (int index) const noexcept;
    const Channel* findChannel (int index) const noexcept;
};

Project parseProject (std::span<const std::uint8_t> file);

// Utilitários expostos para testes
std::uint32_t parseVersionNumber (const std::string& version);
std::size_t playlistRecordSize (std::uint32_t versionNumber, std::size_t payloadSize);
// Converte um controlID de arquivos antigos (classe 0x2..., envios em 0x1F40+d) para o formato atual.
ControlId normalizeControlId (std::uint32_t raw);
} // namespace flp
