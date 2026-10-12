#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "core/Song.h"

namespace daw
{
// Formato próprio de projeto: JSON versionado (.dawproj).
// Notas vão como arrays compactos [start, length, channel, key, velocity].
namespace ProjectFile
{
constexpr const char* extension = ".dawproj";
constexpr int currentVersion = 1;

juce::String toJson (const Song& song);
std::optional<Song> fromJson (const juce::String& json, juce::String& error);

bool save (const Song& song, const juce::File& file, juce::String& error);
std::optional<Song> load (const juce::File& file, juce::String& error);
} // namespace ProjectFile
} // namespace daw
