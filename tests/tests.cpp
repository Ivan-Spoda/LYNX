// Testes do núcleo (sem JUCE). Uso: lynx_tests [pasta-com-arquivos-flp]

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
#include "core/FlpImport.h"
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
    b.u8 (0x96); b.u32 (0x00A14547); // BGR no arquivo
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

    int files = 0, withNotes = 0, withPlaylist = 0, automations = 0, automationsWithTarget = 0, sends = 0;
    int threeOsc = 0, threeOscDecoded = 0, wrapped = 0, wrappedParsed = 0, markers = 0, signatures = 0;

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

            for (const auto& a : song.arrangements)
                for (const auto& m : a.markers)
                {
                    ++markers;
                    signatures += m.isSignature();
                    CHECK (m.tick >= 0);
                }

            // Fruity Wrapper (canais e slots do mixer): todo blob dos demos é lido
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

            // 3x Osc: todo estado dos demos decodifica, com níveis que somam no máximo 1
            for (const auto& c : p.channels)
                if (c.pluginName == "3x Osc")
                {
                    ++threeOsc;
                    lynx::SynthSettings s;

                    if (lynx::synthFrom3xOsc (c.pluginState, s))
                    {
                        ++threeOscDecoded;
                        const auto sum = s.osc[0].level + s.osc[1].level + s.osc[2].level;
                        CHECK (sum <= 1.001f);
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
                CHECK (! a.points.empty());

                for (std::size_t i = 1; i < a.points.size(); ++i)
                    CHECK (a.points[i].tick >= a.points[i - 1].tick);
            }

            // roteamento importado sem ciclos e com destinos válidos
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
            std::printf ("  FALHOU %s: %s\n", entry.path().string().c_str(), e.what());
        }
    }

    CHECK (threeOscDecoded == threeOsc);
    CHECK (wrappedParsed == wrapped);
    std::printf ("  %d plugins no Fruity Wrapper, %d lidos\n", wrapped, wrappedParsed);
    std::printf ("  %d marcadores, %d mudanças de compasso\n", markers, signatures);
    CHECK (markers > 300 && signatures > 60);
    std::printf ("  %d canais 3x Osc, %d decodificados\n", threeOsc, threeOscDecoded);
    std::printf ("  %d arquivos .flp lidos, %d com notas, %d com playlist, %d automações (%d com alvo), %d envios\n",
                 files, withNotes, withPlaylist, automations, automationsWithTarget, sends);
    CHECK (files > 0);
    CHECK (automations > 0 && sends > 0);
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
    CHECK (s.channels.size() == 3); // canais de clip de áudio ficam no rack, como no FL
    CHECK (s.channels[0].instrument == lynx::InstrumentKind::kick);
    CHECK (s.channels[1].instrument == lynx::InstrumentKind::audioClip);
    CHECK (s.channels[2].instrument == lynx::InstrumentKind::synth);
    CHECK (s.patterns.size() == 1);
    CHECK (s.patterns[0].notes[0].start == lynx::ppq);           // 96 @ PPQ 96 = 1 tempo
    CHECK (s.patterns[0].notes[0].channel == 2);
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

TEST (flp_import_3x_osc_state_and_channel_filter)
{
    // patch novo do 3x Osc (versão 14): osc 2 mix 64 coarse -12, osc 3 mix 32 coarse -24
    std::vector<std::uint8_t> blob (460, 0);
    auto put = [&] (std::size_t off, std::int32_t v) {
        for (int b = 0; b < 4; ++b)
            blob[off + (std::size_t) b] = (std::uint8_t) ((std::uint32_t) v >> (8 * b));
    };
    put (0x00, 14);
    put (0x08, 3);    // osc 1 serra
    put (0x10, 7);    // osc 1 fine +7
    put (0x04, -32);  // osc 1 pan -0,5
    put (0x1C, 64);   // osc 2 mix
    put (0x28, -12);  // osc 2 coarse
    put (0x38, 32);   // osc 3 mix
    put (0x40, 2);    // osc 3 quadrada
    put (0x44, -19);  // osc 3 coarse: -1 oitava e -7 semitons
    lynx::SynthSettings s;
    CHECK (lynx::synthFrom3xOsc (blob, s));
    CHECK (std::abs (s.osc[0].level - 0.375f) < 1e-4f && std::abs (s.osc[1].level - 0.375f) < 1e-4f && std::abs (s.osc[2].level - 0.25f) < 1e-4f);
    CHECK (s.osc[0].wave == lynx::Waveform::saw && s.osc[0].detune == 7.0f && s.osc[0].pan == -0.5f);
    CHECK (s.osc[1].wave == lynx::Waveform::sine && s.osc[1].octave == -1 && s.osc[1].semitones == 0);
    CHECK (s.osc[2].wave == lynx::Waveform::square && s.osc[2].octave == -1 && s.osc[2].semitones == -7);
    CHECK (! lynx::synthFrom3xOsc (std::vector<std::uint8_t> (40, 0), s));

    // filtro do canal: padrão aberto; LP com Mod X 128 ~ 2,71 kHz; Fast LP com Mod X 128 ~ 280 Hz
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
    lynx::synthFilterFromChannel (64, 0, 3, hz, res, mode); // passa-altas em ~840 Hz
    CHECK (mode == 2 && std::abs (hz - 840.0f) < 40.0f);
    lynx::synthFilterFromChannel (128, 0, 2, hz, res, mode); // passa-banda
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

    // "kick" do 3x Osc: seno com envelope de afinação +24 st caindo rápido
    lynx::SynthSettings kick;
    kick.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f };
    kick.cutoff = 20000.0f;
    kick.pitchEnv = { 24.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.05f };
    const auto k = render (kick, 48000);
    const auto early = crossings (k, 0, 2400);        // 50 ms iniciais: bem acima de 110 Hz
    const auto late = crossings (k, 24000, 48000);    // depois volta aos 110 Hz (~110 cruzamentos em 0,5 s)
    CHECK (early > 20); // reta de +24 st em 50 ms: ~2,2x a frequência média (11 cruzamentos sem envelope)
    CHECK (std::abs (late - 110) < 4);

    // envelope de corte: serra fechada em 200 Hz abre +5 oitavas no ataque e fecha de novo
    lynx::SynthSettings pluck;
    pluck.cutoff = 200.0f;
    pluck.filterEnv = { 5.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.1f };
    const auto p = render (pluck, 48000);
    auto hf = [&] (int from, int to) { // energia da diferença (agudos)
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
    inst->noteOn (69, 1.0f); // A4 assume a voz do A3: só uma nota soando
    inst->render (l.data() + 4800, r.data() + 4800, 43200, 1.0f, 1.0f);
    int crossings = 0;
    float peak = 0.0f;
    for (std::size_t i = 24001; i < 48000; ++i)
    {
        crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
        peak = std::max (peak, std::abs (l[i]));
    }
    CHECK (std::abs (crossings - 440) < 4); // 0,5 s de 440 Hz, sem o A3 por baixo
    CHECK (peak < 0.35f);                   // uma voz (0,3 de nível), não duas somadas
    // sem estalo na troca: o salto entre samples vizinhos fica no tamanho do seno
    float jump = 0.0f;
    for (std::size_t i = 4790; i < 4810; ++i)
        jump = std::max (jump, std::abs (l[i] - l[i - 1]));
    CHECK (jump < 0.05f);
}

TEST (sampler_channel_filter_darkens_and_bypasses_when_open)
{
    // ruído branco no sampler: com corte em 300 Hz a energia cai muito; aberto passa igual
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
        // passa-altas em 10 kHz: tira a maior parte da energia do ruído também, mas pelo outro lado
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
            lp += 0.01f * (v - lp); // passa-baixas grosseiro: quase nada deve sobrar embaixo
            lowE += (double) lp * lp;
        }
        CHECK (e > 10.0 && e < 0.7 * energy (20000.0f));
        CHECK (lowE < e * 0.01);
    }
    CHECK (open > 100.0);
    CHECK (dark < open * 0.05);

    // envelope de afinação +12 st com sustentação 1: o sample corre no dobro da velocidade
    // e acaba na metade do tempo (1 s de sample -> 0,5 s)
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
    s.osc[0] = { lynx::Waveform::sine, 0, 0.0f, 1.0f, 7, 1.0f }; // quinta acima, todo à direita
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
    // 440 * 2^(7/12) = 659,3 Hz; 0,4 s = 263,7 ciclos = ~527 cruzamentos
    CHECK (std::abs (crossings - 527) < 6);
}

TEST (fl_wrapper_state_parses_records_and_vst3_state)
{
    // blob do Fruity Wrapper (versão 12): tipo VST3 gerador, class ID, nome, caminho e estado
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

    // versão fora da faixa ou sem tipo: não é um wrapper que dá para usar
    blob[0] = 13;
    CHECK (! lynx::parseFlWrapper (blob, p));
    CHECK (! lynx::parseFlWrapper ({ 12, 0, 0, 0 }, p));
}

TEST (schedule_index_matches_full_scan)
{
    // música aleatória (notas longas, swing, clips com offset); o agendamento com índice tem que dar
    // exatamente os mesmos eventos que a varredura completa, inclusive no bloco que dá a volta
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
        const auto samples = 64 + rnd (24000); // até ~1 tempo por bloco
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

TEST (bars_follow_time_signature_markers)
{
    lynx::Song song;
    song.beatsPerBar = 4;
    const auto bar4 = (std::int64_t) lynx::ppq * 4;
    auto bars = song.barsBetween (0, bar4 * 3);
    CHECK (bars.size() == 3 && bars[2].tick == bar4 * 2 && bars[2].number == 3);

    // 7/4 a partir do compasso 3, e 3/4 no meio de um compasso de 7 (corta e recomeça)
    song.addMarker ({ bar4 * 2, "Verso", 7, 4 });
    song.addMarker ({ bar4, "Nome só", 0, 4 });
    const auto seven = (std::int64_t) lynx::ppq * 7;
    song.addMarker ({ bar4 * 2 + seven + lynx::ppq * 2, "", 3, 4 });
    bars = song.barsBetween (0, bar4 * 2 + seven * 2);
    CHECK (bars.size() >= 5);
    CHECK (bars[1].length == bar4 && bars[2].tick == bar4 * 2 && bars[2].length == seven);
    CHECK (bars[3].length == lynx::ppq * 2);                                // cortado pela mudança
    CHECK (bars[4].length == lynx::ppq * 3 && bars[4].number == 5);
    CHECK (song.markers()[0].name == "Nome só");                            // ordenados por tick

    // de longe: começa no compasso certo sem perder a numeração
    const auto far = song.barsBetween (lynx::ppq * 3000, lynx::ppq * 3001);
    CHECK (far.size() == 1 && far[0].length == lynx::ppq * 3);
    CHECK (song.barAt (lynx::ppq * 3000).number == far[0].number);
    CHECK ((lynx::ppq * 3000 - far[0].tick) < far[0].length);
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

    // curva do fader do FL: 0,8 = 0 dB, máximo ≈ +5,6 dB, metade do curso ≈ -12,7 dB
    CHECK (std::abs (lynx::faderGain (0.8f) - 1.0f) < 1e-5f);
    CHECK (std::abs (juce_db (lynx::faderGain (1.0f)) - 5.59f) < 0.05f);
    CHECK (lynx::faderGain (0.0f) == 0.0f);

    // fader do Master no máximo
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.mixer[0].volume = 1.0f; }) - lynx::faderGain (1.0f)) < 1e-4f);

    // os dois canais no mesmo insert, insert com fader em 0,4
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) {
               s.channels[1].mixerTrack = 1;
               s.mixer[1].volume = 0.4f;
           }) - lynx::faderGain (0.4f)) < 1e-4f);

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
    for (int i = frames / 2; i < frames; ++i) // segunda metade: já estabilizado
    {
        CHECK (std::isfinite (l[(std::size_t) i]));
        peak = std::max (peak, std::abs (l[(std::size_t) i]));
    }
    return peak;
}
} // namespace

TEST (parametric_eq_bands_and_response)
{
    // padrão: transparente
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 1000.0)) < 0.01);
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::parametricEq, [] (auto&) {}, 1000.0, 0.5f) - 0.5f) < 0.005f);

    // pico de +12 dB em 1 kHz na banda 4: a resposta e o sinal concordam
    auto boost = [] (lynx::EffectSlot& s) {
        s.params[(std::size_t) lynx::peq::param (3, 1)] = lynx::peq::hzValue (1000.0);
        s.params[(std::size_t) lynx::peq::param (3, 2)] = lynx::peq::gainValue (12.0);
    };
    boost (slot);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 1000.0) - 12.0) < 0.2);
    CHECK (std::abs (lynx::peq::responseDb (slot.params, 48000.0, 100.0)) < 1.0);
    const auto peak = runEffectOnSine (lynx::EffectKind::parametricEq, boost, 1000.0, 0.1f);
    CHECK (std::abs (peak - 0.1f * std::pow (10.0f, 12.0f / 20.0f)) < 0.01f);

    // corta graves a 48 dB/oit em 200 Hz: 50 Hz (2 oitavas abaixo) cai ~96 dB; 2 kHz passa
    auto cut = lynx::makeEffectSlot (lynx::EffectKind::parametricEq);
    cut.params[(std::size_t) lynx::peq::param (0, 0)] = lynx::peq::typeValue (lynx::peq::BandType::lowCut);
    cut.params[(std::size_t) lynx::peq::param (0, 1)] = lynx::peq::hzValue (200.0);
    cut.params[(std::size_t) (lynx::peq::slopeParam + 0)] = 1.0f;
    CHECK (lynx::peq::responseDb (cut.params, 48000.0, 50.0) < -80.0);
    CHECK (std::abs (lynx::peq::responseDb (cut.params, 48000.0, 200.0) + 3.0) < 0.5); // -3 dB no corte (Butterworth)
    CHECK (std::abs (lynx::peq::responseDb (cut.params, 48000.0, 2000.0)) < 0.1);

    // conversões ida e volta
    CHECK (std::abs (lynx::peq::hz (lynx::peq::hzValue (440.0)) - 440.0) < 0.5);
    CHECK (std::abs (lynx::peq::q (lynx::peq::qValue (2.5)) - 2.5) < 0.01);
    CHECK (lynx::peq::type (lynx::peq::typeValue (lynx::peq::BandType::notch)) == lynx::peq::BandType::notch);
}

TEST (effects_eq_compressor_behave)
{
    // EQ no padrão é transparente
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::eq, [] (auto&) {}, 1000.0, 0.5f) - 0.5f) < 0.01f);
    // +18 dB nos graves: 60 Hz sobe bastante, 5 kHz quase nada
    CHECK (runEffectOnSine (lynx::EffectKind::eq, [] (auto& s) { s.params[0] = 1.0f; }, 60.0, 0.05f) > 0.3f);
    CHECK (runEffectOnSine (lynx::EffectKind::eq, [] (auto& s) { s.params[0] = 1.0f; }, 5000.0, 0.05f) < 0.06f);
    // compressor: threshold -20 dB, ratio 20:1 achata um sinal de 0 dBFS para perto do threshold
    const auto squashed = runEffectOnSine (lynx::EffectKind::compressor, [] (auto& s) {
        s.params[0] = 40.0f / 60.0f;
        s.params[1] = 1.0f;
    }, 200.0, 1.0f);
    CHECK (squashed < 0.2f && squashed > 0.05f);
    // sinal abaixo do threshold passa intacto
    CHECK (std::abs (runEffectOnSine (lynx::EffectKind::compressor, [] (auto& s) { s.params[0] = 1.0f; }, 200.0, 0.3f) - 0.3f) < 0.01f);
}

TEST (effects_delay_echoes_on_tempo_and_reverb_has_tail)
{
    auto delay = lynx::makeEffect (lynx::EffectKind::delay);
    delay->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::delay);
    slot.params[0] = 3.5f / 8.0f; // índice 3 = 1/4 = 0,5 s a 120 BPM
    slot.params[1] = 0.0f;        // sem feedback: um eco só
    slot.params[2] = 1.0f;
    slot.params[4] = 0.0f;        // sem abafar
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
    for (int i = 4800; i < 9600; ++i) // entre 100 e 200 ms depois do impulso
        tail = std::max (tail, std::abs (l[(std::size_t) i]));
    CHECK (tail > 1e-4f && tail < 1.0f);
    CHECK (lynx::formatEffectParam (lynx::EffectKind::delay, 0, slot.params[0], 120.0) == "1/4 (500 ms)");
}

TEST (rack_runs_insert_effects_and_reuses_them)
{
    // insert com EQ a -inf não existe; usamos a saída do EQ em -18 dB para ver o efeito agir
    const auto quiet = renderMixerCase ([] (lynx::Song& s) {
        auto eq = lynx::makeEffectSlot (lynx::EffectKind::eq);
        eq.params[4] = 0.0f; // saída -18 dB
        s.mixer[1].effects.push_back (eq);
    });
    CHECK (quiet > 0.55f && quiet < 0.6f); // 0,5 do canal 2 + 0,5 * 0,126 do canal 1

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
    s.mixer[2].effects[0].params[0] = 0.9f; // mexer num knob não recria o efeito
    auto b = lynx::RackState::build (s, a.get(), 48000.0);
    CHECK (b->effects[2][0] == a->effects[2][0]);
    s.mixer[2].effects[0] = lynx::makeEffectSlot (lynx::EffectKind::delay);
    auto c = lynx::RackState::build (s, b.get(), 48000.0);
    CHECK (c->effects[2][0] != b->effects[2][0]);
}

TEST (audio_clips_trigger_cut_and_resume)
{
    lynx::Song s;
    s.bpm = 120.0; // 1 tempo = 0,5 s = 24000 samples a 48 kHz
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "ramp";
    c.volume = 1.0f;
    s.channels.push_back (c);
    s.patterns.emplace_back();

    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.start = lynx::ppq;      // começa no tempo 2
    clip.length = 2 * lynx::ppq; // dura 2 tempos
    clip.offset = lynx::ppq;     // pula o primeiro tempo do áudio
    s.playlist.push_back (clip);
    s.mode = lynx::PlayMode::song;

    const auto tps = lynx::ticksPerSample (120.0, 48000.0);
    lynx::EventBuffer ev;
    lynx::scheduleSong (s, 0.0, 4 * 24000, tps, ev);
    CHECK (ev.size() == 2);
    CHECK (ev[0].type == lynx::NoteEvent::Type::on && ev[0].sampleOffset == 24000 && std::abs (ev[0].startSeconds - 0.5) < 1e-9);
    CHECK (ev[1].type == lynx::NoteEvent::Type::off && ev[1].sampleOffset == 3 * 24000);

    // seek para o meio do clip (tempo 2,5): com resume, começa no primeiro sample do bloco
    ev.clear();
    lynx::scheduleSong (s, 1.5 * lynx::ppq, 256, tps, ev, true);
    CHECK (ev.size() == 1 && ev[0].sampleOffset == 0 && std::abs (ev[0].startSeconds - 0.75) < 1e-9);
    ev.clear();
    lynx::scheduleSong (s, 1.5 * lynx::ppq, 256, tps, ev, false);
    CHECK (ev.size() == 0);

    // sample com valor = posição em segundos: dá para conferir de onde ele começou a tocar
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

    // primeiro bloco tocando já no meio do clip
    r.process (state.get(), true, 1.5 * lynx::ppq, 256, l.data(), rr.data());
    CHECK (std::abs (l[0] - 0.75f) < 0.001f);

    // o clip acaba no tempo 3: depois do fade curto, silêncio (o sample ainda teria áudio)
    double tick = 3.0 * lynx::ppq - 10 * tps;
    r.process (state.get(), false, tick, 256, l.data(), rr.data()); // para (fade)
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
    flp::PlaylistItem beats; // offset em tempos: fim - início = duração em tempos
    beats.kind = flp::PlaylistItem::Kind::channelClip;
    beats.index = 0;
    beats.position = 192;
    beats.length = 384;          // 4 tempos
    beats.startOffset = 2.0f;
    beats.endOffset = 6.0f;
    flp::PlaylistItem ms = beats; // offset em ms: 4 tempos a 120 BPM = 2000 ms
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
    CHECK (s.playlist[1].offset == lynx::ppq); // 500 ms a 120 BPM = 1 tempo
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
    CHECK (a->instruments.size() == 2 && a->instruments[1] != nullptr); // ausente: mudo, mas existe

    // mudar o estado salvo não recria; trocar o plugin recria
    s.channels[0].plugin.state = "abc";
    auto b = lynx::RackState::build (s, a.get(), 48000.0, f);
    CHECK (instrumentsMade == 1 && b->instruments[0] == a->instruments[0] && b->effects[1][0] == a->effects[1][0]);
    s.mixer[1].effects[0].plugin.id = "VST3-Outro-1";
    auto c2 = lynx::RackState::build (s, b.get(), 48000.0, f);
    CHECK (effectsMade == 2);

    // renderizar com plugin ausente e slot de plugin não quebra nada
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
    auto pat = lynx::makeExportSong (s, true, lynx::ppq * 8, length); // Playlist vazia: usa a pattern
    CHECK (length == lynx::ppq * 4);
    CHECK (pat.mode == lynx::PlayMode::song && pat.playlist.size() == 2);
    CHECK (pat.songLength() >= length + lynx::ppq * 8);

    // nenhuma nota nova depois do fim da música, só silêncio até o fim da cauda
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

    // com Playlist: o fim é o do último clip, sem arredondar
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

    // ordenar acompanha os índices
    p.notes[0].start = 1200;
    std::vector<int> tracked { 0, 2 };
    ne::sortTracking (p, tracked);
    CHECK (p.notes[2].start == 1200 && p.notes[(std::size_t) tracked[0]].start == 1200);
    CHECK (p.notes[(std::size_t) tracked[1]].key == 67);

    // mover em grupo respeita os limites
    std::int64_t dt = -5000;
    int dk = 100;
    ne::move (p, { 0, 1 }, dt, dk);
    CHECK (dt == -480 && dk == 127 - 67 && p.notes[0].start == 0 && p.notes[1].key == 127);

    // duplicar: logo depois do trecho, arredondado à grade
    lynx::Pattern q;
    q.notes = { { 0, 240, 0, 60 }, { 240, 300, 0, 62 } }; // trecho de 540 ticks
    const auto added = ne::duplicate (q, { 0, 1 }, 960);
    CHECK (added.size() == 2 && q.notes[(std::size_t) added[0]].start == 960 && q.notes[(std::size_t) added[1]].start == 1200);

    // colar em outro canal com deslocamento
    const auto pasted = ne::paste (q, ne::copy (q, { 1 }), 100, 3);
    CHECK (pasted.size() == 1 && q.notes.back().channel == 3 && q.notes.back().start == 340);

    // quantizar arredonda para o mais próximo
    q.notes[0].start = 130;
    q.notes[1].start = 100;
    ne::quantize (q, { 0, 1 }, 240);
    CHECK (q.notes[0].start == 240 && q.notes[1].start == 0);

    // remover vários (ordem e repetidos não importam)
    const auto before = q.notes.size();
    ne::remove (q, { 1, 0, 1 });
    CHECK (q.notes.size() == before - 2);
}

TEST (recorder_builds_notes_in_pattern_and_song_mode)
{
    auto song = lynx::Song::makeDefault();
    const auto loop = song.patterns[0].effectiveLength (4); // 1 compasso = 3840
    lynx::NoteRecorder r;
    r.add (song, { 100.0, 4, 60, 0.5f, true }); // sem tomada: ignorado
    CHECK (r.takeFinished().empty());

    r.begin (loop);
    r.add (song, { 4000.0, 4, 60, 0.5f, true });   // segunda volta do loop
    r.add (song, { 4480.0, 4, 60, 0.0f, false });
    r.add (song, { 5000.0, 4, 64, 0.9f, true });   // presa até parar
    r.end (song, 5240.0);
    const auto notes = r.takeFinished();
    CHECK (notes.size() == 2);
    CHECK (notes[0].start == 4000 - 3840 && notes[0].length == 480 && notes[0].velocity == 0.5f && notes[0].channel == 4);
    CHECK (notes[1].key == 64 && notes[1].start == 5000 - 3840 && notes[1].length == 240);

    // modo song: posição dentro do clip da pattern atual (com offset)
    song.mode = lynx::PlayMode::song;
    song.playlist = { { 0, 0, 7680, 3840, 960 } };
    CHECK (lynx::patternPositionForTick (song, 7680.0 + 100.0, loop) == 1060);
    CHECK (lynx::patternPositionForTick (song, 100.0, loop) == 100); // fora do clip: como loop
}

TEST (automation_curve_and_playback)
{
    lynx::Automation a;
    a.points = { { 0, 0.0f }, { 960, 1.0f }, { 1920, 0.5f, 0.0f } };
    CHECK (a.valueAt (-10.0) == 0.0f && a.valueAt (480.0) == 0.5f && a.valueAt (5000.0) == 0.5f);
    CHECK (a.length() == 1920);
    a.points[0].tension = 1.0f; // curva: começa devagar
    CHECK (a.valueAt (480.0) < 0.2f && a.valueAt (480.0) > 0.0f);

    // Master automatizado: silêncio no primeiro compasso, som no segundo, e o último valor
    // fica valendo depois do fim do clip de automação
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
    CHECK (peakBar3 > 0.05f); // depois do clip: segue no último valor

    // alvo removido: automação some das tabelas
    song.removeChannel (0);
    song.automations[0].target = { lynx::AutomationTargetKind::channelVolume, 0 };
    song.removeChannel (0);
    CHECK (song.automations[0].target.kind == lynx::AutomationTargetKind::none);
    song.removeAutomation (0);
    CHECK (song.automations.empty() && song.playlist.size() == 3);
}

TEST (mixer_sends_between_inserts)
{
    // insert 1 vai só para o 3; o 3 vai ao Master: soma igual (0,5 + 0,5)
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); }) - 1.0f) < 1e-4f);

    // envio a 50% e destino mudo
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true, 0.5f); s.setRoute (1, 0, false); }) - 0.75f) < 1e-4f);
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[3].muted = true; }) - 0.5f) < 1e-4f);

    // indo ao Master e ao 3: o sinal dobra
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); }) - 1.5f) < 1e-4f);

    // solo no 3 deixa soar quem chega nele (insert 1), mas não o 2
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[3].solo = true; }) - 0.5f) < 1e-4f);
    // solo no 1 deixa soar o caminho até o Master (o 3)
    CHECK (std::abs (renderMixerCase ([] (lynx::Song& s) { s.setRoute (1, 3, true); s.setRoute (1, 0, false); s.mixer[1].solo = true; }) - 0.5f) < 1e-4f);

    // ordem e ciclos
    auto song = lynx::Song::makeDefault();
    song.setRoute (4, 2, true);
    song.setRoute (2, 1, true);
    CHECK (! song.canRoute (1, 4) && ! song.canRoute (1, 2) && song.canRoute (1, 5));
    song.setRoute (1, 4, true); // ignorado: criaria ciclo
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
// Efeito de teste: atrasa o sinal em `latency` samples e informa essa latência (como um plugin com lookahead).
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
    // mixInto = process sobre source*level, somado no destino; em trechos que cruzam o anel
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

    // fonte calada: a cauda que estava na linha ainda sai, depois não sobra nada
    std::vector<float> tl (64, 0.0f), tr (64, 0.0f);
    b.mixInto (nullptr, nullptr, 1.0f, tl.data(), tr.data(), 64);
    CHECK (std::abs (tl[0] - sl[263] * 0.25f) < 1e-6f);
    std::vector<float> zl (64, 0.0f), zr (64, 0.0f);
    b.mixInto (nullptr, nullptr, 1.0f, zl.data(), zr.data(), 64);
    CHECK (*std::max_element (zl.begin(), zl.end()) == 0.0f && *std::min_element (zl.begin(), zl.end()) == 0.0f);
}

TEST (limiter_lookahead_holds_ceiling_on_bursts)
{
    // rajadas curtas sobre um fundo baixo: o máximo deslizante precisa ver cada pico antes de sair
    auto fx = lynx::makeEffect (lynx::EffectKind::limiter);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::limiter);
    slot.params[2] = 0.0f; // release de 10 ms
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
    for (int i = 9973 * 2 + 2000; i < 9973 * 2 + 2400; ++i) // 40 ms depois da rajada o ganho já voltou
        quiet = std::max (quiet, std::abs (r[(std::size_t) i]));
    const auto ceiling = 1.0f; // o teto padrão fica em 0 dBFS ou abaixo
    CHECK (peak <= ceiling + 1e-5f);
    CHECK (quiet > 0.03f);
}

TEST (pdc_aligns_paths_through_latent_effects)
{
    // plano: canal 0 -> insert 1 (efeito de 100) -> insert 3; canal 1 -> insert 2 -> Master
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
    CHECK (plan.inAlign[3] == 100 && plan.channelDelay[2] == 70); // instrumento de 30 no insert 3
    CHECK (plan.sendDelay[1][1] == 0);                             // o caminho mais lento não atrasa
    CHECK (plan.sendDelay[2][0] == 100 && plan.sendDelay[3][0] == 0 && plan.total == 100);

    // bypass zera a latência do slot
    s.mixer[1].effects[0].enabled = false;
    CHECK (lynx::computePdc (s, { 0, 0, 30 }, { {}, { 100 } }).total == 30);

    // renderizado: dois impulsos tocados juntos saem juntos
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
    CHECK (first == 100 && std::abs (l[100] - 2.0f) < 1e-3f); // os dois impulsos chegam juntos, somados

    // editar sem mudar as latências reaproveita as linhas
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
    CHECK (l[0] > 0.45f && l[128] > 0.2f && l[128] < 0.3f && std::abs (l[255]) < 1e-6f); // rampa até zero
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
    CHECK (errorSum / (double) x.size() < 4.0 / 32768.0); // poucos LSB de ruído em média
}

TEST (parallel_rendering_matches_sequential)
{
    // 32 canais (vale a pena paralelizar), inserts com envios em cadeia
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

    // o pool aguenta muitas rodadas pequenas seguidas
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

    // remover a pattern 0 conserta os clips guardados no outro arrangement
    s.removePattern (0);
    CHECK (s.playlist.size() == 1 && s.playlist[0].pattern == 0);
    CHECK (s.arrangements[1].clips.size() == 1 && s.arrangements[1].clips[0].pattern == 0);

    s.removeArrangement (0);
    CHECK (s.arrangements.size() == 1 && s.currentArrangement == 0 && s.playlist.size() == 1 && s.loopEnd == 0);
}

namespace
{
// Efeito de teste que inverte o sinal e conta quantas vezes processou.
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
    fx.mix = 0.25f; // 75% seco (+0,5) e 25% molhado (-0,5) = +0,25
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

    // depois que o sample acaba: 4 s de silêncio e o plugin deixa de ser chamado
    for (int i = 0; i < 100 * 6; ++i) // 6 s
        r.process (state.get(), false, 0.0, 480, l.data(), rr.data());
    const auto callsAfterSilence = inverter->calls;
    for (int i = 0; i < 100; ++i)
        r.process (state.get(), false, 0.0, 480, l.data(), rr.data());
    CHECK (inverter->calls == callsAfterSilence);
    CHECK (callsAfterSilence < 100 * 6);

    // som de novo: volta a processar no mesmo bloco
    live.push (on);
    r.process (state.get(), false, 0.0, 480, l.data(), rr.data(), &live);
    CHECK (inverter->calls == callsAfterSilence + 1 && std::abs (l[100] - 0.25f) < 1e-4f);
}

TEST (swing_delays_offbeat_sixteenths)
{
    CHECK (lynx::swingDelay (0, 1.0f) == 0);
    CHECK (lynx::swingDelay (lynx::ticksPerStep, 1.0f) == lynx::ticksPerStep / 2);
    CHECK (lynx::swingDelay (lynx::ticksPerStep * 2, 1.0f) == 0);
    CHECK (lynx::swingDelay (lynx::ticksPerStep + 10, 1.0f) == (lynx::ticksPerStep - 20) / 2); // fora da grade: peso triangular (FL)
    CHECK (lynx::swingDelay (lynx::ticksPerStep, 1.0f, 0.5f) == lynx::ticksPerStep / 4); // mix do canal
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
        ramp->left.push_back ((float) i / 48000.0f); // rampa: o valor diz onde a leitura está
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

    // +12 semitons: lê o dobro
    lynx::SamplerSettings up;
    up.pitch = 12.0f;
    CHECK (std::abs (play (up, 60, 1000, false)[999] - 2.0f * 999.0f / 48000.0f) < 1e-3f);

    // ao contrário: começa do fim
    lynx::SamplerSettings back;
    back.reverse = true;
    CHECK (play (back, 60, 100, false)[10] > 0.99f);

    // ataque de 10 ms: começa em silêncio e chega ao nível cheio
    lynx::SamplerSettings slow;
    slow.attack = 0.01f;
    const auto a = play (slow, 60, 2000, false);
    CHECK (a[10] < 0.2f * (10.0f / 48000.0f) + 1e-6f && std::abs (a[1500] - 1500.0f / 48000.0f) < 1e-4f);

    // release de 5 ms: some logo depois de soltar; sem release toca inteiro
    lynx::SamplerSettings rel;
    rel.release = 0.005f;
    CHECK (play (rel, 60, 2000, true)[1999] == 0.0f);
    CHECK (play (lynx::SamplerSettings {}, 60, 2000, true)[1999] > 0.0f);

    // cut itself: a segunda nota corta a primeira (sobra uma voz)
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
    CHECK (std::abs (l[1999] - 1999.0f / 48000.0f) < 1e-4f); // só a nota nova
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
    CHECK (ons == 1); // a mutada não toca

    // pan da nota no sampler: tudo à esquerda
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
    tempo.points = { { 0, v120 }, { bar - 1, v120 }, { bar, v240 } }; // degrau: 120 -> 240 no 2º compasso
    song.automations.push_back (tempo);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.track = 1;
    clip.length = bar * 2;
    song.playlist.push_back (clip);

    CHECK (std::abs (song.tempoAt (100.0) - 120.0) < 0.01 && std::abs (song.tempoAt ((double) bar + 100.0) - 240.0) < 0.01);
    // 1 compasso a 120 (2 s) + 1 a 240 (1 s)
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
    t.stereo = -1.0f; // lado dobrado: 1,5 L - 0,5 R
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

    // limiter: +12 dB num seno de 0,9 nunca passa do teto (-1 dB) e informa a latência
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

    // filtro passa-baixas em 500 Hz derruba um seno de 8 kHz
    auto lp = lynx::makeEffect (lynx::EffectKind::filter);
    lp->prepare (sr);
    auto fs = lynx::makeEffectSlot (lynx::EffectKind::filter);
    fs.params[1] = std::log (500.0f / 20.0f) / std::log (1000.0f);
    l = sine (8000.0f, 1.0f, 9600);
    r = l;
    lp->process (l.data(), r.data(), 9600, fs.params, 120.0);
    CHECK (peak (l, 4800) < 0.05f);

    // distorção e chorus: saída finita e com som
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
    // sampler com rampa: a velocidade de leitura dobra depois do slide de uma oitava
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

    // o sequenciador manda "slide" (sem off) para a nota slide
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

    // padrão: soa e some depois do release
    const lynx::SynthSettings standard;
    const auto a = render (standard, 48000, 24000);
    CHECK (peakIn (a, 1000, 20000) > 0.05f && peakIn (a, 40000, 48000) < 1e-3f);

    // seno puro com filtro aberto: a contagem de cruzamentos por zero dá ~440 Hz
    lynx::SynthSettings sine;
    sine.osc[0].wave = lynx::Waveform::sine;
    sine.cutoff = 20000.0f;
    const auto b = render (sine, 48000, 47000);
    int crossings = 0;
    for (int i = 4800; i < 4800 + 48000 / 2; ++i)
        crossings += (b[(std::size_t) i - 1] < 0.0f) != (b[(std::size_t) i] < 0.0f);
    CHECK (std::abs (crossings - 440) < 6); // meio segundo = 220 ciclos = 440 cruzamentos
    float sinePeak = 0.0f;
    for (int i = 4800; i < 9600; ++i)
        sinePeak = std::max (sinePeak, std::abs (b[(std::size_t) i]));
    CHECK (sinePeak > 0.2f && sinePeak < 0.35f); // 0,3 de nível, seno aproximado sem estouro

    // segundo oscilador uma oitava acima soma energia; sustain 0 faz a nota decair sozinha
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

    CHECK (! s.setTargetValue ({ lynx::AutomationTargetKind::channelParam, 0, 0, 3 }, 0.5f)); // mora no plugin
}

TEST (cut_groups_stretch_and_envelope_import)
{
    // hat aberto (cut by 1) é cortado quando o fechado (grupo 1) toca
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
    s.channels[0].cutBy = 1;    // aberto
    s.channels[1].cutGroup = 1; // fechado
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
        if (tick > 960.0 * 2) // o fechado segue; o aberto já parou
            afterCut = std::max (afterCut, std::abs (l[256]));
    }
    CHECK (std::abs (afterCut - 0.5f * 0.78f) < 0.01f); // só o fechado (velocity padrão 0,78)

    // stretch por reamostragem: 1 s de sample esticado para 4 tempos a 120 BPM = 2 s
    auto ramp = std::make_shared<lynx::SampleData>();
    for (int i = 0; i < 48000; ++i)
        ramp->left.push_back ((float) i / 48000.0f);
    ramp->sampleRate = 48000.0;
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::sampler, ramp);
    inst->prepare (48000.0);
    lynx::ChannelDef c;
    c.sampler.stretchBeats = 4.0f;
    inst->configure (c, 120.0);
    inst->noteOn (72, 1.0f); // a altura da nota não muda a velocidade
    std::vector<float> a (48000), b (48000);
    inst->render (a.data(), b.data(), 48000, 1.0f, 1.0f);
    CHECK (std::abs (a[47999] - 0.5f) < 0.01f); // depois de 1 s leu metade

    // tempo do envelope do FL: 32950 = 2 s; < 101 = 0
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
        CHECK (std::abs (crossingsPerSecond (*out) - 880.0) < 15.0); // 440 Hz continua 440 Hz

        float peak = 0.0f;
        for (std::size_t i = 4800; i + 4800 < out->frames(); ++i)
            peak = std::max (peak, std::abs (out->left[i]));
        CHECK (peak > 0.4f && peak < 0.6f); // sem buracos nem estouro na sobreposição
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
    CHECK (std::abs (l[0] - 0.25f) < 0.001f);   // começa no início do trecho
    CHECK (l[2400] > 0.48f && l[2600] == 0.0f); // e para no fim dele
}

TEST (arpeggiator_plays_held_chord_in_steps)
{
    auto song = lynx::Song::makeDefault();
    song.bpm = 120.0;
    song.patterns[0].notes.clear();
    for (int key : { 60, 64, 67 })
        song.patterns[0].notes.push_back ({ 0, lynx::ppq * 4, 4, key, 0.8f }); // acorde segurado 1 compasso
    song.channels[4].arp.direction = 1; // sobe
    song.channels[4].arp.stepBeats = 0.25f;

    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    CHECK (state->arpeggiators[4] != nullptr && state->arpeggiators[0] == nullptr);

    // o arpejador gera as notas; contamos o que ele entrega ao instrumento
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

    // 2 tempos = 8 passos de semicolcheia: 60 64 67 60 64 67 60 64
    CHECK (keys.size() >= 8); // o último bloco passa um pouco dos 2 tempos
    const int expected[] = { 60, 64, 67, 60, 64, 67, 60, 64 };
    for (std::size_t i = 0; i < keys.size() && i < 8; ++i)
        CHECK (keys[i] == expected[i]);

    // renderizando de verdade também soa
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
    clip.length = lynx::ppq * 4;  // 2 s a 120 BPM
    clip.fadeIn = lynx::ppq;      // 0,5 s
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

    CHECK (all[100] < 0.01f);                            // começo do fade-in
    CHECK (std::abs (all[12000] - 0.25f) < 0.02f);       // meio do fade-in: metade de 0,5
    CHECK (std::abs (all[48000] - 0.5f) < 0.01f);        // corpo: o ganho do clip
    CHECK (std::abs (all[96000 - 12000] - 0.25f) < 0.02f); // meio do fade-out
}

TEST (slicer_detects_hits_and_plays_slices_by_key)
{
    // 4 batidas (ruído com decaimento) a cada 0,25 s
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

    // a tecla 61 toca a segunda fatia, que começa em 0,25 e acaba antes de 0,5
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

    std::printf ("\n%d verificações, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
