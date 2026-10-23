// --- Round 135 (parity135v): the description line, the Valhalla roles' mode lists, the original's preset name -------

#include "core/EchoSuite.h"
#include "core/ParamHelp.h"
#include "core/PluginLayout.h"
#include "core/PluginSubstitutes.h"

TEST (description_line_covers_every_control_round135)
{
    using namespace lynx;
    using K = EffectKind;
    // the reference reverbs / delays: every control explained (the engines this round owns)
    for (const auto kind : { K::epochVerb, K::atrium, K::foilPlate, K::nebula, K::halo, K::horizon, K::spiralEcho, K::orbit, K::latticeMod })
        for (const auto& p : effectInfo (kind).params)
        {
            if (paramhelp::describe (kind, p.name) == nullptr)
                std::printf ("  %s / %s: no description\n", effectInfo (kind).name, p.name);
            CHECK (paramhelp::describe (kind, p.name) != nullptr);
        }
    // LYNX's other engines: every control they had in round 135 (parameters appended later fall back to the name)
    const std::pair<K, int> others[] = { { K::ridgeStrip, 34 }, { K::glowLeveler, 8 }, { K::hushDeEsser, 18 }, { K::aether, 10 },
                                         { K::ember, 65 }, { K::crestCompressor, 43 }, { K::prismMultiband, 52 }, { K::sentryGate, 27 },
                                         { K::quellDenoiser, 4 }, { K::geminiDoubler, 33 }, { K::compressor, 9 }, { K::facetEq, 88 },
                                         { K::apexLimiter, 15 }, { K::lynxTune, 46 }, { K::echoform, 34 } };
    for (const auto& [kind, count] : others)
    {
        const auto& params = effectInfo (kind).params;
        for (int i = 0; i < count && i < (int) params.size(); ++i)
        {
            if (paramhelp::describe (kind, params[(std::size_t) i].name) == nullptr)
                std::printf ("  %s / %s: no description\n", effectInfo (kind).name, params[(std::size_t) i].name);
            CHECK (paramhelp::describe (kind, params[(std::size_t) i].name) != nullptr);
        }
    }
    // an engine's own wording wins over the shared one; prefixes are folded
    CHECK (std::string (paramhelp::describe (K::echoform, "Ratio")) != paramhelp::describe (K::crestCompressor, "Ratio"));
    CHECK (paramhelp::describe (K::prismMultiband, "B3 ratio") == paramhelp::describe (K::sentryGate, "Ratio"));
    CHECK (paramhelp::describe (K::horizon, "Echo time") == paramhelp::describe (K::echoform, "Time L"));
    CHECK (paramhelp::describe (K::halo, nullptr) == nullptr && paramhelp::describe (K::halo, "no such control") == nullptr);
}

TEST (description_line_mode_summary_round135)
{
    using namespace lynx;
    using K = EffectKind;
    auto p = makeEffectSlot (K::epochVerb).params;
    const auto mode = paramhelp::modeParam (K::epochVerb);
    CHECK (mode == 16);
    p[16] = presets::valueFromText (K::epochVerb, 16, "Smooth Plate").value_or (0.0f);
    auto s = paramhelp::summary (K::epochVerb, p);
    CHECK (s.param == 16 && s.text != nullptr && std::string (s.text).find ("plate") != std::string::npos);
    CHECK (s.detail != nullptr && std::string (s.detail).find ("smooth") != std::string::npos);
    // a mode whose name says nothing: the engine's own line
    CHECK (paramhelp::summary (K::nebula, makeEffectSlot (K::nebula).params).text != nullptr);
    // engines without a mode
    CHECK (paramhelp::summary (K::glowLeveler, makeEffectSlot (K::glowLeveler).params).param < 0);
    // Halo's Mono mode
    auto h = makeEffectSlot (K::halo).params;
    h[9] = 0.125f;
    CHECK (std::string (paramhelp::summary (K::halo, h).text).find ("mono") != std::string::npos);
}

TEST (halo_four_reference_modes_round135)
{
    using namespace lynx;
    // LYNX's own three keep their values (older projects unchanged), the reference's four sit between them
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.0f, 120.0) == "Wide");
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.5f, 120.0) == "Dense");
    CHECK (formatEffectParam (EffectKind::halo, 9, 1.0f, 120.0) == "Bright");
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.125f, 120.0) == "Mono");
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.25f, 120.0) == "Small stereo");
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.375f, 120.0) == "Medium stereo");
    CHECK (formatEffectParam (EffectKind::halo, 9, 0.625f, 120.0) == "Big stereo");
    // the layout's menu lists the four first, then LYNX's three, each option landing on its mode
    const auto& l = plugui::layoutFor (EffectKind::halo);
    int menus = 0;
    for (const auto& item : l.items)
        if (item.type == plugui::Item::menu && item.param != nullptr && std::string (item.param) == "Reverb mode")
        {
            ++menus;
            const auto labels = plugui::optionLabels (item);
            CHECK (labels.size() == 7);
            for (std::size_t i = 0; i < labels.size(); ++i)
                CHECK (formatEffectParam (EffectKind::halo, 9, plugui::optionValue (item, (int) i), 120.0) == labels[i]);
        }
    CHECK (menus == 1);
    // Mono sums the input (a left-only impulse comes out of both sides); the stereo modes keep the sides apart
    auto run = [] (float mode, double& left, double& right) {
        auto fx = makeEffect (EffectKind::halo);
        fx->prepare (48000.0);
        auto p = makeEffectSlot (EffectKind::halo).params;
        p[0] = 1.0f; // all wet
        p[9] = mode;
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        l[10] = 1.0f;
        for (int start = 0; start < 48000; start += 512)
            fx->process (l.data() + start, r.data() + start, std::min (512, 48000 - start), p, 120.0);
        left = right = 0.0;
        for (int i = 0; i < 48000; ++i)
            left += (double) l[(std::size_t) i] * l[(std::size_t) i], right += (double) r[(std::size_t) i] * r[(std::size_t) i];
    };
    double l = 0, r = 0;
    run (0.125f, l, r);
    CHECK (l > 0.0 && r > 0.1 * l);
    run (0.25f, l, r);
    CHECK (l > 0.0 && r < 0.01 * l);
    // the reference's state: parameter9 below 0.25 = Mono, then a step every 0.25
    auto translate = [] (const char* attrs) {
        lynx::ForeignPlugin fp;
        fp.format = lynx::ForeignPlugin::Format::vst3;
        fp.name = "ValhallaShimmer";
        fp.path = "C:\\Program Files\\Common Files\\VST3\\ValhallaShimmer.vst3";
        fp.state = substtest::flVst3State (substtest::juceXmlComponent (std::string ("<MYPLUGINSETTINGS ") + attrs + "/>"));
        const auto id = lynx::pluginIdentity (fp);
        const auto* entry = lynx::findPluginSubstitute (id, false);
        return entry != nullptr ? lynx::makeSubstituteSlot (*entry, &id) : lynx::EffectSlot {};
    };
    CHECK (formatEffectParam (EffectKind::halo, 9, translate ("parameter9=\"0.0\"").params[9], 120.0) == "Mono");
    CHECK (formatEffectParam (EffectKind::halo, 9, translate ("parameter9=\"0.3\"").params[9], 120.0) == "Small stereo");
    CHECK (formatEffectParam (EffectKind::halo, 9, translate ("parameter9=\"0.6\"").params[9], 120.0) == "Medium stereo");
    CHECK (formatEffectParam (EffectKind::halo, 9, translate ("parameter9=\"0.9\"").params[9], 120.0) == "Big stereo");
}

TEST (horizon_mode_dependent_controls_round135)
{
    using namespace lynx;
    const auto& l = plugui::layoutFor (EffectKind::horizon);
    auto shown = [&] (const char* param, float echoMode, float reverbMode) {
        auto p = makeEffectSlot (EffectKind::horizon).params;
        p[21] = echoMode;
        p[22] = reverbMode;
        int n = 0;
        for (const auto& item : l.items)
            if (item.param != nullptr && std::string (item.param) == param && plugui::isShown (EffectKind::horizon, item, -1, p))
                ++n;
        return n;
    };
    const auto tape = 1.0f / 11.0f, detune = 4.0f / 11.0f, swarm = 10.0f / 11.0f, lofi = 1.0f, chamber = 1.0f / 7.0f, space = 5.0f / 7.0f;
    CHECK (shown ("Echo drive", tape, chamber) == 1 && shown ("Echo detune", tape, chamber) == 0);
    CHECK (shown ("Echo drive", detune, chamber) == 0 && shown ("Echo detune", detune, chamber) == 1);
    CHECK (shown ("Echo drive", swarm, chamber) == 0 && shown ("Echo detune", swarm, chamber) == 1);
    CHECK (shown ("Echo drive", lofi, chamber) == 1 && shown ("Echo detune", lofi, chamber) == 0);
    CHECK (shown ("Early/late", tape, chamber) == 1 && shown ("Attack", tape, chamber) == 0);
    CHECK (shown ("Early/late", tape, space) == 0 && shown ("Attack", tape, space) == 1);
    CHECK (shown ("Early/late", tape, 1.0f) == 1 && shown ("Attack", tape, 1.0f) == 0);
}

TEST (lattice_mode_menu_and_original_preset_name_round135)
{
    using namespace lynx;
    // UberMod's nine algorithms, each on the type index whose tap count the engine plays
    const auto& l = plugui::layoutFor (EffectKind::latticeMod);
    const int counts[] = { 1, 2, 3, 3, 3, 4, 8, 8, 16 };
    int menus = 0;
    for (const auto& item : l.items)
        if (item.type == plugui::Item::menu && item.param != nullptr && std::string (item.param) == "Taps")
        {
            ++menus;
            CHECK (plugui::optionLabels (item).size() == 9);
            for (int i = 0; i < 9; ++i)
                CHECK (echosuite::lattice::tapCount (plugui::optionValue (item, i)) == counts[i]);
        }
    CHECK (menus == 1);

    // the preset name the original was saved with: a Valhalla attribute, a Waves <Preset Name>
    lynx::ForeignPlugin fp;
    fp.format = lynx::ForeignPlugin::Format::vst3;
    fp.name = "ValhallaVintageVerb";
    fp.path = "C:\\Program Files\\Common Files\\VST3\\ValhallaVintageVerb.vst3";
    fp.state = substtest::flVst3State (substtest::juceXmlComponent ("<ValhallaVintageVerb presetName=\"Detuned &amp; Big\" Mix=\"0.4\"/>"));
    const auto id = lynx::pluginIdentity (fp);
    const auto* entry = lynx::findPluginSubstitute (id, false);
    CHECK (entry != nullptr && lynx::makeSubstituteSlot (*entry, &id).presetName == "Detuned & Big");
    const std::string waves = "<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"Start Me Up\" GenericType=\"CL2A\">\n  <PresetHeader>\n   <PluginName>CLA-2A</PluginName>";
    CHECK (lynx::originalPresetName (std::vector<std::uint8_t> (waves.begin(), waves.end())) == "Start Me Up");
    const std::string empty = "<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"\" GenericType=\"CL2A\">";
    CHECK (lynx::originalPresetName (std::vector<std::uint8_t> (empty.begin(), empty.end())).empty());
}
