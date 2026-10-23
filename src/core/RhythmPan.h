#pragma once

// Auto-pan's "Rhythm" voicing (round 136, vox136): a step-pattern auto-panner whose laws were measured black-box on the
// official Soundtoys PanMan trial (hosted through the VST3 API, fed steady tones and her takes, outputs measured;
// nothing decompiled or copied), so that her projects that used it pan the same through LYNX
// (docs/analise/REA_rodada136.md §2).
//
// Measured structure [EXEC]: a pattern of N step values per period (2 steps: left / right; 8 steps: a stepped ramp);
// each step glides linearly from the previous value over `smoothing` of its length (at least ~1.1 ms); the pan
// angle is 45° + offset / 2 + width / 4 · value, clipped to 0 … 90°; equal-power balance gains (left · cos, right ·
// sin: a balance, the channels are not summed). The period is free (Hz, from the start of the song) or a number of
// beats locked to the song position. Analog mode: a low-frequency shelf and an asymmetric soft saturation on each
// input. The oversampler's passband colour is a short causal FIR (the original's 45-sample latency is compensated by
// the host, so this one has none).

#include <array>

namespace lynx::rhythmpan
{
constexpr int maxSteps = 8;

struct Settings
{
    bool sync = false;         // false: rateHz; true: beats per period, locked to the song position
    double rateHz = 1.0, beats = 1.0;
    double width = 180.0;      // degrees (0 … 210)
    double offset = 0.0;       // degrees (−105 … 105)
    double smoothing = 0.0;    // 0 … 1 of a step
    int pattern = 0;           // 0: left / right (2 steps), 1: 8-step ramp
    double amount = 1.0;       // the pattern's values scaled by this
    double phase = 0.0;        // start phase (cycles) at song position 0
    int analog = 1;            // 0 off, 1 the analog input stage (soft saturation), 2 the "hot" stage (a fast compressor)
    double outputDb = 0.0;
    bool operator== (const Settings&) const = default;
};

// the parameter laws (normalised 0 … 1 values)
double rateHz (float v) noexcept;   // 0.01 … 10 Hz (log), the original's Rate law
double beats (float v) noexcept;    // 1/16 … 64 beats (log2)
float beatsValue (double beats) noexcept;
float rateValue (double hz) noexcept;

class Engine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void set (const Settings&) noexcept;
    // `beatPos` = song position in beats at the block's first sample; `bpm` the tempo
    void process (float* left, float* right, int n, double beatPos, double bpm) noexcept;
    float currentPan() const noexcept { return current; } // 0 (left) … 1 (right), for a display / control output

private:
    float analogStage (float x, int c) noexcept;

    double sr = 48000.0;
    Settings s;
    std::array<double, maxSteps> values {};
    int steps = 2;
    double outGain = 1.0;
    // analog LF shelf (first order) and colour FIR, per channel
    double shB0 = 1.0, shB1 = 0.0, shA1 = 0.0;
    std::array<double, 2> shZ {};
    double rmsA = 0.0, attA = 0.0, rms = 0.0, gr = 0.0;
    std::array<std::array<float, 32>, 2> hist {};
    int histPos = 0;
    float current = 0.5f;
};
} // namespace lynx::rhythmpan
