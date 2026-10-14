#include "core/Effector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace lynx::effector
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr double maxRate = 192000.0;

float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

// Mod Rate values (labels "1/8" … "8/1"), in beats
constexpr std::array<double, 9> rates { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 2.0, 4.0, 8.0 };
constexpr std::array<const char*, 9> rateNames { "1/8", "1/4", "3/8", "1/2", "3/4", "1/1", "2/1", "4/1", "8/1" };

int rateIndex (float v) noexcept { return std::clamp ((int) (8.0f * v + 0.5f), 0, 8); }

// modulation amount: sign·(2v − 1)²
float modAmount (float v) noexcept
{
    const auto m = 2.0f * v - 1.0f;
    return m < 0.0f ? -m * m : m * m;
}

// Distort saturation cascade (REA_rodada19 §1.5)
double distortCurve (double s, double k) noexcept
{
    s = s / (1.0 + k * std::abs (s));
    s = s / (1.0 + k * s * s);
    s = s / (1.0 + k * std::abs (s * s * s));
    const auto s2 = s * s;
    return s / (1.0 + k * s2 * s2);
}

struct Biquad
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    // cookbook band-pass with peak gain = Q (b0 = sin ω / 2)
    void bandPass (double hz, double q, double fs) noexcept
    {
        const auto w = 2.0 * pi * std::clamp (hz, 10.0, 0.49 * fs) / fs;
        const auto alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        b0 = std::sin (w) / 2.0 / a0;
        b1 = 0.0;
        b2 = -b0;
        a1 = -2.0 * std::cos (w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x, int ch) noexcept
    {
        const auto y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

class Effector final : public Effect
{
public:
    Effector()
    {
        delayLine[0].assign ((std::size_t) (1.5 * maxRate) + 8, 0.0f);
        delayLine[1].assign (delayLine[0].size(), 0.0f);
        stutter[0].assign ((std::size_t) (1.5 * maxRate) + 8, 0.0f);
        stutter[1].assign (stutter[0].size(), 0.0f);
        shortLine[0].assign ((std::size_t) (0.006 * maxRate) + 8, 0.0f);
        shortLine[1].assign (shortLine[0].size(), 0.0f);
        reverb = makeEffect (EffectKind::reverb);
    }

    void prepare (double sr) noexcept override
    {
        fs = std::min (sr, maxRate);
        reverb->prepare (fs);
        reset();
    }

    void reset() noexcept override
    {
        for (auto* lines : { &delayLine, &stutter, &shortLine })
            for (auto& line : *lines)
                std::fill (line.begin(), line.end(), 0.0f);
        write = shortWrite = 0;
        recorded = 0;
        playPos = 0;
        stutterPhase = 0.0;
        fadeLeft = 0;
        fadeFrom = 0;
        lp[0] = lp[1] = 0.0;
        echoFilter[0] = echoFilter[1] = 0.0;
        hold[0] = hold[1] = 0.0f;
        holdCount = 0;
        phaserState = {};
        phaserFeedback[0] = phaserFeedback[1] = 0.0;
        phaserSweep = -1.0;
        filterLow = {};
        filterHigh = {};
        tapeTime.fill (-1e18); // nothing written yet: reads interpolate towards the first point (value 0)
        tapeTime[0] = 0.0;
        tapeValue = {};
        tapeWrite = tapeRead = 0;
        tapePosition = 0.0;
        tapeRate = -1.0;
        trancer = 0.0;
        ringPhase = 0.0;
        for (auto& b : vowel)
            b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0;
        wetSmooth = -1.0f;
        reverb->reset();
    }

    void setSongPosition (double ticks) noexcept override { songBeats = ticks / ppq; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        if (p[bypass] != 0.0f || n <= 0)
            return;

        const auto kind = typeOf (p[type]);
        const auto x = clamp01 (p[paramX]), y = clamp01 (p[paramY]);
        const auto wet = clamp01 (p[wetMix]);
        if (wetSmooth < 0.0f)
            wetSmooth = wet;
        const auto inGain = p[inputLevel] * p[inputLevel];
        const auto outGain = (float) std::pow (10.0, (p[outputGain] - 0.5) * 2.4);
        const auto beats = rateBeats (p[modRate], p[tempoReduce]);
        const auto periodSeconds = beats * 60.0 / std::max (bpm, 20.0);

        // LFO evaluated once per block, with phase locked to the song position
        const auto phase = songBeats / beats - std::floor (songBeats / beats);
        float lfo = 0.0f;
        switch (std::clamp ((int) (2.0f * p[modShape] + 0.5f), 0, 2))
        {
            case 0: lfo = (float) std::sin (2.0 * pi * phase); break;
            case 1: lfo = (float) (1.0 - 2.0 * phase); break;
            default: lfo = phase < 0.5 ? 1.0f : -1.0f; break;
        }
        const auto mx = clamp01 (x + lfo * modAmount (p[modX])), my = clamp01 (y + lfo * modAmount (p[modY]));

        if (inGain != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= inGain;
                r[i] *= inGain;
            }

        switch (kind)
        {
            case 0: distort (l, r, n, x, y, wet); break;
            case 1: lofi (l, r, n, mx, my, wet); break;
            case 2: flanger (l, r, n, x, lfo * modAmount (p[modX]), my, wet); break;
            case 3: phaser (l, r, n, x, lfo * modAmount (p[modX]), my, wet); break;
            case 4: filter (l, r, n, mx, my, wet); break;
            case 5: echo (l, r, n, x, y, wet, periodSeconds); break;
            case 6: reverbUnit (l, r, n, mx, my, wet, bpm); break;
            case 7: stereo (l, r, n, mx, my, wet); break;
            case 8: trance (l, r, n, x, y, phase, beats, bpm); break;
            case 9: stutterUnit (l, r, n, wet, periodSeconds); break;
            case 10: vowelUnit (l, r, n, mx, my, wet); break;
            default: ring (l, r, n, mx, wet); break;
        }

        if (outGain != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= outGain;
                r[i] *= outGain;
            }
    }

private:
    static constexpr double ppq = 960.0;

    float nextWet (float target) noexcept
    {
        wetSmooth += 0.01f * (target - wetSmooth);
        return wetSmooth;
    }

    // 0 Distort: only the low part (pole (0.9Y + 0.1)⁴) goes through the cascade; the rest stays clean
    void distort (float* l, float* r, int n, float x, float y, float wet) noexcept
    {
        const auto d = std::clamp (0.95 * x + 0.05, 0.0, 1.0), k = 2048.0 * d * d;
        const auto c = std::pow (0.9 * y + 0.1, 4.0);
        const auto g = 0.5 / distortCurve (0.5, k);
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                lp[ch] += c * (s - lp[ch]);
                const auto processed = g * distortCurve (lp[ch], k) + (s - lp[ch]);
                s = (float) ((1.0 - w) * s + w * processed);
            }
        }
    }

    // 1 Lo-fi: holds 128X³ + 1 samples; 65536/(2048Y² + 1) levels
    void lofi (float* l, float* r, int n, float x, float y, float wet) noexcept
    {
        const auto holdSamples = (int) (128.0f * x * x * x) + 1;
        const auto step = (float) (2.0 * (2048.0 * y * y + 1.0) / 65536.0);
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            if (--holdCount <= 0)
            {
                holdCount = holdSamples;
                hold[0] = step * std::floor (l[i] / step + 0.5f);
                hold[1] = step * std::floor (r[i] / step + 0.5f);
            }
            l[i] += w * (hold[0] - l[i]);
            r[i] += w * (hold[1] - r[i]);
        }
    }

    float readShort (int ch, double delaySamples) const noexcept
    {
        const auto size = shortLine[0].size();
        auto pos = (double) shortWrite - delaySamples;
        while (pos < 0.0)
            pos += (double) size;
        const auto i0 = (std::size_t) pos % size, i1 = (i0 + 1) % size;
        const auto f = (float) (pos - std::floor (pos));
        return shortLine[(std::size_t) ch][i0] + f * (shortLine[(std::size_t) ch][i1] - shortLine[(std::size_t) ch][i0]);
    }

    // 2 Flanger (REA_rodada27 §2.2): a "tape" delay of D = 2 + d·(L − 2) samples, L = ⌊0.005·fs⌋, d = X² + LFO·ModX;
    // the tape speed 1/D glides with 0.005 per sample (capped at 0.9); the line is written with −x + fb·r
    // (fb = −0.96·Y') and read one unit behind; output x + w·r
    void flanger (float* l, float* r, int n, float x, float lfoX, float y, float wet) noexcept
    {
        const auto length = std::floor (fs * 0.005);
        auto speed = [length] (double d) { return 1.0 / ((d * (1.0 - 2.0 / length) + 2.0 / length) * length); };
        const auto target = speed (std::clamp ((double) x * x + lfoX, 0.0, 1.0));
        if (tapeRate < 0.0)
            tapeRate = speed (0.5); // the unit starts at d = 0.5
        const auto feedback = -0.96 * y;
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            // read at position − 1, between the first written point at or after it and the one before
            // (positions only grow, so the read index only moves forward)
            const auto q = tapePosition - 1.0;
            while (tapeRead != tapeWrite && tapeTime[tapeRead] < q)
                tapeRead = (tapeRead + 1) & tapeMask;
            const auto previous = (tapeRead - 1) & tapeMask;
            const auto t0 = tapeTime[previous], t1 = tapeTime[tapeRead];
            const auto f = t1 > t0 ? std::clamp ((q - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
            tapePosition += std::min (tapeRate, 0.9);
            const auto slot = (tapeWrite + 1) & tapeMask;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                const auto& values = tapeValue[(std::size_t) ch];
                const auto delayed = values[previous] + (values[tapeRead] - values[previous]) * f;
                tapeValue[(std::size_t) ch][slot] = feedback * delayed - s;
                s = (float) (s + w * delayed);
            }
            tapeTime[slot] = tapePosition;
            tapeWrite = slot;
            tapeRate += (target - tapeRate) * 0.005;
        }
    }

    // 3 Phaser: 6 first-order all-passes per channel with feedback 0.98·(0.6Y')^(1/3); dry + w·wet
    // (REA_rodada27 §2.3: g = S·(1 + 0.5X')·19 950/fs + 50/fs, S following max(X'², 10⁻⁵) at 0.001 per sample)
    void phaser (float* l, float* r, int n, float x, float lfoX, float y, float wet) noexcept
    {
        const auto xm = std::clamp ((double) x + lfoX, 0.0, 1.0); // X' = X + LFO·ModX
        const auto target = std::max (xm * xm, 1e-5);
        if (phaserSweep < 0.0)
            phaserSweep = target;
        const auto feedback = 0.98 * std::cbrt (0.6 * y);
        for (int i = 0; i < n; ++i)
        {
            const auto g = std::clamp (phaserSweep * (1.0 + 0.5 * xm) * (20000.0 - 50.0) / fs + 50.0 / fs, 0.001, 0.499);
            phaserSweep += 0.001 * (target - phaserSweep);
            const auto a = (1.0 - g) / (1.0 + g);
            const auto w = nextWet (wet);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                auto v = (double) s - 1e-6 + feedback * phaserFeedback[ch]; // FL's −10⁻⁶ input offset
                for (auto& st : phaserState[(std::size_t) ch])
                {
                    const auto out = -a * v + st;
                    st = v + a * out;
                    v = out;
                }
                phaserFeedback[ch] = v;
                s = (float) (s + w * v);
            }
        }
    }

    // 4 Filter (REA_rodada27 §2.1): s = 2(0.8X' + 0.1).
    // s ≤ 1, low-pass: 4 sections, each v ← v − k·(last pole of the section), 8 one-poles with coefficient s³, ×(1 + 0.6k),
    //   k = 0.7Y' + 0.2 (×(1 − 6.4516(s − 0.845))² above s = 0.845).
    // s > 1, high-pass: v = x − 6.2568Y'·(low-pass state of stage 16), then 32 one-pole high-passes, coefficient 0.5(s − 1)³.
    void filter (float* l, float* r, int n, float x, float y, float wet) noexcept
    {
        const auto s = 2.0 * (0.8 * x + 0.1);
        const auto hp = s > 1.0;
        const auto c = hp ? 0.5 * std::pow (s - 1.0, 3.0) : s * s * s;
        auto k = 0.7 * y + 0.2;
        if (! hp && s > 0.845)
        {
            const auto fade = 1.0 - (s - 0.845) * 6.451613903045654;
            k *= fade * fade;
        }
        const auto gain = 1.0 + 0.6 * k, highFeedback = 6.2568 * y;
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& smp = ch == 0 ? l[i] : r[i];
                double v = smp;
                if (! hp)
                    for (auto& section : filterLow[(std::size_t) ch])
                    {
                        v -= k * section[7];
                        for (auto& pole : section)
                        {
                            pole += (v - pole) * c;
                            v = pole;
                        }
                        v *= gain;
                    }
                else
                {
                    auto& st = filterHigh[(std::size_t) ch];
                    v -= highFeedback * st[15];
                    for (auto& stage : st)
                    {
                        stage += (v - stage) * c;
                        v -= stage;
                    }
                }
                smp = (float) ((1.0 - w) * smp + w * v);
            }
        }
    }

    // 5 Delay: time = Mod Rate period (halved until it fits in 1.5 s), feedback and level X²,
    // echo filter by Y (Y < 0.5 darkens, Y > 0.5 cuts lows); dry + w·echo
    void echo (float* l, float* r, int n, float x, float y, float wet, double periodSeconds) noexcept
    {
        auto seconds = periodSeconds;
        while (seconds > 1.5)
            seconds *= 0.5;
        seconds = std::max (seconds, 0.0015);
        const auto size = delayLine[0].size();
        const auto delay = std::min ((std::size_t) (seconds * fs), size - 2);
        const auto fb = (double) x * x;
        const auto lowCoef = y < 0.5f ? 0.99 * 2.0 * y + 0.01 : 1.0;
        const auto highCoef = y > 0.5f ? 0.225 * (2.0 * y - 1.0) : 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            const auto readPos = (write + size - delay) % size;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                auto e = (double) delayLine[(std::size_t) ch][readPos];
                echoFilter[ch] += (y < 0.5f ? lowCoef : highCoef) * (e - echoFilter[ch]);
                e = y < 0.5f ? echoFilter[ch] : y > 0.5f ? e - echoFilter[ch] : e;
                delayLine[(std::size_t) ch][write] = (float) (s + fb * e);
                s = (float) (s + w * fb * e);
            }
            write = (write + 1) % size;
        }
    }

    // 6 Reverb: dry stays at gain 1 and the reverb of w·input is added (REA_rodada19 §1.6). Decay (REA_rodada27 §2.4):
    // T60 = 0.1·⌊149.25X'³ + 1.5⌋ s, reproduced with the LYNX reverb's comb feedback g = 10^(−3·0.0298/T60) (mean comb
    // ≈ 29.8 ms; the LYNX reverb covers ≈ 0.6…10.5 s); its diffusion network and the role of Y are approximations.
    void reverbUnit (float* l, float* r, int n, float x, float y, float wet, double bpm) noexcept
    {
        EffectParams rp {};
        const auto& info = effectInfo (EffectKind::reverb);
        for (std::size_t i = 0; i < info.params.size(); ++i)
            rp[i] = info.params[i].defaultValue;
        const auto t60 = 0.1 * std::floor (149.25 * x * x * x + 1.5);
        const auto combGain = std::pow (10.0, -3.0 * 0.02984 / t60);
        rp[0] = (float) std::clamp ((combGain - 0.7) / 0.28, 0.0, 1.0); // LYNX room: comb feedback 0.7 + 0.28·room
        rp[1] = y * y;                     // damping
        rp[3] = 0.225f * x * x * x + 0.5f; // reverb level
        rp[4] = 0.0f;                      // wet only
        for (int start = 0; start < n; start += 256)
        {
            const auto count = std::min (256, n - start);
            std::array<float, 256> wl {}, wr {};
            for (int i = 0; i < count; ++i)
            {
                const auto w = nextWet (wet);
                wl[(std::size_t) i] = w * l[start + i];
                wr[(std::size_t) i] = w * r[start + i];
            }
            reverb->process (wl.data(), wr.data(), count, rp, bpm);
            for (int i = 0; i < count; ++i)
            {
                l[start + i] += wl[(std::size_t) i];
                r[start + i] += wr[(std::size_t) i];
            }
        }
    }

    // 7 Stereo: p = 2X − 1; (Haas) delay of up to 2 ms on the opposite side; Y·p becomes a level difference
    void stereo (float* l, float* r, int n, float x, float y, float wet) noexcept
    {
        const auto pan = 2.0f * x - 1.0f;
        const auto delay = std::abs (pan) * 0.002 * fs;
        const auto level = y * pan;
        const auto gl = std::clamp (1.0f - level, 0.0f, 1.0f), gr = std::clamp (1.0f + level, 0.0f, 1.0f);
        const auto size = shortLine[0].size();
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            shortLine[0][shortWrite] = l[i];
            shortLine[1][shortWrite] = r[i];
            auto outL = l[i], outR = r[i];
            if (pan > 0.0f) // sound to the right: the left is delayed
                outL = readShort (0, delay);
            else if (pan < 0.0f)
                outR = readShort (1, delay);
            shortWrite = (shortWrite + 1) % size;
            l[i] += w * (gl * outL - l[i]);
            r[i] += w * (gr * outR - r[i]);
        }
    }

    // 8 Trancer: each period, gain drops to Y² within 1 % of the period and recovers following u⁴·(50(1 − X)² + 1);
    // Wet Mix is not used (REA_rodada19 §1.1)
    void trance (float* l, float* r, int n, float x, float y, double phase, double beats, double bpm) noexcept
    {
        const auto k = 50.0 * (1.0 - x) * (1.0 - x) + 1.0;
        const auto depth = 1.0 - (double) y * y;
        const auto step = 1.0 / std::max (1.0, beats * 60.0 / std::max (bpm, 20.0) * fs);
        auto phi = phase;
        for (int i = 0; i < n; ++i)
        {
            double e = 0.0;
            if (phi < 0.01)
                e = 100.0 * phi;
            else
            {
                const auto u = (phi - 0.01) / 0.99;
                e = 1.0 - std::min (1.0, u * u * u * u * k);
            }
            trancer += 0.01 * (e - trancer);
            const auto g = (float) (1.0 - trancer * depth);
            l[i] *= g;
            r[i] *= g;
            phi += step;
            if (phi >= 1.0)
                phi -= 1.0;
        }
    }

    // 9 Stutter: records 1.5 s from reset and repeats the start every period (192-sample crossfade)
    void stutterUnit (float* l, float* r, int n, float wet, double periodSeconds) noexcept
    {
        const auto capacity = std::min (stutter[0].size() - 2, (std::size_t) (1.5 * fs));
        const auto step = 1.0 / std::max (1.0, periodSeconds * fs);
        constexpr int fade = 192;
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            if (recorded < capacity)
            {
                stutter[0][recorded] = l[i];
                stutter[1][recorded] = r[i];
                ++recorded;
            }
            stutterPhase += step;
            if (stutterPhase >= 1.0 || playPos + 1 >= capacity)
            {
                stutterPhase -= std::floor (stutterPhase);
                fadeFrom = playPos;
                fadeLeft = fade;
                playPos = 0;
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                if (playPos >= recorded)
                    continue; // not recorded yet: pass the input through
                auto v = stutter[(std::size_t) ch][playPos];
                if (fadeLeft > 0 && fadeFrom < recorded)
                {
                    const auto f = (float) fadeLeft / (float) fade;
                    v = v * (1.0f - f) + stutter[(std::size_t) ch][fadeFrom] * f;
                }
                s += w * (v - s);
            }
            ++playPos;
            if (fadeLeft > 0)
            {
                --fadeLeft;
                ++fadeFrom;
            }
        }
    }

    // 10 Vowel: 8 parallel band-passes with Q = 12; X' sweeps the sets A→B→C→A; Y' scales by 2^(1.86Y' − 1)
    void vowelUnit (float* l, float* r, int n, float x, float y, float wet) noexcept
    {
        static constexpr double sets[3][4] { { 250, 700, 2500, 3500 }, { 700, 1250, 2500, 3500 }, { 250, 2500, 2700, 3500 } };
        static constexpr double upper[4] { 4200, 8500, 10000, 12000 };
        const auto pos = 3.0 * x;
        const auto index = std::min (2, (int) pos);
        const auto frac = x >= 1.0f ? 0.0 : pos - index;
        const auto from = x >= 1.0f ? 0 : index, to = (from + 1) % 3;
        const auto scale = std::pow (2.0, 2.0 * 0.93 * y - 1.0);
        for (int b = 0; b < 8; ++b)
        {
            const auto hz = b < 4 ? sets[from][b] + frac * (sets[to][b] - sets[from][b]) : upper[b - 4];
            vowel[(std::size_t) b].bandPass (hz * scale, 12.0, fs);
        }
        const auto level = 1.0 - 0.125 * (x >= 1.0f ? 0 : index);
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& s = ch == 0 ? l[i] : r[i];
                double sum = 0.0;
                for (auto& band : vowel)
                    sum += band.process (s, ch);
                s = (float) ((1.0 - w) * s + w * sum * level);
            }
        }
    }

    // 11 Ring: sine carrier at 2^(8.7X' + 5) Hz; (1 − w)·x + w·x·sin
    void ring (float* l, float* r, int n, float x, float wet) noexcept
    {
        const auto inc = std::pow (2.0, 8.7 * x + 5.0) / fs;
        for (int i = 0; i < n; ++i)
        {
            const auto w = nextWet (wet);
            const auto m = (float) std::sin (2.0 * pi * ringPhase);
            ringPhase += inc;
            ringPhase -= std::floor (ringPhase);
            l[i] = (1.0f - w) * l[i] + w * l[i] * m;
            r[i] = (1.0f - w) * r[i] + w * r[i] * m;
        }
    }

    double fs = 44100.0, songBeats = 0.0;
    std::array<std::vector<float>, 2> delayLine, stutter, shortLine;
    std::size_t write = 0, shortWrite = 0, recorded = 0, playPos = 0, fadeFrom = 0;
    int fadeLeft = 0;
    double stutterPhase = 0.0;
    double lp[2] {}, echoFilter[2] {};
    float hold[2] {};
    int holdCount = 0;
    std::array<std::array<double, 6>, 2> phaserState {};
    double phaserFeedback[2] {}, phaserSweep = -1.0;
    std::array<std::array<std::array<double, 8>, 4>, 2> filterLow {}; // Filter, low-pass: 4 sections × 8 poles
    std::array<std::array<double, 32>, 2> filterHigh {};              // Filter, high-pass: 32 stages
    // Flanger "tape": samples written at a position that advances by `tapeRate` per sample, read 1 unit behind
    static constexpr std::size_t tapeSize = 4096, tapeMask = tapeSize - 1;
    std::array<double, tapeSize> tapeTime {};
    std::array<std::array<double, tapeSize>, 2> tapeValue {};
    std::size_t tapeWrite = 0, tapeRead = 0;
    double tapePosition = 0.0, tapeRate = -1.0;
    double trancer = 0.0, ringPhase = 0.0;
    std::array<Biquad, 8> vowel {};
    float wetSmooth = -1.0f;
    std::unique_ptr<Effect> reverb;
};
} // namespace

int typeOf (float v) noexcept
{
    return std::clamp ((int) (11.0f * v + 0.5f), 0, typeCount - 1);
}

const char* typeName (int t) noexcept
{
    static constexpr const char* names[typeCount] { "Distort", "Lo-fi", "Flanger", "Phaser", "Filter", "Delay",
                                                    "Reverb", "Stereo", "Trancer", "Stutter", "Vowel", "Ring" };
    return names[std::clamp (t, 0, typeCount - 1)];
}

double rateBeats (float modRate, float tempoReduce) noexcept
{
    const auto reduce = 1.0 - (double) clamp01 (tempoReduce);
    return std::max (1e-3, rates[(std::size_t) rateIndex (modRate)] * reduce * reduce);
}

std::string formatParam (int param, float v, double bpm)
{
    char text[48];
    switch (param)
    {
        case type: return typeName (typeOf (v));
        case bypass: return v != 0.0f ? "on" : "off";
        case modRate:
            std::snprintf (text, sizeof (text), "%s (%.0f ms)", rateNames[(std::size_t) rateIndex (v)],
                           rates[(std::size_t) rateIndex (v)] * 60000.0 / std::max (bpm, 20.0));
            return text;
        case modX: case modY: std::snprintf (text, sizeof (text), "%+.0f%%", modAmount (v) * 100.0f); return text;
        case modShape:
        {
            static constexpr const char* shapes[3] { "Sine", "Saw", "Square" };
            return shapes[std::clamp ((int) (2.0f * v + 0.5f), 0, 2)];
        }
        case inputLevel: std::snprintf (text, sizeof (text), "%.1f dB", 40.0 * std::log10 (std::max (1e-4f, v))); return text;
        case outputGain: std::snprintf (text, sizeof (text), "%+.1f dB", (v - 0.5f) * 48.0f); return text;
        default: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); return text;
    }
}

std::unique_ptr<Effect> make()
{
    return std::make_unique<Effector>();
}
} // namespace lynx::effector
