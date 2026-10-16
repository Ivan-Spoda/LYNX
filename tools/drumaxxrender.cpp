// Renders a Drumaxx state with LYNX's engine like the round-67 EXEC harness drives the DLL (verification harness,
// REA_rodada67 §9): drumaxxrender <state.bin> <out.f32> <length> <rate> [blk=N] [note=start,key,initVol,finalVol,cents ...]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/Drumaxx.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: drumaxxrender state.bin out.f32 length rate [blk=N] [note=s,k,iv,fv,cents ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    int blk = 512;
    std::vector<lynx::drumaxx::RefNote> notes;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::drumaxx::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf", &n.start, &n.key, &n.initVol, &n.finalVol, &n.pitchOffset) == 5)
                notes.push_back (n);
        }
    }
    const auto out = lynx::drumaxx::renderReference (state, notes, rate, length, blk);
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
