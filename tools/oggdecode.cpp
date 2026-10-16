// oggdecode: decodes Ogg-in-WAV / Ogg Vorbis files as FL Studio does (src/core/OggVorbis.h) and prints, per file,
// the FNV-1a 64 hash of the planar float32 samples, the frame count, the channels and the sample rate.
// Usage: oggdecode [-o out.f32] file...   (-o writes the first file's planar float32 samples)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/OggVorbis.h"

int main (int argc, char** argv)
{
    std::string outPath;
    int first = 1;
    if (argc > 2 && std::strcmp (argv[1], "-o") == 0)
        outPath = argv[2], first = 3;

    for (int a = first; a < argc; ++a)
    {
        std::ifstream in (argv[a], std::ios::binary);
        const std::vector<std::uint8_t> bytes { std::istreambuf_iterator<char> (in), {} };
        lynx::oggvorbis::Decoded d;
        if (! lynx::oggvorbis::decode (bytes.data(), bytes.size(), d))
        {
            std::printf ("FAIL | %s\n", argv[a]);
            continue;
        }
        std::uint64_t h = 1469598103934665603ULL;
        for (const auto& ch : d.data)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*> (ch.data());
            for (std::size_t i = 0; i < ch.size() * sizeof (float); ++i)
                h = (h ^ p[i]) * 1099511628211ULL;
        }
        std::printf ("H %016llx %zu %d %d | %s\n", (unsigned long long) h, d.data[0].size(), d.channels, d.sampleRate, argv[a]);
        if (! outPath.empty() && a == first)
        {
            std::ofstream out (outPath, std::ios::binary);
            for (const auto& ch : d.data)
                out.write (reinterpret_cast<const char*> (ch.data()), (std::streamsize) (ch.size() * sizeof (float)));
        }
    }
    return 0;
}
