// --- REA round 146: the touched control wins over its automation while recording, initial values ------------------

TEST (rea146_touched_control_overrides_its_automation)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::song;
    song.channels.clear();
    song.playlist.clear();
    song.automations.clear();
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[0].inputChannel = 0; // a constant through the Master's input
    song.mixer[0].monitor = lynx::InputMonitor::automatic;
    const lynx::AutomationTarget target { lynx::AutomationTargetKind::mixerVolume, 0 };
    lynx::Automation a;
    a.target = target;
    a.points = { { 0, 0.0f, 0.0f }, { 4 * lynx::ppq, 0.0f, 0.0f } }; // the Master's fader automated down to silence
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.length = 64 * lynx::ppq;
    song.playlist.push_back (clip);
    std::vector<int> slots;
    lynx::RackState::slotsOfTarget (song, target, slots);
    CHECK (slots.size() == 1 && slots[0] == 0);

    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> in (512, 0.25f), l (512), r (512);
    input.channels[0] = in.data();
    input.channels[1] = in.data();
    input.numChannels = 2;
    auto run = [&] (int blocks) {
        double tick = 0.0;
        for (int b = 0; b < blocks; ++b, tick += 64.0)
            renderer.process (state.get(), true, tick, 512, l.data(), r.data());
        return std::abs (l[511]);
    };
    CHECK (run (8) < 1e-4f); // the automation plays: silence
    renderer.setOverriddenSlots (slots); // touched while recording: the fader's own value plays
    CHECK (renderer.isSlotOverridden (0) && ! renderer.isSlotOverridden (1));
    const auto touched = run (8);
    CHECK (touched > 0.05f);
    renderer.setOverriddenSlots ({}); // released: the automation drives it again
    CHECK (run (8) < 1e-4f);

    // a pattern event lane on the same target gets its slot after the song's automations
    song.patterns[0].events.push_back ({ target, { { 0, 0.5f } } });
    lynx::RackState::slotsOfTarget (song, target, slots);
    CHECK (slots.size() == 2 && slots[1] == 1);
}

TEST (rea146_initial_values_reset_the_song_like_fl)
{
    auto song = lynx::Song::makeDefault();
    const lynx::AutomationTarget vol { lynx::AutomationTargetKind::mixerVolume, 1 };
    const lynx::AutomationTarget pan { lynx::AutomationTargetKind::channelPan, 1 };
    const lynx::AutomationTarget plug { lynx::AutomationTargetKind::effectParam, 1, 0, 3 };
    song.setInitialValue (vol, 0.25f);
    song.setInitialValue (vol, 0.5f); // replaces
    song.setInitialValue (pan, 0.75f);
    song.setInitialValue (plug, 0.1f);
    CHECK (song.initialValues.size() == 3 && song.findInitialValue (vol) == 0 && song.initialValues[0].value == 0.5f);
    song.setTargetValue (vol, 0.9f);
    song.setTargetValue (pan, 0.1f);
    std::vector<lynx::Song::InitialValue> plugins;
    song.applyInitialValues ([&] (const lynx::AutomationTarget& t, float v) { plugins.push_back ({ t, v }); });
    CHECK (std::abs (song.targetValue (vol) - 0.5f) < 1e-5f && std::abs (song.targetValue (pan) - 0.75f) < 1e-5f);
    // the mixer slot has no effect: the value goes to the caller (a plugin's parameter lives in the plugin)
    CHECK (plugins.size() == 1 && plugins[0].target == plug && plugins[0].value == 0.1f);
    song.removeInitialValue (vol);
    CHECK (song.findInitialValue (vol) < 0 && song.initialValues.size() == 2);
    // removing channel 0 renumbers the target; removing its own channel drops it
    song.removeChannel (0);
    CHECK (song.findInitialValue ({ lynx::AutomationTargetKind::channelPan, 0 }) >= 0);
    song.removeChannel (0);
    CHECK (song.findInitialValue ({ lynx::AutomationTargetKind::channelPan, 0 }) < 0 && song.initialValues.size() == 1);
}

TEST (rea146_global_links_and_project_override)
{
    const lynx::AutomationTarget master { lynx::AutomationTargetKind::mixerVolume, 0 };
    const lynx::AutomationTarget tempo { lynx::AutomationTargetKind::tempo, 0 };
    const lynx::AutomationTarget pan { lynx::AutomationTargetKind::channelPan, 2 };
    std::vector<lynx::MidiLink> global = { { 7, -1, master }, { 20, 0, tempo }, { 21, -1, pan } };
    std::vector<lynx::MidiLink> project = { { 30, -1, master }, { 20, 1, pan } }; // same target / CC 20 on another channel
    const auto links = lynx::linksWithGlobal (project, global);
    // the project's two, then the tempo's global link (the Master's is overridden by the project's own target,
    // the pan's too); CC 20 channel 1 vs channel 0 do not collide
    CHECK (links.size() == 3 && links[2].target == tempo);
    project = { { 21, 5, master } }; // CC 21 on channel 6 collides with the global "any channel" CC 21
    const auto links2 = lynx::linksWithGlobal (project, global);
    CHECK (links2.size() == 2 && links2[1].target == tempo);

    lynx::prefs::MidiPrefs p;
    p.pickup = true;
    p.globalLinks.push_back ({ 7, -1, (int) lynx::AutomationTargetKind::mixerVolume, 0, 0, 0 });
    p.globalLinks.push_back ({ 74, 3, (int) lynx::AutomationTargetKind::effectParam, 2, 1, 4 });
    const auto back = lynx::prefs::MidiPrefs::fromText (p.toText());
    CHECK (back == p);
    CHECK (lynx::prefs::MidiPrefs::fromText ("link=7|-1|99|0|0|0\nlink=1|2\n").globalLinks.empty());
}
