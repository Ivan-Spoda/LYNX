# Behavioural analysis of FL Studio 2026: round 58 (REA) — Fruity PanOMatic, Fruity Balance and Fruity Blood Overdrive: exact engines

> **Nature of this document.** Continuation of `REA_rodada7.md` §4 (state layouts) and `REA_rodada17.md` §3 (the
> volume law of Balance and PanOMatic), with the goal of the effect rounds (40: Maximus): replace LYNX's approximations
> — Balance and PanOMatic as the generic `EffectKind::utility` (PanOMatic's LFO dropped, Balance's pan law never checked
> against the DLL), Blood Overdrive as the generic distortion at its defaults (its state never read) — with exact
> engines. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here
> are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. No FLEX pack or encrypted content was touched; no Image-Line audio or preset data is
> stored in the repository (the unit-test goldens are the DLLs' output for our own states and input; the only tables
> the engines need are computed from the formulas below).
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

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r58/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity PanOMatic/Fruity PanOMatic_x64.dll` | `029d8cda769e2775e7dce7bc772a490b3bbe858feb7fcac7dcf24d35296ce26e` (= round 17) | Delphi plugin |
  | `Plugins/Fruity/Effects/Fruity Balance/Fruity Balance_x64.dll` | `a81c8107c9511392d13f7191523058185a606fcab4313cb038c0b32376a4e9d1` (= round 17) | Delphi plugin |
  | `Plugins/Fruity/Effects/Fruity Blood Overdrive/Fruity Blood Overdrive_x64.dll` | `3178ccb0ec4a900ad60350f0280533557c1c1fd0669df8d2cb4e9dbca419697d` | Delphi plugin |
  | `FLEngine_x64.dll` | `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` (= rounds 42/43) | host: the LFO wave tables PanOMatic reads |

- **Tools:** Ghidra 12.1.4 headless (projects `~/ghproj/r58`; `gh/imp.sh`, `gh/g.sh`, script `DecRange.java`;
  `out/bo_dec.c`, `out/pom_base.c`); `objdump -M intel` for every routine whose float operation order matters
  (`out/pom_pp.asm`, `out/bal_pp.asm`, `out/bal_r.asm`, `out/bo_r.asm`, `out/fle_wt.asm`, FLEngine's whole text
  `out/fle.asm` for the host-table references).
- **EXEC harness:** `run/fx.c` → `fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), round 51's effect harness (DLL
  loaded unmodified from the installation, `SetErrorMode`, vectored fault handler, `timeout`, `TerminateProcess`;
  state restored through the plugin's own SaveRestoreState, Dispatcher sample rate / block size, Eff_Render) plus:
  `early` (rate before the state) and `flushafter` (Dispatcher 2 after the rate) as in round 49; `wt=<file>` hands the
  plugin a host object whose `WaveTables[0…2]` (host `+0x18/+0x20/+0x28`) point to FLEngine's three LFO tables;
  `warm=N` renders N silent frames and then flushes before the measured run.
  `run/wt/wt.exe` maps `FLEngine_x64.dll` **without running it** (`LoadLibraryEx` with `DONT_RESOLVE_DLL_REFERENCES`:
  no `DllMain`, no imports) and calls only its RTL `Sin` to build those tables exactly as FLEngine's builder does;
  `run/wt/rtl.exe` does the same for one Delphi RTL math routine of a plugin DLL (`Sin`, `SinCos`, `Exp`, `Ln`) on a
  file of arguments.
- **Drivers (our own code, `py/`):** `survey58.py` (every mixer-slot state of the three plugins in the installation's
  `.flp` files → `data/states58.pkl`), `vt.py` (harness runner on top of round 51's `ws.py`), `rtl.py`,
  `t_rtl_pom.py`, `t_rtl_bal.py`, `t_balrtl2.py` (RTL vs C library), `pommodel.py`, `balmodel.py`, `bomodel.py`
  (Python models built only from the facts below), `t_pom.py`, `t_bal.py`, `t_bo.py` (models vs DLL), `t_sweep.py`
  (parameter sweeps), `t_cpp.py` (LYNX's C++ engines vs DLL, through `cpp/fxcmp.cpp`), `gold.py` (goldens of
  `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "bit-identical" = every output float equal.

**Installation usage [DATA]** (mixer slots of every `.flp` under `Data`; Patcher-internal nodes not counted):

| Plugin | Slots | Files | Distinct states | Notes |
|---|---|---|---|---|
| Fruity PanOMatic | 192 (71 in 8 demo songs, 121 in 13 templates) | 21 | 54 | LFO on in 29 slots (target pan 25, volume 4; sine 17, triangle 9, square 3); pan ≠ centre in 10, volume ≠ 0 dB in 53 |
| Fruity Balance | 57 | 12 | 10 | balance 0 in every state; volumes 0…320 |
| Fruity Blood Overdrive | 26 (25 in 10 demo songs, 1 in `System/Render tests`) | 11 | 19 | ×100 on in 2 states |

PanOMatic nodes also sit inside Patcher graphs (round 20: 28 of them in "Stroll Up"); the import routes them through
the same function, so they get the exact engine too.

---

## 1. Fruity PanOMatic

### 1.1 State and controls **[DECOMP `FUN_006c6180` (SaveRestoreState), `FUN_006c6240` (constructor), `FUN_006c6650` (ProcessParam) + EXEC]**

- Record `0xD5` = six `i32` copied raw into the plugin: pan, volume, LFO shape, LFO target, LFO amount, LFO speed.
  After the copy every control is read back and re-sent through ProcessParam (no clamping, no version field).
- Ranges (DFM, round 17): pan 0…256 (128 = centre), volume 0…320 (256), shape 0…2 (sine, triangle, square),
  target 0…2 (off, pan, volume), amount 0…128 (64), speed 0…55000 (27500).
- Derived values (float unless stated):
  - pan `p = float(v − 128)·(1/128)`;
  - volume `g`: `f = float(v)·(1/256)`; `g = 1` when `f = 1`, otherwise `g = float((exp(double(float(f·2.39789534f))) − 1)·double(0.1f))`
    (FL's level law L11 with a float ln 11, as the Soft Clipper's post gain, round 51 §1.1);
  - amount `a = float(v)·(1/128)` when the target is pan, `·(1/256)` otherwise (recomputed when either control changes);
  - shape: the LFO reads **the host's** wave table `WaveTables[shape]` (§1.3);
  - speed (`FUN_006c64b0`): with `s = 65536 − v`, the LFO period in samples is
    `P = float(float(float(exp(double(float(s/65536.0))·ln 50001) − 1)·float(s))·float(2048/fs))`, rounded to an integer
    with the current rounding (nearest-even); the phase increment is the unsigned 32-bit quotient `2³²/round(P)`.
    The `2048/fs` factor is set by the sample-rate notification (Dispatcher 4) and the increment recomputed there.
  - every ProcessParam marks the gains dirty.
- **Tempo is never read.** The LFO period depends only on the speed control and the sample rate; the plugin's
  Dispatcher handles only ids 0, 2, 4, 5, 51, 52, 57 (and the framework's ids), none of them tempo, and the render path
  reads nothing else **[DECOMP `FUN_006c5c10`, `FUN_006c2320`]**. The LFO is free running in samples: projects at any
  tempo give the same output for the same sample rate.

### 1.2 Gains (pan law) **[DECOMP `FUN_006c6550` + EXEC]**

Four gains form a 2×2 matrix: `outL = L·g0 + R·g2`, `outR = R·g1 + L·g3`. For a pan `p` and volume `v` (float):

- `p ≥ 0`: `a = 1 − p`; `g3 = ((1 − a)·0.5)·v`; `g0 = a·v`; `g1 = v − g3`; `g2 = 0`;
- `p < 0`: `b = 1 + p`; `g2 = ((1 − b)·0.5)·v`; `g1 = b·v`; `g0 = v − g2`; `g3 = 0`.

So panning right attenuates the left output and moves half of the removed left signal into the right output (and the
mirror image to the left).

### 1.3 The LFO and FL's host wave tables **[DECOMP `FUN_006c6cc0`, FLEngine `FUN_009cdfc0`, `FUN_01316f50` + EXEC]**

- Once per render call of n frames, when the target is not 0: `phase += inc·n` (32-bit wrap; 64-bit product, low 32
  bits added), then `l = table[phase >> 18]·a` (16384-point table, no interpolation).
  - target 1 (pan): `p' = l + p`, clamped to [−1, 1]; gains from `(p', v)`;
  - target 2 (volume): `v' = l + v`, floored at 0; gains from `(p, v')`.
  - Without the LFO the gains are recomputed only when dirty.
- The table is not the plugin's: it is `WaveTables[shape]` of the host object FL passes at creation. FLEngine's host
  constructor (`FUN_01316f50`) stores its three LFO tables at host `+0x18`, `+0x20`, `+0x28`; its builder
  (`FUN_009cdfc0`) fills them with:
  - sine: `float(Sin(double(2i)·π/16384))` (FLEngine's Delphi RTL `Sin`);
  - triangle, aligned with the sine: for `i` in 0…8191 entry `(i + 0x3000) & 0x3FFF = float(double(4i)/16384 − 1)`, for
    `i` in 8192…16383 entry `(i + 0x3000) & 0x3FFF = float(1 − double(4(i − 8192))/16384)`;
  - square: +1 for the first 8192 entries, −1 after.
  EXEC (`run/wt`): FLEngine's own `Sin` gives the same 16384 floats as the C library's `sin` on this machine.
- Without a host table (a harness that leaves them null) the plugin faults at the first render with the LFO on
  **[EXEC]**, which is how the dependency was found.

### 1.4 Gain ramps and the kernel **[DECOMP `FUN_005dfe70`, `FUN_005def10` (through the pointer at `0x718428`), `FUN_005dffc0` + EXEC]**

Every render call ramps each gain from its current value towards its target over the call's n frames:

1. `d = target − start` (float); when `d`'s bit pattern is 0 nothing moves.
2. Otherwise the gain's new value is the target and `d = d/n`; if `|d|` exceeds the step limit, `d` takes the limit with
   `d`'s sign, the new value becomes `start + d·n`, and when that lands below 2⁻²⁴ in magnitude (bit comparison) it is
   set to 0 with `d = −start/n`.
3. The kernel (SSE, single precision) runs `outL = L·g0 + R·g2`, `outR = R·g1 + L·g3` with each gain advanced by its
   `d` after every frame (repeated float additions). Without any ramp it multiplies with the fixed matrix, or — when
   `g2` and `g3` are both +0 — just `L·g0`, `R·g1` (skipped entirely when both are exactly 1 and the call is in place).

The step limit comes from FL's shared smoothing helper (object at plugin `+0xCC`, its rate method `FUN_005dffc0`):
`float(44100/fs · 0.001)` for PanOMatic (`+0x34`) — 0.001 per sample at 44.1 kHz, about 23 ms for a full-scale move.

### 1.5 Flush and start **[DECOMP Dispatcher id 2 + EXEC]**

- Flush (Dispatcher 2) copies the four targets into the current gains and resets the LFO phase to 0.
- A freshly created plugin has all gains at 0 and its targets unset until the first render computes them, so its first
  23 ms fade in **[EXEC]**, flushed or not.

### 1.6 What LYNX does

`EffectKind::fruityPanOMatic` (`core/FruityBasicFx.{h,cpp}`): the six controls are the slot's parameters over FL's
ranges, so the LYNX editor and FL automation drive them; any change acts as FL's ProcessParam. The engine starts with
the current rate, then runs **one silent frame and a flush** (gains at their targets, LFO phase 0) — FL's mixer runs
its effects continuously, so a project that plays in FL does not hear the fade-in of §1.5; the exact host order at
project load/export is **not confirmed**. `Effect::reset()` = FL's flush. Host blocks longer than 512 frames are
processed as 512-frame calls (the LFO and the ramps step once per call; FL's own call size is **not confirmed**).

---

## 2. Fruity Balance

### 2.1 State and law **[DECOMP `FUN_006beaa0` (ProcessParam), `FUN_006be800` (constructor) + EXEC]**

- Record `0xD5` = two `i32`: balance −128…128 (0) and volume 0…320 (256), re-sent through ProcessParam after the load.
- Either control recomputes the four gain targets, in double unless stated:
  - `a = double(float(v_bal + 128)·(1/256))`, `b = double(float(v_bal)·(1/128))`;
  - `k = (1 − Sin(|a − 0.5|·π)·0.5)·c`, with `c = 1/(Cos(π/4)·10)` set by the constructor;
  - `r = double(v_vol)/320` (the control's maximum); `x = float(r·1.25)`; `G = float(exp(double(x)·ln 11) − 1)`;
    `g = double(G)·k` — at the centre this is the L11 law of round 17 divided by `cos(π/4)`;
  - `SinCos((b + 1)·π/4)` → `s`, `co`; `t0 = g·co`, `t1 = g·s`; then `t3 = t1 − t0` (`a ≥ 0.5`) or `t2 = t0 − t1`
    (`a < 0.5`), the other cross term 0.
  - The matrix is PanOMatic's (§1.2 notation); the targets are stored as doubles and converted to float at every render.
- So the centre is **not** a pure identity: `t0 = 1`, `t1 = 0.99999999999999978` and the left-to-right cross term
  `t3 = −2.2·10⁻¹⁶` (a nonzero float), which the kernel applies **[EXEC]**.
- The RTL functions: on every argument these laws can take (257 balance values, 321 volumes), the plugin's own Delphi
  `Sin`/`SinCos` differ from the C library's in the last bit of 10 doubles, and its `Exp` in 4; after the conversions
  to float none of the 82 497 target sets differs **[EXEC `rtl.exe`, `t_balrtl2.py`]**, so LYNX uses the C library.

### 2.2 Render, ramps, flush **[DECOMP `FUN_006befc0`, `FUN_005dfef0`, `FUN_005dee90`, Dispatcher `FUN_006bde30` + EXEC]**

Same ramp and kernel as PanOMatic (§1.4; the routines are identical copies), with the helper's other step,
`float(float(44100/fs·0.001)·10)` (`+0x38`, 0.01 per sample at 44.1 kHz). Then a peak meter for the editor (no effect on
the audio). Flush (Dispatcher 2) sets the current gains to the float targets; without it a new instance fades in from
0 over ≈ 2.3 ms. LYNX (`EffectKind::fruityBalance`) starts flushed; `reset()` = flush.

---

## 3. Fruity Blood Overdrive

### 3.1 State and controls **[DECOMP `FUN_006beaa0` (SaveRestoreState), `FUN_006bf130`, `FUN_006bf450` + EXEC]**

- Record `0xD5`: `i32` version (3 today; versions ≥ 4 are ignored), six `i32` controls, then (version > 2) one `i32`
  taken from the editor (not used by the audio path; values 0, 2, 3 in the data — which control it is, not confirmed),
  then an `i32` 0.
- Controls (DFM hints, tags 0…5): pre band, color, pre amp, ×100, post filter, post gain; ranges 0…10000 (×100: 0…1),
  defaults 0, 5000, 0, 0, 0, 10000 **[EXEC: control objects' min/max/default]**. The plugin value of each control is
  `float(clamp(double(raw − min)·scale, 0, 1))` with the control's scale 1/10000 (×100: 1).
- Derived (`FUN_006bfbc0`, floats; `K = 2.5000…·10⁻⁴` is the float `0x3983126F`):
  - pre gain target `cP = ((sw·(pre·pre·pre)) + 1)·K`, `sw = 25`, or 2500 when `double(×100) > 0.5`;
  - band mix target `cB = (1 − preband)·K`; post gain target `cG = (post·post)·K`;
  - filter coefficient target `cF = ((color·color)·color)·K`;
  - post filter `q = postfilter·0.7 + 0.2`, `q' = 1 − q`.

### 3.2 Render (per frame, floats, FTZ/DAZ) **[DECOMP `FUN_006bf610` + objdump `out/bo_r.asm`]**

1. Four one-pole smoothers with the float coefficient `m = 0x3F7FEF9E` (≈ 0.99975): `P = P·m + cP`, `B = B·m + cB`,
   `G = G·m + cG`, `F = F·m + cF` (they converge to the targets divided by `1 − m`, which `K` approximates).
2. Per channel, a state-variable filter with coefficient F: `low += F·band; high = (x − low) − band; band += F·high`.
3. Then `u += v·0.25; w = ((((high + low)·B) + band)·P − u) − v; v += w·0.25` (a second integrator pair).
4. `y = ((S(u) + S(v)) + S(w))·G`, clamped to ±`0x3F7FFCB9` (≈ 0.99995), where `S(t)` reads a 65536-point table at
   `trunc(double(float(t·32767)))` (64-bit truncation, its low 32 bits) `+ 0x8000`, clamped to 0…65535
   (`FUN_006bfcf0`).
5. Post filter, two cascaded one-poles: `y1 = y·q' + y1·q`, `y2 = y1·q' + y2·q`; the output is `y2`.

The left channel runs completely before the right one in each frame; both share the smoothers (step 1 runs once per
frame). The table (constructor `FUN_006becd0`): entry `0x8000 + k = float(Sin(double(k)·π/65536))`, entry
`0x7FFF − k` = the same value with its sign bit flipped (so entry `0x7FFF` is −0), k = 0…32767. EXEC: the plugin's own
`Sin` and the C library's give the same 32768 floats (1020 last-bit differences in double vanish in the conversion).

### 3.3 Start and flush **[DECOMP]**

- Construction sets P = B = G = 1, F = 0 and every filter state to 0.
- Loading a state (after the controls are re-sent) runs the four smoothers alone for as many frames as the helper's
  sample rate (`+0xCC` object, `+8`: 44100 until the rate notification), i.e. one second of settling.
- No flush: Dispatcher 2 is not handled, the filter states persist.

LYNX (`EffectKind::fruityBloodOverdrive`): rate first, then the state (settling for `rate` frames); `reset()` does
nothing (as FL). A control change re-derives the targets, and the smoothers glide to them as in FL.

---

## 4. Run-time environment and host order

- The plugins run with MXCSR `0x9FE0` (FTZ + DAZ) after creation **[EXEC]**; LYNX's renderer runs effects with FTZ/DAZ.
  Blood Overdrive depends on it (decaying filter states).
- FL's order at project load (rate before or after the state, flush or not before playback/export) was not observed
  (**not confirmed**); LYNX uses rate-first and the starts described in §1.6, §2.2 and §3.3. All three variants of the
  start were verified (§5).

---

## 5. Verification **[EXEC]**

Signals: `music` (kick/hat/chords with a gap and three level stages up to +8 dB), `loud`, `burst`, `quiet` (round 51's
set); 3 s each unless stated.

Cases per set: PanOMatic 54 installation states + 10 synthetic (every shape, target, extreme pan/volume/amount/speed);
Balance 10 + 12 synthetic (balance −128…128, volume 0…320); Blood Overdrive 19 + 6 synthetic (every control at 0 and
full, ×100 on/off). "prime" = LYNX's start (DLL: `warm=1` + flush), "fade" = a fresh instance without it, "flush" /
"fade" for Balance likewise; Blood Overdrive "early" = rate before the state (settling 1 s at the project rate),
"late" = state first (settling 44100 frames).

| Check | Cases | Result |
|---|---|---|
| Python models (`t_pom.py`, `t_bal.py`, `t_bo.py`) vs DLL, music | 63 + 20 + 19 states, 44.1/48/96/22.05 kHz, blocks 512/300/37/1024/128 | all bit-identical |
| LYNX C++ (`t_cpp.py`) vs DLL, 44.1 kHz / 512, signals music, loud, burst, quiet | 64 PanOMatic (prime and fade), 22 Balance (flush and fade), 25 Blood Overdrive (early) per signal | all bit-identical (512 + 176 + 100 runs) |
| LYNX vs DLL at 48 kHz / 300, 96 kHz / 1024, 22.05 kHz / 37 | same sets, every start mode, Blood Overdrive early and late | all bit-identical |
| Control changes during playback (`t_auto.py`: FL's ProcessParam between calls vs LYNX's `set`), blocks 512 and 480 | 2 PanOMatic (8 and 6 changes incl. target/shape/speed), Balance (5), Blood Overdrive (7) | all bit-identical |
| Balance targets (`t_sweep.py`, the plugin's doubles as floats vs LYNX's) | 257 balance values, 321 + 46 volumes | 624/624 equal |
| PanOMatic LFO increments / pan / volume values (`t_sweep.py`) | 571 speeds × 44.1/48/96 kHz; 86 pans × 30 volumes | SWEEP2 |
| RTL vs C library (`rtl.exe`) | FLEngine sine table; PanOMatic `Exp`/`Ln` on all 321 volumes and 55 001 speeds; Balance `Sin`/`SinCos`/`Exp`; Blood Overdrive `Sin` on 32768 points | equal after the conversions to float (§1.3, §2.1, §3.2) |
| Unit test `fruity_panomatic_balance_blood_overdrive` | DLL goldens (3 PanOMatic, 2 Balance, 2 Blood Overdrive), import path with host blocks 1024/2048, texts, fallbacks | within 2·10⁻⁷ (bit-identical on the test machine) |

Import (`tests.cpp`, demo-project battery): every PanOMatic, Balance and Blood Overdrive mixer slot of the
installation's projects becomes the exact effect.

**Cost** (one core, 44.1 kHz, 20 s of stereo noise, `cpp/fxcmp`, file I/O included): PanOMatic and Balance ≈ 0.5 %
of real time, Blood Overdrive ≈ 3 %.

---

## 6. What changed in LYNX

- `core/FruityBasicFx.{h,cpp}`: the three engines (PanOMatic with FL's host LFO tables and gain ramps, Balance,
  Blood Overdrive with its sine shaper table), the state parsers and parameter conversions, the mixer effects.
- `EffectKind::fruityPanOMatic`, `fruityBalance`, `fruityBloodOverdrive` (appended to `Song.h`), wired through
  `Effects.cpp` (info, texts, factory), `FlEffects.cpp` (`fruityBasicExact` before the old routes, automation of every
  control as FL's value over its range, `flPluginsForEffect`), `FlpImport.cpp` (import report for an unreadable state),
  `ProjectFile.cpp` (ids) and `MixerComponent.cpp` (addable effects).
- Fallbacks: a state that cannot be read keeps the old `utility`/`distortion` approximations, and the import report
  lists it.
- Tests: `fruity_panomatic_balance_blood_overdrive` (DLL goldens for the three engines, the import path with host
  blocks of 1024/2048, parameter texts, fallbacks) and the demo battery count (every mixer slot of the three plugins
  in the installation's projects on the exact engines).

---

## 7. Open items

- FL's host call order at project load and export (rate/state order, flush before an export) and the size of FL's
  render calls to mixer effects are not confirmed; PanOMatic's LFO and both plugins' ramps step once per call, so they
  depend on it (§1.6).
- The editor-only `i32` of Blood Overdrive's state (version > 2) is not identified.
- Library-function differences on other platforms (e.g. a Windows C library's `exp`/`sin`/`cos`) could change single
  parameter values; on this machine they match the DLLs' RTL after rounding (§1.3, §2.1, §3.2).
