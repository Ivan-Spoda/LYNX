# Behavioural analysis of FL Studio 2026: round 24 (REA) — exact static spectra: Ogun's timbre engine, Harmless timbre EQ/subs/LHP and Harmor unison

> **Nature of this document.** Continuation of `REA_rodada12.md` §2 (Ogun format), `REA_rodada21.md` §1
> (Harmless) and §2 (Ogun), `REA_rodada18.md` §1.3 and `REA_rodada23.md` §1 (Harmor). *Clean-room* reverse engineering,
> behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is
> here are formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. No encrypted content was touched (no FLEX pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where there
> was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> are stated explicitly as **inference**.

- **Targets:**

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Ogun (and Autogun, same engine — round 12) | `Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= rounds 12/21, checked against the installation) | Delphi (VMT `0x8621f8`) | *note-on* `FUN_0086a8e0`, `ProcessParam` `FUN_008684e0`, load `FUN_00864d00`; timbre generator (object `+0x230`): constructor `FUN_00842340`, sizing `FUN_008424b0`, rate `FUN_00842590`, phase table `FUN_008426e0`, maps `FUN_00842960`, normalisation `FUN_008428c0`/`FUN_00842820`; timbre voice: start `FUN_00842ee0`, spectrum/decay `FUN_00843680` (+ `FUN_00843590`, `FUN_00843470`), first frame `FUN_00843220`, next frame `FUN_00843300`, *release* `FUN_008447d0`, render `FUN_00844940`, reader `FUN_00842c60`/`FUN_00842de0`; map sampling `FUN_0086e6c0`, axis `FUN_00866da0`/`FUN_008669f0`, quality `FUN_00866840`, quality→oversampling `FUN_0086e620`; envelope (library): load `FUN_00781bd0`, evaluation `FUN_00788a20`, coefficients `FUN_00785940`, point value `FUN_00787170`, key/velocity maps `FUN_0078bb40`/`FUN_0078af50`; constants `FUN_0062dec0`, `FUN_00844bc0`; IPP dispatcher `FUN_005bc070` |
  | Installation's Intel IPP | `Shared/dsp_ippv2_x64.dll` `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` | C (IPP) | `ippsRandUniform_Direct_32f` (export `0x1815b3fa0`, dispatcher; generic version `0x1815ed2e0`) |
  | Harmless | `Harmless_x64.dll` (= rounds 10/21) | Delphi | EQ windows `FUN_0077b450`/`FUN_0077b2d0`, application `FUN_00787b40`, LHP `FUN_00784120`, subs `FUN_00784110`, colours `FUN_00787990`/`FUN_007873c0`, `ProcessParam` `FUN_0077f630`, voice `FUN_00784a10`, constants `FUN_00614c80`, IPP dispatcher `FUN_005acdb0` |
  | Harmor | `Harmor_x64.dll` `23673ad8…5aba` (= rounds 11/18/23) | Delphi | unison distribution `FUN_00873e40`, `ProcessParam` `FUN_00879540` (cases 34–39), *note-on* `FUN_00883cd0`, update `FUN_008859a0`, voice `FUN_0088ab10` |

- **Tools:** Ghidra 12.1.4 *headless* on the read-only projects `/tmp/r12gh_Ogun_x64.dll` (program
  `Ogun_x64.dll`), `/tmp/r21gh_Harmless` (`Harmless_x64.dll`) and `/tmp/r23gh` (`hm` = Harmor); new project
  `/tmp/r24gh_ipp` (import without analysis of `dsp_ippv2_x64.dll`). Scripts in `tmp/r24/scripts/` (those of round 23
  + `DecompRec.java`); *dumps* in `tmp/r24/out/`; our own numerical models in Python (`tmp/r24/py/ipprand.py`,
  `ogspec.py`, `oglynx.py`, `hmuni.py`, `env.py`, `og.py`) on `tmp/r14/blobs14.pkl`; execution test
  `tmp/r24/run/rnd.exe` (our own C, compiled with Zig for Windows, which only loads the installation's IPP DLL and
  calls `ippsRandUniform_Direct_32f`). Official manual read via the web.
- **Date:** 2026-10-13.
- **Dataset:** 20 Ogun channels in `Demo projects/**` (state versions v19 = 6, v18 = 14).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by calling the function of the installation's own DLL (without modifying it) |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated `…/plugins/`). The quotations came from summarised web reading; re-check before they become interface text.

**Conventions.** Ogun: `FUN_0040bf50` = ln, `FUN_0040ad30` = exp, `FUN_0040c3d0` = square root,
`FUN_0040c3e0` = rounding, `FUN_0040c4d0` = truncation, `FUN_005fc620` = log₂ (ln × 1/ln 2),
`FUN_005fc730` → `FUN_0040bcc0` = SinCos (1st output sine, 2nd cosine: quadrant table) **[DECOMP]**.
Harmless: `FUN_00409b50` = ln, `FUN_00409520` = cosine, `FUN_00409610` = sine (polynomial kernels: the one in
`FUN_00409390` starts at 1 − x²/2, the one in `FUN_00409460` at x), `0x7ff4e8` = 1/ln 2 **[DECOMP]**. IPP names via the
dispatchers (pointer ↔ name pairs): Ogun `0x8e9bb0` = `ippsRandUniform_Direct_32f`, `0x8e9ee0` =
`ippsMulC_32f_I`, `0x8e8fb0` = `ippsAddC_32f_I`, `0x8e9a30` = `ippsMul_32f_I`, `0x8ea3c8` = `ippsSub_32f_I`,
`0x8e9280` = `ippsMulC_32f`, `0x8e8d88` = `ippsMul_32f`, `0x8e90d0` = `ippsDivCRev_32f_I`, `0x8e8c70` =
`ippsExp_32f_A21`, `0x8ea3a8` = `ippsExp_32f_A11`, `0x8e9588` = `ippsSubCRev_32f_I`, `0x8e8d38` = `ippsSqr_32f`,
`0x8e97c8` = `ippsSum_32f`, `0x8ea380` = `ippsSet_32f`, `0x8ea2f8` = `ippsCopy_32f`, `0x8ea2c0` = `ippsAdd_32f_I`,
`0x8e9fd0`/`0x8ea1b8` = `ippsFFTGetSize/Init_R_32f`, `0x8e8fe8`/`0x8e9c58` = `ippsFFTInv_PackToR_32f(_I)`;
Harmless `0x7ef3c8` = `ippsMul_32f`, `0x7ef850` = `ippsMulC_32f`, `0x7f0960` = `ippsAddProductC_32f`
**[DECOMP]**. Ogun *runtime* constants: `0x8f92c8` = ln 0.01 (round 21), `0x8f94f0` = ln 2,
`0x8f9510` = ln 2/12, `0x8f953c` = ln 10/20 **[DECOMP `FUN_0062dec0`]**; **`0x8fc038` = 440·2^(75/12) =
33 488.07 Hz** (= 261.63 Hz × 2⁷) **[DECOMP `FUN_00844bc0`: exp(75·ln2/12)·440]**.

**LYNX convention** (same as round 23): the "spectral" oscillator sums `(2/π)·spectrum[h]·sin(2π(h·t +
spectrumPhase[h]))`; **A(h)** here is always the **physical** amplitude of the harmonic's sine; `spectrum[h] = A(h)·π/2`
reproduces the level, `spectrum[h] = A(h)` reproduces the shape.

---

## 1. Ogun: the timbre engine

**What LYNX assumes** (`src/core/FlGenerators.cpp`, `synthFromOgun`): sine/triangle/saw chosen by
*fullness*/*richness*, partial +1 octave if fullness > 300. **None of this has a basis in the code** (see below): Ogun
is a **spectral synthesizer via inverse FFT** whose spectrum is pseudorandom noise shaped by maps.

### 1.1 Structure **[DECOMP `FUN_008424b0`, `FUN_00842590`, `FUN_008426e0`, `FUN_00843300`, `FUN_00844940`, `FUN_00842c60`]**

- The timbre generator (one per instance, `+0x230`) creates a real FFT of **order `P52 + q`** (size
  `N = 2^(P52+q)`; `ippsFFTGetSize_R_32f` with *flag* 8) and uses **`M = 2^(P52−1) − 1` partials** (bins 1…M).
  Richness 1…11 = P52 6…16 → M = 31…32 767, exactly the manual's "31 to 32,767" (round 21) **[DECOMP + DOC]**.
- `q` (table oversampling) comes from the quality (P54 = `+0x204`): quality 0/1 → q = 0, 2 → 1, 3/4 → 2; and if
  P52 < 10 with rate < 88 200 Hz and q < 1, q = 1. The same selector sets the **frame step `hop` = 50 ms** (quality
  0–2) or **25 ms** (3–4) (`+0x25c` × 0.001 × fs) **[DECOMP `FUN_00866840`, `FUN_0086e620`, `FUN_00864800`]**.
  In the demos: P54 = 0 in 17, 1 in 3 **[DATA]**.
- Every `hop` samples the voice builds the spectrum (*Pack* format: bin h = (Re, Im) at positions 2h−1, 2h), runs the
  inverse FFT without division into a table of N samples and **reads that table in a loop** with a resampler (fractional
  step, interpolation of an order chosen by the quality), with a linear *crossfade* between the old frame and the
  new one within the `hop` **[DECOMP]**. **Inference (IPP: *flag* 8 = no division):** the sine of partial h in the table
  has amplitude **2·|X_h|**.
- **Partial frequency.** The read step is `ratio · 33 488.07 · 2^q / fs` table samples per sample
  (`+0x7c` × ratio; ratio = 2^(c/1200) with c = voice pitch in cents + `100·P1 + P2` + pitch articulator)
  **[DECOMP `FUN_00842590`, `FUN_00844660`, *note-on*]**. Since the table has N = 2^(P52+q) samples, the table
  period is `f_tab = ratio · 33 488.07 / 2^P52` and partial h sounds at **`h · f_tab`**. The same *note-on* uses
  `c/100 + 60` as the note number, which shows that **c = 0 at C5** **[DECOMP]**; hence, for the note of
  frequency f (261.63 Hz at C5):

  **`f_h = h · f · 2^(7 − P52)`** (direct inference from the code)

  i.e. **the note's fundamental is partial h = 2^(P52−7)**; the intermediate partials are **inharmonic**
  (fractions of f), and the highest one (h = M) sits at ≈ 64·f whatever the richness. Richness controls the
  **density** of partials, not the bandwidth. For P52 = 6 only the even harmonics exist (2f, 4f…); for
  P52 = 7 the partials are exactly harmonics 1…63.
- **Band limit:** only partials `h < min(M, ⌊M·2^q·0.907029/step⌋)` are filled (0.907029 =
  40 000/44 100) **[DECOMP `FUN_00843680`]**; **inference:** cuts at ≈ 0.4535·fs (20 kHz at 44.1 kHz).

### 1.2 Phases (fixed for all presets) **[DECOMP `FUN_008426e0`, `FUN_00842340`; EXEC]**

- A base table is generated **once** with M angles `θ_h` uniform in [0, 6.2831855) by the IPP
  (`ippsRandUniform_Direct_32f`) with **fixed seed 3**; bin h receives `(cos θ_h, sin θ_h)/√M` **[DECOMP]**.
  Hence, in the sine convention: **`φ_h = θ_h/2π + 1/4` cycles** (inference: Re = cos, Im = sin from the SinCos order
  and the *Pack* inverse FFT).
- **Read start phase:** on the first frame, the start position of each unison voice is `unison_phase ·
  N + j₀ − 32` (mod N), where **j₀ = index of the sample with the smallest |x|²** of the freshly generated table (`FUN_00842e90`) and 32 =
  half of the interpolator kernel (`+0xc` = 64) **[DECOMP `FUN_00843220`]**. Therefore the effective phases are
  `φ_h + h·(j₀ − 32)/N` (plus the unison offset) — computable from the table.

### 1.3 The IPP uniform generator (needed for phases and seeds) **[DECOMP `0x1815ed2e0`; EXEC]**

Description (our own words) of the generic version of `ippsRandUniform_Direct_32f(dst, n, lo, hi, &s)`:
- two 32-bit sequences: (a) **subtract-with-borrow**: `e_k = e_{k−2} − e_{k−3} − b`, with `b` = 1 if the
  previous result went negative (initially 1); if `e_k` (signed) is negative, 18 is subtracted (arithmetic
  mod 2³²); initial values `e_{−2} = (s·69069 + 1013904243)·69069 + 1013904243`, `e_{−1} = s·69069 + 1013904243`,
  `e_0 = s`; (b) **congruential** `L_k = L_{k−1}·69069 + 1013904243` with `L_0 = 0x436CBAE9`;
- output k = `float((int32)(e_k + L_k) · (hi − lo)·2⁻³² + (lo + hi)/2)`; the returned seed is the last `e_k`.
- **Verification [EXEC]:** the model (`tmp/r24/py/ipprand.py`) reproduces the installation's DLL with the same output
  seed and identical values for n = 1/7 and several seeds; for n = 32 767 there are differences of **1 ulp** in 243 of
  32 767 values (the vectorised path chosen by the CPU rounds differently) — irrelevant for spectra.

### 1.4 The seeds **[DECOMP `FUN_008684e0` cases 10/11, *note-on* `FUN_0086a8e0`; DOC]**

- `ProcessParam` stores, for Seed 1 (P10) and Seed 2 (P11): **−1 if the value is 0**, otherwise `|mix(v − 0x20CF)|`, where
  `mix` is a 32-bit integer mix (Jenkins style: 8 rounds of subtraction/xor with shifts 8, 13, 12,
  16, 5, 3, 10, 15; constants 0x1CFC and 0x1EEF) **[DECOMP]** (model: `ogspec.py`, `mix`).
- Seed 1 with byte `+0x20C` (body `0xE0`) on: per-note seed `|mix(P10 + note − 0x1F88)|` (constant
  0x1E42) **[DECOMP]**; 1/20 demos (Chase) **[DATA]**.
- **Seed 2 = 0 ("Random"): new seed on every note** = `|mix(counter − 0x200E)|` (constant 0x1DBC), with the
  counter `+0x244` incremented on every note and initialised from a runtime global (`PTR 0x8ea240`) **[DECOMP]** —
  **non-deterministic**. Manual: "Seed 2 harmonics are randomized each time a key is played"; Seed 1 "None":
  "all harmonics are activated" **[DOC `…/plugins/Ogun_synthesis.htm`]**.

### 1.5 From the state to the "weight" B(h) of each partial **[DECOMP `FUN_00843680`, `FUN_00843590`, `FUN_00843470`, `FUN_00842ee0`, *note-on*]**

Input values (at *note-on*; "art k" = static output of articulator k, see §1.8):
- **fullness** `f = clamp(P8/1000 + 2·art9, −1, 1)`; inside the voice it becomes **`p = −sign(f)·(10000001^|f| − 1)·0.01`**
  (|f| = 1 → |p| = 100 000) **[DECOMP: 10000001, 1, 0.01]**;
- **morph** `m = P9/1000 · art10`; **pre-decay** `P = max(0.001, P5/1000 · art6)`;
- **rotation** `ρ = (round(art11·M + 256·M)) mod M`; **EQ shift** `s = round(clamp(art12, −0.5, 0.5)·M)`.

Computation:
1. For each valid seed S: `u = RandUniform(S, M, 0, 1)` (§1.3), rotated: `u'[(i + ρ) mod M] = u[i]`.
2. Shape by fullness: `g(u) = u` (p = 0); `((1+p)^u − 1)/p` (p > 0, i.e. f < 0: values pulled towards 0 =
   sparse spectrum); `((1+|p|) − (1+|p|)^(1−u))/|p|` (p < 0, i.e. f > 0: values pulled towards 1 = full
   spectrum). Matches the manual: "blends between … the Seeded harmonic spectrum (-100) and the full harmonic
   spectrum" **[DOC]**.
3. **`B = m·g(u'_{S2}) + (1 − m)·G₁`**, with `G₁ = g(u'_{S1})` if Seed 1 is valid, `G₁ = 1` if Seed 1 = "None";
   m = 0 skips Seed 2, m ≥ 1 skips Seed 1; negatives → 0 **[DECOMP]**.
4. **Forced harmonics** (map F, §1.6): **`B ← 1 − F·(1 − B)`** (F = 0 forces the partial to 1; F = 1 leaves it unchanged); a
   constant map equal to 0 is discarded (no effect) **[DECOMP `FUN_0086e6c0`, `FUN_00842960`]**.

### 1.6 The four per-partial maps (Forced, Impulse EQ, Decay EQ, Release EQ) **[DECOMP `FUN_0086e6c0`, `FUN_00866da0`, `FUN_008669f0`, `FUN_00864d00`; DATA]**

- In the state, after the 13 articulators (each: 16 B header, type 1 envelope, 16 B, type 2 LFO, 20 B,
  4 type 3 maps = key, velocity, X, Y), come **4 type 3 envelopes: 13 Forced harmonics, 14 Impulse EQ,
  15 Decay EQ, 16 Release EQ** (82 envelopes per channel in 20/20 demos) **[DECOMP `FUN_00781bd0` + DATA]**.
  Envelope format: `i32 type, i32 version (3), i32 n`, n × (`f64 Δx, f64 y, f32 tension, u8 mode, 2 B, u8`),
  20 B header **[DECOMP]**. Evaluation: **same library as round 23 §1.2** (modes 0/2/5/6/7 checked:
  constant 1000001 in `FUN_00785940`, same formulas in `FUN_00788a20`; value = y − centre `+0x18`)
  **[DECOMP]**. Modes 1 (double curve) and 10 (spline, per-point coefficients) appear only in the Decay EQ of 3 channels
  **[DATA]**; their exact formulas **were not transcribed**.
- **x axis of the partial with index i = h − 1:** `x = i/32767` (linear) or, with byte `+0x213` (body `0xE7`) on,
  **`x = log₂(h)/15`** (15 octaves); sampled at `x + 5.96·10⁻⁸` **[DECOMP]**. Linear in 12/20 demos, log in 8
  **[DATA]**. Length: Forced = M; the others = min(1.5·M; 32 768); beyond the end the last value holds.
- **Value:** y clamped to [0, 1]; Forced uses y; the three EQs use **`E = (3^(2y) − 1)/2`** (y = 0.5 → 1 = 0 dB;
  1 → 4; 0 → 0) **[DECOMP: 2.0, ln 3]**.
- Option `+0x226` (body `0xFA`): multiplies Forced, Impulse (and Decay, if ALT DECAY) by `1/√h` (0/20 demos).
- **Fixed patterns** (integer `+0x214 + 4k`, body `0xE8 + 4k`, k = 0…3 for 13…16): a value v ≥ 1 zeroes the map at
  positions `i mod (v+1) = v` (v < 5) or `i mod (v−2) ≠ 0` (v ≥ 5), applied after sampling
  **[DECOMP]**; in the demos: Forced v = 3 (Clap, Snare), Forced 1 + Decay 4 (Swebass 1/2), Impulse 4 (Turbular
  Bells 2) **[DATA]**. (For Forced, 0 = "force".)

### 1.7 Initial spectrum and per-partial decay **[DECOMP `FUN_00843680`, `FUN_008428c0`, `FUN_00842820`]**

- **"Pre-decayed" amplitude:** `D(h) = 0.01^(1/(B(h)/P + 0.001))` — the starting spectrum is what would remain
  after a time `P` if each partial fell 40 dB over a time proportional to B(h). Manual: "Timbre pre-decay.
  Morphs toward a 'Metallic' EQ mapping" **[DOC]**. With B = 1 (Seed 1 None and m = 0) D is constant.
- **RMS normalisation:** `D̂ = D · g / √(ΣD²/M)`, with `g = 1`, or, if byte `+0x225` (body `0xF9`, 11/20
  demos) is on, `g = 0.5/r`, `r = √(Σ E_imp²/M)` (with ALT DECAY: the larger of the Impulse and Decay r)
  **[DECOMP]**.
- **Note spectrum at the start:** `X_h = (cos θ_h, sin θ_h)/√M · D̂(h) · E_imp(h − 1 − s)` (index clamped to the
  map), zeroed outside [first, last partial with E_imp > 5.96·10⁻⁸] when s = 0 and above the band limit.
  In the table: **`A_tab(h) = 2·D̂(h)·E_imp(h−1−s)/√M`**, phase §1.2 **[DECOMP + IPP inference]**.
- **Decay (ALT DECAY off):** every `hop` the spectrum is multiplied, per partial, by
  `0.01^(1/(T·B(h)·E_dec(h−1−s) + T₀))`, with `T = max(0, t_dec·fs/hop − 0.025·fs/hop)` and `T₀ = 0.025·fs/hop`
  **[DECOMP: ln 0.01, 0.025]**; t_dec = `((21^(P6/10⁴) − 1)·997.5·art7 + 50)` ms (round 21). Hence each partial falls
  **40 dB in `τ_h = (t_dec − 0.025)·B(h)·E_dec(h) + 0.025` s**; P6 = 10 000 → no decay (round 21) **[DECOMP]**.
  Confirms the manual: "Combines with the Decay mapping … that defines per-harmonic decay time" **[DOC]**.
- **Release:** the same law with Release EQ and t_rel: `τ_h,rel = (t_rel − 0.025)·B(h)·E_rel(h) + 0.025` s
  **[DECOMP `FUN_008447d0`]**.
- **ALT DECAY** (byte `+0x224`, body `0xF8`; 6/20 demos): the spectrum moves from the Impulse EQ shape to the shape
  of the Decay EQ ("sets an explicit decay spectrum" **[DOC]**), with a per-frame coefficient `1 − 0.01^(1/T)`
  **[DECOMP]**; the exact frame-by-frame mix **was not transcribed**.

### 1.8 Timbral articulators in the static case **[DECOMP `FUN_0078bb40`, `FUN_0078af50`, `FUN_00787170`, `FUN_0086d9e0`; DATA]**

The static value of an articulator k ≥ 5 combines the key map (evaluated at note/120) and the velocity map according to the
articulator's mode (`+8`: 0 product, 1 sum, other 2·product), and then the X/Y tables (513 points) if the X/Y maps are not
neutral; each map returns **y − centre**. In the demos, the default maps are 0.5 on articulators 9, 11, 12
(and 1, 2, 3) and 1.0 on 6, 7, 8, 10 **[DATA]**; **inference:** centre 0.5 on the former → art9 = art11 = art12 = 0,
art6 = art7 = art10 = 1 with default maps. The mode field `+8` **was not located in the state** (not confirmed);
the models below use these neutral values.

### 1.9 Verification on the demos **[DATA + model `tmp/r24/py/ogspec.py`, `oglynx.py`]**

- **Deterministic: 9/20** (valid Seed 2 or m = 0): Metal Drums Drum, Tibetan, Bellish 11, High Chords, Kick,
  Clap, Snare, Trumpet, Turbular Bells 2. **11/20 use Seed 2 = Random with m > 0** (new spectrum on every note).
- **Note harmonics representable in LYNX** = partials h = h'·2^(P52−7) (P52 ≥ 7) or even h' (P52 = 6). Fraction
  of the initial spectrum's energy that falls on these harmonics: Bellish 11 and Trumpet (P52 = 7) **100 %**; Kick (P52 = 9)
  22.6 %; Clap 6.9 %; High Chords 5.3 %; Snare 1.5 %; Tibetan 0.4 %; Metal Drums 0.2 %; Turbular Bells ≈ 0.
- Examples (A = amplitude in the table, before volume/unison; h' = note harmonic; φ in cycles without the
  j₀ offset of §1.2; τ = −40 dB):

| Channel | P52 / M | B | A(h' = 1…6) | φ(1…4) | τ(1…4) s |
|---|---|---|---|---|---|
| Trumpet (Electric Loop) | 7 / 63 | 1 (no seeds) | 0.9087; 0.8610; 0.8151; 0.7711; 0.7290; 0.6888 | 0.919; 0.380; 0.130; 0.985 | 0.083; 0.080; 0.076; 0.073 |
| Bellish 11 (Edlan & Ella Noël) | 7 / 63 | Seed 2 = 29, m = 1, f = −1 | 0; 0; 0; **0.5932**; 0; 0 | same | 0.026; 3.41; 0.025; 2.06 |
| Kick (Electric Loop) | 9 / 255 | 1 | 0.2661; 0.1533; 0.2726; 0.3955; 0.0759; 0.0712 | 0.985; 0.483; 0.779; 0.508 | 0.100 (all) |
| Clap (Electric Loop) | 12 / 2047 | Seed 1 = 68, f = −1, Forced v = 3 | 0.0330 (all) | 0.144; 0.378; 0.285; 0.774 | 0.490 |

  The phases 0.919/0.380/0.130/0.985… are those of partials 1, 2, 3, 4 of the seed-3 table and repeat in all
  presets with P52 = 7 **[EXEC + DECOMP]**.

---

## 2. Harmless: timbre EQ, subs and LHP

**What LYNX assumes** (`src/core/FlSynths.cpp`, `synthFromHarmless`): colour + mask in the "spectral" oscillator;
no timbre EQ and no LHP; Sub 1 as a separate sine.

### 2.1 Windows of the 6-band EQ **[DECOMP `FUN_0077b450`, `FUN_0077b2d0`; DOC]**

- Computed **once** (global tables) over the base ratio table `r = {1, 0.5, 1.5, 2.5, 1, 2, 3, …, 524}`
  (528 positions). For band k = 0…5 and ratio position r: `x = log₂(r) − k`; **`W_k = (cos(π·clamp(x, −1, 1)) +
  1)/2`** (Hann window of ±1 octave); **band 0 is 1 for x < 0** (low shelf) and **band 5 is 1 for
  x > 0** (high shelf) **[DECOMP: centres 0…5, width 1, π, 0.5]**. Centres at harmonics **1, 2, 4, 8, 16,
  32**. The 6 windows sum to 1 at every position (numerical **inference**). Manual: "a floating equalizer, relative to
  the root note" **[DOC `…/plugins/Harmless.htm`]**.
- Since they are computed over the base ratios, **they do not follow the p39 *detune*** (inference: global tables
  built once).

| h (or position) | W₀ | W₁ | W₂ | W₃ | W₄ | W₅ |
|---|---|---|---|---|---|---|
| Sub 1 (0.5) and h = 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| Sub 3 (1.5) | 0.3681 | 0.6319 | 0 | 0 | 0 | 0 |
| h = 2 | 0 | 1 | 0 | 0 | 0 | 0 |
| Sub 4 (2.5) | 0 | 0.7654 | 0.2346 | 0 | 0 | 0 |
| h = 3 | 0 | 0.3681 | 0.6319 | 0 | 0 | 0 |
| h = 5 / 6 / 7 | 0 | 0 | 0.7654 / 0.3681 / 0.0888 | 0.2346 / 0.6319 / 0.9112 | 0 | 0 |
| h = 12 / 24 | 0 | 0 | 0 | 0.3681 / 0 | 0.6319 / 0.3681 | 0 / 0.6319 |
| h ≥ 32 | 0 | 0 | 0 | 0 | 0 | 1 |

### 2.2 How the gains enter **[DECOMP `FUN_00787b40`, `ProcessParam` cases 5–10]**

- Gain of band k (parameter p(5+k), ±1024): **`g_k = (3^((v+1024)/1024) − 1)/2`** (v = 0 → 1; −1024 → 0;
  +1024 → 4) **[DECOMP]**.
- **`A_EQ(pos) = A_colour(pos) · Σ_k g_k·W_k(pos)`** (multiplication by the window and per-band weighted sum) **[DECOMP]**.
- Order in the voice: colour table × EQ → mask (round 21 §1.2) → LHP (§2.4) → gains of the 4 special positions
  (subs, §2.3) **[DECOMP `FUN_00784a10`]**; above position 464 (harmonic 461) a ramp
  `(cos((i−1)·π/65) + 1)/2` attenuates the last 64 positions (irrelevant for 256 harmonics).
- **Data:** EQ ≠ 0 in **96/139** Harmless channels; the most common value ≠ 0 is −1024 (band off) **[DATA]**.

### 2.3 Sub 1 / Sub 3 / Sub 4 **[DECOMP `FUN_00787990`, `FUN_00784110`, `FUN_00787ce0`; DOC]**

- Table positions 1/2/3, ratios 0.5 / (1 + 0.5D) / (1 + 1.5D), D = 2^(p39/1200) (round 21).
- Level: **`Sub1 = (p2/1024)·A_colour(1)·g₀`**, **`Sub3 = (p3/1024)·A_colour(1.5)·(0.3681·g₀ + 0.6319·g₁)`**,
  **`Sub4 = (p4/1024)·A_colour(2.5)·(0.7654·g₁ + 0.2346·g₂)`** — the colour is evaluated at the fractional index (i = 0.5 and
  1.5) by the same colour law (e.g. Saw → 1/1.5 and 1/2.5) **[DECOMP]**; the parity rule of the even/odd colours
  at a fractional index **was not checked**. The mask does not act on the subs **[DECOMP]**. Phases: those of the colour at the
  same indices.
- Manual: "Sub 3 is the 3rd harmonic", "Sub 4 is the 4th harmonic" (of Sub 1) **[DOC]** — consistent with 1.5f and 2.5f.
- **Data:** p3 > 0 in 14/139, p4 > 0 in 12/139 (round 21) **[DATA]**.

### 2.4 Exact Low Harmonics Protection **[DECOMP `FUN_0077b450`, `FUN_00784120`, `ProcessParam` case 1; DOC]**

- `w = (11^(p1/1024) − 1)/10` (p1 = 1024 → 1) **[DECOMP]**.
- For the first 8 positions: **`A = A_mask + (A_EQ − A_mask)·p_k·w`**, i.e. per harmonic n = 1…4:
  **`A(n) = A_colour(n)·E(n)·[m_n + (1 − m_n)·p_n·w]`**, with `m_n` the mask factor (round 21) and
  `p = ((cos(π·log₂(max(r,1))/log₂5) + 1)/2)^5` **[DECOMP]**: **p = 1 (n = 1); 0.0831 (n = 2); 0.000625 (n = 3);
  ≈ 2·10⁻⁷ (n = 4)**; special positions: 1; 1; 0.4474; 0.00925.
- Effect: the LHP **undoes the mask** (not the EQ) on the first harmonics — matches the manual: "Prevents
  filtering functions (harmonic mask) from removing the first few harmonics" **[DOC]**.
- **Data:** p1 = 256 (default; w = 0.0817) in 99/139; 0 in 20 **[DATA]**.

---

## 3. Harmor: unison

### 3.1 Parameters **[DECOMP `FUN_00879540` cases 34–39; DFM]**

| Tag | Control (DFM) | Part field | Law |
|---|---|---|---|
| 34 | *Unison order* 1…9 | `+0x1BC` = n | also `1/n`, `1/(n−1)`, `0.5/(n−0.5)` and gain `+0x1C8` |
| 35 | *Unison distribution* (combo: Classic, Blurred, Uniform, Random, —Special, Hz) | `+0x1B8` = 0…4 | §3.2 |
| 36 | *Unison alternate distribution* | (read at the note) | **inverts the polarity** (sign of the gain) of voices with odd (k + part) **[DECOMP]**; manual: "Alternates gain polarity" **[DOC `…/plugins/Harmor.htm`]** |
| 37 | *Unison panning* 0…256 (default 192) | `+0x1D8` | `v/192` (v ≤ 192) or `1 + (v−192)·0.046875` (up to 4) |
| 38 | *Unison pitch thickness* 0…256 | `+0x1C0` | `(41^(v/256) − 1)·1.875` cents (round 18) |
| 39 | *Unison phase* −1000…1000 (default 500) | `+0x1C4` | `v·0.001` |

### 3.2 Per-voice pitch offset `d_k` (k = 0…n−1) **[DECOMP `FUN_00873e40`, table `0x928290`]**

- Base: `|2k/(n−1) − 1|` (types 0–2), `|2(k + 0.25)/(n − 0.5) − 1|` (type 3), `|2(k + 0.5)/n − 1|` (type 4); **negative
  sign for odd k, inverted for the voices of the upper half when n is odd** (2k > n).
- Type 0 **Classic**: signed base **+ (1/(n−1))·J0[k]**, J0 = {0.306931, −0.28972, 0.392405, −0.223022,
  0.508197, −0.236641, 0.43662, −0.348315, 0.274336}; then **subtracts the mean**.
- Type 1 **Blurred**: signed base **× J1[k]**, J1 = {0.890472, 1.076426, 0.911577, 1.102536, 1.034126,
  1.166861, 0.853971, 1.206273, 1.0}; then subtracts the mean.
- Type 2 **Uniform**: signed base, without centring.
- Type 3 **Random**: the type 3 base **+ uniform noise ±0.5/(n−0.5) drawn on every note** (`ippsRandUniform`
  with a runtime seed) — non-deterministic **[DECOMP `FUN_00883cd0`]**.
- Type 4 **Hz**: the per-voice offset is forced to 1 and a random table of 520 values is drawn at the note;
  the use of that table **was not followed** (not confirmed).
- **Detuning of voice k** (cents) = **`d_k · s₃₈ · (unison pitch articulator)²`** added to the voice pitch
  (round 18) **[DECOMP `FUN_008859a0`, `FUN_00883cd0`]**; in addition the voice passes `d_k·s₃₈` to the sampling of the
  *Harmonic unison pitch* curve (*shaping* envelope 8, round 23) — **inference:** per-harmonic scale, 1 with the
  default curve.

Table `d_k` (model `tmp/r24/py/hmuni.py`):

| n | Classic (0) | Blurred (1) | Uniform (2) |
|---|---|---|---|
| 2 | +1.2983 −1.2983 | +0.9834 −0.9834 | +1 −1 |
| 3 | +1.0852 −0.2131 −0.8721 | +0.8975 +0.0070 −0.9045 | +1 0 −1 |
| 4 | +1.0868 −0.4455 +0.4486 −1.0899 | +0.9572 −0.2921 +0.3706 −1.0358 | +1 −⅓ +⅓ −1 |
| 5 | +1.0420 −0.6072 +0.0634 +0.4095 −0.9077 | +0.9166 −0.5121 +0.0261 +0.5774 −1.0080 | +1 −½ 0 +½ −1 |
| 9 | +1.0270 −0.7976 +0.5377 −0.2893 +0.0521 +0.2090 −0.4568 +0.6951 −0.9771 | +0.8868 −0.8110 +0.4522 −0.2793 −0.0036 +0.2881 −0.4306 +0.9011 −1.0036 | +1 −¾ +½ −¼ 0 +¼ −½ +¾ −1 |

(n = 6–8 and types 3/4 in `tmp/r24/out/hm_unison_offsets.txt`, generated by `hmuni.py`.)

### 3.3 Per-voice pan, gain and phase **[DECOMP `FUN_00883cd0`, `FUN_0088ab10`, `ProcessParam` case 34]**

- **Pan of voice k:** `clamp((2k/(n−1) − 1)·pan₃₇, −1, 1)` — **linear in voice order** (does not alternate like the pitch).
- **Gain of each voice:** **`1 − (3^(1 − 1/n) − 1)/2`** (n = 2 → 0.634; 3 → 0.460; 4 → 0.360; 5 → 0.296; 9 → 0.172);
  n = 1 → 1 **[DECOMP: 1, 3, 0.5]**.
- **Start phase:** voice k receives `+(k/n)·(p39/1000)·(unison phase articulator)` added to the base phase; with the
  product = 1 it enters a special branch (free phase). Unit (probably cycles) **not confirmed**.
- **Data (198 parts):** n ≥ 2 in 78; distribution **Blurred in 58**, Classic 8, Uniform 4, Random 3, Hz 5; p36 = 0
  in all; most common pan 181/256/192 **[DATA]**.

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Ogun: engine | inverse-FFT synthesis of N = 2^(P52+q); **M = 2^(P52−1) − 1 partials**, 50/25 ms frames **[DECOMP]** |
| 1b | Ogun: frequency | **f_h = h·f·2^(7−P52)** (note fundamental = partial 2^(P52−7)) **[DECOMP + inference]** |
| 1c | Ogun: phases | fixed random: IPP RandUniform seed 3, `φ_h = θ_h/2π + ¼`; IPP generator described and reproduced **[DECOMP + EXEC]** |
| 1d | Ogun: seeds/fullness/morph | `B = m·g(u₂) + (1−m)·g(u₁)`; g by fullness with `|p| = (10000001^|f| − 1)/100`; Seed 2 = 0 → random per note **[DECOMP + DOC]** |
| 1e | Ogun: initial spectrum | `A = 2·D̂·E_imp/√M`, `D = 0.01^(1/(B/P + 0.001))`, RMS normalisation **[DECOMP]** |
| 1f | Ogun: maps | Forced `1 − F(1−B)`; EQ `(3^(2y) − 1)/2`; axis `i/32767` or `log₂h/15`; fixed patterns **[DECOMP]** |
| 1g | Ogun: decay | **τ_h = (t − 0.025)·B·E_dec + 0.025 s** at −40 dB (same for release) **[DECOMP]** |
| 1h | Ogun: demos | 9/20 deterministic; only P52 = 7 (2 channels) fits 100 % into harmonics **[DATA]** |
| 2a | Harmless: EQ | Hann of ±1 octave in log₂(r), centres h = 1, 2, 4, 8, 16, 32, shelves at the ends; `A·Σ g_k W_k` **[DECOMP]** |
| 2b | Harmless: subs | levels with the colour at 1.5/2.5 and the EQ windows; freq. 0.5f, (1+0.5D)f, (1+1.5D)f **[DECOMP]** |
| 2c | Harmless: LHP | undoes the mask: `m + (1−m)·p·w`, p = 1; 0.083; 0.0006; ~0 (h = 1…4) **[DECOMP + DOC]** |
| 3a | Harmor: offsets | table §3.2 (Classic with jitter J0, Blurred × J1, Uniform), centred (0/1) **[DECOMP]** |
| 3b | Harmor: pan/gain/phase | linear pan `(2k/(n−1) − 1)·pan`; gain `1 − (3^(1−1/n) − 1)/2`; phase `k/n·p39/1000` **[DECOMP]**; p36 = alternating polarity **[DECOMP + DOC]** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Ogun (`synthFromOgun`):** drop the sine/triangle/saw choice. For **P52 = 7** (and P52 = 6, even
   harmonics only) and a **deterministic channel** (Seed 2 ≠ 0 or P9 = 0), generate the "spectral" oscillator with
   `A(h') = 2·D̂(h)·E_imp(h−1−s)/√M`, `h = h'·2^(P52−7)`, `spectrumPhase = θ_h/2π + 0.25` (θ from the reimplementation of the
   generator of §1.3, seed 3, M values in [0, 6.2831855)), following §1.4–§1.7 (maps 13–16 with the axis and the
   patterns of §1.6, envelope evaluation from round 23). For P52 > 7 the spectrum is mostly inharmonic:
   the spectral oscillator only captures the partials that are multiples of 2^(P52−7) (log the fraction of lost energy, §1.9);
   for a random Seed 2, log that the timbre changes on every note.
   - Decay: `τ_h` of §1.7 is per harmonic; if LYNX only has one envelope, the A²-weighted mean is an
     **inference** (not from FL) — keep the current envelope and log it.
2. **Harmless (`synthFromHarmless`):** multiply each harmonic by **`E(h) = Σ g_k·W_k(h)`** (§2.1/§2.2, g from
   §2.2); apply the **LHP** to harmonics 1–4 after the mask (`m + (1 − m)·p·w`); sub levels per §2.3.
   Sub 3/Sub 4 (1.5f/2.5f) do not fit in the note's series; **option** (representation inference, not from FL): play the
   spectral oscillator one octave lower with `spectrum[2n] = A(n)`, `spectrum[1] = Sub1`, `[3] = Sub3`, `[5] = Sub4`
   (valid only with p39 = 0, up to 128 harmonics).
3. **Harmor (`synthFromHarmor`, unison block):** replace `±spread` with `d_k·s₃₈` from table §3.2 (type p35;
   Blurred is the dominant case) using the voices with the largest |d_k| and the centre when there are 3 oscillators;
   pan `(2k/(n−1) − 1)·pan₃₇` (with `pan₃₇` per the law of §3.1, not `p37/256`), per-voice gain
   `1 − (3^(1−1/n) − 1)/2`; p35 = 3/4 → log it (random).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Ogun `FUN_008424b0` (order P52+q, `(2^(order−1) >> q) − 1`), `FUN_00617260` (FFT R 32f, flag 8) | FFT size and number of partials | DECOMP |
| Ogun `FUN_00842590` (`0x8fc038`·2^q/fs), `FUN_00844bc0` (440, 75, ln2/12), `FUN_00844660`, `FUN_00842c60`, `FUN_00611750`, *note-on* (c/100 + 60, 1/1200, ln 2) | partial frequency | DECOMP |
| Ogun `FUN_008426e0` (RandUniform 0…6.2831855, seed 3, 1/√M, SinCos) | fixed phases | DECOMP |
| IPP `0x1815ed2e0` (69069, 0x3C6EF373, 0x436CBAE9, 18, 2⁻³², 0.5) + `tmp/r24/run/rnd.exe` | uniform generator | DECOMP + EXEC |
| Ogun `FUN_008684e0` cases 5–11 (0.001, 1e−4; mix 0x20CF/0x1CFC/0x1EEF) and *note-on* (0x1F88/0x1E42, 0x200E/0x1DBC, counter `+0x244`) | seeds | DECOMP |
| Ogun `FUN_00843680` (+ `0x844618` 0.025; `0x844630` 0.001; `0x84463c` 0.001; `0x844640` 0.90703; ln 0.01), `FUN_00843590`, `FUN_00843470` (10000001, 0.01) , `FUN_00842ee0` | B, D, normalisation, decay, band limit | DECOMP |
| Ogun `FUN_00842960`, `FUN_008428c0`/`FUN_00842820` (0.5) | maps, partial window, optional normalisation | DECOMP |
| Ogun `FUN_0086e6c0` (0, 1, 2, ln 3, 0.5, patterns), `FUN_00866da0` (32767; log₂, 15), `FUN_008669f0` (1.5M, 1/√h) | map sampling | DECOMP |
| Ogun `FUN_00864d00`, `FUN_00781bd0`, `FUN_00785940` (1000001), `FUN_00788a20`, `FUN_00787170` | envelope format and evaluation | DECOMP |
| Ogun `FUN_00866840`, `FUN_0086e620` | quality → q, hop | DECOMP |
| Ogun `FUN_00843220`, `FUN_00842e90` | initial read position (min. |x|²) | DECOMP |
| 20 Ogun channels (`og.py`, `env.py`, `ogspec.py`, `oglynx.py`) | flags, 82 envelopes, example spectra | DATA |
| Ogun manual | richness, fullness, seeds, pre-decay, maps, ALT DECAY | DOC `…/plugins/Ogun_synthesis.htm` |
| Harmless `FUN_0077b450`/`FUN_0077b2d0` (0…5, 1, −1, π, 0.5; log₅; ^5), `FUN_00787b40`, `FUN_00784120`, `FUN_00784110`, `FUN_00787990` (0.5/1.5), `FUN_0077f630` cases 1 and 5–10, `FUN_00614c80` | EQ, LHP, subs | DECOMP |
| 139 Harmless channels (`tmp/r21/py/hl.py`) | use of EQ/LHP/subs | DATA |
| Harmless manual | floating EQ, LHP, subs | DOC `…/plugins/Harmless.htm` |
| Harmor `FUN_00873e40` (table `0x928290`; 2, 1, 0.25, 0.5), `FUN_00879540` cases 34–39 (1/n, 3, 0.5, 1/192, 0.046875, 41, 1.875, 0.001), `FUN_00883cd0`, `FUN_008859a0`, `FUN_0088ab10` | unison | DECOMP |
| Harmor DFM (`FatOrderSelect`, `FatTypeSelect`, `FatInvBox`, `FatPanWheel`, `FatPitchWheel`, `FatPhaseWheel`) | names and ranges | DATA (DFM) |
| 198 Harmor parts (`tmp/r23/py/hm.py`) | distribution of types | DATA |
| Harmor manual | order, thickness, pan, phase, alternating polarity | DOC `…/plugins/Harmor.htm` |

### Open questions

- Ogun: mode field `+8` of the articulators (key/velocity/X/Y combination) and per-articulator centres
  (the 0.5 of the bipolar ones is an inference from the data); formulas of envelope modes 1 and 10; frame-by-frame mix of
  ALT DECAY; exact role of the interpolator (order by quality) and of the *crossfade* between frames; initial value of the
  Seed 2 counter (runtime global); absolute output scale (master volume and unison from round 21 on the
  table); auditory confirmation of `f_h = h·f·2^(7−P52)`.
- Harmless: parity of colours 3–10 at a fractional index (subs 3/4); confirmation that the windows do not follow
  p39.
- Harmor: use of the random table of the Hz type; unit of the unison phase and the "product = 1" branch; exact role of the
  *Harmonic unison pitch* curve on `d_k·s₃₈`; the manual lists the distributions in an order different from that of the
  DFM combo (the code order is the DFM's).
