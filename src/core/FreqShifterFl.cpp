#include "core/FreqShifterFl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Denormals.h"
#include "core/IldspMath.h"
#include "core/Song.h"

// Clean-room rebuild of FL's Frequency Shifter engine (docs/analise/REA_rodada154.md §4). Float32 like the engine,
// coefficient design in double; the float math routines are the ones of the engine's C run-time (IldspMath, tanhf_).

#pragma clang fp contract(off)

namespace lynx::fsfl
{
namespace
{
inline float ffrom (std::uint32_t u) { float f; std::memcpy (&f, &u, 4); return f; }
inline std::uint32_t fbits (float f) { std::uint32_t u; std::memcpy (&u, &f, 4); return u; }

constexpr float twoPiF = 6.2831855f;
constexpr double butterworthQ[4] { 0.509795606136322, 0.601344883441925, 0.8999761939048767, 2.562915325164795 };
// the Hilbert pair's all-pass coefficients (first chain, then the second)
constexpr float hilbertCoef[8] { 0.4794009f, 0.8762185f, 0.97659755f, 0.9974992f, 0.1617585f, 0.7330289f, 0.94534963f, 0.99059916f };

// RBJ low-pass (type 0) / high-pass (type 1) as the engine designs them, coefficients stored as b0, b1, b2, −a1, −a2
void designLpHp (int type, double f, double fs, double q, float* o) noexcept
{
    const double w = (f * 6.283185307179586) / fs;
    const double s = std::sin (w), c = std::cos (w);
    const double alpha = (0.5 / q) * s;
    const double a1 = c * -2.0, a2 = 1.0 - alpha, a0 = alpha + 1.0;
    double b0, b1;
    if (type == 0)
    {
        b1 = 1.0 - c;
        b0 = b1 * 0.5;
    }
    else
    {
        b0 = (c + 1.0) * 0.5;
        b1 = -(c + 1.0);
    }
    const double inv = 1.0 / a0;
    o[0] = (float) (inv * b0);
    o[1] = (float) (inv * b1);
    o[2] = (float) (inv * b0);
    o[3] = -(float) (inv * a1);
    o[4] = -(float) (inv * a2);
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

float controlValue (int k, int raw) noexcept
{
    const double x = (double) (raw - rawMin[(std::size_t) k]) * (1.0 / (rawMax[(std::size_t) k] - rawMin[(std::size_t) k]));
    return (float) std::clamp (x, 0.0, 1.0);
}
} // namespace

// the engine's tanhf: a rational fit below 1, 1 − 2/(e^{2x} + 1) with a 32-entry exp table up to 10, 1 beyond
float tanhf_ (float x) noexcept
{
    static const std::uint32_t lead[32] { 1065353216u, 1065533440u, 1065713664u, 1065910272u, 1066106880u, 1066303488u, 1066516480u,
                                          1066713088u, 1066926080u, 1067155456u, 1067368448u, 1067597824u, 1067827200u, 1068072960u,
                                          1068318720u, 1068564480u, 1068826624u, 1069072384u, 1069350912u, 1069613056u, 1069891584u,
                                          1070170112u, 1070465024u, 1070759936u, 1071071232u, 1071366144u, 1071693824u, 1072005120u,
                                          1072349184u, 1072676864u, 1073020928u, 1073381376u };
    static const std::uint32_t trail[32] { 0u, 970484104u, 984288671u, 977302532u, 976097054u, 982574637u, 955571424u, 986955427u,
                                           987742534u, 970169244u, 987023746u, 985730060u, 989551270u, 981822163u, 977401682u, 982504967u,
                                           958293624u, 988724509u, 965360116u, 984691497u, 983345221u, 988214809u, 982659156u, 983594567u,
                                           958372184u, 988733023u, 969797312u, 988027805u, 935782336u, 982705775u, 982905940u, 955037392u };
    const std::uint32_t ux = fbits (x), ax = ux & 0x7fffffffu;
    if (ax < 0x39000000u)
        return x;
    if (ax > 0x7f800000u)
        return x + x;
    float two = (float) (ax != ux ? 1 : 0);
    two = two + two;
    const float sgn = 1.0f - two;
    const float y = sgn * x;
    float r = 1.0f;
    if (y > 10.0f)
        r = 1.0f;
    else if (! (1.0f < y))
    {
        const float y2 = y * y, y3 = y2 * y;
        float p, q;
        if (ffrom (1063675494u) > y)
        {
            p = (y2 * ffrom (944581501u) - ffrom (985644190u)) * y2 - ffrom (1049647328u);
            q = y2 * ffrom (1051686556u) + ffrom (1062765904u);
        }
        else
        {
            p = (y2 * ffrom (941656536u) - ffrom (983666176u)) * y2 - ffrom (1047951793u);
            q = y2 * ffrom (1050003039u) + ffrom (1060690784u);
        }
        r = (p * y3) / q + y;
    }
    else
    {
        const float z = y + y;
        float t = z * ffrom (1111009851u);
        t = t > 0.0f ? t + 0.5f : t - 0.5f;
        const int n = (int) t;
        const int j = n & 31, m = n & ~31;
        const float r1 = z - (float) n * ffrom (1018261504u);
        const float r2 = (float) (-n) * ffrom (897973748u);
        const int qd = m / 32, h = qd / 2;
        const float s1 = ffrom ((std::uint32_t) (h + 0x7f) << 23), s2 = ffrom ((std::uint32_t) (qd - h + 0x7f) << 23);
        const float rr = r2 + r1;
        float pp = rr * ffrom (1042983595u) + 0.5f;
        pp = pp * (rr * rr);
        const float T1 = ffrom (lead[j]), T2 = ffrom (trail[j]);
        pp = pp + r2;
        pp = pp + r1;
        pp = pp * (T2 + T1);
        pp = pp + T2;
        pp = pp + T1;
        pp = pp * s1;
        pp = pp * s2;
        r = 1.0f - 2.0f / (pp + 1.0f);
    }
    return r * sgn;
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4 + 4 * numParams)
        return std::nullopt;
    if (i32At (d, 0) > 4)
        return std::nullopt;
    State s;
    for (int k = 0; k < numParams; ++k)
        s.raw[(std::size_t) k] = std::clamp (i32At (d, 4 + 4 * (std::size_t) k), rawMin[(std::size_t) k], rawMax[(std::size_t) k]);
    return s;
}

void toParams (const State& s, float* params) noexcept
{
    for (int k = 0; k < numParams; ++k)
        params[k] = (float) ((double) (s.raw[(std::size_t) k] - rawMin[(std::size_t) k])
                             / (double) (rawMax[(std::size_t) k] - rawMin[(std::size_t) k]));
}

// --- engine ----------------------------------------------------------------------------------------------------------

float Engine::Biquad::run (float x) noexcept
{
    const float y = ((c[3] * y1 + c[4] * y2) + c[2] * x2) + (x * c[0] + x1 * c[1]);
    y2 = y1;
    x2 = x1;
    x1 = x;
    y1 = y;
    return y;
}

void Engine::Bank::design (float fLow, float fHigh) noexcept
{
    count = 8;
    for (int k = 0; k < 4; ++k)
    {
        float lo[5], hi[5];
        designLpHp (0, (double) fLow, st[k].rate, butterworthQ[k], lo);
        designLpHp (1, (double) fHigh, st[k].rate, butterworthQ[k], hi);
        for (int i = 0; i < 5; ++i)
        {
            st[k].c[i][0] = st[k].c[i][1] = lo[i];
            st[k].c[i][2] = st[k].c[i][3] = hi[i];
        }
    }
    for (int k = 0; k < 4; ++k)
        std::memcpy (st[k + 4].c, st[k].c, sizeof st[k].c);
}

void Engine::Bank::run (float* v) noexcept
{
    for (int k = 0; k < count; ++k)
    {
        auto& s = st[k];
        for (int l = 0; l < 4; ++l)
        {
            const float x = v[l];
            float y = s.x1[l] * s.c[1][l] + x * s.c[0][l];
            y = y + s.c[2][l] * s.x2[l];
            y = y + s.y1[l] * s.c[3][l];
            y = y + s.c[4][l] * s.y2[l];
            s.x2[l] = s.x1[l];
            s.x1[l] = x;
            s.y2[l] = s.y1[l];
            s.y1[l] = y;
            v[l] = y;
        }
    }
}

void Engine::Hilbert::run (const float* in, float* a, float* b) noexcept
{
    for (int l = 0; l < 4; ++l)
        a[l] = delayed[l];
    for (int chain = 0; chain < 2; ++chain)
    {
        float v[4];
        std::memcpy (v, in, sizeof v);
        for (int k = chain * 4; k < chain * 4 + 4; ++k)
            for (int l = 0; l < 4; ++l)
            {
                auto& s = st[k][l]; // x1, x2, y1, y2
                float t = v[l] + s[3];
                t = t * coef[k][l];
                t = t - s[1];
                s[3] = s[2];
                s[1] = s[0];
                s[0] = v[l];
                s[2] = t;
                v[l] = t;
            }
        if (chain == 0)
            std::memcpy (delayed, v, sizeof v);
        else
            std::memcpy (b, v, sizeof v);
    }
}

Engine::Engine()
{
    for (int k = 0; k < 8; ++k)
        for (int l = 0; l < 4; ++l)
            hilbert.coef[k][l] = hilbertCoef[k];
    // the constructor: 44.1 kHz, then every control at its default
    setRate (44100.0);
    for (int i = 0; i < numParams; ++i)
    {
        float v = 0.5f;
        if (i == 0)
            v = 1.0f;
        else if (i == 1 || i == 3 || i == 8)
            v = 0.0f;
        target[i] = cur[i] = v;
        setParam (i, v);
    }
}

void Engine::setRate (double fs) noexcept
{
    rate = fs;
    hpL.rate = hpR.rate = fs;
    designLpHp (1, 20.0, fs, 0.5, hpL.c);
    designLpHp (1, 20.0, fs, 0.5, hpR.c);
    designLpHp (0, 20000.0, lpL.rate, 0.5, lpL.c); // the low-pass keeps its construction rate (44.1 kHz), as FL's
    designLpHp (0, 20000.0, lpR.rate, 0.5, lpR.c);
    fadeRate = 499.99997f / (float) fs;
    kSmooth = (float) (1.0 - std::pow (0.09999999999999998, 200.0 / fs));
    kFreq = (float) (1.0 - std::pow (0.09999999999999998, 10.0 / fs));
    invRateD = 1.0 / fs;
    invRate = (float) invRateD;
    for (auto& b : banks)
        for (auto& s : b.st)
            s.rate = fs;
}

void Engine::setControl (int i, float v) noexcept
{
    if (i < 0 || i >= numParams)
        return;
    target[i] = v;
    if (i == 1 || i == 2 || i == 8)
        setParam (i, v);
}

void Engine::setParam (int i, float v) noexcept
{
    switch (i)
    {
        case 0:
        {
            const float t = v + v;
            wet = t >= 0.0f ? std::min (t, 1.0f) : 0.0f;
            const float d = 2.0f - t;
            dry = d >= 0.0f ? std::min (d, 1.0f) : 0.0f;
            break;
        }
        case 1:
            hq = v < 0.6666f ? v < 0.3333f : false;
            shifter = v < 0.6666f ? 1 : 0;
            break;
        case 2: freqTarget = v; break;
        case 3: lrOffset = v * 0.25f; break;
        case 4:
            shapeL = v;
            shapeL1 = 1.0f - v;
            break;
        case 5:
            shapeR = v;
            shapeR1 = 1.0f - v;
            break;
        case 6: fb = (v * 1.005f + v * 1.005f) - 1.005f; break;
        case 7: stereo = (1.0f - v) * 0.5f; break;
        case 8:
        {
            const bool t = v >= 0.6666f;
            if (t != tempoRange)
            {
                tempoRange = t;
                restart = 1;
            }
            range200 = v >= 0.3333f && v < 0.6666f;
            break;
        }
        case 9: phase0 = v + 0.5f; break;
        default: break;
    }
}

void Engine::glide() noexcept
{
    static constexpr int order[] { 0, 4, 5, 6, 7, 3, 9 };
    for (int i : order)
    {
        if (cur[i] == target[i])
            continue;
        const float c = (target[i] - cur[i]) * kSmooth + cur[i];
        cur[i] = c;
        float v = c;
        if (std::fabs (c - target[i]) < 1.0e-5f)
        {
            cur[i] = target[i];
            v = target[i];
        }
        setParam (i, v);
    }
}

float Engine::frequencyHz (float x) const noexcept
{
    const float k = range200 ? 10.965784f : 17.60964f;
    if (x >= 0.5f)
    {
        const float s = ((x - 0.5f) + x) - 0.5f;
        if (s == 0.0f)
            return 0.0f;
        return ildsp::powf_ (2.0f, s * k) * 0.1f;
    }
    float t = 0.5f - x;
    t = t + t;
    if (t == 0.0f)
        return -0.0f;
    return ildsp::powf_ (2.0f, k * t) * -0.1f;
}

void Engine::process (float& left, float& right, double tempo, double hostPos, double hostNext) noexcept
{
    const bool on = hostPos >= 0.0;
    if (! playing && on)
    {
        restart |= 1;
        pos = hostPos;
        playing = on;
        next = hostNext;
    }
    else
    {
        playing = on;
        if (on)
        {
            pos = hostPos;
            next = hostNext;
        }
        else
        {
            const double d = tempo * 0.016666666666666666 * invRateD;
            pos = d + pos;
            next = pos + d;
        }
    }
    glide();
    {
        const float d = freqTarget - freq;
        freq = std::fabs (d) > 1.0e-4f ? d * kFreq + freq : freqTarget;
    }
    const float inL = left, inR = right;
    const float pL = lpL.run (hpL.run (tanhf_ (fb * lastL) + inL));
    const float pR = lpR.run (hpR.run (tanhf_ (fb * lastR) + inR));

    float f = 0.0f; // the shift in Hz (or the tempo period's frequency) — what the HQ crossover is designed around
    if (tempoRange)
    {
        double d;
        if (shifter == 0)
            d = std::pow (2.0, (double) ((1.0f - freq) * 7.0f)) * 48.0;
        else if (freq >= 0.5f)
            d = std::pow (2.0, (double) (7.0f - (freq - 0.5f) * 14.0f)) * 48.0;
        else
            d = std::pow (2.0, (double) (7.0f - (0.5f - freq) * 14.0f)) * -48.0;
        const double period = (double) (int) d * 0.005208333333333333;
        if (restart != 0)
        {
            restart = 0;
            if (period > 0.0)
            {
                const double t = (double) ((float) (pos / period) + phase0);
                phase = t - (double) (int) t;
            }
            else
            {
                const double t = (double) (float) ((-1.0 / period) * pos);
                double u = 1.0 - (t - (double) (int) t) + (double) phase0;
                phase = u - (double) (int) u;
            }
        }
        inc = (double) (float) ((next - pos) / period);
        f = inc == 0.0 ? 0.0f : (float) (1.0 / (inc * rate));
    }
    else
    {
        if (restart != 0)
        {
            restart = 0;
            phase = (double) phase0;
        }
        if (shifter == 0)
        {
            if (freq == 0.0f)
                f = 0.0f;
            else
                f = ildsp::powf_ (2.0f, freq * (range200 ? 10.965784f : 17.60964f)) * 0.1f;
        }
        else
            f = frequencyHz (freq);
        inc = (double) (f * invRate);
    }

    float wetL, wetR, dryL = inL, dryR = inR;
    if (shifter == 0)
    {
        // ring modulator (the side chain input is not routed in LYNX: the oscillator path)
        const float ph = (float) phase;
        wetL = (float) (std::cos ((double) (ph * twoPiF)) * (double) pL);
        float t = ph + lrOffset;
        t = (t - (float) (int) t) * twoPiF;
        wetR = (float) (std::cos ((double) t) * (double) pR);
        const double u = inc + phase;
        phase = u - (double) (int) u;
    }
    else
    {
        float v[4];
        if (hq)
        {
            if (counter == 0)
            {
                fadeStep = f >= 0.0f ? fadeRate : -fadeRate;
                const float af = std::fabs (f);
                float lo = 22050.0f - af;
                lo = lo < 20.0f ? 20.0f : std::min (lo, 20000.0f);
                const float hi = af >= 20.0f ? std::min (af, 20000.0f) : 20.0f;
                banks[0].design (lo, hi);
                banks[1].design (hi, hi);
                banks[2].design (lo, lo);
                banks[3].design (lo, lo);
                banks[4].design (hi, hi);
            }
            counter = counter + 1 == 32 ? 0 : counter + 1;
            float s[4] { pL, pR, pL, pR };
            banks[0].run (s);
            float a[4] { s[0], s[1], s[0], s[1] }, b[4] { s[2], s[3], s[2], s[3] };
            banks[1].run (a);
            banks[2].run (b);
            const float A0 = a[0] + a[2], A1 = a[1] + a[3], B0 = b[0] + b[2], B1 = b[1] + b[3];
            float d[4] { inL, inR, inL, inR };
            banks[3].run (d);
            const float s0 = d[0] + d[2], s1 = d[1] + d[3];
            float e[4] { s0, s1, s0, s1 };
            banks[4].run (e);
            dryL = e[0] + e[2];
            dryR = e[1] + e[3];
            const float m = fade, m1 = 1.0f - fade;
            v[0] = A0 * m + B0 * m1;
            v[1] = A1 * m + B1 * m1;
            v[2] = B0 * m + A0 * m1;
            v[3] = B1 * m + A1 * m1;
            const float nm = m + fadeStep;
            fade = nm;
            if (! (nm >= 0.0f))
                fade = 0.0f;
            else if (nm > 1.0f)
                fade = 1.0f;
        }
        else
        {
            v[0] = pL;
            v[1] = pR;
            v[2] = pL;
            v[3] = pR;
        }
        float a[4], b[4];
        hilbert.run (v, a, b);
        const float ph = (float) phase;
        const float x = ph * twoPiF;
        float t = ph + lrOffset;
        t = (t - (float) (int) t) * twoPiF;
        const float s0 = ildsp::vsin1 (x), s1 = ildsp::vsin1 (x + 1.5707964f), s2 = ildsp::vsin1 (t), s3 = ildsp::vsin1 (t + 1.5707964f);
        double u = inc + 1000.0;
        u = u + phase;
        phase = u - (double) (int) u;
        wetL = (b[2] * s1 + a[2] * s0) * shapeL1 + (b[0] * s1 - a[0] * s0) * shapeL;
        wetR = (b[3] * s3 + a[3] * s2) * shapeR1 + (b[1] * s3 - a[1] * s2) * shapeR;
    }
    lastL = wetL;
    lastR = wetR;
    const float mixL = wet * wetL + dry * dryL;
    const float mixR = wet * wetR + dry * dryR;
    left = stereo * mixR + (1.0f - stereo) * mixL;
    right = (1.0f - stereo) * mixR + mixL * stereo;
}

// --- effect ------------------------------------------------------------------------------------------------------------

std::string paramText (int param, float v01)
{
    char b[48];
    const float v = std::clamp (v01, 0.0f, 1.0f);
    switch (param)
    {
        case 0: std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); return b;
        case 1: return v < 1.0f / 3.0f ? "Frequency shifter HQ" : (v < 2.0f / 3.0f ? "Frequency shifter" : "Ring modulator");
        case 2:
        {
            const float s = 2.0f * v - 1.0f;
            const double hz = s == 0.0f ? 0.0 : (s < 0 ? -0.1 : 0.1) * std::pow (2.0, 17.60964 * std::fabs (s));
            std::snprintf (b, sizeof b, "%.1f Hz (20 kHz range)", hz);
            return b;
        }
        case 3: std::snprintf (b, sizeof b, "%.0f°", v * 90.0); return b;
        case 6: std::snprintf (b, sizeof b, "%+.0f%%", (2.0 * v - 1.0) * 100.0); return b;
        case 8: return v < 1.0f / 3.0f ? "20 kHz" : (v < 2.0f / 3.0f ? "200 Hz" : "Tempo");
        case 9: std::snprintf (b, sizeof b, "%.0f°", (v + 0.5) * 360.0 - 360.0 * (v + 0.5 >= 1.0)); return b;
        default: std::snprintf (b, sizeof b, "%.0f%%", v * 100.0); return b;
    }
}

namespace
{
class FreqShifterEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        fs = sampleRate;
        ready = false;
    }
    void reset() noexcept override {}
    void setSongPosition (double ticks) noexcept override { songBeats = ticks / (double) lynx::ppq; }
    void setPlaying (bool on) noexcept override { transport = on; }
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        std::array<int, numParams> raw {};
        for (int k = 0; k < numParams; ++k)
            raw[(std::size_t) k] = rawMin[(std::size_t) k]
                                   + (int) std::lround (std::clamp (params[(std::size_t) k], 0.0f, 1.0f)
                                                        * (rawMax[(std::size_t) k] - rawMin[(std::size_t) k]));
        if (! ready)
        {
            engine = std::make_unique<Engine>(); // a fresh plugin, its rate, then the state's controls in index order
            engine->setRate (fs);
            for (int k = 0; k < numParams; ++k)
                engine->setControl (k, controlValue (k, raw[(std::size_t) k]));
            current = raw;
            ready = true;
        }
        else
            for (int k = 0; k < numParams; ++k)
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    engine->setControl (k, controlValue (k, raw[(std::size_t) k]));
                    current[(std::size_t) k] = raw[(std::size_t) k];
                }
        ScopedFlushDenormals ftz;
        const double step = bpm / 60.0 / fs;
        for (int i = 0; i < n; ++i)
        {
            const double p = transport ? songBeats + i * step : -1.0;
            engine->process (left[i], right[i], bpm, p, transport ? p + step : -1.0);
        }
    }

private:
    std::unique_ptr<Engine> engine;
    std::array<int, numParams> current {};
    double fs = 44100.0, songBeats = 0.0;
    bool ready = false, transport = false;
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<FreqShifterEffect>(); }
} // namespace lynx::fsfl
