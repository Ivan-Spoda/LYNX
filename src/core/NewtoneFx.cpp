#include "core/NewtoneFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Instruments.h"
#include "core/Scheduler.h"

namespace lynx::newtonefx
{
namespace
{
std::int32_t i32 (const std::uint8_t* p) noexcept
{
    return (std::int32_t) ((std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24));
}

std::int64_t i64 (const std::uint8_t* p) noexcept
{
    return (std::int64_t) ((std::uint64_t) (std::uint32_t) i32 (p) | ((std::uint64_t) (std::uint32_t) i32 (p + 4) << 32));
}

// value of attribute `name` in the XML element starting at `tag` (up to its '>'); nullopt = absent
std::optional<std::string> attribute (const std::string& xml, std::size_t tag, const char* name)
{
    const auto end = xml.find ('>', tag);
    const auto key = std::string (" ") + name + "=\"";
    const auto at = xml.find (key, tag);

    if (at == std::string::npos || (end != std::string::npos && at > end))
        return std::nullopt;

    const auto from = at + key.size();
    const auto to = xml.find ('"', from);

    if (to == std::string::npos)
        return std::nullopt;

    // the five XML entities JUCE writes
    std::string v;
    for (auto i = from; i < to; ++i)
    {
        if (xml[i] == '&')
        {
            static const std::pair<const char*, char> entities[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
            bool done = false;
            for (const auto& [e, c] : entities)
                if (xml.compare (i, std::strlen (e), e) == 0)
                {
                    v += c;
                    i += std::strlen (e) - 1;
                    done = true;
                    break;
                }
            if (done)
                continue;
        }
        v += xml[i];
    }
    return v;
}

double number (const std::string& xml, std::size_t tag, const char* name, double fallback)
{
    const auto v = attribute (xml, tag, name);
    if (! v)
        return fallback;
    char* end = nullptr;
    const auto d = std::strtod (v->c_str(), &end);
    return end != v->c_str() ? d : fallback;
}

// the element names that start with `prefix` + digits, from `from` up to `limit`
std::vector<std::size_t> elements (const std::string& xml, const std::string& prefix, std::size_t from, std::size_t limit)
{
    std::vector<std::size_t> out;
    const auto open = "<" + prefix;

    for (auto at = xml.find (open, from); at != std::string::npos && at < limit; at = xml.find (open, at + 1))
        if (at + open.size() < xml.size() && std::isdigit ((unsigned char) xml[at + open.size()]))
            out.push_back (at);

    return out;
}

bool differs (double a, double b) noexcept { return std::abs (a - b) > 1e-6 * std::max (1.0, std::abs (b)); }

// a note (pitch object) the user changed: pitch, timing, length, volume, formant, drift, vibrato/tremolo added
bool noteEdited (const std::string& xml, std::size_t tag)
{
    const auto n = [&] (const char* name, double fallback) { return number (xml, tag, name, fallback); };

    return differs (n ("fCurPitch", 0.0), n ("fOrigPitch", 0.0)) || differs (n ("iCurLength", 0.0), n ("iOrigLength", 0.0))
           || differs (n ("iCurStartSamplePos", 0.0), n ("iOrigStartSamplePos", 0.0)) || differs (n ("fCurVolume", 1.0), 1.0)
           || differs (n ("fCurFormantFactor", 1.0), 1.0) || differs (n ("fCurrentPitchOffset", 0.0), 0.0)
           || differs (n ("fAdditionalPitchOffset", 0.0), 0.0) || differs (n ("fDrift", 1.0), 1.0)
           || differs (n ("fCurInvStretchFactor", 1.0), 1.0) || differs (n ("fTremAmp", 0.0), 0.0)
           || differs (n ("fVibAmpStart", 0.0), 0.0) || differs (n ("fTimeVariationOffset", 0.0), 0.0)
           || n ("bIsEnabled", 1.0) == 0.0;
}

bool globalEdited (const std::string& xml, std::size_t tag)
{
    const auto n = [&] (const char* name, double fallback) { return number (xml, tag, name, fallback); };

    return differs (n ("GlobalTune", 0.0), 0.0) || differs (n ("GlobalDrift", 1.0), 1.0) || differs (n ("GlobalFormantShift", 1.0), 1.0)
           || differs (n ("GlobalTimeVariation", 0.0), 0.0) || differs (n ("GlobalTimeQuantize", 0.0), 0.0)
           || n ("EnableTempoAdaption", 0.0) != 0.0 || differs (n ("PlaybackOffset", 0.0), 0.0);
}

void readTracks (State& s)
{
    const auto& xml = s.projectXml;
    const auto tracks = xml.find ("<Tracks");

    if (tracks == std::string::npos)
        return;

    const auto trackTags = elements (xml, "Trk_", tracks, xml.size());

    for (std::size_t k = 0; k < trackTags.size(); ++k)
    {
        const auto tag = trackTags[k];
        const auto limit = k + 1 < trackTags.size() ? trackTags[k + 1] : xml.size();
        Track t;
        t.number = (int) number (xml, tag, "TrackNum", (double) k);
        t.timeWarp = attribute (xml, tag, "Type").value_or ("").find ("TimelineWarp") != std::string::npos;
        t.filePath = attribute (xml, tag, "FilePath").value_or ("");

        if (const auto object = xml.find ("<AudioObject", tag); object != std::string::npos && object < limit)
        {
            t.frames = (std::int64_t) number (xml, object, "OriginalFileLength", 0.0);
            t.selfBpm = number (xml, object, "SelfBPM", 0.0);
            t.edited = globalEdited (xml, object);

            for (const auto note : elements (xml, "Pitch_", object, limit))
            {
                ++t.notes;
                t.edited = t.edited || noteEdited (xml, note);
            }
        }

        s.tracks.push_back (std::move (t));
    }
}

// Record 2: FL's VST-wrapper record around the engine's chunk, which is JUCE's binary XML ("VC2!", u32 size, text)
std::string projectXmlOf (const std::uint8_t* d, std::size_t n)
{
    static const std::uint8_t magic[] = { 'V', 'C', '2', '!' };

    for (std::size_t i = 0; i + 8 <= n; ++i)
        if (std::memcmp (d + i, magic, 4) == 0)
        {
            const auto size = (std::size_t) (std::uint32_t) i32 (d + i + 4);
            const auto from = i + 8;
            auto len = std::min (size, n - from);
            while (len > 0 && d[from + len - 1] == 0)
                --len;
            return std::string ((const char*) d + from, len);
        }

    return {};
}

class Engine final : public Effect
{
public:
    explicit Engine (std::shared_ptr<const SampleData> s) : source (std::move (s)) {}

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept override
    {
        engaged = false;
        position = 0;
    }

    void setSongPosition (double t) noexcept override { ticks = t; }
    void setPlaying (bool p) noexcept override { playing = p; }

    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override
    {
        // FL: the engine's transport follows the host only in slave mode (SetPlaying / SongPosChanged), §3.2
        if (params[0] < 0.5f || ! playing || source == nullptr || source->frames() == 0 || bpm <= 0.0)
        {
            engaged = false;
            return;
        }

        const auto tps = ticksPerSample (bpm, sampleRate);

        // play start or a jump: the position comes from the song time (ms × 0.001 × rate in FL)
        if (! engaged || std::abs (ticks - expectedTicks) > 1.0)
            position = (std::int64_t) std::llround (ticks / tps);

        engaged = true;
        expectedTicks = ticks + n * tps;

        const auto frames = (std::int64_t) source->frames();
        const auto* sl = source->leftData();
        const auto* sr = source->rightData();
        const auto ratio = source->sampleRate / sampleRate;

        if (std::abs (ratio - 1.0) < 1e-12)
        {
            // the source's own rate: its samples are added as they are (REA_rodada73 §4, EXEC)
            for (int k = 0; k < n; ++k)
            {
                const auto p = position + k;
                if (p < 0 || p >= frames)
                    continue;
                left[k] += sl[p];
                right[k] += sr[p];
            }
        }
        else
        {
            // another rate: FL's engine output is not a plain resampling (§5); LYNX resamples linearly
            for (int k = 0; k < n; ++k)
            {
                const auto x = (double) (position + k) * ratio;
                const auto p = (std::int64_t) std::floor (x);
                if (p < 0 || p >= frames)
                    continue;
                const auto f = (float) (x - (double) p);
                const auto q = std::min (p + 1, frames - 1);
                left[k] += sl[p] + (sl[q] - sl[p]) * f;
                right[k] += sr[p] + (sr[q] - sr[p]) * f;
            }
        }

        position += n;
    }

private:
    std::shared_ptr<const SampleData> source;
    double sampleRate = 44100.0, ticks = 0.0, expectedTicks = 0.0;
    std::int64_t position = 0;
    bool playing = false, engaged = false;
};
} // namespace

bool State::edited() const noexcept
{
    return std::any_of (tracks.begin(), tracks.end(), [] (const Track& t) { return t.edited; });
}

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;

    State s;
    s.version = i32 (d.data());

    // Newtone loads versions ≤ 0, Newtime ≤ 1 (REA_rodada73 §2)
    if (s.version < 0 || s.version > 1)
        return std::nullopt;

    // records: i32 id, i64 size, data
    std::size_t at = 4;
    bool sawOptions = false;

    while (at + 12 <= d.size())
    {
        const auto id = i32 (d.data() + at);
        const auto size = i64 (d.data() + at + 4);
        at += 12;

        if (size < 0 || (std::uint64_t) size > d.size() - at)
            return std::nullopt;

        const auto* p = d.data() + at;
        const auto n = (std::size_t) size;

        if (id == 1 && n >= 12)
        {
            s.editorWidth = i32 (p);
            s.editorHeight = i32 (p + 4);
            s.flags = (std::uint32_t) i32 (p + 8);
            sawOptions = true;
        }
        else if (id == 2)
            s.projectXml = projectXmlOf (p, n);
        else if (id == 3)
            s.sourcePath.assign ((const char*) p, n);

        at += n;
    }

    if (at != d.size() || ! sawOptions)
        return std::nullopt;

    readTracks (s);
    return s;
}

void toParams (const State& s, EffectParams& params) noexcept
{
    params[0] = s.slave() ? 1.0f : 0.0f;
}

std::string paramText (int param, float v)
{
    if (param == 0)
        return v >= 0.5f ? "on" : "off";
    return {};
}

std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source)
{
    return std::make_unique<Engine> (std::move (source));
}
} // namespace lynx::newtonefx
