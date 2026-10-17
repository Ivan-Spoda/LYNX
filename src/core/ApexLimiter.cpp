#include "core/ApexLimiter.h"

#include <algorithm>
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
    { 0.25, 4.0, 0.7 },  // Punch: mostly fast, transients come back quickly
    { 0.10, 3.0, 0.5 },  // Loud: fast recovery, the most density
    { 0.70, 8.0, 1.5 },  // Safe: long, smooth recovery
};

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
        look = -1; // rebuilt by the next process()
    }

    int latencySamples() const noexcept override { return latency; }

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
        const auto& ip = interp();
        const int len = (int) inL.size();
        const int pad = maxLook - look;

        for (int i = 0; i < n; ++i)
        {
            // history for the inter-sample estimate; the detection point is `interpDelay` samples back
            std::rotate (histL.begin(), histL.begin() + 1, histL.end()); histL.back() = left[i] * gain;
            std::rotate (histR.begin(), histR.begin() + 1, histR.end()); histR.back() = right[i] * gain;
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
            slowL = aL < slowL ? aL : slowL + (aL - slowL) * kSlow;
            fastR = aR < fastR ? aR : fastR + (aR - fastR) * kFast;
            slowR = aR < slowR ? aR : slowR + (aR - slowR) * kSlow;
            const double gL = std::min (aL, (1.0 - law.slowShare) * fastL + law.slowShare * slowL);
            const double gR = std::min (aR, (1.0 - law.slowShare) * fastR + law.slowShare * slowR);

            inL[(std::size_t) pos] = (float) cl; inR[(std::size_t) pos] = (float) cr;
            const int back = (pos - look + len) % len;
            const double yl = std::clamp (inL[(std::size_t) back] * gL, -ceiling, ceiling); // safety clip
            const double yr = std::clamp (inR[(std::size_t) back] * gR, -ceiling, ceiling);
            // the rest of the maximum look-ahead as plain delay, so the latency never changes
            outL[(std::size_t) pos] = (float) yl; outR[(std::size_t) pos] = (float) yr;
            const int rd = (pos - pad + len) % len;
            left[i] = outL[(std::size_t) rd]; right[i] = outR[(std::size_t) rd];
            pos = (pos + 1) % len;
        }
    }

private:
    double sampleRate = 48000.0;
    int maxLook = 240, look = -1, latency = 244;
    std::vector<float> inL, inR, outL, outR;
    std::array<double, interpTaps> histL {}, histR {};
    std::vector<double> boxL, boxR;
    double boxSumL = 1.0, boxSumR = 1.0;
    int boxPos = 0, pos = 0;
    SlidingMin minAll, minL, minR;
    double fastL = 1.0, fastR = 1.0, slowL = 1.0, slowR = 1.0;
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
    static const char* names[] = { "Clean", "Punch", "Loud", "Safe" };
    return names[(int) s];
}

const EffectInfo& info()
{
    static const EffectInfo i { "Apex Limiter",
                                { { "Gain", 0.0f }, { "Ceiling", 1.0f - 0.1f / 30.0f }, { "Lookahead", 0.2f }, { "Release", releaseValue (150.0) },
                                  { "Style", 0.0f }, { "True Peak", 1.0f }, { "Link", 1.0f } } };
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
        case 5: return v >= 0.5f ? "On" : "Off";
        default: std::snprintf (t, sizeof (t), "%.0f%%", v * 100.0f); break;
    }
    return t;
}

int latencyAt (double sr) noexcept { return (int) std::ceil (maxLookaheadMs * 0.001 * sr) + interpDelay; }

std::unique_ptr<Effect> make() { return std::make_unique<ApexLimiterEffect>(); }
} // namespace lynx::apex
