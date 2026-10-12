#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Transport.h"

namespace daw
{
// Metrônomo: um clique por tempo, mais agudo no primeiro tempo
// do compasso. Liga/desliga por Transport::metronome.
class Metronome final : public juce::AudioProcessor
{
public:
    explicit Metronome (const Transport& transport);

    const juce::String getName() const override { return "Metronome"; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

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
    const Transport& transport;
    double currentSampleRate = 44100.0;
    std::int64_t lastBeat = -1;
    double clickPhase = 0.0;
    double clickPhaseDelta = 0.0;
    int clickSamplesLeft = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Metronome)
};
} // namespace daw
