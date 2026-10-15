#pragma once

// FL Studio's Fruity Flangus, Fruity Fast Dist and Fruity Flanger as LYNX mixer effects, exact engines rebuilt in
// docs/analise/REA_rodada55.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or
// data in this repository). Both are stateless apart from what the parameters hold: FL's saved state is only the
// controls, which become the slot parameters (value = (raw − min) / (max − min)) so the LYNX editor and automation
// can change them.
//
// Fast Dist (EffectKind::fruityFastDist): [0] pre amp 64…192, [1] threshold 1…10, [2] type A/B (0…1),
//   [3] mix 0…128, [4] post 0…128. The curves are FL's host waveshaper tables (FLEngine), rebuilt from their
//   formula at run time. While LYNX exports (offlineRendering()) the interpolating kernel FL uses when it renders
//   a file is taken; live playback uses FL's live (non-interpolating) kernel.
// Flangus (EffectKind::fruityFlangus): [0] order 1…9, [1] depth 0…1024, [2] speed 0…1024, [3] delay 0…128,
//   [4] spread 0…128, [5] cross −128…128, [6] dry −128…128, [7] wet −128…128.
// Flanger (EffectKind::fruityFlanger): [0] delay 0…1024, [1] depth 0…5000, [2] rate 0…5000, [3] phase 0…360,
//   [4] damp 0…5000, [5] shape 0…1024, [6] feed 0…100, [7] invert feed, [8] invert wet (0…1024, on above half),
//   [9] dry, [10] wet, [11] cross 0…5000.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::fastdistfx
{
constexpr int params = 5;
struct State
{
    int pre = 128, threshold = 10, type = 0, mix = 128, post = 128; // FL's raw controls (a new instance's defaults)
};
// FL's saved state: five i32 in control order, no version
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

struct Gains
{
    float dry = 0.0f, wet = 0.0f, drive = 0.0f;
};
Gains gains (const State& s) noexcept;
// FL's host curve: set 0 (type A) / 1 (type B), curve 1…10; entries 0…8192 (8192 repeats 8191)
const std::int16_t* curve (int set, int curveIndex) noexcept;
// FL's host waveshaper (host slot 24) as any plugin calls it, in place: set 0/1, curve 1…10, dry/wet/drive gains;
// render = FL's file-render routine (interpolated), else the live one (Fast Dist; Fruit Kick's distortion)
void hostShaper (float* x, int n, int set, int curveIndex, float dry, float wet, float drive, bool render) noexcept;
// one channel, in place; render = FL's file-render kernel (interpolated)
void process (float* x, int n, const State& s, bool render) noexcept;
std::unique_ptr<Effect> make();
} // namespace lynx::fastdistfx

namespace lynx::flangusfx
{
constexpr int params = 8;
constexpr int maxOrder = 9;
using State = std::array<int, params>; // order, depth, speed, delay, spread, cross, dry, wet (raw)
constexpr State defaults { 4, 512, 768, 8, 128, -32, 96, 96 };
constexpr std::array<int, params> rawMin { 1, 0, 0, 0, 0, -128, -128, -128 };
constexpr std::array<int, params> rawMax { 9, 1024, 1024, 128, 128, 128, 128, 128 };

// FL's saved state: eight i32 in control order, no version
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

// The plugin object: FL's control changes, sample rate, block size, flush, and the render (interleaved stereo).
class Engine
{
public:
    Engine();
    ~Engine();
    // prepares the delay line for this rate (allocates; call off the audio thread) and applies it
    void prepare (int sampleRate);
    // FL's ProcessParam: a change of controls 0…5 re-runs the voice set-up (which may clear the delay line)
    void setParam (int index, int raw) noexcept;
    void load (const State& s) noexcept; // every control in order, like FL's state load
    void setRate (int sampleRate) noexcept;
    void setBlockSize (int frames) noexcept;
    void flush() noexcept; // FL's flush: gains at their targets, LFO phases restarted, delay line cleared
    int param (int index) const noexcept;
    // n ≤ 4096 frames, interleaved; in and out may be the same buffer
    void render (const float* in, float* out, int n) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// the mixer effect (the slot parameters carry FL's controls)
std::unique_ptr<Effect> make();
} // namespace lynx::flangusfx

namespace lynx::flangerfx
{
constexpr int params = 12;
using State = std::array<int, params>; // raw controls, all from 0
constexpr State defaults { 0, 2000, 2000, 118, 0, 0, 23, 0, 1024, 3500, 3000, 2500 };
constexpr State rawMax { 1024, 5000, 5000, 360, 5000, 1024, 100, 1024, 1024, 5000, 5000, 5000 };

// FL's saved state: i32 version (1), then twelve i32 in control order (later versions are not read by FL either)
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (int sampleRate); // allocates the delay lines and applies the rate
    void setParam (int index, int raw) noexcept;
    void load (const State& s) noexcept;
    void enable() noexcept; // FL's "enabled" notification: delay lines cleared, LFO restarted, feedback at 0
    int param (int index) const noexcept;
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

std::unique_ptr<Effect> make();
} // namespace lynx::flangerfx
