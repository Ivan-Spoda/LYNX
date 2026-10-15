# Behavioural analysis of FL Studio 2026: round 52 (REA) — Fruity Delay 2 and Fruity Delay 3: exact engines

> **Nature of this document.** Continuation of `REA_rodada7.md` §4 (state layouts of both delays) and
> `REA_rodada8.md` §7.2 (Delay 3's unsynced time unit, left open). *Clean-room* reverse engineering, behavioural only.
> No Image-Line code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in
> our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack was
> opened; no Image-Line preset or audio data was copied into the repository. The constants in LYNX's engine are the
> numbers of the laws below (frequencies, periods, polynomial coefficients), not data files.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). What could not be settled is
> marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code (Ghidra 12.1.4 headless + `objdump -M intel` where the float order matters) |
| **[DATA]** | checked in `.flp`/`.fst` files of the installation, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r52/`.
- **Targets** (all under `Plugins/Fruity/Effects/`):

  | File | SHA-256 | Role |
  |---|---|---|
  | `Fruity Delay 2/Fruity Delay 2_x64.dll` | `d9a7674dca16cd95078b69aed7d68c56614598868a329d2bcfaabe0ec7a6f93d` | Delphi plugin with its own DSP |
  | `Fruity Delay 3/Fruity Delay 3_x64.dll` | `b9d64b0918460d1a8c06ef48127e4d4d0c344b0459cfb33fd0b4e9e537cbfbd1` | Delphi wrapper (state, parameters, host events) |
  | `Fruity Delay 3/engine_x64.dll` | `0b05f78f4fb18f7afceb39f8dc439fbe31477d4d7e2389e0167bdf1cd7828891` | the DSP: a third-party VST2 engine (RTTI `FLDelay3Plugin`, `ILDelay3`, `ParameterSmoother`; exports `CreateEngine`, `VSTPluginMain`) |

- **EXEC harness:** `run/dl.c` → `dl_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), round 48's effect harness extended
  with the plugin's tempo event (`ProcessEvent` id 0 = tempo float bits, flags = samples per tick; sent before the
  state, before and after the rate set-up, or at a sample position), a late state load (`statelate`), calls into the
  engine DLL's own functions (`efn=`) and reads of its globals (`emem=`). Same safety measures as before (unmodified
  DLLs, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`).
- **Drivers (our own code):** `py/survey52.py` (states of the installation → `data/states52.pkl`), `py/dl.py` (runs the
  harness; test signals), `py/d2model.py` (Python model of Delay 2), `py/t_d2all.py` (model vs DLL),
  `cpp/fdcmp.cpp` + `py/lx.py` (LYNX's C++ engines on the same inputs, with the plugin's set-up order),
  `py/t_all.py` (every installation state), `py/t_rand.py` (random states over every control, tempo changes),
  `py/t_math.py` + `cpp/mathref.c` (the engine's math library against glibc), `py/gold.py` (goldens of
  `tests/tests.cpp`).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 0. Data survey **[DATA, `py/survey52.py`]**

| Plugin | Demo-project slots | Template slots | Presets (`.fst`) | Unique states | Size |
|---|---|---|---|---|---|
| Fruity Delay 3 | 66 in 19 files | 44 in 12 files | 10 | 85 | 108 B (version 1 + 26 × i32) |
| Fruity Delay 2 | 61 in 16 files | 0 | 6 | 60 | 32 B (8 × i32, no version) |

Delay 3 states: delay type true stereo 39, ping pong 39, fake stereo 7; tempo sync on in 67; keep pitch on in 18;
saturation type "saturate" 70, "limit" 15; filter LP 55, HP 19, BP 11; modulation on in 45, distortion in 28,
diffusion in 31, rate reduction in 12, bit reduction in 6; 18 states unsynced (raw time 0…550). The delay type
"Off" and the filter mode "Off" occur in no state (covered by the random states of §5).

---

## 1. Fruity Delay 2 — the engine **[DECOMP + EXEC]**

### 1.1 Controls (layout of round 7 §4.2, laws corrected)

| i | Control | Range | Law |
|---|---|---|---|
| 0 | Input pan | −128…128 | `p = v/128` (float) |
| 1 | Input volume | 0…160 | `L11(v)`: `x = v·2⁻⁷` (float); 1 when x = 1, else `(e^{x·ln11} − 1)·0.1` with `ln 11` as a float, the result rounded to float |
| 2 | Dry | 0…160 | `L11(v)` |
| 3 | Feedback | 0…127 | `v/128` (not /127) |
| 4 | **Time** | 1…768 | **1/192 of a beat**: `samples = round(v·rate·0.3125 / bpm)` (`0.3125 = 60/192`; ties to even; at least 1). Round 7's reading (1/48 beat) was 4× too long: the editor shows `v/48 : v%48` in sixteenths |
| 5 | Stereo offset | −512…512 | `ms·100 = 50·(1001^{|v|/512} − 1)` (float steps), `samples = round(ms·100·rate/100000)`; v ≤ 0 delays the left input, v > 0 the right |
| 6 | Mode | 0…2 | 0 normal, 1 swapped outputs (each channel feeds back into itself), 2 ping pong (cross feedback) |
| 7 | Cutoff | 0…128 | one-pole coefficient `c = min(1, (21^{v/128} − 1)·0.05·(44100/rate))` |

The tempo comes from the host's tempo event; a time, offset or tempo change recomputes the line (`FUN_006bfea0`).

### 1.2 Signal path per block (`Eff_Render` `FUN_006c0910`)

1. **Input matrix with gain ramps:** targets from pan p and volume g — p ≥ 0: `LL = (1−p)·g`, `LR = (p/2)·g`,
   `RR = g − LR`, `RL = 0`; p < 0: `RR = (1+p)·g`, `RL = (−p/2)·g`, `LL = g − RL`, `LR = 0` (float steps in that order).
   Each gain moves linearly toward its target, the per-sample step limited to `0.001·44100/rate`; a new instance
   starts from 0, so FL fades the input in over ~1000 samples. `inL' = L·LL + R·RL`, `inR' = R·RR + L·LR` (SSE, float).
2. The block is **added into the line** at the write position, the delayed channel `offset` samples further ahead.
3. **The loop** (x87, extended precision; feedback ramped like the gains): per sample, read the line at the read
   position (output = the read values, swapped in modes 1/2), then for each channel
   `y = (read·fb + line[write] − s)·c + s; s = y` and `line[write] = y` (0 when |y| ≤ 2⁻²⁴); the read slot is cleared.
   Filter states are kept as doubles between blocks.
4. **Output:** `out = in·dry + wet` (x87, rounded to float); with dry = 0 the wet signal is the output.

The line holds `delay + host block + offset` frames; FL resizes it on a time change (`FUN_006bb1f0`); what a resize
does to its content is not confirmed (LYNX clears it).

---

## 2. Fruity Delay 3 — how FL hosts the engine **[DECOMP + EXEC]**

- The Delphi plugin (`VMT 0x6e87d0`) only keeps the 26 raw controls (round 7 §4.1) and forwards them: controls 2,
  3 and 13 go through raw (0/1), the others as `u = (v − min)·(1/(max − min))` in double, clamped to 0…1 and
  rounded to float (`FUN_006e93b0`; ranges read from the controls at run time: Time 1…768, Time smoothing 0…1000,
  Rate reduce 0…60000, Colour −3000…3000, the rest 0…6000 or 0…3). The tempo event goes to the engine's time info
  (`FUN_006e9360`); `Eff_Render` splits the interleaved block, calls the engine's `processReplacing` and interleaves
  back (`FUN_006e9b70`).
- **Set-up order:** with the harness (state loaded, then the sample rate, then the block size) the engine behaves as
  if its rate set-up ran twice after the state, first with the wrapper's creation rate (44 100) then with the new
  rate — the only order that reproduces the DLL at 44.1 and 48 kHz (`py/t_seq.py`). In FL the host rate is the
  project rate from the start, so both runs use the project rate (not confirmed directly in FL; LYNX starts settled,
  §4).
- The engine's controls (`FUN_1800029b0`, jump table at `0x180002f00`) — `u` = the value the wrapper sends:

| i | Control | Engine law |
|---|---|---|
| 0 | Send | input gain `u` |
| 1 | Delay type | `round(3u)`: 0 fake stereo, 1 true stereo, 2 ping pong, 3 off; a change clears the lines |
| 2 | Tempo sync | on when ≥ 0.5 |
| 3 | Keep pitch | on when ≥ 0.5 (time changes crossfade instead of gliding, §3.2) |
| 4 | Time | §3.1 |
| 5 | Time R / pan | `o = |u − 0.5|`; the side with u < 0.5 is the left; ping-pong input pan matrix: x = 2u − 1 ≥ 0 → `LL = 1−x`, `LR = (1−LL)/2`, `RR = 1 − LR`, `RL = 0`; else `RR = 1+x`, `RL = (1−RR)/2`, `LL = 1 − RL`, `LR = 0` |
| 6 | Time smoothing | `T = u` seconds; glide coefficient `k = 1 − 0.01^{1/(T·rate)}` (1 when T = 0) |
| 7 | Stereo spread | `a = 0.5u + 0.5`, `b = 1 − a`; outputs `L = a·cL + b·cR`, `R = a·cR + b·cL` |
| 8 | Mod rate | LFO period from the table {8.872, 3.808, 1.833, 0.878, 0.398, 0.15, 0.05} s at `x = 0.99u·5.9994` (linear between entries); phase step `1/(rate·T)`; depth normaliser `√(T/8.872)` |
| 9 | Mod amount | `0.1·u²` (u² in double) |
| 10 | Cutoff mod | `0.5u` (LFO → cutoff) |
| 11 | Diffusion | all-pass coefficient `g = 0.499·clamp(u)` (four all-passes) |
| 12 | Diffusion spread | all-pass length, §3.1 |
| 13 | Saturation type | `round(u)`: 0 limit, 1 saturate |
| 14 | Feedback | `1.25u` |
| 15 | Filter mode | `round(3u)`: LP, HP, BP, off |
| 16, 17 | Cutoff, resonance | §3.4 |
| 18 | Distortion | §3.5 |
| 19, 20 | Hardness, symmetry | knee shape, §3.5 |
| 21 | Rate reduce | `r = (1−u)³` |
| 22 | Bit reduce | `q = (1−u)²`; scale `2^{(1 − √√q)·14 + 2}` |
| 23 | Wet | `u` |
| 24 | Colour | §3.6 |
| 25 | Dry | `u` |

- **Parameter smoothing** (`FUN_180002170`, `FUN_180002f70`): controls 0, 7, 10, 11, 14, 16–25 glide; every 8th
  sample `x ← x + (target − x)·κ`, `κ = 1 − 0.01^{1/(0.005·rate)}`, applied through the law above; within 10⁻⁴ of the
  target it snaps. The other controls apply at once. Starting values are the engine's defaults.
- The engine renders with FTZ/DAZ on (`MXCSR | 0x8040`) and zeroes input samples inside ±10⁻¹³.

---

## 3. Fruity Delay 3 — the DSP **[DECOMP + EXEC]**

### 3.1 Time law (`FUN_18000b1e0`) — closes round 8 §7.2

- **Synced:** `n = trunc(767u + 0.5)`; seconds `t = ((n + 1)/192, float)·60/bpm`. The unit is **1/192 of a beat**
  (raw 192 = 1 beat, 48 = a sixteenth; the editor shows `(n+1)/48 : (n+1)%48`). Round 7's "1/96" was wrong by 2.
- **Unsynced:** `t = ((0.94u + 0.06)³ − 0.000216)·1.000216·0.999 + 0.001` seconds — 1 ms at the minimum, 1 s at the
  maximum, a cubic in between (u = (raw − 1)/767; raw 80 ≈ 4.7 ms, 384 ≈ 149 ms). **Tempo-independent.**
- `D = trunc(t·rate)` samples (as float). Diffusion length `n_d = trunc(max(1, min(1, D/(0.015·rate))·(rate/44100)·(590·spread + 65)))`.
- **Fake stereo:** left tap `A = trunc(max(1, D − n_d))`, right tap `B = trunc(D − (1 − o)·D + A)` (swapped when the
  offset is to the left), both read from one line fed with `(L+R)/2`.
- **True stereo / ping pong:** main tap `C = trunc(max(1, D − n_d))`; the input pre-delays are 1 (no delay) and
  `trunc(max(1, o²·D))` on the offset side.
- The diffusion all-pass adds `n_d`, so the total is D.

### 3.2 Time changes: glide or crossfade (`FUN_180008ff0`)

Every tap length lives twice: an integer target and a double that the DSP reads. Without keep pitch the doubles
glide `d ← d + (target − d)·k` per sample (k from control 6), snapping within 0.001 — the classic pitch-bending tape
glide. With keep pitch, a change computes the other of two tap sets and crossfades linearly over `trunc(0.1·rate)`
samples (weights updated after each sample). A host tempo change triggers the same.

### 3.3 Per sample (`FUN_1800094b0` fake, `FUN_180009a20` true stereo, `FUN_18000a370` ping pong, mode 3 inline)

1. LFO: output `(sin(2π·phase) + 1)/2` from the phase before the step (sine only — the shape is not a control).
2. Modulation depth: `m ← m + (0.1u²·rate/2·norm − m)·(1 − 0.01^{1/(0.1·rate)})`; tap offset `(lfo + 0.5)·m` samples
   (added in float for fake stereo, in double for the others).
3. Filter coefficients every 4th sample (§3.4).
4. Read the taps (linear interpolation between `w − i` and `w − i − 1`), weighted by the two tap sets.
5. Feedback: the tap times `1.25u` (control 14) through a 10 Hz one-pole high-pass whose coefficients are computed at
   44.1 kHz whatever the rate (fake stereo feeds back the left tap, or the right one when the offset is on the left).
6. `send·input + feedback` → (fake stereo: saturation, then filter; the others: filter, then saturation) → two
   all-passes (`y = (1 − g²)·d + g·x`, line fed `x − g·d`) → the main line(s); ping pong writes the left chain into
   the right line and vice versa, its input through the pan matrix of control 5.
7. Output: colour filter on each tap (§3.6) → spread → `wet·out + dry·in`; ping pong with saturation type 1 runs a
   peak limiter (attack `1 − 0.1^{100/44100}`, release `1 − 0.1^{10/44100}`, divide when the envelope exceeds 1) on
   its output. Delay type "Off" runs only filter → saturation → colour on the input (`wet·colour + dry·in`).

Lines: 2²⁰ floats below 61 440 Hz (2²¹ up to 126 976 Hz, 2²² above), all-passes 2¹⁵.

### 3.4 Filter (`FUN_18000ab70`, `FUN_18000bbe0/bcc0`)

A state-variable filter (trapezoidal, two integrator states, outputs mixed by mode). Cutoff `c = clamp(cutoff +
0.5u_mod·lfo)`; frequency from the tables LP/BP {270, 480, 1300, 7000, 22000} Hz and HP {20, 90, 400, 1300, 4200} Hz,
interpolated at `3.999c` (BP: `3.6c`), at most 0.49·rate; `g = tan(f·3.1415/rate)` (the truncated π);
`r = (clamp(res)/2)²`, `k = 1/(4r + 0.707)` (BP: `1/(4.6r + 0.107)`); `a1 = 1/((g + k)·g + 1)`, `a2 = a1·g`,
`a3 = a2·g`; LP takes the second integrator, HP `x − k·v1 − v2`, BP `k·v1`. Mode "off" bypasses it.

### 3.5 Saturation, distortion and the reducer (`FUN_18000b960/baa0`, `FUN_18000b0e0`, `FUN_18000bef0`, `FUN_1800011f0`)

- **Rate reduce:** a sample-and-hold whose hold length is `max(1, (1 + 0.7435a − 0.0171a² + 0.0002a³)·rate/44100)`,
  `a = 127r`, with a fractional counter carried over; **bit reduce:** `trunc(scale·x)/scale`. Both run first.
- **Distortion** `x = 0.95u`: threshold `−30x` dB; makeup `(1/p)^{0.707}` with
  `p = 2.9843x⁴ − 7.5177x³ + 6.8819x² − 3.1787x² + 1`.
- **Limit (type 0):** divide by `10^{thr/20}·makeup`, an envelope on |x| (attack/release as in §3.3) divides when
  above 1, multiply back.
- **Saturate (type 1):** a soft knee per polarity — above the threshold `t = (1 ± symmetry)·10^{thr/20}·hardness` the
  excess `d` (at most twice the knee width `w = max(10⁻¹¹, (1 ± symmetry)·10^{thr/20} − t)`) becomes
  `(1 − d/(4w))·d + t`; times the makeup.

### 3.6 Colour (`FUN_18000aeb0`)

One-pole per channel: below 0.5 a low-pass from the LP table at `2u·3.9999`, above 0.5 a high-pass from the HP table
at `2(u − 0.5)·3.9999`, exactly 0.5 = flat; `t = tan(f·π/rate)` (float π), LP `b0 = b1 = t/(t+1)`, HP
`b0 = −b1 = 1/(t+1)`, `a1 = (1 − t)/(t + 1)`. A rate change recomputes them from the stored (≤ 0.49·rate) frequency.

### 3.7 The engine's math library **[EXEC `py/t_math.py`, DECOMP `FUN_1800bf350`]**

The engine links Microsoft's runtime: its `tan`/`pow` match glibc after rounding to float in every tested case
(40 000 / 15 000 arguments), but its `sinf` differs from glibc's in 1.2 % of arguments and from `(float)sin(double)`
in 0.1 %. On this machine (`__use_fma3_lib` = 1, read with `emem=`) it reduces the argument by π/2 in double with
fused steps (two-part π/2) and evaluates degree-9/10 polynomials with FMA; LYNX evaluates the same steps with
`std::fma`, which made the 22 modulation states bit-identical (they were at −119…−178 dB with a library sine).
On a CPU without FMA the plugin takes a non-FMA path — not reproduced (not confirmed which CPUs FL users have).

---

## 4. What LYNX does with it

- **`EffectKind::fruityDelay2` / `fruityDelay3`** (`core/FruityDelay`): mixer effects whose parameters are FL's raw
  controls normalised over their ranges (so the generic LYNX editor edits them; the texts show FL's units). The FL
  import routes every readable state there; the old approximations (round 7: generic delay snapped to 8 divisions)
  remain only for unreadable states, which the import report lists. FL automation of the controls maps 1:1.
- **Start-up:** FL runs the plugin long before playback; LYNX starts *settled*: Delay 2's input gains at their
  targets, Delay 3's parameter glides, tap glides and modulation depth at their targets (the LFO starts at phase 0 —
  FL's phase depends on how long it has been running).
- **Tempo:** both follow the project tempo (Delay 2 recomputes its line on a change, Delay 3 glides or crossfades as
  FL does). The engines themselves (`Delay2Engine`, `Delay3Engine`) also reproduce FL's own start (fade-in, glides),
  which `tools/fdelayrender` uses for comparisons.

---

## 5. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Delay 2 Python model vs DLL (`py/t_d2all.py`), music 2 s | 60 unique states × 120 / 87.5 BPM | 60/60 bit-identical at both tempos |
| Delay 2 LYNX engine vs DLL (`py/t_all.py d2`), music 3 s | 60 states × 120 / 87.5 BPM | 60/60 bit-identical |
| Delay 2 at 48 kHz, blocks of 300, noise bursts, 133 BPM (state loaded after the rate) | 60 states | 60/60 bit-identical |
| Delay 3 LYNX engine vs DLL (`py/t_all.py d3`), music 3 s | 85 unique states × 120 / 87.5 BPM | 85/85 bit-identical at both tempos (63/85 before the FMA sine, worst −119.1 dB) |
| Delay 3 at 48 kHz, blocks of 300, noise bursts, 133 BPM | 85 states | 84 bit-identical, 1 at −150.9 dB (a `tan` rounding in a cutoff-modulated filter, §6) |
| Delay 3 random states (every control, types 0–3, filters 0–3, smoothing, keep pitch) | 40 @ 120 BPM, 40 @ 93 BPM | 80/80 bit-identical |
| Delay 3 random states with a tempo change 128 → 97 BPM mid-render (glides / crossfades) | 30 | 30/30 bit-identical |
| Unit tests (`tests/tests.cpp` `fruity_delay_engines`) | 5 Delay 3 + 3 Delay 2 states of our own vs DLL goldens; effects from FL states (echo positions at 2 tempos) | goldens within 2·10⁻⁶ (bit-identical when generated) |

---

## 6. Open items (not confirmed)

- The order in which FL itself loads the state and sets the sample rate (§2): it decides whether a fresh FL instance
  glides from the engine defaults; LYNX starts settled either way.
- What Delay 2 keeps in its line after a resize (time/tempo change); LYNX clears it.
- The plugin's non-FMA math path (other CPUs), and the engine's `tan` in rare roundings (one 48 kHz state at −151 dB).
- The engine's GUI-only functions (`FUN_1800048d0`, `FUN_18000c030`) were not decoded.

## Evidence index

| Function / data | What | Marker |
|---|---|---|
| Delay 2 `FUN_006c0010` (ProcessParam), `FUN_006bfea0` (time/offset), `FUN_006bfdb0` (pan matrix), `FUN_006c0910` (render), `FUN_006bc060/1c0/2f0` + `FUN_006bbfa0/6bc0e0/6bc230` (mode loops), `FUN_005dff80` (ramp), `FUN_005df020` (input matrix), `FUN_005de9b0` (dry mix) | §1 | DECOMP + EXEC |
| Delay 3 wrapper `FUN_006e93b0` (normalisation), `FUN_006e9360` (tempo), `FUN_006e9b70` (render), control ranges peeked at run time | §2 | DECOMP + EXEC |
| engine `FUN_1800029b0` + jump table `0x180002f00` (laws), `FUN_180002170` + `FUN_180002f70` (smoother, process), `FUN_180004640` (rate), `FUN_1800088e0` (defaults), `FUN_180001540` (engine defaults) | §2 | DECOMP + EXEC |
| `FUN_18000b1e0` (times), `FUN_180008ff0` (glide/crossfade), `FUN_1800094b0/9a20/a370` (modes), `FUN_1800012e0` (LFO), `FUN_18000b840` (LFO rate), tables at `0x180104a40/a68/a80` | §3.1–3.3 | DECOMP + EXEC |
| `FUN_18000ab70` + `FUN_18000bbe0/bcc0` (filter), `FUN_18000b960/baa0` + `FUN_1800011f0` + `FUN_18000b0e0` + `FUN_18000bef0` (saturation), `FUN_18000aeb0` + `FUN_18000bdc0` (colour, one-poles), `FUN_18000b410` (lines, rates) | §3.4–3.6 | DECOMP + EXEC |
| `FUN_1800bf350` (sinf, FMA path at `0x1800bf6f0`), `FUN_1800bfc40` (tan), `FUN_1800bcde0` (pow), `FUN_1800be480` (powf) | §3.7 | DECOMP + EXEC |
