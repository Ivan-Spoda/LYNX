# Behavioural analysis of FL Studio 2026: round 70 (REA) — Fruity Vocoder, exact engine (and the Delphi run-time math)

> **Nature of this document.** Completion of `REA_rodada20.md` §4.1 (Fruity Vocoder: state, laws, bands), which gave
> LYNX an approximation of the plugin. This round decodes the whole render path, the precision of every step and the
> run-time functions it calls, and verifies a C++ engine against the installation's own DLL. *Clean-room* reverse
> engineering, behavioural only: no Image-Line code or pseudocode was copied; what is here are layouts, formulas and
> behaviours in our own words. Addresses (`FUN_xxxxxxxx`) only trace the evidence. No FLEX pack was opened; no
> Image-Line preset or audio data went into the repository (the tests use states and signals of our own).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's DLL unmodified (**[EXEC]**). Inferences are marked as such; what could
> not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled code or the disassembly |
| **[DATA]** | checked in `.flp` / `.fst` files, or a value read from the DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15/16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r70/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Vocoder/Fruity Vocoder_x64.dll` | `bfc3bec327dda1f943cdd654220b9c0fa3dffa183080a4a8c56361fea63961b6` (= rounds 19/20) | the effect (Delphi, `TTestPlug`, VMT `0x6cf210`) and the Delphi RTL it links |
  | `Plugins/Fruity/Effects/Vocodex/Vocodex_x64.dll` | `43d10692ea04457018138110a1a3035d99ed8b40a7f52e67a3109c22ca714bb0` (= rounds 14/20/22/35/40) | unchanged: LYNX's Vocodex engine of rounds 35/40 stands (§9) |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r70`, `gh/imp.sh`, `gh/dl.sh`; dumps `out/fv.c` = class range
  `0x6cf000…0x6d6000`, `out/lib*.c` = library and RTL functions), `objdump -M intel` (`asm/`) wherever the precision or
  the operation order matters, `py/pe.py` (our minimal PE reader for constants).
- **EXEC harness:** `run/fx.c` → `fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), the effect harness of rounds 50/61
  (installation DLL loaded unmodified, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state
  through the plugin's own SaveRestoreState, Dispatcher rate / block, ProcessParam events at given frames, Eff_Render;
  `pp=`/`arr=` field dumps, `gd=`/`ipp=`/`fp=` global and function-pointer resolution). Added this round: `fcall` kind
  `S` (a `void(double, double*, double*)` routine, for SinCos).
- **Drivers (our own code):** `py/survey.py` (every Fruity Vocoder state in the installation), `py/fx.py` (runs the
  harness), `py/cmp.py` (LYNX engine `cpp/fvcmp` vs DLL), `py/rnd.py` (random states / rates / blocks / signals /
  automation), `py/rates.py`, `py/mathcmp.py` (the DLL's own exp / ln / SinCos called in place) with `cpp/mt.cpp`,
  `py/golden.py` (the goldens of `tests/tests.cpp`); debugging aids `cpp/fvdbg.cpp`, `cpp/dumpend.cpp`, `py/hyp.py`.

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "bit-identical" = every output sample equal.

---

## 1. Installation usage and state **[DATA + DECOMP `FUN_006cf810`]**

Survey of every `.flp` and `.fst` under `Data` (`py/survey.py`): **11 instances, 7 distinct states** — the demo
"Edlan & Ella Noël – Song For You" (insert 9), the render test `System/Render tests/Fruity Vocoder.flp` (32 bands,
width 27), the mixer presets "Mono tape simulation", "Stereo tape simulation" (4 bands, formant −700, width 95),
"Outside the club", the plugin presets `Clear.fst` (32 bands with a rising level tilt 0.80…1.54) and `Default.fst`
(= the plugin database entry). All version 2, order 2, listen 2 (mix), no swap.

State layout (confirms round 20 §4.1.1): `i32` version (loads if > 1), then 13 bytes read in one go into the fields
bands / order / listen / swap, `bands × f32` levels, `12 × i32` controls. Load order **[DECOMP]**: the band block, the
levels, a reset of all filter states and envelopes (`FUN_006d07f0`), a set-up with the old controls, then the controls
one by one through ProcessParam (each one runs the set-up again). The band count is not validated on load; LYNX
accepts only 4, 8, 16, 32, 64, 128 (the UI's values). The controls are not clamped to the knob ranges either
(round 78 §1).

UI handlers **[DECOMP]**: changing the band count resets the levels to 1 and every state; changing the order resets
every state; listen and swap only reconfigure. The controls never reset anything.

## 2. Run-time math **[DECOMP + EXEC]**

The set-up calls the Delphi Win64 RTL: `Exp` `FUN_00408810`, `Ln` `FUN_00409870` (kernel `FUN_00409780`), `SinCos`
`FUN_004095e0` (argument reduction `FUN_00408e80`, kernels `FUN_00409340` sin / `FUN_00409270` cos), `Round`
`FUN_00409d00` (`cvtsd2si`, nearest-even). Called in place through the harness on 350 000 / 300 000 / 250 000 random
arguments, the C library differs from them in the last bit for **0.2 % (exp), 4.1 % (ln), 2.5 % (sin/cos)** of the
arguments. Those differences reached the output as isolated 1…7-ulp samples (−315…−405 dB) in 4 of 200 random runs (§6), so
LYNX now has its own replica, `core/DelphiMath`:

- **exp:** `k` = `x·log₂e ± ½` truncated (`x·log₂e + ½` for x ≥ 0, `x·log₂e − ½` below); `hi = x − k·ln2_hi`,
  `lo = k·ln2_lo`; `j` = `(hi + lo)·64 ± ½` truncated; `hi −= j/64`; `r = hi + lo`; a degree-4 correction
  `q = lo + r²·(½ + r·((c₃ + c₅r²) + (c₄ + c₆r²)·r))`; the table holds `e^(j/64) − 1` as a double plus its remainder;
  `T = 1 + table` is renormalised into `b + c`, and the result is `2^k · (q·c + hi·c + c + q·b + hi·b + b)` summed in that
  order. Big / small arguments: overflow above `0x40862e42fefa39ef`, zero below `0xc0874910d52d3051`, k > 1023 uses
  k = 1023.5 with the scale `0x7fe6a09e667f3bcd`, the subnormal range scales by a shifted 2⁻¹⁰²² (results flush to zero
  under the plugin's FTZ). The table (j = −64…64 checked) equals `e^(j/64) − 1` rounded to 106 bits; LYNX's copy was
  generated with 60-digit decimal arithmetic, not read from the DLL.
- **ln:** `x = 2^k·m`, `m` in [1, 1.4142…) (threshold: mantissa bits ≤ `0x6a09e667f3bcc`, else `m/2` and k + 1;
  subnormals normalised first); `f = m − 1`, `s = f/(f + 2)`, `z = s²`, `w = z²`; two interleaved polynomials in `w`
  (12 coefficients) combine as `((((P·2)·z)·s + f³/4) − f²/2) − s·f³/4 + f`; then `+ k·ln2_lo`, `+ k·ln2_hi`.
- **SinCos:** `|x| ≤ π/4`: kernels with tail 0; `π/4 < |x| ≤ 5π/4`: n = 1 (|x| ≤ 3π/4) or 2, signed like x, `a = (x −
  n·P₁) − n·P₂`, tail `(−n·P₂ − (a − (x − n·P₁))) − n·P₃` (π/2 in three parts), quadrant `n & 3` selects ±sin/±cos.
  Sin kernel `x + ((((S₀w + S₂)w + S₄ + S₅)z + (S₁w + S₃)w + S₆ + S₇)·z·x + (1 − z/2)·y)`; cos kernel `r + ((p − x·y) −
  (z/2 + (r − 1)))` with `r = 1 − z/2`, `p = ((C₁w + C₃)w + C₅ + ((C₀w + C₂)w + C₄)z)·w`. Larger arguments (never used
  here: the vocoder's ω < 2.85) fall back to the C library — not replicated.
- **sinh** (`FUN_0045d4d0`): 0 when |x| ≤ 10⁻¹²; otherwise `(exp(x) − exp(−x))/2` with the RTL exp.

Replica vs DLL **[EXEC]**: 0 differences over 470 000 exp, 420 000 ln and 370 000 SinCos arguments (ranges −746…710,
subnormal…10³⁰⁴, −3.9…3.9 incl. the π/4, 3π/4, 5π/4 boundaries ± 1 ulp), with FTZ/DAZ as in the plugin.

## 3. Set-up **[DECOMP `FUN_006d03a0`, `FUN_006cff90`, `FUN_006d00b0`, `FUN_005dbb00`, `FUN_005db530` + asm]**

Every cast below is where the code converts; `f(·)` = float, `d(·)` = double.

1. **Mix levels** (listen 2): `v = f(raw)·(1/128)`; level = 1 if v = 1, else `f((exp(d(v·f(ln 11))) − 1)·d(0.1f))`.
   Listen 0: modulator 1, carrier 0, wet 0; listen 1: carrier 1, others 0.
2. **FreqMin/Max:** `t = f(raw·2⁻¹⁶)`, `hz = f(d(f(exp(d(t)·ln 101) − 1))·159.9 + 10)`; if min > max then min = max − 1
   (float); equal values are kept.
3. **Band gain** `G = (wet·1500)/f(width)` (float); **width per cent** `β = (f(width)·f(order))/120000`.
4. **Scale** `S = f(Round(d(f(exp(d(f(raw)/128)·ln 10000) − 1))) + 1)`; **top** `= f(d(rate·20000)/44100)` (int product).
5. **Edges** (i = 0…N): `t = f(d(i)/d(N))` (double division), `u = f(exp(d(t)·ln d(S + 1)) − 1)/S`,
   `hz = min + u·(max − min)` (float), cents `c = f(ln(d(hz/440))/(ln2/1200) + 5700)`.
6. **Banks:** the synthesis bank first (edges + formant cents, float; with Invert the band index is N − i), then the
   analysis bank. Band k (between edges k and k+1): `hz = f(exp(d(((c_k + c_{k+1})·0.5) − 5700)·ln2/1200)·440)`; it is
   active when `hz ≥ 10` and `hz < top`, and an analysis band also needs the synthesis band of the same index. An
   inactive band gets envelope 0, gain 0, the "silent" flag, and both its filter states (analysis and synthesis) are
   zeroed. An active band gets `octaves = β·(c_{k+1} − c_k)` (float), a band-pass design and gain `level_k·G` (float).
7. **Band-pass** (RBJ, 0 dB peak, double): `ω = d(hz)·2π/rate`, `s, c = SinCos(ω)`, `α = s·sinh(((ln2/2)·d(octaves))·ω/s)`,
   `d = 1/(α + 1)`, `a₁ = (d·c)·(−2)`, `a₂ = d·(1 − α)`, `b₀ = d·α`, `b₁ = 0`, `b₂ = d·(−α)`.
8. **Attack / release:** `m = f(rate·0.001)`, `L = f(ln 0.01)`, coefficient `f(exp(d(L/(f(ms)·m))))` (float division).

The set-up keeps the filter states and envelopes (only the reset of §1 clears them).

## 4. Render **[DECOMP `FUN_006d0fb0`, `FUN_006d0e70`, `FUN_005dbc10`, `FUN_005debe0`, `FUN_005df640`, `FUN_005df500` + asm + EXEC]**

Per host block of n frames (stereo interleaved in, stereo out; scratch buffers grow to `(n + 3) & ~3`):

1. Split: modulator = L, carrier = R (swapped with the swap byte). Sum buffer = 0.
2. For each band k = 0…N−1 of the analysis bank, skipped only when it is **silent and its gain is not > 0**:
   - **Hold off:** the modulator goes through `order` identical biquads in cascade (stage 1 from the modulator, the
     others in place), **double-precision direct form I** with float buffers between stages:
     `y = x·b₀ + (((x₁·b₁ − y₂·a₂) + x₂·b₂) − y₁·a₁)`; at the end of the block each of the stage's states with
     |v| < 2⁻²⁴ is set to +0. The filtered block is written into the plugin's **output buffer**, used as scratch.
   - **Envelope follower (x87):** for each sample, `x = |y|·g` (g = the band gain), `env ← (env − x)·c + x` with
     `c = attack` if `x > env` else `release`, all in **extended precision** (the envelope stays in an x87 register for the
     whole block; it is stored as a float only at the end of the block); each sample's envelope is stored as a float in
     place. Then the 64-bit word made of this sample and the next one is compared with `2⁻²⁴`'s bits: the sample is
     zeroed — and the running envelope set to 0 — only when the stored envelope is below 2⁻²⁴ **and the next word is
     exactly +0.0** (for the last sample, the word after the block in the output buffer). The count of samples not
     zeroed decides the band's "silent" flag (all zeroed).
   - If not silent: the carrier goes through the band's synthesis biquads (same kernel), and the sum gets
     `sum = (carrier·envelope) + sum` on the **x87** (one extended rounding, then float).
   - **Hold on:** no analysis; if the band is not silent, the filtered carrier times the stored float envelope is added
     with plain float operations (`mulss`/`addss`).
3. `sum += modulator·level_mod`, then `sum += carrier·level_car` (float, always, even with level 0).
4. **The output is mono:** the sum goes to both channels.

The silent flag also freezes the band's synthesis filter (no carrier filtering while silent). The plugin keeps its
envelope meters (`+0x13c`, `+0x140`) only for the editor; they do not touch the audio.

**Floating-point environment [EXEC]:** after creation the thread runs with MXCSR FTZ + DAZ (as in rounds 51/62); without
them LYNX differed from the DLL on subnormal inputs (four 10⁻⁴³-level samples). The x87 precision is 64 bits: the
follower and the product sums were checked at 53 and 24 bits too and only 64 bits matches.

## 5. What LYNX does

- **`core/FruityVocoder`** (`fvocoder::Engine`, `parse`, `serialise`): the engine above. Host blocks longer than 512
  frames are split into 512-frame calls (the block length matters: the envelope is rounded to float at block ends).
  The word after a block is taken as +0.0, which is what the harness gives; in FL it is whatever the host left in the
  output buffer after the block — **not confirmed**, and it can only zero an envelope already below 2⁻²⁴ one sample
  earlier.
- **`core/DelphiMath`** (`dmath::exp`, `ln`, `sinCos`, `sinh`): §2, reusable by other Delphi plugins. (Round 78 merged
  round 66's `core/DelphiRuntime.h` into it: expm1, sin, cos, power, tanh and SinCos up to 4194304 were added.)
- Both files switch floating-point contraction off: the core test build (zig c++, native CPU) fused multiply-adds and
  missed the goldens until then; g++ and zig builds now give the same bits.
- **`EffectKind::vocoder`** keeps its parameter layout (bands, order, listen, swap, the twelve controls as
  `(raw − min)/(max − min)`, LYNX's side-input mode); the import (`FlEffects.cpp`) now keeps FL's state in
  `EffectSlot::state`, and the per-band levels apply while the slot's band count matches the state's (FL resets them on a
  band-count change). The old "per-band levels not applied" note is gone. Side-input mode feeds the same engine with
  mono side inputs.
- **Editor:** the mixer's effect panel shows the band levels as a bar strip above the knobs (draw with the mouse; 0…2,
  snapping to 1; right-click: reset / tilt up / tilt down), stored in the slot's FL state; knob texts follow FL's laws
  (Hz from §3.2, ms, mix levels). pt-BR strings added.

## 6. Verification **[EXEC]**

LYNX's engine (`cpp/fvcmp`, the app's sources, g++ -O2, FTZ/DAZ; the goldens and the math replica also with zig c++ for
the native CPU) against the DLL:

| Check | Cases | Result |
|---|---|---|
| Every distinct installation state, music 3 s, 44.1 kHz / 512 | 7 | **7 bit-identical** |
| Every installation state × 22.05, 32, 48, 88.2, 96, 192 kHz × (voice, blocks 512; gapped music, blocks 333) (`py/rates.py`) | 84 | **84 bit-identical** |
| Random states (bands 4…128, order 1…3, listen, swap, invert, hold, formant, width, levels incl. 0), rates 22.05/44.1/48/96 kHz, blocks 77/256/300/512, signals music / noise / gaps / voice-like / impulse, 40 % with three mid-stream control changes (`py/rnd.py`, seeds 2…9 × 25 and 11…20 × 30) | 500 | **500 bit-identical** |
| Before §2 (C library math), seeds 2…9 | 200 | 196 bit-identical, 4 with one sample off by 1…7 ulps (−315…−405 dB) |
| Delphi math replica vs the DLL's functions (`py/mathcmp.py`, `cpp/mt.cpp`) | 1.26 M arguments | 0 differences |
| Unit tests `fruity_vocoder_engine` (three states of our own: 16 bands with edited levels; 64 bands order 3 invert formant −300, 48 kHz / 300 with Hold switched on at frame 2048; 4 bands order 1 swap, 22.05 kHz / 77; plus FL import → mixer effect in one 4096-frame call) and `delphi_math_matches_plugin_runtime` | goldens from the DLL | bit-identical |

## 7. Vocodex

`Vocodex_x64.dll` is still shipped and unchanged (same SHA-256 as rounds 35/40). LYNX's `core/Vocodex` engine of those
rounds stands: model vs DLL ≤ −137 dB on every demo instance with SG off (round 35 §9), the Soundgoodizer stage and the
internal Sytrus carrier from round 40. It is not bit-exact; round 35 §9 names the residual sources (the map evaluator's
cursor arithmetic, x87 smoothing). Whether `core/DelphiMath` closes part of that residual was not tried in this round.

## 8. Open items

- The content of the word after the block in FL's own output buffer (§5) — not confirmed.
- Blocks longer than 512 frames: FL's mixer block length is assumed to be ≤ 512 (as for the other exact effects);
  with a longer single block the DLL keeps the envelope in extended precision across it (LYNX splits).
- Curved band-level editing in FL's own editor (its drawing tools) was not decoded; LYNX offers its own strip.

## Summary table

| Item | Result |
|---|---|
| State, load order, UI resets | §1 **[DECOMP + DATA]** |
| Set-up laws and precision | §3 **[DECOMP + asm + EXEC]** |
| Filters, follower, silence logic, sums, mono output | §4 **[DECOMP + asm + EXEC]** |
| Delphi exp / ln / SinCos / sinh | §2, replica exact on 1.26 M arguments **[EXEC]** |
| LYNX engine vs DLL | bit-identical on every installation state and every random case (§6) |

## Evidence index

| Evidence | What it shows | Kind |
|---|---|---|
| `FUN_006cf810` (slot 3), `FUN_006cfe70`, `FUN_006d07f0`, `FUN_006d4cf0`/`FUN_006d4d50`/`FUN_006d4a20`/`FUN_006d5070` | state, resets | DECOMP |
| `FUN_006d03a0`, `FUN_006cff90`, `FUN_006d00b0`, `FUN_006cfef0`, `FUN_006cff40`, `asm/6d03a0.s`, `asm/6d00b0.s` | set-up and casts | DECOMP + asm |
| `FUN_005dbb00`, `FUN_005db530` (type 2), `FUN_005dbc10`, `FUN_005debe0` | band-pass design, kernel, state flush | DECOMP + asm |
| `FUN_006d0fb0`, `FUN_006d0e70` (x87), `FUN_005df640` (x87), `FUN_005df500` (SSE), `FUN_005e0a60`, `FUN_005e0aa0` | render | DECOMP + asm |
| Runtime globals `0x723f80` (ln2/2), `0x723d48` (ln2/1200), `0x723e50` (f(ln 11)), function pointers `0x723f68`, `0x723de0`, `0x724098`, `0x735568`, `0x7335c8` | constants, kernels in use | EXEC (`gd=`, `ipp=`, `fp=`) |
| `FUN_00408810`, `FUN_00409870`/`FUN_00409780`, `FUN_004095e0`/`FUN_00408e80`/`FUN_00409340`/`FUN_00409270`, `FUN_0045d4d0`; tables `0x6e94e8`…`0x6e9aa8` | RTL math | DECOMP + asm + DATA + EXEC (`fcall`) |
| `py/survey.py` | installation states | DATA |
| `py/cmp.py`, `py/rnd.py`, `py/rates.py`, `py/golden.py` | verification | EXEC |
