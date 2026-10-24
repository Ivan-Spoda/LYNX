#include "core/MixerOps.h"

#include <algorithm>
#include <numeric>

namespace lynx::mixerops
{
namespace
{
bool isInsert (const Song& song, int t) noexcept
{
    return t > 0 && t < (int) song.mixer.size();
}

template <typename Fn>
void forEachSlot (std::vector<EffectSlot>& slots, Fn&& fn)
{
    for (auto& slot : slots)
    {
        fn (slot);
        for (auto& branch : slot.branches)
            forEachSlot (branch.chain, fn);
    }
}

bool isTrackTarget (AutomationTargetKind k) noexcept
{
    return k == AutomationTargetKind::mixerVolume || k == AutomationTargetKind::mixerPan || k == AutomationTargetKind::effectParam
           || k == AutomationTargetKind::effectMix;
}
} // namespace

void swapTracks (Song& song, int a, int b)
{
    if (a == b || ! isInsert (song, a) || ! isInsert (song, b))
        return;

    auto map = [a, b] (int t) { return t == a ? b : t == b ? a : t; };
    std::swap (song.mixer[(std::size_t) a], song.mixer[(std::size_t) b]);

    for (auto& c : song.channels)
        c.mixerTrack = map (c.mixerTrack);

    for (auto& track : song.mixer)
    {
        for (auto& send : track.sends)
            send.target = map (send.target);

        forEachSlot (track.effects, [&map] (EffectSlot& slot) {
            if (slot.sidechain > 0)
                slot.sidechain = map (slot.sidechain);
            if (slot.sidechain2 > 0)
                slot.sidechain2 = map (slot.sidechain2);
        });
    }

    song.forEachTarget ([&map] (AutomationTarget& t) {
        if (isTrackTarget (t.kind))
            t.index = map (t.index);
    });

    for (auto& automation : song.automations)
        if (automation.source.track > 0)
            automation.source.track = map (automation.source.track);
}

bool moveTracks (Song& song, std::vector<int>& selection, int delta)
{
    const auto count = (int) song.mixer.size();
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int t) { return t <= 0 || t >= count; }), selection.end());

    if (selection.empty() || delta == 0 || (delta < 0 && selection.front() == 1) || (delta > 0 && selection.back() == count - 1))
        return false;

    if (delta < 0)
    {
        for (auto& t : selection) // left to right: each swaps with its left neighbour
        {
            swapTracks (song, t, t - 1);
            --t;
        }
    }
    else
    {
        for (auto it = selection.rbegin(); it != selection.rend(); ++it)
        {
            swapTracks (song, *it, *it + 1);
            ++*it;
        }
    }

    return true;
}

namespace
{
// default names follow their insert's number (after an insert or a delete moved them)
void renumberDefaultNames (Song& song, int from, int delta)
{
    for (int t = std::max (1, from); t < (int) song.mixer.size(); ++t)
    {
        auto& name = song.mixer[(std::size_t) t].name;
        const auto old = t - delta;
        if (old >= 1 && name == "Insert " + std::to_string (old))
            name = "Insert " + std::to_string (t);
    }
}
} // namespace

bool insertTrack (Song& song, int at)
{
    const auto count = (int) song.mixer.size();
    if (at < 1 || at > count || count >= maxMixerTracks)
        return false;

    song.mixer.emplace_back().name = "Insert " + std::to_string (at);
    for (int t = count; t > at; --t)
        swapTracks (song, t, t - 1);
    renumberDefaultNames (song, at + 1, 1);
    return true;
}

bool deleteTrack (Song& song, int t)
{
    const auto count = (int) song.mixer.size();
    if (t < 1 || t >= count)
        return false;

    for (auto& c : song.channels)
        if (c.mixerTrack == t)
            c.mixerTrack = 0;

    for (auto& track : song.mixer)
    {
        track.sends.erase (std::remove_if (track.sends.begin(), track.sends.end(), [t] (const MixerSend& s) { return s.target == t; }), track.sends.end());
        forEachSlot (track.effects, [t] (EffectSlot& slot) {
            if (slot.sidechain == t)
                slot.sidechain = 0;
            if (slot.sidechain2 == t)
                slot.sidechain2 = 0;
        });
    }

    for (auto& automation : song.automations)
        if (automation.source.track == t)
            automation.source.track = -1; // no longer live

    song.mixer[(std::size_t) t] = MixerTrack {};
    for (int i = t; i + 1 < count; ++i)
        swapTracks (song, i, i + 1);
    renumberDefaultNames (song, t, -1);
    song.mixer.back().name = "Insert " + std::to_string (count - 1);
    return true;
}

std::vector<int> linkChannels (Song& song, const std::vector<int>& channels, int track, bool startingHere)
{
    std::vector<int> result;

    if (song.mixer.empty())
        song.mixer = Song::defaultMixer (0);

    track = std::clamp (track, 0, (int) song.mixer.size() - 1);
    auto next = track;

    for (auto channel : channels)
    {
        if (channel < 0 || channel >= (int) song.channels.size())
        {
            result.push_back (0);
            continue;
        }

        while (next >= (int) song.mixer.size() && (int) song.mixer.size() < maxMixerTracks)
        {
            auto& added = song.mixer.emplace_back();
            added.name = "Insert " + std::to_string (song.mixer.size() - 1);
        }

        const auto target = std::min (next, (int) song.mixer.size() - 1);
        song.channels[(std::size_t) channel].mixerTrack = target;
        result.push_back (target);

        if (startingHere && track > 0)
            ++next;
    }

    return result;
}

std::vector<int> channelsOn (const Song& song, int track)
{
    std::vector<int> out;

    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].mixerTrack == track)
            out.push_back (c);

    return out;
}

int routeTo (Song& song, const std::vector<int>& sources, int to, bool only)
{
    int routed = 0;

    for (auto from : sources)
    {
        if (! isInsert (song, from) || from == to || (to != 0 && ! song.canRoute (from, to) && ! song.routes (from, to)))
            continue;

        if (only)
        {
            auto& track = song.mixer[(std::size_t) from];
            track.sends.erase (std::remove_if (track.sends.begin(), track.sends.end(), [to] (const MixerSend& s) { return s.target != to; }),
                               track.sends.end());
            track.toMaster = to == 0;
        }

        song.setRoute (from, to, true);
        ++routed;
    }

    return routed;
}

int sidechainTo (Song& song, int from, int to)
{
    if (! isInsert (song, from) || ! isInsert (song, to) || from == to)
        return 0;

    if (! song.routes (from, to))
    {
        if (! song.canRoute (from, to))
            return 0;
        song.setRoute (from, to, true, 0.0f);
    }

    int linked = 0;
    for (auto& fx : song.mixer[(std::size_t) to].effects)
        if ((fx.kind == EffectKind::compressor || fx.kind == EffectKind::vocoder) && (fx.sidechain < 0 || fx.sidechain == from))
        {
            fx.sidechain = from;
            ++linked;
        }

    return linked;
}

bool moveEffectSlot (Song& song, int track, int from, int to)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;

    const auto count = (int) song.mixer[(std::size_t) track].effects.size();

    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;

    for (auto s = from; s != to; s += to > from ? 1 : -1)
        song.swapEffectSlots (track, s, s + (to > from ? 1 : -1));

    return true;
}

void setAllEffectsEnabled (Song& song, int track, bool enabled)
{
    if (track >= 0 && track < (int) song.mixer.size())
        for (auto& fx : song.mixer[(std::size_t) track].effects)
            fx.enabled = enabled;
}

bool anyEffectEnabled (const Song& song, int track) noexcept
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;

    const auto& fx = song.mixer[(std::size_t) track].effects;
    return std::any_of (fx.begin(), fx.end(), [] (const EffectSlot& s) { return s.enabled; });
}
void resetToDefault (Song& song, const std::vector<int>& tracks)
{
    for (auto t : tracks)
        if (t >= 0 && t < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) t];
            m.volume = unityFader;
            m.pan = 0.0f;
            m.stereo = 0.0f;
            m.invertPhase = m.swapChannels = false;
            m.eq = TrackEq {};
            m.muted = m.solo = false;
        }
}

void resetRouting (Song& song, const std::vector<int>& tracks)
{
    for (auto t : tracks)
        if (t > 0 && t < (int) song.mixer.size())
        {
            auto& m = song.mixer[(std::size_t) t];
            m.sends.clear();
            m.toMaster = true;
            m.masterLevel = 1.0f;
        }
}

int unrouteInto (Song& song, const std::vector<int>& tracks)
{
    int removed = 0;
    for (int from = 1; from < (int) song.mixer.size(); ++from)
    {
        auto& m = song.mixer[(std::size_t) from];
        for (auto t : tracks)
        {
            if (t == from)
                continue;
            if (t == 0 && m.toMaster && ! m.sends.empty())
            {
                m.toMaster = false; // (it still goes somewhere else)
                ++removed;
            }
            const auto before = m.sends.size();
            m.sends.erase (std::remove_if (m.sends.begin(), m.sends.end(), [t] (const MixerSend& s) { return s.target == t; }), m.sends.end());
            removed += (int) (before - m.sends.size());
        }
        if (m.sends.empty() && ! m.toMaster)
            m.toMaster = true;
    }
    return removed;
}

std::vector<int> destinationsOf (const Song& song, int track)
{
    std::vector<int> out;
    if (track <= 0 || track >= (int) song.mixer.size())
        return out;
    const auto& m = song.mixer[(std::size_t) track];
    if (m.toMaster)
        out.push_back (0);
    for (const auto& s : m.sends)
        if (std::find (out.begin(), out.end(), s.target) == out.end())
            out.push_back (s.target);
    std::sort (out.begin(), out.end());
    return out;
}

std::vector<int> sourcesOf (const Song& song, int track)
{
    std::vector<int> out;
    for (int from = 1; from < (int) song.mixer.size(); ++from)
        if (from != track && song.routes (from, track))
            out.push_back (from);
    return out;
}

int insertTracks (Song& song, int at, int count)
{
    int done = 0;
    for (int i = 0; i < count && insertTrack (song, at + i); ++i)
        ++done;
    return done;
}
} // namespace lynx::mixerops
