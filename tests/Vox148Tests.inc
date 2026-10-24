// Round 148 (vox148): LYNX Tune's held notes in noise — the PSOLA marks follow the waveform's repetition (period
// cross-correlation, sub-sample peak) instead of the single highest peak (docs/analise/REA_rodada148.md §1)

namespace vox148
{
// a held harmonic note (1/k, 12 harmonics) at `hz` plus white noise `snrDb` under it, mono, `sec` long
std::vector<float> noisyNote (double hz, double snrDb, double sec, double sr, std::uint32_t seed = 148)
{
    const int n = (int) (sec * sr);
    std::vector<float> x ((std::size_t) n);
    double s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (int k = 1; k <= 12; ++k)
            v += std::sin (2.0 * 3.141592653589793 * k * hz * i / sr) / k;
        x[(std::size_t) i] = (float) (0.15 * v);
        s2 += 0.0225 * v * v;
    }
    const auto noise = std::sqrt (s2 / n) * std::pow (10.0, -snrDb / 20.0) * std::sqrt (3.0); // uniform: rms = a / sqrt 3
    auto state = seed;
    for (auto& v : x)
    {
        state = state * 1664525u + 1013904223u;
        v += (float) (noise * ((double) (state >> 8) / 8388608.0 - 1.0));
    }
    return x;
}

// rms deviation (cents) of the fundamental's frequency over 40 ms windows (phase advance of the DFT bin at `hz`
// between windows 10 ms apart), from `from` to `to` seconds: how much a held note wobbles
double wobbleCents (const std::vector<float>& y, double hz, double sr, double from, double to)
{
    const int w = (int) (0.04 * sr), hop = (int) (0.01 * sr);
    auto bin = [&] (int s) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < w; ++i)
        {
            const auto win = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / w);
            const auto a = 2.0 * 3.141592653589793 * hz * (s + i) / sr;
            re += win * y[(std::size_t) (s + i)] * std::cos (a);
            im -= win * y[(std::size_t) (s + i)] * std::sin (a);
        }
        return std::atan2 (im, re);
    };
    std::vector<double> c;
    for (int s = (int) (from * sr); s + hop + w < (int) (to * sr); s += hop)
    {
        auto d = bin (s + hop) - bin (s);
        while (d > 3.141592653589793) d -= 2.0 * 3.141592653589793;
        while (d < -3.141592653589793) d += 2.0 * 3.141592653589793;
        const auto f = hz + d / (2.0 * 3.141592653589793 * hop / sr);
        c.push_back (1200.0 * std::log2 (f / hz));
    }
    double m = 0.0, v = 0.0;
    for (auto x : c) m += x;
    m /= (double) c.size();
    for (auto x : c) v += (x - m) * (x - m);
    return std::sqrt (v / (double) c.size());
}
} // namespace vox148

TEST (lynx_tune_held_note_in_noise_round148)
{
    using namespace lynx;
    // [EXEC] Auto-Tune Pro (official trial, retune 0 and 50, chromatic): held notes with white noise 20 dB under them
    // come out within 0.1–0.6 c rms; LYNX Tune wobbled 2–5 c on the low notes (the marks jumped between noise peaks).
    // Now the marks follow the waveform's repetition: a 110 Hz and a 220 Hz note in noise stay within about a cent.
    for (const double hz : { 110.0, 220.0 })
        for (const float retune : { 0.0f, 0.5f })
        {
            auto slot = makeEffectSlot (EffectKind::lynxTune);
            slot.params[lynxtune::scale] = 0.0f; // chromatic (the notes are in tune)
            slot.params[lynxtune::retune] = retune;
            auto fx = makeEffect (slot);
            const double sr = 48000.0;
            fx->prepare (sr);
            auto l = vox148::noisyNote (hz, 20.0, 2.0, sr), r = l;
            const int n = (int) l.size();
            for (int i = 0; i < n; i += 256)
                fx->process (&l[(std::size_t) i], &r[(std::size_t) i], std::min (256, n - i), slot.params, 120.0);
            const auto dry = vox148::wobbleCents (vox148::noisyNote (hz, 20.0, 2.0, sr), hz, sr, 0.5, 1.9);
            const auto wet = vox148::wobbleCents (l, hz, sr, 0.5, 1.9);
            std::printf ("  lynx tune %.0f Hz retune %.0f ms, noise 20 dB under: wobble %.2f c (dry %.2f c)\n", hz,
                         lynxtune::retuneMs (retune), wet, dry);
            CHECK (wet < 1.2);
        }
}
