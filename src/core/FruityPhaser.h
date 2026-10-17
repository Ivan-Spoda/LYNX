#pragma once

// FL Studio's Fruity Phaser as a LYNX mixer effect (EffectKind::fruityPhaser), exact engine rebuilt in
// docs/analise/REA_rodada105_phaser.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line
// code or data in this repository). FL's saved state is only its nine controls, which become the slot parameters
// (value = raw / max) so the LYNX editor and automation can change them.
//
// Controls (all from 0; max, a new instance's default):
//   [0] sweep freq. (5000, 2500)  [1] min depth (1000, 100)  [2] max depth (1000, 800)  [3] freq. range (1024, 0)
//   [4] stereo (1024, 512)  [5] number of stages − 1 (22, 7)  [6] feedback (1000, 400)  [7] dry-wet (1024, 512)
//   [8] out gain (5000, 4000)

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::phaserfx
{
constexpr int params = 9;
constexpr int maxStages = 23;
using State = std::array<int, params>; // raw controls
constexpr State defaults { 2500, 100, 800, 0, 512, 7, 400, 512, 4000 };
constexpr State rawMax { 5000, 1000, 1000, 1024, 1024, 22, 1000, 1024, 5000 };

// FL's saved state: i32 version (1; FL reads only versions below 2), then nine i32 in control order
std::optional<State> parse (const std::vector<std::uint8_t>& state);
std::vector<std::uint8_t> serialize (const State& s);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

// LFO rate in Hz of a sweep control (0…1) in the small (false) or large (true) frequency range
float sweepHz (float sweep01, bool large) noexcept;

// The plugin object: FL's control changes, sample rate, reset and the render (interleaved stereo).
class Engine
{
public:
    Engine();
    ~Engine();
    void setRate (int sampleRate) noexcept;
    void setParam (int index, int raw) noexcept;
    void load (const State& s) noexcept; // every control in order, like FL's state load
    void reset() noexcept;               // all-pass states and LFO phases cleared
    int param (int index) const noexcept;
    // one FL render call (the right LFO is re-aligned to the left one at its start); in may equal out
    void render (const float* in, float* out, int n) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

std::unique_ptr<Effect> make();
} // namespace lynx::phaserfx
