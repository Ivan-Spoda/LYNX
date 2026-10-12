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

struct Channel
{
    int index = 0;
    ChannelType type = ChannelType::unknown;
    std::string name;            // nome mostrado no Channel Rack
    std::string pluginName;      // nome interno do gerador (ex.: "FLEX")
    std::string samplePath;
    int mixerTrack = -1;
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
};

struct Pattern
{
    int index = 0;
    std::string name;
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
    float startOffset = 0.0f;
    float endOffset = 0.0f;
};

struct Arrangement
{
    int index = 0;
    std::string name;
    std::vector<PlaylistItem> items;
};

struct MixerInsert
{
    int index = 0;
    std::string name;
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

    std::vector<std::string> warnings; // coisas ignoradas ou suspeitas, para diagnóstico

    const Pattern* findPattern (int index) const noexcept;
    const Channel* findChannel (int index) const noexcept;
};

Project parseProject (std::span<const std::uint8_t> file);

// Utilitários expostos para testes
std::uint32_t parseVersionNumber (const std::string& version);
std::size_t playlistRecordSize (std::uint32_t versionNumber, std::size_t payloadSize);
} // namespace flp
