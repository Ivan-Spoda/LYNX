// renderwav: imports an .flp and renders a pattern with the built-in instruments.
// Usage: renderwav project.flp output.wav [pattern-index] [loops]
// Without an index, picks the pattern with the most notes.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <chrono>

#include "WavReader.h"
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
    u16 (2);  // stereo
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
        std::fprintf (stderr, "usage: renderwav project.flp output.wav [pattern] [loops]\n");
        return 2;
    }

    std::ifstream in (argv[1], std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    lynx::Song song;

    // On Linux/WSL FL lives under /mnt/c; the .flp paths use backslashes.
    const std::string flRoot = std::getenv ("FL_ROOT") ? std::getenv ("FL_ROOT")
                                                       : "/mnt/c/Program Files/Image-Line/FL Studio 2026";
    auto toLocal = [] (std::string p) {
        std::replace (p.begin(), p.end(), '\\', '/');
        return p;
    };

    lynx::FlpImportOptions options;
    options.roots = { flRoot, flRoot + "/Data", "" };
    options.fileExists = [&] (const std::string& p) { return std::filesystem::exists (toLocal (p)); };

    try
    {
        song = lynx::importFlp (flp::parseProject (bytes), options);
    }
    catch (const flp::Error& e)
    {
        std::fprintf (stderr, "error: %s\n", e.what());
        return 1;
    }

    // "song" instead of the index: the whole song (playlist), like the app's export
    const bool wholeSong = argc > 3 && std::string (argv[3]) == "song";
    std::int64_t songTicks = 0;

    if (wholeSong)
        song = lynx::makeExportSong (song, true, 0, songTicks);

    int chosen = argc > 3 && ! wholeSong ? std::atoi (argv[3]) : -1;

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
    const auto lengthTicks = wholeSong ? songTicks : pattern.effectiveLength (song.beatsPerBar) * loops;

    constexpr int sampleRate = 44100;
    constexpr int blockSize = 256;
    const auto tps = lynx::ticksPerSample (song.bpm, sampleRate);
    const auto totalSamples = (std::size_t) (lengthTicks / tps) + sampleRate; // +1 s of tail

    std::printf ("%.1f BPM, %zu channels, pattern \"%s\" (%zu notes), %d loops, %.1f s\n", song.bpm,
                 song.channels.size(), pattern.name.c_str(), pattern.notes.size(), loops,
                 (double) totalSamples / sampleRate);

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        static const char* kinds[] = { "kick", "snare", "hat", "clap", "synth", "sampler", "audio clip", "plugin", "controller", "sakura", "drumpad", "boobass", "drumsynth" };
        std::printf ("  %2zu %-28s -> %s\n", i, song.channels[i].name.c_str(), kinds[(int) song.channels[i].instrument]);
    }

    std::map<std::string, std::shared_ptr<const lynx::SampleData>> cache;
    auto state = lynx::RackState::build (song, nullptr, sampleRate, [&] (const std::string& p) {
        auto& entry = cache[p];
        if (entry == nullptr)
            entry = lynx::tools::readWav (toLocal (p));
        if (entry == nullptr)
            std::printf ("  (could not read %s)\n", p.c_str());
        return entry;
    });
    {
        std::size_t edges = 0, delayed = 0, tracksWithFx = 0;
        for (const auto& ins : state->mixerInputs)
            for (const auto& in : ins)
            {
                ++edges;
                const auto& d = state->sendDelays[(std::size_t) in.source];
                delayed += (std::size_t) in.delay < d.size() && d[(std::size_t) in.delay] != nullptr;
            }
        for (const auto& t : song.mixer)
            tracksWithFx += ! t.effects.empty();
        std::printf ("mixer: %zu edges (%zu with PDC delay), %zu tracks with effects\n", edges, delayed, tracksWithFx);
    }
    lynx::RackRenderer renderer;
    renderer.prepare (sampleRate);

    std::vector<float> left (totalSamples), right (totalSamples);
    const auto started = std::chrono::steady_clock::now();
    double tick = 0.0;

    for (std::size_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        const auto n = (int) std::min<std::size_t> (blockSize, totalSamples - pos);
        const bool playing = tick < (double) lengthTicks;
        renderer.process (state.get(), playing, tick, n, left.data() + pos, right.data() + pos);
        tick += n * tps;
    }

    std::printf ("render: %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count());

    // normalize the peak to -1 dBFS
    float peak = 0.0f;
    for (std::size_t i = 0; i < totalSamples; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    std::printf ("peak before normalizing: %.4f\n", peak);

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
    std::printf ("written to %s\n", argv[2]);
    return 0;
}
