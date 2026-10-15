#pragma once

// FL Studio's Fruity Vocoder as a LYNX mixer effect: the plugin's own engine rebuilt from the installation's DLL in
// docs/analise/REA_rodada66.md §6 (layout and laws first decoded in REA_rodada20 §4.1; clean-room: behaviour and
// formulas only, no Image-Line code or data). The left input is the modulator and the right input the carrier (swapped
// with FL's swap option); the output is mono on both sides.
//
// The slot keeps FL's saved state (plugin record 0xD5: band count, order, listen mode, swap, the per-band levels and the
// twelve controls) in EffectSlot::state; the slot parameters are FL's twelve controls as (raw − min) / (max − min) so
// the LYNX editor and automation can change them: [0] FreqMin, [1] FreqMax, [2] FreqScale, [3] Invert, [4] Formant,
// [5] Bandwidth, [6] Attack, [7] Release, [8] Hold, [9] Modulator level, [10] Carrier level, [11] Wet level.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::flvocoder
{
constexpr int paramCount = 12;

struct State
{
    int bands = 16, order = 2, listen = 2;
    bool swap = false;
    std::vector<float> levels; // per analysis band
    std::array<int, paramCount> p { 0, 65536, 64, 0, 0, 50, 20, 100, 0, 128, 128, 128 };
};
// FL's state → State; nullopt when it is not a state the plugin reads (version ≤ 1, band count not 4…128, truncated)
std::optional<State> parse (const std::vector<std::uint8_t>& flState);
void rawRange (int param, int& lo, int& hi) noexcept;

class Engine
{
public:
    explicit Engine (const State& s);
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;
    void setRate (int sampleRate) noexcept;   // recomputes the banks (the filter memories are kept, as in FL)
    void setParam (int index, int raw) noexcept;
    void reset() noexcept;                    // FL's flush: filter memories and envelopes cleared
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState);
const EffectInfo& info();
std::unique_ptr<Effect> make (const EffectSlot& slot);
std::string paramText (int param, float value01);
} // namespace lynx::flvocoder
