# Behavioural analysis of FL Studio 2026: round 50 (REA) — Fruity Parametric EQ 2 and Fruity Parametric EQ

> **Nature of this document.** Continuation of `REA_rodada7.md` §1 (Parametric EQ 2 state layout and band laws, read
> from the code but never run) and first decoding of the older Fruity Parametric EQ. *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts, formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r50/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Parametric EQ 2/Fruity Parametric EQ 2_x64.dll` | `871b83968e306bbd567153c774e4722a69db7fcd74c5c2969474ef287b5254f4` (= round 7) | Parametric EQ 2 (Delphi) |
  | `Plugins/Fruity/Effects/Fruity Parametric EQ/Fruity Parametric EQ_x64.dll` | `eab6714211b38a425665edba862b00e7421b7504f2629fdaa4cf6716930c5ac1` | Parametric EQ, v1 (Delphi) |
  | `Plugins/Fruity/Effects/Frequency Splitter/engine_x64.dll` | `567fcd74ea6cc89ad85c83953321758cab103061ed908ce468cafeebf6ae9593` | C++ engine that Parametric EQ 2 loads for three of its options (§2.9, §4, §5) |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r50`, scripts `gh/scripts/DecompAll.java`, `DecompList.java`;
  outputs in `out/`), `objdump -M intel` where the order of float/double operations matters (`asm/`).
- **EXEC harness:** `run/fx.c` → `fx_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu): round 38's effect harness (unmodified
  DLL, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state restored through the plugin's own
  SaveRestoreState, Dispatcher sample rate / block size, Eff_Render), extended with a `mem=` option (bytes at an
  address and the target of a pointer/jump stub).
- **Drivers (our own code):** `py/survey.py` (every Parametric EQ 2 / Parametric EQ mixer state in the installation's
  `.flp` files → `data/peq2/`, `data/peq1/`), `py/fx.py` (runs the harness; signals), `py/pe.py` (reads values at
  virtual addresses of a DLL file), `py/peqmodel.py` (Python model of the band design, built only from the facts
  below), `py/t_coef.py` (model coefficients vs the DLL's, peeked after the state load), `py/t_out.py` (LYNX's C++
  engine vs the DLL on every state), `py/mkstates.py` (our own test states and the goldens of `tests/tests.cpp`),
  `cpp/peqcmp.cpp` (LYNX's engine on a float file, optionally through the slot-parameter round trip).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, 44.1 kHz, blocks of 512 unless stated;
"−∞" = bit-identical.

**Installation usage [DATA]:** Parametric EQ 2 in **1 138 mixer slots of 59 projects** (959 distinct parameter sets;
state versions 8 ×490, 3 ×328, 6 ×199, 7 ×100, 2 ×21 — 21 slots in old projects without the "slot closed" event,
which the first survey missed), Parametric EQ (v1) in **9 slots of 4 projects** (3 demo songs and the render test
`System/Render tests/Fruity Parametric EQ.flp`). Processing options of the Parametric EQ 2 states (§1.2): plain 274,
HQ ×2 594, HQ through Frequency Splitter's resampler 205, HQ "alternative design" 64, linear phase 8 (of which 3 also
HQ). Before this round LYNX mapped Parametric EQ 2 onto its own cookbook EQ (Butterworth-style cuts, no steep/gentle
orders, no HQ) and played Parametric EQ v1 as a flat EQ (no settings reader).

---

## 1. Parametric EQ 2: state and options

### 1.1 Layout **[DECOMP `FUN_006ec340` + DATA]**

Round 7 §1.1 is confirmed. What the loader does with the fields that matter for the sound:

- `0x004`: 36 `i32` parameters, copied raw (table of round 7 §1.2); after the load every parameter is re-sent through
  ProcessParam (VMT slot 6), which rebuilds the derived values below.
- `0x124` UI flags: bit 0 → **HQ** (`P+0x361`); the remaining bits (as a byte) → the **design mode** (`P+0x360`,
  0 or 1 in the data).
- `0x13D` options (v ≥ 4): bit 0 → **linear phase** (`P+0x370`), bit 1 → **use Frequency Splitter's resampler for
  HQ** (`P+0x37C`), bits 2–7 display options.
- `0x13E` (v ≥ 4): band enable mask → one flag per band (`P+0x2114+b`); for v < 4 every band is enabled.
- `0x15E` (v ≥ 7): `P+0x380`; > 0 enables a parameter smoother (§2.10). 0 in every state of the installation.

### 1.2 Processing modes **[DECOMP `FUN_006f1c40`, `FUN_006f1c60`, Eff_Render `FUN_006f0da0`]**

| HQ | design | `0x13D` bit 1 | Result |
|---|---|---|---|
| 0 | any | any | bands at the host rate |
| 1 | 0 | 0 | ×2 oversampling with the shared polyphase half-band (§2.7) |
| 1 | 0 | 1 | ×2 through Frequency Splitter's engine: zero-stuffing up, 176-tap FIR down (§2.9) |
| 1 | 1 | any | bands at the host rate, coefficients from Frequency Splitter's "alternative" design (§4) |
| linear phase (`0x13D` bit 0) | | | FIR built from the bands' magnitude response, run by the engine (§5) |

The engine is `Plugins\Fruity\Effects\Frequency Splitter\engine_x64.dll`, loaded when the plugin is created
(exports `mydispatcher`, `dispatch`) **[DECOMP]**. Without it those options fall back to the half-band / cookbook
paths (not observed: the DLL is always present in an installation).

---

## 2. Parametric EQ 2: the IIR engine

### 2.1 Parameters → band values **[DECOMP `FUN_006ef6f0`, `FUN_006ef120`, `FUN_006ef690`]**

Each parameter is kept as `float(raw)`; a band is recomputed whenever one of its five parameters changes (type and
order changes also clear the band's filter states). With `v` = the float of the raw value:

- frequency `f = float( exp(v · (log2(1000)/65536) · ln 2) · 20 )` (exp of a double product, the result rounded to
  float) — 20 Hz…20 kHz;
- width `w = v · (1/65536)` (float product);
- gain `g = v · 0.01` (float product) for the types that use it (low shelf, peaking, high shelf), else 0;
- order `n = |order|`, `steep = order < 0`, `inv = float(1/(n+1))`;
- band-pass/notch width factors `kbp = float( float(float(e^{0.115129255f·3f}) / float(e^{0.115129255f·6f})) · √(n+1) )`
  (≈ 0.708·√(n+1)) and `knotch = 1/kbp` (float);
- "active": 0 for a disabled type; −1 (always processed) for LP/BP/notch/HP or a non-zero gain; **rate/5** frames for
  a gain-type band at exactly 0 dB (it keeps running for 0.2 s of processed frames, then its states are cleared and
  it is skipped).

The UI type is mapped to an internal design type: Disabled 0, Low pass 1, Band pass 2, High pass 4, Notch 3, Low shelf
6, Peaking 7, High shelf 8 (`0x74E838`) **[DATA]**.

### 2.2 Per-stage design **[DECOMP `FUN_006ee6e0` + EXEC]**

A band has `n + 1` biquad stages `k = 0…n`. With `ωk = (1/rate)·2π` (double, rate = the processing rate, ×2 with HQ)
and `ω = min(f·ωk, limit)`:

| Type | limit | A | α (what goes into §2.3) |
|---|---|---|---|
| LP, HP | 3.138451099395752 | — | `q = (1 − w)³·9.9 + 0.1` (with `1 − w` a float); for `n > 0`: `q = float(q)^inv` (Delphi `Power` on floats = `exp(inv·ln q)` rounded to float); steep: `q ·= steepCutQ[n][k]`; `α = sin ω · 0.7071067811865475 / q` |
| BP, notch | 3.138451099395752 | — | `BW = (w·4.9 + 0.1)·kbp` (notch: `·knotch`); `α = sin ω · sinh(ln2/2 · BW · ω / sin ω)` |
| Peaking, not steep (or n = 0) | 2.1991147994995117 (0.7π) | `float(exp(float(g·inv·0.5)·0.115129255f))` (products in double, then float) | `BW = w·4.9 + 0.1`; `α = sin ω · sinh(ln2/2 · BW · ω / sin ω)` |
| Peaking, steep | 2.5132741928100586 (0.8π) | `float(exp(float(g·G)·0.115129255f))` | stage frequency `f_k = float(f·exp(off[n][k]·BW·s_n·ln 2))`, `BW_k = BW·s_n·wid[n][k]`, `G = 0.32125 − (BW_k − 1)·0.01` for n = 1 else `gain[n]` (0.2442, 0.214445); `α = sin ω_k · sinh(ln2/2 · BW_k · ω_k / sin ω_k)` |
| Low / high shelf | 3.138451099395752 | `float(exp(g·inv·0.5f·0.115129255f))` (all float products) | `S = (1 − w)·1.7192982456140353 + 1/3`, steep: `S ·= steepShelfS[n][k]`; `α' = √A · √((A + 1/A)(1/S − 1) + 2)` (multiplied by sin ω in §2.3) |

`sinh(x) = (eˣ − e⁻ˣ)/2` (0 for |x| ≤ 10⁻¹²) as the run-time library computes it **[DECOMP `FUN_0045d8d0`]**.
Tables, row = n (float values, **[DATA]** `0x74E718…0x74E834`):

| Table | n = 1 | n = 2 | n = 3 |
|---|---|---|---|
| `steepCutQ` | 1.98340166, 0.749214649 | 2.64303994, 1.08151865, 0.694244742 | 3.57645154, 1.14173281, 1.04471076, 0.655118585 |
| `steepShelfS` | 2.63044715, 0.615644455 | 5.16122818, 1.10927105, 0.509867311 | 10.3191233, 1.51443529, 0.694032848, 0.552996695 |
| `off` (octaves × BW·s_n) | −0.25, +0.25 | −0.475, 0, +0.475 | −0.775, −0.3, +0.3, +0.775 |
| `wid` | 1, 1 | 1, π/2, 1 | 1, π/2, π/2, 1 |
| `s_n` = 1/(n+1) | 0.5 | 1/3 | 0.25 |

So "Gentle" orders split a cut's Q (`q^{1/(n+1)}` per stage) and a gain band's dB (`g/(n+1)` per stage) over n + 1
identical stages; "Steep" cuts use per-stage Q multipliers (a Butterworth-like cascade), steep shelves per-stage slope
multipliers, and steep peaking bands spread n + 1 narrower bells across the band width.

### 2.3 Biquad coefficients **[DECOMP `FUN_005ec7e0`]**

From (A, α, sin ω, cos ω), a0 normalised, in double:

- LP/BP/notch/HP: `d = 1/(α + 1)`, `a1 = d·cos ω·(−2)`, `a2 = d·(1 − α)`; LP `b1 = d·(1 − cos ω)`, `b0 = b2 = b1·0.5`;
  BP `b0 = d·α`, `b1 = 0`, `b2 = −d·α`; notch `b0 = b2 = d`, `b1 = a1`; HP `b1 = −d·(cos ω + 1)`, `b0 = b2 = −b1·0.5`.
- Peaking: `d = 1/(α/A + 1)`, `a1 = b1 = d·cos ω·(−2)`, `a2 = d·(1 − α/A)`, `b0 = d·(α·A + 1)`, `b2 = d·(1 − α·A)`.
- Low shelf: `k = (A − 1)·cos ω`, `β = α'·sin ω`, `d = 1/(A + 1 + k + β)`, `a1 = d·((A − 1) + (A + 1)·cos ω)·(−2)`,
  `a2 = d·((A + 1 + k) − β)`, `b1 = d·A·2·((A − 1) − (A + 1)·cos ω)`, `b0 = d·A·((A + 1 − k) + β)`,
  `b2 = d·A·((A + 1 − k) − β)`; high shelf the mirror (`d = 1/((A + 1 − k) + β)`, signs of the cos terms swapped).

**EXEC (`t_coef.py`):** the Python model reproduces the coefficients the DLL holds after the state load: on 46
sampled states (320 stages) 294 bit-identical, the other 23 within 3·10⁻¹⁵ relative (one or two ulps: the run-time
library's sin/cos differ from the C library's in the last bit; not replicated, see §7).

### 2.4 Filter **[DECOMP `FUN_005ed000` (asm `asm/f_5ed000.asm`)]**

Direct form I, interleaved stereo, in double: `y = x·b0 + (((x₁·b1 − y₂·a2) + x₂·b2) − y₁·a1)`, x = the float input
widened, y kept in double in the state and rounded to float for the output. Every stage runs over the whole block
in place (the next stage reads the float output). After each call the 8 state values (x₁, x₂, y₁, y₂ per channel) go
through a clean-up that **zeroes values below 2⁻²⁴ in magnitude and keeps the others in double** (x87 code,
`FUN_005efdf0`; the decompiler shows a float conversion the instructions do not make) **[DECOMP + EXEC: output is
independent of the block size]**. Bands run in order 1…7, stages in order 0…n; a band whose enable flag is off is
skipped (its coefficients are still kept up to date).

### 2.5 Main level **[DECOMP `FUN_006ef6f0`, `FUN_005e4310`, `FUN_005f22e0`]**

`gain = float(exp(float(v·0.01f·0.115129255f)))`, applied first, to the input: copy when it is exactly 1, zeros
when 0, else a float multiply. A change glides linearly over the block with at most `float((44100/rate)·0.001)` per
frame (rate = the processing rate, ×2 with HQ); the per-frame gains are built by repeated float additions (even and odd
frames on separate running sums, groups of eight frames, the remainder one by one), and an end value below 2⁻²⁴ is set
to 0. The first block after a load or a silence reset starts at the target (no glide) **[DECOMP `FUN_005f1090` + EXEC]**.

### 2.6 Rate **[DECOMP `FUN_005f2430`]**

The plugin's rate object holds `1/rate`, `2π·(1/rate)`, the frequency ceiling `float(rate·0.4999)` (used by v1, §3) and
the glide step above; with HQ the object is set to twice the host rate.

### 2.7 HQ ×2 oversampling **[DECOMP `FUN_006ea220`, `FUN_006ea2c0`, kernels `FUN_006e9cf0`/`FUN_006e9ec0`, EXEC peek of `0x7615D8`]**

The shared polyphase half-band of rounds 36/51 (two chains of three first-order all-passes in double, coefficients
{0.14737711360104661, 0.48246854276970014, 0.88300502576937312} and {0.039151597734460045, 0.30264684832849342,
0.6746159185469639}); up: the B chain gives the even output, the A chain the odd one (floats); down: A fed by even,
B by odd samples, output `float((B₃ + A₃)·0.5)`. After each call the 12 all-pass outputs are cleaned as in §2.4
(zero below 2⁻²⁴, else unchanged). The bands are designed at 2·rate and the 0 dB countdown counts oversampled frames.

### 2.8 Silence gate **[DECOMP Eff_Render, `FUN_005f1500`, `FUN_006edbf0`]**

Per host block: the block is "silent" when every sample is within ±1e−6 (`0x358637BD`). A counter is set to
`host rate / 5` by any non-silent block (and always in linear-phase mode) and decreases by the block length on silent
ones. When it reaches −(block length) or below, the block is passed through **unchanged** (no main level either) and,
the first time, the plugin resets: states of the bands still counting down (0 dB bands) and of the half-band
resamplers are cleared (other bands keep their states), the main level glide restarts. The counter starts at 0, so a
silent first block is passed through.

### 2.9 HQ through Frequency Splitter's resampler (`0x13D` bit 1) **[DECOMP `mydispatcher` cmds 7, 9, 10, 12 + EXEC]**

- Up: every host sample `x` becomes `(2x, 0)` (float sum `x + x`) — zero stuffing, no anti-imaging filter.
- The bands run at 2·rate on that signal.
- Down, per channel: a 256-entry ring buffer (newest first: the even sample is written, the index decremented, the
  odd sample written) and a 176-tap kernel `h[i] = float( sin(i·π/176) · float(0.5·sinc((i − 89)·π/2)) )`
  (`sinc(0) = 1`; the window is `sin(i·0.017849958259032916)`) **[EXEC: the kernel read from the running DLL equals
  the formula bit for bit]**. The output is `Σ h[j]·ring[idx + j]` over 176 entries, summed in float as the engine
  does: four running sums over groups of four products, folded as `(((s₀ + acc) + s₁) + s₂) + s₃`, then any remainder
  one by one; with wrap-around the second part starts its own four sums and folds them into the first part's result.
- Reported latency: 44 host samples (`FUN_006eddc0`); the resampler's history is not cleared by the silence reset.

### 2.10 Not exercised by the installation

- The parameter smoother (`P+0x380 > 0`, object `P+0x2148`: per-parameter glides `v += (target − v)·c` per frame
  with per-frame redesign, `FUN_006f2090`/`FUN_006f21a0`/`FUN_006f0790`): no state enables it; not implemented.

---

## 3. Fruity Parametric EQ (v1)

### 3.1 State **[DECOMP `FUN_006cf5e0`, `FUN_006cfbf0` + DATA]**

Record `0xD5` = 29 `i32` copied raw (116 bytes, no version): gains ×7 (−1800…1800, `·0.01` dB), frequencies ×7
(0…65536), widths ×7 (0…65536), types ×7 (same UI types and internal map as Parametric EQ 2), main level (raw, 128
= unity; preset to 128 before the copy). No order, no HQ, no enable mask, no silence gate.

### 3.2 Design **[DECOMP `FUN_006cf980`, `FUN_005dbfa0`, `FUN_005db9d0` + EXEC]**

- Frequency: `x = float(raw·(1/65536))`, `e = float(exp(x·ln 101) − 1)`, `f = float(e·159.9 + 10)` (10 Hz…16 kHz),
  limited to `float(rate·0.4999)`; `w = float(raw·(1/65536))`; gain `g = float(raw)·0.01f`.
- `ω = f·(2π·(1/rate))`; LP/BP/notch/HP: `α = sin ω / ((w·18.9 + 0.1)·2)` (Q = 0.1…19);
  peaking: `A = 10^(g·0.025)` (`exp(g·0.025·ln 10)`), `Q = float(exp(w·ln 1001) − 1)·0.0999 + 0.1`, `α = sin ω/(2Q)`;
  shelves: `A` as above, `α' = √((A² + 1)/(w·1.9 + 0.1) − (A − 1)²)` (× sin ω in the design).
- One stage per band, coefficients as §2.3 (the same routine), filter and state clean-up as §2.4; 0 dB gain bands
  count down rate/5 frames as in Parametric EQ 2.
- Main level: `x = float(raw·0.0078125)`; 1 → copy, else `float((exp(x·2.39789534f) − 1)·0.1f)` (= (11ˣ − 1)/10),
  applied first.

These are the laws of the mixer track's built-in EQ (round 6 §1.2–1.3, FLEngine) as well — same frequency law, same Q
and shelf-slope laws, same cookbook routine.

---

## 4. Parametric EQ 2: "alternative design" (HQ with design mode 1)

In this mode there is no oversampling; every stage's coefficients come from Frequency Splitter's engine (export
`dispatch` → `FUN_1801de630`) instead of §2.3. 61 states use it (3 more combine it with linear phase, §5).

### 4.1 What the plugin passes **[DECOMP `FUN_006ee6e0`, `FUN_006f1c80` + asm]**

`dispatch(code, out, float(f/rate·2), float(Q), float(G))`, rate = the host rate (`P+0x2080`), f the stage frequency;
engine codes LP 2, HP 3, band pass 9, notch 10, low shelf 5, peaking 6, high shelf 8. The five results are b0, b1,
b2, a1, a2 (a0 = 1).

| Band type | f | Q | G |
|---|---|---|---|
| LP / HP | the band frequency | the stage q of §2.2 (after the gentle power and the steep table) × √0.5 | 0 |
| Band pass / notch | the band frequency | `1/(2·sinh(ln2/2 · BW))`, BW including `kbp`/`knotch` | 0 |
| Peaking | band frequency; steep: the shifted stage frequency (double, not rounded to float) | `1/(2·sinh(ln2/2 · BW_k))` | the stage dB × 2 |
| Shelves | the band frequency | `1/√((A + 1/A)(1/S − 1) + 2)` with §2.2's float A and S | `float(g·inv)` |

### 4.2 The engine's design **[DECOMP `FUN_1801de630` (asm `asm/de630.asm`) + EXEC]**

A magnitude-matched second-order design (no bilinear frequency warping). With `f` the frequency over Nyquist and
`A = 10^(G/20)`:

- **Poles** sampled from the analog prototype: `a2 = e²`, `a1 = −2e·c`, `e = exp(−(π/2)·f/Q)`,
  `c = cos(π·f·√(1 − 1/(4Q²)))` for Q > 0.5, else `cosh(π·f·√(1/(4Q²) − 1))`.
- **Notch:** zeros on the unit circle at f, normalised to unity gain at DC. **Band pass:** numerator from the analog
  |H|² at DC, Nyquist and at `sin²(πf/2)`.
- **LP / HP / peaking / shelves:** the pole and zero magnitude-squared terms are fitted at characteristic points and
  turned into coefficients by a common closing step (`b0 = n·(√(h_z² + z_X) + h_z)`, `b1 = n·(1 − √z₀)`, …, with
  `h = (√x + 1)/2` and `n = 1/(√(h_p² + p_X) + h_p)`). LP: fit at `min(f, 0.5)`, a resonance fit when `1/Q² < 2`
  and the peak lies below Nyquist (`1/Q²` in [1.99, 2) is clamped to 1.99); HP: same structure with a blended branch
  when `f² ≥ 1 − 1/(2Q²)`; peaking: a band-edge fit when `f ≥ 1`, else a fit at f; shelves: the analog-pole solution
  for `Q² ≤ 0.6`, otherwise three fit points (r, `y = x·√v`, y/r) with a pow/log term; the low shelf scales the
  numerator by A. Peaking and shelves return the identity when `|A − 1| < 0.001`.
- The helpers it calls are the C library's cos, cosh, exp, log, pow and sin (identified by EXEC).

**EXEC:** a scratch translation was checked against `dispatch` on 21 000 random calls (all seven codes): identical
apart from the sign of one NaN; LYNX's own formulas with the C library's maths are bit-identical on 96 % of those
calls, the rest within 2·10⁻¹² relative (band pass up to 10⁻⁸ for ill-conditioned inputs). Engine code 0xB (type
"all pass", never produced by the EQ) is not handled by the engine: the plugin then keeps whatever was in its buffer —
not reproduced (LYNX uses the identity).

---

## 5. Parametric EQ 2: linear phase

With `0x13D` bit 0 the IIR bands are not run at all: the plugin samples the EQ's magnitude response and hands it to
Frequency Splitter's engine (`mydispatcher` cmds 0, 2, 3, 4, 5, 6, 0xD, 0xE), which runs it as a zero-phase FIR. 8
states use it (1 plain, 3 HQ, 1 HQ-external, 3 alternative design: the option only changes which coefficients the
response is computed from).

### 5.1 Size and table **[DECOMP `FUN_006eddc0`, `FUN_006edd00` + EXEC: tables bit-identical]**

- FFT size `N = 512·2^q`, q = the quality field (`0x135`, default 2); halved below 44.1 kHz, doubled
  `round(log2(rate/44100))` times at or above it, at most 65 536 (2048 at 44.1/48 kHz, 4096 at 88.2 kHz, 1024 at 32
  kHz).
- `N/2 + 1` bins `k·rate/N` (bin 0 at 0.1 Hz). Each is evaluated on the bands as designed at the processing rate (2·fs
  with either HQ resampler, fs otherwise); bins above `float(processing rate·0.4999)` get 0 dB. Narrow bands (width
  < 0x2000, UI type band pass, notch or peaking) move the nearest grid point onto their centre frequency.
- Power per stage in double (the 2ω term as `float(ω)·2`), multiplied over the enabled bands' stages in float (bands
  at 0 dB whose countdown has ended are still included); `dB = 18·float(log10(P)·5/9)`, clamped to ±18000; amplitude
  `2^(dB/6)` by a cubic approximation of 2ˣ (so a +18 dB boost comes out ≈ +18.06 dB), 0 below −100 dB; the last bin
  copies its neighbour.

### 5.2 Kernel and convolution **[DECOMP + EXEC fit to 8·10⁻⁸]**

`h = (1/N)·IFFT(T[k]·(−1)^k)` (real, zero-phase, centred at N/2); the taps `N/4 … 3N/4 − 1` are kept and multiplied by
`sin(i·π/(N/2 − 1))` → an N/2-tap FIR centred on its tap N/4. It runs as overlap-add (block N/2, FFT N) per channel,
output scaled by 1/N in float. **Latency 3N/4** (1536 samples at 44.1 kHz), the value the plugin reports.

### 5.3 Changes and start-up **[DECOMP + EXEC]**

A new table (any parameter change) is taken at the first sample of the next block once no cross-fade is running: the
idle convolver is cleared, loaded, primed with the last N input samples and becomes the current one; the output
cross-fades from the old one over `int(float(rate)·t)` samples, t = {0.1, 0.25, 0.01, 0.02} s by the fade index
(`0x139`, default 0). At start the "old" convolver is empty, so the EQ fades in from silence over 0.1 s. The silence
gate (§2.8) never bypasses in this mode; the main level glide (§2.5) is applied before the convolution.

### 5.4 Not bit-exact

The engine's single-precision FFT (statically linked IPP), its window's `rcpps` reciprocal (CPU-dependent) and vector
sine are not replicated; LYNX convolves in double. If the host rate changes after the state load, the DLL keeps the
table of the old rate until the next parameter change (observed); LYNX always uses the current rate.

---

## 6. What LYNX does with it

- **`core/ParametricEqFl`** (new): `peqfl::Engine` reproduces §2–§4 on stereo blocks (host blocks split into FL's
  512-frame blocks, interleaved internally); `parsePeq2` / `parsePeq1` read the saved states.
  **`core/ParametricEqLinear`** (new): the linear-phase FIR of §5 (`peqfl::LinearPhase`), used by the engine.
- **No new effect kind:** an imported slot stays `EffectKind::parametricEq`, so LYNX's EQ editor keeps working. The
  slot's parameters carry FL's own values (layout in `core/ParametricEqFl.h`): type, frequency (the same 20 Hz…20 kHz
  law LYNX's EQ uses), gain, width, order (Steep 8…Gentle 8), main level, plus an "FL law" marker, the processing
  mode, linear phase and the enable mask in otherwise unused parameters [36…39]. Parametric EQ v1 keeps its own raw
  frequency and main level in [40…47] and uses them while the editor's values still equal them. The round trip
  state → parameters → engine is exact (EXEC checks below run through it).
- The editor and the mixer's knob texts show FL's units for such slots (bandwidth %, order names); the band menu
  offers FL's seven orders for every type; the response curve is computed from FL's coefficients at the rate the
  engine runs.
- Automation imported from FL: band gain, frequency, width and main level map onto the same parameters.
- Latency (PDC): 44 samples for the Frequency Splitter resampler mode, 3N/4 (1536 at 44.1 kHz) in linear phase.
- Main level: FL's glide on changes (§2.5) in every Parametric EQ 2 mode; v1 applies its level directly.
- Fallback: a state too short to be FL's keeps the old cookbook mapping of round 7.

---

## 7. Verification **[EXEC]**

LYNX's C++ engine (`cpp/peqcmp`, the same sources as the app, built with zig c++ and with g++) against the DLL on
**every** saved state of the installation (`t_out.py`; plus `--via`: through the slot parameters):

| Mode (states) | Music, 44.1 kHz, blocks of 512 | Noise, 48 kHz, blocks of 300 (1 state in 4) |
|---|---|---|
| plain (274) | 259 bit-identical, worst −184.1 dB | 59 of 60 bit-identical, worst −200.6 dB |
| HQ ×2 (591) | 434 bit-identical, worst −166.0 dB | 127 of 153, worst −174.0 dB |
| HQ through Frequency Splitter's resampler (204) | 154 bit-identical, worst −169.9 dB | 35 of 50, worst −179.0 dB |
| alternative design (61) | 58 bit-identical, worst −187.1 dB | 19 of 19 bit-identical |
| linear phase (8: plain 1, HQ 3, external 1, alternative 3) | −126.9…−134.1 dB | −132.0…−134.6 dB (rate set before the state load, see §5.4) |
| Parametric EQ v1 (9) | 7 bit-identical, worst −170.2 dB | 7 bit-identical, worst −177.0 dB |

- Through the slot parameters (`--via`, 1 state in 10): identical to the direct path in every mode.
- Coefficients alone (`t_coef.py`, model vs the DLL's memory after the load, 46 states / 317 stages): 294
  bit-identical, the rest within 3·10⁻¹⁵ relative.
- Alternative design alone: 21 000 random `dispatch` calls (§4.2) and 42 synthetic single-band states (all seven
  codes, random frequency/width/order/gain) bit-identical.
- Linear phase: every magnitude table bit-identical; mid-stream changes of band gain/frequency/type and of the main
  level at 44.1/512, 48/300 and 44.1/333 within −127.0…−133.6 dB; FFT sizes 1024 (32 kHz), 2048, 4096 (88.2 kHz).
- Unit tests (`tests/tests.cpp` `fruity_parametric_eq_exact`): our own states (no FL data) — plain, HQ, external
  resampler, enable mask, alternative design and v1 bit-identical to DLL goldens, linear phase (plain and HQ) within
  10⁻⁶; FL import → mixer effect with host blocks of any size, latency, parameter texts, response graph. The
  real-files test counts every Parametric EQ 2 / Parametric EQ mixer slot of the installation on the FL law.

The residual of the IIR modes comes from last-bit differences of sin/cos between FL's run-time library and the C
library (§2.3): one ulp in a coefficient, a few float roundings later in the output. The linear-phase residual is the
engine's single-precision FFT (§5.4).

## 8. Open items

- The parameter smoother option (`0x15E` > 0) is not implemented (no state uses it).
- Linear phase: the IPP FFT rounding (−127…−135 dB residual); the stale table after a host-rate change (§5.4);
  quality values other than 2 and fade indices other than 0 follow the decoded tables but no state uses them.
- Alternative design: engine code 0xB (never produced by the EQ) and the plugin without `engine_x64.dll`.
- The mixer track EQ (round 6) shares v1's laws (§3.2); LYNX's track EQ was not switched to this engine because the
  float rounding of FLEngine's derived values was not checked by EXEC.
