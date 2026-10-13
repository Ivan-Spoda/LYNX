#include "core/Templates.h"

#include "core/SynthPresets.h"

namespace lynx
{
namespace
{
SynthSettings preset (const char* name)
{
    for (const auto& [n, s] : synthPresets())
        if (n == name)
            return s;
    return {};
}

ChannelDef synth (const char* name, const char* presetName, int track, float volume = 0.78f)
{
    ChannelDef c;
    c.name = name;
    c.instrument = InstrumentKind::synth;
    c.synth = preset (presetName);
    c.mixerTrack = track;
    c.volume = volume;
    return c;
}

void steps (Pattern& p, int channel, std::initializer_list<int> list, float velocity = 0.78f)
{
    for (auto s : list)
    {
        Note n;
        n.start = (std::int64_t) s * ticksPerStep;
        n.channel = channel;
        n.velocity = velocity;
        p.notes.push_back (n);
    }
}

void notes (Pattern& p, int channel, std::initializer_list<std::array<int, 3>> list)
{
    // {step, duração em steps, tecla}
    for (const auto& [step, length, key] : list)
    {
        Note n;
        n.start = (std::int64_t) step * ticksPerStep;
        n.length = (std::int64_t) length * ticksPerStep;
        n.key = key;
        n.channel = channel;
        p.notes.push_back (n);
    }
}
} // namespace

std::vector<std::string> templateNames()
{
    return { "Vazio", "Batida básica", "Trap com 808", "House" };
}

Song makeTemplate (int index)
{
    if (index == 1)
        return Song::makeDefault();

    if (index == 0)
    {
        auto s = Song::makeDefault();
        s.patterns[0].notes.clear();
        return s;
    }

    auto s = Song::makeDefault();
    s.patterns[0].notes.clear();
    auto& p = s.patterns[0];

    if (index == 2) // trap: 140 BPM, hats rápidos, 808 com slide
    {
        s.bpm = 140.0;
        p.name = "Trap";
        p.lengthTicks = (std::int64_t) ppq * 4 * 2;
        steps (p, 0, { 0, 10, 16, 26 }, 0.9f);
        steps (p, 3, { 8, 24 });
        for (int i = 0; i < 32; i += 2)
            steps (p, 2, { i }, i % 4 == 0 ? 0.7f : 0.45f);
        steps (p, 2, { 13, 29 }, 0.5f);
        s.channels[4] = synth ("808", "Baixo 808", 5, 0.85f);
        notes (p, 4, { { { 0, 6, 36 } }, { { 10, 4, 36 } }, { { 16, 6, 39 } }, { { 26, 6, 34 } } });
        Note glide;
        glide.start = 28 * ticksPerStep;
        glide.length = 2 * ticksPerStep;
        glide.key = 41;
        glide.channel = 4;
        glide.slide = true;
        p.notes.push_back (glide);
        s.swing = 0.0f;
    }
    else // house: 124 BPM, 4 no chão, clap, hats no contratempo, baixo e acorde
    {
        s.bpm = 124.0;
        p.name = "House";
        steps (p, 0, { 0, 4, 8, 12 }, 0.9f);
        steps (p, 1, { 4, 12 });
        steps (p, 2, { 2, 6, 10, 14 }, 0.6f);
        s.channels[4] = synth ("Baixo", "Baixo Reese", 5);
        notes (p, 4, { { { 2, 2, 33 } }, { { 6, 2, 33 } }, { { 10, 2, 36 } }, { { 14, 2, 31 } } });
        s.channels.push_back (synth ("Acorde", "Pluck", 6, 0.6f));
        for (int key : { 57, 60, 64 })
            notes (p, 5, { { { 3, 1, key } }, { { 7, 1, key } }, { { 11, 1, key } } });
        s.swing = 0.15f;
    }

    p.sortNotes();
    return s;
}
} // namespace lynx
