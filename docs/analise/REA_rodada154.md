# REA round 154 (fldsp154) — FL native plugins' sound, next tier: Emphasizer, Frequency Shifter and Low Lifter (exact), Emphasis survey

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
| Frequency Shifter | 0 | 4 (+ 9 presets) | Approximate | ported, §4 |
| Low Lifter | 0 | 3 (+ 7 presets) | Approximate | ported, §5 |

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

## 4. Frequency Shifter **[DECOMP + EXEC]**

`Plugins/Fruity/Effects/Frequency Shifter/Frequency Shifter_x64.dll` (SHA-256 `5aab5d96…59ca`, Delphi front end) and
its own `Engine_x64.dll` next to it (SHA-256 `a7774536…d8fa`, C++, one export `dispatch`: 0 create, 1 free, 2 sample
rate, 3 set control, 4 process one sample, 5 control name, 6 value text, 7/8 enter/leave a block with FTZ/DAZ).
Round 15 §3 had the state and the control laws; this round rebuilt the engine (`FUN_180002780` and its helpers).

### 4.1 Host contract

The front end calls `dispatch(4)` once per sample with {L, R, side-chain L, R, side-chain present, song position,
next position, tempo}; positions are −1 while the transport is stopped. Controls arrive as (raw − min)/(max − min)
(`dispatch(3)`): Mix, Type and Range act at once, the others glide per sample (`k = 1 − 0.1^(200/fs)`, snapping within
10⁻⁵); the Frequency control glides separately (`k = 1 − 0.1^(10/fs)`, snap 10⁻⁴). A new engine starts at the
defaults (the frequency glide from 0, i.e. −20 kHz) and glides to the state's values over the first samples.

### 4.2 Signal path (float32, coefficient design in double)

1. Feedback: `tanhf(fb·last wet)` added to the input (fb = `2·1.005x − 1.005`); the engine's own `tanhf` (rational fit
   below 1, `1 − 2/(e^{2x}+1)` with a 32-entry exp table up to 10, ±1 beyond) is reproduced bit for bit.
2. 20 Hz high-pass then 20 kHz low-pass (RBJ, Q 0.5); the low-pass is designed at a construction-time 44.1 kHz even
   when the host runs at another rate (FL's behaviour, kept).
3. Ring modulator (Type 2): `x·cos(2π·phase)` (double cos), the right channel at phase + L/R offset/4, or the
   side-chain signal when one is connected (not routed in LYNX).
4. Shifter: HQ (Type 0) first splits the signal around the shift frequency with 16th-order crossovers (4 Butterworth
   Q's 0.5098 / 0.6013 / 0.9000 / 2.5629, each section applied twice, 4 lanes L, R, L, R), re-designed every 32
   samples, low band at `min(20000, max(20, 22050 − |f|))`, high band at `min(20000, max(20, |f|))`; the two halves go
   through matching crossovers and are cross-faded in 2 ms (`500/fs` per sample) when the shift changes sign; the dry
   path runs through the same crossovers (as all-passes) so dry and wet stay aligned.
   Then a Hilbert pair: two chains of four `y = c·(x + y[n−2]) − x[n−2]` sections (coefficients 0.4794009,
   0.8762185, 0.97659755, 0.9974992 | 0.1617585, 0.7330289, 0.94534963, 0.99059916), the first chain one sample late.
   Quadrature oscillator: the C run-time's vector `sinf` on (2πφ, 2πφ + π/2, 2πφ', 2πφ' + π/2) (φ' = φ + L/R offset);
   `wetL = (b₂·cos + a₂·sin)·(1 − Lshape) + (b₀·cos − a₀·sin)·Lshape`, likewise R with lanes 1/3; phase advance
   `frac(inc + 1000 + φ)`, `inc = f/fs`.
5. Mix: wet `min(1, 2x)`, dry `min(1, 2 − 2x)`; Mono↔Stereo `s = (1 − x)/2`: `L = s·R + (1 − s)·L`,
   `R = (1 − s)·R + s·L`.

Frequency: `0.1·2^(K·|2x − 1|)` Hz with K = 17.60964 (20 kHz range) or 10.965784 (200 Hz), the library's `powf`
(the IldspMath replica matches it on 40000 random arguments; the vector `sinf` on 160000 lanes). Tempo range: period
`(int)(48·2^(7 − 14·|x − ½|)) / 192`, phase restarted from the position when the transport starts or the range
changes.

### 4.3 Verification **[EXEC]**

`tmp/fldsp154/cpp/fscmp.cpp` (LYNX engine) against the plug-in in our harness (`early` order, transport stopped,
120 BPM, `py/fsbatch.py 1 16`):

| Test | Result |
|---|---|
| the 14 installation states (5 demo slots, 9 presets) incl. HQ / plain / ring modulator, feedback, shapes, phase offsets | 14/14 bit-identical at 44.1 kHz; the two Tempo-range presets at 48 kHz −85 dB |
| 16 random states × {44.1, 48, 96 kHz}, 0–3 control changes at random samples | 15/16 bit-identical; one Tempo-range state at 96 kHz −46 dB |
| design coefficients with the C library's `sin`/`cos` instead of the engine's vector double sincos | 4 of 2 000 000 float coefficients differ (1 ulp) |
| unit test `fl_frequency_shifter_matches_fl_engine` (4 states, 24000 frames) | FNV-1a of the DLL's output |

LYNX: `src/core/FreqShifterFl.{h,cpp}`, `EffectKind::flFrequencyShifter` (project id `flFrequencyShifter`), the FL
import uses it for every readable state (the old approximation `freqShift` stays for unreadable ones), addable from the
Mixer. While the song plays LYNX passes the song position in beats.

## 5. Low Lifter **[DECOMP + EXEC]**

`Plugins/Fruity/Effects/Low Lifter/Low Lifter_x64.dll` (SHA-256 `a492a7aa…fa96`, Delphi front end) creates
`LowFreqEnhancerImpl` in `Shared/ildsp_x64.dll`, a wrapper around the Faust program `lowFreqEnhancementChebyshev`
(dsp vtable `0x1803de0a0`: instanceConstants `FUN_180177050`, instanceResetUserInterface `FUN_1801772d0`,
instanceClear `FUN_180177320`, buildUserInterface `FUN_180177a80`, compute `FUN_180177f50` — 15.4 KB of code). Round 15
§5 had the state; round 17 §13 the Chebyshev core; round 63 started the engine. This round rebuilt all of it.

### 5.1 Controls

State: `i32` version 6 · 15 × `i32` controls · UI bytes. The front end (`FUN_006c71d0`) sends each control to the
Faust zone of the same index: switches (On, Look-ahead, Invert, Multiband, High / Low band) as their raw integer,
Dry/Wet and Diffuser mix as x, and `a + (b − a)·Exp(Ln(x)/c)` (Delphi run-time) for Low gain (−120, 12, 7.275), Slope
(−60, 0, 3.105), Pre gain (0, 18, 1), Diffuser depth (−0.99, 0.99, 1), High gain (−120, 0, 5), Crossover and Low cut
(20, 120, 0.498918). Host gate (`FUN_006c7a80`): a block whose samples are all within ±10⁻⁶ counts down a one-second
tail (`= sample rate`); once it is spent the engine is skipped and the input passes until a louder block arrives.

### 5.2 Engine (float32 per sample)

Per sample, one-pole TPT smoothers (20 Hz: `G = g/(1+g)`, `g = tan(π·20/fs)`) on every control (slope at 5 Hz) and a
1 Hz fade-in from 0 to 1. Per channel:
- crossover at f (≥ 20 Hz): two cascaded Butterworth TPT state-variable filters (24 dB/oct) give the low band B and the
  high band `x − 2√2·bp − lp`; in multiband mode a second split at f/2 and f/4 (filters at g = tan(π·f/2/fs),
  tan(π·f/4/fs)) and back at f yields four bands; the high band goes to a 2048-sample delay line;
- each band × pre gain → look-ahead limiter: delay 0 or 10 ms (`(int)(0.01·fs)`), |x| through four peak holds of
  `rint((la + 0.05)·fs/4)` samples, bounded below at 1, then four attack (`exp(−14.44/(fs·la))`, 0 without
  look-ahead) / release (`exp(−144.45/fs)`) stages → E; `u = x_delayed/E`, `T₂ = 2u² − 1`,
  `y = (((2T₂ − 1)·b + 1)·x_d/E + a·T₂)/(1 + 2a)` with `a = 10^(slope/20)`, `b = 10^(slope/10)` (= u + aT₂ + bT₃);
  a 20 Hz TPT high-pass;
- the bands are summed (multiband: the three split bands; otherwise the single one), times the fade-in, through four
  rotation all-passes (`buf = s·x − c·z`, `y = c·x + s·z`, c = depth, s = √(1 − c²); lengths `fs·{0.5, 0.2071,
  0.1589, 0.1340}/f − 1` samples) mixed by the diffuser mix, times low gain · low switch · polarity, plus the delayed
  high band × high gain · high switch, then the low cut (TPT high-pass);
- the dry signal through all-passes matching the crossovers and the same delay; output `dry·(1 − wet) + wet·…`.
The library's `tanf` (double evaluation, FMA on FMA machines) and `powf` / `expf` are reproduced; the two channels
were compiled with different groupings of a few products, which the port keeps (Ghidra prints chained products
without their grouping — the disassembly decides).

### 5.3 Verification **[EXEC]**

`cpp/llcmp.cpp` (LYNX effect) against the plug-in in our harness (`py/llbatch.py 1 12`): the 11 installation states
(3 demo slots, 7 presets + Default) and 12 random states at 44.1 / 48 / 96 kHz — **23/23 bit-identical**, both
channels; a 1.6 s gap of silence (gate) at 44.1 and 48 kHz bit-identical. Unit test `fl_low_lifter_matches_fl_engine`.

## Where I stopped

(updated at the end of the round)
