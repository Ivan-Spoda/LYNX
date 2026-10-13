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
    const auto songMode = transport.songMode.load (std::memory_order_relaxed);

    // no modo song, os compassos seguem as mudanças de compasso da Playlist (cópia consistente
    // da tabela; se a UI estiver escrevendo, fica a cópia do bloco anterior)
    if (songMode)
    {
        const auto before = transport.signatureVersion.load (std::memory_order_acquire);

        if ((before & 1u) == 0 && before != signatureVersion)
        {
            const auto count = juce::jlimit (0, Transport::maxSignatures, transport.signatureCount.load (std::memory_order_relaxed));
            decltype (signatures) copy {};

            for (int s = 0; s < count; ++s)
                copy[(std::size_t) s] = { transport.signatureTick[(std::size_t) s].load (std::memory_order_relaxed),
                                          transport.signatureLength[(std::size_t) s].load (std::memory_order_relaxed) };

            if (transport.signatureVersion.load (std::memory_order_acquire) == before)
            {
                signatures = copy;
                signatureCount = count;
                signatureVersion = before;
            }
        }
    }

    auto isDownbeat = [&] (std::int64_t beat) {
        if (! songMode || signatureCount == 0)
            return beat % beatsPerBar == 0;

        const auto tick = beat * ppq;
        auto index = 0;
        for (int s = 1; s < signatureCount; ++s)
            if (signatures[(std::size_t) s].first <= tick)
                index = s;

        const auto [start, length] = signatures[(std::size_t) index];
        return (tick - start) % juce::jmax<std::int64_t> (1, length) == 0;
    };
    const auto startTick = transport.positionTicks.load (std::memory_order_relaxed);
    const auto tps = transport.ticksPerSample();
    const auto clickLength = (int) (clickLengthSeconds * currentSampleRate);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto beat = (std::int64_t) ((startTick + i * tps) / ppq);

        if (beat != lastBeat)
        {
            lastBeat = beat;
            const auto hz = isDownbeat (beat) ? downbeatHz : beatHz;
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
