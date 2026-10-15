// Renders a Fruity DX10 state with LYNX's engine exactly like our round-42 reference model drives it (verification
// harness, REA_rodada42): dx10render <state.bin> <out.f32> <length> <rate> [blk=N] [maxpoly=N]
//   [note=start,key,initVol,finalVol,finalPan,pitchOffset,release ...]. Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Dx10.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: dx10render state.bin out.f32 length rate [blk=N] [maxpoly=N] [note=s,k,iv,fv,fp,po,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    int blk = 512, maxPoly = 0;
    std::vector<lynx::dx10::RefNote> notes;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("maxpoly=", 0) == 0)
            maxPoly = std::atoi (a.c_str() + 8);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::dx10::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchOffset,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::dx10::renderReference (state, notes, rate, length, blk, maxPoly);
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
