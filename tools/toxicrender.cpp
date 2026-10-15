// Renders a Toxic Biohazard state with LYNX's engine exactly like our round-45 reference model drives it (verification
// harness, REA_rodada45 §5): toxicrender <state.bin> <out.f32> <length> <rate> <dllPath> [blk=N] [bpm=X] [maxpoly=N]
//   [nofx] [note=start,key,initVol,finalVol,finalPan,pitchCents,release ...]. Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Toxic.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: toxicrender state.bin out.f32 length rate dllPath [blk=N] [bpm=X] [maxpoly=N] [nofx] [note=s,k,iv,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    const std::string dll = argv[5];
    int blk = 512, maxPoly = 0;
    double bpm = 125.0;
    bool fx = true;
    std::vector<lynx::toxic::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("bpm=", 0) == 0)
            bpm = std::atof (a.c_str() + 4);
        else if (a.rfind ("maxpoly=", 0) == 0)
            maxPoly = std::atoi (a.c_str() + 8);
        else if (a == "nofx")
            fx = false;
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::toxic::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::toxic::renderReference (state, notes, rate, length, dll, blk, bpm, maxPoly, fx);
    if (out.empty())
    {
        std::fprintf (stderr, "state not usable (or its waves need the DLL)\n");
        return 1;
    }
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
