# Round 97: low latency on Windows — WASAPI modes in Settings and the "LYNX ASIO" driver

Branch `feature/asio97`. English, clean-room (FL Studio ASIO's DLL was only inspected for *how* it drives WASAPI; no
Image-Line code is in the repository).

User report: on the user's PC (Ryzen 5 3600, "USB Audio CODEC" device) LYNX's "Windows Audio" device would not go
below ~480 samples; 256 had one glitch on a real project; "Realtek ASIO" kills every host; "FL Studio ASIO" works.

Evidence markers: **[MEASURED]** numbers from our tools on this machine, **[DECOMP]** Ghidra (REA MCP) on
`ILWASAPI2ASIO_x64.dll` (FL Studio ASIO, `C:\Program Files\Image-Line\FL Studio ASIO`), **[CODE]** reading of
LYNX / JUCE 8.0.4 sources, **[DOC]** Microsoft / Steinberg documentation, **[model]** our interpretation,
**not confirmed** where unknown.

Work dir: `~/.claude/jobs/f8789b49/tmp/r97/` (`probe/probe.cpp` WASAPI capability probe, `fl/` the DLL copy and
Ghidra output, `rt/` the `--rtstress` reports).

## 1. What this device can do (WASAPI probe)

[MEASURED] `probe.exe` on the default render endpoint (USB Audio CODEC, Windows class driver):

| Query | Result |
|---|---|
| Mix format | 48000 Hz, 2 ch, 32-bit float (extensible) |
| `GetDevicePeriod` | default 10.000 ms, minimum 3.000 ms |
| `IAudioClient3::GetSharedModeEnginePeriod` | default = fundamental = min = max = **480 frames** |
| Exclusive formats | only **16-bit PCM** accepted (24-in-32, 24, 32 → `AUDCLNT_E_UNSUPPORTED_FORMAT`) |
| Exclusive event init, 16-bit | 32/64/96/128 frames → `AUDCLNT_E_INVALID_DEVICE_PERIOD`; **144** (3 ms), 192, 256, 480 OK |

[model] Consequences:

* Classic shared mode mixes every program in 10 ms engine periods. A smaller client buffer cannot lower the latency
  below that period; it only makes the callbacks bursty (JUCE still lists sizes down to the 3 ms device minimum, which
  is why 256 "worked" but glitched).
* The IAudioClient3 low-latency shared mode is real on drivers that report a smaller engine period, but this device's
  class driver pins it to 480 frames, so "Windows Audio (Low Latency Mode)" gives nothing extra here.
* Exclusive mode reaches the device minimum: **144 frames = 3 ms** at 48 kHz, 16-bit only.

## 2. How FL Studio ASIO drives WASAPI [DECOMP]

`ILWASAPI2ASIO_x64.dll` exports the standard in-proc COM entry points (`DllGetClassObject`, `DllCanUnloadNow`,
`DllRegisterServer`, `DllUnregisterServer`); its PDB path names Steinberg's ASIO SDK sample (`asiosample.pdb`), RTTI
names `ILWasapiToAsioDriver`, `IASIO`, `WASAPIWrapper`, `WASAPIWrapper::Client`. Settings live under
`HKCU\Software\Image-Line\ASIO` (value `bufferSize`, plus endpoint ids).

The audio thread (function at 0x180006690):

1. `timeBeginPeriod(1)`, `CoInitialize`, then the capture and render endpoints: the ids chosen in the panel, or the
   default endpoints (`GetDefaultAudioEndpoint(eCapture/eRender, eMultimedia)`).
2. Both clients are opened in **shared mode, polling (no event flag), with a 1-second buffer**
   (`Initialize(SHARED, 0, 10000000, 0, mixFormat)`); format = each endpoint's mix format.
3. The thread joins MMCSS "Pro Audio" at `AVRT_PRIORITY_CRITICAL`.
4. Loop: `Sleep(1)`; read the ASIO block size from the driver object; target fill =
   **2 × max(ASIO block, device default period)** frames; drain the capture packets into a FIFO; while the render
   queue (`GetCurrentPadding`) is below the target, run the host's `bufferSwitch` and push its output through a
   fractional resampler into the render buffer.

[model] So FL Studio ASIO accepts any ASIO block size because the host callbacks are decoupled from the device by a
FIFO, but in shared mode its real output latency is still about two engine periods (≈ 20 ms here). It is stable
because it never asks the shared engine for less than one period of queued audio. It has no exclusive mode
(not confirmed: no `AUDCLNT_SHAREMODE_EXCLUSIVE` path was found in the audio thread).

## 3. Part 1 — the WASAPI modes in Settings > Audio

JUCE 8 already offers three WASAPI device types [CODE `juce_WASAPI_windows.cpp`]: "Windows Audio" (shared),
"Windows Audio (Low Latency Mode)" (IAudioClient3 `InitializeSharedAudioStream` at the minimum engine period) and
"Windows Audio (Exclusive Mode)" (event-driven exclusive; tries float 32, int 32, 24, 16 bit formats). LYNX now:

* names them for what they are (pt-BR included): "Windows Audio (shared)", "(shared, low-latency period)",
  "(exclusive, lowest latency)", "DirectSound (legacy)";
* explains each under the driver box; for shared mode it prints the engine period of the open device;
* hides, in classic shared mode, buffer sizes smaller than the engine period (they never lower the latency);
* `--rtstress ... --type "<driver type>" --device "<output>" --buffer N` tries another driver for one run and then
  restores the user's saved device.

(measurements: section 5)

## 4. Part 2 — LYNX ASIO (own universal ASIO driver)

`asio/LynxAsio.cpp` (one file, ~1500 lines, built by `scripts/build-lynx-asio.sh` with Zig/MinGW into
`lynx-asio.dll`; CMake target `lynx-asio` for MSVC). Written from Microsoft's WASAPI documentation and the ASIO SDK
interface headers (GPLv3 option, like LYNX's ASIO host); only the general architecture of section 2 was taken
from FL Studio ASIO, and two things are done differently on purpose: an event-driven stream instead of 1 ms
polling, and an exclusive mode.

* **COM server**: `DllGetClassObject` / `DllCanUnloadNow` / `DllRegisterServer` / `DllUnregisterServer`; own
  CLSID `{E987C593-898E-4557-BFE3-4288B0228718}`; `QueryInterface` answers the CLSID itself, which is how ASIO
  hosts ask for `IASIO`. Registration (administrator, `regsvr32`) writes `HKLM\SOFTWARE\Classes\CLSID\{…}`
  (InprocServer32, ThreadingModel Apartment) and `HKLM\SOFTWARE\ASIO\LYNX ASIO` (CLSID, Description).
* **Apartments**: every WASAPI object lives on the driver's own MTA threads (the audio thread, and short-lived
  probe threads for `init` and the panel), whatever apartment the host calls from.
* **Shared mode**: the render stream is event-driven; with the host's rate equal to the mix rate it first tries
  `IAudioClient3::InitializeSharedAudioStream` at the minimum engine period (only when that is shorter than the
  default), else the classic `Initialize(SHARED, EVENTCALLBACK)` (with Windows' sample-rate conversion when the
  host asks for another rate). On each device event it tops the device queue up to a **target fill** of 1, 1.5 or 2
  engine periods ("safety margin" in the panel; default 2 = FL Studio ASIO's choice) from a FIFO that the host's
  `bufferSwitch` calls fill block by block — so any ASIO block size works (32…4096).
* **Exclusive mode**: event-driven at the ASIO block (never below the device's minimum period; re-initialised with
  the aligned size on `AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED`), format chosen in the order float32, int32, 24-in-32,
  int24, int16. The device gets one whole buffer per event.
* **Input**: a shared capture stream drained at every render event into an input FIFO primed with one capture
  period plus one block of silence, trimmed when it grows past two periods plus two blocks (clock drift).
* The host always sees `ASIOSTFloat32LSB`; `bufferSwitchTimeInfo` when the host supports time info; sample
  position and system time for `getSamplePosition`; `kAsioResetRequest` when the device stops calling for 2 s, is
  invalidated, or when the panel / sample rate changes.
* **Thread**: MMCSS "Pro Audio" at `AVRT_PRIORITY_CRITICAL` while streaming.
* **Panel** (`controlPanel`, a small Win32 window built in code, English or pt-BR from the Windows UI language):
  mode (shared / exclusive), output and input device (Windows default, any endpoint, or no input), preferred
  buffer size, safety margin, and an explanation with the device's real shared period and exclusive minimum.
* **Settings**: `%APPDATA%\LYNX\lynx-asio.ini` (no registry); `LYNX_ASIO_INI` overrides the path and
  `LYNX_ASIO_LOG` writes what was opened plus stream statistics (late device events, slow host bursts, input
  underflows) — used for the measurements below.

### Testing without registration

* `tmp/r97/host/asiohost.cpp`: a 150-line ASIO host that `LoadLibrary`s a driver DLL, creates it through its
  class factory and plays a quiet sine.
* LYNX: hidden `--asiodll "<file.dll>[|{CLSID}]"` adds a device type "ASIO driver file"
  (`src/engine/DllAsioDevice.cpp`, an `AudioIODevice` on top of any `IASIO*`); with `--rtstress` it is selected
  automatically, so the real engine plays a real project through the driver:
  `LYNX.exe --asiodll lynx-asio.dll --rtstress project.flp report.txt 15 --buffer 144`.

### Shipping and registration (for other programs)

`build-windows-zig.sh` builds `lynx-asio.dll` next to `LYNX.exe`; the installer, the portable zip and the release
workflow carry it with `Register LYNX ASIO.bat` / `Unregister LYNX ASIO.bat` (they ask for administrator rights
and run `regsvr32`). By hand, from an administrator prompt:

    regsvr32 "%LOCALAPPDATA%\Programs\LYNX\lynx-asio.dll"        (install)
    regsvr32 /u "%LOCALAPPDATA%\Programs\LYNX\lynx-asio.dll"     (remove)

The uninstaller unregisters it first when `HKLM\SOFTWARE\ASIO\LYNX ASIO` exists (one UAC prompt).

## 5. Measurements

[MEASURED] User's PC, USB Audio CODEC, `--rtstress` of the girlfriend's project (`gf92\full.flp`), 15 s each
(`tmp/r97/rt/rt.sh`, reports in `tmp/r97/rt/`). "Latency" = the output latency the device reports to LYNX.

| Driver (LYNX device type) | Asked | Got | Output latency | Over deadline | Late calls | Result |
|---|---|---|---|---|---|---|
| Windows Audio (shared) | 480 | 480 @ 48 kHz | 480 (10.0 ms) | 0 | 0 | OK |
| Windows Audio (Low Latency Mode) | 480 | 480 @ 48 kHz (engine period is fixed) | 480 (10.0 ms) | 0 | 0 | OK |
| Windows Audio (Exclusive Mode) | 64 | open failed | — | — | — | below the 3 ms minimum (the UI never offers it) |
| Windows Audio (Exclusive Mode) | 128 | JUCE fell back to 480 @ 44.1 kHz | 960 | 0 | 0 | OK |
| **Windows Audio (Exclusive Mode)** | **144** | **144 @ 44.1 kHz (3.3 ms)** | **288 (6.5 ms)** | 0 | 0 | **OK** |
| Windows Audio (Exclusive Mode) | 256 | 256 @ 44.1 kHz | 512 (11.6 ms) | 0 | 0 | OK |
| LYNX ASIO exclusive (`--asiodll`) | 64 / 128 | 144 @ 48 kHz (device minimum) | 288 (6.0 ms) | 0 | 0 | OK |
| **LYNX ASIO exclusive** | **144** | **144 @ 48 kHz (3.0 ms)** | **288 (6.0 ms)** | 0 | 0 | **OK** |
| LYNX ASIO exclusive | 256 | 256 @ 48 kHz | 512 (10.7 ms) | 0 | 0 | OK |
| LYNX ASIO shared, margin "none" | 128 | 128 | 608 (12.7 ms) | 0 | 1498 * | OK by the driver's own count |
| LYNX ASIO shared, margin "normal" | 128 | 128 | 1088 (22.7 ms) | 0 | 1499 * | OK by the driver's own count |
| FL Studio ASIO (test host, reference) | 128 / 256 | 44.1 kHz | 2520–2646 (57–60 ms) reported | — | — | — |

\* In shared mode the device wakes the driver once per 10 ms engine period and the driver runs four 128-sample
host blocks back to back; LYNX's "late device calls" heuristic (gap > 1.9 × block + 2 ms) counts every such burst.
The driver's own statistics (`LYNX_ASIO_LOG`) for those runs: 3295 device events, **0 late events** (max gap
11–14 ms), **0 slow bursts** (max 5.9 ms of host work per 10 ms period), 0 input underflows.

LYNX ASIO exclusive, driver statistics over the whole run (including project loading): 2–4 late device events
(max gap 7.3 ms) and one slow burst (4.9–7 ms) — all outside the 15 s measurement window, i.e. at the start of
playback; none during the measured playback. [model] probably the first heavy block when the transport starts.

Test host (`asiohost.exe`, sine): LYNX ASIO exclusive 144 → 1333 events in 4 s, 0 late (max gap 3.29 ms);
shared 128 with margin "none" → 0 late (max gap 10.6 ms); stop/start cycles work (streams are reset on stop).

[model] Conclusions for this PC: the shared modes cannot go below the 10 ms engine period (the class driver pins
the IAudioClient3 period too); **exclusive mode — JUCE's or LYNX ASIO's — runs the real project at 3 ms buffers
(≈ 6 ms output latency) without glitches**. LYNX ASIO's shared mode has a lower real latency than FL Studio ASIO
here (12.7 ms with margin "none", 22.7 ms with "normal", vs. ~57 ms reported by FL Studio ASIO at 44.1 kHz).

## Where I stopped / next steps

* Done: Settings > Audio names/explains the WASAPI modes (pt-BR), hides useless shared sizes; `--rtstress --type
  --device` restores the user's device; LYNX ASIO driver (shared + exclusive, panel, ini, log); `--asiodll` test
  mode; installer / zip / release workflow carry the driver and the register/unregister helpers.
* Not done / ideas:
  - exclusive *capture* (input is always a shared stream, so input latency stays ≈ one capture period + block);
  - LYNX could list `lynx-asio.dll` next to `LYNX.exe` without registration (the `--asiodll` device type already
    works); kept hidden until the user has tried the driver in other hosts;
  - the "late device calls" heuristic should know when a driver delivers blocks in bursts (shared-mode ASIO);
  - a resampler inside LYNX ASIO (shared mode at other rates uses Windows' converter; exclusive offers only the
    device's own rates); 32-bit build of the driver for 32-bit hosts.
