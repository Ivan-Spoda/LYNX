#pragma once

// Playlist clip operations behind the clip menu (Make unique, Merge, Reverse, Fit to tempo, Select all of a
// source) and the audio clip's time mapping. JUCE-free, header-only; each edits a Song and is run inside
// AudioEngine::editSong, so it is one undo step.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::clipops
{
// Audio frames of the file per Playlist tick for an audio clip's channel: the file's own speed, or, when the
// channel is stretched (stretchBeats > 0, with or without keeping the pitch), the whole file spread over that
// many beats.
inline double framesPerTick (const SamplerSettings& sampler, double fileFrames, double fileRate, double bpm) noexcept
{
    if (sampler.stretchBeats > 0.0f && fileFrames > 0.0)
        return fileFrames / ((double) sampler.stretchBeats * ppq);
    return 60.0 / (std::max (1.0, bpm) * ppq) * fileRate;
}

// Length of the whole file in ticks (as the Playlist plays it).
inline std::int64_t fileTicks (const SamplerSettings& sampler, double fileFrames, double fileRate, double bpm) noexcept
{
    const auto perTick = framesPerTick (sampler, fileFrames, fileRate, bpm);
    return perTick > 0.0 ? (std::int64_t) std::llround (fileFrames / perTick) : 0;
}

// "Fit to tempo": the number of beats a loop of `seconds` most likely spans, assuming it is a power-of-two
// number of beats (1, 2, 4, 8 ... 256) recorded at some other tempo; picks the nearest at the project's tempo.
inline float fitBeats (double seconds, double bpm) noexcept
{
    const auto beats = seconds * std::max (1.0, bpm) / 60.0;
    if (! (beats > 0.0))
        return 0.0f;
    const auto power = std::clamp (std::round (std::log2 (beats)), 0.0, 8.0);
    return (float) std::pow (2.0, power);
}

// Clips (indices into song.playlist) that use the same source as `clip`: the same pattern, the same audio
// channel or the same automation.
inline std::vector<int> sameSource (const Song& song, int clip)
{
    std::vector<int> out;
    if (clip < 0 || clip >= (int) song.playlist.size())
        return out;
    const auto& c = song.playlist[(std::size_t) clip];

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& o = song.playlist[(std::size_t) i];
        if (o.kind != c.kind)
            continue;
        if ((c.kind == ClipKind::pattern && o.pattern == c.pattern) || (c.kind == ClipKind::audio && o.channel == c.channel)
            || (c.kind == ClipKind::automation && o.automation == c.automation))
            out.push_back (i);
    }
    return out;
}

// Audio "Make unique": the clips get their own copy of the audio channel (so reversing, stretching or renaming
// them leaves the other clips of that sample alone). Returns the new channel, or -1.
inline int makeAudioUnique (Song& song, const std::vector<int>& clips)
{
    int source = -1;
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            source = song.playlist[(std::size_t) i].channel;
            break;
        }
    if (source < 0 || source >= (int) song.channels.size())
        return -1;

    auto copy = song.channels[(std::size_t) source];
    // "Name #2", "Name #3"...: the first free number
    const auto base = copy.name;
    for (int n = 2;; ++n)
    {
        const auto candidate = base + " #" + std::to_string (n);
        if (std::none_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == candidate; }))
        {
            copy.name = candidate;
            break;
        }
    }
    song.channels.push_back (std::move (copy));
    const auto channel = (int) song.channels.size() - 1;

    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio
            && song.playlist[(std::size_t) i].channel == source)
            song.playlist[(std::size_t) i].channel = channel;
    return channel;
}

// Merge: clips of the same source on the same track that continue each other (each starts where the previous
// ends, and its offset continues the previous one's) become one clip. Fades at the joins are dropped; the
// first clip's fade-in and the last one's fade-out are kept. Gains and mutes must agree. Returns true if
// anything merged.
inline bool canMerge (const Song& song, const std::vector<int>& clips)
{
    if (clips.size() < 2)
        return false;
    std::vector<Clip> list;
    for (auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            return false;
        list.push_back (song.playlist[(std::size_t) i]);
    }
    std::sort (list.begin(), list.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    const auto& f = list.front();
    if (f.kind == ClipKind::automation)
        return false;

    for (std::size_t i = 1; i < list.size(); ++i)
    {
        const auto& a = list[i - 1];
        const auto& b = list[i];
        if (b.kind != f.kind || b.track != f.track || b.muted != f.muted || std::abs (b.gain - f.gain) > 1.0e-6f
            || (f.kind == ClipKind::pattern && b.pattern != f.pattern) || (f.kind == ClipKind::audio && b.channel != f.channel))
            return false;
        if (b.start != a.end() || b.offset != a.offset + a.length)
            return false;
    }
    return true;
}

inline bool mergeClips (Song& song, std::vector<int> clips)
{
    if (! canMerge (song, clips))
        return false;
    std::sort (clips.begin(), clips.end(), [&song] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });
    auto merged = song.playlist[(std::size_t) clips.front()];
    const auto& last = song.playlist[(std::size_t) clips.back()];
    merged.length = last.end() - merged.start;
    merged.fadeOut = last.fadeOut;

    auto doomed = clips;
    std::sort (doomed.rbegin(), doomed.rend());
    for (auto i : doomed)
        song.playlist.erase (song.playlist.begin() + i);
    song.playlist.push_back (merged);
    return true;
}

// FL's track menu "Merge clips" (REA round 143): on `track`, every run of clips that continue each other (same
// source, as canMerge) becomes one clip. Returns how many clips disappeared.
inline int mergeTrackClips (Song& song, int track)
{
    int removed = 0;
    for (bool again = true; again;)
    {
        again = false;
        std::vector<int> onTrack;
        for (int i = 0; i < (int) song.playlist.size(); ++i)
            if (song.playlist[(std::size_t) i].track == track)
                onTrack.push_back (i);
        std::sort (onTrack.begin(), onTrack.end(), [&song] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });
        for (std::size_t i = 0; i + 1 < onTrack.size() && ! again; ++i)
        {
            std::vector<int> run { onTrack[i] };
            for (auto j = i + 1; j < onTrack.size() && canMerge (song, { run.back(), onTrack[j] }); ++j)
                run.push_back (onTrack[j]);
            if (run.size() > 1 && mergeClips (song, run))
            {
                removed += (int) run.size() - 1;
                again = true;
            }
        }
    }
    return removed;
}

// FL's clip menu "Select source pattern / channel" (REA round 143): the clips (of that kind) play another source.
inline void setClipSource (Song& song, const std::vector<int>& clips, ClipKind kind, int source)
{
    for (auto i : clips)
    {
        if (i < 0 || i >= (int) song.playlist.size() || song.playlist[(std::size_t) i].kind != kind)
            continue;
        auto& c = song.playlist[(std::size_t) i];
        if (kind == ClipKind::pattern && source >= 0 && source < (int) song.patterns.size())
            c.pattern = source;
        else if (kind == ClipKind::audio && source >= 0 && source < (int) song.channels.size())
            c.channel = source;
        else if (kind == ClipKind::automation && source >= 0 && source < (int) song.automations.size())
            c.automation = source;
    }
}

// Reverse an audio clip: the channel plays its file backwards (all clips of that channel, as in FL; "Make
// unique" first to reverse one clip alone). The clip keeps showing the same stretch of time: its offset is
// mirrored so the part that was heard is still the part that plays (backwards).
inline void reverseAudio (Song& song, int clip, std::int64_t wholeFileTicks)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return;
    const auto channel = song.playlist[(std::size_t) clip].channel;
    if (song.playlist[(std::size_t) clip].kind != ClipKind::audio || channel < 0 || channel >= (int) song.channels.size())
        return;

    auto& sampler = song.channels[(std::size_t) channel].sampler;
    sampler.reverse = ! sampler.reverse;

    if (wholeFileTicks <= 0)
        return;
    for (auto& c : song.playlist)
        if (c.kind == ClipKind::audio && c.channel == channel)
        {
            const auto played = std::min (c.length, std::max<std::int64_t> (0, wholeFileTicks - c.offset));
            c.offset = std::max<std::int64_t> (0, wholeFileTicks - (c.offset + played));
        }
}
} // namespace lynx::clipops
