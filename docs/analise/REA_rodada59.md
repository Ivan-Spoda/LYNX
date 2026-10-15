# Behavioural analysis of FL Studio 2026: round 59 (REA) — Fruity Chorus, Hyper Chorus and Vintage Chorus

> **Nature of this document.** First decoding of FL's three chorus plugins, which LYNX imported until now as its
> generic chorus at default settings (the saved controls were never read). *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts, formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the repository. The two C
> runtime routines LYNX re-creates (§2.5, §3.7) are Microsoft's math library as statically linked into the engines;
> they are described by their behaviour (reduction, polynomial degree, rounding) and rebuilt from their constants.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r59/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Fruity Chorus/Fruity Chorus_x64.dll` | `da668779ffb10759805242f474cb04ab739fc13318c21dfd6b45c11b218e505a` | Delphi plugin with its own DSP |
  | `Plugins/Fruity/Effects/Hyper Chorus/Hyper Chorus_x64.dll` | `a68175c894e3927063d2cb36f900efd134fdf732ff7776be23bc559824317be9` | Delphi front-end (no DSP) |
  | `Plugins/Fruity/Effects/Hyper Chorus/Engine_x64.dll` | `8f3192951bb0c11e19118d5b7f55bf8aed4eb9fe810df38f4897ed764961bf96` | C++ engine (`QuadrachorEngine`, export `DispatchToEngine`) |
  | `Plugins/Fruity/Effects/Vintage Chorus/Vintage Chorus_x64.dll` | `df080f8d3de28da9e9396706490640966f3155702d36bb9245c1594c83eecf42` | Delphi front-end (no DSP) |
  | `Plugins/Fruity/Effects/Vintage Chorus/engine_x64.dll` | `4767b70609d9af224d44a8ab71c19606d9232bbecad06a6761618e61bc0c2e76` | C++ VST2 engine (class `TestPlug`, export `VSTPluginMain`) |

- **Tools:** headless Ghidra 12.1.4 (projects `~/ghproj/r59`, `gh/scripts/DecRange.java`; decompilations in
  `out/fc_main.c`, `out/hc_main.c`, `out/hce_all.c`, `out/vc_main.c`, `out/vce_all.c`), `objdump -M intel`
  (`out/*.asm`, `py/dis.sh`) wherever the operation order matters — in this round the decompiler's association of
  sums was wrong in places (§3.4), so every expression LYNX uses was read in the disassembly.
- **EXEC harnesses** (zig cc 0.14.1, x86_64-windows-gnu; unmodified DLLs, `SetErrorMode`, vectored fault handler,
  `TerminateProcess`, `timeout`):
  - `run/fx.c` (round 55's effect harness: the Delphi plugin with its own SaveRestoreState, dispatcher, ProcessParam
    and Eff_Render), extended with `poke=` (write an int into the instance after the set-up, used to fix Fruity
    Chorus's random LFO phases), `pre=1` (sample rate and block size before the state), `preinst=k` (k instances
    created first) and `md=` (dump memory of a loaded module, used for the engines' runtime constants and CPU flags);
  - `run/vh.c` (round 29's VST host, rewritten): Vintage Chorus's `engine_x64.dll` directly through `VSTPluginMain`,
    with a time-info answer (tempo, song position, "playing" flag) and parameter events;
  - `run/sc.c` and `run/crt.c`: call one function of an engine (by RVA) on a file of inputs under a chosen MXCSR —
    used to identify and re-create the engines' math-library routines.
- **Drivers (our own code):** `py/survey59.py` (every chorus mixer state of the installation → `data/states59.pkl`),
  `py/fx.py`, `py/fcx.py`, `py/vcx.py` (runners, test signals, error measure), `py/fcmodel.py` (Fruity Chorus model),
  `py/t_fc.py`, `py/t_tab.py`, `py/t_cpp_fc.py`, `py/t_hc.py`, `py/t_hc2.py`, `py/t_crt.py`, `py/t_crt2.py`, `py/t_pow.py`,
  `py/t_vc.py`, `py/t_vc2.py`, `py/t8.py` (models / LYNX's C++ vs the DLLs), `cpp/fxcmp.cpp` (LYNX's three engines on a
  float file, built for Linux and with zig for Windows), `cpp/hcdbg.cpp`, `cpp/crtdbg.cpp` (internal probes),
  `py/gold59.py` (goldens of `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, 44.1 kHz, blocks of 512 unless stated;
"−∞" = bit-identical.

**Installation usage [DATA]:** Fruity Chorus in 20 mixer slots (16 in *Demo projects*, 6 + 3 files), 11 distinct
states; Hyper Chorus in 20 slots (all in *Demo projects*, 7 files), 16 distinct states; Vintage Chorus in 12 slots (11
in *Demo projects*, 4 + 1 files), 11 distinct states.

---

## 1. Fruity Chorus

### 1.1 State and controls **[DECOMP `FUN_006c06a0`, `FUN_006c0a20`, `FUN_006c1360` + EXEC]**

- State: i32 version (FL writes 1; **a version above 1 is not read**), then 12 × i32 controls; after loading, every
  control is sent again through ProcessParam in index order.
- Controls (min 0; max, default) **[EXEC: control objects]**: delay (1024, 512), depth (5000, 2250), stereo (1024, 341),
  LFO 1/2/3 frequency (5000; 1500, 2500, 3500), LFO 1/2/3 waveform (1024; 23, 0, 0), cross type (**100**, 0), cross
  cutoff (5000, 2500), wet only (1024, 0).
- Value `x = float(clamp(double(raw)·(1/max), 0, 1))`, then (Delphi `Trunc` everywhere; `fs` = the sample rate the
  plugin holds **when the control is set** — a later sample-rate change does not recompute anything):
  - delay `D = Trunc((x·29.95 + 0.05)·fs·4.096)` (a delay in ms, 12 fractional bits);
  - depth `W = Trunc(double(float(float(x·5)·fs))·4.096/64)`;
  - stereo `S = Trunc(double(float(x·4096))·0.5) − 1` (an LFO table offset, −1 for x = 0);
  - LFO k frequency: phase increment `Trunc(float(float(x·x·83886080)/fs))` on a 2²⁴ cycle (0…5 Hz, quadratic);
  - LFO k waveform: table `Trunc(float(x·3))` (0…3);
  - cross type: the delay lines hold the low band when `x > 0.25`, otherwise the high band;
  - cross cutoff: `f = float(440·2^((127x − 69)/12))` (Delphi `Power`), limited to `fs/4.1`; `ω = f/fs·6.283184`;
    `a = float((1 − sin ω)/cos ω)`, `b = float(double(1 − a)·0.5)`;
  - wet only: no dry signal when `x > 0.25`.

### 1.2 LFO tables **[DECOMP `FUN_006c18e0` + EXEC: tables read from the running DLL, `py/t_tab.py`]**

Four tables of 4096 integers (0…32766), `value(v) = Trunc(float(float(v + 1)·16383))`, with `t = float(i/2048)`:
0 parabola `4t(1 − t)` (t < 1), `−4(t − 1)(2 − t)` (t ≥ 1); 1 its cube; 2 `(sin(3.1415t) + (sin(9.4245t) +
sin(6.283t))/3)·3/(2√2 + 1)` (double, then float); 3 its cube. Rebuilt with these formulas, all 4 × 4096 entries equal
the DLL's.

### 1.3 Render **[DECOMP `FUN_006c1dd0`, objdump]** — SSE, float unless stated, FTZ/DAZ (MXCSR `0x9FE0`)

Per frame: `x = float(double(in)·0.7)` per channel. Three LFOs, phase `p_k` (32 bits): left value `T[w_k][(p_k ≫ 12) &
4095]`, right value at index `+ S` (mod 4096); offsets `o = (value·W) ≫ 9` (32-bit unsigned product); then each phase
advances. A one-pole low-pass per channel, `s = b·(x + x_prev) + a·s`; the line (4096 floats per channel) receives `s`
(cross type "low") or `x − s`. Write position `w`; tap position `u = w·4096 − D − o` (unsigned, 12 fractional bits):
`v = float(double(L[j]) + double(L[j+1] − L[j])·frac·2⁻¹²)` with `j = (u ≫ 12) & 4095`. Left = `(v₁ + v₂) + v₃` of the
three left taps (right likewise), `w` advances, output `float(double(sum)·0.6) + x` (without `+ x` in wet-only mode).
At the end of each render call a filter state below 10⁻¹¹ in magnitude becomes 0.

### 1.4 Start phases **[DECOMP `FUN_006c17f0` + EXEC]**

At creation and at the "enabled" notification (dispatcher 11, value ≠ 0) the filter states and lines are cleared and
each LFO phase is set to `Random(32767)` — the Delphi RTL's generator, seeded from the clock (`Randomize`): three runs
of the harness gave three different sets **[EXEC]**. The phases are at most 32766/2²⁴ of a cycle (index 0…7 of 4096),
so the effect is small but makes FL's own output differ from run to run. LYNX starts them at 0.

---

## 2. Hyper Chorus

### 2.1 Front-end ↔ engine **[DECOMP `Hyper Chorus_x64.dll` `FUN_006ea220`, `FUN_006e8590`, `FUN_006e90e0`, `FUN_006e9f40` + EXEC]**

The Delphi plugin loads `engine_x64.dll` from its folder and talks to it through `DispatchToEngine(op, engine, a, b,
float)`: 0 create, 1 destroy, 2 sample rate, 3 set parameter (index, value), 4 get, 5 render (interleaved in → out,
n frames), 6–8 texts, 9 reset. ProcessParam sends `x = float(clamp(double(raw − min)·(1/(max − min)), 0, 1))`;
Eff_Render only calls op 5 (and feeds an oscilloscope buffer). The "disabled" notification (dispatcher 11, value 0)
sends op 9. State: i32 version (FL writes 2; **3 and above are not read**), 11 × i32 controls, 1 × i32 of an editor
control (−120…120, no DSP).

Controls (min…max, default) **[EXEC]**: delay, feedback, mod rate, mod amount, movement, movement speed (0…1000 each;
400, 500, 400, 400, 300, 300), ArachnaChor mode (0…1, 0), low-pass (7140…22000, 22000), high-pass (120…10900, 120 =
off), dry/wet (0…1000, 500), LFO phase difference (0…1000, 100).

### 2.2 Engine rendering mode **[DECOMP `DispatchToEngine` + EXEC]**

Op 5 ORs `0xE040` into MXCSR for the render: flush-to-zero, denormals-are-zero **and rounding toward zero**. Every
float/double operation of the render — and of the parameter changes, which are applied inside it (§2.3) — rounds
toward zero.

### 2.3 Parameter changes **[DECOMP `FUN_180002290`, `FUN_180001740`, vtable `0x180022bd0`]**

Op 3 stores the value and enters it in a "smoother" list `{index, current, target}`: a new entry starts with current =
the new value; if the index is already listed, only its target changes. At every rendered frame each entry (last to
first) moves `current += (target − current)·c` and is applied, or, once `|target − current| < 10⁻⁴`, the target is
applied and the entry removed. `c = float(1 − 0.09999999999999998^(1/(fs·0.05)))`. So a change is applied at the next
frame, and two changes of the same parameter before a render glide from the first to the second. **FL's load does
this:** the front-end sends its defaults when it is created, then the state, so a freshly loaded instance glides from
the defaults to the saved values over ~2 000 frames **[EXEC]**.

Applying a value (`FUN_180001ee0`; all float):
- 0…5, 10 go to both chorus cores (§2.4). Per voice `v` of a core (core 1 = rows 0–3, ArachnaChor core = rows 4–7 of
  the table below), with `L(a, b) = (b − a)·x + a`: delay `d = (L(Dmin, Dmax)·0.001)·fs` samples; feedback `x·0.7`;
  mod rate increment `L(Rmin, Rmax)/fs`; mod amount `L(20, 80)²·5·10⁻⁵`; movement amount `x·0.499`; movement speed
  increment `L(Mmin, Mmax)/fs`; phase difference `(x·x)·x·0.25`.

  | Voice | delay ms | mod rate Hz | movement Hz |
  |---|---|---|---|
  | 1 | 2.0…6.0 | 1.5…2.47 | 0.33…3.56 |
  | 2 | 3.5…8.25 | 1.57…3.41 | 0.29…2.66 |
  | 3 | 6.5…10.0 | 0.38…3.64 | 0.26…3.78 |
  | 4 | 5.0…12.8 | 1.29…3.68 | 0.14…2.98 |
  | 5 | 7.1…16.6 | 3.41…5.03 | 0.44…5.06 |
  | 6 | 9.1…15.1 | 2.47…5.37 | 0.66…8.08 |
  | 7 | 8.3…16.8 | 3.01…6.02 | 0.93…6.02 |
  | 8 | 9.6…17.7 | 3.3…6.5 | 0.41…10.22 |

  **[DATA `.rdata 0x18002ba10`]**
- 6: ArachnaChor on when `x ≥ 0.5`.
- 7: low-pass at `f = powf(2, 4.9454355·x)·714` Hz; 8: high-pass at `powf(2, 6.5051498·x)·12` Hz (off when the
  control is at its minimum, §2.4); both RBJ second-order sections with `α = sin ω` (Q = ½), designed in double
  (`ω = (f·2π)·(1/fs)`, `1/(1 + sin ω)`) and stored as float, feedback coefficients negated.
- 9: dry/wet `g = 0.5·(2x)²` (x < ½) or `1 − 0.5·(1 − (((x − ½) + x) − ½))²`; wet `√g`, dry `√(1 − g)`.

### 2.4 Render **[DECOMP `FUN_180001740`, `FUN_180002920`, `FUN_180001d20`, objdump]**

A core has four voices per channel; voice `v` of the left channel and voice `v` of the right share delay, feedback and
the two LFOs. Per frame and voice (float, toward zero):
- parabolic LFO `P(t) = (a − a²) − (b − b²)`, `a = min(t, 1)`, `b = max(t, 1) − 1` (range ±¼) on doubled phases;
  movement `m = (P(2φ_V)·4)·movementAmount + 0.5`; delays `d_L = (P(2φ_M)·4)·(d·amount) + d`, `d_R` the same with the
  phase `frac(φ_M + phaseDifference)`;
- the left line of the voice receives `(√(1 − m)·1.41442716)·inL + feedback·out_L(previous)`, the right one
  `(√m·1.41442716)·inR + out_R(previous)·feedback` — the movement pans each voice between the channels;
- read (lines of 65536, 16-bit wrap): `n = trunc(d)`, `y₀…y₃` = the samples `n … n + 3` frames back (the frame just
  written counts as 0), `f = d − n`, `c = (y₂ − y₀)/2`, `A = y₁ − y₂`, `B = A + c`, `D = (y₃ − y₁)/2 + (B + A)`,
  output `((f·D − (D + B))·f + c)·f + y₁`;
- the phases advance and wrap (`(inc + φ) − trunc(inc + φ)`).

Engine per frame: the smoothers (§2.3); core 1; `L = ((o₀ + o₁) + o₂) + o₃` (right from voices 4…7) × 0.56234097, or
with ArachnaChor `L = (((L₁ + c₃) + c₂) + (c₁ + c₀))·0.28117049` adding the second core; the low-pass; then the
high-pass: while the high-pass control is above its minimum, a cross-fade counter rises to `N = trunc(fs·0.01)` frames
mixing `x·(1 − k/N) + hp(x)·k/N`, then the high-pass alone; when switched off it falls back the same way (the filter
states keep running only while it is in use); output `in·dry + y·wet`. Op 9 clears both filters, the counter, and
**only the first core** (its lines, outputs and write position).

### 2.5 The engine's math library **[DECOMP `FUN_1800163d0`, `0x180016420`/`0x180016aa0`, `FUN_180018d60` + EXEC `run/sc.c`]**

Because the parameter changes run inside the toward-zero render, the library results LYNX must reproduce are those of
Microsoft's routines *in that mode*:
- **sine/cosine** (`FUN_1800163d0`): below 2⁻²⁷ returns `(x, 1 − x²/2)`; otherwise the vector sine of `x` and of
  `|x| + π/2`: `n` from `|x|·(1/π) + 1.5·2⁵²` (the add rounds in the current mode, so toward zero it truncates), `r =
  |x| − n·π` with π in four parts, `r` negated for odd `n`, `r + r·(r²·Q(r²))` with a degree-6 polynomial in `r²`,
  sign of `x`. Toward zero this gives errors up to 2·10⁻⁷ near π **[EXEC]** — real, and audible as coefficient
  differences of one float step. The SSE2 and AVX code paths (CPU dispatch) compute the same.
- **powf(2, e)** (`FUN_180018d60`): `t = e·ln 2` (double), `N = round_current(t·64/ln 2)`, `r = t − N·(ln 2/64)`,
  `q = r²·(r/6 + ½) + r`, result `q·T[N mod 64] + T[N mod 64]` scaled by `2^(N/64)` (the table `T[j] = 2^(j/64)`,
  correctly rounded — all 64 entries checked), converted to float in the current mode.
- LYNX's re-creations match the DLL's routines on 38 006 sine/cosine arguments in three MXCSR modes and 45 002 powf
  arguments in two modes, all bit-identical **[EXEC, `py/t_crt.py`]**; this machine's CPU takes the FMA dispatch of
  powf (flag `0x18002ce10` = 1) and the AVX sine (`0x18002b020` = 5), with the same results.

### 2.6 LFO start phases **[DECOMP `FUN_180002440`, `FUN_1800023c0` + EXEC]**

Each core draws its 4 mod phases then its 4 movement phases from the C runtime's `rand()`
(`s = s·214013 + 2531011`, value `(s ≫ 16) & 0x7FFF`, phase `float(value·(1/32767))`), the per-thread state of the
engine's statically linked runtime, seed 1: instance k created in the process takes calls `16k + 1 … 16k + 16`
**[EXEC: 0, 1 and 3 earlier instances, `preinst=`]**. LYNX numbers the Hyper Chorus slots of an imported project in
mixer order (inference: FL creates them in that order on one thread — not confirmed).

---

## 3. Vintage Chorus

### 3.1 Front-end ↔ engine **[DECOMP `Vintage Chorus_x64.dll` `FUN_006e89a0`, `FUN_006e9010`, `FUN_006ea9d0` + EXEC]**

As for Vintage Phaser (round 29): the Delphi plugin hosts `engine_x64.dll` as a VST2 plug-in; ProcessParam sends
`setParameter(i, float(clamp(double(raw − min)·(1/(max − min)), 0, 1)))` with the same index; Eff_Render
de-interleaves, calls `processReplacing`, re-interleaves. State: i32 version (FL writes 1; 2 and above not read), 17 ×
i32 controls. Front-end and engine driven directly give identical output on all 11 states **[EXEC, `py/t8.py`]**.

Controls (min…max, default) **[EXEC]**: mode 0…2 (0: Chorus 1, 1: Chorus 2, 2: custom), link Chorus 1&2 0…1, LFO
speed 1…245760 (49152), LFO wave 0…1, LFO L/R phase 0…6000 (3000), delay 1/2 0…6000 (60, 3000), wet/dry 0…6000 (3000),
mono input, wet polarity, feedback 0…6000 (0), wet HP 0…6000 (3000), gain 0…6000 (0), hiss 0…6000 (3000), noise gate,
LFO sync, LFO start phase 0…6000 (0).

### 3.2 Parameters (`setParameter` `FUN_18001b390`, `FUN_180019680`) **[DECOMP + EXEC]**

- Mode `trunc((x₀·3)·0.9999)`. **Modes 0 and 1 are fixed settings** (the controls of the custom mode are ignored):
  with link off a triangle LFO at 0.4 Hz (Chorus 1) or 0.6 Hz (Chorus 2), L/R phase ½, delays 1.08…5.23 ms; with link
  on a sine LFO at 8 Hz, L/R phase 0.0286, delays 2.85…3.37 ms; in both, mix ½, polarity +1, no feedback, wet
  high-pass 0.5 Hz, no sync (the exact constants are float bit patterns in the code). Mode 2 re-applies controls 2…16.
- Custom mode: LFO speed `(x²·10)/fs` per frame (0…10 Hz); LFO wave sine when `x ≥ ½`; L/R phase `x` cycles; delays
  `(x·9.8 + 0.2)·0.001` s; mix `x`; polarity −1 when `x ≥ ½`; feedback `x·0.63`; wet high-pass `x²·160 + 10` Hz;
  sync on when `x₁₅ ≥ ½`, period `trunc(float(float((2^((1 − x₂)·7) − 1)·1.0078740119934082)·48·6143)/6144 + 1.1)/48·¼`
  beats.
- Any mode: mono input `x ≥ ½`; gain `10^((x·12)/20)` (0…12 dB); hiss level `0.2·10^((x·50 − 90)/20)` (0 = off) and
  gate threshold `8 × level`; noise gate `x ≥ ½`; start phase `x`.
- `setSampleRate` re-runs the core set-up (§3.3) and re-applies all 17 parameters.

### 3.3 Core set-up (`FUN_1800192e0`, `FUN_180019e00`) **[DECOMP + objdump]**

Smoothing `k = float(1 − 0.09999999999999998^(1/(fs·0.05)))`; `π/fs` as `float(1/fs)·π`; input high-pass one-pole
(`t = tan(f·π/fs)`, `b₀ = 1/(t + 1)`, `b₁ = −b₀`, `a₁ = −(t − 1)/(t + 1)`), 50 Hz until the wet-HP control sets it;
output low-pass one-pole at 7 kHz (`b₀ = b₁ = t/(t + 1)`); two identical second-order low-passes at 7 kHz
(`ω = 43982.297/fs`, `α = sin ω/1.414`, RBJ, float) — the bucket-brigade anti-alias filters.

### 3.4 Render (`FUN_18001a950`, core `FUN_1800186a0`) **[DECOMP + objdump]** — FTZ/DAZ, round to nearest

Per frame, per channel unless stated:
1. mono: both inputs `(L + R)·½`; song position / sync: when sync is on, at the first non-negative position after a
   negative one (the engine starts at −1) the phase is set to `frac(float(ppq/period) + startPhase)` at every frame,
   and the increment is `float((tempo/60)/period)·(1/fs)`;
2. LFO from phase `φ` (left) and `frac(φ + L/R phase)` (right): sine `sinf(2πφ)·½ + ½` or triangle (`2φ + ½`,
   `(1.5 − φ) − φ`, `(φ − ¾) + (φ − ¾)`);
3. smoothed delays `d ← ((spread·lfo + d₁) − d)·k + d`;
4. `x = feedback·out_prev + in`; `φ` advances; high-pass → low-pass → low-pass (biquads computed as
   `(((c₁x₁ + c₀x) + c₂x₂) + c₃y₁) + c₄y₂`);
5. line of 32768, read `d·fs` frames back with the same four-point interpolation as Hyper Chorus (§2.4) — here too
   `D = (y₃ − y₁)/2 + (B + A)` although the decompiler showed `((y₃ − y₁)/2 + B) + A`; the result is `out_prev` for the
   next frame;
6. hiss (when on): two 32-bit LCGs (`s·0x0BB38435 + 0x3619636B`, ×2⁻³¹, same as Vintage Phaser), each through a peaking
   filter redesigned every 16 frames from the LFO (`ω = 40840.707/fs`, `2 sin ω` divided / multiplied by
   `2·lfo + 1`), × level, added;
7. 7 kHz one-pole; mix and gain smoothed by `k`; `y = ((wet·√m)·polarity) + in·√(1 − m)`; the gate (hold 440 frames
   at gain 1 whenever |y| ≥ threshold on either channel, then × `0.1^0.000151…` per frame); `× gain`.

A fresh instance starts with mix and gain at 0 and delays at 10 ms and glides to its settings **[DECOMP + EXEC]**.

### 3.5–3.6 Noise seeds **[DECOMP `FUN_180019770` + EXEC]**

The two generators start at `G` and `G + 0x1099E`, where the global `G` starts at 333 784 and advances by `0xB66AD` per
engine instance created in the process (as in Vintage Phaser). LYNX numbers Vintage Chorus slots like Hyper Chorus's.

### 3.7 The engine's sinf **[DECOMP `FUN_18001e210`, FMA variant `0x18001e5b0` + EXEC]**

The sine LFO calls the runtime's `sinf`, which takes an FMA code path when the CPU has FMA (flag `0x180032cd0`): in
double, `|x| ≤ π/4` → odd degree-9 polynomial (fused multiply-adds); otherwise `n = trunc(fma(|x|, 2/π, ½))`,
`r = fma(−n, π/2_hi, |x|) − n·π/2_lo`, the sine or cosine polynomial by the parity of `n`, sign from `n mod 4` and the
sign of `x`; tiny arguments returned as they are. Against the correctly rounded sine it differs on 269 of 200 000 LFO
arguments **[EXEC, `py/t_crt2.py`]**, so LYNX re-creates it (hardware FMA when the CPU has it). Its `tan`, `sin`,
`cos` and `pow` (set-up and hiss filters only) give the same float results as the correctly rounded functions on
50 000 arguments each, so LYNX uses the C library's.

---

## 4. What LYNX does

- **`EffectKind::fruityChorus`, `hyperChorus`, `vintageChorus`** (`core/ChorusFx.{h,cpp}`): mixer effects whose
  parameters are FL's controls over their ranges, so the LYNX editor and automation can change them; FL automation of
  the controls maps one to one (`effectParamFromFl`).
- Fruity Chorus: the tables and render of §1 in SSE float/double under FTZ/DAZ; the rate-dependent values are computed
  for LYNX's rate; start phases 0 (§1.4).
- Hyper Chorus: the engine of §2 under MXCSR `0xE040` for the render, the smoother list, the re-created sine/powf of
  §2.5; a LYNX instance applies its parameters without the load glide (FL's mixer runs long before playback); the
  instance number (EffectSlot::state) selects FL's rand() phases.
- Vintage Chorus: the engine of §3 with the re-created sinf; `settle()` gives a new or reset LYNX instance the state a
  long-running FL instance has (smoothed values at their targets); song position and tempo come from the transport for
  LFO sync.
- States FL itself does not read (higher versions) or too short ones keep the generic chorus and the import report
  lists them.

---

## 5. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Fruity Chorus LFO tables (formula vs DLL) | 4 × 4096 | all identical |
| Fruity Chorus model vs DLL (phases fixed with `poke=`) | 11 demo states × music 44.1/512, bursts 48/300, saw 96/1000 | all 33 bit-identical |
| Fruity Chorus LYNX C++ vs DLL | 15 states (11 demo + 4 synthetic) × 4 rate/block sets × with/without automation × rate before/after the state | all 240 bit-identical |
| Hyper Chorus LYNX C++ vs DLL (FL's load sequence, glide included) | 16 demo states × 3 rate/block sets | all 48 bit-identical |
| Hyper Chorus: instances 0/1/3, 14-event automation (incl. ArachnaChor and HP toggles), 4 synthetic states | 60 | all bit-identical |
| Engine math routines (re-created vs DLL, `run/sc.c`) | sine/cosine 38 006 × 3 MXCSR; powf 45 002 × 2 | all identical |
| Vintage Chorus front-end vs engine driven directly | 11 demo states | identical |
| Vintage Chorus LYNX C++ vs DLL engine | 11 demo states × 3 rate/block sets, 5 synthetic (modes 0/1 + link, extremes, hiss + gate, mono, polarity) × 3 × with/without 12-event automation, hiss states also as instance 2 | all 96 bit-identical |
| Vintage Chorus LFO sync, transport playing | 2 states × 4 tempo/start positions | all 8 bit-identical |
| LYNX C++ built for Windows (zig, MinGW runtime) vs DLL | 11 + 16 + 11 demo states | all 38 bit-identical (a library `sqrtf` had rounded to nearest inside Hyper Chorus's toward-zero render: LYNX uses the SSE instruction) |
| Unit test `chorus_family_engines` | goldens of 3 + 2 + 3 states, imports, fallbacks | within 2·10⁻⁷ (bit-identical on the test machine) |

Import (`tests.cpp`, demo-project battery): every Fruity / Hyper / Vintage Chorus mixer slot of the installation's
projects becomes the exact effect.

---

## 6. Open items

- Fruity Chorus's start phases are random in FL itself (clock-seeded); LYNX uses 0. The order in which FL sends sample
  rate and state (which decides the rate its rate-dependent values use) is not observed; LYNX uses its own rate.
- Hyper Chorus / Vintage Chorus instance numbering: that FL creates a project's instances in mixer order on one thread
  (which fixes the rand() phases and the hiss seeds) is an inference, not confirmed; instances created earlier in the
  same FL session shift them.
- On a CPU without FMA the engines' library takes its non-FMA paths: for powf the re-creation follows that path's
  formula (and matches the FMA path here); Vintage Chorus's sinf then falls back to the C library's fused multiply-add
  emulation (not verified against the non-FMA variant).
- Vintage Chorus with LFO sync uses LYNX's song position; FL's own time info for a plugin hosted by the front-end
  (which the engine asks for through the front-end) was not observed — the engine's arithmetic was verified with the
  harness's time info.
- Vintage Chorus leaves the SSE denormal flags of the calling thread changed after a render (its `_controlfp_s` resets
  DAZ); this affects only FL's later code, not the chorus.
