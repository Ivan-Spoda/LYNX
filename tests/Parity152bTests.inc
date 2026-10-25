// --- Round 152b: parity items (docs/PARITY.md, REA_rodada152b) ----------------------------------------------------
// Ember Saturator ← Saturn 2: each crossover's own slope, linear phase, modulation, the Collapse character's measured
// pitch drop; LYNX Tune ← Waves Tune Real-Time: Group Octaves off (each key its own status).

namespace p152b
{
// a block through Ember (512-sample blocks), the same signal on both channels
inline std::vector<float> run (const lynx::EffectParams& p, std::vector<float> x, int* latency = nullptr)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::ember);
    slot.params = p;
    auto fx = lynx::makeEffect (slot);
    fx->prepare (48000.0);
    fx->reset();
    auto r = x;
    const int n = (int) x.size();
    for (int at = 0; at < n; at += 512)
        fx->process (x.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    if (latency != nullptr) *latency = fx->latencySamples();
    return x;
}
// the magnitude (dB) of an impulse response at hz, the impulse at `at`
inline double responseDb (const std::vector<float>& h, int at, double hz)
{
    double c = 0.0, s = 0.0;
    for (std::size_t i = 0; i < h.size(); ++i) // the whole response (a zero-phase FIR rings before `at` too)
    {
        const auto w = 2.0 * 3.14159265358979 * hz * ((double) i - (double) at) / 48000.0;
        c += h[i] * std::cos (w);
        s += h[i] * std::sin (w);
    }
    return 20.0 * std::log10 (std::max (std::sqrt (c * c + s * s), 1e-12));
}
inline lynx::EffectParams twoBands (int slopeDbOct, bool linear)
{
    using namespace lynx::mixsuite::ember;
    auto p = lynx::makeEffectSlot (lynx::EffectKind::ember).params;
    p[bands] = 1.0f / 3.0f;
    p[xover1] = crossoverValue (1000.0);
    p[crossSlope1] = ownSlopeValue (slopeDbOct);
    p[processing] = linear ? 1.0f : 0.0f;
    p[band1Mix] = 0.0f; // the bands' dry signal: the crossover alone
    p[band2Mix] = 0.0f;
    return p;
}
} // namespace p152b

TEST (ember_round152_parameter_block_and_upgrade)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    CHECK (em::count <= maxEffectParams);
    CHECK (effectInfo (EffectKind::ember).params.size() == (std::size_t) em::count);
    const auto base = makeEffectSlot (EffectKind::ember).params;
    for (int k = 0; k < 5; ++k) CHECK (base[(std::size_t) (em::crossSlope1 + k)] == 0.0f);
    CHECK (! em::linearPhase (base) && ! em::modulated (base) && em::measuredEffects (base));
    CHECK (em::crossoverSlopeDbOct (base, 2) == em::slopeDbOct (base[em::xoverSlope]));
    // a project saved before round 152 keeps the static effect characters
    auto old = makeEffectSlot (EffectKind::ember);
    upgradeLoadedEffectParams (old, 133);
    CHECK (! em::measuredEffects (old.params));
    auto fresh = makeEffectSlot (EffectKind::ember);
    upgradeLoadedEffectParams (fresh, (std::size_t) em::count);
    CHECK (em::measuredEffects (fresh.params));
    // the laws read from the trial's value texts [EXEC]
    for (const int s : { 6, 12, 24, 36, 48 }) CHECK (em::ownSlopeDbOct (em::ownSlopeValue (s)) == s);
    CHECK (std::abs (em::lfoRateHz (0.5f) - 3.52) < 1e-6 && std::abs (em::lfoRateHz (0.25f) - 1.02) < 1e-6);
    CHECK (std::abs (em::lfoRateHz (base[em::lfo1Rate]) - 1.0) < 0.01);
    CHECK (std::abs (em::lfoRateValue (32.519) - 0.75f) < 1e-4);
    CHECK (std::abs (em::modAmount (0.75f) - 0.16) < 1e-9 && std::abs (em::modAmount (0.25f) + 0.16) < 1e-9);
    CHECK (std::abs (em::modAmount (em::modAmountValue (0.498)) - 0.498) < 1e-6);
    CHECK (em::modAmount (0.5f) == 0.0);
    CHECK (std::abs (em::envAttackMs (0.5f) - 100.9) < 1e-6 && std::abs (em::envReleaseMs (0.0f) - 50.0) < 1e-9);
    CHECK (std::abs (em::envReleaseMs (1.0f) - 5000.0) < 1e-9);
    CHECK (std::abs (em::envSource (-40.6) - 0.67) < 1e-9 && em::envSource (-90.0) == 0.0);
    CHECK (std::abs (em::modDelta (10, 0.16) - 0.32) < 1e-9); // B1 level: 72 dB × 0.16 = 11.52 dB on Ember's ±18 dB
    CHECK (em::modTargetParam (4) == em::drive && em::modTargetParam (22) == em::xover1 && em::modTargetParam (26) == em::xover5);
    // the LFO shapes (two steps +1 / −1 with a glide)
    using em::LfoShape;
    CHECK (em::lfoValue (LfoShape::square, 0.1) == 1.0 && em::lfoValue (LfoShape::square, 0.6) == -1.0);
    CHECK (std::abs (em::lfoValue (LfoShape::triangle, 0.25)) < 1e-12 && std::abs (em::lfoValue (LfoShape::triangle, 0.5) - 1.0) < 1e-12);
    CHECK (std::abs (em::lfoValue (LfoShape::sine, 0.25)) < 1e-12);
    CHECK (em::lfoValue (LfoShape::easeIn, 0.125) < em::lfoValue (LfoShape::triangle, 0.125));
    CHECK (em::lfoValue (LfoShape::easeOut, 0.125) > em::lfoValue (LfoShape::triangle, 0.125));
    CHECK (std::abs (em::collapseRatio (1.0) - 0.25) < 1e-12 && std::abs (em::collapseRatio (0.2) - 0.757858) < 1e-5);
    CHECK (std::abs (em::linearLow (100.0, 1000.0, 6) - 1.0 / 1.1) < 1e-12);
}

TEST (ember_round152_own_slopes_match_the_trial_minimum_phase)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    std::vector<float> imp (16384, 0.0f);
    imp[100] = 1.0f;
    // [EXEC] the trial's low band at 2 kHz / 4 kHz with the crossover at 1 kHz: 6 dB/oct −7.01 / −12.48,
    // 12 dB/oct −14.03 / −24.96, 24 −24.73 / −48.95, 36 −36.46 / −73.41, 48 −48.47 / −97.40 (the IIR designs of LrSplit)
    const std::pair<int, std::pair<double, double>> ref[] { { 6, { -7.01, -12.48 } }, { 12, { -14.03, -24.96 } },
                                                            { 24, { -24.73, -48.95 } }, { 36, { -36.46, -73.41 } } };
    for (const auto& [slope, db] : ref)
    {
        auto p = p152b::twoBands (slope, false);
        p[em::band2Mix] = 1.0f;
        p[em::bandExtra (1, 1)] = 1.0f; // band 2 muted: the low band alone
        int lat = 0;
        const auto h = p152b::run (p, imp, &lat);
        CHECK (std::abs (p152b::responseDb (h, 100 + lat, 2000.0) - db.first) < 0.1);
        CHECK (std::abs (p152b::responseDb (h, 100 + lat, 4000.0) - db.second) < 0.2);
        // both bands (dry): the all-pass sum
        const auto s = p152b::run (p152b::twoBands (slope, false), imp);
        for (const double hz : { 200.0, 1000.0, 5000.0 })
            CHECK (std::abs (p152b::responseDb (s, 100 + lat, hz)) < 0.05);
    }
    // a global slope (own = 0) is unchanged
    auto p = p152b::twoBands (24, false);
    p[em::crossSlope1] = 0.0f;
    p[em::xoverSlope] = 1.0f;
    CHECK (em::crossoverSlopeDbOct (p, 0) == 48);
}

TEST (ember_round152_linear_phase_bands_and_latency)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    std::vector<float> imp (24000, 0.0f);
    imp[100] = 1.0f;
    // [EXEC] the trial's linear-phase low band (crossover 1 kHz): 6 dB/oct −0.82 dB at 100 Hz, −6.02 at 1 kHz,
    // −20.82 at 10 kHz; 24 dB/oct −0.52 at 500 Hz, −24.59 at 2 kHz; the sum is the delayed input; latency 3072 + LYNX's
    // oversampler
    for (const auto& [slope, f, db] : std::vector<std::tuple<int, double, double>> { { 6, 100.0, -0.82 }, { 6, 1000.0, -6.02 }, { 6, 10000.0, -20.82 },
                                                                                       { 24, 500.0, -0.52 }, { 24, 2000.0, -24.59 } })
    {
        auto p = p152b::twoBands (slope, true);
        p[em::band2Mix] = 1.0f;
        p[em::bandExtra (1, 1)] = 1.0f;
        int lat = 0;
        const auto h = p152b::run (p, imp, &lat);
        CHECK (lat == em::linearLatency (48000.0) + mixsuite::Oversampler::latency());
        CHECK (em::linearLatency (48000.0) == 3072);
        CHECK (std::abs (p152b::responseDb (h, 100 + lat, f) - db) < 0.1);
        // zero phase: the response is symmetric around the latency
        double asym = 0.0;
        for (int k = 1; k < 200; ++k) asym = std::max (asym, (double) std::abs (h[(std::size_t) (100 + lat + k)] - h[(std::size_t) (100 + lat - k)]));
        CHECK (asym < 1e-3);
    }
    int lat = 0;
    const auto s = p152b::run (p152b::twoBands (12, true), imp, &lat);
    CHECK (std::abs (s[(std::size_t) (100 + lat)] - 1.0f) < 1e-3);
    double rest = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((int) i != 100 + lat) rest = std::max (rest, (double) std::abs (s[i]));
    CHECK (rest < 1e-3);
    // one band with linear phase: delayed alike
    auto one = makeEffectSlot (EffectKind::ember).params;
    one[em::processing] = 1.0f;
    one[em::mix] = 0.0f;
    const auto d = p152b::run (one, imp, &lat);
    CHECK (std::abs (d[(std::size_t) (100 + lat)] - 1.0f) < 1e-4);
}

TEST (ember_round152_lfo_and_follower_modulate_the_band_level)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    constexpr int n = 96000;
    std::vector<float> tone (n);
    for (int i = 0; i < n; ++i) tone[(std::size_t) i] = (float) (0.1 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
    auto ref = makeEffectSlot (EffectKind::ember).params;
    const auto y0 = p152b::run (ref, tone);
    // [EXEC] the trial: XLFO 1 (1 Hz, two steps +1 / −1, no glide) → B1 Output Level at slot level 0.160: ±11.52 dB
    auto p = ref;
    p[em::lfo1Shape] = 2.0f / 4.0f; // square
    p[em::mod1Source] = 1.0f / 3.0f;
    p[em::mod1Target] = 10.0f / 26.0f;
    p[em::mod1Amount] = 0.75f; // +0.160
    CHECK (em::modulated (p));
    const auto y = p152b::run (p, tone);
    auto rmsDb = [] (const std::vector<float>& v, int from, int len) {
        double s = 0.0;
        for (int i = from; i < from + len; ++i) s += (double) v[(std::size_t) i] * v[(std::size_t) i];
        return 10.0 * std::log10 (s / len);
    };
    // first half period (+1), second (−1); 48 ms windows away from the steps
    CHECK (std::abs (rmsDb (y, 4800, 2400) - rmsDb (y0, 4800, 2400) - 11.52) < 0.2);
    CHECK (std::abs (rmsDb (y, 28800, 2400) - rmsDb (y0, 28800, 2400) + 11.52) < 0.2);
    // the follower: a steady −20 dBFS-peak sine gives about (−20.5 + 60.7) / 30 = 1.34 source units → +15.4 dB at 0.16
    auto q = ref;
    q[em::mod1Source] = 1.0f;
    q[em::mod1Target] = 10.0f / 26.0f;
    q[em::mod1Amount] = 0.75f;
    const auto z = p152b::run (q, tone);
    const auto gain = rmsDb (z, 72000, 4800) - rmsDb (y0, 72000, 4800);
    CHECK (gain > 14.0 && gain < 16.5);
}

TEST (ember_round152_collapse_pitches_down_with_the_drive)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    constexpr int n = 48000;
    std::vector<float> tone (n);
    for (int i = 0; i < n; ++i) tone[(std::size_t) i] = (float) (0.25 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
    auto freqOf = [] (const std::vector<float>& y) {
        double best = 0.0, bestHz = 0.0;
        for (double hz = 100.0; hz <= 1200.0; hz += 5.0)
        {
            double c = 0.0, s = 0.0;
            for (std::size_t i = 24000; i < y.size(); ++i)
            {
                const auto w = 2.0 * 3.14159265358979 * hz * (double) i / 48000.0;
                c += y[i] * std::cos (w);
                s += y[i] * std::sin (w);
            }
            if (c * c + s * s > best) { best = c * c + s * s; bestHz = hz; }
        }
        return bestHz;
    };
    auto p = makeEffectSlot (EffectKind::ember).params;
    p[em::characterParam (0)] = (float) em::collapseCharacter / (float) em::characters;
    p[em::drive] = (float) (30.0 / 36.0); // the reference's 100 %
    // [EXEC] the trial's breakdown style: 1 kHz → 250 Hz at 100 %, 500 Hz at 50 %
    CHECK (std::abs (freqOf (p152b::run (p, tone)) - 250.0) <= 5.0);
    p[em::drive] = (float) (15.0 / 36.0);
    CHECK (std::abs (freqOf (p152b::run (p, tone)) - 500.0) <= 5.0);
    // the old (static) stand-in keeps the input's pitch
    p[em::effectLaw] = 0.0f;
    CHECK (std::abs (freqOf (p152b::run (p, tone)) - 1000.0) <= 5.0);
}

TEST (ember_round152_saturn2_slopes_processing_modulation_translated)
{
    using namespace lynx;
    PluginIdentity id;
    id.name = "FabFilter Saturn 2";
    // [EXEC] read back from the trial's state: band b's crossover slope at 7 + 17b + 16 (0 … 4 = 6 … 48), 696 the
    // processing mode, XLFO 1 at 135 (rate knob value; 143 steps; step j at 144 + 4j: value, glide, function), 646 the
    // follower count, 649 / 650 its attack / release knob values, 700 the slot count, slot k at 701 + 5k (source 13 =
    // XLFO 1, 25 = EF 1; level knob value; bypassed; inverted; target 10 = B1 Output Level, 8 = B1 Drive)
    std::vector<std::pair<int, float>> v { { 5, 100.0f }, { 6, 3.0f }, { 696, 1.0f }, { 134, 1.0f }, { 135, 0.5f }, { 143, 2.0f },
                                           { 144, 1.0f }, { 145, 1.0f }, { 146, 3.0f }, { 148, -1.0f }, { 149, 1.0f }, { 150, 3.0f },
                                           { 646, 1.0f }, { 649, 0.5f }, { 650, 0.25f },
                                           { 700, 2.0f }, { 701, 13.0f }, { 702, 0.5f }, { 705, 10.0f },
                                           { 706, 25.0f }, { 707, 0.75f }, { 709, 1.0f }, { 710, 8.0f } };
    for (int b = 0; b < 3; ++b)
    {
        v.push_back ({ 7 + 17 * b + 10, 100.0f });
        v.push_back ({ 7 + 17 * b + 13, 1.0f });
        v.push_back ({ 7 + 17 * b + 15, (float) std::log2 (200.0 * std::pow (4.0, b)) });
        v.push_back ({ 7 + 17 * b + 16, (float) (b == 0 ? 0 : 3) });
    }
    id.state = dyn108::ffbs (951, v);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr) return;
    const auto slot = makeSubstituteSlot (*entry, &id);
    namespace e = mixsuite::ember;
    const auto& p = slot.params;
    CHECK (e::activeBands (p) == 3);
    CHECK (e::crossoverSlopeDbOct (p, 0) == 6 && e::crossoverSlopeDbOct (p, 1) == 36);
    CHECK (e::linearPhase (p) && e::measuredEffects (p));
    CHECK (std::abs (e::lfoRateHz (p[e::lfo1Rate]) - 3.52) < 1e-4);
    CHECK (e::lfoShapeOf (p[e::lfo1Shape]) == e::LfoShape::sine);
    CHECK (std::abs (e::envAttackMs (p[e::envAttack]) - 100.9) < 0.05 && std::abs (e::envReleaseMs (p[e::envRelease]) - 236.6) < 0.5);
    CHECK (e::modSourceOf (p[e::mod1Source]) == e::ModSource::lfo1 && e::modTargetOf (p[e::mod1Target]) == 10);
    CHECK (std::abs (e::modAmount (p[e::mod1Amount]) - 0.16) < 1e-4);
    CHECK (e::modSourceOf (p[e::mod2Source]) == e::ModSource::envelope && e::modTargetOf (p[e::mod2Target]) == 4);
    CHECK (std::abs (e::modAmount (p[e::mod2Amount]) + 0.498) < 1e-3); // inverted
    CHECK (e::modSourceOf (p[e::mod3Source]) == e::ModSource::off);
}

TEST (lynxtune_round152_group_octaves_off)
{
    using namespace lynx;
    namespace lt = lynxtune;
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    auto& p = slot.params;
    CHECK (lt::count <= maxEffectParams);
    CHECK (effectInfo (EffectKind::lynxTune).params.size() == (std::size_t) lt::count);
    CHECK (lt::groupOctavesOn (p.data()) && ! lt::anyKeyOverride (p.data()));
    CHECK (lt::paramText (lt::groupOctaves, 1.0f) == "On");
    // the key map: exact round trips for every key and status
    for (int note = lt::keyMapFirst; note < lt::keyMapFirst + lt::keyMapKeys; note += 5)
        for (int st = 0; st < lt::keyStatuses; ++st)
        {
            auto q = p;
            lt::setKeyOverride (q.data(), note, (lt::KeyStatus) st);
            lt::setKeyOverride (q.data(), note + 1, lt::KeyStatus::bypass);
            CHECK (lt::keyOverride (q.data(), note) == (lt::KeyStatus) st);
            CHECK (note + 1 >= lt::keyMapFirst + lt::keyMapKeys || lt::keyOverride (q.data(), note + 1) == lt::KeyStatus::bypass);
        }
    CHECK (lt::keyOverride (p.data(), 10) == lt::KeyStatus::follow);
    // chromatic: remove E4 (64) only; Group octaves on → the key map is ignored
    p[lt::scale] = 0.0f; // chromatic
    const auto mask = (std::uint16_t) 0xfff;
    lt::setKeyOverride (p.data(), 64, lt::KeyStatus::removedUp);
    lt::setKeyOverride (p.data(), 52, lt::KeyStatus::bypass);
    CHECK (lt::noteStatus (p.data(), 64, mask) == lt::KeyStatus::legal);
    p[lt::groupOctaves] = 0.0f;
    CHECK (lt::noteStatus (p.data(), 64, mask) == lt::KeyStatus::removedUp);
    CHECK (lt::noteStatus (p.data(), 76, mask) == lt::KeyStatus::legal); // E5: its note name's status
    CHECK (lt::noteStatus (p.data(), 52, mask) == lt::KeyStatus::bypass);
    // a sung E4 goes up to F4; a sung E5 stays; near E4 from below the nearest allowed key is D#4
    CHECK (lt::targetNoteKeys (64.1, p.data(), mask, -1, 0.1) == 65);
    CHECK (lt::targetNoteKeys (76.1, p.data(), mask, -1, 0.1) == 76);
    CHECK (lt::targetNoteKeys (63.4, p.data(), mask, -1, 0.1) == 63);
    lt::setKeyOverride (p.data(), 64, lt::KeyStatus::removed);
    CHECK (lt::targetNoteKeys (64.2, p.data(), mask, -1, 0.1) == 63 || lt::targetNoteKeys (64.2, p.data(), mask, -1, 0.1) == 65);
    // the status cycle of a REMOVE click
    CHECK (lt::nextRemoveStatus (lt::KeyStatus::legal) == lt::KeyStatus::removed);
    CHECK (lt::nextRemoveStatus (lt::KeyStatus::removed) == lt::KeyStatus::removedUp);
    CHECK (lt::nextRemoveStatus (lt::KeyStatus::removedDown) == lt::KeyStatus::legal);
    // Send carries Group octaves and the key map
    const auto sh = lt::captureScale (p.data());
    auto other = makeEffectSlot (EffectKind::lynxTune).params;
    lt::applyScale (other.data(), sh);
    CHECK (! lt::groupOctavesOn (other.data()) && lt::keyOverride (other.data(), 52) == lt::KeyStatus::bypass);
    lt::clearKeyOverrides (p.data());
    CHECK (! lt::anyKeyOverride (p.data()));
}

TEST (lynxtune_round152_group_octaves_off_corrects_per_key)
{
    using namespace lynx;
    namespace lt = lynxtune;
    // a steady 330 Hz (E4 + ~3 cents) in a chromatic scale: corrected to E4 with octaves grouped; with E4 removed
    // (pitch up) on its own key, pulled to F4 (349.2 Hz)
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    auto& p = slot.params;
    p[lt::scale] = 0.0f;
    p[lt::retune] = 0.0f;
    p[lt::groupOctaves] = 0.0f;
    lt::setKeyOverride (p.data(), 64, lt::KeyStatus::removedUp);
    auto fx = makeEffect (slot);
    fx->prepare (48000.0);
    constexpr int n = 48000;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
    {
        double x = 0.0;
        for (int h = 1; h <= 6; ++h) x += 0.3 / h * std::sin (2.0 * 3.14159265358979 * 330.0 * h * i / 48000.0);
        l[(std::size_t) i] = r[(std::size_t) i] = (float) x;
    }
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    // the fundamental: the strongest component 300 … 380 Hz over the last half second
    double best = 0.0, hz = 0.0;
    for (double f = 300.0; f <= 380.0; f += 1.0)
    {
        double c = 0.0, s = 0.0;
        for (int i = n / 2; i < n; ++i)
        {
            const auto w = 2.0 * 3.14159265358979 * f * i / 48000.0;
            c += l[(std::size_t) i] * std::cos (w);
            s += l[(std::size_t) i] * std::sin (w);
        }
        if (c * c + s * s > best) { best = c * c + s * s; hz = f; }
    }
    CHECK (std::abs (hz - 349.2) < 3.0);
}

TEST (lynxtune_round152_midi_reference_tone)
{
    using namespace lynx;
    namespace lt = lynxtune;
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    auto& p = slot.params;
    CHECK (p[lt::referenceTone] == 0.0f && std::abs (lt::referenceToneDb (p[lt::referenceToneLevel])) < 1e-9);
    p[lt::midiChannel] = 1.0f / 128.0f; // channel 1
    p[lt::referenceTone] = 1.0f;
    p[lt::mix] = 0.0f;
    auto fx = makeEffect (slot);
    fx->prepare (48000.0);
    std::array<NoteMask, 1> held {};
    held[0][1] = 1ull << (69 - 64); // A4 held
    fx->setHeldNotes (held.data(), 1);
    constexpr int n = 24000;
    std::vector<float> l (n, 0.0f), r (n, 0.0f);
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    // a 440 Hz sine at 0.25 (−12 dBFS peak) once faded in
    double c = 0.0, s = 0.0, peak = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        const auto w = 2.0 * 3.14159265358979 * 440.0 * i / 48000.0;
        c += l[(std::size_t) i] * std::cos (w);
        s += l[(std::size_t) i] * std::sin (w);
        peak = std::max (peak, (double) std::abs (l[(std::size_t) i]));
    }
    CHECK (std::abs (2.0 * std::sqrt (c * c + s * s) / (n / 2) - 0.25) < 0.01);
    CHECK (peak < 0.26);
    // nothing held: silence after the fade
    held[0][1] = 0;
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    CHECK (std::abs (l[(std::size_t) n - 1]) < 1e-6);
}
