#pragma once

// Decodes WavPack (.wv) audio, the format of many samples in FL Studio's packs, using the
// official library (thirdparty/wavpack, BSD license).

#include <cstdint>
#include <vector>

namespace lynx
{
struct DecodedAudio
{
    std::vector<std::vector<float>> channels; // planar, −1..1
    double sampleRate = 44100.0;
};

// nullopt-equivalent: returns false if the file is not valid WavPack.
bool decodeWavPack (const std::uint8_t* data, std::size_t size, DecodedAudio& out);
} // namespace lynx
