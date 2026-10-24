#include "core/VoxStrip.h"

#include <algorithm>
#include <cmath>

namespace lynx::voxstrip
{
namespace
{
constexpr double pi = 3.141592653589793;
double logMap (float v, double lo, double hi) noexcept { return lo * std::pow (hi / lo, std::clamp ((double) v, 0.0, 1.0)); }
float logValue (double x, double lo, double hi) noexcept
{
    return (float) std::clamp (std::log (std::max (x, 1e-9) / lo) / std::log (hi / lo), 0.0, 1.0);
}
double lin (float v, double lo, double hi) noexcept { return lo + (hi - lo) * std::clamp ((double) v, 0.0, 1.0); }
float linValue (double x, double lo, double hi) noexcept { return (float) std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); }
double coef (double ms, double sr) noexcept { return 1.0 - std::exp (-1.0 / (std::max (ms, 1e-3) * 1e-3 * sr)); }
double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-30)); }

// [EXEC] the notch's Q (analog prototype) fitted at 1 kHz for every 2 dB of gain; the same at 200 Hz … 10 kHz
constexpr std::array<double, 18> notchCutQ { // −36, −34 … −2 dB
    8.477, 8.984, 9.482, 9.968, 10.458, 10.885, 11.284, 11.605, 11.839, 11.958, 11.934, 11.721, 11.284, 10.564,
    9.482, 8.000, 6.010, 3.419 };
constexpr std::array<double, 6> notchBoostQ { 16.734, 14.901, 13.295, 11.839, 10.564, 9.407 }; // +2 … +12 dB

// [EXEC] the de-ploser's detector: low-pass corner (× the frequency) and passband lift (dB) at 50 / 100 / 250 Hz
void dePloserDetector (double hz, double& cornerScale, double& liftDb) noexcept
{
    const double x = std::log2 (std::max (hz, 1.0) / 100.0); // 50 Hz: −1, 250 Hz: +1.32
    if (x < 0.0)
    {
        cornerScale = 0.84 + 0.06 * x;
        liftDb = 5.17 - 3.11 * x;
    }
    else
    {
        const double t = x / std::log2 (2.5);
        cornerScale = 0.84 + 0.08 * t;
        liftDb = 5.17 - 2.89 * t;
    }
    cornerScale = std::clamp (cornerScale, 0.6, 1.0);
}
} // namespace

double dsFreqHz (float v) noexcept { return logMap (v, 1000.0, 20000.0); }
double dpFreqHz (float v) noexcept { return logMap (v, 20.0, 250.0); }
double thresholdDb (float v) noexcept { return lin (v, -48.0, 0.0); }
double hpfHz (float v) noexcept { return logMap (v, 30.0, 300.0); }
double notchHz (float v) noexcept { return logMap (v, 200.0, 10000.0); }
double notchGainDb (float v) noexcept { return lin (v, -36.0, 12.0); }
double highHz (float v) noexcept { return logMap (v, 1000.0, 20000.0); }
double highGainDb (float v) noexcept { return lin (v, -12.0, 12.0); }
double expThresholdDb (float v) noexcept { return lin (v, -96.0, 0.0); }
double attackMs (float v) noexcept { return lin (v, 0.1, 100.0); }
double releaseMs (float v) noexcept { return lin (v, 0.5, 2000.0); }
double ratio (float v) noexcept { return 1.0 + 49.0 * (std::pow (250.0, std::clamp ((double) v, 0.0, 1.0)) - 1.0) / 249.0; }
double makeupDb (float v) noexcept { return lin (v, 0.0, 48.0); }
double gainDb (float v) noexcept { return lin (v, -36.0, 12.0); }
int orderOf (float v) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * 23.0f), 0, 23); }

float dsFreqValue (double hz) noexcept { return logValue (hz, 1000.0, 20000.0); }
float dpFreqValue (double hz) noexcept { return logValue (hz, 20.0, 250.0); }
float thresholdValue (double db) noexcept { return linValue (db, -48.0, 0.0); }
float hpfValue (double hz) noexcept { return logValue (hz, 30.0, 300.0); }
float notchValue (double hz) noexcept { return logValue (hz, 200.0, 10000.0); }
float notchGainValue (double db) noexcept { return linValue (db, -36.0, 12.0); }
float highValue (double hz) noexcept { return logValue (hz, 1000.0, 20000.0); }
float highGainValue (double db) noexcept { return linValue (db, -12.0, 12.0); }
float expThresholdValue (double db) noexcept { return linValue (db, -96.0, 0.0); }
float attackValue (double ms) noexcept { return linValue (ms, 0.1, 100.0); }
float releaseValue (double ms) noexcept { return linValue (ms, 0.5, 2000.0); }
float ratioValue (double r) noexcept
{
    return (float) std::clamp (std::log (1.0 + (std::max (r, 1.0) - 1.0) * 249.0 / 49.0) / std::log (250.0), 0.0, 1.0);
}
float makeupValue (double db) noexcept { return linValue (db, 0.0, 48.0); }
float gainValue (double db) noexcept { return linValue (db, -36.0, 12.0); }

std::array<int, 4> order (int index) noexcept
{
    std::array<int, 4> m { 0, 1, 2, 3 };
    index = std::clamp (index, 0, 23);
    for (int k = 0; k < index; ++k)
        std::next_permutation (m.begin(), m.end());
    return m;
}

const char* orderName (int index) noexcept
{
    static const char* names[24] = { "DsDpEqCo", "DsDpCoEq", "DsEqDpCo", "DsEqCoDp", "DsCoDpEq", "DsCoEqDp",
                                     "DpDsEqCo", "DpDsCoEq", "DpEqDsCo", "DpEqCoDs", "DpCoDsEq", "DpCoEqDs",
                                     "EqDsDpCo", "EqDsCoDp", "EqDpDsCo", "EqDpCoDs", "EqCoDsDp", "EqCoDpDs",
                                     "CoDsDpEq", "CoDsEqDp", "CoDpDsEq", "CoDpEqDs", "CoEqDsDp", "CoEqDpDs" };
    return names[std::clamp (index, 0, 23)];
}

double notchQ (double g) noexcept
{
    if (g >= 0.0)
    {
        const double x = std::clamp (g / 2.0 - 1.0, 0.0, 5.0);
        const int i = std::min ((int) x, 4);
        return notchBoostQ[(std::size_t) i] + (notchBoostQ[(std::size_t) i + 1] - notchBoostQ[(std::size_t) i]) * (x - i);
    }
    const double x = std::clamp ((g + 36.0) / 2.0, 0.0, 17.0);
    const int i = std::min ((int) x, 16);
    return notchCutQ[(std::size_t) i] + (notchCutQ[(std::size_t) i + 1] - notchCutQ[(std::size_t) i]) * (x - i);
}

double highQ (double g) noexcept { return 0.852 * std::exp (-0.0561 * std::abs (g)); }

// Magnitude-matched second-order peaking filter (M. Vicanek, "Matched Second Order Digital Filters", 2016: poles by
// the exact z = e^(s T) mapping, zeros chosen so the magnitude equals the analog one at DC, the centre and Nyquist)
void matchedPeak (Biquad& b, double sampleRate, double hz, double q, double db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), G = A * A;
    const double w0 = 2.0 * pi * std::min (hz, sampleRate * 0.499) / sampleRate;
    const double zeta = 1.0 / (2.0 * A * q); // the poles' damping: the prototype's denominator s² + s / (A Q) + 1
    double a1;
    if (zeta <= 1.0)
        a1 = -2.0 * std::exp (-zeta * w0) * std::cos (std::sqrt (1.0 - zeta * zeta) * w0);
    else
        a1 = -2.0 * std::exp (-zeta * w0) * std::cosh (std::sqrt (zeta * zeta - 1.0) * w0);
    const double a2 = std::exp (-2.0 * zeta * w0);
    const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), A2 = -4.0 * a2;
    const double p1 = std::sin (w0 / 2.0) * std::sin (w0 / 2.0), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
    const double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * G * G;
    const double R2 = (-A0 + A1 + 4.0 * (p0 - p1) * A2) * G * G;
    const double B0 = A0, B2 = (R1 - R2 * p1 - B0) / (4.0 * p1 * p1), B1 = R2 + B0 + 4.0 * (p1 - p0) * B2;
    const double sB0 = std::sqrt (B0), sB1 = std::sqrt (std::max (B1, 0.0));
    const double W = 0.5 * (sB0 + sB1);
    const double b0 = 0.5 * (W + std::sqrt (std::max (W * W + B2, 0.0)));
    const double b1 = 0.5 * (sB0 - sB1);
    const double b2 = -B2 / (4.0 * b0);
    b.set (b0, b1, b2, 1.0, a1, a2);
}

void Engine::ThirdOrder::design (double sr, double hz, bool high) noexcept
{
    const double w = std::tan (pi * std::min (hz, sr * 0.45) / sr);
    a1 = (w - 1.0) / (w + 1.0);
    if (high) { b0 = 1.0 / (1.0 + w); b1 = -b0; }
    else { b0 = w / (1.0 + w); b1 = b0; }
    bq.filter (sr, high ? 1 : 0, hz, 1.0);
}

float Engine::ThirdOrder::process (float x, int c) noexcept
{
    const double y = b0 * x + b1 * x1[c] - a1 * y1[c];
    x1[c] = x;
    y1[c] = y;
    return bq.process ((float) y, c);
}

void Engine::ThirdOrder::reset() noexcept
{
    x1[0] = x1[1] = y1[0] = y1[1] = 0.0;
    bq.reset();
}

void Engine::Delay::process (float& a, float& b) noexcept
{
    if (len <= 0)
        return;
    const auto oa = l[(std::size_t) pos], ob = r[(std::size_t) pos];
    l[(std::size_t) pos] = a;
    r[(std::size_t) pos] = b;
    pos = (pos + 1) % len;
    a = oa;
    b = ob;
}

void Engine::prepare (double sampleRate) noexcept
{
    sr = sampleRate;
    const auto keep = s;
    s = Settings {};
    s.dsHz = -1.0; // force a full design
    set (keep);
    reset();
}

void Engine::reset() noexcept
{
    dsBand.reset();
    dsOut.reset();
    dsOutHigh.reset();
    dpOutLow.reset();
    dpBand.reset();
    dpDetect.reset();
    dsDelay.reset();
    dpDelay.reset();
    hpf.reset();
    notch.reset();
    high.reset();
    dsEb = dsEf = dpEb = dpEf = 0.0;
    cEnv = xEnv = 0.0;
    dsGrDb = dpGrDb = compGrDb = 0.0;
}

void Engine::set (const Settings& n) noexcept
{
    if (n == s)
        return;
    const bool design = n.dsHz != s.dsHz || n.dpHz != s.dpHz || n.hpfHz != s.hpfHz || n.notchHz != s.notchHz
                        || n.notchDb != s.notchDb || n.highHz != s.highHz || n.highDb != s.highDb;
    const bool delays = n.dsLookahead != s.dsLookahead || n.dpLookahead != s.dpLookahead;
    s = n;
    mods = order (s.order);
    if (design)
    {
        dsBand.design (sr, s.dsHz, true);
        dsOut.design (sr, s.dsHz, false);
        dsOutHigh.design (sr, s.dsHz, true);
        double k = 0.84, liftDb = 5.17;
        dePloserDetector (s.dpHz, k, liftDb);
        dpDetect.design (sr, s.dpHz * k, false);
        dpLift = dbToGain (liftDb);
        dpBand.design (sr, s.dpHz, true);
        dpOutLow.design (sr, s.dpHz, false);
        hpf.filter (sr, 1, s.hpfHz, 1.0);
        matchedPeak (notch, sr, s.notchHz, notchQ (s.notchDb), s.notchDb);
        matchedPeak (high, sr, s.highHz, highQ (s.highDb), s.highDb);
    }
    if (delays || dsDelay.len == 0)
    {
        dsDelay.len = s.dsLookahead ? std::min ((int) std::lround (sr * 0.001), 2047) : 0;
        dpDelay.len = s.dpLookahead ? std::min ((int) std::lround (sr * 0.010), 2047) : 0;
        dsDelay.reset();
        dpDelay.reset();
    }
    dsAtt = coef (1.5, sr);
    dsRel = coef (40.0, sr);
    dpAtt = coef (3.0, sr);
    dpRel = coef (40.0, sr);
    cAtt = coef (s.attackMs, sr);
    cRel = coef (s.releaseMs, sr);
    xAtt = coef (0.1, sr);
    xRel = coef (25.0, sr);
    makeup = dbToGain (s.makeupDb);
    inGain = dbToGain (s.inputDb);
    outGain = dbToGain (s.outputDb);
}

int Engine::latencySamples() const noexcept { return dsDelay.len + dpDelay.len; }

void Engine::deEss (float& a, float& b) noexcept
{
    const float ba = dsBand.process (a, 0), bb = dsBand.process (b, 1);
    const double vb = std::max (std::abs (ba), std::abs (bb)), vf = std::max (std::abs (a), std::abs (b));
    dsEb += (vb > dsEb ? dsAtt : dsRel) * (vb - dsEb);
    dsEf += (vf > dsEf ? dsAtt : dsRel) * (vf - dsEf);
    const double rel = gainToDb ((dsEb + 1e-20) / (dsEf + 1e-20));
    dsGrDb = (2.0 / 3.0) * std::max (0.0, rel - s.dsThresholdDb);
    dsDelay.process (a, b); // look-ahead: the audio comes 1 ms after the detector
    // [EXEC] split: a 3rd-order Butterworth crossover at the frequency (low-pass + high-pass, an all-pass sum), the
    // high band turned down: out = LP + g · HP (fitted exactly on steady tones at 7, 10, 15 kHz and pink noise)
    const float la = dsOut.process (a, 0), lb = dsOut.process (b, 1);
    const float ha = dsOutHigh.process (a, 0), hb = dsOutHigh.process (b, 1);
    if (s.dsListen) { a = ha; b = hb; return; } // audition: the band the de-esser acts on
    const auto g = (float) dbToGain (-dsGrDb);
    if (s.dsSplit) { a = la + g * ha; b = lb + g * hb; }
    else { a *= g; b *= g; }
}

void Engine::dePlos (float& a, float& b) noexcept
{
    const float qa = dpDetect.process (a, 0), qb = dpDetect.process (b, 1);
    const double vb = std::max (std::abs (qa), std::abs (qb)) * dpLift, vf = std::max (std::abs (a), std::abs (b));
    dpEb += (vb > dpEb ? dpAtt : dpRel) * (vb - dpEb);
    dpEf += (vf > dpEf ? dpAtt : dpRel) * (vf - dpEf);
    const double rel = gainToDb ((dpEb + 1e-20) / (dpEf + 1e-20));
    dpGrDb = 0.75 * std::max (0.0, rel - s.dpThresholdDb);
    dpDelay.process (a, b);
    // [EXEC] split: the same crossover, the low band turned down: out = HP + g · LP
    const float ha = dpBand.process (a, 0), hb = dpBand.process (b, 1);
    const float la = dpOutLow.process (a, 0), lb = dpOutLow.process (b, 1);
    if (s.dpListen) { a = la; b = lb; return; }
    const auto g = (float) dbToGain (-dpGrDb);
    if (s.dpSplit) { a = ha + g * la; b = hb + g * lb; }
    else { a *= g; b *= g; }
}

void Engine::equalise (float& a, float& b) noexcept
{
    if (s.hpfOn) { a = hpf.process (a, 0); b = hpf.process (b, 1); }
    if (s.notchOn && s.notchDb != 0.0) { a = notch.process (a, 0); b = notch.process (b, 1); }
    if (s.highOn && s.highDb != 0.0) { a = high.process (a, 0); b = high.process (b, 1); }
}

void Engine::compand (float& a, float& b) noexcept
{
    // expander (fixed 1:6 below its threshold + 1.16 dB; its own fast detector)
    const double vx = std::max (std::abs (a), std::abs (b));
    xEnv += (vx > xEnv ? xAtt : xRel) * (vx - xEnv);
    double gDb = 0.0;
    if (s.expThresholdDb > -95.99)
    {
        const double o = gainToDb (xEnv + 1e-20) - (s.expThresholdDb + 1.16);
        if (o < 0.0)
            gDb = std::max (5.0 * o, -200.0);
    }
    const auto ge = (float) dbToGain (gDb);
    a *= ge;
    b *= ge;
    // compressor
    const double v = std::max (std::abs (a), std::abs (b));
    cEnv += (v > cEnv ? cAtt : cRel) * (v - cEnv);
    const double o = gainToDb (cEnv + 1e-20) - s.compThresholdDb, k = 1.0 / std::max (s.ratio, 1.0) - 1.0;
    double gr = 0.0;
    if (s.softKnee)
    {
        constexpr double W = 11.75;
        if (2.0 * o > W) gr = o * k;
        else if (2.0 * o >= -W) gr = k * (o + W / 2.0) * (o + W / 2.0) / (2.0 * W);
    }
    else if (o > 0.0)
        gr = o * k;
    compGrDb = -gr;
    const auto g = (float) (dbToGain (gr) * makeup);
    a *= g;
    b *= g;
    if (s.drive)
    {
        auto clip = [] (float u) noexcept {
            if (u >= 2.0f) return 2.0f;
            if (u <= -2.0f) return -2.0f;
            return 1.5f * u - u * u * u * 0.125f;
        };
        a = clip (a);
        b = clip (b);
    }
}

void Engine::process (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float a = left[i] * (float) inGain, b = right[i] * (float) inGain;
        for (const int m : mods)
        {
            switch (m)
            {
                // [EXEC] the look-ahead delays the audio even while its module is off (and the latency is reported)
                case deEsser: if (s.dsOn) deEss (a, b); else { dsGrDb = 0.0; dsDelay.process (a, b); } break;
                case dePloser: if (s.dpOn) dePlos (a, b); else { dpGrDb = 0.0; dpDelay.process (a, b); } break;
                case eq: if (s.eqOn) equalise (a, b); break;
                default: if (s.compOn) compand (a, b); else compGrDb = 0.0; break;
            }
        }
        left[i] = a * (float) outGain;
        right[i] = b * (float) outGain;
    }
}
} // namespace lynx::voxstrip
