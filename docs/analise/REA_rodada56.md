# Behavioural analysis of FL Studio 2026: round 56 (REA) — Fruity Delay Bank and Fruity Multiband Compressor: exact engines

> **Nature of this document.** First decoding of both plugins (round 7 only listed them; round 22 §1.5 noted that the
> Multiband Compressor has no side input). Goal of rounds 40/48: describe both engines precisely enough for LYNX to run
> them as exact mixer effects instead of the generic delay / single-band compressor approximations. *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No FLEX pack was opened; no Image-Line preset, audio or wave data was copied into the repository
> (the unit-test goldens are a handful of output samples of the DLLs for input signals and states of our own).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Results of our own models (which
> implement only DECOMP/EXEC facts) are marked **[model]**; open points are marked **not confirmed**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code (Ghidra 12.1.4 headless + `objdump -M intel` where the float order matters) |
| **[DATA]** | checked in `.flp`/`.fst` files of the installation, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r56/`
  (the Multiband part under `mb/`).
- **Targets** (copies in `bin/`, all under `Plugins/Fruity/Effects/`):

  | File | SHA-256 | Role |
  |---|---|---|
  | `Fruity Delay Bank/Fruity Delay Bank_x64.dll` (Delphi, plugin VMT `0x6ddc30`) | `af6b73a977754a2e6ccb3978afbbc8501b295a0bf885bc494da9ea6bfda63caf` | the whole Delay Bank (its own DSP) |
  | `Fruity Multiband Compressor/Fruity Multiband Compressor_x64.dll` (Delphi, plugin VMT `0x6d3db0`) | `c8d352d6fbed135a2c9ea3151abd6c42741e1097ca6d26c4b7bb506677b04540` | wrapper: state, controls, metering |
  | `Fruity Multiband Compressor/comp_eng_x64.dll` (MSVC C++, export `CreateEngine`; RTTI `MultiBandComp`, `Band`/`HighBand`/`MidBand`/`LowBand`, `LPF`/`HPF`, `FirFilter`/`FirLpf`/`FirHpf`/`FirBpf`, `Comp`, `Env`, `Limiter`, `LimEnv`) | `d307e696053ba72745792ceb17a1ab8bc56b678ab115339d31818d4687222a13` | the Multiband's whole DSP |

  The Delay Bank calls no shared Image-Line DLL for its DSP (unlike Reeverb 2, round 49); its function-pointer globals
  resolve to its own kernels, plus `ippsZero_8u` / `ippsMove_8u` from `Shared/dsp_ippv2_x64.dll` (EXEC, `py/fpmap.py`).
  The Multiband wrapper loads `comp_eng_x64.dll` from its own folder.
- **Tools:** Ghidra headless with auto-analysis (`gh/imp.sh`; dumps `out/DelayBank_dec.c` 0x6c0000…0x6f8000,
  `out/DelayBank_dsp.c` 0x5d9000…0x5e2000, `out/Multiband_dec.c`, `out/comp_eng_dec.c`; `gh/scripts/DecList.java` for
  scattered helpers), `objdump -M intel` (`out/db_plug.s`, `out/db_dsp.s`, `out/mb_plug.s`, `out/ce.s`).
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `run/db.c` → `db_*.exe`, round 52's `dl.c` (tempo event,
  late state load, engine-DLL function calls) extended with `gp=` (dump through a pointer global), `fsc=` (call an
  internal `void f(double, double*, double*)`), `save=` (the plugin's own SaveRestoreState into a file) and a log of
  MXCSR / the x87 control word. Same safety measures as before (DLL loaded unmodified from the installation,
  `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`).
- **Drivers (ours, `python3 -I`, under `py/`):** `survey56.py` (states → `data/states56.pkl`), `db.py` (harness driver,
  signals), `fpmap.py` (function-pointer globals), `rtl.py` (the Delphi RTL's Exp/Ln/Cos/SinCos/Power against libm),
  `mkref.py` (DLL outputs of every Delay Bank state, cached in `data/ref/`), `t_all.py` (LYNX vs DLL on them),
  `t_ev.py` (control and tempo changes during the render), `t_rand.py` (random states), `isolate.py` / `vary.py`
  (localising a difference), `gold.py` (unit-test goldens); C++ `cpp/dbcmp.cpp` (LYNX's engine on a float file, with
  control/tempo events and FTZ/DAZ), `cpp/coef.cpp` (LYNX's filter coefficients). The Multiband's drivers are listed
  in §2. Logs in `out/`.

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 1. Fruity Delay Bank

### 1.1 Data survey **[DATA, `py/survey56.py`]**

| | Demo-project slots | Template slots | Presets (`.fst`) | Unique states | Size / versions |
|---|---|---|---|---|---|
| Fruity Delay Bank | 27 in 7 files | 6 in 6 files | 21 | 52 | 792 B; version 16 ×36, 15 ×18 |

Among the 106 enabled banks of those states: filter 1 types 1/2/4/5/7 (orders 0 and 2), filter 2 types 1/2/3/4/5/7
(orders 0 and 1), filter 1 after the delay in 10, modes 1 ×34, 2 ×5, 3 ×67, gain patterns in 9, the tap before the
feedback stage in 19, unsynced times in 23, a send to the next bank in 22, a stereo offset in 39; 2 states use the
oversampling option. Four states are silent as stored (dry, output or input at 0: the songs automate them).

### 1.2 State **[DECOMP `FUN_006de180` + DATA]**

u32 version (15 or 16; lower versions are ignored), then a block of 133 × i32 copied raw (`P+0x120`), then for each of
the 8 banks 32 bytes:

| Offset in the bank record | Size | Meaning |
|---|---|---|
| 0, 4 | i32, i32 | filter 1 type (0 off, 1…7), extra sections (0…) |
| 8, 12 | i32, i32 | filter 2 (feedback) type, extra sections |
| 16 | u8 | filter 1 after the delay |
| 17 | i32 | mode: 0 no delay, 1 normal, 2 outputs swapped, 3 ping-pong (crossed feedback) |
| 21, 25 | i32, i32 | gain pattern: steps per delay time (0 off), shape −128…128 |
| 29, 30 | u8, u8 | bank on, output tapped before the feedback stage |
| 31 | u8 | tempo sync |

The 133 controls: 0 dry, 1 output, 2 input, 3 feedback (globals, 0…128); `4 + 16·b + k` (k = 0…15) the bank controls
(table below); 131 the solo bank (−1 none), 132 the oversampling order. Only 0…130 are automatable (`0x83`
parameters; bank 8's "send to next" does not exist). **Version 15** stored the bank times ten times shorter: the loader
multiplies control `16·b + 12` by 10. After the copy the plugin re-applies every control through its own
ProcessParam and resets its state.

| k | Control | Range | Law **[DECOMP `FUN_006e0000` + objdump]** |
|---|---|---|---|
| 0 | input pan | −128…128 | `v/128` |
| 1 | input volume | −160…160 | signed volume law: `L(|x|)·sign(x)`, `x = v/128`, `L(x) = 1` at x = 1, else `float((e^{x·ln11_f} − 1)·0.1f)` (Delay 2's law); × global input |
| 2–4 | filter 1 cutoff, resonance, gain | 0…65536, 0…65536, 0…256 | §1.5 |
| 5 | feedback pan | −128…128 | `v/128` |
| 6 | feedback level | −128…128 | `v/128`; × `(cross·0.5 + 1)` when the cross amount is negative; × global feedback (`v/128`) |
| 7 | feedback cross | −128…128 | `v/128` (§1.4) |
| 8 | time | 0…7680 | `T = float(v·rate)`; synced: `float(T·0.03125/tempo)` (1920 = one beat); free: `T/3840` (1920 = 0.5 s); delay = Round(T), at least 1 |
| 9 | stereo offset | −512…512 | `ms·100 = float(float(e^{float(|v|·2^-9)·ln1001} − 1)·50)`, `samples = Round(float(ms·100·rate)/100000)`; v ≤ 0 delays the left input, v > 0 the right |
| 10–12 | filter 2 cutoff, resonance, gain | as 2–4 | §1.5 |
| 13 | output pan | −128…128 | `v/128` |
| 14 | output volume | −160…160 | signed volume law; × global output; 0 for every bank but the solo one when a solo is set |
| 15 | send to the next bank | −160…160 | signed volume law |

Rate is the (oversampled) rate; the tempo comes from the host's tempo event (`ProcessEvent` id 0, a float). Round is
round-half-even everywhere (`cvtsd2si`). Globals 0–2 use the unsigned volume law. The new instance's state (saved by
the DLL itself, EXEC) has bank 1 on (mode 1, synced, time 1440, feedback 64) and the other banks off.

### 1.3 Signal path per host block **[DECOMP `FUN_006e1380` + objdump of every kernel]**

Interleaved stereo float buffers; n = block length (× the oversampling factor).

1. **Oversampling (order k > 0):** k stages of a 2× half-band polyphase IIR (two branches of three first-order
   all-passes, `y = (x − y₋₁)·c + x₋₁` in double, coefficients 0.14737711360104661, 0.48246854276970014,
   0.88300502576937312 on the first branch and 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 on the
   second, the second branch giving the earlier output sample); down-sampling feeds the two phases to the branches and
   outputs `(B + A)·0.5`. The six all-pass outputs (not the inputs) are set to 0 below 2⁻²⁴ after every block. The
   dispatcher's process-mode call with the "HQ non-real-time" flag forces one stage **[DECOMP `FUN_006ddda0`]**.
2. **Dry:** `out = in·dry`.
3. **Each bank that is on**, in order 1…8 (a bank that is off breaks the send chain):
   - input matrix (§1.4, ramped) from the input into the bank buffer; if the previous bank's send is non-zero, the
     previous bank's processed buffer × send is added;
   - filter 1 if it is set to "before";
   - **delay line** (mode ≠ 0): the bank buffer is added into the line at the write position + the channel's offset;
     then, in segments that never pass the write position (at most the delay length, or to the end of the ring): if the
     tap option is set the read frames are copied to the bank buffer; filter 2 runs on the read frames in the line; the
     feedback matrix (§1.4, ramped per segment) is applied in place; samples below 2⁻²⁴ become 0; without the tap
     option the frames are copied now; each read frame is added into the write position and cleared; mode 2 swaps the
     channels of the copied segment. So the feedback loop delay is the time, the offset only delays the input;
   - **gain pattern** (§1.6);
   - filter 1 if it is set to "after";
   - output matrix (§1.4, ramped) **added** into the output.
4. Down-sampling (k > 0) or copy to the host buffer.

The line holds `host block·factor + offset + max(delay, 1)` frames. It is re-allocated (and cleared, read at 0, write
at the delay) only when that size changes; a time change that keeps the size keeps the positions **[DECOMP
`FUN_006d8f30`]**. The pattern position and the filter states are kept across control changes.

### 1.4 Gain matrices and ramps **[DECOMP `FUN_005dbbb0`, `FUN_005dbd30`, `FUN_005e1220`, kernels `0x5e0290`/`0x5e0440`]**

Four floats `c = {LL, RR, RL, LR}`: `outL = L·c0 + R·c2`, `outR = R·c1 + L·c3` (float).

- Pan law (x87 `fsincos` under control word `0x037F`): `L = cos((1 + p)·π/4)·(v·√2_f)`, `R = sin(…)·(v·√2_f)`,
  extended precision, stored as floats. LYNX executes the same instruction (a library `cosl` differs from `fsincos`
  at π/2 by ~10⁻²⁰, visible only as the 10⁻¹⁷ cross term of a hard pan).
- Input / output matrix from pan p and volume v: p = +0 → `{v, v, 0, 0}`; v = 0 → zeros; otherwise the pan law, then
  `a = 1 − L/v` (p > 0, for the right side) or `1 − R/v` (p < 0, for the left side), halved, and the panned-away share
  is moved to the cross term: `c2 = c0·aₗ, c0 = c0·(1 − aₗ)`, `c3 = c1·aᵣ, c1 = c1·(1 − aᵣ)`.
- Feedback matrix: the same with pan = feedback pan, volume = level·global, plus the cross amount b: when b ≠ 0,
  b > 0 is mapped to `float(e^{b·ln5} − 1)·0.25f` (the run-time `Ln(5)` is one ulp above the correctly rounded
  value, EXEC), b is added to both shares, each clamped to 1. Mode 3 swaps `c0↔c3`, `c1↔c2` (ping-pong).
- Input / output matrices are recomputed only when one of their controls changed; the feedback matrix every block.
- **Ramps:** each coefficient moves linearly from its current value to the target over the block (the feedback
  matrix over each loop segment), the per-sample step limited to `float(44100/rate·0.001)`; a ramp that would end
  below 2⁻²⁴ in magnitude ends at 0. The coefficients step by float additions per frame. A new instance starts every
  coefficient at 0 (the state load's reset copies targets that are not computed yet), so FL fades a new Delay Bank in
  over ~1000 samples.

### 1.5 Filters **[DECOMP `FUN_006debc0`, `FUN_005dc180`, kernel `0x5dc9a0` + EXEC]**

Type → kernel: 1 low-pass, 2 band-pass, 3 notch, 4 high-pass, 5 low shelf, 6 peaking, 7 high shelf (cookbook
forms in double). Each filter is `order + 1` identical biquad sections in series (direct form I in double, input
converted from float, output rounded to float per section, `y = x·b0 + (((x₁·b1 − y₂·a2) + x₂·b2) − y₁·a1)`; the
eight state doubles are set to 0 below 2⁻²⁴ after every call).

- Cutoff `c = v·2⁻¹⁶`: `f = float(e^{float(c)·ln1501} − 1)·K` with K = 0.00066466664 (low-pass) or 0.00039993334
  (others), capped at 0.6001 (high-pass) or 1.0001; `w = (f + 0.003 | 0.0001)·2π·44100/rate`, clamped to
  [0, 2.849517146113191].
- Low/high-pass: `Q = 0.1` (r ≤ 0), 50 (r ≥ 1), else `float(e^{float(r)·ln2001} − 1)·0.02495 + 0.1`; with extra
  sections `Q^{1/(n+1)}` (n+1 = section count); alpha = `sin w·0.5/Q`.
- Band-pass / notch: bandwidth 0.01 / 12 / `float(e^{r·ln101} − 1)·0.1199 + 0.01`; alpha =
  `sin w·sinh(bw·m·w/sin w)` with m = n+1 (band-pass) or 1/(n+1) (notch).
- Peaking: `Q` from `1 − r` (0.1 / 100 / `·ln2001·0.04995 + 0.1`), gain `A = float(e^{g·ln(10/9)_f} − 1)·8.1 + 0.1`
  (g = gain/128), `A^{1/(n+1)}` with extra sections, alpha term `sin w/(2Q)`.
- Shelves: slope from `1 − r` (0.1 / 1 / `·ln3·0.45 + 0.1`), A as above, term `√A·√((A + 1/A)(1/S − 1) + 2)`.
- The filters are designed with the plugin's run-time sin/cos/exp/ln/power (Delphi RTL); those differ from glibc by
  one ulp on 0.2…6 % of random arguments (`py/rtl.py`, EXEC). The RTL's `Sinh` behaves as `(eˣ − e⁻ˣ)/2`
  (identical on 99.7 % of arguments, libm `sinh` on 39 %), which LYNX uses.

### 1.6 Gain pattern **[DECOMP `FUN_006de9a0`, render loop]**

With s steps (> 0) and a bank that is on: period `P = double(float(T)/float(s))`, table length `N = Round(P)` (no
table, no pattern, when N < 1). Table: `m = N − 1`, `h = m/2`, `x = float(π/h)·i` for i < h, else
`float(float(π/(m − h))·(i − h)) + π`; `v = 0.5 − cos(x)·0.5` (one raised-cosine bump per step); the shape
`e = float(e^{|a|·ln10001} − 1)·sign(a)` (a = shape/128) bends it: e > 0 → `1 − ((e+1)^{1−v} − 1)/e`, e < 0 →
`((|e|+1)^v − 1)/|e|`. During the render a double position wraps by P; each segment starts at index
`Round(pos) mod N` and multiplies both channels by the table (x87, i.e. float products).

### 1.7 Run-time environment **[EXEC]**

After creation the thread runs with MXCSR `0x9FE0` (FTZ + DAZ) and x87 control word `0x037F`. The delay-line, pattern
and pan-law kernels are the x87 variants (the framework selects them while the CPU-feature byte `[0x735c38]+0x16` is
0, as on the test machine); the matrices, filters and resampler are SSE2. LYNX renders with FTZ/DAZ; without it one
state differs from the DLL at −793 dB (denormals).

### 1.8 Verification **[EXEC]**

**Python model of the set-up laws** (`py/model.py`, `py/t_model.py`) **[model]**: per enabled bank the time, offsets,
pattern period/length/first entries, input/output/feedback matrix targets and both filters' coefficients, against
the DLL's own fields after 1024 rendered frames: identical in 103 of the 106 banks of the installation states and 82
of 83 banks of 16 random states. Every remaining difference is one double ulp in a filter coefficient (the plugin's
Delphi RTL `Sin`/`Cos` vs glibc) or the 10⁻¹⁷ cross term of a hard pan (`fsincos` vs `cosl`, which the C++ engine
avoids by executing `fsincos`). None of these changed a single output sample in the renders below. (With libm
`sinh` instead of the RTL's form, one random state of the renders differed at −179 dB through a band-pass b0.)


LYNX = the C++ engine (`src/core/FruityDelayBank.cpp`) through `cpp/dbcmp.cpp`, a new instance (gain ramps from 0, as
the DLL in the harness: state, tempo, sample rate, block size), FTZ/DAZ on.

| Check | Cases | Result |
|---|---|---|
| Every unique installation state, music 4 s, 44.1 kHz / blocks of 512, 120 BPM (`t_all.py` a) | 52 | **52 bit-identical** |
| Same states, noise bursts 4 s, 48 kHz / blocks of 300, 93 BPM (`t_all.py` b) | 52 | **52 bit-identical** |
| Control changes (globals; bank time, offset, filters, feedback, pans, volumes, send) and tempo changes during the render (FL's ProcessParam / tempo event at sample positions, `t_ev.py`) | 12 states × 4 event sets | all bit-identical |
| Random states over every control and option (filter types 0–7, orders 0–2, modes 0–3, patterns, tap, sync, solo, oversampling 0–2) at 44.1/48/96 kHz, blocks 256–512, four signals (`t_rand.py`, 4 seeds) | 96 | **96 bit-identical** |
| Unit test `fruity_delay_bank_engine` (`tests/tests.cpp`) | 2 states of our own (3 banks; 2× oversampling at 48 kHz) | bit-identical goldens |

Two defects of an early port were found only by the random states and fixed: the shelf filters' `b` coefficients
multiply `A·(…)` before `d` (§1.5 order), and a step that rounds to 0 samples builds no pattern. Random states with
more than two extra filter sections crash the DLL (its filter record has room for three sections, EXEC: access
violation in the state load); LYNX clamps the order to 2. Without FTZ/DAZ one state differs at −793 dB (denormals).

**Cost** (one core, 20 s at 44.1 kHz, every installation state, process start included): median 0.5 %, at most 2.9 %
of real time.

### 1.9 What LYNX does with it

- **`EffectKind::fruityDelayBank`** (`core/FruityDelayBank`): the slot keeps FL's state (normalised to the version-16
  layout) in `EffectSlot::state`; the four global controls (dry, output, input, feedback) are the slot's parameters
  (raw/128), editable in the mixer and mapped 1:1 from FL automation. The other 127 controls and the bank options come
  from the state (an effect slot has at most 48 parameters); FL automation of bank controls is not imported.
- The host tempo drives the synced times (a change recomputes the times and patterns as FL's tempo event does). Host
  blocks are split into engine blocks of at most 512 frames. LYNX starts with the gain ramps at their targets
  (FL's fade-in of a fresh instance is only reproduced by the engine's `settled = false` mode, used by the tests).
- A new Delay Bank added in LYNX starts from the DLL's own new-instance state (bank 1 on, the others off).
- **Fallback:** a state that cannot be read (wrong size or version) keeps the generic delay; the import report lists
  it.

---

## 2. Fruity Multiband Compressor

Scratch subdirectory `mb/`.

- **Tools:** Ghidra 12.1.4 headless (`gh/imp.sh`; the engine re-decompiled with the runtime's `powf`/`sinf`/`cosf`/`tan`
  prototypes applied, `gh/scripts/SigDec.java` → `mb/out/ce.c`), `objdump -M intel` (`out/ce.s`, `out/mb_plug.s`)
  for every float operation order, constants with `mb/py/pe.py` / `mb/py/k.py` (`mb/out/consts.txt`).
- **EXEC harness:** round 52's `run/db.c` (= `dl.c`) unchanged, plus a copy `mb/run/mb.c` that can also call an
  engine function taking an `int` (`efn=…,if,…`). Same safety measures (unmodified DLLs, `SetErrorMode`, vectored fault
  handler, `TerminateProcess`, `timeout`). The wrapper loads `comp_eng_x64.dll` from its own folder (EXEC, `mods`).
- **Drivers (ours, `python3 -I`):** `py/survey56.py` (states → `data/states56.pkl`), `mb/py/eff.py` + `pmap.py` +
  `t_pmap.py` (controls → engine parameter table), `mb/py/fn.py` + `t_math.py` + `mb/cpp/mathref.c` (the runtime's
  math against our replicas), `mb/py/t_win.py` (window), `mb/py/model.py` + `t_model.py` (Python model of the set-up
  laws vs the DLL's objects), `mb/py/t_cpp.py`, `t_more.py`, `t_r4.py`, `t_clang.py`, `t_win2.py` (LYNX's C++ engine
  through `mb/cpp/mbcmp.cpp` vs the DLL), `mb/py/gold.py` (unit-test goldens). Logs in `mb/out/`.

### 2.1 Usage in the installation **[DATA]** (`py/survey56.py`)

65 states: 30 mixer slots in 11 demo projects, 4 in one template (Cheese's recording setup), 31 presets (`.fst`);
55 unique. All are version 0, 124 bytes. Linear phase on in 33, output limiter on in 44; band modes (high/mid/low):
compress 43/46/43, off 5/8/7, bypass 17/11/15. No installation state has the mid band's crossovers crossed.

### 2.2 Wrapper **[DECOMP + EXEC]**

- **State** (`SaveRestoreState` `0x6d5b50`): i32 version; only when the version is ≤ 0 the next 0x78 bytes are read
  raw into 30 words at `P+0x138`; then the 28 controls 0…27 are re-applied through the plugin's own ProcessParam (get
  then set). Words 28 (four flag bytes at `P+0x1a8`) and 29 (`P+0x1ac`) have no control and only drive the editor's
  meters/analyser (`0x6d4820`, `0x6d5e10`, `Eff_Render`); they never reach the engine.
- **ProcessParam** (`0x6d4fe0`): the engine receives `u = (v − min)/(max − min)` (double, rounded to float) for control
  v through its parameter method (vtable +0x10). Control 2 also sets the latency reported to the host: 52 frames when
  on, 0 when off (dispatcher id 0x1e). For controls 3, 4, 11, 12, 13, 20, 21 an editor routine re-orders the
  crossover knobs on screen; with every installation state and in our tests it never changed a control or an engine
  value (EXEC: engine table read back after load = the law below for all 65 states, `t_pmap.py`).
- **Eff_Render** (`0x6d49a0`): de-interleaves the block, calls the engine's process (vtable +0x8, in/out, n), interleaves
  back. Nothing else touches the audio. Sample-rate changes call the engine's set-up method with kind 0.

| Control | Range | Default | Engine | Law |
|---|---|---|---|---|
| 0 Output gain | 0…65536 | 32768 | id 0 | `4^u − 1` (unity at u = ½) |
| 1 Limiter | 0…1 | 0 | id 1 | on when u ≥ 0.5 |
| 2 Linear phase | 0…1 | 0 | id 0x2e = `1 − u` | FIR crossovers when on (see §2.3) |
| 3 / 11 / 20 band mode (high / mid / low) | 0…2 | 0 | ids 9/6, 0x15/7, 0x22/8 | 0 compress, 1 off (band muted), 2 bypass (band passes uncompressed): compressor on when `(double)u < 0.33`, band enabled unless `0.33f ≤ u ≤ 0.66f` |
| 4 High crossover | 20…20000 | 17000 | id 10 | high-pass at `f = 19981^√u + 19` Hz |
| 12 / 13 Mid upper / lower crossover | 20…20000 | 17000 / 6500 | ids 0x16 / 0x17 | low-pass at f(c12), high-pass at f(c13) |
| 21 Low crossover | 20…20000 | 6500 | id 0x23 | low-pass at f |
| 5 / 14 / 22 band gain | 0…65536 | 21845 | | `8^u − 1` (unity at u = ⅓) |
| 6 / 15 / 23 threshold | 0…65536 | 21845 | | `T = 1000^u · 0.001` (−60…0 dB, linear amplitude) |
| 7 / 16 / 24 ratio | 0…65536 | 45875 / 48875 / 45875 | | `ρ = u` (see §2.4) |
| 8 / 17 / 25 attack | 1…1000 | 100 | | detector smoothing frequency `fc = 1/(u·0.1 + 1/17000)` Hz (double, rounded to float) |
| 9 / 18 / 26 release | 100…10000 | 1000 | | `τ = 0.99u + 0.01` s |
| 10 / 19 / 27 knee | 0…65536 | 52429 | | `κ = 0.995u² + 0.005` |

(All float steps in the order written; the powers use the runtime's `powf`, §2.6.)

### 2.3 Band filters **[DECOMP + EXEC]**

Every band filters its own copy of the input (the bands are not complementary): high = high-pass at f(c4), mid =
low-pass at f(c12) then high-pass at f(c13), low = low-pass at f(c21).

- **IIR mode** (linear phase off): 4th-order Butterworth as two biquads with `Q = 0.541` and `1.307` (the code divides
  `sin w` by the floats 1.082 and 2.614). `w = 2πf/rate` (float), `c = cos w`, `s = sin w`, `α = s/1.082` resp.
  `s/2.614`, `a0 = 1 + α`: low-pass `b0 = b2 = (1 − c)·0.5/a0`, `b1 = (1 − c)/a0`; high-pass `b0 = b2 = (c + 1)·0.5/a0`,
  `b1 = −(c + 1)/a0`; both `a1 = −2c/a0`, `a2 = (1 − α)/a0`. Per sample `y = x₁b1 + x·b0 + x₂b2 − y₁a1 − y₂a2`
  (left to right, float); the input and the output of each section are set to 0 when their exponent is 0. The first
  section runs over the whole block, then the second over its output.
- **FIR mode** (linear phase on): 105 taps, centre tap 52 (the 52 frames of latency), transposed direct form
  (`out = x·h0 + s0`, `s(k−1) = x·h(k) + s(k)`). Design (`FUN_180006880` low-pass, `FUN_180006360` high-pass,
  `FUN_180005ce0` band-pass), all in float: the response is a sum of ideal pieces evaluated tap by tap (n = k − 52):
  - an ideal low-pass of gain g up to x Hz: `2g·x/rate · sin(n·w)/(n·w)` with `w = 2πx/rate` (`2g·x/rate` at n = 0);
  - an ideal high-pass of gain g from x Hz: `g·(sin(nπ)/(nπ) − sin(n·w)/(n·w)·2x/rate)` (`g·(1 − 2x/rate)` at n = 0);
  - a linear transition between two points (x₁, y₁), (x₂, y₂): the impulse response of `a + bω` over 0…ω₂ minus the
    one over 0…ω₁, with the closed form `sin(nΩ)/(nΩ)·(2bW²/R' + 2aW/R') − (1 − cos(nΩ))·2b/(R'·n²/rate²)`
    (W = 2πx, R' = 2π·rate, Ω = W/rate; at n = 0 `bW²/R' + 2aW/R'`), slope and intercept from the two points in
    angular frequency.
  Transitions span `0.83f … 1.17f` (the upper edge clamped to `rate/2 − 1`): low-pass = ideal low-pass to 0.83f plus
  the 1→0 transition; high-pass = the 0→1 transition plus an ideal high-pass from 1.17f; band-pass (upper edge from
  c12, lower from c13) = the 0→1 transition of the lower edge, a flat piece, the 1→0 transition of the upper edge;
  when the two transitions overlap they are joined at their crossing point instead; when `0.83·f(c13)` lies above the
  clamped `1.17·f(c12)` the mid band's FIR stays all zero (silent band). The taps are then multiplied by a 105-point
  **Kaiser window** (β = 5.10226, `I0` from a 15-term series with the factorials as float products, `powf` for the
  powers) and flushed to 0 when their exponent is 0. Both kinds of filters exist in every band; changing the mode
  (control 2) clears every state of the engine.

### 2.4 Band compressor **[DECOMP + EXEC]**

Per sample, on the filtered band: level `m = max(|L|, |R|)`;
- **peak with release:** if `m > e` then `e = h = m`, counter = 0; else, while counter ≤ 10·rate,
  `e = powf(e_f, −(counter/rate)/τ)·h` (e_f = the float 2.7182817) and counter += 1; afterwards e holds;
- **smoothing:** one-pole `y = (x + x₋₁)·b + a·y₋₁` (inputs/outputs with zero exponent set to 0) with `k = π·(float)(tan(πfc/rate)·2·rate)` (the tangent in double), `b = k/(k + 2π·rate)`, `a = (2π·rate − k)/(k + 2π·rate)`;
- **knee curve** on the smoothed level s, in linear amplitude: with `lo = T·κ − 0.005`, `hi = 2T − lo`,
  `qa = 1/((lo − T)·4)`, `qb = −2hi·qa`, `qc = qa·hi² + T`: `over = 0` below lo, `s − (qa·s² + qb·s + qc)` up to hi,
  `s − T` above;
- **gain** `g = T/(over·ρ + T)` applied to both channels; then the band gain.
When the band's compressor is off (mode off/bypass) its detector is reset every block.

### 2.5 Sum and output limiter **[DECOMP + EXEC]**

`out = (mid·e_mid + low·e_low + high·e_high)·G` (e = 0/1 band enables, G = output gain). With the limiter on, each
channel goes through its own detector of the same kind with fixed settings (threshold u = 0.98, ratio 0.75, smoothing
u = 3·10⁻⁵, release u = 0.005, knee u = 0.9) whose release time is multiplied by a factor F: on every new peak x,
`F = 1` if F < 1, else `F = max(0.01, 1 − 0.4(x − 1))`. F is **one global variable of the DLL** shared by both
channels (updated left then right each sample) and by every instance in the process. Then a cubic soft clip: below
0.998 the sample passes; above, `t = (|y| − 0.998)/0.003`, `|y| = 0.998 + 0.002·(1.5 − 0.5t²)·t` (t < 1) or 1.0;
finally ×0.99999 (all constants are the floats of these values).

### 2.6 The runtime's math **[DECOMP + EXEC]** (`mb/py/t_math.py`)

The engine links Microsoft's runtime and takes its FMA code paths on this CPU (the same `sinf` as round 52's Delay 3
engine). Our replicas, checked against the DLL's own functions (`efn=`):

| Function | Replica | Mismatches |
|---|---|---|
| `sinf` (`0x18001d050`), `cosf` (`0x18001cb50`) | argument in double, reduction by π/2 in two parts with fused steps, degree-9/10 polynomials with FMA (cos's own cosine branch subtracts `r²/2` unfused) | 0 / 140 121 each |
| `powf` (`0x18001bc80`) | `ln x` from a 257-entry table of ln(1 + j/256) (j = mantissa rounded to 1/256) plus a cubic, `t = y·ln x` in double, `e^t` from a 64-entry table of 2^(i/64), cubic with FMA, exponent added to the bit pattern, rounded to float; the tables are the exact doubles of those formulas; results below the float range are 0 | 0 / 340 000 (bases e, 19981, 8, 4, 1000, squares); the series route for bases within 1/16 of 1 is not reproduced (1 / 60 000 there; only the window table meets it, and the window is identical, `t_win.py`) |
| `tan` (double) | libm `tan` | not separately measured; no effect seen |

With libm's `(float)pow(double)` instead, 26 of the 55 states differed at −155…−218 dB (1-ulp steps in the release
curve).

### 2.7 Silence and set-up **[DECOMP + EXEC]**

- Per host block, if every input sample is within ±10⁻⁷ and no filter state (IIR or FIR, any band) is outside ±10⁻⁷,
  the engine clears every state (filters, detectors; not the limiter factor F) and copies the input to the output.
- Set-up: the engine is built at 44 100 Hz; a sample-rate change re-applies all 47 internal parameters in order, the
  last one (linear-phase mode) clearing every state. Parameter changes while running only recompute their coefficients
  (FIR taps included), except control 2, which clears every state — **even when the same value is written again**.
- Internal blocks of 1024 frames do not change the result (every stage runs sample by sample).

### 2.8 What LYNX does with it

- **`core/FruityMultiband`** (`lynx::flmb`): FL's 28 raw controls as the slot's parameters (raw over range), the
  engine of §2.2–§2.7 in float with the replicas of §2.6, latency 52 in linear-phase mode, host blocks split into blocks
  of at most 512 frames for the silence rule. `flmb::make (state)` keeps FL's raw state so that parameters still at
  their imported value use FL's exact integer controls after the project file's 4-decimal rounding.
- Differences from FL by design: F (§2.5) is kept per instance, not shared between instances; LYNX applies a control
  only when its value changes (FL clears all states on every write of control 2).

### 2.9 Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Every unique installation state, music signal, 44.1 kHz / 512 (`t_cpp.py`) | 55 | **55 bit-identical** |
| Same, noise bursts, 48 kHz / blocks of 300 | 55 | **55 bit-identical** |
| Same, FTZ+DAZ (LYNX's audio thread) | 55 | 55 bit-identical |
| Music with 2.2 s of digital silence and a −180 dB passage (silence rule) | 55 | 55 bit-identical |
| Random states over every control (crossovers crossed included), 44.1/48 kHz, blocks 64…1024, four signals | 60 | 60 bit-identical |
| Control changes while running (6 per run, FL's ProcessParam at sample positions) | 20 | 20 bit-identical |
| Parameters rounded to 4 decimals, FL state kept (`t_r4.py`) | 55 | 55 bit-identical |
| Builds: g++ -O2, clang -O2, clang -march=native, Windows (zig/mingw) | 19–55 each | all bit-identical |
| Python model of the set-up laws vs the DLL's objects (`t_model.py`): crossover law, IIR coefficients, FIR taps | 17 states | identical |
| Unit test `fruity_multiband_engine` | 3 own states (IIR + limiter; FIR with overlapping mid transitions; FIR with a silent mid band) | goldens identical |

**Cost** (one core, 20 s of noise): 3.6 % of real time in linear-phase mode, 2.8 % with IIR crossovers.

### 2.10 Open items

- `powf` for bases within 1/16 of 1 (a separate series route in the runtime) is approximated by `(float)pow`; the
  engine only evaluates such powers for its window, which is identical.
- FL's CPUs without FMA take the runtime's non-FMA paths (`sinf`, `cosf`, `powf`) — not reproduced (not confirmed which
  results differ).
- The limiter release factor F shared between instances (§2.5) is per instance in LYNX.
- FL's real host block size (the silence rule runs per block) was not observed in FL; LYNX uses ≤ 512.

---

## 3. Open items

- **Delay Bank:** the "HQ for all plugins" render mode (process-mode flag 2) forces one oversampling stage in FL;
  LYNX has no such option and uses the state's own setting for playback and export. The filters are designed with the
  plugin's Delphi RTL sin/cos/exp/ln/power, which differ from glibc/mingw by one ulp on a few percent of arguments;
  a coefficient can therefore differ by one double ulp (4 of 189 modelled banks); no render showed an output
  difference, but it is not confirmed for every control value. The x87/SSE kernel choice is
  made by a CPU-feature byte of the framework (as in round 48); only the variants selected on the test machine are
  modelled. FL automation of the bank controls (not of the four globals) is not mapped.
- **Multiband Compressor:** see §2.10.

---

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| Delay Bank `FUN_006de180` | SaveRestoreState (v15/16, 0x214-byte block, per-bank options, v15 time ×10) | DECOMP |
| `FUN_006e0000` | ProcessParam: control → bank field laws, flags for time / filters / pattern | DECOMP + objdump |
| `FUN_006df830`, `FUN_006dfbb0`, `FUN_006dfe30`, `FUN_006dfd00`, `FUN_006dfc10`, `FUN_006dff00` | time/offset/line set-up, filter design calls, pattern, resets, oversampling set-up | DECOMP + objdump |
| `FUN_006debc0`, `FUN_005dc180` | filter controls → cookbook coefficients | DECOMP + objdump |
| `FUN_006de9a0`, `FUN_005db7d0` | gain-pattern table | DECOMP + objdump |
| `FUN_006e1380`, `FUN_006e11f0`, `FUN_006e1320` | render, ramped matrices, filter cascades | DECOMP + objdump |
| `FUN_006d8f30`, `FUN_006d8ed0`, `FUN_006d9090`, `FUN_006d9400`, `0x6d93e0`, `0x6d9810` | delay line set-up, segments, input add, feedback add | DECOMP + objdump |
| `FUN_005dbbb0`, `FUN_005dbd30`, `0x5e12f0` | input/output and feedback matrices, x87 pan law | objdump |
| `0x5e0290`, `0x5e0440`, `0x5dfb10`, `0x5e08d0`, `0x5e0640`, `0x5df750`, `0x5df790`, `0x5e1220`, `0x5dc9a0` | matrix kernels, send add, gains, flushes, ramps, biquad | objdump |
| `FUN_006db930`, `FUN_006db9d0`, `0x6db400`, `0x6db5d0`, table `[0x746578]` | half-band oversampling | DECOMP + objdump + EXEC |
| `0x5e1370` (host-info object) | rate, 1/rate, 44100/rate, ramp step | objdump + EXEC |
| `FUN_006ddda0` | dispatcher: block size, rate, process mode (HQ → one stage) | DECOMP |
| globals `0x735848`, `0x734d58`, `0x735a98`, `0x735978`, `0x735830`, `0x734f38`, `0x7250b4`, `0x735c38` | 1/128, ln 11, 0.1, 0.5, √2, 0.25, 2⁻²⁴, CPU-feature record | EXEC |
| 13 `.flp` + 21 `.fst` (`py/survey56.py`) | Delay Bank usage, versions | DATA |
| Multiband: see §2 | | |
