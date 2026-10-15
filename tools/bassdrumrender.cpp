// Renders a BassDrum state with LYNX's engine exactly like our round-43 reference model drives it (verification
// harness, REA_rodada43 §2.10): bassdrumrender <state.bin> <out.f32> <length> <rate> <BassDrum folder> [blk=N] [nonoise]
//   [note=start,key,initVol,finalVol,finalPan,pitchCents,release ...]. Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/BassDrum.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: bassdrumrender state.bin out.f32 length rate folder [blk=N] [nonoise] [note=s,k,iv,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    int blk = 512;
    bool noise = true;
    std::vector<lynx::bassdrum::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a == "nonoise")
            noise = false;
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::bassdrum::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::bassdrum::renderReference (state, notes, rate, length, argv[5], blk, noise);
    if (out.empty())
    {
        std::fprintf (stderr, "state or table not usable\n");
        return 1;
    }
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
