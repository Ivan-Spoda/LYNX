#pragma once

// FL Studio's Harmor rebuilt as the additive engine decoded in docs/analise/REA_rodada34.md (clean-room; a 1:1 port of
// our own reference model r34/py/hmodel.py, which was checked against renders of the installation's DLL). Every
// enabled part (A, B) × unison voice is a voice record with its own bank of up to 520 sine partials (self-oscillation,
// Sub 1/3/4, 516 harmonics); every control block (2 ms) the record rebuilds its target amplitudes from the two timbres
// through the part's user-ordered module chain (local EQ, prism, harmonizer, filters + resonance, phaser, pluck, blur,
// protection, clip, volume), then the subs-below fade, self-oscillation, top fade, global EQ and auto-gain, with
// Harmor's 43 articulators per part (point-curve envelopes, LFOs, key/velocity/X/Y/Z/random/unison maps), unison
// (classic/blurred/uniform/random/Hz), legato/portamento/mono, stereo tremolo and the master volume.
//
// The channel keeps the plugin's raw saved state (record 0xD5, as FL wrote it) in ChannelDef::pluginState.
//
// Fallbacks (not decoded in round 34; harmorFallbacks() lists the ones a state needs): image/resynthesis data is
// ignored (the part plays its timbres), arpeggio envelope points act as normal points, the noise resonance (type 109)
// is skipped, the blur variant of misc +0x46 runs as the normal blur, phaser/harmonizer mask curves and timbre blend
// modes ≠ 0 are ignored, non-neutral global envelope scales are ignored (the global attack skip still runs), unknown
// filter/resonance/phaser shapes and clip modes > 4 switch that stage off, the harmonic-randomness module is absent
// (never active in FL's data).
//
// Effects rack (REA_rodada38): distortion, chorus, delay, reverb and the Maximus compressor in the state's unit order
// (core/HarmlessFx.h, hlfx::HarmorRack), plugged into the engine's `harmor::FxHook` slot; the compressor needs Harmor's
// own preset System\Compression\<type>.fst from the user's FL installation (FlpImport stores its Maximus state in
// ChannelDef::modelParams: size, then the bytes).
//
// Random draws: the plugin's Mersenne Twister is seeded with a fixed value per instance (FL seeds it from the clock).
// Polyphony: a pool of 6 notes' worth of records (16…96, sized when the instrument is prepared); the oldest are stolen.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can play this Harmor state (version ≥ 16 with articulators and curves, parsed fully)
bool harmorStateUsable (const std::vector<std::uint8_t>& state);

// the round-34 fallbacks this state relies on (empty: everything it uses is modelled), for logs and reports
std::vector<std::string> harmorFallbacks (const std::vector<std::uint8_t>& state);

// Byte offset of a raw integer parameter inside the state (part 0/1: part parameter 0…385; part −1: global 0…52, then
// 53…58 = the effects rack's unit order),
// or −1 when the state doesn't hold it. Used by the channel settings panel to edit the state in place.
int harmorParamOffset (const std::vector<std::uint8_t>& state, int part, int index);

// the rack compressor type of a state (global misc +52; 0 = off, n = Harmor's System\Compression\<n>.fst)
int harmorCompressorType (const std::vector<std::uint8_t>& state);

std::unique_ptr<Instrument> makeHarmorInstrument();

namespace harmor
{
// The effects rack's slot: every host block, before the master volume (where FL runs its rack), with the records'
// dry mix (left/right, processed in place) and their FX-send mix (fxL/fxR, which joins the chain at the rack's unit 4);
// voices: records existed in this block. LYNX plugs Harmor's rack (hlfx::HarmorRack) in here.
struct FxHook
{
    virtual ~FxHook() = default;
    virtual bool enabled() const noexcept = 0;
    virtual bool quiet() const noexcept = 0;
    virtual void process (float* left, float* right, const float* fxL, const float* fxR, int n, bool voices) noexcept = 0;
    virtual void advance (int n) noexcept = 0;
};

// One note for the reference renderer (the verification harness drives the engine exactly like our Python model).
struct RefNote
{
    int start = 0, key = 60;
    double velocity = 1.0, finalVol = 1.0, finalPan = 0.0;
    int release = -1; // sample of the note-off (−1: held)
};

// Renders `notes` from `state` like hmodel.render (host blocks of `block` samples split at every event) with the
// state's effects rack (compressorState: the Maximus state of Harmor's System\Compression\<type>.fst; empty = the
// compressor stays off). `mt` (625 words: index, then the 624-word state) seeds the random draws like the DLL's
// Mersenne Twister; empty = LYNX's fixed per-instance seed. Returns interleaved stereo float samples (2·length),
// empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, const std::vector<std::uint32_t>& mt = {}, double bpm = 120.0,
                                    const std::vector<std::uint8_t>& compressorState = {});
} // namespace harmor
} // namespace lynx
