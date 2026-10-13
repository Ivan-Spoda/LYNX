#include "core/Song.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

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

    forEachTarget ([index] (AutomationTarget& t) {
        const auto onChannel = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                               || t.kind == AutomationTargetKind::channelParam;

        if (onChannel && t.index == index)
            t.kind = AutomationTargetKind::none;
        else if (onChannel && t.index > index)
            --t.index;
    });

    for (auto& automation : automations) // fonte ao vivo num canal controlador
        if (automation.source.channel == index)
            automation.source.channel = -1, automation.target.kind = AutomationTargetKind::none;
        else if (automation.source.channel > index)
            --automation.source.channel;

    removeDeadMidiLinks();
}

void Song::removeDeadMidiLinks()
{
    midiLinks.erase (std::remove_if (midiLinks.begin(), midiLinks.end(),
                                     [] (const MidiLink& l) { return l.target.kind == AutomationTargetKind::none; }),
                     midiLinks.end());
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

    // arestas: envios e sidechains (a fonte do sidechain precisa ser processada antes de quem a ouve)
    std::vector<std::vector<int>> next ((std::size_t) std::max (0, n));
    for (int t = 1; t < n; ++t)
    {
        for (const auto& s : mixer[(std::size_t) t].sends)
            if (s.target > 0 && s.target < n)
                next[(std::size_t) t].push_back (s.target);
        for (const auto& fx : mixer[(std::size_t) t].effects)
            if (fx.sidechain > 0 && fx.sidechain < n && fx.sidechain != t)
                next[(std::size_t) fx.sidechain].push_back (t);
    }
    for (int t = 1; t < n; ++t)
        for (const auto target : next[(std::size_t) t])
            ++incoming[(std::size_t) target];

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

        for (const auto target : next[(std::size_t) t])
            if (--incoming[(std::size_t) target] == 0)
            {
                // mantém a ordem numérica entre as que ficam prontas juntas
                auto pos = std::upper_bound (ready.begin(), ready.end(), target, std::greater<int>());
                ready.insert (pos, target);
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

    for (auto& automation : automations)
        if (automation.source.track == track)
            automation.source.slot = automation.source.slot == a ? b : automation.source.slot == b ? a : automation.source.slot;

    forEachTarget ([track, a, b] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == track)
            t.slot = t.slot == a ? b : t.slot == b ? a : t.slot;
    });
}

void Song::removeEffectSlot (int track, int slot)
{
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;

    auto& fx = mixer[(std::size_t) track].effects;
    fx.erase (fx.begin() + slot);

    for (auto& automation : automations)
        if (automation.source.track == track)
        {
            if (automation.source.slot == slot)
                automation.source.track = -1, automation.target.kind = AutomationTargetKind::none; // a fonte sumiu
            else if (automation.source.slot > slot)
                --automation.source.slot;
        }

    forEachTarget ([track, slot] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == track)
        {
            if (t.slot == slot)
                t.kind = AutomationTargetKind::none;
            else if (t.slot > slot)
                --t.slot;
        }
    });

    removeDeadMidiLinks();
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

std::vector<Marker>& Song::markers()
{
    if (arrangements.empty())
        arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });

    return arrangements[(std::size_t) std::clamp (currentArrangement, 0, (int) arrangements.size() - 1)].markers;
}

const std::vector<Marker>& Song::markers() const
{
    static const std::vector<Marker> none;
    return arrangements.empty() ? none : arrangements[(std::size_t) std::clamp (currentArrangement, 0, (int) arrangements.size() - 1)].markers;
}

void Song::addMarker (Marker marker)
{
    auto& list = markers();
    const auto at = std::upper_bound (list.begin(), list.end(), marker.tick, [] (std::int64_t t, const Marker& m) { return t < m.tick; });
    list.insert (at, std::move (marker));
}

std::vector<Bar> Song::barsBetween (std::int64_t from, std::int64_t to) const
{
    std::vector<Bar> bars;
    auto barLength = [] (int numerator, int denominator) {
        return std::max<std::int64_t> (1, (std::int64_t) ppq * 4 * std::max (1, numerator) / std::max (1, denominator));
    };

    Bar bar { 0, barLength (beatsPerBar, 4), 1 };
    const auto& list = markers();
    std::size_t next = 0;

    while (bar.tick < to)
    {
        // mudanças de compasso que caem no começo deste compasso valem para ele
        while (next < list.size() && (! list[next].isSignature() || list[next].tick <= bar.tick))
        {
            if (list[next].isSignature())
                bar.length = barLength (list[next].numerator, list[next].denominator);
            ++next;
        }

        // a próxima mudança corta este compasso e começa um novo ali
        const auto end = next < list.size() ? std::min (bar.tick + bar.length, list[next].tick) : bar.tick + bar.length;

        if (end > from)
            bars.push_back ({ bar.tick, end - bar.tick, bar.number });

        bar = { end, bar.length, bar.number + 1 };

        // sem mudanças pela frente: o resto é aritmética (evita andar compasso a compasso até `from`)
        if (bar.tick < from && next >= list.size())
        {
            const auto skip = (from - bar.tick) / bar.length;
            bar.tick += skip * bar.length;
            bar.number += (int) skip;
        }
    }

    return bars;
}

Bar Song::barAt (std::int64_t tick) const
{
    const auto bars = barsBetween (std::max<std::int64_t> (0, tick), std::max<std::int64_t> (0, tick) + 1);
    return bars.empty() ? Bar { 0, (std::int64_t) ppq * std::max (1, beatsPerBar), 1 } : bars.front();
}

void Song::selectArrangement (int index)
{
    if (arrangements.empty())
        arrangements.push_back ({ "Arrangement 1", {}, 0, 0, {} });

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
        a.markers = markers();
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
        case AutomationTargetKind::effectMix:
            return validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                       ? mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].mix
                       : -1.0f;
        case AutomationTargetKind::tempo: return valueFromTempo (bpm);
        case AutomationTargetKind::none: break;
    }

    return -1.0f;
}

bool Song::setTargetValue (const AutomationTarget& t, float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    const auto validChannel = t.index >= 0 && t.index < (int) channels.size();
    const auto validTrack = t.index >= 0 && t.index < (int) mixer.size();

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: if (! validChannel) return false; channels[(std::size_t) t.index].volume = v; return true;
        case AutomationTargetKind::channelPan: if (! validChannel) return false; channels[(std::size_t) t.index].pan = v * 2.0f - 1.0f; return true;
        case AutomationTargetKind::mixerVolume: if (! validTrack) return false; mixer[(std::size_t) t.index].volume = v; return true;
        case AutomationTargetKind::mixerPan: if (! validTrack) return false; mixer[(std::size_t) t.index].pan = v * 2.0f - 1.0f; return true;
        case AutomationTargetKind::tempo: bpm = tempoFromValue (v); return true;
        case AutomationTargetKind::effectMix:
            if (! validTrack || t.slot < 0 || t.slot >= (int) mixer[(std::size_t) t.index].effects.size())
                return false;
            mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].mix = v;
            return true;
        case AutomationTargetKind::effectParam:
            if (validTrack && t.slot >= 0 && t.slot < (int) mixer[(std::size_t) t.index].effects.size()
                && mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind != EffectKind::plugin && t.param >= 0
                && t.param < maxEffectParams)
            {
                mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].params[(std::size_t) t.param] = v;
                return true;
            }
            return false;
        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::none: break;
    }

    return false;
}

float Song::automationValueAt (int index, double tick) const
{
    if (index < 0 || index >= (int) automations.size())
        return std::numeric_limits<float>::quiet_NaN();

    std::vector<const Clip*> clips;

    for (const auto& c : playlist)
        if (c.kind == ClipKind::automation && c.automation == index && c.length > 0 && ! isTrackMuted (c.track))
            clips.push_back (&c);

    if (clips.empty())
        return std::numeric_limits<float>::quiet_NaN();

    std::stable_sort (clips.begin(), clips.end(), [] (const Clip* a, const Clip* b) { return a->start < b->start; });
    const auto& automation = automations[(std::size_t) index];
    const Clip* before = nullptr;

    // mesma regra da reprodução: dentro do clip a curva; entre clips o último valor; antes o primeiro
    for (const auto* c : clips)
    {
        if (tick < (double) c->start)
            break;

        if (tick < (double) c->end())
            return automation.valueAt (tick - (double) c->start + (double) c->offset);

        before = c;
    }

    if (before != nullptr)
        return automation.valueAt ((double) (before->length + before->offset));

    return automation.valueAt ((double) clips.front()->offset);
}

double Song::tempoAt (double tick) const
{
    if (mode != PlayMode::song)
        return bpm;

    for (int i = (int) automations.size() - 1; i >= 0; --i) // a última automação de tempo ganha
        if (automations[(std::size_t) i].target.kind == AutomationTargetKind::tempo)
            if (const auto v = automationValueAt (i, tick); ! std::isnan (v))
                return tempoFromValue (v);

    return bpm;
}

double Song::secondsForTicks (std::int64_t ticks) const
{
    const auto hasTempo = mode == PlayMode::song
                          && std::any_of (automations.begin(), automations.end(),
                                          [] (const Automation& a) { return a.target.kind == AutomationTargetKind::tempo; });

    if (! hasTempo)
        return (double) ticks * 60.0 / (std::max (1.0, bpm) * ppq);

    // integra em passos de 1/16 (a reprodução troca o andamento bloco a bloco; a diferença é mínima)
    double seconds = 0.0;
    constexpr std::int64_t step = ticksPerStep;

    for (std::int64_t t = 0; t < ticks; t += step)
    {
        const auto n = std::min (step, ticks - t);
        seconds += (double) n * 60.0 / (std::max (1.0, tempoAt ((double) t)) * ppq);
    }

    return seconds;
}

float Automation::mapLive (float v) const noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (curve.size() < 2)
        return v;
    const auto x = v * (float) (curve.size() - 1);
    const auto i = std::min ((std::size_t) x, curve.size() - 2);
    const auto f = x - (float) i;
    return std::clamp (curve[i] + (curve[i + 1] - curve[i]) * f, 0.0f, 1.0f);
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

Clip splitClip (Clip& clip, std::int64_t tick)
{
    Clip rest = clip;
    const auto head = std::clamp<std::int64_t> (tick - clip.start, 1, std::max<std::int64_t> (1, clip.length - 1));
    clip.length = head;
    rest.start = clip.start + head;
    rest.offset = clip.offset + head;
    rest.length -= head;
    return rest;
}

double TrackEq::peakQ (float w) noexcept
{
    return 0.0999 * (std::pow (1001.0, std::clamp ((double) w, 0.0, 1.0)) - 1.0) + 0.1;
}

double TrackEq::shelfSlope (float w) noexcept
{
    return 1.9 * std::clamp ((double) w, 0.0, 1.0) + 0.1;
}

std::int64_t swingDelay (std::int64_t start, float swing, float channelMix) noexcept
{
    if (swing <= 0.0f || channelMix <= 0.0f)
        return 0;

    constexpr std::int64_t s = ticksPerStep;
    const auto k = (start + s / 2) / s;

    if (k % 2 == 0)
        return 0;

    const auto w = s - 2 * std::abs (start - k * s);
    return (std::int64_t) std::llround ((double) w * std::clamp (swing, 0.0f, 1.0f) * std::clamp (channelMix, 0.0f, 1.0f) * 0.5);
}

float channelGain (float volume) noexcept
{
    const auto v = std::clamp (volume, 0.0f, 1.0f);
    return (std::pow (11.0f, v) - 1.0f) / 10.0f;
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

Song makeBounceSong (const Song& song, int track)
{
    Song out = song;

    if (track <= 0 || track >= (int) out.mixer.size())
        return out;

    for (auto& t : out.mixer)
        t.solo = false;

    auto& insert = out.mixer[(std::size_t) track];
    insert.sends.clear();
    insert.toMaster = true;
    insert.muted = false;
    insert.solo = true; // soam ele e quem chega nele

    auto& master = out.mixer[0];
    master.effects.clear();
    master.volume = unityFader;
    master.pan = 0.0f;
    master.muted = false;
    out.mode = PlayMode::song;
    return out;
}

int applyBounce (Song& song, int track, const std::string& samplePath, std::int64_t length)
{
    if (track <= 0 || track >= (int) song.mixer.size() || (int) song.mixer.size() >= maxMixerTracks)
        return -1;

    auto& original = song.mixer[(std::size_t) track];
    MixerTrack copy;
    copy.name = original.name + " (áudio)";
    copy.toMaster = original.toMaster;
    copy.sends = original.sends;
    original.muted = true;
    song.mixer.push_back (copy);
    const auto newTrack = (int) song.mixer.size() - 1;

    ChannelDef c;
    c.name = copy.name;
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = samplePath;
    c.mixerTrack = newTrack;
    c.volume = 1.0f;
    song.channels.push_back (c);

    int lane = 0;
    for (const auto& clip : song.playlist)
        lane = std::max (lane, clip.track + 1);

    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = (int) song.channels.size() - 1;
    clip.track = lane;
    clip.length = length;
    song.playlist.push_back (clip);
    return newTrack;
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
        channel ("Clap", InstrumentKind::clap, 0.8f, 0.0f),
        channel ("Hat", InstrumentKind::hat, 0.7f, 0.1f),
        channel ("Snare", InstrumentKind::snare, 0.8f, 0.0f),
        channel ("Synth", InstrumentKind::synth, 0.78f, 0.0f),
    };

    for (std::size_t i = 0; i < song.channels.size(); ++i)
        song.channels[i].mixerTrack = (int) i + 1;

    // como o template do FL: um limiter no Master (transparente até -0,3 dB)
    EffectSlot limiter;
    limiter.kind = EffectKind::limiter;
    limiter.params = { 11.7f / 12.0f, 0.0f, 0.5f };
    song.mixer[0].effects.push_back (limiter);

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
