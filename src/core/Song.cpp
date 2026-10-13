#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lynx
{
void Pattern::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
}

std::int64_t Pattern::effectiveLength (int beatsPerBar) const noexcept
{
    if (lengthTicks > 0)
        return lengthTicks;

    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (auto& n : notes)
        end = std::max (end, n.start + std::max<std::int64_t> (n.length, 1));

    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

bool Pattern::hasStep (int channel, int step) const noexcept
{
    const auto tick = (std::int64_t) step * ticksPerStep;
    return std::any_of (notes.begin(), notes.end(),
                        [&] (const Note& n) { return n.channel == channel && n.start == tick; });
}

void Pattern::toggleStep (int channel, int step)
{
    const auto tick = (std::int64_t) step * ticksPerStep;
    const auto it = std::find_if (notes.begin(), notes.end(),
                                  [&] (const Note& n) { return n.channel == channel && n.start == tick; });

    if (it != notes.end())
    {
        notes.erase (it);
        return;
    }

    Note n;
    n.start = tick;
    n.channel = channel;
    notes.push_back (n);
    sortNotes();
}

std::int64_t Song::songLength() const noexcept
{
    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (const auto& c : playlist)
        end = std::max (end, c.end());

    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

void Song::removePattern (int index)
{
    if (index < 0 || index >= (int) patterns.size())
        return;

    patterns.erase (patterns.begin() + index);

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::pattern && c.pattern == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern > index)
                --c.pattern;
    });

    if (patterns.empty())
        patterns.emplace_back().name = "Pattern 1";

    currentPattern = std::clamp (currentPattern > index ? currentPattern - 1 : currentPattern, 0, (int) patterns.size() - 1);
}

std::vector<MixerTrack> Song::defaultMixer (int inserts)
{
    std::vector<MixerTrack> mixer ((std::size_t) std::clamp (inserts + 1, 1, maxMixerTracks));
    mixer[0].name = "Master";

    for (std::size_t i = 1; i < mixer.size(); ++i)
        mixer[i].name = "Insert " + std::to_string (i);

    return mixer;
}

int Song::nextFreeInsert() const
{
    for (int t = 1; t < (int) mixer.size(); ++t)
        if (std::none_of (channels.begin(), channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; }))
            return t;

    return std::max (0, (int) mixer.size() - 1);
}

void Song::removeChannel (int index)
{
    if (index < 0 || index >= (int) channels.size())
        return;

    channels.erase (channels.begin() + index);

    for (auto& p : patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [index] (const Note& n) { return n.channel == index; }),
                       p.notes.end());

        for (auto& n : p.notes)
            if (n.channel > index)
                --n.channel;
    }

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::audio && c.channel == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::audio && c.channel > index)
                --c.channel;
    });

    for (auto& a : automations)
    {
        auto& t = a.target;
        const auto onChannel = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                               || t.kind == AutomationTargetKind::channelParam;

        if (onChannel && t.index == index)
            t.kind = AutomationTargetKind::none;
        else if (onChannel && t.index > index)
            --t.index;
    }
}

bool Song::routes (int from, int to) const noexcept
{
    if (from <= 0 || from >= (int) mixer.size())
        return false;

    const auto& t = mixer[(std::size_t) from];

    if (to == 0)
        return t.toMaster;

    return std::any_of (t.sends.begin(), t.sends.end(), [to] (const MixerSend& s) { return s.target == to; });
}

bool Song::canRoute (int from, int to) const
{
    const auto n = (int) mixer.size();

    if (from <= 0 || from >= n || to < 0 || to >= n || from == to)
        return false;

    if (to == 0)
        return true;

    // ciclo se `from` já for alcançável a partir de `to`
    std::vector<int> stack { to };
    std::vector<bool> seen ((std::size_t) n, false);

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        if (t == from)
            return false;

        if (seen[(std::size_t) t])
            continue;

        seen[(std::size_t) t] = true;

        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n)
                stack.push_back (s.target);
    }

    return true;
}

void Song::setRoute (int from, int to, bool enabled, float level)
{
    if (from <= 0 || from >= (int) mixer.size() || to < 0 || to >= (int) mixer.size())
        return;

    auto& t = mixer[(std::size_t) from];

    if (to == 0)
    {
        t.toMaster = enabled;
        return;
    }

    auto it = std::find_if (t.sends.begin(), t.sends.end(), [to] (const MixerSend& s) { return s.target == to; });

    if (! enabled)
    {
        if (it != t.sends.end())
            t.sends.erase (it);
        return;
    }

    if (it != t.sends.end())
        it->level = std::clamp (level, 0.0f, 1.0f);
    else if (canRoute (from, to))
        t.sends.push_back ({ to, std::clamp (level, 0.0f, 1.0f) });
}

std::vector<int> Song::mixerOrder() const
{
    const auto n = (int) mixer.size();
    std::vector<int> incoming ((std::size_t) std::max (0, n), 0);

    for (int t = 1; t < n; ++t)
        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n)
                ++incoming[(std::size_t) s.target];

    // Kahn: começa pelas trilhas que ninguém alimenta (em ordem de número)
    std::vector<int> order, ready;

    for (int t = n - 1; t >= 1; --t)
        if (incoming[(std::size_t) t] == 0)
            ready.push_back (t);

    while (! ready.empty())
    {
        const auto t = ready.back();
        ready.pop_back();
        order.push_back (t);

        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n && --incoming[(std::size_t) s.target] == 0)
            {
                // mantém a ordem numérica entre as que ficam prontas juntas
                auto pos = std::upper_bound (ready.begin(), ready.end(), s.target, std::greater<int>());
                ready.insert (pos, s.target);
            }
    }

    // ciclo (arquivo estragado): as que sobraram vão no fim, sem os envios que fecham o ciclo
    for (int t = 1; t < n; ++t)
        if (std::find (order.begin(), order.end(), t) == order.end())
            order.push_back (t);

    return order;
}

std::vector<bool> Song::soloAudible() const
{
    const auto n = mixer.size();
    std::vector<bool> audible (n, true);
    std::vector<int> soloed;

    for (std::size_t t = 1; t < n; ++t)
        if (mixer[t].solo)
            soloed.push_back ((int) t);

    if (soloed.empty())
        return audible;

    std::fill (audible.begin(), audible.end(), false);

    if (n > 0)
        audible[0] = true;

    // para baixo: o caminho do sinal das trilhas solo até o Master
    std::vector<int> stack = soloed;

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        if (audible[(std::size_t) t])
            continue;

        audible[(std::size_t) t] = true;

        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < (int) n)
                stack.push_back (s.target);
    }

    // para cima: quem alimenta as trilhas solo
    std::vector<bool> feeds (n, false);
    stack = soloed;

    while (! stack.empty())
    {
        const auto t = stack.back();
        stack.pop_back();

        for (std::size_t src = 1; src < n; ++src)
            if (! feeds[src] && routes ((int) src, t))
            {
                feeds[src] = true;
                audible[src] = true;
                stack.push_back ((int) src);
            }
    }

    return audible;
}

void Song::swapEffectSlots (int track, int a, int b)
{
    if (track < 0 || track >= (int) mixer.size())
        return;

    auto& fx = mixer[(std::size_t) track].effects;

    if (a < 0 || b < 0 || a >= (int) fx.size() || b >= (int) fx.size())
        return;

    std::swap (fx[(std::size_t) a], fx[(std::size_t) b]);

    for (auto& au : automations)
        if (au.target.kind == AutomationTargetKind::effectParam && au.target.index == track)
            au.target.slot = au.target.slot == a ? b : au.target.slot == b ? a : au.target.slot;
}

void Song::removeEffectSlot (int track, int slot)
{
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;

    auto& fx = mixer[(std::size_t) track].effects;
    fx.erase (fx.begin() + slot);

    for (auto& au : automations)
        if (au.target.kind == AutomationTargetKind::effectParam && au.target.index == track)
        {
            if (au.target.slot == slot)
                au.target.kind = AutomationTargetKind::none;
            else if (au.target.slot > slot)
                --au.target.slot;
        }
}

void Song::removeAutomation (int index)
{
    if (index < 0 || index >= (int) automations.size())
        return;

    automations.erase (automations.begin() + index);

    forEachClipList ([index] (std::vector<Clip>& clips) {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [index] (const Clip& c) { return c.kind == ClipKind::automation && c.automation == index; }),
                     clips.end());

        for (auto& c : clips)
            if (c.kind == ClipKind::automation && c.automation > index)
                --c.automation;
    });
}

void Song::selectArrangement (int index)
{
    if (arrangements.empty())
        arrangements.push_back ({ "Arrangement 1", {}, 0, 0 });

    currentArrangement = std::clamp (currentArrangement, 0, (int) arrangements.size() - 1);
    index = std::clamp (index, 0, (int) arrangements.size() - 1);

    if (index == currentArrangement)
        return;

    auto& from = arrangements[(std::size_t) currentArrangement];
    from.clips = std::move (playlist);
    from.loopStart = loopStart;
    from.loopEnd = loopEnd;

    auto& to = arrangements[(std::size_t) index];
    playlist = std::move (to.clips);
    to.clips.clear();
    loopStart = to.loopStart;
    loopEnd = to.loopEnd;
    currentArrangement = index;
}

int Song::addArrangement (const std::string& name, bool copyCurrent)
{
    Arrangement a;
    a.name = name;

    if (copyCurrent)
    {
        a.clips = playlist;
        a.loopStart = loopStart;
        a.loopEnd = loopEnd;
    }

    arrangements.push_back (std::move (a));
    selectArrangement ((int) arrangements.size() - 1);
    return currentArrangement;
}

void Song::removeArrangement (int index)
{
    if (arrangements.size() <= 1 || index < 0 || index >= (int) arrangements.size())
        return;

    if (index == currentArrangement)
        selectArrangement (index == 0 ? 1 : index - 1);

    arrangements.erase (arrangements.begin() + index);

    if (currentArrangement > index)
        --currentArrangement;
}

float Song::targetValue (const AutomationTarget& t) const noexcept
{
    const auto validChannel = t.index >= 0 && t.index < (int) channels.size();
    const auto validTrack = t.index >= 0 && t.index < (int) mixer.size();

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: return validChannel ? channels[(std::size_t) t.index].volume : -1.0f;
        case AutomationTargetKind::channelPan: return validChannel ? (channels[(std::size_t) t.index].pan + 1.0f) * 0.5f : -1.0f;
        case AutomationTargetKind::mixerVolume: return validTrack ? mixer[(std::size_t) t.index].volume : -1.0f;
        case AutomationTargetKind::mixerPan: return validTrack ? (mixer[(std::size_t) t.index].pan + 1.0f) * 0.5f : -1.0f;
        case AutomationTargetKind::effectParam:
            if (validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                && mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind != EffectKind::plugin && t.param >= 0
                && t.param < maxEffectParams)
                return mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].params[(std::size_t) t.param];
            return validTrack ? 0.5f : -1.0f; // parâmetro de plugin: o valor mora no plugin
        case AutomationTargetKind::channelParam: return validChannel ? 0.5f : -1.0f;
        case AutomationTargetKind::none: break;
    }

    return -1.0f;
}

float Automation::valueAt (double tick) const noexcept
{
    if (points.empty())
        return 0.5f;

    if (tick <= (double) points.front().tick)
        return points.front().value;

    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const auto& a = points[i - 1];
        const auto& b = points[i];

        if (tick < (double) b.tick)
        {
            const auto span = (double) std::max<std::int64_t> (1, b.tick - a.tick);
            auto t = (float) ((tick - (double) a.tick) / span);

            // tensão: expoente da curva (positiva = sobe devagar e acelera)
            if (a.tension != 0.0f)
                t = std::pow (t, std::pow (2.0f, a.tension * 3.0f));

            return a.value + (b.value - a.value) * t;
        }
    }

    return points.back().value;
}

std::int64_t Automation::length() const noexcept
{
    return points.empty() ? 1 : std::max<std::int64_t> (1, points.back().tick);
}

float faderGain (float position) noexcept
{
    const auto v = std::clamp (position, 0.0f, 1.0f) * 1.25f;
    return (std::pow (11.0f, v) - 1.0f) / 10.0f;
}

Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength)
{
    Song out = song;

    if (out.patterns.empty())
        out.patterns.emplace_back().name = "Pattern 1";

    if (! wholeSong || out.playlist.empty())
    {
        const auto index = std::clamp (out.currentPattern, 0, (int) out.patterns.size() - 1);
        Clip clip;
        clip.pattern = index;
        clip.length = out.patterns[(std::size_t) index].effectiveLength (out.beatsPerBar);
        out.playlist = { clip };
        musicLength = clip.length;
    }
    else
    {
        musicLength = 0;

        for (const auto& c : out.playlist)
            musicLength = std::max (musicLength, c.end());
    }

    // pattern vazia na cauda: estende a música sem tocar nada. Um compasso a mais de folga
    // porque o último bloco renderizado passa um pouco do fim e não pode voltar ao início.
    if (tailTicks > 0)
    {
        tailTicks += (std::int64_t) ppq * std::max (1, out.beatsPerBar);
        out.patterns.emplace_back().name = "(cauda)";
        Clip tail;
        tail.pattern = (int) out.patterns.size() - 1;
        tail.start = musicLength;
        tail.length = tailTicks;
        out.playlist.push_back (tail);
    }

    out.mode = PlayMode::song;
    out.loopStart = out.loopEnd = 0; // exportar ignora a região de loop
    return out;
}

Song Song::makeDefault()
{
    Song song;
    song.mixer = defaultMixer();
    auto channel = [] (const char* name, InstrumentKind kind, float volume, float pan) {
        ChannelDef c;
        c.name = name;
        c.instrument = kind;
        c.volume = volume;
        c.pan = pan;
        return c;
    };

    song.channels = {
        channel ("Kick", InstrumentKind::kick, 0.9f, 0.0f),
        channel ("Clap", InstrumentKind::clap, 0.7f, 0.0f),
        channel ("Hat", InstrumentKind::hat, 0.5f, 0.1f),
        channel ("Snare", InstrumentKind::snare, 0.7f, 0.0f),
        channel ("Synth", InstrumentKind::synth, 0.5f, 0.0f),
    };

    for (std::size_t i = 0; i < song.channels.size(); ++i)
        song.channels[i].mixerTrack = (int) i + 1;

    auto& p = song.patterns.emplace_back();
    p.name = "Pattern 1";

    for (int step : { 0, 4, 8, 12 })
        p.toggleStep (0, step);

    for (int step : { 4, 12 })
        p.toggleStep (1, step);

    for (int step : { 2, 6, 10, 14 })
        p.toggleStep (2, step);

    return song;
}
} // namespace lynx
