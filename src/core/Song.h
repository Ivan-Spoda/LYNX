#pragma once

// Modelo de dados da música. Não depende de JUCE.
// A UI edita uma cópia e publica um snapshot imutável para a thread de áudio
// (veja SnapshotExchange.h), então nada aqui precisa ser thread-safe.

#include <cstdint>
#include <string>
#include <vector>

namespace daw
{
constexpr int ppq = 960;              // ticks por semínima
constexpr int stepsPerBeat = 4;       // step sequencer em semicolcheias
constexpr int ticksPerStep = ppq / stepsPerBeat;

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth
};

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.8f;   // 0..1
    float pan = 0.0f;      // -1..1
    bool muted = false;
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

    void sortNotes();
    std::int64_t effectiveLength (int beatsPerBar) const noexcept;

    bool hasStep (int channel, int step) const noexcept;
    void toggleStep (int channel, int step);
};

struct Song
{
    double bpm = 130.0;
    int beatsPerBar = 4;
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;

    static Song makeDefault();
};
} // namespace daw
