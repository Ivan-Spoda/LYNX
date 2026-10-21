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

*(in progress — the open series was interrupted by the GUI pause, see "Where I stopped")*

First run (Thief): LYNX main window with the project after **30.7 s** (load average 19), idle afterwards
**51 % of a core** with the transport stopped, 387 MB; FL: project loaded after 8.5–11 s, 870–1030 MB.

Headless opens (`LYNX.exe --export`, `open_ms` / the export engine's `prepare_ms`, 3 runs):

| project | LYNX open_ms | LYNX prepare_ms |
|---|---|---|
| Thief | 16.0 s | 16.2 s |
| Exoplanet | 1.8 s | 0.4 s |
| SHINE | 1.1 s | 1.1 s |
| Never Out | 4.0 s | 3.1 s |
| game over | 2.4 s | 1.6 s |
| kwe | 1.2 s | 0.3 s |

### 2.3 Offline render  [EXEC]

*(LYNX headless done; FL's `/R` render opens FL's window, so it waits for the end of the GUI pause)*

### 2.4 Real-time playback CPU and threads  [EXEC]

First pair, Thief at 256 samples (before the fix):
- **FL**: 87 % of a core for the main process (FL's own CPU meter showed 20 %); the UI thread
  (`FL64.exe+0x30ed4`) 44 %, the audio thread (`FLEngine_x64.dll`) 12 %, then ~10 mixer worker threads of
  2–3.5 % each (14–15 threads above 1 %): FL does spread the mixer over its pool.
- **LYNX**: 51 % of a core — but the song had **not** started (a posted Space does not reach JUCE's focused
  component, hence `--rtstress` below): the worker threads were burning **6 % each (7 workers) with the transport
  stopped** (§3.1).

*(series 512/256/128 × Thief / Exoplanet / game over pending, see "Where I stopped")*

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

`LYNX_TRACE_STARTUP=1` (new, opt-in) writes `%APPDATA%\LYNX\startup.log` with the time of each start-up step
(static init → options → audio engine + device → startup sound → main window built → revealed → first message
loop turn). *(results pending — needs a GUI run)*

### 3.4 Not cheap, left for later

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

- Baseline startup series done (§2.1). Open series: 1 of 36 runs, then the coordinator's **GUI PAUSE** (user
  installing plugins) stopped every GUI run; headless LYNX exports done.
- Code on `feature/bench116`: worker parking, silent output monitor, startup trace, renderwav load timings,
  test. build-local 1 308 571 checks, 0 failures. Windows build of the branch pending (build slot queue).
- To do when `~/.cache/lynx-gui-pause` is gone: `run.sh open 3`, `run.sh render 3` (FL `/R`), `run.sh play 3`
  for 0.1.7 and the branch build; startup trace; then fill §2.2–2.4, land, README line.
