#include "core/Mp3Encoder.h"

#include <algorithm>

#include "lame.h"

namespace lynx
{
namespace
{
bool plainAscii (const std::string& s)
{
    return std::all_of (s.begin(), s.end(), [] (char c) { return (unsigned char) c >= 32 && (unsigned char) c < 127; });
}
} // namespace

Mp3Encoder::~Mp3Encoder()
{
    if (handle != nullptr)
        lame_close ((lame_t) handle);
}

bool Mp3Encoder::open (int inputRate, int outputRate, int numChannels, int kbps, std::string& error,
                       const std::string& title, const std::string& artist)
{
    auto* gf = lame_init();
    if (gf == nullptr)
    {
        error = "the MP3 encoder could not start";
        return false;
    }

    channels = numChannels == 1 ? 1 : 2;
    lame_set_in_samplerate (gf, inputRate);
    lame_set_out_samplerate (gf, outputRate);
    lame_set_num_channels (gf, channels);
    lame_set_mode (gf, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate (gf, kbps);
    lame_set_quality (gf, 2); // near-best psychoacoustics
    lame_set_bWriteVbrTag (gf, 0); // constant bitrate: no Xing header to patch afterwards

    if ((! title.empty() && plainAscii (title)) || (! artist.empty() && plainAscii (artist)))
    {
        id3tag_init (gf);
        id3tag_add_v2 (gf);
        if (! title.empty() && plainAscii (title))
            id3tag_set_title (gf, title.c_str());
        if (! artist.empty() && plainAscii (artist))
            id3tag_set_artist (gf, artist.c_str());
    }

    if (lame_init_params (gf) < 0)
    {
        lame_close (gf);
        error = "the MP3 encoder does not accept these settings";
        return false;
    }

    handle = gf;
    return true;
}

bool Mp3Encoder::encode (const float* left, const float* right, int n, std::vector<std::uint8_t>& out)
{
    if (handle == nullptr || n <= 0)
        return handle != nullptr;

    const auto start = out.size();
    const auto room = (std::size_t) (1.25 * n + 7200);
    out.resize (start + room);
    const auto written = lame_encode_buffer_ieee_float ((lame_t) handle, left, channels == 1 ? left : right, n,
                                                        out.data() + start, (int) room);
    out.resize (start + (std::size_t) std::max (0, written));
    return written >= 0;
}

bool Mp3Encoder::finish (std::vector<std::uint8_t>& out)
{
    if (handle == nullptr)
        return false;
    const auto start = out.size();
    out.resize (start + 7200);
    const auto written = lame_encode_flush ((lame_t) handle, out.data() + start, 7200);
    out.resize (start + (std::size_t) std::max (0, written));
    return written >= 0;
}

Mp3Scan scanMp3 (const std::vector<std::uint8_t>& b)
{
    Mp3Scan scan;
    std::size_t pos = 0;

    if (b.size() >= 10 && b[0] == 'I' && b[1] == 'D' && b[2] == '3') // ID3v2: synch-safe size
        pos = 10 + (((std::size_t) b[6] & 0x7f) << 21 | ((std::size_t) b[7] & 0x7f) << 14 | ((std::size_t) b[8] & 0x7f) << 7 | ((std::size_t) b[9] & 0x7f));

    static const int rates1[] = { 44100, 48000, 32000 };
    static const int kbps1[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
    bool mixed = false;

    while (pos + 4 <= b.size())
    {
        if (b[pos] != 0xff || (b[pos + 1] & 0xe0) != 0xe0)
            break;
        const auto version = (b[pos + 1] >> 3) & 3; // 3 = MPEG-1
        const auto layer = (b[pos + 1] >> 1) & 3;   // 1 = layer III
        const auto bitIndex = b[pos + 2] >> 4, rateIndex = (b[pos + 2] >> 2) & 3, padding = (b[pos + 2] >> 1) & 1;
        if (version != 3 || layer != 1 || bitIndex == 0 || bitIndex == 15 || rateIndex == 3)
            break;
        const auto kbps = kbps1[bitIndex], rate = rates1[rateIndex];
        if (scan.frames == 0)
        {
            scan.kbps = kbps;
            scan.sampleRate = rate;
            scan.channels = (b[pos + 3] >> 6) == 3 ? 1 : 2;
        }
        else if (kbps != scan.kbps)
            mixed = true;
        ++scan.frames;
        pos += (std::size_t) (144000 * kbps / rate + padding);
    }

    if (mixed)
        scan.kbps = 0;
    return scan;
}
} // namespace lynx
