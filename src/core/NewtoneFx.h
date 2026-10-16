#pragma once

// FL Studio's Newtone (pitch editor) and Newtime (time warper) as LYNX mixer effects, described in
// docs/analise/REA_rodada73.md (clean-room: layouts and behaviour in our own words; no Image-Line or zplane code or data
// in this repository).
//
// Both plugins are editors: the user loads a sample into them, edits it, and exports the result. On a mixer insert their
// render passes the insert's input through unchanged, and only when the plugin's "slave to host playback" switch is on
// AND FL is playing does the plugin's engine add the edited sample, positioned at the song time (REA_rodada73 §3).
// LYNX reproduces the pass-through exactly and, in slave mode, adds the source sample as it is (the edits themselves —
// zplane's pitch/time engines — are not reproduced; see §5).
//
// Parameters (EffectKind::newtone / EffectKind::newtime):  [0] slave to host playback (0/1).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
struct SampleData;
}

namespace lynx::newtonefx
{
constexpr int paramCount = 1;

// One track of the engine's project (XML "Trk_N")
struct Track
{
    int number = 0;
    bool timeWarp = false;        // "CzplJTimelineWarpSource" (Newtime); otherwise "CzplJPositionablePSOLASource" (Newtone)
    std::string filePath;         // the engine's working copy of the sample (a temporary file of the session that saved it)
    int notes = 0;                // pitch objects ("NumOfPitches")
    std::int64_t frames = 0;      // "OriginalFileLength"
    double selfBpm = 0.0;         // "SelfBPM"
    bool edited = false;          // a note or a global setting differs from the analysis (pitch, time, volume, formant...)
};

// The plugin's saved state (record 0xD5 of a mixer slot), REA_rodada73 §2
struct State
{
    int version = 0;                 // Newtone 0, Newtime 1
    int editorWidth = 0, editorHeight = 0;
    std::uint32_t flags = 0;         // options (bit 2: slave to host playback, bit 3: ignore host selection, bit 4: ignore own selection...)
    std::string sourcePath;          // record 3: the sample the user loaded, as the host stored it (ANSI); "" = none
    std::string projectXml;          // the engine's project (JUCE XML) from record 2
    std::vector<Track> tracks;

    bool slave() const noexcept { return (flags & 4u) != 0; }
    bool edited() const noexcept;
};

// nullopt = not a Newtone/Newtime state (wrong version, broken records)
std::optional<State> parse (const std::vector<std::uint8_t>& state);

// Parameters for a slot built from FL's state
void toParams (const State& s, EffectParams& params) noexcept;
std::string paramText (int param, float value01);

// The effect: the input passes through; with [0] on, while the host plays, `source` is added from the song time on.
// `source` may be null (sample not found): the effect is then a pass-through.
std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source);
} // namespace lynx::newtonefx
