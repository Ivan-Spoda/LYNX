#pragma once

// FL Studio's FL Keys rebuilt as decoded in docs/analise/REA_rodada69.md §7 (clean-room; a port of our own round-69
// reference model r69/keys/model/fkm.c, which renders bit-identically to the installation's FL Keys_x64.dll on the
// same wavetable data).
//
// FL Keys plays a "wavetable" (.wti, an INI file next to the plugin): keygroups of 16-bit samples, either cut from one
// raw file (mda Piano, Rhodes) or one WAV file each (Grand Piano, Roto Organ). Per note: the keygroup covering the
// (offset) note and the velocity layer, read with 16.16 fixed-point linear interpolation, an exponential envelope
// (decay while held, release after the note-off), a soft overdrive, a one-pole "muffle" low-pass and fixed per-note
// stereo gains. All voices then pass a treble shelf, a tremolo/auto-pan LFO and a 128-sample "stereo" delay.
// Level and pan reach the voices through FL's host pan law with Final.Vol raised to the velocity-sensitivity power.
//
// The channel keeps the plugin's saved state (record 0xD5: u32 1, the wavetable name, 15 × int32 knobs) in
// ChannelDef::pluginState, the wavetable's files in ChannelDef::zones (in the order FL reads them) and the wavetable
// description in ChannelDef::modelParams (see FlKeysTable).
//
// Not reproduced (REA_rodada69 §7.6): wavetables with "resample" (Roto Organ) are not resampled to the host rate when
// it differs from 44.1 kHz, as FL does; Ogg-compressed samples (Grand Piano) depend on the decoder (LYNX's libvorbis
// rounds 0.01 % of the values to the other neighbour); FL counts its tremolo LFO, its denormal flush and the end of a
// voice per host block, LYNX per 512-sample block.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
namespace flkeys
{
// --------------------------------------------------------------------------------------------- saved state (0xD5)
struct State
{
    std::string wavetable;           // e.g. "Grand Piano"
    std::array<int, 15> knobs {};    // raw knob values (ranges in knobRange)
};
std::optional<State> parseState (const std::vector<std::uint8_t>& state);
std::vector<std::uint8_t> writeState (const State& s);

// knob ranges (EXEC, the plugin's wheel controls): decay, release, hardness, velocity→hardness, muffle,
// velocity→muffle, velocity sensitivity, stereo, tune, detune, stretch, treble, pan/tremolo, LFO rate, overdrive
struct KnobRange { int min, max; const char* name; };
extern const KnobRange knobRange[15];

// --------------------------------------------------------------------------------------------- wavetable description
// What FL's loader reads from the .wti (FUN_00713920), stored flat in ChannelDef::modelParams.
struct WtKeygroup
{
    int root = 60, high = 999;
    int pos = 0, end = 0, loop = 0;     // as written in the file (riff: relative to the keygroup's file)
    bool posSet = false, endSet = false, loopSet = false;
    bool canLoop = true;
    int file = -1;                      // riff: index of the file this keygroup opens (-1: none, reuses the previous)
    int duplicateOf = -1;               // riff: earlier keygroup with the same file name (FL copies its record)
};

struct WtFile
{
    int loopStart = -1, loopEnd = -1;   // the WAV's own loop (smpl), −1 = none
    float scale = 32768.0f;             // float → 16-bit conversion of the decoded file (Ogg: 32767)
};

struct FlKeysTable
{
    bool raw = false;                   // filetype=raw: every keygroup is cut from one file (the first zone)
    bool resample = true;
    float sampleRate = 44100.0f, velocityFactor = 1.0f;
    bool releaseHighNotes = false;
    std::vector<int> thresholds;        // velocity layers (thresholds[0] = 0)
    std::vector<WtKeygroup> keygroups;
    std::vector<WtFile> files;          // riff: one per zone; raw: one (the raw file as a 16-bit mono WAV)

    std::vector<float> encode() const;
    static std::optional<FlKeysTable> decode (const std::vector<float>& params);
};

// Parses a .wti (INI text). `fileNames` receives the keygroup files (riff: relative to the .wti folder; raw: the
// sample file), in the order FL loads them.
std::optional<FlKeysTable> parseWti (const std::string& text, std::vector<std::string>& fileNames);

// A keygroup WAV's loop (smpl chunk, first loop) and the conversion its decoded samples need: 32767 for FL's
// Ogg-compressed WAVs (format 0x674F…0x6751, REA_rodada69 §7.3), 32768 for PCM.
WtFile wavFileInfo (const std::vector<std::uint8_t>& wav);

// Fills an imported channel (FlpImport): `state` is the 0xD5 record, `wavetables` the FL Keys\Wavtables folder.
// `readFile` reads a whole file (empty: missing), `existing` maps a path to the file to use (empty: missing) and
// `saveEmbedded` stores a WAV made here (the raw sample file) and returns its path. False: leave the channel as it is.
bool importChannel (ChannelDef& def, const std::vector<std::uint8_t>& state, const std::string& wavetables,
                    const std::function<std::vector<std::uint8_t> (const std::string&)>& readFile,
                    const std::function<std::string (const std::string&)>& existing,
                    const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveEmbedded);

// --------------------------------------------------------------------------------------------- loaded wavetable
struct Keygroup { int root, high, pos, end, loop; };

struct Wavetable
{
    std::vector<Keygroup> keygroups;
    std::vector<int> thresholds;
    bool stereo = false, releaseHighNotes = false;
    float sampleRate = 44100.0f, velocityFactor = 1.0f;
    std::vector<std::int16_t> data;     // interleaved when stereo, with guard frames
};

// Decoded file: 16-bit samples (interleaved when stereo).
struct FileSamples
{
    std::vector<std::int16_t> samples;
    int channels = 1;
};

// Builds the data FL Keys plays (FUN_00713920, FUN_007160b0, FUN_00716470): files[i] matches table.files[i].
std::shared_ptr<const Wavetable> buildWavetable (const FlKeysTable& table, const std::vector<FileSamples>& files);

// float SampleData → FL's 16-bit samples (round to nearest, clamped)
FileSamples toFileSamples (const SampleData& sample, float scale);

// --------------------------------------------------------------------------------------------- instrument
// `wavetable` may be null (silent). The state's knobs are read in configure().
std::unique_ptr<Instrument> makeFlKeysInstrument (std::shared_ptr<const Wavetable> wavetable);

struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, cents = 0.0;
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like r69/keys/kcmp.py drives the DLL: host blocks of `block` samples split at every event, host pan law A
// (x87, as FL's host on this machine) or plain gains (L = R = gain) with panLawA = false. Interleaved stereo.
std::vector<float> renderReference (const Wavetable& wavetable, const State& state, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, int block = 512, bool panLawA = false);
} // namespace flkeys
} // namespace lynx
