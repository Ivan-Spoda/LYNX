#pragma once

// FL Studio's chorus family as LYNX mixer effects, exact engines rebuilt in docs/analise/REA_rodada59.md (clean-room:
// layouts, formulas and behaviour in our own words; no Image-Line code or data in this repository). FL's saved state
// is only the controls, which become the slot parameters (value = (raw − min) / (max − min)) so the LYNX editor and
// automation can change them.
//
// Fruity Chorus (EffectKind::fruityChorus): [0] delay 0…1024, [1] depth 0…5000, [2] stereo 0…1024,
//   [3]…[5] LFO 1…3 frequency 0…5000, [6]…[8] LFO 1…3 waveform 0…1024, [9] cross type 0…100,
//   [10] cross cutoff 0…5000, [11] wet only 0…1024.
// Hyper Chorus (EffectKind::hyperChorus): [0] delay, [1] feedback, [2] mod rate, [3] mod amount, [4] movement,
//   [5] movement speed (0…1000 each), [6] ArachnaChor mode (0…1), [7] low-pass 7140…22000, [8] high-pass 120…10900
//   (minimum = off), [9] dry/wet 0…1000, [10] LFO phase difference 0…1000. EffectSlot::state holds the instance
//   number that picks FL's random LFO start phases (§2.6).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
// FL import: the exact chorus effect for an FL plugin name and saved state (nullopt for other plugins and for states
// that cannot be read), and the number of FL controls each exact chorus maps one to one onto its parameters (0 for
// other plugins)
std::optional<EffectSlot> chorusFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state);
int chorusControls (const std::string& pluginName) noexcept;
} // namespace lynx

namespace lynx::fruitychorusfx
{
constexpr int params = 12;
using State = std::array<int, params>; // raw controls, all from 0
constexpr State defaults { 512, 2250, 341, 1500, 2500, 3500, 23, 0, 0, 0, 2500, 0 };
constexpr State rawMax { 1024, 5000, 1024, 5000, 5000, 5000, 1024, 1024, 1024, 100, 5000, 1024 };

// FL's saved state: i32 version (FL writes 1; a version above 1 is not read), then twelve i32 in control order
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

// The plugin object: FL's control changes (their sample-rate dependent values use the rate set when the control is
// set), the "enabled" reset and the render (interleaved stereo).
class Engine
{
public:
    Engine();
    ~Engine();
    void setRate (int sampleRate) noexcept; // takes effect for the controls set afterwards (as in FL)
    void setParam (int index, int raw) noexcept;
    void load (const State& s) noexcept; // every control in order, like FL's state load
    // FL's "enabled" notification: filter states and delay lines cleared; FL draws the three LFO start phases at
    // random (0…32766 of a 2^24 cycle), LYNX takes the given ones
    void reset (std::uint32_t phase1 = 0, std::uint32_t phase2 = 0, std::uint32_t phase3 = 0) noexcept;
    int param (int index) const noexcept;
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

const EffectInfo& info();
std::unique_ptr<Effect> make();
} // namespace lynx::fruitychorusfx

namespace lynx::hyperchorusfx
{
constexpr int params = 11;
using State = std::array<int, params>; // raw controls
constexpr State defaults { 400, 500, 400, 400, 300, 300, 0, 22000, 120, 500, 100 };
constexpr State rawMin { 0, 0, 0, 0, 0, 0, 0, 7140, 120, 0, 0 };
constexpr State rawMax { 1000, 1000, 1000, 1000, 1000, 1000, 1, 22000, 10900, 1000, 1000 };

// FL's saved state: i32 version (FL writes 2; 3 and above are not read), eleven i32 in control order, one i32 of the
// editor (no effect on the sound)
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
State fromParams (const float* params) noexcept;
const char* paramName (int param) noexcept;
std::string paramText (int param, float value01);

// The engine (FL's Engine_x64.dll behind the plugin): values 0…1 = (raw − min) / (max − min).
class Engine
{
public:
    // `instance`: FL draws each instance's 16 LFO start phases from the C runtime's rand() (seed 1, per thread);
    // instance k takes calls 16k + 1 … 16k + 16 (§2.6)
    explicit Engine (int instance = 0);
    ~Engine();
    void setRate (double sampleRate) noexcept;
    // a parameter change: applied at the next sample rendered; a second change of the same parameter before that
    // glides from the first value (FL's smoother)
    void setParam (int index, float value) noexcept;
    float param (int index) const noexcept;
    void reset() noexcept; // FL's "disabled" notification: filters, cross-fade and the first chorus core cleared
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

const EffectInfo& info();
std::unique_ptr<Effect> make (int instance = 0);
std::uint32_t randState (int calls) noexcept; // the C runtime's rand() state after `calls` calls from seed 1
} // namespace lynx::hyperchorusfx
