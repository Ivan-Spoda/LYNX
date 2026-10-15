#include "core/MaximusFx.h"

#include <cstring>

namespace lynx::maximusfx
{
namespace
{
constexpr std::uint8_t magic[4] { 'L', 'X', 'M', 'X' };
constexpr std::size_t headerSize = 4 + 1 + 1 + 4;
} // namespace

std::vector<std::uint8_t> pack (const Blob& blob)
{
    std::vector<std::uint8_t> out (magic, magic + 4);
    out.push_back (1);
    out.push_back (blob.build == hlfx::MaximusBuild::soundgoodizer ? 1 : 0);
    const auto mix = (std::int32_t) blob.mix;
    std::uint8_t m[4];
    std::memcpy (m, &mix, 4);
    out.insert (out.end(), m, m + 4);
    out.insert (out.end(), blob.state.begin(), blob.state.end());
    return out;
}

std::optional<Blob> unpack (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < headerSize || std::memcmp (bytes.data(), magic, 4) != 0 || bytes[4] != 1 || bytes[5] > 1)
        return std::nullopt;
    Blob b;
    b.build = bytes[5] == 1 ? hlfx::MaximusBuild::soundgoodizer : hlfx::MaximusBuild::shared;
    std::int32_t mix = 0;
    std::memcpy (&mix, bytes.data() + 6, 4);
    b.mix = mix;
    b.state.assign (bytes.begin() + (std::ptrdiff_t) headerSize, bytes.end());
    return b;
}

std::shared_ptr<const hlfx::MaximusPreset> presetOf (const Blob& blob)
{
    return hlfx::makeMaximusPreset (blob.state, blob.build);
}

std::optional<SoundgoodizerState> parseSoundgoodizer (const std::vector<std::uint8_t>& state)
{
    // FUN_00836960: u32 version (> 0), then the preset index and the amount, read raw into the plugin
    if (state.size() < 12)
        return std::nullopt;
    std::int32_t v[3];
    std::memcpy (v, state.data(), sizeof (v));
    if (v[0] < 1 || v[1] < 0 || v[1] > 3)
        return std::nullopt;
    return SoundgoodizerState { v[1], v[2] };
}

const char* soundgoodizerPresetFile (int preset)
{
    static const char* names[4] { "a.dat", "b.dat", "c.dat", "d.dat" };
    return names[preset < 0 || preset > 3 ? 0 : preset];
}

namespace
{
class MaximusEffect final : public Effect
{
public:
    explicit MaximusEffect (const std::vector<std::uint8_t>& slotState)
    {
        if (const auto blob = unpack (slotState))
            if ((preset = presetOf (*blob)) != nullptr)
                mix = blob->mix >= 0 ? blob->mix : hlfx::maximusMixParameter (*preset);
    }

    void prepare (double sampleRate) noexcept override
    {
        if (preset != nullptr)
            engine.prepare (preset, sampleRate, mix); // allocates; called off the audio thread by the host
    }

    void reset() noexcept override { engine.reset(); }

    void process (float* left, float* right, int n, const EffectParams&, double) noexcept override
    {
        engine.process (left, right, n);
    }

    int latencySamples() const noexcept override { return engine.latencySamples(); }

private:
    std::shared_ptr<const hlfx::MaximusPreset> preset;
    int mix = 1000;
    hlfx::MaximusProcessor engine;
};
} // namespace

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState)
{
    return std::make_unique<MaximusEffect> (slotState);
}
} // namespace lynx::maximusfx
