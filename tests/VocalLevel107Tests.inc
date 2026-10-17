// --- round 107: vocals quieter than in FL (a Waves shell blacklisted by the plugin scan) -----------------------------

#include "core/PluginScanPolicy.h"
#include "core/PluginSubstitutes.h"

TEST (plugin_scan_waits_longer_for_files_with_many_plugins)
{
    using namespace lynx::scanpolicy;
    // one plugin (or an unknown count): the old 30 s
    CHECK (timeoutMs (0) == 30000);
    CHECK (timeoutMs (1) == 30000);
    // a Waves shell (hundreds of classes, each created and initialised to be described) gets minutes, not 30 s
    CHECK (timeoutMs (2) == 32000);
    CHECK (timeoutMs (400) == 30000 + 399 * 2000);
    CHECK (timeoutMs (400) > 10 * 60 * 1000);
    // never unbounded
    CHECK (timeoutMs (100000) == maxTimeoutMs);
    CHECK (timeoutMs (1 << 30) == maxTimeoutMs);
}

namespace vocal107
{
// a project with Master, insert 1 → insert 2 → Master, and FL's route levels for both routes (0xE1)
lynx::Song importRoutes (std::int32_t sendLevel, std::int32_t masterLevel)
{
    FlpBuilder b (96);
    b.ascii (0xC7, "24.2.2.4646");
    auto insert = [&b] (std::vector<std::uint8_t> routes) {
        std::vector<std::uint8_t> params (12, 0);
        params[4] = 0x08; // enabled
        b.data (0xEC, params);
        routes.resize (127, 0);
        b.data (0xEB, routes);
    };
    insert ({});
    insert ({ 0, 0, 1 });  // insert 1 → insert 2 only
    insert ({ 1 });        // insert 2 → Master
    std::vector<std::uint8_t> controls;
    auto control = [&controls] (std::uint32_t id, std::int32_t value) {
        for (auto v : { 0u, id, (std::uint32_t) value })
            for (int k = 0; k < 4; ++k)
                controls.push_back ((std::uint8_t) (v >> (8 * k)));
    };
    // FL 24.2 stores the mixer class as 0x2…, routes at 0x1F40 + destination (normalised on load)
    control (0x20000000u + (1u << 22) + 0x1F40u + 2u, sendLevel);
    control (0x20000000u + (2u << 22) + 0x1F40u, masterLevel);
    b.data (0xE1, controls);
    return lynx::importFlp (flp::parseProject (b.bytes));
}
} // namespace vocal107

TEST (flp_route_levels_use_the_fader_curve_and_reach_plus_5_6_db)
{
    // REA_rodada107 §3 [EXEC]: FL renders of the user's song with the vocal route at 16000 / 12800 / 6400 differ by
    // +5.6 / 0 / −12.7 dB, and the route to Master counts the same way. LYNX read 16000 as 100 % (0 dB) and 6400 as
    // 50 % (−6 dB): her vocals, routed insert → FX insert → Master at 16000 each, came out 11 dB quieter than in FL.
    auto db = [] (float g) { return 20.0f * std::log10 (g); };
    const auto full = vocal107::importRoutes (16000, 16000);
    CHECK (full.mixer.size() > 2 && full.mixer[1].sends.size() == 1 && full.mixer[1].sends[0].target == 2);
    CHECK (! full.mixer[1].toMaster && full.mixer[2].toMaster);
    CHECK (std::abs (db (full.mixer[1].sends[0].level) - 5.59f) < 0.02f);
    CHECK (std::abs (db (full.mixer[2].masterLevel) - 5.59f) < 0.02f);
    CHECK (std::abs (full.mixer[1].sends[0].level - lynx::maxFaderGain) < 1e-3f);

    const auto unity = vocal107::importRoutes (12800, 12800);
    CHECK (std::abs (unity.mixer[1].sends[0].level - 1.0f) < 1e-5f && std::abs (unity.mixer[2].masterLevel - 1.0f) < 1e-5f);

    const auto low = vocal107::importRoutes (6400, 6400);
    CHECK (std::abs (db (low.mixer[1].sends[0].level) - (-12.7f)) < 0.05f);
    CHECK (std::abs (db (low.mixer[2].masterLevel) - (-12.7f)) < 0.05f);

    // no level record: the route plays at 0 dB
    const auto none = lynx::Song::defaultMixer (3);
    CHECK (none[1].masterLevel == 1.0f);
}

TEST (mixer_route_to_master_level_and_hot_sends_are_rendered)
{
    // two channels of 0.5 DC on inserts 1 and 2 → 1.0 on Master; the route levels scale each path
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[1].masterLevel = 0.5f; }) - 0.75f) < 1e-4f);
    // insert 1 → insert 3 at +5.6 dB (FL's top), insert 1 not to Master
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) {
                         s.setRoute (1, 3, true, lynx::maxFaderGain);
                         s.setRoute (1, 0, false);
                     }) - (0.5f + 0.5f * lynx::maxFaderGain)) < 1e-3f);
    // the old 0..1 clamp is gone, but a send never exceeds the fader's top
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (4);
    s.setRoute (1, 2, true, 5.0f);
    CHECK (s.mixer[1].sends.size() == 1 && std::abs (s.mixer[1].sends[0].level - lynx::maxFaderGain) < 1e-6f);
}

TEST (fader_position_inverts_fader_gain)
{
    for (float p : { 0.05f, 0.25f, 0.5f, 0.8f, 0.95f, 1.0f })
        CHECK (std::abs (lynx::faderPosition (lynx::faderGain (p)) - p) < 1e-4f);
    CHECK (lynx::faderPosition (0.0f) == 0.0f && lynx::faderPosition (-1.0f) == 0.0f);
    CHECK (std::abs (lynx::faderPosition (1.0f) - lynx::unityFader) < 1e-5f);
    CHECK (lynx::faderPosition (10.0f) == 1.0f);
    CHECK (std::abs (lynx::faderGain (1.0f) - lynx::maxFaderGain) < 1e-3f);
}

TEST (compressor_auto_gain_keeps_the_long_term_level)
{
    // a vocal-like signal (a 220 Hz tone with a slow 2 Hz swell, peaks near −6 dBFS) through LYNX's Compressor at its
    // defaults (−18 dB, 4:1, no makeup): quieter; with "Auto gain" the level after a few seconds is the input's
    auto run = [] (bool autoGain) {
        auto fx = lynx::makeEffect (lynx::EffectKind::compressor);
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::compressor);
        slot.params[5] = autoGain ? 1.0f : 0.0f;
        fx->prepare (48000.0);
        double in = 0.0, out = 0.0;
        std::vector<float> l (480), r (480);
        for (int block = 0; block < 1000; ++block) // 10 s
        {
            for (int i = 0; i < 480; ++i)
            {
                const auto t = (block * 480 + i) / 48000.0;
                l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.5 * (0.6 + 0.4 * std::sin (2.0 * 3.14159265 * 2.0 * t)) * std::sin (2.0 * 3.14159265 * 220.0 * t));
            }
            if (block >= 600)
                for (auto v : l)
                    in += (double) v * v;
            fx->process (l.data(), r.data(), 480, slot.params, 0.0);
            if (block >= 600)
                for (auto v : l)
                    out += (double) v * v;
        }
        return 10.0 * std::log10 (out / in);
    };
    const auto plain = run (false), matched = run (true);
    std::printf ("  compressor: %.2f dB without auto gain, %.2f dB with\n", plain, matched);
    CHECK (plain < -2.0);
    CHECK (std::abs (matched) < 0.5);
}

TEST (compressor_substitutes_keep_the_level)
{
    // Renaissance Compressor (missing on the user's laptop, in FL too): the substitute shapes dynamics with auto gain
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.hasClassId = true;
    const char* hex = "5554535643527253636f6d7072657373";
    for (int i = 0; i < 16; ++i)
        id.classId[(std::size_t) i] = (std::uint8_t) std::stoi (std::string (hex + 2 * i, 2), nullptr, 16);
    lynx::EffectSlot slot;
    slot.kind = lynx::EffectKind::plugin;
    CHECK (lynx::substituteEffectSlot (slot, id));
    CHECK (slot.kind == lynx::EffectKind::compressor && slot.params[5] >= 0.5f);
}
