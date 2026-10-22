#pragma once

// LYNX's delay / modulation suite (REA_rodada101): four native effects written from published DSP literature
// (fractional delay lines, Hilbert-transform frequency shifting, delay-line pitch shifting, barber-pole and
// through-zero flanging, tape / bucket-brigade echo descriptions from AES / DAFx papers and textbooks) and from our
// black-box measurements of the delay / modulation plugins that projects use most (docs/analise/REA_rodada101.md).
// None of them is a copy of a product; their parameter laws follow the measurements so saved settings of those
// plugins translate one to one when a project uses them on a machine that does not have them:
//
//   Echoform        multi-mode echo: tape, hi-fi tape, bucket brigade, analog, digital, clean, frequency shifter,
//                   pitch, reverse, lo-fi, phaser and detune characters; single / dual / ratio / ping-pong / quad
//                   styles; tempo sync; wow / flutter / LFO; diffusion; ducking
//   Spiral Echo     frequency-shifting echo: every repeat moves by the shift (barber-pole spirals), stereo spread
//   Orbit           modulation machine: barber-pole flangers, through-zero flangers, tri / octo choruses, doubler,
//                   ensemble, symphonic
//   Lattice Mod     multitap modulated delay: 1–16 taps, spread / slope / skew / random, detune + vibrato,
//                   rotating stereo feedback, diffusion, drive, spatial crossover
//
// All parameters are 0..1 in the model (EffectParams). Every effect reports zero latency; the delay lines are
// allocated in prepare() (off the audio thread), process() never allocates.

#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace echosuite
{
bool isSuiteKind (EffectKind kind) noexcept;
std::unique_ptr<Effect> make (EffectKind kind);
std::string paramText (EffectKind kind, int param, float v);

const EffectInfo& echoformInfo();
const EffectInfo& spiralInfo();
const EffectInfo& orbitInfo();
const EffectInfo& latticeInfo();

// ---- parameter laws (exposed for the tests, the editor and the substitutes table) ----
namespace echoform
{
enum Param
{
    mix, style, syncL, noteL, timeL, syncR, noteR, timeR, spread, spacing, ratio, feedback, width, drive, age,
    diffusion, diffSize, lowCut, highCut, modRate, modDepth, wow, flutter, shift, shiftDetune, pitch, pitchDetune,
    mode, ducking,
    output,     // round 119: the echoes' level, −18 … +18 dB (0.5 = 0 dB; older projects load the default)
    pairs,      // round 119: ping-pong in pairs — the right side repeats the left echo at full level and the loop
                // gain applies once per left-right pair (the H-Delay-style ping-pong); off = each side at √gain
    phaseL,     // round 129: the echoes of the left / right side inverted (the H-Delay's phase L / R buttons)
    phaseR,
    paramCount
};
enum class Style { single, ratio, quad, dual, pingPong }; // the two-time styles last (the panel shows the right time for both)
// in the panel's order: the modulation section shows wow / flutter (tape), rate / depth (lo-fi … phaser), shift / detune
// (shifter) or pitch / detune (pitch, reverse, detune) — contiguous ranges of the Mode value
enum class Mode { tape, lofi, hifi, bucket, analog, digital, clean, phaser, shifter, pitch, reverse, detune };
constexpr int modeCount = 12;
constexpr int styleCount = 5;

double timeMs (float v) noexcept;        // 0 .. 20000 ms, piecewise linear (0.5 = 500 ms, 0.76 = 1 s, 0.94 = 6 s)
float timeValue (double ms) noexcept;
int syncKind (float v) noexcept;         // 0 ms, 1 note, 2 dotted, 3 triplet
int noteIndex (float v) noexcept;        // 0 .. 9 = 1/64 .. 8/1
double noteBeats (int index, int kind) noexcept; // quarter notes
// the delay of one side (ms) from its sync / note / time parameters
double sideMs (const EffectParams& p, bool right, double bpm) noexcept;
double feedbackGain (float v) noexcept;  // loop gain 0 .. 2 (0.5 = unity)
double modRateHz (float v) noexcept;     // 0.01 .. 10 Hz (centre 2 Hz)
double shiftHz (float v) noexcept;       // ±1000 Hz, |x|^5
double shiftDetuneHz (float v) noexcept; // ±10 Hz, |x|^2.5
double pitchSemis (float v) noexcept;    // ±24
double lowCutHz (float v) noexcept;      // 10 .. 2000
double highCutHz (float v) noexcept;     // 200 .. 20000
double outputDb (float v) noexcept;      // −18 .. +18 dB (round 119)
Mode modeOf (float v) noexcept;
float modeValue (Mode m) noexcept;
Style styleOf (float v) noexcept;
float styleValue (Style s) noexcept;
const char* modeName (Mode m) noexcept;
} // namespace echoform

namespace spiral
{
enum Param { mix, shift, delay, sync, feedback, lowCut, highCut, stereo, paramCount };
double shiftHz (float v) noexcept;   // ±1000 Hz, |x|^5
double delayMs (float v) noexcept;   // 0.01 .. 1000 ms (centre 100 ms)
float delayValue (double ms) noexcept;
int syncIndex (float v) noexcept;    // floor (23 v): 0 off, 1..8 straight 1/64..2/1, 9..15 dotted 1/64..1/1, 16..23 triplet 1/64..2/1
double syncBeats (int index) noexcept; // quarter notes (0 for "off")
double loopGain (float v) noexcept;  // 1.5 × feedback
double lowCutHz (float v) noexcept;  // 20 .. 3000
double highCutHz (float v) noexcept; // 50 .. 15000
} // namespace spiral

namespace orbit
{
enum Param { mix, rate, depth, feedback, manual, mode, paramCount };
enum class Mode { rise, riseFall, throughZeroPlus, throughZeroMinus, tri, octo, doubler, driftRise, driftRiseFall, ensemble, symphonic };
constexpr int modeCount = 11;
double rateHz (float v) noexcept;    // ±10 Hz, |x|^3.333
double depthMs (float v) noexcept;   // 0 .. 50 ms (centre 10 ms)
double manualMs (float v) noexcept;  // ±50 ms, |x|^4
double feedbackGain (float v) noexcept; // ±0.7·|x|^1.5 (sign kept)
Mode modeOf (float v) noexcept;
float modeValue (Mode m) noexcept;
const char* modeName (Mode m) noexcept;
} // namespace orbit

namespace lattice
{
enum Param
{
    mix, depth, width, delay, feedback, spread, slope, skew, random, tapGain, diffusion, diffSize, diffModRate,
    diffModDepth, lowCut, highCut, xover, detuneRate, detuneDepth, vibratoRate, vibratoDepth, overMod, driveIn,
    driveOut, noise, rotate, smoothing, colour, sync, taps, diffOn, driveOn, drivePost, halfSpeed, paramCount
};
int tapCount (float v) noexcept;     // 1, 1, 2, 3, 3, 3, 4, 8, 8, 16 (index floor(24 v))
double delayMs (float v) noexcept;   // 0 .. 1000 ms
double tapMs (const EffectParams& p, int tap, int taps, bool right) noexcept; // tap position (without modulation)
double tapLevel (const EffectParams& p, int tap, int taps) noexcept;        // signed (the wet is inverted)
} // namespace lattice

// Wet / dry laws: equal power (Echoform, Orbit, Lattice) or linear (Spiral Echo).
void equalPowerMix (float m, float& dry, float& wet) noexcept;
} // namespace echosuite
} // namespace lynx
