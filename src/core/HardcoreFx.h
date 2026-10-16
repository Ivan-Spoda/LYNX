#pragma once

// FL Studio's Hardcore (guitar multi-effect: five pedal boxes, a cabinet impulse and an 8-band EQ) as an exact LYNX
// mixer effect, rebuilt in docs/analise/REA_rodada71.md (clean-room: layouts, formulas and behaviour in our own words;
// no Image-Line code or data in this repository; the cabinet impulses are read from the user's installation).
//
// Signal path per block: input level (with a 30 Hz high-pass) → boxes 1…5 in series (each holds one of 11 pedals or
// nothing, and can be switched off) → cabinet convolution → 8-band EQ (when on) → output level.
//
// The effect slot (EffectKind::hardcore) keeps FL's 50 parameters in its params (all 0…1 except the cabinet, stored
// as index / 20, and the five box types, stored as type / 11); EffectSlot::state holds the cabinet impulse's WAV file
// (read from the installation at import time) so a saved LYNX project keeps working without FL.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Effect;
}

namespace lynx::hardcore
{
constexpr int numParams = 50;
constexpr int numBoxes = 5, paramsPerBox = 5, numCabinets = 20, numPedalTypes = 11;
// FL's parameter indices
constexpr int pInLevel = 0, pOutLevel = 1, pEqOn = 2, pEqBand = 3 /* 3…10 */, pCabinet = 11;
constexpr int pBoxParam = 13 /* 13 + 5·box + k */, pBoxOn = 39 /* 39…43 */, pBoxType = 45 /* 45…49 */;

// pedal types (FL's box type values; 0 = empty box)
enum Pedal
{
    none = 0, distModelA = 1, chorus = 2, flanger = 3, phaser = 4, reverb = 5, delay = 6, noiseGate = 7, equalizer = 8,
    compressor = 9, modulator = 10, tubeDrive = 11
};

struct State
{
    std::array<float, numParams> values {}; // FL's raw values (cabinet 0…20, types 0…11, the rest 0…1)
    std::string presetName;
};

// FL's default values (what the plug-in sets on creation)
std::array<float, numParams> defaultValues() noexcept;
// FL's saved state (plugin record 0xD5: "SSLF", size, then the "1RCH" chunk); nullopt = not a Hardcore state
std::optional<State> parse (const std::vector<std::uint8_t>& state);

// slot parameters (0…1) ⇄ FL's raw values
float toSlot (int param, float raw) noexcept;
float fromSlot (int param, float v01) noexcept;
void toParams (const State& s, EffectParams& params) noexcept;
std::string paramName (int param);
std::string paramText (int param, float v01);
const char* pedalName (int type) noexcept;

// the cabinet impulse file FL loads for a cabinet value (1…20), relative to %FLStudioFactoryData%; "" for 0
std::string cabinetPath (int cabinet);

class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // the impulse WAV files the cabinet parameter may select (index 1…20); call before set (pCabinet, …)
    void setCabinetFile (int cabinet, std::vector<std::uint8_t> wav);
    // FL's setParameter: raw value (types and cabinet as numbers). May allocate (a box type change builds its pedal).
    void set (int param, float raw);
    float get (int param) const noexcept;
    // FL's sample-rate message: every pedal is rebuilt for the rate and cleared, the levels jump to their targets
    void prepare (double sampleRate);
    // stereo, in place
    void process (float* left, float* right, int n) noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

std::unique_ptr<Effect> make (const EffectSlot& slot);
} // namespace lynx::hardcore
