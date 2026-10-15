#pragma once

// FL Studio's Plucked! rebuilt as the engine decoded in docs/analise/REA_rodada65.md §1 (clean-room; a 1:1 port of our
// own round-65 reference model r65/py/plmodel.py, which renders bit-identically to the installation's Plucked!_x64.dll
// for the same random seed).
//
// Per note: a Karplus-Strong string. The delay line holds round(fs / (523.2511306·2^((c − 1200)/1200))) samples
// (c = the note's Final.Pitch in cents, folded into −3600…3599), excited with a half-positive / half-negative square
// of height (1 − colour)/2 plus white noise of depth colour (Delphi's Random); every sample reads the line and writes
// back g·(y + previous y), g from the decay knob (normalised per pitch when "Normalize" is on). "Widen" runs a second,
// independently excited line for the right channel. "Gate" makes note-offs silence the voice; otherwise a note rings
// until its level stays below 0.004 for a whole 256-sample stretch. Level and pan reach the voice through FL's host pan
// law (Final.Vol, Final.Pan), Mod X (Final.FCut) adds to the decay knob and Mod Y (Final.FRes) to the colour knob.
//
// The channel keeps the plugin's raw saved state (record 0xD5: 5 × int32 decay, colour, normalize, gate, widen) in
// ChannelDef::pluginState.
//
// Differences from FL by design / not confirmed (REA_rodada65 §1.7): the random seed is per instrument and starts at
// Delphi's default (FL seeds its process-global generator from the clock, so every FL render differs); FL's host gain
// ramp on level changes is not decoded (LYNX ramps linearly over the block); FL never hands Plucked! a channel sample
// in LYNX (the plugin can excite the string with one — not modelled).

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this Plucked! state (20 bytes or more)
bool pluckedStateUsable (const std::vector<std::uint8_t>& state);

// the five knob values (decay 0…256, colour 0…128, normalize, gate, widen as 0/1) of a state
std::optional<std::vector<int>> pluckedParams (const std::vector<std::uint8_t>& state);

std::unique_ptr<Instrument> makePluckedInstrument();

namespace plucked
{
struct RefNote
{
    int start = 0, key = 60;
    double finalVol = 1.0, finalPan = 0.0, pitchOffset = 0.0, modX = 0.0, modY = 0.0; // Final.Vol/Pan, cents, FCut, FRes
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like r65/py/plcmp.py drives the DLL: host blocks of `block` samples split at every event, host pan law A
// (x87 body, as FL's host on this machine, REA_rodada47 §2.2) or plain gains (L = R = Final.Vol) with panLawA = false,
// Delphi RandSeed `seed` when the first note starts. Interleaved stereo float samples (2·length); empty if the state is
// unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, std::uint32_t seed = 0, bool panLawA = true, int maxPoly = 0);
} // namespace plucked
} // namespace lynx
