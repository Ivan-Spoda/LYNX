# Behavioural analysis of FL Studio 2026: round 66 (REA) — Fruity Love Philter and Fruity Vocoder, exact engines

> **Nature of this document.** Rounds 16–20 and 27 decoded the Love Philter's state, its parameter laws, the pattern
> envelopes, the LFO and the pan law; LYNX converted each unit into a parametric EQ plus gain/pan/rhythm effects, with
> three gaps listed in `STATUS.md` (Mango cutoff in Hz inferred from its pole, cutoff/resonance modulation reaching the
> EQ one block late, pulse curves with more than ten cycles). This round decodes the plugin's render itself and
> replaces the conversion with the plugin's own engine, then does the same for the Fruity Vocoder (round 20 §4.1 had
> the laws). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Embarcadero, nor Intel IPP) code or
> pseudocode was copied: what is here are data layouts, formulas, operation orders and behaviours described in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was opened; no
> Image-Line preset or audio data was copied into the repository (the unit tests use states built by our own scripts).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r66/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Love Philter/Fruity Love Philter_x64.dll` | `b9f11d6359c04d531a031c30141f46573dd8ce301e62b6de612c1329ba2ff547` (= rounds 16–20, 27) | Delphi plugin with its own DSP |
  | `Plugins/Fruity/Effects/Fruity Vocoder/Fruity Vocoder_x64.dll` | `bfc3bec327dda1f943cdd654220b9c0fa3dffa183080a4a8c56361fea63961b6` (= rounds 19–20) | Delphi plugin with its own DSP |

- **Tools:** headless Ghidra 12.1.4 (project `~/ghproj/r66`, `gh/scripts/DecompAll.java`; decompilations in
  `out/lp_main.c` (plugin class), `out/lp_env.c`, `out/lp_env0.c`, `out/lp_lfo.c` (envelope/LFO library),
  `out/lp_dsp.c` (kernels), `out/lp_os.c` (oversampler), `out/lp_rtl.c`, `out/lp_m466.c`, `out/lp_pw.c` (runtime math),
  `out/voc_main.c`, `out/voc_dsp.c`), `objdump -M intel` per function (`py/asmf.sh`) wherever the operation order or
  the FPU used matters — **the decompiler showed several x87 kernels as float code** (§4.2), so every kernel LYNX uses
  was read in the disassembly; constants with `py/k.py`; runtime pointers and globals read from the running DLL.
- **EXEC harness:** `run/lp.c` (round 53's Gross Beat harness: the Delphi plugin with its own SaveRestoreState,
  Dispatcher, ProcessEvent (tempo), ProcessParam and Eff_Render; host song position answers on dispatcher `0x24`,
  dispatcher events at sample positions), extended with `dfn=` (call one function of the plugin DLL on a file of doubles:
  `double f(double)`, `double f(double,double)`, `void f(double,double*,double*)`) and quiet host locks. zig cc 0.14.1,
  x86_64-windows-gnu; unmodified DLLs, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`.
- **Drivers (our own code):** `py/survey66.py` (every Love Philter / Fruity Vocoder mixer state and preset of the
  installation → `data/states66.pkl`, `data/presets66.pkl`), `py/lpstate.py` (state reader/writer used to build
  synthetic states), `py/syn.py`, `py/lpx.py` (runners), `py/t2.py`…`py/t20.py` (comparisons), `py/tv1.py`, `py/tv2.py`
  (vocoder), `py/t_dm.py` (runtime math), `cpp/lpcmp.cpp`, `cpp/voccmp.cpp`, `cpp/dm.cpp` (LYNX's engines and math on
  files; built for Linux with g++ and clang, and for Windows with zig), `py/gold66.py`, `py/goldv.py` (goldens of
  `tests/tests.cpp`).

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "bit-identical" = every output sample equal.

---

## 1. Installation usage **[DATA]**

86 distinct Love Philter states: 43 mixer slots in 18 projects (39 distinct) and 47 presets under
`Data/Patches/Plugin presets/Effects/Fruity Love Philter`. State versions 2, 5, 10, 11, 12. Enabled units use filter
types 0–7, 9 and 11, every mode 0–5 (alternate modes 4/5 in 9 units), FLAT in 1, the waveshaper in 21 (2 unipolar),
NEXT in 18; global HQ in 4 states, oversampling orders 0/1 in 14/72; X/Y maps on cutoff, resonance, volume and pan,
follower maps on volume and cutoff (USE1 in 3 states); pattern envelopes on pan/volume/cutoff/resonance, LFOs on every
target. Fruity Vocoder: 3 distinct states (1 demo slot, presets *Default* and *Clear*).

---

## 2. Love Philter: render structure **[DECOMP `FUN_0085aa10` + EXEC]**

One host block of n stereo frames:

1. **Silence test and up-sampling.** If every input sample is within ±10⁻⁶ (bit test on |x|), a countdown
   (initialised to `Round(fs_int/5)`, i.e. 200 ms) decreases by n, otherwise it is reloaded. With an oversampling order
   k > 0 the input goes through k 2× up-sampling stages, unless the countdown is ≤ −n: then the up-sampled buffer is
   zeros and the up-sampler's memories stay as they were.
2. **Control-tick segments.** The internal block (n·2^k frames) is cut where the tick counter runs out. At each tick:
   Mod X/Y smoothing and indices, the follower index, then every *active* articulator is evaluated (§3).
3. **Follower (mode 1)** on the segment's input (§3.3).
4. **Units** in index order, only the enabled ones (§4). The work buffer is shared: a unit starts from the previous
   unit's output (before pan) scaled by NEXT, plus its own input × IN.
5. Each unit's output is mixed into the output accumulator through its 2×2 pan/volume matrix.
6. After the block: k 2× down-sampling stages, then CENTER (§5.3) or a plain copy.

The first-tick flag (`+0x16C4`, set by FL's flush message, dispatcher 2) makes every ramp start on its target; it is
**not** set at creation nor by the sample-rate message, so a freshly created plugin ramps from zero during its first
tick **[DECOMP]**. Whether FL sends a flush when an export starts is **not confirmed**.

---

## 3. Control: tick, Mod X/Y, articulators **[DECOMP + EXEC]**

### 3.1 Tick and time scales (`FUN_00856690`, `FUN_00856570`, `FUN_00854e20`)

- `v` = 64, or 8 with the HQ-envelopes option or FL's rendering flag; `t₀ = v·fs_int/44100`;
  **tick N = max(4, 4·Round(t₀/4))**; `τ = N·v/(t₀·96)` (= N·44100/(fs_int·96)). Free time scale `2·τ·96/44100`
  (= 2N/fs_int, so a point Δt of 1 lasts 0.5 s), tempo scale `(τ·96/44100)·BPM/60` (Δt in beats). The scales are set
  at load, at every rate change and at every tempo event (then only for the enabled units' articulators).
- **Mod X/Y smoothing** (`+0x14C`): coefficient `exp(ln 0.01·τ/96)` (float); per tick, with SMOOTH on,
  `m ← target + (m − target)·coef` (float; m starts at 0 when the plugin is created, so it glides from 0 to the stored
  value), otherwise `m = target`; index = `Round(m)` clamped to 0…1024.

### 3.2 Articulators (`FUN_008543d0`, `FUN_00789a80`, `FUN_0085cc80`; library `FUN_00786350`, `FUN_00787ba0`, `FUN_00788b70`)

- Combination modes as round 18 §4 (pan, cutoff, resonance additive; the rest multiplicative); level offset of the
  pattern envelopes and maps = 0.5·mode, of the LFO amplitude envelope = 0.5.
- Per tick: `out = base`; for each present map table (X at the smoothed Mod X index, Y at Mod Y, follower at the
  follower index × envelope depth): mode 0 multiplies, mode 1 adds, other modes multiply by 2·m; then the pattern
  envelope (mode 1: `out += E·d_E`; mode 0: `out = out·E·d_E`) and the LFO (mode 1: `out += L·d_L`; mode 0:
  `out ·= 1 + 2·L·d_L`), all in float.
- **Depths:** the LFO depth of every articulator is overwritten with the master LFO level `scurve11(p1/128)`; the
  cutoff articulator's envelope depth is the unit's ENV `p0/128` and its LFO depth `p0/128 ×` master LFO. The other
  envelope depths come from the state.
- **Which articulators run:** a list rebuilt at load, at pattern changes and at transport resyncs holds the
  articulators (of every unit, enabled or not) with an enabled envelope or LFO or a non-default map; the others output
  their base.
- **Envelope loading:** the pattern envelopes skip their four knob integers (factors stay 1, 1, 0, 1) and drop their
  decay point; the LFO envelope drops its decay point; maps drop decay, loop and sustain and are normalised to an x
  length of 1 (a point is inserted at x = 0 if the first Δx is positive; every Δx scaled by 1/Σ). Point Δx and level
  are rounded to float; a NaN level takes the previous point's.
- **Evaluation** is the library's envelope stepper (stage, target point, phase, rate; `phase += adv·rate`, while the
  high word of the phase says ≥ 1: carry, next point or loop) with curve modes 0–12 (round 31 §4.4; mode 10 = the
  "smooth" curve of round 51 §2.4), the zero-tension linear mask `0x1B83`, step counts
  `Round(0.001·(1000001^(1−0.6|t|) − 1))`. **EXEC:** every mode 0–12 with tensions 0, 0.4, −0.6 bit-identical.
- **LFO** (`FUN_00788b70`): during the delay segment (first point of the amplitude envelope) the phase does not move;
  after it the 32-bit phase advances by `Round(adv'·inc)` with adv' the part of the tick after the delay, then
  `L = A·W(phase)` (float). The waveform (round 31 §4.5) is evaluated with **float arithmetic on the float-converted
  phase** (`1 − float(u)·s₀`, `float(u − p₅)·s₁ − 1`), the tension curve in double. Increment per tick
  `Round(min((10001^(S/65536) − 1)·2³²/625·scale, 805306368))`, zero with the frozen flag.

### 3.3 Follower (`FUN_0085a990`, kernel `FUN_0085a910`, `FUN_00854cc0`)

- Mode = 0 without follower maps, else 1 + USE1 (state byte): **1 = on the input, 2 = on unit 1's output** (after its
  waveshaper, per segment).
- Mono `max(|L|, |R|)`, × follower level `curve11(p2/128)`; one-pole with attack/release coefficients
  `exp(ln 0.01 / (float(81^(p/16384) − 1)·0.0125·fs_int))` (0 when the time is 0); **x87: the envelope stays in extended
  precision within the call**, its float copy is stored per sample and cleared below 2⁻²⁴ (then the extended value is
  cleared too). The tick's index is `Round(1024·peak)` of the stored values since the previous tick.

### 3.4 Transport (`FUN_0085cc80`, `FUN_0085cfa0`, dispatcher 12/13, host query `0x24`)

- On "playing" (dispatcher 12), a song-position change (13), a time signature (14) and the rate message, every active
  articulator is restarted and positioned at `pos = (samples − N + counter)/N` ticks with the tick counter reset to 0;
  `samples = t·fs_int·240/(BPM·PPQ·4)`, t = the host's song time in ticks (PPQ from dispatcher 14, default 96), the
  divisor formed in float; 0 when not playing (so a restart while stopped puts the envelopes at −1 tick: they start on
  the next tick).
- Envelope positioning: start, then for pos < 0 a first segment of rate −1/pos; otherwise the time `pos·scale`
  reduced into the loop (`Trunc` of the loop count) and one evaluation step of that length.
- LFO positioning: start, the envelope positioned as above, phase = start phase + the fifth LFO integer +
  `Round(pos·inc)` (the unfrozen increment).
- **EXEC:** 20 states × song positions 0, 7.3 and 33 beats × live/export: bit-identical.
- Pattern change (global 5): every articulator points at the new pattern's envelope; envelopes (not LFOs) are
  repositioned, at the song position if *Sync pattern changes* (state byte `+0x15E`) is on.

---

## 4. Units **[DECOMP `FUN_0085aa10`, `FUN_00859100`, `FUN_0085d5a0`, `FUN_00858af0`, `FUN_0085d110` + EXEC]**

### 4.1 Parameters and set-up

- Unit values (float): `c₀ = p1/256`, `r₀ = p2/256`, L/B/H = `curve11(p3…p5/128)`, WS pre `p6/128`, WS mix `p7/128`,
  PAN `p8/128`, IN `scurve11(p10/128)`, VOL `curve11(p9/128)·master` (0 when another unit is soloed). NEXT of unit i is
  `scurve11(p11/128)` of unit i − 1, only if unit i − 1 is enabled.
- Mode m: n = m − 1 (last stage); m − 1 > 2 → alternate (n −= 2). Q exponent `1/(n+1)` (Mango: `1/(n/4 + 1)`);
  band-width factor `m·ln2/2` (type 5: `(1/m)·ln2/2`); rate factor `k = 44100/fs_int`. Mode 0 = no filtering.
- Per tick and unit: `c = c₀ + cutoff art.`, `r = r₀ + resonance art.`; the coefficient laws are rounds 16/17/19's
  (all confirmed here, with their float/double steps), the alternate modes raise the normalised cutoff x to 0.5/0.75 per
  stage from the last stage backwards, and the Q split uses the runtime's Power (`exp(y·ln x)`). The shelf/peak gain is
  `0.125·8^(p3/64)` of the raw L knob (the L articulator does not reach it); the Mango drive is `p3/128` raw.
- **Mango in Hz** (STATUS gap): no conversion is needed any more — the engine runs the plugin's pole coefficient
  `1 − exp(−π·w·(1.873w³ + 0.4955w² − 0.649w + 0.9988))` directly.

### 4.2 Kernels (`FUN_006257c0/58c0` SVF, `006259e0/5b00` Lime, `006251f0` biquad, `006261c0/5ea0` Mango)

| Kernel | Arithmetic **[objdump]** |
|---|---|
| Chocolate SVF (plain/ramped) | **x87**: low-pass and band-pass integrators in extended precision through the block, outputs stored as floats, ramped f/q added in extended after each sample; per channel; memories stored as doubles then flushed (|x| < 2⁻²⁴ → 0, no rounding) |
| Lime LP/HP | x87 as round 31 (extended integrators, ramped A/B) |
| Vanilla biquads | SSE double, `b0·x + (((x₁·b1 − y₂·a2) + x₂·b2) − y₁·a1)`, memories flushed per call (no rounding) |
| Mango linear / driven | **x87**: the four poles chained in extended precision (each pole's state is stored as a float but the next pole receives the unrounded value), coefficient and drive used as doubles; driven pole `S(v) = v·m/(|v|·m + 12)`, `m = (3 + |v|)·|v| + 6` |

The Chocolate output per stage is `L·low + B·band + H·high` into the work buffer: L by an in-place multiply (IPP
`MulC`), B and H by **IPP `AddProductC`, which is a fused multiply-add on this machine** (AVX2 path of FL's
`dsp_ippv2_x64.dll`; without FMA the result differs in the last bit — EXEC: fused = bit-identical, unfused ≈ −140 dB).
Ramped gains use the plugin's two-lane ramps (frames 2i/2i+1 from g and g + inc, stepping 2·inc; the tail from the
first lane by inc). The gains, the Chocolate/Lime coefficients, the waveshaper mix and the pan matrix ramp over each
segment from the previous target with a slope limit of `0.01·k` per sample (`FUN_0062aee0`); biquad and Mango
coefficients jump.

FLAT on the SVF: after the ramps, stage 0's damping becomes `q_start·k/max(f_start, f_target)` and the other stages'
`k/max(…)` (persisting), the q ramp increment staying the unflattened one.

### 4.3 Waveshaper (`FUN_0085c340`, kernels `FUN_0062a380/a400/a550/a5d0`)

Table of N = 16383 (bipolar) or 32767 (unipolar, state byte 1) int16 points rasterised from the curve (a map) by the
library's iterator stepped by `float(1/N)`, values `Round(curve·((byte+1)·32768 − 1))`; the four x87 kernels of
round 51 (nearest or linear, bipolar odd-symmetric or unipolar), with dry 0 and wet 1; **the pre-gain×N and wet/32768
products are formed before the interpolating kernels switch the x87 rounding to truncation** (EXEC: two presets differed
at −184 dB in export mode until then). The interpolating kernels run while FL renders or with the state's last byte.
Mix `clamp(art7·p7/128, 0, 1)`: 1 copies, other values `x·(1 − m) + shaped·m` (ramped: the two-lane ramps).

### 4.4 Pan law (`FUN_00616620`, `FUN_0062afb0`)

Round 27 §3.1 confirmed: `pan = PAN + 2·art₀`, `vol = VOL·art₁`; pan 0 → diagonal; vol 0 → zero; otherwise
`x87 cos/sin((1 + pan)·π/4)·float(vol·√2)` and the fold. The mix kernel (`FUN_00629090`) is SSE: per frame
`(l·LL + r·RL) + y`, `(r·RR + l·LR) + y`, ramped matrices advancing per frame.

---

## 5. Oversampling, rendering, CENTER **[DECOMP + EXEC]**

- Order = the state's `DQOverSelect` (0…4); with FL's rendering flag (process mode bit 1) `min(2·order, 4)`; ticks of
  8. Each 2× stage is the polyphase half-band of round 51 §2.6 (same coefficients, read from the running DLL), memories
  flushed after each call without rounding; stages run from the highest index down, up and down.
- CENTER: x87 one-pole tracker `v += (x − y)·A; y += v; v ·= 0.96; out = x − y`, `A = 1.0070235/fs` (host rate),
  memories flushed and stored as doubles per block.
- MXCSR as FL's thread (FTZ/DAZ) — LYNX renders with the same flags.

---

## 6. Fruity Vocoder **[DECOMP `FUN_006d0fb0`, `FUN_006d03a0`, `FUN_006d00b0`, `FUN_005dbb00`, `FUN_006d0e70`, `FUN_006cf810` + EXEC]**

Confirms round 20 §4.1 and adds the operation order:

- **Set-up** (state load, every parameter change, the rate message; filter memories kept): listen 0/1/2 levels; FreqMin/
  Max `float(float(101^(p·2⁻¹⁶) − 1)·159.9 + 10)` (min ≥ max → max − 1); gain factor `wet·1500/BW` (float); width
  factor `BW·order/120000`; scale `S' = Round(10000^(p/128) − 1) + 1`; band edges `fmin + (float(e − 1)/S')·(fmax −
  fmin)` with `e = exp(float(i/N)·ln(S' + 1))`, in cents `ln(f/440)/(ln2/1200) + 5700`; the synthesis bank (built first)
  adds the formant and mirrors the index with Invert; a band exists when its centre `440·exp((mid − 5700)·ln2/1200)`
  is ≥ 10 Hz and below `20000·fs/44100` — **fs·20000 is a 32-bit product** (it wraps above 107 kHz, as in the DLL) —
  and, for the analysis bank, when the synthesis band of the same index exists. Missing bands are cleared and silenced.
  Band-pass with constant 0 dB peak, `α = sin ω·sinh(ln2/2·width·ω/sin ω)` (runtime sinh).
- **Render**: per band (skipped when silent and its gain is 0): the modulator through the analysis copies (order 1–3,
  SSE double biquads), the x87 follower in place (|y|·gain, attack/release `exp(float(ln 0.01)/(ms·fs/1000))`); **its
  denormal test reads 64 bits**: the envelope is cleared only when it is below 2⁻²⁴ *and the next raw sample is +0*
  (after the last sample the test reads the next value of FL's output buffer — LYNX reads +0, as the harness's zeroed
  buffer gives); the band is silent when every sample was cleared. A non-silent band filters the carrier through its
  synthesis copies and accumulates `car·env + out` (x87, one rounding to float). Hold uses the stored envelope (SSE
  multiply-add). Then `out += mod·g_mod` and `car·g_car` (SSE, when the level is ≥ 0); mono output.
- Parameters 0…11 set directly (raw); the state's levels are per analysis band.

---

## 7. The Delphi runtime math **[DECOMP/objdump `FUN_0040ad30` Exp, `FUN_0040c090`/`FUN_0040bfa0` Ln, `FUN_0040bb50` Sin, `FUN_0040ba60` Cos, `FUN_0040be00` SinCos, `FUN_0040b4e0` reduction, `FUN_0040b010` ExpM1, `FUN_00466840` Power, `FUN_00466a90` Sinh, `FUN_00466b40` Tanh, `FUN_005fc870` single Power + EXEC]**

Both plugins compute with the Delphi runtime's own double routines, which differ from the C libraries' in the last bit
on a few percent of inputs (EXEC: 3 % of sin/cos, 9 % of ln and 0.25 % of exp against glibc; MinGW's differ elsewhere) — enough
for 6 of the 86 states to differ (−147…−201 dB) between a Linux and a Windows build of LYNX before this was re-created:

- **Exp**: n = `Trunc(x/ln2 ± 0.5)`; r = x − n·ln2 split in two parts; j = `Trunc(64·r ± 0.5)`; r' = r − j/64; a degree-5
  polynomial; a table of `e^(j/64) − 1` (j = −45…45, as an unevaluated hi + lo pair — LYNX computes it itself; it
  equals the DLL's) recombined as a double-double sum; ×2ⁿ.
- **ExpM1** (|x| < ln 2): the same table and polynomial without the 2ⁿ scaling; Exp − 1 above.
- **Ln**: mantissa normalised to [√2/2, √2), f = m − 1, s = f/(f + 2), a degree-11 polynomial in s² split in even/odd
  halves, `+ k·ln2_lo + k·ln2_hi`.
- **Sin/Cos/SinCos**: |x| < π/4 directly; otherwise a reduction by multiples of π/2 (three-part constants; a
  double-double chain up to 4194304) and two kernels (sine: odd polynomial plus `(1 − z/2)·tail`; cosine:
  `w = 1 − z/2` plus a corrected remainder).
- **Power**: integral exponents by squaring, otherwise `Exp(y·Ln x)`; **Sinh** `(Exp x − Exp −x)/2` (0 below 10⁻¹²);
  **Tanh** `1 − 2/(Exp 2x + 1)` from ln2/2, `ExpM1(2x)/(ExpM1(2x) + 2)` below, a cubic below 1.8·10⁻⁵, 1 above 23.

**EXEC (`py/t_dm.py`, `cpp/dm.cpp`):** Exp 150 000, Ln 180 000, Sin/Cos 135 000 each, SinCos 135 000, ExpM1 75 000,
Tanh 90 000, Sinh 75 000, Power 60 000 random arguments over the plugins' ranges: all bit-identical, in both DLLs (the
Vocoder's copies are the same routines). LYNX's `core/DelphiMath.h` holds the re-creation; arguments outside the handled
ranges (overflow, subnormals, trigonometric arguments ≥ 4194304) fall back to the C library.

---

## 8. Host interface **[DECOMP dispatcher `FUN_008545c0`, `FUN_008579e0`, `FUN_00857990`]**

- Dispatcher: 1 process mode (bit 1 = rendering: order ×2, fine ticks), 2 flush (filters, oversamplers, CENTER,
  follower, first-tick flag, resync), 4 sample rate, 11 enable (resync), 12 playing, 13 position, 14 time signature.
  ProcessEvent 0 = tempo (float).
- ProcessParam (359 parameters, round 16 §2.4): globals 0–2 recompute the levels and every unit, 3–4 the follower
  coefficients, 5 switches the pattern (§3.4), 6–7 set Mod X/Y; unit parameters recompute that unit; articulator
  parameters set the LFO knobs (speed, tension, skew, pulse width) and rebuild the LFO. **EXEC:** 6 automation sets
  (Mod X/Y, CUT/RES/VOL/PAN, masters, follower, pattern switches, LFO knobs, NEXT, IN) × 10 states × live/export:
  bit-identical apart from the residuals later removed by §7.

---

## 9. What LYNX does

- `core/LovePhilter.{h,cpp}`: the state reader, the engine (§2–§5, §8) and the mixer effect
  (`EffectKind::lovePhilter`). The slot keeps FL's state in `EffectSlot::state`; 48 FL controls are slot parameters as
  `(raw − min)/(max − min)`: the 8 globals and, per unit, cutoff, resonance, envelope amount, pan and volume — FL
  automation of these is imported (index map `paramOfFlIndex`), others are reported as unsupported. LYNX's transport
  maps to FL's: a moving song position is "playing", a non-contiguous one a jump; exports use FL's rendering mode
  (`offlineRendering()`), as every exact engine does.
- `core/FruityVocoderFx.{h,cpp}`: the vocoder (§6), `EffectKind::fruityVocoder`, FL's twelve controls as slot
  parameters (automation imported).
- `core/DelphiMath.h`: §7.
- Import: `effectFromFl` routes both plugins to the exact engines (inside Patcher graphs too); the Love Philter
  conversion of rounds 16–20 and the generic vocoder remain only for states the engines cannot read.

---

## 10. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Love Philter LYNX C++ vs DLL, music signal 2 s, 44.1 kHz / 512, 120 BPM | 86 states × live / export mode | all 172 bit-identical |
| The same with the Windows build (zig, MinGW) and with clang on Linux | 86 × 2 | all bit-identical |
| Static filters: every type × mode 0–5 × FLAT, L/B/H gains, drive | 144 | all bit-identical |
| Band-pass sweep (cut 0…256 × res 0…256, export mode) | 153 | all bit-identical |
| Envelope curve modes 0–12, tensions 0 / 0.4 / −0.6 | 15 | all bit-identical |
| Automation (6 sets) | 20 states × 2 modes | all 240 bit-identical |
| Transport (song position 0 / 7.3 / 33 beats) | 20 states × 2 modes | all 120 bit-identical |
| Rates / blocks / tempo (48 k/300/133, 96 k/1000/export/87, 22.05 k/77/160, 44.1 k/512/export/140) | 86 states × 4 | all 344 bit-identical |
| Fruity Vocoder LYNX C++ vs DLL | 3 installation + 6 synthetic states × 44.1/48/96 kHz; 2 automation sets; 192 kHz | all bit-identical (Linux and Windows builds) |
| Delphi runtime math | §7 | all bit-identical |
| Unit tests `love_philter_engine`, `fruity_vocoder_engine` | goldens of 3 + 2 synthetic states, index maps, fallbacks | bit-identical on the test machine |

---

## 11. The STATUS gaps

- *Mango Hz inferred from its pole*: closed — the pole is run as the plugin runs it (§4.1).
- *Cutoff/resonance modulation reaches the EQ one block late*: closed — the articulators drive the unit's coefficients
  at the plugin's own control tick, with its ramps (§3, §4.2).
- *Pulse curves with more than 10 cycles*: closed — every curve mode is evaluated exactly (§3.2).

## 12. Open items

- What FL sends when an export starts (a flush message would make the first tick's ramps start on their targets)
  is not observed; LYNX starts like a freshly created plugin.
- IPP's `AddProductC` is fused on CPUs with FMA (the reference machine); on a CPU without FMA FL's result would differ
  in the last bit for the Chocolate band/high gains — LYNX always fuses.
- The Vocoder's 64-bit denormal test reads one value past the band buffer at the end of a block (FL's output buffer);
  LYNX assumes +0 there.
- A tempo change while an envelope segment runs keeps that segment's rate until its end, as in the plugin; LYNX sends
  tempo changes at block boundaries.
- Love Philter's MIDI note pattern switching (*Note input*) and the editor-only options are not modelled (LYNX
  effects receive no MIDI).

---

## Evidence index

| Item | Functions / data | Kind |
|---|---|---|
| Render order, segments, silence, oversampling call | `FUN_0085aa10`, `FUN_00629bc0`, `FUN_0083aa90`, `FUN_0083ab30` | DECOMP |
| Tick, scales, smoothing | `FUN_00856690`, `FUN_00856570`, `FUN_00854e20`, table `0x8D1A08` | DECOMP + EXEC (field dumps) |
| Rate object, max step | `FUN_0062b030` | DECOMP + EXEC |
| Articulator combination, depths | `FUN_008543d0`, `FUN_00789a80`, `FUN_00858af0`, `FUN_00858e60` | DECOMP |
| Envelope library (load, prepare, step, position, maps) | `FUN_007800e0`, `FUN_00783ec0`, `FUN_00783380`, `FUN_00786350`, `FUN_00787020`, `FUN_00787ba0`, `FUN_00787fd0`, `FUN_00785f40` | DECOMP |
| LFO | `FUN_007886b0`, `FUN_00788830`, `FUN_00788b70`, `FUN_00788aa0`, `FUN_00788b00`, `FUN_0060e450`, `FUN_0060e610`, `FUN_0060e870` | DECOMP |
| Follower | `FUN_0085a990`, `FUN_0085a910`, `FUN_00854cc0`, `FUN_0085ca60`, `FUN_0085d110` | DECOMP + objdump |
| Transport | `FUN_0085cc80`, `FUN_0085cfa0`, `FUN_0085d050`, dispatcher | DECOMP + EXEC |
| Unit set-up and coefficients | `FUN_0085d5a0`, `FUN_00859100`, `FUN_006249d0` | DECOMP |
| Kernels | `FUN_006257c0`, `FUN_006258c0`, `FUN_006259e0`, `FUN_00625b00`, `FUN_006251f0`, `FUN_006261c0`, `FUN_00625ea0`, `FUN_00628020`, `FUN_00627fc0`, `FUN_00627fe0` | objdump |
| Gains, ramps, mixing | `FUN_00616570`, `FUN_006164e0`, `FUN_00629660`, `FUN_00628930`, `FUN_00629520`, `FUN_00628760`, `FUN_00628ea0`, `FUN_0062aee0`, `FUN_00629090`; IPP pointers read at run time | DECOMP + objdump + EXEC |
| Waveshaper | `FUN_0085c340`, `FUN_0062a380/a400/a550/a5d0`, control words `0x8A37B2/0x8A37B8` | objdump + DATA |
| Pan law | `FUN_00616620`, `FUN_0062afb0` | DECOMP + objdump |
| Oversampler | `FUN_0083a560`, `FUN_0083a730`, coefficients at `*0x8E6010` | DECOMP + EXEC |
| CENTER | `FUN_006281f0`, `+0x16D8/+0x16DC` | objdump + EXEC |
| Vocoder | `FUN_006d0fb0`, `FUN_006d03a0`, `FUN_006cff90`, `FUN_006d00b0`, `FUN_006cff40`, `FUN_006cfef0`, `FUN_005dbb00`, `FUN_005db530`, `FUN_005dbc10`, `FUN_006d0e70`, `FUN_005df640`, `FUN_005df500`, `FUN_006cf810`, `FUN_006d08c0`, dispatcher `FUN_006cf460` | DECOMP + objdump + EXEC |
| Runtime math | §7 | objdump + EXEC |
| States | `data/states66.pkl`, `data/presets66.pkl` | DATA |
