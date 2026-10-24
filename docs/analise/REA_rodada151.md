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

## Where I stopped

(in progress)
