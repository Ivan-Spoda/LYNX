#include "core/WavPack.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C"
{
#include "wavpack.h"
}

namespace lynx
{
namespace
{
struct Memory
{
    const std::uint8_t* data;
    std::int64_t size, pos = 0;
    int pushed = -1;
};

std::int32_t readBytes (void* id, void* dest, std::int32_t count)
{
    auto& m = *static_cast<Memory*> (id);
    auto* out = static_cast<std::uint8_t*> (dest);
    std::int32_t done = 0;
    if (m.pushed >= 0 && count > 0)
    {
        *out++ = (std::uint8_t) m.pushed;
        m.pushed = -1;
        ++done;
    }
    const auto n = (std::int32_t) std::min<std::int64_t> (count - done, std::max<std::int64_t> (0, m.size - m.pos));
    std::memcpy (out, m.data + m.pos, (std::size_t) n);
    m.pos += n;
    return done + n;
}

std::int64_t getPos (void* id)
{
    auto& m = *static_cast<Memory*> (id);
    return m.pushed >= 0 ? m.pos - 1 : m.pos;
}

int setPosAbs (void* id, std::int64_t pos)
{
    auto& m = *static_cast<Memory*> (id);
    m.pushed = -1;
    m.pos = std::clamp<std::int64_t> (pos, 0, m.size);
    return 0;
}

int setPosRel (void* id, std::int64_t delta, int mode)
{
    auto& m = *static_cast<Memory*> (id);
    const auto base = mode == SEEK_SET ? 0 : mode == SEEK_END ? m.size : getPos (id);
    return setPosAbs (id, base + delta);
}

int pushBackByte (void* id, int c)
{
    auto& m = *static_cast<Memory*> (id);
    if (m.pos <= 0)
        return EOF;
    --m.pos;
    m.pushed = -1;
    return c;
}

std::int64_t getLength (void* id) { return static_cast<Memory*> (id)->size; }
int canSeek (void*) { return 1; }
} // namespace

bool decodeWavPack (const std::uint8_t* data, std::size_t size, DecodedAudio& out)
{
    if (data == nullptr || size < 32 || std::memcmp (data, "wvpk", 4) != 0)
        return false;

    Memory memory { data, (std::int64_t) size };
    WavpackStreamReader64 reader {};
    reader.read_bytes = readBytes;
    reader.get_pos = getPos;
    reader.set_pos_abs = setPosAbs;
    reader.set_pos_rel = setPosRel;
    reader.push_back_byte = pushBackByte;
    reader.get_length = getLength;
    reader.can_seek = canSeek;

    char error[80] {};
    auto* wpc = WavpackOpenFileInputEx64 (&reader, &memory, nullptr, error, OPEN_NORMALIZE | OPEN_2CH_MAX, 0);
    if (wpc == nullptr)
        return false;

    const auto channels = WavpackGetNumChannels (wpc);
    const auto total = WavpackGetNumSamples64 (wpc);
    const auto isFloat = (WavpackGetMode (wpc) & MODE_FLOAT) != 0;
    const auto bits = WavpackGetBitsPerSample (wpc);
    out.sampleRate = WavpackGetSampleRate (wpc);

    if (channels < 1 || channels > 2 || total <= 0 || total > (std::int64_t) 1 << 31 || out.sampleRate < 1000.0 || bits < 1 || bits > 32)
    {
        WavpackCloseFile (wpc);
        return false;
    }

    out.channels.assign ((std::size_t) channels, std::vector<float> ((std::size_t) total));
    const auto scale = isFloat ? 1.0f : 1.0f / (float) (1u << (bits - 1)); // inteiros alinhados à direita
    std::vector<std::int32_t> buffer (4096 * (std::size_t) channels);
    std::int64_t done = 0;
    while (done < total)
    {
        const auto want = (std::uint32_t) std::min<std::int64_t> (4096, total - done);
        const auto got = WavpackUnpackSamples (wpc, buffer.data(), want);
        if (got == 0)
            break;
        for (std::uint32_t i = 0; i < got; ++i)
            for (int c = 0; c < channels; ++c)
            {
                const auto raw = buffer[(std::size_t) i * (std::size_t) channels + (std::size_t) c];
                float v = 0.0f;
                if (isFloat)
                    std::memcpy (&v, &raw, 4);
                else
                    v = (float) raw * scale;
                out.channels[(std::size_t) c][(std::size_t) (done + i)] = std::isfinite (v) ? v : 0.0f;
            }
        done += got;
    }
    WavpackCloseFile (wpc);

    for (auto& ch : out.channels)
        ch.resize ((std::size_t) done);
    return done > 0;
}
} // namespace lynx
