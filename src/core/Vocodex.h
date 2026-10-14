#pragma once

// FL Studio's Vocodex, rebuilt from the behaviour decoded in docs/analise/REA_rodada35.md (clean-room; the reference
// model matches the installation's engine to −137 dB or better on every demo instance). The effect keeps FL's raw
// state (EffectSlot::state) and builds the whole engine from it: band layout and per-band maps, 64-bit band-pass
// filter banks with 2× / octave-level multirate, PEAK/RMS followers, gate, saturation, smoother, unison, carrier
// noise, consonant/carrier contour, pass-through and the gain ramps. Draft re-blocks into 16-frame chunks (16 samples
// of latency, reported to PDC). The Soundgoodizer stage (SG > 0) is LYNX's Soundgoodizer approximation
// (compressor + the presets' −0.2 dB master limiter, whose look-ahead adds to the latency).

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::vocodex
{
// One of the 13 maps: points (x accumulated from the stored deltas, y) with straight segments
struct Map
{
    std::vector<double> x, y;
};

enum Param { wet, sg, modVol, modHp, carVol, carLp, carNoise, carNoiseHp, hold, attack, attackCurve, release, releaseCurve,
             freeze, bandWidth, flatness, modBandWidth, modPitch, unisonPitch, unisonPan, paramCount };

// The saved state (REA_rodada35 §2.1, REA_rodada20 §4.2.1)
struct State
{
    int version = 8;
    std::array<Map, 13> maps;      // 0 gain, 1 pan, 2 gain offset, 3 gate, 4 mod pass, 5/6/7 hold/att/rel, 8 band
                                   // distribution, 9 band width, 10 pitch, 11 saturation mix, 12 saturation curve
    std::array<int, paramCount> params {};
    int holdMode = 0;              // 0 PEAK, 1 RMS
    int order = 2;                 // filter order K (1…4)
    int unison = 0;                // extra analysis voices U (0…4)
    int bands = 47;                // 5…100
    int draftField = 1;            // 0 = Draft button on
    int modIn = 0, carIn = 0;      // 0 = own input, k = k-th side input, CarIn < 0 = internal synth
    bool lrEncoding = false, minTimes = true, toMod = false, threaded = true, subtractiveGate = true, toNoise = false,
         fromCar = false, fromConsonants = false;
    std::array<std::uint8_t, 13> mapAxis {}; // per map: 0 = read at the band frequency, 1 = at the pitch-shifted one
    std::string tone;              // internal carrier synth preset
    std::vector<int> notes;        // notes latched on the internal carrier's keyboard

    bool draft() const noexcept { return draftField == 0; }
    bool internalCarrier() const noexcept { return carIn < 0 && ! lrEncoding; }
};

std::optional<State> parseState (const std::vector<std::uint8_t>& bytes);

// The vocoder engine. Signals are interleaved stereo; `process` handles at most `maxBlock` frames per call.
class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // Builds bands, filters, followers and buffers (allocates). Gains start ramping from 0, as when FL creates it.
    void prepare (const State& state, double sampleRate, int maxBlock, bool draft);
    // FL's Flush: clears every filter / follower / FIFO and the noise seed; the gains snap to their targets
    void flush() noexcept;
    void process (const float* own, const float* mod, const float* car, float* out, int n) noexcept;

    int latencySamples() const noexcept;
    int maxBlock() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Source of the internal carrier (CarIn < 0): renders interleaved stereo into `out`.
class Carrier
{
public:
    virtual ~Carrier() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void render (float* out, int n) noexcept = 0;
};

// The internal carrier synth (a Sytrus with the preset `<tone>.fst` and the state's notes held at volume 0.5,
// REA_rodada35 §8). LYNX has no instrument for it yet: returns nullptr, and the effect then passes the modulator.
std::unique_ptr<Carrier> makeInternalCarrier (const State& state);

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& state);
} // namespace lynx::vocodex
