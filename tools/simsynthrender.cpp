// Renders a SimSynth state with LYNX's engine exactly like the round-64 EXEC harness drives the DLL (verification
// harness, REA_rodada64 §7): simsynthrender <state.bin> <out.f32> <length> <rate> [blk=N]
//   [note=start,key,finalVol,finalPan,pitchCents,release[,fcut,fres] ...]. Writes interleaved stereo float32 and prints
// the random-table index after the set-up.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/SimSynth.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: simsynthrender state.bin out.f32 length rate [blk=N] [note=s,k,fv,fp,pc,off[,fcut,fres] ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    int blk = 512;
    std::vector<lynx::simsynth::RefNote> notes;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::simsynth::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%f,%f,%f,%d,%f,%f", &n.start, &n.key, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release, &n.fcut, &n.fres)
                >= 6)
                notes.push_back (n);
        }
    }
    std::vector<std::int64_t> counters;
    const auto out = lynx::simsynth::renderReference (state, notes, rate, length, blk, &counters);
    if (out.empty())
    {
        std::fprintf (stderr, "state not usable\n");
        return 1;
    }
    std::printf ("ridx %lld chorus %lld created %lld loaded %lld\n", (long long) counters[0], (long long) counters[1], (long long) counters[2], (long long) counters[3]);
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
