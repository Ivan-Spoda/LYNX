# Behavioural analysis of FL Studio 2026: round 44 (REA) — PoiZone: the complete engine as a portable model

> **Nature of this document.** Continuation of `REA_rodada13.md` §5 (PoiZone state), `REA_rodada15.md` §12.1 (parameter
> order) and `REA_rodada17.md` §8 (cutoff, envelope and LFO scales), with the same goal as rounds 32 (Harmless), 34
> (Harmor), 37 (Ogun) and 39 (GMS, Morphine): describe the engine precisely enough for LYNX to port it as an exact
> instrument instead of the current approximation (`synthFromPoiZone` in `src/core/FlGenerators.cpp`). *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No encrypted content was touched and no Image-Line content (code, audio or preset data) was copied
> into the repository (the test state in `tests/tests.cpp` is written from scratch).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**, read in the disassembly
> wherever the float operation order matters), from the data (**[DATA]**) or from running the installation's own DLL,
> unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Results of our
> own numerical model (which implements only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (copies in `r44/bin/`):
  - `Plugins/Fruity/Generators/PoiZone/PoiZone_x64.dll`, SHA-256
    `152100cc34592653816da384f568fd5e2c5449df48a9eda10d51263f32c3e653` (Delphi wrapper, exports `CreatePlugInstance`;
    it loads `engine_x64.dll` from the same folder and forwards to it).
  - `Plugins/Fruity/Generators/PoiZone/engine_x64.dll`, SHA-256
    `1208a11147ecbe78f0f8e59394b65d4953bc3a267c7c71b24c8691b74be5e518` (same hash as round 15; C++/MSVC, RTTI:
    `CFLWrapper`, `CPluginWrapper`, `CSynthesizer`, `PoiZoneSynth`, `CVoiceGenerator`, `PoiZoneGenerator`, `CDelay`,
    `CTapeDelay`, `CChorus`; image base 0x180000000; exports `CreatePlugInstance` too).
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=4G`), project `/tmp/r44gh_engine_x64.dll`; full decompilation
  `r44/out/eng.c` (our script `r44/gh/scripts/DumpAll.java`), RTTI vtables `r44/out/vft.txt` (`Vft.java`); `objdump -M
  intel` (`r44/py/dis.sh`) for every float expression of the audio path — Ghidra's rendering re-associates several of
  them (e.g. `x⁴` is computed as `(x·x)·(x·x)`, `x⁵` as `((x·x)·x)·(x·x)`, the pitch sum groups `spread + T`), so the
  laws below follow the machine code; constants with `r44/py/k.py`, `r44/py/kfun.py`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r44/run/pz.c` → `pz.exe`, a copy of round 39's `gm.c` (same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified
  from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's own
  SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`), driven by `run/pz.sh`; the
  battery runs `engine_x64.dll` directly, which gives the same samples as `PoiZone_x64.dll` (difference 0 on the render
  test state, §1.1). `run/sc.c` → `sc.exe` loads `engine_x64.dll` and calls its own internal sin/cos routines (RVA
  0x55e00, 0x596f0, 0x589f0) on a list of inputs (§6). Peeks `arr=-1,…,228>…` read engine fields through the wrapper's
  `+0x228` pointer. No process or dialog was left behind; outputs were deterministic run to run.
- **Model** (pure Python + numpy, `python3 -I`; under `r44/py/`): `pzmodel.py` (wrapper, engine, voices, arpeggiator),
  `pzfx.py` (gate, shelves, tape lines, chorus, delay). Drivers: `cmp1.py`, `bat.py` (batteries, model or C++ port vs
  EXEC), `dbg.py` (per-block voice-field comparison against EXEC peeks), `xcheck.py` (model vs C++ port, bitwise),
  `epi.py` (lane mapping of the SSE epilogue), `mkst.py` (state variants), `fstx.py` (preset extraction),
  `teststate.py` / `demogold.py` (test goldens). Logs in `r44/out/` (`bat_*.log`).
- **Data:** `r41/out/blobs41.pkl` (0xD5 states of the installation's projects): one demo channel ("DEDZII - NIGHT
  TERROR", channel "PoiZone", preset "ATMO Burst of Hopes") and the render test (`System/Render tests/PoiZone.flp`,
  "<noname>"); the 385 distinct states of `Data/Patches/Plugin presets/Generators/PoiZone/**/*.fst` (`r44/out/fst/`,
  384 tagged "Ver3", 1 "Ver4").

---

## 0. Data survey **[DATA, `r44/out/fst/features.txt`]**

Feature coverage of the 387 states used in the batteries (385 presets + the two project states):

| Feature | States |
|---|---|
| Arpeggiator modes up / down / up-down / random | 13 / 12 / 13 / 3 |
| Mono | 168 |
| Glide held / time-static / both / neither | 275 / 14 / 7 / 91 (227 with a glide time > 0) |
| Unison 1 / 2 / 3 / 4 voices | 167 / 139 / 40 / 41 (8, 12, 16 only in synthetic tests) |
| Delay on / chorus on / gate mix ≥ 0.001 | 323 / 270 / 58 |
| Filter LP / BP / HP | 298 / 58 / 31 |
| LFO shapes saw / pulse / triangle / sine / random | 41 / 14 / 193 / 112 / 27 (58 tempo-synced, 58 retrigger) |
| LFO destinations cutoff / Frq A / Frq AB / PW A / PW AB / pan | 194 / 56 / 76 / 23 / 24 / 14 |
| EG destinations cutoff / Frq A / Frq AB / PW A / PW AB | 327 / 31 / 19 / 7 / 3 |
| Sync / ring / octaver / noise > 0 | 78 / 71 / 66 / 155 |

The demo channel uses unison 3, delay, chorus, the LFO (triangle, synced to 8 steps) on oscillator A's pitch and the
envelope on the cutoff; the render test is the default patch (delay and chorus off).

---

## 1. Plugin structure

### 1.1 The two DLLs, creation and sample rate **[DECOMP + EXEC]**

- `PoiZone_x64.dll` is a Delphi front-end; `engine_x64.dll` holds the whole C++ engine and its own `CFLWrapper`. A render
  through either DLL gives the same samples (render test state, one note, difference 0) **[EXEC]**.
- `CFLWrapper` (VMT `0x180072da0`): slot 1 Dispatcher `FUN_180002a40`, 3 SaveRestoreState, 8 Gen_Render `FUN_180004810`,
  9 TriggerVoice `FUN_180004980`, 10 Voice_Release `FUN_180004aa0`, 11 Voice_Kill `FUN_180004ac0`; second VMT
  `0x180072e60` slot 10 = per-note host tables `FUN_180003e60`. Engine `PoiZoneSynth` VMT `0x180075970`: +0x08 note-on
  (`FUN_180007a20`), +0x10 note-off (`FUN_180007a60`), +0x50 rate set (`FUN_180033090`), +0x88 **process**
  (`FUN_1800312e0`), +0xb8 setParameter (`FUN_18002e3f0`), +0x118/+0x120 state write/read (`FUN_180031020` /
  `FUN_1800310e0`), +0x1c0 master gain (`FUN_180024f10`), +0x1c8 voice event (`FUN_180024300`), +0x1d0 mono voice event
  (`FUN_1800248f0`). Voices: `PoiZoneGenerator` VMT `0x1800758a0` (+0x08 kill `FUN_18002d280`, +0x10 start
  `FUN_18002d0e0`, +0x18 release `FUN_18002d210`, +0x20 reset `FUN_18002d2f0`, +0x28 per-chunk control `FUN_18002c580`).
- **Constructor** (`FUN_18002dca0`): 1 024 voices are created and appended to the engine's voice list in creation order,
  then one extra "padding" voice that is not in the list (§2.6); every voice constructor draws two numbers from the
  first random generator for its start phases (§2.2). Then the MIDI-controller assignments 81…154 are set to −1 and every
  parameter 0…80 is set to its default (`FUN_180008480` with the default table `FUN_18002e1d0`, §1.3).
- On a fresh instance, before any state or rate, the per-chunk smoothers already hold the defaults and the chorus/delay
  lines exist (a state loaded at 44.1 kHz sets the chorus depth on existing lines): the instance has been through one
  complete rate set (§1.2) at 44 100 Hz after the defaults **[EXEC]** (call site not identified).
- **Dispatcher 4 (sample rate)** calls the engine's rate set only when `(int)current_rate ≠ new_rate` **[DECOMP]**: at
  44 100 Hz the engine is *not* reset after a state load; at another rate it is (smoothers snapped to the state's values,
  chorus/delay lines rebuilt, §5.4) **[EXEC: 44.1 and 48 kHz batteries]**. The order FL itself uses (state then rate, or
  rate then state) is **not confirmed**; the harness and LYNX load the state first.

### 1.2 Rendering: chunks and the process order **[DECOMP `FUN_180004190`, `FUN_1800312e0`; EXEC]**

- Gen_Render first fills the per-note host tables (§1.4), then cuts the host block into chunks of `buffer size − 4` = **64
  samples** (peeked: buffer 68, chunk 64) **[EXEC]**; each chunk is one call of the engine's process. After the block,
  note slots whose note no longer sounds and is released are freed (`FUN_1800048d0`).
- The rate set (`FUN_180033090` → `FUN_1800240d0`): every note released (sustain temporarily 0), every voice reset and
  its glide position set to 48.0, master-gain smoothers set to their targets, last note −1, arpeggiator stopped/reset,
  FX rebuilt (§5), `fs8 = 8·fs` (double), amp-gain smoothing coefficient `(float)(fs·6.8027210884353747e-08)` (0.003 at
  44.1 kHz), decimator coefficients (§2.7), global LFO phase 0, the eight smoothers snapped to their targets,
  decimator states and FX states cleared.
- **Process** of a chunk of n samples, in this order:
  1. Eight smoothers, each `s = (t − s)·0.04 + s` (float): PW A ← P1, PW B ← P5, osc balance ← P8, noise ← P9,
     LFO amount ← 2·P26 − 1, EG amount ← 2·P10 − 1, mod-wheel amount ← 2·P43 − 1, cutoff ← P18.
  2. If a voice started or ended since the last chunk, the processing list is rebuilt: active voices in list order, then
     the inactive ones in list order, then the padding voice.
  3. Voices at 8× the host rate (8n samples) into two oversampled buffers (§2.4–§2.6), only if at least one voice is
     active; then the decimator (§2.7) writes the n output samples. With no active voice the output stays 0 and the
     decimator does not run.
  4. Gate (§4.2), then FX (§5): the two 12 kHz shelves, chorus, delay.
  5. Arpeggiator tick when the arpeggiator is on (§4.3), then the master gain (§1.5).
  6. The global LFO advances by n samples at fs (its phase is what a non-retriggered voice LFO starts from, §3.3).

### 1.3 State and parameters **[DECOMP `FUN_180031020`, `FUN_1800310e0`, `FUN_18002e3f0`, `FUN_18002e1d0`; DATA]**

State = `"SSLF"` + u32 0x2B4 + tag (`"Ver4"` 0x56657234 or `"Ver3"`; `"EDV4"`/`"EDV3"` also accepted) + **156 f32 =
setParameter(0…155)** + 64-byte name. Both tags load the same way. Laws (x = parameter value, float32 arithmetic, the
operation order of the machine code):

| P | Meaning | Law |
|---|---|---|
| 0 / 4 | Osc A / B shape | `x ≠ 0` → pulse (1.0), else saw (0.0) |
| 1 / 5 | PW A / B | x (smoothed, §1.2) |
| 2 / 3 | Sync / Ring | `x ≠ 0` |
| 6 | Osc B semitones | `−24 − trunc(x·(−48))` |
| 7 | Osc B detune | `x·0.5` semitones |
| 8 / 9 | Osc balance / noise | x (smoothed) |
| 10 / 26 / 43 | EG / LFO / mod-wheel amount | `2x − 1` (smoothed) |
| 11, 12, 14 | EG attack / decay / release (s) | `clamp((x²)·(x²)·49 995 + 5, 5, 50 000)·0.001` |
| 13 | EG sustain | x |
| 15 | Gain | `powf(10, 1.5x − 1)` (×0.5 per side in the master, §1.5) |
| 16 / 17 | Fine / transpose | `2x − 1` / `−24 − trunc(x·(−48))` semitones; pitch offset = transpose + fine |
| 18–21 | Cutoff, resonance, key track, velocity track | x |
| 22 / 23 / 25 | Amp attack / decay / release (s) | same `x⁴` law with ranges 1…5 000 / 5…20 000 / 15…10 000 ms |
| 24 | Amp sustain | `clamp(x·x, 0, 1)` |
| 27 | LFO retrigger | `x ≠ 0` |
| 28 | LFO rate (Hz) | `clamp(((x·x)·x)·(x·x)·30, 0, 30)` |
| 29 / 30 | Unison detune / pan | `x·1.5` semitones (total spread) / `clamp(x)` |
| 31–38 | Delay on, time L/R, feedback, mod, lo-cut, hi-cut, wet | §5.3 (P31 also resets the delay lines) |
| 39–42 | Chorus on, rate, depth, wet | §5.4 |
| 44 | Mono | `x ≠ 0` (a change releases every note) |
| 45 | Octaver | `x ≠ 0`: odd unison voices +12 semitones |
| 46 | Glide time (s) | `t = 2·clamp(x³)`; below 1e-4 it is stored as 9.99999975e-05 |
| 47 / 48 / 49–64 | Gate mix / smooth / steps 1…16 | §4.2 |
| 65 / 67 | EG / LFO destination | `clamp(trunc(4x + 0.0004), 0, 4)` / `clamp(trunc(5x + 0.0005), 0, 5)` |
| 66 | Filter mode | `clamp(trunc(2x + 0.0002), 0, 2)`: LP / BP / HP |
| 68 | LFO shape | `clamp(trunc(4x + 0.0004), 0, 4)`: saw / pulse / triangle / sine / random |
| 69 / 70 | Arpeggiator mode / range | m = `clamp(trunc(4x + 0.0004), 0, 4)`: 0 off, else on with mode m − 1; range `clamp(trunc(3x + 0.0003), 0, 3) + 1` octaves |
| 71 | Mod-wheel destination | `clamp(trunc(4x + 0.0004), 0, 4)`: cutoff / LFO amount / Frq A / PW A / PW AB |
| 72 / 73 | Glide held / glide time static | glide mode = (P72 ≠ 0) + 2·(P73 ≠ 0) |
| 75 | Unison voices | `trunc(x)` 0…6 → 1, 2, 3, 4, 8, 12, 16 |
| 76 | Polyphony | `trunc(x) + 1`; < 1 or > 64 → 64 |
| 77 / 79 | Arpeggiator / gate speed | `trunc(x)` 0…6 → 0.5, 1, 2, 3, 4, 8, 16 (arpeggiator minimum 0.25) |
| 78 | Pitch-bend range | `trunc(x) + 1` semitones |
| 80 | LFO tempo sync | `clamp(trunc(x), 0, 10)` → free, 1, 2, 3, 4, 8, 12, 16, 32, 64, 128 |
| 81–154 | MIDI-controller assignment of parameters 0…73 (−1 = none) | stored only (`FUN_1800252a0`, used by `FUN_180024fa0` for incoming CCs) |
| 157 / 158 | Mod wheel / pitch bend (host side) | x / `2x − 1` |

P74 has no handler (stored only; meaning not confirmed). **Defaults** (`FUN_18002e1d0`): 1.0 for P1, 5, 12, 13, 14, 18,
23, 24, 37, 49, 51, 53, 54, 55, 57, 59, 61, 62, 63, 77; 0.5 for P6, 10, 15, 16, 17, 26, 34, 38, 40, 41, 43, 68; P28 0.2,
P29 0.1, P32/33 0.42857143, P35 0.3, P42 0.4, P76 12; the others 0. The CRT `powf` used by these laws (and in §2.4) gives
the correctly rounded result in every EXEC case except near-ties (§6) **[EXEC]**.

### 1.4 Host interface **[DECOMP `FUN_180004980`, `FUN_180005fa0`, `FUN_180003e60`, `FUN_1800048d0`, `FUN_180023e00`; EXEC]**

Same wrapper code as GMS (round 39 §1.3/§1.11):
- **TriggerVoice:** `note = trunc(Init.Pitch·0.01)` clamped 0…127; MIDI velocity
  `vel7 = clamp(trunc((logf(Init.Vol·10 + 1)·0.328458726)·127), 0, 127)`; if the note's slot holds another host voice,
  that note is first released in the engine. The engine stores `vel7·(1/127)` per note; the voice velocity is
  `powf(v, 1.0)` (velocity curve fixed at 1.0).
- **Per-note tables** at every Gen_Render, for the notes that have a slot (from note-on until the note stops sounding
  and is released): `T_L`, `T_R` = host ComputeLRVol(Final.Pan, Final.Vol), `T = (float)((float)Final.Pitch·0.01 −
  note + 60)`, and Final.FCut / Final.FRes, which PoiZone uses: FCut is added to the cutoff exponent and FRes to the
  resonance (§2.4). Notes without a slot inherit the previous slot's values (the first slot's values are copied
  downwards; no slot at all → 0).
- **Max polyphony** (ProcessEvent 1, `FUN_180023e00`): value 0 → 64 (the engine's maximum), k → k. Before that event the
  limit is 0, which makes every note take the first voice of the list (§2.3) **[EXEC]**. FL's value is **not
  confirmed**; the batteries send 0.
- **Tempo** (ProcessEvent 0): 120 until set **[EXEC]**. Song position and the "playing" flag (Dispatcher 12/0x24, as
  GMS) only lock the gate (§4.2).

### 1.5 Master gain **[DECOMP + objdump `FUN_180024f10`, `FUN_1800241b0`; EXEC]**

`g = (gain·cc7)·0.5` with cc7 = 1; `f = clamp((((cc10 − 0.5) + cc10) − 0.5) + (c + c), 0, 2)` with cc10 = 0.5 and
c = 0.5, i.e. f = 1: L = g·(2 − f) when f > 1, R = g·f when f < 1, else g. Per output sample `out *= m`, then
`m = (target − m)·0.005 + m` (left and right). The gains are set to the targets at a rate set.

---

## 2. Voices

### 2.1 Voice pool and allocation **[DECOMP `FUN_180024300`, `FUN_180023e20`, `FUN_180024090`; EXEC]**

The 1 024 voices form a fixed list (creation order; it is never reordered). The allocation limit is
`min(npoly, polyphony)·unison`, at most 1 024. A note-on scans the first *limit* voices of the list: the **first inactive**
voice is taken; if none, the oldest released voice (smallest start counter), else the oldest held voice; with a limit of 0
the first voice of the list is always taken. One voice per unison slot u = 0…U−1 is allocated this way. A note-off
releases every voice with that note.

### 2.2 Random generators **[DECOMP `FUN_18002c420`, `FUN_180025150`, `FUN_180026a60`, render loops; EXEC]**

Three process-global LCGs `s ← s·0x0BB38435 + 0x3619636B (mod 2³²)`, started at **22 222** (noise and voice start
phases), **2 222** (drift and random arpeggio) and **2 222** (random LFO). Uniform floats are `(float)s·2.32830644e-10`
(clamped to [0, 1] for the start phases) and, in the render loops, `(float)((double)s·2⁻³²)`. Voice k (k = 0…1 023,
then the padding voice) takes draws 2k + 1 and 2k + 2 as its oscillator A and B start phases; the pulse phases start
equal to them. The generators are shared by every PoiZone of a process, so FL's sequence in a project cannot be
reproduced (LYNX starts one set per instance, like a freshly loaded DLL).

### 2.3 Note events, glide, drift **[DECOMP `FUN_180024300`, `FUN_1800248f0`, `FUN_180025200`, `FUN_180025260`; EXEC]**

- Engine note-on: velocity table, held table `(char)trunc(v·127)`; with the arpeggiator on, the event only rebuilds the
  arpeggio (§4.3). Note-off: ignored while the sustain pedal (CC64) is down; else held entry 0, then the same paths
  with velocity 0. Every voice event requires polyphony ≠ 0.
- **Poly note-on:** one drift draw `r = rand₂ − 1` for the note (all unison voices share it), `drift = (r + r)·0.01`
  semitones. Glide origin: the last note played if any, else the note itself; in glide modes 1/3 the nearest *other*
  held note (smallest |Δ|, scanning keys upwards), else the note itself. For each allocated voice: start counter, note,
  unison index/count, drift, velocity, glide position = origin (= the note if glide time < 0.001 s), released flag
  cleared, glide rate `(float)(12/(fs·t))` semitones per 8×-sample in modes 0/1 or `(float)(|note − pos|/(fs·t))` in
  modes 2/3, then the voice starts (§3). Last note = this note.
- **Mono note-on** (P44): the first U voices of the list; a voice that is inactive or released restarts (velocity, glide
  origin — the note itself in modes 1/3 when the first voice is free or released — envelopes); a held voice only gets the
  new note, drift and glide rate (legato: no restart, the glide continues from the current position). **Mono note-off:**
  if another key is held, the nearest one (smallest |Δ|) becomes the voices' note with rate `12/(fs·t)` (no restart);
  otherwise the voices are released.
- Glide per chunk (`FUN_180025c90`): the position moves by `n8·rate` towards the note and stops on it.
- The glide rate is per 8×-oversampled sample while it is computed from fs, so a glide covers 12 semitones in t/8
  seconds in modes 0/1 **[DECOMP]** (consistent with EXEC in the glide presets).

### 2.4 Per-chunk control values (`FUN_18002c580`) **[DECOMP + objdump; EXEC peeks]**

For each active voice, with n8 = 8n:
1. Glide; if the amplitude envelope has finished, the voice is killed (gains and slopes zeroed, note −1) — it is still
   rendered in this chunk (with zero gains) and leaves the list at the next chunk.
2. EG value `e` (§3.2, n8 samples), voice LFO (rate, shape and sync copied from the global LFO each chunk; tempo passed
   as float) value `l`; mod-wheel destination 1: `l = l·(mwAmt·mw)`.
3. Amplitude `a = amp(n8)·10`; pan `p = 0.5`, or `((l·lfoAmt) + 1)·0.5` for LFO destination 5, clamped to [0, 1].
4. Gain slopes `d = ((gL·a)·p − gainL)·(1/n8)`, `dR = ((gR·a)·(1 − p) − gainR)·(1/n8)` with the unison pan
   `f = ((u/(U−1))·2 − 1)·uniPan` (0 for U = 1): `gL = clamp(f + 1)·T_L[note]·0.5`, `gR = clamp(1 − f)·T_R[note]·0.5`.
   Noise-level slope `(noise − level)·(1/n8)`. Values below 1e-9 in magnitude are flushed to 0 (here and wherever the
   DLL does it).
5. **Pitch** (semitones, float, this grouping): `((((((transpose + fine) + glidePos) + (spread + T[note])) + octave) +
   drift) + pbRange·pb) + 0`, spread `((u·detune)/(U−1)) − detune·0.5`. `freq = (float)(pow(2, (pitch − 57)·(1/12))·220)`.
6. **Osc A** (`FUN_18002cb70`): EG destinations 1/2: `f = (float)(pow(2, (trunc(egAmt·24)·e)·(1/12))·f)`; 3/4: PW += e·egAmt;
   LFO destinations 1/2: `f = f·powf(2, ((λ·λ)·(λ·λ)·l)·(±2))` (λ = LFO amount, sign of λ); 3/4: PW += l·λ; mod wheel
   destination 2: `f = f·powf(2, ((m·m)·m·mw)·2)`, 3/4: PW += m·mw. Clamps: f ≥ 0, PW in [0, 1], f ≤ fs8/2;
   increment `(float)(f/fs8)`; level slope `(((1 − bal)·(1 − noise)) − levelA)/n8`; pulse phase
   `phaseA − (float)(0.99 − PW·0.49)` (+1 if negative) — the pulse is the difference of two saw phases, so PW 1 → square,
   PW 0 → narrow.
7. **Osc B** (`FUN_18002ce40`): `f = (float)(freq·pow(2, (semi + detune)·(1/12)))`; EG destination 2 / 4 and LFO
   destination 2 / 4 as for A (only "AB" destinations), mod wheel destination 4 → PW; level slope
   `(((1 − noise)·bal) − levelB)/n8`.
8. **Filter** (`FUN_18002c8a0`): exponent (double) `v = (double)((pitch − 56)·kbd)·0.0084 + ((vel − 0.5)·velTrk)·0.5 +
   FCut[note]`, ± `(a·a)·e` (EG destination 0), ± `(λ·λ)·l` (LFO destination 0), ± `(m·m)·mw` (mod-wheel destination 0)
   (sign of the amount), `v = cutoff·1.1 + v`, clamped to [0, 0.91]; `w = 2^(10v + 6)/fs8`;
   `k = (float)((w·3.6 − (w·1.6)·w) − 1)`, `g = (k + 1)·0.5`; resonance
   `(float)((e^(1.386249 − 1.5233505·v) + 1)·clamp(FRes[note] + reso, 0, 1)·2.05344)`. Output mix: LP → stage 4; BP →
   3·(stage 4 − stage 3); HP → input − stage 4.

### 2.5 One voice at a time (`FUN_18002d340`) **[DECOMP + objdump; EXEC]**

Per 8×-sample: phases `−= increment` (A, A-pulse, B, B-pulse), one noise draw `((float)((double)s·2⁻³²) − 0.125)·noiseLevel`,
each phase `+1` when **< 0**; hard sync: when B wrapped, `A_pulse −= A·1`, `A −= A·1`; `A = a1 − a2·shapeA`,
`B = b1 − b2·shapeB`; ring: `A = B·A`; `x = ((((levelA·A) + levelB·B) + noise) − 0.42) − stage4·res`; for x < 0
`x = x/((x·x)·3 + 1)`; four stages `y = (x + x_prev)·g − k·y_prev`; output `((y3·c3) + (y4·c4)) + x·cx`; DC blocker
`d = (o − d)·1e-4 + d`, `o −= d`; gains `gL += dL`, `gR += dR`, levels `+= slopes`, smoothed gains
`sL = (gL − sL)·c + sL` with the rate-dependent c of §1.2; `L = o·sL`, `R = o·sR`, added to the oversampled buffers.

### 2.6 Four voices at a time (`FUN_180031760`, SSE) **[DECOMP + objdump; EXEC]**

While at least two voices remain, voices are processed in groups of four lanes taken from the processing list (active
voices first, then inactive voices and the padding voice fill the lanes; a lane's control values are computed only for
active voices). Same algorithm, with these differences (all reproduced by the model):
- four noise draws per sample (one per lane, lane order), also for padding lanes;
- phase wraps when **≤ 0**;
- ring: `((B·levelA)·A) + B·levelB`; no ring: `(B·levelB) + (A·levelA)`; then `noise + that`;
- soft clip `x/((((x ≤ 0)·3)·x)·x + 1)`;
- output `((cx·x) + (c3·y3)) + (c4·y4)`; DC state not flushed;
- **the output uses the smoothed gains of the previous sample** (L and R), then updates them;
- lanes summed as `((l₁ + l₀) + l₂) + l₃`;
- **the oscillator A and B levels are written back in reverse lane order** (lane 3 → first voice … lane 0 → fourth voice;
  the other fields go back to their own voices; `r44/py/epi.py`): a voice whose level ramped up in this chunk can get the
  level of another lane (often 0) and ramp again in the next chunk **[EXEC: confirmed by peeks]**.

### 2.7 Decimator (`FUN_1800328e0`) **[DECOMP + objdump; EXEC peeks]**

One 2nd-order low-pass per channel at 8·fs: `w = (fs·π/12)/fs`, `(s, c) = sincos(w)` (double), `g = (float)((1 − c)/
(0.5·s + 2))`, `a1 = (float)(−4c/(s + 2))`, `a2 = (float)((2 − s)/(s + 2))`; per 8×-sample
`y = (((x + x₋₂) + (x₋₁ + x₋₁))·g − y₋₁·a1) − y₋₂·a2`; every 8th output is kept; states flushed after each group of 8.

---

## 3. Envelopes and LFO

### 3.2 Envelopes (`FUN_180019f20`, `FUN_18001a010`, `FUN_18001a1e0`) **[DECOMP; EXEC]**

Doubles, advanced by n8 samples per chunk at fs8. Start: from 0 if the envelope had finished, else from its current value
(amplitude envelope of a stolen voice; the EG always restarts from 0). Stages: attack position `+= n8/(A·fs8)`, value
`curve(t, start, peak, cA)`; decay to `peak·S` with `cD`; when decay ends with S = 0 the envelope finishes; release from
the current value to 0 with `cR`. `curve(t, a, b, c) = (b − a)·t' + a` with `t' = 1 − 10^(t·c)·(1 − t)` (t' = t when
c = 0). Amplitude: peak = velocity, curves 0 / −2.5 / −2.5; EG: peak 1, curves −2 / −2 / −2. Times ≤ 1 ms → 1 ms.

### 3.3 LFO (`FUN_180026950`) **[DECOMP; EXEC]**

Phase `p = frac(p + (rate/fs)·n)` (rate = `(1/15)/sync·tempo` when synced); shapes: saw p, pulse (p < 0.5), triangle
`2p` / `2 − 2p`, "sine" = **cos(2πp)** (EXEC: the DLL's routine at `0x180058470` behaves as cosine), random = a new
`rand₃·2⁻³²` each time the phase wraps. The LFO is unipolar (0…1). Each voice copies the global LFO at its start
(phase 0 if retrigger); the global LFO advances once per chunk.

---

## 4. Gate and arpeggiator

### 4.2 Gate (`FUN_1800338a0`, `FUN_180033a00`, `FUN_1800339a0`, `FUN_1800339d0`) **[DECOMP + objdump; EXEC]**

Step values `1 − trunc(P49…64)` (step on → 0), mix `clamp(P47)`, smoothing `((1 − s)²)·0.01 + 1e-4`. With no active voice
the gate resets (phase 0, step 0, gain `1 − step₀·mix`). Otherwise, while the host plays, the step and phase are set
from the song position (`d = beats·4/speed`, step = trunc(d) & 15) — **not EXEC-verified** (the harness never plays);
then, if mix ≥ 0.001, per sample: phase `+= ((1/15)/speed·tempo)/fs`, step + 1 (mod 16) when the phase exceeds 1,
gain `g = ((1 − step·mix) − g)·smooth + g` (flushed below 1e-6), L and R × g.

### 4.3 Arpeggiator (`FUN_18001b220`, `FUN_18001ae80`, `FUN_18001b0c0`) **[DECOMP + objdump; EXEC: 41 arpeggio presets]**

- Each note event rebuilds the table: held keys ascending, then the same keys +12 per extra octave (range 1…4), count
  `range·keys` (1…128). The first key starts the arpeggiator (all its notes off, reset: phase 0, index −2, direction up,
  trigger pending); no key stops it.
- Tick (once per chunk, after the voices): if a trigger is pending, the index is fixed per mode (up: 0 when out of range;
  down: count − 1 when negative; up/down: bounces between 0 and count − 1; random: `trunc(rand₂·count)`), the previous
  arpeggio note gets a voice note-off and the new one a voice note-on with its key's velocity, then the index moves.
  The phase advances by `n·(((1/15)/speed)·tempo)/fs`; reaching 1 sets the next trigger. Notes therefore change on chunk
  boundaries and sound from the following chunk.

---

## 5. Effects (`FUN_1800335f0`, `FUN_180033730`, `FUN_180033790`, `FUN_180033850`) **[DECOMP + objdump; EXEC]**

### 5.1 Shelves (`FUN_18001b790`, `FUN_18001b7e0`, `FUN_18001bb80`)

Always on, one per channel: RBJ high shelf at 12 kHz, gain value 5 with `A = powf(10, 5·0.05)` (so the shelf
reaches A² ≈ +10 dB), bandwidth 0.99:
`w = clamp((float)(12000·6.283185/fs), 0, 3)`, `sinf`, `α`-term `2·sinh(0.3465736·bw·w/sin w)` (double, then float),
coefficients in float in the machine code's order, normalised by `1/a0`; process
`y = (((x₁·b1) + x·b0) + b2·x₂ − y₁·a1) − a2·y₂` with input and output flushed below 1e-9.

### 5.2 Tape line (`CTapeDelay`, `FUN_1800253c0`, `FUN_1800256a0`)

A circular "tape" made of two lists of (value, position) entries: the write list collects entries at the write position
`wp ∈ [0, 1)`, which advances by `speed·m` per sample (`m = max(((3 − 2tri)·tri)·(depth·tri) + 1, 1)`, triangle `tri` of
a per-line LFO, speed smoothed `+= (target − speed)·0.005`, clamped to [1/size, 0.2]); the read list is the previous pass,
read at the same `wp` by linear interpolation between its stored positions. When `wp` reaches 1 the lists swap (with three
carried entries, exactly as the DLL does). Written value `read·feedback + input`; output `read·level`, level smoothed by
0.01 per sample towards its target; optional 12 dB low-pass and high-pass on the read value (`FUN_18001b5f0`/`b6f0`,
`FUN_18001b400`/`b500`, coefficients from the CRT's vector sincosf, §6). Speed target for a time x:
`1/((x(1 − 2/size) + 2/size)·size)`.

### 5.3 Delay (`CDelay`, `FUN_180028380`, `FUN_180028560`)

Two tape lines of `2·fs` samples (right one starts at write position 0.7), LFOs 1 Hz / 0.876 Hz, low-pass and high-pass on.
Time: `x = clamp(2/((0.2666667/(trunc(7·P)+1))·tempo), 0, 1)` (recomputed every chunk from the current tempo); feedback
`(float)(P34·0.84)`; mod depth `(float)((P35²)·0.01)`; lo-cut `powf(2, 10·P36)·10` Hz; hi-cut `powf(2, 6.6·P37)·200` Hz
(both clamped to [10, 0.476·fs]); wet level `P38²`. `L += lineL(L)`, `R += lineR(R)`.

### 5.4 Chorus (`CChorus`, `FUN_180026ca0`, `FUN_180026e80`)

Three tape lines of 1.1, 0.854 and 1.0 × `0.02·fs` samples (start positions 0, 0.467, 0.778), LFO rates
`(5·P40 + 0.1)·{0.896, 1, 0.689}` Hz, depth `P41³`, level P42; input `R + L`; `L += d₂ + d₁`, `R += d₂ + d₃`. Two
rate-change quirks **[DECOMP + EXEC at 48 kHz]**: the wet value is stored negated and re-applied by a rate set, so after a
rate change the chorus level becomes −P42 (inverted); and the rebuilt lines get depth 1.0 until P41 is set again.

---

## 6. CRT math in the audio path **[EXEC `run/sc.exe` on 200 003 inputs in [0, 3.2]; DECOMP]**

- The delay filters' sincos (`FUN_180055e00`) is a vectorised float routine (a CPU-dispatched path for |x| ≥ 2⁻¹³; below
  that `sin = x`, `cos = 1 − 0.5x²`): only 76 % of its sines and 20 % of its cosines equal the correctly rounded values
  (scalar `sinf`/`cosf` used by the shelves: 99.9 %). LYNX uses correctly rounded sin/cos there; the 1-ulp coefficient
  differences of the 10 Hz high-pass (pole near 1) and the hi-cut low-pass explain every residual of the batteries with
  the delay on (§7). The routine's exact algorithm was not reproduced (not confirmed whether both CPU paths agree).
- `powf` (`FUN_1800429c0`) and `pow` match the correctly rounded results except in two near-tie cases seen in the
  batteries (a result 0.50002 ulp from the midpoint rounded the other way): 1 ulp in one chunk's oscillator increment,
  −68 dB and −104 dB on those two runs.

---

## 7. Verification **[EXEC + model]**

Each battery renders six overlapping notes over 0.75 s (keys 60, 64, 67, 72, then 60 and 55 again, Init.Vol 0.4…1.0,
note-offs inside the render), ProcessEvent 1 = 0, host blocks of 512 unless stated, L = R = Final.Vol.

| Set | Rate / block / tempo | Result vs EXEC |
|---|---|---|
| Render test (default patch) | 44.1 kHz / 512 / 120 | bit-identical |
| Demo channel (delay + chorus) | 44.1 kHz / 512 / 120 | −72.2 dB (delay filters, §6); with the delay off: bit-identical |
| 385 presets as saved | 44.1 kHz / 512 / 120 | 69 bit-identical; 318 with the delay on: median −78.8 dB, worst −48.5 dB |
| 385 presets, delay off | 44.1 kHz / 512 / 120 | 384 bit-identical, 1 at −68 dB (`powf` tie, §6) |
| 385 presets, delay off, + render test | 48 kHz / 1 000 / 120 | 385 bit-identical, 1 at −104 dB (`pow` tie) |
| 385 presets, delay off | 44.1 kHz / 512 / 137 | 384 bit-identical, 1 at −68 dB (same preset as above) |
| 10 synthetic states (unison 8/16, mono glide modes 2/3, up/down and random arpeggios, gate, random LFO to pan, ring + sync + octaver, BP/HP with full key/velocity tracking, polyphony 1) | 44.1 kHz / 512 / 120 and 48 kHz / 777 / 133 | 20/20 bit-identical |

The C++ port (`core/PoiZone.cpp`) is bit-identical to the model on every state tried (`xcheck.py`), so the same table
applies to LYNX. `tests/tests.cpp` (`poizone_engine`) checks a state written from scratch against golden samples of the
model (that state uses the delay: −67.6 dB vs EXEC, §6) and the instrument against the reference renderer; with FL's
Data folder it also imports the demo and render-test channels on the exact engine and compares the demo channel with
model goldens.

---

## 8. The port in LYNX (`src/core/PoiZone.{h,cpp}`)

- `InstrumentKind::poizone`; the importer keeps the plugin's raw state (`ChannelDef::pluginState`); `synthFromPoiZone`
  stays as the fallback for states the engine cannot read and for the preset button of the built-in synth.
- The instance is built like FL's: construction with the defaults and the 44 100 Hz rate set, then the state, then the
  playing rate (reset only if different). Parameter edits rebuild the instance (sounding notes stop).
- Host side as GMS: FL's channel volume/pan/pitch reach the engine through the per-note tables (recovered from LYNX's
  channel gains, not applied twice), velocity → Init.Vol `(11^(1.28·vel) − 1)/10`, max polyphony 1 for a mono channel,
  else event value 0 (→ 64 voices); tempo = song tempo; the gate locks to the song position while the transport plays.
- By design: one set of random generators per instance; delay low/high-pass coefficients from correctly rounded
  sin/cos (§6).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `CreatePlugInstance` (both DLLs), Delphi front-end, `CFLWrapper` VMT `0x180072da0` / `0x180072e60` | DLL roles, plugin interface | DECOMP + EXEC |
| `FUN_180002a40` case 4, `FUN_180004190`, `FUN_180004810`, `FUN_1800048d0` | rate set only on change, chunks of 64, slot freeing | DECOMP + EXEC |
| `FUN_180004980`, `FUN_180005f80`/`5fa0`, `FUN_180003e60`, `FUN_180023e00` | velocity, per-note tables incl. FCut/FRes, max polyphony | DECOMP + EXEC |
| `FUN_18002dca0`, `FUN_18002c420`, `FUN_180008480`, `FUN_18002e1d0`, `FUN_180023e50` | construction, voice phases, defaults | DECOMP + EXEC |
| `FUN_180031020`, `FUN_1800310e0`, `FUN_18002e3f0`, `FUN_18002e310`/`340`/`2d0`/`2f0` (objdump) | state, parameter laws | DECOMP + DATA + EXEC |
| `FUN_180033090`, `FUN_1800240d0`, `FUN_1800312e0` (objdump) | rate set, process order, smoothers | DECOMP + EXEC |
| `FUN_180024300`, `FUN_1800248f0`, `FUN_180025200`/`5260`, `FUN_180025150`, `FUN_180025c90`, `FUN_180025cf0` | allocation, mono, glide, drift | DECOMP + EXEC |
| `FUN_18002c580`, `FUN_18002cb70`, `FUN_18002ce40`, `FUN_18002c8a0`, `FUN_180025d50`/`5d90`/`5e30` (objdump) | per-chunk control values | DECOMP + EXEC |
| `FUN_18002d340`, `FUN_180031760` (objdump `r44/out/simd.s`, `epi.py`), `FUN_180031260` | voice render, SSE lanes, reversed level write-back, list order | DECOMP + EXEC |
| `FUN_1800328e0` (objdump) | decimator | DECOMP + EXEC |
| `FUN_180019f20`, `FUN_18001a010`, `FUN_18001a1b0`/`a190`, `FUN_18001a1e0` | envelopes | DECOMP + EXEC |
| `FUN_180026950`, `FUN_180026a60`, `FUN_180058470` | LFO, random shape, cosine | DECOMP + EXEC |
| `FUN_1800338a0`, `FUN_180033a00` (objdump), `FUN_1800339a0`, `FUN_1800339d0` | gate | DECOMP + EXEC (song lock: DECOMP only) |
| `FUN_18001ae00`, `FUN_18001ae80` (objdump), `FUN_18001b0b0`…`b390`, `FUN_18001b220` | arpeggiator | DECOMP + EXEC |
| `FUN_18001b790`, `FUN_18001b7e0` (objdump), `FUN_18001bb80` | shelves | DECOMP + EXEC |
| `FUN_1800253c0`, `FUN_1800255e0`, `FUN_1800256a0` (objdump `r44/out/tape.s`), `FUN_180025a40`…`5b90`, `FUN_18001b5f0`/`b6f0`/`b400`/`b500` | tape line | DECOMP + EXEC |
| `FUN_180027fb0`, `FUN_180028090`…`28560`, `FUN_1800285e0` | delay | DECOMP + EXEC |
| `FUN_180026aa0`…`26f50` | chorus | DECOMP + EXEC |
| `FUN_180055e00`, `FUN_1800596f0`, `FUN_1800589f0`, `FUN_1800429c0`, `FUN_180041320` | CRT sin/cos, powf, pow | DECOMP + EXEC (`sc.exe`) |
| `r41/out/blobs41.pkl`, `r44/out/fst/` | demo/render-test states, 385 presets | DATA |
| `run/pz.exe`, `run/sc.exe`; `bat.py`, `dbg.py`, `xcheck.py`; `out/bat_*.log` | §7 | EXEC + model |

### Open questions

- FL's own order of state load and sample-rate set (decides whether the smoothers glide from the defaults at 44.1 kHz,
  and the chorus sign/depth after a rate change); FL's max-polyphony event value; the mixing-time format for the gate's
  song lock (beats assumed as for GMS) — **not confirmed**.
- The vector sincosf algorithm (and whether its CPU-dispatched paths agree), `powf`/`pow` near-tie rounding — not
  reproduced (§6).
- Sustain pedal, mod wheel, pitch bend and MIDI-controller assignments (P81…154) are decoded but were not exercised
  (FL sends pitch through Final.Pitch); P74 meaning not confirmed.
- Random generators are process-global in FL: noise, drift, start phases, random LFO and random arpeggio of a given FL
  render are not reproducible by design.
