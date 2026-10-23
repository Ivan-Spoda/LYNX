#pragma once

#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/Song.h"

namespace lynx
{
// MIDI files (.mid): import as a project, import notes into a channel, and export patterns.
namespace MidiFileIo
{
// New project: one channel (built-in synth) per MIDI track with notes, all in one pattern.
std::optional<Song> loadSong (const juce::File& file, juce::String& error);

// All notes in the file (tracks merged) into `channel`, starting at tick 0.
std::optional<std::vector<Note>> loadNotes (const juce::File& file, int channel, juce::String& error);

// One track per channel with notes in the pattern. Song tempo at the start.
bool savePattern (const Song& song, int pattern, const juce::File& file, juce::String& error);

// FL's File › Export › "MIDI file..." (REA round 139): the arrangement's notes (every pattern clip as it plays in the
// Playlist: its part of the pattern, cut at the clip's end; muted clips and notes left out), one track per channel;
// in pattern mode or with an empty Playlist, the current pattern. Tempo and time signature at the start.
bool saveSong (const Song& song, const juce::File& file, juce::String& error);
} // namespace MidiFileIo
} // namespace lynx
