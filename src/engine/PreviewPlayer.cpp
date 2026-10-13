#include "engine/PreviewPlayer.h"

namespace lynx
{
PreviewPlayer::PreviewPlayer()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void PreviewPlayer::play (std::shared_ptr<const SampleData> sample)
{
    std::shared_ptr<const SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired); // o que o áudio largou é desalocado aqui, na UI
        pending = std::move (sample);
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
            std::swap (current, pending); // o anterior vai para `pending` e depois para `retired`
            retired = std::move (pending);
            hasPending = false;
            position = 0.0;
        }
    }

    if (current == nullptr || current->frames() < 2 || buffer.getNumChannels() < 2)
        return;

    const auto* l = current->leftData();
    const auto* r = current->rightData();
    const auto last = (double) (current->frames() - 1);
    const auto rate = current->sampleRate / deviceRate;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    for (int i = 0; i < buffer.getNumSamples() && position < last; ++i)
    {
        const auto index = (std::size_t) position;
        const auto frac = (float) (position - (double) index);
        outL[i] = 0.8f * (l[index] + frac * (l[index + 1] - l[index]));
        outR[i] = 0.8f * (r[index] + frac * (r[index + 1] - r[index]));
        position += rate;
    }
}
} // namespace lynx
