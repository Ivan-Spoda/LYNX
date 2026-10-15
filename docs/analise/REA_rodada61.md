# Behavioural analysis of FL Studio 2026: round 61 (REA) — EQUO and Pitch Shifter

> **Nature of this document.** First decoding of EQUO's engine (LYNX imported it until now as a flat parametric EQ,
> i.e. a pass-through) and of Pitch Shifter's engines (round 15 §2 read only its state and parameter texts; LYNX played
> it on its own generic pitch shifter). *Clean-room* reverse engineering, behavioural only. No Image-Line code or
> pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was opened; no Image-Line preset
> or audio data was copied into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r61/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/EQUO/EQUO_x64.dll` | `bef93304b279b0c28b05c2e442a25c638ef14d1e779693aa35d4a0f3ddb8bc8d` | EQUO (Delphi, its own DSP plus IPP calls into `Shared\dsp_ippv2_x64.dll`) |
  | `Plugins/Fruity/Effects/Pitch Shifter/Pitch Shifter_x64.dll` | `e7dc07f6a7c2562523e095429ed78d9361c2dbd6cfbd16966639db6944f4d10c` (= round 15) | Pitch Shifter front-end (Delphi, no DSP) |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` (= rounds 17, 54) | C++ DSP library holding both Pitch Shifter engines (exports `PSOLA_CreateInstance`, `GeneralPitchShifter_CreateInstance`) |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r61`, `gh/imp.sh` / `gh/dl.sh`, decompilations `out/equo.c`,
  `out/ps.c`), `objdump -M intel` (`asm/`) wherever the operation order or the precision matters.
- **EXEC harness:** `run/fx.c` → `fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu): round 50's effect harness (unmodified
  DLL, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state through the plugin's own
  SaveRestoreState, Dispatcher sample rate / block size / other messages (`disp=`), ProcessParam changes at given
  frames (`p=`), Eff_Render), extended in this round with `tick=N` / `tick0=N` (calls the plugin's **NewTick**, VMT slot
  14, before every N-th frame / N times after the set-up) and `ipp=` (resolves a doubly indirect function pointer).
- **Drivers (our own code):** `py/survey.py` (every EQUO / Pitch Shifter slot of the installation → `data/equo/`,
  `data/ps/`), `py/fx.py` (runs the harness, signals), `equo/model.py` (Python model of EQUO's parameter layer),
  `equo/t_par.py` (model vs the values and coefficients the DLL holds after the load), `equo/t_out.py` /
  `equo/t_syn.py` / `equo/t_tick.py` (LYNX's C++ engine `cpp/equocmp` vs the DLL), `equo/mkstates.py` (our own random
  test states), `equo/teststate.py` (the states and goldens of `tests/tests.cpp`); Pitch Shifter: `ps/t_cmp.py` (LYNX's
  engine `ps/cpp/pscmp` vs the DLL on the installation states), `ps/t_syn.py` (random states, rates, blocks, signals),
  `ps/t_auto.py` (mid-stream parameter changes), `ps/gpsmodel.py` (Python model of the granular engine), `ps/crt.py`,
  `ps/cpp/tanh.cpp`, `ps/cpp/expf.cpp` (run-time math replicas vs the DLL's own functions, called in place through the
  harness option `fcall=`), `ps/teststate.py` (states and goldens of `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

**Installation usage [DATA]:** EQUO in **10 mixer slots of 7 projects** (Bombs Away ×2, DEDZII ×3, the render test
`System/Render tests/EQUO.flp`, templates Breakbeat, Chillout, Chillout-Ambient, Rock); every state is version 4, no
oversampling, bandwidth 150, no send, all band targets "on"; morph 0, 4000 or 7000; band mix 128, 68, 37 or 71; volume
128, 173 or 148. Pitch Shifter in **15 mixer slots of 7 projects** (§7).

---

## 1. EQUO: plugin and state

### 1.1 Entry points **[DECOMP]**

`CreatePlugInstance` (`0x6cca60`) builds the class whose VMT is at `0x6cc8e0` (constructor `FUN_006cd420`): slot 1
Dispatcher `FUN_006ccc80`, 3 SaveRestoreState `FUN_006cd0a0`, 4 GetName `FUN_006cec50`, 6 ProcessParam `FUN_006ce840`,
7 Eff_Render `FUN_006cf0a0`, 14 NewTick `FUN_006cf020`; parameter text `FUN_006cfbf0`.

### 1.2 State layout (version 4, 4143 bytes; version 3 lacks the last field) **[DECOMP `FUN_006cd0a0` + DATA]**

| Offset | Size | Content |
|---|---|---|
| 0 | i32 | version (3 or 4 are read; < 3 is ignored) |
| 4 | 31 × 3 × (f32, i32) | per band and target (volume, panning, send): the edited value and its **mode** — 0 on (taken from the banks), 1 off, 2 locked (the stored value is used as is) |
| 748 | 9 × 31 × 3 f32 | the **banks**: banks 1…8 (morph targets) and the **master** bank (index 8); normalised values 0…2 |
| 4096 | 5 × i32 | the knobs (§1.3) |
| 4116 | 5 × i32 + 3 × u8 | 2× oversampling (0/1), bandwidth (80…500, default 150), send track (−1 = none), two editor fields, then *send only*, *add mode* and an editor flag |
| 4139 | i32 (v4) | editor option (bit 0) |

Knobs (control ranges from the DFM, **[DATA]**): 0 band shift −12000…12000, 1 band mix −128…128 (default 128),
2 morph 0…7000, 3 motion smoothness 0…1024 (default 170), 4 output volume 0…256 (default 128). Automatable parameters
5 + k address one bank cell: bank = k / 93, then target = (k mod 93) / 31, band = k mod 31; the value is
`raw / 2^29`, stored only when the cell's mode is 0 (and copied to the edited value when it is the bank being edited).

### 1.3 Derived values **[DECOMP `FUN_006ce220`, `FUN_006cda50`, `FUN_006cd980`, `FUN_006cd940`, `FUN_006ccb70`, `FUN_006ccc00` + asm + EXEC]**

- `shift = float(knob0 · 0.0025)` bands, `mix = 1 − float(knob1)·0.0078125` (float), `morph = float(knob2 · 0.001)`.
- Value laws: volume and send `g = float(float(e^{v·ln 3} − 1) · 0.5)` (v = 1 → unity, 2 → ×4); panning `v − 1`.
  The inverse (`ln(float(g/0.5) + 1)/ln 3`, `p + 1`) is used for the morph.
- For band b, target t: mode 1 → the law of 1.0; mode 2 → the law of the edited value; mode 0:
  1. `pos = max(b − shift, 0)` (float), `i = trunc(pos)`, `fp = pos − i`; `bank = trunc(morph)`, `fm = morph − bank`.
  2. a bank's value: `law(v + (1 − v)·mix)` of cell `min(i, 30)`, linearly interpolated (in the law's domain, float)
     towards cell `min(i + 1, 30)` by `fp` when `fp ≠ 0`;
  3. with `fm ≠ 0` the two banks' values are taken back to the normalised domain, interpolated by `fm` and re-applied;
  4. the master bank's cell (unshifted, unmixed) through the same law: panning adds it and clamps to ±1, the other
     targets multiply.
- **EXEC (`t_par.py`):** the 93 values of every installation state and of 18 synthetic states equal the model bit for
  bit.

### 1.4 Bands **[DECOMP `FUN_006ce050`, `FUN_005dc090`, `FUN_005dbac0`, asm + EXEC]**

- Processing rate `R = rate · F`, `F = 2^order` (§2.2). Centre `f_b = float(e^{(b/3)·ln 2} · 20)`, b = 0…30
  (20 Hz…20.48 kHz, third-octave). A band with `f_b ≥ float(R/2)` is off. A band with `f_b > float(F·22100/2)` is
  **doubled**: designed at half its frequency and run on a sample-and-hold ×2 copy of the signal (§2.3).
- Design: the RBJ band pass of Parametric EQ 2 (round 50 §2.3, internal type 2) with `ω = f·((1/R)·2π)`,
  `α = sin ω · sinh(((ln2/2 · BW)·ω)/sin ω)`, `BW = float(float(bandwidth/100)/3)` octaves (0.5 by default);
  `b0 = d·α`, `b1 = 0`, `b2 = d·(−α)`, `a1 = (d·cos ω)·(−2)`, `a2 = d·(1 − α)`, `d = 1/(α + 1)`.
- Normalisation `norm = float(e^{float(0.115129255f·(−3))}) / float(bandwidth/100)` (−3 dB over the width: 0.47196
  at 150).
- The run-time library's sin/cos (an fdlibm-style kernel) match the C library's `sinl`/`cosl` rounded to double on
  every coefficient that reaches the audio checks of §5; an earlier double-precision `sin` differed by one ulp in one
  band of 31 (**EXEC**).

### 1.5 Filter **[DECOMP `FUN_005dc5e0` (asm)]**

Per band, interleaved stereo in double: `y = x·b0 + ((x₂·b2 − y₂·a2) − y₁·a1)` (the zero b1 term is not computed),
x = the float input widened; states (x₁, x₂, y₁, y₂ per channel) kept in double with no clean-up, output rounded to float.

---

## 2. EQUO: render

### 2.1 Block **[DECOMP `FUN_006cf0a0`]** — MXCSR `0x9F80` (FTZ + DAZ, set at entry)

1. **Silence gate:** the block is silent when every sample is within ±1e−6 (bit-pattern compare). A counter is reset to
   `Round(rate/5)` by a non-silent block and decreased by n otherwise; when it is ≤ −n the input is passed through
   unchanged and the "snap" flag is set (§2.4). The counter starts at 0.
2. With oversampling (state field ≠ 0) the input is up-sampled (§2.2).
3. Main sum buffer: zeros, or a copy of the input in **add mode**.
4. Bands 0…30 in order: a band is processed when it is on and its volume or panning target is not "off" (or its send
   target is on and a send track is set). Filter (§1.5), or for a doubled band: each frame duplicated, filtered at 2n,
   pairs averaged `(a + b)·0.5f`.
5. Unless *send only*, a band whose volume target is not off is added to the sum through its gain stage (§2.3).
6. Send: the same with the send target into a second buffer handed to the host's track `send + 1` (**not routed by
   LYNX**; no installation state uses it).
7. Output: the sum (down-sampled with oversampling); *send only* leaves the input in place.
8. Output volume ramp (§2.4).

### 2.2 Oversampling and FL's render mode **[DECOMP `FUN_006ce760`, `FUN_006ca720`, `FUN_006ca820`, `FUN_006ca8c0`, kernels `FUN_006ca2f0`/`FUN_006ca4c0` + EXEC]**

- The resampler is the shared polyphase half-band of Parametric EQ 2 (round 50 §2.7: same coefficients, read from the
  running DLL; same clean-up of the 12 all-pass outputs below 2⁻²⁴), one stage per order.
- `order` = the state's oversampling field, except that the Dispatcher's **process mode with bit value 2** (FL's
  "HQ non-realtime" flag) forces order 1. `F = 2^order` scales the design rate (§1.4), the gain step (§2.4) and the
  doubling threshold.
- But the signal is only resampled when the **state's** field is > 0. With the render flag and the field at 0 (every
  installation state), the bands are designed for 2·rate and run at the host rate: every band sounds an octave higher
  and the top bands' doubling threshold moves to 22.1 kHz **[EXEC: reproduced bit for bit, `disp=1,2`]**. LYNX applies
  this while it exports (as for the other plugins with FL's render mode, round 51 §2.9); whether FL sends that flag
  during an export was not observed (not confirmed).

### 2.3 Gain stage **[DECOMP `FUN_005db690`, `FUN_005e0d20`, `FUN_005e0e20` (x87 asm), `FUN_005df4f0`, `FUN_005df6c0`; IPP `ippsAdd_32f_I`]**

- Level `L0 = vol · norm` (float), pan p: for p ≥ +0 (sign bit clear) `R = L0`, `L = (1 − p)·L0`, else `L = L0`,
  `R = (p + 1)·L0`; the product is computed on the x87 stack and stored to float once (LYNX: in double, exact).
  Add mode subtracts `norm` from both.
- Ramp per channel: `start = now`, `inc = target − start`; if non-zero, `now = target`, `inc /= n`, and when
  `|inc| > step` it is clipped to ±step, `now = start + inc·n`, and a resulting `|now| < 2⁻²⁴` becomes 0 with
  `inc = −start/n`. `step = float((44100/R)·0.001)`.
- No ramp: skipped for zero gains, plain add for (1, 1), else `dst = src·g + dst`. Ramp: the per-frame gains are
  running float sums — even and odd frames on separate sums stepping by `inc + inc` in groups of eight frames, the
  remaining frames one by one from the even sum.
- Each band's gain state starts at **0** after the plugin is created (the first processed block fades in, at most
  `step` per frame); the snap flag of §2.4 sets it to the target instead.

### 2.4 Output volume, snap, Flush **[DECOMP `FUN_005db7e0`, `FUN_006ce840`, Dispatcher id 2, `FUN_006ce3c0`]**

- Volume `x = float(knob4)·0.0078125`; `x = 1` → unity, else `float((e^{float(x·2.39789534f)} − 1)·float(0.1))`.
  Same ramp with `step/F`; 0 → silence, 1 → unchanged, else a multiply (both channels on one ramp).
- The snap flag is set by a passed-through silent block and by the Dispatcher's Flush (id 2), which also clears the
  band filters (not the resamplers) and sets every value in use to its target. With the flag, the next processed
  block starts every gain at its target.

### 2.5 Motion smoothness (NewTick) **[DECOMP `FUN_006cf020`, `FUN_006ce480`, `FUN_005d92f0`, `FUN_005d9850`, Dispatcher id 20 + EXEC `tick=`]**

- The values of §1.3 are targets; the gain stages use a second copy that only changes in **NewTick** (once per song
  tick) or with Flush/load (set to the targets). In NewTick each value goes through a one-pole in double
  `s = target + (s − target)·c`, `c = e^{ln 0.01 / max(1, T)}`,
  `T = float(float(e^{float(|knob3|·(1/1024))·ln 11} − 1)·40000 / float(samples per tick))`; T ≤ 0 → no smoothing.
- Samples per tick come from the Dispatcher id 20 (a float; the constructor's default is 196.875 = 44100·60/(140·96),
  i.e. FL's default tempo at 96 ticks per beat).
- The smoother states start at 1, 0, 1 (volume, panning, send) when the plugin is created and are **not** reset by a
  state load: the first NewTick after a load pulls every value towards those and then back to the targets
  **[EXEC: `tick0=`]**. Without NewTick the loaded values are used as they are.
- So with a static state the sound only depends on NewTick right after a load. Whether FL calls NewTick while the
  transport is stopped (which would settle the smoothers long before an export) was not observed (not confirmed).

---

## 3. What LYNX does with EQUO

- **`EffectKind::equo`** (`core/EquoFx`): the slot keeps FL's raw state in `EffectSlot::state`; the five knobs and four
  options (bandwidth, oversampling, add mode, send only) are slot parameters (`(raw − min)/range`), so the mixer's
  generic editor shows and edits them and FL automation of knobs 0…4 maps onto them. Bank-cell automation (FL
  parameters ≥ 5) is not mapped.
- The engine runs FL's 512-frame blocks, FTZ/DAZ, no FMA contraction (the build's native-CPU zig produced −140 dB
  until contraction was switched off in the file).
- NewTick: called once per song tick from the host tempo at 96 ticks per beat (FL's default; the project's PPQ is not
  passed to effects — not confirmed for other PPQ values), between blocks. The smoother states start at the loaded
  values (no glide after a load; inference from the paragraph above).
- FL's render flag (§2.2) follows LYNX's offline export flag.
- The send output is not routed; a band whose only active target is the send is not processed.
- Fallback: a state that is not EQUO's (wrong version or size) keeps the old flat parametric EQ.

## 4. EQUO verification **[EXEC]**

LYNX's engine (`cpp/equocmp`, the sources of the app; built with g++ and with zig c++ 0.13 for the native CPU) against
the DLL:

| Check | Cases | Result |
|---|---|---|
| Every installation state, music 44.1 kHz / 512 | 10 | bit-identical |
| Every installation state, noise 48 kHz / 300 | 10 | bit-identical |
| Every installation state, render flag (`disp=1,2`) | 10 | bit-identical |
| Synthetic states (`mkstates.py`: shift ±, mix < 0 / ½, fractional morph, master bank, off/locked targets, bandwidth 80/500, volume 0, oversampling 1 and 2, add mode, send only, version 3) × music 44.1/512, noise with silent gaps 48/300, render flag with blocks of 333, Flush | 18 × 4 | bit-identical |
| Rates 22.05, 32, 48, 88.2, 96, 192 kHz, plain and render flag | 5 states × 6 rates × 2 | bit-identical |
| NewTick from the constructor's smoother states (`tick=256`) | 4 states | bit-identical |
| Knob changes (shift, mix, morph, volume, smoothness 600 → 0) with NewTick every 128 / 600 frames, without NewTick, and with samples per tick = 300 (Dispatcher 20) | 5 states × 3 + 2 | bit-identical |
| Unit test `equo_exact` (`tests/tests.cpp`): our own LCG states — plain, oversampling + add mode + bandwidth 300, render flag, NewTick + morph change; FL import → mixer effect with odd host blocks | goldens from the DLL | bit-identical |

---

## 5. Pitch Shifter: plugin and engines

### 5.1 Where the DSP lives **[DECOMP + EXEC]**

Round 15 read the state (§2.1 there, confirmed) and the parameter texts. The plugin itself has no DSP: it loads
`Shared\ildsp_x64.dll` and creates two engines through its exports — **`PSOLA_CreateInstance`** for the UI's "FFT"
mode and **`GeneralPitchShifter_CreateInstance`** for "GRANULAR". So the "FFT" mode is not a phase vocoder: it is a
pitch-synchronous overlap-add shifter whose pitch detector uses an FFT. Intel IPP is linked statically into ildsp and
is used only for that detector's FFT, in **double** precision (a plain double radix-2 FFT gives identical results).

Plugin entry points (VMT table at file offset `0x32af20`): Dispatcher `FUN_0072bcb0`, SaveRestoreState
`FUN_0072c630`, ProcessParam `FUN_0072cda0`, parameter apply `FUN_0072d0f0`, formant mix `FUN_0072d020`, Eff_Render
`FUN_0072de80`, latency report `FUN_0072dfa0` (host message 0x1e).

### 5.2 What the plugin sends to the engines **[DECOMP]**

- Every parameter goes to **both** engines as `float((raw − min)·(1/(max − min)))`, clamped to 0…1, then scaled:
  pitch `(n − 0.5)·24` semitones; duration and density `raw/100`; delay `n·999 + 1` ms, or `n·16` steps with tempo
  sync; channel, delay mode and pitch mode `raw + 1`. Only the selected engine is processed.
- With formant correction below 100 %, the formant sent is `clamp(formant_st + pitch_st·(1 − correction), −12, 12)`.
- The PSOLA engine receives the detector size: 1024 (fast) or 2048 (accurate). The tempo comes from ProcessEvent 0.

### 5.3 PSOLA ("FFT") engine **[DECOMP `ildsp` set-rate `FUN_18017f700`, set-param `FUN_1801804b0`, process `FUN_180180830`, helpers `FUN_180181590`, `FUN_180181730`, `FUN_180181d60` + EXEC]**

- Input → DC-blocking high pass (a per-rate biquad).
- A zero-crossing-rate voicing fade (threshold 0.6, step 66.67/rate) splits the signal into voiced and unvoiced parts.
- Voiced, unvoiced and dry paths are delayed by `int(frame·1.2)` samples: this is the latency the plugin reports
  (2457 at 44.1 kHz with the accurate detector).
- Detector: a frame of `int(size·rate/44100)` samples with a sin² window, zero-padded FFT of 4096 (8192 above 90 kHz),
  magnitudes `(|X|²)^0.1`; up to 100 parabolic peaks between 60 and 1500 Hz; candidates scored with a cosine ×
  Gaussian harmonic score. The detected frequency is smoothed per sample with coefficient `rate·1.1338·10⁻⁷` and
  clamped to 60…1500 Hz.
- Analysis marks one period apart; synthesis grains spaced `period/ratio` (relative pitch mode), or at fixed
  130.8 / 261.6 / 523.3 Hz (absolute modes C4…C6).
- Grains: up to 20 voices, two-period sin² windows, 4-point cubic interpolation read at the formant ratio.
- Output: grains + delayed unvoiced part → soft clip through a 1001-point tanh table → `wet·mix + (1 − mix)·dry`
  (dry delayed by the latency).

### 5.4 Granular engine **[DECOMP `FUN_180161fc0`, `FUN_180162760`, `FUN_180162f90` + EXEC + Python model]**

- One-pole anti-alias with `c = min(2 − ratio, 1)` into a 0.2 s ring buffer.
- Grain spacing `int(length/density + 0.5)` with jitter; a grain starts `(ratio − 1)·length` back plus a random
  position offset.
- Same window, interpolation and soft clip as the PSOLA engine; mixed against the undelayed dry signal; latency 0.
- Random source: an MSVC-style LCG (multiplier `0x343fd`, increment `0x269ec3`, seed 233994) held in **one global of
  ildsp**, so in FL every Pitch Shifter instance in the process draws from the same sequence. LYNX gives each
  instance its own generator starting at 233994, which equals FL only for a single instance in a fresh process (not
  reproducible in general: it depends on how many instances FL created and in which order they rendered).

### 5.5 Delay **[DECOMP + EXEC]**

Both engines: a 4.01 s delay line, delay-time smoothing coefficient 4.41/rate, feedback through three per-rate high-pass
biquads; modes post, pre and delay-only. The PSOLA engine subtracts its latency from the delay time in post/pre modes.
No installation state uses a delay mode (synthetic states only).

### 5.6 Run-time math **[EXEC: `fcall=` against the DLL's own functions]**

The soft-clip table is built with the C run-time's single-precision `tanhf`, which is not correctly rounded (27 of the
1001 table points differ from correctly rounded tanh), and the detector uses the run-time `expf`; LYNX re-creates both
from their behaviour (0 mismatches in 250 000 / 350 000 calls). `powf`, `pow` and `cos` rounded to float equal the C
library on the ranges used. The engines do not set MXCSR; denormal flushing only matters for denormal values.

## 6. What LYNX does with Pitch Shifter

- **`EffectKind::flPitchShifter`** (`core/PitchShifterFl`): slot parameters [0…12] are FL's 13 knobs
  `(raw − min)/range` (the same normalisation FL's automation uses, so automation of knobs 0…12 maps one to one),
  [13] engine (granular), [14] accurate detector, [15] delay tempo sync. Changing an option reloads the engines as FL's
  state load does; knob changes go to the engines as FL's ProcessParam does. Latency goes to LYNX's PDC.
- Tempo: the host tempo (FL's ProcessEvent 0).
- Fallback: a state that cannot be read keeps LYNX's generic pitch shifter (round 15 mapping).

## 7. Pitch Shifter verification **[EXEC]**

Installation usage **[DATA]**: 15 mixer slots of 7 projects (Astes ×3, Bombs Away ×3, Edlan & Ella Noël ×3, LollieVox ×2,
Mehran Abbasi, Sid De La Cruz ×2, the render test `System/Render tests/Pitch Shifter.flp`): 8 granular, 7 PSOLA; no
delay mode.

| Check | Cases | Result |
|---|---|---|
| Every installation state, music 44.1 kHz / 512 | 15 | bit-identical |
| Every installation state, noise 48 kHz / 300 | 15 | bit-identical |
| Random states (both engines, 22.05…192 kHz, blocks 1/300/512/777/1024, music/noise/voice signals, all delay modes, tempo sync, pitch modes, channels, both detectors) | 70 | bit-identical; reported latency identical |
| Mid-stream parameter changes (8 per state) | 15 | bit-identical |
| Python model of the granular engine (0.5 s per state) | 8 granular states | bit-identical |
| zig c++ build | 15 | bit-identical |
| Unit test `pitch_shifter_exact`: our own PSOLA and granular states through FL import → mixer effect, odd host blocks, latency | goldens from the DLL | bit-identical |

Cost (one core): PSOLA ≈ 2.5 % of real time, granular ≈ 0.5 %. The DLL's output does not depend on the host block size.

---

## 8. Open items

- EQUO: whether FL sends the "HQ non-realtime" process mode during an export (§2.2) and calls NewTick while stopped
  (§2.5) — not observed; the send output to another mixer track is not routed; bank-cell automation (FL parameters ≥ 5)
  is not mapped; FL's ticks per beat for projects with a PPQ other than 96.
- Pitch Shifter: the shared random generator across instances (§5.4); at 192 kHz with the accurate detector (or rates
  between 88.2 and 90 kHz) the detector frame is longer than the FFT and overruns the plugin's buffer — LYNX transforms
  only the first N samples (three such cases matched, the overrun itself not decoded); the tempo message was not
  exercised in the harness (only 120 BPM); FL's behaviour on Flush was not observed (LYNX's reset clears the history).
