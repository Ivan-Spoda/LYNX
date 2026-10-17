#include "core/EchoSuite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/Biquad.h"
#include "core/Effects.h"

namespace lynx::echosuite
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;

float clamp01 (float v) noexcept { return std::clamp (std::isfinite (v) ? v : 0.0f, 0.0f, 1.0f); }
double bipolar (float v) noexcept { return 2.0 * (double) clamp01 (v) - 1.0; }
double signedPow (double x, double k) noexcept { return x < 0.0 ? -std::pow (-x, k) : std::pow (x, k); }
bool on (float v) noexcept { return v >= 0.5f; }

// ---- building blocks -----------------------------------------------------------------------------------------------

// Circular delay line (power-of-two size). at (d) is x[n − d] for the sample about to be written (d ≥ 2), read with
// 4-point cubic (Catmull-Rom / Hermite) interpolation.
struct Line
{
    std::vector<float> buf;
    int mask = 0;
    int w = 0;

    void allocate (double samples)
    {
        int size = 16;
        while ((double) size < samples + 8.0)
            size <<= 1;
        buf.assign ((std::size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
    }
    double maxDelay() const noexcept { return (double) mask - 4.0; }
    void push (float x) noexcept
    {
        buf[(std::size_t) w] = x;
        w = (w + 1) & mask;
    }
    float tap (int d) const noexcept { return buf[(std::size_t) ((w - d) & mask)]; } // x[n − d], d ≥ 1
    float at (double d) const noexcept
    {
        if (buf.empty())
            return 0.0f;
        d = std::clamp (d, 2.0, maxDelay());
        const auto i = (int) d;
        const auto t = (float) (d - (double) i);
        const auto xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
};

// one-pole low-pass (impulse invariant); hp() = input − low-pass
struct OnePole
{
    float a = 1.0f, z = 0.0f;
    void set (double hz, double sr) noexcept { a = (float) (1.0 - std::exp (-twoPi * std::clamp (hz, 1.0, sr * 0.49) / sr)); }
    void open() noexcept { a = 1.0f; } // passes everything (the low-pass output is the input)
    float lp (float x) noexcept { return z += a * (x - z); }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() noexcept { z = 0.0f; }
};

// 8th-order Butterworth low-pass (4 cookbook sections)
struct Butter8
{
    std::array<Biquad, 4> s;
    double hz = -1.0;
    void set (double f, double sr) noexcept
    {
        f = std::clamp (f, 20.0, sr * 0.45);
        if (std::abs (f - hz) < 0.5)
            return;
        hz = f;
        static constexpr double q[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        const auto w0 = twoPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
        for (int k = 0; k < 4; ++k)
        {
            const auto alpha = sw / (2.0 * q[k]);
            const auto a0 = 1.0 + alpha;
            auto& b = s[(std::size_t) k];
            b.b0 = (1.0 - cw) * 0.5 / a0;
            b.b1 = (1.0 - cw) / a0;
            b.b2 = b.b0;
            b.a1 = -2.0 * cw / a0;
            b.a2 = (1.0 - alpha) / a0;
        }
    }
    float process (float x, int ch) noexcept
    {
        for (auto& b : s)
            x = b.process (x, ch);
        return x;
    }
    void reset() noexcept
    {
        for (auto& b : s)
            b.reset();
    }
};

// Saturation with unity small-signal gain: (tanh (k (x + b)) − tanh (k b)) / (k sech² (k b)). The bias b gives even
// harmonics (fitted to the measured 2nd / 3rd harmonic levels of each character, REA_rodada101 §3.3).
struct Shaper
{
    float k = 0.5f, b = 0.0f, t0 = 0.0f, g = 1.0f;
    void set (float kk, float bb) noexcept
    {
        k = kk;
        b = bb;
        t0 = std::tanh (k * b);
        g = 1.0f / (k * (1.0f - t0 * t0));
    }
    float operator() (float x) const noexcept { return (std::tanh (k * (x + b)) - t0) * g; }
};

// xorshift noise, uniform in [−1, 1)
struct Noise
{
    std::uint32_t s = 0x9e3779b9u;
    float next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) ((double) s / 2147483648.0 - 1.0);
    }
};

// smooth random LFO (cosine interpolation between random points `rate` times per second), about unit rms
struct RandomLfo
{
    Noise noise;
    double phase = 1.0;
    float from = 0.0f, to = 0.0f;
    void seed (std::uint32_t s) noexcept
    {
        noise.s = s == 0 ? 1u : s;
        from = noise.next();
        to = noise.next();
        phase = 0.0;
    }
    float next (double rate, double sr) noexcept
    {
        phase += rate / sr;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            from = to;
            to = noise.next();
        }
        const auto u = (float) (0.5 - 0.5 * std::cos (pi * phase));
        return 2.1f * (from + (to - from) * u);
    }
};

// 90° phase-difference allpass pair (the published 8th-order IIR design, Niemitalo): re / im of the analytic signal
struct Hilbert
{
    static constexpr float a[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static constexpr float b[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    std::array<float, 4> ax1 {}, ax2 {}, ay1 {}, ay2 {}, bx1 {}, bx2 {}, by1 {}, by2 {};
    float delayed = 0.0f;

    static float stage (float k, float x, float& x1, float& x2, float& y1, float& y2) noexcept
    {
        const auto y = k * k * (x + y2) - x2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
    void analytic (float x, float& re, float& im) noexcept
    {
        auto u = x, v = x;
        for (std::size_t k = 0; k < 4; ++k)
        {
            u = stage (a[k], u, ax1[k], ax2[k], ay1[k], ay2[k]);
            v = stage (b[k], v, bx1[k], bx2[k], by1[k], by2[k]);
        }
        re = delayed;
        im = v;
        delayed = u;
    }
    void reset() noexcept { *this = Hilbert {}; }
};

// single-sideband frequency shifter (shift in Hz, either sign)
struct Shifter
{
    Hilbert h;
    double phase = 0.0;
    float process (float x, double hz, double sr) noexcept
    {
        float up = 0.0f, down = 0.0f;
        both (x, hz, sr, up, down);
        return up;
    }
    // the input shifted by +hz and by −hz (the two sidebands of the same analytic signal)
    void both (float x, double hz, double sr, float& up, float& down) noexcept
    {
        float re = 0.0f, im = 0.0f;
        h.analytic (x, re, im);
        const auto c = (float) std::cos (twoPi * phase), s = (float) std::sin (twoPi * phase);
        phase += hz / sr;
        phase -= std::floor (phase);
        up = re * c + im * s;
        down = re * c - im * s;
    }
    void reset() noexcept
    {
        h.reset();
        phase = 0.0;
    }
};

// delay-line pitch shifter: two taps sweeping a window, crossfaded with sin² / cos² (the classic rotating-tape idea)
struct PitchShifter
{
    Line line;
    double phase = 0.0;
    double window = 2400.0;
    void prepare (double sr)
    {
        window = 0.05 * sr;
        line.allocate (window * 2.0 + 16.0);
    }
    void reset() noexcept
    {
        line.clear();
        phase = 0.0;
    }
    float process (float x, double ratio) noexcept
    {
        line.push (x);
        phase += (1.0 - ratio) / window;
        phase -= std::floor (phase);
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            auto p = phase + 0.5 * k;
            p -= std::floor (p);
            const auto g = (float) std::sin (pi * p);
            y += g * g * line.at (3.0 + p * window);
        }
        return y;
    }
};

// Schroeder allpass with a fractional delay
struct Allpass
{
    Line line;
    float process (float x, double d, float g) noexcept
    {
        const auto v = line.at (d);
        const auto u = x + g * v;
        line.push (u);
        return v - g * u;
    }
};

// first-order allpass (phaser stage)
struct Ap1
{
    float z = 0.0f;
    float process (float x, float a) noexcept
    {
        const auto y = a * x + z;
        z = x - a * y;
        return y;
    }
};

// smoothing of a control value toward a target (one pole, time constant in seconds)
struct Glide
{
    double value = -1.0;
    double next (double target, double seconds, double sr) noexcept
    {
        if (value < 0.0)
            value = target;
        const auto a = 1.0 - std::exp (-1.0 / std::max (1.0, seconds * sr));
        value += (target - value) * a;
        return value;
    }
};

float softLimit (float x) noexcept { return 2.0f * std::tanh (0.5f * x); } // unity small-signal, bounded at ±2

std::string fmt (const char* f, double v)
{
    char b[64];
    std::snprintf (b, sizeof (b), f, v);
    return b;
}

std::string hzText (double f) { return f >= 1000.0 ? fmt ("%.2f kHz", f / 1000.0) : fmt ("%.0f Hz", f); }
std::string msText (double ms) { return ms >= 1000.0 ? fmt ("%.2f s", ms / 1000.0) : fmt (ms < 10.0 ? "%.2f ms" : "%.1f ms", ms); }
std::string pct (double v) { return fmt ("%.0f%%", v); }

const char* const noteNames[10] = { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1", "4/1", "8/1" };
} // namespace

void equalPowerMix (float m, float& dry, float& wet) noexcept
{
    const auto a = 0.5 * pi * (double) clamp01 (m);
    dry = (float) std::cos (a);
    wet = (float) std::sin (a);
    if (m <= 0.0f)
        wet = 0.0f;
    if (m >= 1.0f)
        dry = 0.0f;
}

// =====================================================================================================================
// Echoform
// =====================================================================================================================
namespace echoform
{
namespace
{
constexpr std::array<std::pair<double, double>, 7> timeLaw { { { 0.0, 0.0 }, { 0.5, 500.0 }, { 0.76, 1000.0 }, { 0.82, 1500.0 },
                                                               { 0.88, 3000.0 }, { 0.94, 6000.0 }, { 1.0, 20000.0 } } };
} // namespace

double timeMs (float v) noexcept
{
    const auto x = (double) clamp01 (v);
    for (std::size_t i = 1; i < timeLaw.size(); ++i)
        if (x <= timeLaw[i].first)
        {
            const auto [x0, y0] = timeLaw[i - 1];
            const auto [x1, y1] = timeLaw[i];
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }
    return 20000.0;
}

float timeValue (double ms) noexcept
{
    ms = std::clamp (ms, 0.0, 20000.0);
    for (std::size_t i = 1; i < timeLaw.size(); ++i)
        if (ms <= timeLaw[i].second)
        {
            const auto [x0, y0] = timeLaw[i - 1];
            const auto [x1, y1] = timeLaw[i];
            return (float) (x0 + (x1 - x0) * (ms - y0) / (y1 - y0));
        }
    return 1.0f;
}

int syncKind (float v) noexcept { return std::clamp ((int) std::floor (clamp01 (v) * 4.0f + 1e-4f) - 1, 0, 3); }
int noteIndex (float v) noexcept { return std::clamp ((int) std::floor (clamp01 (v) * 10.0f + 1e-4f), 0, 9); }

double noteBeats (int index, int kind) noexcept
{
    const auto beats = std::pow (2.0, (double) std::clamp (index, 0, 9) - 4.0); // 1/64 = 1/16 of a quarter … 8/1 = 32
    return kind == 2 ? beats * 1.5 : kind == 3 ? beats * 2.0 / 3.0 : beats;
}

double sideMs (const EffectParams& p, bool right, double bpm) noexcept
{
    const auto kind = syncKind (p[right ? syncR : syncL]);
    if (kind == 0)
        return timeMs (p[right ? timeR : timeL]);
    bpm = bpm > 1.0 ? bpm : 120.0;
    return std::min (20000.0, noteBeats (noteIndex (p[right ? noteR : noteL]), kind) * 60000.0 / bpm);
}

double feedbackGain (float v) noexcept { return 2.0 * clamp01 (v); }
double modRateHz (float v) noexcept { return 0.01 + 9.99 * std::pow ((double) clamp01 (v), 2.3277); }
double shiftHz (float v) noexcept { return 1000.0 * signedPow (bipolar (v), 5.0); }
double shiftDetuneHz (float v) noexcept { return 10.0 * signedPow (bipolar (v), 2.5); }
double pitchSemis (float v) noexcept { return 24.0 * bipolar (v); }
double lowCutHz (float v) noexcept { return 10.0 + 1990.0 * clamp01 (v); }
double highCutHz (float v) noexcept { return 200.0 + 19800.0 * clamp01 (v); }
Mode modeOf (float v) noexcept { return (Mode) std::clamp ((int) std::floor (clamp01 (v) * (float) modeCount), 0, modeCount - 1); }
float modeValue (Mode m) noexcept { return ((float) m + 0.5f) / (float) modeCount; }
Style styleOf (float v) noexcept { return (Style) std::clamp ((int) std::floor (clamp01 (v) * (float) styleCount), 0, styleCount - 1); }
float styleValue (Style s) noexcept { return ((float) s + 0.5f) / (float) styleCount; }

const char* modeName (Mode m) noexcept
{
    static const char* const names[modeCount] = { "Tape", "Lo-Fi", "Hi-Fi tape", "Bucket brigade", "Analog", "Digital", "Clean",
                                                  "Phaser", "Shifter", "Pitch", "Reverse", "Detune" };
    return names[std::clamp ((int) m, 0, modeCount - 1)];
}

namespace
{
const char* const styleNames[styleCount] = { "Single", "Ratio", "Quad", "Dual", "Ping-pong" };

class Echoform final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        const auto lineSamples = sr * 20.6 + 4096.0; // 20 s + modulation
        for (auto& c : ch)
        {
            c.line.allocate (lineSamples);
            c.pitch.prepare (sr);
            for (auto& a : c.diff)
                a.line.allocate (sr * 0.05);
        }
        reset();
    }

    void reset() noexcept override
    {
        std::uint32_t seed = 0x1234567u;
        for (auto& c : ch)
        {
            c.line.clear();
            c.pitch.reset();
            c.shifter.reset();
            for (auto& a : c.diff)
                a.line.clear();
            for (auto& a : c.phaser)
                a = {};
            c.shelf.reset();
            c.tapeLp1.reset();
            c.tapeLp2.reset();
            c.hifiLp.reset();
            c.hifiHp.reset();
            c.lowCut.reset();
            c.highCut.reset();
            c.held = 0.0f;
            c.holdPhase = 0.0;
            c.time = {};
            c.wow.seed (seed += 0x9e3779b9u);
            c.flutter1.seed (seed += 0x9e3779b9u);
            c.flutter2.seed (seed += 0x9e3779b9u);
            c.drift.seed (seed += 0x9e3779b9u);
            c.noise.s = seed += 0x9e3779b9u;
        }
        steep.reset();
        lfoPhase = 0.0;
        envelope = 0.0f;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (ch[0].line.buf.empty())
            prepare (sr);

        const auto md = modeOf (p[mode]);
        const auto st = styleOf (p[style]);
        float dryGain = 1.0f, wetGain = 0.0f;
        equalPowerMix (p[mix], dryGain, wetGain);

        // times: sync / ms, spread (±¼ of the spread on each side), style
        auto tL = sideMs (p, false, bpm);
        auto tR = sideMs (p, true, bpm);
        if (st == Style::single || st == Style::quad)
            tR = tL;
        else if (st == Style::ratio)
            tR = tL * (0.01 + 0.99 * (double) clamp01 (p[ratio]));
        const auto spreadMs = 20.0 * bipolar (p[spread]);
        tL = std::max (0.0, tL + spreadMs * 0.25);
        tR = std::max (0.0, tR - spreadMs * 0.25);

        const auto g = feedbackGain (p[feedback]);
        const auto ageV = clamp01 (p[age]);
        const auto driveGain = (float) std::pow (10.0, 24.0 * (double) clamp01 (p[drive]) / 20.0);
        const auto widthV = (float) bipolar (p[width]);
        const auto diffAmount = clamp01 (p[diffusion]);
        const auto diffScale = 0.25 + 0.75 * (double) clamp01 (p[diffSize]);
        const auto duck = clamp01 (p[ducking]);
        const auto rate = modRateHz (p[modRate]);
        const auto depth = (double) clamp01 (p[modDepth]);
        const auto wowV = (double) clamp01 (p[wow]), flutterV = (double) clamp01 (p[flutter]);
        const auto fShift = shiftHz (p[shift]), fDetune = shiftDetuneHz (p[shiftDetune]);
        const auto semis = pitchSemis (p[pitch]);
        const auto cents = 100.0 * bipolar (p[pitchDetune]);

        // character: the write-path filter / shaper and the modulation source
        configure (md, tL, ageV);
        for (auto& c : ch)
        {
            c.lowCut.set (lowCutHz (p[lowCut]), sr);
            // the measured high cut acts like a one-pole at about half its label (§3.3); it opens up fully at the top
            const auto hc = clamp01 (p[highCut]);
            const auto open = std::max (0.0f, (hc - 0.8f) / 0.2f);
            if (hc >= 0.999f)
                c.highCut.open();
            else
                c.highCut.set (highCutHz (hc) * (0.5 + 1.5 * (double) (open * open)), sr);
        }

        const auto quadEl = std::pow (2.0, -0.4 * bipolar (p[spacing]));
        const auto quadEr = std::pow (2.0, 0.4 * bipolar (p[spacing]));
        const auto envAttack = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
        const auto envRelease = (float) (1.0 - std::exp (-1.0 / (0.25 * sr)));
        static constexpr double diffMs[4] = { 4.3, 6.7, 9.1, 12.7 };

        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { l[i], r[i] };

            // modulation (ms): wow / flutter in the tape characters, an LFO in the others
            lfoPhase += rate / sr;
            lfoPhase -= std::floor (lfoPhase);
            double modMs[2] = { 0.0, 0.0 };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                switch (modulation)
                {
                    case ModSource::tape:
                        // measured: wow 100 % ≈ 14 c rms around 1.5–2.5 Hz, flutter 100 % ≈ 14 c rms at 7–15 Hz
                        modMs[c] = wowAmountMs * wowV * s.wow.next (4.0, sr)
                                   + flutterAmountMs * flutterV * 0.7 * (s.flutter1.next (18.0, sr) + s.flutter2.next (26.0, sr));
                        break;
                    case ModSource::sine:
                        modMs[c] = modAmountMs * depth * depth * std::sin (twoPi * (lfoPhase + (c == 1 ? 0.25 : 0.0)));
                        break;
                    case ModSource::random:
                        modMs[c] = 0.5 * modAmountMs * depth * depth * 0.5 * s.drift.next (rate * 2.0, sr);
                        break;
                    case ModSource::none: break;
                }
            }

            // targets in samples (tape glide when the time changes)
            const double target[2] = { tL * 0.001 * sr, tR * 0.001 * sr };
            double d[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                d[c] = std::max (2.0, s.time.next (target[c], 0.12, sr) + modMs[c] * 0.001 * sr);
            }

            // read the echoes (read path: shifter / pitch / reverse / phaser / detune)
            float echo[2], fbSig[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                float y = 0.0f;
                if (st == Style::quad)
                {
                    const auto e = c == 0 ? quadEl : quadEr;
                    float last = 0.0f;
                    for (int k = 1; k <= 4; ++k)
                    {
                        const auto v = s.line.at (std::max (2.0, d[c] * std::pow ((double) k / 4.0, e)));
                        y += 0.5f * v;
                        last = v;
                    }
                    fbSig[c] = last;
                }
                else if (md == Mode::reverse)
                {
                    y = reverseRead (s, d[c], std::pow (2.0, semis / 12.0));
                    fbSig[c] = y;
                }
                else
                {
                    // the pitch shifter's window delays the echo by half a window: read that much earlier
                    const auto early = (md == Mode::pitch || md == Mode::detune) ? s.pitch.window * 0.5 : 0.0;
                    y = s.line.at (std::max (2.0, d[c] - early));
                    fbSig[c] = y;
                }
                const auto sign = c == 0 ? 1.0 : -1.0;
                switch (md)
                {
                    case Mode::shifter:
                        y = s.shifter.process (y, fShift + sign * fDetune, sr);
                        break;
                    case Mode::pitch:
                        y = s.pitch.process (y, std::pow (2.0, (semis + sign * cents / 100.0) / 12.0));
                        break;
                    case Mode::detune:
                    {
                        const auto wobble = 0.2 * s.drift.next (1.0, sr);
                        y = s.pitch.process (y, std::pow (2.0, (sign * cents * (1.0 + wobble)) / 1200.0));
                        break;
                    }
                    case Mode::phaser:
                    {
                        const auto lfo = std::sin (twoPi * (lfoPhase + (c == 1 ? 0.25 : 0.0)));
                        const auto fc = std::clamp (200.0 * std::pow (8.0, (double) ageV) * std::pow (2.0, 2.0 * depth * lfo), 40.0, 0.4 * sr);
                        const auto t = std::tan (pi * fc / sr);
                        const auto a = (float) ((t - 1.0) / (t + 1.0));
                        auto u = y;
                        for (auto& ap : s.phaser)
                            u = ap.process (u, a);
                        y = 0.5f * (y + u);
                        break;
                    }
                    default: break;
                }
                if (st != Style::quad) // the quad style feeds back its last tap as read
                    fbSig[c] = y;
                echo[c] = y;
            }

            // loop inputs per style
            const auto mono = 0.5f * (in[0] + in[1]);
            float loopIn[2];
            if (st == Style::pingPong)
            {
                const auto half = (float) std::sqrt (g);
                loopIn[0] = mono + half * fbSig[1];
                loopIn[1] = half * fbSig[0];
            }
            else
            {
                loopIn[0] = in[0] + (float) g * fbSig[0];
                loopIn[1] = in[1] + (float) g * fbSig[1];
            }

            // write path: diffusion, character, cuts, a soft limit (feedback above 100 % stays bounded)
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                auto u = loopIn[c];
                if (diffAmount > 0.0f)
                    for (std::size_t k = 0; k < 4; ++k)
                        u = s.diff[k].process (u, diffMs[k] * diffScale * 0.001 * sr * (c == 1 ? 1.07 : 1.0), 0.65f * diffAmount);
                u = character (s, c, u, md, ageV, driveGain);
                u = s.highCut.lp (s.lowCut.hp (u));
                s.line.push (softLimit (u));
            }

            // ducking (not measured: a plain envelope-driven gain on the echoes)
            auto duckGain = 1.0f;
            if (duck > 0.0f)
            {
                const auto e = std::abs (mono);
                envelope += (e - envelope) * (e > envelope ? envAttack : envRelease);
                duckGain = 1.0f / (1.0f + 8.0f * duck * envelope);
            }

            // width (M / S of the echoes) and the equal-power mix
            const auto m = 0.5f * (echo[0] + echo[1]), sd = 0.5f * (echo[0] - echo[1]) * widthV;
            l[i] = dryGain * in[0] + wetGain * duckGain * (m + sd);
            r[i] = dryGain * in[1] + wetGain * duckGain * (m - sd);
        }
    }

private:
    enum class ModSource { none, tape, sine, random };

    struct Channel
    {
        Line line;
        PitchShifter pitch;
        Shifter shifter;
        std::array<Allpass, 4> diff;
        std::array<Ap1, 6> phaser;
        OnePole shelf, tapeLp1, tapeLp2, hifiLp, hifiHp, lowCut, highCut;
        RandomLfo wow, flutter1, flutter2, drift;
        Noise noise;
        Glide time;
        float held = 0.0f;
        double holdPhase = 0.0, revPhase = 0.0;
    };

    // reversed grains: two readers sweeping backwards through the last (1 + r)·D samples, Hann crossfade
    float reverseRead (Channel& s, double d, double r) noexcept
    {
        s.revPhase += 1.0 / std::max (64.0, d);
        s.revPhase -= std::floor (s.revPhase);
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            auto p = s.revPhase + 0.5 * k;
            p -= std::floor (p);
            const auto w = (float) std::sin (pi * p);
            y += w * w * s.line.at (2.0 + p * (1.0 + r) * d);
        }
        return y;
    }

    void configure (Mode m, double timeMsL, float ageV) noexcept
    {
        modAmountMs = 6.1;
        switch (m)
        {
            case Mode::tape:
            case Mode::lofi:
            case Mode::phaser:
            {
                // tape: the head bump / low-end loss (low shelf −5 dB), and a two-pole roll-off that falls with the
                // delay time (tape speed): ≈ 9 kHz at 100 ms, 5 kHz at 300 ms, 2.7 kHz at 1 s
                const auto fc = m == Mode::tape ? std::clamp (9000.0 * std::pow (std::max (1.0, timeMsL) / 100.0, -0.5), 1500.0, 10000.0) : 9000.0;
                for (auto& c : ch)
                {
                    c.shelf.set (350.0, sr);
                    c.tapeLp1.set (fc, sr);
                    c.tapeLp2.set (fc, sr);
                }
                shaper.set (m == Mode::lofi ? 0.9f : 0.95f, m == Mode::lofi ? 0.0f : 0.11f);
                modulation = m == Mode::tape ? ModSource::tape : (m == Mode::lofi ? ModSource::sine : ModSource::none);
                break;
            }
            case Mode::hifi:
            case Mode::shifter:
                for (auto& c : ch)
                {
                    c.hifiLp.set (13000.0, sr);
                    c.hifiHp.set (15.0, sr);
                }
                shaper.set (0.55f, m == Mode::hifi ? 0.17f : 0.0f);
                modulation = m == Mode::hifi ? ModSource::random : ModSource::none;
                break;
            case Mode::bucket:
                steep.set (1650.0, sr);
                shaper.set (0.6f, 0.05f);
                modulation = ModSource::sine;
                break;
            case Mode::analog:
                steep.set (3450.0, sr);
                shaper.set (1.525f, 0.0f);
                modulation = ModSource::sine;
                break;
            case Mode::digital:
            case Mode::pitch:
            case Mode::reverse:
            case Mode::detune:
                steep.set (8000.0, sr);
                shaper.set (0.375f, 0.0f);
                modulation = m == Mode::digital ? ModSource::sine : ModSource::none;
                break;
            case Mode::clean:
            {
                // age = a steep low-pass: off at 0, 5.3 kHz at 50 %, 3.3 kHz at 100 %
                const auto a = (double) ageV;
                const auto fc = a < 0.5 ? 5300.0 * std::pow (24000.0 / 5300.0, (0.5 - a) / 0.5) : 3300.0 * std::pow (5300.0 / 3300.0, 2.0 * (1.0 - a));
                steep.set (fc, sr);
                shaper.set (0.3f, 0.0f);
                modulation = ModSource::sine;
                modAmountMs = 10.4;
                break;
            }
        }
    }

    float character (Channel& s, int c, float u, Mode m, float ageV, float driveGain) noexcept
    {
        const auto drive = [this, driveGain] (float x) { return shaper (x * driveGain) / driveGain; };
        switch (m)
        {
            case Mode::tape:
            case Mode::phaser:
                u = drive (u); // saturation at the record head, then the tape's response
                u = 0.8f * (u - 0.44f * s.shelf.lp (u)); // −1.9 dB through the tape path, −5 dB low shelf
                return s.tapeLp2.lp (s.tapeLp1.lp (u));
            case Mode::lofi:
            {
                u = u - 0.44f * s.shelf.lp (u);
                u = s.tapeLp2.lp (s.tapeLp1.lp (u));
                u = crush (s, u, 16.0 - 10.0 * ageV, 1.0 + 5.0 * ageV);
                return drive (u);
            }
            case Mode::hifi:
            case Mode::shifter:
                return drive (s.hifiLp.lp (s.hifiHp.hp (u)));
            case Mode::bucket:
            case Mode::analog:
            {
                u = drive (steep.process (u, c));
                const auto level = m == Mode::bucket ? 1e-3f : 2.5e-4f; // −60 / −72 dBFS at full age (∝ age²)
                return u + level * ageV * ageV * 1.73f * s.noise.next();
            }
            case Mode::digital:
            case Mode::pitch:
            case Mode::reverse:
            case Mode::detune:
            {
                u = drive (steep.process (u, c));
                if (ageV > 0.0f)
                    u = crush (s, u, 24.0 - 14.0 * ageV, 1.0 + 1.5 * ageV * ageV);
                return u;
            }
            case Mode::clean:
                return ageV > 0.0f ? drive (steep.process (u, c)) : drive (u);
        }
        return u;
    }

    // sample-and-hold rate reduction and bit-depth reduction (lo-fi / digital age)
    static float crush (Channel& s, float u, double bits, double hold) noexcept
    {
        s.holdPhase += 1.0;
        if (s.holdPhase >= hold)
        {
            s.holdPhase -= hold;
            const auto q = std::pow (2.0, std::clamp (bits, 4.0, 24.0) - 1.0);
            s.held = (float) (std::round ((double) u * q) / q);
        }
        return s.held;
    }

    double sr = 48000.0;
    std::array<Channel, 2> ch;
    Butter8 steep;
    Shaper shaper;
    ModSource modulation = ModSource::tape;
    double modAmountMs = 6.1;
    double wowAmountMs = 1.15, flutterAmountMs = 0.2;
    double lfoPhase = 0.0;
    float envelope = 0.0f;
};
} // namespace
} // namespace echoform

// =====================================================================================================================
// Spiral Echo
// =====================================================================================================================
namespace spiral
{
double shiftHz (float v) noexcept { return 1000.0 * signedPow (bipolar (v), 5.0); }
double delayMs (float v) noexcept { return 0.01 + 999.99 * std::pow ((double) clamp01 (v), 3.322); }
float delayValue (double ms) noexcept { return (float) std::pow (std::clamp ((ms - 0.01) / 999.99, 0.0, 1.0), 1.0 / 3.322); }
int syncIndex (float v) noexcept { return std::clamp ((int) std::floor (clamp01 (v) * 23.0f), 0, 23); } // as measured: floor (23 v) in float

double syncBeats (int index) noexcept
{
    if (index <= 0 || index > 23)
        return 0.0;
    if (index <= 8)
        return std::pow (2.0, (double) index - 5.0); // 1/64 = 1/16 beat … 2/1 = 8 beats
    if (index <= 15)
        return 1.5 * std::pow (2.0, (double) (index - 8) - 5.0); // dotted 1/64 … 1/1
    return 2.0 / 3.0 * std::pow (2.0, (double) (index - 15) - 5.0); // triplet 1/64 … 2/1
}

double loopGain (float v) noexcept { return 1.5 * clamp01 (v); }
double lowCutHz (float v) noexcept { return 20.0 + 2980.0 * clamp01 (v); }
double highCutHz (float v) noexcept { return 50.0 + 14950.0 * clamp01 (v); }

namespace
{
class SpiralEcho final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        for (auto& c : ch)
            c.line.allocate (sr * 12.0);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& c : ch)
        {
            c.line.clear();
            c.shifter.reset();
            c.lo.reset();
            c.hi.reset();
        }
        time = {};
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (ch[0].line.buf.empty())
            prepare (sr);
        const auto wet = clamp01 (p[mix]), dry = 1.0f - wet; // linear crossfade (REA_rodada101 §4)
        const auto beats = syncBeats (syncIndex (p[sync]));
        const auto ms = beats > 0.0 ? beats * 60000.0 / (bpm > 1.0 ? bpm : 120.0) : delayMs (p[delay]);
        const auto target = std::clamp (ms * 0.001 * sr, 2.0, ch[0].line.maxDelay() - 4.0);
        const auto g = (float) loopGain (p[feedback]);
        const auto hz = shiftHz (p[shift]);
        const auto stereoMode = on (p[stereo]);
        for (auto& c : ch)
        {
            c.lo.set (lowCutHz (p[lowCut]), sr);
            c.hi.set (highCutHz (p[highCut]), sr);
        }
        for (int i = 0; i < n; ++i)
        {
            const auto d = time.next (target, 0.05, sr);
            float* io[2] = { l + i, r + i };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                const auto x = *io[c];
                // every pass around the loop shifts by +hz; in stereo the right output is the same repeat taken
                // with the opposite shift (measured: L 1100 1200 1300 …, R 900 1000 1100 … for +100 Hz)
                const auto y = s.line.at (d);
                float up = 0.0f, down = 0.0f;
                s.shifter.both (s.hi.lp (s.lo.hp (y)), hz, sr, up, down);
                s.line.push (x + g * std::tanh (up)); // the loop is bounded by a soft limiter
                *io[c] = dry * x + wet * (c == 1 && stereoMode ? down : up);
            }
        }
    }

private:
    struct Channel
    {
        Line line;
        Shifter shifter;
        OnePole lo, hi;
    };
    double sr = 48000.0;
    std::array<Channel, 2> ch;
    Glide time;
};
} // namespace
} // namespace spiral

// =====================================================================================================================
// Orbit
// =====================================================================================================================
namespace orbit
{
double rateHz (float v) noexcept { return 10.0 * signedPow (bipolar (v), 3.333); }
double depthMs (float v) noexcept { return 50.0 * std::pow ((double) clamp01 (v), 2.322); }
double manualMs (float v) noexcept { return 50.0 * signedPow (bipolar (v), 4.0); }
double feedbackGain (float v) noexcept { return 0.7 * signedPow (bipolar (v), 1.5); } // measured ≈ 0.22 at 50 %, 0.7 at 100 %
Mode modeOf (float v) noexcept { return (Mode) std::clamp ((int) std::floor (clamp01 (v) * (float) modeCount + 1e-3f) - 1, 0, modeCount - 1); }
float modeValue (Mode m) noexcept { return ((float) m + 1.0f) / (float) modeCount; }

const char* modeName (Mode m) noexcept
{
    static const char* const names[modeCount] = { "Rise", "Rise / fall", "Through-zero +", "Through-zero −", "Tri chorus", "Octo chorus",
                                                  "Doubler", "Drift rise", "Drift rise / fall", "Ensemble", "Symphonic" };
    return names[std::clamp ((int) m, 0, modeCount - 1)];
}

namespace
{
class Orbit final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        for (auto& c : ch)
            c.line.allocate (sr * 0.25);
        reset();
    }

    void reset() noexcept override
    {
        std::uint32_t seed = 0xabcdef1u;
        for (auto& c : ch)
        {
            c.line.clear();
            for (auto& rl : c.random)
                rl.seed (seed += 0x9e3779b9u);
            c.fb = 0.0f;
        }
        phase = fast = 0.0;
        speedRandom.seed (0x51ed27u);
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (ch[0].line.buf.empty())
            prepare (sr);
        float dryGain = 1.0f, wetGain = 0.0f;
        equalPowerMix (p[mix], dryGain, wetGain);
        const auto m = modeOf (p[mode]);
        const auto hz = rateHz (p[rate]);
        const auto dm = depthMs (p[depth]) * 0.001 * sr;
        const auto base = (0.25 + std::abs (manualMs (p[manual]))) * 0.001 * sr;
        const auto g = (float) feedbackGain (p[feedback]);
        rateNow = hz;

        for (int i = 0; i < n; ++i)
        {
            auto step = hz / sr;
            if (m == Mode::driftRise || m == Mode::driftRiseFall)
                step *= 1.0 + 0.5 * speedRandom.next (std::max (0.05, std::abs (hz)), sr) / 2.1;
            phase += step;
            phase -= std::floor (phase);
            fast += 7.3 * step;
            fast -= std::floor (fast);

            const float in[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                const auto wet = voices (s, c, m, base, dm);
                s.line.push (in[c] + g * s.fb);
                s.fb = wet;
                const auto out = dryGain * in[c] + wetGain * wet;
                (c == 0 ? l : r)[i] = out;
            }
        }
    }

private:
    struct Channel
    {
        Line line;
        std::array<RandomLfo, 4> random;
        float fb = 0.0f;
    };

    static double frac (double x) noexcept { return x - std::floor (x); }
    static double sine (double p) noexcept { return std::sin (twoPi * p); }

    float voices (Channel& s, int c, Mode m, double base, double dm) noexcept
    {
        const auto at = [&s] (double d) { return s.line.at (std::max (2.0, d)); };
        const auto off = c == 0 ? 0.0 : 0.25;
        switch (m)
        {
            case Mode::rise:
            case Mode::driftRise:
            {
                // barber pole: two taps ramping across the range, each faded in / out at the ends (sin window)
                const auto lo = m == Mode::rise ? 0.25 : 0.02, span = m == Mode::rise ? 0.5 : 0.96;
                float y = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const auto q = frac (phase + off + 0.5 * k);
                    const auto w = (float) std::sin (pi * q);
                    y += w * w * at (base + dm * (lo + span * (1.0 - q)));
                }
                return y;
            }
            case Mode::riseFall:
            case Mode::driftRiseFall:
            {
                const auto q = frac (phase + off);
                const auto tri = 1.0 - std::abs (2.0 * q - 1.0);
                const auto lo = m == Mode::riseFall ? 0.25 : 0.02, span = m == Mode::riseFall ? 0.5 : 0.96;
                return at (base + dm * (lo + span * tri));
            }
            case Mode::throughZeroPlus:
            case Mode::throughZeroMinus:
            {
                // a fixed tap at the top of the sweep and one sweeping up to (and just past) it: through-zero
                const auto fixed = base + 2.36 * dm;
                const auto moving = base + dm * (0.65 + 1.75 * (0.5 + 0.5 * sine (phase + (c == 0 ? 0.0 : 0.02))));
                const auto sign = m == Mode::throughZeroMinus ? -1.0f : 1.0f;
                return 0.5f * at (fixed) + sign * 0.5f * at (moving);
            }
            case Mode::tri:
            {
                float y = 0.0f;
                for (int k = 0; k < 3; ++k)
                    y += at (base + dm * (0.5 + 0.5 * sine (phase + k / 3.0 + (c == 0 ? 0.0 : 1.0 / 6.0))));
                return y * 0.57735f;
            }
            case Mode::octo:
            {
                float y = 0.0f;
                for (int k = 0; k < 4; ++k)
                    y += at (base + dm * (0.15 + 0.7 * (0.5 + 0.5 * sine (phase + (2 * k + c) / 8.0))));
                return 0.5f * y;
            }
            case Mode::doubler:
                return at (base + dm * (0.5 + 0.4 / 2.1 * std::clamp ((double) s.random[0].next (0.3 + std::abs (rateNow), sr), -2.1, 2.1)));
            case Mode::ensemble:
            {
                float y = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const auto q = phase + (2 * k + c) / 6.0;
                    y += at (base + dm * (0.5 + 0.35 * sine (q) + 0.15 * sine (fast + q)));
                }
                return 0.70711f * y;
            }
            case Mode::symphonic:
            {
                float y = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    const auto q = phase + (2 * k + c) / 4.0;
                    const auto rnd = std::clamp ((double) s.random[(std::size_t) k + 1].next (0.7, sr) / 2.1, -1.0, 1.0);
                    y += at (base + dm * (0.5 + 0.3 * sine (q) + 0.18 * rnd));
                }
                return 0.70711f * y;
            }
        }
        return 0.0f;
    }

    double sr = 48000.0;
    std::array<Channel, 2> ch;
    RandomLfo speedRandom;
    double phase = 0.0, fast = 0.0, rateNow = 0.5;
};
} // namespace
} // namespace orbit

// =====================================================================================================================
// Lattice Mod
// =====================================================================================================================
namespace lattice
{
int tapCount (float v) noexcept
{
    static constexpr int counts[10] = { 1, 1, 2, 3, 3, 3, 4, 8, 8, 16 };
    return counts[std::clamp ((int) std::floor (clamp01 (v) * 24.0f + 1e-4f), 0, 9)];
}

double delayMs (float v) noexcept { return 1000.0 * clamp01 (v); }

double tapMs (const EffectParams& p, int tap, int taps, bool right) noexcept
{
    const auto d = delayMs (p[delay]) * (on (p[halfSpeed]) ? 2.0 : 1.0);
    auto t = d * (1.0 - (double) clamp01 (p[spread]) * (double) tap / (double) std::max (1, taps));
    const auto skewV = bipolar (p[skew]);
    if (! right && skewV < 0.0)
        t *= 1.0 + 0.5 * skewV;
    if (right && skewV > 0.0)
        t *= 1.0 - 0.5 * skewV;
    // random: a fixed scatter of each tap by up to half the tap spacing
    if (p[random] > 0.0f && taps > 1)
    {
        const auto h = std::sin ((double) (tap * 7919 + (right ? 104729 : 0)) * 12.9898) * 43758.5453;
        const auto u = (h - std::floor (h)) * 2.0 - 1.0;
        t += u * 0.5 * (double) clamp01 (p[random]) * d * (double) clamp01 (p[spread]) / (double) taps;
    }
    return std::max (0.0, t);
}

double tapLevel (const EffectParams& p, int tap, int taps) noexcept
{
    auto gain = std::pow (10.0, 12.0 * bipolar (p[tapGain]) / 20.0) / std::sqrt ((double) std::max (1, taps));
    if (taps > 1)
    {
        const auto pos = 1.0 - 2.0 * (double) tap / (double) (taps - 1); // +1 at the longest tap, −1 at the shortest
        gain *= std::pow (10.0, 6.0 * bipolar (p[slope]) * pos / 20.0);
    }
    return -gain; // the measured wet is polarity-inverted (REA_rodada101 §6)
}

namespace
{
double syncBeats (int index) noexcept
{
    static constexpr double beats[18] = { 0.0, 1.0 / 16, 1.0 / 12, 3.0 / 32, 1.0 / 8, 1.0 / 6, 3.0 / 16, 1.0 / 4, 1.0 / 3,
                                          3.0 / 8, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 4.0 };
    return beats[std::clamp (index, 0, 17)];
}

class LatticeMod final : public Effect
{
public:
    void prepare (double rate) noexcept override
    {
        sr = rate;
        for (auto& c : ch)
        {
            c.line.allocate (sr * 6.0);
            for (auto& a : c.diff)
                a.line.allocate (sr * 1.1);
        }
        reset();
    }

    void reset() noexcept override
    {
        std::uint32_t seed = 0x7a11ce5u;
        for (auto& c : ch)
        {
            c.line.clear();
            for (auto& a : c.diff)
                a.line.clear();
            for (auto& rl : c.detune)
                rl.seed (seed += 0x9e3779b9u);
            c.diffLfo.seed (seed += 0x9e3779b9u);
            c.noise.s = seed += 0x9e3779b9u;
            c.lo.reset();
            c.hi.reset();
            c.dark.reset();
            c.xover.reset();
            c.fb = 0.0f;
        }
        vibPhase = 0.0;
        base = {};
    }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (ch[0].line.buf.empty())
            prepare (sr);
        float dryGain = 1.0f, wetGain = 0.0f;
        equalPowerMix (p[mix], dryGain, wetGain);
        const auto nTaps = tapCount (p[lattice::taps]);
        const auto half = on (p[halfSpeed]);
        const auto syncB = syncBeats ((int) std::lround (clamp01 (p[sync]) * 17.0f));
        const auto delayScale = syncB > 0.0 && delayMs (p[delay]) > 0.0
                                    ? (syncB * 60000.0 / (bpm > 1.0 ? bpm : 120.0)) * (half ? 2.0 : 1.0) / (delayMs (p[delay]) * (half ? 2.0 : 1.0))
                                    : 1.0;
        std::array<std::array<double, 16>, 2> tap {};
        std::array<double, 16> gains {};
        for (int k = 0; k < nTaps; ++k)
        {
            tap[0][(std::size_t) k] = tapMs (p, k, nTaps, false) * delayScale * 0.001 * sr;
            tap[1][(std::size_t) k] = tapMs (p, k, nTaps, true) * delayScale * 0.001 * sr;
            gains[(std::size_t) k] = tapLevel (p, k, nTaps);
        }
        const auto master = (double) clamp01 (p[depth]) * (1.0 + 99.0 * (double) clamp01 (p[overMod]));
        const auto detuneHz = (0.01 + 9.99 * (double) clamp01 (p[detuneRate])) * (half ? 0.5 : 1.0);
        const auto vibHz = (0.05 + 19.95 * (double) clamp01 (p[vibratoRate])) * (half ? 0.5 : 1.0);
        // depth in delay samples: the pitch swing stays the same at any rate (detune ±25 c, vibrato ±174 c at 100 %)
        const auto detuneAmp = std::min (0.05 * sr, master * clamp01 (p[detuneDepth]) * (std::pow (2.0, 25.0 / 1200.0) - 1.0) / (twoPi * detuneHz) * sr);
        const auto vibAmp = std::min (0.02 * sr, master * clamp01 (p[vibratoDepth]) * (std::pow (2.0, 174.0 / 1200.0) - 1.0) / (twoPi * vibHz) * sr);
        const auto g = (float) clamp01 (p[feedback]);
        const auto theta = pi * (double) clamp01 (p[rotate]);
        const auto cr = (float) std::cos (theta), sn = (float) std::sin (theta);
        const auto widthV = 2.0f * clamp01 (p[width]);
        const auto useDiff = on (p[diffOn]);
        const auto diffG = 0.7f * clamp01 (p[diffusion]);
        const auto diffSizeMs = 10.0 + 490.0 * (double) clamp01 (p[diffSize]);
        const auto diffRate = 0.01 + 9.99 * (double) clamp01 (p[diffModRate]);
        const auto diffDepth = (double) clamp01 (p[diffModDepth]);
        const auto useDrive = on (p[driveOn]), post = on (p[drivePost]);
        const auto inGain = (float) std::pow (10.0, 24.0 * clamp01 (p[driveIn]) / 20.0);
        const auto outGain = (float) std::pow (10.0, (-24.0 + 24.0 * clamp01 (p[driveOut])) / 20.0);
        const auto noiseLevel = (float) std::pow (10.0, (-120.0 + 90.0 * clamp01 (p[noise])) / 20.0);
        const auto smoothS = (1.0 + 999.0 * (double) clamp01 (p[smoothing])) * 0.001;
        const auto isDark = on (p[colour]);
        static constexpr double diffRatios[4] = { 0.131, 0.217, 0.293, 0.359 };
        for (auto& c : ch)
        {
            c.lo.set (10.0 + 1990.0 * (double) clamp01 (p[lowCut]), sr);
            if (p[highCut] >= 0.999f)
                c.hi.open();
            else
                c.hi.set (100.0 + 19900.0 * (double) clamp01 (p[highCut]), sr);
            if (isDark) // measured "dark": about a one-pole at 13 kHz; "bright" is flat
                c.dark.set (13000.0, sr);
            else
                c.dark.open();
            c.xover.set (10.0 + 1990.0 * (double) clamp01 (p[xover]), sr);
        }
        const auto driveFn = [inGain, outGain] (float x) { return outGain * std::tanh (inGain * x); };

        for (int i = 0; i < n; ++i)
        {
            const auto scale = base.next (1.0, smoothS, sr); // glides the taps after a change (smoothing time)
            vibPhase += vibHz / sr;
            vibPhase -= std::floor (vibPhase);
            float wet[2];
            const float in[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                const auto sign = c == 0 ? 1.0 : -1.0;
                double y = 0.0;
                for (int k = 0; k < nTaps; ++k)
                {
                    const auto det = detuneAmp > 0.0 ? detuneAmp * (double) s.detune[(std::size_t) k].next (detuneHz, sr) / 2.1 : 0.0;
                    const auto vib = vibAmp > 0.0 ? vibAmp * std::sin (twoPi * (vibPhase + (double) k / (double) nTaps)) : 0.0;
                    const auto d = tap[(std::size_t) c][(std::size_t) k] * scale + std::abs (det) + vibAmp + sign * (det + vib) * 0.5;
                    y += gains[(std::size_t) k] * (double) s.line.at (std::max (2.0, d));
                }
                auto v = (float) y;
                v = s.dark.lp (s.hi.lp (s.lo.hp (v)));
                if (useDrive && post)
                    v = driveFn (v) + noiseLevel * s.noise.next();
                wet[c] = v;
            }
            // feedback, rotated between the channels
            const auto fl = cr * wet[0] - sn * wet[1];
            const auto fr = sn * wet[0] + cr * wet[1];
            const float fbv[2] = { fl, fr };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(std::size_t) c];
                auto x = in[c];
                if (useDrive && ! post)
                    x = driveFn (x) + noiseLevel * s.noise.next();
                if (useDiff && diffG > 0.0f)
                {
                    const auto mod = 1.0 + 0.02 * diffDepth * (double) s.diffLfo.next (diffRate, sr) / 2.1;
                    for (std::size_t k = 0; k < 4; ++k)
                        x = s.diff[k].process (x, diffSizeMs * diffRatios[k] * mod * (c == 1 ? 1.05 : 1.0) * 0.001 * sr, diffG);
                }
                s.line.push (x + g * softLimit (fbv[c]));
            }
            // spatial crossover (the side below it is removed) and width
            const auto m = 0.5f * (wet[0] + wet[1]);
            auto sd = 0.5f * (wet[0] - wet[1]);
            sd = ch[0].xover.hp (sd) * widthV;
            l[i] = dryGain * in[0] + wetGain * (m + sd);
            r[i] = dryGain * in[1] + wetGain * (m - sd);
        }
    }

private:
    struct Channel
    {
        Line line;
        std::array<Allpass, 4> diff;
        std::array<RandomLfo, 16> detune;
        RandomLfo diffLfo;
        Noise noise;
        OnePole lo, hi, dark, xover;
        float fb = 0.0f;
    };
    double sr = 48000.0;
    std::array<Channel, 2> ch;
    double vibPhase = 0.0;
    Glide base;
};
} // namespace
} // namespace lattice

// =====================================================================================================================
// registry
// =====================================================================================================================
const EffectInfo& echoformInfo()
{
    static const EffectInfo info { "Echoform",
                                   { { "Mix", 0.5f }, { "Style", 0.1f }, { "Sync L", 0.25f }, { "Note L", 0.45f }, { "Time L", 0.3f },
                                     { "Sync R", 0.25f }, { "Note R", 0.45f }, { "Time R", 0.3f }, { "Spread", 0.5f }, { "Spacing", 0.5f },
                                     { "Ratio", 0.6141414f }, { "Feedback", 0.35f }, { "Width", 1.0f }, { "Drive", 0.0f }, { "Age", 0.5f },
                                     { "Diffusion", 0.0f }, { "Diffusion size", 1.0f }, { "Low cut", 0.0f }, { "High cut", 1.0f },
                                     { "Mod rate", 0.2738341f }, { "Mod depth", 0.5f }, { "Wow", 0.5f }, { "Flutter", 0.5f },
                                     { "Shift", 0.5f }, { "Shift detune", 0.58f }, { "Pitch", 0.5f }, { "Pitch detune", 0.5f },
                                     { "Mode", 0.5f / 12.0f }, { "Ducking", 0.0f } } };
    return info;
}

const EffectInfo& spiralInfo()
{
    static const EffectInfo info { "Spiral Echo",
                                   { { "Mix", 0.5f }, { "Shift", 0.5f }, { "Delay", 0.5f }, { "Sync", 0.0f }, { "Feedback", 0.5f },
                                     { "Low cut", 0.0604027f }, { "High cut", 0.3311037f }, { "Stereo", 1.0f } } };
    return info;
}

const EffectInfo& orbitInfo()
{
    static const EffectInfo info { "Orbit", { { "Mix", 0.5f }, { "Rate", 0.626f }, { "Depth", 0.5f }, { "Feedback", 0.5f },
                                              { "Manual", 0.5f }, { "Mode", 1.0f / 11.0f } } };
    return info;
}

const EffectInfo& latticeInfo()
{
    static const EffectInfo info { "Lattice Mod",
                                   { { "Mix", 0.5f }, { "Depth", 0.5f }, { "Width", 0.5f }, { "Delay", 0.01f }, { "Feedback", 0.0f },
                                     { "Spread", 0.5f }, { "Slope", 0.5f }, { "Skew", 0.5f }, { "Random", 0.0f }, { "Tap gain", 0.5f },
                                     { "Diffusion", 0.5f }, { "Diffusion size", 0.0204082f }, { "Diffusion rate", 0.049049f },
                                     { "Diffusion depth", 0.5f }, { "Low cut", 0.0f }, { "High cut", 1.0f }, { "Crossover", 0.1457286f },
                                     { "Detune rate", 0.049049f }, { "Detune depth", 0.5f }, { "Vibrato rate", 0.2982456f },
                                     { "Vibrato depth", 0.0f }, { "Over-mod", 0.0f }, { "Drive in", 0.375f }, { "Drive out", 0.75f },
                                     { "Noise", 0.3333333f }, { "Rotate", 0.5f }, { "Smoothing", 0.0990991f }, { "Colour", 0.0f },
                                     { "Sync", 0.0f }, { "Taps", 6.5f / 24.0f }, { "Diffusion on", 0.0f }, { "Drive on", 0.0f },
                                     { "Drive post", 1.0f }, { "Half speed", 0.0f } } };
    return info;
}

bool isSuiteKind (EffectKind kind) noexcept
{
    return kind == EffectKind::echoform || kind == EffectKind::spiralEcho || kind == EffectKind::orbit || kind == EffectKind::latticeMod;
}

std::unique_ptr<Effect> make (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::echoform: return std::make_unique<echoform::Echoform>();
        case EffectKind::spiralEcho: return std::make_unique<spiral::SpiralEcho>();
        case EffectKind::orbit: return std::make_unique<orbit::Orbit>();
        case EffectKind::latticeMod: return std::make_unique<lattice::LatticeMod>();
        default: return nullptr;
    }
}

std::string paramText (EffectKind kind, int param, float v)
{
    v = clamp01 (v);
    if (kind == EffectKind::echoform)
    {
        using namespace echoform;
        switch (param)
        {
            case mix: case age: case diffSize: case modDepth: case wow: case flutter: case ducking: return pct (v * 100.0);
            case style: return styleNames[(int) styleOf (v)];
            case syncL: case syncR:
            {
                static const char* const kinds[4] = { "ms", "note", "dotted", "triplet" };
                return kinds[syncKind (v)];
            }
            case noteL: case noteR: return noteNames[noteIndex (v)];
            case timeL: case timeR: return msText (timeMs (v));
            case spread: return fmt ("%+.1f ms", 20.0 * bipolar (v));
            case spacing: case width: return fmt ("%+.0f%%", 100.0 * bipolar (v));
            case ratio: return fmt ("%.1f%%", 1.0 + 99.0 * v);
            case feedback: return pct (200.0 * v);
            case drive: return fmt ("%.1f dB", 24.0 * v);
            case diffusion: return v <= 0.0f ? "off" : pct (v * 100.0);
            case lowCut: return hzText (lowCutHz (v));
            case highCut: return hzText (highCutHz (v));
            case modRate: return fmt ("%.2f Hz", modRateHz (v));
            case shift: return fmt ("%+.1f Hz", shiftHz (v));
            case shiftDetune: return fmt ("%+.2f Hz", shiftDetuneHz (v));
            case pitch: return fmt ("%+.2f st", pitchSemis (v));
            case pitchDetune: return fmt ("%+.0f c", 100.0 * bipolar (v));
            case mode: return modeName (modeOf (v));
            default: return fmt ("%.2f", v);
        }
    }
    if (kind == EffectKind::spiralEcho)
    {
        using namespace spiral;
        switch (param)
        {
            case mix: case feedback: return pct (v * 100.0);
            case shift: return fmt ("%+.2f Hz", shiftHz (v));
            case delay: return msText (delayMs (v));
            case sync:
            {
                const auto k = syncIndex (v);
                if (k == 0)
                    return "off";
                const auto group = k <= 8 ? 0 : k <= 15 ? 1 : 2;
                const auto idx = k <= 8 ? k - 1 : k <= 15 ? k - 9 : k - 16;
                return std::string (noteNames[idx]) + (group == 1 ? "." : group == 2 ? "T" : "");
            }
            case lowCut: return hzText (lowCutHz (v));
            case highCut: return hzText (highCutHz (v));
            case stereo: return on (v) ? "stereo" : "mono";
            default: return fmt ("%.2f", v);
        }
    }
    if (kind == EffectKind::orbit)
    {
        using namespace orbit;
        switch (param)
        {
            case mix: return pct (v * 100.0);
            case rate: return fmt ("%+.2f Hz", rateHz (v));
            case depth: return msText (depthMs (v));
            case feedback: return fmt ("%+.0f%%", 100.0 * bipolar (v));
            case manual: return fmt ("%+.2f ms", manualMs (v));
            case mode: return modeName (modeOf (v));
            default: return fmt ("%.2f", v);
        }
    }
    if (kind == EffectKind::latticeMod)
    {
        using namespace lattice;
        switch (param)
        {
            case mix: case depth: case feedback: case spread: case random: case diffusion: case diffModDepth: case rotate:
            case detuneDepth: case vibratoDepth: return pct (v * 100.0);
            case width: return pct (v * 200.0);
            case delay: return msText (delayMs (v));
            case slope: case skew: return fmt ("%+.0f%%", 100.0 * bipolar (v));
            case tapGain: return fmt ("%+.1f dB", 12.0 * bipolar (v));
            case diffSize: return msText (10.0 + 490.0 * v);
            case diffModRate: case detuneRate: return fmt ("%.2f Hz", 0.01 + 9.99 * v);
            case vibratoRate: return fmt ("%.2f Hz", 0.05 + 19.95 * v);
            case lowCut: case xover: return hzText (10.0 + 1990.0 * v);
            case highCut: return hzText (100.0 + 19900.0 * v);
            case overMod: return fmt ("%.1fx", 1.0 + 99.0 * v);
            case driveIn: return fmt ("%.1f dB", 24.0 * v);
            case driveOut: return fmt ("%.1f dB", -24.0 + 24.0 * v);
            case noise: return fmt ("%.1f dB", -120.0 + 90.0 * v);
            case smoothing: return msText (1.0 + 999.0 * v);
            case colour: return on (v) ? "dark" : "bright";
            case sync:
            {
                static const char* const names[18] = { "off", "1/64", "1/32T", "1/64.", "1/32", "1/16T", "1/32.", "1/16", "1/8T",
                                                       "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1" };
                return names[std::clamp ((int) std::lround (v * 17.0f), 0, 17)];
            }
            case taps: return fmt ("%.0f", (double) tapCount (v));
            case diffOn: case driveOn: return on (v) ? "on" : "off";
            case drivePost: return on (v) ? "post" : "pre";
            case halfSpeed: return on (v) ? "1/2x" : "1x";
            default: return fmt ("%.2f", v);
        }
    }
    return fmt ("%.2f", v);
}
} // namespace lynx::echosuite
