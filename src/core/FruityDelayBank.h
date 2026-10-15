#pragma once

// FL Studio's Fruity Delay Bank as an exact LYNX mixer effect (docs/analise/REA_rodada56.md; clean-room: rebuilt from
// the observed behaviour of the plugin, no Image-Line code).
//
// Eight delay banks. Each bank: input pan/volume matrix, an optional send from the previous bank, a biquad cascade
// ("filter 1") before or after the delay, a tempo-synced or free delay line with a stereo time offset, a feedback path
// (biquad cascade "filter 2", feedback pan / level / stereo matrix, normal / swapped / ping-pong routing), a stepped
// raised-cosine gain pattern, and an output pan/volume matrix. Global dry, output, input and feedback levels, a bank
// solo and an optional 2× oversampling.
//
// The LYNX slot keeps FL's own saved state in EffectSlot::state (the plugin has 131 automatable controls plus per-bank
// options, far more than an effect slot's parameters); the four global controls are also the slot's LYNX parameters
// (raw / 128), so they stay editable and automatable.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
class Effect;
}

namespace lynx::fldbank
{
constexpr int banks = 8;
constexpr int stateControls = 133; // 0…3 global, 4 + 16·b + k bank controls (131 automatable), 131 solo, 132 oversampling
constexpr int automatable = 131;
constexpr int globalControls = 4;  // dry, output, input, feedback master (0…128)

struct BankOptions
{
    int filterType[2] {}, filterOrder[2] {}; // filter 1 (input) and filter 2 (feedback): type 0…7, extra sections
    bool filterAfter = false;                // filter 1 after the delay
    int mode = 1;                            // 0 no delay, 1 normal, 2 swapped outputs, 3 ping-pong feedback
    int steps = 0, amount = 0;               // gain pattern: steps per delay time (0 = off), shape −128…128
    bool enabled = false, tapBeforeFeedback = false, sync = true;
};

struct State
{
    int version = 16;
    std::array<int, stateControls> p {};
    std::array<BankOptions, banks> bank {};
};

State defaultState(); // a new FL instance
// FL's saved state (0xD5): u32 version (15 or 16), 133 × i32 controls, then 32 option bytes per bank. Version 15 stored
// the bank times in units ten times longer.
std::optional<State> parse (const std::vector<std::uint8_t>& state);
std::vector<std::uint8_t> serialise (const State& s); // version 16 layout

// Control names / ranges (index into State::p, 0…130)
const char* controlName (int index) noexcept;
int controlMin (int index) noexcept;
int controlMax (int index) noexcept;

class Engine
{
public:
    // maxBlock: the longest block process() will get (FL sizes the delay lines with the host block).
    // A new FL instance starts its gain ramps at 0 (fades in over ~1000 samples); `settled` starts at the targets.
    Engine (const State& s, double sampleRate, double bpm, int maxBlock = 512, bool settled = false);
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setControl (int index, int raw); // as FL's ProcessParam (0…130)
    void setTempo (double bpm);
    void process (float* left, float* right, int n) noexcept; // in place, n ≤ maxBlock

    struct Impl;

private:
    std::unique_ptr<Impl> d;
};

// LYNX effect: the slot state is FL's state bytes; params 0…3 = the global controls / 128
std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState);
const char* globalName (int index) noexcept;
std::string globalText (int index, float value);
} // namespace lynx::fldbank
