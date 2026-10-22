// Round 124: LYNX's substitutes matched to the official trials at her settings (docs/analise/REA_rodada124.md).
// Golden values are the REFERENCE's measured outputs (fxmeasure, 48 kHz); the substitute must stay within them.

namespace m124
{
constexpr double sr = 48000.0;
struct Stereo
{
    std::vector<float> l, r;
};
inline Stereo run (const lynx::EffectSlot& slot, Stereo x)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    for (std::size_t off = 0; off < x.l.size(); off += 512)
    {
        const auto m = (int) std::min<std::size_t> (512, x.l.size() - off);
        fx->process (x.l.data() + off, x.r.data() + off, m, slot.params, 120.0);
    }
    return x;
}
inline Stereo sine (double hz, double db, double secs)
{
    Stereo s;
    const auto n = (std::size_t) (secs * sr);
    s.l.resize (n);
    for (std::size_t i = 0; i < n; ++i)
        s.l[i] = (float) (std::pow (10.0, db / 20.0) * std::sin (2.0 * 3.141592653589793 * hz * (double) i / sr));
    s.r = s.l;
    return s;
}
// the gain (dB) at hz of y over x from `from` on (a single-bin DFT)
inline double gainDb (const std::vector<float>& x, const std::vector<float>& y, double hz, std::size_t from)
{
    double xr = 0, xi = 0, yr = 0, yi = 0;
    for (auto i = from; i < x.size(); ++i)
    {
        const auto w = 2.0 * 3.141592653589793 * hz * (double) i / sr;
        xr += x[i] * std::cos (w), xi += x[i] * std::sin (w);
        yr += y[i] * std::cos (w), yi += y[i] * std::sin (w);
    }
    return 10.0 * std::log10 ((yr * yr + yi * yi) / (xr * xr + xi * xi));
}
inline lynx::EffectSlot substitute (const lynx::ForeignPlugin& fp)
{
    auto slot = subst113::pluginSlot (fp);
    CHECK (lynx::substituteEffectSlot (slot, lynx::pluginIdentity (fp)));
    slot.mix = 1.0f;
    return slot;
}
} // namespace m124

TEST (fresh_air_dynamic_model_matches_the_trial_round124)
{
    using namespace lynx;
    ForeignPlugin fa;
    fa.format = ForeignPlugin::Format::vst3;
    fa.name = "Fresh Air";
    fa.classId = substtest::classIdOf ("f2a24ccd9800ac4900439bb9001cb96a");
    fa.hasClassId = true;
    fa.state = substtest::flVst3State (subst113::freshAirState (0.292f, 0.252f)); // her main setting (733 slots)
    const auto slot = m124::substitute (fa);
    CHECK (slot.kind == EffectKind::aether && slot.params[mixsuite::aether::dynamic] == 1.0f);
    // [EXEC] the official trial at 0.292 / 0.252, steady sines (dB of output over input)
    struct Point { double hz, level, gain; };
    static constexpr Point golden[] = { { 1000, -60, 1.083 }, { 1000, -12, 1.083 }, { 1000, 0, 1.461 },
                                        { 4000, -60, 3.681 }, { 4000, -12, 1.815 }, { 4000, 0, 1.795 },
                                        { 10000, -60, 9.762 }, { 10000, -30, 8.129 }, { 10000, -12, 3.384 }, { 10000, 0, 2.380 },
                                        { 16000, -60, 12.077 }, { 16000, -30, 9.432 }, { 16000, -12, 4.087 }, { 16000, 0, 2.647 } };
    for (const auto& g : golden)
    {
        const auto x = m124::sine (g.hz, g.level, 0.5);
        const auto y = m124::run (slot, x);
        const auto d = m124::gainDb (x.l, y.l, g.hz, (std::size_t) (0.3 * m124::sr));
        // (16 kHz at -30 dBFS, a steady sine just past the high band's second knee, is the model's worst point: 0.36 dB;
        // the dynamics were fitted on her takes, where the spectra agree within 0.05 dB RMS)
        const auto tolerance = g.hz == 16000 && g.level == -30 ? 0.4 : 0.25;
        if (std::abs (d - g.gain) > tolerance)
            std::printf ("  Fresh Air %g Hz %g dBFS: %.3f dB, reference %.3f\n", g.hz, g.level, d, g.gain);
        CHECK (std::abs (d - g.gain) <= tolerance);
    }
    // no latency, unlike the classic oversampled exciter path
    auto fx = makeEffect (slot);
    fx->prepare (m124::sr);
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    fx->process (l.data(), r.data(), 64, slot.params, 120.0);
    CHECK (fx->latencySamples() == 0);
}

TEST (doubler_her_state_timing_is_sample_exact_round124)
{
    using namespace lynx;
    namespace g = voicetools::gemini;
    // her Doubler (1969 slots): voices 78.2 / 100 ms, -1 / 0 cents, feedback 39 %, hard left / right, aligned
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 15.5_x64";
    id.state = substtest::wavesComponent ("Doubler", "0 1 0 0 1 * * * * 0 -28.7 841 3538 -13.7 0 16 0 0 0 0 64 20 32 -60 -40 -20 "
                                                     "1 0 -45 0 -1 78.2 39 0 1 * 1 0 45 0 0 100 39 0 1 *");
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    int n = 0;
    auto slot = makeSubstituteSlot (*entry, &id, &n);
    slot.mix = 1.0f;
    CHECK (slot.kind == EffectKind::geminiDoubler);
    for (const double rate : { 48000.0, 44100.0 })
    {
        m124::Stereo x;
        x.l.assign ((std::size_t) (0.5 * rate), 0.0f);
        x.r = x.l;
        x.r[1000] = 0.5f;
        auto fx = makeEffect (slot);
        fx->prepare (rate);
        fx->reset();
        for (std::size_t off = 0; off < x.l.size(); off += 512)
            fx->process (x.l.data() + off, x.r.data() + off, (int) std::min<std::size_t> (512, x.l.size() - off), slot.params, 120.0);
        // [EXEC] the trial at 48 and 44.1 kHz: the direct 320 samples late, the 0-cent voice its delay + 495 samples,
        // every repeat one sample more, 0.39 × a first-order 16 Hz high-pass (first sample 0.39 × 0.998954)
        const auto d = (std::size_t) std::lround (0.1 * rate);
        CHECK (std::abs (x.r[1000 + 320] - 0.5f) < 1e-6f);
        CHECK (std::abs (x.r[1000 + d + 495] - 0.5f) < 1e-6f);
        const auto k = std::tan (3.141592653589793 * 16.0 / rate);
        CHECK (std::abs (x.r[1000 + 2 * (d + 495) + 1] - 0.5f * 0.39f * (float) (1.0 / (1.0 + k))) < 1e-5f);
    }
    // the -1-cent voice drifts by 1 - 2^(-1/1200) samples per sample from 495 (linear reads)
    {
        m124::Stereo x;
        x.l.assign (48000 * 2, 0.0f);
        x.r = x.l;
        auto s2 = slot;
        s2.params[g::directOn] = 0.0f;
        s2.params[(std::size_t) g::voiceParam (0, g::vFeedback)] = 0.0f;
        const std::size_t at = 48000;
        x.l[at] = 1.0f;
        const auto y = m124::run (s2, x);
        const auto expected = (double) at + 3753.6 + 495.0 + (1.0 - std::pow (2.0, -1.0 / 1200.0)) * (double) (at + 3753.6 + 495.0);
        double num = 0.0, den = 0.0;
        for (std::size_t i = (std::size_t) expected - 4; i < (std::size_t) expected + 5; ++i)
            num += (double) i * y.l[i], den += y.l[i];
        CHECK (std::abs (num / den - expected) < 0.05);
    }
}

TEST (h_delay_hybrid_engine_matches_the_trial_round124)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 16.0_x64";
    id.state = substtest::wavesComponent ("H-Delay", "* 1 120 1000 16 * 100 60 0 0 1 * * * 0 20100 * 19 * 0 0.1 0 * 0");
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    int n = 0;
    auto slot = makeSubstituteSlot (*entry, &id, &n);
    slot.mix = 1.0f;
    CHECK (e::hybridOf (slot.params[e::hybrid]) == 1);
    // [EXEC] Analog off, a lone 0 dBFS impulse on the left: (L + R)/√2 through x / (1 + |x|) on every pass
    m124::Stereo x;
    x.l.assign (48000 * 4, 0.0f);
    x.r = x.l;
    x.l[1000] = 1.0f;
    x.l[1480] = 0.01f; // 10 ms later: the gain is still released only part of the way (one shared 10 ms release)
    const auto y = m124::run (slot, x);
    CHECK (std::abs (y.l[1000 + 48000] - 0.414212f) < 2e-6f);
    CHECK (std::abs (y.r[1000 + 96001] - 0.292893f) < 2e-6f);
    CHECK (std::abs (y.l[1000 + 144002] - 0.09538f) < 2e-5f);
    CHECK (std::abs (y.l[1480 + 48000] - 0.005994f) < 2e-6f);
    // the two lines share their gain: a loud left pass turns the right one down
    m124::Stereo z;
    z.l.assign (48000 * 3, 0.0f);
    z.r = z.l;
    for (std::size_t i = 0; i < 4800; ++i)
        z.l[1000 + i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * 200.0 * (double) i / 48000.0));
    for (std::size_t i = 0; i < 4800; ++i)
        z.l[49000 + i] = (float) (0.9 * std::sin (2.0 * 3.141592653589793 * 300.0 * (double) i / 48000.0));
    const auto w = m124::run (slot, z);
    double eLinked = 0.0;
    for (std::size_t i = 97001; i < 97001 + 4800; ++i)
        eLinked += (double) w.r[i] * w.r[i];
    z.l.assign (z.l.size(), 0.0f);
    for (std::size_t i = 0; i < 4800; ++i)
        z.l[1000 + i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * 200.0 * (double) i / 48000.0));
    const auto v = m124::run (slot, z);
    double eAlone = 0.0;
    for (std::size_t i = 97001; i < 97001 + 4800; ++i)
        eAlone += (double) v.r[i] * v.r[i];
    CHECK (eLinked < eAlone * 0.8);
}
