// Renders a MiniSynth state with LYNX's engine exactly like our round-41 reference model drives it (verification
// harness, REA_rodada41 §8): minisynthrender <state.bin> <out.f32> <length> <rate> <oscdata.dat> [blk=N] [bpm=X]
//   [maxpoly=N] [songpos=X] [panlaw] [note=start,key,initVol,finalVol,finalPan,pitchCents,release ...].
// Writes interleaved stereo float32.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/MiniSynth.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: minisynthrender state.bin out.f32 length rate oscdata.dat [blk=N] [bpm=X] [maxpoly=N] [songpos=X] [panlaw] [note=s,k,iv,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    const std::string oscdata = argv[5];
    int blk = 512, maxPoly = 0;
    double bpm = 120.0, songPos = -1.0;
    bool panLaw = false;
    std::vector<lynx::minisynth::RefNote> notes;
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
        else if (a == "panlaw")
            panLaw = true;
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::minisynth::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%lf,%d", &n.start, &n.key, &n.initVol, &n.finalVol, &n.finalPan, &n.pitchCents,
                             &n.release)
                >= 6)
                notes.push_back (n);
        }
    }
    const auto out = lynx::minisynth::renderReference (state, notes, rate, length, oscdata, blk, bpm, maxPoly, songPos, panLaw);
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
