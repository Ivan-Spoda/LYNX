#pragma once

// Ridge Strip's "Vocal strip" voicing (round 145, vox145): a vocal channel strip — de-esser, de-ploser, a three-band
// EQ (high-pass, narrow notch / peak, wide high band) and a compander (downward expander → compressor → make-up →
// optional drive) in any of the 24 processing orders — whose structure and constants were measured black-box on the
// official SSL Native Vocalstrip 2 trial (Complete Access; hosted through the public VST3 API, fed tones, bursts,
// impulses and her takes, outputs measured; nothing decompiled or copied), so that projects that used it sound the
// same through LYNX (docs/analise/REA_rodada145.md §2). LYNX's own code.
//
// Measured structure [EXEC]:
//  - compressor: a peak detector on the louder channel (one-pole branches exp(−1 / (t · fs)) for attack and
//    release, linear domain), hard knee or a 11.75 dB quadratic soft knee, gain in dB; make-up is plain gain;
//  - expander (before the compressor): its own fast peak detector (0.1 ms / 25 ms), slope 5 dB per dB (1:6) below
//    the threshold + 1.16 dB;
//  - drive: y = 1.5 u − u³ / 8 for |u| ≤ 2, ±2 beyond (odd harmonics only, +3.5 dB small-signal gain), after the
//    make-up;
//  - de-esser / de-ploser: level-independent detectors — the level of the band (3rd-order Butterworth high-pass at
//    the frequency; the de-ploser: a low-pass at 0.78 … 0.92 × the frequency with a passband lift) relative to the
//    full signal, both peak followers (de-esser 1.5 / 40 ms, de-ploser 3 / 40 ms); gain reduction 2/3 (de-esser) and
//    3/4 (de-ploser) of the excess over the threshold; SPLIT: a 3rd-order Butterworth crossover, only the band's
//    side turned down (LP + g · HP / HP + g · LP);
//    look-ahead delays the audio 1 ms / 10 ms;
//  - EQ: high-pass = cookbook 2nd-order, Q 1 (a small bump above the corner); notch and high band = peaking filters
//    with the analog prototype's magnitude (magnitude-matched digital design) and a Q that follows the gain
//    (notch: a measured table; high band: 0.852 · e^(−0.0561 |dB|)).

#include <array>

#include "core/Biquad.h"

namespace lynx::voxstrip
{
// the original's control laws (normalised 0 … 1 values as its parameters use them)
double dsFreqHz (float v) noexcept;      // 1 … 20 kHz (log)
double dpFreqHz (float v) noexcept;      // 20 … 250 Hz (log)
double thresholdDb (float v) noexcept;   // −48 … 0 dB (de-esser, de-ploser, compressor)
double hpfHz (float v) noexcept;         // 30 … 300 Hz (log)
double notchHz (float v) noexcept;       // 200 Hz … 10 kHz (log)
double notchGainDb (float v) noexcept;   // −36 … +12 dB
double highHz (float v) noexcept;        // 1 … 20 kHz (log)
double highGainDb (float v) noexcept;    // ±12 dB
double expThresholdDb (float v) noexcept; // −96 … 0 dB
double attackMs (float v) noexcept;      // 0.1 … 100 ms
double releaseMs (float v) noexcept;     // 0.5 … 2000 ms
double ratio (float v) noexcept;         // 1 … 50 (1 + 49 (250^v − 1) / 249)
double makeupDb (float v) noexcept;      // 0 … 48 dB
double gainDb (float v) noexcept;        // −36 … +12 dB (input / output)
int orderOf (float v) noexcept;          // 0 … 23

// inverses (the state translator)
float dsFreqValue (double hz) noexcept;
float dpFreqValue (double hz) noexcept;
float thresholdValue (double db) noexcept;
float hpfValue (double hz) noexcept;
float notchValue (double hz) noexcept;
float notchGainValue (double db) noexcept;
float highValue (double hz) noexcept;
float highGainValue (double db) noexcept;
float expThresholdValue (double db) noexcept;
float attackValue (double ms) noexcept;
float releaseValue (double ms) noexcept;
float ratioValue (double r) noexcept;
float makeupValue (double db) noexcept;
float gainValue (double db) noexcept;

enum Module { deEsser, dePloser, eq, compander };
// the order's modules, first to last (lexicographic permutations of De-esser, De-ploser, EQ, Compander)
std::array<int, 4> order (int index) noexcept;
const char* orderName (int index) noexcept; // "DsDpEqCo" …

// the notch's Q for a gain (dB), measured; the high band's
double notchQ (double gainDb) noexcept;
double highQ (double gainDb) noexcept;

// a peaking filter whose magnitude follows the analog prototype (s² + s A/Q + 1) / (s² + s / (A Q) + 1) up to
// Nyquist (poles matched, zeros fitted at DC, the centre and Nyquist)
void matchedPeak (Biquad& b, double sampleRate, double hz, double q, double db) noexcept;

struct Settings
{
    double inputDb = 0.0, outputDb = 0.0;
    int order = 0;
    bool dsOn = false, dsSplit = true, dsLookahead = false, dsListen = false;
    double dsHz = 7500.0, dsThresholdDb = -24.0;
    bool dpOn = false, dpSplit = true, dpLookahead = false, dpListen = false;
    double dpHz = 120.0, dpThresholdDb = -24.0;
    bool eqOn = false, hpfOn = true, notchOn = true, highOn = true;
    double hpfHz = 30.0, notchHz = 200.0, notchDb = 0.0, highHz = 1000.0, highDb = 0.0;
    bool compOn = false, softKnee = false, drive = false;
    double expThresholdDb = -96.0, compThresholdDb = 0.0, ratio = 4.0, attackMs = 0.1, releaseMs = 100.0, makeupDb = 0.0;
    bool operator== (const Settings&) const = default;
};

class Engine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void set (const Settings&) noexcept;
    void process (float* left, float* right, int n) noexcept;
    int latencySamples() const noexcept; // the look-ahead delays (1 ms de-esser, 10 ms de-ploser)
    // gain reduction now, dB (positive): compander, de-esser, de-ploser
    float compReductionDb() const noexcept { return (float) compGrDb; }
    float deEssReductionDb() const noexcept { return (float) dsGrDb; }
    float dePlosReductionDb() const noexcept { return (float) dpGrDb; }

private:
    struct ThirdOrder // 3rd-order Butterworth (first-order + Q 1 biquad), stereo
    {
        double b0 = 1, b1 = 0, a1 = 0;
        double x1[2] {}, y1[2] {};
        Biquad bq;
        void design (double sr, double hz, bool high) noexcept;
        float process (float x, int c) noexcept;
        void reset() noexcept;
    };
    struct Delay
    {
        std::array<float, 2048> l {}, r {};
        int len = 0, pos = 0;
        void process (float& a, float& b) noexcept;
        void reset() noexcept { l.fill (0.0f); r.fill (0.0f); pos = 0; }
    };
    void deEss (float& a, float& b) noexcept;
    void dePlos (float& a, float& b) noexcept;
    void equalise (float& a, float& b) noexcept;
    void compand (float& a, float& b) noexcept;

    double sr = 48000.0;
    Settings s;
    std::array<int, 4> mods { 0, 1, 2, 3 };
    // de-esser
    ThirdOrder dsBand, dsOut, dsOutHigh;
    Delay dsDelay;
    double dsAtt = 0, dsRel = 0, dsEb = 0, dsEf = 0, dsGrDb = 0;
    // de-ploser
    ThirdOrder dpBand, dpDetect, dpOutLow;
    Delay dpDelay;
    double dpAtt = 0, dpRel = 0, dpEb = 0, dpEf = 0, dpGrDb = 0, dpLift = 1;
    // EQ
    Biquad hpf, notch, high;
    // compander
    double cAtt = 0, cRel = 0, cEnv = 0, xAtt = 0, xRel = 0, xEnv = 0, compGrDb = 0, makeup = 1;
    double inGain = 1, outGain = 1;
};
} // namespace lynx::voxstrip
