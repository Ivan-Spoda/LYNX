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
        case InstrumentKind::plugin: return "plugin";
    }
    return "synth";
}

InstrumentKind instrumentFromId (const juce::String& id)
{
    for (auto k : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap,
                    InstrumentKind::synth, InstrumentKind::sampler, InstrumentKind::audioClip,
                    InstrumentKind::plugin })
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
        case EffectKind::plugin: return "plugin";
    }
    return "eq";
}

juce::var pluginToVar (const PluginRef& p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("id", str (p.id));
    o->setProperty ("name", str (p.name));
    o->setProperty ("state", str (p.state));
    return juce::var (o.release());
}

PluginRef pluginFromVar (const juce::var& v)
{
    PluginRef p;
    p.id = v["id"].toString().toStdString();
    p.name = v["name"].toString().toStdString();
    p.state = v["state"].toString().toStdString();
    return p;
}

std::optional<EffectKind> effectFromId (const juce::String& id)
{
    for (auto k : { EffectKind::eq, EffectKind::compressor, EffectKind::delay, EffectKind::reverb, EffectKind::plugin })
        if (id == effectId (k))
            return k;

    return std::nullopt; // efeito de uma versão futura: ignorado
}

const char* targetKindName (AutomationTargetKind k)
{
    switch (k)
    {
        case AutomationTargetKind::channelVolume: return "channelVolume";
        case AutomationTargetKind::channelPan: return "channelPan";
        case AutomationTargetKind::channelParam: return "channelParam";
        case AutomationTargetKind::mixerVolume: return "mixerVolume";
        case AutomationTargetKind::mixerPan: return "mixerPan";
        case AutomationTargetKind::effectParam: return "effectParam";
        case AutomationTargetKind::tempo: return "tempo";
        case AutomationTargetKind::none: break;
    }
    return "none";
}

AutomationTargetKind targetKindFromName (const juce::String& name)
{
    for (auto k : { AutomationTargetKind::channelVolume, AutomationTargetKind::channelPan, AutomationTargetKind::channelParam,
                    AutomationTargetKind::mixerVolume, AutomationTargetKind::mixerPan, AutomationTargetKind::effectParam,
                    AutomationTargetKind::tempo })
        if (name == targetKindName (k))
            return k;

    return AutomationTargetKind::none;
}
juce::var clipsToVar (const std::vector<Clip>& list)
{
    juce::Array<juce::var> clips;

    // [pattern, trilha, início, duração, offset, tipo (0 pattern, 1 áudio, 2 automação), canal ou automação]
    for (const auto& c : list)
        clips.add (juce::Array<juce::var> { c.pattern, c.track, (juce::int64) c.start, (juce::int64) c.length,
                                            (juce::int64) c.offset, (int) c.kind,
                                            c.kind == ClipKind::automation ? c.automation : c.channel });

    return clips;
}

// Precisa de canais, patterns e automações já lidos para validar os índices.
std::vector<Clip> clipsFromVar (const juce::var& v, const Song& song)
{
    std::vector<Clip> out;

    if (auto* clips = v.getArray())
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

            if (c.size() >= 7 && (int) c[5] == 2)
            {
                clip.kind = ClipKind::automation;
                clip.automation = (int) c[6];

                if (clip.automation >= 0 && clip.automation < (int) song.automations.size() && clip.length > 0)
                    out.push_back (clip);
            }
            else if (c.size() >= 7 && (int) c[5] == 1) // [.., tipo, canal]; arquivos antigos têm só 5 campos
            {
                clip.kind = ClipKind::audio;
                clip.channel = (int) c[6];

                if (clip.channel >= 0 && clip.channel < (int) song.channels.size() && clip.length > 0)
                    out.push_back (clip);
            }
            else if (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && clip.length > 0)
                out.push_back (clip);
        }

    return out;
}
} // namespace

juce::String toJson (const Song& song)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "lynx-project");
    root->setProperty ("version", currentVersion);
    root->setProperty ("bpm", song.bpm);
    root->setProperty ("beatsPerBar", song.beatsPerBar);

    if (song.swing > 0.0f)
        root->setProperty ("swing", juce::roundToInt (song.swing * 1000.0f) / 1000.0);
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

        if (! (c.sampler == SamplerSettings {}))
        {
            auto sampler = std::make_unique<juce::DynamicObject>();
            sampler->setProperty ("pitch", c.sampler.pitch);
            sampler->setProperty ("attack", c.sampler.attack);
            sampler->setProperty ("release", c.sampler.release);
            sampler->setProperty ("reverse", c.sampler.reverse);
            sampler->setProperty ("cut", c.sampler.cut);
            o->setProperty ("samplerSettings", juce::var (sampler.release()));
        }

        if (c.instrument == InstrumentKind::plugin)
            o->setProperty ("plugin", pluginToVar (c.plugin));

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

        if (p.colour != 0)
            o->setProperty ("colour", juce::String::toHexString ((juce::int64) (p.colour & 0xFFFFFF)).paddedLeft ('0', 6));

        juce::Array<juce::var> notes;

        for (const auto& n : p.notes)
        {
            juce::Array<juce::var> note { (juce::int64) n.start, (juce::int64) n.length, n.channel, n.key,
                                          juce::roundToInt (n.velocity * 1000.0f) / 1000.0 };

            // [.., pan, afinação fina, mutada] só quando não são o padrão
            if (n.pan != 0.0f || n.finePitch != 0.0f || n.muted)
            {
                note.add (juce::roundToInt (n.pan * 1000.0f) / 1000.0);
                note.add (juce::roundToInt (n.finePitch * 10.0f) / 10.0);
                note.add (n.muted);
            }

            notes.add (note);
        }

        o->setProperty ("notes", notes);
        patterns.add (juce::var (o.release()));
    }

    root->setProperty ("patterns", patterns);

    juce::Array<juce::var> automations;

    for (const auto& a : song.automations)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (a.name));
        o->setProperty ("target", juce::Array<juce::var> { targetKindName (a.target.kind), a.target.index, a.target.slot, a.target.param });
        juce::Array<juce::var> points;

        for (const auto& pt : a.points)
            points.add (juce::Array<juce::var> { (juce::int64) pt.tick, juce::roundToInt (pt.value * 10000.0f) / 10000.0,
                                                 juce::roundToInt (pt.tension * 1000.0f) / 1000.0 });

        o->setProperty ("points", points);
        automations.add (juce::var (o.release()));
    }

    if (! automations.isEmpty())
        root->setProperty ("automations", automations);

    root->setProperty ("playlist", clipsToVar (song.playlist));

    {
        juce::Array<juce::var> tracks;

        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& t = song.playlistTracks[i]; ! t.name.empty() || t.muted)
                tracks.add (juce::Array<juce::var> { (int) i, str (t.name), t.muted });

        if (! tracks.isEmpty())
            root->setProperty ("playlistTracks", tracks);
    }

    if (song.arrangements.size() > 1)
    {
        juce::Array<juce::var> arrangements;

        for (std::size_t i = 0; i < song.arrangements.size(); ++i)
        {
            const auto& a = song.arrangements[i];
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("name", str (a.name));

            if ((int) i != song.currentArrangement) // o atual está em "playlist"
            {
                o->setProperty ("clips", clipsToVar (a.clips));

                if (a.loopEnd > a.loopStart)
                    o->setProperty ("loop", juce::Array<juce::var> { (juce::int64) a.loopStart, (juce::int64) a.loopEnd });
            }

            arrangements.add (juce::var (o.release()));
        }

        root->setProperty ("arrangements", arrangements);
        root->setProperty ("currentArrangement", song.currentArrangement);
    }

    juce::Array<juce::var> mixer;

    for (const auto& t : song.mixer)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (t.name));
        o->setProperty ("volume", t.volume);
        o->setProperty ("pan", t.pan);
        o->setProperty ("muted", t.muted);
        o->setProperty ("solo", t.solo);

        if (! t.toMaster)
            o->setProperty ("toMaster", false);
        if (t.stereo != 0.0f)
            o->setProperty ("stereo", juce::roundToInt (t.stereo * 1000.0f) / 1000.0);
        if (t.invertPhase)
            o->setProperty ("invertPhase", true);
        if (t.swapChannels)
            o->setProperty ("swap", true);

        if (! t.sends.empty())
        {
            juce::Array<juce::var> sends;

            for (const auto& send : t.sends)
                sends.add (juce::Array<juce::var> { send.target, juce::roundToInt (send.level * 10000.0f) / 10000.0 });

            o->setProperty ("sends", sends);
        }

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

            if (slot.mix < 1.0f)
                e->setProperty ("mix", juce::roundToInt (slot.mix * 10000.0f) / 10000.0);

            if (slot.kind == EffectKind::plugin)
                e->setProperty ("plugin", pluginToVar (slot.plugin));
            effects.add (juce::var (e.release()));
        }

        if (! effects.isEmpty())
            o->setProperty ("effects", effects);

        mixer.add (juce::var (o.release()));
    }

    root->setProperty ("mixer", mixer);
    root->setProperty ("mode", song.mode == PlayMode::song ? "song" : "pattern");

    if (song.hasLoop())
        root->setProperty ("loop", juce::Array<juce::var> { (juce::int64) song.loopStart, (juce::int64) song.loopEnd });
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
    song.swing = juce::jlimit (0.0f, 1.0f, (float) (double) root.getProperty ("swing", 0.0));
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
            def.plugin = pluginFromVar (c["plugin"]);

            if (const auto& s = c["samplerSettings"]; s.isObject())
            {
                def.sampler.pitch = juce::jlimit (-24.0f, 24.0f, (float) (double) s.getProperty ("pitch", 0.0));
                def.sampler.attack = juce::jlimit (0.0f, 2.0f, (float) (double) s.getProperty ("attack", 0.0));
                def.sampler.release = juce::jlimit (0.0f, 4.0f, (float) (double) s.getProperty ("release", 0.0));
                def.sampler.reverse = (bool) s.getProperty ("reverse", false);
                def.sampler.cut = (bool) s.getProperty ("cut", false);
            }

            song.channels.push_back (std::move (def));
        }

    if (auto* patterns = root["patterns"].getArray())
        for (const auto& p : *patterns)
        {
            Pattern pattern;
            pattern.name = p["name"].toString().toStdString();
            pattern.lengthTicks = (juce::int64) p.getProperty ("length", 0);

            if (p.hasProperty ("colour"))
                pattern.colour = 0xFF000000u | (std::uint32_t) p["colour"].toString().getHexValue32();

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

                    if (n.size() >= 8)
                    {
                        note.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) n[5]);
                        note.finePitch = juce::jlimit (-120.0f, 120.0f, (float) (double) n[6]);
                        note.muted = (bool) n[7];
                    }

                    if (note.channel >= 0 && note.channel < (int) song.channels.size())
                        pattern.notes.push_back (note);
                }

            pattern.sortNotes();
            song.patterns.push_back (std::move (pattern));
        }

    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    if (auto* automations = root["automations"].getArray())
        for (const auto& a : *automations)
        {
            Automation automation;
            automation.name = a["name"].toString().toStdString();

            if (const auto& t = a["target"]; t.isArray() && t.size() == 4)
                automation.target = { targetKindFromName (t[0].toString()), (int) t[1], (int) t[2], (int) t[3] };

            if (auto* points = a["points"].getArray())
                for (const auto& pt : *points)
                    if (pt.isArray() && pt.size() >= 2)
                        automation.points.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) pt[0]),
                                                       juce::jlimit (0.0f, 1.0f, (float) (double) pt[1]),
                                                       pt.size() >= 3 ? juce::jlimit (-1.0f, 1.0f, (float) (double) pt[2]) : 0.0f });

            std::stable_sort (automation.points.begin(), automation.points.end(),
                              [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
            song.automations.push_back (std::move (automation));
        }

    song.playlist = clipsFromVar (root["playlist"], song);

    if (auto* tracks = root["playlistTracks"].getArray())
        for (const auto& t : *tracks)
            if (t.isArray() && t.size() == 3 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                song.playlistTracks[index] = { t[1].toString().toStdString(), (bool) t[2] };
            }

    if (auto* arrangements = root["arrangements"].getArray(); arrangements != nullptr && ! arrangements->isEmpty())
    {
        song.arrangements.clear();

        for (const auto& a : *arrangements)
        {
            Arrangement arrangement;
            arrangement.name = a["name"].toString().toStdString();
            arrangement.clips = clipsFromVar (a["clips"], song);

            if (const auto& loop = a["loop"]; loop.isArray() && loop.size() == 2)
            {
                arrangement.loopStart = juce::jmax<juce::int64> (0, (juce::int64) loop[0]);
                arrangement.loopEnd = juce::jmax<juce::int64> (arrangement.loopStart, (juce::int64) loop[1]);
            }

            song.arrangements.push_back (std::move (arrangement));
        }

        song.currentArrangement = juce::jlimit (0, (int) song.arrangements.size() - 1, (int) root.getProperty ("currentArrangement", 0));
        song.arrangements[(std::size_t) song.currentArrangement].clips.clear();
    }

    song.mode = root["mode"].toString() == "song" ? PlayMode::song : PlayMode::pattern;

    if (const auto& loop = root["loop"]; loop.isArray() && loop.size() == 2)
    {
        song.loopStart = juce::jmax<juce::int64> (0, (juce::int64) loop[0]);
        song.loopEnd = juce::jmax<juce::int64> (song.loopStart, (juce::int64) loop[1]);
    }

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
            track.toMaster = (bool) t.getProperty ("toMaster", true);
            track.stereo = juce::jlimit (-1.0f, 1.0f, (float) (double) t.getProperty ("stereo", 0.0));
            track.invertPhase = (bool) t.getProperty ("invertPhase", false);
            track.swapChannels = (bool) t.getProperty ("swap", false);

            if (auto* sends = t["sends"].getArray())
                for (const auto& send : *sends)
                    if (send.isArray() && send.size() == 2)
                        track.sends.push_back ({ (int) send[0], juce::jlimit (0.0f, 1.0f, (float) (double) send[1]) });

            if (auto* effects = t["effects"].getArray())
                for (const auto& e : *effects)
                {
                    const auto kind = effectFromId (e["kind"].toString());

                    if (! kind || (int) track.effects.size() >= maxEffectSlots)
                        continue;

                    EffectSlot slot;
                    slot.kind = *kind;
                    slot.enabled = (bool) e.getProperty ("on", true);
                    slot.mix = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("mix", 1.0));

                    if (auto* params = e["params"].getArray())
                        for (int i = 0; i < params->size() && i < maxEffectParams; ++i)
                            slot.params[(std::size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*params)[i]);

                    slot.plugin = pluginFromVar (e["plugin"]);
                    track.effects.push_back (slot);
                }

            song.mixer.push_back (std::move (track));
        }

    if (song.mixer.empty()) // projetos de antes do mixer
        song.mixer = Song::defaultMixer();

    // envios para trilhas que não existem são descartados
    for (auto& t : song.mixer)
        t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(),
                                       [n = (int) song.mixer.size()] (const MixerSend& s) { return s.target <= 0 || s.target >= n; }),
                       t.sends.end());

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
