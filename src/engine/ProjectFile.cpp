#include "engine/ProjectFile.h"

#include "engine/Text.h"

namespace lynx::ProjectFile
{
namespace
{
const char* instrumentId (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "kick";
        case InstrumentKind::snare: return "snare";
        case InstrumentKind::hat: return "hat";
        case InstrumentKind::clap: return "clap";
        case InstrumentKind::synth: return "synth";
        case InstrumentKind::sampler: return "sampler";
        case InstrumentKind::audioClip: return "audioClip";
    }
    return "synth";
}

InstrumentKind instrumentFromId (const juce::String& id)
{
    for (auto k : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap,
                    InstrumentKind::synth, InstrumentKind::sampler, InstrumentKind::audioClip })
        if (id == instrumentId (k))
            return k;

    return InstrumentKind::synth;
}

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

const char* effectId (EffectKind k)
{
    switch (k)
    {
        case EffectKind::eq: return "eq";
        case EffectKind::compressor: return "compressor";
        case EffectKind::delay: return "delay";
        case EffectKind::reverb: return "reverb";
    }
    return "eq";
}

std::optional<EffectKind> effectFromId (const juce::String& id)
{
    for (auto k : { EffectKind::eq, EffectKind::compressor, EffectKind::delay, EffectKind::reverb })
        if (id == effectId (k))
            return k;

    return std::nullopt; // efeito de uma versão futura: ignorado
}
} // namespace

juce::String toJson (const Song& song)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "lynx-project");
    root->setProperty ("version", currentVersion);
    root->setProperty ("bpm", song.bpm);
    root->setProperty ("beatsPerBar", song.beatsPerBar);
    root->setProperty ("currentPattern", song.currentPattern);

    juce::Array<juce::var> channels;

    for (const auto& c : song.channels)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (c.name));
        o->setProperty ("instrument", instrumentId (c.instrument));
        o->setProperty ("volume", c.volume);
        o->setProperty ("pan", c.pan);
        o->setProperty ("muted", c.muted);

        if (! c.samplePath.empty())
            o->setProperty ("sample", str (c.samplePath));

        o->setProperty ("mixer", c.mixerTrack);

        channels.add (juce::var (o.release()));
    }

    root->setProperty ("channels", channels);

    juce::Array<juce::var> patterns;

    for (const auto& p : song.patterns)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (p.name));

        if (p.lengthTicks > 0)
            o->setProperty ("length", (juce::int64) p.lengthTicks);

        juce::Array<juce::var> notes;

        for (const auto& n : p.notes)
            notes.add (juce::Array<juce::var> { (juce::int64) n.start, (juce::int64) n.length, n.channel, n.key,
                                                juce::roundToInt (n.velocity * 1000.0f) / 1000.0 });

        o->setProperty ("notes", notes);
        patterns.add (juce::var (o.release()));
    }

    root->setProperty ("patterns", patterns);

    juce::Array<juce::var> clips;

    for (const auto& c : song.playlist)
        clips.add (juce::Array<juce::var> { c.pattern, c.track, (juce::int64) c.start, (juce::int64) c.length,
                                            (juce::int64) c.offset, c.kind == ClipKind::audio ? 1 : 0, c.channel });

    root->setProperty ("playlist", clips);

    juce::Array<juce::var> mixer;

    for (const auto& t : song.mixer)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (t.name));
        o->setProperty ("volume", t.volume);
        o->setProperty ("pan", t.pan);
        o->setProperty ("muted", t.muted);
        o->setProperty ("solo", t.solo);

        juce::Array<juce::var> effects;

        for (const auto& slot : t.effects)
        {
            auto e = std::make_unique<juce::DynamicObject>();
            e->setProperty ("kind", effectId (slot.kind));
            e->setProperty ("on", slot.enabled);
            juce::Array<juce::var> params;
            for (auto v : slot.params)
                params.add (juce::roundToInt (v * 10000.0f) / 10000.0);
            e->setProperty ("params", params);
            effects.add (juce::var (e.release()));
        }

        if (! effects.isEmpty())
            o->setProperty ("effects", effects);

        mixer.add (juce::var (o.release()));
    }

    root->setProperty ("mixer", mixer);
    root->setProperty ("mode", song.mode == PlayMode::song ? "song" : "pattern");
    return juce::JSON::toString (juce::var (root.release()), false);
}

std::optional<Song> fromJson (const juce::String& json, juce::String& error)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);

    if (parsed.failed())
    {
        error = utf8 ("JSON inválido: ") + parsed.getErrorMessage();
        return std::nullopt;
    }

    // "daw-project" = projetos salvos antes do projeto se chamar LYNX
    if (root["format"].toString() != "lynx-project" && root["format"].toString() != "daw-project")
    {
        error = utf8 ("não é um projeto desta DAW");
        return std::nullopt;
    }

    if ((int) root["version"] > currentVersion)
    {
        error = utf8 ("projeto salvo por uma versão mais nova do LYNX");
        return std::nullopt;
    }

    Song song;
    song.bpm = juce::jlimit (10.0, 999.0, (double) root.getProperty ("bpm", 130.0));
    song.beatsPerBar = juce::jlimit (1, 32, (int) root.getProperty ("beatsPerBar", 4));
    song.currentPattern = (int) root.getProperty ("currentPattern", 0);

    if (auto* channels = root["channels"].getArray())
        for (const auto& c : *channels)
        {
            ChannelDef def;
            def.name = c["name"].toString().toStdString();
            def.instrument = instrumentFromId (c["instrument"].toString());
            def.volume = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("volume", 0.8));
            def.pan = juce::jlimit (-1.0f, 1.0f, (float) c.getProperty ("pan", 0.0));
            def.muted = (bool) c.getProperty ("muted", false);
            def.samplePath = c["sample"].toString().toStdString();
            def.mixerTrack = juce::jlimit (0, maxMixerTracks - 1, (int) c.getProperty ("mixer", 0));
            song.channels.push_back (std::move (def));
        }

    if (auto* patterns = root["patterns"].getArray())
        for (const auto& p : *patterns)
        {
            Pattern pattern;
            pattern.name = p["name"].toString().toStdString();
            pattern.lengthTicks = (juce::int64) p.getProperty ("length", 0);

            if (auto* notes = p["notes"].getArray())
                for (const auto& n : *notes)
                {
                    if (! n.isArray() || n.size() < 5)
                        continue;

                    Note note;
                    note.start = (juce::int64) n[0];
                    note.length = juce::jmax<juce::int64> (1, (juce::int64) n[1]);
                    note.channel = (int) n[2];
                    note.key = juce::jlimit (0, 127, (int) n[3]);
                    note.velocity = juce::jlimit (0.0f, 1.0f, (float) (double) n[4]);

                    if (note.channel >= 0 && note.channel < (int) song.channels.size())
                        pattern.notes.push_back (note);
                }

            pattern.sortNotes();
            song.patterns.push_back (std::move (pattern));
        }

    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    if (auto* clips = root["playlist"].getArray())
        for (const auto& c : *clips)
        {
            if (! c.isArray() || c.size() < 5)
                continue;

            Clip clip;
            clip.pattern = (int) c[0];
            clip.track = juce::jmax (0, (int) c[1]);
            clip.start = juce::jmax<juce::int64> (0, (juce::int64) c[2]);
            clip.length = (juce::int64) c[3];
            clip.offset = juce::jmax<juce::int64> (0, (juce::int64) c[4]);

            if (c.size() >= 7 && (int) c[5] == 1) // [.., tipo, canal]; arquivos antigos têm só 5 campos
            {
                clip.kind = ClipKind::audio;
                clip.channel = (int) c[6];

                if (clip.channel >= 0 && clip.channel < (int) song.channels.size() && clip.length > 0)
                    song.playlist.push_back (clip);
            }
            else if (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && clip.length > 0)
                song.playlist.push_back (clip);
        }

    song.mode = root["mode"].toString() == "song" ? PlayMode::song : PlayMode::pattern;

    if (auto* mixer = root["mixer"].getArray())
        for (const auto& t : *mixer)
        {
            if ((int) song.mixer.size() >= maxMixerTracks)
                break;

            MixerTrack track;
            track.name = t["name"].toString().toStdString();
            track.volume = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("volume", unityFader));
            track.pan = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("pan", 0.0));
            track.muted = (bool) t.getProperty ("muted", false);
            track.solo = (bool) t.getProperty ("solo", false);

            if (auto* effects = t["effects"].getArray())
                for (const auto& e : *effects)
                {
                    const auto kind = effectFromId (e["kind"].toString());

                    if (! kind || (int) track.effects.size() >= maxEffectSlots)
                        continue;

                    EffectSlot slot;
                    slot.kind = *kind;
                    slot.enabled = (bool) e.getProperty ("on", true);

                    if (auto* params = e["params"].getArray())
                        for (int i = 0; i < params->size() && i < maxEffectParams; ++i)
                            slot.params[(std::size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*params)[i]);

                    track.effects.push_back (slot);
                }

            song.mixer.push_back (std::move (track));
        }

    if (song.mixer.empty()) // projetos de antes do mixer
        song.mixer = Song::defaultMixer();

    song.currentPattern = juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern);
    return song;
}

bool save (const Song& song, const juce::File& file, juce::String& error)
{
    // Grava num temporário e troca, para um erro no meio não destruir o projeto anterior.
    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (toJson (song)) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = utf8 ("não foi possível gravar ") + file.getFullPathName();
        return false;
    }

    return true;
}

std::optional<Song> load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = utf8 ("arquivo não encontrado");
        return std::nullopt;
    }

    return fromJson (file.loadFileAsString(), error);
}
} // namespace lynx::ProjectFile
