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
#include "core/MinPhaseBand.h"
#include "core/MixSuite.h"
#include "core/ParametricEqLinear.h"

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
double panOf (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); }
double trimDb (float v) noexcept { return lin (v, -36.0, 36.0); }
LinkMode linkModeOf (float v) noexcept { return (LinkMode) pick (v, linkModes); }
Trigger triggerOf (float v) noexcept { return (Trigger) pick (v, 3); }
double triggerBeats (float v) noexcept { return 4.0 / (double) (1 << pick (v, 5)); }
double triggerRateOf (float v) noexcept { return std::pow (2.0, 2.0 * (double) std::clamp (v, 0.0f, 1.0f) - 1.0); }
double triggerLengthOf (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f); }
int triggerChannelOf (float v) noexcept
{
    const auto k = (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 128.0f);
    return k <= 0 ? -1 : k - 1;
}
// [EXEC] Pro-C 3 hosted at 120 BPM, a steady sine under the threshold: with Host Sync the gain reduction starts on
// every note × rate (1/4 at 100 % = every 500 ms, 50 % = 250 ms, 200 % = 1 s; 1/8 at 100 % = 250 ms) and holds for
// Length of that period (10 % = 50 ms, 50 % = 250 ms); its depth follows a 0 dBFS key (threshold −18 / −40 / 0 dB,
// ratio 4:1, knee 18 dB: 14 / 32 / 2 dB of reduction)
float hostTrigger (const EffectParams& p, double beats) noexcept
{
    const auto period = triggerBeats (p[triggerSync]) * triggerRateOf (p[triggerRate]);
    if (period <= 0.0 || beats < 0.0)
        return 0.0f;
    const auto phase = beats / period - std::floor (beats / period);
    return phase < triggerLengthOf (p[triggerLength]) ? 1.0f : 0.0f;
}
float linkModeValue (LinkMode m) noexcept { return (float) (int) m / (float) (linkModes - 1); }
double dryDb (float v) noexcept
{
    if (v <= 0.0f)
        return -120.0;
    if (v >= 0.25f)
        return 72.0 * (double) v - 36.0;
    return -18.0 + 40.0 * std::log10 ((double) v / 0.25); // amplitude ∝ v² below −18 dB
}
float dryValue (double db) noexcept
{
    if (db <= -119.0)
        return 0.0f;
    if (db >= -18.0)
        return (float) std::clamp ((db + 36.0) / 72.0, 0.25, 1.0);
    return (float) (0.25 * std::pow (10.0, (db + 18.0) / 40.0));
}
double meterRangeDb (float v) noexcept
{
    static constexpr double r[] { 9.0, 18.0, 36.0, 72.0, 90.0 };
    return r[pick (v, 5)];
}
double autoThresholdDb (double knobDb, double programRmsDb) noexcept
{
    return std::clamp (programRmsDb + knobDb + 18.0, -60.0, 0.0);
}

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
            // clean up to about +9 dBFS, then an asymmetric soft limit (the negative side gives first): even + odd
            constexpr float tp = 6.5f, tn = 3.0f, sp = 2.0f, sn = 1.5f;
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
            // an even, rectifier-like term (second harmonic from low levels) and a gentle, slightly asymmetric limit
            constexpr float s = 0.1f;
            const auto u = x + 0.007f * s * (std::sqrt (1.0f + (x / s) * (x / s)) - 1.0f);
            const auto limit = u >= 0.0f ? 7.75f : 5.5f;
            const auto a = std::abs (u / limit);
            const auto d = 1.0f + a * std::sqrt (a);
            return u / std::cbrt (d * d); // u / (1 + |u/L|^1.5)^(1/1.5)
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
        for (auto& d : gainDelay)
            d.assign ((std::size_t) gainCapacity, 1.0f);
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
        dryLa.reset();
        dryDelay.reset();
        for (auto& d : gainDelay)
            std::fill (d.begin(), d.end(), 1.0f);
        gainPos = 0;
        for (auto& o : os)
            o.reset();
        for (auto& o : osSide)
            o.reset();
        for (auto& b : scEq)
            for (auto& s : b)
                s.reset();
        for (auto& d : det)
            d = {};
        for (auto& pk : peaks)
            pk.fill (0.0f);
        dc.reset();
    }

    int latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void setSidechain (const float* l, const float* r) noexcept override
    {
        scL = l;
        scR = r;
    }
    // round 137: the trigger's clock and notes
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) ppq; }
    void setPlaying (bool playing) noexcept override { transportPlaying = playing; }
    void setHeldNotes (const NoteMask* channels, int count) noexcept override
    {
        held = channels;
        heldCount = count;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        lastParams = p;
        configure (p);
        designScEq (p);
        blockBpm = bpm > 1.0 ? bpm : 120.0;
        for (int off = 0; off < n; off += maxBlock)
            chunk (l + off, r + off, std::min (maxBlock, n - off), p, off);
        scL = scR = nullptr;
    }

private:
    static constexpr int maxBlock = 1024, maxFactor = 8;
    static constexpr int gainCapacity = 1 << 17; // the side chain's lead at up to 8x the rate (20 ms at 768 kHz)
    static constexpr int peakWindow = 4096;

    // the detector of one channel
    struct Detector
    {
        double env = -120.0, sustain = 0.0;
        int holdLeft = 0, peakPos = 0;
    };

    // per-chunk settings at the rate the detector runs at
    struct Ctx
    {
        Style st;
        double thr, rat, kn, rng, aA, relMs, rate, slowCoef;
        int holdSamples, window;
        bool autoRel;
    };

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
        dryLa.setLength (laSamples);
        dryDelay.setLength (osLat);
        for (auto* set : { &os, &osSide })
            for (auto& o : *set)
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

    Ctx context (const EffectParams& p, double rate) const noexcept
    {
        Ctx c;
        c.st = styleOf (p[style]);
        const auto& sd = styleDefs[(int) c.st];
        c.thr = on (p[autoThreshold]) ? autoThresholdDb (thresholdDb (p[threshold]), programDb) : thresholdDb (p[threshold]);
        c.rat = ratioOf (p[ratio]);
        c.kn = kneeDb (p[knee]);
        c.rng = std::max (0.01, rangeDb (p[range]));
        c.aA = coef (std::max (0.002, attackMs (p[attack]) * sd.attackScale), rate);
        c.relMs = std::max (0.5, releaseTau (sd, releaseMs (p[release])));
        c.autoRel = on (p[autoRelease]) && sd.curve != Curve::twoWay;
        c.rate = rate;
        c.slowCoef = coef (1000.0, rate);
        c.holdSamples = (int) (holdMs (p[hold]) * 0.001 * rate);
        c.window = sd.windowMs > 0.0 ? std::clamp ((int) (sd.windowMs * 0.001 * rate), 1, peakWindow) : 0;
        return c;
    }

    // one detector step: the level → the gain (dB) the compressor applies now
    double detect (int c, double level, const Ctx& x) noexcept
    {
        auto& d = det[(std::size_t) c];
        if (x.window > 0) // the peak of the last `window` samples
        {
            auto& ring = peaks[(std::size_t) c];
            ring[(std::size_t) (d.peakPos % x.window)] = (float) level;
            d.peakPos = (d.peakPos + 1) % x.window;
            for (int k = 0; k < x.window; ++k)
                level = std::max (level, (double) ring[(std::size_t) k]);
        }
        const auto in = std::max (-120.0, 20.0 * std::log10 (level + 1e-12));
        if (in > d.env)
        {
            d.env = x.aA * d.env + (1.0 - x.aA) * in;
            d.holdLeft = x.holdSamples;
        }
        else if (d.holdLeft > 0)
            --d.holdLeft;
        else
        {
            auto ms = x.relMs;
            if (x.autoRel) // program-dependent: short peaks recover faster than sustained reduction
            {
                const auto now = -curveGain (x.st, d.env, x.thr, x.rat, x.kn, x.rng);
                const auto fraction = now > 0.1 ? std::clamp (d.sustain / now, 0.0, 1.0) : 1.0;
                ms *= 0.35 + 0.65 * fraction;
            }
            const auto aR = coef (ms, x.rate);
            d.env = aR * d.env + (1.0 - aR) * in;
        }
        const auto g = curveGain (x.st, d.env, x.thr, x.rat, x.kn, x.rng);
        d.sustain = x.slowCoef * d.sustain + (1.0 - x.slowCoef) * std::max (0.0, -g);
        return g;
    }

    // the gains of a run of side-chain samples (per channel, or linked), through the look-ahead's gain delay
    double gains (const float* a, const float* b, int count, double link, int lag, const Ctx& x, float* outL, float* outR) noexcept
    {
        double maxGr = 0.0;
        const bool both = link >= 0.999;
        for (int i = 0; i < count; ++i)
        {
            const auto la = std::abs (a[i]), lb = std::abs (b[i]);
            const auto m = (double) std::max (la, lb);
            float g[2];
            const auto g0 = detect (0, link * m + (1.0 - link) * la, x);
            g[0] = (float) dbToGain (g0);
            maxGr = std::max (maxGr, -g0);
            if (both)
                g[1] = g[0];
            else
            {
                const auto g1 = detect (1, link * m + (1.0 - link) * lb, x);
                g[1] = (float) dbToGain (g1);
                maxGr = std::max (maxGr, -g1);
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& line = gainDelay[(std::size_t) c];
                line[(std::size_t) gainPos] = g[c];
                const auto rd = (gainPos - lag + gainCapacity) % gainCapacity;
                (c == 0 ? outL : outR)[i] = line[(std::size_t) rd];
            }
            gainPos = (gainPos + 1) % gainCapacity;
        }
        return maxGr;
    }

    void chunk (float* l, float* r, int n, const EffectParams& p, int offset) noexcept
    {
        const auto inPeak = blockPeak (l, r, n);
        const auto ch = characterOf (p[character]);
        const bool pre = on (p[characterPre]);
        const auto drv = (float) dbToGain (driveDb (p[drive]));
        const auto scGain = (float) dbToGain (scLevelDb (p[scLevel]));
        const bool external = on (p[scExternal]) && scL != nullptr && scR != nullptr;
        // round 137: the trigger replaces the detector's signal (a 0 dBFS key × the side-chain level)
        const auto trig = triggerOf (p[trigger]);
        float midiKey = 0.0f;
        if (trig == Trigger::midi)
            if (const auto c = triggerChannelOf (p[triggerChannel]); c >= 0 && held != nullptr && c < heldCount)
                midiKey = (held[c][0] | held[c][1]) != 0 ? 1.0f : 0.0f;
        const auto beat0 = songBeats + (double) offset * blockBpm / 60.0 / sr, beatStep = blockBpm / 60.0 / sr;
        const auto link = (double) std::clamp (p[stereoLink], 0.0f, 1.0f);
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto st = styleOf (p[style]);
        float makeup = 1.0f;
        if (on (p[autoGain]) && styleDefs[(int) st].curve != Curve::upward)
        {
            const auto rng = std::max (0.01, rangeDb (p[range]));
            makeup = (float) dbToGain (std::min (rng, -0.5 * curveGain (st, 0.0, thresholdDb (p[threshold]), ratioOf (p[ratio]), kneeDb (p[knee]), rng)));
        }
        const auto mode = linkModeOf (p[linkMode]);
        const bool ms = mode != LinkMode::stereo;
        const auto dryG = (float) (p[dryGain] > 0.0f ? dbToGain (dryDb (p[dryGain])) : 0.0);
        const bool autoThr = on (p[autoThreshold]);
        const auto la = std::min (lookaheadMs (p[lookahead]), latencyMs);
        const auto factor = osFactor > 1 ? osFactor : 1;
        const auto lag = std::clamp ((int) std::lround ((latencyMs - la) * 0.001 * sr * factor), 0, gainCapacity - 1);

        // the side chain (base rate) and the audio through the look-ahead delay
        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * inGain, b = r[i] * inGain;
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
            if (ms)
            {
                const auto m = 0.5f * (sa + sb), d = 0.5f * (sa - sb);
                sa = m;
                sb = d;
            }
            if (scEqOn)
                for (int q = 0; q < scBands; ++q)
                    for (int k = 0; k < scCount[(std::size_t) q]; ++k)
                    {
                        auto& f = scEq[(std::size_t) q][(std::size_t) k];
                        sa = f.process (sa, 0);
                        sb = f.process (sb, 1);
                    }
            if (trig != Trigger::off)
                sa = sb = scGain * (trig == Trigger::midi ? midiKey : (transportPlaying ? hostTrigger (p, beat0 + beatStep * (double) i) : 0.0f));
            sideL[(std::size_t) i] = sa;
            sideR[(std::size_t) i] = sb;
            if (autoThr)
                programSum += 0.5 * ((double) sa * sa + (double) sb * sb);
            dl[(std::size_t) i] = a;
            dr[(std::size_t) i] = b;
            if (ms)
            {
                const auto m = 0.5f * (a + b), d = 0.5f * (a - b);
                a = m;
                b = d;
            }
            audioDelay.process (a, b);
            wl[(std::size_t) i] = a;
            wr[(std::size_t) i] = b;
        }

        if (on (p[scListen])) // the detector's signal
        {
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = 0.5f * (sideL[(std::size_t) i] + sideR[(std::size_t) i]);
            meters.set (0, 0.0f);
            meters.set (2, inPeak);
            meters.set (3, blockPeak (l, r, n));
            return;
        }

        if (autoThr) // the programme's slow RMS level (3 s), block by block
        {
            const auto ms2 = programSum / std::max (1, n);
            programSum = 0.0;
            const auto k = std::exp (-(double) n / (3.0 * sr));
            programMs = k * programMs + (1.0 - k) * ms2;
            programDb = std::max (-120.0, 10.0 * std::log10 (programMs + 1e-15));
        }
        const auto detLink = ms ? 0.0 : link; // mid / side: each detector on its own channel
        double maxGr = 0.0;
        if (factor == 1)
        {
            maxGr = gains (sideL.data(), sideR.data(), n, detLink, lag, context (p, sr), gl.data(), gr.data());
            if (ms)
                routeMs (mode, gl.data(), gr.data(), n);
            for (int i = 0; i < n; ++i)
            {
                wl[(std::size_t) i] = stage (wl[(std::size_t) i], gl[(std::size_t) i] * makeup, ch, pre, drv);
                wr[(std::size_t) i] = stage (wr[(std::size_t) i], gr[(std::size_t) i] * makeup, ch, pre, drv);
            }
        }
        else
        {
            // oversampled: the side chain is upsampled (the same path as the audio, so both stay aligned), the
            // detector and the gain run at the high rate, and the gain multiplies the upsampled audio
            const auto m = n * factor;
            for (int c = 0; c < 2; ++c)
            {
                auto* src = c == 0 ? sideL.data() : sideR.data();
                auto* dst = c == 0 ? upL.data() : upR.data();
                int k = 0;
                osSide[(std::size_t) c].process (src, n, [&] (float v) noexcept {
                    dst[std::min (k++, m - 1)] = v;
                    return 0.0f;
                });
            }
            maxGr = gains (upL.data(), upR.data(), m, detLink, lag, context (p, sr * factor), upGl.data(), upGr.data());
            if (ms)
                routeMs (mode, upGl.data(), upGr.data(), m);
            for (int c = 0; c < 2; ++c)
            {
                const auto* g = c == 0 ? upGl.data() : upGr.data();
                int k = 0;
                os[(std::size_t) c].process (c == 0 ? wl.data() : wr.data(), n, [&] (float v) noexcept {
                    return stage (v, g[std::min (k++, m - 1)] * makeup, ch, pre, drv);
                });
            }
        }

        float wetA = 1.0f, wetB = 1.0f, dryA = 1.0f, dryB = 1.0f;
        {
            const auto balance = [] (double pan, float& a, float& b) {
                if (pan > 0.0) a = (float) (1.0 - pan);
                else if (pan < 0.0) b = (float) (1.0 + pan);
            };
            balance (panOf (p[wetPan]), wetA, wetB);
            balance (panOf (p[dryPan]), dryA, dryB);
        }
        const bool panned = wetA != 1.0f || wetB != 1.0f || dryA != 1.0f || dryB != 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float a = wl[(std::size_t) i], b = wr[(std::size_t) i];
            if (ms)
            {
                const auto m = a, d = b;
                a = m + d;
                b = m - d;
            }
            if (ch != Character::off)
            {
                a = dc.process (a, 0);
                b = dc.process (b, 1);
            }
            float da = dl[(std::size_t) i], db = dr[(std::size_t) i];
            dryLa.process (da, db);
            dryDelay.process (da, db);
            if (panned) // round 135: the wet and dry paths balanced on their own
            {
                l[i] = (a * mx * wetA + da * ((1.0f - mx) + dryG) * dryA) * outGain;
                r[i] = (b * mx * wetB + db * ((1.0f - mx) + dryG) * dryB) * outGain;
            }
            else if (dryG > 0.0f) // round 128: the dry level added on top (parallel compression)
            {
                l[i] = (da + (a - da) * mx + da * dryG) * outGain;
                r[i] = (db + (b - db) * mx + db * dryG) * outGain;
            }
            else
            {
                l[i] = (da + (a - da) * mx) * outGain;
                r[i] = (db + (b - db) * mx) * outGain;
            }
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
    }

    // mid / side link modes: the gains computed per channel (0 = mid, 1 = side) → where they apply
    static void routeMs (LinkMode mode, float* gm, float* gs, int count) noexcept
    {
        for (int i = 0; i < count; ++i)
            switch (mode)
            {
                case LinkMode::mid: gs[i] = 1.0f; break;
                case LinkMode::side: gm[i] = 1.0f; break;
                case LinkMode::midToSide: gs[i] = gm[i]; gm[i] = 1.0f; break;
                case LinkMode::sideToMid: gm[i] = gs[i]; gs[i] = 1.0f; break;
                case LinkMode::stereo: break;
            }
    }

    static float stage (float x, float g, Character ch, bool pre, float drv) noexcept
    {
        if (ch == Character::off)
            return x * g;
        if (pre)
            return characterCurve (ch, x * drv) / drv * g;
        return characterCurve (ch, x * g * drv) / drv;
    }

    double sr = 48000.0, latencyMs = -1.0;
    double programSum = 0.0, programMs = 1e-4, programDb = -40.0; // auto threshold: the programme's slow RMS
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
    std::array<std::vector<float>, 2> gainDelay;
    int gainPos = 0;
    std::array<mixsuite::Oversampler, 2> os, osSide;
    std::array<std::array<Biquad, facet::maxSections>, scBands> scEq;
    std::array<int, scBands> scCount {};
    bool scEqOn = false;
    std::array<Detector, 2> det {};
    std::array<std::array<float, peakWindow>, 2> peaks {};
    DcBlock dc;
    const float* scL = nullptr;
    const float* scR = nullptr;
    // round 137: the trigger's clock (quarter notes at the block start) and the held notes
    double songBeats = 0.0, blockBpm = 120.0;
    bool transportPlaying = false;
    const NoteMask* held = nullptr;
    int heldCount = 0;
    std::array<float, maxBlock> wl {}, wr {}, gl {}, gr {}, dl {}, dr {}, sideL {}, sideR {};
    std::array<float, maxBlock * maxFactor> upL {}, upR {}, upGl {}, upGr {};
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
double attackMs (float v) noexcept { return 0.11 + 89.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 3.9); }
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
Phase phaseOf (float v) noexcept
{
    const auto k = pick (v, 3);
    return k == 0 ? Phase::linear : (k == 1 ? Phase::dynamic : Phase::minimum);
}
bool measuredDynamic (const EffectParams& p) noexcept { return on (p[dynamicEngine]); }
int oversamplingOf (float v) noexcept { return 1 << pick (v, 3); }
int linearFftSize (double sampleRate) noexcept
{
    int n = 4096;
    for (double r = 50000.0; sampleRate > r && n < 16384; r *= 2.0)
        n *= 2;
    return n;
}
int linearLatency (double sampleRate) noexcept { return 3 * linearFftSize (sampleRate) / 4; }


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

} // namespace

int bandCount (const EffectParams& p) noexcept { return 1 + pick (p[bandCountParam], bands); }
float bandCountValue (int n) noexcept { return (float) (std::clamp (n, 1, bands) - 1) / (float) (bands - 1); }
double panOf (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); }

std::array<double, bands - 1> crossovers (const EffectParams& p) noexcept
{
    std::array<double, bands - 1> f {};
    for (int k = 0; k < bands - 1; ++k)
    {
        f[(std::size_t) k] = crossoverHz (p[(std::size_t) crossoverParam (k)]);
        if (k > 0)
            f[(std::size_t) k] = std::max (f[(std::size_t) k], f[(std::size_t) (k - 1)] * 1.05);
    }
    return f;
}

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
    const auto nb = bandCount (p);
    const auto w = 2.0 * pi * hz / sampleRate;
    // bands of the tree: B0 = L1, B1 = H1·L2, B2 = H1·H2·L3, …, the last = H1·…·H(n−1); out = x + Σ (g − 1)·Bk
    std::array<std::complex<double>, bands> band {};
    std::complex<double> rest { 1.0, 0.0 };
    for (int k = 0; k < nb - 1; ++k)
    {
        Split s;
        s.design (sampleRate, f[(std::size_t) k], slope);
        band[(std::size_t) k] = rest * s.response (false, w);
        rest *= s.response (true, w);
    }
    band[(std::size_t) (nb - 1)] = rest;
    // round 144: the processing mode — linear phase: the same magnitudes, no phase; minimum phase: the lower bands
    // pass the all-passes of the crossovers above them, the output is the sum of the bands
    const auto mode = phaseOf (p[processingMode]);
    if (mode == Phase::linear)
        for (auto& b : band)
            b = std::abs (b);
    else if (mode == Phase::minimum)
        for (int k = 0; k < nb - 2; ++k)
            for (int j = k + 1; j < nb - 1; ++j)
            {
                Split s;
                s.design (sampleRate, f[(std::size_t) j], slope);
                band[(std::size_t) k] *= s.response (false, w) + s.response (true, w);
            }
    bool solo = false;
    for (int b = 0; b < nb; ++b)
        solo = solo || on (p[(std::size_t) param (b, fSolo)]);
    if (mode == Phase::dynamic && measuredDynamic (p) && ! solo)
    {
        // round 147: the measured engine — the product of each band's 1 + (g − 1)·A (its own crossovers' magnitudes)
        const auto n = slope <= 12 ? 1 : (slope <= 24 ? 2 : 4);
        const auto om = minphase::warp (hz, sampleRate);
        double m = 1.0;
        for (int b = 0; b < nb; ++b)
        {
            const auto wl = b > 0 ? minphase::warp (f[(std::size_t) (b - 1)], sampleRate) : 0.0;
            const auto wh = b < nb - 1 ? minphase::warp (f[(std::size_t) b], sampleRate) : 0.0;
            m *= minphase::bandMagnitude (wl, wh, n, dbToGain (bandGainDb[b]), om);
        }
        return gainToDb (std::abs (m)) + trimDb (p[output]);
    }
    std::complex<double> y = solo || mode == Phase::minimum ? std::complex<double> {} : std::complex<double> { 1.0, 0.0 };
    for (int b = 0; b < nb; ++b)
    {
        const auto g = dbToGain (bandGainDb[b]);
        if (solo)
        {
            if (on (p[(std::size_t) param (b, fSolo)]))
                y += g * band[b];
        }
        else if (mode == Phase::minimum)
            y += g * band[b];
        else
            y += (g - 1.0) * band[b];
    }
    return gainToDb (std::abs (y)) + trimDb (p[output]);
}

namespace
{
// a delay line sized in prepare() (the linear-phase path's dry signal: up to 12 288 samples at 192 kHz)
struct LongDelay
{
    std::vector<float> l, r;
    int pos = 0, len = 0;
    void allocate (int n) { l.assign ((std::size_t) n + 1, 0.0f); r.assign ((std::size_t) n + 1, 0.0f); len = n; pos = 0; }
    void reset() noexcept { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); pos = 0; }
    void process (float& a, float& b) noexcept
    {
        if (len == 0 || l.empty())
            return;
        const auto size = (int) l.size();
        l[(std::size_t) pos] = a;
        r[(std::size_t) pos] = b;
        auto rd = pos - len;
        if (rd < 0)
            rd += size;
        a = l[(std::size_t) rd];
        b = r[(std::size_t) rd];
        if (++pos == size)
            pos = 0;
    }
};

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
        // round 144: the linear-phase crossovers (one stereo FFT convolver per band below the top one) and the dry
        // delay that matches them, allocated here (process() never allocates)
        const auto size = linearFftSize (rate);
        if (size != linSize)
        {
            linSize = size;
            for (auto& c : lin)
                c.init (size);
            for (auto& t : linTable)
                t.assign ((std::size_t) (size / 2 + 1), 0.0f);
            linDry.allocate (linearLatency (rate));
            linSc.allocate (linearLatency (rate));
        }
        mode = -1;
        osFactor = 0;
        configure (lastParams);
        designed = false;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& s : split)
            s.reset();
        for (auto& s : scSplit)
            s.reset();
        for (auto& row : comp)
            for (auto& s : row)
                s.reset();
        delay.reset();
        for (auto& d : bandDelay)
            d.reset();
        for (auto& d : gainDelay)
            d.reset();
        for (auto& c : lin)
            if (c.n > 0)
                c.clear();
        linDry.reset();
        linSc.reset();
        resetOversamplers();
        for (auto& row : dynBand)
            for (auto& fl : row)
                fl.reset();
        dynWorks.fill (false);
        env.fill (-120.0);
        gains.fill (1.0f);
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
        extreme.fill (0.0);
        const auto* sl = scL;
        const auto* sr2 = scR;
        for (int i = 0; i < n; i += chunk)
        {
            const auto m = std::min (chunk, n - i);
            const bool sc = sl != nullptr && sr2 != nullptr;
            processChunk (l + i, r + i, sc ? sl + i : nullptr, sc ? sr2 + i : nullptr, m, p);
        }
        const auto nb = bandCount (p);
        for (int b = 0; b < bands; ++b)
            meters.set (bandMeter (b), b < nb ? (float) std::clamp (extreme[(std::size_t) b] / 30.0, -1.0, 1.0) : 0.0f);
        meters.set (4, inPeak);
        meters.set (5, blockPeak (l, r, n));
        scL = scR = nullptr;
    }

private:
    static constexpr int chunk = 256, maxFactor = 4;

    void processChunk (float* l, float* r, const float* scl, const float* scr, int n, const EffectParams& p) noexcept
    {
        const auto f = crossovers (p);
        const auto slope = slopeDbOct (p[Global::slope]);
        const auto nb = bandCount (p);
        const auto md = (Phase) mode;
        const bool linear = md == Phase::linear, minimum = md == Phase::minimum;
        if (! designed || f != lastF || slope != lastSlope)
        {
            for (int k = 0; k < bands - 1; ++k)
            {
                split[(std::size_t) k].design (sr, f[(std::size_t) k], slope);
                scSplit[(std::size_t) k].design (sr, f[(std::size_t) k], slope);
                for (int j = 0; j < bands - 1; ++j)
                    comp[(std::size_t) j][(std::size_t) k].design (sr, f[(std::size_t) k], slope);
            }
            lastF = f;
            lastSlope = slope;
            designed = true;
            kernelsDirty = true;
        }
        if (nb != lastNb)
        {
            lastNb = nb;
            kernelsDirty = true;
            for (auto& c : lin)
                if (c.n > 0)
                    c.clear();
        }
        if (linear && kernelsDirty)
            designKernels (nb);
        // round 135: the detectors hear the slot's side chain, split by the same crossovers
        const bool external = on (p[scExternal]) && scl != nullptr && scr != nullptr;
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) dbToGain (trimDb (p[output]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const bool msPan = on (p[panMode]);

        std::array<double, bands> aA {}, aR {}, level {};
        std::array<bool, bands> enabled {}, soloed {};
        std::array<float, bands> panL {}, panR {};
        bool anySolo = false;
        for (int b = 0; b < nb; ++b)
        {
            const auto pan = (float) panOf (p[(std::size_t) bandPanParam (b)]);
            panL[(std::size_t) b] = pan > 0.0f ? 1.0f - pan : 1.0f;
            panR[(std::size_t) b] = pan < 0.0f ? 1.0f + pan : 1.0f;
            aA[(std::size_t) b] = coef (attackMs (p[(std::size_t) param (b, fAttack)]), sr);
            aR[(std::size_t) b] = coef (releaseMs (p[(std::size_t) param (b, fRelease)]), sr);
            level[(std::size_t) b] = levelDb (p[(std::size_t) param (b, fLevel)]);
            enabled[(std::size_t) b] = on (p[(std::size_t) param (b, fEnabled)]);
            soloed[(std::size_t) b] = on (p[(std::size_t) param (b, fSolo)]);
            anySolo = anySolo || soloed[(std::size_t) b];
            // round 128: a band's own look-ahead (≤ the global one): its gain waits for the difference
            const auto bandLa = std::min (laMs, lookaheadMs (p[(std::size_t) bandLookaheadParam (b)]));
            gainDelay[(std::size_t) b].setLength (std::max (0, laSamples - (int) std::lround (bandLa * 0.001 * sr)));
        }

        // 1. the split, the detectors and the gains at the base rate; the dry signal, the bands and the gains are kept
        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i] * inGain, xr = r[i] * inGain;
            float bl[bands], br[bands], sl0[bands], sr0[bands];
            float dl = xl, dr = xr;
            if (linear)
            {
                // round 144: FIR bands (zero phase, delayed by the convolvers' latency); the top band is the rest, so
                // the bands always add up to the delayed input
                linDry.process (dl, dr);
                float restL = dl, restR = dr;
                for (int k = 0; k < nb - 1; ++k)
                {
                    float a = xl, c = xr;
                    lin[(std::size_t) k].step (a, c);
                    bl[k] = a;
                    br[k] = c;
                    restL -= a;
                    restR -= c;
                }
                bl[nb - 1] = restL;
                br[nb - 1] = restR;
            }
            else
            {
                // the crossover tree: each split takes the high part of the one before
                float hl = xl, hr = xr;
                for (int k = 0; k < nb - 1; ++k)
                {
                    float nl, nr;
                    split[(std::size_t) k].run (hl, 0, bl[k], nl);
                    split[(std::size_t) k].run (hr, 1, br[k], nr);
                    hl = nl;
                    hr = nr;
                }
                bl[nb - 1] = hl;
                br[nb - 1] = hr;
            }
            if (external)
            {
                float el = scl[i] * inGain, er = scr[i] * inGain;
                if (linear)
                    linSc.process (el, er); // the detectors stay in time with the delayed bands
                for (int k = 0; k < nb - 1; ++k)
                {
                    float nl, nr;
                    scSplit[(std::size_t) k].run (el, 0, sl0[k], nl);
                    scSplit[(std::size_t) k].run (er, 1, sr0[k], nr);
                    el = nl;
                    er = nr;
                }
                sl0[nb - 1] = el;
                sr0[nb - 1] = er;
            }

            delay.process (dl, dr); // the look-ahead: the audio waits, the detectors don't
            dry[0][(std::size_t) i] = dl;
            dry[1][(std::size_t) i] = dr;
            for (int b = 0; b < nb; ++b)
            {
                float g = 1.0f;
                if (enabled[(std::size_t) b])
                {
                    const auto m = external ? std::max (std::abs (sl0[b]), std::abs (sr0[b])) : std::max (std::abs (bl[b]), std::abs (br[b]));
                    const auto x = std::max (-120.0, 20.0 * std::log10 ((double) m + 1e-12));
                    auto& e = env[(std::size_t) b];
                    e = x > e ? aA[(std::size_t) b] * e + (1.0 - aA[(std::size_t) b]) * x
                              : aR[(std::size_t) b] * e + (1.0 - aR[(std::size_t) b]) * x;
                    const auto gdb = bandCurve (p, b, e);
                    if (std::abs (gdb) > std::abs (extreme[(std::size_t) b]))
                        extreme[(std::size_t) b] = gdb;
                    g = (float) dbToGain (gdb + level[(std::size_t) b]);
                }
                gain[(std::size_t) b][(std::size_t) i] = gainDelay[(std::size_t) b].process (g);
                float sl = bl[b], sr2 = br[b];
                if (minimum) // round 144: the band passes the all-passes of the crossovers above it
                    for (int j = b + 1; j < nb - 1; ++j)
                    {
                        sl = comp[(std::size_t) b][(std::size_t) j].allpass (sl, 0);
                        sr2 = comp[(std::size_t) b][(std::size_t) j].allpass (sr2, 1);
                    }
                bandDelay[(std::size_t) b].process (sl, sr2);
                band[(std::size_t) b][0][(std::size_t) i] = sl;
                band[(std::size_t) b][1][(std::size_t) i] = sr2;
            }
        }

        // round 147: Dynamic phase's measured engine — the (look-ahead delayed) input through each working band's
        // minimum-phase filter in cascade (core/MinPhaseBand.h); soloed bands are still listened to through the split
        if (md == Phase::dynamic && measuredDynamic (p) && ! anySolo)
        {
            if (f != dynF || slope != dynSlope || nb != dynNb || osFactor != dynOs || msPan != dynMs)
            {
                // a new crossover layout: every filter is designed again (their states are kept: no click)
                dynF = f;
                dynSlope = slope;
                dynNb = nb;
                dynOs = osFactor;
                dynMs = msPan;
                for (auto& g2 : dynGain)
                    g2.fill (-1.0);
            }
            std::array<bool, bands> works {};
            for (int b = 0; b < nb; ++b)
            {
                const auto i0 = (std::size_t) param (b, 0);
                const bool w = enabled[(std::size_t) b]
                               && (std::abs (rangeDb (p[i0 + fRange])) >= 0.01 || level[(std::size_t) b] != 0.0
                                   || panL[(std::size_t) b] != 1.0f || panR[(std::size_t) b] != 1.0f);
                if (w && ! dynWorks[(std::size_t) b])
                {
                    for (auto& fl : dynBand[(std::size_t) b])
                        fl.reset();
                    dynGain[(std::size_t) b].fill (-1.0);
                }
                works[(std::size_t) b] = dynWorks[(std::size_t) b] = w;
            }
            for (int b = nb; b < bands; ++b)
                dynWorks[(std::size_t) b] = false;
            const auto order = slope <= 12 ? 1 : (slope <= 24 ? 2 : 4);
            const auto run = [&] (int len, double rate, int every, const float* d0, const float* d1, auto&& gainAt, float* o0, float* o1) noexcept {
                std::array<double, bands> wl {}, wh {};
                for (int b = 0; b < nb; ++b)
                {
                    wl[(std::size_t) b] = b > 0 ? minphase::warp (std::min (f[(std::size_t) (b - 1)], sr * 0.45), rate) : 0.0;
                    wh[(std::size_t) b] = b < nb - 1 ? minphase::warp (std::min (f[(std::size_t) b], sr * 0.45), rate) : 0.0;
                }
                for (int i = 0; i < len; ++i)
                {
                    if (i % every == 0)
                        for (int b = 0; b < nb; ++b)
                        {
                            if (! works[(std::size_t) b])
                                continue;
                            const auto g = (double) gainAt (b, i);
                            const double gc[2] { g * panL[(std::size_t) b], g * panR[(std::size_t) b] };
                            for (int c = 0; c < 2; ++c)
                                if (std::abs (gc[c] - dynGain[(std::size_t) b][(std::size_t) c]) > 1e-7)
                                {
                                    dynGain[(std::size_t) b][(std::size_t) c] = gc[c];
                                    dynBand[(std::size_t) b][(std::size_t) c].design (wl[(std::size_t) b], wh[(std::size_t) b], order, std::max (gc[c], 1e-6));
                                }
                        }
                    const double xl = d0[i], xr = d1[i];
                    // the pan's two gains act on left / right, or on mid / side (round 144's pan mode)
                    double a = msPan ? 0.5 * (xl + xr) : xl, c = msPan ? 0.5 * (xl - xr) : xr;
                    for (int b = 0; b < nb; ++b)
                        if (works[(std::size_t) b])
                        {
                            a = dynBand[(std::size_t) b][0].process (a);
                            c = dynBand[(std::size_t) b][1].process (c);
                        }
                    const auto yl = msPan ? a + c : a, yr = msPan ? a - c : c;
                    o0[i] = (float) ((xl + (yl - xl) * mx) * outGain);
                    o1[i] = (float) ((xr + (yr - xr) * mx) * outGain);
                }
            };
            if (osFactor <= 1)
            {
                run (n, sr, 8, dry[0].data(), dry[1].data(), [&] (int b, int i) { return gain[(std::size_t) b][(std::size_t) i]; }, l, r);
                return;
            }
            // oversampled: the input and the gains interpolated (the same half-band filters), the filters designed at
            // the high rate, the result decimated back
            const int hi = n * osFactor;
            const auto upsample = [&] (mixsuite::Oversampler& os, const float* src, float* dst) noexcept {
                std::array<float, chunk> tmp {};
                std::copy (src, src + n, tmp.begin());
                int k = 0;
                os.process (tmp.data(), n, [&] (float v) { dst[k++] = v; return 0.0f; });
            };
            for (int c = 0; c < 2; ++c)
                upsample (upDry[(std::size_t) c], dry[(std::size_t) c].data(), hiDry[(std::size_t) c].data());
            for (int b = 0; b < nb; ++b)
                upsample (upGain[(std::size_t) b], gain[(std::size_t) b].data(), hiGain[(std::size_t) b].data());
            run (hi, sr * osFactor, 8 * osFactor, hiDry[0].data(), hiDry[1].data(),
                 [&] (int b, int k) { return hiGain[(std::size_t) b][(std::size_t) k]; }, hiDry[0].data(), hiDry[1].data());
            for (int c = 0; c < 2; ++c)
            {
                auto* out = c == 0 ? l : r;
                const auto* src = hiDry[(std::size_t) c].data();
                std::fill (out, out + n, 0.0f);
                int k = 0;
                down[(std::size_t) c].process (out, n, [&] (float) { return src[k++]; });
            }
            return;
        }

        // 2. the bands' gains applied and summed (at the oversampled rate when oversampling is on)
        const auto combine = [&] (float dl, float dr, auto&& bandAt, auto&& gainAt, float& outL, float& outR) noexcept {
            float yl = anySolo || minimum ? 0.0f : dl, yr = anySolo || minimum ? 0.0f : dr;
            for (int b = 0; b < nb; ++b)
            {
                const auto g = gainAt (b);
                float sl, sr2;
                bandAt (b, sl, sr2);
                const auto gl = g * panL[(std::size_t) b], gr = g * panR[(std::size_t) b];
                if (msPan) // round 144: the pan's two gains on the band's mid and side
                {
                    const auto mid = 0.5f * (sl + sr2), side = 0.5f * (sl - sr2);
                    if (anySolo && ! soloed[(std::size_t) b])
                        continue;
                    if (anySolo || minimum)
                    {
                        yl += gl * mid + gr * side;
                        yr += gl * mid - gr * side;
                    }
                    else if (gl != 1.0f || gr != 1.0f)
                    {
                        yl += (gl - 1.0f) * mid + (gr - 1.0f) * side;
                        yr += (gl - 1.0f) * mid - (gr - 1.0f) * side;
                    }
                    continue;
                }
                if (anySolo)
                {
                    if (soloed[(std::size_t) b])
                    {
                        yl += gl * sl;
                        yr += gr * sr2;
                    }
                }
                else if (minimum)
                {
                    yl += gl * sl;
                    yr += gr * sr2;
                }
                else if (gl != 1.0f || gr != 1.0f)
                {
                    yl += (gl - 1.0f) * sl;
                    yr += (gr - 1.0f) * sr2;
                }
            }
            outL = (dl + (yl - dl) * mx) * outGain;
            outR = (dr + (yr - dr) * mx) * outGain;
        };

        if (osFactor <= 1)
        {
            for (int i = 0; i < n; ++i)
                combine (dry[0][(std::size_t) i], dry[1][(std::size_t) i],
                         [&] (int b, float& a, float& c) { a = band[(std::size_t) b][0][(std::size_t) i]; c = band[(std::size_t) b][1][(std::size_t) i]; },
                         [&] (int b) { return gain[(std::size_t) b][(std::size_t) i]; }, l[i], r[i]);
            return;
        }
        // round 144: oversampling — every stream is interpolated by the same half-band filters (mixsuite::Oversampler:
        // only its upsampling half is used here), the gains are applied at the high rate (no aliasing from fast gain
        // changes), and one more Oversampler's decimating half brings the sum back
        const int hi = n * osFactor;
        const auto upsample = [&] (mixsuite::Oversampler& os, const float* src, float* dst) noexcept {
            std::array<float, chunk> tmp {};
            std::copy (src, src + n, tmp.begin());
            int k = 0;
            os.process (tmp.data(), n, [&] (float s) { dst[k++] = s; return 0.0f; });
        };
        for (int c = 0; c < 2; ++c)
            upsample (upDry[(std::size_t) c], dry[(std::size_t) c].data(), hiDry[(std::size_t) c].data());
        for (int b = 0; b < nb; ++b)
        {
            for (int c = 0; c < 2; ++c)
                upsample (upBand[(std::size_t) b][(std::size_t) c], band[(std::size_t) b][(std::size_t) c].data(), hiBand[(std::size_t) b][(std::size_t) c].data());
            upsample (upGain[(std::size_t) b], gain[(std::size_t) b].data(), hiGain[(std::size_t) b].data());
        }
        for (int k = 0; k < hi; ++k)
            combine (hiDry[0][(std::size_t) k], hiDry[1][(std::size_t) k],
                     [&] (int b, float& a, float& c) { a = hiBand[(std::size_t) b][0][(std::size_t) k]; c = hiBand[(std::size_t) b][1][(std::size_t) k]; },
                     [&] (int b) { return hiGain[(std::size_t) b][(std::size_t) k]; }, hiDry[0][(std::size_t) k], hiDry[1][(std::size_t) k]);
        for (int c = 0; c < 2; ++c)
        {
            auto* out = c == 0 ? l : r;
            const auto* src = hiDry[(std::size_t) c].data();
            std::fill (out, out + n, 0.0f);
            int k = 0;
            down[(std::size_t) c].process (out, n, [&] (float) { return src[k++]; });
        }
    }

    // the FIR of each band below the top one: the magnitude of the band in the IIR tree (the same crossovers), zero phase
    void designKernels (int nb) noexcept
    {
        kernelsDirty = false;
        if (linSize <= 0)
            return;
        const auto bins = linSize / 2 + 1;
        for (int k = 0; k < nb - 1; ++k)
        {
            auto& t = linTable[(std::size_t) k];
            for (int i = 0; i < bins; ++i)
            {
                const auto w = pi * (double) i / (double) (bins - 1);
                double m = 1.0;
                for (int j = 0; j < k; ++j)
                    m *= std::abs (split[(std::size_t) j].response (true, w));
                m *= std::abs (split[(std::size_t) k].response (false, w));
                t[(std::size_t) i] = (float) m;
            }
            lin[(std::size_t) k].setKernel (t);
        }
    }

    void resetOversamplers() noexcept
    {
        for (auto& o : upDry) o.reset();
        for (auto& row : upBand)
            for (auto& o : row) o.reset();
        for (auto& o : upGain) o.reset();
        for (auto& o : down) o.reset();
    }

    void configure (const EffectParams& p) noexcept
    {
        // round 144: the processing mode and the oversampling (at most 4x, and only up to 400 kHz)
        const int m = linSize > 0 ? (int) phaseOf (p[processingMode]) : (int) Phase::dynamic;
        int factor = oversamplingOf (p[oversampling]);
        while (factor > 1 && sr * factor > 400000.0)
            factor /= 2;
        const auto la = lookaheadMs (p[lookahead]);
        if (la == laMs && m == mode && factor == osFactor)
            return;
        if (m != mode)
        {
            mode = m;
            kernelsDirty = true;
            for (auto& c : lin)
                if (c.n > 0)
                    c.clear();
            linDry.reset();
            linSc.reset();
        }
        if (factor != osFactor)
        {
            osFactor = factor;
            const auto set = [&] (mixsuite::Oversampler& o) { o.setFactor (std::max (2, factor)); o.reset(); };
            for (auto& o : upDry) set (o);
            for (auto& row : upBand)
                for (auto& o : row) set (o);
            for (auto& o : upGain) set (o);
            for (auto& o : down) set (o);
        }
        if (la != laMs)
        {
            laMs = la;
            const auto s = (int) std::lround (la * 0.001 * sr);
            laSamples = s;
            delay.setLength (s);
            for (auto& d : bandDelay)
                d.setLength (s);
        }
        latency.store (laSamples + ((Phase) mode == Phase::linear ? linearLatency (sr) : 0)
                           + (osFactor > 1 ? mixsuite::Oversampler::latency() : 0),
                       std::memory_order_relaxed);
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
    std::array<Split, bands - 1> split, scSplit;
    std::array<std::array<Split, bands - 1>, bands - 1> comp; // minimum phase: band k's copy of split j's all-pass
    std::array<double, bands - 1> lastF {};
    const float* scL = nullptr;
    const float* scR = nullptr;
    int lastSlope = 0;
    bool designed = false, kernelsDirty = true;
    int mode = -1, osFactor = 0, linSize = 0, lastNb = 0;
    std::array<peqfl::FftConvolver, bands - 1> lin;
    std::array<std::vector<float>, bands - 1> linTable;
    LongDelay linDry, linSc;
    StereoDelay delay;
    std::array<StereoDelay, bands> bandDelay;
    std::array<MonoDelay, bands> gainDelay;
    int laSamples = 0;
    std::array<double, bands> env {}, extreme {};
    std::array<float, bands> gains {};
    // one chunk of the base-rate streams, and the same at the oversampled rate
    std::array<std::array<float, chunk>, 2> dry {};
    std::array<std::array<std::array<float, chunk>, 2>, bands> band {};
    std::array<std::array<float, chunk>, bands> gain {};
    std::array<std::array<float, chunk * maxFactor>, 2> hiDry {};
    std::array<std::array<std::array<float, chunk * maxFactor>, 2>, bands> hiBand {};
    std::array<std::array<float, chunk * maxFactor>, bands> hiGain {};
    std::array<mixsuite::Oversampler, 2> upDry, down;
    std::array<std::array<mixsuite::Oversampler, 2>, bands> upBand;
    std::array<mixsuite::Oversampler, bands> upGain;
    // round 147: the measured Dynamic phase — each band's two filters (left / right or mid / side), their last gains
    std::array<std::array<minphase::Band, 2>, bands> dynBand {};
    std::array<std::array<double, 2>, bands> dynGain {};
    std::array<bool, bands> dynWorks {};
    std::array<double, bands - 1> dynF {};
    int dynSlope = 0, dynNb = 0, dynOs = 0;
    bool dynMs = false;
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
double scGainDb (float v) noexcept { return lin (v, -36.0, 36.0); }
double mixOf (float v) noexcept { return lin (v, -1.0, 1.0); }
double panOf (float v) noexcept { return lin (v, -1.0, 1.0); }

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
    const auto g = gateCurve (p, inputDb);
    if (on (p[ducking]) && styleOf (p[style]) != Style::upward) // ducking: the gate turned upside down
        return -rangeDb (p[range]) - g;
    return g;
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
        osFactor = 0;
        configure (lastParams);
        reset();
    }

    void reset() noexcept override
    {
        delay.reset();
        gainDelay.reset();
        hp.reset();
        lp.reset();
        for (auto& o : os) o.reset();
        osGain.reset();
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

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (n > maxBlock) // the oversampled path works on blocks of at most maxBlock samples
        {
            const auto* sl = scL;
            const auto* sr2 = scR;
            for (int i = 0; i < n; i += maxBlock)
            {
                if (sl != nullptr && sr2 != nullptr) { scL = sl + i; scR = sr2 + i; }
                processBlock (l + i, r + i, std::min (maxBlock, n - i), p, bpm);
            }
            scL = scR = nullptr;
            return;
        }
        processBlock (l, r, n, p, bpm);
    }

private:
    void processBlock (float* l, float* r, int n, const EffectParams& p, double) noexcept
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
        // round 128: input level, the expert side-chain gain / mix per detector channel (mid / side in M/S mode),
        // ducking, wet / dry pan
        const auto inG = (float) dbToGain (lin (p[input], -24.0, 24.0));
        const bool msMode = on (p[channelMode]);
        const auto gL = (float) dbToGain (scGainDb (p[scGainL])), gR = (float) dbToGain (scGainDb (p[scGainR]));
        const auto mL = (float) mixOf (p[scMixL]), mR = (float) mixOf (p[scMixR]);
        const bool expertSc = msMode || gL != 1.0f || gR != 1.0f || mL != 0.0f || mR != 0.0f;
        const bool duck = on (p[ducking]) && st != Style::upward;
        const auto duckFloor = dbToGain (-rangeDb (p[range]));
        const auto balance = [] (float pan, float& a, float& b) {
            if (pan > 0.0f) a = 1.0f - pan;
            else if (pan < 0.0f) b = 1.0f + pan;
        };
        float wetA = 1.0f, wetB = 1.0f, dryA = 1.0f, dryB = 1.0f;
        balance ((float) panOf (p[wetPan]), wetA, wetB);
        balance ((float) panOf (p[dryPan]), dryA, dryB);
        const bool panned = wetA != 1.0f || wetB != 1.0f || dryA != 1.0f || dryB != 1.0f;
        if (inG != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= inG;
                r[i] *= inG;
            }
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
        const bool over = osFactor > 1;

        for (int i = 0; i < n; ++i)
        {
            float sa = external ? scL[i] : l[i], sb = external ? scR[i] : r[i];
            if (expertSc)
            {
                float a0 = sa, b0 = sb;
                if (msMode)
                {
                    a0 = 0.5f * (sa + sb);
                    b0 = 0.5f * (sa - sb);
                }
                // each detector channel: its own input at mix 0, moving to the first (−1) or second (+1) channel
                const auto pick = [] (float own, float first, float second, float m) {
                    return m < 0.0f ? own + (-m) * (first - own) : own + m * (second - own);
                };
                sa = gL * pick (a0, a0, b0, mL);
                sb = gR * pick (b0, a0, b0, mR);
            }
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
            auto target = dbToGain (gateCurve (p, gainToDb (level)));
            if (duck) // ducking: an open gate is the deepest cut
                target = duckFloor / std::max (1e-9, target);
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
            if (over) // round 135: the gain is upsampled with the audio and applied at the high rate (below)
            {
                gains[(std::size_t) i] = g;
                l[i] = a;
                r[i] = b;
                continue;
            }
            if (panned)
            {
                l[i] = (a * g * wetA + a * dryGain * dryA) * outGain;
                r[i] = (b * g * wetB + b * dryGain * dryB) * outGain;
            }
            else
            {
                l[i] = (a * g + a * dryGain) * outGain;
                r[i] = (b * g + b * dryGain) * outGain;
            }
        }
        if (over)
        {
            const auto m = n * osFactor;
            if (listen) // the side chain heard through the same path (the latency stays)
                for (int c = 0; c < 2; ++c)
                    os[(std::size_t) c].process (c == 0 ? l : r, n, [] (float x) noexcept { return x; });
            else
            {
                int k = 0;
                osGain.process (gains.data(), n, [&] (float x) noexcept { upGain[(std::size_t) std::min (k++, m - 1)] = x; return 0.0f; });
                for (int c = 0; c < 2; ++c)
                {
                    const auto wet = (c == 0 ? wetA : wetB) * outGain, dryK = (c == 0 ? dryA : dryB) * dryGain * outGain;
                    int j = 0;
                    os[(std::size_t) c].process (c == 0 ? l : r, n, [&] (float x) noexcept {
                        return x * upGain[(std::size_t) std::min (j++, m - 1)] * wet + x * dryK;
                    });
                }
            }
        }
        meters.set (0, (float) std::clamp (maxGr / 30.0, 0.0, 1.0));
        meters.set (2, inPeak);
        meters.set (3, blockPeak (l, r, n));
        scL = scR = nullptr;
    }

    // the look-ahead switch sets the latency (10 ms), the knob only how much of it the detector leads; the
    // oversampler adds its own (round 135)
    void configure (const EffectParams& p) noexcept
    {
        const int want = on (p[lookaheadOn]) ? 1 : 0;
        const int factor = crest::oversamplingOf (p[oversampling]);
        if (want == laOn && factor == osFactor)
            return;
        if (factor != osFactor)
        {
            osFactor = factor;
            for (auto& o : os) { o.setFactor (std::max (2, factor)); o.reset(); }
            osGain.setFactor (std::max (2, factor));
            osGain.reset();
        }
        laOn = want;
        laSamples = want ? (int) std::lround (0.010 * sr) : 0;
        delay.setLength (laSamples);
        latency.store (laSamples + (osFactor > 1 ? mixsuite::Oversampler::latency() : 0), std::memory_order_relaxed);
    }

    static constexpr int maxBlock = 1024, maxFactor = 8;
    int osFactor = 0;
    std::array<mixsuite::Oversampler, 2> os;
    mixsuite::Oversampler osGain;
    std::array<float, maxBlock> gains {};
    std::array<float, maxBlock * maxFactor> upGain {};

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
                                     { "Oversampling", 0.0f }, { "Auto gain", 1.0f }, { "Mix", 1.0f }, { "Input", 0.5f }, { "Output", 0.5f },
                                     { "Dry", 0.0f }, { "Auto threshold", 0.0f }, { "Link mode", 0.0f }, { "Meter scale", 0.75f },
                                     { "Input meter", 1.0f },
                                     { "Wet pan", 0.5f }, { "Dry pan", 0.5f }, // round 135
                                     // round 137: the side-chain trigger (off), 1/4 note, rate 100 %, length 10 %, no channel
                                     { "Trigger", 0.0f }, { "Trigger sync", 0.5f }, { "Trigger rate", 0.5f }, { "Trigger length", 0.1f },
                                     { "Trigger channel", 0.0f } } };
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
            { "B4 threshold", "B4 ratio", "B4 range", "B4 expand", "B4 knee", "B4 attack", "B4 release", "B4 level", "B4 on", "B4 solo" },
            { "B5 threshold", "B5 ratio", "B5 range", "B5 expand", "B5 knee", "B5 attack", "B5 release", "B5 level", "B5 on", "B5 solo" },
            { "B6 threshold", "B6 ratio", "B6 range", "B6 expand", "B6 knee", "B6 attack", "B6 release", "B6 level", "B6 on", "B6 solo" }
        };
        // defaults: −18 dB, 4.5 : 1, range −12 dB (downward), knee 24 dB, attack 20 %, release 20 %, 0 dB, on
        const float defaults[prism::fieldsPerBand] { prism::thresholdValue (-18.0), 0.6f, 0.3f, 0.0f, 0.5f, 0.2f, 0.2f, 0.5f, 1.0f, 0.0f };
        for (int b = 0; b < prism::legacyBands; ++b)
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
        for (const auto* n : { "B1 lookahead", "B2 lookahead", "B3 lookahead", "B4 lookahead" })
            i.params.push_back ({ n, 1.0f }); // 20 ms: each band leads by the whole global look-ahead (as before)
        // round 135: bands 5 and 6, crossovers 4 and 5, their look-aheads, the band count (4), each band's output pan,
        // the external side chain
        for (int b = prism::legacyBands; b < prism::bands; ++b)
            for (int f = 0; f < prism::fieldsPerBand; ++f)
                i.params.push_back ({ names[b][f], defaults[f] });
        i.params.push_back ({ "Crossover 4", prism::crossoverValue (10000.0) });
        i.params.push_back ({ "Crossover 5", prism::crossoverValue (14000.0) });
        i.params.push_back ({ "B5 lookahead", 1.0f });
        i.params.push_back ({ "B6 lookahead", 1.0f });
        i.params.push_back ({ "Bands", prism::bandCountValue (4) });
        for (const auto* n : { "B1 pan", "B2 pan", "B3 pan", "B4 pan", "B5 pan", "B6 pan" })
            i.params.push_back ({ n, 0.5f });
        i.params.push_back ({ "External SC", 0.0f });
        // round 144: the processing mode (dynamic phase = the old engine) and the oversampling (off)
        i.params.push_back ({ "Processing", 0.5f });
        i.params.push_back ({ "Oversampling", 0.0f });
        i.params.push_back ({ "Pan mode", 0.0f });
        // round 147: the measured Dynamic phase engine for new instances; projects saved before it load the old one
        // (engine/ProjectFile.cpp sets 0 when the saved parameter list ends before this index)
        i.params.push_back ({ "Dynamic engine", 1.0f });
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
                                     { "Output", 0.5f },
                                     { "Channel mode", 0.0f }, { "SC gain L", 0.5f }, { "SC mix L", 0.5f }, { "SC gain R", 0.5f },
                                     { "SC mix R", 0.5f }, { "Wet pan", 0.5f }, { "Dry pan", 0.5f }, { "Ducking", 0.0f },
                                     { "Input", 0.5f },
                                     { "Oversampling", 0.0f } } }; // round 135
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
            case dryGain: return v <= 0.0f ? "-inf dB" : db (dryDb (v));
            case linkMode:
            {
                static const char* names[linkModes] { "Stereo", "Mid only", "Side only", "Mid > side", "Side > mid" };
                return names[(int) linkModeOf (v)];
            }
            case meterScale: return fmt ("%.0f dB", meterRangeDb (v));
            case inputMeter: return on (v) ? "shown" : "hidden";
            case wetPan: case dryPan:
            {
                const auto x = panOf (v);
                return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
            }
            case trigger:
            {
                static const char* names[3] { "Off", "Host sync", "MIDI" };
                return names[(int) triggerOf (v)];
            }
            case triggerSync:
            {
                static const char* names[5] { "1/1", "1/2", "1/4", "1/8", "1/16" };
                return names[pick (v, 5)];
            }
            case triggerRate: return fmt ("%.0f%%", 100.0 * triggerRateOf (v));
            case triggerLength: return fmt ("%.0f%%", 100.0 * triggerLengthOf (v));
            case triggerChannel:
                return triggerChannelOf (v) < 0 ? std::string ("Off") : fmt ("Channel %.0f", (double) (triggerChannelOf (v) + 1));
            default: return on (v) ? "on" : "off";
        }
    }
    if (kind == EffectKind::prismMultiband)
    {
        using namespace prism;
        const bool bandField = param < legacyBands * fieldsPerBand || (param >= band5 && param < band5 + 2 * fieldsPerBand);
        if (bandField)
        {
            switch ((param < band5 ? param : param - band5) % fieldsPerBand)
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
            case xover1: case xover2: case xover3: case xover4: case xover5: return hzText (crossoverHz (v));
            case bandCountParam: return std::to_string (1 + pick (v, bands));
            case scExternal: return on (v) ? "external" : "internal";
            case processingMode:
            {
                static const char* const names[3] { "Linear Phase", "Dynamic Phase", "Minimum Phase" };
                return names[(int) phaseOf (v)];
            }
            case oversampling: return oversamplingOf (v) == 1 ? "Off" : std::to_string (oversamplingOf (v)) + "x";
            case panMode: return on (v) ? "mid / side" : "left / right";
            case dynamicEngine: return on (v) ? "measured" : "classic";
            case slope: return std::to_string (slopeDbOct (v)) + " dB/oct";
            case lookahead: return v <= 0.0f ? "off" : msText (lookaheadMs (v));
            case mix: return fmt ("%.0f%%", v * 100.0);
            case input: case output: return db (trimDb (v));
            default:
                if (param >= bandPan0 && param < bandPan0 + bands)
                {
                    const auto x = panOf (v);
                    return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
                }
                return msText (lookaheadMs (v)); // the bands' own look-ahead
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
            case output: case input: return db (lin (v, -24.0, 24.0));
            case oversampling: return crest::oversamplingOf (v) == 1 ? "off" : std::to_string (crest::oversamplingOf (v)) + "x";
            case channelMode: return on (v) ? "Mid/Side" : "Left/Right";
            case scGainL: case scGainR: return db (scGainDb (v));
            case scMixL: case scMixR: return fmt ("%+.2f", mixOf (v));
            case wetPan: case dryPan:
            {
                const auto x = panOf (v);
                return std::abs (x) < 0.005 ? std::string ("C") : fmt (x < 0 ? "%.0f%% L" : "%.0f%% R", std::abs (x) * 100.0);
            }
            default: return on (v) ? "on" : "off";
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::dyn
