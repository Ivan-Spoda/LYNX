#pragma once

// Audio editor window (round 86; the feature set follows FL Studio's Edison, docs/analise/REA_rodada86.md — the
// window and the drawing are LYNX's own). Waveform or spectrogram of one piece of audio with zoom/scroll, a
// selection, markers/regions and a loop; edits from core/AudioEdit (cut/copy/paste, delete, trim, silence, fades
// with curves, normalize, amp, reverse, polarity, DC removal, time stretch / pitch shift, resample, channels);
// an undo history; recording from the sound card input, the Master or a mixer insert; saving as WAV (markers and
// loop kept) and sending the result to the Playlist as an audio clip, to a new Sampler channel or back to the
// channel it came from.

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AudioEdit.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// An empty editor (Tools menu): record into it or open a file.
void openAudioEditor (AudioEngine& engine);
// A file (the Browser).
void openAudioEditor (AudioEngine& engine, const juce::File& file);
// The sample of a channel (a Sampler channel or the channel of a Playlist audio clip); the edit can go back to it.
void openAudioEditorForChannel (AudioEngine& engine, int channel);
// A mixer insert (0 = Master): opens with the recording source set to it, like an editor sitting in a mixer slot.
void openAudioEditorForInsert (AudioEngine& engine, int insert);

// --snapshot: the editor showing `doc` (selection = the second quarter), in the waveform or spectrogram view.
std::unique_ptr<juce::Component> makeAudioEditorForSnapshot (AudioEngine& engine, audioedit::Document doc, bool spectrum);
// --snapshot: the items of the editor's main menu, one per line ("Section > Item"), for the menu check.
juce::StringArray audioEditorMenuForSnapshot (juce::Component& editor);
} // namespace lynx
