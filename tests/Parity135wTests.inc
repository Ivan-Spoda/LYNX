// --- Round 135w: Waves roles + Echoform (H-Delay, ValhallaDelay, EchoBoy / PrimalTap) parity items -----------------

namespace p135w
{
// first sample (after `from`) where |x| exceeds `threshold`, −1 = none
inline int firstAbove (const std::vector<float>& x, float threshold, int from = 0)
{
    for (int i = from; i < (int) x.size(); ++i)
        if (std::abs (x[(std::size_t) i]) > threshold)
            return i;
    return -1;
}

// an impulse through Echoform at 48 kHz; returns the wet left / right
inline void echoImpulse (lynx::EffectParams p, std::vector<float>& l, std::vector<float>& r, int seconds = 2)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::echoform);
    fx->prepare (48000.0);
    l.assign ((std::size_t) (48000 * seconds), 0.0f);
    r = l;
    l[0] = r[0] = 0.5f;
    for (int at = 0; at < (int) l.size(); at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, (int) l.size() - at), p, 120.0);
}

inline lynx::EffectParams cleanEcho (double ms, float feedback01)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    auto p = makeEffectSlot (EffectKind::echoform).params;
    p[e::mix] = 1.0f;
    p[e::mode] = e::modeValue (e::Mode::clean);
    p[e::age] = 0.0f;
    p[e::modDepth] = 0.0f;
    p[e::style] = e::styleValue (e::Style::single);
    p[e::syncL] = p[e::syncR] = 0.0f;
    p[e::timeL] = p[e::timeR] = e::timeValue (ms);
    p[e::feedback] = feedback01;
    return p;
}
} // namespace p135w

TEST (echoform_new_params_keep_old_defaults_round135w)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto& info = effectInfo (EffectKind::echoform);
    CHECK ((int) info.params.size() == e::paramCount);
    auto p = makeEffectSlot (EffectKind::echoform).params;
    CHECK (formatEffectParam (EffectKind::echoform, e::tempoSource, p[e::tempoSource], 120.0) == "project");
    CHECK (formatEffectParam (EffectKind::echoform, e::ownBpm, p[e::ownBpm], 120.0) == "120.0 BPM");
    CHECK (formatEffectParam (EffectKind::echoform, e::groove, p[e::groove], 120.0) == "+0.0%");
    CHECK (p[e::tapA] == 1.0f && p[e::tapD] == 1.0f && p[e::freeze] == 0.0f && p[e::startSide] == 0.0f);
    // own BPM: a quarter note at 150 BPM whatever the project's tempo
    p[e::syncL] = 0.5f; // note
    p[e::noteL] = 0.45f; // index 4 = 1/4
    CHECK (std::abs (e::sideMs (p, false, 120.0) - 500.0) < 1e-6);
    p[e::tempoSource] = 1.0f;
    p[e::ownBpm] = e::bpmValue (150.0);
    CHECK (std::abs (e::sideMs (p, false, 120.0) - 400.0) < 1e-3);
    CHECK (e::nearestPrime (100) == 101 && e::nearestPrime (97) == 97 && e::nearestPrime (1) == 2);
}

TEST (echoform_start_side_quad_taps_and_freeze_round135w)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    std::vector<float> l, r;
    // ping-pong: the first echo on the left by default, on the right with Start side = right
    auto p = p135w::cleanEcho (250.0, 0.25f);
    p[e::style] = e::styleValue (e::Style::pingPong);
    p135w::echoImpulse (p, l, r);
    const auto firstL = p135w::firstAbove (l, 0.05f, 100), firstR = p135w::firstAbove (r, 0.05f, 100);
    CHECK (firstL > 0 && firstR > firstL);
    p[e::startSide] = 1.0f;
    p135w::echoImpulse (p, l, r);
    CHECK (p135w::firstAbove (r, 0.05f, 100) > 0 && p135w::firstAbove (l, 0.05f, 100) > p135w::firstAbove (r, 0.05f, 100));
    // quad with every tap muted: nothing is heard
    p = p135w::cleanEcho (400.0, 0.25f);
    p[e::style] = e::styleValue (e::Style::quad);
    for (const auto t : { e::tapA, e::tapB, e::tapC, e::tapD })
        p[(std::size_t) t] = 0.0f;
    p135w::echoImpulse (p, l, r);
    CHECK (p135w::firstAbove (l, 1e-4f, 10) < 0);
    // only tap A heard: the first echo at a quarter of the time
    p[e::tapA] = 1.0f;
    p135w::echoImpulse (p, l, r);
    const auto a = p135w::firstAbove (l, 0.05f, 10);
    CHECK (std::abs (a - 4800) < 40);
    // freeze: the loop keeps its level (feedback would otherwise let it fall)
    p = p135w::cleanEcho (100.0, 0.25f); // loop gain 0.5
    auto fx = makeEffect (EffectKind::echoform);
    fx->prepare (48000.0);
    std::vector<float> bl (48000, 0.0f), br (48000, 0.0f);
    for (int i = 0; i < 4800; ++i)
        bl[(std::size_t) i] = br[(std::size_t) i] = 0.3f * (float) std::sin (6.2831853 * 440.0 * i / 48000.0);
    fx->process (bl.data(), br.data(), 4800, p, 120.0);
    p[e::freeze] = 1.0f;
    float early = 0.0f, late = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        std::fill (bl.begin(), bl.begin() + 4800, 0.0f);
        std::fill (br.begin(), br.begin() + 4800, 0.0f);
        fx->process (bl.data(), br.data(), 4800, p, 120.0);
        float peak = 0.0f;
        for (int i = 0; i < 4800; ++i) peak = std::max (peak, std::abs (bl[(std::size_t) i]));
        if (block == 2) early = peak;
        if (block == 39) late = peak;
    }
    CHECK (early > 0.05f && std::abs (late - early) < 0.02f * early);
    // the output meter (controlOutput 3) follows the output
    CHECK (fx->controlOutput (3) > 0.05f);
}

TEST (echoform_groove_feel_accent_round135w)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    std::vector<float> l, r;
    // EchoBoy's laws measured on the trial (REA_rodada135w [EXEC]): Groove +25 % = odd repeats a quarter of the echo
    // time late, even ones on the grid; Feel +25 % = every repeat a quarter late
    auto p = p135w::cleanEcho (250.0, 0.25f); // loop gain 0.5
    p[e::groove] = 1.0f;
    p135w::echoImpulse (p, l, r);
    const auto first = p135w::firstAbove (l, 0.05f, 10);
    const auto second = p135w::firstAbove (l, 0.05f, first + 1000);
    CHECK (std::abs (first - 15000) < 60);   // 312.5 ms
    CHECK (std::abs (second - 24000) < 60);  // 500 ms
    p[e::groove] = 0.5f;
    p[e::feel] = 0.0f; // −25 %
    p135w::echoImpulse (p, l, r);
    const auto f1 = p135w::firstAbove (l, 0.05f, 10);
    const auto f2 = p135w::firstAbove (l, 0.05f, f1 + 1000);
    CHECK (std::abs (f1 - 9000) < 60 && std::abs (f2 - 21000) < 60); // 187.5 and 437.5 ms
    // accent +100 %: the first repeat twice the level, the second silent
    p[e::feel] = 0.5f;
    p[e::accent] = 1.0f;
    p135w::echoImpulse (p, l, r);
    float a1 = 0.0f, a2 = 0.0f;
    for (int i = 11000; i < 13000; ++i) a1 = std::max (a1, std::abs (l[(std::size_t) i]));
    for (int i = 23000; i < 25000; ++i) a2 = std::max (a2, std::abs (l[(std::size_t) i]));
    CHECK (a1 > 0.5f && a2 < 0.01f);
}

TEST (echoform_filter_link_and_hdelay_translation_round135w)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    auto p = makeEffectSlot (EffectKind::echoform).params;
    p[e::lowCut] = (100.0f - 10.0f) / 1990.0f;
    p[e::highCut] = (10000.0f - 200.0f) / 19800.0f;
    CHECK (e::linkedEdit (p, e::lowCut, 0.5f).size() == 1); // link off
    p[e::filterLink] = 1.0f;
    const auto edits = e::linkedEdit (p, e::lowCut, (200.0f - 10.0f) / 1990.0f); // 100 → 200 Hz
    CHECK (edits.size() == 2 && edits[1].first == e::highCut && std::abs (e::highCutHz (edits[1].second) - 20000.0) < 1.0);
    // H-Delay: its own BPM is kept (1/8 dotted at 125 BPM), the ping-pong start side (RealWorld 45) is read
    PluginIdentity id;
    id.format = ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 16.0_x64";
    std::string values = "* 0 125 360 11 * 41 40 0 0 1 * * * 0 20100 * 19 * 0 0.1 0 * 0";
    for (int i = 24; i < 45; ++i) values += " *";
    values += " 1";
    id.state = substtest::wavesComponent ("H-Delay", values);
    const auto* entry = findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    if (entry == nullptr)
        return;
    int n = 0;
    const auto s = makeSubstituteSlot (*entry, &id, &n);
    CHECK (n > 0 && s.params[e::tempoSource] == 1.0f && std::abs (e::bpmOf (s.params[e::ownBpm]) - 125.0) < 0.01);
    CHECK (std::abs (e::sideMs (s.params, false, 90.0) - 360.0) < 0.5);
    CHECK (s.params[e::startSide] == 1.0f);
}

TEST (gemini_four_voices_and_reset_round135w)
{
    using namespace lynx;
    namespace g = voicetools::gemini;
    const auto& info = effectInfo (EffectKind::geminiDoubler);
    CHECK ((int) info.params.size() == g::paramCount && g::voiceParam (2, g::vOn) == g::voice3);
    auto p = makeEffectSlot (EffectKind::geminiDoubler).params;
    CHECK (p[(std::size_t) g::voiceParam (2, g::vOn)] == 0.0f && p[(std::size_t) g::voiceParam (3, g::vOn)] == 0.0f);
    CHECK (formatEffectParam (EffectKind::geminiDoubler, g::modReset, 0.0f, 120.0) == "reset");
    // voice 3 alone (direct and voices 1, 2 off): something comes out on its side (left, the odd voices)
    p[g::directOn] = 0.0f;
    p[(std::size_t) g::voiceParam (0, g::vOn)] = p[(std::size_t) g::voiceParam (1, g::vOn)] = 0.0f;
    p[(std::size_t) g::voiceParam (2, g::vOn)] = 1.0f;
    auto fx = makeEffect (EffectKind::geminiDoubler);
    fx->prepare (48000.0);
    std::vector<float> l (9600), r (9600, 0.0f);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = 0.3f * (float) std::sin (6.2831853 * 220.0 * (double) i / 48000.0);
    fx->process (l.data(), r.data(), (int) l.size(), p, 120.0);
    float peak = 0.0f;
    for (std::size_t i = 4800; i < l.size(); ++i) peak = std::max (peak, std::abs (l[i]));
    CHECK (peak > 0.05f);
    // the four-voice doubler's state: voices 3 / 4 from blocks 46 / 56; a two-voice state leaves them off
    std::string values = "0 1 0 0 1 * * * * 0 -28.7 841 3538 -13.7 0 16 0 0 0 0 64 20 32 -60 -40 -20 "
                         "1 0 -45 0 -1 78.2 39 0 1 * 1 0 45 0 0 100 39 0 1 * 1 -6 -20 0 5 30 0 0 1 * 1 -6 20 0 -5 40 0 0 1 *";
    for (const auto* name : { "Doubler4", "Doubler2" })
    {
        PluginIdentity id;
        id.format = ForeignPlugin::Format::vst3;
        id.name = "WaveShell1-VST3 15.5_x64";
        id.state = substtest::wavesComponent (name, values);
        const auto* entry = findPluginSubstitute (id, false);
        CHECK (entry != nullptr);
        if (entry == nullptr)
            continue;
        int n = 0;
        const auto s = makeSubstituteSlot (*entry, &id, &n);
        const auto v3 = s.params[(std::size_t) g::voiceParam (2, g::vOn)];
        CHECK (std::string (name) == "Doubler4" ? v3 == 1.0f && std::abs (g::delayMs (s.params[(std::size_t) g::voiceParam (2, g::vDelay)]) - 30.0) < 0.01 : v3 == 0.0f);
    }
}

TEST (lynx_tune_real_time_controls_round135w)
{
    using namespace lynx;
    namespace t = lynxtune;
    const auto& info = effectInfo (EffectKind::lynxTune);
    CHECK ((int) info.params.size() == t::count);
    const auto p = makeEffectSlot (EffectKind::lynxTune).params;
    CHECK (formatEffectParam (EffectKind::lynxTune, t::speed, p[t::speed], 120.0) == "15 ms");
    CHECK (formatEffectParam (EffectKind::lynxTune, t::toleranceCents, p[t::toleranceCents], 120.0) == "Off");
    CHECK (formatEffectParam (EffectKind::lynxTune, t::vibratoAmount, p[t::vibratoAmount], 120.0) == "100%");
    CHECK (p[t::speedLink] == 1.0f && p[t::keepVibrato] == 1.0f && p[t::correction] == 1.0f);
    // the trial's Speed taper (round 120): 0.25 → 25.05 ms, 0.75 → 290 ms
    CHECK (std::abs (t::speedMs (0.25f) - 25.05) < 0.01 && std::abs (t::speedMs (0.75f) - 290.0) < 0.01 && std::abs (t::speedMs (1.0f) - 800.0) < 1e-9);
    CHECK (std::abs (t::speedMs (t::speedValue (120.0)) - 120.0) < 1e-3);
    // the input / output meters (outputs 3 / 4)
    auto fx = makeEffect (EffectKind::lynxTune);
    fx->prepare (44100.0);
    std::vector<float> l (2048, 0.25f), r (2048, 0.25f);
    fx->process (l.data(), r.data(), 2048, p, 120.0);
    CHECK (std::abs (fx->controlOutput (3) - 0.25f) < 1e-6f);
}

TEST (rcomp_switches_and_limiter_round135w)
{
    using namespace lynx;
    auto p = makeEffectSlot (EffectKind::compressor).params;
    CHECK (formatEffectParam (EffectKind::compressor, 9, p[9], 120.0) == "ARC" && formatEffectParam (EffectKind::compressor, 10, p[10], 120.0) == "electro"
           && formatEffectParam (EffectKind::compressor, 11, p[11], 120.0) == "warm" && formatEffectParam (EffectKind::compressor, 12, p[12], 120.0) == "off");
    auto run = [] (const EffectParams& q, float level) {
        auto fx = makeEffect (EffectKind::compressor);
        fx->prepare (48000.0);
        std::vector<float> l (9600), r (9600);
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = level * (float) std::sin (6.2831853 * 100.0 * (double) i / 48000.0);
        fx->process (l.data(), r.data(), (int) l.size(), q, 120.0);
        float peak = 0.0f;
        for (std::size_t i = 4800; i < l.size(); ++i) peak = std::max (peak, std::abs (l[i]));
        return std::make_pair (peak, fx->controlOutput (4));
    };
    // +12 dB of make-up on a loud sine: the limiter keeps it under 0 dBFS and its pill lights
    p[0] = 1.0f; // threshold 0 dB
    p[4] = 0.5f; // +12 dB
    const auto open = run (p, 0.8f);
    p[12] = 1.0f;
    const auto limited = run (p, 0.8f);
    CHECK (open.first > 1.5f && limited.first <= 1.0001f && limited.second > 0.0f);
    // vintage voicing: Smooth reduces less than Warm (ratio as shown), Manual differs from ARC — the round-150 curve (round
    // 152 measured Smooth = Warm statically: Parity152aTests)
    p[12] = 0.0f;
    p[8] = 1.0f;
    p[18] = 0.0f;
    p[0] = 0.5f; // −30 dB
    p[4] = 0.0f;
    const auto warm = run (p, 0.5f).first;
    p[11] = 1.0f;
    const auto smooth = run (p, 0.5f).first;
    CHECK (smooth > warm);
}

TEST (layouts_round135w)
{
    using namespace lynx;
    // LYNX Tune's "Real-time" page holds the real-time corrector's controls; Gemini's voices 3–4 sit on page 1
    const auto& tune = plugui::layoutFor (EffectKind::lynxTune);
    CHECK (tune.pages.size() == 3);
    int onReal = 0;
    for (const auto& item : tune.items)
        if (item.page == 2 && item.param != nullptr)
        {
            ++onReal;
            CHECK (plugui::paramIndex (EffectKind::lynxTune, item.param) >= lynxtune::speedLink);
        }
    CHECK (onReal == 9); // round 152b: + the reference tone and its level
    const auto& gemini = plugui::layoutFor (EffectKind::geminiDoubler);
    CHECK (gemini.pages.size() == 2);
    for (const auto& item : gemini.items)
        if (item.param != nullptr && std::string (item.param).rfind ("V3 ", 0) == 0)
            CHECK (item.page == 1);
    // every parameter named by the new layouts exists
    for (const auto kind : { EffectKind::echoform, EffectKind::compressor, EffectKind::lynxTune, EffectKind::geminiDoubler })
        for (const auto& item : plugui::layoutFor (kind).items)
            if (item.param != nullptr && plugui::isValueControl (item.type))
                CHECK (plugui::paramIndex (kind, item.param) >= 0);
}
