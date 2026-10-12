#include "core/RackRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace daw
{
std::unique_ptr<const RackState> RackState::build (Song song, const RackState* previous, double sampleRate)
{
    auto state = std::make_unique<RackState>();
    state->instruments.reserve (song.channels.size());

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        const auto kind = song.channels[i].instrument;

        if (previous != nullptr && i < previous->song.channels.size() && i < previous->instruments.size()
            && previous->song.channels[i].instrument == kind)
        {
            state->instruments.push_back (previous->instruments[i]);
            continue;
        }

        std::shared_ptr<Instrument> instrument = makeInstrument (kind);
        instrument->prepare (sampleRate);
        state->instruments.push_back (std::move (instrument));
    }

    state->song = std::move (song);
    return state;
}

void RackRenderer::prepare (double sr) noexcept
{
    sampleRate = sr;

    if (lastState != nullptr)
        for (auto& instrument : lastState->instruments)
            instrument->prepare (sr);
}

void RackRenderer::renderSegment (const RackState& state, float* left, float* right, int n) noexcept
{
    const auto& channels = state.song.channels;

    for (std::size_t i = 0; i < state.instruments.size() && i < channels.size(); ++i)
    {
        const auto& c = channels[i];
        // pan de potência constante
        const auto angle = (c.pan + 1.0f) * 0.25f * 3.14159265f;
        state.instruments[i]->render (left, right, n, c.volume * std::cos (angle) * 1.41421356f,
                                      c.volume * std::sin (angle) * 1.41421356f);
    }
}

void RackRenderer::process (const RackState* state,
                            bool playing,
                            double startTick,
                            int numSamples,
                            float* left,
                            float* right) noexcept
{
    std::memset (left, 0, sizeof (float) * (std::size_t) numSamples);
    std::memset (right, 0, sizeof (float) * (std::size_t) numSamples);
    events.clear();

    if (state == nullptr)
        return;

    lastState = state; // para prepare() saber quais instrumentos reconfigurar

    if (wasPlaying && ! playing)
        for (auto& instrument : state->instruments)
            instrument->allNotesOff();

    wasPlaying = playing;

    const auto& song = state->song;

    if (playing && ! song.patterns.empty())
    {
        const auto patternIndex = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
        const auto& pattern = song.patterns[(std::size_t) patternIndex];
        schedulePatternLoop (pattern, pattern.effectiveLength (song.beatsPerBar), startTick, numSamples,
                             ticksPerSample (song.bpm, sampleRate), events);
    }

    // Renderiza em segmentos entre eventos para cada nota começar no sample exato.
    int position = 0;

    for (const auto& e : events)
    {
        if (e.sampleOffset > position)
        {
            renderSegment (*state, left + position, right + position, e.sampleOffset - position);
            position = e.sampleOffset;
        }

        if (e.channel < 0 || e.channel >= (int) state->instruments.size())
            continue;

        auto& instrument = *state->instruments[(std::size_t) e.channel];

        if (e.type == NoteEvent::Type::on)
        {
            if (! song.channels[(std::size_t) e.channel].muted)
                instrument.noteOn (e.key, e.velocity);
        }
        else
        {
            instrument.noteOff (e.key);
        }
    }

    if (position < numSamples)
        renderSegment (*state, left + position, right + position, numSamples - position);
}
} // namespace daw
