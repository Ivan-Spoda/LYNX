// Renders a 3x Osc state with LYNX's engine exactly like our round-47 reference model drives it (verification harness,
// REA_rodada47): threeoscrender <state.bin> <out.f32> <length> <rate> [blk=N] [maxpoly=N] [tick=N] [ppq=N] [tempo=X]
//   [seed=N] [custom=wave.f32 | wav=file.wav] [note=start,key,finalVol,finalPan,pitchOffset,release ...]. The state is
//   normalised first (version 14, its own snapshot). Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/ThreeOsc.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: threeoscrender state.bin out.f32 length rate [blk=N] [maxpoly=N] [tick=N] [ppq=N] [tempo=X] [seed=N] "
                              "[custom=wave.f32|wav=file.wav] [note=s,k,fv,fp,po,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> raw ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto state = lynx::threeOscNormalisedState (raw, {});
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    lynx::threeosc::RefOptions opt;
    std::vector<lynx::threeosc::RefNote> notes;
    std::vector<float> custom;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            opt.block = std::atoi (a.c_str() + 4);
        else if (a.rfind ("maxpoly=", 0) == 0)
            opt.maxPoly = std::atoi (a.c_str() + 8);
        else if (a.rfind ("tick=", 0) == 0)
            opt.tick = std::atoi (a.c_str() + 5);
        else if (a.rfind ("ppq=", 0) == 0)
            opt.ppq = std::atoi (a.c_str() + 4);
        else if (a.rfind ("tempo=", 0) == 0)
            opt.tempo = std::atof (a.c_str() + 6);
        else if (a.rfind ("seed=", 0) == 0)
            opt.seed = (std::uint32_t) std::strtoul (a.c_str() + 5, nullptr, 10);
        else if (a.rfind ("custom=", 0) == 0)
        {
            std::ifstream w (a.substr (7), std::ios::binary);
            custom.assign (16384, 0.0f);
            w.read (reinterpret_cast<char*> (custom.data()), 16384 * 4);
        }
        else if (a.rfind ("wav=", 0) == 0)
            custom = lynx::threeOscCustomWave (a.substr (4));
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::threeosc::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%d", &n.start, &n.key, &n.finalVol, &n.finalPan, &n.pitchOffset, &n.release) >= 5)
                notes.push_back (n);
        }
    }
    const auto out = lynx::threeosc::renderReference (state, notes, rate, length, opt, custom);
    if (out.empty())
    {
        std::fprintf (stderr, "state not usable\n");
        return 1;
    }
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
