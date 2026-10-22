#pragma once

// Matched voicings (REA round 123): engines that reproduce, as closely as black-box measurement allows, the
// plug-ins her vocal projects use when they are missing. They are selected by a "Voicing" parameter of the LYNX
// effect that substitutes the plug-in (Ridge Strip, Hush De-esser, Compressor, Glow Leveler); the effect's own
// engine stays untouched at voicing 0. Every law here was measured on the vendors' official trials hosted
// headless (tools/fxmeasure: impulses, sine stairs, tone bursts, pink noise, her own takes) and fitted; no
// plug-in code or data was read. Evidence: docs/analise/REA_rodada123.md.

#include <array>
#include <cstdint>

#include "core/Biquad.h"

namespace lynx::vmatch
{
// ---- E-type console channel strip (Ridge Strip, voicing "E") ----
namespace echannel
{
// Controls in the units the console's own dials show (the measured behaviour follows the displayed, rounded values)
struct Settings
{
    double inputDb = 0.0, outputDb = 0.0;
    bool phase = false;
    double hpHz = 0.0;  // 0 = out, else 17 .. 350
    double lpKHz = 0.0; // 0 = out, else 19.5 .. 3
    double lfGain = 0.0, lfHz = 60.0;
    bool lfBell = false;
    double lmfGain = 0.0, lmfKHz = 0.5, lmfQ = 2.5;
    double hmfGain = 0.0, hmfKHz = 3.5, hmfQ = 1.5;
    double hfGain = 0.0, hfKHz = 8.0;
    bool hfBell = false;
    bool eqOn = true;
    double compThreshold = 0.0, compRatio = 1.0, compRelease = 0.4; // dB on the console's scale (+10 .. -20), x:1, s
    bool compFast = false;
    double expThreshold = -30.0, expRange = 0.0, expRelease = 0.2; // console dB (-30 .. +10), dB, s
    bool expFast = false, gate = false;
    bool dynOn = true;
    bool filtersBeforeDynamics = true; // "split": the filters run before the dynamics (otherwise with the EQ)
    bool dynamicsAfterEq = false;      // "dynamics to channel out"
};

// Q of the mid bands as the console's dial shows it, for a 0..1 dial position (3.5 at 0 .. 0.1 at 1)
double qFromDial (double v) noexcept;
double dialFromQ (double q) noexcept;

// One EQ band / filter as a digital biquad at `sampleRate` (band 0 LF, 1 LMF, 2 HMF, 3 HF). False = flat (skip it).
bool bandBiquad (int band, const Settings& s, double sampleRate, Biquad& out);
// The 3rd-order high-pass as a first-order section and a biquad
bool highPass (const Settings& s, double sampleRate, Biquad& first, Biquad& second);
bool lowPass (const Settings& s, double sampleRate, Biquad& out);

// Magnitude of the filters + EQ (dB) at `hz`
double responseDb (const Settings& s, double sampleRate, double hz);
// The compressor's static output level (dBFS peak of a sine) for a steady sine input level, from the model
double compStaticGainDb (const Settings& s, double sineDbfs);

class Channel
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void process (float* l, float* r, int n, const Settings& s) noexcept;
    float compReductionDb() const noexcept { return lastComp; }
    float gateReductionDb() const noexcept { return lastGate; }

private:
    void design (const Settings& s) noexcept;
    double sr = 48000.0;
    Settings designed {};
    bool designedOnce = false;
    Biquad hp1, hp2, lp, bands[4];
    bool hpOn = false, lpOn = false, bandOn[4] {};
    // dynamics state
    double env = 0.0, gr = 0.0, gr1 = 0.0, yl = 0.0, yr = 0.0;
    double genv = 0.0, ggr = 0.0;
    float lastComp = 0.0f, lastGate = 0.0f;
};
} // namespace echannel
} // namespace lynx::vmatch
