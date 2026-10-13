#pragma once

// Saved state (0xD5) of FL's native effects in mixer slots -> parameters of LYNX's built-in
// effects. Formats and formulas in docs/analise/REA_rodada7.md (clean-room).

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Builds the slot from the FL plugin name and the blob. Without a usable blob, returns the similar
// built-in effect with default parameters; nullopt when there is no similar effect.
// `bpm` converts tempo-synced times (e.g. Reeverb 2 predelay) to milliseconds.
std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm = 120.0);

// FL effect preset (.fst from Plugin presets\Effects): the built-in effect with the preset's settings.
std::optional<EffectSlot> effectFromFlPreset (const std::vector<std::uint8_t>& fileBytes, double bpm = 120.0);

// FL plugins whose presets map to this built-in effect (for listing the preset folders).
std::vector<std::string> flPluginsForEffect (EffectKind kind);
// Automation of an FL native effect parameter: our equivalent parameter and the conversion from FL's
// normalized value (0..1 over its parameter range) to ours. nullopt = no equivalent.
struct FlParamMap
{
    int param = 0;
    std::function<float (float)> convert;
};
std::optional<FlParamMap> effectParamFromFl (const std::string& pluginName, int flParam);

// Head of a Patcher branch leaving bus `bus` of a Frequency Splitter or Stereo Shaper
// (REA_rodada17 §1-2). Splitter: bus 0 = sum of the bands; bus k = the band whose destination (word
// 15 + i) equals k, filtered by the crossovers with the combo's slope; with 2 bands the mid is muted.
// Stereo Shaper: 0 = matrix output; the "difference" destination (parameter 6) = input − output.
struct PatcherBranchHead
{
    bool valid = false;
    std::optional<EffectSlot> slot; // effect at the head of the branch (none = direct signal)
    float gain = 1.0f;
    std::string note;               // approximation used (for the report)
};
PatcherBranchHead patcherBranchHead (const std::string& pluginName, const std::vector<std::uint8_t>& state, int bus);

// Fruity Love Philter -> LYNX effects (REA_rodadas 16 §2 and 17 §15): each enabled unit becomes a parametric
// EQ (type, cutoff and Q from FL's formulas at 44.1 kHz) + gain/pan. One unit: `slots` in series
// (and `paramMap` translates FL automation to them); several in parallel (NEXT = 0): a container.
struct LovePhilterConversion
{
    std::vector<EffectSlot> slots;
    // automation of FL parameter k -> (index within `slots`, our parameter, value conversion)
    std::function<std::optional<std::tuple<int, int, std::function<float (float)>>> (int k)> paramMap;
    std::vector<std::string> notes; // what was left out (for the report)
    // live automations: the control output of slot `source` drives parameter `param` of slot `target`
    // (indices within `slots`), through the curve (33 points over 0..1)
    struct LiveLink
    {
        int source = 0, target = 0, param = 0;
        std::vector<float> curve;
    };
    std::vector<LiveLink> links;
};
// The Fruity Limiter's compression section as a LYNX compressor (for the sidechain, which only feeds the
// compressor's detector; REA_rodada22 §1)
std::optional<EffectSlot> fruityLimiterCompressor (const std::vector<std::uint8_t>& state);

std::optional<LovePhilterConversion> lovePhilterEffects (const std::vector<std::uint8_t>& state);

// Current value (0..1) of output `output` of a static controller (Fruity X-Y Controller, Control
// Surface; REA_rodada15 §11): a link from it becomes a fixed value on the target. nullopt if unreadable.
std::optional<float> controllerConstantOutput (const std::string& pluginName, const std::vector<std::uint8_t>& state, int output);
} // namespace lynx
