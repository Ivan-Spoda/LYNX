// Round 105: FL's Fruity Send as an exact engine with a real mid-chain send to another insert
// (docs/analise/REA_rodada105_send.md).

#include "core/FruityBasicFx.h"
#include "core/Pdc.h"

namespace send105
{
std::vector<std::uint8_t> state (int pan, int vol, int dry, int track)
{
    std::vector<std::uint8_t> d (16);
    const std::int32_t v[4] = { pan, vol, dry, track };
    std::memcpy (d.data(), v, 16);
    return d;
}

lynx::EffectSlot sendSlot (int pan, int vol, int dry, int track)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::fruitySend);
    lynx::flbasic::sendToParams ({ pan, vol, dry, track }, slot.params.data());
    return slot;
}

// channel 0 on insert `from` plays an impulse of 1 at sample 0 (both sides) then a DC of 0.25 from sample 16
std::pair<std::vector<float>, std::vector<float>> render (const std::function<void (lynx::Song&)>& tweak, int from, int n = 600)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (4);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "imp";
    c.volume = 1.0f;
    c.mixerTrack = from;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    tweak (s);
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.assign (48000, 0.25f);
        for (int i = 1; i < 16; ++i)
            d->left[(std::size_t) i] = 0.0f;
        d->left[0] = 1.0f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto st = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> l ((std::size_t) n), rr ((std::size_t) n);
    r.process (st.get(), false, 0.0, n, l.data(), rr.data(), &live);
    return { l, rr };
}

int firstAbove (const std::vector<float>& x, float level)
{
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > level)
            return (int) i;
    return -1;
}
} // namespace send105

TEST (fruity_send_state_import_and_texts)
{
    using namespace lynx;
    const auto slot = effectFromFl ("Fruity Send", send105::state (-54, 256, 197, 2), 120.0);
    CHECK (slot && slot->kind == EffectKind::fruitySend);
    if (slot)
    {
        const auto s = flbasic::sendFromParams (slot->params.data());
        CHECK (s.pan == -54 && s.vol == 256 && s.dry == 197 && s.track == 2);
        CHECK (fruitySendTarget (*slot) == 2);
        CHECK (formatEffectParam (EffectKind::fruitySend, 1, slot->params[1], 120.0) == "+0.0 dB");
        CHECK (formatEffectParam (EffectKind::fruitySend, 3, slot->params[3], 120.0) == "Insert 2");
    }
    // no destination: a Fruity Send that only scales what continues on its insert
    const auto none = effectFromFl ("Fruity Send", send105::state (0, 256, 256, 0), 120.0);
    CHECK (none && none->kind == EffectKind::fruitySend && fruitySendTarget (*none) == -1);
    CHECK (formatEffectParam (EffectKind::fruitySend, 3, 0.0f, 120.0) == "None");
    // a state too short for the four values keeps the old approximation (dry level only)
    const auto shortState = effectFromFl ("Fruity Send", std::vector<std::uint8_t> (12, 0), 120.0);
    CHECK (shortState && shortState->kind == EffectKind::utility);
    CHECK (paramIndex (EffectKind::fruitySend, "Send to") == 3);
    CHECK (plugui::hasFlLayout (EffectKind::fruitySend));
}

TEST (fruity_send_engine_law)
{
    using namespace lynx;
    // centre pan, volume 256 (L11 law: exactly 1): the send is the input; dry 0 (L11: 0) silences the insert
    {
        flbasic::SendEngine e;
        e.prepare (44100);
        e.set ({ 0, 256, 0, 3 });
        e.flush();
        const float l[2] = { 0.5f, -0.25f }, r[2] = { 0.125f, 1.0f };
        float sl[2] = { 0.0f, 1.0f }, sr[2] = { 0.0f, 0.0f };
        e.process (l, r, sl, sr, 2);
        CHECK (sl[0] == 0.5f && sl[1] == 0.75f && sr[0] == 0.125f && sr[1] == 1.0f); // added into the buffer
        CHECK (e.dryGain() == 0.0f);
    }
    // fully left: the right side's signal is halved into the left one, the right send is silent
    {
        flbasic::SendEngine e;
        e.prepare (44100);
        e.set ({ -128, 256, 256, 3 });
        e.flush();
        const float l[1] = { 0.5f }, r[1] = { 0.25f };
        float sl[1] = { 0.0f }, sr[1] = { 0.0f };
        e.process (l, r, sl, sr, 1);
        CHECK (std::abs (sl[0] - (0.5f * 0.5f + 0.25f * 0.5f)) < 1e-7f && sr[0] == 0.0f);
        CHECK (e.dryGain() == 1.0f);
    }
    // half right, volume 320 (L11: (11^1.25 − 1) / 10): left keeps half, a quarter of the left goes right
    {
        flbasic::SendEngine e;
        e.prepare (44100);
        e.set ({ 64, 320, 128, 3 });
        e.flush();
        const auto g = (float) ((std::pow (11.0, 1.25) - 1.0) / 10.0);
        const float l[1] = { 1.0f }, r[1] = { 0.0f };
        float sl[1] = { 0.0f }, sr[1] = { 0.0f };
        e.process (l, r, sl, sr, 1);
        CHECK (std::abs (sl[0] - 0.5f * g) < 1e-5f && std::abs (sr[0] - 0.25f * g) < 1e-5f);
        CHECK (std::abs (e.dryGain() - (float) ((std::sqrt (11.0) - 1.0) / 10.0)) < 1e-6f); // dry 128 = half on the L11 law
    }
    // a volume change ramps at the host's step (0.001 per sample at 44.1 kHz), never jumps
    {
        flbasic::SendEngine e;
        e.prepare (44100);
        e.set ({ 0, 256, 256, 3 });
        e.flush();
        e.set ({ 0, 0, 256, 3 });
        std::vector<float> l (512, 1.0f), r (512, 1.0f), sl (512, 0.0f), sr (512, 0.0f);
        e.process (l.data(), r.data(), sl.data(), sr.data(), 512);
        CHECK (std::abs (sl[0] - 1.0f) < 1e-6f && std::abs (sl[100] - 0.9f) < 1e-4f && std::abs (sl[511] - (1.0f - 0.511f)) < 1e-3f);
    }
}

TEST (fruity_send_routes_mid_chain_in_the_same_block)
{
    using namespace lynx;
    // insert 1 → Fruity Send (dry 0) → insert 3 (not routed to the Master from 1): the signal comes out of
    // insert 3 at the same sample, at the send level
    const auto [l, r] = send105::render ([] (Song& s) { s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 0, 3)); }, 1);
    CHECK (send105::firstAbove (l, 0.5f) == 0 && std::abs (l[0] - 1.0f) < 1e-5f && std::abs (r[0] - 1.0f) < 1e-5f);
    CHECK (std::abs (l[599] - 0.25f) < 1e-5f);

    // dry kept: the insert's own output plus the send (twice the level)
    const auto [l2, r2] = send105::render ([] (Song& s) { s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 256, 3)); }, 1);
    CHECK (std::abs (l2[0] - 2.0f) < 1e-5f && std::abs (l2[599] - 0.5f) < 1e-5f);

    // to a lower-numbered insert (3 → 1): the routing order puts the destination after the source, no delay
    const auto [l3, r3] = send105::render ([] (Song& s) { s.mixer[3].effects.push_back (send105::sendSlot (0, 256, 0, 1)); }, 3);
    CHECK (send105::firstAbove (l3, 0.5f) == 0 && std::abs (l3[0] - 1.0f) < 1e-5f);
    Song order;
    order.mixer = Song::defaultMixer (4);
    order.mixer[3].effects.push_back (send105::sendSlot (0, 256, 0, 1));
    const auto o = order.mixerOrder();
    CHECK (std::find (o.begin(), o.end(), 3) < std::find (o.begin(), o.end(), 1));
    CHECK (! order.canRoute (1, 3)); // a route back would close a cycle

    // the destination's effects process the sent signal; a muted source sends nothing; a bypassed slot sends nothing
    const auto [l4, r4] = send105::render ([] (Song& s) {
        s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 0, 3));
        auto gain = makeEffectSlot (EffectKind::utility);
        gain.params[0] = 0.25f; // utility gain below unity
        s.mixer[3].effects.push_back (gain);
    }, 1);
    CHECK (std::abs (l4[599]) < 0.25f - 1e-3f && std::abs (l4[599]) > 1e-4f);
    const auto [l5, r5] = send105::render ([] (Song& s) {
        s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 0, 3));
        s.mixer[1].muted = true;
    }, 1);
    CHECK (send105::firstAbove (l5, 1e-6f) < 0);
    const auto [l6, r6] = send105::render ([] (Song& s) {
        s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 0, 3));
        s.mixer[1].effects.back().enabled = false;
    }, 1);
    CHECK (std::abs (l6[599] - 0.25f) < 1e-5f); // bypassed: the insert's signal goes on unchanged, nothing is sent

    // soloing the destination keeps the source audible
    const auto [l7, r7] = send105::render ([] (Song& s) {
        s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 0, 3));
        s.mixer[3].solo = true;
    }, 1);
    CHECK (std::abs (l7[599] - 0.25f) < 1e-5f);
}

TEST (fruity_send_pdc_counts_the_slots_before_it)
{
    using namespace lynx;
    // insert 1: [latent 100][Fruity Send → 2][latent 50]; channel 1 on insert 2. The send leaves after 100 samples,
    // so insert 2's input aligns at 100 and the channel on it is delayed by 100; insert 1's output (150) is the total.
    Song s;
    s.mixer = Song::defaultMixer (3);
    s.channels.resize (2);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 2;
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::eq));
    s.mixer[1].effects.push_back (send105::sendSlot (0, 256, 256, 2));
    s.mixer[1].effects.push_back (makeEffectSlot (EffectKind::eq));
    const auto plan = computePdc (s, { 0, 0 }, { {}, { 100, 0, 50 } });
    CHECK (plan.inAlign[2] == 100 && plan.channelDelay[1] == 100);
    CHECK (plan.sendDelay[1].size() == 2 && plan.sendDelay[1][1] == 0); // [Master, the Fruity Send]
    CHECK (plan.total == 150 && plan.sendDelay[2][0] == 50);
}
