#pragma once

// Controls that live in an effect slot's saved state (EffectSlot::state) instead of its parameters.
//
// Several FL effects keep far more settings than an effect slot has parameters (Fruity Delay Bank's eight banks,
// Multiband Delay's and EQUO's band tables, Fruity Convolver's knobs, Maximus' bands): LYNX keeps FL's own state bytes
// and builds the exact engine from them. The native editors (core/PluginLayout.h) edit those settings through the
// tables below: each control has a name, FL's raw range and default, and reads / writes its raw value in the state.
// They are not automatable (FL's own automation of them is not imported either).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::stateparams
{
struct Param
{
    std::string name;
    int minimum = 0, maximum = 1, defaultValue = 0;
};

struct Table
{
    std::vector<Param> params;
    // the raw value of control `index` in a slot state (nullopt = not a state of this effect)
    std::function<std::optional<int> (const std::vector<std::uint8_t>& state, int index)> get;
    // the slot state with control `index` set to `raw` (clamped to its range); unchanged when it cannot be edited
    std::function<std::vector<std::uint8_t> (const std::vector<std::uint8_t>& state, int index, int raw)> with;
    // the value as the editor shows it (the state gives context: a tempo-synced time, the filter type, ...)
    std::function<std::string (const std::vector<std::uint8_t>& state, int index, int raw)> text;

    int index (const char* name) const; // −1 = no such control
    const char* name (int index) const; // "" = out of range
};

// The state-backed controls of an effect kind (nullptr = none)
const Table* tableFor (EffectKind kind);

// Fruity Delay Bank: names of bank b's (0…7) controls, e.g. bankControl (0, "Time") = "B1 Time"
std::string bankControl (int bank, const char* control);
} // namespace lynx::stateparams
