# REA round 121: opening the vocal projects with the Waves trials installed

Since the Waves trials (WaveShell1-VST3 17.1) were installed, LYNX loads the Waves slots of the girlfriend's vocal
projects (`tmp/gf/proj2`: kwe, game over) for real, and opening them got much slower: headless `open_ms` of kwe
1.2 s → 16–23 s (round 116), one GUI open hung ~4 min. Scope: find where the time goes and fix what is ours.

Evidence markers: [EXEC] runs of LYNX / our own probes on this PC, [code] reading LYNX / JUCE source, [model] our
reading. Third-party rule: the Waves shell was only *hosted*: its public VST3 entry points were called and timed from
our side (tmp/load121/probe*.cpp: `LoadLibrary`, `InitDll`, `GetPluginFactory`, `countClasses`, `getClassInfo`,
`setHostContext`, `createInstance`, `initialize`); nothing of it was opened, disassembled or patched; licensing not
touched. Measurements in `C:\Users\kauan\lynx-test\load121` (`hl.sh` headless, `gui.sh` + `lxwatch.ps1` GUI).

## 1. Where the time went [EXEC]

`load.log` of a GUI open of kwe with v0.1.8 + main (`LYNX-base.exe`), `WavesLocalServer.exe` not running:

    total: 13002 ms
      plugin_instantiate               11748 ms  x14
      plugin SSLChannel Stereo         11411 ms  x2     <- the first Waves plugin of the project
      adopt_plugins                      640 ms  x1
      plugin_lookup                      417 ms  x1
      (every other Waves plugin: 45–90 ms per instance; samples 16 ms wall)

So it is the **first** plugin created from the shell, not one shell per plugin, and not repeated scans. Black-box
probe of the same shell, step by step (`probe.exe`, ms):

| step | Waves service already running | service not running |
|---|---|---|
| LoadLibrary | 37 | 37 |
| InitDll | 325 | 329 |
| GetPluginFactory | 1 | 1 |
| **countClasses** (587 classes) | **718** | **9 785** |
| createInstance (first plugin) | 529 | 579 |
| initialize | 76 | 68 |
| next plugins: createInstance + initialize | 20–25 | 20–25 |

The first factory call waits for the vendor's background service (`WavesLocalServer.exe`, started by the shell when it
is not running; it stays up for a while after the last client). JUCE makes that call inside the first
`createPluginInstance`, which LYNX runs on the message thread (JUCE / VST3 requirement) → the window froze 11–13 s,
Windows calls it "not responding". The ~4 min hang of round 116 was the first run right after the Waves install
(first start of the service / its plugin cache); it did not happen again here (14 cold-service opens, 13–18 s).

[EXEC] The same calls made on **another thread** first (`WARM=1 probe.exe`: LoadLibrary + InitDll + GetPluginFactory +
countClasses on a `std::thread`, factory kept) leave the main thread's later calls at 0.0 ms (same factory object,
already listed) and the plugins create and initialise normally. That is what a plugin scanner does on its threads too.

Other costs of ours found on the way:
- `adopt_plugins` 0.65–1.5 s and `plugin_lookup` 0.4 s: `findForeign` checked `File::exists` for every list entry on
  every lookup — the shell adds ~600 entries with the same file, asked for each of the ~30 slots. Cached per open.

## 2. A shell that hands out the wrong plugin [EXEC]

New check in LYNX (`createRightInstance`): the instance's name against the plugin asked for. In **every** open of kwe /
game over, the first Waves plugin of the process was another one: `SSLChannel Stereo -> JJP-Guitars Mono/Stereo`
(round 119 had seen "Q10" in fxmeasure). So until now her first SSL channel strip was a JJP-Guitars plugin fed the
SSL's state — wrong sound, silently. Probe (`probe3/4.exe`):

    before setHostContext: 587 classes, "SSLChannel Stereo" at 495
    after the first setHostContext: 1174 classes, "SSLChannel Stereo" at 990; index 495 now holds "JJP-Guitars Mono/Stereo"
    further contexts (same or other objects): unchanged

[code] JUCE (`VST3ModuleHandle::create` → `findClassMatchingDescription`) looks the class up **by index** before
`VST3ComponentHolder::initialise` sets the host context, then fetches `getClassInfo (classIdx)` again → another class.
Setting a context ahead from LYNX's own warm-up thread did not settle it (JUCE's context reordered again:
"MannyM EQ Stereo"), and creating it again after *releasing* the wrong instance sometimes gave the wrong one again
(the shell went back to its first list). What works every time (12 runs): keep the wrong instance alive, create
again (up to 3 tries), then release the wrong one. If it were still wrong, the slot reports a load failure ("the
plugin file handed out another plugin (X)") instead of playing another plugin. Same check in the separate-process
child (`PluginSandbox`).

## 3. Fix

- `core/ModuleWarmup` (+ `PluginHost::startModulesFor`): modules worth starting ahead (VST3 Waves shells only —
  JUCE-made plugins take the loading thread as their message thread, so this is not done for every plugin) are
  loaded on a thread of their own as soon as an open knows its plugins: at app start for a project on the command
  line (during the splash), at the panel's first step otherwise, before the samples decode in `importFlpFile`
  (export). The factory is kept for the session (not released at exit: round 111's no-teardown exit), so later opens
  find it ready. Not with the plugin sandbox (the child loads it). `LYNX_NO_MODULE_WARMUP=1` turns it off (A/B).
- Progress panel: a new step between the samples and the plugins waits for those modules **without blocking** (timer
  poll): "Starting WaveShell1-VST3 17.1..."; after 8 s "… is slow to start (N s): its vendor's service is starting.
  You can wait or continue without it." with a **Continue without it** button: that module's plugins are left out of
  this open (LYNX substitutes play their slots, listed as usual). A plugin created by anything else while its module
  is still starting waits for it (never two threads inside the shell at once).
- `createRightInstance`: the wrong-plugin check and retry of §2 (in-process and in the sandbox child).
- `findForeign`: file existence cached per open (`fileStillThere`).
- Splash: the 2.1 s minimum counts from the process start (the bar fills over what is left, at least the pepper's
  landing). Window revealed 3.17–3.19 s → 2.32 s after launch (`LYNX_TRACE_STARTUP`, 2 runs each).

Tests: `tests/LoadTests121.inc` (which modules start ahead, display names, a module started once per session,
failed ones retried on the next open, the wait report and slow message, skip and its return on the next open, a
registry destroyed while its thread still loads). pt-BR strings for the panel texts and the load failure.

## 4. Before / after [EXEC]

GUI open from the command line (`gui.sh`: time until the title says the project and the window answers; longest
stretch the window did not answer, 100 ms polling), 2 runs each, idle PC:

| project | Waves service | base: loaded / longest freeze | new: loaded / longest freeze |
|---|---|---|---|
| kwe | not running | 17.1–17.9 s / 12.8–13.5 s | 13.2–15.2 s / 1.4–2.8 s |
| kwe | running | 7.3 s / 2.9–3.5 s | 4.4–4.8 s / 1.4–1.7 s |
| game over | not running | 18.1–18.2 s / 12.8 s | 13.9 s / 1.6 s |
| game over | running | 8.8–8.9 s / 3.4 s | 5.4–5.5 s / 1.4 s |

(The remaining ~1.4 s freeze is the first plugin's own `createInstance` + `initialize` and the wrong-plugin retry, on
the message thread; ~0.4–1 s of the cold-service gain is the warm-up starting during the splash.)

Headless `--export --pattern` (`open_ms`): kwe 12.7 → 12.3 s cold / 3.7 → 2.9 s warm; game over 15.1 → 12.6 s cold /
4.9 → 3.4 s warm (headless has no UI to keep alive, so the service start-up is only overlapped with sample decoding;
the gain there is the cached file checks: `adopt_plugins` 0.7–1.5 s → 0.04–0.08 s).

FL Studio 2026 on the same projects (`flwatch.ps1`, one FL at a time, idle PC; time until its "Problems loading the
project" box, i.e. loading done): kwe 6.6 s (2 runs), game over 11.1 s (2 runs). That box lists WaveShell1-VST3 15.5 /
16.0, Auto-Tune Pro and Filterverse as missing: FL loads **no** Waves plugin here (round 119), so FL's numbers are the
projects without their Waves chain, while LYNX's include it (4.4 / 5.4 s with the Waves service running).

## Where I stopped

- Done: §1–§4, tests, pt-BR. Landing via land.sh.

### Next
- The first Waves plugin still costs ~1.4 s on the message thread (its own createInstance/initialize + the retry).
- A `.lynx` project (not .flp) with Waves plugins does not start the shell ahead yet (its rack builds synchronously).
- With the plugin sandbox on, every Waves plugin runs in its own child: each child pays the shell start (≥ 0.7 s,
  ~10 s with the service down) and the message thread waits for it — not measured.
- Auto-Tune Pro shows as missing in both LYNX and FL here (not activated / not installed yet, tune120's territory).
