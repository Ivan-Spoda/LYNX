# Behavioural analysis of FL Studio 2026: round 21 (REA) — the Harmless, Ogun and MiniSynth scales that LYNX uses

> **Nature of this document.** Continuation of `REA_rodada10.md` (Harmless, MiniSynth), `REA_rodada12.md` (Ogun)
> and `REA_rodada18.md` §1 (Harmor, same family as Harmless). *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode was copied: what is here are formulas and behaviours described in our
> own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted
> content was touched (no FLEX package was opened).
>
> **Rule of this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where there
> was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> or from signal theory (e.g.: "the ±1 saw has harmonic n of amplitude 2/(πn)") are made explicit as
> **inference**.

- **Targets:**

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Harmless | `Harmless_x64.dll` `4100bf85ad29dc665051a73ef62017272a25aefbda1cf2f1ace39e6be376440b` (= round 10) | Delphi (`TTestPlug`, VMT `0x7791d0`) | `ProcessParam` `FUN_0077f630`, voice/render `FUN_00784a10`, *note-on* (unison, envelope) block `0x783…`/`0x7840xx` of the same project, timbre colour `FUN_00787990` + `FUN_007873c0`, *detune* `FUN_00787ce0`, number of harmonics `FUN_00787c30`, application of colour/EQ `FUN_00787b40`, mask `FUN_00783f90`/`FUN_00783f00`, LHP `FUN_00784120`, subs `FUN_00784110`, filter range `FUN_0077c170`, filter shapes `FUN_0077ca30` (+ `FUN_0077c940`/`FUN_0077c970`/`FUN_0077c540`), per-rate *setup* `FUN_0077f170`, LFO base `FUN_0077f0c0`, tempo event `FUN_0077f5e0`, *dispatcher* `FUN_007793e0`, portamento `FUN_00784630`; library: envelope `FUN_005f5f60`/`FUN_005f5f30`/`FUN_005f5fe0`/`FUN_005f6120`, curve `FUN_005fd4d0`, constants `FUN_00614c80`, sample rate `FUN_00612f00` |
  | Ogun (and Autogun) | `Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= round 12) | Delphi (VMT `0x8621f8`) | `ProcessParam` `FUN_008684e0`, master `FUN_0086a570`, *note-on* `FUN_0086a8e0`, *release* `FUN_0086c050`, filter `FUN_00867cc0`, timbre engine `FUN_00842ee0`/`FUN_00843680`/`FUN_008447d0`, constants `FUN_0062dec0`/`FUN_00605d70` |
  | MiniSynth | `MiniSynth_x64.dll` `e00ca0194eccd4987f00a1976f3da3403c7ba78cfb5bf771f2fbf1ed0848ec85` (= round 10) | C++ (`CMiniSynth`) | parameter `set` `FUN_180023c20`, LFO `FUN_1800249b0`, render `FUN_180025a40`, tuning `FUN_180022250`, oscillators `FUN_180021d10`, filter `FUN_1800213e0`, envelopes `FUN_1800229c0`/`FUN_180022440` |

- **Tools:** Ghidra 12.1.4 *headless* (new: `/tmp/r21gh_Harmless`, `/tmp/r21gh_Mini`; reused for reading
  only: `/tmp/r12gh_Ogun_x64.dll`); scripts in `tmp/r18/gh/scripts/` (`DecompRange`, `Decomp`, `Xrefs`,
  `DecompGrep`); `tmp/r18/k.py` for constants; *dumps* in `tmp/r21/out/` (`h_range.c`, `o_range.c`, `m*.c`);
  data with `tmp/r21/py/hl.py`, `og.py`, `ms.py` over `tmp/r14/blobs14.pkl`; `objdump` (`tmp/r10/*.asm`) to
  check instructions. Official manual read via the web.
- **Date:** 2026-10-13.
- **Dataset:** channels in `Demo projects/**` of the round 14 *pickle*: **139 Harmless** (18 files;
  137 distinct *blobs*), **20 Ogun**, **10 MiniSynth**. (The count "56" cited in the request was not reproduced with
  this slice; the numbers below are from this slice.)

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated as `…/plugins/` below). The manual quotes came from summarised web reading; the quoted excerpts
are short and should be rechecked on the page before becoming interface text.

**Reading conventions** (apply to all three plugins): `exp`, `ln` and rounding were identified through the
library routines — in Harmless `FUN_00408930` = exp, `FUN_00409b50` = ln, `FUN_00409610` = sine,
`FUN_004088f0` = fractional part and **`FUN_00409fe0` = rounding to the nearest integer** (it is a single
`cvtsd2si` instruction, checked in `objdump`) **[DECOMP]**. The *runtime* constants were resolved through the
initialisers: in Harmless `0x7ff4e0` = ln 2, `0x7ff514` = ln 11, `0x7ff51c` = ln 3, `0x7ff52c` = ln 10 / 20,
`0x7ff500` = ln 2 / 12, `0x7ff530` = log₂ 440 (`FUN_00614c80`) and `0x7ff2b8` = **ln 0.01** (`FUN_005f6120`); in
Ogun `0x8f952c` = ln 3, `0x8f9508` = ln 2 / 1200 (`FUN_0062dec0`) and `0x8f92c8` = ln 0.01 (`FUN_00605d70`)
**[DECOMP]**.

---

## 1. Harmless (139 channels in the demos)

**What LYNX assumes** (`src/core/FlSynths.cpp`, `synthFromHarmless`): shape = saw (colours 5/6 → square,
7 → triangle; mask ≥ 0.5 → square; between 0.05 and 0.5 → saw `1 − m` + square `m`; colours 8–10 → three
saws ±7 ct); sub 1 = sine −1 octave with level `0.5·min(1, v/1024)`; unison ≥ 2 → 3 oscillators at 1/3 with
`±max(5, (41^(v67/256) − 1)·1.875)` ct and pan `±v66/128`; master tuning = p38/100 semitones; attack
`0.05·(101^x − 1)` s; decay `1.5·(0.11995·(1001^x − 1) + 0.05)` s (−40 → −60 dB conversion) or sustain 1 if 1000;
release `1.5·(0.0249·(101^x − 1) + 0.01)` s; cutoff `73.4·2^(7.855·v/8192)` Hz without *keytracking*; resonance
`0.9·x59 (+0.1 if p62 > 0)`; filter envelope = `x52·7.9` octaves (non-adaptive) or the remaining range
(adaptive), times `0.12·(1001^x − 1)` s (20 s if 1000), *sustain* 0; LFO with period `64/2^(|v89|/1000)`
beats, shapes 0/1/8 → sine, 3/6 → triangle, 2/4/5 → pulse; destinations pitch (p42, ct), cutoff (`6·x58` octaves,
**bipolar**) and volume (p25); portamento `(41^|x| − 1)/26.667` s; gain `0.75·(3^x − 1)/2 ÷ 0.75`.

### 1.1 Timbre colour (combo `HarmColSelect`, integer of state `0x1A4`) → harmonic amplitudes **[DECOMP `FUN_00787990`, `FUN_007873c0`]**

On every colour change the plugin recomputes a table of **528 partials**: 4 special partials (§1.3) followed
by harmonics `n = 1…524`. For each harmonic, `FUN_007873c0` returns an amplitude `A(n)` and a phase `φ(n)`
(phase stored in cycles — the value 0.25 appears as "a quarter"; the unit is **inference**). Writing
`n` = harmonic number (the code works with `i = n − 1`):

| Colour | Name (DFM, round 10) | Amplitude `A(n)` | Phase |
|---|---|---|---|
| 0 | Saw | `1/n` | 0 |
| 1 | Bright saw | `√0.5 · 2/(n + 1)` = `√2/(n + 1)` | 0 |
| 2 | Brighter saw | `2/(n + 3)` | 0 |
| 3 | Dephased saw | `1/n` | 0.25 on even harmonics |
| 4 | Dephased saw 2 | `1/n` | 0.5 when `n ≡ 2 (mod 4)` |
| 5 | Squaretooth | odd `1.1/n`; even `1.1/(n·(n − 1))` | even 0.25 |
| 6 | Squaretooth 2 | odd `1.1/n`; even `1.1/(n·(n − 1)^0.2)` | even 0.25 |
| 7 | Mole | `0.29167·(7^(1/n) − 1)` (n = 1 → 1.75; n = 2 → 0.48) | even 0.75 |
| 8 | Double saw | `0.5/n`, plus `1/(n + 1)` on the even ones | 0 |
| 9 | Triple saw | `1/(3n)`, plus `2/(3(n + 1))` on the even ones, plus `4/(3(n + 3))` when `n ≡ 0 (mod 4)` | 0 |
| 10 | Triple saw 2 | `0.5/n`, plus `0.5/(n + 1)` on the even ones, plus `0.5/(n + 3)` when `n ≡ 0 (mod 4)` | 0 |
| 11 | Custom | amplitude and phase read from the state table (524 amplitudes + 524 phases = the 1048 floats of round 10 §1.5) | from the table |

The manual confirms that the colour selects "pre-computed additive waveforms" and that *Custom* resynthesises a
single-cycle file **[DOC `…/plugins/Harmless.htm`]**; the editor code opens the "Open single-cycle audio file" dialog
when the last item is chosen (`FUN_00792270`/`FUN_007922e0`) **[DECOMP]**.

After the colour, `FUN_00787b40` multiplies the amplitudes by the 6 gains of the timbre EQ (cosine windows in
octaves of the harmonic series, `FUN_0077b450`/`FUN_0077b2d0`; band gain `(3^g − 1)/2`, `g = (v + 1024)/1024`,
as in round 10) **[DECOMP]**.

**Data:** colour 0 in 78/139, Custom (11) in 33, 1 and 8 in 5 each, 5 in 4, others ≤ 3 **[DATA]**.

### 1.2 Harmonic mask, *mask mix*, LHP and *detune* **[DECOMP `FUN_0077f630` cases 1, 11…23, 24, 39; `FUN_00783f90`, `FUN_00783f00`, `FUN_00784120`, `FUN_00787ce0`]**

- **Mask gain** (p11…p22): `g_j = (11^x − 1)/10` with `x = v/1000` (sign preserved) **[DECOMP]**.
- **Application:** starting from the 1st harmonic (the 4 special partials are left out), harmonic `n` is multiplied
  by **`g_((n−1) mod 12)·m + (1 − m)`**, where `m` = *mask mix* (p23, `x`) **plus** the LFO (p24): `m ← m + U·a24`
  with `U ∈ [0, 1]` the **unipolar** LFO (§1.7) and `a24 = x24` limited so that `m` stays in [0, 1] **[DECOMP]**.
  The mask repeats every 12 harmonics — confirms the manual ("cut the first 12 harmonics and their 12×
  multiples", **[DOC `…/plugins/Harmless.htm`]**).
- **With the default mask** (1, 0, 1, 0…; 15 of the 34 channels with m > 0 use exactly this pattern **[DATA]**):
  odd harmonics ×1 and even ×(1 − m). **Inference (Fourier series):** in a ±1 saw harmonic n is
  `2/(πn)` and in a ±1 square the odd ones are `4/(πn)`; hence "saw·(1 − m) + square·(m/2)" reproduces
  the spectrum exactly (odd 1, even 1 − m). LYNX uses a square at level `m` (doubles the odd ones in the mix)
  and switches to a pure square from m = 0.5. The other 19 mask patterns in the demos (e.g.: "one in every 3",
  irregular masks) are **not** saw/square.
- **Low harmonics protection** (p1): `w = (11^(v/1024) − 1)/10`; for the first 8 positions of the table (the 4
  special ones + harmonics 1–4) the amplitude returns to the unmasked value by `masked + (unmasked − masked)·p_k·w`,
  with `p_k = ((1 + cos(π·log₂ r_k / log₂ 5))/2)^5` (r_k = frequency ratio of the position; 1 for r ≤ 1) — in
  practice it protects the fundamental and the subs (Sub 1 gets 1; Sub 3, 0.44; n = 2, 0.08; n = 3, 0.0006) **[DECOMP `FUN_0077b450`, `FUN_00784120`]**.
- **Detune** (p39, ±2400): **it is not unison detuning**. It changes the spacing of the partials: harmonic k sits
  at `f·(1 + (k − 1)·D)` with **`D = 2^(p39/1200)`** (the fundamental does not move); subs 3/4 go to
  `f·(1 + 0.5D)` and `f·(1 + 1.5D)` **[DECOMP `FUN_00787ce0`; constants 1/1200, 1.0, 0.5, 1.5]**. Manual: "Changes
  harmonic tuning, ±24 semitones" **[DOC]**. E.g.: p39 = 1200 → partials at f, 3f, 5f…; p39 = −1200 → f, 1.5f, 2f…
  The number of generated harmonics is `round(limit/(f·D)) + 4`, at most 528 (`FUN_00787c30`) **[DECOMP]**.
  **Data:** p39 ≠ 0 in 19/139 (1200 ×9, −1200 ×5, −2400 ×3, 2400 ×1, 25 ×1) **[DATA]**. LYNX ignores p39.

### 1.3 Sub-harmonics and the 4 special partials **[DECOMP constructor `FUN_0077b800`, `FUN_00787990`, `FUN_0077f630` cases 2–4 and 62, `FUN_00784110`]**

- The frequency ratio table starts with **{1.0; 0.5; 1.5; 2.5}** and continues with 1, 2, 3 … 524 (ramp).
- Gains of these 4 positions (`param+0x580`): position 0 = **p62/256** ("resonance self-oscillation"), positions
  1…3 = **p2/1024, p3/1024, p4/1024** (Sub 1/3/4, linear 0…2) **[DECOMP]**.
- Base amplitude: position 1 (Sub 1, ratio 0.5) uses the amplitude of harmonic 1 of the colour; positions 2 and 3
  use the colour evaluated at n = 1.5 and n = 2.5 **[DECOMP `FUN_00787990`]**. Therefore **Sub 1 = sine one octave below,
  with amplitude (p2/1024)·A(1)** (= p2/1024 in the Saw colour). Manual: "Sub 1 sits an octave below the root. Sub 3 and
  Sub 4 are harmonics of Sub 1" **[DOC]**.
- The mask does not affect the subs (it starts after them) **[DECOMP]**.
- **Inference (Fourier):** with LYNX's ±1 saw the fundamental is 2/π ≈ 0.637; to keep FL's proportion
  the sub sine should have level `0.637·p2/1024` relative to the level-1 saw (up to 1.27 with p2 = 2048). LYNX uses
  `0.5·min(1, p2/1024)`.
- **Data:** p2 > 0 in 19/139; p3 > 0 in 14; p4 > 0 in 12 **[DATA]**.

### 1.4 Master tuning and unison **[DECOMP `FUN_0077f630` cases 38, 65–70, 99; `FUN_00784a10`; *note-on* `0x783ec0…0x784140` in `h_range.c`]**

- **Master pitch** (p38): the plugin stores `p38 − 1200` and adds it to the voice's pitch variable, which is in
  **cents**; the frequency is `440·2^((c/100 + 3)/12)` (`log₂ 440` + …/12) **[DECOMP]**. The fixed −1200 makes a
  note with `c` = 0 sound at 261.6 Hz (**inference**: assumes the host pitch comes in cents relative to FL's
  C5; consistent with C5 = middle C). That is, **p38 = pure transposition in cents** — confirms LYNX.
  Manual: "Main pitch control, ±24 semitones" **[DOC]**.
- **Order** (p65): `n` subvoices; gain per subvoice **`1 − (3^(1 − 1/n) − 1)/2`** (1 for n = 1) **[DECOMP]**.
- **Position** of each subvoice k = 0…n − 1: `u = 2k/(n − 1) − 1` ∈ [−1, 1].
- **Pan** (p66): `pan_k = u·p66/128`, added to the host pan **[DECOMP]** → confirms LYNX's `±p66/128` at the extremes.
- **Tuning** (p67): `S = 1.875·(41^(p67/256) − 1)` (p67 = 128 → 10.1; 256 → 75); subvoice deviation =
  **`±|u|·d_k·S` cents**, added to the pitch variable in cents (with p99 the value is also multiplied by
  velocity) **[DECOMP]**. The sign alternates with the parity of k (and flips in the upper half when n is odd).
  `d_k` comes from a table of 9 values per type (p70) **[DECOMP, table `0x7ee5c8`]**:

  | p70 | Type | d₀…d₈ |
  |---|---|---|
  | 0 | Classic | 0.877 1.069 0.929 1.181 1.031 1.129 0.887 1.117 0.977 |
  | 1 | Uniform | 1.0 (all) |
  | 2 | Blurred | 0.8905 1.0764 0.9116 1.1025 1.0341 1.1669 0.854 1.2063 1.0 (and extra exponential phase) |

  For n = 3: deviations `+d₀S`, 0, `−d₂S` — LYNX's "0, ±S" is the approximation of the Uniform type.
- **Phase** (p68): `k/n·p68/128`; **variation** (p69) multiplies the subvoice's attack time by
  `2^((|u| − 1)·3·p69/128)` (the central ones attack faster) **[DECOMP]**.
- **Data:** n = 1 in 41/139, 2 in 24, 3 in 18, 5 in 16, 7 in 14, 9 in 14; p70 = 2 in 90; p66 = 128 in 95;
  p67 = 128 in 30, 0 in 19 **[DATA]**.
- Total level: FL sums `n` subvoices with gain `1 − (3^(1−1/n) − 1)/2` each; LYNX uses 3 oscillators at 1/3.
  **Inference (sum of decorrelated voices):** FL's RMS grows as `√n·gain(n)` (n = 3: 0.80) and LYNX's
  as `√3/3` = 0.58 (≈ −2.9 dB for n = 3).

### 1.5 Volume envelope **[DECOMP `FUN_0077f630` cases 26–30, `FUN_0077f170`, voice `FUN_00784a10`, `FUN_005f5f60`, `FUN_005f5fe0`, `FUN_005f5f30`]**

- **Times** (converted to samples with the host rate): attack `A = 0.05·(101^x − 1)` s; decay
  `D = 0.11995·(1001^x − 1) + 0.05` s, **p27 ≥ 1000 = infinite**; release `R = 0.0249·(101^x − 1) + 0.01` s
  (`x = v/1000`) **[DECOMP]** → confirm LYNX's formulas.
- **Decay and release**: 1-pole smoother with coefficient **`exp(ln 0.01 / N)`** per sample (N = time in
  samples) that chases zero → **exponential fall that reaches 1 % (−40 dB) at N** and continues (there is no
  *sustain*: it decays to silence) **[DECOMP]**. Infinite decay = coefficient 1 (holds the level) **[DECOMP
  `FUN_005f5f30`]**. Hence LYNX's **1.5** factor (time to −60 dB of an exponential = 1.5 × time to −40 dB) is
  **correct**, and `p27 = 1000 → sustain 1` too.
- **Attack**: **linear** ramp up to 1. For `A ≥ 0.2 s` it goes from 0 to 1 in A. For `A < 0.2 s` (`param+0x5D0` =
  0.2·fs) the ramp has slope `1/(0.2 s)` and **starts at level `(1 − A/0.2)²`**, lasting `A·(2 − A/0.2)`; A = 0
  starts at 1. Manual: "The first few ms set the starting level" **[DECOMP + DOC `…/plugins/Harmless.htm`]**.
- **Output smoother** (object `+0x144`): the whole envelope goes through a second 1-pole filter (1 % in
  `min(A, 30 ms)` during the attack, **80 ms** after the attack, 30 ms in the release) **[DECOMP; constants 0.03 and
  0.08]**. Audible effect only on short decays/releases (≤ ~0.1 s).
- Velocity → attack (p29): A × `(1 − min(1; 1.125·vel))`; velocity → release (p30) multiplies R by a value
  from the host (not identified) **[DECOMP]**.
- **Data:** p27 = 1000 (infinite decay) in 81/139; p26 = 0 in 96; p28 = 0 in 26, 250 in 22 **[DATA]**.

### 1.6 Filter **[DECOMP `FUN_0077f630` cases 47–58, `FUN_0077c170`, `FUN_0077ca30`, `FUN_0077c940`/`FUN_0077c970`, `FUN_0077c540`, voice `FUN_00784a10`]**

The Harmless filter is a **tabulated curve applied to the amplitudes of the partials** (round 10 §1.3); LYNX
approximates it with a biquad. What was confirmed:

- **Domain:** position in **log₂ Hz**; the curve is placed over `log₂(f_partial)` with index
  `(log₂ f − pos)·W + centre`, where **`W = 1024·2^(1 − 0.004·p56)`** points per octave (p56 = 500 → 512) **[DECOMP]**.
- **Range:** `lo = log₂(440·2^(−31/12)) − max(0, M·eL − 1)`, `hi = log₂ 17000 + max(0, M·eR − 1)`, with
  `M = 682.67/W = (2/3)·2^(0.004·p56 − 1)` **[DECOMP `FUN_0077c170`; 17,000 = `0x4684d000`; −31, 440]** — the same
  rule as Harmor (round 18 §1.2). The per-type extensions come from `FUN_0077c940` **[DECOMP]**:

  | Type (combo, without separators) | Table points | centre / eL / eR |
  |---|---|---|
  | 0 Classic LP, 4 Crude LP, 8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 13 Leaky LP | 2048 / 1024 / 682 / 1024 / 1365 / 1024 / 1024 | 0 / 1 / 0 |
  | 1 Classic BP, 2 Classic BS, 5 Crude BP, 6 Crude BS, 14 Phaser, 15 Band phaser, 16 Flat BP | 4096 / 4096 / 2048 / 2048 / 4096 / 4096 / 2048 | 0.5 / 1 / 1 |
  | 3 Classic HP, 7 Crude HP | 2048 / 1024 | 1 / 0 / 1 |
  | 12 Fin LP | 4096 | 0.5 / 1 / 0 |

- **Cutoff (p54):** `pos = lo + (p54/8192)·(hi − lo)` and **`fc = 2^pos` Hz** **[DECOMP]**. With Classic LP and p56 = 500
  (M = 4/3): `lo = log₂ 58.3`, `hi = log₂ 17000`, **`fc = 58.3·2^(8.19·p54/8192)`** (0 → 58 Hz; 4096 → 995 Hz;
  8192 → 17 kHz). LYNX's formula (`73.4·2^(7.855·v/8192)`) is the version without margin (valid for p56 ≤ 250 on
  the low-passes). **p56 = 500 in 57/139** channels **[DATA]**.
- **Shape of Classic LP** (type 0, centre 0): gain 1 below `fc`; above, `10^(−72·t/20)·(1 − t^16)` with
  `t` ∈ [0, 1] along the 2048 points → **−72 dB over 2048/W octaves** (p56 = 500: 4 octaves, ≈ −18 dB/octave;
  p56 = 0: −72 dB/octave; p56 = 1000: ≈ −4.5 dB/octave) **[DECOMP `FUN_0077ca30` case 0; −72 and ln10/20]**.
  The other types were not tabulated (not confirmed).
- **Keytracking (p57):** `pos += (log₂ f_voice − 7)·p57/1000` → **reference 2⁷ = 128 Hz**, factor 1 = 100 %
  (constant −7.0 at `param+0x6F8`) **[DECOMP]**. The lower limit also follows (`lo + key term`).
  **Data:** p57 = 1000 in 43/139, ≠ 0 in 51 **[DATA]**. **LYNX does not set `keyTrack` for Harmless.**
- **Filter envelope** (p48 attack, p49 decay): linear attack 0 → 1 in `0.12·(1001^x − 1)` s (**p48 ≥ 1000: never
  rises**); linear decay 1 → 0 in `0.12·(1001^x − 1)` s (**p49 ≥ 1000: holds at 1**) **[DECOMP cases 48/49,
  per-sample increments]**; then the tension curve (p50 attack, p51 decay) with `q = sign(t)(1000001^|t| − 1)`:
  t > 0 → `1 − ((1+q)^(1−E) − 1)/q`, t < 0 → `((1+|q|)^E − 1)/|q|` — **the same curve as Love Philter** (round
  20 §1.3) **[DECOMP `FUN_005fd4d0`, 1000001]**. Default p51 = −300 (63/139 channels): `q = −62.1`; the level falls to
  half at 16 % of the time.
- **Amount (p52) — non-adaptive mode** (p47 = 0): **`pos += (E − 0.5)·x52·(hi − lo)`** — the envelope is
  **centred**: E = 0 lowers by half the amount, E = 1 raises by half the amount **[DECOMP: `(E − 0.5)` with 0.5 at
  `0x787300`]**. LYNX adds `x52·7.9` octaves without the −0.5 (centre error). Used in **11/139** channels **[DATA]**.
- **Adaptive mode** (p47 = 1, 128/139 channels): `x52 > 0` → `pos += (hi − pos)·E·x52`; `x52 < 0` →
  `pos −= (pos − lo_kt)·E·|x52|` **[DECOMP]**. Confirms LYNX (which uses `log₂(17000/fc)` and `log₂(fc/73.4)`; differs
  only by the margins and by the key term in the lower limit). Manual: "Alt limits modulation to the range from the
  current cutoff to the frequency limit" **[DOC]**.
- Velocity → amount (p53): multiplies x52 by velocity **[DECOMP]**.
- **Resonance:** p59 (`x`), p61 offset (`v/1200` octaves), p62 self-oscillation (`v/256`, gain of special
  partial 0) **[DECOMP]**. The peak width and the conversion to a biquad Q **were not decoded** — LYNX's
  `0.9·x59 (+0.1)` rule is an approximation with no basis in the code.
- Order of the types: confirmed by the table extensions above (LP/BP-BS/HP) and matches Harmor's
  **[DECOMP]**; LYNX's *switch* (1/5/14/15/16 → BP, 2/6 → BS, 3/7 → HP, rest LP) is consistent.
- p55 (oct/Hz scale) switches the curve's axis to linear frequency (`W·0.9/f_c`) **[DECOMP]**; 14/139 channels.

### 1.7 LFO **[DECOMP `FUN_0077f630` cases 24, 42, 58, 86–89; `FUN_0077f0c0`, `FUN_0077f5e0`, `FUN_007793e0` (event 0xD), voice `FUN_00784a10`]**

- **Rate** (p89): phase increment per sample = `sign(v)·2^(|v|/1000)·BPM/(3840·fs)` cycles, with the BPM coming from the
  tempo event (`param+0x2CC`, default 120) **[DECOMP; 3840 = `0xF00`]** → **period = 64/2^(|v|/1000) beats**
  (3000 → 8; 6000 → 1) — confirms LYNX; v = 0 → stopped; v < 0 → phase running backwards.
- **Global** (p87): with p87 > 0 the voice copies the phase of the global LFO, which is **repositioned by the song position**
  (phase = frac(position·rate)) in event 0xD of the *dispatcher*; without p87 the phase belongs to the voice **[DECOMP]**. 112/139 channels
  have p87 = 1 **[DATA]**. LYNX does not turn on `global`.
- **Shapes** (p86; `φ` = phase in cycles, "round" = nearest integer) **[DECOMP]**:

  | p86 | Name | Value L ∈ [−1, 1] |
  |---|---|---|
  | 0 | Sine | `sin(2π(φ + 0.5))` = −sin(2πφ) |
  | 1 | Smooth saw | internal table (not decoded) |
  | 2 | Smooth pulse | `sign(s)·(1 − (1 − |s|)^6)`, s = the sine above |
  | 3 | Saw | `2·(ψ − round ψ)`, ψ = φ − 0.5 (rising ramp) |
  | 4 | Pulse | ±1 according to the parity of `round(2φ − 0.5)` |
  | 5 | Stairs | `(round(3φ − 0.5) mod 3) − 1` (steps −1, 0, 1) |
  | 6 | Triangle | `|4·(d − round(d − 0.5)) − 2| − 1`, d = φ + 0.25 |
  | 7 | Sickle | `2u³ − 1`, u = phase in [0, 1) |
  | 8 | Multisine | `(sin θ + sin(0.2(θ + 7)))/2`, θ = 2π(φ + 0.5) |
  | 9…13 | Filter env / Filter freq / Voice random / Velocity / Pitch | sources, not oscillators |

- **Attack** (p88): linear ramp from 0 to 1 in `0.05·(101^x − 1)` s; the LFO is multiplied by the **square** of this
  ramp **[DECOMP]**.
- **Outputs:** bipolar `L·r²` and **unipolar `U = (L·r² + 1)/2`** **[DECOMP]**.
- **Destinations and units** **[DECOMP]**:
  - **Pitch** (p42): bipolar, `±12·(101^|x| − 1)` **cents** added to the pitch (max. ±1200 ct) — confirms LYNX.
  - **Cutoff** (p58): **unipolar**: `pos += U·A`, with `A = 6·x58` **octaves**, limited so as not to go past `hi`
    (A > 0) or `lo` (A < 0). LYNX applies ±A bipolar; the equivalent is **shifting the cutoff by A/2 and modulating ±A/2**.
  - **Mask mix** (p24): unipolar (§1.2).
  - **There is no volume destination** in the main LFO. p25 ("Stereo tremolo depth", DFM) feeds **its own stereo
    tremolo with a fixed 2.5 Hz oscillator** (`param+0x7A4` = 2.5/fs) applied at the global output
    (`FUN_00604a90`) **[DECOMP]**; the exact form of the application was not decoded. The manual lists the LFO targets
    as "harmonic mask mix, pitch, filter cutoff, phaser width and offset, harmonizer" **[DOC]**. LYNX's mapping
    p25 → volume LFO, with the period of the main LFO, is **wrong**.
  - **Vibrato** (p41): its own **6 Hz** oscillator (`6/fs`), depth `0.075·v` **cents**, with gradual
    fade-in over 1/3 s (`3/fs` per sample, squared) **[DECOMP]**.
- **Data:** p86 = 0 in 102/139; p89 = 3000 in 67; p42 ≠ 0 in 55; p58 ≠ 0 in 31; p25 ≠ 0 in 32 **[DATA]**.

### 1.8 Portamento, mono and master volume **[DECOMP `FUN_0077f630` cases 36, 43–45; `FUN_00784630`]**

- **Time** (p45): `T = (41^|x| − 1)/26.667` s, converted into control blocks **[DECOMP]** — confirms LYNX.
- **Shape:** p45 > 0 → **exponential** approach (coefficient `exp(ln 0.01/blocks)`: 99 % of the interval in T);
  p45 < 0 ("inverted") → **linear** ramp in `T` **[DECOMP; the threshold is 0.0 at `0x784838` as a float]**.
- In one of the *note-on* paths the pitch difference is reduced in steps of 1200 ct while it exceeds
  1250 ct (glides at most ~1 octave); the exact condition **was not confirmed**.
- p43 = portamento, p44 = monophonic (manual: "legato: monophonic mode that slides between overlapping notes")
  **[DOC]**. Data: p44 = 1 in 45/139, p43 = 1 in 52, p45 = 400 in 58 **[DATA]**.
- **Master volume** (p36, 0…2000): `x = v/1000`; gain **`0.75·(3^x − 1)/2`** (x = 1 treated exactly as 0.75)
  **[DECOMP; 0.75 at `0x781170`, ln 3]** — confirms LYNX's curve. The normalisation "0.75 = 0 dB" is a choice of
  LYNX (the absolute level of Harmless relative to other generators was not measured in this round).

---

## 2. Ogun (20 channels in the demos)

**What LYNX assumes** (`src/core/FlGenerators.cpp`, `synthFromOgun`): shape chosen by *fullness*/*richness*
(sine/triangle/saw + partial +1 octave if fullness > 300); tuning `P1 + P2/100` st; decay/release
`((21^(v/10000) − 1)·997.5 + 50)` ms, 10000 = infinite (infinite release → 30 s), attack 0; unison → copies at
±10 ct with pan `±P13/128` and level ×0.6; filter only with order > 0: LP/HP `c = 0.000665(1501^u − 1) + 0.003`
(HP ≤ 0.6), BP/BS `c = min(1.0001; 0.0004(1501^u − 1))`, **`fc = 22050·c·0.9^(order − 1)`**, resonance
`min(0.95; P18/256)`; gain `(3^(P0/128) − 1)/2`.

### 2.1 Level, tuning **[DECOMP `FUN_0086a570`]**

- **Master level** (P0): `x = P0/128`; gain **`(3^x − 1)/2`** (x = 1 → exactly 1.0) **[DECOMP; ln 3 at `0x8f952c`]**
  — confirms LYNX (0 → 0; 128 → 1; 256 → 4). The manual describes the range as "0 to 200%" **[DOC
  `…/plugins/Ogun_master.htm`]** (interface label; the curve is the one from the code).
- **Tuning:** `100·P1 + P2` cents **[DECOMP]**; manual: coarse ±24 st, fine ±50 ct **[DOC
  `…/plugins/Ogun_master.htm`]** — confirms LYNX.

### 2.2 Timbre decay and release **[DECOMP `FUN_008684e0` cases 5–9, `FUN_0086a8e0`, `FUN_0086c050`, `FUN_00842ee0`, `FUN_00843680`]**

- `ProcessParam` stores `P6·0.0001` (or −1 if **P6 = 10000** = infinite) and `P7·0.0001`; at the note, the time in
  samples is **`((21^(P·0.0001) − 1)·997.5·a + 50)·0.001·fs`**, where `a` is the output of the "decay/release
  time" articulator (1 when off) **[DECOMP; 21, 997.5, 50, 0.001]** — confirms LYNX's formula.
- **Release has no infinity:** P7 = 10000 gives `(20·997.5 + 50)` ms = **20.0 s** **[DECOMP: the −1 test exists only
  for the decay]**. LYNX uses 30 s.
- What the time means: the timbre engine (FFT, frames) builds the per-harmonic fall rates by dividing by the
  time, multiplying by the *Decay/Release EQ* maps and by **ln 0.01**, and exponentiating **[DECOMP
  `FUN_00843680`, constant `0x8f92c8` = ln 0.01]**. **Inference:** each harmonic falls 40 dB (to 1 %) in the time
  × map factor. The per-frame normalisation and the per-harmonic maps **were not followed to the end** — the
  1.5 factor (−40 → −60 dB) for LYNX remains **inference, not confirmed**.
- Manual: "DEC: Timbre decay time, combined with the per-harmonic Decay mapping"; "REL: … combined with the
  Release mapping"; the volume envelope (articulator) "must be turned on with 'Enable Envelope / LFO', which is
  off by default" **[DOC `…/plugins/Ogun_synthesis.htm`]**. The articulators of the 20 channels **were not decoded**
  (real attack not confirmed).

### 2.3 Timbre (waveform) **[DOC + partial DECOMP]**

- *Richness*: "Number of active harmonics, from 31 to 32,767 in 11 steps" **[DOC `…/plugins/Ogun_synthesis.htm`]**.
  **Inference:** 31…32767 in 11 steps = `2^(r + 4) − 1` with r = 1…11 = `P52 − 5`.
- *Fullness*: "Blends from the seeded spectrum at −100 to the full spectrum at +100, limited by Rich" **[DOC]**;
  internally `P8/1000` + articulator, limited to [−1, 1] **[DECOMP]**.
- *Seeds* go through an integer *hash* (round 12) and generate the spectrum **[DECOMP]**; the amplitude law of the "full
  spectrum" **was not decoded**. Therefore LYNX's sine/triangle/saw choice is a **heuristic with no confirmed
  basis**.

### 2.4 Unison **[DECOMP `FUN_0086a8e0`, table `0x8e7b00`]**

- Same geometry as Harmless: `u = 2k/(n − 1) − 1`, alternating signs, `d_k` table = that of Harmless's Classic
  type (0.877 1.069 0.929 1.181 1.031 1.129 0.887 1.117 0.977).
- **Tuning** (P15): `S = 5·(11^(P15·a/256) − 1)` **cents** (P15 = 128 → 11.6 ct; 256 → 50 ct;
  `a` = "unison pitch" articulator); subvoice at `±|u|·d_k·S` ct (ratio `exp(… · ln2/1200)`) **[DECOMP; 11, 5,
  ln 2/1200]**. LYNX uses a fixed ±10 ct.
- **Pan** (P13): `u·P13/128` **[DECOMP]** — confirms LYNX.
- **Volume** (P14): per subvoice `0.3` for n ≤ 2; for n ≥ 3, `0.3·(v + 2(1 − |u|)(1 − v))/√n` with `v = P14/128`
  **[DECOMP; 0.3, 2, 1/√n]**.
- **Phase** (P16) spreads the initial phases **[DECOMP]**.
- **Data:** n = 1 in 9/20, 2 in 3, 4 in 2, 5 in 3, 7 in 1, 9 in 2; P15 = 256 in 5 **[DATA]**.

### 2.5 Filter **[DECOMP `FUN_00867cc0` + *note-on* `FUN_0086a8e0` + rate `FUN_00612f00` (Harmless, same library)]**

- Order (P50): 0 = no filter; 1/2/3 = **cascade of 1/2/3 identical RBJ biquads** with the **same** cutoff; on LP/HP
  each stage uses `Q^(1/order)` **[DECOMP: `param+0xF30` = order − 1, `+0xF34` = 1/order, power]**. **There is no
  0.9^(order − 1) factor** that LYNX applies.
- **Frequency scale:** `ω = (c + c₀)·2π·s`, limited to **2.8495 rad**, with `s = 44100/fs` (the `+0x20` field
  of the rate object is `44100/fs` — `FUN_00612f00` of Harmless; **inference**: Ogun uses the same library,
  the `+0xCC` structure is the same). Since `ω = 2π·fc/fs`, this gives **`fc = 44100·(c + c₀)` Hz**, independent of `fs`
  (with oversampling, `s` is divided by 2 — `param+0x59C`). The round 12 hypothesis `s = 0.5` is
  **corrected**: LYNX uses 22050 and yields half the frequency.

  | Type (P49) | `c` (u = P17/256) | c₀ | Limit of c |
  |---|---|---|---|
  | 0 Low pass | `0.0006646667·(1501^u − 1)` | 0.003 | — |
  | 1 Band pass, 2 Band stop | `0.00039993·(1501^u − 1)` | 0.0001 | 1.0001 |
  | 3 High pass | `0.00039993·(1501^u − 1)` | 0.0001 | 0.6001 |

  **[DECOMP: constants `0x868378…0x8683e0`]**. LYNX uses the LP formula for the HP (wrong).
- **Resonance** (P18, `r = P18/256`): with **FLAT** (P51 ≠ 0) it uses `r·(u + 1)/2` **[DECOMP]**. LP/HP:
  `Q = 0.1` (r ≤ 0), `50` (r ≥ 1), otherwise `0.02495·(2001^r − 1) + 0.1`; BP/BS: width `0.01` (r ≤ 0), `12` (r ≥ 1),
  otherwise `0.1199·(101^r − 1) + 0.01` **[DECOMP]**. The translation to LYNX's 0…1 has no basis in the code (the
  current `min(0.95, r)` does not reproduce Q = 0.1 at r = 0).
- **Data (13/20 channels with order > 0):** **[DATA + calculation with the formulas above]**

  | Channel | Type/order | u | FL fc | LYNX fc today |
  |---|---|---|---|---|
  | Kick | LP/3 | 0.242 | 275 Hz | 112 Hz |
  | Clap | LP/3 | 0.719 | 5.7 kHz | 2.3 kHz |
  | High Hat | BP/2 | 0.922 | 14.9 kHz | 6.7 kHz |
  | Snare | LP/3 | 0.289 | 346 Hz | 140 Hz |
  | Swebass 1 / 2 | LP/3 | 0.469 / 0.410 | 1007 / 692 Hz | 408 / 280 Hz |
  | Midnight Lady | LP/2 | 0.816 | 11.6 kHz | 5.2 kHz |
  | Supersaw | LP/1 | 0.375 | 558 Hz | 279 Hz |
  | Airguitar 3 | BS/3 | 1.0 | 20 kHz (limit) | 10.7 kHz |
  | Trumpet | LP/1 | 0.844 | 14.1 kHz | 7.1 kHz |
  | Silverswirll | LP/2 | 0.672 | 4.1 kHz | 1.8 kHz |
  | Padsaw | LP/3 | 0.246 | 280 Hz | 114 Hz |
  | Turbular Bells 2 | LP/1 | 1.0 | 20 kHz (limit) | 20 kHz |

---

## 3. MiniSynth (10 channels in the demos)

**What LYNX assumes** (`synthFromMiniSynth`): Kind `int(9p)`; SAW+SAW/SQR+SQR → 2nd shape with `10·(0.2 + m)` ct,
SQR+SAW → saw with `10·m` ct; supersaw → 3 saws `±1200·log₂(1 + 0.05m)` ct; transposition `int((p − 0.5)·24)`;
cutoff `2^(4 + 10.3F)`; resonance `0.99p`; envelopes with `k = 0.95p + 0.05`; release with `k` applied again;
filter envelope amount `10·(2p − 1)` octaves; LFO: table of periods {1; 1.5; 2; 3; 4; 8; 16; 32; 64;
16; 32; 64} beats, linear amount `4·a` octaves / `12·a` semitones; portamento `v2·0.5` beats.

### 3.1 Confirmed parameters **[DECOMP `FUN_180023c20`]**

- Voices (p0): `int(2p)` → 0: poly *flag*; 1: mono; 2: lead *flag* (`+0x39CC/+0x39CD`) **[DECOMP]**; labels
  Poly/Mono/Lead **[DOC `…/plugins/MiniSynth.htm`]**. All 10 channels: 0 **[DATA]**.
- Transpose (p1): `int((p − 0.5)·24)` semitones **[DECOMP]** — confirms LYNX.
- Slide (p2): coefficient per **block of 32 samples** `k = 0.9·(1 − p)^4 + 0.01`; the "sliding" note moves
  `(note − current)·k` per block (exponential) **[DECOMP `FUN_180022250`, `FUN_180025a40` (blocks ≤ 32)]**.
  **Inference:** time to 99 % = `32·ln 0.01/ln(1 − k)/fs` (p = 0: ~1.4 ms; p = 1: 0.33 s at 44.1 kHz). Manual: "The
  page has no dedicated slide control. Portamento happens automatically in Mono (always) and Lead (when notes
  overlap)" **[DOC]**. In poly the note starts at its own pitch (**not confirmed**). p2 = 0 in all demos.
- OSC Kind (p5): `int(9p)` (0…9) **[DECOMP]** — confirms. Noise (p7): `p²` — confirms. Peak (p9): `0.99p` — confirms.
- LFO Shape (p17) and Dest (p18): `int(2p)` **[DECOMP]**.
- **LFO Rate** (p19): `int(11p)` → **R = 0.25, 0.375, 0.5, 0.75, 1, 2, 4, 8, 16, 64, 128, 256** **[DECOMP: doubles
  `0x3fd0…0x4070` at `+0x3A28`]**.
- LFO Amount (p20) and EGF Amount (p16): `2p − 1` **[DECOMP]**.
- Envelopes (p12–15, p21–24): see §3.4.

### 3.2 Oscillators **[DECOMP `FUN_180021d10` + `objdump` `0x180021d75…0x180021da7`]**

- **Ratio `r` of the 2nd voice** (Kinds SAW+SAW, SQR+SAW, SQR+SQR): `r = 2^(trunc(24·m' − 12)/12 + 0.00625)`, with
  `m' = OSC Mod (+ 12·L if the LFO goes to OSCMOD)` → **integer semitones from −12 to +12 plus a fixed 7.5 ct**
  (`0.00625·1200`) **[DECOMP: 24, 12, 1/12, 0.00625]**. The manual speaks of "±24 semitones" **[DOC]**; the code
  limits to ±12 (the code prevails). LYNX uses a few cents — wrong.
- SAW+SAW: 2nd saw at `f·r`; SQR+SAW: square at f + saw at `f·r`; SQR+SQR: 2nd square at `f·r` **[DECOMP]**.
- SUPERSAW: 8 saws at `f·((k·a − b)·m + c)` (the round 10 constants: `1 + (0.0125k − 0.05)m`) **[DECOMP]**.
- SAW SPREAD: `0.5f(1 − 0.01m)`, `f`, `2f(1 + 0.01m)`; SPREAD 2: `1.5f(1 − 0.01m)` in place of `0.5f` **[DECOMP]**
  — confirms LYNX's choice of octaves/fifth (the ≤ 1 % deviation is ignored there).
- Tuning: `f = 220·2^((sliding_note + transpose + drift + 12·L(FRQ) − 57)/12)`; smoothed random drift
  (`0.01` per block) **[DECOMP `FUN_180022250`]**.
- **Data:** Kind 5 (supersaw) in 7/10, Kind 3 (SQR+SAW) in 2, Kind 0 in 1 **[DATA]**.

### 3.3 Filter **[DECOMP `FUN_1800213e0`]**

- `octaves = 4 + 10.3·F + Kbd·(note − 48)/12 + 14·Vel·(vel − 0.5) + 0.103·drift (+ 4·L if LFO → FLT)
  (+ 4·MW if MW → Filter)`; `fc = 2^(octaves + envelope)` limited to 20 kHz **[DECOMP: 4, 10.3, 48, 1/12, 14, 0.5,
  0.103, 20000]** — confirms LYNX's cutoff.
- **Velocity → cutoff:** p11 = 1.0 in 3/10 channels (Riser, Energic Pads ×2) **[DATA]** → ±7 octaves between vel 0 and 1;
  LYNX ignores it. Kbd = 0 in all.
- The envelope shape (round 10: `fc·(1 + (2^(10a) − 1)·E)`) **was not rechecked** in this round; the peak
  (+10a octaves at E = 1) matches LYNX's `10·(2p − 1)`.

### 3.4 Envelopes **[DECOMP `FUN_1800229c0`, `FUN_180022440`, `FUN_180023c20` cases 12–15, 21–24]**

- Rate per sample: `0.0002/(K·k⁴)` with `k = 0.95·p + 0.05`, `K = 500` (attack) or `50` (decay/release);
  p < 0.001 → instantaneous **[DECOMP]** — confirms round 10.
- **Amplitude release** (p24): the code calls the same routine with `p' = 0.95·p + 0.05` and multiplies the rate by 10
  → `k = 0.95·p' + 0.05` (two mappings) **[DECOMP: 0.95, 0.05, 10]**. **LYNX applies the mapping three times**
  (`tau(0.95·k(v) + 0.05)` and `tau` applies `k` again): for p = 0.56 it gives 0.59 s versus FL's 0.52 s (−60 dB).
- Sustain = p (p14, p23). The stage change is per sample; the rates do **not** depend on `fs` in the code
  (**inference**: times scale with 44.1 kHz/fs).

### 3.5 LFO **[DECOMP `FUN_1800249b0`, `FUN_180025a40`]**

- **Period = R beats**: the phase advances `(1/60)/R·BPM/fs` per sample; with sync, `phase = frac(position in
  beats / R)` (the position advances `n/fs·BPM/60` per block) **[DECOMP: 1/60 at `0x180064358`]**. **Inference:** the
  host's `+0x60` method returns the BPM (it is the only factor that, with 1/60, gives beats). LYNX uses 4R for indices
  0…8 and swapped values for 9…11.
- **Shapes:** 0 sine `sin(2πφ)`; 1 **falling** saw `2(1 − φ) − 1`; 2 square `+1` for φ < 0.5 **[DECOMP]**.
- **Squared amount:** `L = sign(a)·a²·wave` (a = 2p − 1, + mod wheel if MW → LFO), limited to |a| ≤ 1
  **[DECOMP]**. Destinations: cutoff `4·L` octaves; tuning `12·L` semitones; OSCMOD adds `12·L` to OSC Mod
  **[DECOMP]**.
- **Data:** 7/10 channels with LFO → FRQ, |a| = 0.07…0.10 → **±0.06…0.13 st in FL**, versus ±0.84…1.24 st in LYNX;
  all with R = 4 beats (LYNX uses 16) **[DATA + calculation]**. Riser: LFO → OSCMOD (ignored by LYNX).

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Harmless: timbre colour | 12 amplitude/phase laws per harmonic (§1.1), 528 partials (4 special + 524) **[DECOMP]** |
| 1b | Harmless: mask/LHP/detune | gain `g·m + 1 − m` with period 12; LHP protects the fundamental and the subs; p39 = partial spacing `2^(p39/1200)` (not unison) **[DECOMP+DOC]** |
| 1c | Harmless: sub | Sub 1 = partial at f/2 with `(p2/1024)·A(1)`, outside the mask **[DECOMP+DOC]** |
| 1d | Harmless: tuning/unison | p38 in ct (confirms); gain `1 − (3^(1−1/n) − 1)/2`; pan `u·p66/128` (confirms); deviation `±|u|·d_k·1.875(41^(p67/256) − 1)` ct (confirms the unit) **[DECOMP]** |
| 1e | Harmless: envelope | A/D/R confirmed; decay/release = exponential to −40 dB (LYNX's ×1.5 correct); attack < 0.2 s starts at `(1 − A/0.2)²`; smoother 30/80 ms **[DECOMP+DOC]** |
| 1f | Harmless: filter | `fc = 2^(lo + v/8192·(hi − lo))` with margins per type/width (default Classic LP: 58.3·2^(8.19v/8192)); **keytracking ref. 128 Hz** (LYNX ignores); non-adaptive envelope **centred** `(E − 0.5)·x·(hi − lo)`; adaptive confirmed; times `0.12(1001^x − 1)` s with 1000 = never rises / holds; tension curves 1000001 **[DECOMP+DOC]**; resonance Q not confirmed |
| 1g | Harmless: LFO | period `64/2^(|v|/1000)` beats (confirms); global = song position; 9 shapes; squared attack; bipolar pitch in ct (confirms); **unipolar cutoff** `U·6x` octaves; p25 = its own 2.5 Hz tremolo (not the LFO); vibrato 6 Hz **[DECOMP+DOC]** |
| 1h | Harmless: porta/volume | `(41^|x| − 1)/26.667` s (confirms), exponential (p45 > 0) or linear (p45 < 0); volume `0.75(3^x − 1)/2` (confirms) **[DECOMP]** |
| 2a | Ogun: level/tuning | `(3^(P0/128) − 1)/2` and `100·P1 + P2` ct confirmed **[DECOMP+DOC]** |
| 2b | Ogun: decay/release | ms formula confirmed; **release 10000 = 20 s** (not infinite); fall to −40 dB = inference **[DECOMP]** |
| 2c | Ogun: unison | deviation `±|u|·d_k·5(11^(P15/256) − 1)` ct; pan `u·P13/128`; volume per subvoice §2.4 **[DECOMP]** |
| 2d | Ogun: filter | **`fc = 44100·(c + c₀)` Hz** (LYNX uses 22050), no 0.9^(order−1) factor; HP with the BP/BS law (c ≤ 0.6001); Q/width by r; FLAT `r(u+1)/2` **[DECOMP]** |
| 2e | Ogun: timbre | richness `2^(r+4) − 1` harmonics (DOC + inference); *seeds* spectrum not decoded |
| 3a | MiniSynth: oscillators | 2nd voice at `trunc(24m − 12)` st + 7.5 ct (manual says ±24; code ±12) **[DECOMP+DOC]** |
| 3b | MiniSynth: envelopes | `0.0002/(K·k⁴)` confirmed; release with 2 mappings (LYNX uses 3) **[DECOMP]** |
| 3c | MiniSynth: LFO | period = R beats (R = 0.25…16, 64, 128, 256); amount `sign(a)·a²`; falling saw **[DECOMP]** |
| 3d | MiniSynth: filter | cutoff law confirmed; Vel → ±7 octaves (3 channels) ignored by LYNX **[DECOMP+DATA]** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Harmless (`synthFromHarmless`):**
   - **Keytracking:** `synth.keyTrack = p57/1000` (FL's reference = 128 Hz; LYNX's is key 48 = 130.8 Hz,
     a difference of 0.03 octave).
   - **Cutoff:** use the range with margins: `lo = log₂ 73.42 − max(0, M·eL − 1)`, `hi = log₂ 17000 + max(0, M·eR − 1)`,
     `M = (2/3)·2^(0.004·p56 − 1)`, with (eL, eR) = (1, 0) for the LPs, (1, 1) for BP/BS/phasers, (0, 1) for HP,
     (1, 0) for Fin LP; `fc = 2^(lo + p54/8192·(hi − lo))`.
   - **Non-adaptive filter envelope (p47 = 0):** lower the cutoff by `0.5·x52·(hi − lo)` octaves and use amount
     `x52·(hi − lo)`. p49 ≥ 1000 → sustain 1 on the filter envelope; p48 ≥ 1000 → envelope with no effect (stays at 0).
   - **Cutoff LFO:** unipolar — add `A/2` (A = 6·x58 octaves) to the cutoff and use depth `A/2`.
   - **p25:** do not map to the volume LFO with the period of the main LFO; if an approximation is wanted, a volume LFO
     of its own at **2.5 Hz** (period 0.4 s, no *sync*) — the exact depth was not confirmed; otherwise,
     ignore it and record it in the log.
   - **Global:** `lfo.global = p87 != 0`.
   - **Default mask (1,0,1,0…)**: saw at level `1 − m` + square at level `m/2` across the whole range (no switch to
     pure square at m ≥ 0.5); only apply this rule when the 12 gains are the odd pattern.
   - **Detune p39 ≠ 0:** the partial spacing is `2^(p39/1200)`; with ±1200/±2400 the timbre stops being a saw
     (19/139 channels) — record it in the log; the approximation by a basic shape is not covered by the code.
   - **Unison:** keep pan `±p66/128` and deviation `±S` (S = 1.875(41^(p67/256) − 1) ct); FL's compensation gain
     is `1 − (3^(1−1/n) − 1)/2` per subvoice.
   - Keep: master tuning, attack/decay/release (×1.5 is correct), LFO period, pitch LFO in ct, portamento,
     master volume curve.
2. **Ogun (`synthFromOgun`):**
   - **Cutoff:** `fc = 44100·(c + c₀)` Hz with (c, c₀) from the table in §2.5, limited to `2.8495/(2π)·fs`; **remove**
     `0.9^(order − 1)`; HP with `c = min(0.6001; 0.00039993(1501^u − 1))`, `c₀ = 0.0001`.
   - **Release** P7 = 10000 → 20 s (not 30 s).
   - **Unison:** deviation `±5·(11^(P15/256) − 1)` ct instead of ±10 ct.
   - Keep: gain `(3^(P0/128) − 1)/2`, tuning, decay time formula (P6 = 10000 = infinite).
3. **MiniSynth (`synthFromMiniSynth`):**
   - **LFO period** (beats) = `{0.25; 0.375; 0.5; 0.75; 1; 2; 4; 8; 16; 64; 128; 256}[int(11p)]`.
   - **LFO amount:** `4·sign(a)·a²` octaves (FLT) and `12·sign(a)·a²` semitones (FRQ).
   - **Amplitude release:** `k = 0.95·(0.95p + 0.05) + 0.05` (only two mappings): replace
     `tau ((float) (0.95 * k (v[24]) + 0.05))` with `tau ((float) (0.95 * v[24] + 0.05))`.
   - **SAW+SAW / SQR+SAW / SQR+SQR:** 2nd voice `trunc(24m − 12)` semitones + 7.5 ct.
   - **Velocity → cutoff:** if LYNX has velocity on the filter, `14·p11·(vel − 0.5)` octaves.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Harmless `FUN_0077f630` (constants `0x781068…0x781190`: 101, 1001, 0.05, 0.11995, 0.0249, 0.01, 0.12, 0.11975, 41, 26.667, 12, 65, −0.0125, 0.075, 1.875, 0.75, 6, 1000001, 3, 1/1024, 1/256, 1/128) | laws of all parameters used by LYNX | DECOMP |
| Harmless `FUN_00614c80`, `FUN_005f6120` (ln 2, ln 11, ln 3, ln10/20, ln2/12, log₂ 440, ln 0.01) | *runtime* constants | DECOMP |
| Harmless `FUN_00787990`, `FUN_007873c0` (0.5; 1; 0.25; 1.1; 0.2; 7; 0.29167; 0.75; 3; 0.125; √0.5) | timbre colours | DECOMP |
| Harmless `FUN_0077b800` (ratios 1; 0.5; 1.5; 2.5 + ramp), `FUN_0077b450`/`FUN_0077b2d0`, `FUN_00787b40`, `FUN_00784110`, `FUN_00784120` | special partials, subs, timbre EQ, LHP | DECOMP |
| Harmless `FUN_00787ce0` (1/1200, 0.5, 1.5), `FUN_00787c30` | *detune* = spacing, number of harmonics | DECOMP |
| Harmless `FUN_00783f90`, `FUN_00783f00` | mask with period 12, mix + unipolar LFO | DECOMP |
| Harmless *note-on* (`h_range.c` l. 3940–4075; table `0x7ee5c8`; 2, 1/128, 3, 1.125, 0.2·fs) | unison (pan, deviation, gain, phase, variation), attack | DECOMP |
| Harmless `FUN_005f5f60`, `FUN_005f5f30`, `FUN_005f5fe0`, voice l. 5700–5770, `FUN_0077f170` (0.03; 0.01; 0.1; 0.2; 0.08) | envelope: exponential to 1 %, smoothers | DECOMP |
| Harmless `FUN_0077c170` (682.67; 1; 0.5), constructor (17,000, 440, −31), `FUN_0077ca30` (−72, 2048…), `FUN_0077c940`/`FUN_0077c970`, `FUN_0077c540` | filter range, margins and shapes | DECOMP |
| Harmless voice `FUN_00784a10` l. 330–380 (−7.0 at `+0x6F8`, 0.5) | keytracking, centred/adaptive envelope, unipolar cutoff LFO | DECOMP |
| Harmless voice l. 120–270 (2π, 0.5, 2, 3, 0.25, 4, 7, 0.2, 1/3600), `FUN_0077f0c0` (3840), `FUN_0077f5e0`, `FUN_007793e0` (event 0xD) | LFO shapes, rate and sync, pitch in ct | DECOMP |
| Harmless `FUN_0077f170` (2.5/fs, 6/fs, 3/fs), `FUN_00604a90` | stereo tremolo and vibrato with their own oscillators | DECOMP |
| Harmless `FUN_00784630` (0.0 as float, 1250, 1200, −1) | portamento shape | DECOMP |
| Harmless `FUN_00612f00` (1/fs, 44100/fs, 2π) | fields of the rate object | DECOMP |
| `objdump` Harmless `0x409fe0` (`cvtsd2si`) | rounding to nearest | DECOMP |
| Ogun `FUN_008684e0`, `FUN_0086a570` (1/128, ln 3, 1/1000, 0.0001) | level, tuning, decay/release storage | DECOMP |
| Ogun `FUN_0086a8e0` (21, 997.5, 50, 0.001, 11, 5, ln2/1200, 0.3, 2, 1/√n, table `0x8e7b00`) | timbre times, unison | DECOMP |
| Ogun `FUN_0086c050` (same formula, no infinity) | release | DECOMP |
| Ogun `FUN_00842ee0`, `FUN_00843680`, `FUN_008447d0`, `FUN_00605d70` (ln 0.01) | use of the times in the timbre engine | DECOMP (partial) |
| Ogun `FUN_00867cc0` (1501, 0.000665, 0.00039993, 0.003, 0.0001, 1.0001, 0.6001, 2π, 2.8495, 2001, 0.02495, 0.1, 50, 101, 0.1199, 0.01, 12) | filter per type, Q, order | DECOMP |
| Ogun *note-on* (`+0xF2C` type, `+0xF30` order − 1, `+0xF34` 1/order, `+0xF3C` = `+0x20` of the rate object, ×0.5 with `+0x59C`) | filter `s` scale | DECOMP |
| MiniSynth `FUN_180023c20` (24, 0.5, 0.9, 0.01, 9, 0.99, 11, R table, 0.95, 0.05, 10) | parameters | DECOMP |
| MiniSynth `FUN_1800249b0`, `FUN_180025a40` (1/60, 2π, 0.5) | LFO: period, shapes, squared amount | DECOMP |
| MiniSynth `FUN_180021d10` + `objdump` (12, 24, 1/12, 0.00625) | 2nd voice ratio | DECOMP |
| MiniSynth `FUN_180022250` (220, 57, 1/12, 12, 0.01) | tuning, drift, glide | DECOMP |
| MiniSynth `FUN_1800213e0` (4, 10.3, 48, 14, 0.103, 20000) | cutoff | DECOMP |
| MiniSynth `FUN_1800229c0`, `FUN_180022440` (0.0002, 500, 50, 0.95, 0.05) | envelopes | DECOMP |
| 139 Harmless, 20 Ogun, 10 MiniSynth from `Demo projects` (`tmp/r21/py/hl.py`, `og.py`, `ms.py`) | distributions cited | DATA |
| Harmless manual | colour, mask, subs, pitch/detune, attack, filter (alt/adaptive), LFO targets, legato | DOC `…/plugins/Harmless.htm` |
| Ogun manual | level/tuning ranges; DEC/REL with maps, richness, envelope off by default | DOC `…/plugins/Ogun_master.htm`, `…/plugins/Ogun_synthesis.htm`, `…/plugins/Ogun.htm` |
| MiniSynth manual | voice modes, automatic slide, ±24 st (diverges from the code), synced LFO | DOC `…/plugins/MiniSynth.htm` |

### Open questions

- Harmless: shape of the LFO's *Smooth saw* colour (table) and of the curve of the filter types beyond Classic LP; width of the
  resonance peak (p59/p61/p62) and a translation to Q; effect of the *pluck* (p31, 53/139 channels with value < 1000;
  `param+0x600` = `0.11975(1001^x − 1)`, `FUN_00784860` not decoded); shape of the stereo tremolo (p25) and its
  depth; condition of the "octave jump" in the portamento; value returned by the host for velocity → release
  (p30); the absolute level of Harmless relative to the other generators; the audible effect of *detune* p39 in an
  approximation by basic shapes.
- Ogun: per-frame normalisation of the fall rates in the FFT engine (confirm the −40 dB); *Decay/Release EQ* maps and
  articulators of the 20 channels (volume, real attack); law of the "full" spectrum and of the *seeds*; pan law of `0x8f9660`;
  confirm in Ogun itself that the `+0x20` field of the rate object is `44100/fs` (read in Harmless, same
  library).
- MiniSynth: whether glide exists in poly mode; shape of the filter envelope (round 10, not rechecked); exact
  structure of the filter (the routine does not look like a simple 4-pole *ladder* — state matrix); dependence of the rates
  on `fs`; the constant term 0.07 added to the oscillator mix in `FUN_180022440`.
