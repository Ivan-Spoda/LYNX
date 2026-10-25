// Round 158: LYNX Soundfont (SF2 / SF3 player, GitHub issue #6). docs/analise/REA_rodada158.md.

#include "core/SoundFont.h"
#include "core/SoundFontIndex.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <new>

// counts the allocations of the thread that sets sfCountAllocations (the render loop's real-time check)
static std::atomic<long> sfAllocations { 0 };
static thread_local bool sfCountAllocations = false;

void* operator new (std::size_t n)
{
    if (sfCountAllocations)
        ++sfAllocations;
    if (void* p = std::malloc (n != 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
namespace sft
{
std::string tempDir (const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / ("lynx-sf158-" + std::string (name));
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
    std::filesystem::create_directories (dir);
    return dir.string();
}

std::string writeFile (const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    return path;
}

struct Rig
{
    std::shared_ptr<lynx::sf::FontHandle> font;
    std::unique_ptr<lynx::Instrument> inst;
    lynx::ChannelDef channel;
    double rate = 48000.0;

    explicit Rig (const std::string& path, double sampleRate = 48000.0) : rate (sampleRate)
    {
        font = lynx::sf::FontLibrary::instance().getNow (path);
        inst = lynx::sf::makeInstrument (font);
        inst->prepare (rate);
        channel = lynx::sf::makeChannel (path);
        inst->configure (channel, 120.0);
    }

    void set (int param, float v)
    {
        channel.modelParams[(std::size_t) param] = v;
        inst->configure (channel, 120.0);
    }

    // renders `seconds`, returns the left channel
    std::vector<float> run (double seconds)
    {
        const auto n = (int) (seconds * rate);
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        for (int at = 0; at < n; at += 256)
            inst->render (l.data() + at, r.data() + at, std::min (256, n - at), 1.0f, 1.0f);
        return l;
    }
};

float peak (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0;
    for (std::size_t i = from; i < std::min (to, x.size()); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

// zero-crossing frequency over the second half
double frequency (const std::vector<float>& x, double rate)
{
    int crossings = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = x.size() / 2 + 1; i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (double) (last - first) : 0.0;
}

std::string testFont (const char* name, const std::vector<lynx::sf::TestPreset>& presets, int layers = 2)
{
    return writeFile (tempDir (name) + "/font.sf2", lynx::sf::makeTestFont (presets, layers));
}
} // namespace sft
} // namespace

TEST (soundfont158_parse_generated_font)
{
    const auto bytes = lynx::sf::makeTestFont ({ { "Grand Piano", 0, 0 }, { "808 Kit", 128, 0 }, { "Warm Pad", 0, 89 } }, 3);
    std::string error;
    const auto font = lynx::sf::parseFont (bytes, &error);
    CHECK (font != nullptr);
    if (font == nullptr)
    {
        std::printf ("  parse error: %s\n", error.c_str());
        return;
    }
    CHECK (font->name == "LYNX test font");
    CHECK (font->presets.size() == 3);
    // sorted by bank, then program
    CHECK (font->presets[0].name == "Grand Piano" && font->presets[0].bank == 0 && font->presets[0].program == 0);
    CHECK (font->presets[1].name == "Warm Pad" && font->presets[1].program == 89);
    CHECK (font->presets[2].bank == 128);
    for (const auto& p : font->presets)
        CHECK (p.regions.size() == 6); // 2 key zones × 3 velocity layers
    const auto& r = font->presets[0].regions[0];
    CHECK (r.loKey == 0 && r.hiKey == 59 && r.loVel == 0 && r.hiVel == 41);
    CHECK (r.loop == lynx::sf::LoopMode::continuous && r.loopEnd > r.loopStart && r.loopEnd <= r.end);
    CHECK (r.rootKey == 48 && r.sampleRate == 22050);
    CHECK (std::abs (r.ampEnv.attack - 0.01f) < 0.001f && std::abs (r.ampEnv.release - 0.1f) < 0.005f);
    CHECK (font->findPreset (0, 89) == 1 && font->findPreset (128, 5) == 2 && font->findPreset (3, 0) == 0);

    // the quick scan reads the same presets (file order), from bytes and from a file without the sample data
    const auto info = lynx::sf::scanFont (bytes);
    CHECK (info.valid && info.presets.size() == 3 && info.presets[1].name == "808 Kit" && info.presets[1].bank == 128);
    const auto path = sft::writeFile (sft::tempDir ("scan") + "/font.sf2", bytes);
    const auto fileInfo = lynx::sf::scanFontFile (path);
    CHECK (fileInfo.valid && fileInfo.presets == info.presets && fileInfo.name == "LYNX test font");
    CHECK (std::string (lynx::sf::category (128, 0, "Standard")) == "Drums");
    CHECK (std::string (lynx::sf::category (0, 0, "Grand Piano")) == "Piano");
    CHECK (std::string (lynx::sf::category (0, 89, "Warm")) == "Synth pad");
    CHECK (std::string (lynx::sf::category (0, 38, "TR-808 Bass")) == "Drums"); // the "808" word wins
}

TEST (soundfont158_damaged_files)
{
    const auto bytes = lynx::sf::makeTestFont ({ { "A", 0, 0 }, { "B", 0, 1 } });
    CHECK (lynx::sf::parseFont ({}) == nullptr);
    CHECK (lynx::sf::parseFont (std::vector<std::uint8_t> (100, 0)) == nullptr);
    // every truncation and many byte flips: rejected or parsed, never a crash / out-of-range read
    int parsed = 0;
    for (std::size_t len = 0; len < bytes.size(); len += 7)
    {
        std::vector<std::uint8_t> cut (bytes.begin(), bytes.begin() + (std::ptrdiff_t) len);
        if (lynx::sf::parseFont (cut) != nullptr)
            ++parsed;
        (void) lynx::sf::scanFont (cut);
    }
    CHECK (parsed < 50);
    std::uint32_t seed = 158;
    for (int k = 0; k < 400; ++k)
    {
        auto bad = bytes;
        for (int f = 0; f < 6; ++f)
        {
            seed = seed * 1664525u + 1013904223u;
            bad[(seed >> 8) % bad.size()] = (std::uint8_t) (seed >> 24);
        }
        if (auto font = lynx::sf::parseFont (bad))
        {
            // and it plays without reading out of bounds
            auto inst = lynx::sf::makeInstrument (nullptr);
            for (const auto& p : font->presets)
                for (const auto& r : p.regions)
                    CHECK (r.start < r.end && r.end <= font->samples.size() && r.loopStart >= r.start && r.loopEnd <= r.end);
        }
    }
    const auto missing = lynx::sf::FontLibrary::instance().getNow ("/nonexistent/lynx/font.sf2");
    CHECK (missing->finished() && missing->font() == nullptr && ! missing->error().empty());
}

TEST (soundfont158_note_on_off_and_loops)
{
    const auto path = sft::testFont ("notes", { { "Looped", 0, 0, 0, true }, { "Unlooped", 0, 1, 0, false } });
    sft::Rig rig (path);
    CHECK (rig.font->font() != nullptr);
    rig.inst->noteOn (48, 0.8f);
    const auto held = rig.run (1.0);
    // a looped 0.25 s sample still sounds after a second
    CHECK (sft::peak (held, 40000, 48000) > 0.1f);
    CHECK (std::abs (sft::frequency (held, 48000.0) - 130.81) < 1.0);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->noteOff (48);
    const auto after = rig.run (0.5);
    CHECK (sft::peak (after, 20000) < 1.0e-4f); // 100 ms release
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);
    CHECK (rig.inst->idle());

    // without its loop the sample ends; "Use loops" off does the same to a looped one
    rig.set (lynx::sf::pProgram, 1);
    rig.inst->noteOn (48, 0.8f);
    auto x = rig.run (0.6);
    CHECK (sft::peak (x, 0, 10000) > 0.1f && sft::peak (x, 14000) == 0.0f);
    rig.set (lynx::sf::pProgram, 0);
    rig.set (lynx::sf::pUseLoops, 0);
    rig.inst->noteOn (48, 0.8f);
    x = rig.run (0.6);
    CHECK (sft::peak (x, 0, 10000) > 0.1f && sft::peak (x, 14000) == 0.0f);
}

TEST (soundfont158_velocity_layers_and_presets)
{
    const auto path = sft::testFont ("layers", { { "First", 0, 0 }, { "Second", 0, 1 } }, 2);
    sft::Rig rig (path);
    rig.set (lynx::sf::pVelocityCurve, 4); // fixed: only the layer changes the level
    const auto level = [&] (int program, float velocity) {
        rig.set (lynx::sf::pProgram, (float) program);
        rig.inst->noteOn (72, velocity);
        const auto x = rig.run (0.2);
        rig.inst->noteOff (72);
        (void) rig.run (0.3);
        return sft::peak (x, 4800);
    };
    const auto soft = level (0, 0.3f), loud = level (0, 0.9f);
    CHECK (std::abs (loud / soft - 0.8f / 0.5f) < 0.03f); // layer amplitudes 0.5 and 0.8
    // the second preset plays its own samples (half the level)
    CHECK (std::abs (level (1, 0.9f) / loud - 0.5f) < 0.02f);
    // the SoundFont velocity curve: 40·log10(v) dB
    rig.set (lynx::sf::pVelocityCurve, 0);
    const auto part = level (0, 0.6f), full = level (0, 1.0f); // (both in the loud layer)
    const auto half = part, expect = std::pow ((float) std::lround (0.6f * 127.0f) / 127.0f, 2.0f);
    CHECK (std::abs (half / full - expect) < 0.02f);
    // a program change (MIDI) picks the preset too
    const std::uint8_t pc[2] { 0xC0, 1 };
    rig.set (lynx::sf::pVelocityCurve, 4);
    rig.set (lynx::sf::pProgram, 0);
    rig.inst->midiMessage (pc, 2);
    rig.inst->noteOn (72, 0.9f);
    const auto x = rig.run (0.2);
    rig.inst->allNotesOff();
    CHECK (std::abs (sft::peak (x, 4800) / loud - 0.5f) < 0.02f);
}

TEST (soundfont158_exclusive_class_sustain_polyphony)
{
    const auto path = sft::testFont ("excl", { { "Hats", 128, 0, 1, true }, { "Keys", 0, 0, 0, true } }, 1);
    sft::Rig rig (path);
    rig.set (lynx::sf::pBank, 128);
    rig.set (lynx::sf::pProgramMode, 1); // (instrument: note-offs release)
    rig.inst->noteOn (42, 0.8f);
    (void) rig.run (0.1);
    rig.inst->noteOn (46, 0.8f); // same exclusive class: the first stops
    (void) rig.run (0.1);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->allNotesOff();
    (void) rig.run (0.3);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);

    // the sustain pedal holds released notes until it lifts
    rig.set (lynx::sf::pBank, 0);
    const std::uint8_t down[3] { 0xB0, 64, 127 }, up[3] { 0xB0, 64, 0 };
    rig.inst->midiMessage (down, 3);
    rig.inst->noteOn (60, 0.8f);
    (void) rig.run (0.1);
    rig.inst->noteOff (60);
    const auto sustained = rig.run (0.5);
    CHECK (sft::peak (sustained, 20000) > 0.05f && lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->midiMessage (up, 3);
    (void) rig.run (0.3);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);

    // polyphony 2: the oldest voices make room
    rig.set (lynx::sf::pPolyphony, 2);
    for (int k = 0; k < 6; ++k)
        rig.inst->noteOn (60 + k, 0.8f);
    (void) rig.run (0.1);
    CHECK (lynx::sf::activeVoices (*rig.inst) <= 2);
    rig.inst->allNotesOff();
    (void) rig.run (0.3);
    // drums mode: a note-off is ignored, the (unlooped) sample plays out
    rig.set (lynx::sf::pPolyphony, 64);
    rig.set (lynx::sf::pUseLoops, 0);
    rig.set (lynx::sf::pProgramMode, 2);
    rig.inst->noteOn (60, 0.8f);
    rig.inst->noteOff (60);
    const auto drum = rig.run (0.2);
    CHECK (sft::peak (drum, 7000, 9000) > 0.1f);
}

TEST (soundfont158_pitch_bend_and_sample_rates)
{
    const auto path = sft::testFont ("bend", { { "Keys", 0, 0 } }, 1);
    for (const double rate : { 44100.0, 96000.0 })
    {
        sft::Rig rig (path, rate);
        rig.inst->noteOn (48, 0.8f);
        const auto plain = rig.run (0.5);
        CHECK (std::abs (sft::frequency (plain, rate) - 130.81) < 1.0); // independent of the output rate
        const std::uint8_t bendUp[3] { 0xE0, 127, 127 };
        rig.inst->midiMessage (bendUp, 3);
        const auto bent = rig.run (0.5);
        CHECK (std::abs (sft::frequency (bent, rate) - 130.81 * std::pow (2.0, 2.0 / 12.0)) < 1.5); // ±2 semitones by default
        rig.set (lynx::sf::pBendRange, 12);
        const auto octave = rig.run (0.5);
        CHECK (std::abs (sft::frequency (octave, rate) - 261.63) < 2.5);
        // RPN 0 sets the range from MIDI
        for (const std::uint8_t cc : { 101, 100 })
        {
            const std::uint8_t m[3] { 0xB0, cc, 0 };
            rig.inst->midiMessage (m, 3);
        }
        const std::uint8_t data[3] { 0xB0, 6, 7 };
        rig.inst->midiMessage (data, 3);
        const auto fifth = rig.run (0.5);
        CHECK (std::abs (sft::frequency (fifth, rate) - 130.81 * std::pow (2.0, 7.0 / 12.0)) < 2.0);
        // every resampling quality plays the same pitch
        for (int q = 0; q < 4; ++q)
        {
            rig.set (lynx::sf::pResampling, (float) q);
            const std::uint8_t centre[3] { 0xE0, 0, 64 };
            rig.inst->midiMessage (centre, 3);
            const auto x = rig.run (0.4);
            CHECK (std::abs (sft::frequency (x, rate) - 130.81) < 1.0);
        }
    }
}

TEST (soundfont158_realtime_render_does_not_allocate)
{
    const auto path = sft::testFont ("rt", { { "Keys", 0, 0 } }, 2);
    sft::Rig rig (path);
    rig.set (lynx::sf::pReverbAmount, 0.5f);
    rig.set (lynx::sf::pChorusAmount, 0.5f);
    rig.set (lynx::sf::pResampling, 3);
    std::vector<float> l (512), r (512);
    rig.inst->render (l.data(), r.data(), 512, 1, 1); // (first block: settings taken)
    sfAllocations = 0;
    sfCountAllocations = true;
    for (int k = 0; k < 400; ++k)
    {
        if (k % 20 == 0)
            rig.inst->noteOn (40 + k % 40, 0.7f);
        if (k % 20 == 10)
            rig.inst->noteOff (40 + (k - 10) % 40);
        if (k == 100)
        {
            const std::uint8_t m[3] { 0xB0, 64, 127 };
            rig.inst->midiMessage (m, 3);
        }
        rig.inst->setParameter (lynx::sf::pCutoff, 0.5f);
        rig.inst->render (l.data(), r.data(), 512, 1, 1);
    }
    sfCountAllocations = false;
    CHECK (sfAllocations.load() == 0);
}

TEST (soundfont158_params_roundtrip)
{
    const auto d = lynx::sf::defaultParams();
    CHECK ((int) d.size() == lynx::sf::paramCount);
    const auto p = lynx::sf::readParams ({});
    for (int i = 0; i < lynx::sf::paramCount; ++i)
        CHECK (p[(std::size_t) i] == d[(std::size_t) i]);
    // out of range / NaN / truncated states read as valid values
    const auto q = lynx::sf::readParams ({ 999.0f, NAN, -5.0f });
    CHECK (q[lynx::sf::pBank] == 128 && q[lynx::sf::pProgram] == 0 && q[lynx::sf::pReverbAmount] == 0 && q[lynx::sf::pPolyphony] == 64);
    for (int i = 0; i < lynx::sf::paramCount; ++i)
    {
        const auto& info = lynx::sf::paramInfo (i);
        CHECK (std::abs (lynx::sf::denormalise (i, lynx::sf::normalise (i, info.def)) - info.def) < 1.0e-4f);
        CHECK (! lynx::sf::displayValue (i, info.def).empty());
    }
    const auto c = lynx::sf::makeChannel ("x.sf2", 128, 25);
    CHECK (c.instrument == lynx::InstrumentKind::soundfont && c.samplePath == "x.sf2" && c.modelParams[lynx::sf::pBank] == 128
           && c.modelParams[lynx::sf::pProgram] == 25);
}

TEST (soundfont158_preset_search_index)
{
    const auto dir = sft::tempDir ("index");
    std::filesystem::create_directories (dir + "/sub");
    sft::writeFile (dir + "/General.sf2", lynx::sf::makeTestFont ({ { "Grand Piano", 0, 0 }, { "Bright Piano", 0, 1 }, { "Fingered Bass", 0, 33 } }));
    sft::writeFile (dir + "/sub/Drums.SF2", lynx::sf::makeTestFont ({ { "TR-808", 128, 25 }, { "Standard", 128, 0 } }));
    sft::writeFile (dir + "/sub/notafont.sf2", std::vector<std::uint8_t> (64, 7));
    CHECK (lynx::sf::findFontFiles (dir).size() == 3);
    lynx::sf::PresetIndex index;
    index.setCacheFile (dir + "/cache/index.txt");
    index.setFolders ({ dir });
    for (int k = 0; k < 500 && index.busy(); ++k)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    CHECK (! index.busy());
    CHECK (index.presetCount() == 5);
    auto hits = index.search ("piano");
    CHECK (hits.size() == 2 && hits[0].category == "Piano");
    hits = index.search ("808");
    CHECK (hits.size() == 1 && hits[0].presetName == "TR-808" && hits[0].bank == 128 && hits[0].program == 25);
    hits = index.search ("drums");
    CHECK (hits.size() == 2);
    hits = index.search ("bass -piano");
    CHECK (hits.size() == 1 && hits[0].presetName == "Fingered Bass");
    CHECK (index.search ("").size() == 5);
    // whole-word name matches rank first
    hits = lynx::sf::searchFonts ({ { "a.sf2", "A", 0, 0, { { "Pianola", 0, 0 }, { "Piano", 0, 1 } } } }, "piano");
    CHECK (hits.size() == 2 && hits[0].presetName == "Piano");
    // the cache file brings the index back
    lynx::sf::PresetIndex again;
    again.setCacheFile (dir + "/cache/index.txt");
    CHECK (again.presetCount() == 5);
    CHECK (lynx::sf::PresetIndex::fromText (lynx::sf::PresetIndex::toText (index.fonts())).size() == 2);
}
