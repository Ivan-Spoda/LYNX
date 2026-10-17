#include "core/NewtoneFx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Instruments.h"
#include "core/Scheduler.h"
#include "core/VocalPitch.h"

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
            t.globalFormant = number (xml, object, "GlobalFormantShift", 1.0);

            for (const auto note : elements (xml, "Pitch_", object, limit))
            {
                ++t.notes;
                t.edited = t.edited || noteEdited (xml, note);
                const auto v = [&] (const char* name, double fallback) { return number (xml, note, name, fallback); };
                Note p;
                p.origStart = (std::int64_t) v ("iOrigStartSamplePos", 0.0);
                p.origEnd = (std::int64_t) v ("iOrigEndSamplePos", (double) p.origStart);
                p.curStart = (std::int64_t) v ("iCurStartSamplePos", (double) p.origStart);
                p.curEnd = (std::int64_t) v ("iCurEndSamplePos", (double) p.origEnd);
                p.origPitch = v ("fOrigPitch", 0.0);
                p.shift = v ("fCurrentPitchOffset", 0.0) + v ("fAdditionalPitchOffset", 0.0);
                p.stretch = v ("fCurInvStretchFactor", 1.0);
                p.volume = v ("fCurVolume", 1.0);
                p.formant = v ("fCurFormantFactor", 1.0);
                p.drift = v ("fDrift", 1.0);
                p.smoothIn = v ("fPitchSmoothInTime", 0.0);
                p.smoothOut = v ("fPitchSmoothOutTime", 0.0);
                p.fadeInTime = v ("fVolumeFadeInTime", 0.0);
                p.fadeOutTime = v ("fVolumeFadeOutTime", 0.0);
                p.fadeInGain = v ("fVolumeFadeInGain", 1.0);
                p.fadeOutGain = v ("fVolumeFadeOutGain", 1.0);
                p.vibPeriod = v ("fVibFreq", 0.0);
                p.vibAmpStart = v ("fVibAmpStart", 0.0);
                p.vibAmpEnd = v ("fVibAmpEnd", 0.0);
                p.tremAmp = v ("fTremAmp", 0.0);
                p.enabled = v ("bIsEnabled", 1.0) != 0.0;
                if (! std::isfinite (p.stretch) || p.stretch <= 0.0)
                    p.stretch = 1.0;
                t.pitches.push_back (p);
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

bool Note::edited (double globalFormant) const noexcept
{
    return differs (shift, 0.0) || differs (drift, 1.0) || differs (stretch, 1.0) || curStart != origStart
           || differs (volume, 1.0) || differs (formant * globalFormant, 1.0) || ! enabled || vibPeriod > 0.0
           || differs (tremAmp, 0.0) || differs (fadeInGain, 1.0) || differs (fadeOutGain, 1.0);
}

namespace
{
constexpr double twoPi = 6.283185307179586;

// Analysis marks of the source (LYNX's own analysis, not Newtone's): one per period where voiced, locked to the
// peaks of a low-passed copy; every `unvoicedHop` samples elsewhere
struct Mark
{
    double pos = 0.0, period = 0.0, note = 0.0;
    bool voiced = false;
};

std::vector<Mark> analyse (const std::vector<float>& x, double fs, std::vector<vocal::PitchFrame>& frames, int hop, int unvoicedHop)
{
    const auto n = (int) x.size();
    frames = vocal::analysePitch (x.data(), n, fs, hop, 60.0, 1000.0);
    std::vector<float> env (x.size());
    const auto a = (float) (1.0 - std::exp (-twoPi * 900.0 / fs));
    float z = 0.0f;
    for (int i = 0; i < n; ++i)
        env[(std::size_t) i] = z += a * (x[(std::size_t) i] - z);

    std::vector<Mark> marks;
    double pos = 0.0;
    while (pos < n)
    {
        const auto f = std::min (frames.size() - 1, (std::size_t) std::lround (pos / hop));
        const auto hz = frames.empty() ? 0.0 : frames[f].hz;
        if (hz > 0.0)
        {
            const auto p = fs / hz;
            const auto from = std::max (0, (int) std::ceil (pos - 0.25 * p)), to = std::min (n - 1, (int) std::floor (pos + 0.25 * p));
            auto best = std::clamp ((int) std::lround (pos), 0, n - 1);
            for (int i = from; i <= to; ++i)
                if (env[(std::size_t) i] - 1e-4f * (float) std::abs (i - pos) / (float) p > env[(std::size_t) best] - 1e-4f * (float) std::abs (best - pos) / (float) p)
                    best = i;
            if (! marks.empty() && best <= marks.back().pos)
                best = (int) std::ceil (marks.back().pos + 1.0);
            marks.push_back ({ (double) best, p, vocal::hzToNote (hz), true });
            pos = best + p;
        }
        else
        {
            marks.push_back ({ pos, (double) unvoicedHop, 0.0, false });
            pos += unvoicedHop;
        }
    }
    return marks;
}

float readAt (const std::vector<float>& x, double pos) noexcept
{
    const auto i = (std::int64_t) std::floor (pos);
    const auto n = (std::int64_t) x.size();
    if (i < 1 || i + 2 >= n)
        return 0.0f;
    const auto t = (float) (pos - (double) i);
    const auto y0 = x[(std::size_t) i - 1], y1 = x[(std::size_t) i], y2 = x[(std::size_t) i + 1], y3 = x[(std::size_t) i + 2];
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
} // namespace

std::shared_ptr<SampleData> renderEdits (const SampleData& source, const Track& track)
{
    const auto n = (std::int64_t) source.frames();
    if (n <= 0 || track.timeWarp || track.pitches.empty())
        return nullptr;
    const auto& notes = track.pitches;
    if (std::none_of (notes.begin(), notes.end(), [&] (const Note& p) { return p.edited (track.globalFormant); }))
        return nullptr;

    const auto fs = source.sampleRate > 0.0 ? source.sampleRate : 44100.0;
    const auto stereo = source.isStereo();
    std::vector<float> in[2];
    in[0].assign (source.leftData(), source.leftData() + n);
    if (stereo)
        in[1].assign (source.rightData(), source.rightData() + n);
    std::vector<float> mono = in[0];
    if (stereo)
        for (std::int64_t i = 0; i < n; ++i)
            mono[(std::size_t) i] = 0.5f * (in[0][(std::size_t) i] + in[1][(std::size_t) i]);

    const int hop = 128, unvoicedHop = std::max (16, (int) std::lround (128.0 * fs / 44100.0));
    std::vector<vocal::PitchFrame> frames;
    const auto marks = analyse (mono, fs, frames, hop, unvoicedHop);
    auto voicedAt = [&] (double pos) {
        if (frames.empty())
            return false;
        const auto f = std::min (frames.size() - 1, (std::size_t) std::max<double> (0.0, std::round (pos / hop)));
        return frames[f].hz > 0.0;
    };

    // drift centre: the note's mean sung pitch over its voiced frames [model: REA_rodada95 §2.3, "about the mean"]
    std::vector<double> centre (notes.size(), 0.0);
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        double sum = 0.0;
        int count = 0;
        for (auto f = std::max<std::int64_t> (0, notes[k].origStart / hop); f < std::min<std::int64_t> ((std::int64_t) frames.size(), notes[k].origEnd / hop + 1); ++f)
            if (frames[(std::size_t) f].hz > 0.0)
            {
                sum += vocal::hzToNote (frames[(std::size_t) f].hz);
                ++count;
            }
        centre[k] = count > 0 ? sum / count : notes[k].origPitch;
    }

    // the span each note owns: [curStart, curEnd), minus what an earlier note still covers
    std::int64_t length = n;
    std::vector<std::pair<std::int64_t, std::int64_t>> owned (notes.size());
    std::int64_t covered = 0;
    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto a = std::max (notes[k].curStart, covered), b = notes[k].curEnd;
        owned[k] = { a, std::max (a, b) };
        covered = std::max (covered, b);
        length = std::max (length, b);
    }

    // pitch shift (semitones) of note k at output sample t, with the transitions to its neighbours (§2.4)
    auto adjacent = [&] (std::size_t j, std::size_t k) {
        return notes[j].curEnd == notes[k].curStart && voicedAt ((double) notes[k].origStart);
    };
    auto shiftAt = [&] (std::size_t k, double t) {
        const auto& p = notes[k];
        auto s = p.shift;
        if (k > 0 && adjacent (k - 1, k) && differs (notes[k - 1].shift, p.shift) && p.smoothIn > 0.0)
        {
            const auto b = (double) p.curStart;
            const auto v0 = notes[k - 1].shift + 0.0435 * (p.shift - notes[k - 1].shift);
            if (t >= b && t < b + p.smoothIn)
                s = v0 + (p.shift - v0) * (t - b) / p.smoothIn;
        }
        if (k + 1 < notes.size() && adjacent (k, k + 1) && differs (notes[k + 1].shift, p.shift))
        {
            const auto b = (double) p.curEnd;
            if (t >= b - 0.5 * p.smoothOut && t < b)
                s = p.shift + 0.0435 * (notes[k + 1].shift - p.shift);
        }
        if (p.vibPeriod > 0.0 && p.curEnd > p.curStart)
        {
            const auto u = (t - (double) p.curStart) / (double) (p.curEnd - p.curStart);
            const auto amp = p.vibAmpStart + (p.vibAmpEnd - p.vibAmpStart) * std::clamp (u, 0.0, 1.0);
            s += amp * std::sin (twoPi * (t - (double) p.curStart) / (p.vibPeriod * fs));
        }
        return s;
    };

    auto out = std::make_shared<SampleData>();
    out->sampleRate = fs;
    out->left.assign ((std::size_t) length, 0.0f);
    if (stereo)
        out->right.assign ((std::size_t) length, 0.0f);
    std::vector<float>* dst[2] { &out->left, &out->right };
    const auto channels = stereo ? 2 : 1;

    // TD-PSOLA of note k over output samples [a, b): grains at synthesis marks one target period apart, each taken
    // from the analysis mark nearest the mapped source time; then the note's gain
    std::vector<float> acc[2];
    auto render = [&] (std::size_t k, std::int64_t a, std::int64_t b) {
        const auto& p = notes[k];
        const auto margin = (std::int64_t) std::ceil (2.0 * fs / 60.0) + 4;
        const auto from = a - margin, to = b + margin;
        for (int c = 0; c < channels; ++c)
            acc[c].assign ((std::size_t) (to - from), 0.0f);
        const auto formant = std::clamp (p.formant * track.globalFormant, 0.25, 4.0);
        auto ts = (double) from;
        while (ts < (double) to)
        {
            const auto src = (double) p.origStart + (ts - (double) p.curStart) * p.stretch;
            const auto it = std::lower_bound (marks.begin(), marks.end(), src, [] (const Mark& m, double v) { return m.pos < v; });
            const Mark* mk = nullptr;
            if (it != marks.end())
                mk = &*it;
            if (it != marks.begin() && (mk == nullptr || std::abs (std::prev (it)->pos - src) < std::abs (mk->pos - src)))
                mk = &*std::prev (it);
            const auto voiced = mk != nullptr && mk->voiced;
            double ha, hs, r = 1.0, f = 1.0, ta = src;
            if (voiced)
            {
                const auto semis = shiftAt (k, ts) + (p.drift - 1.0) * (mk->note - centre[k]);
                r = std::exp2 (std::clamp (semis, -36.0, 36.0) / 12.0);
                f = formant;
                ha = mk->period;
                hs = ha / f;
                ta = mk->pos;
            }
            else
                ha = hs = unvoicedHop;
            const auto gain = (float) (f / r);
            const auto o0 = (std::int64_t) std::ceil (ts - hs), o1 = (std::int64_t) std::floor (ts + hs);
            for (auto o = std::max (o0, from); o <= std::min (o1, to - 1); ++o)
            {
                const auto u = ((double) o - ts) / hs;
                if (u <= -1.0 || u >= 1.0)
                    continue;
                const auto w = gain * (float) (0.5 + 0.5 * std::cos (3.141592653589793 * u));
                const auto at = ta + ((double) o - ts) * f;
                for (int c = 0; c < channels; ++c)
                    acc[c][(std::size_t) (o - from)] += w * readAt (in[c], at);
            }
            ts += voiced ? ha / r : ha;
        }
        // the note's gain: volume, fade-in from fadeInGain over fadeInTime, fade-out to fadeOutGain [model]
        for (auto t = a; t < b; ++t)
        {
            auto g = p.volume;
            const auto since = (double) (t - p.curStart), until = (double) (p.curEnd - t);
            if (p.fadeInTime > 0.0 && since < p.fadeInTime)
                g *= p.fadeInGain + (1.0 - p.fadeInGain) * since / p.fadeInTime;
            if (p.fadeOutTime > 0.0 && until < p.fadeOutTime)
                g *= p.fadeOutGain + (1.0 - p.fadeOutGain) * until / p.fadeOutTime;
            for (int c = 0; c < channels; ++c)
            {
                const auto v = (float) g * acc[c][(std::size_t) (t - from)];
                (*dst[c])[(std::size_t) t] = std::isfinite (v) ? v : 0.0f;
            }
        }
    };

    for (std::size_t k = 0; k < notes.size(); ++k)
    {
        const auto& p = notes[k];
        const auto [a, b] = owned[k];
        if (a >= b || ! p.enabled)
            continue; // a disabled note plays silence (§2.4)
        // the sample itself where nothing changes (unedited notes in place); rendered: edited notes, and the
        // transition regions an edited neighbour gives an unedited note (§2.4)
        auto plainAt = [&] (int c, std::int64_t t) {
            const auto src = p.origStart + (t - p.curStart);
            return src >= 0 && src < n ? in[c][(std::size_t) src] : 0.0f;
        };
        for (auto t = a; t < b; ++t)
            for (int c = 0; c < channels; ++c)
                (*dst[c])[(std::size_t) t] = plainAt (c, t);

        std::vector<std::pair<std::int64_t, std::int64_t>> ranges;
        if (p.edited (track.globalFormant))
            ranges.push_back ({ a, b });
        else
        {
            if (k > 0 && adjacent (k - 1, k) && differs (notes[k - 1].shift, p.shift) && p.smoothIn > 0.0)
                ranges.push_back ({ a, std::min (b, a + (std::int64_t) std::ceil (p.smoothIn)) });
            if (k + 1 < notes.size() && adjacent (k, k + 1) && differs (notes[k + 1].shift, p.shift))
            {
                const auto x = std::max (a, (std::int64_t) std::floor ((double) p.curEnd - 0.5 * p.smoothOut));
                if (! ranges.empty() && x <= ranges.back().second)
                    ranges.back().second = b;
                else
                    ranges.push_back ({ x, b });
            }
        }
        for (const auto& [x, y] : ranges)
        {
            if (x >= y)
                continue;
            render (k, x, y);
            // short cross-fades where a rendered span meets the plain sample (no click at the switch)
            const auto fade = std::min<std::int64_t> (64, (y - x) / 2);
            for (std::int64_t i = 0; i < fade; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos (3.14159265f * (float) (i + 1) / (float) (fade + 1));
                for (int c = 0; c < channels; ++c)
                {
                    if (x > a || x == p.curStart)
                    {
                        auto& v = (*dst[c])[(std::size_t) (x + i)];
                        v = plainAt (c, x + i) + w * (v - plainAt (c, x + i));
                    }
                    if (y < b)
                    {
                        auto& v = (*dst[c])[(std::size_t) (y - 1 - i)];
                        v = plainAt (c, y - 1 - i) + w * (v - plainAt (c, y - 1 - i));
                    }
                }
            }
        }
    }
    if (! stereo)
        out->right.clear();
    return out;
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

std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& state)
{
    if (source != nullptr)
        if (const auto s = parse (state); s && ! s->tracks.empty() && ! s->tracks.front().timeWarp)
            if (auto edited = renderEdits (*source, s->tracks.front()))
                return std::make_unique<Engine> (std::move (edited));
    return make (std::move (source));
}
} // namespace lynx::newtonefx
