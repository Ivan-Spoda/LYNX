#pragma once

// FL Studio's small filters as LYNX mixer effects, exact engines rebuilt in docs/analise/REA_rodada63.md (clean-room:
// layouts, formulas and behaviour in our own words; no Image-Line code or data in this repository).
//
// Every parameter is FL's control over its range (value = (raw − min) / (max − min)), so the LYNX editor and FL
// automation drive the same values FL's ProcessParam receives:
//
//   Fruity Fast LP (EffectKind::fruityFastLP):  [0] cutoff 0…1024 (1024), [1] resonance 0…1024 (0).
//   Fruity Free Filter (EffectKind::fruityFreeFilter):  [0] type 100…700 (low pass, band pass, high pass, notch,
//     low shelf, peaking, high shelf), [1] frequency 0…1024 (10 Hz…16 kHz), [2] Q 0…1024, [3] gain 0…1024 (±18 dB),
//     [4] CENTER (DC removal) 0/1. The filter is always designed for 44.1 kHz, whatever the rate (as FL's is).
//   Fruity Filter (EffectKind::fruityFilter):  [0] cutoff, [1] resonance, [2] low pass, [3] band pass, [4] high pass
//     (each 0…1024; defaults 1024, 205, 1024, 0, 512), [5] ×2 0/1, [6] CENTER 0/1. A state-variable filter whose
//     coefficient does not depend on the rate either (fully open = fs/6).
//
// Engine start: a fresh plugin (FL builds it with its default controls, then loads the state), so the Fast LP's
// smoothed cutoff starts at the default's coefficient and glides to the state's over the first call. Host blocks
// longer than 512 frames are processed as 512-frame engine calls (the coefficient ramps are per call).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::flfilter
{
constexpr int fastLPParams = 2, freeFilterParams = 5, filterParams = 7;

// FL's raw controls; parse = the plugin's saved state (record 0xD5)
struct FastLPState { int cutoff = 1024, reso = 0; };
struct FreeFilterState { int type = 100, freq = 512, q = 169, gain = 676; bool center = false; };
struct FilterState { std::array<int, 6> raw { 1024, 205, 1024, 0, 512, 0 }; bool center = false; };

std::optional<FastLPState> parseFastLP (const std::vector<std::uint8_t>& state); // i32 version (< 2) + 2 × i32
void fastLPToParams (const FastLPState& s, float* params) noexcept;
FastLPState fastLPFromParams (const float* params) noexcept;
std::string fastLPParamText (int param, float value01);
std::optional<FreeFilterState> parseFreeFilter (const std::vector<std::uint8_t>& state); // version ≤ 2, 4 × i32, u8
void freeFilterToParams (const FreeFilterState& s, float* params) noexcept;
FreeFilterState freeFilterFromParams (const float* params) noexcept;
std::string freeFilterParamText (int param, float value01);
std::optional<FilterState> parseFilter (const std::vector<std::uint8_t>& state); // version ≤ 2, 6 × i32, u8
void filterToParams (const FilterState& s, float* params) noexcept;
FilterState filterFromParams (const float* params) noexcept;
std::string filterParamText (int param, float value01);

// ------------------------------------------------------------------------------------------------------- engines
class FastLPEngine
{
public:
    void set (const FastLPState& s) noexcept;               // FL's ProcessParam (both coefficients are recomputed)
    void process (float* left, float* right, int n) noexcept; // one FL render call

private:
    FastLPState raw {};
    float c = 1.0f, q = 0.0f; // target coefficient and feedback
    float cur = 1.0f;         // the smoothed coefficient (starts at the default's)
    double lp[2] {}, v[2] {}; // filter states, kept as doubles between calls
};

class FreeFilterEngine
{
public:
    FreeFilterEngine() noexcept;                            // a fresh plugin (default controls)
    void prepare (int sampleRate) noexcept;                 // FL's sample-rate notification (CENTER only; clears it)
    void set (const FreeFilterState& s) noexcept;           // FL's ProcessParam for every control that changed
    void process (float* left, float* right, int n) noexcept; // one FL render call

private:
    void param (int k, int raw) noexcept;
    void design() noexcept;
    FreeFilterState raw {};
    int type = 1;
    float freq = 0, q = 0, gain = 0;
    float co[5] {};  // b0 b1 b2 a1 a2
    float st[8] {};  // x1 L/R, x2 L/R, y1 L/R, y2 L/R
    float dcC = 0, dcQ = 0.96f;
    double dc[4] {}; // CENTER: lp L/R, v L/R
};

class FilterEngine
{
public:
    FilterEngine() noexcept;                                // a fresh plugin (default controls, smoothers settled)
    void prepare (int sampleRate) noexcept;                 // FL's sample-rate notification (CENTER only; clears it)
    void set (const FilterState& s) noexcept;               // FL's ProcessParam for every control that changed
    void process (float* left, float* right, int n) noexcept; // one FL render call

private:
    void param (int k, int raw) noexcept;
    FilterState raw {};
    bool twice = false;
    std::array<float, 5> target {}, cur {}; // f, damping, LP, BP, HP gains
    float st[12] {};                        // low 1/2, band 1/2, high 1/2 (L, R each)
    float dcC = 0, dcQ = 0.96f;
    double dc[4] {};
};

std::unique_ptr<Effect> makeFastLP();
std::unique_ptr<Effect> makeFreeFilter();
std::unique_ptr<Effect> makeFilter();
} // namespace lynx::flfilter
