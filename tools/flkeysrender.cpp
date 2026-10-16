// flkeysrender: renders FL Keys notes with LYNX's port (core/FlKeys), for comparison with the installation's plugin
// (REA_rodada69 §7). Reads the wavetable from FL's folder: raw files and 16-bit PCM WAVs (not Ogg-compressed ones).
// Usage: flkeysrender <Wavtables folder> <state.bin> <out.f32> <rate> <length> <block> [law=x87] [dump=<file>]
//        events: on3=<start>,<key>,<init vol>,<final vol>,<final pan>,<cents>  off=<sample>,<note index>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/FlKeys.h"

using namespace lynx;

static std::vector<std::uint8_t> readAll (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), {} };
}

static std::string unixPath (std::string p)
{
    for (auto& c : p)
        if (c == '\\')
            c = '/';
    return p;
}

// 16-bit PCM WAV → interleaved samples
static flkeys::FileSamples pcm (const std::vector<std::uint8_t>& w)
{
    flkeys::FileSamples f;
    std::uint16_t format = 0, channels = 1, bits = 0;
    for (std::size_t pos = 12; pos + 8 <= w.size();)
    {
        std::uint32_t size = 0;
        std::memcpy (&size, w.data() + pos + 4, 4);
        if (std::memcmp (w.data() + pos, "fmt ", 4) == 0)
        {
            std::memcpy (&format, w.data() + pos + 8, 2);
            std::memcpy (&channels, w.data() + pos + 10, 2);
            std::memcpy (&bits, w.data() + pos + 22, 2);
        }
        else if (std::memcmp (w.data() + pos, "data", 4) == 0 && format == 1 && bits == 16)
        {
            const auto n = std::min<std::size_t> (size, w.size() - pos - 8) / 2;
            f.samples.resize (n);
            std::memcpy (f.samples.data(), w.data() + pos + 8, n * 2);
            f.channels = channels;
        }
        pos += 8 + size + (size & 1);
    }
    return f;
}

int main (int argc, char** argv)
{
    if (argc < 7)
    {
        std::fprintf (stderr, "usage: flkeysrender <Wavtables> <state.bin> <out.f32> <rate> <length> <block> [law=x87] events...\n");
        return 1;
    }
    const std::string folder = argv[1];
    const auto state = flkeys::parseState (readAll (argv[2]));
    if (! state)
        return 2;
    const auto wti = readAll (folder + "/" + state->wavetable + ".wti");
    std::vector<std::string> names;
    const auto table = flkeys::parseWti (std::string (wti.begin(), wti.end()), names);
    if (! table)
        return 3;
    // (Ogg-compressed files cannot be read here: pass wtdump= for those wavetables)
    auto t = *table;
    std::vector<flkeys::FileSamples> files;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        const auto bytes = readAll (folder + "/" + unixPath (names[i]));
        if (t.raw)
        {
            flkeys::FileSamples f;
            f.samples.resize (bytes.size() / 2);
            std::memcpy (f.samples.data(), bytes.data(), f.samples.size() * 2);
            files.push_back (std::move (f));
        }
        else
        {
            t.files[i] = flkeys::wavFileInfo (bytes);
            files.push_back (pcm (bytes));
        }
    }
    auto wt = flkeys::buildWavetable (t, files);
    for (int i = 7; i < argc; ++i)
        if (std::strncmp (argv[i], "wtdump=", 7) == 0)
        {
            // the plugin's own loaded wavetable, dumped by fk.exe dumpwt= (r69/keys/run/fk.c)
            const auto b = readAll (argv[i] + 7);
            std::int32_t h[8];
            std::memcpy (h, b.data(), 32);
            auto w = std::make_shared<flkeys::Wavetable>();
            std::size_t p = 32;
            for (int k = 0; k < h[0]; ++k, p += 0x24)
            {
                flkeys::Keygroup g;
                std::memcpy (&g, b.data() + p, 20);
                w->keygroups.push_back (g);
            }
            w->thresholds.resize ((std::size_t) h[1]);
            std::memcpy (w->thresholds.data(), b.data() + p, 4 * (std::size_t) h[1]);
            p += 4 * (std::size_t) h[1];
            w->stereo = h[2] != 0, w->releaseHighNotes = h[5] != 0;
            std::memcpy (&w->sampleRate, &h[3], 4);
            std::memcpy (&w->velocityFactor, &h[4], 4);
            w->data.resize ((std::size_t) h[6] / 2 + 16);
            std::memcpy (w->data.data(), b.data() + p, std::min<std::size_t> ((std::size_t) h[6] + 16, b.size() - p));
            wt = w;
        }
    if (wt == nullptr)
        return 4;
    bool law = false;
    std::vector<flkeys::RefNote> notes;
    for (int i = 7; i < argc; ++i)
    {
        flkeys::RefNote n;
        float iv, fv, fp, ce;
        int a, b;
        if (std::strcmp (argv[i], "law=x87") == 0)
            law = true;
        else if (std::strncmp (argv[i], "dump=", 5) == 0)
        {
            // the same layout as fk.exe dumpwt= (r69/keys/run/fk.c)
            std::FILE* f = std::fopen (argv[i] + 5, "wb");
            std::int32_t h[8] = { (std::int32_t) wt->keygroups.size(), (std::int32_t) wt->thresholds.size(), wt->stereo ? 1 : 0, 0, 0,
                                  wt->releaseHighNotes ? 1 : 0, 0, 0 };
            std::memcpy (&h[3], &wt->sampleRate, 4);
            std::memcpy (&h[4], &wt->velocityFactor, 4);
            const auto ch = wt->stereo ? 2 : 1;
            const auto frames = wt->data.size() / (std::size_t) ch - 4;
            h[6] = (std::int32_t) (frames * (std::size_t) ch * 2);
            std::fwrite (h, 4, 8, f);
            for (const auto& k : wt->keygroups)
            {
                std::uint8_t r[0x24] {};
                std::memcpy (r, &k, 20);
                std::fwrite (r, 1, 0x24, f);
            }
            std::fwrite (wt->thresholds.data(), 4, wt->thresholds.size(), f);
            std::fwrite (wt->data.data(), 2, frames * (std::size_t) ch, f);
            std::fclose (f);
        }
        else if (std::sscanf (argv[i], "on3=%d,%d,%f,%f,%f,%f", &a, &b, &iv, &fv, &fp, &ce) == 6)
        {
            n.start = a, n.key = b, n.initVol = iv, n.finalVol = fv, n.finalPan = fp, n.cents = ce;
            notes.push_back (n);
        }
        else if (std::sscanf (argv[i], "off=%d,%d", &a, &b) == 2 && b >= 0 && b < (int) notes.size())
            notes[(std::size_t) b].release = a;
    }
    const auto out = flkeys::renderReference (*wt, *state, notes, std::atof (argv[4]), std::atoi (argv[5]), std::atoi (argv[6]), law);
    std::FILE* f = std::fopen (argv[3], "wb");
    std::fwrite (out.data(), 4, out.size(), f);
    std::fclose (f);
    return 0;
}
