// Round 132 (issue #4 feature requests): the Sampler's Gain knob, the tempo tapper, the utility volumes
// (metronome, Browser preview) and the Channel Rack's piano-roll view. Included at the end of tests.cpp.

TEST (feat132_sampler_gain_factor)
{
    CHECK (lynx::samplerGain (0.0f) == 1.0f);
    CHECK (std::abs (lynx::samplerGain (6.0206f) - 2.0f) < 1e-3f);
    CHECK (std::abs (lynx::samplerGain (-6.0206f) - 0.5f) < 1e-3f);
    CHECK (lynx::samplerGain (60.0f) == lynx::samplerGain (24.0f)); // clamped to +-24 dB
    CHECK (lynx::samplerGain (-99.0f) == lynx::samplerGain (-24.0f));
    CHECK (lynx::samplerGain (std::nanf ("")) == 1.0f);
    CHECK (lynx::SamplerSettings {}.gainDb == 0.0f);
}

TEST (feat132_sampler_gain_scales_the_output)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    for (int i = 0; i < 2000; ++i)
        data->left.push_back (std::sin (0.05f * (float) i) * 0.25f);

    auto render = [&data] (float gainDb, bool filtered) {
        auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        sampler->prepare (48000.0);
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.sampler.gainDb = gainDb;
        if (filtered)
            c.sampler.cutoff = 3000.0f;
        sampler->configure (c, 120.0);
        sampler->noteOn (60, 0.8f);
        std::vector<float> l (1500), r (1500);
        sampler->render (l.data(), r.data(), 1500, 0.7f, 0.7f);
        return l;
    };

    for (const auto filtered : { false, true })
    {
        const auto unity = render (0.0f, filtered);
        const auto boosted = render (6.0206f, filtered);
        const auto cut = render (-12.0412f, filtered);
        double maxErrBoost = 0.0, maxErrCut = 0.0, energy = 0.0;
        for (std::size_t i = 0; i < unity.size(); ++i)
        {
            maxErrBoost = std::max (maxErrBoost, (double) std::abs (boosted[i] - 2.0f * unity[i]));
            maxErrCut = std::max (maxErrCut, (double) std::abs (cut[i] - 0.25f * unity[i]));
            energy += (double) unity[i] * unity[i];
        }
        CHECK (energy > 1.0);
        CHECK (maxErrBoost < 1e-4 && maxErrCut < 1e-4);
    }
}

TEST (feat132_tap_tempo_states)
{
    using lynx::controls::TapTempo;
    TapTempo taps;
    CHECK (taps.state() == TapTempo::State::waiting && taps.lastTap() < 0.0);
    CHECK (! taps.tap (1000.0));
    CHECK (taps.state() == TapTempo::State::waiting);
    CHECK (taps.tap (1500.0) && *taps.tap (2000.0) == 120.0);
    CHECK (taps.state() == TapTempo::State::tapping); // three taps: still moving
    taps.tap (2500.0);
    CHECK (taps.state() == TapTempo::State::stable && taps.lastTap() == 2500.0);
    // an uneven tap (30% late) is not stable
    taps.tap (3150.0);
    CHECK (taps.state() == TapTempo::State::tapping);
    // a pause longer than the reset starts over
    CHECK (! taps.tap (3150.0 + TapTempo::resetMs + 1.0) && taps.count() == 1);
}

TEST (feat132_tapper_sync_and_nudge)
{
    using namespace lynx::controls;
    CHECK (nearestBeatTicks (0.0) == 0.0);
    CHECK (nearestBeatTicks (lynx::ppq * 3.4) == lynx::ppq * 3.0);
    CHECK (nearestBeatTicks (lynx::ppq * 3.6) == lynx::ppq * 4.0);
    CHECK (nearestBeatTicks (-50.0) == 0.0);
    CHECK (std::abs (nudgedTempo (100.0, true) - 104.0) < 1e-9 && std::abs (nudgedTempo (100.0, false) - 96.0) < 1e-9);
    CHECK (nudgedTempo (lynx::maxTempo, true) == lynx::maxTempo);
}

TEST (feat132_utility_volumes)
{
    using namespace lynx::controls;
    CHECK (metronomeGain (1.0) == 1.0f && previewGain (1.0) == 1.0f); // 100% keeps the old levels
    CHECK (metronomeGain (0.0) == 0.0f);
    CHECK (std::abs (20.0 * std::log10 ((double) previewGain (0.5)) + 12.0412) < 1e-3); // 50% = -12 dB
    CHECK (metronomeGain (9.0) == metronomeGain (maxUtilityVolume));
    CHECK (std::abs (metronomeGain (maxUtilityVolume) - 1.5625f) < 1e-6f);
    CHECK (clampUtilityVolume (-1.0) == 0.0 && clampUtilityVolume (std::nan ("")) == 1.0 && clampUtilityVolume (0.4) == 0.4);
}

TEST (feat132_rack_piano_roll_view)
{
    lynx::Pattern p;
    p.notes.push_back ({ 0, lynx::ticksPerStep, 0, 60 }); // a step on channel 0
    // steps stay step buttons, unless the piano-roll view is on (then every row, even an empty one, is a miniature)
    CHECK (! lynx::showsNotePreview (p, 0) && ! lynx::showsNotePreview (p, 0, false));
    CHECK (lynx::showsNotePreview (p, 0, true) && lynx::showsNotePreview (p, 3, true));
    p.notes.push_back ({ 0, lynx::ticksPerStep * 4, 1, 64 }); // a long note on channel 1
    CHECK (lynx::showsNotePreview (p, 1, false));
}

TEST (feat132_new_actions)
{
    for (const auto* id : { "view.audioEditor", "view.tempoTapper", "rack.pianoRollView" })
        CHECK (lynx::keys::findAction (id) != nullptr);
    lynx::keys::KeyMap map;
    CHECK (map.conflicts().empty());
}
