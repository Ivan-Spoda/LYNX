// --- Round 147a: Echoform ← EchoBoy's styles / saturation, PrimalTap's Multiply (REA_rodada147a) ------------------
// The reference numbers below were measured black-box on the official EchoBoy / PrimalTap trials (D:\lynx-work\parity147\a).

namespace p147a
{
// a steady sine through Echoform at 48 kHz (both inputs), the wet left after `skipSeconds`
inline std::vector<float> echoSine (lynx::EffectParams p, double hz, double peakDb, double seconds = 2.0, double skipSeconds = 1.0)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::echoform);
    fx->prepare (48000.0);
    const int n = (int) (48000.0 * seconds);
    std::vector<float> l ((std::size_t) n), r;
    const auto a = std::pow (10.0, peakDb / 20.0);
    for (int i = 0; i < n; ++i)
        l[(std::size_t) i] = (float) (a * std::sin (2.0 * 3.14159265358979 * hz * i / 48000.0));
    r = l;
    for (int at = 0; at < n; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, n - at), p, 120.0);
    return std::vector<float> (l.begin() + (std::ptrdiff_t) (48000.0 * skipSeconds), l.end());
}

// the amplitude of `hz` in x (a single-bin DFT over whole cycles)
inline double toneAmp (const std::vector<float>& x, double hz)
{
    const int cyc = (int) std::floor (x.size() * hz / 48000.0);
    const int n = (int) std::lround (cyc * 48000.0 / hz);
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto ph = 2.0 * 3.14159265358979 * hz * i / 48000.0;
        re += x[(std::size_t) i] * std::cos (ph);
        im += x[(std::size_t) i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / n;
}

inline lynx::EffectParams voiced (int voicing, double driveDb, double ms = 250.0)
{
    namespace e = lynx::echosuite::echoform;
    auto p = p135w::cleanEcho (ms, 0.0f);
    p[e::voicing] = e::voicingValue (voicing);
    p[e::drive] = (float) (driveDb / 24.0);
    return p;
}
} // namespace p147a

TEST (echoform_voicing_params_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    CHECK ((int) effectInfo (EffectKind::echoform).params.size() == e::paramCount);
    CHECK (e::voicing == e::loopTreble + 1 && e::multiply == e::voicingStereo - 1); // appended after round 144's
    CHECK (e::paramCount <= maxEffectParams);
    const auto p = makeEffectSlot (EffectKind::echoform).params;
    CHECK (e::voicingOf (p[e::voicing]) == 0 && e::satTypeOf (p[e::satType]) == 0 && e::multiplyOf (p[e::multiply]) == 1);
    CHECK (formatEffectParam (EffectKind::echoform, e::voicing, p[e::voicing], 120.0) == "off");
    CHECK (formatEffectParam (EffectKind::echoform, e::voicing, e::voicingValue (2), 120.0) == "Studio reel");
    CHECK (formatEffectParam (EffectKind::echoform, e::satType, e::satTypeValue (5), 120.0) == "grit");
    CHECK (formatEffectParam (EffectKind::echoform, e::multiply, e::multiplyValue (4), 120.0) == "4x");
    for (int k = 0; k <= e::voicingCount; ++k)
        CHECK (e::voicingOf (e::voicingValue (k)) == k);
    CHECK (e::voicingForEchoBoyStyle (1) == 2 && e::voicingForEchoBoyStyle (31) == 0);
    // the names are LYNX's own (no product or trademark names)
    for (int k = 1; k <= e::voicingCount; ++k)
    {
        const std::string name = e::voicingName (k);
        CHECK (name.find ("Echo") == std::string::npos && name.find ("Memory") == std::string::npos && name.find ("Space") == std::string::npos);
    }
}

TEST (echoform_voicing_tones_match_the_trial_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] the first echo's tone (output × loop) at 125 Hz / 1 kHz / 4 kHz, 1/3-octave power ratios on the trial
    struct Ref { int style; double db[3]; };
    const Ref refs[] { { 1, { -3.27, -3.53, -4.71 } },   // studio tape
                       { 10, { -12.62, 2.76, -8.59 } },  // the telephone style
                       { 14, { -34.77, 7.31, 7.77 } },   // the transmitter style
                       { 15, { -2.91, -2.81, -2.81 } },  // the digital style
                       { 3, { -3.76, 0.18, -8.9 } } };   // the multi-head tape style
    const double hz[3] { 125.0, 1000.0, 4000.0 };
    for (const auto& r : refs)
        for (int i = 0; i < 3; ++i)
        {
            const int k = e::voicingForEchoBoyStyle (r.style);
            const auto db = e::voicingToneDb (k, false, hz[i], 48000.0) + e::voicingToneDb (k, true, hz[i], 48000.0);
            CHECK (std::abs (db - r.db[i]) < 2.0);
        }
}

TEST (echoform_voicing_saturation_curves_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] the steady 1 kHz echo's gain over −40 … 0 dBFS (output tone at 1 kHz removed): studio tape at Saturation
    // 24 dB (a shaper), the FM-radio style at 12 dB and the limited style at 18 dB (compressors)
    const double lv[6] { -40, -30, -20, -12, -6, 0 };
    struct Ref { int style; double drive; double db[6]; };
    const Ref refs[] { { 1, 24.0, { 2.27, 2.27, 1.81, 0.03, -2.79, -6.7 } },
                       { 12, 12.0, { -0.66, -0.86, -1.61, -3.66, -7.14, -11.9 } },
                       { 24, 18.0, { 8.74, 8.23, 6.16, 1.43, -3.95, -9.75 } } };
    for (const auto& r : refs)
    {
        const int k = e::voicingForEchoBoyStyle (r.style);
        const auto tone = e::voicingToneDb (k, false, 1000.0, 48000.0) + e::voicingToneDb (k, true, 1000.0, 48000.0);
        for (int i = 0; i < 6; ++i)
            CHECK (std::abs (tone + e::satGainDb (k, 0, r.drive, lv[i]) - r.db[i]) < 1.5);
    }
    // through the engine: studio reel at 24 dB, a 0 dBFS sine → about −6.7 dB at 1 kHz; at −30 dBFS about +2.3 dB
    const auto loud = p147a::echoSine (p147a::voiced (2, 24.0), 1000.0, 0.0);
    const auto soft = p147a::echoSine (p147a::voiced (2, 24.0), 1000.0, -30.0);
    CHECK (std::abs (20.0 * std::log10 (p147a::toneAmp (loud, 1000.0)) - -6.7) < 1.5);
    CHECK (std::abs (20.0 * std::log10 (p147a::toneAmp (soft, 1000.0) / std::pow (10.0, -1.5)) - 2.27) < 1.5);
}

TEST (echoform_voicing_wobble_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] the vibrato style's echo of a steady 1 kHz tone swings ≈ 16.7 cents rms at 5.5 Hz; the digital style is steady
    auto cents = [] (const std::vector<float>& x) {
        std::vector<double> zc; // rising zero crossings
        for (std::size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                zc.push_back ((double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]));
        std::vector<double> c;
        for (std::size_t i = 4; i < zc.size(); i += 4) // the period over 4 cycles
            c.push_back (1200.0 * std::log2 (48000.0 * 4.0 / (zc[i] - zc[i - 4]) / 1000.0));
        double m = 0.0, v = 0.0;
        for (auto d : c) m += d;
        m /= (double) c.size();
        for (auto d : c) v += (d - m) * (d - m);
        return std::sqrt (v / (double) c.size());
    };
    const auto vib = p147a::echoSine (p147a::voiced (e::voicingForEchoBoyStyle (20), 0.0), 1000.0, -20.0, 5.0, 1.0);
    const auto dig = p147a::echoSine (p147a::voiced (e::voicingForEchoBoyStyle (15), 0.0), 1000.0, -20.0, 3.0, 1.0);
    const auto c = cents (vib);
    CHECK (c > 11.0 && c < 23.0);
    CHECK (cents (dig) < 0.5);
}

TEST (echoform_multiply_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] PrimalTap: Multiply 2x / 4x / 8x = the delay × 2 / 4 / 8, the band ≈ 6.2 / 3.1 / 1.55 kHz
    for (int f : { 1, 2, 4, 8 })
    {
        auto p = p135w::cleanEcho (100.0, 0.0f);
        p[e::multiply] = e::multiplyValue (f);
        CHECK (e::multiplyOf (p[e::multiply]) == f);
        std::vector<float> l, r;
        p135w::echoImpulse (p, l, r);
        const int at = p135w::firstAbove (l, 0.01f, 10);
        CHECK (std::abs (at - 4800 * f) < 80);
    }
    auto p = p135w::cleanEcho (100.0, 0.0f);
    p[e::multiply] = e::multiplyValue (4);
    const auto low = p147a::toneAmp (p147a::echoSine (p, 1000.0, -20.0), 1000.0);
    const auto high = p147a::toneAmp (p147a::echoSine (p, 5000.0, -20.0), 5000.0);
    CHECK (20.0 * std::log10 (low / 0.1) > -3.0);
    CHECK (20.0 * std::log10 (high / 0.1) < -40.0);
}

TEST (echoform_saturation_type_without_voicing_round147a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // the Clean mode with a chosen curve: Grit (the fuzzed style's) adds harmonics, Auto keeps the mode's own shaper
    auto p = p135w::cleanEcho (250.0, 0.0f);
    p[e::drive] = 0.5f;
    const auto autoOut = p147a::echoSine (p, 1000.0, -6.0);
    p[e::satType] = e::satTypeValue (5);
    const auto grit = p147a::echoSine (p, 1000.0, -6.0);
    const auto h3 = [] (const std::vector<float>& x) { return p147a::toneAmp (x, 3000.0) / p147a::toneAmp (x, 1000.0); };
    const auto h2 = [] (const std::vector<float>& x) { return p147a::toneAmp (x, 2000.0) / p147a::toneAmp (x, 1000.0); };
    CHECK (h2 (grit) + h3 (grit) > 3.0 * (h2 (autoOut) + h3 (autoOut)) + 1e-4);
    // the EchoBoy stand-in for an unknown state plays the studio reel voicing (EchoBoy's default style)
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst2;
    id.vst2Id = 0x53744542u;
    id.name = "EchoBoy";
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry != nullptr)
    {
        const auto slot = makeSubstituteSlot (*entry);
        CHECK (e::voicingOf (slot.params[(std::size_t) e::voicing]) == 2);
    }
}
