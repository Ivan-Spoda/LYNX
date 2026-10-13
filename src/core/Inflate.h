#pragma once

// Small DEFLATE/zlib (RFC 1950/1951) decompressor, for FL plugin states saved with
// zlib (Sytrus, Harmor). Read-only; no dependencies.

#include <cstdint>
#include <optional>
#include <vector>

namespace lynx
{
// `data` starts at the zlib header (78 xx) or, with `raw`, directly at the DEFLATE stream. `maxSize` caps the
// output (protection against corrupted blobs). Nothing if the stream is invalid.
std::optional<std::vector<std::uint8_t>> inflateZlib (const std::uint8_t* data, std::size_t size, bool raw = false,
                                                      std::size_t maxSize = 64u << 20);
} // namespace lynx
