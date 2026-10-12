#include "engine/Metronome.h"

namespace lynx
{
namespace
{
constexpr double clickLengthSeconds = 0.03;
constexpr double downbeatHz = 1760.0;
constexpr double beatHz = 880.0;
constexpr float clickGain = 0.4f;
} // namespace

Metronome::Metronome (const Transport& t)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (t)
{
}

void Metronome::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    lastBeat = -1;
    clickSamplesLeft = 0;
}

void Metronome::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();

    if (! transport.playing.load (std::memory_order_relaxed) || ! transport.metronome.load (std::memory_order_relaxed))
    {
        lastBeat = -1;
        clickSamplesLeft = 0;
        return;
    }

    const auto beatsPerBar = juce::jmax (1, transport.beatsPerBar.load (std::memory_order_relaxed));
    const auto startTick = transport.positionTicks.load (std::memory_order_relaxed);
    const auto tps = transport.ticksPerSample();
    const auto clickLength = (int) (clickLengthSeconds * currentSampleRate);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto beat = (std::int64_t) ((startTick + i * tps) / ppq);

        if (beat != lastBeat)
        {
            lastBeat = beat;
            const auto hz = (beat % beatsPerBar == 0) ? downbeatHz : beatHz;
            clickPhase = 0.0;
            clickPhaseDelta = juce::MathConstants<double>::twoPi * hz / currentSampleRate;
            clickSamplesLeft = clickLength;
        }

        if (clickSamplesLeft > 0)
        {
            const auto envelope = (float) clickSamplesLeft / (float) clickLength;
            const auto sample = clickGain * envelope * (float) std::sin (clickPhase);
            clickPhase += clickPhaseDelta;
            --clickSamplesLeft;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }
    }
}
} // namespace lynx
