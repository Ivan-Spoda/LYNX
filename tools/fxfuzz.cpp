// fxfuzz: crash fuzzer for LYNX's built-in effects (round 111).
//
// Builds every effect the way the engine and the editors do and drives it hard: every parameter swept across its
// range (both extremes, random values, every parameter random at once), several sample rates and block sizes, the
// editors' read-outs (formatEffectParam, the parametric EQ's response graph, the state-backed controls of
// core/StateParams.h) and loud / silent / denormal / DC input. Sources of slots:
//   - the default slot of every EffectKind (makeEffectSlot)
//   - every FL effect preset (.fst) under a directory (effectFromFlPreset, the Browser's drop on a mixer insert)
//   - every effect slot of every .flp under a directory (importFlp)
// Reports non-finite output (NaN / inf) and, built with -fsanitize=address,undefined, any memory error.
//
// Usage: fxfuzz [--seed n] [--rounds n] [--kinds] [--dir <folder with .flp / .fst>]... [--only <kind index>]
// Exit code: 0 = clean, 1 = non-finite output found (crashes abort on their own).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FlpImport.h"
#include "core/PluginLayout.h"
#include "core/StateParams.h"
#include "flp/Flp.h"

namespace
{
int nonFinite = 0, slotsTested = 0;
std::mt19937 rng { 111 };
int rounds = 6;
bool verbose = false;

float uni() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); }

std::vector<std::uint8_t> readFile (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), {} };
}

void fillInput (float* l, float* r, int n, int mode, double& phase)
{
    for (int i = 0; i < n; ++i)
    {
        float v = 0.0f;
        switch (mode)
        {
            case 0: v = uni() * 2.0f - 1.0f; break;                      // noise
            case 1: v = (float) std::sin (phase); phase += 0.0571; break; // sine
            case 2: v = 0.0f; break;                                      // silence
            case 3: v = 1e-39f; break;                                    // denormal
            case 4: v = (uni() * 2.0f - 1.0f) * 30.0f; break;              // very loud noise (+30 dB)
            case 5: v = 1.0f; break;                                      // DC
            default: v = (i == 0) ? 1.0f : 0.0f; break;                   // impulse
        }
        l[i] = v;
        r[i] = mode == 1 ? -v : v * 0.7f;
    }
}

bool run (lynx::Effect& fx, const lynx::EffectParams& params, int blocks, int maxBlock, const char* what, const std::string& name)
{
    static std::vector<float> l (8192), r (8192);
    static const float sc[8192] {};
    double phase = 0.0;
    const int mode = (int) (rng() % 7);
    bool bad = false;

    for (int b = 0; b < blocks; ++b)
    {
        static const int sizes[] = { 1, 2, 7, 32, 64, 100, 128, 255, 256, 441, 480, 511, 512, 513, 1024, 2048 };
        int n = sizes[rng() % (sizeof (sizes) / sizeof (sizes[0]))];
        n = std::min (n, maxBlock);
        fillInput (l.data(), r.data(), n, mode, phase);
        fx.setSongPosition (b * 96.0);
        fx.setPlaying (b % 5 != 4);
        fx.setSidechain (b % 3 == 0 ? nullptr : sc, b % 3 == 0 ? nullptr : sc);
        fx.process (l.data(), r.data(), n, params, 60.0 + (b % 4) * 60.0);

        for (int i = 0; i < n; ++i)
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
            {
                bad = true;
                break;
            }

        if (bad)
            break;
    }

    if (bad)
    {
        ++nonFinite;
        std::printf ("NONFINITE %s [%s] (input mode %d)\n", name.c_str(), what, mode);
        if (verbose)
        {
            std::printf ("  params:");
            for (std::size_t i = 0; i < params.size(); ++i)
                if (params[i] != 0.0f)
                    std::printf (" [%d]=%.9g", (int) i, params[i]);
            std::printf ("\n");
        }
    }

    return ! bad;
}

void fuzzSlot (lynx::EffectSlot slot, const std::string& name)
{
    using namespace lynx;
    ++slotsTested;
    const auto kind = slot.kind;
    const auto& info = effectInfo (kind);
    const int np = std::min<int> ((int) info.params.size(), maxEffectParams);

    // editor read-outs and the EQ graph
    for (int p = 0; p < plugui::paramCount (kind) + 2; ++p)
        for (float v : { 0.0f, 1e-7f, 0.25f, 0.5f, 0.9999999f, 1.0f, uni() })
        {
            (void) formatEffectParam (kind, p, v, 120.0);
            (void) formatEffectParam (kind, p, v, 120.0, slot.params);
            (void) plugui::paramName (kind, p);
            if (plugui::stateIndex (kind, p) >= 0)
            {
                (void) plugui::stateValue01 (kind, p, slot.state);
                (void) plugui::stateRaw (kind, p, v);
            }
        }

    if (kind == EffectKind::parametricEq)
        for (int b = 0; b < peq::bands; ++b)
        {
            (void) peq::orderText (slot.params, b);
            (void) peq::widthText (slot.params, b);
        }

    const auto& layout = plugui::layoutFor (kind);
    for (const auto& item : layout.items)
    {
        (void) plugui::optionLabels (item);
        for (int page = -1; page < 4; ++page)
            (void) plugui::isShown (kind, item, page, slot.params, &slot.state);
    }

    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0, 22050.0 })
    {
        auto fx = makeEffect (slot);
        if (fx == nullptr)
            continue;
        fx->prepare (sr);
        run (*fx, slot.params, 6, 2048, "as loaded", name);

        if (kind == EffectKind::parametricEq)
            for (double hz : { 1.0, 20.0, 1000.0, sr * 0.49, sr * 0.5, sr })
                (void) peq::responseDb (slot.params, sr, hz);

        // every parameter alone at its extremes and at random values (what dragging a knob does)
        for (int p = 0; p < np; ++p)
            for (float v : { 0.0f, 1.0f, uni(), 1e-6f, 0.999999f })
            {
                auto params = slot.params;
                params[(std::size_t) p] = v;
                char what[64];
                std::snprintf (what, sizeof (what), "sr %.0f, param %d (%s) = %g", sr, p, info.params[(std::size_t) p].name, v);
                if (! run (*fx, params, 3, 2048, what, name))
                    break;
                if (kind == EffectKind::parametricEq)
                    (void) peq::responseDb (params, sr, 1000.0);
                (void) formatEffectParam (kind, p, v, 120.0, params);
            }

        // everything random at once, and all at 0 / all at 1, also with a fresh effect built from those params
        for (int k = 0; k < rounds; ++k)
        {
            auto params = slot.params;
            const float fixed = k == 0 ? 0.0f : k == 1 ? 1.0f : -1.0f;
            for (int p = 0; p < np; ++p)
                params[(std::size_t) p] = fixed >= 0.0f ? fixed : (rng() % 4 == 0 ? (float) (rng() % 2) : uni());
            char what[64];
            std::snprintf (what, sizeof (what), "sr %.0f, all params %s", sr, k == 0 ? "0" : k == 1 ? "1" : "random");
            run (*fx, params, 4, 2048, what, name);
            auto copy = slot;
            copy.params = params;
            if (auto fresh = makeEffect (copy))
            {
                fresh->prepare (sr);
                run (*fresh, params, 3, 2048, what, name);
            }
            if (kind == EffectKind::parametricEq)
                for (int b = 0; b < peq::bands; ++b)
                {
                    (void) peq::orderText (params, b);
                    (void) peq::widthText (params, b);
                    (void) peq::responseDb (params, sr, 20.0 + 1000.0 * b);
                }
        }

        // automated plugin parameters
        for (int p = 0; p < np + 2; ++p)
            fx->setParameter (p, uni());
        run (*fx, slot.params, 2, 2048, "setParameter", name);
        fx->reset();
        run (*fx, slot.params, 2, 2048, "after reset", name);
    }

    // controls kept in the slot's state (EQUO bands, Delay Bank banks, Convolver knobs, Maximus bands...)
    if (const auto* table = stateparams::tableFor (kind))
    {
        auto fx = makeEffect (slot);
        fx->prepare (48000.0);
        auto state = slot.state;
        for (int k = 0; k < rounds * 4; ++k)
        {
            for (int i = 0; i < (int) table->params.size(); ++i)
            {
                const auto& p = table->params[(std::size_t) i];
                const int choice = (int) (rng() % 3);
                const int raw = choice == 0 ? p.minimum : choice == 1 ? p.maximum : p.minimum + (int) (rng() % (std::uint32_t) std::max (1, p.maximum - p.minimum + 1));
                if (rng() % 3 == 0 || k == 0)
                {
                    state = table->with (state, i, raw);
                    (void) table->get (state, i);
                    (void) table->text (state, i, raw);
                }
            }
            auto copy = slot;
            copy.state = state;
            if (! fx->acceptState (state))
            {
                fx = makeEffect (copy);
                fx->prepare (48000.0);
            }
            run (*fx, slot.params, 3, 2048, "state controls", name);
        }
    }
}

void fuzzDir (const std::filesystem::path& dir)
{
    int presets = 0, projects = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator (dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
    {
        if (ec || ! it->is_regular_file())
            continue;
        auto ext = it->path().extension().string();
        for (auto& c : ext)
            c = (char) std::tolower ((unsigned char) c);
        const auto bytes = readFile (it->path());

        if (ext == ".fst")
        {
            if (auto slot = lynx::effectFromFlPreset (bytes, 120.0))
            {
                ++presets;
                fuzzSlot (*slot, it->path().string());
            }
        }
        else if (ext == ".flp")
        {
            lynx::Song song;
            try
            {
                song = lynx::importFlp (flp::parseProject (bytes));
            }
            catch (const std::exception&)
            {
                continue;
            }
            ++projects;
            for (std::size_t t = 0; t < song.mixer.size(); ++t)
                for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
                    if (song.mixer[t].effects[s].kind != lynx::EffectKind::plugin)
                        fuzzSlot (song.mixer[t].effects[s], it->path().string() + " insert " + std::to_string (t) + " slot " + std::to_string (s));
        }
    }
    std::printf ("  %s: %d effect presets, %d projects\n", dir.string().c_str(), presets, projects);
}
} // namespace

int main (int argc, char** argv)
{
    std::vector<std::string> dirs;
    bool kinds = true;
    int only = -1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc) rng.seed ((unsigned) std::atoi (argv[++i]));
        else if (a == "--rounds" && i + 1 < argc) rounds = std::atoi (argv[++i]);
        else if (a == "--dir" && i + 1 < argc) dirs.push_back (argv[++i]);
        else if (a == "--no-kinds") kinds = false;
        else if (a == "--verbose") verbose = true;
        else if (a == "--only" && i + 1 < argc) only = std::atoi (argv[++i]);
    }

    // --replay <kind> <sr> "<[i]=v ...>": a fresh instance of that kind with those parameters, every input mode
    for (int i = 1; i + 3 < argc; ++i)
        if (std::string (argv[i]) == "--replay")
        {
            auto slot = lynx::makeEffectSlot ((lynx::EffectKind) std::atoi (argv[i + 1]));
            const double sr = std::atof (argv[i + 2]);
            const char* s = argv[i + 3];
            int index = 0, used = 0;
            float value = 0.0f;
            while (std::sscanf (s, " [%d]=%f%n", &index, &value, &used) == 2)
            {
                slot.params[(std::size_t) index] = value;
                s += used;
            }
            for (int k = 0; k < 20; ++k)
            {
                auto fx = lynx::makeEffect (slot);
                fx->prepare (sr);
                run (*fx, slot.params, 6, 2048, "replay", "replay");
            }
            std::printf ("replay: %d of 20 non-finite\n", nonFinite);
            return 0;
        }

    if (kinds)
        for (int k = 0; k <= (int) lynx::EffectKind::sentryGate; ++k)
        {
            const auto kind = (lynx::EffectKind) k;
            if (kind == lynx::EffectKind::plugin || (only >= 0 && k != only))
                continue;
            std::printf ("kind %d %s\n", k, lynx::effectInfo (kind).name);
            std::fflush (stdout);
            fuzzSlot (lynx::makeEffectSlot (kind), lynx::effectInfo (kind).name);
        }

    for (const auto& d : dirs)
        fuzzDir (d);

    std::printf ("fxfuzz: %d slots, %d with non-finite output\n", slotsTested, nonFinite);
    return nonFinite == 0 ? 0 : 1;
}
