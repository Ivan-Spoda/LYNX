#include "core/RhythmPan.h"

#include <algorithm>
#include <cmath>

namespace lynx::rhythmpan
{
namespace
{
constexpr double pi = 3.141592653589793;

// [EXEC] the trial's passband colour after its latency is compensated (white noise at −40 dBFS, centre pan, digital
// mode; least-squares causal fit, scaled to unity below 1 kHz: within 0.1 dB to 3 kHz, 0.3 dB / 1.1° to 14 kHz,
// REA_rodada136 §2.3)
constexpr int firTaps = 16;
constexpr float fir[firTaps] { 0.674099823f, 0.504901329f, -0.249933016f, 0.096115708f, -0.006222688f, -0.023875790f, -0.011813289f, 0.035777016f, -0.023361458f, -0.008869098f, 0.025607335f, -0.016960316f, -0.005317371f, 0.019541821f, -0.017288454f, 0.007071806f };

// [EXEC] Analog mode: a first-order low-frequency section (zero −1.84 Hz, pole 9.18 Hz, gain 1.0032: fitted on the
// analog / digital response ratio 3 … 200 Hz) and the input saturation y = s(x + b) − s(b),
// s(v) = v / (1 + |v / c|^p)^(1/p), c 0.851, p 1.976, b 0.093, scaled to unity at low level (1 kHz stairs −40 … +6
// dBFS: level within 0.03 dB to −20 dBFS, 0.25 dB above; H2 / H3 within 2–4 dB)
constexpr double lfZeroHz = -1.839, lfPoleHz = 9.180, lfGain = 1.0032;
constexpr double satC = 0.8510, satP = 1.9762, satB = 0.0929, satGain = 1.01899; // satGain = 1 / s'(b): unity at low level

// [EXEC] the "hot" analog stage of some states (1 kHz / 2 kHz stairs and bursts, almost no harmonics): a linked RMS
// detector (3.19 ms), soft-knee (5.28 dB) 2.21:1 gain computer above −11.22 dB (detector level re a full-scale sine),
// the reduction smoothed with a 4.88 ms attack and an immediate release (0.2 dB rms on the bursts)
constexpr double hotRmsMs = 3.19, hotAttackMs = 4.88, hotThreshold = -11.22, hotKnee = 5.28, hotRatio = 2.209;

// [EXEC] round 150: PanMan's Tweak envelope (OffsetMod +90°, Width 0, 1 kHz stairs and steps, REA_rodada150a §6): the
// modulation follows the input before Input / Output gain, 0 below −20 dBFS (a sine's peak), linear in amplitude to
// full at −9 dBFS; a step to −6 dBFS reaches 90 % in ≈ 50 ms, falls to half in ≈ 200 ms — fitted (2 % rms) by a 1 ms
// mean square, then 21.3 ms attack / 291.5 ms release on the peak-equivalent level
constexpr double envMeanMs = 1.0, envAttackMs = 21.3, envReleaseMs = 291.5;

double side (double v) noexcept { return v / std::pow (1.0 + std::pow (std::abs (v / satC), satP), 1.0 / satP); }
const double sideB = side (satB);

constexpr double patternValues[2][maxSteps] { { 1.0, -1.0 }, { -1.0, -0.5, 0.0, 0.5, 1.0, 0.5, 0.0, -0.5 } };
constexpr int patternSteps[2] { 2, 8 };
} // namespace

double rateHz (float v) noexcept { return 0.01 * std::pow (1000.0, std::clamp ((double) v, 0.0, 1.0)); }
float rateValue (double hz) noexcept { return (float) std::clamp (std::log10 (std::max (hz, 1e-6) / 0.01) / 3.0, 0.0, 1.0); }
double beats (float v) noexcept { return std::pow (2.0, -4.0 + 10.0 * std::clamp ((double) v, 0.0, 1.0)); }
float beatsValue (double b) noexcept { return (float) std::clamp ((std::log2 (std::max (b, 1e-6)) + 4.0) / 10.0, 0.0, 1.0); }

void Engine::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    set (s);
    reset();
}

void Engine::reset() noexcept
{
    shZ = {};
    rms = gr = 0.0;
    for (auto& h : hist)
        h.fill (0.0f);
    histPos = 0;
    envMs = env = envAmount = extraPhase = 0.0;
}

void Engine::set (const Settings& settings) noexcept
{
    s = settings;
    const int p = std::clamp (s.pattern, 0, 1);
    steps = patternSteps[p];
    for (int i = 0; i < maxSteps; ++i)
        values[(size_t) i] = patternValues[p][i] * s.amount;
    outGain = std::pow (10.0, s.outputDb / 20.0);
    rmsA = std::exp (-1.0 / (hotRmsMs * 0.001 * sr));
    attA = std::exp (-1.0 / (hotAttackMs * 0.001 * sr));
    msA = std::exp (-1.0 / (envMeanMs * 0.001 * sr));
    envAtt = std::exp (-1.0 / (envAttackMs * 0.001 * sr));
    envRel = std::exp (-1.0 / (envReleaseMs * 0.001 * sr));
    // bilinear transform of g (s + wz) / (s + wp)
    const double k = 2.0 * sr, wz = 2.0 * pi * lfZeroHz, wp = 2.0 * pi * lfPoleHz;
    shB0 = lfGain * (k + wz) / (k + wp);
    shB1 = lfGain * (wz - k) / (k + wp);
    shA1 = (wp - k) / (k + wp);
}

float Engine::analogStage (float x, int c) noexcept
{
    const double y = shB0 * x + shZ[(size_t) c];
    shZ[(size_t) c] = shB1 * x - shA1 * y;
    return (float) (satGain * (side (y + satB) - sideB));
}

void Engine::process (float* left, float* right, int n, double beatPos, double bpm) noexcept
{
    const double bps = std::max (bpm, 1.0) / 60.0;
    // the pattern's phase: free = seconds since the song start × rate; synced = beats / period, both + the start phase
    const double cyclesPerSample = s.sync ? bps / sr / std::max (s.beats, 1e-6) : s.rateHz / sr;
    double phase = s.phase + (s.sync ? beatPos / std::max (s.beats, 1e-6) : beatPos / bps * s.rateHz) - s.feel / steps;
    const bool tweak = s.rateMod != 0.0 || s.offsetMod != 0.0 || s.widthMod != 0.0;
    const double thr = std::pow (10.0, s.envThresholdDb / 20.0), full = std::pow (10.0, (s.envThresholdDb + std::max (0.1, s.envRangeDb)) / 20.0);
    // the glide: `smoothing` of a step, at least 55 samples at 48 kHz (the trial's ramp with Smoothing 0)
    const double stepSamples = 1.0 / std::max (cyclesPerSample * steps, 1e-12);
    const double glide = std::max (s.smoothing, std::min (1.0, 55.0 * sr / 48000.0 / stepSamples));
    for (int i = 0; i < n; ++i, phase += cyclesPerSample)
    {
        float a = left != nullptr ? left[i] : 0.0f, b = right != nullptr ? right[i] : 0.0f;
        double offset = s.offset, width = s.width;
        if (tweak) // round 150: the Tweak page's envelope follower (the input as it comes in)
        {
            envMs = (double) (a * a + b * b) + msA * (envMs - (double) (a * a + b * b)); // 2 × the mean square per channel
            const double lvl = std::sqrt (envMs);                                           // a sine's peak
            env = lvl + (lvl > env ? envAtt : envRel) * (env - lvl);
            envAmount = std::clamp ((env - thr) / (full - thr), 0.0, 1.0);
            offset = std::clamp (offset + s.offsetMod * envAmount, -105.0, 105.0);
            width = std::clamp (width + s.widthMod * envAmount, 0.0, 210.0);
            if (s.rateMod != 0.0)
                extraPhase += cyclesPerSample * (std::pow (2.0, s.rateMod * envAmount) - 1.0);
        }
        if (s.analog == 1)
        {
            a = analogStage (a, 0);
            b = analogStage (b, 1);
        }
        else if (s.analog == 2)
        {
            const double ya = shB0 * a + shZ[0], yb = shB0 * b + shZ[1];
            shZ[0] = shB1 * a - shA1 * ya;
            shZ[1] = shB1 * b - shA1 * yb;
            const double pw = ya * ya + yb * yb; // linked: 2 · mean square of the pair (a sine's amplitude²)
            rms = pw + rmsA * (rms - pw);
            const double level = 10.0 * std::log10 (rms + 1e-20);
            const double x = (level - hotThreshold) / hotKnee;
            const double target = (1.0 - 1.0 / hotRatio) * hotKnee * (x > 30.0 ? x : std::log1p (std::exp (x)));
            gr = target > gr ? target + attA * (gr - target) : target;
            const double g = std::pow (10.0, -gr / 20.0);
            a = (float) (ya * g);
            b = (float) (yb * g);
        }
        hist[0][(size_t) histPos] = a;
        hist[1][(size_t) histPos] = b;
        double fa = 0.0, fb = 0.0;
        for (int j = 0; j < firTaps; ++j)
        {
            const auto idx = (size_t) ((histPos - j) & 31);
            fa += fir[j] * hist[0][idx];
            fb += fir[j] * hist[1][idx];
        }
        histPos = (histPos + 1) & 31;
        // the step position with its glide
        const double ph = phase + extraPhase - std::floor (phase + extraPhase);
        double pos = ph * steps;
        if (s.groove != 0.0 && steps >= 2) // round 150: the odd steps start late (+) or early (−) by groove × a step
        {
            const double pair = 2.0 * std::floor (pos / 2.0), u = pos - pair, mid = 1.0 + std::clamp (s.groove, -0.25, 0.25);
            pos = pair + (u < mid ? u / mid : 1.0 + (u - mid) / (2.0 - mid));
            pos = std::min (pos, (double) steps - 1e-9);
        }
        const int k = std::min ((int) pos, steps - 1);
        const double fr = std::clamp ((pos - k) / glide, 0.0, 1.0);
        const double cur = values[(size_t) k], prev = values[(size_t) ((k + steps - 1) % steps)];
        // round 150 [EXEC]: past the speakers (offset + width / 2 beyond ±90°) the angle folds back with the far side's
        // polarity inverted (offset 45° + OffsetMod 90° → the left at tan 7.5° of the right, inverted); else clipped
        const double angle = 45.0 + 0.5 * offset + 0.25 * width * (prev + (cur - prev) * fr);
        const double theta = (s.beyond ? std::clamp (angle, -52.5, 142.5) : std::clamp (angle, 0.0, 90.0)) * pi / 180.0;
        if (left != nullptr) left[i] = (float) (fa * std::cos (theta) * outGain);
        if (right != nullptr) right[i] = (float) (fb * std::sin (theta) * outGain);
        current = (float) std::clamp (theta * 2.0 / pi, 0.0, 1.0);
    }
}
} // namespace lynx::rhythmpan
