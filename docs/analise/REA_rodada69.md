# Behavioural analysis of FL Studio 2026: round 69 (REA) — sample players: FL's interpolators, the sampler's volume envelope, FPC layers, Slicex volume

> **Nature of this document.** Continuation of rounds 3 (time-stretch, channel envelopes) and 9 (state of the
> sample-based generators). *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was
> copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack or encrypted content was touched;
> no Image-Line audio data was copied into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own code, unmodified (**[EXEC]**). Where there was not enough
> evidence, the item is marked **"not confirmed"**. Results of our own implementation are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` files of the installation |
| **[EXEC]** | result obtained by running the installation's own code (without modifying it) |
| **[model]** | output of our own implementation, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r69/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `FLEngine_x64.dll` | `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` | host: native sampler channels, audio clips, unit `DSP_Resample` (`TResampler`) |
  | `Shared/dsp_ippv2_x64.dll` | `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` | Intel IPP build FL loads by name (two routines used by the sinc kernels) |
  | `Plugins/Fruity/Generators/FPC/FPC_x64.dll` | `5990885c6dbf02a6be2e75101a899b8b496846705b8dab5c2dce01bfb9586f04` | FPC (Delphi, carries its own copy of `DSP_Resample`) |
  | `Plugins/Fruity/Generators/Slicex/Slicex_x64.dll` | `401b5ece958913d8fd00bd6cd8defd66c4b624a0702ade13992fc1ff1569857b` | Slicex (Delphi, own copy of `DSP_Resample`) |
  | `Plugins/Fruity/Generators/Fruity Slicer/Fruity Slicer_x64.dll` | `7616d1f88febef330916b5f45e51ba758a7f2666b80718fab321fda546312ed6` | Fruity Slicer (Delphi, own copy of `DSP_Resample`) |
  | `Plugins/Fruity/Generators/DirectWave/DirectWave_x64.dll` | `3432775dd4e06b333fb04f210d834fb02d91932d702a3699b4c180501a746570` | DirectWave (C++/MSVC) |

- **Tools:** Ghidra 12.1.4 headless (project `~/ghproj/r69`, programs imported without auto-analysis, script
  `gh/scripts/DecAt.java` that creates the function at an address before decompiling it; `gh/g.sh`, `gh/gp.sh`);
  `objdump -M intel` where the operation order matters (`gh/fle.asm`, `dll/*.asm`); our PE reader `py/pe.py`.
- **EXEC harness** `run/kern.c` → `run/kern.exe` (Zig 0.14.1, x86_64-windows-gnu, our own code): maps
  `FLEngine_x64.dll` unmodified with `DONT_RESOLVE_DLL_REFERENCES` (DllMain never runs, as in round 43), points the two
  run-time IPP slots of the sinc kernels (`0x15f3830` sine, `0x15f3c60` threshold) at the installation's
  `dsp_ippv2_x64.dll` exports, builds a minimal resampler object (half-width, cutoff fields, fraction and frame-pointer
  arrays, scratch) and calls one interpolation kernel directly; vectored fault handler, `TerminateProcess` at the end.
  `kern.exe cpu` runs FLEngine's CPU probe (`0x63a330`) and prints its feature record.
- **Models:** `cpp/model69.c` (C reference of the three kernel families), `cpp/rscheck.cpp` (LYNX's port run on the
  same inputs), drivers `py/kt.py` (matrix), `py/nearone.py`, `py/gold.py` (test goldens).

---

## 0. Data survey **[DATA, `py/survey69.py`, `py/collect69.py`]**

All 165 `.flp` files of `FL Studio 2026/Data` (channel section):

| Channel kind | Channels | Files |
|---|---|---|
| Native sampler with a sample | 409 | 42 |
| Audio clip channels | 837 | 51 |
| DirectWave | 57 | 18 |
| Slicex | 43 | 21 |
| FPC | 25 | 20 |
| FL Keys | 11 | 9 |
| Fruity Slicer | 10 | 5 |

Stretch modes of the sampler/clip channels (round 3 §5): Resample 855, Auto (offline) 176, real-time Stretch 119,
others ≤ 20 each. Sampler channels with an enabled envelope: volume 96, cutoff 8, pitch 5. The native sampler and the
audio clips are therefore the sample players that matter most; both run FLEngine's interpolators on every pitched or
rate-converted note.

---

## 1. FL's interpolators (`DSP_Resample`, `TResampler`)

### 1.1 Quality index → kernel **[DECOMP `FUN_0067d410`, `FUN_0067d320`, `FUN_0067d4f0`, table `0x13eb808`]**

An interpolation index 1…9 selects a half-width `N` from a table: 1 → 1 (2-point linear), 2 → 3 (6-point polynomial,
FL's "6-point Hermite"), 3 → 8, 4 → 12, 5 → 16, 6 → 32, 7 → 64, 8 → 128, 9 → 256 (windowed sinc with `2N` points:
16, 24, 32, 64, 128, 256, 512). Index 10 is a special offline path (§1.7). Widths below 3 select the linear kernels,
3 the 6-point kernels, larger ones the sinc kernels; widths are capped at 512.

The kernel is one of twelve functions chosen by kind × channel count (1/2) × in-memory sample format (0 = 16-bit
integer, 1 = 32-bit float; the sample object keeps 16-bit files as integers) — table `0x13e9f30 + 0x20·kind + 0x10·ch +
8·fmt` **[DECOMP]**. A second table (`0x13e9f90…`) holds double-precision variants selected when byte `+0x16` of the CPU
record `0x15f32e0` is set; the CPU probe leaves it 0 on the test machine (`kern.exe cpu`) **[EXEC]**; what else sets it
is **not confirmed**.

### 1.2 Defaults **[DECOMP]**

- **Playback:** the unit's start-up code (`0x6a68a8`) applies the index stored at `0x13ec8c8` = **4 → 24-point sinc**
  (`FUN_006a04d0` writes the global default half-width `0x15f499c`, which every new resampler takes). No setting for it
  exists in the user's registry, so this default stands.
- **Render:** the render dialog's settings record starts with `Interpolation` = **5 → 32-point sinc** (`0xedff62`);
  the dialog writes combo index + 1 there and reads/writes it from the registry value `Interpolation` (0…10) with that
  default (`0xeeca4d`). The render driver hands the index to every channel (`FUN_01366a70` → `FUN_0132f8d0`).
- FPC, Slicex and Fruity Slicer carry the same table and their own default byte (= 4, 24-point sinc; FPC `0x8bd248`),
  applied only at unit start-up **[DECOMP]**; whether the host changes it while rendering is **not confirmed**.

### 1.3 Linear and 6-point kernels **[DECOMP + EXEC]**

Both run on the x87 unit with 64-bit precision (FL's control word `0x33F`, `0x13e4d54`).

- **Linear:** `(b − a)·x + a`, `a`/`b` the frames at `i` and `i + 1`, `x` the fraction (a double), one rounding to float
  at the end (16-bit samples: the integer values, then `× 2⁻¹⁵`).
- **6-point:** with `t = x − ½` and the frame pairs `(i−2, i+3)`, `(i−1, i+2)`, `(i, i+1)`, the even powers use the pair
  sums, the odd powers the pair differences (left minus right). The outer pair's sum and difference are rounded to
  float; everything else stays in extended precision. `y = ((((c₅·t + c₄)·t + c₃)·t + c₂)·t + c₁)·t + c₀`, each `c_k`
  formed as outer·w₁ + middle·w₂ + inner·w₃ (left to right) with the weights (float constants at `0x13e9eb4`):

  | power | weights (outer, middle, inner) |
  |---|---|
  | t⁵ (diff.) | −1/24, 5/24, −5/12 |
  | t⁴ (sum) | 1/48, −1/16, 1/24 |
  | t³ (diff.) | 5/48, −11/16, 37/24 |
  | t² (sum) | −5/96, 13/32, −17/48 |
  | t¹ (diff.) | −3/128, 61/384, −87/64 |
  | t⁰ (sum) | 3/256, −25/256, 75/128 |

### 1.4 Windowed sinc **[DECOMP `FUN_00679aa0`/`FUN_006797a0`/`FUN_00679800` and their mono/float siblings; EXEC]**

Weight of the frame `i + m` (m = −N+1 … N) at distance `d = x − m`:
`sin(w·d)/(π·d) · (0.36 + c + 0.64·c²)/2`, `c = cos(π·d/N)`, `w` the cutoff (π, or lower, §1.5).

How it is evaluated (this decides the bits):
1. Fractions below 10⁻²⁵ are raised to 10⁻²⁵ (IPP `ippsThreshold_LT_64f_I`).
2. Per output frame, four arguments in x87 extended precision rounded to double: `π/2 − x·π/N`, that minus `π/N`,
   `x·w`, that plus `w`; their sines come from IPP `ippsSin_64f_A50` (so the window cosines are sines of complements).
3. From each pair of sines, two more are stepped backwards with `s(d−1) = 2cos·s(d) − s(d+1)` in extended precision
   (`2cos(π/N)` and `2cos w` as doubles), and the four values per function are rounded to float.
4. SSE single precision from there: four lanes hold the taps m = 1, 0, 2, −1; `1/(π·d)` is the `rcpps` estimate refined
   by one Newton step (`r·(2 − π·d·r)`, as `(r + r) − (π·d·r)·r`); the window is `(0.64·c)·c + c + 0.36`; each loop
   iteration handles the next four taps outwards (two frames on each side) in two halves, advancing the sines and
   cosines by the float Chebyshev recurrence and the denominators by ±2π (float); products are accumulated per lane.
5. Result = (sum of the two lanes of each channel) × ½ (16-bit samples: × 2⁻¹⁶).

A mono kernel is the left lane of the stereo one (same arithmetic) **[DECOMP]**; 16-bit and float sources give the same
bits for the same values, since the 2⁻¹⁵ scaling commutes with every rounding (checked in §1.8) **[EXEC]**.

### 1.5 Speed, width and cutoff **[DECOMP `FUN_0067d9b0`, `FUN_0067db00`, `FUN_00d26890`]**

- Speed (source frames per output frame) for a pitch in cents and a rate ratio `SR_sample·(1/SR_out)`: exactly the ratio
  at 0 cents, else `exp(cents·k)·ratio` with `k = (ln 2/12)/100` (Delphi `Exp`, `Ln`; `0x15f49d0` set at `0x6a6a0a`).
  With time-stretch the rules of round 3 §5.1 apply.
- When the speed is set and the kernel is a sinc: with `|speed| > 1` the cutoff becomes `w = π/|speed|` and `2cos w` is
  recomputed, else `w = π` and `2cos w = −2`; the half-width becomes `max(base, (round(|speed|) + 3) & ~3)`, capped at
  512 (`round` = banker's rounding), recomputing `π/N` and `2cos(π/N)` when it changes.
- A flag is set when the speed is exactly 1.0 **and** the position has no fraction: the kernel is skipped and frames are
  **copied** (`+0x75`, `FUN_0067ec50`).

### 1.6 Read positions, edges, loops, end **[DECOMP `FUN_0067ec50`, `FUN_0067e4b0`, `FUN_0067d2b0`, `FUN_0067de50`]**

- Position = double; integer part = truncation, fraction = position − integer part. Inside the region (at least `N+2`
  frames from the start and `N+3` from the end for the whole block) positions are `pos + k·speed`
  (IPP `ippsVectorSlope_64f`) and the integer/fraction split is vectorised; near the edges they accumulate `pos += speed`
  per sample.
- Near the edges the `2N` taps are gathered into a buffer: frames before the region start repeat the first frame,
  frames past the region end repeat the last one (a flag `+0x76` would put zeros instead; who sets it is **not
  confirmed**); with a loop, taps past the loop end wrap back by the loop length and, once the voice has looped, taps
  before the loop start wrap forward.
- The voice plays while the integer part of the position is at most the region's last frame; a loop jump is
  `pos = (pos − loopEnd) + loopStart`, counting the passes.
- Region start/end (`FUN_0067de50`): the end defaults to the last frame; the loop points default to the sample's.

### 1.7 Offline resampling (`FUN_00694c80`) **[DECOMP]**

Used by the host to resample a whole sample (e.g. 3x Osc's custom wave, round 47 §2.4, with index 6 = 64-point sinc):
output length `round(len·ratio)`, blocks of 1024. Index 10 additionally pre-filters (FFT-based) and uses the 512-point
kernel. Not used by LYNX.

### 1.8 Verification **[EXEC + model]**

`py/kt.py`: 1500/3000 outputs per case, random 16-bit and float signals (frames ±600 of padding), random fractions plus
special ones (0, 10⁻³⁰, 10⁻¹², ½, 1−10⁻¹⁶, ¼), linear, 6-point and sinc with N = 8, 12, 16, 32, 256, mono and stereo,
`w` = π, π/1.37, π/3.3:

- linear and 6-point, every case: **bit-identical** (C model and LYNX's port);
- sinc with `w = π`: **bit-identical** in every case;
- sinc with `w < π`: bit-identical except at fractions within ~10⁻⁹ of 1 (`py/nearone.py`: 1−10⁻⁵ → 999/1000
  identical, 1−10⁻⁹ → 418/1000, 1−10⁻¹³ → −58 dB): there the stepped sine of the tap next to the position is a
  cancellation of two library sines, so it depends on the last bits of IPP's sine, which LYNX does not use (C library
  sine instead). Such positions need a speed above 1 and land within 10⁻⁹ of a frame — **not reproduced**.

The bits depend on the CPU only through `rcpps`; LYNX uses the same instruction, so on the same machine it matches FL.

### 1.9 LYNX port **[model]**

- `src/core/FlResampler.{h,cpp}`: `flrs::Kernel` (quality → half-width, speed → width/cutoff), `flrs::interpolate`
  (the three kernel families; SSE intrinsics and x87 `long double` with `flvp::X87Extended` in scope; a portable path
  with an exact reciprocal for other CPUs), `flrs::speedFor`.
- The sampler (`SamplerInstrument`, `src/core/Instruments.cpp`) reads every voice through it: 24-point sinc while
  playing, 32-point while exporting (`offlineRendering()` at `prepare`), FL's copy path at speed 1.0 on whole frames,
  edge frames repeated, loop wrapping, the end rule of §1.6, FL's speed formula; reversed voices read the region
  backwards through the same rules. Positions accumulate per sample (FL's block slope differs by < 10⁻¹² frame).
- Tests: `fl_sampler_interpolators_match_fl` (goldens from FL's kernels, `py/gold.py`: linear, 6-point, 24-point,
  32-point at speed 1.37, 16-point widened to 24 at speed 9.5 — all 16 values per case bit-identical),
  `fl_sampler_copies_at_original_rate`.

---

## 2. Sampler voice: how the articulators reach the level **[DECOMP `FUN_00d24ae0`, `FUN_00d24500`]**

Per render call the voice steps its five articulators (`FUN_009ce490`) when the sequencer-tick flag is set (and on the
voice's first call); then: volume = volume envelope value × (volume LFO · LFO amount + 1), pan, cutoff
(`env·amount + lfo·lfo amount`), resonance and pitch in cents (`env·amount + lfo·lfo amount`, the pitch going to the
speed of §1.5). The gain (velocity-scaled volume through the pan law) ramps linearly from the previous tick's value to
the new one over `round(samples per tick)` samples (`FUN_006a3810`). The velocity and pan laws of the host voice are
**not confirmed** in this round (LYNX keeps its own).

## 3. The articulator is the plugins' **[DECOMP `FUN_009ce950`, `FUN_009ce9e0`, `FUN_009ce490`, `FUN_009cedb0`, `FUN_009cecc0`]**

FLEngine's articulator set-up, step, start and release are the same procedures as the plugin voice processor of rounds
42/43 (`flvp::Rec`/`flvp::Art`): time = `round((1001^(v/65536) − 1)·E)` ticks with `E = ((PPQ/4)/48·24576)/1000`
(× BPM/120 when not synced) — round 3 wrote `1000^`; the code uses 1001 — the same states, tension curves (`exp`/`ln`
with `(value, 1/|value|, ln(|value|+1))`), the same end threshold (≈ −144 dB) and the start rule (flag 4 / attack 100
start at 1).

**LYNX [model]:** an imported native sampler channel with an enabled volume envelope keeps the raw 0xDA record and the
project's PPQ (`SamplerSettings::flVolumeEnvelope`, saved in the project); the sampler then drives the level with
`flvp::Rec`/`flvp::Art` stepped once per tick (the voice's first block included) and ramped linearly over the tick,
instead of the ADSR approximation (delay, hold, the three tensions, sustain and FL's tick resolution now apply; the
release starts from the current level). Editing the ADSR in the channel settings drops it. Tick boundaries are counted
from the note start (FL's are the song's), so the steps can sit up to one tick (≈ 5 ms at 96 PPQ/120 BPM) off FL's —
**not reproduced**. Test: `fl_sampler_volume_envelope_steps_per_tick`.

---

## 4. FPC **[DECOMP `FUN_008698a0` (TriggerVoice), `FUN_0086b480`, `FUN_00869db0`; DATA `py/fpcstat.py`]**

- **Velocity index** = banker's `round(velocity · 128)` of the host's velocity, clamped to 0…127; it only selects
  layers (no velocity-to-gain in FPC's trigger).
- **Overlapping layers** (pad flag bit 3): the candidates are the layers covering the velocity, in list order; the pad
  remembers the last chosen layer (`pad+0x49`); modes: 0 random (`floor(rand·count)`), 1 random avoiding the previous
  (`floor(rand·(count−1))`, shifted up by one at or above the previous one), 2 the previous candidate (wrapping), 3 the
  next candidate (wrapping; the first one when the last chosen is not among them).
- Data: all 112 pads with bit 3 use mode 3 (the 8-layer "HQ" kits, up to 3 layers overlapping); pads without bit 3
  never have overlapping layers (so "every covering layer plays" never happens in the demos).

**LYNX [model]:** the zoned sampler picks overlapping zones with mode 3's rule (exact for every demo pad) and FL's
velocity index. Random modes and simultaneous layers: **not reproduced** (no demo uses them). Test:
`fl_fpc_cycle_layers_and_slicex_master_volume`.

## 5. Slicex **[DECOMP `FUN_0087a8a0`, `FUN_0087e4f0` (ProcessParam), `FUN_0087efe0`, voice `0x8843d5`; DATA `py/slicex2.py`]**

- Master parameter 2 (volume, 0…16000) → gain `(exp(x·ln 11) − 1)·0.1` with `x = v·(1/12800)` in float (exactly 1 at
  12800), `ln 11` held as a float; the voice multiplies its volume by it (`+0x1084`). Master parameters 3/4 go through the
  same law with sign; master 5 is the pitch in cents (round 9).
- Deck layout refined: strings are `u8` length (255 → a `u32` length follows); after the 76 parameter bytes come the
  deck's four articulator objects (header `u32 6, u32 3, u32 n`, `n` points of 16 bytes, 36-byte trailer), then
  `u32` count and the 68-byte region records.
- Region records (17 i32, converted by `FUN_008874a0`; field meanings beyond the conversion **not confirmed**): every one
  of the 598 regions in the demos holds the defaults (0, 0, 0, 8000, 0, 12800, 128, 128, 0, 12800, 0, 0, 12800, 0, 0,
  0, 12800) — they do not change any demo's sound.
- Master volume in the demos: 12800 ×37, 13549 ×3, 15312, 16000, 13487.

**LYNX [model]:** `slicex()` returns the master gain; the importer gives the channel one full-range zone carrying it.

## 6. DirectWave — what remains **[DECOMP `FUN_180027740`, `FUN_180010f30`; DATA `py/dwstat.py`]**

- 834 of the 1 287 demo zones (pianos and electric pianos in 19 channels) have **filter A = low-pass** at cutoff 0.47
  (562), 0.5, 0.42, 0.4 (some with resonance 0.5), 88 fully open. The cutoff parameter maps to
  `440·2^((127·v)/12 − 5)` Hz (a MIDI note `127·v`, note 60 → 440 Hz) and feeds a filter chosen by type (types 0–5 one
  family, 6 and 7 others), but the programs' modulation matrix and envelopes can move it per voice: **not decoded** — LYNX
  plays DirectWave zones unfiltered.
- No demo zone uses pan ≠ centre, a sample offset, time-stretch or differing amplitude envelopes inside a program
  (DATA), so those gaps do not change the demos.

## 7. Open items

- DirectWave's voice filter and modulation matrix (§6).
- The host sampler voice's velocity/pan laws and filter (§2); FL's song-aligned tick phase for the envelopes (§3).
- The sinc kernel at positions within ~10⁻⁹ of a frame with a cutoff below π (§1.8).
- The double-precision kernel table and the zero-padding flag (§1.1, §1.6).
- Whether FPC/Slicex/Fruity Slicer switch their interpolation when FL renders (§1.2): LYNX plays their channels through
  the sampler's settings (24-point live, 32-point when exporting).

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| FLEngine `FUN_0067d410`, `FUN_0067d320`, table `0x13eb808` | index → half-width, kernel table, π/N set-up | DECOMP |
| FLEngine `FUN_0067d9b0`, `FUN_0067db00`, `FUN_00d26890` | speed, cutoff and width rules, copy flag | DECOMP |
| FLEngine `FUN_00678a90…FUN_0067a3e0` | linear, 6-point, sinc kernels (mono/stereo × int16/float) | DECOMP + EXEC |
| FLEngine `FUN_0067ec50`, `FUN_0067e4b0`, `FUN_0067d2b0`, `FUN_0067de50` | positions, edge taps, loops, end | DECOMP |
| FLEngine `0x6550d8`, `0x656150` (slot registration by name) | IPP sine/threshold/slope routines | DECOMP |
| FLEngine `0x6a68a8`, `FUN_006a04d0`, byte `0x13ec8c8` | playback default index 4 | DECOMP |
| FLEngine `0xedff62`, `0xee7ec4`, `0xeeca4d` | render default index 5, registry value | DECOMP |
| FLEngine `FUN_00d24ae0`, `FUN_00d24500`, `FUN_009ce490/9e0/950/edb0/ecc0` | sampler voice, articulators | DECOMP |
| FPC `FUN_008698a0`, `FUN_0086b480`, `FUN_00869db0` | velocity index, layer choice | DECOMP |
| Slicex `FUN_0087a8a0`, `FUN_0087e4f0`, `FUN_0087efe0`, `FUN_008874a0`, `0x8843d5` | state, master gain law and use | DECOMP |
| DirectWave `FUN_180027740`, `FUN_180010f30` | parameter text, cutoff law | DECOMP |
| `run/kern.exe` on FLEngine | kernel outputs, CPU record | EXEC |
| 165 `.flp` | usage counts, FPC layers, Slicex regions, DirectWave zones | DATA |
