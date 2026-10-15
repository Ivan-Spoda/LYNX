# Behavioural analysis of FL Studio 2026: round 47 (REA) — 3x Osc: the complete engine, its HQ oscillators and its channel processor

> **Nature of this document.** Continuation of `REA_rodada4.md` (3x Osc state, levels, channel filter) and
> `REA_rodada6.md` §3 (AM), with the same goal as rounds 37 (Ogun), 39 (GMS/Morphine) and 42 (DX10): describe FL's
> most used generator (75 of the installation's demo/template projects, 152 project channels, 179 presets) precisely
> enough for LYNX to play it as an exact instrument instead of the built-in-synth approximation of round 4
> (`synthFrom3xOsc`). *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied:
> what is here are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence. No encrypted content was touched and no Image-Line content
> (code, audio, tables) was copied into the repository: the host's wave tables are recomputed from their formulas and
> the custom-wave samples are read from the user's installation at run time.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the data (**[DATA]**) or from running the installation's own DLL, unmodified (**[EXEC]**). Where there was not enough
> evidence, the item is marked **"not confirmed"**. Inferences made *from* the code are stated explicitly as
> **inference**. Results of our own numerical model (which implements only what is marked DECOMP/EXEC) are marked
> **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets:**
  - `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll`, SHA-256
    `5d84d5ad2d8461edd43070d9a8a1147e8e8c308815ad8c91862533440d89dff2` (= rounds 4/6; copy in `r47/bin/`), Delphi,
    image base 0x400000, plugin VMT `0x737e20`: slot 1 Dispatcher `FUN_00737f90`, 3 SaveRestoreState `FUN_00738580`,
    5 ProcessEvent `FUN_00738e50`, 6 ProcessParam `FUN_00739060`, **8 Gen_Render `FUN_0073ab70`**, **9 TriggerVoice
    `FUN_00739a40`**, 10 Voice_Release `FUN_00739c30`, 11 Voice_Kill `FUN_00739c50`, 12 Voice_ProcessEvent
    `FUN_00739c90` (returns 0), **13 Voice_Render `FUN_0073a230`**, 14 NewTick `FUN_0073ac60` **[DECOMP + DATA (VMT read
    from the PE)]**. Constructor `FUN_007389f0`; levels `FUN_00738f10`; oscillator pan `FUN_00738ea0`; stereo flag
    `FUN_00738ff0`; voice stealing `FUN_00739b80`; mode dispatch `FUN_00739e90`; host-HQ path `FUN_0073a000`;
    mixers `FUN_00739ca0/ce0/d20` (x87) and `FUN_00739d70/dc0/e10` (SSE), chosen by `FUN_0073acc0`.
  - `Plugins/Fruity/Generators/3x Osc/engine_x64.dll`, SHA-256
    `80dc8bdc67dd2b005313bb4a0532b41c3a9dd886dd50ca1a3ebb52eaeadfc0b3` (one export, `dispatch`): the **HQ oscillators**
    (§2.6). The same file ships next to other generators (BassDrum, Drumaxx, FLEX, Kepler, PoiZone, Sakura, …).
  - `FLEngine_x64.dll` (SHA-256 `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02`, = rounds 6/42) for
    the host side: wave tables `FUN_009cdfc0`, the host object's `WaveTables[0…5]` (`0x1316fbf…0x1317011`), ComputeLRVol
    `FUN_006a3a00` with its x87/SSE bodies `0x6a3990`/`0x6a6040`, the channel-sample hand-over `FUN_00feda30`.
- **Shared Image-Line library inside the DLL:** the "plugin voice processor" of round 42 §3. Every function reachable
  from Gen_Render's processor calls (32 functions) and the settings/articulator/filter/end-fade functions (31 more)
  have **identical instruction streams** in DX10 and 3x Osc once absolute addresses are masked (`r47/py/fpair.py`,
  `ffind.py`) **[DECOMP]**: e.g. block start `FUN_0072b350` (DX10 `FUN_00721d10`), per-voice render `FUN_0072b450`
  (`FUN_00721dc0`), mix out `FUN_0072b510` (`FUN_00721e80`), control `FUN_0072bec0` (`FUN_00722830`), gains/filter
  `FUN_0072b920` (`FUN_00722290`), filter `FUN_0072bae0` (`FUN_00722450`), snapshot `FUN_0072c250/2e0/390`
  (`FUN_00722bc0/c50/d00`). New this round (code DX10 carries but never runs): release `FUN_0072c050`, articulator
  release `FUN_0072a6e0` (= DX10 `FUN_007201e0`), kill/fade start `FUN_0072c0d0`, released flag `FUN_0072b440`, and
  the library's rate setter `FUN_005f8470`.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=4G`): projects `/tmp/r47gh` (3x Osc, full analysis), `/tmp/r47fl`
  (FLEngine, no auto-analysis, our script `DecAt.java` decompiles single functions), `/tmp/r47eng` (engine_x64);
  outputs `r47/out/plug.c` (0x737e00…0x73b000), `base.c`, `lib.c`, `lst*.c`, `fle_smp.c`, `eng.c`; `objdump -M intel`
  of everything (`r47/out/all.asm`, `eng.asm`, `fle_wt.asm`, round 46's `/tmp/r46_fle.dis`).
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r47/run/ox.c` → `ox.exe`, a copy of round 42's `dx.c`
  (same safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded
  unmodified from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the
  plugin's own SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Additions: the
  host's `WaveTables[0…5]` built with FLEngine's formulas (§1.2); the host's ComputeLRVol replayed instruction by
  instruction (FL's x87 body `0x6a3990`, control word 0x33F; `HL_LAW=sse` selects the SSE body); `cw=<file>` hands a
  16384-float custom wave through Dispatcher 10 (as FL's host does); `m0=<va>` logs a DLL global after loading (the
  Delphi `RandSeed`, so the model can start from the same seed). The plugin itself loads `engine_x64.dll` from its
  folder. No process or dialog was left behind; outputs were deterministic for a given seed.
- **Models** (pure Python + numpy, `python3 -I`; under `r47/py/`): `osc3model.py` (state, oscillators, host
  interface, the processor's release path; uses round 42's `dxproc.py`/`dxmodel.py` for the processor), `hqmodel.py`
  (engine_x64). Drivers: `o3run.py`, `bat1.py` (every state), `t_art47.py` (processor), `t_custom.py`, `hqtest.py`,
  `cppcmp47.py`, `cpphq.py`, `golden47.py`, `syn.py`, `dbg.py`; data `collect47.py` → `r47/out/states47.pkl`.
- **Data:** every 3x Osc 0xD5 state of the installation: 334 (63 demo projects, 52 System, 40 Templates, 179
  presets), **249 distinct**; versions 10 (75), 11 (63), 12 (59), 13 (1), 14 (50), 0 (1, 44 bytes); 10 distinct states
  with the HQ byte (13 channels: *Bombs Away* ×9, *Africa's Calling* "Sub Bass", *Ookay – Thief* ×2, the
  *Hardcore* render test); 16 with AM; 46 with phase randomness. Shapes over all oscillators: saw 171, sine 123,
  square 78, custom 66, rounded 31, noise 26, triangle 24.

---

## 1. The oscillators

### 1.1 State (`FUN_00738580`) **[DECOMP + DATA; round 4 §1.1]**

Round 4's layout holds; what the loader does per version:

| Version | Read | Notes |
|---|---|---|
| < 10 | 40 bytes: four fields per oscillator | the version word itself becomes osc 1's shape; osc 2/3 mix, shape, coarse, fine; the rest 0 |
| 10, 11 | 80 bytes: parameters 0…19, then 4 bytes invert ×3 + AM | phase randomness 0; version 10 clears the four bytes |
| 12, 13 | 89 bytes: 21 parameters, invert ×3, AM, HQ (bit 0) | a 92-byte state has no HQ byte: the loader then takes an uninitialised stack byte — LYNX uses 0, **not confirmed** |
| ≥ 14 | 0x1C8 bytes: the same + 3 padding bytes + the 364-byte snapshot | the snapshot is loaded only when all 364 bytes came |

Then every parameter goes through ProcessParam (read, then write with flags 0x11), which rebuilds everything below.
Saving always writes version 14.

### 1.2 The host's wave tables (FLEngine `FUN_009cdfc0`, host `+0x18…+0x40`) **[DECOMP + EXEC]**

FL hands native plugins six 16384-float tables (the SDK's `WaveTables`), in this order (`0x1316fbf…0x1317011`):

| Index | Table | Formula |
|---|---|---|
| 0 | sine | `float(sin(2i·π/16384))` |
| 1 | triangle | `T[(i + 0x3000) & 0x3fff] = 4i/16384 − 1` (i < 8192), `1 − 4(i − 8192)/16384` above: 0 at index 0, rising |
| 2 | square | +1 for i < 8192, −1 above |
| 3 | saw | `float(2i/16384 − 1)` |
| 4 | "rounded" shape | i < 7600: `v₀ = 32767f`, `x = 32767f − 2v` (float), `T = float(int16(trunc x)/32767)`, `v ← v·0.997f`; then +1 up to 13652, −1 above: an exponential rise from −1 (RC-like) to +1, a plateau, a low step |
| 5 | noise | MT19937 seeded with 19650218: `float(u32·2^-31 − 1)` |

(Rounds 4/6 guessed shapes 1 and 4: shape 1 is the triangle, shape 4 is this table.) The harness builds them so; the
DLL's renders of all six shapes are bit-identical to the model that uses them (§4).

### 1.3 Parameters → oscillator records (`FUN_00739060`, `FUN_00738f10`, `FUN_00738ea0`, `FUN_00738ff0`) **[DECOMP + EXEC]**

`(i + 1) = 7·osc + field` (round 4). Per oscillator (record at `P+0x180 + 0x24·k`):
- **pitch** `cents = coarse·100 + fine`; **stereo detune** (right channel only) in cents; **stereo phase offset**
  `spo·0x1ffffff` (int32 product) — the right channel's start phase;
- **table**: shape < 6 → `WaveTables[shape]`; 6 → the custom wave if the host gave one (Dispatcher 10), else the saw;
  **noise** = shape 5; "needs anti-aliasing" = not noise and not the sine table;
- **levels**: `g3 = float(mix3)·(1/128)`, `rest = 1 − g3` (AM off) or 1 (AM on), `g2 = (rest·float(mix2))·(1/128)`,
  `g1 = rest − g2` (float), the sign bit flipped for inverted oscillators;
- **pan**: the host's ComputeLRVol (host slot 31) with `pan = float(pan)·(1/64)` and `vol = g` gives the oscillator's
  left/right gains (§2.2);
- phase randomness: `r = P20 << 18`.

The plugin is **stereo** when the phase randomness is on or any oscillator has noise, a phase offset, a detune or a
pan; otherwise the right channel is a copy of the left.

### 1.4 TriggerVoice (`FUN_00739a40`) **[DECOMP + EXEC]**

Voice stealing (§1.7), then a 0x34-byte record appended to the plugin's (ordered) voice list: per oscillator the left
phase 0 and the right phase = the phase offset; with randomness `r ≠ 0`, `left += (Random(r) − (r >> 1))·256` then
`right += (Random(r) − (r >> 1))·256` (unsigned arithmetic, oscillators in order). `Random(n)` is Delphi's
(`seed = seed·0x8088405 + 1`, result `(seed·n) >> 32`) on the DLL's own `RandSeed`, which a unit initialisation seeds
with `Randomize` — so **FL draws a different seed in every session** and all 3x Osc instances of a session share it
**[DECOMP]** (the harness reads it after loading; LYNX gives each instrument its own seed, 0). The voice keeps the
plugin's stereo flag; Init.Vol (velocity) is **not used** — the level comes only from Final.Vol (§2). The record is
handed to the processor (round 42 §3) like DX10's.

### 1.5 Voice_Render (`FUN_0073a230`) **[DECOMP + objdump + EXEC]**

Per call, left pass then (if stereo) right pass, oscillators 1, 2, 3 in order:
1. increment: noise → `Random(0x99326) + 0x99326` (a new random step every block, per pass); else
   `round(float(523.2511306·2^32/fs) · exp(double(p)·ln2/1200))` (cvtsd2si, low 32 bits) with
   `p = float(Final.Pitch + float(cents))`, plus `float(detune)` on the right pass (`FUN_00725970`; `ln2/1200` is a
   double computed at load from Delphi's Ln);
2. an oscillator whose gain is +0.0 (integer compare: −0.0 renders) only advances its phase by `inc·n`;
3. osc 3 with AM on → mode 12: `acc − (T[ph>>18]·g)·acc`; the first other audible oscillator → mode 11 (writes
   `T·g`), the following ones → mode 10 (`T·g + acc`); `g` = that side's pan gain; the phase steps by `inc` per sample
   and the table index is its top 14 bits;
4. the HQ byte routes every audible oscillator to engine_x64.dll instead (§2.6); FL's host "HQ render" mode routes
   anti-aliased shapes to `FUN_0073a000` (§2.7).

**Mixers:** two sets with the same semantics, picked by a CPU-feature byte at load (`FUN_0073acc0`); on this machine the
byte is 0 and the DLL uses the **x87** set **[EXEC: peek]**. The DLL leaves the x87 control word at **0x33F (64-bit
precision)** after building its own tables at load (`FUN_005a27f0`), so `T·g` is exact and `T·g + acc` / the AM step
round once from the 64-bit mantissa to float — the model reproduces it with 80-bit long doubles **[EXEC: 53-bit
emulation fails, 64-bit matches]**. Without stereo the right phases are copied from the left and the left samples
duplicated. The function returns 0: only the processor ends voices.

### 1.6 Release, kill, list **[DECOMP + EXEC]**

- Voice_Release only calls the processor's release (§3.1).
- Voice_Kill frees the processor voice and removes the record with an **ordered** delete (`FUN_004e9960` →
  `FUN_004e9470`, Delphi `TList.Delete`: the following voices move up — unlike DX10's unordered list).
- Gen_Render (`FUN_0073ab70`) is DX10's: processor block start, each voice through the processor in list order, the
  mix copied out, then last-to-first the finished voices reported to the host for killing.

### 1.7 Polyphony (`FUN_00739b80`) **[DECOMP + EXEC]**

With a max polyphony M (ProcessEvent 1) and `0 < M ≤ voice count`: the first voice in list order whose processor is
not released (else the last voice) is released through `FUN_0072c0d0` — flagged released and its **10 ms end fade
started**; it is removed when the fade ends.

---

## 2. Host interface

### 2.1 Levels **[DECOMP]**

3x Osc applies Final.Vol and Final.Pan through the processor's own pan law (round 42 §3.5) and the oscillator pans
through the host's ComputeLRVol — so a centred channel is the product of two −3 dB laws (`0.7071²`). No "link
velocity" event is sent: FL's default (Final.Vol = channel volume × Init.Vol) applies (inference, as for DX10).

### 2.2 The host's ComputeLRVol (FLEngine `FUN_006a3a00`) **[DECOMP + EXEC]**

Pan clamped to ±1; mode 1 (a project setting, round 28) → law B; otherwise law A through a function pointer set at
start-up to one of two bodies, chosen by the same kind of CPU-feature byte (`[0x15e4c78]+0x16`):
- x87 body `0x6a3990`: `fld1; fadd pan; fmul π/4 (double); fsincos; L = float(cos·vol), R = float(sin·vol)` —
  identical to the processor's pan law (round 42: the FPU's 66-bit π reduction above π/4);
- SSE body `0x6a6040`: `t = float(double(pan + 1)·π/4)`, Delphi SinCos in double, `L = vol·float(cos t)`,
  `R = vol·float(sin t)` (float products).
The DLL picks its x87 mixers on this machine (§1.5), so the harness replays the x87 body (with FL's control word 0x33F,
`0x13e4d54`). That FL's own host takes the x87 body on the same CPU is an **inference** (the byte is not read inside
FL). A plain C `cos/sin` stand-in differs from the replayed body by one float ulp for some gains (e.g. 0.484375·cos π/4)
**[EXEC]**.

### 2.3 Rate (Dispatcher 4) **[DECOMP + EXEC]**

Besides its own increment base `float(523.2511306·2^32/fs)`, 3x Osc hands the rate to the processor library
(`FUN_005f8470`): `r = float(44100/fs)`, ramp limit `float(r·0.001)` (and r·0.01, r·32.768, unused here). DX10 never
calls it, which is why round 42 found rate-independent coefficients. For 3x Osc **the filter coefficients scale with
r** (the fast LP's `c`, the biquads' `f` and their 0.02 floor, the SVF's `f`) and the gain/cutoff ramps are limited to
`r·0.001` per sample **[DECOMP + EXEC at 22.05/48/96 kHz]**. It also forwards the rate to engine_x64 (dispatch 2).

### 2.4 Custom wave (Dispatcher 10) **[DECOMP]**

FL's host (`FUN_00feda30`), for plugins whose info flags ask for the channel sample (`& 0x80008`), resamples the
channel's sample to **16384 frames** over its whole length (`FUN_006934a0` selection 0…len−1, `FUN_00694c80` with
interpolation mode 6, ratio `16384/len`), converts it (`FUN_00690cf0`) and passes the buffer to Dispatcher 10. 3x Osc
uses it as the shape-6 table (and hands it to engine_x64, dispatch 3). FL's resampler (mode 6) and the conversion's
channel handling are **not decoded**: LYNX reads the sample from the user's installation (the channel's 0xC4 path),
averages its channels and interpolates linearly to 16384 points. Data: only the *Vocoder* template's "String" channel
has a custom sample (`HQ_SmoothInvSaw.wav`); *Bombs Away* "3x Osc #16" and *Gimbal* "3xOsc #2" use shape 6 without a
sample (= saw); 31 presets use `Patches/Packs/Shapes/…` samples **[DATA]**. The DLL renders an arbitrary custom table
bit-identically to the model (§4).

### 2.5 Tempo, PPQ, ticks, channel settings

As DX10 (round 42 §2): ProcessEvent 0 tempo, Dispatcher 14 PPQ, NewTick → processor tick; without a snapshot the
plugin asks the host for the channel's settings (Dispatcher 0x1b/0x28 → host 0x5a). In the `.flp` the channel's
0xDA/0xDB records follow the plugin state; in 52 of the 57 version-14 demo states they equal the state's snapshot
(round 4 §1.4). That FL applies the channel's records after the state (so they win when the snapshot is stale) is an
**inference**; LYNX uses the channel's records when present.

### 2.6 HQ oscillators (engine_x64.dll `dispatch`) **[DECOMP + EXEC]**

With the HQ byte set (and the engine loaded) every audible oscillator goes to `dispatch(instance, mode, …)` with the
shape (`FUN_00739610`: 6 without custom → 3), buffer, n, phase, increment, gain (mode 11 first clears the buffer, then
all modes add; mode 12 is AM). In `FUN_180001660`:
- **sine**: `sinf(float(phase·2^-32)·2π)` (MSVC's sinf, float);
- **noise**: the engine's own LCG `s = s·0xbb38435 + 0x3619636b` (initial 0x1099e, process-global), value
  `float(int32 s)·2^-31`; the phase only advances;
- **other shapes**: band-limited tables built once (`FUN_180001310`/`FUN_180002d90`, custom: dispatch 3
  `FUN_1800014f0` averages the custom wave by 8 into 2048 points): the analytic shape (`FUN_1800011f0`: triangle,
  square, saw, and shape 4 = `1 − 2·0.997^(16384x)` up to 0.46387, +1 up to 0.83333, −1) at 2048 points, upsampled ×16
  by linear interpolation to 32768, FFT, then **44 levels keeping harmonics 1…H**, `H = ⌊1024·2^(−k/4)⌋` (≥ 1),
  inverse FFT ÷ 32768; level `= float(log10(0.1223091977·fs·f)·13.2877 − 5)` clamped to [0, 42] (so the kept band
  stays below ≈ 19.9 kHz), truncated; value = linear interpolation at `float(phase·2^-32)·32768`; below 0.0005
  cycles/sample the analytic shape itself;
- float (SSE) arithmetic: `v·g + acc`, AM `acc − (acc·v)·g`.
Our model (numpy FFT in double) reaches −137…−144 dB on the 9 table-based HQ states, −168 dB on the HQ sine (MSVC's
sinf vs a correctly rounded one: 75 samples differ by one ulp) and bit-identity on HQ noise **[EXEC + model]**. FL's
FFT is its own single-precision SSE code; it was not replicated.

### 2.7 FL's "HQ render" (Dispatcher 1 bit 1, `P+0x1fd`) **[DECOMP]**

When the host's process mode asks for HQ (offline rendering with high quality), oscillators that need anti-aliasing
and are not HQ go through `FUN_0073a000`: `k = max(inc >> 22, 1)` copies at gain `g/k`, the first at the voice's
phase (stored), the others offset by `inc/k` each (a box-filtered oversampling). Not modelled: LYNX renders as FL's
realtime mode; whether FL's export dialog sets this bit by default is **not confirmed**.

---

## 3. The processor's release path (DX10 never used it)

### 3.1 Release (`FUN_0072c050`) **[DECOMP + EXEC]**

Flags the processor voice released; the volume, cutoff, resonance and pitch articulators (not pan) go to the release
stage: if their state is below 7, `position = inverse release tension(current value)` (double, as the decay→sustain
step), counter 0, state 6 (`FUN_0072a6e0`); then they step with the release slope and tension, and the volume reaching
2^-24 starts the end fade (round 42 §3.8). **If the volume envelope is off, the end fade starts at once**
(`FUN_0072c0d0`) — a released 3x Osc note without an envelope lasts exactly 441 more samples.

### 3.2 Settings on block start (`FUN_0072b350`)

A pending settings change re-reads the snapshot; with the settings object's `+0xac` = 0 the next block re-releases
voices whose volume articulator is in release (state 6) — not modelled (LYNX applies settings between notes).

---

## 4. Verification

| Test | Result |
|---|---|
| All 249 distinct installation states, 3 overlapping notes with releases, pans and cents, own snapshots (version 14) or the plugin's defaults, 44.1 kHz/512 (`bat1.py`, `out/bat1_44k.log`) | **240/249 bit-identical** to the DLL: all 239 non-HQ states and the HQ noise; the 9 other HQ states within −137…−168 dB **[EXEC + model]** |
| The same at 48 kHz/1000 and 96 kHz/333 (`bat1_48k.log`, `bat1_96k.log`) | **240/249 bit-identical** at both rates (the same 9 HQ states within −136…−167 dB) |
| Processor (`t_art47.py`, `out/t_art47.log`): volume envelopes released (tensions, delay, gate, decay to 0, tempo sync), cutoff/resonance/pitch envelopes released, pan LFO, global volume LFO, the 8 filter types at 44.1/48/96 kHz, AM, inverted oscillators, max polyphony 1 and 2, 22.05/96 kHz | **43/43 bit-identical** |
| Custom wave through Dispatcher 10, plain and HQ (`t_custom.py`) | plain: **bit-identical** (2 states); HQ: −140.6 / −137.5 dB |
| LYNX's C++ port (`tools/threeoscrender`) vs the model: every state + the processor cases, 4 configurations (44.1 k/512, 48 k/1000 ticks, 96 k/333 ticks max-poly 1, 22.05 k/64 ticks max-poly 2), random notes and seeds (`cppcmp47.py`) | **650/650 bit-identical** (all non-HQ jobs); the 20 HQ jobs within 2.4·10⁻⁷ (different FFTs, §2.6) |
| C++ HQ path vs the DLL (`cpphq.py`) | −104…−144 dB, dominated by the exact-cosine end fade (§5) |

---

## 5. LYNX port (`src/core/ThreeOsc.{h,cpp}`, `src/core/FlVoiceProcessor.{h,cpp}`, InstrumentKind `threeosc`)

- The processor of round 42 moved out of `Dx10.cpp` into `FlVoiceProcessor` (DX10's checks stay bit-identical:
  256 neutral + 23 processor jobs), gaining the release path (§3), the library's rate ratio and ramp limit (§2.3).
- The importer normalises every state version to version 14 with the channel's 0xDA/0xDB records as the snapshot
  (when the channel has them), `modelParams[0]` = the project's PPQ, `samplePath` = the channel's sample when it exists
  (custom wave). The 3x Osc preset menu keeps the built-in-synth conversion.
- Host side as FL (DX10's front-end): Final.Vol = channel volume × Init.Vol, Final.Pan = note + channel pan,
  Final.Pitch = 100·(key − 60) + channel and note cents, max polyphony 1 for mono channels, NewTick on the song's PPQ
  grid.
- Mixers in `long double` with the x87 precision field forced to 64 bits; where long double is not the x87 format
  (MSVC, ARM) results are close but not bit-exact.
- By design / not modelled: the end-fade table uses the exact cosine (FL: IPP's); HQ tables from a double FFT; the
  custom wave's resampling; FL's host HQ-render mode; one random seed per instrument (FL: one per session, shared);
  the HQ noise generator per instrument (FL: per process); notes have no Mod X/Y; settings re-release (§3.2).

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| VMT `0x737e20` | slots 1…14 | DECOMP + DATA |
| `FUN_00738580` | state versions, snapshot condition | DECOMP + DATA |
| `FUN_00739060`, `FUN_00738f10`, `FUN_00738ea0`, `FUN_00738ff0` | parameters, levels, pans, stereo flag | DECOMP + EXEC |
| `FUN_00739a40`, `FUN_00407c10`, `FUN_00407bf0`, `0x74f010` | trigger, Delphi Random, RandSeed | DECOMP + EXEC |
| `FUN_0073a230`, `FUN_00725970`, `FUN_00739e90`, `FUN_00739ca0/ce0/d20` | Voice_Render, increments, modes, x87 mixers | DECOMP + objdump + EXEC |
| `FUN_0073acc0`, `0x7a0d5e`, `FUN_005a27f0` | mixer choice, x87 control word 0x33F | DECOMP + EXEC |
| `FUN_00739c30/c50`, `FUN_004e9960`, `FUN_004e9470` | release, kill, ordered delete | DECOMP + EXEC |
| `FUN_00739b80`, `FUN_0072b440`, `FUN_0072b420`, `FUN_0072c0d0` | voice stealing with the end fade | DECOMP + EXEC |
| `FUN_0072c050`, `FUN_0072a6e0` | processor release | DECOMP + EXEC |
| `FUN_005f8470`, `0x77e6f4`, `0x77e6f8` | rate ratio, ramp limit | DECOMP + EXEC |
| `FUN_0073a000` | host-HQ oversampling | DECOMP |
| engine_x64 `dispatch`, `FUN_180001660`, `FUN_1800011f0`, `FUN_180001310`, `FUN_180002d90`, `FUN_180001be0`, `FUN_1800014f0` | HQ oscillators | DECOMP + EXEC |
| FLEngine `FUN_009cdfc0`, `0x1316fbf…0x1317011` | host wave tables and their order | DECOMP + EXEC |
| FLEngine `FUN_006a3a00`, `0x6a3990`, `0x6a6040`, `0x6a6cd6…0x6a6e99` | ComputeLRVol bodies | DECOMP + EXEC |
| FLEngine `FUN_00feda30`, `FUN_00fed990`, `FUN_00694c80` | custom wave hand-over | DECOMP |
| 334 states (`collect47.py`) | versions, usage, HQ/AM/custom | DATA |
