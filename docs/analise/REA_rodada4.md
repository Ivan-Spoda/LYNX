# Behavioural analysis of FL Studio 2026: round 4 (REA) — 3x Osc and the channel filter

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md`, `REA_rodada2.md` and
> `REA_rodada3.md`. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode
> has been copied: what is here are descriptions, data layouts and formulas written in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence.

- **Targets:**
  - `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll` (SHA-256 `5d84d5ad…dff2`, Delphi, plugin class
    `TTestPlug`, VMT at `0x737e20`), Ghidra 12.1.4 via REA
  - `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`) for the channel filter
- **Format validation:** 165 `.flp` from `FL Studio 2026/Data`; **152 "3x Osc" channels** in 75 files
  (the request mentioned 47; the real number, counting tutorial/`Plugin presets` files, is 152)
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Confirmed statistically in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `P+0xNN` is an offset in the 3x Osc plugin object (`TTestPlug`); `V+0xNN` is an offset in the
channel voice in FLEngine; `C+0xNN` in the channel object (same convention as round 3).

---

## 1. Where the 3x Osc state lives **[DECOMP+DATA]**

- 3x Osc is **not** in FLEngine: it is a separate native plugin (`3x Osc_x64.dll` + `engine_x64.dll`).
  In the `.flp` it appears like any internal plugin: `0xC9` = `"3x Osc"` (UTF-16LE), `0xD4` (52 B
  header), `0xCB` (channel name), and **`0xD5` = blob returned by the plugin's `SaveRestoreState`**.
- `SaveRestoreState` = entry 3 of the `TTestPlug` VMT = **`FUN_00738580`**. Order of events within the
  channel: `0xD5` comes **before** `0xDB`/`0xE5`/`0xDA` (these still belong to the channel, see §4).
- Sizes seen in the demos: **92 B** (version 12, 93 channels), **93 B** (version 13, 2), **460 B**
  (version 14, 57). FL 2026 always writes version 14 / 460 B.

### 1.1 Layout of the `0xD5` blob (little-endian) **[DECOMP+DATA]**

| Off | Type | Field | Range (DFM) | Default |
|---|---|---|---|---|
| 0x00 | u32 | state **version** (12, 13 or 14; writes 14) | | 14 |
| 0x04 | 21 × i32 | **parameters 0…20** (table 1.2); go to `P+0x120 + 4·i` | | |
| 0x58 | u8 | **Invert osc 1** (bool) → `P+0x174` | 0/1 | 0 |
| 0x59 | u8 | **Invert osc 2** → `P+0x175` | 0/1 | 0 |
| 0x5A | u8 | **Invert osc 3** → `P+0x176` | 0/1 | 0 |
| 0x5B | u8 | **"Oscillator 3 = amplitude modulation"** (AM) → `P+0x177` | 0/1 | 0 |
| 0x5C | u8 | **HQ** ("High quality / anti-aliased oscillators"), bit 0 only → `P+0x208` (version ≥ 13) | 0/1 | 0 |
| 0x5D | 3 B | padding (always 0 in the 57 v14 blobs) | | 0 |
| 0x60 | 364 B | **copy of the channel envelope/filter** (version 14 only; §1.4) | | |

Read rules per version (`FUN_00738580`, branch `Save == 0`):

| Version | What is read |
|---|---|
| < 10 | old format with 4 fields per oscillator (40 B), converted to the 7-field layout. Does not appear in the demos |
| 10, 11 | 80 B (parameters 0…19; *phase rand* = 0) + 4 bools. Version 10: the 4 bools are zeroed |
| 12, 13 | 89 B from 0x04 (21 parameters + 4 bools + HQ byte; with 92 B the HQ byte does not exist and stays 0) |
| ≥ 14 | 0x1C8 = 456 B from 0x04. The channel copy (0x60…) is only applied if ≥ 364 B came in it |

The bool names come from the field table of the `TSynthEditorForm` form (offsets
`0x798 InvOsc1Btn`, `0x7C0 InvOsc2Btn`, `0x7F0 InvOsc3Btn`, `0x848 AMBtn`, `0x888 HqModeBtn`), which is
exactly the order in which `FUN_00738580` passes `P+0x174…0x177` and `P+0x208` on to the UI **[DECOMP+STR]**.

### 1.2 The 21 parameters **[DECOMP+STR+DATA]**

The parameter index `i` decomposes as `(i + 1) = 7·osc + field` (`FUN_005b63d0(i+1, 7)` in
`FUN_00739060`, the `ProcessParam`). Oscillator 1 has no "mix" (its field-0 position is taken by the blob version,
which is why the file begins with the version: the on-disk layout is literally `[version][pan1]…`). Ranges and
names come from the DFM (`Tag` of each control = parameter index).

| i | Off in `0xD5` | Osc | Field | Range | Default | Unit/use |
|---|---|---|---|---|---|---|
| 0 | 0x04 | 1 | Pan | −64…+64 | 0 | pan/64 (§2.3) |
| 1 | 0x08 | 1 | Shape | 0…6 | 0 | §1.3 |
| 2 | 0x0C | 1 | Coarse | −24…+24 | 0 | semitones |
| 3 | 0x10 | 1 | Fine | −100…+100 | 0 | cents |
| 4 | 0x14 | 1 | Stereo phase offset (SPO) | −64…+64 | 0 | v/128 cycle, R channel only |
| 5 | 0x18 | 1 | Stereo detune | −50…+50 | 0 | cents, R channel only |
| 6 | 0x1C | 2 | **Mix level** | 0…128 | 64 | v/128 |
| 7 | 0x20 | 2 | Pan | −64…+64 | 0 | |
| 8 | 0x24 | 2 | Shape | 0…6 | 0 | |
| 9 | 0x28 | 2 | Coarse | −24…+24 | 0 (new patch: −12) | |
| 10 | 0x2C | 2 | Fine | −100…+100 | 0 | |
| 11 | 0x30 | 2 | SPO | −64…+64 | 0 | |
| 12 | 0x34 | 2 | Stereo detune | −50…+50 | 0 | |
| 13 | 0x38 | 3 | **Mix level** | 0…128 | 64 in the DFM (new patch: 32) | |
| 14 | 0x3C | 3 | Pan | −64…+64 | 0 | |
| 15 | 0x40 | 3 | Shape | 0…6 | 0 | |
| 16 | 0x44 | 3 | Coarse | −24…+24 | 0 (new patch: −24) | |
| 17 | 0x48 | 3 | Fine | −100…+100 | 0 | |
| 18 | 0x4C | 3 | SPO | −64…+64 | 0 | |
| 19 | 0x50 | 3 | Stereo detune | −50…+50 | 0 | |
| 20 | 0x54 | — | **Stereo phase randomness** | 0…64 | 0 | §2.5 |

The "new patch" (3OSC.flp, unedited) is: osc2 mix 64 coarse −12, osc3 mix 32 coarse −24, everything else 0
**[DATA]**. All 152 channels respect the ranges above (observed minima/maxima: pan −23…35,
coarse −24…24, fine −100…51, SPO −17…64, detune −50…50, mix 0…128, phase rand 0…64) **[DATA]**.

3x Osc **has no** absolute "phase offset" parameter nor a separate octave parameter: the octave is the coarse
(±24 st = ±2 octaves), and the only phase shift is the SPO (relative between L and R).

### 1.3 Waveforms (Shape) **[DECOMP+DATA, order 1…4 strong HYP]**

`ProcessParam`, field 2 (`FUN_00739060`): for `shape < 6` the oscillator points to the host's wave table
`host+0x18 + shape·8` (the 10 wave tables that FL hands to native plugins); `shape = 6`
uses the user-loaded sample (*custom*, via dispatcher 10) or, if there is none, table 3.
The code marks `shape == 5` as **noise** (generated with a random phase increment, not from a periodic
table) and considers table 0 "alias-free" (**sine**).

| Value | Shape | Evidence |
|---|---|---|
| 0 | **Sine** | DECOMP (table 0 treated as having no harmonics); DATA ("Sub Bass", "DNB SUB", "Sub Pad": 0) |
| 1 | **Triangle** | HYP (order of the host tables; round 3: sine, triangle, square, ramp…) |
| 2 | **Square** | DATA (channel "Square" and "Square Bot": 2) |
| 3 | **Saw** | DATA ("Sweep Down Saw": 3) |
| 4 | **Rounded saw** ("analog saw") | HYP (order of the host tables) |
| 5 | **Noise** | DECOMP + DATA (all "White Noise"/"WNoise": 5) |
| 6 | **Custom** (user sample; no sample = saw) | DECOMP |

Usage in the demos: 0 (201×), 3 (122×), 2 (50×), 4 (28×), 5 (28×), 1 (24×), 6 (3×).

### 1.4 The channel copy inside the v14 `0xD5` **[DECOMP+DATA]**

The 364 B from 0x60 are a "snapshot" of the channel envelope/filter stored by the plugin itself
(written by `FUN_0072c390`, read by `FUN_0072c2e0`, defaults in `FUN_0072c250`):

| Off (in the blob) | Contents |
|---|---|
| 0x060 | 5 records of 68 B, **identical to the `0xDA` format** (Pan, Vol, ModX, ModY, Pitch; round 3 §1.2) |
| 0x1B4 | 8 unused B (0) |
| 0x1BC | i32 **Mod X** (cutoff), default 256 |
| 0x1C0 | i32 **Mod Y** (resonance), default 0 |
| 0x1C4 | 4 unused B (0) |
| 0x1C8 | i32 **filter type** (0…7, §3.1) |

- In 52 of the 57 v14 blobs the 5 records are byte-for-byte equal to the channel's `0xDA`, and the three integers
  match `modX/modY/type` of `0xDB`. In the remaining 5 the channel was edited afterwards (the snapshot became
  stale) **[DATA]**.
- If the snapshot was loaded, the plugin sets `P+0x234 = 1` and stops asking the host for the default
  envelope (dispatcher `0x1B`/`0x28` in `FUN_00737f90`). It lets plugin presets also load the
  envelope and the filter **[DECOMP for the flag; HYP for the purpose]**.
- **To import, always use the channel events (`0xDA`, `0xDB`, `0xE5`)**; the snapshot is only a
  fallback when they are missing.

---

## 2. How 3x Osc sounds: conversions **[DECOMP]**

### 2.1 Tuning

- Each oscillator stores `cents = coarse·100 + fine` (`FUN_00739060`, fields 3/4) and plays at
  `voicePitch + cents` (the voice already includes note, channel pitch and channel pitch envelope). The R channel also adds
  the **stereo detune** in cents: `pitchR = voicePitch + cents + detune` (`FUN_0073a230`).
- For LYNX: `octave = coarse / 12` (integer) and `semitone = coarse mod 12` if the internal synth only
  has octave; FL allows any semitone within ±24. `detune_cents = fine` (the *stereo detune* has no
  mono equivalent; approximate it as half detune on each side, or discard).

### 2.2 Mixing (levels) — `FUN_00738f10`

```
g3 = mix3 / 128
rest = 1 − g3            (if AM off)
rest = 1                 (if AM on: osc 3 does not "steal" level)
g2 = rest · mix2 / 128
g1 = rest − g2
```

That is, osc 1 gets whatever is left. New patch (mix2 = 64, mix3 = 32): g3 = 0.25, g2 = 0.375,
g1 = 0.375 (sum 1). "Invert osc N" flips the sign of `gN` (XOR on the float's sign bit). An oscillator with
gain 0 only advances its phase; it is not rendered.

**For LYNX (level 0…1 per oscillator):** `level1 = g1`, `level2 = g2`, `level3 = g3` (with negative
sign if inverted). If the LYNX synth normalises the sum, this already comes out normalised.

### 2.3 Per-oscillator pan — `FUN_00738ea0`

The plugin calls a host service (`host->vtbl+0xF8`) with `pan/64` (−1…+1) and the oscillator gain, which
returns the L/R gains. **[HYP]** the law is FL's usual linear balance (opposite side attenuated by
`1 − |p|`, panned side at full gain); I did not verify the host function.

### 2.4 Stereo phase (SPO)

At voice creation (`FUN_00739a40`) the L phase of each oscillator starts at 0 and the R phase at
`SPO · 0x1FFFFFF` (32-bit phase), i.e. **SPO/128 of a cycle** (±64 = ±half a cycle = ±180°). The phase is
reset on every note (there is no free-running oscillator).

### 2.5 Stereo phase randomness (parameter 20)

Stored as `v << 18`. At voice creation, a uniform random value of width `v/64` cycle, centred on 0, is
independently added to the L and R phases of each oscillator
(±`v/128` cycle; v = 64 → fully random phase). Since L and R receive different draws, this
also widens the stereo image.

### 2.6 AM and HQ

- **AM** (`FUN_007389d0`, table `DAT_00791a18` = {−1, 2}): with AM on, the oscillator of index 2
  (osc 3) is rendered in a different mix mode (mode 0x0C of `FUN_00739e90`) that **multiplies**
  the accumulated osc 1 + osc 2 instead of summing **[DECOMP for the mode; HYP for the exact formula, e.g.
  `(o1·g1 + o2·g2)·(g3·o3)`]**.
- **HQ** (`P+0x208`) and the host "high quality render" flag (`P+0x1FD`, dispatcher 1 bit 2)
  select anti-aliased oscillators for shapes ≠ sine/noise.
- Stereo processing is only enabled (`P+0x1FC`) if some oscillator has pan, SPO or detune ≠ 0, if
  there is *phase rand*, or if there is noise; otherwise L = R.

### 2.7 Envelope and filter

3x Osc **has no envelope or filter of its own in the DSP**. The sound is shaped by the channel:
volume by record 1 of `0xDA` (round 3 §1), filter by `0xDB` + records 2/3 of `0xDA` (§3).
The names "Note cutoff frequency" / "Note resonance" that the plugin exposes (`FUN_00739640`) are merely the
labels of the Mod X / Mod Y note properties.

---

## 3. Channel filter (Mod X / Mod Y) **[DECOMP]**

### 3.1 Types (last i32 of `0xDB`) **[STR+DECOMP]**

The `FTypeSelect` combo of `TPluginForm` (Channel settings) lists, in value order:

| Value | UI name | UI hint | Internal implementation (table `0x01510E40`: type, ×2) |
|---|---|---|---|
| 0 | Fast LP | "Default fast 12dB low pass" | type 0 (fast 2-pole filter, §3.3) |
| 1 | LP | 12 dB low pass | biquad LP |
| 2 | BP | 12 dB band pass | biquad BP |
| 3 | HP | 12 dB high pass | biquad HP |
| 4 | BS | 12 dB band stop | biquad notch |
| 5 | LPx2 | 24 dB low pass | biquad LP **twice** in series |
| 6 | SVF LP | 12 dB low pass SVF | SVF |
| 7 | SVF LPx2 | 24 dB low pass SVF | SVF twice |

In the demos, among the v14 3x Osc channels with a filter: types 0, 1, 2, 5, 6 and 7 appear **[DATA]**.

### 3.2 From `0xDB` to the filter inputs

1. Channel (`FUN_00fea140`): `cutN = (modX + modXAdj) / 256`, `resN = (modY + modYAdj) / 256`
   (modX/modY from `0xDB`, 0…256; adjustments from `0xE5`, normally 0). Stored at `C+0x1A4/0x1A8`.
   Defaults: Mod X = 256 (open), Mod Y = 0.
2. Voice (`FUN_00d265e0`): `x = cutN + noteModX`, `r = resN + noteModY` (the note adds its
   own Mod X/Y; with the note's default value the contribution is 0 **[HYP for the note scale]**).
3. On each control tick (`FUN_00d24650`): `x += envCut`, `r += envRes`, where
   `envCut = y·envAmt/128 + l·lfoAmt/128` from record 2 of `0xDA` and `envRes` likewise from record 3
   (round 3 §1.6). That is, the cutoff envelope with amount 128 sweeps the entire Mod X range.

`x` and `r` are normalised (1.0 = knob at maximum). Below, `k = 44100 / sampleRate`
(global `0x013EC894`, written in `FUN_006a0230`).

### 3.3 Fast LP (type 0) — `FUN_00981df0` + `FUN_006a05d0`

Coefficients:
```
c  = 0                          if x ≤ 0
c  = 1                          if x ≥ 1
c  = min(1, (601^x − 1)/600 · k)  otherwise
fb = 0                          if r ≤ 0
fb = 1 − (601^(1 − 0.625·min(r,1)) − 1)/600   otherwise
```
(`ln 601` at `DAT_015F49E8`, computed in `FUN_006a6890`; `1/600` at `0x013EC888`; `0.625` at
`0x0141E5DC`). Per-sample, per-channel recurrence (state `v`, `y`):
```
v ← v + (input − y)·c
y ← y + v          (output = y)
v ← v · fb
```
With `fb = 0` it is a 1-pole low-pass (`fc ≈ −fs/(2π)·ln(1 − c)`); with `fb` close to 1 it becomes a
resonant 2-pole with a peak at `ω ≈ acos(1 − c/2)` ≈ `√c` rad/sample. With Mod X = 256 and Mod Y = 0
(the channel default) it is an **exact bypass** (c = 1, fb = 0); the filter is only processed in the voice once
`c ≠ 1` or `fb ≠ 0` (flag `V+0x234`). `c` is linearly interpolated within the block.

Reference table (44.1 kHz, r = 0):

| Mod X | x | c | approx. fc (1 pole) |
|---|---|---|---|
| 256 | 1.00 | 1.000 | bypass |
| 192 | 0.75 | 0.202 | 1.6 kHz |
| 128 | 0.50 | 0.0392 | 280 Hz |
| 64 | 0.25 | 0.0066 | 46 Hz |
| 0 | 0 | 0 | silence (c = 0) |

### 3.4 LP / BP / HP / BS / LPx2 (types 1…5) — `FUN_00981c50` + `FUN_009815f0` + `FUN_00982020`

```
ω = ((51^x − 1)·0.0596 + 0.02)·k        rad/sample, limited to [0.02·k ; 3.0]
Q = 0.1                                 if r ≤ 0
Q = (51^(0.625·min(r,1)) − 1)·0.4 + 0.1   otherwise
α = sin ω / (2Q)
```
(`ln 51` and `ln 51 · 80/128` computed in `FUN_009823d0`.) The coefficients are those of the *Audio EQ
Cookbook* (RBJ), normalised by `a0 = 1 + α`:

| Type | b0 | b1 | b2 |
|---|---|---|---|
| LP | (1−cos ω)/2 | 1−cos ω | (1−cos ω)/2 |
| BP (0 dB peak) | α | 0 | −α |
| HP | (1+cos ω)/2 | −(1+cos ω) | (1+cos ω)/2 |
| BS | 1 | −2 cos ω | 1 |

with `a1 = −2 cos ω`, `a2 = 1 − α` (all ÷ `a0`), applied in direct form I per channel.

- **Frequency in Hz** (independent of the rate, because `k` compensates): `fc = ((51^x − 1)·0.0596 + 0.02) ·
  44100 / (2π)`. Mod X 0 → **140 Hz**; 64 → 840 Hz; 128 → **2.71 kHz**; 192 → 7.7 kHz; 256 → **21.06 kHz**
  (ceiling: ω = 3.0 at 44.1 kHz).
- **Q:** Mod Y 0 → 0.1 (heavily damped: even "open", the LP already attenuates highs); 64 → 0.44;
  128 → 1.07; 192 → 2.23; 256 → **4.37**.
- **LPx2:** the same LP biquad applied twice, and the voice gain is multiplied by
  `1 − Q·(3.2 − ω)·0.046875` to compensate for the double peak (`FUN_00d247a0`).

### 3.5 SVF LP / SVF LPx2 (types 6, 7) — `FUN_00981f20` + `FUN_00982140`

```
f = 0                         if x ≤ 0
f = min(1, (26^x − 1)·0.04·k)  otherwise
d = 1                         if r ≤ 0
d = 1 − 0.8·r                 if 0 < r < 1
d = 0.2                       if r ≥ 1
per sample:  lp ← lp + f·bp ;  bp ← bp + f·(input − lp − d·bp) ;  output = lp
```
It is the Chamberlin SVF (`f ≈ 2 sin(π fc/fs)`): Mod X 256 → f = 1 → **≈ 7.35 kHz** at 44.1 kHz;
128 → f = 0.164 → ≈ 1.15 kHz; 0 → silence. **SVF LPx2** runs the same filter twice; on the second
pass the damping becomes `d + (1 − d)·0.25` (`FUN_00d247a0`, constant `0x013E7548`).

### Recommendations for LYNX (importing "3x Osc")

1. Detect the channel by `0xC9 == "3x Osc"` and read `0xD5`: `version` at 0x00, 21 `i32` from 0x04,
   bools at 0x58…0x5C (0x5C only if the blob has ≥ 93 B). Ignore the 0x60 block if the channel has `0xDA`.
2. Per oscillator `n` (base `b = 0x04 + 28·(n−1)`, fields `[mix|—, pan, shape, coarse, fine, spo, det]`,
   where for osc 1 the first field is the version):
   - `waveform = {0: sine, 1: triangle, 2: square, 3: saw, 4: saw (rounded), 5: noise, 6: saw}`
   - `octave = trunc(coarse/12)`, `remaining_semitones = coarse − 12·octave` (add to the detune: `detune
     = fine + 100·remaining_semitones` if LYNX only accepts octave + cents)
   - `level` = `g1/g2/g3` from §2.2 (negative if *invert*); `pan = pan/64` if LYNX has per-osc pan.
3. ADSR: record 1 (Volume) of `0xDA` with the conversions from round 3 §1. If the envelope is
   off, a "gate" ADSR (attack 0, sustain 1, short release).
4. Filter: `cutoff_Hz` and `Q` via the formulas of §3.4 when the type is 1…5 (the LYNX LP has
   cutoff/resonance). For type 0 (Fast LP, the default), Mod X = 256 and Mod Y = 0 means **no
   filter**; for other values use `fc ≈ −fs/(2π)·ln(1 − c)` as an approximation, or implement the
   recurrence of §3.3 if you want fidelity. Types 6/7 → `fc ≈ fs/π · asin(f/2)`.
5. Cutoff envelope (record 2 of `0xDA`, if on): amount ±128 shifts `x` by ±1.0, i.e.
   it sweeps the entire Mod X range. Convert to LYNX "modulation octaves" by computing `fc(x)`
   at both extremes.

---

## 4. Neighbouring plugins (recognition only, cheap) **[DATA]**

- **FLEX** (57 channels, `0xD5` of 2,707 B): starts with `u32 1, u32 1, u32 0x812, u32 0`, then the
  **library** name (fixed-size text at 0x10, e.g. "General Midi Library", "Olbaid Compendium") and the
  **preset** name at 0x411 ("Piano Dark", "4.9 Pad"…), followed by the parameters (macros). The sound depends
  on FLEX's sample packs: LYNX can only map it by preset name. Not decoded.
- **Fruity DrumSynth Live** (8 channels, 19,467 B): `u32 4` (version), `u32` index of the selected kit
  (e.g. 66, 65, 51) and the list of kit names ("808 Bongo H" … "909 Snare") in short strings, followed
  by the parameters. Not decoded; it also has no direct equivalent in the 3-oscillator synth.

---

## 5. Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| 3x Osc `FUN_00738580` (VMT+3) | `SaveRestoreState`: versions, `0xD5` layout | DECOMP |
| 3x Osc `FUN_00739060` | `ProcessParam`: decomposition `(i+1)=7·osc+field`, shape/pitch/SPO/phase rand | DECOMP |
| 3x Osc `FUN_00738f10`, `FUN_00738ea0` | Level mixing, inversion, pan via host | DECOMP |
| 3x Osc `FUN_00739a40` | Initial voice phases (SPO, randomness) | DECOMP |
| 3x Osc `FUN_0073a230` | Render: detune on R, AM (mode 0x0C), noise | DECOMP |
| 3x Osc `FUN_007389d0`, `DAT_00791a18` | AM: osc of index 2 | DECOMP |
| 3x Osc `FUN_0072c2e0/0072c390/0072c250` | Envelope/filter snapshot (364 B) | DECOMP |
| 3x Osc DFM `TSynthEditorForm` (`0x41D6FC`) | Control tags, ranges and defaults | STR |
| FLEngine DFM `TPluginForm` (`0x1464BCC`) | Names and hints of the 8 filter types; Mod X/Y ranges | STR |
| FLEngine `FUN_00fea140` | Channel Mod X/Y → `/256` | DECOMP |
| FLEngine `FUN_00d265e0`, `FUN_00d24650`, `FUN_00d247a0` | Note+channel+envelope sum; dispatch by type; ×2 | DECOMP |
| FLEngine table `0x01510E40` | Channel type → (internal type, ×2) | DECOMP |
| FLEngine `FUN_00981df0` + `FUN_006a05d0` | Fast LP | DECOMP |
| FLEngine `FUN_00981c50` + `FUN_009815f0` + `FUN_00982020` | RBJ biquads | DECOMP |
| FLEngine `FUN_00981f20` + `FUN_00982140` | SVF | DECOMP |
| FLEngine `FUN_006a6890`, `FUN_009823d0`, `FUN_006a0230` | Constants ln 601, ln 51, 44100/fs | DECOMP |
| 152 3x Osc channels in the demos | Ranges, versions, shapes × names, equality of the snapshot with `0xDA/0xDB` | DATA |

### Open items

- Exact host pan law (`host->vtbl+0xF8`) and exact formula of the AM mode (§2.3, §2.6).
- Confirm the triangle (1) / rounded saw (4) order by reading the host's table generator
  (`FUN_009cdfc0`, round 3) or by listening.
- Per-note Mod X/Y scale (`V+0x284/0x288`).
