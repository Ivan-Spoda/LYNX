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

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    hybrid,     // round 124: the hybrid echo engine (off / clean / analog) — a plain two-line echo with a level-dependent
                // gain on every pass, measured on the H-Delay trial (REA_rodada124 §5); off = Echoform as before
    // ---- round 135w (docs/PARITY.md: H-Delay, ValhallaDelay, EchoBoy / PrimalTap rows), appended; every default is
    // the old sound
    tempoSource, // note sync follows the project tempo (0) or the echo's own BPM (1, H-Delay's "BPM" sync)
    ownBpm,      // that tempo, 30 … 300 BPM (linear; default 120)
    startSide,   // ping-pong: the first echo on the left (0, default) or on the right (1) (H-Delay's start direction)
    filterLink,  // the editor moves the low and high cut together (UI only; the engine ignores it)
    tapA, tapB, tapC, tapD, // quad: each tap heard or muted (all on by default)
    swell,       // quad: feedback from the last tap (0, "Repeat") or from every tap heard (1, "Swell")
    groove,      // ±25 % of the echo time: odd repeats late (+) or early (−) (EchoBoy's Groove, measured [EXEC])
    feel,        // ±25 % of the echo time: the whole echo train late (+, "draggin'") or early (−, "rushin'")
    accent,      // ±100 %: odd repeats louder (+) or softer (−) than even ones
    primes,      // the delay times snap to a prime number of samples (EchoBoy's Prime Numbers)
    crossFeedback, // 0 … 100 %: each side's feedback taken from the other side (EchoBoy's Dual "FB Mix")
    balance,     // ±100 %: the echoes' left / right level
    freeze,      // the loop holds what it has (no input, unity feedback, no colouring) (PrimalTap's Freeze)
    // ---- round 137 (docs/PARITY.md: EchoBoy "Echo Mode: Rhythm"), appended; off by default (the old sound)
    rhythm,        // Rhythm echo mode: a pattern of evenly spaced taps that the feedback replays (overrides the style)
    rhythmSync,    // the tap spacing in ms (0) or as a note / dotted / triplet value (the Sync L law)
    rhythmNote,    // that note (the Note L law, default 1/8)
    rhythmTime,    // that time in ms (the Time L law)
    rhythmRepeats, // 1 … 16 slots in the pattern (the first slot is the input itself: repeats − 1 taps are heard)
    rhythmDecay,   // 0 … 40 dB from the first slot to the last
    rhythmLength,  // the pattern length, 1 … 8 beats (measured default: 2 beats); taps past it are dropped
    era,           // round 137: ValhallaDelay's Era: Past (0, the old sound) / Present (0.5) / Future (1): the loop's
                   // bandwidth per mode, measured black-box on the demo (REA_rodada137 §3)
    rhythmTap1,    // round 137: the level of each heard rhythm tap (1 … 15), 0 … 100 % (EchoBoy's rhythm editor bars);
                   // 100 % by default, applied after the pattern's normalisation
    rhythmTapLast = rhythmTap1 + 14,
    lofi,          // round 137: H-Delay's LoFi switch, independent of the mode: a steep low-pass (8th order, 5.8 kHz)
                   // inside the loop (measured on the trial, docs/specs/h-delay.md §4.2); off by default
    tapeVoicing, // round 136: a measured tape-echo loop (REA_rodada136 §4): 0 = off, else one of tapeVoicingCount
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
double bpmOf (float v) noexcept;         // 30 .. 300 BPM (round 135w)
float bpmValue (double bpm) noexcept;
// the tempo the note sync follows: the project's or the echo's own (round 135w)
double tempoFor (const EffectParams& p, double projectBpm) noexcept;
// the nearest prime number of samples to `samples` (≥ 2) (round 135w, EchoBoy's Prime Numbers)
int nearestPrime (int samples) noexcept;
// round 135w: an edit of `param` with Filter link on: the low and high cut move together (the same frequency ratio);
// returns the edits to apply (just the one when the link is off or the parameter is not a cut)
std::vector<std::pair<int, float>> linkedEdit (const EffectParams& p, int param, float value);
// round 137: Rhythm mode (EchoBoy's, measured black-box [EXEC], REA_rodada137 §2)
struct RhythmTap { double ms; double level; };
int rhythmRepeatsOf (float v) noexcept;  // 1 … 16
double rhythmDecayDb (float v) noexcept; // 0 … 40
int rhythmBeatsOf (float v) noexcept;    // 1 … 8
double rhythmSpacingMs (const EffectParams& p, double bpm) noexcept;
double rhythmLengthMs (const EffectParams& p, double bpm) noexcept;
// the heard taps of one pass (slot 0 = the input is not heard; it comes back through the feedback after the length)
// and the level of slot 0 in `slot0`; levels are equal-power normalised over the slots that fit in the length
std::vector<RhythmTap> rhythmTaps (const EffectParams& p, double bpm, double* slot0 = nullptr);
int rhythmPattern (const EffectParams& p, double bpm, std::array<RhythmTap, 16>& taps, double& slot0) noexcept; // no allocation
int eraOf (float v) noexcept;           // 0 past, 1 present, 2 future (round 137)
// the factor on the character's low-pass cut-off for the mode and era (1 for Past)
double eraBandwidth (Mode m, int era) noexcept;
int hybridOf (float v) noexcept;         // 0 off, 1 clean, 2 analog (round 124)
constexpr int tapeVoicingCount = 3; // round 136: ping-pong (edited style), studio tape, studio tape hot
int tapeVoicingOf (float v) noexcept; // 0 = off, 1 … tapeVoicingCount
float tapeVoicingValue (int k) noexcept;
const char* tapeVoicingName (int k) noexcept;
float hybridValue (int h) noexcept;
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
    driveOut, noise, rotate, smoothing, colour, sync, taps, diffOn, driveOn, drivePost, halfSpeed,
    inputPan, // round 137 (appended): what enters the taps, 9 steps (index round (8 v)), measured on the reference
              // [EXEC]: 0 / 1 stereo (default 1), 2 swapped, 3 mono to both, 4 mono to the left, 5 mono to the right,
              // 6–8 stereo (no difference measured with an impulse)
    paramCount
};
int inputPanOf (float v) noexcept;  // 0 … 8
int tapCount (float v) noexcept;     // 1, 1, 2, 3, 3, 3, 4, 8, 8, 16 (index floor(24 v))
double delayMs (float v) noexcept;   // 0 .. 1000 ms
double tapMs (const EffectParams& p, int tap, int taps, bool right) noexcept; // tap position (without modulation)
double tapLevel (const EffectParams& p, int tap, int taps) noexcept;        // signed (the wet is inverted)
} // namespace lattice

// Wet / dry laws: equal power (Echoform, Orbit, Lattice) or linear (Spiral Echo).
void equalPowerMix (float m, float& dry, float& wet) noexcept;
} // namespace echosuite
} // namespace lynx
