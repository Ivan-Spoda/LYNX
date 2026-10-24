// Round 124 (extras): her corpus' other third-party plugins measured on the official trials and their LYNX substitutes
// matched (docs/analise/REA_rodada124_extras.md). Golden curves are the references' own measured responses at her
// saved settings.

#include "core/FabFilterStates.h"
#include "core/FacetEq.h"
namespace m124x
{
constexpr double sr = 48000.0;

// a FabFilter "FFBS" component state (version 1) holding `values`
inline std::vector<std::uint8_t> ffbs (const std::vector<float>& values)
{
    std::vector<std::uint8_t> b { 'F', 'F', 'B', 'S' };
    auto put = [&b] (std::uint32_t u) {
        for (int i = 0; i < 4; ++i)
            b.push_back ((std::uint8_t) (u >> (8 * i)));
    };
    put (1);
    put ((std::uint32_t) values.size());
    for (const auto v : values)
    {
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        put (u);
    }
    return b;
}

inline lynx::EffectSlot substitute (const char* name, const std::vector<std::uint8_t>& state, int& translated)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = name;
    id.state = state;
    const auto* entry = lynx::findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    translated = 0;
    return entry != nullptr ? lynx::makeSubstituteSlot (*entry, &id, &translated) : lynx::EffectSlot {};
}

// |H(f)| in dB of an effect (left channel), from its impulse response
inline std::vector<double> magnitudeDb (const lynx::EffectSlot& slot, const std::vector<double>& hz, int length = 1 << 15)
{
    auto fx = lynx::makeEffect (slot);
    fx->prepare (sr);
    fx->reset();
    std::vector<float> l ((std::size_t) length, 0.0f), r ((std::size_t) length, 0.0f);
    l[0] = r[0] = 1.0f;
    for (int off = 0; off < length; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, length - off), slot.params, 120.0);
    std::vector<double> out;
    for (const auto f : hz)
    {
        double re = 0.0, im = 0.0;
        const auto w = 2.0 * 3.141592653589793 * f / sr;
        for (int i = 0; i < length; ++i)
        {
            re += l[(std::size_t) i] * std::cos (w * i);
            im -= l[(std::size_t) i] * std::sin (w * i);
        }
        out.push_back (10.0 * std::log10 (re * re + im * im + 1e-30));
    }
    return out;
}

// one Pro-Q 4 band (23 values) in a 600-value state; the globals as her states hold them
inline void proQBand (std::vector<float>& v, int band, double hz, double gainDb, double qNorm, int shape, int slope)
{
    const auto b = (std::size_t) (23 * band);
    const float values[23] { 1, 1, (float) std::log2 (hz), (float) gainDb, (float) qNorm, (float) shape, (float) slope, 2, 1, 0, 1, 1, 1,
                             50, 50, 0, 0, 6.644f, 11.55f, 0, 0, 50, 0 };
    std::copy (std::begin (values), std::end (values), v.begin() + (std::ptrdiff_t) b);
}

inline std::vector<float> proQGlobals()
{
    std::vector<float> v (600, 0.0f);
    for (int b = 0; b < 24; ++b)
    {
        v[(std::size_t) (23 * b + 1)] = 1.0f;
        v[(std::size_t) (23 * b + 2)] = 9.966f;
        v[(std::size_t) (23 * b + 4)] = 0.5f;
        v[(std::size_t) (23 * b + 6)] = 2.0f;
        v[(std::size_t) (23 * b + 7)] = 2.0f;
    }
    const float g[] { 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, -1, 1, 2, 2, 3, 0, 1, 1, 2, 0, 0, 1, 1 };
    std::copy (std::begin (g), std::end (g), v.begin() + 552);
    return v;
}
} // namespace m124x

TEST (pro_q4_states_translate_onto_facet_and_match_the_reference_round124)
{
    using namespace lynx;
    // the reference's |H| in dB (official trial, impulse response, exact DTFT) at her four saved states
    const std::vector<double> hz { 29.297, 58.594, 99.609, 199.219, 498.047, 1001.953, 1998.047, 4998.047, 10001.953, 15000.0, 19001.953 };
    struct Case
    {
        const char* what;
        std::vector<std::array<double, 5>> bands; // hz, gain dB, Q (norm), shape, slope index
        std::array<double, 11> ref;
    };
    const Case cases[] {
        { "bell 153.5 Hz -1.5 dB Q 1", { { 153.51, -1.5, 0.5, 0, 2 } },
          { -0.110, -0.429, -1.072, -1.316, -0.283, -0.071, -0.018, -0.003, -0.001, -0.000, -0.000 } },
        { "low cut 508.8 Hz + high cut 9868 Hz, 12 dB/oct", { { 508.77, 0, 0.51, 2, 2 }, { 9868.0, 0, 0.508, 4, 2 } },
          { -49.584, -37.532, -28.289, -16.206, -2.557, 0.028, 0.092, -0.026, -2.615, -7.935, -12.274 } },
        { "low cut 82.4 Hz Q 0.471, 24 dB/oct", { { 82.370, 0, 0.398, 2, 4 } },
          { -37.090, -16.766, -6.662, -1.604, -0.246, -0.060, -0.015, -0.002, -0.001, -0.000, -0.000 } },
        { "bells 116.8 Hz -4.56 dB Q 4.27, 9899 Hz -0.12 dB Q 1.96", { { 116.76, -4.56, 0.696692, 0, 2 }, { 9898.7, -0.12, 0.591071, 0, 2 } },
          { -0.037, -0.223, -2.364, -0.380, -0.032, -0.008, -0.005, -0.023, -0.120, -0.047, -0.024 } },
    };
    for (const auto& c : cases)
    {
        auto v = m124x::proQGlobals();
        int b = 0;
        for (const auto& band : c.bands)
            m124x::proQBand (v, b++, band[0], band[1], band[2], (int) band[3], (int) band[4]);
        int n = 0;
        const auto slot = m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
        CHECK (slot.kind == EffectKind::facetEq && n == 5 * (int) c.bands.size() + 5); // round 144: + resolution, character
        const auto mag = m124x::magnitudeDb (slot, hz);
        double worst = 0.0;
        for (std::size_t k = 0; k < hz.size(); ++k)
            worst = std::max (worst, std::abs (mag[k] - c.ref[k]));
        CHECK (worst < 0.1);
        if (worst >= 0.1 || std::getenv ("M124X_VERBOSE"))
            for (std::size_t k = 0; k < hz.size(); ++k) std::printf ("    %.0f Hz lynx %.3f ref %.3f\n", hz[k], mag[k], c.ref[k]);
        if (worst >= 0.1)
            std::printf ("  Pro-Q 4 %s: worst %.3f dB\n", c.what, worst);
    }
    // a band placed on one side only, or unused, is not taken; a state of another size is not read
    {
        auto v = m124x::proQGlobals();
        m124x::proQBand (v, 0, 1000.0, 6.0, 0.5, 0, 2);
        v[7] = 3.0f; // mid
        int n = 0;
        const auto slot = m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
        CHECK (n == 5 && facet::shape (slot.params[(std::size_t) facet::param (0, 0)]) == facet::Shape::off);
        v.resize (599);
        m124x::substitute ("FabFilter Pro-Q 4", m124x::ffbs (v), n);
        CHECK (n == 0);
    }
}

TEST (waves_s1_imager_translates_onto_the_stereo_shaper_matrix_round124)
{
    using namespace lynx;
    // L → L, R → L, L → R, R → R of the official trial (white noise, least squares; REA_rodada124_extras §4)
    struct Case
    {
        const char* values;
        double ll, rl, lr, rr;
    };
    const Case cases[] {
        { "0 1.22 1 650 0 0 * * * * * 0 0 0 0 0 0 0", 1.0395, -0.1030, -0.1030, 1.0395 },   // her state 0
        { "0 3 1 650 0 0 * * * * * 0 0 0 0 0 0 0", 1.63299, -0.81650, -0.81650, 1.63299 }, // her state 1
        { "0 0.38 1 650 0 0 * * * * * 0 0 0 0 0 0 0", 0.69, 0.31, 0.31, 0.69 },
        { "0 1.88 1 650 0 0 * * * * * 0 0 0 0 0 0 0", 1.23126, -0.37622, -0.37622, 1.23126 },
        { "0 1 1 650 22.5 0 * * * * * 0 0 0 0 0 0 0", 0.92388, -0.38268, 0.38268, 0.92388 },
        { "0 1.88 1 650 22.5 0 * * * * * 0 0 0 0 0 0 0", 1.28151, -0.81877, 0.12360, 0.99357 },
        { "-12 1 1 650 0 0 * * * * * 0 0 0 0 0 0 0", 0.25119, 0.0, 0.0, 0.25119 },
    };
    const auto slotFor = [] (const char* values, int& n) {
        PluginIdentity id;
        id.format = ForeignPlugin::Format::vst3;
        id.name = "WaveShell1-VST3 16.0_x64";
        id.state = substtest::wavesComponent ("S1 Imager", values);
        const auto* entry = findPluginSubstitute (id, false);
        CHECK (entry != nullptr);
        n = -1;
        return entry != nullptr ? makeSubstituteSlot (*entry, &id, &n) : EffectSlot {};
    };
    for (const auto& c : cases)
    {
        int n = 0;
        const auto slot = slotFor (c.values, n);
        CHECK (slot.kind == EffectKind::stereoShaper && n == 4);
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        fx->reset();
        // a steady input after the gains have settled: [L, R] = [1, 0], then [0, 1]
        double out[2][2] {};
        for (int src = 0; src < 2; ++src)
        {
            std::vector<float> l (9600, src == 0 ? 1.0f : 0.0f), r (9600, src == 1 ? 1.0f : 0.0f);
            for (int off = 0; off < 9600; off += 480)
                fx->process (l.data() + off, r.data() + off, 480, slot.params, 120.0);
            out[src][0] = l.back();
            out[src][1] = r.back();
        }
        const auto worst = std::max ({ std::abs (out[0][0] - c.ll), std::abs (out[1][0] - c.rl), std::abs (out[0][1] - c.lr), std::abs (out[1][1] - c.rr) });
        CHECK (worst < 2e-4);
        if (worst >= 2e-4)
            std::printf ("  S1 %s: L=%.5f·L%+.5f·R R=%.5f·L%+.5f·R\n", c.values, out[0][0], out[1][0], out[0][1], out[1][1]);
    }
    // asymmetry (not measured beyond 0) leaves the slot untranslated
    int n = -1;
    slotFor ("0 1.22 1 650 0 45 * * * * * 0 0 0 0 0 0 0", n);
    CHECK (n == 0);
}
