#pragma once

// FL Studio's Frequency Splitter as an exact LYNX engine, rebuilt in docs/analise/REA_rodada62.md (clean-room: layouts,
// formulas and behaviour in our own words; no Image-Line code or data in this repository).
//
// The plugin is a thin wrapper around a VST2 engine (`engine_x64.dll`, "HQ 3-Band Splitter") with 26 parameters,
// each 0…1. LYNX's slot (EffectKind::frequencySplitter) carries those 26 engine values, so the editor can change them
// and FL's automation maps one to one, plus three LYNX-only output flags:
//   [0] low/mid frequency, [1] mid/high frequency (30·2^(9.0589·v) Hz), [2] low/mid slope, [3] mid/high slope
//   (⌊v/0.125⌋ → 6/12/18/24/36/48/72/96 dB/oct), [4] filter mode (0 normal IIR, 1 FFT linear phase),
//   [5] number of bands (< 0.5: 2, else 3), [6] FFT quality (⌊v·5.994⌋: 512…16384 at 44.1 kHz), [7] FFT cross-fade
//   time (10^(2v)·2 ms), [8…10] low/mid/high send level, [11…13] low/mid/high level in the summed output
//   (v < 0.5: 2v, else 1 + 13.8866·(v − 0.5); 0.5 = 0 dB), [14…16] send mutes, [17…19] summed mutes,
//   [20…22] send solos, [23…25] summed solos (≥ 0.5 = on);
//   [26…28] LYNX only: which band sends form the output (low, mid, high); all off = the summed output (FL's main
//   output). A Patcher bus k of FL is the sum of the bands whose send destination is k.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::fsplit
{
constexpr int engineParams = 26;
constexpr int outputLow = 26, outputMid = 27, outputHigh = 28;
constexpr int slotParams = 29;

// FL's saved state (record 0xD5, version 4): the 17 controls as the wrapper keeps them, the mute bits and the
// engine values the wrapper derives from them
struct State
{
    std::array<int, 17> words {}; // [0] low freq, [1] high freq (0…65536), [2,3] slopes (0…7), [4] mode, [5] 3 bands,
                                  // [6] quality (0…5), [7] cross-fade (0…65536), [8…13] levels (−1800…1800),
                                  // [14…16] send destinations of the low, mid and high band (0 = none)
    int muteBits = 0;             // bit 0…2 send mutes, 3…5 summed mutes
    std::array<float, engineParams> params {}; // the engine's parameters after the state load
    std::array<int, 3> destinations() const noexcept { return { words[14], words[15], words[16] }; }
};
std::optional<State> parseState (const std::vector<std::uint8_t>& state);

// The slot's parameters for the summed output (bus 0) or for FL bus `bus` (the bands sent to it)
void toParams (const State& s, float* params, int bus = 0) noexcept;
bool busUsed (const State& s, int bus) noexcept; // bus 0 always; bus k when a band is sent to it

const EffectInfo& info();
std::string paramText (int param, float value01);

// The engine, block by block. Outputs: band sends (low, mid, high) and the summed output, each stereo.
class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void prepare (double sampleRate);
    // settled = true: as an instance that has been running (gains at their targets, the FFT response loaded without
    // a fade-in); false: as the plugin right after it was created (gains from 0, FFT faded in from an empty response)
    void reset (bool settled) noexcept;
    // the engine's setParameter: only the parameter's own effects (FL's order of side effects is kept)
    void setParameter (int index, float value) noexcept;
    float parameter (int index) const noexcept;
    // in: two channels; out[0…7]: low L/R, mid L/R, high L/R, sum L/R (any may be nullptr)
    void process (const float* inL, const float* inR, float* const* out, int n) noexcept;
    int latencySamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

std::unique_ptr<Effect> make();
} // namespace lynx::fsplit
