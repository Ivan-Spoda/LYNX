#pragma once

// FL Studio's LuxeVerb, rebuilt from the behaviour decoded in docs/analise/REA_rodada60.md (clean-room). The plugin
// (Plugins/Fruity/Effects/LuxeVerb) is a Delphi host around the "shimmer reverb" engine of FL's shared DSP library
// (Shared/ildsp_x64.dll): input gain and filters, an envelope follower, a modulated predelay with a two-stage
// all-pass diffuser, a 16-line feedback delay network with a rotation matrix and per-line damping, a feedback path
// through a granular pitch shifter (or two feed-forward shifters), an output peak filter, a soft limiter, stereo
// width and dry/wet. The effect's parameters are FL's 49 controls stored as (raw − min) / (max − min) plus the four
// tempo-sync switches, so the slot stays editable and maps back to FL's integers exactly.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
constexpr int luxeVerbControlCount = 49;
constexpr int luxeVerbParamCount = luxeVerbControlCount + 4; // + sync of predelay, feedback delay, feed-forward 1 / 2

std::unique_ptr<Effect> makeLuxeVerb();

// FL state (versions 7…9) → the effect's parameters; nullopt if the layout is not recognised
std::optional<EffectSlot> luxeVerbFromFl (const std::vector<std::uint8_t>& state);

// Text of a parameter in FL's units
std::string luxeVerbParamText (int param, float value01);

namespace luxeverb
{
struct Range
{
    int lo, hi;
};
extern const std::array<Range, luxeVerbControlCount> ranges; // FL's control ranges
extern const std::array<const char*, luxeVerbParamCount> names;

struct Settings
{
    std::array<int, luxeVerbControlCount> raw {};
    std::array<bool, 4> sync {}; // predelay, feedback delay, feed-forward delay 1, feed-forward delay 2

    Settings();
    bool operator== (const Settings&) const = default;
};

Settings fromParams (const EffectParams& params) noexcept;
void toParams (const Settings& s, EffectParams& params) noexcept;
std::optional<Settings> parseState (const std::vector<std::uint8_t>& state);

// The plugin's mapping of a control's integer to the engine parameter it sends (REA_rodada60 §3.2)
float engineValue (int control, int raw) noexcept;

// The plugin with its engine, on separate stereo buffers in place. The sidechain (envelope source when the
// "Envelope sidechain" switch is on) defaults to the input.
class Engine
{
public:
    Engine();
    ~Engine();
    // creation, sample rate, state (FL's order: rate, all 49 controls, tempo with the sync choices)
    void prepare (int sampleRate, const Settings& s, double bpm);
    // the same in two steps: create() allocates (not for the audio thread), load() only computes
    void create (int sampleRate);
    void load (const Settings& s, double bpm);
    void flush() noexcept; // FL's reset: clears the lines, filters and the feedback shifter
    void set (const Settings& s, double bpm); // control / switch / tempo changes (FL's ProcessParam / tempo event)
    void process (float* left, float* right, int n, const float* scLeft = nullptr, const float* scRight = nullptr) noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> d;
    Settings cur;
    double curBpm = 0.0;
};
} // namespace luxeverb
} // namespace lynx
