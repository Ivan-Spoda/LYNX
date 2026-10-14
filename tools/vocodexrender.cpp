// Runs LYNX's Vocodex engine (core/Vocodex.h) on raw signals, for comparison with the REA round-35 reference model.
// Usage: vocodexrender <state.bin> <mod.f32> <car.f32|-> <own.f32|-> <out.f32> <fs> <block> [modk=1] [cark=2] [draft=0|1]
// mod/car/own: interleaved stereo float32. `mod` arrives on side input `modk`, `car` on `cark`, `own` is the track's
// own input; the state's ModIn/CarIn select among them (0 = own input, CarIn < 0 = internal synth → silence).
// The Soundgoodizer stage is not applied (the engine only).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/Vocodex.h"

namespace
{
std::vector<float> readF32 (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<char> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    std::vector<float> out (bytes.size() / sizeof (float));
    std::memcpy (out.data(), bytes.data(), out.size() * sizeof (float));
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 8)
    {
        std::fprintf (stderr, "usage: vocodexrender state.bin mod.f32 car.f32|- own.f32|- out.f32 fs block [modk=1 cark=2 draft=0|1]\n");
        return 1;
    }
    std::ifstream stateFile (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> raw ((std::istreambuf_iterator<char> (stateFile)), std::istreambuf_iterator<char>());
    const auto state = lynx::vocodex::parseState (raw);
    if (! state)
    {
        std::fprintf (stderr, "not a Vocodex state\n");
        return 1;
    }
    const auto mod = readF32 (argv[2]);
    const auto car = std::strcmp (argv[3], "-") != 0 ? readF32 (argv[3]) : std::vector<float> {};
    const auto own = std::strcmp (argv[4], "-") != 0 ? readF32 (argv[4]) : std::vector<float> {};
    const int fs = std::atoi (argv[6]), block = std::atoi (argv[7]);
    int modk = 1, cark = 2, draft = -1;
    for (int i = 8; i < argc; ++i)
    {
        char key[64];
        int value = 0;
        if (std::sscanf (argv[i], "%63[^=]=%d", key, &value) == 2)
        {
            if (std::strcmp (key, "modk") == 0)
                modk = value;
            else if (std::strcmp (key, "cark") == 0)
                cark = value;
            else if (std::strcmp (key, "draft") == 0)
                draft = value;
        }
    }

    lynx::vocodex::Engine engine;
    engine.prepare (*state, fs, block, draft >= 0 ? draft != 0 : state->draft());
    const auto frames = (long) mod.size() / 2;
    std::vector<float> out ((std::size_t) frames * 2), zero ((std::size_t) block * 2), lm (zero.size()), lc (zero.size());
    for (long n0 = 0; n0 < frames; n0 += block)
    {
        const int len = (int) std::min<long> (block, frames - n0);
        const float* m = mod.data() + 2 * n0;
        const float* c = car.empty() ? zero.data() : car.data() + 2 * n0;
        const float* o = own.empty() ? zero.data() : own.data() + 2 * n0;
        const float *mm, *cc;
        if (state->lrEncoding)
        {
            for (int k = 0; k < len; ++k)
            {
                lm[(std::size_t) (2 * k)] = lm[(std::size_t) (2 * k + 1)] = o[2 * k];
                lc[(std::size_t) (2 * k)] = lc[(std::size_t) (2 * k + 1)] = o[2 * k + 1];
            }
            mm = lm.data(), cc = lc.data();
        }
        else
        {
            auto pick = [&] (int index) { return index == 0 ? o : index == modk ? m : index == cark ? c : o; };
            mm = pick (state->modIn);
            cc = state->carIn < 0 ? zero.data() : pick (state->carIn);
        }
        engine.process (o, mm, cc, out.data() + 2 * n0, len);
    }
    std::FILE* f = std::fopen (argv[5], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
