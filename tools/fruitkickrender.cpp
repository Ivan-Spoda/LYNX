// Renders a Fruit Kick state with LYNX's engine exactly like our round-43 reference model drives it (verification
// harness, REA_rodada43 §1.9): fruitkickrender <state.bin> <out.f32> <length> <rate> [blk=N] [tick=first,period]
//   [tempo=X] [render] [note=start,key,finalVol,finalPan,pitchCents,release ...]. Writes interleaved stereo float32;
//   `render` = as while LYNX exports (FL's file-render waveshaper routine, REA_rodada55 §2.3).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/FruitKick.h"

namespace lynx
{
static bool renderFlag = false;
bool offlineRendering() noexcept { return renderFlag; } // this tool links the engine alone
} // namespace lynx

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: fruitkickrender state.bin out.f32 length rate [blk=N] [tick=first,period] [tempo=X] [note=s,k,fv,fp,pc,off ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    int blk = 512;
    double tempo = 140.0;
    std::vector<int> ticks;
    std::vector<lynx::fruitkick::RefNote> notes;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        int first = 0, period = 0;
        if (a == "render")
            lynx::renderFlag = true;
        else if (a.rfind ("blk=", 0) == 0)
            blk = std::atoi (a.c_str() + 4);
        else if (a.rfind ("tempo=", 0) == 0)
            tempo = std::atof (a.c_str() + 6);
        else if (std::sscanf (a.c_str(), "tick=%d,%d", &first, &period) == 2 && period > 0)
            for (int t = first; t < length; t += period)
                ticks.push_back (t);
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::fruitkick::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%lf,%lf,%lf,%d", &n.start, &n.key, &n.finalVol, &n.finalPan, &n.pitchCents, &n.release) >= 5)
                notes.push_back (n);
        }
    }
    const auto out = lynx::fruitkick::renderReference (state, notes, rate, length, blk, ticks, tempo);
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
