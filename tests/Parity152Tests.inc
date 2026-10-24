// round 152 (REA_rodada152): Prism Multiband ← Pro-MB — the measured detector's attack in the gain stage (slow attacks)

TEST (prism_gain_stage_attack_round152)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB, one band over the whole spectrum, threshold −30, 4 : 1, knee 0, range −30, release 20 %: the gain
    // after a 1 kHz step −30 → −6 dBFS (1 ms rms windows, D:\lynx-work\parity150\mb\att*.wav)
    struct Case { float attack; int ms[4]; double want[4]; };
    const Case cases[] {
        { 0.5f, { 10, 20, 50, 100 }, { -7.71, -11.67, -15.81, -16.92 } },
        { 0.75f, { 10, 50, 100, 200 }, { -1.95, -7.49, -11.55, -15.36 } },
        { 1.0f, { 50, 100, 200, 500 }, { -2.88, -5.25, -8.97, -14.72 } },
    };
    std::vector<float> x (48000 * 2);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) ((i < 48000 ? 0.0316 : 0.5) * std::sin (2.0 * dyn108::pi * 1000.0 * (double) i / 48000.0));
    for (const auto& c : cases)
        for (const bool gainStage : { true, false })
        {
            auto q = p147::wholeBand (-30.0, 4.0);
            q[(std::size_t) m::param (0, m::fAttack)] = c.attack;
            q[(std::size_t) m::param (0, m::fRelease)] = 0.2f;
            q[m::detectorLaw] = 1.0f;
            q[m::rev152] = gainStage ? 1.0f : 0.0f;
            const auto y = dyn108::run (EffectKind::prismMultiband, q, x);
            double err = 0.0;
            std::printf ("  Prism attack %.0f %% (%s):", c.attack * 100.0, gainStage ? "gain stage" : "level, round 150");
            for (int k = 0; k < 4; ++k)
            {
                const auto a = (std::size_t) (48000 + 48 * c.ms[k]);
                const auto got = dyn108::rmsDb (y, a, a + 48) - dyn108::rmsDb (x, a, a + 48);
                err = std::max (err, std::abs (got - c.want[k]));
                std::printf (" %d ms %.2f (Pro-MB %.2f)", c.ms[k], got, c.want[k]);
            }
            std::printf ("\n");
            if (gainStage)
                CHECK (err < 0.5);
        }
    // new instances use the gain-stage attack; a project saved before round 152 keeps round 150's
    auto slot = makeEffectSlot (EffectKind::prismMultiband);
    CHECK (slot.params[m::rev152] >= 0.5f);
    upgradeLoadedEffectParams (slot, (std::size_t) m::rev152);
    CHECK (slot.params[m::rev152] < 0.5f && m::measuredDetector (slot.params));
    CHECK ((int) effectInfo (EffectKind::prismMultiband).params.size() == m::count && m::rev152 == m::detectorLaw + 1);
}

TEST (prism_dynamic_narrow_bands_round152)
{
    using namespace lynx;
    namespace m = dyn::prism;
    // [EXEC] Pro-MB Dynamic phase, one static band in the free layout (magnitude, dB, 48 kHz; D:\lynx-work\parity152\mb)
    struct Case { std::array<double, 5> band; double hz[5]; double db[5]; };
    const Case cases[] {
        { { 300.0, 900.0, 24, 24, -30 }, { 150.0, 300.0, 520.0, 900.0, 1800.0 }, { -0.83, -7.28, -13.34, -7.28, -0.82 } },
        { { 300.0, 600.0, 12, 12, -12 }, { 150.0, 300.0, 424.0, 600.0, 1200.0 }, { -1.57, -3.19, -3.52, -3.19, -1.56 } },
        { { 300.0, 600.0, 48, 48, -30 }, { 250.0, 300.0, 424.0, 600.0, 700.0 }, { -2.56, -7.37, -16.94, -7.38, -3.08 } },
        { { 300.0, 600.0, 12, 12, 6 }, { 150.0, 300.0, 424.0, 600.0, 1200.0 }, { 1.32, 2.60, 2.84, 2.60, 1.32 } },
        { { 300.0, 1200.0, 36, 36, -24 }, { 150.0, 300.0, 600.0, 1200.0, 2400.0 }, { -0.14, -5.88, -20.76, -5.88, -0.14 } },
        // 300 – 450 Hz is narrower than 0.8 octave: the upper edge moves to 522 Hz
        { { 300.0, 450.0, 48, 48, -30 }, { 300.0, 375.0, 420.0, 470.0, 600.0 }, { -7.28, -13.14, -13.07, -10.62, -3.51 } },
    };
    for (const auto& c : cases)
        for (const bool now : { true, false })
        {
            auto p = p150::freeBands ({ c.band });
            p[m::processingMode] = 0.5f; // Dynamic phase
            p[m::rev152] = now ? 1.0f : 0.0f;
            double err = 0.0, errTone = 0.0;
            std::printf ("  Prism dynamic %.0f-%.0f Hz %.0f dB/oct %+.0f dB (%s):", c.band[0], c.band[1], c.band[2], c.band[4], now ? "round 152" : "round 150");
            for (int k = 0; k < 5; ++k)
            {
                const auto curve = p150::curveDb (p, c.hz[k]);
                err = std::max (err, std::abs (curve - c.db[k]));
                if (now)
                    errTone = std::max (errTone, std::abs (p147::toneGain (p, c.hz[k]) - c.db[k]));
                std::printf (" %.0f Hz %.2f (%.2f)", c.hz[k], curve, c.db[k]);
            }
            std::printf ("\n");
            if (now)
                CHECK (err < 0.1 && errTone < 0.15);
        }
}

TEST (foil_plate_view_round152)
{
    using namespace lynx;
    // round 152: Foil Plate's appended View (Full | Plate): the Plate view is the Little Plate role's one-knob panel
    const auto& info = effectInfo (EffectKind::foilPlate);
    CHECK (info.params.size() == 16 && std::string (info.params[15].name) == "View" && info.params[15].defaultValue == 0.0f);
    CHECK (formatEffectParam (EffectKind::foilPlate, 15, 1.0f, 120.0) == "Plate" && formatEffectParam (EffectKind::foilPlate, 15, 0.0f, 120.0) == "Full");
    const auto& l = plugui::layoutFor (EffectKind::foilPlate);
    for (const float view : { 0.0f, 1.0f })
    {
        auto p = makeEffectSlot (EffectKind::foilPlate).params;
        p[15] = view;
        std::map<std::string, int> shown;
        for (const auto& item : l.items)
        {
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
            if (item.param != nullptr && plugui::isShown (EffectKind::foilPlate, item, 0, p))
                ++shown[item.param];
        }
        for (const auto* name : { "Decay", "Low cut", "Mix", "Infinite", "Voicing", "View" })
            CHECK (shown[name] == 1);
        CHECK ((shown["Size"] == 1) == (view < 0.5f) && (shown["Mod depth"] == 1));
    }
}
