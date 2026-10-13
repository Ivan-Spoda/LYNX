#pragma once

// Decodifica áudio WavPack (.wv), o formato de muitos samples dos pacotes do FL Studio, com a
// biblioteca oficial (thirdparty/wavpack, licença BSD).

#include <cstdint>
#include <vector>

namespace lynx
{
struct DecodedAudio
{
    std::vector<std::vector<float>> channels; // planar, −1..1
    double sampleRate = 44100.0;
};

// nullopt-equivalente: devolve false se o arquivo não for WavPack válido.
bool decodeWavPack (const std::uint8_t* data, std::size_t size, DecodedAudio& out);
} // namespace lynx
