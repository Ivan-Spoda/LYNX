// UI fix round (uifix): press-to-preview notes always stop (core/HeldNotes.h). Included at the end of tests.cpp.

#include "core/HeldNotes.h"

namespace
{
struct NoteLog
{
    std::vector<std::tuple<int, int, bool>> events; // channel, key, on

    lynx::HeldNotes::Sender sender()
    {
        return [this] (int channel, int key, float, bool on) { events.emplace_back (channel, key, on); };
    }

    // every note-on has exactly one later note-off for the same channel and key
    bool balanced() const
    {
        std::map<std::pair<int, int>, int> open;
        for (const auto& [c, k, on] : events)
        {
            auto& n = open[{ c, k }];
            n += on ? 1 : -1;
            if (n < 0 || n > 1)
                return false;
        }
        for (const auto& [ck, n] : open)
            if (n != 0)
                return false;
        return true;
    }
};
} // namespace

TEST (held_notes_press_release_pairs)
{
    NoteLog log;
    {
        lynx::HeldNotes held (log.sender());
        held.press (2, 60, 0.8f);
        CHECK (held.isHeld (2, 60) && held.size() == 1);
        held.release (2, 60);
        CHECK (held.empty());
        held.release (2, 60); // a second release sends nothing
        CHECK (log.events.size() == 2);
        CHECK (log.balanced());
    }
}

TEST (held_notes_repeat_press_never_doubles)
{
    NoteLog log;
    lynx::HeldNotes held (log.sender());
    // painting steps over the same row, key repeat, a click on a key already held
    held.press (0, 60, 0.8f);
    held.press (0, 60, 0.8f);
    held.press (0, 60, 0.8f);
    CHECK (held.size() == 1);
    held.releaseAll();
    CHECK (held.empty());
    CHECK (log.balanced());
    CHECK (log.events.size() == 6);
}

TEST (held_notes_release_all_on_gesture_end)
{
    NoteLog log;
    {
        lynx::HeldNotes held (log.sender());
        // a vertical drag over the channel rack's steps: several rows pressed, the mouse released elsewhere
        for (int row = 0; row < 5; ++row)
            held.press (row, 60, 0.78f);
        held.releaseAll();
        CHECK (held.empty());
        CHECK (log.balanced());

        // destroyed mid-gesture (window closed while a key is down): the destructor stops it
        held.press (7, 64, 0.8f);
    }
    CHECK (log.balanced());
}

TEST (held_notes_tags_release_their_notes)
{
    NoteLog log;
    lynx::HeldNotes held (log.sender());
    // the typing piano: key 'Z' plays on channels 1 and 3, key 'X' on channel 1
    held.press (1, 48, 0.8f, 'Z');
    held.press (3, 48, 0.8f, 'Z');
    held.press (1, 50, 0.8f, 'X');
    CHECK (held.hasTag ('Z') && held.hasTag ('X'));
    int tags = 0;
    held.forEachTag ([&tags] (int) { ++tags; });
    CHECK (tags == 2);
    held.releaseTag ('Z');
    CHECK (! held.hasTag ('Z') && held.isHeld (1, 50) && ! held.isHeld (3, 48));
    held.releaseTag ('X');
    CHECK (held.empty() && log.balanced());
}

TEST (held_notes_off_goes_to_the_channel_that_started)
{
    NoteLog log;
    lynx::HeldNotes held (log.sender());
    held.press (4, 72, 0.8f); // the Piano Roll previews on channel 4 ...
    // ... the user switches to channel 9 while the key is held; the release must still reach channel 4
    held.releaseAll();
    CHECK (log.events.size() == 2 && std::get<0> (log.events[1]) == 4 && std::get<1> (log.events[1]) == 72);
    CHECK (! std::get<2> (log.events[1]));
}

TEST (key_ledger_off_uses_the_snapped_key)
{
    lynx::KeyLedger ledger;
    ledger.on (0, 61, 60);              // C# snapped to C by the scale
    CHECK (ledger.off (0, 61, 62) == 60); // the snap changed meanwhile (would give 62): the off still stops 60
    CHECK (ledger.off (0, 61, 62) == 62); // nothing open: the fallback
    ledger.on (0, 61, 60);
    ledger.on (0, 61, 61);
    CHECK (ledger.off (0, 61, 0) == 60 && ledger.off (0, 61, 0) == 61 && ledger.size() == 0);
}

TEST (input_held_notes_follow_the_note_on)
{
    lynx::InputHeldNotes held;
    lynx::InputHeldNotes::Target on[2] = { { 0, 60 }, { 3, 60 } };
    lynx::InputHeldNotes::Target out[lynx::InputHeldNotes::maxTargets];
    CHECK (held.noteOn (60, on, 2, out) == 0);
    // the selection changed to channel 5 while the key was down: the note-off still goes to 0 and 3
    const auto n = held.noteOff (60, out);
    CHECK (n == 2 && out[0].channel == 0 && out[1].channel == 3 && out[0].key == 60);
    CHECK (held.noteOff (60, out) == 0); // a stray note-off stops nothing

    // two note-ons without a note-off between them: the first targets come back as stale
    held.noteOn (64, on, 1, out);
    lynx::InputHeldNotes::Target again[1] = { { 5, 64 } };
    CHECK (held.noteOn (64, again, 1, out) == 1 && out[0].channel == 0);
    CHECK (held.noteOff (64, out) == 1 && out[0].channel == 5);
    CHECK (held.noteOn (200, on, 2, out) == 0 && held.noteOff (-1, out) == 0);
}

// --- clip menu operations (core/ClipOps.h) ----------------------------------------------------------

#include "core/ClipOps.h"

namespace
{
lynx::Song clipSong()
{
    lynx::Song s;
    s.bpm = 120.0;
    s.channels.clear();
    lynx::ChannelDef audio;
    audio.name = "Loop";
    audio.instrument = lynx::InstrumentKind::audioClip;
    audio.samplePath = "loop.wav";
    s.channels.push_back (audio);
    s.playlist.clear();
    lynx::Clip c;
    c.kind = lynx::ClipKind::audio;
    c.channel = 0;
    c.track = 2;
    c.start = 0;
    c.length = lynx::ppq * 4;
    s.playlist.push_back (c);
    c.start = lynx::ppq * 4;
    c.offset = lynx::ppq * 4; // continues the first one
    s.playlist.push_back (c);
    c.start = lynx::ppq * 16;
    c.offset = 0;
    s.playlist.push_back (c);
    return s;
}
} // namespace

TEST (clipops_frames_per_tick_follows_the_stretch)
{
    lynx::SamplerSettings sampler;
    // 120 BPM: a beat is 0.5 s = 22050 frames at 44.1 kHz
    CHECK (std::abs (lynx::clipops::framesPerTick (sampler, 88200.0, 44100.0, 120.0) * lynx::ppq - 22050.0) < 1.0e-6);
    CHECK (lynx::clipops::fileTicks (sampler, 88200.0, 44100.0, 120.0) == lynx::ppq * 4);
    sampler.stretchBeats = 8.0f; // the 2 s file spread over 8 beats, whatever the tempo
    CHECK (lynx::clipops::fileTicks (sampler, 88200.0, 44100.0, 120.0) == lynx::ppq * 8);
    CHECK (lynx::clipops::fileTicks (sampler, 88200.0, 44100.0, 90.0) == lynx::ppq * 8);
}

TEST (clipops_fit_beats_power_of_two)
{
    // a one-bar loop at 100 BPM (2.4 s) in a 128 BPM project: 5.12 beats -> 4
    CHECK (lynx::clipops::fitBeats (2.4, 128.0) == 4.0f);
    // an 8-bar loop at 140 BPM (13.71 s) at 120: 27.4 beats -> 32
    CHECK (lynx::clipops::fitBeats (32.0 * 60.0 / 140.0, 120.0) == 32.0f);
    CHECK (lynx::clipops::fitBeats (0.0, 120.0) == 0.0f);
    CHECK (lynx::clipops::fitBeats (0.1, 120.0) == 1.0f);
}

TEST (clipops_same_source_and_make_unique)
{
    auto s = clipSong();
    lynx::ChannelDef other;
    other.name = "Other";
    s.channels.push_back (other);
    auto c = s.playlist[0];
    c.channel = 1;
    s.playlist.push_back (c);

    CHECK ((lynx::clipops::sameSource (s, 0) == std::vector<int> { 0, 1, 2 }));
    CHECK ((lynx::clipops::sameSource (s, 3) == std::vector<int> { 3 }));

    const auto made = lynx::clipops::makeAudioUnique (s, { 2 });
    CHECK (made == 2 && s.channels.size() == 3);
    CHECK (s.channels[2].name == "Loop #2" && s.channels[2].samplePath == "loop.wav");
    CHECK (s.playlist[2].channel == 2 && s.playlist[0].channel == 0 && s.playlist[1].channel == 0);
    CHECK (lynx::clipops::makeAudioUnique (s, { 0 }) == 3 && s.channels[3].name == "Loop #3");
    CHECK (lynx::clipops::makeAudioUnique (s, {}) == -1);
}

TEST (clipops_merge_contiguous_only)
{
    auto s = clipSong();
    CHECK (lynx::clipops::canMerge (s, { 0, 1 }));
    CHECK (! lynx::clipops::canMerge (s, { 0, 2 }));  // a gap
    CHECK (! lynx::clipops::canMerge (s, { 0 }));
    s.playlist[1].fadeOut = 30;
    s.playlist[0].fadeIn = 20;
    CHECK (lynx::clipops::mergeClips (s, { 1, 0 }));
    CHECK (s.playlist.size() == 2);
    const auto& m = s.playlist.back();
    CHECK (m.start == 0 && m.length == lynx::ppq * 8 && m.offset == 0 && m.fadeIn == 20 && m.fadeOut == 30);

    auto t = clipSong();
    t.playlist[1].offset = 0; // starts where the first ends, but restarts the audio: not a continuation
    CHECK (! lynx::clipops::canMerge (t, { 0, 1 }));
    t = clipSong();
    t.playlist[1].track = 3;
    CHECK (! lynx::clipops::canMerge (t, { 0, 1 }));
}

TEST (clipops_reverse_mirrors_the_offset)
{
    auto s = clipSong();
    const auto whole = lynx::ppq * 10;
    s.playlist[2].offset = lynx::ppq * 1; // plays beats 1..5 of the file
    lynx::clipops::reverseAudio (s, 2, whole);
    CHECK (s.channels[0].sampler.reverse);
    // beats 1..5 of the forward file are beats 5..9 of the reversed one
    CHECK (s.playlist[2].offset == lynx::ppq * 5 && s.playlist[2].length == lynx::ppq * 4);
    CHECK (s.playlist[0].offset == lynx::ppq * 6); // every clip of the file follows
    lynx::clipops::reverseAudio (s, 2, whole);
    CHECK (! s.channels[0].sampler.reverse && s.playlist[2].offset == lynx::ppq * 1 && s.playlist[0].offset == 0);
}
