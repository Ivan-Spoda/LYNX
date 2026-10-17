#pragma once

// Facet EQ — LYNX's precision parametric equalizer (8 bands, zero latency).
//
// Each band is an analog prototype (bell, shelves, cuts up to 96 dB/oct, notch, band pass, tilt shelf) turned into
// digital sections with a magnitude-matched design (poles by impulse invariance, zeros fitted so the digital
// magnitude equals the analog one at DC, at Nyquist and at the band frequency), so bands near Nyquist keep their
// analog shape instead of being squeezed as bilinear-transform EQs are. The prototype family, the Q conventions and the
// slope law follow our black-box measurements of the reference EQ (docs/analise/MEASURE_fabfilter.md §1).
//
// Parameters (0..1, EffectKind::facetEq): band b uses [5b .. 5b+4] = shape, frequency, gain, Q, slope;
// [40] output gain, [41] gain scale; (round 103, appended so older projects load unchanged — every default is the old
// behaviour) [42 + b] dynamic range of band b (0.5 = 0 dB = a static band), [50 + b] its threshold, [58] attack,
// [59] release, [60] phase mode (zero latency / natural / linear), [61] solo band (0 = none), [62] dynamics detector
// (the EQ's input / the slot's sidechain), [63] analyser display (editor only: off, pre, post, pre + post).
//
// Dynamic EQ (docs/analise/REA_rodada103.md §2, measured on the reference): each band with a gain (bell, shelves,
// tilt) whose range is not 0 listens to its own region of the detector signal (band pass at the band frequency and
// Q; low pass / high pass for the shelves) and moves its gain by up to `range` dB as that level rises over the
// threshold — 1 dB per dB above it (an infinite ratio) with a 10 dB soft knee, clamped at the range, smoothed by the
// attack / release times. Phase modes: zero latency (the IIR sections), natural phase (the analog prototype's
// magnitude and phase as an FIR, 320 samples at 48 kHz) and linear phase (the analog magnitude, zero phase, 4608
// samples at 48 kHz), both run as a uniformly partitioned FFT convolution and reported to PDC.

#include <array>
#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::facet
{
constexpr int bands = 8;
constexpr int fieldsPerBand = 5;
constexpr int outputParam = 40;
constexpr int scaleParam = 41;
constexpr int rangeParam0 = 42;     // + band
constexpr int thresholdParam0 = 50; // + band
constexpr int attackParam = 58;
constexpr int releaseParam = 59;
constexpr int phaseParam = 60;
constexpr int soloParam = 61;
constexpr int detectorParam = 62;
constexpr int analyzerParam = 63;
constexpr int paramCount = 64;
constexpr int maxSections = 8; // 96 dB/oct = 16th order

enum class Shape { off, bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf };
constexpr int shapeCount = 9;
constexpr std::array<int, 9> slopes { 6, 12, 18, 24, 30, 36, 48, 72, 96 }; // dB/oct

inline int param (int band, int field) noexcept { return band * fieldsPerBand + field; } // 0 shape 1 freq 2 gain 3 Q 4 slope

Shape shape (float v) noexcept;
float shapeValue (Shape s) noexcept;
double hz (float v) noexcept;          // 10 Hz .. 30 kHz (log)
float hzValue (double hz) noexcept;
double gainDb (float v) noexcept;      // ±30 dB
float gainValue (double db) noexcept;
double q (float v) noexcept;           // 0.025 .. 40 (log, 0.5 = 1)
float qValue (double q) noexcept;
int slopeDb (float v) noexcept;        // 6 .. 96 dB/oct
float slopeValue (int dbPerOct) noexcept;
double outputDb (float v) noexcept;    // ±24 dB
double gainScale (float v) noexcept;   // 0 .. 200 %
const char* shapeName (Shape s) noexcept;
bool hasGain (Shape s) noexcept;       // bell, shelves, tilt: the shapes a gain (and dynamics) applies to

enum class Phase { zeroLatency, natural, linear };
constexpr int phaseCount = 3;
enum class Analyzer { off, pre, post, both };

double rangeDb (float v) noexcept;     // dynamic range ±30 dB (0.5 = 0 = static)
float rangeValue (double db) noexcept;
double thresholdDb (float v) noexcept; // −80 … 0 dB (sine peak)
float thresholdValue (double db) noexcept;
double attackMs (float v) noexcept;    // 0.25 … 50 ms
double releaseMs (float v) noexcept;   // 20 … 4000 ms
Phase phase (float v) noexcept;
float phaseValue (Phase p) noexcept;
int soloBand (float v) noexcept;       // −1 = none
float soloValue (int band) noexcept;
bool sidechainDetector (float v) noexcept;
Analyzer analyzer (float v) noexcept;
float analyzerValue (Analyzer a) noexcept;
bool isDynamic (const EffectParams& params, int band) noexcept;
const char* phaseName (Phase p) noexcept;

// The dynamic gain (dB) a band applies for a detector level (dB, sine peak): 0 below the knee, then 1 dB per dB over the
// threshold (10 dB soft knee), clamped at the range; the sign follows the range.
double dynamicGainDb (double levelDb, double thresholdDb, double rangeDb) noexcept;
// Latency (samples) of a phase mode at a sample rate
int latencyFor (Phase p, double sampleRate) noexcept;

const EffectInfo& info();
std::string paramText (int param, float v);

// One digital section: b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2 (first-order sections have b2 = a2 = 0).
struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// The sections of one band at a sample rate; returns how many were written (0 = the band is off / flat).
// `extraDb` is added to the band's gain (before the gain scale): the dynamic EQ's movement.
int design (const EffectParams& params, int band, double sampleRate, std::array<Section, maxSections>& out, double extraDb = 0.0) noexcept;
// The analog prototype's response of one band (dB) — the target the digital design matches.
double analogDb (const EffectParams& params, int band, double hz, double extraDb = 0.0) noexcept;
// Response of the whole EQ (dB) at hz: the digital sections in zero-latency mode, the analog prototype in the FIR
// modes. `dynamicDb` (optional, 8 values) are the bands' live dynamic gains.
double responseDb (const EffectParams& params, double sampleRate, double hz, const float* dynamicDb = nullptr) noexcept;
// One band's own response (dB) at hz, same rules
double bandResponseDb (const EffectParams& params, int band, double sampleRate, double hz, double extraDb = 0.0) noexcept;

// The effect; with the slot's parameters its latency is known from the start (PDC plans it before the first block).
std::unique_ptr<Effect> make (const EffectParams* initial = nullptr);
// controlOutput (b), b = 0 … 7: band b's live dynamic gain in dB / 60 (signed: 0 = no movement, also when cleared)
} // namespace lynx::facet
