#pragma once

// FL Studio's Frequency Shifter as a LYNX mixer effect: the plugin's own engine (Engine_x64.dll next to the plugin),
// rebuilt in docs/analise/REA_rodada154.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line
// code or data in this repository).
//
// Per sample: parameter glides → feedback (the library's tanhf of the last wet sample) → 20 Hz high-pass and 20 kHz
// low-pass (the low-pass always designed for 44.1 kHz, as FL's) → either
//   ring modulator: × cos(2π·phase) (the right channel with the L/R offset), or the side-chain signal, or
//   shifter: (HQ: a 16th-order crossover around the shift frequency that keeps the mirrored band out, re-designed every
//   32 samples, with a 2 ms cross-fade when the shift changes sign; the dry path runs through matching all-pass
//   crossovers) → a 4 + 4 section all-pass Hilbert pair → single side band with the quadrature oscillator, the
//   L / R Shape knobs mixing the upper and lower bands
// → wet / dry (Mix) → Mono↔Stereo cross-mix.
//
// Parameters (EffectKind::flFrequencyShifter) are FL's controls over their range, value = (raw − min) / (max − min):
//   [0] Mix 0…10000   [1] Type 0…2 (HQ shifter, shifter, ring modulator)   [2] Frequency −70000…70000
//   [3] L/R phase offset 0…10000   [4] L shape 0…10000   [5] R shape 0…10000   [6] Feedback 0…10000
//   [7] Mono↔Stereo 0…10000   [8] Range 0…2 (20 kHz, 200 Hz, tempo)   [9] Left phase offset 0…10000
// While the song plays the engine gets the song position (in beats; FL's unit for the tempo range is not confirmed);
// the start of playback restarts the oscillator at the left phase offset, as in FL. The side chain of the ring
// modulator is not routed.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::fsfl
{
constexpr int numParams = 10;
constexpr std::array<int, numParams> rawMin { 0, 0, -70000, 0, 0, 0, 0, 0, 0, 0 };
constexpr std::array<int, numParams> rawMax { 10000, 2, 70000, 10000, 10000, 10000, 10000, 10000, 2, 10000 };

struct State
{
    std::array<int, numParams> raw { 10000, 1, 70000, 0, 10000, 10000, 5000, 10000, 1, 5000 };
};
// FL's state (0xD5): i32 version (≤ 4) + 10 × i32 controls + i32 flags + i32 side-chain track
std::optional<State> parse (const std::vector<std::uint8_t>& state);
void toParams (const State& s, float* params) noexcept;
std::string paramText (int param, float v01);
std::unique_ptr<Effect> make();

// The library's float tanh (feedback path), bit-exact with the engine's
float tanhf_ (float x) noexcept;

class Engine
{
public:
    Engine();
    void setRate (double sampleRate) noexcept;
    void setControl (int index, float value) noexcept; // FL's dispatch 3: the control's target (0…1)
    // one sample in place; tempo in BPM; the song position (beats) of this sample and the next one while the transport
    // plays, −1 when it is stopped (FL's per-sample call)
    void process (float& left, float& right, double tempo, double hostPos = -1.0, double hostNext = -1.0) noexcept;

private:
    struct Biquad // one scalar section: y = ((c3·y1 + c4·y2) + c2·x2) + (c0·x + c1·x1)
    {
        double rate = 44100.0;
        float c[5] {};
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run (float x) noexcept;
    };
    struct Stage4 // four lanes of a section (lanes 0, 1 and 2, 3 share their coefficients)
    {
        double rate = 44100.0;
        float c[5][4] {};
        float y1[4] {}, y2[4] {}, x1[4] {}, x2[4] {};
    };
    struct Bank
    {
        Stage4 st[8];
        int count = 0;
        void design (float fLow, float fHigh) noexcept; // lanes 0, 1 low-pass at fLow, lanes 2, 3 high-pass at fHigh
        void run (float* v) noexcept;                   // 4 lanes in place
    };
    struct Hilbert
    {
        float coef[8][4] {};
        float st[8][4][4] {}; // x1, x2, y1, y2 per section
        float delayed[4] {};
        void run (const float* in, float* a, float* b) noexcept; // a: first chain (one sample late), b: second
    };

    void setParam (int index, float v) noexcept;
    void glide() noexcept;
    float frequencyHz (float x) const noexcept;

    float target[numParams] {}, cur[numParams] {};
    float kSmooth = 0, kFreq = 0, fade = 1.0f, fadeStep = 0, fadeRate = 0, phase0 = 0;
    std::uint8_t restart = 0;
    bool playing = false, hq = false, tempoRange = false, range200 = false;
    int shifter = 0;
    float dry = 0, wet = 0, shapeR1 = 0, shapeL1 = 0, shapeR = 0, shapeL = 0, freqTarget = 0, freq = 0, lrOffset = 0,
          stereo = 1.0f;
    double pos = 0, next = 0, inc = 0, phase = 0, invRateD = 0, rate = 44100.0;
    float invRate = 0, fb = 0;
    int counter = 0;
    float lastL = 0, lastR = 0;
    Hilbert hilbert;
    Bank banks[5]; // 0x390 split, 0x8a0 / 0xdb0 the two halves, 0x12c0 / 0x17d0 the dry path
    Biquad hpL, hpR, lpL, lpR;
};
} // namespace lynx::fsfl
