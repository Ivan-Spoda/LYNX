// Renders a Harmor state with LYNX's engine exactly like our round-34 reference model drives it (verification
// harness, REA_rodada34 §1.25): harmorrender <state.bin> <out.f32> <length> <mt.txt|-> <rate> [blk=N] [bpm=X]
//   [note=start,key,velocity,finalVol,finalPan,release ...]. Writes interleaved stereo float32; mt.txt holds the
// DLL's Mersenne-Twister state (index, then 624 words) or "-" for LYNX's fixed seed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Harmor.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: harmorrender state.bin out.f32 length mt.txt|- rate [blk=N] [bpm=X] [note=s,k,v,fv,fp,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    std::vector<std::uint32_t> mt;
    if (std::strcmp (argv[4], "-") != 0)
    {
        std::ifstream m (argv[4]);
        unsigned long long v = 0;
        while (m >> v)
            mt.push_back ((std::uint32_t) v);
    }
    const double rate = std::atof (argv[5]);
    int blk = 512;
    double bpm = 120.0;
    std::vector<lynx::harmor::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("bpm=", 0) == 0)
            bpm = std::atof (a.c_str() + 4);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::harmor::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%d", &n.start, &n.key, &n.velocity, &n.finalVol, &n.finalPan, &n.release) >= 5)
                notes.push_back (n);
        }
    }
    const auto out = lynx::harmor::renderReference (state, notes, rate, length, blk, mt, bpm);
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
    for (const auto& s : lynx::harmorFallbacks (state))
        std::printf ("fallback: %s\n", s.c_str());
    return 0;
}
