#pragma once

#include <functional>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/ExportOptions.h"
#include "core/Song.h"

namespace lynx
{
class AudioEngine;

// The short form used by the command line, the self-test and "Render to audio" (the Export window uses
// exportopts::Options, core/ExportOptions.h).
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

    exportopts::Options toOptions() const;
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
    ExportJob (const Song& song, const exportopts::Options& options, const juce::File& output);
    ~ExportJob();

    // progress(0..1) returns false to cancel. On error or cancellation the file is deleted. With several formats
    // (Options::moreFormats) every file is written from the one render: `output`, then the same name per extension.
    bool run (const std::function<bool (double)>& progress, juce::String& error);

    double getMusicSeconds() const noexcept { return musicSeconds; }
    float getPeak() const noexcept { return peak; }
    AudioEngine& getEngine() noexcept { return *engine; }

private:
    bool writeBuffered (std::vector<float>& left, std::vector<float>& right, juce::String& error);
    bool writeFile (const exportopts::Options& o, const juce::File& file, float* left, float* right, int frames, int channels, juce::String& error);

    std::unique_ptr<AudioEngine> engine;
    exportopts::Options options;
    juce::File output;
    std::string title, artist;
    double musicSeconds = 0.0, loopSeconds = 0.0;
    float peak = 0.0f;
};

// Seconds the export of `song` with these options lasts (the music, all loops; no tail).
double exportMusicSeconds (const Song& song, const exportopts::Options& options);
} // namespace lynx
