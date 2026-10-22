#pragma once

// LYNX's dynamics suite (REA_rodada108): three native processors designed against our own black-box measurements of
// the reference mixing plugins (docs/analise/MEASURE_fabfilter.md §2, §6, §7 and §12) and published DSP literature.
// Own names, own code, own parameter laws:
//
//   Crest Compressor   11 styles (detector / curve / timing families), knee, range, hold, look-ahead, auto release,
//                      a 3-band side-chain EQ (Facet EQ prototypes), side-chain level / external input / listen,
//                      stereo link, character stage (warm / crisp / bright, pre or post), 2x-8x oversampling,
//                      auto gain, mix
//   Prism Multiband    4-band dynamics (compress / expand, downward or upward by the sign of the range), crossovers
//                      12 / 24 / 48 dB/oct, transparent at rest (a band without gain change leaves the signal
//                      untouched), per-band level, solo and bypass, look-ahead
//   Sentry Gate        gate / expander with 5 styles (hysteresis and timing families, upward expansion), knee,
//                      range, hold, look-ahead, side-chain high / low pass, external side chain, listen, dry mix
//
// Parameters are 0..1 in the model (EffectParams); the laws to real units are below. Nothing allocates after
// construction. Latency depends only on the "maximum look-ahead" / look-ahead switches and the oversampling choice and
// is reported through latencySamples() (known after prepare(), and for slots built by make (kind, &params) right
// away, so PDC is planned before the first block).
//
// Meters (Effect::controlOutput): Crest / Sentry: 0 = gain reduction 0..1 over 0..30 dB, 2 = input peak, 3 = output
// peak; Prism: 0..3 = the bands' gain change in dB / 30 (negative = reduction, positive = boost), 4 = input peak,
// 5 = output peak.

#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace dyn
{
// ---- Crest Compressor ----
namespace crest
{
enum Param
{
    style, threshold, ratio, knee, range, attack, release, autoRelease, hold, lookahead, maxLookahead,
    character, characterPre, drive, scLevel, scExternal, stereoLink, scListen,
    sc1Shape, sc1Freq, sc1Gain, sc1Q, sc1Slope,
    sc2Shape, sc2Freq, sc2Gain, sc2Q, sc2Slope,
    sc3Shape, sc3Freq, sc3Gain, sc3Q, sc3Slope,
    oversampling, autoGain, mix, input, output,
    // round 128 (Pro-C 3 parity): dry level (parallel compression), auto threshold, stereo-link mode, display settings
    dryGain, autoThreshold, linkMode, meterScale, inputMeter,
    count
};
constexpr int scBands = 3;
// stereo-link modes: L/R (the classic linked or split stereo detection), mid only, side only, mid triggers the side,
// side triggers the mid (the last four process in mid / side)
enum class LinkMode { stereo, mid, side, midToSide, sideToMid };
constexpr int linkModes = 5;
LinkMode linkModeOf (float v) noexcept;
float linkModeValue (LinkMode m) noexcept;
double dryDb (float v) noexcept;         // −∞ (v = 0) … +36 dB; 0 dB at 0.5 (72 dB/unit above 0.25, square law below)
float dryValue (double db) noexcept;
double meterRangeDb (float v) noexcept;  // the display's range: 9, 18, 36, 72 or 90 dB
// Auto threshold (round 128): the threshold follows the programme — the effective threshold is the detector input's
// slow RMS level (3 s window) + (the threshold knob + 18 dB), so the knob's −18 dB puts it right at the average level.
double autoThresholdDb (double knobDb, double programRmsDb) noexcept;
inline int scParam (int band, int field) noexcept { return sc1Shape + band * 5 + field; } // field: 0 shape .. 4 slope

enum class Style { clean, allround, punch, upward, twoWay, variMu, classic, opto, vocal, bus, pumping };
constexpr int styles = 11;
enum class Character { off, warm, crisp, bright };

Style styleOf (float v) noexcept;
float styleValue (Style s) noexcept;
double thresholdDb (float v) noexcept;   // −60 .. 0 dBFS
double ratioOf (float v) noexcept;       // 1 .. 100 : 1
float ratioValue (double r) noexcept;
double kneeDb (float v) noexcept;        // 0 .. 72 dB
double rangeDb (float v) noexcept;       // 0 .. 60 dB
double attackMs (float v) noexcept;      // 0.005 .. 250 ms (log)
float attackValue (double ms) noexcept;
double releaseMs (float v) noexcept;     // 10 .. 2500 ms (log)
float releaseValue (double ms) noexcept;
double holdMs (float v) noexcept;        // 0 .. 500 ms (square law)
float holdValue (double ms) noexcept;
double lookaheadMs (float v) noexcept;   // 0 .. 20 ms
double maxLookaheadMs (float v) noexcept; // off, 1, 2, 5, 10, 20 ms (the latency)
Character characterOf (float v) noexcept;
double driveDb (float v) noexcept;       // −24 .. +24 dB
double scLevelDb (float v) noexcept;     // −36 .. +36 dB
int oversamplingOf (float v) noexcept;   // 1 (off), 2, 4, 8
double trimDb (float v) noexcept;        // input / output: −36 .. +36 dB

// Static curve: gain (dB, ≤ 0 for downward styles) the compressor settles to for a steady sine of `inputDb` peak
// level (side chain flat, no auto gain). Used by the editor's curve and the tests.
double staticGainDb (const EffectParams& p, double inputDb);
// The character stage's transfer curve at a drive (no compensation), for the editor.
float characterCurve (Character c, float x) noexcept;
} // namespace crest

// ---- Prism Multiband ----
namespace prism
{
constexpr int bands = 4;
constexpr int fieldsPerBand = 10;
enum Field { fThreshold, fRatio, fRange, fMode, fKnee, fAttack, fRelease, fLevel, fEnabled, fSolo };
enum Global { xover1 = bands * fieldsPerBand, xover2, xover3, slope, lookahead, mix, input, output,
              // round 128 (Pro-MB parity): each band's own look-ahead (≤ the global one, which sets the latency)
              bandLookahead0, count = bandLookahead0 + bands };
inline int param (int band, int field) noexcept { return band * fieldsPerBand + field; }

double thresholdDb (float v) noexcept;   // −90 .. 0 dBFS (square-root law, finer near 0 dB)
float thresholdValue (double db) noexcept;
double ratioOf (float v) noexcept;       // 1 .. 100 : 1 (same law as Crest)
double rangeDb (float v) noexcept;       // −30 .. +30 dB (negative = downward, positive = upward)
double kneeDb (float v) noexcept;        // 0 .. 48 dB
double attackMs (float v) noexcept;      // detector τ 0.11 .. 89 ms (shown as 0 .. 100 %)
double releaseMs (float v) noexcept;     // detector τ 130 .. 3630 ms (shown as 0 .. 100 %)
double levelDb (float v) noexcept;       // −30 .. +30 dB
double crossoverHz (float v) noexcept;   // 30 Hz .. 18 kHz (log)
float crossoverValue (double hz) noexcept;
int slopeDbOct (float v) noexcept;       // 12, 24 or 48
double lookaheadMs (float v) noexcept;   // 0 .. 20 ms (0 = off: no latency)
double trimDb (float v) noexcept;        // ±36 dB

// Static gain change of band `b` (dB) for a steady sine of `inputDb` inside the band.
double bandStaticDb (const EffectParams& p, int b, double inputDb);
// The magnitude of the whole effect (dB) at `hz` with every band at the gain `bandGainDb[b]` — the editor's curve.
double responseDb (const EffectParams& p, const double* bandGainDb, double sampleRate, double hz);
} // namespace prism

// ---- Sentry Gate ----
namespace sentry
{
enum Param
{
    style, threshold, ratio, range, attack, release, hold, knee, lookahead, lookaheadOn,
    highPass, lowPass, scExternal, scListen, upThreshold, upRatio, dry, output,
    // round 128 (Pro-G parity): channel mode, the expert side-chain gain / mix per detector channel, wet / dry pan,
    // ducking, input level
    channelMode, scGainL, scMixL, scGainR, scMixR, wetPan, dryPan, ducking, input,
    count
};
double scGainDb (float v) noexcept;      // −36 … +36 dB
double mixOf (float v) noexcept;         // −1 (left / mid) … +1 (right / side); 0 = the channel's own input
double panOf (float v) noexcept;         // −1 … +1 balance
enum class Style { classic, clean, vocal, guitar, upward };
constexpr int styles = 5;
Style styleOf (float v) noexcept;
float styleValue (Style s) noexcept;
double thresholdDb (float v) noexcept;   // −60 .. 0
double ratioOf (float v) noexcept;       // 1 .. 100 (same law as Crest)
double rangeDb (float v) noexcept;       // 0 .. 100 dB
double attackMs (float v) noexcept;      // 0 .. 1000 ms (v⁴)
double releaseMs (float v) noexcept;     // 0 .. 5000 ms (v³)
double holdMs (float v) noexcept;        // 0 .. 250 ms (v²)
double kneeDb (float v) noexcept;        // 0 .. 30 dB
double lookaheadMs (float v) noexcept;   // 0 .. 10 ms
double filterHz (float v) noexcept;      // 5 Hz .. 30 kHz (log)
float filterValue (double hz) noexcept;
double upThresholdDb (float v) noexcept; // −30 .. 0
double upRatioOf (float v) noexcept;     // 1 .. 3
double hysteresisDb (Style s) noexcept;  // how far below the threshold an open gate closes

// Static gain (dB) for a steady sine of `inputDb` (the gate open state taken as reached from above when `fromAbove`).
double staticGainDb (const EffectParams& p, double inputDb, bool fromAbove = false);
} // namespace sentry

const EffectInfo& crestInfo();
const EffectInfo& prismInfo();
const EffectInfo& sentryInfo();

bool isDynKind (EffectKind kind) noexcept;
std::string paramText (EffectKind kind, int param, float v);
// The effect; with `initial` the latency already matches those parameters before the first block.
std::unique_ptr<Effect> make (EffectKind kind, const EffectParams* initial = nullptr);
} // namespace dyn
} // namespace lynx
