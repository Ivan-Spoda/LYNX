#include "core/WaveAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lynx::wave
{
namespace
{
// 2nd-order Butterworth section (RBJ cookbook, Q = 1/sqrt 2), transposed direct form II, mono
struct Section
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Section make (double hz, double rate, bool highPass) noexcept
    {
        const auto w = 2.0 * 3.14159265358979323846 * std::min (hz, rate * 0.45) / rate;
        const auto cw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.70710678118654752);
        const auto a0 = 1.0 + alpha;
        Section s;
        if (highPass)
        {
            s.b0 = (1.0 + cw) / 2.0 / a0;
            s.b1 = -(1.0 + cw) / a0;
        }
        else
        {
            s.b0 = (1.0 - cw) / 2.0 / a0;
            s.b1 = (1.0 - cw) / a0;
        }
        s.b2 = s.b0;
        s.a1 = -2.0 * cw / a0;
        s.a2 = (1.0 - alpha) / a0;
        return s;
    }

    double process (double x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

std::int16_t quantiseSigned (float v, float scale) noexcept
{
    return (std::int16_t) std::lround (std::clamp (v / scale, -1.0f, 1.0f) * 32767.0f);
}

std::uint16_t quantiseUnsigned (double v, float scale) noexcept
{
    return (std::uint16_t) std::lround (std::clamp (v / (double) scale, 0.0, 1.0) * 65535.0);
}

float rms16 (double sumOfSquares, std::size_t n) noexcept
{
    return n == 0 ? 0.0f : (float) std::sqrt (sumOfSquares / (double) n);
}

// merges `count` columns into one (min of mins, max of maxes, RMS of RMS values)
Column merge (const Column* c, std::size_t count) noexcept
{
    Column out;
    for (int ch = 0; ch < 2; ++ch)
    {
        out.lo[ch] = c[0].lo[ch];
        out.hi[ch] = c[0].hi[ch];
    }
    double rms[2] {}, band[3] {};
    for (std::size_t i = 0; i < count; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            out.lo[ch] = std::min (out.lo[ch], c[i].lo[ch]);
            out.hi[ch] = std::max (out.hi[ch], c[i].hi[ch]);
            rms[ch] += (double) c[i].rms[ch] * c[i].rms[ch];
        }
        for (int b = 0; b < 3; ++b)
            band[b] += (double) c[i].band[b] * c[i].band[b];
    }
    for (int ch = 0; ch < 2; ++ch)
        out.rms[ch] = (std::uint16_t) std::lround (std::min (65535.0, std::sqrt (rms[ch] / (double) count)));
    for (int b = 0; b < 3; ++b)
        out.band[b] = (std::uint16_t) std::lround (std::min (65535.0, std::sqrt (band[b] / (double) count)));
    return out;
}

template <typename T>
void put (std::vector<std::uint8_t>& out, T v)
{
    const auto at = out.size();
    out.resize (at + sizeof (T));
    std::memcpy (out.data() + at, &v, sizeof (T));
}

template <typename T>
bool get (const std::uint8_t*& p, const std::uint8_t* end, T& v)
{
    if ((std::size_t) (end - p) < sizeof (T))
        return false;
    std::memcpy (&v, p, sizeof (T));
    p += sizeof (T);
    return true;
}

constexpr std::uint32_t magic = 0x5657594c; // "LYWV"
static_assert (sizeof (Column) == 18, "Column is written raw to the disk cache");
} // namespace

std::size_t Summary::bucketOf (std::size_t level) const noexcept
{
    auto b = bucket;
    for (std::size_t i = 0; i < level; ++i)
        b *= levelFactor;
    return b;
}

std::size_t Summary::memoryBytes() const noexcept
{
    std::size_t total = sizeof (Summary);
    for (const auto& l : levels)
        total += l.size() * sizeof (Column);
    return total;
}

Point Summary::query (double fromFrame, double toFrame, int channel) const noexcept
{
    Point p;
    if (levels.empty() || levels[0].empty() || fromFrame >= (double) frames || toFrame <= 0.0)
        return p;

    fromFrame = std::max (0.0, fromFrame);
    toFrame = std::min ((double) frames, std::max (toFrame, fromFrame));
    const auto span = toFrame - fromFrame;
    const auto sampleScale = scale / 32767.0f, levelScale = scale / 65535.0f;
    const auto right = channels > 1 ? 1 : 0;

    auto read = [&] (const Column& c, float& lo, float& hi, float& rms) {
        if (channel < 0)
        {
            lo = (float) std::min (c.lo[0], c.lo[right]) * sampleScale;
            hi = (float) std::max (c.hi[0], c.hi[right]) * sampleScale;
            const auto r0 = (float) c.rms[0], r1 = (float) c.rms[right];
            rms = std::sqrt ((r0 * r0 + r1 * r1) * 0.5f) * levelScale;
        }
        else
        {
            const auto ch = channel > 0 ? right : 0;
            lo = (float) c.lo[ch] * sampleScale;
            hi = (float) c.hi[ch] * sampleScale;
            rms = (float) c.rms[ch] * levelScale;
        }
    };

    if (span < (double) bucket)
    {
        // closer than one column per pixel: interpolate between neighbouring column centres
        const auto& base = levels[0];
        const auto pos = std::max (0.0, (fromFrame + toFrame) * 0.5 / (double) bucket - 0.5);
        const auto i0 = std::min ((std::size_t) pos, base.size() - 1);
        const auto i1 = std::min (i0 + 1, base.size() - 1);
        const auto t = (float) std::clamp (pos - (double) i0, 0.0, 1.0);
        float lo0, hi0, r0, lo1, hi1, r1;
        read (base[i0], lo0, hi0, r0);
        read (base[i1], lo1, hi1, r1);
        p.lo = lo0 + (lo1 - lo0) * t;
        p.hi = hi0 + (hi1 - hi0) * t;
        p.rms = r0 + (r1 - r0) * t;
        p.low = ((float) base[i0].band[0] + ((float) base[i1].band[0] - (float) base[i0].band[0]) * t) * levelScale;
        p.mid = ((float) base[i0].band[1] + ((float) base[i1].band[1] - (float) base[i0].band[1]) * t) * levelScale;
        p.high = ((float) base[i0].band[2] + ((float) base[i1].band[2] - (float) base[i0].band[2]) * t) * levelScale;
        p.valid = true;
        return p;
    }

    // the coarsest level whose columns are still no wider than the range: a few columns per pixel
    std::size_t level = 0;
    while (level + 1 < levels.size() && (double) bucketOf (level + 1) <= span)
        ++level;

    const auto& columns = levels[level];
    const auto b = (double) bucketOf (level);
    const auto first = std::min ((std::size_t) (fromFrame / b), columns.size() - 1);
    const auto last = std::clamp ((std::size_t) std::ceil (toFrame / b), first + 1, columns.size());

    float lo = 0.0f, hi = 0.0f;
    double sumRms = 0.0, sumBand[3] {};
    for (auto i = first; i < last; ++i)
    {
        float cLo, cHi, cRms;
        read (columns[i], cLo, cHi, cRms);
        lo = i == first ? cLo : std::min (lo, cLo);
        hi = i == first ? cHi : std::max (hi, cHi);
        sumRms += (double) cRms * cRms;
        for (int k = 0; k < 3; ++k)
            sumBand[k] += (double) columns[i].band[k] * columns[i].band[k];
    }

    const auto n = (double) (last - first);
    p.lo = lo;
    p.hi = hi;
    p.rms = (float) std::sqrt (sumRms / n);
    p.low = (float) std::sqrt (sumBand[0] / n) * levelScale;
    p.mid = (float) std::sqrt (sumBand[1] / n) * levelScale;
    p.high = (float) std::sqrt (sumBand[2] / n) * levelScale;
    p.valid = true;
    return p;
}

Summary analyse (const float* left, const float* right, std::size_t frames, double sampleRate, const std::atomic<bool>* cancel)
{
    Summary s;
    s.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    s.frames = frames;
    s.channels = right != nullptr && right != left ? 2 : 1;

    if (left == nullptr || frames == 0)
    {
        s.frames = 0;
        return s;
    }

    if (s.channels == 1)
        right = left;

    while (frames / s.bucket > maxBaseColumns)
        s.bucket *= 2;

    float peak = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    s.peak = peak;
    s.scale = std::max (1.0f, peak);

    // crossovers: low = LP 250 x2, mid = HP 250 x2 + LP 4k x2, high = HP 4k x2
    Section lowLp[2] = { Section::make (lowCrossoverHz, s.sampleRate, false), Section::make (lowCrossoverHz, s.sampleRate, false) };
    Section midHp[2] = { Section::make (lowCrossoverHz, s.sampleRate, true), Section::make (lowCrossoverHz, s.sampleRate, true) };
    Section midLp[2] = { Section::make (highCrossoverHz, s.sampleRate, false), Section::make (highCrossoverHz, s.sampleRate, false) };
    Section highHp[2] = { Section::make (highCrossoverHz, s.sampleRate, true), Section::make (highCrossoverHz, s.sampleRate, true) };

    auto& base = s.levels.emplace_back();
    base.resize ((frames + s.bucket - 1) / s.bucket);

    for (std::size_t c = 0; c < base.size(); ++c)
    {
        if (cancel != nullptr && (c & 255) == 0 && cancel->load (std::memory_order_relaxed))
            return {};

        const auto from = c * s.bucket, to = std::min (frames, from + s.bucket);
        float lo[2] = { left[from], right[from] }, hi[2] = { left[from], right[from] };
        double sq[2] {}, band[3] {};

        for (auto i = from; i < to; ++i)
        {
            const auto l = left[i], r = right[i];
            lo[0] = std::min (lo[0], l);
            hi[0] = std::max (hi[0], l);
            lo[1] = std::min (lo[1], r);
            hi[1] = std::max (hi[1], r);
            sq[0] += (double) l * l;
            sq[1] += (double) r * r;

            const auto mono = 0.5 * ((double) l + (double) r);
            const auto lowBand = lowLp[1].process (lowLp[0].process (mono));
            const auto midBand = midLp[1].process (midLp[0].process (midHp[1].process (midHp[0].process (mono))));
            const auto highBand = highHp[1].process (highHp[0].process (mono));
            band[0] += lowBand * lowBand;
            band[1] += midBand * midBand;
            band[2] += highBand * highBand;
        }

        const auto n = to - from;
        auto& col = base[c];
        for (int ch = 0; ch < 2; ++ch)
        {
            col.lo[ch] = quantiseSigned (lo[ch], s.scale);
            col.hi[ch] = quantiseSigned (hi[ch], s.scale);
            col.rms[ch] = quantiseUnsigned (rms16 (sq[ch], n), s.scale);
        }
        for (int k = 0; k < 3; ++k)
            col.band[k] = quantiseUnsigned (rms16 (band[k], n), s.scale);
    }

    // coarser levels until one column covers everything
    while (s.levels.back().size() > 1)
    {
        const auto& below = s.levels.back();
        std::vector<Column> next ((below.size() + levelFactor - 1) / levelFactor);
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            const auto from = i * levelFactor;
            next[i] = merge (below.data() + from, std::min (levelFactor, below.size() - from));
        }
        s.levels.push_back (std::move (next));
    }

    return s;
}

std::vector<std::uint8_t> serialise (const Summary& s)
{
    std::vector<std::uint8_t> out;
    out.reserve (64 + s.memoryBytes());
    put (out, magic);
    put (out, formatVersion);
    put (out, (std::uint64_t) s.frames);
    put (out, s.sampleRate);
    put (out, (std::uint32_t) s.channels);
    put (out, (std::uint64_t) s.bucket);
    put (out, s.scale);
    put (out, s.peak);
    put (out, (std::uint32_t) s.levels.size());
    for (const auto& level : s.levels)
    {
        put (out, (std::uint64_t) level.size());
        const auto at = out.size();
        out.resize (at + level.size() * sizeof (Column));
        if (! level.empty())
            std::memcpy (out.data() + at, level.data(), level.size() * sizeof (Column));
    }
    return out;
}

bool deserialise (const std::uint8_t* data, std::size_t size, Summary& out)
{
    const auto* p = data;
    const auto* end = data + size;
    std::uint32_t m = 0, version = 0, channels = 0, levelCount = 0;
    std::uint64_t frames = 0, bucket = 0;
    Summary s;

    if (data == nullptr || ! get (p, end, m) || m != magic || ! get (p, end, version) || version != formatVersion)
        return false;
    if (! get (p, end, frames) || ! get (p, end, s.sampleRate) || ! get (p, end, channels) || ! get (p, end, bucket)
        || ! get (p, end, s.scale) || ! get (p, end, s.peak) || ! get (p, end, levelCount))
        return false;
    if (frames == 0 || (channels != 1 && channels != 2) || bucket < minBucket || (bucket & (bucket - 1)) != 0 || levelCount == 0
        || levelCount > 64 || ! (s.sampleRate > 0.0 && s.sampleRate < 1.0e7) || ! (s.scale >= 1.0f) || ! std::isfinite (s.peak))
        return false;

    s.frames = (std::size_t) frames;
    s.channels = (int) channels;
    s.bucket = (std::size_t) bucket;
    auto expected = (s.frames + s.bucket - 1) / s.bucket;

    for (std::uint32_t l = 0; l < levelCount; ++l)
    {
        std::uint64_t count = 0;
        if (! get (p, end, count) || count != expected || (std::size_t) (end - p) / sizeof (Column) < count)
            return false;
        auto& level = s.levels.emplace_back ((std::size_t) count);
        std::memcpy (level.data(), p, (std::size_t) count * sizeof (Column));
        p += count * sizeof (Column);
        expected = (expected + levelFactor - 1) / levelFactor;
    }

    if (p != end || s.levels.back().size() != 1)
        return false;

    out = std::move (s);
    return true;
}

BandMix bandMix (float low, float mid, float high) noexcept
{
    // weighted amplitudes, then a gentle contrast (power 1.5) so a dominant band reads clearly
    const auto l = std::pow (std::max (0.0f, low), 1.5f);
    const auto m = std::pow (std::max (0.0f, mid) * 1.6f, 1.5f);
    const auto h = std::pow (std::max (0.0f, high) * 3.2f, 1.5f);
    const auto sum = l + m + h;

    if (! (sum > 1.0e-12f))
        return { 0.0f, 1.0f, 0.0f }; // silence: neutral
    return { l / sum, m / sum, h / sum };
}

Rgb spectralColour (const BandMix& mix) noexcept
{
    Rgb c { lowColour.r * mix.low + midColour.r * mix.mid + highColour.r * mix.high,
            lowColour.g * mix.low + midColour.g * mix.mid + highColour.g * mix.high,
            lowColour.b * mix.low + midColour.b * mix.mid + highColour.b * mix.high };

    // blends of opposite anchors drift to grey: push saturation back up and keep the colour bright
    const auto grey = (c.r + c.g + c.b) / 3.0f;
    c.r = grey + (c.r - grey) * 1.3f;
    c.g = grey + (c.g - grey) * 1.3f;
    c.b = grey + (c.b - grey) * 1.3f;
    const auto top = std::max ({ c.r, c.g, c.b });
    if (top > 0.0f && top < 0.9f)
    {
        const auto k = 0.9f / top;
        c.r *= k;
        c.g *= k;
        c.b *= k;
    }
    c.r = std::clamp (c.r, 0.0f, 1.0f);
    c.g = std::clamp (c.g, 0.0f, 1.0f);
    c.b = std::clamp (c.b, 0.0f, 1.0f);
    return c;
}
} // namespace lynx::wave
