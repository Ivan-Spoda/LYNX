// --- Round 150a: Echoform's voicings in EchoBoy's stereo modes, the L/R offset, the measured wobble shapes -----------
// The reference numbers below were measured black-box on the official EchoBoy trial (D:\lynx-work\parity150\a,
// docs/analise/REA_rodada150a.md).

namespace p150a
{
// an impulse (left, right amplitudes) through Echoform at 48 kHz; returns the wet left / right
inline void impulse (lynx::EffectParams p, std::vector<float>& l, std::vector<float>& r, float al, float ar, double seconds = 1.5)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::echoform);
    fx->prepare (48000.0);
    l.assign ((std::size_t) (48000.0 * seconds), 0.0f);
    r = l;
    l[0] = al;
    r[0] = ar;
    for (int at = 0; at < (int) l.size(); at += 512)
        fx->process (l.data() + at, r.data() + at, std::min (512, (int) l.size() - at), p, 120.0);
}

// the echoes' peaks (ms, peak level) in x: the largest |x| in each 20 ms group above `threshold`
inline std::vector<std::pair<double, float>> peaks (const std::vector<float>& x, float threshold)
{
    std::vector<std::pair<double, float>> out;
    for (int i = 0; i < (int) x.size(); ++i)
        if (std::abs (x[(std::size_t) i]) > threshold)
        {
            int best = i;
            const int end = std::min ((int) x.size(), i + 960);
            for (int j = i; j < end; ++j)
                if (std::abs (x[(std::size_t) j]) > std::abs (x[(std::size_t) best]))
                    best = j;
            out.push_back ({ best / 48.0, std::abs (x[(std::size_t) best]) });
            i = end;
        }
    return out;
}

inline lynx::EffectParams voiced (int echoBoyStyle, lynx::echosuite::echoform::Style style, float feedback01)
{
    namespace e = lynx::echosuite::echoform;
    auto p = p135w::cleanEcho (250.0, feedback01);
    p[e::voicing] = e::voicingValue (e::voicingForEchoBoyStyle (echoBoyStyle));
    p[e::drive] = 0.0f;
    p[e::style] = e::styleValue (style);
    p[e::timeR] = e::timeValue (375.0);
    return p;
}
} // namespace p150a

TEST (echoform_voicing_stereo_params_round150a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    CHECK ((int) effectInfo (EffectKind::echoform).params.size() == e::paramCount);
    CHECK (e::voicingStereo == e::multiply + 1 && e::lrOffset == e::paramCount - 1); // appended after round 147's
    CHECK (e::paramCount <= maxEffectParams);
    auto slot = makeEffectSlot (EffectKind::echoform);
    CHECK (slot.params[e::voicingStereo] >= 0.5f);                          // new instances: the measured behaviour
    CHECK (std::abs (e::lrOffsetMs (slot.params[e::lrOffset]) - 8.0) < 1e-4); // EchoBoy's default offset
    CHECK (formatEffectParam (EffectKind::echoform, e::lrOffset, slot.params[e::lrOffset], 120.0) == "8.0 ms");
    CHECK (formatEffectParam (EffectKind::echoform, e::voicingStereo, 0.0f, 120.0) == "classic");
    // a project saved before round 150 (a shorter parameter list) keeps the round-147 voicings
    upgradeLoadedEffectParams (slot, (std::size_t) e::voicingStereo);
    CHECK (slot.params[e::voicingStereo] < 0.5f);
    auto fresh = makeEffectSlot (EffectKind::echoform);
    upgradeLoadedEffectParams (fresh, (std::size_t) e::paramCount);
    CHECK (fresh.params[e::voicingStereo] >= 0.5f);
    // the offset law: −4 / +6 ms left, +4 / −6 ms right at 8 ms [EXEC]
    CHECK (std::abs (e::lrOddMs (8.0, false) + 4.0) < 1e-9 && std::abs (e::lrEvenMs (8.0, false) - 6.0) < 1e-9);
    CHECK (std::abs (e::lrOddMs (8.0, true) - 4.0) < 1e-9 && std::abs (e::lrEvenMs (8.0, true) + 6.0) < 1e-9);
}

TEST (echoform_voicing_lr_offset_round150a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] EchoBoy, Single, 250 ms, the digital style, feedback 0.75: left 246.2 / 506.2 / 746.2 / 1006.2 ms, right
    // 254.2 / 494.2 / 754.2 / 994.2 ms (0.2 ms = the measured tone's peak), each repeat −2.6 dB
    std::vector<float> l, r;
    auto p = p150a::voiced (15, e::Style::single, 0.375f);
    p150a::impulse (p, l, r, 0.05f, 0.05f);
    const auto pl = p150a::peaks (l, 0.001f), pr = p150a::peaks (r, 0.001f);
    CHECK (pl.size() >= 4 && pr.size() >= 4);
    if (pl.size() >= 4 && pr.size() >= 4)
    {
        const double wantL[4] = { 246.0, 506.0, 746.0, 1006.0 }, wantR[4] = { 254.0, 494.0, 754.0, 994.0 };
        for (int k = 0; k < 4; ++k)
        {
            CHECK (std::abs (pl[(std::size_t) k].first - wantL[k]) < 0.6);
            CHECK (std::abs (pr[(std::size_t) k].first - wantR[k]) < 0.6);
        }
        // each pass the loop gain (0.75) and the voicing's flat loop: the second repeat ≈ 2.5 dB under the first
        const auto drop = 20.0 * std::log10 (pl[1].second / pl[0].second);
        CHECK (drop < -1.5 && drop > -3.5);
    }
    // a left-only input leaves the right side silent (no cross-feed) [EXEC]
    p150a::impulse (p, l, r, 0.05f, 0.0f);
    CHECK (p150a::peaks (r, 1e-6f).empty() && ! p150a::peaks (l, 0.001f).empty());
    // Dual: each side its own time with the same offsets (right 379.2 / 744.2 ms) [EXEC]
    p = p150a::voiced (15, e::Style::dual, 0.375f);
    p150a::impulse (p, l, r, 0.05f, 0.05f);
    const auto dr = p150a::peaks (r, 0.001f);
    CHECK (dr.size() >= 2);
    if (dr.size() >= 2)
        CHECK (std::abs (dr[0].first - 379.0) < 0.6 && std::abs (dr[1].first - 744.0) < 0.6);
    // classic (round 147): both sides at T
    p = p150a::voiced (15, e::Style::single, 0.375f);
    p[e::voicingStereo] = 0.0f;
    p150a::impulse (p, l, r, 0.05f, 0.05f);
    const auto cl = p150a::peaks (l, 0.001f), cr = p150a::peaks (r, 0.001f);
    CHECK (! cl.empty() && ! cr.empty() && std::abs (cl[0].first - 250.0) < 0.6 && std::abs (cr[0].first - 250.0) < 0.6);
}

TEST (echoform_voicing_ping_pong_round150a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] EchoBoy Ping-Pong (250 / 375 ms, feedback 0.5): left at 250 ms, right at 625 ms with exactly the left echo
    // (same level and tone), left again at 875 ms 6 dB down; the input is (L + R) / √2: +3 dB for L = R against Single,
    // −3 dB for a left-only input
    std::vector<float> l, r;
    auto p = p150a::voiced (15, e::Style::pingPong, 0.25f);
    p150a::impulse (p, l, r, 0.05f, 0.05f);
    const auto pl = p150a::peaks (l, 0.001f), pr = p150a::peaks (r, 0.001f);
    CHECK (pl.size() >= 2 && pr.size() >= 1);
    if (pl.size() >= 2 && pr.size() >= 1)
    {
        CHECK (std::abs (pl[0].first - 250.0) < 0.6 && std::abs (pr[0].first - 625.0) < 0.6 && std::abs (pl[1].first - 875.0) < 0.6);
        CHECK (std::abs (20.0 * std::log10 (pr[0].second / pl[0].second)) < 0.1);
        CHECK (std::abs (20.0 * std::log10 (pl[1].second / pr[0].second) + 6.02) < 0.3);
    }
    std::vector<float> sl, sr;
    p150a::impulse (p150a::voiced (15, e::Style::single, 0.0f), sl, sr, 0.05f, 0.05f);
    const auto ps = p150a::peaks (sl, 0.001f);
    CHECK (! ps.empty() && ! pl.empty());
    if (! ps.empty() && ! pl.empty())
        CHECK (std::abs (20.0 * std::log10 (pl[0].second / ps[0].second) - 3.01) < 0.2);
    p150a::impulse (p, l, r, 0.05f, 0.0f);
    const auto ol = p150a::peaks (l, 0.0005f);
    CHECK (! ol.empty() && ! ps.empty());
    if (! ol.empty() && ! ps.empty())
        CHECK (std::abs (20.0 * std::log10 (ol[0].second / ps[0].second) + 3.01) < 0.2);
    // the pair tone: flat for the digital voicing, a band-pass for the phone line's (measured, fitted)
    for (const double hz : { 100.0, 1000.0, 8000.0 })
        CHECK (std::abs (e::voicingPairToneDb (e::voicingForEchoBoyStyle (15), hz, 48000.0)) < 0.3);
    const auto phone = e::voicingForEchoBoyStyle (10);
    CHECK (e::voicingPairToneDb (phone, 100.0, 48000.0) < e::voicingPairToneDb (phone, 1200.0, 48000.0) - 6.0);
    CHECK (e::voicingPairToneDb (phone, 12000.0, 48000.0) < e::voicingPairToneDb (phone, 1200.0, 48000.0) - 6.0);
    // no voicing's pair tone rises more than 1 dB anywhere (the loop stays bounded)
    for (int k = 1; k <= e::voicingCount; ++k)
        for (double hz = 30.0; hz < 20000.0; hz *= 1.12)
            CHECK (e::voicingPairToneDb (k, hz, 48000.0) < 1.05);
}

TEST (echoform_voicing_wobble_shapes_round150a)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // [EXEC] the CE-1-like style's delay swings as a triangle (1.63 Hz, ±1.0 ms): the echo's pitch is nearly constant
    // (±11.3 cents) between the turns, so its peak deviation is close to its rms (a sine: √2 × the rms)
    auto pitch = [] (const std::vector<float>& x) {
        std::vector<double> zc;
        for (std::size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                zc.push_back ((double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]));
        std::vector<double> c;
        for (std::size_t i = 8; i < zc.size(); i += 8) // the period over 8 cycles
            c.push_back (1200.0 * std::log2 (48000.0 * 8.0 / (zc[i] - zc[i - 8]) / 1000.0));
        double m = 0.0, v = 0.0, mx = 0.0;
        for (auto d : c) m += d;
        m /= (double) c.size();
        for (auto d : c) { v += (d - m) * (d - m); mx = std::max (mx, std::abs (d - m)); }
        return std::make_pair (std::sqrt (v / (double) c.size()), mx);
    };
    auto pe = p147a::voiced (e::voicingForEchoBoyStyle (19), 0.0);
    const auto tri = pitch (p147a::echoSine (pe, 1000.0, -20.0, 5.0, 1.0));
    CHECK (tri.first > 9.0 && tri.first < 13.5);
    CHECK (tri.second < 1.25 * tri.first);
    const auto sine = pitch (p147a::echoSine (p147a::voiced (e::voicingForEchoBoyStyle (20), 0.0), 1000.0, -20.0, 5.0, 1.0));
    CHECK (sine.second > 1.3 * sine.first);
}
