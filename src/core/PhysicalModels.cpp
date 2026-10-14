#include "core/PhysicalModels.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;

std::uint32_t lcg (std::uint32_t s) noexcept { return s * 0x0BB38435u + 0x3619636Bu; }
double clamp01 (double x) noexcept { return std::clamp (x, 0.0, 1.0); }

// 2-pole high-pass used by both engines (REA_rodada28 §1.4): numerator (1, −2, 1)·b, poles a1/a2
struct HighPass
{
    double b = 0.0, a1 = 0.0, a2 = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    void design (double f, double q, double fs) noexcept
    {
        const auto w = 2.0 * pi * f / fs, s = std::sin (w), c = std::cos (w);
        b = (1.0 + c) / (s / q + 2.0);
        a1 = -2.0 * q * c / (q + s);
        a2 = (q - s) / (q + s);
        x1 = x2 = y1 = y2 = 0.0;
    }

    double tick (double x) noexcept
    {
        const auto y = (x - 2.0 * x1 + x2) * b - a2 * y2 - a1 * y1;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// Settings handed from configure() (UI thread) to the audio thread without blocking
template <typename T>
class Mailbox
{
public:
    void write (const T& value) noexcept
    {
        bool expected = false;
        while (! busy.compare_exchange_weak (expected, true))
            expected = false;
        pending = value;
        fresh = true;
        busy.store (false);
    }

    bool read (T& out) noexcept
    {
        bool expected = false;
        if (! busy.compare_exchange_strong (expected, true))
            return false;
        const auto had = fresh;
        if (had)
            out = pending;
        fresh = false;
        busy.store (false);
        return had;
    }

private:
    std::atomic<bool> busy { false };
    T pending {};
    bool fresh = false;
};

// Linear resampler from the engine's internal rate to the host rate (one per instrument, shared by its voices)
template <typename Source>
void renderResampled (Source& source, double internalRate, double hostRate, double& position, float* left, float* right,
                      int n, float gl, float gr, std::array<float, 2>& previous, std::array<float, 2>& current) noexcept
{
    const auto step = internalRate / hostRate;
    for (int i = 0; i < n; ++i)
    {
        while (position >= 1.0)
        {
            previous = current;
            current = source.tick();
            position -= 1.0;
        }
        const auto f = (float) position;
        left[i] += (previous[0] + (current[0] - previous[0]) * f) * gl;
        right[i] += (previous[1] + (current[1] - previous[1]) * f) * gr;
        position += step;
    }
}

// ---------------------------------------------------------------------------
// Sakura (REA_rodada28 §1): exciter → 4 string loops (near/far × 2 strings) → combs → stereo mix → amp envelope
// → 32 Hz high-pass → master gain. Modulation matrix routings, EG/LFO destinations, resonators and FX are not
// modelled (their shapes were not confirmed).

using SakuraParams = std::array<float, sakuraParamCount>;

struct SakuraVoice
{
    bool active = false;
    int key = 0;
    // per-note
    int lens[4] {}, combLen[2] {};
    std::array<std::vector<float>, 4> loops;
    std::array<std::vector<float>, 2> combs;
    int ptr[4] {}, combPtr[2] {};
    double kdc[4] {}, step = 0.0;
    // state
    std::uint32_t seed = 0;
    double hold = 0.0, s[3] {}, z4 = 0.0, hiCutEnv = 0.0;
    double mean[4] {}, slow[4] {}, z[4] {};
    double fbs[4] {}, gains[4] {};
    double acc = 0.0, prev[2] {}, cur[2] {};
    double env = 0.0, elapsed = 0.0, release = -1.0;
    HighPass lowCut;
    int blockLeft = 0;
    double hcTarget = 0.0, hcRate = 0.0, envTarget = 0.0, envRate = 0.0;
    double level = 1.0; // modulation-matrix level relative to the default ×5 (velocity/note routings)
};

class SakuraInstrument final : public Instrument
{
public:
    static constexpr double fs = 48000.0;
    static constexpr int maxVoices = 12;
    static constexpr int maxLength = 9600; // FL allows 48 000 (notes down to 1 Hz); LYNX keeps ≥ 20 Hz × 4 in memory

    SakuraInstrument()
    {
        for (auto& v : voices)
        {
            for (auto& l : v.loops)
                l.assign (maxLength + 1, 0.0f);
            for (auto& c : v.combs)
                c.assign (maxLength + 1, 0.0f);
        }
    }

    void prepare (double sr) noexcept override
    {
        hostRate = sr;
        outHp[0].design (32.0, 1.0, sr);
        outHp[1].design (32.0, 1.0, sr);
        allNotesOff();
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        SakuraParams p {};
        for (std::size_t i = 0; i < p.size() && i < channel.modelParams.size(); ++i)
            p[i] = std::isfinite (channel.modelParams[i]) ? channel.modelParams[i] : 0.0f;
        mailbox.write (p);
    }

    void noteOn (int key, float velocity) noexcept override
    {
        mailbox.read (params); // the note's lengths and laws come from the latest settings
        SakuraVoice* target = nullptr;
        for (auto& v : voices)
            if (! v.active)
            {
                target = &v;
                break;
            }
        if (target == nullptr)
            target = &voices[(std::size_t) (nextSteal++ % maxVoices)];
        start (*target, key);
        target->level = matrixLevel (velocity, key);
    }

    void noteOff (int key) noexcept override
    {
        for (auto& v : voices)
            if (v.active && v.key == key && v.release < 0.0)
                v.release = 0.0;
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            v.active = false;
    }

    bool idle() const noexcept override
    {
        return std::none_of (voices.begin(), voices.end(), [] (const SakuraVoice& v) { return v.active; });
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        mailbox.read (params);
        // × 1/√2: FL's per-voice levels are Vol·cos/sin((1 + pan)π/4) and LYNX's channel gains √2·cos/sin (§4)
        const auto master = (float) (std::pow (10.0, 4.8 * params[0] - 1.8) * 0.70710678);
        struct Source
        {
            SakuraInstrument& self;
            std::array<float, 2> tick() noexcept { return self.tickVoices(); }
        } source { *this };
        // render at 48 kHz into the stereo stream, resampled to the host, then the 32 Hz HP and master gain
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            renderResampled (source, fs, hostRate, position, &l, &r, 1, 1.0f, 1.0f, previous, current);
            left[i] += (float) outHp[0].tick (l) * master * gl;
            right[i] += (float) outHp[1].tick (r) * master * gr;
        }
    }

private:
    double p (int i) const noexcept { return i >= 0 && i < sakuraParamCount ? (double) params[(std::size_t) i] : 0.0; }

    // modulation matrix, destination 1 (level, §1.9): m = Σ amount²·source over the slots routed to it (sign by
    // "invert"); with no routed slot on destinations 0/1, m = 1. Level = 10^(2m)·0.05 (m = 1 → ×5, the default).
    // Static sources only: velocity (vel/127) and note number × 0.0125; mod wheel, pressure, bend and the release
    // gate are 0 while the note is held; EG and LFO shapes were not decoded (their slots count as 0).
    double matrixLevel (float velocity, int key) const noexcept
    {
        bool routed = false;
        double m = 0.0;
        for (int slot = 0; slot < 8; ++slot)
        {
            const auto source = (int) p (79 + 4 * slot), destination = (int) p (82 + 4 * slot);
            if (source == 0 || (destination != 0 && destination != 1))
                continue;
            routed = true;
            if (destination != 1)
                continue;
            const auto amount = p (80 + 4 * slot) * p (80 + 4 * slot) * (p (81 + 4 * slot) != 0.0 ? -1.0 : 1.0);
            const auto value = source == 6 ? std::clamp ((double) velocity, 0.0, 1.0) : source == 7 ? key * 0.0125 : 0.0;
            m += amount * value;
        }
        if (! routed)
            m = 1.0;
        return std::pow (10.0, 2.0 * m) * 0.05 / 5.0;
    }

    static void damping (double a, double offsetSquared, double& nearC, double& farC) noexcept
    {
        const auto base = std::pow (50.0, 2.57 - 0.8995 * a * a * a) * (2.0 / 44100.0);
        nearC = std::min (1.0, base);
        farC = std::min (1.0, (1.0 - offsetSquared) * base);
    }

    void start (SakuraVoice& v, int key) noexcept
    {
        static constexpr double octaves[7] { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        auto f = 220.0 * std::pow (2.0, (key - 57) / 12.0) * octaves[std::clamp ((int) (p (113) * 6.0), 0, 6)];
        const auto base = std::max (1, (int) (48000.0 / f));
        int l1 = base, l2 = base;
        switch ((int) p (111))
        {
            case 0: l2 = base / 4; l1 = l2 * 4; break;
            case 1: l2 = base / 3; l1 = l2 * 3; break;
            case 2: l2 = base / 2; l1 = l2 * 2; break;
            case 4: l2 = 2 * base; break;
            case 5: l2 = 3 * base; break;
            case 6: l2 = 4 * base; break;
            default: break;
        }
        l1 = std::clamp (l1, 1, maxLength);
        l2 = std::clamp (l2, 1, maxLength);
        v.lens[0] = v.lens[1] = l1;
        v.lens[2] = v.lens[3] = l2;
        const auto rate = 48000.0 / l1;
        v.kdc[0] = v.kdc[1] = 1.0 / l1;
        v.kdc[2] = v.kdc[3] = 1.0 / l2;
        const auto ratio = p (19);
        v.combLen[0] = std::clamp ((int) (l1 * ratio), 1, maxLength);
        v.combLen[1] = std::clamp ((int) (l2 * ratio), 1, maxLength);
        for (int j = 0; j < 4; ++j)
        {
            std::fill (v.loops[(std::size_t) j].begin(), v.loops[(std::size_t) j].begin() + v.lens[j], 0.0f);
            v.ptr[j] = 0;
            v.mean[j] = v.slow[j] = v.z[j] = 0.0;
        }
        for (int c = 0; c < 2; ++c)
        {
            std::fill (v.combs[(std::size_t) c].begin(), v.combs[(std::size_t) c].begin() + v.combLen[c], 0.0f);
            v.combPtr[c] = 0;
        }
        const auto tune = std::clamp ((int) (p (1) * 198.0 - 99.0), -99, 99) * 0.01;
        v.step = f / rate * std::pow (2.0, tune / 12.0);
        v.seed = 0;
        v.hold = 0.0;
        v.s[0] = v.s[1] = v.s[2] = 0.0;
        v.z4 = v.hiCutEnv = 0.0;
        v.acc = 0.0;
        v.prev[0] = v.prev[1] = v.cur[0] = v.cur[1] = 0.0;
        v.env = v.elapsed = 0.0;
        v.release = -1.0;
        v.lowCut.design (std::pow (2.0, 9.3 * clamp01 (p (24)) + 5.0), 10.0 * p (25) + 1.0, fs);
        v.blockLeft = 0;
        v.key = key;
        v.active = true;
        for (int j = 0; j < 4; ++j)
            v.fbs[j] = v.gains[j] = 0.0;
        computeStatic();
        for (int j = 0; j < 4; ++j)
            v.fbs[j] = feedback[j], v.gains[j] = mixGain[j];
    }

    // per-block static values (no modulation)
    void computeStatic() noexcept
    {
        saturation = 100.0 * std::pow (p (16), 6.0);
        threshold = 0.996 * std::pow (1.0 - clamp01 (p (32)), 0.25);
        drive = 1000.0 * std::pow (p (33), 4.0);
        noiseA = (1.0 - p (31)) * (1.0 - p (31));
        noiseB = p (31);
        ladderG = clamp01 (p (29) * p (29));
        ladderR = std::max (-10.0 * p (30) * (1.0 - ladderG), -4.0);
        double c0, c1, c2, c3;
        damping (clamp01 (p (9)), p (8) * p (8), c0, c1);
        damping (clamp01 (p (14)), p (13) * p (13), c2, c3);
        const auto sh1 = -0.5 * p (10) * p (10), sh2 = -0.5 * p (15) * p (15);
        coef[0] = c0, coef[1] = c1, coef[2] = c2, coef[3] = c3;
        sharp[0] = (1.0 - c0) * sh1, sharp[1] = (1.0 - c1) * sh1, sharp[2] = (1.0 - c2) * sh2, sharp[3] = (1.0 - c3) * sh2;
        const auto fb1 = std::pow (clamp01 (p (6)), 0.05) * (p (7) != 0.0 ? -1.0 : 1.0);
        const auto fb2 = std::pow (clamp01 (p (11)), 0.05) * (p (12) != 0.0 ? -1.0 : 1.0);
        feedback[0] = feedback[1] = fb1;
        feedback[2] = feedback[3] = fb2;
        const auto sep = clamp01 (p (18)), sep2 = clamp01 (1.0 - p (18));
        const auto g1 = (1.0 - p (17)) * 100.0 * 0.05, g2 = p (17) * 100.0 * 0.05;
        mixGain[0] = (1.0 - sep) * g1, mixGain[1] = sep * g1, mixGain[2] = (1.0 - sep2) * g2, mixGain[3] = sep2 * g2;
        combFeedback = std::sqrt (std::max (0.0, p (20))) * (p (21) != 0.0 ? -1.0 : 1.0);
        combLevel = p (22) * (p (23) != 0.0 ? -1.0 : 1.0);
        auto f32 = [] (double x) { return (double) (float) x; };
        attack = f32 (35.0 * std::pow (p (2), 5.0) + 0.001);
        decay = f32 (50.0 * std::pow (p (3), 5.0) + 0.001);
        sustain = f32 (p (4) * p (4));
        releaseTime = f32 (10.0 * std::pow (p (5), 5.0) + 0.1);
        hcAttack = f32 (p (26) * p (26) * 0.5);
        hcDecay = p (27);
        hcSustain = p (28) * p (28);
    }

    // one 48 kHz sample of all voices
    std::array<float, 2> tickVoices() noexcept
    {
        double outL = 0.0, outR = 0.0;
        for (auto& v : voices)
            if (v.active)
            {
                const auto [l, r] = tickVoice (v);
                outL += l;
                outR += r;
            }
        return { (float) outL, (float) outR };
    }

    std::array<double, 2> tickVoice (SakuraVoice& v) noexcept
    {
        constexpr double blockSeconds = 48.0 / 48000.0;
        if (v.blockLeft <= 0) // 48-sample control block: envelope targets and smoothed values reset
        {
            v.blockLeft = 48;
            computeStatic();
            if (v.release < 0.0)
            {
                if (hcAttack < v.elapsed)
                    v.hcRate = 0.0002 / (3.0 * hcDecay * hcDecay * hcDecay + 0.001), v.hcTarget = hcSustain;
                else
                    v.hcTarget = 1.0, v.hcRate = hcAttack >= 0.001 ? 0.0002 / hcAttack : 1.0;
                if (attack <= v.elapsed)
                    v.envTarget = sustain, v.envRate = 8.82 / (decay * 48000.0);
                else
                    v.envTarget = 5.0, v.envRate = 4.658742368e-06 / attack;
            }
            else
            {
                v.hcRate = 0.002;
                v.hcTarget = 0.0;
                if (releaseTime < v.release)
                {
                    v.active = false;
                    return { 0.0, 0.0 };
                }
                v.envTarget = 0.0;
                v.envRate = 8.82 / (releaseTime * 48000.0);
                v.release += blockSeconds;
            }
            for (int j = 0; j < 4; ++j)
                v.fbs[j] = feedback[j], v.gains[j] = mixGain[j];
            v.elapsed += blockSeconds;
        }
        --v.blockLeft;

        // exciter: sample-and-hold noise / click → Lo-Cut → 4-pole Hi-Cut with envelope, resonance and drive
        const auto x = v.seed * (1.0 / 4294967296.0);
        v.seed = lcg (lcg (lcg (lcg (v.seed))));
        if (x > threshold)
            v.hold = x;
        const auto y = v.lowCut.tick (noiseA * v.hold + noiseB);
        v.hiCutEnv += (v.hcTarget - v.hiCutEnv) * v.hcRate;
        const auto c = ladderG * v.hiCutEnv;
        v.s[0] += ((ladderR * v.z4 + y) - v.s[0]) * c;
        v.s[1] += (v.s[0] - v.s[1]) * c;
        v.s[2] += (v.s[1] - v.s[2]) * c;
        const auto w = v.z4 + (v.s[2] - v.z4) * c;
        v.z4 = w / (w * w * drive + 1.0);
        const auto e = v.z4;

        // the loops run once per crossing of the fractional clock
        v.acc += v.step;
        auto k = (int) v.acc;
        v.acc -= k;
        double d[4];
        for (int j = 0; j < 4; ++j)
            d[j] = e - v.mean[j];
        for (; k > 0; --k)
        {
            v.prev[0] = v.cur[0];
            v.prev[1] = v.cur[1];
            double rr[4];
            for (int j = 0; j < 4; ++j)
                if (v.ptr[j] >= v.lens[j])
                    v.ptr[j] = 0;
            for (int j = 0; j < 4; ++j)
            {
                v.mean[j] += v.kdc[j] * d[j];
                d[j] = e - v.mean[j];
                auto& line = v.loops[(std::size_t) j];
                const auto r = (double) line[(std::size_t) v.ptr[j]];
                rr[j] = r;
                v.slow[j] += ((sharp[j] * v.z[j] + r) - v.slow[j]) * coef[j];
                const auto vv = v.z[j] + (v.slow[j] - v.z[j]) * coef[j];
                v.z[j] = vv / (vv * vv * saturation + 1.0);
                line[(std::size_t) v.ptr[j]] = (float) (v.fbs[j] * v.z[j] + d[j]);
                ++v.ptr[j];
            }
            for (int cIdx = 0; cIdx < 2; ++cIdx)
                if (v.combPtr[cIdx] >= v.combLen[cIdx])
                    v.combPtr[cIdx] = 0;
            auto& ca = v.combs[0];
            auto& cb = v.combs[1];
            const auto oA = combLevel * ca[(std::size_t) v.combPtr[0]] + rr[0];
            const auto oB = combLevel * cb[(std::size_t) v.combPtr[1]] + rr[2];
            v.cur[0] = v.gains[2] * oB + v.gains[0] * oA;
            v.cur[1] = v.gains[3] * oB + v.gains[1] * oA;
            ca[(std::size_t) v.combPtr[0]] = (float) (ca[(std::size_t) v.combPtr[0]] * combFeedback + rr[1]);
            cb[(std::size_t) v.combPtr[1]] = (float) (cb[(std::size_t) v.combPtr[1]] * combFeedback + rr[3]);
            ++v.combPtr[0];
            ++v.combPtr[1];
            for (int j = 0; j < 4; ++j)
            {
                v.fbs[j] += (feedback[j] - v.fbs[j]) * 0.05;
                v.gains[j] += (mixGain[j] - v.gains[j]) * 0.001;
            }
        }
        const auto outL = ((v.cur[0] - v.prev[0]) * v.acc + v.prev[0]) * v.env * v.level;
        const auto outR = ((v.cur[1] - v.prev[1]) * v.acc + v.prev[1]) * v.env * v.level;
        v.env = std::min (1.0, (v.envTarget - v.env) * v.envRate + v.env);
        return { outL, outR };
    }

    Mailbox<SakuraParams> mailbox;
    SakuraParams params {};
    std::array<SakuraVoice, maxVoices> voices {};
    int nextSteal = 0;
    double hostRate = 48000.0, position = 1.0;
    std::array<float, 2> previous {}, current {};
    HighPass outHp[2];
    // static per-block values
    double saturation = 0.0, threshold = 1.0, drive = 0.0, noiseA = 1.0, noiseB = 0.0, ladderG = 0.0, ladderR = 0.0;
    double coef[4] {}, sharp[4] {}, feedback[4] {}, mixGain[4] {};
    double combFeedback = 0.0, combLevel = 0.0;
    double attack = 0.001, decay = 0.001, sustain = 1.0, releaseTime = 0.1, hcAttack = 0.0, hcDecay = 0.0, hcSustain = 0.0;
};

// ---------------------------------------------------------------------------
// Drumpad (REA_rodada28 §2): mallet exciter → bank of independent comb loops (length 512 − i, low-pass x²,
// feedback −Decay^0.3, folded-triangle output weights) → self-oscillating filter → LOW high-pass → HIGH shelf →
// MID peak (enveloped) → clip → hold → lo-fi, at 38 kHz. No key tracking (the DSP has none).

using DrumpadParams = std::array<float, drumpadParamCount>;

struct Rbj
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void design (int kind, double f, double gainDb, double bandwidth, double fs) noexcept
    {
        const auto a = std::pow (10.0, gainDb * 0.05);
        const auto w = std::clamp (f * 6.283185 / fs, 0.0, 3.0);
        const auto sn = std::sin (w), cs = std::cos (w);
        const auto sh = std::sinh (0.3465736 * bandwidth * w / sn);
        const auto alpha = sn * sh, beta = std::sqrt (a) * 2.0 * sh * sn;
        double nb0, nb1, nb2, na0, na1, na2;
        if (kind == 1) // peaking
            nb0 = 1 + alpha * a, nb1 = -2 * cs, nb2 = 1 - alpha * a, na0 = 1 + alpha / a, na1 = -2 * cs, na2 = 1 - alpha / a;
        else // high shelf
        {
            nb0 = a * ((a + 1) + (a - 1) * cs + beta);
            nb1 = -2 * a * ((a - 1) + (a + 1) * cs);
            nb2 = a * ((a + 1) + (a - 1) * cs - beta);
            na0 = (a + 1) - (a - 1) * cs + beta;
            na1 = 2 * ((a - 1) - (a + 1) * cs);
            na2 = (a + 1) - (a - 1) * cs - beta;
        }
        b0 = nb0 / na0, b1 = nb1 / na0, b2 = nb2 / na0, a1 = na1 / na0, a2 = na2 / na0;
    }

    double tick (double x) noexcept
    {
        const auto y = x * b0 + x1 * b1 + x2 * b2 - y1 * a1 - y2 * a2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};

class DrumpadInstrument final : public Instrument
{
public:
    static constexpr double fs = 38000.0;
    static constexpr int rings = 128; // groups of 4 lanes: lane i of group k has length 512 − 4k

    DrumpadInstrument()
    {
        for (int k = 0; k < rings; ++k)
            ring[(std::size_t) k].assign ((std::size_t) (512 - 4 * k), {});
    }

    void prepare (double sr) noexcept override
    {
        hostRate = sr;
        allNotesOff();
    }

    void configure (const ChannelDef& channel, double) noexcept override
    {
        DrumpadParams p {};
        for (std::size_t i = 0; i < p.size() && i < channel.modelParams.size(); ++i)
            p[i] = std::isfinite (channel.modelParams[i]) ? channel.modelParams[i] : 0.0f;
        mailbox.write (p);
    }

    // one voice (Drumpad retriggers it; the loops keep their contents across notes)
    void noteOn (int, float velocity) noexcept override
    {
        mailbox.read (raw);
        computeParams (std::sqrt (std::clamp ((double) velocity, 0.0, 1.0)));
        e = amp;
        if (retrig)
            seed = noiseSeed;
        lowCut.design (lowF, lowQ, fs);
        high.design (2, highF, highDb, std::max (highBw, 1e-4), fs);
        high.x1 = high.x2 = high.y1 = high.y2 = 0.0;
        mid.x1 = mid.x2 = mid.y1 = mid.y2 = 0.0;
        envMid = 1.0;
        midCount = 0;
        midRetrig = midRetrigCount;
        const auto c = std::pow (2.0, (sofF * 0.5 + 0.1) * 9.3 + 5.0) * 0.00015789474;
        sc = (float) std::clamp (c, 0.0, 1.0);
        sk = sc * 333.3333333333333f;
        senv = 1.0f;
        s1 = s2 = s3 = y4 = 0.0f;
        gain = 1.0;
        t = 0.0;
        sounding = true;
        silentFor = 0;
    }

    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override { sounding = false; }
    bool idle() const noexcept override { return ! sounding; }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        if (! sounding)
            return;
        struct Source
        {
            DrumpadInstrument& self;
            std::array<float, 2> tick() noexcept
            {
                const auto x = (float) self.tick();
                return { x, x };
            }
        } source { *this };
        // × 1/√2: FL's per-voice levels are Vol·cos/sin((1 + pan)π/4), LYNX's channel gains √2·cos/sin (REA_rodada28 §4)
        renderResampled (source, fs * pitch, hostRate, position, left, right, n, gl * 0.70710678f, gr * 0.70710678f, previous, current);
        // the voice ends when the hold gain has decayed and the bank is quiet
        if (gain < 1e-5 && std::abs (current[0]) < 1e-6f)
            sounding = false;
    }

private:
    double g (int k, double velocity) const noexcept
    {
        const auto value = (double) raw[(std::size_t) k];
        auto alt = value;
        for (int slot = 0; slot < 4; ++slot) // velocity morph: params 30…33 = target index − 1, 34…37 = value at velocity 1
            if (const auto target = (int) raw[(std::size_t) (30 + slot)]; target != 0 && target + 1 == k)
                alt = raw[(std::size_t) (34 + slot)];
        return (1.0 - velocity) * value + alt * velocity;
    }

    void computeParams (double vel) noexcept
    {
        cut = std::pow (clamp01 (g (7, vel)), 2.0);
        fb = -std::pow (clamp01 (g (6, vel)), 0.3);
        const auto phase = clamp01 (g (9, vel)), tension = clamp01 (g (8, vel)), material = clamp01 (g (10, vel));
        size = std::min (0x1ff, (int) (clamp01 (1.0 - g (11, vel)) * 512.0));
        shape = std::clamp ((int) (g (12, vel) * 128.0), 1, 128);
        amp = std::pow (10.0, (1.0 - clamp01 (g (2, vel))) * -4.0);
        const auto md = 1.0 - clamp01 (g (3, vel));
        malletDecay = (std::pow (md, 4.0) * 500.0 + 1.0) * 1e-4;
        noise = clamp01 (g (4, vel));
        retrig = raw[0] != 0.0f;
        noiseSeed = (std::uint32_t) ((std::uint64_t) ((double) (float) g (5, vel) * 4294967295.0) & 0xffffffffu);
        const auto lf = clamp01 (g (27, vel));
        lofi = lf >= 0.01 ? (int) (std::pow (1.0 - lf, 5.0) * 65536.0 + 4.0) : 0;
        holdTime = std::pow (clamp01 (g (28, vel)), 3.0) * 3.0 + 0.001;
        pitch = std::pow (2.0, 2.0 * clamp01 (g (29, vel)) - 1.0);
        lowOn = g (13, vel) != 0.0;
        lowF = std::pow (2.0, clamp01 (g (13, vel)) * 11.0 + 2.0);
        lowQ = clamp01 (g (14, vel)) * 8.0 + 1.0;
        highF = std::pow (2.0, clamp01 (g (24, vel)) * 4.1 + 8.0);
        highBw = (1.0 - clamp01 (g (26, vel))) * 2.0 + 0.5;
        highDb = (clamp01 (g (25, vel)) - 0.5) * 36.0;
        highOn = std::abs (highDb) > 0.01;
        midF = std::pow (2.0, clamp01 (g (18, vel)) * 9.0 + 5.0);
        midFmod = (clamp01 (g (21, vel)) - 0.5) * 32.0;
        const auto lv = g (19, vel), lm = g (22, vel);
        midLevelOn = lv != 0.5;
        midDb = (clamp01 (lv) - 0.5) * 32.0;
        midLmodOn = lm != 0.5;
        midLmod = (clamp01 (lm) - 0.5) * 48.0;
        midBw = (1.0 - clamp01 (g (20, vel))) * 4.0 + 0.5;
        const auto ma = 1.0 - clamp01 (g (23, vel));
        midAttack = (std::pow (ma, 4.0) * 1000.0 + 1.0) * 1e-5;
        midRetrigCount = std::clamp ((int) (raw[1] * 3.0f), 0, 3);
        sofF = clamp01 (g (15, vel));
        const auto sd = clamp01 (g (16, vel));
        sofDecay = (float) ((std::pow (sd, 4.0) * 0.25 * 250.0 + 1.0) * 1e-5);
        sofOn = g (17, vel) != 0.0;
        sofLevel = (float) (std::pow (clamp01 (g (17, vel)), 8.0) * 0.5);

        // output weights: folded triangle raised to the Material exponent, over the lanes Size…Size + Shape
        const auto m = 2.0 * material - 1.0;
        auto ex = 1.0 + 20.0 * std::pow (m, 4.0);
        if (m < 0.0)
            ex = 1.0 / ex;
        const auto stp = 0.5 * std::pow (tension, 4.0);
        auto ph = phase;
        weights.fill (0.0);
        const auto top = std::min (size + shape, 0x1ff);
        for (int i = 511; i >= 0; --i)
        {
            if (i >= size && i <= top)
            {
                auto x = (ph - 0.5) + ph - 0.5;
                if (x > 0.5)
                    x = 1.0 - x;
                if (x < -0.5)
                    x = -1.0 - x;
                const auto x2 = x + x;
                weights[(std::size_t) i] = x2 < 0.0 ? -std::pow (std::abs (x2), ex) : std::pow (x2, ex);
            }
            ph += stp;
            if (ph > 1.0)
                ph -= 1.0;
        }
        firstRing = std::clamp (size / 4, 0, 127);
        const auto end = size + shape >= 0x200 ? 128 : std::min (128, (size + shape) / 4 + ((size + shape) % 4 != 0 ? 1 : 0));
        ringCount = std::max (0, end - firstRing);
    }

    double tick() noexcept
    {
        e = (1e-7 - e) * malletDecay + e;
        const auto nz = seed * (1.0 / 4294967296.0);
        seed = lcg (seed);
        const auto exc = ((nz - 0.5) * noise + (1.0 - noise)) * e;
        double acc = 0.0;
        for (int k = firstRing; k < firstRing + ringCount; ++k)
        {
            auto& r = ring[(std::size_t) k];
            const auto len = (int) r.size();
            auto& p = ptr[(std::size_t) k];
            auto& lp = lowPass[(std::size_t) k];
            // lanes 1…3 are shorter by 1…3 samples: their last cells mirror the start of the ring
            if (p == len - 1)
                r[(std::size_t) p][1] = r[0][1];
            if (p == len - 2)
                r[(std::size_t) p][2] = r[0][2], r[(std::size_t) p + 1][2] = r[1][2];
            if (p == len - 3)
                r[(std::size_t) p][3] = r[0][3], r[(std::size_t) p + 1][3] = r[1][3], r[(std::size_t) p + 2][3] = r[2][3];
            auto& cell = r[(std::size_t) p];
            for (int j = 0; j < 4; ++j)
            {
                acc += cell[(std::size_t) j] * weights[(std::size_t) (4 * k + j)];
                lp[(std::size_t) j] = (cell[(std::size_t) j] - lp[(std::size_t) j]) * cut + lp[(std::size_t) j];
                cell[(std::size_t) j] = lp[(std::size_t) j] * fb + exc;
            }
            if (++p == len)
            {
                for (int q = 0; q < len - 1; ++q)
                    r[(std::size_t) q][1] = r[(std::size_t) q + 1][1];
                for (int q = 0; q < len - 2; ++q)
                    r[(std::size_t) q][2] = r[(std::size_t) q + 2][2];
                for (int q = 0; q < len - 3; ++q)
                    r[(std::size_t) q][3] = r[(std::size_t) q + 3][3];
                p = 0;
            }
        }
        auto x = acc;
        if (sofOn) // self-oscillating filter, in float32 as in the DLL (its −100000 feedback needs it)
        {
            const auto y4o = y4;
            const auto a = (float) ((float) x + y4o * -100000.0f);
            x = (float) ((float) x + y4o * sofLevel);
            const auto gg = sc / ((sk + 1.0f) - senv * sk);
            const auto tt = a / (std::abs (a) * 0.005f + 1.0f);
            s1 = (tt - s1) * gg + s1;
            s2 = (s1 - s2) * gg + s2;
            s3 = (s2 - s3) * gg + s3;
            senv = senv - senv * sofDecay;
            y4 = (s3 - y4o) * gg + y4o;
        }
        if (lowOn)
            x = lowCut.tick (x);
        if (highOn)
            x = high.tick (x);
        if (midLevelOn || midLmodOn)
        {
            if (midCount == 0)
            {
                const auto db = (1.0 - envMid) * midLmod + midDb;
                const auto fm = midFmod * (1.0 - envMid);
                auto f = midFmod >= 0.0 ? (fm + 1.0) * midF : midF / (1.0 - fm);
                f = std::clamp (std::max (32.0, f), 0.0, 12000.0);
                const auto keep = mid;
                mid.design (1, f, db, std::max (midBw, 1e-4), fs);
                mid.x1 = keep.x1, mid.x2 = keep.x2, mid.y1 = keep.y1, mid.y2 = keep.y2;
            }
            midCount = (midCount + 1) % 6;
            envMid -= envMid * midAttack;
            if (midRetrig != 0 && envMid < 0.01)
                envMid = 1.0, --midRetrig;
            x = mid.tick (x);
        }
        x = std::clamp (x, -1.0, 1.0);
        if (holdTime <= t)
            gain -= gain * 0.001;
        else
            gain = (1.0 - gain) * 0.1 + gain;
        t += 2.6315789e-05;
        x *= gain;
        if (lofi != 0)
            x = (double) (int) (lofi * x) / lofi;
        return x;
    }

    Mailbox<DrumpadParams> mailbox;
    DrumpadParams raw {};
    std::array<std::vector<std::array<double, 4>>, rings> ring;
    std::array<int, rings> ptr {};
    std::array<std::array<double, 4>, rings> lowPass {};
    std::array<double, 512> weights {};
    int firstRing = 0, ringCount = 0;
    double cut = 0, fb = 0, amp = 1e-4, malletDecay = 1e-4, noise = 0, holdTime = 1, pitch = 1;
    int size = 0, shape = 1, lofi = 0, midRetrigCount = 0, midRetrig = 0, midCount = 0;
    bool retrig = false, lowOn = false, highOn = false, midLevelOn = false, midLmodOn = false, sofOn = false, sounding = false;
    std::uint32_t seed = 0, noiseSeed = 0;
    double lowF = 100, lowQ = 1, highF = 1000, highBw = 1, highDb = 0, midF = 1000, midFmod = 0, midDb = 0, midLmod = 0,
           midBw = 1, midAttack = 1e-5;
    double sofF = 0;
    float sofDecay = 0, sofLevel = 0, sc = 0, sk = 0, senv = 1, s1 = 0, s2 = 0, s3 = 0, y4 = 0;
    double e = 0, envMid = 1, gain = 1, t = 0;
    HighPass lowCut;
    Rbj high, mid;
    double hostRate = 48000.0, position = 1.0;
    std::array<float, 2> previous {}, current {};
    int silentFor = 0;
};
} // namespace

std::unique_ptr<Instrument> makeSakuraInstrument() { return std::make_unique<SakuraInstrument>(); }
std::unique_ptr<Instrument> makeDrumpadInstrument() { return std::make_unique<DrumpadInstrument>(); }
} // namespace lynx
