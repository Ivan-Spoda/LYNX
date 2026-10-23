#include "core/LynxTune.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "core/Effects.h"
#include "core/VocalPitch.h"

namespace lynx::lynxtune
{
namespace
{
constexpr double maxRate = 192000.0;
// how far past the half-way point (semitones) a held target is kept: enough to stop a wobbly note between two
// neighbours from flipping, small enough that a glide is not pulled back toward the note it left (round 120: 0.1
// biased a glide's correction by ~10 c against its direction; Waves Tune Real-Time measured shows no such bias)
constexpr double hysteresisSt = 0.03;

struct RangeInfo
{
    const char* name;
    double fmin, fmax;
};
// voice ranges: the lowest pitch sets the detector window and the latency
constexpr RangeInfo ranges[rangeCount] { { "Soprano", 150.0, 1200.0 }, { "Alto / Tenor", 80.0, 1000.0 },
                                         { "Low male", 55.0, 700.0 }, { "Instrument", 40.0, 1200.0 } };
constexpr const char* keyNames[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

int stepped (float v, int steps) noexcept { return std::clamp ((int) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (steps - 1)), 0, steps - 1); }
} // namespace

const char* rangeName (int r) noexcept { return ranges[std::clamp (r, 0, rangeCount - 1)].name; }
double rangeMinHz (int r) noexcept { return ranges[std::clamp (r, 0, rangeCount - 1)].fmin; }
double rangeMaxHz (int r) noexcept { return ranges[std::clamp (r, 0, rangeCount - 1)].fmax; }
int rangeIndex (float v) noexcept { return stepped (v, rangeCount); }
int scaleIndex (float v) noexcept { return stepped (v, vocal::scaleCount); }
int keyIndex (float v) noexcept { return stepped (v, 12); }
double retuneMs (float v) noexcept { return 400.0 * (double) v * (double) v; }
double formantSemitones (float v) noexcept { return 14.0 * ((double) std::clamp (v, 0.0f, 1.0f) - 0.5); }
int transposeSemitones (float v) noexcept { return stepped (v, 25) - 12; }
double referenceHz (float v) noexcept { return 430.0 + 20.0 * (double) std::clamp (v, 0.0f, 1.0f); }
int midiChannelIndex (float v) noexcept
{
    const auto k = (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 128.0f);
    return k <= 0 ? -1 : k - 1;
}

double naturalVibratoDb (float v) noexcept { return 24.0 * ((double) std::clamp (v, 0.0f, 1.0f) - 0.5); }
int vibratoShapeIndex (float v) noexcept { return stepped (v, vibratoShapes); }
double vibratoRateHz (float v) noexcept { return 0.1 + 9.9 * (double) std::clamp (v, 0.0f, 1.0f); }
double vibratoDepthCents (float v) noexcept { return 100.0 * (double) std::clamp (v, 0.0f, 1.0f); }
double vibratoOnsetMs (float v) noexcept { return 1500.0 * (double) std::clamp (v, 0.0f, 1.0f); }
int effectiveScale (float scaleValue, float moreScalesValue) noexcept
{
    switch (stepped (moreScalesValue, 3))
    {
        case 1: return (int) vocal::Scale::wholeTone;
        case 2: return (int) vocal::Scale::diminished;
        default: return scaleIndex (scaleValue);
    }
}

int latencyFor (double sampleRate, int range, bool lowLatency) noexcept
{
    const auto pmax = sampleRate / rangeMinHz (range);
    return (int) std::ceil ((lowLatency ? 1.25 : 2.0) * pmax) + 4;
}

const EffectInfo& info()
{
    static const EffectInfo i = [] {
        EffectInfo e { "LYNX Tune", {} };
        e.params = { { "Key", 0.0f },
                     { "Scale", 1.0f / (float) (vocal::scaleCount - 1) }, // Major
                     { "Retune", 0.2236068f },                           // 20 ms
                     { "Humanize", 0.0f },
                     { "Formant preserve", 1.0f },
                     { "Formant shift", 0.5f },
                     { "Transpose", 0.5f },
                     { "Reference", 0.5f }, // A4 = 440 Hz
                     { "Range", 1.0f / 3.0f },  // alto / tenor
                     { "Low latency", 0.0f },
                     { "MIDI", 0.0f },
                     { "Mix", 1.0f },
                     { "Tracking", 0.4f } };
        for (const auto* n : keyNames)
            e.params.push_back ({ n, 1.0f });
        e.params.push_back ({ "Flex-Tune", 0.0f });
        // round 128
        e.params.push_back ({ "Natural vibrato", 0.5f });
        static const char* const bypassNames[12] { "C bypass", "C# bypass", "D bypass", "D# bypass", "E bypass", "F bypass",
                                                   "F# bypass", "G bypass", "G# bypass", "A bypass", "A# bypass", "B bypass" };
        for (const auto* n : bypassNames)
            e.params.push_back ({ n, 0.0f });
        e.params.push_back ({ "Ignore vibrato", 0.0f });
        e.params.push_back ({ "Vibrato shape", 0.0f });
        e.params.push_back ({ "Vibrato rate", (float) ((5.5 - 0.1) / 9.9) }); // 5.5 Hz
        e.params.push_back ({ "Vibrato depth", 0.18f });                        // 18 c
        e.params.push_back ({ "Onset delay", 1.0f / 3.0f });                    // 500 ms
        e.params.push_back ({ "Onset rate", 1.0f / 3.0f });                     // 500 ms
        e.params.push_back ({ "More scales", 0.0f });
        // round 135f
        e.params.push_back ({ "Learn MIDI", 0.0f });
        e.params.push_back ({ "Classic", 0.0f });
        return e;
    }();
    return i;
}

std::string paramText (int param, float v)
{
    char t[48];
    auto onOff = [v] { return std::string (v >= 0.5f ? "On" : "Off"); };
    switch (param)
    {
        case key: return keyNames[keyIndex (v)];
        case scale: return vocal::scaleName ((vocal::Scale) scaleIndex (v));
        case naturalVibrato: std::snprintf (t, sizeof t, "%+.1f dB", naturalVibratoDb (v)); return t;
        case ignoreVibrato: return onOff();
        case vibratoShape: { static const char* n[vibratoShapes] { "No vibrato", "Sine", "Square", "Sawtooth" }; return n[vibratoShapeIndex (v)]; }
        case vibratoRate: std::snprintf (t, sizeof t, "%.1f Hz", vibratoRateHz (v)); return t;
        case vibratoDepth: std::snprintf (t, sizeof t, "%.0f c", vibratoDepthCents (v)); return t;
        case vibratoDelay:
        case vibratoOnset: std::snprintf (t, sizeof t, "%.0f ms", vibratoOnsetMs (v)); return t;
        case moreScales: { static const char* n[3] { "Off", "Whole tone", "Diminished" }; return n[stepped (v, 3)]; }
        case retune: std::snprintf (t, sizeof t, "%.0f ms", retuneMs (v)); return t;
        case humanize:
        case flexTune:
        case mix: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v); return t;
        case tracking: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v); return t;
        case formantPreserve:
        case learnMidi:
        case classicMode:
        case lowLatency: return onOff();
        case formantShift: std::snprintf (t, sizeof t, "%+.1f st", formantSemitones (v)); return t;
        case transpose: std::snprintf (t, sizeof t, "%+d st", transposeSemitones (v)); return t;
        case reference: std::snprintf (t, sizeof t, "%.1f Hz", referenceHz (v)); return t;
        case range: return rangeName (rangeIndex (v));
        case midiChannel:
        {
            const auto c = midiChannelIndex (v);
            if (c < 0)
                return "Off";
            std::snprintf (t, sizeof t, "Channel %d", c + 1);
            return t;
        }
        default: break;
    }
    if ((param >= noteC && param < noteC + 12) || (param >= bypassC && param < bypassC + 12))
        return onOff();
    std::snprintf (t, sizeof t, "%.2f", (double) v);
    return t;
}

namespace
{
class Tune final : public Effect
{
public:
    Tune() : dry ((std::size_t) (2 * dryRing), 0.0f) {}

    void prepare (double sampleRate) noexcept override
    {
        sr = std::clamp (sampleRate > 0.0 ? sampleRate : 44100.0, 8000.0, maxRate);
        configure (curRange, curLow);
    }

    void reset() noexcept override
    {
        det.reset();
        psola.reset();
        std::fill (dry.begin(), dry.end(), 0.0f);
        dryPos = 0;
        shift = 0.0;
        wanted = 0.0;
        target = -1;
        noteAge = 0.0;
        unvoicedFor = 1e9;
        sung = -1.0;
        center1 = center2 = 0.0;
        extraWanted = extra = 0.0;
        noteTime = 0.0;
        lfoPhase = 0.0;
    }

    void setHeldNotes (const NoteMask* channels, int count) noexcept override
    {
        held = channels;
        heldCount = count;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto rg = rangeIndex (p[range]);
        const auto low = p[lowLatency] >= 0.5f;
        if (rg != curRange || low != curLow)
            configure (rg, low);

        det.setThreshold ((float) (0.05 + 0.25 * std::clamp (p[tracking], 0.0f, 1.0f)));

        // the target notes: the scale on the key, minus the notes switched off
        std::uint16_t notes = 0;
        for (int k = 0; k < 12; ++k)
            if (p[(std::size_t) (noteC + k)] >= 0.5f)
                notes = (std::uint16_t) (notes | (1u << k));
        const auto sc = (vocal::Scale) effectiveScale (p[scale], p[moreScales]);
        std::uint16_t bypass = 0;
        for (int k = 0; k < 12; ++k)
            if (p[(std::size_t) (bypassC + k)] >= 0.5f)
                bypass = (std::uint16_t) (bypass | (1u << k));
        // round 128: natural vibrato (gain on the vibrato already sung), ignore vibrato, created vibrato
        const auto natGain = std::pow (10.0, naturalVibratoDb (p[naturalVibrato]) / 20.0);
        const auto natural = std::abs (natGain - 1.0) > 1e-9;
        const auto ignoreVib = p[ignoreVibrato] >= 0.5f;
        const auto vibShape = vibratoShapeIndex (p[vibratoShape]);
        const auto vibInc = vibratoRateHz (p[vibratoRate]) / sr;
        const auto vibDepth = vibratoDepthCents (p[vibratoDepth]) / 100.0;
        const auto vibDelay = vibratoOnsetMs (p[vibratoDelay]) * 0.001;
        const auto vibOnset = vibratoOnsetMs (p[vibratoOnset]) * 0.001;
        auto mask = (std::uint16_t) (vocal::scaleMask (sc, keyIndex (p[key]), notes) & notes);

        // MIDI targets: the pitch classes held on the source channel (none held: the scale)
        const auto ch = midiChannelIndex (p[midiChannel]);
        std::uint16_t heldMask = 0;
        if (ch >= 0 && held != nullptr && ch < heldCount)
        {
            for (int k = 0; k < 128; ++k)
                if ((held[ch][(std::size_t) (k >> 6)] >> (k & 63)) & 1u)
                    heldMask = (std::uint16_t) (heldMask | (1u << (k % 12)));
            if (heldMask != 0 && p[learnMidi] < 0.5f) // learning (round 135): the notes teach the scale instead
                mask = heldMask;
        }
        meterHeld.store ((float) heldMask / 4096.0f, std::memory_order_relaxed);
        const bool classic = p[classicMode] >= 0.5f;

        const auto aRef = referenceHz (p[reference]);
        const auto tau = retuneMs (p[retune]) * 0.001;
        const auto human = std::clamp ((double) p[humanize], 0.0, 1.0);
        const auto flex = std::clamp ((double) p[flexTune], 0.0, 1.0);
        const auto trans = (double) transposeSemitones (p[transpose]);
        const auto fShift = std::exp2 (formantSemitones (p[formantShift]) / 12.0);
        const auto preserve = p[formantPreserve] >= 0.5f;
        const auto wet = std::clamp (p[mix], 0.0f, 1.0f);
        const auto dt = 1.0 / sr;
        const auto stereo = r != nullptr && r != l;

        for (int i = 0; i < n; ++i)
        {
            auto clean = [] (float v) { return std::isfinite (v) ? v : 0.0f; };
            const float xin[2] { clean (l[i]), clean (stereo ? r[i] : l[i]) };
            const auto mid = stereo ? 0.5f * (xin[0] + xin[1]) : xin[0];

            if (det.push (mid))
            {
                const auto& e = det.estimate();
                const auto hop = (double) detHop / sr;
                if (e.voiced && e.hz > 0.0)
                {
                    psola.setPitch (e.period, true);
                    sung = vocal::hzToNote (e.hz, aRef);
                    const auto newPhrase = unvoicedFor > 0.06;
                    // steadiness (humanize): how long the sung pitch has stayed within a few cents of its own recent
                    // average. [EXEC] Auto-Tune Pro (round 120 §5): even a ±5 c vibrato is not a sustained note
                    // A single move (the singer settles on another pitch) keeps the note sustained; another move 30 …
                    // 150 ms later (a vibrato, a wobble) makes it unsteady.
                    sinceJump += hop;
                    // the pitch with its vibrato smoothed away (two one-poles, 80 ms): ignore vibrato / natural vibrato
                    if (newPhrase)
                        center1 = center2 = sung;
                    else
                    {
                        const auto a = 1.0 - std::exp (-hop / 0.08);
                        center1 += a * (sung - center1);
                        center2 += a * (center1 - center2);
                    }
                    extraWanted = natural ? (natGain - 1.0) * (sung - center2) : 0.0;
                    const auto heard = sung + extraWanted; // the sung pitch with the natural-vibrato gain
                    if (newPhrase)
                    {
                        steadyRef = sung;
                        noteAge = 0.0;
                        sinceJump = 1.0;
                    }
                    else if (std::abs (sung - steadyRef) > 0.04)
                    {
                        steadyRef = sung;
                        if (sinceJump >= 0.03 && sinceJump < 0.15) // (estimates < 30 ms apart: the same move)
                            noteAge = 0.0;
                        sinceJump = 0.0;
                    }
                    else
                    {
                        steadyRef += (1.0 - std::exp (-hop / 0.05)) * (sung - steadyRef);
                        noteAge += hop;
                    }
                    if (newPhrase)
                    {
                        target = -1; // a new phrase: no hysteresis toward the last note
                        // ... and no correction left over from the last one: the phrase starts at the sung pitch and
                        // the retune pulls it from there (round 120: the previous phrase's correction used to bend
                        // the first notes of the next one, by up to half a semitone the wrong way)
                        shift = 0.0;
                    }
                    const auto t = vocal::nearestNote (ignoreVib ? center2 : sung, mask, target, hysteresisSt);
                    if (t != target)
                    {
                        target = t;
                        noteAge = 0.0;
                        noteTime = 0.0;
                    }
                    else
                        noteTime += hop;
                    if (newPhrase)
                        noteTime = 0.0;
                    wanted = (double) target - heard;
                    if (((bypass >> (((target % 12) + 12) % 12)) & 1u) != 0)
                        wanted = 0.0; // a bypassed note passes as sung
                    // flex-tune: a note far from its target is left as sung. [EXEC] Auto-Tune Pro (round 120 §5): the
                    // correction of a note d cents from its target is min (d, (1 − F) / F · (50 − d)) cents, d measured
                    // in hundredths of the gap to the next target on the sung side (a semitone in chromatic)
                    if (flex > 0.0)
                    {
                        const auto up = sung > (double) target;
                        int gap = 1;
                        while (gap < 12 && mask != 0 && ((mask >> (((target + (up ? gap : -gap)) % 12 + 12) % 12)) & 1u) == 0)
                            ++gap;
                        const auto d = std::abs (wanted) * 100.0 / (double) gap;
                        const auto keep = d > 1e-6 ? std::clamp ((1.0 - flex) / flex * (50.0 - d) / d, 0.0, 1.0) : 1.0;
                        wanted *= keep;
                    }
                    if (unvoicedFor > 0.06 && tau <= 0.0)
                        shift = wanted;
                    unvoicedFor = 0.0;
                }
                else
                {
                    psola.setPitch (0.0, false);
                    extraWanted = 0.0;
                    unvoicedFor += hop;
                    if (unvoicedFor > 0.06)
                        sung = -1.0;
                }
            }

            // retune: the applied correction follows the wanted one; humanize slows it on sustained (steady) notes.
            // [EXEC] Auto-Tune Pro (round 120 §5): a note held steady for ~0.6 s is retuned as if the retune speed were
            // longer by the humanize amount (100 % = +100 ms of its speed = +135 ms of LYNX's time constant)
            auto t = tau;
            if (human > 0.0)
            {
                const auto x = std::clamp ((noteAge - 0.2) / 0.4, 0.0, 1.0);
                t += human * 0.135 * x * x * (3.0 - 2.0 * x);
            }
            if (classic) // round 135: a constant-rate glide, one semitone per retune time
            {
                const auto step = t > 0.0 ? dt / t : 1e9;
                shift += std::clamp (wanted - shift, -step, step);
            }
            else
                shift += (t > 0.0 ? 1.0 - std::exp (-dt / t) : 1.0) * (wanted - shift);

            auto semis = shift + trans;
            if (natural || extra != 0.0)
            {
                extra += (1.0 - std::exp (-dt / 0.003)) * (extraWanted - extra);
                semis += extra;
            }
            if (vibShape > 0)
            {
                lfoPhase += vibInc;
                lfoPhase -= std::floor (lfoPhase);
                if (unvoicedFor <= 0.0 && noteTime > vibDelay)
                {
                    const auto env = vibOnset > 0.0 ? std::min (1.0, (noteTime - vibDelay) / vibOnset) : 1.0;
                    const auto w = vibShape == 1 ? std::sin (2.0 * 3.14159265358979323846 * lfoPhase)
                                 : vibShape == 2 ? (lfoPhase < 0.5 ? 1.0 : -1.0)
                                                 : 2.0 * lfoPhase - 1.0;
                    semis += vibDepth * env * w;
                }
            }
            semis = std::clamp (semis, -24.0, 24.0);
            const auto ratio = std::exp2 (semis / 12.0);
            psola.setShift (ratio, preserve ? fShift : ratio * fShift);

            float y[2];
            psola.process (xin, y, stereo ? 2 : 1);

            // the dry signal, delayed like the wet one
            const auto w = (std::size_t) (dryPos & (dryRing - 1));
            const auto rd = (std::size_t) ((dryPos - latency) & (dryRing - 1));
            dry[w] = xin[0];
            dry[w + dryRing] = xin[1];
            const auto d0 = dry[rd], d1 = dry[rd + dryRing];
            ++dryPos;

            l[i] = d0 + wet * (y[0] - d0);
            if (stereo)
                r[i] = d1 + wet * (y[1] - d1);
        }

        meterSung.store ((float) sung, std::memory_order_relaxed);
        meterTarget.store ((float) target, std::memory_order_relaxed);
        meterShift.store ((float) shift, std::memory_order_relaxed);
    }

    // 0: sung note / 127 (0 = none), 1: target note / 127, 2: applied correction (0.5 ± semitones / 4)
    float controlOutput (int o) const noexcept override
    {
        switch (o)
        {
            case 0: return std::max (0.0f, meterSung.load (std::memory_order_relaxed)) / 127.0f;
            case 1: return std::max (0.0f, meterTarget.load (std::memory_order_relaxed)) / 127.0f;
            case 2: return std::clamp (0.5f + meterShift.load (std::memory_order_relaxed) / 4.0f, 0.0f, 1.0f);
            case 3: return meterHeld.load (std::memory_order_relaxed);
            default: return 0.0f;
        }
    }

    int latencySamples() const noexcept override { return latencyNow.load (std::memory_order_relaxed); }

private:
    static constexpr int dryRing = 16384;

    void configure (int rg, bool low) noexcept
    {
        curRange = rg;
        curLow = low;
        detHop = std::max (16, (int) std::lround (128.0 * sr / 44100.0));
        det.prepare (sr, rangeMinHz (rg), rangeMaxHz (rg), detHop);
        latency = std::min (latencyFor (sr, rg, low), dryRing - 1);
        psola.prepare (sr, sr / rangeMinHz (rg), latency);
        psola.setMarkLag (det.windowCentre());
        latencyNow.store (latency, std::memory_order_relaxed);
        reset();
    }

    double sr = 44100.0;
    int curRange = 1, detHop = 128, latency = 1200;
    bool curLow = false;
    vocal::PitchDetector det;
    vocal::Psola psola;
    std::vector<float> dry;
    std::int64_t dryPos = 0;
    double shift = 0.0, wanted = 0.0, noteAge = 0.0, steadyRef = 0.0, sinceJump = 1.0, unvoicedFor = 1e9, sung = -1.0;
    double center1 = 0.0, center2 = 0.0, extraWanted = 0.0, extra = 0.0, noteTime = 0.0, lfoPhase = 0.0;
    int target = -1;
    const NoteMask* held = nullptr;
    int heldCount = 0;
    std::atomic<int> latencyNow { 1200 };
    std::atomic<float> meterSung { -1.0f }, meterTarget { -1.0f }, meterShift { 0.0f }, meterHeld { 0.0f };
};
} // namespace

std::unique_ptr<Effect> make()
{
    auto e = std::make_unique<Tune>();
    e->prepare (44100.0);
    return e;
}
} // namespace lynx::lynxtune
