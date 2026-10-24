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

(see the table below; sub-rounds r143a — Piano roll tools, Playlist track / clip menus, Mixer insert menu — and r143b —
Channel settings, wrapper settings, Browser menus, Options pages, hint bar)

---

## Where I stopped

