# FL Studio 2026 behavioural analysis: round 7 (REA) — saved state of the native mixer effects

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md` and of rounds 2–5 (round 6,
> done in parallel, covers the mixer track EQ/LFO in FLEngine and is not touched here). *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode has been copied: what is
> here are descriptions, data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence.

- **Targets** (all in `Plugins/Fruity/Effects/<name>/`, Delphi, Ghidra 12.1.4 via REA):

  | Plugin | DLL (SHA-256) | Plugin VMT | `SaveRestoreState` | Parameter text |
  |---|---|---|---|---|
  | Fruity Parametric EQ 2 | `871b8396…54f4` | `0x6ebb68` | `FUN_006ec340` | `FUN_006efda0` |
  | Fruity Limiter | `ed81447b…b082` | `0x6d30c0` | `FUN_006d3760` | `FUN_006d5380` |
  | Fruity Reeverb 2 | `bc24b17d…97a1` | `0x6d4338` | `FUN_006d4920` | `FUN_006d5480` |
  | Fruity Delay 3 | `b9d64b09…fbd1` (+ `engine_x64.dll` `0b05f78f…8891`) | `0x6e87d0` | `FUN_006e8da0` | delegated to the C++ engine |
  | Fruity Delay 2 | `d9a7674d…f93d` | `0x6bf030` | `FUN_006bfa00` | `FUN_006c0490` |
  | Fruity Compressor | `f4021492…81d1` | `0x6bd5f8` | `FUN_006bd9a0` | `FUN_006be220` |
  | Fruity Soft Clipper | `50aac728…72c6` | `0x6be3f0` | — | `ProcessParam` `FUN_006bee60` |

  The VMT follows the same order as in round 4 (entry 3 = `SaveRestoreState`, 4 = `GetName` → parameter
  text, 6 = `ProcessParam`). The DFM forms (`TSynthEditorForm`) were read by our own `TPF0`
  parser: each control's `Tag` is the parameter index, `Min/Max/DefaultValue` the range.
- **Format validation:** 165 `.flp` files from `FL Studio 2026/Data`, mixer section (after the 1st `0xEC`):
  `0xC9` (name) → `0xD5` (blob) → `0x62` (closes the slot). Actual slot counts: **PEQ2 1,138**
  (1,117 with a UTF-16 name + 21 with the name **in ASCII** in an old `.flp` — the importer must accept both),
  **Limiter 346**, **Reeverb 2 333**, PanOMatic 193, Delay 3 110, Compressor 88, Stereo Shaper 77,
  Delay 2 62, Balance 57, Flangus 56, Fast Dist 53, Filter 32, Soft Clipper 28.
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Conventions: everything little-endian; `i32` signed; `P+0xNN` = offset into the plugin object; "parameter *i*" = host
index (= the control's `Tag` in the DFM). Pattern common to almost all of them: **the blob is a raw copy of the plugin's
integer parameter vector** (`P+0x120…`), sometimes preceded by a version; the saved value is the same
integer the knob holds internally (DFM range), and the conversion to the real unit is done afterwards.

### Shared curve laws **[DECOMP]**

All static Fruity plugins have the same initialisation unit (`FUN_005f41c0` in PEQ2,
`FUN_005e22b0` in Limiter) that precomputes `ln 2`, `1/ln 2`, `ln 2 / 2`, `ln 601`, `ln 11`, `ln 3`, `ln 7`,
`ln 10 / 20`, `ln 2 / 12`. From these come the "normalised exponential" laws that appear below:

```
Lk(x) = (k^x − 1) / (k − 1)        x = value/scale, Lk(0) = 0, Lk(1) = 1
```

- `L11` ("FL volume"): linear gain = `(11^x − 1)/10`. Used in Reeverb 2 (dry/ER/wet), Soft Clipper
  (post gain); probably also Delay 2/Balance/PanOMatic **[HYP]**.
- `L7`: gain = `(7^x − 1)/6` (Limiter: gain, comp threshold, noise gain/threshold).
- `L3`: gain = `(3^x − 1)/2` (Limiter: ceiling, saturation).
- Time `T(x) = 2·(501^x − 1)` ms (Limiter).

---

## 1. Fruity Parametric EQ 2 (1,138 slots) **[DECOMP+STR+DATA]**

### 1.1 Layout of `0xD5`

| Off | Type | Field | Versions |
|---|---|---|---|
| 0x000 | u32 | **version** (writes 8; demos: 8 ×490, 3 ×328, 6 ×199, 7 ×100, 2 ×21) | all |
| 0x004 | 36 × i32 | **parameters 0…35** (current state, table 1.2) → `P+0x120` | all |
| 0x094 | 36 × i32 | **backup state (A/B compare)**, same layout; ignore | all |
| 0x124 | u32 | UI flags: bit 0 → `P+0x361`, bits 1+ → `P+0x360` | all |
| 0x128 | u32 | `P+0x364` (0 or 1) | all |
| 0x12C | u8 | `P+0x368` (UI checkbox) | all |
| 0x12D | u32 | **Monitor** (0 off, 1 input, 2 output; always 2) | all |
| 0x131 | u32 | bit 0 → `P+0x2078` | v ≥ 3 |
| 0x135 | u32 | `P+0x36C` (2 when absent) | v ≥ 4 |
| 0x139 | u32 | `P+0x374` (0) | v ≥ 4 |
| 0x13D | u8 | bits: 0 → `P+0x370` (**linear phase** **[HYP]**), 1 → `P+0x37C` = **HQ / oversampling ×2** (the analyser rate is multiplied by `1+bit`), 2…7 → display options | v ≥ 4 |
| 0x13E | u32 | **enabled-bands mask** (bit *b* = band *b+1*; default `0x7F`); it is each band's "Mute / solo" button | v ≥ 4 |
| 0x142 | u32 | `P+0x212C` (1) | v ≥ 4 |
| 0x146 | u32 | `P+0x20EC` (0) | v ≥ 5 |
| 0x14A | 5 × u32 | display options (`P+0x2100, 0x2104, 0x2108, 0x20FC, 0x2110`; demos `3,1,2,2,2`) | v ≥ 6 |
| 0x15E | u32 | `P+0x380` (0) | v ≥ 7 |

Sizes: v2 = 305, v3 = 309, v6 = 350, v7/v8 = 354 B — they match the sum above exactly across the 1,138 blobs.
For v < 4 all bands count as enabled. Reading the 36 parameters **does not depend on the version**.

### 1.2 The 36 parameters

| i | Off | Field | Range (DFM) | Default (DFM) | Unit |
|---|---|---|---|---|---|
| 0…6 | 0x04…0x1C | **gain** of band 1…7 | −1800…1800 | 0 | `dB = v/100` (±18 dB) |
| 7…13 | 0x20…0x38 | **frequency** of band 1…7 | 0…65536 | 5777, 13024, 19904, 33145, 42327, 49307, 55825 | `Hz = 20·1000^(v/65536)` |
| 14…20 | 0x3C…0x54 | **width** (BW) | 0…65536 | 40124 (b1, b7), 25412 (b2…b6) | `w = v/65536` (text: `w·100 %`) |
| 21…27 | 0x58…0x70 | **type** | 0…7 | 5, 6, 6, 6, 6, 6, 7 | table 1.3 |
| 28…34 | 0x74…0x8C | **order / slope** | −3…+3 | 0 | table 1.4 |
| 35 | 0x90 | **Main level** | −1800…1800 | 0 | `dB = v/100` |

- Frequency: `FUN_006fbf60` defines the step `log2(1000)/65536`, `FUN_006f49c0`/`FUN_006fbf20` do the forward
  `v = log2(Hz/20) / step`, and `FUN_006ef120` the reverse `Hz = 20·exp(v·step·ln 2)`. Range **20 Hz…20 kHz
  logarithmic**. DFM defaults: 36.8 Hz, 125 Hz, 397 Hz, 658 Hz, 1.87 kHz, 3.57 kHz, 7.19 kHz.
  Many old demos use the uniform grid 10923…54613 (= 63 Hz, 200 Hz, 632 Hz, 2 kHz, 6.3 kHz…).
- Gain: text with 2 decimals + "dB" (`×0.01`, constant `0x3C23D70A`) **[DECOMP]**.

### 1.3 Types **[DECOMP+STR]**

Name table at `0x74E858`; the UI type is remapped to an internal type (`0x74E838`):

| UI | Name | Internal | Uses gain? | Width → biquad parameter |
|---|---|---|---|---|
| 0 | Disabled | 0 | — | band ignored |
| 1 | Low pass (= "high cut") | 1 | no | `Q = 0.1 + 9.9·(1 − w)³` |
| 2 | Band pass | 2 | no | `BW_oct = (0.1 + 4.9·w)·k_bp` |
| 3 | High pass (= "low cut") | 4 | no | `Q = 0.1 + 9.9·(1 − w)³` |
| 4 | Notch | 3 | no | `BW_oct = (0.1 + 4.9·w)·k_notch` |
| 5 | Low shelf | 6 | yes | `S = 1/3 + 1.7193·(1 − w)` |
| 6 | Peaking | 7 | yes | `BW_oct = 0.1 + 4.9·w` |
| 7 | High shelf | 8 | yes | `S = 1/3 + 1.7193·(1 − w)` |

### 1.4 Order (slope) **[DECOMP+STR]**

Text (`0x74E8B0`, indexed from −3 to +3): `Steep 8, Steep 6, Steep 4, 2, Gentle 4, Gentle 6, Gentle 8`.
The number is the filter order (2 = one biquad, 12 dB/oct). With `n = |order|`, the band uses **n+1 cascaded
biquads**; a negative order turns on *steep* mode (`FUN_006ef120`: `P+0x384+0x1F8·band`).

- **Gentle** (n > 0): LP/HP use `Q^(1/(n+1))` in each stage (`FUN_005d1aa0` = power); peaking and shelf
  split the gain: each stage receives `gain/(n+1)`.
- **Steep** (n < 0): LP/HP multiply Q by a Butterworth-like per-stage table (`0x74E758`; n=1:
  1.983/0.749; n=2: 2.643/1.082/0.694; n=3: 3.576/1.142/1.045/0.655); shelves use another table (`0x74E718`);
  peaking spreads the stages in frequency (offsets in octaves `0x74E798`, e.g. n=3: −0.775, −0.3,
  +0.3, +0.775 × BW) with corrected gain (`0x74E7D8`). Detail only needed for fine fidelity.
- Band pass / notch: `k_bp = 0.708·√(n+1)` and `k_notch = 1/k_bp` (`P+0x1EC/0x1F0`, derived from
  `10^(3/20)/10^(6/20)`) **[DECOMP]**.

### 1.5 Filter maths (`FUN_006ee6e0`) **[DECOMP]**

These are the **"Audio EQ Cookbook" (RBJ) biquads**, with `ω = 2π·f/fs` (limited to ≈ 0.999π; 0.7π for
normal peaking; 0.8π for the steep peaking stages):

- LP/HP: `α = sin ω /(2Q)`.
- BP/notch/peaking: bandwidth-in-octaves form, `α = sin ω · sinh( (ln 2 / 2)·BW_oct·ω / sin ω )`.
- Peaking/shelf: `A = 10^(gain_dB/40)` (`0.5·ln10/20` in the exponent).
- Shelf: `α = (sin ω / 2)·√( (A + 1/A)(1/S − 1) + 2 )`; with the default `w` (40124 → 0.612) it gives **S = 1.0**.
- Peaking/shelf with gain exactly 0: the band counts 0.2 s and is then ignored (bypass).
- Main level: output gain in dB (`v/100`) **[HYP: applied as 10^(dB/20) at the end]**.
- There is an alternative "linear phase" path (`FUN_006f1c60`/`FUN_006f1c80`, FFT) used when the flag is
  on; the magnitude response is the same **[HYP]**.

Useful equivalences for LYNX (which works with Q): peaking `Q ≈ 1 / (2·sinh(ln2/2 · BW_oct))`
(default 2 oct → Q 0.667; w = 0 → 0.1 oct → Q 14.4; w = 1 → 5 oct → Q 0.18). Shelf: `1/Q = √((A+1/A)(1/S−1)+2)`.

### 1.6 Usage in the demos **[DATA]**

- Effectively active bands (type ≠ 0, mask bit on, and gain ≠ 0 for peaking/shelf): **Peaking
  2,026, High pass 473, High shelf 458, Low shelf 372, Low pass 256**, Notch 4, Band pass 1.
- Active bands per instance: 0 → 40, 1 → 156, 2 → 353, 3 → 169, 4 → 142, 5 → 104, 6 → 85, 7 → 89.
- Order: 0 in 92%; **−3 ("Steep 8") 537 times** (almost always HP on band 1 and LP on band 7), −1 40, +3 28.
- Median frequencies: HP 152 Hz (20…3.98 kHz), LP 16.1 kHz, low shelf 126 Hz, high shelf 6.6 kHz,
  peaking 1.05 kHz. Gains from −18 to +18 dB; main level 0 in 988/1,138 (range −15.3…+18 dB).
- Mask ≠ 0x7F in 16 instances (muted bands that must **not** sound).

---

## 2. Fruity Limiter (346 slots) **[DECOMP+STR+DATA]**

### 2.1 Layout

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | version (7 ×190, 6 ×156; writes 7) |
| 0x04 | 18 × i32 | **parameters 0…17** (table 2.2) |
| 0x4C | 18 × i32 | backup state (A/B); ignore |
| 0x94 | i32 | display scroll speed (50) |
| 0x98 | i32 | panel **visible tab** (0/1: LIMIT/COMP) **[HYP: UI only; the three sections always process]** |
| 0x9C | 4 × u8 | display buttons (input/output/envelopes/markers) |
| 0xA0 | i32 | `P+0x1BC` (0) |
| 0xA4 | i32 | **sidechain input** (0…3) |
| 0xA8 | u8 | v7 only: bit 0 → `P+0x277` **[HYP: menu option]** |

v ≥ 6 reads 0xA4 bytes after the version; v5 read 0x9D (does not appear in the demos).

### 2.2 Parameters and conversions (`FUN_006d5380`)

| i | Off | Name (DFM) | Range | Default | Conversion |
|---|---|---|---|---|---|
| 0 | 0x04 | Gain | 0…2000 | 1000 | `L7(v/1000)` linear → dB (0 = −∞, 1000 = 0 dB, 2000 = ×8 = **+18.1 dB**) |
| 1 | 0x08 | Soft saturation threshold | 0…1000 | 1000 | `L3(v/1000)` (1000 = 0 dB) |
| 2 | 0x0C | Limiter **ceiling** | 0…2000 | 1000 | `L3(v/1000)` (1000 = 0 dB, 2000 = ×4 = **+12 dB**) |
| 3 | 0x10 | Limiter attack | 0…10000 | 1115 | `T(v/10000)` ms (0…1000; default **2.0 ms**) |
| 4 | 0x14 | Limiter attack curve | 1…8 | 3 | integer (curve tension) |
| 5 | 0x18 | Limiter release | 0…10000 | 5000 | `2·T(v/10000)` ms (0…2000; default **85.5 ms**) |
| 6 | 0x1C | Limiter release curve | 1…8 | 3 | integer |
| 7 | 0x20 | Limiter peak window ("SUSTAIN") | 0…10000 | 2882 | `T` ms (default **10.0 ms**) |
| 8 | 0x24 | Comp threshold | 0…1000 | 1000 | `L7(v/1000)` (1000 = 0 dB; fixed floor) |
| 9 | 0x28 | Comp **ratio** | −1000…1000 | 0 | v > 0: `r = 1 + 0.38·(51^(v/1000) − 1)` → "r:1" (up to **20:1**); v < 0: `r = 1 + 0.06·(51^(|v|/1000) − 1)` → "1:r" (up to 1:4, expansion); 0 = 1:1 |
| 10 | 0x2C | Comp knee | −1000…1000 | 0 | `v/10` % |
| 11 | 0x30 | Comp attack | 0…10000 | 0 | `T` ms |
| 12 | 0x34 | Comp release | 0…10000 | 6666 | `2·T` ms (default **248 ms**) |
| 13 | 0x38 | Comp curve | 1…8 | 1 | integer |
| 14 | 0x3C | Comp RMS window | 0…10000 | 650 | `T` ms (default **1.0 ms**) |
| 15 | 0x40 | Noise gate gain | 0…1000 | 1000 | `L7` |
| 16 | 0x44 | Noise gate threshold | 0…1000 | 100 | `L7` (0 = gate off) |
| 17 | 0x48 | Noise gate release | 0…10000 | 4187 | `2·T` ms (default **50 ms**) |

`T(x) = 2·(501^x − 1)` ms (`FUN_006d3dc0`, constant 501.0); gains are displayed in dB via
`20·log10` (`FUN_005e0ce0`); "ms"/"dB"/"%"/"1:"/":1" are the strings at `0x6D594C…`.

### 2.3 Usage in the demos **[DATA]**

- Parameters changed from default: noise threshold 346 (all 0 = gate off), comp threshold 222,
  ratio 200, ceiling 159, attack 145, gain 136, comp release 93.
- Ceiling 2000 (+12 dB, limiter practically off) in 85; gain 2000 (+18 dB) in 12.
- Ratio 1000 (20:1) in 73; typical comp threshold 64…280 (≈ −33…−18 dB). Knee and curves almost always default.
- Sidechain (0xA4) ≠ 0 in ~150 instances (131 with input 1, 9 with 2, 10 with 3) — "ducking" compression;
  LYNX has no sidechain, so those slots will sound different either way.

---

## 3. Fruity Reeverb 2 (333 slots) **[DECOMP+STR+DATA]**

### 3.1 Layout

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | version: **10001** (271 slots, 66 B) or **10000** (62 slots, 58 B); < 10000 = old unversioned format (reads 0x35 B from the start) |
| 0x04 | 15 × i32 (v10001) / 13 × i32 (v10000) | parameters 0…14 / 0…12 |
| after | u8 | **tempo-based predelay** (`P+0x15C`) |
| after | u8 | **Mid/side input** (`P+0x15D`) |

For v10000, Mod Speed = 684 and Mod Depth = 0.

### 3.2 Parameters (`FUN_006d5480`, `FUN_006d5160`)

| i | Name | Range | Default | Conversion |
|---|---|---|---|---|
| 0 | Low cut | 19…3000 | 75 | Hz; **< 20 = Off** |
| 1 | High cut | 5…221 | 40 | `v/10` kHz (0.5…22 kHz); **≥ 221 = Off** |
| 2 | Predelay | 0…384 | 0 | without sync: `v·1000/384` ms (0…1000 ms); with sync: `v/48` beats (48 ticks/beat, up to 8 beats) |
| 3 | Room size | 1…100 | 50 | integer |
| 4 | Diffusion | 0…100 | 100 | % |
| 5 | Decay | 1…200 | 15 | `v/10` s (0.1…20 s) |
| 6 | High damping | 5…221 | 40 | like High cut (kHz, 221 = Off) |
| 7 | Bass multiplier | 20…300 | 100 | `v/100` (×0.2…×3) |
| 8 | Crossover | 25…2000 | 500 | Hz |
| 9 | Stereo separation | −64…64 | 0 | `s = v/64`; enters a 2×2 wet mixing matrix; with M/S on the cross terms change sign **[DECOMP; exact direction HYP]** |
| 10 | Dry | 0…160 | 128 | gain `L11(v/128)` (128 = 0 dB, 160 = +5.6 dB) |
| 11 | Early reflections | 0…160 | 128 (initial knob 64) | gain `L11(v/256)` (128 → −12.7 dB) |
| 12 | Wet | 0…160 | 128 (initial knob 64) | gain `L11(v/256)` (64 → −21.7 dB) |
| 13 | Mod speed | 0…2048 | 684 | `v/2048` (%) |
| 14 | Mod depth | 0…2048 | 0 | `v/2048` (%) |

The 1/256 scale of ER/wet (versus 1/128 for dry) was read directly from the constants (`0x715234` = 0.00390625,
`0x715230` = 0.0078125) **[DECOMP]**; the `ln 11` base comes from the shared table **[strong HYP]**.
The text shows ER/wet as `v/128` %, so "100 %" in the UI ≈ −12.7 dB real.

### 3.3 Usage in the demos **[DATA]**

Low cut 75 Hz in 71 (Off in 25; up to 2.49 kHz); high cut 4 kHz in 97 (Off in 14); predelay 0 in 227
(with sync: 18, 27, 48 ticks); room 50/100; decay 1.5 s (53), 2.2 s, 4.5 s; damping 4 kHz; bass ×1.0;
crossover 500 Hz; separation 0 or negative (−40, −64); dry 128 in 225 and **0 in 39** (reverb on a send);
wet spread out (64…160). Mod depth > 0 in 81 of the 271 v10001 instances.

---

## 4. Delays

### 4.1 Fruity Delay 3 (110 slots) **[DECOMP layout + STR + DATA; units HYP]**

Layout: `u32 version (1)` + **26 × i32** (`P+0x180`, 0x68 B) = 108 B. The DSP and the text live in a separate C++
engine (`engine_x64.dll`, classes `FLDelay3Plugin`/`ILDelay3`, third-party strings), so the units
below come from the DFM and from statistics.

| i | Name (DFM) | Range | Default | Reading |
|---|---|---|---|---|
| 0 | Input wet | 0…6000 | 6000 | normalised `v/6000` |
| 1 | Delay type | 0…3 | 0 | 0 Mono ("Fake stereo"), 1 True stereo, 2 **Ping pong**, 3 Off |
| 2 | Tempo sync | 0/1 | — | |
| 3 | Keep pitch | 0/1 | — | |
| 4 | **Time** | 1…768 | 80 | sync = 1: **ticks of 1/96 of a beat** (96 = 1/4, 144 = dotted 1/4, 192 = 1/2, 384 = 1 bar) **[DATA]**; sync = 0: unknown unit **[HYP]** |
| 5 | Time R/L offset | 0…6000 | 3000 | 3000 = centre |
| 6 | Time smooth | 0…1000 | 500 | |
| 7 | Stereo spread | 0…6000 | 6000 | |
| 8–10 | Mod rate / time / cutoff | 0…6000 | 3000/0/0 | |
| 11–12 | Diffusion level / spread | 0…6000 | 0/3000 | |
| 13 | Distortion type | 0/1 | 0 | Limit / Sat |
| 14 | **Feedback** | 0…6000 | 3000 | `v/6000` **[HYP: level 0…100 %]** |
| 15 | Filter mode | 0…2 | 0 | LP / HP / BP **[STR/HYP]** |
| 16 | **Feedback cutoff** | 0…6000 | 3000 | 6000 = maximum ("Max Hz") **[HYP log 20 Hz…20 kHz]** |
| 17 | Feedback resonance | 0…6000 | 3000 | |
| 18–20 | Distortion threshold/hardness/symmetry | 0…6000 | 0 | |
| 21 | Rate reduction | 0…60000 | 60000 | 60000 = off |
| 22 | Bits reduction | 0…6000 | 6000 | 6000 = off |
| 23 | **Output wet** | 0…6000 | 6000 | |
| 24 | Output tone | −3000…3000 | 0 | |
| 25 | **Output dry** | 0…6000 | 6000 | 0 in 19 slots (send) |

Demos: type 2 (ping pong) 59, 1 (true stereo) 46; sync on 80/110; feedback 0…5610 (median ~3000).

### 4.2 Fruity Delay 2 (62 slots) **[DECOMP+STR+DATA]**

**No version**: 8 raw × i32 (32 B).

| i | Off | Name | Range | Default | Conversion |
|---|---|---|---|---|---|
| 0 | 0x00 | Input pan | −128…128 | 0 | `v/128` |
| 1 | 0x04 | Input volume | 0…160 | 128 | text `v/160` %; gain **[HYP: `L11(v/128)`]** |
| 2 | 0x08 | Dry | 0…160 | 128 | same |
| 3 | 0x0C | Feedback | 0…127 | 64 | `v/127` (text in %) |
| 4 | 0x10 | **Time** | 1…768 | 144 | **always synced**: `v/48` beats (48 ticks/beat; 144 = 3 beats, up to 16 beats) |
| 5 | 0x14 | Time stereo offset | −512…512 | 0 | `0.5·(1001^(|v|/512) − 1)` %, sign = left/right |
| 6 | 0x18 | Feedback mode | 0…2 | 0 | Normal / Invert / **Ping pong** |
| 7 | 0x1C | Cutoff | 0…128 | 128 | `v/128` (128 = open) **[HYP: same law as the channel filter]** |

Demos: ping pong mode in 53/62; time 192/144/384/96; median feedback 64.

---

## 5. Fruity Compressor (88 slots) **[DECOMP+STR+DATA]**

Layout: `u32 version (1, 2, 3)` + **6 × i32** (`P+0x124`) + `u32 flags` (v ≥ 2) + `i32 sidechain` (v3).
Sizes 28/32/36 B.

| i | Off | Name | Range | Default | Conversion |
|---|---|---|---|---|---|
| 0 | 0x04 | Threshold | −600…0 | 0 | `v/10` dB |
| 1 | 0x08 | Ratio | 4…300 | 10 | `v/10` : 1 (0.4…30) |
| 2 | 0x0C | Gain | −300…300 | 0 | `v/10` dB |
| 3 | 0x10 | Attack | 0…4000 | 150 | `v/10` ms (0…400; default 15 ms) |
| 4 | 0x14 | Release | 1…4000 | 200 | `v` ms |
| 5 | 0x18 | Type | 0…7 | 0 | Hard, Medium, Vintage, Soft, Hard/R, Medium/R, Vintage/R, Soft/R (knee; "/R" **[HYP: RMS/slow detection]**) |
| — | 0x1C | flags | bit 0 | | turns on **lookahead**: reported latency 0 or **64 samples** (`FUN_006bdb90`, table `0x70E378`) |
| — | 0x20 | sidechain | 0…n | | v3 only |

Demos: threshold −13 dB (15), −20 dB, −35 dB; ratio 10:1, 8:1, 6.1:1; gain +13 dB; types Hard 39,
Vintage 18, Hard/R 17; lookahead on in 71/86.

---

## 6. Extras (layout + ranges; conversions partially HYP)

| Plugin | `0xD5` layout | Fields (range, default) | Notes |
|---|---|---|---|
| **Fruity Balance** (57) | 2 × i32, no version | pan −128…128 (0); volume 0…320 (256) | volume 256 = 0 dB **[HYP: `L11(v/256)`]**; demos: pan always 0 |
| **Fruity Soft Clipper** (28) | 2 × i32 | threshold 1…127 (100); post gain 0…160 (128) | threshold = `v/128` linear (100 → −2.1 dB) **[DECOMP]**; post = `L11(v/128)` **[DECOMP]** |
| **Fruity Fast Dist** (53) | 5 × i32 | pre amp 64…192 (128); threshold 1…10 (10); type 0/1 (A/B) **[HYP]**; mix 0…128; post 0…128 | |
| **Fruity Filter** (32) | u32 version (1/2) + 6 × i32 (+ u8 in v2) | cutoff 0…1024 (1024); reso 0…1024 (205); LP, BP, HP 0…1024 (mix of each output); x2 0/1; v2 byte = "Remove DC offset" **[HYP]** | |
| **Fruity PanOMatic** (193) | 6 × i32 | pan 0…256 (128 = centre); volume 0…320 (256); LFO shape 0 sine/1 tri/2 square; LFO target 0 off/1 pan/2 vol; LFO amount 0…128 (64); LFO speed 0…55000 (27500) | 163/193 with LFO off → it is a "pan+volume" |
| **Fruity Stereo Shaper** (77) | u32 0 + 6 × i32 + u32 0 + u32 | R→L, L, R, L→R (±25600; L/R 12800); delay ±4096; dephasing ±4096; FX position (0 pre / 1 post) | 12800 = linear gain 1.0 (8076 = −4 dB, M/S presets) **[DATA]** |
| **Fruity Flangus** (56) | 8 × i32 | order 1…9 (4); depth 0…1024 (512); speed 0…1024 (768); delay 0…128 (8); spread 0…128 (128); cross ±128; dry ±128; wet ±128 | maps onto the chorus |
| Fruity WaveShaper (73) | u16/u16 + parameters + curve points as `double` | pre amp 64…192, mix 0…128, post 0…128 at the start | not decoded |

Also present and not decoded: Maximus (125, ~1.3 kB), Soundgoodizer (97, 12 B: preset A–D +
amount **[HYP]**), Transient Processor (47), Love Philter, Multiband Compressor, Delay Bank, Gross Beat,
Patcher, LuxeVerb.

---

## Recommendations for LYNX

General rule: identify by `0xC9` (accept UTF-16LE **and** ASCII), read `0xD5`, convert to real units
with the formulas above and only then to the 0…1 of the internal parameters (`peq::hzValue`,
`peq::gainValue`, `peq::qValue` etc.). Always ignore the backup state (A/B).

1. **Parametric EQ 2 → "EQ paramétrico" (7 bands, already exists in `src/core/Effects.*`).** It is the mapping
   with the highest payoff (1,138 slots, ~3,600 active bands).
   - Type: 0 → `off`; 1 → `highCut`; 2 → `bandPass`; 3 → `lowCut`; 4 → `notch`; 5 → `lowShelf`;
     6 → `peak`; 7 → `highShelf`. Band with bit 0 in the mask (v ≥ 4) → `off`.
   - Frequency `20·1000^(v/65536)`; gain `v/100` dB (±18 fits exactly in the LYNX range).
   - Q: LP/HP `0.1 + 9.9(1−w)³`; peaking/BP/notch `1/(2·sinh(0.3466·BW_oct))` with `BW_oct = 0.1 + 4.9w`
     (BP: ×0.708, notch: ×1.413); shelf: compute `S = 1/3 + 1.7193(1−w)` and convert to Q with
     `1/Q = √((A+1/A)(1/S−1)+2)`, `A = 10^(dB/40)` — or, better, add to `peq` a shelf variant
     parametrised by *S* and a peaking variant by width in octaves, which reproduces FL without approximation.
   - Slope: FL has order **per band**; LYNX has a global `slopeParam` (1, 2, 4 biquads).
     It needs to become a per-band parameter (stages = |order|+1, or at least 1/2/4 with |order| 0 → 1, 1 → 2,
     2…3 → 4). "Steep 8" on HP/LP (537 bands) ≈ 4 Butterworth biquads (48 dB/oct). For peaking/shelf
     with order ≠ 0 (≈ 40 cases), cascading with split gain is sufficient.
   - Main level → "Saída" parameter (`outputParam`), `v/100` dB.
2. **Limiter → Limiter (lookahead) + Compressor.** Fruity Limiter runs *gain → compressor → limiter →
   gate* all at once; the saved tab is only visual.
   - Ceiling `L3(v/1000)` → "Teto" (if ≥ +0 dB and gain 0 dB, the limiter is practically transparent);
     Gain `L7(v/1000)` → "Ganho"; Release `4·(501^(v/10000)−1)` ms → "Release".
   - If ratio > 0 and threshold < 1000: also instantiate the internal **Compressor** before the limiter
     (threshold `20·log10 L7`, ratio by formula 2.2, attack/release `T`/`2T`, knee `v/10` %).
   - Noise gate (threshold > 0) has no equivalent; ignore (demos: always 0). Sidechain: ignore and
     warn in the log.
3. **Reeverb 2 → Reverb (Freeverb).** Room ← `room/100` combined with decay (Freeverb has no decay in
   seconds: approximate `roomsize` from RT60 = decay·bass mult in the lows); "Abafar" ← high damping
   (kHz → 0…1, 221 = no damping); "Largura" ← `1 − |sep|/64` **[HYP]**; Mix: the internal effect is
   wet/dry, so use `wet_lin = L11(wet/256) + 0.5·L11(er/256)` and `dry_lin = L11(dry/128)`; dry = 0
   (39 slots) indicates reverb on a send → mix 100 % wet. Low cut/high cut: apply an HP/LP on the wet if LYNX
   gains these parameters (low cut on in 308 of the 333 slots); predelay likewise (cheap new parameter).
4. **Delay 2/Delay 3 → Delay.** Time in note beats: Delay 2 `v/48`, Delay 3 (sync) `v/96`; ping pong
   = mode 2 in both. Feedback: Delay 2 `v/127`, Delay 3 `v/6000`. Mix: Delay 2 dry `L11(dry/128)` and wet
   from the input; Delay 3 output wet/dry `v/6000`. "Abafar" ← cutoff (Delay 2 `v/128`, Delay 3 `v/6000`,
   inverted). Delay 3 without sync: keep the default until the engine is decoded.
5. **Compressor → Compressor.** Direct and exact mapping: threshold `v/10` dB, ratio `v/10`, makeup
   `v/10` dB, attack `v/10` ms, release `v` ms; Soft/Medium types suggest a soft knee. The
   lookahead flag (64 samples) can be ignored.
6. **Extras:** Soft Clipper and Fast Dist → Distortion (drive from threshold/pre amp, output from post gain);
   Fruity Filter → Filter (LP if `lp ≥ hp`, cutoff `v/1024` **[HYP: cutoff law]**); Balance and PanOMatic
   without LFO → track gain/pan (or a new, trivial "gain/pan" utility effect); Flangus → Chorus
   (speed/depth/mix); Stereo Shaper → needs a new "stereo matrix" effect
   (4 gains + delay), cheap to implement and used in 77 slots.
7. **Suggested new internal effects**, in order of payoff: (a) per-band slope + shelf by *S* in the
   parametric EQ; (b) low cut / high cut / predelay in the Reverb; (c) noise gate and sidechain only if the
   roadmap asks for them; (d) stereo matrix (Stereo Shaper) and utility gain/pan.

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| PEQ2 `FUN_006ec340` (VMT+3) | `SaveRestoreState`: versions 2…8, layout 0x000…0x161 | DECOMP |
| PEQ2 `FUN_006ec2a0` / `FUN_006ec2f0` | Packs/unpacks the 7-band mask | DECOMP |
| PEQ2 `FUN_006efda0` | Text: gain ×0.01 dB, Hz, width %, type/order names | DECOMP |
| PEQ2 `FUN_006fbf60`, `FUN_006fbf20`, `FUN_006f49c0` | Frequency scale 20·1000^(v/65536) | DECOMP |
| PEQ2 `FUN_006ef120` | Parameters → band (type remap, order, gain/stage) | DECOMP |
| PEQ2 `FUN_006ee6e0`, `FUN_005d1aa0` | RBJ coefficients per type, Q/BW/S, cascade | DECOMP |
| PEQ2 tables `0x74E718…0x74E8B0` | Remap, names, steep multipliers | DECOMP |
| PEQ2 `FUN_005f41c0` | Shared constants (ln 2, ln 3, ln 7, ln 11, ln 601, ln10/20) | DECOMP |
| PEQ2 DFM `TSynthEditorForm` | Tags 0…35, ranges, defaults, hints | STR |
| Limiter `FUN_006d3760` | `SaveRestoreState` (v5…7) | DECOMP |
| Limiter `FUN_006d5380`, `FUN_006d3dc0`, `FUN_005e0ce0`, `FUN_005e22b0` | L7/L3/T laws, ratio, knee, dB | DECOMP |
| Reeverb 2 `FUN_006d4920` | `SaveRestoreState` (10000/10001/old) | DECOMP |
| Reeverb 2 `FUN_006d5480`, `FUN_006d5160`, `FUN_006d5030` | Units; dry `L11(v/128)`, ER/wet `L11(v/256)`, separation/M-S matrix | DECOMP |
| Delay 3 `FUN_006e8da0`, `FUN_006e9740` | Layout v1 + 26 parameters; text delegated to `engine_x64.dll` | DECOMP |
| Delay 2 `FUN_006bfa00`, `FUN_006c0490` | 8 raw parameters; time in 1/48 of a beat; offset 1001^x | DECOMP |
| Compressor `FUN_006bd9a0`, `FUN_006be220`, `FUN_006bdb90` | Layout v1…3; /10 units; types; lookahead 64 | DECOMP |
| Soft Clipper `FUN_006bee60` | threshold v/128, post L11 | DECOMP |
| DFMs of Delay 3/2, Compressor, Balance, Fast Dist, Filter, PanOMatic, Stereo Shaper, Flangus, WaveShaper | Tags, ranges, defaults | STR |
| 165 `.flp` (mixer section) | Counts, versions, sizes, typical values | DATA |

### Pending items

- Delay 3: unit of the time without sync and feedback/cutoff laws (they are in the C++ engine `engine_x64.dll`,
  `FLDelay3Plugin`; look for the text method in vtable `+0x1D8` of the object at `P+0x178`).
- PEQ2: confirm that the *linear phase* path (bit 0 of 0x13D) only changes the phase and where *main level* is
  applied; meaning of UI fields 0x124/0x128/0x135/0x142.
- Limiter: comp threshold floor (`DAT_0073E5D0`, BSS) and the exact shape of curves 1…8.
- Reeverb 2: confirm `ln 11` as the base of ER/wet (BSS global `0x73746C`) and the direction of the separation.
- Volume laws of Balance/PanOMatic/Delay 2 (assumed `L11`), Fruity Filter cutoff, Soundgoodizer.
