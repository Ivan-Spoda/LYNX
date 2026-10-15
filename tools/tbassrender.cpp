// Renders a Transistor Bass state with LYNX's engine the way the REA round-57 EXEC harness drives FL's engine DLL
// (verification, REA_rodada57 §2.10): tbassrender <state.bin> <engine_x64.dll> <out.f32> <length> <rate> [tempo=X] [blk=N]
// [note=start,key,initVol,finalPan,pitchCents,off[,fcut,fres,channel] ...]   (off = −1: held). Output: interleaved stereo
// float32.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "core/TransistorBass.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: tbassrender state.bin engine_x64.dll out.f32 length rate [tempo=X] [blk=N] "
                              "[note=start,key,initVol,finalPan,pitchCents,off[,fcut,fres,channel] ...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[4]);
    const double rate = std::atof (argv[5]);
    lynx::tbass::RefOptions opt;
    std::vector<lynx::tbass::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto val = [&] { return a.substr (a.find ('=') + 1); };
        if (a.rfind ("tempo=", 0) == 0)
            opt.tempo = std::atof (val().c_str());
        else if (a.rfind ("blk=", 0) == 0)
            opt.block = std::atoi (val().c_str());
        else if (a.rfind ("note=", 0) == 0)
        {
            std::stringstream ss (val());
            std::string t;
            std::vector<double> v;
            while (std::getline (ss, t, ','))
                v.push_back (std::atof (t.c_str()));
            if (v.size() < 6)
                continue;
            lynx::tbass::RefNote n;
            n.start = (int) v[0];
            n.key = (int) v[1];
            n.initVol = v[2];
            n.finalPan = v[3];
            n.pitchCents = v[4];
            n.release = (int) v[5];
            if (v.size() >= 8)
                n.fcut = v[6], n.fres = v[7];
            if (v.size() >= 9)
                n.channel = (int) v[8];
            notes.push_back (n);
        }
    }
    if (! lynx::transistorBassStateUsable (state))
    {
        std::fprintf (stderr, "state not usable\n");
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = lynx::tbass::renderReference (state, argv[2], notes, rate, length, opt);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    if (out.empty())
    {
        std::fprintf (stderr, "render failed (engine DLL not usable?)\n");
        return 1;
    }
    std::ofstream o (argv[3], std::ios::binary);
    o.write (reinterpret_cast<const char*> (out.data()), (std::streamsize) (out.size() * sizeof (float)));
    std::printf ("rendered %d samples in %.1f ms\n", length, ms);
    return 0;
}
