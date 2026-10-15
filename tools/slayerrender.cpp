// Renders FL Slayer parameters with LYNX's engine exactly like the round-68 EXEC harness drives the DLL (verification
// harness, REA_rodada68 §7): slayerrender <dll> <out.f32> <length> <rate> p0,p1,…,p28 [blk=N] [tempo=T]
//   [note=start,key,velocity,release ...]. Writes interleaved stereo float32 (raw engine output).
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "core/Slayer.h"

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: slayerrender dll out.f32 length rate p0,…,p28 [blk=N] [tempo=T] [note=s,k,v,off ...]\n");
        return 2;
    }
    const int length = std::atoi (argv[3]);
    const double rate = std::atof (argv[4]);
    std::vector<int> params;
    {
        std::stringstream ss (argv[5]);
        std::string item;
        while (std::getline (ss, item, ','))
            params.push_back (std::atoi (item.c_str()));
    }
    lynx::slayer::RefOptions opt;
    std::vector<lynx::slayer::RefNote> notes;
    for (int i = 6; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("blk=", 0) == 0)
            opt.block = std::atoi (a.c_str() + 4);
        else if (a.rfind ("tempo=", 0) == 0)
            opt.tempo = std::atof (a.c_str() + 6);
        else if (a.rfind ("midi=", 0) == 0)
        {
            std::array<int, 4> m {};
            if (std::sscanf (a.c_str() + 5, "%d,%d,%d,%d", &m[0], &m[1], &m[2], &m[3]) == 4)
                opt.midi.push_back (m);
        }
        else if (a.rfind ("peek=", 0) == 0)
            opt.peek.push_back ((int) std::strtol (a.c_str() + 5, nullptr, 16));
        else if (a.rfind ("note=", 0) == 0)
        {
            lynx::slayer::RefNote n;
            if (std::sscanf (a.c_str() + 5, "%d,%d,%d,%d", &n.start, &n.key, &n.velocity, &n.release) >= 3)
                notes.push_back (n);
        }
    }
    const auto out = lynx::slayer::renderReference (params, argv[1], notes, rate, length, opt);
    if (out.empty())
    {
        std::fprintf (stderr, "not usable\n");
        return 1;
    }
    std::FILE* f = std::fopen (argv[2], "wb");
    if (f == nullptr)
        return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    return 0;
}
