#pragma once

// FL Studio's Fruity DrumSynth Live rebuilt from the behaviour decoded in docs/analise/REA_rodada30.md §1 (clean-room;
// the reference model was checked sample by sample against the installation's own DLLs). 120 patches, one per key
// (C0…B9), each synthesised by the engine's two filtered noise/sine oscillators; the optional recorded waveforms are
// read at run time from the user's own drumsynth_eng_x64.dll (ChannelDef::samplePath, SHA-256 checked), never shipped.
//
// ChannelDef::modelParams layout: 120 × 38 raw patch integers (as floats, patch slot s at 38·s, §1.4), then the
// editor's selected key and the state version (both informational: neither chooses the sound).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
constexpr int drumSynthPatches = 120;
constexpr int drumSynthPatchParams = 38;
constexpr int drumSynthParamCount = drumSynthPatches * drumSynthPatchParams + 2;
constexpr int drumSynthEditorKeyIndex = drumSynthPatches * drumSynthPatchParams;

// Key → patch slot (§1.2): keys 48…71 → slots 0…23, keys < 48 → key + 24, keys 72…119 → same; −1 = no patch
int drumSynthKeyToSlot (int key) noexcept;

// FL project state (plugin record 0xD5) → modelParams (layout above); nullopt if it is not a version 2…4 state
std::optional<std::vector<float>> drumSynthLiveParams (const std::vector<std::uint8_t>& state);

std::unique_ptr<Instrument> makeDrumSynthInstrument();

// true if `path` is the decoded drumsynth_eng_x64.dll build (its recorded waveforms can be read)
bool drumSynthDllUsable (const std::string& path);
} // namespace lynx
