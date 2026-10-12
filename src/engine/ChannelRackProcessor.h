#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/LiveEventQueue.h"
#include "core/RackRenderer.h"
#include "core/SnapshotExchange.h"
#include "engine/Transport.h"

namespace lynx
{
// Nó do grafo que toca o Channel Rack: pega o snapshot mais recente da música
// e renderiza os instrumentos internos com o sequenciador.
class ChannelRackProcessor final : public juce::AudioProcessor
{
public:
    ChannelRackProcessor (const Transport& transport, SnapshotExchange<RackState>& exchange, LiveEventQueue& live,
                          MeterBank& meters);

    const juce::String getName() const override { return "Channel Rack"; }

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
    SnapshotExchange<RackState>& exchange;
    LiveEventQueue& live;
    MeterBank& meters;
    RackRenderer renderer;
    juce::AudioBuffer<float> scratch; // para saída mono

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackProcessor)
};
} // namespace lynx
