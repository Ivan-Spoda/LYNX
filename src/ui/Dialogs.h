#pragma once

#include <cstdint>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
namespace MidiFileIo
{
struct ImportOptions;
}
// FL's "Import MIDI data" window (File › Import › MIDI file..., REA round 143). `onAccept` gets the chosen options.
void showMidiImportDialog (const juce::String& fileName, const MidiFileIo::ImportOptions& current,
                           std::function<void (const MidiFileIo::ImportOptions&)> onAccept);
std::unique_ptr<juce::AlertWindow> makeMidiImportDialog();

// "Rename and color" dialog for patterns and channels (docs/WORKFLOW_FL.md, P1 item 4): a name field
// and LYNX's palette (plus "automatic"); F2 inside the dialog picks a random palette color.
// `onDone(name, colour)` runs when the user confirms (colour 0 = automatic / theme default).
void showNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour,
                              std::function<void (const juce::String&, std::uint32_t)> onDone);

// The record button's question (FL's "Recording - What would you like to record?", REA_rodada88): audio into
// the Playlist, notes, everything, or audio into the audio editor. `onChoice(choice, dontAskAgain)`: 1 = audio,
// 2 = notes, 3 = everything, 4 = audio editor.
enum RecordChoice
{
    recordChoiceAudio = 1,
    recordChoiceNotes = 2,
    recordChoiceEverything = 3,
    recordChoiceAudioEditor = 4
};
void showRecordChoiceDialog (std::function<void (int, bool)> onChoice);
std::unique_ptr<juce::AlertWindow> makeRecordChoiceDialog();

// The dialog itself, for --snapshot mode.
std::unique_ptr<juce::AlertWindow> makeNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour);
} // namespace lynx
