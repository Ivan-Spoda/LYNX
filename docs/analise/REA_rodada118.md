# REA round 118 — FL Studio's saving / project-safety technologies, and a survey of its engine technologies

User request (2026-10-20): "FL has lots of technologies in saving. Use REA to understand how FL does it — and the
technologies in general." Part A studies FL's saving and project safety and implements the missing pieces in LYNX;
Part B surveys FL's engine/app technologies against LYNX and ranks what to do next.

- **Target:** `FL Studio 2026/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`, same build as rounds 69/72/76).
- **Tools:** Ghidra 12.1.4 headless on our own copy of round 76's analysed project (`~/ghproj/r118`, program
  `fle.dll`), our helper scripts `StrRefs.java` (strings + referencing functions), `DecAt.java`, `Xr.java`
  (round 67/76 scripts); `strings` (ASCII + UTF-16) dump of the DLL (`tmp/tech118/fle-strings.txt`); a tiny PE reader
  for constants; Delphi published-method tables (name → code address) to find the File-menu action handlers.
  Decompilations: `tmp/tech118/gh/d1.c … d6.c` (not in the repo).
- **Evidence markers:** **[DECOMP]** Ghidra pseudocode at the given address, **[DATA]** string/constant in the DLL,
  **[DOC]** published documentation, **[model]** our reading, "not confirmed" where unverified.
- Clean-room: only behaviour, names and formats are described here; LYNX's code is our own.

---

## Part A — Saving and project safety

### A1. Autosave **[DECOMP `FUN_011ff450`, `FUN_011fec90`, `FUN_012131b0`; DATA `0x015c86d8`]**

- Six modes (Settings › File › Backup, combo `AutoSaveBox`) **[DATA]**: "Never (just remind every 10 minutes)",
  "Rarely (every 15 minutes)", "Occasionally (every 10 minutes)", "Regularly (every 5 minutes)", "Frequently (every 5
  minutes & before risky operations)", "Very frequently (every minute & before risky operations)".
- Timer table `0x015c86d8` (int32 ms per mode) **[DATA]**: 600000, 900000, 600000, 300000, 300000, 60000 — mode 0's
  10-minute timer only shows the hint "Remember: save often!" (`FUN_012131b0`) **[DECOMP]**.
- `FUN_011ff450(reason)` **[DECOMP]**: does nothing when the mode is 0; `reason == 1` ("risky operation") only saves in
  modes 4–5. In modes < 4 a busy state (a global flag, probably playback/recording — **not confirmed**) postpones the
  save: a pending flag is set and the save runs when that state ends (`FUN_011ca490` checks the flag) **[DECOMP]**.
  Callers include channel deletion (`FUN_01004a60`, reason 2), arrangement creation (reason 1) and ~20 other edit
  commands **[DECOMP xrefs]**.
- File name: `FUN_011ff080(path, "autosaved")` formats `"%s (%s on %s at %s)%s"` = stem, "autosaved", the short
  date with "/" replaced by "-", the time as `h"h"nn` ("14h19"), extension → **"Song (autosaved on 2026-10-20 at
  14h19).flp"**; the name is then made unique by `FUN_00819ca0` (adds "_2", "_3"…) **[DECOMP + DATA `0x011ff28c`
  "/", `0x011ff29c` "-"]**. Untitled projects use "Untitled" **[DECOMP]**.
- Folder: the user data folder's `Projects\Backup` (`FUN_009db5b0` builds `Projects\Backup`, also registered as a
  Browser folder `System\Config\Browser\Infos\Backup`) **[DECOMP + DATA]**.
- Rotation: `FUN_011feea0` keeps a newest-first list of backups (`FUN_011fed30` rebuilds it from the folder) and
  deletes the oldest while the count exceeds "Maximum amount of backups that are auto-saved" (`0x015c2700`, default
  **100**; 1000 = unlimited), clearing the read-only attribute first if a delete fails; then stores the path as
  `General\LastSavedBackup` in the registry **[DECOMP + DATA]**.
- Failure: hint "Failed autosaving" and a log line **[DECOMP]**.

### A2. Saving: temp file + rename, "overwritten" copies **[DECOMP `FUN_00f54570`, `FUN_011dc320`]**

- `FUN_00f54570` writes the project to `<file>` + a temp suffix, then `FUN_011dc320(target, temp, keepBackup)`:
  if the target exists and backups are enabled it is **moved** (`MoveFileW`) into the backup folder as
  **"Song (overwritten on 2026-10-20 at 14h19).flp"** (same name builder, kind "overwritten") and the rotation runs;
  otherwise it is deleted; finally the temp file is renamed onto the target (`MoveFileW`). A failed rename returns
  error 6 and failed writes delete the temp file **[DECOMP]**. So FL's saves are atomic-by-rename and every save keeps
  the previous version in the Backup folder.
- `FUN_011db9e0` warns when saving *into* the backup folder: "This folder is designed for temporary files only, and is
  periodically cleaned up. Your file will be lost!" **[DECOMP + DATA]**.

### A3. Save new version **[DECOMP `FUN_00f1f940(…, 1)` via `FileSaveNewVersionActionExecute` `0xf1f840`, `FUN_00819ca0`]**

- Untitled → behaves like Save as. Otherwise `FUN_00819ca0` takes the file name without extension, strips a trailing
  run of digits **only if preceded by "_"**, uses that number (or 1) as the start and increments until
  `stem_N.flp` does not exist: "Song" → "Song_2", "Song_2" → "Song_3" **[DECOMP + DATA `0x00819ebc` "_", digit set
  `0x00819e90`]**. FL's shortcut is Ctrl+N; Shift+Ctrl+S is "Save as… / save new version" **[DATA]**.

### A4. Revert to last backup **[DECOMP `FileRevertToLastBackupActionExecute` `0xf1f1c0` → `FUN_0121d2c0`]**

Opens `LastSavedBackup` (A1) **[DECOMP]**; when the loaded file is in `\Backup\` its name is compared with the current
project so the project keeps its identity (details not fully decoded).

### A5. Project data folder, collect, zipped packages **[DECOMP `FUN_011eca70`, `FUN_011ec0f0`, `FUN_00ed0370`; DATA]**

- Per-project **data folder** (Project › Info / Save as with a folder): recordings, renders and sliced audio go to
  its `Audio\Recorded`, `Audio\Rendered`, `Audio\Sliced audio`, `Audio\Shapes` **[DATA]**; "Invalid project data
  folder … the project file may have been moved" dialog offers browse/reset **[DATA `FUN_00f45b70`]**.
- **Export › Project data files** ("Collect project data files", `FUN_011eca70`): "Copy the %d files this project is
  currently using to…" a folder; `FUN_011ec0f0` copies each file with `CopyFileW(…, failIfExists)` to
  `<folder>\<file name>` made unique with `FUN_00819ca0` **[DECOMP]**.
- **Zipped loop package** (`.zip` via Save as): samples are stored by file name only, so FL warns "This project has
  multiple samples with the same file name. This may result in lost sample data in the zipped project." (Save anyway /
  Cancel, `FUN_00ed0370`) **[DECOMP]**. Opening a zip looks for the .flp inside ("Could not find any FLP project file
  in this zipped archive") **[DATA]**; "Loading a zipped loop package and saving as an .flp project may lead to lost
  samples unless a project folder is used" **[DATA]**. The zip engine is ZipMaster/`DelZip64.dll` **[DATA]**.
- Sample search: "Missing data folders|Some of the samples and files that you located are not in any of the FL Studio
  search folders. Do you want to add those locations to the list now?" (`FUN_00f46490`) **[DATA]**; plugin search
  paths in `HKCU\Software\Image-Line\Shared\Plugin search paths` **[DATA]**; `%FLStudioUserData%`-style variables for
  paths (`%FLPath%`, `%FLPluginDBPath%`, `%FLStudioDownloadsData%`) **[DATA]**.

### A6. Undo **[DATA]**

`Undo2.TUndoEvent_*` classes per edit kind (arrangement, automation clip, audio recording batch…), "Maximum undo
levels", "Alternate undo mode", "Enable undo for large samples", "Delete audio files to the recycle bin after undo of
a recording or render", "Show undo history in the browser when undoing changes". Whether the history survives a save
is **not confirmed** (no undo data is written to the .flp in the event lists studied in earlier rounds).

### A7. What LYNX had and what this round added

| FL behaviour | LYNX before | LYNX now (round 118) |
|---|---|---|
| Atomic save (temp + rename) | yes (`juce::TemporaryFile`) | unchanged |
| Autosave timer + rotation | every N min (0–30), keep N (1–100), names "Song (2026-10-16 19.50.03)", rotation by name | FL-style names **"Song (autosaved on 2026-10-20 at 14h19)"** (+ "_2"), rotation **by file time** across old and new names, postponed while recording, a hint when it fails |
| "overwritten" copy on save | no | the file a save replaces is copied to the backup folder as **"Song (overwritten on … at …)"** (rotation shared) |
| Warning when saving into the backup folder | no | Save as into it asks first; Ctrl+S on a project opened from it goes to Save as |
| Save new version | no | **File › Save new version** (Ctrl+Alt+S; FL's Ctrl+N is LYNX's New) — FL's "_N" rule |
| Revert to last backup | no | **File › Revert to last backup…** (newest backup of this project, confirm, the project file changes only on save) |
| Crash recovery | emergency save "recovered <stamp>.lynx" + backtrace | plus a **session marker**: at the next start "LYNX did not close properly — open the newest backup it saved?" (only backups written during that session; a second LYNX window does not trigger it) |
| Collect project data files | no | **File › Collect project samples into a folder…** (unique names, never overwrites, the project then uses the copies) |
| Zipped package | no | **File › Export project with samples (.zip)…**: `<name>.lynx` + `samples/…` (duplicate names made unique — no FL warning needed); opening a .zip extracts it to `Documents/LYNX/Projects/Unpacked/<name>` and opens the project |
| Moved projects | absolute paths + Missing files dialog | relative sample paths resolve against the project folder; missing samples are looked for next to the project (`.`, `samples`, `Samples`, `Audio`, `Audio/Recorded`) before the dialog |
| Plugin "notify about rendering mode" | no | hosted plugins get `setNonRealtime(true)` (VST3 kOffline / VST2 offline level) during exports |

Code: `src/core/ProjectSafety.{h,cpp}` (JUCE-free logic + tests `tests/SafetyTests118.inc`),
`src/engine/ProjectPackage.{h,cpp}`, `src/ui/MainComponent.cpp` (autosave/backup/menu/session), `ProjectFile::load`
(sample resolution), `PluginHost`/`AudioEngine::renderOffline` (render mode). All paths come from
`juce::File` special locations (portable for round 117's macOS/Linux port).

---

## Part B — FL engine/app technologies vs LYNX

Priority: **H** high value & low risk (next rounds), **M** worthwhile, **L** low / niche.

| FL technology | What FL does (evidence) | LYNX status | Prio |
|---|---|---|---|
| Notify plugins about rendering | wrapper option "Notify about rendering mode", default on: plugin learns it is rendering to file (HQ mode) **[DATA; DOC pyflp]** | **done this round** (setNonRealtime during exports) | — |
| Fixed-size buffers per plugin | wrapper "Use fixed size buffers" (+ "Process maximum size buffers"): constant block sizes for plugins that misbehave with variable ones, +latency **[DATA; DOC pyflp/Splice]** | LYNX splits blocks at every event, so plugins get **variable** sizes; no per-plugin option. A likely cause of some third-party plugin glitches/crashes (see round 111) | **H** |
| Bridged / separate process | "Make bridged", "(bridged)", `IPC.Bridge`, "Bridge process destruction timed out" **[DATA]** | global "Run plugins in a separate process" + always-sandboxed scanning; no **per-plugin** toggle | **H** (per-plugin toggle in the plugin's menu) |
| Smart disable | "Smart disable" per plugin / global **[DATA]** | built-in effects smart-disabled on silence; hosted plugins not | M |
| Multithreaded processing | "Multithreaded generator processing", "Multithreaded mixer processing", "Allow threaded processing" (per plugin), "Limit separation to fewer CPU cores", "Leaves some CPU for other applications" **[DATA]** | worker pool for channels + mixer layers (round 116 studies performance) | M (per-plugin "allow threaded" opt-out) |
| PDC | "Plugin delay compensation", "Latency compensation", mixer PDC menu **[DATA]** | per-edge PDC, export trim | — |
| Safe overloads / underruns | "Safe overloads", "%d underruns", "Mix in buffer switch", "Triple buffer" (ASIO page) **[DATA]** | RtStats over-budget counter, buffer advice | M (underrun counter in the CPU panel) |
| Priority | "CPU priority given to the mixer" (Priority box) **[DATA]** | fixed audio-thread priority | L |
| Render options | "HQ for all plugins", dithering, "Leave the rendered tail (rendering songs)", resampling quality **[DATA]** | dither, tail modes, 32-pt sinc on export; per-plugin HQ only for FL engines | M ("HQ for all plugins" = oversampling switch for LYNX engines) |
| Stretching engines | élastique v2/v3/Pro/efficient/Soloist modes, "Stretch Pro", resample **[DATA]** (licensed zplane library) | resample + WSOLA | M (better stretch; no licensed code) |
| CPU / RAM meters | "CPU meter speed", "Memory usage", "(memory used: " **[DATA]** | toolbar meter: CPU, RAM, polyphony, heaviest units | — |
| Performance mode | "Performance mode" (live performance mode for Playlist) **[DATA]** | none | L |
| Plugin database | plugin database folders + "Scanning plugin database…", "Scan - internal", `.fst` presets per plugin **[DATA]** | reads FL's database, own scanned list, blacklist | — |
| Browser indexing / cache | `BrowserCache.TDirtyFolder`, "Cache, folder refresh", "Indexing" **[DATA]** | spectral waveform cache; browser lists folders live | M (index for search) |
| Purge unused | "Delete unused files and save project", "Delete unused recorded files", "Purge unused audio clips", "You created, downloaded or recorded %d sample file(s) that are no longer used…" **[DATA]** | none | **H** (cheap: unused recordings on close/save) |
| Recording file names / folder | project data folder `Audio\Recorded`, names "untitled_2026-02-01 21-59-57_Insert 9.wav" (user-observed) **[DATA + user]** | `Music/LYNX/Recordings/Take <stamp> - Insert N.wav` | M (record into the project's folder when it has one) |
| Undo of recordings to recycle bin | "Put undone recordings in the recycle bin" **[DATA]** | undo keeps the files | L |
| Project backups to cloud | "FL Cloud" backups, "Download project backup" **[DATA]** | none (out of scope) | L |
| Zip themes/presets | "Zipped theme file (*.zip)", `.fst/.fsc` presets **[DATA]** | LYNX presets JSON | L |

## Where I stopped / next candidates

- Done (landing 1): Part A table A7 (code + tests), "notify about rendering mode".
- Done (landing 2): per-plugin **Processing** submenu (channel menu + mixer slot menu): **Use fixed size buffers**
  (`src/core/FixedBlock.h`: the plugin always gets blocks of the device buffer size; input collected, output delayed by
  exactly one block, MIDI kept at its offset in the block; the block is added to the plugin's `latencySamples()` so
  PDC compensates it) and **Run in a separate process** (FL's "Make bridged", per plugin; greyed when the global option
  is on). Both are saved in the project (`fixedBuffers`, `separateProcess`) and re-create the plugin with its current
  state when toggled. **Underrun counter** in the CPU meter (driver's xrun count when it reports one, else LYNX's
  estimate), red number + hint. Tests `tests/FixedBlockTests118.inc`.
- Next rounds, in order: (1) **purge unused recordings** (FL's "Delete unused recorded files" prompt); (2) record into
  a project data folder (`<project>/Audio/Recorded`) with FL-like names; (3) per-plugin "allow threaded processing"
  opt-out; (4) read FL's wrapper flags ("Use fixed size buffers", "Make bridged") from imported .flp files into
  `PluginRef`; (5) "Process maximum size buffers" variant.
- Not confirmed: the exact busy flag that postpones autosave in FL's modes 0–3; whether FL's undo history survives a
  save.
