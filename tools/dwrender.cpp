// Renders a DirectWave state through LYNX's engine (src/core/DirectWave) for comparison with the DLL (REA round 72).
// usage: dwrender <state.bin> <zonedump.bin|-> <out.f32> <fs> <frames> <block> [prog=<i>] [seed=<n>] [on=n,key,vel,fvol,fpan,cents] [off=n,id]
// zonedump: what r72/run/dw.exe dumpzones= writes (program record, then per zone the 0x41a record and its samples)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "core/DirectWave.h"

using namespace lynx;

static std::vector<std::uint8_t> readAll (const char* p)
{
    std::ifstream f (p, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
}

int main (int argc, char** argv)
{
    if (argc < 7) { std::fprintf (stderr, "usage\n"); return 2; }
    const auto st = dw::parseState (readAll (argv[1]));
    if (! st) { std::fprintf (stderr, "state not usable\n"); return 1; }
    int pi = 0; unsigned seed = 1;
    std::vector<dw::RefNote> notes;
    for (int i = 7; i < argc; ++i)
    {
        dw::RefNote n; int a, id;
        if (std::sscanf (argv[i], "prog=%d", &a) == 1) pi = a;
        else if (std::sscanf (argv[i], "seed=%u", &seed) == 1) {}
        else if (std::sscanf (argv[i], "on=%d,%d,%lf,%lf,%lf,%lf", &n.start, &n.key, &n.velocity, &n.finalVol, &n.finalPan, &n.cents) == 6) notes.push_back (n);
        else if (std::sscanf (argv[i], "off=%d,%d", &a, &id) == 2 && id >= 0 && id < (int) notes.size()) notes[(std::size_t) id].release = a;
    }
    const auto& prog = st->programs[(std::size_t) pi];
    std::vector<std::shared_ptr<const dw::ZoneSamples>> samples;
    if (std::strcmp (argv[2], "-") != 0)
    {
        const auto zd = readAll (argv[2]);
        std::size_t o = 0xd60;
        for (const auto& z : prog.zones)
        {
            std::int32_t rec[4] {};
            if (o + 16 <= zd.size()) std::memcpy (rec, zd.data() + o, 16);
            o += 0x41a;
            auto s = std::make_shared<dw::ZoneSamples>();
            s->bits = rec[3]; // the loaded file's format, not the state's
            s->frames = rec[0];
            s->channels = rec[1];
            const auto n = (std::size_t) rec[0] + 512;
            for (int c = 0; c < rec[1]; ++c)
            {
                if (s->bits == 32)
                {
                    std::vector<float> v (n);
                    if (o + 4 * n <= zd.size()) std::memcpy (v.data(), zd.data() + o, 4 * n);
                    o += 4 * n;
                    s->f32.push_back (std::move (v));
                }
                else
                {
                    std::vector<std::int16_t> v (n);
                    if (o + 2 * n <= zd.size()) std::memcpy (v.data(), zd.data() + o, 2 * n);
                    o += 2 * n;
                    s->i16.push_back (std::move (v));
                }
            }
            samples.push_back (s);
        }
    }
    dw::EngineOptions opt;
    opt.velocityAsInitVol = (st->flags & 2) != 0;
    opt.noGainClamp = (st->flags & 1) != 0;
    const auto out = dw::renderReference (prog, samples, opt, notes, std::atof (argv[4]), std::atoi (argv[5]), std::atoi (argv[6]), seed);
    std::FILE* f = std::fopen (argv[3], "wb");
    std::fwrite (out.data(), 4, out.size(), f);
    std::fclose (f);
    return 0;
}
