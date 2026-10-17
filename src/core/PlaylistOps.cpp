#include "core/PlaylistOps.h"

#include <algorithm>
#include <cstdio>

namespace lynx::clipprops
{
namespace
{
bool validSource (const Song& song, ClipKind kind, int source)
{
    switch (kind)
    {
        case ClipKind::pattern: return source >= 0 && source < (int) song.patterns.size();
        case ClipKind::audio:
            return source >= 0 && source < (int) song.channels.size() && song.channels[(std::size_t) source].instrument == InstrumentKind::audioClip;
        case ClipKind::automation: return source >= 0 && source < (int) song.automations.size();
    }
    return false;
}
} // namespace

bool read (const Song& song, int clip, Props& out)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    const auto& c = song.playlist[(std::size_t) clip];
    Props p;
    p.kind = c.kind;
    p.track = c.track;
    p.start = c.start;
    p.length = c.length;
    p.offset = c.offset;
    p.gain = c.gain;
    p.fadeIn = c.fadeIn;
    p.fadeOut = c.fadeOut;
    p.muted = c.muted;

    if (c.kind == ClipKind::pattern && validSource (song, c.kind, c.pattern))
    {
        const auto& pat = song.patterns[(std::size_t) c.pattern];
        p.source = c.pattern;
        p.name = pat.name;
        p.colour = pat.colour;
    }
    else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
    {
        const auto& ch = song.channels[(std::size_t) c.channel];
        p.source = c.channel;
        p.name = ch.name;
        p.colour = ch.colour;
        p.pitch = ch.sampler.pitch;
        p.stretchBeats = ch.sampler.stretchBeats;
        p.keepPitch = ch.sampler.keepPitch;
    }
    else if (c.kind == ClipKind::automation && validSource (song, c.kind, c.automation))
    {
        p.source = c.automation;
        p.name = song.automations[(std::size_t) c.automation].name;
    }
    out = p;
    return true;
}

bool apply (Song& song, int clip, const Props& p)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    auto& c = song.playlist[(std::size_t) clip];

    if (p.kind == c.kind && validSource (song, c.kind, p.source))
    {
        if (c.kind == ClipKind::pattern)
            c.pattern = p.source;
        else if (c.kind == ClipKind::audio)
            c.channel = p.source;
        else
            c.automation = p.source;
    }

    c.track = std::clamp (p.track, 0, 499);
    c.start = std::max<std::int64_t> (0, p.start);
    c.length = std::max<std::int64_t> (1, p.length);
    c.offset = std::max<std::int64_t> (0, p.offset);
    c.muted = p.muted;

    if (c.kind == ClipKind::audio)
    {
        c.gain = std::clamp (p.gain, 0.0f, 4.0f);
        c.fadeIn = std::clamp<std::int64_t> (p.fadeIn, 0, c.length);
        c.fadeOut = std::clamp<std::int64_t> (p.fadeOut, 0, c.length - c.fadeIn);
    }

    // the source's name and colour (and the audio channel's pitch / stretch)
    if (c.kind == ClipKind::pattern && validSource (song, c.kind, c.pattern))
    {
        auto& pat = song.patterns[(std::size_t) c.pattern];
        if (! p.name.empty())
            pat.name = p.name;
        pat.colour = p.colour;
    }
    else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
    {
        auto& ch = song.channels[(std::size_t) c.channel];
        if (! p.name.empty())
            ch.name = p.name;
        ch.colour = p.colour;
        ch.sampler.pitch = std::clamp (p.pitch, -24.0f, 24.0f);
        ch.sampler.stretchBeats = std::max (0.0f, p.stretchBeats);
        ch.sampler.keepPitch = p.keepPitch && ch.sampler.stretchBeats > 0.0f;
    }
    else if (c.kind == ClipKind::automation && validSource (song, c.kind, c.automation) && ! p.name.empty())
        song.automations[(std::size_t) c.automation].name = p.name;

    return true;
}

void changeMute (Song& song, const std::vector<int>& clips, MuteChange change)
{
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            auto& m = song.playlist[(std::size_t) i].muted;
            switch (change)
            {
                case MuteChange::muteAll: m = true; break;
                case MuteChange::unmuteAll: m = false; break;
                case MuteChange::flip: m = ! m; break;
                case MuteChange::keep: break;
            }
        }
}

void changeGain (Song& song, const std::vector<int>& clips, float value, bool relative)
{
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            auto& g = song.playlist[(std::size_t) i].gain;
            g = std::clamp (relative ? g * value : value, 0.0f, 4.0f);
        }
}

namespace
{
std::string bbt (std::int64_t ticks, int beatsPerBar, int base)
{
    const auto beat = (std::int64_t) ppq;
    const auto bar = beat * std::max (1, beatsPerBar);
    const auto t = std::max<std::int64_t> (0, ticks);
    char text[48];
    std::snprintf (text, sizeof (text), "%lld:%lld:%03lld", (long long) (t / bar + base), (long long) ((t % bar) / beat + base), (long long) (t % beat));
    return text;
}

std::int64_t parseBbt (const std::string& text, int beatsPerBar, int base)
{
    long long parts[3] = { 0, base, 0 };
    int count = 0;
    std::string token;
    auto flush = [&] {
        if (token.empty() || count >= 3)
            return false;
        for (auto c : token)
            if (c < '0' || c > '9')
                return false;
        parts[count++] = std::stoll (token);
        token.clear();
        return true;
    };
    for (auto c : text)
    {
        if (c == ' ')
            continue;
        if (c == ':' || c == '.')
        {
            if (! flush())
                return -1;
        }
        else
            token += c;
    }
    if (! flush())
        return -1;
    if (parts[0] < base || parts[1] < base || parts[1] - base >= std::max (1, beatsPerBar) || parts[2] >= ppq)
        return -1;
    const auto beat = (std::int64_t) ppq;
    return (parts[0] - base) * beat * std::max (1, beatsPerBar) + (parts[1] - base) * beat + parts[2];
}
} // namespace

std::string formatPosition (std::int64_t ticks, int beatsPerBar) { return bbt (ticks, beatsPerBar, 1); }
std::string formatLength (std::int64_t ticks, int beatsPerBar) { return bbt (ticks, beatsPerBar, 0); }
std::int64_t parsePosition (const std::string& text, int beatsPerBar) { return parseBbt (text, beatsPerBar, 1); }
std::int64_t parseLength (const std::string& text, int beatsPerBar) { return parseBbt (text, beatsPerBar, 0); }

std::vector<std::pair<int, std::string>> sourcesFor (const Song& song, ClipKind kind)
{
    std::vector<std::pair<int, std::string>> out;
    if (kind == ClipKind::pattern)
        for (int i = 0; i < (int) song.patterns.size(); ++i)
        {
            if (song.patterns[(std::size_t) i].name == "(tail)")
                continue;
            out.emplace_back (i, song.patterns[(std::size_t) i].name);
        }
    else if (kind == ClipKind::audio)
    {
        for (int i = 0; i < (int) song.channels.size(); ++i)
            if (song.channels[(std::size_t) i].instrument == InstrumentKind::audioClip)
                out.emplace_back (i, song.channels[(std::size_t) i].name);
    }
    else
        for (int i = 0; i < (int) song.automations.size(); ++i)
            out.emplace_back (i, song.automations[(std::size_t) i].name);
    return out;
}
} // namespace lynx::clipprops

namespace lynx::arrangeops
{
const std::vector<Clip>& clipsOf (const Song& song, int index)
{
    static const std::vector<Clip> none;
    if (index == song.currentArrangement)
        return song.playlist;
    if (index < 0 || index >= (int) song.arrangements.size())
        return none;
    return song.arrangements[(std::size_t) index].clips;
}

std::int64_t lengthOf (const Song& song, int index)
{
    std::int64_t end = 0;
    for (const auto& c : clipsOf (song, index))
        end = std::max (end, c.end());
    return end;
}

std::int64_t positionTick (const Song& song, MergePosition position, std::int64_t currentTick)
{
    switch (position)
    {
        case MergePosition::start: return 0;
        case MergePosition::current: return std::max<std::int64_t> (0, currentTick);
        case MergePosition::end:
        {
            const auto end = lengthOf (song, song.currentArrangement);
            const auto bar = (std::int64_t) ppq * std::max (1, song.beatsPerBar);
            return (end + bar - 1) / bar * bar; // the next bar line
        }
    }
    return 0;
}

void clearRange (std::vector<Clip>& clips, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    std::vector<Clip> out;
    for (auto c : clips)
    {
        if (c.end() <= from || c.start >= to)
        {
            out.push_back (c);
            continue;
        }
        if (c.start < from) // keep the head
        {
            auto head = c;
            splitClip (head, from);
            out.push_back (head);
        }
        if (c.end() > to) // keep the end
        {
            auto copy = c;
            out.push_back (splitClip (copy, to));
        }
    }
    clips = std::move (out);
}

bool merge (Song& song, int source, MergeMode mode, std::int64_t at)
{
    if (source == song.currentArrangement || source < 0 || source >= (int) song.arrangements.size())
        return false;

    const auto incoming = song.arrangements[(std::size_t) source].clips; // copy: the playlist may grow
    const auto length = lengthOf (song, source);
    at = std::max<std::int64_t> (0, at);

    if (mode == MergeMode::insert && length > 0)
    {
        // what crosses the position is split; everything after it moves later
        std::vector<Clip> moved;
        for (auto c : song.playlist)
        {
            if (c.start < at && c.end() > at)
            {
                auto rest = splitClip (c, at);
                rest.start += length;
                moved.push_back (c);
                moved.push_back (rest);
            }
            else
            {
                if (c.start >= at)
                    c.start += length;
                moved.push_back (c);
            }
        }
        song.playlist = std::move (moved);
        for (auto& m : song.markers())
            if (m.tick >= at)
                m.tick += length;
    }
    else if (mode == MergeMode::replace)
        clearRange (song.playlist, at, at + length);

    for (auto c : incoming)
    {
        c.start += at;
        song.playlist.push_back (c);
    }

    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) {
        return a.start != b.start ? a.start < b.start : a.track < b.track;
    });
    return true;
}

void move (Song& song, int from, int to)
{
    const auto n = (int) song.arrangements.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    auto item = std::move (song.arrangements[(std::size_t) from]);
    song.arrangements.erase (song.arrangements.begin() + from);
    song.arrangements.insert (song.arrangements.begin() + to, std::move (item));

    auto& cur = song.currentArrangement;
    if (cur == from)
        cur = to;
    else if (from < cur && to >= cur)
        --cur;
    else if (from > cur && to <= cur)
        ++cur;
}

void rename (Song& song, int index, const std::string& name)
{
    if (index >= 0 && index < (int) song.arrangements.size() && ! name.empty())
        song.arrangements[(std::size_t) index].name = name;
}

int duplicate (Song& song, int index)
{
    if (index < 0 || index >= (int) song.arrangements.size())
        return -1;
    Arrangement copy = song.arrangements[(std::size_t) index];
    if (index == song.currentArrangement)
    {
        copy.clips = song.playlist;
        copy.loopStart = song.loopStart;
        copy.loopEnd = song.loopEnd;
    }
    copy.name += " (copy)";
    song.arrangements.insert (song.arrangements.begin() + index + 1, std::move (copy));
    if (song.currentArrangement > index)
        ++song.currentArrangement;
    return index + 1;
}
} // namespace lynx::arrangeops
