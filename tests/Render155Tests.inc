// Round 155: the export's plugin warm-up (core/PluginWarmup.h, AudioEngine::renderOffline). A fresh plugin instance
// whose restored state lands late (bridged over yabridge, background loading) must not be heard half-loaded at the
// start of the exported file (docs/analise/REA_rodada155.md).

#include "core/PluginWarmup.h"

namespace render155
{
// A source that is always on (a hosted instrument holding a drone): 0.25 on both sides whatever the notes.
struct Drone final : lynx::Instrument
{
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float* l, float* r, int n, float gl, float gr) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] += 0.25f * gl;
            r[i] += 0.25f * gr;
        }
    }
};

// An effect whose saved state (output gain 0.5) only takes effect after `blocks` processed blocks, or once a clock
// reaches `atTime` — as a bridged plugin applies a state forwarded over IPC a few blocks / milliseconds later. Before
// that it runs with its default gain (1.0): the "half-loaded effect".
struct LateState final : lynx::Effect
{
    int blocks = 0;
    double atTime = -1.0;
    double firstNewsAt = -1.0; // (bridged) from this time on the plugin reports intermediate values until it lands
    const double* clock = nullptr;
    int processed = 0;
    float gain = 1.0f; // what the plugin reports (its parameter)

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override
    {
        ++processed;
        const auto landed = (blocks > 0 && processed > blocks) || (atTime >= 0.0 && clock != nullptr && *clock >= atTime) || (blocks == 0 && atTime < 0.0);
        if (landed)
            gain = 0.5f;
        else if (firstNewsAt >= 0.0 && clock != nullptr && *clock >= firstNewsAt)
            gain = 0.75f - 0.01f * (float) (int) ((*clock - firstNewsAt) / 0.2); // one more value every 0.2 s
        for (int i = 0; i < n; ++i)
        {
            l[i] *= gain;
            r[i] *= gain;
        }
    }
};

lynx::Song droneSong()
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (1);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "drone";
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = "late-state";
    s.mixer[1].effects.push_back (fx);
    return s;
}

struct Rig
{
    std::shared_ptr<LateState> effect = std::make_shared<LateState>();
    std::unique_ptr<const lynx::RackState> state;
    lynx::RackRenderer renderer;
    static constexpr int block = 512;
    std::vector<float> l = std::vector<float> (block), r = std::vector<float> (block);

    Rig()
    {
        lynx::RackState::Factories f;
        f.pluginInstruments = [] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> { return std::make_shared<Drone>(); };
        auto fx = effect;
        f.pluginEffects = [fx] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> { return fx; };
        state = lynx::RackState::build (droneSong(), nullptr, 48000.0, f);
        renderer.prepare (48000.0, block);
    }

    // what the engine's warm-up does: stopped at the start, silent output thrown away
    int discard()
    {
        renderer.process (state.get(), false, 0.0, block, l.data(), r.data());
        return block;
    }

    // the render's first block (playing from the start)
    float firstSample()
    {
        renderer.process (state.get(), true, 0.0, block, l.data(), r.data());
        return l[0];
    }
};
} // namespace render155

TEST (render155_warm_up_waits_for_a_state_applied_after_n_blocks)
{
    using namespace render155;
    // the reference: a plugin whose state is in place from the start
    Rig ready;
    ready.effect->blocks = 0;
    const auto expected = ready.firstSample();
    CHECK (expected > 0.01f);

    // without the warm-up the file starts with the half-loaded effect (twice as loud here)
    Rig cold;
    cold.effect->blocks = 40;
    const auto coldFirst = cold.firstSample();
    CHECK (std::abs (coldFirst - 2.0f * expected) < 1.0e-4f);

    // with it: the state has landed before the first rendered sample
    Rig warm;
    warm.effect->blocks = 60; // ~0.64 s at 48 kHz / 512
    double clock = 0.0;
    lynx::warmup::Settings settings;
    const auto result = lynx::warmup::run (
        settings, 48000.0, [&] { clock += 0.0005; return warm.discard(); }, [&] { lynx::warmup::Hash h; h.addValue (warm.effect->gain); return h.value; },
        [&] { return clock; }, [&] (double s) { clock += s; });
    CHECK (! result.timedOut && ! result.cancelled);
    CHECK (result.blocks > 60);
    CHECK (result.audioSeconds >= settings.minAudioSeconds);
    CHECK (result.signatureChanges == 1);
    warm.renderer.prepare (48000.0, Rig::block); // as the engine does: LYNX's own units start clean
    const auto warmFirst = warm.firstSample();
    CHECK (std::abs (warmFirst - expected) < 1.0e-6f);
    // and every sample of the first block
    for (int i = 0; i < Rig::block; ++i)
        CHECK (std::abs (warm.l[(std::size_t) i] - expected) < 1.0e-6f);
}

TEST (render155_warm_up_waits_for_a_state_applied_late_in_real_time)
{
    using namespace render155;
    // a bridged plugin: the restored values arrive one by one over 2 s of wall time after creation (the first at
    // 0.3 s, then one every 0.2 s, the last at 2.0 s), whatever the number of blocks
    Rig warm;
    double clock = 0.0;
    warm.effect->atTime = 2.0;
    warm.effect->firstNewsAt = 0.3;
    warm.effect->clock = &clock;
    lynx::warmup::Settings settings;
    const auto result = lynx::warmup::run (
        settings, 48000.0, [&] { clock += 0.001; return warm.discard(); }, [&] { lynx::warmup::Hash h; h.addValue (warm.effect->gain); return h.value; },
        [&] { return clock; }, [&] (double s) { clock += s; });
    CHECK (! result.timedOut);
    CHECK (result.signatureChanges >= 5); // (one per value that arrived)
    CHECK (result.wallSeconds >= 2.0 + settings.stableWallSeconds - 0.02 && result.wallSeconds < 2.5);
    // paced: the discarded audio does not run far ahead of real time once the minimum is processed
    CHECK (result.audioSeconds <= result.wallSeconds + 0.05);
    Rig ready;
    const auto expected = ready.firstSample();
    warm.renderer.prepare (48000.0, Rig::block);
    CHECK (std::abs (warm.firstSample() - expected) < 1.0e-6f);
}

TEST (render155_warm_up_minimum_time_covers_a_silent_late_load)
{
    using namespace render155;
    // a plugin that reports nothing while it loads and lands 0.8 s (wall time) after creation: the minimum real time
    Rig warm;
    double clock = 0.0;
    warm.effect->atTime = 0.8;
    warm.effect->clock = &clock;
    const auto result = lynx::warmup::run (
        lynx::warmup::Settings {}, 48000.0, [&] { clock += 0.001; return warm.discard(); },
        [&] { lynx::warmup::Hash h; h.addValue (warm.effect->gain); return h.value; }, [&] { return clock; }, [&] (double s) { clock += s; });
    CHECK (result.wallSeconds >= 1.0);
    Rig ready;
    const auto expected = ready.firstSample();
    warm.renderer.prepare (48000.0, Rig::block);
    CHECK (std::abs (warm.firstSample() - expected) < 1.0e-6f);
}

TEST (render155_warm_up_minimums_timeout_and_cancel)
{
    // nothing changes: it stops after the minimums (audio and real time)
    {
        double clock = 0.0;
        int blocks = 0;
        lynx::warmup::Settings s;
        const auto r = lynx::warmup::run (
            s, 48000.0, [&] { ++blocks; clock += 0.0001; return 512; }, [] { return std::uint64_t { 7 }; }, [&] { return clock; },
            [&] (double d) { clock += d; });
        CHECK (! r.timedOut && r.signatureChanges == 0);
        CHECK (r.audioSeconds >= s.minAudioSeconds && r.wallSeconds >= s.minWallSeconds && r.wallSeconds < s.minWallSeconds + 0.1);
        CHECK (r.blocks == blocks);
    }
    // a plugin that never stops changing (a meter exposed as a parameter): the timeout ends it
    {
        double clock = 0.0;
        std::uint64_t n = 0;
        lynx::warmup::Settings s;
        s.maxWallSeconds = 2.0;
        const auto r = lynx::warmup::run (
            s, 48000.0, [&] { clock += 0.001; return 512; }, [&] { return ++n; }, [&] { return clock; }, [&] (double d) { clock += d; });
        CHECK (r.timedOut && r.wallSeconds >= 2.0 && r.wallSeconds < 2.1);
    }
    // cancelled from the progress window
    {
        double clock = 0.0;
        int blocks = 0;
        const auto r = lynx::warmup::run (
            lynx::warmup::Settings {}, 48000.0, [&] { clock += 0.001; return ++blocks > 5 ? -1 : 512; }, [] { return std::uint64_t { 1 }; },
            [&] { return clock; }, [&] (double d) { clock += d; });
        CHECK (r.cancelled && r.blocks == 5);
    }
    // quantised: float noise in a cached parameter value is not a change
    lynx::warmup::Hash a, b, c;
    a.addValue (0.5f);
    b.addValue (0.5f + 1.0e-7f);
    c.addValue (0.51f);
    CHECK (a.value == b.value && a.value != c.value);
}

TEST (render155_export_option_plugin_warm_up)
{
    lynx::exportopts::Options o;
    CHECK (o.pluginWarmUp); // on by default
    CHECK (lynx::exportopts::fromText ("").pluginWarmUp);
    o.pluginWarmUp = false;
    CHECK (! lynx::exportopts::fromText (lynx::exportopts::toText (o)).pluginWarmUp);
    o.pluginWarmUp = true;
    CHECK (lynx::exportopts::fromText (lynx::exportopts::toText (o)).pluginWarmUp);
}
