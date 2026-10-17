# Behavioural analysis of FL Studio 2026: round 105 (REA) — Fruity Phaser

> **Nature of this document.** First decoding of FL's Fruity Phaser, which LYNX imported until now as its generic
> chorus. *Clean-room* reverse engineering, behavioural only: no Image-Line code or pseudocode was copied; what is here
> are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`) and offsets only
> trace the evidence. No Image-Line preset or audio data is in the repository.
>
> **Rule: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data (**[DATA]**) or from
> running the installation's own DLL, unmodified (**[EXEC]**); what could not be established is "not confirmed".

- **Date:** 2026-10-17. Scratch directory: `~/.claude/jobs/f8789b49/tmp/r105/phaser/` (paths below are relative to it).
- **Target:** `Plugins/Fruity/Effects/Fruity Phaser/Fruity Phaser_x64.dll`, SHA-256
  `0f053b585b20c4b17b532f92ae76735548dcd235b0dd9fc2b80ee4e901411351` (Delphi, image base 0x400000).
- **Tools:** REA MCP (`open_binary`, `batch_decompile` on the plugin's VMT entries) and headless Ghidra 12.1.4 for the
  whole plugin range (`gh/run.sh` → `dec/range.c`, the shared REA server was switched to another agent's target
  mid-session), `objdump -M intel` (`dec/all.asm`) where the float order matters.
- **EXEC harness:** round 55's effect harness (`run/fx.c`, built with zig cc 0.14.1 for x86_64-windows-gnu): the DLL
  is loaded unmodified, created with `CreatePlugInstance`, its state restored through its own SaveRestoreState, sample
  rate and block size through its Dispatcher, rendered through Eff_Render; `vt=1` printed its VMT, `pp=` read
  fields of the instance (knob objects, LFO tables).
- **Drivers (our own code):** `py/survey.py` (mixer states in the installation's projects), `py/presets.py` (the 27
  presets), `py/phmodel.py` (model built only from the facts below), `py/t_ph.py` (model vs DLL), `cpp/cmp.cpp` +
  `py/t_cpp.py` (LYNX's C++ engine vs DLL), `py/gold.py` (goldens of `tests/tests.cpp`).

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

**Installation usage [DATA]:** 3 mixer slots in the demo projects (*Never Out*, *Purps On The Beat*, *The Ogun Dream*),
three distinct states (= presets *Stereo Enhance 01*, *Moog It 02*, *Stereo Enhance 02*); 27 presets in
`Patches/Plugin presets/Effects/Fruity Phaser`; FL's own render test `System/Render tests/Fruity Phaser.flp`.

## 1. State and controls **[DECOMP `FUN_006ba7b0`, `FUN_006bace0`, `FUN_006bb670` + EXEC]**

- VMT (EXEC, `vt=1`): 1 Dispatcher `0x6ba4c0`, 3 SaveRestoreState `0x6ba7b0`, 6 ProcessParam `0x6bace0`,
  7 Eff_Render `0x6bbba0`.
- State (0xD5): `i32 version` (FL writes 1; **only a version below 2 is read**), then 9 × `i32` raw controls (36
  bytes, copied as a block), which are then re-sent in order through ProcessParam. 40 bytes in every file found.
- Controls, all with minimum 0 (maximum, a new instance's default) **[EXEC: the knob objects' min / max / default]**:

  | # | FL caption | max | default | meaning |
  |---|---|---|---|---|
  | 0 | SWEEP FREQ. | 5000 | 2500 | LFO rate |
  | 1 | MIN DEPTH | 1000 | 100 | all-pass coefficient at the LFO's bottom |
  | 2 | MAX DEPTH | 1000 | 800 | all-pass coefficient at the LFO's top |
  | 3 | FREQ. RANGE | 1024 | 0 | LFO rate range: "small" (≤ half) or "large" |
  | 4 | STEREO | 1024 | 512 | right LFO phase offset |
  | 5 | NR. STAGES | 22 | 7 | number of all-pass stages − 1 |
  | 6 | FEEDBACK | 1000 | 400 | feedback amount |
  | 7 | DRY-WET | 1024 | 512 | dry share (FL writes `100 − round(100·v)` "% wet") |
  | 8 | OUT GAIN | 5000 | 4000 | output gain |

- Each change stores the raw value and `v = float(clamp((raw − min)·(1/range), 0, 1))` (double, range factor from the
  knob). Then:
  - control 0: `rate = float(((exp(double(float(v₀·4))) − 1)/(exp(4) − 1))·R)`, `R = 10` when `double(v₃) > 0.5`, else 2
    (Hz; on a floating-point exception FL takes 1.0 — not reachable with clamped inputs);
  - control 3 re-sends control 0 (so the rate follows the range switch);
  - control 5: `stages = clamp(raw + 1, 3, 23)`.
- The value texts FL shows **[DECOMP `FUN_006bb0a0`]**: sweep `%.5f Hz` of the *normalised* value (not the rate),
  depths `%.5f`, range `small` / `large`, stereo `%.5f phase`, stages `raw + 1`, feedback `%.5f amount`, dry-wet
  `100 − Round(v·100)` `% wet`, out gain `v·2` in dB. LYNX shows the real rate in Hz and the stereo offset in degrees.

## 2. LFO **[DECOMP `FUN_006b9030`, `FUN_006b9140`, `FUN_006b92a0`, `FUN_006b9300`, `FUN_006b9320`, `FUN_006b9350` + EXEC]**

- Two LFO objects (left, right). Tables of 4096 floats: `T[i] = float((sin(i/4096·6.283185307) + 1)·0.5)` (a raised
  sine from 0.5 — EXEC: `T[0] = 0.5`, `T[1024] = 1`), slopes `S[i] = T[i+1] − T[i]` (the last `T[0] − T[4095]`),
  fractions `F[j] = float(j/4096)`.
- Value of a 32-bit phase `p`: `T[k] + F[p & 4095]·S[k]` with `k = (p >> 12) & 4095` (float); the phase then advances by
  the increment (32-bit wrap). A cycle is 2²⁴.
- Sample rate (dispatcher 4, and at creation with the host's rate): `scale = float((4096/fs)·4096)`
  (EXEC: 380.43573 at 44.1 kHz).
- At the start of every render call: `inc = Round(double(float(rate·scale)))` for both LFOs (Round = to nearest, ties
  to even: `cvtsd2si`), and the right phase is reset to `left phase + Round(double(v₄)·2²³)` (so STEREO = 1 is half a
  cycle, 180°).
- Reset (dispatcher 2, and at creation): every all-pass state and the two feedback values cleared, left phase 0, right
  phase from the stereo offset.

## 3. Render **[DECOMP `FUN_006bbba0`, `FUN_006bba00`, objdump — SSE, float]**

Per call: `fb = float(double(v₆)·0.99)`, `g = float(double(v₈)·2·sqrt(double(float(1 − v₆·v₆))))`. Per frame, for each
side (left then right, each LFO read then advanced): coefficient `c = float(−v₁) − lfo·(v₂ − v₁)`; the chain input is
`x + last·fb` (`last` = the previous frame's last-stage output of that side); a cascade of `stages` first-order
all-passes `yₖ = c·(yₖ₋₁ − yₖ[n−1]) + yₖ₋₁[n−1]` (stage 0 uses the chain input and its previous value); `last` = the
last stage's output; out = `(v₇·(x − last) + last)·g` (`x` = the dry input, without feedback). The arrays hold 23
stages.

## 4. What LYNX does

`src/core/FruityPhaser.{h,cpp}` (`lynx::phaserfx`, `EffectKind::fruityPhaser`, appended to the enum): the engine of
§1–3 with FL's single-precision operation order; the slot parameters are FL's nine controls as `raw / max`, so the
editor and automation change them. `effectFromFl` builds it from FL's state (version < 2, ≥ 40 bytes), otherwise the
old chorus approximation stays (and the import report says so); FL automation of parameters 0…8 maps to the same
controls; projects save it as `fruityPhaser`; it is in the mixer's effect list. Editor: FL's generic "Fruity" panel,
three rows of three cells (SWEEP FREQ., MIN DEPTH, MAX DEPTH / FREQ. RANGE, STEREO, NR. STAGES / FEEDBACK, DRY-WET,
OUT GAIN), measured on the FL 2026 capture of the Default preset (`tmp/r93/flref/w_phaser.png`).

## 5. Verification **[EXEC]**

- Model vs DLL (`py/t_ph.py`): all 27 presets × two signals (music-like, decaying saw), 44.1 kHz, blocks of 512:
  **−∞ (bit-identical)** on all 54 runs.
- LYNX's C++ (g++ -O2) vs DLL (`py/t_cpp.py`): 27 presets × (44.1 kHz / 512, 48 kHz / 333, 96 kHz / 1024): identical
  except denormal-range residues (worst −742 dB): FL runs with flush-to-zero / denormals-are-zero set (MXCSR 0x9FE0),
  the test tool did not.
- `tests/tests.cpp` `fruity_phaser_engine`: four DLL goldens (two sample rates, small / large range, 3 to 23 stages,
  feedback up to 0.95) with a 2·10⁻⁵ relative tolerance (compilers / libm), the texts, the import and its round trip;
  the real-projects test checks that every Fruity Phaser mixer slot of the installation imports on the exact engine.

- **FL's own command-line render** (`FL64.exe /R /Ewav`, under the FL lock) of `System/Render tests/Fruity Phaser.flp`
  (an FL 11 project: a 3x Osc into the phaser, saved inside the old Fruity Wrapper as a VST2 with nine 0…1 floats —
  the defaults; LYNX now rebuilds the native state from them, the stage count as `round(v·23) − 1`, [DATA, defaults
  only]) vs LYNX's `renderwav` of the same project: level within 0.45 dB RMS / 0.18 dB peak, envelope correlation
  1.00, spectrum within 1.6 dB. Sample-wise the residual is −10.9 dB, but it is already in the source: running the
  model on LYNX's dry render (the same project with the phaser removed) reproduces LYNX's wet render to −132 dB, and
  sweeping the LFO's start phase over 64 values, phase 0 at the song start fits FL's render best (`py/fit.py`). So the
  phaser itself starts as FL's does; the difference is the 3x Osc source of this project.

## 6. Open items

- FL's block size inside a real FL render is not modelled: the right LFO is re-aligned to the left one at the start of
  every render call, which is exact for any block size (the offset is constant), so this only matters through the
  LFO increment's rounding being per call — identical in both.
- The old wrapped-VST2 phaser: only the default floats were seen; the mapping of other values (especially the stage
  count) is not confirmed.

## Where I stopped

- Engine, import, automation, editor layout, tests: done on `feature/phaser105` (pushed to `wip/phaser105`).
- Next: build-local with the FL Data dir, Windows build, selftest, land.
