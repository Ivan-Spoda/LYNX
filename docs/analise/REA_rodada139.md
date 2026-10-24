# REA round 139 ("flrea139") — FL Studio technologies (round 134 continued) and side-by-side design fidelity

Brief (2026-10-23 evening): continue round 134's "Where I stopped" (REA_rodada134.md, REA_rodada118.md Part B):
export entries per format and "Last exported format(s)", "Disable maximum polyphony", the record menu's Quantize
section and recording markers, the Browser's details box, "Use maximum buffer size from host", per-plugin "smart
disable"; then FL vs LYNX screens side by side, fixing layout / workflow / information differences (LYNX keeps its
own dark + red style, no FL graphics).

- **Targets:** `FL Studio 2026/FLEngine_x64.dll` (build 5654, the binary of rounds 69/72/76/118/134; our own copy of
  the Ghidra project, `~/ghproj/r139`) and `Fruity Wrapper_x64.dll` (`~/ghproj/r139w`, round 134's analysis).
- **Tools:** Ghidra 12.1.4 headless with our helper scripts (`StrRefs.java`, `DecAt.java`, `Xr.java`), `objdump -d`
  listings of round 134, our Python readers of the PE sections and of Delphi field tables (`tmp/flrea139/py/fields.py`:
  vmtFieldTable at VMT − 0xA0), a 0xD4 survey of 400 `.flp` files (`tmp/flrea139/py/d4.py`), and FL Studio itself
  driven with our CopyFromScreen / SendInput helpers under `~/.cache/lynx-fl.lock` on a copy of the demo
  `Ookay - Thief.flp` on D: (captures in `tmp/flrea139/flref`, not in the repo).
- **Evidence markers:** **[DECOMP]** Ghidra pseudocode, **[ASM]** disassembly, **[DATA]** strings / constants / resources
  in the binary or values in saved files, **[EXEC]** observed by running FL, **[DOC]** published documentation
  (PyFLP's source), **[model]** our reading; "not confirmed" where unverified.
- Clean-room: only behaviour, names and formats are described; LYNX's code is our own.

---

## Part B — technologies

### B1. Export entries per format and "Last exported format(s)" **[DECOMP FLEngine `FUN_011dd840`, `FUN_011dd800`, `FUN_011a5800`, `FUN_011af820`; ASM `0xd1f7af`; DATA field table of `TWAVRenderForm`]**

- File › Export opens the render window through `FUN_011dd840 (…, kind)`; kind 3 = Wave, 4 = MP3, 5 = OGG,
  6 = FLAC, 7 = MIDI, 9 = M4A, 10 = "Last exported format(s)" **[DECOMP]**. For one format the window ticks only that
  format's box (`WAVBox` +0x7b8, `MP3Box` +0x7c0, `OGGBox` +0x7c8, `FlacBox` +0x8b8, `MIDBox` +0x7d0, `M4ABox`
  +0x990 — offsets from the class's published field table **[DATA]**); for kind 10 it ticks the boxes of the global
  mask `LastExportedFormats` **[DECOMP]**.
- The mask: bit 3 WAV, 4 MP3, 5 OGG, 6 FLAC, 7 MIDI, 9 M4A, written when the window closes with Start
  (`0xd1f7af`: `box.checked << bit` OR-ed, stored at `0x16b8e5c`) **[ASM]**; read/written as the settings value
  `General\LastExportedFormats`, default 8 (WAV) **[DECOMP]**; a value < 8 or > 0x3FF becomes 8 **[DECOMP]**.
  `FUN_011dd800` returns the first ticked format 3…9 (else 3) **[DECOMP]**.
- FL renders once and writes every ticked format **[DATA: the boxes are independent check boxes]**.

**LYNX now:** File › **Export** ▸ *Wave file…*, *MP3 file…*, *OGG file…*, *FLAC file…*, *MIDI file…*,
*Last exported format(s)…* (Ctrl+R), then *Project with samples (.zip)…* and *Project data files…*. A format
entry opens the Export window with that format alone (`exportopts::withSingleFormat`); "Last exported" with the
remembered ones. The window has an **Also save** row (WAV / MP3 / OGG / FLAC boxes): the extra formats are written
from the same render (`Options::moreFormats`, `formatsOf`, `ExportJob::writeBuffered` → one file per format, same
name, own extension, each with its own dither); the disk-space line counts every file. FL's mask is mapped by
`flLastExportedMask` / `withFlLastExported` (MIDI / M4A only → WAV). *MIDI file…* writes the arrangement's notes
(`exportopts::midiExportNotes`: each pattern clip's part of its pattern, cut at the clip's end, muted clips / tracks
left out; pattern mode = the current pattern) with `MidiFileIo::saveSong`. Tests `rea139_last_exported_formats_like_fl`,
`rea139_midi_export_notes_follow_the_playlist`.

### B2. "Disable maximum polyphony" and the channels' Max polyphony **[DECOMP FLEngine `0xd1a568…` (render start), `FUN_00fed7c0`; DATA DFM `NoPolyBox`]**

- The render window's `NoPolyBox` ("Disable maximum polyphony", +0x818) is copied to the renderer (+0x9ca); when on,
  before the render every channel's polyphony value (+0x1ec) is saved to +0x368 and `FUN_00fed7c0 (channel, 0)` sets it
  to 0 — no limit — and tells the generator (interface call `+0x28 (1, value)`) **[DECOMP]**. Saved with the render
  options as "Disable max poly" (bit 2 of `+0xe2d`) **[DECOMP, round 134]**.
- The value is the channel's Polyphony › Max (flp event 0xDD i32, 0 = unlimited; round 6).

**LYNX before:** no channel polyphony limit (an imported Max of 1 became Mono). **Now:** `ChannelDef::maxPolyphony`
(imported from 0xDD, saved in `.lynx`); the renderer keeps the order of each channel's sounding notes
(`core/VoiceLimit.h`, `VoiceOrder`, fixed storage) and, past the limit, releases the oldest note before the new one
(FL: "the excess releases the oldest voices") — for every instrument, in songs and for live notes; a Mono channel
with Max 1 keeps its own mono handling. The Export window has **Disable maximum polyphony** (on, as FL; remembered):
the exported song's limits are 0 (`makeRenderSong`). Test `rea139_max_polyphony_releases_the_oldest_and_export_lifts_it`.

### B3. Record menu: Quantize section and recording markers **[DECOMP FLEngine `FUN_012ca7b0`, `FUN_012ca680`, `FUN_011ff9d0`, `FUN_011ffb80`, `FUN_0119fad0`]**

- Settings `QuantizeNoteStartTime` (bool), `QuantizeNoteEndTime` (int 0/1/2), `RecToSS` (bool), `QuantizeAutomations`
  (bool), `EnablePunchInOutMarkers` (bool) **[DECOMP `FUN_0119fad0`]**.
- The menu (`FUN_011ff9d0` / `FUN_011ffb80`): "Note start time" = QuantizeNoteStartTime (shown unticked while "Record
  to step sequencer" is on), "Note end time" = end mode 1, "Leave note duration" = end mode 2 (the two exclude each
  other; clicking the ticked one sets 0), "Record to step sequencer" = RecToSS, "Automations" = QuantizeAutomations,
  "Enable recording markers" = EnablePunchInOutMarkers **[DECOMP]**.
- When a take is committed (`FUN_012ca7b0`, per recorded note: start, length): with RecToSS the start goes to the step
  grid and the length becomes 0 (a step note); else with "Note start time" the start goes to the snap
  (`FUN_00e5a350`); end mode 2 keeps the played length from the new start, mode 1 puts the end on the snap, mode 0
  keeps the played end; the length is at least a minimum and is cut at the pattern's end **[DECOMP]**. Automation
  events are quantized only with QuantizeAutomations and a snap that is not "none"/"line" (`FUN_012ca680`) **[DECOMP]**.
- Marker types 9 "Start recording" / 10 "Stop recording" in the `.flp` (round 6 §5) are the recording markers
  **[DATA]**; FL respects them when "Enable recording markers" is on **[model: the setting's name]**.

**LYNX now:** the record button's menu has a **Quantize** section — *Note start time*, *Note end time*, *Leave note
duration*, *Record to step sequencer* — with FL's rules (`quantizeTake`, `TakeQuantize`, `NoteEnd`; the grid is the
global snap; LYNX's step note is one step long) — and **Enable recording markers**. Playlist markers have FL's
actions (`Marker::action`, imported from the `.flp` type, saved in `.lynx`); the ruler's menu adds *Add a Start
recording marker here*, *Add a Stop recording marker here* and *Marker action* ▸ (none / Start / Stop recording);
recording markers are drawn in the accent colour. With the option on (song mode) a note that starts outside the
ranges is not recorded and an audio take becomes clips only for its parts inside the ranges (`punchRanges`,
`punchParts`; nothing inside: the take files are removed). Options › Recording › *Quantize recorded notes…* has the
same items. Saved with the UI settings. Not done: "Automations" (LYNX does not record automation yet). Tests
`rea139_record_quantize_like_fl`, `rea139_recording_markers_punch_in_out`.

### B4. The Browser's details box **[EXEC FL 2026: the search results' box "Date / Version / Path"]**

FL shows, under the results, a box with the selected item's **Date**, **Version** (the FL version that saved a
project or preset) and **Path** (wrapping) **[EXEC, round 134 capture]**.
**LYNX now:** a details box above the preview strip whenever something is selected in the Browser (tree or search):
*Date*, *Size*, *Version* (for an `.flp`, read from its first 4 KB: `flp::readSavedVersion`, event 0xC7) and *Path*
(wrapping); its hint shows the whole path. Test `rea139_flp_wrapper_flags_and_saved_version`.

### B5. "Use maximum buffer size from host" **[DECOMP Fruity Wrapper `FUN_00870670`, round 134; DATA fixed-buffer popup]**

Fixed-buffer options bit 1: the plugin is told the host's maximum block instead of the fixed block **[DECOMP]**.
**LYNX before:** every hosted plugin was prepared for 4096 samples. **Now:** a plugin with "Use fixed size buffers" is
prepared for its fixed block (2 ms, or the device buffer with "Process maximum size buffers"), the block it is always
called with; the new Processing item **Use maximum buffer size from host** prepares it for 4096 as before
(`PluginRef::hostMaxBuffer`, saved, imported from the wrapper's options, re-creates the plugin when changed). The
render-mode re-prepare keeps the prepared size.

### B6. Smart disable per plugin **[EXEC FL: slot menu and generator menu "CPU › Smart disable"; DATA .flp diff; DECOMP FLEngine `FUN_01232870`, `FUN_0124d430`, `0x132f900`]**

- The plugin menu's "Smart disable" (`SmartDisableMenuClick` → `FUN_01232870`) toggles the plugin object's byte
  +0xa4 through its setter (`0x132f900`) **[DECOMP]**; Tools › Macros › "Switch smart disable for all plugins"
  (`FUN_0124d430`) sets it on for the 10 slots of every insert and every plugin channel **[DECOMP]**.
- Saved in the plugin record **0xD4**, u16 at +16 (PyFLP's `WrapperEvent.flags` **[DOC]**): unticking it on Thief's
  Master "Fruity Soft Clipper" and saving changed only that record, `0x160` → `0x140` — bit **0x20** **[EXEC + DATA]**.
  Other bits: 0x10 generator, 0x40 threaded, 0x100 hide settings **[DOC]**. Corpus (400 files): effects 6 633 × 0x160,
  4 703 × 0x140 **[DATA]**.
- The wrapper's own "Allow smart disable" (record 0x02 flag `0x20000000`, round 134) is the plugin's permission
  **[DATA]**; FL skips a slot only with both (round 2 §2.4: −96 dB, 4 s hold on input and output).

**LYNX before:** every effect slot was smart-disabled (built-ins after 1 s, plugins after 4 s). **Now:** the slot menu
has **Smart disable** (`EffectSlot::smartDisable`, on by default as before; off = always processed; imported from the
0xD4 bit, saved), the plugin Processing menu **Allow smart disable** (`PluginRef::allowSmartDisable`, from the
wrapper flag), and Tools › Macros › **Switch smart disable for all plugins**. Channels keep LYNX's lossless idle skip
(an instrument that is silent and gets no notes is not called). Test `rea139_smart_disable_per_slot_and_allow_smart_disable`.

---

## Part A — FL vs LYNX screens

**Method.** REA's native-UI capture is still macOS-only, so FL Studio 2026 (build 5654, 1920×1080) was driven with our
SendInput / CopyFromScreen helpers (`tmp/flrea139/fl.sh`, under `~/.cache/lynx-fl.lock`) on a copy of `Ookay -
Thief.flp` on D: **[EXEC]**; LYNX's screens come from `LYNX.exe --snapshot` of this round's build. Side-by-side images:
`tmp/flrea139/compare/01…10-*.png` (FL left, LYNX right; not in the repo).

| # | Screen | FL Studio 2026 | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | Menu bar | FILE EDIT **ADD** PATTERNS VIEW OPTIONS **TOOLS** HELP | File Edit View Patterns Options Help | FL's eight menus in FL's order |
| 2 | File menu | New, New from template, Open \| Save … Delete unused files and save project \| Import ▸, **Export ▸** \| **Recent projects** listed in the menu \| Revert to last backup \| Exit | Open recent ▸ submenu; Export audio / package / collect as separate items | FL's grouping: Save items, then **Export ▸** (per format, Last exported, project package, project data files), the **Recent projects** section in the menu (10, the rest under "More recent projects", the folder on the right), Revert / autosave folder, Exit |
| 3 | Add menu | Channel (every generator by category), Other: Effect ▸, Plugin picker, plugin database, Manage plugins, Automation for last tweaked, Pattern | none (the Channel Rack's "+" only) | **Add**: Channel (LYNX's instruments, controllers, sampler, installed plugins — the rack's list), Other: Pattern, Plugin manager |
| 4 | Tools menu | Browser find, One-click audio recording, … Macros: Stop sound, Select unused channels, **Switch smart disable for all plugins**, Delete unused files and save project, … | none (scattered in Options / Edit) | **Tools**: Browser find, One-click audio recording (arm the selected insert), Plugin performance monitor; Macros: Stop sound, Select unused channels, Switch smart disable for all plugins, Delete unused recordings and save project, Collect project samples |
| 5 | Browser | details box under the results: Date, Version, Path (wrapping) | the path only in the row's hint | details box above the preview: Date, Size, Version (.flp), Path (B4) |
| 6 | Render window | WAV / MP3 / OGG / FLAC / MID / M4A boxes ticked together, Disable maximum polyphony | one format at a time | **Also save** boxes, **Disable maximum polyphony** (B1, B2) |
| 7 | Mixer slot menu | … CPU: **Allow threaded processing**, **Smart disable** … | Processing ▸ submenu only | a **CPU** section: Allow threaded processing (plugins), Smart disable (B6) |
| 8 | Record button menu | … Options: …, Enable recording markers \| **Quantize**: Note start time, Note end time, Leave note duration, Record to step sequencer, Automations | one "quantise" item | FL's Quantize section and recording markers (B3) |
| 9 | Generator / plugin menu | CPU › Allow threaded processing, Smart disable | Processing ▸ (fixed buffers, process, threaded, MIDI port) | + **Use maximum buffer size from host**, **Allow smart disable** (B5, B6) |
| 10 | Mixer, Channel Rack, Piano roll | (captures 07–09) | layout and information already match (rounds 90/104/115: slots panel with EQ and in/out, rack rows with LED, pan, volume, insert number, steps) | unchanged; recorded differences: FL's slot panel also shows the Master's input selector, FL draws insert names vertically and colours whole strips |


---

## Where I stopped

- Done: Part B items B1–B6 and Part A rows 1–9 (code + tests `tests/Rea139Tests.inc`, pt-BR strings).
- Not done: the record menu's "Automations" quantize (LYNX does not record automation); FL's "Import ▸" submenu
  (LYNX's Open already reads .flp / .mid / audio); "Automation for last tweaked parameter" and the Last tweaked tools
  (LYNX keeps no last-tweaked list yet); a per-channel "Smart disable" (LYNX's channels already skip a silent idle
  instrument, losslessly); per-plugin Max polyphony passed to FL's own engines (the renderer's generic voice limit
  is used for every instrument); the Master's input selector in the slot panel.
- Not confirmed: FL's minimum note length after "Note end time" (`PTR_DAT_015e0c60`; LYNX uses one grid step);
  whether FL honours several Start/Stop recording pairs (LYNX does).
