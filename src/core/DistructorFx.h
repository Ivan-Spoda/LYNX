#pragma once

// FL Studio's Distructor as an exact LYNX mixer effect, rebuilt in docs/analise/REA_rodada71.md (clean-room: layouts,
// formulas and behaviour described in our own words; no Image-Line code or data in this repository).
//
// Distructor is a chain of up to four "modules" (each one an instance of the plugin's Module_x64.dll: distortion,
// filter, chorus or amp/cabinet), processed in the order they are stored, followed by an optional soft clipper.
// Every module has 49 integer controls; the module type decides which of them are used.
//
// The effect slot (EffectKind::distructor) keeps FL's own saved state (plugin record 0xD5) in EffectSlot::state, so
// the engine always starts from exactly what FL saved; the slot parameters expose the common controls of each
// module (input volume, mix, output volume) plus the final clipper so they can be edited and automated.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
class Effect;
}

namespace lynx::distructor
{
constexpr int maxModules = 4;
constexpr int moduleParams = 49;

enum class ModuleType : int
{
    distortion = 0,
    filter = 1,
    chorus = 2,
    ampCab = 3
};

// one module's saved state (Module_x64.dll's own state, version 1…4)
struct ModuleState
{
    int version = 4;
    int type = 0;          // ModuleType (0…3)
    int algorithm = 0;     // distortion algorithm 0…17
    int oversample = 0;    // 0…4: ×2^oversample around distortion / filter modules
    bool removeDc = true;  // distortion: DC blocker after the module
    bool bypass = false;
    int filterType = 0;    // filter type 0…21
    bool smoothFilter = true;
    int impulse = 1;       // amp/cabinet impulse 0…58
    std::array<int, moduleParams> p {};
    int chorusMode = 0;    // chorus: 0 wide, 1 mono, 2 stereo
};

struct State
{
    int version = 5;
    std::vector<ModuleState> modules;
    bool clip = true; // the final soft clipper
    std::array<int, maxModules> order { 0, 1, 2, 3 };
};

// the defaults of a new module (Module_x64's own initial controls)
ModuleState defaultModule (ModuleType type) noexcept;

// FL's saved state of the whole plugin; nullopt = not a Distructor state
std::optional<State> parse (const std::vector<std::uint8_t>& state);
// one module's own state block (231 bytes for version 4); nullopt = not a module state
std::optional<ModuleState> parseModule (const std::uint8_t* data, std::size_t size, std::size_t* used = nullptr);
// writes FL's layout back (version 5 / module version 4)
std::vector<std::uint8_t> serialize (const State& s);

// a short description of a module for the UI ("Distortion: Fast dist")
std::string moduleName (const ModuleState& m);

// where the installation's cabinet impulses (".wv") live; empty = search the usual installation folders
void setImpulseFolder (const std::string& folder);

// One module (FL's Module_x64 instance): prepare(), then process() in place. Stereo interleaving is not used:
// left/right buffers.
class Module
{
public:
    Module();
    ~Module();
    Module (const Module&) = delete;
    Module& operator= (const Module&) = delete;
    void prepare (double sampleRate, int maxBlock);
    void set (const ModuleState& s); // a newly loaded state (as FL's state load: smoothers start from rest)
    void process (float* left, float* right, int n) noexcept;
    const ModuleState& state() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate, int maxBlock);
    void set (const State& s);
    void process (float* left, float* right, int n) noexcept; // FL processes blocks of ≤ 512 frames
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& state);
} // namespace lynx::distructor
