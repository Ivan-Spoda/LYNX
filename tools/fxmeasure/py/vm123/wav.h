#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <cstdlib>
struct Wav { int sr = 48000, ch = 2; std::vector<float> l, r; };
inline Wav readWav (const std::string& path)
{
    Wav w; FILE* f = std::fopen (path.c_str(), "rb"); if (! f) { std::perror (path.c_str()); std::exit (1); }
    std::vector<unsigned char> b; unsigned char buf[65536]; size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0) b.insert (b.end(), buf, buf + n);
    std::fclose (f);
    size_t p = 12; int bits = 0, tag = 0;
    while (p + 8 <= b.size())
    {
        uint32_t sz; std::memcpy (&sz, &b[p + 4], 4);
        if (! std::memcmp (&b[p], "fmt ", 4)) { tag = b[p + 8] | b[p + 9] << 8; w.ch = b[p + 10] | b[p + 11] << 8; std::memcpy (&w.sr, &b[p + 12], 4); bits = b[p + 22]; }
        if (! std::memcmp (&b[p], "data", 4))
        {
            if (tag != 3 && tag != 0xFFFE) { std::fprintf (stderr, "need float\n"); std::exit (1); }
            (void) bits; size_t frames = sz / 4 / (size_t) w.ch; w.l.resize (frames); w.r.resize (frames);
            const float* d = (const float*) &b[p + 8];
            for (size_t i = 0; i < frames; ++i) { w.l[i] = d[i * w.ch]; w.r[i] = d[i * w.ch + (w.ch > 1 ? 1 : 0)]; }
        }
        p += 8 + sz + (sz & 1);
    }
    return w;
}
inline void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr = 48000)
{
    FILE* f = std::fopen (path.c_str(), "wb"); uint32_t frames = (uint32_t) l.size(), data = frames * 8, riff = 36 + data, fmt = 16, rate = sr, bps = sr * 8; uint16_t t = 3, c = 2, ba = 8, bits = 32;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f); std::fwrite (&fmt, 4, 1, f);
    std::fwrite (&t, 2, 1, f); std::fwrite (&c, 2, 1, f); std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&data, 4, 1, f);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite (&l[i], 4, 1, f); std::fwrite (&r[i], 4, 1, f); }
    std::fclose (f);
}
