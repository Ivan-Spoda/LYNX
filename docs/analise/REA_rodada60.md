# Behavioural analysis of FL Studio 2026: round 60 (REA) — LuxeVerb and Fruity Convolver: exact engines

> **Nature of this document.** Effect round in the line of `REA_rodada40.md` (Maximus) and `REA_rodada49.md`
> (Reeverb 2): replace LYNX's approximations of **LuxeVerb** and **Fruity Convolver** (both imported as the generic
> Freeverb-style `EffectKind::reverb` through the name table of `src/core/FlpImport.cpp`) with exact engines.
> *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data
> layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve
> only to trace the evidence. No FLEX pack or encrypted content was touched; no Image-Line audio or preset data is
> stored in the repository (unit-test goldens are the DLLs' output for our own states and input; impulse responses
> are read at run time from the user's installation or from the project's own state).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> anything without evidence is **"not confirmed"**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code (`objdump -M intel` wherever the order of float operations matters) |
| **[DATA]** | checked in `.flp` files or read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying its code) |
| **[model]** | our own reference model (C++ or Python) built from DECOMP facts and compared with EXEC |
| **not confirmed** | not enough evidence in this round |

No manual page was read in this round.

- **Date:** 2026-10-15 (the first attempt was lost in a power failure before any commit; its scratch notes were reused
  and every result below was re-run). Scratch directory (paths relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r60/`
  (`cv/` holds the Convolver half).
- **Targets (LuxeVerb):**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/LuxeVerb/LuxeVerb_x64.dll` | `498d8f8dffbfda3fdb3b290e3c919d2764fdaa9729220bd5e18d7f53b0b819e6` | Delphi plugin: state, controls, the control → engine mapping, host events, render |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` | C++ DSP library; export `Shimmer_CreateInstance` = the whole LuxeVerb engine |

- **Tools:** Ghidra 12.1.4 headless (`gh/imp.sh`, `gh/g.sh`, scripts `DecompAll.java`, `DecRange.java`); decompilations
  `out/lv_plug.c` (the plugin), `out/il_shim.c`, `out/il_shim2.c`, `out/setp.c`, `out/rate.c`, `out/proc.c` (the
  engine); `objdump -M intel` listings annotated with the constants they load (`py/ann.py`): `out/ctor.a.asm`,
  `out/rate.a.asm`, `out/setp.a.asm`, `out/proc.a.asm`, `out/pitch.a.asm`, `out/pinit.a.asm`, `out/dl.a.asm`,
  `out/hq.a.asm`, `out/m2.a.asm`, `out/reset.a.asm`, and the library's float routines `out/crt_*.a.asm`; constants
  with `py/rd.py`.
- **EXEC harnesses** (zig cc 0.14.1, x86_64-windows-gnu; same safety measures as the earlier rounds: unmodified DLLs,
  `SetErrorMode`, vectored fault handler, `TerminateProcess` at the end, `timeout`):
  - `run/fx.c` → `fx_<n>.exe`: the plugin through its FL interface with a minimal host (round 55's harness), extended
    with the host tempo event (`bpm=`, `ProcessEvent` id 0 sent after creation and after the rate set-up), the sample
    rate before the state (`pre=1`), in-place rendering (`inplace`), parameter events at sample positions (`p=n,k,v`,
    each logged as `#pe`), and an **engine-call logger** (`hook=1b8`, `hkf=<file>`): a copy of the engine object's
    virtual table, installed in the harness's own memory, forwards slots 1/2/3/5/10 to the original functions and logs
    each call with its arguments (and the input/sidechain samples of every process call). The DLLs' code is not
    touched.
  - `run/sh.c` → `sh.exe`: `ildsp_x64.dll`'s engine alone (`Shimmer_CreateInstance`, then rate / set / tempo / reset /
    process from a command file), with dumps of the engine object's memory (`dump`, `dumpp`) and calls of the
    library's float routines on our own inputs (`crt`).
- **Drivers (our own code):** `py/survey60.py` (installation states → `data/states60.pkl`), `py/fx.py`, `py/sh.py`
  (harness drivers, test signals), `py/lvparams.py` (each state's control values through the plugin's own text
  function), `py/ranges.py` (the controls' ranges read at run time), `py/mapsweep.py` + `py/mapcmp.py` (the
  control → engine mapping swept through the DLL), `py/replay.py` (plugin run with the engine-call log, replayed
  through a model), `py/cmpm.py`, `py/bis.py`, `py/dbg.py`, `py/bufcmp.py` (model ↔ engine memory comparisons,
  bisection of the first differing sample), `py/t_crt.py` + `cpp/crtcmp.cpp` (float routines), `py/e2e.py`,
  `py/e2erand.py`, `py/e2eauto.py` (LYNX ↔ DLL end to end), `py/gold.py` (unit-test goldens). Models:
  `cpp/lvmodel.cpp` (a byte-for-byte layout model of the engine object, used to localise differences against memory
  dumps), `cpp/ilmath.h` (float routines); LYNX's own engine is driven by `lx/lxrun.cpp` (engine core, the logged
  command language) and `lx/lxfx.cpp` (the whole effect from an FL state).

---

## 1. Where LuxeVerb's DSP lives **[DECOMP + EXEC]**

The plugin is a Delphi host. Its constructor (`FUN_0072df10`) resolves `Shimmer_CreateInstance` in
`Shared/ildsp_x64.dll` (`FUN_0072b710`) and keeps the returned object; every sample of audio is produced by that
object. The object's interface (virtual table, observed with the call logger and in `out/il_shim.c`):

| Slot | Call | Notes |
|---|---|---|
| 1 | sample rate (float) | (re)initialises everything (§4.1) and clears the lines |
| 2 | set parameter (index 0…48, float) | §3.2 / §4.3 |
| 3 | tempo (float BPM) | stored for the synced delays (the engine's own field and its three pitch shifters) |
| 4 | meter read (channel) | the plugin's peak display; no effect on the audio |
| 5 | process (frames, input, sidechain, output) | interleaved stereo, §4.2 |
| 10 | reset | clears lines, filter states and the feedback shifter |

The plugin adds nothing to the audio: with the logger, the plugin's output equals the engine's output for the logged
calls and samples (23/23 installation states bit-identical, `py/replay.py`, §6).

## 2. State and controls **[DECOMP `FUN_0072f610`, `FUN_0072f570`; DATA; EXEC]**

### 2.1 Layout

All little-endian: `i32 version` (7, 8 or 9 seen/written; < 10 accepted), `i32 sidechain input` (0 = none; the
plugin asks the host for that input buffer at render time, §3.4), five bytes — *predelay sync*, *feedback delay sync*
and three display switches (spectrum view options; drawing only) — then the controls as `i32` each: **38** below
version 8, **39** in version 8, **49** from version 9, which adds two bytes: *feed-forward delay 1 sync* and
*feed-forward delay 2 sync*. Saving writes version 9.

Before reading an older state the plugin sets controls 39…48 to 0, 0, 24, 20, 1719, 20, 1000, 192, 1000, 192 (and
control 38 to 1 below version 8). The two feed-forward sync flags are **not read** for versions < 9: the plugin then
applies whatever its stack held (EXEC: differs run to run); LYNX uses "off" (**not confirmed** what FL ends up with —
it only matters when such a state switches to the feed-forward pitch type later).

Installation survey (`py/survey60.py`): 23 LuxeVerb mixer slots in 8 projects (7 demo projects), 23 distinct states,
13 of version 7 and 10 of version 8.

### 2.2 Controls

Ranges read at run time from the plugin's controls (`py/ranges.py`); defaults are a freshly created plugin's values
(EXEC, no state).

| # | Control | Range | Default | # | Control | Range | Default |
|---|---|---|---|---|---|---|---|
| 0 | input gain (dB) | −600…1500 | 746 | 25 | envelope attack | 50…1000 | 50 |
| 1 | input high cut | 40…20000 | 12991 | 26 | envelope decay | 50…1000 | 400 |
| 2 | input low cut | 20…5000 | 20 | 27 | envelope offset | 0…100 | 50 |
| 3 | reverb on | 0…1 | 1 | 28 | envelope mode (off / → wet / → decay) | 0…2 | 1 |
| 4 | decay | 250…20000 | 13105 | 29 | envelope sidechain | 0…1 | 0 |
| 5 | predelay | 0…5000 | 1 | 30 | envelope low cut | 20…2000 | 301 |
| 6 | predelay (synced) | 1…768 | 192 | 31 | envelope smoothing | 10…1000 | 10 |
| 7 | brightness | 0…1000 | 850 | 32 | dry level | −600…1500 | 746 |
| 8 | character | 0…100 | 50 | 33 | wet level | −600…1500 | 746 |
| 9 | size | 1…100 | 20 | 34 | peak frequency | 40…10000 | 7726 |
| 10 | mod frequency | 0…1000 | 100 | 35 | peak gain | −1800…1800 | 0 |
| 11 | mod amount | 0…100 | 10 | 36 | peak Q | 25…5000 | 1859 |
| 12 | diffusion | 0…1000 | 1000 | 37 | stereo width | 0…200 | 125 |
| 13 | freeze mode (normal / freeze / sustain) | 0…2 | 0 | 38 | limiter | 0…1 | 1 |
| 14 | high quality | 0…1 | 1 | 39 | pitch type (feedback / feed-forward) | 0…1 | 0 |
| 15 | feedback on | 0…1 | 1 | 40 | feed-forward unpitched | 0…1000 | 0 |
| 16 | feedback pitch | −120…120 | 0 | 41 | feed-forward pitch 1 | −24…24 | 0 |
| 17 | feedback delay | 10…5000 | 1000 | 42 | feed-forward pitch 2 | −24…24 | 20 |
| 18 | feedback delay (synced) | 1…768 | 192 | 43 | feed-forward amount 1 | 0…1000 | 0 |
| 19 | feedback gain | 0…1000 | 700 | 44 | feed-forward amount 2 | 0…1000 | 20 |
| 20 | feedback high cut | 20…5000 | 1719 | 45 | feed-forward delay 1 | 10…5000 | 1000 |
| 21 | feedback low cut | 20…5000 | 20 | 46 | feed-forward delay 1 (synced) | 1…768 | 192 |
| 22 | feedback reverb mix | 0…1000 | 1000 | 47 | feed-forward delay 2 | 0…1000 | 1000 |
| 23 | envelope scale | −1000…1000 | 0 | 48 | feed-forward delay 2 (synced) | 1…768 | 192 |
| 24 | envelope threshold | −6000…0 | −4000 | | | | |

## 3. From the plugin to the engine

### 3.1 The sidechain buffer **[DECOMP `FUN_00731870`; EXEC]**

The render passes the engine `(frames, input, sidechain, output)` where *sidechain* is the host's buffer for the
state's sidechain input when that is > 0 and the host returns one, **else the output buffer itself**. The engine reads
the sidechain sample before writing the output sample, so with FL's in-place rendering (output = input) the envelope
sees the input; with separate buffers it sees whatever the output buffer held (EXEC: the harness's zeroed buffer —
the first random runs differed from LYNX only for states with *envelope sidechain* on until the harness rendered in
place).

### 3.2 Control → engine value **[DECOMP `FUN_00730100`, `FUN_0072fe70`, `FUN_0072deb0`; EXEC sweep]**

The control's position is `x = clamp((raw − min) · 1/(max − min), 0, 1)` in double, then single. The value sent to
engine index *k* (= the control's number) is (`curve(x, lo, hi, k) = lo + (hi − lo)·x^(1/k)` in double, evaluated as
exp(ln x / k), then single):

| Controls | Engine value |
|---|---|
| 0, 32, 33 | curve(x, −60, 15, 2) — dB |
| 1 | curve(x, 40, 20000, 1.5) — Hz |
| 2, 20, 21, 30, 34 | curve(x, min, max, 0.5) — Hz |
| 4 | curve(x, 0.25, 20, 0.4) — seconds |
| 5 | curve(x, 0, 500, 0.5) — ms |
| 6, 18, 46, 48 | x · 16 (single) — sixteenths |
| 8, 11 | raw · 0.01 (double) |
| 9, 31 | x · 99 + 1 (single) |
| 13, 28 | raw + 1 |
| 16 | (x − 0.5) · 24 (double) — semitones |
| 17, 45, 47 | x · 499 + 1 (single) — ms |
| 23 | x · 2 − 1 (single) |
| 24, 35, 37 | raw · 0.01 (single) |
| 25, 26, 38, 41, 42 | raw |
| 27 | x − 0.5 (double) |
| 36 | x · 4.75 + 0.25 (double) |
| the rest | x |

EXEC: every integer of every control's range (and a margin outside it; large ranges sampled at ~2000 points incl.
both ends) sent through the plugin's ProcessParam with the engine-call logger: **49 836 / 49 836 values
bit-identical** to LYNX's `luxeverb::engineValue` (`py/mapsweep.py`, `py/mapcmp.py`; ln/exp of the C library give the
same singles as the Delphi routines over all of them).

### 3.3 Host order **[DECOMP `FUN_0072e280` (dispatcher), `FUN_0072f610`, `FUN_0072fc40`; EXEC log]**

- **Sample rate** (dispatcher 4): engine rate (§4.1), then all 49 controls are sent again in order.
- **State load**: all 49 controls in order, then `FUN_0072fc40`.
- **`FUN_0072fc40`** (also run on every tempo event and from a few GUI updates of controls 3, 15, 28, 39): engine
  tempo, then one control of each delay pair — predelay 5 or 6, feedback delay 17 or 18, feed-forward delay 45 or 46,
  47 or 48 — the synced one when that pair's sync switch is on. The engine keeps one delay per pair, so the last value
  sent wins.
- **ProcessParam** of one control: that control only (also a delay's unsynced value while its sync is on, until the
  next tempo event).

Setting the rate re-initialises the engine (tempo back to 120, line lengths and envelope times to their defaults); the
plugin's full resend afterwards restores them. LYNX does the same sequence: rate, 49 controls, tempo, the four chosen
delays (EXEC: identical to the plugin with the rate set before the state, §6).

## 4. The engine (`ildsp_x64.dll`, `Shimmer_CreateInstance` → object of 0x191470 bytes) **[DECOMP + asm; EXEC dumps]**

Entry points: constructor `FUN_1801843b0`, rate `FUN_1801849a0`, set parameter `FUN_180186030`, process
`FUN_180187290`, reset `FUN_180188ac0`; pitch shifter: set-up `FUN_1801821f0`, set `FUN_180182910`, process
`FUN_180183180`; predelay read `FUN_180183f40`; diffuser `FUN_180184170`; detector high-pass `FUN_1801871b0`; the
line groups `FUN_180188c30`.

### 4.1 Set-up at a sample rate *fs*

- **Smoothers** (13): one-pole glides `c ← (target − c)·k + c`, `k = 1 − 0.37^(1/(rate·time))`. Rate *fs* for freeze
  gain, freeze mix (both 20 ms), envelope, width, brightness, decay; *fs*/16 for peak frequency/gain/Q, the input low
  and high cut and the feedback filters' frequencies (all 5 ms) — although they are stepped once **per sample** (the
  engine's update counter is never advanced, so the "every 16 samples" branch runs every time).
- **Lines:** 16 delay lines in 4 groups, each a power-of-two buffer ≥ 1 s; default lengths *fs* × a fixed factor per
  line (0.0199…0.1587); a length glide of 6.61/*fs* per sample; per-line damping (§4.2).
- **Diffuser:** per channel 8 all-passes of 4096 samples in series, delays *fs* × (0.0036…0.0161 s), gains 0.5…0.75.
- **Predelay:** per channel a 172 000-sample line read at a gliding position (glide 1.5·10⁻⁴ per sample, starting at
  1 sample after creation — so a long predelay delays the first wet output well past its own length while it glides).
- **Filters:** detector high-pass 20 Hz Q 0.75 (RBJ), the feedback path's DC blocker (pole a quadratic in *fs*) and
  10 kHz low-pass / high-pass pair, input one-pole low cut / high cut, output peak (1 kHz default).
- **Followers:** envelope attack/release coefficients exp(−220/fs) and exp(−11/fs) until the controls set them
  (exp(−2200/(ms·fs)) then); the output peak followers' release exp(−110/44100) and the limiter's glide
  1 − 0.37^(100/44100) are computed with the constructor's 44.1 kHz rate at every sample rate.
- **Three pitch shifters** (§4.4): feedback (+12 st, density 3.5, jitter 0.1/0.1, mode 3, clip of the grain sum),
  feed-forward 1 (+7 st) and 2 (+12 st) (density 2.25, mode 3, clip of the feedback signal).
- **Rotation:** 45°; size gain 1.8667; LFO 0.5 Hz; tempo 120; everything cleared.

### 4.2 One sample

1. **Envelope** (unless mode "off"): (L + R)/2 of the sidechain (switch on) or of the gained input, through the
   20 Hz (or *envelope low cut*) high-pass, rectified, attack/release follower, smoothed (*envelope smoothing*), in dB
   via the library's `log10f`.
2. **Input:** L, R × input gain. With pitch type *feed-forward* and *feedback on*: dry × *unpitched* + shifter 1 ×
   *amount 1* + shifter 2 × *amount 2* (both shifters read the gained input; each has its own delay).
3. **Input filters:** one-pole high-pass (low cut) then one-pole low-pass (high cut), coefficients from the smoothed
   frequencies every sample (bilinear, ω = π f / fs).
4. **Predelay and diffusion** (reverb on): predelay read (linear, or 4-point cubic in high quality), then
   `diffused · diffusion + predelayed · (1 − diffusion)`.
5. **Feedback injection** (pitch type *feedback*, feedback on): the previous feedback output × gain × 8 ×
   min(decay^−0.2, 1) × (pitch ≥ 0 ? 0.7·st/12 + 0.5 : 0.5 − 2·st/12) × freeze mix, added to both channels.
6. **Network** (reverb on): input × freeze gain; a 16-tap triangle LFO (phases i/16) modulates each line's length
   by ± *mod amount*·0.1·(fs/44.1 k); the decay glides (and, in envelope → decay mode, is offset by
   `scale·(max(env − threshold, 0) + offset·threshold)`, clamped 0.25…20 s); one line per sample gets new damping
   coefficients (a first-order shelf from the line length, decay and decay × brightness). Each line is read at its
   gliding fractional length (linear, or a 4-point cubic), damped, blended with the undamped value by the freeze mix;
   the 16 outputs go through a 16 × 16 matrix — the Kronecker product of four 2 × 2 rotations by *character* · 90° —
   plus the injection (mono to the first two lines, L/R alternately to the rest) and are written back. L and R are
   the sums of the even / odd lines × size gain / 8.
7. **Limiter:** peak followers on |L|, |R|; gain 1 / max(4 · glide(max(followers)), 1) (glide of the constant
   0.25 when the limiter is off).
8. **Feedback path** (pitch type *feedback*, feedback on): `mix·(L + R)/2 + (1 − mix)·(pre-limiter mono)` → feedback
   shifter (mono) → DC blocker → low-pass (*feedback high cut*) → high-pass (*feedback low cut*), each a biquad made of
   two identical one-pole sections with w = 4.8814·f/fs from the smoothed frequencies; the result feeds step 5 next
   sample.
9. **Output peak**: an all-pass-based peaking section, A = (2Q − sin ω)/(2Q + sin ω), b0/b2 = (1 + A)/2 ± gain·(1 − A)/2,
   b1 = a1 = −cos ω·(1 + A), a2 = A, coefficients every sample from the smoothed frequency, gain and Q (`sinf`/`cosf`).
10. **Wet level** = *wet level* dB, or in envelope → wet mode `2·scale·(max(env − threshold, 0) + offset·threshold) +
    wet`; gain 10^(dB/20) clamped to 0…5.6; dry and wet gains glide (45.45 s⁻¹); mid/side with width; out =
    wet-side signal · wet + input · dry.

Operation order, operand order and every constant were taken from the disassembly; the whole render runs with the
rounding the engine sets on entry: **toward zero, flush-to-zero and denormals-are-zero** (MXCSR | 0xE040), restored on
exit.

### 4.3 Parameters **[DECOMP `FUN_180186030`]**

Index *k* matches the control number. Notable side effects: *reverb* off clears the lines (and sets the freeze glides
to 1), on re-applies the freeze mode; *freeze mode* 1/2/3 = normal (gain 1, mix 1) / freeze (gain 0, mix 0) /
sustain (gain 1, mix 0); *feedback* off clears the feedback filters, DC blocker and shifter; *size* picks the 16 line
lengths among the primes below 20 000 (index `a·base_i + b` with a, b from a fitted curve of the size), sets the size
gain and the damping; *character* rebuilds the matrix; *envelope attack/decay* use the envelope's stored rate;
*dry level* is clamped to 0…5.6 linear.

### 4.4 Pitch shifter **[DECOMP `FUN_180183180`, `FUN_180182910`, `FUN_1801821f0`]**

Granular: the input (averaged with the previous sample when shifting up) is recorded in a 0.8 s ring; every
`grain length / density (+ jitter)` samples a new grain (up to 20) starts behind the write position (further back for
upward shifts) with a random offset, reading at the pitch ratio with a 4-point cubic under a sin² window; an optional
soft clip `ceiling · (2 − |t/2|)·t/2` (t = 0.2/ceiling · x, ±2 limits) of the grain sum or of the feedback signal; a
4-second delay line with a gliding read (ms or synced sixteenths) and a three-biquad filter (coefficients per sample
rate: 22.05, 44.1 (and every other rate), 48, 88.2, 96, 192 kHz) closes the loop according to the mode. Randomness is
the library's LCG (×0x343FD + 0x269EC3, bits 16…30) with a **process-wide** seed (initially 0x3920A): in FL every
shimmer engine in the process draws from it; LYNX keeps one per instance (**not confirmed** for several LuxeVerbs).

### 4.5 The library's float routines **[asm `out/crt_*.a.asm`; EXEC `py/t_crt.py`]**

`expf`, `powf`, `log10f`, `sinf`, `cosf`, `sincosf` (the vector sine used for coefficients), `fminf`, `fmaxf` of the
library's statically linked C runtime, with an FMA path chosen by a CPU-feature flag. Re-created in
`src/core/IldspMath.h` (tables generated from their mathematical definitions); EXEC: 46 000–114 000 inputs per routine
× three rounding modes (nearest, toward zero with FTZ/DAZ, both) — **0 mismatches** for all six.

## 5. What LYNX does with it

- `src/core/LuxeVerb.{h,cpp}`: `EffectKind::luxeVerb`, parameters = the 49 controls as (raw − min)/(max − min) + the
  four sync switches (53, so `maxEffectParams` grew from 48 to 64); `luxeVerbFromFl` (versions 7…9),
  `luxeverb::engineValue` (§3.2), `luxeverb::Engine` (FL's order of §3.3, then per block only the controls that
  changed; a tempo change re-sends the chosen delays), the engine of §4 in single precision with the library's float
  routines. The slot stays editable (generic editor, FL-unit texts).
- Import: `effectFromFl` routes "LuxeVerb" to the exact effect (the generic reverb mapping stays as the fallback for an
  unrecognised state); the state's sidechain input becomes the slot's sidechain (envelope source when *envelope
  sidechain* is on; a silent source is silence, no side input = the input, as FL renders in place); FL automation of
  controls 0…48 maps to the same parameter.
- Tests: `tests/LuxeVerbTests.inc` (`luxeverb_engine`): three of our own version-9 states (feedback shifter with
  envelope → decay and synced predelay; both feed-forward shifters with envelope → wet, limiter off, low quality, width
  200 %; reverb off with a negative feedback shift, sustain and filters) against DLL goldens, 35 mapping bit patterns
  from the DLL, the three state layouts and the parameter round trip.

## 6. Verification **[EXEC]**

| Comparison | Set | Result |
|---|---|---|
| Float routines vs the library (`py/t_crt.py`) | 6 routines × 3 MXCSR modes | 0 mismatches |
| Engine-call replay: plugin with the logger vs the layout model (`py/replay.py`) | 23 installation states, music 3 s, 44.1 kHz | 23/23 bit-identical (20/23 before the shifter's clip parameters 13/14, found by memory bisection) |
| The same replays through LYNX's engine core (`py/rplx.py`) | 23 states | 23/23 bit-identical |
| Control → engine mapping (`py/mapcmp.py`) | 49 836 control values | 49 836 bit-identical |
| LYNX effect (FL state → slot → engine) vs plugin, rate before the state, in place (`py/e2e.py`) | 23 states × {44.1 kHz/512/120 BPM, 48 kHz/333/87.5 BPM} | 46/46 bit-identical |
| Random version-9 states over every control and sync switch (`py/e2erand.py`) | 60 states over 22.05, 32, 44.1, 48, 88.2, 96, 192 kHz, blocks 64…1000, 4 tempos, 4 signals | 60/60 bit-identical |
| Random states with 6 control changes during the render (`py/e2eauto.py`) | 30 | 30/30 bit-identical |
| Unit test goldens (`py/gold.py`) | 3 states, 18 samples × 2 channels | bit-identical when generated |

@CONVOLVER@

## 8. Open items

- LuxeVerb: the feed-forward sync flags of version-7/8 states (uninitialised in the plugin) — LYNX: off.
- LuxeVerb: the shifters' random sequence is process-wide in FL (shared by every shimmer engine and advanced from
  plugin creation, also while FL is stopped); LYNX starts each instance at the library's initial seed when the
  effect is prepared, so grain jitter phases can differ from a given FL session (the shifters are only audible with
  feedback gain > 0 or the feed-forward type).
- LuxeVerb: FL's order of state load and sample-rate set at project load is assumed rate-first (as in the earlier
  rounds); the engine's state after either order is the same here because the plugin re-sends every control after a
  rate change.
- LuxeVerb: the plugin's own display of the synced delays ("beats:sixteenths" style) is not reproduced; LYNX shows
  sixteenths.
@CONVOLVER_OPEN@
