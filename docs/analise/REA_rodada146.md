# REA round 146 — FL Studio technologies (initialized controls, global links, Misc tab, mixer groups, track modes, wrapper page) + design fidelity, round 3

Brief (2026-10-24): continue round 143 ("Where I stopped"): the moved control overriding its playing automation while
recording; the "Clips" record filter; the parameter menu's Init song with this position / Delete initial value, Edit
events in piano roll, Override global link; the MIDI import window's track / channel / controller selection,
zero-velocity notes, Blend; the Misc tab's FL-only parameters; mixer groups and dock-to; Playlist track modes /
consolidate; FL's plugin wrapper settings page. The round ran as three sub-rounds: main (parameter menu, recording,
initialized controls, global links), r146a (mixer, Playlist tracks, wrapper page), r146b (Misc tab, MIDI import).

- **Targets:** `FL Studio 2026/FLEngine_x64.dll` (build 5654; our own copies of the round-134 Ghidra project,
  `~/ghproj/r146`, `r146a`, `r146b`), FL's own render test projects and the installation's Data folder.
- **Tools:** Ghidra 12.1.4 headless with our helper scripts (`StrRefs.java`, `DecAt.java`, `Xr.java`, r146a's
  `FindOps.java`); our Python readers (`dfm.py`, `meth.py` published-method table, new `va.py`: a VMT's class name);
  `.flp` surveys over the 400-project corpus; r146b's FL renders (`FL64.exe /R /Ewav`) of one-record changes of FL's
  `Render tests/Sampler.flp`, measured with our own analysers (tmp/flrea146/b, not in the repo).
- **Evidence markers:** **[DECOMP]** Ghidra pseudocode, **[DATA]** form resources / strings / tables / saved files,
  **[EXEC]** observed running FL, **[model]** our reading; "not confirmed" where unverified.
- Clean-room: behaviour, names and formats only; LYNX's code is our own.

---

## Part B — technologies (main sub-round)

### B1. The touched control wins over its playing automation while recording **[model: FL's behaviour, round 143 "not done"; LYNX design]**

**LYNX before:** while recording, a parameter with automation was still driven by the playback between two writes
(the knob jumped back until the next five-per-second commit). **Now:** every engine tick (30 Hz) the touched targets
are turned into their automation slots (`RackState::slotsOfTarget`: the song's automations, then the pattern event
lanes, numbered as `buildAutomationTables` numbers them) and handed to the renderer
(`RackRenderer::setOverriddenSlots`, a 1024-bit atomic mask). An overridden slot is not driven by its clips / events
(`clearOverriddenValues` before the plugin parameters are applied; the tempo automation too): the control's own value
plays at once, until the gesture ends and its points are written. Test `rea146_touched_control_overrides_its_automation`.

### B2. Initialized controls: Init song with this position / Delete initial value **[DATA DFM `Param_InitMenu` "&Init song with this position", `Param_DeleteInitMenu`, `MLParam_InitMenu` / `MLParam_InitPatMenu`; DECOMP `FUN_012009e0` (Param_InitMenuClick), `FUN_01052040`, `FUN_012448c0` → `FUN_010520d0`, `FUN_011e7470`, `FUN_010703d0`, `FUN_00ff98c0`]**

- *Init song with this position* (`FUN_01052040 (control, −1)`): reads the control's current raw value
  (`FUN_01046a70`) and adds an event at tick 0 for its control ID to the song's **init list** (`PTR_DAT_015dfc48`, the
  same event list type as pattern events). With a pattern number (the multi-link menu's *Init pattern with these
  positions*) the event goes into that pattern's own events instead **[DECOMP]**.
- *Delete initial value* (`FUN_010520d0`): removes every event of that control ID from the init list (tick range
  0x80000001 … 0x7fffffff) **[DECOMP]**.
- The list is played whenever the song position is set in **song mode** (`FUN_011e7470`: when the mode global is 1 it
  replays the init list, `FUN_01065af0 (…, 0x80000001)`, besides the pattern events up to the position), i.e. when
  the song starts and on a jump **[DECOMP]**. The Browser lists them in *Current project › Initialized controls*, hint
  "To be reset when the song starts" (`FUN_010703d0`) **[DATA]**. It is the .flp's 0xD8 record (round 85).

**LYNX before:** 0xD8 was applied once at import as the targets' values. **Now:** `Song::initialValues` (target,
value), kept by the import (0xD8), saved in .lynx (`initialValues`), renumbered / dropped with the channels / slots
(`forEachTarget`); every parameter menu has *Init song with this position* (checked when set) and *Delete initial
value*; `AudioEngine::resetToInitialValues` puts them back when playback starts in song mode (`play()`, so exports and
renders too) and on a seek while playing; hosted plugins' parameters get them through `setParameterFromUi`. Test
`rea146_initial_values_reset_the_song_like_fl`. The Browser's *Current project* has FL's **Initialized controls**
section (each target with its value; right-click *Delete initial value*). Not done: *Init pattern with these
positions* (LYNX has no multi-link selection).

### B3. Edit events in piano roll **[DATA DFM `Param_EditPRMenu` Tag 2 → `Param_EditMenuClick` `FUN_01200970` → `FUN_00e34450 (…, tag)`]**

**LYNX before:** only the Event editor window. **Now:** the parameter menu's *Edit events in piano roll* opens the
Piano roll (selecting the channel when the parameter is a channel's) with the parameter's pattern events in the
bottom lane: the lane box lists *Velocity / Pan / Fine pitch*, then a *Pattern events* section with the pattern's event
lanes; drag draws one event per grid step (`eventedit::fill`), right-drag deletes (`eventedit::erase`), the events show
as a held-value line with dots.

### B4. Override global link **[DATA DFM `Param_GenLinkMenu` "&Override global link..."; DECOMP `FUN_012009a0` → `FUN_00f778e0` → `FUN_00f77920`, which opens the form class `TMIDIInputForm_Generic` (VMT read with our `va.py`), DFM caption "Global link settings"; strings `System\Config\Mapping\Generic`]**

FL keeps *global* controller links in its user data (`Mapping\Generic`), for every project; *Link to controller*
(`FUN_00f8a040` → `FUN_00f83a20`) makes the project's own link. **LYNX before:** project links only. **Now:**
`prefs::MidiPrefs::globalLinks` (Settings file, `link=cc|channel|kind|index|slot|param`), *Override global link...*
learns one (the next CC moved), *Remove global link (CC n)*; a project link of the same target or controller wins
(`linksWithGlobal`); Settings › MIDI lists the project's links, then the global ones. Test
`rea146_global_links_and_project_override`.

### B5. The "Clips" recording filter — not done (performance mode only) **[DECOMP `FUN_0119f5b0`, `FUN_0126bd90`, `FUN_011fc7b0`, `FUN_00d274e0`, `FUN_00d0bde0`, `FUN_011c9ab0`]**

The filter bit 8 is copied with the others into the active recording mask at record start (`FUN_0126bd90`); it is
read only by the performance-mode clip scheduler (`FUN_00d0bde0` → `FUN_00d274e0`: a launched clip is written to the
playlist track) and the periodic playlist refresh while recording (`FUN_011fc7b0` → `FUN_01221880`); the record
button's hint lists "automation, score, audio, clips". Curiosity: the "start recording on input" test
(`FUN_011c9ab0`) checks a clip event with `(mask & 1) == 8`, which is never true **[DECOMP]**. LYNX has no performance
mode, so the filter would do nothing; left out.

---

## Part C — mixer groups and docks, Playlist track modes, wrapper settings page (sub-round r146a)


Target: `FL Studio 2026/FLEngine_x64.dll` (build 5654), own Ghidra copy `~/ghproj/r146a` (from r134), helper scripts in
`tmp/flrea146/a/gh/scripts` (StrRefs, DecAt, new FindOps = functions whose instructions contain all given operand
substrings); published-method addresses read with tmp/flrea143/py/meth.py-style lookup; .flp surveys
`tmp/flrea146/a/py/` (ins.py, ins2.py, ins3.py, trk.py, tm.py) over the 400-project corpus (tmp/flrea134/flps.txt) and the
165 FL Data .flp files. No FL run this sub-round (FL was busy with r145) — everything is [DECOMP]/[DATA].

### C1. Mixer: Group (Separator, Create group, Auto color group, Select › Group) and Layout › Dock to

- Published handlers **[DATA method table]**: `SeparatorMenuClick` 0x012ab440, `CreateGroupMenuClick` 0x012aa200,
  `AutoColorGroupMenuClick` 0x012ab2b0, `SelectGroupMenuClick` 0x0128f130, `DockToMenuClick` 0x0128f9b0 (+ the item
  click `FUN_0128f800`, undo name "dock mixer track(s)").
- Mixer track record = 0x1478 bytes per track (`PTR_DAT_015e06a0`); **+0x2c = separator**, **+0x2d = dock** (menu Tag
  0 left / 1 middle / 2 right) **[DECOMP FUN_012ab440, FUN_0128f800]**.
- Separator: sets +0x2c of every selected track to the item's checked state (undo "mixer track separator(s)") **[DECOMP]**.
- Create group (`FUN_012aa200`, undo "group mixer tracks"): for each selected track, if its display neighbour on the
  left (`FUN_0127c990(i,-1)`) is not selected it gets a separator; if its right neighbour is not selected, that neighbour
  gets one; then the selected tracks without a colour take the current track's colour **[DECOMP]**. So FL's mixer
  "group" = a run of tracks between separators.
- Auto color group (`FUN_012ab2b0`): selects the current track's group (`FUN_0128efc0(cur,1)`) and gives it the
  current track's colour **[DECOMP]**.
- .flp: FL's insert writer `FUN_00f3d620` writes record 0xEC flags = `(dock & 3) << 6` | 1 phase | 2 swap | 4 effects
  on | 8 enabled | 0x10 threaded off | **0x400 separator** | 0x800 | 0x1000 solo | 0x2000 | 0x4000 | 0x8000 **[DECOMP]**.
  Survey **[DATA]**: 12 413 / 13 213 inserts have 0x40 (middle); the Master and the last insert (FL's "Current" track)
  have dock 0 (left); 0x400 only in the FL demo NewStuff.flp (inserts 10, 12, 18, 20); 0x80 never.

| # | FL | LYNX before | LYNX now |
|---|---|---|---|
| G1 | Group: Separator / Create group / Auto color group; Select › Group | none | `MixerTrack::separator`, `mixerops::setSeparator / createGroup / autoColorGroup / groupOf` (FL's neighbour rule in display order, inside a dock block); mixer track menu **Group** section; **Select › Group**; separators drawn as a gap + line on the left of the track |
| G2 | Layout › Dock to › Left / Middle / Right | none | `MixerTrack::dock` (`MixerDock`), `mixerops::dockTracks / displayOrder`; the strips are laid out left block, middle (with the "+"), right block, a dark gap between blocks (`MixerStrips::layoutStrips`) |
| G3 | .flp flags | dock / separator not read | imported (0x400, bits 6–7; FL's "Current" track — the last insert — stays in the middle); saved in .lynx (`separator`, `dock`) |

Partial: FL's left/right docks stay pinned while the middle scrolls; LYNX's blocks scroll together. Not done: group
names (FL copies something at +0xc of the current track to unnamed selected tracks in Create group — not confirmed what).

### C2. Playlist track modes and Consolidate track

- .flp record 0xEE (70 bytes) **[DATA survey of FL demos, e.g. "Astes - Bien Duro.flp"]**: **u32 mode at byte 49** (0
  unassigned, 1 audio track, 3 instrument track), **i32 mixer track at 53** (audio: 1, 2, 3…; instrument: −1 or its
  track), **i32 FL channel index at 57** (instrument tracks: a generator channel, e.g. 198 "4.6 Lead"); matches the
  in-memory playlist track (0x114 bytes, mode at +0xb4, mixer at +0xb8; hint "(Instrument track)" /
  "(Audio track, linked to mixer track %d)" **[DECOMP FUN_00eab1f0]**).
- FL strings **[DATA]**: "Only generators can be used as instrument tracks.", "Can't create new audio or instrument
  track| There are no available mixer tracks.", "Make clip unique?|The clip "%s" is also used on another audio
  track. Do you want to make this instance unique and then route it to this audio track?", "Instrument tracks are not
  for audio clips.". Consolidate: `EEPLTrackConsolidateMenuClick` 0x012238c0 → `FUN_012ea170(playlist, tag == 1)`
  (TrackLength / SongLength items), test helper names the result "… (consolidated).wav" **[DECOMP FUN_011109e0]**.

| # | FL | LYNX before | LYNX now |
|---|---|---|---|
| T1 | Track mode ▸ Unassigned / Audio track / Instrument track | none | **Track mode** submenu: Audio track links the track to the first free mixer track (named / coloured after the track); Instrument track ▸ (generator channels) links the channel and its mixer track (a free one when needed); `trackmodes::*` |
| T2 | audio clips on an audio track go to its mixer track (making the clip unique when shared) | n/a | `trackmodes::followAudioTracks` after every clip edit (in `PlaylistComponent::sortClips`): the clip's channel is routed to the track's mixer track; a channel shared with clips elsewhere is copied first |
| T3 | hint bar "(Instrument track)" / "(Audio track, linked to mixer track N)" | name only | same text after the track name |
| T4 | Consolidate track(s) ▸ From track start / From song start | none | the track's unmuted clips rendered (32-bit float WAV in Music/LYNX/Renders, 2 s tail, through the mixer, Master without effects at unity) into one audio clip "<track> (consolidated)" on the same track, in place of those clips (one undo step; the new channel plays on the audio track's mixer track or the Master) |
| T5 | .flp / .lynx | — | 0xEE mode / mixer / channel imported (channel through the import's channel map); .lynx `playlistTrackModes` |

[model] FL's consolidate is assumed to render the clips' processed sound (not confirmed: rendering path not traced);
LYNX renders with the mixer and routes the result past it (Master), so it sounds the same. Tempo automation clips of
other tracks are not part of the render (a song with tempo changes renders at the playlist's tempo map shifted).
Not done: instrument-track editing behaviour (FL draws the channel's notes on the track itself), refusing audio clips
dropped on instrument tracks, track modes per arrangement.

Tests (tests/Rea146aTests.inc): `rea146a_mixer_separators_groups_and_docks`, `rea146a_flp_insert_flags_dock_and_separator`,
`rea146a_playlist_track_modes`, `rea146a_flp_playlist_track_modes`, `rea146a_consolidate_track`.

### C3. FL's plugin wrapper settings page

- FL's wrapper settings are built in code (no DFM form; `TPluginForm` only) **[DATA]**. Option names and hints from the
  resource string pool **[DATA, no xrefs]**: "Use fixed size buffers", "Use maximum buffer size from host",
  "Allow threaded processing" / "|Allow plugin to be multi-threaded by FL Studio", "Allow smart disable" / "|Allow smart
  disable for plugin", "Make bridged" / "|Load plugin in separate process (bridged)", "|MIDI input port number",
  "|MIDI output port number", "Notify about rendering mode" / "|Let the plugin know when rendering to audio file",
  "Send loop position" / "|Let the plugin know about loop start and end points", "Reset on transport", "Use time
  offset" / "|Adjust time information reported to a plugin", "Process inactive inputs and outputs", "Report latency",
  "Send note release velocity", "Send pitch bend range", "|Keep plugin editor in bridge process", "|Increase idle rate",
  the warning "While some plugins require processing buffers to always be the same size…".

| # | FL | LYNX before | LYNX now |
|---|---|---|---|
| W1 | plugin window header: the plugin page and the wrapper settings (gear) page | the plugin's editor alone; options only in the rack / slot menu › Processing | hosted-plugin windows opened from the Channel rack or a mixer slot get a header with **Plugin** / **⚙ Wrapper settings** tabs (`WrappedEditor`); the page (`WrapperSettingsPage`) shows Name + version, Vendor, Format (instrument), File, Latency (samples / ms), Inputs / outputs, Parameters, then **Processing** (Use fixed size buffers, Process maximum size buffers, Use maximum buffer size from host, Allow threaded processing, Allow smart disable), **Bridging** (Make bridged), **MIDI** (Input port) — the options LYNX applies, from one table `wrapperOptions()` (core/FlWrapper) |

Options that re-create the plugin (fixed buffers, bridged) keep its state and close the window, as the menu did. Not
done (no LYNX behaviour behind them): MIDI output port, Notify about rendering mode, Send loop position, Reset on
transport, Use time offset, Process inactive inputs and outputs, Report latency override, note-release velocity,
pitch-bend range, editor-in-bridge, idle rate; FL's parameter browser tab. Patcher-hosted plugin windows keep the plain
editor (no PluginRef path). Side-by-side capture not made (FL was held by r145 under the lock).

Test: `rea146a_wrapper_settings_page_options`.

---

## Part D — the Misc tab's FL-only parameters, the MIDI import window (sub-round r146b)


Method: FL Studio 2026 (build 5654) under `~/.cache/lynx-fl.lock`. (1) **[EXEC renders]** FL's own
`Data/System/Render tests/Sampler.flp` with one channel record changed (our generator `tmp/flrea146/b/py/mk.py`,
experiments `exp1…exp6.py`), rendered with `FL64.exe /R /Ewav` (`flr.sh`), measured with our analysers (`an.py`
segments, `steps.py` per-2.5 ms pitch runs, `env.py`); test tones 441 Hz (`tone.wav` 3 s, `blip.wav` 60 ms). (2)
**[EXEC saves]** FL's GUI (Misc tab of the Sampler channel): one control toggled, Ctrl+S, the saved events diffed
(`py/evdiff.py`). (3) **[EXEC hints]** FL's hint bar over the knobs. (4) **[DATA]** FL's `System/Config/Chords.map`,
FL's TMIDIImportForm (round 143 DFM), the online manual's "Channel settings - Miscellaneous" page (wording only).
(5) Ghidra (own copy `~/ghproj/r146b`, StrRefs) only for the hint strings ("^b^aFine time shift", "Delay number of
echoes", "|Process / leave first echo"…) **[DATA]**. Clean-room: behaviour and record layouts only.

### Records (FL 2026 saves, one control changed each) [EXEC saves + renders]

| Control | Record | Notes |
|---|---|---|
| Time › Shift | word event 0x59 | FL's time-knob value; 1024 = 1 step ("Fine time shift: 0:00") |
| Time › Gate | 0xD7 u16 at 64 | time-knob value; ≥ 1446 = Off (default 1447) |
| Time › Full porta | 0xD7 byte 62 | 1 = on (default) |
| Arpeggiator direction / range / chord / time / gate | 0xD7 i32 at 40 / 44 / 48 / 52 / 56 | direction 1 up, 2 down, 3 bounce, 4 sticky, 5 random; chord −1 Auto (sustain), −2 Auto, 0 None, k = line k of Chords.map; time = time-knob (1024 = 1:00); gate /48 (48 = 100 %) |
| Arpeggiator Slide | 0xD7 byte 60 | |
| Arpeggiator Repeat | 0xD7 i32 at 92 | 1 = default |
| Echo delay | 0xD1: i32 feedback, pan, pitch, echoes, time | feedback /12800 (12800 = as loud as the note), pan 6400 centre, pitch cents, time 1/192 beat (144 = 3:00) |
| Echo Mod X / Y | dword event 0x8A: u16 X, u16 Y | 128 = none |
| Ping pong / Fat mode | 0xD7 byte 10 bits 2 / 4 | |
| Enable main pitch | 0xD7 byte 11 | |
| Add to key | 0xD7 byte 63 | |
| PITCH knob / RANGE | 0xDB +8 cents / 0xD7 i32 at 112 | range default 2; the pitch is not limited by the range in renders (1200 cents → +12 st) |
| Vol / Key tracking | 0xE4 ×2 (velocity, key): i32 mid, pan, mod X, mod Y | defaults mid 100 / 60 |

### Behaviour [EXEC renders]

- **Time knobs**: beats = (27^(v/1024) − 1)·48/26 / 192 (the scale round 6 found for the porta slide); Shift 1000 →
  22 ticks at 96 PPQ, 800 → 11, 512 → 4, 400 → 2, 200 → 1; arpeggiator time 640/768/896/1024/1152/1280 →
  32/52/79/125/188/293 ms at 120 BPM; Gate 900 → 83 ms, 600 → 26 ms (rounded to the project's ticks).
- **Shift** delays note on and off; **Gate** cuts notes longer than it (with a volume envelope; FL's sampler without
  one plays the sample out).
- **Echo**: echo k at k·time, volume feedback^k (exactly 0.5 per echo at 6400; independent of velocity: velocity 50
  only scales all), pan k·step clamped (6400 → 3200: −0.5, −1, −1, −1), pitch k·step cents (100: +1, +2… st);
  **ping pong** bounces the pan off the ends (−0.5, −1, −0.5, 0).
- **Key tracking**: pan += (key − (120 − mid))/48 · pan/64 (mid 60: ±12 st → ±0.25 at pan 64; mid 72 → zero at 48).
  **Velocity tracking**: pan += 0.275 · pan/64 · (v(vel) − v(mid)), v = FL's velocity → volume curve.
- **Arpeggiator**: steps count from the chord's start (a chord at 52 ms arpeggiates at 52, 180, 307…), notes last
  time·gate and past 100 % the next step waits (gate 64 → 167 ms steps); sticky = the ends twice; Repeat r = the
  sequence r times per octave before the next octave (range 2, repeat 2: 69 73 76 69 73 76 81 85…); Repeat 0 plays
  the chord at once each step (not done); chord k on one note (5 → 0 3 7, 27 → 0 4 7 11, 0 → the note only).

### FL vs LYNX

| # | Item | FL Studio 2026 | LYNX before | LYNX now |
|---|---|---|---|---|
| M1 | Time › Gate / Shift / Full porta | knobs on the time-knob scale; porta notes ignore the gate with Full porta | missing (only Swing) | `ChannelMisc::gateBeats / shiftBeats / fullPorta`, applied by the scheduler (pattern loop and Playlist; `ScheduleIndex::maxShift` widens the note window); Misc tab knobs with FL's steps:ticks text; imported from 0x59 / 0xD7 |
| M2 | Echo delay / fat mode | Feed, Pan, Mod X, Mod Y, Pitch, Time, Echoes, Ping pong, Fat mode | missing | `channelmisc::NoteEcho` after the arpeggiator (echoes of every note, their offs with the same delay, volume factor through the new `Instrument::setNextNoteGain` — the sampler scales the voice, other engines the velocity); Misc tab section; imported from 0xD1 / 0x8A / 0xD7 byte 10. Mod X / Y and Fat mode stored only |
| M3 | Vol / Key tracking | Pan, Mod X, Mod Y, Mid (Vol and Key tabs) | missing | `trackedPan` at note on; tabs Vol / Key with Pan and Mid; imported from 0xE4. Mod X / Y stored only |
| M4 | Arpeggiator | directions incl. sticky, Time knob, Gate up to 267 %, Range, Repeat (per octave), Slide, Chord list | Off/Up/Down/Up-down/Random on the transport grid, gate ≤ 100 %, repeat = each note n times, no chord / slide; **not imported from .flp** | FL's timing (from the chord's start, gate past 100 %), sticky direction, Repeat per octave (`octaveRepeat`; LYNX's note repeat kept), Slide (slide events, no new attack), Chord (Auto / None / LYNX's chord list; FL's index read from the installation's Chords.map); the whole arpeggiator imported from 0xD7 |
| M5 | Levels adjustment Mod X / Mod Y | assignable modulation | missing | the channel filter's cutoff / resonance for sampler and synth channels (disabled for others) |
| M6 | Header PITCH + RANGE | pitch knob ± range semitones | missing | Pitch (the channel's pitch, ± range) and Range knobs in the header; range imported (0xD7 +112) |
| M7 | Enable main pitch | per channel | the master pitch applied to every channel | `misc.mainPitch` (imported from 0xD7 byte 11) |
| I1 | MIDI import: Which tracks / channels / controller | track list, 16 MIDI-channel toggles, controller −1…127 | all tracks and channels, no controllers | core `midiimport` (testable without JUCE): the selection, controller 7 / 11 → channel volume, 10 → pan, into the pattern's event lane; the window lists the file's tracks and the controllers LYNX applies |
| I2 | Import zero velocity notes | off by default | always note off | option: a note on with velocity 0 is a note (a note on of its key ends the open ones) |
| I3 | Blend with existing data (piano roll) | off: the channel's notes are deleted first | always pasted | the piano roll's Import .mid opens the window with Blend and the selection lists (`importIntoChannel`) |

Check after the change **[EXEC]**: LYNX's `renderwav` of the same experiment files against FL's renders
(`steps.py` / `an.py`): arpeggio up / up-down / chord 5 / range 2 repeat 2 / gate 64 / chord at tick 10 — the same
notes at the same times (±5 ms, the analyser's window); echo ping pong (pan −0.5, −1, −0.5, 0, levels ×0.5 per echo)
and echo pitch (+1…+4 st) the same. FL cuts an arpeggio note at its gate even on a one-shot sample: LYNX's
arpeggiator "off" now chokes the channel (except with the echo delay on).

Tests (tests/Rea146bTests.inc): `rea146b_fl_time_knob_scale`, `rea146b_shift_and_gate_schedule`,
`rea146b_echo_delay_events`, `rea146b_tracking_pan`, `rea146b_arpeggiator_like_fl`, `rea146b_flp_misc_records`
(FL's Sampler render test patched in memory, with the installation's Chords.map), `rea146b_midi_import_selection`.

Not done: Fat mode's effect (FL: "subtler echoes with alternating panning" — our renders did not separate it);
echo / tracking Mod X / Y (LYNX's notes route no mod X / Y); Repeat 0 (chord at once); "Auto" (no sustain) chord
differences; Add to key and the root-note keyboard (LYNX folds the root note into the sampler pitch at import); the
header Pitch acts on samplers and FL engines (LYNX's synth has no channel pitch); live-played notes skip the arp /
echo / tracking (as before).

---

## Where I stopped

- Done: the touched control wins over its automation while recording; FL's initialized controls (parameter menu
  Init song with this position / Delete initial value, reset when the song starts in song mode, .flp 0xD8 kept, saved,
  Browser › Current project › Initialized controls); Edit events in piano roll (the bottom lane draws pattern events);
  Override global link (global links in the MIDI settings, the project's own link wins); the "Clips" filter studied
  and left out (performance mode only). r146a: mixer separators / groups / docks, Playlist track modes and Consolidate
  track, the wrapper settings page. r146b: the Misc tab's time / echo / tracking / arpeggiator / pitch parameters with
  FL's measured behaviour, the MIDI import window's selections, zero-velocity notes and Blend.
- Not done: *Init pattern with these positions* (multi-link selection), the Clips filter (no performance mode), pinned
  left / right mixer docks while the middle scrolls, group names, instrument-track editing on the Playlist, the
  wrapper options without LYNX behaviour (listed in C3), Fat mode's effect, echo / tracking Mod X / Y, arpeggiator
  Repeat 0, Add to key, root-note keyboard; live-played notes skip the arpeggiator / echo / tracking.
- Not confirmed: whether FL resets initialized controls when playing from a position inside the song in every case
  (the decompiled position setter does it in song mode), what FL copies to unnamed tracks in Create group, FL's
  Consolidate rendering path. Side-by-side captures of these screens were not made this round (FL was held by r145).
