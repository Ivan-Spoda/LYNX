// --- REA round 153 (fltech153): FL's note colours, the piano roll Select menu and the chord readout -------------

#include "core/NoteTools.h"
#include "core/PianoRollTools.h"
#include "core/TempoDetect.h"
#include "core/ClipOps.h"

namespace
{
lynx::Note n153 (std::int64_t start, std::int64_t length, int key, int colour = 0, bool muted = false)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.colour = colour;
    n.muted = muted;
    return n;
}
} // namespace

TEST (rea153_flp_note_colour_is_imported)
{
    // FL keeps the note colour (0..15) in the low nibble of the note's 'extra' byte (bits 4/5 = porta / muted)
    flp::Project p;
    p.channels.push_back ({ 0, flp::ChannelType::sampler, "Lead", "", "", 1 });
    flp::Pattern pat;
    pat.index = 1;
    for (int c : { 0, 5, 15 })
    {
        flp::Note n;
        n.position = (std::uint32_t) (c * 96);
        n.length = 96;
        n.extra = (std::uint8_t) (c | 0x20); // muted as well: the colour must not take the flag bits
        pat.notes.push_back (n);
    }
    p.patterns.push_back (pat);
    const auto song = lynx::importFlp (p);
    CHECK (song.patterns.size() == 1);
    if (song.patterns.size() == 1 && song.patterns[0].notes.size() == 3)
    {
        CHECK (song.patterns[0].notes[0].colour == 0);
        CHECK (song.patterns[0].notes[1].colour == 5);
        CHECK (song.patterns[0].notes[2].colour == 15);
        CHECK (song.patterns[0].notes[2].muted);
    }
}

TEST (rea153_select_helpers_follow_fls_select_menu)
{
    using namespace lynx::notetools;
    lynx::Pattern p;
    p.notes = { n153 (0, 96, 60, 1), n153 (0, 96, 64, 2), n153 (96, 96, 60, 1, true), n153 (192, 96, 60),
                n153 (192, 96, 60),          // exact duplicate of note 3: stacked
                n153 (200, 40, 60),          // inside note 3: stacked and overlapping
                n153 (250, 100, 60) };       // starts inside note 3: overlapping, not stacked
    const std::vector<int> all { 0, 1, 2, 3, 4, 5, 6 };
    CHECK ((invertSelection (all, { 1, 3 }) == std::vector<int> { 0, 2, 4, 5, 6 }));
    CHECK ((invertSelection ({ 2, 0, 2 }, {}) == std::vector<int> { 0, 2 }));
    CHECK ((selectByColour (p, all, 1) == std::vector<int> { 0, 2 }));
    CHECK ((selectMuted (p, all) == std::vector<int> { 2 }));
    // time order: 0 (t0 k60), 1 (t0 k64), 2, 3, 4, 5, 6 -> every other one from the first
    CHECK ((selectOdd (p, all) == std::vector<int> { 0, 2, 4, 6 }));
    CHECK ((selectStacked (p, all) == std::vector<int> { 4, 5 }));
    CHECK ((selectOverlapping (p, all) == std::vector<int> { 4, 5, 6 }));
    // deleting the stacked notes leaves what is heard: the remaining notes still cover the same key/time
    const auto half = selectRandom (p, all, 0.5f, 7u);
    CHECK (half == selectRandom (p, all, 0.5f, 7u)); // deterministic for a seed
    CHECK (selectRandom (p, all, 0.0f, 3u).empty());
    CHECK (selectRandom (p, all, 1.0f, 3u) == all);
    setColour (p, { 3, 4 }, 9);
    CHECK (p.notes[3].colour == 9 && p.notes[4].colour == 9 && p.notes[5].colour == 0);
    setColour (p, { 0 }, 40);
    CHECK (p.notes[0].colour == lynx::noteColourCount - 1);
}

TEST (rea153_chord_detection_names_chords)
{
    using lynx::prtools::detectChord;
    auto name = [] (std::vector<int> keys, bool flats = false) {
        const auto c = detectChord (std::move (keys), flats);
        return c ? c->name : std::string ("-");
    };
    CHECK (name ({ 60, 64, 67 }) == "C");
    CHECK (name ({ 57, 60, 64 }) == "Am");
    CHECK (name ({ 64, 67, 72 }) == "C/E");          // first inversion
    CHECK (name ({ 55, 59, 62, 65 }) == "G7");
    CHECK (name ({ 60, 64, 67, 69 }) == "C6");       // the bass as the root wins over Am7/C
    CHECK (name ({ 57, 60, 64, 67 }) == "Am7");
    CHECK (name ({ 62, 65, 69, 72 }) == "Dm7");
    CHECK (name ({ 59, 62, 65, 69 }) == "Bm7b5");
    CHECK (name ({ 60, 63, 66, 69 }) == "Cdim7");
    CHECK (name ({ 60, 64, 68 }) == "Caug");
    CHECK (name ({ 60, 62, 67 }) == "Csus2");
    CHECK (name ({ 60, 65, 67 }) == "Csus4");
    CHECK (name ({ 60, 64, 67, 71 }) == "Cmaj7");
    CHECK (name ({ 60, 64, 67, 74 }) == "Cadd9");
    CHECK (name ({ 60, 64, 67, 70, 74 }) == "C9");
    CHECK (name ({ 40, 47 }) == "E5");               // power chord
    CHECK (name ({ 58, 62, 65 }, true) == "Bb");
    CHECK (name ({ 58, 62, 65 }) == "A#");
    CHECK (name ({ 48, 60, 64, 67, 72 }) == "C");    // doubled notes and octaves
    CHECK (name ({ 60 }) == "-");
    CHECK (name ({ 60, 72 }) == "-");               // one pitch class
    CHECK (name ({ 60, 61, 62 }) == "-");           // a cluster: no chord
    const auto c = detectChord ({ 64, 67, 72 });
    CHECK (c && c->root == 0 && c->bass == 4);
}

namespace
{
// a drum loop: a pitched kick on the beats, noise hats off the beat, a noise snare on 2 and 4, a little noise floor
std::vector<float> drums153 (double bpm, double seconds, double rate, unsigned seed)
{
    std::vector<float> x ((std::size_t) (seconds * rate));
    std::uint32_t state = seed * 2654435761u + 1u;
    auto noise = [&state] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 4294967296.0 * 2.0 - 1.0);
    };
    const auto beat = 60.0 / bpm;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const auto t = (double) i / rate;
        const auto kick = std::fmod (t, beat), hat = std::fmod (t + beat / 2.0, beat), snare = std::fmod (t + beat, 2.0 * beat);
        x[i] = 0.02f * noise() + (float) (0.8 * std::exp (-kick * 25.0) * std::sin (2.0 * 3.14159265358979 * (50.0 + 80.0 * std::exp (-kick * 40.0)) * kick))
               + (float) (0.15 * std::exp (-hat * 60.0)) * noise() + (float) (0.4 * std::exp (-snare * 20.0)) * noise();
    }
    return x;
}
} // namespace

TEST (rea153_detect_tempo_of_drum_loops)
{
    using namespace lynx::tempodetect;
    constexpr double rate = 44100.0;
    // free lengths (not whole beats): the tempo itself within 0.05 BPM
    for (const auto bpm : { 87.0, 100.0, 128.0, 140.0 })
    {
        const auto x = drums153 (bpm, 12.3, rate, (unsigned) bpm);
        const auto r = detect (x.data(), nullptr, x.size(), rate);
        CHECK (std::abs (r.bpm - bpm) < 0.05);
        CHECK (r.confidence > 0.5);
    }
    // a loop of exactly 8 beats: its length gives the tempo exactly, and the clip spans 8 beats
    for (const auto bpm : { 93.0, 127.0, 85.5 })
    {
        const auto x = drums153 (bpm, 8.0 * 60.0 / bpm, rate, 3u);
        const auto r = detect (x.data(), x.data(), x.size(), rate);
        CHECK (std::abs (r.bpm - bpm) < 0.002);
        CHECK (std::abs (r.beats - 8.0) < 0.001);
    }
    // the range decides the octave: 75 BPM with off-beat hats reads as 150 in 90-180
    {
        const auto x = drums153 (75.0, 12.3, rate, 9u);
        Options o;
        o.minBpm = 90.0;
        o.maxBpm = 180.0;
        CHECK (std::abs (detect (x.data(), nullptr, x.size(), rate, o).bpm - 150.0) < 0.1);
        o.minBpm = 60.0;
        o.maxBpm = 120.0;
        o.wholeBpm = true;
        CHECK (detect (x.data(), nullptr, x.size(), rate, o).bpm == 75.0);
    }
    // silence and too-short audio: no tempo
    const std::vector<float> silence ((std::size_t) (4.0 * rate), 0.0f);
    CHECK (detect (silence.data(), nullptr, silence.size(), rate).bpm == 0.0);
    CHECK (detect (silence.data(), nullptr, 1000, rate).bpm == 0.0);
}

TEST (rea153_playlist_select_overlapping_and_stacked_clips)
{
    lynx::Song song;
    auto clip = [] (int track, std::int64_t start, std::int64_t length, bool muted = false) {
        lynx::Clip c;
        c.track = track;
        c.start = start;
        c.length = length;
        c.muted = muted;
        return c;
    };
    song.playlist = { clip (0, 0, 100), clip (0, 0, 100),  // exact duplicate: stacked
                      clip (0, 20, 30),                     // inside clip 0: stacked
                      clip (0, 80, 100, true),              // overlaps the end of clip 0
                      clip (1, 50, 10), clip (0, 300, 10) };
    CHECK ((lynx::clipops::overlappingClips (song) == std::vector<int> { 1, 2, 3 }));
    CHECK ((lynx::clipops::stackedClips (song) == std::vector<int> { 1, 2 }));
    CHECK ((lynx::clipops::mutedClips (song) == std::vector<int> { 3 }));
}
