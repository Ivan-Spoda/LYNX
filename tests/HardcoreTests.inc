#include "core/HardcoreFx.h"

// Hardcore, exact (REA_rodada71): goldens of Hardcore_x64.dll (the installation's plug-in, run unmodified) on states of
// our own — 50 parameters from an LCG, the box types and switches set below — and this test's input, rendered with the
// state before the rate, blocks of 512. Bit-identical. The cabinet cases read the installation's impulses (skipped
// without it).
namespace
{
std::vector<std::uint8_t> hardcoreTestState (std::uint32_t seed, std::array<int, 5> types, std::array<int, 5> on, int eq, int cab)
{
    std::array<float, 50> v {};
    auto x = seed;
    for (auto& f : v)
    {
        x = (x * 1103515245u + 12345u) & 0x7fffffffu;
        f = (float) ((x >> 8) & 0x7fffffu) / 8388608.0f;
    }
    v[0] = 0.55f, v[1] = 0.45f, v[2] = (float) eq, v[11] = (float) cab;
    v[12] = v[38] = v[44] = 0.5f;
    for (int b = 0; b < 5; ++b)
        v[(std::size_t) (39 + b)] = (float) on[(std::size_t) b], v[(std::size_t) (45 + b)] = (float) types[(std::size_t) b];
    std::vector<std::uint8_t> s { 'S', 'S', 'L', 'F', 0xcc, 0x01, 0, 0, '1', 'R', 'C', 'H' };
    const auto* p = (const std::uint8_t*) v.data();
    s.insert (s.end(), p, p + 200);
    const char name[] = "LYNX test";
    s.insert (s.end(), name, name + sizeof (name) - 1);
    s.resize (8 + 0x1cc, 0);
    return s;
}

std::vector<std::uint8_t> hardcoreInstallFile (const std::string& relative)
{
    if (realFlpDir.empty())
        return {};
    auto path = std::filesystem::path (realFlpDir).parent_path();
    std::string rel = relative;
    std::replace (rel.begin(), rel.end(), '\\', '/');
    std::ifstream in (path / rel, std::ios::binary);
    return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST (hardcore_exact)
{
    constexpr int n = 8192;
    std::vector<float> inL (n), inR (n);
    for (int i = 0; i < n; ++i)
    {
        inL[(std::size_t) i] = (float) (((i * 37) % 200 - 100) / 60.0);
        inR[(std::size_t) i] = (float) (((i * 53) % 170 - 85) / 50.0);
    }
    struct Gold { int i; float l, r; };
    auto run = [&] (const std::vector<std::uint8_t>& st, bool cabinet, std::initializer_list<Gold> gold) {
        const auto s = lynx::hardcore::parse (st);
        CHECK (s.has_value() && s->presetName == "LYNX test");
        if (! s)
            return;
        lynx::hardcore::Engine e;
        if (cabinet)
            for (int c = 1; c <= 20; ++c)
                e.setCabinetFile (c, hardcoreInstallFile (lynx::hardcore::cabinetPath (c)));
        for (int i = 0; i < 50; ++i)
            e.set (i, s->values[(std::size_t) i]);
        e.prepare (44100.0);
        std::vector<float> l (inL), r (inR);
        for (int i = 0; i < n; i += 512)
            e.process (l.data() + i, r.data() + i, 512);
        for (const auto& g : gold)
            CHECK (l[(std::size_t) g.i] == g.l && r[(std::size_t) g.i] == g.r);
    };
    // distortion A, chorus, flanger, phaser, reverb; EQ on
    run (hardcoreTestState (1, { 1, 2, 3, 4, 5 }, { 1, 1, 1, 1, 1 }, 1, 0), false,
         { { 100, 0.15404899418354034f, 0.15402227640151978f }, { 1000, 0.5094000101089478f, 0.18492065370082855f },
           { 2047, -1.0110447406768799f, -0.9156813621520996f }, { 4000, -1.7594877481460571f, 0.10223101079463959f },
           { 8191, 0.14906616508960724f, 1.9577150344848633f } });
    // delay, noise gate, equalizer, compressor, modulator
    run (hardcoreTestState (2, { 6, 7, 8, 9, 10 }, { 1, 1, 1, 1, 1 }, 0, 0), false,
         { { 100, 0.0f, 0.0f }, { 1000, -0.02293410710990429f, -0.049560099840164185f },
           { 2047, 0.0009527528891339898f, 0.002417751122266054f }, { 4000, -0.07934026420116425f, -0.12522825598716736f },
           { 8191, -0.29015353322029114f, -0.10772112756967545f } });
    // tube drive, an empty box, a switched-off distortion, chorus, delay; EQ on
    run (hardcoreTestState (3, { 11, 0, 1, 2, 6 }, { 1, 1, 0, 1, 1 }, 1, 0), false,
         { { 100, 0.06743612140417099f, 0.11324237287044525f }, { 1000, -0.019734300673007965f, -0.3736976683139801f },
           { 2047, 0.8394869565963745f, 1.0702182054519653f }, { 4000, -0.34187665581703186f, -0.32515519857406616f },
           { 8191, 0.4963859021663666f, 0.36226585507392883f } });
    if (! hardcoreInstallFile (lynx::hardcore::cabinetPath (4)).empty())
    {
        // cabinets "Model D" (float WAV) and "Model T" (24-bit stereo WAV)
        run (hardcoreTestState (4, { 6, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 }, 0, 4), true,
             { { 100, 0.1504828929901123f, 0.12085545808076859f }, { 1000, -0.15621426701545715f, -0.12705086171627045f },
               { 2047, -0.17077459394931793f, 0.22107036411762238f }, { 4000, -0.15526969730854034f, -0.03145243600010872f },
               { 8191, 0.4664047658443451f, -0.37835803627967834f } });
        run (hardcoreTestState (5, { 2, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 }, 1, 20), true,
             { { 100, 0.05236158147454262f, -0.0008346981485374272f }, { 1000, 3.7916646003723145f, 6.497415542602539f },
               { 2047, -6.757109642028809f, -4.441843032836914f }, { 4000, 2.6982486248016357f, -0.5133857131004333f },
               { 8191, -2.7014360427856445f, 1.8544065952301025f } });

        // the FL import: the slot carries the installation's impulses; the mixer effect equals the engine
        const auto st = hardcoreTestState (4, { 6, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 }, 0, 4);
        const auto slot = lynx::effectFromFl ("Hardcore", st, 120.0, hardcoreInstallFile);
        CHECK (slot && slot->kind == lynx::EffectKind::hardcore && lynx::hardcore::hasCabinet (*slot, 4)
               && lynx::hardcore::hasCabinet (*slot, 20));
        if (slot)
        {
            auto fx = lynx::makeEffect (*slot);
            fx->prepare (44100.0);
            std::vector<float> l (inL), r (inR);
            fx->process (l.data(), r.data(), 700, slot->params, 120.0);
            fx->process (l.data() + 700, r.data() + 700, n - 700, slot->params, 120.0);
            CHECK (l[1000] == -0.15621426701545715f && r[8191] == -0.37835803627967834f);
        }
    }

    // import without the installation: the exact engine, cabinet off; parameters round trip through the slot
    const auto st = hardcoreTestState (1, { 1, 2, 3, 4, 5 }, { 1, 1, 1, 1, 1 }, 1, 7);
    const auto slot = lynx::effectFromFl ("Hardcore", st, 120.0);
    CHECK (slot && slot->kind == lynx::EffectKind::hardcore && slot->state.empty());
    if (slot)
    {
        const auto s = *lynx::hardcore::parse (st);
        for (int i = 0; i < 50; ++i)
            CHECK (lynx::hardcore::fromSlot (i, slot->params[(std::size_t) i]) == s.values[(std::size_t) i]);
        CHECK (lynx::formatEffectParam (lynx::EffectKind::hardcore, 45, slot->params[45], 120.0) == "Distortion");
        CHECK (lynx::formatEffectParam (lynx::EffectKind::hardcore, 11, slot->params[11], 120.0) == "Model G");
    }
    CHECK (lynx::effectInfo (lynx::EffectKind::hardcore).params.size() == 50);
    CHECK (lynx::hardcore::cabinetPath (9) == "Plugins\\Fruity\\Effects\\Hardcore\\Imp\\Model I .wav");
    CHECK (! lynx::hardcore::parse (std::vector<std::uint8_t> (100, 0)).has_value());
}
