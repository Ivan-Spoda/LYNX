#pragma once

#include <functional>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

struct ExportSettings
{
    enum class Format
    {
        wav16,
        wav24,
        wav32Float,
        flac24,
        ogg
    };

    Format format = Format::wav24;
    double sampleRate = 44100.0;
    bool wholeSong = true;     // false = current pattern only
    double tailSeconds = 6.0;  // maximum tail; stops earlier when the sound ends
};

// File extension (with dot) for the format.
juce::String exportExtension (ExportSettings::Format format);

// Exports a copy of the song with its own engine, without touching what is playing.
// The constructor creates the instruments and plugins (call on the UI thread); run() may
// run on a background thread.
class ExportJob
{
public:
    ExportJob (const Song& song, const ExportSettings& settings, const juce::File& output);
    ~ExportJob();

    // progress(0..1) returns false to cancel. On error or cancellation the file is deleted.
    bool run (const std::function<bool (double)>& progress, juce::String& error);

    double getMusicSeconds() const noexcept { return musicSeconds; }
    float getPeak() const noexcept { return peak; }
    AudioEngine& getEngine() noexcept { return *engine; }

private:
    std::unique_ptr<AudioEngine> engine;
    ExportSettings settings;
    juce::File output;
    double musicSeconds = 0.0;
    float peak = 0.0f;
};
} // namespace lynx
