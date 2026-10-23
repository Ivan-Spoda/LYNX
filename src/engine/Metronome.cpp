#include "engine/Metronome.h"

#include "core/ValueMath.h"

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

    const auto countIn = transport.countIn.load (std::memory_order_relaxed);
    const auto counting = countIn > 0.0 && ! transport.playing.load (std::memory_order_relaxed);

    if (! counting && (! transport.playing.load (std::memory_order_relaxed) || ! transport.metronome.load (std::memory_order_relaxed)))
    {
        lastBeat = -1;
        clickSamplesLeft = 0;
        return;
    }

    const auto beatsPerBar = juce::jmax (1, transport.beatsPerBar.load (std::memory_order_relaxed));
    const auto songMode = transport.songMode.load (std::memory_order_relaxed);

    // in song mode, bars follow the Playlist time-signature changes (consistent copy
    // of the table; if the UI is writing, the previous block's copy is kept)
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
        if (beat < 0 || ! songMode || signatureCount == 0)
            return ((beat % beatsPerBar) + beatsPerBar) % beatsPerBar == 0;

        const auto tick = beat * ppq;
        auto index = 0;
        for (int s = 1; s < signatureCount; ++s)
            if (signatures[(std::size_t) s].first <= tick)
                index = s;

        const auto [start, length] = signatures[(std::size_t) index];
        return (tick - start) % juce::jmax<std::int64_t> (1, length) == 0;
    };
    // during the count-in the clicks run on a position before the start (whole bars, so the beats line up)
    const auto startTick = transport.positionTicks.load (std::memory_order_relaxed) - (counting ? countIn : 0.0);
    const auto tps = transport.ticksPerSample();
    const auto clickLength = (int) (clickLengthSeconds * currentSampleRate);
    const auto level = clickGain * controls::metronomeGain (transport.metronomeVolume.load (std::memory_order_relaxed));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto beat = (std::int64_t) std::floor ((startTick + i * tps) / ppq);

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
            const auto sample = level * envelope * (float) std::sin (clickPhase);
            clickPhase += clickPhaseDelta;
            --clickSamplesLeft;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }
    }
}
} // namespace lynx
