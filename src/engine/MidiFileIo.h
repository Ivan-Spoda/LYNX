#pragma once

#include <optional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/Song.h"

namespace lynx
{
// Arquivos MIDI (.mid): importar como projeto, importar notas num canal e exportar patterns.
namespace MidiFileIo
{
// Projeto novo: um canal (synth interno) por trilha MIDI com notas, tudo numa pattern.
std::optional<Song> loadSong (const juce::File& file, juce::String& error);

// Todas as notas do arquivo (trilhas juntas) para `channel`, a partir do tick 0.
std::optional<std::vector<Note>> loadNotes (const juce::File& file, int channel, juce::String& error);

// Uma trilha por canal com notas na pattern. Tempo da música no início.
bool savePattern (const Song& song, int pattern, const juce::File& file, juce::String& error);
} // namespace MidiFileIo
} // namespace lynx
