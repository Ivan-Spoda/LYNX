// --- Round 147: Prism Multiband's measured Dynamic phase (Pro-MB, REA_rodada147 §1) ---------------------------------

#include "core/MinPhaseBand.h"

namespace p147
{
namespace m = lynx::dyn::prism;
inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }

// a steady sine's gain (dB) through the effect
inline double toneGain (const lynx::EffectParams& p, double hz)
{
    std::vector<float> x (24000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.25 * std::sin (2.0 * dyn108::pi * hz * (double) i / dyn108::sr));
    const auto y = dyn108::run (lynx::EffectKind::prismMultiband, p, x);
    return dyn108::rmsDb (y, 12000, 18000) - dyn108::rmsDb (x, 12000, 18000);
}

// two static bands split at `hz` (24 dB/oct): levels in dB (range 0: no dynamics)
inline lynx::EffectParams twoBands (double lowDb, double highDb, double hz = 1000.0)
{
    auto p = dyn108::defaults (lynx::EffectKind::prismMultiband);
    p[m::bandCountParam] = m::bandCountValue (2);
    p[m::xover1] = m::crossoverValue (hz);
    for (int b = 0; b < 2; ++b)
        p[(std::size_t) m::param (b, m::fRange)] = 0.5f;
    p[(std::size_t) m::param (0, m::fLevel)] = (float) ((lowDb + 30.0) / 60.0);
    p[(std::size_t) m::param (1, m::fLevel)] = (float) ((highDb + 30.0) / 60.0);
    return p;
}
} // namespace p147

TEST (prism_measured_dynamic_phase_filters_round147)
{
    using namespace lynx;
    using p147::dbToGain;
    using p147::gainToDb;
    namespace mp = minphase;
    // [EXEC] Pro-MB 1.3.3 at 48 kHz, Dynamic phase, static band levels: magnitude (dB) and phase (degrees) at 1 kHz
    struct Case { double lo, hi; int n; double gainDb, magDb, phaseDeg; };
    const Case cases[] {
        { 0.0, 1000.0, 2, -12.0, -4.08, 53.0 },     // low band to 1 kHz, 24 dB/oct
        { 0.0, 1000.0, 1, -12.0, -4.08, 36.8 },     // 12 dB/oct
        { 0.0, 1000.0, 2, 12.0, 7.93, -53.0 },      // +12 dB
        { 1000.0, 0.0, 2, -12.0, -4.07, -53.0 },    // high band from 1 kHz
        { 300.0, 3000.0, 2, -12.0, -11.50, 2.6 },   // a middle band 300 Hz – 3 kHz
        { 300.0, 3000.0, 4, -12.0, -11.99, 2.0 },   // 48 dB/oct
    };
    for (const auto& c : cases)
    {
        mp::Band b;
        const auto wl = c.lo > 0.0 ? mp::warp (c.lo, 48000.0) : 0.0, wh = c.hi > 0.0 ? mp::warp (c.hi, 48000.0) : 0.0;
        b.design (wl, wh, c.n, dbToGain (c.gainDb));
        const auto h = b.response (2.0 * mp::pi * 1000.0 / 48000.0);
        const auto mag = gainToDb (std::abs (h)), ph = std::arg (h) * 180.0 / mp::pi;
        std::printf ("  Prism dynamic band %.0f-%.0f Hz n=%d %+.0f dB: 1 kHz %.2f dB %.1f deg (Pro-MB %.2f dB %.1f deg)\n",
                     c.lo, c.hi, c.n, c.gainDb, mag, ph, c.magDb, c.phaseDeg);
        CHECK (std::abs (mag - c.magDb) < 0.1);
        CHECK (std::abs (ph - c.phaseDeg) < 1.0);
        // the designed magnitude is the linear-phase one everywhere
        for (const double hz : { 50.0, 200.0, 700.0, 2000.0, 8000.0, 16000.0 })
            CHECK (std::abs (std::abs (b.response (2.0 * mp::pi * hz / 48000.0)) - mp::bandMagnitude (wl, wh, c.n, dbToGain (c.gainDb), mp::warp (hz, 48000.0))) < 1e-6);
        // stable: the impulse response dies away
        b.reset();
        double tail = 0.0;
        for (int i = 0; i < 96000; ++i)
        {
            const auto y = b.process (i == 0 ? 1.0 : 0.0);
            if (i > 90000)
                tail += y * y;
        }
        CHECK (tail < 1e-20);
    }
}

TEST (prism_measured_dynamic_phase_engine_round147)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // new instances run the measured engine; the text of the switch
    const auto def = makeEffectSlot (EffectKind::prismMultiband).params;
    CHECK (m::measuredDynamic (def));
    CHECK ((int) effectInfo (EffectKind::prismMultiband).params.size() == m::count);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::dynamicEngine, 1.0f, 120.0) == "measured");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::dynamicEngine, 0.0f, 120.0) == "classic");

    // [EXEC] the low band at −12 dB below 1 kHz: −12.0 / −10.6 / −4.1 / −0.4 / 0.0 dB (Pro-MB, dynamic = linear magnitude)
    const double hz[] { 50.0, 500.0, 1000.0, 2000.0, 10000.0 };
    const double measured[] { -12.0, -10.6, -4.1, -0.4, 0.0 };
    const double gains[m::bands] { -12.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    for (int k = 0; k < 5; ++k)
    {
        const auto p = p147::twoBands (-12.0, 0.0);
        const auto g = p147::toneGain (p, hz[k]);
        std::printf ("  Prism measured dynamic: %.0f Hz %.2f dB (Pro-MB %.1f)\n", hz[k], g, measured[k]);
        CHECK (std::abs (g - measured[k]) < 0.15);
        CHECK (std::abs (m::responseDb (p, gains, 48000.0, hz[k]) - g) < 0.15); // the editor's curve
    }
    // [EXEC] the bands are in cascade: two adjacent bands at −12 dB give −8.15 dB at their crossover (not −12),
    // −12 / +6 dB give −0.57 dB
    const auto both = p147::toneGain (p147::twoBands (-12.0, -12.0), 1000.0);
    const auto mixed = p147::toneGain (p147::twoBands (-12.0, 6.0), 1000.0);
    std::printf ("  Prism measured dynamic, two bands: -12/-12 %.2f dB (Pro-MB -8.15), -12/+6 %.2f dB (Pro-MB -0.57)\n", both, mixed);
    CHECK (std::abs (both + 8.15) < 0.15);
    CHECK (std::abs (mixed + 0.57) < 0.15);

    // at rest the effect is the input, exactly (bands that cannot work are not run)
    std::vector<float> noise (8192);
    std::uint32_t seed = 11;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) ((double) (seed >> 8) / 16777216.0 - 0.5);
    }
    const auto y = dyn108::run (EffectKind::prismMultiband, p147::twoBands (0.0, 0.0), noise);
    double err = 0.0;
    for (std::size_t i = 0; i < noise.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - noise[i]));
    CHECK (err < 1e-6);

    // the classic engine stays for older projects: +2.76 dB at the crossover (round 144b's note)
    auto classic = p147::twoBands (-12.0, 0.0);
    classic[m::dynamicEngine] = 0.0f;
    const auto old = p147::toneGain (classic, 1000.0);
    CHECK (std::abs (old - 2.76) < 0.2);

    // oversampled: the same static magnitudes
    auto os = p147::twoBands (-12.0, 0.0);
    os[m::oversampling] = 1.0f;
    CHECK (std::abs (p147::toneGain (os, 50.0) + 12.0) < 0.15);
    CHECK (std::abs (p147::toneGain (os, 1000.0) + 4.1) < 0.15);

    // a working compressor band: a loud 100 Hz tone in band 1 is turned down; the high band passes untouched
    auto comp = dyn108::defaults (EffectKind::prismMultiband);
    for (int b = 1; b < m::bands; ++b)
        comp[(std::size_t) m::param (b, m::fEnabled)] = 0.0f;
    const auto g100 = p147::toneGain (comp, 100.0), g12k = p147::toneGain (comp, 12000.0);
    std::printf ("  Prism measured dynamic, compressing band 1: 100 Hz %.2f dB, 12 kHz %.2f dB\n", g100, g12k);
    CHECK (g100 < -1.5);
    CHECK (std::abs (g12k) < 0.1);
}

TEST (prism_old_projects_keep_classic_dynamic_round147)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // a project written now (every parameter saved) keeps the measured engine
    auto now = makeEffectSlot (EffectKind::prismMultiband);
    upgradeLoadedEffectParams (now, (std::size_t) m::count);
    CHECK (m::measuredDynamic (now.params));
    // a project written before round 147 (its parameter list ends at the pan mode) loads the classic engine
    auto old = makeEffectSlot (EffectKind::prismMultiband);
    upgradeLoadedEffectParams (old, (std::size_t) m::dynamicEngine);
    CHECK (! m::measuredDynamic (old.params));
    // other engines are left alone
    auto crest = makeEffectSlot (EffectKind::crestCompressor);
    const auto before = crest.params;
    upgradeLoadedEffectParams (crest, 3);
    CHECK (crest.params == before);
}

namespace p147
{
// one band over the whole spectrum compressing: threshold, ratio, range −30, knee 0 (the trial's settings)
inline lynx::EffectParams wholeBand (double thresholdDb, double ratio)
{
    auto p = dyn108::defaults (lynx::EffectKind::prismMultiband);
    p[m::bandCountParam] = m::bandCountValue (1);
    p[(std::size_t) m::param (0, m::fThreshold)] = m::thresholdValue (thresholdDb);
    p[(std::size_t) m::param (0, m::fRatio)] = lynx::dyn::crest::ratioValue (ratio);
    p[(std::size_t) m::param (0, m::fRange)] = 0.0f; // −30 dB
    p[(std::size_t) m::param (0, m::fKnee)] = 0.0f;
    return p;
}

// steady gains (dB) of a 200 Hz tone of the given left / right amplitudes: { left, right, mid, side }
inline std::array<double, 4> stereoGains (const lynx::EffectParams& p, double aL, double aR)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::prismMultiband);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (dyn108::sr);
    fx->reset();
    const int n = 72000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
    {
        const auto s = std::sin (2.0 * dyn108::pi * 200.0 * i / dyn108::sr);
        l[(std::size_t) i] = (float) (aL * s);
        r[(std::size_t) i] = (float) (aR * s);
    }
    const auto il = l, ir = r;
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), p, 120.0);
    const auto rms = [&] (const std::vector<float>& a, const std::vector<float>& b, double ka, double kb) {
        double e = 0.0;
        for (int i = 48000; i < n; ++i)
        {
            const auto v = ka * a[(std::size_t) i] + kb * b[(std::size_t) i];
            e += v * v;
        }
        return 10.0 * std::log10 (e + 1e-30);
    };
    return { rms (l, r, 1, 0) - rms (il, ir, 1, 0), rms (l, r, 0, 1) - rms (il, ir, 0, 1),
             rms (l, r, 0.5, 0.5) - rms (il, ir, 0.5, 0.5), rms (l, r, 0.5, -0.5) - rms (il, ir, 0.5, -0.5) };
}
} // namespace p147

TEST (prism_stereo_link_round147)
{
    using namespace lynx;
    namespace m = dyn::prism;
    CHECK (m::bandLink0 == 88 && m::count == 118 && (int) effectInfo (EffectKind::prismMultiband).params.size() == m::count);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandLink0, 0.25f, 120.0) == "50%");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandLink0 + 2, 0.75f, 120.0) == "100%, 50% only");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::bandScLow0, m::scFilterValue (2000.0), 120.0) == "2.00 kHz");
    // [EXEC] Pro-MB, one band, threshold −30 dB, 4 : 1, range −30, knee 0; 200 Hz at −6 dB left, −36 dB right
    struct Case { float link, mode; double l, r, mid, side; bool ms; };
    const Case cases[] {
        { 0.0f, 0.0f, -17.74, 0.0, 0, 0, false },      // unlinked: the quiet right side untouched
        { 0.25f, 0.0f, -17.74, -13.47, 0, 0, false },  // 50 %
        { 0.5f, 0.0f, -17.74, -17.74, 0, 0, false },   // 100 % (the old detector)
        { 1.0f, 0.0f, 0, 0, -13.47, 0.0, true },       // 100 % + 100 % mid only: the side untouched
        { 1.0f, 1.0f, 0, 0, 0.0, -13.06, true },       // side only
        { 0.75f, 0.0f, 0, 0, -15.95, -4.74, true },    // 100 % + 50 % mid only
    };
    for (const auto& c : cases)
    {
        auto p = p147::wholeBand (-30.0, 4.0);
        p[m::bandLink0] = c.link;
        p[m::bandLinkMode0] = c.mode;
        const auto g = p147::stereoGains (p, 0.5, 0.5 * std::pow (10.0, -1.5));
        std::printf ("  Prism link %.2f mode %.0f: L %.2f R %.2f M %.2f S %.2f dB (Pro-MB %s %.2f / %.2f)\n", c.link, c.mode, g[0], g[1], g[2], g[3],
                     c.ms ? "M/S" : "L/R", c.ms ? c.mid : c.l, c.ms ? c.side : c.r);
        CHECK (std::abs ((c.ms ? g[2] : g[0]) - (c.ms ? c.mid : c.l)) < 0.8);
        CHECK (std::abs ((c.ms ? g[3] : g[1]) - (c.ms ? c.side : c.r)) < 0.8);
    }
}

TEST (prism_free_sidechain_filter_round147)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB, one band, threshold −60, 2 : 1, range −30: the detector level of a −20 dB probe (from its gain)
    // with a free side-chain filter 2 – 4 kHz: 1 kHz −22.4, 2 kHz +2.1, 3 kHz +5.5, 8 kHz −23.5 dB (relative)
    const double hz[] { 1000.0, 2000.0, 3000.0, 8000.0 };
    const double rel[] { -22.4, 2.1, 5.5, -23.5 };
    for (int k = 0; k < 4; ++k)
    {
        auto p = p147::wholeBand (-60.0, 2.0);
        p[m::bandScFree0] = 1.0f;
        p[m::bandScLow0] = m::scFilterValue (2000.0);
        p[m::bandScHigh0] = m::scFilterValue (4000.0);
        const auto g = p147::toneGain (p, hz[k]);
        // tone at −12 dBFS (0.25): detector = −2·gain − 60 → relative to the probe's own level
        const auto det = -2.0 * g - 60.0 - (-12.04);
        std::printf ("  Prism free side chain 2-4 kHz: %.0f Hz detector %+.1f dB (Pro-MB %+.1f)\n", hz[k], det, rel[k]);
        CHECK (std::abs (det - rel[k]) < 2.0);
    }
    // band side chain (the default) is unchanged by the free filter's settings
    auto a = p147::wholeBand (-60.0, 2.0), b = a;
    b[m::bandScLow0] = m::scFilterValue (2000.0);
    CHECK (std::abs (p147::toneGain (a, 300.0) - p147::toneGain (b, 300.0)) < 1e-6);
}

TEST (prism_promb_expert_translation_round147)
{
    using namespace lynx;
    namespace m = dyn::prism;
    std::vector<std::pair<int, float>> mb { { 0, 1.0f }, { 1, (float) std::log2 (30.0) }, { 2, 0.5f }, { 3, (float) std::log2 (30000.0) }, { 4, 0.5f },
                                            { 6, 0.5f }, { 8, 0.6f }, { 133, 1.0f }, { 139, 1.0f },
                                            { 15, 1.0f }, { 16, (float) std::log2 (2000.0) }, { 17, (float) std::log2 (4000.0) }, { 19, 0.25f }, { 20, 1.0f } };
    for (int b = 1; b < 6; ++b)
        mb.push_back ({ 22 * b, 2.0f });
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "FabFilter Pro-MB";
    for (const float expertOn : { 0.0f, 1.0f })
    {
        auto values = mb;
        values.push_back ({ 142, expertOn });
        id.state = dyn108::fabf (151, values);
        const auto* entry = findPluginSubstitute (id, false);
        CHECK (entry != nullptr);
        if (entry == nullptr)
            continue;
        const auto slot = makeSubstituteSlot (*entry, &id, nullptr);
        // the band is our band 1 (from 30 Hz); expert off: the reference ignores the expert settings
        const bool on = expertOn > 0.5f;
        CHECK (std::abs (slot.params[m::bandLink0] - (on ? 0.25f : 0.5f)) < 1e-6f);
        CHECK (slot.params[m::bandLinkMode0] == (on ? 1.0f : 0.0f));
        CHECK (slot.params[m::bandScFree0] == (on ? 1.0f : 0.0f));
        if (on)
        {
            CHECK (std::abs (m::scFilterHz (slot.params[m::bandScLow0]) - 2000.0) < 1.0);
            CHECK (std::abs (m::scFilterHz (slot.params[m::bandScHigh0]) - 4000.0) < 2.0);
        }
        CHECK (m::measuredDynamic (slot.params)); // a translated state runs the measured Dynamic phase
    }
}
