#pragma once

// FL Studio Harmless's built-in effects rack, rebuilt from docs/analise/REA_rodada36.md (clean-room; a port of our own
// reference model, which was checked against the installation's Harmless_x64.dll, Reverb_x64.dll and SG_x64.dll).
//
// Order on the mix of all voices (Gen_Render): chorus → delay → reverb → compressor. The chorus, delay and reverb
// are fixed presets selected by the rack's type combos (tables read in Harmless_x64.dll); the compressor is FL's
// Maximus engine (hosted through Shared/SG_x64.dll) loaded with Harmless's own preset
// Plugins/Fruity/Generators/Harmless/Data/Compression/<type>.fst, whose processed/original band mix is the rack's
// "Mix" knob. The preset files are read at run time from the user's FL installation; nothing of them is stored here.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace lynx::hlfx
{
// the Maximus state (plugin record 0xD5) inside an FL preset file (.fst); nullopt if there is none
std::optional<std::vector<std::uint8_t>> maximusStateFromFst (const std::vector<std::uint8_t>& fst);

// Everything of a Maximus state that does not depend on the sample rate (parameters, envelope gain tables,
// saturation tables). Built off the audio thread; shared read-only by the instruments that use it.
struct MaximusPreset;
std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& state);

class Maximus;

// The whole rack of one Harmless instance (state lives here; the presets are shared)
class Rack
{
public:
    Rack();
    ~Rack();
    Rack (const Rack&) = delete;
    Rack& operator= (const Rack&) = delete;

    // p: the 100 parameters, t: the 7 combo integers; comp: the compression preset (null: compressor off)
    void prepare (const std::array<int, 100>& p, const std::array<int, 7>& t, double fs, double bpm,
                  std::shared_ptr<const MaximusPreset> comp);
    bool enabled() const noexcept { return on; }

    // in place on the float mix of one host block (n ≤ maxBlock); voices: records existed at the start of the block
    void process (float* left, float* right, int n, bool voices) noexcept;

    // no voices and the rack has gone quiet: only its free-running counters and phases advance
    void advance (int n) noexcept;

    // true once the rack's output has stayed below −140 dB for longer than its longest internal delay
    bool quiet() const noexcept { return ! on || quietFor >= quietNeeded; }

    static constexpr int maxBlock = 512;

private:
    struct Chorus;
    struct Delay;
    struct Reverb;
    std::unique_ptr<Chorus> chorus;
    std::unique_ptr<Delay> delay;
    std::unique_ptr<Reverb> reverb;
    std::unique_ptr<Maximus> comp;
    std::shared_ptr<const MaximusPreset> compPreset;
    float wet = 0.0f;
    int tail = 0, tailLength = 0;
    long long quietFor = 0, quietNeeded = 0;
    bool on = false;
    std::array<float, maxBlock> scratchL {}, scratchR {}, mono {};
};
} // namespace lynx::hlfx
