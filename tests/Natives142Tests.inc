// Round 142 (issue #5): FL engines usable in native LYNX projects (core/NativeEngines.h, REA_rodada142)

#include "../tools/WavReader.h"
#include "core/NativeEngines.h"

namespace natives142
{
// the user's FL installation as the tests see it (WSL / POSIX): FL's backslashes become '/', names matched ignoring case
lynx::natives::FlAccess flAccess()
{
    namespace fs = std::filesystem;
    lynx::natives::FlAccess fl;
    if (realFlpDir.empty())
        return fl;
    const auto local = [] (std::string p) {
        std::replace (p.begin(), p.end(), '\\', '/');
        return lynx::existingPathIgnoringCase (p);
    };
    fl.roots.data = fs::path (realFlpDir).string();
    fl.roots.factoryData = fs::path (realFlpDir).parent_path().string();
    fl.fileExists = [local] (const std::string& p) { return fs::is_regular_file (local (p)); };
    fl.readFile = [local] (const std::string& p) {
        std::ifstream f (local (p), std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    };
    fl.listPresets = [local] (const std::string& folder) {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator (local (folder), ec))
            if (e.is_regular_file())
                out.push_back (e.path().string());
        std::sort (out.begin(), out.end());
        return out;
    };
    fl.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) {
        const auto path = (fs::temp_directory_path() / ("lynx-natives142-" + std::to_string (wav.size()) + "-" + std::to_string (name.size()) + ".wav")).string();
        std::ofstream (path, std::ios::binary).write ((const char*) wav.data(), (std::streamsize) wav.size());
        return path;
    };
    return fl;
}

// one channel alone in the rack, a note held for `seconds`: the output's RMS
double renderChannel (const lynx::ChannelDef& channel, const lynx::natives::FlAccess& fl, int key = 60, double seconds = 1.0)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    auto samples = [&fl] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto p = path;
        std::replace (p.begin(), p.end(), '\\', '/');
        (void) fl;
        return lynx::tools::readWav (lynx::existingPathIgnoringCase (p));
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, samples);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0;
    on.key = key;
    on.velocity = 0.8f;
    live.push (on);
    const auto n = (int) (seconds * 48000.0);
    double sum = 0.0;
    for (int done = 0; done < n; done += 512)
    {
        std::vector<float> l (512), rr (512);
        r.process (state.get(), false, 0.0, 512, l.data(), rr.data(), &live, nullptr);
        for (int i = 0; i < 512; ++i)
            sum += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) rr[(std::size_t) i] * rr[(std::size_t) i];
    }
    return std::sqrt (sum / (2.0 * n));
}
} // namespace natives142

TEST (natives142_lists_and_no_fl_installation)
{
    using namespace lynx;
    // every engine once; the effect engines are not plain parameter effects
    std::set<int> seen;
    for (const auto& e : natives::instrumentEngines())
        CHECK (seen.insert ((int) e.kind).second);
    seen.clear();
    for (const auto& e : natives::effectEngines())
        CHECK (seen.insert ((int) e.kind).second);
    CHECK (natives::instrumentEngine (InstrumentKind::sytrus) != nullptr && natives::instrumentEngine (InstrumentKind::synth) == nullptr);
    CHECK (natives::effectEngine (EffectKind::newtone) != nullptr && natives::effectEngine (EffectKind::eq) == nullptr);

    // without FL: the engines with LYNX's own defaults are made and play, the others are not available
    natives::FlAccess none;
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives::makeChannel (e.kind, none);
        CHECK (natives::available (e, none) == (e.needs == natives::Needs::nothing));
        CHECK (c.has_value() == (e.needs == natives::Needs::nothing));
        if (c)
        {
            CHECK (c->instrument == e.kind && c->name == e.name);
            CHECK (natives142::renderChannel (*c, none) > 1.0e-3);
        }
    }
    for (const auto& e : natives::effectEngines())
    {
        const auto fx = natives::makeSlot (e.kind, none);
        CHECK (fx.has_value() == (e.needs == natives::Needs::nothing));
        if (fx)
            CHECK (fx->kind == e.kind);
    }
    CHECK (natives::newEffect (EffectKind::eq, none).has_value()); // (a plain effect: always)
    CHECK (natives::effectMenuName (EffectKind::newtone) == "Newtone (beta)" && natives::effectMenuName (EffectKind::maximus) == "Maximus");
}

TEST (natives142_fl_keys_default_wavetable)
{
    // a wavetable the installation doesn't ship: FL Keys loads the one default.wti names (FL 2026 has no "Piano")
    std::map<std::string, std::string> files {
        { "W\\default.wti", "[default]\r\nfile=mda piano.wti\r\n" },
        { "W\\mda piano.wti", "[info]\r\nname=mda\r\nsamples=1\r\nfiletype=riff\r\nsamplerate=44100\r\n\r\n[velocitylayers]\r\ncount=1\r\n0=0\r\n\r\n[sample0]\r\nname=a.wav\r\nroot=60\r\nhigh=127\r\n" },
    };
    auto read = [&] (const std::string& p) {
        const auto it = files.find (p);
        return it == files.end() ? std::vector<std::uint8_t> {} : std::vector<std::uint8_t> (it->second.begin(), it->second.end());
    };
    auto existing = [] (const std::string& p) { return p; };
    // FL Keys' state: its wavetable name "Piano" (REA_rodada69 §8 layout, see flkeys::parseState)
    lynx::flkeys::State st;
    st.wavetable = "Piano";
    const auto state = lynx::flkeys::writeState (st);
    lynx::ChannelDef def;
    CHECK (lynx::flkeys::importChannel (def, state, "W", read, existing, {}));
    CHECK (def.instrument == lynx::InstrumentKind::flkeys && def.zones.size() == 1 && def.zones[0].path == "W\\a.wav");
    files.erase ("W\\default.wti");
    lynx::ChannelDef none;
    CHECK (! lynx::flkeys::importChannel (none, state, "W", read, existing, {}));
}

TEST (natives142_engines_from_the_fl_installation)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    int made = 0;
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives::makeChannel (e.kind, fl);
        CHECK (c.has_value());
        if (! c)
        {
            std::printf ("  not made: %s\n", e.name);
            continue;
        }
        ++made;
        CHECK (c->instrument == e.kind && c->name == e.name);
        if (e.kind == InstrumentKind::granulizer) // (plays the channel's sample: none yet)
            continue;
        const auto rms = natives142::renderChannel (*c, fl, e.kind == InstrumentKind::drumaxx ? 48 : 60);
        CHECK (rms > 1.0e-3 && std::isfinite (rms));
        if (! (rms > 1.0e-3))
            std::printf ("  silent: %s (%g)\n", e.name, rms);
    }
    CHECK (made == (int) natives::instrumentEngines().size());

    for (const auto& e : natives::effectEngines())
    {
        const auto slot = natives::makeSlot (e.kind, fl);
        CHECK (slot.has_value() && slot->kind == e.kind);
        if (! slot)
            continue;
        auto fx = makeEffect (*slot);
        fx->prepare (48000.0);
        double in = 0.0, out = 0.0;
        bool finite = true;
        for (int b = 0; b < 94; ++b)
        {
            std::vector<float> l (512), r (512);
            for (int i = 0; i < 512; ++i)
            {
                const auto t = (b * 512 + i) / 48000.0;
                l[(std::size_t) i] = r[(std::size_t) i] = (float) (0.3 * std::sin (2.0 * M_PI * 220.0 * t) + 0.05 * std::sin (2.0 * M_PI * 3100.0 * t));
                in += 2.0 * l[(std::size_t) i] * l[(std::size_t) i];
            }
            fx->process (l.data(), r.data(), 512, slot->params, 120.0);
            for (int i = 0; i < 512; ++i)
            {
                finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]);
                out += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
            }
        }
        CHECK (finite && out > 0.01 * in);
    }
    // the state-based ones carry FL's default state (not an empty one)
    for (const auto kind : { EffectKind::maximus, EffectKind::vocodex, EffectKind::equo, EffectKind::distructor, EffectKind::lovePhilter, EffectKind::grossBeat })
    {
        const auto slot = natives::makeSlot (kind, fl);
        CHECK (slot && ! slot->state.empty());
    }
}
