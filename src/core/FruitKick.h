#pragma once

// FL Studio's Fruit Kick rebuilt as decoded in docs/analise/REA_rodada43.md §1 (clean-room; a 1:1 port of our own
// round-43 reference model r43/py/fkmodel.py, which reproduces renders of the installation's Fruit Kick_x64.dll
// bit for bit except during the 10 ms release fade, where FL uses IPP's 11-bit cosine).
//
// Voice: the host's 16384-point sine table read with a 32-bit phase (start phase = Click << 24), a per-sample
// multiplicative pitch fall from Max freq (+ half the note offset) down to the Min freq floor and an amplitude that
// starts at 1.5 and falls by a per-sample factor until it is below 0.0002; Distortion runs FL's host waveshaper (curve
// set 0, curve 10, interpolated). Around it, FL's plugin voice framework: five channel-envelope/LFO targets stepped
// once per sequencer tick (pan LFO, volume envelope × LFO; cutoff/resonance/pitch have no effect on this plugin), the
// sin/cos pan law on (Final.Pan, Final.Vol), level ramps of at most 0.001 per sample, and a 10 ms raised-cosine fade
// when a note is released without a volume envelope (or when the volume envelope ends).
//
// The channel keeps the plugin's raw saved state (record 0xD5) in ChannelDef::pluginState. Version-1 states carry the
// envelope/filter snapshot the plugin uses; for version-0 states the importer appends the channel's own 0xDA/0xDB
// records as that snapshot (FL hands them to the plugin when it has none, REA_rodada43 §1.6 — inference).
//
// Host side as FL's: link velocity on, so Final.Vol = Init.Vol × channel volume with Init.Vol = (11^(1.28·v) − 1)/10;
// Final.Pan = note pan + channel pan; Final.Pitch = 100·(key − 60) + channel pitch + note cents. LYNX's channel gains
// are turned back into those values and applied once, inside the engine. Ticks run at 96 PPQ of the song tempo from
// the instrument's start (FL's tick grid follows the song position: not reproduced); the channel filter of the
// framework is not modelled (every known state has it neutral).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the engine can read this Fruit Kick state (version 0 or 1, the six parameters present)
bool fruitKickStateUsable (const std::vector<std::uint8_t>& state);

// A version-0 state turned into a version-1 one whose snapshot holds the five 68-byte channel envelope records (0xDA,
// Pan, Vol, Cutoff, Res, Pitch) and the filter cutoff/resonance/type (0xDB); other states are returned unchanged.
std::vector<std::uint8_t> fruitKickWithChannelEnvelope (const std::vector<std::uint8_t>& state,
                                                        const std::vector<std::vector<std::uint8_t>>& envelopeRecords,
                                                        int cutoff, int resonance, int filterType);

// what this state uses that is not modelled exactly (empty: nothing), for reports
std::vector<std::string> fruitKickFallbacks (const std::vector<std::uint8_t>& state);

// raw integer parameter 0…5 (Max freq, Min freq, Freq decay, Amp decay, Click, Distortion), 0 if absent
int fruitKickParam (const std::vector<std::uint8_t>& state, int index);
bool fruitKickSetParam (std::vector<std::uint8_t>& state, int index, int value);

std::unique_ptr<Instrument> makeFruitKickInstrument();

namespace fruitkick
{
struct RefNote
{
    int start = 0, key = 60;
    double finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Final.Vol, Final.Pan, offset added to 100·(key − 60)
    int release = -1;                                        // sample of the note-off (−1: held)
};

// Renders like fkmodel.run: host blocks of `block` samples split at every note and tick event; the plugin's tick
// callback before the block that starts at each sample in `ticks`; tempo/PPQ for the envelope time conversion.
// Interleaved stereo (2·length floats); empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, const std::vector<int>& ticks = {}, double tempo = 140.0,
                                    int ppq = 96);
} // namespace fruitkick
} // namespace lynx
