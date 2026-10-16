# Behavioural analysis of FL Studio 2026: round 63 (REA) — Fruity Fast LP, Fruity Free Filter and Fruity Filter: exact engines

> **Nature of this document.** Goal of the effect rounds (40: Maximus): replace LYNX's approximations of three small FL
> filters — Fruity Fast LP, Fruity Free Filter and Fruity Filter were all imported as the generic `EffectKind::filter`
> — with exact engines. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was
> copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`) serve only to trace the evidence. No FLEX pack or encrypted content was touched; no Image-Line audio
> or preset data is stored in the repository (the unit-test goldens are the DLLs' output for our own states and input).
>
> **Scope.** The round was planned to also cover **Low Lifter** and **Emphasis**. They were **not ported**: Low
> Lifter's decompilation had only been started when the round was interrupted (power loss), Emphasis was only surveyed.
> Both keep their existing approximations (Low Lifter → the parametric EQ, Emphasis → the limiter, REA round 15/17
> mappings) and remain future work (§7). Nothing in this document describes them.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**); **[model]** marks the Python
> models that implement only DECOMP/EXEC facts. Inferences are marked as such; anything without evidence is
> **"not confirmed"**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst` files or read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | the round's Python model (our own code), itself checked against EXEC |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r63/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Fast LP/Fruity Fast LP_x64.dll` | `e9bb7501868d1866ebb362ee5eec992624f5215d27ec6ef390f4fe80d5bc0bce` | Delphi plugin with its own DSP |
  | `Plugins/Fruity/Effects/Fruity Free Filter/Fruity Free Filter_x64.dll` | `99594019c084262c923b2aa77ddb6e52340aae767d9c4c62998fd85c7793cb69` | Delphi plugin with its own DSP |
  | `Plugins/Fruity/Effects/Fruity Filter/Fruity Filter_x64.dll` | `492af2fbfa65a406eafcb0332160deb0a6c2a8715d39e01da51ca17f7f409d4a` | Delphi plugin with its own DSP |

  (Copies in `bin/` hash identically to the installation's files.)
- **Tools:** Ghidra 12.1.4 headless (project `~/ghproj/r63`; `gh/imp.sh`, `gh/g.sh`, scripts `DecRange.java`,
  `DecompAll.java`; listings `out/flp_dec.c`, `out/ffr_dec.c`, `out/fil_dec.c`, `out/ffr_pp.s`, `out/ffr_all.s`).
- **EXEC harness:** `run/fx.c` → `run/fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), round 51's effect harness: the
  DLL loaded unmodified from the installation, `SetErrorMode`, vectored fault handler, `timeout`, `TerminateProcess`;
  state restored through the plugin's own SaveRestoreState, Dispatcher sample rate / block size, ProcessParam events
  during the render, Eff_Render; `pp=` dumps the Free Filter's designed coefficients.
- **Drivers (our own code):** `py/survey63.py` (every mixer state of the five planned plugins → `data/states63.pkl`),
  `py/presets.py` (the installation's `.fst` presets → `data/presets63.pkl`), `py/vt.py` (runner glue on round 51's
  `ws.py`), models `py/flpmodel.py`, `py/ffrmodel.py`, `py/filmodel.py`, checks `py/t_flp.py`, `py/t_flpauto.py`,
  `py/t_ffr.py`, `py/t_fil.py` (models vs DLLs), `py/t_cpp.py` with `cpp/fxcmp.cpp` (LYNX's C++ effects vs DLLs),
  `py/gold.py` (goldens of `tests/tests.cpp`). The verification numbers of §5 were re-run for this document
  (`out/verify_doc.log`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" / "bit-identical" = every output
sample equal.

**Installation usage [DATA]:** all in mixer slots of *Demo projects*: Fruity Fast LP 17 slots (2 files, 7 distinct
states, all version 1, 12 bytes); Fruity Filter 32 slots (4 files, 19 distinct states: 20 slots of version 1 / 28
bytes, 12 of version 2 / 29 bytes); Fruity Free Filter 4 slots (2 files, 2 distinct states, version 2 / 21 bytes).
Presets: 9 Free Filter (9 distinct) and 8 Fruity Filter (4 distinct) `.fst` states; none for Fast LP.

---

## 1. Fruity Fast LP

### 1.1 State and controls **[DECOMP `FUN_006c87d0`, `FUN_006c89d0` (ProcessParam) + EXEC]**

- State: i32 version (FL writes 1; **2 and above are not read**), then cutoff and resonance as i32 raw values, both
  0…1024 (defaults 1024 and 0). The raw values are taken as they are (no clamping at load); the control value is
  `x = raw/1024` clamped to 0…1 in double and stored as float.
- Any parameter change recomputes **both** coefficients.

### 1.2 Coefficients **[DECOMP `FUN_006c86b0` + EXEC]** — float steps, the exponential in double

- Cutoff: `x' = (x + 1/63)·(63/64)`; coefficient `c = (601^x' − 1)/600` (601^x' computed as `exp(x'·ln 601)` with
  `ln 601` and `1/600` held as floats), forced to exactly 1 when `x' ≥ 1` and to 0 when `x'` is not positive.
- Resonance: `y = 0.625·x` (limited to 0.625); feedback `q = 1 − (601^(1 − y) − 1)/600`, and `q = 0` for `y = 0`.
- Neither depends on the sample rate: the same raw values give the same coefficients at 22.05…96 kHz **[EXEC]**.

### 1.3 Render **[DECOMP `FUN_006c8c90` (cutoff ramp), `FUN_006c8d60` (render) + EXEC]**

- **Per-call cutoff ramp.** The plugin keeps a "current" coefficient and glides it to the target at each render call:
  for calls of 128 frames or more the step is `(target − current)/n` and the current value lands on the target at the
  end of the call; for shorter calls the step is `(target − current)/128` and the current value advances by `n` steps
  (so the glide spans several calls), clamped to 0 (with the step recomputed from the start value) if it would fall
  below 2⁻²⁴. A fresh instance starts with the default's coefficient (1) and so glides to the loaded state's cutoff
  over its first call.
- **Kernel** (x87, 80-bit, round to nearest; per frame, left then right): a two-state resonant low-pass — the velocity
  state gains `(input − lp)·c`, `lp` gains the velocity, the output is `lp` rounded to float, then the velocity is
  multiplied by `q`; `c` advances by the ramp step once per frame.
- **End of call:** each state with magnitude ≤ 2⁻²⁴ (or NaN) becomes 0; states are kept as doubles between calls.
- **Fully open shortcut:** with `c` exactly 1, `q` exactly 0 and no ramp, the call copies the input to the output and
  the filter state takes the last input sample.
- No sample-rate notification and no flush affect the filter (LYNX's `reset()` therefore does nothing — taken from the
  round's code notes; not re-checked in the decompilation for this write-up).

---

## 2. Fruity Free Filter

### 2.1 State and controls **[DECOMP `FUN_006cc620` (SaveRestoreState), `FUN_006cd1e0` (ProcessParam), `FUN_006cce50` + EXEC]**

- State: i32 version (FL writes 2; **3 and above are not read**), four i32 raw controls — type 100…700 (default 100),
  frequency 0…1024 (512), Q 0…1024 (169), gain 0…1024 (676) — then, for version 2, one byte whose bit 0 is the
  CENTER button (DC removal). A type below 100 is an older layout (type·10): it is converted to the current scale as
  `100 + round((⌊type/10⌋ − 0.5)/7·600)` with round-half-even **[DECOMP + EXEC: two synthetic old-layout states]**.
- Values `x = (raw − min)/range` (double, clamped, then float):
  - type: `round(x·7 − 0.5) + 1`, limited to 1…7 = low pass, band pass, high pass, notch, low shelf, peaking, high
    shelf; **a change of type clears the filter state**;
  - frequency `f = 10 + 159.9·(101^x − 1)` Hz (10 Hz…16 kHz, intermediate rounded to float), limited to 22 kHz;
  - Q: `x` itself; gain `36x − 18` dB.
- CENTER is a button stored in the state, not an automatable parameter.

### 2.2 Design **[DECOMP `FUN_006ca5c0` + EXEC: the DLL's coefficients dumped with `pp=` and compared]** — double, stored as float

- `ω = f·2π/44100`, **always at 44.1 kHz, whatever the sample rate** (verified bit-identical at 22.05, 48 and 96 kHz
  with the fixed design rate), with a floor of 0.0004.
- Low pass, band pass, high pass, notch: RBJ cookbook sections with `α = sin ω / (2·Qₑ)`, `Qₑ = 0.1 + 18.9·Q`
  (0.1…19), normalised by `1/(1 + α)`. The band pass is the constant-0-dB-peak form (`b₀ = α`, `b₁ = 0`, `b₂ = −α`);
  low/high pass take `b₀ = b₂ = b₁/2` from `b₁` already rounded to float; the notch shares `b₁` with `a₁`.
- Peaking: `A = 10^(gain/40)` (with exactly 1 for 0 dB, the Delphi power), `α = sin ω / (2·Qₚ)`,
  `Qₚ = 0.1 + 0.0999·(1001^Q − 1)` (0.1…100), the RBJ peaking section normalised by `1/(1 + α/A)`.
- Low / high shelf: `A` as above; the RBJ shelf with slope `S = 0.1 + 1.9·Q` (0.1…2), i.e. the cookbook's `2√A·α`
  term computed as `sin ω · √((A² + 1)/S − (A − 1)²)`.
- Every coefficient is computed in double in the plugin's own grouping and rounded to float once (the order of the
  operations is reproduced in `core/FruityFilters.cpp`).

### 2.3 Render **[DECOMP `FUN_006cb000` / `FUN_006cb0f0` (filter), `FUN_006cae50` / `FUN_006caf70` (CENTER) + EXEC]**

- Direct form I per channel, the two channels interleaved frame by frame; the accumulation runs in x87 extended
  precision in the order `b₁x₁ + b₂x₂ − a₁y₁ − a₂y₂ + b₀x`, the result rounded to float is both the output and the
  new `y₁`; the `x`/`y` history is kept as floats.
- End of call: each history value with magnitude below 2⁻²⁴ becomes 0 (this is the only way the call size affects the
  output: −75 dB-level differences when the calls are chunked differently, §5).
- CENTER (when the button is on): after the filter, a DC remover per channel — the same two-state low-pass kernel as
  the Fast LP (x87) with coefficient `1.0070235/fs` and feedback 0.96, output `input − lp`, end-of-call ≤ 2⁻²⁴ → 0.
  The sample-rate notification sets its coefficient and clears it; it is the only rate-dependent part.

---

## 3. Fruity Filter

### 3.1 State and controls **[DECOMP `FUN_006cb920` (SaveRestoreState), `FUN_006cbf10` (ProcessParam) + EXEC]**

- State: i32 version (1 or 2; **3 and above are not read**), six i32 raw controls — cutoff, resonance, low-pass,
  band-pass, high-pass level (each 0…1024; defaults 1024, 205, 1024, 0, 512) and ×2 (0/1) — then, for version 2, a
  byte whose bit 0 is CENTER. Version-1 states (28 bytes) have no CENTER.
- Values (`x = raw/1024`, double → float):
  - cutoff: SVF coefficient `f = (26^x − 1)·0.04` (0…1; fully open `f = 1`, i.e. `fs/6`);
  - resonance: damping `d = 1 − 0.8x`;
  - the three output gains `(11^x − 1)/10` (0…1);
  - ×2: on when the raw value is 1; **switching it clears the filter state**.
- None of these depend on the sample rate **[EXEC: 22.05…96 kHz]**. CENTER is a button, not an automatable
  parameter.

### 3.2 Smoothing **[DECOMP `FUN_006cc2e0` + EXEC]**

The five values (f, d, three gains) each have a per-call smoother with the Fast LP's rule (§1.3: whole-call ramp from
128 frames, `1/128` steps below, a floor just above 2⁻²⁴). A fresh plugin starts with them settled at the defaults, so
a loaded state glides from the defaults over the first call. Inside the call, the smoothed values advance by one step
**before** each frame is computed.

### 3.3 Render **[DECOMP `FUN_006cc3b0` + EXEC]** — float, FTZ/DAZ

Per frame and channel, a Chamberlin state-variable filter: low += f·band; high = (input − low) − d·band;
band = f·high + band; output `(low·g_LP + band·g_BP) + high·g_HP`. With ×2 a second section of the same form, with
damping 1 and the same `f` and gains, filters that output. End of call: states with magnitude below 2⁻²⁴ become 0 (the
second section's only while ×2 is on). CENTER: the same DC remover as the Free Filter (§2.3), after the filter.

---

## 4. What LYNX does

- **`EffectKind::fruityFastLP`, `fruityFreeFilter`, `fruityFilter`** (appended to the enum in `core/Song.h`; engines in
  `core/FruityFilters.{h,cpp}`): mixer effects whose parameters are FL's controls over their ranges (Fast LP 2, Free
  Filter 4 + CENTER, Fruity Filter 6 + CENTER), editable in the LYNX mixer (added to the "add effect" list), saved in
  `.lynx` projects (`ProjectFile.cpp` ids), with parameter texts (Hz, %, dB, type names, On/Off).
- Import: `FlEffects.cpp` `fruityFilterExact()` parses the saved state (record 0xD5) and builds the exact slot;
  FL automation of the controls maps one to one (`effectParamFromFl`; CENTER excluded, as in FL). States FL itself
  does not read (higher versions) or too short ones keep the old generic filter, and the import report lists them as
  "state not readable: approximated" (`FlpImport.cpp`).
- Engines: the arithmetic of §1–§3 operation by operation — x87 kernels in `long double` with 64-bit precision and
  round to nearest, SSE parts under FTZ/DAZ, no FMA contraction. Each LYNX instance starts as a fresh FL plugin
  (defaults, then the state's values, with the resulting first-call glide). Host blocks longer than 512 frames are
  processed as 512-frame engine calls (the ramps and end-of-call guards are per call).
- Low Lifter and Emphasis are untouched by this round (§7).

---

## 5. Verification **[EXEC]**

All runs below were repeated for this document (`out/verify_doc.log`). LYNX C++ = `cpp/fxcmp` built from the
committed `src/core/FruityFilters.cpp` (Linux, g++), fed through the same `Effect` interface the mixer uses. "Auto" =
a list of 8–10 ProcessParam events during the render (cutoff/resonance sweeps, type and ×2 toggles, gain/level jumps).

| Check | Cases | Result |
|---|---|---|
| Fast LP model vs DLL **[model]** | 7 demo + 7 synthetic states × 44.1 kHz/512 and 48 kHz/333; 9-event automation | all bit-identical |
| Free Filter model vs DLL **[model]** | 42 states (the 2 demo states, 33 random over the 7 types, 5 extremes, 2 old-layout) | all 42 bit-identical |
| Fruity Filter model vs DLL **[model]** | 22 states (19 demo + 3 synthetic) × 44.1/512 and 48/333 | all bit-identical |
| Fast LP LYNX C++ vs DLL | 14 states × {22.05/64, 44.1/512, 48/333, 96/512} × with/without automation; saw signal at 44.1/512 | all 126 bit-identical |
| Free Filter LYNX C++ vs DLL | 49 states (2 demo + 38 synthetic + 9 presets) × the same 9 sets | all 441 bit-identical |
| Fruity Filter LYNX C++ vs DLL | 27 states (19 demo + 4 preset + 4 synthetic) × the same 9 sets | all 243 bit-identical |
| Host calls longer than 512 frames (DLL called with 1000 or 4096 frames, LYNX splits into 512) | the three suites | differ: Fast LP / Fruity Filter by up to +1.6 dB (the glide spans a different length), Free Filter −75 dB (end-of-call guard) — a property of the call size, see §7 |
| Unit test `fruity_fast_lp_free_filter_filter` (`tests/tests.cpp`) | goldens from the DLLs (`py/gold.py`) for 2 + 3 + 2 states, the old Free Filter layout, texts, fallbacks; import battery: every Fast LP / Free Filter / Fruity Filter demo slot (≥ 53) on the exact engines | committed; **not re-run** for this document (docs-only step; no build) |

Verification level: **bit-identical to the three DLLs** for every installation state (demo slots and presets) and the
synthetic states, at 22.05/44.1/48/96 kHz, with control changes during playback, whenever FL's render calls are at
most 512 frames.

---

## 6. Files changed in LYNX (commits `8d0d509`, `16c5a32`)

`src/core/FruityFilters.{h,cpp}` (new), `src/core/Song.h` (three enum values), `src/core/Effects.cpp` (info, texts,
factory), `src/core/FlEffects.cpp` (exact import, FL plugin names, automation map), `src/core/FlpImport.cpp` (report),
`src/engine/ProjectFile.cpp` (ids), `src/ui/MixerComponent.cpp` (addable effects), `CMakeLists.txt`,
`scripts/build-local.sh`, `scripts/build-windows-zig.sh`, `tests/tests.cpp`.

---

## 7. Open items

- **Low Lifter and Emphasis were not ported in this round.** They keep their approximations (Low Lifter as the
  parametric EQ without its harmonics generator, Emphasis as the limiter). The scratch directory holds a started Low
  Lifter decompilation (`out/ll_*.c`) and both plugins' states (`data/states63.pkl`: Low Lifter 4 slots / 4 states,
  version 6, plus 12 presets; Emphasis 5 slots / 5 states, versions 7/8/10, plus 26 presets), but no engine facts were
  established. Future work.
- FL's own render-call size for mixer effects is **not confirmed**; LYNX assumes calls of at most 512 frames (as in
  rounds 40, 54 and 58). With longer calls the Fast LP and Fruity Filter glides and all three end-of-call guards would
  land differently (§5).
- The order in which FL sends the sample rate and the state at project load is not observed; it matters only for
  CENTER's DC remover (cleared by the rate notification), whose start state is zero either way.
- The flush/reset behaviour (LYNX's `reset()` leaves the filters as they are) comes from the round's code notes and
  was not re-checked for this write-up.
- The unit test and the Linux/Windows LYNX builds were not run as part of this document; the C++ comparison above used
  a Linux g++ build of the engine file only. The Windows (zig/MinGW) build of the engines is not verified against the
  DLLs.
