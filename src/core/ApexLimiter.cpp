#include "core/ApexLimiter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace lynx::apex
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int interpTaps = 8;   // per phase of the 4× inter-sample peak estimator
constexpr int interpDelay = interpTaps / 2;

// 4× interpolation filter (windowed sinc), phases 1..3 (phase 0 is the sample itself)
struct Interp
{
    std::array<std::array<double, interpTaps>, 3> h {};
    Interp()
    {
        for (int ph = 1; ph <= 3; ++ph)
        {
            double sum = 0.0;
            for (int k = 0; k < interpTaps; ++k)
            {
                const double t = (k - (interpTaps / 2 - 1)) - ph / 4.0; // tap position relative to the interpolated point
                const double x = pi * t;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (x) / x;
                const double w = 0.5 + 0.5 * std::cos (pi * t / (interpTaps / 2 + 0.5));
                h[(std::size_t) ph - 1][(std::size_t) k] = sinc * w;
                sum += sinc * w;
            }
            for (auto& c : h[(std::size_t) ph - 1]) c /= sum;
        }
    }
};
const Interp& interp() { static const Interp i; return i; }

// per style: share of the slow release stage, slow-stage time factor, smoothing shape of the attack (0 = linear ramp)
struct StyleLaw { double slowShare, slowFactor, fastFactor; };
constexpr StyleLaw laws[styleCount] = {
    { 0.50, 6.0, 1.0 },  // Clean: half of the recovery on a slow stage — least distortion
    { 0.60, 7.0, 1.2 },  // Glue (round 128): a little more slow stage than Clean — smooth, steady
    { 0.25, 4.0, 0.7 },  // Punch: mostly fast, transients come back quickly
    { 0.35, 5.0, 0.6 },  // Dynamic (round 128): quick fast stage, some slow stage — keeps the movement
    { 0.15, 3.5, 0.6 },  // Dense (round 128): between Punch and Loud
    { 0.10, 3.0, 0.5 },  // Loud: fast recovery, the most density
    { 0.40, 5.0, 0.8 },  // Bus (round 128): moderate, for groups and buses
    { 0.70, 8.0, 1.5 },  // Safe: long, smooth recovery
};

// error-feedback noise shaping filters (on the quantisation error of the last samples)
constexpr double shapers[4][3] = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 2.0, -1.0, 0.0 }, { 1.623, -0.982, 0.109 } };

// running minimum over the last `len` values (monotonic queue in a ring allocated by setCapacity, never on the audio thread)
class SlidingMin
{
public:
    void setCapacity (int n) { idx.assign ((std::size_t) n + 2, 0); val.assign ((std::size_t) n + 2, 0.0); setLength (1); }
    void setLength (int n) noexcept { len = std::clamp (n, 1, (int) idx.size() - 1); head = tail = 0; index = 0; }
    double push (double v) noexcept
    {
        const int cap = (int) idx.size();
        while (tail != head && val[(std::size_t) ((tail - 1 + cap) % cap)] >= v) tail = (tail - 1 + cap) % cap;
        idx[(std::size_t) tail] = index; val[(std::size_t) tail] = v; tail = (tail + 1) % cap;
        while (idx[(std::size_t) head] <= index - len) head = (head + 1) % cap;
        ++index;
        return val[(std::size_t) head];
    }
private:
    std::vector<long long> idx;
    std::vector<double> val;
    int head = 0, tail = 0, len = 1;
    long long index = 0;
};

class ApexLimiterEffect final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        maxLook = (int) std::ceil (maxLookaheadMs * 0.001 * sr);
        latency = maxLook + interpDelay;
        inL.assign ((std::size_t) maxLook + 2, 0.0f); inR.assign ((std::size_t) maxLook + 2, 0.0f);
        outL.assign ((std::size_t) maxLook + 2, 0.0f); outR.assign ((std::size_t) maxLook + 2, 0.0f);
        boxL.assign ((std::size_t) maxLook + 1, 1.0); boxR.assign ((std::size_t) maxLook + 1, 1.0);
        minAll.setCapacity (maxLook + 1); minL.setCapacity (maxLook + 1); minR.setCapacity (maxLook + 1);
        reset();
    }

    void reset() noexcept override
    {
        std::fill (inL.begin(), inL.end(), 0.0f); std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (outL.begin(), outL.end(), 0.0f); std::fill (outR.begin(), outR.end(), 0.0f);
        histL.fill (0.0); histR.fill (0.0);
        pos = 0;
        fastL = fastR = slowL = slowR = 1.0;
        dcXL = dcXR = dcYL = dcYR = 0.0;
        err = {};
        rng = 0x9e3779b97f4a7c15ull;
        look = -1; // rebuilt by the next process()
    }

    int latencySamples() const noexcept override { return latency; }

    // live meters for the editor (round 114): 0 = gain reduction over 0 … 30 dB (0..1), 2 = input peak, 3 = output peak
    float controlOutput (int k) const noexcept override
    {
        return k == 0 ? meterGr.load (std::memory_order_relaxed) : k == 2 ? meterIn.load (std::memory_order_relaxed)
                      : k == 3 ? meterOut.load (std::memory_order_relaxed) : 0.0f;
    }

    void process (float* left, float* right, int n, const EffectParams& p, double) noexcept override
    {
        if (inL.empty()) prepare (sampleRate);
        const double gain = std::pow (10.0, gainDb (p[0]) / 20.0);
        const double ceiling = std::pow (10.0, ceilingDb (p[1]) / 20.0);
        const int newLook = std::clamp ((int) std::lround (lookaheadMs (p[2]) * 0.001 * sampleRate), 1, maxLook);
        if (newLook != look)
        {
            look = newLook;
            minAll.setLength (look + 1); minL.setLength (look + 1); minR.setLength (look + 1);
            std::fill (boxL.begin(), boxL.end(), 1.0); std::fill (boxR.begin(), boxR.end(), 1.0);
            boxSumL = boxSumR = (double) look; boxPos = 0;
        }
        const auto law = laws[(int) style (p[4])];
        const double rel = releaseMs (p[3]) * 0.001 * sampleRate;
        const double kFast = 1.0 - std::exp (-1.0 / std::max (1.0, rel * law.fastFactor));
        const double kSlow = 1.0 - std::exp (-1.0 / std::max (1.0, rel * law.slowFactor));
        const bool truePeak = p[5] >= 0.5f;
        const double link = std::clamp ((double) p[6], 0.0, 1.0);
        const double attackSamples = attackMs (p[pAttack]) * 0.001 * sampleRate;
        const double kAttack = attackSamples >= 1.0 ? 1.0 - std::exp (-1.0 / attackSamples) : 0.0;
        const double linkRelease = std::clamp ((double) p[pLinkRelease], 0.0, 1.0);
        const bool unity = p[pUnity] >= 0.5f, audition = p[pAudition] >= 0.5f, dcFilter = p[pDcFilter] >= 0.5f;
        if (! dcFilter) dcXL = dcXR = dcYL = dcYR = 0.0;
        dcR = std::exp (-2.0 * pi * 5.0 / sampleRate);
        bits = ditherBits (p[pDither]);
        shaping = noiseShaping (p[pShaping]);
        const auto& ip = interp();
        const int len = (int) inL.size();
        const int pad = maxLook - look;
        double minGain = 1.0;
        float inPeak = 0.0f, outPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // history for the inter-sample estimate; the detection point is `interpDelay` samples back
            double xl = left[i], xr = right[i];
            if (dcFilter) // one-pole high pass at ~5 Hz
            {
                const double yl = xl - dcXL + dcR * dcYL, yr = xr - dcXR + dcR * dcYR;
                dcXL = xl; dcXR = xr; dcYL = yl; dcYR = yr;
                xl = yl; xr = yr;
            }
            std::rotate (histL.begin(), histL.begin() + 1, histL.end()); histL.back() = xl * gain;
            std::rotate (histR.begin(), histR.begin() + 1, histR.end()); histR.back() = xr * gain;
            const double cl = histL[(std::size_t) interpTaps / 2 - 1], cr = histR[(std::size_t) interpTaps / 2 - 1];
            double pl = std::abs (cl), pr = std::abs (cr);
            if (truePeak)
                for (int ph = 0; ph < 3; ++ph)
                {
                    double sl = 0.0, sr = 0.0;
                    for (int k = 0; k < interpTaps; ++k)
                    {
                        sl += ip.h[(std::size_t) ph][(std::size_t) k] * histL[(std::size_t) k];
                        sr += ip.h[(std::size_t) ph][(std::size_t) k] * histR[(std::size_t) k];
                    }
                    pl = std::max (pl, std::abs (sl)); pr = std::max (pr, std::abs (sr));
                }

            // required gain, held over look + 1 samples, then a box average of `look` samples: the result is at or
            // below the requirement of the sample `look` samples back, the one leaving the delay now
            const double reqAll = std::min (1.0, ceiling / std::max (std::max (pl, pr), 1e-12));
            const double reqL = std::min (1.0, ceiling / std::max (pl, 1e-12));
            const double reqR = std::min (1.0, ceiling / std::max (pr, 1e-12));
            const double hAll = minAll.push (reqAll), hL = minL.push (reqL), hR = minR.push (reqR);
            const double tL = link * hAll + (1.0 - link) * hL, tR = link * hAll + (1.0 - link) * hR;
            boxSumL += tL - boxL[(std::size_t) boxPos]; boxL[(std::size_t) boxPos] = tL;
            boxSumR += tR - boxR[(std::size_t) boxPos]; boxR[(std::size_t) boxPos] = tR;
            boxPos = (boxPos + 1) % look;
            const double aL = std::min (1.0, boxSumL / look), aR = std::min (1.0, boxSumR / look);

            // release: the gain follows a drop at once and recovers on a fast and a slow stage (mixed by style)
            fastL = aL < fastL ? aL : fastL + (aL - fastL) * kFast;
            slowL = aL < slowL ? (kAttack > 0.0 ? slowL + (aL - slowL) * kAttack : aL) : slowL + (aL - slowL) * kSlow;
            fastR = aR < fastR ? aR : fastR + (aR - fastR) * kFast;
            slowR = aR < slowR ? (kAttack > 0.0 ? slowR + (aR - slowR) * kAttack : aR) : slowR + (aR - slowR) * kSlow;
            double gL = std::min (aL, (1.0 - law.slowShare) * fastL + law.slowShare * slowL);
            double gR = std::min (aR, (1.0 - law.slowShare) * fastR + law.slowShare * slowR);
            if (linkRelease > link) // the release stage linked further than the transients (round 128)
            {
                const double shared = std::min (gL, gR), f = (linkRelease - link) / (1.0 - link);
                gL += f * (shared - gL);
                gR += f * (shared - gR);
            }

            minGain = std::min (minGain, std::min (gL, gR));
            inPeak = std::max (inPeak, std::max (std::abs (left[i]), std::abs (right[i])));
            inL[(std::size_t) pos] = (float) cl; inR[(std::size_t) pos] = (float) cr;
            const int back = (pos - look + len) % len;
            double yl = std::clamp (inL[(std::size_t) back] * gL, -ceiling, ceiling); // safety clip
            double yr = std::clamp (inR[(std::size_t) back] * gR, -ceiling, ceiling);
            if (audition) { yl = inL[(std::size_t) back] - yl; yr = inR[(std::size_t) back] - yr; } // what is removed
            if (unity) { yl /= gain; yr /= gain; }
            if (bits > 0) { yl = dither (yl, 0); yr = dither (yr, 1); }
            // the rest of the maximum look-ahead as plain delay, so the latency never changes
            outL[(std::size_t) pos] = (float) yl; outR[(std::size_t) pos] = (float) yr;
            const int rd = (pos - pad + len) % len;
            left[i] = outL[(std::size_t) rd]; right[i] = outR[(std::size_t) rd];
            outPeak = std::max (outPeak, std::max (std::abs (left[i]), std::abs (right[i])));
            pos = (pos + 1) % len;
        }
        meterGr.store ((float) std::clamp (-20.0 * std::log10 (std::max (minGain, 1e-9)) / 30.0, 0.0, 1.0), std::memory_order_relaxed);
        meterIn.store (std::min (inPeak, 1.0f), std::memory_order_relaxed);
        meterOut.store (std::min (outPeak, 1.0f), std::memory_order_relaxed);
    }

private:
    // TPDF dither at the chosen word length with error-feedback noise shaping
    double dither (double x, int ch) noexcept
    {
        const double scale = std::ldexp (1.0, bits - 1);
        const auto& h = shapers[shaping];
        auto& e = err[(std::size_t) ch];
        const double v = x - (h[0] * e[0] + h[1] * e[1] + h[2] * e[2]);
        const double tpdf = (rand01() - rand01()); // ±1 LSB, triangular
        const double q = std::round (v * scale + tpdf) / scale;
        e[2] = e[1]; e[1] = e[0]; e[0] = q - v;
        return q;
    }
    double rand01() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
    }

    double sampleRate = 48000.0;
    int maxLook = 240, look = -1, latency = 244;
    int bits = 0, shaping = 0;
    std::array<std::array<double, 3>, 2> err {};
    unsigned long long rng = 0x9e3779b97f4a7c15ull;
    double dcXL = 0.0, dcXR = 0.0, dcYL = 0.0, dcYR = 0.0, dcR = 0.9993;
    std::vector<float> inL, inR, outL, outR;
    std::array<double, interpTaps> histL {}, histR {};
    std::vector<double> boxL, boxR;
    double boxSumL = 1.0, boxSumR = 1.0;
    int boxPos = 0, pos = 0;
    SlidingMin minAll, minL, minR;
    double fastL = 1.0, fastR = 1.0, slowL = 1.0, slowR = 1.0;
    std::atomic<float> meterGr { 0.0f }, meterIn { 0.0f }, meterOut { 0.0f };
};
} // namespace

double gainDb (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f) * 30.0; }
double ceilingDb (float v) noexcept { return ((double) std::clamp (v, 0.0f, 1.0f) - 1.0) * 30.0; }
double lookaheadMs (float v) noexcept { return (double) std::clamp (v, 0.0f, 1.0f) * maxLookaheadMs; }
double releaseMs (float v) noexcept { return std::pow (1000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float releaseValue (double ms) noexcept { return (float) std::clamp (std::log (ms) / std::log (1000.0), 0.0, 1.0); }
Style style (float v) noexcept { return (Style) std::clamp ((int) std::lround (v * (styleCount - 1)), 0, styleCount - 1); }
const char* styleName (Style s) noexcept
{
    static const char* names[] = { "Clean", "Glue", "Punch", "Dynamic", "Dense", "Loud", "Bus", "Safe" };
    return names[(int) s];
}
double attackMs (float v) noexcept { const double x = std::clamp ((double) v, 0.0, 1.0); return 10000.0 * x * x * x * x; }
int ditherBits (float v) noexcept
{
    static constexpr int b[] { 0, 16, 18, 20, 22, 24 };
    return b[std::clamp ((int) std::lround (v * 5.0f), 0, 5)];
}
int noiseShaping (float v) noexcept { return std::clamp ((int) std::lround (v * 3.0f), 0, 3); }
int meterScale (float v) noexcept { return std::clamp ((int) std::lround (v * 5.0f), 0, 5); }

const EffectInfo& info()
{
    static const EffectInfo i { "Apex Limiter",
                                { { "Gain", 0.0f }, { "Ceiling", 1.0f - 0.1f / 30.0f }, { "Lookahead", 0.2f }, { "Release", releaseValue (150.0) },
                                  { "Style", 0.0f }, { "True Peak", 1.0f }, { "Link", 1.0f },
                                  // round 128
                                  { "Attack", 0.0f }, { "Link release", 0.0f }, { "Dither", 0.0f }, { "Noise shaping", 2.0f / 3.0f },
                                  { "Unity gain", 0.0f }, { "Audition", 0.0f }, { "DC filter", 0.0f }, { "Meter scale", 0.4f } } };
    return i;
}

std::string paramText (int p, float v)
{
    char t[40];
    switch (p)
    {
        case 0: std::snprintf (t, sizeof (t), "+%.1f dB", gainDb (v)); break;
        case 1: std::snprintf (t, sizeof (t), "%.1f dB", ceilingDb (v)); break;
        case 2: std::snprintf (t, sizeof (t), "%.2f ms", lookaheadMs (v)); break;
        case 3: std::snprintf (t, sizeof (t), releaseMs (v) < 100 ? "%.1f ms" : "%.0f ms", releaseMs (v)); break;
        case 4: return styleName (style (v));
        case 5: case pUnity: case pAudition: case pDcFilter: return v >= 0.5f ? "On" : "Off";
        case pAttack:
        {
            const auto ms = attackMs (v);
            if (ms < 0.05) return "0 ms";
            std::snprintf (t, sizeof (t), ms < 10.0 ? "%.2f ms" : ms < 1000.0 ? "%.0f ms" : "%.2f s", ms < 1000.0 ? ms : ms / 1000.0);
            break;
        }
        case pDither:
            if (ditherBits (v) == 0) return "Off";
            std::snprintf (t, sizeof (t), "%d bits", ditherBits (v));
            break;
        case pShaping: { static const char* n[] = { "None", "Basic", "Optimized", "Weighted" }; return n[noiseShaping (v)]; }
        case pMeterScale: { static const char* n[] = { "-16 dB", "-32 dB", "-48 dB", "K-12", "K-14", "K-20" }; return n[meterScale (v)]; }
        default: std::snprintf (t, sizeof (t), "%.0f%%", v * 100.0f); break;
    }
    return t;
}

int latencyAt (double sr) noexcept { return (int) std::ceil (maxLookaheadMs * 0.001 * sr) + interpDelay; }

std::unique_ptr<Effect> make() { return std::make_unique<ApexLimiterEffect>(); }
} // namespace lynx::apex
