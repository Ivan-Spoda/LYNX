#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Instruments.h"

namespace lynx
{
// Nó do grafo que toca a prévia de um arquivo (navegador de samples), fora da música.
class PreviewPlayer final : public juce::AudioProcessor
{
public:
    PreviewPlayer();

    // Thread da UI. nullptr para parar.
    void play (std::shared_ptr<const SampleData> sample);

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
    juce::SpinLock lock;                       // a thread de áudio só tenta (nunca espera)
    std::shared_ptr<const SampleData> pending; // trocado pela UI
    bool hasPending = false;
    std::shared_ptr<const SampleData> current;  // só a thread de áudio mexe
    std::shared_ptr<const SampleData> retired;  // a UI solta (o áudio nunca desaloca)
    double position = 0.0;
    double deviceRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPlayer)
};
} // namespace lynx
