#pragma once

// FL Studio's Emphasizer (FL 2025+, the one-knob sibling of Emphasis) as an exact LYNX engine, rebuilt in
// docs/analise/REA_rodada154.md (clean-room: the plug-in's layout, formulas and behaviour in our own words; no
// Image-Line code or data in this repository).
//
// The plug-in is a thin front end over a look-ahead limiter of FL's shared DSP library: input gain → peak detector
// (|L|, |R| max, scaled down by the Emphasis amount) → 8-stage peak hold → attack / program-dependent release envelope
// → gain = ceiling / max(envelope, ceiling) applied to the 2 ms delayed signal → hard or soft clip → output gain →
// optional level matching (÷ input gain) → bypass cross-fade with the delayed dry signal, × 0.9999.
//
// Parameters (EffectKind::emphasizer) are FL's controls over their range, value = raw / max, so FL automation and the
// LYNX editor drive what FL's ProcessParam receives:
//   [0] Input gain 0…10000 (0…+24 dB, linear in dB)   [1] Emphasis 0…10000 (0…100 %, default 5013)
//   [2] Soft clipping 0/1                              [3] Level matching 0/1
//   [4] Bypass 0/1 (a 20 Hz cross-fade, not a hard switch)   [5] Output 0…10000 (FL's volume curve, 10000 = 0 dB)
// Host blocks longer than 512 frames are processed as 512-frame engine calls (per-call quantities: the level
// matching factor and the detector scale).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::emphasizer
{
constexpr int numParams = 6;
constexpr std::array<int, numParams> rawMax { 10000, 10000, 1, 1, 1, 10000 };
constexpr std::array<int, numParams> rawDefault { 0, 5013, 0, 0, 0, 10000 };

// The plug-in's saved state (record 0xD5): i32 version (1 or 2) + 5 (v1) or 6 (v2) i32 controls + u8 loudness meter
struct State
{
    std::array<int, numParams> raw = rawDefault;
    bool loudnessMeter = true;
};
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;

std::string paramText (int param, float v01);
std::unique_ptr<Effect> make();

// What the front end sends to the engine for control k at raw value v (dB for 0 and 5, the amount for 1, the raw
// switch for 2…4)
float engineValue (int k, int raw) noexcept;

// The library's limiter, one instance per plug-in
class Engine
{
public:
    static constexpr int ring = 65536; // per-channel work buffers / delay rings (FL: 0x10000 floats)

    void init (float sampleRate);
    void setParam (int k, float v) noexcept;
    // n <= ring; in and out may alias
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;

    float gainReduction() const noexcept { return lastGain; } // last sample's gain (≤ 1)

private:
    struct Delay
    {
        std::vector<float> buf = std::vector<float> (ring, 0.0f);
        std::uint16_t write = 0, read = 0;
        std::uint16_t length = 0;
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); write = read = 0; }
        void run (const float* in, float* out, int n) noexcept;
    };

    float sr = 44100.0f, invSr = 1.0f / 44100.0f;
    float smooth = 0.0f; // one-pole 20 Hz coefficient for the gain ramps
    float inDb = 0.0f, inTarget = 1.0f, inGain = 1.0f;
    float outDb = 0.0f, outTarget = 1.0f, outGain = 0.0f; // the output ramp starts from 0 (a fresh instance fades in)
    float amount2 = 0.0f, attackT = 0.0f, releaseT = 0.0f, relFast = 0.0f, relSlow = 0.0f, detScale = 1.0f;
    float softClip = 0.0f, levelMatch = 0.0f, bypass = 0.0f, mix = 0.0f;
    float lastGain = 0.0f, refGain = 0.0f;

    // peak hold + envelope
    std::array<float, 8> holdV {};
    std::array<std::uint64_t, 8> holdN {};
    float holdLen = 0.0f;
    float envRelease = 0.0f, envAttack = 0.0f, env = 0.0f, lastHold = 0.0f;
    float envAttackT = 0.0f, envReleaseT = 0.0f;

    Delay dryL, dryR, wetDelayL, wetDelayR;
    std::vector<float> dryBufL = std::vector<float> (ring), dryBufR = std::vector<float> (ring),
                       wetL = std::vector<float> (ring), wetR = std::vector<float> (ring), gain = std::vector<float> (ring);
};
} // namespace lynx::emphasizer
