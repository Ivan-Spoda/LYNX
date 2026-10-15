#pragma once

// FL Studio's small stereo / dynamics mixer effects as exact LYNX engines, rebuilt in docs/analise/REA_rodada54.md
// (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or data in this repository).
// Every effect's parameters are FL's controls over their ranges, value = (raw − min) / (max − min), so the LYNX UI
// can edit them and FL automation maps one to one; the saved state's own values are copied in at import.
//
// Fruity Stereo Shaper (EffectKind::stereoShaper):
//   [0] R→L, [1] L, [2] R, [3] L→R  (−25600…25600; 12800 = the knob's unit position)
//   [4] delay (−4096…4096; > 0 delays the right channel, < 0 the left)
//   [5] phase / dephasing (−4096…4096; > 0 all-passes the right channel, < 0 the left)
//   [6] effects order (0 = delay/phase before the matrix, 1 = after)
//   [7] LYNX only: output the "In/Out difference" (input − output) instead of the output; FL sends that signal to
//       its parameter-6 destination, which LYNX uses for Patcher branches.
//
// Fruity Stereo Enhancer (EffectKind::stereoEnhancer):
//   [0] pan (−128…128), [1] volume (0…320; 256 = 0 dB), [2] stereo separation (−96…96), [3] phase offset / delay
//   (−512…512; > 0 delays the right channel), [4] effects order (0 = delay before the matrix, 1 = after),
//   [5] phase inversion (0 none, 1 left, 2 right).
//
// Transient Processor (EffectKind::transientProcessor):
//   [0] attack, [1] drive, [2] release, [3] gain (0…160 each; 80 = centre), [4] attack shape, [5] release shape
//   (0…2: sharp / mid / slow), [6] split frequency, [7] split balance (0…160), [8] on (0…1).
//
// Spreader (EffectKind::spreader):
//   [0] width (0…10000), [1] separation (−10000…10000), [2] low bypass (1000…1000000), [3] mono bass (0…1),
//   [4] enable (0…1).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::stereofx
{
// ------------------------------------------------------------------------------------------- Fruity Stereo Shaper
constexpr int stereoShaperParams = 8;
struct StereoShaperState
{
    int rightToLeft = 0, left = 12800, right = 12800, leftToRight = 0; // FL's raw controls (new instance)
    int delay = 0, phase = 0, send = 0, post = 0;
};
// record 0xD5: i32 version (0; ignored by FL) + 8 × i32 (the eight parameters in FL's order)
std::optional<StereoShaperState> parseStereoShaper (const std::vector<std::uint8_t>& state);
void stereoShaperToParams (const StereoShaperState& s, float* params) noexcept;
StereoShaperState stereoShaperFromParams (const float* params) noexcept;
std::string stereoShaperParamText (int param, float value01);
std::unique_ptr<Effect> makeStereoShaper();

// ----------------------------------------------------------------------------------------- Fruity Stereo Enhancer
constexpr int stereoEnhancerParams = 6;
struct StereoEnhancerState
{
    int pan = 0, volume = 256, separation = 0, delay = 0, post = 1, invert = 0; // FL's raw controls (new instance)
};
// record 0xD5: 6 × i32 (the six parameters in FL's order; no version word)
std::optional<StereoEnhancerState> parseStereoEnhancer (const std::vector<std::uint8_t>& state);
void stereoEnhancerToParams (const StereoEnhancerState& s, float* params) noexcept;
StereoEnhancerState stereoEnhancerFromParams (const float* params) noexcept;
std::string stereoEnhancerParamText (int param, float value01);
std::unique_ptr<Effect> makeStereoEnhancer();

// -------------------------------------------------------------------------------------------- Transient Processor
constexpr int transientProcessorParams = 9;
struct TransientProcessorState
{
    int attack = 80, drive = 0, release = 80, gain = 80, attackShape = 1, releaseShape = 1, split = 80, balance = 80, on = 1;
};
// record 0xD5: i32 version (1) + 9 × i32 + editor options (ignored: they only drive FL's meters)
std::optional<TransientProcessorState> parseTransientProcessor (const std::vector<std::uint8_t>& state);
void transientProcessorToParams (const TransientProcessorState& s, float* params) noexcept;
TransientProcessorState transientProcessorFromParams (const float* params) noexcept;
std::string transientProcessorParamText (int param, float value01);
std::unique_ptr<Effect> makeTransientProcessor();

// ------------------------------------------------------------------------------------------------------- Spreader
constexpr int spreaderParams = 5;
struct SpreaderState
{
    int width = 5000, separation = 0, lowBypass = 10990, monoBass = 0, enable = 1; // FL's raw controls (new instance)
};
// record 0xD5: u32 version (0x10002 / 0x10003) + 5 × i32 + editor options (ignored)
std::optional<SpreaderState> parseSpreader (const std::vector<std::uint8_t>& state);
void spreaderToParams (const SpreaderState& s, float* params) noexcept;
SpreaderState spreaderFromParams (const float* params) noexcept;
std::string spreaderParamText (int param, float value01);
std::unique_ptr<Effect> makeSpreader();
} // namespace lynx::stereofx
