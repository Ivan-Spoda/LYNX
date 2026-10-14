// Core tests (no JUCE). Usage: lynx_tests [folder-with-flp-files]

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "core/Dither.h"
#include "core/Effects.h"
#include "core/Effector.h"
#include "core/FlpImport.h"
#include "core/FlEffects.h"
#include "core/FlPatcher.h"
#include "core/FlSamplers.h"
#include "core/FlSynths.h"
#include "core/Inflate.h"
#include "core/WavPack.h"
#include "core/FlWrapper.h"
#include "core/LiveEventQueue.h"
#include "core/RackRenderer.h"
#include "core/Scheduler.h"
#include "core/SnapshotExchange.h"
#include "core/SoftClip.h"
#include "core/Slicer.h"
#include "core/Templates.h"
#include "core/TimeStretch.h"
#include "core/WorkerPool.h"
#include "core/Song.h"
#include "core/NoteEdit.h"
#include "core/Recorder.h"
#include "flp/Flp.h"

namespace
{
int failures = 0;
int checks = 0;
std::vector<std::pair<const char*, std::function<void()>>>& registry()
{
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}

struct Register
{
    Register (const char* name, std::function<void()> fn) { registry().emplace_back (name, std::move (fn)); }
};

#define TEST(name)                                   \
    static void name();                              \
    static Register reg_##name (#name, name);        \
    static void name()

#define CHECK(cond)                                                                      \
    do                                                                                   \
    {                                                                                    \
        ++checks;                                                                        \
        if (! (cond))                                                                    \
        {                                                                                \
            ++failures;                                                                  \
            std::printf ("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                                \
    } while (false)

std::string realFlpDir;

// --- helpers for building synthetic .flp files -----------------------------

struct FlpBuilder
{
    std::vector<std::uint8_t> bytes;

    FlpBuilder (std::uint16_t ppq = 96)
    {
        put ("FLhd");
        u32 (6);
        u16 (0);
        u16 (1);
        u16 (ppq);
        put ("FLdt");
        u32 (0); // size is not used by the reader
    }

    void put (const char* s) { bytes.insert (bytes.end(), s, s + std::strlen (s)); }
    void u8 (std::uint8_t v) { bytes.push_back (v); }
    void u16 (std::uint16_t v) { u8 ((std::uint8_t) v); u8 ((std::uint8_t) (v >> 8)); }
    void u32 (std::uint32_t v) { u16 ((std::uint16_t) v); u16 ((std::uint16_t) (v >> 16)); }

    void varint (std::size_t n)
    {
        do
        {
            auto b = (std::uint8_t) (n & 0x7F);
            n >>= 7;
            u8 (n ? (std::uint8_t) (b | 0x80) : b);
        } while (n);
    }

    void data (std::uint8_t id, const std::vector<std::uint8_t>& payload)
    {
        u8 (id);
        varint (payload.size());
        bytes.insert (bytes.end(), payload.begin(), payload.end());
    }

    void ascii (std::uint8_t id, const std::string& s)
    {
        std::vector<std::uint8_t> p (s.begin(), s.end());
        p.push_back (0);
        data (id, p);
    }

    void utf16 (std::uint8_t id, const std::u16string& s)
    {
        std::vector<std::uint8_t> p;
        for (auto c : s)
        {
            p.push_back ((std::uint8_t) c);
            p.push_back ((std::uint8_t) (c >> 8));
        }
        p.push_back (0);
        p.push_back (0);
        data (id, p);
    }
};

std::vector<std::uint8_t> noteRecord (std::uint32_t pos, std::uint16_t channel, std::uint32_t len, std::uint16_t key,
                                      std::uint8_t velocity)
{
    std::vector<std::uint8_t> r (24, 0);
    std::memcpy (&r[0], &pos, 4);
    std::memcpy (&r[6], &channel, 2);
    std::memcpy (&r[8], &len, 4);
    std::memcpy (&r[12], &key, 2);
    r[16] = 120;
    r[20] = 64;
    r[21] = velocity;
    return r;
}
} // namespace

// ---------------------------------------------------------------------------
// FLP

TEST (flp_rejects_garbage)
{
    std::vector<std::uint8_t> junk (64, 0x41);
    bool threw = false;
    try { flp::EventReader r (junk); } catch (const flp::Error&) { threw = true; }
    CHECK (threw);
}

TEST (flp_reads_size_classes_varint_and_extended_ids)
{
    FlpBuilder b;
    b.u8 (0x15); b.u8 (2);                 // u8
    b.u8 (0x40); b.u16 (513);              // u16
    b.u8 (0x9C); b.u32 (128500);           // u32
    b.data (0xD5, std::vector<std::uint8_t> (300, 7)); // 2-byte varint
    b.u8 (0xAC); b.u32 (0xC0000100); b.varint (3); b.u8 (1); b.u8 (2); b.u8 (3); // extended ID

    flp::EventReader r (b.bytes);
    flp::Event e;
    CHECK (r.header().ppq == 96);
    CHECK (r.next (e) && e.id == 0x15 && e.value == 2);
    CHECK (r.next (e) && e.id == 0x40 && e.value == 513);
    CHECK (r.next (e) && e.id == 0x9C && e.value == 128500);
    CHECK (r.next (e) && e.id == 0xD5 && e.data.size() == 300 && e.data[299] == 7);
    CHECK (r.next (e) && e.id == 0xC0000100 && e.isData() && e.data.size() == 3 && e.data[2] == 3);
    CHECK (! r.next (e));
}

TEST (flp_truncated_file_throws)
{
    FlpBuilder b;
    b.u8 (0x9C); b.u8 (1); // u32 with only 1 byte
    flp::EventReader r (b.bytes);
    flp::Event e;
    bool threw = false;
    try { r.next (e); } catch (const flp::Error&) { threw = true; }
    CHECK (threw);
}

TEST (flp_version_numbers)
{
    CHECK (flp::parseVersionNumber ("25.2.5.5319") == 25002005u);
    CHECK (flp::parseVersionNumber ("20.99.3000.3031") == 20102000u);
    CHECK (flp::parseVersionNumber ("11.0.3") == 11000003u);
}

TEST (flp_playlist_record_size_by_version)
{
    CHECK (flp::playlistRecordSize (12009003, 64) == 32);
    CHECK (flp::playlistRecordSize (21000000, 120) == 60);
    CHECK (flp::playlistRecordSize (25002005, 160) == 80);
    CHECK (flp::playlistRecordSize (25002005, 60) == 60);  // size doesn't match the version -> tries the others
    CHECK (flp::playlistRecordSize (25002005, 7) == 0);
}

TEST (flp_parses_project_model)
{
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0x9C); b.u32 (174500);
    b.utf16 (0xC2, u"Título");
    b.u8 (0x41); b.u16 (3);
    b.u8 (0x96); b.u32 (0x00A14547); // BGR in the file
    auto notes = noteRecord (96, 1, 48, 64, 100);
    auto n2 = noteRecord (0, 0, 0, 60, 128);
    notes.insert (notes.end(), n2.begin(), n2.end());
    b.data (0xE0, notes);
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (0);
    b.utf16 (0xCB, u"Kick");
    {
        std::vector<std::uint8_t> levels (24, 0);
        const std::int32_t lv[] = { 3200, 12800, -150 };
        std::memcpy (levels.data(), lv, sizeof (lv));
        b.data (0xDB, levels);
    }
    b.u8 (0x68); b.u16 (5);
    b.u8 (0x40); b.u16 (1);
    b.u8 (0x15); b.u8 (2);
    b.utf16 (0xC9, u"FLEX");
    b.utf16 (0xCB, u"Lead");
    b.u8 (0x63); b.u16 (0);

    std::vector<std::uint8_t> item (80, 0);
    std::uint32_t pos = 384, len = 768;
    std::uint16_t base = 20480, idx = 20480 + 3, rv = 499 - 2;
    std::memcpy (&item[0], &pos, 4);
    std::memcpy (&item[4], &base, 2);
    std::memcpy (&item[6], &idx, 2);
    std::memcpy (&item[8], &len, 4);
    std::memcpy (&item[12], &rv, 2);
    b.data (0xE9, item);
    b.utf16 (0xCC, u"Drums");
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));
    b.data (0xEC, std::vector<std::uint8_t> (12, 0));

    const auto p = flp::parseProject (b.bytes);
    CHECK (p.version == "25.2.5.5319");
    CHECK (std::abs (p.tempo - 174.5) < 1e-9);
    CHECK (p.title == "Título");
    CHECK (p.channels.size() == 2);
    CHECK (p.channels[0].name == "Kick" && p.channels[0].mixerTrack == 5);
    CHECK (p.channels[0].hasLevels && p.channels[0].pan == 3200 && p.channels[0].volume == 12800 && p.channels[0].pitchCents == -150);
    CHECK (p.channels[1].type == flp::ChannelType::instrument && p.channels[1].pluginName == "FLEX");
    CHECK (p.patterns.size() == 1 && p.patterns[0].index == 3);
    CHECK (p.patterns[0].colour && *p.patterns[0].colour == 0x4745A1);
    CHECK (p.patterns[0].notes.size() == 2 && p.patterns[0].notes[0].position == 0); // sorted
    CHECK (p.patterns[0].notes[1].key == 64 && p.patterns[0].notes[1].length == 48);
    CHECK (p.arrangements.size() == 1 && p.arrangements[0].items.size() == 1);
    const auto& it = p.arrangements[0].items[0];
    CHECK (it.kind == flp::PlaylistItem::Kind::pattern && it.index == 3 && it.track == 2 && it.position == 384);
    CHECK (p.mixer.size() == 2 && p.mixer[0].name == "Drums" && p.mixer[1].name.empty());
    CHECK (p.warnings.empty());
}

TEST (flp_fuzz_corrupted_files_do_not_crash)
{
    if (realFlpDir.empty())
        return;

    // some real .flp files, corrupted in various ways: the parser may reject them (flp::Error), but
    // must never hang, read past the buffer or produce a Song with invalid indices
    std::vector<std::vector<std::uint8_t>> sources;
    for (auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir))
        if (entry.path().extension() == ".flp" && sources.size() < 6 && std::filesystem::file_size (entry.path()) < 3'000'000)
        {
            std::ifstream in (entry.path(), std::ios::binary);
            sources.emplace_back ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        }

    std::uint32_t seed = 2024;
    auto rnd = [&] (std::uint32_t n) { seed = seed * 1664525u + 1013904223u; return n == 0 ? 0u : (seed >> 8) % n; };
    int parsed = 0, rejected = 0;

    for (int round = 0; round < 240 && ! sources.empty(); ++round)
    {
        auto bytes = sources[(std::size_t) rnd ((std::uint32_t) sources.size())];

        switch (round % 4)
        {
            case 0: bytes.resize (rnd ((std::uint32_t) bytes.size())); break;                          // truncated
            case 1: for (int k = 0; k < 20; ++k) bytes[(std::size_t) rnd ((std::uint32_t) bytes.size())] ^= (std::uint8_t) (1 + rnd (255)); break;
            case 2: // absurd event size
                for (int k = 0; k < 5; ++k)
                    bytes[(std::size_t) rnd ((std::uint32_t) bytes.size())] = 0xFF;
                break;
            default: // shuffled block
            {
                const auto a = rnd ((std::uint32_t) bytes.size()), b = rnd ((std::uint32_t) bytes.size());
                std::reverse (bytes.begin() + std::min (a, b), bytes.begin() + std::max (a, b));
                break;
            }
        }

        try
        {
            const auto project = flp::parseProject (bytes);
            const auto song = lynx::importFlp (project);
            ++parsed;

            for (const auto& p : song.patterns)
                for (const auto& n : p.notes)
                    CHECK (n.channel >= 0 && n.channel < (int) song.channels.size());
            for (const auto& c : song.playlist)
                CHECK (c.kind != lynx::ClipKind::pattern || (c.pattern >= 0 && c.pattern < (int) song.patterns.size()));
        }
        catch (const flp::Error&)
        {
            ++rejected;
        }
    }

    std::printf ("  fuzz: %d imported, %d rejected\n", parsed, rejected);
    CHECK (parsed + rejected == 240);
}

TEST (plugin_state_decoders_survive_garbage)
{
    // random blobs (varied sizes, including valid headers followed by garbage)
    std::uint32_t seed = 77;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    const char* plugins[] = { "Fruity Parametric EQ 2", "Fruity Limiter", "Fruity Reeverb 2", "Fruity Delay 2", "Fruity Delay 3",
                              "Fruity Compressor", "Fruity Balance", "Fruity PanOMatic", "Fruity Stereo Shaper", "Fruity Phase Inverter" };
    int decoded = 0;

    for (int round = 0; round < 3000; ++round)
    {
        std::vector<std::uint8_t> blob ((std::size_t) (rnd() % 600));
        for (auto& b : blob)
            b = (std::uint8_t) rnd();
        if (blob.size() >= 4 && round % 3 == 0) // versions the readers accept
        {
            const std::uint32_t versions[] = { 8, 10001, 10000, 12, 14, 6, 1 };
            const auto v = versions[rnd() % 7];
            std::memcpy (blob.data(), &v, 4);
        }

        if (auto fx = lynx::effectFromFl (plugins[rnd() % 10], blob))
        {
            ++decoded;
            for (auto v : fx->params)
                CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
        }

        lynx::SynthSettings synth;
        if (lynx::synthFrom3xOsc (blob, synth))
            for (const auto& o : synth.osc)
                CHECK (std::isfinite (o.level) && o.level >= 0.0f && o.level <= 1.0f);

        float harmlessGain = 1.0f;
        if (lynx::synthFromHarmless (blob, 120.0, synth, harmlessGain))
            CHECK (std::isfinite (harmlessGain) && std::isfinite (synth.cutoff) && std::isfinite (synth.attack));
        if (lynx::synthFromFruitKick (blob, synth))
            CHECK (std::isfinite (synth.decay) && std::isfinite (synth.pitchEnv.amount));
        if (lynx::synthFromGms (blob, 120.0, synth, harmlessGain) || lynx::synthFromHarmor (blob, synth, harmlessGain) || lynx::synthFromOgun (blob, synth, harmlessGain)
            || lynx::synthFromSakura (blob, synth) || lynx::synthFromKepler (blob, synth, harmlessGain))
            CHECK (std::isfinite (synth.cutoff) && std::isfinite (synth.attack) && std::isfinite (harmlessGain));
        if (auto sound = lynx::drumSynthLiveSound (blob))
            CHECK (sound->size() < 256);
        if (auto wave = lynx::waveTravellerSample (blob))
            CHECK (! wave->first.empty() && wave->second >= 0 && wave->second <= 127);
        lynx::synthFromBooBass (blob, synth);
        if (lynx::synthFromPoiZone (blob, synth, harmlessGain) || lynx::synthFromBassDrum (blob, synth, harmlessGain) || lynx::synthFromToxic (blob, synth, harmlessGain)
            || lynx::synthFromMorphine (blob, synth, harmlessGain))
            CHECK (std::isfinite (synth.cutoff) && std::isfinite (harmlessGain));
        if (auto patch = lynx::drumpadPatchName (blob))
            CHECK (patch->size() <= 0x30 && std::isfinite (lynx::drumToneForName (*patch).decay));
        if (auto flex = lynx::flexPresetName (blob))
            CHECK ((! flex->first.empty() || ! flex->second.empty()) && flex->second.size() <= 1024);
        if (lynx::synthFromSytrus (blob, 120.0, synth, harmlessGain))
            CHECK (std::isfinite (synth.cutoff) && std::isfinite (synth.attack));
        if (lynx::synthFromDX10 (blob, synth))
            CHECK (std::isfinite (synth.decay) && std::isfinite (synth.release) && std::isfinite (synth.osc[1].level));
        if (blob.size() >= 4 && round % 5 == 0) // MiniSynth header followed by garbage
        {
            std::memcpy (blob.data(), "SSLF", 4);
            if (blob.size() > 220)
            {
                std::memcpy (blob.data() + 40, "DATA", 4);
                std::memcpy (blob.data() + 48, "0rev", 4);
            }
        }
        if (lynx::synthFromMiniSynth (blob, 120.0, synth))
            CHECK (std::isfinite (synth.cutoff) && std::isfinite (synth.decay) && std::isfinite (synth.attack));

        lynx::ForeignPlugin foreign;
        lynx::parseFlWrapper (blob, foreign);
        std::vector<std::uint8_t> component, controller;
        lynx::splitFlVst3State (blob, component, controller);
    }

    CHECK (decoded == 3000); // every known name yields an effect (default when the blob is unusable)
}

TEST (inflate_zlib)
{
    // fixed Huffman with back-references
    const std::uint8_t fixed[] = { 0x78, 0xda, 0x4b, 0x4c, 0x4a, 0x4e, 0xc4, 0x86, 0x22, 0x00, 0x7c, 0x69, 0x09, 0x89 };
    const auto a = lynx::inflateZlib (fixed, sizeof (fixed));
    CHECK (a && std::string (a->begin(), a->end()) == "abcabcabcabcabcabcabcabcX");

    // stored (uncompressed) block
    const std::uint8_t stored[] = { 0x78, 0x01, 0x01, 0x03, 0x00, 0xfc, 0xff, 'L', 'Y', 'N', 0x00, 0x00, 0x00, 0x00 };
    const auto b = lynx::inflateZlib (stored, sizeof (stored));
    CHECK (b && std::string (b->begin(), b->end()) == "LYN");

    // truncated, bad header and size limit
    CHECK (! lynx::inflateZlib (fixed, 6));
    const std::uint8_t bad[] = { 0x12, 0x34, 0x00 };
    CHECK (! lynx::inflateZlib (bad, sizeof (bad)));
    CHECK (! lynx::inflateZlib (fixed, sizeof (fixed), false, 10));
}

TEST (fl_round15_effects_convert_known_values)
{
    auto blob = [] (std::vector<std::int32_t> words) {
        std::vector<std::uint8_t> b (words.size() * 4);
        std::memcpy (b.data(), words.data(), b.size());
        return b;
    };
    // Frequency Shifter "+800 Hz": frequency 51542 in the 20 kHz range, 100% wet
    const auto fs = lynx::effectFromFl ("Frequency Shifter", blob ({ 4, 10000, 0, 51542, 0, 10000, 10000, 5000, 10000, 0, 5000, 0, 0 }));
    CHECK (fs && fs->kind == lynx::EffectKind::freqShift);
    if (fs)
    {
        const auto x = (fs->params[0] - 0.5) * 2.0;
        CHECK (std::abs (2000.0 * x * std::abs (x) - 800.0) < 10.0);
        CHECK (std::abs (fs->params[1] - 1.0f) < 1e-4f);
    }
    // Emphasis: input 0.0625 (= +1.5 dB) and ceiling 0.9308 (≈ −1.6 dB) -> limiter
    const auto em = lynx::effectFromFl ("Emphasis", blob ({ 8, 0, 0, 2, 1, 0, 0, 625, 10000, 10000, 9308, 0, 2, 7, 0 }));
    CHECK (em && em->kind == lynx::EffectKind::limiter && em->enabled);
    if (em)
    {
        CHECK (std::abs (em->params[1] * 24.0f - 1.5f) < 0.05f);
        CHECK (std::abs (em->params[0] * 12.0f - 12.0f + 1.6f) < 0.1f);
    }
    // Pitch Shifter +12 st, mix 100%
    const auto ps = lynx::effectFromFl ("Pitch Shifter", blob ({ 7, 1200, 0, 0, 2400, 8500, 200, 0, 0, 768, 0, 0, 65536, 0, 65536, 0, 0, 0, 0 }));
    CHECK (ps && ps->kind == lynx::EffectKind::pitchShift && std::abs (ps->params[0] - (0.5f + 12.0f / 48.0f)) < 1e-4f);
}

TEST (frequency_shifter_moves_spectrum_by_hz)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::freqShift);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::freqShift);
    // +200 Hz: x·|x|·2000 = 200 -> x = sqrt(0.1)
    slot.params[0] = (float) (0.5 + std::sqrt (0.1) / 2.0);
    std::vector<float> l (48000), r (48000);
    for (int i = 0; i < 48000; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = (float) std::sin (2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
    for (int done = 0; done < 48000; done += 480)
        fx->process (l.data() + done, r.data() + done, 480, slot.params, 120.0);
    auto energy = [&] (double f) {
        const auto w = 2.0 * 3.141592653589793 * f / 48000.0, k = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 9600; i < 48000; ++i)
        {
            const auto s0 = l[(std::size_t) i] + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - k * s1 * s2;
    };
    // the energy moves to 1200 Hz; little remains at 1000 and almost nothing at the image (800)
    CHECK (energy (1200.0) > 50.0 * energy (1000.0));
    CHECK (energy (1200.0) > 50.0 * energy (800.0));
}

TEST (pitch_shift_moves_frequency_keeps_length)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::pitchShift);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::pitchShift);
    auto crossingsAfter = [&] (float semitoneParam) {
        fx->reset();
        slot.params[0] = semitoneParam;
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / 48000.0);
        for (int done = 0; done < 48000; done += 512)
            fx->process (l.data() + done, r.data() + done, std::min (512, 48000 - done), slot.params, 120.0);
        int crossings = 0;
        for (int i = 9600; i < 48000; ++i)
            crossings += (l[(std::size_t) i - 1] < 0.0f) != (l[(std::size_t) i] < 0.0f);
        for (auto v : l)
            CHECK (std::isfinite (v) && std::abs (v) < 1.6f);
        return crossings / 0.8 / 2.0; // cycles per second
    };
    CHECK (std::abs (crossingsAfter (0.5f) - 220.0) < 8.0);           // 0 st
    CHECK (std::abs (crossingsAfter (0.5f + 12.0f / 48.0f) - 440.0) < 25.0); // +12 st
    CHECK (std::abs (crossingsAfter (0.5f - 12.0f / 48.0f) - 110.0) < 12.0); // −12 st
}

TEST (volume_gate_follows_bar_position)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::volumeGate);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::volumeGate);
    for (int i = 0; i < 32; ++i)
        slot.params[(std::size_t) i] = i < 16 ? 1.0f : 0.0f; // first two beats open, two closed
    slot.params[33] = slot.params[34] = 0.0f;                // no smoothing

    // 120 BPM: one beat = 0.5 s = 24000 samples; the 4-beat bar starts at tick 0
    std::vector<float> l (96000, 1.0f), r (96000, 1.0f);
    fx->setSongPosition (0.0);
    for (int done = 0; done < 96000; done += 512)
        fx->process (l.data() + done, r.data() + done, std::min (512, 96000 - done), slot.params, 120.0);
    CHECK (l[10000] > 0.99f && l[40000] > 0.99f);  // beats 1 and 2
    CHECK (l[60000] < 0.01f && l[90000] < 0.01f);  // beats 3 and 4

    // starting mid-bar (beat 3): already closed
    std::vector<float> l2 (4800, 1.0f), r2 (4800, 1.0f);
    fx->reset();
    fx->setSongPosition (2.0 * lynx::ppq);
    fx->process (l2.data(), r2.data(), 4800, slot.params, 120.0);
    CHECK (l2[4000] < 0.01f);

    // mix 0: transparent
    slot.params[32] = 0.0f;
    std::vector<float> l3 (4800, 1.0f), r3 (4800, 1.0f);
    fx->setSongPosition (2.0 * lynx::ppq);
    fx->process (l3.data(), r3.data(), 4800, slot.params, 120.0);
    CHECK (l3[4000] > 0.99f);
}

TEST (patcher_branch_heads_follow_rea)
{
    auto words = [] (std::vector<std::int32_t> v) {
        std::vector<std::uint8_t> b (v.size() * 4);
        std::memcpy (b.data(), v.data(), b.size());
        return b;
    };
    // Frequency Splitter, 2 bands, destinations 1/2/3, slope combo 3 (24 dB/oct), high send −6 dB
    std::vector<std::int32_t> fs (18, 0);
    fs[0] = 4;
    fs[1] = 32768;
    fs[3] = 3;
    fs[6] = 0;
    fs[11] = -600;
    fs[15] = 1;
    fs[16] = 2;
    fs[17] = 3;
    const auto split = words (fs);
    const auto low = lynx::patcherBranchHead ("Frequency Splitter", split, 1);
    CHECK (low.valid && low.slot && low.note.empty());
    if (low.slot)
    {
        // low band = just the Crossover low-pass, combo 3 (24 dB/oct)
        CHECK (low.slot->kind == lynx::EffectKind::crossover && low.slot->params[0] == 0.0f && low.slot->params[3] == 1.0f);
        CHECK (std::lround (low.slot->params[5] * 7.0f) == 3);
    }
    const auto mid = lynx::patcherBranchHead ("Frequency Splitter", split, 2);
    CHECK (mid.valid && mid.gain == 0.0f); // with 2 bands the mid is silent
    const auto high = lynx::patcherBranchHead ("Frequency Splitter", split, 3);
    CHECK (high.valid && std::abs (high.gain - std::pow (10.0f, -6.0f / 20.0f)) < 1e-4f);
    CHECK (! lynx::patcherBranchHead ("Frequency Splitter", split, 7).valid);

    // Stereo Shaper with identity matrix: the difference (destination 4) is zero
    const auto shaper = words ({ 3, 0, 12800, 12800, 0, 0, 0, 4, 0 });
    const auto diff = lynx::patcherBranchHead ("Fruity Stereo Shaper", shaper, 4);
    CHECK (diff.valid && diff.slot && std::abs (diff.slot->params[2] - 0.5f) < 1e-6f && std::abs (diff.slot->params[5] - 0.5f) < 1e-6f);
    CHECK (! lynx::patcherBranchHead ("Fruity Stereo Shaper", shaper, 2).valid);
}

TEST (parallel_container_sums_branches)
{
    // two branches: dry with gain 0.5 and a utility that flips phase (matrix −1) with gain 0.5 -> silence
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::parallel);
    lynx::EffectBranch dry;
    dry.gain = 0.5f;
    lynx::EffectBranch inverted;
    inverted.gain = 0.5f;
    auto flip = lynx::makeEffectSlot (lynx::EffectKind::utility);
    flip.params[2] = flip.params[5] = 0.25f; // (−1 + 2)/4
    inverted.chain.push_back (flip);
    slot.branches = { dry, inverted };
    auto fx = lynx::makeParallelEffect (slot);
    fx->prepare (48000.0);
    std::vector<float> l (9000, 0.8f), r (9000, -0.4f);
    fx->process (l.data(), r.data(), 9000, slot.params, 120.0);
    CHECK (std::abs (l[100]) < 1e-5f && std::abs (r[8999]) < 1e-5f);

    // only the dry branch with gain 2: doubles
    slot.branches = { lynx::EffectBranch { {}, 2.0f } };
    auto doubler = lynx::makeParallelEffect (slot);
    doubler->prepare (48000.0);
    std::fill (l.begin(), l.end(), 0.25f);
    doubler->process (l.data(), r.data(), 9000, slot.params, 120.0);
    CHECK (std::abs (l[5000] - 0.5f) < 1e-6f);
}

TEST (fl_controller_constant_outputs)
{
    std::vector<std::int32_t> xy { 2, 32768, 0, 65536, 0, 16384, 0, 65536, 0, 1024, 256 };
    std::vector<std::uint8_t> blob (xy.size() * 4 + 8, 0);
    std::memcpy (blob.data(), xy.data(), xy.size() * 4);
    CHECK (std::abs (lynx::controllerConstantOutput ("Fruity X-Y Controller", blob, 0).value_or (-1.0f) - 0.5f) < 1e-4f);
    CHECK (std::abs (lynx::controllerConstantOutput ("Fruity X-Y Controller", blob, 1).value_or (-1.0f) - 0.25f) < 1e-4f);

    // Control Surface: version + group { 0x836: value 0.75, -, output 2 }
    std::vector<std::uint8_t> cs (4, 0);
    cs[0] = 1;
    auto chunk = [] (std::vector<std::uint8_t>& out, std::uint32_t id, const std::vector<std::uint8_t>& body) {
        const auto size = (std::uint64_t) body.size();
        out.insert (out.end(), reinterpret_cast<const std::uint8_t*> (&id), reinterpret_cast<const std::uint8_t*> (&id) + 4);
        out.insert (out.end(), reinterpret_cast<const std::uint8_t*> (&size), reinterpret_cast<const std::uint8_t*> (&size) + 8);
        out.insert (out.end(), body.begin(), body.end());
    };
    std::vector<std::uint8_t> value (12, 0);
    const float v = 0.75f;
    const std::int32_t out = 2;
    std::memcpy (value.data(), &v, 4);
    std::memcpy (value.data() + 8, &out, 4);
    std::vector<std::uint8_t> group;
    chunk (group, 0x836, value);
    chunk (cs, 0, group);
    CHECK (std::abs (lynx::controllerConstantOutput ("Control Surface", cs, 2).value_or (-1.0f) - 0.75f) < 1e-6f);
    CHECK (! lynx::controllerConstantOutput ("Control Surface", cs, 0).has_value());
}

TEST (flp_midi_out_notes_reach_wrapper_on_same_port)
{
    auto record = [] (std::vector<std::uint8_t>& out, std::uint32_t id, std::vector<std::uint8_t> body) {
        const auto size = (std::uint64_t) body.size();
        out.insert (out.end(), reinterpret_cast<const std::uint8_t*> (&id), reinterpret_cast<const std::uint8_t*> (&id) + 4);
        out.insert (out.end(), reinterpret_cast<const std::uint8_t*> (&size), reinterpret_cast<const std::uint8_t*> (&size) + 8);
        out.insert (out.end(), body.begin(), body.end());
    };
    auto i32s = [] (std::vector<std::int32_t> v) {
        std::vector<std::uint8_t> b (v.size() * 4);
        std::memcpy (b.data(), v.data(), b.size());
        return b;
    };

    flp::Project p;
    flp::Channel midiOut;
    midiOut.index = 0;
    midiOut.type = flp::ChannelType::native;
    midiOut.pluginName = "MIDI Out";
    midiOut.pluginState = i32s ({ 7, 0, -1, -1, -1, 0, 3, 0 }); // channel 1, port 3 (@24)
    flp::Channel wrapper;
    wrapper.index = 1;
    wrapper.type = flp::ChannelType::native;
    wrapper.pluginName = "Fruity Wrapper";
    wrapper.pluginState = i32s ({ 9 });
    record (wrapper.pluginState, 0x32, i32s ({ 7 }));      // VST3
    record (wrapper.pluginState, 0x01, i32s ({ 3, -1, 0, 0, 0 })); // input port 3
    p.channels = { midiOut, wrapper };
    flp::Pattern pattern;
    pattern.index = 1;
    flp::Note note;
    note.channel = 0;
    note.key = 64;
    note.length = 96;
    pattern.notes.push_back (note);
    p.patterns.push_back (pattern);

    const auto song = lynx::importFlp (p);
    CHECK (song.channels.size() == 2 && song.channels[0].muted);
    CHECK (song.channels[1].plugin.foreign.midiInPort == 3);
    int toWrapper = 0;
    for (const auto& pat : song.patterns)
        for (const auto& n : pat.notes)
            toWrapper += n.channel == 1 && n.key == 64;
    CHECK (toWrapper == 1);
}

TEST (flp_effect_automation_follows_slots_and_params)
{
    // track 1: slot 0 with an effect that has no equivalent (dropped), slot 2 with Parametric EQ 2
    flp::Project p;
    flp::MixerInsert insert;
    insert.index = 1;
    insert.slots.push_back ({ 0, "Some Unknown FX", {} });
    insert.slots.push_back ({ 2, "Fruity Parametric EQ 2", {} });
    p.mixer.push_back (insert);

    auto clip = [&] (int index, std::uint32_t target) {
        flp::Channel c;
        c.index = index;
        c.type = flp::ChannelType::automationClip;
        c.automation = { { 0.0, 0.0 }, { 96.0, 1.0 } };
        p.channels.push_back (c);
        p.automationLinks.push_back ({ index, { target } });
    };
    const auto mixer = [] (int track, int slot, int param) { return 0x70000000u + ((std::uint32_t) track << 22) + ((std::uint32_t) slot << 16) + (std::uint32_t) param; };
    clip (0, mixer (1, 2, 0x8000 + 7)); // EQ band 1 frequency
    clip (1, mixer (1, 2, 0x1F01));     // EQ slot mix
    clip (2, mixer (1, 0, 0x8000));     // parameter of the dropped effect
    clip (3, mixer (1, 2, 0x8000 + 21)); // band type: no equivalent

    const auto song = lynx::importFlp (p);
    CHECK (song.automations.size() == 4);
    CHECK (song.mixer[1].effects.size() == 1 && song.mixer[1].effects[0].kind == lynx::EffectKind::parametricEq);

    const auto& freq = song.automations[0];
    CHECK (freq.target.kind == lynx::AutomationTargetKind::effectParam && freq.target.index == 1 && freq.target.slot == 0);
    CHECK (freq.target.param == lynx::peq::param (0, 1));
    // FL's 0..1 (20 Hz..20 kHz) becomes our frequency scale
    CHECK (std::abs (freq.points.front().value - lynx::peq::hzValue (20.0)) < 1e-4f);
    CHECK (std::abs (freq.points.back().value - lynx::peq::hzValue (20000.0)) < 1e-4f);

    CHECK (song.automations[1].target.kind == lynx::AutomationTargetKind::effectMix && song.automations[1].target.slot == 0);
    CHECK (song.automations[2].target.kind == lynx::AutomationTargetKind::none);
    CHECK (song.automations[3].target.kind == lynx::AutomationTargetKind::none);

    // the automated mix is read/written through the target
    auto copy = song;
    CHECK (copy.setTargetValue (song.automations[1].target, 0.25f) && std::abs (copy.mixer[1].effects[0].mix - 0.25f) < 1e-6f);
    CHECK (std::abs (copy.targetValue (song.automations[1].target) - 0.25f) < 1e-6f);
}

TEST (flp_real_files)
{
    if (realFlpDir.empty())
    {
        std::printf ("  (skipped: pass a folder with .flp files as argument)\n");
        return;
    }

    int files = 0, withNotes = 0, withPlaylist = 0, automations = 0, automationsWithTarget = 0, sends = 0;
    int threeOsc = 0, threeOscDecoded = 0, wrapped = 0, wrappedParsed = 0, markers = 0, signatures = 0, trackEqs = 0;
    int harmless = 0, harmlessDecoded = 0, sytrus = 0, sytrusDecoded = 0;
    std::map<std::string, std::pair<int, int>> generators; // GMS/Ogun/Autogun/Sakura/DrumSynth: channels, decoded
    int ogunTrumpet = 0; // Ogun channels whose spectrum matches the REA "Trumpet" reference
    int peqSlots = 0, peqShaped = 0, reverbs = 0, reverbsDryless = 0, reverbsLowCut = 0;
    int dwChannels = 0, dwZones = 0, fpcKits = 0, padChannels = 0, padNotes = 0;
    // import with the installation's samples (realFlpDir is ...\Data): for DirectWave/FPC
    lynx::FlpImportOptions withFiles;
    const auto flRoot = std::filesystem::path (realFlpDir).parent_path().string();
    withFiles.roots = { flRoot, realFlpDir, "" };
    withFiles.fileExists = [] (const std::string& path) {
        auto local = path;
        std::replace (local.begin(), local.end(), '\\', '/');
        return std::filesystem::exists (local);
    };
    withFiles.readFile = [] (const std::string& path) {
        auto local = path;
        std::replace (local.begin(), local.end(), '\\', '/');
        std::ifstream in (local, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    };
    int embeddedSaved = 0;
    withFiles.saveEmbedded = [&embeddedSaved] (const std::vector<std::uint8_t>& wav, const std::string&) {
        ++embeddedSaved;
        return wav.size() > 44 ? std::string ("embutido.wav") : std::string();
    };
    int slicedChannels = 0, flKeysChannels = 0;

    for (auto& entry : std::filesystem::recursive_directory_iterator (realFlpDir))
    {
        if (entry.path().extension() != ".flp")
            continue;

        std::ifstream in (entry.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        ++files;

        try
        {
            const auto p = flp::parseProject (bytes);
            CHECK (! p.version.empty());
            CHECK (p.tempo > 10.0 && p.tempo < 1000.0);

            const auto song = lynx::importFlp (p);
            CHECK (! song.patterns.empty());

            {
                const auto full = lynx::importFlp (p, withFiles);
                bool hasFpc = false;
                for (const auto& c : p.channels)
                    hasFpc = hasFpc || c.pluginName == "FPC";
                for (const auto& c : full.channels)
                {
                    flKeysChannels += c.zones.size() == 30 && c.zones.front().path.find ("CHANGPIANO") != std::string::npos ? 1 : 0;
                    if (c.sampler.slices.size() >= 4)
                    {
                        ++slicedChannels;
                        CHECK (std::is_sorted (c.sampler.slices.begin(), c.sampler.slices.end()));
                        CHECK (c.sampler.slices.back() < 1.0f);
                    }
                    if (! c.zones.empty() && c.name.find (" - ") == std::string::npos)
                    {
                        ++dwChannels;
                        dwZones += (int) c.zones.size();
                    }
                    padChannels += c.name.find (" - ") != std::string::npos && c.instrument == lynx::InstrumentKind::sampler ? 1 : 0;
                }
                fpcKits += hasFpc;
                for (const auto& pat : full.patterns)
                    for (const auto& n : pat.notes)
                    {
                        CHECK (n.channel >= 0 && n.channel < (int) full.channels.size());
                        padNotes += full.channels[(std::size_t) n.channel].name.find (" - ") != std::string::npos ? 1 : 0;
                    }
            }

            const auto peqDefaults = lynx::makeEffectSlot (lynx::EffectKind::parametricEq).params;
            for (const auto& t : song.mixer)
                for (const auto& fx : t.effects)
                {
                    if (fx.kind == lynx::EffectKind::parametricEq)
                    {
                        ++peqSlots;
                        peqShaped += fx.params != peqDefaults;
                        for (double hz : { 50.0, 1000.0, 10000.0 })
                            CHECK (std::isfinite (lynx::peq::responseDb (fx.params, 48000.0, hz)));
                    }
                    if (fx.kind == lynx::EffectKind::reverb)
                    {
                        ++reverbs;
                        reverbsDryless += fx.params[4] < 0.01f;
                        reverbsLowCut += fx.params[5] > 0.0f;
                    }
                }

            for (const auto& t : song.mixer)
                if (t.eq.active())
                {
                    ++trackEqs;
                    for (std::size_t b = 0; b < 3; ++b)
                        CHECK (t.eq.hz[b] >= 10.0f && t.eq.hz[b] <= 16000.0f && std::abs (t.eq.gainDb[b]) <= 18.0f);
                }

            for (const auto& a : song.arrangements)
                for (const auto& m : a.markers)
                {
                    ++markers;
                    signatures += m.isSignature();
                    CHECK (m.tick >= 0);
                }

            // Fruity Wrapper (channels and mixer slots): every blob in the demos is read
            auto countWrapper = [&] (const std::string& plugin, const std::vector<std::uint8_t>& state) {
                if (plugin != "Fruity Wrapper")
                    return;
                ++wrapped;
                lynx::ForeignPlugin f;
                wrappedParsed += lynx::parseFlWrapper (state, f) && f.format != lynx::ForeignPlugin::Format::none && ! f.name.empty();
            };
            for (const auto& c : p.channels)
                countWrapper (c.pluginName, c.pluginState);
            for (const auto& insert : p.mixer)
                for (const auto& slot : insert.slots)
                    countWrapper (slot.plugin, slot.state);

            // Harmless: every state in the demos becomes a synth, with finite values
            for (const auto& c : p.channels)
                if (c.pluginName == "Harmless")
                {
                    ++harmless;
                    lynx::SynthSettings hs;
                    float g = 1.0f;
                    if (lynx::synthFromHarmless (c.pluginState, 128.0, hs, g))
                    {
                        ++harmlessDecoded;
                        CHECK (std::isfinite (g) && hs.cutoff >= 20.0f && hs.cutoff <= 20000.0f && std::isfinite (hs.decay) && std::isfinite (hs.release));
                    }
                }

            // Sytrus: every state in the demos (zlib + 10 units) decodes, with finite values
            for (const auto& c : p.channels)
                if (c.pluginName == "Sytrus")
                {
                    ++sytrus;
                    lynx::SynthSettings ss;
                    float g = 1.0f;
                    if (lynx::synthFromSytrus (c.pluginState, 128.0, ss, g))
                    {
                        ++sytrusDecoded;
                        CHECK (std::isfinite (g) && ss.cutoff >= 20.0f && ss.cutoff <= 20000.0f && std::isfinite (ss.attack) && std::isfinite (ss.decay)
                               && std::isfinite (ss.release) && ss.sustain >= 0.0f && ss.sustain <= 1.0f && std::isfinite (ss.filterEnv.amount));
                        for (const auto& o : ss.osc)
                            CHECK (o.level >= 0.0f && o.level <= 1.0f && o.octave >= -3 && o.octave <= 3);
                    }
                }

            // round 12 generators
            for (const auto& c : p.channels)
            {
                lynx::SynthSettings gs;
                float g = 1.0f;
                int decoded = -1;
                if (c.pluginName == "Harmor")
                {
                    decoded = lynx::synthFromHarmor (c.pluginState, gs, g);
                    if (decoded)
                    {
                        // REA_rodada25 §4: most resonant settings stay below Q ≈ 3, a few saturate
                        CHECK (gs.resonance >= 0.0f && gs.resonance <= 1.0f);
                        static int resonant = 0, strong = 0, total = 0;
                        resonant += gs.resonance > 0.0f;
                        strong += gs.resonance > 0.9f;
                        if (++total % 50 == 0)
                            std::printf ("  Harmor resonance: %d with a peak, %d with Q > ~5 (of %d)\n", resonant, strong, total);
                    }
                    static int saw = 0, square = 0, other = 0, total = 0;
                    if (decoded && std::any_of (gs.osc.begin(), gs.osc.end(), [] (const lynx::SynthOscillator& o) { return o.wave == lynx::Waveform::spectral; }))
                    {
                        auto isSaw = true, isSquare = true;
                        for (int n = 1; n <= 16; ++n)
                        {
                            isSaw = isSaw && std::abs (gs.spectrum[(std::size_t) n - 1] - 1.0f / n) < 1e-3f;
                            isSquare = isSquare && std::abs (gs.spectrum[(std::size_t) n - 1] - (n % 2 == 1 ? 1.1535f / n : 0.0f)) < 2e-3f;
                        }
                        ++(isSaw ? saw : isSquare ? square : other);
                        if (++total % 20 == 0 || total == 100)
                            std::printf ("  Harmor spectrum: %d saw, %d square, %d other (of %d)\n", saw, square, other, total);
                    }
                }
                else if (c.pluginName == "GMS")
                    decoded = lynx::synthFromGms (c.pluginState, 128.0, gs, g);
                else if (c.pluginName == "Ogun" || c.pluginName == "Autogun")
                {
                    decoded = lynx::synthFromOgun (c.pluginState, gs, g);
                    // REA_rodada24 §1.9: the "Trumpet" preset (richness 7, no seeds) has partials 0.9087, 0.8610,
                    // 0.8151… at the fixed seed-3 phases 0.919, 0.380, 0.130
                    if (decoded && gs.osc[0].wave == lynx::Waveform::spectral)
                    {
                        CHECK (std::all_of (gs.spectrum.begin(), gs.spectrum.end(), [] (float a) { return std::isfinite (a) && a >= 0.0f; }));
                        static constexpr float trumpet[3] { 0.9087f, 0.8610f, 0.8151f }, phases[3] { 0.919f, 0.380f, 0.130f };
                        auto match = true;
                        for (std::size_t k = 0; k < 3; ++k)
                            match = match && std::abs (gs.spectrum[k] * 2.0f / 3.14159265f - trumpet[k]) < 1e-3f && std::abs (gs.spectrumPhase[k] - phases[k]) < 1e-3f;
                        ogunTrumpet += match ? 1 : 0;
                    }
                }
                else if (c.pluginName == "Sakura")
                    decoded = lynx::synthFromSakura (c.pluginState, gs);
                else if (c.pluginName == "Kepler")
                    decoded = lynx::synthFromKepler (c.pluginState, gs, g);
                else if (c.pluginName == "Wave Traveller")
                    decoded = lynx::waveTravellerSample (c.pluginState).has_value() ? 2 : 0;
                else if (c.pluginName == "PoiZone")
                    decoded = lynx::synthFromPoiZone (c.pluginState, gs, g);
                else if (c.pluginName == "BassDrum")
                    decoded = lynx::synthFromBassDrum (c.pluginState, gs, g);
                else if (c.pluginName == "Toxic Biohazard")
                    decoded = lynx::synthFromToxic (c.pluginState, gs, g);
                else if (c.pluginName == "Morphine")
                    decoded = lynx::synthFromMorphine (c.pluginState, gs, g);
                else if (c.pluginName == "BooBass")
                    decoded = lynx::synthFromBooBass (c.pluginState, gs);
                else if (c.pluginName == "Drumpad" || c.pluginName == "DrumPad")
                    decoded = lynx::drumpadPatchName (c.pluginState).has_value() ? 2 : 0;
                else if (c.pluginName == "Fruity Envelope Controller" && c.pluginState.size() >= 4 && c.pluginState[2] >= 6) // v >= 6
                    decoded = lynx::parseEnvelopeController (c.pluginState).has_value() ? 2 : 0;
                else if (c.pluginName == "FLEX")
                    decoded = lynx::flexPresetName (c.pluginState).has_value() ? 2 : 0;
                else if (c.pluginName == "Fruity DrumSynth Live")
                {
                    const auto sound = lynx::drumSynthLiveSound (c.pluginState);
                    decoded = sound && ! lynx::drumSampleCandidates (*sound).empty();
                }
                if (decoded < 0)
                    continue;
                auto& counter = generators[c.pluginName];
                ++counter.first;
                counter.second += decoded != 0;
                if (decoded == 1 && c.pluginName != "Fruity DrumSynth Live")
                {
                    CHECK (std::isfinite (g) && g >= 0.0f && gs.cutoff >= 20.0f && gs.cutoff <= 20000.0f && gs.sustain >= 0.0f && gs.sustain <= 1.0f);
                    CHECK (std::isfinite (gs.attack) && std::isfinite (gs.decay) && std::isfinite (gs.release) && std::isfinite (gs.filterEnv.amount) && std::isfinite (gs.lfo[lynx::lfoCutoff].amount));
                    for (const auto& o : gs.osc)
                        CHECK (o.level >= 0.0f && o.level <= 4.0f && std::isfinite (o.detune) && std::isfinite (o.pan));
                }
            }

            // 3x Osc: every state in the demos decodes, with levels summing to at most 1
            for (const auto& c : p.channels)
                if (c.pluginName == "3x Osc")
                {
                    ++threeOsc;
                    lynx::SynthSettings s;

                    if (lynx::synthFrom3xOsc (c.pluginState, s))
                    {
                        ++threeOscDecoded;
                        // with AM, osc 3 is the modulation depth and is left out of the sum
                        const auto sum = s.osc[0].level + s.osc[1].level + (s.am ? 0.0f : s.osc[2].level);
                        CHECK (sum <= 1.001f && s.osc[2].level <= 1.0f);
                        for (const auto& o : s.osc)
                            CHECK (o.octave >= -2 && o.octave <= 2 && std::abs (o.semitones) <= 11 && std::abs (o.pan) <= 1.0f);
                    }
                }

            for (auto& pat : p.patterns)
                if (! pat.notes.empty())
                {
                    ++withNotes;
                    break;
                }

            for (auto& a : p.arrangements)
                for (auto& item : a.items)
                    CHECK (item.track >= 0 && item.track < 500);

            for (auto& clip : song.playlist)
            {
                CHECK (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size());
                CHECK (clip.length > 0 && clip.offset >= 0);

                if (clip.kind == lynx::ClipKind::automation)
                    CHECK (clip.automation >= 0 && clip.automation < (int) song.automations.size());
            }

            for (auto& a : song.automations)
            {
                ++automations;
                automationsWithTarget += a.target.kind != lynx::AutomationTargetKind::none;
                CHECK (a.isLive() || ! a.points.empty());

                for (std::size_t i = 1; i < a.points.size(); ++i)
                    CHECK (a.points[i].tick >= a.points[i - 1].tick);
            }

            // imported routing has no cycles and valid destinations
            CHECK (song.mixerOrder().size() + 1 == song.mixer.size());
            for (auto& t : song.mixer)
                for (auto& s : t.sends)
                {
                    ++sends;
                    CHECK (s.target > 0 && s.target < (int) song.mixer.size());
                }

            if (! song.playlist.empty())
                ++withPlaylist;
        }
        catch (const flp::Error& e)
        {
            ++failures;
            std::printf ("  FAILED %s: %s\n", entry.path().string().c_str(), e.what());
        }
    }

    CHECK (threeOscDecoded == threeOsc);
    std::printf ("  Harmless: %d channels, %d decoded\n", harmless, harmlessDecoded);
    std::printf ("  Sytrus: %d channels, %d decoded\n", sytrus, sytrusDecoded);
    for (const auto& [name, counter] : generators)
    {
        std::printf ("  %s: %d channels, %d decoded\n", name.c_str(), counter.first, counter.second);
        CHECK (counter.second == counter.first);
    }
    std::printf ("  Ogun spectra matching the Trumpet reference: %d\n", ogunTrumpet);
    CHECK (ogunTrumpet >= 1);

    // demo Gross Beat: the volume slot becomes the gate (those using an envelope slot get steps < 1)
    {
        int vocodexes = 0, vocodexSides = 0;
        int lovePhilterLinks[4] {}; // live links: cutoff, resonance; inside containers: cutoff, resonance
        int embeddedZones = 0, lovePhilters = 0, lovePhiltersRead = 0, lovePhiltersConverted = 0, gates = 0, shaped = 0, effectors = 0, patchers = 0, serial = 0, parallel = 0, generatorPatchers = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Demo projects"))
            if (entry.path().extension() == ".flp")
            {
                std::ifstream in (entry.path(), std::ios::binary);
                const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
                try
                {
                    const auto project = flp::parseProject (file);
                    for (const auto& insert : project.mixer)
                        for (const auto& slot : insert.slots)
                            if (slot.plugin == "Gross Beat")
                            {
                                const auto fx = lynx::effectFromFl (slot.plugin, slot.state);
                                CHECK (fx && fx->kind == lynx::EffectKind::volumeGate);
                                ++gates;
                                if (fx && std::any_of (fx->params.begin(), fx->params.begin() + 32, [] (float v) { return v < 0.99f; }))
                                    ++shaped;
                            }
                            else if (slot.plugin == "Fruity Love Philter")
                            {
                                ++lovePhilters;
                                lovePhiltersRead += lynx::parseLovePhilter (slot.state).has_value();
                                if (const auto conv = lynx::lovePhilterEffects (slot.state))
                                {
                                    ++lovePhiltersConverted;
                                    CHECK (! conv->slots.empty());
                                    for (const auto& fx : conv->slots)
                                        for (auto v : fx.params)
                                            CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
                                    for (const auto& fx : conv->slots)
                                        for (const auto& link : fx.links)
                                        {
                                            const auto& chain = fx.branches[(std::size_t) link.branch].chain;
                                            CHECK (link.source < (int) chain.size() && chain[(std::size_t) link.target].kind == lynx::EffectKind::parametricEq);
                                            lovePhilterLinks[2 + (link.param == lynx::peq::param (0, 3) ? 1 : 0)]++;
                                        }
                                    for (const auto& link : conv->links)
                                    {
                                        lovePhilterLinks[link.param == lynx::peq::param (0, 3) ? 1 : 0]++;
                                        CHECK (link.curve.size() == 33);
                                        for (auto v : link.curve)
                                            CHECK (std::isfinite (v) && v >= 0.0f && v <= 1.0f);
                                    }
                                }
                            }
                            else if (slot.plugin == "Vocodex")
                            {
                                const auto v = lynx::vocodexEffect (slot.state);
                                CHECK (v.has_value());
                                vocodexes += v.has_value();
                                vocodexSides += v && (v->modIn > 0 || v->carIn > 0);
                            }
                            else if (slot.plugin == "Effector")
                                effectors += lynx::effectFromFl (slot.plugin, slot.state).has_value();
                            else if (slot.plugin == "Patcher")
                            {
                                const auto graph = lynx::parsePatcher (slot.state);
                                CHECK (graph.has_value());
                                ++patchers;
                                if (graph && ! graph->serialChain.empty())
                                    ++serial;
                                if (graph && graph->serialChain.empty() && graph->audio)
                                    ++parallel;
                            }
                    for (const auto& c : project.channels)
                        if (c.pluginName == "DirectWave")
                            if (const auto program = lynx::directWaveProgram (c.pluginState))
                                for (const auto& wav : program->embeddedWav)
                                    if (! wav.empty())
                                    {
                                        ++embeddedZones;
                                        CHECK (wav.size() > 44 && std::memcmp (wav.data(), "RIFF", 4) == 0);
                                    }
                    for (const auto& c : project.channels)
                        if (c.pluginName == "Patcher")
                        {
                            const auto graph = lynx::parsePatcher (c.pluginState);
                            CHECK (graph.has_value());
                            generatorPatchers += graph && graph->mainGenerator >= 0;
                        }
                }
                catch (const flp::Error&) {}
            }
        std::printf ("  Gross Beat: %d slots, %d with gate; Effector: %d converted; Love Philter: %d, %d read\n", gates, shaped, effectors,
                     lovePhilters, lovePhiltersRead);
        CHECK (lovePhiltersRead == lovePhilters && lovePhiltersConverted == lovePhilters);
        std::printf ("  Vocodex: %d converted, %d with side inputs\n", vocodexes, vocodexSides);
        CHECK (vocodexes >= 9);
        std::printf ("  Love Philter live links: %d cutoff, %d resonance; in multi-unit containers: %d cutoff, %d resonance\n",
                     lovePhilterLinks[0], lovePhilterLinks[1], lovePhilterLinks[2], lovePhilterLinks[3]);
        CHECK (lovePhilterLinks[2] + lovePhilterLinks[3] > 0);
        std::printf ("  DirectWave: %d zones with embedded audio\n", embeddedZones);
        CHECK (embeddedZones == 12);
        std::printf ("  Patcher: %d in the mixer (%d serial, %d reduced to serial/parallel), %d as instrument with generator\n", patchers, serial,
                     parallel, generatorPatchers);
        CHECK (gates == 0 || shaped > 0);
    }

    // Envelope Controller "Basic Sidechain" (Future House): drops on the note and returns to ~1 within ~1/3 s
    {
        const auto path = std::filesystem::path (realFlpDir) / "Demo projects" / "Demo songs" / "Asher Postman - Future House.flp";
        if (std::filesystem::exists (path))
        {
            std::ifstream in (path, std::ios::binary);
            const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
            const auto song = lynx::importFlp (flp::parseProject (file));
            const lynx::ChannelDef* controller = nullptr;
            for (const auto& c : song.channels)
                if (c.instrument == lynx::InstrumentKind::controller)
                    controller = &c;
            CHECK (controller != nullptr);
            if (controller != nullptr)
            {
                auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
                inst->prepare (48000.0);
                inst->configure (*controller, song.bpm);
                std::vector<float> l (480), r (480);
                inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
                inst->noteOn (60, 1.0f);
                inst->render (l.data(), r.data(), 480, 1.0f, 1.0f); // 10 ms
                const auto ducked = inst->controlOutput (0);
                for (int i = 0; i < 100; ++i) // +1 s
                    inst->render (l.data(), r.data(), 480, 1.0f, 1.0f);
                const auto recovered = inst->controlOutput (0);
                std::printf ("  Envelope Controller (Basic Sidechain): %.2f on the note, %.2f after 1 s\n", ducked, recovered);
                CHECK (ducked < 0.3f && recovered > 0.9f);
            }
        }
    }

    // mutation fuzz: real states with flipped and truncated bytes must not crash the readers
    {
        std::map<std::string, std::vector<std::uint8_t>> samples; // one real blob per plugin
        for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Demo projects"))
        {
            if (entry.path().extension() != ".flp")
                continue;
            std::ifstream in (entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
            try
            {
                const auto project = flp::parseProject (file);
                for (const auto& c : project.channels)
                    if (! c.pluginState.empty())
                        samples.emplace (c.pluginName, c.pluginState);
                for (const auto& insert : project.mixer)
                    for (const auto& slot : insert.slots)
                        if (! slot.state.empty())
                            samples.emplace (slot.plugin, slot.state);
            }
            catch (const flp::Error&) {}
        }

        std::uint32_t seed = 12345;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        int mutations = 0;
        for (const auto& [name, original] : samples)
            for (int round = 0; round < 40; ++round)
            {
                auto blob = original;
                if (round % 4 == 3 && ! blob.empty())
                    blob.resize (rnd() % blob.size()); // truncated
                else
                    for (int k = 0; k < 1 + (int) (rnd() % 8) && ! blob.empty(); ++k)
                        blob[rnd() % blob.size()] = (std::uint8_t) rnd();
                ++mutations;
                lynx::SynthSettings synth;
                float gain = 1.0f;
                lynx::synthFromSytrus (blob, 120.0, synth, gain);
                lynx::synthFromHarmor (blob, synth, gain);
                lynx::synthFromGms (blob, 120.0, synth, gain);
                lynx::synthFromOgun (blob, synth, gain);
                lynx::synthFromMorphine (blob, synth, gain);
                lynx::synthFromKepler (blob, synth, gain);
                lynx::synthFromPoiZone (blob, synth, gain);
                lynx::synthFromToxic (blob, synth, gain);
                lynx::synthFromBassDrum (blob, synth, gain);
                lynx::synthFromHarmless (blob, 120.0, synth, gain);
                lynx::parseEnvelopeController (blob);
                lynx::parseKeyboardController (blob);
                lynx::parseLovePhilter (blob);
                lynx::parsePatcher (blob);
                lynx::effectFromFl (name, blob);
                lynx::fpcPads (blob);
                lynx::directWaveProgram (blob);
                CHECK (std::isfinite (gain) && std::isfinite (synth.cutoff));
            }
        std::printf ("  mutation fuzz: %d blobs from %d plugins\n", mutations, (int) samples.size());
    }

    // WavPack (.wv) samples from FL packs: all decode, with finite, non-silent audio
    {
        int files = 0, decoded = 0, audible = 0;
        double seconds = 0.0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator (std::filesystem::path (realFlpDir) / "Patches" / "Packs"))
            if (entry.path().extension() == ".wv")
            {
                std::ifstream in (entry.path(), std::ios::binary);
                const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
                ++files;
                lynx::DecodedAudio audio;
                if (! lynx::decodeWavPack (file.data(), file.size(), audio))
                    continue;
                ++decoded;
                float peak = 0.0f;
                bool finite = true;
                for (const auto& ch : audio.channels)
                    for (auto v : ch)
                    {
                        finite = finite && std::isfinite (v);
                        peak = std::max (peak, std::abs (v));
                    }
                CHECK (finite && peak <= 4.0f);
                audible += peak > 1e-4f;
                seconds += (double) audio.channels[0].size() / audio.sampleRate;
            }
        std::printf ("  WavPack: %d .wv files, %d decoded, %d audible, %.0f s of audio\n", files, decoded, audible, seconds);
        CHECK (decoded == files);
    }

    // native GMS presets (.gmsynth = the contents of 0xD5 without the "SSLF")
    int gmsPresets = 0, gmsDecoded = 0;
    const auto gmsFolder = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / "GMS";
    if (std::filesystem::exists (gmsFolder))
        for (const auto& entry : std::filesystem::recursive_directory_iterator (gmsFolder))
            if (entry.path().extension() == ".gmsynth")
            {
                std::ifstream in (entry.path(), std::ios::binary);
                std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
                std::vector<std::uint8_t> blob { 'S', 'S', 'L', 'F' };
                const auto n = (std::uint32_t) file.size();
                blob.insert (blob.end(), reinterpret_cast<const std::uint8_t*> (&n), reinterpret_cast<const std::uint8_t*> (&n) + 4);
                blob.insert (blob.end(), file.begin(), file.end());
                lynx::SynthSettings gs;
                float g = 1.0f;
                ++gmsPresets;
                if (lynx::synthFromGms (blob, 120.0, gs, g))
                {
                    ++gmsDecoded;
                    CHECK (std::isfinite (g) && gs.cutoff >= 20.0f && std::isfinite (gs.pitchEnv.amount) && std::isfinite (gs.lfo[lynx::lfoPitch].period));
                }
            }
    std::printf ("  GMS presets: %d, %d decoded\n", gmsPresets, gmsDecoded);

    // the synth's "FL presets" button: Sytrus/Harmor .fst and .gmsynth become synth settings
    int presetFiles = 0, presetsRead = 0;
    for (const auto* name : { "Sytrus", "Harmor", "GMS", "Harmless", "Ogun", "Sakura", "Fruit Kick", "Fruity DX10", "MiniSynth", "Kepler",
                              "PoiZone", "Toxic Biohazard", "BassDrum" })
    {
        const auto folder = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Generators" / name;
        if (! std::filesystem::exists (folder))
            continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator (folder))
            if (entry.path().extension() == ".fst" || entry.path().extension() == ".gmsynth")
            {
                std::ifstream in (entry.path(), std::ios::binary);
                const std::vector<std::uint8_t> file ((std::istreambuf_iterator<char> (in)), {});
                ++presetFiles;
                presetsRead += lynx::synthFromFlPreset (file).has_value();
            }
    }
    std::printf ("  FL synth presets: %d files, %d read\n", presetFiles, presetsRead);
    CHECK (presetsRead >= presetFiles * 95 / 100);
    CHECK (gmsDecoded == gmsPresets);
    CHECK (sytrusDecoded == sytrus);
    CHECK (harmlessDecoded == harmless && harmless > 100);

    // 3x Osc channel presets shipped with FL (.fst): all become synth settings
    int presets = 0, presetsDecoded = 0;
    const auto presetDir = std::filesystem::path (realFlpDir) / "Patches" / "Channel presets" / "3x Osc";
    if (std::filesystem::is_directory (presetDir))
        for (auto& entry : std::filesystem::directory_iterator (presetDir))
            if (entry.path().extension() == ".fst")
            {
                ++presets;
                std::ifstream in (entry.path(), std::ios::binary);
                std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
                if (auto synth = lynx::synthFromFlPreset (bytes))
                {
                    ++presetsDecoded;
                    CHECK (synth->osc[0].level + synth->osc[1].level + (synth->am ? 0.0f : synth->osc[2].level) <= 1.001f);
                }
            }
    std::printf ("  %d 3x Osc presets, %d read\n", presets, presetsDecoded);
    CHECK (presetsDecoded == presets);

    // FL effect presets that become built-in effects
    int fxPresets = 0, fxDecoded = 0;
    for (auto kind : { lynx::EffectKind::parametricEq, lynx::EffectKind::limiter, lynx::EffectKind::reverb, lynx::EffectKind::delay,
                       lynx::EffectKind::utility })
        for (const auto& plugin : lynx::flPluginsForEffect (kind))
        {
            const auto dir = std::filesystem::path (realFlpDir) / "Patches" / "Plugin presets" / "Effects" / plugin;
            if (! std::filesystem::is_directory (dir))
                continue;
            for (auto& entry : std::filesystem::recursive_directory_iterator (dir))
                if (entry.path().extension() == ".fst")
                {
                    ++fxPresets;
                    std::ifstream in (entry.path(), std::ios::binary);
                    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
                    fxDecoded += lynx::effectFromFlPreset (bytes).has_value();
                }
        }
    std::printf ("  %d FL effect presets, %d read\n", fxPresets, fxDecoded);
    CHECK (fxDecoded == fxPresets);
    CHECK (wrappedParsed == wrapped);
    std::printf ("  %d plugins in Fruity Wrapper, %d read\n", wrapped, wrappedParsed);
    std::printf ("  %d markers, %d time signature changes, %d tracks with EQ\n", markers, signatures, trackEqs);
    std::printf ("  %d parametric EQs (%d with FL settings), %d reverbs (%d without dry)\n", peqSlots, peqShaped, reverbs, reverbsDryless);
    std::printf ("  DirectWave: %d channels, %d zones; FPC: %d projects, %d pad channels, %d pad notes\n", dwChannels, dwZones, fpcKits,
                 padChannels, padNotes);
    CHECK (dwZones > 1000 && padChannels > 250 && padNotes > 500);
    std::printf ("  sliced channels (Slicer/Slicex): %d; embedded WAVs saved: %d\n", slicedChannels, embeddedSaved);
    CHECK (slicedChannels >= 25 && embeddedSaved >= 10); // without decoding Ogg here, some loops have no duration
    std::printf ("  FL Keys (Grand Piano, 30 zones): %d channels\n", flKeysChannels);
    CHECK (flKeysChannels >= 8);
    CHECK (peqSlots > 1000 && peqShaped > 900 && reverbsDryless > 20 && reverbsLowCut > 250);
    CHECK (markers > 300 && signatures > 60);
    std::printf ("  %d 3x Osc channels, %d decoded\n", threeOsc, threeOscDecoded);
    std::printf ("  %d .flp files read, %d with notes, %d with playlist, %d automations (%d with target), %d sends\n",
                 files, withNotes, withPlaylist, automations, automationsWithTarget, sends);
    CHECK (files > 0);
    CHECK (automations > 0 && sends > 0);
}

// ---------------------------------------------------------------------------
// Sequencer

TEST (pattern_toggle_step_and_length)
{
    lynx::Pattern p;
    CHECK (! p.hasStep (0, 4));
    p.toggleStep (0, 4);
    CHECK (p.hasStep (0, 4));
    CHECK (p.notes.size() == 1 && p.notes[0].start == 4 * lynx::ticksPerStep);
    CHECK (p.effectiveLength (4) == 4 * lynx::ppq); // one bar
    p.toggleStep (1, 20);                            // second bar
    CHECK (p.effectiveLength (4) == 8 * lynx::ppq);
    p.toggleStep (0, 4);
    CHECK (! p.hasStep (0, 4) && p.notes.size() == 1);
}

TEST (scheduler_places_events_at_exact_samples)
{
    lynx::Pattern p;
    p.toggleStep (0, 0);
    p.toggleStep (0, 4); // tick 960 = 1 beat

    const double sr = 48000.0, bpm = 120.0; // 1 beat = 0.5 s = 24000 samples
    const auto tps = lynx::ticksPerSample (bpm, sr);
    lynx::EventBuffer ev;

    lynx::schedulePatternLoop (p, p.effectiveLength (4), 23900.0 * tps, 512, tps, ev);
    // the beat-2 note starts at sample 24000 -> offset 100 in this block
    int ons = 0;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on)
        {
            ++ons;
            CHECK (e.sampleOffset == 100);
        }
    CHECK (ons == 1);
}

TEST (scheduler_wraps_the_loop)
{
    lynx::Pattern p;
    p.toggleStep (2, 0);
    const auto len = p.effectiveLength (4); // 3840 ticks
    const double sr = 48000.0;
    const auto tps = lynx::ticksPerSample (120.0, sr);
    lynx::EventBuffer ev;

    // block that crosses the loop end: starts 10 samples before
    const auto start = (double) len - 10.0 * tps;
    lynx::schedulePatternLoop (p, len, start, 64, tps, ev);

    bool found = false;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on && e.channel == 2)
        {
            found = true;
            CHECK (e.sampleOffset == 10);
        }
    CHECK (found);
}

TEST (scheduler_off_before_on_at_same_sample)
{
    lynx::Pattern p;
    lynx::Note a;
    a.start = 0; a.length = 240; a.channel = 0;
    lynx::Note b = a;
    b.start = 240;
    p.notes = { a, b };
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::EventBuffer ev;
    lynx::schedulePatternLoop (p, p.effectiveLength (4), 200.0, 4000, tps, ev);

    CHECK (ev.size() >= 2);
    CHECK (ev[0].type == lynx::NoteEvent::Type::off);
    CHECK (ev[1].type == lynx::NoteEvent::Type::on);
    CHECK (ev[0].sampleOffset == ev[1].sampleOffset);
}

TEST (snapshot_exchange_hands_over_and_recycles)
{
    struct Counted
    {
        int value;
        int* alive;
        Counted (int v, int* a) : value (v), alive (a) { ++*alive; }
        ~Counted() { --*alive; }
    };

    int alive = 0;
    {
        lynx::SnapshotExchange<Counted> x;
        CHECK (x.acquire() == nullptr);

        x.publish (std::make_unique<Counted> (1, &alive));
        CHECK (x.acquire()->value == 1);

        x.publish (std::make_unique<Counted> (2, &alive));
        x.publish (std::make_unique<Counted> (3, &alive)); // 2 was never read: freed immediately
        CHECK (alive == 2);
        CHECK (x.acquire()->value == 3);
        CHECK (alive == 2); // 1 was returned, but only the UI frees it
        x.collectGarbage();
        CHECK (alive == 1);

        // without collection, the return slots fill up and the audio side holds on to the current one
        for (int i = 0; i < 20; ++i)
        {
            x.publish (std::make_unique<Counted> (10 + i, &alive));
            CHECK (x.acquire() != nullptr);
        }
    }
    CHECK (alive == 0);
}

TEST (rack_renderer_makes_sound_and_reuses_instruments)
{
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    const double sr = 48000.0;
    auto state = lynx::RackState::build (song, nullptr, sr);

    lynx::RackRenderer r;
    r.prepare (sr);
    std::vector<float> l (512), rr (512);

    double tick = 0.0;
    float peak = 0.0f;
    int kicks = 0;

    for (int block = 0; block < 93; ++block) // 47616 samples: just before 1 s
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        for (auto& e : r.lastEvents())
            if (e.type == lynx::NoteEvent::Type::on && e.channel == 0)
                ++kicks;
        for (int i = 0; i < 512; ++i)
        {
            CHECK (std::isfinite (l[(std::size_t) i]));
            peak = std::max (peak, std::abs (l[(std::size_t) i]));
        }
        tick += 512 * lynx::ticksPerSample (song.bpm, sr);
    }

    CHECK (peak > 0.1f && peak < 4.0f);
    CHECK (kicks == 2); // 120 BPM: beats at 0 and 0.5 s

    // editing the song keeps the instruments of channels whose type didn't change
    song.channels[4].instrument = lynx::InstrumentKind::kick;
    auto next = lynx::RackState::build (song, state.get(), sr);
    CHECK (next->instruments[0] == state->instruments[0]);
    CHECK (next->instruments[4] != state->instruments[4]);

    // stopping silences the synth and triggers no new notes
    r.process (next.get(), false, tick, 512, l.data(), rr.data());
    CHECK (r.lastEvents().size() == 0);
}

TEST (flp_import_maps_channels_and_rescales)
{
    flp::Project p;
    p.header.ppq = 96;
    p.tempo = 150.0;
    p.channels = { { 0, flp::ChannelType::sampler, "Kick 808", "", "", 1 },
                   { 1, flp::ChannelType::audioClip, "Vocal", "", "", 2 },
                   { 2, flp::ChannelType::instrument, "Lead", "FLEX", "", 3 } };
    flp::Pattern pat;
    pat.index = 1;
    flp::Note n;
    n.position = 96;
    n.length = 0;
    n.channel = 2;
    n.key = 72;
    n.velocity = 64;
    pat.notes.push_back (n);
    p.patterns.push_back (pat);

    const auto s = lynx::importFlp (p);
    CHECK (s.bpm == 150.0);
    CHECK (s.channels.size() == 3); // audio clip channels stay in the rack, as in FL
    CHECK (s.channels[0].instrument == lynx::InstrumentKind::kick);
    CHECK (s.channels[1].instrument == lynx::InstrumentKind::audioClip);
    CHECK (s.channels[2].instrument == lynx::InstrumentKind::synth);
    CHECK (s.patterns.size() == 1);
    CHECK (s.patterns[0].notes[0].start == lynx::ppq);           // 96 @ PPQ 96 = 1 beat
    CHECK (s.patterns[0].notes[0].channel == 2);
    CHECK (s.patterns[0].notes[0].length == lynx::ticksPerStep); // step note
    CHECK (std::abs (s.patterns[0].notes[0].velocity - 0.5f) < 1e-6f);
}

TEST (live_queue_is_fifo_and_bounded)
{
    lynx::LiveEventQueue q;
    lynx::NoteEvent e;
    CHECK (! q.pop (e));

    for (int i = 0; i < 10; ++i)
    {
        lynx::NoteEvent in;
        in.key = i;
        CHECK (q.push (in));
    }

    for (int i = 0; i < 10; ++i)
        CHECK (q.pop (e) && e.key == i);

    int pushed = 0;
    while (q.push (e))
        ++pushed;
    CHECK (pushed == (int) lynx::LiveEventQueue::capacity - 1); // one slot stays free to tell full from empty
}

TEST (sampler_plays_pitched_one_shot)
{
    // 1000-frame ramp sample: the position can be measured from the amplitude
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    for (int i = 0; i < 1000; ++i)
        data->left.push_back (1.0f);

    auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    sampler->prepare (48000.0);
    std::vector<float> l (2048, 0.0f), r (2048, 0.0f);

    sampler->noteOn (60, 1.0f);
    sampler->noteOff (60); // one-shot: noteOff doesn't cut
    sampler->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    int sounding = 0;
    for (auto s : l)
        sounding += s > 0.5f;
    CHECK (sounding >= 995 && sounding <= 1000); // plays the whole sample at the original pitch

    std::fill (l.begin(), l.end(), 0.0f);
    sampler->noteOn (72, 1.0f); // one octave up: half the duration
    sampler->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    sounding = 0;
    for (auto s : l)
        sounding += s > 0.5f;
    CHECK (sounding >= 495 && sounding <= 500);
    CHECK (r[10] > 0.5f); // mono goes to both sides

    auto silent = lynx::makeInstrument (lynx::InstrumentKind::sampler, nullptr);
    silent->prepare (48000.0);
    silent->noteOn (60, 1.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    silent->render (l.data(), r.data(), 64, 1.0f, 1.0f);
    CHECK (l[0] == 0.0f);
}

TEST (rack_live_notes_and_sample_provider)
{
    lynx::Song song;
    lynx::ChannelDef c;
    c.name = "Smp";
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "a.wav";
    song.channels.push_back (c);
    song.patterns.emplace_back();

    int loads = 0;
    auto provider = [&] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        ++loads;
        CHECK (path == "a.wav");
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (4800, 0.5f);
        return d;
    };

    auto state = lynx::RackState::build (song, nullptr, 48000.0, provider);
    CHECK (loads == 1);

    lynx::RackRenderer renderer;
    renderer.prepare (48000.0);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0;
    on.velocity = 1.0f;
    live.push (on);

    std::vector<float> l (256), r (256);
    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live); // stopped, but the live note plays
    CHECK (l[0] > 0.1f);

    // same sample: reused; different sample: reloaded
    auto same = lynx::RackState::build (song, state.get(), 48000.0, provider);
    CHECK (loads == 1 && same->instruments[0] == state->instruments[0]);
    song.channels[0].samplePath = "b.wav";
    auto other = lynx::RackState::build (song, state.get(), 48000.0, [&] (const std::string&) { ++loads; return nullptr; });
    CHECK (loads == 2 && other->instruments[0] != state->instruments[0]);
}

TEST (flp_paths_resolve_and_samplers_import)
{
    lynx::FlPathRoots roots { "C:\\FL", "C:\\FL\\Data", "D:\\User" };
    CHECK (lynx::resolveFlPath ("%FLStudioFactoryData%\\Data\\x.wav", roots) == "C:\\FL\\Data\\x.wav");
    CHECK (lynx::resolveFlPath ("%FLStudioData%\\Patches\\y.wav", roots) == "C:\\FL\\Data\\Patches\\y.wav");
    CHECK (lynx::resolveFlPath ("%FLStudioUserData%\\z.wav", roots) == "D:\\User\\z.wav");
    CHECK (lynx::resolveFlPath ("E:\\abs.wav", roots) == "E:\\abs.wav");
    CHECK (lynx::resolveFlPath ("%FLStudioUserData%\\z.wav", {}) == "");

    flp::Project p;
    p.channels = { { 0, flp::ChannelType::sampler, "Kick", "", "%FLStudioData%\\k.wav", 1 },
                   { 1, flp::ChannelType::sampler, "Hat", "", "%FLStudioData%\\missing.wav", 1 } };
    lynx::FlpImportOptions options;
    options.roots = roots;
    options.fileExists = [] (const std::string& path) { return path == "C:\\FL\\Data\\k.wav"; };

    const auto s = lynx::importFlp (p, options);
    CHECK (s.channels[0].instrument == lynx::InstrumentKind::sampler);
    CHECK (s.channels[0].samplePath == "C:\\FL\\Data\\k.wav");
    CHECK (s.channels[1].instrument == lynx::InstrumentKind::hat); // no file: built-in instrument
}

TEST (soft_clip_is_transparent_below_knee_and_bounded)
{
    float s[] = { 0.5f, -0.89f, 1.5f, -3.0f, 0.95f };
    lynx::softClip (s, 5);
    CHECK (s[0] == 0.5f && s[1] == -0.89f);
    CHECK (s[2] > 0.9f && s[2] <= 1.0f);
    CHECK (s[3] < -0.9f && s[3] >= -1.0f);
    CHECK (s[4] > 0.9f && s[4] < 0.95f);
}

TEST (song_scheduler_plays_clips_with_offset_and_cut)
{
    lynx::Song s;
    s.channels.resize (1);
    auto& p = s.patterns.emplace_back();
    for (int step : { 0, 4, 8, 12 }) // one note per beat
        p.toggleStep (0, step);

    // clip at bar 2 playing only the second half of the pattern (beats 3 and 4)
    lynx::Clip c;
    c.pattern = 0;
    c.start = 4 * lynx::ppq;
    c.length = 2 * lynx::ppq;
    c.offset = 2 * lynx::ppq;
    s.playlist.push_back (c);
    CHECK (s.songLength() == 8 * lynx::ppq);

    const double sr = 48000.0;
    const auto tps = lynx::ticksPerSample (120.0, sr); // 1 beat = 24000 samples
    lynx::EventBuffer ev;

    // whole song in a single block (8 beats = 192000 samples)
    lynx::scheduleSong (s, 0.0, 192000, tps, ev);
    std::vector<int> ons;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on)
            ons.push_back (e.sampleOffset);
    CHECK (ons.size() == 2);
    CHECK (ons.size() == 2 && ons[0] == 4 * 24000 && ons[1] == 5 * 24000);

    // crossing the loop: bar 1 is empty, so nothing at the start of the second pass
    ev.clear();
    lynx::scheduleSong (s, 8.0 * lynx::ppq - 100 * tps, 200, tps, ev);
    CHECK (ev.size() == 0);

    // a note longer than the clip is cut at the clip end
    p.notes[3].length = 4 * lynx::ppq;
    ev.clear();
    lynx::scheduleSong (s, 0.0, 192000, tps, ev);
    int lastOff = -1;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::off)
            lastOff = std::max (lastOff, e.sampleOffset);
    CHECK (lastOff == 6 * 24000);
}

TEST (remove_pattern_fixes_clips)
{
    lynx::Song s;
    s.patterns.resize (3);
    s.currentPattern = 2;
    s.playlist = { { 0, 0, 0, 100, 0 }, { 1, 0, 100, 100, 0 }, { 2, 1, 0, 100, 0 } };
    s.removePattern (1);
    CHECK (s.patterns.size() == 2);
    CHECK (s.playlist.size() == 2 && s.playlist[1].pattern == 1);
    CHECK (s.currentPattern == 1);
}

TEST (flp_import_3x_osc_state_and_channel_filter)
{
    // new 3x Osc patch (version 14): osc 2 mix 64 coarse -12, osc 3 mix 32 coarse -24
    std::vector<std::uint8_t> blob (460, 0);
    auto put = [&] (std::size_t off, std::int32_t v) {
        for (int b = 0; b < 4; ++b)
            blob[off + (std::size_t) b] = (std::uint8_t) ((std::uint32_t) v >> (8 * b));
    };
    put (0x00, 14);
    put (0x08, 3);    // osc 1 saw
    put (0x10, 7);    // osc 1 fine +7
    put (0x04, -32);  // osc 1 pan -0.5
    put (0x1C, 64);   // osc 2 mix
    put (0x28, -12);  // osc 2 coarse
    put (0x38, 32);   // osc 3 mix
    put (0x40, 2);    // osc 3 square
    put (0x44, -19);  // osc 3 coarse: -1 octave and -7 semitones
    lynx::SynthSettings s;
    CHECK (lynx::synthFrom3xOsc (blob, s));
    CHECK (std::abs (s.osc[0].level - 0.375f) < 1e-4f && std::abs (s.osc[1].level - 0.375f) < 1e-4f && std::abs (s.osc[2].level - 0.25f) < 1e-4f);
    CHECK (s.osc[0].wave == lynx::Waveform::saw && s.osc[0].detune == 7.0f && s.osc[0].pan == -0.5f);
    CHECK (s.osc[1].wave == lynx::Waveform::sine && s.osc[1].octave == -1 && s.osc[1].semitones == 0);
    CHECK (s.osc[2].wave == lynx::Waveform::square && s.osc[2].octave == -1 && s.osc[2].semitones == -7);
    CHECK (! lynx::synthFrom3xOsc (std::vector<std::uint8_t> (40, 0), s));

    // channel filter: open by default; LP with Mod X 128 ~ 2.71 kHz; Fast LP with Mod X 128 ~ 280 Hz
    float hz = 0.0f, res = 1.0f;
    int mode = -1;
    lynx::synthFilterFromChannel (256, 0, 0, hz, res, mode);
    CHECK (hz >= 19999.0f && res == 0.0f);
    lynx::synthFilterFromChannel (128, 0, 1, hz, res, mode);
    CHECK (std::abs (hz - 2710.0f) < 30.0f && res == 0.0f);
    lynx::synthFilterFromChannel (128, 0, 0, hz, res, mode);
    CHECK (hz > 240.0f && hz < 320.0f);
    lynx::synthFilterFromChannel (128, 256, 1, hz, res, mode);
    CHECK (res > 0.8f);
    CHECK (mode == 0);
    lynx::synthFilterFromChannel (64, 0, 3, hz, res, mode); // high-pass at ~840 Hz
    CHECK (mode == 2 && std::abs (hz - 840.0f) < 40.0f);
    lynx::synthFilterFromChannel (128, 0, 2, hz, res, mode); // band-pass
    CHECK (mode == 1);
}

TEST (synth_pitch_and_filter_envelopes)
{
    auto render = [] (const lynx::SynthSettings& s, int frames) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        inst->noteOn (45, 1.0f); // A2 = 110 Hz
        std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
        inst->render (l.data(), r.data(), frames, 1.0f, 1.0f);
        return l;
    };
    auto crossings = [] (const std::vector<float>& b, int from, int to) {
        int n = 0;
        for (int i = from + 1; i < to; ++i)
            n += (b[(std::size_t) i - 1] < 0.0f) != (b[(std::size_t) i] < 0.0f);
        return n;
    };

    // 3x Osc "kick": sine with a +24 st pitch envelope falling fast
    lynx::SynthSettings kick;
    kick.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    kick.cutoff = 20000.0f;
    kick.pitchEnv = { 24.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.05f };
    const auto k = render (kick, 48000);
    const auto early = crossings (k, 0, 2400);        // first 50 ms: well above 110 Hz
    const auto late = crossings (k, 24000, 48000);    // then back to 110 Hz (~110 crossings in 0.5 s)
    CHECK (early > 20); // +24 st ramp over 50 ms: ~2.2x the average frequency (11 crossings without envelope)
    CHECK (std::abs (late - 110) < 4);

    // cutoff envelope: saw closed at 200 Hz opens +5 octaves on attack and closes again
    lynx::SynthSettings pluck;
    pluck.cutoff = 200.0f;
    pluck.filterEnv = { 5.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.1f };
    const auto p = render (pluck, 48000);
    auto hf = [&] (int from, int to) { // energy of the difference (highs)
        double e = 0.0;
        for (int i = from + 1; i < to; ++i)
            e += std::pow ((double) p[(std::size_t) i] - p[(std::size_t) i - 1], 2.0);
        return e;
    };
    CHECK (hf (0, 2400) > 4.0 * hf (40000, 42400));
}

TEST (synth_mono_reuses_the_sounding_voice)
{
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.synth.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    c.synth.cutoff = 20000.0f;
    c.synth.mono = true;
    inst->configure (c, 120.0);
    std::vector<float> l (48000), r (48000);
    inst->noteOn (57, 1.0f);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    inst->noteOn (69, 1.0f); // A4 takes over the A3 voice: only one note sounding
    inst->render (l.data() + 4800, r.data() + 4800, 43200, 1.0f, 1.0f);
    int crossings = 0;
    float peak = 0.0f;
    for (std::size_t i = 24001; i < 48000; ++i)
    {
        crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
        peak = std::max (peak, std::abs (l[i]));
    }
    CHECK (std::abs (crossings - 440) < 4); // 0.5 s of 440 Hz, without the A3 underneath
    CHECK (peak < 0.35f);                   // one voice (0.3 level), not two summed
    // no click on the handover: the jump between neighbouring samples stays sine-sized
    float jump = 0.0f;
    for (std::size_t i = 4790; i < 4810; ++i)
        jump = std::max (jump, std::abs (l[i] - l[i - 1]));
    CHECK (jump < 0.05f);
}

TEST (synth_porta_glides_and_am_modulates)
{
    auto make = [] (const lynx::SynthSettings& s) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        return inst;
    };
    auto crossings = [] (const std::vector<float>& b, std::size_t from, std::size_t to) {
        int n = 0;
        for (auto i = from + 1; i < to; ++i)
            n += (b[i - 1] < 0.0f) != (b[i] < 0.0f);
        return n;
    };

    // mono + 1-beat porta (0.5 s at 120 BPM): from A3 to A4, halfway ~E4 (329.6 Hz)
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;
    s.mono = true;
    s.porta = true;
    s.portaBeats = 1.0f;
    auto inst = make (s);
    std::vector<float> l (96000), r (96000);
    inst->noteOn (57, 1.0f);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    inst->noteOn (69, 1.0f);
    inst->render (l.data() + 4800, r.data() + 4800, 91200, 1.0f, 1.0f);
    const auto middle = crossings (l, 4800 + 12000 - 2400, 4800 + 12000 + 2400); // 0.1 s around 0.25 s
    CHECK (std::abs (middle - 66) < 6);                                            // ~330 Hz * 0.1 s * 2
    CHECK (std::abs (crossings (l, 48000, 96000) - 880) < 4);                    // reached A4 (1 s = 880 crossings)

    // AM: osc 3 at level 1.0, slow sine, modulates osc 1 (the average level drops, the sum doesn't gain osc 3)
    lynx::SynthSettings am;
    am.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    am.osc[2] = { lynx::Waveform::sine, -3, 0.0f, 1.0f };
    am.cutoff = 20000.0f;
    am.am = true;
    auto modulated = make (am);
    modulated->noteOn (69, 1.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    modulated->render (l.data(), r.data(), 48000, 1.0f, 1.0f);
    float peak = 0.0f;
    for (std::size_t i = 4800; i < 48000; ++i)
        peak = std::max (peak, std::abs (l[i]));
    CHECK (peak > 0.45f && peak < 0.65f); // 0.3 · (1 + 1) at most: (1 − o3) goes up to 2
}

TEST (channel_lfo_vibrato_and_tremolo)
{
    auto render = [] (const lynx::SynthSettings& s) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        inst->noteOn (69, 1.0f);
        std::vector<float> l (48000), r (48000);
        inst->render (l.data(), r.data(), 48000, 1.0f, 1.0f);
        return l;
    };
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;

    // tremolo: 100% volume LFO at 4 Hz (0.25 s period) -> the amplitude envelope goes from ~0 to ~2x
    auto tremolo = s;
    tremolo.lfo[lynx::lfoVolume] = { 1.0f, 0.25f, false, 0, 0.0f, 0.0f };
    const auto t = render (tremolo);
    float lowest = 1.0f, highest = 0.0f;
    for (std::size_t w = 4800; w + 480 <= 48000; w += 480) // 10 ms windows (4.4 cycles of 440 Hz)
    {
        float peak = 0.0f;
        for (auto i = w; i < w + 480; ++i)
            peak = std::max (peak, std::abs (t[i]));
        lowest = std::min (lowest, peak);
        highest = std::max (highest, peak);
    }
    CHECK (lowest < 0.08f && highest > 0.5f);

    // synced vibrato: ±1 semitone, 1-beat period (0.5 s at 120 BPM); the average stays at 440 Hz
    auto vibrato = s;
    vibrato.lfo[lynx::lfoPitch] = { 1.0f, 1.0f, true, 0, 0.0f, 0.0f };
    const auto v = render (vibrato);
    int crossings = 0;
    for (std::size_t i = 1; i < 48000; ++i)
        crossings += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
    CHECK (std::abs (crossings - 880) < 12);
    int firstQuarter = 0; // first quarter of the cycle: rising sine -> higher than 440
    for (std::size_t i = 1; i < 6000; ++i)
        firstQuarter += (v[i - 1] < 0.0f) != (v[i] < 0.0f);
    CHECK (firstQuarter > 112);
}

TEST (sampler_porta_glides_from_previous_note)
{
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    data->left.resize (96000);
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = 0.5f * (float) std::sin (6.283185307 * 220.0 * (double) i / 48000.0);
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.cut = true;
    c.sampler.porta = true;
    c.sampler.portaBeats = 1.0f; // 0.5 s at 120 BPM
    inst->configure (c, 120.0);
    std::vector<float> l (24000), r (24000);
    inst->noteOn (60, 1.0f);
    inst->render (l.data(), r.data(), 2400, 1.0f, 1.0f);
    inst->noteOn (72, 1.0f); // one octave up: starts at 220 Hz and rises
    std::fill (l.begin(), l.end(), 0.0f);
    inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
    auto crossings = [&] (std::size_t from, std::size_t to) {
        int n = 0;
        for (auto i = from + 1; i < to; ++i)
            n += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
        return n;
    };
    const auto start = crossings (600, 2400);     // 37.5 ms still near 220 Hz: ~17 crossings (440 Hz would give 33)
    const auto end = crossings (21600, 24000);    // 50 ms at the end of the glide: near 440 Hz (~44; 220 Hz would give 22)
    CHECK (start < 22);
    CHECK (end > 36);
}

TEST (zoned_sampler_picks_zone_by_key_and_velocity)
{
    auto sine = [] (double hz, float amp) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (48000);
        for (std::size_t i = 0; i < d->left.size(); ++i)
            d->left[i] = amp * (float) std::sin (6.283185307 * hz * (double) i / 48000.0);
        return d;
    };
    // lows: 220 Hz sine recorded as A3 (57), up to key 69; highs: 880 Hz as A5 (81)
    // hard layer on the highs: same tuning, louder
    std::vector<lynx::LoadedZone> zones {
        { { "graves.wav", 57, 0, 69, 0, 127, 0.0f, 1.0f }, sine (220.0, 0.5f) },
        { { "agudos.wav", 81, 70, 127, 0, 99, 0.0f, 1.0f }, sine (880.0, 0.25f) },
        { { "agudos forte.wav", 81, 70, 127, 100, 127, 0.0f, 1.0f }, sine (880.0, 0.9f) },
    };
    auto inst = lynx::makeZonedSampler (zones);
    inst->prepare (48000.0);
    inst->configure (lynx::ChannelDef {}, 120.0);
    auto play = [&] (int key, float velocity) {
        inst->allNotesOff();
        std::vector<float> l (24000), r (24000);
        inst->render (l.data(), r.data(), 2000, 1.0f, 1.0f); // let the old voices die out
        std::fill (l.begin(), l.end(), 0.0f);
        inst->noteOn (key, velocity);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        int crossings = 0;
        float peak = 0.0f;
        for (std::size_t i = 1; i < 24000; ++i)
        {
            crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
            peak = std::max (peak, std::abs (l[i]));
        }
        return std::pair { crossings, peak };
    };
    const auto low = play (69, 0.5f);    // A4 from root A3: 440 Hz -> 440 crossings in 0.5 s
    CHECK (std::abs (low.first - 440) < 4);
    const auto soft = play (81, 0.5f);   // high zone at the root: 880 Hz
    CHECK (std::abs (soft.first - 880) < 6);
    const auto hard = play (81, 0.95f);  // high velocity: hard layer
    CHECK (hard.second > soft.second * 2.0f);

    // overlapping layers round-robin: two identical zones at different levels, notes alternate
    auto rr = lynx::makeZonedSampler ({ { { "a.wav", 60 }, sine (440.0, 0.2f) }, { { "b.wav", 60 }, sine (440.0, 0.8f) } });
    rr->prepare (48000.0);
    rr->configure (lynx::ChannelDef {}, 120.0);
    float peaks[2] {};
    for (int k = 0; k < 2; ++k)
    {
        rr->allNotesOff();
        std::vector<float> l (4800), r (4800);
        rr->render (l.data(), r.data(), 2000, 1.0f, 1.0f);
        std::fill (l.begin(), l.end(), 0.0f);
        rr->noteOn (60, 1.0f);
        rr->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
        for (auto v : l)
            peaks[k] = std::max (peaks[k], std::abs (v));
    }
    CHECK (std::abs (peaks[0] - peaks[1]) > 0.3f);
}

TEST (zoned_sampler_loops_while_held)
{
    // 0.1 s sample with a loop in the middle: holding the note it keeps sounding well past the end of the file;
    // on release, it leaves the loop and ends
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    data->left.resize (4800);
    for (std::size_t i = 0; i < data->left.size(); ++i)
        data->left[i] = 0.5f * (float) std::sin (6.283185307 * 480.0 * (double) i / 48000.0); // 100 frames per cycle
    lynx::SampleZone zone { "loop.wav", 60 };
    zone.loopStart = 1000;
    zone.loopEnd = 3000;
    zone.loopMode = 2;
    auto inst = lynx::makeZonedSampler ({ { zone, data } });
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.release = 0.01f;
    inst->configure (c, 120.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (48000), r (48000);
    inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f); // 0.5 s held
    float held = 0.0f;
    for (std::size_t i = 20000; i < 24000; ++i)
        held = std::max (held, std::abs (l[i]));
    CHECK (held > 0.3f);
    inst->noteOff (60);
    inst->render (l.data() + 24000, r.data() + 24000, 24000, 1.0f, 1.0f);
    float after = 0.0f;
    for (std::size_t i = 40000; i < 48000; ++i)
        after = std::max (after, std::abs (l[i]));
    CHECK (after == 0.0f);
}

TEST (global_lfo_follows_song_position)
{
    // global volume LFO (1-beat period): a note mid-cycle starts mid-wave; the
    // same note with a per-note LFO starts at phase 0
    auto first = [] (bool global, double songTicks) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
        c.synth.cutoff = 20000.0f;
        c.synth.lfo[lynx::lfoVolume] = { 1.0f, 1.0f, true, 2, 0.0f, 0.0f, global }; // pulse: +1 in the first half
        inst->configure (c, 120.0);
        inst->setSongPosition (songTicks);
        inst->noteOn (69, 1.0f);
        std::vector<float> l (4800), r (4800);
        inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
        float peak = 0.0f;
        for (std::size_t i = 2400; i < 4800; ++i)
            peak = std::max (peak, std::abs (l[i]));
        return peak;
    };
    // mid-cycle (phase 0.6) the pulse is at −1: volume ~0; per note (phase 0) it is at +1: ~2x
    const auto globalLate = first (true, 0.6 * lynx::ppq);
    const auto perNote = first (false, 0.6 * lynx::ppq);
    CHECK (globalLate < 0.05f);
    CHECK (perNote > 0.4f);
}

TEST (sampler_channel_filter_darkens_and_bypasses_when_open)
{
    // white noise in the sampler: with a 300 Hz cutoff the energy drops a lot; open passes unchanged
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    data->left.resize (48000);
    std::uint32_t seed = 7;
    for (auto& v : data->left)
    {
        seed = seed * 1664525u + 1013904223u;
        v = ((float) (seed >> 8) / 16777216.0f - 0.5f);
    }
    data->right = data->left;
    auto energy = [&] (float cutoff) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.sampler.cutoff = cutoff;
        inst->configure (c, 120.0);
        inst->noteOn (60, 1.0f);
        std::vector<float> l (24000), r (24000);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        double e = 0.0;
        for (auto v : l)
            e += (double) v * v;
        return e;
    };
    const auto open = energy (20000.0f), dark = energy (300.0f);
    {
        // high-pass at 10 kHz: also removes most of the noise energy, but from the other side
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
        inst->prepare (48000.0);
        lynx::ChannelDef hp;
        hp.sampler.cutoff = 10000.0f;
        hp.sampler.filterType = 2;
        inst->configure (hp, 120.0);
        inst->noteOn (60, 1.0f);
        std::vector<float> l (24000), r (24000);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        double e = 0.0, lowE = 0.0;
        float lp = 0.0f;
        for (auto v : l)
        {
            e += (double) v * v;
            lp += 0.01f * (v - lp); // crude low-pass: almost nothing should remain below
            lowE += (double) lp * lp;
        }
        CHECK (e > 10.0 && e < 0.7 * energy (20000.0f));
        CHECK (lowE < e * 0.01);
    }
    CHECK (open > 100.0);
    CHECK (dark < open * 0.05);

    // +12 st pitch envelope with sustain 1: the sample runs at double speed
    // and ends in half the time (1 s of sample -> 0.5 s)
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.pitchEnv = { 12.0f, 0.0f, 0.0f, 0.1f, 1.0f, 0.1f };
    inst->configure (c, 120.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (48000), r (48000);
    inst->render (l.data(), r.data(), 48000, 1.0f, 1.0f);
    float tail = 0.0f, body = 0.0f;
    for (std::size_t i = 20000; i < 23000; ++i)
        body = std::max (body, std::abs (l[i]));
    for (std::size_t i = 25000; i < 48000; ++i)
        tail = std::max (tail, std::abs (l[i]));
    CHECK (body > 0.1f);
    CHECK (tail == 0.0f);
}

TEST (synth_oscillator_pan_and_semitones)
{
    auto render = [] (lynx::SynthSettings s, std::vector<float>& l, std::vector<float>& r) {
        auto synth = lynx::makeInstrument (lynx::InstrumentKind::synth);
        synth->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        synth->configure (c, 120.0);
        synth->noteOn (69, 1.0f);
        l.assign (24000, 0.0f);
        r.assign (24000, 0.0f);
        synth->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
    };
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f, 7, 1.0f }; // a fifth up, fully right
    s.cutoff = 20000.0f;
    std::vector<float> l, r;
    render (s, l, r);
    float peakL = 0.0f, peakR = 0.0f;
    int crossings = 0;
    for (std::size_t i = 4800; i < 24000; ++i)
    {
        peakL = std::max (peakL, std::abs (l[i]));
        peakR = std::max (peakR, std::abs (r[i]));
        crossings += (r[i - 1] < 0.0f) != (r[i] < 0.0f);
    }
    CHECK (peakL < 1e-4f && peakR > 0.1f);
    // 440 * 2^(7/12) = 659.3 Hz; 0.4 s = 263.7 cycles = ~527 crossings
    CHECK (std::abs (crossings - 527) < 6);
}

TEST (fl_wrapper_state_parses_records_and_vst3_state)
{
    // Fruity Wrapper blob (version 12): VST3 generator type, class ID, name, path and state
    std::vector<std::uint8_t> blob;
    auto u32 = [] (std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((std::uint8_t) (v >> (8 * i))); };
    auto record = [&] (std::vector<std::uint8_t>& b, std::uint32_t id, const std::vector<std::uint8_t>& payload) {
        u32 (b, id);
        u32 (b, (std::uint32_t) payload.size());
        u32 (b, 0);
        b.insert (b.end(), payload.begin(), payload.end());
    };
    auto bytes = [] (const std::string& s) { return std::vector<std::uint8_t> (s.begin(), s.end()); };

    std::vector<std::uint8_t> vst3State;
    u32 (vst3State, 1);
    record (vst3State, 1, std::vector<std::uint8_t> (64, 0));
    record (vst3State, 3, bytes ("COMPONENTE"));
    record (vst3State, 2, bytes ("CONTROLADOR"));
    // record 4: 3 slots → ParamIDs 700, −1 (empty), 0xFFFFFF01
    std::vector<std::uint8_t> slots;
    for (auto v : { 3u, 700u, 0xFFFFFFFFu, 0xFFFFFF01u })
        u32 (slots, v);
    record (vst3State, 4, slots);

    u32 (blob, 12);
    record (blob, 0x01, std::vector<std::uint8_t> (20, 0xFF));
    record (blob, 0x32, { 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    std::vector<std::uint8_t> cid;
    for (int i = 0; i < 16; ++i)
        cid.push_back ((std::uint8_t) (0xA0 + i));
    record (blob, 0x34, cid);
    record (blob, 0x36, bytes ("Surge XT"));
    record (blob, 0x37, bytes ("C:/Program Files/Common Files/VST3/Surge XT.vst3"));
    record (blob, 0x35, vst3State);

    lynx::ForeignPlugin p;
    CHECK (lynx::parseFlWrapper (blob, p));
    CHECK (p.format == lynx::ForeignPlugin::Format::vst3);
    CHECK (p.hasClassId && p.classId[0] == 0xA0 && p.classId[15] == 0xAF);
    CHECK (p.name == "Surge XT" && p.path.find ("Surge XT.vst3") != std::string::npos);
    std::vector<std::uint8_t> component, controller;
    CHECK (lynx::splitFlVst3State (p.state, component, controller));
    CHECK (component == bytes ("COMPONENTE") && controller == bytes ("CONTROLADOR"));
    const auto saved = lynx::flVst3ParamSlots (p.state);
    CHECK (saved == (std::vector<std::int32_t> { 700, -1, (std::int32_t) 0xFFFFFF01u }));

    // FL's slot assignment: saved slots keep their ParamID wherever it now sits; the empty slot stays empty;
    // a vanished ParamID (0xFFFFFF01) leaves its slot dead; parameters without a slot get slots 3, 4… in list order
    const auto positions = lynx::flVst3SlotPositions (saved, { 5u, 700u, 9u });
    CHECK (positions == (std::vector<int> { 1, -1, -1, 0, 2 }));
    CHECK (lynx::flVst3SlotPositions ({}, { 5u, 6u }) == (std::vector<int> { 0, 1 })); // new instance: slot = position

    // version out of range or no type: not a usable wrapper
    blob[0] = 13;
    CHECK (! lynx::parseFlWrapper (blob, p));
    CHECK (! lynx::parseFlWrapper ({ 12, 0, 0, 0 }, p));
}

TEST (schedule_index_matches_full_scan)
{
    // random song (long notes, swing, clips with offset); indexed scheduling must yield
    // exactly the same events as the full scan, including in the block that wraps around
    lynx::Song s = lynx::Song::makeDefault();
    s.swing = 0.6f;
    std::uint32_t seed = 12345;
    auto rnd = [&] (int n) { seed = seed * 1664525u + 1013904223u; return (int) ((seed >> 8) % (std::uint32_t) n); };
    s.patterns.resize (3);
    for (auto& p : s.patterns)
    {
        p.notes.clear();
        for (int i = 0; i < 60; ++i)
        {
            lynx::Note n;
            n.start = rnd (16 * lynx::ppq);
            n.length = rnd (5) == 0 ? rnd (12 * lynx::ppq) + 1 : rnd (lynx::ppq) + 1;
            n.channel = rnd (4);
            n.key = 40 + rnd (40);
            n.slide = rnd (20) == 0;
            p.notes.push_back (n);
        }
        p.sortNotes();
    }
    s.playlist.clear();
    for (int i = 0; i < 12; ++i)
    {
        lynx::Clip c;
        c.pattern = rnd (3);
        c.track = rnd (4);
        c.start = rnd (40) * lynx::ppq;
        c.length = (rnd (12) + 1) * lynx::ppq;
        c.offset = rnd (4) * lynx::ppq;
        s.playlist.push_back (c);
    }
    s.mode = lynx::PlayMode::song;
    const auto index = lynx::makeScheduleIndex (s);
    const auto tps = lynx::ticksPerSample (140.0, 48000.0);
    const auto loop = (double) s.songLength();
    int mismatches = 0, events = 0;

    for (int block = 0; block < 400; ++block)
    {
        const auto start = block == 0 ? loop - 100.0 * tps : (double) rnd ((int) loop * 2);
        const auto samples = 64 + rnd (24000); // up to ~1 beat per block
        lynx::EventBuffer a, b;
        lynx::scheduleSong (s, start, samples, tps, a, false, nullptr);
        lynx::scheduleSong (s, start, samples, tps, b, false, &index);
        events += a.size();
        if (a.size() != b.size())
        {
            ++mismatches;
            continue;
        }
        for (int i = 0; i < a.size(); ++i)
        {
            const auto& x = *(a.begin() + i);
            const auto& y = *(b.begin() + i);
            mismatches += x.sampleOffset != y.sampleOffset || x.type != y.type || x.key != y.key || x.channel != y.channel;
        }
    }

    CHECK (events > 500);
    CHECK (mismatches == 0);
}

TEST (mixer_track_eq_defaults_and_boost)
{
    // FL defaults: 90 Hz, 1.5 kHz and 8 kHz; peak Q 0.632; shelf slope 0.607
    lynx::TrackEq eq;
    CHECK (std::abs (lynx::TrackEq::peakQ (eq.width[1]) - 0.632) < 0.01);
    CHECK (std::abs (lynx::TrackEq::shelfSlope (eq.width[0]) - 0.607) < 0.01);
    CHECK (! eq.active());

    // +12 dB peak at 1.5 kHz on a track: a 1.5 kHz sine rises ~4x
    lynx::Song song = lynx::Song::makeDefault();
    song.mixer[1].eq.gainDb[1] = 12.0f;
    for (auto& c : song.channels)
        c.mixerTrack = 1;
    song.channels[4].instrument = lynx::InstrumentKind::synth;
    song.channels[4].synth = {};
    song.channels[4].synth.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    song.channels[4].synth.cutoff = 20000.0f;
    auto render = [] (const lynx::Song& s) {
        auto state = lynx::RackState::build (s, nullptr, 48000.0);
        lynx::RackRenderer renderer;
        renderer.prepare (48000.0);
        lynx::LiveEventQueue live;
        lynx::NoteEvent on; // 1.5 kHz ~ key 90.6: use 91 (1568 Hz), still within the wide peak
        on.channel = 4;
        on.key = 91;
        on.velocity = 1.0f;
        live.push (on);
        std::vector<float> l (24000), r (24000);
        for (int pos = 0; pos < 24000; pos += 480)
            renderer.process (state.get(), false, 0.0, 480, l.data() + pos, r.data() + pos, pos == 0 ? &live : nullptr);
        float peak = 0.0f;
        for (std::size_t i = 12000; i < 24000; ++i)
            peak = std::max (peak, std::abs (l[i]));
        return peak;
    };
    const auto boosted = render (song);
    song.mixer[1].eq.gainDb[1] = 0.0f;
    const auto flat = render (song);
    CHECK (flat > 0.01f);
    CHECK (boosted / flat > 3.0f && boosted / flat < 4.2f);
}

TEST (bars_follow_time_signature_markers)
{
    lynx::Song song;
    song.beatsPerBar = 4;
    const auto bar4 = (std::int64_t) lynx::ppq * 4;
    auto bars = song.barsBetween (0, bar4 * 3);
    CHECK (bars.size() == 3 && bars[2].tick == bar4 * 2 && bars[2].number == 3);

    // 7/4 from bar 3, and 3/4 in the middle of a 7 bar (cuts and restarts)
    song.addMarker ({ bar4 * 2, "Verso", 7, 4 });
    song.addMarker ({ bar4, "Nome só", 0, 4 });
    const auto seven = (std::int64_t) lynx::ppq * 7;
    song.addMarker ({ bar4 * 2 + seven + lynx::ppq * 2, "", 3, 4 });
    bars = song.barsBetween (0, bar4 * 2 + seven * 2);
    CHECK (bars.size() >= 5);
    CHECK (bars[1].length == bar4 && bars[2].tick == bar4 * 2 && bars[2].length == seven);
    CHECK (bars[3].length == lynx::ppq * 2);                                // cut short by the change
    CHECK (bars[4].length == lynx::ppq * 3 && bars[4].number == 5);
    CHECK (song.markers()[0].name == "Nome só");                            // sorted by tick

    // from far away: starts at the right bar without losing the numbering
    const auto far = song.barsBetween (lynx::ppq * 3000, lynx::ppq * 3001);
    CHECK (far.size() == 1 && far[0].length == lynx::ppq * 3);
    CHECK (song.barAt (lynx::ppq * 3000).number == far[0].number);
    CHECK ((lynx::ppq * 3000 - far[0].tick) < far[0].length);
}

TEST (split_drum_channel_moves_notes_to_pads)
{
    lynx::Song song = lynx::Song::makeDefault();
    const auto channels = (int) song.channels.size();
    song.patterns[0].notes.clear();
    song.patterns[0].notes.push_back ({ 0, 240, 1, 36, 0.9f });   // kit kick (channel 1)
    song.patterns[0].notes.push_back ({ 240, 240, 1, 38, 0.8f }); // snare
    song.patterns[0].notes.push_back ({ 480, 240, 1, 50, 0.8f }); // key without a pad: dropped
    song.patterns[0].notes.push_back ({ 480, 240, 3, 60, 0.8f }); // other channel: shifts by 1 position
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::channelVolume, 3, 0, 0 };
    song.automations.push_back (a);

    std::vector<lynx::DrumPad> pads (2);
    pads[0].name = "Kick";
    pads[0].key = 36;
    pads[0].layers = { { "kick.wav" } };
    pads[1].name = "Snare";
    pads[1].key = 38;
    pads[1].layers = { { "snare soft.wav", 60, 0, 127, 0, 90 }, { "snare hard.wav", 60, 0, 127, 91, 127 } };
    pads[1].cutGroup = 2;
    pads[1].cutBy = 2;
    lynx::splitDrumChannel (song, 1, pads);

    CHECK ((int) song.channels.size() == channels + 1);
    CHECK (song.channels[1].samplePath == "kick.wav" && song.channels[1].instrument == lynx::InstrumentKind::sampler);
    CHECK (song.channels[2].zones.size() == 2 && song.channels[2].cutGroup == song.channels[2].cutBy && song.channels[2].cutGroup != 0);
    const auto& notes = song.patterns[0].notes;
    CHECK (notes.size() == 3);
    CHECK (notes[0].channel == 1 && notes[0].key == 60);
    CHECK (notes[1].channel == 2 && notes[1].key == 60);
    CHECK (notes[2].channel == 4); // the old channel 3
    CHECK (song.automations[0].target.index == 4);
}

TEST (flp_import_brings_playlist)
{
    flp::Project p;
    p.header.ppq = 96;
    p.channels = { { 0, flp::ChannelType::sampler, "Kick", "", "", 1 } };
    flp::Pattern empty;
    empty.index = 1;
    flp::Pattern full;
    full.index = 2;
    flp::Note n;
    n.channel = 0;
    full.notes.push_back (n);
    p.patterns = { empty, full };

    flp::Arrangement a;
    flp::PlaylistItem item;
    item.kind = flp::PlaylistItem::Kind::pattern;
    item.index = 2;
    item.position = 384;
    item.length = 192;
    item.track = 3;
    item.patternStartTick = 96;
    a.items.push_back (item);
    item.index = 1; // empty pattern: discarded
    a.items.push_back (item);
    item.kind = flp::PlaylistItem::Kind::channelClip; // audio: not supported yet
    a.items.push_back (item);
    p.arrangements.push_back (a);

    const auto s = lynx::importFlp (p);
    CHECK (s.mode == lynx::PlayMode::song);
    CHECK (s.playlist.size() == 1);
    CHECK (s.playlist[0].pattern == 0 && s.playlist[0].track == 3);
    CHECK (s.playlist[0].start == 4 * lynx::ppq && s.playlist[0].length == 2 * lynx::ppq && s.playlist[0].offset == lynx::ppq);
}

TEST (rack_releases_notes_on_position_jump)
{
    lynx::Song s;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::synth;
    s.channels.push_back (c);
    auto& p = s.patterns.emplace_back();
    lynx::Note n;
    n.start = 0;
    n.length = 4 * lynx::ppq; // long note
    p.notes.push_back (n);
    s.bpm = 120.0;

    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    lynx::RackRenderer r;
    r.prepare (48000.0);
    std::vector<float> l (512), rr (512);
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);

    double tick = 0.0;
    for (int i = 0; i < 20; ++i, tick += 512 * tps)
        r.process (state.get(), true, tick, 512, l.data(), rr.data());

    // jump to tick 2000 (mid-note, no new "on"): the synth must go silent
    for (int i = 0; i < 40; ++i)
        r.process (state.get(), true, 2000.0 + i * 512 * tps, 512, l.data(), rr.data());

    float tail = 0.0f;
    for (auto v : l)
        tail = std::max (tail, std::abs (v));
    CHECK (tail < 0.001f);
}

namespace
{
// Two samplers with a constant 0.5 signal on different inserts; returns the output peak.
float juce_db (float gain)
{
    return 20.0f * std::log10 (gain);
}

float renderMixerCase (const std::function<void (lynx::Song&)>& tweak, lynx::MeterBank* meters = nullptr)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (4);

    for (int i = 0; i < 2; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.samplePath = "dc";
        c.volume = 1.0f;
        c.mixerTrack = i + 1;
        s.channels.push_back (c);
    }

    s.patterns.emplace_back();
    tweak (s);

    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (48000, 0.5f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };

    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;

    for (int ch = 0; ch < 2; ++ch)
    {
        lynx::NoteEvent on;
        on.channel = ch;
        on.velocity = 1.0f;
        live.push (on);
    }

    std::vector<float> l (1000), rr (1000); // larger than the mixer block: tests the chunking
    r.process (state.get(), false, 0.0, 1000, l.data(), rr.data(), &live, meters);
    return std::max (std::abs (l[999]), std::abs (rr[999]));
}
} // namespace

TEST (mixer_routes_mutes_solos_and_meters)
{
    // two 0.5 channels summed on the Master at 0 dB
    CHECK (std::abs (renderMixerCase ([] (lynx::Song&) {}) - 1.0f) < 1e-4f);

    // insert 1 muted: only channel 2 remains
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[1].muted = true; }) - 0.5f) < 1e-4f);

    // solo on insert 2: same
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[2].solo = true; }) - 0.5f) < 1e-4f);

    // FL fader curve: 0.8 = 0 dB, max ≈ +5.6 dB, half travel ≈ -12.7 dB
    CHECK (std::abs (lynx::faderGain (0.8f) - 1.0f) < 1e-5f);
    CHECK (std::abs (juce_db (lynx::faderGain (1.0f)) - 5.59f) < 0.05f);
    CHECK (lynx::faderGain (0.0f) == 0.0f);

    // Master fader at max
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[0].volume = 1.0f; }) - lynx::faderGain (1.0f)) < 1e-4f);

    // both channels on the same insert, insert fader at 0.4
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) {
               s.channels[1].mixerTrack = 1;
               s.mixer[1].volume = 0.4f;
           }) - lynx::faderGain (0.4f)) < 1e-4f);

    // a route outside the mixer falls to the last existing insert, without crashing
    CHECK (renderMixerCase ([] (lynx::Song& s) { s.channels[0].mixerTrack = 100; }) > 0.9f);

    // no mixer: everything straight to the output at 0 dB
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer.clear(); }) - 1.0f) < 1e-4f);

    lynx::MeterBank meters;
    renderMixerCase ([] (lynx::Song&) {}, &meters);
    CHECK (std::abs (meters.takePeak (1, 0) - 0.5f) < 1e-4f);
    CHECK (std::abs (meters.takePeak (0, 1) - 1.0f) < 1e-4f);
    CHECK (meters.takePeak (1, 0) == 0.0f); // reading resets
    CHECK (meters.takePeak (3, 0) == 0.0f); // insert with no channel
}

TEST (next_free_insert_and_flp_mixer_import)
{
    auto s = lynx::Song::makeDefault();
    CHECK (s.mixer.size() == 17 && s.mixer[0].name == "Master");
    CHECK (s.nextFreeInsert() == 6); // the 5 default channels take 1..5

    flp::Project p;
    p.channels = { { 0, flp::ChannelType::sampler, "Kick", "", "", 30 } };
    p.mixer = { { 0, "" }, { 30, "Drums" } };
    const auto song = lynx::importFlp (p);
    CHECK (song.channels[0].mixerTrack == 30);
    CHECK (song.mixer.size() == 31);
    CHECK (song.mixer[30].name == "Drums" && song.mixer[0].name == "Master");
}

namespace
{
float runEffectOnSine (lynx::EffectKind kind, const std::function<void (lynx::EffectSlot&)>& tweak, double hz, float amp,
                       int frames = 48000)
{
    auto fx = lynx::makeEffect (kind);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (kind);
    tweak (slot);
    std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
    for (int i = 0; i < frames; ++i)
        l[(std::size_t) i] = r[(std::size_t) i] = amp * (float) std::sin (6.283185307 * hz * i / 48000.0);
    for (int i = 0; i < frames; i += 480)
        fx->process (l.data() + i, r.data() + i, std::min (480, frames - i), slot.params, 120.0);
    float peak = 0.0f;
    for (int i = frames / 2; i < frames; ++i) // second half: already settled
    {
        CHECK (std::isfinite (l[(std::size_t) i]));
        peak = std::max (peak, std::abs (l[(std::size_t) i]));
    }
    return peak;
}
} // namespace

TEST (parametric_eq_bands_and_response)
{
    // default: transparent
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 1000.0)) < 0.01);
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::parametricEq, [] (auto&) {}, 1000.0, 0.5f) - 0.5f) < 0.005f);

    // +12 dB peak at 1 kHz on band 4: the response and the signal agree
    auto boost = [] (lynx::EffectSlot& s) {
        s.params[(std::size_t) lynx::peq::param (3, 1)] = lynx::peq::hzValue (1000.0);
        s.params[(std::size_t) lynx::peq::param (3, 2)] = lynx::peq::gainValue (12.0);
    };
    boost (slot);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 1000.0) - 12.0) < 0.2);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 100.0)) < 1.0);
    const auto peak = runEffectOnSine (lynx::EffectKind::parametricEq, boost, 1000.0, 0.1f);
    CHECK (std::abs (peak - 0.1f * std::pow (10.0f, 12.0f / 20.0f)) < 0.01f);

    // low cut at 48 dB/oct at 200 Hz: 50 Hz (2 octaves below) drops ~96 dB; 2 kHz passes
    auto cut = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
    cut.params[(std::size_t) lynx::peq::param (0, 0)] = lynx::peq::typeValue (lynx::peq::BandType::lowCut);
    cut.params[(std::size_t) lynx::peq::param (0, 1)] = lynx::peq::hzValue (200.0);
    cut.params[(std::size_t) lynx::peq::param (0, 3)] = lynx::peq::qValue (0.70710678); // Butterworth
    cut.params[(std::size_t) (lynx::peq::slopeParam + 0)] = 1.0f;
    CHECK (lynx::peq::responseDb (cut.params, 48000.0, 50.0) < -80.0);
    CHECK (std::abs (lynx::peq::responseDb (cut.params, 48000.0, 200.0) + 3.0) < 0.5); // -3 dB at the cutoff (Butterworth)
    CHECK (std::abs (lynx::peq::responseDb (cut.params, 48000.0, 2000.0)) < 0.1);

    // round-trip conversions
    CHECK (std::abs (lynx::peq::hz (lynx::peq::hzValue (440.0)) - 440.0) < 0.5);
    CHECK (std::abs (lynx::peq::q (lynx::peq::qValue (2.5)) - 2.5) < 0.01);
    CHECK (lynx::peq::type (lynx::peq::typeValue (lynx::peq::BandType::notch)) == lynx::peq::BandType::notch);
}

TEST (fl_parametric_eq2_state_maps_to_bands)
{
    // v8 blob: band 1 low cut at 100 Hz "Steep 8", band 4 peak +6 dB at 1 kHz, output −3 dB
    std::vector<std::uint8_t> blob (354, 0);
    auto put = [&] (std::size_t off, std::int32_t v) { std::memcpy (blob.data() + off, &v, 4); };
    put (0, 8);
    auto param = [&] (int i, std::int32_t v) { put (4 + 4 * (std::size_t) i, v); };
    auto hzRaw = [] (double hz) { return (std::int32_t) std::lround (std::log (hz / 20.0) / std::log (1000.0) * 65536.0); };
    for (int b = 0; b < 7; ++b)
        param (21 + b, 0); // all off
    param (21 + 0, 3);          // high pass (= low cut)
    param (7 + 0, hzRaw (100.0));
    param (14 + 0, 40124);      // default width -> Q ~0.68
    param (28 + 0, -3);         // Steep 8 -> 4 biquads
    param (21 + 3, 6);          // peaking
    param (7 + 3, hzRaw (1000.0));
    param (3, 600);             // +6 dB
    param (14 + 3, 25412);
    param (35, -300);           // main level −3 dB
    put (0x13E, 0x7F);

    const auto slot = lynx::effectFromFl ("Fruity Parametric EQ 2", blob);
    CHECK (slot.has_value() && slot->kind == lynx::EffectKind::parametricEq);
    const auto& p = slot->params;
    CHECK (lynx::peq::type (p[(std::size_t) lynx::peq::param (0, 0)]) == lynx::peq::BandType::lowCut);
    CHECK (lynx::peq::slopeStages (p[(std::size_t) lynx::peq::slopeParam]) == 4);
    CHECK (lynx::peq::type (p[(std::size_t) lynx::peq::param (3, 0)]) == lynx::peq::BandType::peak);
    CHECK (lynx::peq::type (p[(std::size_t) lynx::peq::param (1, 0)]) == lynx::peq::BandType::off);
    CHECK (std::abs (lynx::peq::responseDb (p, 48000.0, 1000.0) - 3.0) < 0.3);   // +6 from the peak −3 from the output
    CHECK (lynx::peq::responseDb (p, 48000.0, 30.0) < -40.0);                     // 48 dB/oct below 100 Hz
    CHECK (std::abs (lynx::peq::responseDb (p, 48000.0, 8000.0) + 3.0) < 0.3);

    // a band muted by the mask doesn't sound
    put (0x13E, 0x7F & ~(1 << 3));
    const auto muted = lynx::effectFromFl ("Fruity Parametric EQ 2", blob);
    CHECK (lynx::peq::type (muted->params[(std::size_t) lynx::peq::param (3, 0)]) == lynx::peq::BandType::off);
}

TEST (reverb_low_cut_and_predelay)
{
    // impulse: with 100 ms predelay the wet only starts later; with a 3 kHz low cut a
    // 100 Hz sine barely enters the reverb
    auto run = [] (const std::function<void (lynx::EffectSlot&)>& tweak, double hz, bool impulse) {
        auto fx = lynx::makeEffect (lynx::EffectKind::reverb);
        fx->prepare (48000.0);
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::reverb);
        slot.params[4] = 0.0f; // wet only
        slot.params[3] = 1.0f;
        tweak (slot);
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = impulse ? (i == 0 ? 1.0f : 0.0f) : 0.5f * (float) std::sin (6.283185307 * hz * i / 48000.0);
        for (int i = 0; i < 48000; i += 480)
            fx->process (l.data() + i, r.data() + i, 480, slot.params, 120.0);
        return l;
    };
    const auto delayed = run ([] (auto& s) { s.params[7] = 0.1f; }, 0.0, true);
    float early = 0.0f, later = 0.0f;
    for (std::size_t i = 0; i < 4800; ++i)
        early = std::max (early, std::abs (delayed[i]));
    for (std::size_t i = 4800; i < 24000; ++i)
        later = std::max (later, std::abs (delayed[i]));
    CHECK (early == 0.0f && later > 0.0f);

    auto energy = [] (const std::vector<float>& b) { double e = 0.0; for (std::size_t i = 24000; i < 48000; ++i) e += (double) b[i] * b[i]; return e; };
    const auto open = energy (run ([] (auto&) {}, 100.0, false));
    const auto cut = energy (run ([] (auto& s) { s.params[5] = 1.0f; }, 100.0, false));
    CHECK (cut < open * 0.01);
}

TEST (utility_effect_and_fl_stereo_tools)
{
    // transparent by default; balance fully right silences the left
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::utility, [] (auto&) {}, 440.0, 0.5f) - 0.5f) < 0.001f);
    CHECK (runEffectOnSine (lynx::EffectKind::utility, [] (auto& s) { s.params[1] = 1.0f; }, 440.0, 0.5f) < 0.001f);

    // Stereo Shaper in "mono": each output = 0.5 L + 0.5 R
    std::vector<std::uint8_t> blob (36, 0);
    auto put = [&] (std::size_t off, std::int32_t v) { std::memcpy (blob.data() + off, &v, 4); };
    put (4, 6400); put (8, 6400); put (12, 6400); put (16, 6400);
    const auto slot = lynx::effectFromFl ("Fruity Stereo Shaper", blob);
    CHECK (slot && slot->kind == lynx::EffectKind::utility);
    auto fx = lynx::makeEffect (lynx::EffectKind::utility);
    float l = 1.0f, r = 0.0f;
    fx->process (&l, &r, 1, slot->params, 120.0);
    CHECK (std::abs (l - 0.5f) < 1e-4f && std::abs (r - 0.5f) < 1e-4f);

    // Balance: volume 256 = 0 dB, pan −128 = fully left
    std::vector<std::uint8_t> bal (8, 0);
    std::int32_t pan = -128, vol = 256;
    std::memcpy (bal.data(), &pan, 4);
    std::memcpy (bal.data() + 4, &vol, 4);
    const auto b = lynx::effectFromFl ("Fruity Balance", bal);
    l = 0.5f; r = 0.5f;
    fx->process (&l, &r, 1, b->params, 120.0);
    CHECK (std::abs (l - 0.5f) < 1e-3f && std::abs (r) < 1e-4f);
}

TEST (effects_eq_compressor_behave)
{
    // EQ at default is transparent
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::eq, [] (auto&) {}, 1000.0, 0.5f) - 0.5f) < 0.01f);
    // +18 dB on the lows: 60 Hz rises a lot, 5 kHz barely
    CHECK (runEffectOnSine (lynx::EffectKind::eq, [] (auto& s) { s.params[0] = 1.0f; }, 60.0, 0.05f) > 0.3f);
    CHECK (runEffectOnSine (lynx::EffectKind::eq, [] (auto& s) { s.params[0] = 1.0f; }, 5000.0, 0.05f) < 0.06f);
    // compressor: threshold -20 dB, ratio 20:1 flattens a 0 dBFS signal to near the threshold
    const auto squashed = runEffectOnSine (lynx::EffectKind::compressor, [] (auto& s) {
        s.params[0] = 40.0f / 60.0f;
        s.params[1] = 1.0f;
    }, 200.0, 1.0f);
    CHECK (squashed < 0.2f && squashed > 0.05f);
    // a signal below the threshold passes intact
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::compressor, [] (auto& s) { s.params[0] = 1.0f; }, 200.0, 0.3f) - 0.3f) < 0.01f);
}

TEST (effects_delay_echoes_on_tempo_and_reverb_has_tail)
{
    auto delay = lynx::makeEffect (lynx::EffectKind::delay);
    delay->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::delay);
    slot.params[0] = 3.5f / 8.0f; // index 3 = 1/4 = 0.5 s at 120 BPM
    slot.params[1] = 0.0f;        // no feedback: a single echo
    slot.params[2] = 1.0f;
    slot.params[4] = 0.0f;        // no damping
    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    l[0] = r[0] = 1.0f;
    delay->process (l.data(), r.data(), 48000, slot.params, 120.0);
    CHECK (std::abs (l[24000] - 1.0f) < 1e-4f);
    CHECK (std::abs (l[12000]) < 1e-6f);

    auto reverb = lynx::makeEffect (lynx::EffectKind::reverb);
    reverb->prepare (48000.0);
    auto rs = lynx::makeEffectSlot (lynx::EffectKind::reverb);
    rs.params[3] = 1.0f;
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    l[0] = r[0] = 1.0f;
    reverb->process (l.data(), r.data(), 48000, rs.params, 120.0);
    float tail = 0.0f;
    for (int i = 4800; i < 9600; ++i) // between 100 and 200 ms after the impulse
        tail = std::max (tail, std::abs (l[(std::size_t) i]));
    CHECK (tail > 1e-4f && tail < 1.0f);
    CHECK (lynx::formatEffectParam (lynx::EffectKind::delay, 0, slot.params[0], 120.0) == "1/4 (500 ms)");
}

TEST (rack_runs_insert_effects_and_reuses_them)
{
    // an insert with EQ at -inf doesn't exist; we use the EQ output at -18 dB to see the effect act
    const auto quiet = renderMixerCase ([] (lynx::Song& s) {
        auto eq = lynx::makeEffectSlot (lynx::EffectKind::eq);
        eq.params[4] = 0.0f; // output -18 dB
        s.mixer[1].effects.push_back (eq);
    });
    CHECK (quiet > 0.55f && quiet < 0.6f); // 0.5 from channel 2 + 0.5 * 0.126 from channel 1

    const auto bypassed = renderMixerCase ([] (lynx::Song& s) {
        auto eq = lynx::makeEffectSlot (lynx::EffectKind::eq);
        eq.params[4] = 0.0f;
        eq.enabled = false;
        s.mixer[1].effects.push_back (eq);
    });
    CHECK (std::abs (bypassed - 1.0f) < 1e-4f);

    auto s = lynx::Song::makeDefault();
    s.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));
    auto a = lynx::RackState::build (s, nullptr, 48000.0);
    s.mixer[2].effects[0].params[0] = 0.9f; // turning a knob doesn't recreate the effect
    auto b = lynx::RackState::build (s, a.get(), 48000.0);
    CHECK (b->effects[2][0] == a->effects[2][0]);
    s.mixer[2].effects[0] = lynx::makeEffectSlot (lynx::EffectKind::delay);
    auto c = lynx::RackState::build (s, b.get(), 48000.0);
    CHECK (c->effects[2][0] != b->effects[2][0]);
}

TEST (audio_clips_trigger_cut_and_resume)
{
    lynx::Song s;
    s.bpm = 120.0; // 1 beat = 0.5 s = 24000 samples at 48 kHz
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "ramp";
    c.volume = 1.0f;
    s.channels.push_back (c);
    s.patterns.emplace_back();

    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.start = lynx::ppq;      // starts at beat 2
    clip.length = 2 * lynx::ppq; // lasts 2 beats
    clip.offset = lynx::ppq;     // skips the first beat of the audio
    s.playlist.push_back (clip);
    s.mode = lynx::PlayMode::song;

    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::EventBuffer ev;
    lynx::scheduleSong (s, 0.0, 4 * 24000, tps, ev);
    CHECK (ev.size() == 2);
    CHECK (ev[0].type == lynx::NoteEvent::Type::on && ev[0].sampleOffset == 24000 && std::abs (ev[0].startSeconds - 0.5) < 1e-9);
    CHECK (ev[1].type == lynx::NoteEvent::Type::off && ev[1].sampleOffset == 3 * 24000);

    // seek to the middle of the clip (beat 2.5): with resume, starts at the block's first sample
    ev.clear();
    lynx::scheduleSong (s, 1.5 * lynx::ppq, 256, tps, ev, true);
    CHECK (ev.size() == 1 && ev[0].sampleOffset == 0 && std::abs (ev[0].startSeconds - 0.75) < 1e-9);
    ev.clear();
    lynx::scheduleSong (s, 1.5 * lynx::ppq, 256, tps, ev, false);
    CHECK (ev.size() == 0);

    // sample whose value = position in seconds: lets us check where playback started
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        for (int i = 0; i < 48000 * 3; ++i)
            d->left.push_back ((float) i / 48000.0f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> l (256), rr (256);

    // first block already playing mid-clip
    r.process (state.get(), true, 1.5 * lynx::ppq, 256, l.data(), rr.data());
    CHECK (std::abs (l[0] - 0.75f) < 0.001f);

    // the clip ends at beat 3: after the short fade, silence (the sample would still have audio)
    double tick = 3.0 * lynx::ppq - 10 * tps;
    r.process (state.get(), false, tick, 256, l.data(), rr.data()); // stops (fade)
    for (int i = 0; i < 4; ++i)
        r.process (state.get(), false, tick, 256, l.data(), rr.data());
    CHECK (std::abs (l[255]) < 1e-6f);
}

TEST (remove_channel_fixes_notes_and_audio_clips)
{
    lynx::Song s;
    s.channels.resize (3);
    s.patterns.resize (1);
    s.patterns[0].notes = { { 0, 240, 0, 60, 1.0f }, { 0, 240, 1, 60, 1.0f }, { 0, 240, 2, 60, 1.0f } };
    lynx::Clip a;
    a.kind = lynx::ClipKind::audio;
    a.channel = 1;
    a.length = 100;
    lynx::Clip b = a;
    b.channel = 2;
    s.playlist = { a, b };
    s.removeChannel (1);
    CHECK (s.channels.size() == 2);
    CHECK (s.patterns[0].notes.size() == 2 && s.patterns[0].notes[1].channel == 1);
    CHECK (s.playlist.size() == 1 && s.playlist[0].channel == 1);
}

TEST (flp_import_audio_clips_with_offsets)
{
    flp::Project p;
    p.header.ppq = 96;
    p.tempo = 120.0;
    p.channels = { { 0, flp::ChannelType::audioClip, "Vocal", "", "%FLStudioData%\\v.wav", 2 },
                   { 1, flp::ChannelType::audioClip, "Loop", "", "%FLStudioData%\\l.wav", 3 } };
    flp::Arrangement arr;
    flp::PlaylistItem beats; // offset in beats: end - start = duration in beats
    beats.kind = flp::PlaylistItem::Kind::channelClip;
    beats.index = 0;
    beats.position = 192;
    beats.length = 384;          // 4 beats
    beats.startOffset = 2.0f;
    beats.endOffset = 6.0f;
    flp::PlaylistItem ms = beats; // offset in ms: 4 beats at 120 BPM = 2000 ms
    ms.index = 1;
    ms.startOffset = 500.0f;
    ms.endOffset = 2500.0f;
    arr.items = { beats, ms };
    p.arrangements.push_back (arr);

    lynx::FlpImportOptions options;
    options.roots = { "C:\\FL", "C:\\FL\\Data", "" };
    options.fileExists = [] (const std::string&) { return true; };
    const auto s = lynx::importFlp (p, options);

    CHECK (s.channels.size() == 2 && s.channels[0].instrument == lynx::InstrumentKind::audioClip);
    CHECK (s.channels[0].samplePath == "C:\\FL\\Data\\v.wav");
    CHECK (s.playlist.size() == 2);
    CHECK (s.playlist[0].kind == lynx::ClipKind::audio && s.playlist[0].start == 2 * lynx::ppq);
    CHECK (s.playlist[0].offset == 2 * lynx::ppq);
    CHECK (s.playlist[1].offset == lynx::ppq); // 500 ms at 120 BPM = 1 beat
    CHECK (s.mode == lynx::PlayMode::song);
}

TEST (plugin_factories_create_reuse_and_fallback)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::plugin;
    c.plugin.id = "VST3-Synth-123";
    c.mixerTrack = 1;
    s.channels.push_back (c);
    lynx::ChannelDef missing = c;
    missing.plugin.id = "VST3-Ausente-0";
    s.channels.push_back (missing);
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = "VST3-Reverb-9";
    s.mixer[1].effects.push_back (fx);
    s.patterns.emplace_back();

    int instrumentsMade = 0, effectsMade = 0;
    lynx::RackState::Factories f;
    f.pluginInstruments = [&] (const lynx::PluginRef& ref, double) -> std::shared_ptr<lynx::Instrument> {
        if (ref.id != "VST3-Synth-123")
            return nullptr;
        ++instrumentsMade;
        auto synth = lynx::makeInstrument (lynx::InstrumentKind::synth);
        synth->prepare (48000.0);
        return synth;
    };
    f.pluginEffects = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> {
        ++effectsMade;
        auto eq = lynx::makeEffect (lynx::EffectKind::eq);
        eq->prepare (48000.0);
        return eq;
    };

    auto a = lynx::RackState::build (s, nullptr, 48000.0, f);
    CHECK (instrumentsMade == 1 && effectsMade == 1);
    CHECK (a->instruments.size() == 2 && a->instruments[1] != nullptr); // missing: muted, but it exists

    // changing the saved state doesn't recreate; swapping the plugin does
    s.channels[0].plugin.state = "abc";
    auto b = lynx::RackState::build (s, a.get(), 48000.0, f);
    CHECK (instrumentsMade == 1 && b->instruments[0] == a->instruments[0] && b->effects[1][0] == a->effects[1][0]);
    s.mixer[1].effects[0].plugin.id = "VST3-Outro-1";
    auto c2 = lynx::RackState::build (s, b.get(), 48000.0, f);
    CHECK (effectsMade == 2);

    // rendering with a missing plugin and a plugin slot breaks nothing
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0;
    on.velocity = 1.0f;
    live.push (on);
    on.channel = 1;
    live.push (on);
    std::vector<float> l (256), rr (256);
    r.process (c2.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    float peak = 0.0f;
    for (auto v : l)
    {
        CHECK (std::isfinite (v));
        peak = std::max (peak, std::abs (v));
    }
    CHECK (peak > 0.0f);
}

TEST (export_song_adds_silent_tail_and_never_restarts)
{
    auto s = lynx::Song::makeDefault();
    s.playlist.clear();
    std::int64_t length = 0;
    auto pat = lynx::makeExportSong (s, true, lynx::ppq * 8, length); // empty Playlist: uses the pattern
    CHECK (length == lynx::ppq * 4);
    CHECK (pat.mode == lynx::PlayMode::song && pat.playlist.size() == 2);
    CHECK (pat.songLength() >= length + lynx::ppq * 8);

    // no new notes after the song ends, only silence until the end of the tail
    lynx::EventBuffer events;
    const auto tps = lynx::ticksPerSample (pat.bpm, 48000.0);
    int onsAfterEnd = 0, onsBefore = 0;

    for (double tick = 0.0; tick < (double) (length + lynx::ppq * 8); tick += 512 * tps)
    {
        events.clear();
        lynx::scheduleSong (pat, tick, 512, tps, events);
        for (const auto& e : events)
            if (e.type == lynx::NoteEvent::Type::on)
                (tick + e.sampleOffset * tps >= (double) length ? onsAfterEnd : onsBefore)++;
    }

    CHECK (onsBefore == (int) s.patterns[0].notes.size());
    CHECK (onsAfterEnd == 0);

    // with Playlist: the end is the last clip's, without rounding
    s.playlist.push_back ({ 0, 0, lynx::ppq * 2, lynx::ppq * 3, 0 });
    auto song = lynx::makeExportSong (s, true, 0, length);
    CHECK (length == lynx::ppq * 5 && song.playlist.size() == 1);
    song = lynx::makeExportSong (s, false, 0, length);
    CHECK (length == lynx::ppq * 4 && song.playlist[0].start == 0);
}

TEST (note_edit_selection_operations)
{
    namespace ne = lynx::noteedit;
    lynx::Pattern p;
    p.notes = { { 0, 240, 0, 60 }, { 480, 240, 0, 64 }, { 960, 480, 1, 67 } };

    // sorting keeps the indices in step
    p.notes[0].start = 1200;
    std::vector<int> tracked { 0, 2 };
    ne::sortTracking (p, tracked);
    CHECK (p.notes[2].start == 1200 && p.notes[(std::size_t) tracked[0]].start == 1200);
    CHECK (p.notes[(std::size_t) tracked[1]].key == 67);

    // group move respects the limits
    std::int64_t dt = -5000;
    int dk = 100;
    ne::move (p, { 0, 1 }, dt, dk);
    CHECK (dt == -480 && dk == 127 - 67 && p.notes[0].start == 0 && p.notes[1].key == 127);

    // duplicate: right after the selection, rounded to the grid
    lynx::Pattern q;
    q.notes = { { 0, 240, 0, 60 }, { 240, 300, 0, 62 } }; // 540-tick selection
    const auto added = ne::duplicate (q, { 0, 1 }, 960);
    CHECK (added.size() == 2 && q.notes[(std::size_t) added[0]].start == 960 && q.notes[(std::size_t) added[1]].start == 1200);

    // paste onto another channel with an offset
    const auto pasted = ne::paste (q, ne::copy (q, { 1 }), 100, 3);
    CHECK (pasted.size() == 1 && q.notes.back().channel == 3 && q.notes.back().start == 340);

    // quantize rounds to the nearest
    q.notes[0].start = 130;
    q.notes[1].start = 100;
    ne::quantize (q, { 0, 1 }, 240);
    CHECK (q.notes[0].start == 240 && q.notes[1].start == 0);

    // remove several (order and duplicates don't matter)
    const auto before = q.notes.size();
    ne::remove (q, { 1, 0, 1 });
    CHECK (q.notes.size() == before - 2);
}

TEST (recorder_builds_notes_in_pattern_and_song_mode)
{
    auto song = lynx::Song::makeDefault();
    const auto loop = song.patterns[0].effectiveLength (4); // 1 bar = 3840
    lynx::NoteRecorder r;
    r.add (song, { 100.0, 4, 60, 0.5f, true }); // no take: ignored
    CHECK (r.takeFinished().empty());

    r.begin (loop);
    r.add (song, { 4000.0, 4, 60, 0.5f, true });   // second loop pass
    r.add (song, { 4480.0, 4, 60, 0.0f, false });
    r.add (song, { 5000.0, 4, 64, 0.9f, true });   // held until stop
    r.end (song, 5240.0);
    const auto notes = r.takeFinished();
    CHECK (notes.size() == 2);
    CHECK (notes[0].start == 4000 - 3840 && notes[0].length == 480 && notes[0].velocity == 0.5f && notes[0].channel == 4);
    CHECK (notes[1].key == 64 && notes[1].start == 5000 - 3840 && notes[1].length == 240);

    // song mode: position inside the current pattern's clip (with offset)
    song.mode = lynx::PlayMode::song;
    song.playlist = { { 0, 0, 7680, 3840, 960 } };
    CHECK (lynx::patternPositionForTick (song, 7680.0 + 100.0, loop) == 1060);
    CHECK (lynx::patternPositionForTick (song, 100.0, loop) == 100); // outside the clip: like loop
}

TEST (automation_curve_and_playback)
{
    lynx::Automation a;
    a.points = { { 0, 0.0f }, { 960, 1.0f }, { 1920, 0.5f, 0.0f } };
    CHECK (a.valueAt (-10.0) == 0.0f && a.valueAt (480.0) == 0.5f && a.valueAt (5000.0) == 0.5f);
    CHECK (a.length() == 1920);
    a.points[0].tension = 1.0f; // curve: starts slow
    CHECK (a.valueAt (480.0) < 0.2f && a.valueAt (480.0) > 0.0f);

    // automated Master: silence in the first bar, sound in the second, and the last value
    // keeps applying after the automation clip ends
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.mode = lynx::PlayMode::song;
    const auto bar = (std::int64_t) lynx::ppq * 4;
    for (int b = 0; b < 3; ++b)
        song.playlist.push_back ({ 0, 0, bar * b, bar, 0 });
    lynx::Automation master;
    master.target = { lynx::AutomationTargetKind::mixerVolume, 0 };
    master.points = { { 0, 0.0f }, { bar - 1, 0.0f }, { bar, 0.8f } };
    song.automations.push_back (master);
    lynx::Clip ac;
    ac.kind = lynx::ClipKind::automation;
    ac.automation = 0;
    ac.track = 1;
    ac.length = bar + bar / 2;
    song.playlist.push_back (ac);

    const double sr = 48000.0;
    auto state = lynx::RackState::build (song, nullptr, sr);
    CHECK (state->hasAutomation && state->mixerVolumeAuto[0] == 0 && state->automationClips[0].size() == 1);

    lynx::RackRenderer r;
    r.prepare (sr, 512);
    std::vector<float> l (512), rr (512);
    const auto tps = lynx::ticksPerSample (song.bpm, sr);
    float peakBar1 = 0.0f, peakBar2 = 0.0f, peakBar3 = 0.0f;

    for (double tick = 0.0; tick < (double) (bar * 3); tick += 512 * tps)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        auto& peak = tick < (double) bar - 512 * tps ? peakBar1 : tick < (double) bar * 2 ? peakBar2 : peakBar3;
        for (auto v : l)
            peak = std::max (peak, std::abs (v));
    }

    CHECK (peakBar1 < 1.0e-6f);
    CHECK (peakBar2 > 0.05f);
    CHECK (peakBar3 > 0.05f); // after the clip: keeps the last value

    // target removed: automation disappears from the tables
    song.removeChannel (0);
    song.automations[0].target = { lynx::AutomationTargetKind::channelVolume, 0 };
    song.removeChannel (0);
    CHECK (song.automations[0].target.kind == lynx::AutomationTargetKind::none);
    song.removeAutomation (0);
    CHECK (song.automations.empty() && song.playlist.size() == 3);
}

TEST (mixer_sends_between_inserts)
{
    // insert 1 goes only to 3; 3 goes to the Master: same sum (0.5 + 0.5)
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); }) - 1.0f) < 1e-4f);

    // send at 50% and muted destination
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true, 0.5f); s.setRoute (1, 0, false); }) - 0.75f) < 1e-4f);
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[3].muted = true; }) - 0.5f) < 1e-4f);

    // going to the Master and to 3: the signal doubles
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); }) - 1.5f) < 1e-4f);

    // solo on 3 lets what reaches it sound (insert 1), but not 2
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[3].solo = true; }) - 0.5f) < 1e-4f);
    // solo on 1 lets the path to the Master (3) sound
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[1].solo = true; }) - 0.5f) < 1e-4f);

    // order and cycles
    auto song = lynx::Song::makeDefault();
    song.setRoute (4, 2, true);
    song.setRoute (2, 1, true);
    CHECK (! song.canRoute (1, 4) && ! song.canRoute (1, 2) && song.canRoute (1, 5));
    song.setRoute (1, 4, true); // ignored: would create a cycle
    CHECK (! song.routes (1, 4));
    const auto order = song.mixerOrder();
    const auto pos = [&] (int t) { return std::find (order.begin(), order.end(), t) - order.begin(); };
    CHECK ((int) order.size() == (int) song.mixer.size() - 1);
    CHECK (pos (4) < pos (2) && pos (2) < pos (1));
    song.setRoute (4, 2, false);
    CHECK (! song.routes (4, 2) && song.routes (4, 0));
}

namespace
{
// Test effect: delays the signal by `latency` samples and reports that latency (like a lookahead plugin).
class LatentEffect final : public lynx::Effect
{
public:
    explicit LatentEffect (int samples) : latency (samples), line (samples) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override { line.process (l, r, n); }
    int latencySamples() const noexcept override { return latency; }

private:
    int latency;
    lynx::DelayLine line;
};
} // namespace

TEST (delay_line_mix_into_matches_process_and_drains_tail)
{
    // mixInto = process over source*level, added to the destination; in stretches that cross the ring
    lynx::DelayLine a (37), b (37);
    std::vector<float> sl (300), sr (300), dl (300, 0.5f), dr (300, -0.5f), xl (300), xr (300);
    for (int i = 0; i < 300; ++i)
    {
        sl[(std::size_t) i] = (float) std::sin (i * 0.1);
        sr[(std::size_t) i] = (float) std::cos (i * 0.07);
    }
    for (int start = 0; start < 300; start += 100)
    {
        b.mixInto (sl.data() + start, sr.data() + start, 0.25f, dl.data() + start, dr.data() + start, 100);
        for (int i = start; i < start + 100; ++i)
        {
            xl[(std::size_t) i] = sl[(std::size_t) i] * 0.25f;
            xr[(std::size_t) i] = sr[(std::size_t) i] * 0.25f;
        }
        a.process (xl.data() + start, xr.data() + start, 100);
    }
    float worst = 0.0f;
    for (int i = 0; i < 300; ++i)
        worst = std::max ({ worst, std::abs (dl[(std::size_t) i] - 0.5f - xl[(std::size_t) i]),
                            std::abs (dr[(std::size_t) i] + 0.5f - xr[(std::size_t) i]) });
    CHECK (worst < 1e-6f);

    // silenced source: the tail still in the line comes out, then nothing remains
    std::vector<float> tl (64, 0.0f), tr (64, 0.0f);
    b.mixInto (nullptr, nullptr, 1.0f, tl.data(), tr.data(), 64);
    CHECK (std::abs (tl[0] - sl[263] * 0.25f) < 1e-6f);
    std::vector<float> zl (64, 0.0f), zr (64, 0.0f);
    b.mixInto (nullptr, nullptr, 1.0f, zl.data(), zr.data(), 64);
    CHECK (*std::max_element (zl.begin(), zl.end()) == 0.0f && *std::min_element (zl.begin(), zl.end()) == 0.0f);
}

TEST (limiter_lookahead_holds_ceiling_on_bursts)
{
    // short bursts over a low floor: the sliding maximum must see each peak before it leaves
    auto fx = lynx::makeEffect (lynx::EffectKind::limiter);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::limiter);
    slot.params[2] = 0.0f; // 10 ms release
    std::vector<float> l (48000), r (48000);
    std::uint32_t seed = 1;
    for (int i = 0; i < 48000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto noise = ((float) (seed >> 8) / 16777216.0f - 0.5f) * 0.1f;
        l[(std::size_t) i] = noise + ((i % 9973) < 3 ? 3.0f : 0.0f);
        r[(std::size_t) i] = -noise;
    }
    for (int i = 0; i < 48000; i += 333)
        fx->process (l.data() + i, r.data() + i, std::min (333, 48000 - i), slot.params, 120.0);
    float peak = 0.0f, quiet = 0.0f;
    for (int i = 0; i < 48000; ++i)
        peak = std::max (peak, std::abs (l[(std::size_t) i]));
    for (int i = 9973 * 2 + 2000; i < 9973 * 2 + 2400; ++i) // 40 ms after the burst the gain has already recovered
        quiet = std::max (quiet, std::abs (r[(std::size_t) i]));
    const auto ceiling = 1.0f; // the default ceiling is at 0 dBFS or below
    CHECK (peak <= ceiling + 1e-5f);
    CHECK (quiet > 0.03f);
}

TEST (pdc_aligns_paths_through_latent_effects)
{
    // plan: channel 0 -> insert 1 (effect of 100) -> insert 3; channel 1 -> insert 2 -> Master
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (4);
    s.channels.resize (3);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 2;
    s.channels[2].mixerTrack = 3;
    s.mixer[1].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::eq));
    s.setRoute (1, 3, true);
    s.setRoute (1, 0, false);

    auto plan = lynx::computePdc (s, { 0, 0, 30 }, { {}, { 100 } });
    CHECK (plan.inAlign[1] == 0 && plan.outLat[1] == 100);
    CHECK (plan.inAlign[3] == 100 && plan.channelDelay[2] == 70); // instrument of 30 on insert 3
    CHECK (plan.sendDelay[1][1] == 0);                             // the slowest path isn't delayed
    CHECK (plan.sendDelay[2][0] == 100 && plan.sendDelay[3][0] == 0 && plan.total == 100);

    // bypass zeroes the slot's latency
    s.mixer[1].effects[0].enabled = false;
    CHECK (lynx::computePdc (s, { 0, 0, 30 }, { {}, { 100 } }).total == 30);

    // rendered: two impulses played together come out together
    lynx::Song r;
    r.mixer = lynx::Song::defaultMixer (2);
    for (int i = 0; i < 2; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.samplePath = "impulso";
        c.volume = 1.0f;
        c.mixerTrack = i + 1;
        r.channels.push_back (c);
    }
    r.patterns.emplace_back();
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = "latente";
    r.mixer[1].effects.push_back (fx);

    lynx::RackState::Factories f;
    f.samples = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (64, 0.0f);
        d->left[0] = 1.0f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    f.pluginEffects = [] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> { return std::make_shared<LatentEffect> (100); };

    auto state = lynx::RackState::build (r, nullptr, 48000.0, f);
    CHECK (state->pdc.total == 100 && state->sendDelays[2][0] != nullptr && state->sendDelays[2][0]->length() == 100);

    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    for (int ch = 0; ch < 2; ++ch)
    {
        lynx::NoteEvent on;
        on.channel = ch;
        on.key = 60;
        on.velocity = 1.0f;
        live.push (on);
    }

    std::vector<float> l (256), rr (256);
    renderer.process (state.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    int first = -1;
    for (int i = 0; i < 256 && first < 0; ++i)
        if (std::abs (l[(std::size_t) i]) > 0.01f)
            first = i;
    CHECK (first == 100 && std::abs (l[100] - 2.0f) < 1e-3f); // both impulses arrive together, summed

    // editing without changing latencies reuses the lines
    r.channels[0].volume = 0.5f;
    auto next = lynx::RackState::build (r, state.get(), 48000.0, f);
    CHECK (next->sendDelays[2][0] == state->sendDelays[2][0]);
}

TEST (mixer_mute_fades_instead_of_clicking)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "dc";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();

    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (48000, 0.5f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };

    auto on = lynx::RackState::build (s, nullptr, 48000.0, provider);
    s.mixer[1].muted = true;
    auto off = lynx::RackState::build (s, on.get(), 48000.0, provider);

    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent note;
    note.velocity = 1.0f;
    live.push (note);

    std::vector<float> l (256), rr (256);
    r.process (on.get(), false, 0.0, 256, l.data(), rr.data(), &live);
    CHECK (std::abs (l[255] - 0.5f) < 1e-3f);
    r.process (off.get(), false, 0.0, 256, l.data(), rr.data());
    CHECK (l[0] > 0.45f && l[128] > 0.2f && l[128] < 0.3f && std::abs (l[255]) < 1e-6f); // ramp down to zero
    r.process (off.get(), false, 0.0, 256, l.data(), rr.data());
    CHECK (std::abs (l[0]) < 1e-9f);
}

TEST (dither_quantizes_to_lsb_grid_with_small_error)
{
    lynx::Dither d (16);
    std::vector<float> x (4800);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.25f * std::sin ((float) i * 0.05f);
    auto y = x;
    d.process (y.data(), (int) y.size(), 0);

    double errorSum = 0.0;
    bool onGrid = true;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto steps = y[i] * 32768.0f;
        onGrid = onGrid && std::abs (steps - std::round (steps)) < 1e-3f;
        errorSum += std::abs (y[i] - x[i]);
    }
    CHECK (onGrid);
    CHECK (errorSum / (double) x.size() < 4.0 / 32768.0); // a few LSBs of noise on average
}

TEST (parallel_rendering_matches_sequential)
{
    // 32 channels (worth parallelizing), inserts with chained sends
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (16);

    for (int i = 5; i < 32; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = i % 2 ? lynx::InstrumentKind::synth : lynx::InstrumentKind::hat;
        c.mixerTrack = 1 + i % 12;
        c.volume = 0.3f;
        song.channels.push_back (c);

        for (int step = i % 4; step < 16; step += 3)
            song.patterns[0].notes.push_back ({ (std::int64_t) step * lynx::ticksPerStep, 200, i, 48 + i, 0.7f });
    }

    song.patterns[0].sortNotes();
    song.setRoute (3, 7, true, 0.6f);
    song.setRoute (7, 9, true, 0.5f);
    song.setRoute (2, 9, true);
    song.mixer[9].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));

    auto seqState = lynx::RackState::build (song, nullptr, 48000.0);
    auto parState = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (parState->parallel && parState->mixerLayers.size() == 3);

    lynx::WorkerPool pool (3);
    lynx::RackRenderer seq, par;
    seq.prepare (48000.0, 512);
    par.prepare (48000.0, 512);
    par.setWorkerPool (&pool);

    std::vector<float> l1 (512), r1 (512), l2 (512), r2 (512);
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    bool same = true;
    float peak = 0.0f;

    for (int block = 0; block < 200; ++block)
    {
        seq.process (seqState.get(), true, block * 512 * tps, 512, l1.data(), r1.data());
        par.process (parState.get(), true, block * 512 * tps, 512, l2.data(), r2.data());
        same = same && l1 == l2 && r1 == r2;
        for (auto v : l1)
            peak = std::max (peak, std::abs (v));
    }

    CHECK (same);
    CHECK (peak > 0.05f);

    // the pool handles many small rounds in a row
    std::atomic<int> sum { 0 };
    auto add = [&sum] (int i) { sum.fetch_add (i + 1); };
    for (int round = 0; round < 2000; ++round)
        pool.parallelFor (5, add);
    CHECK (sum.load() == 2000 * 15);
}

TEST (arrangements_switch_and_keep_clip_indices)
{
    auto s = lynx::Song::makeDefault();
    s.patterns.emplace_back().name = "B";
    s.playlist = { { 1, 0, 0, 960, 0 } };
    s.loopEnd = 960;
    CHECK (s.arrangements.size() == 1 && s.currentArrangement == 0);

    const auto second = s.addArrangement ("Versão curta", true);
    CHECK (second == 1 && s.currentArrangement == 1 && s.playlist.size() == 1 && s.loopEnd == 960);
    s.playlist.push_back ({ 0, 1, 960, 960, 0 });
    s.loopEnd = 0;

    s.selectArrangement (0);
    CHECK (s.playlist.size() == 1 && s.loopEnd == 960 && s.arrangements[1].clips.size() == 2);

    // removing pattern 0 fixes the clips stored in the other arrangement
    s.removePattern (0);
    CHECK (s.playlist.size() == 1 && s.playlist[0].pattern == 0);
    CHECK (s.arrangements[1].clips.size() == 1 && s.arrangements[1].clips[0].pattern == 0);

    s.removeArrangement (0);
    CHECK (s.arrangements.size() == 1 && s.currentArrangement == 0 && s.playlist.size() == 1 && s.loopEnd == 0);
}

namespace
{
// Test effect that inverts the signal and counts how many times it processed.
class CountingInverter final : public lynx::Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override
    {
        ++calls;
        for (int i = 0; i < n; ++i)
        {
            l[i] = -l[i];
            r[i] = -r[i];
        }
    }
    int calls = 0;
};
} // namespace

TEST (keyboard_controller_outputs_key_value)
{
    std::vector<std::uint8_t> blob (554, 0);
    const std::uint32_t version = 3;
    std::memcpy (blob.data(), &version, 4);
    const float key60 = 0.7f, off = 0.2f;
    std::memcpy (blob.data() + 12 + 60 * 4, &key60, 4);
    std::memcpy (blob.data() + 548, &off, 4);
    blob[552] = 1; // auto release
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::controller;
    c.controllerState = blob;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::controller);
    inst->prepare (48000.0);
    inst->configure (c, 120.0);
    std::vector<float> l (64), r (64);
    inst->noteOn (60, 1.0f);
    inst->render (l.data(), r.data(), 64, 1.0f, 1.0f);
    CHECK (std::abs (inst->controlOutput (0) - 0.7f) < 1e-6f);
    inst->noteOff (60);
    CHECK (std::abs (inst->controlOutput (0) - 0.2f) < 1e-6f);
}

TEST (envelope_controller_ducks_on_each_note)
{
    // a unipolar articulator like "Basic Sidechain": base 0.05, drops to 0 on the note and rises to 1 in 0.3 s
    lynx::ControllerArticulator a;
    a.envOn = true;
    a.base = 0.05f;
    a.envLevel = 1.0f;
    a.count = 3;
    a.time = { 0.0f, 0.2f, 0.3f };
    a.level = { 0.0f, 0.75f, 1.0f };
    CHECK (std::abs (lynx::controllerValue (a, 0.0, -1.0) - 0.05f) < 1e-4f);
    CHECK (std::abs (lynx::controllerValue (a, 0.1, -1.0) - 0.425f) < 1e-3f);
    CHECK (lynx::controllerValue (a, 1.0, -1.0) > 0.99f);  // no sustain: runs to the end and stays
    a.sustain = 1;
    CHECK (std::abs (lynx::controllerValue (a, 1.0, -1.0) - 0.8f) < 1e-3f);  // held at sustain
    CHECK (lynx::controllerValue (a, 1.0, 0.5) > 0.99f);                     // released: continues to the end
    a.bipolar = true;
    a.base = 0.5f;
    a.sustain = -1;
    CHECK (std::abs (lynx::controllerValue (a, 0.0, -1.0) - 0.0f) < 1e-4f); // bipolar: 0.5 + (0 − 0.5)
}

TEST (peak_controller_drives_live_automation)
{
    // track 1: source (DC 0.5) with Peak Controller; track 2: DC 0.5 with volume linked to the controller
    auto render = [] (bool linked) {
        lynx::Song s;
        s.mixer = lynx::Song::defaultMixer (2);
        for (int t = 1; t <= 2; ++t)
        {
            lynx::ChannelDef c;
            c.instrument = lynx::InstrumentKind::sampler;
            c.samplePath = "dc";
            c.volume = 1.0f;
            c.mixerTrack = t;
            s.channels.push_back (c);
        }
        s.patterns.emplace_back();
        auto peak = lynx::makeEffectSlot (lynx::EffectKind::peakController);
        peak.params[0] = 1.0f; // base 1
        peak.params[1] = 0.0f; // amount −1: 1 − peak
        peak.params[2] = 0.5f; // no tension
        s.mixer[1].effects.push_back (peak);
        if (linked)
        {
            lynx::Automation a;
            a.name = "Peak";
            a.target = { lynx::AutomationTargetKind::mixerVolume, 2 };
            a.source = { 1, 0, 0 };
            s.automations.push_back (a);
        }

        lynx::RackState::Factories f;
        f.samples = [] (const std::string&) {
            auto d = std::make_shared<lynx::SampleData>();
            d->left.assign (48000, 0.5f);
            return std::shared_ptr<const lynx::SampleData> (d);
        };
        auto state = lynx::RackState::build (s, nullptr, 48000.0, f);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        lynx::LiveEventQueue live;
        for (int ch = 0; ch < 2; ++ch)
        {
            lynx::NoteEvent on;
            on.channel = ch;
            on.velocity = 1.0f;
            live.push (on);
        }
        std::vector<float> l (4800), rr (4800);
        r.process (state.get(), false, 0.0, 4800, l.data(), rr.data(), &live);
        return l[4000];
    };

    const auto free = render (false), ducked = render (true);
    CHECK (std::isfinite (ducked) && ducked > 0.0f);
    CHECK (ducked < free - 0.05f); // the 0.5 peak on track 1 lowers track 2's volume
}

TEST (effect_slot_dry_wet_and_smart_disable)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (1);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "dc";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::plugin;
    fx.plugin.id = "inversor";
    fx.mix = 0.25f; // 75% dry (+0.5) and 25% wet (-0.5) = +0.25
    s.mixer[1].effects.push_back (fx);

    auto inverter = std::make_shared<CountingInverter>();
    lynx::RackState::Factories f;
    f.samples = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (4800, 0.5f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    f.pluginEffects = [inverter] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Effect> { return inverter; };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, f);

    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> l (480), rr (480);
    r.process (state.get(), false, 0.0, 480, l.data(), rr.data(), &live);
    CHECK (std::abs (l[100] - 0.25f) < 1e-4f);

    // after the sample ends: 4 s of silence and the plugin stops being called
    for (int i = 0; i < 100 * 6; ++i) // 6 s
        r.process (state.get(), false, 0.0, 480, l.data(), rr.data());
    const auto callsAfterSilence = inverter->calls;
    for (int i = 0; i < 100; ++i)
        r.process (state.get(), false, 0.0, 480, l.data(), rr.data());
    CHECK (inverter->calls == callsAfterSilence);
    CHECK (callsAfterSilence < 100 * 6);

    // sound again: processing resumes in the same block
    live.push (on);
    r.process (state.get(), false, 0.0, 480, l.data(), rr.data(), &live);
    CHECK (inverter->calls == callsAfterSilence + 1 && std::abs (l[100] - 0.25f) < 1e-4f);
}

TEST (swing_delays_offbeat_sixteenths)
{
    CHECK (lynx::swingDelay (0, 1.0f) == 0);
    CHECK (lynx::swingDelay (lynx::ticksPerStep, 1.0f) == lynx::ticksPerStep / 2);
    CHECK (lynx::swingDelay (lynx::ticksPerStep * 2, 1.0f) == 0);
    CHECK (lynx::swingDelay (lynx::ticksPerStep + 10, 1.0f) == (lynx::ticksPerStep - 20) / 2); // off the grid: triangular weight (FL)
    CHECK (lynx::swingDelay (lynx::ticksPerStep, 1.0f, 0.5f) == lynx::ticksPerStep / 4); // channel mix
    CHECK (lynx::swingDelay (lynx::ticksPerStep * 3, 0.5f) == lynx::ticksPerStep / 4);

    lynx::Pattern p;
    p.notes = { { 0, 100, 0, 60 }, { lynx::ticksPerStep, 100, 0, 62 } };
    lynx::EventBuffer ev;
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::schedulePatternLoop (p, p.effectiveLength (4), 0.0, 12000, tps, ev, 1.0f);
    int onOffset = -1;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on && e.key == 62)
            onOffset = e.sampleOffset;
    CHECK (onOffset == (int) std::floor ((lynx::ticksPerStep * 1.5) / tps));
}

TEST (sampler_settings_pitch_reverse_envelope_cut)
{
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 48000; ++i)
        ramp->left.push_back ((float) i / 48000.0f); // ramp: the value tells where the read position is
    ramp->sampleRate = 48000.0;

    auto play = [&] (const lynx::SamplerSettings& s, int key, int frames, bool releaseAfterHalf) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.sampler = s;
        inst->configure (c, 120.0);
        inst->noteOn (key, 1.0f);
        std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
        inst->render (l.data(), r.data(), frames / 2, 1.0f, 1.0f);
        if (releaseAfterHalf)
            inst->noteOff (key);
        inst->render (l.data() + frames / 2, r.data() + frames / 2, frames - frames / 2, 1.0f, 1.0f);
        return l;
    };

    // +12 semitones: reads twice as fast
    lynx::SamplerSettings up;
    up.pitch = 12.0f;
    CHECK (std::abs (play (up, 60, 1000, false)[999] - 2.0f * 999.0f / 48000.0f) < 1e-3f);

    // reversed: starts from the end
    lynx::SamplerSettings back;
    back.reverse = true;
    CHECK (play (back, 60, 100, false)[10] > 0.99f);

    // 10 ms attack: starts silent and reaches full level
    lynx::SamplerSettings slow;
    slow.attack = 0.01f;
    const auto a = play (slow, 60, 2000, false);
    CHECK (a[10] < 0.2f * (10.0f / 48000.0f) + 1e-6f && std::abs (a[1500] - 1500.0f / 48000.0f) < 1e-4f);

    // 5 ms release: gone right after release; without release it plays in full
    lynx::SamplerSettings rel;
    rel.release = 0.005f;
    CHECK (play (rel, 60, 2000, true)[1999] == 0.0f);
    CHECK (play (lynx::SamplerSettings {}, 60, 2000, true)[1999] > 0.0f);

    // cut itself: the second note cuts the first (one voice remains)
    lynx::SamplerSettings cut;
    cut.cut = true;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    lynx::ChannelDef cutChannel;
    cutChannel.sampler = cut;
    inst->configure (cutChannel, 120.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (2000), r (2000);
    inst->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
    inst->noteOn (60, 1.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    inst->render (l.data(), r.data(), 2000, 1.0f, 1.0f);
    CHECK (std::abs (l[1999] - 1999.0f / 48000.0f) < 1e-4f); // only the new note
}

TEST (playlist_split_and_track_mute)
{
    lynx::Clip c { 0, 3, 960, 3840, 480 };
    const auto rest = lynx::splitClip (c, 2000);
    CHECK (c.start == 960 && c.length == 1040 && c.offset == 480);
    CHECK (rest.start == 2000 && rest.length == 2800 && rest.offset == 480 + 1040 && rest.track == 3);

    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::song;
    s.playlist = { { 0, 0, 0, 3840, 0 } };
    lynx::EventBuffer ev;
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::scheduleSong (s, 0.0, 48000, tps, ev);
    CHECK (ev.size() > 0);
    s.playlistTracks.resize (1);
    s.playlistTracks[0].muted = true;
    ev.clear();
    lynx::scheduleSong (s, 0.0, 48000, tps, ev);
    CHECK (ev.size() == 0);
}

TEST (note_pan_pitch_and_mute)
{
    lynx::Pattern p;
    lynx::Note a { 0, 240, 0, 60 };
    a.pan = -1.0f;
    a.finePitch = 50.0f;
    lynx::Note b { 240, 240, 0, 62 };
    b.muted = true;
    p.notes = { a, b };
    lynx::EventBuffer ev;
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::schedulePatternLoop (p, p.effectiveLength (4), 0.0, 48000, tps, ev);
    int ons = 0;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on)
        {
            ++ons;
            CHECK (e.key == 60 && e.pan == -1.0f && e.cents == 50.0f);
        }
    CHECK (ons == 1); // the muted one doesn't play

    // note pan in the sampler: fully left
    auto dc = std::make_shared<lynx::SampleData>();
    dc->left.assign (4800, 0.5f);
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, dc);
    inst->prepare (48000.0);
    inst->setNextNoteExpression (-1.0f, 0.0f);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (100), r (100);
    inst->render (l.data(), r.data(), 100, 1.0f, 1.0f);
    CHECK (l[50] > 0.6f && std::abs (r[50]) < 1e-6f);
}

TEST (tempo_automation_changes_speed_and_duration)
{
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.mode = lynx::PlayMode::song;
    const auto bar = (std::int64_t) lynx::ppq * 4;
    song.playlist = { { 0, 0, 0, bar, 0 }, { 0, 0, bar, bar, 0 } };
    lynx::Automation tempo;
    tempo.target = { lynx::AutomationTargetKind::tempo, 0 };
    const auto v120 = lynx::valueFromTempo (120.0), v240 = lynx::valueFromTempo (240.0);
    tempo.points = { { 0, v120 }, { bar - 1, v120 }, { bar, v240 } }; // step: 120 -> 240 at bar 2
    song.automations.push_back (tempo);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.track = 1;
    clip.length = bar * 2;
    song.playlist.push_back (clip);

    CHECK (std::abs (song.tempoAt (100.0) - 120.0) < 0.01 && std::abs (song.tempoAt ((double) bar + 100.0) - 240.0) < 0.01);
    // 1 bar at 120 (2 s) + 1 at 240 (1 s)
    CHECK (std::abs (song.secondsForTicks (bar * 2) - 3.0) < 0.01);

    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->tempoAutomation == 0);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> l (512), rr (512);
    r.process (state.get(), true, 10.0, 512, l.data(), rr.data());
    CHECK (std::abs (r.currentBpm() - 120.0) < 0.01);
    r.process (state.get(), true, (double) bar + 10.0, 512, l.data(), rr.data());
    CHECK (std::abs (r.currentBpm() - 240.0) < 0.01);
}

TEST (bounce_insert_song_and_apply)
{
    auto s = lynx::Song::makeDefault();
    s.setRoute (3, 7, true, 0.5f);
    s.mixer[0].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));
    s.mixer[2].solo = true;

    const auto b = lynx::makeBounceSong (s, 3);
    CHECK (b.mixer[3].solo && ! b.mixer[2].solo && b.mixer[3].toMaster && b.mixer[3].sends.empty());
    CHECK (b.mixer[0].effects.empty() && b.mode == lynx::PlayMode::song);
    const auto audible = b.soloAudible();
    CHECK (audible[3] && ! audible[1] && ! audible[7]);

    const auto channels = s.channels.size();
    const auto inserts = s.mixer.size();
    const auto created = lynx::applyBounce (s, 3, "C:/x.wav", 3840);
    CHECK (created == (int) inserts && s.mixer.size() == inserts + 1 && s.mixer[3].muted);
    CHECK (s.mixer[(std::size_t) created].sends.size() == 1 && s.mixer[(std::size_t) created].sends[0].target == 7);
    CHECK (s.channels.size() == channels + 1 && s.channels.back().mixerTrack == created);
    CHECK (s.playlist.back().kind == lynx::ClipKind::audio && s.playlist.back().length == 3840);
}

TEST (mixer_stereo_separation_phase_swap)
{
    lynx::MixerTrack t;
    std::vector<float> l { 1.0f }, r { 0.0f };
    t.stereo = 1.0f; // mono
    lynx::RackRenderer::applyStereoForTest (t, l.data(), r.data(), 1);
    CHECK (std::abs (l[0] - 0.5f) < 1e-6f && std::abs (r[0] - 0.5f) < 1e-6f);

    l = { 1.0f };
    r = { 0.0f };
    t.stereo = -1.0f; // doubled side: 1.5 L - 0.5 R
    lynx::RackRenderer::applyStereoForTest (t, l.data(), r.data(), 1);
    CHECK (std::abs (l[0] - 1.5f) < 1e-6f && std::abs (r[0] + 0.5f) < 1e-6f);

    l = { 1.0f };
    r = { 0.25f };
    t.stereo = 0.0f;
    t.swapChannels = true;
    t.invertPhase = true;
    lynx::RackRenderer::applyStereoForTest (t, l.data(), r.data(), 1);
    CHECK (l[0] == -0.25f && r[0] == -1.0f);
}

TEST (effects_limiter_filter_distortion_chorus)
{
    const double sr = 48000.0;
    auto sine = [] (float hz, float amp, int n) {
        std::vector<float> v ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            v[(std::size_t) i] = amp * std::sin (2.0f * 3.14159265f * hz * (float) i / 48000.0f);
        return v;
    };
    auto peak = [] (const std::vector<float>& v, int from) {
        float p = 0.0f;
        for (std::size_t i = (std::size_t) from; i < v.size(); ++i)
            p = std::max (p, std::abs (v[i]));
        return p;
    };

    // limiter: +12 dB on a 0.9 sine never exceeds the ceiling (-1 dB) and reports the latency
    auto lim = lynx::makeEffect (lynx::EffectKind::limiter);
    lim->prepare (sr);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::limiter);
    slot.params[0] = 11.0f / 12.0f; // -1 dB
    slot.params[1] = 0.5f;          // +12 dB
    auto l = sine (100.0f, 0.9f, 48000), r = l;
    lim->process (l.data(), r.data(), 48000, slot.params, 120.0);
    CHECK (peak (l, 0) <= std::pow (10.0f, -1.0f / 20.0f) + 1e-4f);
    CHECK (peak (l, 4800) > 0.8f);
    CHECK (lim->latencySamples() == 72);

    // low-pass filter at 500 Hz knocks down an 8 kHz sine
    auto lp = lynx::makeEffect (lynx::EffectKind::filter);
    lp->prepare (sr);
    auto fs = lynx::makeEffectSlot (lynx::EffectKind::filter);
    fs.params[1] = std::log (500.0f / 20.0f) / std::log (1000.0f);
    l = sine (8000.0f, 1.0f, 9600);
    r = l;
    lp->process (l.data(), r.data(), 9600, fs.params, 120.0);
    CHECK (peak (l, 4800) < 0.05f);

    // distortion and chorus: finite, audible output
    for (auto kind : { lynx::EffectKind::distortion, lynx::EffectKind::chorus })
    {
        auto fx = lynx::makeEffect (kind);
        fx->prepare (sr);
        auto ps = lynx::makeEffectSlot (kind);
        l = sine (220.0f, 0.5f, 9600);
        r = l;
        fx->process (l.data(), r.data(), 9600, ps.params, 120.0);
        bool finite = true;
        for (auto v : l)
            finite = finite && std::isfinite (v);
        CHECK (finite && peak (l, 2400) > 0.1f && peak (l, 0) < 2.0f);
    }
}

TEST (slide_notes_glide_without_retrigger)
{
    // sampler with a ramp: the read speed doubles after a one-octave slide
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 96000; ++i)
        ramp->left.push_back ((float) i / 96000.0f);
    ramp->sampleRate = 48000.0;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (2000), r (2000);
    inst->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
    const auto stepBefore = l[999] - l[998];
    inst->slideTo (72, 0.0f, 100.0f);
    std::fill (l.begin(), l.end(), 0.0f);
    inst->render (l.data(), r.data(), 1000, 1.0f, 1.0f);
    const auto stepAfter = l[999] - l[998];
    CHECK (std::abs (stepAfter / stepBefore - 2.0f) < 0.01f);

    // the sequencer sends "slide" (no off) for the slide note
    lynx::Pattern p;
    lynx::Note base { 0, 960, 0, 48 };
    lynx::Note glide { 480, 240, 0, 55 };
    glide.slide = true;
    p.notes = { base, glide };
    lynx::EventBuffer ev;
    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::schedulePatternLoop (p, p.effectiveLength (4), 0.0, 30000, tps, ev);
    int ons = 0, offs = 0, slides = 0;
    for (auto& e : ev)
    {
        ons += e.type == lynx::NoteEvent::Type::on;
        offs += e.type == lynx::NoteEvent::Type::off;
        if (e.type == lynx::NoteEvent::Type::slide)
        {
            ++slides;
            CHECK (e.key == 55 && std::abs (e.slideSamples - 240.0f / (float) tps) < 1.0f);
        }
    }
    CHECK (ons == 1 && offs == 1 && slides == 1);
}

TEST (synth_fm_phase_modulation)
{
    auto render = [] (const lynx::SynthSettings& s) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        inst->noteOn (69, 1.0f); // 440 Hz
        std::vector<float> l (48000), r (48000);
        inst->render (l.data(), r.data(), 48000, 1.0f, 1.0f);
        return l;
    };
    // energy (Goertzel) at frequency f over frames [from, to)
    auto energy = [] (const std::vector<float>& x, double f, int from, int to) {
        const auto w = 2.0 * 3.141592653589793 * f / 48000.0, k = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < to; ++i)
        {
            const auto s0 = x[(std::size_t) i] + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - k * s1 * s2;
    };

    lynx::SynthSettings plain;
    plain.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    plain.osc[2] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    plain.cutoff = 20000.0f;

    auto fm = plain;
    fm.fm = 1.0f;
    const auto a = render (plain), b = render (fm);

    // no FM: osc 3 adds (same pitch) and there's no 2nd harmonic; with FM the sidebands appear
    CHECK (energy (a, 880.0, 4800, 24000) < 1e-3 * energy (a, 440.0, 4800, 24000));
    CHECK (energy (b, 880.0, 4800, 24000) > 0.1 * energy (b, 440.0, 4800, 24000));
    CHECK (energy (b, 1320.0, 4800, 24000) > 0.01 * energy (b, 440.0, 4800, 24000));
    for (auto v : b)
        CHECK (std::isfinite (v) && std::abs (v) < 1.0f);

    // index decaying to 0: the start is bright, the end is nearly a sine again
    auto decaying = fm;
    decaying.fmDecay = 0.1f;
    decaying.fmSustain = 0.0f;
    const auto c = render (decaying);
    const auto early = energy (c, 880.0, 0, 2400) / energy (c, 440.0, 0, 2400);
    const auto late = energy (c, 880.0, 36000, 48000) / energy (c, 440.0, 36000, 48000);
    CHECK (early > 0.05 && late < 1e-3);
}

TEST (synth_three_oscillators_adsr_filter)
{
    auto render = [] (const lynx::SynthSettings& s, int frames, int releaseAt) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        inst->noteOn (69, 1.0f); // A4 = 440 Hz
        std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
        inst->render (l.data(), r.data(), releaseAt, 1.0f, 1.0f);
        inst->noteOff (69);
        inst->render (l.data() + releaseAt, r.data() + releaseAt, frames - releaseAt, 1.0f, 1.0f);
        return l;
    };
    auto peakIn = [] (const std::vector<float>& v, int from, int to) {
        float p = 0.0f;
        for (int i = from; i < to; ++i)
            p = std::max (p, std::abs (v[(std::size_t) i]));
        return p;
    };

    // default: sounds and fades after the release
    const lynx::SynthSettings standard;
    const auto a = render (standard, 48000, 24000);
    CHECK (peakIn (a, 1000, 20000) > 0.05f && peakIn (a, 40000, 48000) < 1e-3f);

    // pure sine with an open filter: zero-crossing count gives ~440 Hz
    lynx::SynthSettings sine;
    sine.osc[0].wave = lynx::Waveform::sine;
    sine.cutoff = 20000.0f;
    const auto b = render (sine, 48000, 47000);
    int crossings = 0;
    for (int i = 4800; i < 4800 + 48000 / 2; ++i)
        crossings += (b[(std::size_t) i - 1] < 0.0f) != (b[(std::size_t) i] < 0.0f);
    CHECK (std::abs (crossings - 440) < 6); // half a second = 220 cycles = 440 crossings
    float sinePeak = 0.0f;
    for (int i = 4800; i < 9600; ++i)
        sinePeak = std::max (sinePeak, std::abs (b[(std::size_t) i]));
    CHECK (sinePeak > 0.2f && sinePeak < 0.35f); // 0.3 level, approximated sine without clipping

    // second oscillator an octave up adds energy; sustain 0 makes the note decay on its own
    lynx::SynthSettings two = sine;
    two.osc[1] = { lynx::Waveform::sine, 1, 0.0f, 1.0f };
    two.sustain = 0.0f;
    two.decay = 0.1f;
    const auto c = render (two, 48000, 47000);
    CHECK (peakIn (c, 100, 2000) > peakIn (b, 100, 2000) && peakIn (c, 30000, 40000) < 1e-3f);
}

TEST (set_target_value_round_trips)
{
    auto s = lynx::Song::makeDefault();
    s.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::delay));
    const lynx::AutomationTarget targets[] = {
        { lynx::AutomationTargetKind::channelVolume, 1 }, { lynx::AutomationTargetKind::channelPan, 2 },
        { lynx::AutomationTargetKind::mixerVolume, 3 },   { lynx::AutomationTargetKind::mixerPan, 0 },
        { lynx::AutomationTargetKind::effectParam, 2, 0, 1 }, { lynx::AutomationTargetKind::tempo, 0 },
    };

    for (const auto& t : targets)
    {
        CHECK (s.setTargetValue (t, 0.25f));
        CHECK (std::abs (s.targetValue (t) - 0.25f) < 1e-5f);
    }

    CHECK (! s.setTargetValue ({ lynx::AutomationTargetKind::channelParam, 0, 0, 3 }, 0.5f)); // lives in the plugin
}

TEST (cut_groups_stretch_and_envelope_import)
{
    // open hat (cut by 1) is cut when the closed one (group 1) plays
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    for (int i = 0; i < 2; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.samplePath = "dc";
        c.volume = 1.0f;
        c.mixerTrack = i + 1;
        s.channels.push_back (c);
    }
    s.channels[0].cutBy = 1;    // open
    s.channels[1].cutGroup = 1; // closed
    auto& p = s.patterns.emplace_back();
    p.notes = { { 0, 100, 0, 60 }, { 960, 100, 1, 60 } };

    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (96000, 0.5f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    CHECK (state->hasCutGroups && state->cutTargets[1].size() == 1 && state->cutTargets[1][0] == 0);

    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    float afterCut = 0.0f;
    for (double tick = 0.0; tick < 960.0 * 3; tick += 512 * tps)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        if (tick > 960.0 * 2) // the closed one continues; the open one has stopped
            afterCut = std::max (afterCut, std::abs (l[256]));
    }
    CHECK (std::abs (afterCut - 0.5f * 0.78f) < 0.01f); // only the closed one (default velocity 0.78)

    // resampling stretch: 1 s of sample stretched to 4 beats at 120 BPM = 2 s
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 48000; ++i)
        ramp->left.push_back ((float) i / 48000.0f);
    ramp->sampleRate = 48000.0;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.stretchBeats = 4.0f;
    inst->configure (c, 120.0);
    inst->noteOn (72, 1.0f); // the note pitch doesn't change the speed
    std::vector<float> a (48000), b (48000);
    inst->render (a.data(), b.data(), 48000, 1.0f, 1.0f);
    CHECK (std::abs (a[47999] - 0.5f) < 0.01f); // after 1 s it has read half

    // FL envelope time: 32950 = 2 s; < 101 = 0
    CHECK (std::abs (flp::Channel::envelopeTime (32950) - 2.0) < 0.01 && flp::Channel::envelopeTime (50) == 0.0);
}

TEST (time_stretch_keeps_pitch)
{
    lynx::SampleData sine;
    sine.sampleRate = 48000.0;
    for (int i = 0; i < 48000; ++i)
        sine.left.push_back (0.5f * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / 48000.0f));

    auto crossingsPerSecond = [] (const lynx::SampleData& d) {
        int c = 0;
        const auto n = (int) d.frames();
        for (int i = 4800; i < n - 4800; ++i)
            c += (d.left[(std::size_t) i - 1] < 0.0f) != (d.left[(std::size_t) i] < 0.0f);
        return (double) c / ((double) (n - 9600) / d.sampleRate);
    };

    for (double ratio : { 2.0, 0.5, 1.37 })
    {
        const auto out = lynx::timeStretch (sine, ratio);
        CHECK (std::abs ((double) out->frames() - 48000.0 * ratio) < 2.0);
        CHECK (std::abs (crossingsPerSecond (*out) - 880.0) < 15.0); // 440 Hz stays 440 Hz

        float peak = 0.0f;
        for (std::size_t i = 4800; i + 4800 < out->frames(); ++i)
            peak = std::max (peak, std::abs (out->left[i]));
        CHECK (peak > 0.4f && peak < 0.6f); // no gaps or clipping in the overlap
    }
}

TEST (sampler_region_start_and_end)
{
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 10000; ++i)
        ramp->left.push_back ((float) i / 10000.0f);
    ramp->sampleRate = 48000.0;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.regionStart = 0.25f;
    c.sampler.regionEnd = 0.5f;
    inst->configure (c, 120.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (6000), r (6000);
    inst->render (l.data(), r.data(), 6000, 1.0f, 1.0f);
    CHECK (std::abs (l[0] - 0.25f) < 0.001f);   // starts at the beginning of the slice
    CHECK (l[2400] > 0.48f && l[2600] == 0.0f); // and stops at its end
}

TEST (arpeggiator_plays_held_chord_in_steps)
{
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.patterns[0].notes.clear();
    for (int key : { 60, 64, 67 })
        song.patterns[0].notes.push_back ({ 0, lynx::ppq * 4, 4, key, 0.8f }); // chord held for 1 bar
    song.channels[4].arp.direction = 1; // up
    song.channels[4].arp.stepBeats = 0.25f;

    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->arpeggiators[4] != nullptr && state->arpeggiators[0] == nullptr);

    // the arpeggiator generates the notes; we count what it hands to the instrument
    auto& arp = *state->arpeggiators[4];
    const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
    std::vector<int> keys;
    lynx::EventBuffer ev;

    for (double tick = 0.0; tick < lynx::ppq * 2.0; tick += 512 * tps)
    {
        ev.clear();
        lynx::schedulePatternLoop (song.patterns[0], song.patterns[0].effectiveLength (4), tick, 512, tps, ev);
        int in = 0, out = 0;
        for (auto& e : ev)
            if (e.channel == 4)
                arp.input[(std::size_t) in++] = e;
        arp.process (4, arp.input.data(), in, tick, tps, 512, song.channels[4].arp, arp.output.data(), out);
        for (int i = 0; i < out; ++i)
            if (arp.output[(std::size_t) i].type == lynx::NoteEvent::Type::on)
                keys.push_back (arp.output[(std::size_t) i].key);
    }

    // 2 beats = 8 sixteenth steps: 60 64 67 60 64 67 60 64
    CHECK (keys.size() >= 8); // the last block goes slightly past 2 beats
    const int expected[] = { 60, 64, 67, 60, 64, 67, 60, 64 };
    for (std::size_t i = 0; i < keys.size() && i < 8; ++i)
        CHECK (keys[i] == expected[i]);

    // actually rendering also makes sound
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> l (512), rr (512);
    float peak = 0.0f;
    for (double tick = 0.0; tick < lynx::ppq; tick += 512 * tps)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        for (auto v : l)
            peak = std::max (peak, std::abs (v));
    }
    CHECK (peak > 0.01f);
}

TEST (audio_clip_fades_and_gain)
{
    lynx::Song s;
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    s.mixer = lynx::Song::defaultMixer (1);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "dc";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.length = lynx::ppq * 4;  // 2 s at 120 BPM
    clip.fadeIn = lynx::ppq;      // 0.5 s
    clip.fadeOut = lynx::ppq;
    clip.gain = 0.5f;
    s.playlist.push_back (clip);

    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (48000 * 4, 1.0f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> all;
    std::vector<float> l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);

    for (double tick = 0.0; tick < lynx::ppq * 4.0; tick += 512 * tps)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        all.insert (all.end(), l.begin(), l.end());
    }

    CHECK (all[100] < 0.01f);                            // fade-in start
    CHECK (std::abs (all[12000] - 0.25f) < 0.02f);       // fade-in midpoint: half of 0.5
    CHECK (std::abs (all[48000] - 0.5f) < 0.01f);        // body: the clip gain
    CHECK (std::abs (all[96000 - 12000] - 0.25f) < 0.02f); // fade-out midpoint
}

TEST (slicer_detects_hits_and_plays_slices_by_key)
{
    // 4 hits (decaying noise) every 0.25 s
    lynx::SampleData loop;
    loop.sampleRate = 48000.0;
    loop.left.assign (48000, 0.0f);
    lynx::Noise noise;
    for (int hit = 0; hit < 4; ++hit)
        for (int i = 0; i < 6000; ++i)
            loop.left[(std::size_t) (hit * 12000 + i)] = noise.next() * 0.8f * std::exp (-(float) i / 1500.0f);

    const auto slices = lynx::detectSlices (loop, 0.5f);
    CHECK (slices.size() == 4);
    for (std::size_t i = 0; i < slices.size(); ++i)
        CHECK (std::abs (slices[i] - 0.25f * (float) i) < 0.01f);

    // key 61 plays the second slice, which starts at 0.25 and ends before 0.5
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 48000; ++i)
        ramp->left.push_back ((float) i / 48000.0f);
    ramp->sampleRate = 48000.0;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.slices = slices;
    inst->configure (c, 120.0);
    inst->noteOn (61, 1.0f);
    std::vector<float> l (20000), r (20000);
    inst->render (l.data(), r.data(), 20000, 1.0f, 1.0f);
    CHECK (std::abs (l[0] - 0.25f) < 0.01f && l[11000] > 0.45f && l[13000] == 0.0f);
}

TEST (project_templates_render_sound)
{
    const auto names = lynx::templateNames();
    CHECK (names.size() == 4);

    for (int t = 1; t < (int) names.size(); ++t)
    {
        auto song = lynx::makeTemplate (t);
        auto state = lynx::RackState::build (song, nullptr, 48000.0);
        lynx::RackRenderer r;
        r.prepare (48000.0, 512);
        std::vector<float> l (512), rr (512);
        const auto tps = lynx::ticksPerSample (song.bpm, 48000.0);
        float peak = 0.0f;

        for (double tick = 0.0; tick < lynx::ppq * 4.0; tick += 512 * tps)
        {
            r.process (state.get(), true, tick, 512, l.data(), rr.data());
            for (auto v : l)
                peak = std::max (peak, std::abs (v));
        }

        CHECK (peak > 0.05f && peak < 4.0f);
    }

    CHECK (lynx::makeTemplate (0).patterns[0].notes.empty());
}

int main (int argc, char** argv)
{
    if (argc > 1)
        realFlpDir = argv[1];

    for (auto& [name, fn] : registry())
    {
        std::printf ("%s\n", name);
        fn();
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

TEST (synth_cutoff_keytrack)
{
    // the cutoff follows the note from key 48 (130.81 Hz), like Harmor's "Key to filter frequency"
    auto rms = [] (float keyTrack, int key) {
        lynx::SynthSettings s;
        s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
        s.cutoff = 130.81f;
        s.keyTrack = keyTrack;
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = s;
        inst->configure (c, 120.0);
        inst->noteOn (key, 1.0f);
        std::vector<float> l (24000), r (24000);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        double e = 0.0;
        for (int i = 4800; i < 24000; ++i)
            e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return std::sqrt (e / 19200.0);
    };
    const auto fixed = rms (0.0f, 72), tracked = rms (1.0f, 72);
    CHECK (tracked > 4.0 * fixed);            // 2 octaves up: without keytrack the low-pass cuts
    CHECK (std::abs (rms (1.0f, 48) - rms (0.0f, 48)) < 0.05 * rms (0.0f, 48)); // unchanged at the reference key
}

namespace
{
std::vector<float> renderSynthNote (const lynx::SynthSettings& s, int key, int frames)
{
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.synth = s;
    inst->configure (c, 120.0);
    inst->noteOn (key, 1.0f);
    std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
    inst->render (l.data(), r.data(), frames, 1.0f, 1.0f);
    return l;
}

double goertzel (const std::vector<float>& x, double f, int from, int to)
{
    const auto w = 2.0 * 3.141592653589793 * f / 48000.0, k = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < to; ++i)
    {
        const auto s0 = x[(std::size_t) i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}
} // namespace

TEST (synth_pitch_sweep_ring_and_bands)
{
    lynx::SynthSettings sine;
    sine.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    sine.cutoff = 20000.0f;

    // sweep: starts at f·(1 + sweep) and returns to f (time constant s = 10 ms; s⁴ falls 4× faster)
    auto sweep = sine;
    sweep.sweep = 1.0f;
    sweep.sweepTime = 0.01f;
    const auto a = renderSynthNote (sweep, 69, 24000);
    auto crossings = [] (const std::vector<float>& x, int to) {
        int n = 0;
        for (int i = 1; i < to; ++i)
            n += (x[(std::size_t) i - 1] < 0.0f) != (x[(std::size_t) i] < 0.0f);
        return n;
    };
    CHECK (crossings (a, 480) > 1.3 * crossings (renderSynthNote (sine, 69, 480), 480)); // 10 ms: much higher
    CHECK (goertzel (a, 440.0, 12000, 24000) > 100.0 * goertzel (a, 880.0, 12000, 24000));

    // ring with |a| = 1: carrier 440 × modulator 110 -> only 330 and 550, no 440
    auto ring = sine;
    ring.am = ring.ring = true;
    ring.osc[2] = { lynx::Waveform::sine, -2, 0.0f, 1.0f };
    const auto b = renderSynthNote (ring, 69, 24000);
    CHECK (goertzel (b, 330.0, 4800, 24000) > 100.0 * goertzel (b, 440.0, 4800, 24000));
    CHECK (goertzel (b, 550.0, 4800, 24000) > 100.0 * goertzel (b, 440.0, 4800, 24000));

    // 3 bands: muting the high knocks down a 4 kHz note and barely touches an 80 Hz one
    auto bands = sine;
    bands.bandGain = { 1.0f, 1.0f, 0.0f };
    auto rms = [] (const std::vector<float>& x) {
        double e = 0.0;
        for (std::size_t i = 4800; i < x.size(); ++i)
            e += (double) x[i] * x[i];
        return std::sqrt (e / (double) (x.size() - 4800));
    };
    CHECK (rms (renderSynthNote (bands, 107, 24000)) < 0.3 * rms (renderSynthNote (sine, 107, 24000)));
    CHECK (rms (renderSynthNote (bands, 28, 24000)) > 0.85 * rms (renderSynthNote (sine, 28, 24000)));
}

TEST (love_philter_map_evaluation)
{
    lynx::LovePhilterMap line { { { 0.0, 0.0 }, { 1.0, 1.0 } } };
    CHECK (std::abs (lynx::lovePhilterMapAt (line, 0.5) - 0.5) < 1e-9);
    CHECK (std::abs (lynx::lovePhilterMapAt (line, 1.0) - 1.0) < 1e-9);
    // steps (Δx = 0): 1 until 0.4375, ramp to 0 at 0.5, then 0
    lynx::LovePhilterMap steps { { { 0.0, 1.0 }, { 0.4375, 1.0 }, { 0.0625, 0.0 }, { 0.5, 0.0 } } };
    CHECK (lynx::lovePhilterMapAt (steps, 0.27) == 1.0);
    CHECK (lynx::lovePhilterMapAt (steps, 0.75) == 0.0);
    CHECK (std::abs (lynx::lovePhilterMapAt (steps, 0.46875) - 0.5) < 1e-9);
    CHECK (lynx::lovePhilterMapAt ({}, 0.3) == 1.0);
}

TEST (lofi_holds_and_quantizes)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::lofi);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::lofi);
    slot.params[0] = 0.5f; // 128·0.125 + 1 = 17 samples
    slot.params[1] = 1.0f; // 65536/2049 ≈ 32 levels
    std::vector<float> l (170), r (170);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = 0.9f * (float) std::sin (0.05 * (double) i);
    const auto original = l;
    fx->process (l.data(), r.data(), 170, slot.params, 120.0);
    for (int i = 0; i < 17; ++i)
        CHECK (l[(std::size_t) i] == l[0]);
    CHECK (l[17] != l[16]);
    const auto step = 2.0f * 2049.0f / 65536.0f;
    for (auto v : l)
        CHECK (std::abs (v / step - std::round (v / step)) < 1e-3f);
    CHECK (std::abs (l[0] - original[0]) <= step);
    CHECK (lynx::formatEffectParam (lynx::EffectKind::lofi, 1, 0.0f, 120.0) == "16.0 bits");
}

namespace
{
lynx::EffectParams effectorParams (int type, float x, float y)
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::effector);
    slot.params[0] = (float) type / 11.0f;
    slot.params[3] = x;
    slot.params[4] = y;
    return slot.params;
}
} // namespace

TEST (effector_algorithms)
{
    CHECK (lynx::effector::typeOf (8.0f / 11.0f) == 8);
    CHECK (std::string (lynx::effector::typeName (11)) == "Ring");
    CHECK (std::abs (lynx::effector::rateBeats (0.5f, 0.0f) - 0.75) < 1e-9); // trunc(8·0.5 + 0.5) = 4 -> 3/4
    CHECK (std::abs (lynx::effector::rateBeats (1.0f, 0.5f) - 2.0) < 1e-9);   // 8 × (1 − 0.5)²

    auto run = [] (const lynx::EffectParams& p, std::vector<float> in, double bpm = 120.0) {
        auto fx = lynx::makeEffect (lynx::EffectKind::effector);
        fx->prepare (48000.0);
        fx->setSongPosition (0.0);
        auto r = in;
        fx->process (in.data(), r.data(), (int) in.size(), p, bpm);
        return in;
    };

    // Distort with Y = 1 (everything goes to the cascade): 0.5 comes out 0.5 (compensation G = 0.5/F(0.5))
    {
        auto p = effectorParams (0, 0.5f, 1.0f);
        const auto out = run (p, std::vector<float> (4800, 0.5f));
        CHECK (std::abs (out.back() - 0.5f) < 1e-3f);
        const auto small = run (p, std::vector<float> (4800, 0.05f));
        CHECK (small.back() > 0.45f && small.back() < 0.5f); // saturates near ±0.5
    }

    // Ring: DC 1 becomes the carrier sine, 2^(8.7·0 + 5) = 32 Hz -> 32 cycles per second
    {
        auto p = effectorParams (11, 0.0f, 0.0f);
        const auto out = run (p, std::vector<float> (48000, 1.0f));
        int crossings = 0;
        for (std::size_t i = 4800; i < out.size(); ++i)
            crossings += (out[i - 1] < 0.0f) != (out[i] < 0.0f);
        CHECK (crossings >= 56 && crossings <= 60); // 0.9 s × 32 Hz × 2
    }

    // Trancer, Y = 0: at the start of the period the gain drops to zero; X = 1 returns only at the end of the period
    {
        auto p = effectorParams (8, 1.0f, 0.0f);
        p[5] = 1.0f; // 8/1 = 8 beats = 4 s at 120 BPM
        const auto out = run (p, std::vector<float> (48000, 1.0f));
        CHECK (out[100] > 0.3f);       // still rising on the "and" (1% of the period = 40 ms)
        CHECK (out[24000] < 0.05f);    // 0.5 s: trough
    }

    // Reverb with Wet = 0: the dry passes intact
    {
        auto p = effectorParams (6, 0.5f, 0.5f);
        p[2] = 0.0f;
        std::vector<float> in (4800);
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = (float) std::sin (0.01 * (double) i);
        const auto out = run (p, in);
        for (std::size_t i = 0; i < in.size(); ++i)
            CHECK (std::abs (out[i] - in[i]) < 1e-6f);
    }

    // Delay: the echo arrives after the Mod Rate period (1/4 beat at 120 BPM = 125 ms), at level X²
    {
        auto p = effectorParams (5, 0.7f, 0.5f);
        p[5] = 1.0f / 8.0f; // index 1 = 1/4
        std::vector<float> in (12000, 0.0f);
        in[0] = 1.0f;
        const auto out = run (p, in);
        CHECK (std::abs (out[6000] - 0.49f) < 0.02f);
        CHECK (std::abs (out[3000]) < 1e-6f);
    }

    // Stutter: repeats the recorded start every period
    {
        auto p = effectorParams (9, 0.0f, 0.0f);
        p[5] = 0.0f; // 1/8 beat = 62.5 ms = 3000 samples
        std::vector<float> in (9000);
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = (float) i / 9000.0f;
        const auto out = run (p, in);
        CHECK (std::abs (out[3000 + 500] - in[500]) < 0.01f); // after the crossfade, it's the start again
        CHECK (std::abs (out[1000] - in[1000]) < 1e-6f);
    }
}

TEST (crossover_linkwitz_riley_sums_flat)
{
    // low band + high band of the same crossover: sum with magnitude 1 (Linkwitz-Riley; for 12 and 36 dB the HP is inverted)
    for (const auto combo : { 1, 3, 4, 5, 6, 7 })
        for (const auto testHz : { 200.0, 1000.0, 5000.0 })
        {
            auto run = [&] (bool high) {
                auto fx = lynx::makeEffect (lynx::EffectKind::crossover);
                fx->prepare (48000.0);
                auto slot = lynx::makeEffectSlot (lynx::EffectKind::crossover);
                const auto value = (float) (std::log2 (1000.0 / 30.0) / 9.0589);
                slot.params[high ? 0 : 3] = 1.0f;
                slot.params[high ? 1 : 4] = value;
                slot.params[high ? 2 : 5] = (float) combo / 7.0f;
                std::vector<float> l (24000), r (24000);
                for (std::size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = (float) std::sin (2.0 * 3.141592653589793 * testHz * (double) i / 48000.0);
                fx->process (l.data(), r.data(), 24000, slot.params, 120.0);
                return l;
            };
            const auto low = run (false), high = run (true);
            double peak = 0.0;
            for (std::size_t i = 12000; i < low.size(); ++i)
                peak = std::max (peak, (double) std::abs (low[i] + high[i]));
            CHECK (std::abs (peak - 1.0) < 0.02);
        }
    // 96 dB/oct: two octaves above the cutoff the low-pass is already below −150 dB... in practice, very low
    auto fx = lynx::makeEffect (lynx::EffectKind::crossover);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::crossover);
    slot.params[3] = 1.0f;
    slot.params[4] = (float) (std::log2 (500.0 / 30.0) / 9.0589);
    slot.params[5] = 1.0f;
    std::vector<float> l (24000), r (24000);
    for (std::size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = (float) std::sin (2.0 * 3.141592653589793 * 2000.0 * (double) i / 48000.0);
    fx->process (l.data(), r.data(), 24000, slot.params, 120.0);
    float peak = 0.0f;
    for (std::size_t i = 12000; i < l.size(); ++i)
        peak = std::max (peak, std::abs (l[i]));
    CHECK (peak < 1e-4f);
}

TEST (rhythm_gain_and_autopan)
{
    // 1-beat gate: 1 → 0 at 0.5 beat (linear) → 1 at the end; at 120 BPM 1 beat = 24000 samples at 48 kHz
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::rhythmGain);
    slot.params[0] = 1.0f / 64.0f;
    slot.params[1] = 3.0f / 12.0f;
    const float pts[3][3] = { { 0.0f, 1.0f, 0.5f }, { 0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 0.5f } };
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 3; ++j)
            slot.params[(std::size_t) (2 + 3 * k + j)] = pts[k][j];
    auto fx = lynx::makeEffect (lynx::EffectKind::rhythmGain);
    fx->prepare (48000.0);
    fx->setSongPosition (960.0 * 2.0); // beat 2: phase 0
    std::vector<float> l (24000, 1.0f), r (24000, 1.0f);
    fx->process (l.data(), r.data(), 24000, slot.params, 120.0);
    CHECK (std::abs (l[0] - 1.0f) < 1e-3f);
    CHECK (std::abs (l[6000] - 0.5f) < 1e-3f);
    CHECK (l[12000] < 1e-3f);
    CHECK (std::abs (l[18000] - 0.5f) < 1e-3f);
    // positive tension: changes quickly at the start of the segment
    CHECK (lynx::effectInfo (lynx::EffectKind::rhythmGain).params.size() == 39);

    // auto-pan: 1-beat period, full depth; start phase 0.75 -> W = 0 rising (pan centered)
    auto pan = lynx::makeEffectSlot (lynx::EffectKind::autoPan);
    pan.params[0] = 1.0f / 64.0f;
    pan.params[1] = 1.0f;
    auto ap = lynx::makeEffect (lynx::EffectKind::autoPan);
    ap->prepare (48000.0);
    ap->setSongPosition (0.0);
    std::vector<float> a (24000, 1.0f), b (24000, 1.0f);
    ap->process (a.data(), b.data(), 24000, pan.params, 120.0);
    CHECK (std::abs (a[0] - 1.0f) < 1e-3f && std::abs (b[0] - 1.0f) < 1e-3f);
    CHECK (a[6000] < 0.01f && std::abs (b[6000] - 1.0f) < 1e-3f); // ¼ cycle later: fully right
}

TEST (vocoder_side_inputs_split_modulator_and_carrier)
{
    // "Side inputs" mode (Vocodex): modulator from side input 1 (noise in the first half), carrier from side input 2
    // (saw), own input silent → the output follows the side modulator; with input 1 silent, the output is silent
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::vocoder);
    slot.params[16] = 1.0f;
    auto fx = lynx::makeEffect (lynx::EffectKind::vocoder);
    fx->prepare (48000.0);
    std::vector<float> modL (48000), car (48000), l (48000, 0.0f), r (48000, 0.0f);
    std::uint32_t seed = 1;
    for (std::size_t i = 0; i < modL.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        modL[i] = i < 24000 ? ((float) (seed >> 8) / 16777216.0f - 0.5f) : 0.0f;
        car[i] = (float) (2.0 * std::fmod ((double) i * 220.0 / 48000.0, 1.0) - 1.0) * 0.5f;
    }
    for (int done = 0; done < 48000; done += 480)
    {
        fx->setSidechain (modL.data() + done, modL.data() + done);
        fx->setSidechain2 (car.data() + done, car.data() + done);
        fx->process (l.data() + done, r.data() + done, 480, slot.params, 120.0);
    }
    auto rms = [&] (int from, int to) {
        double e = 0.0;
        for (int i = from; i < to; ++i)
            e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return std::sqrt (e / (to - from));
    };
    CHECK (rms (6000, 20000) > 0.01);
    CHECK (rms (40000, 48000) < 0.1 * rms (6000, 20000));
    // the side modulator configured but silent: nothing comes out (the own input is not used instead)
    std::vector<float> a (4800, 0.3f), b (4800, 0.3f);
    for (int done = 0; done < 4800; done += 480)
    {
        fx->setSidechain (nullptr, nullptr);
        fx->setSidechain2 (car.data() + done, car.data() + done);
        fx->process (a.data() + done, b.data() + done, 480, slot.params, 120.0);
    }
    CHECK (std::abs (a[4700]) < 0.05f);
}

TEST (vocoder_follows_modulator)
{
    // modulator (L) plays only in the first half; carrier (R) is a continuous saw: the wet follows the modulator
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::vocoder);
    auto fx = lynx::makeEffect (lynx::EffectKind::vocoder);
    fx->prepare (48000.0);
    std::vector<float> l (48000), r (48000);
    std::uint32_t seed = 1;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        l[i] = i < 24000 ? ((float) (seed >> 8) / 16777216.0f - 0.5f) : 0.0f;
        r[i] = (float) (2.0 * std::fmod ((double) i * 220.0 / 48000.0, 1.0) - 1.0) * 0.5f;
    }
    fx->process (l.data(), r.data(), 48000, slot.params, 120.0);
    auto rms = [&] (int from, int to) {
        double e = 0.0;
        for (int i = from; i < to; ++i)
            e += (double) l[(std::size_t) i] * l[(std::size_t) i];
        return std::sqrt (e / (to - from));
    };
    CHECK (rms (6000, 20000) > 0.01);
    CHECK (rms (40000, 48000) < 0.1 * rms (6000, 20000)); // modulator silent: the vocoder fades out (100 ms release)
    for (std::size_t i = 0; i < l.size(); ++i)
        CHECK (l[i] == r[i] && std::isfinite (l[i]));    // mono output
}

TEST (synth_spectral_oscillator)
{
    // 1/n spectrum = saw: same harmonic proportions and same level as LYNX's saw
    lynx::SynthSettings saw;
    saw.osc[0] = { lynx::Waveform::saw, 0, 0.0f, 1.0f };
    saw.cutoff = 20000.0f;
    auto spectral = saw;
    spectral.osc[0].wave = lynx::Waveform::spectral;
    for (int n = 1; n <= lynx::SynthSettings::spectrumSize; ++n)
        spectral.spectrum[(std::size_t) n - 1] = 1.0f / (float) n;
    const auto a = renderSynthNote (saw, 57, 24000), b = renderSynthNote (spectral, 57, 24000); // 220 Hz
    for (const auto h : { 1, 2, 3, 5 })
    {
        const auto ea = goertzel (a, 220.0 * h, 4800, 24000), eb = goertzel (b, 220.0 * h, 4800, 24000);
        CHECK (std::abs (10.0 * std::log10 (eb / ea)) < 0.5); // within 0.5 dB
    }
    // odd only: no 2nd harmonic
    auto odd = spectral;
    for (int n = 2; n <= lynx::SynthSettings::spectrumSize; n += 2)
        odd.spectrum[(std::size_t) n - 1] = 0.0f;
    const auto c = renderSynthNote (odd, 57, 24000);
    CHECK (goertzel (c, 440.0, 4800, 24000) < 1e-4 * goertzel (c, 220.0, 4800, 24000));
    // high note: the band-limited tables don't pass Nyquist (no aliasing below the fundamental)
    const auto d = renderSynthNote (spectral, 105, 24000); // 3520 Hz
    CHECK (goertzel (d, 1000.0, 4800, 24000) < 1e-3 * goertzel (d, 3520.0, 4800, 24000));
}

TEST (compressor_sidechain_ducks_and_orders_mixer)
{
    // the detector listens to the side input: quiet signal on the track, loud kick on the sidechain -> reduction
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::compressor);
    slot.params[0] = 0.5f; // threshold
    slot.params[1] = 1.0f; // high ratio
    slot.params[4] = 0.0f;
    auto run = [&] (bool withSidechain) {
        auto fx = lynx::makeEffect (lynx::EffectKind::compressor);
        fx->prepare (48000.0);
        std::vector<float> l (4800, 0.1f), r (4800, 0.1f), kick (4800, 1.0f);
        if (withSidechain)
            fx->setSidechain (kick.data(), kick.data());
        fx->process (l.data(), r.data(), 4800, slot.params, 120.0);
        return l.back();
    };
    const auto alone = run (false), ducked = run (true);
    CHECK (ducked < 0.5f * alone);

    // order: track 5 listens to 7 via sidechain -> 7 comes before 5
    auto song = lynx::Song::makeDefault();
    while (song.mixer.size() < 8)
        song.mixer.push_back ({});
    auto comp = lynx::makeEffectSlot (lynx::EffectKind::compressor);
    comp.sidechain = 7;
    song.mixer[5].effects.push_back (comp);
    const auto order = song.mixerOrder();
    const auto at = [&] (int t) { return std::find (order.begin(), order.end(), t) - order.begin(); };
    CHECK (at (7) < at (5));
}

TEST (pitch_correction_pulls_to_allowed_note)
{
    auto run = [] (const lynx::EffectParams& p, double hz) {
        auto fx = lynx::makeEffect (lynx::EffectKind::pitchCorrect);
        fx->prepare (48000.0);
        std::vector<float> l (48000 * 2), r;
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / 48000.0);
        r = l;
        fx->process (l.data(), r.data(), (int) l.size(), p, 120.0);
        // frequency from zero crossings in the last second
        int crossings = 0;
        for (std::size_t i = 48000 + 1; i < l.size(); ++i)
            crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
        return crossings / 2.0;
    };
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::pitchCorrect); // chromatic, instant, A 440
    CHECK (std::abs (run (slot.params, 452.0) - 440.0) < 4.0);
    for (int k = 0; k < 12; ++k)
        slot.params[(std::size_t) (5 + k)] = k == 0 ? 1.0f : 0.0f; // only C
    CHECK (std::abs (run (slot.params, 452.0) - 523.25) < 5.0);  // C5 (523 Hz) is closer than C4 (262)
    slot.params[2] = 1.0f; // turn everything off: passes unchanged
    CHECK (std::abs (run (slot.params, 452.0) - 452.0) < 4.0);
}

TEST (pitch_correction_bypass_is_a_pure_delay)
{
    // FL's Pitcher with Bypass All still runs its shifter: the plain path is an exact 9-sample delay, the formant
    // path (256-Hann overlap-add) an exact 257-sample delay, and Gender changes nothing without a shift
    // (REA_rodada30 §2.4/2.5); both are reported as latency
    std::vector<float> in (48000);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * (double) i / 48000.0)
              + 0.2f * (float) std::sin (2.0 * 3.141592653589793 * 1234.5 * (double) i / 48000.0);
    auto run = [&] (bool formant, float gender, int& latency) {
        auto fx = lynx::makeEffect (lynx::EffectKind::pitchCorrect);
        fx->prepare (48000.0);
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::pitchCorrect);
        slot.params[2] = 1.0f;
        slot.params[29] = formant ? 1.0f : 0.0f;
        slot.params[30] = gender;
        auto l = in, r = in;
        for (std::size_t i = 0; i < l.size(); i += 480)
            fx->process (l.data() + i, r.data() + i, 480, slot.params, 120.0);
        latency = fx->latencySamples();
        return l;
    };
    int latency = 0;
    auto plain = run (false, 0.5f, latency);
    CHECK (latency == 9);
    auto worst = 0.0f;
    for (std::size_t i = 4000; i < in.size(); ++i)
        worst = std::max (worst, std::abs (plain[i] - in[i - 9]));
    CHECK (worst < 1e-5f);
    auto formant = run (true, 0.8f, latency);
    CHECK (latency == 257);
    worst = 0.0f;
    for (std::size_t i = 4000; i < in.size(); ++i)
        worst = std::max (worst, std::abs (formant[i] - in[i - 257]));
    CHECK (worst < 1e-5f);
}

TEST (pitch_correction_formant_path)
{
    // Formant on: PSOLA keeps the correction (output pitch = grain spacing) while Gender resamples the grains:
    // the spectrum of a saw moves up with Gender > 0.5 and down with Gender < 0.5
    auto run = [] (float gender) {
        auto fx = lynx::makeEffect (lynx::EffectKind::pitchCorrect);
        fx->prepare (48000.0);
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::pitchCorrect); // chromatic, instant, A 440
        slot.params[29] = 1.0f;
        slot.params[30] = gender;
        std::vector<float> l (48000 * 2), r;
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = 0.5f * (float) (2.0 * std::fmod (452.0 * (double) i / 48000.0, 1.0) - 1.0); // saw
        r = l;
        fx->process (l.data(), r.data(), (int) l.size(), slot.params, 120.0);
        l.erase (l.begin(), l.begin() + 48000);
        return l;
    };
    // period by autocorrelation (lags 60…160 = 300…800 Hz)
    auto pitch = [] (const std::vector<float>& x) {
        auto best = 0.0, bestLag = 0.0;
        for (int lag = 60; lag <= 160; ++lag)
        {
            auto sum = 0.0;
            for (std::size_t i = 0; i + (std::size_t) lag < x.size(); ++i)
                sum += (double) x[i] * x[i + (std::size_t) lag];
            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }
        return 48000.0 / bestLag;
    };
    // brightness: energy of the first difference relative to the energy
    auto brightness = [] (const std::vector<float>& x) {
        auto e = 0.0, d = 0.0;
        for (std::size_t i = 1; i < x.size(); ++i)
        {
            e += (double) x[i] * x[i];
            d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        }
        return d / e;
    };
    const auto neutral = run (0.5f), up = run (0.8f), down = run (0.3f);
    CHECK (std::abs (pitch (neutral) - 440.0) < 5.0); // 452 Hz pulled to A
    CHECK (std::abs (pitch (up) - 440.0) < 5.0);      // Gender does not move the pitch
    CHECK (brightness (up) > 1.5 * brightness (neutral));
    CHECK (brightness (down) < 0.7 * brightness (neutral));
}

TEST (parallel_container_inner_links_modulate)
{
    // one branch: utility (gain) + a control-only auto-pan LFO (1 beat); the link maps the LFO 0..1 onto the
    // utility gain −24 dB..0 dB, so the level must swing; without the link it stays put
    auto container = lynx::makeEffectSlot (lynx::EffectKind::parallel);
    auto gain = lynx::makeEffectSlot (lynx::EffectKind::utility);
    auto lfo = lynx::makeEffectSlot (lynx::EffectKind::autoPan);
    lfo.params[0] = 1.0f / 64.0f;
    lfo.params[4] = 1.0f; // control only: audio passes unchanged
    container.branches.push_back ({ { gain, lfo }, 1.0f });
    lynx::EffectLink link;
    link.source = 1;
    link.param = 0;
    for (int i = 0; i <= 32; ++i)
        link.curve.push_back (0.5f * (float) i / 32.0f);

    auto levels = [&] (bool linked) {
        auto slot = container;
        if (linked)
            slot.links.push_back (link);
        auto fx = lynx::makeParallelEffect (slot);
        fx->prepare (48000.0);
        float lo = 1e9f, hi = 0.0f;
        for (int block = 0; block < 94; ++block) // ~1 beat at 120 BPM in 256-sample blocks
        {
            std::vector<float> l (256, 1.0f), r (256, 1.0f);
            fx->setSongPosition (block * 256.0 / 48000.0 * 2.0 * 960.0); // the engine positions every block
            fx->process (l.data(), r.data(), 256, slot.params, 120.0);
            lo = std::min (lo, std::abs (l[128]));
            hi = std::max (hi, std::abs (l[128]));
        }
        return std::pair { lo, hi };
    };
    const auto [lo, hi] = levels (true);
    CHECK (hi > 0.8f && lo < 0.12f);
    const auto [lo0, hi0] = levels (false);
    CHECK (std::abs (hi0 - lo0) < 1e-4f);
}

TEST (synth_spectrum_decays_per_harmonic)
{
    // harmonic 1 holds, harmonic 3 falls 60 dB in 0.2 s: early the 3rd is as loud as the 1st, later it is gone
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::spectral, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;
    s.decay = 0.0f;
    s.sustain = 1.0f;
    s.spectrum[0] = 1.0f;
    s.spectrum[2] = 1.0f;
    s.spectrumDecay[2] = 0.2f;
    const auto x = renderSynthNote (s, 57, 48000); // 220 Hz, 48 kHz, 1 s
    // equal 2400-sample windows (Goertzel power grows with the window): 10…60 ms and 500…550 ms
    const auto early1 = goertzel (x, 220.0, 480, 2880), early3 = goertzel (x, 660.0, 480, 2880);
    const auto late1 = goertzel (x, 220.0, 24000, 26400), late3 = goertzel (x, 660.0, 24000, 26400);
    CHECK (early3 > 0.05 * early1); // ≈ −9 dB on average over 10…60 ms
    CHECK (late3 < 1e-4 * late1);
    CHECK (std::abs (10.0 * std::log10 (late1 / early1)) < 1.0); // the 1st holds
    // without per-harmonic decay the spectrum stays as it is
    s.spectrumDecay[2] = 0.0f;
    const auto y = renderSynthNote (s, 57, 48000);
    CHECK (std::abs (10.0 * std::log10 (goertzel (y, 660.0, 24000, 26400) / goertzel (y, 660.0, 480, 2880))) < 1.0);
}

TEST (worker_pool_runs_each_index_once_per_round)
{
    // rounds of alternating sizes (a stalled worker must never run an index of a later round), each index
    // exactly once, and nothing running after parallelFor returns
    lynx::WorkerPool pool (3);
    std::vector<std::atomic<int>> hits (64);
    std::atomic<int> round { 0 }, outside { 0 }, wrongRound { 0 };
    std::atomic<bool> open { false };
    int failures = 0;
    for (int r = 1; r <= 20000; ++r)
    {
        const auto n = (r % 3 == 0) ? 64 : (r % 3 == 1 ? 2 : 9);
        for (int i = 0; i < n; ++i)
            hits[(std::size_t) i].store (0);
        round.store (r);
        open.store (true);
        auto fn = [&, r] (int i) {
            if (! open.load())
                outside.fetch_add (1);
            if (round.load() != r)
                wrongRound.fetch_add (1);
            hits[(std::size_t) i].fetch_add (1);
        };
        pool.parallelFor (n, fn);
        open.store (false);
        for (int i = 0; i < n; ++i)
            failures += hits[(std::size_t) i].load() != 1;
    }
    CHECK (failures == 0);
    CHECK (outside.load() == 0);
    CHECK (wrongRound.load() == 0);
}

TEST (ladder_filter_follows_toxic_law)
{
    // the 4-pole ladder: −24 dB/oct far above the cutoff (vs −12 for the SVF), resonance raises the corner, and
    // the input saturation keeps it bounded even at full resonance
    auto level = [] (bool ladder, float res, double hz) {
        lynx::SynthSettings s;
        s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
        s.cutoff = 500.0f;
        s.resonance = res;
        s.ladder = ladder;
        const auto key = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
        const auto x = renderSynthNote (s, key, 48000);
        double e = 0.0, peak = 0.0;
        for (std::size_t i = 24000; i < 48000; ++i)
            e += (double) x[i] * x[i], peak = std::max (peak, (double) std::abs (x[i]));
        return std::pair { std::sqrt (e / 24000.0), peak };
    };
    const auto low = level (true, 0.0f, 110.0).first, high = level (true, 0.0f, 3520.0).first;   // 3 octaves above 500 Hz… 2.8
    const auto svfHigh = level (false, 0.0f, 3520.0).first / level (false, 0.0f, 110.0).first;
    CHECK (20.0 * std::log10 (high / low) < -55.0);               // ≈ 24 dB/oct over ~2.8 octaves
    CHECK (20.0 * std::log10 (svfHigh) > -45.0);                  // the 2-pole SVF falls much less
    const auto corner0 = level (true, 0.0f, 523.25).first, corner1 = level (true, 0.6f, 523.25).first;
    CHECK (corner1 > 1.5 * corner0);                               // resonance boosts near the cutoff
    const auto loud = level (true, 1.0f, 523.25);
    CHECK (std::isfinite (loud.first) && loud.second < 20.0);      // self-oscillation stays bounded
}

TEST (synth_spectrum_settles_on_target)
{
    // ALT DECAY-like morph: harmonic 3 starts at 1 and settles on 0.25 (−12 dB) instead of fading out
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::spectral, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;
    s.decay = 0.0f;
    s.sustain = 1.0f;
    s.spectrum[0] = 1.0f;
    s.spectrum[2] = 1.0f;
    s.spectrumDecay[2] = 0.1f;
    s.spectrumTarget[2] = 0.25f;
    const auto x = renderSynthNote (s, 57, 48000);
    const auto ratio = goertzel (x, 660.0, 24000, 26400) / goertzel (x, 220.0, 24000, 26400);
    CHECK (std::abs (10.0 * std::log10 (ratio) - 20.0 * std::log10 (0.25)) < 1.0);
}

TEST (kepler_chorus_is_stereo_and_keeps_level)
{
    // chorus I on a sine: L and R differ (opposite LFO phases), the level stays close to the dry one
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;
    auto dry = s;
    s.chorus = 1;
    auto render = [] (const lynx::SynthSettings& set) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
        inst->prepare (48000.0);
        lynx::ChannelDef c;
        c.synth = set;
        inst->configure (c, 120.0);
        inst->noteOn (69, 1.0f);
        std::vector<float> l (96000), r (96000);
        inst->render (l.data(), r.data(), 96000, 1.0f, 1.0f);
        return std::pair { l, r };
    };
    const auto [l, r] = render (s);
    const auto [dl, dr] = render (dry);
    double diff = 0.0, e = 0.0, ed = 0.0;
    for (std::size_t i = 48000; i < 96000; ++i)
        diff += std::abs (l[i] - r[i]), e += (double) l[i] * l[i], ed += (double) dl[i] * dl[i];
    CHECK (diff / 48000.0 > 1e-3);
    CHECK (std::abs (10.0 * std::log10 (e / ed)) < 3.0);
}

TEST (fl_pan_law_matches_love_philter_dll)
{
    // values printed by the installation's Love Philter pan routine for vol 0.8 (REA_rodada27 §3.1, lppan.txt)
    struct Row { double pan, ll, rr, rl, lr; };
    const Row rows[] = { { -1.5, 0.239782482, -0.432956904, 0.805467963, 0.0 }, { -0.5, 0.805467963, 0.432956904, 0.239782482, 0.0 },
                         { 0.0, 0.8, 0.8, 0.0, 0.0 }, { 0.75, 0.220719516, 0.707889318, 0.0, 0.401742548 },
                         { 1.0, 0.0, 0.565685451, 0.0, 0.565685451 } };
    for (const auto& row : rows)
    {
        float ll, rl, lr, rr;
        lynx::flPanMatrix (row.pan, ll, rl, lr, rr);
        CHECK (std::abs (0.8 * ll - row.ll) < 1e-6 && std::abs (0.8 * rr - row.rr) < 1e-6);
        CHECK (std::abs (0.8 * rl - row.rl) < 1e-6 && std::abs (0.8 * lr - row.lr) < 1e-6);
    }
}

TEST (effector_units_match_fl_dll_impulse_responses)
{
    // impulse responses of the installation's Effector engine (REA_rodada27 §2, eu_<kind>_<X>_<Y>.txt; LFO off,
    // wet 1, blocks of 256): samples 1, 10, 100 and 1000
    struct Case { int kind; float x, y; double v[4]; };
    const Case cases[] = { { 3, 0.5f, 0.5f, { -0.603055418, 0.159798339, -0.0055043716, -1.06241259e-06 } },
                           { 4, 0.2f, 0.5f, { 4.68885949e-26, 4.20032622e-19, 0.000487731799, 2.27715846e-09 } },
                           { 4, 0.8f, 0.3f, { -0.293783396, -0.0306414738, -0.00068128563, 1.358577e-15 } } };
    for (const auto& c : cases)
    {
        auto fx = lynx::effector::make();
        fx->prepare (44100.0);
        lynx::EffectParams p {};
        p[lynx::effector::type] = c.kind / 11.0f;
        p[lynx::effector::wetMix] = 1.0f;
        p[lynx::effector::paramX] = c.x;
        p[lynx::effector::paramY] = c.y;
        p[lynx::effector::modX] = p[lynx::effector::modY] = 0.5f;
        p[lynx::effector::modRate] = 0.5f;
        p[lynx::effector::inputLevel] = 1.0f;
        p[lynx::effector::outputGain] = 0.5f;
        std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
        l[0] = r[0] = 1.0f;
        for (int d = 0; d < 1024; d += 256)
            fx->process (l.data() + d, r.data() + d, 256, p, 120.0);
        const int at[4] { 1, 10, 100, 1000 };
        for (int k = 0; k < 4; ++k)
            CHECK (std::abs (l[(std::size_t) at[k]] - c.v[k]) < 2e-7 + 1e-5 * std::abs (c.v[k]));
    }
}

TEST (harmless_stereo_tremolo_and_vibrato)
{
    // tremolo d = 1: each side swings between 0 and 1.25 at 5 Hz, L and R in anti-phase; vibrato modulates the pitch
    lynx::SynthSettings s;
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    s.cutoff = 20000.0f;
    s.stereoTremolo = 1.0f;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::synth);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.synth = s;
    inst->configure (c, 120.0);
    inst->noteOn (69, 1.0f);
    std::vector<float> l (48000), r (48000);
    inst->render (l.data(), r.data(), 48000, 1.0f, 1.0f);
    auto envelope = [] (const std::vector<float>& x, int at) { float m = 0.0f; for (int i = at; i < at + 120; ++i) m = std::max (m, std::abs (x[(std::size_t) i])); return m; };
    // θ = 2π·2.5·t: at t = 0.4 s cos = 1 (L peak 1.25×), sin = 0 (R near 0); at t = 0.5 s the opposite
    CHECK (envelope (l, 19200) > 4.0f * envelope (r, 19200));
    CHECK (envelope (r, 24000) > 4.0f * envelope (l, 24000));

    // vibrato 100 cents: zero crossings vary in spacing; without it they are regular
    auto crossingsSpread = [] (float cents) {
        lynx::SynthSettings v;
        v.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
        v.cutoff = 20000.0f;
        v.vibratoCents = cents;
        const auto x = renderSynthNote (v, 69, 48000);
        std::vector<int> zc;
        for (int i = 24000; i < 48000; ++i)
            if (x[(std::size_t) i - 1] < 0.0f && x[(std::size_t) i] >= 0.0f)
                zc.push_back (i);
        int lo = 1 << 30, hi = 0;
        for (std::size_t k = 1; k < zc.size(); ++k)
            lo = std::min (lo, zc[k] - zc[k - 1]), hi = std::max (hi, zc[k] - zc[k - 1]);
        return hi - lo;
    };
    CHECK (crossingsSpread (0.0f) <= 1);
    CHECK (crossingsSpread (100.0f) >= 4);
}

TEST (physical_models_sound_and_end)
{
    // Sakura: a plucked string with default-ish parameters rings and fades after the release; Drumpad: a hit decays
    for (const auto kind : { lynx::InstrumentKind::sakura, lynx::InstrumentKind::drumpad })
    {
        lynx::ChannelDef c;
        c.instrument = kind;
        if (kind == lynx::InstrumentKind::sakura)
        {
            c.modelParams.assign (146, 0.5f);
            c.modelParams[111] = 3.0f;  // both strings at the note
            c.modelParams[113] = 0.5f;  // octave ×1
            c.modelParams[6] = c.modelParams[11] = 0.99f; // long decay
            c.modelParams[31] = 0.0f;   // noise exciter
        }
        else
        {
            c.modelParams.assign (38, 0.5f);
            c.modelParams[0] = 1.0f;    // retrigger noise
            c.modelParams[30] = c.modelParams[31] = c.modelParams[32] = c.modelParams[33] = 0.0f; // no velocity morph
        }
        auto inst = lynx::makeInstrument (kind);
        inst->prepare (48000.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (48000), r (48000);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        inst->noteOff (60);
        inst->render (l.data() + 24000, r.data() + 24000, 24000, 1.0f, 1.0f);
        double early = 0.0;
        for (int i = 0; i < 12000; ++i)
            early += (double) l[(std::size_t) i] * l[(std::size_t) i];
        CHECK (early > 1e-6);
        CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return std::isfinite (v); }));
        std::vector<float> tail (480000), tailR (480000);
        inst->render (tail.data(), tailR.data(), 480000, 1.0f, 1.0f); // 10 s later the voice has ended
        CHECK (inst->idle());
    }
}

TEST (squeeze_matches_fl_model)
{
    // Fruity Squeeze: Steps quantises, Mix 0 passes the input dry ×Volume/127; a silent block is left untouched
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::squeeze);
    slot.params[1] = 0.0f; // mix 0: dry only
    auto fx = lynx::makeEffect (lynx::EffectKind::squeeze);
    fx->prepare (44100.0);
    std::vector<float> l (512, 0.3f), r (512, -0.2f);
    fx->process (l.data(), r.data(), 512, slot.params, 120.0);
    CHECK (std::abs (l[100] - 0.3f) < 1e-6f && std::abs (r[100] + 0.2f) < 1e-6f); // Volume 127/127
    slot.params[1] = 1.0f;          // wet only
    slot.params[2] = 32000.0f / 32768.0f; // 32768^(1 − 32000/32768) ≈ 1.27 levels per unit → heavy quantisation
    std::vector<float> a (512), b (512);
    for (int i = 0; i < 512; ++i)
        a[(std::size_t) i] = b[(std::size_t) i] = 0.4f * (float) std::sin (i * 0.05);
    fx->process (a.data(), b.data(), 512, slot.params, 120.0);
    const auto levels = std::pow (32768.0f, 1.0f - 32000.0f / 32768.0f);
    for (int i = 0; i < 512; i += 37)
        CHECK (std::abs (a[(std::size_t) i] * levels - std::nearbyint (a[(std::size_t) i] * levels)) < 1e-3f);
    std::vector<float> z (512, 0.0f), z2 (512, 0.0f);
    fx->process (z.data(), z2.data(), 512, slot.params, 120.0);
    CHECK (z[10] == 0.0f);
}

TEST (sakura_matrix_and_effects)
{
    // REA_rodada29 §1: velocity → level follows 10^(2m)·0.05 (vel 1 vs 0.5 → ×10 in amplitude); the delay leaves
    // an echo after the note has ended and keeps the instrument from going idle until its tail is silent
    auto base = [] {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sakura;
        c.modelParams.assign (146, 0.0f);
        const std::pair<int, float> values[] { { 0, 0.375f }, { 1, 0.5f }, { 3, 0.5f }, { 4, 1.0f }, { 6, 0.95f }, { 9, 0.3f },
                                               { 11, 0.95f }, { 14, 0.3f }, { 18, 0.5f }, { 19, 0.5f }, { 27, 0.5f }, { 28, 1.0f },
                                               { 29, 0.7f }, { 31, 1.0f }, { 32, 0.5f }, { 111, 3.0f }, { 113, 0.5f } };
        for (const auto& [i, v] : values)
            c.modelParams[(std::size_t) i] = v;
        return c;
    };
    auto render = [] (const lynx::ChannelDef& c, float velocity, int held, int total) {
        auto inst = lynx::makeInstrument (lynx::InstrumentKind::sakura);
        inst->prepare (48000.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, velocity);
        std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
        for (int s = 0; s < total; s += 256)
        {
            if (s == held)
                inst->noteOff (60);
            inst->render (l.data() + s, r.data() + s, std::min (256, total - s), 1.0f, 1.0f);
        }
        return std::make_pair (l, std::move (inst));
    };
    auto energy = [] (const std::vector<float>& x, int from, int to) {
        double e = 0.0;
        for (int i = from; i < to; ++i)
            e += (double) x[(std::size_t) i] * x[(std::size_t) i];
        return e;
    };
    auto c = base();
    c.modelParams[79] = 6.0f; // slot 1: velocity → level, amount 1
    c.modelParams[80] = 1.0f;
    c.modelParams[82] = 1.0f;
    const auto loud = energy (render (c, 1.0f, 24064, 24064).first, 0, 24064);
    const auto soft = energy (render (c, 0.5f, 24064, 24064).first, 0, 24064);
    CHECK (loud > 1e-6 && std::abs (loud / soft - 100.0) < 1.0);

    auto d = base();
    auto [dry, dryInst] = render (d, 0.8f, 4864, 72000);
    d.modelParams[63] = 1.0f; // delay on: time 0.5 → ≈ 0.52 s, wet 1, no feedback: one echo
    d.modelParams[64] = 0.5f;
    d.modelParams[69] = 1.0f;
    auto [wet, wetInst] = render (d, 0.8f, 4864, 72000);
    CHECK (energy (wet, 26000, 40000) > 100.0 * energy (dry, 26000, 40000) + 1e-9); // the note ends near 0.2 s
    CHECK (dryInst->idle() || energy (dry, 70000, 72000) < 1e-12);
    CHECK (! wetInst->idle()); // the echo tail has not been silent for 3 s yet
}
