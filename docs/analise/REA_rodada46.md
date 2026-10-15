# Behavioural analysis of FL Studio 2026: round 46 (REA) — Kepler: the complete engine as a portable model

> **Nature of this document.** Continuation of `REA_rodada13.md` §2 (Kepler state layout), `REA_rodada17.md` §6,
> `REA_rodada18.md` §7, `REA_rodada19.md` §3 (parameter laws, LFO1, chorus mix), `REA_rodada26.md` §3 (chorus) and
> `REA_rodada33.md` §3 (chorus noise/gate, LFO1 BPM sync), with the same goal as rounds 37 (Ogun) and 39 (GMS, Morphine):
> describe the engine precisely enough for LYNX to port it as an exact instrument instead of the current approximation
> (`synthFromKepler` in `src/core/FlGenerators.cpp`). *Clean-room* reverse engineering, behavioural only. No Image-Line
> code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched and no
> Image-Line content (code, LFO shape or noise data) was copied into the repository: the engine reads those two tables
> at run time from the user's own DLL.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence,
> the item is marked **"not confirmed"**. Inferences made *from* the code are stated as **inference**. Results of our own
> implementation (which implements only DECOMP/EXEC facts) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own implementation, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (copies in `r46/bin/`, hashes identical to rounds 13/17/18/19/26/33):
  - `Plugins/Fruity/Generators/Kepler/engine_x64.dll`, SHA-256
    `baf37a1d8266aaafc55016de259be4b95f1a136a1fc13ba97514c72e2afc63ab` (C++/MSVC, framework `d1framework`, RTTI:
    `FLWrapperPlug`, `MyPlugin`, `Voice`, `JunoOscEx`, `SlaveOsc`, `ModMatrixDSP`, `JunoArpeggiator`, `DelayChorusLFOBase`,
    `ILDelay3`, `fxReverb`), image base 0x180000000.
  - `Plugins/Fruity/Generators/Kepler/Kepler_x64.dll` (front-end, Delphi), SHA-256
    `9db78f5a67af7dd1c932c3e2e8d13114905e9482e6367e797245098849bc4d5b` — only its forwarders were read (§5.1).
  - `FLEngine_x64.dll` (host, 22 227 016 bytes): only the channel-volume event path was read (§5.2).
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=4G`), project `/tmp/r46gh` (import + analysis + full
  decompilation in `r46/gh/all.c`, script `r46/gh/scripts/DumpAll.java`); `objdump -M intel` of the whole engine
  (`r46/dis.asm`) and per-function excerpts in `r46/asm/` (`b940.asm` per-sample render, `voice.asm`, `oscmix.asm`,
  `ladder0.asm`, `cd80.asm` decimator, `chorus.asm`, `vset.asm` voice setter) — every float operation order used by the
  port was taken from the disassembly; constants with `r46/k.sh` (`r46/py/k.py`); synth-object field reader
  `r46/py/sy.py`; host disassembly `/tmp/r46_fle.dis`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r46/run/kp.c` → `kp.exe`, a copy of round 39's `gm.c` (same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified
  from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's own
  SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Additions: it prints the
  plugin's VMT; `dumpc=`/`dumpsy=`/`dumpend=` write the whole synth object (0x1D000 bytes) right after creation, after
  the rate set-up and at the end; peeks `arr=-1,…,260>OFF` read synth fields through the plugin's `+0x260` pointer every
  block (with `blk=1`: every sample), `arr=-2,…` read module globals. No process or dialog was left behind; outputs were
  deterministic run to run.
- **Model:** for this engine the reference implementation is the C++ port itself (`src/core/Kepler.cpp`, built natively
  by `scripts/build-local.sh` into `build-local/keplerrender`): the engine renders in the "round toward zero" SSE mode
  (§1.2), which a numpy model cannot reproduce, and a per-sample pure-Python model of 8 voices at 2× would be too slow
  for the batteries below. Python (`python3 -I`) was used for the parts that do not depend on the rounding mode and for
  all comparisons: `r46/py/blep.py`, `r46/py/blep2.py` (impulse-kernel tables, bit-exact against the DLL's, §3.2),
  `r46/py/kcmp.py` (run the DLL and LYNX on the same state/notes, compare), `r46/py/probe.py` (per-sample comparison of
  voice-0 internals), `r46/py/battery.py` (all presets), `r46/py/mkstate.py` (synthetic states), `r46/py/fstext.py`
  (preset extraction). Logs: `r46/battery_44k.log`, `r46/build*.log`.
- **Data:** the 4 Kepler demo channels (`r46/st/demo0…3.bin`, from `r41/out/blobs41.pkl`), `Default.fst` and a synced
  preset (`r46/st/fst_*.bin`, round 33), and the 277 `.fst` presets of `Patches/Plugin presets/Generators/Kepler`
  (`r46/fst/*.bin`).

---

## 0. Data survey **[DATA]**

### 0.1 The 4 demo channels

| # | Project / channel | Version / floats | Features |
|---|---|---|---|
| 0 | Song For You: "Pads - Kepler" | 4 / 34 | pulse + saw, PW 0.25 by LFO1, **LFO1 BPM-synced** (x 0.602 → 286/192 beat) vibrato 0.19, env→cutoff 0.335, chorus **II** |
| 1 | Stroll Up: "Kepler" | 4 / 36 | **arpeggiator** (up/down, 2 octaves, free rate), saw only, noise 0.366, chorus I |
| 2 | Secret Agent: "AceBass" | 4 / 34 | **arpeggiator** (up, 1 octave, **synced**), pulse + sub, PW **manual** 0.709, **gate VCA**, **LFO1 trig mode** (gate 0), key follow 0.904, chorus I |
| 3 | Secret Agent: "Badious" | 4 / 34 | pulse + saw + sub 0.787, PW 0.869 by LFO1, LFO1 delay 1.0, octave −1, resonance 0.344, chorus II |

All four carry gate byte 0, noise byte 0 and oversampling byte 0 (round 33) **[DATA]**.

### 0.2 The 277 presets

All are `Kepler-1` version 2 with 34 floats. Coverage of the features (presets + demos): arpeggiator 24 (all three modes,
ranges 1/2/4, synced 19, free 6), PW source LFO 69 / manual 86 / envelope 76 (50 without PW), gate VCA 74, LFO1 trig mode
74 (none with "LFOs Trigger" on), LFO1 BPM sync 73, chorus off 63 / I 134 / II 76 / I+II 8, octave −1 45 / +1 16, negative
envelope polarity 38, Hold 0. Resonance = 1 (self-oscillating ladder) in 22 presets.

---

## 1. Structure **[DECOMP]**

### 1.1 State, parameters and defaults (completes round 13 §2 and round 33 §3.1)

- The plugin keeps 258 internal parameters (0x00…0x101) in several copies: the global setter's (`synth+0x1B798`), one
  per voice (`voice+0x2EB8`), the raw values the forwarder receives (`synth+0x1B390`, also the modulation-route bases)
  and the "current" array the loader writes (`synth+0x1AB80`, re-applied on every sample-rate change). Names come from
  the name table `FUN_18001fa30`; the panel order of a `Kepler-1` state maps to internal indices through
  `FUN_180016300`:

  | k | internal | name | k | internal | name |
  |---|---|---|---|---|---|
  | 0 | 0x55 | Arpeggiator Active | 18 | 0x24 | HP Freq |
  | 1 | 0x59 | Arpeggiator Mode | 19 | 0x25 | Cutoff |
  | 2 | 0x58 | Arpeggiator Range | 20 | 0x26 | Resonance |
  | 3 | 0x56 | Arpeggiator Rate | 21 | 0x2B | Env1→Cutoff Polarity |
  | 4 | 0x57 | Arpeggiator Sync | 22 | 0x2C | Env1→Cutoff Amount |
  | 5 | 0x30 | LFO1 Rate | 23 | 0x2D | LFO1→Cutoff Amount |
  | 6 | 0x31 | LFO1 BPM Sync | 24 | 0x27 | Cutoff Key Follow |
  | 7 | 0x32 | LFO1 Delay | 25 | 0x2F | VCA Source |
  | 8 | 0x33 | LFO1 Trig Mode | 26–29 | 0x3D–0x40 | Env1 A/D/S/R |
  | 9 | 0x51 | LFOs Trigger | 30 | 0x45 | Chorus Mode |
  | 10 | 0x00 | LFO1→DCO1 Amount | 31 | 0x52 | Hold |
  | 11 | 0x05 | DCO1 Pulse PW Amount | 32 | 0x53 | Octave Transpose |
  | 12 | 0x06 | DCO1 Pulse PW Mod Source | 33 | 0x48 | Main Volume |
  | 13–16 | 0x07–0x0A | Pulse / Saw / Sub active, Sub volume | 34, 35 | 0x34, 0x35 | LFO1 Retrig Phase Active / Phase |
  | 17 | 0x22 | Noise Osc Volume | | | |

- **Defaults** (`FUN_18001d900`): 1.0 for 0x03, 0x08, 0x0A, 0x0F, 0x11, 0x17, 0x20, 0x21, 0x25, 0x28 (Filter Slope →
  24 dB/oct), 0x49 (Velocity→Volume), 0x5A (Arp gate length), 0x9A (Highpass Active); 0.5 for 0x0B, 0x0D, 0x12, 0x2F,
  0x30, 0x3E, 0x42, 0x43, 0x48, 0x4A (Pan), 0x53, 0x5C…0x63 (matrix amounts), 0x65…0x6D, 0x95/0x96 (X/Y offset), 0x100,
  0x101; 0.33 for 0x0E, 0x36, 0x3F; 0 otherwise — so DCO2, filter types 1/2, Filter FM, saturation, EQ, delay, reverb,
  hyper chorus, monophonic mode and legato are off in every `Kepler-1` state. The defaults initialiser `FUN_18001ed30`
  also installs two **per-voice modulation routes**: source 6 (the voice's X = Final.FCut expression) → destination 0x25
  (cutoff) and source 7 (Y = Final.FRes) → 0x26 (resonance), amounts 2·P[0x5C] − 1 and 2·P[0x5D] − 1 (both 1).
- **Routed destinations:** the forwarder `FUN_1800199b0` stores the value and calls the global and voice setters, except
  for parameters that are route destinations: those reach a voice only when its matrix runs (§3.1) as
  `clamp(base + X·amount, 0, 1)`.
- **Loader** (`FUN_18001f170`): reset, defaults (all 258 through the forwarder, in index order), then the panel floats in
  panel order, then (version > 1) one byte (`synth+0x1C878`, used by the arpeggiator restart, §5.4), Velocity→Volume and
  Sub Osc Phase; version > 2: gate, noise and ×4-oversampling bytes (round 33). Version 1 carries no values. For version < 4
  with BPM sync on, the stored LFO1 rate is rewritten as in round 33 §3.4 (written to the "current" array without running
  the setter; it is applied at the next sample-rate change, which in the harness follows the load).

### 1.2 Rounding mode **[DECOMP `FUN_180019210`]**

The block render sets `MXCSR |= 0xE040` for its duration: flush-to-zero, denormals-are-zero **and rounding toward zero**.
Everything inside the per-sample loop — voice control, oscillators, filters, LFO, chorus, gains, MIDI events queued for
the block, the arpeggiator's note-ons — therefore rounds toward zero; the wrapper's note-on/off, expressions, parameter
setters and the state load run outside it, in the default round-to-nearest mode. The port reproduces both contexts
(`RenderMode`/`NearestMode` in `Kepler.cpp`).

### 1.3 Life cycle **[DECOMP + EXEC dumps]**

- Plugin constructor (`FUN_1800078c0`) → synth constructor (`FUN_1800166d0`) at 44.1 kHz: 8 voices, each with
  `noiseIndex = trunc(i·85 280.625)` (constant `0x1800ED9D0`), voice lane `i & 3` in filter group `i >> 2`; defaults pass
  **before** the routes exist, so every voice's cutoff is set directly to `2^C(1.0) = 49 999.98 Hz` and its resonance to
  0; the routes are then installed. LFO random seed `synth+0x1CD98 = 77 784`, last note `synth+0x13F0 = 64`
  **[EXEC dumps]**.
- State load as §1.1; then the host's sample rate re-applies all 258 "current" values through the forwarder.
- Chorus mode changes are deferred (§3.7): right after a load the chorus fades from its constructor state.
- The 8 voices run **all the time** (oscillators, filters, envelopes, matrix updates), sounding or not; nothing is reset
  at note start except the envelope stages (§3.1). Their phases therefore depend on the time since the instance was
  created (**inference**: in FL, since the project was loaded).

---

## 2. Installation data read at run time **[DECOMP + DATA + EXEC]**

| Table | Location (RVA) | Use |
|---|---|---|
| LFO1 shape | 2 170 int16 at 0x252ED0 (`.data`) | normalised at start-up (`FUN_180013d30`) to `T[i] = (s[i] − c)·(1/h)` in float with `c = (max + min)·0.5`, `h = (max − min)·0.5` (min −32 768, max 30 425 here), plus `T[2170] = T[0]` — bit-exact against the DLL's table in memory **[EXEC]** |
| Noise | 683 269 (0xA6905) int16 at 0x105CC0 | per-voice noise oscillator: `n = s[idx]·8.6315915e-05`, idx + 1 per oversample, wraps to 0 |

The engine reads both from the user's `engine_x64.dll` after checking its SHA-256 (the analysed build only).

---

## 3. Signal path **[DECOMP + EXEC]**

### 3.1 Voice control, once per host sample (`FUN_180023ab0`)

In this order (float unless noted):
1. **Envelope 1** (`+0x2DD0`, stage `+0x2DD4`): attack `e = (1.7 − e)·kA + e`, at `e ≥ 1` → 1 and decay; decay
   `u = kD·u; e = u·(1 − S) + S`; release `e = kR·e`. Envelope 2 the same (it only feeds DCO1 pitch with amount 0 in
   Kepler-1). `kA = 1 − 0.41176^(1/(fs·TA))` (double; 1 if TA = 0), `kD = 0.1^(1/(TD·fs))`, `kR = 0.1^(1/(TR·fs))` (float
   pow), with the 11-point tables of round 17 (§4).
2. **Matrix counter:** every `trunc(0.001·fs)` = 44 samples (and at note start) the voice applies its two routes (§1.1).
3. **Pulse width:** `PW = 0.458 − src·0.432·amount`, `src` = LFO1 triangle + 0.5 (source < 0.333), 1.0 (manual, < 0.666)
   or envelope 1.
4. **Glide:** `p = (target − p)·k + p` (`k` = 1 when Portamento = 0); targets are `(note − 69)/12` octaves.
5. **Pitch:** `s = (2·L1)·A_dco + dcoPitch + (((((0·bend) + p) + octave) + pitchExpr) + midiPitch) + (env2·0 + 0)` (this
   association order), `inc = pow(2, s·1.0005)·440·(1/(2·fs))` in double, clamped to `[0, 0.25]` → oscillator increment;
   leak `= min(pow(0.37, 10/fs), 1 − inc·0.267949)`.
6. **Cutoff:** `x = ((note − 24)·(1/12))·keyFollow + 0·L2 + (polarity·env1·envAmount + (2·L1)·A_cut)`,
   `fc = powf(2, x)·f0`, clamped to `[5, 30 000]` → the voice's filter lane, with the resonance.
7. Voice age `+1`; gate VCA follower `v = (velGain − v)·(1 − 0.1^(400/fs)) + v`.

### 3.2 Oscillator `JunoOscEx` (`FUN_180014f80`, `FUN_180014da0`, `FUN_180014d00`), twice per host sample

- **Band-limited impulse train, integrated.** Phase φ and increment in double. Per oversample: `φ += inc`; impulses are
  added for (a) the pulse's falling edge when `φ ≥ PW` (amplitude −pulse level), (b) the sub-square's toggle when
  `φ ≥ subPoint` (`subPoint = 0.99·SubPhase`; amplitude ±sub level, sign alternating), (c) the wrap `φ ≥ 1`
  (`φ −= trunc φ`, amplitude saw + pulse), then (a) and (b) again in the same sample when the new phase already passed
  them. The impulse's fractional position is `(edge − φ_prev)/inc` (float), `1 − φ/inc` (double → float) for the wrap.
- **Kernel:** row `trunc(1024·pos)` (pos clamped to [0, 1]) of a 1025 × 32 table (16 taps + copy), added from the ring
  position into a 16-sample ring buffer. The table is the impulse response of eight RBJ low-pass biquads in double
  (Butterworth Q's 0.50241929 … 5.1011486, `ω = π/1024` at the 1024× resolution, the middle tap weighted by `b2` instead
  of `b1` — as the code does), first 768 samples dropped, scaled by `1/max`, linear fade over the last 1024 samples.
  **Bit-exact** against the DLL's table in memory when the cosine is taken one ulp below the correctly rounded value and
  `1/max` one ulp above (the DLL uses a reciprocal estimate + Newton step) **[EXEC, `r46/py/blep2.py`]**.
- **Integrator:** `y = (y − inc·saw) ; y = (y + ring[r])·leak`, `out = (y + y) − saw`, ring slot cleared, `r = (r + 1) & 15`.
- **Mix** (`FUN_180024200`): `in = (noise·noiseVol + out·dco1Level)·0.27`, then the ladder.

### 3.3 Ladder filter (filter type 0, `FUN_18001b940` coefficients + `FUN_180013260`), SIMD over 4 voices

- Coefficients once per host sample: `w = max(min(fc, 0.4995·2fs), 20)·π/(2fs)`,
  `t = ((2.4716885 − w²·0.18975969)·w)/(2.4674010 − w²)`, `G = 1/(t + 1)`, `g = G·t`, `g4 = (g²)²`,
  `k = 4.2·res`, `norm = 1/(k·g4 + 1)`.
- Per oversample: `fb = ((g·(G·s1) + G·s2)·g + G·s3)·g + G·s4`, `u = ((1 + res)·in − fb·k)·0.36429873`,
  `v = sat(u)·2.745·norm`; four stages `y_n = g·x + G·s_n`, `s_n += 2·g·sat(x − s_n)`; output `y_slope` (Filter Slope
  `trunc(3x + 0.5)` → 6/12/18/24 dB, 24 by default). `sat` = the Padé tanh
  `(((u²+378)u²+17325)u²+135135)u / (((28u²+3150)u²+62370)u²+135135)` with `u` clamped to ±5.

### 3.4 Order of one host sample (`FUN_18001b940`)

LFO1 (§3.5) → voice control ×8 → filter coefficients → 2 × (oscillators + noise ×8, ladders, VCA/pan sum §3.6) →
decimator → high-pass → chorus with mode cross-fade (§3.7) → [saturation, EQ, hyper chorus, delay, reverb: all off] →
main volume/pan and output gain (§3.8).

### 3.5 LFO1 (global)

`φ_f = (float)φ`; triangle `2φ` (< 0.25), `0.5 − ((φ − 0.25) + φ − 0.25)` (< 0.75), `((φ − 0.75) + φ) − 1.25`;
shape `W = ((1 − f)·T[i] + f·T[i+1])·0.5` with `i = trunc(2170φ)`; then, when synced, `d = (beats − last)·κ` replaces the
increment if `0 ≤ d < 1` (round 33); `φ += inc` (double) with wrap. Outputs: triangle + 0.5 (PW source) and
`L1 = R·G·W` where `R` is the delay ramp **before** this sample's increment (`R = min(R + 1/(D·fs), 1)`, restarted at
the first held note when Trig Mode is off) and `G` = 0 when Trig Mode is on and "LFOs Trigger" < 0.5 (else 1). Rate as
round 17; `inc = (double)(rate_f·(1/fs)_f)`; `κ = 192/trunc((2^(7(1 − x)) − 1)·48.370079 + 1.1)`.
The beat position is the host song position while playing, advanced per sample by `(1/fs)·tempo/60` (double)
**[DECOMP `FUN_180019210`]**. LFO2 runs too but all its amounts are 0 in Kepler-1.

### 3.6 VCA, sum, decimation, high-pass

- Per voice and oversample: amplitude = gate follower (VCA Source ≥ 0.5) or `max(env1 − 0.025, 0)·velGain·1.025641`
  (envelope; optional extra smoothing by VCA Smooth, off); pan gains `√(1 − p)·1.414427`, `√p·1.414427` from the pan
  expression, smoothed by `1 − 0.1^(200/fs)` except on the first sample of a fresh voice;
  `L += (y·panL)·amp`, `R += (y·panR)·amp` over all 8 voices.
- **Decimator** (`FUN_18001cd80`): a 2-path polyphase half-band of 6 allpass sections each
  (`y = (x − y[n−2])·c + x[n−2]`), coefficients 0.0366815031, 0.274631768, 0.561098993, 0.769741833, 0.89226079,
  0.962094545 (path A) and 0.136547625, 0.423138618, 0.677540064, 0.839889646, 0.931541979, 0.987816393 (path B), both
  paths fed every oversample, output `(A[n] + B[n−1])·0.5` at the second oversample.
- **High-pass** (Highpass Active, on by default): `fc = 2680·x^1.6 + 20` Hz, `t = tan(fc·π/fs)`, `b0 = 1/(t+1)`,
  `b1 = −b0`, `a1 = (1 − t)·b0`; `y = (b0·x + b1·x1) + a1·y1`.

### 3.7 Chorus (refines round 26 §3 to the engine's float order)

- Mode change (`P[0x45]`) only marks a pending switch: the render fades the current contribution out by `200/fs` per
  sample, sets the fade to −1 at the switch (so the new mode stays silent for the first 5 ms and fades in over the next
  5 ms), applies the new mode's constants (rate `0.4/fs`, `0.6/fs` or `8/fs`, offset, base and depth as round 26, mix
  target 0.5, wet gain 1) and re-designs the pre high-pass at **0.5 Hz** (`tan(0.5·π/fs)`, in the render's rounding).
  The 50 Hz design happens only in the sample-rate setter. The output during the fade is `dry·(1 − f) + f·chorus` with
  `f = max(fade, 0)`.
- Processing (`FUN_180001a60`): mono input `(L + R)·0.5`; LFO (float phase, triangle or `(sin(2πφ) + 1)·0.5`); delay
  smoothing `D = (base + depth·w − D)·s + D`, `s = 1 − 0.1^(20/fs)`; pre high-pass `(b0·x + b1·x1) + a1·y1`; two RBJ
  biquads 7 kHz Q 0.707 (`((a1·y1 + a2·y2) + b2·x2) + (b0·x + b1·x1)`, coefficients designed in float with `sincos`);
  32 768-sample lines written before the read; 4-point read `((((c + ((b + a) + b))·t − (c + ((b + a) + b) + (b + a)))·t
  + a)·t) + p1` with `a = (p2 − p0)/2`, `b = p1 − p2`, `c = (p3 − p1)/2`; post low-pass 7 kHz `(b1·x1 + b0·x) + a1·y1`;
  mix `wet·√m·g + dry·√(1 − m)`, `m` and the output gain smoothed by `s` from 0 toward 0.5 and 1. Noise and gate stay off
  (round 33).

### 3.8 Output gains

`main = ((0.32754314 − v·1.3922914)·v − 0.34967893)·v` (v = Main Volume; negative: the engine's output is inverted),
`L = main·√(1 − pan)`, `R = main·√pan` (pan 0.5), smoothed by `1 − 0.1^(400/fs)` from 0; then the MIDI-volume gains,
smoothed by 0.05 per sample, which keep their constructor value **6.309** (`6.2103·1 − 1.461·1 + 1.5597·1` at MIDI volume
1.0, centre pan) unless the host sends ProcessEvent 3/2 (§5.2).

---

## 4. Parameter laws (11-point tables, `(1 − f)·T[i] + f·T[i+1]` at `10x`) **[DECOMP]**

Round 17/18 tables confirmed (attack, decay, release, cutoff log2, env→cutoff, LFO rate, LFO1→DCO, LFO1→cutoff, LFO
delay). Additions:

| Parameter | Law |
|---|---|
| Sub Osc Volume | `table{0, 0.01, 0.02, 0.05, 0.09, 0.14, 0.23, 0.46, 0.68, 0.92, 1}·1.1111112` when Sub active (else 0) |
| Noise Osc Volume | same table, never below 0.01 |
| Pulse / Saw active | level 1 or 0 (x ≥ 0.5) |
| PW | `0.458 − src·0.432·amount` (§3.1) |
| LFO1→DCO1 Range | ×10 when ≥ 0.5 (not on the Kepler-1 panel) |
| Octave | `trunc(2x + 0.5) − 1` |
| Cutoff | `2^C(clamp(x))` Hz, set through the route (§1.1) |
| Velocity curve | `g = ((v²·(0.98435v) − (0.231573v)·v) + 0.247223v)·VV + (1 − VV)`, v = MIDI velocity/127, VV = Velocity→Volume |
| Arpeggiator | §5.4 |

---

## 5. Notes, voices, host interface

### 5.1 The FL wrapper (`FLWrapperPlug`: TriggerVoice `FUN_180009160`, Voice_Release `FUN_180009510`, Gen_Render `FUN_180009740`, ProcessEvent `FUN_180008e60`) **[DECOMP + EXEC]**

- The front-end `Kepler_x64.dll` forwards Gen_Render, TriggerVoice, Voice_Release and Voice_Kill unchanged
  (`0x6d5790…0x6d57f0`) **[DECOMP]**.
- TriggerVoice: note = `trunc(Init.Pitch)/100`, MIDI velocity `trunc(clamp(41.71426·ln(10·Init.Vol + 1), 0, 127))`; if
  the same (channel, key) still has a record, the wrapper first sends that key's note-off; then note-on, then the
  expressions pan (Init.Pan), velocity (unused), X (Init.FCut), Y (Init.FRes).
- Gen_Render compares each held note's FinalLevels with the last values sent (initially −10 000) and sends the changed
  ones: X (Final.FCut), Y (Final.FRes), pitch (`(Final.Pitch − 100·(key − 60))/1200` octaves), pan (`p = (Final.Pan + 1)/2`
  → `√(1 − p)·1.414427`, `√p·1.414427`). **Final.Vol is not read.**
- Voice_Release: note-off (velocity from the host's release velocity); Voice_Kill only drops the wrapper's record.
- ProcessEvent: 0 tempo, 2 MIDI pan, 3 MIDI volume (`vol/127`, cubic `6.2103v³ − 1.461v² + 1.5597v`), 4 MIDI pitch.

### 5.2 Channel volume in FL **[DECOMP host, partly]**

FL's channel-volume updater (`FLEngine 0xfe9d20`) sends ProcessEvent 3 (MIDI volume `V·W/1 280 000`, i.e. 100 at the
default volume) only to plugins whose channel flag `+0x6EC` is set; that flag is `Info.Flags & 0x4000`
(`0x12bd700`). Kepler's flags are **0x08200011** (no 0x4000) **[EXEC: plugin info]**, so Kepler gets no MIDI-volume
event and ignores Final.Vol. How FL then applies the channel volume to this plugin is **not confirmed** (no host
multiplication was found in this round or in round 28 §4.3).

### 5.3 Voices **[DECOMP `FUN_180018fe0`, `FUN_1800188a0`, `FUN_180018680`, `FUN_180022280`, `FUN_180018b30`]**

- A note already held is ignored. On the first held note (and Trig Mode off) LFO1's delay ramp restarts and, with
  "Retrig Phase Active", the phase jumps to `min(Phase, 0.999999)`.
- Allocation: the voice already playing the key, else the free voice (gate 0) with the largest age, else the oldest.
- Voice start: glide source = the last note played (Legato off), envelopes restart from their current level (stage
  attack, decay accumulator 1), velocity curve (§4), matrix applied immediately; oscillators, filters, noise position and
  smoothers are **not** reset.
- Note-off: gate 0, velocity gain 0, both envelopes to release. Hold ≥ 0.5 keeps notes (released when Hold goes off).

### 5.4 Arpeggiator (`JunoArpeggiator`: `FUN_180012940`, `FUN_180012d60`, `FUN_180012b30`, clock in `FUN_180019210`)

- Four note lists: played order, reverse order, ascending, descending. Mode `x < 0.333` up, `< 0.666` up/down, else down
  (down starts at the top octave); range `2^trunc(2x + 0.5)` octaves; step note = list note + 12·octave, clamped 0…127,
  velocity of that list entry.
- Clock: seconds (free) or beats (Sync); period `T(x)` s from `{0.811, 0.754, 0.575, 0.41, 0.211, 0.144, 0.102, 0.077,
  0.053, 0.03, 0.021}` or `{96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6}[trunc(12x + 0.5)]/96` beats; gate length
  `P[0x5A]·period` (1). Per sample: accumulate the time delta; at each step time play the next note, at step + gate
  release it and schedule the next step. The first note restarts the clock at the current time, or (state byte set,
  transport playing and Sync on) at the next grid point `ceil((t − 0.001)/T)·T`.
- A note added while running keeps the current position (index +1 when inserted at or before it).

---

## 6. Verification against the DLL **[EXEC + model]**

Harness and port fed the same state and notes (44.1 kHz, blocks of 512 unless noted); difference = max |DLL − LYNX| over
both channels, in dB relative to the DLL's peak.

### 6.1 Results

| Set | Result |
|---|---|
| `Default.fst`, 3–5 notes, 22.05/44.1/48/96 kHz | **bit-exact** (every sample) |
| 4 demo channels, 3 notes, 2 s | −127.9 … −132.4 dB |
| 4 demo channels, 30 s, 30 notes | −126.4 … −131.4 dB (no drift) |
| demo 0/2 at 22.05/48/96 kHz | −124.2 … −132.7 dB |
| synthetic: Hold, LFO retrig phase, LFO trig + LFOs Trigger, arp down 4 oct, arp synced at 90/125/140 BPM, slow envelopes + all oscillators + chorus I+II, same-key retrigger | −125.4 … −132.6 dB |
| 277 presets + 6 demo/test states, 4 notes, 2 s | 273 of 283 within −99 dB (median −130.6 dB); the 10 others all have **Resonance = 1** (self-oscillating ladder; 22 presets have it, the other 12 stay within −99 dB): the ulp-level differences of §6.2 grow to −79 … −1 dB while the self-oscillation rings, then fall back to about −130 dB |

### 6.2 Remaining differences

- **pow inside the render.** The pitch (`pow`, double) and cutoff (`powf`) come from the DLL's statically linked
  library running in the "toward zero" mode. The port evaluates `pow` in extended precision and rounds to double (a plain
  double `pow` gave more mismatches; truncating the extended value toward zero changes no output sample of the tests).
  Its result still differs from the DLL's by one ulp on some samples (EXEC: e.g. the 814th increment of demo 0, where the
  DLL's value is one ulp above the truncated exact 2^x checked with 106-bit arithmetic), so single-ulp differences remain
  (−125 … −135 dB on the signal).
- **Self-oscillating patches.** With Resonance = 1 the ladder rings on its own and amplifies these ulp-level
  differences: one single one-ulp change of one voice's cutoff on one sample (our engine against itself) grows to
  −60.6 dB in "Kromatone" and −94.4 dB in "Terminaline" (both Resonance 1), while it stays at −132 … −143 dB in
  ordinary presets ("Acceletrosonic", "Ascendirect", "Acutobrass", "Reedy strings") **[model]**. The DLL/LYNX
  differences of §6.1 on the 10 outliers have this origin (**inference** from the growth pattern: they start at the
  ulp level and grow only while the self-oscillation rings).
- The impulse-kernel scale uses the DLL's reciprocal estimate as measured on the analysis machine; the DLL's own table can
  differ by one ulp on other CPUs (**inference**: reciprocal estimates are vendor-specific).

---

## 7. For LYNX

Implemented in this round (`src/core/Kepler.{h,cpp}`, `InstrumentKind::kepler`):
- the whole engine of §1–§5 with the engine's rounding (render toward zero, setters to nearest); the LFO shape and noise
  tables read from the user's `engine_x64.dll` (SHA-256 checked), the impulse kernel computed at start-up;
- the importer turns a usable `Kepler-1` state into `InstrumentKind::kepler` with the DLL path in `samplePath`;
  `synthFromKepler` stays the fallback (state unusable — Kepler-2/Exo, gate/noise/×4 bytes, version 1 — or DLL missing);
- host side as FL's wrapper (§5.1): velocity from Init.Vol `(11^(1.28·vel) − 1)/10`, per-key records with the
  same-key release, Final.Pan = note pan + channel pan through the pan expression, channel/note pitch through the pitch
  expression; LYNX's channel volume is applied as an output gain (FL's mechanism not confirmed, §5.2);
- tempo from the song, song position (beats) while the transport plays for the synced LFO1 and the arpeggiator;
- `tools/keplerrender.cpp` (reference renderer), tests `kepler_engine` (default patch bit-exact against the DLL) and
  `kepler_demo_channels` (4 demo channels against DLL fingerprints), import counts in the demo scan; the channel
  settings panel edits 15 panel floats inside the state (the voices restart).

The engine is built off the audio thread (on configure / sample-rate change) and swapped in; a channel counts as idle
after a quarter of a second of silence (every voice's amplitude 0), so the chorus and filters ring out.
Differences by design: LYNX skips idle channels, so the free-running oscillators/LFO pause between notes (FL's run
from instance creation); MIDI volume/pan events are not sent (as FL for this plugin).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| K1 | Structure | 8 always-running voices, DCO at 2× with integrated band-limited impulses, 4-stage Padé-tanh ladder, envelope/gate VCA, half-band decimator, high-pass, Juno chorus, gains **[DECOMP+EXEC]** |
| K2 | Rounding | render in round-toward-zero with FTZ/DAZ; setters/note-on in round-to-nearest **[DECOMP+EXEC]** |
| K3 | Installation data | LFO shape (2 170 int16) and noise (683 269 int16) from the DLL; impulse kernel generated (bit-exact) **[DECOMP+EXEC]** |
| K4 | State | panel map, defaults, two X/Y routes to cutoff/resonance, loader order **[DECOMP+DATA]** |
| K5 | Host interface | Init.Vol → velocity, same-key release, pan/pitch/X/Y expressions, Final.Vol ignored, MIDI-volume default 6.309 **[DECOMP+EXEC]**; channel volume path **not confirmed** |
| K6 | Arpeggiator | 4 lists, up / up-down / down, 1/2/4 octaves, free or synced periods, gate length, grid restart **[DECOMP+EXEC]** |
| K7 | Verification | default patch bit-exact; demos −124…−133 dB; 273/283 states within −99 dB, the other 10 self-oscillating (Resonance 1) **[EXEC+model]** |

---

## Evidence index

| Function / data | What it gave | Marker |
|---|---|---|
| `FUN_180019210` | block render, MXCSR, MIDI queue, transport, arpeggiator clock | DECOMP |
| `FUN_18001b940` (`asm/b940.asm`) | per-sample order, LFO1, filter coefficients, VCA sum, HP, chorus switching, gains | DECOMP |
| `FUN_180023ab0` (`asm/voice.asm`) | voice control order and constants | DECOMP |
| `FUN_180024200`, `FUN_180014f80`, `FUN_180014da0`, `FUN_180014d00` | oscillator, impulses, noise mix | DECOMP |
| `FUN_180014a20`, `FUN_180015130` | impulse-kernel tables | DECOMP + EXEC (table dump) |
| `FUN_180013260` (`asm/ladder0.asm`) | ladder | DECOMP |
| `FUN_18001cd80` (`asm/cd80.asm`), decimator object | half-band, coefficients | DECOMP + EXEC (peek) |
| `FUN_180001a60`, `FUN_1800025c0`, `FUN_180002690` | chorus processing and filter designs | DECOMP |
| `FUN_180019a20`, `FUN_180022840` (`asm/vset.asm`) | global and voice setters | DECOMP |
| `FUN_18001d300`, `FUN_18001d700`, `FUN_180022440` | sample-rate set-up | DECOMP |
| `FUN_18001f170`, `FUN_18001ed30`, `FUN_18001d900`, `FUN_180016300`, `FUN_18001fa30` | loader, defaults, panel map, names | DECOMP |
| `FUN_1800199b0`, `FUN_180016180`, `FUN_180015f90` | forwarder, matrix | DECOMP + EXEC (route peek) |
| `FUN_180018fe0`, `FUN_1800188a0`, `FUN_180018680`, `FUN_180022280`, `FUN_180018b30` | note on/off, allocation, voice start/release | DECOMP |
| `FUN_180012940`, `FUN_180012d60`, `FUN_180012b30`, `FUN_180021a40`, `FUN_18001b620` | arpeggiator | DECOMP |
| `FUN_180009160`, `FUN_180009510`, `FUN_180009740`, `FUN_180008e60`, `FUN_1800184f0`, `FUN_180018440` | wrapper, expressions, MIDI gains | DECOMP |
| `FUN_180013d30`, RVA 0x252ED0 / 0x105CC0 | LFO shape normalisation, noise | DECOMP + EXEC |
| `FLEngine` `0xfe9d20`, `0x12bd700` | MIDI-volume event condition | DECOMP |
| `r46/run/kp.exe` dumps and peeks | initial state, flags, tables, per-sample internals | EXEC |
| `r46/battery_44k.log`, `r46/py/kcmp.py` runs | verification | EXEC + model |

### Open questions

- How FL applies the channel volume to Kepler (no MIDI-volume event, Final.Vol ignored).
- The DLL library's pow rounding inside the render (exact emulation not attempted).
- Whether FL re-sends the converted LFO1 rate of old synced states (round 33) — in the port it is applied at the
  sample-rate set-up that follows the load.
- Kepler-2 (Kepler Exo) layout and the features it enables (DCO2, cross-mod, filter types 1/2, saturation, EQ, delay,
  reverb, hyper chorus, mod matrix): not decoded in this round.
