# REA round 116 — is LYNX really lighter than FL Studio? (measurements on this PC)

User question (2026-10-20): "LYNX feels light, but is it really lighter than FL?" This round answers with
measurements of LYNX (current main = 0.1.7, `664e83d`) and FL Studio 2026 on the same PC, then fixes the cheap
bottlenecks the measurements showed. Evidence markers: [EXEC] measured here, [CODE] read in our source, [model]
our reading. Raw results, scripts and screenshots stay in `tmp/bench116` and `C:\Users\kauan\lynx-test\bench116`
(never in the repo).

## 1. Method

**PC:** AMD Ryzen 5 3600 (6 cores / 12 threads), 64 GB, Windows 11, NVIDIA GPU, two monitors. FL Studio 2026
(`C:\Program Files\Image-Line\FL Studio 2026`), LYNX 0.1.7 release exe (`LYNX.exe`, md5 `cbb353dc…`, identical to
`build-win/LYNX.exe` of main).

**Same audio path for both:** the shared **"FL Studio ASIO"** driver (FL's own default output here), 44.1 kHz, its
block size set in `HKCU\Software\Image-Line\ASIO\bufferSize` (512 / 256 / 128) before each run; LYNX opens the
same driver as a JUCE ASIO device (`--type ASIO --device "FL Studio ASIO" --buffer N`, or a temporary
`audio-device.xml`). The user's settings (LYNX `audio-device.xml`, splash option, the ASIO block size 256) are
restored after every series.

**Harness** (`bench.ps1`, PowerShell + Win32): starts the program at *AboveNormal* priority (other agents were
compiling on the same PC the whole time — load average 10–35 in WSL — and this keeps the measured program in front
of the compilers; both programs get the same treatment), then polls every 50 ms:
- **window** = the process's biggest visible top-level window is ≥ 900×500, answers `WM_NULL` within 250 ms
  (`SendMessageTimeout`, not "hung") and — for a project — its title names the project (FL: `"<name> - FL Studio"`,
  LYNX: `"LYNX - <name> …"`; both set the title when the load has finished, LYNX's load runs on the message thread
  so the window does not answer before it ends);
- **settled** = after that, the main process stays under 10 % of one core for 1.5 s;
- **idle CPU** = process-tree CPU time over the next 10 s, in % of one logical core (100 % = one core busy);
- **RAM** = *private working set* (`Win32_PerfFormattedData_PerfProc_Process.WorkingSetPrivate`) of the main
  process and of the whole process tree (FL starts WebView2 helper processes for its start page and browser);
- **playback CPU** = process CPU and **per-thread CPU** (`Process.Threads[].TotalProcessorTime` deltas) over 20 s
  while the song plays; each thread is named by its start module (`GetThreadDescription` + Win32 start address).
  FL is started with the project and gets a posted Space (`PostMessage WM_KEYDOWN`, no global keystrokes); LYNX
  plays through `--rtstress` (same window and device, plays from the start 1.5 s after loading) and also writes
  its own callback statistics.
FL's modal start dialogs on this PC ("Cloud path warning", "Welcome to FL Studio") are closed by the harness
(`WM_CLOSE`) as soon as they appear — they are counted in neither time.

Each case runs 3 times, LYNX and FL interleaved; tables give the **median**. `load1` (WSL load average) is logged
with every run. Cold start (empty file cache) could not be forced: the session is not elevated (no standby-list
purge) and filling RAM to evict the cache would have disturbed the other agents, so all times are **warm**.

**Projects:** FL demo songs copied to `bench116\proj` (Ookay – Thief, Eric Kauffmann & NrgMind – Exoplanet,
LeriQ – SHINE, Mehdi Ebrahiminejad – Never Out) and the girlfriend's copies `game over` and `kwe` (tmp/gf/proj2;
partial sample sets: game over 7 and kwe 5 samples missing in both programs). Her projects use third-party
plugins that are not installed here: **FL drops them, LYNX loads its substitutes** (LYNX Tune, Ridge Strip,
Compressor, Ember, Aether, Auto-pan, Fruity Delay 3 …), so for those two LYNX does *more* work than FL.

## 2. Results

### 2.1 Start-up (empty project) and idle  [EXEC]

| | LYNX 0.1.7 (splash on, default) | LYNX (splash off) | FL Studio 2026 |
|---|---|---|---|
| time to a usable main window | 3.8 s | 3.4 s | 2.8 s |
| idle CPU, empty project (audio device running) | 9 % of a core | 13 % | 29 % |
| RAM (private working set), main process | **48 MB** | 48 MB | 310 MB |
| RAM, whole process tree | 48 MB | 48 MB | 483 MB (+ WebView2 helpers) |
| threads | 52 | 50 | 39 |

LYNX's splash has a fixed minimum of 2.1 s on screen (`SplashScreen::minimumDuration`), but even without it the
window took 3.1–3.8 s, slower than FL. See §3.3.

### 2.2 Opening a project  [EXEC]

GUI opens (`LYNX.exe <project.flp>` / `FL64.exe <project.flp>`), branch build (0.1.7 + this round + round 112's
load work), 3 runs each, LYNX and FL interleaved, load average 1.3–7 (median per row). *Window* = the main window
titled with the project and answering; *idle* = CPU over the next 10 s, transport stopped; *RAM* = private
working set (FL: main process / whole tree with its WebView2 helpers).

| project | LYNX window | FL window | LYNX idle CPU | FL idle CPU | LYNX RAM | FL RAM (main / tree) |
|---|---|---|---|---|---|---|
| Thief | **9.4 s** | 10.2 s | **15 %** | 19 % | **391 MB** | 818 / 998 MB |
| Exoplanet | **4.4 s** | 6.2 s | **21 %** | 59 % | **265 MB** | 904 / 1078 MB |
| SHINE | **3.6 s** | 6.0 s | **25 %** | 42 % | **126 MB** | 806 / 979 MB |
| Never Out | **5.8 s** | 8.3 s | 27 % | **20 %** | **319 MB** | 930 / 1108 MB |
| game over | 11.0 s (2/3) | — | 22 % | — | 541 MB | — |
| kwe | 7.1 s | — | 20 % | — | 198 MB | — |

- LYNX opens every demo song faster than FL (0.8–2.4 s ahead) and uses **2.5–7× less RAM**. Idle CPU with the
  project loaded is lower in 3 of 4 songs (the remaining LYNX idle cost is the UI: meters, waveforms, the
  playlist; the audio side is parked since §3.1).
- The 0.1.7 → branch change for Thief is round 112's (open 16 s → 3–4 s headless, 30.7 s → 9.4 s GUI).
- **game over / kwe**: the Waves trials installed on 2026-10-20 are now loaded for real by both programs (their
  `WaveShell` VST3 + `WavesLocalServer.exe`), which costs 10–20 s per open in LYNX (headless `open_ms` of kwe
  1.2 s → 16–23 s). FL never reached a titled window within 240 s in 6 runs (a modal dialog the harness does not
  know, probably a missing plugin / Waves licensing prompt) — no FL numbers. One LYNX run of each also hung
  (kwe 237 s, game over > 240 s) — third-party plugin start-up, not measured further.

Headless opens before round 112 (0.1.7, `LYNX.exe --export`, `open_ms` / the export engine's `prepare_ms`, 3 runs,
load 12–40):

| project | LYNX open_ms | LYNX prepare_ms |
|---|---|---|
| Thief | 16.0 s | 16.2 s |
| Exoplanet | 1.8 s | 0.4 s |
| SHINE | 1.1 s | 1.1 s |
| Never Out | 4.0 s | 3.1 s |
| game over | 2.4 s | 1.6 s |
| kwe | 1.2 s | 0.3 s |

Same with the branch build (load 1–8): Thief 3.7 s / 1.2 s, SHINE 0.5 s / 0.07 s, Never Out 0.9 s / 0.4 s.

### 2.3 Offline render  [EXEC]

Whole program run, from launch to exit: `LYNX.exe --export <flp> <wav>` (headless, no window) vs
`FL64.exe /R /Ewav /O<dir> <flp>` (FL opens its window, renders, quits). 44.1 kHz WAV, 3 runs, load 1–11, median:

| project (song length) | LYNX | FL Studio | LYNX render part only |
|---|---|---|---|
| Thief (3:57) | **37 s** | 111 s | 32 s (7.4× real time) |
| SHINE (1:51) | **16 s** | 32 s | 14.6 s |
| Never Out (2:41) | **25 s** | 65 s | 23 s |

LYNX renders the same songs **2–3× faster** end to end (FL's figure includes its ~6–10 s start-up, LYNX's its
~0.5–4 s open). kwe: LYNX 8–28 s; FL took 59 minutes in one run and wrote no file in another — the Waves trial
plugins again (not comparable). Exoplanet (7:41) was left out of the FL runs (LYNX alone: 190–280 s at load 12–34).

### 2.4 Real-time playback CPU and threads  [EXEC]

Song playing through "FL Studio ASIO" at 256 samples (5.8 ms deadline), 20 s sampled after 5 s of playback, 3
runs, median. LYNX plays via `--rtstress` (also reports its own callback load); FL gets a posted Space.

| | LYNX branch (spin 250 µs) | LYNX + §3.4 (spin 60 µs) | FL Studio 2026 |
|---|---|---|---|
| Thief, process CPU (% of one core) | 106–120 % | **70 %** | 70 % |
| Exoplanet, process CPU | 114–123 % | **90 %** | 93 % |
| audio callback average / max load (LYNX) | 14–16 % / 27–38 % | 14–15 % / 22–34 % | — |
| callbacks over the deadline | 0 | 0 | — |

Per thread (Thief): **FL** — UI thread (`FL64.exe+0x30ed4`) 28 %, audio thread (`FLEngine_x64.dll`) 12 %, ~10
mixer workers 2.6–4.5 % each. **LYNX** — the ASIO callback thread 16 % (= the callback's own load figure), message
thread 5–6 %, and seven render workers at **13–14 % each** with the 250 µs spin: they are awake during the
whole callback, spinning between the parallelFor calls of a block → §3.4. With 60 µs: 6–7 % each.

So during playback LYNX now costs about what FL costs (equal on Thief, ~same on Exoplanet) while its audio work
itself (16 % of the deadline on one thread + workers) has headroom; LYNX's *idle* and *RAM* are much lower.

Notes: the rtstress reports "late device calls" ≈ 140 per 30 s (2.7 % of the callbacks start more than 1.9 blocks + 2 ms
after the previous one) for every LYNX build at every setting — the FL Studio ASIO driver (a WASAPI wrapper) delivers
its blocks unevenly, the callbacks themselves are never over the deadline. The 128-sample series is left out:
the FL Studio ASIO driver kept 256 samples for LYNX (`--buffer 128` was not accepted by the driver), so the "128"
LYNX runs were 256 again; FL's 128 runs gave the same 70 / 86 % as at 256.

### 2.5 Size on disk

| | LYNX 0.1.7 | FL Studio 2026 |
|---|---|---|
| installer download | 6.9 MB (setup) / 9.6 MB (zip) | — (not measured) |
| program files | `LYNX.exe` 20.4 MB (+ `lynx-asio.dll` 0.5 MB) | 2.15 GB (`FL64.exe` 0.6 MB + `FLEngine_x64.dll` 22 MB + 44 MB root DLLs, Plugins 910 MB, Data 823 MB, Shared 216 MB, System 135 MB) |

## 3. Bottlenecks found and fixes

### 3.1 Worker threads spinning between audio blocks (fixed)

[EXEC] With a project loaded and the transport **stopped**, LYNX used ~50 % of a core: seven threads at ~6 % each,
all started by `ucrtbase` (std::thread) — the render workers. [CODE] `WorkerPool`: after finishing a
`parallelFor` a worker spins `spinMicros` = 250 µs before sleeping on its semaphore, so the next `parallelFor` of
the same block (next mixer layer, next chunk) finds it awake. The renderer runs the channel pass and the mixer
layers through the pool on every device callback, playing or not, so after each block's *last* `parallelFor`
every worker spun 250 µs for nothing: 7 workers × 172 blocks/s (256 samples) ≈ 0.3 core, twice that at 128.

Fix: `WorkerPool::park()` — the audio callback (`AudioEngine::audioDeviceIOCallbackWithContext`) calls it when
its block is done; it stores the generation of the last `parallelFor`, and a spinning worker that sees the ticket
still at that generation goes to sleep at once (checked every 64 pauses). The next block's first `parallelFor`
wakes the sleepers through the semaphore, which it already did whenever the gap between blocks exceeded 250 µs
(always, at 256 samples and more). Offline export does not call `park()` (its blocks follow each other without a
gap, the spin still pays there). New test `worker_pool_park_sends_spinning_workers_to_sleep`.
Linux micro-benchmark (7 workers, 3 tiny `parallelFor` per 5.8 ms block): 267 % → 50 % of a core.

### 3.2 Output monitor repainting at 30 Hz in silence (fixed)

[CODE] The toolbar's output monitor (oscilloscope / long view / spectrum) called `repaint()` on every 30 Hz tick,
even when nothing played, and every repaint is a Direct2D present (the NVIDIA driver thread showed 3–4 % of a core
at idle). Now, once the output has been silent long enough for every display to have decayed to zero (history
length + 3 s), it stops repainting until sound returns.

### 3.3 Start-up time (diagnosed, not changed)

`LYNX_TRACE_STARTUP=1` (new, opt-in) writes `%APPDATA%\LYNX\startup.log` with the time of each start-up step.
[EXEC] Branch build, empty project, idle PC (3 runs, last one shown; window 3.2–3.3 s, first run 5.5 s):

| step | at | took |
|---|---|---|
| static init → options + file types | 13 ms | 13 ms |
| splash shown, before the audio engine | 318 ms | 306 ms |
| audio engine + ASIO device opened | 1085 ms | 766 ms |
| start-up sound | 1108 ms | 24 ms |
| main window built | 2004 ms | 895 ms |
| window revealed (splash minimum reached) | 3188 ms | **1185 ms** |
| first message loop turn | 3381 ms | 182 ms |

The window is ready at 2.0 s; the splash's fixed 2.1 s minimum, counted from when the splash appears, holds it
back for another 1.2 s — that is the whole gap to FL's 2.8 s (§2.1). Counting the minimum from process start (or
lowering it) would reveal the window at ~2.1 s; left as a UI decision (see next).

### 3.4 Workers spinning inside a playing block (fixed)

[EXEC] §2.4: while a song plays, each of the 7 workers used 13–14 % of a core, as long as the callback itself —
after each parallelFor (channel pass, each mixer layer) a worker spins up to `spinMicros` = 250 µs waiting for the
next one, so in practice the workers never slept during a block. [EXEC] `spinMicros` 250 → **60 µs**, same
projects, interleaved A/B runs: process CPU Thief 106 → 70 %, Exoplanet 114 → 90 % (median of 3), workers 6–8 %
each; the callback's average load (14–15 % of the deadline) and its peaks (22–34 %) did not change, 0 callbacks
over the deadline. Offline export (no park(), blocks back to back) is unchanged: Thief render 26.5 / 26.3 s,
SHINE 12.1 / 12.4 s (60 / 250 µs, 2 runs each, idle PC).

### 3.5 Not cheap, left for later

- **DSP cost of the bit-exact FL ports** (perf on `build-local/renderwav`, Thief): Reeverb 2 12 %, Maximus
  bands 11 % + 3 %, Sytrus 17 % (postOps/perSample/filters), Parametric EQ 2 6 %, Vocodex 6 %. These are the exact
  ports of FL's DSP; speeding them up without changing a bit of their output is a project of its own.
- **Thief opens in 16 s on Windows** but its core load (import 0.26 s + `RackState::build` 3.0 s) is the same
  in a Windows build of `renderwav` (tools/renderwav now prints both) — about 12 s of the app's `set_song` are
  not in the core build and not in round 112's load trace sub-phases (rack_samples, sample_decode,
  builtin_fx, instruments ≈ 5 s). Reported to round 112 (load time owner).
- Offline export builds a second engine (`prepare_ms` ≈ `open_ms` for Thief): round 112 shares the decoded
  samples between the two; the instruments/effects are still built twice.

## Where I stopped

- Done: start-up, open, render and playback series (§2.1–2.4), fixes §3.1 (worker parking), §3.2 (silent output
  monitor), §3.4 (worker spin 250 → 60 µs), start-up trace §3.3. First landing `a589fce` (parking, monitor,
  trace); second landing: spin 60 µs + results + README line.
- Raw data: `tmp/bench116/res-*.jsonl`, `open3.log`, `render3.log`, `play3.log`, `spin.log`.

### Next

- Splash: count `SplashScreen::minimumDuration` from process start → window ~1 s sooner (UI decision).
- Harness: close FL's missing-plugin / Waves dialogs so game over / kwe can be compared; PowerShell
  `Start-Process -Wait` waits for `WavesLocalServer.exe` too (use the process handle instead).
- 128-sample comparison needs a driver that takes LYNX's `--buffer 128` (FL Studio ASIO kept 256).
- Waves trials make LYNX's open 10–20 s longer (kwe 1.2 → 16–23 s headless) — profile the VST3 shell load
  (one WaveShell instance per plugin? parallel instantiation?).
- DSP cost of the bit-exact ports (§3.5).
