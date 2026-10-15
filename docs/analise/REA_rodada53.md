# Behavioural analysis of FL Studio 2026: round 53 (REA) — Gross Beat's engine (time travel and volume)

> **Nature of this document.** Continuation of `REA_rodada14.md` §2 (Gross Beat's state layout, parameters and slots)
> and of the envelope work of `REA_rodada31.md` §4.4 (the `TMEnvelope` curve family). *Clean-room* reverse
> engineering, behavioural only: what is here are data layouts, formulas and behaviours described in our own words. No
> Image-Line (nor Intel IPP) code or pseudocode was copied. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only
> to trace the evidence. No Image-Line audio or preset data is in the repository: the engine runs on the state FL saved
> in the user's own project.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLL, unmodified (**[EXEC]**). Inferences are marked as such;
> unknowns are "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from the DLL's data section / its running object |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-15. Scratch directory (paths below relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r53/`.
- **Target:** `Plugins/Fruity/Effects/Gross Beat/Gross Beat_x64.dll`, SHA-256
  `9025d93836796c80d8bf84238338a54630f4ff5843d3f155335c653473298f1a` (= round 14), Delphi (`TTestPlug`, VMT `0x83f0f0`).
  Its envelope code is the shared `DSP_MEnvelope.pas` unit (source path string in the DLL).
- **Tools:** Ghidra 12.1.4 headless (`gh/`, scripts `Dec`, `Tree`, `Xr`, `Grep` of our own), `objdump -M intel` for
  the x87/SSE kernels, `tools/k.py` (constants from the DLL's sections).
- **EXEC harness:** `run/gb.c` → `gb_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu), round 52's effect harness extended
  with: sample rate/block size also sent before the state, the host's **mixing-time** answer (host dispatcher `0x24`,
  §2.1) advancing with the rendered samples, dispatcher events at sample positions (`dsp=`, play/stop), position jumps
  (`hjump=`: new host time + dispatcher 13), a time-signature record (`tsig=`, dispatcher 14), FPU-state logging,
  direct calls of the DLL's IPP entry points on double vectors (`ippD=`) and of the plugin's own sinc reader on a fake
  reader object (`kern=`). Same safety measures as before (unmodified DLL, `SetErrorMode`, vectored fault handler,
  `TerminateProcess`, `timeout`).
- **Drivers (our own code):** `py/survey53.py` (states of the installation → `data/states53.pkl`), `py/gbs.py` (state
  parser), `py/gb.py` (runs the harness), `py/cmp.py` (LYNX vs DLL), `py/all.py` (every state), `py/ext.py`
  (automation/tempo/hold/offset/block scenarios), `py/kt.py` (sinc reader alone), `py/expfit.py` (IPP exp model);
  C++: `cpp/gbcmp.cpp` (LYNX's engine on a float file, same host model as the harness), `cpp/kcmp.cpp`, `cpp/dbg.cpp`.

Error measure: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 0. Data **[DATA]**

`survey53.py` over every `.flp` of `Data/` and the plugin's presets: **41 states** — 30 mixer slots in 15 demo
projects (16 counting `NewStuff.flp`), 2 in the Touchscreen template and the 9 `.fst` presets. No Gross Beat inside a
Patcher and none in a channel. Versions: v8 everywhere in the projects, v4/v8 in the presets. 9 project slots use the
time-travel side (time mix ≠ 0 and a non-default time slot: Complex 1/2/3, Stut 9, +12, 1/2 Speed, Flange 1,
1/2 Grains), 15 use a volume slot (gates, Sidechain, Tremelo, Spd Up Gate…), 2 have a volume tension ≠ 0, 13 have the
DC option on, 6 have attack compensation on.

---

## 1. State **[DECOMP `FUN_0083f890` + `DSP_MEnvelope` `FUN_00780ff0`, `FUN_00784240`, `FUN_007845d0`, `FUN_00784d60`, `FUN_00786830`]**

Round 14 §2.1 holds. Additions:

- The nine option bytes land at plugin offsets `0x168…0x170`. Identified: byte 4 (`0x16c`) **DC removal**, byte 5
  (`0x16d`) **attack compensation** (a signed multiplier of the attack time, §3.3), byte 6 (`0x16e`) **high quality**
  (16-tap reader allowed, §2.4), byte 8 (`0x170`) **declick** (jump smoothing, §2.5; also indexes the 1/1/5 ms and
  25/25/35 ms tables). Before reading, the plugin presets byte 7 and byte 8 to 1 and parameter 8 to 65536 (older
  states keep these). Bytes 0–3 form an integer read only by the editor code; byte 7 is not read by the audio path.
- Per slot, the 15 bytes are: `i32` trigger sync, `i32` position sync, `i32` **hold**, then the bytes link,
  key-held, one-shot (names from the plugin's own slot-menu strings `FUN_00844500`). Sync combos map to
  **0, PPQ/4, PPQ/2, PPQ, 2·PPQ, 4·PPQ ticks** (table filled from the time-signature record's PPQ — **[EXEC]**: 0,24,
  48,96,192,384 at PPQ 96 and ten times that at PPQ 960). A version < 4 name "Slot N" is cleared.
- The envelope stream is read only when its kind is 3 ("map"). Points: Δx (f64; f32 for stream version < 2), level
  (stored f64, kept as f32), then tension (f32) and the curve-type byte (only 4 bytes before version 1); 20 trailing
  bytes. After loading, each slot is **normalised to one 4-beat bar**: a first point with Δx > 0 gets a copy of point
  0 inserted after it (Δx 0 — 28 slots of the data have Δx₀ = 3.7·10⁻⁵, so this path is live), then all Δx are
  multiplied by `4/ΣΔx` when that factor is not exactly 1 (an empty slot gets a point; a slot of length 0 gets a point
  at 4 beats). Negative Δx become 0.
- **Per-point coefficients** (from the arrival point's tension t, for points 1…n−1): direction = sign of t (0 when t
  is ±0); types 0/1: `k = sign·(float)(e^{|t|·ln 1000001} − 1)`, then `1/|k|` and `ln(|k|+1)` (floats); stairs/pulse
  /wave counts `n = Round((float)(e^{(float)(1−0.6|t|)·ln 1000001} − 1)·0.001)` (+1 for stairs with t ≥ 0); type 3:
  `(n+1)`, `±0.5`, `1/((n+1) ∓ 1)`; type 4: `(n+1)`, `1/(n+1)`; type 5 (pulse): `2(n+1)`, `±0.5`; type 6 (wave):
  `n + 0.5`, 2π (a rising wave with n = 0 becomes a line); types 7/8: t; type 9: |t|; types 11/12: from
  `k = max(0.9|t|, 2⁻¹⁶)`, `a = 1 − k`, `p = 1 − a³`, `q = (1 − p)/p` (negated, and the offset `q − 1`, for t > 0; halved
  for type 12). Types 0, 1, 7, 8, 9, 11, 12 with t = 0 are straight lines. Type 10 (a spline using its neighbours) is
  **not decoded** (it is in no selected slot of the data).
- A slot is **"flat"** (does nothing) when every level equals the first and that level is the default 1.0
  (`FUN_0078bd90`; time and volume slots alike, **[EXEC]** default 1.0).

---

## 2. Time travel **[DECOMP `FUN_008420f0` (Eff_Render), `FUN_00841ac0`, `FUN_00841a20`, `FUN_007802d0`, `FUN_007801d0`, `FUN_00843890`, `FUN_00614d50`/`FUN_00614a70`/`FUN_00614ad0`, `FUN_00617c90`/`FUN_00617d80`, `FUN_00843a40`, `FUN_00841920` + EXEC]**

### 2.1 Host model

- Gross Beat processes only between the host's **dispatcher 12 value 1 and value 0** ("playing"; enable clears the
  buffer, the 15-frame delay, the DC filter, the declick state and re-syncs the slots); dispatcher 13 (song position
  changed) re-syncs the slots without clearing **[DECOMP `FUN_0083f290`, `FUN_00841cf0`, `FUN_00843450`, `FUN_00844320`
  + EXEC]**. Stopped, it copies input to output; the first stopped block fades from the last output to the input with
  `w = (cos(iπ/(n−1)) + 1)/2`.
- At every block (and at every slot switch) it asks the host's **mixing time** (host dispatcher `0x24`, index 4 =
  ticks per the FL SDK's naming — **inference** — into a double) and **rounds it to an integer tick**. EXEC confirms
  the unit: shifting the answer by 12 ticks at PPQ 96 inverts a ⅛-beat gate.
- Tempo arrives by ProcessEvent 0 (float bits). The PPQ comes from dispatcher 14's record (default 96).

### 2.2 Timing and buffer

- `spb = (float)(fs·240) / (float)tempo` (float division) = samples per bar; the **span** is `2·spb` (two bars), the
  envelope step `4/spb` beats per sample.
- The buffer holds stereo float frames for **three bars at 10 BPM** (`round(3·fs·240/10) + 16` plus block and
  guards: 3 175 760 frames at 44.1 kHz/512 **[EXEC]**), with 16 guard frames mirrored at both ends so the reader never
  wraps inside a kernel. Each block's input is written to it unless the hold is frozen (§2.6).
- The reader keeps a base index `D = clamp(round(span + 0.5) + 15, 15, L − 15)` frames behind the write head, which
  advances one frame per sample. A slot value y reads at `base + y·span`: **y = 1 is the present minus 15 frames, y = 0
  two bars ago**. The base is recomputed (from the write head) at every tempo change.
- The **dry path is delayed by the same 15 frames** (a plain delay line, `TBasicDelay_32FS`) **[EXEC: an empty state
  is the input delayed by 15 samples, bit for bit]** — FL reports no latency for it.
- Time side active when the current time slot is not flat and the time mix ≠ 0, or while frozen/declicking. Otherwise
  the time envelope is advanced by one step of `n·step` and the delayed dry signal is used.

### 2.3 Envelope player (`FUN_00788d60` float / `FUN_00789a70` double) **[DECOMP + EXEC]**

State: point index, phase (0…1), rate (1/Δx), start level, delta. Start: index 0; Δx₀ = 0 → phase = rate = 1, else
rate `1/Δx₀`, phase 0; start = level₀. Each step adds `adv·rate`; while the phase is ≥ 1 (tested on the high word):
excess converted to beats, start = level of the reached point, index + 1 (**wrapping to 0 at the end: the bar
loops**), delta = `(float)level − start`, new rate `1/Δx` (Δx ≤ 0: rate 1 and phase + 1). Value = `start +
shape(phase)·delta` (round 31 §4.4's shapes, with the per-point coefficients of §1). The time player writes doubles,
the volume player floats.

### 2.4 Reader (windowed sinc) **[DECOMP + EXEC `py/kt.py`: the DLL's own reader on our inputs vs LYNX, bit-identical]**

- Position p (samples) → integer part (truncation) and fraction; the fraction is floored at 10⁻²⁵.
- `N` taps per side pair (8, or 16 in high quality when the read speed is < 1 or > 1); the kernel sums **2N frames**
  (`idx − N + 1 … idx + N`) weighted by `sin(ωc·x)/x · window`, with sines and cosines produced by Chebyshev
  recurrences seeded from four double sines (`cos(f·π/N)`, `cos(f·π/N + π/N)`, `sin(f·ωc)`, `sin(f·ωc + ωc)`) and the
  window `0.36 + c + 0.64c²`; the reciprocal of each tap distance is the CPU's approximate reciprocal refined by one
  Newton step; result × 0.5. The seeds and tap distances are computed in x87 code that the plugin runs with the
  **control word 0x073F: 64-bit precision, rounding toward −∞** (`FUN_005b2df0` before, `0x033F` after); this
  matters — without it the output differs in the last bit.
- Cutoff ωc = π, except in high quality when the smoothed speed exceeds 1: per sample `ωc = π/max(speed, 1)` and
  `2cos(ωc)`. Speed = `|Δposition + 1|` per sample, smoothed by a 1 ms (5 ms for declick level 2) symmetric
  follower whose state restarts at 1 on enable.

### 2.5 Declick, tempo crossfade, mix

- With the declick option, a per-sample speed whose float bits exceed those of `0.002·fs` is a **jump**: an offset
  `(offset + previous output) − current output` is added to the output and decays by `e^{ln 0.01/(25 ms)}` per sample
  (states flushed below 5.96·10⁻⁸). A hold start forces a jump.
- A tempo change while playing (span change > 8 samples) makes a second 16-tap reader play the old positions from the
  old base and crossfades linearly over `round(0.001·fs)` samples (44).
- Time mix m = P2/256 (if ≠ 1): `out = time·m + dry·(1 − m)`.

### 2.6 Hold (parameter 7, slot hold) **[DECOMP + EXEC]**

Bit 0 of a hold mask = P7, bits 1/2 = the time/volume slot's hold field. When the mask becomes non-zero, at the next
4-beat boundary (or at once for a slot hold of 1) the buffer write stops and the read base jumps back one bar
(`round(spb)` frames) and again every 4 beats; releasing restores the saved base. FL writes a stored P7 before calling its own
parameter handler, so a **saved** P7 does not engage by itself; only a change does (mirrored in LYNX).

---

## 3. Volume **[DECOMP `FUN_00611cd0`/`FUN_00611d60`, `FUN_00841fb0`/`FUN_00841ec0`/`FUN_00841de0`, `FUN_00608c30`/`FUN_00608dd0`, `FUN_0062bf00`]**

### 3.1 Chain

volume slot (float player) → tension curve → mix → two cascaded attack/release followers → `out = gain × time-side
output` → DC filter (option). With a flat slot or mix 0 the gain is 1.0 (the player still advances).

### 3.2 Tension (parameter 6)

`k = sign·(float)(e^{(float)|P6/256|·ln 1001} − 1)`; for k ≠ 0 the curve of round 31 type 0 is applied to the gain in
float vector steps (`1 − x`, `× ln(|k|+1)`, exp, `− 1`, `× 1/|k|`, `1 − x` for k > 0). The exp is IPP's
**`ippsExp_32f_A11`**: on this FMA-capable CPU its outputs are reproduced exactly by `x = n·ln2 + r`, n the nearest
integer of `x·log₂e` (float), the cubic `1 + r(1.00011802 + r(0.503705382 + r·0.165419281))` with fused multiply-adds,
scaled by 2ⁿ **[EXEC `py/expfit.py`: 400 001 inputs over [−7, 7.5], all equal; the tension uses |x| ≤ ln 1001]**.

### 3.3 Attack / release

`t_ms(v) = (float)(501^{v/10000} − 1)` (parameter values as float × 1e-4f); attack A = t(P4)·0.001·fs samples, release
2·t(P5)·0.001·fs; each follower uses `exp(ln 0.01 / max(T/√2, 1))` (0 if T = 0) for rising and falling input, and runs
in **x87 extended precision** with its state kept as a double between blocks (flushed below 5.96·10⁻⁸). On enable
both states take the first gain value. **Attack compensation** advances both slot envelopes by `A × option byte 5`
samples at every slot (re)start.

### 3.4 DC filter (option byte 4)

Two-pole high-pass per channel in x87: `v += (x − s)·c₂; s += v; v ·= 0.96; y = x − s`, `c₂ = (float)(2.2835·10⁻⁵ ·
44100/fs)` (**[EXEC]** bit-identical at 44.1 and 48 kHz with this scaling), states flushed below 5.96·10⁻⁸ and stored
as doubles at the end of each block.

---

## 4. Slots **[DECOMP `FUN_00844060`, `FUN_00843af0`, `FUN_00843fe0`, `FUN_00843fa0` + EXEC]**

- Parameters 0/1 request a time/volume slot. Playing, the switch waits for the next multiple of the **requested**
  slot's trigger sync (`ceil(pos/period)·period`, checked at block starts), or happens at once with trigger sync off.
- A switch restarts the slot's envelope and advances it to `(pos mod period)/PPQ` beats for its position sync
  (0 when off; during a hold the position counts from the hold), plus the attack compensation.
- **Link**: a linked time slot makes the volume side follow the time slot's index. **One-shot**: back to slot 0 after
  4 beats. Key-held and MIDI selection are MIDI features (not modelled: LYNX effects get no MIDI).

---

## 5. What LYNX does with it

- **`EffectKind::grossBeat`** (`core/GrossBeat.{h,cpp}`): the slot state is a container of our own (`"LXGB"`,
  version, project PPQ, then FL's saved bytes), the eight FL parameters are LYNX parameters (0…1 ↔ raw, exact round
  trip), so the mixer UI shows and automates them. FL's "playing"/"position changed" messages come from LYNX's
  transport (moving song position = playing; a block that does not follow the previous one = jump). The project's PPQ
  is stored at import. Gross Beat is exempt from smart-disable (its buffer must keep recording silence). Blocks longer
  than 4096 frames are split.
- The previous volume-gate conversion stays as the fallback when a state cannot be read (import report).
- The buffer (three bars at 10 BPM, as FL) is cleared on play only where it was written.

---

## 6. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Every state of the installation (`py/all.py`, music signal, 3 s, blocks of 512) | 41 states × 95 BPM / 44.1 kHz | **41/41 bit-identical** |
| same | 41 states × 174 BPM / 48 kHz | **41/41 bit-identical** |
| same | 41 states × 120 BPM / 44.1 kHz | 34/41 bit-identical; 1/2 Speed ×3, Pitch shifter, SHINE −141.5…−153 dB; Flange 1 ×2: one sample (−223 dB) |
| Song offset 1234.567 ticks / PPQ 96, 12345.6 ticks / PPQ 960, blocks 256 and 1000 at 87/133 BPM (`py/ext.py`) | 6 states | bit-identical |
| Slot automation (5 switches with trigger sync), tempo changes 128→140→97.5, P7 hold on/off, an "Inf rep" slot | 4 states | bit-identical (each scenario checked to change the DLL's output) |
| Stop / play / song-position jump | 5 states | bit-identical |
| Sinc reader alone (`py/kt.py`) | 2005 fractions × 8/16 taps × 2 cutoffs | bit-identical with the plugin's control word |
| Unit tests (`tests/tests.cpp` `gross_beat_*`) | own synthetic states: container/parameters, a 2-beat gate, a 1-bar time slot | pass |

The 120 BPM residual: at 120 BPM spans are whole numbers of samples, so slowly moving slots read at fractions within
10⁻⁹ of a whole sample. There the four seed sines come from IPP's **50-bit** `ippsSin_64f_A50`, which near π/2 is a
last bit away from the correctly rounded sine (EXEC on 400 000 arguments); after the round-toward-−∞ float conversion
that bit sometimes survives. LYNX uses the C library's sine, plus IPP's measured value at exactly whole-sample positions
(the common case of stutter slots). Not reproducible without IPP's algorithm; documented.

**Cost** (one core, 20 s at 44.1 kHz, `cpp/gbcmp` incl. file I/O): volume-only state 0.9 % of real time, time-travel
states 1.9–2.7 %. Memory: the buffer is 25 MB per instance at 44.1 kHz (FL's size).

---

## 7. Notes on exactness (what had to match)

- The follower and the DC filter run in x87 extended precision (not float); the reader's x87 part with rounding
  toward −∞; the tension's exp is IPP's 11-bit one; the time envelope is in doubles but the volume one in floats; the
  player advances a skipped side with a single step of n·step (not n steps). Each of these was a measured difference
  before it was modelled.
- LYNX reproduces the x87 parts with `long double` and the x87 control word where the compiler targets x86 with an
  80-bit `long double` (GCC/Clang, Linux and MinGW); elsewhere it rounds to nearest (last-bit differences).
- The CPU's approximate reciprocal is the same instruction in FL and LYNX, so a given machine matches itself;
  different CPU families may differ in the last bit (in FL too).

---

## 8. Open items

- MIDI control of the slots (keys, key-held, the MIDI option) — not modelled.
- Parameter 8 (hidden; 65536 = neutral in every state): its offset path is not modelled.
- Curve type 10 (spline): coefficients not decoded, evaluated as a line (not used by any selected slot).
- FL's host values themselves (that the mixing time is in project ticks and that FL sends 12/13 exactly at
  play/stop/jump) are inferred from the SDK's naming and the plugin's behaviour, not observed inside FL.
