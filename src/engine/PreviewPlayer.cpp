#include "engine/PreviewPlayer.h"

namespace lynx
{
PreviewPlayer::PreviewPlayer()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void PreviewPlayer::play (std::shared_ptr<const SampleData> sample, bool loop, float newGain)
{
    std::shared_ptr<const SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired); // whatever the audio thread released is freed here, on the UI thread
        pending = std::move (sample);
        pendingLoop = loop;
        pendingGain = newGain;
        pendingBrowser = false;
        hasPending = true;
    }
}

void PreviewPlayer::playBrowser (std::shared_ptr<const SampleData> sample, float newGain)
{
    std::shared_ptr<const SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired);
        pending = std::move (sample);
        pendingLoop = false;
        pendingGain = newGain;
        pendingBrowser = true;
        hasPending = true;
    }
}

void PreviewPlayer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();

    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        if (sl.isLocked() && hasPending)
        {
            std::swap (current, pending); // the previous one goes to `pending` and then to `retired`
            retired = std::move (pending);
            hasPending = false;
            looping = pendingLoop;
            gain = pendingGain;
            browser = pendingBrowser;
            position = 0.0;
        }
    }

    if (current == nullptr || current->frames() < 2 || buffer.getNumChannels() < 2)
    {
        playPosition.store (-1.0, std::memory_order_relaxed);
        return;
    }

    const auto* l = current->leftData();
    const auto* r = current->rightData();
    const auto last = (double) (current->frames() - 1);
    const auto rate = current->sampleRate / deviceRate;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    const auto level = gain * (browser ? volume.load (std::memory_order_relaxed) : 1.0f);

    for (int i = 0; i < buffer.getNumSamples() && position < last; ++i)
    {
        const auto index = (std::size_t) position;
        const auto frac = (float) (position - (double) index);
        outL[i] = level * (l[index] + frac * (l[index + 1] - l[index]));
        outR[i] = level * (r[index] + frac * (r[index + 1] - r[index]));
        position += rate;

        if (looping && position >= last)
            position -= last;
    }

    playPosition.store (position < last ? position : -1.0, std::memory_order_relaxed);
}
} // namespace lynx
