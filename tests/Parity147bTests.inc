// --- Round 147b: parity items (docs/PARITY.md, REA_rodada147b) ----------------------------------------------------

namespace p147b
{
// one second of a 1 kHz sine through Ember (one block size), the given left / right inputs
inline std::pair<std::vector<float>, std::vector<float>> emberRun (const lynx::EffectParams& p, float gl, float gr)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::ember);
    fx->prepare (48000.0);
    std::vector<float> l (24000), r (24000);
    for (int i = 0; i < 24000; ++i)
    {
        const auto s = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        l[(std::size_t) i] = gl * s;
        r[(std::size_t) i] = gr * s;
    }
    for (int at = 0; at < 24000; at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, 24000 - at), p, 120.0);
    return { l, r };
}
inline double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (std::size_t i = 4800; i < a.size(); ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}
inline double peakOf (const std::vector<float>& a)
{
    double m = 0.0;
    for (std::size_t i = 4800; i < a.size(); ++i) m = std::max (m, (double) std::abs (a[i]));
    return m;
}
} // namespace p147b

TEST (ember_saturn_channel_mode_drive_pan_band_pan_round147b)
{
    using namespace lynx;
    namespace em = mixsuite::ember;
    auto base = makeEffectSlot (EffectKind::ember).params;
    CHECK (effectInfo (EffectKind::ember).params.size() == (std::size_t) em::count);
    // defaults: left / right, every pan centred (the old sound)
    CHECK (! em::midSide (base));
    for (int b = 0; b < em::maxBands; ++b)
    {
        CHECK (base[(std::size_t) em::bandDrivePanParam (b)] == 0.5f && base[(std::size_t) em::bandPanParam (b)] == 0.5f);
    }
    CHECK (em::panScales (0.0) == std::make_pair (1.0f, 1.0f));
    CHECK (em::panScales (-0.5) == std::make_pair (1.0f, 0.5f));  // [EXEC] pan −0.5: the right −6.02 dB
    CHECK (em::panScales (1.0) == std::make_pair (0.0f, 1.0f));
    CHECK (formatEffectParam (EffectKind::ember, em::channelMode, 1.0f, 120.0) == "Mid/Side");
    CHECK (formatEffectParam (EffectKind::ember, em::band1Pan, 0.25f, 120.0) == "0.0 / -6.0 dB");
    CHECK (formatEffectParam (EffectKind::ember, em::band2DrivePan, 0.5f, 120.0) == "centre");

    base[em::drive] = 0.5f;
    base[em::autoGain] = 0.0f;
    // drive pan fully left [EXEC: the far side's drive × (1 − |pan|)]: left = the full drive, right = no drive
    {
        auto p = base;
        p[(std::size_t) em::bandDrivePanParam (0)] = 0.0f;
        const auto [l, r] = p147b::emberRun (p, 1.0f, 1.0f);
        const auto [fl, fr] = p147b::emberRun (base, 1.0f, 1.0f);
        auto q = base;
        q[em::drive] = 0.0f;
        const auto [zl, zr] = p147b::emberRun (q, 1.0f, 1.0f);
        CHECK (p147b::maxDiff (l, fl) < 1e-6);
        CHECK (p147b::maxDiff (r, zr) < 1e-6);
        CHECK (p147b::maxDiff (r, fr) > 1e-3);
    }
    // half left: the right channel's drive is half the dB
    {
        auto p = base;
        p[(std::size_t) em::bandDrivePanParam (0)] = 0.25f;
        auto q = base;
        q[em::drive] = 0.25f;
        const auto [l, r] = p147b::emberRun (p, 1.0f, 1.0f);
        const auto [hl, hr] = p147b::emberRun (q, 1.0f, 1.0f);
        CHECK (p147b::maxDiff (r, hr) < 1e-6);
    }
    // band pan fully right: the left channel is silent (mix 100 %)
    {
        auto p = base;
        p[(std::size_t) em::bandPanParam (0)] = 1.0f;
        const auto [l, r] = p147b::emberRun (p, 1.0f, 1.0f);
        CHECK (p147b::peakOf (l) < 1e-6 && p147b::peakOf (r) > 0.1);
    }
    // mid / side: a mid-only input gives the left / right result (S = 0), a side-only input is shaped on its own
    {
        auto p = base;
        p[em::channelMode] = 1.0f;
        const auto [ml, mr] = p147b::emberRun (p, 1.0f, 1.0f);
        const auto [ll, lr] = p147b::emberRun (base, 1.0f, 1.0f);
        CHECK (p147b::maxDiff (ml, ll) < 1e-5 && p147b::maxDiff (mr, lr) < 1e-5);
        // pan fully left in M/S = the mid only: a side-only input disappears
        p[(std::size_t) em::bandPanParam (0)] = 0.0f;
        const auto [sl, sr] = p147b::emberRun (p, 1.0f, -1.0f);
        CHECK (p147b::peakOf (sl) < 1e-6 && p147b::peakOf (sr) < 1e-6);
        // a left-only tone in M/S with the drive panned to the mid: the right gets distortion products
        p[(std::size_t) em::bandPanParam (0)] = 0.5f;
        p[(std::size_t) em::bandDrivePanParam (0)] = 0.0f;
        const auto [al, ar] = p147b::emberRun (p, 1.0f, 0.0f);
        CHECK (p147b::peakOf (ar) > 1e-3);
    }
    // multiband: band 2's pan right silences its part on the left
    {
        auto p = base;
        p[em::bands] = 1.0f / 3.0f;
        p[em::xover1] = em::crossoverValue (300.0);
        p[(std::size_t) em::bandPanParam (1)] = 1.0f;
        const auto [l, r] = p147b::emberRun (p, 1.0f, 1.0f); // 1 kHz sits in band 2
        CHECK (p147b::peakOf (l) < 0.2 * p147b::peakOf (r));
    }
}

TEST (ember_saturn2_channel_mode_and_pans_translated_round147b)
{
    using namespace lynx;
    PluginIdentity id;
    id.name = "FabFilter Saturn 2";
    // [EXEC] the trial's state: 695 = channel mode, band b drive pan at 7 + 17b + 5, band pan at + 12 (−1 … +1)
    id.state = dyn108::ffbs (951, { { 5, 100.0f }, { 6, 1.0f }, { 7 + 5, -0.5f }, { 7 + 10, 100.0f }, { 7 + 12, 0.5f }, { 7 + 13, 1.0f },
                                    { 695, 1.0f } });
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr) return;
    const auto slot = makeSubstituteSlot (*entry, &id);
    namespace e = mixsuite::ember;
    CHECK (e::midSide (slot.params));
    CHECK (std::abs (e::panOf (slot.params[(std::size_t) e::bandDrivePanParam (0)]) + 0.5) < 1e-4);
    CHECK (std::abs (e::panOf (slot.params[(std::size_t) e::bandPanParam (0)]) - 0.5) < 1e-4);
}

TEST (lynx_tune_note_status_directions_and_range_round147b)
{
    using namespace lynx;
    namespace t = lynxtune;
    CHECK ((int) effectInfo (EffectKind::lynxTune).params.size() == t::count);
    auto p = makeEffectSlot (EffectKind::lynxTune).params;
    // defaults: every direction "nearest", the range the whole keyboard (the old behaviour)
    for (int k = 0; k < 12; ++k) CHECK (t::noteDir (p[(std::size_t) (t::noteDirC + k)]) == t::NoteDir::nearest);
    CHECK (t::rangeNote (p[t::rangeLow]) == 0 && t::rangeNote (p[t::rangeHigh]) == 127);
    CHECK (formatEffectParam (EffectKind::lynxTune, t::rangeHigh, t::rangeNoteValue (60), 120.0) == "C4");
    CHECK (formatEffectParam (EffectKind::lynxTune, t::noteDirC + 3, 0.5f, 120.0) == "Up");
    CHECK (t::rangePresetNotes (6) == std::make_pair (0, 127));
    for (int k = 0; k + 1 < t::rangePresets; ++k)
        CHECK (t::rangePresetNotes (k).first < t::rangePresetNotes (k).second && t::rangePresetNotes (k).second - t::rangePresetNotes (k).first >= 24);

    // the target with an illegal note: C major without E (legal C D F G A B): 63.9 (≈ E4 − 0.1) goes to the nearest
    // legal note by default (64 is illegal: D# / F — 65 F is nearer than 62 D), up = F4, down = D4
    const std::uint16_t legal = (1u << 0) | (1u << 2) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11);
    std::array<t::NoteDir, 12> dirs {};
    CHECK (t::targetNote (64.2, legal, -1, 0.0, dirs.data()) == 65);
    dirs[4] = t::NoteDir::down;
    CHECK (t::targetNote (64.2, legal, -1, 0.0, dirs.data()) == 62);
    dirs[4] = t::NoteDir::up;
    CHECK (t::targetNote (63.8, legal, -1, 0.0, dirs.data()) == 65);
    CHECK (t::targetNote (60.1, legal, -1, 0.0, dirs.data()) == 60); // a legal note is untouched

    // the range: a 450 Hz tone (A4 + 39 c) is pulled to 440 Hz; with the range ending at G4 it passes as sung
    auto freqOut = [] (const EffectParams& q) {
        auto fx = makeEffect (EffectKind::lynxTune);
        fx->prepare (48000.0);
        const int len = 96000;
        std::vector<float> l (len), r (len);
        for (int i = 0; i < len; ++i) l[(std::size_t) i] = r[(std::size_t) i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 450.0 * i / 48000.0);
        for (int at = 0; at < len; at += 512)
            fx->process (l.data() + at, r.data() + at, std::min (512, len - at), q, 120.0);
        int crossings = 0, first = -1, last = -1;
        for (int i = 60000; i < len; ++i)
            if (l[(std::size_t) i - 1] < 0.0f && l[(std::size_t) i] >= 0.0f)
            {
                if (first < 0) first = i;
                last = i;
                ++crossings;
            }
        return crossings > 1 ? 48000.0 * (crossings - 1) / (double) (last - first) : 0.0;
    };
    const auto corrected = freqOut (p);
    p[t::rangeHigh] = t::rangeNoteValue (67);
    const auto outside = freqOut (p);
    CHECK (std::abs (corrected - 440.0) < 3.0);
    CHECK (std::abs (outside - 450.0) < 3.0);
}

TEST (ridge_auto_makeup_and_analog_round147b)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    CHECK ((int) effectInfo (EffectKind::ridgeStrip).params.size() == r::count);
    auto p = makeEffectSlot (EffectKind::ridgeStrip).params;
    CHECK (p[r::autoMakeup] == 0.0f && p[r::analog] == 0.0f); // the old sound
    // the console's make-up for the same threshold / ratio (round 123's measured law): none at 1:1, more with more
    // compression
    p[r::compRatio] = 0.0f;
    CHECK (r::autoMakeupDb (p) == 0.0);
    p[r::compRatio] = 0.4f;                                         // 1 + 19·0.16 = 4.04 : 1
    p[r::compThreshold] = 0.5f;                                     // −20 dBFS = the console's −2 dB
    const auto mk = r::autoMakeupDb (p);
    vmatch::echannel::Settings e;
    e.compRatio = r::ratio (0.4f);
    e.compThreshold = -2.0;
    CHECK (std::abs (mk - vmatch::echannel::compMakeupDb (e)) < 1e-9 && mk > 0.5);
    p[r::compThreshold] = 0.25f;                                    // −30 dBFS: deeper, more make-up
    CHECK (r::autoMakeupDb (p) > mk);
    p[r::compThreshold] = 0.5f;
    // processed: switching it on raises the output by exactly that much
    auto run = [] (const EffectParams& q, float level) {
        auto fx = makeEffect (EffectKind::ridgeStrip);
        fx->prepare (48000.0);
        std::vector<float> l (48000), rr (48000);
        for (int i = 0; i < 48000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = level * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        for (int at = 0; at < 48000; at += 512)
            fx->process (l.data() + at, rr.data() + at, std::min (512, 48000 - at), q, 120.0);
        return std::make_pair (l, rr);
    };
    const auto off = run (p, 0.3f).first;
    p[r::autoMakeup] = 1.0f;
    const auto onOut = run (p, 0.3f).first;
    CHECK (std::abs (20.0 * std::log10 (p147b::peakOf (onOut) / p147b::peakOf (off)) - mk) < 0.01);
    // ANALOG [EXEC]: white noise at −110.7 dBFS RMS per channel, uncorrelated; off = digital silence
    auto q = makeEffectSlot (EffectKind::ridgeStrip).params;
    const auto silent = run (q, 0.0f);
    CHECK (p147b::peakOf (silent.first) == 0.0);
    q[r::analog] = 1.0f;
    const auto noisy = run (q, 0.0f);
    double sl = 0.0, sr = 0.0, slr = 0.0;
    for (std::size_t i = 4800; i < noisy.first.size(); ++i)
    {
        sl += (double) noisy.first[i] * noisy.first[i];
        sr += (double) noisy.second[i] * noisy.second[i];
        slr += (double) noisy.first[i] * noisy.second[i];
    }
    const auto count = (double) (noisy.first.size() - 4800);
    CHECK (std::abs (10.0 * std::log10 (sl / count) + 110.73) < 0.3);
    CHECK (std::abs (10.0 * std::log10 (sr / count) + 110.73) < 0.3);
    CHECK (std::abs (slr / std::sqrt (sl * sr)) < 0.03);
    // the E voicing gets the same noise
    q[r::voicing] = 1.0f;
    CHECK (p147b::peakOf (run (q, 0.0f).first) > 1e-6);
}
