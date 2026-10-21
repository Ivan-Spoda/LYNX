#include "core/VoiceTools.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/LrCrossover.h"

// REA_rodada119. Literature behind the two engines:
//  - Linkwitz-Riley crossovers and the all-pass compensation that keeps a band split phase-coherent:
//    S. Linkwitz, "Active Crossover Networks for Noncoincident Drivers", JAES 24(1), 1976; the tree / all-pass
//    arrangement as in U. Zölzer (ed.), "DAFX", 2nd ed., ch. 2 (filters) and ch. 4 (dynamics);
//  - multiband downward expansion as a broadband noise reducer (the classic "noise gate per band" scheme of tape
//    noise reduction units and single-ended hiss reducers), with a soft-knee gain computer and log-domain smoothing:
//    D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
//    JAES 60(6), 2012 (the same computer, below the threshold);
//  - delay-line pitch shifting with two crossfaded read taps (the "rotating tape head" / Doppler shifter):
//    DAFX ch. 6 (time-segment processing) and J. Dattorro, "Effect Design, Part 2: Delay-Line Modulation and
//    Chorus", JAES 45(10), 1997; doubling by short, slightly detuned delays: the same paper (doubler / chorus);
//  - shelving filters: R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients".

namespace lynx::voicetools
{
namespace
{
constexpr double pi = 3.141592653589793;
inline double clamp01 (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f); }
inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline bool on (float v) noexcept { return v >= 0.5f; }
inline double coef (double ms, double sr) noexcept { return ms <= 0.0 ? 0.0 : std::exp (-1000.0 / (ms * sr)); }

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof b, f, v);
    return b;
}
std::string hzText (double f) { return f >= 1000.0 ? fmt ("%.2f kHz", f / 1000.0) : fmt ("%.0f Hz", f); }
} // namespace

// =====================================================================================================================
// Quell Denoiser
// =====================================================================================================================
namespace quell
{
double outputDb (float v) noexcept { return -12.0 + 24.0 * clamp01 (v); }

const Tuning& defaultTuning() noexcept
{
    // REA_rodada119 §3: fitted to the reference's band gains on three vocal takes (raw and −12 dB) at Amount 23, 40
    // and 70 % plus sine stairs and tone bursts (tmp/w119/py/qfit.py)
    static const Tuning t = []
    {
        Tuning u;
        const double thr0[bands] { -34.00, -52.00, -43.50, -47.50, -51.00, -65.00 };
        const double thr1[bands] { -21.00, -42.50, -39.00, -40.00, -42.00, -48.50 };
        const double r0[bands] { 8.00, 27.00, 29.00, 26.00, 29.00, 13.50 };
        const double r1[bands] { 25.00, 26.00, 27.50, 26.00, 26.00, 17.50 };
        const double c0[bands] { 0.00, 0.00, 0.00, 0.50, 0.50, 2.30 };
        const double c1[bands] { 1.50, -0.75, 0.25, 0.00, 0.50, 3.20 };
        for (int b = 0; b < bands; ++b)
        {
            u.thr0[b] = thr0[b];
            u.thr1[b] = thr1[b];
            u.range0[b] = r0[b];
            u.range1[b] = r1[b];
            u.floorCut0[b] = c0[b];
            u.floorCut1[b] = c1[b];
        }
        u.slope = 1.40;
        u.knee = 19.00;
        u.detAttackMs = 2.00;
        u.detReleaseMs = 5.00;
        u.openMs = 2.00;
        u.closeMs = 45.00;
        return u;
    }();
    return t;
}

double reductionDb (const Tuning& t, int b, double levelDb, double amount) noexcept
{
    b = std::clamp (b, 0, bands - 1);
    amount = std::clamp (amount, 0.0, 1.0);
    const auto thr = t.thr0[b] + (t.thr1[b] - t.thr0[b]) * amount;
    const auto range = t.range0[b] + (t.range1[b] - t.range0[b]) * amount;
    const auto cut = t.floorCut0[b] + (t.floorCut1[b] - t.floorCut0[b]) * amount;
    // soft-knee downward expander: below the knee the reduction grows by `slope` dB per dB
    const auto under = thr - levelDb, half = 0.5 * t.knee;
    double r;
    if (under <= -half)
        r = 0.0;
    else if (under >= half || t.knee <= 0.0)
        r = t.slope * under;
    else
        r = t.slope * (under + half) * (under + half) / (2.0 * t.knee);
    return std::min (range, std::max (0.0, r)) + cut;
}

namespace
{
class Quell final : public Effect
{
public:
    explicit Quell (const Tuning& t) : tuning (t) {}

    void prepare (double sampleRate) noexcept override
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int k = 0; k < bands - 1; ++k)
        {
            splitters[(std::size_t) k].design (sr, split[k], 24);
            for (auto& a : comp[(std::size_t) k])
                a.design (sr, split[k], 24);
        }
        detA = coef (tuning.detAttackMs, sr);
        detR = coef (tuning.detReleaseMs, sr);
        openC = coef (tuning.openMs, sr);
        closeC = coef (tuning.closeMs, sr);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& s : splitters)
            s.reset();
        for (auto& row : comp)
            for (auto& a : row)
                a.reset();
        power.fill (0.0);
        gainDb.fill (0.0);
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto amt = clamp01 (p[amount]);
        const bool listenOnly = on (p[listen]);
        const auto out = (float) dbToGain (outputDb (p[output]));
        std::array<double, bands> sumRed {};
        for (int i = 0; i < n; ++i)
        {
            float band[2][bands];
            for (int c = 0; c < 2; ++c)
            {
                float rest = c == 0 ? l[i] : r[i];
                for (int k = 0; k < bands - 1; ++k)
                {
                    float lo, hi;
                    splitters[(std::size_t) k].run (rest, c, lo, hi);
                    // keep the low band in phase with the bands split after it (their splits' all-passes)
                    for (int j = k + 1; j < bands - 1; ++j)
                        lo = comp[(std::size_t) j][(std::size_t) k].allpass (lo, c);
                    band[c][k] = lo;
                    rest = hi;
                }
                band[c][bands - 1] = rest;
            }
            float yl = 0.0f, yr = 0.0f, dl = 0.0f, dr = 0.0f;
            for (int b = 0; b < bands; ++b)
            {
                const auto e = 0.5 * ((double) band[0][b] * band[0][b] + (double) band[1][b] * band[1][b]);
                auto& pw = power[(std::size_t) b];
                pw = e + (pw - e) * (e > pw ? detA : detR);
                const auto levelDb = 10.0 * std::log10 (2.0 * pw + 1e-30); // a sine reads its peak level
                const auto target = -reductionDb (tuning, b, levelDb, amt);
                auto& g = gainDb[(std::size_t) b];
                g = target + (g - target) * (target > g ? openC : closeC);
                sumRed[(std::size_t) b] += -g;
                const auto lin = (float) dbToGain (g);
                yl += lin * band[0][b];
                yr += lin * band[1][b];
                dl += (1.0f - lin) * band[0][b];
                dr += (1.0f - lin) * band[1][b];
            }
            l[i] = out * (listenOnly ? dl : yl);
            r[i] = out * (listenOnly ? dr : yr);
        }
        double mean = 0.0;
        for (int b = 0; b < bands; ++b)
        {
            const auto red = n > 0 ? sumRed[(std::size_t) b] / n : 0.0;
            meter[(std::size_t) b + 1].store ((float) std::clamp (red / 30.0, 0.0, 1.0), std::memory_order_relaxed);
            mean += red / bands;
        }
        meter[0].store ((float) std::clamp (mean / 30.0, 0.0, 1.0), std::memory_order_relaxed);
    }

    float controlOutput (int k) const noexcept override
    {
        return k >= 0 && k <= bands ? meter[(std::size_t) k].load (std::memory_order_relaxed) : 0.0f;
    }

private:
    Tuning tuning;
    double sr = 48000.0, detA = 0.0, detR = 0.0, openC = 0.0, closeC = 0.0;
    std::array<LrSplit, bands - 1> splitters;
    // comp[j][k]: split j's all-pass applied to band k (k < j)
    std::array<std::array<LrSplit, bands - 1>, bands - 1> comp;
    std::array<double, bands> power {}, gainDb {};
    std::array<std::atomic<float>, bands + 1> meter {};
};
} // namespace

std::unique_ptr<Effect> make (const Tuning& t) { return std::make_unique<Quell> (t); }
} // namespace quell

// =====================================================================================================================
// Gemini Doubler
// =====================================================================================================================
namespace gemini
{
double outputDb (float v) noexcept { return -24.0 + 24.0 * clamp01 (v); }
double gainDb (float v) noexcept { return -48.0 + 48.0 * clamp01 (v); }
double panDegrees (float v) noexcept { return -45.0 + 90.0 * clamp01 (v); }
double delayMs (float v) noexcept { return 100.0 * clamp01 (v); }
double feedback (float v) noexcept { return clamp01 (v); }
double detuneCents (float v) noexcept { return -100.0 + 200.0 * clamp01 (v); }
double depthCents (float v) noexcept { return -200.0 + 400.0 * clamp01 (v); }
double rateValue (float v) noexcept { return 0.1 * std::pow (4000.0, clamp01 (v)); }
double shelfDb (float v) noexcept { return -30.0 + 42.0 * clamp01 (v); }
double freqHz (float v) noexcept { return 16.0 * std::pow (21357.0 / 16.0, clamp01 (v)); }
float freqValue (double hz) noexcept { return (float) std::clamp (std::log (std::max (hz, 1.0) / 16.0) / std::log (21357.0 / 16.0), 0.0, 1.0); }
int sourceOf (float v) noexcept { return std::clamp ((int) std::floor (clamp01 (v) * 3.0), 0, 2); }
double windowMs (float rangeV) noexcept { return on (rangeV) ? 53.333 : 13.333; }

namespace
{
// mono delay line with linear-interpolated reads
struct Line
{
    std::vector<float> buf;
    int pos = 0;
    void allocate (int n) { buf.assign ((std::size_t) std::max (n, 4), 0.0f); pos = 0; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push (float x) noexcept
    {
        buf[(std::size_t) pos] = x;
        if (++pos == (int) buf.size())
            pos = 0;
    }
    // d samples ago (d ≥ 1: the sample pushed last is 1 ago)
    float at (double d) const noexcept
    {
        const auto size = (int) buf.size();
        d = std::clamp (d, 1.0, (double) size - 4.0);
        const auto di = (int) d;
        const auto f = (float) (d - di);
        auto i0 = pos - di;
        if (i0 < 0)
            i0 += size;
        // 4-point Hermite between the samples d and d + 1 ago (keeps the top octave of a fractional delay)
        const auto at = [this, size] (int i) { i %= size; return buf[(std::size_t) (i < 0 ? i + size : i)]; };
        const auto xm1 = at (i0 + 1), x0 = at (i0), x1 = at (i0 - 1), x2 = at (i0 - 2);
        const auto c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

class Gemini final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        // longest read: 100 ms of delay + the 53 ms window (+ the octave's doubled sweep) + margin
        const auto n = (int) std::ceil (0.25 * sr) + 8;
        for (auto& v : voice)
            v.line.allocate (n);
        for (auto& d : direct)
            d.allocate ((int) std::ceil (0.03 * sr) + 8);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& v : voice)
        {
            v.line.clear();
            v.phase = 0.0;
            v.lfo = 0.0;
            v.last = 0.0f;
            v.fbHp.reset();
        }
        for (auto& d : direct)
            d.clear();
        lowShelf.reset();
        highShelf.reset();
        designed = false;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (voice[0].line.buf.empty())
            return;
        design (p);
        const auto master = (float) dbToGain (outputDb (p[output]));
        const bool dirOn = on (p[directOn]);
        const auto dirGain = dirOn ? (float) dbToGain (gainDb (p[directGain])) : 0.0f;
        const auto rot = -panDegrees (p[directPan]) * pi / 180.0; // −45°: L = (L + R)/√2, R = (R − L)/√2
        const auto rc = (float) std::cos (rot), rs = (float) std::sin (rot);
        const auto window = windowMs (p[range]) * 0.001 * sr;
        // [EXEC] measured: with Align the direct signal comes 6.67 ms late and every pass through a voice takes its
        // delay + 10.3 ms (the repeats too); without it a voice takes its delay alone
        const bool aligned = on (p[align]);
        const auto alignSamples = aligned ? sr / 150.0 : 0.0; // 6.67 ms (320 samples at 48 kHz)
        const auto voiceExtra = (aligned ? 0.0103 * sr : 0.0) - 0.5 * window;
        const bool lowOnV = on (p[lowOn]), highOnV = on (p[highOn]);

        struct VoiceSetup
        {
            bool active;
            float gl, gr, fb;
            double delay, cents, depth, lfoInc;
            bool octave;
            int source;
        } vs[voices];
        for (int k = 0; k < voices; ++k)
        {
            auto& s = vs[k];
            s.active = on (p[(std::size_t) voiceParam (k, vOn)]);
            const auto g = dbToGain (gainDb (p[(std::size_t) voiceParam (k, vGain)]));
            // equal-power pan over ±45°: −45 = left only, 0 = −3 dB on each side
            const auto a = (panDegrees (p[(std::size_t) voiceParam (k, vPan)]) + 45.0) * pi / 180.0;
            s.gl = (float) (g * std::cos (a));
            s.gr = (float) (g * std::sin (a));
            s.fb = (float) feedback (p[(std::size_t) voiceParam (k, vFeedback)]);
            s.delay = delayMs (p[(std::size_t) voiceParam (k, vDelay)]) * 0.001 * sr;
            s.cents = detuneCents (p[(std::size_t) voiceParam (k, vDetune)]);
            s.depth = depthCents (p[(std::size_t) voiceParam (k, vDepth)]);
            s.lfoInc = 0.5 * rateValue (p[(std::size_t) voiceParam (k, vRate)]) / sr;
            s.octave = on (p[(std::size_t) voiceParam (k, vOctave)]);
            s.source = sourceOf (p[(std::size_t) voiceParam (k, vSource)]);
        }

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            float wetL = 0.0f, wetR = 0.0f;
            for (int k = 0; k < voices; ++k)
            {
                auto& v = voice[k];
                const auto& s = vs[k];
                const auto own = k == 0 ? inL : inR, other = k == 0 ? inR : inL;
                const auto src = s.source == 0 ? own : (s.source == 1 ? 0.5f * (own + other) : 0.5f * (own - other));
                // feedback from the voice's last output through the low cut
                v.line.push (src + s.fb * v.fbHp.process (v.last, 0));
                if (! s.active)
                {
                    v.last = 0.0f;
                    continue;
                }
                // triangle LFO on the pitch
                v.lfo += s.lfoInc;
                v.lfo -= std::floor (v.lfo);
                const auto tri = v.lfo < 0.5 ? 4.0 * v.lfo - 1.0 : 3.0 - 4.0 * v.lfo;
                auto ratio = std::pow (2.0, (s.cents + s.depth * tri) / 1200.0);
                if (s.octave)
                    ratio *= 0.5;
                // two read taps half a window apart sweep the window at (1 − ratio) samples per sample
                v.phase += (1.0 - ratio) / window;
                v.phase -= std::floor (v.phase);
                float y = 0.0f;
                for (int t = 0; t < 2; ++t)
                {
                    auto ph = v.phase + 0.5 * t;
                    ph -= std::floor (ph);
                    const auto w = (float) std::sin (pi * ph); // power-complementary crossfade of the two grains
                    y += w * v.line.at (1.0 + s.delay + voiceExtra + ph * window);
                }
                v.last = y;
                wetL += s.gl * y;
                wetR += s.gr * y;
            }
            if (lowOnV)
            {
                wetL = lowShelf.process (wetL, 0);
                wetR = lowShelf.process (wetR, 1);
            }
            if (highOnV)
            {
                wetL = highShelf.process (wetL, 0);
                wetR = highShelf.process (wetR, 1);
            }
            // the direct signal: rotated in the stereo field, delayed by half a window when aligned with the voices
            direct[0].push (inL);
            direct[1].push (inR);
            const auto dL = alignSamples > 0.0 ? direct[0].at (1.0 + alignSamples) : inL;
            const auto dR = alignSamples > 0.0 ? direct[1].at (1.0 + alignSamples) : inR;
            const auto oL = dirGain * (rc * dL + rs * dR), oR = dirGain * (rc * dR - rs * dL);
            l[i] = master * (oL + wetL);
            r[i] = master * (oR + wetR);
        }
    }

private:
    void design (const EffectParams& p) noexcept
    {
        float key[5] { p[lowGain], p[lowFreq], p[highGain], p[highFreq], p[feedbackCut] };
        if (designed && std::equal (key, key + 5, lastKey))
            return;
        std::copy (key, key + 5, lastKey);
        designed = true;
        // [EXEC] measured: first-order shelves; a high shelf of gain g at f0 has its corners at f0 and f0 · max (g, 1/g)
        // (-12 dB at 3538 Hz: -2.1 dB at 3.5 kHz, -6.8 at 8 kHz, -11.9 at 16 kHz; +6 dB at 1306 Hz: +2.6 at 1.5 kHz,
        // +4.5 at 3 kHz); the low shelf mirrors it (corners f0 / max (g, 1/g) and f0)
        firstOrderShelf (lowShelf, freqHz (p[lowFreq]), shelfDb (p[lowGain]), false);
        firstOrderShelf (highShelf, freqHz (p[highFreq]), shelfDb (p[highGain]), true);
        for (auto& v : voice)
            v.fbHp.filter (sr, 1, std::max (10.0, freqHz (p[feedbackCut])), 0.7071);
    }

    // H(s) = k (1 + s/wz) / (1 + s/wp), bilinear with the corners pre-warped
    void firstOrderShelf (Biquad& b, double f0, double db, bool high) const noexcept
    {
        const auto g = dbToGain (db);
        const auto r = std::max (g, 1.0 / g);
        const auto lo = high ? f0 : f0 / r, up = high ? f0 * r : f0;
        // high boost: zero lo, pole up; high cut: pole lo, zero up; low boost: pole lo, zero up, × r; low cut: zero lo, pole up, ÷ r
        double fz, fp, k;
        if (high)
            fz = g >= 1.0 ? lo : up, fp = g >= 1.0 ? up : lo, k = 1.0;
        else
            fz = g >= 1.0 ? up : lo, fp = g >= 1.0 ? lo : up, k = g >= 1.0 ? r : 1.0 / r;
        const auto warp = [this] (double f) { return std::tan (pi * std::min (f, 0.49 * sr) / sr); };
        const auto wz = warp (fz), wp = warp (fp);
        // s → (1 − z⁻¹)/(1 + z⁻¹) with s/w: (1 + s/wz) → ((wz + 1) + (wz − 1) z⁻¹) / (wz (1 + z⁻¹))
        const auto n0 = k * (wz + 1.0) / wz, n1 = k * (wz - 1.0) / wz;
        const auto d0 = (wp + 1.0) / wp, d1 = (wp - 1.0) / wp;
        b.b0 = n0 / d0;
        b.b1 = n1 / d0;
        b.b2 = 0.0;
        b.a1 = d1 / d0;
        b.a2 = 0.0;
    }

    struct Voice
    {
        Line line;
        double phase = 0.0, lfo = 0.0;
        float last = 0.0f;
        Biquad fbHp;
    };

    double sr = 48000.0;
    std::array<Voice, voices> voice;
    std::array<Line, 2> direct;
    Biquad lowShelf, highShelf;
    bool designed = false;
    float lastKey[5] {};
};
} // namespace
} // namespace gemini

// =====================================================================================================================
// registry
// =====================================================================================================================
const EffectInfo& quellInfo()
{
    static const EffectInfo info { "Quell Denoiser", { { "Amount", 0.3f }, { "Listen", 0.0f }, { "Output", 0.5f } } };
    return info;
}

const EffectInfo& geminiInfo()
{
    static const EffectInfo info { "Gemini Doubler",
                                   { { "Output", 1.0f }, { "Direct", 1.0f }, { "Direct gain", 1.0f }, { "Direct pan", 0.5f },
                                     { "Align", 1.0f }, { "Low shelf", 0.0f }, { "Low gain", 30.0f / 42.0f },
                                     { "Low freq", gemini::freqValue (100.0) }, { "High shelf", 0.0f }, { "High gain", 30.0f / 42.0f },
                                     { "High freq", gemini::freqValue (5000.0) }, { "Feedback cut", 0.0f }, { "Range", 0.0f },
                                     // voice 1 (left) and voice 2 (right): a classic double, 9.4 / 23.7 ms, ±6 cents, −6 dB
                                     { "V1 on", 1.0f }, { "V1 gain", 0.875f }, { "V1 pan", 0.0f }, { "V1 delay", 0.094f },
                                     { "V1 feedback", 0.0f }, { "V1 detune", 0.53f }, { "V1 depth", 0.5f }, { "V1 rate", 0.2776f },
                                     { "V1 octave", 0.0f }, { "V1 source", 0.1667f },
                                     { "V2 on", 1.0f }, { "V2 gain", 0.875f }, { "V2 pan", 1.0f }, { "V2 delay", 0.237f },
                                     { "V2 feedback", 0.0f }, { "V2 detune", 0.47f }, { "V2 depth", 0.5f }, { "V2 rate", 0.2776f },
                                     { "V2 octave", 0.0f }, { "V2 source", 0.1667f } } };
    return info;
}

bool isVoiceKind (EffectKind kind) noexcept { return kind == EffectKind::quellDenoiser || kind == EffectKind::geminiDoubler; }

std::unique_ptr<Effect> make (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::quellDenoiser: return quell::make (quell::defaultTuning());
        case EffectKind::geminiDoubler: return std::make_unique<gemini::Gemini>();
        default: return nullptr;
    }
}

std::string paramText (EffectKind kind, int param, float v)
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (kind == EffectKind::quellDenoiser)
    {
        switch (param)
        {
            case quell::amount: return fmt ("%.1f%%", 100.0 * v);
            case quell::listen: return on (v) ? "removed noise" : "off";
            case quell::output: return fmt ("%+.1f dB", quell::outputDb (v));
            default: return fmt ("%.2f", v);
        }
    }
    if (kind == EffectKind::geminiDoubler)
    {
        using namespace gemini;
        if (param >= voice1 && param < paramCount)
        {
            switch ((VoiceParam) ((param - voice1) % voiceParams))
            {
                case vOn: case vOctave: return on (v) ? "on" : "off";
                case vGain: return fmt ("%.1f dB", gainDb (v));
                case vPan: return fmt ("%+.1f°", panDegrees (v));
                case vDelay: return fmt ("%.1f ms", delayMs (v));
                case vFeedback: return fmt ("%.0f%%", 100.0 * feedback (v));
                case vDetune: return fmt ("%+.0f c", detuneCents (v));
                case vDepth: return fmt ("%+.0f c", depthCents (v));
                case vRate: return fmt ("%.1f", rateValue (v));
                case vSource:
                {
                    static const char* const names[3] { "own side", "L + R", "L − R" };
                    return names[sourceOf (v)];
                }
                default: break;
            }
            return fmt ("%.2f", v);
        }
        switch (param)
        {
            case output: return fmt ("%.1f dB", outputDb (v));
            case directGain: return fmt ("%.1f dB", gainDb (v));
            case directPan: return fmt ("%+.1f°", panDegrees (v));
            case lowGain: case highGain: return fmt ("%+.1f dB", shelfDb (v));
            case lowFreq: case highFreq: case feedbackCut: return hzText (freqHz (v));
            case range: return on (v) ? "20 Hz" : "80 Hz";
            default: return on (v) ? "on" : "off";
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::voicetools
