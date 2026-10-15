# Behavioural analysis of FL Studio 2026: round 55 (REA) — Fruity Flangus, Fruity Fast Dist and Fruity Flanger

> **Nature of this document.** Continuation of `REA_rodada8.md` §3 (Fast Dist) and §5 (Flangus), whose layouts are
> confirmed here and whose open items (the Flangus LFO, the host waveshaper's variants) are settled; first decoding
> of the Fruity Flanger. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was
> copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was opened; no Image-Line
> preset or audio data was copied into the repository (Fast Dist's curves are rebuilt from their formula at run time).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r55/`.
- **Targets:**

  | File | SHA-256 |
  |---|---|
  | `Plugins/Fruity/Effects/Fruity Flangus/Fruity Flangus_x64.dll` | `cc83092fbfa95992d32a6b9cbe7e92c4839d6b0db9d8456f666a8b509e063d73` (= round 8) |
  | `Plugins/Fruity/Effects/Fruity Fast Dist/Fruity Fast Dist_x64.dll` | `cba6ef46e8f18c6a66d21656c0afb7a7e2e501ae1c969bfd566ea857847d682a` (= round 8) |
  | `Plugins/Fruity/Effects/Fruity Flanger/Fruity Flanger_x64.dll` | `2d2ca0c8b47530ae536e15e68c4eb8c7bd15f7d3acb0200b967ed4e29153e05d` |
  | `FLEngine_x64.dll` (host waveshaper of Fast Dist) | `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` (= rounds 28/43) |

- **Tools:** headless Ghidra 12.1.4 (projects `~/ghproj/r55`, `gh/scripts/DecRange.java`), `objdump -M intel`
  (`out/*.asm`, `py/dis.sh`) wherever the operation order matters; round 43's FLEngine disassembly for the host side.
- **EXEC harness:** `run/fx.c` → `run/fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu): round 51's effect harness
  (unmodified DLL, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state restored through the
  plugin's own SaveRestoreState; Dispatcher sample rate then block size; Eff_Render), extended this round with **host
  slot 24 (vt+0xC0) forwarded to the installation's own `FLEngine_x64.dll`**: the DLL is mapped without running its
  DllMain or resolving imports (round 43's `fldump` method), its table builder `FUN_00b96cc0` runs once, the two
  code-pointer variables are set the way FL's start-up sets them (§2.3; `flsse=1` selects the other variants for
  comparison), the render flag byte is set by the option `flr=0|1`, and the plugin's call goes to FLEngine's own
  `0x132adc0`. `run/fldump.exe` (round 43) dumps the host tables (`data/shaper.bin`).
- **Drivers (our own code):** `py/survey55.py` (every Flangus / Fast Dist / Flanger mixer state in the installation →
  `data/states55.pkl`), `py/fx.py` (runs the harness; test signals), models `py/fdmodel.py`, `py/fgmodel.py`,
  `py/flmodel.py` (built only from the facts below), `py/t_tab.py` (host curves), `py/t_fd.py`, `py/t_fg.py`,
  `py/t_fg2.py`, `py/t_fl.py` (models vs DLL), `py/t_cpp.py` + `cpp/fxcmp.cpp` (LYNX's C++ engines vs DLL, built for
  Linux and with zig for Windows), `py/gold55.py` (goldens of `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, 44.1 kHz, blocks of 512 unless stated;
"−∞" = bit-identical.

**Installation usage [DATA]:** Fruity Fast Dist in 52 mixer slots (37 in *Demo projects*; 17 files), 31 distinct
states; Fruity Flangus in 55 slots (28 in *Demo projects*; 19 files), 18 distinct states; Fruity Flanger in 5 slots
(4 in *Demo projects*, 1 template; 4 files), 3 distinct states. Before this round LYNX imported Fast Dist as its
generic distortion with a tanh drive fitted to FL's curve (round 8 §3.2), Flangus as its generic chorus without
feedback, and the Flanger as a default chorus.

---

## 1. Fruity Flangus

### 1.1 State and controls **[DECOMP `FUN_006b6b20`, `FUN_006b6be0`, `FUN_006b6fb0` + EXEC]**

- State = 8 × i32 (32 bytes, no version), copied raw into the plugin; then every control is sent again through
  ProcessParam in index order (each send of controls 0…5 re-runs the voice set-up, §1.3).
- Controls (min…max, default) **[EXEC: control objects]**: order 1…9 (4), depth 0…1024 (512), speed 0…1024 (768),
  delay 0…128 (8), spread 0…128 (128), cross −128…128 (−32), dry −128…128 (96), wet −128…128 (96).
- The constructor first stores every default, then sends each through ProcessParam (so its first set-up already
  sees all defaults).
- Dry / wet gain: `x = float(v)·(1/128)`; `a = |x|`; `g = 1` if `a = 1`, else
  `float((exp(double(float(a·2.39789534f))) − 1)·double(0.1f))`; the result is multiplied by the sign of `x` taken
  from its bits (0 for ±0, −1 when the sign bit is set).
- Engine values: order (integer), `depth = float(double(v)/1024)`, `speed` likewise, `delay = float(v)·(1/128)`,
  `spread`, `cross` likewise.

### 1.2 Dispatcher **[DECOMP `FUN_006b6680`]**

- id 4 (sample rate): the framework's rate record (`FUN_005e00a0`) stores the rate and the gain-ramp limit
  `maxStep = float((44100/fs)·0.001)` (double steps) **[EXEC: 0.001 at 44.1 kHz, 0.00091875 at 48 kHz]**; then
  the set-up.
- id 3 (block size): stored (it sizes the delay line) and the set-up.
- id 2 (flush): the three ramped gains jump to their targets, every voice's LFO phase returns to its start value and
  the delay line is cleared.

### 1.3 Voice set-up **[DECOMP `FUN_006b3ef0` (+ `FUN_006b3e30`, nested `FUN_006b3eb0`), objdump]**

Constants of the code: `ln 11`, `ln 101`, `ln 1001` (each computed by the RTL's `Ln`), the float table of 20 spread
factors at `0x704e78` (0.967, 1.039, 1.021, **0.1013**, 0.977, 1.033, 0.997, 0.971, 1.061, 0.947, 1.031, **0.1009**,
1.063, 0.953, 0.991, 1.049, 0.983, 1.051, 1.019, 1.069 — two entries are ten times smaller than their neighbours)
**[DATA]**. In order (float unless stated; one rounding per step):

1. `order = max(order, 1)`; `D = float(exp(double(depth)·ln 11) − 1)·109.25`;
   `P = float(exp(double(1 − speed)·ln 101) − 1)`; `R = (2³¹ / (double(P)·75000 + 50000))·500` (double);
   `B = float(exp(double(delay)·ln 11) − 1)·441`; spread weight `w = float(exp(double(spread)·ln 1001) − 1)/1000`;
   `blend(a, b) = a·double(w) + b·double(1 − w)` (double).
2. The voice list is resized to `2·order` (entries kept, **new entries zero-filled — including their LFO phase**).
3. For voice `i` (0 … 2·order − 1): left voices `i < order` with `f = i`, channel L, start phase 0; right voices with
   `f = 2·order − 1 − i`, channel R, start phase 2³¹. `k = 1 − f/order`; `r = factor[i mod 20]`;
   `rr = float(blend(double(r·r), 1))`. Gains: own channel 1, other channel `cross·k` (swapped for right voices);
   `sum = float(double(sum) + |gL| + |gR|)`. `d0 = D·k + 10`;
   `rate = blend(R·double(rr)/double(d0), R/double(D + 10))`;
   `center = float(blend(double((B·k)·rr), double(B)))`;
   start phase `+= Round(((k·w)·2³¹)/256) << 8` (32-bit); `fsr = float(44100/fs)`;
   phase increment `= Round(rate·double(fsr))` (low 32 bits); `d0 = d0/fsr`;
   `d3 = center/fsr`, replaced by 8 unless `d3 > 8`; `center = d0 + d3`; the running maximum of `center + d0`;
   LFO scale `dd = sqrt(double(d0·4))`, stored as `float(dd/2³¹)`, `float(dd)`, `float(dd/2)`.
4. Normalisation `g = float(exp(double(1 − 1/sum)·ln 1001) − 1)/1000`; every voice gain ×`(1 − g)` (the engine's
   factor at `+0x50`, 1, never changed).
5. `maxd = Round(maximum) + 8`. The delay line holds `(block + max(maxd, 1))` stereo frames; **when that size
   changes, or when the spread value differs from the one of the last reset, the line is cleared and every voice's
   phase returns to its start value.** (Most changes of order, depth or delay change `maxd`, so they clear the line.)

Round 8's "f ≈ 250·44100/(P·Dv)" is this rate with the 2³² cycle: the LFO period in samples is
`2³²/increment`.

### 1.4 Render **[DECOMP `FUN_006b7510`, `FUN_006b45f0`, `FUN_006b47f0`, `FUN_006b4680`, `FUN_006b4730`, objdump]**

1. The input block is copied into the circular delay line (interleaved stereo frames).
2. **Voices (x87).** The kernel is taken from a pointer set at creation: the x87 one unless the CPU record's byte
   `+0x16` is set (no instruction writes it; it is 0 at EXEC: the x87 kernel `0x6b47f0` is the one installed).
   For the whole block the control word is `0x0F3F` (64-bit precision, **every rounding toward zero**), restored to
   `0x033F` afterwards. Per voice in list order, per frame: `t = |float(phase as int32)·s₈| − s₁₀`;
   `D = t·(s₉ − |t|) + center` (a parabolic LFO between `center − d0` and `center + d0`); `j = trunc(D)`,
   `frac = D − j`; with `x₀` = the voice's channel `j` frames back from the current frame (the current input frame
   being 0 frames back) and `x₁` one frame older: `o = x₀ + (x₁ − x₀)·frac`; the first voice stores
   `float(o·gL)`, `float(o·gR)`, later voices `float(o·g + previous)`. The phase advances by the increment after
   each frame (32-bit wrap).
3. **Dry:** the output is the input × the dry gain through the framework's gain ramp (§1.5).
4. **Wet:** the voice sum × the wet gain (left and right ramps, same target) is added to the output (§1.5).

### 1.5 Framework gain ramps and mixers **[DECOMP `FUN_005dff50`, `FUN_005db1d0`, `FUN_005db080`, `FUN_005df770`, `FUN_005de870`, `FUN_005dea40` + EXEC: IPP pointers]**

- Ramp per block of n frames: `step = target − state`; if non-zero: `state = target`, `step = step/n`; if
  `|step| > maxStep`: `step = ±maxStep`, `state = start + step·n`, and if that is below 2⁻²⁴ in magnitude (bit
  compare) the state becomes 0 with `step = −start/n`. The block uses `start` and `step`.
- Constant gain (step 0): 0 → output cleared (`ippsZero`), 1 → input copied (`ippsMove`), else `ippsMulC_32f`.
  Ramped: frames in pairs, gain lanes starting at `start` and `start + step`, each advanced by `2·step` (float adds)
  per pair; the frames after the last multiple of 8 continue lane 0 by `step` per frame.
- Wet: both gains 0 (bit compare) → nothing; both 1 → `ippsAdd_32f_I`; else `out += sum·g` per channel (SSE, float);
  ramped as above with separate left/right lanes.
- The ramp states start at 0 when the plugin is created: without a flush the first blocks fade in from silence
  **[EXEC]**.

### 1.6 Run-time environment **[EXEC]**

MXCSR `0x9FE0` (flush-to-zero, denormals-are-zero) after creation, as in round 51.

---

## 2. Fruity Fast Dist

### 2.1 State and gains **[DECOMP `FUN_006baf30`, `FUN_006bb260` + EXEC]**

- State = 5 × i32 (pre, threshold, type, mix, post), raw, no version, no clamping; then every control is re-sent.
- Controls **[EXEC]**: pre 64…192 (128), threshold 1…10 (10), type 0…1 (0 = A), mix 0…128 (128), post 0…128 (128).
- Gains (only controls 0, 3, 4 recompute them; L11 = the dry/wet law of §1.1 without the sign):
  `pre = L11(v_pre/128)`, `post = L11(v_post/128)`, `wet = L11(v_mix/128)·post`, `dry = ((post − wet)·pre)·post`.

### 2.2 Render **[DECOMP `FUN_006bb7c0`]**

The input is copied to the output, then the plugin calls the **host's waveshaper** (FL host interface slot 24,
`vt+0xC0`) once on the interleaved block (2n samples) with set = type, curve = threshold, and (dry, wet, pre). The
plugin has no DSP of its own.

### 2.3 FL's host waveshaper **[DECOMP FLEngine `0x132adc0`, `0x6a2b70`, `0x6a2bf0`, `0x6a5aa0`, `0x6a5c20`, `0x6a6c97…0x6a6e5a`; EXEC]**

- `0x132adc0` picks the curve (2 sets × 10 curves of 8194 int16; the curve for threshold t is the t-th, element 0
  being the value for input 0) and calls one of two code pointers with `N = 8191`: the **interpolating** routine when
  the byte at `0x16baff0` is set, the **non-interpolating** one otherwise.
- FL's start-up fills those pointers with the x87 routines (`0x6a2bf0` / `0x6a2b70`) unless byte `+0x16` of its CPU
  record is set, in which case the SSE ones (`0x6a5aa0` / `0x6a5c20`, the ones round 43 described). No instruction
  writes that byte (the record is zeroed, then only bytes `+0x0…+0x9` and the feature-flag dword at `+0x12` are
  written; the dword ends at `+0x15`); the same CPU unit in the plugin DLLs leaves it at 0 on the test machine **[EXEC]**. LYNX therefore uses
  the x87 routines (that FL's own start-up ends with 0 there is an inference from the identical unit — not run).
- **The flag `0x16baff0`** is set and cleared only around two FLEngine routines (`0xc4dba0`, `0xd17fd0`), the first
  of which handles the `.wav` output file name; round 2 places the render-tail logic in the same area. Inference:
  set while FL renders to a file. LYNX takes the interpolating routine when it exports, the other one live.
- Live routine (x87, the thread's control word — `0x037F` at EXEC): `k = drive·8191`, `w = wet·2⁻¹⁵` in extended
  precision; per sample `i = round(x·k)` (fistp, nearest), `y = x·dry + (±T[min(|i|, N)])·w` with odd symmetry; an
  index outside the 32-bit range gives the "integer indefinite" −2³¹, i.e. `−T[N]`.
- Render routine (x87, control word `0x0F3F` for the call, then `0x033F`): `t = x·k`, `j = trunc(t)`,
  `frac = t − j`; `j ≥ 0`: `T[j′] + (T[j′+1] − T[j′])·frac`, `j < 0`: `−(T[j′] − (T[j′+1] − T[j′])·frac)` with
  `j′ = min(|j|, N)`; index out of range → 0; `y = x·dry + that·w`; every rounding toward zero.
- The SSE routines would give different live results (they truncate the index): up to −57 dB from the x87 live
  routine on the demo states, −143 dB for the render routine **[EXEC, `flsse=1`]**.

### 2.4 The curves **[DECOMP FLEngine `FUN_00b96cc0` + EXEC: `fldump` tables]**

For curve c = 1…10, k = 0…8191, `x = k/8191`, `d = 20·log₁₀ x` (−10000 for set 0, −1000 for set 1 when x = 0),
value `= Round(32767·10^(d'/20))`, element 8192 repeats 8191 (and element −1 repeats 0):

- set 0 (type A): `d' = (d + 25)·0.1·(11 − c) − 25` when `d ≥ −25`; then `+1.6·(c − 1)` unless `d' > −1.6·(c − 1)`;
- set 1 (type B): `d' = −Power(−d, 0.8 + 0.02·(10 − c))` (Delphi `Power` = `exp(y·ln x)`, 0 for x = 0).

All 20 curves rebuilt with this formula equal FL's tables entry for entry **[EXEC: `py/t_tab.py`]** (round 43 had
checked seven of them).

---

## 3. Fruity Flanger

### 3.1 State and controls **[DECOMP `FUN_006badc0`, `FUN_006baec0`, `FUN_006bb210`, `FUN_006bbb60` + EXEC]**

- State: i32 version (FL writes 1); **only a version < 2 is read**: then 12 × i32 controls, re-sent in order.
- Controls (all from 0; max, default) **[EXEC]**: delay (1024, 0), depth (5000, 2000), rate (5000, 2000), phase (360,
  118), damp (5000, 0), shape (1024, 0), feed (100, 23), invert feed (1024, 0), invert wet (1024, 1024), dry
  (5000, 3500), wet (5000, 3000), cross (5000, 2500).
- Each control is normalised `v = float(clamp(double(raw)·(1/max), 0, 1))` and updates:
  - delay/depth: `law(v, k) = float(((k + 1)·v)/(double(float(k·v)) + 1))` (double steps);
    `d0 = law(v_delay, −0.8)·20`, `d1 = law(v_depth, −0.8)·20` (ms);
    `base = trunc(((d0 + d1·0.5)·fs·4096)/1000)`, `swing = trunc((d1·0.5·fs)/1000)` (double);
  - rate/phase: `rate = law(v_rate, −0.9)·5` (Hz) → LFO increment `Round(((rate·4096)/fs)·4096)` (float steps),
    right phase = left phase + `Round(v_phase·2²⁴)`;
  - damp: `c = float(1 − v_damp)`;
  - shape: LFO table = `a·triangle + b·sine` with `a = Round(v·4096)`, `b = Round(double(1 − v)·4096)`, and the
    per-entry slopes `(a·Δtriangle + b·Δsine) >> 12` (32-bit products, arithmetic shift);
  - feed / invert feed: `fb = float(double(±v_feed)·0.98)` (negative when invert feed > 0.5);
  - invert wet / dry / wet / cross: `dry = v_dry`, `wet = ±v_wet`, `cross = ±v_cross` (negative when invert wet > 0.5).

### 3.2 LFO and delay lines **[DECOMP `FUN_006b90f0`, `FUN_006b91e0`, `FUN_006b9410`, `FUN_006b94a0`, `FUN_006b96b0`, `FUN_006b9830`, `FUN_006b98a0`, `FUN_006bc070`]**

- Tables of 4096 integers: triangle from a double accumulator (0 → 1 over 1024 steps of 1/1024, 1 → −1 over 2048,
  −1 → 0 over 1024; each `trunc(d·32767)`); sine `trunc(sin(i·6.283185307/4096)·32767)` (the constant as written);
  slopes `T[i+1] − T[i]`, the last one `T[0] − T[4095]`.
- LFO value of a 32-bit phase p: `(tab[(p >> 12) & 4095] + (p & 4095)·slope[…]) >> 12` (32-bit sum); the cycle is 2²⁴.
- Two delay lines (L, R) of a power of two ≥ `Round(fs/10)` floats; the fractional-position table holds
  `float(i/4096)`.
- At creation: LFO at 44.1 kHz, phase 0, sine shape; lines for 44.1 kHz; feedback states 0. Sample rate (dispatcher
  4): lines re-sized (FL reallocates without clearing — LYNX clears, not observable on the tested rates), LFO rate and
  delays recomputed. Dispatcher 11 with value 1 ("enabled"): lines cleared, LFO left phase reset to 0, feedback states 0.

### 3.3 Render **[DECOMP `FUN_006bc0e0`, objdump — SSE, float]**

Per frame: LFO values `vL`, `vR` (each phase then advances by the increment); positions
`u = ((int32)(swing·v) >> 3) + base` (fixed point, 12 fractional bits); the line receives `in + state·fb`; with `j =
u >> 12`, `x = s[w − j] + (s[w − j − 1] − s[w − j])·frac[u & 4095]` (the current frame is `w`); `w` advances;
`state += c·(x − state)`; output `L = (stateL·wet + inL·dry) + cross·stateR`, `R` symmetric.

---

## 4. What LYNX does

- **`EffectKind::fruityFastDist`, `fruityFlangus`, `fruityFlanger`** (`core/FlangusFastDist.{h,cpp}`): mixer
  effects whose parameters are FL's controls over their ranges, so the LYNX editor and automation can change them;
  a parameter change goes through the same update path as FL's ProcessParam (Flangus: the set-up of §1.3 with its
  delay-line clears).
- Fast Dist: the 20 host curves are rebuilt from their formula once (§2.4); exports (`offlineRendering()`) use FL's
  render routine, live playback the live one; both in `long double` with the x87 control word set explicitly.
- Flangus: the x87 voices in `long double` under a truncating control word; FL's ramps and mixers; a fresh or reset
  effect starts as after FL's flush (gains at their targets) rather than fading in from silence (§1.5), since FL's
  mixer runs long before playback starts; host blocks are split into 512-frame engine calls.
- Flanger: integer LFO and float lines as in §3; LYNX's reset clears the lines like FL's "enabled" notification.
- FL automation of these plugins' controls maps onto the same parameters; states that cannot be read (too short;
  Flanger version ≥ 2) keep round 8's approximations (Flanger: a default chorus) and the import report lists them.

---

## 5. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Host curves (formula vs `fldump`) | 2 sets × 10 curves × 8194 | all identical |
| Fast Dist model vs DLL + FLEngine | 31 demo states + 5 edge states × live/render × music (with ±10⁶/−3·10⁹ samples), loud, bursts | all 216 bit-identical |
| Flangus model vs DLL | 18 demo states × 5 (music 44.1/512, bursts, loud 48 kHz/300, saw 96 kHz/1000, music 22.05 kHz/77) | all bit-identical |
| Flangus model vs DLL: edge states, automation, flush | 9 states (order 0, −3, 9; extremes) × 3 automation scripts × with/without flush | all 54 bit-identical |
| Flanger model vs DLL | 3 demo + 4 synthetic states | all bit-identical |
| LYNX C++ (Linux g++) vs DLL | Fast Dist 70 × 2 signals; Flangus 22 states × with/without automation × 4 rate/block sets; Flanger 6 states × automation × 3 rate sets | all bit-identical |
| LYNX C++ built for Windows (zig, MinGW libm) vs DLL | Fast Dist 70, Flangus 44, Flanger 12 (44.1 kHz) | all bit-identical |
| Unit test `fruity_fast_dist_flangus_flanger` | goldens of 3 + 3 + 2 of our own states, import of each, fallback | goldens within 2·10⁻⁷ (bit-identical on the test machine) |

Import (`tests.cpp`, demo-project battery): every Fast Dist, Flangus and Flanger mixer slot of the installation's
projects becomes the exact effect.

---

## 6. Open items

- Fast Dist live vs file render: the flag's meaning (set while FL writes a `.wav`) is an inference from where it is
  set; whether FL's start-up could set the CPU byte that selects the SSE host routines is not confirmed by running it
  (only the identical unit of the plugin DLLs was run).
- Fruit Kick (round 43) calls the same host routine (set 0, curve 10, drive 0.1). Its port followed the SSE
  interpolating routine; it now uses the x87 routines of §2.3 (live / file render). With FL's own routine forwarded
  from FLEngine (`run/kd55.c`, `py/fkx.py`), Fruit Kick_x64.dll and LYNX's `fruitkickrender` are bit-identical on all
  11 states × 3 note cases × 44.1/48 kHz, live and render (66/66 each); before, live renders of distorted states
  differed by up to −60 dB **[EXEC]**.
- The x87 control word of FL's audio thread (Fast Dist's live routine does not set it): the plugins' value `0x037F`
  is assumed.
- Whether FL flushes Flangus (dispatcher 2) or sends the Flanger's "enabled" notification before playback, and in
  which order it sends sample rate, block size and the state, is not observed; LYNX starts both as flushed / enabled.
- Flangus's delay line is sized from the block size FL announces; LYNX's line is always long enough, which matches FL
  as long as FL never renders longer blocks than it announced.
- FL keeps the Flanger's reallocated delay memory without clearing it when the sample rate grows the line; LYNX
  clears it.
