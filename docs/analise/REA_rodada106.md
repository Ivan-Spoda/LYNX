# Round 106: LYNX ASIO — full-duplex input, 32-bit build, control panel latency readout

Branch `feature/asio106`, follows round 97 (`REA_rodada97.md`, the driver itself). English, clean-room: FL Studio
ASIO's DLL was decompiled only to see *how* it handles the input; no Image-Line code is in the repository.

Evidence markers: **[MEASURED]** numbers from our tools on this machine (Ryzen 5 3600, "USB Audio CODEC", Windows
class driver), **[DECOMP]** Ghidra (REA MCP) on `ILWASAPI2ASIO_x64.dll`, **[EXEC]** FL Studio ASIO's own DLLs run
by our test host, **[CODE]** our sources, **[DOC]** Microsoft / Steinberg documentation, **[model]** our
interpretation, **not confirmed** where unknown.

Work dir: `~/.claude/jobs/f8789b49/tmp/r106/` (`probe/cprobe.cpp` capture probe, `host/duplex.cpp` full-duplex test
host for 64- and 32-bit, `ut/` the stand-alone drift tests, `runs/` driver logs, `loop.py` recording analysis,
`flref/` FL decompilation — kept out of the repository).

## 1. What the recording side of this device can do (WASAPI capture probe)

[MEASURED] `cprobe.exe`, capture endpoint "Microfone (USB Audio CODEC)":

| Query | Result |
|---|---|
| Mix format | 48000 Hz, 2 ch, 32-bit float |
| `GetDevicePeriod` | default 10 ms, minimum 3 ms |
| Exclusive formats | 16-bit PCM only, 44.1 and 48 kHz (as the playback side) |
| Exclusive event init | 144 / 192 / 256 / 480 frames all accepted, stream latency = one period |
| Exclusive *polling* init | accepted, but `GetNextPacketSize` returned whole-buffer packets far faster than real time — unusable; LYNX ASIO uses event-driven capture only |

[MEASURED] Timing of the capture packets (driver log, `LYNX_ASIO_LOG`): an exclusive capture stream at the 3 ms
minimum period delivers packets up to 8 ms apart now and then, and the device flags
`AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY` (frames lost) about once per second — even when our thread had read the
previous packet 6 ms earlier, so it is the device/driver, not the reader. At 4 ms (192) still lost packets; from
6 ms (288) on: none in 60 s runs. The longest wait for a packet was 11.6–12.5 ms at a 6 ms period and 16.6 ms at the
10 ms shared period.

## 2. How FL Studio ASIO handles the input [DECOMP] / [EXEC]

Audio thread at 0x180006690 (round 97 found the render side):

* Capture is a **shared, polled** stream with a 1 s buffer, opened on the chosen or default capture endpoint, in the
  endpoint's mix format; the thread polls it every 1 ms (`Sleep(1)`).
* Every capture packet goes through the same fixed-step fractional resampler as the output (0x180005ca0: position
  += step, step set from the two sample rates — no adaptive term was found; *adaptive drift correction: not
  present as far as decoded*).
* The resampled input queue is capped at **4 × max(ASIO block, device period)**; frames beyond that are dropped and
  a global counter is incremented. A packet flagged silent clears the queue to that cap.
* When an input stream is open, host blocks are only run while the input queue holds at least **2 × block** —
  the capture clock gates the host (output queue then absorbs the difference).
* [EXEC] FL Studio ASIO run by our test host (64-bit and its 32-bit `ILWASAPI2ASIO.dll`), default settings, block
  256: 44.1 kHz, reported latency **input 2601 / output 2646 samples (59 / 60 ms)**. The input buffers carried a
  loud noise-like signal (RMS −6 dBFS) in both builds — not confirmed why (perhaps the input is not configured in its
  panel on this PC); no further time spent on it.

[model] FL's approach is robust but has three effects LYNX ASIO avoids: a large fixed latency, hard drops when the two
clocks drift (the queue cap), and input-gated scheduling of the host (output timing follows the microphone).

## 3. What LYNX ASIO does now

### 3.1 Recording stream (`asio/LynxAsio.cpp`)

* **Exclusive mode** opens the recording device exclusive too, event-driven, at the playback period but **not below
  twice the device's minimum** (§1: 6 ms here), in the best integer/float format it accepts at the host's rate. If
  it refuses (format, device in use) the shared stream is used and the log says why.
* **Shared mode** records with an event-driven shared stream (IAudioClient3 low-latency period when the device has
  one and no conversion is needed; else the classic stream with Windows' rate conversion; 100 ms of slack).
* The audio thread waits on three handles — commands, the capture event, the render event — capture first, so a
  recorded packet is taken before the block that needs it; it also drains the capture stream between the blocks of
  a long burst.
* **Lost frames are replaced by silence of the same length**, using the device position that `GetBuffer` returns
  (`u64DevicePosition` jumps by the number of lost frames): a take stays in time with the playback even when the
  device drops a packet.
* Packets are stamped with their **capture time** (`u64QPCPosition` + packet length), not with the time we read them.
* Device invalidation on the capture side asks the host for a reset, like the render side.

### 3.2 Clock drift: `asio/InputDrift.h`

The playback device paces the host; the microphone runs on its own clock (another device, or Windows' converter).
`InputDrift` is the FIFO between them plus an adaptive resampler:

* **Fractional reader**: 16-tap Kaiser-windowed sinc (β 8, cutoff 0.92 × Nyquist, 256 phases, linear between
  phases). [EXEC] 15 kHz passes within ±0.3 dB at unity; 8 frames of look-ahead.
* **Level estimate**: the FIFO level after each block **plus the frames the device has recorded since its last
  packet** (from the capture time stamp). Without that term the slow phase walk between the two clocks looks like a
  level change (the first controller version chased a 480-frame sawtooth with a 35 s period and underflowed).
* **PI controller** on the 0.5 s average: 1000 ppm per 500 frames of error, an integral term for the constant clock
  difference, at most ±0.5 % (normally a few hundred ppm: inaudible, < 1 cent).
* **Target level** = capture packet + burst/2 − block/2 + look-ahead + 32 + lateness margin, where burst = the device
  period rounded up to whole blocks (worst-case phase between packets and bursts). Lateness margin from the
  panel's safety setting: none = max(¼ period, 2 ms), small = max(½ period, 4 ms), normal = max(1 period, 6 ms) —
  sized from §1's measured packet lateness.
* **Start**: the level is set to the target at 0.3 s and once more at 1.5 s (devices deliver irregularly while
  starting), then only the controller moves it.
* **Underflow** (packets later than the margin): the block is completed with silence and the target is **raised by
  the shortfall** (at most 2× the initial target), so an irregular device clicks once, not every few seconds; the
  driver then sends `kAsioLatenciesChanged` to hosts that support it. **Overflow** (host stopped reading):
  resynchronise to the target.
* [EXEC] `tests/AsioDriftTests.inc` (in the main test run): a simulated capture clock at +300 / −300 / 0 ppm with
  10 ms packets and 128-frame blocks, ±1 ms jitter, 90 s — no underflow/overflow, the controller finds the clock
  ratio within 30 ppm, and a 1 kHz sine reaches the host without a dropped or repeated frame; the same at 3 ms
  (exclusive-like); unity transparency; a 100 ms capture stall recovers (≤ 8 short blocks, target raised within its
  limit, clean afterwards).

### 3.3 Latencies reported to the host

* Output: the device queue (shared target or exclusive period) + block (as round 97).
* Input: FIFO target + block + half a capture packet (age of the newest frame) + look-ahead. [model] Not verified
  against a physical loopback (§5).

### 3.4 Control panel

Mode, output device, input device (Windows default / any endpoint / none), buffer size, safety margin (now also the
input margin, so it is enabled in both modes), the device explanation, and a new **latency readout**:
"Latency at 144 samples (3.0 ms): output 6.0 ms, input 19.0 ms (exclusive)" for the settings being edited, plus
"Running now: exclusive, buffer 144, output 6.0 ms, input 19.0 ms (exclusive)" for the open stream. It updates on
every change (device, mode, buffer, margin). English or pt-BR from the Windows UI language.

### 3.5 32-bit driver

[MEASURED] Zig 0.14.1 targets `x86-windows-gnu` (not `i686`). The only source change needed: thread entry points
are now `WINAPI` functions (`__stdcall` on 32-bit) instead of lambdas. Checked by disassembly: on this target clang
compiles member functions as **`__thiscall`** (`this` in ECX, `ret 4` for one argument), which is what MSVC-built
ASIO hosts call; COM entry points are exported undecorated through the `.def` file; only system DLLs are imported.
`scripts/build-lynx-asio.sh` builds `lynx-asio.dll` (x64) and `lynx-asio32.dll` (x86); `build-windows-zig.sh`, the
installer, the zip and the release workflow carry both. "Register LYNX ASIO.bat" registers the 32-bit one with
`%SystemRoot%\SysWOW64\regsvr32.exe` (its keys land in the 32-bit registry view, `WOW6432Node`, by redirection);
the uninstaller unregisters both (and now reads the 64-bit registration from the 64-bit view — before it read the
32-bit view and never found it).

### 3.6 LYNX itself

* **"LYNX ASIO (built in)"**: when `lynx-asio.dll` is next to `LYNX.exe` and LYNX ASIO is not registered, LYNX lists
  a driver type "LYNX ASIO" that loads the DLL directly (no administrator step). Registered, it appears under
  "ASIO" like any driver. Settings › Audio explains it (pt-BR included); "Driver settings..." opens its panel.
* The ASIO-file device re-reads the latencies on `kAsioLatenciesChanged`.
* `--rtstress ... --record take.wav` records the input during the run exactly like Rec + Play (the take becomes a
  clip) and reports the take's length, channels and peak with the device's latencies.

## 4. Measurements

[MEASURED] Full-duplex test host (`duplex.exe`, 64- and 32-bit; plays 10 ms tone bursts, records both inputs),
`LYNX_ASIO_LOG` statistics:

| Run | Host | Mode / block | Reported in / out | Underflows | Device gaps | Clock ratio at end | Clicks in the take |
|---|---|---|---|---|---|---|---|
| 60 s | 64-bit | exclusive / 144 (capture 288) | 912 / 288 (19.0 / 6.0 ms) | 0 | 0 | +9 ppm | 0 |
| 60 s | 32-bit | shared / 128 | 1600 / 1088 (33.3 / 22.7 ms) | 0 | 0 | +39 ppm | 0 |
| 8 s each | 64 + 32 | shared / 128, exclusive / 144 | — | 0 | — | — | 0 |
| reference | 64 + 32 | FL Studio ASIO / 256, 44.1 kHz | 2601 / 2646 (59 / 60 ms) | — | — | — | — |

(LYNX `--rtstress` rows: §6.)

## 5. Loopback

There is no cable or acoustic path from this PC's output to its microphone: the 1 kHz bursts the host plays do not
come back in any recording (folded correlation peak/median ≈ 6, lag random between runs; the microphone records
room noise around −50 dBFS RMS). So the round-trip latency could not be measured physically here; the recordings
were checked for continuity instead (no click-like discontinuities, no underflows, the drift controller settles).
To measure it on a machine with a loopback cable: `duplex.exe lynx-asio.dll 144 20 rec.wav` and
`python3 loop.py rec.wav` (prints the burst's lag).

## 6. LYNX runs

(filled in below as they are made)

## Where I stopped / next steps

* Done: exclusive + shared capture, drift-compensated input FIFO (tested), lost-frame filling, latency model,
  panel readout, 32-bit DLL + registration + packaging, built-in LYNX ASIO type, `--rtstress --record`.
* Next: LYNX `--rtstress` with the built-in type (shared/exclusive) and `--record`; land.
