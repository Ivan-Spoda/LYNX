#pragma once

// Ogg Vorbis decoding the way FL Studio's own loader decodes it (REA_rodada77): FL stores most factory samples as
// WAV files whose "data" chunk is an Ogg Vorbis stream (format tags 0x674F…0x6751) and decodes them, and plain .ogg
// files, with libvorbis' plain packet loop rather than with vorbisfile:
//   - the whole file is scanned for Ogg pages (the RIFF header and trailing chunks are skipped as non-Ogg bytes);
//   - the logical stream is the first page's serial number; pages of other serials (FL's files multiplex a second,
//     non-Vorbis stream into many samples) are ignored, and holes in the packet sequence are skipped;
//   - decoding of a stream ends at the first page carrying the end-of-stream flag, whatever its serial number; a
//     following chained stream is decoded too when it starts with Vorbis headers, otherwise decoding stops there;
//   - every packet is synthesised, block-merged and its samples appended (libvorbis' granule-position trimming of the
//     first and last packets applies, nothing else).
// vorbisfile (used by JUCE's OggVorbisAudioFormat) treats a mid-stream page that begins another stream as a chain
// boundary and drops samples, so in 295 of the installation's 3 137 Ogg WAVs it returned fewer frames than the file
// holds; and FL's libvorbis builds its MDCT table from single-precision angles (third_party/vorbis/src/mdct.c, LYNX
// modification). With both, all 3 137 files decode bit-identically to FL's decoder [EXEC].

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lynx::oggvorbis
{
struct Decoded
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<std::vector<float>> data; // one vector per channel, all the same length
};

// True when `bytes` is a RIFF/WAVE file whose format tag is one of FL's Ogg Vorbis tags (0x674F, 0x6750, 0x6751).
bool isOggWav (const std::uint8_t* bytes, std::size_t size);

// True when `bytes` starts with an Ogg page ("OggS").
bool isOggStream (const std::uint8_t* bytes, std::size_t size);

// Decodes an Ogg-in-WAV file or a plain Ogg Vorbis file (the whole file's bytes) as FL does. Like FL's loader it
// accepts only mono and stereo streams. False when no Vorbis stream could be decoded.
bool decode (const std::uint8_t* bytes, std::size_t size, Decoded& out);
} // namespace lynx::oggvorbis
