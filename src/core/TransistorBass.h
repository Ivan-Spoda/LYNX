#pragma once

// FL Studio's Transistor Bass rebuilt from its engine (docs/analise/REA_rodada57.md; clean-room: behaviour, layouts and
// formulas in our own words, checked against renders of the installation's own engine DLL). A TB-303-style monophonic
// bass: band-limited saw/square oscillator (or the "303 Pulse" recorded oscillator cycles), 4-pole diode-ladder low-pass,
// accent/decay envelopes, VCA, high-pass, all at 4× the host rate with two half-band decimators; then the Rat-style
// distortion, the stereo delay and the plate reverb; the 128 programs (patch + 16-step sequence + transpose) and the
// built-in step sequencer driven by host notes and tempo.
//
// Installation data read at run time from the user's own engine DLL (never shipped): the 57 recorded oscillator cycles of
// the "303 Pulse" mode. The DLL must be the analysed build (SHA-256 checked); otherwise the importer reports the plugin as
// unsupported.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true for a Transistor Bass state the engine can read (wrapper version 1, engine stream version 1…5)
bool transistorBassStateUsable (const std::vector<std::uint8_t>& state);

// true when `path` is the analysed build of Transistor Bass's engine_x64.dll
bool transistorBassDllUsable (const std::string& path);

std::unique_ptr<Instrument> makeTransistorBassInstrument();

namespace tbass
{
// One note for the reference renderer (drives the engine like the EXEC harness of REA_rodada57).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0;  // Init.Vol (FL: (11^(1.28·velocity) − 1)/10)
    double finalPan = 0.0; // −1…1
    double pitchCents = 0.0;
    double fcut = 0.0, fres = 0.0; // Final.FCut / Final.FRes (the X/Y expressions)
    int release = -1;      // sample of the note-off (−1: held)
    int channel = 0;       // host "colour" answer (MIDI channel; 12 = sequence transpose)
};

struct RefOptions
{
    double tempo = 0.0; // ProcessEvent 0 (≤ 0: never sent)
    int block = 512;    // host block (split at every event)
};

// Renders `notes` from `state` with the recorded cycles of the DLL at `dll`. Interleaved stereo (2·length); empty if
// unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::string& dll, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, const RefOptions& options = {});
} // namespace tbass
} // namespace lynx
