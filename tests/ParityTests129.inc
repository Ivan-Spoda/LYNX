// --- Round 129: the originals' preset bar — LYNX's factory presets, user preset files, A / B in the slot ----------

#include "core/EffectPresets.h"

TEST (factory_presets_resolve_every_value_round129)
{
    const auto kinds = lynx::presets::kindsWithFactoryPresets();
    CHECK (kinds.size() >= 16);
    for (const auto kind : kinds)
        for (const auto& preset : lynx::presets::factory (kind))
        {
            std::vector<std::string> unresolved;
            lynx::presets::apply (kind, preset, 120.0, &unresolved);
            if (! unresolved.empty())
                std::printf ("  %s / %s: unresolved %s\n", lynx::effectInfo (kind).name, preset.name.c_str(), unresolved.front().c_str());
            CHECK (unresolved.empty());
            CHECK (! preset.name.empty() && ! preset.values.empty());
        }
}

TEST (preset_values_land_on_the_shown_value_round129)
{
    using namespace lynx;
    // a number in the editor's own unit, an option by its name, seconds vs milliseconds, kHz
    const auto decay = presets::valueFromText (EffectKind::epochVerb, presets::paramByName (EffectKind::epochVerb, "Decay"), "2.8 s");
    CHECK (decay && formatEffectParam (EffectKind::epochVerb, 2, *decay, 120.0) == "2.80 s");
    const auto mode = presets::valueFromText (EffectKind::epochVerb, presets::paramByName (EffectKind::epochVerb, "mode"), "plate");
    CHECK (mode && formatEffectParam (EffectKind::epochVerb, 16, *mode, 120.0) == "Plate");
    const auto cut = presets::valueFromText (EffectKind::epochVerb, 13, "6 kHz");
    CHECK (cut && formatEffectParam (EffectKind::epochVerb, 13, *cut, 120.0) == "6.00 kHz");
    const auto raw = presets::valueFromText (EffectKind::epochVerb, 0, "#0.25");
    CHECK (raw && *raw == 0.25f);
    CHECK (! presets::valueFromText (EffectKind::epochVerb, 16, "No such mode"));
    CHECK (presets::paramByName (EffectKind::epochVerb, "nope") == -1);
    // a preset starts from the engine's defaults
    presets::Preset p { "t", "", { { "Mix", "25 %" } } };
    const auto params = presets::apply (EffectKind::epochVerb, p);
    CHECK (formatEffectParam (EffectKind::epochVerb, 0, params[0], 120.0) == "25%");
    CHECK (params[2] == makeEffectSlot (EffectKind::epochVerb).params[2]);
}

TEST (user_preset_text_round_trip_round129)
{
    using namespace lynx;
    auto params = makeEffectSlot (EffectKind::glowLeveler).params;
    params[0] = 0.4f;
    params[1] = 0.125f;
    const auto text = presets::toText (EffectKind::glowLeveler, "My leveler", params);
    const auto back = presets::fromText (EffectKind::glowLeveler, text);
    CHECK (back && back->name == "My leveler");
    CHECK (back && std::abs (back->params[0] - 0.4f) < 1e-6f && std::abs (back->params[1] - 0.125f) < 1e-6f);
    CHECK (back && back->params[4] == params[4]);
    CHECK (! presets::fromText (EffectKind::hushDeEsser, text)); // another engine's file
    CHECK (! presets::fromText (EffectKind::glowLeveler, "hello"));
    CHECK (presets::fileNameFor ("a/b:c?") == "a_b_c_");
    CHECK (presets::fileNameFor ("  ") == "Preset");
}

TEST (tune_presets_keep_the_key_and_scale_round129)
{
    using namespace lynx;
    auto current = makeEffectSlot (EffectKind::lynxTune).params;
    const auto key = presets::paramByName (EffectKind::lynxTune, "Key"), scale = presets::paramByName (EffectKind::lynxTune, "Scale");
    current[(std::size_t) key] = *presets::valueFromText (EffectKind::lynxTune, key, "E");
    current[(std::size_t) scale] = *presets::valueFromText (EffectKind::lynxTune, scale, "Minor");
    const auto& list = presets::factory (EffectKind::lynxTune);
    CHECK (! list.empty());
    const auto params = presets::apply (EffectKind::lynxTune, list.back(), 120.0, nullptr, &current);
    CHECK (formatEffectParam (EffectKind::lynxTune, key, params[(std::size_t) key], 120.0) == "E");
    CHECK (formatEffectParam (EffectKind::lynxTune, scale, params[(std::size_t) scale], 120.0) == "Minor");
    CHECK (params[(std::size_t) presets::paramByName (EffectKind::lynxTune, "Retune")] != current[(std::size_t) presets::paramByName (EffectKind::lynxTune, "Retune")]);
}

TEST (substitute_roles_fixed_round129)
{
    using namespace lynx;
    auto slotFor = [] (const char* name, std::uint32_t vst2 = 0) {
        PluginIdentity id;
        id.format = vst2 != 0 ? ForeignPlugin::Format::vst2 : ForeignPlugin::Format::vst3;
        id.vst2Id = vst2;
        id.name = name;
        const auto* e = findPluginSubstitute (id, false);
        return e != nullptr ? makeSubstituteSlot (*e) : EffectSlot {};
    };
    // PanMan: Auto-pan really sweeps (+50 %; 0.5 was 0 % on the bipolar depth = no movement)
    const auto pan = slotFor ("PanMan", 0x5374506eu);
    CHECK (pan.kind == EffectKind::autoPan && formatEffectParam (pan.kind, 1, pan.params[1], 120.0) == "+50%");
    // EchoBoy / PrimalTap: Echoform, a tape echo on a quarter note
    const auto echo = slotFor ("EchoBoy", 0x53744542u);
    CHECK (echo.kind == EffectKind::echoform);
    CHECK (formatEffectParam (echo.kind, presets::paramByName (echo.kind, "Mode"), echo.params[(std::size_t) presets::paramByName (echo.kind, "Mode")], 120.0) == "Tape");
    CHECK (formatEffectParam (echo.kind, presets::paramByName (echo.kind, "Note L"), echo.params[(std::size_t) presets::paramByName (echo.kind, "Note L")], 120.0) == "1/4");
    CHECK (slotFor ("PrimalTap").kind == EffectKind::echoform);
    // Little Plate: LYNX's plate
    CHECK (slotFor ("Little Plate").kind == EffectKind::foilPlate);
    // Foil Plate's EQ defaults: 700 Hz / 10 kHz like the reference
    const auto foil = makeEffectSlot (EffectKind::foilPlate);
    CHECK (formatEffectParam (EffectKind::foilPlate, 7, foil.params[7], 120.0) == "700 Hz");
    CHECK (formatEffectParam (EffectKind::foilPlate, 9, foil.params[9], 120.0) == "10.00 kHz");
}

TEST (glow_trim_and_compressor_meters_round129)
{
    using namespace lynx;
    // Glow Leveler's TRIM (after the mix) moves the output by its dB; older projects (no value) keep 0 dB
    auto run = [] (EffectKind kind, const EffectParams& p, float amplitude) {
        auto fx = makeEffect (kind);
        fx->prepare (48000.0);
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = amplitude * (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
        float peak = 0.0f;
        for (int at = 0; at < 48000; at += 512)
        {
            const auto n = std::min (512, 48000 - at);
            fx->process (l.data() + at, r.data() + at, n, p, 120.0);
        }
        for (int i = 24000; i < 48000; ++i) peak = std::max (peak, std::abs (l[(std::size_t) i]));
        return std::make_pair (peak, fx->controlOutput (0));
    };
    auto glow = makeEffectSlot (EffectKind::glowLeveler).params;
    const auto trim = presets::paramByName (EffectKind::glowLeveler, "Trim");
    CHECK (trim >= 0 && formatEffectParam (EffectKind::glowLeveler, trim, glow[(std::size_t) trim], 120.0) == "+0.0 dB");
    const auto a = run (EffectKind::glowLeveler, glow, 0.1f).first;
    glow[(std::size_t) trim] = *presets::valueFromText (EffectKind::glowLeveler, trim, "-6 dB");
    const auto b = run (EffectKind::glowLeveler, glow, 0.1f).first;
    CHECK (std::abs (20.0 * std::log10 (b / a) + 6.0) < 0.2);
    // the basic compressor publishes its gain reduction (the RComp-style panel's meter)
    auto comp = makeEffectSlot (EffectKind::compressor).params;
    const auto [peak, gr] = run (EffectKind::compressor, comp, 0.9f);
    CHECK (peak > 0.0f && gr > 0.1f);
}
