# FL Studio 2026 behavioural analysis: round 11 (REA) — Sytrus (and a Harmor summary)

> **Nature of this document.** Continuation of `REA_rodada10.md`. *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode has been copied: what is here are
> descriptions, data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence.

- **Targets** (local copies in `tmp/r11/bin/`, originals in `Plugins/Fruity/Generators/<name>/`):
  - `Sytrus/Sytrus_x64.dll` (SHA-256 `d52a97c5…4f15`, Delphi 12 x64, class `TTestPlug`, VMT `0x860da0`)
  - `Harmor/Harmor_x64.dll` (SHA-256 `23673ad8…5aba`, Delphi 12 x64, class `TTestPlug`, VMT `0x86dbd0`)
- **Data:** the same 165 `.flp` files from `FL Studio 2026/Data` + all `.fst` files in `Data/Patches`, channel
  section (before the first `0xEC`), `0xD5` after `0xC9`. Sytrus: **268 channels in 26 `.flp`** + 997 `.fst`
  (1265 blobs). Harmor: **100 channels in 21 `.flp`** + 329 `.fst` (429 blobs).
- **Reference parser** (Python, ours): `tmp/r11/syt.py` (Sytrus, validates the 1265 blobs with 0 bytes
  left over), `tmp/r11/harm.py` (Harmor), `tmp/r11/ana.py` / `show.py` / `stats.py` (analysis).
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Same method as round 10: `TTestPlug` VMT found via the "self" pointer at VMT − 0xC8; entry **[3]
= `SaveRestoreState`** (Sytrus `FUN_00862d70`, Harmor `FUN_0086f2f0`), **[6] = `ProcessParam`** (Sytrus
`FUN_00867930`), [4] = parameter names/texts (Sytrus `FUN_00868ed0`). Ghidra 12.1.4 headless
(project `/tmp/r11gh`, scripts in `tmp/r11/gh/`). Runtime constants (`ln 11`, `ln2/1200` …)
initialised in Sytrus `FUN_0062e5f0`.

---

## 1. Sytrus: format overview **[DECOMP+DATA]**

Sytrus has **10 "units"** in its state, each in a 0x900-byte record starting at `P+0x518`:
**0–5 = operators 1–6, 6–8 = filters 1–3, 9 = effects**. Each unit has 10 integer parameters and a
list of **articulators** (envelope + LFO + mappings). All the routing lives in a **double 9×9 matrix
(FM and RM)**.

### 1.1 Header and compression

| Off | Type | Content |
|---|---|---|
| 0 | u32 | version. If > 0xFFFF, the real version is `value >> 16` (FL 2026 writes `03 00 19 00` = **0x00190003 → v25**). In the data: v5…v25 (`.flp`: v22 = 78, v23 = 9, v24 = 132, v25 = 49) |
| 4 | … | **v ≥ 5: the rest is a zlib stream** (`78 9C …`); decompressed it is ~38–65 KB. v < 5: raw (does not occur in the data) |

The compressed blob is 977 B…13 KB (median 1.3 KB). Everything below is an offset **inside the decompressed
stream**.

### 1.2 Layout of the decompressed stream (v25; per-version deviations in parentheses)

| Order | Size | Destination | Content |
|---|---|---|---|
| 1 | **962 B** (v<2: −162; v<1: −9) | `P+0x12C` | main block, §1.3 |
| 2 | 4 (v>3) | `P+0x4F0` | integer (almost always 0) |
| 3 | 1 (v>5), 1 (v>6), 1 (v>12) | `P+0x4F4..0x4F6` | global flags (0x4F6 is copied to each operator: *band-limit* quality **[HYP]**) |
| 4 | 1/5/8/9 (v>13 / >14 / >17 / >18) | `P+0x4F7..` | flags: **byte 1 = PORTA** (button `PortaBtn`, Tag 596 → parameter 0x254) **[DECOMP]**; byte 8 = alternative *unison pitch* curve (§1.8) |
| 5 | 3 × 23 | `P+0x5F30 + 0x38·k` | 3 delay units: 5 × i32 (feedback, time, stereo offset, volume, mode) + 3 bytes (on, tempo, serial) **[DECOMP+STR]** |
| 6 | 37 (v>10: 38; v<10: absent) | `P+0x5FEC` | reverb: 9 × i32 + flags (defaults 75, 40, 0, 40, 100, 15, 40, 2, 25 = lowcut, highcut, predelay, room, diffusion, decay, damping, colour, wet) **[DECOMP]** |
| 7 | 4 + 3 × 16 (v ≤ 13: 3 × 12) | `P+0x61CC`, `P+0x6018` | EQ mode + 3 EQ bands (level, freq, width, type) |
| 8 | var. | — | **info text**: v ≥ 23: `u8 n` (255 → `u32 n`) + n UTF-16 characters; v < 23: `i32 n` + n bytes (RTF) **[DECOMP]** |
| 9 | var. | 10 units | §1.4: for each unit u = 0…9: **10 × i32 parameters**; op (u < 6): 0x828 B block; filter (6–8): 0x30 B + *waveshaper* curve + 1 B; then the articulators |
| 10 | 1 (v>23), 1 (v>24) | `P+0x500`, `P+0x5F18` | final flags |

### 1.3 Main 962 B block (offsets relative to the start of the stream)

| Off | Content | Conversion |
|---|---|---|
| 0x000 | i32 **Master level** (Tag 0, 0…160, def 128) | **gain = (11^(v/128) − 1)/10** (128 → 1.0; 160 → 1.9) **[DECOMP `FUN_0086a1d0`]** |
| 0x004 | i32 Master LFO level (Tag 1, ±128) | sign·(11^(\|v\|/128) − 1)/10 (multiplies the depth of all LFOs) |
| 0x008 | i32 **Master pitch** (Tag 2, ±24) | **semitones** (internally ×100 = cents). Data: 0 (187), −12 (43), −24 (14), +12 (6) |
| 0x00C | **FM matrix** 9 × 9 i32 | §1.5 |
| 0x150 | **RM matrix** 9 × 9 i32 | §1.5 |
| 0x294 | 8 × i32 (Tags 3–10) | Volume attack/decay/sustain/release, Filter attack/decay/sustain/release: global scales applied to the envelopes (128 = ×1; sustain ±128 = offset) **[STR; scale HYP]** |
| 0x2B4 | 7 × i32 (Tags 11–17) | **unison**: order (1–9), pan (0–128), volume (16–128), pitch (0–256), sub (0–96), phase (0–128), envelope variation (0–192) — §1.8 |
| 0x2D0 | 9 × i32 | effects unit parameters (pan, chorus order, depth, speed, delay, spread, cross, volume, send) |
| 0x2F4 | i32 | −1 (FX *send* selection) |
| 0x2F8 | 2 × i32 | Mod X / Mod Y (0…512, def 256) |
| 0x300 | 8 × i32 | internal state (float 0.953…, flags) — ignore |
| 0x320 | **162 bytes** | **per-cell "mute"** of the two matrices (same indexing); byte ≠ 0 ⇒ the cell is 0 **[DECOMP `FUN_008650d0`]** |

### 1.4 Units (10 parameters + blocks)

**Operators (u = 0…5)** — `p[0..9]` (Tags of the `OpPanel` panel):

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 0 | Starting phase | 0…256 (0) | initial phase = v/256 cycle |
| 1 | **Volume** | ±256 (256) | **linear gain v/256** (negative inverts the phase) **[DECOMP `FUN_00869e30`]** |
| 2 | **Pitch envelope/LFO amount** | 0…4800 (1200) | **cents**: range of the "pitch" articulator (§1.6) |
| 3–9 | — | 0 | unused |

Operator block (0x828 B, v ≥ 17; v 8–16: 0x428 with 128 harmonics; v < 8: 0xE7 with 24) — offsets
relative to the start of the block:

| Off | Type | Content |
|---|---|---|
| 0x000 | i32 | **Freq ratio × 20000** (20000 = 1.0; 10000 = 0.5; 40000 = 2; 0 = offset only). Data (carriers): 0.5 (199), 1 (144), 0.25 (120), 0 (92), 2 (22) **[DECOMP `FUN_0086a730` divides by 20000; DATA: `Default.fst` = 20000]** |
| 0x004 | i32 | **Freq offset in Hz × 100** (±999999 → ±9999.99 Hz); with ratio 0 = **fixed frequency** **[DECOMP]** |
| 0x008 | i32 | (0/128; no identified role) |
| 0x00C | 256 × f32 | **amplitude of harmonic k+1** ("harmonics" table). Default: only the 1st = 1 (sine) |
| 0x40C | 256 × f32 | phase of harmonic k+1 (fraction of half a cycle) |
| 0x80C | 5 × i32 | **shape**: Shape (0…512), Tension (±128), Skew (0…128), Sine shaper (0…128), Pre-filter (0…128, def 128) **[DECOMP `FUN_00847df0`]** |
| 0x820 | 8 × u8 | flags: 0 Half, 1 Even, 2 Absolute (**[DECOMP]**); 3–7 = Declick/Pluck/Center/Global… (**[HYP]**, order not confirmed) |

**The operator waveform** (`FUN_00847df0` + `FUN_00847be0`) **[DECOMP]**: the table is summed
harmonic by harmonic, but each "partial" uses a **base shape** chosen by `Shape`:
`x = Shape/128` →
- `x = 0`: sine;
- `0 < x ≤ 1`: sine → **triangle** blend (weight x);
- `1 < x ≤ 2`: triangle whose peak slides until it becomes a **sawtooth** (x = 2 ⇒ 256 = saw);
- `2 < x ≤ 3`: saw → trapezoid → **square** (x = 3 ⇒ 384);
- `3 < x ≤ 4`: increasingly narrow **pulse** (width `(1 − 0.75·(x−3))/2` of the period; 512 ⇒ 12.5 %).

Then come the *sine shaper* (fold `sin(v·0.0368·s)`), the tension (exponential curve over the amplitude) and the
pre-filter (windowed harmonic cut). In the 1265 blobs, 6636 of the 7590 tables are "fundamental only";
the timbre then comes from `Shape` (0 in 86 %; 256 = saw is the 2nd most common value) **[DATA]**.

**Filters (u = 6…8)** — `p[0..9]` (panel `FilterPanel`) **[DECOMP `FUN_00869e80` + STR]**:

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 0 | **Filter mode** (order) | 0…5 (1) | 0 OFF, 1 x1, 2 x2, 3 x3, 4 ALT x2 (dual/quad), 5 ALT x3 (triple/penta) — no. of cascaded stages |
| 1 | **Cutoff envelope / LFO amount** | 0…256 (128) | `a = v/128` (0…2), depth of the "cutoff" articulator |
| 2 | **Cutoff** | 0…256 (128) | `c = v/256` (0…1), normalised position (§1.7) |
| 3 | **Resonance / bw / feedback** | 0…256 (0) | `r = v/256` |
| 4 | Low / gain / drive | 0…128 (128) | gain (11^(v/128) − 1)/10 |
| 5 | Band | 0…128 (0) | same |
| 6 | High | 0…128 (0) | same |
| 7 | Waveshaper pre amp | 64…192 (128) | v/128 |
| 8 | Waveshaper mix | 0…128 | v/128 |
| 9 | **To next filter** | ±256 (0) | sign·(11^(\|v\|/256) − 1)/10 (filter k → filter k+1 in series) |

Filter block (0x30 B, v ≥ 9): +0x00 flags (HQ/FLAT), +0x04…+0x24 float copies of the parameters,
+0x28 i32 *waveshaper* on, **+0x2C i32 = filter type** (0…12) **[DECOMP+DATA]**. Then the
*waveshaper* curve (an envelope §1.6, `TCustomMEnvelope` format) and 1 byte (v ≥ 12:
uni/bipolar mode).

**Filter types** (name table at `0x8FC850`, same order as the index) **[STR+DECOMP]**:
0 **Chocolate state variable** (uses Low/Band/High as an LP/BP/HP mix), 1 Lime low pass, 2 Lime high pass,
3 Vanilla low pass, 4 Vanilla band pass, 5 Vanilla band stop, 6 Vanilla high pass, 7 Vanilla all pass,
8 Vanilla low shelf, 9 Vanilla peaking, 10 Vanilla high shelf, 11 Mango low pass, 12 Cherry phaser.

**Effects (u = 9)**: the 10 `p` are always 0 (the real parameters live at `0x2D0` of the main block).

**Articulators**: after the blocks, each unit writes its fixed list of articulators (§1.6):
**operator = 6** (0 panning, 1 **volume**, 2 input modulation, 3 **pitch**, 4 phase, 5 plucked damp),
**filter = 8** (0 panning, 1 volume, 2 **cutoff**, 3 resonance, 4 low/gain, 5 band, 6 high, 7 waveshaper),
**effects = 2** (panning, volume) **[STR (table `0x8FC9D0`) + DATA (count that closes the 1265 blobs)]**.

### 1.5 The modulation matrix **[DECOMP `FUN_0086dd40`, `FUN_00865120`, `FUN_008652a0` + DATA]**

`M[layer][row][column]`, i32 ±256, layer 0 = **FM** (phase), layer 1 = **RM** (ring). **Row =
destination**, column = source:

| Row (destination) | Columns 0–5 | Column 6 | Column 7 | Column 8 |
|---|---|---|---|---|
| 0–5 (op 1–6) | FM/RM received from op j (j = row ⇒ **feedback**) | op **PAN** | **FX** (send to rack) | **OUT** (direct output) |
| 6–8 (filter 1–3) | **audio input** coming from op j | filter PAN | FX | **OUT** |

- Cell conversion: `g = sign(v)·(11^(|v|/256) − 1)/10` (0…1). **FM**: index = **4·g** (table phase
  units; **[HYP]** cycles ⇒ ≈ 25·g rad); feedback (j = row): **g·35/128**. **RM**: the carrier is
  multiplied by `(1 − |g|) + g·modulator`.
- PAN = v/512 (±0.5 **[HYP: ±0.5 = L/R extremes]**); OUT and FX use the `g` curve.
- Final gain of an operator at the output = `g(OUT) · p[1]/256 · volume envelope`.
- Filter k → filter k+1 also via `p[9]` (*to next filter*).

Examples **[DATA]**: `Sytrus/Default.fst` = only `M[0][0][8] = 256` (op 1 sine straight to the output);
`Default subtractive.fst` = op 1 with Shape 256 (saw) → `M[0][6][0]` → filter 1 (Vanilla LP, x2,
cutoff 64/256, res 87/256, cutoff envelope A 0 / D 2 s / S 0.5) → `M[0][6][8]` OUT.

### 1.6 Articulators, envelopes and LFOs **[DECOMP `FUN_0078b4a0`, `FUN_0078bb60`, `FUN_00861180`, `FUN_00781be0`, `FUN_00789d10`, `FUN_0078a190`]**

The format is that of IL's generic envelope editor (classes `TCustomMEnvelope`/`TMEnvelope`/`TMLFO`/
`TMMap`, the same ones as in Harmor and, probably, in Fruity Envelope Controller).

**Articulator** (`TOpArticulator`) = `i32 version (3)` + `i32 flags` + `f32 envelope depth` +
`f32 LFO depth` + **8 parts**: envelope, LFO, keyboard map, velocity map, Mod X map,
Mod Y map, random map, unison map. (For filters, the two depths of the cutoff articulator
are recalculated from `p[1]`; for all of them, the LFO depth is multiplied by the *Master LFO level*.)

**Envelope (each part)**:

| Field | Type | Content |
|---|---|---|
| kind | i32 | 1 = envelope, 2 = LFO, 3 = map (curve) |
| version | i32 | 3 in current FL (0…2 in old presets) |
| n | i32 | no. of points |
| points | n × 24 B (v<2: 16 B; v<1: 12 B) | `f64 Δt` (time since the previous point), `f64 level` (0…1), `f32 tension` (±1, curvature), 4 B flags (curve mode: 0 single curve, 1 double, 2 hold, 3 stairs, 4 smooth stairs, 5 pulse, 6 wave, 7–12 variants; arp break) |
| header | 20 B | `+0` flags (bit 0 **TEMPO**, bit 1 **GLOBAL**, bit 2…: others), `+4` **enabled** (LongBool: −1 = on, 0 = off), `+8` **decay point**, `+12` **loop start**, `+16` **sustain point / loop end** (−1 = none) |
| (envelope) | 4 × i32 | articulator **A/D/S/R knobs**: attack time ×(v/128), decay ×(v/128), sustain +v/128, release ×(v/128) (default 128, 128, 0, 128) |
| (LFO) | 4 × i32 + i32 (v ≥ 3) | speed (512…65536), tension (±128), skew (±128), pulse width (±112); + 1 i32 |

- **Enabled**: field `+4` is the *enable*: `−1` in 4,870 edited envelopes, `0` in 72,372 envelopes still at
  default (`(0,0) → (0.75; 1) → (1.5; 0.5) → (0.25; 0)`, sustain at point 2) **[DECOMP `FUN_00787a40`
  (0 ⇒ "stopped" state) + DATA]**. A **disabled** volume envelope = constant gain while the note
  lasts (with the *declick*) **[HYP]**.
- **Time unit**: `Δt` in **seconds**; with the TEMPO bit, in **beats** — the time scale is
  chosen between two values according to that bit **[DECOMP `FUN_00789c10`; unit HYP]**. Consistent with the
  presets: attack/release medians of carriers with the envelope on — Pad 1.99 s / 1.09 s; Brass 0.025 s
  / 0.06 s; Bass 0 / 0.2 s; Percussion decay 0.54 s with sustain 0 **[DATA]**.
- **ADSR reading** (for LYNX): `T_i = Σ Δt`; peak = point of maximum level; **A** = T(peak) − T(0);
  **sustain** = level of the sustain point; **D** = T(sustain) − T(peak); **R** = T(last) − T(sustain).
  No sustain point = "one-shot" envelope (decays to the end even with the key held).
- **LFO**: period = **625 / (10001^(v/65536) − 1)** (v = speed; 40898 → 2.0; 65536 → 0.0625;
  512 → ~8400) in seconds, or in beats with TEMPO **[DECOMP `FUN_0078a510`; unit HYP]**. The LFO
  shape is the point curve itself (one cycle). Enabled = same field `+4`.
- **Pitch** (operator articulator 3): level 0.5 = no deviation; deviation = **(level − 0.5)·2·p[2] cents**
  **[HYP; DATA: 296 enabled pitch envelopes end at 0.5; typical = starts at 1.0 and falls to 0.5 =
  "pitch drop"]**.

### 1.7 Cutoff in Hz and resonance **[DECOMP `FUN_00870820`, `FUN_0086ebe0`; final unit HYP]**

Per voice, the effective cutoff is **`c = p[2]/256 + cutoff articulator output`** (same 0…1 scale;
smoothed `unit+0x890` + `voice+0x630`), and the resonance `r = p[3]/256 + articulator`. The
coefficient computation depends on the **type**; each curve is exponential in `c`. Writing in Hz at 44.1 kHz (the
per-voice scale factor at `voice+0x184` was assumed = 1 at the nominal rate **[HYP]**):

| Types | Cutoff (Hz) | c = 0 / 0.25 / 0.5 / 0.75 / 1 | "Q" from r |
|---|---|---|---|
| 0 Chocolate SVF | `22050·0.0396·(26^c − 1)` (index into a table of 2048 coefficients) | 0 / 1.1k / 3.6k / 9.2k / 21.8k | damping `1 − 0.8·r` (1 → 0.2; FLAT ⇒ squared) ⇒ LYNX res ≈ 0.8·r |
| 1, 2 Lime LP/HP | `22050·(0.007 + 0.01324·(76^c − 1))`, ceiling 0.4·fs | 154 / 724 / 2.4k / 7.4k / 17.6k | `1 + 0.049·(1001^r − 1)` (1…50) |
| 3 Vanilla LP | `44100·(0.003 + 0.000665·(1501^c − 1))`, ceiling ω = 2.85 rad | 132 / 285 / 1.2k / 7.2k / 20k | `0.1 + 0.02495·(2001^r − 1)` (0.1…50) |
| 4–10 Vanilla BP/BS/HP/AP/shelf/peak | `44100·(0.0001 + 0.0004·(1501^c − 1))`, ceiling 20 kHz | 4 / 97 / 670 / 4.2k / 20k | same as Vanilla (shelf/peak use another curve for gain) |
| 11 Mango LP, 12 Cherry phaser | own curves (not tabulated) | — | — |

With more stages (`mode` 2–5) the Q is raised to the power of the number of stages before becoming damping.
Distribution across the demo channels: c = 0.24 / 0.44 / 0.50 / 0.70 / 0.86 at percentiles 10/25/50/75/90
**[DATA]**. Since the articulator adds directly to `c`, the cutoff envelope with depth `a = p[1]/128`
shifts `c` by up to `a` (Vanilla: Δc = 0.25 ≈ 2.6 octaves) **[DECOMP for the sum; HYP for the scale of a]**.

### 1.8 Unison **[DECOMP `FUN_00871fd0` + DFM]**

- Order 1…9 (1 = no unison). In the 268 channels: 1 (164), 2 (43), 4 (18), 3 (17), 5 (10) …
- **Pitch**: `d = (11^(v/256) − 1)·5` cents (max. 50 ct) or, with flag `P+0x4FF` (byte 8 of §1.2/4),
  `(41^(v/256) − 1)·1.875` (max. 75 ct). 128 → ~11 ct. Each voice gets a fraction ±d.
- Pan: `v/128` of spread; volume of the extra voices v/128; sub/phase/variation: ignore in LYNX.

---

## 2. Statistics of the 268 Sytrus channels in the demos **[DATA]** (`tmp/r11/stats.py`)

"Carrier" = op with OUT ≠ 0, or op that feeds a filter whose output reaches OUT (directly or via *to next*).

| Measure | Result |
|---|---|
| No. of carriers | 0 (5), **1 (109)**, **2 (75)**, **3 (48)**, 4 (14), 5 (15), 6 (2) |
| Strong FM (Σ\|g\| > 0.15 over carriers) | 39 channels (21 without filter, 18 with) |
| Light FM (0 < Σ ≤ 0.15) | 44 |
| No FM | 185 (**152 with filter**, 33 without) |
| **"Simple"** (≤ 3 carriers, FM ≤ 0.15, ≤ 1 active filter) | **157 / 268 (59 %)** |
| RM on carrier / feedback | 23 / 11 |
| Unison > 1 | 104 |
| Porta on | 7 |
| Carrier waveform | sine 239, **saw 134**, "rich" (table with many harmonics) 57, triangle 34, "dark" 32, few harmonics 25, pulse 12, square 9 |
| Carrier ratios | 1.0 (157), 0.5 (133), 0.25 (34), 0.125 (30), fixed (14), 2.0 (10), ≈0.502/1.002 (slight detune) |
| Active filters (type, mode) | Vanilla BP (4) x1 36, Vanilla LP (3) x1 28, BP x2 24, Chocolate x0 24, Lime LP x1 22, Chocolate x1 21, BP x3 16, LP x2 16, Vanilla HP 12 |
| Carrier volume envelope | on 431, off 115 |
| Cutoff envelope of active filters | on 81, off 197 |
| Carrier A / D / S / R (p10/25/50/75/90) | A 0 / 0 / 0 / 0.005 / 0.89 s; D 0.06 / 0.4 / 0.75 / 1.05 / 2.0 s; S 0 / 0 / 0 / 0.75 / 1.0; R 0 / 0 / 0.07 / 0.25 / 1.2 s |

Sanity check against the preset names (`Plugin presets/Generators/Sytrus/<category>`; carrier medians):
Bass → pitch −12, ratio 0.25, A 0, R 0.2 s; Pad → A 2 s, S 0.87, R 1.1 s; Percussion → S 0, D 0.5 s;
Organ → S 0.75; Brass → A 25 ms, S 1. "Bass/Sub bass" = 2 sines (ratio 0.5 and 1) straight to the output, sustain
at the 1st point and release 0.2 s; "Bass/Deep" = op 1 (FM from op 2) → Vanilla LP x2 with cutoff 0 and
cutoff envelope A 14 ms / D 8.4 s. All consistent with the conversions above.

---

## 3. Recommendations for LYNX (Sytrus → 3-oscillator subtractive synthesizer)

Parser: `u32 version` (`>>16` if > 0xFFFF) → zlib → 962 B block → optional per-version fields → text →
10 units (§1.2/§1.4) → articulators (6/8/2 per unit, 8 parts each). If something does not add up, log it
and fall back to a default patch (sine + ADSR 0/0/1/0.1).

1. **Classify the carriers** (§2): for each op `i`, `amp_i = g(M[0][i][8])·p1/256` (direct) +
   `Σ_f g(M[0][6+f][i])·g(M[0][6+f][8])·p1/256` (via filter f). Sort by |amp| and **take the 3 largest**
   → LYNX osc1…osc3 (`level = min(1, |amp|)`, negative sign ignored).
2. **Tuning**: `semitones = 12·log2(ratio)` (+ `Master pitch`), split into octave + semitone; remainder in
   cents (`detune`). Ratio 0 with offset f Hz: fixed-frequency oscillator — if LYNX does not support it,
   use the note closest to C5 and note it in the log. Small offsets (≤ 2 Hz, common as "beating"):
   `detune ≈ 1200·log2(1 + f/261.6)` ct.
3. **Waveform**:
   - Table with the fundamental only → by `Shape`: 0 → `sine`; 1…191 → `triangle`; 192…319 → `saw`;
     320…447 → `square`; ≥ 448 → `square` (narrow pulse, no PWM).
   - Rich table: ~1/k → `saw`; odd only ~1/k → `square`; odd ~1/k² → `triangle`; others: `saw` if the
     energy above the 4th harmonic is high, otherwise `triangle` (classifier in `tmp/r11/cls.py`).
   - **Strong FM** on the carrier (Σ\|g\| ≥ 0.15, not counting feedback): replace `sine` with **`saw`** if the
     modulator has a mixed even/odd integer ratio (≈ full spectrum), **`square`** if the
     modulator/carrier ratio is 2 or odd-over-even (odd spectrum), and add a **filter envelope**
     imitating the modulator's volume envelope (opens the LYNX LP by `+4·Σg` octaves, decay = modulator
     decay). Feedback `g > 0.3` on a sine → `saw`. Non-integer ratio → metallic sound: use the 3rd oscillator
     as `sine` at the modulator's ratio with level `min(1, Σg)` (additive approximation).
   - Relevant **RM** (`|g| ≥ 0.2`): use **LYNX AM** (osc3 modulates) with the modulator in osc3.
4. **Pan / level**: LYNX pan = `2·M[0][i][6]/512` (±1). Levels from §3.1 × `Master level` (§1.3).
5. **Amplitude envelope**: from the "volume" envelope (articulator 1) of the loudest carrier, converted as
   in §1.6 (A, D, S = level at the sustain point relative to the peak, R). If it is **disabled**:
   `A = 0, D = 0, S = 1, R = 0.01`. One-shot (no sustain): `S = 0`, `D = time until the last point`. TEMPO
   bit on: multiply by `60/BPM`. Apply the articulator knobs (×v/128) and the global "Volume
   A/D/S/R" (Tags 3–6) as multipliers **[HYP]**.
6. **Filter**: use the **first active filter** (mode ≠ 0, with input from some carrier and OUT/next ≠ 0):
   - type: 1, 3, 11 → `LP`; 2, 6 → `HP`; 4 → `BP`; 5 → `notch`; 0 (Chocolate SVF) → by the largest of
     Low/Band/High (`p4/p5/p6`) → LP/BP/HP; 7 (all pass), 8–10 (shelf/peak), 12 (phaser) → no filter
     (or open LP) and log.
   - cutoff = table from §1.7 by type; LYNX resonance: convert the "Q" of §1.7 with
     `res = clamp((Q − 0.707)/8, 0, 0.95)` (Vanilla r = 0 gives Q 0.1 = very soft LP → res 0); `mode` 2…5
     (more stages) → steeper response: in the LYNX 12 dB SVF, compensate by lowering the cutoff ~10 % per
     extra stage **[HYP]**.
   - carriers that do **not** go through the filter: if they are a minority in level, ignore them; if they are the majority,
     do not apply the filter (LYNX has only one filter).
7. **Filter envelope**: from the "cutoff" articulator (2) of the chosen filter, if enabled: A/D/S/R as in §1.6;
   amount in octaves = `log2(fc(c + a·peak_level) / fc(c + a·initial_level))` using the type's curve
   (§1.7, a = p1/128); LYNX sustain = sustain level relative to the peak. This way LYNX reproduces the
   real opening in Hz, including in patches with cutoff 0 and a strong envelope ("Bass/Deep").
8. **Pitch envelope**: from the "pitch" articulator (3) of the main carrier, if enabled: amount (semitones) =
   `(initial level − 0.5)·2·p2/100`, decay = time until it returns to 0.5, attack/hold 0.
9. **LFOs** (4 in LYNX): for the articulators with an enabled LFO — carrier volume → volume LFO,
   filter cutoff → cutoff LFO, pitch → pitch LFO, panning → pan LFO. Speed = 1/period
   (§1.6), shape `sine` (or `triangle`/`square` if the point curve has 3/2 points with steps).
10. **Unison**: order ≥ 2 and only 1–2 carriers ⇒ fill the free oscillators with the same wave and detune
    `0, +d, −d` (d from §1.8, minimum 5 ct), pan `±p_pan/128`.
11. **Mono / portamento**: porta = byte 1 of `P+0x4F7` (§1.2). Mono/legato come from the FL channel events
    (channel polyphony), not from the blob — keep applying them as for the other generators.
12. **Internal effects** (chorus, 3 delays, reverb, EQ): ignore and log (or map to channel FX,
    if they exist).
13. **Priority order** for implementing: (a) parser + carriers + ratio/pitch + wave from Shape +
    volume envelope (covers the 157 "simple" patches); (b) filter 1 + cutoff envelope; (c) FM → brightness;
    (d) unison and LFOs.

---

## 4. Harmor (summary) **[DECOMP+STR+DATA]**

Harmor is the "big brother" of Harmless (round 10 §1): additive synthesis with 2 parts (A/B), timbres
drawn as per-harmonic curves, 2 "curve" filters, phaser, harmonizer, *blur*, image/
resynthesis and FX rack. The names, ranges and defaults come from the DFM `TSynthEditorForm` (`tmp/r11/harmor.dfm.txt`).

### 4.1 Layout of `0xD5` (`SaveRestoreState` = `FUN_0086f2f0`, parts in `FUN_0086ed30`)

**It is not compressed.**

| Order | Content |
|---|---|
| u32 | version: `03 00 VV 00` → v = `>>16` (data: v19 = 44, v18 = 53, v16/17 = 3 in the `.flp`; v11/12 in old presets) |
| u32 | **mask**: bit 0 = part A present, bit 1 = part B, bit 2 = global section (always 7 in the data) |
| per part | `u32 size` of the block (allows skipping) + **386 × i32 part parameters** (v < 16: 385) + 0x30 B + 0x4B B (v < 11: 0x47) of combos/state + **43 articulators** (§1.6 format with **11 parts**: envelope, LFO and 9 maps) + **26 "shaping" curves** (simple envelopes) + image object + string (image file). In v < 16 articulator 33 is not written |
| global | 53 × i32 (Tags 10000–10016 and 20000–20035, in order) + 6 × i32 + 0x3D B + text (as in §1.2/8) + 1 B flags |

Validation: with this layout, the 682 part blocks with v ≥ 16 close exactly **[DATA]**.

### 4.2 Useful parameters (index in the part array = **DFM Tag**) **[STR+DATA]**

| i | Name | Range (def.) | Use in LYNX |
|---|---|---|---|
| 4 | Timbre 1 & 2 mix | 0…1000 | almost always 0 (timbre 1 only) |
| 11 | **Part volume** | ±2000 (1000) | part level = v/1000 |
| 34 | **Unison order** | 1…9 | unison |
| 37 / 38 | Unison panning (0…256) / pitch thickness (0…256, def 128) | | as in Harmless §1.2 |
| 40 / 41 | **Freq multiplicator** (×10000) / divider | 10000…160000 / 1…9 | part ratio = (v40/10000)/v41 |
| 44 | Pitch articulator amount | ±6000 (6000) | pitch envelope range (ct) |
| 49, 50 | Portamento time / limit | | |
| 52 / 64 | **Filter 1 / 2 frequency** | 0…8192 | same scale as Harmless: `f ≈ 73.4·2^(7.855·v/8192)` Hz **[HYP: inherited]**; 8192 = open (41 % of parts) |
| 56 / 68 | Filter env amount | ±1000 | |
| 57 / 69 | Filter resonance | 0…1000 | `r = v/1000` |
| 74 | Filter 1 & 2 mix (parallel ↔ series) | ±1000 | |
| 75 | Pluck decay | 0…1000 | 1000 = off |

Filter types: integers 13 and 14 of the 0x4B B block (`F1`, `F2`), indices **without separators** of the
DFM list (0 Classic LP, 1 BP, 2 BS, 3 HP, 4 Crude LP … 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP,
17/18 Custom) — in the data F1 = 4 (Crude LP, 60) / 0 (Classic LP, 29) **[HYP]**.

Globals (`g[i]` = Tag 10000 + i): 4 Legato, 5 Portamento, 10 **Part A & B mix** (0…1000, def 500),
11 **Main volume** (0…2000, def 1000), 12 Post-FX volume, **14 Main pitch** (±2400 ct; −2400 in 10 %).

Part articulators (order of the `ArtSelect` combo, without separators): 0 panning, **1 volume**, 2 FX/dry,
3 timbre mix, **4 filter 1 freq**, 5 filter 2 freq, … **24 pitch**, 25 vibrato, 26 unison pitch, …;
mappings 31–42 (F1/F2 env amount, global env A/D/S/R, global LFO …). The 26 *shaping* curves
start with local EQ, harmonic phase, **timbre 1 harmonic level**, **timbre 2 harmonic level** (per-harmonic
level, x = normalised harmonic) **[STR]**.

Data (100 channels): volume envelope enabled in only 45/200 parts (the rest use the gate + "global envelope"
from the mappings); when enabled: A 0 (median) / D 2 s / S 0.5 / R 0.74 s.

### 4.3 Rough approximation in LYNX

- One part = one `saw` oscillator (Harmor's default timbre = saw; if the "timbre 1 harmonic level" curve
  falls only on the odd harmonics → `square`), level = part volume × (1 − A/B mix or mix), pitch = Main pitch +
  `12·log2(ratio)`. Part B in osc2 with the same criterion.
- Filter 1 → LYNX LP/BP/HP/notch by type; cutoff via the Harmless scale; resonance v/1000.
- Volume envelope: articulator 1 if enabled (§1.6), otherwise `A 0 / S 1 / R 0.1`.
- Unison: like Harmless (round 10 §1.7/2).
- Image, *blur*, *prism*, harmonizer, phaser and FX: ignore and log.

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| Sytrus VMT `0x860da0` (`tmp/r11/vmt12.py`) | Locates `TTestPlug` and the envelope classes | DECOMP |
| `FUN_00862d70` (VMT+3) | `SaveRestoreState`: version, zlib, 962 B, flags, delays, reverb, EQ, text, 10 units, articulators | DECOMP |
| `FUN_00867930` (VMT+6), `FUN_00868c90` | `ProcessParam` and the index → (unit, articulator, control) decoder; offsets 0x12C/0x3C0/0x3E0/0x3FC/0x424/0x6018 | DECOMP |
| `FUN_008681e0`, `FUN_00868ed0` | Parameter text/names; tables `0x8FC9D0` (articulators), `0x8FC8C8` (filter modes), `0x8FC850` (types) | DECOMP+STR |
| `FUN_0062e5f0` | Constants `ln 11`, `ln2/1200` … | DECOMP |
| `FUN_0086a1d0`, `FUN_00869e30`, `FUN_0086a730`, `FUN_00869e80` | Master vol/LFO/pitch; op volume and pitch range; ratio/20000 and offset Hz·100; filter parameters | DECOMP |
| `FUN_008650d0`, `FUN_00864ff0`, `FUN_00865120`, `FUN_008652a0` | Matrix indexing + mute; gain curve; PAN/FX/OUT columns; op→filter routing | DECOMP |
| `FUN_0086dd40` | Voice assembly: FM (×4), feedback (×35/128), RM, filter inputs | DECOMP |
| `FUN_00848780`, `FUN_008477f0`, `FUN_00847df0`, `FUN_00847be0`, `FUN_00847900` | Op wave table: harmonics, Shape (sine→tri→saw→square→pulse), tension, sine shaper, band-limit | DECOMP |
| `FUN_0078b4a0`, `FUN_0078bb60`, `FUN_00861180` | Articulator serialisation (8 parts) | DECOMP |
| `FUN_00781be0`, `FUN_00789d10`, `FUN_0078a190`, `FUN_0078b2a0` | Envelope/LFO/map serialisation (f64 points, 20 B header, knobs) | DECOMP |
| `FUN_007849c0`, `FUN_007849a0`, `FUN_00787a40`, `FUN_00789de0`, `FUN_00789c10` | Header defaults; enabled (+4); decay/loop/sustain points; knobs/128; TEMPO bit | DECOMP |
| `FUN_0078a510`, `FUN_00818400` | LFO speed → period; display in time/bars with TEMPO | DECOMP |
| `FUN_00871fd0`, `FUN_00865a90` | Unison (pitch 11^/41^, pan, volume); reverb defaults | DECOMP |
| `FUN_00872290`, `FUN_00870820`, `FUN_0086ebe0` | Cutoff/res smoothing; base + articulator sum; coefficients per filter type (curves 26^, 76^, 1501^; Q 2001^/1001^) | DECOMP |
| Sytrus DFM `TSynthEditorForm` (`tmp/r11/sytrus.dfm.txt`) | Tags, ranges and defaults; articulator parts | STR |
| 1265 Sytrus blobs (268 in `.flp`) | Layout closes in all of them; §2 statistics; sanity by preset category | DATA |
| Harmor VMT `0x86dbd0`; `FUN_0086f2f0`, `FUN_0086ed30` | Per-part/global layout, versions | DECOMP |
| Harmor DFM (`tmp/r11/harmor.dfm.txt`) + 429 blobs | Index = Tag; 11-part articulators; §4.2 statistics | STR+DATA |
| Scripts `tmp/r11/syt.py`, `ana.py`, `show.py`, `stats.py`, `cls.py`, `harm.py` | Parser, analysis and statistics | DATA |

### Open questions

- Sytrus cutoff (§1.7): confirm the per-voice factor `voice+0x184` (= 44100/fs? 1/oversampling?), the
  Mango/Cherry curves and the scale of the cutoff envelope depth `a`.
- Unit of the FM index (cycles × radians) and tuning reference of ratio 1.0 (MIDI note ↔ Hz).
- Time units of the envelopes (seconds without TEMPO is the most likely; confirm with a render).
- How the global "Volume/Filter A/D/S/R" knobs (Tags 3–10) and the articulator A/D/S/R knobs scale
  time (linear v/128 assumed).
- Meaning of operator flags 3–7 (declick, pluck, center, global) and of the i32 at +0x008 of the op block.
- Filter with `mode = 0` (OFF): does it pass through unfiltered or go silent?
- Harmor: filter frequency scale (assumed equal to Harmless), meaning of integers 15/16 of the
  0x4B block, contents of the image object, and the default timbre (confirm it is saw).
- Suggested validation: render in FL `Sytrus/Default subtractive`, `Bass/Sub bass`, `Bass/Deep`,
  `Pad/*` and compare with LYNX (envelope and spectrum); sweep the cutoff from 0 to 256 on a saw patch
  to measure the Hz curve.
