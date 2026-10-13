#pragma once

// State (0xD5) of simple FL synthesizers approximated by the built-in 3-oscillator synth.
// Formats and formulas in docs/analise/REA_rodada10.md (clean-room).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Harmless: oscillators (timbre, odd mask, sub, unison), volume envelope, filter with
// envelope, LFO, mono/porta. `gain` receives Harmless's master volume (linear). `bpm` converts
// times in seconds for the tempo-synced parameters.
bool synthFromHarmless (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Fruit Kick: sine with a pitch envelope and amplitude decay.
bool synthFromFruitKick (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// MiniSynth (from FL Studio Mobile; "SSLF" container with 43 floats): oscillator type, ladder
// filter, envelopes, LFO, mono.
bool synthFromMiniSynth (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth);

// Fruity DX10 (2-operator FM): sine carrier on osc 1, modulator on osc 3 (the synth's FM) with the
// index falling from Init to Sustain; "Mod Thru" sounds on osc 2.
bool synthFromDX10 (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Sytrus (6 FM/RM operators + 3 filters, zlib state; REA_rodada11): the 3 strongest carriers
// become the oscillators, strong FM becomes a richer wave + filter envelope, the first active filter
// becomes the synth's filter. `gain` receives the Master level (linear).
bool synthFromSytrus (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Fruity Envelope Controller (REA_rodada15 §10): 8 articulators; output k is articulator k, which
// follows the channel's last note within its key range. Up to 64 points per envelope.
struct ControllerArticulator
{
    bool bipolar = false, envOn = false, tempo = false; // tempo: beats (quarter notes) instead of seconds
    float base = 0.5f, envLevel = 1.0f;
    int sustain = -1, lowKey = 0, highKey = 256;
    int count = 0;
    std::array<float, 64> time {}, level {}; // accumulated time of each point and level 0..1
};
using EnvelopeControllerData = std::array<ControllerArticulator, 8>;
std::optional<EnvelopeControllerData> parseEnvelopeController (const std::vector<std::uint8_t>& state);

// Fruity Keyboard Controller (REA_rodada16 §1.4): value of each key (0..131) and the note-off value
// (with auto release). Its smoothing (attack/release 0..256) has no known time scale and is left out.
struct KeyboardControllerData
{
    std::array<float, 132> keys {};
    float noteOffValue = 0.0f;
    bool autoRelease = false;
};
std::optional<KeyboardControllerData> parseKeyboardController (const std::vector<std::uint8_t>& state);

// Fruity Love Philter (REA_rodada16 §2): zlib state with 6 globals, Mod X/Y and 8 filter units, each
// with 12 parameters, mode (stages), type, flags and 8 articulators (pan, volume, cutoff, resonance,
// low, band, high, waveshaper) with the X/Y maps.
struct LovePhilterMap
{
    std::vector<std::pair<double, double>> points; // (Δx, y)
};
// The combination mode is not stored: it is fixed per target (REA_rodada18 §4). Pan, cutoff and resonance
// add (base 0); the others multiply (base 1). The f32 in the file is the base.
struct LovePhilterArticulator
{
    float base = 1.0f;
    float envDepth = 0.0f, lfoDepth = 0.0f;
    LovePhilterMap mapX, mapY;

    // envelope of the selected pattern (REA_rodada20 §1.2/§1.3): Δt in beats with `tempo`, curve of the destination point
    struct Point
    {
        double dt, y;
        float tension;
        int curve;
    };
    struct
    {
        bool on = false, tempo = false;
        std::vector<Point> points;
    } pattern;

    // LFO (§1.5): period 625/(10001^(speed/65536) − 1) beats; output amplitude·wave, amplitude = y − 0.5
    struct
    {
        bool on = false, tempo = false, frozen = false;
        float amplitude = 0.5f;
        std::int32_t speed = 0, skew = 0, width = 0;
        std::uint32_t phase = 0; // offset in 2^−32 cycles
    } lfo;
};
struct LovePhilterUnit
{
    std::array<std::int32_t, 12> params {};
    int mode = 0, type = 0;
    bool alt = false, enabled = false, waveshaper = false;
    std::array<LovePhilterArticulator, 8> articulators {};
};
struct LovePhilterState
{
    int version = 0;
    std::array<std::int32_t, 6> globals {};
    std::int32_t modX = 512, modY = 512;
    int soloUnit = -1; // −1 = none
    std::array<LovePhilterUnit, 8> units {};
};
std::optional<LovePhilterState> parseLovePhilter (const std::vector<std::uint8_t>& state);

// value of an X/Y map at x (0..1): straight segments between the points (tension is not applied)
double lovePhilterMapAt (const LovePhilterMap& map, double x) noexcept;

// Value (0..1) of the articulator `sinceOn` after the trigger; `sinceRelease` < 0 = key still held.
// Times in the articulator's unit (seconds, or beats with `tempo`).
float controllerValue (const ControllerArticulator& a, double sinceOn, double sinceRelease) noexcept;

// Harmor (REA_rodada11 §4), rough approximation: each part (A/B) becomes a saw with the frequency
// ratio, filter 1 (or 2) with the envelope, volume envelope, unison, mono/porta.
bool synthFromHarmor (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// GMS (REA_rodada12 §1): "SSLF" + .gmsynth preset; 3 wavetable oscillators, cascaded mixing,
// AM/sync, unison, ladder, 2 AD envelopes and 2 LFOs. `gain` = Mix Level × Amp Level.
bool synthFromGms (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Ogun / Autogun (§2), rough approximation: shape from "fullness", timbre decay/release,
// unison and the filter. The additive timbre and the articulators are left out.
bool synthFromOgun (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// Sakura (§4): approximated string (saw + triangle, Hi-Cut, ADSR). Scales [HIP].
bool synthFromSakura (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Fruity DrumSynth Live (§3): name of the selected sound (e.g. "909 Hat O") and paths (with
// %FLStudioData%) of FL drum samples that replace it, in order of preference.
std::optional<std::string> drumSynthLiveSound (const std::vector<std::uint8_t>& state);

// Kepler (REA_rodada13 §2, "Kepler-1" mode): saw/pulse/sub/noise, VCF with Env1, LFO, chorus.
bool synthFromKepler (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// Wave Traveller (§4): the sample (path with FL's variables) and the root note; scratching is left out.
std::optional<std::pair<std::string, int>> waveTravellerSample (const std::vector<std::uint8_t>& state);

// FLEX (§1.1): pack and preset names, from the plugin state. FLEX packs are protected and
// LYNX does not open them: the channel gets a synth patch chosen by those names.
std::optional<std::pair<std::string, std::string>> flexPresetName (const std::vector<std::uint8_t>& state);

// PoiZone, BassDrum, Toxic Biohazard (REA_rodada15 §12): PoiZone maps well (2 osc + noise, 2 ADSR,
// filter, LFO); BassDrum becomes a kick (sine with pitch drop + noise); Toxic, 2-operator FM.
bool synthFromPoiZone (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);
bool synthFromBassDrum (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);
bool synthFromToxic (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// Morphine (REA_rodada16 §3): each additive generator that produces sound becomes an oscillator (shape from
// the average spectrum, level from Mix); envelope, level and tuning from the master block.
bool synthFromMorphine (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// BooBass (REA_rodada10 §5): saw + low sine, cutoff from Treble.
bool synthFromBooBass (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Drumpad (§6): patch name ("War Drum Lo 01"); `drumToneForName` gives a generic drum by name
// (kicks/snares/hi-hats use LYNX's drum instruments).
std::optional<std::string> drumpadPatchName (const std::vector<std::uint8_t>& state);
SynthSettings drumToneForName (const std::string& patchName);
// Drumpad: the 38 state parameters (2 Mallet Amplitude, 29 Pitch, …; REA_rodada18 §12)
std::optional<std::array<float, 38>> drumpadParams (const std::vector<std::uint8_t>& state);
std::vector<std::string> drumSampleCandidates (const std::string& soundName);
} // namespace lynx
