// --- Round 137: parity items (docs/PARITY.md, REA_rodada137) -----------------------------------------------------

namespace p137
{
// the largest |x| within ±`radius` samples of `at`
inline float peakNear (const std::vector<float>& x, int at, int radius = 96)
{
    float m = 0.0f;
    for (int i = std::max (0, at - radius); i < std::min ((int) x.size(), at + radius); ++i)
        m = std::max (m, std::abs (x[(std::size_t) i]));
    return m;
}

inline lynx::EffectParams rhythmEcho (int repeats, int noteIdx, double decayDb, float feedback01)
{
    namespace e = lynx::echosuite::echoform;
    auto p = p135w::cleanEcho (500.0, feedback01);
    p[e::rhythm] = 1.0f;
    p[e::rhythmSync] = 0.5f; // note
    p[e::rhythmNote] = ((float) noteIdx + 0.5f) / 10.0f;
    p[e::rhythmRepeats] = (float) (repeats - 1) / 15.0f;
    p[e::rhythmDecay] = (float) (decayDb / 40.0);
    p[e::rhythmLength] = 1.0f / 7.0f; // 2 beats
    return p;
}
} // namespace p137

TEST (echoform_rhythm_defaults_and_texts_round137)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto& info = effectInfo (EffectKind::echoform);
    CHECK ((int) info.params.size() == e::paramCount);
    const auto p = makeEffectSlot (EffectKind::echoform).params;
    CHECK (p[e::rhythm] == 0.0f);
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmRepeats, p[e::rhythmRepeats], 120.0) == "8");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmLength, p[e::rhythmLength], 120.0) == "2 beats");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmNote, p[e::rhythmNote], 120.0) == "1/8");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmSync, p[e::rhythmSync], 120.0) == "note");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmDecay, 0.5f, 120.0) == "20.0 dB");
}

TEST (echoform_rhythm_pattern_law_round137)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // EchoBoy measured: 4 repeats of 1/8 at 120 BPM = taps at 250 / 500 / 750 ms, each 1/√4 (−6 dB)
    auto p = p137::rhythmEcho (4, 3, 0.0, 0.0f);
    double slot0 = 0.0;
    auto taps = e::rhythmTaps (p, 120.0, &slot0);
    CHECK (taps.size() == 3);
    CHECK (std::abs (taps[0].ms - 250.0) < 1e-6 && std::abs (taps[2].ms - 750.0) < 1e-6);
    CHECK (std::abs (taps[1].level - 0.5) < 1e-9 && std::abs (slot0 - 0.5) < 1e-9);
    // 16 repeats of 1/16 do not fit in 2 beats: 8 taps up to 1000 ms, 1/√9 each (measured −9.3 dB incl. the style's +0.2)
    taps = e::rhythmTaps (p137::rhythmEcho (16, 2, 0.0, 0.0f), 120.0);
    CHECK (taps.size() == 8 && std::abs (taps.back().ms - 1000.0) < 1e-6 && std::abs (taps[0].level - 1.0 / 3.0) < 1e-9);
    // decay: 20 dB over 8 slots = 2.86 dB a slot, equal-power normalised (measured −5.8 … −22.9 dB with +0.2)
    taps = e::rhythmTaps (p137::rhythmEcho (8, 2, 20.0, 0.0f), 120.0, &slot0);
    CHECK (taps.size() == 7);
    CHECK (std::abs (20.0 * std::log10 (taps[0].level) - -6.0) < 0.15);
    CHECK (std::abs (20.0 * std::log10 (taps[6].level) - -23.1) < 0.15);
    // the rhythm editor's bars: each heard tap's level, after the normalisation (the others stay as they are)
    p[e::rhythmTap1 + 1] = 0.5f;
    taps = e::rhythmTaps (p, 120.0);
    CHECK (std::abs (taps[1].level - 0.25) < 1e-9 && std::abs (taps[0].level - 0.5) < 1e-9);
    p[e::rhythmTap1 + 1] = 1.0f;
    // the length follows the tempo: 2 beats at 60 BPM = 2 s
    CHECK (std::abs (e::rhythmLengthMs (p, 60.0) - 2000.0) < 1e-6);
}

TEST (echoform_rhythm_impulse_round137)
{
    using namespace lynx;
    std::vector<float> l, r;
    // no feedback: three taps at −6 dB of the impulse (0.5), nothing after the pattern
    p135w::echoImpulse (p137::rhythmEcho (4, 3, 0.0, 0.0f), l, r, 2);
    for (const int at : { 12000, 24000, 36000 })
        CHECK (std::abs (p137::peakNear (l, at) - 0.25f) < 0.03f && std::abs (p137::peakNear (r, at) - 0.25f) < 0.03f);
    CHECK (p137::peakNear (l, 48000, 2000) < 0.01f);
    // feedback 50 %: the pattern comes back after 2 beats (slot 0 included), 6 dB down
    p135w::echoImpulse (p137::rhythmEcho (4, 3, 0.0, 0.25f), l, r, 3);
    for (const int at : { 48000, 60000, 72000, 84000 })
        CHECK (std::abs (p137::peakNear (l, at) - 0.125f) < 0.02f);
    CHECK (std::abs (p137::peakNear (l, 96000) - 0.0625f) < 0.015f);
    // off: the plain echo as before (500 ms single)
    auto p = p137::rhythmEcho (4, 3, 0.0, 0.0f);
    p[echosuite::echoform::rhythm] = 0.0f;
    p135w::echoImpulse (p, l, r, 2);
    CHECK (p137::peakNear (l, 12000) < 0.01f && p137::peakNear (l, 24000) > 0.2f);
}

TEST (echoform_era_round137)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto p0 = makeEffectSlot (EffectKind::echoform).params;
    CHECK (p0[e::era] == 0.0f && formatEffectParam (EffectKind::echoform, e::era, 0.0f, 120.0) == "past");
    CHECK (formatEffectParam (EffectKind::echoform, e::era, 0.5f, 120.0) == "present");
    CHECK (formatEffectParam (EffectKind::echoform, e::era, 1.0f, 120.0) == "future");
    CHECK (e::eraBandwidth (e::Mode::tape, 0) == 1.0 && e::eraBandwidth (e::Mode::clean, 2) == 1.0 && e::eraBandwidth (e::Mode::hifi, 2) == 1.0);
    // Future opens the digital echo's top end (measured on the demo: 12 kHz −45 dB in Past, −6 dB in Future)
    auto energyAbove = [] (const std::vector<float>& x, int from, int n) {
        // the high-passed energy (first difference) of one repeat
        double hi = 0.0;
        for (int i = from + 1; i < from + n; ++i)
            hi += (double) (x[(std::size_t) i] - x[(std::size_t) i - 1]) * (x[(std::size_t) i] - x[(std::size_t) i - 1]);
        return hi;
    };
    auto p = p135w::cleanEcho (300.0, 0.0f);
    p[e::mode] = e::modeValue (e::Mode::digital);
    std::vector<float> l, r;
    p135w::echoImpulse (p, l, r, 1);
    const auto past = energyAbove (l, 14000, 2000);
    p[e::era] = 1.0f;
    p135w::echoImpulse (p, l, r, 1);
    const auto future = energyAbove (l, 14000, 2000);
    CHECK (future > past * 1.3);
}

TEST (voiceshift_midi_control_round137)
{
    using namespace lynx;
    namespace vs = voiceshift;
    auto p = makeEffectSlot (EffectKind::voiceShift).params;
    CHECK (effectInfo (EffectKind::voiceShift).params.size() == (std::size_t) vs::count);
    CHECK (p[vs::midi] == 0.0f && formatEffectParam (EffectKind::voiceShift, vs::midi, 0.0f, 120.0) == "Off");
    CHECK (formatEffectParam (EffectKind::voiceShift, vs::midi, vs::midiChannelValue (2), 120.0) == "Channel 3");
    // E4 (64) held on channel 3
    std::array<NoteMask, 4> held {};
    held[2][1] = 1ull; // note 64 → word 1, bit 0
    auto runHeld = [&held] (const EffectParams& params, double f0) {
        auto l = p135s::voice (f0, 1.0), r = l;
        auto fx = makeEffect (EffectKind::voiceShift);
        fx->prepare (p135s::sr);
        fx->reset();
        for (std::size_t off = 0; off < l.size(); off += 256)
        {
            fx->setHeldNotes (held.data(), (int) held.size());
            const auto m = (int) std::min<std::size_t> (256, l.size() - off);
            fx->process (l.data() + off, r.data() + off, m, params, 120.0);
        }
        return p135s::peakHz (l, 36000, 9600, 150.0, 400.0);
    };
    p[vs::midi] = vs::midiChannelValue (2);
    p[vs::mode] = 1.0f; // Robot sings the held note: E4 = 329.6 Hz
    CHECK (std::abs (runHeld (p, 200.0) - 329.63) < 5.0);
    p[vs::mode] = 0.0f; // Transpose by 64 − 60 = +4 st: 200 → 252 Hz (the Pitch knob, +2 st, is replaced)
    p[vs::pitch] = vs::semitoneValue (2.0);
    CHECK (std::abs (runHeld (p, 200.0) - 200.0 * std::exp2 (4.0 / 12.0)) < 4.0);
    p[vs::midi] = 0.0f; // MIDI off: the Pitch knob again (+2 st: 224.5 Hz)
    CHECK (std::abs (runHeld (p, 200.0) - 200.0 * std::exp2 (2.0 / 12.0)) < 4.0);
}

TEST (lattice_input_pan_round137)
{
    using namespace lynx;
    namespace lt = echosuite::lattice;
    auto p = makeEffectSlot (EffectKind::latticeMod).params;
    CHECK (lt::inputPanOf (p[lt::inputPan]) == 1);
    CHECK (formatEffectParam (EffectKind::latticeMod, lt::inputPan, 0.25f, 120.0) == "swapped");
    p[lt::mix] = 1.0f;
    p[lt::depth] = 0.0f;
    auto energy = [&p] (float pan, bool rightOut) {
        auto q = p;
        q[lt::inputPan] = pan;
        auto fx = makeEffect (EffectKind::latticeMod);
        fx->prepare (48000.0);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[100] = 0.5f; // a left-only impulse
        for (int at = 0; at < 48000; at += 512)
            fx->process (l.data() + at, r.data() + at, std::min (512, 48000 - at), q, 120.0);
        double e = 0.0;
        for (const auto v : rightOut ? r : l)
            e += (double) v * v;
        return e;
    };
    // [EXEC] the reference: stereo keeps the sides, 2 swaps them, 3 sends (L + R) / 2 to both (−6 dB), 4 / 5 to one side
    CHECK (energy (0.125f, false) > 100.0 * energy (0.125f, true));
    CHECK (energy (0.25f, true) > 100.0 * energy (0.25f, false));
    CHECK (std::abs (10.0 * std::log10 (energy (0.375f, false) / energy (0.375f, true))) < 0.5);
    CHECK (std::abs (10.0 * std::log10 (energy (0.375f, false) / energy (0.125f, false)) - -6.0) < 0.7);
    CHECK (energy (0.625f, true) > 100.0 * energy (0.625f, false));
}

TEST (echoform_ducking_measured_round137)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] ValhallaDelay demo, a 1 kHz tone into the echo (300 ms, 70 % feedback): the echoes during the tone are
    // reduced by 41.5 dB (amplitude 0.5, ducking 100 %), 20.1 dB (0.25, 75 %), 7.1 dB (0.25, 50 %), none at 0.05 / 50 %
    auto run = [] (float amp, float duck) {
        auto p = p135w::cleanEcho (300.0, 0.35f);
        p[e::ducking] = duck;
        auto fx = makeEffect (EffectKind::echoform);
        fx->prepare (48000.0);
        std::vector<float> l (48000 * 2, 0.0f), r;
        for (int i = 0; i < 48000; ++i)
            l[(std::size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        r = l;
        const auto in = l;
        for (int at = 0; at < (int) l.size(); at += 512)
            fx->process (l.data() + at, r.data() + at, std::min (512, (int) l.size() - at), p, 120.0);
        double sum = 0.0;
        for (int i = 28800; i < 45600; ++i)
            sum += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return 10.0 * std::log10 (sum / 16800.0 + 1e-20);
    };
    CHECK (std::abs (run (0.5f, 1.0f) - run (0.5f, 0.0f) - -41.5) < 3.0);
    CHECK (std::abs (run (0.25f, 0.75f) - run (0.25f, 0.0f) - -20.1) < 3.0);
    CHECK (std::abs (run (0.25f, 0.5f) - run (0.25f, 0.0f) - -7.1) < 2.5);
    CHECK (std::abs (run (0.05f, 0.5f) - run (0.05f, 0.0f)) < 1.5);
}

TEST (crest_trigger_host_sync_and_midi_round137)
{
    using namespace lynx;
    namespace cr = dyn::crest;
    auto p = makeEffectSlot (EffectKind::crestCompressor).params;
    CHECK (cr::triggerOf (p[cr::trigger]) == cr::Trigger::off);
    CHECK (formatEffectParam (EffectKind::crestCompressor, cr::triggerSync, p[cr::triggerSync], 120.0) == "1/4");
    CHECK (formatEffectParam (EffectKind::crestCompressor, cr::triggerRate, 0.0f, 120.0) == "50%");
    CHECK (formatEffectParam (EffectKind::crestCompressor, cr::triggerRate, 1.0f, 120.0) == "200%");
    // the pulse: 1/4 at 100 % = every beat, 10 % of it; 1/4 at 50 % = every half beat
    CHECK (cr::hostTrigger (p, 0.05) == 1.0f && cr::hostTrigger (p, 0.2) == 0.0f && cr::hostTrigger (p, 1.02) == 1.0f);
    auto q = p;
    q[cr::triggerRate] = 0.0f;
    CHECK (cr::hostTrigger (q, 0.52) == 1.0f && cr::hostTrigger (q, 0.3) == 0.0f);
    p[cr::autoGain] = 0.0f;
    // a steady sine under the threshold: no reduction by itself; the trigger pulls it down at every beat
    auto run = [] (EffectParams params, bool playing, const NoteMask* held, int heldCount, std::vector<float>& out) {
        auto fx = makeEffect (EffectKind::crestCompressor);
        fx->prepare (48000.0);
        out.assign (48000, 0.0f);
        for (int i = 0; i < 48000; ++i)
            out[(std::size_t) i] = 0.05f * (float) std::sin (2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
        auto r = out;
        for (int at = 0; at < 48000; at += 480)
        {
            fx->setSongPosition ((double) at / 48000.0 * 2.0 * (double) ppq);
            fx->setPlaying (playing);
            fx->setHeldNotes (held, heldCount);
            fx->process (out.data() + at, r.data() + at, 480, params, 120.0);
        }
    };
    auto levelDb = [] (const std::vector<float>& x, int from, int to) {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += (double) x[(std::size_t) i] * x[(std::size_t) i];
        return 10.0 * std::log10 (sum / (to - from) + 1e-20) - 20.0 * std::log10 (0.05 / std::sqrt (2.0));
    };
    std::vector<float> y;
    run (p, true, nullptr, 0, y);
    CHECK (std::abs (levelDb (y, 24000 + 960, 24000 + 2400)) < 0.5); // off: untouched
    p[cr::trigger] = 0.5f; // host sync
    run (p, true, nullptr, 0, y);
    const auto during = levelDb (y, 24000 + 960, 24000 + 2400), between = levelDb (y, 24000 + 20000, 24000 + 23500);
    CHECK (during < -8.0);
    CHECK (between > during + 4.0);
    run (p, false, nullptr, 0, y); // stopped: no pulses
    CHECK (std::abs (levelDb (y, 24000 + 960, 24000 + 2400)) < 0.5);
    // MIDI: reduction while a note is held on the chosen channel, none without notes
    p[cr::trigger] = 1.0f;
    p[cr::triggerChannel] = 2.0f / 128.0f; // channel 2 (index 1)
    std::array<NoteMask, 3> held {};
    run (p, true, held.data(), 3, y);
    CHECK (std::abs (levelDb (y, 24000, 30000)) < 0.5);
    held[1][0] = 1ull << 60;
    run (p, true, held.data(), 3, y);
    CHECK (levelDb (y, 24000, 30000) < -8.0);
}

TEST (echoform_lofi_switch_round137)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    auto p = p135w::cleanEcho (300.0, 0.0f);
    CHECK (p[e::lofi] == 0.0f);
    std::vector<float> l, r;
    p135w::echoImpulse (p, l, r, 1);
    auto hfEnergy = [] (const std::vector<float>& x) {
        double hi = 0.0;
        for (int i = 14001; i < 16000; ++i)
            hi += (double) (x[(std::size_t) i] - x[(std::size_t) i - 1]) * (x[(std::size_t) i] - x[(std::size_t) i - 1]);
        return hi;
    };
    const auto open = hfEnergy (l);
    p[e::lofi] = 1.0f;
    p135w::echoImpulse (p, l, r, 1);
    CHECK (hfEnergy (l) < open * 0.5);
    CHECK (formatEffectParam (EffectKind::echoform, e::lofi, 1.0f, 120.0) == "on");
}
