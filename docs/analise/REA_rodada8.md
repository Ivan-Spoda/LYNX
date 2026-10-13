# FL Studio 2026 behavioural analysis: round 8 (REA) — native states left open

> **Nature of this document.** Direct continuation of `REA_rodada7.md` (same method, same marks,
> same `Lk` curve laws). *Clean-room* reverse engineering, behavioural only: no Image-Line code or
> pseudocode has been copied; what follows are data layouts, formulas and descriptions written
> in our own words. Addresses (`FUN_xxxxxxxx`) and offsets serve only to trace the evidence.

- **Targets** (Delphi, Ghidra 12.1.4 via REA; `objdump` for short excerpts and for `FLEngine_x64.dll`):

  | Plugin | DLL (SHA-256) | Plugin VMT | `SaveRestoreState` | Text / `ProcessParam` |
  |---|---|---|---|---|
  | Maximus | `acc7affb…bdc1` | `0x83ee68` | `FUN_0083f8f0` | `FUN_00842d70` / `FUN_00842480` |
  | Soundgoodizer | `fe3f0c13…2ac2` | `0x835de8` | `FUN_00836960` | — / `FUN_00838510` |
  | Fruity Fast Dist | `cba6ef46…682a` | `0x6ba958` | `FUN_006baf30` | `FUN_006bb5e0` / `FUN_006bb260` |
  | Fruity Filter | `492af2fb…9d4a` | `0x6cb550` | `FUN_006cb920` | `FUN_006cc140` / `FUN_006cbd10` |
  | Fruity Flangus | `cc83092f…3d73` | `0x6b6510` | `FUN_006b6b20` | `FUN_006b7330` / `FUN_006b6fb0` |
  | Fruity Soft Clipper | `50aac728…72c6` | `0x6be3f0` | (2 raw × i32) | `FUN_006bee60` |
  | Transient Processor | `3d0475da…d053` | — | (u32 + 9 × i32 + extras) | `FUN_006edf70` |
  | `FLEngine_x64.dll` (host) | `6d9c5ab0…8e02` | `TFruityHost` `0x13141b8` | — | slot `+0xC0` = fast distortion |

- **Data:** the same 165 `.flp` files from `FL Studio 2026/Data` (mixer section). Slots: Maximus **125**,
  Delay 3 110, Soundgoodizer **97**, Flangus 56, Fast Dist 53, Transient Processor 47, Filter 32,
  Soft Clipper 28. The 4 Soundgoodizer presets (`Soundgoodizer/Data/Presets/{a,b,c,d}.dat`) were also
  read — they are complete Maximus states.
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Conventions from round 7: little-endian, `i32` signed, `P+0xNN` = offset into the plugin object, "parameter *i*"
= host index (= DFM `Tag`). `Lk(x) = (k^x − 1)/(k − 1)`. Time law `T(x) = 2·(501^x − 1)` ms
(x = v/10000), the same as Fruity Limiter.

---

## 1. Maximus (125 slots) **[DECOMP+STR+DATA]**

3-band multiband (low / mid / high) + a **master** section with the same band structure. There is no
threshold/ratio knob: each band's compression curve is a **drawn envelope** (Edison's envelope
editor) that maps input level → output level. It is this envelope that carries
"threshold", "ratio" and "ceiling".

### 1.1 Layout of `0xD5`

| Off | Type | Field |
|---|---|---|
| 0x000 | u32 | header: `0x000F0003` (58 slots), `0x000A0003` (45), `0x0000000A` (12), `0x000E0003` (9), `0x000B0003` (1). If > 0xFFFF the **version is the high word** (15, 10, 14, 11); otherwise it is the value itself. FL writes `0x000F0003` |
| 0x004 | 59 × i32 | **parameters 0…58** (table 1.2) → `P+0x12C` |
| 0x0F0 | 59 × i32 | backup state (A/B compare); ignore |
| 0x1DC | 0x22 B | UI state: u32 monitor speed (50), u32 **band selected in the editor** (0…3), u32, 8 × u8 display buttons, solo etc. |
| 0x1FE | — | end of the fixed block for v ≥ 7 (v < 7: 0x1F9 B; v < 6: 0x1F5; v < 5: 0x1F1; v < 4: 0x1EE; v < 3: old 3-band format read in pieces) |
| 0x1FE | u32, u32, u8, 5 × u32 | **v ≥ 14 only**: options (`P+0x328`, `P+0x330`; flags byte: bit 0 → `P+0x32C`, bit 1, …, **bit 6 → curve scale**, see 1.4; 5 display options). Demos: `(2, 0, 6, (3,1,2,2,2))` |
| +29 | u32 | **v ≥ 15 only**: `P+0x858` (1) |
| next | 8 × envelope | per band b = 0…3 (low, mid, high, master): **compression curve** (`P+0x3A4+0x100·b`) and a second envelope (`P+0x3AC+0x100·b`) |

Sizes: v10 = 1,342 B, v14 = 1,371 B, v15 = 1,375 B with 3 points per envelope; each extra point
adds 24 B (1,351…1,462 in the demos). The parser matched byte for byte on 124/125 blobs (the only v11 has 4 extra B
before the envelopes **[HYP: extra field in v11–13]**).

**Serialised envelope** (`FUN_00781bd0`) **[DECOMP]**:

| Off | Type | Field |
|---|---|---|
| +0 | u32 | envelope type identifier (3) |
| +4 | u32 | envelope version (3) |
| +8 | u32 | N = number of points |
| +12 | N × 24 B | per point: `f64` **Δx** (position relative to the previous point), `f64` **y**, `f32` **tension** (curvature of the segment arriving at the point, ≈ −1…+1), 4 B of flags (byte +3 = segment shape: 2 = straight, 1 / 0xFF = curved; byte +1 = selection) |
| +12+24N | 5 × i32 | markers (loop/sustain), normally `0, −1, −1, −1, −1` |

The x axis is **cumulative** (sum of the Δx), both axes in 0…1.

### 1.2 Parameters (`FUN_00842440` splits the index: < 7 global; otherwise band = (i−7) div 13, control = 7 + (i−7) mod 13)

Globals (0…6):

| i | Off | Name (DFM) | Range | Default | Conversion |
|---|---|---|---|---|---|
| 0 | 0x04 | Low band frequency | 0…65536 | 19800 | `Hz = 30·533.33^(v/65536)` (30 Hz…16 kHz; default **200 Hz**) |
| 1 | 0x08 | Low band slope | 0/1 | 1 | 12 dB / **24 dB** per octave |
| 2 | 0x0C | High band frequency | 0…65536 | 48065 | same law (default **3.0 kHz**) |
| 3 | 0x10 | High band slope | 0/1 | 1 | 12 / 24 dB |
| 4 | 0x14 | LMH lookahead delay | 0…10000 | 1115 | `T` ms (default 2.0 ms) |
| 5 | 0x18 | **LMH mix** | 0…1000 | 1000 | `v/1000`: processed/unprocessed mix **of each band** (1.3) |
| 6 | 0x1C | Low cut | −1…65536 | 32768 | `Hz = 5·16^(v/65536)` (5…80 Hz; default 20 Hz); **v < 0 = off** |

Per band, `base = 0x20 + 0x34·b` (b = 0 low, 1 mid, 2 high, **3 master**):

| j | Off | Name | Range | Default | Conversion |
|---|---|---|---|---|---|
| 7 | +0x00 | **Mode** | 0…3 | 0 | 0 on, 1 *compression off* (gains/saturation only), 2 muted, 3 off (band passes unprocessed) |
| 8 | +0x04 | **Pre gain** | 0…2000 | 1000 | `L7(v/1000)` linear (1000 = 0 dB, 2000 = ×8 = **+18.1 dB**, 0 = −∞) |
| 9 | +0x08 | **Post gain** | 0…2000 | 1000 | `L7(v/1000)` |
| 10 | +0x0C | Stereo separation | −1000…1000 | 0 | `v/1000` |
| 11 | +0x10 | Soft saturation threshold | −1000…1000 | 0 | 0 = no saturation; v < 0 **mode A**, v > 0 **mode B**; amount `|v|/1000` |
| 12 | +0x14 | Soft saturation ceiling | 0…2000 | 1000 | `L7(v/1000)` |
| 13 | +0x18 | **Attack** | 0…10000 | 1115 | `T` ms (default 2.0 ms); on bands 0…2, **0 = "Default"** (inherits) |
| 14 | +0x1C | Release 2 | 0…10000 | 0 | `2·T` ms; 0 = "Default" (off) |
| 15 | +0x20 | Attack / release 2 curve | 1…8 | 2 | integer |
| 16 | +0x24 | **Release** | 0…10000 | 5000 | `2·T` ms (default **85.5 ms**) |
| 17 | +0x28 | Release curve | 1…8 | 3 | integer |
| 18 | +0x2C | Sustain (peak window) | 0…10000 | 2882 | `T` ms (default 10 ms) |
| 19 | +0x30 | Envelope mode | 0/1 | 0 | peak / RMS |

Conversions read from the parameter text (`FUN_00842d70`, strings `"%sms"`, `"%dHz"`, `"No saturation"`,
`"saturation mode A/B"`, `"Default"`) and from the initialisation unit (`FUN_00852530` builds
`ln 533.33/(ln 2·65536)` and `ln 16/(ln 2·65536)`; `FUN_0062ddf0` builds ln 2, ln 601, ln 11, ln 3, ln 7)
**[DECOMP]**. `ProcessParam` stores `L7(pre)` in `P+0x380+0x100·b+0x10` and `L7(post)` in `+0x14`
**[DECOMP]**.

### 1.3 Signal flow **[partial DECOMP + HYP]**

input → low cut (optional) → split into 3 bands (crossovers 0/2, slope 1/3) → per band:
pre gain → stereo separation → **compression by the curve** (peak/RMS detector with attack, release,
release 2 and "sustain" window; "LMH delay" lookahead delay) → saturation → post gain → **LMH mix**
→ sum → **master** band (same chain) → output.

The LMH mix is, per band with mode < 2, `output = mix·processed + (1 − mix)·original band` (band
gain applied to both terms) — read in the render (`0x845300…0x845383`) **[DECOMP]**. Mode 2 silences the
band, mode 3 lets it through raw **[HYP: from the name and the flow branches]**.

### 1.4 The compression curve **[DECOMP]**

`FUN_00846490` turns each band's envelope into a gain table of 32,769 entries:

- Linear input level `ℓ = i/8192` (0…4, i.e. up to **+12 dBFS**).
- Editor coordinate: `c = log₃(1 + 2ℓ) / 2` ⇔ `ℓ = (3^(2c) − 1) / 2`. Hence **c = 0.5 is 0 dBFS**
  and c = 1 is +12 dB; c = 0.25 ≈ −8.7 dB, 0.1 ≈ −18 dB, 0.05 ≈ −25 dB.
- Output: `y = envelope(c)` (points interpolated with the segment tension), output level
  `(3^(2y) − 1)/2 · s`, with `s = 0.975` (−0.22 dB) or `1.0` when bit 6 of the v ≥ 14 flags is on
  (never in the demos).
- The table stores `output/input` (static gain); the detector with attack/release reads this table.

Typical curves (cumulative x):

| Points (x, y) | Meaning |
|---|---|
| (0,0) (0.5;0.5) (1.0;0.5) — **default** | linear up to 0 dB, then flat: **brickwall limiter at −0.22 dBFS** |
| (0,0) (0.5;0.5) (1.0;0.6) | linear up to 0 dB, above ≈ **4.4:1** up to +12 dB → ceiling +2.5 dB |
| (0,0) (0.35;0.35) (1.0;0.6) | knee ≈ **−4.7 dB**, ≈ 2.2:1, ceiling +2.5 dB (preset B) |
| (0,0) (0.4;0.4) (1.0;0.5) | knee ≈ −2.4 dB, ≈ 4.9:1, ceiling −0.2 dB (preset C) |
| (0,0) (0.15;0.15) (1.0;0.15) | limiter at ≈ −14 dB ("squashed" effect) |

The second envelope of each band (`+0x3AC`) is the default in 123/125 slots **[DATA]**; exact function
**[HYP: secondary curve/“gate”; ignorable]**.

### 1.5 Usage in the demos **[DATA]**

- Master: mode 0 in 85, **1 (compression off) in 30**, 3 in 10. Master curve: default (brickwall
  −0.22 dB) in 82; "4.4:1 above 0 dB" in 16; the rest are unique curves (knees −3.9…−13.7 dB).
- Master pre/post almost always 0 dB; the few ≠ 0 range from −10.8 to +13.8 dB. Master release: 85.5 ms
  (82), 117 ms, 32 ms. Master saturation: threshold −100/−16/−12 (mode A, 1…10 %) in ~35 slots, ceiling
  992 (−0.15 dB) in 21.
- Bands: mode 0 in ~90 per band (with the default curve = band limiter), 3 (off) in 14/20 on the
  low/high; pre gains up to +18 dB (2000) in 9 slots of the mid band.
- Crossovers: 200 Hz / 3 kHz in ~95 %; LMH mix 1000 in 110, 294 (≈ 29 %) in 9; low cut 20 Hz in 74,
  off in 31.

---

## 2. Soundgoodizer (97 slots) **[DECOMP+DATA]**

The DLL is the **entire Maximus engine** with a different panel (`SynthPanel2`: `MixWheel` "Amount" 0…1000,
default 600; `PresetSel` A/B/C/D) **[STR]**.

- **Layout of `0xD5` (12 B):** `u32 version (3)`, `i32 preset (0…3 = A…D)` → `P+0x334`,
  `i32 amount (0…1000)` → `P+0x338` (`FUN_00836960`) **[DECOMP]**.
- On load, `FUN_00836ac0` reads `Data/Presets/<a|b|c|d>.dat` (a 1,333 B Maximus v4 state) and
  then **forces parameter 5 (LMH mix) = amount** **[DECOMP]**. That is: *amount* is the
  processed/original mix of each band; the master section remains 100 % active.
- Demos: preset A in 75, B 10, C 10, D 2; amount 600 (default) in 22, median 270, range 30…1000.

### 2.1 What each preset does (read from the `.dat` files) **[DATA]**

All: crossovers 200 Hz / 3 kHz (C: 200 Hz / 1.9 kHz), attack 2 ms, release 85.5 ms (low: 137 ms).

| Preset | Bands (pre / post, curve) | Master |
|---|---|---|
| **A** | low +5.0/+5.6 dB, mid +6.4/0, high +6.9/+2.9; low/high on a 0 dB limiter with saturation A 100 %; mid knee −2.3 dB ≈ 2.6:1 | curve "4.4:1 above 0 dB", saturation A 20 %, release 2 |
| **B** | all three in *compression off*: only post +8.4 / +10.4 / +9.2 dB (+ saturation) | knee −3.9 dB, ≈ 2.2:1, ceiling +2.5 dB, saturation A 30 % |
| **C** | pre +8.5 / +12.7 / +11.3 dB, post +1.5 / +2.8 / +2.9 dB, band limiters | knee −2.4 dB, ≈ 4.9:1, ceiling −0.2 dB |
| **D** | mid only: pre +14.1 dB into "4.4:1 above 0 dB"; low and high **off** | brickwall −0.22 dB |

Summary: it is a "maximiser": it pushes each band +5…+14 dB against band limiters/compressors,
mixes with the original by *amount* and passes through a master compressor/limiter.

---

## 3. Fruity Fast Dist (53 slots) **[DECOMP+DATA]**

Layout: **5 × i32 without version** (20 B) → `P+0x120`: `pre amp` (64…192), `threshold` (1…10), `type`
(0 = A, 1 = B), `mix` (0…128), `post` (0…128).

### 3.1 Gains (`FUN_006bb260`)

- `pre = L11(v/128)` (128 = 0 dB, 192 = **+11.0 dB**, 64 = −12.7 dB).
- `post = L11(v/128)`, `m = L11(mix/128)`.
- Coefficients: **wet = m·post**, **dry = (post − wet)·pre·post** (*post* appears squared in the dry
  term — that is really how it is in the code) **[DECOMP: `0x6bb45e…0x6bb48d`]**.

### 3.2 DSP: table in the host (`FLEngine`, slot `+0xC0` of `TFruityHost`) **[DECOMP]**

The render (`FUN_006bb7c0`) calls the host with (type, threshold, buffer, n, dry, wet, pre). The host
(`0x132ADC0`) picks a table of 8,192 `i16` samples by (type, threshold) and runs `0x6A2BF0`:

```
idx = |x|·pre·8191 (truncated, with linear interpolation of the fraction; above 8191 it saturates at the end of the table)
y   = dry·x + wet·sign(x)·T[type][thr](idx)/32767
```

The tables are built once (`0xB96CC0`), in dB, with `X = 20·log10(i/8191)`:

- **Type A** (k₁ = (11 − t)/10, k₂ = 1.6·(t − 1)): if `X ≥ −25`: `Y = −25 + k₁·(X + 25)`; then, if
  `Y ≤ −k₂`, `Y += k₂`. It is an **instantaneous per-sample compression** (power `x^k₁` in the top 25 dB)
  with make-up gain k₂: t = 1 is linear; **t = 10 is almost a square wave** (k₁ = 0.1). Output
  level at 0 dBFS input: **−0.9·(t − 1) dB** (t = 10 → −8.1 dB).
- **Type B** (p = 0.8 + 0.02·(10 − t)): `Y = −(−X)^p`. Expands the level of low signals upwards
  (−20 dB → −11 dB at t = 10), keeps 0 dB at 0 dB. Smoother and with more "raised noise".
- Input above 0 dBFS (after *pre*) is stuck at the last value → **hard clipping** at that level.

Curves (linear output for input 0.01 / 0.05 / 0.1 / 0.25 / 0.5 / 1):

| t | Type A | Type B |
|---|---|---|
| 1 | 0.010 0.050 0.100 0.250 0.500 1.000 | 0.014 0.060 0.114 0.267 0.512 1.0 |
| 5 | 0.021 0.104 0.166 0.288 0.436 0.661 | 0.041 0.115 0.181 0.339 0.560 1.0 |
| 10 | 0.052 0.262 0.313 0.343 0.367 0.394 | 0.111 0.210 0.282 0.431 0.616 1.0 |

Least-squares fit to `tanh(D·x)/tanh(D)` (normalised form): type A t = 4…10 →
D = 2.2 / 4.5 / 6.8 / 9.2 / 12.8 / 17.8 / **22.8 dB**; t ≤ 3 → 0 dB. Type B t = 6…10 → 0.8…4.2 dB.

### 3.3 Demos **[DATA]**

pre 128 (38) / **192 (13)**; threshold **10 in 51/53**; type A in 50; mix 128 in 24 (the rest 29…92);
post 128 (41) / 97 (9, = −5.8 dB).

---

## 4. Fruity Filter (32 slots) **[DECOMP+DATA]**

Layout: `u32 version (1/2)` + **6 × i32** (`P+0x120`: cutoff, reso, LP, BP, HP, x2) + **u8 "Remove DC
offset"** in v2 only (`FUN_006cb920`; absent = off) **[DECOMP]**.

- **State variable filter (Chamberlin)** per channel (`FUN_006cc3b0`):
  `low += f·band; high = in − low − q·band; band += f·high; out = gLP·low + gBP·band + gHP·high`.
- **Cutoff:** `f = L26(v/1024)` (`(26^x − 1)·0.04`) **[DECOMP]**, direct SVF coefficient, *without*
  sample-rate correction: `fc = fs/π · asin(f/2)`. At 44.1 kHz: v = 134 → 149 Hz, 331 → 524 Hz,
  512 → 1.15 kHz, 768 → 2.98 kHz, **1024 → 7.35 kHz (fs/6)**; at 48 kHz everything scales by 48/44.1.
  In other words, "fully open" is still a low-pass at ~7–8 kHz.
- **Resonance:** damping `q = 1 − 0.8·(v/1024)` → **Q = 1/q = 1…5** (default 205 → Q 1.19).
- **LP/BP/HP outputs:** gain of each `L11(v/1024)` (1024 = 1.0; 512 = 0.232); summed.
- **x2:** a second identical SVF in series, same `f`, **fixed damping 1 (Q = 1)**, fed by the
  mix of the first and mixed again with the same gains.
- The coefficients ramp over 128 samples (`FUN_006cc2e0`); the remove-DC is a runtime routine.
- Demos: LP 1024 alone in 25, HP 1024 alone in 7, BP never; cutoff 1024 in 15 and 0 in 8 (start of
  automation), rest 134…372; reso 0 in 23; x2 in 2; remove DC on in 10/12 of the v2 ones.

---

## 5. Fruity Flangus (56 slots) **[DECOMP; exact LFO period HYP]**

Layout: **8 × i32 without version** (32 B) → `P+0x120`: order, depth, speed, delay, spread, cross, dry, wet.
`FUN_006b6ec0` passes to the engine: order (integer), depth/1024, speed/1024, delay/128, spread/128,
cross/128; `FUN_006b3ef0` computes the voices. Reference rate 44.1 kHz, rescaled by `44100/fs`.

- **Voices:** `2·order` voices; the first `order` belong to the left channel, the others to the right with the LFO
  shifted 180°. Voice *i* has weight `k = 1 − i/order` (1, …, 1/order).
- **Depth:** `D = L11(depth) · 1092.5` samples = **24.8 ms·L11(depth/1024)** (512 → 5.7 ms;
  1024 → 24.8 ms). Voice depth `Dv = D·k + 10` samples.
- **Delay (base):** `B = L11(delay/128) · 100 ms` (8 → 1.6 ms; 128 → 100 ms), minimum 8 samples; voice
  centre delay = `Dv + B`.
- **Speed:** `P = 75000·(101^(1 − speed/1024) − 1) + 50000`; phase increment `500·2³¹/P/Dv` per sample
  → **f ≈ 250·44100 / (P·Dv)** Hz (with `Dv` in samples) **[DECOMP for the formula; HYP: cycle = 2³²]**.
  The LFO speed is inversely proportional to the depth: the knob controls the *sweep
  speed*. Defaults (depth 512): speed 768 → **0.20 Hz**, 892 → 0.38 Hz, 632 → 0.10 Hz, 1024 → 0.84 Hz.
- **Spread:** `w = L1001(spread/128)` (`(1001^x − 1)/1000`); blend between equal rates/delays (w = 0)
  and per-voice randomised rates with a table of 20 factors ≈ 1 ± 0.07 (w = 1, default).
- **Cross:** each voice enters its own channel with gain 1 and the opposite one with `cross·k` (signed); then
  all are normalised by `1 − L1001(1 − 1/Σ|g|)`.
- **Dry / wet:** `sign(v)·L11(|v|/128)` (negative inverts phase). **There is no feedback.**
- Demos: order 4 (36), 2 (9); depth 512 (36), 86 (9, ≈ 0.7 ms); speed 768/892/632; delay 8 (38) / 0 (14);
  spread 128 (41) / 0 (10); cross −32 (35); dry 128 (32) / 96 (13); wet 96/48/70/36/128.

---

## 6. Fruity Soft Clipper (28 slots) **[DECOMP+DATA]** — confirmation

- `0xD5` = 2 × i32: threshold (1…127) and post (0…160). Threshold `t = v/128` linear; post `L11(v/128)`
  (confirmed: `FUN_006bee60` and `FUN_005dfd00`, fixed ceiling 1.0).
- **Curve** (`FUN_005dfd40` / SSE variant `FUN_005e1ad0`): below `t` it is linear; above,
  `|y| = t + (1 − t)·(1 − exp(−(|x| − t)/(1 − t)))`, sign preserved. Exponential knee with derivative 1 at
  `t` and asymptote at **1.0 (0 dBFS)**. The post gain is applied afterwards.
- Fit to `tanh` (inputs up to +6 dB): the equivalent drive is ~0 dB and the level drops 1.3…3.8 dB
  (t = 127 → −1.3 dB, 100 → −1.5, 64 → −2.0, 1 → −3.8).
- Demos: threshold 100 (16), 1 (5); post 128 (20), 132…150.

---

## 7. Transient Processor (47 slots) and Delay 3

### 7.1 Transient Processor **[DECOMP text + STR + DATA; DSP in `engine_x64.dll` not read]**

Layout (49 B): `u32 version (1)` + 9 × i32 (parameters 0…8) + `i32` (5/7) + `i32` (12800) + `u8`
**[HYP: menu options]**.

| i | Name | Range | Default | Text (`FUN_006edf70`) |
|---|---|---|---|---|
| 0 | Attack | 0…160 | 80 | `(v − 80)/80` → −100…+100 % |
| 1 | Drive | 0…160 | 0 | `v/160` % |
| 2 | Release | 0…160 | 80 | `(v − 80)/80` |
| 3 | Gain | 0…160 | 80 | `(v/160 − 0.5)·18` dB (±9 dB) |
| 4/5 | Attack / release shape | 0…2 | 1 | Sharp / Mid / Slow |
| 6 | Split frequency | 0…160 | 80 | `5 + 995·(v/160)²` Hz (default 253.8 Hz) |
| 7 | Split balance | 0…160 | 80 | dry = `min(1, 2(1−n))`, wet = `min(1, 2n)`, n = v/160 |
| 8 | Effect | 0/1 | 1 | Off / On |

Demos: attack 160 (+100 %) in 8, 80 in 6; median release ~60 (−25 %); gain 80; split 80 in 32.

### 7.2 Fruity Delay 3 — no progress

The text and the DSP are in `engine_x64.dll` (C++, no unit strings). The statistics of the 27 slots
without sync (time 22, 1, 0, 91…550) were not enough to pin down the unit. Still pending (round 7, 4.1).

---

## Recommendations for LYNX

Current internal formulas (from `src/core/Effects.cpp`): distortion `drive = v·36 dB` (tanh with
compensation `1/tanh(max(drive,1))`), `tone = 800·25^v Hz` (LP), mix, `output = (v − 0.5)·24 dB`;
chorus `rate = 0.05·100^v Hz`, `depth = v·8 ms` (sweep over a fixed 12 ms base), mix
(`output = in + (delayed − in)·mix·0.5`), `feedback = v·0.7`; filter type LP/HP/BP (`floor(3v)`),
`cutoff = 20·1000^v Hz`, `Q = 0.5·24^v`, mix; compressor `thr = −60+60v`, `ratio = 1+19v`,
`attack = 0.1·1000^v ms`, `release = 10·100^v ms`, `makeup = 24v dB`; limiter `ceiling = −12+12v`,
`gain = 24v dB`, `release = 10·100^v ms`. Useful inverses: `v = ln(x/lo)/ln(hi/lo)`.

1. **Maximus → Limiter (+ Compressor when the master curve has a knee).** The **master** section
   (band 3) represents the global effect; the bands are multiband and have no equivalent.
   - Master mode 3 → ignore the slot. Mode 1 → gain only: limiter with ceiling 0 dB (v = 1) and
     `gain = pre_dB + post_dB` (if negative, move it to the track volume).
   - Mode 0: compute the master curve as in 1.4 (cumulative points, linear interpolation in c; the tension
     can be ignored in a first version) and extract: **ceiling** = highest output level in dB (includes
     −0.22 dB from the 0.975 scale and, if there is saturation, `L7(satCeil)`), **knee** = first level at which
     output < input − 0.5 dB, **ratio** = Δinput/Δoutput between the knee and +12 dB.
   - Limiter: `ceiling v = (min(ceiling_dB + post_dB, 0) + 12)/12`; `gain v = pre_dB/24` (master pre
     `20·log10 L7(v/1000)`, 0…+18 dB fits); `release v = ln(ms/10)/ln(100)` with
     `ms = 4·(501^(rel/10000) − 1)`. Default curve → ceiling −0.22 dB (v = 0.982).
   - If knee < −0.5 dB and the ratio is finite: Compressor before the limiter with `thr v = (knee + 60)/60`,
     `ratio v = (r − 1)/19`, `attack v = ln(ms/0.1)/ln(1000)` with `ms = 2·(501^(att/10000) − 1)`
     (default 2 ms → v = 0.43), release as above, makeup 0.
   - Bands with mode 0/1 and gain ≠ 0 dB: add to the limiter gain the average gain
     `mix·(pre + post)` of the non-off bands **[HYP, rough approximation]**; low cut on → optional
     HP 20 Hz (LYNX does not have it in the limiter).
2. **Soundgoodizer → Compressor + Limiter**, with a per-preset approximation and `a = amount/1000`
   **[HYP]**: `ratio_eff = 1/(1 − a·(1 − 1/r_band))`, `makeup = a·(pre + post)` averaged over the bands.
   For a = 0.6:

   | Preset | thr (dB) | ratio | makeup (dB) | Limiter after |
   |---|---|---|---|---|
   | A | −6 | 2.5 | +5.3 | ceiling 0 dB (master curve 4.4:1 above 0 dB → use ratio 4 with thr 0 if there is a 2nd compressor) |
   | B | −10.5 | 2.2 | +6.6 | ceiling 0 dB |
   | C | −10.8 | 2.5 | +7.9 | ceiling −0.2 dB |
   | D | −14 | 1.9 | +8.5 | ceiling −0.2 dB |

   attack 2 ms (v = 0.43), release 85 ms (v = 0.47). In `v`: `thr v = (thr+60)/60`, `ratio v = (r−1)/19`,
   `makeup v = dB/24`. The current mapping (`Soundgoodizer → compressor`) can stay, just filling in these
   values; ideally a limiter (−0.2 dB) would be chained after it.
3. **Fast Dist → Distortion.**
   - `drive v = (20·log10 L11(pre/128) + D(t, type)) / 36`, with D from table 3.2 (t = 10 type A →
     22.8 dB; pre 192 → +11 dB → v = 0.94). Tone: **v = 1** (Fast Dist does not filter).
   - Mix/output: with `wet = m·post`, `dry = (post − wet)·pre·post` and `S = 10^(−0.9(t−1)/20)` (type A; type B
     S = 1): `G = wet·S + dry`, `mix v = wet·S/G`, `output v = 0.5 + 20·log10(G)/24`. Default (mix 128,
     post 128, t = 10): mix 1, output −8.1 dB → **v = 0.16**.
4. **Fruity Filter → Filter.** Type = largest among the gains `L11(LP/1024)`, `L11(BP/1024)`, `L11(HP/1024)`
   (LP → 0, HP → 1/3, BP → 2/3 of `floor(3v)`); if LP and HP are both ≥ 0.5, it is a notch/all-pass that
   LYNX does not have → keep LP. `cutoff Hz = fs/π·asin(L26(v/1024)/2)` → `v = ln(Hz/20)/ln(1000)`
   (1024 → 7.35 kHz → v = 0.855; do **not** force 1.0 as the current code does — FL muffles even when open).
   `Q = 1/(1 − 0.8·reso/1024)` → `v = ln(Q/0.5)/ln 24` (reso 0 → Q 1 → v = 0.218). x2 → raise Q
   ×1.3 **[HYP]** or cascade two biquads if LYNX gains filter order. Mix = 1.
5. **Flangus → Chorus.** `rate v = ln(f/0.05)/ln(100)` with `f` from section 5 (defaults → 0.20 Hz →
   v = 0.30); `depth v = min(1, 2·D_ms/8)` **[HYP: sweep = 2·Dv]** (default 5.7 ms → 1.0);
   `feedback v = 0`; mix: `r = |wet_g|/|dry_g|` (gains `L11(|v|/128)`), `mix v = min(1, 2r/(1+r))`
   (dry 128 / wet 96 → r = 0.50 → v = 0.67; dry 0 → 1). Flangus's base delay (1.6 ms default) is
   much smaller than LYNX's fixed 12 ms — if the chorus gains a "base" parameter, use `B + Dv`.
6. **Soft Clipper → Distortion:** `drive v = 0` (not 0.15 as today), tone 1, mix 1,
   `output v = 0.5 + (20·log10 L11(post/128) − c(t))/24`, with `c` ≈ 1.5 dB (t ≥ 100), 2.0 (t ≈ 64),
   3.8 (t = 1). Even better: implement the exact curve from 6 as a "soft clip" mode of the distortion
   (LYNX's `SoftClip.h` already exists; parametrise the knee by `t`).
7. **Transient Processor:** no equivalent internal effect; ignore with a warning, or apply only
   `Gain` (±9 dB) to the track volume.

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| Maximus `FUN_0083f8f0` (VMT+3) | `SaveRestoreState`: header, 59 + 59 i32, UI block, v14/v15 extras, 8 envelopes | DECOMP |
| Maximus `FUN_00842440`, `FUN_00842480` | Index → (band, control); `ProcessParam` (L7 for pre/post) | DECOMP |
| Maximus `FUN_00842d70` | Text: Hz (`30·533.33^x`, `5·16^x`), ms (`T`, `2T`), saturation A/B, "Default" | DECOMP |
| Maximus `FUN_00852530`, `FUN_0062ddf0` | Constants ln 533.33, ln 16, ln 2, ln 3, ln 7, ln 11, ln 601 | DECOMP |
| Maximus `FUN_00781bd0` | Envelope serialisation (Δx, y, tension, flags, markers) | DECOMP |
| Maximus `FUN_00846490` | Envelope → gain table (`ℓ = (3^(2c)−1)/2`, scale 0.975/1.0) | DECOMP |
| Maximus render `0x845300…0x845383` | Per-band LMH mix (mix·processed + (1−mix)·original) | DECOMP |
| Maximus DFM `SynthPanel` | Tags 0…19, ranges, defaults, lists (mode, slope, peak/RMS) | STR |
| Soundgoodizer `FUN_00836960`, `FUN_00836ac0` | 12 B layout; loads `Presets/*.dat` and forces LMH mix = amount | DECOMP |
| Soundgoodizer `Presets/{a,b,c,d}.dat` | Maximus v4 states of the presets | DATA |
| Fast Dist `FUN_006bb260`, `FUN_006bb7c0` | L11 of pre/mix/post; dry/wet; call to the host | DECOMP |
| `FLEngine` `0x132ADC0`, `0x6A2BF0`, `0xB96CC0` | Table selection, interpolated waveshaper, construction of curves A/B | DECOMP |
| Filter `FUN_006cb920`, `FUN_006cbf10`, `FUN_006cc3b0` | Layout v1/v2; L26 of the cutoff, Q, L11 gains; SVF ×1/×2 | DECOMP |
| Flangus `FUN_006b6ec0`, `FUN_006b3ef0` (+ `0x6b3e30`, `0x6b3eb0`) | Scales; voices, depth, base, speed, spread, cross | DECOMP |
| Soft Clipper `FUN_006bee60`, `FUN_005dfd00`, `FUN_005dfd40`, `FUN_005e1ad0` | t = v/128, ceiling 1; exponential curve | DECOMP |
| Transient Processor `FUN_006edf70`, `FUN_006edee0` | Text units; split dry/wet | DECOMP |
| 165 `.flp` (mixer section) | Counts, versions, curves, typical values | DATA |

### Pending items

- Maximus: exact shape of the tension in the envelope segments (`FUN_00788a20`), the role of the second
  per-band envelope, saturation A/B, extra field in v11–13.
- Flangus: confirm the LFO cycle (2³² vs 2³¹) and the waveform (triangular **[HYP]**).
- Delay 3 (time unit without sync, feedback/cutoff laws) and the Transient Processor DSP: both in the
  C++ `engine_x64.dll`, not yet opened.
