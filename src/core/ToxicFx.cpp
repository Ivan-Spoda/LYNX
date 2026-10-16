#include "core/ToxicFx.h"

#include "core/IlFxKit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// 1:1 port of our round-45 reference model (r45/fx/toxfx.py + crtpow.py). The model emulates the DLL's float32
// arithmetic with numpy float32 scalars and uses Python floats (doubles) where the DLL computes in double. This file
// keeps those types — `float` where the model holds float32, `double` + an explicit (float) where it rounds a double —
// and the operation order of every expression, so fusing a·b + c into one FMA would change the results: contraction
// stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
constexpr float th5 = 9.99999975e-06f; // 1e-5f
constexpr float fade = 0.004999999888241291f;
using namespace ilfx;

// ------------------------------------------------------------------------------------------------ effect slot
// one slot (REA_rodada45 §FX.2–FX.7): 0 delay, 1 chorus, 2 reverb, 3 flanger, 4 phaser, 5 lo-fi
constexpr double syncFactor[10] = { 0.0, 0.066666666666666666, 0.033333333333333333, 0.022222222222222223, 0.016666666666666666,
                                    0.0083333333333333332, 0.0055555555555555558, 0.0041666666666666666, 0.0020833333333333333,
                                    0.0010416666666666667 };

struct Slot
{
    // parameter fields (indices of the even plug-in parameter 238…292, 304)
    float flDelay = 0, flFb = 0, flMix = 0, flDepth = 0, flRate = 0, flInvMix = 0, flInvFb = 0;
    float chDepth = 0, chRate = 0, chMix = 0;
    float dlTime = 0, dlSync = 0, dlFb = 0, dlRate = 0, dlDepth = 0, dlBlur = 0, dlMix = 0;
    float rvMix = 0, rvDecay = 0, rvDamp = 0, rvHiCut = 0;
    float lfBits = 0, lfSmp = 0, lfFlt = 0;
    float phRate = 0, phDepth = 0, phFb = 0, phMix = 0, phFrq = 0;

    Tape lines[6];
    Reverb rev;
    LpBiquad lpL, lpR;
    bool bypass = true, valid = false;
    double rate = 0.0;
    int type = -1, pend = 0;
    float g = 0, w = 0;
    int cnt = 0;
    float holdL = 0, holdR = 0;
    float sL[6] {}, sR[6] {}, fbL = 0, fbR = 0, ph = 0, sfrq = 0, smix = 0;

    Slot() { rev.construct(); }

    float* field (int idx) noexcept
    {
        switch (idx - (idx & 1))
        {
            case 238: return &flDelay;
            case 240: return &flFb;
            case 242: return &flDepth;
            case 244: return &flRate;
            case 246: return &flMix;
            case 248: return &flInvFb;
            case 250: return &flInvMix;
            case 252: return &chDepth;
            case 254: return &chRate;
            case 256: return &chMix;
            case 258: return &rvMix;
            case 260: return &rvDecay;
            case 262: return &rvDamp;
            case 264: return &rvHiCut;
            case 266: return &dlTime;
            case 268: return &dlFb;
            case 270: return &dlRate;
            case 272: return &dlDepth;
            case 274: return &dlBlur;
            case 276: return &dlMix;
            case 278: return &lfBits;
            case 280: return &lfSmp;
            case 282: return &lfFlt;
            case 284: return &phFrq;
            case 286: return &phRate;
            case 288: return &phDepth;
            case 290: return &phFb;
            case 292: return &phMix;
            case 304: return &dlSync;
            default: return nullptr;
        }
    }
    void setParam (int idx, float v) noexcept // FUN_1800292d0
    {
        if (v <= 0.0f)
            v = 0.0f;
        auto* f = field (idx);
        if (f == nullptr)
            return;
        if ((f == &rvMix || f == &rvDecay) && v >= 1.0f)
            v = 1.0f;
        *f = v;
        recompute();
    }
    void setEnable (bool on) noexcept // FUN_180028190
    {
        const auto byp = ! on;
        if (byp != bypass)
        {
            if (! byp)
                w = 1.0f;
            bypass = byp;
        }
    }
    void setRate (double fs) // FUN_180027d40
    {
        if (fs != rate)
        {
            rate = fs;
            for (auto& ln : lines)
                ln.reserve ((int) (fs + fs) + 2);
            rev.setRate ((float) fs);
            const auto old = type;
            type = -1;
            applyType (old);
        }
    }
    void applyType (int t) noexcept // FUN_180027d90
    {
        if (! (t >= 0 && t < 6) || t == type)
            return;
        type = t;
        valid = false;
        const auto fs = rate;
        if (t == 3)
        {
            const auto n = i32 (fs * 0.012);
            lines[0].init (n, 0.0);
            lines[1].init (n, 0.0);
        }
        if (t == 1)
        {
            const auto b = (double) i32 (fs * 0.02);
            lines[0].init ((int) cround (b * 1.1), 0.0);
            lines[1].init ((int) cround (b * 0.854), 0.467);
            lines[2].init ((int) cround (b), 0.778);
        }
        if (t == 0)
        {
            const auto n = (int) (unsigned) (fs + fs);
            const auto dn = (double) n;
            lines[0].init (n, 0.0);
            lines[1].init ((int) cround (dn * 0.63), 0.27);
            lines[2].init ((int) cround (dn * 0.41), 0.4);
            lines[3].init (n, 0.3);
            lines[4].init ((int) cround (dn * 0.76), 0.62);
            lines[5].init ((int) cround (dn * 0.54), 0.87);
        }
        valid = true;
        reset();
        recompute();
    }
    int lineCount() const noexcept { return type == 0 ? 6 : type == 1 ? 3 : type == 3 ? 2 : 0; }
    void reset() noexcept // FUN_180027c50
    {
        g = 0.0f;
        w = bypass ? 0.0f : 1.0f;
        if (! valid)
            return;
        for (int k = 0; k < lineCount(); ++k)
            lines[k].reset();
        for (int k = 0; k < 6; ++k)
            sL[k] = sR[k] = 0.0f;
        fbL = fbR = 0.0f;
        rev.reset();
        lpL.clear();
        lpR.clear();
        cnt = 0;
        ph = 0.0f;
        sfrq = smix = -1.0f;
    }
    void recompute() noexcept // FUN_180028d70
    {
        if (! valid)
            return;
        const auto fs = rate;
        auto* ln = lines;
        if (type == 3)
        {
            const auto d = (double) (flDelay * flDelay);
            ln[0].setTime (d), ln[1].setTime (d);
            auto fb = (float) ((double) flFb * 0.98999999999999999);
            if (fb >= 9.99999975e-05f)
            {
                if (flInvFb != 0.0f)
                    fb = -fb;
            }
            else
                fb = 0.0f;
            ln[0].setFb (fb), ln[1].setFb (fb);
            const auto rr = (double) ((flRate * flRate) * 10.0f);
            ln[0].setInc (rr, fs), ln[1].setInc (rr * 1.1000000000000001, fs);
            const auto dp = (double) (flDepth * flDepth);
            ln[0].setDepth (dp), ln[1].setDepth (dp);
            auto m = flMix;
            if ((double) m >= 0.0001)
            {
                if (flInvMix != 0.0f)
                    m = -m;
            }
            else
                m = 9.99999975e-05f;
            ln[0].setGain (m), ln[1].setGain (m);
        }
        if (type == 1)
        {
            const auto rr = (double) (float) ((double) (chRate * 5.0f) + 0.10000000000000001);
            ln[0].setInc (rr * 0.89600000000000002, fs);
            ln[1].setInc (rr, fs);
            ln[2].setInc (rr * 0.68899999999999995, fs);
            const auto gn = (double) -(chMix * chMix);
            const auto dp = (double) ((chDepth * chDepth) * chDepth);
            for (int k = 0; k < 3; ++k)
                ln[k].setGain (gn), ln[k].setDepth (dp);
        }
        if (type == 0)
        {
            const auto d4 = (double) ((dlDepth * dlDepth) * (dlDepth * dlDepth));
            const auto rr = (double) ((dlRate * dlRate) * 20.0f);
            const auto tm = (double) (float) ((double) (dlTime * dlTime) + 0.01);
            for (int k = 0; k < 6; ++k)
            {
                ln[k].setInc ((k & 1) ? rr * 1.1000000000000001 : rr, fs);
                ln[k].setTime (tm);
                ln[k].setFb ((double) dlFb * -0.98999999999999999);
                ln[k].setDepth (d4);
            }
            const auto mx = dlMix, bl = dlBlur;
            ln[0].setGain (mx * mx), ln[1].setGain ((mx * mx) * bl), ln[2].setGain ((bl * bl) * (mx * mx));
            ln[3].setGain (mx * mx), ln[4].setGain ((mx * mx) * bl), ln[5].setGain ((bl * bl) * (mx * mx));
        }
        if (type == 2)
        {
            rev.setp (9, std::fabs (1.0f - rvDamp));
            rev.setp (8, (float) ((double) ((rvDecay * rvDecay) * rvDecay) * 0.59999999999999998));
            rev.setp (12, (float) ((double) (rvMix * 0.999000013f) + 0.001));
            rev.setp (2, (rvHiCut * rvHiCut) * rvHiCut);
            rev.setp (5, 1.0f);
        }
        if (type == 5)
        {
            auto f = (float) (std::pow (2.0, (double) lfFlt * 9.3000000000000007) * 30.0);
            const auto lim = fs * 0.47619047619047616;
            if ((double) f > lim)
                f = (float) lim;
            lpL.design (f, 1.0f, (float) fs);
            lpR.design (f, 1.0f, (float) fs);
        }
    }
    void process (float* L, float* R, int n, double tempo) noexcept // FUN_1800281b0
    {
        if (pend >= 0 && ! lines[0].worms[0].v.empty()) // storage exists once setSampleRate ran
        {
            applyType (pend);
            pend = -1;
        }
        if (! valid)
            return;
        if (bypass)
        {
            if (w == 0.0f)
                return;
            if (w < th5)
            {
                reset();
                return;
            }
        }
        auto* ln = lines;
        if (type == 0)
        {
            const auto sy = i32 (dlSync);
            if (sy != 0)
            {
                const auto x = (sy >= 1 && sy <= 9) ? tempo * syncFactor[sy] : 1.0;
                const auto tm = (double) (float) (0.5 / x);
                for (int k = 0; k < 6; ++k)
                    ln[k].setTime (tm);
            }
            for (int i = 0; i < n; ++i)
            {
                auto x = L[i];
                x = x + ln[0].tick (x * g) * w;
                x = x + ln[1].tick (x * g) * w;
                L[i] = ln[2].tick (x * g) * w + x;
                x = R[i];
                x = x + ln[3].tick (x * g) * w;
                x = x + ln[4].tick (x * g) * w;
                R[i] = ln[5].tick (x * g) * w + x;
                g = (1.0f - g) * fade + g;
                if (bypass)
                    w = w - w * fade;
            }
        }
        else if (type == 3)
        {
            for (int i = 0; i < n; ++i)
            {
                L[i] = ln[0].tick (L[i] * g) * w + L[i];
                R[i] = ln[1].tick (R[i] * g) * w + R[i];
                g = (1.0f - g) * fade + g;
                if (bypass)
                    w = w - w * fade;
            }
        }
        else if (type == 1)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto g0 = g;
                const auto x = (R[i] + L[i]) * g0;
                g = (1.0f - g0) * fade + g0;
                const auto y1 = ln[0].tick (x);
                const auto y3 = ln[2].tick (x);
                const auto y2 = ln[1].tick (x);
                L[i] = (y2 + y1) * w + L[i];
                R[i] = (y2 + y3) * w + R[i];
                if (bypass)
                    w = w - w * fade;
            }
        }
        else if (type == 5)
        {
            if (! bypass)
                w = 4.99999987e-06f; // bits 0x36a7c5ac: makes a later bypass reset at once
            g = 1.0f;
            const auto b = lfBits;
            const auto q = (float) i32 (((b * 1024.0f) * b) * (b * b) + 4.0f);
            const auto inv = 1.0f / q;
            const auto lim = i32 (lfSmp * 40.0f);
            for (int i = 0; i < n; ++i)
            {
                const auto rv = (float) i32 (q * R[i]) * inv;
                if (cnt == 0)
                {
                    holdR = rv;
                    holdL = (float) i32 (q * L[i]) * inv;
                }
                const auto c = cnt + 1;
                cnt = c > lim ? 0 : c;
                L[i] = lpL.tick (holdL) + L[i];
                R[i] = lpR.tick (holdR) + R[i];
            }
        }
        else if (type == 4)
        {
            const auto inv = 1.0 / rate;
            const auto lo = (float) (inv * 50.0), hi = (float) (inv * 20000.0);
            const auto K = (phDepth * phDepth) * (phDepth * 0.5f);
            const auto fb = (float) ((double) powfCrt (phFb, 0.3333333432674408f) * 0.97999999999999998);
            auto frq = phFrq * phFrq;
            if (! (frq > th5))
                frq = th5;
            const auto inc = (double) (float) ((double) ((phRate * phRate) * 44100.0f) * inv) * 0.00050000000000000001;
            auto mix = phMix;
            if (! (mix > th5))
                mix = th5;
            if (0.0f > smix)
                smix = mix;
            if (0.0f > sfrq)
                sfrq = frq;
            const auto rng = hi - lo;
            for (int i = 0; i < n; ++i)
            {
                const auto g0 = g;
                auto xl = g0 * L[i] - 9.99999997e-07f, xr = g0 * R[i] - 9.99999997e-07f;
                g = (1.0f - g0) * fade + g0;
                const auto c = cosfCrt (ph);
                const auto mm = (c + 1.0f) * K;
                auto nph = (float) ((double) ph + inc);
                if ((double) nph > 6.2831853000000004)
                    nph = nph - 6.2831854820251465f;
                ph = nph;
                auto wv = (sfrq + mm) * rng + lo;
                if (! (wv < 0.499000013f))
                    wv = 0.499000013f;
                sfrq = (float) ((double) (frq - sfrq) * 0.001 + (double) sfrq);
                const auto a = (1.0f - wv) / (wv + 1.0f);
                xl = xl + fb * fbL;
                xr = xr + fb * fbR;
                for (int k = 0; k < 6; ++k)
                {
                    const auto tl = sL[k] - a * xl;
                    sL[k] = tl * a + xl;
                    xl = tl;
                    const auto tr = sR[k] - a * xr;
                    sR[k] = tr * a + xr;
                    xr = tr;
                }
                fbL = xl, fbR = xr;
                const auto sm = smix;
                smix = (float) ((double) (mix - sm) * 0.001 + (double) sm);
                L[i] = (xl * sm) * w + L[i];
                R[i] = (xr * sm) * w + R[i];
                if (bypass)
                    w = w - w * fade;
            }
        }
        else if (type == 2)
        {
            if (! bypass)
                w = 4.99999987e-06f;
            g = 1.0f;
            rev.process (L, R, n);
        }
    }
};

} // namespace

// ------------------------------------------------------------------------------------------------ rack
struct ToxicFxRack::Impl
{
    Slot slots[2];
    EqBand eqL[8], eqR[8];
    double eqFs = 0.0, tempo = 120.0;
    bool eqOff = true, sleeping = false;
    float d = 0, dT = 0, mL = 0, mR = 0;
    int counter = 0, limit = 0;

    Impl()
    {
        for (int b = 0; b < 8; ++b)
        {
            const auto f = (float) (250.0 * std::exp2 ((double) (b - 2)));
            const auto t = b == 0 ? 0 : (b == 7 ? 2 : 1);
            eqL[b].init (f, t);
            eqR[b].init (f, t);
        }
    }
    void eqClear() noexcept
    {
        for (int b = 0; b < 8; ++b)
            eqL[b].clear(), eqR[b].clear();
    }
    void rateOnly (double fs) // FUN_1800384b0
    {
        limit = i32 (fs * 2.1000000000000001);
        for (auto& s : slots)
            s.setRate (fs);
        eqFs = fs;
        for (int b = 0; b < 8; ++b)
            eqL[b].design ((float) eqFs), eqR[b].design ((float) eqFs);
        eqClear();
    }
    void reset() noexcept // FUN_180038450
    {
        for (auto& s : slots)
            s.reset();
        eqClear();
        mL = mR = 0.0f;
        d = dT;
        sleeping = true;
    }
    void setParam (int idx, float v) noexcept // FUN_180040370
    {
        if (idx == 1)
            dT = (v * v) * 20.0f;
        else if (idx == 227)
        {
            eqOff = v == 0.0f;
            eqClear();
        }
        else if (idx >= 228 && idx <= 235)
        {
            const auto b = idx - 228;
            const auto x = (double) v;
            const auto y = ((x - 0.5) + x) - 0.5;
            auto gq = y * y;
            if (y < 0.0)
                gq = -gq;
            const auto gn = (float) (gq * 18.0);
            eqL[b].setGain (gn), eqL[b].design ((float) eqFs);
            eqR[b].setGain (gn), eqR[b].design ((float) eqFs);
        }
        else if (idx == 236 || idx == 237)
            slots[idx - 236].setEnable (v != 0.0f);
        else if ((idx >= 238 && idx <= 293) || idx == 304 || idx == 305)
            slots[idx & 1].setParam (idx, v);
        else if (idx == 314 || idx == 315)
            slots[idx - 314].pend = i32 (v);
    }
    void process (float* L, float* R, int n) noexcept // FUN_1800382f0
    {
        auto loud = false;
        for (int i = 0; i < n; ++i) // FUN_180038550: distortion + DC blocker
        {
            const auto xl = L[i], xr = R[i];
            if (xl > th5 || xr > th5 || xl < -th5 || xr < -th5)
                loud = true;
            d = (float) ((double) (dT - d) * 0.01 + (double) d);
            float yl = xl, yr = xr;
            if ((double) d >= 0.001 || (double) dT >= 0.001)
            {
                auto gq = 1.0f - d * 0.0625f;
                gq = gq * gq, gq = gq * gq, gq = gq * gq;
                const auto gain = (float) ((double) gq * 0.69999999999999996 + 0.29999999999999999);
                yl = xl >= 0.0f ? 1.0f - powfCrt (10.0f, -(d * xl)) * (1.0f - xl) : powfCrt (10.0f, d * xl) * (xl + 1.0f) - 1.0f;
                yl = yl * gain;
                yr = xr >= 0.0f ? 1.0f - powfCrt (10.0f, -(d * xr)) * (1.0f - xr) : powfCrt (10.0f, d * xr) * (xr + 1.0f) - 1.0f;
                yr = yr * gain;
            }
            else
                d = dT = 0.0f;
            mL = tiny0 ((yl - mL) * 0.003f + mL);
            mR = tiny0 ((yr - mR) * 0.003f + mR);
            L[i] = yl - mL;
            R[i] = yr - mR;
        }
        auto skip = false;
        if (loud)
            counter = 0, sleeping = false;
        else if (sleeping)
            skip = true;
        if (! skip)
        {
            for (auto& s : slots)
                s.process (L, R, n, tempo);
            if (! eqOff)
                for (int b = 0; b < 8; ++b)
                    for (int i = 0; i < n; ++i)
                        L[i] = eqL[b].tick (L[i]), R[i] = eqR[b].tick (R[i]);
        }
        for (int i = 0; i < n; ++i)
        {
            if (L[i] > th5 || R[i] > th5 || L[i] < -th5 || R[i] < -th5)
            {
                counter = 0;
                break;
            }
            if (counter == 0)
                counter = 1;
        }
        if (counter != 0)
            counter += n;
        if (! loud && counter > limit && ! sleeping)
            reset();
    }
};

ToxicFxRack::ToxicFxRack() : impl (std::make_unique<Impl>()) {}
ToxicFxRack::~ToxicFxRack() = default;

void ToxicFxRack::reset() { impl = std::make_unique<Impl>(); }
void ToxicFxRack::setSampleRate (double sampleRate)
{
    impl->rateOnly (sampleRate);
    impl->reset();
}
void ToxicFxRack::setTempo (double bpm) noexcept { impl->tempo = bpm; }
void ToxicFxRack::setParam (int index, float value) noexcept { impl->setParam (index, value); }
void ToxicFxRack::process (float* left, float* right, int n) noexcept
{
    if (n > 0)
        impl->process (left, right, n);
}
bool ToxicFxRack::active() const noexcept { return ! (impl->sleeping && impl->mL == 0.0f && impl->mR == 0.0f); }
float toxicPowf (float x, float y) noexcept { return powfCrt (x, y); }
} // namespace lynx
