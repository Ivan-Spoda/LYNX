# Behavioural analysis of FL Studio 2026: round 23 (REA) — exact static spectra: Harmor part timbres and Sytrus operator waveform

> **Nature of this document.** Continuation of `REA_rodada11.md` (Sytrus and Harmor states; description of the
> operator "Shape" in §1.4), `REA_rodada18.md` §1 and `REA_rodada19.md` §2 (Harmor). *Clean-room* reverse
> engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here
> are formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve
> only to trace the evidence. No encrypted content was touched (no FLEX pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where there
> was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> or from signal theory (e.g.: "the Fourier series of a ±1 saw has harmonic n of amplitude 2/(πn)") are
> made explicit as **inference**.

- **Targets:**

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Sytrus | `Sytrus_x64.dll` `d52a97c55e36c2457305ce4f2d6eed08aabd12dbec02c9f5f55ce8788e944f15` (= round 11, checked against the installation) | Delphi (`TTestPlug`) | operator wavetable generator `FUN_00847df0`, base shape `FUN_00847be0`, pre-filter `FUN_00847900`, register copy `FUN_008477f0`, shape *cache* `FUN_00848780`, table size `FUN_00848a50`/`FUN_00848980`, tension curve `FUN_0060ec90`, pre-filter conversion `FUN_00869c90`, state `FUN_00862d70`, IPP dispatcher `FUN_005bc580` |
  | Harmor | `Harmor_x64.dll` `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` (= round 11/18, checked) | Delphi (`TTestPlug`) | timbre construction `FUN_0088f310`, T1/T2 mix `FUN_00885b70` + `FUN_00885af0`, local EQ `FUN_00889ec0`, log-axis table `FUN_00888ed0`, partial ratios `FUN_0088fcf0`, processing order `FUN_008737e0`, timbre *scope* `FUN_0088f880`, random timbre `FUN_00896c60`, part constructor `FUN_00870750`, filter shapes `FUN_00874ed0`, per-voice filter `FUN_00886eb0`, table sampling `FUN_00874930`, editor *hint* `FUN_008726e0`, `ProcessParam` `FUN_00879540`, voice `FUN_0088ab10`, oscillator bank `FUN_00784840`/`FUN_00783ba0`; generic envelope (`TMEnvelope`, VMT `0x79a0d8`): table render `FUN_007a2640`/`FUN_007a26f0`, step `FUN_007a1930`, point value `FUN_0079fef0`, per-point coefficients `FUN_0079e680`, number of waves `FUN_007a0070`, load `FUN_0079a810`, constants `FUN_00630170`, IPP dispatcher `FUN_005bd1c0` |

- **Tools:** Ghidra 12.1.4 *headless* on read-only copies of the round 11 projects (`/tmp/r23gh` and
  `/tmp/r23gh2`, copied from `/tmp/r11gh_copy`/`_copy2`; program `hm` = Harmor, `sy` = Sytrus, hashes above);
  scripts in `tmp/r23/scripts/` (those of rounds 18/19 + `DGR.java`, search in the decompiled output restricted to an
  address range); `tmp/r18/k.py` for constants; *dumps* in `tmp/r23/out/`; our own numerical models in pure Python
  (`tmp/r23/py/sywave.py`, `hspec.py`) and data scripts (`sy.py`, `hm.py`, `t2.py`…`t9.py`) over
  `tmp/r14/blobs14.pkl`. Official manual read via web.
- **Date:** 2026-10-13.
- **Dataset** (channels in `Demo projects/**` of the round 14 *pickle*): **99 Harmor channels** (20 files,
  93 distinct *blobs*, **198 parts**; state versions v18 = 53, v19 = 44, v16 = 2) and **265 Sytrus channels**
  (1590 operators). The count "36 channels" cited in the request was not reproduced with this slice; the numbers below
  are from this slice.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated as `…/plugins/`). The manual quotes came from a summarised web reading; they must be rechecked on the
page before becoming interface text.

**Reading conventions.** Library routines (identical in both DLLs): `FUN_0040c090` = ln, `FUN_0040ad30` = exp,
`FUN_0040bb50` = sine, `FUN_0040ba60` = cosine (same quadrant structure as the sine, kernel swapped —
**inference** from the code), `FUN_0040c520` = round to nearest integer **[DECOMP]**.
Indirect calls go to the Intel IPP library; the names come from the dispatchers, which pair each pointer with
the function name *string* (Sytrus `FUN_005bc580`, Harmor `FUN_005bd1c0`) **[DECOMP]**: e.g. Sytrus
`0x90d640` = `ippsTone_Direct_32f`, `0x90d3f0` = `ippsAbs_32f_I`, `0x90d0c8` = `ippsMulC_32f_I`; Harmor
`0x93a2c0` = `ippsSqr_32f_I`, `0x93a0e8` = `ippsAddC_64f_I`, `0x93a090` = `ippsMul_32f_I`, `0x93a070` =
`ippsSub_32f`, `0x93a6a8` = `ippsThreshold_LT_32f_I`, `0x93a278` = `ippsSin_64f_A26`. Harmor *runtime* constants
(`FUN_00630170`): `0x93b4f0` = ln 2, `0x93b4f8` = 1/ln 2, `0x93b53c` = ln 10 / 20 **[DECOMP]**.

**LYNX convention** (`src/core/Song.h`, `src/core/Instruments.cpp`): the "spectral" oscillator sums
`(2/π)·spectrum[h]·sin(2π(h·t + spectrumPhase[h]))`, h = 1…256; hence `spectrum[h] = 1/h`, phase 0 is the ±1 saw.
In this document **A(n)** is always the **physical** amplitude of the sine of harmonic n in the plugin; for LYNX,
`spectrum[n] = A(n)·π/2` reproduces the same level, and `spectrum[n] = A(n)` reproduces the same shape with level ×2/π.

---

## 1. Harmor: static spectrum of a part

**What LYNX assumes** (`src/core/FlSytrus.cpp`, `synthFromHarmor`): each part = `saw` oscillator; level =
`g(|p11|)·g(p12)·mix A/B`; cutoff/envelope/unison as in rounds 18/19. The drawn timbres, the per-harmonic phase,
the T1/T2 mix and the local EQ are ignored.

### 1.1 Where the curves live in the state **[DECOMP `FUN_0086ed30`, `FUN_00870750` + DATA]**

Layout of the part block (complements round 11 §4.1; all items in sequence, without fixed offsets because the
articulators have variable size):

| Order | Content | Use in this round |
|---|---|---|
| 1 | 386 × i32 parameters (index = DFM Tag) | p4 *Timbre 1 & 2 mix* (0…1000), p5/p6/p7 *Sub-harmonic 1/3/4* (0…2048), p42/p43 *Harmonic detuning multiplicator/divider* |
| 2 | **0x30 B = 12 × i32: processing order** → part `+0xA6C` | 192/198 parts = `0,1,…,11` (identity) **[DATA]** |
| 3 | 0x4B B of combos → part (`+0x60`, `+0x668`, `+0x66C`, `+0x100`, `+0x104`, `+0xC0`, …) | **misc+0x00 = timbre blending mode** (`+0x60`); **misc+0x1C = partials mode** (`+0xE0`); misc+0x34/+0x38 = filter types 1/2 (round 18) |
| 4 | 43 articulators (11 parts each) | articulator 3 = *Timbre 1 & 2 mix* |
| 5 | **26 "shaping" envelopes** (round 11 §1.6 format, `kind` = 3) | in memory: envelope of index `0x2B + s` at part `+0x670 + 8·(0x2B + s)` |

Order of the 26 *shaping* envelopes (DFM `ArtSelect` combo, without separators) **[DFM + DECOMP]**:
**s = 0 Local EQ**, **1 Harmonic phase**, **2 Timbre 1 harmonic level**, **3 Timbre 2 harmonic level**,
4 Harmonic randomness, 5 Harmonic protection, 6 Harmonic clipping, 7 Harmonic prism, 8 Harmonic unison pitch,
9 Image formant mutation, 10/11 Filter shape 1/2, 12/13 Resonance shape 1/2, 14 Pluck shape, 15 Phaser shape,
16 Harmonizer distribution, **17 Global EQ**, 18–25 masks. Check in the code: the timbre routine reads
envelope `0x2D + t` (t = 0/1) and the phase routine reads `0x2C`; the local EQ reads `0x2B`; the global EQ (shape code 300) reads
`0x3C` **[DECOMP `FUN_0088f310`, `FUN_00889ec0`, `FUN_00874ed0`]**.

Points of each envelope (file): `f64 Δx`, `f64 y`, `f32 tension`, `u8 mode`, 2 B, `i8 sign` (the last byte is
recomputed at load as the sign of the tension) **[DECOMP `FUN_0079a810`, `FUN_0079e680`]**.

**Default values** (constructor `FUN_00870750`): Local EQ, T1, T2 and Global EQ = 0.25; Harmonic phase and
randomness = 0.5; others = 1.0; the default T2 is loaded from a system envelope file
(`System\….fnv`) **[DECOMP]**. In the data: T1 = flat line 0.25 in 170/198 parts; Phase = flat line 0.5 in 171; Local EQ =
flat line 0.25 in 194; T2 = (0; 0.2685) → (1; 0) with mode 6 and tension −0.164 in 195 **[DATA]**.

**Envelope centre.** The value used is `y − c`. At construction, `c = 0.5` for the envelopes whose bit is set in
mask `0x19490` (index − 0x28): bits 4, 7, 10, 12, 15, 16 → **Harmonic phase (0x2C) has c = 0.5**; T1, T2,
Local EQ and Global EQ have **c = 0** **[DECOMP `FUN_00870750`, `FUN_0079a7c0`, `FUN_0079fef0`]**.

### 1.2 Curve evaluation (common to all *shaping* curves) **[DECOMP `FUN_007a2640`, `FUN_007a1930`, `FUN_0079e680`, `FUN_007a0070`]**

- The total length (sum of the Δx) is normalised to the envelope length (1.0 in the data) **[DECOMP
  `FUN_0079db40`; DATA: Σ Δx = 1.0]**.
- In the segment from point j−1 to point j, with `t ∈ [0, 1)` the relative position, the value is
  **`y_{j−1} + f(t)·(y_j − y_{j−1})`**, where `f` is given by the **mode and the tension T of the arrival point j**:

| Mode | f(t) |
|---|---|
| 0 (single curve), T = 0 | `t` (linear) |
| 0, T > 0 | `1 − ((k+1)^(1−t) − 1)/k`, with **`k = 1000001^|T| − 1`** |
| 0, T < 0 | `((k+1)^t − 1)/k`, same k |
| 2 (hold) | 0 (holds `y_{j−1}`) |
| 5 (pulse) | `round(t·2(N+1) − σ/2) mod 2`, σ = sign of T |
| 6 (wave), T < 0 | **`(sin(2π(t·(N + ½) + ¾)) + 1)/2`** |
| 6 (wave), T ≥ 0 | triangle wave `|2·frac' − 1|` with `N + ½` periods (if N = 0 and T > 0: linear) |
| 7 | `t − (u⁶ − u)·T`, u = t (T ≤ 0) or 1 − t (T > 0) |

  with **`N = round((1000001^(1 − 0.6|T|) − 1)·0.001)`** for modes 3–6 (+1 in mode 3 with T ≥ 0). Example of the
  default T2: T = −0.164 → N = 257 **[DECOMP + calculation]**. Modes 1, 3, 4, 8–12 exist in the code but do not appear
  in the timbre/phase curves of the demos (count of point modes: 0 in 20,388 segments, 6 in 200, 7 in 2,
  5 in 1) **[DATA]**; their formulas were not transcribed here.

### 1.3 Timbres 1 and 2 → per-harmonic level **[DECOMP `FUN_0088f310`; DOC]**

- The curve is sampled at **516 equally spaced points**: harmonic **n (1…516) sits at `x = (n − 1)/515`**
  (linear axis). The editor shows the same axis: "Harmonic %d" with `n = round(515·x) + 1` **[DECOMP `FUN_0088f310`,
  `FUN_008726e0`]**. Curves drawn from loaded waves have exactly one point per harmonic (516
  points spaced 1/515 apart in 8 T1 parts) **[DATA]**.
- Level law: **`G_t(n) = (4·y_t(n))²`** (y = 0.25 → 1 = 0 dB; y = 1 → 16 = +24.1 dB; y = 0 → −∞). The editor
  shows the same value in dB with `20·log10((4y)²)` **[DECOMP `FUN_0088f310` (factor 4.0 squared),
  `FUN_008726e0` + `FUN_0062e320`]**.
- The table has 4 special positions before the harmonics (indices 0–3; harmonic n sits at index n + 3): index 1
  repeats the value of harmonic 1, indices 2 and 3 are the curve read at the fractional positions "harmonic 1.5" and
  "2.5" (*Around fundamental* setting) **[DECOMP]**. These are the subs, multiplied by `p5/1024`, `p6/1024`,
  `p7/1024` (`ProcessParam` cases 5–7, factor 1/1024) **[DECOMP `FUN_00879540`, `FUN_00885b70` → `FUN_00882670`]**;
  sub frequencies: 0.5·f, (1 + 0.5d)·f, (1 + 1.5d)·f (or 0.25·f and 0.125·f in *Below fundamental*)
  **[DECOMP `FUN_0088fcf0`, `FUN_00870750`]**. In the data p5/p6/p7 ≠ 0 in only 5/2/1 parts **[DATA]**.
- **Blending mode** (misc+0x00; combo *Timbre blending mode*): before the *crossfade*, T2 is replaced by
  1 Subtract → `max(G₁ − G₂, 0)`; 2 Multiply → `G₁·G₂`; 3 Maximum; 4 Minimum (functions `FUN_00882f60`/`FUN_00882f90`,
  names from the combo); 5 Pluck → T2 multiplied by another table and the mix forced to 0 (detail **not
  confirmed**); 0 Fade → no change **[DECOMP `FUN_0088f310`, `FUN_00885b70`]**. In the data: 0 in 198/198
  **[DATA]**.
- **Mix** (linear *crossfade* on the gains): **`G(n) = (1 − m)·G₁(n) + m·G₂(n)`**, with
  **`m = min(1, (p4/1000)·A₃)`**, A₃ = output of articulator 3 (*Timbre 1 & 2 mix*; base 1, multiplicative); `m ≤ 0`
  → T1 only, `m = 1` → T2 only **[DECOMP `FUN_00885b70`, `FUN_00885af0`; `ProcessParam` case 4 = `p4·0.001`]**.
  Manual: "Standard cross-fade from Timbre 1 to Timbre 2" **[DOC `…/plugins/Harmor.htm`]**. Data: p4 = 0 in
  167 parts, 1000 in 14, intermediate in 17 **[DATA]**.

### 1.4 Local EQ: here is the 1/n "Brownian calibration" **[DECOMP `FUN_00889ec0`, `FUN_00888ed0`, `FUN_008737e0`; DOC]**

- The *Local EQ* curve is sampled on a **logarithmic axis**: harmonic n sits at **`x = log2(n)/9`** (9 octaves
  over the curve length) **[DECOMP `FUN_00888ed0`: position = ln(n)·(1/ln 2)·L/9]**.
- With **misc+0x1C ≥ 1** (196/198 parts): **`E(n) = (4·y_L(log2(n)/9))² / n`**; the special positions use
  divisors 1.5 and 2.5. With misc+0x1C = 0 (2 parts): `E(n) = y_L²` without division **[DECOMP]**.
- The local EQ is module 0 of the processing chain (`+0xA6C`); when it is first in the chain (192/198
  parts) it is multiplied directly into the timbre table; otherwise it is applied at its position in the chain (`FUN_00886E00`
  multiplies by the same table) — in both cases the per-harmonic gain is the same **[DECOMP `FUN_008737e0`,
  `FUN_0088f310`, `FUN_00886e00`]**.
- Hence, with the default Local EQ (0.25): `E(n) = 1/n` and **a flat timbre line generates the saw**, as the manual says:
  "Envelope values are scaled relative to the slope of the Brownian noise spectrum" and "Timbre 1 and Timbre 2
  generate a Saw and Square wave respectively" **[DOC `…/plugins/Harmor.htm`]**. Indirect check: the
  "random timbre" function normalises the energy by `Σ (g/n)²` and writes `y = √g/4`, i.e. it treats the physical amplitude as
  `g/n` **[DECOMP `FUN_00896c60`]**.
- misc+0x1C also selects the partial ratios (§1.5); its name in the interface **was not identified**.

### 1.5 Partial frequencies **[DECOMP `FUN_0088fcf0`, `ProcessParam` cases 42/43]**

- `d = p42/(p43·10000)` (default 10000/1 → 1.0; 196/198 parts) **[DECOMP + DATA]**.
- misc+0x1C ≥ 1: **`r(n) = 1 + (n − 1)·d`** (exact harmonics with d = 1; "detuning" stretching the series as in
  Harmless). misc+0x1C = 0: `r(n) = 2^((n−1)·d/8)` (partials spaced 1/8 octave apart) **[DECOMP]**.
- The number of partials per voice is limited by a maximum frequency (`FUN_0088fbc0`); the value of this limit **was not
  tabulated** (for 256 harmonics in LYNX it only matters for high notes).

### 1.6 Per-harmonic phase **[DECOMP `FUN_0088f310`, `FUN_0088ab10`, `FUN_00784840`, `FUN_00783ba0`; DOC]**

- The *Harmonic phase* curve is sampled on the same linear axis (`x = (n − 1)/515`), minus the centre 0.5, plus 1.0
  (`ippsAddC_64f_I`), and copied as the initial phase (in cycles) of each oscillator of the voice **[DECOMP]**.
- The oscillator bank sums `A·sin(2π·frac(phase))` with the phase advancing `f/fs` per sample (`ippsSin_64f_A26`
  over `2π·(phase − round(phase))`) → **unit = cycles, sine convention** **[DECOMP `FUN_00783ba0`, `FUN_00784840`]**.
- Therefore **`φ(n) = (y_φ((n − 1)/515) − 0.5) mod 1`** cycles, shared by both timbres ("they share the
  same harmonic phase information" **[DOC `…/plugins/Harmor.htm`]**). Default (0.5) → φ = 0.
- There is a branch that adds random phase (`ippsRandUniform_Direct_64f`) at note start; the parameter that enables it
  **was not identified** (not confirmed).

### 1.7 Final formula (static case, no modulation, no image)

For harmonic n = 1…256 of a part (before part volume/volume articulator of rounds 18/19):

**`A(n) = [(1 − m)·(4y₁(xₙ))² + m·(4y₂(xₙ))²] · (4y_L(log2(n)/9))² / n · EQg(fₙ) · F(fₙ)`**,
**`φ(n) = (y_φ(xₙ) − 0.5) mod 1`**, with `xₙ = (n − 1)/515`, misc+0x1C = 1 and blending mode 0.

`EQg` = global EQ (envelope 0x3C; table by absolute frequency, default 0.25 → gain `(4·0.25)² = 1` across the useful
range; the exact mapping in Hz and the ramps at the ends **were not transcribed**) **[DECOMP `FUN_00874ed0` code
300]**; `F` = filters (§1.8). Non-linear or time-dependent modules (clipping, blur, prism, harmonizer,
phaser, pluck, image) are left out — demo values in §1.9.

**Check against the data** (model `tmp/r23/py/hspec.py`; the values are A(n) before filters/volume) **[DATA +
formula above]**:

| Part (file · channel · part) | Curves | A(1…8) | φ(1…4) |
|---|---|---|---|
| Astes – Bien Duro · 4.6 Lead · 0 (138 parts equal to this case) | default, m = 0 | 1; 0.5; 0.3333; 0.25; 0.2; 0.1667; 0.1429; 0.125 (= 1/n, saw) | 0; 0; 0; 0 |
| Right Night feeling · Harmor Bell · 1 | default, p4 = 1000 (T2 only) | 1.1535; 0; 0.3845; 0; 0.2307; 0; 0.1648; 0 (square: odd `1.1535/n`) | 0… |
| Bien Duro · 4.4 Glass pluck · 0 | default T, drawn phase, p4 = 332 | 1.051; 0.334; 0.3503; 0.167; 0.2102; 0.1113; 0.1501; 0.0835 | 0.734; 0.723; 0.975; 0.986 |
| Bien Duro · 4.5.2 Wasp high · 0 | T1 and phase drawn (516 points), m = 0 | 0.9888; 0.552; 0.3607; 0.255; 0.1965; 0.1544; 0.13; 0.1118 | 0.968; 0.938; 0.043; 0.179 |
| Bien Duro · 4.6 Lead · 1 | T1 drawn (y₁(1) = 0.5) | 4.0; 1.9652; 1.2871; 0.9483; 0.7451; 0.6097; 0.5131; 0.4407 | 0… |

Classification of the 198 parts: default T1/phase/Local EQ with m = 0 in **138**; T1 drawn in 28; phase drawn in
27; Local EQ drawn in 4; p4 > 0 in 31 **[DATA `tmp/r23/py/t9.py`]**.

**Inference (Fourier):** since `Σ sin(nx)/n` has peak π/2 and `Σ_odd sin(nx)/n` has peak π/4, the default saw
(`1/n`) has peak ≈ 1.571 and the default square (odd `1.1535/n`) has peak `1.1535·π/4 ≈ 0.906` — the levels of the
two default shapes are not equal.

### 1.8 Shapes of filters 1/2 per harmonic (static case) **[DECOMP `FUN_00874ed0`, `FUN_00874930`, `FUN_00886eb0`, `FUN_00874240`, `FUN_00874d60`, `FUN_00874d30`]**

- Each shape is a table `T[i]`, i = 0…N−1, with **origin position `o`** and extensions `(eL, eR)` (used at the margins
  of the cutoff band, round 18 §1.2). The gain of the partial of frequency `fₙ` is
  **`F(fₙ) = T(o + (log2 fₙ − L)·s)`**, with linear interpolation between entries, `T[0]` below 0 and `T[N−1]` beyond
  the end; **`s = 1024·2^(1 − 0.004·p54·A_w)`** entries per octave (p66 in filter 2; A_w = articulator
  6/7 *Filter width*, base 1); L = log2 of the voice cutoff (round 18 §1.2 and round 19 §2.1). With p54 = 500 (155/198
  parts): s = 512/octave **[DECOMP]**. Inference: `s = 682.67/W` with the W of round 18.
- Tables (x = i/(N−1); "dB" via `ln 10/20`):

  | Type | N | T[i] | o | (eL, eR) |
  |---|---|---|---|---|
  | 0 Classic LP | 2048 | `10^(−72x/20)·(1 − x¹⁶)` | 0 | (1, 0) |
  | 1 Classic BP | 4096 | the same law with `x = 1 − |1 − 2·((i + N/2) mod N)/N|` (peak at the centre) | (N−1)/2 | (1, 1) |
  | 2 Classic BS | 4096 | `1 −` (BP law) | (N−1)/2 | (1, 1) |
  | 3 Classic HP | 2048 | LP law with `x → 1 − x` | N−1 | (0, 1) |
  | 4 Crude LP | 1024 | **`cos⁴(πx/2)`** | 0 | (1, 0) |
  | 5 Crude BP | 2048 | `sin⁴(πx)` | (N−1)/2 | (1, 1) |
  | 6 Crude BS | 2048 | `cos⁴(πx)` | (N−1)/2 | (1, 1) |
  | 7 Crude HP | 1024 | `sin⁴(πx/2)` | N−1 | (0, 1) |

  (Shapes 4–7 use the library cosine — see conventions.) Shapes 8–18 (Hill, Leg, Hybrid, Snake, Fin,
  Leaky, phasers, Flat BP, Custom) **were not transcribed**. The tables go through a zero cut
  (`FUN_00629eb0`) and a removal of constant stretches at the ends that only adjusts `o` (`FUN_00874e40`) **[DECOMP]**.
- **Note dependence:** `log2 fₙ − L = log2 n + log2 f₀ − L₀ − KT·(log2 f₀ − log2 130.81)`. Only with **KT = 1** does the
  gain become a function of n alone; in the data **p55 (KT) = 0 in 182/198 parts** (KT = 1 in 7) **[DATA]** — the
  Harmor filter in the demos is fixed in Hz and **cannot** be baked into a static per-channel spectrum.
- The resonance multiplies by `2^(r·s(x))` (round 19 §2.2, **inference** about the `exp`) and does not enter the static
  case with p57 = 0.

### 1.9 Unison, *prism*, *blur* and others in the demos **[DFM + DATA; laws not confirmed]**

| Parameter (Tag) | Default (DFM) | Demos (198 parts) | Effect on the static spectrum |
|---|---|---|---|
| p34 *Unison order* | 1 | 1 in 120; 2 in 31; 3 in 15; 9 in 11… | detuned copies (round 18 §1.3); each copy has the same A(n) |
| p26 *Prism amount* | 0 | 0 in 175 | shifts the partial frequencies; with 0, no effect (**inference** from the name/curve); law **not confirmed** |
| p23 *Harmonic blur amount*; p21/p22 *Time blur* | 0 | p23 = 0 in 189; p21 = 0 in 197; p22 = 0 in 198 | law **not confirmed** |
| p27 *Harmonizer mix* | 0 | 0 in 161 | **not confirmed** |
| p75 *Pluck decay* | — | 1000 in 170 | round 11: 1000 = off |
| p8 *Harmonic protection*, p9 *Clipping threshold* | 256 / 1024 | p8 = 0 in 188; p9 = 1024 in 188 | `ProcessParam`: p8 → `(e^{c·v/1024} − 1)·c'`; effect **not confirmed** |
| image (resynthesis) | — | present in parts of "Square a Saw – … Resynthesized" (not counted) | multiplies A(n) by a column of the image that varies over time (`FUN_00882850`) — outside the static case |

---

## 2. Sytrus: operator waveform

**What LYNX assumes** (`synthFromSytrus` + `waveOf`): table with only the fundamental → `sine`/`triangle`/`saw`/`square`
by Shape ranges; rich table with default shape → "spectral" oscillator with `spectrum = |a|·π/2` and
`spectrumPhase = ph·0.5 (+0.5 if a < 0)`.

### 2.1 Where the fields live **[DECOMP `FUN_00862d70`, `FUN_008477f0`, `FUN_00847df0`]**

The operator block (0x828 B, round 11 §1.4) is read into the unit's register at `+0x40`; the shape generator copies
`+0x4C…` into its object. Mapping (block offsets):

| Block | Field | Object |
|---|---|---|
| 0x00C | 256 × f32 amplitudes a_k | +0x34 |
| 0x40C | 256 × f32 phases p_k | +0x434 |
| 0x80C…0x81C | Shape, Tension, Skew, Sine shaper, Pre-filter (i32) | +0x834…+0x844 |
| 0x820 / 0x821 / 0x822 | **Half / Even ("half silent") / Absolute** | +0x848 / +0x849 / +0x84A |
| — | global byte `P+0x4F6` copied to each operator at load | +0x84B (**Gibbs window**, §2.6) |

Bytes 0x823–0x827 of the block are not read by the shape generator **[DECOMP]** (their meaning remains
**not confirmed**). Control names: DFM `OpPanel` (`ShapeHalfBtn` "Half", `ShapeHalfSilentBtn` "Even",
`ShapeAbsBtn` "Absolute", `ShapeShapeWheel` 0…512, `ShapeTensionWheel` ±128, `ShapeSkewWheel` 0…128,
`ShapeSineWheel` 0…128, `ShapeFilterWheel` 0…128 default 128) **[STR]**. There is also `ShapeNoiseWheel` ("Noise"),
whose field **was not located** in the generator.

### 2.2 Table and steps **[DECOMP `FUN_00848a50`, `FUN_00848980`, `FUN_00847df0`]**

- The shape is computed in a table of **N = 2¹⁴ = 16,384 samples** per cycle (plus one guard sample) and stored
  in a shared *cache* **[DECOMP]**.
- Order: (1) sum of the partials with the base shape; (2) peak normalisation; (3) Tension; (4) Absolute;
  (5) Pre-filter; (6) zeroes values with |v| < 5.96·10⁻⁸ **[DECOMP]**.

### 2.3 The base shape B by Shape (`FUN_00847be0`) **[DECOMP + numerical verification]**

Writing θ ∈ [0, 1) for the phase in cycles and `x = Shape/128`:

| Range | m (weight of shape "w") | d (rise fraction) | t (trapezoid) | p (pulse) |
|---|---|---|---|---|
| x ≤ 1 | x | 0.5 | 0 | 0 |
| 1 < x ≤ 2 | 1 | (2 − x)/2 | 0 | 0 |
| 2 < x ≤ 3 | 1 | 0 | x − 2 | 0 |
| x > 3 | 1 | 0 | 1 | x − 3 |

- **p > 0 (pulse):** `B = +1` for `θ < D`, `−1` otherwise, **`D = (1 − 0.75p)/2`** (512 → 12.5 %).
- **p = 0:** `B = (1 − m)·sin(2πθ) + m·w(θ)`, with `u = (θ + d/2) mod 1` and
  `w = 2u/d − 1` if `u < d` (rise from −1 to +1), otherwise `w₀ = 1 − 2(u − d)/(1 − d)` (fall from +1 to −1);
  with t > 0 (only when d = 0): **`w = ±t + (1 − t)·w₀`**, sign + for `u < ½`, − for `u ≥ ½`.
  Cases: 0 = sine; 128 = triangle (zero at θ = 0, peak at ¼); 256 = **descending saw** `1 − 2θ`;
  384 = square (+1 in the first half).
- **Fourier series** (**inference**, checked against the numerical model of the code for Shape = 0, 64, 128, 160,
  192, 256, 300, 384, 448, 512 with error < 10⁻⁴ — `tmp/r23/py/test1.py`): `B = Σ_j b_j·sin(2πjθ)` with
  - sliding triangle (0 < d ≤ ½): `tri_j = 2·sin(πjd)/(π²j²·d(1 − d))`; d → 0: `2/(πj)` (saw);
  - square: `sq_j = 4/(πj)` on the odd ones;
  - **`b_j = m·[(1 − t)·tri_j + t·sq_j] + (1 − m)·[j = 1]`** (the sign of `b_j` becomes phase ½);
  - pulse: `B = (2D − 1) + Σ (2/(πj))·[(1 − cos 2πjD)·sin 2πjθ + sin 2πjD·cos 2πjθ]`.

### 2.4 Sum of partials, Half/Even, Skew and Sine shaper **[DECOMP `FUN_00847df0`]**

- **Normal:** `W(θ) = Σ_{k=1}^{256} a_k · S(B(c_k))`, with **`c_k = frac(k·θ + p_k)`** — the phase of the harmonic
  table is **in cycles of the harmonic itself** (the code uses period 2 and adds `2·p_k`). Partials with
  `a_k = 0` are skipped; negative a inverts.
- **Half:** each harmonic traverses only the **first half** of B stretched to a whole cycle:
  `B(c/2)` with `c = frac(k·θ + 2p_k)` (the phase becomes worth 2p_k cycles).
- **Even (half silent):** a whole cycle of B in the first half of the harmonic's cycle and **zero** in the second:
  `c = frac(k·θ + p_k/2)`; `B(2c)` if `c ≤ ½`, otherwise 0. **Half + Even:** `B(c')` with c' in the first half and zero in the
  second.
- **Skew** (0…128): `a_s = (21^(Skew/128) − 1)/20`, `k_s = 10⁹^(a_s) − 1`; the local phase `φ = 2c ∈ [0, 2]` is
  folded around the centre 1: `δ = |φ − 1|`, **`δ' = 1 − ((k_s + 1)^(1−δ) − 1)/k_s`**, `φ' = 1 ∓ δ'` (− for
  φ ≤ 1), and B is evaluated at φ'/2. Skew = 0 → no effect.
- **Sine shaper** (0…128): **`S(v) = sin(v · 1.5π · SineShaper/128)`** (constant 0.0368155 = 1.5π/128) applied
  to the value of each partial before multiplying by a_k; 0 → `S(v) = v`.
- With Shape = Skew = Sine = 0 and without Half/Even, the code uses a direct sine generator (`ippsTone_Direct_32f`, which is
  a cosine, with phase `(2p_k + 1.5)·π`) — **equivalent** to `sin(2π(kθ + p_k))` **[DECOMP + identity cos(x + 3π/2)
  = sin x]**. This confirms the unit of the phase.

### 2.5 Normalisation, Tension and Absolute **[DECOMP `FUN_00847df0`, `FUN_0062a570`, `FUN_0060ec90`]**

- **Peak:** `P = max|W|` (`FUN_0062a570`); if `P > 5.96·10⁻⁸`, **`W ← W/P`** — the operator wave always comes out with
  peak 1, whatever the sum of the amplitudes **[DECOMP]**.
- **Tension** (±128): `a_T = (21^(|T|/128) − 1)/20`, **`k_T = sign(T)·(K^(a_T) − 1)`** with K = 9.99999984·10¹⁷
  (≈ 10¹⁸, constant at `0x848758`); each sample becomes `sign(v)·C(|v|)` with
  `C(u) = 1 − ((k+1)^(1−u) − 1)/k` (k > 0) or `C(u) = ((|k|+1)^u − 1)/|k|` (k < 0) **[DECOMP]**.
- **Absolute:** `W ← |W|` (`ippsAbs_32f_I`), after the tension **[DECOMP]**.

### 2.6 Pre-filter and "Gibbs" **[DECOMP `FUN_00847900`, `FUN_00869c90`]**

- **`M = round((10^(10·(0.65 + 0.35·Pre/128)) − 1)·1.6384·10⁻⁶)`**: Pre = 0 → 5; 25 → 25; 64 → 291;
  96 → 2185; 116 → 7697; **≥ 117 → > 8192 = no filter** (default 128 → 16,384).
- If `M ≤ N/2`: real FFT of the table ("Pack" format, **inference** from the index pattern), **zeroes harmonics
  ≥ M** (keeps DC and 1…M−1) and, if the object byte `+0x84B` is set, multiplies harmonic j by
  **`sinc(j/(M − 1)) = sin(πj/(M−1))/(πj/(M−1))`** (Lanczos σ factors); inverse FFT **[DECOMP]**.
- `+0x84B` comes from the global byte `P+0x4F6` (written to the state from v13 onwards) **[DECOMP `FUN_00862d70`]**; the
  association of this byte with the global **GIBBS OFF** button ("Attenuate ringing ripples", DFM) is **inference** (the effect
  of the code is exactly to attenuate the Gibbs phenomenon). In the demos: 1 in 24/265 channels **[DATA]**.

### 2.7 Check against the data **[DATA + model `tmp/r23/py/sywave.py`]**

In the 1590 operators of the demos: Shape/Tension/Skew/Sine/Pre = (0,0,0,0,128) in 1235; Shape 256 in 95; 128 in 21;
384 in 11; Tension/Skew/Sine/Pre non-default in 138; Half in 3, Even in 3, Absolute in 5. Rich tables: 124
with default shape, 87 with non-default shape **[DATA `t2.py`]**. Stored phases: uniformly distributed in
[0, 1] (3314 zeros; maximum 1.0) — consistent with **cycles**, not "half cycle" **[DATA]**. Examples (A = amplitude
of the sine of each harmonic of the operator wave, already normalised; phase in cycles):

| Operator | Shape | A(1…6) | φ(1…3) |
|---|---|---|---|
| Steptime · Angela · op 2 | Pre = 25 (M = 25), Gibbs = 1, table 1; 0.395; 0.131… | 0.8532; 0.3342; 0.1095; 0.046; 0.0234; 0.0139 (harmonics ≥ 25 = 0) | 0.272; 0.045; 0.82 (= p_k) |
| With You · Synth Pad · op 6 | Shape 128 (triangle) over a₁ = 1, a₈ = 0.574… | 0.5608; 0; 0.0623; 0; 0.0224; 0.0015 | 0; ½; ½ |
| Exoplanet · Cello · op 1 | Tension 6, Sine 47, Even | 0.0689; 0.1421; 0.0938; 0.1477; 0.0668; 0.1617 | 0.553; 0.566; 0.574 |

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Harmor: where the curves are | 26 *shaping* envelopes at the end of the part block; s = 0 Local EQ, 1 Harmonic phase, 2/3 Timbre 1/2 level, 17 Global EQ; memory `+0x670 + 8(0x2B+s)` **[DECOMP+DFM]** |
| 1b | Harmor: x axis | timbres and phase: **harmonic n at x = (n−1)/515** (516 points); Local EQ: **x = log2(n)/9** **[DECOMP]** |
| 1c | Harmor: level law | **`(4y)²`** (0.25 = 0 dB); local EQ `(4y_L)²/n` = Brownian calibration; flat → saw **[DECOMP+DOC]** |
| 1d | Harmor: mix | `G = (1−m)G₁ + mG₂`, `m = min(1, p4/1000·A₃)`; Subtract/Multiply/Max/Min modes alter G₂ beforehand; 198/198 = Fade **[DECOMP+DATA]** |
| 1e | Harmor: phase | `φ(n) = (y_φ − 0.5) mod 1` cycles, sine convention, common to both timbres **[DECOMP+DOC]** |
| 1f | Harmor: curves | interpolation by the mode/tension of the arrival point; tension `k = 1000001^|T| − 1`; mode 6 with `N + ½` waves, `N = round((1000001^(1−0.6|T|) − 1)/1000)` → default T2 = exact square (N = 257) **[DECOMP]** |
| 1g | Harmor: partials | `r(n) = 1 + (n−1)·p42/(p43·10⁴)` (misc+0x1C = 1; 196/198) **[DECOMP+DATA]** |
| 1h | Harmor: filters | `F = T(o + (log2 f − L)·1024·2^(1−0.004 p54))`; tables 0–7 transcribed (Classic: −72 dB·x·(1−x¹⁶); Crude: cos⁴/sin⁴) **[DECOMP]**; KT = 0 in 182/198 → depend on the note **[DATA]**; shapes 8–18 **not confirmed** |
| 1i | Harmor: prism/blur/unison/harmonizer | neutral values in most demos **[DATA]**; laws **not confirmed** |
| 2a | Sytrus: base shape | table §2.3 (sine→triangle→sliding triangle→saw→trapezoid→square→pulse `D = (1−0.75p)/2`) + closed-form Fourier series **[DECOMP + verified inference]** |
| 2b | Sytrus: sum | `Σ a_k·S(B(frac(kθ + p_k)))`, Half/Even/Skew/Sine shaper as in §2.4; table of 16,384 samples **[DECOMP]** |
| 2c | Sytrus: post-sum | normalises peak to 1; Tension `K = 10¹⁸`; Abs; Pre-filter `M` and σ window (Gibbs) **[DECOMP]** |
| 2d | Sytrus: phase of the harmonic table | **cycles of the harmonic** (not half cycle); Half → 2p, Even → p/2 **[DECOMP+DATA]** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Sytrus (`synthFromSytrus`, "spectral" oscillator block, `FlSytrus.cpp` ~l. 525–543):**
   - **Phase:** replace `spectrumPhase = ph·0.5 (+0.5 if a < 0)` with **`spectrumPhase = ph (+0.5 if a < 0)`** (phase in
     cycles of the harmonic).
   - **Level:** the FL operator wave has peak 1. Compute `P = max_θ |Σ a_k·sin(2π(kθ + p_k))|` and use
     **`spectrum[k] = |a_k|/P · π/2`**.
   - **Generalise** the exact path to any shape: generate the 16,384-sample table with §2.3–§2.6 (base
     shape, Half/Even, Skew, Sine shaper, normalisation, Tension, Absolute, Pre-filter/Gibbs) and extract harmonics
     1…256 by DFT (amplitude `|c_k|`, phase `arg(c_k)/2π + ¼` for the LYNX sine convention); discard the DC.
     This also replaces the Shape ranges of `waveOf` for the "fundamental only" table (Shape 256 = exact descending
     saw, 384 = square, 448–512 = pulses 31.25 %…12.5 % with DC).
   - Also require Half/Even/Absolute = 0 in the current "default shape" shortcut (today only `form` is checked).
2. **Harmor (`synthFromHarmor`):** replace the saw with the "spectral" oscillator with
   **`A(n) = [(1−m)(4y₁(xₙ))² + m(4y₂(xₙ))²]·(4y_L(log2 n/9))²/n`**, **`φ(n) = (y_φ(xₙ) − 0.5) mod 1`**, `xₙ = (n−1)/515`,
   `m = min(1, p4/1000)` (articulator 3 ignored in the static case), evaluating the curves per §1.2 (modes 0, 2, 5, 6,
   7; log the others). `spectrum[n] = A(n)·π/2` keeps the physical level of Harmor; `spectrum[n] = A(n)`
   keeps the LYNX "saw = 1/n" convention (−3.9 dB).
   - Apply blending modes 1–4 (§1.3) before the *crossfade*; with mode 5, use only T1.
   - Only apply the spectral path when misc+0x1C = 1 and p42/p43 = 10000/1 (196/198); otherwise log it.
   - Keep the LYNX filter (KT = 0 in most): the static spectrum must **not** include F(fₙ). For KT = 1 (7
     parts), the filter shape can be baked in with §1.8 (types 0–7 only).
   - Subs (p5–p7) do not fit in the LYNX harmonic series (f/2, 1.5f, 2.5f); log when ≠ 0.
3. Keep: cutoff, cutoff envelope, part volume, unison (rounds 18/19).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Sytrus `FUN_00847df0` (constants `0x848700…0x848770`: 1, 2, 3, 21, 1, 0.05, 10⁹, 1.5π/128, 2, 1.5, π, ≈10¹⁸; `PTR 0x8ff8c0` = 1/128) | sum of partials, Half/Even, Skew, Sine shaper, peak, Tension, Abs | DECOMP |
| Sytrus `FUN_00847be0` (0, 1, 0.75, π, 2, 1) | base shape by Shape | DECOMP |
| Sytrus `FUN_00847900`, `FUN_00869c90` (0.35, 0.65, 10¹⁰, 1, 1.6384·10⁻⁶, π) | Pre-filter M and σ factors | DECOMP |
| Sytrus `FUN_008477f0`, `FUN_00862d70` (register `+0x40` = block; `+0x868` ← `P+0x4F6`) | block fields → object | DECOMP |
| Sytrus `FUN_00848a50`/`FUN_00848980` (order 14) | table of 16,384 samples | DECOMP |
| Sytrus `FUN_0060ec90`, `FUN_0062a570`, `FUN_00628bc0` | tension curve; peak; denormal cut | DECOMP |
| Sytrus `FUN_005bc580` | IPP names (`ippsTone_Direct_32f`, `ippsAbs_32f_I`, `ippsMulC_32f_I`…) | DECOMP |
| DFM Sytrus `OpPanel` | names/ranges Half, Even, Absolute, Shape, Tension, Skew, Sine, Pre-filter, Noise; GIBBS OFF | STR |
| 265 Sytrus channels (`tmp/r23/py/sy.py`, `t2.py`, `t3.py`) | distribution of shapes/flags/phases; example spectra | DATA |
| Model `tmp/r23/py/sywave.py` + `test1.py` | closed-form Fourier series = model of the code | verified inference |
| Harmor `FUN_0086ed30`, `FUN_00870750` (mask `0x19490`, defaults 0.25/0.5/1.0) | part layout, curve centres and defaults | DECOMP |
| Harmor `FUN_0088f310` (516 samples, 4.0², `ippsAddC_64f_I` 1.0, blending modes) | timbres and phase | DECOMP |
| Harmor `FUN_00885b70`, `FUN_00885af0`, `ProcessParam` case 4 (×0.001) | T1/T2 *crossfade* | DECOMP |
| Harmor `FUN_00889ec0`, `FUN_00888ed0` (9 octaves, 1/ln 2, division by n) | local EQ with 1/n | DECOMP |
| Harmor `FUN_008737e0`, `FUN_00886e00`, `FUN_0088f880` | processing chain; local EQ always applied; *scope* = T × local EQ | DECOMP |
| Harmor `FUN_0088fcf0`, `ProcessParam` cases 42/43 | partial ratios | DECOMP |
| Harmor `FUN_00896c60` | normalisation `Σ(g/n)²` and `y = √g/4` | DECOMP |
| Harmor `FUN_008726e0` (515, 4, 0.5), `FUN_0062e320` (20·log10) | axis and scale shown in the editor | DECOMP |
| Harmor `FUN_007a2640`, `FUN_007a1930`, `FUN_0079fef0`, `FUN_0079e680` (1000001, 0.5, 2π), `FUN_007a0070` (1000001, 0.6, 0.001), `FUN_0079a810`, `FUN_0079db40` | curve evaluation | DECOMP |
| Harmor `FUN_00784840`, `FUN_00783ba0` (2π), `FUN_00784740` | sine bank, phase in cycles | DECOMP |
| Harmor `FUN_00874ed0` (2048/4096/1024, −72, ln10/20, cos/sin), `FUN_00874d60`, `FUN_00874d30`, `FUN_00874930`, `FUN_00886eb0`, `FUN_00874240` (0.004, 2/3) | filter shapes and log-frequency sampling | DECOMP |
| Harmor `FUN_00630170` (ln 2, 1/ln 2, ln 10/20) | *runtime* constants | DECOMP |
| Harmor `FUN_005bd1c0` | IPP names | DECOMP |
| 198 Harmor parts (`tmp/r23/py/hm.py`, `t5.py`…`t9.py`, `hspec.py`) | defaults, distributions, example spectra | DATA |
| Harmor manual | saw/square by default, Brownian calibration, shared phase, *crossfade* | DOC `…/plugins/Harmor.htm` |

### Open questions

- Harmor: formulas of curve modes 1, 3, 4, 8–12 (not used in the timbre/phase curves of the demos); interface
  name of misc+0x1C; blending mode 5 (*Pluck*) in detail; Hz mapping and ramps of the global EQ
  (code 300); filter shapes 8–18; the parameter that enables random phase at note start; laws of *prism*,
  *blur*, *harmonic protection/clipping*, harmonizer and per-harmonic *unison pitch*; the maximum frequency that limits
  the number of partials; the special partial at index 0; contribution of the image (resynthesis) per part; absolute
  output calibration of Harmor relative to the other generators.
- Sytrus: field of the operator's *Noise* and bytes 0x823–0x827 of the block; confirmation in the interface that `P+0x4F6` is the
  GIBBS OFF button; real-time *band-limit* (BAND LIMIT button) and its effect on harmonics above N/2 in the
  16,384-sample table (the generator does not band-limit before the sum).
