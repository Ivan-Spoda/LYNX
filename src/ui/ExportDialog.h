#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// "Export audio" window: format, sample rate and what to export; then asks for the file and
// renders on a background thread with a progress bar. `suggestedName` = name without extension.
void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName);

// "Render to audio" (freeze): the insert becomes an audio clip on a new insert with the same
// routing, and the original is muted. Runs on a background thread with progress.
void bounceInsertToAudio (AudioEngine& engine, int track);

// Panel only (for --snapshot mode; `format`: 0 WAV, 1 MP3, 2 OGG, 3 FLAC with loop/stem options set). Nothing is saved.
std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine, int format = 0);
} // namespace lynx
