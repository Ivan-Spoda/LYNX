#pragma once

// Descompressor DEFLATE/zlib (RFC 1950/1951) pequeno, para estados de plugins do FL gravados com
// zlib (Sytrus, Harmor). Só leitura; sem dependências.

#include <cstdint>
#include <optional>
#include <vector>

namespace lynx
{
// `data` começa no cabeçalho zlib (78 xx) ou, com `raw`, direto no fluxo DEFLATE. `maxSize` limita a
// saída (proteção contra blobs corrompidos). Nada se o fluxo for inválido.
std::optional<std::vector<std::uint8_t>> inflateZlib (const std::uint8_t* data, std::size_t size, bool raw = false,
                                                      std::size_t maxSize = 64u << 20);
} // namespace lynx
