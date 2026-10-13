# Behavioural analysis of FL Studio 2026: round 15 (REA) — effects the import still discards, Envelope Controller, X-Y/Control Surface and the parameter order of PoiZone, Toxic Biohazard and BassDrum

> **Nature of this document.** Continuation of `REA_rodada14.md` (Patcher, Gross Beat, controllers) and
> `REA_rodada13.md` (minor generators). *Clean-room* reverse engineering, behavioural only. No Image-Line code
> or pseudocode was copied: what is here are descriptions, data layouts and formulas
> written in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched in this round (all the states below are open `0xD5` blobs,
> written by the plugins themselves).

- **Targets** (local copies in `tmp/r15/bin/`, originals in `Plugins/Fruity/Effects/<name>/` and
  `Plugins/Fruity/Generators/<name>/`):

  | Plugin | DLL (SHA-256) | Language | State (`SaveRestoreState`) / parameter text |
  |---|---|---|---|
  | Pitch Shifter | `e7dc07f6…d10c` | Delphi | `FUN_0072c630` (VMT+3) / `FUN_0072d5b0` |
  | Frequency Shifter | `5aab5d96…59ca` + `Engine_x64.dll` `a7774536…d8fa` | Delphi + C++ engine | `FUN_006dd080` / engine `dispatch` (op 5 = name, op 6 = text `FUN_1800020a0`) |
  | Emphasis | `e64ca492…5e48` | Delphi | `FUN_00706090` / `FUN_00706ba0` |
  | Low Lifter | `a492a7aa…fa96` | Delphi | `FUN_006c6d50` / `FUN_006c7490` |
  | Fruity Squeeze | `ed797a2f…57dd` | Delphi | `FUN_006d30c0` / `FUN_006d3a50` |
  | Transporter | `12d1116e…adda` | Delphi | `FUN_00713760` / `FUN_00713f40` |
  | Vintage Phaser | `3f7c558a…0d0f` (+ `engine_x64.dll`) | Delphi + C++ engine | `FUN_006ea8e0` / `FUN_006eb7f0` |
  | Fruity Send | `03add028…4f09` | Delphi | `FUN_006bd0f0` / `FUN_006bd930` |
  | Pitcher | `965db573…a73d` | C++ | (round 14) + `setParam` `FUN_18001cab0`, scale `FUN_18001d890` |
  | Fruity Envelope Controller | `e60f7462…c763` | Delphi | `FUN_008ad310` / `FUN_008aef80`; voice `FUN_008af660`; *tick* `FUN_008afec0` |
  | Fruity X-Y Controller | `c78e1ff2…c19b` | Delphi | `FUN_006c5d20` / `ProcessParam` `FUN_006c6650` |
  | Control Surface | `ad086c77…ed6c` | Delphi | `FUN_00854f70` → writer `FUN_00855120` |
  | PoiZone (engine) | `engine_x64.dll` `1208a111…e518` | C++ | state `FUN_180031020`, names `FUN_180030920`, text `FUN_18002f810` |
  | Toxic Biohazard | `5c326637…cb46` | C++ | names `FUN_18003b6e0`, state `FUN_180041b00` (class `ToxicSynth`) |
  | BassDrum (engine) | `engine_x64.dll` `2fb1b2f8…5e8b` | C++ | state `FUN_18002bce0`, names in the constructor `FUN_18002a380`, text `FUN_18002ace0` |

- **Tools:** Ghidra 12.1.4 *headless* in dedicated projects (`/tmp/r15gh_<dll>`), scripts in
  `tmp/r15/gh/scripts/` (`Decomp`, `Dis`, `StrRefs`, `ScalarFind`, `DecompGrep` from rounds 12–14, plus `Sym`
  — lists RTTI symbols/vtables). DFMs read with `tmp/r14/tools/dfm.py` + filter `tmp/r15/dfmc.py` (Tag = parameter
  index). Published fields of the Delphi classes (offset → control name) with `tmp/r15/fields.py`
  (field table at VMT−0xA0). VMT of `TTestPlug` with `tmp/r14/tools/vmts.py` + `tmp/r15/vmt.py`.
- **Collection:** `tmp/r14/blobs14.pkl` (mixer slots, channels, `.fst`). Supporting decoders for this round:
  `tmp/r15/dump.py` (blob → words), `tmp/r15/envctl.py` (complete Envelope Controller), `tmp/r15/cs.py`
  (Control Surface chunks), `tmp/r15/count.py` (counting).
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

**Conventions.** `x` = normalized parameter value, `(v − min)/(max − min)` with the control's range from the DFM
(this is how all the Delphi plugins in this round compute the text: `(v − ctrl+0x3A8)·ctrl+0x3CC`, clipped to
0…1). `lerp(a, b, c)` = `a + (b − a)·x^(1/c)` (form used by Emphasis/Low Lifter: `exp(ln(x)/c)`).
`curve11(x)` = `(11^x − 1)/10` — FL's volume curve, which LYNX already has as `curve(11, x)` in
`FlEffects.cpp`.

### Counts in the data (priority) **[DATA]**

Mixer slots in `Data/Demo projects/**.flp` (in parentheses: all the `.flp` files in `Data`).

| Plugin | Slots in demos | (all) | Demo files | `.fst` | Size of the `0xD5` |
|---|---|---|---|---|---|
| Pitcher | 39 | (61) | 7 | 4 | 160 B |
| **Pitch Shifter** | **14** | (15) | 6 | 31 | 75 B |
| Frequency Shifter | 4 | (5) | 3 | 13 | 52 B |
| Emphasis | 4 (all on the Master) | (5) | 4 | 26 | 64 B (v8) / 67 B (v10) |
| Low Lifter | 3 | (4) | 2 | 12 | 82 B |
| Fruity Squeeze | 1 | (1) | 1 | 10 | 52 B |
| Transporter | 1 | (2) | 1 | 11 | 110 B (v8) / 85–94 B |
| Vintage Phaser | 1 | (1) | 1 | 21 | 149 B (v3) / 132 B (v1) |
| Fruity Send | 1 | (1) | 1 | 4 | 16 B |
| Fruity X-Y Controller | 1 | (19) | 1 | 9 | 74–81 B |
| Control Surface | 3 | (17) | 3 | 4 | 80 B…29 KB |
| Fruity Formula Controller | 2 | (2) | 1 | 23 | — |

Generators (channels in the demo `.flp` files): Fruity Envelope Controller 4 (+26 `.fst`), Toxic Biohazard 3, BassDrum 3
(+2 in templates), PoiZone 1 (+389 `.fst`).

---

## 1. Pitcher — the missing parameters (21, 22, 25…33) **[DECOMP `FUN_18001cab0`, `FUN_18001d020`, `FUN_18001d890` + DATA]**

Complements `REA_rodada14.md` §3. The 34 `f32` of the state are **raw** values (not normalized): 0…1 for the
continuous ones, integers for the selectors. The `setParam` (`FUN_18001cab0`) and the defaults table (`FUN_18001d020`)
give the meaning:

| i | Name (registry / deduction) | Value | Effect in the engine |
|---|---|---|---|
| 0 | Correction Speed | 0…1 (default 1) | correction coefficient ∝ `8.82 / (sr·(3·(1−v)³ + 0.001))`: v = 1 → instantaneous correction; v = 0.6 → time constant ≈ 0.07 s **[DECOMP; unit HYP]** |
| 2 | Gender | 0…1 (0.5) | formant shift (0.5 = neutral) |
| 3 | Fine Tune | 0…1 (0.5) | `2v − 1` (±1, fraction of a semitone **[HYP]**) |
| 4 | Bypass All | 0/1 | turns the correction on/off |
| 5…16 | Key C…B Bypass | 0/1 | manual note mask (recomputes the scale mask) |
| 17…20 | Voice 1–4 Pan | 0…1 (0.5) | pan of the harmony voices |
| 21 | — | always 0 in the data | not handled by `setParam` (only stored) |
| 22 | (button) | 0/1 | turns on a mode and forces an internal restart — candidate: **Harmonize** **[HYP; STR `BTN Harmonize.tga`]** |
| 23 | Note A Freq | 0…1 (0.5) | frequency of the reference A (0.5 = 440 Hz **[HYP]**) |
| 24 | Pitch Reference | 0…1 | `int(2v)` → 0/1/2 |
| 25 | (button) | 0/1 | when 1, the scale mask is **not** recomputed from parameters 28/29 → notes come from MIDI: **MIDI** **[strong HYP; STR `BTN Midi.tga`]** |
| 26, 27 | (buttons) | 0/1 | two boolean options (candidates: **Formant**, *stereo*/other) **[HYP; STR `BTN Formant.tga`]** |
| 28 | **Scale tonic** | integer 0…11 (C = 0) | rotates the scale mask (`FUN_18001d890`) **[DECOMP]** |
| 29 | **Scale type** | integer (default 3) | 0 = **natural minor**, 1 = **major**, 2 = C, D♯, F, G (4 notes), 3 = **chromatic** (all), 4 = tonic only, 1000 = none **[DECOMP]** |
| 30, 31 | — | always 0 | not handled |
| 32 | **Voice range** | integer 0…5 (default 2) | minimum detection frequency: 220, 170, **110**, 80, 55, 25 Hz (soprano → very low) **[DECOMP]** |
| 33 | MIDI port | 0…1 | `int(256·v) − 1` (−1 = none) **[HYP; STR "MIDI Input Port Number:"]** |

The 12 bytes between the floats (offset 136 of the block) are the **resulting note mask** (1 byte per pitch
class, C…B). Checked: tonic 6 + scale 0 ⇒ mask `0 1 1 0 1 0 1 0 1 1 0 1` = **F♯ minor** (13 slots
in the demos); tonic 3/scale 0 = D♯ minor; tonic 4/scale 0 = E minor; tonic 7/scale 1 = G major
**[DATA]**.

**Approximation in LYNX:** still no equivalent (pitch correction by detection). If `Gender = 0.5`,
`Bypass All = 0` and the MIDI/harmony mode is off (22 = 25 = 0), pass the signal straight through **without an
alarming warning** (on an already-tuned vocal the audible effect is small). With harmony/MIDI on, record it in the log.

---

## 2. Pitch Shifter **[DECOMP `FUN_0072c630`, `FUN_0072d5b0`, `FUN_0072cda0` + STR + DATA]**

Pitch shifter with two engines: **FFT** (with formant preservation/shifting) and **granular**
(grains with duration, density and randomness), and an optional *delay* before/after the shift.

### 2.1 Layout of the `0xD5` (75 B) **[DECOMP + DATA]**

| Off | Type | Content |
|---|---|---|
| 0 | i32 | version (7; the reader accepts 6 and 7) |
| 4 | 13 × i32 | **parameters 0…12** (DFM Tag) |
| 56 | i32 | position of the *Formant correction* control (copy of parameter 11) |
| 60 | u8 | **Tempo sync** of the delay (checkbox `TempoSyncCheckBox`) |
| 61 | i32 | **engine** (`PluginTypeSelect`): 0 = FFT, 1 = GRANULAR |
| 65 | i32 | pitch detector quality (`PitchDetectorCombo`): 0 = fast, 1 = accurate |
| 69 | i32 | spectrum speed (500/1000/2000 = fast/medium/slow — UI only) |
| 73 | u8, u8 | spectrum on, monochrome (UI only) |

The field names come from the published field table of `TSynthEditorForm` (offsets 0x6F8, 0x718, 0x810,
0x818) **[DECOMP+STR]**. Version 6 lacks the last 3 fields.

### 2.2 Parameters and conversions (`FUN_0072d5b0`) **[DECOMP]**

| i | Name | Range (default) | Real value |
|---|---|---|---|
| 0 | **Pitch** | −1200…1200 (0) | `24x − 12` semitones = **v/100 semitones** (cents) |
| 1 | **Formant** | −1200…1200 (0) | likewise, formant shift (FFT mode only) |
| 2 | Analysis channel | 0…2 | LEFT / RIGHT / LEFT+RIGHT |
| 3 | **Mix** | 0…2400 (2400) | `100x` % of processed signal |
| 4 | Duration (grain) | 2000…15000 (8500) | `20 + 130x` ms → **20…150 ms** (default 85 ms) |
| 5 | Density | 100…500 (200/300) | `100 + 400x` % (= v %) — grain overlap |
| 6 | Jitter | 0…100 | % (randomness of the grain position) |
| 7 | Rand | 0…100 | % (randomness of the grain's pitch/timing **[HYP]**) |
| 8 | Delay | 1…768 (768) | without *tempo sync*: `1 + 999x` ms; with sync: `v/48` *steps* (768 = 16 steps = 1 bar) |
| 9 | Feedback | 0…100 | % (delay feedback) |
| 10 | Delay mode | 0…3 | OFF / POST (delay after the shift) / PRE (before) / NO SHIFT (delay only) |
| 11 | Formant correction | 0…65536 (65536) | `100x` % (how much the FFT engine preserves the spectral envelope) |
| 12 | Pitch mode | 0…3 | RELATIVE / ABSOLUTE C4 / C5 / C6 (absolute: forces the detected pitch to the note **[HYP]**) |

The `ProcessParam` redraws the visualization when parameter 0 changes, 1 in FFT mode or 4 in granular mode
— confirms which parameters belong to each engine **[DECOMP]**.

### 2.3 In the demos (14 slots) **[DATA]**

Pitch: 0 (5×, probably automated), ±12 st (6×), +8, +2, +10.15, −5. Engine: FFT 7, granular 7. Delay
mode = OFF in **all**. Formant ≠ 0 in 4 (−3, −12, −1.65, −5 st). Mix < 100 % in 4 (27 %, 34 %, 51 %, 60 %).

### 2.4 Behaviour to implement (algorithm) **[HYP: description of the audible effect, not of the code]**

- **Granular (mode 1):** circular buffer; grains of `D` ms with a Hann window read at a rate `r = 2^(st/12)`;
  a new grain starts every `D/(density/100)` ms (density 200 % = 2 overlapping grains); *jitter* shifts the
  read start randomly by up to `jitter% · D`; *rand* slightly varies `r`/duration per grain. It is the same
  principle as the two *crossfading* heads of LYNX's current `PitchShift` (window = `D`).
- **FFT (mode 0):** *phase vocoder* with bin shifting by `r` and, independently, shifting of the
  spectral envelope by `2^(formant/12)`; *formant correction* = how much of the original envelope is reapplied after the
  shift (100 % = timbre preserved, 0 % = "Chipmunk" effect). Without FFT in LYNX, approximate with the granular
  engine with a ~60 ms window.
- **Delay:** delay line with feedback `fb%`, applied before (PRE) or after (POST) the shift.
- **Mix:** `output = (1 − m)·dry + m·processed`.

---

## 3. Frequency Shifter **[DECOMP `FUN_006dd080` + engine `dispatch`, `FUN_1800020a0`, `FUN_1800023c0`, `FUN_180001fd0` + DATA]**

Shifts all components by **fixed Hz** (single sideband) or does *ring modulation*; L/R phase
LFO, feedback and tempo-synced mode.

### 3.1 Layout of the `0xD5` (52 B) **[DECOMP + DATA]**

`i32` version (4) · **10 × i32 = parameters 0…9** · `i32` flags (bit 0 = *Linked mode* button of the shapes) ·
`i32` *sidechain* track (`SideChainSelect`, 0 = none).

### 3.2 Parameters (engine names, op 5) and conversions (op 6 / `setParam`) **[DECOMP]**

The engine receives normalized `x`.

| i | Name | Range (default) | Real value |
|---|---|---|---|
| 0 | **Mix** | 0…10000 (10000) | wet gain `min(1, 2x)`, dry `min(1, 2 − 2x)` (0.5 = both at full; 1 = processed only) |
| 1 | **Type** | 0…2 (1) | 0 = *Frequency Shifter HQ*, 1 = *Frequency Shifter*, 2 = **Ring Modulator** (`x < 1/3`, `< 2/3`, rest) |
| 2 | **Frequency** | −70000…70000 (0) | see below |
| 3 | L/R Phase Offset | 0…10000 | `0.25·x` cycle (0…90°) between the channels |
| 4, 5 | L / R Shape | 0…10000 (10000) | mixes `x` of the shifted band with `1 − x` of the mirrored band (1 = single sideband only) **[HYP on the meaning]** |
| 6 | Feedback | 0…10000 (5000) | `1.005·(2x − 1)` (±100 %; 0.5 = no feedback) |
| 7 | Mono↔Stereo | 0…10000 (10000) | `(1 − x)·0.5` of spread |
| 8 | **Range** | 0…2 (1) | 0 = **20 kHz**, 1 = **200 Hz**, 2 = **Tempo** |
| 9 | Left Phase Offset (start phase) | 0…10000 (5000) | `x + 0.5` cycle |

**Frequency** (`FUN_180001fd0`): with `s = 2x − 1` (−1…1), `|f| = 0.1 Hz · 2^(K·|s|)`, sign of `s`; **K = 17.6096**
in the 20 kHz range (max. = 20,000 Hz) and **K = 10.9658** in the 200 Hz range (max. = 200 Hz); `s = 0` → 0 Hz. In the
*Tempo* range, the value is a synced period displayed as `bars:48ths` (`(1 − x)` → time **[HYP]**). In
*Ring Modulator* only the magnitude counts (0.1 Hz·2^(K·x)). Checked in the presets: "+800 Hz" (51542, range 0) →
800.2 Hz; "−800 Hz" (−51537) → −799.9 Hz; default (70000, range 1) → +200 Hz **[DATA]**.

### 3.3 In the demos **[DATA]**

"Bien Duro" ins. 17: −71 Hz (200 Hz range), 100 % wet; ins. 65: +561 Hz (20 kHz range); "Africa's Calling"
ins. 45: +8.0 Hz (the classic "slow phaser"); "Madonna" ins. 21: mix 0 = dry (effective bypass). All type 0
(HQ), feedback 0, no sidechain.

### 3.4 Algorithm **[HYP: classic behaviour of the effect]**

Analytic signal via Hilbert transform (pair of all-pass filters in quadrature), multiplied by
`e^{i2πft}`; output = real part (one sideband). *Ring*: `x·cos(2πft)` (two sidebands). Feedback: sum of the
output delayed by 1 short block into the input. LYNX's current `FreqShift` implements exactly the
"type 0/1, single band" case.

---

## 4. Emphasis (Master maximizer) **[DECOMP `FUN_00706090`, `FUN_00706ba0`, `FUN_007043f0`, `FUN_00705110` + DFM + DATA]**

### 4.1 Layout of the `0xD5` **[DECOMP]**

`i32` version (writer: 10; demos 8) · **13 × i32 = parameters 0…12** · `i32` UI state (`+0x870`) ·
`i32` (`+0x878`, v ≥ 7) · `u8` (v ≥ 9) · `u8`, `u8` (v ≥ 10: *Instant recovery* and *DC block*, DFM Tags 26/27
**[HYP on the order]**). In versions < 8 parameter 2 is incremented by 1 and clipped to 1…4.

### 4.2 Parameters **[DECOMP+STR]**

| i | Name | Range | Real value |
|---|---|---|---|
| 0 | Bypass | 0/1 | |
| 1 | Delta | 0/1 | listen to the difference only |
| 2 | Envelope order | 1…4 | integer |
| 3 | True peak | 0/1 | |
| 4 | Channel mode | 0/1 | L/R or M/S |
| 5 | Emphasis mode | 0…3 | Versatile / Loud / Transient / Steady |
| 6 | **Input gain** | 0…10000 | **`24x` dB** (linear in dB: `lerp(0, 24, 1)`) |
| 7 | Linking | 0…10000 | `100x` % |
| 8 | Emphasis amount | 0…10000 | `100x` % (0 = "OFF") |
| 9 | **Output ceiling** | 0…10000 | **`20·log10(curve11(x))` dB** (x = 1 → 0 dB; 0.9308 → −1.6 dB; 0.99 → −0.23 dB) |
| 10 | Level matching | 0/1 | |
| 11 | Oversampling | 0…4 | OFF / 2× / 4× / 8× / 16× |
| 12 | Hardness | 0…9 | integer |

(The constant `K` of the ceiling curve is initialized at run time; `(e^{K·x} − 1)·0.1` only equals 1 at x = 1
with `K = ln 11`, i.e. the same `curve11` as FL's faders **[DECOMP+calculation]**.)

### 4.3 In the demos (4 Masters) **[DATA]**

| Demo | Input | Ceiling | Mode |
|---|---|---|---|
| Bien Duro | +1.5 dB | −1.6 dB | Versatile |
| NIGHT TERROR | +6.0 dB | −0.23 dB | Versatile, M/S |
| With You | +2.1 dB | −0.29 dB | Steady |
| Use Me | +5.0 dB | −0.20 dB | Versatile |

**Approximation:** LYNX `limiter` with `params[1] = inputDb/24` (= **x directly**) and
`params[0] = (ceilingDb + 12)/12`; default release. It is faithful in the essentials (input gain + ceiling).

---

## 5. Low Lifter (bass enhancement) **[DECOMP `FUN_006c6d50`, `FUN_006c7490`, `FUN_006c54c0` + DFM + DATA]**

### 5.1 Layout (82 B) **[DECOMP]**

`i32` version (6) · **15 × i32 = parameters 0…14** · `u8`, `u8`, `i32`, `i32`, `i32`, `i32` (UI/
visual state; in the data `196609, 196608, 131072, 131072`).

### 5.2 Parameters **[DECOMP]**

| i | Name | Real value |
|---|---|---|
| 0 | Enabled | 0/1 |
| 1 | **Dry/Wet** | `100x` % |
| 2 | **Low band gain** | `lerp(−120, 12, 7.275)` dB (0 = mute; 0.4999 → 0 dB) |
| 3 | Harmonics decay slope | `lerp(−60, 0, 3.105)` dB |
| 4 | Lookahead | 0/1 |
| 5 | Invert harmonics polarity | 0/1 |
| 6 | **Pre-shaping boost** | `18x` dB (gain before the harmonics generator = "drive") |
| 7 | Diffuser mix | `x` (0…1) |
| 8 | Diffuser coefficient | `−0.99 + 1.98x` |
| 9 | **High band gain** | `lerp(−120, 0, 5)` dB (1 → 0 dB) |
| 10 | **Crossover cutoff** | `lerp(20, 120, 0.4989)` Hz ≈ `20 + 100x²` Hz |
| 11 | **Low cut** | same scale, 20…120 Hz |
| 12 | Multiband processing | 0/1 |
| 13, 14 | High band / Low band enabled | 0/1 |

Behaviour **[HYP, from the control names]**: the band below the *crossover* goes through a harmonics
generator (saturation) with *pre-boost*, whose harmonics decay with the given slope; a low-cut at *low
cut*; optional diffuser (all-pass); sum of the bands with their gains; dry/wet mix.

### 5.3 Demos **[DATA]**

"Bien Duro" ins. 32/41: mix 75 %/100 %, *pre-boost* +12.2 dB, slope −18 dB, *crossover* 80 Hz, *low cut*
48/38 Hz, multiband; "Song For You" ins. 3: *pre-boost* +9.4 dB, *crossover* 72 Hz, no diffuser.

**Approximation:** `parametricEq` with band 1 = *low cut* (high-pass 12 dB/oct) at `f(p11)` (if > 21 Hz),
band 2 = *low shelf* at `f(p10)` with gain `= lowGain − highGain + boost`, where
`boost ≈ 0.25·preBoostDb·mix` (the harmonics generator sounds like +2…+3 dB of perceived bass **[HYP]**), and
output = `highGain`. Optional: a light `distortion` before it is not recommended (it would saturate the whole range).

---

## 6. Fruity Squeeze **[DECOMP `FUN_006d30c0`, `FUN_006d3a50` + DFM + DATA]**

`0xD5` (52 B) = `i32` version (0) + **12 × i32 = parameters 0…11**:

| i | Name | Range (default) | Real value |
|---|---|---|---|
| 0 | Gain | 0…160 (127) | linear gain `v/127` (127 = 0 dB; 160 = +2 dB) |
| 1 | **Mix** | 0…128 (64) | dry `(128 − v)/128`, wet `v/128` |
| 2 | **Steps** | 1…32768 (1) | no. of quantization levels `= 32768^(1 − v/32768)` (1 → 32768 levels = clean; 12144 → ≈ 700 levels ≈ 9.5 bits) |
| 3 | Filter | 0/1 | turns the filter on |
| 4 | Filter type | 0…4 | LOW PASS / HIGH PASS / BAND PASS / NOTCH / PEAK |
| 5, 6 | Filter res. / freq. | 0…65536 | `x` (no unit in the text) |
| 7 | Filter position | 0/1 | pre / post |
| 8 | Relation | 0…3 | none / impact = preserve / ×2 / ÷2 |
| 9, 10 | Impact / Preserve | 0…44100 | `44101^x − 1` samples (× rate/44100) |
| 11 | Amount | 0…65536 | `x` (displayed in dB) |

Demo ("NIGHT TERROR" ins. 41): steps ≈ 700 levels, post LP filter at `x = 0.87`, mix 50 %. **Approximation:**
`distortion` with low *drive* (light quantization ≈ 9-bit noise) + `filter` LP (cutoff `20·1000^0.87` Hz
**[HYP]**), mix 0.5.

## 7. Transporter **[DECOMP `FUN_00713760`, `FUN_00713f40`, `FUN_007125f0` + DFM]**

Repeater/"looper" of segments detected by transients (A/B loops, probabilistic reverse, octave up/down).
`0xD5` = `i32` version (8) + **22 × i32 = parameters 0…21** (v < 8: 20) + `u8`, `u8` +
`i32` output send track + `i32` input track + `i32`, `i32`.

Parameters: 0 **Dry level**, 1 Loop level A, 2 Loop level B (0…12800; dB = `20·log10(curve11(x))`), 3–5
biases reverse / octave shift / octave down-up (`2x − 1`), 6 spread/auto-pan, 7 min. length A (`0.02 + 1.98x` s),
8 A/B ratio (`1 + 9x`), 9 transient sensitivity, 10 bouncing rate, 11 ignore transients, 12 flush, 13 loop mode
(Auto/Bouncing/Adaptive/Tempo), 14 bypass, 15–17 update loop A/B/AB, 18 probabilistic/deterministic, 19/20
tempo ratio length/trigger (1/1…1/16 quintuplet), 21 ignore bias **[DECOMP+STR]**.

Demo ("Song For You" ins. 44): dry −3.3 dB, loops −15/−14 dB. **No approximation**: import as `utility`
with the *dry level* gain (what sounds "underneath" the effect) and record it in the log.

## 8. Vintage Phaser **[DECOMP `FUN_006ea8e0`, `FUN_006eb7f0`, `FUN_006eb680` + DFM]**

`0xD5` = `i32` version (3) + **36 × i32 = parameters 0…35** + `u8` (v1: 32 parameters). Main conversions:
frequencies 0 (min.), 1 (max.), 4 (manual) = `1.25 Hz · 2^(14x)` (up to 20.5 kHz); 2 feedback = `99x` %; 6
LFO speed = exponential interpolation between the frequencies of parameters 29/30 (*rate frequency* 1/2),
or synced (7); 21 mix = `100x` % dry/wet; 15 *delay stereo* (center/left/right); 16 delay time `x`
ms or steps; 23 noise in dB; 33 engine V1/V2. Single demo ("Stroll Up" ins. 6, preset "Lowgrounder").
**Approximation:** `chorus` with high depth, *feedback* = `p2·0.99`, converted speed and mix = `p21`
**[HYP; sounds different — phaser ≠ chorus]**.

## 9. Fruity Send **[DECOMP `FUN_006bd0f0`, `FUN_006bd930`]**

`0xD5` (16 B) = **3 × i32 = parameters 0…2** + `i32` destination track (−1 = none):

| i | Name | Range (default) | Real value |
|---|---|---|---|
| 0 | Send pan | −128…128 | `v/128` |
| 1 | **Send volume** | 0…320 (256) | `curve11(v/256)` (256 = 0 dB; 320 ≈ +5.6 dB) |
| 2 | **Dry level** | 0…256 (256) | linear gain `v/256` of the signal that continues on the track |

Demo ("Madonna" ins. 1): dry 197 → **−2.3 dB**, destination −1 (sends nothing). **Approximation:** `utility` with
gain `20·log10(dry/256)`; if there is a destination ≥ 0, create a post-effect *send* (at the slot's position) to that track
with level `curve11(vol/256)` and pan `p0/128`.

---

## 10. Fruity Envelope Controller **[DECOMP `FUN_008ad310`, `FUN_008af660`, `FUN_008afec0`, `FUN_008ae740`, `FUN_008b11c0`, `FUN_008b0f90`, `FUN_007e0170` + DFM + DATA]**

"Controller" generator: produces no audio. It has **8 articulators** (envelope + LFO + maps, the same
`TArticulator`/`TCustomMEnvelope` as Sytrus, `REA_rodada11.md` §1.6); **controller output k is
articulator k** (k = 0…7), and that is what the `0xE3` links with source `channel<<16 | 0x8000+k` refer to
(`REA_rodada14.md` §7.1).

### 10.1 Complete layout of the `0xD5` **[DECOMP `FUN_008ad310` + DATA: the 30 blobs are consumed exactly]**

```
u32 version             0x00060003 (writer; the reader uses the high half if ≥ 0x10000) | 6 | 5 (old)
i32 × N  parameters     N = 90 (v ≥ 6) = 8 articulators × 11 + Mod X + Mod Y;  N = 44 (v < 6) = 4 × 11
i32  articulator shown in the UI (0…7)
i32  part shown in the editor (0 = envelope, 1 = LFO, …) (only v ≥ 1)
i32  flags: bit 0 = Freeze (editor), bit 1 = Slide (editor), bit 2 = continuous output (see §10.4)
i32  position of the ModeSelect combo (UI)
repeat 8× (v < 6: 4×):
  i32 articulator flags (only v ≥ 2): bit 0 = BIPOLAR mode (sum); 0 = UNIPOLAR (product)
  articulator (Sytrus format):
     i32 version (3) · f32 base · f32 envelope level · f32 LFO level      (= parameters/256)
     envelope (kind 1) + 4 × i32 A/D/S/R
     LFO (kind 2) + 4 × i32 (speed, tension, skew, pulse width) + i32 (version ≥ 3)
     5 maps (kind 3): keyboard, velocity, Mod X, Mod Y, random     (v < 6: 2 maps)
  i32 minimum note, i32 maximum note          (default 0 and 256 = all keys)
```

Each envelope/LFO/map object is the `TCustomMEnvelope` already described (`kind`, `version`, `n`, `n × {f64 Δx, f64 y,
f32 tension, 4 B curve type}`, 5 × i32: flags (bit 0 TEMPO), **enabled** (−1/0), decay point, loop
start, sustain/loop end) — the `readEnvelope` reader in `FlSytrus.cpp` works unchanged.

### 10.2 The 11 parameters of each articulator (index = 11·k + s) **[DECOMP `FUN_008ae740`]**

| s | Parameter | Range (default) | Use |
|---|---|---|---|
| 0 | **Base level** | 0…256 (128) | resting value, `/256` |
| 1 | **Envelope level** | −256…256 (256) | envelope depth, `/256` |
| 2 | **LFO level** | −256…256 (128) | LFO depth, `/256` |
| 3…6 | Env A / D / S / R | (128, 128, 0, 128) | envelope scales (the same as Sytrus: times ×v/128, sustain +v/128) |
| 7…10 | LFO speed / tension / skew / PW | (40000 or 40898, 0, 0, 0) | speed = period `625/(10001^(v/65536) − 1)` (s, or beats with TEMPO) |
| — | 88 / 89 | **Mod X / Mod Y** global | 0…65536 (32768) | input of the Mod X/Y maps = `v/65536` + the note's X/Y |

### 10.3 Triggering and output computation **[DECOMP `FUN_008af660` (TriggerVoice), `FUN_008afca0` (Release), `FUN_008afec0` (NewTick)]**

- **Triggering:** each note of the controller's **own channel** (patterns/piano roll) creates a "voice". For each
  articulator whose note range contains the key (§10.1, `min ≤ note ≤ max`), the articulator is **removed from the
  previous voices** and follows only the new note — i.e. **each articulator is monophonic, last note
  wins**, and restarts envelope and LFO. The voice stores the values of the keyboard maps (`note/120`), velocity
  (logarithmic velocity curve, 0…1) and random, computed once at trigger time.
- **Releasing the note** (`Voice_Release`) sends envelope and LFO to the release phase (skips the sustain point).
  The voice dies when envelope and LFO finish.
- **On every sequencer *tick*** (`NewTick`, not per sample) and for each active articulator, the value is
  built starting from `v = base`:
  1. envelope enabled: `v += env(t)·envLevel`;
  2. LFO enabled: bipolar mode → `v += lfo(t)·lfoLevel`; unipolar mode → `v *= 1 + 2·lfo(t)·lfoLevel`;
  3. each non-neutral map (keyboard, Mod X, Mod Y) with its input: bipolar → `v += map`, unipolar →
     `v *= map`;
  4. combines with the voice's velocity/random factors (same rule);
  5. **clips to 0…1** and, if it changed, sends it to the host as the value of output k (scale 2³⁰ = 1.0).
  In bipolar mode, envelope, LFO and maps are **centered on 0.5** (the articulator passes 0.5 as "neutral" to
  the parts — `FUN_008b0f90` → `FUN_007d1600`), so what is added is `(y − 0.5)`; in unipolar the neutral is 0 (for
  sums) / 1 (for products) **[DECOMP; the exact offset within each part strong HYP]**.
- **No active note:** if bit 2 of the global flags is set ("Continuous output" **[HYP from the menu]**), the output
  is recomputed continuously from base + Mod X/Y maps only (this is what the "Smart X/XY" presets do); otherwise,
  **the output stays frozen at the last value**.
- **Time units:** the same as Sytrus: `Δx` of the points in **seconds**, or in **beats (quarter notes)**
  when the TEMPO bit of the envelope flags is set **[strong HYP; same class `TMEnvelope`]**.

### 10.4 The 4 channels in the demos **[DATA `tmp/r15/envctl.py`]**

| Demo / channel | Articulators used | Reading |
|---|---|---|
| "Future House" / *Basic Sidechain* | 0–3, **unipolar**, bases 14/0/25/128 (/256) | art. 0: `0 → 0.754 in 0.237 s → 1.0 in +0.092 s` (tensions −0.30/+0.18), no sustain: on each note it drops to ≈ 0.05 and returns to 1 in ~⅓ s (*ducking*); art. 1 the same with 0.47 s; art. 2/3 ramps 0→1 in 0.35/0.40 s |
| "Madonna" / *Harmony Notes* | 0, bipolar | envelope enabled `0 → 1 → 1 → 0` (gate: 1 while the note lasts) |
| "FlashyWaves" (ZGE) | 0, bipolar | instantaneous attack to 0.75, falls to 0 in 0.6 s |
| "Steptime" (v5, 4 articulators) | 0 | `0 → 1 → 1 (0.8 s) → 0` |

### 10.5 How to turn it into a modulation source in LYNX

See Recommendations §2. Summary: for each `0xE3` link with source `channel<<16 | 0x8000+k`, the target's value at
instant `t` is `clamp(formula of §10.3 with articulator k)`, where the envelope is restarted on each note of the
controller's channel (`t − note start`, release at the note's end). The result (0…1) goes through the link curve, as
the Peak Controller already does.

---

## 11. Fruity X-Y Controller and Control Surface: where the current value is

### 11.1 X-Y Controller **[DECOMP `FUN_006c5d20`, `FUN_006c6650` + DFM + DATA]**

`0xD5` = `i32` version (2) + **10 × i32 = parameters 0…9** + `u8` + *shortstring* grid name + `i32` +
5 × `i32` (v < 2: an extra `u8` + `i32` before the final block).

| i | Parameter | Range (default) |
|---|---|---|
| 0 | **X level (current position)** | 0…65536 (32768) |
| 1, 2 | X min / X max | 0…65536 (0 / 65536) |
| 3 | X tension | −128…128 |
| 4 | **Y level (current position)** | 0…65536 (32768) |
| 5, 6 | Y min / Y max | |
| 7 | Y tension | |
| 8, 9 | Speed / Acceleration (movement smoothing) | 1024…2048 / 256…1024 |

The `ProcessParam` treats parameters 0–7 as **2 axes × 4** (`axis = i div 4`) **[DECOMP]** → **output 0 = X,
output 1 = Y** **[strong HYP]**. Constant value to apply: `min + (max − min)·tension(level/65536)` with
min/max/65536; with tension 0, linear. In the demo ("Drumloop Masher" ins. 1): X = 0.5, Y = 0.

### 11.2 Control Surface **[DECOMP `FUN_00855120`, `FUN_008534b0` + DATA]**

`0xD5` = `i32` version (1; −1 = old format) + chunks `u32 id · u64 size · data` (same envelope as the
Patcher): `2000` (64 B, header), `0x7D2` (surface size), `0x7D3`, and per control:

| id | Content |
|---|---|
| `0x834` (32 B) | control type in the 1st `i32` (3 = button, 2 = knob/fader, 0 = pad/2D… **[HYP]**) |
| `0x837` | name (UTF-16) |
| `0x838` (16 B) | rectangle (4 × f32) |
| **`0x836`** (12 B, one per control value) | **`f32` current value (0…1)**, 4 B of the value's 2nd attribute, **`i32` index of the published output** (−1 = not published) |
| `0x839`/`0x83A`/`0x83B` | properties, colors, "published" (text) |
| `0x835` | end of control |

The index in the 3rd field is looked up in the plugin's table of published outputs (`FUN_008534b0`) — it is the **k** of the link
`0x7…8000+k`. Demos: "Future House" button *Transition FX* = output 0, value 0; "Delay 3"/"Transient Processor":
ON/OFF button = output 0, value 1.0 **[DATA]**.

---

## 12. Parameter order of PoiZone, Toxic Biohazard and BassDrum

### 12.1 PoiZone **[DECOMP engine: `FUN_180031020` (state), `FUN_180030920` (names), `FUN_18002f810` (text)]**

State = `"SSLF"` + `u32 0x2B4` + tag `"Ver4"` + **156 × f32 = parameters 0…155 in index order** (the writer
calls `getParameter(i)` in a loop) + 64 B preset name. **Confirmed: float k = parameter k.** Names (0…73) and
conversions (`x` = value 0…1):

| i | Name | Conversion | i | Name | Conversion |
|---|---|---|---|---|---|
| 0 | OscA Shape | 0 = Saw, > 0 = Pulse | 26 | LFO Amount | `200x − 100` % |
| 1 | OscA PW | `100x` % | 27 | LFO Retrig | on/off |
| 2 | OscA Sync | on/off | 28 | **LFO Rate** | `30·x⁵` Hz |
| 3 | OscA Ring | on/off | 29, 30 | Unison Detune / Pan | % |
| 4 | OscB Shape | 0 = Saw, > 0 = Pulse | 31 | Delay ON/OFF | |
| 5 | OscB PW | % | 32, 33 | Delay Time L/R | `(1 + ⌊7x⌋)/4` (quarters) |
| 6 | **OscB Pitch** | `⌊48x⌋ − 24` semitones | 34–38 | Delay Fdbk / Mod / Lo-Cut / Hi-Cut / Wet | % |
| 7 | OscB Detune | `50x` cents | 39 | Chorus ON/OFF | |
| 8 | **Osc Balance** | % (A↔B) | 40 | Chorus Rate | `5x` Hz (+ min.) |
| 9 | Noise Balance | % | 41, 42 | Chorus Depth / Wet | % |
| 10 | **EG Amount** | `200x − 100` % | 43 | MW Amount | ±100 % |
| 11, 12, 14 | **EG Attack / Decay / Release** | `5 + 49995·x⁴` ms | 44 | **Monophonic** | on/off |
| 13 | EG Sustain | % | 45 | Octaver | on/off |
| 15 | Gain | `30x − 20` dB | 46 | **Glide Time** | % |
| 16 | Fine Tune | `200x − 100` cents | 47, 48 | Gate Mix / Smooth | % |
| 17 | Transpose | `⌊48x⌋ − 24` semitones | 49–64 | Gate Step 1…16 | on/off |
| 18 | **Flt. Cutoff** | % (no unit in the text; **[HYP]** `20·1000^x` Hz) | 65 | **EG Dest** | `⌊4x⌋`: Cutoff / Frq A / Frq AB / PW A / PW AB |
| 19 | Flt. Reso | % | 66 | **Filter Mode** | `⌊2x⌋`: LP / BP / HP |
| 20, 21 | Flt. KbdTrk / VelTrk | % | 67 | LFO Dest | `⌊5x⌋`: Cutoff / Frq A / Frq AB / PW A / PW AB / Pan |
| 22 | **Amp. Attack** | `1 + 4999·x⁴` ms | 68 | LFO Shape | `⌊4x⌋`: Saw / Pulse / Triangle / Sine / Random |
| 23 | **Amp. Decay** | `1 + 19999·x⁴` ms | 69, 70 | ARP Mode / Range | |
| 24 | Amp. Sustain | % | 71 | ModWheel Dest | |
| 25 | **Amp. Release** | `15 + 9985·x⁴` ms | 72, 73 | Glide Held / Glide Time Static | |

74…155: arpeggiator/sequence state (−1 = empty step) — these are not named parameters **[DATA]**.
Checked in the default preset: pitch/transpose/fine = 0.5 (center), gate 1 0 1 0 1 1 1 0…, cutoff 1, sustain 1
**[DATA]**.

### 12.2 Toxic Biohazard **[DECOMP `FUN_18003b6e0` (names), `FUN_180041b00` (state) + DATA]**

State (10,050 B) = `"SSLF"` + `u32 0x273A` + tag `"TBi2"` + 64 B header (16/32-bit integers:
waveform/UI selections) + **8,192 B of drawn waveforms** (8 × 2 × 2 × 64 f32) + **340 × f32 of
parameters starting at blob offset 0x204C** (the first 294 are the named parameters, in index
order; 294…339 are internal state) + 294 B (one byte per parameter, 0xFF in the data: MIDI CC mapping
**[HYP]**) + 128 B preset name. Names by index:

| Indices | Parameters |
|---|---|
| 0–10 | Gain, Distortion, Transpose, **Master Atk / Dec / Sus / Rel**, Vel. Curve, Unison Dtn, Unison Pan, Glide Time |
| 11–14 | EGF to Pitch, Lfo2 to Pitch, Lfo1 Speed, Lfo2 Speed |
| 15–24 | **Flt Cutoff**, Flt Res, Flt KeyTrk, Flt VelTrk, Flt EG Atk/Dec/Sus/Rel, Flt Eg Amnt, Flt Lfo1 Amnt |
| 25–30 | O1…O6 Enable |
| 31–36 / 37–42 | O1…O6 **Coarse** / **Fine** (0.5 = center) |
| 43–48 / 49–54 / 55–60 | O1…O6 Frq Shift / Free Run / Phase |
| 61–66 / 67–72 | O1…O6 VelSens / Init |
| 73–78 / 79–84 / 85–90 / 91–96 | O1…O6 **Atk / Dec / Sus / Rel** |
| 97–102 / 103–108 | O1…O6 Pan / **Mix** (operator output level) |
| 109–144 | **FM matrix** "Oi Fm j" (j = 1…6, i = 1…6; index `109 + 6·(j−1) + (i−1)`) |
| 145–156 | LFO1 → Osc1…6, LFO2 → Osc1…6 |
| 157–162 | Seq Play, Seq Swing, Seq P-P, Seq Rnd, Seq Dual, Seq Steps |
| 163–226 | Seq Ptrn 0…63 |
| 227–235 | EQ On, EQ Band 1…8 |
| 236–293 | FX: Fx1/Fx2 Enable, Flanger (delay, fdbk, depth, rate, mix), Chorus, Reverb (decay, damp, hi-cut, mix), Delay (time, fdbk, rate, depth, blur, mix), LoFi (bits, smp, flt), Phaser (frq, rate, dpth, fdbk, mix) — two of each |

Checked: Enable 1/0 on the operators in use, Coarse/Fine 0.5, matrix almost all 0, EQ bands 0.5 **[DATA]**. The
plugin has no text with units (names only); the scales remain **[HYP]**: Coarse `round((x−0.5)·48)`
semitones/ratio, times `~10·x³` s.

### 12.3 BassDrum **[DECOMP engine: `FUN_18002bce0` (state), `FUN_18002a380` (names), `FUN_18002ace0` (text)]**

State = `"SSLF"` + `u32 0x560` + tag (`"1kiK"` in the data; the current writer writes `"2kiK"`) + 256 B preset name
+ 1024 B path of the noise sample + **23 × f32 = parameters 0…22 in index order** (blob 0x50C).
**The order from round 13 was wrong**; the correct one:

| i | Name | Conversion | i | Name | Conversion |
|---|---|---|---|---|---|
| 0 | **Output Level** | `40(x − 0.5)` dB | 12 | Var. Phase | % |
| 1 | Sampling Ratio | `2^(4(x − 0.5))` (0.25…4) | 13 | Click Cut | % |
| 2 | **Duration** | % | 14 | Click Freq | % |
| 3 | Butt | % | 15 | **Click Amount** | % |
| 4 | **Drive** | % | 16 | Noise A | % |
| 5 | Attack | % | 17 | Noise D | % |
| 6 | **Base** (final frequency) | `2^(4x + 4)` Hz (16…256 Hz) | 18 | Noise S | % |
| 7 | **Peak** (initial frequency) | % **[HYP: same relative scale]** | 19 | Noise Start | `200(x − 0.5)` % |
| 8 | **Slide Time** | % | 20 | Noise Pitch | `48(x − 0.5)` semitones |
| 9 | Var. Base | `200(x − 0.5)` % | 21 | **Noise Mix** | % |
| 10 | Var. Peak | `200(x − 0.5)` % | 22 | Noise Filter | % |
| 11 | Var. Slide | `200(x − 0.5)` % | | | |

Preset "Bassdrum 01": output +0.6 dB, *base* `2^(4·0.305+4)` = 37 Hz, *peak* 0.374, *slide* 0.27, *duration*
0.625, click 1.0, noise mix 0.66 **[DATA]**.

---

## Recommendations for LYNX

1. **Pitch Shifter → `EffectKind::pitchShift` (already exists) (priority 1, 14 slots).** In `FlEffects.cpp`:
   read §2.1; `p = i32(4)` cents → `st = trunc(p/100)`, `ct = p − 100·st`;
   `params[0] = 0.5 + st/48`, `params[1] = 0.5 + ct/200`, `params[2] = i32(16)/2400` (mix),
   `params[3] = clamp01((D − 20)/100)` with `D = 20 + 130·(i32(20) − 2000)/13000` ms in granular mode and
   `D = 60 ms` in FFT mode (byte 61 = 0). **Keep the slot even with pitch 0** (5 demos — parameter 0 is usually
   automated). `effectParamFromFl("Pitch Shifter", 0)` → `params[0]` with `v ↦ 0.5 + round(48v − 24)/48`
   and the remainder in `params[1]` (FL normalizes −1200…1200 into 0…1). Formant ≠ 0 and *delay mode* ≠ 0 → log
   ("formant ignored"/"Pitch Shifter delay ignored"); if *delay mode* ≠ 0, alternative: insert a `delay`
   after it, time `1 + 999x` ms or `v/48` steps.
2. **Envelope Controller → live modulation source (priority 1; the 18 channel links in the demos, `REA_rodada14.md` §7.1).**
   - New parser (`FlGenerators.cpp` or its own file) following §10.1, reusing `readEnvelope` from
     `FlSytrus.cpp`; produce `EnvControllerState { articulators[8] { bipolar, base, envLevel, lfoLevel, knobs
     ADSR, lfoSpeed, env, lfo, maps, noteLo, noteHi }, modX, modY, continuous }`.
   - In `Flp.cpp`/`FlpImport.cpp`, accept a `ControllerLink` whose source is a **channel** (`source & 0xFFFF ≥ 0x8000`,
     `channel = source >> 16`, `k = (source & 0xFFFF) − 0x8000`) when the channel is an Envelope Controller.
   - Recommended implementation (faithful and cheap): extend `Automation::LiveSource` with
     `kind = channelEnvelope`, storing the channel index and `k`; in the engine, on each block, evaluate articulator
     k from the **channel's last note** within the range (`t` since note-on; release at note-off),
     with §10.3 (envelope in seconds, or in beats if the TEMPO bit is set), clip to 0…1 and apply the link curve.
     The controller channel plays no sound (instrument "none"), but its notes need to reach the evaluator.
   - Alternative without a new engine: "stamp" the envelope as automation points on the target at each note of the channel
     in the playlist (sample every 1/64 beat; hold the final value until the next note). Covers the
     *sidechain* case of "Future House" (art. 0–3 → volumes) identically.
   - Before the first note, use the **final value of the envelope** (for the *ducking* = 1.0) — FL keeps the last
     value sent.
3. **Frequency Shifter → `EffectKind::freqShift` (already exists).** `Hz = ±0.1·2^(K·|2x−1|)`, K per range (§3.2);
   in `params[0]`: `s = sign(Hz)·sqrt(min(|Hz|, 2000)/2000)`, `v = 0.5 + s/2` (inverts the current `fshift::hz`,
   ±2000 Hz). Mix: LYNX does a *crossfade*; use `wet/(wet + dry)` with `wet = min(1, 2x)`,
   `dry = min(1, 2 − 2x)` (x = 0 → slot disabled). Type 2 (ring) and *Tempo* range → log; feedback ≠ 0.5 → log.
4. **Emphasis → `limiter`** (4 Masters): `params[1] = p6/10000`, `params[0] = (20·log10(curve11(p9/10000)) +
   12)/12`. Bypass (p0 = 1) → slot disabled.
5. **Low Lifter → `parametricEq`** as in §5.3 (low cut + low shelf at the *crossover* + output), `mix` = p1.
6. **Fruity Send → `utility`** with gain `dry/256` (and a *send* if destination ≥ 0). **Fruity Squeeze →
   `distortion` + `filter`** (§6). **Transporter → `utility`** with the *dry level*. **Vintage Phaser → `chorus`**
   (§8). All with a message in the import report.
7. **X-Y Controller / Control Surface → constant value.** For effect-controller links that are not Peak
   Controller: X-Y → output 0 = `p0/65536` mapped by min/max (p1, p2), output 1 = `p4/65536` (p5, p6);
   Control Surface → find the `0x836` whose 3rd field = k and use the `f32` value. Apply once to the target (via the
   link curve) as the parameter's initial value, without automation.
8. **PoiZone → `SynthSettings`** (now with confirmed order): osc1 = OscA (`p0 = 0` → saw, otherwise square with
   PW `p1`), osc2 = OscB with `semitones = ⌊48·p6⌋ − 24`, `detune = 50·p7` ct, levels by the balance `p8`
   (`osc1 = 1 − p8`, `osc2 = p8` **[HYP on the direction]**), osc3 = noise with level `p9`; global transposition
   `⌊48·p17⌋ − 24` + fine `200·p16 − 100` ct; amplitude ADSR p22–p25 (ms ÷ 1000); cutoff `20·1000^p18` Hz
   **[HYP]**, resonance p19, `filterType` from p66 (LP 0, BP 1, HP 2); if `p65 = 0` (Cutoff destination),
   `filterEnv` = ADSR p11–p14 with `amount = (2·p10 − 1)·4` octaves **[HYP on the scale]**; LFO: destination p67,
   shape p68, rate `30·p28⁵` Hz, amount `2·p26 − 1`; `mono = p44 ≥ 0.5`, `port = p46 > 0`. The 16-step
   *gate* (p47 mix > 0) can become a `volumeGate` on the channel, if LYNX has channel FX.
9. **BassDrum → same scheme as Fruit Kick (round 10 §4), now with the correct order**: sine with pitch
   going from *peak* to *base* (`2^(4·p6+4)` Hz) over `slide time` (p8), *decay* from `duration` (p2), click from
   `click amount` (p15), noise from `noise mix` (p21) with ADS p16–p18, *drive* p4, gain `40(p0 − 0.5)` dB.
   (LYNX does not yet decode BassDrum; do not use the hypothetical order from round 13.)
10. **Toxic Biohazard → 2-operator FM** in the LYNX synth: choose the "carrier" operator with the highest `Oi Mix`
    (103–108) and the modulator with the highest `Fm` into it in the matrix (109–144); osc1 = carrier sine
    (`Coarse` p31+i → ratio), osc3 = modulator sine, `fm = matrix value · 4` **[HYP]**, `fmDecay` from the
    modulator's *decay* (p79+j); master ADSR p3–p6; cutoff p15. Low accuracy; record it in the log.

## Evidence index

| Function / data | What it does | Marker |
|---|---|---|
| Pitch Shifter `FUN_0072c630` (VMT+3) | Version 7, 13 params (0x34 B), correction, tempo sync, engine, detector, UI | DECOMP |
| Pitch Shifter `FUN_0072d5b0`, constants 24/12/130/20/4/100/999 | Parameter text (semitones, ms, %, modes) | DECOMP |
| Pitch Shifter field table of `TSynthEditorForm` (`fields.py`) | 0x6F8 PluginTypeSelect, 0x718 CorrectionWheel, 0x810 TempoSyncCheckBox, 0x818 PitchDetectorCombo | STR |
| Pitcher `FUN_18001cab0`, `FUN_18001d020`, `FUN_18001d890` | setParam (speed, fine, voice range 220…25 Hz, MIDI port), defaults, scale mask by tonic/type | DECOMP |
| Frequency Shifter `FUN_006dd080` | Version 4, 10 params, flags, sidechain track | DECOMP |
| Frequency Shifter engine `dispatch`, `FUN_1800020a0`, `FUN_1800023c0`, `FUN_180001fd0` | Names, text (dry/wet mix, types, ranges), frequency `0.1·2^(K·|s|)` with K = 17.61 / 10.97 | DECOMP |
| Emphasis `FUN_00706090`, `FUN_00706ba0`, `FUN_007043f0`, `FUN_00705110` | Layout v10, input `24x` dB, ceiling `curve11` | DECOMP |
| Low Lifter `FUN_006c6d50`, `FUN_006c7490`, `FUN_006c54c0` | Layout v6, `lerp` scales (dB, Hz) | DECOMP |
| Squeeze `FUN_006d30c0`, `FUN_006d3a50` | 12 params, steps `32768^(1−x)`, filter types, relation | DECOMP |
| Transporter `FUN_00713760`, `FUN_00713f40`, `FUN_007125f0` | 22 params + tracks, `curve11` levels | DECOMP |
| Vintage Phaser `FUN_006ea8e0`, `FUN_006eb7f0`, `FUN_006eb680` | 36 params, frequency `1.25·2^(14x)` | DECOMP |
| Fruity Send `FUN_006bd0f0`, `FUN_006bd930` | 3 params + track, volume `curve11(v/256)`, dry `v/256` | DECOMP |
| Envelope Controller `FUN_008ad310` | Complete layout (v6: 90 params; v5: 44; flags; 8 articulators + note range) | DECOMP |
| Envelope Controller `FUN_008ae740` | Parameter `11k + s` → base/env/LFO (`/256`), A/D/S/R, LFO; 88/89 = Mod X/Y `/65536` | DECOMP |
| Envelope Controller `FUN_008af660`, `FUN_008afb60`, `FUN_008afa80` | Triggering per note in range; monophonic articulator (last note) | DECOMP |
| Envelope Controller `FUN_008afec0` (NewTick), `FUN_008b11c0`, `FUN_008b0f90`, `FUN_007e0170` | Sum/product per mode, order of the parts, neutral 0.5 in bipolar, clip 0…1, sending of output k | DECOMP |
| 30 Envelope Controller blobs (`envctl.py`) | Complete parse with no leftover (v6 and v5) | DATA |
| X-Y Controller `FUN_006c5d20`, `FUN_006c6650` | Layout v2; params 0–7 = 2 axes × (level, min, max, tension) | DECOMP |
| Control Surface `FUN_00854f70`, `FUN_00855120`, `FUN_008534b0` | Chunks per control; `0x836` = (value, attribute, published output) | DECOMP |
| PoiZone engine `FUN_180031020`, `FUN_180030920`, `FUN_18002f810`, `FUN_18002e310`, `FUN_18002e340` | `Ver4` + 156 f32 in order; names 0…73; scales `x⁴`, `x⁵`, selectors | DECOMP |
| Toxic `FUN_18003b6e0`, `FUN_180041b00`, vtables `ToxicSynth`/`ToxicInstance` (`Sym`) | 294 names; layout (header, waves, 340 f32, CC, name) | DECOMP |
| BassDrum engine `FUN_18002bce0`, `FUN_18002a380`, `FUN_18002ace0` | 23 f32 in order at 0x504 of the chunk; names; scales | DECOMP |

### Open questions

- Pitcher: confirm which of buttons 22/25/26/27 are *Harmonize*, *MIDI* and *Formant*; exact unit of *Fine
  Tune* and *Note A Freq*; the harmony voices engine.
- Pitch Shifter: exact delay time with *tempo sync* (the text says `steps:48ths` — confirm whether the step is the
  sixteenth note); effect of the absolute *Pitch mode*.
- Frequency Shifter: precise meaning of *L/R Shape* and the period in the *Tempo* range.
- Low Lifter / Emphasis: the processing itself (only the scales were read); the order of the 3 final bytes of
  Emphasis v10.
- Envelope Controller: exact offset of each part in unipolar mode (the LFO neutral in the product); whether the
  `NewTick` *tick* changes with the project PPQ (affects only resolution, not timings); official name of bit 2
  of the flags.
- Control Surface: complete table of control types (`0x834`) and the 2nd field of `0x836`.
- X-Y: confirm the order of the outputs (0 = X) with a real link in the demos (the only demo has no link).
- PoiZone: scale of the *cutoff* (the text is just %) and of the *EG Amount* in octaves; direction of *Osc Balance*.
- Toxic Biohazard: scales (the plugin displays no units); role of the 46 internal floats 294…339 and of the 64 B
  header (waveforms selected per operator).
- BassDrum: scale of *Peak* (text in %, possibly relative to *Base*), *Duration* in seconds.
