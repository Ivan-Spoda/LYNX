// Round 157: LYNX Tune on a singer's real takes (docs/analise/REA_rodada157c.md). Two detector faults made audible
// bursts on held notes: (1) the pitch tracker read 1.5x / 2x / 3x the fundamental where one harmonic sat on a strong
// formant, so the PSOLA grains shrank for a few frames; (2) 3..35 ms "unvoiced" dropouts in the middle of notes
// re-seeded the grains. Synthetic vowels from LynxTuneTests95.inc (r95::vowel, r95::measureHz, r95::runTune).

TEST (tune_harmonic_guard_holds_the_fundamental_round157)
{
    // A low sung vowel whose 3rd harmonic sits right on a narrow, strong first formant (f0 = 290 Hz, F1 = 870 Hz):
    // with the guard every voiced frame stays on the fundamental, never on a multiple.
    const double fs = 44100.0, hz = 290.0;
    const auto x = r95::vowel (fs, (int) (fs * 1.0), [hz] (double) { return hz; }, 870.0, 1700.0, 2900.0);
    int frames = 0, wrong = 0;
    lynx::vocal::PitchDetector det;
    det.prepare (fs, 70.0, 1000.0, 128);
    det.setHarmonicGuard (true);
    for (const auto v : x)
        if (det.push (v) && det.estimate().voiced)
        {
            ++frames;
            if (std::abs (r95::cents (det.estimate().hz, hz)) > 50.0)
                ++wrong;
        }
    CHECK (frames > 200);
    CHECK (wrong == 0);

    // and the guard keeps the round-95 accuracy on plain vowels (82 .. 700 Hz, three rates): within 1 cent
    double worst = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const double f : { 82.0, 110.0, 196.0, 261.63, 440.0, 700.0 })
        {
            const auto y = r95::vowel (rate, (int) (rate * 0.6), [f] (double) { return f; });
            lynx::vocal::PitchDetector d;
            d.prepare (rate, 60.0, 1000.0, (int) (rate * 0.003));
            d.setHarmonicGuard (true);
            double last = 0.0;
            for (const auto v : y)
                if (d.push (v) && d.estimate().voiced)
                    last = d.estimate().hz;
            worst = std::max (worst, std::abs (r95::cents (last, f)));
        }
    CHECK (worst < 1.0);
}

TEST (tune_short_voicing_dropout_is_bridged_round157)
{
    // A held, 40-cent-flat note with a 20 ms rough patch (breath noise mixed in) in the middle. The corrected note
    // stays on the target right after the patch: the grains keep the last period instead of re-seeding.
    const double fs = 44100.0, target = 220.0, sung = target * std::pow (2.0, -40.0 / 1200.0);
    auto x = r95::vowel (fs, (int) (fs * 1.2), [sung] (double) { return sung; });
    const int g0 = (int) (fs * 0.6), g1 = g0 + (int) (fs * 0.02);
    unsigned seed = 3;
    for (int i = g0; i < g1; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        x[(std::size_t) i] = 0.5f * x[(std::size_t) i] + 0.2f * (float) ((seed >> 8) / 16777216.0 - 0.5);
    }
    auto p = r95::tuneDefaults();
    int latency = 0;
    const auto y = r95::runTune (x, fs, p, latency);
    const auto after = g1 + latency + (int) (fs * 0.01);
    const auto hz = r95::measureHz (y, after, 2048, fs);
    CHECK (hz > 0.0);
    CHECK (std::abs (r95::cents (hz, target)) < 10.0);
    // no burst: the output level around the patch never jumps above the steady level by more than 3 dB
    auto rms = [&] (int from, int len) {
        double s = 0.0;
        for (int i = from; i < from + len; ++i)
            s += (double) y[(std::size_t) i] * y[(std::size_t) i];
        return std::sqrt (s / len);
    };
    const auto steady = rms (g0 + latency - (int) (fs * 0.2), (int) (fs * 0.1));
    double peakWin = 0.0;
    for (int at = g0 + latency - 512; at < g1 + latency + 2048; at += 128)
        peakWin = std::max (peakWin, rms (at, 256));
    CHECK (peakWin < steady * 1.41);
}
