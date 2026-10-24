// --- Facet EQ round 103: dynamic EQ, phase modes, solo, sidechain detector (core/FacetEq.h, REA_rodada103) -----------

namespace facet103
{
using namespace lynx::facet;

lynx::EffectParams defaults()
{
    lynx::EffectParams p {};
    const auto& in = info();
    for (std::size_t i = 0; i < in.params.size(); ++i) p[i] = in.params[i].defaultValue;
    return p;
}

lynx::EffectParams bell (double f, double g, double qq)
{
    auto p = defaults();
    p[(std::size_t) param (0, 0)] = shapeValue (Shape::bell);
    p[(std::size_t) param (0, 1)] = hzValue (f);
    p[(std::size_t) param (0, 2)] = gainValue (g);
    p[(std::size_t) param (0, 3)] = qValue (qq);
    return p;
}

// level (dB re `amp`) of the tone `f` in the last 0.2 s of the processed signal, phase-aligned to the latency
double toneDb (lynx::Effect& e, const lynx::EffectParams& p, double f, double amp, double secs, const std::vector<float>* side = nullptr,
               double sideAmp = 0.0, double sideHz = 0.0)
{
    const double sr = 48000.0;
    const int n = (int) (secs * sr);
    std::vector<float> l ((std::size_t) n), r ((std::size_t) n), sl ((std::size_t) n), sr2 ((std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        l[(std::size_t) i] = r[(std::size_t) i] = (float) (amp * std::sin (2.0 * 3.14159265358979 * f * i / sr));
        sl[(std::size_t) i] = sr2[(std::size_t) i] = (float) (sideAmp * std::sin (2.0 * 3.14159265358979 * sideHz * i / sr));
    }
    (void) side;
    for (int o = 0; o < n; o += 480)
    {
        const int m = std::min (480, n - o);
        if (sideAmp > 0.0) e.setSidechain (sl.data() + o, sr2.data() + o);
        e.process (l.data() + o, r.data() + o, m, p, 120.0);
    }
    const int lat = e.latencySamples();
    const int w = (int) (0.2 * sr);
    double c = 0.0, s = 0.0;
    for (int i = n - w; i < n; ++i)
    {
        const double ph = 2.0 * 3.14159265358979 * f * (i - lat) / sr;
        c += l[(std::size_t) i] * std::cos (ph);
        s += l[(std::size_t) i] * std::sin (ph);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (c * c + s * s) / w / amp + 1e-12);
}
} // namespace facet103

TEST (facet_eq_new_parameters_keep_old_behaviour_round103)
{
    using namespace lynx::facet;
    // the 22 appended controls default to the round-99 behaviour, so older projects load unchanged
    const auto p = facet103::defaults();
    CHECK (info().params.size() == (std::size_t) paramCount && paramCount >= 64 && paramCount <= lynx::maxEffectParams);
    for (int b = 0; b < bands; ++b)
    {
        CHECK (std::abs (rangeDb (p[(std::size_t) (rangeParam0 + b)])) < 1e-6);
        CHECK (! isDynamic (p, b));
    }
    CHECK (phase (p[(std::size_t) phaseParam]) == Phase::zeroLatency);
    CHECK (soloBand (p[(std::size_t) soloParam]) == -1);
    CHECK (! sidechainDetector (p[(std::size_t) detectorParam]));
    auto e = make (&p);
    e->prepare (48000.0);
    CHECK (e->latencySamples() == 0);
    // laws round-trip
    CHECK (std::abs (rangeDb (rangeValue (-12.0)) + 12.0) < 0.01);
    CHECK (std::abs (thresholdDb (thresholdValue (-30.0)) + 30.0) < 0.01);
    CHECK (soloBand (soloValue (3)) == 3 && soloBand (soloValue (-1)) == -1);
    for (int m = 0; m < phaseCount; ++m) CHECK ((int) phase (phaseValue ((Phase) m)) == m);
    CHECK (std::abs (attackMs (0.5f) - 3.54) < 0.05 && std::abs (releaseMs (0.5f) - 282.8) < 1.0);
    CHECK (lynx::formatEffectParam (lynx::EffectKind::facetEq, phaseParam, phaseValue (Phase::linear), 120.0) == "Linear Phase");
    CHECK (lynx::formatEffectParam (lynx::EffectKind::facetEq, rangeParam0 + 2, rangeValue (-6.0), 120.0) == "-6.00 dB");
}

TEST (facet_eq_dynamic_gain_law_matches_measured_reference_round103)
{
    // REA_rodada103 §2.1: threshold −30, range −12; measured steps (dB of gain change at sine-peak levels)
    const std::pair<double, double> measured[] = { { -40, 0.0 }, { -34, -0.1 }, { -32, -0.5 }, { -30, -1.3 }, { -28, -2.5 }, { -26, -4.1 },
                                                   { -24, -6.0 }, { -22, -8.0 }, { -20, -10.0 }, { -18, -12.0 }, { -6, -12.0 } };
    for (const auto& [level, gain] : measured)
        CHECK (std::abs (lynx::facet::dynamicGainDb (level, -30.0, -12.0) - gain) < 0.12);
    // a positive range mirrors it; the range clamps
    CHECK (std::abs (lynx::facet::dynamicGainDb (-24.0, -30.0, 12.0) - 6.0) < 1e-9);
    CHECK (std::abs (lynx::facet::dynamicGainDb (0.0, -30.0, -6.0) + 6.0) < 1e-9);
}

TEST (facet_eq_dynamic_band_in_every_phase_mode_round103)
{
    using namespace lynx::facet;
    auto p = facet103::bell (1000.0, 0.0, 1.0);
    p[(std::size_t) rangeParam0] = rangeValue (-12.0);
    p[(std::size_t) thresholdParam0] = thresholdValue (-30.0);
    CHECK (isDynamic (p, 0));
    for (int m = 0; m < phaseCount; ++m)
    {
        p[(std::size_t) phaseParam] = phaseValue ((Phase) m);
        for (const auto& [level, expect] : { std::pair { -50.0, 0.0 }, { -24.0, -6.0 }, { -6.0, -12.0 } })
        {
            auto e = make (&p);
            e->prepare (48000.0);
            CHECK (e->latencySamples() == latencyFor ((Phase) m, 48000.0));
            const auto got = facet103::toneDb (*e, p, 1000.0, std::pow (10.0, level / 20.0), 1.2);
            if (std::abs (got - expect) >= 0.45) std::printf ("  facet dyn mode %d level %.0f: %.2f vs %.2f\n", m, level, got, expect);
            CHECK (std::abs (got - expect) < 0.45);
            CHECK (std::abs (e->controlOutput (0) * 60.0f - (float) expect) < 0.5f); // the live meter the editor shows
        }
    }
    // static gain + dynamic range: +6 below the threshold, −6 above (as measured)
    p = facet103::bell (1000.0, 6.0, 1.0);
    p[(std::size_t) rangeParam0] = rangeValue (-12.0);
    p[(std::size_t) thresholdParam0] = thresholdValue (-30.0);
    auto e = make (&p);
    e->prepare (48000.0);
    CHECK (std::abs (facet103::toneDb (*e, p, 1000.0, std::pow (10.0, -50.0 / 20.0), 1.0) - 6.0) < 0.2);
    CHECK (std::abs (facet103::toneDb (*e, p, 1000.0, std::pow (10.0, -6.0 / 20.0), 1.0) + 6.0) < 0.4);
}

TEST (facet_eq_sidechain_detector_round103)
{
    using namespace lynx::facet;
    auto p = facet103::bell (1000.0, 0.0, 1.0);
    p[(std::size_t) rangeParam0] = rangeValue (-12.0);
    p[(std::size_t) thresholdParam0] = thresholdValue (-30.0);
    p[(std::size_t) detectorParam] = 1.0f;
    // a quiet input (below the threshold) ducked by a loud sidechain in the band
    auto e = make (&p);
    e->prepare (48000.0);
    const auto ducked = facet103::toneDb (*e, p, 1000.0, 0.003, 1.0, nullptr, 0.5, 1000.0);
    CHECK (std::abs (ducked + 12.0) < 0.5);
    // the detector set to the sidechain but nothing routed: the input is detected
    auto e2 = make (&p);
    e2->prepare (48000.0);
    CHECK (std::abs (facet103::toneDb (*e2, p, 1000.0, 0.003, 1.0)) < 0.2);
    // a sidechain outside the band does not trigger it
    auto e3 = make (&p);
    e3->prepare (48000.0);
    CHECK (std::abs (facet103::toneDb (*e3, p, 1000.0, 0.003, 1.0, nullptr, 0.05, 8000.0)) < 0.3); // −26 dB three octaves away: under the threshold
}

TEST (facet_eq_phase_modes_round103)
{
    using namespace lynx::facet;
    // magnitude: the analog prototype in both FIR modes (also near Nyquist, where zero latency deviates)
    auto p = facet103::bell (10000.0, 6.0, 2.0);
    p[(std::size_t) param (1, 0)] = shapeValue (Shape::lowCut);
    p[(std::size_t) param (1, 1)] = hzValue (100.0);
    p[(std::size_t) param (1, 4)] = slopeValue (24);
    CHECK (latencyFor (Phase::natural, 48000.0) == 320 && latencyFor (Phase::natural, 96000.0) == 640);
    CHECK (latencyFor (Phase::linear, 48000.0) == 5120 && latencyFor (Phase::zeroLatency, 44100.0) == 0);
    for (const auto m : { Phase::natural, Phase::linear })
    {
        p[(std::size_t) phaseParam] = phaseValue (m);
        for (const double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0 })
        {
            auto e = make (&p);
            e->prepare (48000.0);
            const auto got = facet103::toneDb (*e, p, f, 0.1, 0.6);
            const auto want = analogDb (p, 0, f) + analogDb (p, 1, f);
            if (std::abs (got - want) >= 0.2) std::printf ("  facet mode %d %.0f Hz: %.3f vs %.3f\n", (int) m, f, got, want);
            CHECK (std::abs (got - want) < 0.2);
            CHECK (std::abs (bandResponseDb (p, 0, 48000.0, f) - analogDb (p, 0, f)) < 1e-9); // the editor's curve
        }
    }
    // linear phase: a symmetric impulse response centred on the latency
    {
        auto lp = facet103::bell (1000.0, 9.0, 3.0);
        lp[(std::size_t) phaseParam] = phaseValue (Phase::linear);
        auto e = make (&lp);
        e->prepare (48000.0);
        const int lat = e->latencySamples(), n = lat * 2 + 2048;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        l[0] = r[0] = 1.0f;
        for (int o = 0; o < n; o += 256) e->process (l.data() + o, r.data() + o, std::min (256, n - o), lp, 120.0);
        float peak = 0.0f;
        int at = 0;
        for (int i = 0; i < n; ++i)
            if (std::abs (l[(std::size_t) i]) > peak) { peak = std::abs (l[(std::size_t) i]); at = i; }
        CHECK (at == lat);
        double asym = 0.0;
        for (int k = 1; k < 1500; ++k) asym = std::max (asym, (double) std::abs (l[(std::size_t) (lat + k)] - l[(std::size_t) (lat - k)]));
        CHECK (asym < 1e-4);
        CHECK (std::abs (l[(std::size_t) lat] - r[(std::size_t) lat]) < 1e-6f);
    }
    // natural phase: nothing comes out before the latency minus its short pre-ring
    {
        auto np = facet103::bell (1000.0, 9.0, 3.0);
        np[(std::size_t) phaseParam] = phaseValue (Phase::natural);
        auto e = make (&np);
        e->prepare (48000.0);
        const int n = 4096;
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        l[0] = r[0] = 1.0f;
        for (int o = 0; o < n; o += 128) e->process (l.data() + o, r.data() + o, 128, np, 120.0);
        double before = 0.0, total = 0.0;
        for (int i = 0; i < n; ++i)
        {
            total += (double) l[(std::size_t) i] * l[(std::size_t) i];
            if (i < 128) before += (double) l[(std::size_t) i] * l[(std::size_t) i];
        }
        CHECK (before < 1e-12 && total > 0.5);
    }
    // switching mode at run time changes the reported latency (PDC re-plans)
    {
        auto sp = facet103::bell (1000.0, 3.0, 1.0);
        auto e = make (&sp);
        e->prepare (48000.0);
        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        e->process (l.data(), r.data(), 256, sp, 120.0);
        CHECK (e->latencySamples() == 0);
        sp[(std::size_t) phaseParam] = phaseValue (Phase::natural);
        e->process (l.data(), r.data(), 256, sp, 120.0);
        CHECK (e->latencySamples() == 320);
    }
}

TEST (facet_eq_solo_band_round103)
{
    using namespace lynx::facet;
    auto p = facet103::bell (1000.0, 6.0, 2.0);
    p[(std::size_t) soloParam] = soloValue (0);
    auto e = make (&p);
    e->prepare (48000.0);
    CHECK (std::abs (facet103::toneDb (*e, p, 1000.0, 0.25, 0.5)) < 0.3); // the band's centre passes
    auto e2 = make (&p);
    e2->prepare (48000.0);
    CHECK (facet103::toneDb (*e2, p, 8000.0, 0.25, 0.5) < -15.0);         // far away is gone
    // solo of a band that is off: no effect
    p[(std::size_t) param (0, 0)] = 0.0f;
    auto e3 = make (&p);
    e3->prepare (48000.0);
    CHECK (std::abs (facet103::toneDb (*e3, p, 8000.0, 0.25, 0.5)) < 0.05);
}

TEST (live_meters_cover_facet_and_tune_round103)
{
    // Facet EQ publishes 8 dynamic gains, LYNX Tune 3 pitch values; the analyser tap ring is a power of two
    CHECK (lynx::ProcessTimes::meterOutputs >= 8);
    CHECK ((lynx::ProcessTimes::AnalyzerTap::size & (lynx::ProcessTimes::AnalyzerTap::size - 1)) == 0);
    const auto& l = lynx::plugui::layoutFor (lynx::EffectKind::lynxTune);
    bool graph = false;
    for (const auto& item : l.items)
        graph = graph || (item.type == lynx::plugui::Item::display && item.display != nullptr && (std::string (item.display) == "pitchgraph" || std::string (item.display) == "pitchtrace")); // round 120: graph + ring
    CHECK (graph);
}
