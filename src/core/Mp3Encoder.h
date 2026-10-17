#pragma once

// MP3 encoding for Export (constant bitrate, LAME 3.100's libmp3lame, third_party/lame, LGPL). No JUCE.

#include <cstdint>
#include <string>
#include <vector>

namespace lynx
{
class Mp3Encoder
{
public:
    Mp3Encoder() = default;
    ~Mp3Encoder();
    Mp3Encoder (const Mp3Encoder&) = delete;
    Mp3Encoder& operator= (const Mp3Encoder&) = delete;

    // `inputRate`: the rendered audio's rate; `outputRate`: 32000/44100/48000 (LAME resamples when they differ).
    // `title`/`artist` go into an ID3 tag when they are plain ASCII. False (with `error`) when LAME refuses them.
    bool open (int inputRate, int outputRate, int channels, int kbps, std::string& error,
               const std::string& title = {}, const std::string& artist = {});

    // Encodes `n` frames (float, ±1 full scale; `right` ignored for mono) and appends the MP3 bytes to `out`.
    bool encode (const float* left, const float* right, int n, std::vector<std::uint8_t>& out);
    // Flushes the last frames.
    bool finish (std::vector<std::uint8_t>& out);

private:
    void* handle = nullptr; // lame_t
    int channels = 2;
};

// Parses MP3 frame headers in `bytes` (after an optional ID3v2 tag): the number of frames found in a row, their
// bitrate (kbps, 0 when they differ) and sample rate. For tests and diagnostics.
struct Mp3Scan
{
    int frames = 0, kbps = 0, sampleRate = 0, channels = 0;
};
Mp3Scan scanMp3 (const std::vector<std::uint8_t>& bytes);
} // namespace lynx
