# REA round 143 — FL Studio technologies (Last tweaked, automation recording, Import) + design fidelity, continued

Brief (2026-10-23 night): continue round 139 ("Where I stopped"): the Import submenu, "Last tweaked" (Tools › Last
tweaked: link / automate the last moved control), automation recording (the record menu's "Automation" filter and the
Quantize section's "Automations"), the Master's audio input selector in the effects panel, per-channel smart disable
(only if it changes behaviour); then the next tier of FL vs LYNX screens.

- **Targets:** `FL Studio 2026/FLEngine_x64.dll` (build 5654; our own copy of the round-134 Ghidra project,
  `~/ghproj/r143`).
- **Tools:** Ghidra 12.1.4 headless with our helper scripts (`StrRefs.java`, `DecAt.java`); our own Python readers
  (`tmp/flrea143/py/`): `dfm.py` (Delphi binary form resources TPF0 → text), `fld.py` (published field table entries:
  offset of a form's component field), `meth.py` (published method table: code address of an event handler), and
  round 139's `.flp` survey of record 0xD4 (`tmp/flrea139/py/d4.py`, 400 projects).
- **Evidence markers:** **[DECOMP]** Ghidra pseudocode, **[DATA]** form resources / strings / tables / saved files,
  **[EXEC]** observed running FL, **[model]** our reading; "not confirmed" where unverified.
- Clean-room: behaviour, names and formats only; LYNX's code is our own.

---

## Part B — technologies

### B1. Last tweaked parameters **[DATA DFM `TFruityLoopsMainForm`: `LastTweakedParamsMenu`, `LastTweaked1Menu` (Tag 0), `LastTweaked2Menu` (Tag 1), `Add_AutoLastMenu`; DECOMP `FUN_0123d1d0` (LastTweakedMenuClick), `FUN_01231100` (Add_AutoLastMenuClick), `FUN_01234ae0`]**

- FL keeps the **two** last tweaked parameters in a 2-entry table (`PTR_DAT_015e31a8`, −1 = none) **[DECOMP
  `FUN_01234ae0`: counts the entries ≠ −1; title "Last tweaked parameter" / "Last 2 tweaked parameters" / "No
  parameter selected"]**.
- Tools has the section "Last tweaked parameters" with **Last tweaked** and **Next to last tweaked** (the item's Tag
  indexes the table) **[DATA]**. Opening one (`FUN_0123d1d0`) puts the parameter's name in the first item ("No
  parameter" when empty), then copies 17 items of the control's own parameter menu (items 7… of the menu at form
  +0xa78) and their enabled / checked state (`FUN_011ffe20`) **[DECOMP]**. That parameter menu is, in the DFM
  **[DATA]**: *-Automation*, Edit events, Edit events in new window, Edit events in piano roll, Init song with this
  position, Delete initial value, Create automation clip, Edit automation clip…, *-Remote control*, Link to
  controller…, Override global link…, *-Value*, Copy value, Paste value, Type in value…, Set….
- Add › **Automation for last tweaked parameter** (`FUN_01231100`): when entry 0 ≠ −1, creates the automation clip
  of that parameter (`FUN_01191a50 (param, 1, 0, 1, 1)`) **[DECOMP]**.

**LYNX before:** no record of the last moved control. **Now:** every automatable control reports its moves
(`ui::onTweak` from `ValueDrag::update` / `wheelValue`: rack volume / pan, mixer faders / pans, effect knobs, slot
mix, the Master volume; the tempo display; a linked MIDI controller's moves; a hosted plugin editor's parameter
gestures through `PluginWindows::onGesture` → `AudioEngine::targetOfPlugin`) to `AudioEngine::noteTweaked`, which
keeps the last two (`getLastTweaked (0 / 1)`). Tools › **Last tweaked parameters** › *Last tweaked* / *Next to last
tweaked* open the parameter's menu: its name, *Automation* (Create automation clip, Edit events in the pattern, Link
to MIDI controller, Link to internal controller, Unlink), *Edit automation clip…*, *Value* (Copy value, Paste value,
Type in value…). Add › **Automation for last tweaked parameter (name)**. Not done: FL's "Init song with this
position" / "Delete initial value" (LYNX has no initialized-controls list: a project already saves every control's
value), "Edit events in piano roll", "Override global link".

### B2. Automation recording **[DATA DFM `RecMenu`: `Recfilter1Menu` "Automation" Tag 1, `Recfilter2Menu` "Notes" Tag 2, `Recfilter3Menu` "Audio" Tag 4, `Recfilter4Menu` "Clips" Tag 8, `RecQuantizeAutomationsMenu`; DECOMP `FUN_011ff990` (Recfilter1MenuClick), `FUN_011ff9d0`, round 139 `FUN_012ca680`, `FUN_0119fad0`]**

- The recording filter is a bit mask, the setting `RecordingFilter2` (`PTR_DAT_015e2c08`); a filter item toggles its
  Tag's bit (`FUN_011ff990`: `mask | tag` / `mask & ~tag`) and the menu shows each bit (`FUN_011ff9d0`, items at form
  +0x13d8, +0x13e0, +0x13e8, +0x1b78 — field offsets read with `fld.py`) **[DECOMP + DATA]**. Order in FL's menu:
  Automation, Notes, Audio, Clips **[DATA]**.
- Quantize › "Automations" is `RecQuantizeAutomationsMenu` (+0x22a0) = setting `QuantizeAutomations`
  (`PTR_DAT_015e0008`) **[DECOMP `FUN_011ff9d0`]**; automation events are put on the snap only with it and a snap
  that is not "none" / "line" (round 139, `FUN_012ca680`) **[DECOMP]**.
- Where recorded automation goes (song mode: automation clips; pattern mode: pattern events) **[model: FL's
  documented behaviour; not traced in the binary this round]**.

**LYNX before:** the record menu had Notes and Audio only; nothing recorded the controls. **Now:** the filter has
**Automation** first (on by default; remembered), the Quantize section **Automations**, and the "What would you like to
record?" choice *Notes and automation*. While recording and playing, each touched parameter (a touch lasts while the
control keeps moving, 350 ms, or a plugin editor's gesture until it ends) is sampled at the engine's 30 Hz timer
(`AudioEngine::updateAutomationRecording`) and written about five times a second, the whole take one undo step:
- song mode (`commitAutomationPoints`): into the automation clip of that parameter playing there; else a new
  automation and clip from the bar of the first point, on the first track free from there on, growing by bars while
  the take goes on. Only the gesture's stretch is replaced (the curve before and after it stays: a boundary point of
  the old curve on each side); points past an existing clip's end are not written; a loop's wrap splits the points;
  equal values in a row are thinned.
- pattern mode (`commitEventPoints`): into the current pattern's event lane for the parameter (the Event editor's
  lanes), one event per tick, the value that held before the gesture coming back after it.
- with Quantize › Automations, the points go to the record grid (the global snap).
Tests `rea143_recorded_automation_makes_a_clip_that_grows`, `rea143_recording_over_an_existing_clip_replaces_only_the_gesture`,
`rea143_recorded_automation_quantize_and_loop_wrap`, `rea143_recorded_events_in_pattern_mode`.
Not done: FL's "Clips" filter (performance-mode clip launches; LYNX has no performance mode); while a parameter
with automation plays, the playback still drives it between two writes (FL lets the touched control win at once).

### B3. File › Import **[DATA DFM `ImportMenu` › `ImportMIDIMenu` (Tag 3, action `FileImportMIDIFileAction` "&MIDI file..."); DFM `TMIDIImportForm`]**

- FL's File › Import holds **MIDI file…** **[DATA]**, which opens the "Import MIDI data" window **[DATA]**: *Start new
  project* (off; hint "Start blank project or append to existing one?"), *Create one channel per track* (on; "One
  channel per track or per MIDI channel?"), *Realign events* (off; "Removes any silent part at the beginning"),
  *Import time signatures* (on), *Import zero velocity notes* (off), *Set mixer tracks for new channels* (on; "Route
  new channels to consecutive mixer tracks"), *Channel type* (FLEX / MIDI Out / MIDI Out with Fruity LSD), the
  tracks / channels / controller to import, *Blend with existing data* (piano roll import), **Accept**.

**LYNX before:** a `.mid` could only be *opened* as a new project. **Now:** File › **Import** › *MIDI file…* → the
**Import MIDI data** window with FL's options as LYNX applies them (`MidiFileIo::ImportOptions`, `importInto`): new
project or added to the open one (new channels + a new pattern that becomes current), one channel per track or per
MIDI channel, realign, time signature (new project), consecutive mixer tracks, channel type LYNX synth or MIDI Out on
the note's MIDI channel; the choices are remembered. Not done: track / channel / controller selection, zero-velocity
notes, Blend.

### B4. The Master's audio input **[EXEC round 139 capture 07: FL's slot panel shows the input selector on the Master too]**

**LYNX before:** only inserts 1… had the input row. **Now:** the Master's effects panel has the input box (no arm):
its input plays through the Master's effects whenever its monitoring is not *Off* (`RackRenderer::mixLiveInput` from
track 0; the input menu offers *Off* / *On* for the Master; `setInsertInput` / `setInsertMonitor` accept track 0).
Test `rea143_master_plays_its_audio_input`.

### B5. Per-channel smart disable — not done, no behaviour change **[DATA .flp survey; round 139 B6]**

In 400 projects, the 56 026 generator records 0xD4 (+8 = −1) carry flags 0x50 / 0x51 / 0x250 — the smart disable
bit 0x20 is **never** set on a channel (effects: 6 633 of 11 336 have it) **[DATA]**. FL therefore processes generator
plugins all the time, as LYNX does (a hosted plugin's `idle()` is false; only LYNX's own engines skip when silent,
losslessly). Adding the switch would not change what imported projects do.

---

## Part A — FL vs LYNX screens

### A.1 Piano roll Tools, Playlist track / clip menus, Mixer track menu (sub-round r143a)

Method: FL Studio 2026 (build 5654) driven under ~/.cache/lynx-fl.lock with SendInput/CopyFromScreen (tmp/flrea143/a/fl.sh) on a
copy of `Ookay - Thief.flp` (D:/lynx-work/flrea143a) **[EXEC]**; captures in tmp/flrea143/a/flref (not in the repo); LYNX's
menus from `--snapshot` (`<prefix>-menus143.png`, the clip menus in `-clip-menu-*.png`). Side by side (FL left, LYNX right): tmp/flrea143/compare/a-prtools.png, a-trackmenu.png, a-clip_pat.png, a-clip_audio.png, a-clip_auto.png, a-mixmenu.png. Builds: build-local with FL Data 1482142 checks, 0 failures; Windows build OK (build-win-flrea143a).
FLEngine strings (own Ghidra copy ~/ghproj/r143a, StrRefs): the menu captions are in the resource string pool (0x0146xxxx…, no
xrefs: "&Save mixer track state as...", "&Open mixer track state...", "&Merge clips", "Chop in bars/beats/half beats/quarter
beats/8th beats/16th/32nd", "&Split by channel", "Arpeggiate...", "Claw machine..." …) **[DATA]**; the piano roll tools' own
forms (`forms.prflamform`, `forms.prstrumform`, `forms.prflipform`, `TPRLegatoForm`, "quick quantize" / "glue" / "quick chopping"
undo names at 0x00e39b80…0x00e3b178, `FUN_00e3b630` "Chopped into %d pieces") **[DATA]**. Menu structure = **[EXEC]**.

| # | Screen | FL Studio 2026 | LYNX before | LYNX now |
|---|---|---|---|---|
| A1 | Piano roll › Tools | Built-in: Riff machine Alt+E, Generate chord progression Alt+P \| Quick legato Ctrl+L, Articulate Alt+L \| Quick quantize Ctrl+Q, Quick quantize start times Shift+Q, Quantize Alt+Q \| Quick chop Ctrl+U, Chop Alt+U \| Glue Ctrl+G \| Arpeggiate Alt+A, Strum Alt+S, Flam Alt+F, Claw machine Alt+W, Limit Alt+K, Flip Alt+Y, Randomize Alt+R \| Scale levels Alt+X, LFO Alt+O; second column "Scripts": Arpeggiator, Euclidean, Humanize, Note repeater sprinkler, Select by velocity and more (Python scripts) | every tool present, but grouped differently (dialog tools first, quick operations only in the "more" menu), keys only for 6 tools | FL's groups, order and keys (8 new shortcuts: `pianoRoll.quantize/chop/flam/claw/lfo/articulate/riffMachine/chordProgression`); "More tools" column with LYNX's own **Euclidean rhythm**, **Humanize**, **Note repeater** dialogs and a **Select by velocity** submenu (core: `notetools::euclidean`, `humanize`, `repeatNotes`, `selectByVelocity`, `selectLoudest`) **[EXEC + model: own designs, not FL's scripts]** |
| A2 | Playlist track header menu | Rename/color/icon, Change color, Random color, Change icon, Auto name, Auto name clips, Reset \| Track mode ▸ (Unassigned / Audio track / Instrument track), Performance settings ▸ \| Size ▸, Lock to this size \| Group with above, Ungroup, Auto color group, Collapse grouped \| Current clip source ▸, Lock to content, Merge clips, Consolidate track(s) ▸ (From track start / From song start), Mute all clips, Unmute all clips \| Show ghost notes \| Insert one, Clone, Delete \| Move up, Move down | the same except icon, Auto name clips, Track mode, Performance settings, Lock to this size, Lock to content, Merge clips, Consolidate, Show ghost notes | + **Merge clips** (`clipops::mergeTrackClips`: every run of clips that continue each other becomes one). Track modes (audio/instrument tracks), performance mode, consolidate and content lock are features LYNX does not have yet (not done) |
| A3 | Pattern clip menu | Preview \| Muted \| Rename and color, Change color, Random color \| Select source pattern ▸, Edit pattern, Make unique, Select all similar clips, Delete \| Use current time signature from ▸ (Playlist to pattern / Pattern to playlist) \| Region: Select region ▸, Chop ▸ (Time based, Repeating, Marching, Patterns, Complex) | Properties, Rename and color, Make unique, Select all clips of this source, Split here, Chop to grid, Merge, Open in Channel Rack / Piano Roll, Mute, Cut/Copy/Paste/Delete | + **Change color ▸**, **Random color** (the pattern's colour), **Select source pattern ▸** (`clipops::setClipSource`), **Chop ▸** (to grid, in bars / beats / half / quarter / 8th beats). Not done: Preview from the menu, time signature, regions |
| A4 | Audio clip menu | Preview, Muted, Rename/Change color/Random color/Change icon, Select source channel ▸, Channel settings, Make unique, Select all similar, Delete, Properties \| Sample: Make unique as sample, Edit sample, Pitch-correct, Time-warp, Extract stems, Deverb, Detect tempo, Fit to tempo \| Automation: Automate ▸, Crossfade with ▸ \| Fades: Reset fades, Fade in / out curve ▸ \| Region | Properties, Rename, Make unique, Reverse, Normalize, Clip volume, Fit to tempo, Remove fades, Open in new Sampler, Edit in audio editor, Show file | + **Change color ▸ / Random color** (the channel's colour), **Select source channel ▸** (the audio-clip channels), **Channel settings**, Chop ▸. Not done: stems, deverb, pitch-correct, time-warp, fade curves, crossfade |
| A5 | Automation clip menu | Preview, Muted, Rename/colour/icon, Select source channel ▸, Channel settings, Articulator tools ▸, Make unique, Select all similar, Delete, Region | Properties, Make unique, Select all…, Edit points, Delete automation | + **Select source channel ▸** (the project's automations), Chop ▸ |
| A6 | Mixer track menu | header (track colour) \| Rename/color/icon F2, Change color, Random color, Gradient color, Change icon, Assign to new Audio track(s) \| **File ▸** (Open mixer track state…, Save mixer track state as… Shift+Ctrl+S, Browse states, preset list) \| Open audio editor (Ctrl+E), … and record \| Reset selected track(s) to default \| **Routing**: Channel routing ▸ (Route selected channels to this track Ctrl+L / starting from this track Shift+Ctrl+L), Track routing ▸, **Select ▸** (Channels routed to this track Alt+L, Channels and playlist tracks routed…, Tracks this one is routed to, Tracks routed to this one Ctrl+Alt+L, Group, All Ctrl+A), Reset selected track(s) routing, Unroute tracks routed to the selected track(s) \| Solo selected S, Alt solo, Arm selected, Disarm selected \| Compensate for master latency \| **Group**: Separator, Create group, Auto color group \| **Layout**: Dock to ▸ (Left/Middle/Right), Move left Alt+Left, Move right Alt+Right \| Delete, Insert one, Insert multiple…, Insert from state ▸ \| **CPU**: Allow threaded processing | flat list: Rename and color, Change color, Random color, Insert one, Delete, Audio input, Arm, route channel / link items, route selected inserts, Move left/right, Send only…, Stereo separation, Track EQ, Invert, Swap, Freeze, Rename…, Reset, CPU | FL's sections in FL's order: **File ▸** with LYNX's own **mixer track state** files (`.lynxtrack`, JSON: effects with their states, level, pan, separation, phase, swap, EQ, name, colour — `ProjectFile::mixerTrackStateToJson / FromJson / applyMixerTrackState`, folder Documents/LYNX/Mixer states, listed in the submenu), Open audio editor, **Reset selected track(s) to default**, **Routing** (Channel routing ▸, **Select ▸**, **Reset selected track(s) routing**, **Unroute…**, LYNX's route-the-selected-inserts items), **Solo / Arm / Disarm selected track(s)**, a **Sound** section (LYNX's separation / EQ / phase / swap / freeze), **Layout** (Move, Delete, Insert one, **Insert multiple…**, **Insert from state ▸**), CPU. Core: `mixerops::resetToDefault, resetRouting, unrouteInto, destinationsOf, sourcesOf, insertTracks`. FL's .fst track states are not read (FL's own binary preset format). Not done: icons, gradient colour, audio tracks, groups/separator, dock to, master latency compensation, alt solo |

Tests (tests/Rea143aTests.inc): `rea143_piano_roll_tools_have_fls_keys`, `rea143_euclidean_humanize_repeat_and_select_by_velocity`,
`rea143_playlist_track_merge_and_clip_source`, `rea143_mixer_track_menu_operations`.

### A.2 Channel settings, wrapper settings, Browser menus, Options pages, hint bar (sub-round r143b)

<!-- B-NOTES -->

---

## Where I stopped

