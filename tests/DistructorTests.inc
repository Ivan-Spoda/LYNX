#include "core/DistructorFx.h"

// Distructor, exact (REA_rodada71): goldens of the installation's Distructor module (Module_x64.dll, run unmodified)
// on module states of our own and this test's input, state loaded before the rate, blocks of 512. Every module type
// except the Harmor shapes (IPP vector math, last-bit differences) and the cabinet (float FFT) is bit-identical; the
// goldens below were printed with 9–17 significant digits, hence the small tolerances.
TEST (distructor_engine)
{
    using namespace lynx::distructor;
    constexpr int n = 6000;
    std::vector<float> inL (n), inR (n);
    std::uint32_t s = 12345;
    for (int i = 0; i < n; ++i)
    {
        const double env = (double) (i % 8192) / 8192.0;
        for (int c = 0; c < 2; ++c)
        {
            s = s * 1664525u + 1013904223u;
            const auto v = (float) ((double) (std::int32_t) s * 4.656612873077393e-10 * 0.6 * env);
            (c == 0 ? inL : inR)[(std::size_t) i] = v;
        }
    }
    struct Case
    {
        const char* name;
        int type, algo, os;
        bool dc;
        int ftype;
        bool smooth;
        int mode;
        std::vector<std::pair<int, int>> set;
        double ss, y100, y2500, y5999, tol;
    };
    const std::vector<Case> cases {
        { "blood", 0, 0, 0, true, 0, true, 0, { { 0, 3000 }, { 1, 7000 }, { 2, 6000 }, { 3, 5000 }, { 4, 4000 }, { 5, 1 } }, 2001.1990785276823, 0.0278644934, -0.601555288, 0.168672472, 1e-8 },
        { "fastdist", 0, 1, 1, true, 0, true, 0, { { 16, 150 }, { 17, 6 }, { 18, 100 }, { 19, 90 }, { 20, 1 } }, 143.82232147647986, 0.00746897794, -0.0530273616, 0.117043905, 1e-8 },
        { "softclip", 0, 2, 0, false, 0, true, 0, { { 21, 60 }, { 22, 140 }, { 36, 9000 } }, 1312.7187293429124, 0.00815192796, -0.242243469, 0.569292426, 1e-8 },
        { "wavefolder", 0, 14, 0, true, 0, true, 0, { { 6, 4000 }, { 7, 6000 }, { 8, 3000 }, { 9, 5000 } }, 1236.1941906718992, -0.49445802, 0.268441975, -0.121243574, 1e-8 },
        { "aperture", 0, 15, 0, false, 0, true, 0, { { 10, 3000 }, { 11, 4000 }, { 12, 6000 }, { 13, 3000 }, { 14, 7000 }, { 15, 1 } }, 1090.3262624138356, 0.0248198062, 0.312451929, -0.366700262, 1e-8 },
        { "abrasive", 0, 16, 0, true, 0, true, 0, { { 28, 3000 }, { 29, 1000 }, { 30, 7000 }, { 31, 4000 }, { 32, 6000 }, { 33, 1 } }, 239.01098844425007, 0.00306873373, -0.061636474, -0.081221506, 1e-8 },
        { "crusher", 0, 17, 2, false, 0, true, 0, { { 34, 4000 }, { 35, 6000 } }, 215.42537704839316, 0.0, -0.0133385342, 0.0485080183, 1e-8 },
        { "lp6smooth", 1, 0, 0, true, 0, true, 0, { { 39, 3000 }, { 40, 5000 } }, 3.1195923783758652, 5.37097258e-05, 0.00172995729, -0.0229271241, 1e-8 },
        { "peak", 1, 0, 0, true, 12, false, 0, { { 39, 5500 }, { 40, 8000 } }, 592.80632262414531, 0.00598734571, 0.0257016197, -0.0100544095, 1e-8 },
        { "lp24alt", 1, 0, 0, true, 11, true, 0, { { 39, 6000 }, { 40, 7000 } }, 9.1666408914450805, 0.0779398531, -0.0234151389, 0.0129876016, 1e-8 },
        { "vowel", 1, 0, 0, true, 15, false, 0, { { 39, 4000 }, { 40, 6000 } }, 1284.2456911154554, -0.00385778164, 0.211883456, 1.1561923, 1e-8 },
        { "phaser2", 1, 0, 0, true, 17, true, 0, { { 39, 5000 }, { 40, 3000 } }, 532.58466069000406, 0.00691025518, 0.0993106291, -0.175770327, 1e-8 },
        { "combminus", 1, 0, 0, true, 14, false, 0, { { 39, 3000 }, { 40, 8000 } }, 327.17195266393452, 0.00354568847, -0.145344049, 0.0769093186, 1e-8 },
        { "chorus", 2, 0, 0, true, 0, true, 2, { { 43, 60 }, { 44, 1500 }, { 45, 2500 }, { 46, 2000 }, { 47, 800 }, { 48, 500 } }, 138.33688823977343, 0.00275966898, -0.143275425, 0.00370971859, 1e-8 },
        { "flathat", 0, 5, 0, true, 0, true, 0, { { 23, 400 }, { 24, 100 }, { 25, 900 }, { 26, -300 }, { 27, 900 } }, 13.957440036846645, 0.00286740158, -0.028486846, 0.0676169544, 1e-6 },
    };
    for (const auto& c : cases)
    {
        auto m = defaultModule ((ModuleType) c.type);
        m.algorithm = c.algo, m.oversample = c.os, m.removeDc = c.dc, m.filterType = c.ftype, m.smoothFilter = c.smooth;
        m.chorusMode = c.mode;
        for (const auto& [k, v] : c.set)
            m.p[(std::size_t) k] = v;
        State st;
        st.modules.push_back (m);
        st.clip = false;
        // the FL layout round trip
        const auto parsed = parse (serialize (st));
        CHECK (parsed && parsed->modules.size() == 1 && parsed->modules[0].p == m.p && parsed->modules[0].filterType == m.filterType);
        Engine e;
        e.prepare (44100.0, 512);
        e.set (st);
        auto l = inL, r = inR;
        for (int i = 0; i < n; i += 512)
            e.process (l.data() + i, r.data() + i, std::min (512, n - i));
        double ss = 0;
        for (int i = 0; i < n; ++i)
            ss += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
        const bool ok = std::abs (ss - c.ss) < c.tol * std::max (1.0, c.ss) && std::abs (l[100] - c.y100) < c.tol + 1e-9
                        && std::abs (r[2500] - c.y2500) < c.tol + 1e-9 && std::abs (l[5999] - c.y5999) < c.tol + 1e-9;
        if (! ok)
            std::printf ("  %s: ss %.17g y %.9g %.9g %.9g\n", c.name, ss, l[100], r[2500], l[5999]);
        CHECK (ok);
    }
    // the slot blob keeps the FL state and the impulse files; the slot parameters follow FL's controls
    State st;
    st.modules.push_back (defaultModule (ModuleType::ampCab));
    st.modules.push_back (defaultModule (ModuleType::distortion));
    st.modules[0].impulse = 21;
    const auto fl = serialize (st);
    const auto blob = packBlob (fl, { { 21, std::vector<std::uint8_t> { 1, 2, 3 } } });
    const auto back = parseBlob (blob);
    CHECK (back && back->modules.size() == 2 && back->modules[0].impulseFile.size() == 3 && back->modules[1].impulseFile.empty());
    CHECK (impulsesUsed (st) == std::vector<int> { 21 } && impulsePath (21).find ("IMP Cabinet Mono Closest.wv") != std::string::npos);
    std::array<float, effectParams> p {};
    toParams (st, p.data());
    CHECK (p[0] == 0.5f && p[1] == 1.0f && p[2] == 0.5f && p[12] == 1.0f && p[6] == 0.0f);
    // with the installation: the cabinet impulses decode and every demo-project Distructor imports exactly (see the
    // real-FLP survey); here the Amp/Cab module must produce sound from an installation impulse
    if (! realFlpDir.empty())
    {
        const auto folder = std::filesystem::path (realFlpDir).parent_path() / "Plugins" / "Fruity" / "Effects" / "Distructor" / "module" / "impulses";
        if (std::filesystem::exists (folder))
        {
            setImpulseFolder (folder.string());
            State cab;
            cab.modules.push_back (defaultModule (ModuleType::ampCab));
            cab.modules[0].impulse = 1;
            cab.modules[0].p[37] = 10000;
            Engine e;
            e.prepare (44100.0, 512);
            e.set (cab);
            auto l = inL, r = inR;
            for (int i = 0; i < n; i += 512)
                e.process (l.data() + i, r.data() + i, std::min (512, n - i));
            double ss = 0;
            for (int i = 0; i < n; ++i)
                ss += (double) l[(std::size_t) i] * l[(std::size_t) i];
            CHECK (ss > 1.0);
            setImpulseFolder ({});
        }
    }
}
