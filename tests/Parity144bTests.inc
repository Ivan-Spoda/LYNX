// --- Round 144b: Prism Multiband's processing modes and oversampling (Pro-MB, REA_rodada144b) ----------------------

namespace p144b
{
namespace m = lynx::dyn::prism;

// two bands split at 1 kHz (24 dB/oct), the low band a static −12 dB (range 0: no dynamics), the high band untouched
inline lynx::EffectParams lowCut (float mode, float os = 0.0f)
{
    auto p = dyn108::defaults (lynx::EffectKind::prismMultiband);
    p[m::bandCountParam] = m::bandCountValue (2);
    p[m::xover1] = m::crossoverValue (1000.0);
    for (int b = 0; b < 2; ++b)
        p[(std::size_t) m::param (b, m::fRange)] = 0.5f; // 0 dB
    p[(std::size_t) m::param (0, m::fLevel)] = 0.3f;      // −12 dB
    p[(std::size_t) m::param (1, m::fEnabled)] = 0.0f;
    p[m::processingMode] = mode;
    p[m::oversampling] = os;
    return p;
}

// steady-state gain (dB) of a sine through the effect (latency removed by the runner)
inline double toneGain (const lynx::EffectParams& p, double hz)
{
    std::vector<float> x (24000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.25 * std::sin (2.0 * dyn108::pi * hz * (double) i / dyn108::sr));
    const auto y = dyn108::run (lynx::EffectKind::prismMultiband, p, x);
    return dyn108::rmsDb (y, 12000, 18000) - dyn108::rmsDb (x, 12000, 18000);
}
} // namespace p144b

TEST (prism_processing_modes_defaults_round144b)
{
    using namespace lynx;
    namespace m = dyn::prism;
    const auto p = makeEffectSlot (EffectKind::prismMultiband).params;
    CHECK ((int) effectInfo (EffectKind::prismMultiband).params.size() == m::count);
    CHECK (m::phaseOf (p[m::processingMode]) == m::Phase::dynamic); // the old engine
    CHECK (m::oversamplingOf (p[m::oversampling]) == 1);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::processingMode, 0.0f, 120.0) == "Linear Phase");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::processingMode, 1.0f, 120.0) == "Minimum Phase");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::oversampling, 0.5f, 120.0) == "2x");
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::oversampling, 1.0f, 120.0) == "4x");
    // the measured latencies (48 kHz): linear phase 3072 samples, plus the 20 ms look-ahead when it is on
    CHECK (m::linearLatency (48000.0) == 3072 && m::linearLatency (44100.0) == 3072 && m::linearLatency (96000.0) == 6144);
    int lat = -1;
    dyn108::run (EffectKind::prismMultiband, p144b::lowCut (0.0f), std::vector<float> (4096, 0.0f), &lat);
    CHECK (lat == 3072);
    auto q = p144b::lowCut (0.0f);
    q[m::lookahead] = 1.0f;
    dyn108::run (EffectKind::prismMultiband, q, std::vector<float> (4096, 0.0f), &lat);
    CHECK (lat == 3072 + 960);
    dyn108::run (EffectKind::prismMultiband, p144b::lowCut (0.5f), std::vector<float> (4096, 0.0f), &lat);
    CHECK (lat == 0);
    dyn108::run (EffectKind::prismMultiband, p144b::lowCut (0.5f, 1.0f), std::vector<float> (4096, 0.0f), &lat);
    CHECK (lat == mixsuite::Oversampler::latency());
}

TEST (prism_linear_and_minimum_phase_round144b)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // every mode at rest: linear = the input delayed exactly; minimum = an all-pass (flat magnitude, not the input)
    std::vector<float> noise (16384);
    std::uint32_t seed = 7;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = (float) ((double) (seed >> 8) / 16777216.0 - 0.5);
    }
    auto rest = p144b::lowCut (0.0f);
    rest[(std::size_t) m::param (0, m::fLevel)] = 0.5f;
    auto y = dyn108::run (EffectKind::prismMultiband, rest, noise);
    double err = 0.0;
    for (std::size_t i = 0; i + 3072 < noise.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - noise[i]));
    CHECK (err < 1e-5);
    rest[m::processingMode] = 1.0f;
    for (const double hz : { 100.0, 1000.0, 5000.0 })
        CHECK (std::abs (p144b::toneGain (rest, hz)) < 0.05);

    // the band level: Pro-MB measured −12.0 / −10.6 / −4.1 / −0.4 / 0.0 dB at 50 / 500 / 1k / 2k / 10 kHz in every mode
    const double hz[] { 50.0, 500.0, 1000.0, 2000.0, 10000.0 };
    const double measured[] { -12.0, -10.6, -4.1, -0.4, 0.0 };
    for (const float mode : { 0.0f, 1.0f })
        for (int k = 0; k < 5; ++k)
        {
            const auto g = p144b::toneGain (p144b::lowCut (mode), hz[k]);
            std::printf ("  Prism %s: %.0f Hz %.2f dB (Pro-MB %.1f)\n", mode < 0.5f ? "linear" : "minimum", hz[k], g, measured[k]);
            CHECK (std::abs (g - measured[k]) < 0.35);
            // the editor's curve follows the mode
            const double gains[m::bands] { -12.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
            CHECK (std::abs (m::responseDb (p144b::lowCut (mode), gains, 48000.0, hz[k]) - g) < 0.35);
        }
    // dynamic phase (the old engine) at the crossover, for the round doc
    std::printf ("  Prism dynamic phase at 1 kHz: %.2f dB (Pro-MB -4.1)\n", p144b::toneGain (p144b::lowCut (0.5f), 1000.0));
}

TEST (prism_oversampling_round144b)
{
    using namespace lynx;
    // the static band level is the same with 2x / 4x (the gains are applied at the high rate)
    for (const float os : { 0.5f, 1.0f })
        for (const float mode : { 0.0f, 0.5f, 1.0f })
        {
            CHECK (std::abs (p144b::toneGain (p144b::lowCut (mode, os), 50.0) + 12.0) < 0.35);
            CHECK (std::abs (p144b::toneGain (p144b::lowCut (mode, os), 10000.0)) < 0.35);
        }
}

TEST (prism_promb_mode_translation_round144b)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // Pro-MB state slots 139 (0 linear, 1 dynamic, 2 minimum) and 140 (0 off, 1 2x, 2 4x)
    std::vector<std::pair<int, float>> mb { { 0, 1.0f }, { 1, (float) std::log2 (300.0) }, { 2, 0.5f }, { 3, (float) std::log2 (3000.0) },
                                            { 4, 0.5f }, { 6, 0.5f }, { 8, 0.6f }, { 14, -0.5f }, { 133, 1.0f }, { 139, 2.0f }, { 140, 2.0f } };
    for (int b = 1; b < 6; ++b)
        mb.push_back ({ 22 * b, 2.0f });
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "FabFilter Pro-MB";
    id.state = dyn108::fabf (151, mb);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry != nullptr)
    {
        const auto slot = makeSubstituteSlot (*entry, &id, nullptr);
        CHECK (m::phaseOf (slot.params[m::processingMode]) == m::Phase::minimum);
        CHECK (m::oversamplingOf (slot.params[m::oversampling]) == 4);
        // the band pan, mid / side (state −0.5 = side −6 dB; our band 2: band 1 is the gap below 300 Hz)
        CHECK (slot.params[m::panMode] == 1.0f && std::abs (slot.params[(std::size_t) m::bandPanParam (1)] - 0.25f) < 1e-6f);
    }
}

TEST (prism_mid_side_band_pan_round144b)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // a pure side signal (L = −R) at 100 Hz, band 1's pan fully left: left / right keeps the left side, mid / side
    // (the reference's band pan: side gain 0) removes it
    const auto runSide = [] (float panModeValue) {
        auto p = p144b::lowCut (0.5f);
        p[(std::size_t) m::param (0, m::fLevel)] = 0.5f;
        p[(std::size_t) m::bandPanParam (0)] = 0.0f;
        p[m::panMode] = panModeValue;
        auto slot = makeEffectSlot (EffectKind::prismMultiband);
        slot.params = p;
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        fx->reset();
        std::vector<float> l (24000), r (24000);
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            l[i] = (float) (0.25 * std::sin (2.0 * dyn108::pi * 100.0 * (double) i / 48000.0));
            r[i] = -l[i];
        }
        for (std::size_t off = 0; off < l.size(); off += 512)
            fx->process (l.data() + off, r.data() + off, 512, p, 120.0);
        return dyn108::rmsDb (l, 12000, 18000);
    };
    CHECK (std::abs (runSide (0.0f) - dyn108::rmsDb (std::vector<float> (6000, 0.25f), 0, 6000) + 3.0) < 0.5);
    CHECK (runSide (1.0f) < -40.0);
    CHECK (formatEffectParam (EffectKind::prismMultiband, m::panMode, 1.0f, 120.0) == "mid / side");
}
