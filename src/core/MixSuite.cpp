#include "core/MixSuite.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/EmberClassic.h"
#include "core/LrCrossover.h"
#include "core/VoiceMatch.h"

// REA_rodada96. Every formula below is textbook material, cited where it matters:
//  - biquads: R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients";
//  - half-band FIR oversampling with a Kaiser window: Oppenheim & Schafer, "Discrete-Time Signal Processing", 7.6
//    and 4.7 (multirate), Kaiser's design formula for beta;
//  - compressor / gate gain computer with a soft knee and the "smooth branching" peak detector in the log domain:
//    D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
//    JAES 60(6), 2012;
//  - the electro-optical cell (an electroluminescent panel lighting a photoresistor in a feedback leveler): the
//    published behaviour of such cells — about 10 ms attack, a release in two stages (roughly half the reduction
//    within ~60 ms, the rest over 0.5 to several seconds, longer after long or heavy reduction) — as described in
//    the public service literature and circuit analyses of opto levelers; the model (a fast and a slow conductance
//    and a memory that lengthens the slow release) is our own;
//  - waveshaping and its harmonics: U. Zölzer (ed.), "DAFX: Digital Audio Effects", 2nd ed., ch. 4 (nonlinear
//    processing: tube / tape / distortion curves, exciters and enhancers), and J. Pakarinen, D. T. Yeh, "A Review of
//    Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers", CMJ 33(2), 2009 (asymmetric curves → even
//    harmonics).

namespace lynx::mixsuite
{
namespace
{
constexpr double pi = 3.141592653589793;

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double logMap (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, (double) std::clamp (v, 0.0f, 1.0f)); }
inline double lin (float v, double lo, double hi) noexcept { return lo + (hi - lo) * (double) std::clamp (v, 0.0f, 1.0f); }
inline bool on (float v) noexcept { return v >= 0.5f; }
// one-pole smoothing coefficient for a time constant in seconds
inline double coef (double seconds, double sr) noexcept { return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * sr)); }

// |H(e^jw)| of a biquad
double biquadMag (const Biquad& b, double w)
{
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = b.b0 + b.b1 * c1 + b.b2 * c2, ni = -(b.b1 * s1 + b.b2 * s2);
    const double dr = 1.0 + b.a1 * c1 + b.a2 * c2, di = -(b.a1 * s1 + b.a2 * s2);
    return std::sqrt ((nr * nr + ni * ni) / std::max (dr * dr + di * di, 1e-300));
}

// first-order high-pass (bilinear), stereo
struct OnePoleHp
{
    double b0 = 1, b1 = 0, a1 = 0;
    double x1[2] {}, y1[2] {};
    void set (double sr, double hz) noexcept
    {
        const auto k = std::tan (pi * std::min (hz, sr * 0.45) / sr);
        b0 = 1.0 / (1.0 + k);
        b1 = -b0;
        a1 = (k - 1.0) / (k + 1.0);
    }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0; }
    float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + b1 * x1[ch] - a1 * y1[ch];
        x1[ch] = x;
        y1[ch] = y;
        return (float) y;
    }
    double mag (double w) const
    {
        const double nr = b0 + b1 * std::cos (w), ni = -b1 * std::sin (w);
        const double dr = 1.0 + a1 * std::cos (w), di = -a1 * std::sin (w);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// Stereo delay line of fixed capacity (no allocation).
template <int N>
struct Delay2
{
    std::array<float, N> l {}, r {};
    int pos = 0, len = 0;
    void setLength (int n) noexcept { len = std::clamp (n, 0, N - 1); }
    void reset() noexcept { l.fill (0.0f); r.fill (0.0f); pos = 0; }
    void process (float& a, float& b) noexcept
    {
        if (len == 0)
            return;
        l[(std::size_t) pos] = a;
        r[(std::size_t) pos] = b;
        auto rd = pos - len;
        if (rd < 0)
            rd += N;
        a = l[(std::size_t) rd];
        b = r[(std::size_t) rd];
        if (++pos == N)
            pos = 0;
    }
};

struct Meters
{
    std::atomic<float> gr0 { 0.0f }, gr1 { 0.0f }, in { 0.0f }, out { 0.0f };
    float read (int k) const noexcept
    {
        switch (k)
        {
            case 0: return gr0.load (std::memory_order_relaxed);
            case 1: return gr1.load (std::memory_order_relaxed);
            case 2: return in.load (std::memory_order_relaxed);
            case 3: return out.load (std::memory_order_relaxed);
            default: return 0.0f;
        }
    }
    static float peak (const float* l, const float* r, int n) noexcept
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = std::max (p, std::max (std::abs (l[i]), std::abs (r[i])));
        return std::min (p, 1.0f);
    }
    void set (float grDb0, float grDb1, float inPeak, float outPeak) noexcept
    {
        gr0.store (std::clamp (grDb0 / 30.0f, 0.0f, 1.0f), std::memory_order_relaxed);
        gr1.store (std::clamp (grDb1 / 30.0f, 0.0f, 1.0f), std::memory_order_relaxed);
        in.store (inPeak, std::memory_order_relaxed);
        out.store (outPeak, std::memory_order_relaxed);
    }
};

// ---- oversampler design ----
constexpr int stageTaps[Oversampler::maxStages] = { 95, 23, 15 }; // N = 4k + 3: the centre index c = (N − 1) / 2 is odd
constexpr double stageBeta[Oversampler::maxStages] = { 10.0, 9.0, 8.5 };

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

// latency of a factor's path in units of the highest rate, before the pad
int rawUnits (int stages)
{
    const int f = 1 << stages;
    int u = 0;
    for (int s = 0; s < stages; ++s)
        u += ((stageTaps[s] - 1) / 2) * f / (1 << s);
    return u;
}

int pathLatency (int stages)
{
    const int f = 1 << stages;
    const int u = rawUnits (stages);
    return (u + (f - u % f) % f) / f;
}
} // namespace

// ================================================================== Oversampler

Oversampler::Oversampler()
{
    for (int s = 0; s < maxStages; ++s)
    {
        auto& stage = st[(std::size_t) s];
        const int n = stageTaps[s];
        const int c = (n - 1) / 2;
        const auto i0b = besselI0 (stageBeta[s]);
        double sum = 0.0;
        std::array<double, 64> h {};

        for (int j = 0; 2 * j < n; ++j)
        {
            const int t = 2 * j - c; // odd offset from the centre
            const auto r = (double) (2 * j - c) / c;
            const auto w = besselI0 (stageBeta[s] * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(std::size_t) j] = std::sin (0.5 * pi * t) / (0.5 * pi * t) * w; // sinc(t/2), scaled below
            sum += h[(std::size_t) j];
        }

        stage.taps = (n + 1) / 2;
        for (int j = 0; j < stage.taps; ++j)
            stage.coef[(std::size_t) j] = (float) (h[(std::size_t) j] * 0.5 / sum); // the side taps sum to 0.5
        stage.centreDelay = (c - 1) / 2;
    }
    setFactor (4);
}

int Oversampler::latency() noexcept { return pathLatency (maxStages); }

void Oversampler::setFactor (int factor) noexcept
{
    const int s = factor >= 8 ? 3 : (factor >= 4 ? 2 : 1);
    if (s == stages && (hiPadLen != 0 || loPadLen != 0 || s == maxStages))
        return;
    stages = s;
    const int f = 1 << s;
    const int u = rawUnits (s);
    hiPadLen = (f - u % f) % f;
    loPadLen = latency() - pathLatency (s);
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& s : st)
    {
        s.upHist.fill (0.0f);
        s.evenHist.fill (0.0f);
        s.oddHist.fill (0.0f);
        s.upPos = s.downPos = 0;
    }
    hiPad.fill (0.0f);
    loPad.fill (0.0f);
    hiPadPos = loPadPos = 0;
}

void Oversampler::up (Stage& s, float x, float& a, float& b) noexcept
{
    const int L = s.taps;
    s.upPos = s.upPos == 0 ? L - 1 : s.upPos - 1;
    s.upHist[(std::size_t) s.upPos] = s.upHist[(std::size_t) (s.upPos + L)] = x;
    const float* hist = s.upHist.data() + s.upPos; // hist[j] = x[m − j]
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * hist[j];
    a = 2.0f * acc;              // y[2m]
    b = hist[s.centreDelay];     // y[2m + 1] = x[m − (c − 1) / 2]
}

float Oversampler::down (Stage& s, float a, float b) noexcept
{
    const int L = s.taps;
    s.downPos = s.downPos == 0 ? L - 1 : s.downPos - 1;
    s.evenHist[(std::size_t) s.downPos] = s.evenHist[(std::size_t) (s.downPos + L)] = a;
    s.oddHist[(std::size_t) s.downPos] = s.oddHist[(std::size_t) (s.downPos + L)] = b;
    const float* even = s.evenHist.data() + s.downPos;
    float acc = 0.0f;
    for (int j = 0; j < L; ++j)
        acc += s.coef[(std::size_t) j] * even[j];
    return acc + 0.5f * s.oddHist[(std::size_t) (s.downPos + s.centreDelay + 1)]; // v[2m − c]
}

float Oversampler::padIn (float x) noexcept
{
    if (hiPadLen == 0)
        return x;
    const auto y = hiPad[(std::size_t) hiPadPos];
    hiPad[(std::size_t) hiPadPos] = x;
    hiPadPos = (hiPadPos + 1) % hiPadLen;
    return y;
}

float Oversampler::padOut (float x) noexcept
{
    if (loPadLen == 0)
        return x;
    const auto y = loPad[(std::size_t) loPadPos];
    loPad[(std::size_t) loPadPos] = x;
    loPadPos = (loPadPos + 1) % loPadLen;
    return y;
}

// ================================================================== Ridge Strip

namespace ridge
{
double hpfHz (float v) noexcept { return v <= 0.0f ? 0.0 : logMap (v, 16.0, 350.0); }
double lpfHz (float v) noexcept { return v <= 0.0f ? 0.0 : logMap (v, 22000.0, 3000.0); }
double bandGainDb (float v, bool black) noexcept { return (2.0 * std::clamp (v, 0.0f, 1.0f) - 1.0) * (black ? 18.0 : 15.0); }
double lfHz (float v) noexcept { return logMap (v, 30.0, 450.0); }
double lmfHz (float v) noexcept { return logMap (v, 200.0, 2500.0); }
double hmfHz (float v) noexcept { return logMap (v, 600.0, 7000.0); }
double hfHz (float v) noexcept { return logMap (v, 1500.0, 16000.0); }
double bandQ (float v) noexcept { return logMap (v, 0.5, 2.5); }
double ratio (float v) noexcept { return v >= 0.999f ? 1e9 : 1.0 + 19.0 * std::pow ((double) std::clamp (v, 0.0f, 1.0f), 2.0); }
double compThresholdDb (float v) noexcept { return lin (v, -40.0, 0.0); }
double releaseSeconds (float v) noexcept { return logMap (v, 0.1, 4.0); }
double gateRangeDb (float v) noexcept { return lin (v, 0.0, 60.0); }
double gateThresholdDb (float v) noexcept { return lin (v, -80.0, 0.0); }
double trimDb (float v) noexcept { return lin (v, -20.0, 20.0); }
double eQ (float v) noexcept { return vmatch::echannel::qFromDial (1.0 - (double) std::clamp (v, 0.0f, 1.0f)); }
double eCompThresholdDb (float v) noexcept { return lin (v, -20.0, 10.0); }
double eGateRangeDb (float v) noexcept { return lin (v, 0.0, 40.0); }
double eGateThresholdDb (float v) noexcept { return lin (v, -30.0, 10.0); }
bool eVoicing (const EffectParams& p) noexcept { return on (p[voicing]); }

namespace
{
constexpr double kneeDb = 6.0;

// Giannoulis et al. 2012, eq. (4): soft-knee gain computer, output level for input level x (dB)
double gainComputer (double x, double threshold, double r)
{
    const auto over = x - threshold;
    if (2.0 * over < -kneeDb)
        return x;
    if (2.0 * std::abs (over) <= kneeDb)
        return x + (1.0 / r - 1.0) * (over + kneeDb / 2.0) * (over + kneeDb / 2.0) / (2.0 * kneeDb);
    return threshold + over / r;
}

// the EQ section's filters (shared by the processor and responseDb)
struct EqDesign
{
    OnePoleHp hp1;
    Biquad hp2, lp, lf, lmf, hmf, hf;
    bool hpOn = false, lpOn = false;

    void design (const EffectParams& p, double sr)
    {
        const bool black = on (p[curve]);
        hpOn = p[hpf] > 0.0f;
        lpOn = p[lpf] > 0.0f;
        if (hpOn)
        {
            // 3rd-order Butterworth high-pass: a first-order section and a second-order one with Q = 1
            hp1.set (sr, hpfHz (p[hpf]));
            hp2.filter (sr, 1, hpfHz (p[hpf]), 1.0);
        }
        if (lpOn)
            lp.filter (sr, 0, std::min (lpfHz (p[lpf]), sr * 0.45), 0.70710678);

        const auto nyq = sr * 0.49;
        // shelves: "brown" curves are plain (S = 1, no overshoot); "black" ones have the resonant shelf with the small
        // dip / bump before the shelf that many console EQs show (S > 1)
        const auto shelfS = black ? 1.6 : 1.0;
        const auto lg = bandGainDb (p[lfGain], black), hg = bandGainDb (p[hfGain], black);
        if (on (p[lfBell]))
            lf.peaking (sr, lfHz (p[lfFreq]), black ? propQ (0.9, lg) : 0.9, lg);
        else
            lf.shelf (sr, lfHz (p[lfFreq]), lg, false, shelfS);
        if (on (p[hfBell]))
            hf.peaking (sr, std::min (hfHz (p[hfFreq]), nyq), black ? propQ (0.9, hg) : 0.9, hg);
        else
            hf.shelf (sr, std::min (hfHz (p[hfFreq]), nyq), hg, true, shelfS);

        const auto mg = bandGainDb (p[lmfGain], black), ng = bandGainDb (p[hmfGain], black);
        lmf.peaking (sr, lmfHz (p[lmfFreq]), black ? propQ (bandQ (p[lmfQ]), mg) : bandQ (p[lmfQ]), mg);
        hmf.peaking (sr, std::min (hmfHz (p[hmfFreq]), nyq), black ? propQ (bandQ (p[hmfQ]), ng) : bandQ (p[hmfQ]), ng);
    }

    // "black" bells: proportional Q — wide for small boosts / cuts, narrower as the gain grows
    static double propQ (double q, double gainDb) { return std::max (0.2, q * (0.5 + std::abs (gainDb) / 12.0)); }
};
} // namespace

double responseDb (const EffectParams& p, double sampleRate, double hz)
{
    EqDesign d;
    d.design (p, sampleRate);
    const auto w = 2.0 * pi * hz / sampleRate;
    double m = 1.0;
    if (d.hpOn)
        m *= d.hp1.mag (w) * biquadMag (d.hp2, w);
    if (d.lpOn)
        m *= biquadMag (d.lp, w);
    if (on (p[eqOn]))
        m *= biquadMag (d.lf, w) * biquadMag (d.lmf, w) * biquadMag (d.hmf, w) * biquadMag (d.hf, w);
    return gainToDb (m);
}

double compressorCurveDb (const EffectParams& p, double inputDb)
{
    return gainComputer (inputDb, compThresholdDb (p[compThreshold]), ratio (p[compRatio]));
}

namespace
{
class Strip final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        console.prepare (rate);
        reset();
    }

    void reset() noexcept override
    {
        console.reset();
        for (auto* d : { &audio, &side })
        {
            d->hp1.reset(); d->hp2.reset(); d->lp.reset(); d->lf.reset(); d->lmf.reset(); d->hmf.reset(); d->hf.reset();
        }
        compGr = 0.0;
        gateGr = 0.0;
        levelEnv = 0.0;
        gateOpen = false;
    }

    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (eVoicing (p))
        {
            const auto inPeak = Meters::peak (l, r, n);
            console.process (l, r, n, consoleSettings (p));
            meters.set (console.compReductionDb(), console.gateReductionDb(), inPeak, Meters::peak (l, r, n));
            return;
        }
        audio.design (p, sr);
        side.design (p, sr);
        const auto inPeak = Meters::peak (l, r, n);
        const auto inGain = (float) dbToGain (trimDb (p[input]));
        const auto outGain = (float) (dbToGain (trimDb (p[output])) * (on (p[phase]) ? -1.0 : 1.0));
        const bool eq = on (p[eqOn]), dyn = on (p[dynOn]), eqFirst = ! on (p[order]), fts = on (p[filtersToSidechain]);

        // compressor
        const auto thr = compThresholdDb (p[compThreshold]), rat = ratio (p[compRatio]);
        const auto compAttack = coef (on (p[compFastAttack]) ? 0.001 : 0.010, sr);
        const auto compRel = coef (releaseSeconds (p[ridge::compRelease]), sr);
        const auto cmix = (float) std::clamp (p[compMix], 0.0f, 1.0f);
        const bool compOn = p[compRatio] > 0.0005f;
        // gate / expander
        const auto range = gateRangeDb (p[gateRange]), gthr = gateThresholdDb (p[gateThreshold]);
        const bool gateOn = range > 0.05, expand = on (p[expander]);
        const auto gateAttack = coef (on (p[gateFastAttack]) ? 0.0001 : 0.0015, sr);
        const auto gateRel = coef (releaseSeconds (p[ridge::gateRelease]), sr);
        const auto levelRelease = coef (0.030, sr);
        float maxComp = 0.0f, maxGate = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * inGain, b = r[i] * inGain;
            if (! fts)
                filters (audio, a, b);
            if (eq && eqFirst)
                bands (audio, a, b);

            if (dyn && (compOn || gateOn))
            {
                float sa = a, sb = b;
                if (fts)
                    filters (side, sa, sb);
                const auto rect = (double) std::max (std::abs (sa), std::abs (sb));
                // level detector: instant attack, 30 ms release (keeps the ripple of low notes out of the gain)
                levelEnv = rect > levelEnv ? rect : levelRelease * levelEnv + (1.0 - levelRelease) * rect;
                const auto peak = levelEnv;

                // gate / expander first (as in console channel dynamics), then the compressor
                if (gateOn)
                {
                    const auto lvl = gainToDb (peak);
                    double target;
                    if (expand) // 1:2 downward expansion below the threshold, limited to the range
                        target = lvl < gthr ? std::min (range, (gthr - lvl)) : 0.0;
                    else
                    {
                        // hysteresis: opens at the threshold, closes 4 dB below it
                        if (lvl >= gthr)
                            gateOpen = true;
                        else if (lvl < gthr - 4.0)
                            gateOpen = false;
                        target = gateOpen ? 0.0 : range;
                    }
                    gateGr = target < gateGr ? gateAttack * gateGr + (1.0 - gateAttack) * target
                                             : gateRel * gateGr + (1.0 - gateRel) * target;
                }
                else
                    gateGr = 0.0;

                if (compOn)
                {
                    const auto x = gainToDb (peak) - gateGr;
                    const auto want = x - gainComputer (x, thr, rat);
                    compGr = want > compGr ? compAttack * compGr + (1.0 - compAttack) * want
                                           : compRel * compGr + (1.0 - compRel) * want;
                }
                else
                    compGr = 0.0;

                const auto g = (float) dbToGain (-gateGr);
                const auto c = (float) dbToGain (-compGr);
                const auto wet = g * (cmix * c + (1.0f - cmix));
                a *= wet;
                b *= wet;
                maxComp = std::max (maxComp, (float) compGr);
                maxGate = std::max (maxGate, (float) gateGr);
            }

            if (eq && ! eqFirst)
                bands (audio, a, b);
            l[i] = a * outGain;
            r[i] = b * outGain;
        }
        meters.set (maxComp, maxGate, inPeak, Meters::peak (l, r, n));
    }

private:
    // E voicing: Ridge's controls in the console's units (the shared ranges use Ridge's own laws)
    static vmatch::echannel::Settings consoleSettings (const EffectParams& p) noexcept
    {
        vmatch::echannel::Settings e;
        e.inputDb = trimDb (p[input]);
        e.outputDb = trimDb (p[output]);
        e.phase = on (p[phase]);
        e.hpHz = p[hpf] > 0.0f ? hpfHz (p[hpf]) : 0.0;
        e.lpKHz = p[lpf] > 0.0f ? lpfHz (p[lpf]) / 1000.0 : 0.0;
        e.lfGain = bandGainDb (p[lfGain], false);
        e.lfHz = lfHz (p[lfFreq]);
        e.lfBell = on (p[lfBell]);
        e.lmfGain = bandGainDb (p[lmfGain], false);
        e.lmfKHz = lmfHz (p[lmfFreq]) / 1000.0;
        e.lmfQ = eQ (p[lmfQ]);
        e.hmfGain = bandGainDb (p[hmfGain], false);
        e.hmfKHz = hmfHz (p[hmfFreq]) / 1000.0;
        e.hmfQ = eQ (p[hmfQ]);
        e.hfGain = bandGainDb (p[hfGain], false);
        e.hfKHz = hfHz (p[hfFreq]) / 1000.0;
        e.hfBell = on (p[hfBell]);
        e.eqOn = on (p[eqOn]);
        e.compRatio = p[compRatio] <= 0.0005f ? 1.0 : ratio (p[compRatio]);
        e.compThreshold = eCompThresholdDb (p[compThreshold]);
        e.compRelease = releaseSeconds (p[ridge::compRelease]);
        e.compFast = on (p[compFastAttack]);
        e.expRange = eGateRangeDb (p[gateRange]);
        e.expThreshold = eGateThresholdDb (p[gateThreshold]);
        e.expRelease = releaseSeconds (p[ridge::gateRelease]);
        e.expFast = on (p[gateFastAttack]);
        e.gate = ! on (p[expander]);
        e.dynOn = on (p[dynOn]);
        e.dynamicsAfterEq = ! on (p[order]);
        e.filtersBeforeDynamics = on (p[filterSplit]);
        return e;
    }

    vmatch::echannel::Channel console;

    static void filters (EqDesign& d, float& a, float& b) noexcept
    {
        if (d.hpOn)
        {
            a = d.hp2.process (d.hp1.process (a, 0), 0);
            b = d.hp2.process (d.hp1.process (b, 1), 1);
        }
        if (d.lpOn)
        {
            a = d.lp.process (a, 0);
            b = d.lp.process (b, 1);
        }
    }

    static void bands (EqDesign& d, float& a, float& b) noexcept
    {
        a = d.hf.process (d.hmf.process (d.lmf.process (d.lf.process (a, 0), 0), 0), 0);
        b = d.hf.process (d.hmf.process (d.lmf.process (d.lf.process (b, 1), 1), 1), 1);
    }

    double sr = 44100.0;
    EqDesign audio, side;
    double compGr = 0.0, gateGr = 0.0, levelEnv = 0.0;
    bool gateOpen = false;
    Meters meters;
};
} // namespace
} // namespace ridge

// ================================================================== Glow Leveler

namespace glow
{
double gainDb (float v) noexcept { return lin (v, 0.0, 30.0); }
double sidechainDb (float v) noexcept { return lin (v, -20.0, 40.0); }
bool classic (const EffectParams& p) noexcept { return on (p[voicing]); }
double trimDb (float v) noexcept { return lin (v, -18.0, 18.0); }

namespace
{
class Leveler final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        panelAttack = coef (0.001, sr);
        panelRelease = coef (0.030, sr);
        fastAttack = coef (0.010, sr);
        fastRelease = coef (0.050, sr);
        slowAttack = coef (0.400, sr);
        memoryUp = coef (2.0, sr);
        memoryDown = coef (8.0, sr);
        autoCoef = coef (0.5, sr);
        classicEngine.prepare (rate);
        reset();
    }

    void reset() noexcept override
    {
        emph.reset();
        classicEngine.reset();
        panel = cellFast = cellSlow = memory = autoGr = 0.0;
        lastL = lastR = 0.0f;
    }

    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto inPeak = Meters::peak (l, r, n);
        if (classic (p))
        {
            vmatch::opto::Settings c;
            c.peakReduction = 100.0 * std::clamp ((double) p[peakReduction], 0.0, 1.0);
            c.gain = 100.0 * std::clamp ((double) p[gain], 0.0, 1.0);
            c.hiFreq = 100.0 * std::clamp ((double) p[emphasis], 0.0, 1.0);
            c.limit = on (p[mode]);
            const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
            const auto trimGain = (float) dbToGain (trimDb (p[trim]));
            for (int off = 0; off < n; off += maxBlock)
            {
                const int m = std::min (maxBlock, n - off);
                std::copy (l + off, l + off + m, dryL.begin());
                std::copy (r + off, r + off + m, dryR.begin());
                classicEngine.process (l + off, r + off, m, c);
                for (int i = 0; i < m; ++i)
                {
                    l[off + i] = (l[off + i] * mx + dryL[(std::size_t) i] * (1.0f - mx)) * trimGain;
                    r[off + i] = (r[off + i] * mx + dryR[(std::size_t) i] * (1.0f - mx)) * trimGain;
                }
            }
            meters.set (classicEngine.reductionDb(), 0.0f, inPeak, Meters::peak (l, r, n));
            return;
        }
        const auto sc = p[peakReduction] <= 0.0f ? 0.0 : dbToGain (sidechainDb (p[peakReduction]));
        const auto gamma = on (p[mode]) ? 9.0 : 2.0; // asymptotic ratio 1 + gamma: about 3:1 (compress), 10:1 (limit)
        emph.shelf (sr, 2500.0, 10.0 * std::clamp ((double) p[emphasis], 0.0, 1.0), true, 0.7);
        const auto makeup = (float) dbToGain (gainDb (p[gain]));
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto trimGain = (float) dbToGain (trimDb (p[trim]));
        const auto autoUp = on (p[autoMakeup]);
        double maxGr = 0.0;

        for (int i = 0; i < n; ++i)
        {
            // feedback detection: the cell sees the leveler's own output (the previous sample)
            const auto s = std::max (std::abs (emph.process (lastL, 0)), std::abs (emph.process (lastR, 1)));
            panel = s > panel ? panelAttack * panel + (1.0 - panelAttack) * s : panelRelease * panel + (1.0 - panelRelease) * s;
            const auto light = std::pow (sc * panel, gamma);

            // the cell: a fast conductance (attack ~10 ms, release ~50 ms) and a slow one whose release lengthens
            // with the memory of past reduction (0.4 s fresh, up to 5 s after long, heavy leveling)
            cellFast = light > cellFast ? fastAttack * cellFast + (1.0 - fastAttack) * light
                                        : fastRelease * cellFast + (1.0 - fastRelease) * light;
            const auto slowRelease = coef (0.4 + 4.6 * memory, sr);
            cellSlow = light > cellSlow ? slowAttack * cellSlow + (1.0 - slowAttack) * light
                                        : slowRelease * cellSlow + (1.0 - slowRelease) * light;
            const auto busy = light > 0.1 ? 1.0 : 0.0;
            memory = busy > memory ? memoryUp * memory + (1.0 - memoryUp) * busy : memoryDown * memory + (1.0 - memoryDown) * busy;

            const auto conductance = 0.7 * cellFast + 0.3 * cellSlow;
            const auto g = (float) (1.0 / (1.0 + conductance));
            lastL = l[i] * g;
            lastR = r[i] * g;
            // auto make-up (round 129) [model]: gives back ~2/3 of the slow average reduction, so turning PEAK REDUCTION
            // up keeps the level roughly where it was (the reference's AUTO MAKEUP; its law is not measured)
            auto wet = makeup;
            if (autoUp)
            {
                const auto grDb = -gainToDb (g);
                autoGr = autoCoef * autoGr + (1.0 - autoCoef) * grDb;
                wet *= (float) dbToGain (0.66 * autoGr);
            }
            l[i] = ((lastL * wet) * mx + l[i] * (1.0f - mx)) * trimGain;
            r[i] = ((lastR * wet) * mx + r[i] * (1.0f - mx)) * trimGain;
            maxGr = std::max (maxGr, -gainToDb (g));
        }
        meters.set ((float) maxGr, 0.0f, inPeak, Meters::peak (l, r, n));
    }

private:
    static constexpr int maxBlock = 1024;
    double sr = 44100.0;
    vmatch::opto::Leveler classicEngine;
    std::array<float, maxBlock> dryL {}, dryR {};
    Biquad emph;
    double panel = 0, cellFast = 0, cellSlow = 0, memory = 0;
    double panelAttack = 0, panelRelease = 0, fastAttack = 0, fastRelease = 0, slowAttack = 0, memoryUp = 0, memoryDown = 0;
    double autoGr = 0, autoCoef = 0;
    float lastL = 0, lastR = 0;
    Meters meters;
};
} // namespace
} // namespace glow

// ================================================================== Hush

namespace hush
{
double hz (float v) noexcept { return logMap (v, 2000.0, 16000.0); }
double lowPassHz (float v) noexcept { return logMap (v, 2000.0, 20000.0); }
double thresholdDb (float v) noexcept { return lin (v, -60.0, 0.0); }
double rangeDb (float v) noexcept { return lin (v, 0.0, 24.0); }
double releaseMs (float v) noexcept { return logMap (v, 10.0, 250.0); }
double lookaheadMs() noexcept { return 2.0; }
double lookaheadTimeMs (float v) noexcept { return std::round (lin (v, 0.0, 15.0) * 100.0) / 100.0; } // (0.01 ms steps: the default is exactly 2 ms)
float lookaheadTimeValue (double ms) noexcept { return (float) std::clamp (ms / 15.0, 0.0, 1.0); }
double classicThresholdDb (float v) noexcept { return lin (v, -80.0, 0.0); }
bool classic (const EffectParams& p) noexcept { return on (p[voicing]); }

namespace
{
class DeEsser final : public Effect
{
public:
    explicit DeEsser (const EffectParams* p = nullptr) : primedMs (p != nullptr ? lookaheadTimeMs ((*p)[lookaheadTime]) : lookaheadMs()) {}

    void prepare (double rate) noexcept override
    {
        sr = rate;
        setDelay (primedMs);
        classicEngine.prepare (rate);
        reset();
    }

    void reset() noexcept override
    {
        delay.reset();
        scDelay.reset();
        classicEngine.reset();
        detect.reset();
        detectLp.reset();
        listenF.reset();
        listenLp.reset();
        split.reset();
        bandLp.reset();
        bandLp2.reset();
        topHp.reset();
        topHp2.reset();
        env = 0.0;
        gr = 0.0;
        envCh[0] = envCh[1] = grCh[0] = grCh[1] = 0.0;
        fullEnv = 0.0;
        for (auto& o : os) o.reset();
        for (auto& o : osBand) o.reset();
        for (auto& o : osGain) o.reset();
    }

    int latencySamples() const noexcept override { return delay.len + (osFactor > 1 ? Oversampler::latency() : 0); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }
    void setSidechain (const float* l, const float* r) noexcept override { scL = l; scR = r; }

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
        // round 128: the look-ahead time is the delay (and the reported latency; the engine re-plans the delay
        // compensation when it moves). The default 2 ms is the old fixed value.
        if (const auto ms = lookaheadTimeMs (p[lookaheadTime]); std::abs (ms - delayMs) > 1e-9)
            setDelay (ms);
        const auto inGainDb = lin (p[input], -12.0, 12.0);
        if (inGainDb != 0.0)
        {
            const auto gIn = (float) dbToGain (inGainDb);
            for (int i = 0; i < n; ++i) { l[i] *= gIn; r[i] *= gIn; }
        }
        const auto inPeak = Meters::peak (l, r, n);
        // round 135: oversampling (Hush's own voicing): the gain and the band it acts on are upsampled with the audio
        const auto factor = classic (p) ? 1 : oversamplingOf (p[oversampling]);
        if (factor != osFactor)
        {
            osFactor = factor;
            for (auto* bank : { &os, &osBand, &osGain })
                for (auto& o : *bank) { o.setFactor (std::max (2, factor)); o.reset(); }
        }
        const bool over = osFactor > 1;
        if (classic (p))
        {
            // the measured classic de-esser has no look-ahead; the fixed latency Hush reports is kept as a plain delay
            vmatch::deesser::Settings c;
            c.hz = hz (p[frequency]);
            c.thresholdDb = classicThresholdDb (p[threshold]);
            c.bandPass = on (p[detector]);
            c.wideband = on (p[mode]);
            c.listen = on (p[listen]);
            c.outputDb = lin (p[output], -12.0, 12.0);
            classicEngine.process (l, r, n, c);
            for (int i = 0; i < n; ++i)
                delay.process (l[i], r[i]);
            meters.set (classicEngine.reductionDb(), 0.0f, inPeak, Meters::peak (l, r, n));
            return;
        }
        const auto f = std::min (hz (p[frequency]), sr * 0.45);
        const bool band = on (p[detector]);
        if (band)
            detect.filter (sr, 2, f, 1.4);
        else
            detect.filter (sr, 1, f, 0.70710678);
        listenF = detect;
        split.filter (sr, 0, f, 0.70710678);
        const bool lpOn = p[lowPass] < 0.999f;
        if (lpOn)
        {
            const auto lpHz = std::min (std::max (lowPassHz (p[lowPass]), f * 1.05), sr * 0.45);
            detectLp.filter (sr, 0, lpHz, 0.70710678);
            listenLp.filter (sr, 0, lpHz, 0.70710678);
            bandLp.filter (sr, 0, lpHz, 0.70710678); // Linkwitz-Riley 4th order at the low-pass: band = LP², above = HP²
            bandLp2 = bandLp;
            topHp.filter (sr, 1, lpHz, 0.70710678);
            topHp2 = topHp;
        }
        const auto thr = thresholdDb (p[threshold]), range = rangeDb (p[hush::range]);
        const bool wide = on (p[mode]), listening = on (p[listen]), ahead = on (p[lookahead]);
        const bool allroundMode = on (p[allround]), mid = on (p[linkMid]);
        const bool ext = on (p[external]) && scL != nullptr && scR != nullptr;
        const auto link = std::clamp ((double) p[stereoLink], 0.0, 1.0);
        const bool linked = link >= 1.0 || mid;
        const auto release = coef (releaseMs (p[hush::release]) * 0.001, sr);
        const auto attack = coef (ahead ? std::max (delayMs, 0.03) * 0.001 / 3.0 : 0.0005, sr);
        const auto out = (float) dbToGain (lin (p[output], -12.0, 12.0));
        constexpr double knee = 3.0;
        double maxGr = 0.0;

        // gain reduction (dB) wanted for a detected level `s` (and the full-band level in allround mode)
        auto wanted = [&] (double s, double full) {
            auto over = gainToDb (s) - thr;
            if (allroundMode) // the sibilance must also stand out from the whole signal (steady highs in a mix trigger less)
                over -= 0.5 * std::max (0.0, gainToDb (full) - gainToDb (s) - 6.0);
            double want = 0.0;
            if (over > knee)
                want = over;
            else if (over > -knee)
                want = (over + knee) * (over + knee) / (4.0 * knee);
            return std::min (want, range);
        };

        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            float da = a, db = b;
            delay.process (da, db);
            // with lookahead the detector hears the signal before it is played; without, both see the delayed one
            auto xa = ahead ? a : da, xb = ahead ? b : db;
            if (ext)
            {
                float sa = scL[i], sb = scR[i];
                float dsa = sa, dsb = sb;
                scDelay.process (dsa, dsb);
                xa = ahead ? sa : dsa;
                xb = ahead ? sb : dsb;
            }
            double sA, sB;
            if (mid)
            {
                auto m = detect.process (0.5f * (xa + xb), 0);
                if (lpOn) m = detectLp.process (m, 0);
                sA = sB = std::abs (m);
            }
            else
            {
                auto ya = detect.process (xa, 0), yb = detect.process (xb, 1);
                if (lpOn) { ya = detectLp.process (ya, 0); yb = detectLp.process (yb, 1); }
                sA = std::abs (ya);
                sB = std::abs (yb);
            }
            double full = 0.0;
            if (allroundMode)
            {
                const double fa = std::max (std::abs (xa), std::abs (xb));
                fullEnv = fa > fullEnv ? fa : release * fullEnv + (1.0 - release) * fa;
                full = fullEnv;
            }
            float gA, gB;
            if (linked)
            {
                const auto s = std::max (sA, sB);
                env = s > env ? s : release * env + (1.0 - release) * s;
                const auto want = wanted (env, full);
                gr = want > gr ? attack * gr + (1.0 - attack) * want : release * gr + (1.0 - release) * want;
                gA = gB = (float) dbToGain (-gr);
                maxGr = std::max (maxGr, gr);
            }
            else
            {
                const auto s = std::max (sA, sB);
                const double ss[2] { link * s + (1.0 - link) * sA, link * s + (1.0 - link) * sB };
                for (int c = 0; c < 2; ++c)
                {
                    envCh[c] = ss[c] > envCh[c] ? ss[c] : release * envCh[c] + (1.0 - release) * ss[c];
                    const auto want = wanted (envCh[c], full);
                    grCh[c] = want > grCh[c] ? attack * grCh[c] + (1.0 - attack) * want : release * grCh[c] + (1.0 - release) * want;
                    maxGr = std::max (maxGr, grCh[c]);
                }
                gA = (float) dbToGain (-grCh[0]);
                gB = (float) dbToGain (-grCh[1]);
            }

            if (listening)
            {
                a = listenF.process (da, 0);
                b = listenF.process (db, 1);
                if (lpOn) { a = listenLp.process (a, 0); b = listenLp.process (b, 1); }
            }
            else if (over)
            {
                // output = input + band × (gain − 1): the band (whole signal, above the split, or between the two
                // handles) and the gain go to the high rate below
                float ba = da, bb = db;
                if (! wide)
                {
                    const auto la = split.process (da, 0), lb = split.process (db, 1);
                    ba = da - la;
                    bb = db - lb;
                    if (lpOn)
                    {
                        ba = bandLp2.process (bandLp.process (ba, 0), 0);
                        bb = bandLp2.process (bandLp.process (bb, 1), 1);
                        topHp2.process (topHp.process (da - la, 0), 0); // (kept running: switching back stays smooth)
                        topHp2.process (topHp.process (db - lb, 1), 1);
                    }
                }
                bandBuf[0][(std::size_t) i] = ba;
                bandBuf[1][(std::size_t) i] = bb;
                gainBuf[0][(std::size_t) i] = gA - 1.0f;
                gainBuf[1][(std::size_t) i] = gB - 1.0f;
                a = da;
                b = db;
            }
            else if (wide)
            {
                a = da * gA;
                b = db * gB;
            }
            else if (! lpOn)
            {
                // complementary split: low = 2nd-order low-pass, high = input − low (sums back exactly)
                const auto la = split.process (da, 0), lb = split.process (db, 1);
                a = la + (da - la) * gA;
                b = lb + (db - lb) * gB;
            }
            else
            {
                // round 128: only the band between the two handles is turned down; above the low-pass it passes
                // (the high part is split again at the low-pass by a Linkwitz-Riley pair: flat sum, no notch)
                const auto la = split.process (da, 0), lb = split.process (db, 1);
                const auto ha = da - la, hb = db - lb;
                const auto ma = bandLp2.process (bandLp.process (ha, 0), 0), mb = bandLp2.process (bandLp.process (hb, 1), 1);
                const auto ta = topHp2.process (topHp.process (ha, 0), 0), tb = topHp2.process (topHp.process (hb, 1), 1);
                a = la + ma * gA + ta;
                b = lb + mb * gB + tb;
            }
            l[i] = a * out;
            r[i] = b * out;
        }
        if (over)
        {
            const auto m = n * osFactor;
            for (int c = 0; c < 2; ++c)
            {
                auto* x = c == 0 ? l : r;
                if (listening)
                {
                    os[(std::size_t) c].process (x, n, [] (float v) noexcept { return v; });
                    continue;
                }
                auto& ub = upBand[(std::size_t) c];
                auto& ug = upGain[(std::size_t) c];
                int k = 0, j = 0, q = 0;
                osBand[(std::size_t) c].process (bandBuf[(std::size_t) c].data(), n, [&] (float v) noexcept { ub[(std::size_t) std::min (k++, m - 1)] = v; return 0.0f; });
                osGain[(std::size_t) c].process (gainBuf[(std::size_t) c].data(), n, [&] (float v) noexcept { ug[(std::size_t) std::min (j++, m - 1)] = v; return 0.0f; });
                os[(std::size_t) c].process (x, n, [&] (float v) noexcept {
                    const auto at = (std::size_t) std::min (q++, m - 1);
                    return v + ub[at] * ug[at] * out;
                });
            }
        }
        meters.set ((float) maxGr, 0.0f, inPeak, Meters::peak (l, r, n));
        scL = scR = nullptr;
    }

    void setDelay (double ms) noexcept
    {
        delayMs = ms;
        const auto len = (int) std::lround (ms * 0.001 * sr);
        delay.setLength (len);
        scDelay.setLength (len);
    }

    static constexpr int maxBlock = 1024, maxFactor = 8;
    int osFactor = 1;
    std::array<Oversampler, 2> os, osBand, osGain;
    std::array<std::array<float, maxBlock>, 2> bandBuf {}, gainBuf {};
    std::array<std::array<float, maxBlock * maxFactor>, 2> upBand {}, upGain {};
    double sr = 44100.0;
    double primedMs = 2.0, delayMs = 2.0;
    Delay2<4096> delay, scDelay;
    vmatch::deesser::DeEsser classicEngine;
    Biquad detect, detectLp, listenF, listenLp, split, bandLp, bandLp2, topHp, topHp2;
    double env = 0.0, gr = 0.0, fullEnv = 0.0;
    double envCh[2] {}, grCh[2] {};
    const float* scL = nullptr;
    const float* scR = nullptr;
    Meters meters;
};
} // namespace
} // namespace hush

// ================================================================== Aether

namespace aether
{
double airDb (float v) noexcept { return lin (v, 0.0, 15.0); }
double airHz (float v) noexcept { return logMap (v, 5000.0, 20000.0); }
double exciterHz (float v) noexcept { return logMap (v, 1000.0, 12000.0); }

namespace
{
// ---- the dynamic air model (round 124) ------------------------------------------------------------------------------
// Black-box measurements of the official Fresh Air trial (docs/analise/REA_rodada124.md §3), our own code:
//   y = s(mid, high) * x + kMid(mid) * gMid(t) * HPmid(x) + kHigh(high) * gHigh(t) * HPhigh(x)
// HPmid / HPhigh: RBJ 2nd-order high-passes (2993 Hz, Q 0.629 / 9000 Hz, Q 0.707; the same analog corners at 44.1
// and 48 kHz); s, kMid, kHigh: measured on a 0.025 grid (small-signal transfer functions, residual < 0.4 %).
// gMid: a linked peak detector (instant attack, 5 ms hold, 96 ms release) of the band, 4:1 above -30.1 dBFS,
// released through a 7.7 ms one-pole. gHigh: two 4:1 stages — a peak detector with 2-pole gain smoothing, then a
// feedback stage on the band after the first one, whose threshold rises as High air goes down (T + c (1 - high)^2).
namespace dyn
{
constexpr double gridMidS[41] { 1.00237, 1.02960, 1.04368, 1.05483, 1.06431, 1.07263, 1.08006, 1.08676, 1.09284, 1.09838, 1.10343,
    1.10804, 1.11224, 1.11606, 1.11952, 1.12264, 1.12542, 1.12788, 1.13004, 1.13189, 1.13345, 1.13472, 1.13571, 1.13642, 1.13685,
    1.13700, 1.13689, 1.13650, 1.13584, 1.13492, 1.13373, 1.13227, 1.13055, 1.12855, 1.12629, 1.12376, 1.12097, 1.11790, 1.11456,
    1.11095, 1.10707 };
constexpr double gridMidK[41] { 0.00000, 0.13781, 0.22533, 0.30473, 0.38070, 0.45506, 0.52874, 0.60230, 0.67608, 0.75035, 0.82529,
    0.90102, 0.97767, 1.05531, 1.13403, 1.21388, 1.29491, 1.37717, 1.46071, 1.54556, 1.63175, 1.71932, 1.80829, 1.89870, 1.99056,
    2.08391, 2.17877, 2.27515, 2.37309, 2.47260, 2.57371, 2.67643, 2.78079, 2.88681, 2.99451, 3.10390, 3.21501, 3.32786, 3.44246,
    3.55884, 3.67701 };
constexpr double gridHighS[41] { 1.00237, 1.03339, 1.04999, 1.06349, 1.07527, 1.08587, 1.09559, 1.10462, 1.11306, 1.12100, 1.12851,
    1.13562, 1.14238, 1.14881, 1.15493, 1.16077, 1.16634, 1.17166, 1.17673, 1.18156, 1.18616, 1.19055, 1.19472, 1.19867, 1.20242,
    1.20597, 1.20932, 1.21247, 1.21543, 1.21820, 1.22077, 1.22316, 1.22535, 1.22736, 1.22918, 1.23082, 1.23227, 1.23353, 1.23461,
    1.23550, 1.23620 };
constexpr double gridHighK[41] { 0.00000, 0.26995, 0.45609, 0.63358, 0.81042, 0.98969, 1.17298, 1.36128, 1.55524, 1.75537, 1.96206,
    2.17564, 2.39639, 2.62458, 2.86045, 3.10422, 3.35611, 3.61634, 3.88510, 4.16260, 4.44906, 4.74466, 5.04961, 5.36411, 5.68837,
    6.02260, 6.36699, 6.72176, 7.08712, 7.46329, 7.85048, 8.24890, 8.65879, 9.08036, 9.51384, 9.95946, 10.41747, 10.88807, 11.37154,
    11.86810, 12.37801 };
inline double grid (const double (&t)[41], double v) noexcept
{
    const auto x = std::clamp (v, 0.0, 1.0) * 40.0;
    const auto i = std::min (39, (int) x);
    return t[i] + (t[i + 1] - t[i]) * (x - i);
}
// the dry gain with both bands at 0
constexpr double baseGain = 1.00237;

// the fitted dynamics (REA_rodada124 §3)
struct Tuning
{
    double midThreshold = -30.13, midHoldMs = 5.0, midReleaseMs = 96.03, midGainReleaseMs = 7.706;
    double highThreshold = -38.03, highReleaseMs = 0.5354, highGainAttackMs = 2.1428, highGainReleaseMs = 55.52;
    double high2ReleaseMs = 0.0190, high2GainAttackMs = 56.73, high2GainReleaseMs = 53.45, high2Slope = 8.257;
    double high2Threshold = -41.13, high2Curve = 15.585;
};

class Engine
{
public:
    void prepare (double rate) noexcept
    {
        sr = rate;
        mid.hp (rate, 2993.0, 0.6291);
        high.hp (rate, 9000.0, 0.7069);
        reset();
    }
    void reset() noexcept
    {
        mid.reset();
        high.reset();
        envM = holdM = gM = envH = g1a = g1b = env2 = g2a = g2b = 0.0;
    }
    // processes in place; `out` is the output gain (linear)
    void process (float* l, float* r, int n, double midAmount, double highAmount, double out) noexcept
    {
        const Tuning t;
        const auto c = [this] (double ms) { return ms <= 0.0 ? 0.0 : std::exp (-1000.0 / (ms * sr)); };
        const auto s = (grid (gridMidS, midAmount) + grid (gridHighS, highAmount) - baseGain) * out;
        const auto km = grid (gridMidK, midAmount) * out, kh = grid (gridHighK, highAmount) * out;
        const auto mRel = c (t.midReleaseMs), mGr = c (t.midGainReleaseMs), mHold = t.midHoldMs * sr / 1000.0;
        const auto hRel = c (t.highReleaseMs), h1a = c (t.highGainAttackMs), h1r = c (t.highGainReleaseMs);
        const auto h2Rel = c (t.high2ReleaseMs), h2a = c (t.high2GainAttackMs), h2r = c (t.high2GainReleaseMs);
        const auto t2 = t.high2Threshold + t.high2Curve * (1.0 - highAmount) * (1.0 - highAmount);
        for (int i = 0; i < n; ++i)
        {
            const double xl = l[i], xr = r[i];
            const double ml = mid.process ((float) xl, 0), mr = mid.process ((float) xr, 1);
            const double hl = high.process ((float) xl, 0), hr = high.process ((float) xr, 1);
            // mid: peak with hold, 4:1
            const auto pm = std::max (std::fabs (ml), std::fabs (mr));
            if (pm >= envM) { envM = pm; holdM = mHold; }
            else if (holdM > 0.0) holdM -= 1.0;
            else envM = pm + (envM - pm) * mRel;
            const auto gmTarget = -0.75 * std::max (0.0, 20.0 * std::log10 (envM + 1e-30) - t.midThreshold);
            gM = gmTarget < gM ? gmTarget : gmTarget + (gM - gmTarget) * mGr;
            // high, stage 1: peak, 4:1, 2-pole gain smoothing
            const auto ph = std::max (std::fabs (hl), std::fabs (hr));
            envH = ph > envH ? ph : ph + (envH - ph) * hRel;
            const auto g1Target = -0.75 * std::max (0.0, 20.0 * std::log10 (envH + 1e-30) - t.highThreshold);
            const auto k1 = g1Target < g1b ? h1a : h1r;
            g1a = g1Target + (g1a - g1Target) * k1;
            g1b = g1a + (g1b - g1a) * k1;
            const auto g1 = std::pow (10.0, g1b / 20.0);
            // high, stage 2: feedback (detector after both stages), peak, 2-pole gain smoothing
            const auto g2 = std::pow (10.0, g2b / 20.0);
            const auto p2 = ph * g1 * g2;
            env2 = p2 >= env2 ? p2 : p2 + (env2 - p2) * h2Rel;
            const auto g2Target = -t.high2Slope * std::max (0.0, 20.0 * std::log10 (env2 + 1e-30) - t2);
            const auto k2 = g2Target < g2b ? h2a : h2r;
            g2a = g2Target + (g2a - g2Target) * k2;
            g2b = g2a + (g2b - g2a) * k2;
            const auto gm = km * std::pow (10.0, gM / 20.0), gh = kh * g1 * g2;
            l[i] = (float) (s * xl + gm * ml + gh * hl);
            r[i] = (float) (s * xr + gm * mr + gh * hr);
        }
    }
    float reductionDb() const noexcept { return (float) std::min (gM, g1b + g2b); }

private:
    struct Hp2 : Biquad
    {
        void hp (double rate, double fc, double q) noexcept
        {
            const auto w = 2.0 * 3.14159265358979323846 * fc / rate, a = std::sin (w) / (2.0 * q), cw = std::cos (w);
            set ((1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0, 1.0 + a, -2.0 * cw, 1.0 - a);
        }
        double process (float x, int ch) noexcept
        {
            const auto y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };
    double sr = 48000.0;
    Hp2 mid, high;
    double envM = 0, holdM = 0, gM = 0, envH = 0, g1a = 0, g1b = 0, env2 = 0, g2a = 0, g2b = 0;
};
} // namespace dyn

class Air final : public Effect
{
public:
    Air()
    {
        for (auto& o : os)
            o.setFactor (4);
    }

    void prepare (double rate) noexcept override
    {
        sr = rate;
        dry.setLength (Oversampler::latency());
        dynEngine.prepare (rate);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& o : os)
            o.reset();
        dry.reset();
        shelf.reset();
        pre.reset();
        post.reset();
        dynEngine.reset();
    }

    // the dynamic model has no latency (as the measured reference); the exciter path is oversampled
    int latencySamples() const noexcept override { return dynamicMode ? 0 : Oversampler::latency(); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const bool dynamicNow = p[dynamic] >= 0.5f;
        if (dynamicNow != dynamicMode)
        {
            dynamicMode = dynamicNow;
            reset();
        }
        for (int off = 0; off < n; off += maxBlock)
            if (dynamicMode)
                dynamicChunk (l + off, r + off, std::min (maxBlock, n - off), p);
            else
                chunk (l + off, r + off, std::min (maxBlock, n - off), p);
    }

private:
    void dynamicChunk (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        const auto inPeak = Meters::peak (l, r, n);
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        if (mx < 1.0f)
            for (int i = 0; i < n; ++i)
                hl[(std::size_t) i] = l[i], hr[(std::size_t) i] = r[i];
        dynEngine.process (l, r, n, std::clamp (p[midAir], 0.0f, 1.0f), std::clamp (p[highAir], 0.0f, 1.0f), dbToGain (lin (p[output], -12.0, 12.0)));
        if (mx < 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] = hl[(std::size_t) i] + (l[i] - hl[(std::size_t) i]) * mx;
                r[i] = hr[(std::size_t) i] + (r[i] - hr[(std::size_t) i]) * mx;
            }
        meters.set (0.0f, -dynEngine.reductionDb(), inPeak, Meters::peak (l, r, n));
    }

    void chunk (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        const auto inPeak = Meters::peak (l, r, n);
        shelf.shelf (sr, std::min (airHz (p[airFreq]), sr * 0.45), airDb (p[air]), true, 0.6);
        const auto ex = std::min (exciterHz (p[exciterFreq]), sr * 0.45);
        pre.filter (sr, 1, ex, 0.70710678);
        post.filter (sr, 1, ex, 0.70710678);
        const auto amount = 2.0f * std::clamp (p[harmonics], 0.0f, 1.0f);
        const auto odd = std::clamp (p[character], 0.0f, 1.0f);
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto out = (float) dbToGain (lin (p[output], -12.0, 12.0));

        // harmonic generator (DAFX ch. 4, exciters): only the band above the exciter frequency is shaped, at 4x;
        // an even part (x² -like, 2nd harmonic) and an odd part (tanh − x, 3rd harmonic), blended by `character`
        auto gen = [odd] (float x) noexcept
        {
            const auto k = 4.0f * x;
            const auto even = (std::sqrt (1.0f + k * k) - 1.0f) * 0.25f;
            const auto oddPart = std::tanh (3.0f * x) / 3.0f - x;
            return (1.0f - odd) * even - odd * oddPart * 3.0f;
        };

        for (int i = 0; i < n; ++i)
        {
            hl[(std::size_t) i] = pre.process (l[i], 0);
            hr[(std::size_t) i] = pre.process (r[i], 1);
        }
        if (amount > 0.0f)
        {
            os[0].process (hl.data(), n, gen);
            os[1].process (hr.data(), n, gen);
        }
        else
        {
            // keep the oversamplers' state moving so turning the harmonics up does not click with stale history
            auto zero = [] (float) noexcept { return 0.0f; };
            os[0].process (hl.data(), n, zero);
            os[1].process (hr.data(), n, zero);
        }

        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            dry.process (a, b);
            const auto wa = shelf.process (a, 0) + amount * post.process (hl[(std::size_t) i], 0);
            const auto wb = shelf.process (b, 1) + amount * post.process (hr[(std::size_t) i], 1);
            l[i] = (a + (wa - a) * mx) * out;
            r[i] = (b + (wb - b) * mx) * out;
        }
        meters.set (0.0f, 0.0f, inPeak, Meters::peak (l, r, n));
    }

    static constexpr int maxBlock = 2048;
    double sr = 44100.0;
    std::array<Oversampler, 2> os;
    Delay2<256> dry;
    Biquad shelf, pre, post;
    dyn::Engine dynEngine;
    bool dynamicMode = false;
    std::array<float, maxBlock> hl {}, hr {};
    Meters meters;
};
} // namespace
} // namespace aether

// ================================================================== Ember

namespace ember
{
Style styleOf (float v) noexcept { return (Style) std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * (styles - 1)), 0, styles - 1); }
double driveDb (float v) noexcept { return lin (v, 0.0, 36.0); }
double toneTilt (float v) noexcept { return lin (v, -1.0, 1.0); }
double lowCutHz (float v) noexcept { return v <= 0.0f ? 0.0 : logMap (v, 20.0, 500.0); }
double highCutHz (float v) noexcept { return v >= 1.0f ? 0.0 : logMap (v, 2000.0, 20000.0); }
int oversampling (float v) noexcept { return v < 0.25f ? 2 : (v < 0.75f ? 4 : 8); }
int voiceIndex (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * voices), 0, voices); }
const char* voiceName (int k) noexcept
{
    static const char* const names[voices + 1] { "Off", "Tape pre", "Console", "Grit", "Triode", "Pentode" };
    return names[std::clamp (k, 0, voices)];
}
Style voiceStyle (int k) noexcept
{
    static const Style st[voices + 1] { Style::tube, Style::tube, Style::console, Style::transistor, Style::tube, Style::tape };
    return st[std::clamp (k, 0, voices)];
}
double lowCutWideHz (float v) noexcept { return v <= 0.0f ? 0.0 : logMap (v, 20.0, 1000.0); }
double highCutWideHz (float v) noexcept { return v >= 1.0f ? 0.0 : logMap (v, 1000.0, 20000.0); }
double driveSteps (float v) noexcept { return driveDb (v) / 3.6; }
int bandCount (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f), 0, 3) + 1; }
double crossoverHz (float v) noexcept { return logMap (v, 30.0, 18000.0); }
float crossoverValue (double hz) noexcept { return (float) std::clamp (std::log (hz / 30.0) / std::log (600.0), 0.0, 1.0); }
int slopeDbOct (float v) noexcept { return v < 0.25f ? 12 : (v < 0.75f ? 24 : 48); }
double bandLevelDb (float v) noexcept { return lin (v, -18.0, 18.0); }
BandState bandState (float v) noexcept { return (BandState) std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 2.0f), 0, 2); }
double inputDb (float v) noexcept { return lin (v, -24.0, 24.0); }
double toneDb (float v) noexcept { return lin (v, -18.0, 18.0); }
double feedbackAmount (float v) noexcept { return lin (v, 0.0, 1.0); }
double feedbackHz (float v) noexcept { return logMap (v, 10.0, 1000.0); }
double dynamicsAmount (float v) noexcept { return lin (v, -1.0, 1.0); }
double panOf (float v) noexcept { return std::clamp ((double) v * 2.0 - 1.0, -1.0, 1.0); }
std::pair<float, float> panScales (double pan) noexcept
{
    pan = std::clamp (pan, -1.0, 1.0);
    return { (float) (pan > 0.0 ? 1.0 - pan : 1.0), (float) (pan < 0.0 ? 1.0 + pan : 1.0) };
}
bool midSide (const EffectParams& p) noexcept { return on (p[channelMode]); }
int bandParam (int band, int field) noexcept
{
    if (band <= 0)
    {
        static constexpr int first[] { style, drive, band1Mix, band1Level };
        return first[std::clamp (field, 0, 3)];
    }
    return band2Style + (band - 1) * 4 + std::clamp (field, 0, 3);
}

float shape (Style s, float x) noexcept
{
    switch (s)
    {
        case Style::tube:
        {
            // asymmetric: a biased tanh (even and odd harmonics, like a single-ended triode stage), unit slope at 0
            constexpr float bias = 0.35f;
            const float tb = std::tanh (bias);
            return (std::tanh (x + bias) - tb) / (1.0f - tb * tb);
        }
        case Style::tape: return x / std::sqrt (1.0f + x * x); // gentle symmetric (odd) saturation
        case Style::transistor:
        {
            const auto a = std::abs (x);
            const auto a2 = a * a;
            return x / std::sqrt (std::sqrt (1.0f + a2 * a2)); // harder knee: x / (1 + x⁴)^¼
        }
        case Style::console:
        {
            // round 135: a softer, mostly symmetric knee with a little even content (x / (1 + |x|^2.5)^0.4 + a small bias)
            constexpr float bias = 0.12f;
            auto f = [] (float u) { const auto a = std::abs (u); return u / std::pow (1.0f + std::pow (a, 2.5f), 0.4f); };
            const auto fb = f (bias);
            return f (x + bias) - fb;
        }
        case Style::fuzz:
        default:
            if (x >= 0.0f)
            {
                const auto y = x / 0.6f, y2 = y * y, y4 = y2 * y2;
                return 0.6f * y / std::sqrt (std::sqrt (std::sqrt (1.0f + y4 * y4))); // near-hard positive clip at 0.6
            }
            return std::tanh (1.5f * x) / 1.5f;
    }
}

namespace
{
// A band's round-128 stages (Saturn parity): tone after the shaper, a feedback loop around it, dynamics on its level.
// Each is skipped while it is at its neutral setting, so the old sound is untouched.
struct BandExtras
{
    std::array<Biquad, 4> tone;   // bass, mid, treble, presence
    bool toneOn = false;
    Biquad fbBand;                // the feedback band (at the oversampled rate)
    double fbGain = 0.0;
    float fbLast[2] {};
    double dyn = 0.0, env = 0.0, envAtk = 0.0, envRel = 0.0;
    std::array<float, 4> lastTone { -1.0f, -1.0f, -1.0f, -1.0f };
    float lastFbHz = -1.0f;
    int lastFactor = 0;

    void reset() noexcept
    {
        for (auto& t : tone) t.reset();
        fbBand.reset();
        fbLast[0] = fbLast[1] = 0.0f;
        env = 0.0;
    }

    void configure (const EffectParams& p, int band, double sr, int factor) noexcept
    {
        std::array<float, 4> tv;
        for (int k = 0; k < 4; ++k) tv[(std::size_t) k] = p[(std::size_t) bandExtra (band, 2 + k)];
        if (tv != lastTone)
        {
            tone[0].shelf (sr, 120.0, toneDb (tv[0]), false, 0.7);
            tone[1].peaking (sr, 800.0, 0.8, toneDb (tv[1]));
            tone[2].shelf (sr, 3500.0, toneDb (tv[2]), true, 0.7);
            tone[3].peaking (sr, 6000.0, 1.0, toneDb (tv[3]));
            lastTone = tv;
        }
        toneOn = false;
        for (const auto v : tv) toneOn = toneOn || std::abs (toneDb (v)) > 1e-6;
        fbGain = 0.9 * feedbackAmount (p[(std::size_t) bandExtra (band, 6)]);
        const auto fhz = p[(std::size_t) bandExtra (band, 7)];
        if (fbGain > 0.0 && (fhz != lastFbHz || factor != lastFactor))
        {
            fbBand.filter (sr * factor, 2, feedbackHz (fhz), 1.4);
            lastFbHz = fhz;
            lastFactor = factor;
        }
        dyn = dynamicsAmount (p[(std::size_t) bandExtra (band, 8)]);
        envAtk = coef (0.005, sr);
        envRel = coef (0.080, sr);
    }

    // the shaper with the feedback loop: the band-passed last output added to the input (channel ch)
    float shapeWithFeedback (Style s, float x, int ch) noexcept
    {
        const auto y = shape (s, x + (float) fbGain * fbLast[ch]);
        fbLast[ch] = fbBand.process (y, ch);
        return y;
    }

    // after the shaper: the level follows the band's input level (dynamics), then the tone
    void post (float& a, float& b, float inA, float inB) noexcept
    {
        if (dyn != 0.0)
        {
            const double x = std::max (std::abs (inA), std::abs (inB));
            env = x > env ? envAtk * env + (1.0 - envAtk) * x : envRel * env + (1.0 - envRel) * x;
            const double over = gainToDb (env + 1e-9) + 18.0; // dB above −18 dBFS
            const auto g = (float) dbToGain (std::clamp (dyn * 0.5 * over, -18.0, 18.0));
            a *= g;
            b *= g;
        }
        if (toneOn)
            for (auto& t : tone)
            {
                a = t.process (a, 0);
                b = t.process (b, 1);
            }
    }
};

class Saturator final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        dry.setLength (Oversampler::latency());
        dc.set (sr, 8.0);
        classic.prepare (sr); // round 136: the Classic voicing (allocates here, never in process)
        classicOn = false;
        for (auto& d : bandDry)
            d.setLength (Oversampler::latency());
        for (auto& d : bandDc)
            d.set (sr, 8.0);
        splitBands = 0; // redesign the crossovers at the new rate
        lastVoice = -1;
        reset();
    }

    void reset() noexcept override
    {
        classic.reset();
        for (auto& o : os)
            o.reset();
        dry.reset();
        lowcut.reset();
        emphasis.reset();
        deemphasis.reset();
        tiltLo.reset();
        tiltHi.reset();
        highcut.reset();
        highcut2.reset();
        highcut1.reset();
        voicePre.reset();
        voicePost.reset();
        dc.reset();
        for (auto& b : bandOs)
            for (auto& o : b)
                o.reset();
        for (auto& d : bandDry)
            d.reset();
        for (auto& d : bandDc)
            d.reset();
        for (auto* x : { &split[0], &split[1], &split[2], &ap2Low, &ap3Low, &ap3Mid })
            x->reset();
        for (auto& x : extras)
            x.reset();
    }

    int latencySamples() const noexcept override { return Oversampler::latency(); }
    float controlOutput (int k) const noexcept override { return meters.read (k); }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        // round 147: mid / side — M = (L + R) / 2 on channel 0, S = (R − L) / 2 on channel 1 (the trial's sign: a
        // left-only tone's even harmonics land on the right), decoded L = M − S, R = M + S
        const bool ms = midSide (p);
        if (ms)
            for (int i = 0; i < n; ++i)
            {
                const auto m = 0.5f * (l[i] + r[i]), sd = 0.5f * (r[i] - l[i]);
                l[i] = m;
                r[i] = sd;
            }
        for (int off = 0; off < n; off += maxBlock)
            chunk (l + off, r + off, std::min (maxBlock, n - off), p);
        if (ms)
            for (int i = 0; i < n; ++i)
            {
                const auto m = l[i], sd = r[i];
                l[i] = m - sd;
                r[i] = m + sd;
            }
    }

private:
    void chunk (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        if (on (p[voicing])) // round 136: the Classic voicing (core/EmberClassic.h) replaces the whole engine
        {
            chunkClassic (l, r, n, p);
            return;
        }
        classicOn = false;
        if (const auto inDb = inputDb (p[inputGain]); std::abs (inDb) > 1e-9) // round 128: the input gain
        {
            const auto g = (float) dbToGain (inDb);
            for (int i = 0; i < n; ++i) { l[i] *= g; r[i] *= g; }
        }
        if (bandCount (p[bands]) > 1)
        {
            chunkBands (l, r, n, p);
            return;
        }
        const auto inPeak = Meters::peak (l, r, n);
        const auto factor = oversampling (p[quality]);
        for (auto& o : os)
            o.setFactor (factor);
        // round 135: a voicing replaces the Style menu's curve and adds its own EQ; PUNISH adds 20 dB before the drive
        const auto vk = voiceIndex (p[voice]);
        const auto s = vk > 0 ? voiceStyle (vk) : styleOf (p[style]);
        const auto drv = (float) dbToGain (driveDb (p[drive]) + (on (p[punish]) ? punishDb : 0.0));
        // round 147: the drive pan — the far channel's drive × (1 − |pan|) (in dB, the trial's 0 … 100 % scale)
        const auto [dsA, dsB] = panScales (panOf (p[bandDrivePanParam (0)]));
        const bool drivePanned = dsA != 1.0f || dsB != 1.0f;
        const auto drvA = drivePanned ? (float) dbToGain (driveDb (p[drive]) * dsA + (on (p[punish]) ? punishDb : 0.0)) : drv;
        const auto drvB = drivePanned ? (float) dbToGain (driveDb (p[drive]) * dsB + (on (p[punish]) ? punishDb : 0.0)) : drv;
        const auto [panA, panB] = panScales (panOf (p[bandPanParam (0)]));
        const bool tape = s == Style::tape && vk == 0;
        const auto [lc, hc] = configureCuts (p);
        configureVoice (vk);
        // tape: pre-emphasis before the drive and the inverse after it, so the highs saturate first (DAFX 4.3)
        emphasis.shelf (sr, 3000.0, 6.0, true, 0.7);
        deemphasis.shelf (sr, 3000.0, -6.0, true, 0.7);
        const auto tilt = toneTilt (p[tone]);
        tiltLo.shelf (sr, 1000.0, -6.0 * tilt, false, 0.5);
        tiltHi.shelf (sr, 1000.0, 6.0 * tilt, true, 0.5);
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto out = (float) dbToGain (lin (p[output], -18.0, 18.0));
        if (on (p[autoGain]) && (drv != lastDrive || s != lastStyle))
        {
            makeup = autoMakeup (s, drv);
            lastDrive = drv;
            lastStyle = s;
        }
        const auto comp = on (p[autoGain]) ? makeup : 1.0f;
        const auto compA = drivePanned && on (p[autoGain]) ? autoMakeup (s, drvA) : comp;
        const auto compB = drivePanned && on (p[autoGain]) ? autoMakeup (s, drvB) : comp;

        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            if (lc > 0.0)
            {
                a = lowcut.process (a, 0);
                b = lowcut.process (b, 1);
            }
            if (vk > 0)
            {
                a = voicePre.process (a, 0);
                b = voicePre.process (b, 1);
            }
            if (tape)
            {
                a = emphasis.process (a, 0);
                b = emphasis.process (b, 1);
            }
            wl[(std::size_t) i] = a * drvA;
            wr[(std::size_t) i] = b * drvB;
        }

        // round 128: band 1's power, mute, feedback, dynamics and tone (single-band mode)
        auto& ex = extras[0];
        ex.configure (p, 0, sr, factor);
        const bool powered = on (p[bandExtra (0, 0)]);
        const bool muted = bandState (p[bandExtra (0, 1)]) == BandState::mute;
        if (ex.fbGain > 0.0)
        {
            auto left = [&ex, s] (float x) noexcept { return ex.shapeWithFeedback (s, x, 0); };
            auto right = [&ex, s] (float x) noexcept { return ex.shapeWithFeedback (s, x, 1); };
            os[0].process (wl.data(), n, left);
            os[1].process (wr.data(), n, right);
        }
        else
        {
            auto curve = [s] (float x) noexcept { return shape (s, x); };
            os[0].process (wl.data(), n, curve);
            os[1].process (wr.data(), n, curve);
        }
        const bool post = ex.toneOn || ex.dyn != 0.0;

        for (int i = 0; i < n; ++i)
        {
            auto a = dc.process (wl[(std::size_t) i], 0) * compA, b = dc.process (wr[(std::size_t) i], 1) * compB;
            if (tape)
            {
                a = deemphasis.process (a, 0);
                b = deemphasis.process (b, 1);
            }
            if (post) ex.post (a, b, l[i], r[i]);
            if (vk > 0)
            {
                a = voicePost.process (a, 0);
                b = voicePost.process (b, 1);
            }
            a = tiltHi.process (tiltLo.process (a, 0), 0);
            b = tiltHi.process (tiltLo.process (b, 1), 1);
            if (hc > 0.0)
            {
                a = applyHighCut (a, 0);
                b = applyHighCut (b, 1);
            }
            float da = l[i], db = r[i];
            dry.process (da, db);
            if (! powered) { a = da; b = db; }  // switched off: the band passes clean
            if (muted) { a = 0.0f; b = 0.0f; }
            a *= panA; // round 147: the band's pan
            b *= panB;
            l[i] = (da + (a - da) * mx) * out;
            r[i] = (db + (b - db) * mx) * out;
        }
        meters.set (0.0f, 0.0f, inPeak, Meters::peak (l, r, n));
    }

    // Multiband mode (REA_rodada108): Linkwitz-Riley bands (the lower ones through the higher splits' all-passes, so
    // the bands sum flat), each with its own style, drive, mix and level, oversampled separately; the cuts, tone,
    // global mix and output as in the single-band mode.
    void chunkBands (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        const auto inPeak = Meters::peak (l, r, n);
        const int nb = bandCount (p[bands]);
        const auto factor = oversampling (p[quality]);
        std::array<double, 3> f { crossoverHz (p[xover1]), crossoverHz (p[xover2]), crossoverHz (p[xover3]) };
        f[1] = std::max (f[1], f[0] * 1.05);
        f[2] = std::max (f[2], f[1] * 1.05);
        const auto slope = slopeDbOct (p[xoverSlope]);
        if (f != splitHz || slope != splitSlope || nb != splitBands)
        {
            for (int k = 0; k < 3; ++k)
                split[(std::size_t) k].design (sr, f[(std::size_t) k], slope);
            ap2Low.design (sr, f[1], slope);
            ap3Low.design (sr, f[2], slope);
            ap3Mid.design (sr, f[2], slope);
            if (nb != splitBands)
            {
                for (auto* x : { &split[0], &split[1], &split[2], &ap2Low, &ap3Low, &ap3Mid })
                    x->reset();
                for (auto& d : bandDry)
                    d.reset();
            }
            splitHz = f;
            splitSlope = slope;
            splitBands = nb;
        }
        const auto [lc, hc] = configureCuts (p);
        const auto tilt = toneTilt (p[tone]);
        tiltLo.shelf (sr, 1000.0, -6.0 * tilt, false, 0.5);
        tiltHi.shelf (sr, 1000.0, 6.0 * tilt, true, 0.5);
        const auto mx = std::clamp (p[mix], 0.0f, 1.0f);
        const auto out = (float) dbToGain (lin (p[output], -18.0, 18.0));
        const bool autoOn = on (p[autoGain]);
        const auto punishGain = (float) (on (p[punish]) ? dbToGain (punishDb) : 1.0); // round 135

        std::array<Style, maxBands> st {};
        std::array<float, maxBands> drv {}, bmix {}, lvl {}, comp {};
        std::array<float, maxBands> drvB {}, compB {}, panA {}, panB {}; // round 147: channel 1's drive, the band pan
        bool anySolo = false;
        for (int b = 0; b < nb; ++b) anySolo = anySolo || bandState (p[(std::size_t) bandExtra (b, 1)]) == BandState::solo;
        for (int b = 0; b < nb; ++b)
        {
            st[(std::size_t) b] = styleOf (p[(std::size_t) bandParam (b, 0)]);
            drv[(std::size_t) b] = (float) dbToGain (driveDb (p[(std::size_t) bandParam (b, 1)])) * punishGain;
            bmix[(std::size_t) b] = std::clamp (p[(std::size_t) bandParam (b, 2)], 0.0f, 1.0f);
            lvl[(std::size_t) b] = (float) dbToGain (bandLevelDb (p[(std::size_t) bandParam (b, 3)]));
            comp[(std::size_t) b] = autoOn ? autoMakeup (st[(std::size_t) b], drv[(std::size_t) b]) : 1.0f;
            // round 147: drive pan (channel 0's drive stays in drv) and band pan
            const auto [dsA, dsB] = panScales (panOf (p[(std::size_t) bandDrivePanParam (b)]));
            const auto dDb = driveDb (p[(std::size_t) bandParam (b, 1)]);
            drvB[(std::size_t) b] = drv[(std::size_t) b];
            compB[(std::size_t) b] = comp[(std::size_t) b];
            if (dsA != 1.0f || dsB != 1.0f)
            {
                drv[(std::size_t) b] = (float) dbToGain (dDb * dsA) * punishGain;
                drvB[(std::size_t) b] = (float) dbToGain (dDb * dsB) * punishGain;
                comp[(std::size_t) b] = autoOn ? autoMakeup (st[(std::size_t) b], drv[(std::size_t) b]) : 1.0f;
                compB[(std::size_t) b] = autoOn ? autoMakeup (st[(std::size_t) b], drvB[(std::size_t) b]) : 1.0f;
            }
            const auto [pa, pb] = panScales (panOf (p[(std::size_t) bandPanParam (b)]));
            panA[(std::size_t) b] = pa;
            panB[(std::size_t) b] = pb;
            bandOs[(std::size_t) b][0].setFactor (factor);
            bandOs[(std::size_t) b][1].setFactor (factor);
        }

        for (int i = 0; i < n; ++i)
        {
            float x[2] { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto v = x[c];
                if (lc > 0.0)
                    v = lowcut.process (v, c);
                float b0, rest, b1, rest2, b2, b3;
                split[0].run (v, c, b0, rest);
                if (nb == 2)
                {
                    band (0, c, i) = b0;
                    band (1, c, i) = rest;
                    continue;
                }
                split[1].run (rest, c, b1, rest2);
                b0 = ap2Low.allpass (b0, c);
                if (nb == 3)
                {
                    band (0, c, i) = b0;
                    band (1, c, i) = b1;
                    band (2, c, i) = rest2;
                    continue;
                }
                split[2].run (rest2, c, b2, b3);
                band (0, c, i) = ap3Low.allpass (b0, c);
                band (1, c, i) = ap3Mid.allpass (b1, c);
                band (2, c, i) = b2;
                band (3, c, i) = b3;
            }
        }

        // each band: keep its dry copy (delayed like the oversampled path), drive and shape it
        for (int b = 0; b < nb; ++b)
        {
            auto* bl = bandBuf[(std::size_t) b][0].data();
            auto* br = bandBuf[(std::size_t) b][1].data();
            for (int i = 0; i < n; ++i)
            {
                float da = bl[i], db = br[i];
                bandDry[(std::size_t) b].process (da, db);
                dryBand[0][(std::size_t) i] = da;
                dryBand[1][(std::size_t) i] = db;
                bl[i] *= drv[(std::size_t) b];
                br[i] *= drvB[(std::size_t) b];
            }
            const auto s = st[(std::size_t) b];
            // round 128: the band's feedback, dynamics, tone, power and solo / mute
            auto& ex = extras[(std::size_t) b];
            ex.configure (p, b, sr, factor);
            if (ex.fbGain > 0.0)
            {
                auto left = [&ex, s] (float v) noexcept { return ex.shapeWithFeedback (s, v, 0); };
                auto right = [&ex, s] (float v) noexcept { return ex.shapeWithFeedback (s, v, 1); };
                bandOs[(std::size_t) b][0].process (bl, n, left);
                bandOs[(std::size_t) b][1].process (br, n, right);
            }
            else
            {
                auto curve = [s] (float v) noexcept { return shape (s, v); };
                bandOs[(std::size_t) b][0].process (bl, n, curve);
                bandOs[(std::size_t) b][1].process (br, n, curve);
            }
            const auto m = bmix[(std::size_t) b], g = lvl[(std::size_t) b], k = comp[(std::size_t) b], kB = compB[(std::size_t) b];
            const auto gA = g * panA[(std::size_t) b], gB = g * panB[(std::size_t) b];
            const bool post = ex.toneOn || ex.dyn != 0.0;
            const bool powered = on (p[(std::size_t) bandExtra (b, 0)]);
            const auto state = bandState (p[(std::size_t) bandExtra (b, 1)]);
            const bool silent = state == BandState::mute || (anySolo && state != BandState::solo);
            for (int i = 0; i < n; ++i)
            {
                auto wa = bandDc[(std::size_t) b].process (bl[i], 0) * k, wb = bandDc[(std::size_t) b].process (br[i], 1) * kB;
                if (post) ex.post (wa, wb, dryBand[0][(std::size_t) i], dryBand[1][(std::size_t) i]);
                auto ya = (dryBand[0][(std::size_t) i] + (wa - dryBand[0][(std::size_t) i]) * m) * gA;
                auto yb = (dryBand[1][(std::size_t) i] + (wb - dryBand[1][(std::size_t) i]) * m) * gB;
                if (! powered) { ya = dryBand[0][(std::size_t) i]; yb = dryBand[1][(std::size_t) i]; }
                if (silent) { ya = 0.0f; yb = 0.0f; }
                if (b == 0)
                {
                    wl[(std::size_t) i] = ya;
                    wr[(std::size_t) i] = yb;
                }
                else
                {
                    wl[(std::size_t) i] += ya;
                    wr[(std::size_t) i] += yb;
                }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            auto a = tiltHi.process (tiltLo.process (wl[(std::size_t) i], 0), 0);
            auto b = tiltHi.process (tiltLo.process (wr[(std::size_t) i], 1), 1);
            if (hc > 0.0)
            {
                a = applyHighCut (a, 0);
                b = applyHighCut (b, 1);
            }
            float da = l[i], db = r[i];
            dry.process (da, db);
            l[i] = (da + (a - da) * mx) * out;
            r[i] = (db + (b - db) * mx) * out;
        }
        meters.set (0.0f, 0.0f, inPeak, Meters::peak (l, r, n));
    }

    float& band (int b, int c, int i) noexcept { return bandBuf[(std::size_t) b][(std::size_t) c][(std::size_t) i]; }

    // round 135: the low / high cut of either range. Old range: 12 dB/oct both; THUMP gives the low cut a resonance,
    // STEEP makes the high cut 30 dB/oct (5th-order Butterworth), the wide range's gentle high cut is 6 dB/oct
    // [EXEC trial: gentle ≈ 6 dB/oct, steep ≈ 30 dB/oct, low cut 12 dB/oct, Thump +3.5 dB at the cutoff]
    std::pair<double, double> configureCuts (const EffectParams& p) noexcept
    {
        const bool wide = on (p[wideCuts]);
        const auto lc = wide ? lowCutWideHz (p[lowCutWide]) : lowCutHz (p[lowCut]);
        const auto hc = wide ? highCutWideHz (p[highCutWide]) : highCutHz (p[highCut]);
        if (lc > 0.0)
            lowcut.filter (sr, 1, lc, on (p[thump]) ? thumpQ : 0.70710678);
        hcMode = on (p[steep]) ? 2 : (wide ? 1 : 0);
        if (hc > 0.0)
        {
            const auto f = std::min (hc, sr * 0.45);
            if (hcMode == 2)
            {
                highcut.filter (sr, 0, f, 1.0 / (2.0 * std::cos (3.14159265358979323846 / 5.0)));        // Q 0.618
                highcut2.filter (sr, 0, f, 1.0 / (2.0 * std::cos (2.0 * 3.14159265358979323846 / 5.0))); // Q 1.618
            }
            else if (hcMode == 0)
                highcut.filter (sr, 0, f, 0.70710678);
            const auto k = std::tan (3.14159265358979323846 * f / sr); // first-order section (bilinear)
            highcut1.set (k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
        }
        return { lc, hc };
    }

    float applyHighCut (float x, int c) noexcept
    {
        if (hcMode == 1) return highcut1.process (x, c);
        x = highcut.process (x, c);
        if (hcMode == 2) x = highcut1.process (highcut2.process (x, c), c);
        return x;
    }

    // a voicing's EQ before and after its curve (small, broad shelves: the five references differ mostly there)
    void configureVoice (int k) noexcept
    {
        if (k <= 0 || k == lastVoice) return;
        lastVoice = k;
        switch (k)
        {
            case 1: voicePre.shelf (sr, 8000.0, 1.0, true, 0.7); voicePost.shelf (sr, 60.0, 0.0, false, 0.7); break;  // tape pre: open top
            case 2: voicePre.shelf (sr, 7000.0, 2.5, true, 0.7); voicePost.shelf (sr, 50.0, 1.0, false, 0.7); break;  // console: airy, firm bass
            case 3: voicePre.shelf (sr, 6000.0, -2.5, true, 0.7); voicePost.shelf (sr, 80.0, 0.0, false, 0.7); break; // grit: darker top
            case 4: voicePre.shelf (sr, 60.0, -3.0, false, 0.7); voicePost.shelf (sr, 10000.0, 0.5, true, 0.7); break; // triode: thinner bass
            default: voicePre.peaking (sr, 100.0, 0.8, 2.5); voicePost.shelf (sr, 1000.0, 0.0, true, 0.7); break;     // pentode: bass bump
        }
    }

    static constexpr int maxBlock = 2048;
    // level compensation: the RMS gain of the curve on a −12 dBFS sine at this drive, inverted
    static float autoMakeup (Style s, float drv) noexcept
    {
        double in = 0.0, out = 0.0, mean = 0.0;
        constexpr int points = 64;
        std::array<double, points> ys {};
        for (int k = 0; k < points; ++k)
        {
            const auto x = 0.25 * std::sin (2.0 * pi * (k + 0.5) / points);
            ys[(std::size_t) k] = shape (s, (float) (x * drv));
            mean += ys[(std::size_t) k];
            in += x * x;
        }
        mean /= points;
        for (auto y : ys)
            out += (y - mean) * (y - mean);
        return out > 1e-12 ? (float) std::sqrt (in / out) : 1.0f;
    }

    double sr = 44100.0;
    std::array<Oversampler, 2> os;
    Delay2<256> dry;
    Biquad lowcut, emphasis, deemphasis, tiltLo, tiltHi, highcut;
    Biquad highcut2, highcut1, voicePre, voicePost; // round 135
    int hcMode = 0, lastVoice = -1;
    OnePoleHp dc;
    std::array<float, maxBlock> wl {}, wr {};
    float makeup = 1.0f, lastDrive = -1.0f;
    Style lastStyle = Style::tube;
    // round 136: the Classic voicing
    emberclassic::Engine classic;
    emberclassic::Settings classicSet;
    bool classicOn = false;

    void chunkClassic (float* l, float* r, int n, const EffectParams& p) noexcept
    {
        emberclassic::Settings c;
        c.style = emberclassic::styleOf (p[classicStyle]);
        c.drive = emberclassic::driveUnits (p[classicDrive]);
        c.punish = on (p[classicPunish]);
        c.lowCutHz = emberclassic::lowCutHz (p[classicLowCut]);
        c.thump = on (p[classicThump]);
        c.toneDb = emberclassic::toneDb (p[classicTone]);
        c.highCutHz = emberclassic::highCutHz (p[classicHighCut]);
        c.steep = on (p[classicSteep]);
        c.mix = std::clamp ((double) p[classicMix], 0.0, 1.0);
        c.outputDb = emberclassic::outputDb (p[classicOutput]);
        if (! classicOn || ! (c == classicSet))
        {
            if (! classicOn)
                classic.reset();
            classic.set (c);
            classicSet = c;
            classicOn = true;
        }
        const auto inPeak = Meters::peak (l, r, n);
        classic.process (l, r, n);
        meters.set (0.0f, 0.0f, inPeak, Meters::peak (l, r, n));
    }
    // multiband mode
    std::array<LrSplit, 3> split;
    LrSplit ap2Low, ap3Low, ap3Mid;
    std::array<double, 3> splitHz {};
    int splitSlope = 0, splitBands = 0;
    std::array<std::array<Oversampler, 2>, maxBands> bandOs;
    std::array<Delay2<256>, maxBands> bandDry;
    std::array<OnePoleHp, maxBands> bandDc;
    std::array<std::array<std::array<float, maxBlock>, 2>, maxBands> bandBuf {};
    std::array<std::array<float, maxBlock>, 2> dryBand {};
    std::array<BandExtras, maxBands> extras;
    Meters meters;
};
} // namespace
} // namespace ember

// ================================================================== registry

const EffectInfo& ridgeInfo()
{
    static const EffectInfo info { "Ridge Strip",
                                   { { "Input", 0.5f }, { "HPF", 0.0f }, { "LPF", 0.0f },
                                     { "LF gain", 0.5f }, { "LF freq.", 0.40f }, { "LF bell", 0.0f },
                                     { "LMF gain", 0.5f }, { "LMF freq.", 0.43f }, { "LMF Q", 0.43f },
                                     { "HMF gain", 0.5f }, { "HMF freq.", 0.49f }, { "HMF Q", 0.43f },
                                     { "HF gain", 0.5f }, { "HF freq.", 0.70f }, { "HF bell", 0.0f },
                                     { "EQ curve", 0.0f }, { "EQ on", 1.0f },
                                     { "Ratio", 0.0f }, { "Comp. threshold", 1.0f }, { "Comp. release", 0.29f }, { "Fast attack", 0.0f },
                                     { "Gate range", 0.0f }, { "Gate threshold", 0.25f }, { "Gate release", 0.29f }, { "Gate fast", 0.0f },
                                     { "Expander", 0.0f }, { "Dynamics on", 1.0f },
                                     { "Order", 1.0f }, { "Filters to SC", 0.0f }, { "Output", 0.5f }, { "Phase", 0.0f }, { "Comp. mix", 1.0f },
                                     { "Voicing", 0.0f }, { "Filter split", 0.0f } } };
    return info;
}

const EffectInfo& glowInfo()
{
    static const EffectInfo info { "Glow Leveler",
                                   { { "Peak reduction", 0.0f }, { "Gain", 0.0f }, { "Limit", 0.0f }, { "Emphasis", 0.0f }, { "Mix", 1.0f },
                                     { "Trim", 0.5f }, { "Auto makeup", 0.0f }, { "Voicing", 0.0f } } };
    return info;
}

const EffectInfo& hushInfo()
{
    static const EffectInfo info { "Hush De-esser",
                                   { { "Frequency", 0.53f }, { "Threshold", 0.5f }, { "Range", 0.5f }, { "Wideband", 0.0f },
                                     { "Band detector", 0.0f }, { "Listen", 0.0f }, { "Lookahead", 1.0f }, { "Release", 0.56f },
                                     { "Output", 0.5f }, { "Voicing", 0.0f }, // round 123 (vmatch123)
                                     // round 128 (parity with the reference de-esser)
                                     { "Allround", 0.0f }, { "Low-pass freq.", 1.0f }, { "Lookahead time", 2.0f / 15.0f },
                                     { "Stereo link", 1.0f }, { "Link mid", 0.0f }, { "External SC", 0.0f }, { "Input", 0.5f },
                                     { "Display", 1.0f },
                                     { "Oversampling", 0.0f } } }; // round 135
    return info;
}

const EffectInfo& aetherInfo()
{
    static const EffectInfo info { "Aether",
                                   { { "Air", 0.0f }, { "Air freq.", 0.62f }, { "Harmonics", 0.0f }, { "Exciter freq.", 0.56f },
                                     { "Character", 0.5f }, { "Mix", 1.0f }, { "Output", 0.5f },
                                     { "Dynamic", 0.0f }, { "Mid air", 0.0f }, { "High air", 0.0f } } };
    return info;
}

const EffectInfo& emberInfo()
{
    static const EffectInfo info { "Ember Saturator",
                                   { { "Drive", 0.0f }, { "Style", 0.0f }, { "Tone", 0.5f }, { "Low cut", 0.0f }, { "High cut", 1.0f },
                                     { "Mix", 1.0f }, { "Output", 0.5f }, { "Auto gain", 1.0f }, { "Oversampling", 0.5f },
                                     { "Bands", 0.0f }, { "Crossover 1", 0.3266f }, { "Crossover 2", 0.6574f }, { "Crossover 3", 0.9375f },
                                     { "Crossover slope", 0.5f }, { "B1 mix", 1.0f }, { "B1 level", 0.5f },
                                     { "B2 style", 0.0f }, { "B2 drive", 0.0f }, { "B2 mix", 1.0f }, { "B2 level", 0.5f },
                                     { "B3 style", 0.0f }, { "B3 drive", 0.0f }, { "B3 mix", 1.0f }, { "B3 level", 0.5f },
                                     { "B4 style", 0.0f }, { "B4 drive", 0.0f }, { "B4 mix", 1.0f }, { "B4 level", 0.5f },
                                     // round 128 (Saturn parity)
                                     { "Input", 0.5f },
                                     { "B1 on", 1.0f }, { "B1 state", 0.0f }, { "B1 bass", 0.5f }, { "B1 mid", 0.5f }, { "B1 treble", 0.5f }, { "B1 presence", 0.5f },
                                     { "B1 feedback", 0.0f }, { "B1 fb freq.", 0.6990f }, { "B1 dynamics", 0.5f },
                                     { "B2 on", 1.0f }, { "B2 state", 0.0f }, { "B2 bass", 0.5f }, { "B2 mid", 0.5f }, { "B2 treble", 0.5f }, { "B2 presence", 0.5f },
                                     { "B2 feedback", 0.0f }, { "B2 fb freq.", 0.6990f }, { "B2 dynamics", 0.5f },
                                     { "B3 on", 1.0f }, { "B3 state", 0.0f }, { "B3 bass", 0.5f }, { "B3 mid", 0.5f }, { "B3 treble", 0.5f }, { "B3 presence", 0.5f },
                                     { "B3 feedback", 0.0f }, { "B3 fb freq.", 0.6990f }, { "B3 dynamics", 0.5f },
                                     { "B4 on", 1.0f }, { "B4 state", 0.0f }, { "B4 bass", 0.5f }, { "B4 mid", 0.5f }, { "B4 treble", 0.5f }, { "B4 presence", 0.5f },
                                     { "B4 feedback", 0.0f }, { "B4 fb freq.", 0.6990f }, { "B4 dynamics", 0.5f },
                                     // round 135 (the Decapitator role)
                                     { "Punish", 0.0f }, { "Thump", 0.0f }, { "Steep", 0.0f }, { "Voice", 0.0f }, { "Wide cuts", 0.0f },
                                     { "Low cut (wide)", 0.0f }, { "High cut (wide)", 1.0f },
                                     // round 136: the Classic voicing (EmberClassic.h)
                                     { "Voicing", 0.0f }, { "Classic style", 0.0f }, { "Classic drive", 0.0f }, { "Classic punish", 0.0f },
                                     { "Classic low cut", 0.0f }, { "Classic thump", 0.0f }, { "Classic tone", 0.5f }, { "Classic high cut", 1.0f },
                                     { "Classic steep", 0.0f }, { "Classic mix", 1.0f }, { "Classic output", 1.0f },
                                     // round 147 (Saturn 2's channel mode, drive pan and band pan)
                                     { "Channel mode", 0.0f },
                                     { "B1 drive pan", 0.5f }, { "B2 drive pan", 0.5f }, { "B3 drive pan", 0.5f }, { "B4 drive pan", 0.5f },
                                     { "B1 pan", 0.5f }, { "B2 pan", 0.5f }, { "B3 pan", 0.5f }, { "B4 pan", 0.5f } } };
    return info;
}

std::unique_ptr<Effect> makeHush (const EffectParams* params) { return std::make_unique<hush::DeEsser> (params); }

bool isSuiteKind (EffectKind kind) noexcept
{
    return kind == EffectKind::ridgeStrip || kind == EffectKind::glowLeveler || kind == EffectKind::hushDeEsser
           || kind == EffectKind::aether || kind == EffectKind::ember;
}

std::unique_ptr<Effect> make (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::ridgeStrip: return std::make_unique<ridge::Strip>();
        case EffectKind::glowLeveler: return std::make_unique<glow::Leveler>();
        case EffectKind::hushDeEsser: return std::make_unique<hush::DeEsser>();
        case EffectKind::aether: return std::make_unique<aether::Air>();
        case EffectKind::ember: return std::make_unique<ember::Saturator>();
        default: return nullptr;
    }
}

namespace
{
std::string hzText (double f)
{
    char t[32];
    if (f >= 1000.0)
        std::snprintf (t, sizeof (t), "%.2f kHz", f / 1000.0);
    else
        std::snprintf (t, sizeof (t), "%.0f Hz", f);
    return t;
}

std::string fmt (const char* f, double v)
{
    char t[32];
    std::snprintf (t, sizeof (t), f, v);
    return t;
}

std::string onOff (float v) { return on (v) ? "on" : "off"; }
} // namespace

namespace glow
{

std::string classicParamText (int param, float v)
{
    switch (param)
    {
        case peakReduction: case gain: case emphasis: return fmt ("%.1f", v * 100.0);
        default: return paramText (EffectKind::glowLeveler, param, v);
    }
}
} // namespace glow

namespace hush
{
int oversamplingOf (float v) noexcept { return 1 << std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f), 0, 3); }

std::string classicParamText (int param, float v)
{
    switch (param)
    {
        case threshold: return fmt ("%.1f dB", classicThresholdDb (v));
        case range: case lookahead: case release: return "-";
        default: return paramText (EffectKind::hushDeEsser, param, v);
    }
}
} // namespace hush

namespace ridge
{
std::string eParamText (int param, float v)
{
    switch (param)
    {
        case lmfQ: case hmfQ: return fmt ("Q %.2f", eQ (v));
        case lfGain: case lmfGain: case hmfGain: case hfGain: return fmt ("%+.1f dB", bandGainDb (v, false));
        case compThreshold: return fmt ("%+.1f dB", eCompThresholdDb (v));
        case gateRange: return v <= 0.0f ? "off" : fmt ("%.1f dB", eGateRangeDb (v));
        case gateThreshold: return fmt ("%+.1f dB", eGateThresholdDb (v));
        case curve: return "-";
        default: return paramText (EffectKind::ridgeStrip, param, v);
    }
}
} // namespace ridge

std::string paramText (EffectKind kind, int param, float v)
{
    if (kind == EffectKind::ridgeStrip)
    {
        using namespace ridge;
        switch (param)
        {
            case input: case output: return fmt ("%+.1f dB", trimDb (v));
            case hpf: return v <= 0.0f ? "off" : hzText (hpfHz (v));
            case lpf: return v <= 0.0f ? "off" : hzText (lpfHz (v));
            case lfGain: case lmfGain: case hmfGain: case hfGain: return fmt ("%+.1f dB", bandGainDb (v, false)) + " / " + fmt ("%+.1f", bandGainDb (v, true));
            case lfFreq: return hzText (lfHz (v));
            case lmfFreq: return hzText (lmfHz (v));
            case hmfFreq: return hzText (hmfHz (v));
            case hfFreq: return hzText (hfHz (v));
            case lmfQ: case hmfQ: return fmt ("Q %.2f", bandQ (v));
            case lfBell: case hfBell: return on (v) ? "bell" : "shelf";
            case curve: return on (v) ? "black" : "brown";
            case compRatio: return v >= 0.999f ? "inf : 1" : fmt ("%.1f : 1", ratio (v));
            case compThreshold: return fmt ("%.1f dB", compThresholdDb (v));
            case compRelease: case gateRelease: return fmt ("%.2f s", releaseSeconds (v));
            case gateRange: return v <= 0.0f ? "off" : fmt ("%.0f dB", gateRangeDb (v));
            case gateThreshold: return fmt ("%.1f dB", gateThresholdDb (v));
            case expander: return on (v) ? "expander" : "gate";
            case order: return on (v) ? "dyn > EQ" : "EQ > dyn";
            case voicing: return on (v) ? "E console" : "classic";
            case filterSplit: return on (v) ? "split" : "with EQ";
            case phase: return on (v) ? "inverted" : "normal";
            case compMix: return fmt ("%.0f%%", v * 100.0);
            default: return onOff (v);
        }
    }
    if (kind == EffectKind::glowLeveler)
    {
        using namespace glow;
        switch (param)
        {
            case peakReduction: return fmt ("%.0f", v * 100.0);
            case gain: return fmt ("%+.1f dB", gainDb (v));
            case mode: return on (v) ? "limit" : "compress";
            case emphasis: case mix: return fmt ("%.0f%%", v * 100.0);
            case trim: return fmt ("%+.1f dB", trimDb (v));
            case autoMakeup: return on (v) ? "on" : "off";
            default: return fmt ("%.2f", v);
        }
    }
    if (kind == EffectKind::hushDeEsser)
    {
        using namespace hush;
        switch (param)
        {
            case frequency: return hzText (hz (v));
            case threshold: return fmt ("%.1f dB", thresholdDb (v));
            case range: return fmt ("%.1f dB", rangeDb (v));
            case mode: return on (v) ? "wideband" : "split";
            case detector: return on (v) ? "band" : "high-pass";
            case allround: return on (v) ? "allround" : "single vocal";
            case lowPass: return v >= 0.999f ? "off" : hzText (lowPassHz (v));
            case lookaheadTime: return fmt ("%.1f ms", lookaheadTimeMs (v));
            case stereoLink: return fmt ("%.0f%%", v * 100.0);
            case linkMid: return on (v) ? "mid" : "stereo";
            case external: return on (v) ? "external" : "internal";
            case input: return fmt ("%+.1f dB", lin (v, -12.0, 12.0));
            case display: return on (v) ? "shown" : "hidden";
            case release: return fmt ("%.0f ms", releaseMs (v));
            case output: return fmt ("%+.1f dB", lin (v, -12.0, 12.0));
            case voicing: return on (v) ? "classic split" : "modern";
            case oversampling: return oversamplingOf (v) == 1 ? "off" : std::to_string (oversamplingOf (v)) + "x";
            default: return onOff (v);
        }
    }
    if (kind == EffectKind::aether)
    {
        using namespace aether;
        switch (param)
        {
            case air: return fmt ("%+.1f dB", airDb (v));
            case airFreq: return hzText (airHz (v));
            case exciterFreq: return hzText (exciterHz (v));
            case character: return v < 0.05f ? "even" : (v > 0.95f ? "odd" : fmt ("%.0f%% odd", v * 100.0));
            case output: return fmt ("%+.1f dB", lin (v, -12.0, 12.0));
            case dynamic: return onOff (v);
            default: return fmt ("%.0f%%", v * 100.0);
        }
    }
    if (kind == EffectKind::ember)
    {
        using namespace ember;
        switch (param)
        {
            case drive: return fmt ("%+.1f dB", driveDb (v)) + fmt (" (%.1f)", driveSteps (v)); // round 135: + the 0 … 10 scale
            case punish: case thump: case wideCuts: return onOff (v);
            case steep: return on (v) ? "30 dB/oct" : "gentle";
            case voice: return voiceName (voiceIndex (v));
            case lowCutWide: return v <= 0.0f ? "off" : hzText (lowCutWideHz (v));
            case highCutWide: return v >= 1.0f ? "off" : hzText (highCutWideHz (v));
            case style:
            {
                static const char* names[] = { "tube", "tape", "transistor", "fuzz" };
                return names[(int) styleOf (v)];
            }
            case tone: return fmt ("%+.0f%%", toneTilt (v) * 100.0);
            case lowCut: return v <= 0.0f ? "off" : hzText (lowCutHz (v));
            case highCut: return v >= 1.0f ? "off" : hzText (highCutHz (v));
            case mix: return fmt ("%.0f%%", v * 100.0);
            case output: return fmt ("%+.1f dB", lin (v, -18.0, 18.0));
            case autoGain: return onOff (v);
            case quality: return std::to_string (oversampling (v)) + "x";
            case bands: return bandCount (v) == 1 ? "1 band" : std::to_string (bandCount (v)) + " bands";
            case xover1: case xover2: case xover3: return hzText (crossoverHz (v));
            case xoverSlope: return std::to_string (slopeDbOct (v)) + " dB/oct";
            case band2Style: case band3Style: case band4Style:
            {
                static const char* names[] = { "tube", "tape", "transistor", "fuzz" };
                return names[(int) styleOf (v)];
            }
            case band2Drive: case band3Drive: case band4Drive: return fmt ("%+.1f dB", driveDb (v));
            case band1Mix: case band2Mix: case band3Mix: case band4Mix: return fmt ("%.0f%%", v * 100.0);
            case band1Level: case band2Level: case band3Level: case band4Level: return fmt ("%+.1f dB", bandLevelDb (v));
            case inputGain: return fmt ("%+.1f dB", inputDb (v));
            // round 136: the Classic voicing, in the reference's own units
            case voicing: return on (v) ? "Classic" : "Ember";
            case classicStyle: { static const char* names[] = { "A", "E", "N", "T", "P" }; return names[emberclassic::styleOf (v)]; }
            case classicDrive: return fmt ("%.1f", emberclassic::driveUnits (v));
            case classicPunish: case classicThump: return onOff (v);
            case classicSteep: return on (v) ? "steep" : "gentle";
            case classicLowCut: return hzText (emberclassic::lowCutHz (v));
            case classicTone: return fmt ("%+.1f dB", emberclassic::toneDb (v));
            case classicHighCut: return hzText (emberclassic::highCutHz (v));
            case classicMix: return fmt ("%.0f%%", v * 100.0);
            case classicOutput: return fmt ("%+.1f dB", emberclassic::outputDb (v));
            // round 147
            case channelMode: return on (v) ? "Mid/Side" : "Left/Right";
            case band1DrivePan: case band2DrivePan: case band3DrivePan: case band4DrivePan:
            {
                const auto pan = panOf (v);
                return std::abs (pan) < 0.005 ? std::string ("centre") : fmt ("%.0f%% ", std::abs (pan) * 100.0) + (pan < 0.0 ? "L/M" : "R/S");
            }
            case band1Pan: case band2Pan: case band3Pan: case band4Pan:
            {
                const auto [a, b] = panScales (panOf (v));
                const auto db = [] (float g) { return g <= 0.0f ? std::string ("-inf") : fmt ("%.1f", gainToDb (g)); };
                return std::abs (panOf (v)) < 0.005 ? std::string ("centre") : db (a) + " / " + db (b) + " dB";
            }
            default:
                if (param >= band1On && param < band1On + maxBands * extraFields)
                    switch ((param - band1On) % extraFields)
                    {
                        case 0: return onOff (v);
                        case 1: { static const char* names[] = { "normal", "solo", "mute" }; return names[(int) bandState (v)]; }
                        case 2: case 3: case 4: case 5: return fmt ("%+.1f dB", toneDb (v));
                        case 6: return fmt ("%.0f%%", feedbackAmount (v) * 100.0);
                        case 7: return hzText (feedbackHz (v));
                        default: return fmt ("%+.2f", dynamicsAmount (v));
                    }
                return fmt ("%.2f", v);
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::mixsuite
