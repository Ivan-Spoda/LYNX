#include "core/FrequencySplitter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Denormals.h"
#include "core/ParametricEqLinear.h"

// bit-exactness needs every product and sum rounded on its own (no fused multiply-add)
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

// Frequency Splitter (docs/analise/REA_rodada62.md). §-references below are to that document.

namespace lynx::fsplit
{
namespace
{
constexpr int slopes[8] { 6, 12, 18, 24, 36, 48, 72, 96 };
constexpr int maxBlock = 2048;

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::int32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

float f32 (double v) noexcept { return (float) v; }

// parameters 2/3 (§2.2): the first k with v below the float running sum (k + 1)·0.125, else 96 dB/oct
int slopeOf (float v) noexcept
{
    float edge = 0.0f;
    for (int k = 0; k < 8; ++k)
    {
        edge += 0.125f;
        if (v < edge)
            return slopes[k];
    }
    return 96;
}

float frequencyOf (float v) noexcept { return f32 (std::pow (2.0, (double) (v * 9.058894157f))) * 30.0f; }

// levels (§2.2): 0.5 = unity, 1 = +18 dB
float levelOf (float v) noexcept { return v < 0.5f ? v + v : (v - 0.5f) * 13.886564255f + 1.0f; }

// ------------------------------------------------------------------------------------------------ IIR crossover

// four float lanes: low-pass L, R, high-pass L, R (§2.3)
struct Section
{
    float b0[4] {}, b1[4] {}, b2[4] {}, a1[4] {}, a2[4] {}; // y = b0·x + b1·x1 + b2·x2 + a1·y1 + a2·y2
    float y1[4] {}, y2[4] {}, x1[4] {}, x2[4] {};

    void setHalf (int half, const float c[5]) noexcept
    {
        for (int lane = 2 * half; lane < 2 * half + 2; ++lane)
        {
            b0[lane] = c[0], b1[lane] = c[1], b2[lane] = c[2], a1[lane] = c[3], a2[lane] = c[4];
        }
    }
    void copyCoefficients (const Section& o) noexcept
    {
        std::memcpy (b0, o.b0, sizeof (b0));
        std::memcpy (b1, o.b1, sizeof (b1));
        std::memcpy (b2, o.b2, sizeof (b2));
        std::memcpy (a1, o.a1, sizeof (a1));
        std::memcpy (a2, o.a2, sizeof (a2));
    }
    void clear() noexcept
    {
        std::fill (y1, y1 + 4, 0.0f), std::fill (y2, y2 + 4, 0.0f), std::fill (x1, x1 + 4, 0.0f), std::fill (x2, x2 + 4, 0.0f);
    }
};

// the engine's biquad design (§2.3): 0 low-pass, 1 high-pass (RBJ forms with α = sin ω·0.5/Q), 9/10 first-order
// low/high-pass from tan(π f/rate); the recursive coefficients are stored negated
void design (int type, double f, double rate, double q, float c[5]) noexcept
{
    if (type == 9 || type == 10)
    {
        const auto t = f32 (std::tan ((f * 3.141592653589793) / rate));
        const auto d = 1.0 / (double) (t + 1.0f);
        const double n0 = type == 9 ? (double) t : 1.0, n1 = type == 9 ? (double) t : -1.0;
        c[0] = f32 (d * n0);
        c[1] = f32 (d * n1);
        c[2] = f32 (d * 0.0);
        c[3] = -f32 (d * (double) (t - 1.0f));
        c[4] = -f32 (d * 0.0);
        return;
    }
    const auto w = (f * 6.283185307179586) / rate;
    const auto s = std::sin (w), cs = std::cos (w);
    const auto alpha = (0.5 / q) * s;
    const auto d = 1.0 / (alpha + 1.0);
    const double b1 = type == 0 ? 1.0 - cs : -(cs + 1.0);
    const double b0 = type == 0 ? (1.0 - cs) * 0.5 : (cs + 1.0) * 0.5;
    c[0] = f32 (d * b0);
    c[1] = f32 (d * b1);
    c[2] = f32 (d * b0);
    c[3] = -f32 (d * (cs * -2.0));
    c[4] = -f32 (d * (1.0 - alpha));
}

struct Crossover
{
    std::array<Section, 8> sec {};
    int sections = 0;
    bool invertHigh = false, allPass = false;
    // 18 dB/oct (§2.4): a first-order all-pass (c1) and a second-order all-pass (k1, k2) on the halved input
    float c1 = 0.0f, k1 = 0.0f, k2 = 0.0f;
    float ap1[4] {};     // y L, y R, x L, x R
    float ap2[2][4] {};  // per channel y1, y2, x1, x2

    void clear() noexcept
    {
        for (auto& s : sec)
            s.clear();
        std::fill (ap1, ap1 + 4, 0.0f);
        std::memset (ap2, 0, sizeof (ap2));
    }

    void set (int slope, float f, double rate) noexcept
    {
        sections = 0;
        invertHigh = false;
        allPass = false;
        const auto fd = (double) f;
        float c[5];
        auto pair = [&] (int i, double q, int lp, int hp) {
            design (lp, fd, rate, q, c);
            sec[(std::size_t) i].setHalf (0, c);
            design (hp, fd, rate, q, c);
            sec[(std::size_t) i].setHalf (1, c);
        };
        auto copy = [&] (int to, int from) { sec[(std::size_t) to].copyCoefficients (sec[(std::size_t) from]); };
        switch (slope)
        {
            case 6: sections = 1; pair (0, 0.0, 9, 10); break;
            case 12: invertHigh = true; sections = 1; pair (0, 0.5, 0, 1); break;
            case 18:
            {
                allPass = true;
                const auto w = fd * 6.283185307179586;
                const auto s1 = std::sin (w / rate), cs1 = std::cos (w / rate);
                c1 = f32 (cs1 / (s1 + 1.0));
                const auto s2 = std::sin (w / rate), cs2 = std::cos (w / rate);
                const auto t = cs2 / (s2 + 1.0);
                const auto x = ((t + 1.0) / (1.0 - t)) * 0.5 * (1.0 - cs2);
                k1 = f32 ((1.0 - x) / (x + 1.0));
                k2 = f32 ((double) (k1 + 1.0f) * cs2);
                break;
            }
            case 24: sections = 2; pair (0, 0.7070000171661377, 0, 1); copy (1, 0); break;
            case 36:
                invertHigh = true;
                sections = 3;
                pair (0, 0.5411959886550903, 0, 1);
                pair (1, 1.3065630197525024, 0, 1);
                pair (2, 0.7070000171661377, 0, 1);
                break;
            case 48:
                sections = 4;
                pair (0, 0.5411959886550903, 0, 1);
                pair (1, 1.3065630197525024, 0, 1);
                copy (2, 0), copy (3, 1);
                break;
            case 72:
                sections = 6;
                pair (0, 0.517638087272644, 0, 1);
                pair (1, 0.7071067690849304, 0, 1);
                pair (2, 1.9318517446517944, 0, 1);
                copy (3, 0), copy (4, 1), copy (5, 2);
                break;
            default: // 96
                sections = 8;
                pair (0, 0.509795606136322, 0, 1);
                pair (1, 0.601344883441925, 0, 1);
                pair (2, 0.8999761939048767, 0, 1);
                pair (3, 2.562915325164795, 0, 1);
                copy (4, 0), copy (5, 1), copy (6, 2), copy (7, 3);
                break;
        }
    }

    // (low L, low R, high L, high R)
    void run (float l, float r, float out[4]) noexcept
    {
        if (allPass)
        {
            const float x[2] { l * 0.5f, r * 0.5f };
            float a[2], b[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto y = (ap1[ch] - x[ch]) * c1 + ap1[2 + ch];
                ap1[2 + ch] = x[ch];
                ap1[ch] = y;
                a[ch] = y;
                auto* s = ap2[ch];
                const auto z = ((s[0] - s[2]) * k2 + (x[ch] - s[1]) * k1) + s[3];
                s[1] = s[0], s[0] = z, s[3] = s[2], s[2] = x[ch];
                b[ch] = z;
            }
            out[0] = b[0] + a[0];
            out[1] = b[1] + a[1];
            out[2] = a[0] - b[0];
            out[3] = a[1] - b[1];
            return;
        }
        float v[4] { l, r, l, r };
        for (int i = 0; i < sections; ++i)
        {
            auto& s = sec[(std::size_t) i];
            for (int k = 0; k < 4; ++k)
            {
                const auto y = (((s.x1[k] * s.b1[k] + v[k] * s.b0[k]) + s.b2[k] * s.x2[k]) + s.y1[k] * s.a1[k]) + s.a2[k] * s.y2[k];
                s.x2[k] = s.x1[k], s.x1[k] = v[k], s.y2[k] = s.y1[k], s.y1[k] = y;
                v[k] = y;
            }
        }
        out[0] = v[0], out[1] = v[1];
        out[2] = invertHigh ? -v[2] : v[2];
        out[3] = invertHigh ? -v[3] : v[3];
    }
};

// ------------------------------------------------------------------------------------------------ FFT mode (§3)

float fBits (std::uint32_t b) noexcept
{
    float f;
    std::memcpy (&f, &b, 4);
    return f;
}

// the engine's log2 approximation: exponent plus a polynomial in the mantissa bits taken as an integer
float log2Approx (float x) noexcept
{
    std::uint32_t b;
    std::memcpy (&b, &x, 4);
    const auto m = (float) (int) (b & 0x7fffffu);
    const auto u = 1.0f - m * 1.1920928955e-07f;
    auto a = 0.1556500942f - m * 1.8554937142e-08f;
    a = a - (u * m) * 1.8554937142e-08f;
    a = a + (0.1063157022f - m * 1.2673819327e-08f);
    a = a * u;
    a = a + (0.7390000224f - m * 8.8095667650e-08f);
    return (1.0f - a) + (float) ((int) (b >> 23) - 127);
}

// 2^y by a cubic on the fraction (float), negative arguments as the reciprocal
float pow2Approx (float y) noexcept
{
    auto power = [] (float v) {
        const int i = (int) v;
        const auto f = v - (float) i;
        const auto p = ((f * 0.07906465977f + 0.2243280858f) * f + 0.6965985298f) * f + 0.9998621941f;
        return p * fBits ((std::uint32_t) (i + 127) << 23);
    };
    if (y > 0.0f)
        return power (y);
    if (y < 0.0f)
        return 1.0f / power (-y);
    return 1.0f;
}

int fftSize (int quality, float rate) noexcept
{
    const int base = 512 << std::clamp (quality, 0, 7);
    if (rate < 44100.0f)
        return base / 2;
    const int e = (int) (std::log ((double) rate * 2.2675736961451248e-05) * 1.4426950408889634 + 0.5);
    return std::min (base << std::clamp (e, 0, 7), 65536);
}

// magnitude of the low band (high = false: from f_c on it falls at `slope` dB/oct, −6 dB at f_c, complementary
// below) or of the high band (mirrored), on the grid k·rate/n (§3.2)
std::vector<float> magnitudes (int n, float rate, float fc, float slope, bool high)
{
    std::vector<float> t ((std::size_t) (n / 2 + 1));
    const auto step = f32 ((double) rate / (double) n);
    const auto inv = 1.0f / fc;
    for (int k = 0; k <= n / 2; ++k)
    {
        const auto f = (float) k * step;
        if (f == 0.0f)
        {
            t[(std::size_t) k] = high ? 0.0f : 1.0f;
            continue;
        }
        auto lg = log2Approx (inv * f);
        if (high)
            lg = -lg;
        auto v = slope * lg;
        float amp;
        if (lg >= 0.0f)
        {
            v = -6.0f - v;
            amp = v >= -120.0f ? pow2Approx (v * 0.1666666716f) : 0.0f;
        }
        else
        {
            v = v - 6.0f;
            amp = v >= -120.0f ? 1.0f - pow2Approx (v * 0.1666666716f) : 1.0f;
        }
        t[(std::size_t) k] = amp;
    }
    return t;
}
} // namespace

// -------------------------------------------------------------------------------------------------------- state

std::optional<State> parseState (const std::vector<std::uint8_t>& d)
{
    // record 0xD5 (§1): u32 version (< 5), 17 × i32 (the wrapper's controls), i32 UI value, i32 (unused), i32 editor
    // bits, i32 mute bits, options byte, version-dependent UI fields
    if (d.size() < 0x48)
        return std::nullopt;
    const auto version = i32At (d, 0);
    if (version < 0 || version >= 5)
        return std::nullopt;
    State s;
    for (int i = 0; i < 17; ++i)
        s.words[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    s.muteBits = d.size() >= 0x58 ? i32At (d, 0x54) & 0x3f : 0;

    // the wrapper's ProcessParam (§2.1): (raw − min)/(max − min) clamped, frequencies sorted, slopes /7, quality /5,
    // mode and band count as they are; the mute bits become parameters 14…19, solos stay off
    auto unit = [] (double v) { return (float) std::clamp (v, 0.0, 1.0); };
    const auto& w = s.words;
    const auto lo = std::min (w[0], w[1]), hi = std::max (w[0], w[1]);
    auto& p = s.params;
    p[0] = unit (lo * (1.0 / 65536.0));
    p[1] = unit (hi * (1.0 / 65536.0));
    p[2] = (float) (w[2] / 7.0);
    p[3] = (float) (w[3] / 7.0);
    p[4] = (float) w[4];
    p[5] = (float) w[5];
    p[6] = (float) (w[6] / 5.0);
    p[7] = unit (w[7] * (1.0 / 65536.0));
    for (int i = 0; i < 6; ++i)
        p[(std::size_t) (8 + i)] = unit ((w[(std::size_t) (8 + i)] + 1800) * (1.0 / 3600.0));
    for (int i = 0; i < 6; ++i)
        p[(std::size_t) (14 + i)] = (s.muteBits >> i) & 1 ? 1.0f : 0.0f;
    return s;
}

bool busUsed (const State& s, int bus) noexcept
{
    if (bus == 0)
        return true;
    for (const auto d : s.destinations())
        if (d == bus)
            return true;
    return false;
}

void toParams (const State& s, float* params, int bus) noexcept
{
    for (int i = 0; i < engineParams; ++i)
        params[i] = s.params[(std::size_t) i];
    const auto dest = s.destinations();
    for (int b = 0; b < 3; ++b)
        params[outputLow + b] = bus > 0 && dest[(std::size_t) b] == bus ? 1.0f : 0.0f;
}

const EffectInfo& info()
{
    // defaults: FL's "Default" preset (3 bands at 200 Hz / 4 kHz, 24 dB/oct, unity levels)
    static const EffectInfo i { "Frequency Splitter", {
        { "Low/mid freq.", 19800.0f / 65536.0f }, { "Mid/high freq.", 48065.0f / 65536.0f }, { "Low/mid slope", 3.0f / 7.0f },
        { "Mid/high slope", 3.0f / 7.0f }, { "FFT mode", 0.0f }, { "3 bands", 1.0f }, { "FFT quality", 0.4f },
        { "FFT fade", 45820.0f / 65536.0f }, { "Low send", 0.5f }, { "Mid send", 0.5f }, { "High send", 0.5f },
        { "Low level", 0.5f }, { "Mid level", 0.5f }, { "High level", 0.5f }, { "Low send mute", 0.0f },
        { "Mid send mute", 0.0f }, { "High send mute", 0.0f }, { "Low mute", 0.0f }, { "Mid mute", 0.0f },
        { "High mute", 0.0f }, { "Low send solo", 0.0f }, { "Mid send solo", 0.0f }, { "High send solo", 0.0f },
        { "Low solo", 0.0f }, { "Mid solo", 0.0f }, { "High solo", 0.0f }, { "Out: low", 0.0f }, { "Out: mid", 0.0f },
        { "Out: high", 0.0f } } };
    return i;
}

std::string paramText (int param, float v)
{
    char text[64];
    if (param == 0 || param == 1)
        std::snprintf (text, sizeof (text), "%.1f Hz", (double) frequencyOf (v));
    else if (param == 2 || param == 3)
        std::snprintf (text, sizeof (text), "%d dB/oct", slopeOf (v));
    else if (param == 4)
        return (int) (v * 1.998f) == 0 ? "Normal" : "FFT";
    else if (param == 5)
        return v < 0.5f ? "2" : "3";
    else if (param == 6)
    {
        static const char* names[6] { "Lowest", "Low", "Medium", "High", "Higher", "Highest" };
        const int q = std::clamp ((int) (v * 5.994f), 0, 5);
        std::snprintf (text, sizeof (text), "%s (%d samples)", names[q], fftSize (q, 44100.0f));
    }
    else if (param == 7)
        std::snprintf (text, sizeof (text), "%.1f ms", (double) (f32 (std::pow (10.0f, v + v)) * 2.0f));
    else if (param >= 8 && param < 14)
    {
        const auto g = levelOf (v);
        if (g == 0.0f)
            return "-inf dB";
        std::snprintf (text, sizeof (text), "%.1f dB", 20.0 * std::log10 ((double) g));
    }
    else
        return v >= 0.5f ? "On" : "Off";
    return text;
}

// ------------------------------------------------------------------------------------------------------- engine

struct Engine::Impl
{
    float rate = 44100.0f;
    std::array<float, engineParams> p {};
    // derived values (the engine's fields)
    int mode = -1, quality = 0;
    bool twoBands = true, dirty = true;
    std::array<int, 2> slope { 6, 6 };
    std::array<float, 2> freq {};
    float fadeTime = 0.0f, smooth = 1.0f;
    std::array<bool, 12> flag {};  // send mutes, sum mutes, send solos, sum solos
    std::array<float, 6> factor { 1, 1, 1, 1, 1, 1 }, target {}, gain {}; // sends low/mid/high, then sums
    std::array<Crossover, 3> xo {};

    // FFT mode
    int n = 0, active = 0, histPos = 0, fadePos = 4096, fadeLength = 4096, delayPos = 0;
    std::vector<float> hist[2], delay[2];
    std::array<peqfl::FftConvolver, 4> conv {}; // [set·2 + 0] low, [set·2 + 1] high

    void factors (int group) noexcept
    {
        // group 0: sends (mutes 0…2, solos 6…8), 1: sums (mutes 3…5, solos 9…11)
        const int m = 3 * group, s = 6 + 3 * group;
        const bool solo = flag[(std::size_t) s] || flag[(std::size_t) s + 1] || flag[(std::size_t) s + 2];
        for (int i = 0; i < 3; ++i)
            factor[(std::size_t) (m + i)] = solo ? (flag[(std::size_t) (s + i)] ? 1.0f : 0.0f) : (flag[(std::size_t) (m + i)] ? 0.0f : 1.0f);
    }
    void retarget (int i) noexcept { target[(std::size_t) i] = levelOf (p[(std::size_t) (8 + i)]) * factor[(std::size_t) i]; }

    void set (int index, float v) noexcept
    {
        if (index < 0 || index >= engineParams)
            return;
        p[(std::size_t) index] = v;
        if (index >= 14)
        {
            const int f = index - 14; // 0…2 send mutes, 3…5 sum mutes, 6…8 send solos, 9…11 sum solos
            flag[(std::size_t) f] = v >= 0.5f;
            const int group = (f / 3) % 2;
            factors (group);
            if (f < 6)
                retarget (f); // a mute updates its own band only
            else
                for (int i = 0; i < 3; ++i)
                    retarget (3 * group + i);
            return;
        }
        if (index >= 8)
        {
            retarget (index - 8);
            return;
        }
        switch (index)
        {
            case 0:
            case 1:
            {
                const auto f = frequencyOf (v);
                if (f != freq[(std::size_t) index])
                    freq[(std::size_t) index] = f, dirty = true;
                break;
            }
            case 2:
            case 3:
            {
                const auto s = slopeOf (v);
                if (s != slope[(std::size_t) index - 2])
                    slope[(std::size_t) index - 2] = s, dirty = true;
                break;
            }
            case 4:
                if (mode != (int) (v * 1.998f))
                    mode = (int) (v * 1.998f), dirty = true;
                break;
            case 5:
                if (twoBands != (v < 0.5f))
                    twoBands = v < 0.5f, dirty = true;
                break;
            case 6:
                if (quality != (int) (v * 5.994f))
                    quality = (int) (v * 5.994f), dirty = true;
                break;
            case 7: fadeTime = f32 (std::pow (10.0f, v + v)) * 0.002f; break;
            default: break;
        }
    }

    void designIir() noexcept
    {
        if (twoBands)
            xo[0].set (slope[0], freq[0], rate);
        else
        {
            xo[0].set (slope[0], freq[0], rate);
            xo[1].set (slope[1], freq[1], rate);
            xo[2].set (slope[1], freq[1], rate);
        }
    }

    // (§3.3) a new response goes to the idle convolver set, primed with the last n input samples
    void loadFft()
    {
        const int size = fftSize (quality, rate);
        if (size != n)
        {
            n = size;
            histPos = 0;
            active = 0;
            for (auto& h : hist)
                h.assign ((std::size_t) n, 0.0f);
            for (auto& c : conv)
            {
                c.init (n);
                c.setKernel (std::vector<float> ((std::size_t) (n / 2 + 1), 0.0f));
            }
        }
        auto& low = conv[(std::size_t) (2 * active)];
        auto& high = conv[(std::size_t) (2 * active + 1)];
        low.setKernel (magnitudes (n, rate, freq[0], (float) slope[0], false));
        if (! twoBands)
            high.setKernel (magnitudes (n, rate, freq[1], (float) slope[1], true));
        dirty = false;
        fadePos = 0;
        fadeLength = (int) (fadeTime * rate);
        low.clear();
        high.clear();
        for (int k = 0, i = histPos; k < n; ++k)
        {
            float l = hist[0][(std::size_t) i], r = hist[1][(std::size_t) i];
            low.step (l, r);
            l = hist[0][(std::size_t) i], r = hist[1][(std::size_t) i];
            high.step (l, r);
            if (++i == n)
                i = 0;
        }
        active = active == 0 ? 1 : 0;
    }

    // one convolver set (§3.4): low, mid, high per channel from the input and the input delayed by 3n/4
    void split (int set, float l, float r, float dl, float dr, float v[6]) noexcept
    {
        float ll = l, lr = r;
        conv[(std::size_t) (2 * set)].step (ll, lr);
        v[0] = ll, v[1] = lr;
        if (! twoBands)
        {
            float hl = l, hr = r;
            conv[(std::size_t) (2 * set + 1)].step (hl, hr);
            v[4] = hl, v[5] = hr;
            v[2] = (dl - ll) - hl;
            v[3] = (dr - lr) - hr;
        }
        else
        {
            v[4] = dl - ll;
            v[5] = dr - lr;
            v[2] = v[3] = 0.0f;
        }
    }

    void smoothGains() noexcept
    {
        for (int i = 0; i < 6; ++i)
            gain[(std::size_t) i] = (target[(std::size_t) i] - gain[(std::size_t) i]) * smooth + gain[(std::size_t) i];
    }

    void output (const float b[6], float* const* out, int i) noexcept
    {
        const auto& g = gain;
        for (int k = 0; k < 3; ++k)
        {
            if (out[2 * k] != nullptr)
                out[2 * k][i] = b[2 * k] * g[(std::size_t) k];
            if (out[2 * k + 1] != nullptr)
                out[2 * k + 1][i] = b[2 * k + 1] * g[(std::size_t) k];
        }
        if (out[6] != nullptr)
            out[6][i] = (b[0] * g[3] + b[2] * g[4]) + b[4] * g[5];
        if (out[7] != nullptr)
            out[7][i] = (b[1] * g[3] + b[3] * g[4]) + b[5] * g[5];
    }
};

Engine::Engine() : impl (std::make_unique<Impl>())
{
    for (auto& d : impl->delay)
        d.assign (65536, 0.0f);
    // the engine's defaults, set in its constructor in parameter order (§2.5)
    for (int i = 0; i < engineParams; ++i)
        impl->set (i, i == 0 ? 0.05f : i == 1 ? 0.25f : i == 8 ? 1.0f : 0.0f);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate)
{
    auto& m = *impl;
    m.rate = (float) sampleRate;
    m.smooth = f32 (1.0 - std::pow (0.09999999999999998, 200.0 / (double) m.rate));
    m.dirty = true;
    m.n = 0;
}

void Engine::reset (bool settled) noexcept
{
    auto& m = *impl;
    for (auto& x : m.xo)
        x.clear();
    for (auto& d : m.delay)
        std::fill (d.begin(), d.end(), 0.0f);
    m.delayPos = 0;
    m.n = 0;
    m.active = 0;
    m.dirty = true;
    m.gain = settled ? m.target : std::array<float, 6> {};
    m.fadePos = m.fadeLength = 4096;
    if (settled)
    {
        if (m.mode == 1)
        {
            m.loadFft();
            m.fadePos = m.fadeLength;
        }
        else
        {
            m.designIir();
            m.dirty = false;
        }
    }
}

void Engine::setParameter (int index, float value) noexcept { impl->set (index, value); }
float Engine::parameter (int index) const noexcept { return index >= 0 && index < engineParams ? impl->p[(std::size_t) index] : 0.0f; }

int Engine::latencySamples() const noexcept
{
    const auto& m = *impl;
    return m.mode == 1 ? 3 * fftSize (m.quality, m.rate) / 4 : 0;
}

void Engine::process (const float* inL, const float* inR, float* const* out, int n) noexcept
{
    const ScopedFlushDenormals ftz; // the engine runs with flush-to-zero and denormals-are-zero (§2.6)
    auto& m = *impl;
    if (m.mode == 0 && m.dirty)
    {
        m.designIir();
        m.dirty = false;
    }
    float b[6];
    if (m.mode == 1)
    {
        for (int i = 0; i < n; ++i)
        {
            m.smoothGains();
            if (m.dirty && m.fadePos >= m.fadeLength)
                m.loadFft();
            const auto l = inL[i], r = inR[i];
            m.hist[0][(std::size_t) m.histPos] = l;
            m.hist[1][(std::size_t) m.histPos] = r;
            if (++m.histPos == m.n)
                m.histPos = 0;
            m.delay[0][(std::size_t) m.delayPos] = l;
            m.delay[1][(std::size_t) m.delayPos] = r;
            const auto back = (std::size_t) ((m.delayPos - 3 * m.n / 4) & 0xffff);
            const auto dl = m.delay[0][back], dr = m.delay[1][back];
            m.delayPos = (m.delayPos + 1) & 0xffff;
            if (m.fadePos < m.fadeLength)
            {
                float a[6], c[6];
                m.split (0, l, r, dl, dr, a);
                m.split (1, l, r, dl, dr, c);
                const auto f = (float) m.fadePos / (float) m.fadeLength;
                const auto w = m.active == 0 ? 1.0f - f : f;
                const auto w1 = 1.0f - w;
                for (int k = 0; k < 6; ++k)
                    b[k] = c[k] * w1 + a[k] * w;
            }
            else
                m.split (m.active == 0 ? 1 : 0, l, r, dl, dr, b);
            m.output (b, out, i);
            if (m.fadePos < m.fadeLength)
                ++m.fadePos;
        }
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        m.smoothGains();
        float x0[4];
        m.xo[0].run (inL[i], inR[i], x0);
        if (m.twoBands)
        {
            b[0] = x0[0], b[1] = x0[1], b[2] = b[3] = 0.0f, b[4] = x0[2], b[5] = x0[3];
        }
        else
        {
            float x1[4], x2[4];
            m.xo[1].run (x0[2], x0[3], x1);
            m.xo[2].run (x0[0], x0[1], x2);
            b[0] = x2[0] + x2[2], b[1] = x2[1] + x2[3];
            b[2] = x1[0], b[3] = x1[1], b[4] = x1[2], b[5] = x1[3];
        }
        m.output (b, out, i);
    }
}

// --------------------------------------------------------------------------------------------------- the effect

namespace
{
class SplitterEffect final : public Effect
{
public:
    // `initial`: the slot's parameters, so the latency (linear-phase mode) is known before the first block — a
    // latency that appears only after the first block makes the host re-plan its delay compensation (REA round 87)
    explicit SplitterEffect (const EffectParams* initial = nullptr)
    {
        for (auto& b : bands)
            b.assign ((std::size_t) maxBlock, 0.0f);
        if (initial != nullptr)
            expected = *initial, hasExpected = true;
    }

    void prepare (double sr) noexcept override
    {
        rate = sr;
        engine.prepare (sr);
        engine.reset (true);
    }
    void reset() noexcept override { engine.reset (true); }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        if (! applied || std::memcmp (last.data(), params.data(), sizeof (float) * engineParams) != 0)
        {
            const bool first = ! applied;
            for (int i = 0; i < engineParams; ++i)
                if (first || params[(std::size_t) i] != last[(std::size_t) i])
                    engine.setParameter (i, params[(std::size_t) i]);
            std::memcpy (last.data(), params.data(), sizeof (float) * engineParams);
            applied = true;
            if (first)
                engine.reset (true);
        }
        const bool sel[3] { params[outputLow] >= 0.5f, params[outputMid] >= 0.5f, params[outputHigh] >= 0.5f };
        const bool sends = sel[0] || sel[1] || sel[2];
        for (int done = 0; done < n;)
        {
            const int len = std::min (maxBlock, n - done);
            float* out[8] {};
            if (sends)
            {
                for (int k = 0; k < 3; ++k)
                    if (sel[k])
                        out[2 * k] = bands[(std::size_t) (2 * k)].data(), out[2 * k + 1] = bands[(std::size_t) (2 * k + 1)].data();
            }
            else
                out[6] = bands[6].data(), out[7] = bands[7].data();
            engine.process (l + done, r + done, out, len);
            if (sends)
            {
                // FL adds every band sent to the destination into its buffer, low, mid, high in that order
                std::fill (l + done, l + done + len, 0.0f);
                std::fill (r + done, r + done + len, 0.0f);
                for (int k = 0; k < 3; ++k)
                    if (sel[k])
                        for (int i = 0; i < len; ++i)
                        {
                            l[done + i] += bands[(std::size_t) (2 * k)][(std::size_t) i];
                            r[done + i] += bands[(std::size_t) (2 * k + 1)][(std::size_t) i];
                        }
            }
            else
            {
                std::copy (bands[6].begin(), bands[6].begin() + len, l + done);
                std::copy (bands[7].begin(), bands[7].begin() + len, r + done);
            }
            done += len;
        }
    }

    int latencySamples() const noexcept override
    {
        if (! applied && hasExpected)
            return (int) (expected[4] * 1.998f) == 1 ? 3 * fftSize ((int) (expected[6] * 5.994f), (float) rate) / 4 : 0;
        return engine.latencySamples();
    }

private:
    double rate = 44100.0;
    EffectParams expected {};
    bool hasExpected = false;
    Engine engine;
    std::array<float, engineParams> last {};
    bool applied = false;
    std::array<std::vector<float>, 8> bands;
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<SplitterEffect>(); }
std::unique_ptr<Effect> make (const EffectParams& params) { return std::make_unique<SplitterEffect> (&params); }
} // namespace lynx::fsplit
