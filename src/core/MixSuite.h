#pragma once

// LYNX's own mixing suite (REA_rodada96): five native effects built only from published DSP knowledge
// (textbooks, papers, AES / DAFx literature and public circuit analyses of classic studio hardware).
// None of them is a model of a particular product; they fill the roles a vocal chain needs:
//
//   Ridge Strip     channel strip: HPF / LPF, 4-band EQ (two curve families), compressor, gate / expander,
//                   processing order, filters to the sidechain, metering
//   Glow Leveler    optical leveler: program-dependent attack / two-stage release of an electro-optical cell,
//                   peak reduction, gain, compress / limit
//   Hush            de-esser: split-band or wideband, listen, frequency / threshold / range, lookahead
//   Aether          air band (high shelf) + harmonic exciter, oversampled
//   Ember           analog-style saturator: tube / tape / transistor / fuzz, tone, oversampled (2x / 4x / 8x), mix
//
// All parameters are 0..1 in the model (EffectParams); the mappings to real units are below. Nothing allocates
// after construction, and every effect reports a latency that does not depend on its parameters, known right
// after prepare() (REA round 87: PDC is planned before the first block).

#include <array>
#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace mixsuite
{
// ---- shared helpers (exposed for the tests and the editor graphs) ----

// Multi-stage half-band FIR oversampler for one channel (2x, 4x or 8x). The latency in base-rate samples is the
// same for every factor (the shorter paths are padded), so switching the quality never moves the PDC.
class Oversampler
{
public:
    static constexpr int maxStages = 3;
    Oversampler();
    void setFactor (int factor) noexcept; // 2, 4 or 8
    int factor() const noexcept { return 1 << stages; }
    void reset() noexcept;
    static int latency() noexcept;        // base-rate samples (the same for every factor)

    // Runs `f` (float → float) at the oversampled rate on each sample of `x` (in place).
    template <typename F>
    void process (float* x, int n, F&& f) noexcept
    {
        for (int i = 0; i < n; ++i)
            x[i] = padOut (run<0> (x[i], f));
    }

private:
    struct Stage
    {
        int taps = 0;                       // non-zero side taps of the half-band (centre excluded)
        std::array<float, 64> coef {};      // h[2j], j = 0 .. taps − 1
        int centreDelay = 0;                // (c − 1) / 2 and (c + 1) / 2 in input samples
        std::array<float, 128> upHist {}, evenHist {}, oddHist {};
        int upPos = 0, downPos = 0;
    };

    template <int level, typename F>
    float run (float x, F& f) noexcept
    {
        if constexpr (level == maxStages)
            return f (padIn (x));
        else
        {
            if (level == stages)
                return f (padIn (x));
            float a, b;
            up (st[(std::size_t) level], x, a, b);
            const auto ya = run<level + 1> (a, f);
            const auto yb = run<level + 1> (b, f);
            return down (st[(std::size_t) level], ya, yb);
        }
    }

    static void up (Stage& s, float x, float& a, float& b) noexcept;
    static float down (Stage& s, float a, float b) noexcept;
    float padIn (float x) noexcept;  // a short delay at the highest rate (makes the latency whole)
    float padOut (float x) noexcept; // base-rate delay up to the common latency

    std::array<Stage, maxStages> st;
    int stages = 2;
    std::array<float, 8> hiPad {};
    int hiPadLen = 0, hiPadPos = 0;
    std::array<float, 64> loPad {};
    int loPadLen = 0, loPadPos = 0;
};

// ---- Ridge Strip (channel strip) ----
namespace ridge
{
enum Param
{
    input, hpf, lpf,
    lfGain, lfFreq, lfBell, lmfGain, lmfFreq, lmfQ, hmfGain, hmfFreq, hmfQ, hfGain, hfFreq, hfBell,
    curve, eqOn,
    compRatio, compThreshold, compRelease, compFastAttack,
    gateRange, gateThreshold, gateRelease, gateFastAttack, expander, dynOn,
    order, filtersToSidechain, output, phase, compMix,
    count
};

double hpfHz (float v) noexcept;      // 0 = off, else 16 .. 350 Hz (18 dB/oct)
double lpfHz (float v) noexcept;      // 0 = off, else 22 kHz .. 3 kHz (12 dB/oct)
double bandGainDb (float v, bool black) noexcept; // ±15 dB (brown) / ±18 dB (black)
double lfHz (float v) noexcept;       // 30 .. 450 Hz
double lmfHz (float v) noexcept;      // 200 Hz .. 2.5 kHz
double hmfHz (float v) noexcept;      // 600 Hz .. 7 kHz
double hfHz (float v) noexcept;       // 1.5 .. 16 kHz
double bandQ (float v) noexcept;      // 0.5 .. 2.5
double ratio (float v) noexcept;      // 1:1 .. 20:1, ∞ at the top (returned as 1e9)
double compThresholdDb (float v) noexcept; // −40 .. 0 dBFS
double releaseSeconds (float v) noexcept;  // 0.1 .. 4 s
double gateRangeDb (float v) noexcept;     // 0 (off) .. 60 dB
double gateThresholdDb (float v) noexcept; // −80 .. 0 dBFS
double trimDb (float v) noexcept;          // ±20 dB (input / output)

// The EQ section's magnitude (filters + 4 bands) in dB at `hz`, as processed at `sampleRate`.
double responseDb (const EffectParams& p, double sampleRate, double hz);
// Static curve of the compressor: output level for a steady input level (dB), no gate.
double compressorCurveDb (const EffectParams& p, double inputDb);
} // namespace ridge

// ---- Glow Leveler (optical leveler) ----
namespace glow
{
enum Param { peakReduction, gain, mode, emphasis, mix, count };
double gainDb (float v) noexcept;   // 0 .. +30 dB make-up
double sidechainDb (float v) noexcept; // peak reduction: −20 .. +40 dB into the cell
} // namespace glow

// ---- Hush (de-esser) ----
namespace hush
{
// Round 128 (parity with the reference de-esser), appended so saved projects keep their meaning, every default
// leaving the output unchanged: allround (detection mode: 0 single vocal, 1 allround = the sibilance must also stand
// out from the full-band level), lowPass (the band's upper edge 2 .. 20 kHz; 1 = off), lookaheadTime (0 .. 15 ms, the
// delay = the latency; default 2 ms), stereoLink (0 .. 100 %), linkMid (detect on the mid signal only), external
// (detect the slot's side chain), input (±12 dB), display (editor only: show the waveform).
enum Param { frequency, threshold, range, mode, detector, listen, lookahead, release, output,
             allround, lowPass, lookaheadTime, stereoLink, linkMid, external, input, display, count };
double hz (float v) noexcept;          // 2 .. 16 kHz
double lowPassHz (float v) noexcept;   // 2 .. 20 kHz (v ≥ 0.999 = off)
double lookaheadTimeMs (float v) noexcept; // 0 .. 15 ms
float lookaheadTimeValue (double ms) noexcept;
double thresholdDb (float v) noexcept; // −60 .. 0 dBFS
double rangeDb (float v) noexcept;     // 0 .. 24 dB
double releaseMs (float v) noexcept;   // 10 .. 250 ms
double lookaheadMs() noexcept;         // the default lookahead (2 ms)
} // namespace hush

// ---- Aether (air band + exciter) ----
namespace aether
{
enum Param { air, airFreq, harmonics, exciterFreq, character, mix, output, count };
double airDb (float v) noexcept;       // 0 .. +15 dB
double airHz (float v) noexcept;       // 5 .. 20 kHz
double exciterHz (float v) noexcept;   // 1 .. 12 kHz
} // namespace aether

// ---- Ember (saturator) ----
namespace ember
{
enum Param
{
    drive, style, tone, lowCut, highCut, mix, output, autoGain, quality,
    // multiband mode (round 108): with bands > 1 the drive / style above are band 1's; the tone, cuts, mix, output,
    // auto gain and oversampling stay global
    bands, xover1, xover2, xover3, xoverSlope, band1Mix, band1Level,
    band2Style, band2Drive, band2Mix, band2Level,
    band3Style, band3Drive, band3Mix, band3Level,
    band4Style, band4Drive, band4Mix, band4Level,
    count
};
constexpr int maxBands = 4;
enum class Style { tube, tape, transistor, fuzz };
constexpr int styles = 4;
Style styleOf (float v) noexcept;
double driveDb (float v) noexcept;     // 0 .. +36 dB
double toneTilt (float v) noexcept;    // −1 (dark) .. +1 (bright), ±6 dB tilt around 1 kHz
double lowCutHz (float v) noexcept;    // 0 = off, else 20 .. 500 Hz (before the drive)
double highCutHz (float v) noexcept;   // 1 = off, else 2 .. 20 kHz (after the drive)
int oversampling (float v) noexcept;   // 2, 4 or 8
float shape (Style s, float x) noexcept; // the transfer curve (no drive, no DC removal)
int bandCount (float v) noexcept;      // 1 .. 4
double crossoverHz (float v) noexcept; // 30 Hz .. 18 kHz (log)
float crossoverValue (double hz) noexcept;
int slopeDbOct (float v) noexcept;     // 12, 24 or 48 (Linkwitz-Riley)
double bandLevelDb (float v) noexcept; // ±18 dB
// a band's style / drive / mix / level parameters (band 0 = the global Style and Drive, band1Mix / band1Level)
int bandParam (int band, int field) noexcept; // field: 0 style, 1 drive, 2 mix, 3 level
} // namespace ember

const EffectInfo& ridgeInfo();
const EffectInfo& glowInfo();
const EffectInfo& hushInfo();
const EffectInfo& aetherInfo();
const EffectInfo& emberInfo();

std::string paramText (EffectKind kind, int param, float v);
std::unique_ptr<Effect> make (EffectKind kind);
// Hush with its look-ahead (= latency) known before the first block (round 128)
std::unique_ptr<Effect> makeHush (const EffectParams* params);
bool isSuiteKind (EffectKind kind) noexcept;

// Meters the editor shows (written by the audio thread, read by the UI): see Effect::controlOutput.
// Output 0 = gain reduction 0..1 over 0..30 dB (Ridge: compressor), 1 = second gain reduction (Ridge: gate),
// 2 = input peak, 3 = output peak (linear, 0..1 clamped).
} // namespace mixsuite
} // namespace lynx
