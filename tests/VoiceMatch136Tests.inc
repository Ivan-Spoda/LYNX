// --- Round 136 (vox136): the Soundtoys roles of her corpus, measured on the official trials -------------------------
// Golden values are the REFERENCE's measured outputs (tools/fxmeasure, 48 kHz, docs/analise/REA_rodada136.md); the
// substitutes must stay within the stated tolerance of them.

namespace v136
{
constexpr double sr = 48000.0;
struct Out
{
    double rmsDb, h2Dbc, h3Dbc;
};
// a 1 kHz sine at `db` dBFS through `slot` (stereo), measured over the last half: RMS and the 2nd / 3rd harmonics
inline Out sineThrough (const lynx::EffectSlot& slot, double db)
{
    const int n = 24000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * 3.141592653589793 * 1000.0 * i / sr));
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
    double e = 0;
    std::array<double, 4> re {}, im {};
    for (int i = n / 2; i < n; ++i)
    {
        e += l[(size_t) i] * (double) l[(size_t) i];
        for (int h = 1; h <= 3; ++h)
        {
            const auto w = 2.0 * 3.141592653589793 * 1000.0 * h * i / sr;
            re[(size_t) h] += l[(size_t) i] * std::cos (w);
            im[(size_t) h] += l[(size_t) i] * std::sin (w);
        }
    }
    auto mag = [&] (int h) { return std::hypot (re[(size_t) h], im[(size_t) h]); };
    return { 10.0 * std::log10 (e / (n / 2)), 20.0 * std::log10 (mag (2) / mag (1)), 20.0 * std::log10 (mag (3) / mag (1)) };
}
inline lynx::EffectSlot classicEmber (float drive, float tone, float highCut, float mix, float output)
{
    namespace e = lynx::mixsuite::ember;
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::ember);
    slot.params[(size_t) e::voicing] = 1.0f;
    slot.params[(size_t) e::classicDrive] = drive;
    slot.params[(size_t) e::classicTone] = tone;
    slot.params[(size_t) e::classicHighCut] = highCut;
    slot.params[(size_t) e::classicMix] = mix;
    slot.params[(size_t) e::classicOutput] = output;
    return slot;
}
} // namespace v136

TEST (ember_classic_voicing_keeps_embers_latency_round136)
{
    using namespace lynx;
    auto own = makeEffectSlot (EffectKind::ember);
    auto classic = v136::classicEmber (0.2f, 0.5f, 1.0f, 1.0f, 1.0f);
    auto a = makeEffect (own), b = makeEffect (classic);
    a->prepare (48000.0);
    b->prepare (48000.0);
    CHECK (a->latencySamples() == b->latencySamples());
    CHECK (emberclassic::Engine::latency() == mixsuite::Oversampler::latency());
    // the dry path is the input delayed by that latency (Mix 0)
    auto dry = v136::classicEmber (0.5f, 0.5f, 1.0f, 0.0f, 1.0f);
    auto fx = makeEffect (dry);
    fx->prepare (48000.0);
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    l[10] = r[10] = 0.5f;
    fx->process (l.data(), r.data(), 256, dry.params, 120.0);
    CHECK (std::abs (l[(size_t) (10 + emberclassic::Engine::latency())] - 0.5f) < 1e-6f);
}

TEST (ember_classic_matches_the_trial_at_her_state_round136)
{
    // [EXEC] the official trial, 1 kHz sine: her most used state (241 slots: style A, drive 2.0, tone +1.7 dB, high cut
    // 8.14 kHz, mix 88 %, output −3.2 dB), drive 0 and drive 6 (style A, everything else neutral)
    struct Point { float drive, tone, hc, mix, out; double in, rms, h2, h3; };
    const Point golden[] {
        { 0.2f, 0.570833f, 0.699995f, 0.88f, 0.866667f, -30, -32.124, -42.8, -93.9 },
        { 0.2f, 0.570833f, 0.699995f, 0.88f, 0.866667f, -12, -14.189, -27.5, -40.5 },
        { 0.2f, 0.570833f, 0.699995f, 0.88f, 0.866667f, -6, -9.006, -25.8, -27.6 },
        { 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, -30, -35.049, -47.6, -103.2 },
        { 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, -12, -17.022, -29.9, -66.2 },
        { 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, -6, -11.101, -26.2, -40.0 },
        { 0.6f, 0.5f, 1.0f, 1.0f, 1.0f, -30, -15.195, -28.5, -55.7 },
        { 0.6f, 0.5f, 1.0f, 1.0f, 1.0f, -12, -2.515, -16.8, -13.1 },
    };
    for (const auto& g : golden)
    {
        const auto o = v136::sineThrough (v136::classicEmber (g.drive, g.tone, g.hc, g.mix, g.out), g.in);
        if (std::getenv ("LYNX_TEST_VERBOSE"))
            std::printf ("  classic drive %.1f in %.0f: rms %.3f (%.3f) h2 %.1f (%.1f) h3 %.1f (%.1f)\n", g.drive * 10.0, g.in, o.rmsDb, g.rms, o.h2Dbc, g.h2, o.h3Dbc, g.h3);
        // interim tolerances (REA_rodada136 §1.5: the even harmonics at low level are still under the trial's)
        CHECK (std::abs (o.rmsDb - g.rms) < 0.6);
        CHECK (std::abs (o.h2Dbc - g.h2) < 9.0);
        if (g.h3 > -60.0)
            CHECK (std::abs (o.h3Dbc - g.h3) < 7.0);
    }
}

TEST (soundtoys_states_are_recognised_by_their_preset_hash_round136)
{
    using namespace lynx;
    // FNV-1a 64 of the payload after "PRESET =" (the trailing ';' excluded)
    const std::string text = "WIDGET = Decapitator;\rPRESET =abc;\r";
    const std::vector<std::uint8_t> st (text.begin(), text.end());
    CHECK (soundtoysPresetHash (st) == 0xe71fa2190541574bull);
    // an unknown state keeps the substitute's preset (her usual light drive in the Classic voicing)
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst2;
    id.name = "Decapitator";
    id.state = st;
    const auto* e = findPluginSubstitute (id, false);
    CHECK (e != nullptr && e->effect == EffectKind::ember);
    if (e != nullptr)
    {
        int n = -1;
        const auto slot = makeSubstituteSlot (*e, &id, &n);
        CHECK (n == 0);
        CHECK (slot.params[(size_t) mixsuite::ember::voicing] > 0.5f);
        CHECK (std::abs (emberclassic::driveUnits (slot.params[(size_t) mixsuite::ember::classicDrive]) - 2.0) < 1e-6);
    }
}

TEST (rhythm_pan_follows_the_trials_pattern_round136)
{
    using namespace lynx;
    // [EXEC] her most used PanMan state: a 2-step left / right pattern, 3.560 Hz, width 150.6 degrees, glide 99 % of a
    // step, start phase 0.9917 — the pan angle measured on the trial at 25 ms steps from the start (1 kHz tone)
    auto slot = makeEffectSlot (EffectKind::autoPan);
    const float v[12] { 1.0f, 0.850488f, 0.0f, 0.4f, 0.717143f, 0.5f, 0.9925f, 0.0f, 0.9981f, 0.9917f, 0.0f, 0.5f };
    for (int i = 0; i < 12; ++i)
        slot.params[(size_t) (7 + i)] = v[i];
    auto fx = makeEffect (slot);
    fx->prepare (48000.0);
    const int n = 48000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = 0.25f * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
    for (int i = 0; i < n; i += 480)
        fx->process (l.data() + i, r.data() + i, 480, slot.params, 120.0);
    // angle over 1 ms windows centred on t (equal-power: tan θ = R / L)
    auto theta = [&] (int t)
    {
        double a = 0, b = 0;
        for (int k = t - 24; k < t + 24; ++k) { a += l[(size_t) k] * l[(size_t) k]; b += r[(size_t) k] * r[(size_t) k]; }
        return std::atan2 (std::sqrt (b), std::sqrt (a)) * 180.0 / 3.141592653589793;
    };
    // the trial (PanMan_0 from a reset, every 25 ms from 50 ms, its 45-sample latency removed)
    const double measured[] { 18.9, 33.5, 49.2, 65.1, 72.1, 61.6, 49.8, 37.2, 24.1, 10.6, 17.0, 30.4, 43.9, 57.3 };
    // (the trial's first ~200 ms after a reset glide in from hard left: not modelled, REA_rodada136 §2.5)
    for (int k = 6; k < 14; ++k)
    {
        if (std::getenv ("LYNX_TEST_VERBOSE"))
            std::printf ("  pan t %d: %.1f (%.1f)\n", 2400 + k * 1200, theta (2400 + k * 1200), measured[k]);
        CHECK (std::abs (theta (2400 + k * 1200) - measured[k]) < 2.0);
    }
    // equal power: the two gains' squares add up to one
    double a = 0, b = 0;
    for (int k = 24000; k < 24480; ++k) { a += l[(size_t) k] * l[(size_t) k]; b += r[(size_t) k] * r[(size_t) k]; }
    if (std::getenv ("LYNX_TEST_VERBOSE"))
        std::printf ("  pan power %.4f\n", std::sqrt (2.0 * (a + b) / 480.0) / 0.25);
    CHECK (std::abs (std::sqrt (2.0 * (a + b) / 480.0) / 0.25 - 1.0) < 0.02);
}

TEST (alterboy_substitute_plays_her_usual_setting_round136)
{
    using namespace lynx;
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst2;
    id.name = "Little AlterBoy";
    const auto* e = findPluginSubstitute (id, false);
    CHECK (e != nullptr && e->effect == EffectKind::lynxTune);
    if (e == nullptr)
        return;
    auto slot = makeSubstituteSlot (*e);
    CHECK (slot.params[(size_t) lynxtune::driveStage] > 0.5f);
    CHECK (std::abs (lynxtune::formantSemitones (slot.params[(size_t) lynxtune::formantShift]) - 1.2) < 0.01);
    // [EXEC] the reference's Drive 4.6 (Transpose mode, pitch / formant 0): a 200 Hz sine at −20 dBFS comes out
    // +5.1 dB, at −8 dBFS +0.9 dB (the drive's output compensation); LYNX Tune with every note bypassed + the stage
    for (int k = 0; k < 12; ++k)
        slot.params[(size_t) (lynxtune::bypassC + k)] = 1.0f;
    slot.params[(size_t) lynxtune::formantShift] = 0.5f;
    for (const auto [in, gain] : { std::pair { -20.0, 5.1 }, std::pair { -8.0, 0.9 } })
    {
        const int n = 48000;
        std::vector<float> l (n), r (n);
        for (int i = 0; i < n; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) (std::pow (10.0, in / 20.0) * std::sin (2.0 * 3.141592653589793 * 200.0 * i / 48000.0));
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        for (int off = 0; off < n; off += 512)
            fx->process (l.data() + off, r.data() + off, std::min (512, n - off), slot.params, 120.0);
        double a = 0;
        for (int i = n / 2; i < n; ++i) a += l[(size_t) i] * (double) l[(size_t) i];
        const auto outDb = 10.0 * std::log10 (a / (n / 2)) - (in - 3.0103);
        if (std::getenv ("LYNX_TEST_VERBOSE"))
            std::printf ("  alterboy drive 4.6 in %.0f: %+.2f dB (%+.1f)\n", in, outDb, gain);
        CHECK (std::abs (outDb - gain) < 1.0);
    }
}
