# Behavioural analysis of FL Studio 2026: round 14 (REA) — Patcher, Gross Beat and the effects/controllers the import still discards

> **Nature of this document.** Continuation of `REA_rodada7.md` (state of the native mixer effects) and
> `REA_rodada12.md`. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode
> was copied: what is here are descriptions, data layouts and formulas written in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence.

- **Targets** (local copies in `tmp/r14/bin/`, originals in `Plugins/Fruity/Effects/<name>/` and
  `Plugins/Fruity/Generators/<name>/`):

  | Plugin | DLL (SHA-256) | Language | State entry point |
  |---|---|---|---|
  | Patcher | `cb4212c4…c64b` | Delphi | write `FUN_00955150`, read `FUN_009513d0`, node `FUN_00952a70` |
  | Gross Beat | `9025d938…8f1a` | Delphi (`TTestPlug`, VMT `0x83f0f0`) | `SaveRestoreState` `FUN_0083f890` (VMT+3) |
  | Pitcher | `965db573…a73d` | C++ | write `FUN_18001d6e0`, read `FUN_18001d790` |
  | Effector | `2f538ec9…72e5` (+ `engine_x64.dll`) | Delphi + C++ engine | DFM + data only |
  | Frequency Splitter | `e2505dc6…e27c` (+ `engine_x64.dll`) | Delphi + C++ engine | DFM + data only |
  | Vocodex | `43d10692…4bb0` | Delphi | DFM only |
  | Fruity Peak / Envelope / Formula / X-Y Controller | `e73ec8da…2d27` / `e60f7462…c763` / `302779c8…76a7` / `c78e1ff2…c19b` | Delphi | DFM + data |

- **Tools:** Ghidra 12.1.4 *headless* in our own projects (`/tmp/r14gh_<dll>`), scripts in
  `tmp/r14/gh/scripts/` (`Decomp`, `DecompGrep`, `Xrefs`, `ScalarFind` — the latter finds the functions that use
  the *chunk* constants). DFMs read by our own `TPF0` parser (`tmp/r14/tools/dfm.py`; the `Tag` of each
  control is the parameter index). Delphi VMTs listed by `tmp/r14/tools/vmts.py` (class name at
  VMT−0x88).
- **Collection:** `tmp/r14/collect14.py` → `tmp/r14/blobs14.pkl` = `(MIX, CH, FST)`: mixer slots
  (`0xC9` → `0xD5` → `0x62`, after the 1st `0xEC`), channel generators and `.fst` presets. Support
  decoders: `tmp/r14/pdec.py`, `graph.py`, `flat2.py` (Patcher), `gbp.py` (Gross Beat).
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

### Count in the data (priority) **[DATA]**

Mixer effect slots in `Data/Demo projects/**.flp` (in parentheses: all `.flp` files in `Data`,
including `Templates/` and `System/`).

| Plugin | Slots in demos | (all) | Demo files | `.fst` | Size of `0xD5` |
|---|---|---|---|---|---|
| Pitcher | 39 | (61) | 7 | 4 | 160 B (`SSLF` + `1RTP` + floats) |
| Effector | 26 | (27) | 11 | 4 | 316 B (`SSLF` + `10XF` + floats) |
| Gross Beat | 30 | (32) | 15 | 13 | 7–44 KB (72 envelopes) |
| Patcher | 24 + 3 channels | (30 + 4) | 8 | **329** | 2 KB…450 KB (chunks) |
| Fruity Peak Controller | 20 | (33) | 13 | 7 | 42 B |
| Frequency Splitter | 17 | (17) | 3 | 13 | 113 B |
| Vocodex | 10 | (12) | 8 | 25 | ~1.2 KB |
| Control Surface / X-Y / Formula Controller | 3 / 1 / 2 | (17 / 19 / 2) | — | — | — |
| Fruity Envelope Controller | — | — | 4 channels | 26 | ~2–6 KB |

In addition, **75 controller links** (`0xE3` events whose source is a plugin's output, §7.1) in 18
demos — all of them currently discarded by the import.

---

## 1. Patcher **[DECOMP+DATA]**

Patcher is a graph of plugins inside a slot (effect) or a channel (generator). Its `0xD5` is a
**sequence of chunks with a 64-bit size**, and each contained plugin stores **the same state blob it
would have as `0xD5` in a mixer slot** — that is, the decoders from round 7 (PEQ2, Limiter, Reeverb 2,
Delay 3…) work unchanged.

### 1.1 Envelope and versions **[DECOMP `FUN_007c97a0`/`FUN_007c9a50`/`FUN_007c9a70` + DATA]**

```
u32 version                       4 (current, UTF-16 strings) | 3 (ANSI strings) | 1 ("Patcherize" templates)
repeat until the end:
  u32 id  ·  u64 size  ·  size bytes of data
```

- **id = 0 is a group**: its content is, again, a sequence of `id/size/data` chunks. The writer
  opens the group by writing a header with size 0 and, on closing, goes back and writes the real size
  (`FUN_007c9a70`). Each node, pin, connection and position is a group.
- Chunk `0x3E9` (1001) with size 0 marks the **end of the state**.
- Strings: in version 4 they are UTF-16LE without terminator; in version 3 they are **8-bit ANSI** (read as UTF-16
  they turn into CJK characters — that is what showed up in the 13 old presets). Detect by version. **[DATA]**
- In the 363 blobs (34 from `.flp` + 329 `.fst`) the recursive parser consumes everything with nothing left over **[DATA]**.

### 1.2 Top-level chunks **[DECOMP `FUN_00955150` (write), `FUN_009513d0` (read)]**

| id | Size | Content |
|---|---|---|
| `1000` (0x3E8) | 64 | header: `i64` next free id (node/port ids are allocated from here), `i32` global counter, `u32` flags (bit 0, bit 1: editor options). Ignorable |
| `1002` (0x3EA) | 44 | editor state (number of visible pins, option bits, positions of the I/O nodes). Ignorable |
| `1003` (0x3EB) | 12 | v3 only: `i32` type (0…2) + position of an I/O node (old). Ignorable |
| `1004` (0x3EC) | ~324 | control surface layout (`FUN_008256d0`). Ignorable |
| group `5000…5001` | var | **a node (plugin)**, §1.3 |
| `1100` (0x44C) | 32 | **exposed Patcher parameter**: `u64` control port id + `i32` Patcher parameter index. One per parameter the user published (this is how "Patcher" automation reaches an internal parameter) |
| group `1400…1401` | var | **Patcher I/O pin**, §1.4 |
| group `1300…1301` | var | **connection**, §1.5 |
| `1500` (0x5DC) | 40 | editor view (`f32` zoom = 1.0, scroll). Ignorable |
| group `1600` | 24 | position of a node: `u64` node id + `f32 x`, `f32 y`. First the two I/O nodes, then one per plugin. Ignorable |
| `1001` (0x3E9) | 0 | end |

The reader also accepts `0x47E` and `0x6A5` (not seen in the data).

### 1.3 Node (group started by `5000`) **[DECOMP `FUN_00955150` node-loop lines, `FUN_00952a70`]**

| id | Size | Content |
|---|---|---|
| `5000` (0x1388) | 40 | header: `u32` **plugin type** (2 = effect, 3 = generator — 8,665 / 687 nodes in the data), `u64` **node id**, `u32` flags (bit 0 seen in 477 nodes; bit 2 = disabled, never seen), `u32` **number of input ports**, `u32` **number of output ports**, then 13 B of window/UI |
| `5004` (0x138C) | var | plugin identifier: in v4 the name (`"Fruity Limiter"`); in v3 the DLL path relative to the FL folder (`Plugins\Fruity\Effects\…\….dll`). Only written for some types |
| `5008` (0x1390) | var | **internal plugin name** (the same as in `0xC9`) — use this to choose the decoder. Comes with varying case (`"Fruity parametric EQ 2"`): compare case-insensitively |
| `5010` (0x1392) | var | displayed name (user label, e.g. `"Limiter"`) |
| `5005` (0x138D) | var | **plugin state** — byte for byte the same format as that plugin's `0xD5` in the mixer (§1.7). For VSTs the name is `"Fruity Wrapper"` and the blob is the wrapper's (`REA_rodada5.md`) |
| `5007` (0x138F) | 40 | node options: `f32` (always 1.0 in the data), `i32` preset index (−1 = none), `u32` flags, 3 × `i32` (default `0, 0x7FFFFFFF, 60`: note range and root note **[HYP]**), 2 × `u32` of UI |
| `5006` (0x138E) | var | name of the loaded preset (only when the index ≥ 0; 16 cases, e.g. `"Mastering\20Hz + 18k…"`) |
| `5009` (0x1391) | 28 | **a port** (repeated): first all inputs, then all outputs, §1.3.1 |
| `0x3F2`/`0x3F6` | var / 84 | optional block of an auxiliary object of the node (not seen in the data) |
| `5001` (0x1389) | 0 | end of node |

#### 1.3.1 Port (`5009`, 28 B) **[DECOMP + DATA]**

| Off | Type | Field |
|---|---|---|
| 0 | u32 | **class**: 0 = audio, 1 = MIDI/notes, 2 = control (parameter) |
| 4 | u32 | **index**: audio = bus (0 = main, 1… = sidechain on the input / extra outputs on the output); input control = **plugin parameter index**; output control = `0x10000000 + control output number` |
| 8 | u64 | **port id** (this is what the connections reference) |
| 16 | u32 | flags: bit 0 = **is input**; bit 1 = visible/in use; bit 2 = another option; bit 3 = "fixed" parameter port |
| 20 | 8 | zero |

Patterns in the data: `(0, …, flags 3)` audio input, `(0, …, 2)` audio output, `(0, i ≥ 1, 0)` hidden extra
outputs (the Fruity effects have 1 input + **5 outputs**: main + 4 auxiliary), `(2, p, 11)` parameter p
exposed as an input, `(2, 0x1000000k, 10)` control output k of a controller **[DATA]**.

### 1.4 Patcher I/O pins (group `1400`) **[DECOMP `FUN_00952560` + DATA]**

`1400` (28 B): `u64` port id · `u32` class (0 audio, 1 MIDI) · `u32` flags · `u8` channels (2 = stereo) ·
`i32` bus. Followed by `1402` with the name and `1401` (end).

- **flags bit 0 = 1** → pin that **brings a signal in from outside** (becomes an *output* inside the graph): `"Audio input"`
  (bus 0, the slot input), `"7 Sidechain"`/`"Master"` (bus ≥ 1 = sidechain coming from that
  track), `"Notes"` (the channel's notes, in the generator Patcher).
- **flags bit 0 = 0** → pin that **takes a signal outside**: `"Audio output"` (bus 0 = slot output),
  `"Insert N"` (bus ≥ 1 = Patcher auxiliary outputs to other tracks), `"MIDI port N"`.
- bit 1 = main bus (0); bit 2 = display option.

### 1.5 Connection (group `1300`, 32 B) **[DECOMP `FUN_0079ca50`, `FUN_0079b730`, `FUN_00952440`, `FUN_0079b790` + DATA]**

| Off | Type | Field |
|---|---|---|
| 0 | u64 | **destination port** (a node input or a Patcher output pin) |
| 8 | u64 | **source port** (a node output or a Patcher input pin) |
| 16 | f32 | **cable gain** (default 1.0 when the link is created) |
| 20 | u32 | flags: bit 0 = cable disconnected/muted **[HYP]**; bit 1 = the next byte is valid |
| 24 | u8 | cable class (0 audio, 1 MIDI, 2 control), same as the ports' |
| 25 | 7 | zero |

- The *destination, source* order comes from the link constructor, which always puts first the end that is not an output
  (`FUN_0079b730`), and it was checked on the 24,361 connections: **100 %** have input/output-pin in the 1st position and
  output/input-pin in the 2nd **[DECOMP+DATA]**.
- Class by count (by destination port): control 18,405, audio 5,026, MIDI 930 **[DATA]**.
- **Gain:** the field starts at 1.0 (`FUN_0079ef40`); in the data 23,770 cables = 1.0 and the rest between 0.15 and 2.0,
  always on audio cables that sum branches (e.g. "Twii": Harmor direct → output × 1.32, in parallel with the
  Reeverb→EQ chain × 1.0; "Bien Duro" ins. 57: splitter band → output × 0.875, distortion → output × 0.8). The use
  of the field as a linear amplitude multiplier was not located in the engine **[strong HYP]**.
- Several connections arriving at the same input are **summed** (that is how Patcher does a *merge*) **[strong HYP; universal
  pattern in the parallel graphs]**.

### 1.6 Graph reconstruction (procedure) **[DECOMP+DATA]**

1. Read all groups; build `port → (node, class, index, input?)` from the `5009`s and
   `port → (pin, input-from-outside?, bus, name)` from the `1400`s.
2. For each `1300` of **class 0 (audio)**: edge `node(source) --gain--> node(destination)`, keeping the
   bus of both ends. Pin with bit 0 = virtual node `IN` (bus 0) or `SC<n>`; without bit 0 = `OUT`
   (bus 0) or `AUX<n>`.
3. **Class 2 (control)** edges: `control output k of node A → parameter p of node B` (§7.2 for mapping
   to automation).
4. Class 1 (MIDI) edges: only matter for the generator Patcher (`Notes` pin → generators).
5. Consider only the nodes on an `IN → … → OUT` path via bus 0; nodes outside it are
   controllers, analysers (Peak Controller fed by an EQ) or notepad.

### 1.7 State of internal plugins = mixer state **[DATA]**

Comparison of size + 1st word of the `5005`s against the `0xD5`s of mixer slots:

| Plugin | Inside Patcher | In the mixer |
|---|---|---|
| Fruity Parametric EQ 2 | 354 B (v7/v8), 309 B (v3) | same |
| Fruity Limiter | 169 B (v7), 168 B (v6) | same |
| Fruity Reeverb 2 | 66 B (`0x2711`), 58 B | same |
| Fruity Delay 3 | 108 B (v1) | same |
| Stereo Shaper / PanOMatic | 36 B / 24 B | same |
| Maximus | 1375/1351/1342 B | same |
| Gross Beat | `0x00080003` … | same |
| Fruity Wrapper (VST) | 396–400 B (v8) | same |

### 1.8 Topologies found **[DATA `tmp/r14/flat2.py`]**

| Origin | Serial | Parallel | No IN→OUT path |
|---|---|---|---|
| Mixer slots (all `.flp`) | 4 | 26 | — |
| Channels (generator Patcher) | — | — | 4 (internal generators → output) |
| `.fst` | 22 | 123 | 184 |

Patterns in the demos:

- **Serial** (4): "Bien Duro" ins. 9 (Flangus → Distructor), ins. 40 (Squeeze → PEQ2 → Transient Processor →
  Fast Dist → Limiter), ins. 55 (PEQ2 → Limiter); "Bombs Away" ins. 70 (Stereo Shaper → PEQ2 → WaveShaper →
  PEQ2). Only ins. 55 has all plugins already decoded by LYNX.
- **M/S with Stereo Shaper** ("Africa's Calling" 31, "Inner Days" 21, "Exoplanet" 22/101, "With You" 1):
  `IN → Stereo Shaper`, whose **output 0 and output 1** (different buses!) go to two PEQ2s (sometimes +
  Limiter/Reeverb 2) that sum into `OUT`. Or two Stereo Shapers in parallel from `IN`.
- **Multiband with Frequency Splitter** ("With You" 18/21, "Bien Duro" 9/18/57): splitter outputs 1/2/3
  (the bands) → per-band effects → sum; sometimes a band goes to the **sidechain** (bus 1) of a
  Limiter.
- **Templates** (`Funk/Jazz/Metal/Rock` ins. 125): `IN → PanOMatic → Stereo Shaper → Limiter → Stereo Shaper
  → OUT` in parallel with `IN → PanOMatic → OUT` (dry/wet mix).
- "Stroll Up" ins. 1: 69 nodes (4 bands × flanger/chorus/flangus + 28 PanOMatics); not worth flattening.

Conclusion: **serial flattening covers little**; the most rewarding case is *"N parallel branches, each one
a simple chain, summed at the output"* — which in LYNX becomes a *submix* (§Recommendations 1).

---

## 2. Gross Beat **[DECOMP+STR+DATA]**

Gross Beat has **36 "time travel" slots** and **36 volume slots**, each with a one-bar envelope
drawn in points; a parameter selects the active slot of each type. Volume is a tempo-synced
*gate*/tremolo — exactly what can be approximated in LYNX.

### 2.1 Layout of `0xD5` (`FUN_0083f890`) **[DECOMP+DATA]**

| Off | Size | Content |
|---|---|---|
| 0 | 4 | version: the writer writes `0x00090003`; the reader uses the high half when the value ≥ 0x10000 (demos: `0x00080003` → **v8**; old `.fst` files write 4 or 8 directly). Rejects ≥ 0x90004 |
| 4 | 36 (v < 2: 32) | **9 × `i32` = parameters 0…8** (§2.2) |
| 40 | 9 (v < 3: 8; v < 5: −1) | option bytes (MIDI trigger, *click reduction*, attack compensation, DC, HQ… — the DFM's `OptionsMenu`) **[STR; order HYP]** |
| … | 36 × slot | **time travel slots** (0…35) |
| … | 36 × slot | **volume slots** (0…35) |
| end | 1 (v ≥ 9 only) | extra flag |

**Slot:**

| Field | Size | Content |
|---|---|---|
| name | 1 + n | ANSI *shortstring* (`"Empty"`, `"1/2 Bt Gate"`, `"Sidechain"`…). In v < 4, `"Slot N"` names are cleared |
| sync | 15 | `i32` **trigger sync**, `i32` **position sync** (combo indices: 0 Off, 1 ¼ beat, 2 ½ beat, 3 1 beat, 4 2 beats, **5 4 beats** — 5 in every slot in the data), 7 slot option bytes (link, *key hold*, *one-shot*…) **[STR; mapping strong HYP]** |
| envelope | 12 + 24·n + 20 | envelope object of the `TCustomMEnvelope` family (§2.3) |

Validation: the parser in `tmp/r14/gbp.py` walks the 72 slots and ends **exactly** at the end of the blob
(38,283 B in `NewStuff.flp`) **[DATA]**.

### 2.2 Parameters (index = `Tag` of the `TSynthEditorForm` DFM; conversions from `FUN_00840ed0`, `FUN_00841de0`, `FUN_00841fb0`) **[DECOMP+STR]**

| i | Name | Range (default) | Conversion |
|---|---|---|---|
| 0 | Time slot | 0…35 | active time travel slot |
| 1 | **Volume slot** | 0…35 | **active volume slot** |
| 2 | Time mix | 0…256 (256) | `/256` (0 = original signal without time travel) |
| 3 | **Volume mix** | 0…256 (256) | `/256` (mix between gain 1 and the envelope) |
| 4 | Volume smoothing attack | 0…10000 (1765) | `t_ms = 501^(v/10000) − 1` (0…500 ms; default ≈ 2 ms) |
| 5 | Volume smoothing release | 0…10000 (5000) | `t_ms = 2·(501^(v/10000) − 1)` (0…1000 ms; default ≈ 43 ms) |
| 6 | Volume envelope tension | −256…256 (0) | `/256`: global curvature applied to the volume envelope |
| 7 | Hold (bar repeat) | 0/1 | freezes/repeats the bar |
| 8 | (no control in the DFM) | 0…65536 | `v/65536 − 1`, displayed ×8 (time offset/scale) **[HYP]** |

In the 30 demo slots the *volume slot* (`p1`) is 0 ("Empty") in 15, and the rest is spread among 3, 5, 9, 22,
24–26 and **28 ("Sidechain")**; the *volume mix* is 256 in 22 and 0 in 4 **[DATA]**.

### 2.3 Envelope (`TCustomMEnvelope`, same format as Sytrus/FPC/clips — rounds 2, 9 and 11) **[DATA + DECOMP from earlier rounds]**

```
i32 type    (3 = "map": drawn x→y curve; 1 = ADSR envelope; 2 = LFO)
i32 version (3)
i32 n       (number of points)
n × { f64 Δx · f64 y (0…1) · f32 tension (−1…1) · u8 curve type · u8 · u8 · i8 direction }
5 × i32    flags, and −1, −1, −1, −1 (sustain/loop points: none)
```

- **Δx in beats (quarter notes)**; in **all** 72 slots the sum of the Δx is **4.0 = one bar** in 4/4.
  Checked against the names: "2 Beat Gate" alternates 1/0 every 1.0; "1 Beat Gate" every 0.5; "1/2 Bt Gate" every
  0.25 **[DATA]**.
- **Curve type of the segment that ends at the point** (byte 20): 0 single curve (with tension; tension 0 = straight line),
  **2 hold** (step: keeps the previous value until the point — this is the type of the *gates*), 5 pulse and 6 wave (a single
  segment that oscillates; e.g. "1/3 Bt Gate" = 2 points, *pulse* segment with tension 0.54; "Tremelo Slw/Fst" and
  "AM" = *wave*), 1/3/4/7… rare. Formulas in `REA_rodada2.md` §3.3. Byte 23 is the direction cache (sign of the
  tension; 2 = straight line) — recompute.
- **Volume:** `y` = linear gain (1 = 0 dB, 0 = mute) **[strong HYP]**. **Time travel:** `y = 1` = play
  normally; smaller values = read from a delayed point in the buffer (`1 − y` proportional to the delay) — not
  approximable in LYNX **[HYP]**.
- Factory volume slots: Empty, 2/1/½/⅓/¼/⅙/⅛ Beat Gate, Trance Gt 1–4, Saw Gate 1–2, 1st Step, 1st Stp
  Fade, 2 Step Gate, Off Beat, ¼ Dynamic, ¼ Swing, ¼ Gate Out/In, Spd Up / Slw Dn Gate, End Fade, End Gate,
  Tremelo Slw/Fst, Sidechain, Drum Loop (363 points), Copter, AM, Fade In, Fade Out, Fade Out In, Mute.

### 2.4 Sync **[STR+HYP]**

With *position sync* = 4 beats (the default in 100 % of the slots in the data) the envelope phase is
`(song position in beats) mod 4`. *Trigger sync* only matters when the slot is switched (by automation of
parameter 1 or by MIDI): the switch waits for the next multiple. Final gain =
`(1 − mix) + mix · env(phase)`, smoothed by an attack/release follower with the times from §2.2.

---

## 3. Pitcher **[DECOMP `FUN_18001d6e0`/`FUN_18001d790`/`FUN_18001c240` + DATA]**

Pitch correction/harmoniser. `0xD5` = `"SSLF"` + `u32 0x98` + 152 B:

| Off | Content |
|---|---|
| 0 | tag `"1RTP"` (`0x50545231`) — or `"1DTP"` in the demo version (ignored by the normal reader) |
| 4 | **33 × `f32` = parameters 0…32** (normalised host value) |
| 136 | 12 B of internal state (not a parameter) |
| 148 | `f32` = parameter 33 |

Known names (`FUN_18001c240`): 0 **Correction Speed**, 1 Stereo, 2 **Gender**, 3 Fine Tune, 4 **Bypass
All**, 5…16 *Key C…B Bypass* (scale), 17…20 *Voice 1–4 Pan*, 23 *Note A Freq*, 24 *Pitch Reference*; 21, 22,
25…33 unnamed in the registry (modes/harmonisation; in the data 28 = 1/3/6 and 32 = 2, integer values) **[DECOMP+DATA]**.
In the 39 demo slots: speed 0.6…1.0, gender 0.5 (neutral) almost always **[DATA]**.

Approximation: **none** with the internal effects — pitch correction has no equivalent; pass through
(log it). If `Gender ≠ 0.5` or there is harmonisation (voices) the sound changes considerably; just warn.

## 4. Effector **[STR+DATA]**

12-algorithm multi-effect with X/Y modulation. `0xD5` = `"SSLF"` + `u32 0x134` + `"10XF"` + 64 `f32` (zeroed
in all 31 blobs; reserved **[HYP]**) + **12 `f32` = parameters 0…11** (normalised 0…1; the DFM has
range 0…256):

| i | Parameter | In the data |
|---|---|---|
| 0 | **Effect kind** | `round(v·11)`: 0 Distort, 1 Lo-fi, 2 Flanger, 3 Phaser, 4 Filter, 5 Delay, 6 Reverb, 7 Stereo, 8 Trans, 9 Grain, 10 Vox, 11 Ring |
| 1 | FX bypass | 0 |
| 2 | **Wet mix** | 0.1…1 |
| 3 | **Param X** | — |
| 4 | **Param Y** | — |
| 5 | Mod rate | `round(v·8)` → ⅛, ¼, ⅜, ½, ¾, 1, 2, 4, 8 (/1 = bars) |
| 6 | Tempo reduce | 0 |
| 7 / 8 | Mod amount X / Y | 0.5 = no modulation |
| 9 | Mod shape | Off, On, Sine, Saw, Square **[STR; order HYP]** |
| 10 | Input level | 1.0 (0.58 in one case) |
| 11 | Output gain | 0.5 = 0 dB **[HYP]** |

In the 26 demo slots: Reverb 11, Trans 8, Vox 4, Lo-fi 2, Distort 1 **[DATA]**.

## 5. Frequency Splitter **[STR+DATA]**

2/3-band splitter with extra outputs (sends to mixer tracks / outputs 1–3 inside Patcher) and a
"main output" with per-band level. `0xD5` = 28 × `i32` + 1 byte (113 B):

| Word | Tag | Content |
|---|---|---|
| 0 | — | version (4) |
| 1 | 0 | **Low/Mid freq** 0…65536 (19800) |
| 2 | 1 | **Mid/High freq** 0…65536 (48065) |
| 3 | 2 | Low/Mid slope (combo 0…7) |
| 4 | 3 | Mid/High slope (combo 0…7) |
| 5 | 4 | Linear phase (0/1) |
| 6 | 5 | **Number of bands** (0 = 2 bands, 1 = 3 bands) **[DATA: presets "Dual band…" = 0, "Steep tri band" = 1]** |
| 7 | 6 | Linear phase quality |
| 8 | 7 | Linear phase mod speed |
| 9–11 | 8–10 | Low/Mid/High send level, hundredths of a dB (−1800…1800) |
| 12–14 | 11–13 | **level at the main output** Low/Mid/High, hundredths of a dB (−1800 = mute) |
| 15–27 | — | mutes/solos, destination tracks and UI **[HYP]** |

**Frequency** (fit to the presets "Isolate below 200Hz" = 19792, "Dual band 500Hz" = 29365, "Isolate over
4kHz" = 51069, "Steep 30Hz Cut" = 0): `f = 30 Hz · (16000/30)^(v/65536)`, error < 1 % **[DATA]**.

## 6. Vocodex **[STR]**

Vocoder (modulator = input; carrier = sidechain or internal synthesiser). The blob starts like Gross Beat's
(`0x00080003`) and stores 20 parameters (DFM: Soundgoodizing, Mod/Car pass-through level and filters, *added
noise*, band hold/attack/release, band width and *flatness*, modulator pitch/unison…) and the
bands. **No useful approximation** in LYNX (it needs a carrier); import as bypass with a warning.

---

## 7. Internal controllers (Peak, Envelope, Formula, X-Y) and the links **[DATA+STR]**

### 7.1 Where the link lives in the `.flp` **[DATA]**

There is no new event: the controller link uses the **same `0xE3`** as automation clips (20 B records,
flags `8`, last field `0x1D5` as in clips). The difference is in the **1st field (source)**: instead of
`channel << 16`, it is a **ControlId of the controller output**, with the low part ≥ `0x8000`:

| Source | Meaning | Example |
|---|---|---|
| `channel<<16 \| 0x8000+k` | output k of a **controller generator** in the channel (Envelope Controller, Keyboard Controller) | `0x00018000` = channel 1, output 0 ("Basic Sidechain") |
| `0x70000000 + track<<22 + slot<<16 + 0x8000+k` | output k of a **controller effect** in a mixer slot (Peak, X-Y, Formula, Control Surface) | `0x70458001` = track 1, slot 5, output 1 (the Peak Controller of "With You") |
| `0x2…/0x3…` + same calculation | old form (add `0x50000000`, as `normalizeControlId` already does) | `0x39008000` = track 100, slot 0 (Control Surface of "Future House") |

The 2nd field repeats the source in the old form in some files (ignorable). The 3rd is the target (normal ControlId).
In the demos: 57 links coming from mixer slots and 18 from channels; outputs used k = 0 (46), 1 (17), 2 (12) **[DATA]**.
Currently `Flp.cpp` reads `source >> 16` as the "clip channel" — these records land on a non-existent channel or on a channel
that is not a clip, and are silently discarded.

### 7.2 Inside Patcher

Same concept, but via class 2 connections (§1.5): `output control port (0x1000000k)` → `parameter
port p`. 18,405 such connections in the data (Formula/X-Y/Control Surface → PEQ2, PanOMatic…).

### 7.3 Fruity Peak Controller (42 B) **[STR+DATA]**

10 × `i32` = Tags 0…9 + 2 option bytes:

| i | Parameter | Range (default) |
|---|---|---|
| 0 | Base level | 0…65536 (0) |
| 1 | **Peak amount** | −256…256 (179) |
| 2 | Peak tension | −128…128 |
| 3 | **Peak decay speed** | 5…128 (66) |
| 4 | LFO base level | 0…65536 |
| 5 | LFO amount | −256…256 |
| 6 | LFO tension | −128…128 |
| 7 | LFO speed | 200…65536 (32950) |
| 8 | LFO shape | combo |
| 9 | LFO phase | 0…65535 |

Outputs: 0, 1, 2 — probably *peak*, *LFO* and *peak × LFO* **[HYP: the order was not isolated]**. Typical use
in the demos: Peak Controller in a slot whose audio is the kick, linked to the **volume of a Balance** on another track
(e.g. "With You": track 1 slot 5 → track 7 slot 4, parameter 1 = Balance volume) = *sidechain pumping*.

### 7.4 Fruity Envelope Controller (generator, 2–6 KB) **[DATA; partial layout]**

Header `0x00060003`, `i32 14`, 14 blocks of 11 `i32` (attack/decay/sustain/release scales = 128, LFO
`0x9C40`…), then **articulators in the Sytrus format** (`REA_rodada11.md` §1.6): `i32 3`, 3 floats
(depths), and parts with `TCustomMEnvelope` objects — the envelope (type 1) followed by LFO and maps (type 3,
two flat points). In "Basic Sidechain" the envelope is `0 → 0.754 at 0.237 → 1.0 at +0.092` (tensions −0.30 and
+0.18): the classic *duck* that comes back in ~⅓ s. Δx in **seconds** when the footer's TEMPO bit is off
(as here), in beats when on. The envelope fires on every note of the channel (the channel's pattern is the "trigger").

### 7.5 Formula / X-Y / Control Surface **[DATA]**

- **Formula Controller:** `i32` version (2/4) + 3 knobs `a, b, c` (0…65536) + formula text (UTF-16) + RTF
  description. Evaluating it requires an expression interpreter; out of scope.
- **X-Y Controller** (77–81 B) and **Control Surface** (up to 29 KB): current values of the axes/controls + UI. A
  static control = constant value: just apply the current output value to the target once.

---

## Recommendations for LYNX

1. **Patcher in a mixer slot (priority 1).**
   - **Parser** (`src/flp/` or `FlEffects.cpp`): implement §1.1–1.5 (`u32 id + u64 size` chunks, recursive id 0
     groups, UTF-16 strings in v4 / ANSI in v3). Produce `PatcherGraph { nodes[id → name (5008),
     state (5005), nIn, nOut], ports[id → (node, class, index, input?)], pins[id → (input-from-outside?,
     bus, name)], edges[(source, destination, gain, class)], exposed parameters (1100) }`.
   - **Serial flattening:** if the audio subgraph on bus 0 is a single chain
     `IN → n1 → … → nk → OUT` (each node with exactly 1 audio input and 1 audio output in use, all on
     bus 0, gains 1.0), **replace the Patcher slot with the k slots**, decoding each `5005` with the
     existing decoder (`FlEffects.cpp`, case-insensitive comparison). If slots run out (max. 10 per track),
     keep the first ones and warn. Gain ≠ 1 on a cable of the chain → multiply into the next `mix`/utility.
   - **Parallel → submix:** when `IN` splits into R branches that only join at `OUT`, and each branch is a
     simple chain: create R auxiliary tracks in the LYNX mixer (name `"<track> · Patcher branch r"`), each one
     with the branch's chain; the original track sends (post-fader *send*, level = gain of the 1st cable) to each branch and
     stops going directly to the destination; the branches route to the track's original destination with the gain of the last cable.
     This is only exact when Patcher is the **last** effect on the track and the fader is at 0 dB; otherwise, warn.
     Branches that leave via **bus 1 of a Stereo Shaper / Frequency Splitter** (M/S, bands) cannot be
     reproduced with simple sends: approximate the branch with an EQ (for bands, §5) or discard the branch with a warning.
   - **Sidechain** (edge to an input with bus ≥ 1) and nodes outside `IN→OUT` (Peak Controller,
     Formula, notebook): ignore for audio; the control connections become automation (item 4).
   - **Exposed parameters (1100):** keep `Patcher index → (node, parameter)` to redirect FL automations
     that point to "Patcher parameter i" (slot ControlId with `pluginParamBase + i`).
   - **Generator Patcher (channel):** pick the generator node linked to `OUT` with the most gain (Harmor, Sytrus, 3x Osc…),
     import it as the channel's generator and hang its effect chain as channel FX, if LYNX has that;
     otherwise, just the generator.
2. **Gross Beat → tempo-synced volume gate (priority 2).** Read volume slot `p1` (§2.1), the
   `volume mix` `p3/256`, attack/release (§2.2). If the slot is "Empty" (2 points y = 1) or `mix = 0`, create
   nothing. Otherwise, two options:
   - **(preferred)** new `EffectKind::volumeGate` (or a mode of `utility`): table of up to 64 points
     `(x in beats, y, tension, type)` over 4 beats, phase = `position in beats mod 4` (with *position sync*
     ≠ 5, use the corresponding period), curve types 0 (exponential by tension), 2 (hold), 5/6 (pulse/wave
     with number of cycles from the tension; fallback: straight line), gain `(1 − mix) + mix·y`, attack/release smoothing
     (1-pole follower with τ = time/2.2).
   - **(no new effect)** generate an automation clip for the gain of a `utility` inserted in its place,
     repeating the pattern every bar throughout the song (note: `utilityGainValue` only goes down to −24 dB; for
     `y = 0` use the minimum and accept the imperfect "mute").
   - The *time slot* (`p0`) is only tolerable when `p2 = 0` or the time slot is flat (`y ≡ 1`); otherwise log
     "Gross Beat time travel ignored".
3. **Frequency Splitter → EQ.** For the main output: `parametricEq` with a *low shelf* at `f(p0)` with gain
   `L − M`, a *high shelf* at `f(p1)` with gain `H − M` (3 bands; with 2 bands only the shelf at `f(p0)` with `L − H`) and
   output gain `M` (dB = word/100; −1800 = mute → −60 dB). Slope: combo 0…7 → 6·(k+1) dB/oct
   **[HYP]**, or simply a 12 dB/oct shelf. The per-band sends to other tracks are lost (warn).
4. **Controller links (`0xE3` with source ≥ `0x8000`).** In `Flp.cpp`, separate: if `source & 0xFFFF ≥
   0x8000`, it is a `ControllerLink { source: ControlId (channel or track/slot), output k, target }`, not a clip.
   - Envelope Controller (channel): for each note of the channel in the playlist, stamp the envelope (§7.4) as
     automation points on the target (value = base + envelope; converted by the existing `effectParamFromFl`). Produces
     faithful automation for the "sidechain via pattern" case.
   - Peak Controller → volume (Balance/fader/utility) with negative *amount* or volume target: approximate with
     **ducking** — an automation clip that dips on each note of the channel feeding the Peak Controller's track
     (decay = `p3`) **[HYP]**; or, if LYNX gains a compressor with sidechain, use that.
   - X-Y / Control Surface / Formula without their own automation: apply the current value once to the target parameter.
5. **Effector:** map by `kind` (§4): Distort → `distortion` (drive ≈ X), Lo-fi → `distortion` (*bitcrush*
   mode if available, otherwise low drive + LP `filter` with cutoff ∝ Y), Flanger/Phaser → `chorus` (short delay,
   feedback ∝ Y), Filter → `filter` (cutoff ≈ X on a log scale 20 Hz…20 kHz, resonance ≈ Y), Delay → `delay`
   (synced time ≈ X), Reverb → `reverb` (size ≈ X, damping ≈ Y), Stereo → `utility` (width ≈ X);
   Trans/Grain/Vox/Ring → bypass with a warning. `mix = p2`; `p1 = 1` → slot disabled. All the X/Y scales are
   **[HYP]** (the engine is C++ in `engine_x64.dll`, not read in this round).
6. **Pitcher and Vocodex:** import as a disabled/bypassed slot with a message; there is no honest approximation.
7. **Suggested validation:** (a) open "Bien Duro" ins. 55 in FL and in LYNX after flattening (PEQ2 → Limiter);
   (b) render a track with Gross Beat "1/2 Bt Gate" and compare the amplitude envelope; (c) check in FL
   whether the gain of a Patcher cable (right click on the cable) corresponds to the `f32` from §1.5.

## Evidence index

| Function / data | What it does | Marker |
|---|---|---|
| Patcher `FUN_00955150` | Writes the state: 1000, 0x3EA, 0x3EB, 0x3EC, nodes (5000…5001), 0x44C, pins (0x578), connections (0x514), 0x5DC, positions (0x640), 0x3E9 | DECOMP |
| Patcher `FUN_009513d0` | Reads the state (dispatch by id; v < 3 migrates; v < 2 swaps types 1/2 of 0x3EB) | DECOMP |
| Patcher `FUN_00952a70` | Reads a node: names, state, options, ports; handles `"Fruity Wrapper"` | DECOMP |
| Patcher `FUN_007c97a0`, `FUN_007c9a50`, `FUN_007c9a70` | Chunk = `u32 id + u64 size + data`; group id 0 with size fixed up on close | DECOMP |
| Patcher `FUN_0079cbc0`, `FUN_0079ca50` | Enumerate the links to write (port +8, port +0x10, `f32` +0x18, bool +0x1C, class) | DECOMP |
| Patcher `FUN_0079b730`, `FUN_0079ef40` | Create the link: order the ends (non-output first) and initialise the gain to 1.0 | DECOMP |
| Patcher `FUN_00952440`, `FUN_0079b790` | Read a connection and reconnect by port class | DECOMP |
| Patcher `FUN_00952560` | Reads a pin (audio/MIDI, default names "Audio input/output", "MIDI Port %d", "Notes") | DECOMP |
| Patcher `FUN_009529d0` | Reads an exposed parameter (0x44C) | DECOMP |
| 363 Patcher blobs (`pdec.py`, `graph.py`, `flat2.py`) | Full parse, connection direction (100 %), topologies, internal states = mixer states | DATA |
| Gross Beat VMT `0x83f0f0` (`vmts.py`), `FUN_0083f890` | `SaveRestoreState`: version, 9 params, options, 2 × 36 slots (name + 15 B + envelope) | DECOMP |
| Gross Beat `FUN_00840b20`, `FUN_00840ed0`, `FUN_00841de0`, `FUN_00841fb0` | `ProcessParam`, parameter text, curve `501^(v/10000) − 1` ms, release ×2 | DECOMP |
| Gross Beat DFM `TSynthEditorForm` (`GrossBeat.dfm.txt`, `gb_items.txt`) | Tags 0–7, sync combos, snap, options | STR |
| 45 Gross Beat blobs (`gbp.py`) | 72 slots, sum of Δx = 4, curve types | DATA |
| Pitcher `FUN_18001d6e0`, `FUN_18001d790`, `FUN_18001c240` | Tag `1RTP`, 33 + 1 floats, parameter names | DECOMP |
| DFM Effector / Frequency Splitter / Vocodex / Peak / Envelope Controller (`*.dfm.txt`) | Tags, ranges, defaults | STR |
| 31 Effector, 30 Frequency Splitter, 40 Peak Controller | Layouts, ranges, fit of the frequency curve | DATA |
| `0xE3` events in the demos | Source = ControlId of a controller output (75 links) | DATA |

### Open questions

- Patcher: confirm in FL that the cable `f32` is linear gain (and what the UI range is); meaning of bit 0
  of the node and cable flags; `0x3F2/0x3F6` and `0x47E/0x6A5` (not seen); semantics of auxiliary outputs 1…4 of the
  Fruity effects other than Frequency Splitter (Stereo Shaper uses output 1 in the M/S patches — *mid* and *side*
  separated?).
- Gross Beat: exact order of the 9 option bytes and of the 7 bytes per slot; unit of parameter 8; delay scale
  in time travel; how the global tension (p6) combines with each point's.
- Pitcher: names of parameters 21, 22, 25…33 (MIDI mode, scale, harmonisation).
- Effector: X/Y scales per algorithm (C++ engine), meaning of the 64 initial floats.
- Frequency Splitter: table of the slopes and of fields 15…27 (send destination tracks).
- Peak Controller: which output (0/1/2) is peak, LFO and product; *decay speed* curve.
- Envelope Controller: what the 14 blocks of 11 integers are and how many outputs there are (8 articulators in the "Basic Sidechain" blob).
