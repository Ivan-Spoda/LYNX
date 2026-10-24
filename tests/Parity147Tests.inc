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
