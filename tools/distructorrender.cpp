// Renders an interleaved float32 stereo file through LYNX's Distructor engine (core/DistructorFx), the way FL calls
// the plugin: state load, sample rate, then blocks. Used to compare against the installation's plugin (REA round 71).
// usage: distructorrender <state.bin> <in.f32> <out.f32> [rate] [block] [module|plugin] [impulse folder]
//   module: the state is a single Module_x64 state block instead of a whole Distructor state
//   impulse folder: the installation's Plugins/Fruity/Effects/Distructor/module/impulses
#include "core/DistructorFx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "usage: distructorrender <state.bin> <in.f32> <out.f32> [rate] [block] [module]\n");
        return 2;
    }
    std::ifstream sf (argv[1], std::ios::binary);
    std::vector<std::uint8_t> st ((std::istreambuf_iterator<char> (sf)), {});
    std::ifstream inf (argv[2], std::ios::binary);
    std::vector<char> raw ((std::istreambuf_iterator<char> (inf)), {});
    std::vector<float> x (raw.size() / 4);
    std::memcpy (x.data(), raw.data(), x.size() * 4);
    const double rate = argc > 4 ? std::atof (argv[4]) : 44100.0;
    const int blk = argc > 5 ? std::atoi (argv[5]) : 512;
    const bool module = argc > 6 && std::strcmp (argv[6], "module") == 0;
    if (argc > 7)
        lynx::distructor::setImpulseFolder (argv[7]);
    lynx::distructor::State s;
    if (module)
    {
        auto m = lynx::distructor::parseModule (st.data(), st.size());
        if (! m)
            return 3;
        s.modules.push_back (*m);
        s.clip = false;
    }
    else
    {
        auto p = lynx::distructor::parse (st);
        if (! p)
            return 3;
        s = *p;
    }
    lynx::distructor::Engine e;
    // like the EXEC harness: the state is loaded before the host sets the rate (at 44100)
    e.prepare (rate, blk, std::getenv ("DISTRUCTOR_STATE_RATE") ? std::atof (std::getenv ("DISTRUCTOR_STATE_RATE")) : 0.0);
    e.set (s);
    const auto n = (int) (x.size() / 2);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = x[(std::size_t) (2 * i)], r[(std::size_t) i] = x[(std::size_t) (2 * i + 1)];
    for (int i = 0; i < n; i += blk)
        e.process (l.data() + i, r.data() + i, std::min (blk, n - i));
    for (int i = 0; i < n; ++i)
        x[(std::size_t) (2 * i)] = l[(std::size_t) i], x[(std::size_t) (2 * i + 1)] = r[(std::size_t) i];
    std::ofstream of (argv[3], std::ios::binary);
    of.write ((const char*) x.data(), (std::streamsize) (x.size() * 4));
}
