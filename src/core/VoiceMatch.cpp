#include "core/VoiceMatch.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace lynx::vmatch
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr double refRate = 48000.0; // the rate the calibration was measured at

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double coefMs (double ms, double sr) noexcept { return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-1000.0 / (ms * sr)); }
inline double roundTo (double v, double step) noexcept { return std::round (v / step) * step; }

// piecewise-linear lookup in a sorted table
double interp (const float* xs, const float* ys, std::size_t N, double x) noexcept
{
    if (x <= xs[0])
        return ys[0];
    if (x >= xs[N - 1])
        return ys[N - 1];
    const auto* it = std::upper_bound (xs, xs + N, (float) x);
    const auto i = (std::size_t) (it - xs) - 1;
    const auto t = (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[i] + t * (ys[i + 1] - ys[i]);
}

// bilinear transform of an analog biquad (n2 s^2 + n1 s + n0) / (s^2 + d1 s + d0) with s = K (1 - z^-1) / (1 + z^-1)
void bilinear (const double n[3], const double d[2], double K, Biquad& b) noexcept
{
    const double K2 = K * K;
    const double b0 = n[0] * K2 + n[1] * K + n[2], b1 = 2.0 * (n[2] - n[0] * K2), b2 = n[0] * K2 - n[1] * K + n[2];
    const double a0 = K2 + d[0] * K + d[1], a1 = 2.0 * (d[1] - K2), a2 = K2 - d[0] * K + d[1];
    b.set (b0, b1, b2, a0, a1, a2);
}

double biquadMagnitude (const Biquad& b, double w) noexcept
{
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = b.b0 + b.b1 * c1 + b.b2 * c2, ni = -(b.b1 * s1 + b.b2 * s2);
    const double dr = 1.0 + b.a1 * c1 + b.a2 * c2, di = -(b.a1 * s1 + b.a2 * s2);
    return std::sqrt ((nr * nr + ni * ni) / std::max (dr * dr + di * di, 1e-300));
}
} // namespace

namespace echannel
{
namespace
{
#include "core/VoiceMatchTables.inc"

constexpr double gainNodes[21] = { -15, -13.5, -12, -10.5, -9, -7.5, -6, -4.5, -3, -1.5, 0, 1.5, 3, 4.5, 6, 7.5, 9, 10.5, 12, 13.5, 15 };

// Catmull-Rom over the gain nodes of one segment ([-15,-7.5], [-7.5,0], [0,7.5], [7.5,15]: the band's law changes
// slope at the segment ends, measured), `at(i)` = the feature vector of gain node i
template <typename At>
void gainInterp (double g, At at, double out[5]) noexcept
{
    g = std::clamp (g, -15.0, 15.0);
    const int seg = g < -7.5 ? 0 : (g < 0.0 ? 1 : (g < 7.5 ? 2 : 3));
    const int lo = seg * 5, hi = lo + 5; // node indexes of the segment ends
    int i = std::clamp ((int) std::floor ((g + 15.0) / 1.5), lo, hi - 1);
    const double t = (g - gainNodes[i]) / 1.5;
    double p0[5], p1[5], p2[5], p3[5];
    at (i, p1);
    at (i + 1, p2);
    if (i > lo) at (i - 1, p0); else for (int k = 0; k < 5; ++k) p0[k] = 2 * p1[k] - p2[k];
    if (i + 2 <= hi) at (i + 2, p3); else for (int k = 0; k < 5; ++k) p3[k] = 2 * p2[k] - p1[k];
    const double t2 = t * t, t3 = t2 * t;
    for (int k = 0; k < 5; ++k)
    {
        const double m1 = 0.5 * (p2[k] - p0[k]), m2 = 0.5 * (p3[k] - p1[k]);
        out[k] = (2 * t3 - 3 * t2 + 1) * p1[k] + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * p2[k] + (t3 - t2) * m2;
    }
}

struct Axis { int i; double t; };
Axis axis (const float* nodes, std::size_t N, double x) noexcept
{
    int i = 0;
    while (i < (int) N - 2 && x > nodes[i + 1])
        ++i;
    const double t = std::clamp ((x - nodes[i]) / (nodes[i + 1] - nodes[i]), 0.0, 1.0);
    return { i, t };
}

// the analog-domain biquad (normalised features x reference frequency) of one band, and its reference frequency
bool bandAnalog (int band, const Settings& s, double n[3], double d[2], double& wref) noexcept
{
    double g = 0.0, f = 0.0, q = 0.0;
    bool bell = false;
    switch (band)
    {
        case 0: g = s.lfGain; f = roundTo (std::clamp (s.lfHz, 30.0, 450.0), 1.0); bell = s.lfBell; break;
        case 1: g = s.lmfGain; f = roundTo (std::clamp (s.lmfKHz, 0.2, 2.5), 0.01) * 1000.0; q = s.lmfQ; break;
        case 2: g = s.hmfGain; f = roundTo (std::clamp (s.hmfKHz, 0.6, 7.0), 0.01) * 1000.0; q = s.hmfQ; break;
        default: g = s.hfGain; f = roundTo (std::clamp (s.hfKHz, 1.5, 16.0), 0.01) * 1000.0; bell = s.hfBell; break;
    }
    g = roundTo (g, 0.1);
    if (std::abs (g) < 0.05)
        return false;
    double u[5];
    if (band == 0 || band == 3)
    {
        const bool lo = band == 0;
        const float* feat = lo ? lfFeat : hfFeat;
        const double lw = lo ? interp (lfLawText, lfLawLogW, std::size (lfLawText), f) : interp (hfLawText, hfLawLogW, std::size (hfLawText), f);
        wref = std::exp (lw);
        const auto ax = lo ? axis (lfNodeLogW, std::size (lfNodeLogW), lw) : axis (hfNodeLogW, std::size (hfNodeLogW), lw);
        const int nf = (int) (lo ? std::size (lfNodeLogW) : std::size (hfNodeLogW)), type = bell ? 1 : 0;
        gainInterp (g, [&] (int gi, double* o) {
            const float* a = feat + ((type * 21 + gi) * nf + ax.i) * 5;
            for (int k = 0; k < 5; ++k)
                o[k] = (1 - ax.t) * a[k] + ax.t * a[5 + k];
        }, u);
    }
    else
    {
        const bool lo = band == 1;
        const float* feat = lo ? lmfFeat : hmfFeat;
        const double lw = lo ? interp (lmfLawText, lmfLawLogW, std::size (lmfLawText), f) : interp (hmfLawText, hmfLawLogW, std::size (hmfLawText), f);
        wref = std::exp (lw);
        const auto ax = lo ? axis (lmfNodeLogW, std::size (lmfNodeLogW), lw) : axis (hmfNodeLogW, std::size (hmfNodeLogW), lw);
        const auto aq = lo ? axis (lmfQv, std::size (lmfQv), dialFromQ (roundTo (q, 0.01))) : axis (hmfQv, std::size (hmfQv), dialFromQ (roundTo (q, 0.01)));
        const int nf = (int) (lo ? std::size (lmfNodeLogW) : std::size (hmfNodeLogW)), nq = (int) std::size (lmfQv);
        gainInterp (g, [&] (int gi, double* o) {
            for (int k = 0; k < 5; ++k)
                o[k] = 0.0;
            for (int df = 0; df < 2; ++df)
                for (int dq = 0; dq < 2; ++dq)
                {
                    const double w = (df ? ax.t : 1 - ax.t) * (dq ? aq.t : 1 - aq.t);
                    const float* a = feat + ((gi * nf + ax.i + df) * nq + aq.i + dq) * 5;
                    for (int k = 0; k < 5; ++k)
                        o[k] += w * a[k];
                }
        }, u);
    }
    n[0] = u[0];
    n[1] = u[1] * wref;
    n[2] = u[2] * wref * wref;
    d[0] = u[3] * wref;
    d[1] = u[4] * wref * wref;
    return true;
}

// At other sample rates the 48 kHz analog-domain response is kept at the band's own frequency: the prototype is
// scaled so that bilinear(K = 2 sr) maps that frequency where bilinear(K = 96000) mapped it at 48 kHz [model]
double rateScale (double wref, double sr) noexcept
{
    if (std::abs (sr - refRate) < 1.0)
        return 1.0;
    const double f0 = refRate / pi * std::atan (wref / (2.0 * refRate)); // the band's frequency in Hz (48 kHz design)
    const double fs = std::min (f0, 0.45 * sr);
    return (2.0 * sr * std::tan (pi * fs / sr)) / (2.0 * refRate * std::tan (pi * fs / refRate)) * (fs / f0);
}
} // namespace

double qFromDial (double v) noexcept
{
    v = std::clamp (v, 0.0, 1.0) * 20.0;
    const int i = std::min ((int) v, 19);
    return qText[i] + (v - i) * (qText[i + 1] - qText[i]);
}

double dialFromQ (double q) noexcept
{
    q = std::clamp (q, (double) qText[20], (double) qText[0]);
    for (int i = 0; i < 20; ++i)
        if (q <= qText[i] && q >= qText[i + 1])
            return (i + (qText[i] - q) / (qText[i] - qText[i + 1])) / 20.0;
    return 1.0;
}

bool bandBiquad (int band, const Settings& s, double sampleRate, Biquad& out)
{
    double n[3], d[2], wref = 0.0;
    if (! bandAnalog (band, s, n, d, wref))
        return false;
    const double a = rateScale (wref, sampleRate);
    n[1] *= a; n[2] *= a * a; d[0] *= a; d[1] *= a * a;
    bilinear (n, d, 2.0 * sampleRate, out);
    return true;
}

bool highPass (const Settings& s, double sampleRate, Biquad& first, Biquad& second)
{
    if (s.hpHz < 16.5)
        return false;
    // 3rd-order Butterworth in the analog domain (a real pole and a Q = 1 pair at the same frequency), measured
    double w = interp (hpText, hpW, std::size (hpText), roundTo (std::clamp (s.hpHz, 17.0, 350.0), 1.0));
    w *= rateScale (w, sampleRate);
    const double K = 2.0 * sampleRate;
    // first order s / (s + w)
    first.set (K, -K, 0.0, K + w, w - K, 0.0);
    const double n[3] = { 1.0, 0.0, 0.0 }, dd[2] = { w, w * w };
    bilinear (n, dd, K, second);
    return true;
}

bool lowPass (const Settings& s, double sampleRate, Biquad& out)
{
    if (s.lpKHz <= 0.0)
        return false;
    const double f = roundTo (std::clamp (s.lpKHz, 3.0, 19.5), 0.1) * 1000.0;
    // the table holds (n2, n1, n0, d1, d0) per displayed frequency
    constexpr auto count = std::size (lpText);
    std::size_t i = 0;
    while (i < count - 2 && f > lpText[i + 1])
        ++i;
    const double t = std::clamp ((f - lpText[i]) / (lpText[i + 1] - lpText[i]), 0.0, 1.0);
    double c[5];
    for (int k = 0; k < 5; ++k)
        c[k] = (1 - t) * lpCoef[i * 5 + k] + t * lpCoef[(i + 1) * 5 + k];
    double n[3] = { c[0], c[1], c[2] }, d[2] = { c[3], c[4] };
    const double a = rateScale (std::sqrt (std::max (d[1], 1.0)), sampleRate);
    n[1] *= a; n[2] *= a * a; d[0] *= a; d[1] *= a * a;
    bilinear (n, d, 2.0 * sampleRate, out);
    return true;
}

double responseDb (const Settings& s, double sampleRate, double hz)
{
    const double w = 2.0 * pi * hz / sampleRate;
    double m = 1.0;
    Biquad a, b;
    if (highPass (s, sampleRate, a, b))
        m *= biquadMagnitude (a, w) * biquadMagnitude (b, w);
    if (lowPass (s, sampleRate, a))
        m *= biquadMagnitude (a, w);
    if (s.eqOn)
        for (int band = 0; band < 4; ++band)
            if (bandBiquad (band, s, sampleRate, a))
                m *= biquadMagnitude (a, w);
    return gainToDb (m);
}

// ---- dynamics ----
namespace
{
#include "core/VoiceMatchDynamics.inc"

double softplus (double o, double k) noexcept { return o > 30.0 * k ? o : k * std::log1p (std::exp (o / k)); }
} // namespace

double compStaticGainDb (const Settings& s, double sineDbfs)
{
    // iterate the feedback loop to its fixed point for a steady sine (peak detector on the output)
    const auto law = compLaw (s);
    double gr = 0.0;
    for (int it = 0; it < 200; ++it)
    {
        const double o = sineDbfs - gr - (s.compThreshold + law.thresholdOffset);
        const double target = law.slope * softplus (o, law.knee);
        gr += 0.3 * (target - gr);
    }
    return law.makeup - gr;
}

void Channel::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    designedOnce = false;
    reset();
}

void Channel::reset() noexcept
{
    hp1.reset(); hp2.reset(); lp.reset();
    for (auto& b : bands)
        b.reset();
    env = gr = gr1 = yl = yr = genv = ggr = 0.0;
    gateOpen = false;
}

void Channel::design (const Settings& s) noexcept
{
    const auto eq = [] (const Settings& a, const Settings& b) {
        return a.hpHz == b.hpHz && a.lpKHz == b.lpKHz && a.lfGain == b.lfGain && a.lfHz == b.lfHz && a.lfBell == b.lfBell
            && a.lmfGain == b.lmfGain && a.lmfKHz == b.lmfKHz && a.lmfQ == b.lmfQ && a.hmfGain == b.hmfGain && a.hmfKHz == b.hmfKHz
            && a.hmfQ == b.hmfQ && a.hfGain == b.hfGain && a.hfKHz == b.hfKHz && a.hfBell == b.hfBell;
    };
    if (designedOnce && eq (s, designed))
        return;
    designed = s;
    designedOnce = true;
    Biquad a, b;
    hpOn = highPass (s, sr, a, b);
    if (hpOn) { hp1.b0 = a.b0; hp1.b1 = a.b1; hp1.b2 = a.b2; hp1.a1 = a.a1; hp1.a2 = a.a2; hp2.b0 = b.b0; hp2.b1 = b.b1; hp2.b2 = b.b2; hp2.a1 = b.a1; hp2.a2 = b.a2; }
    lpOn = lowPass (s, sr, a);
    if (lpOn) { lp.b0 = a.b0; lp.b1 = a.b1; lp.b2 = a.b2; lp.a1 = a.a1; lp.a2 = a.a2; }
    for (int i = 0; i < 4; ++i)
    {
        bandOn[i] = bandBiquad (i, s, sr, a);
        if (bandOn[i]) { bands[i].b0 = a.b0; bands[i].b1 = a.b1; bands[i].b2 = a.b2; bands[i].a1 = a.a1; bands[i].a2 = a.a2; }
    }
}

void Channel::process (float* l, float* r, int n, const Settings& s) noexcept
{
    design (s);
    const double inG = dbToGain (roundTo (s.inputDb, 0.1)), outG = dbToGain (roundTo (s.outputDb, 0.1)) * (s.phase ? -1.0 : 1.0);
    const auto law = compLaw (s);
    const bool compOn = s.dynOn && s.compRatio > 1.0005;
    const double cA = coefMs (s.compFast ? law.attackFastMs : law.attackSlowMs, sr), cR = coefMs (law.releaseMs, sr), cR1 = coefMs (law.release1Ms, sr);
    const double frac = law.release1Fraction;
    const double dA = coefMs (law.detectorAttackMs, sr), dR = coefMs (law.detectorReleaseMs, sr);
    const double makeup = s.dynOn ? dbToGain (law.makeup) : 1.0;
    const auto gl = gateLaw (s);
    const bool gateOn = s.dynOn && gl.range > 0.05;
    const double gA = coefMs (gl.attackMs, sr), gR = coefMs (gl.releaseMs, sr), gD = coefMs (gl.detectorReleaseMs, sr);
    float maxComp = 0.0f, maxGate = 0.0f;

    auto filters = [this] (double& a, double& b) {
        if (hpOn)
        {
            a = hp2.process (hp1.process ((float) a, 0), 0);
            b = hp2.process (hp1.process ((float) b, 1), 1);
        }
        if (lpOn)
        {
            a = lp.process ((float) a, 0);
            b = lp.process ((float) b, 1);
        }
    };
    auto eq = [this] (double& a, double& b) {
        for (int i = 0; i < 4; ++i)
            if (bandOn[i])
            {
                a = bands[i].process ((float) a, 0);
                b = bands[i].process ((float) b, 1);
            }
    };

    for (int i = 0; i < n; ++i)
    {
        double a = l[i] * inG, b = r[i] * inG;
        if (s.filtersBeforeDynamics || s.dynamicsAfterEq)
            filters (a, b);
        if (s.dynamicsAfterEq && s.eqOn)
            eq (a, b);

        if (gateOn)
        {
            const double lev = std::max (std::abs (a), std::abs (b));
            genv = lev > genv ? lev : genv + gD * (lev - genv);
            const double levDb = gainToDb (genv);
            double target;
            if (gl.gate)
            {
                if (levDb >= gl.threshold) gateOpen = true;
                else if (levDb < gl.threshold - gl.hysteresis) gateOpen = false;
                target = gateOpen ? 0.0 : gl.range;
            }
            else
            {
                const double u = gl.threshold - levDb;
                target = std::min (gl.range, gl.slope * (u > 30.0 * gl.knee ? u : gl.knee * std::log1p (std::exp (u / gl.knee))));
            }
            ggr += (target < ggr ? gA : gR) * (target - ggr); // less reduction = opening (attack), more = closing (release)
            const double g = dbToGain (-ggr);
            a *= g;
            b *= g;
            maxGate = std::max (maxGate, (float) ggr);
        }
        if (compOn)
        {
            // feedback compressor: the detector hears the compressor's own output (the previous sample)
            const double sc = std::max (std::abs (yl), std::abs (yr));
            env += (sc > env ? dA : dR) * (sc - env);
            const double o = gainToDb (env) - (s.compThreshold + law.thresholdOffset);
            const double target = law.slope * softplus (o, law.knee);
            gr1 += (target > gr1 ? cA : cR1) * (target - gr1);
            gr += (target > gr ? cA : cR) * (target - gr);
            const double g = dbToGain (-(frac * gr1 + (1.0 - frac) * gr));
            yl = a * g;
            yr = b * g;
            a = yl;
            b = yr;
            maxComp = std::max (maxComp, (float) (frac * gr1 + (1.0 - frac) * gr));
        }
        a *= makeup;
        b *= makeup;

        if (! s.dynamicsAfterEq)
        {
            if (! s.filtersBeforeDynamics)
                filters (a, b);
            if (s.eqOn)
                eq (a, b);
        }
        l[i] = (float) (a * outG);
        r[i] = (float) (b * outG);
    }
    lastComp = maxComp;
    lastGate = maxGate;
}
} // namespace echannel

// ================================================================== split-band de-esser
// [EXEC] Fitted on the official DeEsser trial (her state and sweeps of threshold -40 .. -5 dB and frequency 2.5 .. 14.4
// kHz; 6 kHz sine stairs, pink noise, her takes; tools/fxmeasure/py/vm123/decfg.py): the side chain is a 4th-order
// high-pass (two Butterworth sections) or a band-pass at the dial's frequency, an energy (power) detector with a
// 0.3 ms attack and a 12 ms release, a 7.1 : 1 soft-knee (1.26 dB) gain computer 0.6 dB below the dial; Split
// attenuates only what lies above a first-order crossover at 0.33 x the dial's frequency (so the attenuation of a
// sibilant floors near -11 dB: no range control), Wideband the whole signal. Her state: null -38 / -50 / -44 dB.
namespace deesser
{
namespace
{
constexpr double thrOffset = -0.6009, thrSlope = -0.0097, ratio = 7.1171, knee = 1.2597, attackMs = 0.3027, releaseMs = 12.1503;
constexpr double splitK = 0.3306, splitExp = 0.0491, scK = 1.0120, scExp = -0.0353, bpQ = 1.2988, bpOffset = 3.9011;

void rbj (Biquad& b, int type, double f, double q, double sr) noexcept
{
    const double w = 2.0 * pi * std::min (f, sr * 0.49) / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q);
    if (type == 0) b.set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al); // high-pass
    else b.set (al, 0.0, -al, 1 + al, -2 * cw, 1 - al);                                     // band-pass (0 dB peak)
}
} // namespace

void DeEsser::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    designedHz = -1.0;
    reset();
}

void DeEsser::reset() noexcept
{
    sc1.reset();
    sc2.reset();
    lpX[0] = lpX[1] = lpY[0] = lpY[1] = 0.0;
    env = gr = 0.0;
}

void DeEsser::design (const Settings& s) noexcept
{
    const double f = std::clamp (s.hz, 2000.0, 16000.0);
    if (f == designedHz && s.bandPass == designedBp)
        return;
    designedHz = f;
    designedBp = s.bandPass;
    const double rel = f / 5506.0;
    const double fsc = f * scK * std::pow (rel, scExp);
    rbj (sc1, s.bandPass ? 1 : 0, fsc, s.bandPass ? bpQ : 0.70710678, sr);
    sc2.b0 = sc1.b0; sc2.b1 = sc1.b1; sc2.b2 = sc1.b2; sc2.a1 = sc1.a1; sc2.a2 = sc1.a2;
    const double k = std::tan (pi * std::min (f * splitK * std::pow (rel, splitExp), 0.45 * sr) / sr);
    lpB = k / (1.0 + k);
    lpA = (k - 1.0) / (k + 1.0);
}

void DeEsser::process (float* l, float* r, int n, const Settings& s) noexcept
{
    design (s);
    const double T = std::clamp (s.thresholdDb, -80.0, 0.0);
    const double thr = T + thrOffset + thrSlope * (T + 24.2) + (s.bandPass ? bpOffset : 0.0);
    const double aA = coefMs (attackMs, sr), aR = coefMs (releaseMs, sr), out = dbToGain (s.outputDb);
    double maxGr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = sc2.process (sc1.process (l[i], 0), 0), b = sc2.process (sc1.process (r[i], 1), 1);
        const double p = 0.5 * (a * a + b * b);
        env += (p > env ? aA : aR) * (p - env);
        const double o = 10.0 * std::log10 (std::max (env, 1e-24)) - thr;
        gr = (1.0 - 1.0 / ratio) * (o > 30.0 * knee ? o : knee * std::log1p (std::exp (o / knee)));
        const double g = dbToGain (-gr);
        maxGr = std::max (maxGr, gr);
        if (s.listen)
        {
            l[i] = (float) (a * out);
            r[i] = (float) (b * out);
            continue;
        }
        double x[2] = { l[i], r[i] };
        for (int c = 0; c < 2; ++c)
        {
            if (s.wideband)
                x[c] *= g;
            else
            {
                const double lo = lpB * (x[c] + lpX[c]) - lpA * lpY[c];
                lpX[c] = x[c];
                lpY[c] = lo;
                x[c] = lo + (x[c] - lo) * g;
            }
        }
        l[i] = (float) (x[0] * out);
        r[i] = (float) (x[1] * out);
    }
    last = (float) maxGr;
}
} // namespace deesser

// ================================================================== electro-optical leveler
// [EXEC] Fitted on the official CLA-2A trial (tools/fxmeasure/py/vm123: sine stairs at 100 Hz … 10 kHz, 1 kHz bursts,
// a 1 kHz tone with a quiet pink probe at five levels, pink noise, her takes; dynfit "cla5"). A feedback leveler: the
// side chain hears the cell's output through a peaking emphasis (+9.4 dB at 9 kHz); the cell's conductance C follows
// the light — C = ((level - Yt) / Y0)^gamma above a threshold — with an attack in the log domain that speeds up with C,
// a hyperbolic release (dC/dt = -a C^n: about half the reduction back in 60 ms, the rest over seconds) and a slow
// second conductance; the audio sees g = 1 / (1 + C) with a high-frequency bypass that opens under reduction (the
// measured tilt: up to +7 dB at 20 kHz), then the gain dial's make-up, a little 2nd / 3rd harmonic, and the unit's
// idle response (a 4 Hz high-pass and a 0.7 dB dip around 12 kHz, measured).
namespace opto
{
namespace
{
#include "core/VoiceMatchOpto.inc"
} // namespace

void Leveler::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    reset();
}

void Leveler::reset() noexcept
{
    cf = cs = el = mem = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        yc[c] = fx1[c] = fy1[c] = 0.0;
        for (int k = 0; k < 4; ++k)
            tx[c][k] = ty[c][k] = 0.0;
    }
    sc.reset();
}

void Leveler::process (float* l, float* r, int n, const Settings& s) noexcept
{
    const double dt = 1.0 / sr;
    if (scDesignedRate != sr)
    {
        scDesignedRate = sr;
        const double A = std::pow (10.0, scDb / 40.0), w = 2.0 * pi * std::min (scHz, 0.45 * sr) / sr, al = std::sin (w) / (2.0 * scQ), cw = std::cos (w);
        sc.set (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    const double shift = sensitivityDb (s.peakReduction, s.hiFreq) + (s.limit ? limitShiftDb : 0.0);
    const double Y0 = dbToGain (y0Db + shift), Yt = dbToGain (ytDb + shift), gam = s.limit ? limitGamma : gamma;
    const double mk = dbToGain (makeupDb (s.gain) + idleMakeupDb);
    const double cA = 1.0 - std::exp (-dt * 1000.0 / attackSlowMs), mUp = 1.0 - std::exp (-dt * 1000.0 / memUpMs), mDn = 1.0 - std::exp (-dt * 1000.0 / memDownMs);
    const double K = 2.0 * sr;
    // the idle response measured at 48 kHz; other rates take its bilinear analog prototype (approximate)
    const double* tb = idleB;
    const double* ta = idleA;
    double maxGr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double e0 = sc.process ((float) yc[0], 0), e1 = sc.process ((float) yc[1], 1);
        el = std::max (std::abs (e0), std::abs (e1)); // the light follows the drive within the sample (fitted)
        const double ct = (el > Yt ? std::pow ((el - Yt) / Y0, gam) : 0.0) * (1.0 + memGain * mem);
        const double ctf = (1.0 - slowShare) * ct, cts = slowShare * ct;
        if (ctf > cf)
        {
            double cd = std::log1p (cf);
            cd += (std::log1p (ctf) - cd) * (1.0 - std::exp (-dt * 1000.0 * std::pow (1.0 + cf, attackBeta) / attackMs));
            cf = std::expm1 (cd);
        }
        else
            cf = std::max (ctf, cf - dt * releaseA * std::pow (cf, releaseN));
        if (cts > cs)
            cs += (cts - cs) * cA;
        else
            cs = std::max (cts, cs - dt * releaseA2 * std::pow (cs, releaseN2));
        const double g = 1.0 / (1.0 + cf + cs);
        mem += ((1.0 - g) > mem ? mUp : mDn) * ((1.0 - g) - mem);
        const double gp = g + (1.0 - g) * (tiltC0 - tiltC1 * g);
        const double wz = 2.0 * pi * tiltHz * (1.0 + g);
        const double a0 = 1.0 + K * gp / wz, b0 = g * (1.0 + K / wz) / a0, b1 = g * (1.0 - K / wz) / a0, a1 = (1.0 - K * gp / wz) / a0;
        float* ch[2] = { &l[i], &r[i] };
        for (int c = 0; c < 2; ++c)
        {
            const double x = *ch[c];
            const double y = b0 * x + b1 * fx1[c] - a1 * fy1[c];
            fx1[c] = x;
            fy1[c] = y;
            yc[c] = y;
            double o = y * mk;
            o = o + h2 * o * o + h3 * o * o * o;
            for (int j = 3; j > 0; --j) { tx[c][j] = tx[c][j - 1]; ty[c][j] = ty[c][j - 1]; }
            tx[c][0] = o;
            const double t = tb[0] * tx[c][0] + tb[1] * tx[c][1] + tb[2] * tx[c][2] + tb[3] * tx[c][3] - ta[1] * ty[c][1] - ta[2] * ty[c][2] - ta[3] * ty[c][3];
            ty[c][0] = t;
            *ch[c] = (float) t;
        }
        maxGr = std::max (maxGr, -gainToDb (g));
    }
    last = (float) maxGr;
}
} // namespace opto

// ================================================================== program-dependent peak compressor
// [EXEC] Fitted on the official Renaissance Compressor trial at her state (-15 dB, 2.95:1, 0.5 ms, 160 ms, ARC, Warm,
// Electro) on 1 kHz stairs, tone bursts, pink noise and her takes (dynfit "rc2"): a 64-sample (1.33 ms at 48 kHz)
// delay of the audio against the detector, a linked peak detector (9 ms release), a 21.7 dB-wide soft knee 2.8 dB
// under the threshold, a fast gain path (attack 2.1 x the knob, release 25 ms) and a slow "program" path that
// charges in ~220 ms and releases at a constant rate in linear gain (3.8 /s x 160 ms / the release knob) — so short
// peaks recover in milliseconds and long phrases slowly (its ARC / Electro behaviour at her state). Her takes:
// null -26 dB (stairs and bursts -40 … -50 dB). [model] The knob laws away from her state, ARC / manual, Opto and
// Smooth are not separated yet (REA_rodada123 §3).
namespace progcomp
{
namespace
{
constexpr double thrOffset = -2.8056, kneeDb = 21.7431, ratioScale = 1.3191, attackScale = 2.1395, fastReleaseMs = 24.5151;
constexpr double slowChargeMs = 222.5718, slowRate = 3.8413, slowShape = -1.1528, detectorReleaseMs = 9.2761;
} // namespace

void Compressor::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    delayLen = std::clamp ((int) std::lround (lookahead * sr / 48000.0), 1, (int) dl.size() - 1);
    reset();
}

void Compressor::reset() noexcept
{
    dl.fill (0.0f);
    dr.fill (0.0f);
    pos = 0;
    env = grf = grs = 0.0;
}

void Compressor::process (float* l, float* r, int n, const Settings& s) noexcept
{
    const double T = s.thresholdDb + thrOffset, R = 1.0 + (std::max (1.0, s.ratio) - 1.0) * ratioScale, W = kneeDb;
    const double aA = coefMs (std::max (0.01, s.attackMs) * attackScale, sr), fR = coefMs (fastReleaseMs, sr), sC = coefMs (slowChargeMs, sr);
    const double dR = coefMs (detectorReleaseMs, sr), relK = 160.0 / std::max (1.0, s.releaseMs), dt = 1.0 / sr;
    const double makeup = s.gainDb, trim = dbToGain (s.trimDb);
    const int N = (int) dl.size();
    double maxGr = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
        env = a > env ? a : env + dR * (a - env);
        const double x = gainToDb (env), o = x - T;
        double y;
        if (2.0 * o < -W) y = x;
        else if (2.0 * std::abs (o) <= W) y = x + (1.0 / R - 1.0) * (o + W / 2.0) * (o + W / 2.0) / (2.0 * W);
        else y = T + o / R;
        const double target = std::max (0.0, x - y);
        grf += (target > grf ? aA : fR) * (target - grf);
        if (grf > grs)
            grs += sC * (grf - grs);
        else
        {
            double gs = dbToGain (-grs);
            gs += slowRate * relK * (1.0 + slowShape * (1.0 - gs)) * dt;
            grs = gs >= 1.0 ? 0.0 : -gainToDb (gs);
            grs = std::max (grs, grf);
        }
        const double gr = std::max (grf, grs);
        maxGr = std::max (maxGr, gr);
        const double g = (dbToGain (makeup - gr) * s.mix + (1.0 - s.mix)) * trim;
        dl[(std::size_t) pos] = l[i];
        dr[(std::size_t) pos] = r[i];
        int rd = pos - delayLen;
        if (rd < 0) rd += N;
        l[i] = (float) (dl[(std::size_t) rd] * g);
        r[i] = (float) (dr[(std::size_t) rd] * g);
        if (++pos == N) pos = 0;
    }
    last = (float) maxGr;
}
} // namespace progcomp
} // namespace lynx::vmatch
