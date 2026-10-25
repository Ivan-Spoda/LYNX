// Round 157: LYNX Tune on a singer's real takes (docs/analise/REA_rodada157c.md). The pitch tracker read 1.5x / 2x /
// 3x the fundamental where one harmonic sat on a strong formant, so the PSOLA grains shrank for a few frames (rough
// bursts on held notes). Synthetic vowels from LynxTuneTests95.inc (r95::vowel, r95::cents).

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

TEST (flp_insert_input_imported_round157c)
{
    // FL's list on a two-input device (FL Studio 2026, REA_rodada157c): 0 = "In 1 - In 2", 1 = "In 1", 2 = "In 2"
    auto is = [] (const lynx::InputSource& s, int channel, bool stereo) { return s.channel == channel && s.stereo == stereo; };
    CHECK (is (lynx::flInputSource (0, 2), 0, true));
    CHECK (is (lynx::flInputSource (1, 2), 0, false));
    CHECK (is (lynx::flInputSource (2, 2), 1, false));
    // eight inputs: four pairs, then the channels alone (her projects' 4 = "In 1" alone on such a device)
    CHECK (is (lynx::flInputSource (3, 8), 6, true));
    CHECK (is (lynx::flInputSource (4, 8), 0, false));
    CHECK (is (lynx::flInputSource (11, 8), 7, false));
    // past this device's list (FL had more inputs): the first channel alone; none stays none
    CHECK (is (lynx::flInputSource (4, 2), 0, false));
    CHECK (lynx::flInputSource (-1, 2).channel == -1);
    // the .flp: 0x9A follows the insert's 0xEC (before 0x93); Master −1, insert 1 = 4, insert 2 = −1
    FlpBuilder b;
    auto insert = [&b] (std::int32_t input) {
        b.data (0xEC, std::vector<std::uint8_t> (12, 0));
        b.u8 (0x9A);
        b.u32 ((std::uint32_t) input);
        b.u8 (0x93);
        b.u32 ((std::uint32_t) -1);
    };
    insert (-1);
    insert (4);
    insert (-1);
    const auto p = flp::parseProject (b.bytes);
    CHECK (p.mixer.size() == 3 && p.mixer[0].input == -1 && p.mixer[1].input == 4 && p.mixer[2].input == -1);
    const auto song = lynx::importFlp (p);
    CHECK (song.mixer.size() > 2 && song.mixer[1].flInput == 4 && song.mixer[1].monitor == lynx::InputMonitor::on);
    CHECK (song.mixer.size() > 2 && song.mixer[1].inputChannel == -1); // resolved by the engine on the open device
    CHECK (song.mixer.size() > 2 && song.mixer[0].flInput == -1 && song.mixer[2].flInput == -1);
    CHECK (song.mixer.size() > 2 && song.mixer[2].monitor == lynx::InputMonitor::automatic);
}
