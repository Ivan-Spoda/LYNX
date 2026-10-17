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
// [40] output gain, [41] gain scale.

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
constexpr int paramCount = 42;
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

const EffectInfo& info();
std::string paramText (int param, float v);

// One digital section: b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2 (first-order sections have b2 = a2 = 0).
struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// The sections of one band at a sample rate; returns how many were written (0 = the band is off / flat).
int design (const EffectParams& params, int band, double sampleRate, std::array<Section, maxSections>& out) noexcept;
// The analog prototype's response of one band (dB) — the target the digital design matches.
double analogDb (const EffectParams& params, int band, double hz) noexcept;
// Response of the whole EQ (digital, dB) at hz.
double responseDb (const EffectParams& params, double sampleRate, double hz) noexcept;

std::unique_ptr<Effect> make();
} // namespace lynx::facet
