// renderwav: importa um .flp e renderiza uma pattern com os instrumentos internos.
// Uso: renderwav projeto.flp saida.wav [indice-da-pattern] [loops]
// Sem índice, escolhe a pattern com mais notas.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/FlpImport.h"
#include "core/RackRenderer.h"

namespace
{
void writeWav (const char* path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    std::ofstream out (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { out.write ((const char*) &v, 4); };
    auto u16 = [&] (std::uint16_t v) { out.write ((const char*) &v, 2); };
    const auto frames = (std::uint32_t) left.size();
    const auto dataBytes = frames * 4;

    out.write ("RIFF", 4);
    u32 (36 + dataBytes);
    out.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (1);  // PCM
    u16 (2);  // estéreo
    u32 ((std::uint32_t) sampleRate);
    u32 ((std::uint32_t) sampleRate * 4);
    u16 (4);
    u16 (16);
    out.write ("data", 4);
    u32 (dataBytes);

    for (std::uint32_t i = 0; i < frames; ++i)
        for (float s : { left[i], right[i] })
        {
            const auto clipped = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
            u16 ((std::uint16_t) (std::int16_t) (clipped * 32767.0f));
        }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "uso: renderwav projeto.flp saida.wav [pattern] [loops]\n");
        return 2;
    }

    std::ifstream in (argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    daw::Song song;

    try
    {
        song = daw::importFlp (flp::parseProject (bytes));
    }
    catch (const flp::Error& e)
    {
        std::fprintf (stderr, "erro: %s\n", e.what());
        return 1;
    }

    int chosen = argc > 3 ? std::atoi (argv[3]) : -1;

    if (chosen < 0 || chosen >= (int) song.patterns.size())
    {
        chosen = 0;
        for (int i = 0; i < (int) song.patterns.size(); ++i)
            if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) chosen].notes.size())
                chosen = i;
    }

    song.currentPattern = chosen;
    const int loops = argc > 4 ? std::max (1, std::atoi (argv[4])) : 2;
    const auto& pattern = song.patterns[(std::size_t) chosen];
    const auto lengthTicks = pattern.effectiveLength (song.beatsPerBar) * loops;

    constexpr int sampleRate = 44100;
    constexpr int blockSize = 256;
    const auto tps = daw::ticksPerSample (song.bpm, sampleRate);
    const auto totalSamples = (std::size_t) (lengthTicks / tps) + sampleRate; // +1 s de cauda

    std::printf ("%.1f BPM, %zu canais, pattern \"%s\" (%zu notas), %d loops, %.1f s\n", song.bpm,
                 song.channels.size(), pattern.name.c_str(), pattern.notes.size(), loops,
                 (double) totalSamples / sampleRate);

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        static const char* kinds[] = { "kick", "snare", "hat", "clap", "synth" };
        std::printf ("  %2zu %-28s -> %s\n", i, song.channels[i].name.c_str(), kinds[(int) song.channels[i].instrument]);
    }

    auto state = daw::RackState::build (song, nullptr, sampleRate);
    daw::RackRenderer renderer;
    renderer.prepare (sampleRate);

    std::vector<float> left (totalSamples), right (totalSamples);
    double tick = 0.0;

    for (std::size_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        const auto n = (int) std::min<std::size_t> (blockSize, totalSamples - pos);
        const bool playing = tick < (double) lengthTicks;
        renderer.process (state.get(), playing, tick, n, left.data() + pos, right.data() + pos);
        tick += n * tps;
    }

    // normaliza o pico para -1 dBFS
    float peak = 0.0f;
    for (std::size_t i = 0; i < totalSamples; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });

    if (peak > 0.0f)
    {
        const auto gain = 0.891f / peak;
        for (std::size_t i = 0; i < totalSamples; ++i)
        {
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    writeWav (argv[2], left, right, sampleRate);
    std::printf ("gravado em %s\n", argv[2]);
    return 0;
}
