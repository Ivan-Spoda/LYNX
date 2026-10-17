# Round 87: audio crackles during playback ("picotando") — real-time audit, fixes and instrumentation

Branch `feature/glitch87`. English, clean-room (FL Studio's engine was only inspected for *how* it schedules its
audio threads; no Image-Line code is in the repository).

User report (v0.1.5): real-world FL projects (a friend's `.flp` files with samples, native FL plugin ports, maybe
VSTs) crackle on playback on a strong 12-core Windows PC.

Evidence markers: **[MEASURED]** numbers from our tools on this machine, **[DECOMP]** disassembly of
`FLEngine_x64.dll`, **[CODE]** reading of LYNX / JUCE / libc++ sources, **[model]** our interpretation,
**not confirmed** where unknown.

Work dir: `~/.claude/jobs/f8789b49/tmp/r87/` (scripts `brw2.sh` Linux renderwav, `bwin.sh` Windows renderwav with
`OLDPOOL=1` for main's worker pool, `perfattach.sh` render-phase `perf`, `py/prof.py` symbolizer, `survey.sh` all
demos, `ba.sh` before/after).

## 1. Causes found

### 1.0 Patchers were rebuilt about twice a second during playback (clicks + a frozen UI)

[MEASURED] `--rtstress` on *Astes – Bien Duro* (window hidden): the engine's 30 Hz timer ran **1.6 times a second**
(longest gap 713 ms) and the PDC plan was rebuilt **18 times in 10 s** ("unit 344: 0 → 1536 samples").
[CODE] Two defects fed each other:

1. `RackState::build` never reused a parallel container (FL's Patcher, `EffectKind::parallel`): every snapshot —
   every knob turn, and every PDC re-plan — built a new Patcher, i.e. new children with empty reverb tails, delay
   lines and linear-phase convolvers.
2. A fresh Frequency Splitter in linear-phase mode reported latency 0 until its first block applied its parameters,
   then 3n/4 (1536). The engine's timer sees a latency change → publishes a new snapshot (re-planned PDC: delay lines
   of new lengths, i.e. emptied) → which rebuilds the Patcher → whose splitter reports 0 again → …

So any project with a Patcher holding a linear-phase splitter/EQ re-planned forever: each round cost a full
`RackState::build` on the UI thread (~0.6 s for this project: the UI froze between rounds) and, on the audio side,
restarted the Patcher's effects and emptied the PDC delay lines of every edge whose length changed — audible
dropouts/clicks roughly twice a second.

Fix: a container is reused when its branches, children (kind, enabled, parameters, mix, state, sample, plugin id,
side-chains, nested containers) and links are unchanged (`sameContainer`); the Frequency Splitter built from a slot
reports its latency from the slot's parameters before its first block. After the fix: the timer runs at 30.0/s
(longest gap 35 ms), one PDC re-plan right after loading (effects whose latency is known only after their first
block — Fruity Limiter 0→88, Multiband Compressor 0→52, Pitch Shifter, Pitch correction 9→257, Compressor 64→0 —
listed by `renderwav` for every demo; **not changed**, they re-plan once when added). Test
`patcher_container_is_reused_across_snapshots`.

### 1.1 The linear-phase FFT convolver evaluated sin/cos for every butterfly (most of a heavy project's CPU)

[MEASURED] `perf` on the render phase of *Astes – Bien Duro* (262 channels, 71 inserts with effects): **55.6 %** of
all samples inside `peqfl::FftConvolver::block()` — the block FFT convolution shared by Parametric EQ 2's linear-phase
mode, Frequency Splitter's linear-phase crossover and Maximus's linear-phase band split. Its radix-2 FFT called
`std::polar(1, angle)` (one `sin` + one `cos`, with range reduction) **per butterfly** and multiplied through
`std::complex`'s NaN-recovery path; `block()` also allocated a `std::vector` on the audio thread every hop.
Per-unit profile: one Patcher holding linear-phase splitters/EQs took **135 % of real time on its own** (more than a
core), a Frequency Splitter 34 %.

Fix: the twiddle factors are computed once per FFT size (same `std::polar` values, shared table), the complex product is
written out for finite values (same expression order, contraction off), and the scratch buffers are preallocated.
**The output is bit-identical** (`cmp` of 10 s raw renders before/after) and the single-thread CPU of that project went
from a mean of 65–75 % of the deadline to ~35–40 % (§3).

### 1.2 Fruity Convolver computed all its partition levels on the same sample (15 ms spikes)

[MEASURED] Per-unit "longest single call": Fruity Convolver 14–16 ms in *Bombs Away*, *Edlan & Ella Noël*,
*LollieVox* — longer than a whole 512-sample deadline (11.6 ms) in one call, every 16384 samples. [CODE] The
zero-latency partitioned convolver (levels of 128/1024/8192/16384) ran every level whose block began on that sample,
so at multiples of 16384 all four levels did their FFTs and multiply-accumulates at once.

Fix: the larger levels' partitions now start at twice their size or later (128-level: 128…2048, 1024-level:
2048…16384, 8192-level: 16384…65536, 16384-level: 65536…), so a level's output for block *b* depends only on input
up to block *b−1*; that output is computed **during block b−1, a slice every 128 samples** (input copy, bit reversal,
each FFT pass, each partition's multiply-accumulate, the inverse passes and the output are separate slices). Zero
latency is kept; the convolution is the same (new test `convolver_long_kernel_matches_direct_convolution`: a sparse
100 001-tap kernel spanning every level matches direct convolution within 2e-5; the FL goldens of
`convolver_engine` still pass).

### 1.3 The worker pool's wake-up was Sleep() polling on Windows

[CODE] `WorkerPool` woke its threads with `std::counting_semaphore`. In the Windows build (Zig 0.14.1, libc++ for
`x86_64-windows-gnu`) libc++ has no platform wait for Windows (`src/atomic.cpp`: only Linux futex, Apple ulock,
FreeBSD umtx); the fallback polls with `__libcpp_timed_backoff_policy`: after 64 µs it sleeps `elapsed/2`, rounded
**up** to whole milliseconds by `__libcpp_thread_sleep_for` (`Sleep(ceil(ms))`), and `Sleep(8)` after 128 ms. A
worker idle between two audio blocks therefore woke **1–8 ms late**, often after the block's work was over: the
audio thread did nearly everything alone, and the polling workers kept waking at MMCSS priority.

Fix: workers spin briefly (250 µs, `_mm_pause`) after finishing — the next `parallelFor` of the same block (next mixer
layer, next 128-sample automation chunk) comes microseconds later — then block on a real OS semaphore (Win32
`CreateSemaphore`/`WaitForSingleObject`; the futex-backed `std::counting_semaphore` elsewhere). `run()` releases only
as many permits as there are sleeping workers. New test `worker_pool_wakes_sleeping_workers`.

[DECOMP] FL does the same kind of thing: a worker thread routine in `FLEngine_x64.dll` (`0xbb0920`) loads
`AvSetMmThreadCharacteristicsW` / `AvSetMmThreadPriority` dynamically (names at `0x65d490`/`0x65d4cc`, stored at
`0x15f40f8`/`0x15f4100` by the loader at `0x65cca6…`), registers the thread as **"Pro Audio"** (UTF-16 string at
`0xbb0a28`), sets **`AVRT_PRIORITY_HIGH`** (`edx = 1`) and then loops waiting on an event object with an infinite
timeout before each job. LYNX's workers now also call `AvSetMmThreadPriority(HIGH)` after joining "Pro Audio"
(the JUCE WASAPI thread uses `AVRT_PRIORITY_NORMAL`). Which FL job types run on this thread class (mixer vs generator
threads) is **not confirmed**. FL's option strings: "Multithreaded generator processing", "Multithreaded mixer
processing", "Allow smart disable", "Buffer length %dsmp (%dms)" [DATA, strings].

[MEASURED] Windows renderwav (real-time paced blocks of 512, 7 workers, *Astes*, after §1.1): main's pool p99 69–85 %,
max 122–134 %, 1–4 blocks over the deadline; new pool p99 70 %, max 114 % (the first block only), 1 over. The pool is
not the whole story on this project (its critical path is a few heavy serial inserts), but the late wake-ups are gone.

### 1.4 Smaller findings

- First block after playback starts: 2–3× the deadline in most demos [MEASURED] — one-time initialisation of the
  effects' first `process()` (e.g. Fruity Delay 3's `settle()`, ~2.4 ms per instance). One possible click at the very
  start of playback; **not changed** in this round.
- `RackRenderer::process` prepares a snapshot's instruments/effects on the audio thread if the snapshot was built for
  another sample rate (e.g. right after the device's rate changes): a Convolver/Delay 3 then allocates and builds
  kernels on the audio thread. Rare (device rate change), **not changed** (the offline export relies on it).
- Heavy units found by the survey (single-thread share of real time, demos): Fruity Convolver, Patchers with
  linear-phase EQs (fixed above), Harmless pads (*Harmless Essentials*: mean 49 %, p99 166 % single-thread — needs
  the workers), Sytrus/Ogun voices with 10–20 ms single calls at note starts (*Midnight Lady* Ogun 21 ms, *Classic
  rave* 17 ms) **not investigated further**.
- UI: the Playlist/Piano Roll/Channel Rack timers already repaint only the playhead columns; meters use relaxed
  atomics; the snapshot exchange is lock-free; MIDI prefs use a spin lock the audio thread never takes. No lock shared
  between the UI and the audio callback was found. Disk: samples above 30 s are memory-mapped disk caches that a
  prefetch thread reads 4 s ahead (song mode); page faults remain possible after a seek **[model]**.
- Default device: with no saved setup JUCE opened the default WASAPI shared device with its default period (typically
  480 samples = 10 ms at 48 kHz). A fresh install now asks for **512 samples at 44.1/48 kHz** (1024 at 88.2/96 kHz)
  when offered (`prefs::safeBufferSize`); a saved setup is untouched.

## 2. Instrumentation (so the user can tell us)

- `core/RtStats.h`: the audio callback records its duration against its deadline (buffer / rate): callbacks,
  over-deadline count, 80–100 % count, late device calls (start-to-start gap > 1.9 deadlines + 2 ms), max and average
  load, a 10 %-step histogram. Relaxed atomics only.
- `RackRenderer` per-unit profile (`setProfiling`): CPU time and longest single call per channel and per insert effect
  slot (two clock reads per active unit and chunk; on in the app).
- App: the CPU meter's hint shows the over-deadline / late-call counts, the peak load and the three heaviest
  channels/effects; the bar gets a peak mark and a red outline for 5 s after a missed deadline. Settings › Audio shows
  "N blocks over the deadline (max X%)". `%APPDATA%\LYNX\audio-glitches.log` gets a line (at most every 2 s) whenever
  callbacks miss the deadline or the device calls late: time, bar, device/type/rate/buffer/latency/driver dropouts,
  the counters and the heaviest units.
- `LYNX.exe --rtstress project.(flp|lynx) report.txt [seconds] [--buffer N]`: the normal window and the saved audio
  device play the project from the start and the report lists the device, CPU, the counters, the histogram and the
  heaviest units; `result=OK` / `GLITCHES`.
- `renderwav` (build-local, also built for Windows by `tmp/r87/bwin.sh`): `LYNX_BLOCK`, `LYNX_THREADS`,
  `LYNX_REALTIME=1` (blocks paced at real time so workers sleep between blocks as with a sound card),
  `LYNX_PROFILE=1` (per-unit table + longest calls), `LYNX_PROFILE_CPU=1` (Linux: thread CPU clock for units); block
  timing report (mean/p50/p99/max of the deadline, over-budget count, slowest block positions).

## 3. Results

**Core, single thread** [MEASURED] (`renderwav`, live quality, 512-sample blocks, first 20 s of the song; `before` =
main + the instrumentation only, `after` = this branch without the app-side fixes, which renderwav does not exercise).
Instructions retired (`perf stat`, independent of the machine's load, includes loading) and the thread-CPU share of the
11.61 ms deadline per block (noisy on this shared machine):

| Project | Instructions before → after | Blocks over the deadline before → after | p99 before → after |
|---|---|---|---|
| Astes – Bien Duro | 92.9 G → 51.7 G | 183 → 1 (of 1723) | 181 % → 68 % |
| Purps On The Beat – Stroll Up | 124.1 G → 46.1 G | 1018 → 7 | 192 % → 94 % |
| Bombs Away – I'm Awake | 44.8 G → 42.7 G | 46 → 3 | 137 % → 80 % |
| LollieVox – I'll Be Your Madonna | 45.8 G → 41.9 G | 55 → 6 | 137 % → 88 % |
| Edlan & Ella Noël – Song For You | 28.4 G → 26.3 G | 55 → 1 | 110 % → 43 % |
| Olbaid – Harmless Essentials (control) | 44.50 G → 44.50 G | 20 → 43 (noise) | 103 % → 110 % |

The 10 s raw render of *Astes* is bit-identical before/after the FFT change (`cmp`); the convolver's levels change
the float summation order only (tests within 2e-5 of the FL goldens / of direct convolution).

**App, Windows** [MEASURED] (`LYNX.exe --rtstress … 10`, this PC: Ryzen 5 3600, 6 cores / 12 threads, WASAPI shared,
48 kHz, 512 samples = 10.67 ms, 7 workers, window shown):

| Project | Over deadline | Average / max load | UI timer |
|---|---|---|---|
| Astes – Bien Duro (before the Patcher fix, window hidden) | 0 | 17 % / 65–85 % | 1.6/s, gaps 0.7 s, 18 PDC re-plans |
| Astes – Bien Duro | 0 | 14–17 % / 42–44 % | 30/s, 1 re-plan at load |
| Bombs Away | 0 | 18 % / 44 % | 30/s |
| LollieVox | 0 | 22 % / 42 % | 30/s |
| Purps On The Beat | 0 | 35 % / 59 % | 30/s |
| Harmless Essentials | 0 | 7 % / 14 % | 30/s |
| Astes at 256 samples (5.33 ms) | 18 in 10 s | 42 % / 148 % | — |

The "load" here is the callback's time against its deadline, so with 7 workers it is far below the single-thread
figures above. At 256 samples the heaviest demo still misses deadlines on this 6-core PC: the 512 default is
justified; the glitch log also records one missed deadline right after loading (stopped, the effects' first block).

## Where I stopped / next steps

- Ask the user to run `LYNX.exe --rtstress "<friend's project>.flp" report.txt 30` (and send `report.txt` plus
  `%APPDATA%\LYNX\audio-glitches.log`): the report names the device, the deadline misses, late device calls and the
  heaviest channels/effects of *their* projects (VST plugins included).
- Effects whose latency is known only after their first block (Limiter, Multiband Compressor, Pitch Shifter, Pitch
  correction, Compressor) still re-plan PDC once when created; computing it at construction would avoid that click.
- The first block after loading costs 2–3 deadlines (effects' first-block set-up, e.g. Delay 3's `settle()`).
- Per-note start cost of Sytrus / Ogun voices (10–20 ms single calls in the survey) and Harmless pads.
- `RackRenderer` still prepares a snapshot built for another sample rate on the audio thread (device rate change).
- The two per-unit timers (round 84's `ProcessTimes` for the Plugin performance monitor, this round's `UnitTimes`) could
  be merged.
