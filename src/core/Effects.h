#pragma once

// Built-in effects for the mixer slots. Written from scratch, no dependencies.
// Buffers are allocated in the constructor already sized for 192 kHz, so prepare() and
// process() never allocate and can run on the audio thread.

#include <array>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Parameter description for the UI (all values are 0..1 in the model).
struct EffectParamInfo
{
    const char* name;
    float defaultValue;
};

struct EffectInfo
{
    const char* name;
    std::vector<EffectParamInfo> params;
};

const EffectInfo& effectInfo (EffectKind kind);

// Text for a parameter value, converted to its real unit (dB, ms, %...).
std::string formatEffectParam (EffectKind kind, int param, float value01, double bpm);

EffectSlot makeEffectSlot (EffectKind kind);

// 7-band parametric EQ. Parameters: band b uses [4b .. 4b+3] = type, frequency, gain, Q;
// [28] = output; [29 + b] = cut slope (1 to 4 biquads: 12/24/36/48 dB per octave).
// For shelves the "Q" is the cookbook shelf slope S (1 = default).
namespace peq
{
constexpr int bands = 7;
constexpr int outputParam = 28;
constexpr int slopeParam = 29;

enum class BandType { off, peak, lowShelf, highShelf, lowCut, highCut, bandPass, notch };
constexpr int bandTypes = 8;

inline int param (int band, int field) noexcept { return band * 4 + field; } // field: 0 type, 1 freq, 2 gain, 3 Q
BandType type (float v) noexcept;
float typeValue (BandType t) noexcept;
double hz (float v) noexcept;      // 20 Hz .. 20 kHz (log)
float hzValue (double hz) noexcept;
double gainDb (float v) noexcept;  // ±18 dB
float gainValue (double db) noexcept;
double q (float v) noexcept;       // 0.1 .. 18 (log)
float qValue (double q) noexcept;
int slopeStages (float v) noexcept; // 1 to 4 biquads (12/24/36/48 dB/oct)
float slopeValue (int stages) noexcept;
const char* typeName (BandType t) noexcept;

// Response of the whole EQ (all bands and the output) in dB, for the editor graph.
double responseDb (const EffectParams& params, double sampleRate, double hz);
} // namespace peq

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;

    // Processes in place. Parameters arrive every block (they may change at any time).
    virtual void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept = 0;
    // Automated plugin parameter (built-in effects receive their parameters in process()). 0..1.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // Control output ("controller" effects such as the Peak Controller), 0..1. Read every block.
    virtual float controlOutput (int /*output*/) const noexcept { return 0.0f; }
    // Song position at the start of the block, in ticks (for tempo-synced effects).
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    // Sidechain input for the next process(): another track's output, or nullptr if that track is
    // silent. Only called when the slot has a sidechain.
    virtual void setSidechain (const float* /*left*/, const float* /*right*/) noexcept {}
    // Second side input (Vocodex carrier), same rules; only called when the slot has `sidechain2`.
    virtual void setSidechain2 (const float* /*left*/, const float* /*right*/) noexcept {}
    // Latency the effect introduces (plugins), in samples. Used by PDC.
    virtual int latencySamples() const noexcept { return 0; }
};

std::unique_ptr<Effect> makeEffect (EffectKind kind);

// FL's mode-0 pan law as a 2×2 matrix (outL = ll·inL + rl·inR, outR = lr·inL + rr·inR), unit volume (REA_rodada27 §3.1)
void flPanMatrix (double pan, float& ll, float& rl, float& lr, float& rr) noexcept;
// `parallel` container holding the effects of the slot's branches.
std::unique_ptr<Effect> makeParallelEffect (const EffectSlot& slot);
} // namespace lynx
