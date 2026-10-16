#pragma once

// FL Studio's Fruity Love Philter as a LYNX mixer effect: the plugin's own engine rebuilt from the installation's DLL
// in docs/analise/REA_rodada66.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or
// data in this repository). Rounds 16–20 and 27 decoded the state and the laws; round 66 decoded the render itself
// (oversampling, control ticks, articulators, the twelve filter kernels, waveshaper, envelope follower, pan law) and
// verified it against the DLL.
//
// The effect slot keeps FL's saved state (plugin record 0xD5) in EffectSlot::state. The 48 slot parameters are the
// controls LYNX's editor and automation reach, each as (raw − min) / (max − min) of FL's raw range:
//   [0…7]   the globals: master level, master LFO level, follower level, follower attack, follower release, pattern,
//           Mod X, Mod Y;
//   [8 + 5u + 0…4] unit u (0…7): cutoff, resonance, envelope amount, pan, volume.
// The other unit controls (L/B/H, waveshaper, IN, NEXT) and the articulators live in the state only.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::philter
{
constexpr int paramCount = 48;

// FL parameter index (0…358, REA_rodada16 §2.4) of slot parameter `param`
int flIndexOf (int param) noexcept;
// slot parameter of FL parameter index k (−1 when the slot does not expose it)
int paramOfFlIndex (int k) noexcept;
// FL's raw range of FL parameter k (for the 48 exposed ones)
void rawRange (int k, int& lo, int& hi) noexcept;

// FL's state → the engine's view; nullopt when it cannot be read (version outside 1…12, bad zlib, truncated)
struct State;
std::shared_ptr<const State> parse (const std::vector<std::uint8_t>& flState);
int rawParam (const State& s, int k) noexcept; // current raw value of FL parameter k (exposed ones)

// The plugin object. Host model: FL's mixer (rate, process mode, tempo and transport messages, parameter changes).
class Engine
{
public:
    explicit Engine (std::shared_ptr<const State> state);
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setRenderMode (bool rendering) noexcept; // FL's process-mode flag (HQ non-realtime): oversampling ×2, fine ticks
    void setRate (int sampleRate) noexcept;       // the sample-rate message: everything restarts at the song start
    void setTempo (double bpm) noexcept;          // FL's tempo event (default 140)
    // transport: FL's "playing" message (with the song position in beats); while playing, envelopes and LFOs are
    // positioned at the song position, otherwise they run freely from where they are restarted
    void setPlaying (bool playing, double songBeats) noexcept;
    void jump (double songBeats) noexcept; // song position changed while playing
    void setParam (int k, int raw) noexcept; // FL parameter k (0…358), raw value
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// FL import: the exact Love Philter slot for a saved state (nullopt when the state cannot be read)
std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState);
const EffectInfo& info();
std::unique_ptr<Effect> make (const EffectSlot& slot);
std::string paramText (int param, float value01);
} // namespace lynx::philter
