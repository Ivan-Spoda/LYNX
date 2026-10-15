#pragma once

// FL Studio's Kepler rebuilt from its engine (docs/analise/REA_rodada46.md; clean-room: behaviour, layouts and formulas in
// our own words, checked against renders of the installation's own engine DLL). Juno-style polysynth: 8 voices that run
// all the time, each with a band-limited (integrated impulse train) DCO — saw, pulse with PWM, sub square, noise — at
// twice the host rate, a 4-stage saturating ladder low-pass (6…24 dB/oct), envelope or gate VCA and per-voice pan; the
// voice sum is decimated by a 12-coefficient polyphase half-band, then the 1-pole high-pass, the Juno chorus (I, II,
// I+II), the main volume / pan and the output gain. Global LFO1 (shape table, delay ramp, BPM sync), envelope, glide,
// key follow, the voice allocator and the internal arpeggiator follow the engine. Every floating-point step is done in
// float/double in the engine's order, with the engine's rounding mode (toward zero, denormals flushed) during the render.
//
// Installation data read at run time from the user's own engine DLL (never shipped): the LFO shape (2 170 points) and the
// noise table (683 269 points). The DLL must be the analysed build (SHA-256 checked); otherwise the importer keeps the
// synth approximation (synthFromKepler).
//
// Host interface as FL's wrapper: note number = key, MIDI velocity from Init.Vol (41.714·ln(10·Init.Vol + 1), 0…127),
// per-note pan / pitch expressions from FinalLevels (pan law √(1 − p), √p with p = (pan + 1)/2), the X/Y (FCut/FRes)
// modulation routes. Not modelled / differences: the Kepler-2 (Kepler Exo) layout, DCO2, the filter types 1/2, saturation,
// EQ, delay, reverb and hyper chorus (all off in every Kepler-1 state); the chorus noise/gate (off in all FL data); the
// oversampling ×4 flag (0 in all data). The free-running oscillators/LFO pause while LYNX skips an idle channel.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true for a "Kepler-1" state the engine can play (layout parsed, version 1…4)
bool keplerStateUsable (const std::vector<std::uint8_t>& state);

// true when `path` is the analysed build of Kepler's engine_x64.dll (its shape and noise tables can be read)
bool keplerDllUsable (const std::string& path);

std::unique_ptr<Instrument> makeKeplerInstrument();

namespace kepler
{
// One note for the reference renderer (drives the engine like the EXEC harness of REA_rodada46).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0;  // Init.Vol (FL: (11^(1.28·velocity) − 1)/10)
    double finalPan = 0.0; // −1…1
    double pitchCents = 0.0;
    int release = -1;      // sample of the note-off (−1: held)
};

struct RefOptions
{
    double tempo = 125.0; // the wrapper's default tempo (ProcessEvent 0 never sent)
    int block = 512;      // host block (split at every event)
};

// Renders `notes` from `state` with the tables of the DLL at `dll`. Interleaved stereo (2·length); empty if unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::string& dll, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, const RefOptions& options = {});
} // namespace kepler
} // namespace lynx
