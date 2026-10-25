#include "core/Mp3Decoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wsign-compare"
 #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
 #if ! defined(__clang__)
  #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
 #endif
#endif
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_STDIO
#define MINIMP3_NOSKIP_APEV2 // minimp3 removes 32 bytes too many (the size already counts the footer): done below
#include "../../third_party/minimp3/minimp3_ex.h"
#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif

namespace lynx::mp3
{
namespace
{
bool startsWith (const std::uint8_t* p, std::size_t size, const char* magic)
{
    const auto n = std::strlen (magic);
    return size >= n && std::memcmp (p, magic, n) == 0;
}

// Every ID3v2 tag at the start (some taggers write two in a row); returns the offset of what follows.
std::size_t skipId3v2Tags (const std::uint8_t* data, std::size_t size)
{
    std::size_t pos = 0;
    while (pos < size)
    {
        const auto tag = mp3dec_skip_id3v2 (data + pos, size - pos);
        if (tag == 0)
            break;
        pos += std::min (tag, size - pos);
    }
    return pos;
}

// The end of the audio: ID3v1 (and its "TAG+" extension), APEv2 and Lyrics3 v2 tags removed, in any order.
std::size_t audioEnd (const std::uint8_t* data, std::size_t begin, std::size_t size)
{
    auto end = size;
    for (int pass = 0; pass < 3 && end > begin; ++pass)
    {
        const auto before = end;
        auto rest = end - begin;
        mp3dec_skip_id3v1 (data + begin, &rest); // ID3v1 and "TAG+" (minimp3_ex)
        end = begin + rest;
        // APEv2 footer: its size counts the items and the footer, plus a 32-byte header when flag bit 31 says there is one
        if (end - begin > 32 && std::memcmp (data + end - 32, "APETAGEX", 8) == 0)
        {
            const auto* f = data + end - 32;
            const auto le32 = [f] (int at) {
                return (std::size_t) f[at] | ((std::size_t) f[at + 1] << 8) | ((std::size_t) f[at + 2] << 16) | ((std::size_t) f[at + 3] << 24);
            };
            const auto tagBytes = le32 (12) + ((le32 (20) & (1u << 31)) != 0 ? 32u : 0u);
            if (tagBytes >= 32 && tagBytes <= end - begin)
                end -= tagBytes;
        }
        // Lyrics3 v2: "...LYRICS200" with a 6-digit size before it
        if (end - begin > 15 && std::memcmp (data + end - 9, "LYRICS200", 9) == 0)
        {
            std::size_t n = 0;
            bool digits = true;
            for (int i = 0; i < 6; ++i)
            {
                const auto c = data[end - 15 + (std::size_t) i];
                digits = digits && c >= '0' && c <= '9';
                n = n * 10 + (std::size_t) (c - '0');
            }
            if (digits && n + 15 <= end - begin)
                end -= n + 15;
        }
        if (end == before)
            break;
    }
    return end;
}

// A plausible MPEG audio header (sync, layer, bitrate and rate fields not reserved).
bool validHeader (const std::uint8_t* h)
{
    return h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && (h[1] & 0x18) != 0x08 && (h[1] & 0x06) != 0 && (h[2] & 0xF0) != 0xF0
           && (h[2] & 0x0C) != 0x0C;
}
} // namespace

bool looksLikeMp3 (const std::uint8_t* head, std::size_t size)
{
    if (head == nullptr || size < 4)
        return false;
    for (const auto* magic : { "RIFF", "RIFX", "RF64", "FORM", "fLaC", "OggS", "wvpk", "caff", "MThd" })
        if (startsWith (head, size, magic))
            return false;
    if (mp3dec_skip_id3v2 (head, size) != 0)
        return true;

    // two headers in a row within the first bytes (free-format frames: minimp3's own frame search)
    int freeFormat = 0, frameBytes = 0;
    const auto at = mp3d_find_frame (head, (int) std::min<std::size_t> (size, 1 << 20), &freeFormat, &frameBytes);
    return frameBytes > 0 && (std::size_t) at + 4 <= size && validHeader (head + at);
}

bool decode (const std::uint8_t* data, std::size_t size, Decoded& out, Timing timing)
{
    out = {};
    if (data == nullptr || size < 4)
        return false;

    const auto begin = skipId3v2Tags (data, size);
    const auto end = audioEnd (data, begin, size);
    if (end <= begin)
        return false;

    const auto* buf = data + begin;
    const auto total = end - begin;
    std::size_t pos = 0;

    // the first frame: a Xing/Info header is a silent frame that only describes the stream
    std::uint32_t taggedFrames = 0;
    int delay = 0, padding = 0;
    {
        int freeFormat = 0, frameBytes = 0;
        const auto window = (int) std::min<std::size_t> (total, 1 << 24);
        const auto at = mp3d_find_frame (buf, window, &freeFormat, &frameBytes);
        if (frameBytes <= 0)
            return false;
        if (const auto* first = buf + at; HDR_GET_LAYER (first) == 1 /* layer III */)
        {
            const auto tag = mp3dec_check_vbrtag (buf + at, frameBytes, &taggedFrames, &delay, &padding);
            if (tag != 0)
            {
                out.info.xingHeader = true;
                out.info.headerFrameSamples = hdr_frame_samples (first);
                pos = (std::size_t) at + (std::size_t) frameBytes;
            }
            if (tag > 0 && (delay > 0 || padding != 0))
                out.info.lameHeader = true;
            if (tag <= 0)
                taggedFrames = 0, delay = padding = 0;
            if (out.info.lameHeader)
                out.info.encoderDelay = delay, out.info.encoderPadding = padding;
        }
    }

    mp3dec_t dec;
    mp3dec_init (&dec);
    std::vector<float> pcm ((std::size_t) MINIMP3_MAX_SAMPLES_PER_FRAME);
    std::uint32_t audioFrames = 0;
    std::int64_t decoded = 0;

    // reserve from the stream's size and the first frame's bitrate
    bool reserved = false;

    while (pos < total)
    {
        mp3dec_frame_info_t info {};
        const auto chunk = (int) std::min<std::size_t> (total - pos, 1 << 24);
        const auto samples = mp3dec_decode_frame (&dec, buf + pos, chunk, pcm.data(), &info);
        if (info.frame_bytes <= 0)
            break;
        pos += (std::size_t) info.frame_bytes;
        if (samples <= 0)
        {
            // junk skipped (no header: hz 0), or a frame that could not be decoded. Before the first audio this is
            // the bit reservoir filling up; later a damaged frame keeps its place as silence (the rest stays in time)
            if (info.hz <= 0 || decoded == 0 || info.hz != out.info.sampleRate)
                continue;
            const auto silent = info.layer == 1 ? 384 : (info.layer == 3 && info.hz < 32000 ? 576 : 1152);
            out.left.insert (out.left.end(), (std::size_t) silent, 0.0f);
            if (out.info.channels > 1)
                out.right.insert (out.right.end(), (std::size_t) silent, 0.0f);
            decoded += silent;
            ++audioFrames;
            continue;
        }

        if (out.info.sampleRate == 0)
        {
            out.info.sampleRate = info.hz;
            out.info.channels = std::clamp (info.channels, 1, 2);
            out.info.layer = info.layer;
            out.info.firstBitrateKbps = info.bitrate_kbps;
        }
        // a frame at another sample rate (a stream glued to another one) is left out
        if (info.hz != out.info.sampleRate)
            continue;
        ++audioFrames;

        if (! reserved && info.bitrate_kbps > 0)
        {
            const auto seconds = (double) total * 8.0 / (1000.0 * info.bitrate_kbps);
            const auto estimate = (std::size_t) std::min (seconds * info.hz * 1.05 + 4096.0, 2.0e9);
            out.left.reserve (estimate);
            if (out.info.channels > 1)
                out.right.reserve (estimate);
            reserved = true;
        }

        const auto stereoFrame = info.channels > 1;
        for (int i = 0; i < samples; ++i)
        {
            const auto l = pcm[(std::size_t) (stereoFrame ? 2 * i : i)];
            const auto r = stereoFrame ? pcm[(std::size_t) (2 * i + 1)] : l;
            if (out.info.channels > 1)
            {
                out.left.push_back (l);
                out.right.push_back (r);
            }
            else
                out.left.push_back (stereoFrame ? 0.5f * (l + r) : l);
        }
        decoded += samples;
    }

    if (decoded <= 0 || out.info.sampleRate <= 0)
    {
        out = {};
        return false;
    }

    if (timing == Timing::flStudio)
    {
        // FL: the header frame plays as a frame of silence, nothing is trimmed
        if (const auto lead = (std::size_t) out.info.headerFrameSamples; lead > 0)
        {
            out.left.insert (out.left.begin(), lead, 0.0f);
            if (! out.right.empty())
                out.right.insert (out.right.begin(), lead, 0.0f);
            out.info.leadingSilence = (int) lead;
        }
    }
    else
    {
        // gapless: the encoder's delay (with the decoder's 529) at the start; its padding at the end only when the
        // stream has the frames the header announced (a cut file keeps everything it has)
        auto first = (std::int64_t) std::max (0, delay);
        auto last = decoded;
        if (out.info.lameHeader && padding > 0 && audioFrames == taggedFrames)
            last = std::max (first, decoded - padding);
        first = std::min (first, last);
        out.info.trimmedStart = (int) first;
        out.info.trimmedEnd = (int) (decoded - last);

        auto trim = [first, last] (std::vector<float>& v) {
            if (v.empty())
                return;
            v.erase (v.begin() + (std::ptrdiff_t) last, v.end());
            v.erase (v.begin(), v.begin() + (std::ptrdiff_t) first);
            v.shrink_to_fit();
        };
        trim (out.left);
        trim (out.right);
    }
    out.info.frames = (std::int64_t) out.left.size();
    return out.info.frames > 0;
}

bool decodeReference (const std::uint8_t* data, std::size_t size, Decoded& out)
{
    out = {};
    mp3dec_t dec;
    mp3dec_file_info_t info {};
    if (data == nullptr || mp3dec_load_buf (&dec, data, size, &info, nullptr, nullptr) != 0 || info.buffer == nullptr || info.samples == 0
        || info.channels < 1)
    {
        std::free (info.buffer);
        return false;
    }
    const auto channels = (std::size_t) info.channels;
    const auto frames = info.samples / channels;
    out.info.sampleRate = info.hz;
    out.info.channels = (int) std::min<std::size_t> (channels, 2);
    out.info.layer = info.layer;
    out.left.resize (frames);
    if (channels > 1)
        out.right.resize (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        out.left[i] = info.buffer[i * channels];
        if (channels > 1)
            out.right[i] = info.buffer[i * channels + 1];
    }
    std::free (info.buffer);
    out.info.frames = (std::int64_t) frames;
    return frames > 0;
}
} // namespace lynx::mp3
