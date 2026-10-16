#pragma once

// FL Studio's Fruity DX10 rebuilt as the engine decoded in docs/analise/REA_rodada42.md (clean-room; a 1:1 port of our
// own round-42 reference model r42/py/dxmodel.py + dxproc.py, which renders bit-identically to the installation's DLL on
// all 64 distinct DX10 states of the installation, on the 12 project channels with their channel settings and on
// synthetic batteries).
//
// Per voice (§1): a phase accumulator φ (period 2) advanced by the carrier increment plus two sine modulators (Mod 1 and
// Mod 2, in parallel, on the host's 16384-point sine table) whose levels glide from Init to Sustain (and to 0 after the
// release), folded into ±1 and shaped by the "Waveform" polynomial φ + φ³·(r·(φ² − 1) − 1) plus the "Mod Thru" share of
// modulator 1; an amplitude envelope (attack/decay/release one-pole laws, normalised by 1.2/peak); a plugin-wide
// vibrato oscillator stepped every 100 samples of each voice; voice stealing past the host's max polyphony.
// Then Image-Line's plugin voice processor, which DX10 keeps itself (§3): the channel's five "INS" articulators (pan,
// volume, cutoff, resonance, pitch: delay/attack/hold/decay/sustain envelopes with tensions and delayed, faded LFOs,
// stepped once per sequencer tick; DX10 never releases them), the channel filter (fast LP, 12 dB LP/BP/HP/BS, 24 dB LP,
// SVF LP ×1/×2, Mod X/Y), the pan law and gain ramps, the 10 ms end fade when the volume envelope dies; voices mixed in
// list order.
//
// The channel keeps the plugin state in ChannelDef::pluginState as version 2 (92 bytes of parameters + the 364-byte
// channel-settings snapshot): FL's own version-2 states carry it; for version-1 states the importer appends the
// channel's 0xDA/0xDB records, which FL's host hands the plugin (§3.1). ChannelDef::modelParams[0] (optional) is the FL
// project's PPQ, which sets the tick rate of the articulators (96 when absent).
//
// Host interface (REA_rodada28 §4.1 inferences, as for Harmless): Init.Vol = (11^(1.28·velocity) − 1)/10; FL's
// default "linked velocity" gives Final.Vol = channel volume × Init.Vol (DX10 sends no link event); Final.Pan = note
// pan + channel pan; Final.Pitch = 100·(key − 60) + channel pitch + note fine pitch (cents), plus the pitch
// articulator (restored every tick, inference); Final.FCut/FRes (Mod X/Y of notes) = 0. LYNX's channel gains are
// turned back into FL's channel volume/pan (gl = √2·V·cos((1 + p)π/4), gr = √2·V·sin(…)) and applied once, inside the
// engine. Sequencer ticks fall on the song's PPQ grid while the transport moves, else run freely at the same rate.
// Voices that DX10 or the processor flag as finished are removed at the end of the block, as FL's host does on the
// plugin's kill request.
//
// Differences from FL by design / not modelled (REA_rodada42 §4): global LFOs ignore song-position jumps; LYNX notes
// have no Mod X/Y. (The end-fade table uses IPP's cosine since REA_rodada74.)

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this DX10 state (version 1 or 2 with the 22 parameters)
bool dx10StateUsable (const std::vector<std::uint8_t>& state);

// The state as version 2 with `snapshot` (364 bytes) as its channel settings; a version-2 state keeps its own unless
// `replace` is set. Empty if the state is unusable.
std::vector<std::uint8_t> dx10StateWithSnapshot (const std::vector<std::uint8_t>& state, const std::vector<std::uint8_t>& snapshot,
                                                 bool replace = false);

// true when the channel settings of a state (its snapshot; the plugin's defaults for version 1) leave the sound
// untouched: no envelope/LFO acting and the default "fast LP" filter fully open
bool dx10ProcessorNeutral (const std::vector<std::uint8_t>& state);

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

struct RefOptions
{
    int block = 512;      // host block (split at every event and tick)
    int maxPoly = 0;      // ProcessEvent 1 (0 = unlimited)
    int tick = 0;         // > 0: the host's NewTick at every multiple of `tick` samples, before that sample's events
    int ppq = 96;         // SetTimeSig PPQ
    double tempo = 140.0; // ProcessEvent 0
};

// Renders `notes` from `state` like dxmodel.run(). Interleaved stereo float samples (2·length); empty if the state is
// unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const RefOptions& options = {});
} // namespace dx10
} // namespace lynx
