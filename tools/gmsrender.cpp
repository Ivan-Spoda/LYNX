// Renders a GMS state with LYNX's engine exactly like our round-39 reference model drives it (verification harness,
// REA_rodada39 §3.1): gmsrender <state.bin> <out.f32> <length> <rate> <wavesDir> [blk=N] [bpm=X] [maxpoly=N]
//   [songpos=X] [nofx] [note=start,key,initVol,finalVol,finalPan,pitchCents,release ...]. Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Gms.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: gmsrender state.bin out.f32 length rate wavesDir [blk=N] [bpm=X] [maxpoly=N] [songpos=X] [nofx] [note=s,k,iv,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    const std::string waves = argv[5];
    int blk = 512, maxPoly = 0;
    double bpm = 140.0, songPos = -1.0;
    bool fx = true;
    std::vector<lynx::gms::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("bpm=", 0) == 0)
            bpm = std::atof (a.c_str() + 4);
        else if (a.rfind ("maxpoly=", 0) == 0)
            maxPoly = std::atoi (a.c_str() + 8);
        else if (a.rfind ("songpos=", 0) == 0)
            songPos = std::atof (a.c_str() + 8);
        else if (a == "nofx")
            fx = false;
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::gms::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::gms::renderReference (state, notes, rate, length, waves, blk, bpm, maxPoly, songPos, fx);
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
