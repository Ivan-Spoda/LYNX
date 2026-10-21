// Round 111: crash fuzzing of the built-in effects (docs/analise/REA_rodada111.md; the full run is tools/fxfuzz.cpp and
// LYNX.exe --fxfuzz). Every effect, every parameter at both ends and random, several rates and block sizes: no crash
// and no NaN / inf output.

#include <limits>

#include "core/Biquad.h"
#include "core/RackRenderer.h"

namespace fuzz111
{
bool finite (const std::vector<float>& x)
{
    for (const auto v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}

// runs `blocks` blocks of noise through the effect; false = non-finite output
bool drive (lynx::Effect& fx, const lynx::EffectParams& params, std::mt19937& rng, int blocks)
{
    static const int sizes[] = { 1, 7, 64, 255, 512, 513, 2048 };
    std::vector<float> l (2048), r (2048);
    for (int b = 0; b < blocks; ++b)
    {
        const auto n = sizes[rng() % 7];
        for (int i = 0; i < n; ++i)
        {
            l[(std::size_t) i] = std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng);
            r[(std::size_t) i] = -0.5f * l[(std::size_t) i];
        }
        fx.process (l.data(), r.data(), n, params, 128.0);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (l[(std::size_t) i]) || ! std::isfinite (r[(std::size_t) i]))
                return false;
    }
    return true;
}
} // namespace fuzz111

TEST (fuzz_every_effect_survives_extreme_parameters)
{
    using namespace lynx;
    std::mt19937 rng (111);
    int checked = 0;
    for (int k = 0; k <= (int) EffectKind::sentryGate; ++k)
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin)
            continue;
        const auto slot = makeEffectSlot (kind);
        const auto count = (int) effectInfo (kind).params.size();
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            auto fx = makeEffect (slot);
            fx->prepare (rate);
            bool ok = fuzz111::drive (*fx, slot.params, rng, 2);
            for (int p = 0; p < count && ok; ++p)
                for (const float v : { 0.0f, 1.0f })
                {
                    auto params = slot.params;
                    params[(std::size_t) p] = v;
                    ok = ok && fuzz111::drive (*fx, params, rng, 1);
                    (void) formatEffectParam (kind, p, v, 120.0, params);
                }
            for (int round = 0; round < 3 && ok; ++round)
            {
                auto params = slot.params;
                for (int p = 0; p < count; ++p)
                    params[(std::size_t) p] = round == 0 ? 0.0f : round == 1 ? 1.0f : std::uniform_real_distribution<float> (0.0f, 1.0f) (rng);
                ok = fuzz111::drive (*fx, params, rng, 2);
            }
            if (! ok)
                std::printf ("  non-finite: %s at %.0f Hz\n", effectInfo (kind).name, rate);
            CHECK (ok);
            ++checked;
        }
    }
    CHECK (checked > 200);
}

TEST (fuzz_parametric_eq_steep_shelf_stays_finite)
{
    using namespace lynx;
    // round 111: a shelf band with a high gain and the Q knob near its top (slope S above what the gain allows) made
    // the cookbook's square root negative: NaN coefficients, a silent / broken insert and a NaN curve in the editor
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    for (const auto type : { peq::BandType::lowShelf, peq::BandType::highShelf })
        for (const double db : { -18.0, 18.0 })
            for (const float qKnob : { 0.6f, 0.8f, 1.0f })
            {
                auto p = slot.params;
                p[(std::size_t) peq::param (0, 0)] = peq::typeValue (type);
                p[(std::size_t) peq::param (0, 2)] = peq::gainValue (db);
                p[(std::size_t) peq::param (0, 3)] = qKnob;
                for (const double hz : { 20.0, 1000.0, 20000.0 })
                    CHECK (std::isfinite (peq::responseDb (p, 48000.0, hz)));
                auto fx = makeEffect (EffectKind::parametricEq);
                fx->prepare (48000.0);
                std::mt19937 rng (7);
                CHECK (fuzz111::drive (*fx, p, rng, 4));
            }
    Biquad b;
    b.shelf (48000.0, 1000.0, 18.0, false, 4.0);
    CHECK (std::isfinite (b.b0) && std::isfinite (b.a1) && std::isfinite (b.a2) && std::abs (b.a2) < 1.0);
    b.shelf (48000.0, 1000.0, 6.0, true, 1.0); // the usual range is unchanged: S = 1 at 6 dB is the cookbook value
    const auto a = std::pow (10.0, 6.0 / 40.0), w = 2.0 * 3.141592653589793 * 1000.0 / 48000.0;
    const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
    const auto a0 = (a + 1) - (a - 1) * std::cos (w) + 2.0 * std::sqrt (a) * alpha;
    CHECK (std::abs (b.a2 - ((a + 1) - (a - 1) * std::cos (w) - 2.0 * std::sqrt (a) * alpha) / a0) < 1e-12);
}

TEST (fuzz_effects_at_unusual_rates_do_not_crash)
{
    using namespace lynx;
    // round 111: Multiband Delay wrote past its crossover mask below 44.1 kHz; Fruity Vocoder's band top overflowed an
    // int above 107 kHz
    std::mt19937 rng (3);
    for (const auto kind : { EffectKind::multibandDelay, EffectKind::vocoder })
        for (const double rate : { 22050.0, 32000.0, 176400.0, 192000.0 })
        {
            auto fx = makeEffect (makeEffectSlot (kind));
            fx->prepare (rate);
            CHECK (fuzz111::drive (*fx, makeEffectSlot (kind).params, rng, 3));
        }
}

TEST (fuzz_renderer_mutes_an_effect_that_outputs_nan)
{
    using namespace lynx;
    // a NaN reaching an insert effect (here from the source itself) is muted after that effect and its state reset,
    // so it never spreads to the rest of the mix
    Song s;
    s.mixer = Song::defaultMixer (2);
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = "nan";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::parametricEq));
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (4800, 0.25f);
        d->left[10] = std::numeric_limits<float>::quiet_NaN();
        return std::shared_ptr<const SampleData> (d);
    };
    auto st = RackState::build (s, nullptr, 48000.0, provider);
    RackRenderer r;
    r.prepare (48000.0, 256);
    LiveEventQueue live;
    NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    const auto before = RackRenderer::nonFiniteEffectBlocks();
    std::vector<float> l (1024), rr (1024);
    r.process (st.get(), false, 0.0, 1024, l.data(), rr.data(), &live);
    CHECK (fuzz111::finite (l) && fuzz111::finite (rr));
    CHECK (RackRenderer::nonFiniteEffectBlocks() > before);
}
