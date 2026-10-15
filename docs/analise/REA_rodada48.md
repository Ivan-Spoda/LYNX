# Behavioural analysis of FL Studio 2026: round 48 (REA) — Fruity Limiter and Fruity Compressor: exact engines

> **Nature of this document.** Continuation of `REA_rodada7.md` §2 and §5 (state layouts and control laws of both
> plugins) and `REA_rodada22.md` §1.3–1.4 (their side inputs), with the goal of rounds 36/40 (Maximus): describe both
> engines precisely enough for LYNX to run them as exact mixer effects instead of the generic limiter/compressor
> approximations (`limiter()` / `compressor()` in `src/core/FlEffects.cpp`). *Clean-room* reverse engineering,
> behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No FLEX pack was opened; no Image-Line preset, audio or wave data was copied into the repository
> (the unit-test goldens are a handful of output samples of the DLLs for input signals and states of our own).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Results of our own model (which
> implements only DECOMP/EXEC facts) are marked **[model]**; open points are marked **not confirmed**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r48/`.
- **Targets** (copies in `bin/`, hashes identical to rounds 7 and 22):

  | File | SHA-256 |
  |---|---|
  | `Plugins/Fruity/Effects/Fruity Limiter/Fruity Limiter_x64.dll` (Delphi, plugin VMT `0x6d30c0`) | `ed81447b31db7e5085c33c039f198483af71a353c481e85272d09d48e038b082` |
  | `Plugins/Fruity/Effects/Fruity Compressor/Fruity Compressor_x64.dll` (Delphi, plugin VMT `0x6bd5f8`) | `f40214923d930176d246f8d35ccdcd28adecc9812eece931a6a1e0bc670581d1` |
  | `Shared/dsp_ippv2_x64.dll` (Intel IPP build the Limiter calls through function-pointer globals) | only run (EXEC), not decompiled |

- **Tools:** Ghidra 12.1.4 headless with auto-analysis (`gh/imp.sh`, projects `/tmp/r48gh_Limiter`,
  `/tmp/r48gh_Compressor`; dumps `out/Limiter_dec.c` 0x6c0000…0x6e0000, `out/Compressor_dec.c` 0x6b0000…0x6c8000);
  `objdump -M intel` for every place where float/double/x87 order matters (`out/lim_plug.s`, `out/comp_plug.s`);
  constants with our `py/pe.py`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `run/lm.c` → `lm_*.exe`, round 38's `mx.c` with three
  additions: the host dispatcher answers the side-input count query (`0x32`, option `nin=`), `fnN=rva,in,out` calls one
  of the DLL's internal `double` functions on a file of inputs (used to compare the Delphi RTL `Ln`/`Exp`/`Power` with
  libm), and `ippA=`/`ippI=` call an export of the loaded `dsp_ippv2_x64.dll` on a float file. Same safety measures as
  before (DLL loaded unmodified from the installation, `SetErrorMode`, vectored fault handler, `TerminateProcess`,
  `timeout`). Host slot 36 (`GetInBuffer`) returns the side signal for the state's side-input index.
- **Models and drivers** (ours, `python3 -I`, under `py/`): `survey48.py` (states), `lm.py` (harness driver and test
  signals, from round 38), `fpmap.py` (resolves the plugins' function-pointer globals to IPP export names, EXEC),
  `rtl.py`, `ippt.py`, `ippt2.py` (RTL and IPP accuracy), `compmodel.py` and `limmodel.py` (the two models),
  `t_comp.py`, `t_lim.py` (model vs DLL), `t_cpp.py` (LYNX's C++ engine through `cpp/fdcmp.cpp` vs DLL), `gold.py`
  (unit-test goldens). Logs in `out/`.

---

## 1. Usage in the installation **[DATA]** (`py/survey48.py`)

| Plugin | Mixer slots in `.flp` | Files | Presets (`.fst`) | Unique states | Versions |
|---|---|---|---|---|---|
| Fruity Limiter | 343 (298 demo songs, 45 templates; 9 on the master track) | 52 | 13 | 272 | 7 ×190, 6 ×153, 5 ×13 (the presets) |
| Fruity Compressor | 88 (76 demo songs, 12 templates; 2 on the master) | 15 | 20 | 70 | 2 ×71, 3 ×35, 1 ×2 |

The Limiter states exercise every section: compression in 224 (threshold < 1000), side input k ≠ 0 in 175 (k = 1 ×146,
2 ×16, 3 ×10, 4 ×1, 12 ×2), knee ≠ 0 in 62, saturation < 0 dB in 21, noise gate on in 15, attack/release/compressor
curves other than the default in 29, the v7 menu option set in 2. Compressor: all eight types, look-ahead flag on in 84
of 108, no side input.

---

## 2. Fruity Limiter

### 2.1 State (refines round 7 §2.1) **[DECOMP `FUN_006d3760`]**

u32 version; **v ≥ 5** copies 0xA4 bytes (v5: 0x9D) raw into the plugin from `P+0x120` — the 18 controls first
(round 7 table 2.2), the side-input index lands at `P+0x1C0` (blob offset 0xA4, v ≥ 6 only); **v ≤ 4 reads nothing**
and the controls keep their defaults; **v ≥ 7** reads one more byte whose bit 0 is the menu option at `P+0x277`.
Then every control is re-applied through the plugin's own ProcessParam (§2.2/2.3), the side input is registered with
the host and the detector state is reset (`FUN_006d6790`).

The option byte selects the limiter threshold factor from a two-entry table (`0x72bc7c`: 0.975, 1.0)
**[DECOMP + DATA]**: off → the limiter acts at 0.975 × ceiling, on → exactly at the ceiling.

### 2.2 Control laws (float steps as in the code) **[DECOMP `FUN_006d4490` + objdump]**

- `L7(v)`, `L3(v)`: `x = float(v)·0.001f`; x = 1 → 1; otherwise `float((Exp(double(x·ln k_f)) − 1)·double(1/(k−1))_f)`
  with `ln 7`, `ln 3`, `1/6`, `1/2` held as floats (runtime globals, read with the harness). Gain `G = L7(p0)`, ceiling
  `C = L3(p2)`, limit level `T = C·{0.975|1}`, gate gain `L7(p15)`, gate threshold `min(T, L7(p16))`, saturation
  threshold `S = L3(p1)`.
- Time law in samples (`FUN_006d3eb0`): `TS(v) = double(float(Exp(double(float(v)·1e-4f)·Ln 501) − 1))·2·0.001·fs`.
- Compressor threshold (p8): section on only if p8 < 1000; then `θ = max(L7(p8), 0.001)` (the floor is the float
  0.001 at `0x73e5d0`, round 7's open item), else `θ = 100`; `θ_dB = float(Log10(θ)·20)`; level clamp `100·θ`.
- Ratio (p9): `e = Exp(double(float(|v|)·0.001f)·Ln 51)`; v ≤ 0: `ρ = float(double(float(e−1))·0.06 + 1)`;
  v > 0: `ρ = float(1/(double(float(e−1))·0.38 + 1))`; slope `ρ − 1`.
- Knee (p10): sign s ∈ {−1, 0, 1}; `e = Exp(double(|float(v)·0.001f|)·Ln 201)`; v ≤ 0: `κ = float(double(float(e−1))·0.001)`,
  `λ = float(Ln(κ + 1))`, `1/κ`; v > 0: `κ = float(double(float(e−1))·0.005)`, `λ = float(1/Ln(κ + 1))`.
- Saturation (p0/p1/p2/p15/p16 group): on when `double(S) < 1`; record (t, r, 1/r) with `t = S`,
  `r = min(C, 1) − S` (≤ 0 → 2⁻¹⁶) **[DECOMP `FUN_005e0360`]**.
- `Ln`, `Exp`, `Power`, `Log10` are the Delphi RTL's double routines; on 25 000 random inputs each they differ from
  libm by at most 1 double ulp and **never after rounding to float** (`py/rtl.py`) **[EXEC]**.

### 2.3 Derived set-up **[DECOMP `FUN_006d3ee0`, `FUN_006d4170`, `FUN_005d9ae0`]**

- **Look-ahead** `a = Round(TS(p3))` (round half even): the audio delay in frames and the latency reported to the host.
- **Peak window** `W₁ = max(a, 1, Round(TS(p7)))`; **RMS window** `W₂ = max(Round(TS(p14)), 1)`.
- **Curve stages** — one-pole sections with an attack coefficient (used when the input is at or above the state) and a
  release coefficient; a time t samples gives `Exp(Ln 0.01 / max(t, 1))`, t ≤ 0 gives 0; equal coefficients make the
  stage symmetric:
  - *gain smoothing* (only when a ≥ 1): p4 stages, both times `double(float(a)·0.8f)/√p4`, state reset to 1;
  - *limiter release* (only when TS(p5) > 0): p6 stages, attack 0 (instant), release `TS(p5)·2/√p6`, state 0;
  - *compressor curve*: p13 stages, attack `TS(p11)/√p13`, release `TS(p12)·2/√p13`, state 0.
  A stage array is rebuilt (all states reset) only when its count changes.
- **Gate release** `float(Exp(Ln 0.01 / (max(TS(p17), 1)·2)))`; gate state 1.
- **Silence hold** `a + Round(1.5·fs)` samples.

### 2.4 Render, per host block (interleaved stereo, in place) **[DECOMP `FUN_006d5d60` + objdump]**

1. **Silence:** if every sample of the block is ≤ 1e-6 in magnitude the silence counter decreases by the block length,
   otherwise it is set to the hold. Once it reaches −(block length) the plugin **passes the input through untouched**
   and resets its detectors once (`FUN_006d6790`: curve states, gate state, look-ahead buffer). A new plugin starts in
   this bypassed state until the first non-silent block.
2. **Peak level:** per frame `max(|L|, |R|)`, then a running maximum over the last W₁ frames (`FUN_006d0820`, a
   ring plus a monotone index queue, comparisons on the float bit patterns).
3. **Gain buffer** = G for every frame. With the compressor section on: the detector is the side input when the side
   index is > 0 and the host supplies a buffer, otherwise the block itself (still undelayed); per frame `L² + R²`
   (float), a running sum over W₂ frames kept in x87 extended precision (stored as a double between blocks),
   × `float(1/(2·W₂))`, negative values to 0, square root; then the compressor curve stages. Every frame whose
   value exceeds θ (bit compare) gets
   `over = 20·log10(min(v, 100θ)) − θ_dB`, then the reduction `d = over·(ρ − 1)` without knee, or
   `d = (f(over + 1) − 1)·ρ − over` with `f(u) = Ln(κu + 1)·λ` (knee > 0) or `f(u) = (Exp(λu) − 1)/κ` (knee < 0), and
   gain `G·exp(d·ln10/20)` (all float vector steps through IPP).
4. **Look-ahead:** the audio is delayed by a frames (ring swap; a = 0 → no delay).
5. The peak level goes through the release stages.
6. **Limiter and gate**, per frame with level l and gain g: `r = (l·g > T) ? T/l : g` (bit compares); if `l` is below
   the gate threshold, `r = gate + (prev − gate)·gateRelease` instead; `prev = r`.
7. r goes through the gain-smoothing stages and multiplies both channels.
8. **Output stage:** with saturation, samples above t become `t + r·(1 − e^{(t−|x|)/r})` with the sign kept (x87); otherwise
   a hard clip at ±C.

### 2.5 Precision and machine dependence **[DECOMP + EXEC]**

- The curve stages and the RMS sum use the x87 variants (`0x5d9bc0`, `0x6d0bd0`) on the test machine (AMD Ryzen 5
  3600); the framework selects SSE2/double variants (`0x5d9c20`, `0x6d0da0`) when a byte of a CPU-feature record
  (`[0x72d600]+0x16`) is set — what sets it was **not confirmed**, and only the x87 variants are modelled.
- MXCSR during render is 0x9F80 (flush-to-zero, no denormals-are-zero); the x87 control word is 0x033F (64-bit mantissa).
- The vector steps go through `dsp_ippv2_x64.dll` (resolved with `py/fpmap.py`): `ippsLog10_32f_A24`,
  `ippsExp_32f_A24`, `ippsLn_32f_I`, `ippsExp_32f_I`, `ippsSqrt_32f_I`, plus Set/Add/Mul/Threshold primitives that are
  exact. Measured against correctly rounded float results on 200 000 values (`py/ippt2.py`): Log10_A24 differs on
  0.02 %, Exp_A24 on 4 % (±1 ulp), Sqrt on 29 % (±2 ulp), Ln_I on 10 % (up to ±4 ulp), Exp_I on 51 % (up to ±3 ulp)
  **[EXEC]**. LYNX uses correctly rounded results; this is the whole residual of §4.

### 2.6 Side input **[DECOMP + EXEC]**

Confirms round 22 §1.3: only the compressor detector listens to it; the peak detector, limiter and gate always use the
track's own audio. A sidechained Limiter whose source track is silent therefore sees a zero detector signal (no
compression); without any source (host returns no buffer) it falls back to its own input.

---

## 3. Fruity Compressor

### 3.1 State **[DECOMP `FUN_006bd9a0`]**

Versions below 4: six i32 controls into `P+0x124` (round 7 §5), re-applied through ProcessParam, u32 flags (bit 0 =
look-ahead; read even for v1, where the stream ends and it stays 0), v ≥ 3 the side-input index. The flag only
changes the latency reported to the host: **0 or 64 samples** (table `0x70e378`); the audio path is the same.

### 3.2 Set-up (on the first render after any control change) **[DECOMP `FUN_006be980`, `FUN_006be900` + objdump]**

- Time coefficient `c(ms) = 0` for 0 ms, else `float(Power(0.1, float(1/float(ms·fs/1000))))`.
- Peak follower: attack c(0.01), release c(1). Gain attack `c(p3/10)`; release coefficients `c(p4)` (slow peak hold
  and base) and, for the "/R" types (≥ 4), a fast one `c(p4·0.1)` (otherwise `c(p4)`).
- Threshold `τ = 10^(p0/200)`, ratio `p1/10`, slope `1/ratio − 1`, makeup `10^(p2/200)`; knee width from the type
  (`type & 3` → 0, 6, 7, 15 dB at `0x70e3f0`), `κ = 10^(−knee/20)`; natural-log domain bounds `lo = ln(τκ)`,
  `hi = ln(τ/κ)`, `k = 0.5/(hi − lo)` (∞ for the hard knee).
- **Vintage** (`type & 3 = 2`): `hi = ln τ + (ratio/10)^0.25·1.25`, ceiling gain `γ = exp((hi − ln τ)·slope)`, then
  `hi = ln(e^hi/κ)` and `slope = ln γ/(hi − ln τ)`.
- DC blocker: first-order all-pass with `t = tan(3π/fs)` (1.5 Hz), coefficient `(t−1)/(t+1)`.
- The set-up also resets the look-ahead limiter's gain (1), its hold counter and speed (0.01).

### 3.3 Render, per sample **[DECOMP `FUN_006bee30` + objdump]**

1. Input + 1e-30 (in double, then float); `h = (x − allpass(x))·0.5`: the DC-blocked signal, which is also the audio
   that is output.
2. Detector: `max(|h_L|, |h_R|)` — or of the side input (+1e-30, **not** DC-blocked) when the side index is > 0 and the
   host supplies a buffer — + 1e-30, into the peak follower.
3. Static curve on `ℓ = ln(follower)`: below `lo` unity; non-vintage: `[lo, hi)` → `exp(slope·(ℓ−lo)²·k)`, above →
   `exp((ℓ − ln τ)·slope)`; vintage: above hi → γ, else a smoothstep blend `u + (u²(3 − 2u) − u)·min(ln τ − lo, 1)` of
   `u = (ℓ − lo)/(hi − lo)` scaling `(hi − ln τ)·slope` in the exponent.
4. Gain smoother: attack coefficient when the target is lower, the **dynamic release** otherwise; the release is
   recomputed every sample as `base + (fast − base)·r`, r = slow peak-hold minus the gain (vintage:
   `(0.707 − g)·1.414`), floored at 0.
5. × makeup, applied to `h`.
6. **Look-ahead brick wall** (always active): when a sample would exceed 1.0, the target gain `0.9885531/|peak|`
   (−0.1 dB) is written into a 128-entry gain ring as a 64-sample smoothstep from the current gain; the target then
   recovers towards 1 with an accelerating speed (`s ← s + (0.09 − s)·5e-5`) after a 64-sample hold. The audio passes
   through a 64-frame ring, so **the output is always 64 frames late** — with the look-ahead flag off FL simply does not
   compensate it.

Everything is SSE scalar float with the double steps shown; no IPP. Initial state (`FUN_006be800`): all-pass states
1e-30, gain ring 1, audio ring 0.

---

## 4. Verification **[EXEC]**

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical. Signals from round 38
(`music` 3 s with 0.3/1.0/2.5 gain sections, `loud`, `burst`, `quiet`); side input = noise bursts for states with
k > 0. LYNX = the C++ effect (`src/core/FruityDynamics.cpp`) through `cpp/fdcmp.cpp`.

| Check | Cases | Result |
|---|---|---|
| Compressor, Python model vs DLL (`t_comp.py`, music / loud 1 s) | 70 unique states | all bit-identical |
| Compressor, LYNX vs DLL, 44.1 kHz / 512, music / loud / burst / quiet | 70 states × 4 | all bit-identical |
| Compressor, LYNX vs DLL, 48 kHz / blocks of 300, burst | 70 states | all bit-identical |
| Limiter, Python model vs DLL (`t_lim.py`, 1 s) | 272 unique states × music / loud | music: 108 bit-identical, median −154.0 dB, worst −130.9 dB; loud: 103, −157.7 dB, −132.5 dB |
| Limiter, LYNX vs DLL, 44.1 kHz / 512, music | 272 states | 101 bit-identical, median −151.1 dB, worst −129.5 dB |
| Limiter, LYNX vs DLL, loud | 272 states | 103 bit-identical, median −156.2 dB, worst −131.9 dB |
| Limiter, LYNX vs DLL, burst / quiet | 272 states × 2 | burst: 114 bit-identical, median −150.1 dB, worst −128.6 dB; quiet: 125, −152.5 dB, −129.5 dB |
| Limiter, LYNX vs DLL, `gap` (0.5 s music, 2.5 s digital silence, music; exercises the silence bypass and reset) | 272 states, 4 s | 100 bit-identical, median −152.3 dB, worst −130.0 dB |
| Limiter, LYNX vs DLL, 48 kHz / blocks of 300, music | 272 states | 100 bit-identical, median −152.0 dB, worst −129.8 dB |
| Unit test `fruity_dynamics_engines` (`tests/tests.cpp`) | 3 Limiter + 2 Compressor states of our own, goldens from the DLLs | within 2·10⁻⁶ (look-ahead limiter and both compressor cases bit-identical) |

Every Limiter state that is not bit-identical uses the compressor section (IPP log/exp/sqrt, §2.5); the worst cases
are knee ≠ 0 (legacy `Ln_I`/`Exp_I`, up to 4 ulp) and very low thresholds at 20:1. The one exception (−804 dB) is a
noise gate closing to gain 0: the decaying gain reaches denormal values, which FL flushes to zero (FTZ) and the
comparison driver did not; inside LYNX the audio thread runs with FTZ/DAZ.

---

## 5. What LYNX does with it

- **`EffectKind::fruityLimiter`** and **`EffectKind::fruityCompressor`** (`core/FruityDynamics`): the slot parameters
  are FL's raw controls normalised over their ranges (Limiter: 18 controls + the "exact ceiling" option; Compressor:
  6 controls + the look-ahead flag), so the effect stays editable in LYNX's mixer (knobs with FL's units) and an
  imported state round-trips exactly (the project file's 4-decimal precision recovers every raw value). FL
  automation of these plugins maps 1:1 onto the parameters.
- **Side input:** the slot's sidechain track (round 22's source resolution) feeds the Limiter's compressor detector /
  the Compressor's detector; a silent source track is a zero signal, as in FL. The "Sidechain from" menu is available
  on both.
- **Latency:** the Limiter reports its look-ahead to LYNX's PDC; the Compressor reports 64 only with the flag on
  (its output is 64 frames late either way, like FL).
- Host blocks longer than 512 frames are split into calls of at most 512 (the silence logic and the stage precision
  depend on the block boundaries).
- **Fallbacks:** a state that cannot be read (too short, Compressor version ≥ 4) keeps round 7's approximation; the
  import report lists it.

---

## 6. Open items

- The IPP vector math (§2.5) is computed correctly rounded; FL's IPP build differs by 1–4 ulp on part of the values
  (residual −130…−156 dB on the compressor section of the Limiter).
- The SSE2/double variants of the Limiter's curve stages and RMS sum (selected by a CPU-feature byte of the framework)
  are not modelled; what selects them was **not confirmed**.
- The side-input index is clamped by the plugin to the host's input count at load (`FUN_006ce570`); LYNX resolves it as
  round 22 (an index beyond the inputs list = no side input). Whether FL's input count is final at that moment was **not
  confirmed**; no demo state has an index beyond its inputs.
- The peak window's freshly allocated ring is uninitialised in the plugin except for its first entry; LYNX zeroes it
  (matching every EXEC run).

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| Limiter `FUN_006d3760` | state load (v ≤ 4 / 5 / 6 / 7), option byte → `P+0x277` | DECOMP |
| Limiter `FUN_006d4490` | ProcessParam: L7/L3, threshold floor, ratio, knee, saturation record, groups | DECOMP + objdump |
| Limiter `FUN_006d3ee0`, `FUN_006d4170`, `FUN_006d3dc0`, `FUN_006d3eb0` | look-ahead, windows, curve stages, gate, time law | DECOMP + objdump |
| `FUN_005d9ae0`, `FUN_005d9bc0` / `FUN_005d9c20` | stage coefficients; x87 / SSE stage loops | DECOMP |
| Limiter `FUN_006d5d60` | render | DECOMP + objdump |
| VMT `0x6d0458` (`FUN_006d07b0`, `FUN_006d09e0`, `FUN_005e0150`, `FUN_006d0820`) | peak detector and running max | DECOMP |
| VMT `0x6d05f0` (`FUN_006d0b60`, `FUN_006d0d00`, `FUN_006d0bd0`) | RMS detector | DECOMP |
| VMT `0x6cf760` (`FUN_006cf7e0`, `FUN_006cfa60`) | look-ahead delay | DECOMP |
| `FUN_005e02a0`, `FUN_005e0360`, `FUN_005e03a0` | silence test, saturation record and curve | DECOMP |
| table `0x72bc7c`, floor `0x73e5d0`, globals `0x72c5c0…0x72d550` | 0.975/1.0, 0.001, law constants | DATA + EXEC |
| `py/fpmap.py`, `py/ippt2.py` | IPP routines used and their accuracy | EXEC |
| Compressor `FUN_006bd9a0`, `FUN_006bdb90` | state, latency report 0/64 | DECOMP |
| Compressor `FUN_006be800`, `FUN_006be980`, `FUN_006be900` | reset, set-up, time coefficient | DECOMP + objdump |
| Compressor `FUN_006bee30` | render | DECOMP + objdump |
| 52 `.flp` + 33 `.fst` (`py/survey48.py`) | usage, versions | DATA |
