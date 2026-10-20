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
4. **Last line of defence in the renderer** (core/RackRenderer.cpp): after each insert effect, a vectorised finite
   check; a block with NaN / inf is muted and that effect is `reset()`, so one bad setting never poisons the rest of
   the mix (`RackRenderer::nonFiniteEffectBlocks()` counts them; the selftest fails when any occurs).

Remaining non-finite outputs found only at 22.05 kHz (Vintage Phaser with all controls at max, Fruity Multiband
Compressor, Frequency Splitter): FL's exact engines are only defined for 44.1 kHz and up; caught by the renderer's
guard, left as they are (not confirmed what FL does at that rate).

`LYNX.exe --fxfuzz report.txt [rounds]` runs every native editor with the engine: random / extreme parameters, mouse
drags over a grid of points on each editor page (plain left button), the editor painted, audio rendered at 44.1, 48
and 96 kHz. A short run of it is part of `--selftest` (`effects=OK`), so CI fails if an editor or engine crashes or
outputs NaN. tests/CrashFuzzTests111.inc: every effect at 44.1/48/96 kHz with extreme parameters, the shelf case, the
two rate bugs and the renderer's NaN guard.

## Where I stopped

- Done: §1–§3 in feature/crash111.
- Next: GUI reproduction on the tester's laptop (not allowed while she uses it); confirm Waves plugins scan and load
  there with this build; consider a breadcrumb file for fail-fast crashes of the main process.
