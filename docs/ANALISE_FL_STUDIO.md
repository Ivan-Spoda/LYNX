# Behavioural analysis of FL Studio 2026 (FLEngine_x64.dll)

> **Nature of this document.** *Clean-room* reverse engineering, behavioural only, to guide the design
> of a new and independent DAW (C++/JUCE). No Image-Line code, and no decompiled pseudocode, has been
> copied. Everything below is a description, in our own words, of behaviour, structures (conceptually)
> and algorithms. Addresses and symbol names serve only as traceability of the evidence.

- **Target:** `~/projetos/daw-analise/FLEngine_x64.dll` (Delphi, PE x86-64, ~22 MB)
- **SHA-256:** `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02`
- **Tools:** REA MCP + Ghidra 12.1.4 (59,803 procedures, 42,077 strings)
- **Date:** 2026-10-12

### Confidence legend

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings / RTTI names of Delphi classes and units |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `FUN_xxxxxxxx` = function address in Ghidra; `G[0x15e....]` = global pointer of the DLL
(Delphi unit variable).

---

## 0. Architecture overview

The engine is a single global "god object" with state in unit variables, not an encapsulated object
graph. The hot path is:

```
Driver (ASIO thread / own mixing thread / DirectSound)
  └─ FUN_00d0ece0  render(N samples)  ── splits the buffer into sub-blocks at the sequencer's tick boundaries
       ├─ FUN_00d0e840  advances 1 tick (song position, markers, countdown)
       └─ FUN_00d0eb10 → FUN_00d0d4c0  processes 1 sub-block:
            ├─ transport/loop/markers, start/stop fade
            ├─ generators (Channel Rack channels) → summed into the mixer tracks' buffers
            │     ("threaded" generators run in a parallel task group)
            └─ FUN_00cf42f0  mixer: runs the "layers" of the mixer threading map
```

A single global `CRITICAL_SECTION` (`G[0x15e3d40]`) wraps the entire render in all drivers and
also the rebuild of the mixer threading map. **[DECOMP]**

---

## 1. Audio core: callback, threading, buffers, drivers

### 1.1 The tick-driven render loop — `FUN_00d0ece0` **[DECOMP]**

Behaviour:

1. Receives an absolute target position in samples (the driver's accumulated counter).
2. Keeps a current sample counter (`G[0x15e0b78]`) and the sample position of the **next tick**
   (`G[0x15e1a00]`).
3. The next tick's position comes from a **double** accumulator (`G[0x15e3450]`) to which
   "samples per tick" (`G[0x15e2600]`, also double) is added at each tick, and then rounded. That is, the
   tick time is fractional and does not accumulate rounding error. There is an alternative mode in which
   the accumulator is multiplied by a float factor and the result is shifted `<<3` (positions in 1/8 of a
   sample?) **[HYP]**.
4. When the counter reaches the tick boundary, it increments the tick counter (`G[0x15e3aa0]`) and calls the
   tick processor (`FUN_00d0e840`), which increments the song position in ticks (`G[0x15e3550]`),
   handles pre-roll/countdown (mode `G[0x15e09f8]==2`) and markers.
5. Renders a **sub-block** from the current point up to `min(next tick, target)`. The sub-block size goes to
   `G[0x15e4dc8]`; if it is larger than the allocated maximum, the work buffers are reallocated on the spot
   (`FUN_00d0ead0` → `FUN_00d0cee0`).
6. After each sub-block, the master buffer pointer advances `len*8` bytes (interleaved stereo float32)
   (`FUN_00d0eb10`). At the end, the pointer returns to the start.

**Central conclusion:** FL **does not process the driver buffer all at once**. It slices it at the
sequencer's tick boundaries (PPQ), and all note and automation events fall on sub-block boundaries.
This is FL's classic model ("tick-accurate"), and explains why FL delivers variable-size buffers
to plugins (and why the "fixed size buffers" option exists, which increases latency; see strings
`0x14890ae`).

In offline render there is a tail detector: after the end of the song, it tests whether the master has
dropped below a threshold (dB×−96 scaled) to end the render (`FUN_00d0eb10`). **[DECOMP]**

Overflow protection: when the sample counter exceeds `0x7FA81BFE` (~2.14 billion), all
position counters (int and double) are shifted together by that same amount (`FUN_012aef00`).
**[DECOMP]**

### 1.2 Processing a sub-block — `FUN_00d0d4c0` **[DECOMP]**

Observed order:

1. **Transport:** handles the pending position jump, the playlist markers (0x34-byte records
   with position and type: type 1 = start/loop marker, 3 = stop/pause, 9/10 = punch-in/punch-out
   **[HYP for the names]**), the loop region and the "selection loop".
2. **Start/stop fade:** a global gain envelope with a cosine curve raised to powers
   (`(1−cos)/2` raised to 9, mapped to 0.01…1) avoids clicks when starting or stopping. It is protected by
   its own `CRITICAL_SECTION`.
3. **Generators (channels):** for each channel, runs the generator plugin into the channel's own buffer and
   then **sums** that buffer into the buffer of the destination mixer track (channel field `+0x258`, limited
   to 0…500). Channels with several outputs have a list of outputs, each with a track offset. The sum into
   each track is protected by a **per-track `CRITICAL_SECTION`** (track `+0x4c`), because generators
   running on different threads may write to the same track. After the sum the track is marked as
   "has signal" (`+0x148 = 2`).
4. **Parallel generators:** if there are generators marked as "threaded", a task group is
   fired (submit) before the serial loop and awaited (wait) afterwards (`DAT_016b8dd8`, vtable +0x10/+0x18).
5. **Mixer:** `FUN_00cf42f0` (see §2).

### 1.3 Threading model **[DECOMP]**

- **Thread pool:** obtained from an external factory (function pointer `DAT_015f4648`, probably
  exported by the host executable) with default count = number of logical threads of the machine
  (`FUN_00666b40`, `FUN_012c6f30`). Delphi unit: **`QuickThreads`**, class `TSTBTaskGroup` **[STR]**.
- **Per-worker context:** an array (`G[0x15e0348]`) with one context object per worker slot
  (scratch buffers), created at initialisation (`FUN_012c6f30`).
- **Task group:** a structure with N slots of 0x20 bytes, a callback, a context and a **Win32
  Event** (`CreateEventW`, auto-reset) for the join (`FUN_00666a60`, `FUN_00666960`).
  `submit+wait` = vtable +0x10 / +0x18 (`FUN_00666a30`).
- **Work distribution (work claiming):** each task enters a loop doing an *atomic decrement*
  (`LOCK dec`) of a global counter of remaining items (`G[0x15e2f38]`). The value obtained is the index of
  the item to process, and the loop ends when the counter becomes negative (`FUN_011fec40`, `FUN_011febf0`).
  There is no per-thread queue: it is a shared counter ("self-scheduling").
- **Dedicated threads observed [STR]:** `TMixerThread` (ASIO with its own thread), `TMixerThread_DS`
  (DirectSound), `TRenderThread`/`TRenderWorker`/`TMixDownWorker`/`TRealtimeMasterWorker` (render
  to file), `TMIDIThread`/`SyncThread`, `TWAVStreamingThread`, `TSampleLoaderThread`,
  `TBridgeReaderThread`/`TBridgeProcessingThread` (bridge), browser/scan threads.
- **MMCSS:** the DLL dynamically imports `AvSetMmThreadCharacteristicsW` / `AvSetMmThreadPriority` and
  contains the string `"Pro Audio"` **[STR]**; the exact use was not traced.
- **User options [STR]:** "Multithreaded generator processing", "Multithreaded mixer processing",
  "Allow threaded processing" (per plugin), "Allow smart disable", "Multithread plugins on this map".

### 1.4 Drivers

**Only ASIO and DirectSound live in FLEngine.** There is no WASAPI string in the DLL. "FL Studio
ASIO" is a separate ASIO driver (the engine only detects it and warns: *"Your audio device is set to FL Studio
ASIO. This can cause inaccurate MIDI sync timing."*). **[STR]**

#### ASIO — callback `FUN_00c56ae0` (bufferSwitchTimeInfo) **[DECOMP]**

Records the block timestamp (system time from ASIOTime or `timeGetTime`, plus QPC). Then
follows one of three modes:

| Mode | Condition | Behaviour |
|---|---|---|
| **A. Inline** | no mixing thread | Inside the ASIO callback: takes the global lock, advances the counter by +bufferSize, points each track's I/O pointers directly at the active half of the ASIO double buffer, renders, calls `ASIOOutputReady` and clears the master. |
| **B. Own thread** (`TMixerThread`, `FUN_00c55740`) | mixing thread active | The callback does `SignalObjectAndWait(evStart, evDone, timeout)`. The mixing thread wakes up, renders and signals. **With timeout** (`DAT_016b8814`, configurable): if it expires, the callback resets the state and counts an underrun (`FUN_00c57950`, `FUN_01253980`). |
| **C. Intermediate buffer** | flag `DAT_016b87e8` | Copies or converts between FL's intermediate buffers and the ASIO buffers (per-format conversion function), signals the mixing thread to render the **next** block asynchronously and adds 1 buffer to the reported output latency. |

There is also a reentrancy/busy guard (`G[0x15e0430] > 0` → skips the render).

Opening ASIO — `FUN_00c57a00` **[DECOMP]**:

- Builds the list of `ASIOBufferInfo` (0x18 bytes: isInput, channel, 2 pointers) only with the channels used
  by the mixer tracks: outputs come in **stereo pairs** per track (`+0x370` = output pair index,
  `+0x378` = input).
- If several tracks send to the same physical output, the first uses a "copy" routine and the others a
  "sum" routine (function pointers stored at `+0x74`).
- Detects float32/float64 sample types (LSB/MSB) and marks the others as "needs conversion".
- Writes to the log: driver name and version, `Buffer length: N frames`, I/O latencies and `Time out: X ms`.

#### DirectSound — `TMixerThread_DS` (`FUN_012aef00`) **[DECOMP]**

Circular buffer in two halves. Waits for 2 notification events (`WaitForMultipleObjects`) or,
alternatively, polls the play cursor. Renders the free half and converts float → int16
(or copies float32 when the format is 32-bit). Detects underrun when the play cursor falls inside the
freshly written region.

#### External pull — `FUN_0125a760` **[DECOMP + HYP]**

A "render N samples now" function (limited to the buffer size), used when the engine is driven from
outside. Hypothesis: FL as plugin/ReWire or external host.

### 1.5 Buffer format **[DECOMP]**

- Internal track and master buffers: **interleaved stereo float32** (L,R,L,R…), size
  `frames*2` floats.
- Maximum sub-block size: dynamic, grows on demand.
- Driver buffer: defined by the driver (ASIO) or by the option "Buffer length %dsmp (%dms)" (DirectSound).

### Implications for our DAW
- Adopting **event-sliced render** (sub-blocks at tick or event boundaries) is optional. In
  JUCE it is cleaner to use *sample-accurate* with `MidiBuffer` and offsets, but slicing simplifies the
  automation of parameters of plugins that only accept per-block values. Recommendation: sample-accurate by
  default, with optional slicing per plugin.
- Accumulate the transport position in **double/int64** and compute the tick boundaries without accumulating error.
  Using int64 from the start eliminates the counter "rebase" that FL has to do.
- Avoid the **global lock** around the render: use a lock-free queue for UI→audio messages.
- The "driver callback → handoff to a mixing thread with timeout" design is useful for unstable
  drivers, but costs latency or jitter. In JUCE the `AudioIODevice` callback is already the real-time
  thread, so the recommended approach is to render inline and use workers for parallelism.
- Summing into shared tracks with a per-track `CRITICAL_SECTION` is simple but can cause contention.
  Prefer private buffers per generator, summed by the track owner.
- Implement the fade ramp on transport start/stop (anti-click).

---

## 2. Mixer

### 2.1 Track structure **[DECOMP]**

- Global static array of tracks (`G[0x15e06a0]`), with **0x1478 bytes (5,240 B) per track**.
- Indices: the code limits destinations to **0…500** and treats index **501 (`0x1f5`)** as special (it is left out
  of the post-mix cleanup and of the normal layer). Interpretation: 0 = Master, 1…500 = inserts, 501 = special
  track ("Current"/preview) **[HYP for the exact role]**. FL 2026 therefore supports up to
  **500 inserts**.
- **10 effect slots per track** (array of 10 pointers at `+0x134c`). A flag at `+0x380` indicates
  "slots enabled" (`FUN_00cf3660`).
- Identified fields: `+0x34` audio buffer pointer; `+0x4c` track lock; `+0x74` output routine
  to hardware; `+0xd0/+0xe4/+0xec` ASIO I/O pointers; `+0xfc` level in the graph (see
  below); `+0x148` signal flag; `+0x1c0` track "processor" object (virtual method
  `Process(len)`); `+0x370/+0x378` physical output/input.

### 2.2 Mixer threading map — `FUN_00cf3790` **[DECOMP]**

Scheduling algorithm (our description):

1. There is a list of active tracks **sorted by dependency level** (`+0xfc`), i.e. a topological
   order of the routing graph.
2. The list is scanned and grouped into **layers**. A new layer begins when the level increases, and
   also at the first track with index > 500.
3. Within a layer, tracks are grouped into **units**. A track opens a new unit
   only if it has "parallelisable" plugins and has no dedicated physical output. Otherwise, it is attached to the
   current unit and runs serially with it.
4. For each layer a task group is created with `min(number of units, number of workers)` tasks.
5. The log literally says: *"Mixer threading map rebuilt: N layer(s) / Layer i: M unit(s) / Unit j:
   track(s) …"*.

Execution (`FUN_00cf42f0` → `FUN_00cf4250` → `FUN_011fec40` → `FUN_00cf4430`):

- **Layers run in sequence, with a barrier between them**: submit + wait per layer.
- Within a layer, workers take units via the atomic counter. Each unit processes its tracks
  serially by calling the virtual `Process(len)` of the track object, with the worker context injected.
- Without multithreading (empty map), tracks run serially in list order.
- At the end, tracks that received signal have their buffer zeroed for the next block (except 501).

### 2.3 Routing and sends **[STR + partial DECOMP]**

- In the FLP, routing between tracks comes in event `0xEB` (235) and track parameters in `0xEC` (236);
  see §5. The topological level `+0xfc` is recomputed from that graph.
- Classes `TFXTrackSideIOList` and `TMixerImplementation` **[STR]**.

### 2.4 Latency compensation (PDC) **[STR]**

- Menus and options: "Plugin delay compensation", "Auto PDC", "Reset PDC", "Reset manual latency on all
  tracks", "Compensate for master latency", "Delay never bypass latency comp", "Trim PDC silence" on
  export.
- Each track has **manual latency + plugin latency**: `"%sms (manual) + %sms (plugin)"`.
- Option "Delay the output of this track by the total mixer latency when it has an external output".
- Recording latency compensation: "Compensate for latency when recording audio from an external
  device"; the diagnostics show `"Latency: input: %dsmp, output: %dsmp, output + plugins: %dsmp"`.
- The exact PDC algorithm (where the delay lines live) **was not decompiled**.

### Implications for our DAW
- Copy the **idea** of scheduling by topological layers: it is simple, deterministic and works well.
  A possible improvement is to replace the per-layer barrier with dependency-based scheduling ("ready
  counters") to reduce idle time.
- Grouping tracks without useful parallelism into "serial units" reduces dispatch overhead.
- Plan for a high track limit (FL: 500) with dynamic allocation, instead of a static array of 5 KB
  per track.
- PDC: latency per node = manual + sum of plugins. Delay each path up to the maximum of the summing point and
  offer "trim PDC silence" on export.

---

## 3. Sequencing

### 3.1 Timebase and tempo **[DECOMP]**

- The **PPQ** comes from the FLP header (`FLhd`, offset 12) and is kept in a global (`G[0x15e0db0]`). A special
  legacy value (16) is replaced by the current default PPQ. There is a "Timebase (PPQ)" UI and the data-loss
  warning when reducing the PPQ **[STR]**.
- **Tempo stored as integer BPM×1000.** Event `0x9C` (156) sets the full tempo. The legacy event
  `0x42` (66) sets the integer part (×1000, preserving the fraction). There is also a fine-tuning
  event `0x5D` (93) **[DECOMP for 0x42/0x9C]**.
- Song position in **integer ticks** (`G[0x15e3550]`), incremented 1 by 1 by the tick processor.

### 3.2 Channel Rack, patterns, notes **[partial DECOMP]**

- Channels are `TChannel` objects in a list (`G[0x15e2218]`) with the generator plugin, their own output buffer
  and destination track **[DECOMP]**. `TChannelSample`/`TChannelSamplePool` handle the samples **[STR]**.
- Patterns live in a global array with a stride of 0xC0 bytes per pattern (`G[0x15e2aa0]`). Maximum
  pattern index on load: **999** (event `0x41` limited to 999) **[DECOMP]**.
- Modes: `G[0x15e0ef0]==1` → Song mode (playlist), otherwise Pattern mode **[DECOMP]**.
- **Note record** (read from event `0xE0`/224): up to FL 20.99 it is **24 bytes per note**; in newer
  versions the size is the native size of the current record (`FUN_00f558e0`) **[DECOMP]**. The read
  confirms these fields:
  - byte `+0x10` = *fine pitch* (versions < 3.1.6 get 120 = centre) **[DECOMP]**;
  - byte `+0x13`: the low nibble is converted in versions < 5.3 (probably MIDI channel/colour) **[DECOMP]**.
  - The full layout (pos u32, flags u16, channel u16, length u32, key u16, group u16, fine pitch u8,
    ?, release u8, MIDI channel u8, pan u8, velocity u8, mod X u8, mod Y u8) matches the public
    community documentation (PyFLP) and the two fields confirmed above. **[strong HYP]**

### 3.3 Playlist and arrangements **[partial DECOMP]**

- There are **multiple arrangements**: event `0x63` (99) selects or creates the current arrangement
  (*"FLP : Wrong arrangement index !"*). **[DECOMP]**
- Playlist items come in event `0xE9` (233), as an array of records: **32 bytes** up to FL 20.99, and
  in newer versions the native size, with variants of **60 (0x3C)** and **80 (0x50)** bytes depending on
  file flags. **[DECOMP]**
- Strings "Audio clips", "Automation clips", clip fades (`TPlaylistFadesTest`), clip merge and chop.
  Time markers in 0x34-byte records in the arrangement (`song+0x21C48`). **[DECOMP]**

### 3.4 Automation **[STR]**
- Automation clips, `TAutomationDelay`, per-clip "Mapping formula", control recording events
  (`0xD8` CtrlRecChan, `0xDF` PatCtrlRecChan).

### Implications for our DAW
- Store tempo as a high-resolution integer (FL uses BPM×1000) or as double. To interoperate with
  FLP, convert `tempo_flp / 1000.0`.
- Use a high internal PPQ (e.g. 960) and rescale when importing FLP (PPQ from the header).
- The import data model is: channels → patterns (notes per channel) → playlist (pattern,
  audio or automation clips) → arrangements.

---

## 4. Plugin hosting

### 4.1 Formats **[STR]**
- **VST2** ("Fruity Wrapper", `TVSTPluginEditor`, `TVSTPresetFile`), **VST3** (`TPluginFormatVST3`,
  `TVST3Plugin`, `TVST3ComponentHandler 1/2/3`, `TVST3ParameterChanges`, `TVST3EventList`, module
  `Vst3ModuleInfo` = reading of `moduleinfo.json`, own extensions `il.VST3Extensions`,
  `VST3_IPSLHostCommands`/`IPSLContextInfo`), **CLAP** (`TPluginFormatCLAP`, `TCLAPPlugin`,
  `clap_entry`, extensions `clap.thread-pool`, `clap.thread-check`, `clap.latency`) and the native
  **FL** format (`TPluginFormatFL`).
- The `clap.thread-pool` extension implies that the host exposes its thread pool to CLAP plugins.
- "Combine VST and VST3 versions of a plugin" (deduplication in the plugin database).

### 4.2 Bridging / separate process **[STR + partial DECOMP]**
- Executables `32bit\ilbridge.exe` and `64bit\ilbridge.exe`. The per-plugin option "Load plugin in separate
  process (bridged)" and "Make bridged" show that the bridge serves both for 32↔64 bits and for
  sandboxing at the same bitness.
- Unit `IPC.Bridge`: `TBridge`, `TBridgeShare` (base), `TBridgeMessageShare`, `TBridgePingShare`
  (heartbeat), `TBridgeDataShare`, `TBridgeReaderThread`, `TBridgeProcessingThread`,
  `TBridgeProcessingJob`, `TWaitingThreadInfo` (dictionary per thread-id for synchronous calls),
  `TBridgeWrapperProcessThread`/`Share` (audio processing), `TSimpleBridgeEventBuffer`.
- **Named shared memory [DECOMP]:** names formatted as `"%s_%d_%s"` and `"%s_%d_data"`
  (prefix, numeric id, suffix). Creation does `OpenFileMappingW` and, if it does not exist,
  `CreateFileMappingW` on the pagefile with the requested size, followed by `MapViewOfFile` (`FUN_00c83820`,
  `FUN_00c915b0`).
- Messages to the GUI: `PostMessage(PM_ProcessMessages)`, "Disconnect calls ExecuteOnGUIThread".

### 4.3 Scan **[STR]**
- Separate process "FL Studio Plugin Scanner" (key `Software\Image-Line\FL Plugin Scanner`),
  `TScanThread`, `TCopyPluginDBThread`, "fast plugin scan", plugin database
  ("Scanning plugin database (generators/effects)").

### 4.4 Per-plugin execution **[STR]**
- Per-plugin flags: "Allow plugin to be multi-threaded by FL Studio", "Allow smart disable" (turns the
  plugin off during silence), "fixed size buffers" (with an extra-latency warning), "Report latency".
- `TPlugMonitor`/`TPlugMeasureEntry`: per-plugin CPU measurement.

### Implications for our DAW
- JUCE already covers VST3 (and VST2 with the legacy SDK). For **CLAP**, use `clap-juce-extensions` or our own CLAP
  host, and plan for exposing `thread-pool`.
- For the bridge, FL's IPC design is: named shared memory for audio and data, message
  channel, ping/heartbeat, and reader thread + processing thread. This can be reproduced with
  shared memory + semaphores/events and one `lynx-bridge` process per plugin or per group.
- Do the scan in a separate process with a persistent database and a "fast scan" mode (only new files).
- Implement "smart disable" (silence detection + bypass), per-plugin CPU measurement and the per-plugin
  fixed buffer option.

---

## 5. .flp file format

This section is the most precise: the event reader was fully decompiled.

### 5.1 Container **[DECOMP]** (`FUN_00f51fd0`, `FUN_00f54900`, `FUN_00f54d70`)

```
Offset  Size Contents
0       4    "FLhd"
4       4    header chunk size (normally 6). The reader skips to 8+size
8       2    format (u16 LE)
10      2    number of channels (u16 LE)
12      2    PPQ (u16 LE); the legacy value 16 is replaced by the default PPQ
--      4    "FLdt"
+4      4    data chunk size
+8      ...  sequence of events until end of file
```

The file must be at least 0x16 (22) bytes. **Format** values seen in the code: `0x00` =
project (song), `0x10` = score, `0x18` = automation, `0x20` = channel preset, `0x30–0x32` = plugin
presets (`0x31`/`0x32` chosen by bit 0x10 of the flags of event 0xD4), `0x40` = mixer track
preset **[DECOMP for the values; names come from the messages "project/preset/score/automation"]**. A
format-0x20 target accepts a 0x30 file. An incompatible format produces *"Project format is not compatible with
target, aborting"*.

### 5.2 Event encoding **[DECOMP]** (`FUN_00a9e7a0` and readers `FUN_00a9e430/4d0/570/610`)

1. Reads **1 byte** = event ID.
2. **New:** if the ID is **`0xAC` (172)**, reads **4 more bytes (u32 LE)** which are the **extended ID**.
3. The size class is derived from the ID:

| ID (1 byte) | Extended ID (high bits) | Payload |
|---|---|---|
| `0x00–0x3F` | `0x0xxxxxxx` | 1 byte (u8) |
| `0x40–0x7F` | `0x4xxxxxxx` | 2 bytes (u16 LE) |
| `0x80–0xBF` | `0x8xxxxxxx` | 4 bytes (u32 LE; value treated as signed int32) |
| `0xC0–0xFF` | `0xCxxxxxxx` | variable size: varint + bytes |

4. **Size varint:** 7-bit bytes in little-endian (bit 7 = continue), in the style of unsigned LEB128.
5. Payloads < 1 MiB go into an internal 1 MiB buffer; larger ones are allocated on the heap.
6. An **optional filter** decides whether the variable payload is read or skipped (seek). On project load the
   filter skips **only event `0xD5`** (plugin state data), which is read later on demand
   (`FUN_00f45ad0`).
7. There is an optional post-processing mode for the bytes read (reader flag `+0x11`, `FUN_00a9ece0`):
   decryption or checksum **[HYP]**.
8. **Text:** depends on the file version. If `version > 11.5.4`, strings are **UTF-16LE**; otherwise,
   ANSI (reader flag `+0x10`) (`FUN_00a9e8e0`).

Extended IDs seen in the dispatcher: `0xC0000100`, `0xC0000102` (variable text, e.g. a list of strings
parsed line by line) and ID `0x100` (outside the 1-byte range, therefore only reachable via `0xAC`).

### 5.3 Version **[DECOMP]**
Event **`0xC7` (199)** is the version string (e.g. `"25.1.2.4567"`). It is converted to an integer
**`major*1,000,000 + minor*1,000 + patch`** and used in dozens of compatibility *gates*:
8.1.0 (old records), 11.5.4 (UTF-16), 12.9.0, 20.99.1 (size of note and playlist records),
24.2.99 etc. A file newer than the program produces a warning.

### 5.4 Table of events handled by the project loader

The dispatcher `FUN_00f46490` (3,753 decompiled lines) handles **181 distinct IDs**. The names below
follow the community's public nomenclature (FLP SDK/PyFLP). The "Evidence" column indicates what we confirmed
by decompilation.

| ID | Class | Name (community) | Evidence in this binary |
|---|---|---|---|
| 0x00 | u8 | ChanEnabled | handled |
| 0x09 | u8 | LoopActive | calls loop setter |
| 0x0F | u8 | (pattern/channel flag) | handled |
| 0x15 (21) | u8 | ChanType (0…5) | **[DECOMP]** range 0–5 validated ("ABORT FLP_ChanType … out of range") |
| 0x16 (22) | u8 | TargetFXTrack | **[DECOMP]** "FLP_TargetFXTrack target track out of range" |
| 0x1B (27) | u8 | FX track flags | **[DECOMP]** bits 0x1/0x2/0x4/0x8/0x10 → track flags; bit 0x10 disables the slots |
| 0x32 (50) | u8 | (channel property) | handled |
| 0x40 (64) | u16 | NewChan (index of current channel) | **[DECOMP]** creates or selects the channel |
| 0x41 (65) | u16 | NewPat (index of current pattern) | **[DECOMP]** limited to 999 |
| 0x42 (66) | u16 | Tempo (integer part, legacy) | **[DECOMP]** tempo = v×1000 + old fraction |
| 0x44 (68) | u16 | (16-bit mask per pattern) | **[DECOMP]** 16 flags |
| 0x5D (93) | u16 | TempoFine | handled |
| 0x63 (99) | u16 | Current arrangement | **[DECOMP]** |
| 0x8A (138) | u32 | (two 16-bit fields) | **[DECOMP]** |
| 0x93 (147) | u32 | FXOutChanNum | **[DECOMP]** index correction in versions < 3.6.3 |
| 0x9C (156) | u32 | FineTempo (BPM×1000) | **[DECOMP]** |
| 0x9E (158) | u32 | (pattern field, +0x18) | **[DECOMP]** |
| 0xC4 (196) | text | SampleFileName | **[DECOMP]** `/` → `\` |
| 0xC7 (199) | text | Version | **[DECOMP]** |
| 0xD4 (212) | data | NewPlugin (0x34-byte header; flags at +0x10, bit 0x10 = generator/effect) | **[DECOMP]** |
| 0xD5 (213) | data | PluginParams (opaque plugin state) | **[DECOMP]** skipped and read on demand |
| 0xE0 (224) | data | PatNoteRecChan = **pattern notes** (array of note records) | **[DECOMP]** |
| 0xE9 (233) | data | **Playlist items** (array) | **[DECOMP]** |
| 0xEB (235) | data | Mixer routing | **[STR/HYP]** |
| 0xEC (236) | data | Mixer track parameters (includes a field obfuscated by XOR with a constant) | **[DECOMP]** |
| 0xED (237) | data | (16-byte record) | handled |
| 0xF0 (240) | text | (global string list) | **[DECOMP]** |
| 0xF6 (246) | text | (channel name/property) | handled |
| 0xE2/0xE3 | data | (range handled together, `id-0xE2`) | handled |

Full list of the 1-byte IDs seen in the dispatcher (hex):
`00–06, 09–24, 26–34, 40–50, 53–59, 5B–5F, 61–65, 67, 68, 80–84, 87, 8A–96, 98–9E, A0–A2, A4–AB, C0–CF,
D1, D2, D4, D5, D7–DD, DF, E0, E1, E4–E7, E9–F3, F5–FF`, plus the extended `0x100` and
`0xC0000100`/`0xC0000102`.

### 5.5 Versioned record arrays **[DECOMP]** (`FUN_00f558e0`)
Array events (notes, playlist, controls) are read as a blob and reinterpreted with a **record size
that depends on the version**:

| Type | < 8.1.0 | 8.1.0 … < 20.99.1 | ≥ 20.99.1 |
|---|---|---|---|
| 0 (control/automation) | legacy converter | 12 bytes | current native size |
| 1 (notes) | legacy converter | 24 bytes | current native size |
| 2 (playlist) | legacy converter | 32 bytes | 60 or 80 bytes, or the native size, depending on flags |

That is: to import FLP, the **record size must be derived from the event size ÷ number of
items**, or from the version, and not hard-coded.

### Implications for our DAW (FLP importer)
- Implement the event reader with **support for the `0xAC` escape + 32-bit ID**, which probably
  breaks older community parsers.
- Use LEB128 varint for the size, UTF-16LE strings when `version > 11.5.4`, and the PPQ from the header.
- Note and playlist record size per version (24/32 bytes old; 60/80+ in the new ones).
- Skip and store `0xD5` (plugin state) as an opaque blob. It can only be reused when the same
  VST/VST3/CLAP plugin exists on our side: the wrapper payload contains the plugin chunk, but the internal
  format of the wrapper still needs study.
- Tempo = `u32 / 1000.0` BPM (event 0x9C).

---

## 6. Open questions

1. **PDC algorithm:** where are the delay lines (per track, per send, per slot) and how does "Auto PDC"
   compute the delay? The computation of level `+0xfc` and of the accumulated latency still needs to be decompiled.
2. **Internal track processing** (`object +0x1c0`, virtual `Process`): pre/post-fader order of the 10
   slots, sends, panning law, mute/solo, smart disable. The vtable needs to be resolved.
3. **Track 501 and the 500 limit:** confirm the role (Current/preview) and whether the number of inserts is
   configurable.
4. **Exact format of the note and playlist records in versions ≥ 20.99** (60/80 bytes): map the new
   fields (probably colour, slide, porta, clip flags, audio offsets).
5. **Routing (0xEB) and track parameter (0xEC) events:** full layout, including the field
   obfuscated by XOR.
6. **0xD5 / wrapper payload:** how the VST/VST3/CLAP chunk is encapsulated.
7. **1/8-sample mode** in the next-tick computation (`<<3`): is it sub-sample timing?
8. **Thread pool factory** (`DAT_015f4648`): does it come from the `FL64.exe` executable? What is the
   affinity/priority and MMCSS policy of the workers?
9. **Bridge IPC:** message protocol, audio synchronisation (events/semaphores? spin?) and timeouts.
10. **WASAPI:** where is it implemented (in `FL64.exe`? in the "FL Studio ASIO" driver?). It is not in this DLL.
11. **Event reader post-processing** (flag `+0x11`, `FUN_00a9ece0`): checksum, encryption or
    integrity hash?

---

## Appendix A — key functions (traceability)

| Address | Inferred role | Confidence |
|---|---|---|
| `FUN_00c56ae0` | ASIO bufferSwitchTimeInfo (3 modes) | DECOMP |
| `FUN_00c55740` | Execute of `TMixerThread` (ASIO) | DECOMP |
| `FUN_00c57a00` | ASIO buffer creation, track I/O mapping, latency log | DECOMP |
| `FUN_012aef00` | DirectSound mixing thread | DECOMP |
| `FUN_0125a760` | Render by external pull | DECOMP |
| `FUN_00d0ece0` | Tick-sliced render loop | DECOMP |
| `FUN_00d0e840` | Advance of 1 tick (song position / countdown) | DECOMP |
| `FUN_00d0eb10` | Sub-block wrapper + tail detector + master pointer advance | DECOMP |
| `FUN_00d0d4c0` | Processes sub-block: transport, fade, generators, mixer | DECOMP |
| `FUN_00cf3790` | Rebuilds the mixer threading map (layers/units) | DECOMP |
| `FUN_00cf42f0` | Runs the mixer (serial or by layers) | DECOMP |
| `FUN_00cf4250` | Dispatches the next layer | DECOMP |
| `FUN_011fec40` / `FUN_011febf0` | Worker tasks (mixer / generators), atomic counter | DECOMP |
| `FUN_012c6f30` | Init: thread pool + per-worker contexts | DECOMP |
| `FUN_00666a60` / `FUN_00666b40` | Task group (QuickThreads) | DECOMP |
| `FUN_00f51fd0` | Opens the FLP and validates the FLhd/FLdt header | DECOMP |
| `FUN_00f46490` | FLP event dispatcher (181 IDs) | DECOMP |
| `FUN_00a9e7a0` | Event reader (ID + 0xAC escape + size class) | DECOMP |
| `FUN_00a9e610` | Variable payload reader (varint) | DECOMP |
| `FUN_00f558e0` | Versioned record array reader | DECOMP |
| `FUN_00c83820` | Named shared memory (bridge) | DECOMP |
