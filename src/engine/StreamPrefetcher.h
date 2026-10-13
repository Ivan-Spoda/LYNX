#pragma once

#include <juce_core/juce_core.h>

#include "core/Song.h"
#include "engine/SampleLibrary.h"
#include "engine/Transport.h"

namespace lynx
{
// Thread que lê do disco, antes da hora, o trecho dos samples mapeados (os longos, ver
// SampleLibrary) que vai tocar nos próximos segundos. Assim a thread de áudio quase nunca
// espera o disco. Os clips vêm da UI (update) e a posição do transporte.
class StreamPrefetcher final : private juce::Thread
{
public:
    explicit StreamPrefetcher (const Transport& transport);
    ~StreamPrefetcher() override;

    // Thread da UI: clips de áudio da música atual que usam samples mapeados.
    void update (const Song& song, SampleLibrary& samples);

    static constexpr double lookaheadSeconds = 4.0;

private:
    struct Entry
    {
        std::shared_ptr<const SampleData> sample;
        double start = 0.0, end = 0.0; // ticks na música
        double offsetSeconds = 0.0;    // de onde o sample começa a tocar
    };

    void run() override;

    const Transport& transport;
    juce::CriticalSection lock;
    std::vector<Entry> entries;
    double songLength = 1.0;
    double bpm = 120.0;
    bool songMode = false;
};
} // namespace lynx
