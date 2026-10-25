// REA round 151: Mod X / Y through the echo delay and the tracking; the release velocity and Mod X carried by the
// scheduled notes (FL's wrapper MIDI options use them)

TEST (rea151_echo_and_tracking_move_mod_x_y)
{
    using namespace lynx;
    EchoSettings s;
    s.feedback = 0.5f;
    s.echoes = 3;
    s.timeBeats = 0.5f;
    s.modX = 0.2f;  // each echo +0.1 of the note's Mod X
    s.modY = -0.4f; // each echo −0.2 of its Mod Y
    channelmisc::NoteEcho echo;
    std::vector<NoteEvent> ons;
    const auto tps = ticksPerSample (120.0, 48000.0);
    for (int block = 0; block < 100; ++block)
    {
        NoteEvent in[2];
        int count = 0;
        if (block == 0)
        {
            in[0].type = NoteEvent::Type::on;
            in[0].key = 60;
            in[0].velocity = 0.8f;
            in[0].modX = 0.5f;
            in[0].modY = 0.7f;
            in[1] = in[0];
            in[1].type = NoteEvent::Type::off;
            in[1].sampleOffset = 100;
            count = 2;
        }
        int n = 0;
        echo.process (0, in, count, block * 512 * tps, tps, 512, s, echo.output.data(), echo.outputGains.data(), n);
        for (int i = 0; i < n; ++i)
            if (echo.output[(std::size_t) i].type == NoteEvent::Type::on)
                ons.push_back (echo.output[(std::size_t) i]);
    }
    CHECK (ons.size() == 4u);
    if (ons.size() == 4u)
    {
        CHECK (std::abs (ons[1].modX - 0.6f) < 1e-5f && std::abs (ons[2].modX - 0.7f) < 1e-5f && std::abs (ons[3].modX - 0.8f) < 1e-5f);
        CHECK (std::abs (ons[1].modY - 0.5f) < 1e-5f && std::abs (ons[2].modY - 0.3f) < 1e-5f && std::abs (ons[3].modY - 0.1f) < 1e-5f);
    }

    // key tracking: the same distance as the pan's, scaled by the Mod X knob (FL's Mid = C5 = 60)
    ChannelMisc misc;
    misc.key.modX = 1.0f;
    const auto [atMid, unusedY] = channelmisc::trackedModulation (misc, 60, 0.78f, 0.5f, 0.5f);
    (void) unusedY;
    const auto [up, upY] = channelmisc::trackedModulation (misc, 84, 0.78f, 0.5f, 0.5f);
    const auto [down, downY] = channelmisc::trackedModulation (misc, 36, 0.78f, 0.5f, 0.5f);
    CHECK (std::abs (atMid - 0.5f) < 1e-5f);
    CHECK (up > 0.5f && down < 0.5f && std::abs ((up - 0.5f) + (down - 0.5f)) < 1e-5f);
    CHECK (upY == 0.5f && downY == 0.5f); // Mod Y knob at 0
}

TEST (rea151_scheduled_notes_carry_release_and_mod_x)
{
    using namespace lynx;
    Song s;
    s.channels.push_back ({});
    s.patterns.emplace_back();
    Note n;
    n.start = 0;
    n.length = ppq;
    n.release = 0.25f;
    n.modX = 0.9f;
    n.modY = 0.1f;
    s.patterns[0].notes.push_back (n);
    EventBuffer out;
    schedulePatternLoop (s.patterns[0], (std::int64_t) ppq * 16, 0.0, 48000, ticksPerSample (120.0, 48000.0), out, 0.0f, &s.channels);
    bool on = false, off = false;
    for (int i = 0; i < out.size(); ++i)
    {
        const auto& e = out[i];
        if (e.type == NoteEvent::Type::on)
            on = std::abs (e.modX - 0.9f) < 1e-6f && std::abs (e.modY - 0.1f) < 1e-6f;
        if (e.type == NoteEvent::Type::off)
            off = std::abs (e.release - 0.25f) < 1e-6f && e.velocity == 0.0f;
    }
    CHECK (on && off);

    // the wrapper options are saved per plugin with FL's defaults
    PluginRef ref;
    CHECK (ref.releaseVelocity && ! ref.sendBendRange && ref.bendRange == 12 && ! ref.modXAftertouch && ref.allNotesOffOnStop
           && ref.resetOnFlush && ref.sendLoopPosition);
    auto other = ref;
    other.modXAftertouch = true;
    CHECK (! (other == ref)); // a change re-creates the plugin with its state, so the new option applies
}

TEST (rea151_instrument_track_draws_its_own_pattern)
{
    using namespace lynx;
    // FL 2026 [EXEC]: the pencil on an instrument track places a pattern named after its channel, made on the first
    // click; later clicks place the same pattern
    auto s = Song::makeDefault();
    ChannelDef synth;
    synth.name = "Lead";
    synth.instrument = InstrumentKind::synth;
    synth.colour = 0xff3060c0;
    s.channels.push_back (synth);
    const auto lead = (int) s.channels.size() - 1;
    CHECK (trackmodes::instrumentTrackPattern (s, 0) == -1); // not an instrument track
    CHECK (trackmodes::makeInstrumentTrack (s, 3, lead));
    const auto before = s.patterns.size();
    const auto p = trackmodes::instrumentTrackPattern (s, 3);
    CHECK (p == (int) before && s.patterns.size() == before + 1);
    CHECK (s.patterns[(std::size_t) p].name == "Lead" && s.patterns[(std::size_t) p].colour == 0xff3060c0u);
    CHECK (trackmodes::instrumentTrackPattern (s, 3) == p); // the same one again
    // a pattern of that name with another channel's notes is not the track's
    Note other;
    other.channel = 0;
    s.patterns[(std::size_t) p].notes.push_back (other);
    const auto q = trackmodes::instrumentTrackPattern (s, 3);
    CHECK (q != p && s.patterns[(std::size_t) q].name == "Lead");
}

TEST (rea151_flp_fine_tune_imported)
{
    using namespace lynx;
    // FL 2026 saves Misc › Root note › Fine tune as the channel event 0x8E (signed cents) [DATA: +15 and +78 saved]
    std::vector<std::uint8_t> fine { 0x8E, 15, 0, 0, 0 }, negative { 0x8E, 0xF6, 0xFF, 0xFF, 0xFF }; // +15, −10
    auto withEvent = [] (const std::vector<std::uint8_t>& ev) {
        // a channel (0x40 = new channel, 0x15 = type generator) followed by the event
        std::vector<std::uint8_t> body { 0x40, 0, 0, 0x15, 0 };
        body.insert (body.end(), ev.begin(), ev.end());
        std::vector<std::uint8_t> file { 'F', 'L', 'h', 'd', 6, 0, 0, 0, 0, 0, 1, 0, 96, 0, 'F', 'L', 'd', 't' };
        const auto n = (std::uint32_t) body.size();
        for (int k = 0; k < 4; ++k)
            file.push_back ((std::uint8_t) (n >> (8 * k)));
        file.insert (file.end(), body.begin(), body.end());
        return flp::parseProject ({ file.data(), file.size() });
    };
    const auto a = withEvent (fine);
    const auto b = withEvent (negative);
    CHECK (! a.channels.empty() && a.channels[0].fineTune == 15);
    CHECK (! b.channels.empty() && b.channels[0].fineTune == -10);
}

TEST (rea151_fl_wrapper_midi_and_compatibility_flags)
{
    using namespace lynx;
    // FL 2026's own wrapper record for an FL Studio VSTi with its default options [DATA]: flags 0xB4007D8C
    ForeignPlugin f;
    CHECK (parseFlWrapper (rea134WrapperBlob (12, 0xB4007D8Cu, 0), f));
    PluginRef ref;
    applyFlWrapperOptions (f, ref);
    CHECK (! ref.sendBendRange && ref.releaseVelocity && ! ref.modXAftertouch && ref.allNotesOffOnStop && ref.resetOnFlush && ref.sendLoopPosition);
    // each option toggled in FL and saved [DATA]: bend range +0x1, release velocity off +0x20, MOD X +0x20000,
    // reset off +0x02000000, loop position off −0x800
    CHECK (parseFlWrapper (rea134WrapperBlob (12, 0xB4007D8Cu | 0x1u | 0x20u | 0x20000u | 0x02000000u, 0), f));
    applyFlWrapperOptions (f, ref);
    CHECK (ref.sendBendRange && ! ref.releaseVelocity && ref.modXAftertouch && ! ref.resetOnFlush && ref.sendLoopPosition);
    CHECK (parseFlWrapper (rea134WrapperBlob (12, 0xB4007D8Cu & ~0x800u, 0), f));
    applyFlWrapperOptions (f, ref);
    CHECK (! ref.sendLoopPosition);

    // "Send All Notes Off when playback stops" off: the second flags word (+21) gets 0x1000; the bend range value is
    // record 0x01's third word
    auto blob = rea134WrapperBlob (12, 0xB4007D8Cu, 0);
    // (the options record starts after the version (4) and its id / size (12): flags2 at 16 + 21)
    blob[16 + 22] = 0x10;
    std::vector<std::uint8_t> ports { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    std::vector<std::uint8_t> head { 1, 0, 0, 0, (std::uint8_t) ports.size(), 0, 0, 0, 0, 0, 0, 0 };
    blob.insert (blob.begin() + 4, ports.begin(), ports.end());
    blob.insert (blob.begin() + 4, head.begin(), head.end());
    CHECK (parseFlWrapper (blob, f) && f.bendRange == 7 && f.hasWrapperFlags2 && f.wrapperFlags2 == 0x1000u);
    ref = {};
    applyFlWrapperOptions (f, ref);
    CHECK (! ref.allNotesOffOnStop && ref.bendRange == 7);
}

TEST (rea151_patcher_surface_controls)
{
    using namespace lynx;
    // FL's Patcher › Surface [EXEC FL 2026]: each control is an output of the Map's Surface node
    PatcherDoc d;
    PatchNode fx;
    fx.effect.kind = EffectKind::delay;
    patcher::addNode (d, fx);
    SurfaceControl knob;
    knob.name = "Knob 1";
    knob.value = 0.25f;
    SurfaceControl pad;
    pad.kind = SurfaceControl::Kind::xy;
    pad.name = "XY 1";
    pad.value = 0.1f;
    pad.valueY = 0.9f;
    SurfaceControl button;
    button.kind = SurfaceControl::Kind::button;
    button.name = "Button 1";
    CHECK (patcher::addSurfaceControl (d, knob) == 0 && patcher::addSurfaceControl (d, pad) == 1 && patcher::addSurfaceControl (d, button) == 2);
    CHECK (d.surfaceOutputs() == 4); // the pad has two
    CHECK (d.surfaceOutput (0) == 0.25f && d.surfaceOutput (1) == 0.1f && d.surfaceOutput (2) == 0.9f && d.surfaceOutput (3) == 0.0f);

    PatchWire w;
    w.kind = PatchWire::Kind::control;
    w.from = patchSurface;
    w.fromPort = 3; // the button
    w.to = 0;
    w.toPort = 1;
    CHECK (patcher::connect (d, w));
    w.fromPort = 4; // no such output
    CHECK (! patcher::connect (d, w));
    w.fromPort = 0;
    w.kind = PatchWire::Kind::audio; // only control cables leave the Surface
    CHECK (! patcher::connect (d, w));

    // a value is not structure (the running graph takes it); a new control is
    auto e = d;
    e.surface[0].value = 0.8f;
    CHECK (patcher::sameStructure (d, e));
    patcher::addSurfaceControl (e, knob);
    CHECK (! patcher::sameStructure (d, e));

    // deleting the pad: its two outputs go, the button's cable moves down to output 1
    patcher::removeSurfaceControl (d, 1);
    CHECK (d.surface.size() == 2u && d.wires.size() == 1u && d.wires[0].fromPort == 1);

    // a running graph reads the Surface values through its cables (built-in factories, no plugins)
    patcher::Graph graph (d, 48000.0, patcher::builtInFactories (48000.0));
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    graph.process (l.data(), r.data(), nullptr, nullptr, l.data(), r.data(), 256, 120.0);
    CHECK (std::isfinite (l[100]));
}
