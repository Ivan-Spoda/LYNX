# Behavioural analysis of FL Studio 2026: round 19 (REA) — what round 18 left open (Effector types 8…11, Distort and Reverb; Harmor; Kepler; Toxic Biohazard; Sakura; Pitcher/Vocoder; Love Philter)

> **Nature of this document.** Continuation of `REA_rodada18.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode was copied: what is here are formulas and behaviours described in our
> own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted
> content was touched (no FLEX package was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where
> there was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> (e.g. "this routine is `exp` because it is always called right after multiplying by ln 2") are made explicit in the text.

- **Targets** (all already checked by hash in previous rounds, except Fruity Vocoder):

  | Plugin | DLL (SHA-256) | Language | New key functions |
  |---|---|---|---|
  | Effector | `engine_x64.dll` `4bfdc0f6…7dc2071a` (= round 18) | C++ engine | Trancer `FUN_180019f90`, Stutter `FUN_18001ae50`, Vowel `FUN_18001b140`, Ring `FUN_18001b5a0`, biquad `FUN_180014520`/`FUN_180014680`, Distort `FUN_180019860`, Reverb `FUN_18001a130` + `fxReverb` slot 43 `FUN_18001e9f0`, *buffer* creation `FUN_180018700`, *reset* `FUN_180018a50`, synced phase `FUN_180016d40` |
  | Harmor | `23673ad8…499d5aba` (= round 11/18) | Delphi | `TMEnvelope` (VMT `0x79a0d8`) `FUN_007a0c60`/`FUN_0079fef0`, centre `FUN_0079a7c0`/`FUN_0079a7f0`, `TArticulator` method 0 `FUN_007a4880`, flag set `FUN_00896fe0` (`0x89732c`), *note-off* `FUN_00885390`, voice death `FUN_008855e0`/`FUN_00883cd0`, *declick* `FUN_008635c0`/`FUN_008781f0`, resonance in `FUN_00886eb0`/`FUN_00882af0`/`FUN_00882820` |
  | Kepler (engine) | `baf37a1d…2afc63ab` (= round 13/17/18) | C++ | constructor `FUN_1800166d0`, sources `FUN_1800225f0`, LFO `FUN_18001b940`, table `FUN_180013d30`, *setter* `FUN_180019a20`, restart `FUN_180018fe0`, chorus `FUN_180001a60`/`FUN_180001770` |
  | Toxic Biohazard | `5c326637…f136cb46` (= round 15/17/18) | C++ | *setter* `FUN_180040080`, smoothing `FUN_180040ff0`/`FUN_1800406d0`, targets `FUN_1800395b0`, render `FUN_180038d00`, voice LFO1 `FUN_180039880` |
  | Sakura (engine) | `e632487c…ae0f1bab` (= round 17/18) | C++ | `FUN_18002f4c0`, `FUN_18002da50`, render `FUN_180031960` |
  | Pitcher | `965db573…a73d` (= round 14/15) | C++ | `setParam` `FUN_18001cab0`, processing `FUN_18001e030`, target `FUN_18001ea80`, harmony `FUN_18001f800`, mask `FUN_18001d9c0`, render `FUN_18001ed60`/`FUN_18001f2b0` |
  | Fruity Vocoder | `bfc3bec3…a63961b6` | Delphi | DFM + data only |
  | Fruity Love Philter | `b9f11d63…ba2ff547` (= round 16/17/18) | Delphi | coefficients `FUN_00859100`, stages `FUN_0085d5a0`, RBJ biquad `FUN_006249d0`, Mango `FUN_006261c0`/`FUN_00625ea0`, render `FUN_0085aa10` |

- **Tools:** Ghidra 12.1.4 *headless* (projects from previous rounds opened read-only:
  `/tmp/r18gh_EffEng`, `/tmp/r11gh_copy`, `/tmp/r13gh_kepeng_x64.dll`, `/tmp/r15gh_Toxic.dll`,
  `/tmp/r17gh_Sakura_eng`, `/tmp/r14gh_Pitcher_x64.dll`, `/tmp/r17gh_LP`); scripts in `tmp/r18/gh/scripts/` +
  `tmp/r19/gh/scripts/ScalarIns.java`; `tmp/r18/k.py` for constants; `tmp/r19/scr/hmenv.py` and
  `tmp/r19/py/pt2.py`/`vc.py` for the data; DFM with `tmp/r15/dfmc.py`. Official manual read via web.
- **Date:** 2026-10-13.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated as `…/plugins/` below). The manual quotes came from summarised web reading; the quoted excerpts
are short and must be rechecked on the page before becoming interface text.

---

## 1. Effector — types 8…11, Distort curve and Reverb dry **[DECOMP + DOC]**

**What LYNX assumes** (`FlEffects.cpp` `effector`): Trancer/Stutter/Vowel/Ring become internal effects with
"approximate" X/Y; Distort = generic saturation; Reverb with *crossfade* mix.

### 1.0 Common structure (complement to round 18) **[DECOMP]**

- The processor (`FUN_18001b970` → `FUN_18001ba60`) calls **the 12 units in series, every block**, on the same
  *buffer*; each unit has the flag `+0x28` = "(index ≠ chosen type) OR bypass" (`FUN_18001bb30`). Only the unit
  of the chosen type processes; the others pass the signal intact (Vowel/Ring/Reverb still do an in/out ramp
  with coefficient 0.01/sample on the field `+0x2c`; Stutter, Vowel, Ring and Reverb reset themselves,
  `FUN_180018a50`, when they finish deactivating).
- Unit fields read here: `+0x8` sample rate; `+0xC` = X and `+0x10` = Y (already clamped to 0…1 by the
  *setters* `FUN_180018bb0`/`FUN_180018bd0`); `+0x1C`/`+0x20` = Mod Amount X/Y; `+0x14` = Mod Rate value
  (× `(1 − TempoReduce)²`, `FUN_18001ba60`); `+0x38` = phase increment per sample = **`1/(T·fs)`** with
  **`T = value·60/BPM` s** (`FUN_180018c20`); `+0xADC/+0xAE0` = Wet Mix and its smoothed version (coef. 0.01).
- The LFO phase `+0x30` is resynced with the song position (`FUN_180016d40`: phase = frac(position/value),
  called by `FUN_180017010` and by the Mod Rate `setParameter`) **[DECOMP; that the argument is the position in
  beats is inference from usage]**.

### 1.1 Trancer (type 8; manual "Trans") — `FUN_180019f90`

- **Does not use the LFO or Mod Amount**: reads raw X and Y. Uses the phase `+0x30` (the same one synced to the song) with period
  `T` from Mod Rate.
- Per period (phase φ from 0 to 1):
  - φ < 0.01: `e = 100·φ` (linear rise over 1 % of the period);
  - φ ≥ 0.01: `u = (φ − 0.01)/0.99`, **`e = 1 − min(1, u⁴·K)`** with **`K = 50·(1 − X)² + 1`**.
  - `e` goes through a pole with coefficient 0.01 per sample (≈ 100 samples); the gain applied to both channels is
    **`g = 1 − e·(1 − Y²)`**.
- Reading: every period the gain drops to **`Y²`** (in ~1 % of the period) and returns to 1 following `u⁴`; the full
  return ends at `φ = 0.01 + 0.99·K^(−1/4)`: **X = 0 → 38 % of the period, X = 0.5 → 53 %, X = 1 → 100 %**.
  Y = 0 → total silence in the trough; Y = 1 → no effect.
- **Wet Mix does not enter the output** (the smoothed value is updated but not used): the output is `input × g`.
- Manual: "X - Release time. Y - Drop level" **[DOC `…/plugins/Effector.htm`]** — consistent.

### 1.2 Stutter (type 9; manual "Grain") — `FUN_18001ae50`

- Creates (`FUN_180018700`) two *buffers* of **1.5 s** (`1.5·fs` samples, L and R).
- **Recording:** since the unit's last *reset*, input samples are written in sequence until the
  *buffer* is full (1.5 s); after that recording stops (the counter only returns to 0 on the *reset* `FUN_180018a50`, called on
  rate change/initialisation `FUN_18001b910`←`FUN_1800179a0`, and when the unit is deactivated).
- **Playback:** a position advances 1 sample per sample and **returns to 0 every period `T` of Mod Rate** (its own
  phase `+0xAE4`, not synced to the song) or on reaching 1.5 s. If the position has already been recorded, the output is the
  *buffer* content at that position; in the first **192 samples** of each repetition there is a linear *crossfade*
  with the continuation of the previous segment. If it has not been recorded yet (first period after the *reset*), the
  input passes through.
- Resulting effect (direct inference from the flow above): **repeats, every period `T`, the audio excerpt that starts
  at the instant of *reset*/activation** (up to 1.5 s).
- There is a reverse-read branch (flag `+0xB10`), but it depends on the unit's byte `+0`, which **is only ever written with 0**
  in the code read (constructor/*reset*) → reversal **inactive in practice / not confirmed**.
- **X and Y are not read** by the unit; nor is the LFO. Mix: **`(1 − w)·dry + w·repeat`** (w = smoothed Wet Mix).
- Manual: "GRAIN … X & Y - Not used" **[DOC `…/plugins/Effector.htm`]** — consistent.

### 1.3 Vowel (type 10; manual "Vox") — `FUN_18001b140`

- `X' = clamp(X + LFO·ModX, 0, 1)`, `Y' = clamp(Y + LFO·ModY, 0, 1)` (LFO evaluated once per block).
- **Bank of 8 band-pass biquads per channel, in parallel** (`FUN_180014520` coefficients / `FUN_180014680`
  processing), all with **Q = 12**, in the "peak gain = Q" form (b0 = sin ω/2, normalised by 1 + α,
  α = sin ω/(2Q); the sine/cosine assignment is inferred from the shape of the formulas).
- **Three frequency sets** (constants `0x1800586a0/b0/c0`; the last 4 values are shared,
  `0x1800586d0`):

  | Set | Bands 1–4 (Hz) | Bands 5–8 (Hz) |
  |---|---|---|
  | A | 250, 700, 2500, 3500 | 4200, 8500, 10000, 12000 |
  | B | 700, 1250, 2500, 3500 | same |
  | C | 250, 2500, 2700, 3500 | same |

- **X chooses the vowel:** `p = 3·X'`, `n = ⌊p⌋`, `f = p − n`; frequencies = linear interpolation
  **A→B (n = 0), B→C (n = 1), C→A (n = 2), A (X' = 1)** — closed cycle.
- **Y scales all frequencies** ("throat size"): factor **`2^(2·0.93·Y' − 1)`** (Y = 0 → ×0.5; 0.5 → ×0.953;
  1 → ×1.815).
- Sum of the 8 bands × `(1 − 0.125·n)` (n = 0, 1, 2 → 1; 0.875; 0.75).
- Mix: **`(1 − w)·dry + w·wet`**, with `w` = smoothed Wet Mix × activation ramp.
- Numerical note (derived from the formulas): with peak gain 12 per band, the wet can end up well above the input
  level near the formants; no additional normalisation was found in the unit.
- Manual: "X - Formant filter, vowel sound (A,E,I,O,U). Y - Throat size" **[DOC]**. **Divergence:** the code has
  **3** formant sets (not 5 vowels). The vowel names of each set **are not in the code**
  (not confirmed).

### 1.4 Ring (type 11) — `FUN_18001b5a0`

- `X' = clamp(X + LFO·ModX, 0, 1)`; **carrier frequency `f = 2^(8.7·X' + 5)` Hz** (X = 0 → 32 Hz;
  0.5 → 653 Hz; 1 → 13,308 Hz), computed once per block.
- **Sinusoidal** carrier (accumulated phase `+0x2D8` with increment `f/fs`; sine of `2π·phase`, `FUN_180040dc0`).
- Output: **`(1 − w)·x + w·x·sin(2π·phase)`** (same `w` as Vowel). **Y is not read.**
- Manual: "X - Modulator frequency Y - Not used" **[DOC]** — consistent.

### 1.5 Distort — exact saturation curve (`FUN_180019860`)

- `d = clamp(0.95·X + 0.05, 0, 1)`, **`k = 2048·d²`** (X = 0 → 5.12; 1 → 2048). X and Y are read raw (no LFO).
- Pre-filter: single pole `lp ← lp + c·(x − lp)` with `c = (0.9·Y + 0.1)⁴`.
- **Cascade of 4 rational stages** applied to `lp` (s₀ = lp):
  1. `s₁ = s₀ / (1 + k·|s₀|)`
  2. `s₂ = s₁ / (1 + k·s₁²)`
  3. `s₃ = s₂ / (1 + k·|s₂|³)`
  4. `s₄ = s₃ / (1 + k·s₃⁴)`
- **Compensation gain `G = 0.5 / F(0.5)`**, where `F` is the cascade above (computed per block), so that an
  input of 0.5 comes out as 0.5.
- Wet = **`G·F(lp) + (x − lp)`**; output = `(1 − w)·x + w·wet`.
- Numerical consequence (computed from the formulas): the low part saturates around **±0.5**; X = 0 → G ≈
  3.97 and `G·F` = 0.157 / 0.256 / 0.56 for x = 0.05 / 0.1 / 1; X = 0.5 → G ≈ 284 and `G·F` ≈ 0.485 already at x = 0.05
  (almost a ±0.5 square wave).

### 1.6 Reverb — the dry is preserved (`FUN_18001a130` + `fxReverb` slot 43 `FUN_18001e9f0`)

- The unit copies the input to temporary *buffers* multiplied by **`w`** (smoothed Wet Mix × activation
  ramp) and calls the `fxReverb` processing (VMT `0x180057fc0`, slot 43 = `+0x158`) with **input = scaled
  copies and output = the original *buffers***.
- The processing **adds** into the output (`out += reverb_sample`), where the reverb sample includes
  `input·DryGain` (field `+0x274`, set to 0 by the unit) + tail.
- Hence **the original dry signal stays at gain 1** and the reverb is added: **`out = x + Reverb(w·x)`**.
  Confirms the round 18 question (dry preserved; Wet Mix controls only the send to the reverb).

---

## 2. Harmor — E range, resonance and level after *note-off*

**What LYNX assumes** (`FlSytrus.cpp` `synthFromHarmor`): filter envelope centred on level 0.5 with ±1000 =
±7.9 octaves; resonance not modelled with the FL law; volume envelope off = gate `A 2 ms / S 1 / R 0.1 s`.

### 2.1 Range of the envelope's E level in the per-voice cutoff **[DECOMP + DATA]**

- **Envelope value.** The class is `TMEnvelope` (VMT `0x79a0d8`). Method 11 (`FUN_007a0c60`) interpolates between
  the points. The value of each point (`FUN_0079fef0`) is the point's `y` (+ tension/modulation offset)
  **minus the envelope's field `+0x18`** (*double*). With per-point modulation, the value is clamped to
  `[+0x70, +0x78]`, which the constructor (`FUN_0079a650`) sets to **[0; 1]**. The result goes to `+0x34` of the
  voice's articulator record — the same field that `FUN_00886eb0` reads as E.
- **`+0x18` is the "centre".** Method 0 of `TArticulator` (`FUN_007a4880`) writes the combination mode (`+8`) with
  a *flag* and calls `FUN_0079a7c0(envelope, flag)`, which writes **`+0x18 = flag × 0.5`** (constant `0x79a7e8`,
  via `FUN_0079a7f0`).
- **Who receives the *flag*** (`FUN_00896fe0`, bit set `0x89732c`): bits {0, 2, **4, 5**, 12–15, 17, 18,
  23, 24, 27–30, 33, 36, 38–42}. Hence articulators **4/5 ("Filter 1/2 frequency") are in mode 1 (additive)
  with centre 0.5**; 1 (Volume) and 8/9 (resonance) are in mode 0 (multiplicative) with centre 0.
- **Data:** in 682 Harmor parts from the demos, the `y` of the envelope points of articulators 1, 4, 5 and 8 are
  always between 0.0 and 1.0 **[DATA]**.
- **Conclusion: in the cutoff, E = y − 0.5 ∈ [−0.5; +0.5]** (centred). The amount term from round 18 becomes
  `2·(hi − lo)·(p56/1000)·(y − 0.5)·m` = **±(hi − lo)·amt ≈ ±7.92 oct·amt** → **the LYNX assumption (centre 0.5,
  ±1000 = ±7.9 oct) is confirmed for this term** (corrects the "15.8 oct/unit" reading from round 18, which
  did not account for the centre).
- **Extra term of the articulator output.** At voice start the envelope depth in the record (`+0x118`) is
  set to 1 (modes 0/1) or 2 (mode 2) (`FUN_00897c70`), and the evaluator (`FUN_00897d90`, mode 1) adds `E·1` to the
  output of articulator 4/5. Since round 18 read the articulator output with factor `2·(hi − lo)`, **inference
  from the code:** with the envelope on, the cutoff also moves by `2·(hi − lo)·E` (±7.92 oct) even with
  p56 = 0. The factor `2·(hi − lo)` of this term was not reread in this round (**not confirmed** again).

### 2.2 Resonance p57/p69 **[DECOMP + DFM]**

- Names (DFM): 57/69 "Filter 1/2 resonance / noise amount"; 58/70 oct/Hz scale; 59/71 "resonance width / noise
  length"; 60/72 "resonance offset" (±2400); 61/73 "adaptive width / free noise". Articulators: 8/9 "res
  amount", 10/11 "res width", 12/13 "res offset".
- `ProcessParam` **has no case** for 57/69; the value is read in the voice render (`FUN_00886eb0`):
  **`r = (p57/1000)·A₈`** (A₈ = output of articulator 8/9, base 1, multiplicative); it only acts if `r > 0` and the filter
  is on.
- **Normal path** (filter integer `+0x430` ≠ 0x6D): the "Resonance shape 1/2" curve (table `+0x428`,
  prepared by `FUN_008744c0` according to p58/p61) is sampled per harmonic at the position
  `c + (x − L − p60/1200 − 8·A₁₂)·w_r` (x = log2 of the harmonic's frequency; L = voice cutoff in log2 Hz;
  **p60 in cents**; additive A₁₂ = **8 octaves per unit**); width `w_r = k₀·2^(1 − 0.004·p59·A₁₀)`
  (`k₀` = field `+0x49C`, **not identified**; with p61 on, `k₀` is divided by the voice's `+0x1B8`; in p58's
  "Hz" scale the offset becomes a factor `2^(p60/1200 + 8·A₁₂)` on the frequency).
- **Gain:** the sampled values `s` are multiplied by **`r·ln 2`** (`FUN_00882af0`) and go through the routine
  of pointer `+0x1150`; the result multiplies the harmonic amplitudes (`FUN_00882820`). **Inference:**
  `+0x1150` is a vector `exp` (always called after ×ln 2; the pointer is assigned in `FUN_00878f60` and was not
  checked) → resonance gain **`2^(r·s(x))`**.
- "Noise" path (`+0x430 = 0x6D`): pseudo-random generator with rate tied to r — **not decoded**. Peak in
  dB for r = 1 (depends on the default "Resonance shape" curve, not tabulated) — **not confirmed**.

### 2.3 Level after *note-off* with the volume envelope off **[DECOMP]**

- **Note-off** (`FUN_00885390`): voice state (`+0x214`) = 6; active envelopes/LFOs enter *release*
  (`FUN_007a09f0`); the voice timestamp (`+0x40`) is shifted by −2⁶³ (becomes "the oldest").
- **Render** (`FUN_0088ab10`): the voice only ends by itself (`FUN_008856b0` → state 9) if the volume envelope
  is **on and finished**. When off, the volume articulator does not change → **the level stays constant after the
  *note-off*; there is no ramp and no implicit *release*.**
- **The voice only ends if it is killed** (`FUN_008855e0`, state 9), called on note trigger (`FUN_00883cd0`) in
  two cases: **voice stealing** when polyphony is reached (`param_1[0x1c]`; released ones go first via the −2⁶³
  mark) and **retrigger of the same note** (according to the mode in `+0x10d4`).
- **In state 9 there is *declick***: the part's object `+0x660` (`FUN_008635c0`) applies an output ramp
  configured by `FUN_008781f0` → `FUN_00863450`/`FUN_00862d70` as **type 3, 10 ms** (`round(0.01·fs)`
  samples; table with π and 0.5 — cosine shape **by inference**); then the voice goes to state 10 (free).
- Voice killing by the host (Voice_Kill) — **not traced**.

---

## 3. Kepler — LFO1 amplitude and rate, chorus mix **[DECOMP + DATA]**

### 3.1 L₁ value and its amplitude

- The pointers `voz+0x11B0`/`+0x11B8` are filled in the global constructor `FUN_1800166d0` (loop over the 8 voices):
  `+0x11B0` → **global `+0x1C724`**, `+0x11B8` → global `+0x1C720` (the source table `FUN_1800225f0` gives the
  same addresses: source 0x10 and 0xF).
- In the voice (`FUN_180023ab0`) the value used in pitch (× `+0x11E8`) and in cutoff (× `+0x11E0`) is **`2·L₁`**, L₁ =
  `*(+0x1C724)`.
- The global render (`FUN_18001b940`) computes per sample **`+0x1C724 = R·G·0.5·W(φ)`**, with `W` = interpolated table of
  **2,170 points** (index `φ·2170`; phase `+0x1C6C0` with increment `+0x1C688`, wraps at 1). The table
  is built in `FUN_180013d30` from 2,170 `int16` at `0x180252ED0` and **normalised to exactly −1…+1**.
  Shape **[DATA, read in the PE]**: almost sinusoidal (fundamental 1.006; 2nd harmonic 0.031; 3rd 0.013).
- **Hence `2·L₁ = R·G·W(φ)` ∈ [−1; +1]**: the depths A(x) and A_c(x) from round 18 are the **peak** of the deviation,
  symmetric (±A octaves in pitch, ±A_c octaves in cutoff).
- Other outputs of the same LFO (matrix only; the panel's fixed path does not use them): `+0x1C6A0` triangle in
  [−0.5; 0.5]; `+0x1C698` = triangle + 0.5; `+0x1C71C`/`+0x1C714` = those × R·G.

### 3.2 Delay, *gate* and rate

- `R` (`+0x1388`) is a linear ramp (adds `+0x1390` per sample, clamped to 1). Internal parameter 0x32 (LFO1
  Delay, `FUN_180019a20`) gives `+0x1390 = 1/(D·fs)`, with D from the 11-point table at `0x180254100` (seconds —
  inference, since it divides by fs): **{0; 0.229; 0.447; 0.547; 0.833; 1.0; 2.074; 4.775; 5.612; 6.774; 6.706}**,
  interpolated at 10·x. The ramp returns to 0 on the *note-on* that brings the note counter to 1, if Trig Mode (0x33)
  is off (`FUN_180018fe0`); if "LFO1 Retrig Phase Active" ≥ 0.5, the phase goes to "Phase".
- `G` (`+0x1C708`) equals 1, or 0 under certain Trig Mode conditions (0x33 and case 0x51) — exact condition **not
  confirmed**.
- **Rate:** the round 17 law stands (`f = 21.739·2^(−T(x))` Hz, 0.125…21.7 Hz); in this round it was only
  confirmed that the phase advances per sample with that increment. The BPM Sync law (internal 0x31) **was not read**.

### 3.3 Chorus mix (`FUN_180001a60`, object at global `+0x1BBB0`, constructor `FUN_180001770`)

- **Mono input:** byte 0 of the object is 1 in the constructor (no other writer found) → **L and R (the dry
  included) are replaced by `(L + R)/2`** before the chorus.
- **Output: `out = wet·√m·g + dry·√(1 − m)`**, with `m` (`+0x208`) = **0.5** (constructor and on every mode change
  in `FUN_18001b940`), `g` (`+0x204`) = 1, smoothing 0.01/sample → **dry and wet at 0.7071 (−3 dB) each**.
- Delay feedback (object's `+0x178`) = **0** in all modes. "Chorus Noise" (0x46): filtered noise
  added to the wet with level `0.2·x³` (only if > 0); "Chorus Noise Gate" (0x47) boolean (x ≥ 0.5). Final gain
  `+0x200` = 1.
- Mode change with *crossfade* (`+0x1BDF0`); with mode Off the chorus is not called and the stereo passes intact.

---

## 4. Toxic Biohazard — filter mode and scales in Hz **[DECOMP + DATA + DOC]**

### 4.1 Mode selection

- **Parameter index 300** (internal state range 294…339; *float* 300 of the block starting at offset 0x204C):
  in the *setter* `FUN_180040080`, `int(v)` (raw value), clamped to ≤ 3, written to `+0x3B78`. Data: p300 = 1 in the
  default `.fst` and in "Saw #2"; 0 in "Bombs Away" and "Saw" **[DATA: 7 blobs]**.
- Filter output (`FUN_1800395b0` sets targets; `FUN_180038d00` renders): 4 stages
  `s_i = (x_i + x_i,ant)·a − b·s_i,ant`, input `u` = saturation of `(input − res·k·s4)`; output
  `(c0·u + c1·s3 + c2·s4)·(1 − β) + β·input`, weights tracked with coefficient 0.005/sample.

  | p300 | c0 | c1 | c2 | β | Mode |
  |---|---|---|---|---|---|
  | 0 (or other) | 0 | 0 | 0 | 1 | **Bypass** (with β ≥ 0.999999 the filter is not even computed) |
  | 1 | 0 | 0 | 1 | ≈0 (1e−7) | **LP** = s4 (4 poles) |
  | 2 | 0 | 1 | −1 | ≈0 | **BP** = s3 − s4 |
  | 3 | 1 | 0 | −1 | ≈0 | **HP** = u − s4 |

  Order consistent with the manual "Bypass/LP/BP/HP" **[DOC `…/plugins/ToxicBio_filter.htm`]**.

### 4.2 Scales (`FUN_180040080`; copy/smoothing `FUN_180040ff0`/`FUN_1800406d0`)

- 15 Cutoff → x; 16 Res → x (0…1 per voice); 17 KeyTrk → x; 18 VelTrk → x; 19/20/22 EG A/D/R →
  `FUN_18003d480(x)` (not read); 21 Sus → x²; **23 EG Amnt → `sign(2x − 1)·(2x − 1)²`** (−1…+1; 0.5 = 0);
  **24 Lfo1 Amnt → x³**.
- **Correction of round 18:** the "EGAmt·env" term of `FUN_1800395b0` is actually **`Lfo1Amnt·LFO1`** (`+0x3BE4` =
  parameter 24; `voz+0x220` = LFO1 value, written per block in `FUN_180039880`). The **filter envelope is
  applied per sample in the render**: `index = u·65535 (ramped within the block) + env·EGAmt·65535`, clipped to 0…65535.
- Envelope (`voz+0x70`) **0…1**: attack target 5.0 with coefficient `8.82/(39.44·T_A·fs)`, clamped to 1; decay to
  S with `8.82/(T_D·fs)`; release to 0 with `8.82/(T_R·fs)`.

### 4.3 In Hz (with `fc = 2^(9u + 5)`, u clamped to 0…1 → 32 Hz…16,384 Hz; 1 unit of u = 9 octaves)

- **EG:** `+9·EGAmt·env` octaves (max. ±9 oct; e.g.: x = 0.735 → EGAmt = 0.221 → +1.99 oct with env = 1).
- **Key-track:** `KT·(note − 60)·0.01·9` = **0.09·KT octaves per semitone** (KT = 1 → 1.08 oct/oct), reference at
  note 60 (that `FUN_180026a80` returns the MIDI note is inference).
- **Velocity:** `VelTrk·(vel − 1)·0.5·9` octaves (down to −4.5·VelTrk); the range of `vel` **not confirmed**.
- **LFO1:** `9·x³·LFO1` octaves; LFO1 amplitude (`FUN_180030110`) **not confirmed**.

---

## 5. Sakura — Hi-Cut in Hz **[DECOMP; Hz conversion by mathematical deduction]**

- **Topology** (render `FUN_180031960`, 4 SIMD voices; coefficients in `FUN_18002da50`): **4 cascaded 1-pole
  low-passes** with negative feedback from the saturated output: 1st stage `y1 += (r·ŷ4,ant + x − y1)·c`; others
  `yi += (y(i−1) − yi)·c`; `ŷ4 = y4/(1 + D·y4²)` with **D = 1000·Drive⁴** (parameter 33 → `+0xFC`); `x` =
  excitation (noise filtered by a biquad); **`r = −10·Res·(1 − g)`, clamped to ≥ −4**.
- **Coefficient: `c = g·E_hc`**, `g = clamp((Cutoff + 0.5·mod)², 0, 1)` (round 18), `E_hc` = Hi-Cut envelope
  (`voz+0x178`): attack target 1 with rate `0.0002/T`, `T = (A + mod)²·0.5` (rate 1 if T < 0.001); decay to `S²`
  with `0.0002/(3D³ + 0.001)`; after *note-off* to **0** with rate **0.002/sample**. Without envelope (E = 1) and without
  modulation, **c = Cutoff²**.
- **In Hz** (deduction from the pole `1 − c`, not a constant from the code): per stage **`f = −fs·ln(1 − c)/(2π)`** (c = 0.5
  → 0.110·fs ≈ 5.3 kHz at 48 k; c = 0.1 → 0.0168·fs ≈ 805 Hz; c = 0.01 → ≈ 77 Hz; c = 1 → no filtering). The cascade
  of 4 equal stages (no resonance, analogue approximation for small c) has −3 dB at **≈ 0.435·f**.
- The coefficient does not depend on fs → the Hz scales with the rate at which the engine runs; **whether it is the host rate or a fixed
  48 kHz was not confirmed**.

---

## 6. Pitcher — pending parameters and static approximation **[DECOMP + DOC + DATA]**

Complements `REA_rodada14.md` §3 and `REA_rodada15.md` §1. **Layout in the `.flp` (unchanged):** `"SSLF"` + `u32 0x98`
+ `"1RTP"` + 33 `f32` (parameters 0…32, raw values) + 12 B of note mask (C…B) + `f32` of parameter 33.
In this DLL `0x18002df70` = `2^x` and `0x18002c134` = log2 (**inference from the operands**).

| i | Name | Law | Evidence |
|---|---|---|---|
| 0 | Correction Speed | `u = 1 − v`; u < 0.01 → `c = 1` (instantaneous); otherwise `c = 8.82/(fs·(3u³ + 0.001))`, per-sample pole that takes the smoothed correction (`+0x1724/+0x1728`, semitones) to the target (`+0x1714`) → **τ = (3u³ + 0.001)/8.82 s** (−76 dB in `3u³ + 0.001` s). v = 0.9025 → τ ≈ 0.43 ms; v = 0.6 → τ ≈ 21.9 ms; v = 0 → 0.34 s. With no detected pitch (`+0x16F4 = 0`) the smoothed correction is zeroed. **Corrects round 15** (which estimated 0.07 s for v = 0.6) | DECOMP `FUN_18001cab0` case 0, `FUN_18001e030`; constants 8.82 (`0x18005d608`), 3, 0.001, 0.01 |
| 1 | Stereo | only in the harmony voices: L = `clamp(2·(0.5 − (pan − 0.5)·s), 0, 1)`, R = `clamp(2·(0.5 + (pan − 0.5)·s), 0, 1)`, s = p1 (that `pan` is p17…20 is inference) | DECOMP `FUN_18001f800`; DOC "Panning multiplier" |
| 2 | Gender | factor **`2^(2v − 1)`** (0.5…2; 0.5 = neutral) in `+0x16A0`, used **only in the Formant path** (`FUN_18001f2b0` → `FUN_18001f180`); the internal effect **not decoded** | DECOMP; DOC "Formant override control" |
| 3 | Fine Tune | `2v − 1` **semitones** added to the correction (±100 cents) | DECOMP `FUN_18001e030`; DOC "+/- 100 cents (1 semitone)" |
| 4 | Bypass All | outside MIDI mode, target correction = 0 | DECOMP `FUN_18001ea80` |
| 5…16 | Key C…B Bypass | if the class of the nearest note has its button on, correction = 0 (outside MIDI) | DECOMP; DOC "…pass un-corrected" |
| 22 | **Formant** | chooses the render `FUN_18001f2b0` (with formant/Gender) instead of `FUN_18001ed60`; forces restart (`+0x16A4` → `FUN_18001d290`) | DECOMP; name from DOC (strong inference) |
| 23 | Note A Freq | `A = 55·2^((2v − 1)/12)` Hz (±1 st around 440/8); detected note `n = 9 + 12·log2(f/A)`; v = 0.5 → A 440 Hz; range 415.3…466.2 Hz | DECOMP case 0x17 (1/6, 1/12, 55); `FUN_18001ea80` |
| 24 | Pitch Reference | `int(2v)` → 0/1/2: which detector each channel's correction comes from; with 0, L is copied to R in `FUN_18001ed60`; order L/R/Independent from DOC (case 2 not checked) | DECOMP + DOC |
| 25 | **MIDI** | note mask built from the **held MIDI notes** (vector of 128 at `+0x1068`) | DECOMP `FUN_18001d9c0`, `FUN_18001b800`, `FUN_18001b5e0` |
| 26 | **Octaves** | in MIDI, uses the absolute note (note − 12) instead of the class; in harmony adds 12 × octave | DECOMP; DOC "to the note AND octave played" |
| 27 | **Harmonize** | up to **4 voices** from the MIDI notes; without it, a single voice | DECOMP `FUN_18001f800`, `FUN_18001e030`; DOC "Up to 4 MIDI notes" |
| 28/29 | Tonic / scale | as in round 15 (`FUN_18001d890`) | DECOMP |
| 30 | **Replace/Mix** | in Harmonize, ≠ 0 adds the dry input; = 0 voice 0 has gain 1 | DECOMP; name from DOC |
| 31 | **Vel** | ≠ 0 → per-voice gain × `2·vel²` (only with Octaves off) | DECOMP; DOC |
| 32 | Range | 220/170/110/80/55/25 Hz (round 15) | DECOMP; DOC "minimum detected frequency" |
| 33 | MIDI Port | `int(256v) − 1` (constant −256) — confirms round 16 | DECOMP |

**Correction algorithm** (`FUN_18001ea80`, in our own words): per channel with detected pitch, computes `n`;
searches the 128-note mask for the nearest allowed note below and above; picks the nearest (decision at the
middle of the interval); target = `note − n` semitones; smooths through the Correction Speed pole; adds Fine Tune; the render
applies the shift (in the Formant path, conversion `2^(−s/12)`).

**Data** **[DATA: 65 blobs, 61 in projects + 4 `.fst`]**: Speed 1.0 in 32, 0.9025 in 13, 0.6…0.8 in the rest;
Stereo = 1 in all; Gender 0.5 in 54, ≈0.49/0.5025 in 9, 1.0 in 2; Fine 0.5 (±0.0025); Bypass All and Key Bypass
= 0 in all; Formant on in 7; MIDI + Octaves + Harmonize together in 2 (Bien Duro); 30 = 31 = 0; Pitch Ref 0 in
64; Range 2 in 56, 3 in 9.

**Static approximation:** the code does **not** apply a fixed shift — the correction depends, per sample, on the detected
pitch, the nearest allowed note and the pole. **There is no exact static equivalent.** Exact identity only
with Bypass All = 1 (outside MIDI) and Fine = 0.5 (zero correction); whether the render still colours the sound in that case (or with
Formant and Gender = 0.5) **was not confirmed**. The only honest approximation without a detector is to pass the signal straight through.
A faithful implementation requires a detector (f_min from the p32 table), note choice in the mask, pole `c` and
shifter — the FL detector and shifter **were not decoded**. Harmonize/MIDI depend on the port's notes
(round 16).

### 6b. Fruity Vocoder — basic state **[DFM + DATA; layout by inference]**

- DFM tags (`TSynthEditorForm`): 0 FreqMin (0…65536, default 0), 1 FreqMax (0…65536, 65536), 2 FreqScale
  (0…128, 64), 3 Invert, 4 Formant (−1200…1200, 0), 5 Bandwidth (5…200, 50), 6 Attack (10…1000, 20), 7 Release
  (10…1000, 100), 8 Hold, 9 Modulator level (0…160, 0), 10 Carrier level (0…160, 0), 11 Wet level (0…160, 128).
  Selectors outside the tags: NumBands (6 options, default index 2), Filter order (3, default 2), Listen (3, default 2).
- Observed layout (10 blobs: 1 demo + 9 `.fst`, all version 2): `i32` version = 2; `i32` **number of bands**
  (4, 16 or 32 in the data); two `i32` = 2 and 2 (compatible with Filter order/Listen — **order not confirmed**);
  1 byte (0; **not confirmed**); **N × `f32` = level per band**; **12 × `i32` = tags 0…11** (in the Default preset:
  `0, 65536, 64, 0, 0, 50, 20, 100, 0, 0, 0, 128`). The state reader **was not decompiled** → layout is
  inference from data + DFM.
- Manual: "left channel is the modulator and the right channel is the carrier"; bands "4 to 128" **[DOC
  `…/plugins/Fruity%20Vocoder.htm`]**. Units of Attack/Release/BW/Formant and the FreqMin/Max law in Hz **not
  confirmed**. **Vocodex:** nothing beyond round 14 §6 (**not confirmed**).

---

## 7. Fruity Love Philter — types 4/5, 7 and 11 **[DECOMP]**

**Type → biquad map** (table `0x8d1e4c` → code passed to `FUN_006249d0`, which builds the "Audio
EQ Cookbook" (RBJ) coefficients normalised by `1 + α`; checked in the LP and all-pass cases; `FUN_005fc960` = SinCos):
3 → LP, **4 → BP**, **5 → notch**, 6 → HP, **7 → all-pass**, 8 → low shelf, 9 → peaking, 10 → high shelf.

### 7.1 Types 4/5 (Vanilla band pass / band stop)

- **Frequency:** `x = min((1501^c − 1)·0.00039993, 1.0001)`, `ω = clamp((x + 0.0001)·2π·k, 0, 2.8495)` with
  `k = 44100/fs_interno` → **`fc = (x + 0.0001)·44100` Hz** (c = 0 → 4.4 Hz; 0.5 → ≈ 670 Hz), up to `0.4535·fs_interno`.
- **Width in octaves:** `BW = 0.01 + 0.1199·(101^r − 1)` (r ≤ 0 → 0.01; r ≥ 1 → **12 oct**), with
  `α = sen ω·senh((ln 2/2)·BW′·ω/sen ω)` — the RBJ octave-width formula (`FUN_00466a90` = sinh; global
  `0x8e3500` = `Ln(2)/2`, initialised at `0x62d8b3`).
- `BW′` per stage = `BW·N` in BP and `BW/N` in notch (N = integer of mode `+0x30`; `FUN_0085d5a0` writes `+0xC0`);
  in the *alternate* modes 4/5, N is 4/5 even though the stages are 2/3 (that is what the code does). With ALT (`+0x38`):
  `r ← r·(c + 1)·0.5`.

### 7.2 Type 7 (Vanilla all pass)

- Frequency: same law as 7.1. **`Q = 0.1 + 0.02495·(2001^r − 1)`** (0.1…50); multistage: `Q ← Q^(1/stages)`
  (`+0xBC`, `FUN_005fc870` = power). `α = sen ω/(2Q)`; RBJ all-pass
  `H(z) = [(1 − α) − 2cos ω·z⁻¹ + (1 + α)·z⁻²] / [(1 + α) − 2cos ω·z⁻¹ + (1 − α)·z⁻²]`. ALT: `r·(c + 1)·0.5`.

### 7.3 Type 11 (Mango low pass) (`FUN_006261c0` without *drive*, `FUN_00625ea0` with *drive*)

- **4 cascaded 1st-order poles per channel** (`y += p·(input − y)`); input of the 1st = `in + k·fb`, `fb =
  y₄[n] + y₄[n−1]` from the previous sample; output = `g·(y₄[n] + y₄[n−1])`.
- Coefficients (`FUN_00859100`, case 11): `x = min((201^c − 1)·0.01495, 3.0)`; `w = min((x + 0.01)·0.5·k, 0.65)`;
  **`p = 1 − exp(−π·w·(1.873w³ + 0.4955w² − 0.649w + 0.9988))`** (global `0x8e3440` = `4·ArcTan(1)` = π).
  **Inference:** equating to `1 − e^(−2π·fc/fs)`, `fc ≈ (x + 0.01)·11025·polynomial(w)` Hz (c = 0 → ≈ 110 Hz;
  0.5 → ≈ 2.3 kHz; ceiling at w = 0.65 → ≈ 0.42·fs_interno).
- **Resonance:** `R = r·(1.9 + 0.6·min(G, 1))`; multistage `R ← (R + 1)^(+0xBC) − 1` with
  `+0xBC = 1/(stages/4 + 1)`; **`k = −R·(−3.9364w² + 1.8409w + 0.9968)`**.
- ***Drive*:** `G = (251^s − 1)·0.12`, s = slot parameter 3/128 (`+0x240 = int(+0xC)·0.0078125`, render
  `FUN_0085aa10`). If G ≥ 0.001: each pole uses `p/G·(S(2G·input) − S(2G·y))` with
  `S(u) = u·(u² + 3|u| + 6)/(|u|³ + 3u² + 6|u| + 12)`; `g = 0.5·(1 − k)/tanh(4/G)` (`FUN_00466b40` = tanh by
  structure); with ALT, `k ×= tanh(4/G)`. Without *drive*: `g = 0.5·(1 − k)`.

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Effector Trancer (8) | gain `1 − e·(1 − Y²)`; `e` rises over 1 % of the period and falls as `1 − min(1, u⁴·(50(1−X)² + 1))`; period = Mod Rate; **Wet Mix ignored**; no LFO **[DECOMP]** |
| 1b | Effector Stutter (9) | 1.5 s *buffer* recorded since the *reset*; every period repeats the initial excerpt (*crossfade* 192 samples); **X/Y not used**; dry/wet crossfade **[DECOMP+DOC]**; reverse inactive/not confirmed |
| 1c | Effector Vowel (10) | 8 BP biquads Q = 12 in parallel; 3 formant sets cycling A→B→C→A via `3X'`; Y scales `2^(1.86Y' − 1)`; crossfade **[DECOMP]**; divergence from the manual (5 vowels) |
| 1d | Effector Ring (11) | sine at **`2^(8.7X' + 5)` Hz** (32 Hz…13.3 kHz); `(1 − w)x + w·x·sen`; Y not used **[DECOMP+DOC]** |
| 1e | Effector Distort | 4 stages `s/(1 + k|s|)`, `s/(1 + ks²)`, `s/(1 + k|s|³)`, `s/(1 + ks⁴)`, `k = 2048d²`; `G = 0.5/F(0.5)`; wet `G·F(lp) + (x − lp)` **[DECOMP]** |
| 1f | Effector Reverb | **dry preserved**: `out = x + Reverb(w·x)` **[DECOMP]** |
| 2a | Harmor E | **E = y − 0.5 ∈ [−0.5; 0.5]** (art. 4/5 additive, centre 0.5) → ±7.92 oct·amt (confirms LYNX) **[DECOMP+DATA]** |
| 2b | Harmor resonance | `r = p57/1000·A₈`; per-harmonic gain `2^(r·s(x))` (exp inferred), "Resonance shape" curve, offset p60 in cents **[DECOMP]**; peak in dB **not confirmed** |
| 2c | Harmor *note-off* without env. | **constant** level; voice only leaves via stealing/retrigger with a **10 ms** *declick* **[DECOMP]** |
| 3 | Kepler | `2·L₁ ∈ [−1; 1]` (2,170-pt almost sinusoidal table) → A/A_c are ± peaks; delay by ramp (table D); chorus with mono input, dry/wet **−3 dB each** (`√0.5`), no feedback **[DECOMP+DATA]**; BPM Sync **not confirmed** |
| 4 | Toxic | mode = **p300** (0 Bypass, 1 LP s4, 2 BP s3−s4, 3 HP u−s4); KT = 0.09·KT oct/st (ref. 60); EG `sign(2x−1)(2x−1)²`·9 oct, env 0…1; Lfo1 `x³`·9 oct **[DECOMP+DATA+DOC]** |
| 5 | Sakura Hi-Cut | 4 1st-order poles, `c = g·E_hc`; per stage `f = −fs·ln(1 − c)/2π`, cascade −3 dB ≈ 0.435·f **[DECOMP + deduction]**; effective fs **not confirmed** |
| 6 | Pitcher | buttons 22 Formant, 25 MIDI, 26 Octaves, 27 Harmonize, 30 Mix, 31 Vel; Speed τ = `(3(1−v)³ + 0.001)/8.82` s; Fine ±1 st; A = `55·2^((2v−1)/12)`; Gender `2^(2v−1)` **[DECOMP+DOC+DATA]**; no static approximation |
| 6b | Fruity Vocoder | version, N bands, 2 selectors, N levels, 12 tags (inference from data + DFM) |
| 7 | Love Philter | 4/5: RBJ BP/notch with **width 0.01…12 oct**; 7: RBJ all-pass Q 0.1…50; 11: Mango = 4 poles with feedback and rational *drive* **[DECOMP]** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Effector:**
   - Trancer: gain `1 − e·(1 − Y²)` with the shape from §1.1 and Mod Rate period; do not apply Wet Mix.
   - Stutter: repeat, every period, the excerpt recorded since the start of playback/activation (up to 1.5 s), with
     a 192-sample *crossfade*; ignore X/Y.
   - Vowel: bank of 8 BP Q = 12 with the frequencies from the table, cyclic interpolation via `3X'` and scale
     `2^(1.86Y' − 1)`; × `(1 − 0.125n)`.
   - Ring: `2^(8.7X' + 5)` Hz, sine.
   - Distort: the 4-stage cascade with `G = 0.5/F(0.5)` on the low part only.
   - Reverb: add the reverb to the dry (`x + Reverb(w·x)`) instead of a *crossfade*.
2. **Harmor:** keep the filter envelope centred at 0.5 with ±7.92 oct per amt; with the volume envelope
   off, the note keeps sounding after *note-off* until stealing/retrigger (in LYNX, apply only a 10 ms *declick*
   when the voice is freed by the polyphony limit).
3. **Kepler:** bipolar LFO1 (±1, almost sine) with depth = peak; chorus: sum L+R/2 at the input, dry and
   wet at −3 dB, no feedback.
4. **Toxic:** filter mode via p300 (0/1/2/3 = Bypass/LP/BP/HP); EG Amount `sign(2x−1)(2x−1)²`·9 oct;
   key-track 0.09·KT oct/semitone from note 60.
5. **Sakura:** Hi-Cut as 4 poles with `c = Cutoff²`·envelope; to convert to Hz use `−fs·ln(1 − c)/2π` per
   stage (with the fs caveat).
6. **Pitcher:** no static equivalent — pass straight through; log when Harmonize/MIDI (27/25) or
   Gender ≠ 0.5 with Formant (22) are on.
7. **Love Philter:** types 4/5 with width in octaves (RBJ, 0.01…12 oct), 7 RBJ all-pass (Q 0.1…50), 11 Mango
   with the model from §7.3.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Effector `FUN_180018c20`, `FUN_18001ba60`, `FUN_18001bb30`, `FUN_180018bb0`/`FUN_180018bd0`, `FUN_180016d40`, `FUN_180017010` | per-type dispatch, units in series, X/Y/rate fields, synced phase | DECOMP |
| Effector `FUN_180019f90` (constants 0.01; 1.0101; 50; 100) | Trancer | DECOMP |
| Effector `FUN_18001ae50`, `FUN_180018700` (1.5 s), `FUN_180018a50` (*reset*), `FUN_18001b910`, `FUN_1800179a0` | Stutter | DECOMP |
| Effector `FUN_18001b140` (tables `0x1800586a0…d0`, 3, 0.93, 0.125, Q 12), `FUN_180014520`, `FUN_180014680` | Vowel, BP biquad | DECOMP |
| Effector `FUN_18001b5a0` (8.7 and 5 at `0x18005854c`/`0x180058518`) | Ring | DECOMP |
| Effector `FUN_180019860` (0.95; 0.05; 2048; 0.9; 0.1) | Distort | DECOMP |
| Effector `FUN_18001a130`, `fxReverb` VMT `0x180057fc0` slot 43 `FUN_18001e9f0` | Reverb adds into the output | DECOMP |
| Effector manual (Trans, Grain, Vox, Ring mod, Reverb, Distortion) | X/Y described | DOC `…/plugins/Effector.htm` |
| Harmor `FUN_007a0c60`, `FUN_0079fef0`, `FUN_0079a650`, `FUN_007a4880`, `FUN_0079a7c0`/`FUN_0079a7f0`, `FUN_00896fe0` (`0x89732c`), `FUN_00897c70`, `FUN_00897d90`; 682 parts | centred E | DECOMP+DATA |
| Harmor `FUN_00886eb0`, `FUN_008744c0`, `FUN_00882af0`, `FUN_00882820`, `FUN_00878f60`; DFM | resonance | DECOMP |
| Harmor `FUN_00885390`, `FUN_0088ab10`, `FUN_008856b0`, `FUN_008855e0`, `FUN_00883cd0`, `FUN_008635c0`, `FUN_008781f0`, `FUN_00863450`, `FUN_00862d70` | *note-off*, death, 10 ms *declick* | DECOMP |
| Kepler `FUN_1800166d0`, `FUN_1800225f0`, `FUN_180023ab0`, `FUN_18001b940`, `FUN_180013d30` (`0x180252ED0`), `FUN_180019a20` (`0x180254100`), `FUN_180018fe0`, `FUN_180001a60`, `FUN_180001770` | LFO1 ±1, delay, chorus | DECOMP+DATA |
| Toxic `FUN_180040080`, `FUN_180040ff0`, `FUN_1800406d0`, `FUN_1800395b0`, `FUN_180038d00`, `FUN_180039880`; 7 blobs | p300 mode, scales | DECOMP+DATA |
| Sakura `FUN_18002f4c0`, `FUN_18002da50`, `FUN_180031960` | 4-pole Hi-Cut | DECOMP |
| Pitcher `FUN_18001cab0`, `FUN_18001e030`, `FUN_18001ea80`, `FUN_18001f800`, `FUN_18001d9c0`, `FUN_18001b5e0`, `FUN_18001b800`, `FUN_18001ed60`, `FUN_18001f2b0`, `FUN_18001c240`; 65 blobs | parameters, algorithm | DECOMP+DATA |
| Fruity Vocoder DFM + 10 blobs | state layout | DATA (inference) |
| Love Philter `0x8d1e4c`, `FUN_006249d0`, `FUN_00859100`, `FUN_0085d5a0`, `FUN_006261c0`, `FUN_00625ea0`, `FUN_0085aa10`, `0x8e3500`, `0x8e3440` | types 4/5/7/11 | DECOMP |
| Manual (Pitcher, Fruity Vocoder, Toxic filter) | names, ranges | DOC |

### Open questions

- Effector: who writes the Stutter reverse byte (no writer found); vowel names of the 3
  sets; cutoff in Hz of the Filter cascade (round 18).
- Harmor: factor `2·(hi − lo)` of the direct term of the articulator 4/5 output (not reread); `k₀` of the resonance
  width and default "Resonance shape" curve; "noise" mode; `+0x1150` = `exp`; voice killing by the host.
- Kepler: LFO BPM Sync law; condition of the *gate* G.
- Toxic: range of `vel`; LFO1 amplitude; times of `FUN_18003d480`.
- Sakura: effective engine rate (absolute Hz of the Hi-Cut).
- Pitcher: pitch detector and shifter; internal effect of Gender; whether the render colours the sound with zero
  correction; case 2 of Pitch Reference.
- Fruity Vocoder: state reader (order of the two selectors, extra byte), units; Vocodex.
- Love Philter: exact Hz correspondence of Mango (inference from the pole); meaning of slot parameter 3
  in Mango.
