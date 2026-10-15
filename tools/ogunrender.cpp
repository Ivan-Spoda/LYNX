// Renders an Ogun state with LYNX's engine exactly like our round-37 reference model drives it (verification harness,
// REA_rodada37 §1.21): ogunrender <state.bin> <out.f32> <length> <rate> [q=N] [tempo=X] [blk=N] [counter=N] [maxpoly=N]
// [kills=0|1] [note=start,key,vel,fvol,fpan,pofs,fcut,fres,relvel,off ...] [j0=s0,s1,...]
// Autogun (REA_rodada57): [autogun=1] [sg=<path of the installation's Autogun\Data\SG.dat>]
// Output: interleaved stereo float32. Prints the render time and the fallbacks the state relies on.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "core/Ogun.h"

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf (stderr, "usage: ogunrender state.bin out.f32 length rate [q=N] [tempo=X] [blk=N] [counter=N] [maxpoly=N] [kills=0|1] "
                              "[note=start,key,vel,fvol,fpan,pofs,fcut,fres,relvel,off ...] [j0=s0,s1,...]\n");
        return 2;
    }
    std::ifstream in (argv[1], std::ios::binary);
    const std::vector<std::uint8_t> state ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    lynx::ogun::RefOptions opt;
    std::vector<lynx::ogun::RefNote> notes;
    for (int i = 5; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto val = [&] { return a.substr (a.find ('=') + 1); };
        if (a.rfind ("q=", 0) == 0)
            opt.quality = std::atoi (val().c_str());
        else if (a.rfind ("tempo=", 0) == 0)
            opt.tempo = std::atof (val().c_str());
        else if (a.rfind ("blk=", 0) == 0)
            opt.block = std::atoi (val().c_str());
        else if (a.rfind ("counter=", 0) == 0)
            opt.counter = std::atoi (val().c_str());
        else if (a.rfind ("maxpoly=", 0) == 0)
            opt.maxPolyphony = std::atoi (val().c_str());
        else if (a.rfind ("autogun=", 0) == 0)
            opt.autogun = std::atoi (val().c_str()) != 0;
        else if (a.rfind ("sg=", 0) == 0)
        {
            std::ifstream sg (val(), std::ios::binary);
            opt.sgState.assign ((std::istreambuf_iterator<char> (sg)), std::istreambuf_iterator<char>());
        }
        else if (a.rfind ("kills=", 0) == 0)
            opt.hostKills = std::atoi (val().c_str()) != 0;
        else if (a.rfind ("j0=", 0) == 0)
        {
            std::stringstream ss (val());
            std::string t;
            while (std::getline (ss, t, ','))
                opt.j0Shift.push_back (std::atoi (t.c_str()));
        }
        else if (a.rfind ("note=", 0) == 0)
        {
            std::stringstream ss (val());
            std::string t;
            std::vector<double> v;
            while (std::getline (ss, t, ','))
                v.push_back (std::atof (t.c_str()));
            v.resize (10, 0.0);
            lynx::ogun::RefNote n;
            n.start = (int) v[0], n.key = (int) v[1], n.velocity = v[2], n.finalVol = v[3], n.finalPan = v[4];
            n.pitchOffset = v[5], n.fcut = v[6], n.fres = v[7], n.releaseVelocity = v[8], n.release = (int) v[9];
            notes.push_back (n);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = lynx::ogun::renderReference (state, notes, rate, length, opt);
    const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    if (out.empty())
    {
        std::fprintf (stderr, "state not usable\n");
        return 1;
    }
    std::ofstream o (argv[2], std::ios::binary);
    o.write ((const char*) out.data(), (std::streamsize) (out.size() * sizeof (float)));
    std::printf ("render_ms=%.1f\n", ms);
    for (const auto& s : lynx::ogunFallbacks (state, opt.autogun))
        std::printf ("fallback: %s\n", s.c_str());
    return 0;
}
