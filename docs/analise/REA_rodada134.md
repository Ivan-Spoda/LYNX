# REA round 134 ("flrea134") — FL Studio technologies (round 118 Part B, continued) and UI fidelity

Brief (2026-10-23): continue the REA of FL Studio itself: (b) the technology list of `REA_rodada118.md` Part B
"Next rounds" — purge unused recordings, record into `<project>/Audio/Recorded` with FL-like names, per-plugin
"Allow threaded processing", FL's wrapper flags from `.flp`, "Process maximum size buffers", a browser search index,
"HQ for all plugins"; (a) FL vs LYNX screens, fixing differences of workflow and information (LYNX keeps its own dark
+ red style).

- **Targets:** `FL Studio 2026/FLEngine_x64.dll` (same build as rounds 69/72/76/118, our copy of round 118's Ghidra
  project, `~/ghproj/r134`); `Plugins/Fruity/Generators/Fruity Wrapper/Fruity Wrapper_x64.dll` (SHA-256
  `fa5a2f6f…d2e7`, the build of round 5; freshly analysed in our own project `~/ghproj/r134w`).
- **Tools:** Ghidra 12.1.4 headless with our helper scripts (`StrRefs.java`, `DecAt.java`), `objdump -d` of both
  DLLs for instruction searches, a small Python reader of Delphi published-method tables and DFM resources
  (`tmp/flrea134/py/methods.py`), a survey of the wrapper records of 400 `.flp` files (`tmp/flrea134/py/wrapflags.py`:
  FL's Data folder and the girlfriend's projects). Decompilations stay in `tmp/flrea134/gh/` (not in the repo).
- **Evidence markers:** **[DECOMP]** Ghidra pseudocode at the given address, **[ASM]** disassembly, **[DATA]**
  string/constant/resource in the binary or values in saved files, **[DOC]** published documentation, **[model]** our
  reading, "not confirmed" where unverified.
- Clean-room: only behaviour, names and formats are described here; LYNX's code is our own.

---

## Part B — technologies

### B1. Recording folder and take names **[DECOMP FLEngine `FUN_012b4e70`, `FUN_011bf050`, `FUN_00c4b0b0`]**

- The file of a recorded take is built per mixer insert by `FUN_012b4e70` (insert index): unless the insert has a
  file name set by the user, the name is `<project name>_<Now formatted "yyyy-mm-dd hh-mm-ss">_<insert name>` (the
  project name falls back to a default, "untitled"; the insert's name comes from `FUN_012be330`, "Insert %d" when it
  has none), made a legal file name, placed in `FUN_011bf050(…, 2)` and given ".wav"; then `FUN_00819ca0` (round
  118's "_2", "_3"… rule) makes it unique **[DECOMP]**. This matches the user-observed
  "untitled_2026-02-01 21-59-57_Insert 9.wav".
- `FUN_011bf050(kind)` returns the project-data-folder location for a kind: 1/2 = `…\Audio` subfolders (2 =
  recordings, the global user-data "Recorded" folder id `0x3ef` when there is no project data folder), 3 = Samples,
  4 = Backup **[DECOMP]**; the folder names are the strings `Audio\Recorded`, `Audio\Rendered`, … of round 118 **[DATA]**.
- `FUN_00c4b0b0` opens the WAV (writes the RIFF header by hand) and fails with "Unable to create folder for recorded
  wave file: " **[DECOMP]**.

**LYNX now:** a saved project records into `<project folder>/Audio/Recorded` (LYNX's "project data folder" is the
folder of the `.lynx`, or of the imported `.flp`); an unsaved one into the global `Music/LYNX/Recordings`; when the
project folder cannot be written (e.g. a demo under Program Files) the global folder is used. Takes are named
`<project>_<yyyy-mm-dd hh-mm-ss>_<insert name>.wav` (`safety::recordingName`, unique with `safety::uniqueName`).
"Open recordings folder" opens the current one. Code: `core/ProjectSafety.*`, `AudioEngine::setRecordingTarget`,
`MainComponent` timer. Test `rea134_recording_names_like_fl`.

### B2. Unused recordings **[DECOMP FLEngine `FUN_00ed1380`; DATA]**

- `FUN_00ed1380(files, mode, flags)` first drops the files that no longer exist, then asks "Confirm|" +
  mode 0: "This project has %d unused sample file(s)." / mode 1: "You created, downloaded or recorded %d sample
  file(s) that are no longer used if the project isn't saved." + "Keep these files or delete them?" (+ "|Deleting
  files may cause problems with Undo." when flag bit 0), buttons **Keep / Delete**; Delete runs "Deleting unused sample
  files..." with a progress bar and removes each file (flag bit 1: also clears the references to it) **[DECOMP]**.
- Settings: "Delete unused recorded files" (`DeleteRecordedFiles`), "Delete recorded files to recycle bin" **[DATA]**;
  Tools › Macros "Delete unused files and save project" (`MacroDeleteUnusedFilesAndSaveAction`) **[DATA]**.

**LYNX now:** the engine keeps the takes written this session. Whenever a project is left (New, Open, Open recent,
the Browser, quit, crash-recovery open — every path goes through `confirmDiscard`), the takes the project *as it will
stay on disk* does not reference (after Save: the current song; after Discard: the samples of the last saved state)
are offered for deletion: "Unused recordings — You recorded N audio files that this project will not use." with the
file names, **Keep / Delete** (Delete = to the recycle bin; each file is asked about once). File menu and the record
button's menu: **Delete unused recordings and save...** (FL's macro, for this project's takes in its
`Audio/Recorded` and this session's takes). Settings › General › Backup: "When a project closes, offer to delete the
recordings it no longer uses" (on). Code: `safety::unusedRecordings` / `samePath`, `MainComponent::
offerToDeleteUnusedRecordings`, `deleteUnusedRecordingsAndSave`. Test `rea134_unused_recordings`.

### B3. Wrapper processing options in the `.flp` **[DECOMP Fruity Wrapper `FUN_00874480` (writer), `FUN_00875aa0` (reader); ASM; DATA]**

The wrapper's Settings page is a DFM in `Fruity Wrapper_x64.dll` with check boxes bound to published handlers
(`FixedSizeBuffersBoxClick` `0x78c160`, `AllowThreadedBoxClick` `0x78ad40`, `BridgedBoxClick` `0x78b0c0`,
`RenderingModeBoxClick` `0x7902b0`, `ProcessInactiveBoxClick` `0x7901d0`, `AllowSmartDisableBoxClick` `0x78acf0`, …)
**[DATA: method table]**. Each handler calls a setter of the options object (`W+0x5064`, vtable `0x772750`: slot
`0xa8` → byte `+0x20`, `0xf8` → `+0x2a`, `0x100` → `+0x2b`, `0xb0` → u32 `+0x24`) **[ASM]**, and record `0x02` of the
`0xD5` blob packs them (u32 flags at +9, u32 at +0x0D):

| Flag (+9) | Option | Default |
|---|---|---|
| `0x2` | Use fixed size buffers | off |
| `0x4` | Notify about rendering mode | on |
| `0x8` | Process inactive inputs and outputs | on |
| `0x1000` | Allow threaded processing (read for wrapper version ≥ 7) | on |
| `0x20000000` | Allow smart disable (version ≥ 10) | on |
| `0x40000` | Make bridged (loaded in the bridge; version ≥ 8, older: bridged unless `0x400`) | off |
| `0x400000` | "use the defaults" (the reader ignores the rest) | — |

The u32 at +0x0D is the fixed-buffer options mask of the "|Set options for fixed size buffers" popup
(`mnuFixedOptionsHostBlockSizeClick` `0x78dbb0` toggles bit `1 << Tag`): bit 0 "Process maximum size buffers"
(hint "More latency (only use when necessary)"), bit 1 "Use maximum buffer size from host", bit 2 (inverted)
"Align block size" (hidden item) **[DECOMP + DATA DFM]**. The corpus confirms the defaults: 9 729 wrapper instances,
flags `0xA4007D8C`/`D` (and `0xB4…` with bit 28, `0x24007DA0`) — fixed buffers and bridging never set, threaded
always on **[DATA]**.

**LYNX now:** `parseFlWrapper` keeps record `0x02`; `applyFlWrapperOptions` turns it into the plugin's
`fixedBuffers`, `maxSizeBuffers`, `threaded` and `separateProcess` when an `.flp` is imported (channels and mixer
slots). Test `rea134_fl_wrapper_flags_from_flp`.

### B4. Fixed block sizes, "Process maximum size buffers" **[DECOMP Fruity Wrapper `FUN_00870670`]**

With fixed buffers on, FL's block is `round(sampleRate × 2 / 1000)` — **2 ms** — rounded up to a multiple of 4
samples unless "Align" is off; with "Process maximum size buffers" it is the host's maximum block (`W+0x7c8`);
"Use maximum buffer size from host" only changes the size reported to the plugin **[DECOMP]**.

**LYNX before:** fixed buffers always used the device buffer — that is FL's "Process maximum size buffers".
**Now:** "Use fixed size buffers" = 2 ms blocks (88 samples at 44.1 kHz, 96 at 48 kHz; `flFixedBlockSize`), and a
new item under it, **Process maximum size buffers (more latency, only when necessary)** = the device buffer. Both in
the plugin's Processing menu, saved (`maxSizeBuffers`), imported from FL.

### B5. Allow threaded processing **[DATA DFM: `WantTPMenu` / `FXWantTPMenu` "Allow threaded processing", hint "Use this to spare CPU"; wrapper `AllowThreadedBoxClick`; mixer track menu "CPU" section (round 115)]**

FL has the option per plugin (generator and effect wrapper menus, wrapper settings) and per mixer track **[DATA]**.
**LYNX now:** the plugin Processing menu and the mixer track menu have **Allow threaded processing** (on). Off: that
channel / insert is always processed on the audio thread itself, never handed to a worker (RackState keeps
`threadedChannels` / `audioThreadChannels` and per-layer `threadedLayers` / `audioThreadLayers`); nothing is
re-created when it changes. Saved as `threaded: false`. Tests `rea134_allow_threaded_processing_off_pins_to_audio_thread`
(the output is identical to the sequential render), `rea134_mixer_track_allow_threaded_processing`.

### B6. "HQ for all plugins" **[DECOMP FLEngine `FUN_00eeac20` / `FUN_00eec630`; DOC]**

The render window keeps "Dither", "HQ for all plugins", "Disable max poly" as bits 0/1/2 of its options byte
(`+0xe2d`), saved under those names **[DECOMP]**; the consumer of bit 1 inside the render was not traced (not
confirmed). Image-Line's knowledge base: plugins may have different draft and render qualities, which "can also be
affected by the 'HQ for all plugins' setting" **[DOC kb 582]**; users switch it off so the export matches playback
**[DOC forum]**.
**LYNX now:** Export window option **HQ for all plugins** (on, remembered). On: as before, engines with an FL
render-quality setting use it (`offlineRendering()`), and HQ switches are forced on (`hqForAllPlugins()`: the
WaveShaper's HQ). Off: the export keeps the playback quality. Hosted plugins are still told that an export is running
(FL's separate per-plugin "Notify about rendering mode"). Test `rea134_hq_for_all_plugins_export_option`.

### B7. Browser search index **[DATA FLEngine `BrowserCache.TDirtyFolder`, "Indexing", `EnableIndexingMenu`, `EnableAutoIndexingMenu`]**

**LYNX before:** every search walked the folders on disk. **Now:** `core/SearchIndex.*` keeps the listed files of
every folder under the Browser's roots (in memory, and in `browser-index.txt` in the settings folder between
sessions). A search first refreshes the index — only each folder's modification time is read; the folders whose time
changed ("dirty") are listed again, removed ones dropped with their subfolders — then reads the index. The Browser
refreshes the whole index in the background 4 s after start. Test `rea134_browser_search_index_dirty_folders`.

---

## Part A — FL vs LYNX screens

**Method.** The REA native-UI tools (`observe_native_ui`, `capture_native_ui_scenario`) are still bound to macOS
Mach-O targets (their schema: "bound to the active Mach-O target"), so — as in round 115 — FL Studio 2026 (FL64.exe
build 5654, 1920×1080) was driven with our CopyFromScreen/SendInput helper (`tmp/flrea134/fl.sh`) under
`~/.cache/lynx-fl.lock`, on a copy of the demo `Ookay - Thief.flp` on D: **[EXEC]**. (The girlfriend's `game over`
copy opened Auto-Tune Pro 11's "All visible licenses expired" activation window: left alone, FL closed — ASK_USER.)
Captures stay in `tmp/flrea134/flref` (not in the repo); round 115's captures were reused for the mixer menu.

| # | Screen | FL Studio 2026 | LYNX before | LYNX now |
|---|---|---|---|---|
| 1 | File menu | New, New from template, Open | Save, Save as, Save new version, (cloud), **Save to new project folder...**, Save as template, **Delete unused files and save project** \| Import, Export \| recent projects \| Revert to last backup \| Exit | no "new project folder", no unused-files macro | **Save to new project folder...** (`<name>/<name>.lynx`; this session's takes from the global folder are copied into `<name>/Audio/Recorded` and used from there) and **Delete unused recordings and save...** in FL's order after Save as template |
| 2 | File › Export | Wave/MP3/OGG/FLAC/MIDI/M4A file, Last exported format(s), cloud, All playlist tracks / track groups, Video, Zipped loop package, Project bones | Export audio (one window for all formats), Export project with samples (.zip), Collect | unchanged (LYNX's single Export window covers the formats; recorded as a difference) |
| 3 | Render window | Project type: Mode, Tail; info line "Length: 144 bars  Total time: 3'57''  Disk space: 38.76 MB"; format; Quality: Resampling, **HQ for all plugins** (on), Disable maximum polyphony (on), Dithering; Misc: markers, Split mixer tracks (Include Master/Current), Trim PDC silence, Enable insert / master effects; **Show file(s) when complete**; Render minimized / Start | info "Total time"; no HQ, no show-when-complete | info line **"Length: N bars · Total time · Disk space"** (`lengthInBars`, `estimatedBytes`: 38.76 MiB for Thief's 144 bars at 16-bit, as FL), **HQ for all plugins** (B6), **Show the file(s) when complete** (opens the file manager on the result / the stems folder) |
| 4 | Record button menu | Recording filter: Automation, Notes, Audio, Clips \| Options: **Recording starts playback**, **Disarm on stop**, Enable recording markers \| Quantize: Note start/end time, Leave note duration, Record to step sequencer, Automations; hint "Record (automation, score, audio, clips)" | filter Notes / Audio, ask, count-in, overdub/replace, quantise | an **Options** section with **Recording starts playback** and **Disarm the inserts when recording stops** (saved with the UI settings); "Delete unused recordings and save..." next to "Open recordings folder" |
| 5 | Mixer track menu | last section **CPU › Allow threaded processing** (✓) | none | **CPU › Allow threaded processing** at the end of the menu (B5) |
| 6 | Plugin wrapper | Processing/Troubleshooting check boxes; fixed buffers' popup "Process maximum size buffers" / "Use maximum buffer size from host" | Processing submenu: fixed buffers, separate process | + **Process maximum size buffers** (B4), **Allow threaded processing** (B5); FL projects bring their wrapper settings (B3) |
| 7 | Browser search | results at once (indexed), the hint bar says **"Found 51 files"**, a details box (Date, Version, Path), the box's folder button "Search in selected folder" | results after a disk walk; nothing in the hint bar | index (B7) + hint **"Found N files"** (or "Found more than 500 files"); LYNX already searches the selected folder, the path is in each row's hint (round 115) |

Not changed (recorded): FL's per-format export entries and "Last exported format(s)", "Disable maximum polyphony"
(LYNX has no global polyphony cap during export), "Enable insert effects", recording markers, the Quantize section of
the record menu (r131 works on recording), the browser's details box.

---

## Where I stopped

- Done (landing 1): Part B items B1–B7 and Part A rows 1, 3, 4, 5, 6, 7 (code + tests `tests/Rea134Tests.inc`).
- Build note: `scripts/build-windows-zig.sh` recompiles every app source each run, but Zig's own cache can hand back
  an object compiled before a header changed when the header is edited while a build runs; a stale `Song.h`
  (EffectSlot copy constructor from the old layout) crashed `.flp` imports. Clearing `ZIG_GLOBAL_CACHE_DIR` fixed it.
- Next candidates: FL's per-format Export entries and "Last exported format(s)"; "Disable maximum polyphony" for
  exports; the record menu's Quantize section and recording markers; the Browser's details box (date, path) for the
  selected result; FL's "Use maximum buffer size from host" (report the device buffer to a fixed-buffer plugin);
  "Allow smart disable" per plugin (bit `0x20000000`) with smart disable for hosted plugins; "Delete recorded files to
  recycle bin" as a choice (LYNX always uses the recycle bin); the consumer of the render window's HQ bit in FLEngine.
- Not confirmed: what exactly FL forces with "HQ for all plugins" inside its own plugins; `0x400` / `0x10000000` and
  the other record-`0x02` flags.
