#pragma once

// LYNX substitutes for missing third-party plugins (docs/PLUGIN_FALLBACK.md).
//
// When a project (.flp or .lynx) uses a third-party plugin that isn't installed here, LYNX can put one of its own
// built-in effects "in the role of" that plugin: a reverb for a reverb, a delay for a delay... The slot keeps the
// ORIGINAL plugin reference byte for byte (EffectSlot::substituteFor), so the project saves it back unchanged and
// reopens with the real plugin on a machine that has it.
//
// What stands in for what is DATA: a text table (built in, extendable with registerPluginSubstitutes) of entries
// that name the third-party product (VST2 unique IDs, VST3 class IDs, CLAP ids, plugin / file names, Waves shell
// sub-plugin names), the LYNX effect, a default preset and, where the saved state is readable, a parameter map.
// Parameter maps only use layouts that are visible in the saved data itself (VST2 float parameters, the attributes of
// a JUCE XML state, Waves' "RealWorld" parameter lists) and meanings known from public documentation or our own
// black-box measurements; never decompiled third-party code. Without a readable state the default preset is used.
//
// Does not depend on JUCE so it can be tested in isolation.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// One parameter of the substitute taken from the original plugin's saved state.
struct SubstituteParamMap
{
    enum class Source : std::uint8_t
    {
        vst2,  // FL's VST2 state (parseFlVst2State): float parameter `index` of the current program, 0..1
        xml,   // attribute `attribute` of the first element of a JUCE XML state ("VC2!" block or plain "<?xml")
        waves  // value `index` of the active setup of a Waves "PresetChunkXMLTree" (real-world units)
    };
    Source source = Source::vst2;
    int index = 0;
    std::string attribute;
    // target: a parameter of the LYNX effect (by position), or the slot's dry/wet (`toSlotMix`)
    int target = 0;
    bool toSlotMix = false;
    // linear map, clamped: in [inLo, inHi] -> out [outLo, outHi] (0..1 normalised parameter)
    float inLo = 0.0f, inHi = 1.0f, outLo = 0.0f, outHi = 1.0f;
    // or a point list ("| curve x:y x:y …": piecewise linear, clamped at the ends; "| steps x:y …": the y of the last
    // point whose x ≤ the value — enumerations), x ascending
    enum class Shape : std::uint8_t { linear, curve, steps };
    Shape shape = Shape::linear;
    std::vector<std::pair<float, float>> points;
};

struct SubstituteEntry
{
    std::string key;     // stable id of the entry ("valhalla-vintageverb")
    std::string product; // the third-party product, for display ("ValhallaVintageVerb")
    std::string vendor;
    bool instrument = false; // reserved: the loader substitutes effects only (instrument entries are not applied yet)

    // what identifies the product
    std::vector<std::uint32_t> vst2Ids;
    std::vector<std::array<std::uint8_t, 16>> vst3ClassIds; // TUID as laid out in memory (FL's record 0x34)
    std::vector<std::string> clapIds;
    std::vector<std::string> names;      // pluginNameKey of plugin / file names ("valhallavintageverb")
    std::vector<std::string> wavesNames; // pluginNameKey of a WaveShell sub-plugin's <PluginName> ("rcomp")

    // the LYNX effect and its default preset
    EffectKind effect = EffectKind::eq;
    std::vector<std::pair<int, float>> preset; // parameter position -> normalised value, over makeEffectSlot (effect)
    float mix = 1.0f;                          // slot dry/wet of the substitute
    std::vector<SubstituteParamMap> params;
    // VST2 parameters per program (AEffect::numParams), needed to find the current program in a state saved with
    // "all programs" (FL doesn't store the count); 0 = unknown (then only program 0 can be read from such a state)
    int vst2Params = 0;
};

// What a substitute is matched on and translated from: the original plugin's identity and its own saved state.
struct PluginIdentity
{
    ForeignPlugin::Format format = ForeignPlugin::Format::none;
    std::string name, path;
    std::uint32_t vst2Id = 0;
    std::array<std::uint8_t, 16> classId {};
    bool hasClassId = false;
    std::string clapId;
    // VST2: FL's state layout (the 0x35 record / LYNX's VST2 host state); VST3: the component state; CLAP: the raw state
    std::vector<std::uint8_t> state;
};

// The identity of an .flp plugin (FL's wrapper record); for VST3 the state is the component part of FL's state.
PluginIdentity pluginIdentity (const ForeignPlugin& plugin);

// Parses entries in the table format (docs/PLUGIN_FALLBACK.md). Lines it can't use are skipped and counted in
// `errors` (when given). Entries whose LYNX effect name is unknown are dropped (counted as errors).
std::vector<SubstituteEntry> parseSubstituteTable (std::string_view text, int* errors = nullptr);

// The table in use: the built-in entries plus the registered ones (later registrations win on the same key).
const std::vector<SubstituteEntry>& pluginSubstitutes();
// Adds entries (table text) — the hook other LYNX modules use to offer themselves as substitutes. Returns how many
// entries were added. Call it from the UI/message thread before projects open.
int registerPluginSubstitutes (std::string_view tableText);

// The entry standing in for `plugin` (effect or instrument), or null. Matches by VST2 ID, VST3 class ID, CLAP id,
// Waves sub-plugin name (read from the state), then by plugin / file name (a WaveShell file name never matches
// by name: the shell holds many plugins).
const SubstituteEntry* findPluginSubstitute (const PluginIdentity& plugin, bool instrument);

// The built-in effect slot standing in for the plugin: the entry's preset, then the parameters the saved state lets
// us translate. `translated` (when given) gets how many parameters came from the state.
EffectSlot makeSubstituteSlot (const SubstituteEntry& entry, const PluginIdentity* original = nullptr, int* translated = nullptr);

// Replaces a plugin effect slot (kind == plugin) by its substitute, keeping the original reference in
// `substituteFor` and the slot's on/off, dry/wet (multiplied by the substitute's) and side-chain routing.
// False (slot unchanged) when no entry matches. `identity` is the original's identity (pluginIdentity for .flp).
bool substituteEffectSlot (EffectSlot& slot, const PluginIdentity& identity);

// A substitute slot back to its original plugin (kind = plugin, plugin = substituteFor). False if not a substitute.
bool restoreOriginalPlugin (EffectSlot& slot);

// Display name of the original plugin of a substitute slot ("" when not a substitute).
std::string substituteOriginalName (const EffectSlot& slot);

// --- state readers (exposed for tests) --------------------------------------------------------------------------
// The attributes of the first element of the JUCE XML state inside `state` (after "VC2!" or the first "<?xml").
std::vector<std::pair<std::string, std::string>> juceXmlStateAttributes (const std::vector<std::uint8_t>& state);
// The <PluginName> of a Waves preset chunk ("" when not a Waves state).
std::string wavesPluginName (const std::vector<std::uint8_t>& state);
// The "RealWorld" parameter values of the active setup of a Waves preset chunk ("*" = unused -> NaN).
std::vector<double> wavesRealWorldParams (const std::vector<std::uint8_t>& state);

// --- lossless text form of an .flp plugin reference (.lynx keeps it for substitutes) ------------------------------
// Base64 of a versioned binary record of every ForeignPlugin field; foreignFromText(foreignToText(p)) == p, the
// state byte for byte.
std::string foreignToText (const ForeignPlugin& plugin);
bool foreignFromText (const std::string& text, ForeignPlugin& out);

std::string base64Encode (const std::uint8_t* data, std::size_t size);
bool base64Decode (const std::string& text, std::vector<std::uint8_t>& out);
} // namespace lynx
