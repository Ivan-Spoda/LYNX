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
        case retune: std::snprintf (t, sizeof t, "%.0f ms", retuneMs (v)); return t;
        case humanize:
        case mix: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v); return t;
        case tracking: std::snprintf (t, sizeof t, "%.0f%%", 100.0 * v); return t;
        case formantPreserve:
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
    if (param >= noteC && param < noteC + 12)
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
        const auto sc = (vocal::Scale) scaleIndex (p[scale]);
        auto mask = (std::uint16_t) (vocal::scaleMask (sc, keyIndex (p[key]), notes) & notes);

        // MIDI targets: the pitch classes held on the source channel (none held: the scale)
        const auto ch = midiChannelIndex (p[midiChannel]);
        if (ch >= 0 && held != nullptr && ch < heldCount)
        {
            std::uint16_t m = 0;
            for (int k = 0; k < 128; ++k)
                if ((held[ch][(std::size_t) (k >> 6)] >> (k & 63)) & 1u)
                    m = (std::uint16_t) (m | (1u << (k % 12)));
            if (m != 0)
                mask = m;
        }

        const auto aRef = referenceHz (p[reference]);
        const auto tau = retuneMs (p[retune]) * 0.001;
        const auto human = std::clamp ((double) p[humanize], 0.0, 1.0);
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
                    if (newPhrase)
                    {
                        target = -1; // a new phrase: no hysteresis toward the last note
                        noteAge = 0.0;
                        // ... and no correction left over from the last one: the phrase starts at the sung pitch and
                        // the retune pulls it from there (round 120: the previous phrase's correction used to bend
                        // the first notes of the next one, by up to half a semitone the wrong way)
                        shift = 0.0;
                    }
                    const auto t = vocal::nearestNote (sung, mask, target, hysteresisSt);
                    if (t != target)
                    {
                        target = t;
                        noteAge = 0.0;
                    }
                    else
                        noteAge += hop;
                    wanted = (double) target - sung;
                    if (unvoicedFor > 0.06 && tau <= 0.0)
                        shift = wanted;
                    unvoicedFor = 0.0;
                }
                else
                {
                    psola.setPitch (0.0, false);
                    unvoicedFor += hop;
                    if (unvoicedFor > 0.06)
                        sung = -1.0;
                }
            }

            // retune: the applied correction follows the wanted one; humanize relaxes it on held notes
            auto t = tau;
            if (human > 0.0)
            {
                const auto x = std::clamp ((noteAge - 0.12) / 0.3, 0.0, 1.0);
                t += human * 0.25 * x * x * (3.0 - 2.0 * x);
            }
            shift += (t > 0.0 ? 1.0 - std::exp (-dt / t) : 1.0) * (wanted - shift);

            const auto semis = std::clamp (shift + trans, -24.0, 24.0);
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
    double shift = 0.0, wanted = 0.0, noteAge = 0.0, unvoicedFor = 1e9, sung = -1.0;
    int target = -1;
    const NoteMask* held = nullptr;
    int heldCount = 0;
    std::atomic<int> latencyNow { 1200 };
    std::atomic<float> meterSung { -1.0f }, meterTarget { -1.0f }, meterShift { 0.0f };
};
} // namespace

std::unique_ptr<Effect> make()
{
    auto e = std::make_unique<Tune>();
    e->prepare (44100.0);
    return e;
}
} // namespace lynx::lynxtune
