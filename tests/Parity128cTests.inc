// --- Round 128 (parity128c): LYNX Tune's parity with the auto-mode corrector it stands in for -----------------------
// Natural vibrato, per-note Bypass vs Remove, created vibrato, ignore vibrato, auto key, the extra scales and the
// Auto-Tune Pro translation of them. Synthetic vowels (r95::vowel) so pitch can be measured against a known truth.

namespace p128c
{
// peak-to-peak (cents) of the output pitch around `ref` Hz between t0 and t1 seconds (10 ms steps)
double pitchSpan (const std::vector<float>& y, int latency, double fs, double ref, double t0, double t1, double* maxCents = nullptr)
{
    double lo = 1e9, hi = -1e9;
    for (double t = t0; t < t1; t += 0.01)
    {
        const auto hz = r95::measureHz (y, (int) (t * fs) - 512 + latency, 1024, fs, 150.0, 500.0);
        if (hz <= 0.0)
            continue;
        const auto c = r95::cents (hz, ref);
        lo = std::min (lo, c);
        hi = std::max (hi, c);
    }
    if (maxCents != nullptr)
        *maxCents = hi;
    return hi - lo;
}

lynx::EffectParams chromatic (double retuneMs)
{
    auto p = r95::tuneDefaults();
    p[lynx::lynxtune::scale] = 0.0f; // chromatic
    p[lynx::lynxtune::retune] = (float) std::sqrt (retuneMs / 400.0);
    return p;
}

std::vector<std::uint8_t> autoTuneStateWithEdits (const std::string& extraParams, const std::string& scales)
{
    const auto body = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <ActualParams><Parameter X1=\"1.0\"/><Parameter X2=\"9.0\"/>")
                      + "<Parameter X4=\"20.0\"/>" + extraParams + "</ActualParams>";
    std::string params ("VC2!");
    for (int i = 0; i < 4; ++i)
        params += (char) (body.size() >> (8 * i));
    params += body;
    const auto xml = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?> <SessionChunkData><ChunkForVersion ChunkVersion=\"10.0.0\">")
                     + "<Parameters ParamsData=\"" + tune120::juceBase64 (params) + "\" IsUnencrypted=\"1\"/><Custom RAND_ID=\"1\" ScaleGivenKey=\"9\""
                     + " ScaleGivenScale=\"1\" ScaleGivenDetune=\"0\"/>" + scales + "</ChunkForVersion></SessionChunkData>";
    std::vector<std::uint8_t> c { 'V', 'C', '2', '!' };
    for (int i = 0; i < 4; ++i)
        c.push_back ((std::uint8_t) (xml.size() >> (8 * i)));
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
} // namespace p128c

TEST (lynx_tune_round128_params_are_neutral_by_default)
{
    using namespace lynx;
    const auto& info = effectInfo (EffectKind::lynxTune);
    CHECK ((int) info.params.size() == lynxtune::count && lynxtune::count <= maxEffectParams);
    auto slot = makeEffectSlot (EffectKind::lynxTune);
    CHECK (lynxtune::naturalVibratoDb (slot.params[lynxtune::naturalVibrato]) == 0.0);
    CHECK (lynxtune::vibratoShapeIndex (slot.params[lynxtune::vibratoShape]) == 0);
    CHECK (std::abs (lynxtune::vibratoRateHz (slot.params[lynxtune::vibratoRate]) - 5.5) < 1e-4);
    CHECK (std::abs (lynxtune::vibratoDepthCents (slot.params[lynxtune::vibratoDepth]) - 18.0) < 1e-3);
    CHECK (std::abs (lynxtune::vibratoOnsetMs (slot.params[lynxtune::vibratoDelay]) - 500.0) < 1e-3);
    for (int k = 0; k < 12; ++k)
        CHECK (slot.params[(std::size_t) (lynxtune::bypassC + k)] == 0.0f);
    CHECK (slot.params[lynxtune::ignoreVibrato] == 0.0f && slot.params[lynxtune::moreScales] == 0.0f);
    CHECK (formatEffectParam (EffectKind::lynxTune, lynxtune::naturalVibrato, 0.75f, 120.0) == "+6.0 dB");
    CHECK (formatEffectParam (EffectKind::lynxTune, lynxtune::vibratoShape, 1.0f / 3.0f, 120.0) == "Sine");
    CHECK (formatEffectParam (EffectKind::lynxTune, lynxtune::moreScales, 1.0f, 120.0) == "Diminished");

    // an old project (no round-128 values saved) plays exactly as before: the defaults leave the output untouched by
    // any of the new stages (bypass flags on notes outside the scale, a vibrato rate with shape "none", 0 dB)
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 0.8), [] (double t) { return 220.0 * std::exp2 ((30.0 + 25.0 * std::sin (2.0 * r95::pi * 5.0 * t)) / 1200.0); });
    auto p = r95::tuneDefaults();
    int latency = 0;
    const auto a = r95::runTune (x, fs, p, latency);
    p[lynxtune::vibratoRate] = 0.9f;
    p[lynxtune::vibratoDepth] = 1.0f;
    p[(std::size_t) (lynxtune::bypassC + 1)] = 1.0f; // C#: not in C major
    const auto b = r95::runTune (x, fs, p, latency);
    CHECK (a == b);
}

TEST (lynx_tune_round128_extra_scales)
{
    using namespace lynx;
    CHECK (vocal::scaleMask (vocal::Scale::wholeTone, 0) == 0b010101010101);
    CHECK (vocal::scaleMask (vocal::Scale::diminished, 0) == 0b101101101101);
    CHECK (lynxtune::effectiveScale (0.0f, 0.5f) == (int) vocal::Scale::wholeTone);
    CHECK (lynxtune::effectiveScale (0.0f, 1.0f) == (int) vocal::Scale::diminished);
    CHECK (lynxtune::effectiveScale (1.0f / 13.0f, 0.0f) == (int) vocal::Scale::major);
    CHECK (std::string (vocal::scaleName (vocal::Scale::wholeTone)) == "Whole tone");
}

TEST (lynx_tune_round128_natural_vibrato)
{
    // a held A3 with a ±40 c, 5.5 Hz vibrato and a slow retune (the vibrato survives): +6 dB doubles it, −12 dB keeps a quarter
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 1.6), [] (double t) { return 220.0 * std::exp2 (40.0 * std::sin (2.0 * r95::pi * 5.5 * t) / 1200.0); });
    double span[3] {};
    const float values[3] { 0.5f, 0.75f, 0.0f };
    for (int k = 0; k < 3; ++k)
    {
        auto p = p128c::chromatic (400.0);
        p[lynx::lynxtune::naturalVibrato] = values[k];
        int latency = 0;
        const auto y = r95::runTune (x, fs, p, latency);
        span[k] = p128c::pitchSpan (y, latency, fs, 220.0, 0.6, 1.5);
    }
    CHECK (span[0] > 50.0);
    CHECK (span[1] / span[0] > 1.6 && span[1] / span[0] < 2.4);
    CHECK (span[2] / span[0] < 0.5);
}

TEST (lynx_tune_round128_bypass_and_remove)
{
    // A3 +40 c, instant retune, chromatic: corrected to A; A bypassed: left at +40 c; A removed: retargets to A# (+100 c)
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 0.8), [] (double) { return 220.0 * std::exp2 (40.0 / 1200.0); });
    auto out = [&] (int bypassA, int onA) {
        auto p = p128c::chromatic (0.0);
        p[(std::size_t) (lynx::lynxtune::bypassC + 9)] = (float) bypassA;
        p[(std::size_t) (lynx::lynxtune::noteC + 9)] = (float) onA;
        int latency = 0;
        const auto y = r95::runTune (x, fs, p, latency);
        return r95::cents (r95::measureHz (y, (int) (0.5 * fs) + latency, 2048, fs), 220.0);
    };
    CHECK (std::abs (out (0, 1)) < 4.0);
    CHECK (std::abs (out (1, 1) - 40.0) < 4.0);
    CHECK (std::abs (out (0, 0) - 100.0) < 4.0);
    CHECK (std::abs (out (1, 0) - 100.0) < 4.0); // a removed note is not a target, bypassed or not
}

TEST (lynx_tune_round128_created_vibrato)
{
    // a steady A3, instant retune, a 5 Hz sine vibrato of 50 c from the note's start: ~100 c peak to peak once it is in
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 1.4), [] (double) { return 220.0; });
    auto p = p128c::chromatic (0.0);
    int latency = 0;
    const auto plain = r95::runTune (x, fs, p, latency);
    CHECK (p128c::pitchSpan (plain, latency, fs, 220.0, 0.5, 1.3) < 8.0);
    p[lynx::lynxtune::vibratoShape] = 1.0f / 3.0f;
    p[lynx::lynxtune::vibratoRate] = (float) ((5.0 - 0.1) / 9.9);
    p[lynx::lynxtune::vibratoDepth] = 0.5f;
    p[lynx::lynxtune::vibratoDelay] = 0.2f;  // 300 ms
    p[lynx::lynxtune::vibratoOnset] = 0.1f;  // 150 ms
    const auto y = r95::runTune (x, fs, p, latency);
    CHECK (p128c::pitchSpan (y, latency, fs, 220.0, 0.08, 0.25) < 10.0); // before the onset delay
    const auto span = p128c::pitchSpan (y, latency, fs, 220.0, 0.7, 1.3);
    CHECK (span > 75.0 && span < 115.0);
}

TEST (lynx_tune_round128_ignore_vibrato)
{
    // A3 +20 c with a ±45 c vibrato: its peaks (+65 c) cross toward A#; the instant retune follows them unless the target
    // is chosen from the pitch with its vibrato smoothed away
    const double fs = 44100.0;
    const auto x = r95::vowel (fs, (int) (fs * 1.6), [] (double t) { return 220.0 * std::exp2 ((20.0 + 45.0 * std::sin (2.0 * r95::pi * 5.0 * t)) / 1200.0); });
    auto p = p128c::chromatic (0.0);
    int latency = 0;
    double hiPlain = 0.0, hiIgnore = 0.0;
    p128c::pitchSpan (r95::runTune (x, fs, p, latency), latency, fs, 220.0, 0.6, 1.5, &hiPlain);
    p[lynx::lynxtune::ignoreVibrato] = 1.0f;
    p128c::pitchSpan (r95::runTune (x, fs, p, latency), latency, fs, 220.0, 0.6, 1.5, &hiIgnore);
    CHECK (hiPlain > 70.0);
    CHECK (hiIgnore < 30.0);
}

TEST (lynx_tune_round128_auto_key)
{
    using namespace lynx;
    // time per pitch class of a melody in A minor (tonic and fifth longer) and one in E major
    std::array<double, 12> aMinor {}, eMajor {};
    for (const int pc : { 9, 11, 0, 2, 4, 5, 7 })
        aMinor[(std::size_t) pc] = 1.0;
    aMinor[9] = 3.0, aMinor[4] = 2.0, aMinor[0] = 1.5;
    for (const int pc : { 4, 6, 8, 9, 11, 1, 3 })
        eMajor[(std::size_t) pc] = 1.0;
    eMajor[4] = 3.0, eMajor[11] = 2.0, eMajor[8] = 1.5;
    const auto a = vocal::detectKey (aMinor), e = vocal::detectKey (eMajor);
    CHECK (a.first == 9 && a.second == vocal::Scale::minor);
    CHECK (e.first == 4 && e.second == vocal::Scale::major);
    CHECK (vocal::detectKey ({}).first == -1);
}

TEST (lynx_tune_round128_auto_tune_pro_translation)
{
    using namespace lynx;
    // A minor; Natural Vibrato +3 dB, Ignore Vibrato on, Create Vibrato square 4 Hz 30 c, onset 250 / 750 ms; the minor
    // scale's root bypassed and its 4th degree (D) removed
    const auto state = p128c::autoTuneStateWithEdits (
        "<Parameter X5=\"2.0\"/><Parameter X6=\"30.0\"/><Parameter X7=\"4.0\"/><Parameter X44=\"1.0\"/><Parameter X46=\"250.0\"/>"
        "<Parameter X50=\"750.0\"/><Parameter X62=\"3.0\"/>",
        "<Scales><Scale ScaleIndex=\"0\" Length=\"7\"><Bypass Values=\"0000000\"/><Remove Values=\"0000000\"/></Scale>"
        "<Scale ScaleIndex=\"1\" Length=\"7\"><Bypass Values=\"1000000\"/><Remove Values=\"0001000\"/></Scale></Scales>");
    const auto edits = autoTuneProNoteEdits (state, 9, (int) vocal::Scale::minor);
    CHECK (edits && edits->first == (1u << 9) && edits->second == (1u << 2));
    const auto at = subst113::autoTune (state);
    auto slot = subst113::pluginSlot (at);
    CHECK (substituteEffectSlot (slot, pluginIdentity (at)));
    CHECK (std::abs (lynxtune::naturalVibratoDb (slot.params[lynxtune::naturalVibrato]) - 3.0) < 1e-3);
    CHECK (slot.params[lynxtune::ignoreVibrato] == 1.0f);
    CHECK (lynxtune::vibratoShapeIndex (slot.params[lynxtune::vibratoShape]) == 2);
    CHECK (std::abs (lynxtune::vibratoRateHz (slot.params[lynxtune::vibratoRate]) - 4.0) < 1e-3);
    CHECK (std::abs (lynxtune::vibratoDepthCents (slot.params[lynxtune::vibratoDepth]) - 30.0) < 1e-3);
    CHECK (std::abs (lynxtune::vibratoOnsetMs (slot.params[lynxtune::vibratoDelay]) - 250.0) < 1e-2);
    CHECK (std::abs (lynxtune::vibratoOnsetMs (slot.params[lynxtune::vibratoOnset]) - 750.0) < 1e-2);
    CHECK (slot.params[(std::size_t) (lynxtune::bypassC + 9)] == 1.0f && slot.params[(std::size_t) (lynxtune::noteC + 9)] == 1.0f);
    CHECK (slot.params[(std::size_t) (lynxtune::noteC + 2)] == 0.0f && slot.params[(std::size_t) (lynxtune::bypassC + 2)] == 0.0f);
    CHECK (slot.params[(std::size_t) (lynxtune::noteC + 4)] == 1.0f);
}
