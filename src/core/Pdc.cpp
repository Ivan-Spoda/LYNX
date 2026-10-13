#include "core/Pdc.h"

#include <algorithm>

namespace lynx
{
bool PdcPlan::any() const noexcept
{
    if (std::any_of (channelDelay.begin(), channelDelay.end(), [] (int d) { return d > 0; }))
        return true;

    for (const auto& t : sendDelay)
        if (std::any_of (t.begin(), t.end(), [] (int d) { return d > 0; }))
            return true;

    return false;
}

PdcPlan computePdc (const Song& song, const std::vector<int>& channelLatency, const std::vector<std::vector<int>>& slotLatency)
{
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    PdcPlan plan;
    plan.inAlign.assign ((std::size_t) tracks, 0);
    plan.outLat.assign ((std::size_t) tracks, 0);
    plan.channelDelay.assign (song.channels.size(), 0);
    plan.sendDelay.assign ((std::size_t) tracks, {});

    auto channelLat = [&] (std::size_t c) { return c < channelLatency.size() ? std::max (0, channelLatency[c]) : 0; };
    auto trackOf = [&] (const ChannelDef& c) { return std::clamp (c.mixerTrack, 0, tracks - 1); };

    // latência própria de entrada: o instrumento mais lento que chega na trilha
    for (std::size_t c = 0; c < song.channels.size(); ++c)
    {
        auto& in = plan.inAlign[(std::size_t) trackOf (song.channels[c])];
        in = std::max (in, channelLat (c));
    }

    auto effectsLatency = [&] (int t) {
        int sum = 0;

        if (t < (int) song.mixer.size() && t < (int) slotLatency.size())
        {
            const auto& slots = song.mixer[(std::size_t) t].effects;

            for (std::size_t s = 0; s < slots.size() && s < slotLatency[(std::size_t) t].size(); ++s)
                if (slots[s].enabled)
                    sum += std::max (0, slotLatency[(std::size_t) t][s]);
        }

        return sum;
    };

    // inserts em ordem: quando T é processada, todas as fontes dela já foram
    const auto order = song.mixerOrder();

    for (auto t : order)
    {
        plan.outLat[(std::size_t) t] = plan.inAlign[(std::size_t) t] + effectsLatency (t);
        const auto& track = song.mixer[(std::size_t) t];

        if (track.toMaster)
            plan.inAlign[0] = std::max (plan.inAlign[0], plan.outLat[(std::size_t) t]);

        for (const auto& s : track.sends)
            if (s.target > 0 && s.target < tracks)
                plan.inAlign[(std::size_t) s.target] = std::max (plan.inAlign[(std::size_t) s.target], plan.outLat[(std::size_t) t]);
    }

    plan.outLat[0] = plan.inAlign[0] + effectsLatency (0);
    plan.total = plan.outLat[0];

    // atrasos por aresta (o que falta para chegar no alinhamento do destino)
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        plan.channelDelay[c] = plan.inAlign[(std::size_t) trackOf (song.channels[c])] - channelLat (c);

    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];
        auto& delays = plan.sendDelay[(std::size_t) t];
        delays.assign (1 + track.sends.size(), 0);
        delays[0] = track.toMaster ? plan.inAlign[0] - plan.outLat[(std::size_t) t] : 0;

        for (std::size_t i = 0; i < track.sends.size(); ++i)
        {
            const auto target = track.sends[i].target;

            if (target > 0 && target < tracks)
                delays[1 + i] = std::max (0, plan.inAlign[(std::size_t) target] - plan.outLat[(std::size_t) t]);
        }
    }

    return plan;
}

DelayLine::DelayLine (int delaySamples)
    : delay (std::max (1, delaySamples)),
      buffer ((std::size_t) delay * 2, 0.0f)
{
}

void DelayLine::process (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        auto* slot = buffer.data() + (std::size_t) position * 2;
        const auto l = slot[0], r = slot[1];
        slot[0] = left[i];
        slot[1] = right[i];
        left[i] = l;
        right[i] = r;

        if (++position == delay)
            position = 0;
    }
}
} // namespace lynx
