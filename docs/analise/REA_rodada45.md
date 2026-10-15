# Behavioural analysis of FL Studio 2026: round 45 (REA) — Toxic Biohazard: the complete engine as a portable model

> **Nature of this document.** Continuation of `REA_rodada13.md` §5 (state format), `REA_rodada15.md` §12.2 (parameter
> order), `REA_rodada17.md` §9, `REA_rodada18.md` §8, `REA_rodada19.md` §4 (scales), `REA_rodada26.md` §2 (4-pole ladder)
> and `REA_rodada33.md` §2 (filter envelope), with the same goal as rounds 32/34/37/39: describe the whole engine precisely
> enough for LYNX to play Toxic Biohazard as an exact instrument instead of the 2-operator approximation
> (`synthFromToxic` in `src/core/FlGenerators.cpp`). *Clean-room* reverse engineering, behavioural only. No Image-Line
> code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched and
> no Image-Line content (code, audio, wave or spectrum data) was copied into the repository: the built-in wave spectra
> are read at run time from the user's own DLL (§2.1).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the official
> Image-Line documentation (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's own DLL,
> unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Inferences made
> *from* the code are stated explicitly as **inference**. Results of our own numerical model (which implements only what is
> marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line documentation (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target:** `Plugins/Fruity/Generators/Toxic Biohazard/Toxic Biohazard_x64.dll`, SHA-256
  `5c326637be38fcea7148e170b83e6bec8600c87f00fa774eb610ed68f136cb46` (= rounds 15/17/18/19/26/33; copy `r45/bin/Toxic.dll`),
  C++/MSVC, image base 0x180000000. RTTI: `CFLWrapper`, `CPluginRenderer`, `CSynthesizer`, `ToxicSynth`, `CVoiceGenerator`,
  `CToxicGenerator`, `CTapeDelay`, `AudioEffect`, `fxReverb`. VMTs (`r45/py/vft.py`): `ToxicSynth` `0x18008b950`,
  `CToxicGenerator` `0x18008b880`, `CFLWrapper` `0x180088b90`.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=4G`), project `/tmp/r45gh`, full decompilation `r45/out/all.c`
  (`r45/gh/imp.sh`, our `DecompAll.java`); helpers `r45/py/fn.py` (print/callers), `r45/py/kf.py` (constants used by a
  function), `r45/py/k.py`; `objdump -M intel` wherever Ghidra dropped CRT-call arguments (`powf` `0x180057f90`, double
  `pow` `0x1800568f0`, vector `sin` `0x18006b4b0`) or the float operation order mattered. The effects rack (§5) was decoded by
  a parallel sub-task from the same dumps, under the same rules (its own harness `r45/fx/fxrun.c`).
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r45/run/tx.c` → `tx.exe`, a copy of round 39's `gm.c` (same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified,
  minimal fake host, state restored through the plugin's own SaveRestoreState from a read-only `IStream`,
  `TerminateProcess` at the end, `timeout`). Additions: `TXNAMES` (calls the parameter-name function for all indices),
  `sp=<index>,<value>` (the engine's own setParameter, VMT slot 23), `tab=<k>,<file>` (dumps wave-table object k: spectrum
  and the 128 × 1024 coefficients), `ratefirst` (sample rate set before the state is restored, as FL does when it creates
  a channel), `pef=0,<bpm>` (tempo). All comparisons use `pe=1,0` (max-polyphony event 0), `panlaw` (host ComputeLRVol law A)
  and `ratefirst`. No process or dialog was left behind; outputs were deterministic run to run.
- **Models** (pure Python + numpy, `python3 -I`): `r45/py/toxmodel.py` (wrapper, engine, voices, oscillator bank, filter),
  `r45/py/toxwave.py` (wave tables), `r45/fx/toxfx.py` + `r45/fx/crtpow.py` (effects rack and the DLL's CRT float
  functions). Drivers: `r45/py/cmp.py`, `t1.py`, `t2.py`, `bat.py` (battery), `ccmp.py` (C++ port vs model),
  `mktest.py` (the unit-test state); effects: `r45/fx/suite.py`, `csuite.py`. Logs in `r45/out/` (`bat1.log`, `bat2.log`)
  and `r45/fx/` (`suite_full.txt`, `csuite.txt`).
- **Data:** `r41/out/blobs41.pkl` — the 3 Toxic Biohazard channels of the installation's demo projects ("Bombs Away ft
  KARRA – I'm Awake": *Toxic Biohazard*; "SeamlessR – Bass Antics Performance": *Saw*, *Saw #2*), copied to
  `r45/states/{bombs,saw,saw2}.bin`; 10 `.fst` presets and the `.tbio` preset folders of the installation.

---

## 0. Data survey **[DATA, `r45/py/survey.py`]**

| Channel | Operators on (wave) | FM cells (source → target) | Unison | Filter | Effects (slot types) |
|---|---|---|---|---|---|
| Bombs Away: Toxic Biohazard | 1 (sine), 2 (wave 15), 3 (wave 18) | 1→2 0.09, 1→3 0.27, 2→1 0.41, 2→2 0.19, 3→3 0.22 | 2 voices | bypass | slot 1 flanger (off), slot 2 delay (sync 2); EQ on |
| Bass Antics: Saw | all 6 (wave 5 ×5, sine) | 2→1 0.48, 3→2 0.18, 5→1 0.34, 6→2 0.26 | 8 | bypass | reverb + delay; distortion 0.25; EQ on |
| Bass Antics: Saw #2 | as Saw | as Saw | 8 | LP, cutoff 0.39, res 0.48 | reverb + delay; EQ on |

All three: every operator free-running (random start phase), polyphony 32 / 13 / 18, Seq Play off, no glide.

---

## 1. Structure, host interface, process **[DECOMP + EXEC]**

### 1.1 Wrapper and chunking
- The wrapper is the `CFLWrapper` of GMS (round 39 §1.3/§1.11): **TriggerVoice** sends voice event 6, takes
  `note = trunc(Init.Pitch·0.01)`, MIDI velocity `trunc(127·ln(1 + 10·Init.Vol)/ln 21)`, releases the note in the engine
  first if the key already holds another host voice; **Gen_Render** fills the per-note tables `T_L/T_R` (ComputeLRVol of
  Final.Pan/Final.Vol, held-note propagation) and `T = Final.Pitch·0.01 − note + 60`, then renders in chunks and kills the
  host voices whose engine voices ended **[DECOMP `FUN_180004980`, `FUN_180004810`, `FUN_180004190`]**.
- **Chunk = 64 samples** (engine buffer size − 4). **[EXEC]** the per-chunk smoothers step once per 64 samples
  (host blocks of 64/128/512 give 1/2/8 steps); host blocks of 512 and 1024 render identically, 508 does not.
- Engine note-on (`FUN_180007a20`): `vel = vel7·0.00787401572` (float), stored in the per-note table; 0 = note-off; then
  the `CSynthesizer` handler `FUN_1800250b0` (poly) or `FUN_1800256a0` (mono) **[DECOMP]**.
- The CSynthesizer keeps 256 voices (`+0x39b0`); the usable count is `min(min(P, Npoly)·1, 256)` with
  `P` = polyphony parameter (§2.3; `< 1` or `> 32` → 32) and `Npoly` = the host's max-polyphony event (ProcessEvent →
  `FUN_180024bb0`; value 0 → 32). Without that event `Npoly` = 0 and every note takes the first voice **[DECOMP + EXEC]**;
  LYNX sends value 0 (FL's own value at channel creation is **not confirmed**, as for GMS).

### 1.2 Creation order, defaults
- Constructor values of the 340 raw parameters were read from a fresh instance **[EXEC]** (`toxmodel.DEFAULTS`).
- **Rate set** (`ToxicSynth` slot 10 `FUN_180040ff0` → `FUN_180024e80`): smoothed copies jump to their targets, every voice
  is reset (glide position 48.0), the output gains jump to their targets, the last note is forgotten, the effects rack is
  re-rated and cleared (§5.0). The harness shows that a rate equal to the current one changes nothing; FL sets the
  project rate when it creates the channel and then restores the state, so the state's values are reached **through the
  smoothers** (output gain from the default 0.5 with 0.005/sample, filter values with the per-chunk smoother) —
  **[EXEC with `ratefirst`; FL's order itself is an inference]**.
- **SaveRestoreState** (`FUN_180041520`/writer `FUN_180041b00`): §2.1 layout; every parameter goes through the setter
  `FUN_18003dcc0` (§2.2).

### 1.3 Engine process (`ToxicSynth` slot 17, `FUN_1800406d0`), per chunk of n ≤ 64
1. Six smoothed values follow their double targets: `v ← (float)((T − v)·c + v)`, `c = (double)(float)(4410/fs)`, values
   within ±1e-9 → 0: cutoff `+0x3bd0`, resonance `+0x3bd4`, key-track `+0x3bd8`, velocity-track `+0x3bdc`, EG amount
   `+0x3be0`, LFO1 amount `+0x3be4`; targets other than the EG amount are first raised to 1e-7 (the double target itself is
   overwritten). The six operators' frequency shifts follow theirs the same way (double).
2. LFOs (§3.5): if no voice is active and the LFO's "free" flag (raw 297/299) is 0, its phase restarts at 0; rate =
   `20x³ + 0.01` Hz (parameters 13/14, computed `(double)(x·x·x)·20 + 0.01`) or, with sync mode m = 1…9 (raw 296/298),
   `tempo·{1/15, 1/30, 1/45, 1/60, 1/120, 1/180, 1/240, 1/480, 1/960}` Hz; `inc = rate/fs`; `phase += n·inc`, minus 1 when
   > 1.
3. Modulation slots of type 2/3 (LFO sources) re-apply their target parameter (§2.4).
4. The voices render, in list order, **adding** into the chunk buffers (§3).
5. Effects rack (§5): `FUN_180038510(tempo)` then `FUN_1800382f0`.
6. Master gain: per sample `out ·= g` (current value), then `g ← (float)((double)(float)(G − g)·0.005 + g)` per channel,
   `G = powf(10, 3·P0 − 1.5)·CC7·0.5` (0.5 → 0 dB·0.5 = −6 dB; CC7 = 1 since LYNX sends no CC; CC10 = 0.5 → centred)
   **[DECOMP `FUN_180025c90`, `FUN_18003e510` case 0 + objdump `0x18003e5a5`]**. Note the difference is taken in float:
   the smoother settles a few ulps away from G (EXEC: 1.0000088·G for P0 = 0.241).

---

## 2. State and parameters

### 2.1 State layout **[DECOMP `FUN_180041b00`/`FUN_180041520` + DATA]**

`"SSLF"` + u32 size (0x273A) + tag `TBi2` (also `TBio`; `TBiD` when the host is in demo mode) + **CC header** (8 × i16 source
type, 8 × i16 target parameter, 8 × i32 raw amount = 0x40 bytes) + **8 drawn spectra** (operators 1–6, LFO 1, LFO 2; each 128
f32 harmonic amplitudes + 128 f32 phases in cycles = 1024 bytes) + **340 f32 raw parameters** (§2.2) + 294 bytes (one MIDI
CC per parameter, 0xFF = none) + 128-byte preset name; 10 050 bytes. The loader also skips an optional 0x280-byte prefix
(older layout, not present in the data; not modelled).

### 2.2 Parameter laws (setter `FUN_18003dcc0` and its helpers) **[DECOMP + EXEC]**

`acb(x, lo, hi) = clamp(trunc((float)(hi − lo)·x + lo), lo, hi)` (`FUN_18003acb0`); `T(x) = max(50·x⁴, 0.00101)` s in
double (`FUN_18003d480`, as round 33). Every named parameter (< 294) first goes through the modulation function (§2.4),
which also clamps it to 0…1.

| Index | Name | Law |
|---|---|---|
| 0 | Gain | `powf(10, 3x − 1.5)` (×0.5 at the output, §1.3) |
| 1 | Distortion | rack, `20x²` (§5.2) |
| 2 | Transpose | `acb(x, −24, 24)` semitones |
| 3, 4, 6 | Master A, D, R | `T(x)`; R = `0.35·T(x) + 0.02` |
| 5 | Master S | `x²` |
| 7 | Vel. Curve | exponent `max(2x + 1, 0)`: voice velocity = `powf(vel, 1 + 2x)` |
| 8 | Unison Dtn | x (§3.3) |
| 9 | Unison Pan | clamp(x) (§3.3) |
| 10 | Glide Time | `x³`, below 1e-4 → 1e-4 s |
| 11 | EGF to Pitch | `acb(x, −24, 24)/12` octaves per unit of filter envelope |
| 12 | Lfo2 to Pitch | `x²/12` octaves per unit of LFO 2 |
| 13, 14 | Lfo1/2 Speed | `20x³ + 0.01` Hz |
| 15–18 | Flt Cutoff, Res, KeyTrk, VelTrk | x (smoothed, §1.3) |
| 19, 20, 22 | Flt EG A, D, R | `T(x)` |
| 21 | Flt EG S | `x²` |
| 23 | Flt Eg Amnt | `sign(2x − 1)·(2x − 1)²` |
| 24 | Flt Lfo1 Amnt | `(x·√x)²` = x³ |
| 25–30 | O1…O6 Enable | x > 0.5 |
| 31–36 / 37–42 | Coarse / Fine | `acb(x, −99, 99)` semitones / `acb(x, −99, 99)·0.01` semitone |
| 43–48 | Frq Shift | `(2x − 1)⁵·4000` Hz (double) |
| 49–54 | Free Run | x ≠ 0 → random start phase (§3.2) |
| 55–60 | Phase | x cycles (start phase when not free-running) |
| 61–66 | VelSens | x |
| 67–72 | Init | `x²` (level at start and the release goal of the operator envelope) |
| 73–96 | Atk, Dec, Sus, Rel | `T(x)`, `T(x)`, `x²`, `T(x)` |
| 97–102 / 103–108 | Pan / Mix | x / `x²` |
| 109–144 | "Oi Fm j" = 109 + 6(j−1) + (i−1) | cell `(trunc(100x)·0.01)²`; **operator i modulates operator j** (§3.6) |
| 145–150 / 151–156 | LFO1/LFO2 → Osc | operators 1 and 5: `acb(x, −99, 100)·0.01`; operators 2, 3, 4, 6: `(−99 − trunc(−199x))·0.01` (an inlined variant that rounds differently below 0.5) |
| 157–226 | Sequencer | not modelled (§7) |
| 227–293 | EQ, effects | §5.1 |
| 295 | (internal) bend range | `acb(x, 0, 12)` semitones |
| 296 / 298 | LFO1/2 sync mode | raw int 0…9 |
| 297 / 299 | LFO1/2 free | raw ≠ 0: the phase keeps running while no voice plays |
| 300 | filter mode | raw int, ≤ 3: 0 bypass, 1 LP, 2 BP, 3 HP (round 19) |
| 301 | polyphony | raw int; 0 = **mono** (`FUN_18003d540`) |
| 302 | unison | raw int + 1, clamped 1…8 |
| 303 | glide mode | raw int: 0 "Bend VT", 1 "Held VT", 2 "Bend ST", 3 "Held ST" (names from the manual, [DOC `…/plugins/ToxicBio_master.htm`]) |
| 304 / 305 | delay sync, slot 1 / 2 | §5 |
| 306–313 | wave of operators 1–6, LFO 1, LFO 2 | raw int 0…32 (§2.5); other values leave the table unchanged |
| 314 / 315 | effect type, slot 1 / 2 | §5 |
| 316–323 / 324–331 / 332–339 | modulation slots: source type / target parameter + 1 / amount `acb(x, −99, 100)·0.01` | §2.4 |

### 2.3 Operator record (0x60 bytes ×6 at `+0x3b10`) **[DECOMP `FUN_18003e730`, `FUN_18003eb20`, `FUN_18003efa0`, `FUN_18003f3f0`, `FUN_18003f840`, `FUN_18003fc30`]**
coarse, fine, frequency shift (target + smoothed, double), mix, pan, phase, free-run byte, enable byte, velocity
sensitivity, init, A, D, S, R, six FM cells (to targets 1…6), LFO 1/2 amounts.

### 2.4 Modulation slots ("MIDI CC" matrix) **[DECOMP `FUN_1800413f0`, `FUN_18003a3c0`, `FUN_1800406d0`; EXEC]**
Eight slots (type, target, amount). When a named parameter is set, every slot with type ≠ 0 aimed at it adds
`source·amount`, then the value is clamped to 0…1. Sources: 1 = the velocity of the last started voice (`+0x3c58`), 2/3 =
LFO 1/2 at the engine's own phase, 4/5 = aftertouch/pitch bend (0 in LYNX), 7…134 = MIDI CC 0…127 (LYNX sends none: the
constructor's CC7 = 1 and CC10 = 0.5 remain) **[EXEC: CC table peeked]**. The target is re-set (from its raw value) at
every voice start (all types) and at every chunk (types 2/3). Demo states: Saw/Saw #2 carry slots with CC1 and type 4
sources (0 in LYNX and in FL without those controllers).

### 2.5 Waves **[DECOMP `FUN_1800421c0`, `FUN_18002fa50`; EXEC `tab=` dumps]**
- **Wave 0** = the slot's drawn spectrum from the state; **waves 1…32** = 128-harmonic spectra (amplitudes, phases in
  cycles) assembled from constants of the DLL's `.rdata`; wave 1 is the plain sine (amplitude 1 on harmonic 1).
  **[EXEC]** all 32 dumped spectra are made of 16-byte pieces that occur verbatim in the DLL file (1984 pieces, 259 of them
  zero): LYNX keeps only the **file offsets** of those pieces (`src/core/ToxicWaves.inc`, generated by `r45/py`) and reads
  the spectra from the user's own DLL at run time after a SHA-256 check (no spectrum is stored in the repository).
- **Table construction:** for harmonic h = 1…128 in order, `buf[n] = (float)(a_h·sin(n·h·2π/256 + 2π·φ_h) + buf[n])` for
  n = 0…255 (double sine; the DLL's vector sine), snapshot after each harmonic; **level L (0…127) = the snapshot after
  harmonic 128 − L** (level 127 = the fundamental only). Each level stores 256 segments of 4 coefficients (Catmull-Rom of the
  periodic 256-point wave): `c0 = y0`, `c1 = (y1 − y−1)/2`, `c3 = (y2 − y−1)/2 + (y0 − y1)·1.5`, `c2 = (y−1 − y0) + c1 − c3`
  (float). **[EXEC + model `toxwave.py`]** bit-identical to the DLL's tables (all 128 × 1024 coefficients) for waves 1, 2, 6,
  12, 20, 24, 32 with numpy's sine.
- **Level choice** for an operator (`FUN_18002f9f0`): `f > fs/256` → `L = clamp(128 − trunc(fs·0.5/f), 0, 127)`, else 0;
  `f` = the operator's first unison lane frequency including the frequency shift.

---

## 3. The voice (`CToxicGenerator`) **[DECOMP + EXEC]**

### 3.1 Notes, allocation, glide (`FUN_1800250b0`, `FUN_1800256a0`, `FUN_180026980`)
- **Poly note-on:** glide start = the last note if any (−1 after a rate set) else the key; glide modes 1/3 ("Held"): the
  key, or the nearest other held key (smallest |Δ|, lowest first). Voice choice among the first `count` voices (§1.1): the
  first free one; else the oldest released one if the oldest-released candidate is released, else the oldest held one (the
  candidates start at the list head; counters compared unsigned). The voice gets a fresh counter, the note, random detune 0
  (the RNG `0x18009ee80` is still advanced; scale `+0x38cc` is 0), velocity `powf(vel, curve)` (**the DLL's CRT powf**,
  §5.8), glide position = start (or the note if glide < 0.001), glide rate `12/(fs·glide)` semitones per sample (modes 0/1)
  or `|note − start|/(fs·glide)` (modes 2/3), then the start (§3.2).
- **Mono** (polyphony 0): always the first voice; if it was free or released it restarts (velocity, glide start; glide
  modes 1/3 start at the key), otherwise it only changes note and glide rate (legato, no restart). Note-off: the voice moves
  to the nearest still-held key (glide rate `12/(fs·glide)`) or is released.
- **Poly note-off:** every voice with that note is (re-)released.
- **Glide** per 128-sample block: the position moves by `128·rate` towards the note and stops on it.

### 3.2 Start, release, reset (`FUN_18003a3c0`, `FUN_18003a660`, `FUN_18003a730`)
- Start: sets the modulation velocity source, re-applies the modulation targets (§2.4); a voice whose elapsed time is
  not 0 (it had been running) gets `trunc(fs·4/44100)` blocks of fast fade (rate `147/fs`, elapsed time frozen; round 33);
  latches master A, D, S (A, D ≥ 0.001), filter A, D, S, unison count, per-operator enable, init, A, D (≥ 0.001), S,
  velocity sensitivity, and the engine's two LFO phases.
- Release latches master R (≥ 0.01), filter R and the operators' R (≥ 0.001) and starts the release counter.
- Reset (also at the end of a voice): note −1, filter states/weights cleared (weights to the "unset" marker −20), cutoff
  ramp marker −1, envelopes 0, start flag set, block counter 128.
- **Operator lanes at the first block:** for every latched operator, for every unison lane, in operator order:
  `FUN_180029ec0` — the generator `s ← s·0x0bb38435 + 0x3619636b` (`0x18009ee84`, starts at **22222** when the DLL loads,
  shared by every instance in the process) advances once per lane; free-running lanes take `s` as their 32-bit start
  phase, others `(uint32)(φ·4294967295)`; the envelope starts at Init (≥ 1e-7), increments 0, FM input 20.0.
  **[EXEC]** the demo channels (all free-running) are bit-exact with a generator started at 22222 in a fresh process.

### 3.3 Per-block update (every 128 samples; `FUN_180039880`, `FUN_180039b30`)
Time base: elapsed time and release counter advance by `128/44100` s per block **at any rate** (round 33).
1. Glide; oscillator-bank block constants (`44100/fs`, level-ramp position 0, step `1/128`).
2. Voice LFOs: phase `+= 128·inc` (minus 1 when ≥ 1), value = level 0 of the LFO's wave table at `(float)phase·255` (§3.5).
3. First block: lane initialisation (§3.2).
4. Smoothed velocity: first block = velocity, then `(float)((double)(vel − v)·0.05 + v)` (always equal here).
5. Master envelope stage (§3.8); a voice whose release counter exceeds its release time is reset here.
6. **Operators** (in order, latched ones only), lanes numbered consecutively over the latched operators
   (`op rank·unison + u`):
   - gains: `L = clamp(1 + s_u)·T_L[note]·0.5·2(1 − pan)·mix`, `R = clamp(1 − s_u)·T_R[note]·0.5·2·pan·mix`,
     `s_u = (2u/(U − 1) − 1)·UnisonPan` (0 for U = 1);
   - envelope stage (§3.4);
   - level `ℓ = (float)((l1·LFO1 + 1 + l2·LFO2)·((v − 0.5)·VelSens + 0.5))` (double expression, float velocity term);
     an operator switched off since the start gets 1e-5. A 4-value history gives a **Catmull-Rom ramp** across the block
     from the value of two blocks ago to the previous one (first block: from 0 to ℓ);
   - pitch: `f = 2^(LFO2·Lfo2toPitch + (fine + coarse)/12 + env·EGFtoPitch) · 220·2^((p − 57)/12) · r^(−(U − 1)/2)`,
     `p` = transpose + glide position + the host pitch table T[note] (+ bend, 0), `r = (float)(1 + UnisonDtn/(12U))`
     (`powf`), lane u frequency `f·r^u + shift` (r^u by repeated float products) — the unison is **geometric around the
     centre**; the frequency shift is added in Hz after the ratio;
   - FM routes: for every latched target j with cell > 1e-5: lane u of this operator → lane u of operator j, amount 2·cell.
   - The number of lanes is padded to a multiple of 4 with silent lanes; **each padding lane also advances the phase
     generator** (every block) **[DECOMP + EXEC: with this, Bombs Away (6 lanes + 2 padding) is bit-exact]**.
7. Filter stage and targets (§3.7); elapsed time += 128/44100 unless fading; a voice without lanes is reset.

### 3.4 Operator envelope (`FUN_1800394d0`)
Fading: goal Init, rate `147/fs`. Held: elapsed < A → goal 5.0, rate `8.82/(39.441975·A·fs)`; else goal S, rate
`8.82/(D·fs)`. Released: **goal Init** (not 0), rate `8.82/(R·fs)`. Per sample `e ← (goal − e)·rate + e`, clipped at 1
(the operator uses the value before the update). Same law as the filter envelope of round 33.

### 3.5 LFOs (`FUN_180030110`)
Value = cubic segment `i = clamp(trunc(255·φ), 0, 255)` of level 0 of the LFO's table (`φ·255`, not 256 — the last segment
is stretched). Engine phases (§1.3) feed the modulation slots; voices copy them at start and advance their own per block.

### 3.6 FM direction (corrects round 18 §8)
`FUN_180039b30` adds, for the lane of operator **i**, routes to the lanes of every operator **j** with the cell
`+0x40 + 4j` of operator i's record — and operator i's record holds the parameters "O(i+1) Fm 1…6" (`FUN_18003e730` for
operator 1: indices 109, 115, …, 139). So **"Oi Fm j" (parameter 109 + 6(j−1) + (i−1)) is operator i modulating operator
j** — the opposite of round 18's reading **[DECOMP + EXEC: the FM demo states are bit-exact]**. Self-modulation (i = j)
is allowed (Bombs Away 2→2, 3→3).

### 3.7 Oscillator bank render (`FUN_180029fa0`, 4 lanes at a time)
Per sample, per lane: `read = (int)(16777215f·M)·256 + phase` (32-bit wrap; M = FM input, 20.0 = no modulation: 20·2^24
rounds to an exact number of cycles in float); `phase += inc; inc += Δinc` (the increment ramps linearly over the block
from its previous value to `trunc(f/fs·(2^32 − 1))` with `f ≤ 0.49·fs`; `Δinc` = integer division by 128, so the first
block of a note glides up from 0 Hz); segment = top 8 bits, `t = (read & 0xFFFFFF)·2^−24`; wave =
`((c3·t + c2)·t + c1)·t + c0`; `y = (wave·e_old)·level(τ)` with `level(τ) = ((c3τ + c2)τ + c1)τ + c0`, τ = block position;
envelope update; gains smoothed with `0.001·44100/fs` (snap from 0 at the first use), the old gains are used; the left/right
sums per group of 4 are `((g1y1 + g0y0) + g2y2) + g3y3`, groups added in order. After the sample: every M back to 20.0,
then the routes add `out_src·amount` in route order (one-sample delay for all FM).

### 3.8 Filter, envelopes, amplitude (`FUN_1800395b0`, `FUN_180038d00`) — rounds 26 and 33, completed
- Target (per block): `u·65535` with `u = ((pitch − 60)·0.01·KT + VT·(v − 1)·0.5) + (Lfo1Amt·LFO1 + Cutoff)` — **the
  order of the sum matters** (asm `0x18003968c`); the ramp starts at the target at the first block; resonance
  `clamp(Res, 0, 1)` per block; EG amount `·65535` (float).
- Per sample, when β < 0.999998987: index `trunc((double)(e·E_A) + U)` clamped to 0…65535 (table of round 26 §2.1); the
  feedback is **`s4·(res·R)`** (asm `0x180038ef6`; not `(s4·res)·R`); saturation and stages as round 26; output
  `((c0·u + c1·s3) + c2·s4)·(1 − β) + β·x`. Bypass: no filtering and **the cutoff ramp does not advance**.
- Filter envelope as round 33 (decay goal `S + 1e-7`, attack goal 5, release goal 0); weights and β follow their goals at
  0.005 per sample (weights within ±1e-9 → 0), jump at the first block.
- Master envelope (`FUN_1800393b0`): attack goal 5.0 with `8.82/(39.441975·A·fs)`, decay goal S (≥ 1e-7) with
  `8.82/(fs·D)`, release goal 1e-7 with `8.82/(R·fs)`; the output sample is `filtered·amp_old` added into the chunk.

### 3.9 Voice end
At a block start: released and release counter > master R → reset (the wrapper then kills the host voice once the key is
no longer held). Reset voices are skipped by the render.

---

## 4. Verification of the voice engine **[EXEC + model]**

Error = `20·log10(rms(model − DLL)/rms(DLL))`; "−999" = bit-identical (max |diff| = 0). Notes (unless stated): C5
Init.Vol 0.78 0–30000, E5 0.6 11025–40000, G5 0.85 22050–52000, C4 1.0 60000–70000, 2 s, host blocks 512, `panlaw`, tempo
125, max-polyphony event 0, rate set before the state. "fx off" = distortion, EQ and both slots off in the state (the
always-on DC blocker stays). `r45/out/bat2.log`.

| Case | Result |
|---|---|
| Bombs Away / Saw / Saw #2 (fx off), 44.1 kHz | bit-identical ×3 |
| Saw #2 48 kHz; Bombs Away 96 kHz; Saw #2 host block 1000 | bit-identical ×3 |
| Saw #2 BP / HP; resonance 1 + key-track 1 + velocity-track 1 | bit-identical ×3 |
| Saw #2 LFO1 → cutoff; LFO1 synced (mode 4); LFO waves 9/20 → cutoff and operator | bit-identical ×3 |
| Bombs LFO → operators; LFO2 → pitch; EG → pitch; velocity curve 1 + velocity sensitivities | bit-identical ×4 |
| Saw #2 glide 0.3 modes 0/2/1 ("held"); mono; mono + glide mode 3; polyphony 2 (stealing) | bit-identical ×6 |
| Saw #2 unison pan 1 + detune 1; Bombs frequency shift; transpose −12; fixed phases; waves 6/12/24 | bit-identical ×5 |
| Saw #2 modulation slots: velocity → cutoff; LFO1 → cutoff | bit-identical ×2 |
| Saw #2 long releases; Bombs gain 0.8; Final.Vol 0.7 / pan −0.5 / +30 cents and pan 0.4 / −50 cents; same key retriggered | bit-identical ×4 |
| **Full chain** (effects as saved): Saw, Saw #2, Bombs Away at 44.1 kHz; Saw #2 48 kHz; Bombs 48 kHz block 1000; Saw 3 s tail; Saw #2 with chorus + phaser; Bombs flanger on + lo-fi + distortion 0.6 | see §6 |
| The unit-test state (`mktest.py`: drawn waves, unison 3, glide, velocity slot, chorus + synced delay, EQ, distortion), full chain, tempo 140 | bit-identical |

---

## 5. The output effects rack **[DECOMP + EXEC]** (sub-task; `r45/fx/`)

### 5.0 Structure and order
- **Rack object** (engine `+0x3c70`, constructor `FUN_1800381f0`): slots at `+0`/`+8`; EQ-off byte `+0x10` (starts at 1),
  EQ at `+0x18`; distortion `+0x3e0` d / `+0x3e4` target; DC tracker `+0x3e8`/`+0x3ec`; tempo `+0x3f0` (double, 120);
  silence logic `+0x3f8` sleeping, `+0x3f9` loud, `+0x3fc` counter, `+0x400` limit.
- **Chain per engine chunk** (`FUN_1800382f0`): distortion + DC blocker → slot 1 → slot 2 (`FUN_1800281b0`) → EQ
  (`FUN_180029880`, unless off) → silence logic.
- **Rate set** (`FUN_1800384b0`): limit = `(int)(fs·2.1)`; both slots re-rated (`FUN_180027d40`); EQ redesigned and cleared.
  On an engine rate change (`FUN_180040ff0`) the rack is also cleared (`FUN_180038450`: slots reset, EQ states cleared, DC
  states 0, d = target, **sleep state entered**).

### 5.1 Parameter map (`FUN_180040370` → `FUN_1800292d0`)
| Index | Effect |
|---|---|
| 1 | distortion target `x²·20` |
| 227 | EQ off ⇔ 0; every write also clears all EQ states |
| 228–235 | band gain dB `±y²·18`, `y = ((v − 0.5) + v) − 0.5` (double) |
| 236/237 | slot enable (`FUN_180028190`); turning on sets the wet gain w = 1 |
| 238–293 | slot fields (even → slot 1, odd → slot 2), all clamped ≥ 0, reverb mix/decay ≤ 1; every write recomputes (`FUN_180028d70`) |
| 304/305 | delay sync field |
| 314/315 | pending type, applied at the next process call (`FUN_180027d90`); **the user value is the internal type**: 0 delay, 1 chorus, 2 reverb, 3 flanger, 4 phaser, 5 lo-fi |

Demo states: Bombs Away = slot 1 flanger (off), slot 2 delay with sync 2; Saw / Saw #2 = reverb + delay. The constructor
leaves slot 1's pending type at 1 **[EXEC]**. Manual: the chain is "INPUT – Distortion – Effect 1 – Effect 2 – Equalizer –
OUTPUT" **[DOC `…/plugins/ToxicBio_effects.htm`]**.

### 5.2 Distortion + DC blocker (`FUN_180038550`)
Per sample: loud flag if |L| or |R| > 1e-5 at the input; `d ← (float)((double)(T − d)·0.01 + d)`; if d ≥ 0.001 or T ≥ 0.001
(double compare): `g = (1 − d/16)⁸` (three float squarings), gain `(float)(g·0.7 + 0.3)`, `x ≥ 0: y = 1 − powf(10, −d·x)·(1 − x)`,
`x < 0: y = powf(10, d·x)·(x + 1) − 1`, `y·gain`; otherwise d **and T** become 0. DC blocker, always on: `m += (y − m)·0.003`
(±1e-9 → 0), out = y − m (a ~21 Hz one-pole high-pass at 44.1 kHz; the coefficient does not follow the rate).

### 5.3 Tape line (`CTapeDelay`: `FUN_180026140`, tick `FUN_180026400`, reset `FUN_180026340`)
Two halves of `n + 256` floats + `n + 256` doubles (n = max(4, size)), the storage scheme of GMS's tape line (round 39
§1.13) with: reset puts the position at the constructor's phase argument; minimum speed 1/n. Setters: time
(`FUN_1800267a0`) d ∈ [0, 0.99], m = 2/n, speed target `1/((d(1 − m) + m)·n)`; gain target; feedback; LFO increment r/fs
(r ≥ 0); depth ∈ [0, 1]. Tick: input ±1e-9 → 0; triangle LFO t = 2φ or `((φ − 0.5) + φ) − 0.5`, speed factor
`max(((3 − 2t)·t)·(depth·t) + 1, 1)`, φ wraps by its integer part; linear read of the previous cycle as GMS; speed =
clamp(factor·smoothed speed, ≥ 1/n, ≤ 0.2); stored = out·fb + x; **returns out·g_s with the gain before this sample's
update**; halves swap at wrap carrying three entries; `g_s ← (float)((double)(g_t − g_s)·0.01 + g_s)`, speed smoother 0.005
(double). The feedback-path filters exist but are never enabled **[DECOMP]**.

### 5.4 Slot types (`FUN_180027d90` build, `FUN_180028d70` laws, `FUN_1800281b0` process)
Changing the type rebuilds the lines with defaults (time 0.5, gain 1, feedback 0, increment 0, depth 0), then reset and
recompute. Reset (`FUN_180027c50`): input fade g = 0; w = bypass ? 0 : 1; lines, phaser, reverb and lo-fi states cleared;
lo-fi counter 0; phaser phase 0, smoothed frequency/mix −1. Gate: bypassed and w == 0 → return; bypassed and w < 1e-5 →
reset and return. g → 1 at 0.005 per sample; while bypassed, w decays by w·0.005 per sample.

| Type | Lines (size / start phase) | Laws | Per sample |
|---|---|---|---|
| 0 Delay | N = (uint)(2fs): N/0, round(0.63N)/0.27, round(0.41N)/0.40, N/0.30, round(0.76N)/0.62, round(0.54N)/0.87 | time (float)(p266² + 0.01); fb p268·(−0.99); inc 20·p270²/fs (×1.1 on lines 2/4/6); depth p272⁴; gains mix², mix²·blur, blur²·mix² (mix p276, blur p274) | L through lines 1→2→3 in series: x += tape(x·g)·w; R on lines 4–6; **sync** (p304/305) 1…9 ⇒ x = tempo·{1/15 … 1/960}, other non-zero ⇒ x = 1; time (float)(0.5/x) on all six lines every chunk; sync 0 keeps the last value until the next recompute |
| 1 Chorus | b = (int)(0.02fs): round(1.1b)/0, round(0.854b)/0.467, round(b)/0.778 | r = (float)(5·p254 + 0.1); inc r·{0.896, 1, 0.689}/fs; gain −p256²; depth p252³ | in = (R + L)·g (g updated first); y1, y3, y2 ticked in that order; L += (y2 + y1)·w, R += (y2 + y3)·w |
| 2 Reverb | — | fxReverb 9 ← \|1 − p262\|, 8 ← (float)(p260³·0.6), 12 ← (float)(p258·0.999 + 0.001), 2 ← p264³, 5 ← 1 | while on, w = 0x36a7c5ac (≈5e-6) and g = 1 (switching it off clears it at once); in place |
| 3 Flanger | n = (int)(0.012fs) ×2, phase 0 | time p238²; fb (float)(0.99·p240), 0 below 1e-4, negated if p248 ≠ 0; inc 10·p244²/fs (×1.1 on R); depth p242²; gain p246 (negated if p250 ≠ 0; < 1e-4 → 1e-4) | L += tape(L·g)·w; R likewise |
| 4 Phaser | 6 all-pass stages per channel | lo (float)(50/fs), hi (float)(20000/fs); K = (p288²)·(p288·0.5); fb (float)(powf(p290, 1/3)·0.98); frq max(p284², 1e-5); inc (float)(44100·p286²/fs)·0.0005; mix max(p292, 1e-5); smoothers start at the target | x = g·in − 1e-6 (+ fb·last); c = cosf(phase), phase += inc (wraps at 2π); w_f = min((s_frq + (c + 1)K)(hi − lo) + lo, 0.499); a = (1 − w_f)/(w_f + 1); stage t = s − a·x, s = t·a + x; out += t₆·s_mix(old)·w; s_frq, s_mix smoothed 0.001 (double) |
| 5 Lo-fi | — | Q = (float)(int)(1024·p278⁵ + 4); hold lim = (int)(40·p280); low-pass f = min((float)(2^(9.3·p282)·30), fs·0.47619) Hz, q = 1 (GMS's LP biquad, `FUN_18001c3a0`/`FUN_18001c4a0`) | w ≈ 5e-6 as the reverb, g = 1; counter 0 → hold = (float)(int)(Q·x)/Q; counter++ and wraps past lim; **out = x + LP(hold)** |

### 5.5 Reverb (`fxReverb`, the library of GMS's reverb)
As round 39 §1.13 except: the host rate is passed in (it does not stay at 44.1 kHz); output in place
`out = x·dry + (df + sm)` with dry = 1; **early reflections active** — level db(−35)·0.5, a power-of-two buffer of
2^(bitlen((int)(0.3163·fs)) + 1) samples fed from the first input all-pass, taps from two 8-entry tables scaled by
`((p4 + 3)·fs/103)·3.40136e-5`, but only tap 0 has a non-zero gain (left tap 1102, gain min(L0, R0)·0.5/L0; right tap 837,
gain min·0.36/R0); tank output `a = lev·tank_L + er·tap_L`. The first recalculation zeroes the lines and the reflection
buffer; resets fill everything with 1e-30. Toxic sets only indices 2, 5, 8, 9, 12; the rest stay at the default preset.
Why only tap 0 has a gain is **not confirmed** (the behaviour itself is verified).

### 5.6 EQ (`FUN_1800295b0`, `FUN_1800297c0`, designer `FUN_18001c590`, tick `FUN_18001c930`)
8 bands × 2 channels, f = 62.5·2^b Hz, bandwidth 1 octave; band 0 low shelf, 1–6 peaking, 7 high shelf **[EXEC peek]**;
the designer and tick are GMS's EqBand (A = powf(10, dB·0.05), w = (float)(f·6.283185/fs) clamped to [0, 3], α from
sinh(0.3465736·bw·w/sinf w)); each band processes the whole chunk in turn. Manual: "8 band graphic EQ with +/- 18 dB range"
**[DOC `…/plugins/ToxicBio_effects.htm`]**.

### 5.7 Silence / sleep
Loud input → counter 0, awake. Quiet and asleep → slots and EQ skipped (distortion and DC still run). After processing,
any |out| > 1e-5 resets the counter; otherwise counter 0 → 1, then counter += n; not loud, counter > `(int)(2.1·fs)` and
awake → rack clear (§5.0, asleep). Every rate set also enters the sleep state.

### 5.8 CRT math
The DLL's MSVC CRT float functions (SSE2 paths; the AVX flag `0x1800a14d4` was 0 on the test machine) are **not correctly
rounded**: ~0.1 % of `sinf`/`cosf` and ~0.008 % of `powf` results differ from correct rounding by one ulp. All five used by
the rack are emulated from their decoded algorithms (tables of 1/c, ln(1 + j/256), 2^(i/64) recomputed, equal to the
binary's): `powf` `0x180057f90` (257-entry log table + cubic, or a near-1 series; exp via 2^(k/64) + cubic) — 0 mismatches in
1.6 M calls; `sinf` `0x18006edd0` / `cosf` `0x18006db40` (Cody–Waite + Taylor in double) — 0 / 430 k each; `expf`
`0x18006e440` — 0 / 400 k; `tanf` `0x18006fa20` — 0 / 330 k **[EXEC `r45/fx/powt.exe`]**. Without the `powf` emulation the
Saw states drift to −80 dB through the reverb/delay feedback. The voice engine uses the same `powf` (gain, velocity curve,
unison ratio). Double `sinh`/`pow` (`0x18006f4c8`, `0x1800568f0`) differ from glibc by about one double ulp but matched after
the float rounding on 200 k arguments each (not proven for all arguments).

### 5.9 Verification of the rack
Python model vs the DLL's own rack called on test signals (`r45/fx/suite.py`, `suite_full.txt`): **178/178 cases
bit-identical** — distortion 0/0.25/1; flanger (incl. inverted feedback/mix, slot 2); chorus; delay; delay sync 2 @ 130 BPM,
sync 7 @ 97.3 BPM, sync switching; phaser; lo-fi; reverb; EQ; enable/bypass/type/EQ toggles mid-stream; the three demo
states; mid-stream rate changes; silence/sleep (5 s with 3 s of exact silence) — at 44.1 and 48 kHz and chunks of 64, 128,
37 (and 512/1000 for the distortion). C++ `ToxicFxRack` vs DLL (`csuite.py`): 176/176 bit-identical (the two mid-stream
rate-change cases are not reachable through the public API).

---

## 6. Full chain **[EXEC + model]**

Same notes and settings as §4, effects as saved in the state (`r45/out/bat2.log`).

| Case (full chain) | Result |
|---|---|
| Saw, Saw #2, Bombs Away, 44.1 kHz | bit-identical ×3 |
| Saw #2 48 kHz; Bombs Away 48 kHz, host blocks of 1000 | bit-identical ×2 |
| Saw with a 3 s reverb/delay tail | bit-identical |
| Saw #2 with chorus (slot 1) + phaser (slot 2); Bombs Away with the flanger switched on, lo-fi in slot 2 and distortion 0.6 | bit-identical ×2 |
| The unit-test state (`mktest.py`), tempo 140 | bit-identical |

**Result: the model reproduces FL's DLL bit for bit on all 3 demo channels (voice only and full chain, 44.1/48/96 kHz)
and on every synthetic case run (41 battery renders + the unit-test state).**

---

## 7. Not modelled / not confirmed

- **Sequencer** (Seq Play/Swing/P-P/Rnd/Dual/Steps/Pattern, parameters 157–226; `FUN_18001bbb0`, `FUN_18002ed20`): off in
  every demo state; LYNX plays notes directly (a state with Seq Play on is imported with its sequencer ignored).
- **MIDI controller sources** of the modulation slots (CC, aftertouch, pitch bend) and the bend range: LYNX sends none, so
  they are 0 (CC7 = 1, CC10 = 0.5); FL delivers channel pitch through Final.Pitch, which the engine receives through the
  wrapper's pitch table (the same path as GMS). Sustain pedal (CC64) not modelled.
- **Phase generator** of free-running operators: process-global in FL (one LCG for every Toxic instance, started at 22222
  when the DLL loads); LYNX gives each instance its own generator started at 22222 — exact for the first instance of a
  fresh FL process, different from FL in general **by design**.
- FL's **max-polyphony event** value at channel creation and FL's **order** of rate set vs state restore (both taken from
  the GMS findings: event 0 → 32 voices; rate first) — **inference**, not observed in FL itself.
- The optional 0x280-byte state prefix (`FUN_180041520`) and the `TBiD` demo-mode tag are parsed but not seen in data.
- CRT special paths (§5.8) and the AVX/FMA CRT paths are not emulated.

---

## 8. For LYNX — implementation and port status

- **`src/core/Toxic.{h,cpp}`** — a 1:1 port of `toxmodel.py` (+ `toxwave.py`): `ToxicInstrument` (InstrumentKind::toxic,
  appended at the end of the enum), engine, voices, oscillator bank, filter, wave tables (built off the audio thread and
  cached by spectrum), host interface as the GMS front-end (Init.Vol from LYNX velocity, per-note level tables recovered
  from LYNX's channel gains, max-polyphony event 0, the song tempo), chunks of 64.
- **`src/core/ToxicFx.{h,cpp}`** — the rack (`ToxicFxRack`) with the CRT `powf/sinf/cosf/expf/tanf` emulation; the voice
  engine shares `toxicPowf`.
- **`src/core/ToxicWaves.inc`** — file offsets of the built-in spectra's 16-byte pieces in the decoded DLL build; the
  importer stores the path of the user's `Toxic Biohazard_x64.dll` in `ChannelDef::samplePath` and the engine reads the
  spectra from it after the SHA-256 check. States that only use wave 0 (drawn) or wave 1 (sine) play without the DLL; other
  states fall back to the old 2-operator conversion (whose FM direction is corrected here, §3.6) when the DLL is missing.
- `tools/toxicrender.cpp` renders a state exactly like the model's `run` (verification harness).
- Tests (`tests/tests.cpp`): `toxic_engine` checks a state written from scratch (our own drawn spectra + sine; same bytes as
  `r45/py/mktest.py`) against golden samples of the model (which is bit-identical to the DLL on that state) and the
  instrument against the reference renderer; with FL's Data folder the import test plays the 3 demo channels on the exact
  engine (waves from the installation) and compares "Bombs Away" with model goldens.
- **C++ vs model** (`r45/py/ccmp.py`, `r45/out/ccmp.log`): bit-identical on every battery case (§4, §6); the C++ engine
  renders the 2-s Saw #2 case (48 lanes per voice) in ~0.15 s.
- The engine keeps running between notes while a free-running LFO modulates something (FL's engine phase keeps moving,
  §1.3); otherwise the channel goes idle once the voices ended and the rack fell asleep (§5.7).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `CFLWrapper` `FUN_180004980`, `FUN_180004810`, `FUN_180004190`, `FUN_180004aa0`, `FUN_180005fa0` | velocity law, note release on reuse, chunking, tables | DECOMP + EXEC |
| `FUN_180018a60`, chunk probe (host blocks 64/128/508/512/1024) | chunk = buffer − 4 = 64 | DECOMP + EXEC |
| `ToxicSynth` VMT `0x18008b950`: `FUN_1800406d0` (process), `FUN_180040ff0` (rate), `FUN_18003dca0`/`FUN_18003dcc0` (setter), `FUN_180041520`/`FUN_180041b00` (state), `FUN_18003b6e0` (names), `FUN_18003d540` (polyphony/mono) | engine | DECOMP + EXEC |
| `FUN_18003e620`, `FUN_18003e510`, `FUN_180040080`, `FUN_18003e730`…`FUN_18003fc30`, `FUN_180040370`, `FUN_180040240`, `FUN_1800413f0`, `FUN_18003acb0`, `FUN_18003d480`, `FUN_18003d4b0`, `FUN_180025bd0` | parameter laws, modulation slots | DECOMP + EXEC |
| `CSynthesizer` `FUN_180024c00` (ctor), `FUN_180024e80`, `FUN_1800250b0`, `FUN_1800256a0`, `FUN_180024bb0`, `FUN_180024bd0`, `FUN_180025c00`, `FUN_180025c20`, `FUN_180025c90`, `FUN_180025ed0`, `FUN_180025f80`, `FUN_180025fe0`, `FUN_180007a20` | notes, allocation, mono, glide, output gain | DECOMP + EXEC |
| `CToxicGenerator` VMT `0x18008b880`: `FUN_18003a3c0`, `FUN_18003a660`, `FUN_18003a730`, `FUN_180039880`, `FUN_180038d00`; `FUN_180039b30`, `FUN_1800394d0`, `FUN_1800393b0`, `FUN_1800395b0` (asm `0x18003968c`), `FUN_180026980`, `FUN_180026a40`, `FUN_180026a80`, `FUN_180026b20`, `FUN_1800269e0` | voice | DECOMP + EXEC |
| `FUN_180029cb0`, `FUN_180029cc0`, `FUN_180029da0`, `FUN_180029ec0`, `FUN_180029fa0` | oscillator bank, FM routes, phase generator | DECOMP + EXEC |
| `FUN_1800421c0`, `FUN_18002fa50` (asm `0x18002fb84…0x18002fc36`), `FUN_18002f9f0`, `FUN_180030110` | waves, tables, level choice, LFO read | DECOMP + EXEC (`tab=` dumps) |
| `FUN_180038d00` (asm `0x180038e80…0x180038f80`), `FUN_180038890` (round 26) | ladder | DECOMP + EXEC |
| Rack: `FUN_1800381f0`, `FUN_1800382f0`, `FUN_180038450`, `FUN_1800384b0`, `FUN_180038510`, `FUN_180038520`, `FUN_180038530`, `FUN_180038550`; slots `FUN_180027b30`, `FUN_180027c50`, `FUN_180027d40`, `FUN_180027d90`, `FUN_180028190`, `FUN_1800281b0`, `FUN_180028d70`, `FUN_1800292d0`; tape `FUN_180026140`, `FUN_180026340`, `FUN_180026400`, `FUN_1800267a0`; reverb VMT `0x18008b0c0`; EQ `FUN_1800295b0`, `FUN_1800297c0`, `FUN_180029880`, `FUN_18001c590`, `FUN_18001c930` | effects | DECOMP + EXEC (`fx/fxrun.exe`) |
| CRT `0x180057f90`, `0x18006edd0`, `0x18006db40`, `0x18006e440`, `0x18006fa20`, `0x1800568f0`, `0x18006f4c8`, `0x18006b4b0` | float math | DECOMP + EXEC (`fx/powt.exe`) |
| `r41/out/blobs41.pkl`, `r45/states/*.bin`, `r45/py/survey.py` | demo states | DATA |
| Image-Line manual pages `ToxicBio_effects.htm`, `ToxicBio_master.htm`, `Toxic Biohazard.htm` | chain order, EQ range, glide mode names | DOC |
