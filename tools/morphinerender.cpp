// Renders a Morphine state with LYNX's engine exactly like our round-39 reference model drives it (verification
// harness, REA_rodada39 §3.2): morphinerender <state.bin> <out.f32> <length> <rate> <noise.wav|-> [blk=N] [bpm=X]
//   [ratechange] [note=start,key,initVol,finalVol,finalPan,pitchCents,release ...]. Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Morphine.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: morphinerender state.bin out.f32 length rate noise.wav|- [blk=N] [bpm=X] [ratechange] [note=s,k,iv,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    const std::string noise = std::strcmp (argv[5], "-") == 0 ? std::string() : std::string (argv[5]);
    int blk = 512;
    double bpm = 120.0;
    bool rateChange = false;
    std::vector<lynx::morphine::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("bpm=", 0) == 0)
            bpm = std::atof (a.c_str() + 4);
        else if (a == "ratechange")
            rateChange = true;
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::morphine::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::morphine::renderReference (state, notes, rate, length, noise, blk, bpm, rateChange);
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
