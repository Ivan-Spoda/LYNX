# Round 157a (crash157): "the EQ crashes when opening, and other plugins too"

2026-10-25. Report from a real user testing the released v0.1.15-alpha on Windows (hybrid Intel + RTX 3050 laptop,
Parsec virtual display, pt-BR interface, SSL ASIO at 44.1 kHz): "the Facet EQ plugin crashes when opening (its
parameters / editor), and other plugins too". It blocked v0.1.16.

Evidence markers: [LOG] her files (copies in tmp/gf/logs157: crash file, crash-history.log, autosaves, Windows event
log and the WER AppHang report); [RUN] reproduced / checked with the Windows build on this PC; [CODE] read in LYNX's
source (file named); [TEST] proven by a test.

## 1. What happened on her laptop

| Time (2026-10-25) | Event | Source |
|---|---|---|
| 00:07:59 | LYNX **crashed** right after "opening the editor of Auto-Tune Pro" / "editor open: Auto-Tune Pro". Stack inside the GPU driver's OpenGL ICD: `SetPixelFormat → wglSetPixelFormat → DrvSetPixelFormat → (driver)`, called from the plugin's DLL. | [LOG] crash 2026-10-25 00-07-59.txt, crash-history.log |
| 00:14–00:16 | Autosaves of a near-empty project; between them she added a **Parametric EQ** (`"peq"`, 36 params, defaults) to Insert 1 after a reverb. | [LOG] the two autosaves (diff) |
| 00:17:32 | Windows **AppHangB1** for LYNX.exe 0.1.15 ("LYNX não está respondendo" → she closed it). No crash file (a frozen UI runs no crash handler). | [LOG] Application event 1001/1002, WER ReportArchive AppHang_LYNX.exe_… |
| 2026-10-18 | (v0.1.7) BEX64 0xc0000409 in `igd10um64xe.dll` 32.0.101.7076 (Intel's D3D10/11 user-mode driver). | [LOG] earlier dumps, event log |

So "Facet crashes" was the **Parametric EQ freezing LYNX** (both are EQs; Facet EQ itself opens fine), and "other
plugins too" is the **Auto-Tune Pro editor crashing in the Intel OpenGL driver**.

## 2. Crash / freeze list

### 2.1 Parametric EQ: every way of opening it froze LYNX (since v0.1.10) — FIXED

- [RUN] v0.1.15 release zip, `--lang pt-BR`, fresh project: Insert 1 › Slot 1 › Parametric EQ › "Abrir EQ
  paramétrico" → title "LYNX (Não está respondendo)", the process burning one core (CPU time +1 s per second) until
  killed. Facet EQ opened the same way on Insert 2 works (screenshot tmp: C:\Users\kauan\lynx-test\crash157\s11.png).
- [CODE] Cause: mutual recursion. `NativeEffectWindows::open` (src/ui/PluginEditors.cpp) forwarded the Parametric EQ
  to `ParametricEqWindows::open`, and `ParametricEqWindows::open` (src/ui/ParametricEqEditor.cpp, since commit
  15897026 "Parametric EQ 2 in FL's arrangement", 2026-10-17) forwards to `NativeEffectWindows::open`. With
  optimisation both are tail calls, so the compiler turned the recursion into an endless loop (no stack overflow, no
  crash file — a freeze). Every entry point goes through it: double-click on the mixer slot, the effects panel's
  "Open parametric EQ" button, the slot menu.
- [CODE] Why no test caught it: `--fxfuzz` / `--plugui` / the self-test build the editors with
  `makeNativeEffectEditor` directly, never through the window-opening functions the UI uses.
- Fix: `NativeEffectWindows::open` no longer forwards the Parametric EQ (it opens its layout editor there).
- [TEST] new `LYNX.exe --opentest report.txt` (below) opens every effect through `NativeEffectWindows::open`; on
  v0.1.15's code it would stop at "Parametric EQ / default: open" with `HANG`.

### 2.2 Auto-Tune Pro (third-party) editor crashes in the Intel OpenGL driver — MITIGATED

- [LOG] The plugin's own OpenGL setup (`SetPixelFormat` on its child window) crashes inside the GPU driver. LYNX
  cannot fix the driver; three mitigations:
  1. [CODE] Plugin windows (src/ui/PluginWindow.cpp) now draw LYNX's part with **GDI** instead of JUCE 8's default
     Direct2D: the plugin's OpenGL child then sits in a classic redirected top-level window, as in hosts that never
     used a DXGI swap chain for the parent (re-applied whenever JUCE re-creates the native window).
  2. [CODE] A new session step "editor shown: X" after the window is visible (the OpenGL setup runs then), and at
     the next start, when the session ended during a plugin's editor steps, LYNX offers **"Run X in a separate
     process from now on?"** (`setPluginAlwaysSeparate`, file separate-process.txt; also in the wrapper menu as
     "Always run X in a separate process"). In its own process a crash of that editor only stops the plugin.
  3. [CODE] Crash files now end with the faulting thread's frames as `module+0xoffset` (from the exception context,
     unwound with the modules' unwind tables). JUCE's backtrace drops every frame without a symbol, i.e. all of a
     release LYNX.exe, and never names the module (her file showed "RegisterProcTableCallback + 0x18f073" with no
     DLL name). The start-up check reads these module names.

### 2.3 Intel D3D11 driver crash in LYNX's own drawing (2026-10-18, igd10um64xe.dll) — MITIGATED

- [CODE] JUCE 8 draws every Windows window with Direct2D (`HWNDComponentPeer` engine 1). New option **Options ›
  General settings › "Draw with the GPU (Direct2D)"** (gpu-rendering.txt; on by default). Off = every LYNX window
  draws with GDI (applied to open windows at once, new windows within 0.5 s; src/ui/WindowOwner.cpp).
- [CODE] Automatic fallback: when the previous session's crash file shows a GPU / Direct3D / Direct2D module in the
  faulting frames (igd*, nvwgf2um, nvoglv, atidxx, amdxx, d3d11, dxgi, d2d1, opengl32, dcomp…) and no plugin editor
  was opening, LYNX turns GPU drawing off and says so.

## 3. UI freeze watchdog (new)

- [CODE] src/platform/CrashGuard.cpp `startHangWatch` / `heartbeat`: the UI app starts a watcher thread; a 5 Hz timer
  on the message thread is the heartbeat. After 2 s without one, the watcher suspends the message thread for a
  moment, unwinds its stack (Windows x64, no symbols needed, no allocation while suspended), writes
  `%APPDATA%\LYNX\autosave\hang <time>.txt` (frames as module+offset with the nearest exported symbol) and a session
  step "UI not responding: stack in autosave\hang …"; a second sample follows if the freeze lasts (a loop shows the
  same frames twice), and "UI responding again after N s" when it ends. The newest 20 hang files are kept. So the
  next "it crashed" that is really a freeze leaves a file saying where.

## 4. Systematic test: `--opentest`

`LYNX.exe --opentest report.txt [--visible]` (src/Main.cpp `runOpenTest`), on the real message loop:

- every built-in effect (`NativeEffectWindows::canOpen`, not the Patcher) opened exactly as the mixer opens it
  (Facet EQ, Parametric EQ, the Pro editors, Newtone/Newtime, Formula Controller, layout editors), each with these
  states: default (FL default preset where FL is installed), all 0, all 1, random, and **old projects**: the
  parameter list cut to half, to 90, 114 and 128 (Facet's 90 → 114 growth, the 128 → 160 slot change) and read back
  through the project reader;
- every instrument's channel window (`openChannelSettings`) fresh and saved/read back;
- per state: open, render 30 ms at 44.1 / 48 / 96 kHz, paint every window, resize to a third and to double, close;
- windows off-screen unless `--visible`; a watchdog thread ends the test with `HANG at <step>` after 15 s without
  progress, and any crash ends the process (no `result=` line).

## Where I stopped

(filled in at the end of the round)
