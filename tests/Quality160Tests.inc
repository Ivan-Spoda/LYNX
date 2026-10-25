// --- Round 160: sound quality of the signal path (time-stretch, docs/analise/REA_rodada160.md) --------------------

namespace q160
{
lynx::SampleData sineOf (double freq, double seconds, double rate = 44100.0, bool stereo = false)
{
    lynx::SampleData s;
    s.sampleRate = rate;
    const auto n = (std::size_t) (seconds * rate);
    for (std::size_t i = 0; i < n; ++i)
        s.left.push_back ((float) (0.5 * std::sin (2.0 * 3.141592653589793 * freq * (double) i / rate)));
    if (stereo)
        s.right = s.left;
    return s;
}

// one 3-sample click every `gap` seconds, from 0.1 s
lynx::SampleData clicksOf (int count, double gap, double rate = 44100.0)
{
    lynx::SampleData s;
    s.sampleRate = rate;
    s.left.assign ((std::size_t) ((0.1 + gap * count + 0.3) * rate), 0.0f);
    for (int k = 0; k < count; ++k)
    {
        const auto i = (std::size_t) std::llround ((0.1 + k * gap) * rate);
        s.left[i] = 0.9f;
        s.left[i + 1] = -0.5f;
        s.left[i + 2] = 0.2f;
    }
    s.right = s.left;
    return s;
}

// signal-to-(noise + distortion) of a steady sine, dB (least-squares fit of the known frequency per 4096 frames)
double sinadDb (const lynx::SampleData& d, double freq)
{
    std::vector<double> ratios;
    const auto n = (long) d.frames();
    for (long at = (long) (0.2 * d.sampleRate); at + 4096 < n - (long) (0.2 * d.sampleRate); at += 4096)
    {
        double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0, xx = 0;
        for (long i = 0; i < 4096; ++i)
        {
            const auto w = 2.0 * 3.141592653589793 * freq * (double) i / d.sampleRate;
            const auto s = std::sin (w), c = std::cos (w), x = (double) d.leftData()[at + i];
            ss += s * s; cc += c * c; sc += s * c; xs += x * s; xc += x * c; xx += x * x;
        }
        const auto det = ss * cc - sc * sc;
        const auto a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
        const auto fit = a * xs + b * xc; // energy explained by the sine
        ratios.push_back (fit / std::max (1e-30, xx - fit));
    }
    std::sort (ratios.begin(), ratios.end());
    return ratios.empty() ? 0.0 : 10.0 * std::log10 (ratios[ratios.size() / 2]);
}
} // namespace q160

TEST (q160_stretch_near_unity_is_a_clean_resample)
{
    // FL's recorded takes carry a stretch time equal to their own length: ratio 0.99998 (LEMITGO). The old WSOLA
    // rebuilt them from overlapping windows anyway (null vs the ideal −1 dB); now they are resampled.
    const auto in = q160::sineOf (1000.0, 2.0, 44100.0, true);
    const auto ratio = 0.99998;
    const auto out = lynx::timeStretch (in, ratio, lynx::StretchMethod::general);
    const auto m = (long) std::llround ((double) in.frames() * ratio);
    CHECK ((long) out->frames() == m && out->isStereo());

    const auto step = (double) in.frames() / (double) m;
    double err = 0.0, ref = 0.0;
    for (long j = 2000; j < m - 2000; ++j)
    {
        const auto ideal = 0.5 * std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) j * step / 44100.0);
        err += (out->left[(std::size_t) j] - ideal) * (out->left[(std::size_t) j] - ideal);
        ref += ideal * ideal;
    }
    CHECK (10.0 * std::log10 (err / ref) < -80.0);

    // exactly 1 = a copy
    const auto same = lynx::timeStretch (in, 1.0);
    CHECK (same->frames() == in.frames() && same->left[12345] == in.left[12345]);
}

TEST (q160_stretch_keeps_clicks_sharp_and_on_time)
{
    // a click train stretched by 1.5: every click lands where the time map puts it, without pre-echo or smear
    // (old WSOLA: lost/moved clicks; plain spectral stretch: −27 dB pre-echo)
    const auto in = q160::clicksOf (10, 0.25);
    for (auto method : { lynx::StretchMethod::general, lynx::StretchMethod::transients })
        for (double ratio : { 0.8, 1.5, 2.0 })
        {
            const auto out = lynx::timeStretch (in, ratio, method);
            CHECK (std::abs ((double) out->frames() - (double) in.frames() * ratio) < 1.5);
            int onTime = 0;
            double worstPre = -200.0;
            for (int k = 0; k < 10; ++k)
            {
                const auto expect = (long) std::llround ((0.1 + k * 0.25) * 44100.0 * ratio);
                long at = expect;
                float peak = 0.0f;
                for (long i = expect - 400; i < expect + 400; ++i)
                    if (std::abs (out->left[(std::size_t) i]) > peak)
                    {
                        peak = std::abs (out->left[(std::size_t) i]);
                        at = i;
                    }
                onTime += std::abs (at - expect) <= 2 && peak > 0.7f;
                double pre = 0.0, core = 0.0;
                for (long i = expect - 1323; i < expect - 66; ++i)
                    pre += (double) out->left[(std::size_t) i] * out->left[(std::size_t) i];
                for (long i = expect - 66; i < expect + 66; ++i)
                    core += (double) out->left[(std::size_t) i] * out->left[(std::size_t) i];
                worstPre = std::max (worstPre, 10.0 * std::log10 ((pre + 1e-20) / core));
            }
            CHECK (onTime == 10);
            CHECK (worstPre < -30.0);
        }
}

TEST (q160_stretch_tonal_purity)
{
    // a steady 1 kHz tone stretched by 1.25 / 0.8 stays clean (old WSOLA ≈ 148 dB on a pure sine, but the plain
    // Signalsmith preset gave 57 dB; LYNX's 120 ms / 20 ms configuration ≈ 100 dB)
    const auto in = q160::sineOf (1000.0, 3.0);
    for (auto method : { lynx::StretchMethod::general, lynx::StretchMethod::mono })
        for (double ratio : { 0.8, 1.25 })
        {
            const auto out = lynx::timeStretch (in, ratio, method);
            CHECK (q160::sinadDb (*out, 1000.0) > 75.0);
        }
    const auto drums = lynx::timeStretch (in, 1.25, lynx::StretchMethod::transients);
    CHECK (q160::sinadDb (*drums, 1000.0) > 45.0);
}

TEST (q160_stretch_short_sounds_and_mapping)
{
    // a 40 ms one-shot (shorter than the analysis block) still stretches to the right length and is not silent
    auto hit = q160::sineOf (200.0, 0.04);
    for (std::size_t i = 0; i < hit.left.size(); ++i)
        hit.left[i] *= (float) std::exp (-(double) i / 300.0);
    const auto out = lynx::timeStretch (hit, 1.5);
    CHECK (std::abs ((double) out->frames() - (double) hit.frames() * 1.5) < 1.5);
    float peak = 0.0f;
    for (auto v : out->left)
        peak = std::max (peak, std::abs (v));
    CHECK (peak > 0.25f);

    // FL's stretch modes (REA_rodada3 §5.2) → LYNX's methods
    CHECK (lynx::stretchMethodForFlMode (-2) == lynx::StretchMethod::general); // Stretch Pro
    CHECK (lynx::stretchMethodForFlMode (5) == lynx::StretchMethod::general);  // Auto
    CHECK (lynx::stretchMethodForFlMode (2) == lynx::StretchMethod::mono);     // E3 mono
    CHECK (lynx::stretchMethodForFlMode (9) == lynx::StretchMethod::mono);     // E2 speech
    CHECK (lynx::stretchMethodForFlMode (7) == lynx::StretchMethod::transients); // E2 transient
    CHECK (lynx::stretchMethodForFlMode (3) == lynx::StretchMethod::transients); // slice stretch

    // attack detection: one attack per click, on the click's sample
    const auto clicks = q160::clicksOf (4, 0.3);
    const auto attacks = lynx::stretchdetail::detectAttacks (clicks.left.data(), (long) clicks.frames(), 44100.0, 9.0);
    CHECK (attacks.size() == 4);
    CHECK (attacks.size() == 4 && attacks[0] == 4410 && attacks[3] == (long) std::llround (1.0 * 44100.0));
    // a steady tone has none
    const auto tone = q160::sineOf (440.0, 1.0);
    CHECK (lynx::stretchdetail::detectAttacks (tone.left.data(), (long) tone.frames(), 44100.0, 9.0).size() <= 1);
}

TEST (q160_master_protection_distortion_before_and_after)
{
    // a mastered mix peaking at −0.3 dBFS: the old soft clip bent every peak (measured THD), the new one is bit-exact
    const auto amp = std::pow (10.0, -0.3 / 20.0);
    std::vector<float> a (44100), b;
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] = (float) (amp * std::sin (2.0 * 3.141592653589793 * 1000.0 * (double) i / 44100.0));
    b = a;
    auto legacy = a;
    lynx::legacySoftClip (legacy.data(), (int) legacy.size());
    lynx::clipOvers (b.data(), (int) b.size());
    CHECK (b == a);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        diff += (legacy[i] - a[i]) * (legacy[i] - a[i]);
        ref += (double) a[i] * a[i];
    }
    CHECK (10.0 * std::log10 (diff / ref) > -60.0); // the old curve changed the signal audibly (≈ −48 dB, REA_rodada160)
}

TEST (q160_dither_survives_the_writer_conversion)
{
    // JUCE: int32 = round((2^31 − 1)·x), then the low 16 / 8 bits dropped. Every grid value must come out as itself,
    // for both signs and up to full scale (before round 160 positive values above −6 dBFS came out 1 LSB low).
    for (int bits : { 16, 24 })
    {
        lynx::Dither d (bits, false);
        const auto scale = (double) (1 << (bits - 1));
        const int shift = 32 - bits;
        int wrong = 0;
        std::vector<float> x (20000);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (0.999 * std::sin ((double) i * 0.0123));
        auto y = x;
        d.process (y.data(), (int) y.size(), 0);
        for (std::size_t i = 0; i < y.size(); ++i)
        {
            const auto k = (long long) std::llround (((double) y[i] - (double) d.writerBias()) * scale); // the grid value
            const double samp = y[i];
            const auto i32 = samp >= 1.0 ? 2147483647LL : (long long) std::floor (2147483647.0 * samp + 0.5);
            const auto written = i32 >> shift; // arithmetic shift, like JUCE's Int16/Int24 converters
            wrong += written != k;
        }
        CHECK (wrong == 0);
    }
}

TEST (q160_event_sort_is_stable_and_allocation_free)
{
    lynx::EventBuffer b;
    // offsets out of order, an "off" after an "on" on the same sample, equal events keep their order (keys)
    const int offsets[] = { 30, 10, 10, 20, 10, 0 };
    for (int i = 0; i < 6; ++i)
    {
        lynx::NoteEvent e;
        e.sampleOffset = offsets[i];
        e.type = i == 4 ? lynx::NoteEvent::Type::off : lynx::NoteEvent::Type::on;
        e.key = i;
        b.add (e);
    }
    b.sortByOffset();
    CHECK (b.size() == 6);
    CHECK (b[0].sampleOffset == 0 && b[1].key == 4 /* off first */ && b[2].key == 1 && b[3].key == 2);
    CHECK (b[4].sampleOffset == 20 && b[5].sampleOffset == 30);
}

TEST (q160_sampler_uses_render_quality_when_export_starts_after_prepare)
{
    // the export engine prepares its instruments first and switches to offline rendering afterwards: the note must
    // still get FL's render interpolation (32-point sinc), i.e. sound like an instrument prepared during the export
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 44100.0;
    for (int i = 0; i < 20000; ++i)
        data->left.push_back ((float) std::sin ((double) i * 0.9) * 0.5f); // bright: the kernels differ
    auto render = [&] (bool offlineBeforePrepare) {
        lynx::setOfflineRendering (offlineBeforePrepare);
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        inst->configure (c, 120.0);
        lynx::setOfflineRendering (true);
        inst->noteOn (63, 1.0f);
        std::vector<float> l (4000), r (4000);
        inst->render (l.data(), r.data(), 4000, 1.0f, 1.0f);
        lynx::setOfflineRendering (false);
        return l;
    };
    CHECK (render (false) == render (true));
}

TEST (q160_asio_suggestion)
{
    using lynx::prefs::suggestedAsioDriver;
    const std::vector<std::string> laptop { "ASIO4ALL v2", "FL Studio ASIO", "SSL ASIO Driver 1" };
    CHECK (suggestedAsioDriver ("Windows Audio", laptop) == "SSL ASIO Driver 1");
    CHECK (suggestedAsioDriver ("DirectSound", laptop) == "SSL ASIO Driver 1");
    CHECK (suggestedAsioDriver ("ASIO", laptop).empty());
    CHECK (suggestedAsioDriver ("Windows Audio (Exclusive Mode)", laptop).empty());
    CHECK (suggestedAsioDriver ("Windows Audio", { "ASIO4ALL v2", "LYNX ASIO" }) == "LYNX ASIO");
    CHECK (suggestedAsioDriver ("Windows Audio", { "ASIO4ALL v2" }) == "ASIO4ALL v2");
    CHECK (suggestedAsioDriver ("Windows Audio", {}).empty());
}

TEST (q160_preview_interpolation_44k1_file_on_48k_device)
{
    // the Browser / editor preview reads with FL's 24-point sinc now (it was linear: a 10 kHz tone of a 44.1 kHz file on
    // a 48 kHz device came out 1.5 dB low with an image at 13.9 kHz only 21 dB down, REA_rodada160 §4)
    const double fi = 44100.0, fo = 48000.0, f = 10000.0;
    std::vector<float> x ((std::size_t) fi * 2);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.5 * std::sin (2.0 * 3.141592653589793 * f * (double) i / fi));
    lynx::flrs::Kernel k;
    k.setQuality (lynx::flrs::liveQuality);
    k.setSpeed (fi / fo);
    const auto half = (std::int64_t) k.halfWidth;
    lynx::SampleData out;
    out.sampleRate = fo;
    for (long j = 0; j < (long) fo; ++j)
    {
        const auto pos = (double) j * fi / fo + 100.0;
        const auto whole = lynx::flrs::wholeFrames (pos);
        float yl = 0.0f, yr = 0.0f;
        lynx::flrs::interpolate (k, x.data() + (whole - half + 1), x.data() + (whole - half + 1), pos - (double) whole, yl, yr);
        out.left.push_back (yl);
    }
    const auto sinad = q160::sinadDb (out, f);
    CHECK (sinad > 60.0);
    // the linear read it replaces, for the record
    lynx::SampleData lin;
    lin.sampleRate = fo;
    for (long j = 0; j < (long) fo; ++j)
    {
        const auto pos = (double) j * fi / fo + 100.0;
        const auto i = (std::size_t) pos;
        const auto fr = (float) (pos - (double) i);
        lin.left.push_back (x[i] + fr * (x[i + 1] - x[i]));
    }
    CHECK (q160::sinadDb (lin, f) < 30.0);
    std::printf ("  [q160] preview 10 kHz 44.1->48 kHz SINAD: sinc %.1f dB, linear %.1f dB\n", sinad, q160::sinadDb (lin, f));
}
