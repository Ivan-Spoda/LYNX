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
      left ((std::size_t) delay, 0.0f),
      right ((std::size_t) delay, 0.0f),
      quiet (delay)
{
}

void DelayLine::process (float* l, float* r, int n) noexcept
{
    // em trechos contíguos do anel: troca o que entra pelo que sai
    for (int done = 0; done < n;)
    {
        const auto run = std::min (n - done, delay - position);
        auto* bl = left.data() + position;
        auto* br = right.data() + position;

        for (int i = 0; i < run; ++i)
        {
            std::swap (bl[i], l[done + i]);
            std::swap (br[i], r[done + i]);
        }

        done += run;
        position = (position + run) % delay;
    }

    quiet = 0;
}

void DelayLine::mixInto (const float* sl, const float* sr, float level, float* dl, float* dr, int n) noexcept
{
    if (sl == nullptr)
    {
        if (quiet >= delay)
            return; // tudo zero lá dentro: a saída seria silêncio e a posição não importa

        quiet += n;
    }
    else
    {
        quiet = 0;
    }

    for (int done = 0; done < n;)
    {
        const auto run = std::min (n - done, delay - position);
        auto* bl = left.data() + position;
        auto* br = right.data() + position;
        auto* ol = dl + done;
        auto* orr = dr + done;

        if (sl != nullptr)
        {
            const auto* il = sl + done;
            const auto* ir = sr + done;

            for (int i = 0; i < run; ++i)
            {
                ol[i] += bl[i];
                orr[i] += br[i];
                bl[i] = il[i] * level;
                br[i] = ir[i] * level;
            }
        }
        else
        {
            for (int i = 0; i < run; ++i)
            {
                ol[i] += bl[i];
                orr[i] += br[i];
                bl[i] = 0.0f;
                br[i] = 0.0f;
            }
        }

        done += run;
        position = (position + run) % delay;
    }
}
} // namespace lynx
