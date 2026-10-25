#pragma once

// MP3 decoding for samples and audio clips (round 157b): minimp3 (third_party/minimp3, CC0) instead of JUCE's
// MP3AudioFormat, which glitched on many real files (whole granules wrong, −12 dB errors), read MPEG-2/2.5 files at
// twice their length, guessed VBR lengths from the first frame, decoded a second ID3v2 tag as audio and refused
// free-format streams or files with junk before the first frame. No JUCE.
//
// Timing (measured against FL Studio 2026's own renders, docs/analise/REA_rodada157b.md §3): FL plays an MP3 as the
// raw decoded stream, the encoder's delay and padding included, and a Xing/Info header frame (the silent first frame
// LAME, ffmpeg and most converters write) as one frame of silence. `Timing::flStudio` decodes the same way, so an
// imported project's MP3 clips sound exactly where they did in FL (JUCE's reader dropped the header frame: 26 ms early
// at 44.1 kHz). `Timing::gapless` instead removes what the LAME/Lavc extension of that header says the encoder added
// (its delay plus the decoder's 529 samples at the start, its padding at the end), as LAME's decoder, mpg123 and
// ffmpeg do; without the header nothing is removed.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lynx::mp3
{
struct Info
{
    int sampleRate = 0;
    int channels = 0;        // 1 or 2 (of the first audio frame)
    int layer = 0;           // 1, 2 or 3
    int firstBitrateKbps = 0;
    bool xingHeader = false; // a Xing/Info frame was found (and skipped)
    bool lameHeader = false; // with an encoder-delay extension (LAME, Lavc...)
    int headerFrameSamples = 0; // samples of the Xing/Info frame (1152, or 576 for MPEG-2/2.5); 0 without one
    int encoderDelay = 0;    // from the LAME/Lavc extension: the encoder's delay + the decoder's 529 (0 without)
    int encoderPadding = 0;  // from the LAME/Lavc extension: the encoder's padding − 529
    int leadingSilence = 0;  // Timing::flStudio: silent frames added for the header frame
    int trimmedStart = 0;    // Timing::gapless: frames removed at the start
    int trimmedEnd = 0;      // Timing::gapless: frames removed at the end
    std::int64_t frames = 0; // decoded length, per channel, after trimming
};

struct Decoded
{
    Info info;
    std::vector<float> left, right; // right empty for mono files
};

// Whether the bytes look like an MPEG audio stream: an ID3v2 tag, or (in the first `size` bytes) a frame header
// followed by another one where the first frame ends. WAV/AIFF/FLAC/Ogg/WavPack signatures are never taken for MP3.
bool looksLikeMp3 (const std::uint8_t* head, std::size_t size);

enum class Timing
{
    flStudio, // FL's: raw stream, the Xing/Info frame as silence (what LYNX loads)
    gapless   // the original's length and start (LAME/Lavc header)
};

// Decodes a whole file held in memory. False when no audio frame was found.
bool decode (const std::uint8_t* data, std::size_t size, Decoded& out, Timing timing = Timing::flStudio);

// minimp3's own whole-file decoder (mp3dec_load_buf: gapless, one ID3v2 tag) into the same layout, for the tests
// (Timing::gapless must give its samples bit for bit).
bool decodeReference (const std::uint8_t* data, std::size_t size, Decoded& out);

// Name of this decoder for cache keys (bumped when the decoded samples of a file can change).
inline constexpr const char* decoderId = "minimp3-fl-1";
} // namespace lynx::mp3
