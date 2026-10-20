# REA round 112 — opening the vocal projects: where the time goes, and a progress bar that moves

User report (on v0.1.7, girlfriend's laptop): opening her vocal projects "sits at 0 % for a very long time before
anything happens". The Windows event log on the laptop has `AppHangB1` events for LYNX 0.1.6 (17/10 13:56, 15:50,
15:54) and 0.1.7 (18/10 18:34): Windows reported LYNX as not responding (the message thread stopped pumping) and
the user closed it.

Scope: loading only (r111 handles crashes, r113 the substitute sound). Measurements on her laptop were taken
headless (`LYNX.exe --export`, hidden window, Idle priority, one run at a time) with her real installed plugins
hosted by LYNX; no third-party binary was analysed. Nothing from the laptop goes into git.

## 1. How an open worked in 0.1.7 [code]

`MainComponent::openFile (.flp)`:
1. parse the project on the message thread to list its third-party plugins;
2. if any is not known: `ProjectPluginScan` (a `ThreadWithProgressWindow` "Finding this project's plugins...")
   lists **all** plugin folders recursively (`filesToScanFor`, progress 0 the whole time) and describes candidate
   files in child processes (progress only moves between files);
3. `importFlp` **on the message thread**: parse again, `importFlp` (sample probing: several candidate paths per
   sample), `adoptFlPlugins` (finds each hosted plugin; for automated VST3 plugins it *creates an extra instance*
   just to read the parameter IDs), then `setSong → publishSong → RackState::build`: decodes every sample, time-
   stretches the tempo-following audio clips (WSOLA), creates every hosted plugin in-process, one after another.

Nothing repainted during step 3, so whatever the scan window showed last (0 %) stayed on screen; a project with a
dozen hosted plugins blocked the thread long enough for Windows to call LYNX "not responding".

The plugin list on her laptop also explains why step 2 ran **on every open**: the Waves shell
(`WaveShell1-VST3 16.0_x64.vst3`) is blacklisted in `plugins.xml`, and her projects use Waves plugins
(saved with WaveShell 15.5, REA_rodada107 §5) — so they are "unknown" every time, and the whole plugin-folder walk
(18 folders, several nested inside `Common Files\VST3`, so walked twice) ran again each open with nothing to find.
A file a scan described with zero plugins was also rescanned every time (JUCE's `isListingUpToDate` is false for a
file with no types).

## 2. Measurements [EXEC]

New phase timing (`core/LoadTrace.h`): every stage adds its time; `--export` writes `load_<phase>_ms/_count`
lines into its report, and every open writes the table (slowest first, including **one line per hosted plugin**)
to `%APPDATA%\LYNX\load.log`.

Her laptop (Core 5 210H, 16 GB; projects and samples on the external drive K:), `--export`, LYNX 0.1.7 vs this
round's build. `open_ms` = import + rack; the export then builds a second engine (`prepare_ms`), which creates
every hosted plugin again — so `plugin_instantiate` below covers both engines. The new build ran at Idle priority
while she was working in FL Studio, so its absolute times are pessimistic; the split is what matters.

| project | hosted plugins | 0.1.7 open_ms (normal prio) | new: plugin_instantiate | adopt_plugins (VST3 remap) | rack_stretch | sample decode (sum / wall, parallel) |
|---|---|---|---|---|---|---|
| TWIN SWAG | 10 | 4380 | 7366 ms ×20 | 6388 ms | — | — (60 samples missing) |
| game over | 11 | 7341 | 8669 ms ×22 | 129 ms | 1963 ms ×6 | 2181 / 379 ms ×45 |
| luisa sonsa | 12 | 7754 | 7858 ms ×24 | 95 ms | — | 636 / 117 ms ×27 |
| kwe | 2 | 3462 cold / 234 warm | 1 ms (Auto-Tune did not load, §2b) | 106 ms | — | 444 / 83 ms ×20 |

Everything else is negligible: parse < 1 ms, import ≤ 9 ms, file probes ≤ 2 ms, built-in effects 35–125 ms,
PDC ≤ 9 ms, schedule/graph ≤ 9 ms. K: was connected during these runs (no missing-root cost); the first, cold
read of `kwe` from K: took 3.4 s against 0.23 s warm (external drive + OS cache).

**What dominates: creating the hosted plugins** (Auto-Tune Pro, UADx Neve/1176/LA-2A, Pro-Q 4, Pro-DS,
Decapitator, EchoBoy, Fresh Air, ValhallaVintageVerb...), ~0.35–0.7 s each, all on the message thread; then the
VST3 automation remap (TWIN SWAG: 6.4 s, an extra instance per automated VST3 plugin, thrown away afterwards);
then the tempo stretch of the vocal takes (game over: ~2 s) and the sample decoding (now parallel, 4–7× shorter
wall time). In the app the same work ran with the audio device open and the UI waiting, after the scan window.

Locally (this PC, no third-party plugins, project copies on C:), 0.1.7 → new: game over open 973 → 698 ms,
prepare 772 → 543 ms; Liwn 986 → 433 ms; kwe 411 → 169 ms (parallel decode, the shared sample cache lets the
export engine reuse the open's samples).

## 3. Fix

- **Progress panel** (`ui/ProjectLoading.*`): the whole open runs behind one panel over the main window
  ("Opening <project>", a red bar that only moves forward, a percentage and what is loading now: "Reading take
  3.wav... 12/45", "Loading Auto-Tune Pro... 3/12", "Building the mixer..."). Stages: targeted scan 2–20 %
  (thread), read/import/decode 20–55 % (thread), hosted plugins 55–90 % (message thread, **one per message-loop
  turn**, the panel is painted synchronously before each one), rack 90–100 %. Input is swallowed meanwhile; a
  second open is refused; closing LYNX joins the worker first.
- **Off the message thread:** parse, import, sample probing, sample decoding (parallel, up to 6 threads) and the
  tempo stretch of audio clips (parallel) now run in `AudioEngine::prepareFlpImport`. `SampleLibrary` became
  thread-safe and its decoded-sample cache is shared by the process (a path being decoded is waited for, not
  decoded twice — its disk stream cache is written once; the export engine reuses the open's samples).
- **Plugins created once:** `PluginHost::preload` creates each hosted plugin ahead of the rack and
  `makeEffect/makeInstrument` take the ready instance (same id, state and rate); the VST3 automation remap now
  creates *the* instance the rack will use (it was a throw-away second one). A plugin that fails is tried once.
  Hosted plugins are **not** created in parallel or lazily: JUCE's VST3/VST2 hosting must create instances on the
  message thread (JUCE's own requirement); creating them elsewhere is not something plugin vendors promise
  to support — sequential with progress is the safe choice.
- **No repeated scans on open:** a project plugin that a targeted scan could not find is remembered
  (`scan-misses.txt`, 7 days, forgotten by any full scan or "forget all") and not searched for again; a file a scan
  described with zero plugins is skipped until its modification time changes (`scan-empty.txt`).
- **Missing drives probed once per root** (`loadtrace::RootProbe`): a sample on an unplugged external drive or a
  dead network share costs one root check (2 s cap, on a detached thread) per open instead of several file queries
  per sample (each can take ~20–30 s on a disconnected mapped drive).
- Wave-cache: already analysed in the background (ThreadPool) from the engine's decoded copy; unchanged.

Tests: `tests/LoadTraceTests112.inc` (progress monotonic across stages, item labels and translation, phase report
and log table, path roots for drives/UNC/relative paths, one probe per root, importer issuing no query under a
missing root while still listing the files as missing).

### 2b. Second laptop run (final build) and a silent Auto-Tune [EXEC]

Per-plugin lines (both engines of `--export`, Idle priority): Decapitator 2.5–3.9 s, EchoBoy 3.0 s, PanMan
1.3–2.9 s, Little AlterBoy 1.2–2.0 s, UADx LA-2A 2.2 s, Fresh Air 1.1–1.5 s, Pro-Q 4 1.1 s, UADx Neve 1.1 s,
Auto-Tune Pro 0.6–0.8 s. The SoundToys VST2 plugins are the slowest to create.

The same run showed **Auto-Tune Pro taking 1–3 ms: it never loaded** ("No compatible plug-in format exists for this
plug-in" → bypassed, so her vocal chain played without Auto-Tune). Her `plugins.xml` lists Auto-Tune Pro twice with
the same class ID: `VST3\Antares\Auto-Tune Pro.vst3` (exists) and `VST3\Auto-Tune Pro.vst3` (gone — moved by its
installer); `findForeign` took the first, stale one. Fix: entries whose file no longer exists sort after the live
ones (existence cached once per open). With the fix Auto-Tune Pro is found at `Antares\` and loads (625–840 ms).
The stretched copies are now shared by every engine of the process (the export engine no longer recomputes them:
`rack_stretch` 1568 → 0 ms on game over).

### 2c. Corpus: her 48 saved projects [EXEC]

With her consent all 48 saved projects (12 GB) were copied to this PC (tmp only). Each project folder was copied to
C: (fresh modification times, so LYNX's disk stream cache is cold, as on a first open) and exported headless with
this round's build; this PC has no SoundToys/UAD/Waves, but Pro-Q 4 (trial) is installed. Of 38 finished so far,
the slowest opens are: FALA PRA MIM! 10.6 s (samples: 28 s of decoding spread over threads, 10 s wall), twin swag
7.0 s (36.6 s → 6.3 s wall), luisa sonsa 6.1 s (Pro-Q 4 4.4 s), AIAIAIAUIUI 6.0 s, WIU 4.9 s, a 4.6 s, OIOIOI
4.5 s; median ≈ 1.9 s. On projects with many long takes the **first** open is dominated by decoding the takes into
the disk stream cache (`%TEMP%\LYNX-cache`, reused afterwards); on her laptop, by creating the hosted plugins.

Old vs new on the largest one (JÃO, 100 samples, 516 MB), copied to C:: 0.1.7 cold open 5.5 s + export prepare
1.4 s → new 1.9 s + 0.5 s; warm 1.5 s → 0.8 s. Over the slow WSL file share it was 272 s + 191 s (0.1.7, every sample
decoded twice and serially) → 111 s + 0.4 s.

## 4. Left / not confirmed

- The laptop could not be driven interactively (she was using it), so the UI open there is not measured; the
  next open there writes `%APPDATA%\LYNX\load.log` — read it to confirm (per-plugin lines show the slowest one).
- A full plugin scan on her laptop would also drop the stale entries from `plugins.xml` (not done: her settings).
- The Waves shell stays blacklisted on her laptop (REA_rodada107 territory); her Waves slots keep their substitutes.
- A single slow plugin (> 5 s) still blocks the message thread for its own duration (the panel shows its name).

## Where I stopped

Implementation, tests, laptop and corpus measurements done; the progress panel itself could not be watched on screen
(GUI pause on this PC, the laptop in use) — check it on the next interactive session. Landing via land.sh.
