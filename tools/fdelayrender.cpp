// Renders FL's Fruity Delay 2 / Fruity Delay 3 engines (core/FruityDelay) on a raw stereo float file, for comparing
// with the plugins (REA_rodada52). The Delay 3 set-up follows FL's wrapper: state, then the sample rate twice.
// usage: fdelayrender d2|d3 <state.bin> <in.f32> <out.f32> <sampleRate> <bpm> <block> [settle]
#include "core/FruityDelay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace lynx::fldelay;

static std::vector<std::uint8_t> readAll (const char* path)
{
    std::vector<std::uint8_t> v;
    if (auto* f = std::fopen (path, "rb"))
    {
        std::uint8_t b[65536];
        std::size_t n;
        while ((n = std::fread (b, 1, sizeof b, f)) > 0)
            v.insert (v.end(), b, b + n);
        std::fclose (f);
    }
    return v;
}

int main (int argc, char** argv)
{
    if (argc < 8)
    {
        std::fprintf (stderr, "usage: fdelayrender d2|d3 state.bin in.f32 out.f32 sampleRate bpm block [settle]\n");
        return 2;
    }
    const auto state = readAll (argv[2]);
    const auto in = readAll (argv[3]);
    const double rate = std::atof (argv[5]), bpm = std::atof (argv[6]);
    const int block = std::max (1, std::atoi (argv[7]));
    const bool settle = argc > 8 && std::strcmp (argv[8], "settle") == 0;
    const std::size_t n = in.size() / 8;
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        std::memcpy (&l[i], &in[8 * i], 4);
        std::memcpy (&r[i], &in[8 * i + 4], 4);
    }
    if (std::strcmp (argv[1], "d3") == 0)
    {
        const auto s = parseDelay3 (state);
        if (! s)
            return 3;
        Delay3Engine e (rate);
        e.setTempo (bpm);
        for (int i = 0; i < delay3Controls; ++i)
            e.setParameter (i, delay3EngineValue (i, s->v[(std::size_t) i]));
        e.setSampleRate (rate);
        e.setSampleRate (rate);
        if (settle)
            e.settle();
        for (std::size_t s0 = 0; s0 < n; s0 += (std::size_t) block)
            e.process (&l[s0], &r[s0], (int) std::min ((std::size_t) block, n - s0));
    }
    else
    {
        const auto s = parseDelay2 (state);
        if (! s)
            return 3;
        Delay2Engine e (*s, rate, bpm, block, settle);
        for (std::size_t s0 = 0; s0 < n; s0 += (std::size_t) block)
            e.process (&l[s0], &r[s0], (int) std::min ((std::size_t) block, n - s0));
    }
    if (auto* f = std::fopen (argv[4], "wb"))
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            std::fwrite (&l[i], 4, 1, f);
            std::fwrite (&r[i], 4, 1, f);
        }
        std::fclose (f);
    }
    return 0;
}
