# Behavioural analysis of FL Studio 2026: round 16 (REA) — MIDI Out and per-port routing, Fruity Love Philter (complete layout, automation indices, Mod X/Y) and Morphine (chunk tree, master parameters, spectra)

> **Nature of this document.** Continuation of `REA_rodada15.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode has been copied: what is here are descriptions, data layouts and
> formulas written in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched (all the states below are open `0xD5` blobs — the Love Philter one
> is only zlib-compressed — written by the plugins themselves).

- **Targets** (local copies in `tmp/r16/bin/`, originals in `Plugins/Fruity/Generators/<name>/` and
  `Plugins/Fruity/Effects/Fruity Love Philter/`):

  | Plugin | DLL (SHA-256) | Language | State (`SaveRestoreState`) / parameters |
  |---|---|---|---|
  | MIDI Out | `a87bf4c1…ef45` | Delphi (`TTestPlug`, VMT `0x6fd778`) | `FUN_006fde10` (VMT+3); dispatch `FUN_006fdb80`; routing word `FUN_006fec00` |
  | Fruity Keyboard Controller | `72ef38b5…0b3c` | Delphi (VMT `0x6fcbb0`) | `FUN_006fcff0` (VMT+3) |
  | Fruity Love Philter | `b9f11d63…f547` | Delphi (VMT `0x853df8`) | `FUN_008551e0` (VMT+3); `ProcessParam` `FUN_008579e0`; index→target `FUN_008584e0`; text `FUN_00858100`; names `FUN_00858680`; constructor `FUN_00856780`; coefficients `FUN_00859100` |
  | Morphine | `decf72d6…d48e` | C++ (MSVC, `MorphineInstance`) | chunk tree: `MPR0` `FUN_18010eb40`, `MVC0` `FUN_180113460`, `MZH0` `FUN_18012ad30`, `MSP0` `FUN_1800ec160`, `MBP0` `FUN_1800ef900`; parameter get/set `FUN_18010e340`/`FUN_18010dc50`; names `FUN_18010a2a0`; text `FUN_180109ef0`; master envelope `FUN_1800fffa0`/`FUN_180100620` |

- **Tools:** Ghidra 12.1.4 *headless* in `/tmp/r16gh_<dll>`, scripts in `tmp/r16/gh/scripts/` (those from
  round 15 + `DecompRange.java`, which decompiles every function in an address range). DFM with
  `tmp/r14/tools/dfm.py` + `tmp/r15/dfmc.py`; published fields with `tmp/r15/fields.py`; VMTs with
  `tmp/r14/tools/vmts.py` + `tmp/r15/vmt.py`.
- **Supporting decoders (ours, Python):** `tmp/r16/lp.py` (complete Love Philter: **96/96 blobs**
  — 43 slots + 53 `.fst`, versions 2, 5, 6, 10, 11, 12 — with no byte left over), `tmp/r16/lpstat.py`
  (what each articulator modulates), `tmp/r16/lplinks.py` (links/automation pointing at the Love Philter),
  `tmp/r16/mor.py` (Morphine `MPR0` tree: 7/7 channels + `.fst`), `tmp/r16/scanw.py` (MIDI ports of the
  wrapper and of MIDI Out in every `.flp`).
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

**Conventions.** `curve11(x)` = `(11^x − 1)/10` (FL's volume curve, already in LYNX as `curve(11, x)`).
`scurve11(v)` = `sign(v)·curve11(|v|)` (signed version, used when the control allows a negative value =
phase inversion). "Δx" = coordinate stored as an **increment** relative to the previous point (this is how it is
in the envelopes/maps of IL's generic editor, see round 11 §1.6).

### Counts in the data (priority) **[DATA]**

| Plugin | Where | In the demos | Demo files | `.fst` | `0xD5` size |
|---|---|---|---|---|---|
| **Fruity Love Philter** | mixer slots | **39** (43 across all of `Data`) | 16 | 53 | 837…1,427 B (zlib; 59…88 KB decompressed) |
| Morphine | channels | 7 (4 files) | 4 | 4 | 47…241 KB (`SSLF`…) |
| MIDI Out | channels | 3 (+1 template) | 2 | 14 | 383 B (v6/v7); Roland presets 675…3,377 B |
| Fruity Keyboard Controller | channels | 10 (all in "Drumloop Masher") | 1 | 11 | 553 B (v2) / 501 B (v0) |
| Fruity Wrapper | mixer slots | 23 (2 files) | — | — | record `0x01` always `−1, −1` |

No `.flp` in `Data` has a **channel** with the Fruity Wrapper (all generators in the demos are native), and the
23 mixer wrappers have both MIDI ports at `−1`. In other words: **in the demos, MIDI Out does not feed any
hosted plugin** — it feeds the **Pitcher** (MIDI mode) and, in the video template, ZGameEditor (§1.3).

---

## 1. MIDI Out and the path of notes to a plugin

### 1.1 MIDI Out `0xD5` layout (version 7) **[DECOMP `FUN_006fde10`, `FUN_006fec00`, `FUN_006feb10`, constructor `FUN_006fe4c0` + DATA]**

| Offset | Type | Field (object `P+…`) | Meaning |
|---|---|---|---|
| 0 | u32 | — | **version** (current FL writes 7; demos 6 and 7; bank `.fst` 4; Roland presets 2/4/5) |
| 4 | i32 | `P+0x120` | **MIDI channel, 0-based** (the UI shows +1; `ChanSelect` 1…16). Default for a new instance: a global counter cycling 0…14 **skipping 9** (drum channel 10) |
| 8 | i32 | `P+0x124` | always −1 in the data; not used on load **[HYP: reserved]** |
| 12 | i32 | `P+0x128` | **Bank MSB** (CC 0), −1 = do not send (`BankMSBSelect`) |
| 16 | i32 | `P+0x12C` | **Bank LSB** (CC 32), −1 = do not send (`BankLSBSelect`) |
| 20 | i32 | `P+0x130` | displayed control **page** (`PageSelect`; control `i` belongs to page `i div 9`) |
| 24 | i32 | `P+0x134` | **output port** 0…255 (`PortSelect`) |
| 28 | u8 | `P+0x138` | "Send pan, volume and pitch" (`SendPanVolBox`) |
| 29 | u8 | `P+0x139` | "Map note color to MIDI channel" (`MapColToChanBox`) |
| 30 | 73 × i32 | `P+0x13C…` | `[0]` = **program** (1…128; 0 = do not send — it is sent as `0xC0` with `program−1`), `[1…72]` = current values of the 72 controls (0…127) |
| 322 | records | `P+0x260 + i·0x2C` | up to 72 control definitions: `u8 index` (0xFF terminates), 24 B (`type` CC/RPN/NRPN/AFT/Note, number, min, max, note-off value, …), 2 strings (long name, short name). Only those differing from the default are written |
| … | 8 × string | — | names of the 8 pages ("Page 1"…"Page 8") |
| … | u32 (v > 5) | `P+0x1FC0` (bits) | "On reset" options **inverted** (bit = 1 → option off): 1 program, 2 pan, 4 volume, 8 pitch, 0x10 pitch bend range, 0x20 cut notes |

Older versions read fewer bytes in the header block: v ≥ 4 → 26 B (up to `P+0x139`), v 2…3 → 24 B, v < 2 → 20 B;
v < 7 copies field +8 of each control to +0x14.

**The routing word** (`FUN_006fec00`): the plugin builds `channel + port·0x1000000` and adds it to each
short message it sends to the host — i.e. **the port goes in the high byte of the internal MIDI message** and the
channel in the low nibble of the status. The host (FL) delivers the message to every destination whose **input port**
equals that port. The dispatch `FUN_006fdb80` also accepts from the host (opcode 0x10) a change of channel (`param_3 = 0`) and of
port (`param_3 = 1`).

**Demo data:**

| File / channel | v | channel | port | Destination listening on the same port |
|---|---|---|---|---|
| Asher Postman — "ASHER POSTMAN" | 6 | 0 (ch 1) | 0 | no plugin with port 0 in the project **[DATA]** ("decorative" channel) |
| Astes — Bien Duro — "1.2 MIDI" | 7 | 0 (ch 1) | **0** | **Pitcher** on insert 4, slot 0 (MIDI port 0) |
| Astes — Bien Duro — "1.3 MIDI" | 7 | 0 (ch 1) | **1** | **Pitcher** on insert 5, slot 0 (MIDI port 1) |
| Template "Video chopping" — "Trigger Video" | 7 | 0 | 1 | ZGameEditor Visualizer (not verified) |

The Bien Duro pair closes the round 15 §1 hypothesis about **Pitcher parameter 33**: the two Pitchers in MIDI
mode have `p33 = 0.00638` and `0.0112` → `int(256·v) − 1` = **0** and **1**, exactly the ports of the two MIDI Outs;
the other 9 Pitchers in the file have `p33 = 0` (→ −1, no port) **[DATA]**.

### 1.2 Where the Fruity Wrapper stores the input port **[DECOMP from round 5 + DATA]**

Record `0x01` of the wrapper blob (round 5 §2.2): `i32 MIDI input port` (`W+0x854`), `i32 MIDI output
port` (`W+0x858`), `u8` option, 11 zero bytes. −1 = none. **There is no MIDI channel in the wrapper**: it only filters
by port; the channel travels inside the message and the hosted plugin decides (Kontakt multi, etc.).
In the demos: 23 wrappers, all `−1, −1` **[DATA]** — there is no real example of MIDI Out → VST in the data.

Native plugins that listen on a port via their **own state** (not via the wrapper): Pitcher (p33, above). For the
other natives the port lives in the options of FL's native *wrapper*, which do not appear in the demos' `.flp`
**[HYP]** (the slots' `0xD4` header does not change with it — checked in the three files above).

### 1.3 Routing algorithm for LYNX **[HYP: FL behaviour, consistent with the data]**

1. For each MIDI Out channel: `port = i32 @24`, `channel = i32 @4` (0…15).
2. Destinations listening on port `p`: (a) channels/slots with a Fruity Wrapper whose record `0x01` has
   `input port == p`; (b) slots with a Pitcher whose `int(256·p33) − 1 == p` (and MIDI button on, p25).
3. The notes of the MIDI Out channel's *piano roll* are delivered to all these destinations **with MIDI channel
   `channel`** (or, if "Map note color to MIDI channel" is on, with the note colour as the channel). Do not
   filter by channel in LYNX: forward the message with the channel and let the plugin decide.
4. Program/Bank (`@30`, `@12`, `@16`) are sent on reset/start if ≥ 0 (program > 0).
5. MIDI Out itself produces no sound: in LYNX, the channel should be muted and serve only as a note source.

### 1.4 Fruity Keyboard Controller **[DECOMP `FUN_006fcff0` + DFM + DATA]**

This is not MIDI routing: it is an **internal controller** (like the Envelope Controller), whose output 0 is a value
0…1 chosen by the **last key played** on the channel, with smoothing; the `0xE3` links point at it with
source `channel<<16 | 0x8000` (round 14 §7.1).

| Offset (v2/v3) | Type | Meaning |
|---|---|---|
| 0 | u32 | version (3 in current FL; demos 2; old `.fst` 0) |
| 4 | 2 × i32 | smoothing: **attack** and **release** (0…256, default 32; `SpeedAttWheel`/`SpeedRelWheel`; these are parameters 0 and 1). v0: only 1 i32 (release copies attack) |
| 12 | 132 × f32 | **value of each key** 0…131 (v0: 120 keys) |
| 540 | 2 × i32 | key range selected on the UI keyboard (no effect on the sound) |
| 548 | f32 | *note off* value (`AutoRelWheel`, 0…1) |
| 552 | u8 | *auto release* on (when no key is held, it goes to the note-off value) |
| 553 | u8 (v ≥ 3) | "link attack & release" button |

Demo "Drumloop Masher": "Glitch" maps keys 48…83 onto a 0…1 ramp and is linked to the **Mod X** of two Love
Philters (§2.6); "Filter", "Delay send N", "Verb send N" do the same for other parameters **[DATA]**.
**LYNX:** treat as a control source: `out = values[note]` on the note (or `noteOffValue` on note-off if
auto release is on), filtered by a 1st-order smoother with times ∝ attack/release.

---

## 2. Fruity Love Philter

### 2.1 Blob envelope **[DECOMP `FUN_008551e0`]**

- `u32` version. If > 0xFFFF, the version is the high word (current FL writes `0x000C0003` → **12**); data: 2, 5,
  6, 10, 11, 12. Value `0x7FFFFFFF` or ≥ `0xC0004` = rejected.
- The rest of the blob is a **zlib stream** (all versions). Everything below is the decompressed content, little-endian.

### 2.2 Decompressed layout **[DECOMP `FUN_008551e0`, `FUN_00855080`, constructor `FUN_00856780`; DATA 96/96]**

| Block | Size | Content |
|---|---|---|
| Globals | 6 × i32 (`P+0x128`) | parameters 0…5 (table §2.4) |
| Globals 2 | 33 B (v ≥ 11; v10: 32; v < 10: 31) (`P+0x140`) | `i32 Mod X`, `i32 Mod Y` (0…1024, default 512), `i32` (0), `f32` (≈ 0.968: internal smoothing coefficient **[HYP]**), `i32` **soloed unit** (−1 = none; with solo, the other units get volume 0), `i32` **HQ envelopes** (`DQEnvBtn` = value > 0), `i32` **oversampling** (`DQOverSelect`, 0…4, default 1), `u8` **SMOOTH** (`ModSmoothBtn`), `u8` **CENTER** (global DC), `u8 u8 u8` menu options (*Sync pattern changes*, *Note input*, *HQ WaveShaper* **[order HYP]**) |
| (v ≥ 2) | `u8` + `u32` | `USE1` (envelope follower reads unit 1) and position of the `HeartWheel` (UI only) |
| Info text | v < 6: `i32 n` + n bytes (RTF ANSI); v ≥ 6: `u8 n` + n × UTF-16 | "Info / comments" box ("Preset name\rPreset details") **[the n ≥ 255 case was not seen; HYP]** |
| **8 units** | see §2.3 | |
| (v ≥ 12) | `u8` | `P+0x161` (option; 0 in the data) |

### 2.3 Unit (×8, in this order) **[DECOMP + DATA]**

| Field | Type | Content |
|---|---|---|
| params | 12 × i32 | unit parameters 0…11 (table §2.4) |
| mode | i32 (+0x30) | **order / mode** (`FOrderSelect`): 0 off, **1 single** (default), 2 dual, 3 triple, 4 alternate dual, 5 alternate triple |
| type | i32 (+0x34) | **filter type** (`FTypeSelect`), table §2.5 (default 0) |
| flags | 3 × u8 (+0x38…) | `+0x38` **ALT** (alternative resonance/width; `FFlatBtn`), `+0x39` **unit enabled** (`EnableBankN`; only the 1st comes enabled), `+0x3A` **waveshaper enabled** (`WSEnableBtn`) |
| WS curve | envelope | *waveshaper* curve (`kind 3`, default `(0, 0) → Δ(1, 0.75)`) in the envelope format of round 11 §1.6 |
| WS bipolar | u8 | waveshaper unipolar/bipolar button |
| **8 articulators** | see below | targets, in this order: **0 pan, 1 volume, 2 cutoff, 3 resonance, 4 low/gain/drive, 5 band, 6 high, 7 waveshaper** (name table at `PTR_u_panning_008d19c0`) **[DECOMP]** |

**Love Philter articulator** (same `TMEnvelope`/`TMLFO`/`TMMap` family as Sytrus, round 11 §1.6, but with
different parts — `EnvSelect`: *pattern envelope, LFO, mod X mapping, mod Y mapping, input envelope follower
mapping*):

```
i32  version (1)
f32  mode: 1.0 = unipolar target (multiplier), 0.0 = bipolar (offset)   [DATA: pan/cutoff/res = 0; vol/low/band/high/ws = 1]
f32  envelope depth    (in the cutoff articulator = p0/128, the unit's ENV knob)
f32  LFO depth         (= envelope depth × scurve11(MainLFO/128))
10 × { envelope (kind 1) ; 4 × i32 knobs A/D/S/R (default 128,128,0,128) }   ← one per "pattern" 1…10
LFO envelope (kind 2) ; 4 × i32 (speed 512…65536, tension, skew, pulse width) ; + 1 i32 if the LFO version ≥ 3
3 × map (kind 3): X, Y, envelope follower
```

The envelopes use the 20 B header from round 11 (`flags`, **enabled** at +4, decay, loop, sustain). The active
*pattern* is global parameter 5 (`PatNum`, 1…10): only the envelope at that index plays.

X/Y/IEF maps: the curve is `f(input)` in Δx; neutral = **0.5** for bipolar targets and **1.0** for unipolar ones
(all 63 "untouched" articulators of the enabled units in the demos follow this) **[DATA]**.

### 2.4 Parameter indices (automation/links) **[DECOMP `FUN_008584e0`, `FUN_00856780`, `FUN_008579e0`, `FUN_00858100`]**

There are **359** parameters (`< 0x167`). The first 8 are global; then each unit `u` (0…7) starts at
`base(u) = 8 + 44·u` and has **12 parameters of its own** (the last unit only 11 — it has no *to next*) followed
by **32 articulator parameters** (8 articulators × 4).

**Globals (k = 0…7):**

| k | Name (DFM) | Raw range | Effective value |
|---|---|---|---|
| 0 | Master level (`MainVolSlider`) | 0…160 (128) | gain `curve11(v/128)`, multiplies the output volume of all units |
| 1 | Master LFO level | −128…128 (128) | `scurve11(v/128)`; scales the LFO depth of all articulators |
| 2 | Envelope follower level | 0…160 (128) | `curve11(v/128)` |
| 3 | Envelope follower attack | 0…16384 (4096) | **ms = 12.5·(81^(v/16384) − 1)** → 0…1000 ms (4096 → 25 ms) |
| 4 | Envelope follower release | 0…16384 (8192) | same formula (8192 → 100 ms) |
| 5 | Pattern number | 1…10 (1) | integer (clipped to 1…10) |
| **6** | **Mod X** | 0…1024 (512) | **x = v/1024** |
| **7** | **Mod Y** | 0…1024 (512) | **y = v/1024** |

**Unit u, k = base(u) + i, i = 0…11** (DFM Tag = i):

| i | Name | Raw range (default) | Effective value **[DECOMP `FUN_00858af0`]** |
|---|---|---|---|
| 0 | ENV (cutoff env/LFO/IEF amount) | 0…256 (128) | envelope depth of the **cutoff** articulator = `v/128` |
| 1 | **CUT** cutoff | 0…256 (128) | `c = v/256` (0…1), added to the output of articulator 2 |
| 2 | **RES** resonance/width | 0…256 (128) | `r = v/256`, added to the output of articulator 3 |
| 3 | L (low / gain / drive) | 0…128 (128) | `curve11(v/128)` × articulator 4 |
| 4 | B (band) | 0…128 (0) | `curve11(v/128)` × articulator 5 |
| 5 | H (high) | 0…128 (0) | `curve11(v/128)` × articulator 6 |
| 6 | WS pre amp | 64…192 (128) | `v/128` |
| 7 | WS mix | 0…128 | `v/128` |
| 8 | PAN | −128…128 (0) | `v/128` |
| 9 | **VOL** output | 0…160 (128) | `curve11(v/128) · curve11(Master/128)`; 0 if another unit is soloed |
| 10 | IN volume | −160…160 (128) | `scurve11(v/128)` (negative = phase inverted) |
| 11 | **NEXT** (to next filter) | −160…160 (0) | gain `scurve11(v/128)` with which **this unit's output feeds the next** (only counts if the next is enabled) |

**Unit u, articulators: k = base(u) + n_u + 4·a + f**, `n_u` = 12 (11 in unit 8), `a` = target 0…7
(pan, vol, cut, res, low, band, high, ws), `f` = 0…3 = **LFO speed, tension, skew and pulse width**
of that articulator (the setter writes to `LFO+0xD0+4f`; the text for `f = 0` comes from the envelope editor, the others
are `v/max` in %) **[DECOMP]**.

Examples used in the demos **[DATA `lplinks.py`]**: `k = 7` (Mod Y) — 7 Envelope Controller links ("Basic
Sidechain" and similar) and 1 clip; `k = 6` (Mod X) — 4 links (Keyboard Controller "Glitch", X-Y Controller) and
4 clips; `k = 9` (unit 1 CUT) — 10 clips; `k = 10` (unit 1 RES) — 1 clip; `k = 17` (unit 1 VOL) —
3 clips. No data points at articulators (k ≥ 20).

### 2.5 Filter types and scales **[DECOMP `FUN_00859100`, constants `0x85a740…0x85a908`; Hz HYP]**

Name table (`0x8d18c0`): 0 **Chocolate state variable**, 1 Lime low pass, 2 Lime high pass, 3 Vanilla low
pass, 4 Vanilla band pass, 5 Vanilla band stop, 6 Vanilla high pass, 7 Vanilla all pass, 8 Vanilla low shelf,
9 Vanilla peaking, 10 Vanilla high shelf, 11 Mango low pass. Usage (63 enabled units in the 43 slots of `Data`): 0 (SVF) 36, 1 8, 3 6, 6 6, 2 2, 7 2, 4 1, 5 1, 11 1
**[DATA]**.

With `c` and `r` already summed with the articulator (0…1) and `k` = oversampler rate factor (`unidade+0xC4`;
interpreted as `44100/fs_interno`, **[HYP]**):

| Type | Frequency | Resonance / Q |
|---|---|---|
| 0 SVF (Chocolate) | coefficient `g = ((51^c − 1)·0.0159 + 0.005)·k`, clipped at 0.625 (→ `fc ≈ fs·asin(g/2)/π` **[HYP]**) | damping `d = 1 − 0.9·r` (r ≤ 0 → 1; r ≥ 1 → 0.2); ALT → `d²`. Output = `L·LP + B·BP + H·HP` with L/B/H = parameters 3/4/5 |
| 1, 2 Lime LP/HP | `x = (76^c − 1)·0.01324` (≤ 1.6); `ω = (x + 0.007)·(π/2)·k` (≤ 0.4π) | `1001^r` (r ≥ 1 → 50) |
| 3 Vanilla LP | `x = (1501^c − 1)·0.000664667` (≈ 0…0.997) ; `ω = (x + 0.003)·2π·k` (≤ 2.8495 rad ≈ 0.4535·fs) | **Q = 0.1 + 0.02495·(2001^r − 1)** (0.1…50) |
| 4, 5 BP / notch | `x = (1501^c − 1)·0.00039993` (≤ 1.0001); `ω = (x + 0.0001)·2π·k` | width `0.01 + 0.1199·(101^r − 1)` (≤ 12) |
| 6 HP | same as 4/5, but clipped at 0.6001 | Q as for LP |
| 7 all pass | same as 4/5 | Q as for LP |
| 8, 10 shelves | same as 4/5 | slope `S = 0.1 + 0.45·(3^(1−r) − 1)` (r = 0 → 1; r = 1 → 0.1) |
| 9 peaking | same as 4/5 | `Q = 0.1 + 0.04995·(2001^(1−r) − 1)` (r = 0 → 100, narrow; r = 1 → 0.1, wide); gain = value of the "low/gain" slot |
| 11 Mango LP | `x = (201^c − 1)·0.01495` (≤ 3.0) | another family (`251^…`, fitting polynomial) **[not detailed]** |

Reading in Hz at 44.1 kHz (k = 1): Vanilla LP `c = 0` → 132 Hz, `c = 0.5` → ≈ 1.2 kHz, `c = 1` → clip ≈ 20 kHz;
families 4…10 `c = 0` → ≈ 4 Hz, `0.5` → ≈ 670 Hz, `1` → clip. With ALT, in the biquad types, `r` is multiplied
by `(c + 1)·0.5` before the calculation.

**Mode/order:** the number of cascaded stages is `modo` for 1…3 (12, 24, 36 dB/oct in the biquads); 4/5
(*alternate*) use 2/3 stages with the cutoff of each stage offset **[HYP]**. Mode 0 = unit does not filter
(gain/pan/WS only).

### 2.6 Routing between units and Mod X/Y **[DECOMP `FUN_0085d110`, processing at `0x85b…` + DATA]**

- For each enabled unit `u`, the input is `in·IN(u) + out(u−1)·NEXT(u−1)` (the second term only if `u−1`
  is enabled). The output of each unit, with VOL and PAN, is **summed** into the plugin output. With all NEXT at
  0 (99 % of the data), the units are **in parallel**.
- Per sample/block: `cutoff = p1/256 + art_cut`, `res = p2/256 + art_res`, gains L/B/H = `curve11(p/128) ·
  art`, volume and WS likewise (multipliers).
- **Mod X / Mod Y have no fixed target**: each articulator has an X map and a Y map; X/Y (0…1) pass through the curve
  and the result becomes an offset (bipolar, neutral 0.5) or a multiplier (unipolar, neutral 1). The "Mod X target"
  is therefore the set of articulators whose X map is not the neutral line.

What the data maps (63 enabled units in the 43 slots of `Data`; 56 of them in the 39 slots of the demos) **[DATA `lpstat.py`]**:

| Non-neutral map | Occurrences | Typical shape | Reading |
|---|---|---|---|
| **Y → volume** | 8 | line `(0, 0) → Δ(1;1)` | **gain = y**: the *sidechain*/ducking (Envelope Controller → k = 7) |
| X → cutoff | 14 | line `0→1`, or step at 0.5 | cutoff shifts by `(x − 0.5)` (map scale: −0.5…+0.5 in `c`) |
| Y → resonance | 12 | line `(0, 0)→(1, 0.625)` or similar | same for `r` |
| X → pan | 7 | line `0→1` | pan = `(x − 0.5)·2` added |
| X → volume in steps | 25 (+2 lines) | 2…6 steps of 0/1 along x | **gate by X range** (with the Keyboard Controller "Glitch" on X: each key picks a pattern) |
| LFO → pan / cutoff | 7 / 7 | — | auto-pan / *wobble* |
| envelope → volume | 9 | pattern envelope enabled | rhythmic gate (pattern 1) |

### 2.7 Approximation in LYNX **[HYP; see Recommendations]**

An enabled unit with `modo ≥ 1` → `filter` (type per §2.5: 0 with L=128,B=H=0 → LP; L=0,H=128 → HP; B
dominant → BP; 3/1/11 → LP; 6/2 → HP; 4 → BP; 5 → notch; 7/8/9/10 → `parametricEq` corresponding band),
`cutoff` = `c` in Hz per the type's row, Q per §2.5, number of stages = `modo`; then `utility` with gain
`curve11(VOL/128)·curve11(Master/128)` (and, if the Y→volume map exists, gain multiplied by `y`) and pan
`PAN/128`. Units in series (NEXT ≠ 0) → chain; in parallel (all NEXT = 0, more than one enabled) → `parallel`
container. Unit with `modo = 0` → `utility` only.

---

## 3. Morphine

### 3.1 Chunk tree **[DECOMP writers `FUN_18010eb40`, `FUN_180113460`, `FUN_18012ad30`, `FUN_1800ec160`, `FUN_1800ef900`; DATA 7/7]**

The tags are 4-character integers written in little-endian, which is why they appear reversed in the file
(`"0roM"` = `Mor0`). Format:

```
"SSLF" u32 total_size                            (common header of IL C++ state)
"Mor0"                                           (type)
"MPR0"                                           (preset)
  4 × { i32 n ; MVC0 (n bytes) }                 generators A, B, C, D
  i32 n ; MML0 (n bytes)                         morph line (X/Y breakpoints over time)
  i32 n ; MMD0 (n bytes)                         modulation: 4 × MEG0 (envelopes Eg1…Eg4)
  global parameter block (i32 0x5C4 + 0x5C0 B; §3.2)
MVC0: i32 n ; MZH0 (n bytes) ; i32 0x1C ; 6 × i32 (generator options)
MZH0: i32 number of zones ; per zone { i32 width/key, f32 level, i32 n, MSP0 }
MSP0: i32 number of frames ; per frame { i32 duration, f32 gain, i32 n, MBP0 } ; i32 0x424 + 0x420 B (options)
MBP0: 3 × [128 partials in 16 groups of 8: u8 mask + one f32 per 1 bit (0 bits = value 0)]
```

Caution: the size written before each zone's `MSP0` and the trailing `0x424` **include the size field
itself** (unlike the other chunks) **[DATA: only this way do the 7 blobs close]**.

**Partials (`MBP0`)**: 3 vectors of 128 values (`CBreakPoint+0x28/0x30/0x38`, all 0 by default):
`[0]` = **amplitude** of harmonic n+1 (normalised, peak 1.0), `[1]` = frequency deviation (almost always 0;
one generator in "Soft Pad" uses a constant ≈ −0.54 → offset **[HYP]**), `[2]` = **phase** in cycles (−0.5…0.5)
**[HYP from the range]**. Each generator plays the **sequence of frames** (a spectrum evolving over time): the `i32`
of each frame is the relative duration (the sum gives multiples of 100) and the `f32` the frame gain (built-in
amplitude envelope). The absolute time scale depends on *Gen X: Rate* (not decoded) **[HYP]**.

Empty generator = 1 frame of duration 100 with everything 0 (this is the state of C and D in all demos; B in 5 of 7).

`MVC0` options (6 × i32): `[0]` f32 generator **Mix** (data 1.0 / 0.425 / 0.5), `[1]` `int(2·v)` of a
selector (**Loop mode**? 0 in the data), `[2]` int, `[3]` u8 (1 in generators with data), `[4]` f32, `[5]` f32
(1.0) **[only `[0]` is strong; rest HYP]**.

### 3.2 Global parameter block (`0x5C4` B) and indices **[DECOMP `FUN_18010e340`, `FUN_18010eb40`, defaults `FUN_1801099e0`]**

Layout (offset within the block, from the `i32 0x5C4`): `+4` 40 × f32 (object `+0x608…+0x6A4`), `+164`
f32 (`+0x6A8`), `+168` i32 (`+0x6AC`), `+172` 1028 B **noise sample name** (e.g. `AmbientNoise_01.wav`),
`+1200` 256 B **preset name** (e.g. `PAD Keepin it Simple FG`), `+1456` (`+0x6BC`), `+1460` (`+0x6B0`),
`+1464` (`+0x6B4`), `+1468` (`+0x6B8`), `+1472` (`+0x6C0`).

Values are **normalised 0…1** (VST style), except Polyphony and Glide Mode. Parameter index `k` →
field (`A[i]` = i-th f32 of the 40-element vector) and scale (text `FUN_180109ef0` and DSP `FUN_1800fffa0`):

| k | Name | Field | Scale |
|---|---|---|---|
| 0 | Master Level | A0 | **dB = 48·v − 24** (0.5 → 0 dB) |
| 1 | Glide Time | A1 | 0…1 (% ) |
| 2 | Velocity Curve | A4 | % |
| 3 | Bend Range | A8 | `int(12·v)` semitones |
| 4 | Transpose | A5 | `int((v − 0.5)·48)` semitones |
| 5 | Fine Tune | A6 | `int((v − 0.5)·200)` cents |
| 6 | Overdrive | A7 | % |
| 7–10 | Chorus Depth / Wet / Rate / Enable | A13–A16 | rate Hz = `5v + 0.1` |
| 11–16 | Delay Time / Stereo / Modulation / Feedback / Wet / On | A17–A22 | time ms = `(0.95v + 0.05)²·1000` |
| 17–20 | Reverb Decay / Size / Mix / Enable | A23–A26 | % |
| 21–28 | Eq Band 1…8 | A27–A34 | dB = `(v − 0.5)·24` |
| 29 | Eq Enable | A35 | |
| **30–33** | **Master Attack / Decay / Sustain / Release** | A9–A12 | **A s = 15·v^3.5; D s = 30·v³; S = v² (amplitude); R s = 10·v³** (min. 3 ms) — already with the modulation added and clipped to 0…1 **[DECOMP `FUN_1800fffa0`, `FUN_180100620`]** |
| 34–36, 38 | Noise Mix / Pan / Mute / VelSens | A36–A39 | |
| 37 | Noise Mono Mode | `+1472` | |
| 39 | Noise Ratio | `+164` | `2^((v − 0.5)·4)` **[HYP: the function is pow2]** |
| 40–42 | Noise Flt Cutoff / Resonance / KeyTrk | `+1460`, `+1464`, `+1468` | cutoff Hz ≈ `35·2^(9v)` **[HYP]** |
| 43–98 | Gen A…D (Release Loop, Tempo Sync, Rate, VelTrk, KeyTrk, PWM…, Detune/Panning Range, Mix, Pan, Mute), Morph | sub-objects (`MVC0`/`MSP0`/`MML0`) | — |
| 99–122 | Eg1…Eg4 (Release Loop, Tempo Sync, Rate, KeyTrk, VelTrk, Velocity) | `MEG0` | — |
| 124 | **Polyphony** | A3 | integer (default 31; data 15, 11, 0 = mono **[HYP]**) |
| 125 | **Glide Mode** | A2 | integer (0/1/2) |
| 126, 127 | Noise Loop Mode / Noise Flt Mode | `+168`, `+1456` | integers |

**There is no main filter nor classic oscillators**: the timbre is the additive spectrum (up to 128 harmonics) of
up to 4 mixed generators (and "morphed" by X/Y along the `MML0` line), plus filtered noise and effects.

### 3.3 What the 7 demo channels contain **[DATA `mor.py`]**

| Channel | Active generator(s) | Average spectrum (weighted by duration × gain) | Master ADSR |
|---|---|---|---|
| Cuebur "Synth 03" / "Lead Synth" (identical) | A (mix 0.425), 99 frames | 81 % of the energy in the 1st harmonic, 87 % in the odd ones, 12 harmonics > 5 %, slope −1.3 → **almost sine/triangle** | A 0, D 0.19 s, S 0.62, R 3 ms |
| DEDZII "Morphine" | A, 47 frames | bright (centroid 14, 125 harmonics), slope −0.6 → **saw/noisy** | — |
| LeriQ "Morphine #2" | A, 147 frames | centroid 27, flat → **very bright noise/pad** | — |
| LeriQ "Morphine #4" | A with **25 keyboard zones**, 11 frames each | slope −0.5…−1.1, weak 1st harmonic → **soft saw** | — |
| LollieVox "Soft Pad" | A (mix 1.0, 85 frames) + B (0.425, 393 frames) | centroid 4…5, slope −0.7…−1 → **saw** | A 0.92 s, D 7.5 s, S 0.62, R 1.45 s |

---

## Recommendations for LYNX

1. **MIDI Out → plugin (port).** On import, read the MIDI Out (`version @0`, `channel @4`, `port @24`, program
   `@30`, bank `@12/@16`) and mark the channel as a **muted note source**. Build the list of destinations per port:
   wrappers (channel or slot) with `record 0x01.inputPort == port` and Pitchers with `int(256·p33) − 1 ==
   port`. On playback, copy each note of the MIDI Out channel to these destinations, with the MIDI Out's MIDI channel
   (do not filter by channel). Currently `FlWrapper.cpp` discards record `0x01`: read the two `i32` and store them in
   `ForeignPlugin` (`midiInPort`, `midiOutPort`), keeping the blob opaque for the *round-trip*.
2. **Pitcher in MIDI mode** (Bien Duro): when the MIDI Out on the same port has notes, use the notes as the
   tuning target (harmonisation/correction to the MIDI note). If LYNX does not have this, log "Pitcher
   follows MIDI Out port N" and pass the audio straight through.
3. **Keyboard Controller** → new control source (like the Envelope Controller from round 15): `value =
   keys[lastNote]`, smoothed; `0xE3` links with source `channel<<16 | 0x8000`.
4. **Love Philter — parser.** Inflate zlib from byte 4; version = high word if > 0xFFFF. Read globals (6
   i32), the 31/32/33 B block, `u8 + u32` (v ≥ 2), text (v < 6: i32+bytes; v ≥ 6: u8 + UTF-16), 8 units
   (12 i32, i32 mode, i32 type, 3 u8, WS envelope, u8, 8 articulators) and the final `u8` (v ≥ 12). Articulator =
   `i32, 3 f32, 10 × (envelope + 4 i32), envelope LFO + 4 i32 (+1 if LFO version ≥ 3), 3 maps`. Reuse
   `readEnvelope` from `FlSytrus.cpp`. Validated on 96/96 blobs by `tmp/r16/lp.py`.
5. **Love Philter → LYNX effects** (replaces the blind mapping `Fruity Love Philter → filter` in
   `FlpImport.cpp:91`): for each enabled unit, `filter`/`parametricEq` + `utility` as in §2.7.
   Scales: cutoff per §2.5 with k = 1 (44.1 kHz); LP/HP Q = `0.1 + 0.02495·(2001^r − 1)`; SVF: use the type
   from the largest of L/B/H and `Q ≈ 1/d` with `d = 1 − 0.9r`. More than one enabled unit with NEXT = 0 → `parallel`
   (each branch = unit); NEXT ≠ 0 → in series, with the NEXT gain in the branch. Soloed unit → only that one.
6. **Love Philter — Mod X/Y and automation.** Store `x = ModX/1024`, `y = ModY/1024` and, per unit, the X/Y maps
   of the **volume, cutoff, resonance and pan** articulators (the only ones used in the demos). On playback (or
   in automation conversion):
   - **Y→volume** map as a `0→1` line (8 cases; *sidechain* by Envelope Controller at `k = 7`) → `utility`
     gain = `y` (with the round 15 link driving that gain);
   - straight X→cutoff / Y→res maps → cutoff `c = p1/256 + (map(x) − 0.5)` (same for res);
   - stepped X→volume map (gate) → gain = `map(x)` evaluated on the curve (accumulated Δx);
   - automation/link at `k`: 6/7 = Mod X/Y; `k = 8 + 44u + i` (i < 12) = parameter i of unit u (table
     §2.4; normalise the 0…1 link value to the raw range: e.g. VOL `v = 160·value`; CUT `256·value`); `k ≥
     8 + 44u + 12` = articulator LFO (can be ignored: no data uses it).
7. **Morphine → 3-oscillator synth** (approximation):
   - Read `MPR0` (§3.1) and the `0x5C4` block (§3.2). Volume = `48·A0 − 24` dB; transpose `int((A5−0.5)·48)`; fine
     `int((A6−0.5)·200)`; **master ADSR** `A = 15·A9^3.5`, `D = 30·A10³`, `S = A11²`, `R = 10·A12³` s (min.
     3 ms); `mono = A3 ≤ 1` **[HYP]**, `port = A1 > 0` with time ∝ A1, bend range `int(12·A8)`.
   - For each generator with some non-zero frame (up to 3, by largest Mix): compute the average spectrum of the frames
     weighted by `duration × gain` and classify: 1st-harmonic energy ≥ 0.7 and ≤ 6 harmonics > 5 % →
     **sine**; ≥ 80 % of the energy in odd harmonics → **square** if the log-log slope ≥ −1.4, otherwise **triangle**;
     otherwise → **saw**. Oscillator level = generator Mix (`MVC0[0]`).
   - Brightness: energy centroid of the spectrum `C`; an ideal saw (1/n) gives `C ≈ 3.3`. If `C < 3.3`, use an LP
     with *key tracking* `fc ≈ f0·8·C/3.3`; otherwise filter open. Noise (Noise Mix A36 > 0 and not muted) →
     noise oscillator with LP at `35·2^(9·v40)` Hz **[HYP]**.
   - The evolution of the frames (per-frame gain) can become the LYNX filter/volume envelope when the gain falls
     monotonically (e.g. "Lead Synth": 1.0 → 0.57). Log that the timbre is approximated.
8. **Unsupported count:** `Fruity Keyboard Controller` is already excluded from the counter in
   `FlpImport.cpp:851`; include `MIDI Out` in the same exception (it is not an instrument).

## Evidence index

| Function / data | What it does | Marker |
|---|---|---|
| MIDI Out `FUN_006fde10` (VMT+3 of `TTestPlug` `0x6fd778`) | v7 layout: channel, bank, page, port, flags, program + 72 controls, definitions, pages, reset options | DECOMP |
| MIDI Out `FUN_006fec00` | Routing word = channel + port·2²⁴ | DECOMP |
| MIDI Out `FUN_006feb10`, `FUN_006fd900` | Program change (`0xC0`, program−1), bank CC 0/32 | DECOMP |
| MIDI Out `FUN_006fe4c0` | Defaults; initial channel from a global counter that skips channel 10 | DECOMP |
| MIDI Out `FUN_006fdb80` | Host changes channel/port (opcode 0x10) | DECOMP |
| MIDI Out DFM (`PortSelect`, `ChanSelect`, `BankMSB/LSB`, `PatchSelect`, `SendPanVolBox`, `MapColToChanBox`) | Field names and ranges | STR |
| Bien Duro: MIDI Out port 0/1 ↔ Pitcher p33 = 0.00638/0.0112 | Pitcher port = `int(256·v) − 1`; routing by port | DATA |
| `scanw.py` (all of `Data`) | 23 wrappers, all with ports −1; no wrapper on a channel | DATA |
| Keyboard Controller `FUN_006fcff0` | v0/v2/v3 layout (132 per-key values, smoothing, note off) | DECOMP |
| Love Philter `FUN_008551e0` | Version (high word), zlib, globals, options, text, 8 units, final byte | DECOMP |
| Love Philter `FUN_00856780` | Defaults; parameter numbering (base 8 + 44u, 12/11 + 32) | DECOMP |
| Love Philter `FUN_008584e0`, `FUN_008579e0`, `FUN_00858100`, `FUN_00858680` | Index → (unit, articulator, field); text (IEF ms `12.5·(81^(v/16384) − 1)`) | DECOMP |
| Love Philter `PTR_u_panning_008d19c0`, table `0x8d18c0` | Order of the 8 articulator targets; 12 filter names | DECOMP/STR |
| Love Philter `FUN_00858af0`, `FUN_00858e60`, `FUN_008590b0` | Scales: `/256`, `/128`, `curve11`, Mod X/Y `/1024` | DECOMP |
| Love Philter `FUN_0085d110` + processing loop (`unidade+0x3C/+0x40`) | Input = IN + NEXT of the previous unit | DECOMP |
| Love Philter `FUN_00859100` + constants `0x85a740…` | Cutoff/Q curves per type | DECOMP (Hz HYP) |
| Love Philter `FUN_00855080` | DQOver/HQ/SMOOTH/CENTER controls ↔ fields | DECOMP |
| 96 Love Philter blobs (`lp.py`) | Complete parse with no leftover, versions 2…12 | DATA |
| `lpstat.py`, `lplinks.py` | X/Y maps used; links/clips at k = 6, 7, 9, 10, 17 | DATA |
| Morphine `FUN_18010eb40`, `FUN_180113460`, `FUN_18012ad30`, `FUN_1800ec160`, `FUN_1800ef900`, `FUN_1800ef440` | `MPR0/MVC0/MZH0/MSP0/MBP0` tree, partial encoding | DECOMP |
| Morphine `FUN_18010e340` (get) / `FUN_18010dc50` (set), `FUN_18010a2a0` (names), `FUN_180109ef0` (text) | Index → field; level, transpose, fine, EQ, delay, chorus scales | DECOMP |
| Morphine `FUN_1800fffa0`, `FUN_180100620` | Master ADSR: `15v^3.5`, `30v³`, `v²`, `10v³` s | DECOMP |
| 7 channels + Morphine `.fst` (`mor.py`) | Tree closes; average spectra | DATA |

### Open questions

- MIDI Out: field `+8` (`P+0x124`, always −1); whether FL also delivers the notes to native generators that have a
  port in the native wrapper options (where that is saved); what "Map note color" does with colours > 15.
- ZGameEditor Visualizer: where it stores the MIDI port (template "Video chopping", port 1).
- Love Philter: exact `k` factor (`unidade+0xC4`, comes from the oversampling object) and whether the SVF runs oversampled;
  details of Lime and Mango; how *alternate* spreads the cutoff across stages; the exact scale of the
  offset of the bipolar maps (assumed ±0.5 in `c`); the LFO (period per the round 11 §1.6 formula?);
  order of the 3 menu bytes of the global block; info text with ≥ 255 characters.
- Morphine: time unit of the frames (relative `duration` × *Gen Rate*); meaning of `MBP0[1]` and of the
  `MVC0` options/`MSP0` tail; keyboard zones (`i32` = width in keys? sum ≠ 128 in "#4");
  `MML0`/`MMD0`/`MEG0` (morph X/Y and Eg1…4) not decoded; role of Polyphony = 0.
