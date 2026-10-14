# Behavioural analysis of FL Studio 2026: round 26 (REA) — Ogun ALT DECAY and release, Toxic Biohazard filter, Kepler chorus

> **Nature of this document.** Continuation of `REA_rodada24.md` §1.7 (Ogun spectrum and per-partial decay),
> `REA_rodada18.md` §7–§8 and `REA_rodada19.md` §3–§4 (Kepler, Toxic Biohazard). *Clean-room* reverse engineering,
> behavioural only. No Image-Line (nor Intel IPP, nor CRT) code or pseudocode was copied: what is here are formulas
> and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched (no FLEX pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from
> running a function of the installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence,
> the item is marked **"not confirmed"**. Inferences made *from* the code are stated explicitly as **inference**.
> Results of our own numerical models (which implement only what is marked DECOMP/EXEC) are marked **[model]**.

- **Targets:**

  | Plugin | DLL (SHA-256, re-hashed against the installation in this round) | Language | Key functions |
  |---|---|---|---|
  | Ogun | `Plugins/Fruity/Generators/Ogun/Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= rounds 12/21/24) | Delphi | timbre voice: start `FUN_00842ee0`, first frames `FUN_00843220`, spectrum/decay set-up `FUN_00843680`, frame step `FUN_00843300`, seed draw `FUN_00843590`, fullness shaping `FUN_00843470`, *release* `FUN_008447d0`, render `FUN_00844940`, crossfade reset `FUN_008446e0`, crossfade mixers `FUN_008448b0`/`FUN_008448f0`, small-value flush `FUN_006284a0`; generator: map set/first–last range `FUN_00842960`, ALT flag setter `FUN_00842680` (callers `FUN_008649e0`, `FUN_0086e550`); IPP dispatcher `FUN_005bc070` |
  | Toxic Biohazard | `Plugins/Fruity/Generators/Toxic Biohazard/Toxic Biohazard_x64.dll` `5c326637be38fcea7148e170b83e6bec8600c87f00fa774eb610ed68f136cb46` (= rounds 15/18/19) | C++ (MSVC, `CToxicGenerator`) | filter render `FUN_180038d00`, coefficient table (in the constructor) `FUN_180038890`, per-block targets `FUN_1800395b0`, per-block voice update `FUN_180039880`, note-on reset `FUN_18003a730`, parameter copy/smoothing `FUN_180040ff0`/`FUN_1800406d0` |
  | Kepler (engine) | `Plugins/Fruity/Generators/Kepler/engine_x64.dll` `baf37a1d8266aaafc55016de259be4b95f1a136a1fc13ba97514c72e2afc63ab` (= rounds 13/17/18/19; front-end `Kepler_x64.dll` `9db78f5a…4d5b` not analysed) | C++ (MSVC) | chorus process `FUN_180001a60`, chorus constructor `FUN_180001770`, one-pole designer `FUN_1800025c0`, biquad designer `FUN_180002690`, sample-rate setter `FUN_18001d300`, mode change inside the global render `FUN_18001b940` (`0x18001c583…0x18001c77f`), global *setter* `FUN_180019a20` (cases 0x45–0x47); CRT helpers `FUN_1800c29e0` (sin), `FUN_1800c3900` (tan), `FUN_1800bff00` (pow) |

- **Tools:** Ghidra 12.1.4 *headless*, read-only, on the existing projects `/tmp/r12gh_Ogun_x64.dll` (program
  `Ogun_x64.dll`), `/tmp/r15gh_Toxic.dll` (`Toxic.dll`) and `/tmp/r13gh_kepeng_x64.dll` (`kepeng_x64.dll`). Runner
  `tmp/r26/g.sh`, scripts `tmp/r26/scripts/` (copies of round 25's); dumps in `tmp/r26/out/` (`og1.c`, `og2.c`,
  `og3.c`, `d3680.txt`, `tx1.c`…`tx6.c`, `kp1.c`…`kp3.c`, `kb940.txt`, `k1a60b.txt`, `kd300.txt`); constants read from
  the PE with `tmp/r18/k.py`. Our own models (pure Python, `python3 -I`): `tmp/r26/py/ogdecay.py`,
  `tmp/r26/py/toxfilt.py`, `tmp/r26/py/toxosc.py`, `tmp/r26/py/kchorus.py`.
  **Execution harnesses** (our own C, built with Zig 0.14.1 for Windows, pattern of round 25's `tab2.c`:
  `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, vectored fault handler that
  ends the process, `TerminateProcess` at the end without the DLL's shutdown, run under `timeout`; no process or
  dialog was left behind):
  - `tmp/r26/run/txtab.c` → `txtab.exe`: loads `Toxic Biohazard_x64.dll`, calls the `CToxicGenerator` constructor
    `FUN_180038890` on a zeroed object (with a zeroed parent buffer) so that the DLL fills its global coefficient
    table, and dumps the 65 536 × 3 floats (`tmp/r26/run/txtab.bin`).
  - `tmp/r26/run/kchorus.c` → `kchorus.exe`: loads `engine_x64.dll`, builds a chorus object with the DLL's constructor,
    designs the filters with the DLL's own designers, writes the mode fields exactly as the decompiled mode-change code
    does, then calls the DLL's per-sample chorus `FUN_180001a60` on a test signal (sine 440 Hz, amplitude 0.5, plus an
    impulse at n = 1000) for 2 s; outputs `tmp/r26/run/kch_{1,2,3}.txt` (first lines also report the CRT helpers).
- **Date:** 2026-10-14.
- **Dataset:** `tmp/r14/blobs14.pkl` (Ogun 23, Toxic Biohazard 3, Kepler 4 channels).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by calling a function of the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated `…/plugins/`). No new manual reading in this round; the quotes below come from rounds 18/19/24.

**Conventions (Ogun, as round 24).** `FUN_0040ad30` = exp, `FUN_0040bf50` = ln. IPP pointers resolved through the
dispatcher `FUN_005bc070` (pointer → global → name): `0x8e9de8` = `ippsRealToCplx_32f`, `0x8e9808` = `ippsSub_32f`
(destination = second source − first source), `0x8e9f68` = `ippsThreshold_LT_32s_I` (applied to float data with
level 0, i.e. negative values become 0), `0x8e8dc0` = `ippsZero_32f`, `0x8ea3e8` = `ippsCopy_64s`; the others as in
round 24 (`0x8e9a30` `ippsMul_32f_I`, `0x8e9ee0` `ippsMulC_32f_I`, `0x8e8fb0` `ippsAddC_32f_I`, `0x8ea2c0`
`ippsAdd_32f_I`, `0x8e9280` `ippsMulC_32f`, `0x8e8d88` `ippsMul_32f`, `0x8e90d0` `ippsDivCRev_32f_I`, `0x8e8c70`
`ippsExp_32f_A21`, `0x8ea380` `ippsSet_32f`) **[DECOMP]**. Ogun constants read in this round: `0x844618` = 0.025,
`0x844630` = 0.001, `0x84463c` = 0.001f, `0x844640` = 0.907029478 (= 40 000/44 100), `0x8f92c8` = ln 0.01,
`0x8c136c` = 5.9604645·10⁻⁸ (= 2⁻²⁴) **[DECOMP + PE]**.

---

## 1. Ogun: ALT DECAY and release (closes round 24 §1.7)

**What LYNX assumes** (`src/core/FlGenerators.cpp`, Ogun timbre engine): per-harmonic τ_h of round 24 folded into
the spectral oscillator's per-harmonic decay; ALT DECAY only enters the RMS normalisation; release uses one overall
time.

### 1.1 Where the state lives **[DECOMP `FUN_00842ee0`, `FUN_00843680`, `FUN_00842960`, `FUN_008649e0`, `FUN_0086e550`, `FUN_00842680`]**

- The plugin's ALT DECAY byte `+0x224` is pushed into the timbre generator (`+0x230`) field **`+0x20`** by
  `FUN_00842680` (callers pass `*(plugin + 0x224)`); each voice copies it at start into its own flag **`+0x65`**
  **[DECOMP]**.
- Voice spectrum buffers (complex, IPP *Pack* layout, bin h at float positions 2h−1, 2h): **`e`** (`voice[0xe]`, the
  spectrum of the *next* frame to be synthesised) and **`f`** (`voice[0xf]`, the per-frame operand). Voice fields:
  `+0x20`/`+0x24` = first/last float index updated each frame, `+0x28` = band limit (number of partials), `+0x2c` = the
  shift s of round 24, `+0x48` = decay state (0 active, 1 "infinite time", −1 frozen), `+0x5c` (double) = the ALT
  coefficient c **[DECOMP]**.
- Partial range at the generator level (`FUN_00842960`, computed when the Impulse map is set, or the Decay map with
  ALT on): **ALT off** → span of map indices with `E_imp > ε`; **ALT on (and both maps present)** → span of indices
  where **`|E_imp − E_dec| > ε`** (ε = the IPP-pointer constant used in round 24, 5.96·10⁻⁸) **[DECOMP]**. With
  s = 0 the voice updates only that span; with s ≠ 0, all partials below the band limit.

### 1.2 Frame clock and what is heard between frames **[DECOMP `FUN_00843220`, `FUN_00843300`, `FUN_00844940`, `FUN_008446e0`, `FUN_008448b0`/`FUN_008448f0`]**

- At note start the voice builds spectrum **X₀**, synthesises it (inverse FFT) into a table, advances `e` to X₁,
  synthesises X₁, advances `e` to X₂, and pairs the two tables (new = X₁, old = X₀). At every frame boundary
  (`hop` samples, 50 ms or 25 ms — round 24 §1.1) it swaps tables, synthesises the current `e` (X_{k+1}) and advances
  `e` once more **[DECOMP]**.
- During frame k the reader multiplies the *old* table by a gain going linearly from 1 to 0 and the *new* table by a
  gain going from 0 to 1 over exactly `hop` samples (increments ±g/hop per sample, per unison voice and channel)
  **[DECOMP `FUN_008446e0` + mixers]**. Both tables share the fixed phase table, so **the amplitude of partial h is
  linear between frame values**:

  **`a_h(n) = X_k(h) + (X_{k+1}(h) − X_k(h))·r/hop`, with `n = k·hop + r`, 0 ≤ r < hop** (n = samples since note-on).

- Amplitude units: `X_k(h)` is in the units of round 24's `A_tab(h) = 2·D̂(h)·E_imp(h−1−s)/√M` (sine amplitude in the
  table, before volume/unison) **[DECOMP + round 24 inference on IPP scaling]**.

### 1.3 Hold law (key down) **[DECOMP `FUN_00843680` (mode 0), `FUN_00843300`]**

Common quantities (per voice; `fs` = rate, `t_dec` in seconds from round 21's law, `hop` in samples):

- `T₀ = 0.025·fs/hop` frames; **`T = max(0, t_dec·fs/hop − T₀)`** frames (`t_dec·fs` is the decay argument passed by
  *note-on*; the 0.025 is `0x844618`) **[DECOMP]**.
- `D̂(h)` = pre-decayed, RMS-normalised level of round 24 §1.7 (computed the same way for both ALT states).

**ALT DECAY off.** `f(h) = d_h` (real factor copied into both components), with
**`d_h = 0.01^(1/(T·B(h)·E_dec(h−1−s) + T₀))`**; each frame `e ← e·f` over the update span **[DECOMP]**. Hence

  **`X_k(h) = A₀(h)·d_hᵏ = A₀(h)·0.01^(t_k/τ_h)`**, `t_k = k·hop/fs`, **`τ_h = max(0, t_dec − 0.025)·B(h)·E_dec(h) + 0.025` s**

(−40 dB time; identical to round 24, now with the max() made explicit).

**ALT DECAY on.** Instead of decay factors, `f` receives a second *target* spectrum
**`Y(h) = D̂(h)·E_dec(h−1−s)`·(same phase, same 2/√M scale as A₀)** — i.e. the Decay EQ is used as an amplitude
shape, not as a time map — and the voice stores **`c = 1 − 0.01^(1/T)`** (c = 1 when T = 0) **[DECOMP: `+0x5c`,
ln 0.01 / T, 1.0 −]**. Each frame (IPP `Sub`, `MulC`, `Add`): `e ← e + c·(f − e)` over the update span
**[DECOMP `FUN_00843300`]**. Hence

  **`X_k(h) = Y(h) + (A₀(h) − Y(h))·(1 − c)ᵏ = Y(h) + (A₀(h) − Y(h))·0.01^(t_k/τ_alt)`**, **`τ_alt = max(0, t_dec − 0.025)` s**

- The morph is **uniform in time for all partials** (B and E_dec do not change its speed); after `τ_alt` the remaining
  gap to the Decay shape is −40 dB. With `t_dec ≤ 25 ms` the second frame already equals Y.
- **The spectrum does not decay to silence while the key is held**: it settles on Y (the Decay EQ shape scaled by
  D̂). Partials outside the update span keep A₀; by construction of the span (§1.1) A₀ = Y there to within ε **[DECOMP]**.
- Infinite decay (`t_dec·fs < 0`, round 21: P6 = 10 000): state `+0x48` = 1 → no frame update (both ALT states keep X₀;
  after the first frames the state becomes −1 and the tables are no longer rebuilt) **[DECOMP]**.
- ALT off only: after each multiply, if the global flag behind `PTR 0x8e9940` (global `0x8f7d78`) is non-zero,
  magnitudes below 2⁻²⁴ are flushed to 0 (`FUN_006284a0`); the factors `d_h` are always flushed the same way. Who sets
  that flag: **not confirmed**.

### 1.4 Release **[DECOMP `FUN_008447d0`, `FUN_00843680` (mode 1), `FUN_00843300`]**

- At note-off the voice recomputes `f` with the **Release EQ** and the release time: **`d_rel,h =
  0.01^(1/(T_rel·B(h)·E_rel(h−1−s) + T₀))`**, `T_rel = max(0, t_rel·fs/hop − T₀)`; the decay state becomes active again
  (`+0x48 = (t_rel·fs < 0)`), so release works even after an infinite decay **[DECOMP]**.
- **ALT DECAY is switched off for the rest of the note** (`+0x65 = 0`) and the update span becomes all partials below
  the band limit; from then on every frame multiplies by `d_rel,h` **[DECOMP]**. So the release **does not restart from
  A₀ or from Y**: it starts from the spectrum the voice already had, whatever the ALT morph reached.
- **Timing.** If note-off arrives r samples into frame k (r > 0): the heard table is frozen at its current mix
  (`(r/hop)·new + (1 − r/hop)·old`), then a new frame of `hop` samples starts at once, crossfading from that value to
  **X_{k+2}** (the spectrum that was already prepared with the *hold* law), and after that every frame multiplies by
  `d_rel,h` **[DECOMP]**:

  `a_h(n_off + j·hop + q) = R_j + (R_{j+1} − R_j)·q/hop`, with `R₀ = a_h(n_off)`, `R₁ = X_{k+2}(h)`, `R_{j+1} = R_j·d_rel,h` (j ≥ 1).

  If the note-off comes before the first rendered sample, the voice first builds X₀/X₁ and then switches `f` without
  restarting the crossfade (the release law then applies from X₂ on) **[DECOMP]**.
- Hence the per-partial release time to −40 dB is **`τ_rel,h = max(0, t_rel − 0.025)·B(h)·E_rel(h) + 0.025` s**,
  measured from X_{k+2}; `t_rel` law: round 21 (not re-read here).

### 1.5 Seeds and fullness (`FUN_00843590`, `FUN_00843470`) **[DECOMP]**

Only re-read to complete the chain: `FUN_00843590` fills M uniform values in [0, 1) with `ippsRandUniform_Direct_32f`
and the given seed, cyclically rotated by the voice offset `+0x30` (set at voice start from a *note-on* argument,
`FUN_00842ee0`); `FUN_00843470` maps u to `(1+p)ᵘ − 1` for p > 0, `(1−p) − (1−p)^(1−u)` for p < 0, copy for p = 0;
the caller scales by `1/|p|` (1 if p = 0), weights the two seeds by the morph value (`voice +0x3c`) and `1 −` it (a
constant `1 − m` when the second seed is "none"), and clamps negatives to 0 — the B(h) of round 24 §1.4, unchanged
**[DECOMP]**.

### 1.6 Model **[model `tmp/r26/py/ogdecay.py`]**

Implements §1.2–§1.4 (frame recurrence, linear crossfade, release timing) and checks the closed forms against the
recurrence (max error 1·10⁻¹⁶). Example (fs 44.1 kHz, hop 2205, t_dec = 0.5 s → T = 9.5, T₀ = 0.5 frames;
A₀ = 1, Y = 0.3): ALT off X₁₀ = 0.0100 (−40 dB at 0.5 s); ALT on X₁₀ = 0.3055, X₄₀ = 0.3000.

---

## 2. Toxic Biohazard: the 4-pole filter (closes round 18 §8 / round 19 §4)

**What LYNX assumes** (`synthFromToxic`): LYNX SVF with `resonance = clamp(0.9·p16, 0, 0.95)`, "no confirmed scale".

### 2.1 Coefficient table **[DECOMP `FUN_180038890` + EXEC `txtab.exe`]**

Built once (global, 65 536 entries of 3 floats at `0x1800af470`), for index i:

- `u = i/65535`; **`fc = 2^(9u + 5)` Hz** (32 Hz … 16 384 Hz); `f = 2·fc/44100` (constants 7.2/44100, 2/44100,
  3.2/44100);
- **`k = 3.6f − 1.6f² − 1`** (entry 0), **`p = (k + 1)/2`** (entry 1);
- **`R = 1.96·(1 − u)^1.5 + 0.98·S`**, `S = 0.95·e^((1 − p)·1.386249) + (u > 0.7 ? (8/9)·(u − 0.7)² : 0)` (entry 2;
  constants 1.96, 0.98, 0.95, 1.386249, 0.7, 0.2666…·3.333… = 8/9) **[DECOMP]**.
- **[EXEC]** the table written by the DLL equals our float32 evaluation of these formulas **bit for bit at all
  65 536 × 3 positions** (`py/toxfilt.py`, max |diff| = 0); this also confirms that the two helper calls are 2^x and
  e^x.

| u | fc (Hz) | k | p | R |
|---|---|---|---|---|
| 0 | 32 | −0.994779 | 0.002611 | 5.670379 |
| 0.25 | 152 | −0.975224 | 0.012388 | 4.933475 |
| 0.5 | 724 | −0.883503 | 0.058248 | 4.127912 |
| 0.7 | 2521 | −0.609284 | 0.195358 | 3.162454 |
| 0.9 | 8780 | 0.179739 | 0.589869 | 1.740690 |
| 1 | 16384 | 0.791569 | 0.895784 | 1.154101 |

The table uses fs = 44 100 Hz as a constant; nothing in the functions read rescales it with the voice rate (voice
`+0x1c`, used by the envelopes and to scale the oscillators by 44100/rate). So at another rate the cutoff in Hz would
scale with rate/44100 — **inference**; whether the host rate reaches `+0x1c` unchanged: **not confirmed**.

### 2.2 Per-sample render (per channel, L and R have separate states) **[DECOMP `FUN_180038d00`]**

1. **Index:** `i = trunc(env·EGAmt·65535 + U)` clamped to 0…65535, where `U` is the per-block cutoff ramp
   (`voice +0x230`, increment `(target − U)/blocklen`, target = round 19's `u·65535`) and `env` the filter envelope
   (`voice +0x70`), both read *before* their per-sample update. No interpolation between entries.
2. **Feedback and saturation:** `v = x − res·R(i)·s4[n−1]` and
   **`u = v·(1 − 3a + 4a² − 2a³)`, `a = min(|v|/15, 1)`** (constants 0.0666667 = 1/15, 4, 2, 3, 1). The curve is odd,
   slope 1 at 0, peak 1.875 at |v| = 7.5, and returns to **0 for |v| ≥ 15** (sat(0.5) = 0.452, sat(1) = 0.817,
   sat(2) = 1.333) **[DECOMP; numbers model]**.
3. **Four identical stages** (x₁ = u, x_{i+1} = s_i):
   **`s_i[n] = (x_i[n] + x_i[n−1])·p − k·s_i[n−1]`** — confirmed exactly as asked (stage input of the previous sample,
   not the previous output) **[DECOMP]**.
4. **Output:** `y = (c0·u + c1·s3 + c2·s4)·(1 − β) + β·x`, with the per-mode weight targets of round 19
   (LP: c2 = 1; BP: c1 = 1, c2 = −1; HP: c0 = 1, c2 = −1; Bypass: β = 1); in modes 1–3 `β = 1.0·10⁻⁷`. Weights and β
   follow their targets with **0.005/sample** and are snapped to 0 inside ±10⁻⁹; with β ≥ 0.9999990 the filter is not
   computed **[DECOMP]**.
5. The result is multiplied by the amplitude envelope value of the previous sample and **added** into the output
   buffers **[DECOMP]**.

### 2.3 Resonance amount **[DECOMP `FUN_1800395b0`, `FUN_180040ff0`/`FUN_1800406d0`]**

- **`res = clamp(Res, 0, 1)`**, where Res is the global smoothed copy of parameter 16 (`+0x3bd4`, round 19 §4.2),
  sampled once per 128-sample block (`voice +0x240`, no per-sample ramp). **Feedback gain = `res·R(u_index)`**:
  5.67 at the bottom of the range, 4.13 at u = 0.5, 1.15 at the top.
- At note-on (`FUN_18003a730`): all filter states 0, envelope 0, cutoff ramp reset (first block jumps to its target),
  output weights and β jump to their targets (sentinel −20) **[DECOMP]**.
- **[model `toxosc.py`]** LP self-oscillation threshold (impulse 0.01, still ringing after 0.75 s): res ≈ 0.732
  (u = 0), 0.809 (0.3), 0.865 (0.5), 0.885 (0.7), 0.909 (0.9), 0.964 (1.0).

---

## 3. Kepler: the chorus (extends round 18 §7 / round 19 §3.3)

**What LYNX assumes** (`synthFromKepler`): "Chorus I/II: the LYNX synth has no chorus".

### 3.1 Mode and switching **[DECOMP `FUN_180019a20` case 0x45, `FUN_18001b940`; DATA]**

- Panel position 30 → internal 0x45 (round 17); **mode = trunc(3x + 0.5)**: 0 Off, 1 = I, 2 = II, 3 = I+II
  (constant 3.0 at `0x1800ed91c`). Demos: "Pads - Kepler" and "Badious" x = 0.66 → **II**; "Stroll" and "AceBass"
  x = 0.33 → **I** **[DATA]**.
- A mode change fades the chorus contribution out (old mode) and in (new mode) with a step of `200/fs` per sample
  (5 ms each way) **[DECOMP: `0x1800ed950` = 200]**; with mode Off the chorus is not called.

### 3.2 Per-mode constants **[DECOMP `0x18001c583…0x18001c77f`]**

| Mode | LFO rate | Shape | R-channel phase offset | Delay = base + depth·w (w ∈ [0, 1]) |
|---|---|---|---|---|
| I | **0.4 Hz** | triangle | **0.5 cycle** | 1.08 ms + 4.15 ms·w (1.08…5.23 ms) |
| II | **0.6 Hz** | triangle | **0.5 cycle** | same |
| I+II | **8.0 Hz** | sine | **0.0286111 cycle** (10.3°) | 2.85 ms + 0.52 ms·w (2.85…3.37 ms) |

All modes: feedback 0, wet gain 1, mix m = 0.5, tempo sync off, phase-reset flag off. The LFO increment is
`rate/fs` per sample in a **float32** phase (this truncation is audible in the model comparison, §3.5) **[DECOMP]**.

### 3.3 Signal chain (per sample) **[DECOMP `FUN_180001a60`, `FUN_180001770`, `FUN_18001d300`, `FUN_1800025c0`, `FUN_180002690`]**

1. **Mono input:** L = R = (L + R)/2 (constructor flag), also used as the dry.
2. **LFO:** triangle `w = 2φ + 0.5` (φ < 0.25), `1.5 − 2φ` (φ < 0.75), `2φ − 1.5` (else) — 0.5 at φ = 0, 1 at 0.25,
   0 at 0.75; sine `w = (sin 2πφ + 1)/2`. Left uses φ, right uses frac(φ + offset) **[DECOMP; EXEC: helper is sin]**.
3. **Delay smoothing:** `D ← D + (base + depth·w − D)·s`, with **`s = 1 − 0.1^(20/fs)`** (0.001044 at 44.1 kHz; set by
   the sample-rate setter, the constructor's 0.01 is overwritten); D starts at 0.01 s **[DECOMP + EXEC pow]**.
4. **Pre-filter into the delay line:** first-order high-pass (bilinear, `t = tan(π·fc/fs)`) then **two identical RBJ
   low-pass biquads, 7 000 Hz, Q = 0.707** **[DECOMP]**. The high-pass is designed at **50 Hz** by the sample-rate
   setter and re-designed at **0.5 Hz** on every mode change (`XMM15` = 0.5 loaded at `0x18001bba4`, the only write
   before `0x18001c5f9`) **[DECOMP]**; which of the two is active in a running instance depends on the order of the
   host's rate change and the state load — **not confirmed** (the harness uses 0.5 Hz).
5. **Delay line:** 32 768 samples per channel; read at `D·fs` samples with **4-point cubic Hermite (Catmull-Rom)**
   interpolation between the samples written ⌊D·fs⌋ and ⌊D·fs⌋+1 samples ago **[DECOMP]**.
6. **Noise (optional):** white noise from a 32-bit LCG per channel (multiplier 0x0BB38435, increment 0x3619636B,
   scaled by 2⁻³¹), filtered by a peaking biquad **6 500 Hz, Q = 0.25, linear gain A = 1 + 2w** (redesigned every
   16 samples, w = that channel's LFO value), times **level = 0.2·x³** (internal 0x46), added to the wet
   **[DECOMP]**. Which biquad state belongs to L/R is **inference** (the decompiler lost the output pointers).
   Default level when the panel state does not carry 0x46 ("Kepler-1" layout has no Chorus Noise slot): the engine
   constructor writes **0.01** to that field; whether the front-end overwrites it: **not confirmed**.
7. **Post-filter:** first-order low-pass (bilinear) at **7 000 Hz** **[DECOMP]**.
8. **Mix:** `out = wet·√m·g + dry·√(1 − m)`; m starts at 0 and follows 0.5 with the coefficient s of step 3 **[DECOMP]**.
9. **Gate (optional, internal 0x47 ≥ 0.5):** if |L| or |R| ≥ 8·level, hold 440 samples at gain 1, then multiply the
   gain by 0.999652 per sample (−60 dB after ≈ 19 800 samples; constants from the constructor, no rate scaling found)
   **[DECOMP]**.
10. **Output gain:** starts at 0 and follows 1.0 with coefficient s (fade-in of ≈ 50 ms to −20 dB of the gap after
    the instance starts) **[DECOMP]**.

The stereo image therefore comes only from the LFO phase offset between the two delay lines (and the independent
noise generators); both lines receive the same mono signal.

### 3.4 Coefficients at 44.1 kHz **[EXEC `kchorus.exe`]**

High-pass 0.5 Hz: (0.999964, −0.999964; feedback 0.999929); post low-pass 7 kHz: (0.352571, 0.352571; feedback
0.294858); each RBJ biquad: b = (0.143485, 0.286971, 0.143485), feedback (0.680702, −0.254644); s = 0.00104371.
CRT helpers: `FUN_1800c29e0(0) = 0`, `(π/2) = 1` (sin); `FUN_1800c3900(0.5) = 0.546302` (tan);
`FUN_1800bff00(0.1, 2) = 0.01` (pow).

### 3.5 Verification **[EXEC + model `py/kchorus.py`]**

The model (§3.2–§3.3, noise off, float32 phase/delay arithmetic) against the DLL's own `FUN_180001a60` over 88 200
samples: max |difference| = 2.2·10⁻⁵ (I), 2.3·10⁻⁵ (II), 1.6·10⁻⁵ (I+II), with output peaks ≈ 0.71/0.71/0.59 (≈ −90 dB).
With double-precision LFO phase the difference grows to 4.6·10⁻³ (the float32 phase slightly changes the effective
rate) **[model]**.

---

## 4. Not decoded / open in this round

- Ogun: who sets the flush flag behind `PTR 0x8e9940` (§1.3); the release-time law itself is round 21's (not re-read).
- Toxic: whether the voice rate `+0x1c` is the host rate (and hence whether cutoff in Hz scales with rate/44100);
  velocity range (round 19).
- Kepler: which pre-filter high-pass (50 Hz or 0.5 Hz) is active in practice; default Chorus Noise level/gate as set
  by the `Kepler_x64.dll` front-end; BPM-sync law of the chorus LFO (object byte `+1`, not used by any mode-change path).

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| 1a | Ogun frame law, ALT off | `X_k = A₀·d_hᵏ`, `d_h = 0.01^(1/(T·B·E_dec + T₀))`, `T = max(0, t_dec·fs/hop − T₀)`, `T₀ = 0.025·fs/hop` **[DECOMP]** |
| 1b | Ogun frame law, ALT on | `X_k = Y + (A₀ − Y)·(1 − c)ᵏ`, `c = 1 − 0.01^(1/T)`, `Y = D̂·E_dec` (shape, no time scaling); settles on Y while held **[DECOMP]** |
| 1c | Ogun between frames | linear crossfade over `hop` samples: `a(n) = X_k + (X_{k+1} − X_k)·r/hop` **[DECOMP]** |
| 1d | Ogun release | from the current spectrum (no restart), ALT switched off, `×0.01^(1/(T_rel·B·E_rel + T₀))` per frame; first segment ends at X_{k+2} (hold law) **[DECOMP]** |
| 2a | Toxic table | `fc = 2^(9u+5)`, `k = 3.6f − 1.6f² − 1`, `p = (k+1)/2`, `R = 1.96(1−u)^1.5 + 0.98(0.95e^((1−p)·1.386249) + [u>0.7](8/9)(u−0.7)²)` **[DECOMP + EXEC bit-exact]** |
| 2b | Toxic feedback | `res·R`, res = clamp(p16 smoothed, 0, 1), per 128-sample block **[DECOMP]** |
| 2c | Toxic saturation | `v·(1 − 3a + 4a² − 2a³)`, a = min(|v|/15, 1), applied to `x − res·R·s4[n−1]` **[DECOMP]** |
| 2d | Toxic stages | `s_i = (x_i + x_i,prev)·p − k·s_i,prev` ×4; LP s4, BP s3 − s4, HP u − s4 **[DECOMP]** |
| 3a | Kepler chorus modes | I 0.4 Hz tri, II 0.6 Hz tri (L/R 180°), 1.08–5.23 ms; I+II 8 Hz sine (L/R 10.3°), 2.85–3.37 ms **[DECOMP + EXEC]** |
| 3b | Kepler chorus chain | mono in; HP (0.5/50 Hz) + 2× RBJ LP 7 kHz Q 0.707 → Catmull-Rom delay → (+noise) → 1-pole LP 7 kHz; equal-power mix 0.5; no feedback **[DECOMP + EXEC]** |

---

## 5. For LYNX

1. **Ogun (`FlGenerators.cpp`, Ogun timbre engine).** Per partial h, with `A₀(h)` = round 24 amplitude,
   `t_k = k·hop/fs`, `hop` = 50 ms (quality 0–2) or 25 ms (3–4):
   - ALT off: `A(t_k) = A₀·0.01^(t_k/τ_h)`, `τ_h = max(0, t_dec − 0.025)·B(h)·E_dec(h−1−s) + 0.025` (as now).
   - **ALT on** (body byte `0xF8`): `A(t_k) = Y + (A₀ − Y)·0.01^(t_k/τ_alt)`, `τ_alt = max(0, t_dec − 0.025)`,
     `Y(h) = 2·D̂(h)·E_dec(h−1−s)/√M` (with the 1/√h option of round 24 applied to the Decay map too). If the LYNX
     spectral oscillator only offers a per-harmonic exponential decay to zero, ALT DECAY cannot be expressed exactly:
     it needs a per-harmonic *sustain level* Y and a common time `τ_alt` (log the approximation if it is reduced to
     the existing decay).
   - Between frame instants interpolate linearly (or simply evaluate the closed form continuously — the difference is
     the piecewise-linear shape within 25/50 ms).
   - **Release:** start from the current per-partial amplitude, `A(t) = A_off·0.01^(t/τ_rel,h)` with
     `τ_rel,h = max(0, t_rel − 0.025)·B(h)·E_rel(h−1−s) + 0.025`, ALT no longer applied (exact Ogun timing: one more
     frame of the hold law first, §1.4).
2. **Toxic (`synthFromToxic`).** Replace the SVF mapping with the filter of §2: per sample, index from cutoff/envelope
   (round 19 §4.2–4.3), coefficients `k, p, R` from the formulas of §2.1 (a 65 536-entry table or direct evaluation at
   fs = 44 100 constants), `v = x − res·R·s4`, `u = sat(v)`, four stages `s_i = (x_i + x_i,prev)·p − k·s_i,prev`, output
   s4 / s3 − s4 / u − s4 for LP / BP / HP, `res = clamp(p16, 0, 1)`. Reference code: `tmp/r26/py/toxfilt.py`
   (`Filt.tick`).
3. **Kepler (`synthFromKepler`).** Add a stereo chorus after the voice sum with the constants of §3.2/§3.3: mono sum,
   two modulated delays (L and R LFO phases offset by 0.5 cycle for I/II, 0.0286 for I+II), Catmull-Rom read, 7 kHz
   low-pass before (2× biquad Q 0.707) and after (1-pole) the delay, delay smoothing `1 − 0.1^(20/fs)`, equal-power
   mix `wet·√0.5 + dry·√0.5`, no feedback; noise only if the state carries a Chorus Noise value (not in "Kepler-1").
   Reference code: `tmp/r26/py/kchorus.py` (`Chorus.tick`), matched to the DLL to −90 dB.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Ogun `FUN_00843680` | T, T₀, c = 1 − 0.01^(1/T) (`+0x5c`), d_h, Y = D̂·E_dec into `f` when ALT, release factors with Release map, ALT cleared at release, span set-up | DECOMP |
| Ogun `FUN_00843300` | per-frame `e ← e·f` (ALT off) or `e ← e + c(f − e)` (ALT on); IFFT + table pairing | DECOMP |
| Ogun `FUN_00843220`, `FUN_00844940`, `FUN_008446e0`, `FUN_008448b0/8f0` | frame clock, linear crossfade old→new over `hop` | DECOMP |
| Ogun `FUN_008447d0` | release: mid-frame freeze of the heard table, new frame towards X_{k+2} | DECOMP |
| Ogun `FUN_00842960` | update span: E_imp > ε, or |E_imp − E_dec| > ε with ALT | DECOMP |
| Ogun `FUN_008649e0`, `FUN_0086e550`, `FUN_00842680` | plugin `+0x224` → generator `+0x20` → voice `+0x65` | DECOMP |
| Ogun `FUN_00843590`, `FUN_00843470` | seed draw and fullness shaping (round 24 §1.4) | DECOMP |
| Toxic `FUN_180038890` + `txtab.bin` | coefficient table formulas; DLL table = model bit for bit | DECOMP + EXEC |
| Toxic `FUN_180038d00` | index, saturation, stage recursion, output weights, dry leak, VCA | DECOMP |
| Toxic `FUN_1800395b0`, `FUN_18003a730`, `FUN_180039880` | res = clamp(p16), per-block targets, note-on reset, 44100/rate oscillator scaling | DECOMP |
| Kepler `FUN_180019a20` (0x45–0x47) | mode = trunc(3x + 0.5), noise level 0.2x³, gate flag | DECOMP |
| Kepler `FUN_18001b940` @ `0x18001c583…` | per-mode rate, shape, offset, delay base/depth, mix, HP 0.5 Hz | DECOMP |
| Kepler `FUN_18001d300`, `FUN_1800025c0`, `FUN_180002690` | smoothing 1 − 0.1^(20/fs), HP 50 Hz, 2× RBJ LP 7 kHz Q 0.707, 1-pole LP 7 kHz, noise peaking EQ | DECOMP |
| Kepler `FUN_180001a60`, `FUN_180001770` | chain order, Catmull-Rom read, noise LCG, gate, equal-power mix, output fade-in | DECOMP |
| `kchorus.exe` → `kch_*.txt` | DLL chorus output vs model (≤ 2.3·10⁻⁵) and CRT helper identities | EXEC |
| `blobs14.pkl` (Kepler) | chorus panel values 0.33/0.66 → modes I/II | DATA |
