# REA round 111: "plugins crash LYNX" (v0.1.7 on the tester's laptop)

User report (paraphrased): some plugins crash LYNX, e.g. "eq parameter". Evidence markers: [DUMP] the two Windows
minidumps of the crashes on the laptop, [DATA] files in the laptop's `%APPDATA%\LYNX`, [EXEC] runs on this PC (fuzzers
with AddressSanitizer / UBSan, LYNX.exe), [model] our reading. Dumps, logs and the tester's files stay in tmp/ (never in
the repo); third-party plugin binaries were not analysed — only the module names and offsets on the LYNX process's
stack.

## 1. What crashed on the laptop: the plugin scan child, at exit, inside a Waves shell's teardown

[DATA] Windows' event log has two crashes of `LYNX.exe 0.1.7.0` on 2026-10-18 (18:33 and 18:41): faulting module
`igd10um64xe.DLL` (Intel's D3D11 user-mode driver), exception `0xC0000409` (fail-fast), offset `0x11c549`. No
`crash*.txt` was written: a fail-fast does not reach the unhandled-exception filter. LYNX itself kept running (its
autosave and `audio-glitches.log` have entries after 18:41:41).

[DUMP] Both processes were not the LYNX the user was working in but children of the background plugin scan:
command line `LYNX.exe --scan-one VST3 "C:\Program Files\Common Files\VST3\WaveShell1-VST3 16.0_x64.vst3" …\lynx-scan-….xml`.
The faulting thread's stack, oldest first (return addresses, resolved against the 0.1.7 build — the code around them
is identical: each address follows a `call`):

    LYNX.exe+0xd9711c   C runtime start-up (mainCRTStartup)
    LYNX.exe+0xd97340   right after `call exit` (the import thunk of ucrtbase!exit)
    ucrtbase+0xa0813    exit → atexit table / DLL detach
    WaveShell1-VST3 16.0_x64.vst3 (several frames)   the shell's own teardown
    d3d11.dll (many frames) → igd10um64xe.DLL        → fail-fast in the GPU driver

So the scan child had already written its result and was leaving `main`; at `exit()`, the static `juce::DynamicLibrary`
that `countVst3Classes` keeps open (and JUCE's module cache) unloaded the Waves shell, whose teardown released Direct3D
objects through the Intel driver, which aborted the process. Consequences for the user:

1. Windows' "LYNX stopped working" dialog popped up during the background scan (while she was working, e.g. on an EQ) —
   this is the "crash" she saw;
2. the parent saw a non-zero exit code and **blacklisted the whole Waves shell** ([DATA] `plugins.xml`: the only
   blacklisted file is `WaveShell1-VST3 16.0_x64.vst3`), so none of her Waves plugins (CLA, SSL, VEQ, PuigTec,
   Waves Tune…) were ever available: projects using them got LYNX substitutes, which also explains part of report (c)
   ("the voice sounds different").

Fixes (src/Main.cpp, src/engine/PluginHost.cpp, src/platform/CrashGuard.*):
- the scan child disables Windows Error Reporting's UI (`SetErrorMode` + `WerSetFlags(WER_FAULT_REPORTING_NO_UI)`)
  and, once its result is written, ends with `TerminateProcess` (`crashguard::exitNow`): no DLL detach / static
  destructors of the plugin;
- the sandbox worker processes do the same, and the main app ends the same way at the end of `shutdown()` when any
  plugin module was loaded in the session (everything is saved and closed by then) — the same teardown crash would
  otherwise show a crash dialog when closing LYNX;
- the parent accepts a complete, non-empty result even when the child's exit code is not 0 (older builds, plugins that
  crash after describing themselves); a child that hangs or crashes before writing its result is still blacklisted;
- `scan-retry.txt` goes to 2: on the first start of this version the blacklist is cleared once more, so the shells
  rejected for this reason are scanned again.

Not confirmed: whether the Intel driver fault happens only in the scan child or also when the tester's main LYNX
closes with a Waves plugin loaded (no dump of that). The quick exit covers both. The coordinator suggested a Direct2D
fallback; the dumps show LYNX's own rendering is not involved, so it was not done.

## 2. Crash-proofing hosted plugins

`PluginEffect` / `PluginInstrument` (src/engine/PluginHost.cpp) now call the plugin through `crashguard::run` (SEH
`__try/__except`, src/platform/CrashGuard.cpp — the only unit built with `-fms-extensions`; [EXEC] verified on this PC
with zig 0.14.1 / x86_64-windows-gnu that access violations and C++ exceptions thrown through the guard are caught and
that normal C++ exception handling and the heap keep working afterwards): `processBlock`, parameter changes from
automation (audio thread) and from the UI / controllers, `getStateInformation` when saving, `releaseResources`.
A plugin that faults is marked crashed and never called again: an effect slot plays through (bypass), an instrument is
silent, its last saved state is kept, its editor does not open (message), and the main window shows "The plugin X
crashed and was switched off…" (en + pt-BR) and appends a line to `%APPDATA%\LYNX\plugin-crashes.log`. Not covered:
a crash inside the plugin's own editor window (its window procedure runs outside LYNX's calls) — the out-of-process
option (Options → Run plugins in a separate process) remains the full protection.

[EXEC] tools/testplugin/lynx_test_crash.c, a CLAP effect that crashes on purpose (env `LYNX_TEST_CRASH`):
`process` = access violation in its 20th `process()` call: `LYNX.exe --plugintest` renders to the end, the slot plays
through at full level afterwards (peak 0.71 instead of the plugin's half-gain 0.35) and `plugin_crashes=` lists it;
`exit` = fail-fast in `DllMain(DLL_PROCESS_DETACH)` at process exit (the Waves case): `LYNX.exe --scantest` finds the
plugin (`found=1`, nothing blacklisted). (Run with an isolated plugin list: --plugintest adds the file to
`%APPDATA%\LYNX\plugins.xml`; the entry was removed again on this PC.)

## 3. Built-in effects: fuzzing (tools/fxfuzz.cpp, scripts/fxfuzz-asan.sh, LYNX.exe --fxfuzz, --selftest)

[EXEC] `tools/fxfuzz.cpp` builds every EffectKind (default slot), every FL effect preset (.fst) and every effect slot
of every .flp under a folder, and drives each one: every parameter alone at 0 / 1 / random, all parameters at 0, at 1
and random, 5 sample rates (22.05–192 kHz), block sizes 1…2048, noise / sine / silence / denormal / +30 dB / DC /
impulse input, sidechain on and off, `setParameter`, `reset`, the state-backed controls (core/StateParams.h), and the
editors' read-outs (formatEffectParam, parameter names, layouts, the parametric EQ's graph). Built with ASan + UBSan
(`scripts/fxfuzz-asan.sh`). Found and fixed:

1. **Parametric EQ (LYNX law, the 7-band EQ): NaN.** A shelf band with a high gain and the Q knob in its upper half
   (shelf slope S > ~2.3 at ±18 dB) makes the cookbook's `sqrt((A + 1/A)(1/S − 1) + 2)` negative: NaN coefficients, the
   insert outputs NaN for good (stuck in the filter state, then in every effect after it) and the editor's curve
   becomes NaN. Fix in `Biquad::shelf` (core/Biquad.h): the root's argument is held ≥ 0.01 (shelf Q ≤ 10); unchanged
   for every valid setting (the cookbook range S ≤ 1 and the mixer's track EQ never reach it). `peq::responseDb`
   skips a non-finite stage and the editor maps a NaN level to 0 dB, so a curve can never carry NaN into the path
   renderer. This is the likely "eq parameter" problem if it was LYNX's own EQ.
2. **Multiband Delay below 44.1 kHz: heap overflow** (`designCrossover`: the linear-phase mask index ran past its 1025
   bins when the upper crossovers lie above Nyquist). Clamped.
3. **Fruity Vocoder above 107 kHz: signed overflow** (`rate * 20000` in int). Computed in double (same value below).
4. **Fruity Limiter: NaN** with Gain at −inf (0), a negative knee and the compressor threshold at its bottom: the
   knee curve overflows to inf and inf · 0 = NaN (core/FruityDynamics.cpp `compressorGain`). A non-finite gain now
   becomes 0 at gain 0 (the largest float otherwise, then clipped by the ceiling as before); finite values unchanged.
   Found by the core test below (zig build), not by the ASan run: the input decides whether the curve overflows.
5. **Last line of defence in the renderer** (core/RackRenderer.cpp): after each insert effect, a vectorised finite
   check; a block with NaN / inf is muted and that effect is `reset()`, so one bad setting never poisons the rest of
   the mix (`RackRenderer::nonFiniteEffectBlocks()` counts them; the selftest fails when any occurs).

Remaining non-finite outputs found only at 22.05 kHz (Vintage Phaser with all controls at max, Fruity Multiband
Compressor, Frequency Splitter): FL's exact engines are only defined for 44.1 kHz and up; caught by the renderer's
guard, left as they are (not confirmed what FL does at that rate).

`LYNX.exe --fxfuzz report.txt [rounds]` runs every native editor with the engine: random / extreme parameters, (with `--drags`) mouse
drags over a grid of points on each editor page (plain left button), the editor painted, audio rendered at 44.1, 48
and 96 kHz. A short run of it is part of `--selftest` (`effects=OK`), so CI fails if an editor or engine crashes or
outputs NaN. tests/CrashFuzzTests111.inc: every effect at 44.1/48/96 kHz with extreme parameters, the shelf case, the
two rate bugs and the renderer's NaN guard.

## 4. Session breadcrumb for fail-fast crashes (src/platform/CrashGuard.*, src/Main.cpp)

A fail-fast (0xC0000409, the Intel driver case of §1) or a forced end runs no crash handler, so the user gets no
`crash*.txt` and nobody knows what LYNX was doing. While the UI runs, `%APPDATA%\LYNX\sessions\session-<pid>.txt`
holds a header (version, pid, process creation time) and the last 10 steps, each written synchronously: plugin being
loaded / loaded (name, format, file), plugin editor being opened / open / closed, project being opened, crash report
written, closing. A clean exit deletes it. At the next start, a session file whose process is gone (pid not running, or
running with another creation time = reused id; another open LYNX window is left alone) is appended to
`%APPDATA%\LYNX\crash-history.log`, removed, and a message says "LYNX closed unexpectedly … Last step: …" (en + pt-BR).
Command-line modes and the scan / sandbox children write nothing.

[EXEC] this PC, round-111 build: UI started → session file with the header; `Stop-Process -Force` (stands in for a
fail-fast) → next start appended it to crash-history.log and showed the message (last step "start-up"); the new
session recorded "opening project self-demo.lynx"; closing the window normally removed the file.

## 5. Waves (official trials on this PC) with this build

[EXEC] `LYNX.exe --scantest` over copies of the four WaveShell bundles (17.1, 17.2, 17.4, 17.1-ARA): 620 plugins
found, none rejected, 4 min, no crash dialog. `LYNX.exe --plugintest "<WaveShell1-VST3 17.1_x64.vst3>" report "<name>"`
(new optional name argument to pick one plugin inside a shell) for CLA-2A, SSLChannel (the E-Channel), RCompressor,
DeEsser, Doubler2, H-Delay, NS1 and Waves Tune Real-Time (all Stereo): each loads, processes audio (finite peak, no
plugin crash caught), and the process exits with code 0 (no teardown crash). `--plugintest` itself was fixed: it checked
for the plugin before the rack was built (the engine builds it when prepared), so it reported "not loaded" for every
VST3; it now checks after the render and prints the host's load failures. This PC's plugin list was restored after the
tests (plugintest adds the file to it).

Tester's laptop (2026-10-20 ~18:40): reachable, but she was logged in with FL Studio open, so no GUI reproduction was
tried. Read-only check: no LYNX crash events since the two of 2026-10-18 (§1).

## Where I stopped

- Done: §1–§5 in feature/crash111 (also two fuzz fixes: Fruity Delay Bank time product in 64-bit at 192 kHz; Maximus
  saturation table index guarded against NaN / inf).
- 2026-10-20 ~20:00 (after the second power loss): merged main (tech118: project packages, ProjectSafety) — conflicts
  in Main.cpp (selftest now needs package AND effects), PluginHost.h (setRenderMode skips a crashed plugin) and the
  generated I18nData.h (regenerated). The tester's laptop was unreachable over Tailscale (ssh timeout), so the GUI
  reproduction below is still open; it needs 0.1.8 installed there anyway.
- Next: GUI reproduction on the tester's laptop with 0.1.8 when she is not using it (open a Waves editor, close LYNX,
  check the event log / crash-history.log); after 0.1.8 is installed there, confirm the Waves shell is rescanned
  (scan-retry 2) and her vocal projects load the real Waves plugins instead of substitutes.
