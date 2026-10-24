#pragma once

// Audio logger (FL 2026's Tools > Audio Logger, behaviour from its release notes; LYNX's own implementation,
// REA round 153): while it is on, everything the Master plays is written to a WAV file in the background, so an
// idea played live is never lost. The audio thread only copies the Master's block into a lock-free ring
// (MeterBank::logTap); a background thread empties it into the file.

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/AudioCapture.h"

namespace lynx
{
class AudioLogger final : private juce::Thread
{
public:
    explicit AudioLogger (AudioCapture& tap);
    ~AudioLogger() override;

    // Starts a new file in `folder` ("LYNX log <date> <time>.wav", 24-bit stereo at `sampleRate`). False if the file
    // cannot be created.
    bool start (const juce::File& folder, double sampleRate);
    void stop();
    bool isLogging() const noexcept { return writer != nullptr; }
    juce::File currentFile() const { return file; }
    double secondsWritten() const noexcept { return rate > 0.0 ? (double) framesWritten.load() / rate : 0.0; }

    static juce::File defaultFolder(); // Documents/LYNX/Audio logger

private:
    void run() override;
    void drain();

    AudioCapture& tap;
    juce::File file;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    double rate = 0.0;
    std::atomic<std::int64_t> framesWritten { 0 };
    std::vector<float> left, right;
    juce::CriticalSection writing;
};
} // namespace lynx
