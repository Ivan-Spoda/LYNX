#include "engine/MidiFileIo.h"

#include <map>

#include "core/ExportOptions.h"
#include "engine/Text.h"

namespace lynx::MidiFileIo
{
namespace
{
struct Parsed
{
    std::vector<std::vector<Note>> tracks; // notes per track (channel = track index)
    std::vector<std::string> names;
    double bpm = 120.0;
};

std::optional<Parsed> parse (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;

    if (! in.openedOk() || ! midi.readFrom (in))
    {
        error = utf8 ("not a valid MIDI file");
        return std::nullopt;
    }

    const auto format = midi.getTimeFormat();

    if (format <= 0)
    {
        // SMPTE: convert to seconds, then to ticks at the default tempo
        midi.convertTimestampTicksToSeconds();
    }

    Parsed out;
    const auto filePpq = format > 0 ? (double) format : 0.0;
    auto toTicks = [&] (double t) {
        return filePpq > 0.0 ? (std::int64_t) std::llround (t * ppq / filePpq) : (std::int64_t) std::llround (t * out.bpm / 60.0 * ppq);
    };

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        auto track = *midi.getTrack (t);
        track.updateMatchedPairs();
        std::vector<Note> notes;
        std::string name;

        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto* e = track.getEventPointer (i);
            const auto& m = e->message;

            if (m.isTempoMetaEvent() && out.bpm == 120.0 && m.getTempoSecondsPerQuarterNote() > 0.0)
                out.bpm = juce::jlimit (20.0, 999.0, 60.0 / m.getTempoSecondsPerQuarterNote());
            else if (m.isTrackNameEvent() && name.empty())
                name = m.getTextFromTextMetaEvent().toStdString();
            else if (m.isNoteOn())
            {
                Note n;
                n.start = toTicks (m.getTimeStamp());
                const auto end = e->noteOffObject != nullptr ? toTicks (e->noteOffObject->message.getTimeStamp()) : n.start + ticksPerStep;
                n.length = std::max<std::int64_t> (1, end - n.start);
                n.key = m.getNoteNumber();
                n.velocity = m.getFloatVelocity();
                notes.push_back (n);
            }
        }

        if (! notes.empty())
        {
            out.tracks.push_back (std::move (notes));
            out.names.push_back (name);
        }
    }

    if (out.tracks.empty())
    {
        error = utf8 ("the file has no notes");
        return std::nullopt;
    }

    return out;
}
} // namespace

std::optional<Song> loadSong (const juce::File& file, juce::String& error)
{
    auto parsed = parse (file, error);

    if (! parsed)
        return std::nullopt;

    Song song;
    song.bpm = parsed->bpm;
    song.mixer = Song::defaultMixer();
    auto& pattern = song.patterns.emplace_back();
    pattern.name = file.getFileNameWithoutExtension().toStdString();

    for (std::size_t t = 0; t < parsed->tracks.size(); ++t)
    {
        ChannelDef c;
        c.name = parsed->names[t].empty() ? "MIDI " + std::to_string (t + 1) : parsed->names[t];
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = song.nextFreeInsert();
        c.volume = 0.6f;
        song.channels.push_back (c);

        for (auto n : parsed->tracks[t])
        {
            n.channel = (int) t;
            pattern.notes.push_back (n);
        }
    }

    pattern.sortNotes();
    return song;
}

std::optional<std::vector<Note>> loadNotes (const juce::File& file, int channel, juce::String& error)
{
    auto parsed = parse (file, error);

    if (! parsed)
        return std::nullopt;

    std::vector<Note> notes;

    for (const auto& track : parsed->tracks)
        for (auto n : track)
        {
            n.channel = channel;
            notes.push_back (n);
        }

    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

bool savePattern (const Song& song, int patternIndex, const juce::File& file, juce::String& error)
{
    if (patternIndex < 0 || patternIndex >= (int) song.patterns.size())
    {
        error = "pattern does not exist";
        return false;
    }

    const auto& pattern = song.patterns[(std::size_t) patternIndex];
    std::map<int, juce::MidiMessageSequence> byChannel;

    for (const auto& n : pattern.notes)
    {
        auto& seq = byChannel[n.channel];
        const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
        seq.addEvent (juce::MidiMessage::noteOn (1, n.key, velocity), (double) n.start);
        seq.addEvent (juce::MidiMessage::noteOff (1, n.key), (double) (n.start + std::max<std::int64_t> (1, n.length)));
    }

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);

    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / song.bpm)), 0.0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (song.beatsPerBar, 4), 0.0);
    midi.addTrack (tempo);

    for (auto& [channel, seq] : byChannel)
    {
        const auto name = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].name : std::string ("Channel");
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (name.c_str())), 0.0);
        seq.sort();
        seq.updateMatchedPairs();
        midi.addTrack (seq);
    }

    file.deleteFile();
    juce::FileOutputStream out (file);

    if (out.failedToOpen() || ! midi.writeTo (out))
    {
        error = utf8 ("could not write ") + file.getFullPathName();
        return false;
    }

    return true;
}

bool saveSong (const Song& song, const juce::File& file, juce::String& error)
{
    // the arrangement flattened into one pattern, written like a pattern (one track per channel)
    Song flat;
    flat.bpm = song.bpm;
    flat.beatsPerBar = song.beatsPerBar;
    flat.channels = song.channels;
    flat.patterns.resize (1);
    flat.patterns[0].notes = exportopts::midiExportNotes (song);
    return savePattern (flat, 0, file, error);
}
} // namespace lynx::MidiFileIo
