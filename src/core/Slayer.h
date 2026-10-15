#pragma once

// FL Studio's FL Slayer (guitar and bass synth) rebuilt from its engine (docs/analise/REA_rodada68.md; clean-room:
// behaviour, layouts and formulas in our own words, checked against renders of the installation's own engine DLL).
//
// In FL 2026 the plugin is a 64-bit Delphi front-end ("FL Slayer_x64.dll") that runs the 32-bit engine
// (Slayer_Engine.dll, a VST 2 plugin) in FL's 32-bit bridge process. The front-end turns FL's calls into VST calls:
// parameters as setParameter(i, value/max), notes as MIDI note on/off with deltaFrames 0 (velocity from Init.Vol, key
// from Final.Pitch), parameter 28 as MIDI CC 1, a centred pitch bend after loading; it mixes the engine's stereo output
// with the host's ComputeLRVol(pan 0, vol 1) gains. The engine: six waveguide strings at an internal 44.1 kHz (two
// fractional delay lines with damping, a noise or recorded-pick exciter, slide noise, one or two "coil" pickups with a
// resonant filter), resampled to the host rate with a cubic interpolator, played in six modes (single, chords, power
// chords, strum, mono glide); then the effects block (16 effects: phaser, tremolo, auto-wah, wah, ring modulation,
// shaper, octave, talk box, chorus, flanger, leslie, tremolo pan, delay, reverb), the amp (four types: clean, overdrive,
// distortion, fuzz, with feedback), the 3-band EQ, the cabinet (two biquads) and the volume. Everything runs on the x87
// FPU with 64-bit precision and truncating rounding (the engine sets it in every processReplacing); LYNX reproduces
// that with long double arithmetic under the same control word.
//
// Installation data read at run time from the user's own Slayer_Engine.dll (never shipped): the 15 recorded pick
// excitations, the chord and glide tables. The DLL must be the analysed build (SHA-256 checked); otherwise the importer
// keeps the channel on the generic fallback. The channel keeps the plugin's raw state (record 0xD5: version, 28 or 29
// parameter integers) in ChannelDef::pluginState and the DLL's path in ChannelDef::samplePath.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true for an FL Slayer state the engine can read (version 0: 28 parameters, version 1: 29)
bool slayerStateUsable (const std::vector<std::uint8_t>& state);

// true when `path` is the analysed build of FL Slayer's Slayer_Engine.dll
bool slayerDllUsable (const std::string& path);

// The 29 parameters of a state as integers (control ranges: see slayerParamMax); empty if unusable
std::vector<int> slayerStateParams (const std::vector<std::uint8_t>& state);

// the front-end's control maxima (parameter i is sent to the engine as value / max)
int slayerParamMax (int index) noexcept;

std::unique_ptr<Instrument> makeSlayerInstrument();

namespace slayer
{
// One note for the reference renderer (drives the engine like the EXEC harness of REA_rodada68: every event is
// delivered with the next engine call).
struct RefNote
{
    int start = 0, key = 60, velocity = 100; // MIDI velocity as the front-end computes it
    int release = -1;                        // sample of the note-off (−1: held)
};

struct RefOptions
{
    int block = 512;     // host block (split at every event)
    double tempo = 120.0; // host tempo (VST time info)
    std::vector<int> peek; // engine fields (original byte offsets) printed after every block (verification only)
    std::vector<std::array<int, 4>> midi; // extra MIDI events: sample, status, data 1, data 2
};

// the MIDI velocity the front-end sends for a voice's Init.Vol (TriggerVoice: Round(127·ln(10·v + 1)/ln 21.551182), ≤ 127)
int midiVelocity (double initVol) noexcept;

// Renders `notes` with the 29 parameter integers of a state (front-end order) and the DLL at `dll`. Raw engine output
// (no host gains), interleaved stereo (2·length); empty if unusable.
std::vector<float> renderReference (const std::vector<int>& params, const std::string& dll, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, const RefOptions& options = {});
} // namespace slayer
} // namespace lynx
