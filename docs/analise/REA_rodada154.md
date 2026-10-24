# REA round 154 (fldsp154) — FL native plugins' sound, next tier: Emphasizer (exact), Emphasis survey

Date: 2026-10-24. Clean-room: FL Studio's own binaries were studied with Ghidra 12.1.4 (headless) and run unmodified
in our own harness; this document describes their behaviour in our own words. No Image-Line code, preset or sample is
in the repository. FLEX packs were not touched.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in Ghidra's decompilation / disassembly of the FL binary |
| **[EXEC]** | measured by running the installation's DLL unmodified in our harness (`tmp/fldsp154/run/fx.c`) |
| **[DATA]** | counted in project / preset files |
| **[FL]** | FL Studio's own render (`FL64.exe /R /Ewav`) |
| **[HYP]** | hypothesis, not verified |

## 1. What to port — usage counts **[DATA]**

Native plugin names (event `0xC9`) in the girlfriend's 48 projects (`tmp/gf/all`, backups excluded) and in the 165
`.flp` files of FL's `Data` folder, compared with `docs/PLUGINS.md`'s status:

| Plugin | her projects | FL demos / data | LYNX before | |
|---|---:|---:|---|---|
| **Emphasizer** | **12** (always the last Master slot, default state) | 0 | **not supported** (dropped) | ported, §2 |
| Gross Beat | 30 | 18 | Exact | — |
| Newtone | 9 | — | Close | — |
| Fruity Soft Clipper / Balance / Stereo Enhancer / Reeverb 2 / Parametric EQ 2 / Limiter / Flangus | 2…38 | many | Exact / Near-exact | — |
| FLEX | 13 | 15 | Approximate (packs encrypted, never opened) | — |
| Emphasis | 0 | 5 (4 demo Masters + render test) | Approximate (LYNX limiter) | surveyed, §3 |
| Frequency Shifter | 0 | 4 | Approximate | open |
| Low Lifter | 0 | 3 | Approximate | open |

All 92 Emphasizer instances in her projects (12 projects plus their backups) carry the same 29-byte state: version 2,
controls `0, 5013, 0, 0, 0, 10000`, meter byte 1 — the plug-in's default (Emphasis 50 %) on the Master. LYNX used to
drop the slot, so every one of those songs reached the output without FL's final limiting stage.

## 2. Emphasizer **[DECOMP + EXEC]**

`Plugins/Fruity/Effects/Emphasizer/Emphasizer_x64.dll` (SHA-256 `9cbde60a…2141`, Delphi front end) creates its engine
in `Shared/ildsp_x64.dll` (SHA-256 `b96354e1…1760`): RTTI class `LimiterLiteImpl` (vtable `0x1803dfd80`) wrapping a
`DSP` object (vtable `0x1803dfd30`; init `FUN_180173610`, process `FUN_180173940`, set-parameter `FUN_1801aabb0`).

### 2.1 State and controls

`0xD5` = `i32` version (1 or 2) · 5 (v1) or 6 (v2) `i32` controls · `u8` loudness-meter switch. Version 1 has no
Output control; the control keeps its default (10000 = 0 dB). After reading the state the front end calls its own
ProcessParam for every control in index order (flags 2 then `0x11`), which sends each to the engine.

| i | Control | Range | Engine value |
|---|---|---|---|
| 0 | Input gain | 0…10000 | `0 + 24·Exp(Ln(x)/1)` dB (Delphi run-time Exp/Ln; x = raw·10⁻⁴ as float) |
| 1 | Emphasis | 0…10000 | x |
| 2 | Soft clipping | 0/1 | the raw integer (texts "Hard" / "Soft") |
| 3 | Level matching | 0/1 | raw |
| 4 | Bypass | 0/1 | raw |
| 5 | Output | 0…10000 | x = 1 → 0 dB; else `20·Log10((Exp(x·ln 11) − 1)·0.1)` (FL's volume curve), −∞ at 0 |
| (6) | loudness meter | state byte | display only |

### 2.2 Engine (all float32; FTZ/DAZ during the call)

Set-up (`FUN_180173610`, once, at the host's first sample-rate call): smoothing coefficient `c = (float)exp(−125.66/fs)`
(20 Hz one-pole), look-ahead `D = (u64)(fs·0.002f)` samples (88 at 44.1 kHz) for the four delay rings (65536 floats,
16-bit indices: write, then read `write − D`), hold length `fs·0.003125`, ceiling `0.9885531`; it zeroes the input-gain
dB, the emphasis coefficients, the switches 2–4 and the bypass cross-fade. Output gain ramp and envelope keep their
construction values (output ramp 0 → a fresh instance fades in over ~10 ms; dry→wet cross-fade from 0 likewise).

Emphasis `e` (set-parameter 1): `a = e²`; detector scale `s = 1 − a/2`; attack `T_a = 4a + 0.002` s, release
`T_r = 3(1 − a) + 0.1` s; envelope coefficients `(float)exp(−2π/(fs·T))` (double exp); program release pair
`expf(−2π/(fs·T_r))` and `expf(−2π/(fs·T_r/4))`. Gains: `expf(dB·0.115129255)`.

Per call of n frames:
1. dry rings ← input (delayed by D);
2. input gain ramp `g = (g − G)·c + G`, wet = g·in;
3. detector `d = s·max(|wetL|, |wetR|)` (s read at the call's start);
4. per sample: 8 cascaded peak holds of `hold` samples each (a stage keeps its value while it is larger than the input
   and its counter is below the length) → `h`; envelope `env = (env − h)·k + h` with the attack coefficient when
   `h > env`, else the current release coefficient; `gain = 0.9885531 / max(env, 0.9885531)`; a reference gain `r`
   follows `gain` whenever it does not rise; `q = max(1, (h/s)·0.5 / r)`; next release = the slow one when
   `gain² < r/q`, else the fast one;
5. wet rings (delay D) → `out = gain·wet`;
6. hard clip to ±1, or the soft clip (above 0.84139514: `0.84139514 + 0.15860486·t/√(t²+1)`, `t = 6.30497694·(v −
   0.84139514)`, symmetric below);
7. output gain ramp; level matching multiplies by `1 / g` (the input ramp's value at the end of the call);
8. bypass: cross-fade `w = (w − W)·c + W` (W = 0 when bypassed) between the delayed dry and the result, × 0.9999.

### 2.3 Verification **[EXEC]**

`tmp/fldsp154/cpp/emzcmp.cpp` (LYNX engine, our driver) against the DLL in `tmp/fldsp154/run/fx.c` (sample rate and
block size sent *before* the state — option `early`):

| Test | Result |
|---|---|
| her state (`0, 5013, 0, 0, 0, 10000`), 4 s test signal (sines, noise bursts, +8 dB overload), 44.1 kHz / 512 | bit-identical |
| `Default.fst` (version 1) | bit-identical |
| 20 random states × {22.05, 44.1, 48, 96 kHz} × calls of {1, 64, 333, 512} frames, 0–3 control changes at random samples (`py/batch.py 1 20`) | 20/20 bit-identical |
| unit test `emphasizer_matches_fl_engine` (import → slot → effect, 4 states, 24000 frames, host blocks of 1000) | FNV-1a of the DLL's output |

Host order: with the state loaded *before* the first sample-rate call the engine's set-up runs after the controls and
wipes them (Emphasis 0 %, attack/release times 0) — the harness's default order reproduces that. LYNX follows the
rate-first order used by every earlier port; FL's own order is checked with FL renders (§2.4).

### 2.4 FL renders **[FL]**

`py/mkemz.py` turns `Data/System/Render tests/Emphasis.flp` into seven projects with Emphasizer in place of Emphasis
(her state; Emphasis 0 / 100 %; soft clip; +12 dB; level matching; output 70 %), rendered with `FL64 /R /Ewav` into
`C:\Users\kauan\lynx-test\fldsp154\ref`. (Results in "Where I stopped".)

### 2.5 In LYNX

`src/core/EmphasizerFx.{h,cpp}` (engine, state parser, value texts, effect), `EffectKind::emphasizer` (project id
`emphasizer`), import of FL's `Emphasizer` slots (`FlEffects.cpp`, `FlpImport.cpp`), addable from the Mixer, editor
layout with FL's arrangement (Input gain / Emphasis / Output, meter strip, Soft clipping / Level matching / Bypass),
pt-BR strings.

## 3. Emphasis — survey **[DECOMP + EXEC]**

`Emphasis_x64.dll` (SHA-256 `e64ca492…5e48`, unchanged since round 15) runs a much larger engine in the same library:
wrapper vtable `0x1803dff40`, inner `DSP` vtable `0x1803ddc48` (init `FUN_1801665b0` / `FUN_180166970`, process
`FUN_180167330` — 7.7 KB of code, set-parameter `FUN_180169150` — 4.7 KB, a 28-entry switch at `0x18016a38c` whose
cases re-dispatch after recursive calls, which Ghidra does not follow). Its state is ~10 MB (oversampling ×2…×16
polyphase stages per channel, true-peak path, envelope orders 1–4, a "Steady" loudness analyser with rational
saturation polynomials, M/S and linking). Its front end maps 13 controls + the 3 option bytes (`FUN_00706860`).
The exact port is left for a later round; LYNX keeps the limiter approximation.

## Where I stopped

(updated at the end of the round)
