#pragma once

// FL Studio's Fruity DX10 rebuilt as the engine decoded in docs/analise/REA_rodada42.md (clean-room; a 1:1 port of our
// own round-42 reference model r42/py/dxmodel.py, which renders bit-identically to the installation's DLL on all 64
// distinct DX10 states of the installation and on a synthetic battery of 256 runs).
//
// Per voice: a phase accumulator φ (period 2) advanced by the carrier increment plus two sine modulators (Mod 1 and
// Mod 2, in parallel, on the host's 16384-point sine table) whose levels glide from Init to Sustain (and to 0 after the
// release), folded into ±1 and shaped by the "Waveform" polynomial φ + φ³·(r·(φ² − 1) − 1) plus the "Mod Thru" share of
// modulator 1; an amplitude envelope (attack/decay/release one-pole laws, normalised by 1.2/peak); a plugin-wide
// vibrato oscillator stepped every 100 samples of each voice; voice stealing past the host's max polyphony.
// The output goes through Image-Line's plugin voice processor (the channel "INS" envelopes, LFOs and filter, kept by
// the plugin itself): this port implements its neutral state exactly (pan law gains from Final.Pan/Final.Vol with the
// gain-ramp rule, voices mixed in list order); channels whose envelope/LFO/filter settings are not neutral are reported
// by dx10ProcessorNeutral() and stay on the importer's approximation (REA_rodada42 §3).
//
// The channel keeps the plugin's raw saved state (record 0xD5, version 1 = 92 bytes or 2 = 460 bytes) in
// ChannelDef::pluginState.
//
// Host interface (REA_rodada28 §4.1 inferences, as for Harmless): Init.Vol = (11^(1.28·velocity) − 1)/10; FL's
// default "linked velocity" gives Final.Vol = channel volume × Init.Vol (DX10 sends no link event); Final.Pan = note
// pan + channel pan; Final.Pitch = 100·(key − 60) + channel pitch + note fine pitch (cents). LYNX's channel gains are
// turned back into FL's channel volume/pan (gl = √2·V·cos((1 + p)π/4), gr = √2·V·sin(…)) and applied once, inside the
// engine. Voices that DX10 flags as finished are removed at the end of the block, as FL's host does on the plugin's
// kill request.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this DX10 state (version 1 or 2 with the 22 parameters)
bool dx10StateUsable (const std::vector<std::uint8_t>& state);

// true when the state's channel processor settings (the 364-byte snapshot of a version-2 state, else the neutral
// defaults) leave the sound untouched: no envelope/LFO acting on pan, volume, cutoff, resonance or pitch and the
// default "fast LP" filter fully open. `snapshot` (optional, 364 bytes) overrides the state's own.
bool dx10ProcessorNeutral (const std::vector<std::uint8_t>& state);
bool dx10SnapshotNeutral (const std::vector<std::uint8_t>& snapshot);

// Raw integer parameter P[index] (0…21) of a state, or nullopt
std::optional<int> dx10Param (const std::vector<std::uint8_t>& state, int index);
// Writes P[index] (clamped to its knob range: 0…65536, Octave −3…3); false if the state can't be read
bool dx10SetParam (std::vector<std::uint8_t>& state, int index, int value);

std::unique_ptr<Instrument> makeDx10Instrument();

namespace dx10
{
// One note for the reference renderer (drives the engine exactly like the reference model's run()).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchOffset = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch − 100·(key − 60)
    int release = -1;                                                        // sample of the note-off (−1: held)
};

// Renders `notes` from `state` like dxmodel.run(): host blocks of `block` samples split at every event, max
// polyphony `maxPoly` (0 = unlimited). Interleaved stereo float samples (2·length); empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, int maxPoly = 0);
} // namespace dx10
} // namespace lynx
