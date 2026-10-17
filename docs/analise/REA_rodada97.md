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

(in progress)

## 5. Measurements

(in progress)

## Where I stopped / next steps

* Part 1 code is in the WIP commit; Windows build + `--rtstress` measurements next.
