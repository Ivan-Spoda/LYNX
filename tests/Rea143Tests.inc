// --- REA round 143: automation recording, the Master's audio input -----------------------------------------------

TEST (rea143_recorded_automation_makes_a_clip_that_grows)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    song.automations.clear();
    // a pattern clip over 4 bars gives the song its length
    lynx::Clip pc;
    pc.kind = lynx::ClipKind::pattern;
    pc.length = 4 * 4 * lynx::ppq;
    song.playlist.push_back (pc);
    const lynx::AutomationTarget target { lynx::AutomationTargetKind::mixerVolume, 1 };
    lynx::AutomationWrite w;
    w.target = target;
    w.name = "Insert 1 - Volume";
    const double bar = 4.0 * lynx::ppq;
    // first batch inside bar 2
    lynx::commitAutomationPoints (song, w, { { bar + 100, 0.2f }, { bar + 200, 0.3f }, { bar + 300, 0.3f }, { bar + 400, 0.3f } }, 0);
    CHECK (song.automations.size() == 1 && w.automation == 0 && w.createdByTake);
    const lynx::Clip* clip = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == lynx::ClipKind::automation)
            clip = &c;
    CHECK (clip != nullptr && clip->start == (std::int64_t) bar && clip->length == (std::int64_t) bar && clip->track == 1);
    // the held value is thinned (two points of the same value in between go)
    CHECK (song.automations[0].points.size() == 3 && song.automations[0].points.front().tick == 100 && song.automations[0].points.back().tick == 400);
    CHECK (song.automations[0].name == "Insert 1 - Volume" && song.automations[0].target == target);
    // the next batch goes on into bar 3: the take's clip grows by a bar
    lynx::commitAutomationPoints (song, w, { { 2 * bar - 10, 0.5f }, { 2 * bar + 50, 0.8f } }, 0);
    for (const auto& c : song.playlist)
        if (c.kind == lynx::ClipKind::automation)
            clip = &c;
    CHECK (clip->length == 2 * (std::int64_t) bar && song.automations[0].points.back().value == 0.8f);
    CHECK (std::abs (song.automations[0].valueAt (bar + 50.0) - 0.8f) < 1e-6f);
}

TEST (rea143_recording_over_an_existing_clip_replaces_only_the_gesture)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    song.automations.clear();
    const lynx::AutomationTarget target { lynx::AutomationTargetKind::channelPan, 0 };
    lynx::Automation a;
    a.target = target;
    a.points = { { 0, 0.0f, 0.0f }, { 1000, 1.0f, 0.0f } }; // a ramp
    song.automations.push_back (a);
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::automation;
    clip.automation = 0;
    clip.start = 960;
    clip.length = 1000;
    song.playlist.push_back (clip);
    lynx::AutomationWrite w;
    w.target = target;
    // a gesture at song ticks 1360..1460 = automation ticks 400..500
    lynx::commitAutomationPoints (song, w, { { 1360, 0.9f }, { 1460, 0.9f } }, 0);
    const auto& p = song.automations[0].points;
    CHECK (w.automation == 0 && ! w.createdByTake && song.automations.size() == 1);
    // the ramp before (kept up to the gesture) and after (comes back right after it)
    CHECK (std::abs (song.automations[0].valueAt (200.0) - 0.2f) < 1e-3f);
    CHECK (std::abs (song.automations[0].valueAt (450.0) - 0.9f) < 1e-6f);
    CHECK (std::abs (song.automations[0].valueAt (800.0) - 0.8f) < 2e-3f);
    CHECK (p.front().tick == 0 && p.back().tick == 1000);
    // a following batch of the same gesture continues it (no point of the old curve in between)
    lynx::commitAutomationPoints (song, w, { { 1560, 0.95f } }, 0);
    CHECK (std::abs (song.automations[0].valueAt (550.0) - 0.925f) < 1e-3f);
    // points past the end of an existing clip are not written (the clip is not stretched)
    w.lastLocal = -1;
    const auto before = song.automations[0].points.size();
    lynx::commitAutomationPoints (song, w, { { 1965, 0.1f } }, 0);
    CHECK (song.automations.size() == 2 || song.automations[0].points.size() == before);
}

TEST (rea143_recorded_automation_quantize_and_loop_wrap)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::song;
    song.playlist.clear();
    song.automations.clear();
    song.loopStart = 0;
    song.loopEnd = 4 * lynx::ppq; // a one-bar loop
    CHECK (lynx::songTickForRecorded (song, 4.0 * lynx::ppq + 10.0) == 10);
    lynx::AutomationWrite w;
    w.target = { lynx::AutomationTargetKind::tempo, 0 };
    // the take's points on the snap (FL's Quantize › Automations); the wrap splits them into two stretches
    const auto step = (std::int64_t) lynx::ticksPerStep;
    lynx::commitAutomationPoints (song, w, { { 3.0 * step + 3, 0.4f }, { 4.0 * lynx::ppq + 2, 0.6f } }, step);
    CHECK (song.automations.size() == 1);
    // the recorded values sit on the grid (an old-curve boundary point may follow the second stretch)
    bool onGrid = true;
    for (const auto& p : song.automations[0].points)
        if (p.value == 0.4f || p.value == 0.6f)
            onGrid = onGrid && p.tick % step == 0;
    CHECK (onGrid && song.automations[0].points.front().tick == 0 && song.automations[0].points.front().value == 0.6f
           && song.automations[0].points.back().tick == 3 * step && song.automations[0].points.back().value == 0.4f);
}

TEST (rea143_recorded_events_in_pattern_mode)
{
    auto song = lynx::Song::makeDefault();
    song.mode = lynx::PlayMode::pattern;
    song.currentPattern = 0;
    song.patterns[0].events.clear();
    const lynx::AutomationTarget target { lynx::AutomationTargetKind::channelVolume, 0 };
    lynx::AutomationWrite w;
    w.target = target;
    const std::int64_t loop = 4 * lynx::ppq;
    lynx::commitEventPoints (song, 0, w, { { 100, 0.3f }, { 110, 0.3f }, { 120, 0.5f } }, loop, 0);
    CHECK (song.patterns[0].events.size() == 1 && song.patterns[0].events[0].target == target);
    const auto& ev = song.patterns[0].events[0].events;
    CHECK (ev.size() == 2 && ev[0].tick == 100 && ev[1].tick == 120 && ev[1].value == 0.5f); // repeats go
    // a second pass over the loop (absolute ticks past the loop) replaces the gesture's range; the value that held
    // before the gesture comes back after it
    lynx::AutomationWrite w2;
    w2.target = target;
    lynx::commitEventPoints (song, 0, w2, { { (double) loop + 105, 0.9f } }, loop, 0);
    const auto& ev2 = song.patterns[0].events[0].events;
    CHECK (std::abs (song.patterns[0].events[0].valueAt (105.0) - 0.9f) < 1e-6f);
    CHECK (std::abs (song.patterns[0].events[0].valueAt (106.0) - 0.3f) < 1e-6f);
    CHECK (std::abs (song.patterns[0].events[0].valueAt (130.0) - 0.5f) < 1e-6f && ev2.size() == 4);
}

TEST (rea143_master_plays_its_audio_input)
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.mixer = lynx::Song::defaultMixer (2);
    song.mixer[0].inputChannel = 0; // the Master's input selector (FL's slot panel)
    song.mixer[0].monitor = lynx::InputMonitor::automatic; // no arm on the Master: anything but "off" plays
    auto state = lynx::RackState::build (song, nullptr, 48000.0);
    lynx::RackRenderer renderer;
    renderer.prepare (48000.0, 512);
    lynx::LiveInput input;
    renderer.setLiveInput (&input);
    std::vector<float> a (256, 0.25f), b (256, -0.5f), l (256), r (256);
    input.channels[0] = a.data();
    input.channels[1] = b.data();
    input.numChannels = 2;
    renderer.process (state.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[100] - 0.25f) < 1e-3f && std::abs (r[100] + 0.5f) < 1e-3f);
    CHECK (std::abs (input.meters.takePeak (0, 0) - 0.25f) < 1e-6f);
    song.mixer[0].monitor = lynx::InputMonitor::off;
    auto off = lynx::RackState::build (song, state.get(), 48000.0);
    renderer.process (off.get(), true, 0.0, 256, l.data(), r.data());
    CHECK (std::abs (l[200]) < 1e-6f);
}
