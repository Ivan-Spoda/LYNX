// Round 145 (vox145): SSL Native Vocalstrip 2 measured black-box on the official Complete Access trial; Ridge Strip's
// Vocal strip voicing (core/VoxStrip.h) and the state translator (docs/analise/REA_rodada145.md §2)
#include "core/VoxStrip.h"

namespace vox145
{
using lynx::EffectKind;
namespace r = lynx::mixsuite::ridge;

lynx::EffectParams vocal()
{
    auto p = lynx::makeEffectSlot (EffectKind::ridgeStrip).params;
    p[r::vocal] = 1.0f;
    return p;
}

// steady-state level change (dB) of a sine through Ridge with `p`, measured over the last 0.1 s of 0.5 s
double gainDb (const lynx::EffectParams& p, double hz, double dbfs, double sr = 48000.0)
{
    const auto in = mixsuite96::sine (hz, std::pow (10.0, dbfs / 20.0), (int) (0.5 * sr), sr);
    const auto out = mixsuite96::run (EffectKind::ridgeStrip, p, in, sr);
    double a = 0, b = 0;
    for (std::size_t i = in.size() - (std::size_t) (0.1 * sr); i < in.size(); ++i)
    {
        a += (double) in[i] * in[i];
        b += (double) out[i] * out[i];
    }
    return 10.0 * std::log10 (b / a);
}

std::vector<std::uint8_t> state (const std::string& a)
{
    return substtest::juceXmlComponent ("<SSL_PLUGIN_STATE PluginName=\"SSL Native Vocalstrip 2\" Version=\"6.9.4\" StateASelected=\"1\">"
                                        "<A LastLoadedPreset=\"x\"><PROCESSOR_STATE>" + a + "</PROCESSOR_STATE></A>"
                                        "<B><PROCESSOR_STATE><PARAM id=\"compOn\" value=\"0.0\"/></PROCESSOR_STATE></B></SSL_PLUGIN_STATE>");
}
} // namespace vox145

TEST (vocal_strip_voicing_laws_round145)
{
    using namespace lynx;
    namespace vs = voxstrip;
    namespace r = mixsuite::ridge;
    // [EXEC] the reference's control laws (its own parameter texts at 0 / 0.5 / 1)
    CHECK (std::abs (vs::ratio (0.0f) - 1.0) < 1e-9 && std::abs (vs::ratio (0.5f) - 3.9) < 0.06 && std::abs (vs::ratio (1.0f) - 50.0) < 1e-9);
    CHECK (std::abs (vs::ratio (vs::ratioValue (10.466)) - 10.466) < 1e-3);
    CHECK (std::abs (vs::dsFreqHz (0.672591f) - 7500.0) < 1.0 && std::abs (vs::dpFreqHz (0.709403f) - 120.0) < 0.1);
    CHECK (std::abs (vs::releaseMs (0.049762f) - 100.0) < 0.1);
    // the 24 processing orders, lexicographic like the reference's list (her state's 0.478 = De-ploser, Compander, EQ, De-esser)
    CHECK (std::string (vs::orderName (vs::orderOf (0.47826087f))) == "DpCoEqDs");
    CHECK ((vs::order (11) == std::array<int, 4> { vs::dePloser, vs::compander, vs::eq, vs::deEsser }));
    CHECK ((vs::order (23) == std::array<int, 4> { vs::compander, vs::eq, vs::dePloser, vs::deEsser }));
    // the notch's and the high band's Q follow the gain
    CHECK (std::abs (vs::notchQ (12.0) - 9.407) < 1e-6 && std::abs (vs::notchQ (-18.0) - 11.958) < 1e-6 && std::abs (vs::notchQ (-7.0) - 8.741) < 1e-3);
    CHECK (std::abs (vs::highQ (12.0) - 0.4355) < 0.002);
}

TEST (vocal_strip_voicing_compander_round145)
{
    using namespace lynx;
    namespace vs = voxstrip;
    namespace r = mixsuite::ridge;
    auto p = vox145::vocal();
    // off: neutral
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -20.0)) < 0.01);
    // [EXEC] hard knee: 10 dB over a −30 dB threshold at 10:1 → 9 dB of reduction (peak detector: the sine's peak)
    p[r::vCompOn] = 1.0f;
    p[r::vCompThreshold] = vs::thresholdValue (-30.0);
    p[r::vRatio] = vs::ratioValue (10.0);
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -20.0) + 9.0) < 0.1);
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -40.0)) < 0.05);
    // soft knee: 11.75 dB wide, at the threshold 0.9 · 11.75 / 8 = 1.32 dB
    p[r::vKnee] = 1.0f;
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -30.0) + 1.32) < 0.1);
    // make-up is plain gain; the drive: 1.5 u − u³ / 8 (+3.5 dB at low level), never above 2
    p = vox145::vocal();
    p[r::vCompOn] = 1.0f;
    p[r::vMakeup] = vs::makeupValue (12.0);
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -40.0) - 12.0) < 0.02);
    p[r::vDrive] = 1.0f;
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -40.0) - 15.52) < 0.05);
    const auto loud = mixsuite96::run (EffectKind::ridgeStrip, p, mixsuite96::sine (1000.0, 0.9, 4800, 48000.0), 48000.0);
    float peak = 0;
    for (auto v : loud)
        peak = std::max (peak, std::abs (v));
    CHECK (peak <= 2.0001f && peak > 1.99f);
    // the expander: 1:6 below its threshold + 1.16 dB
    p = vox145::vocal();
    p[r::vCompOn] = 1.0f;
    p[r::vExpThreshold] = vs::expThresholdValue (-40.0);
    std::printf ("  expander: -45 dB tone -> %.2f dB\n", vox145::gainDb (p, 1000.0, -45.0));
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -45.0) - 5.0 * (-45.0 + 40.0 - 1.16)) < 1.0); // [EXEC] the trial: -30.8
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -30.0)) < 0.05);
}

TEST (vocal_strip_voicing_de_esser_eq_latency_round145)
{
    using namespace lynx;
    namespace vs = voxstrip;
    namespace r = mixsuite::ridge;
    // [EXEC] the de-esser is level-independent: a 10 kHz tone (all of it above 5 kHz) is turned down 2/3 of the
    // threshold's 30 dB, loud or quiet; a 1 kHz tone is left alone
    auto p = vox145::vocal();
    p[r::vDsOn] = 1.0f;
    p[r::vDsSplit] = 0.0f;
    p[r::vDsFreq] = vs::dsFreqValue (5000.0);
    p[r::vDsThreshold] = vs::thresholdValue (-30.0);
    CHECK (std::abs (vox145::gainDb (p, 10000.0, -10.0) + 20.0) < 0.3);
    CHECK (std::abs (vox145::gainDb (p, 10000.0, -50.0) + 20.0) < 0.3);
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -10.0)) < 0.05);
    // EQ: the high-pass is a Q-1 cookbook filter (0 dB at its corner, a bump above); the notch peaks at its gain
    p = vox145::vocal();
    p[r::vEqOn] = 1.0f;
    p[r::vNotchOn] = 0.0f;
    p[r::vHighOn] = 0.0f;
    p[r::vHpfFreq] = vs::hpfValue (100.0);
    CHECK (std::abs (vox145::gainDb (p, 100.0, -20.0)) < 0.05 && std::abs (vox145::gainDb (p, 141.0, -20.0) - 1.25) < 0.1);
    p[r::vHpfOn] = 0.0f;
    p[r::vNotchOn] = 1.0f;
    p[r::vNotchFreq] = vs::notchValue (1000.0);
    p[r::vNotchGain] = vs::notchGainValue (-12.0);
    CHECK (std::abs (vox145::gainDb (p, 1000.0, -20.0) + 12.0) < 0.1 && std::abs (vox145::gainDb (p, 700.0, -20.0)) < 0.5);
    // the look-ahead delays the audio even with its module off (her state: the de-ploser's, 10 ms) and is reported
    p = vox145::vocal();
    p[r::vDpLookahead] = 1.0f;
    auto fx = makeEffect (EffectKind::ridgeStrip);
    fx->prepare (48000.0);
    std::vector<float> l (512, 0.0f), rr (512, 0.0f);
    l[0] = rr[0] = 1.0f;
    fx->process (l.data(), rr.data(), 512, p, 120.0);
    CHECK (fx->latencySamples() == 480 && l[480] == 1.0f && l[0] == 0.0f);
}

TEST (ssl_vocalstrip_2_state_translates_round145)
{
    using namespace lynx;
    namespace vs = voxstrip;
    namespace r = mixsuite::ridge;
    // her state (11 slots of okkk): EQ / de-esser / de-ploser off, a heavy soft-knee 10.5:1 compressor
    const auto st = vox145::state (
        "<PARAM id=\"bypassAll\" value=\"0.0\"/><PARAM id=\"compAttack\" value=\"25.5\"/><PARAM id=\"compDrive\" value=\"0.0\"/>"
        "<PARAM id=\"compKnee\" value=\"1.0\"/><PARAM id=\"compMakeup\" value=\"14.0\"/><PARAM id=\"compOn\" value=\"1.0\"/>"
        "<PARAM id=\"compRatio\" value=\"10.4660005569458\"/><PARAM id=\"compRelease\" value=\"1063.599975585938\"/>"
        "<PARAM id=\"compThreshold\" value=\"-33.20000076293945\"/><PARAM id=\"dpLookAhead\" value=\"1.0\"/><PARAM id=\"dpOn\" value=\"0.0\"/>"
        "<PARAM id=\"dsOn\" value=\"0.0\"/><PARAM id=\"eqOn\" value=\"0.0\"/><PARAM id=\"expThreshold\" value=\"-74.90000152587891\"/>"
        "<PARAM id=\"hsGain\" value=\"6.5\"/><PARAM id=\"inputGain\" value=\"-3.399997711181641\"/>"
        "<PARAM id=\"outputGain\" value=\"-17.10000038146973\"/><PARAM id=\"processOrder\" value=\"0.47826087474823\"/>");
    ForeignPlugin vs2;
    vs2.format = ForeignPlugin::Format::vst3;
    vs2.name = "SSL Native Vocalstrip 2";
    vs2.state = substtest::flVst3State (st);
    auto slot = subst113::pluginSlot (vs2);
    CHECK (substituteEffectSlot (slot, pluginIdentity (vs2)));
    CHECK (slot.kind == EffectKind::ridgeStrip && slot.params[r::vocal] == 1.0f);
    CHECK (slot.params[r::vCompOn] == 1.0f && slot.params[r::vKnee] == 1.0f && slot.params[r::vEqOn] == 0.0f);
    CHECK (std::abs (vs::thresholdDb (slot.params[r::vCompThreshold]) + 33.2) < 0.01);
    CHECK (std::abs (vs::ratio (slot.params[r::vRatio]) - 10.466) < 0.01);
    CHECK (std::abs (vs::attackMs (slot.params[r::vAttack]) - 25.5) < 0.01 && std::abs (vs::releaseMs (slot.params[r::vRelease]) - 1063.6) < 0.1);
    CHECK (std::abs (vs::makeupDb (slot.params[r::vMakeup]) - 14.0) < 0.01 && std::abs (vs::expThresholdDb (slot.params[r::vExpThreshold]) + 74.9) < 0.01);
    CHECK (std::abs (vs::gainDb (slot.params[r::vInput]) + 3.4) < 0.01 && std::abs (vs::gainDb (slot.params[r::vOutput]) + 17.1) < 0.01);
    CHECK (std::string (vs::orderName (vs::orderOf (slot.params[r::vOrder]))) == "DpCoEqDs");
    CHECK (slot.params[r::vDpLookahead] == 1.0f && slot.params[r::vDpOn] == 0.0f);
    CHECK (std::abs (vs::highGainDb (slot.params[r::vHighGain]) - 6.5) < 0.01);
    // her raw state from the project (when this machine has the round's extraction)
    const std::filesystem::path f = "/mnt/d/lynx-work/vox145/vs/states/her0.bin";
    if (std::filesystem::exists (f))
    {
        std::ifstream in (f, std::ios::binary);
        ForeignPlugin real = vs2;
        real.state = substtest::flVst3State (std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>()));
        auto s2 = subst113::pluginSlot (real);
        CHECK (substituteEffectSlot (s2, pluginIdentity (real)));
        CHECK (s2.params[r::vocal] == 1.0f && std::abs (vs::thresholdDb (s2.params[r::vCompThreshold]) + 33.2) < 0.01);
    }
    // another SSL strip's state is not read: Ridge at its defaults
    const auto other = substtest::juceXmlComponent ("<SSL_PLUGIN_STATE PluginName=\"SSL Native Channel Strip 2\"><A><PROCESSOR_STATE>"
                                                    "<PARAM id=\"compOn\" value=\"1.0\"/></PROCESSOR_STATE></A></SSL_PLUGIN_STATE>");
    ForeignPlugin cs = vs2;
    cs.name = "SSL Native Channel Strip 2";
    cs.state = substtest::flVst3State (other);
    auto s3 = subst113::pluginSlot (cs);
    CHECK (substituteEffectSlot (s3, pluginIdentity (cs)));
    CHECK (s3.kind == EffectKind::ridgeStrip && s3.params[r::vocal] == 0.0f);
}
