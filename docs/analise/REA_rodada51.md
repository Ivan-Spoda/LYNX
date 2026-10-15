# Behavioural analysis of FL Studio 2026: round 51 (REA) — Fruity WaveShaper and Fruity Soft Clipper

> **Nature of this document.** Continuation of `REA_rodada8.md` §6 (Soft Clipper state and curve, confirmed here at
> the bit level) and first decoding of the Fruity WaveShaper. *Clean-room* reverse engineering, behavioural only. No
> Image-Line code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our
> own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was
> opened; no Image-Line preset or audio data was copied into the repository (the WaveShaper curve travels inside the
> user's own project state; the `Data\Maps\*.fnv` files shipped with the plugin are not used).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r51/`.
- **Targets:**

  | File | SHA-256 |
  |---|---|
  | `Plugins/Fruity/Effects/Fruity Soft Clipper/Fruity Soft Clipper_x64.dll` | `50aac72859d241664e2cd3cbe62c10ca87d63ed184c8582c43136dcc1ce772c6` (= round 8) |
  | `Plugins/Fruity/Effects/Fruity WaveShaper/Fruity WaveShaper_x64.dll` | `7c5bf09601ce77e4343805447b9ce27199b2efd65fae5e78f686c1bb8529da01` |

- **Tools:** headless Ghidra 12.1.4 (projects outside the scratch tree, `~/ghproj/r51`; scripts `gh/scripts/DecRange.java`),
  `objdump -M intel` for the x87/SSE operation order.
- **EXEC harness:** `run/ws.c` → `ws_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu): round 38/49's effect harness
  (unmodified DLL, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state restored through the
  plugin's own SaveRestoreState, Dispatcher sample rate / block size, Eff_Render), extended this round with: plugin
  VMT dump (`vt=1`), double-indirect function pointers (`fpp=`), raw bytes behind a global pointer (`gx=`), 16-bit
  array dumps (type `h`, used for the shaping table), and the thread's MXCSR / x87 control word around the render.
- **Drivers (our own code):** `py/survey51.py` (every Soft Clipper / WaveShaper mixer state in the installation's
  `.flp` files → `data/states51.pkl`), `py/ws.py` (runs the harness; test signals), `py/scmodel.py` and
  `py/wsmodel.py` (Python models built only from the facts below), `py/t_ws.py` / `t_ws48.py` (model vs DLL),
  `py/t_modes.py` (shaping tables of synthetic curves with every segment mode vs DLL), `py/t_cpp.py` (LYNX's C++
  engines vs DLL), `py/gold.py` (goldens of `tests/tests.cpp`), `cpp/wscmp.cpp` (LYNX's engines on a float file).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, 44.1 kHz, blocks of 512 unless stated;
"−∞" = bit-identical.

**Installation usage [DATA]:** Fruity Soft Clipper in 27 mixer slots (21 in *Demo projects*, 17 + 6 files), 12
distinct states, threshold 100 in 16 of them, 1 in 4; Fruity WaveShaper in 73 slots (62 in *Demo projects*, 19 + 10
files), 57 distinct states, layout versions 2 (22) and 4 (51). Before this round LYNX imported both as its generic
distortion: the WaveShaper's state was never read (default drive, a level FL never applies) and the Soft Clipper ran
as a zero-drive distortion with an output correction.

---

## 1. Fruity Soft Clipper

### 1.1 State and controls **[DECOMP `FUN_006beb00`, `FUN_006bee60`, `FUN_005dfd00` + EXEC]**

- Record `0xD5` = two `i32` copied raw into the plugin (threshold, post); after the copy every parameter is read back
  and re-sent through ProcessParam, so the derived values below are rebuilt; no clamping, no version field.
- Controls: threshold 1…127 (default 100), post 0…160 (default 128) **[EXEC: control objects' min/max/default]**.
- Threshold: `t = float(v) · (1/128)` (float), ceiling 1.0f; range `r = 1 − t` (float), replaced by 2⁻¹⁶ when
  `r ≤ 0`; `inv = 1/r` (float).
- Post: `n = float(v) · (1/128)` (float); gain 1 exactly when `n = 1`, otherwise
  `g = float((exp(double(float(n · 2.39789534f))) − 1) · double(0.1f))` — FL's level law L11 evaluated with a float
  ln 11 and a float 0.1.

### 1.2 Render **[DECOMP `FUN_006bf330` + EXEC]**

1. Input copied to the output buffer (when they differ); the curve then runs in place over the 2n interleaved samples.
2. **Curve** (the default kernel, x87): a sample is left unchanged unless its magnitude's bit pattern is greater than
   the threshold's, compared as signed 32-bit integers (so with a negative threshold every sample is processed).
   Otherwise, in extended precision, `y = (1 − e^{(t − |x|)·inv})·r + t` (the exponential through `f2xm1`/`fscale`),
   stored as a float, and the input's sign bit is OR-ed into the result's bits.
3. A peak meter (editor only) when the editor is open.
4. When `g ≠ 1`: every sample multiplied by `g` in float.

The kernel is taken from a function table filled at load time: x87 versions by default, SSE versions (an `exp`
call in double, a float `(t − |x|)·inv`) only when a byte of the DLL's CPU record is non-zero; no instruction in the
DLL writes that byte and it is 0 at EXEC, so the x87 kernel is the one used **[DECOMP `FUN_005e1f00…22d0` + EXEC]**.

### 1.3 Run-time environment **[EXEC]**

After the plugin is created the thread runs with MXCSR `0x9FE0` (flush-to-zero and denormals-are-zero set) and the x87
control word `0x037F` (64-bit precision, round to nearest). Whether FL's audio thread has the same settings is not
confirmed (LYNX renders with FTZ/DAZ, and its x87 kernels set the control word explicitly).

---

## 2. Fruity WaveShaper

### 2.1 State **[DECOMP `FUN_00831040` + DATA]**

| Field | Type | Content |
|---|---|---|
| version | i32 | `0x00040003` today; the high word is the layout (2…4; a plain 3 is read as layout 3); > `0x40003` is rejected |
| pre, mix, post | 3 × i32 | the three wheels, raw (ranges in §2.2) |
| unipolar | u8 | the curve's polarity button (`PolarBtn`, hint "Unipolar / bipolar mode") |
| center | u8 | `CenterBtn` ("CENTER", hint "Remove DC offset") |
| oversampling | i32 | `OverSelect`, 0…4 |
| map | — | FL's envelope-editor map: `i32 kind (3)`, `i32 version (3)`, `i32 n`, n × (`f64 Δt`, `f64 level`, `f32 tension`, `u8 mode`, 3 bytes), then a 20-byte header (flags, enabled, decay, loop, sustain points) |
| layout ≥ 2 | i32 | bit 0: an editor-only toggle (the form control at `+0x768`; not referenced by the audio path — which button it is, not confirmed) |
| layout ≥ 3 | u8 | `HqCheckBox` (hint "Don't truncate below −85dB") |
| layout ≥ 4 | u8 | `SmoothCheckBox` |

Demo usage: unipolar 5 slots, CENTER 7, oversampling 1/2/4 in 3/5/1, HQ 5, smooth 4.

### 2.2 Controls and gains **[DECOMP `FUN_00831c40` + EXEC]**

Ranges **[EXEC]**: pre 64…192, mix 0…128, post 0…128 (defaults 128 each), oversampling 0…4 (default 0). Any change
recomputes, in float unless stated, with `k = 1/128`:

- `pre = float(exp(double(float(v_pre)·k) · ln 4) − 1) / 3` (so 1 at 128, 1/3 at 64, 7/3 at 192);
- `g` = the post law of §1.1 applied to `v_post`;
- `wet = (float(v_mix)·k) · g`;
- `dry = ((g − wet) · pre) · g`.

The output is `dry·x + wet·shape(pre·x)` (§2.5): the dry part is taken from the un-amplified input but scaled by the
pre gain.

### 2.3 Shaping table **[DECOMP `FUN_00832ba0` + EXEC: table dumps]**

- N = 16 383 (bipolar) or 32 767 (unipolar); the table holds N + 3 signed 16-bit entries, indices −1…N+1.
- Entry i (0…N) = `clamp(round(double(v_i) · S), −32768, 32767)` with S = 32 767 (bipolar) or 65 535 (unipolar),
  round-half-even, `v_i` = the map evaluator's float output after i steps of `1/N` (double); entry −1 copies entry 0
  and N+1 copies N.
- The map's levels are offset by −0.5 in unipolar mode (the curve then spans −0.5…0.5).

### 2.4 The map (FL's shared envelope library) **[DECOMP `FUN_00776550`, `FUN_007797f0`, `FUN_0077a330`, `FUN_0077c550`, `FUN_0077d490`, `FUN_0077bd20` + EXEC]**

- **Load:** Δt and level are kept as floats; a NaN level takes the previous point's. The map's time range is fixed
  at 1: when the first point starts after 0 a point (0, first level) is inserted; then every Δt is scaled by
  `1/Σ Δt` (sum in double of the floats; skipped when the factor is exactly 1), as `float(Δt · factor)`; a map whose
  total time is 0 gets a point at time 1 with the last level. Negative Δt become 0.
- **Per-point preparation (point i > 0, the segment from i−1 to i):** sign = sign of the tension (0 when none).
  - modes 0/1 (single / double curve): `k = float(exp(|t|·ln 1 000 001) − 1)·sign`, `1/|k|` and `ln(|k|+1)` as floats;
  - modes 3–6 (steps / smooth steps / pulse / wave): a count `round(float(exp(float(1 − 0.6|t|)·ln 1 000 001) − 1) · 0.001)`
    (+1 for mode 3 with t ≥ 0), from which mode 3 takes `count + 1` and `1/(count + 1 − sign)` with an offset ±0.5,
    mode 4 `count + 1` and its inverse, mode 5 `2(count + 1)` with ±0.5, mode 6 `count + 0.5` (with count 0 and a
    positive tension it is linear);
  - modes 7/8: the tension; mode 9: |tension|;
  - mode 10 (smooth): a monotone cubic Hermite segment whose two end slopes are weighted harmonic means of the
    neighbouring segments' slopes (only neighbours that are themselves mode 10 count; the weights are float sums
    `2·r + 1` and `r + 2` of the neighbour/segment duration ratio `r`, floored at 2⁻²⁴); flat segments are linear;
    the third parameter is `1 − |tension|`;
  - modes 11/12: from `u = 1 − max(0.9|t|, 2⁻¹⁶)`, `p = 1 − u³`, `(1 − p)/p` and its signed variants (halved for 12);
  - modes 0, 1, 7, 8, 9, 11, 12 with no tension are linear.
- **Evaluator:** a stepper with a segment index, a phase and a rate. Start: index 0, the first level, rate 1 and
  phase 1 when the first Δt is 0 (time scale / Δt and phase 0 otherwise). A step of `dx` adds `dx·rate` to the
  phase; while the phase is ≥ 1 it moves to the next segment: the leftover phase is turned back into time
  (`(phase − 1)/rate`), the start level becomes `float(level(i))`, the index advances (it stays on the last point at
  the end, where the phase and rate become 0), the segment's span is `float(level(i+1) − start)`, and the new rate is
  `1/Δt` (phase multiplied by it) — or rate 1 and phase + 1 when Δt is 0. The value is
  `start + shape(phase)·span` in float, `shape` being each mode's formula (double, rounded to float; hold / unknown
  modes give 0).

EXEC: all 57 demo curves give tables identical to the DLL's; so do 168 synthetic curves (every mode 0…13, six
tensions including ±1, bipolar and unipolar, totals ≠ 1).

### 2.5 Kernels **[DECOMP `FUN_00629d80`, `…9e00`, `…9f50`, `…9fd0` (x87) + EXEC]**

All four compute `dry·x + T(pre·x)·wet·2⁻¹⁵` in x87 extended precision and store a float.

| Kernel | Used when | Table read |
|---|---|---|
| bipolar, nearest | HQ off and not rendering | index `round(x·pre·N)` (current rounding = nearest-even), odd-symmetric (`−T[min(−i, N)]` for i < 0) |
| bipolar, interpolated | HQ on, or rendering | `i = trunc(x·pre·N)`, `T[i] + (T[i+1] − T[i])·frac` with the same odd symmetry |
| unipolar, nearest | as above | index `round(x·pre·N/2 + N/2)` clamped to 0…N |
| unipolar, interpolated | as above | `i = trunc(x·pre·N/2 + N/2)` clamped to −1…N, linear |

The interpolating kernels load the control word `0x0F3F` for their duration: **every rounding inside them —
products, sums and the final float store — is toward zero**; the others keep the host's (round to nearest). An index
outside the 32-bit range (x87 "integer indefinite") gives the end entry, negated, in the nearest bipolar kernel and
0 in the interpolated one.

### 2.6 Oversampling **[DECOMP `FUN_00832300`, `FUN_008300a0`, `FUN_00830140`, `FUN_0082fb70`, `FUN_0082fd40`, `FUN_00627920` + EXEC]**

- Order k from the state (factor 2^k); **when FL renders (§2.9) the order becomes 4 (×16), but only if the state's
  order is not 0** (with 0 the render path is not oversampled at all).
- Each order is a 2× stage, separate for up- and down-sampling, built from a polyphase IIR half-band: two chains of
  three first-order all-passes `y = x[n−1] + (x − y[n−1])·a` in double, coefficients
  {0.14737711360104660, 0.48246854276970014, 0.88300502576937310} and
  {0.03915159773446005, 0.30264684832849340, 0.67461591854696390} **[DATA]**.
  Up: each input sample feeds both chains; the second chain's output is the even output sample, the first chain's
  the odd one (floats). Down: even samples feed the first chain, odd samples the second; the output is
  `float((second + first)·0.5)`.
- After each stage call (each host block) the six all-pass outputs (per channel) with magnitude < 2⁻²⁴ are zeroed;
  the input-delay states are not. Stage states are cleared when the order changes or the plugin is flushed.

### 2.7 Smooth **[DECOMP `FUN_00832390`, Dispatcher `FUN_00830bf0` id 4]**

With the smooth option, the three gains glide per (oversampled) frame: `s += (target − s)·c` in float, pre / wet /
dry in that order, starting at 0 when the plugin is created; `c = float(1 − 0.1^{1/(double(float(fs)·0.005f))})`
(Delphi `Power` = `exp(y·ln x)`), with fs the host rate. Each frame then runs the kernel on its two samples.

### 2.8 CENTER (DC removal) **[DECOMP `FUN_00627af0`, `FUN_006278c0`, `FUN_00836010` + EXEC at 44.1 and 48 kHz]**

After down-sampling, at the host rate, per channel: `v += (x − p)·a; p += v; v ·= b; y = x − p` in x87 extended
precision with the two states kept in registers for the whole block, then stored as doubles (zeroed when
|value| ≤ 2⁻²⁴); `a = float(2.2835·10⁻⁵ · 44100/fs)`, `b = 0.96f`.

### 2.9 Process modes **[DECOMP Dispatcher id 1]**

FL's process-mode notification sets a "rendering" flag from **bit 1 of its value** (`PM_HQ_NonRealtime` in FL's SDK
naming — the name is an inference from the SDK; which value FL sends during an export was not observed). With the
flag: interpolated kernels and the ×16 order of §2.6. EXEC uses the value 2.

### 2.10 Render order **[DECOMP `FUN_00832390`]**

Peak meter (editor only) → up-sampling (order > 0) → kernel (whole block, or per frame with smooth) →
down-sampling → CENTER. Environment as in §1.3 (MXCSR FTZ+DAZ: denormal results of the SSE stages are flushed).

---

## 3. What LYNX does

- **`EffectKind::fruitySoftClipper`** and **`EffectKind::fruityWaveShaper`** (`core/FruityShapers.{h,cpp}`): mixer
  effects whose parameters are FL's controls over their ranges (Soft Clipper: threshold, post; WaveShaper: pre, mix,
  post, oversampling, HQ, smooth, CENTER, unipolar), so the LYNX editor and automation can change them; the
  WaveShaper keeps FL's saved state in `EffectSlot::state` for its curve (the controls in the state are copied into
  the parameters at import). Both tables (bipolar and unipolar) are built when the effect is created.
- Exports (`offlineRendering()`) take FL's rendering behaviour (§2.9); live playback uses the state's options.
- The x87 kernels run in `long double` with the control word set explicitly (nearest, or toward zero for the
  interpolating kernels); host blocks longer than 512 frames are split into 512-frame engine calls.
- FL automation of the WaveShaper's pre/mix/post and the Soft Clipper's threshold/post maps onto the same parameters.
- **Fallbacks:** a state that cannot be read keeps the previous distortion approximation, and the import report
  lists it.

---

## 4. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Soft Clipper model (`scmodel.py`) and LYNX (`t_cpp.py sc`) vs DLL, music / loud / noise bursts | 12 demo states + 6 edge states (threshold 0, −20, 300, …) × 3 signals | all bit-identical |
| WaveShaper tables, model vs DLL | 57 demo curves; 168 synthetic curves (modes 0…13) | all identical |
| WaveShaper model (`t_ws.py`) vs DLL, live and rendering | 57 states × 2 | all bit-identical |
| LYNX (`t_cpp.py ws`) vs DLL, 3 s music, 44.1 kHz / 512 | 57 states × live / rendering | all 114 bit-identical |
| LYNX vs DLL, 48 kHz / blocks of 300 | 57 states × 2 | all 114 bit-identical |
| Unit test `fruity_waveshaper_and_soft_clipper` | 3 Soft Clipper goldens, 3 WaveShaper states of our own × live/render, tables, import, fallback | goldens within 2·10⁻⁷ (bit-identical on the test machine) |

Import (`tests.cpp`, demo-project battery): all 73 WaveShaper and 27 Soft Clipper mixer slots of the installation's
projects become the exact effects (plus WaveShaper nodes inside Patcher graphs).

**Cost** (one core, 44.1 kHz, 20 s of stereo noise, `cpp/wscmp`, file I/O included): a demo state without oversampling
< 0.5 % of real time; ×4 oversampling 1 %; the ×16 rendering path 3.7 %.

Without FTZ/DAZ the oversampled states differ from the DLL only in samples below the float normal range
(≈ −790 dB); LYNX's renderer runs with FTZ/DAZ, as the plugin does.

---

## 5. Open items

- Which process-mode value FL sends when it exports (and whether "HQ for all plugins" matters) was not observed; LYNX
  treats every export as FL's rendering mode (§2.9).
- The MXCSR / x87 control word of FL's own audio thread is not confirmed (§1.3); the results above hold for the
  plugin's own settings.
- Library-function differences (`exp`, `log`, `sin` of another C library, e.g. on Windows builds) could change single
  table entries or output ulps; not observed on the test machine.
- The editor-only state toggle of layout ≥ 2 (§2.1) is not identified.
