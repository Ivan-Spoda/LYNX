#pragma once

// LYNX's voice tools (REA_rodada119): two native processors for vocal chains, written from published DSP literature
// and designed against our own black-box measurements of the plugins vocal projects use in these roles (official
// trials, docs/analise/REA_rodada119.md). Own names, own code, own parameter laws; the laws follow the measured ones,
// so a saved setting of those plugins translates one to one when a project uses them on a machine without them.
//
//   Quell Denoiser   one-knob broadband noise suppressor: a phase-coherent 6-band Linkwitz-Riley split, a downward
//                    expander per band whose threshold, depth and slope follow the Amount knob, fast opening and a
//                    slower close, linked stereo; Listen plays what is taken away
//   Gemini Doubler   two-voice doubler: each voice is a pitch-shifting delay (two crossfaded grains reading a delay
//                    line) with gain, pan, delay, feedback, detune, a triangle LFO on the pitch, an octave-down switch
//                    and its source (left, sum, difference); the direct signal can be rotated in the stereo field and
//                    aligned with the voices; low / high shelves on the voices, a low cut in the feedback paths
//
// Parameters are 0..1 in the model (EffectParams). Delay lines are allocated in prepare() (off the audio thread),
// process() never allocates. Neither effect reports latency: Gemini's "Align" delays the direct signal inside the
// effect, as the measured reference does (it reports none either).
//
// Meters (Effect::controlOutput): Quell: 0 = mean reduction 0..1 over 0..30 dB, 1..6 = each band's reduction / 30.

#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace voicetools
{
bool isVoiceKind (EffectKind kind) noexcept;
std::unique_ptr<Effect> make (EffectKind kind);
std::string paramText (EffectKind kind, int param, float v);
const EffectInfo& quellInfo();
const EffectInfo& geminiInfo();

namespace quell
{
enum Param { amount, listen, output, paramCount };
constexpr int bands = 6;
// crossover frequencies (Hz) between the six bands
constexpr double split[bands - 1] { 105.0, 311.0, 921.0, 2727.0, 8075.0 };
double outputDb (float v) noexcept; // −12 .. +12 dB

// The expander's settings per band at an Amount (0..1). Fitted to the measurements (REA_rodada119 §3).
struct Tuning
{
    double thr0[bands] {};        // threshold (dB of band power, sine-calibrated) at Amount 0
    double thr1[bands] {};        // ... at Amount 1 (linear in between)
    double range0[bands] {};      // depth (dB) at Amount 0
    double range1[bands] {};      // ... at Amount 1
    double slope = 1.0;           // dB of reduction per dB below the threshold
    double knee = 6.0;            // soft knee width (dB)
    double floorCut0[bands] {};   // constant reduction (dB) at Amount 0 (the top band's steady trim)
    double floorCut1[bands] {};
    double detAttackMs = 2.0, detReleaseMs = 30.0; // power detector
    double openMs = 3.0, closeMs = 60.0;           // gain smoothing (opening / closing)
};
const Tuning& defaultTuning() noexcept;
// steady-state reduction (dB, ≥ 0) of band b for a band power `levelDb` at `amount`
double reductionDb (const Tuning& t, int band, double levelDb, double amount) noexcept;
// an engine with explicit tuning (tests and the fitting harness)
std::unique_ptr<Effect> make (const Tuning& t);
} // namespace quell

namespace gemini
{
constexpr int voices = 2;
enum Param
{
    output, directOn, directGain, directPan, align, lowOn, lowGain, lowFreq, highOn, highGain, highFreq, feedbackCut, range,
    voice1, // first parameter of voice 1; each voice has `voiceParams` parameters (VoiceParam order)
    paramCount = voice1 + 2 * 10
};
enum VoiceParam { vOn, vGain, vPan, vDelay, vFeedback, vDetune, vDepth, vRate, vOctave, vSource, voiceParams };
inline int voiceParam (int voice, VoiceParam p) noexcept { return voice1 + voice * voiceParams + (int) p; }

double outputDb (float v) noexcept;      // −24 .. 0 dB
double gainDb (float v) noexcept;        // voice / direct gain: −48 .. 0 dB
double panDegrees (float v) noexcept;    // −45 .. +45 (voices: equal-power pan; direct: stereo rotation)
double delayMs (float v) noexcept;       // 0 .. 100 ms
double feedback (float v) noexcept;      // 0 .. 1
double detuneCents (float v) noexcept;   // −100 .. +100
double depthCents (float v) noexcept;    // −200 .. +200 (triangle LFO on the pitch)
double rateValue (float v) noexcept;     // 0.1 .. 400 (log); the LFO runs at rate / 2 Hz
double shelfDb (float v) noexcept;       // −30 .. +12 dB
double freqHz (float v) noexcept;        // 16 .. 21357 Hz (log)
float freqValue (double hz) noexcept;
int sourceOf (float v) noexcept;         // 0 own side, 1 sum, 2 difference
double windowMs (float rangeV) noexcept; // grain window: 13.3 ms (80 Hz range), 53.3 ms (20 Hz range)
} // namespace gemini
} // namespace voicetools
} // namespace lynx
