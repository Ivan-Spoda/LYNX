// --- Round 144a: parity items (docs/PARITY.md, REA_rodada144a) ----------------------------------------------------

TEST (lattice_delay_sync_reference_law_round144)
{
    using namespace lynx;
    namespace lt = echosuite::lattice;
    auto p = makeEffectSlot (EffectKind::latticeMod).params;
    CHECK ((int) effectInfo (EffectKind::latticeMod).params.size() == lt::paramCount);
    CHECK (p[lt::delaySync] == 0.0f && lt::syncedDelayMs (p, 120.0) == 0.0); // free by default (the old sound)
    // [EXEC] the reference at 120 BPM, one tap, Delay 500 ms: index floor (17 v) -> the tap time
    const double measured[18] = { 0, 0, 125, 250, 500, 1000, 2000, 2750, 187.5, 375, 750, 1500, 2750, 83.33, 166.67, 333.33, 666.67, 1333.33 };
    for (int k = 0; k < 18; ++k)
    {
        p[lt::delaySync] = k < 17 ? ((float) k + 0.5f) / 17.0f : 1.0f;
        CHECK (lt::delaySyncIndex (p[lt::delaySync]) == k);
        CHECK (std::abs (lt::syncedDelayMs (p, 120.0) - measured[k]) < 0.01);
    }
    // the boundaries are k / 17 (floor): just under 3/17 is still 1/16
    CHECK (lt::delaySyncIndex (3.0f / 17.0f - 0.002f) == 2 && lt::delaySyncIndex (3.0f / 17.0f + 0.002f) == 3);
    // 240 BPM: 2/1 = 2000 ms and 1/1. = 1500 ms (no longer capped)
    p[lt::delaySync] = 7.5f / 17.0f;
    CHECK (std::abs (lt::syncedDelayMs (p, 240.0) - 2000.0) < 0.01);
    CHECK (formatEffectParam (EffectKind::latticeMod, lt::delaySync, 7.5f / 17.0f, 120.0) == "2/1");
    CHECK (formatEffectParam (EffectKind::latticeMod, lt::delaySync, 12.5f / 17.0f, 120.0) == "1/1.");
    CHECK (formatEffectParam (EffectKind::latticeMod, lt::delaySync, 1.0f, 120.0) == "1/1T");
    CHECK (formatEffectParam (EffectKind::latticeMod, lt::delaySync, 1.5f / 17.0f, 120.0) == "free");
    // free: the older by-length Sync still works (projects saved before round 144)
    p[lt::delaySync] = 0.0f;
    p[lt::sync] = 13.0f / 17.0f; // "1/4" in the old list
    CHECK (std::abs (lt::syncedDelayMs (p, 120.0) - 500.0) < 0.01);

    // an impulse through one tap synced to 1/8 at 120 BPM: the echo 250 ms later
    p = makeEffectSlot (EffectKind::latticeMod).params;
    p[lt::mix] = 1.0f;
    p[lt::depth] = 0.0f;
    p[lt::detuneDepth] = 0.0f;
    p[lt::delay] = 0.5f;
    p[lt::taps] = 0.0f;
    p[lt::delaySync] = 3.5f / 17.0f;
    auto fx = makeEffect (EffectKind::latticeMod);
    fx->prepare (48000.0);
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[100] = r[100] = 0.5f;
    for (int at = 0; at < 48000; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, 48000 - at), p, 120.0);
    int peak = 0;
    for (int i = 1; i < 48000; ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) peak])) peak = i;
    CHECK (std::abs ((peak - 100) - 12000) < 200);
}

TEST (facet_linear_phase_resolution_round144)
{
    using namespace lynx;
    using namespace lynx::facet;
    auto p = makeEffectSlot (EffectKind::facetEq).params;
    CHECK ((int) effectInfo (EffectKind::facetEq).params.size() == paramCount);
    CHECK (resolution (p[(std::size_t) resolutionParam]) == 1); // Medium: the old FIR length
    CHECK (paramText (resolutionParam, 0.75f) == "Very High");
    // [EXEC] the trial's linear-phase latencies at 48 kHz: 1024 + 2^11, 2^12, 2^13, 2^14, 2^16
    const int expect[resolutionCount] = { 3072, 5120, 9216, 17408, 66560 };
    for (int r = 0; r < resolutionCount; ++r)
    {
        CHECK (latencyFor (Phase::linear, 48000.0, r) == expect[r]);
        p[(std::size_t) phaseParam] = phaseValue (Phase::linear);
        p[(std::size_t) resolutionParam] = resolutionValue (r);
        auto e = make (&p);
        e->prepare (48000.0);
        CHECK (e->latencySamples() == expect[r]);
    }
    CHECK (latencyFor (Phase::linear, 96000.0, 2) == 2048 + 16384);
    CHECK (latencyFor (Phase::natural, 48000.0, 4) == 320); // natural phase does not depend on it

    // a 40 Hz bell (+12 dB, Q 4): the longer FIR gets closer to the analog gain, with the impulse at the latency
    auto bellGain = [] (int r) {
        auto q = makeEffectSlot (EffectKind::facetEq).params;
        q[(std::size_t) param (0, 0)] = shapeValue (Shape::bell);
        q[(std::size_t) param (0, 1)] = hzValue (40.0);
        q[(std::size_t) param (0, 2)] = gainValue (12.0);
        q[(std::size_t) param (0, 3)] = qValue (4.0);
        q[(std::size_t) phaseParam] = phaseValue (Phase::linear);
        q[(std::size_t) resolutionParam] = resolutionValue (r);
        auto e = make (&q);
        e->prepare (48000.0);
        const int lat = e->latencySamples(), n = lat + 48000 * 3;
        std::vector<float> l ((std::size_t) n), rr ((std::size_t) n);
        for (int i = 0; i < n; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.1f * (float) std::sin (2.0 * 3.14159265358979 * 40.0 * i / 48000.0);
        for (int at = 0; at < n; at += 512)
            e->process (l.data() + at, rr.data() + at, std::min (512, n - at), q, 120.0);
        double peak = 0.0;
        for (int i = n - 48000; i < n; ++i) peak = std::max (peak, (double) std::abs (l[(std::size_t) i]));
        return 20.0 * std::log10 (peak / 0.1);
    };
    const auto low = bellGain (0), maxRes = bellGain (4);
    CHECK (std::abs (maxRes - 12.0) < 0.3);
    CHECK (std::abs (maxRes - 12.0) < std::abs (low - 12.0));

    // Pro-Q 4 state: 553 = the resolution index, 554 = the character index (read back from the trial's state)
    fabfilter::State s;
    s.count = 600;
    s.values.assign (600, 0.0f);
    s.values[0] = 1.0f; s.values[1] = 1.0f; s.values[2] = 9.966f; s.values[4] = 0.5f; s.values[6] = 2.0f; s.values[7] = 2.0f;
    s.values[552] = 2.0f;
    s.values[553] = 3.0f;
    s.values[554] = 2.0f;
    auto slot = makeEffectSlot (EffectKind::facetEq);
    CHECK (fabfilter::fromProQ4 (s, slot) > 0);
    CHECK (phase (slot.params[(std::size_t) phaseParam]) == Phase::linear && resolution (slot.params[(std::size_t) resolutionParam]) == 3);
    CHECK (character (slot.params[(std::size_t) characterParam]) == 2);
}
