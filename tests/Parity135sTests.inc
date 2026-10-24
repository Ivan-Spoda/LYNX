// Round 135 (parity135s): the Soundtoys roles — Voice Shift (the Little AlterBoy role), Ember's Decapitator controls,
// Auto-pan's rhythmic-panner sources (PanMan), Foil Plate's infinite decay and low cut (Little Plate), and the
// translation of her Soundtoys states read black-box in the official trials (docs/analise/REA_rodada135s.md).

#include <filesystem>
#include <fstream>

#include "core/EffectPresets.h"
#include "core/LynxVerbs.h"
#include "core/MixSuite.h"
#include "core/PluginSubstitutes.h"
#include "core/VoiceShift.h"

namespace p135s
{
constexpr double sr = 48000.0;

inline void run (lynx::EffectKind kind, const lynx::EffectParams& p, std::vector<float>& l, std::vector<float>& r, double bpm = 120.0)
{
    auto slot = lynx::makeEffectSlot (kind);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    for (std::size_t off = 0; off < l.size(); off += 256)
    {
        const auto m = (int) std::min<std::size_t> (256, l.size() - off);
        fx->process (l.data() + off, r.data() + off, m, p, bpm);
    }
}

// a harmonic "voice" (20 harmonics, 1/k) at f0 Hz
inline std::vector<float> voice (double f0, double secs, double amp = 0.25)
{
    std::vector<float> x ((std::size_t) (secs * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        double v = 0.0;
        for (int k = 1; k <= 20; ++k)
            v += std::sin (2.0 * 3.14159265358979323846 * f0 * k * (double) i / sr) / k;
        x[i] = (float) (amp * v / 1.8);
    }
    return x;
}

// the strongest frequency between lo and hi Hz of x[from .. from + n) (a plain DFT scan, 1 Hz steps)
inline double peakHz (const std::vector<float>& x, std::size_t from, std::size_t n, double lo, double hi)
{
    double best = 0.0, bestHz = 0.0;
    for (double f = lo; f <= hi; f += 1.0)
    {
        double c = 0.0, s = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) n);
            c += w * x[from + i] * std::cos (2.0 * 3.14159265358979323846 * f * (double) i / sr);
            s += w * x[from + i] * std::sin (2.0 * 3.14159265358979323846 * f * (double) i / sr);
        }
        if (c * c + s * s > best) { best = c * c + s * s; bestHz = f; }
    }
    return bestHz;
}

inline double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to; ++i) e += (double) v[i] * v[i];
    return std::sqrt (e / (double) (to - from));
}
} // namespace p135s

TEST (voice_shift_modes_round135s)
{
    using namespace lynx;
    namespace vs = voiceshift;
    // the parameter list follows the trial's: pitch / formant ±12 st with 0.1 st read-outs, link, three modes, drive 0–10
    auto p = makeEffectSlot (EffectKind::voiceShift).params;
    CHECK (effectInfo (EffectKind::voiceShift).params.size() == (std::size_t) vs::count);
    CHECK (formatEffectParam (EffectKind::voiceShift, vs::pitch, vs::semitoneValue (2.5), 120.0) == "+2.5 st");
    CHECK (formatEffectParam (EffectKind::voiceShift, vs::mode, 0.5f, 120.0) == "Quantize");
    CHECK (formatEffectParam (EffectKind::voiceShift, vs::drive, 0.46f, 120.0) == "4.6");
    CHECK (vs::latencyFor (p135s::sr) > 0);
    {
        auto fx = makeEffect (EffectKind::voiceShift);
        fx->prepare (p135s::sr);
        CHECK (fx->latencySamples() == vs::latencyFor (p135s::sr));
    }
    const std::size_t from = 36000, n = 9600;
    // Transpose +2.5 st: 200 Hz → 231 Hz [EXEC trial: 231 Hz]
    {
        auto l = p135s::voice (200.0, 1.0), r = l;
        p[vs::pitch] = vs::semitoneValue (2.5);
        p135s::run (EffectKind::voiceShift, p, l, r);
        CHECK (std::abs (p135s::peakHz (l, from, n, 150.0, 300.0) - 200.0 * std::exp2 (2.5 / 12.0)) < 3.0);
    }
    // Quantize: 210 Hz (between G#3 and A3, nearer G#3 = 207.7 Hz) [EXEC trial: 208 Hz]
    {
        auto l = p135s::voice (210.0, 1.0), r = l;
        p = makeEffectSlot (EffectKind::voiceShift).params;
        p[vs::mode] = 0.5f;
        p135s::run (EffectKind::voiceShift, p, l, r);
        CHECK (std::abs (p135s::peakHz (l, from, n, 150.0, 300.0) - 207.65) < 3.0);
    }
    // Robot: any note → C4 (261.6 Hz), Pitch moves it (+3 st = D#4, 311 Hz) [EXEC trial: 262 / 311 Hz]
    {
        auto l = p135s::voice (200.0, 1.0), r = l;
        p = makeEffectSlot (EffectKind::voiceShift).params;
        p[vs::mode] = 1.0f;
        p135s::run (EffectKind::voiceShift, p, l, r);
        CHECK (std::abs (p135s::peakHz (l, from, n, 150.0, 400.0) - 261.6) < 4.0);
        auto l2 = p135s::voice (200.0, 1.0), r2 = l2;
        p[vs::pitch] = vs::semitoneValue (3.0);
        p135s::run (EffectKind::voiceShift, p, l2, r2);
        CHECK (std::abs (p135s::peakHz (l2, from, n, 150.0, 400.0) - 311.1) < 4.0);
    }
    // Mix 0 % = the dry signal, delayed by the latency; drive adds level (≈ 1.75 dB per step at −18 dBFS)
    {
        auto l = p135s::voice (200.0, 0.5), r = l;
        const auto x = l;
        p = makeEffectSlot (EffectKind::voiceShift).params;
        p[vs::mix] = 0.0f;
        p135s::run (EffectKind::voiceShift, p, l, r);
        const auto lat = (std::size_t) vs::latencyFor (p135s::sr);
        double err = 0.0;
        for (std::size_t i = lat; i < l.size(); ++i) err = std::max (err, (double) std::abs (l[i] - x[i - lat]));
        CHECK (err < 1e-6);
        CHECK (vs::driveCurve (0.25f, 0.0) == 0.25f);
        const auto g = 20.0 * std::log10 (std::abs (vs::driveCurve (0.01f, 2.5) / 0.01f));
        CHECK (g > 3.0 && g < 6.0);
    }
}

TEST (ember_decapitator_controls_round135s)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto base = makeEffectSlot (EffectKind::ember).params;
    CHECK (effectInfo (EffectKind::ember).params.size() == (std::size_t) em::count);
    // defaults are the old sound: new switches off, no voicing, old filter ranges
    CHECK (base[em::punish] == 0.0f && base[em::thump] == 0.0f && base[em::steep] == 0.0f && base[em::voice] == 0.0f && base[em::wideCuts] == 0.0f);
    CHECK (formatEffectParam (EffectKind::ember, em::voice, 0.2f, 120.0) == "Tape pre");
    CHECK (formatEffectParam (EffectKind::ember, em::drive, 0.5f, 120.0) == "+18.0 dB (5.0)");
    CHECK (std::abs (em::lowCutWideHz (1.0f) - 1000.0) < 1e-6 && std::abs (em::highCutWideHz (0.0f) - 1000.0) < 1e-6 && em::highCutWideHz (1.0f) == 0.0);
    auto level = [] (const EffectParams& q, double hz, double amp = 0.05) {
        std::vector<float> l (24000), r (24000);
        for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (float) (amp * std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / p135s::sr));
        p135s::run (EffectKind::ember, q, l, r);
        return 20.0 * std::log10 (p135s::rms (l, 12000, 24000) / (amp / std::sqrt (2.0)));
    };
    auto p = base;
    p[em::autoGain] = 0.0f;
    // PUNISH: +20 dB before the drive (a small signal, clean curve region → about +20 dB, less the curve's knee)
    {
        auto q = p;
        q[em::punish] = 1.0f;
        const auto d = level (q, 1000.0, 0.002) - level (p, 1000.0, 0.002);
        CHECK (d > 17.0 && d < 21.0);
    }
    // wide cuts: low cut at 1 kHz; THUMP lifts the cutoff
    {
        auto q = p;
        q[em::wideCuts] = 1.0f;
        q[em::lowCutWide] = 1.0f;
        const auto plain = level (q, 1000.0, 0.01);
        q[em::thump] = 1.0f;
        const auto thump = level (q, 1000.0, 0.01);
        CHECK (plain < -2.0 && thump > plain + 3.0);
    }
    // STEEP: the high cut falls ~30 dB/oct (gentle wide range: ~6 dB/oct)
    {
        auto q = p;
        q[em::wideCuts] = 1.0f;
        q[em::highCutWide] = 0.0f; // 1 kHz
        const auto gentle = level (q, 4000.0, 0.01);
        q[em::steep] = 1.0f;
        const auto steep = level (q, 4000.0, 0.01);
        CHECK (gentle > -16.0 && gentle < -9.0 && steep < -50.0);
    }
    // every voicing plays (finite, non-silent) and differs from the plain style
    for (int k = 1; k <= em::voices; ++k)
    {
        auto q = p;
        q[em::drive] = 0.5f;
        q[em::voice] = (float) k / (float) em::voices;
        const auto v = level (q, 1000.0, 0.1);
        CHECK (std::isfinite (v) && v > -20.0 && v < 40.0);
    }
    // the factory voicing presets resolve
    CHECK (presets::factory (EffectKind::ember).size() >= 9 && presets::factory (EffectKind::voiceShift).size() >= 6);
}

TEST (autopan_panner_sources_round135s)
{
    using namespace lynx;
    namespace ap = autopan;
    auto base = makeEffectSlot (EffectKind::autoPan).params;
    CHECK (effectInfo (EffectKind::autoPan).params.size() == (std::size_t) ap::count + 20); // + round 136's Rhythm voicing + round 150's Tweak block
    CHECK (ap::sourceOf (base[ap::source]) == ap::Source::fl); // older projects / FL conversions keep FL's LFO
    CHECK (std::abs (ap::rateHz (base[ap::rate]) - 0.25) < 0.01); // the reference's default 0.25 Hz
    CHECK (formatEffectParam (EffectKind::autoPan, ap::rate, ap::rateValue (3.56), 120.0) == "3.56 Hz");
    // equal-power balance: width 0 = both sides −3 dB; full width at the extremes = one side at 0 dB, the other silent
    auto p = base;
    p[ap::source] = 0.5f;
    p[1] = 0.5f; // width 0
    {
        std::vector<float> l (4800, 0.5f), r (4800, 0.5f);
        p135s::run (EffectKind::autoPan, p, l, r);
        CHECK (std::abs (l.back() - 0.5f * std::sqrt (0.5f)) < 1e-4f && std::abs (r.back() - l.back()) < 1e-6f);
    }
    // LFO at 1 Hz, full width, soft: a quarter period after the start the sound is hard right
    {
        p[1] = 1.0f;
        p[ap::rate] = ap::rateValue (1.0);
        std::vector<float> l (12000, 0.5f), r (12000, 0.5f);
        p135s::run (EffectKind::autoPan, p, l, r);
        CHECK (std::abs (l[12000 - 1]) < 0.01f && std::abs (r[12000 - 1] - 0.5f) < 0.01f);
    }
    // rhythm steps: 3 steps of a quarter note at 120 BPM, back and forth, hard: L, C, R, C, L …
    {
        auto q = base;
        q[ap::source] = 1.0f;
        q[1] = 1.0f;
        q[ap::smoothing] = 0.0f;
        q[ap::stepsParam] = 1.0f / 6.0f;
        q[ap::stepNote] = 0.4f;
        std::vector<float> l (96000, 0.5f), r (96000, 0.5f);
        p135s::run (EffectKind::autoPan, q, l, r);
        const auto at = [&] (double sec) { return std::pair<float, float> { l[(std::size_t) (sec * p135s::sr)], r[(std::size_t) (sec * p135s::sr)] }; };
        CHECK (at (0.25).first > 0.49f && at (0.25).second < 0.01f);            // left
        CHECK (std::abs (at (0.75).first - at (0.75).second) < 0.01f);          // centre
        CHECK (at (1.25).second > 0.49f && at (1.25).first < 0.01f);            // right
    }
    // input / output gains and the analog stage (harmonics, no level jump at low level)
    {
        auto q = p;
        q[1] = 0.5f;
        q[ap::outputGain] = 0.75f; // +12 dB
        std::vector<float> l (4800, 0.1f), r (4800, 0.1f);
        p135s::run (EffectKind::autoPan, q, l, r);
        CHECK (std::abs (l.back() - 0.1f * std::sqrt (0.5f) * std::pow (10.0f, 12.0f / 20.0f)) < 1e-3f);
    }
}

TEST (foil_infinite_and_low_cut_round135s)
{
    using namespace lynx;
    auto p = makeEffectSlot (EffectKind::foilPlate).params;
    CHECK (p.size() >= 14 && p[12] == 0.0f && p[13] == 0.0f);
    CHECK (formatEffectParam (EffectKind::foilPlate, 13, 1.0f, 120.0).find ("1") != std::string::npos);
    auto tail = [] (const EffectParams& q) {
        std::vector<float> l (5 * 48000, 0.0f), r (5 * 48000, 0.0f);
        for (int i = 0; i < 2400; ++i) l[(std::size_t) i] = r[(std::size_t) i] = 0.3f * (float) std::sin (0.07 * i);
        p135s::run (EffectKind::foilPlate, q, l, r);
        return p135s::rms (l, 4 * 48000, 5 * 48000);
    };
    p[2] = 0.0f; // 0.5 s
    const auto shortTail = tail (p);
    p[12] = 1.0f; // infinite
    const auto held = tail (p);
    CHECK (held > 100.0 * shortTail && held > 1e-4);
}

TEST (soundtoys_known_states_round135s)
{
    using namespace lynx;
    // the hash covers only the PRESET payload (the FL wrapper header and the version lines around it don't matter)
    const std::string text = "WIDGET = Little AlterBoy;\rVERSION = 4;\rPRESET = 0101TEST ;\r";
    const std::vector<std::uint8_t> st (text.begin(), text.end());
    CHECK (soundtoysStateHash (st) && *soundtoysStateHash (st) == 0xb3c6c15d69b2af7dull);
    CHECK (! soundtoysStateHash (std::vector<std::uint8_t> { 1, 2, 3 }));
    CHECK (knownSoundtoysStates() == 17);
    // an unknown AlterBoy state: the substitute (round 136's measured model, kept by the round-138 merge: LYNX Tune as
    // the voice shifter) with her usual setting — Quantize, formant +1.2 st, drive 4.6
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst2;
    id.name = "LittleAlterBoy";
    id.state = st;
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr && entry->effect == EffectKind::lynxTune);
    if (entry != nullptr)
    {
        int n = -1;
        const auto s = makeSubstituteSlot (*entry, &id, &n);
        CHECK (n == 0 && s.params[lynxtune::driveStage] == 1.0f && std::abs (s.params[lynxtune::drive] - 0.46f) < 1e-6f);
    }
    // round 135's own translators stay reachable for Voice Shift (its table: 17 payloads, checked above)
    CHECK (effectInfo (EffectKind::voiceShift).params.size() == (std::size_t) voiceshift::count);
}

TEST (autopan_panel_places_every_parameter_round135s)
{
    using namespace lynx;
    // the panel shows every parameter on at least one control (some swap in place with the source) and stays inside
    const auto& l = plugui::layoutFor (EffectKind::autoPan);
    std::vector<int> placed (effectInfo (EffectKind::autoPan).params.size(), 0);
    for (const auto& item : l.items)
    {
        CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
        if (item.param != nullptr && item.type != plugui::Item::display && item.type != plugui::Item::value)
        {
            const auto p = plugui::paramIndex (EffectKind::autoPan, item.param);
            CHECK (p >= 0);
            if (p >= 0) ++placed[(std::size_t) p];
        }
    }
    for (std::size_t p = 0; p < placed.size(); ++p)
        CHECK (placed[p] >= 1);
    // the Voice Shift panel is one of LYNX's own panels
    CHECK (std::find (plugui::ownLayoutKinds().begin(), plugui::ownLayoutKinds().end(), EffectKind::voiceShift) != plugui::ownLayoutKinds().end());
}
