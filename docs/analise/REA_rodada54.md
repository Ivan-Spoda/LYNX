# Behavioural analysis of FL Studio 2026: round 54 (REA) — Fruity Stereo Shaper, Fruity Stereo Enhancer, Transient Processor and Spreader

> **Nature of this document.** Continuation of `REA_rodada7.md` (state layouts of these effects, first survey) and
> `REA_rodada17.md` §2 (Stereo Shaper's "In/Out difference" send). *Clean-room* reverse engineering, behavioural only.
> No Image-Line code or pseudocode was copied (nor that of the Faust-generated engine or Intel IPP): what is here are
> data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the
> repository — these four effects need no installation data at all.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Unknowns are marked
> "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code (`objdump -M intel` wherever the order of float operations matters) |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r54/`.
- **Targets** (`Plugins/Fruity/Effects/…` unless stated):

  | File | SHA-256 | Role |
  |---|---|---|
  | `Fruity Stereo Shaper/Fruity Stereo Shaper_x64.dll` | `25766939e0c83fc30bd6bea1f2bb8bb66202b403eb140bd33bcb449ea69b5204` (= round 17) | Delphi; render `FUN_006cf460`, effects stage `FUN_006cf350`, `ProcessParam` `FUN_006ceb50`, state `FUN_006ce390` |
  | `Fruity Stereo Enhancer/Fruity Stereo Enhancer_x64.dll` | `5dca2e9ac278516b30e147d8e80e84e49813e9ef5a0da765dacd9fb7df5d7749` | Delphi; render `FUN_006bfec0`, delay `FUN_006bfe50`, gains `FUN_006bf2c0`, `ProcessParam` `FUN_006bf530`, state `FUN_006bef40` |
  | `Transient Processor/Transient Processor_x64.dll` | `3d0475da4c58967015ceb61ae993c591c4388790b67f77e8fab65cda84ced053` | Delphi wrapper; render `FUN_006edaf0`, `ProcessParam` `FUN_006ed030`, state `FUN_006ec7c0`, dispatcher `FUN_006ebf00` |
  | `Transient Processor/engine_x64.dll` | `9591b7d2e75c125017b4ddf51e6ff435dad47646b228871c027395ce8d589822` | C++ (MSVC, RTTI `TransientProcessorAudioEffect : AudioEffectX`); process `FUN_180002610`, detector `FUN_1800011c0`, parameter set `FUN_180002010`, rate `FUN_180002470`, detector coefficients `FUN_180001000`, constructor `FUN_180001840` |
  | `Spreader/Spreader_x64.dll` | `5b8845fa64cb39c92b9b1fb2dc43083649f7651aa23f781ee134b4c2c3bf10d7` | Delphi wrapper; render `FUN_006da430`, `ProcessParam` `FUN_006d9740`, parameter hand-off `FUN_006d9960`, state `FUN_006d94f0` |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` | C++; Spreader's engine `StereoEnhancerImpl` → Faust-generated `ILRDStereoEnhancer` (metadata: `StereoEnhancer.dsp`, `-single -ftz 0`); compute `FUN_180189900`, constants `FUN_180189420`, clear `FUN_180189590` |

- **EXEC harness:** `run/fx.c` → `fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu): round 51's effect harness (itself
  rounds 35/38's) with three additions: `q=idx,val,flags` calls the plugin's own `ProcessParam` and logs the result
  (used to read every control's range and default), the host's output-buffer request (host slot 37) hands out a send
  buffer that is written to a file (`sendf=`), and `late` loads the state after the sample rate instead of before.
  Same safety measures as before (unmodified DLLs, `SetErrorMode`, vectored fault handler, `TerminateProcess`,
  `timeout`). The plugins leave the MXCSR at `0x9FE0`/`0x9FC0` (flush-to-zero and denormals-are-zero on) **[EXEC]**.
- **Drivers (our own code):** `py/survey54.py` (every state of the four effects: mixer and channel slots of every
  `.flp` of the installation, their plugin presets, and nodes nested in Patcher states at any depth →
  `data/states54.pkl`), `py/fx.py` (runs the harness; test signals as in round 40), Python models `py/ssmodel.py`,
  `py/semodel.py`, `py/tpmodel.py`, `py/spmodel.py` with their comparisons `py/t_ss.py`, `t_se.py`, `t_tp.py`,
  `t_sp.py`; `py/t_cpp.py` (LYNX's C++ engines through `cpp/sfxcmp.cpp` against the DLLs), `py/gold.py` (the goldens
  of `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "bit-identical" = every output sample
equal.

---

## 0. Installation survey **[DATA]**

| Effect | States found | Demo-project states (files) | Kinds | Distinct states |
|---|---|---|---|---|
| Fruity Stereo Shaper | 492 | 61 (18) | 76 mixer, 391 inside Patcher, 25 presets | 104 |
| Transient Processor | 58 | 47 (14) | 47 mixer, 1 Patcher, 10 presets | 57 |
| Fruity Stereo Enhancer | 51 | 27 (9) | 23 mixer, 25 Patcher, 3 presets | 30 |
| Spreader | 33 | 32 (8) | 33 mixer | 25 |

Before this round LYNX imported the Stereo Shaper as a plain L/R matrix with a linear gain law (its delay, phase and
pre/post options dropped) and the other three as a transparent utility.

---

## 1. The Delphi plugin framework shared by Stereo Shaper and Stereo Enhancer **[DECOMP + EXEC]**

Both DLLs carry byte-for-byte the same framework routines (compared instruction by instruction):

- **Host rate object** (set by `FPD_SetSampleRate`): `w = (1/fs)·2π`; a maximum filter frequency
  `float(fs·0.4999)`; a gain slope limit `float((44100/fs)·0.001)` per sample.
- **Gain ramp** (per block of n samples, per gain): the block starts at the current value `s`; `inc = target − s`;
  if its bit pattern is not zero the current value becomes the target and `inc /= n`; when `|inc|` exceeds the slope
  limit, `inc` takes the limit with its sign, the current value becomes `s + inc·n`, and if that lands below 2⁻²⁴ in
  magnitude it is set to 0 and `inc = −s/n`. All in single precision.
- **2×2 matrix** on interleaved stereo with gains `(c₀, c₁, c₂, c₃)`: `L' = L·c₀ + R·c₂`, `R' = R·c₁ + L·c₃`; while
  any gain ramps, each gain is advanced by its increment after every sample (accumulated in float); without ramps and
  without cross terms only the two direct gains are applied.
- The four gains of a new instance are 0, so **the output fades in** over the first blocks; the Flush dispatcher
  (`FPD_Flush`) makes them jump to their targets and clears the all-pass state.
- **All-pass** (type 5 of the framework's biquad designer): `α = sin ω`, `d = 1/(1 + α)`, `a₁ = b₁ = −2·cos ω·d`,
  `a₂ = b₀ = (1 − α)·d`, `b₂ = 1`; direct form I in double precision with float input/output, evaluated as
  `y = x·b₀ + (((x₁·b₁ − y₂·a₂) + x₂·b₂) − y₁·a₁)`. After each block the four state values are kept in double and only
  zeroed below 2⁻²⁴ (the decompiler shows a float conversion that the x87 instructions do not make — the same trap as
  round 40 §2.4).
- **Delay ring:** a ring of D floats on one channel; each sample swaps the channel's value with the ring slot and
  advances. D = `Round(float(units · float(fs/100000)))`, units in 10 µs.

## 2. Fruity Stereo Shaper **[DECOMP + EXEC]**

### 2.1 State and controls
Record `0xD5`: an `i32` the loader reads and ignores (0 in every state), then 8 × `i32` = parameters 0…7, taken raw.
Ranges and defaults (EXEC, the plugin's own `ProcessParam`): R→L, L, R, L→R −25600…25600 (defaults 0, 12800,
12800, 0); delay and phase −4096…4096 (0); the "In/Out difference" destination 0…; effects order 0/1 (0 = pre).

### 2.2 Formulas
- **Matrix gain** of a raw value v: `x = float(v/12800)`; `|g| = 1` when `|x| = 1`, otherwise
  `float((exp(float(|x|·float(ln 3))) − 1)·0.5)`, with the sign of x — i.e. **(3^|x| − 1)/2**. 12800 = unity, 25600 =
  ×4 (+12 dB), 6400 = 0.366; the M/S presets' 8076 gives 0.5003, the manual's "Mid = (L+R)/2". LYNX's earlier linear
  law (6400 → 0.5) was wrong everywhere except 0 and ±12800. Gains: L←L = L, R←R = R, L←R = R→L, R←L = L→R.
- **Delay:** `units = float(float(exp(float(|v|/4096)·ln 1001) − 1)·50)` (0…500 ms); v > 0 delays the right channel,
  v < 0 the left. The delay is re-sized (and its ring cleared) when the rate or the control changes.
- **Phase:** `f = float(float(exp(float(1 − |v|/4096)·ln 5001) − 1)·4.398 + 10)` Hz (≈ 22 kHz … 10 Hz), clamped to the
  host's maximum frequency; the all-pass of §1 at f on the right channel for v > 0, the left for v < 0.
- **Render:** with order 0 the delay then the all-pass run on the input before the matrix, with order 1 after it.
- **Difference send:** when the destination is > 0 the input is copied first; after processing `input − output`
  (float) is **added** to the host buffer of that destination (host slot 37, two calls: get / release). EXEC with a
  send buffer (`py/t_diff.py`): the sent signal is exactly `float(in − out)` on all 36 distinct installation states
  that use a destination or a delay (destination forced to 1). In the mixer no installation state uses it; inside Patcher it is the
  destination bus (round 17 §2).

### 2.3 Verification **[EXEC]**
| Check | Cases | Result |
|---|---|---|
| Python model vs DLL, music, 44.1 kHz / 512 | 104 distinct states | 102 bit-identical; 2 differ only below −789 dB (denormals: the model has no FTZ) |
| LYNX C++ vs DLL (`t_cpp.py ss`) | 104 states × {44.1 kHz/512 music, 48 kHz/300 noise bursts, 96 kHz/512 saw, 22.05 kHz/64 loud} | **bit-identical on all** |
| LYNX's difference output vs the DLL's send buffer | 36 states | bit-identical |

## 3. Fruity Stereo Enhancer **[DECOMP + EXEC]**

### 3.1 State and controls
Record `0xD5`: 6 × `i32`, no version word (the first word is the pan). Ranges/defaults (EXEC): pan −128…128 (0),
volume 0…320 (256), stereo separation −96…96 (0), phase offset −512…512 (0), effects order 0…1 (1), phase inversion
0…2 (0 = none, 1 = left, 2 = right).

### 3.2 Formulas
- `pan = float(p₀)·1/128`, `sep = float(p₂)·1/128`; volume `x = float(p₁)/256`, `vol = 1` when x = 1, otherwise
  `float((exp(float(x·2.39789534f)) − 1)·double(0.1f))` — the PanOMatic law (11^x − 1)/10.
- `a = vol·(1 − sep·0.5)`; gains L←L = R←R = a, L←R = R←L = a·sep; then pan < 0 multiplies R←R and R←L by
  `pan + 1`, pan ≥ 0 multiplies L←L and L←R by `1 − pan`; inversion 1 negates L←L and L←R, 2 negates R←R and R←L.
  (Negative separation subtracts the opposite channel: wider; positive adds it: narrower.)
- Phase offset = a delay of `units = float(float(exp(float(|v|/512)·ln 1001) − 1)·50)` (0…500 ms) on the right
  channel for v > 0, the left for v < 0; it runs before the matrix with order 0, after it with order 1, and not at all
  without a delay. Gains ramp and fade in as in §1.
- **Rate quirk:** the delay length is computed with the rate known *when the control is set*; a later
  `FPD_SetSampleRate` updates the rate but does not re-size the ring. With the harness's usual order (state, then rate)
  a state loaded before a 48 kHz rate keeps its 44.1 kHz delay length. FL's own order of these two calls when it
  loads a project is **not confirmed**; LYNX uses the engine's rate (= FL when the rate is set before the state, and
  at 44.1 kHz either way).

### 3.3 Verification **[EXEC]**
| Check | Cases | Result |
|---|---|---|
| Python model vs DLL, music, 44.1 kHz / 512 | 30 distinct states | bit-identical on all |
| LYNX C++ vs DLL | 30 states × {44.1 kHz/512 music and noise bursts} | bit-identical |
| LYNX C++ vs DLL, state loaded after the rate (`late`) | 30 states × {48 kHz/300 bursts, 96 kHz/512 saw, 22.05 kHz/64 loud} | bit-identical |

## 4. Transient Processor **[DECOMP + EXEC]**

### 4.1 State and controls
Record `0xD5` (49 bytes): `i32` version (1; versions ≥ 2 are not read at all), 9 × `i32` parameters, a byte of three
flags, a `u32` flag and an `i32` — the last three only drive the editor's meters (scroll speed, display options)
**[DECOMP]**. Parameters (EXEC ranges): 0 attack, 1 drive, 2 release, 3 gain, 6 split frequency, 7 split balance
(0…160, default 80 except drive 0), 4 attack shape and 5 release shape (0…2 = Sharp/Mid/Slow, default 1), 8 on (0/1).
The wrapper sends parameters 0…7 to the engine as `float(clamp(raw/range, 0, 1))`; parameter 8 off makes the
render a plain copy.

### 4.2 Wrapper behaviour
- **Silence bypass:** each block is "silent" when every sample is within ±10⁻⁶ (by bit pattern: NaN counts as sound).
  A counter is reset to `Round(fs·1.5) + 88` by any non-silent block and decreased by n otherwise; when it is no longer
  above −n the block is copied through and the engine is not called (its state freezes) — about 1.5 s after the
  input goes quiet. The counter starts at 0, so a silent first block is already bypassed.
- **State load:** after the nine parameters the wrapper issues the engine call `0x3E9`, which applies every parameter
  at once; without it they would glide from 0.5 **[DECOMP + EXEC]**.
- Flush (dispatcher 2) and the render process mode showed no effect on the output **[EXEC]**.

### 4.3 Engine (single precision, FTZ/DAZ inside its process call)
- **Parameter smoothing:** parameters 0, 1, 2, 3, 6, 7 are queued with their target; every 8th sample each queued value
  moves `v += (target − v)·k` with `k = 1 − float(0.1^(200/fs))` and leaves the queue (taking the exact target) once
  within 10⁻⁴. Shapes (4, 5) apply at once.
- **Parameter laws** (x = normalised value): attack/release `u = ((x − 0.5) + x) − 0.5`, `q = ±u²` (sign of u); attack
  amount = `0.4·q` (q ≥ 0) or `0.6·q`; release amount = `0.6·q` (q ≥ 0) or `q`. Drive = `9x + 0.1` below 0.1, else
  `powf(15, (x − 0.1)·1.1111111)`. Gain = `powf(10, (x − 0.5)·0.9)` (±9 dB). Shapes 0.5/1/2 for x < 0.333 / < 0.666 /
  above. Split frequency `995x² + 5` Hz through `t = tanf(x²·(1/fs)·3125.8848 + (1/fs)·15.70796)`. Balance: dry
  `min(2(1 − x), 1)`, processed `min(2x, 1)`.
- **Split:** two first-order sections per channel from t: low-pass `y = d·t·x + d·t·x₁ − (t − 1)·d·y₁` and high-pass
  `y = d·x − d·x₁ − (t − 1)·d·y₁`, `d = 1/(t + 1)`.
- **Detector** on the high bands a (L) and b (R), all float: `m = (|b| + |a|)/2`; a peak `pk` (rise instantly, decay
  ×0.1^(5/fs)); an envelope `env` (attack coefficient `1 − 0.1^(1/(attack shape·0.01·fs))`, decay as pk); ratio
  `r = pk/env` (1 when env ≤ 0 or r < 1; with attack amount ≥ 0: 6.6 above 10, else `r·(0.96 − 0.003·r·r)`); a long
  peak (decay `0.1^(1/float(4fs))`), a hold (decay `0.1^(1/(release shape·0.1·fs))`), a slow follower moving towards the
  hold by `(q·c + (1 − c))` with `c = 0.1^(1/(release shape·0.02·fs))` and `q = clamp(3·(hold + 10⁻⁴ − slow)/(hold +
  10⁻⁴), 0, 1)` (q = 1 when release amount < 0 or hold ≤ 0); ratio `t = long peak / slow` (1 when slow ≤ 10⁻⁶ or
  t < 1; with release amount ≥ 0: 12 above 20, else `0.84t − t²·0.0006t`). Gain `g = (t − 1)·release + (r − 1)·attack`,
  then `g + 1` (g ≥ 0) or `1/(1 − g)`.
- **Drive/saturation:** `v = drive·(x·g)`; `s(v) = ((v² + 27)·v)/(9v·v + 27)` within ±3, ±1 outside; output
  `s(v)/drive` (× an output scale fixed at 1). The four detector parameters take their new value one sample late
  (`(new − old) + old`).
- **Output** per channel: `(dry·low + processed·saturated high)·gain`.
- The engine's CRT `pow`, `powf` and `tanf` matched double-precision `pow`/`tan` rounded to float on every value the
  tests exercised (the models and LYNX use the latter) **[EXEC]**.

### 4.4 Verification **[EXEC]**
| Check | Cases | Result |
|---|---|---|
| Python model vs DLL, music, 2 s, 44.1 kHz / 512 | 57 distinct states | bit-identical, or differing only in denormals (≤ −759 dB, after the signal's silent gap; the model has no FTZ) |
| LYNX C++ vs DLL | 57 states × {44.1/512 music, 48/300 bursts, 96/512 saw, 22.05/64 loud} | **bit-identical on all** |

## 5. Spreader **[DECOMP + EXEC]**

### 5.1 State and wrapper
Record `0xD5` (30 bytes): `u32` version `0x10003` (the loader subtracts `0x10000`), 5 × `i32` parameters, an `i32`
and two bytes of editor options. Ranges (EXEC): width 0…10000 (5000), separation −10000…10000 (0), low bypass
1000…1000000 (10990), mono bass 0/1 (0), enable 0/1 (1). The wrapper hands the engine `float(clamp(normalised, 0, 1))`
for width and separation, the raw 0/1 for mono bass and enable, and for low bypass a frequency: 0…500 Hz linearly
over the first half of the control, 500 Hz…10 kHz linearly over the second (through `exp(ln x)`), then at least 10 Hz.
Setting the sample rate re-computes the engine's constants **and clears it**; Flush clears it too.

### 5.2 Engine (`ildsp_x64.dll`, Faust-generated, single precision)
- **Constants:** `fs' = min(max(1, fs), 192000)`; smoothing pole `k = expf(−500/fs')`; `ω = π/fs'`; maximum delay
  `D = int(fs'·0.0063492064)` (≈ 6.35 ms); crossfade step `1/int(fs'·0.02321995)` (≈ 23.2 ms);
  a fixed 15 kHz one-pole pair from `tanf(47123.89/fs')`.
- **Smoothed controls** (one-pole `y = y·k + (1 − k)·x`, per sample): low bypass, enable, width, separation, mono bass.
- **Split:** per channel two cascaded TPT sections (`t = tanf(low bypass·ω)`, √2 damping) giving a high part
  `e − 2√2·u` and a low part, where `e = enable·x`.
- **Width:** the side signal `(low R + high L) − (high R + low L)` goes through a variable delay of `int(width·D)`
  samples with a linear crossfade between two read positions whenever the delay changes; filtered by the 15 kHz pair;
  a second line carries `high sum − low sum` the same way.
- **Output:** dry `(1 − enable)·x`, mono bass `(1 − mono)·low` per channel, a mid term and a side term scaled by
  `2·separation`, combined as L = `(dry L + mid) + (mono L + side)`, R = `((mono R + dry R) + mid) − side` — the exact
  association of every sum taken from the disassembly (the decompiler re-associates one of them).
- All controls start from 0 after a clear (so the effect fades in over ~2 ms).
- At 22.05 kHz the fixed 15 kHz section is above Nyquist: **FL's engine diverges to NaN** within ~70 samples on every
  state **[EXEC]**; LYNX reproduces the same samples and then replaces non-finite output samples by 0 (§6).

### 5.3 Verification **[EXEC]**
| Check | Cases | Result |
|---|---|---|
| Python model vs DLL, music, 2 s, 44.1 kHz / 512 | 25 distinct states | bit-identical on all |
| LYNX C++ vs DLL | 25 states × {44.1/512 music, 48/300 bursts, 96/512 saw} | **bit-identical on all** |
| LYNX C++ vs DLL, 22.05 kHz / 64, loud | 25 states | identical including the NaN positions (before LYNX's guard) |

---

## 6. What LYNX does with it

- New mixer effects `EffectKind::stereoShaper`, `stereoEnhancer`, `transientProcessor`, `spreader` (`core/StereoFx`),
  whose parameters are FL's controls over their ranges (editable in the LYNX UI, FL automation mapped one to one).
  The import uses them for every readable state; the earlier approximations (utility matrix / pass-through) remain
  for unreadable states.
- **Patcher:** a Stereo Shaper that splits by bus gives the exact engine on bus 0 and the same engine with its
  "difference" output (LYNX parameter 7 = input − output) on its destination bus — now also with delay and phase.
- Host blocks longer than 512 frames are split into calls of 512 (as in round 40). The DLLs ramp their gains per call,
  so a host block of 1024 in FL would ramp differently; FL's block size for these plugins is **not confirmed**.
- Initial state = a fresh plugin: Shaper/Enhancer gains fade in, the Spreader starts cleared, the Transient Processor
  starts with its parameters applied (as after FL's state load). `reset()` = FL's Flush (Shaper/Enhancer: gains jump,
  all-pass cleared; Spreader: cleared; Transient Processor: nothing).
- The engines run with FTZ/DAZ (as the DLLs do in FL) and are compiled without FMA contraction.
- Spreader: non-finite output samples (FL's own divergence at rates where its 15 kHz section passes Nyquist) are
  replaced by 0 so they cannot poison the mixer.
- Cost (one core, 44.1 kHz, 20 s of noise, including file I/O): 0.5–0.7 % of real time for each effect.

## 7. Open items

- FL's call order (sample rate vs state) at project load — matters only for a Stereo Enhancer with a delay at rates
  other than 44.1 kHz (§3.2). Not confirmed.
- FL's block size for these plugins when it exports (the gain ramps of Shaper/Enhancer are per call). Not confirmed.
- The Stereo Shaper's difference send to *mixer* tracks (no installation state uses it outside Patcher); LYNX has no
  effect-to-track sends.
- Spreader states older than version `0x10002` (a different layout; none in the installation).
