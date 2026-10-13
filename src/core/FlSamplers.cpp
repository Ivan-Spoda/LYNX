#include "core/FlSamplers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>

namespace lynx
{
namespace
{
// chunk = id:u32 tamanho:u64 corpo (FPC e DirectWave); o corpo pode ter sub-chunks
struct Chunk
{
    std::uint32_t id = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

void forEachChunk (const std::uint8_t* p, std::size_t n, const std::function<void (const Chunk&)>& fn)
{
    std::size_t pos = 0;

    while (pos + 12 <= n)
    {
        Chunk c;
        std::uint64_t size = 0;
        std::memcpy (&c.id, p + pos, 4);
        std::memcpy (&size, p + pos + 4, 8);
        pos += 12;

        if (size > n - pos)
            return; // truncado: para aqui

        c.data = p + pos;
        c.size = (std::size_t) size;
        fn (c);
        pos += c.size;
    }
}

std::int32_t i32 (const Chunk& c, std::size_t index)
{
    std::int32_t v = 0;
    if ((index + 1) * 4 <= c.size)
        std::memcpy (&v, c.data + index * 4, 4);
    return v;
}

float f32 (const std::uint8_t* p)
{
    float v = 0.0f;
    std::memcpy (&v, p, 4);
    return std::isfinite (v) ? v : 0.0f;
}

std::string ascii (const Chunk& c)
{
    std::string s (reinterpret_cast<const char*> (c.data), c.size);
    s.erase (std::find (s.begin(), s.end(), '\0'), s.end());
    return s;
}

std::string utf16 (const Chunk& c)
{
    std::string out;

    for (std::size_t i = 0; i + 1 < c.size; i += 2)
    {
        const auto ch = (std::uint32_t) c.data[i] | ((std::uint32_t) c.data[i + 1] << 8);

        if (ch == 0)
            break;
        if (ch < 0x80)
            out += (char) ch;
        else if (ch < 0x800)
        {
            out += (char) (0xC0 | (ch >> 6));
            out += (char) (0x80 | (ch & 0x3F));
        }
        else
        {
            out += (char) (0xE0 | (ch >> 12));
            out += (char) (0x80 | ((ch >> 6) & 0x3F));
            out += (char) (0x80 | (ch & 0x3F));
        }
    }

    return out;
}

double l11 (double x) // (11^x − 1)/10: as curvas de volume do FL
{
    return (std::pow (11.0, std::clamp (x, 0.0, 2.0)) - 1.0) / 10.0;
}
} // namespace

std::optional<std::vector<DrumPad>> fpcPads (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 16)
        return std::nullopt;

    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);

    if (version < 0xF424B || version > 0x1E84E403) // formatos antigos sem chunks: não tratados
        return std::nullopt;

    std::vector<DrumPad> pads;

    forEachChunk (d.data() + 4, d.size() - 4, [&] (const Chunk& top) {
        if (top.id != 2) // pad
            return;

        DrumPad pad;
        bool muted = false;

        forEachChunk (top.data, top.size, [&] (const Chunk& c) {
            if (c.id == 0x64) // 16 × i32 de parâmetros
            {
                pad.gain = (float) l11 (std::clamp (i32 (c, 0), 0, 128) / 127.0);
                pad.pan = std::clamp (i32 (c, 1), -128, 128) / 128.0f;
                muted = (i32 (c, 2) & 1) != 0;
                pad.key = std::clamp (i32 (c, 3), 0, 127);
                pad.mixerOffset = std::clamp (i32 (c, 4), -99, 103);
                pad.cutGroup = i32 (c, 5) >= 0 ? i32 (c, 5) + 1 : 0; // −1 = nenhum; 0 vira o grupo 1
                pad.cutBy = i32 (c, 6) >= 0 ? i32 (c, 6) + 1 : 0;
                pad.tune = std::clamp (i32 (c, 7), -128, 128) * 9.375f / 100.0f;
            }
            else if (c.id == 0x65)
                pad.name = utf16 (c);
            else if (c.id == 0x69) // camada
            {
                SampleZone layer;
                bool hasPath = false;

                forEachChunk (c.data, c.size, [&] (const Chunk& l) {
                    if (l.id == 0x12C)
                    {
                        layer.path = utf16 (l);
                        hasPath = ! layer.path.empty();
                    }
                    else if (l.id == 0x12D)
                    {
                        layer.gain = (float) l11 (std::clamp (i32 (l, 0), 0, 127) / 127.0);
                        layer.lowVelocity = std::clamp (i32 (l, 2), 0, 127);
                        const auto high = std::clamp (i32 (l, 3), 0, 127); // exclusiva, exceto 127
                        layer.highVelocity = high >= 127 ? 127 : std::max (layer.lowVelocity, high - 1);
                        layer.tune = std::clamp (i32 (l, 5), -128, 128) * 9.375f / 100.0f;
                    }
                });

                if (hasPath)
                    pad.layers.push_back (std::move (layer));
            }
        });

        if (! pad.layers.empty() && ! muted)
        {
            if (pad.name.empty())
                pad.name = "Pad " + std::to_string (pads.size() + 1);
            pads.push_back (std::move (pad));
        }
    });

    if (pads.empty())
        return std::nullopt;

    return pads;
}

std::optional<DirectWaveProgram> directWaveProgram (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 16)
        return std::nullopt;

    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);

    if (version < 36 || version > 38)
        return std::nullopt;

    std::optional<DirectWaveProgram> result;

    forEachChunk (d.data() + 4, d.size() - 4, [&] (const Chunk& top) {
        if (top.id != 1 || result) // o primeiro programa com zonas
            return;

        DirectWaveProgram program;
        bool envelopeSet = false;

        forEachChunk (top.data, top.size, [&] (const Chunk& z) {
            if (z.id != 3) // zona
                return;

            SampleZone zone;
            float a = 0.0f, dcy = 0.0f, sus = 1.0f, rel = 0.0f;
            bool hasEnvelope = false;

            forEachChunk (z.data, z.size, [&] (const Chunk& c) {
                if (c.id == 0x1F4 && c.size >= 13) // cabeçalho: raiz, teclas, velocities, ganho
                {
                    zone.rootKey = std::clamp ((int) c.data[0], 0, 127);
                    zone.lowKey = std::clamp ((int) c.data[1], 0, 127);
                    zone.highKey = std::clamp ((int) c.data[2], zone.lowKey, 127);
                    zone.lowVelocity = std::clamp ((int) c.data[3], 0, 127);
                    zone.highVelocity = std::clamp ((int) c.data[4], zone.lowVelocity, 127);
                    const auto g = std::clamp (f32 (c.data + 9), 0.0f, 2.0f);
                    zone.gain = g * g; // ganho = v²
                }
                else if (c.id == 0x1F6)
                    zone.path = ascii (c);
                else if (c.id == 0x1F8 && c.size >= 4)
                    zone.tune = (std::clamp (f32 (c.data), 0.0f, 1.0f) - 0.5f) * 24.0f; // (v − 0,5)·2400 cents
                else if (c.id == 0x1FD && c.size >= 16)
                {
                    auto time = [] (float v) { return 10.0f * std::pow (std::clamp (v, 0.0f, 1.0f), 4.0f); }; // 10·v⁴ s
                    a = time (f32 (c.data));
                    dcy = time (f32 (c.data + 4));
                    sus = std::clamp (f32 (c.data + 8), 0.0f, 1.0f);
                    rel = time (f32 (c.data + 12));
                    hasEnvelope = true;
                }
            });

            if (zone.path.empty())
                return;

            if (hasEnvelope && ! envelopeSet)
            {
                program.attack = a;
                program.decay = dcy;
                program.sustain = sus;
                program.release = rel;
                envelopeSet = true;
            }

            program.zones.push_back (std::move (zone));
        });

        if (! program.zones.empty())
            result = std::move (program);
    });

    return result;
}
} // namespace lynx
