# Behavioural analysis of FL Studio 2026: round 49 (REA) — Fruity Reeverb 2 and Fruity Reeverb: exact engines

> **Nature of this document.** Continuation of `REA_rodada7.md` §3 (Fruity Reeverb 2 state layout and parameter laws),
> with the goal of the effect rounds (40: Maximus): replace LYNX's approximation of the plugin (the Freeverb-style
> `EffectKind::reverb` with decay → comb feedback, `reeverb2()` in `src/core/FlEffects.cpp`) with an exact engine.
> *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data
> layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve
> only to trace the evidence. No FLEX pack or encrypted content was touched; no Image-Line audio or preset data is
> stored in the repository (the unit-test goldens are the DLL's output for our own states and input).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> anything without evidence is **"not confirmed"**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files or read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **not confirmed** | not enough evidence in this round |

No manual page was read in this round. LYNX's engine was ported directly from the DECOMP facts to C++ and verified
against EXEC sample by sample (no separate Python model this round: the C++ engine plays that role, and every case
below compares it with the DLL).

- **Date:** 2026-10-15. Scratch directory (paths relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r49/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Reeverb 2/Fruity Reeverb 2_x64.dll` | `bc24b17d920652e9ff848fff3f1193b7fb771d148290986ec2b653fc0af897a1` (= round 7) | Delphi plugin: state, parameter laws, coefficient computation, gains |
  | `Shared/Reverb_x64.dll` | `9881ff2ccd74227e454a3d5f1912568f4336abe06e68f3693162aa4d251c969f` (= rounds 31/36) | C++ `BaseReverb`: the per-sample process Reeverb 2 runs |
  | `Plugins/Fruity/Effects/Fruity Reeverb/Fruity Reeverb_x64.dll` | `2fc216ded46bc3bf2470758507b6cba7763b1ca9e85efe28fa713906da817cb8` | Delphi plugin with its own copy of the plate (§6) |

- **Tools:** Ghidra 12.1.4 headless (`gh/imp.sh`, `gh/g.sh`, script `DecRange.java`); decompilations
  `out/Reeverb2_dec.c` (0x6d0000…0x6d8000), `out/r2_5db.c` (0x5db000…0x5e1000, the mixing helpers),
  `out/Reverb_dec.c` (0x180001000…0x180010000), `out/Reeverb1_dec.c` (0x6b7000…0x6bd800); `objdump -M intel` for the
  coefficient routines (`out/coef.asm`, `out/coef1.asm`) and the process loops wherever the float operation order or a
  register's lifetime mattered; constants with `py/rd.py`.
- **EXEC harness:** `run/rv.c` → `rv_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), round 40's `mx.c` plus three
  options: `ev=0,<bpm>` sends ProcessEvent 0 (tempo, float bits) through VMT slot 5, `early` sets the sample rate and
  block size **before** the state is restored, `flushafter` sends Dispatcher 2 (flush) after the rate. Same safety
  measures (DLL unmodified, `SetErrorMode`, vectored fault handler, `timeout`, `TerminateProcess`). Runtime function
  pointers and globals were read with the harness's `ipp=` / `gd=` peeks.
- **Drivers (our own code, `py/`):** `survey49.py` (states + project tempo → `data/states49.pkl`), `rv.py` (harness
  runner, test signals `music`, `burst`, `saw`, `loud`, `quiet`), `t_lynx.py` (one state, LYNX vs DLL), `t_all.py`
  (every installation state), `t_syn.py` (synthetic states, options, automation), `t_v1.py` (§6); C++ drivers
  `cpp/rvcmp.cpp` / `cpp/rv1cmp.cpp` (LYNX's engines on a float file, optional `p=n,idx,val` automation, `FTZ=`
  MXCSR bits), `cpp/coefdump.cpp`; test goldens `tests/gen.py`, `tests/gen1.py`.

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 1. Where Reeverb 2's DSP lives **[DECOMP + EXEC]**

- The plugin object owns a Delphi `TFullReverb` (subclass of `TBaseReverb`, unit `DSP_UFReverb`), constructor
  `FUN_006d2820`. On creation `FUN_006d18b0` loads **`Reverb_x64.dll`** (the installation's `Shared` copy — the harness's
  module list shows `Shared\Reverb_x64.dll` loaded) and calls its export `CreateReverbInstance`; the Delphi object then
  only fills the C++ object's parameter block (`obj + 0x10`, obtained through the C++ VMT slot 3) and calls the C++
  **VMT slot 1**, `FUN_180003270`, for every block (`FUN_006d2a10`).
- `Reverb_x64.dll`'s VMT (`0x1800603c0`): slot 0 destructor, **slot 1 `FUN_180003270` = the "full" process** (mono in,
  wet stereo out, optional early-reflection stereo out), **slot 2 `FUN_180002980` = the process the Sytrus/Harmless/
  Harmor/Ogun hosts use** (rounds 31/36/37, LYNX's `core/PlateReverb.h`), slot 3 parameter block, slot 5 reset of the
  two modulation lines.
- If the DLL cannot be loaded the plugin keeps its handle 0 and renders nothing through the reverb (not exercised).

## 2. State and controls **[DECOMP `FUN_006d4920`, `FUN_006d5160`, `FUN_006d4e10`, `FUN_006d5030`; DATA]**

- Layout as round 7 §3.1: version 10001 → 15 ints + tempo byte + Mid/Side byte (0x3E bytes after the version); 10000
  → 13 ints + the two bytes (Mod speed = 684, Mod depth = 0); **< 10000** → no version: 0x35 bytes are read from the
  start into the control vector (13 ints, the 53rd byte lands in the low byte of control 13 and is then overwritten by
  the 684/0 defaults), the tempo byte is not read and Mid/Side is cleared.
- The controls keep FL's integers (ranges and units of round 7 §3.2). Without a state the plugin's own defaults are
  `75, 40, 0, 50, 100, 15, 40, 100, 500, 0, 128, 64, 64, 684, 0`, both options off **[EXEC field dump]**.
- What a control change does (`FUN_006d5160`, flag 1):
  - **0…8, 13, 14** → `FUN_006d4e10` hands the reverb its fields and marks the coefficients dirty (recomputed at the
    next render):
    - predelay samples `P = Round(p2·fs / 384)` (float division; Round = round-half-even) or, with the tempo option,
      `Round((p2·fs)·0.3125 / bpm)` (double, bpm a float); **a changed P re-allocates the reverb and clears every
      line and state** (`FUN_006d1d00`);
    - decay `T = (float)(p5·fs)·0.1`, room p3, diffusion p4, bass multiplier `p7·0.01`, crossover `p8` Hz;
    - low cut `p0` Hz (19 → off), high cut `p1` and damping `p6` in hundreds of Hz (221 → off);
    - mod speed `p13/2048`, mod depth `p14/2048` (copied into the C++ block at the next coefficient update, together
      with the rate).
  - **9** separation `s = p9/64` → both gain matrices rebuilt; **10** dry gain; **11 / 12** early-reflection / wet matrix.
- Tempo changes (ProcessEvent 0, `FUN_006d48d0`) re-run `FUN_006d4e10` only when the tempo option is on.
- **Level law:** `x = p·(1/128)` for dry, `p·(1/256)` for early reflections and wet; gain `1` if `x = 1`, else
  `(float)((e^{(double)(x·2.3978953f)} − 1)·0.1f)`.
- **Gain matrix** `FUN_005db570` (pan 0, mode 0), `outL += m0·L + m2·R`, `outR += m1·R + m3·L`:
  separation 0 → `[g, g, 0, 0]`; gain 0 → zeros; otherwise `m0, m1 = cos, sin((1 + 0)·π/4)·(g·√2)` in x87 extended
  precision, then `k = s` (s < 0) or `k = (e^{s·ln 5} − 1)·0.25` (s > 0), `f = min(k, 1)`, `m2 = m0·f·0.5`,
  `m0 ← m0·(1 − f·0.5)`, `m3 = m1·f·0.5`, `m1 ← m1·(1 − f·0.5)`. **Mid/Side input** negates `m1` and `m3`.

## 3. Coefficients (`FUN_006d1f30`, Delphi side) **[DECOMP + asm]**

`fs` = the reverb's integer rate, `inv = (float)(1/fs)`; all coefficients are floats.

- **Lines:** `R = ⌊fs·(room + 15)/115⌋`, `L_k = max(1, ⌊N_k·R/20000⌋)` with round 31's twelve proportions; each line
  pointer is reduced modulo its new length; output taps `⌊M·R/20000⌋` with round 31's 7 + 7 offsets. Lines are allocated
  at the room-100 size; **a room change zeroes lines 0…11 over their new length and the early-reflection line**.
- **Low cut** (first-order all-pass high-pass): `t = (float)tan(f·π·inv)`, `c = (t − 1)/(t + 1)` (double), f clamped to
  fs/2; 0 when off. **High cut / damping** one-pole: `y = (float)(2 − cos(f·628.3185·inv))`,
  `c = 1 − (y − √(y² − 1))`; 1 when off.
- **Diffusion** `d = (float)(p4·0.01)`, `h = (float)(inv·0.5)`: input all-pass `e^{ln 0.001·(L0+L1)h/(0.07d)}`, tank
  all-passes `…(L4+L8)h/(0.35d)` and `…(L6+L10)h/(0.51d)` (a fourth value with `(L2+L3)`, 0.11 is computed but the full
  process does not read it); all 0 when d = 0.
- **Decay** `g = (float)e^{0.25·ln(e^{ln 0.001·ΣL/T})}`, ΣL = L5 + L7 + L9 + L11 (Delphi `Power(x, 0.25)` =
  `exp(0.25·ln x)`).
- **Bass shelf:** `ρ = (float)Power(e^{ln 0.001·ΣL/(T·bass)}, 0.25) / g`; shelf amount `(ρ − 1)·0.5`; `ρ ← 1` if
  ρ > 1; crossover all-pass `c = (t − ρ)/(t + ρ)`, `t = (float)tan(p8·π·inv)`.
- **Early-reflection taps:** `x = (float)(fs·(room + 3)·3.3022917905e−7)` (= 1.5/(44100·103)); left/right offsets
  `Round(E·x)` from two 8-entry tables (`1102 … 9263`, `837 … 9263`). The weight loop resets its numerator to 0 on
  every pass (asm: the register is reloaded with 0.0 at the loop head), so **only the first tap of each side has a
  weight**: `m·0.5/oL₀` and `m·0.36/oR₀`, `m = min(oL₀, oR₀)`; all others are 0 **[DECOMP + asm; EXEC impulse
  response: one reflection per channel]**.
- **Early-reflection line:** power of two, `2^(bits(Round(fs·0.31633)) + 1)` (32 768 at 44.1 kHz).

## 4. The full process (`Reverb_x64.dll` `FUN_180003270`) **[DECOMP + asm]**

Per block: mod amplitude target `A = depth·0.0045351474·fs` (samples, i.e. up to 4.535 ms), LFO increment
`(speed³·9.9 + 0.1)/fs` (Hz), smoothing factor `1 − powf(0.01, 1/(fs·0.1))`. Per sample:

1. **LFO** (state survives flushes): `s = (float)sin(2π·φ)` on the phase *before* the advance `φ ← frac(φ + inc)`.
   Amplitude: `a ← A` when `|A − a| < 0.01`, else `a ← (A − a)·k + a + 10⁻³⁰`. Modulation delay `D = a·((s + 1)·0.5)`.
2. **Input:** `x + 10⁻³⁰` → high-cut one-pole → low-cut all-pass, output `(x − ap)·0.5 + 10⁻³⁰` when the low cut is on.
3. **Early reflections** (only when that matrix is non-zero): read from the ER line at `(tap + pointer) & mask` before
   this sample's write: left `+t0 + t1 − t2 + t3 + t4 + t5 + t6` (7 taps), right `+t0 − t1 + t2 + t3 − t4 + t5 + t6 −
   t7` (8 taps), each times its weight (§3: only t0 is non-zero).
4. **Input diffusion:** four all-passes (all with the input coefficient); the **ER line takes the first all-pass's
   output**; then the predelay line (read then write, length P), output `+ 10⁻²⁰`.
5. **Tank, half A:** `u = line11·g`; bass shelf (first-order all-pass at the crossover, `((ap + u)·shelf + u) + pre`);
   damping one-pole; **write into a 4096-sample modulated line and read it back `D` samples late with linear
   interpolation** (`(1 − f)·y[n−⌊D⌋] + f·y[n−⌊D⌋−1]`, D = 0 returns the sample just written); all-pass (tank coefficient
   1), delay line 5, `×g`, all-pass (coefficient 2), write line 7 then read it one position later for half B.
   **Half B** is the same with its own states, the second modulated line (same D), lines 8–11.
6. **Wet taps:** round 31 §7.5's 7 + 7 taps with weights `c + (1 − c)·g` (0.25, 0.7, 0.2, 0.6, 0.5).

With depth 0 this is round 31's process plus the `10⁻²⁰` on the predelay output (the slot-2 process has neither the
modulated lines nor the ER output) — **inference** from comparing the two decompilations; not used by LYNX.

## 5. The plugin's render (`FUN_006d5a90`) **[DECOMP + EXEC]**

1. Mono input `L + R` (no ½), or **`L − R`** with Mid/Side (`FUN_005dfed0` / `FUN_005dffb0`).
2. Reverb (coefficients first if dirty) → wet buffer, ER buffer (or none when both ER matrices are zero).
3. **Dry** (`FUN_005db3c0`): the output is overwritten with `in × dry`; gain 0 → zeros, 1 → copy.
4. **ER** then **wet** are added through their matrices (`FUN_005df6b0`).
5. **Ramps** (`FUN_005e0540`): dry and each matrix element move from the current to the target value once per block,
   `inc = (target − current)/n` limited to ±0.001 per sample (`host + 0x34` = 0.001 **[EXEC]**); dry uses the unrolled
   pair pattern of `FUN_005dfc80` (even/odd samples accumulate `2·inc` separately, the tail adds `inc`), the matrices
   the per-sample pattern of `FUN_005df6b0`. A fresh plugin starts from 0 (a fade-in); **Dispatcher 2 (flush)** sets
   current = target and clears the reverb (`FUN_006d1c60`).

### Host call order **not confirmed**

The plugin computes predelay and decay **with the rate known when the state is loaded**. With the rate set after the
state (the harness's original order) the 48/96 kHz renders keep 44.1 kHz decay/predelay values (EXEC: +3 dB error vs
the rate-first order at 48 kHz). FL's actual order on project load and whether it flushes before an export were not
observed. LYNX computes everything with the current rate and starts flushed; all verification uses rate-before-state +
flush (`early` + `disp=2`), and the no-flush fade-in is verified separately.

## 6. Fruity Reeverb (first version) **[DECOMP `FUN_006ba4f0`, `FUN_006bac20`, `FUN_006bb600`, `FUN_006bb8d0`, `FUN_006bc150`; EXEC]**

Its own Delphi copy of the same plate (same line and tap proportions), without early reflections, modulation or
ramps:

- **State:** version (written 1; ≥ 2 is ignored), 10 ints: low cut, high cut, predelay, room, diffusion, colour,
  decay, high damping, dry, wet. Control ranges **[EXEC]**: 0…65536 except predelay 0…250 (ms) and colour 0…40. Defaults
  `1250, 10800, 0, 26000, 65536, 20, 4700, 10800, 65536, 13120`.
- **Conversions** (`x = (float)min(1, raw·scale)`, Trunc of float expressions): low cut `⌊2981x + 19⌋` Hz (≤ 19 off),
  high cut `⌊216x + 5⌋`·100 Hz (≥ 221 off), room `⌊99x + 1⌋`, diffusion `⌊100x⌋`, colour `Round(raw·0.1)` → factor
  0.2 / 0.6 / 1 / 1.5 / 3 ("Brighter", "Bright", "Flat", "Warm", "Warmer"; 1 otherwise), decay `⌊199x + 1⌋`/10 s,
  damping `⌊216x + 5⌋`; dry and wet are linear `x`. Predelay `⌊ms·fs/1000⌋` samples (min 1) in a 250 ms line.
- **Coefficients:** as §3 with: input scale 0.5 (`(L + R)·0.5`), half-sums in double `(La + Lb)·0.5/fs`, the decay
  ratio `(float)ΣL/fs` and `ln 0.001` as floats, the colour factor in place of the bass multiplier, a **fixed 500 Hz**
  crossover; the room change clears lines 0…11.
- **Process:** input `(float)((L+R)·0.5 + 10⁻³⁰)` (double add), high cut, low cut (`(float)((x − ap)·0.5 + 10⁻³⁰)`),
  four all-passes, predelay (no `10⁻²⁰`), tank as §4 without modulated lines and with the shelf summed as
  `(u + (u + ap)·shelf) + pre`; output `in·dry + taps·wet` per channel.
- **Flush** (`FUN_006ba400`, also at creation) fills every line with the float bit pattern `0x0DA24260` (≈ 10⁻³⁰), not
  0, and zeroes the filter states; a rate change re-allocates without filling (the content is undefined until the next
  flush).
- The plugin runs with denormals flushed: LYNX matches it bit for bit only with FTZ or DAZ set (EXEC: a 1-ulp
  difference at 10⁻³¹ levels otherwise), which is how LYNX's renderer runs effects (`ScopedFlushDenormals`).
- **Usage [DATA]:** 7 slots, all in one demo (Exoplanet).

## 7. Relation to the other reverbs in LYNX **[DECOMP]**

- `core/PlateReverb.h` (Harmless/Harmor/Ogun) is `Reverb_x64.dll`'s **slot 2**; Reeverb 2 runs **slot 1** of the same
  object (§1, §4): same line/tap tables and coefficient laws, plus the modulated lines, the ER output and the 10⁻²⁰.
  LYNX's Reeverb 2 reuses `PlateReverb`'s tables and implements slot 1 separately (`core/Reeverb2.cpp`).
- **Effector's reverb** (rounds 27/33, `engine_x64.dll` `fxReverb`) is a different implementation of the same family:
  same proportions, the same ER scaling formula and Reeverb-2-like parameter ranges, but double precision, a dB level
  law, input all-passes 1/3 using a second coefficient, no modulation; its port stays as it is.

## 8. What LYNX does with it

- **`EffectKind::reeverb2`** (`core/Reeverb2.{h,cpp}`): 17 parameters = FL's 15 controls + the tempo and Mid/Side
  options, each `(raw − min)/(max − min)`, so the slot stays editable in LYNX's UI (with FL's units as text) and maps back
  to FL's integers exactly; FL automation of the plugin keeps FL's normalised value. `effectFromFl` routes every
  readable Reeverb 2 state to it; the old Freeverb mapping (`reeverb2Approx`) remains only for unreadable states.
- **`EffectKind::reeverb1`**: the same for Fruity Reeverb (10 controls, raw/max).
- Engine semantics: created at the first block with the current rate and tempo, then flushed; a control change acts as
  FL's ProcessParam (dirty coefficients, predelay re-allocation, ramps); `Effect::reset()` = FL's flush. Contraction
  of float expressions is disabled in the file (`fp contract(off)`).

## 9. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Reeverb 2, every distinct installation state (`t_all.py`; 313 demo slots in 31 demo files + 10 slots in 7 other installation projects → 293 distinct state/tempo pairs, 19 with tempo predelay, 52 v10000) | music 44.1 kHz / 512; noise bursts 48 kHz / 300; saw 96 kHz / 1000; quiet 22.05 kHz / 64; loud 44.1 kHz / 4096 | **293/293 bit-identical** in every set |
| Same, without the initial flush (gain fade-in) | music 44.1 kHz / 512 | 293/293 bit-identical |
| Synthetic (`t_syn.py`): Mid/Side ± separation, ±separation, room 1/100, decay 0.1/20 s, diffusion 0, bass ×0.2/×3, crossover 25/2000 Hz, all filters at extremes, modulation max/slow, predelay 384 (free and at 60/173 BPM), all gains 160, all gains 0, a v10000 and an unversioned state | 21 states | all bit-identical |
| Automation of every control (`t_syn.py`, blocks of 480, change at sample 40 000 and back at 90 001) + dry/wet ramps without flush | 19 runs | all bit-identical |
| LYNX engine built with Zig/clang for the host CPU, and for Windows (`x86_64-windows-gnu`, run on Windows) | all 293 states | bit-identical |
| LYNX with FTZ+DAZ (as the renderer runs) | all 293 states, the synthetic set, Fruity Reeverb | bit-identical |
| Fruity Reeverb (`t_v1.py`): the 7 demo states × 44.1/48/96 kHz, 7 synthetic states, automation of every control | 39 runs | all bit-identical (with FTZ or DAZ) |
| Unit tests `reeverb2_engine` (`tests/tests.cpp`) | DLL goldens on 3 Reeverb 2 + 2 Fruity Reeverb own states through `makeEffect` | exact equality |

**Cost** (`cpp/bench.cpp`, one core, 20 s of noise at 44.1 kHz, blocks of 512, other builds running): Reeverb 2 with
modulation and early reflections 1.0 % of real time, Fruity Reeverb 0.6 %.

## 10. Open items

- FL's host call order (rate before/after the state, flush before an export) — not confirmed (§5); LYNX assumes rate
  first and a flush.
- Mid/Side (no installation state uses it) and the unversioned layout were verified only on synthetic states.
- Reeverb 2 without a loadable `Reverb_x64.dll` (silent reverb in FL) is not reproduced.
