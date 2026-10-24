# REA round 151 — performance pass + FL REA round 5

Brief (2026-10-24): round 149 found every edit froze the UI for 3.5 s on FL's "Thief" demo. First a performance pass
over the girlfriend's heaviest projects and FL demo projects in the running app (open / edit / play / scroll / zoom;
edit times, UI frame time, audio CPU, load time; fix anything above ~16 ms in the UI path or any audio dropout;
compare load times with FL). Then round 149's leftovers: a plugin picker instead of the huge Plugin submenu, notes on
Playlist instrument tracks, the Patcher's Surface and minimap, echo / tracking Mod X / Y and Fine tune, the wrapper
options that can have a real behaviour.

- **Targets:** LYNX.exe (this branch, `build-win-flrea151`, run from `C:\Users\kauan\lynx-test\flrea151`); copies of
  the girlfriend's heaviest projects on `D:\lynx-work\flrea151` (gente morta — 71 plugins —, twin swag, MINA
  INSEGURA'', yunk, WIU and a 110 MB autosave of WIU) and FL demos (Thief, Never Out, Oxygen, I'm Awake); FL Studio
  2026 (build 5654) on the same copies, under `~/.cache/lynx-fl.lock`.
- **Tools:** `LYNX_TRACE_UI=1` (new, below) + `LYNX_TRACE_EDITS=1` + `%APPDATA%\LYNX\load.log`; our `ui.ps1` (SendInput
  mouse / keys, screenshots), `loadtime.ps1` (new: starts a program and logs each window it opens with the time, and
  its CPU), `test.sh` / `testpanel.sh` (scroll ×8, zoom ×6, horizontal scroll ×6, play 6 s; a panel shown / played /
  scrolled / hidden; a summary per step) in tmp/flrea151.
- **Evidence markers:** **[EXEC]** measured / seen in the running program, **[DECOMP]** Ghidra pseudocode, **[DATA]**
  form resources / files, **[model]** our reading.
- Clean-room: behaviour, names and layout logic only; LYNX's code and drawing are our own.

---

## Part A — performance pass **[EXEC]**

### A0. The trace

`LYNX_TRACE_UI=1` (src/ui/UiTrace.h) writes `%APPDATA%\LYNX\ui.log`: a watchdog thread pings the message loop every
4 ms and logs every **stall above 16 ms** with the slowest paint of that second; the big views' paints
(Playlist, Piano roll, Channel rack, Mixer strips / effects, Browser, main window) and the main timer are timed
(`LYNX_TRACE_UI_MIN=<ms>` sets the threshold, default 8), with the clip area of each paint; the Playlist also sums its
parts (frames, names, waveforms, outlines, note thumbnails, the batched small clips); once a second a summary line
with the audio side (callbacks, over-deadline blocks, late calls, max load, CPU).

### A1. Results and fixes

| Project / action | Before | Cause | After |
|---|---|---|---|
| Thief, playing with the Mixer maximised | 279 stalls in 5 s (18–24 ms each), CPU 25–37 % | the **hidden Playlist under the Mixer repainted 13–19 ms every frame** (the meters' dirty area reached it; workspace panels were not opaque) | panel frames opaque: **0 stalls, worst 4.7 ms, CPU 12 %** |
| Thief, Playlist scroll / zoom (1 384 clips visible, the chopped vocals) | **45–95 ms** a frame | every clip a handful of GPU paths (rounded frame, gradient header, waveform, outline); 82 names re-shaped every frame | clips under 12 px gathered and filled per colour (rectangles + one waveform path per colour), header without a gradient brush, the names' glyphs cached: **16–22 ms** |
| gente morta (71 plugins), Missing files › Apply | **25 s frozen, then a crash** inside a plugin (`InitDll`, 2 of 2 tries) | the re-import created all 71 plugins again while the first copies still ran | plugins the song already runs with the same settings are kept: **0.8 s, no crash** |
| gente morta, every two minutes (autosave) | **182 ms** freeze | the backup file (every plugin state) written on the message thread | written on a thread of its own (the states are still read on the message thread) |
| gente morta, Mixer scroll / repaint | 14.7 ms (show: 30 ms) | ~160 texts per paint; JUCE 8 re-shapes text whose box moved (its own cache holds 128 and keys on the position) | LYNX's text cache (src/ui/TextCache.h, position-free, 6 000 entries) in the Mixer, Playlist, Channel rack and Piano roll: **10.4 ms** |
| gente morta, Playlist scroll / zoom / play | ≤ 15.7 ms, 0–1 stall | — | — |
| gente morta, Channel rack scroll | 13.8 ms | — | — |
| every edit (mute, move a clip, undo) | 2.0–2.8 ms (round 149's fix) | — | — |
| audio while playing | 0 blocks over the deadline, 0 late calls in every test; CPU 12–30 % | — | — |

### A2. Load times, LYNX and FL Studio (launch → project window) **[EXEC, same PC, same copies]**

| Project | FL Studio 2026 | LYNX | LYNX's own load (load.log) |
|---|---|---|---|
| Thief (FL demo) | 13.6 s | 9.4 s | 5.4–6.0 s |
| Never Out (FL demo) | 17.2 s | 6.5 s | 3.5 s |
| Oxygen (FL demo) | 26.9 s | 4.8 s | 1.8 s |
| I'm Awake (FL demo) | 34.5 s | 10.3 s | 6.0 s |
| gente morta (71 plugins) | 46.2 s | 28.5 s | 25.5 s (plugins 22.6 s: SSL Channel ×7 8.0 s, Auto-Tune Pro ×7 3.7 s, Little AlterBoy 3.4 s) |
| twin swag | 38.2 s | 34.2 s | 30.0 s |
| MINA INSEGURA'' | 41.0 s | 24.6 s | 21.2 s |
| yunk | 44.7 s ¹ | 19.4 s | 16.1 s |
| WIU | 45.5 s ¹ | 21.1 s | 17.0 s |
| WIU autosave, 110 MB | 26.2 s ¹ | 14.8 s | 11.6 s |

¹ FL first asks "Invalid project data folder" (the copies moved); answered *Do nothing* by our script within ~0.5 s.
FL's times end at its "Problems loading the project" box (missing plugins / samples, shown once the load is done) or
at its project window.

FL's window appears 5.5 s after launch (LYNX's 3 s); both then load; with plugins both are bound by the plugins'
own start-up (one after another in both).

---

## Part B — the plugin picker **[EXEC FL 2026: Add › More plugins… = "Select generator plugin"; F8 = Plugin picker]**

FL's channel / slot menus list the favourite plugins and **More plugins…**: a list of every generator (or effect) with
a favourite star, Name, Format, Bits, Vendor, a *Find* box and *Manage plugins*; its Plugin picker (F8) groups them
by category tabs (generators: Controller, Drum, MIDI, Misc, Patcher, Sampler, Synth classic, Synth special, Visual;
effects: Controller, Delay reverb, Distortion, Dynamics, Filter, Flanger, Gain, Misc, Patcher, Visual).

LYNX's menus listed every scanned plugin in one submenu (hundreds of items in four columns on this PC). Now
(`addPluginItems`): the favourites, then **More plugins… (N instruments / effects)**, which opens the **Plugin picker**
(src/ui/PluginPicker.cpp): Instruments / Effects tabs, a search box (every word must match name, vendor, format or
category), the categories down the left (All, ★ Favourites, LYNX's categories with counts, then the vendors), the
list with the favourite star (click it: the Plugin manager's favourites), name, vendor, format and category;
double-click / Enter adds, *Manage plugins…*, *Scan for new plugins*. The categories (src/core/PluginCatalog.cpp,
LYNX's own names: Synth, Sampler, Drum, Piano & keys, Other / Delay & reverb, Distortion, Dynamics, EQ & filter,
Modulation, Pitch & vocal, Analyzer, Utility, Other) come from what the plugin declares (VST3 sub-categories) and,
when it declares nothing useful (VST2's bare "Effect" / "Synth"), from words in its name. Tests
`rea151_plugin_catalog_categories`, `rea151_plugin_catalog_search_and_favourites`.

## Part C — the wrapper options with a LYNX behaviour **[DATA: FL 2026, each option toggled on an FL Studio VSTi, saved, compared]**

| FL option (default) | Saved as | LYNX now |
|---|---|---|
| Settings › MIDI › Send note release velocity (on) | wrapper record 0x02 flags (+9) **0x20 set = off** | note offs carry the note's release velocity (`Note::release`, through `NoteEvent::release`); off: velocity 0 |
| Send pitch bend range (off), value 12 | flags **0x1**; record 0x01 **+8** = semitones | a pitch-bend-sensitivity RPN (101/100 = 0, 6 = range) before the plugin's first notes |
| Send MOD X as polyphonic aftertouch (off) | flags **0x20000** | each note's Mod X (0..1 → 0..127) as poly aftertouch right after its note on |
| Send All Notes Off when playback stops (on) | second flags word (+21) **0x1000 set = off** | on stop: All Notes Off + All Sound Off; off: only the notes still held get a note off |
| Troubleshooting › Reset plugin when FL Studio resets (on) | flags **0x02000000 set = off** | `AudioProcessor::reset()` before an export and on the panic button |
| Send loop position (on) | flags **0x800** | the play head reports the loop (start / end, looping) when the song has a loop region |

Imported from the .flp (wrapper version ≥ 10), saved in .lynx, shown on the wrapper page (the MIDI ones under
Settings › MIDI with the range box, the others under Troubleshooting); a change re-creates the plugin with its state.
Not done (no LYNX behaviour yet): Use time offset, Fast idle, Process inactive inputs and outputs, the GUI options.
Test `rea151_fl_wrapper_midi_and_compatibility_flags`.

## Part D — Mod X / Y through the echo delay and the tracking; Fine tune

- The notes' Mod X / Y now travel with them (`NoteEvent::modX / modY`): the **echo delay**'s Mod X / Y knobs move each
  echo k steps (FL's −1..1 knob = a step of half the note's 0..1 range, **[model]**; fat mode mirrors only pan and
  pitch), the **Vol / Key tracking**'s Mod X / Y knobs move them by the same velocity / key distance as the pan
  (**[model]**). They reach hosted plugins ("Send MOD X as polyphonic aftertouch"); LYNX's own instruments do not use
  them yet. The Misc tab shows the new knobs (tracking: Pan, Mod X, Mod Y, Mid; echo: Feed, Pan, Mod X, Mod Y, Pitch,
  Time, Echoes).
- **Fine tune** (Misc › Root note) **[DATA: FL saves +15 / +78 cents as the channel event 0x8E = 15 / 78]**: every note
  of the channel this many cents away (±100); imported, saved, a knob on the Root note row.
Tests `rea151_echo_and_tracking_move_mod_x_y`, `rea151_scheduled_notes_carry_release_and_mod_x`, `rea151_flp_fine_tune_imported`.

## Part E — drawing on an instrument track **[EXEC FL 2026]**

FL: *Track mode › Instrument track* offers *Use existing channel*, *More plugins…* and the generators (a new channel).
With the pencil on an instrument track, the first click makes a pattern named after the channel (empty, one bar,
the channel's colour; its piano roll is the channel's) and places it; every later click places the same pattern.
LYNX now does the same (`trackmodes::instrumentTrackPattern`: the pattern named after the channel whose notes are all
that channel's, else a new one; the pencil places it and selects the channel). Test
`rea151_instrument_track_draws_its_own_pattern`. Not done: making a new channel from the Track mode menu.

## Part F — the Patcher's minimap and Surface **[EXEC FL 2026: Patcher › Map / Surface]**

- **Minimap**: FL shows the whole map in a small box at the bottom right. LYNX: every node as a block (the selected one
  in the accent colour) and the visible part as a frame; click / drag in it moves the view there.
- **Surface**: FL's Surface tab takes controls (Bevel, Button, CheckBox, Digits, Label, Keyboard, Knob, Slider, XY,
  each with skins); each becomes a control output of the Map's **Surface** node. LYNX: Map / Surface tabs; on the
  Surface "+" (or right-click) adds a **Knob, Slider, Button, Check box or XY pad** ("Knob 1"…), *Edit* moves them,
  right-click renames / deletes; turning one moves what its cables reach (the graph reads the values without being
  rebuilt). The Map shows the Surface node with one output per control (an XY pad two). Saved in .lynx. Not done:
  FL's other control kinds and skins, importing FL's Surface from a .flp, automating a Surface control.
  Test `rea151_patcher_surface_controls`.

---

## Where I stopped

- Done: the performance pass (Part A: the UI trace; the hidden Playlist under a maximised Mixer, 1 000+ small clips,
  re-shaped text, the Missing files re-import that re-created every plugin and crashed, the timed backup on the
  message thread; load times beside FL's — LYNX opens every measured project sooner); the plugin picker (B); six FL
  wrapper options with LYNX behaviour, read from FL's own saved flags (C); Mod X / Y through the echo delay and the
  tracking, Fine tune from .flp 0x8E (D); drawing on an instrument track (E); the Patcher's minimap and Surface (F).
  Every screen above was used in the running app (screenshots tmp/flrea151/shots g01–g29, FL's in tmp/flrea151/flref).
- Still above 16 ms: Thief's Playlist scrolled with ~1 400 clips on screen, 17–40 ms a frame depending on the PC's load
  (other rounds were compiling; 16–22 ms when the PC was quiet): the batched small waveforms (one path per colour) are
  now most of it. Next idea: cache each clip's small waveform as an image per zoom level.
- Not done: FL's other Surface controls / skins and its .flp Surface import; a new channel from Track mode › Instrument
  track; LYNX's own instruments reading Mod X / Y; the wrapper's time offset / fast idle / process inactive I/O.
- Not confirmed: FL's exact Mod X / Y step law for the echo and tracking knobs (LYNX: [model] above).
