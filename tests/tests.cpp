// Testes do núcleo (sem JUCE). Uso: lynx_tests [pasta-com-arquivos-flp]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/LiveEventQueue.h"
#include "core/RackRenderer.h"
#include "core/Scheduler.h"
#include "core/SnapshotExchange.h"
#include "core/SoftClip.h"
#include "core/Song.h"
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
            std::printf ("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                                \
    } while (false)

std::string realFlpDir;

// --- utilitários para montar .flp sintéticos -------------------------------

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
        u32 (0); // tamanho não é usado pelo leitor
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
    b.data (0xD5, std::vector<std::uint8_t> (300, 7)); // varint de 2 bytes
    b.u8 (0xAC); b.u32 (0xC0000100); b.varint (3); b.u8 (1); b.u8 (2); b.u8 (3); // ID estendido

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
    b.u8 (0x9C); b.u8 (1); // u32 com só 1 byte
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
    CHECK (flp::playlistRecordSize (25002005, 60) == 60);  // tamanho não bate com a versão → tenta os outros
    CHECK (flp::playlistRecordSize (25002005, 7) == 0);
}

TEST (flp_parses_project_model)
{
    FlpBuilder b (96);
    b.ascii (0xC7, "25.2.5.5319");
    b.u8 (0x9C); b.u32 (174500);
    b.utf16 (0xC2, u"Título");
    b.u8 (0x41); b.u16 (3);
    auto notes = noteRecord (96, 1, 48, 64, 100);
    auto n2 = noteRecord (0, 0, 0, 60, 128);
    notes.insert (notes.end(), n2.begin(), n2.end());
    b.data (0xE0, notes);
    b.u8 (0x40); b.u16 (0);
    b.u8 (0x15); b.u8 (0);
    b.utf16 (0xCB, u"Kick");
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
    CHECK (p.channels[1].type == flp::ChannelType::instrument && p.channels[1].pluginName == "FLEX");
    CHECK (p.patterns.size() == 1 && p.patterns[0].index == 3);
    CHECK (p.patterns[0].notes.size() == 2 && p.patterns[0].notes[0].position == 0); // ordenadas
    CHECK (p.patterns[0].notes[1].key == 64 && p.patterns[0].notes[1].length == 48);
    CHECK (p.arrangements.size() == 1 && p.arrangements[0].items.size() == 1);
    const auto& it = p.arrangements[0].items[0];
    CHECK (it.kind == flp::PlaylistItem::Kind::pattern && it.index == 3 && it.track == 2 && it.position == 384);
    CHECK (p.mixer.size() == 2 && p.mixer[0].name == "Drums" && p.mixer[1].name.empty());
    CHECK (p.warnings.empty());
}

TEST (flp_real_files)
{
    if (realFlpDir.empty())
    {
        std::printf ("  (pulado: passe uma pasta com .flp como argumento)\n");
        return;
    }

    int files = 0, withNotes = 0, withPlaylist = 0;

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
            }

            if (! song.playlist.empty())
                ++withPlaylist;
        }
        catch (const flp::Error& e)
        {
            ++failures;
            std::printf ("  FALHOU %s: %s\n", entry.path().string().c_str(), e.what());
        }
    }

    std::printf ("  %d arquivos .flp lidos, %d com notas, %d com playlist\n", files, withNotes, withPlaylist);
    CHECK (files > 0);
}

// ---------------------------------------------------------------------------
// Sequenciador

TEST (pattern_toggle_step_and_length)
{
    lynx::Pattern p;
    CHECK (! p.hasStep (0, 4));
    p.toggleStep (0, 4);
    CHECK (p.hasStep (0, 4));
    CHECK (p.notes.size() == 1 && p.notes[0].start == 4 * lynx::ticksPerStep);
    CHECK (p.effectiveLength (4) == 4 * lynx::ppq); // um compasso
    p.toggleStep (1, 20);                            // segundo compasso
    CHECK (p.effectiveLength (4) == 8 * lynx::ppq);
    p.toggleStep (0, 4);
    CHECK (! p.hasStep (0, 4) && p.notes.size() == 1);
}

TEST (scheduler_places_events_at_exact_samples)
{
    lynx::Pattern p;
    p.toggleStep (0, 0);
    p.toggleStep (0, 4); // tick 960 = 1 tempo

    const double sr = 48000.0, bpm = 120.0; // 1 tempo = 0,5 s = 24000 samples
    const auto tps = lynx::ticksPerSample (bpm, sr);
    lynx::EventBuffer ev;

    lynx::schedulePatternLoop (p, p.effectiveLength (4), 23900.0 * tps, 512, tps, ev);
    // a nota do tempo 2 começa no sample 24000 → offset 100 neste bloco
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

    // bloco que atravessa o fim do loop: começa 10 samples antes
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
        x.publish (std::make_unique<Counted> (3, &alive)); // 2 nunca foi lido: liberado na hora
        CHECK (alive == 2);
        CHECK (x.acquire()->value == 3);
        CHECK (alive == 2); // o 1 foi devolvido, mas só a UI libera
        x.collectGarbage();
        CHECK (alive == 1);

        // sem coleta, as vagas de devolução enchem e o áudio segura o atual
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

    for (int block = 0; block < 93; ++block) // 47616 samples: logo antes de 1 s
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
    CHECK (kicks == 2); // 120 BPM: tempos em 0 e 0,5 s

    // editar a música preserva os instrumentos de canais que não mudaram de tipo
    song.channels[4].instrument = lynx::InstrumentKind::kick;
    auto next = lynx::RackState::build (song, state.get(), sr);
    CHECK (next->instruments[0] == state->instruments[0]);
    CHECK (next->instruments[4] != state->instruments[4]);

    // parar silencia o synth e não dispara notas novas
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
    CHECK (s.channels.size() == 2); // o clip de áudio fica de fora
    CHECK (s.channels[0].instrument == lynx::InstrumentKind::kick);
    CHECK (s.channels[1].instrument == lynx::InstrumentKind::synth);
    CHECK (s.patterns.size() == 1);
    CHECK (s.patterns[0].notes[0].start == lynx::ppq);           // 96 @ PPQ 96 = 1 tempo
    CHECK (s.patterns[0].notes[0].channel == 1);                // índice denso
    CHECK (s.patterns[0].notes[0].length == lynx::ticksPerStep); // nota de step
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
    CHECK (pushed == (int) lynx::LiveEventQueue::capacity - 1); // uma vaga fica livre para distinguir cheio de vazio
}

TEST (sampler_plays_pitched_one_shot)
{
    // sample de 1000 frames com rampa: dá para medir a posição pela amplitude
    auto data = std::make_shared<lynx::SampleData>();
    data->sampleRate = 48000.0;
    for (int i = 0; i < 1000; ++i)
        data->left.push_back (1.0f);

    auto sampler = lynx::makeInstrument (lynx::InstrumentKind::sampler, data);
    sampler->prepare (48000.0);
    std::vector<float> l (2048, 0.0f), r (2048, 0.0f);

    sampler->noteOn (60, 1.0f);
    sampler->noteOff (60); // one-shot: noteOff não corta
    sampler->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    int sounding = 0;
    for (auto s : l)
        sounding += s > 0.5f;
    CHECK (sounding >= 995 && sounding <= 1000); // toca o sample inteiro na altura original

    std::fill (l.begin(), l.end(), 0.0f);
    sampler->noteOn (72, 1.0f); // uma oitava acima: metade da duração
    sampler->render (l.data(), r.data(), 2048, 1.0f, 1.0f);
    sounding = 0;
    for (auto s : l)
        sounding += s > 0.5f;
    CHECK (sounding >= 495 && sounding <= 500);
    CHECK (r[10] > 0.5f); // mono vai para os dois lados

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
    renderer.process (state.get(), false, 0.0, 256, l.data(), r.data(), &live); // parado, mas a nota ao vivo toca
    CHECK (l[0] > 0.1f);

    // mesmo sample: reaproveita; sample diferente: recarrega
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
    CHECK (s.channels[1].instrument == lynx::InstrumentKind::hat); // sem arquivo: instrumento interno
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
    for (int step : { 0, 4, 8, 12 }) // uma nota por tempo
        p.toggleStep (0, step);

    // clip no compasso 2 tocando só a segunda metade da pattern (tempos 3 e 4)
    lynx::Clip c;
    c.pattern = 0;
    c.start = 4 * lynx::ppq;
    c.length = 2 * lynx::ppq;
    c.offset = 2 * lynx::ppq;
    s.playlist.push_back (c);
    CHECK (s.songLength() == 8 * lynx::ppq);

    const double sr = 48000.0;
    const auto tps = lynx::ticksPerSample (120.0, sr); // 1 tempo = 24000 samples
    lynx::EventBuffer ev;

    // música inteira em um bloco só (8 tempos = 192000 samples)
    lynx::scheduleSong (s, 0.0, 192000, tps, ev);
    std::vector<int> ons;
    for (auto& e : ev)
        if (e.type == lynx::NoteEvent::Type::on)
            ons.push_back (e.sampleOffset);
    CHECK (ons.size() == 2);
    CHECK (ons.size() == 2 && ons[0] == 4 * 24000 && ons[1] == 5 * 24000);

    // atravessando o loop: o compasso 1 está vazio, então nada no começo do segundo giro
    ev.clear();
    lynx::scheduleSong (s, 8.0 * lynx::ppq - 100 * tps, 200, tps, ev);
    CHECK (ev.size() == 0);

    // nota mais longa que o clip é cortada no fim do clip
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
    item.index = 1; // pattern vazia: descartada
    a.items.push_back (item);
    item.kind = flp::PlaylistItem::Kind::channelClip; // áudio: ainda não suportado
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
    n.length = 4 * lynx::ppq; // nota longa
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

    // salto para o tick 2000 (no meio da nota, sem "on" novo): o synth precisa calar
    for (int i = 0; i < 40; ++i)
        r.process (state.get(), true, 2000.0 + i * 512 * tps, 512, l.data(), rr.data());

    float tail = 0.0f;
    for (auto v : l)
        tail = std::max (tail, std::abs (v));
    CHECK (tail < 0.001f);
}

namespace
{
// Dois samplers de sinal constante 0,5 em inserts diferentes; retorna o pico da saída.
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

    std::vector<float> l (1000), rr (1000); // maior que o bloco do mixer: testa os pedaços
    r.process (state.get(), false, 0.0, 1000, l.data(), rr.data(), &live, meters);
    return std::max (std::abs (l[999]), std::abs (rr[999]));
}
} // namespace

TEST (mixer_routes_mutes_solos_and_meters)
{
    // dois canais de 0,5 somados no Master em 0 dB
    CHECK (std::abs (renderMixerCase ([] (lynx::Song&) {}) - 1.0f) < 1e-4f);

    // mute do insert 1: sobra só o canal 2
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[1].muted = true; }) - 0.5f) < 1e-4f);

    // solo no insert 2: idem
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[2].solo = true; }) - 0.5f) < 1e-4f);

    // fader do Master no máximo (1,0) = ganho 1/0,8
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[0].volume = 1.0f; }) - 1.25f) < 1e-4f);

    // os dois canais no mesmo insert, insert com fader a -6 dB (0,4)
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) {
               s.channels[1].mixerTrack = 1;
               s.mixer[1].volume = 0.4f;
           }) - 0.5f) < 1e-4f);

    // rota fora do mixer cai no último insert existente, sem travar
    CHECK (renderMixerCase ([] (lynx::Song& s) { s.channels[0].mixerTrack = 100; }) > 0.9f);

    // sem mixer: tudo direto na saída em 0 dB
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer.clear(); }) - 1.0f) < 1e-4f);

    lynx::MeterBank meters;
    renderMixerCase ([] (lynx::Song&) {}, &meters);
    CHECK (std::abs (meters.takePeak (1, 0) - 0.5f) < 1e-4f);
    CHECK (std::abs (meters.takePeak (0, 1) - 1.0f) < 1e-4f);
    CHECK (meters.takePeak (1, 0) == 0.0f); // ler zera
    CHECK (meters.takePeak (3, 0) == 0.0f); // insert sem canal
}

TEST (next_free_insert_and_flp_mixer_import)
{
    auto s = lynx::Song::makeDefault();
    CHECK (s.mixer.size() == 17 && s.mixer[0].name == "Master");
    CHECK (s.nextFreeInsert() == 6); // os 5 canais padrão ocupam 1..5

    flp::Project p;
    p.channels = { { 0, flp::ChannelType::sampler, "Kick", "", "", 30 } };
    p.mixer = { { 0, "" }, { 30, "Drums" } };
    const auto song = lynx::importFlp (p);
    CHECK (song.channels[0].mixerTrack == 30);
    CHECK (song.mixer.size() == 31);
    CHECK (song.mixer[30].name == "Drums" && song.mixer[0].name == "Master");
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

    std::printf ("\n%d verificações, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
