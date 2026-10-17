#include "core/DynamicsSuite.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LrCrossover.h"
#include "core/MixSuite.h"

// REA_rodada108. Published material the designs rest on:
//  - feed-forward compressor with a log-domain level detector (branching attack / release), soft-knee gain computer:
//    D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
//    JAES 60(6), 2012 (§2.2 gain computer, §3 detectors, §4 auto make-up gain);
//  - look-ahead by delaying the audio against the side chain: U. Zölzer (ed.), "DAFX", 2nd ed., ch. 4.2;
//  - upward compression / expansion as mirrored gain curves of the same computer: ibid. ch. 4.2.2;
//  - Linkwitz-Riley crossovers (two cascaded Butterworth sections per side): S. Linkwitz, "Active Crossover
//    Networks for Non-coincident Drivers", JAES 24(1), 1976;
//  - waveshaping families (smooth odd polynomial, asymmetric clipping, even-order rectifier terms) and their
//    harmonic signatures: DAFX ch. 4.3, J. Pakarinen / D. T. Yeh, CMJ 33(2), 2009.
// Every constant that shapes a "style" was fitted by us to the black-box measurements (MEASURE_fabfilter.md); the
// fitted values and their residuals are in REA_rodada108.md.

namespace lynx::dyn
{
namespace
{
constexpr double pi = 3.141592653589793;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-6)); }
inline double logMap (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, (double) std::clamp (v, 0.0f, 1.0f)); }
inline float logValue (double x, double lo, double hi) noexcept { return (float) std::clamp (std::log (x / lo) / std::log (hi / lo), 0.0, 1.0); }
inline double lin (float v, double lo, double hi) noexcept { return lo + (hi - lo) * (double) std::clamp (v, 0.0f, 1.0f); }
inline bool on (float v) noexcept { return v >= 0.5f; }
inline double coef (double ms, double sr) noexcept { return ms <= 0.0 ? 0.0 : std::exp (-1.0 / (ms * 0.001 * sr)); }
inline int pick (float v, int n) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (n - 1)), 0, n - 1); }

// the shared ratio law: 1 .. 100 : 1, 0.5 ≈ 2.7 : 1, 0.6 ≈ 4.5 : 1, 0.75 ≈ 11 : 1
double ratioLaw (float v) noexcept { return std::exp (std::log (100.0) * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 2.2)); }
float ratioLawValue (double r) noexcept { return (float) std::clamp (std::pow (std::log (std::max (1.0, r)) / std::log (100.0), 1.0 / 2.2), 0.0, 1.0); }

// Giannoulis et al. eq. (4): reduction (dB, ≥ 0) for an overshoot `o` (dB above the threshold), slope s = 1 − 1/R
double kneeCurve (double o, double s, double knee) noexcept
{
    if (2.0 * o <= -knee)
        return 0.0;
    if (knee > 0.0 && 2.0 * std::abs (o) < knee)
        return s * (o + knee * 0.5) * (o + knee * 0.5) / (2.0 * knee);
    return s * o;
}

float blockPeak (const float* l, const float* r, int n) noexcept
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i)
        p = std::max (p, std::max (std::abs (l[i]), std::abs (r[i])));
    return std::min (p, 1.0f);
}

// Stereo delay line, fixed capacity (no allocation after construction).
struct StereoDelay
{
    static constexpr int capacity = 8192; // 20 ms look-ahead at 192 kHz + the oversampler, with room
    std::array<float, capacity> l {}, r {};
    int pos = 0, len = 0;
    void setLength (int n) noexcept
    {
        n = std::clamp (n, 0, capacity - 1);
        if (n != len)
        {
            len = n;
            reset();
        }
    }
    void reset() noexcept { l.fill (0.0f); r.fill (0.0f); pos = 0; }
    void process (float& a, float& b) noexcept
    {
        if (len == 0)
            return;
        l[(std::size_t) pos] = a;
        r[(std::size_t) pos] = b;
        auto rd = pos - len;
        if (rd < 0)
            rd += capacity;
        a = l[(std::size_t) rd];
        b = r[(std::size_t) rd];
        if (++pos == capacity)
            pos = 0;
    }
};

// mono float delay (the side chain's gain against the look-ahead)
struct MonoDelay
{
    static constexpr int capacity = 8192;
    std::array<float, capacity> d {};
    int pos = 0, len = 0;
    void setLength (int n) noexcept { len = std::clamp (n, 0, capacity - 1); }
    void reset() noexcept { d.fill (0.0f); pos = 0; }
    float process (float x) noexcept
    {
        if (len == 0)
            return x;
        d[(std::size_t) pos] = x;
        auto rd = pos - len;
        if (rd < 0)
            rd += capacity;
        const auto y = d[(std::size_t) rd];
        if (++pos == capacity)
            pos = 0;
        return y;
    }
};

// first-order DC blocker (stereo)
struct DcBlock
{
    double a = 0.9995, x1[2] {}, y1[2] {};
    void set (double sr) noexcept { a = std::exp (-2.0 * pi * 5.0 / sr); }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0; }
    float process (float x, int c) noexcept
    {
        const auto y = (double) x - x1[c] + a * y1[c];
        x1[c] = x;
        y1[c] = y;
        return (float) y;
    }
};

struct LiveMeters
{
    std::array<std::atomic<float>, 8> v {};
    float read (int k) const noexcept { return k >= 0 && k < 8 ? v[(std::size_t) k].load (std::memory_order_relaxed) : 0.0f; }
    void set (int k, float x) noexcept { v[(std::size_t) k].store (x, std::memory_order_relaxed); }
};

std::string fmt (const char* f, double v)
{
    char t[48];
    std::snprintf (t, sizeof (t), f, v);
    return t;
}

std::string hzText (double f)
{
    return f >= 1000.0 ? fmt ("%.2f kHz", f / 1000.0) : fmt ("%.0f Hz", f);
}

std::string msText (double ms)
{
    if (ms >= 1000.0)
        return fmt ("%.2f s", ms / 1000.0);
    if (ms >= 100.0)
        return fmt ("%.0f ms", ms);
    if (ms >= 10.0)
        return fmt ("%.1f ms", ms);
    return fmt ("%.2f ms", ms);
}

std::string ratioText (double r) { return r >= 99.5 ? "100 : 1" : fmt (r < 10.0 ? "%.2f : 1" : "%.1f : 1", r); }
} // namespace

// ================================================================== Crest Compressor

namespace crest
{
Style styleOf (float v) noexcept { return (Style) pick (v, styles); }
float styleValue (Style s) noexcept { return (float) (int) s / (float) (styles - 1); }
double thresholdDb (float v) noexcept { return lin (v, -60.0, 0.0); }
double ratioOf (float v) noexcept { return ratioLaw (v); }
float ratioValue (double r) noexcept { return ratioLawValue (r); }
double kneeDb (float v) noexcept { return lin (v, 0.0, 72.0); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 60.0); }
double attackMs (float v) noexcept { return logMap (v, 0.005, 250.0); }
float attackValue (double ms) noexcept { return logValue (std::clamp (ms, 0.005, 250.0), 0.005, 250.0); }
double releaseMs (float v) noexcept { return logMap (v, 10.0, 2500.0); }
float releaseValue (double ms) noexcept { return logValue (std::clamp (ms, 10.0, 2500.0), 10.0, 2500.0); }
double holdMs (float v) noexcept { return 500.0 * (double) v * (double) v; }
float holdValue (double ms) noexcept { return (float) std::sqrt (std::clamp (ms / 500.0, 0.0, 1.0)); }
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 20.0); }
double maxLookaheadMs (float v) noexcept
{
    static constexpr double ms[] { 0.0, 1.0, 2.0, 5.0, 10.0, 20.0 };
    return ms[pick (v, 6)];
}
Character characterOf (float v) noexcept { return (Character) pick (v, 4); }
double driveDb (float v) noexcept { return lin (v, -24.0, 24.0); }
double scLevelDb (float v) noexcept { return lin (v, -36.0, 36.0); }
int oversamplingOf (float v) noexcept { return 1 << pick (v, 4); }
double trimDb (float v) noexcept { return lin (v, -36.0, 36.0); }

namespace
{
// The style families (fitted to MEASURE_fabfilter §2, see REA_rodada108 §3): time-constant scales of the level
// detector (τ = scale × knob), the knee every curve gets on top of the knob, and the curve family.
enum class Curve { downward, upward, twoWay, vocal, variMu };
struct StyleDef
{
    double attackScale;  // detector attack τ = scale × knob
    double releaseScale; // detector release τ = scale × 200 ms × (knob / 200 ms)^releaseExp
    double releaseExp;   // how much the release follows its knob (1 = proportional, 0 = fixed)
    double kneeAdd, kneeScale; // knee = add + scale × knob
    Curve curve;
    double windowMs;     // > 0: the level is the peak over this window (a ripple-free detector for fast releases)
};
constexpr StyleDef styleDefs[styles] {
    { 0.265, 1.27, 0.77, 6.0, 0.50, Curve::downward, 0.0 }, // clean
    { 0.56, 0.94, 0.15, 6.0, 1.00, Curve::downward, 0.0 },  // allround
    { 0.31, 1.10, 0.65, 4.0, 0.80, Curve::downward, 0.0 },  // punch
    { 0.31, 2.50, 1.00, 0.0, 1.00, Curve::upward, 0.0 },    // upward
    { 0.235, 0.01, 0.00, 0.0, 1.00, Curve::twoWay, 1.3 },   // two-way (recovers within about a millisecond)
    { 0.335, 0.65, 0.98, 0.0, 1.00, Curve::variMu, 0.0 },   // vari-mu
    { 0.30, 1.57, 0.53, 4.0, 0.17, Curve::downward, 0.0 },  // classic
    { 0.32, 1.52, 0.88, 4.0, 0.65, Curve::downward, 0.0 },  // opto
    { 1.08, 1.70, 0.04, 0.0, 1.00, Curve::vocal, 0.0 },     // vocal
    { 0.24, 3.57, 0.19, 0.0, 1.00, Curve::downward, 0.0 },  // bus
    { 0.08, 2.36, 1.00, 0.0, 1.00, Curve::downward, 0.0 },  // pumping
};

double releaseTau (const StyleDef& d, double knobMs) noexcept
{
    return d.releaseScale * 200.0 * std::pow (knobMs / 200.0, d.releaseExp);
}

// vari-mu: the share of the full ratio reached at an overshoot o (dB), by slope s = 1 − 1/R — measured (§2.1 data,
// slopes 1/3 … 0.99, overshoots 0 … 18 dB) and divided by our detector's own shortfall (0.96)
double variMuShare (double s, double o) noexcept
{
    static constexpr double slopes[6] { 0.3333, 0.5, 0.75, 0.875, 0.95, 0.99 };
    static constexpr double table[6][10] {
        { 0, 0.071, 0.118, 0.189, 0.28, 0.383, 0.491, 0.599, 0.701, 0.797 },
        { 0, 0.102, 0.2, 0.311, 0.422, 0.524, 0.615, 0.695, 0.764, 0.824 },
        { 0, 0.263, 0.442, 0.561, 0.645, 0.708, 0.758, 0.797, 0.829, 0.856 },
        { 0, 0.478, 0.637, 0.723, 0.776, 0.813, 0.841, 0.863, 0.881, 0.896 },
        { 0, 0.545, 0.677, 0.742, 0.783, 0.812, 0.833, 0.85, 0.863, 0.875 },
        { 0, 0.665, 0.767, 0.811, 0.839, 0.857, 0.871, 0.882, 0.892, 0.9 } };
    if (o <= 0.0)
        return 0.0;
    const auto row = [&] (int k) {
        if (o >= 18.0)
            return 1.0 - (1.0 - table[k][9]) * 18.0 / o;
        const auto x = o / 2.0;
        const int i = std::min (8, (int) x);
        return table[k][i] + (table[k][i + 1] - table[k][i]) * (x - i);
    };
    double f;
    if (s <= slopes[0])
        f = row (0) * s / slopes[0];
    else if (s >= slopes[5])
        f = row (5);
    else
    {
        int k = 0;
        while (s > slopes[k + 1])
            ++k;
        const auto t = (s - slopes[k]) / (slopes[k + 1] - slopes[k]);
        f = row (k) + (row (k + 1) - row (k)) * t;
    }
    return std::min (1.0, f / 0.96);
}

// target gain change (dB; negative = reduction) for a detector level `x` (dB)
double curveGain (Style st, double x, double thr, double r, double knee, double range) noexcept
{
    const auto& d = styleDefs[(int) st];
    const auto s = 1.0 - 1.0 / r;
    switch (d.curve)
    {
        case Curve::upward:
        {
            // upward: quiet parts are lifted toward a point 10.5 dB under the threshold; above it the gain holds
            const auto g = s * (thr - 10.5 - std::min (x, thr));
            return std::clamp (g, -range, range);
        }
        case Curve::twoWay:
        {
            // two-way: a fixed offset, upward compression below (threshold − 6 dB), downward above (threshold + 4 dB)
            const auto g = -5.75 * s + 0.473 * s * std::max (0.0, thr - 6.0 - x) - 0.808 * kneeCurve (x - thr - 4.0, s, knee);
            return std::clamp (g, -range, range);
        }
        case Curve::vocal:
        {
            // a long smooth curve that already reduces far below the threshold (softplus in dB), steeper on top
            const auto k = 8.0 + knee * 0.5;
            const auto sv = std::min (1.0, 1.1667 * s);
            const auto z = (x - (thr - 2.1)) / k;
            const auto soft = z > 30.0 ? z : std::log1p (std::exp (z));
            return -std::min (range, sv * k * soft);
        }
        case Curve::variMu:
        {
            // the ratio grows with the reduction (variable-mu tube behaviour), as measured
            const auto o = std::max (0.0, x - thr);
            const auto soft = knee > 0.0 ? kneeCurve (x - thr, 1.0, knee) : o;
            return -std::min (range, s * soft * variMuShare (s, o + knee * 0.25));
        }
        case Curve::downward:
        default:
            return -std::min (range, kneeCurve (x - thr, s, d.kneeAdd + d.kneeScale * knee));
    }
}

void scEqParams (const EffectParams& p, EffectParams& out) noexcept
{
    out.fill (0.0f);
    for (int b = 0; b < scBands; ++b)
        for (int f = 0; f < 5; ++f)
            out[(std::size_t) facet::param (b, f)] = p[(std::size_t) scParam (b, f)];
    out[(std::size_t) facet::outputParam] = 0.5f;
    out[(std::size_t) facet::scaleParam] = 0.5f;
}
} // namespace

double staticGainDb (const EffectParams& p, double inputDb)
{
    return curveGain (styleOf (p[style]), inputDb, thresholdDb (p[threshold]), ratioOf (p[ratio]), kneeDb (p[knee]),
                      std::max (0.01, rangeDb (p[range])));
}

float characterCurve (Character c, float x) noexcept
{
    switch (c)
    {
        case Character::warm:
        {
            // clean up to about +8 dBFS, then an asymmetric soft limit (the positive side gives later): even + odd
            constexpr float tp = 2.5f, tn = 2.0f, sp = 3.0f, sn = 2.5f;
            if (x > tp)
                return tp + sp * std::tanh ((x - tp) / sp);
            if (x < -tn)
                return -tn - sn * std::tanh ((-x - tn) / sn);
            return x;
        }
        case Character::crisp:
        {
            // a slightly expanding cubic (odd harmonics growing with the square of the level), then an asymmetric
            // limit far above full scale
            const auto u = x + 0.0083f * x * x * x;
            return u >= 0.0f ? 16.0f * std::tanh (u / 16.0f) : 12.0f * std::tanh (u / 12.0f);
        }
        case Character::bright:
        {
            // an even, rectifier-like term (second harmonic from very low levels) and a soft limit near +12 dBFS
            constexpr float s = 0.05f;
            const auto even = 0.007f * s * (std::sqrt (1.0f + (x / s) * (x / s)) - 1.0f);
            return 4.0f * std::tanh ((x + even) / 4.0f);
        }
        case Character::off:
        default: return x;
    }
}

namespace
{
class Compressor final : public Effect
{
public:
    explicit Compressor (const EffectParams* initial)
    {
        if (initial != nullptr)
            configure (*initial);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        dc.set (sr);
        latencyMs = -1.0; // re-apply the delays
        configure (lastParams);
        reset();
    }

    void reset() noexcept override
    {
        audioDelay.reset();
        dryDelay.reset();
        for (auto& g : gainDelay)
            g.reset();
        for (auto& o : os)
            o.reset();
        for (auto& b : scEq)
            for (auto& s : b)
                s.reset();
        env = { -120.0, -120.0 };
        holdLeft = { 0, 0 };
        sustain = { 0.0, 0.0 };
        dc.reset();
        prevGain = { 1.0f, 1.0f };
        for (auto& pk : peaks)
            pk.fill (0.0f);
        peakPos = { 0, 0 };
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        lastParams = p;
        configure (p);
        for (int off = 0; off < n; off += maxBlock)
            chunk (l + off, r + off, std::min (maxBlock, n - off), p, off);
        scL = scR = nullptr;
    }

private:
    static constexpr int maxBlock = 1024;

    // latency = the chosen maximum look-ahead (+ the oversampler when on), as the reference does (§2: 960 samples
    // for "maximum 20 ms" whatever the look-ahead knob says)
    void configure (const EffectParams& p) noexcept
    {
        const auto la = maxLookaheadMs (p[maxLookahead]);
        const auto factor = oversamplingOf (p[oversampling]);
        if (la == latencyMs && factor == osFactor)
            return;
        latencyMs = la;
        osFactor = factor;
        laSamples = (int) std::lround (la * 0.001 * sr);
        const auto osLat = osFactor > 1 ? mixsuite::Oversampler::latency() : 0;
        audioDelay.setLength (laSamples);
        dryDelay.setLength (osLat);
        for (auto& o : os)
            o.setFactor (std::max (2, osFactor));
        latency.store (laSamples + osLat, std::memory_order_relaxed);
    }

    void designScEq (const EffectParams& p) noexcept
    {
        EffectParams fp;
        scEqParams (p, fp);
        bool any = false;
        for (int b = 0; b < scBands; ++b)
        {
            std::array<facet::Section, facet::maxSections> sec;
            const auto count = facet::design (fp, b, sr, sec);
            scCount[(std::size_t) b] = count;
            for (int k = 0; k < count; ++k)
            {
                auto& q = scEq[(std::size_t) b][(std::size_t) k];
                q.b0 = sec[(std::size_t) k].b0; q.b1 = sec[(std::size_t) k].b1; q.b2 = sec[(std::size_t) k].b2;
                q.a1 = sec[(std::size_t) k].a1; q.a2 = sec[(std::size_t) k].a2;
            }
            any = any || count > 0;
        }
        scEqOn = any;
    }

    void chunk (float* l, float* r, int n, const EffectParams& p, int offset) noexcept
    {
        const auto inPeak = blockPeak (l, r, n);
        const auto st = styleOf (p[style]);
        const auto& sd = styleDefs[(int) st];
        const auto thr = thresholdDb (p[threshold]);
        const auto rat = ratioOf (p[ratio]);
        const auto kn = kneeDb (p[knee]);
        const auto rng = std::max (0.01, rangeDb (p[range]));
        const auto attMs = std::max (0.002, attackMs (p[attack]) * sd.attackScale);
        const auto relKnob = releaseMs (p[release]);
        const auto relMs = std::max (0.5, releaseTau (sd, relKnob));
        const bool autoRel = on (p[autoRelease]) && sd.curve != Curve::twoWay;
        const auto window = sd.windowMs > 0.0 ? std::clamp ((int) (sd.windowMs * 0.001 * sr), 1, peakWindow) : 0;
        const auto aA = coef (attMs, sr);
        const auto holdSamples = (int) (holdMs (p[hold]) * 0.001 * sr);
        const auto la = std::min (lookaheadMs (p[lookahead]), latencyMs);
        const auto gainLag = std::max (0, laSamples - (int) std::lround (la * 0.001 * sr));
        for (auto& g : gainDelay)
            g.setLength (gainLag);
        const auto ch = characterOf (p[character]);
        const bool pre = on (p[characterPre]);
        const auto drv = (float) dbToGain (driveDb (p[drive]));
        const auto scGain = (float) dbToGain (scLevelDb (p[scLevel]));
        const bool external = on (p[scExternal]) && scL != nullptr && scR != nullptr;
        const auto link = (double) std::clamp (p[stereoLink], 0.0f, 1.0f);
        const bool listen = on (p[scListen]);
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        float makeup = 1.0f;
        if (on (p[autoGain]) && sd.curve != Curve::upward)
            makeup = (float) dbToGain (std::min (rng, -0.5 * curveGain (st, 0.0, thr, rat, kn, rng)));
        designScEq (p);
        const auto slowCoef = coef (1000.0, sr);
        double maxGr = 0.0;

        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * inGain, b = r[i] * inGain;
            const float da = a, db = b; // dry (before the look-ahead delay)

            // side chain
            float sa, sb;
            if (external)
            {
                sa = scL[offset + i] * inGain;
                sb = scR[offset + i] * inGain;
            }
            else if (pre && ch != Character::off)
            {
                sa = characterCurve (ch, a * drv) / drv;
                sb = characterCurve (ch, b * drv) / drv;
            }
            else
            {
                sa = a;
                sb = b;
            }
            sa *= scGain;
            sb *= scGain;
            if (scEqOn)
                for (int q = 0; q < scBands; ++q)
                    for (int k = 0; k < scCount[(std::size_t) q]; ++k)
                    {
                        auto& f = scEq[(std::size_t) q][(std::size_t) k];
                        sa = f.process (sa, 0);
                        sb = f.process (sb, 1);
                    }
            if (listen)
            {
                scOut[(std::size_t) i] = 0.5f * (sa + sb);
                continue;
            }

            const auto m = std::max (std::abs (sa), std::abs (sb));
            const double lv[2] { link * m + (1.0 - link) * std::abs (sa), link * m + (1.0 - link) * std::abs (sb) };
            const bool both = link >= 0.999;
            float gch[2] {};
            for (int c = 0; c < 2; ++c)
            {
                if (both && c == 1)
                {
                    gch[1] = gch[0];
                    break;
                }
                auto level = lv[c];
                if (window > 0) // the peak of the last `window` samples
                {
                    auto& ring = peaks[(std::size_t) c];
                    ring[(std::size_t) peakPos[(std::size_t) c]] = (float) level;
                    peakPos[(std::size_t) c] = (peakPos[(std::size_t) c] + 1) % window;
                    for (int k = 0; k < window; ++k)
                        level = std::max (level, (double) ring[(std::size_t) k]);
                }
                const auto x = std::max (-120.0, 20.0 * std::log10 (level + 1e-12));
                auto& e = env[(std::size_t) c];
                if (x > e)
                {
                    e = aA * e + (1.0 - aA) * x;
                    holdLeft[(std::size_t) c] = holdSamples;
                }
                else if (holdLeft[(std::size_t) c] > 0)
                    --holdLeft[(std::size_t) c];
                else
                {
                    auto ms = relMs;
                    if (autoRel) // program-dependent: short peaks recover faster than sustained reduction
                    {
                        const auto now = -curveGain (st, e, thr, rat, kn, rng);
                        const auto fraction = now > 0.1 ? std::clamp (sustain[(std::size_t) c] / now, 0.0, 1.0) : 1.0;
                        ms *= 0.35 + 0.65 * fraction;
                    }
                    const auto aR = coef (ms, sr);
                    e = aR * e + (1.0 - aR) * x;
                }
                const auto g = curveGain (st, e, thr, rat, kn, rng);
                sustain[(std::size_t) c] = slowCoef * sustain[(std::size_t) c] + (1.0 - slowCoef) * std::max (0.0, -g);
                maxGr = std::max (maxGr, -g);
                gch[c] = (float) dbToGain (g);
            }

            // the audio (and its gain) through the look-ahead delay
            audioDelay.process (a, b);
            wl[(std::size_t) i] = a;
            wr[(std::size_t) i] = b;
            gl[(std::size_t) i] = gainDelay[0].process (gch[0]) * makeup;
            gr[(std::size_t) i] = gainDelay[1].process (gch[1]) * makeup;
            dl[(std::size_t) i] = da;
            dr[(std::size_t) i] = db;
        }

        if (listen)
        {
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = scOut[(std::size_t) i];
            meters.set (0, 0.0f);
            meters.set (2, inPeak);
            meters.set (3, blockPeak (l, r, n));
            return;
        }

        // gain (and the character) on the delayed audio, oversampled when asked
        if (osFactor > 1)
        {
            runOversampled (0, wl.data(), gl.data(), n, ch, pre, drv);
            runOversampled (1, wr.data(), gr.data(), n, ch, pre, drv);
        }
        else
            for (int i = 0; i < n; ++i)
            {
                wl[(std::size_t) i] = stage (wl[(std::size_t) i], gl[(std::size_t) i], ch, pre, drv);
                wr[(std::size_t) i] = stage (wr[(std::size_t) i], gr[(std::size_t) i], ch, pre, drv);
            }

        for (int i = 0; i < n; ++i)
        {
            float a = wl[(std::size_t) i], b = wr[(std::size_t) i];
            if (ch != Character::off)
            {
                a = dc.process (a, 0);
                b = dc.process (b, 1);
            }
            float da = dl[(std::size_t) i], db = dr[(std::size_t) i];
            audioDelayDry (da, db);
            l[i] = (da + (a - da) * mx) * outGain;
            r[i] = (db + (b - db) * mx) * outGain;
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
    }

    // the dry path gets the same delay as the wet one (look-ahead, then the oversampler)
    void audioDelayDry (float& a, float& b) noexcept
    {
        dryLa.setLength (laSamples);
        dryLa.process (a, b);
        dryDelay.process (a, b);
    }

    static float stage (float x, float g, Character ch, bool pre, float drv) noexcept
    {
        if (ch == Character::off)
            return x * g;
        if (pre)
            return characterCurve (ch, x * drv) / drv * g;
        return characterCurve (ch, x * g * drv) / drv;
    }

    void runOversampled (int c, float* x, const float* g, int n, Character ch, bool pre, float drv) noexcept
    {
        const int factor = osFactor;
        int calls = 0;
        auto& prev = prevGain[(std::size_t) c];
        os[(std::size_t) c].process (x, n, [&] (float v) noexcept {
            const int i = calls / factor, k = calls % factor;
            ++calls;
            const auto target = g[std::min (i, n - 1)];
            const auto gi = prev + (target - prev) * (float) (k + 1) / (float) factor;
            if (k == factor - 1)
                prev = target;
            return stage (v, gi, ch, pre, drv);
        });
    }

    double sr = 48000.0, latencyMs = -1.0;
    int osFactor = 0, laSamples = 0;
    std::atomic<int> latency { 0 };
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = crestInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    StereoDelay audioDelay, dryLa, dryDelay;
    std::array<MonoDelay, 2> gainDelay;
    std::array<mixsuite::Oversampler, 2> os;
    std::array<std::array<Biquad, facet::maxSections>, scBands> scEq;
    std::array<int, scBands> scCount {};
    bool scEqOn = false;
    std::array<double, 2> env { -120.0, -120.0 }, sustain {};
    std::array<int, 2> holdLeft {};
    std::array<float, 2> prevGain { 1.0f, 1.0f };
    static constexpr int peakWindow = 512;
    std::array<std::array<float, peakWindow>, 2> peaks {};
    std::array<int, 2> peakPos {};
    DcBlock dc;
    const float* scL = nullptr;
    const float* scR = nullptr;
    std::array<float, maxBlock> wl {}, wr {}, gl {}, gr {}, dl {}, dr {}, scOut {};
    LiveMeters meters;
};
} // namespace
} // namespace crest

// ================================================================== Prism Multiband

namespace prism
{
double thresholdDb (float v) noexcept { return -90.0 * (1.0 - std::sqrt ((double) std::clamp (v, 0.0f, 1.0f))); }
float thresholdValue (double db) noexcept
{
    const auto u = 1.0 - std::clamp (-db / 90.0, 0.0, 1.0);
    return (float) (u * u);
}
double ratioOf (float v) noexcept { return ratioLaw (v); }
double rangeDb (float v) noexcept { return lin (v, -30.0, 30.0); }
double kneeDb (float v) noexcept { return lin (v, 0.0, 48.0); }
double attackMs (float v) noexcept { return 0.3 + 240.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.9); }
double releaseMs (float v) noexcept { return 130.0 + 3500.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.0); }
double levelDb (float v) noexcept { return lin (v, -30.0, 30.0); }
double crossoverHz (float v) noexcept { return logMap (v, 30.0, 18000.0); }
float crossoverValue (double hz) noexcept { return logValue (std::clamp (hz, 30.0, 18000.0), 30.0, 18000.0); }
int slopeDbOct (float v) noexcept
{
    static constexpr int s[] { 12, 24, 48 };
    return s[pick (v, 3)];
}
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 20.0); }
double trimDb (float v) noexcept { return lin (v, -36.0, 36.0); }

namespace
{
// gain change (dB) of a band for a detector level x (dB)
double bandCurve (const EffectParams& p, int b, double x) noexcept
{
    const auto at = [&] (int f) { return p[(std::size_t) param (b, f)]; };
    const auto thr = thresholdDb (at (fThreshold));
    const auto r = ratioOf (at (fRatio));
    const auto rng = rangeDb (at (fRange));
    const auto kn = kneeDb (at (fKnee));
    if (std::abs (rng) < 0.01)
        return 0.0;
    const bool expand = on (at (fMode));
    if (! expand)
    {
        if (rng < 0.0) // downward compression above the threshold
            return -std::min (-rng, kneeCurve (x - thr, 1.0 - 1.0 / r, kn));
        return std::min (rng, kneeCurve (thr - x, 1.0 - 1.0 / r, kn)); // upward compression below it
    }
    if (rng < 0.0) // downward expansion below the threshold
        return -std::min (-rng, kneeCurve (thr - x, r - 1.0, kn));
    return std::min (rng, kneeCurve (x - thr, 1.0 - 1.0 / r, kn)); // upward expansion above it
}

using Split = LrSplit;

std::array<double, 3> crossovers (const EffectParams& p) noexcept
{
    std::array<double, 3> f { crossoverHz (p[xover1]), crossoverHz (p[xover2]), crossoverHz (p[xover3]) };
    f[1] = std::max (f[1], f[0] * 1.05);
    f[2] = std::max (f[2], f[1] * 1.05);
    return f;
}
} // namespace

double bandStaticDb (const EffectParams& p, int b, double inputDb)
{
    if (! on (p[(std::size_t) param (b, fEnabled)]))
        return 0.0;
    return bandCurve (p, b, inputDb) + levelDb (p[(std::size_t) param (b, fLevel)]);
}

double responseDb (const EffectParams& p, const double* bandGainDb, double sampleRate, double hz)
{
    const auto f = crossovers (p);
    const auto slope = slopeDbOct (p[Global::slope]);
    std::array<Split, 3> s;
    for (int k = 0; k < 3; ++k)
        s[(std::size_t) k].design (sampleRate, f[(std::size_t) k], slope);
    const auto w = 2.0 * pi * hz / sampleRate;
    // bands of the tree: B0 = L1, B1 = H1·L2, B2 = H1·H2·L3, B3 = H1·H2·H3; out = x + Σ (g − 1)·Bk
    const auto l1 = s[0].response (false, w), h1 = s[0].response (true, w);
    const auto l2 = s[1].response (false, w), h2 = s[1].response (true, w);
    const auto l3 = s[2].response (false, w), h3 = s[2].response (true, w);
    const std::complex<double> band[4] { l1, h1 * l2, h1 * h2 * l3, h1 * h2 * h3 };
    bool solo = false;
    for (int b = 0; b < bands; ++b)
        solo = solo || on (p[(std::size_t) param (b, fSolo)]);
    std::complex<double> y = solo ? std::complex<double> {} : std::complex<double> { 1.0, 0.0 };
    for (int b = 0; b < bands; ++b)
    {
        const auto g = dbToGain (bandGainDb[b]);
        if (solo)
        {
            if (on (p[(std::size_t) param (b, fSolo)]))
                y += g * band[b];
        }
        else
            y += (g - 1.0) * band[b];
    }
    return gainToDb (std::abs (y)) + trimDb (p[output]);
}

namespace
{
class Multiband final : public Effect
{
public:
    explicit Multiband (const EffectParams* initial)
    {
        if (initial != nullptr)
            configure (*initial);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        laMs = -1.0;
        configure (lastParams);
        designed = false;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& s : split)
            s.reset();
        delay.reset();
        for (auto& d : bandDelay)
            d.reset();
        env.fill (-120.0);
        gains.fill (1.0f);
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        lastParams = p;
        configure (p);
        const auto f = crossovers (p);
        const auto slope = slopeDbOct (p[Global::slope]);
        if (! designed || f != lastF || slope != lastSlope)
        {
            for (int k = 0; k < 3; ++k)
                split[(std::size_t) k].design (sr, f[(std::size_t) k], slope);
            lastF = f;
            lastSlope = slope;
            designed = true;
        }
        const auto inPeak = blockPeak (l, r, n);
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);

        std::array<double, bands> aA {}, aR {}, level {};
        std::array<bool, bands> enabled {}, soloed {};
        bool anySolo = false;
        for (int b = 0; b < bands; ++b)
        {
            aA[(std::size_t) b] = coef (attackMs (p[(std::size_t) param (b, fAttack)]), sr);
            aR[(std::size_t) b] = coef (releaseMs (p[(std::size_t) param (b, fRelease)]), sr);
            level[(std::size_t) b] = levelDb (p[(std::size_t) param (b, fLevel)]);
            enabled[(std::size_t) b] = on (p[(std::size_t) param (b, fEnabled)]);
            soloed[(std::size_t) b] = on (p[(std::size_t) param (b, fSolo)]);
            anySolo = anySolo || soloed[(std::size_t) b];
        }
        std::array<double, bands> extreme {};

        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i] * inGain, xr = r[i] * inGain;
            float bl[bands], br[bands];
            float lo, hi, lo2, hi2;
            split[0].run (xl, 0, bl[0], hi);
            split[1].run (hi, 0, bl[1], hi2);
            split[2].run (hi2, 0, bl[2], bl[3]);
            split[0].run (xr, 1, br[0], lo);
            split[1].run (lo, 1, br[1], lo2);
            split[2].run (lo2, 1, br[2], br[3]);

            float dl = xl, dr = xr;
            delay.process (dl, dr); // the look-ahead: the audio waits, the detectors don't
            float yl = anySolo ? 0.0f : dl, yr = anySolo ? 0.0f : dr;
            for (int b = 0; b < bands; ++b)
            {
                float g = 1.0f;
                if (enabled[(std::size_t) b])
                {
                    const auto m = std::max (std::abs (bl[b]), std::abs (br[b]));
                    const auto x = std::max (-120.0, 20.0 * std::log10 ((double) m + 1e-12));
                    auto& e = env[(std::size_t) b];
                    e = x > e ? aA[(std::size_t) b] * e + (1.0 - aA[(std::size_t) b]) * x
                              : aR[(std::size_t) b] * e + (1.0 - aR[(std::size_t) b]) * x;
                    const auto gdb = bandCurve (p, b, e);
                    if (std::abs (gdb) > std::abs (extreme[(std::size_t) b]))
                        extreme[(std::size_t) b] = gdb;
                    g = (float) dbToGain (gdb + level[(std::size_t) b]);
                }
                float sl = bl[b], sr2 = br[b];
                bandDelay[(std::size_t) b].process (sl, sr2);
                if (anySolo)
                {
                    if (soloed[(std::size_t) b])
                    {
                        yl += g * sl;
                        yr += g * sr2;
                    }
                }
                else if (g != 1.0f)
                {
                    yl += (g - 1.0f) * sl;
                    yr += (g - 1.0f) * sr2;
                }
            }
            l[i] = (dl + (yl - dl) * mx) * outGain;
            r[i] = (dr + (yr - dr) * mx) * outGain;
        }
        for (int b = 0; b < bands; ++b)
            meters.set (b, (float) std::clamp (extreme[(std::size_t) b] / 30.0, -1.0, 1.0));
        meters.set (4, inPeak);
        meters.set (5, blockPeak (l, r, n));
    }

private:
    void configure (const EffectParams& p) noexcept
    {
        const auto la = lookaheadMs (p[lookahead]);
        if (la == laMs)
            return;
        laMs = la;
        const auto s = (int) std::lround (la * 0.001 * sr);
        delay.setLength (s);
        for (auto& d : bandDelay)
            d.setLength (s);
        latency.store (s, std::memory_order_relaxed);
    }

    double sr = 48000.0, laMs = -1.0;
    std::atomic<int> latency { 0 };
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = prismInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    std::array<Split, 3> split;
    std::array<double, 3> lastF {};
    int lastSlope = 0;
    bool designed = false;
    StereoDelay delay;
    std::array<StereoDelay, bands> bandDelay;
    std::array<double, bands> env {};
    std::array<float, bands> gains {};
    LiveMeters meters;
};
} // namespace
} // namespace prism

// ================================================================== Sentry Gate

namespace sentry
{
Style styleOf (float v) noexcept { return (Style) pick (v, styles); }
float styleValue (Style s) noexcept { return (float) (int) s / (float) (styles - 1); }
double thresholdDb (float v) noexcept { return lin (v, -60.0, 0.0); }
double ratioOf (float v) noexcept { return ratioLaw (v); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 100.0); }
double attackMs (float v) noexcept { return 1000.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 4.0); }
double releaseMs (float v) noexcept { return 5000.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.0); }
double holdMs (float v) noexcept { return 250.0 * (double) v * (double) v; }
double kneeDb (float v) noexcept { return lin (v, 0.0, 30.0); }
double lookaheadMs (float v) noexcept { return lin (v, 0.0, 10.0); }
double filterHz (float v) noexcept { return logMap (v, 5.0, 30000.0); }
float filterValue (double hz) noexcept { return logValue (std::clamp (hz, 5.0, 30000.0), 5.0, 30000.0); }
double upThresholdDb (float v) noexcept { return lin (v, -30.0, 0.0); }
double upRatioOf (float v) noexcept { return lin (v, 1.0, 3.0); }

namespace
{
// style families (fitted to MEASURE_fabfilter §7): where an expander starts under the threshold (divided by
// ratio − 1, so a gate of high ratio opens right at the threshold), the fastest opening, release = base + scale ×
// knob, the share of the hold knob that delays the release
struct StyleDef
{
    double offsetDb, attackMinMs, attackScale, releaseBaseMs, releaseScale, holdScale;
};
constexpr StyleDef styleDefs[styles] {
    { 7.9, 0.0, 0.49, 45.0, 0.10, 0.50 },   // classic
    { 4.3, 0.15, 0.49, 120.0, 0.25, 0.50 }, // clean
    { 0.2, 17.0, 0.49, 100.0, 0.20, 0.50 }, // vocal
    { 0.0, 0.7, 0.49, 20.0, 0.05, 0.60 },   // guitar (its release waits ~110 ms, then falls fast)
    { 0.0, 0.0, 0.49, 45.0, 0.10, 0.50 },   // upward
};
constexpr double guitarWaitMs = 110.0;

// target gain (dB) for a detector level x
double gateCurve (const EffectParams& p, double x) noexcept
{
    const auto st = styleOf (p[style]);
    if (st == Style::upward)
    {
        const auto ut = upThresholdDb (p[upThreshold]);
        return kneeCurve (x - ut, 1.0 - 1.0 / upRatioOf (p[upRatio]), kneeDb (p[knee]));
    }
    const auto r = ratioOf (p[ratio]);
    if (r <= 1.0001)
        return 0.0;
    const auto start = thresholdDb (p[threshold]) - styleDefs[(int) st].offsetDb / std::max (1.0, r - 1.0);
    return -std::min (rangeDb (p[range]), kneeCurve (start - x, r - 1.0, kneeDb (p[knee])));
}
} // namespace

double hysteresisDb (Style s) noexcept { return styleDefs[(int) s].offsetDb; }

double staticGainDb (const EffectParams& p, double inputDb, bool)
{
    return gateCurve (p, inputDb);
}

namespace
{
class Gate final : public Effect
{
public:
    explicit Gate (const EffectParams* initial)
    {
        if (initial != nullptr)
            configure (*initial);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        laOn = -1;
        configure (lastParams);
        reset();
    }

    void reset() noexcept override
    {
        delay.reset();
        gainDelay.reset();
        hp.reset();
        lp.reset();
        level = 0.0;
        gain = 0.0;
        holdLeft = 0;
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }
    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        lastParams = p;
        configure (p);
        const auto inPeak = blockPeak (l, r, n);
        const auto st = styleOf (p[style]);
        const auto& sd = styleDefs[(int) st];
        const auto hpHz = filterHz (p[highPass]), lpHz = filterHz (p[lowPass]);
        const bool hpOn = hpHz > 6.0, lpOn = lpHz < std::min (29000.0, sr * 0.45);
        if (hpOn)
            hp.filter (sr, 1, std::min (hpHz, sr * 0.45), 0.70710678);
        if (lpOn)
            lp.filter (sr, 0, std::min (lpHz, sr * 0.45), 0.70710678);
        const bool external = on (p[scExternal]) && scL != nullptr && scR != nullptr;
        const bool listen = on (p[scListen]);
        const auto attMs = std::max (sd.attackMinMs, attackMs (p[attack]) * sd.attackScale);
        const auto aA = coef (attMs, sr);
        const auto aR = coef (sd.releaseBaseMs + sd.releaseScale * releaseMs (p[release]), sr);
        const auto holdSamples = (int) (holdMs (p[hold]) * sd.holdScale * 0.001 * sr)
                                 + (st == Style::guitar ? (int) (guitarWaitMs * 0.001 * sr) : 0);
        const auto levelRel = coef (5.0, sr);
        const auto dryGain = std::clamp (p[dry], 0.0f, 1.0f);
        const auto outGain = (float) dbToGain (lin (p[output], -24.0, 24.0));
        const auto la = laOn > 0 ? lookaheadMs (p[lookahead]) : 0.0;
        gainDelay.setLength (std::max (0, laSamples - (int) std::lround (la * 0.001 * sr)));
        double maxGr = 0.0;

        for (int i = 0; i < n; ++i)
        {
            float sa = external ? scL[i] : l[i], sb = external ? scR[i] : r[i];
            if (hpOn)
            {
                sa = hp.process (sa, 0);
                sb = hp.process (sb, 1);
            }
            if (lpOn)
            {
                sa = lp.process (sa, 0);
                sb = lp.process (sb, 1);
            }
            if (listen)
            {
                l[i] = sa;
                r[i] = sb;
                continue;
            }
            // peak detector: instant attack, 5 ms release
            const auto m = (double) std::max (std::abs (sa), std::abs (sb));
            level = m > level ? m : levelRel * level + (1.0 - levelRel) * m;
            const auto target = dbToGain (gateCurve (p, gainToDb (level)));
            // the gain moves in the linear domain: opening with the attack, closing (after hold) with the release
            if (target >= gain)
            {
                gain = aA * gain + (1.0 - aA) * target;
                holdLeft = holdSamples;
            }
            else if (holdLeft > 0)
                --holdLeft;
            else
                gain = aR * gain + (1.0 - aR) * target;
            maxGr = std::max (maxGr, -gainToDb (gain));
            const auto g = gainDelay.process ((float) gain);
            float a = l[i], b = r[i];
            delay.process (a, b);
            l[i] = (a * g + a * dryGain) * outGain;
            r[i] = (b * g + b * dryGain) * outGain;
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
        scL = scR = nullptr;
    }

private:
    // the look-ahead switch sets the latency (10 ms), the knob only how much of it the detector leads
    void configure (const EffectParams& p) noexcept
    {
        const int want = on (p[lookaheadOn]) ? 1 : 0;
        if (want == laOn)
            return;
        laOn = want;
        laSamples = want ? (int) std::lround (0.010 * sr) : 0;
        delay.setLength (laSamples);
        latency.store (laSamples, std::memory_order_relaxed);
    }

    double sr = 48000.0;
    int laOn = -1, laSamples = 0;
    std::atomic<int> latency { 0 };
    EffectParams lastParams = [] {
        EffectParams p {};
        const auto& info = sentryInfo();
        for (std::size_t i = 0; i < info.params.size(); ++i)
            p[i] = info.params[i].defaultValue;
        return p;
    }();
    StereoDelay delay;
    MonoDelay gainDelay;
    Biquad hp, lp;
    double level = 0.0, gain = 0.0;
    int holdLeft = 0;
    const float* scL = nullptr;
    const float* scR = nullptr;
    LiveMeters meters;
};
} // namespace
} // namespace sentry

// ================================================================== registry

const EffectInfo& crestInfo()
{
    static const EffectInfo info { "Crest Compressor",
                                   { { "Style", 0.0f }, { "Threshold", 0.7f }, { "Ratio", 0.6f }, { "Knee", 0.0f }, { "Range", 1.0f },
                                     { "Attack", 0.68f }, { "Release", 0.55f }, { "Auto release", 0.0f }, { "Hold", 0.0f },
                                     { "Lookahead", 0.0f }, { "Max lookahead", 0.0f },
                                     { "Character", 0.0f }, { "Character pre", 0.0f }, { "Drive", 0.5f },
                                     { "SC level", 0.5f }, { "External SC", 0.0f }, { "Stereo link", 1.0f }, { "SC listen", 0.0f },
                                     { "SC1 shape", 0.0f }, { "SC1 freq.", 0.30f }, { "SC1 gain", 0.5f }, { "SC1 Q", 0.5f }, { "SC1 slope", 0.125f },
                                     { "SC2 shape", 0.0f }, { "SC2 freq.", 0.575f }, { "SC2 gain", 0.5f }, { "SC2 Q", 0.5f }, { "SC2 slope", 0.125f },
                                     { "SC3 shape", 0.0f }, { "SC3 freq.", 0.80f }, { "SC3 gain", 0.5f }, { "SC3 Q", 0.5f }, { "SC3 slope", 0.125f },
                                     { "Oversampling", 0.0f }, { "Auto gain", 1.0f }, { "Mix", 1.0f }, { "Input", 0.5f }, { "Output", 0.5f } } };
    return info;
}

const EffectInfo& prismInfo()
{
    static const EffectInfo info = [] {
        EffectInfo i { "Prism Multiband", {} };
        static const char* names[prism::bands][prism::fieldsPerBand] {
            { "B1 threshold", "B1 ratio", "B1 range", "B1 expand", "B1 knee", "B1 attack", "B1 release", "B1 level", "B1 on", "B1 solo" },
            { "B2 threshold", "B2 ratio", "B2 range", "B2 expand", "B2 knee", "B2 attack", "B2 release", "B2 level", "B2 on", "B2 solo" },
            { "B3 threshold", "B3 ratio", "B3 range", "B3 expand", "B3 knee", "B3 attack", "B3 release", "B3 level", "B3 on", "B3 solo" },
            { "B4 threshold", "B4 ratio", "B4 range", "B4 expand", "B4 knee", "B4 attack", "B4 release", "B4 level", "B4 on", "B4 solo" }
        };
        // defaults: −18 dB, 4.5 : 1, range −12 dB (downward), knee 24 dB, attack 20 %, release 20 %, 0 dB, on
        const float defaults[prism::fieldsPerBand] { prism::thresholdValue (-18.0), 0.6f, 0.3f, 0.0f, 0.5f, 0.2f, 0.2f, 0.5f, 1.0f, 0.0f };
        for (int b = 0; b < prism::bands; ++b)
            for (int f = 0; f < prism::fieldsPerBand; ++f)
                i.params.push_back ({ names[b][f], defaults[f] });
        i.params.push_back ({ "Crossover 1", prism::crossoverValue (120.0) });
        i.params.push_back ({ "Crossover 2", prism::crossoverValue (1000.0) });
        i.params.push_back ({ "Crossover 3", prism::crossoverValue (6000.0) });
        i.params.push_back ({ "Slope", 0.5f });
        i.params.push_back ({ "Lookahead", 0.0f });
        i.params.push_back ({ "Mix", 1.0f });
        i.params.push_back ({ "Input", 0.5f });
        i.params.push_back ({ "Output", 0.5f });
        return i;
    }();
    return info;
}

const EffectInfo& sentryInfo()
{
    static const EffectInfo info { "Sentry Gate",
                                   { { "Style", 0.0f }, { "Threshold", 0.5f }, { "Ratio", 1.0f }, { "Range", 1.0f }, { "Attack", 0.1f },
                                     { "Release", 0.31f }, { "Hold", 0.28f }, { "Knee", 0.0f }, { "Lookahead", 0.0f },
                                     { "Lookahead on", 0.0f }, { "High pass", 0.0f }, { "Low pass", 1.0f }, { "External SC", 0.0f },
                                     { "SC listen", 0.0f }, { "Up threshold", 0.8f }, { "Up ratio", 0.25f }, { "Dry", 0.0f },
                                     { "Output", 0.5f } } };
    return info;
}

bool isDynKind (EffectKind kind) noexcept
{
    return kind == EffectKind::crestCompressor || kind == EffectKind::prismMultiband || kind == EffectKind::sentryGate;
}

std::unique_ptr<Effect> make (EffectKind kind, const EffectParams* initial)
{
    switch (kind)
    {
        case EffectKind::crestCompressor: return std::make_unique<crest::Compressor> (initial);
        case EffectKind::prismMultiband: return std::make_unique<prism::Multiband> (initial);
        case EffectKind::sentryGate: return std::make_unique<sentry::Gate> (initial);
        default: return nullptr;
    }
}

std::string paramText (EffectKind kind, int param, float v)
{
    const auto db = [] (double x) { return fmt ("%+.1f dB", x); };
    if (kind == EffectKind::crestCompressor)
    {
        using namespace crest;
        if (param >= sc1Shape && param <= sc3Slope)
        {
            const auto field = (param - sc1Shape) % 5;
            switch (field)
            {
                case 0: return facet::shapeName (facet::shape (v));
                case 1: return hzText (facet::hz (v));
                case 2: return db (facet::gainDb (v));
                case 3: return fmt ("Q %.2f", facet::q (v));
                default: return std::to_string (facet::slopeDb (v)) + " dB/oct";
            }
        }
        switch (param)
        {
            case style:
            {
                static const char* names[styles] { "Clean", "Allround", "Punch", "Upward", "Two-way", "Vari-mu", "Classic", "Opto",
                                                   "Vocal", "Bus", "Pumping" };
                return names[(int) styleOf (v)];
            }
            case threshold: return fmt ("%.1f dB", thresholdDb (v));
            case ratio: return ratioText (ratioOf (v));
            case knee: return fmt ("%.1f dB", kneeDb (v));
            case range: return fmt ("%.1f dB", rangeDb (v));
            case attack: return msText (attackMs (v));
            case release: return msText (releaseMs (v));
            case hold: return msText (holdMs (v));
            case lookahead: return msText (lookaheadMs (v));
            case maxLookahead: return maxLookaheadMs (v) <= 0.0 ? "off" : msText (maxLookaheadMs (v));
            case character:
            {
                static const char* names[] { "off", "warm", "crisp", "bright" };
                return names[(int) characterOf (v)];
            }
            case characterPre: return on (v) ? "pre" : "post";
            case drive: return db (driveDb (v));
            case scLevel: return db (scLevelDb (v));
            case stereoLink: case mix: return fmt ("%.0f%%", v * 100.0);
            case oversampling: return oversamplingOf (v) == 1 ? "off" : std::to_string (oversamplingOf (v)) + "x";
            case input: case output: return db (trimDb (v));
            default: return on (v) ? "on" : "off";
        }
    }
    if (kind == EffectKind::prismMultiband)
    {
        using namespace prism;
        if (param < bands * fieldsPerBand)
        {
            switch (param % fieldsPerBand)
            {
                case fThreshold: return fmt ("%.1f dB", thresholdDb (v));
                case fRatio: return ratioText (ratioOf (v));
                case fRange: return db (rangeDb (v));
                case fMode: return on (v) ? "expand" : "compress";
                case fKnee: return fmt ("%.1f dB", kneeDb (v));
                case fAttack: case fRelease: return fmt ("%.0f%%", v * 100.0);
                case fLevel: return db (levelDb (v));
                default: return on (v) ? "on" : "off";
            }
        }
        switch (param)
        {
            case xover1: case xover2: case xover3: return hzText (crossoverHz (v));
            case slope: return std::to_string (slopeDbOct (v)) + " dB/oct";
            case lookahead: return v <= 0.0f ? "off" : msText (lookaheadMs (v));
            case mix: return fmt ("%.0f%%", v * 100.0);
            default: return db (trimDb (v));
        }
    }
    if (kind == EffectKind::sentryGate)
    {
        using namespace sentry;
        switch (param)
        {
            case style:
            {
                static const char* names[styles] { "Classic", "Clean", "Vocal", "Guitar", "Upward" };
                return names[(int) styleOf (v)];
            }
            case threshold: return fmt ("%.1f dB", thresholdDb (v));
            case ratio: return ratioText (ratioOf (v));
            case range: return fmt ("%.1f dB", rangeDb (v));
            case attack: return msText (attackMs (v));
            case release: return msText (releaseMs (v));
            case hold: return msText (holdMs (v));
            case knee: return fmt ("%.1f dB", kneeDb (v));
            case lookahead: return msText (lookaheadMs (v));
            case highPass: return v <= 0.0f ? "off" : hzText (filterHz (v));
            case lowPass: return v >= 1.0f ? "off" : hzText (filterHz (v));
            case upThreshold: return fmt ("%.1f dB", upThresholdDb (v));
            case upRatio: return fmt ("%.2f : 1", upRatioOf (v));
            case dry: return fmt ("%.0f%%", v * 100.0);
            case output: return db (lin (v, -24.0, 24.0));
            default: return on (v) ? "on" : "off";
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::dyn
