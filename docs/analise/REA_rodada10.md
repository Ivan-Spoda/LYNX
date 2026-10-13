# FL Studio 2026 behavioural analysis: round 10 (REA) — simple native synthesizers (Harmless, MiniSynth, DX10, Fruit Kick, BooBass, Drumpad)

> **Nature of this document.** Continuation of `REA_rodada4.md` (3x Osc). *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode has been copied: what is here are
> descriptions, data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence.

- **Targets** (local copies in `tmp/r10/bin/`, originals in `Plugins/Fruity/Generators/<name>/`):
  - `Harmless/Harmless_x64.dll` (SHA-256 `4100bf85…440b`, Delphi, class `TTestPlug`, VMT `0x7791d0`)
  - `Minisynth/MiniSynth_x64.dll` (SHA-256 `e00ca019…ec85`, **C++**, class `CMiniSynth`, originating from FL Studio Mobile)
  - `Fruity DX10/Fruity DX10_x64.dll` (SHA-256 `3a9d58d7…e8d9`, Delphi, VMT `0x7343f0`)
  - `Fruit Kick/Fruit Kick_x64.dll` (SHA-256 `a93d8645…122b`, Delphi, VMT `0x72cff8`)
  - `BooBass/BooBass_x64.dll`, `Drumpad/Drumpad_x64.dll` (format + DFM only)
- **Data:** 165 `.flp` files in `FL Studio 2026/Data` + all `.fst` files in `Data/Patches` (channel presets,
  same event format). Collection only from the channel section (before the first `0xEC`), `0xD5` after `0xC9`.
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

### Counts in the data

| Plugin (`0xC9`) | Channels in `.flp` | Total with `.fst` | `0xD5` sizes |
|---|---|---|---|
| Harmless | **160** (25 files) | 594 | 436/437 B (no custom table), 4628/4629 B (with), 428 B (v4/v5) |
| MiniSynth | 12 | 19 | 214/228/242 B (varies with the preset name) |
| Fruity DX10 (+ "DX10") | 12 | 75 | 92 B (v1), 460 B (v2) |
| Fruit Kick / "Fruit kick" | 3 | 15 | 32 B (v0), 400 B (v1) |
| BooBass | 3 | 7 | 16 B |
| Drumpad / "DrumPad" | 11 | 976 | 300 B / 292 B |

### General note: how to find `SaveRestoreState` in these Delphi plugins

In this FL's Delphi 12 x64 plugins, the VMT "self" pointer sits at **VMT − 0xC8** (not −0xB0 as in
old Delphi). By searching for a qword pointing to its own address + 0xC8 near the string `TTestPlug`,
the VMT shows up and the entries follow the SDK order: **[0] DestroyObject, [1] Dispatcher, [2] Idle,
[3] SaveRestoreState, [4] GetName, [5] ProcessEvent, [6] ProcessParam, [8] Gen_Render, [9] TriggerVoice**
(script `tmp/r10/findself.py`) **[DECOMP]**. It is the same "entry 3" as in round 4.

---

## 1. Harmless **[DECOMP+STR+DATA]**

Harmless is an **additive** synthesizer (the harmonics of a saw are summed and shaped by masks,
a timbre EQ and a "filter" that is a curve applied in the harmonic domain), but its interface and its use in the
demos are those of a classic subtractive: saw/square oscillator, unison, filter with envelope, volume ADR,
LFO, portamento and an effects rack (chorus/delay/reverb/compressor).

### 1.1 Layout of `0xD5` (`SaveRestoreState` = `FUN_0077adb0`)

| Off | Type | Field |
|---|---|---|
| 0x000 | u32 | **version** (FL 2026 writes 12; in the data: 4, 5, 9, 10, 11, 12) |
| 0x004 | 100 × i32 | **parameters 0…99** (`P+0x128 + 4·i`); versions < 9 store only 99 (396 B) and 99 stays 0 |
| 0x194 | 7 × i32 | (version ≥ 4) state of the combos without a parameter index, §1.4 |
| 0x1B0 | i32 `n` | (version ≥ 8) size of the custom harmonic table: 0 or 524 (`0x20C`) |
| 0x1B4 | `n` × 8 B | custom table (only if `n > 0`; §1.5) |
| end | u8 | (version ≥ 12) interface flag (`P+0x798`); if 0 the plugin recalculates something about the editor scale. Ignore **[HYP]** |

Hence the sizes: 4 + 400 + 28 + 4 + 1 = **437** (v12 without table), 437 + 4192 = **4629** (v12 with table),
436/4628 (v9–v11, without the final byte), 428 (v4/v5: 4 + 396 + 28).

Conversions of old versions done on read **[DECOMP]**:
- version < 5: in the compression combo the value 3 becomes 4 (an item was inserted); if parameter 86
  (LFO shape) > 9, add 1 ("Filter frequency" was inserted).
- version < 6: parameter 61 (resonance offset) is multiplied by 1.2.
- version < 10: the custom table is shifted by 4 positions (old format).
- version < 11 with parameter 47 = 0: parameter 58 is rescaled by the filter range.

In the 160 channels in `.flp`: v12/437 B = 102, v12/4629 B = 35, v11/436 B = 16, v11/4628 B = 7 **[DATA]**.

### 1.2 Parameters (DFM `TSynthEditorForm` Tag = index) and conversions

Names = the control's *hint* in the DFM **[STR]**; formulas from `ProcessParam` (`FUN_0077f630`) and from the text
shown in the UI (`FUN_00781200`) **[DECOMP]**. Constants initialised in `FUN_00614c80`: `ln 2`, `ln 11`,
`ln 3`, `ln 2 / 12` **[DECOMP]**. Below, `x = v/1000` unless stated; "range" and "def." come from the DFM.

**Timbre / harmonics**

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 0 | Phase randomness (no Tag in the DFM ⇒ 0) | −1000…1000 (0) | 0 = "Constant"; > 0 = "running" (v/10 %); < 0 = "spectral random" (|v|/10 %). Internal `sign·x²` |
| 1 | Low harmonics protection | 0…1024 (256) | g = v/1024; gain = (11^g − 1)/10 (0…1) |
| 2, 3, 4 | Sub harmonic 1 / 3 / 4 | 0…2048 (0) | linear gain v/1024 (0…2) |
| 5…10 | Timbre EQ bands 1…6 | −1024…1024 (0) | g = (v+1024)/1024; gain = (3^g − 1)/2 → −1024 = mute, 0 = 1.0 (0 dB), +1024 = 4.0 (+12 dB) |
| 11…22 | Harmonic mask 1/12…12/12 | 0…1000 (DFM: alternates 1000/0) | gain = (11^x − 1)/10 (0…1). Default 1,0,1,0… = odd harmonics only |
| 23 | Harmonic mask mix | 0…1000 (500) | x (0 = no mask, 1 = full mask) |
| 24 | Mask mix LFO amount | ±1000 | x |
| 25 | Stereo tremolo depth | 0…1000 | x |
| 39 | Detuning | ±2400 (0) | cents; exact role not verified (data: 0, ±1200, 2400) **[HYP]** |
| 40 | Grittiness | ±1000 | internal `−0.0125·sign·(65^|x| − 1)` |

With the default mask (odd), "mask mix" takes the saw to a **square**: it is Harmless's "odd/even"
**[DECOMP for the formula; HYP for "mask repeats every 12 harmonics"]**.

**Volume / pluck / global**

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 26 | Volume env attack | 0…1000 (0) | **t = 0.05·(101^x − 1) s** (0…5 s), linear ramp |
| 27 | Volume env decay | 16…1000 (500; data: 1000) | **1000 = infinite** (holds the level); otherwise **t = 0.11995·(1001^x − 1) + 0.05 s**, exponential fall down to −40 dB (decays to silence: **there is no sustain**) |
| 28 | Volume env release | 0…1000 (250) | **t = 0.0249·(101^x − 1) + 0.01 s** (0.01…2.5 s), exponential down to −40 dB |
| 29, 30 | VEL → attack / release vel → release | bool | |
| 31 | Pluck decay | 0…1000 (500; data: 1000) | 1000 = off; otherwise t = 0.11975·(1001^x − 1) s |
| 32…35 | VEL→pluck, alt. pluck, pluck uses mask, pluck only on release | bool | |
| 36 | Master volume | 0…2000 (1000) | x ∈ [0,2]; **gain = 0.75·(3^x − 1)/2** → 1000 = 0.75 (−2.5 dB), 2000 = 3.0 (+9.5 dB) |
| 37 | VEL → volume | bool (data: 1 in 58 %) | |
| 38 | **Master pitch** | ±2400 (0) | transposition in **cents**. Data: basses use −2400/−1200 **[DATA]** |
| 41 | Pitch vibrato depth | 0…1000 | internal v·0.075 (max. 75; **[HYP]** cents) |
| 42 | Pitch LFO amount | ±1000 | **±(101^|x| − 1)·12 cents** (max. ±1200 = ±1 octave) **[DECOMP; unit HYP]** |
| 43 | Portamento mode | bool | |
| 44 | Monophonic | bool | |
| 45 | Portamento / legato time | ±1000 (0; data: 400) | **t = (41^|x| − 1)/26.667 s** (400 → 0.13 s; 1000 → 1.5 s). Negative = "Inverted porta" **[STR+HYP]** |
| 46 | VEL → porta time | bool | |

Reference table (s):

| v | attack (26) | decay (27) | release (28) | filter env. (48/49) | porta (45) |
|---|---|---|---|---|---|
| 0 | 0 | 0.05 | 0.010 | 0 | 0 |
| 100 | 0.029 | 0.17 | 0.025 | 0.12 | 0.017 |
| 250 | 0.109 | 0.61 | 0.064 | 0.56 | 0.057 |
| 500 | 0.45 | 3.7 | 0.24 | 3.7 | 0.20 |
| 750 | 1.54 | 21 | 0.78 | 21 | 0.57 |
| 1000 | 5.0 | ∞ | 2.5 | ∞ | 1.5 |

Envelope **[DECOMP]** (`FUN_00784a10` + `FUN_005f5f60`): linear attack; decay and release are multiplications
by `exp(ln(0.01)/N)` per sample, i.e. they reach **1 % (−40 dB) in N samples** = the time in the table.
There is also a fixed 80 ms *declick* ramp when a voice is stolen.

**Filter**

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 47 | Adaptive filter envelope mode | bool (data: 1 in 86 %) | changes the scale of the env amount (below) |
| 48 | Filter env attack | 0…1000 (0) | 1000 = infinite; otherwise t ≈ 0.12·(1001^x − 1) s |
| 49 | Filter env decay | 0…1000 (500) | same (decays to 0; no sustain) |
| 50, 51 | Attack / decay slope | ±1000 (decay: −300) | curvature **[HYP]** |
| 52 | **Filter env amount** | ±1000 (0; DFM value 500) | a = x. Non-adaptive: shift = a·(full range, ≈ 7.9 octaves); adaptive: a unscaled (fraction of the remaining range) **[DECOMP for the scale; HYP for the adaptive meaning]** |
| 53 | VEL → env amount | bool | |
| 54 | **Filter frequency** | 0…8192 (4096) | position on the log scale: `f ≈ 73.4 Hz · 2^(7.855·v/8192)` (§1.3) |
| 55 | Filter scale | 0 = octave, 1 = Hz | |
| 56 | Filter width | 0…1000 (500) | curve width = 1024·2^(1 − 0.004·v) (internal) |
| 57 | **Key to filter frequency** | ±2000 (1000) | keytracking = x (1000 = 100 %) |
| 58 | Filter LFO amount | ±1000 | internal x·6 (**[HYP]** octaves) |
| 59 | Resonance amount | 0…1000 (0) | x |
| 60 | Resonance scale | 0 = octave, 1 = Hz | |
| 61 | Resonance offset / noise length | ±2400 | v/1200 octaves (cents) |
| 62 | Resonance self-oscillation | 0…2048 | v/256 |
| 63, 64 | Adaptive width, track freq motion | bool | |

**Unison, phaser, harmonizer, LFO, effects**

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 65 | **Unison order** | 1…9 (1) | no. of voices; compensation gain = 1 − (3^(1−1/n) − 1)/2 (n=2: 0.63; n=9: 0.17) |
| 66 | Unison panning | 0…128 (128) | v/128 |
| 67 | **Unison pitch thickness** | 0…256 (128) | (41^(v/256) − 1)·1.875 → 128 ≈ 10, 256 = 75 (**[HYP]** cents of spread) |
| 68 | Unison phase | 0…128 (64) | v/128 |
| 69 | Unison variation | 0…128 | v/128 |
| 70 | Unison type | 0 Classic, 1 Uniform, 2 Blurred | |
| 71…78 | Phaser mix, scale (oct/Hz/harmonic "use for PWM"), width (v·0.0001), width LFO (v·0.0005), offset (shown v·0.36), offset LFO, motion speed (like the LFO), key→offset | | |
| 79…85 | Harmonizer mix, width, width LFO, VEL, strength (1.331·2^(v/256)), type (Oct up, Oct up +1, Oct up +2, Harm up, Harm up +1), position (Pre-filter, Pre-phaser, Pre-pluck, Final) | | |
| 86 | **LFO shape / source** | 0…13 | 0 Sine, 1 Smooth saw, 2 Smooth pulse, 3 Saw, 4 Pulse, 5 Stairs, 6 Triangle, 7 Sickle, 8 Multisine, 9 Filter envelope, 10 Filter frequency, 11 Voice random, 12 Velocity, 13 Pitch |
| 87 | Global / retriggered LFO | bool | |
| 88 | LFO attack | 0…1000 | t = 0.05·(101^x − 1) s |
| 89 | **LFO speed** | ±8000 (3000) | **period = 64 / 2^(|v|/1000) beats** (3000 → 8 beats; 6000 → 1 beat; 8000 → 1/4 beat). 0 = "Still"; negative = same period with inverted sign (reversed phase). The UI shows it in steps:ticks with 48 ticks/step |
| 90 | Chorus type | 0…9 | 0 Off, 1 Classic, 2 Thick, 3 Deep, 4 Flanger, 5 Alt flanger, 6 Stereoized, 7 Modulation, 8 Surround, 9 Vibration |
| 91 | Chorus mix | 0…1000 (500) | x |
| 92 | Delay type | 0…3 | 0 Off, 1 Classic, 2 Ping-pong, 3 Stereoized |
| 93, 94 | Delay input, feedback | 0…1000, 0…900 | x |
| 95 | **Delay time** | 0…384 (144) | **ticks at 192 per beat** (144 = 3/16, 192 = 1/4) |
| 96 | Reverb type | 0…6 | 0 Off, 1 Classic, 2 Large hall, 3 Cathedral, 4 The venue, 5 Drum room, 6 Small studio |
| 97 | Reverb wet | 0…1000 (500) | x |
| 98 | Compression level | 0…1000 (500) | x |
| 99 | VEL → unison thickness | bool | |

**The combo indices ignore the DFM `-` / `-Special` separators** (the effect combo 0…9 has 10
usable items; the filter one 0…16 has 17) **[DECOMP+DATA]**: confirmed because types 3 and 7 (Classic HP and
Crude HP) receive the same "high-pass" configuration in `FUN_0077ca30`.

### 1.3 The filter in Hz **[DECOMP, margins HYP]**

The Harmless filter is a **tabulated curve** (chosen by the combo, generated in `FUN_0077ca30`) applied to the
harmonic amplitudes. The curve position lives on a log2 frequency scale:

- base extremes (constructor, `0x77bb43`): `fLow = 440·2^(−31/12) ≈ 73.4 Hz`, `fHigh = 17 000 Hz`
  (≈ 7.86 octaves). `FUN_0077c170` widens these ends by a margin that depends on the shape and the width
  (param 56), so that the filter can close/open fully.
- `position = lo + (v/8192)·(hi − lo)`. Approximation without margins: **`f ≈ 73.4·2^(7.855·v/8192)` Hz**
  (0 → 73 Hz; 2048 → 286 Hz; 4096 → 1.1 kHz; 6144 → 4.4 kHz; 8192 → 17 kHz).
- Keytracking (57) default 100 %: the position follows the note **[HYP: reference C5]**.

Types (tail[0], §1.4): 0 Classic LP, 1 Classic BP, 2 Classic BS, 3 Classic HP, 4 Crude LP, 5 Crude BP,
6 Crude BS, 7 Crude HP, 8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 12 Fin LP, 13 Leaky LP,
14 Phaser, 15 Band phaser, 16 Flat band pass. In the 160 channels: 0 (69), 4 (47), 1 (11), 9 (7), 16 (5)…

### 1.4 The 7 integers at 0x194 (combos with `Tag = −1`) **[DECOMP+STR+DATA]**

Mapped via the form's published fields (`FilterTypeSelect @0x6F8`, `ResTypeSelect @0x700`,
`PhaserTypeSelect @0x770`, `CompTypeSelect @0x9F8`, `HarmColSelect @0x778`, `ThreadedBox @0x7D8`,
`QualitySelect @0xA28`):

| Off | Field | Values (without separators) | Data (594) |
|---|---|---|---|
| 0x194 | Filter type | 0…16 (§1.3) | 0–16 |
| 0x198 | Resonance type | 0 Classic, 1 Spring, 2 Pedestal, 3 Sedge hat, 4 Wide bump, 5 Double cone, 6 Well, 7 Wormhole, 8 Noise | 0–8 |
| 0x19C | Phaser type | 0 Classic, 1 Triangle, 2 Eggs, 3 Deep, 4 Deeper, 5 Condom, 6 Twins, 7 Cascade, 8 Box, 9 Freq | 0–9 |
| 0x1A0 | Compression | 0 Off, 1 Limiting, 2 Warming, 3 Heating, 4 Distortion | 0–4 |
| 0x1A4 | **Timbre color** | 0 Saw, 1 Bright saw, 2 Brighter saw, 3 Dephased saw, 4 Dephased saw 2, 5 Squaretooth, 6 Squaretooth 2, 7 Mole, 8 Double saw, 9 Triple saw, 10 Triple saw 2, 11 **Custom** | 0 (298), 11 (155) |
| 0x1A8 | Threaded (bool) | | 1 (583) |
| 0x1AC | Quality | 0/1 | |

### 1.5 Custom table (timbre color = Custom)

`n = 524` entries of 8 B = 1048 floats. The first half starts at 1.0 and falls ~1/k (1.0; 0.525; 0.353;
0.266…) = **amplitude of harmonic k**; the second half is another per-harmonic curve (phase?) **[HYP]**.
For LYNX: if the amplitudes are ~1/k, treat as a saw; if only odd ones are relevant, a square.

### 1.6 Statistics of the 160 channels in `.flp` **[DATA]**

- Timbre color: Saw 90, Custom 37, others 33. Mask mix > 0 in 37.
- Unison: order 1 (46), 2 (30), 3 (18), 5 (21), 7 (14), 9 (16)…
- Infinite volume decay in 102/160 (= full sustain); pluck off (1000) in 103.
- Mono 46, portamento 53. Adaptive env 146.
- Effects: chorus on 84, delay on 90, reverb on 117; phaser mix > 0 in 63, harmonizer 42.
- Examples (presets): "Bass Psy 1" A 0 / D 1.35 s / R 0.06 s, cutoff 4096 (~1.1 kHz), env 752, mono, pitch −2400;
  "EpicPad NUC" A 0.73 s / D ∞ / R 0.50 s, unison 9; "WobbleBass NUC" res 873, mono. Consistent with the
  conversions above.

### 1.7 How to approximate it in the LYNX synthesizer

1. **Oscillators** (timbre color + mask + sub):
   - Saw / Bright / Dephased / Custom ~1/k → `saw`. Squaretooth → osc1 `saw` + osc2 `square` (levels 0.5).
     Double/Triple saw → 2–3 `saw` with light detune (±7 ct). Mole → `triangle` **[HYP]**.
   - Mask mix m (23) with the default odd mask: `level(saw) = 1 − m`, `level(square) = m`
     (or just `square` if m ≥ 0.5 and there are no free oscillators).
   - Sub harmonic 1 (2) > 0 → osc3 `sine` one octave below, level = v/1024 (limit 1).
   - Transposition: `pitch (38)/100` semitones (split into octave + semitone).
2. **Unison** (65 ≥ 2): use the 3 oscillators with the same shape, detune `0, +d, −d` with
   `d = (41^(v67/256) − 1)·1.875` cents (minimum 5 ct), pan `±v66/128`. Total level × compensation
   gain (§1.2). If the order is 1, the free oscillators remain for mask/sub.
3. **Amplitude envelope:** attack = table (26). If decay (27) = 1000 → `decay = 0, sustain = 1`;
   otherwise `sustain = 0` and `decay = t27` (if the LYNX ADSR is exponential by time constant,
   use `τ = t27/4.6`). Release = t28 (likewise `τ = t28/4.6`).
4. **Filter:** LP types (0, 4, 8–13) → `lowpass`; BP (1, 5, 16) → `bandpass`; BS (2, 6) → `notch`;
   HP (3, 7) → `highpass`; Phaser/Band phaser (14, 15) → wide `bandpass`. Cutoff Hz = §1.3; LYNX
   resonance = `0.9·x59` (+0.1 if self-osc 62 > 0). Keytrack = x57.
5. **Filter envelope:** attack t48, decay t49, sustain 0, release = volume release; amount in
   octaves ≈ `x52 · 7.9` (non-adaptive) or `x52 · log2(17000/fc)` if positive and adaptive **[HYP]**.
6. **LFO:** shape (86) 0/1/8 → sine, 3/6 → tri, 2/4/5 → pulse; period = 64/2^(|v89|/1000) beats
   (in beats in LYNX). Destinations: pitch (42, in semitones = cents/100), cutoff (58, octaves ≈ 6·x),
   volume (25, tremolo). LYNX delay/fade-in = LFO attack (88). Vibrato (41) → extra pitch LFO
   ~5 Hz with depth `0.075·v` ct **[HYP]**.
7. **Mono/portamento:** mono = 44; porta on if 43; time in beats = `t45 · BPM/60`.
8. **Volume:** channel gain × `0.75·(3^(v36/1000) − 1)/2`.
9. **Effects (90–98):** LYNX has no rack in the synthesizer; if there are channel/mixer FX, map
   delay (type 2 = ping-pong, time = v95/192 beats, feedback x94, mix x93) and reverb (wet x97); otherwise
   ignore and record it in the import log.

---

## 2. MiniSynth **[DECOMP+STR+DATA]**

MiniSynth is the **FL Studio Mobile** synthesizer (C++, class `CMiniSynth`, waveform tables in
`oscdata.dat`). It does not use a DFM: the parameter list comes from the constructor (`FUN_180022a30`), which registers
43 names by index.

### 2.1 Layout of `0xD5` ("SSLF" container) **[DATA+DECOMP]**

| Off | Content |
|---|---|
| 0x00 | `"SSLF"` + u32 size of the rest |
| 0x08 | signature `99 46 4C 53 0D 0A 1A 0A` ("\x99FLS\r\n\x1a\n", PNG style) |
| 0x10 | chunk `"HEAD"` + u32 size: u16 1, u16 1, u16 `L`, `L` UTF-16 characters = **preset name** |
| … | chunk `"DATA"` + u32 size (0xB0 = 176): tag `"0rev"` + **43 floats 0…1** |

The total size varies only with the name ("Default" → 228 B, "Default Preset" → 242 B, empty → 214 B).

### 2.2 Parameters (float p ∈ [0,1]; `FUN_180023c20` = set, `FUN_180023580` = text, `FUN_1800234b0` = default)

| i | Name | Def. | Conversion |
|---|---|---|---|
| 0 | Voices Mode | 0.375 | `int(2p)`: 0 POLY, 1 MONO, 2 LEAD (mono legato **[HYP]**) |
| 1 | Transpose | 0.5 | `int((p − 0.5)·24)` semitones (±12) |
| 2 | Slide Time | 0 | per-block glide coefficient `0.9·(1−p)^4 + 0.01` (p=0: instantaneous; p=1: slow) |
| 3 | MW Dest | 0 | 0 Filter, 1 LFO |
| 4 | MW Amount | 0 | p (mod wheel → cutoff ±4 octaves·p) |
| 5 | **OSC Kind** | 0 | `int(9p)` (§2.3) |
| 6 | OSC Mod | 0 | p (width/detune/spread, depends on the Kind) |
| 7 | OSC Noise | 0 | noise level = p² |
| 8 | **FLT Freq** | 1 | §2.4 |
| 9 | FLT Peak | 0 | resonance = 0.99·p |
| 10 | FLT Kbd | 0 | keytracking p (octaves per octave, reference note 48) |
| 11 | FLT Vel | 0 | ±7 octaves·p·(vel − 0.5)·2 |
| 12–15 | EGF Atk / Dec / Sus / Rel | 0 / 1 / 1 / 1 | §2.5 (sustain = p) |
| 16 | EGF Amnt | 0.5 | a = 2p − 1 (bipolar) |
| 17 | LFO Shape | 0 | `int(2p)`: 0 SINE, 1 SAW, 2 SQUARE |
| 18 | LFO Dest | 0 | `int(2p)`: 0 FLT (cutoff ±4 oct·amt), 1 FRQ (pitch ±12 st·amt), 2 OSCMOD (adds 12·amt to OSC Mod) |
| 19 | LFO Rate | 0.25 | `int(11p)` → 1/4, 3/8, 1/2, 3/4, 1/1, 2/1, 4/1, 8/1, 16/1, 4 Bars, 8 Bars, 16 bars (internal value 0.25…256; **[HYP]** period in 1/16 steps; the label of index 9 is overwritten from "2 Bars" to "4 Bars") |
| 20 | LFO Amnt | 0.5 | 2p − 1 |
| 21–24 | AMP Atk / Dec / Sus / Rel | 0 / 1 / 1 / 0.07 | §2.5 |
| 25 | DIST (on) | 0 | bool |
| 26 | Overdrive | 0 | p |
| 27 | Decimator | 0 | p |
| 28–32 | DELAY on, Time (`(int(3p)+1)/4`: 1/4, 2/4, 3/4, 1/1), Fdbk, Filter, Mix | | |
| 33–36 | CHORUS on, ModRate, ModDepth, Mix | | |
| 37–42 | PHASER on, ModSpeed (table of 12 values 0.25…256), ModWidth, Fdbk, Mix, Freq | | |

### 2.3 Oscillators (`FUN_180021d10`) **[DECOMP]**

All are sums of table **saws** (with mip-maps) with gain ±1 and relative phase/frequency; `m` = OSC Mod:

| Kind | Name | Structure |
|---|---|---|
| 0 | SAW | saw + (saw − saw phase-shifted by `1−m` cycle) → saw with a pulse component (m=0: pure saw) |
| 1 | SAW+SAW | 2 saws: f and f·r(m) (r = detuning dependent on m **[HYP]**) |
| 2 | PULSE | saw − phase-shifted saw (`1 − m`) = **pulse with PWM** |
| 3 | SQR+SAW | square (saw − saw at ½ cycle) + saw at f·r(m) |
| 4 | SQR+SQR | 2 squares: f and f·r(m) |
| 5 | SUPERSAW | **8 saws**, gain 0.5, frequencies `f·(1 + (0.0125·k − 0.05)·m)`, k = 0…7 (−5 %…+3.75 % · m) |
| 6 | BELL | 6 saws at the same frequency with phases spread by `m/2` cycle |
| 7 | DEAF SAW | 2 saws summed, phase-shifted by `1 − m` |
| 8 | SAW SPREAD | saws at f/2·(1 − 0.01m), f and 2f·(1 + 0.01m) |
| 9 | SAW SPREAD 2 | same with 1.5f·(1 − 0.01m) instead of f/2 |

Tuning (`FUN_180022250`): `f = 220·2^((glide_note + transpose + drift + LFO − 57)/12)` (MIDI note;
random drift of ±0.08 st per voice) **[DECOMP]**.

### 2.4 Filter (`FUN_1800213e0`) **[DECOMP]**

**4-pole cascaded (ladder, 24 dB/oct)** low-pass with feedback `−4.05·peak` (self-oscillates
near peak = 1). Cutoff in octaves above 1 Hz:

`oct = 4 + 10.3·F + Kbd·(note − 48)/12 + 14·Vel·(vel − 0.5) + 0.103·drift (+ 4·LFO if dest FLT) (+ 4·MW)`
→ **`fc = 2^oct` Hz** (F = 0 → 16 Hz; 0.5 → 570 Hz; 0.75 → 3.4 kHz; 1 → 20 kHz, clamped to 20 kHz).
Envelope: `fc ← fc·(1 + (2^(10·a) − 1)·envF)` (a = bipolar EGF Amnt; a = +1 opens up to +10 octaves).

### 2.5 Envelopes (`FUN_1800229c0`, `FUN_180022440`) **[DECOMP]**

Per sample: `env += (target − env)·rate`. The attack chases target 4.0 and moves to decay when env > 1;
decay chases the sustain. With `k = 0.95p + 0.05`:
- attack rate = `0.0002/(500·k⁴)` → **t_attack ≈ 0.288·500·k⁴/0.0002 samples** (p=0.25: 0.11 s;
  0.5: 1.24 s; 1: 16 s at 44.1 kHz); p < 0.001 → instantaneous.
- filter decay / release: time constant **τ = 50·k⁴/0.0002 samples** (p=0.5: 0.43 s; 1: 5.7 s).
- amplitude release: τ = 1/10 of that with `k' = 0.95k + 0.05` (p=0.07 → ~1 ms; p=0.5 → 0.05 s;
  p=1 → 0.57 s).
- The rates are not corrected for the sample rate **[HYP: times scale with 44.1 kHz/fs]**.

### 2.6 Data (19 instances) and approximation in LYNX

Kinds used: SAW (9), SUPERSAW (8), SQR+SAW (2); all POLY; filter open in 10, peak ≤ 0.15 **[DATA]**.

- SAW → osc1 `saw`. PULSE → osc1 `square` (no PWM in LYNX). SQR+SAW → osc1 `square` + osc2 `saw`.
  SAW+SAW / SQR+SQR → two equal shapes with ±10 ct. SUPERSAW → 3 `saw` with detune
  `0, ±1200·log2(1 + 0.05·m)` ct. SAW SPREAD → `saw` at −1, 0, +1 octave. Noise → osc3 `noise`, level p².
- Filter `lowpass`, `cutoff = 2^(4 + 10.3·F)` Hz, resonance `0.99·peak`, keytrack = Kbd.
- Envelopes: convert with the formulas above (τ → LYNX time: decay ≈ 4.6·τ for −40 dB).
- Filter env amount in octaves = `10·(2p − 1)`.
- LFO: dest FLT → cutoff LFO (4·amt octaves), FRQ → pitch LFO (12·amt semitones).
- Mono = Voices ≥ 1; portamento only if Slide > 0.

---

## 3. Fruity DX10 **[DECOMP+STR+DATA]**

DX10 is a 2-operator FM in the style of mda's "DX10" (ported and extended by IL with a second
modulator). Names, *tags* and ranges from the DFM `TSynthEditorForm`.

### 3.1 Layout of `0xD5` (`SaveRestoreState` = `FUN_00734940`)

| Off | Content |
|---|---|
| 0x00 | u32 version: 1 (92 B) or 2 (460 B); ≥ 3 is ignored |
| 0x04 | **22 × i32** parameters (`P+0x1B8`) |
| 0x5C | (v2) 364 B: the same channel envelope/filter snapshot as in 3x Osc (round 4 §1.4), applied only if ≥ 364 B are present |

Normalised value `p = (v − min)/(max − min)`: all 0…65536 except Octave (−3…3).

### 3.2 Parameters and conversions (`FUN_00735760`, text in `FUN_00735220`)

`fs` = sample rate. "τ" is the time constant of the 1-pole filter that shapes the envelope.

| i | Name (DFM) | Conversion |
|---|---|---|
| 0 | Attack | **τ = e^(8p − 8) s** (p=0: 0.3 ms; 0.5: 18 ms; 1: 1 s) |
| 1 | Decay | p > 0.98 → **no decay (full sustain)**; otherwise multiplicative decay with **τ = e^(8p − 5) s** (0.5: 0.37 s) — decays to zero |
| 2 | Release | **τ = e^(5p − 5) s** (0: 7 ms; 1: 1 s) |
| 3 | Mod 1 Coarse | integer part of the ratio = `floor(40.1·p²)` (0…40) |
| 4 | Mod 1 Fine | p < 0.5 → `+0.2·p²`; otherwise `round(8.9p)`: 4 → +0.25, 5 → +1/3, 6 → +0.5, 7 → +2/3, 8–9 → +0.75 |
| 5 | Mod 1 Init | initial index ∝ `0.0002·p²·44100/fs` |
| 6 | Mod 1 Time (decay) | τ = e^(7p − 6) s |
| 7 | Mod 1 Sustain | sustain index ∝ 0.0002·p² |
| 8 | Mod 1 Release | τ = e^(8p − 5) s |
| 9 | Mod 1 Velocity | sensitivity p |
| 10 | Vibrato | depth 0.001·p² |
| 11 | Waveform | "richness" = 0.5 − 3p² (modulator feedback/shape **[HYP]**) |
| 12 | Mod Thru | direct mix of the modulator into the output = 0.25·p² |
| 13 | LFO Rate | **25·p² Hz** (UI text "%7.2f") |
| 14–20 | Mod 2: Coarse, Fine, Init, Time, Sus, Rel, Vel | like 3–9 |
| 21 | Octave ("COARSE") | −3…+3. Base tuning = `8.1758 Hz·2^(oct+1)·2^(note/12)` → at 0 the DX10 sounds **one octave above** the MIDI note (mda heritage) **[DECOMP; HYP that "note" is the MIDI no.]** |

Final modulator ratio = coarse + fine (e.g.: default preset 9.02; "Thick Bass" 1.00; "Clunk Bass" 2.00;
"Robot" 0.33) **[DATA]**. The exact topology (mod 2 in series or parallel) was not read **[HYP]**.

### 3.3 Data (75 instances, 12 in `.flp`)

v1/92 B = 73, v2/460 B = 2. Attack 0 in 49; Octave 0 (32), −1 (20), 2 (10); Mod 2 off (Init 0) in 48.

### 3.4 Approximation in LYNX (no FM)

- osc1 `sine` (carrier) with octave = `Octave + 1`.
- FM brightness → osc2 shape from the ratio R and initial index I (p5): R ≈ 1 → `saw`; R ≈ 2 → `square`;
  integer R ≥ 3 → `sine` at harmonic R (semitones = 12·log2 R) with level ∝ p5; non-integer R → detuned osc2 `sine`
  (metallic sound). osc2 level ≈ `min(1, 2·p5)`.
- The index decay (Mod Time p6 → Sus p7) becomes a **filter envelope**: LP with high cutoff,
  amount = `+4·p5` octaves, decay ≈ 4.6·τ6, sustain = p7/p5.
- Mod Thru → osc3 `sine` at the modulator frequency, level 0.25·p12².
- Amplitude: attack ≈ 3·τ0 (to reach ~95 %), decay = 4.6·τ1 with sustain 0 (or sustain 1 if p1 > 0.98),
  release 4.6·τ2.
- Vibrato: pitch LFO, `25·p13²` Hz.

---

## 4. Fruit Kick **[DECOMP+STR+DATA]**

### 4.1 Layout (`SaveRestoreState` = `FUN_0072d690`)

| Version | Layout |
|---|---|
| 0 (32 B) | u32 0 + 6 × i32 parameters + 1 unused i32 (the reader reads 28 B; garbage such as 4504291) |
| 1 (400 B) | u32 1 + 6 × i32 + 372 B: 364 B channel snapshot (as in §3.1) if ≥ 364 B |

### 4.2 Parameters (Tag = index; `ProcessParam` `FUN_0072dce0`, `TriggerVoice` `FUN_0072e2f0`)

| i | Name | Range (def.) | Conversion |
|---|---|---|---|
| 0 | Max freq | −2400…3600 (0) | initial pitch (ct) = `max + notePitch + 1200`, clamped to [final pitch, 4800] |
| 1 | Min freq | −1200…1200 (−600) | final pitch (ct) = `min − 3600`, fixed (does not follow the note) |
| 2 | Freq decay | 0…256 (64) | per-sample coef. `0.9997 − (1001^(1−v/256) − 1)·9.7e−6` (adjusted to fs) → τ: 0 → 2 ms, 64 → 11 ms, 128 → 38 ms, 256 → 76 ms |
| 3 | Amp decay | 0…256 (128) | coef. `0.99998 − (1001^(1−v/256) − 1)·4.98e−6` → τ: 64 → 25 ms, 128 → 0.13 s, 256 → 1.13 s |
| 4 | Click | 0…64 (32) | click level |
| 5 | Distortion | 0…128 (0) | d = v/128: dry ×(1 − d), drive ×3.3d |

The "ct" are relative to the host's pitch reference **[HYP: 0 ct = C5 = 523.25 Hz ⇒ min −600 → ~46 Hz]**.
Initial amplitude 1.5. Presets: Default (0, −600, 64, 128, 32, 0), Low (−2400, −832, 143, 127, 0, 0),
Punchy (624, −8, 42, 86, 64, 20) **[DATA]**.

### 4.3 LYNX

osc1 `sine`; pitch envelope with attack 0, decay ≈ 4.6·τ(freq decay), amount (semitones) =
`(start − end)/100`; volume ADSR attack 0, decay ≈ 4.6·τ(amp decay), sustain 0; click → osc3 `noise`
with level `v/64·0.3` and a very short envelope (if LYNX has no per-oscillator envelope, use fast AM or
ignore). Distortion: no equivalent.

---

## 5. BooBass **[STR+DATA]**

`0xD5` = 16 B: u32 version 1 + 3 × i32 0…65535 (centre 32767): **[0] Bass, [1] Mid, [2] Treble** (DFM Tags 0, 1, 2).
The engine (modelled bass) was not analysed. LYNX: osc1 `saw` + osc2 `sine` −1 octave, LP with
`cutoff ≈ 300·2^(4·treble)` Hz, resonance 0.2, sine level ∝ Bass, short decay **[HYP]**.

## 6. Drumpad **[STR+DATA]**

FL Mobile physical drum plugin. `0xD5` = `"SSLF"` + u32 size (0x11C) + tag `"3PMD"` + patch name
in ASCII in a fixed field (e.g.: "War Drum Lo 01\0 War Drum 3") + **37 floats 0…1** from 0x90
(292/300 B). The DFM lists 37 controls (Tags 1…37: Retrig, Mallet amp/decay/noise/noise RP, Membrane
decay/cutoff/tension/phase/material/size/shape, LOW freq/reso, SOF freq/decay/level, MID freq/level/res/…,
HIGH freq/level/res, Lo-Fi, Hold, Pitch, 4 velocity sources/values); the float↔Tag order was not
confirmed **[HYP: float k = Tag k+1]**. 961 of the 975 are `.fst` presets; only 11 channels in `.flp`. No
useful mapping to 3 oscillators: use a generic "tom" (sine with pitch envelope + noise) or render.

---

## Recommendations for LYNX

1. **Dispatch on `0xC9`** (comparing case-insensitively: "Fruit kick"/"Fruit Kick",
   "Drumpad"/"DrumPad", "DX10"/"Fruity DX10") and, for all of them, **keep applying the channel events**
   (`0xDA` channel envelope/LFO, `0xDB` channel filter, rounds 3/4): these generators are hosted like
   3x Osc. The 364 B snapshots inside DX10 v2 and Fruit Kick v1 only serve as plan B.
2. **Harmless first** (160 channels): parser = `u32 version` + `100 × i32` (99 if version < 9) + `7 × i32`
   (if version ≥ 4) + optional custom table; ignore the final byte. Apply the conversions of §1.2 and the
   mapping of §1.7. Quick fidelity win: unison → 3 detuned oscillators, ADR envelope
   (decay 1000 = full sustain) and filter in Hz via the 73 Hz…17 kHz log scale.
3. **MiniSynth:** read the generic SSLF container (HEAD = name, DATA = `"0rev"` + floats) — the same
   container serves Drumpad and probably other FL Mobile plugins. Map Kind → shapes (§2.6)
   and the ladder filter → `lowpass` with `fc = 2^(4 + 10.3·F)`.
4. **DX10:** there is no FM in LYNX; the "sine + harmonic osc + filter envelope imitating the index"
   approximation (§3.4) is the best cost/benefit. If LYNX gains 2-operator FM, the direct mapping is trivial
   (ratio = coarse + fine, index with init→sus envelope).
5. **Fruit Kick:** maps well: sine + pitch envelope + amplitude decay.
6. **Record in the import log** what was discarded (Harmless internal FX, phaser/harmonizer,
   Drumpad, MiniSynth effects), so the user knows why it sounds different.
7. **Suggested validation:** render in FL the presets "Bass Psy 1", "EpicPad NUC" (Harmless), "Default"
   (MiniSynth), "Thick Bass" (DX10), "Default" (Fruit Kick) and compare envelope/spectrum with LYNX —
   in particular the pitch reference of Harmless (pitch −2400 on the basses) and of Fruit Kick.

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| `tmp/r10/findself.py` | Locates the VMT (self at VMT−0xC8) | DECOMP |
| Harmless `FUN_0077adb0` (VMT+3) | `SaveRestoreState`: versions, 100 params, 7 combos, custom table, final byte | DECOMP |
| Harmless `FUN_0077f630` (VMT+6) | `ProcessParam`: conversions of all parameters | DECOMP |
| Harmless `FUN_00781200` | Value text (ms, "Infinite", "Still", "Unison order", %/dB) | DECOMP |
| Harmless `FUN_00614c80` | Constants ln2, ln11, ln3, ln2/12 | DECOMP |
| Harmless constructor `0x77bb43…` | Filter range 73.4 Hz…17 kHz | DECOMP |
| Harmless `FUN_0077c170`, `FUN_0077ca30` | Filter log scale; curves per type (types 3/7 = HP) | DECOMP |
| Harmless `FUN_00784a10`, `FUN_005f5f60` | Voice render; exponential envelopes to −40 dB | DECOMP |
| Harmless DFM `TSynthEditorForm` + field table | Tags, ranges, defaults, combo items; offsets 0x6F8…0xA28 | STR |
| MiniSynth `FUN_180022a30` | Constructor: 43 parameter names | DECOMP |
| MiniSynth `FUN_180023c20`, `FUN_1800234b0`, `FUN_180023580` | Parameter set/default/text | DECOMP |
| MiniSynth `FUN_180021d10` | Structure of the 10 oscillator types | DECOMP |
| MiniSynth `FUN_180022250`, `FUN_1800213e0` | Tuning (220 Hz @ 57); ladder filter and cutoff | DECOMP |
| MiniSynth `FUN_1800229c0`, `FUN_180022440` | Envelope rates and shape | DECOMP |
| DX10 `FUN_00734940` (VMT+3), `FUN_00734f30` (VMT+6) | Layout and normalisation | DECOMP |
| DX10 `FUN_00735760`, `FUN_00735660/690`, `FUN_00735220` | Conversions, ratios, text | DECOMP |
| Fruit Kick `FUN_0072d690`, `FUN_0072dce0`, `FUN_0072dcb0`, `FUN_0072e2f0` | Layout, conversions, initial/final pitch | DECOMP |
| DFMs of DX10, Fruit Kick, BooBass, Drumpad | Names/Tags/ranges | STR |
| 594 Harmless, 19 MiniSynth, 75 DX10, 15 Fruit Kick, 7 BooBass, 976 Drumpad (`.flp` + `.fst`) | Versions, sizes, ranges, usage | DATA |
| Scripts `tmp/r10/collect.py`, `hstat.py` | Collection and statistics | DATA |

### Pending items

- Harmless: role of parameter 39 ("Detuning"), exact unit of the vibrato (41) and of the filter LFO (58),
  semantics of the "adaptive" envelope, pitch reference of the keytracking and of the master pitch.
- MiniSynth: detuning function r(m) (`FUN_1800371f0`) and unit of the LFO Rate.
- DX10: topology of the second modulator and exact use of "Waveform".
- Fruit Kick: host pitch reference (`FUN_0071b2c0`); scale of the note pitch (`PTR_DAT_00785c50`).
- Drumpad: float↔Tag order and synthesis model.
