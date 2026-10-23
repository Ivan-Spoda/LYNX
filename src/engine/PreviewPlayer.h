#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Instruments.h"

namespace lynx
{
// Graph node that plays a file preview (sample browser), outside the song.
class PreviewPlayer final : public juce::AudioProcessor
{
public:
    PreviewPlayer();

    // UI thread. nullptr to stop. `loop`: plays it over and over (the audio editor's loop playback).
    void play (std::shared_ptr<const SampleData> sample, bool loop = false, float gain = 0.8f);
    // A Browser preview: like play(), and the level also follows the preview volume (setVolume) while it plays.
    void playBrowser (std::shared_ptr<const SampleData> sample, float gain = 0.8f);
    // The Browser's preview volume as a gain (core/ValueMath.h previewGain). Any thread; applies at once.
    void setVolume (float gainFactor) noexcept { volume.store (gainFactor, std::memory_order_relaxed); }
    // Any thread: the frame being played in the current sample, or -1 when nothing plays.
    double getPlayPosition() const noexcept { return playPosition.load (std::memory_order_relaxed); }

    const juce::String getName() const override { return "Preview"; }
    void prepareToPlay (double sampleRate, int) override { deviceRate = sampleRate; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::SpinLock lock;                       // the audio thread only tries (never waits)
    std::shared_ptr<const SampleData> pending; // swapped by the UI
    bool hasPending = false;
    bool pendingLoop = false, looping = false;
    float pendingGain = 0.8f, gain = 0.8f;
    bool pendingBrowser = false, browser = false;
    std::atomic<float> volume { 1.0f };
    std::atomic<double> playPosition { -1.0 };
    std::shared_ptr<const SampleData> current;  // touched only by the audio thread
    std::shared_ptr<const SampleData> retired;  // released by the UI (audio never deallocates)
    double position = 0.0;
    double deviceRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPlayer)
};
} // namespace lynx
