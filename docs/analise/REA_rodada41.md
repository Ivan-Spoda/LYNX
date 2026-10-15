# Behavioural analysis of FL Studio 2026: round 41 (REA) — MiniSynth: the complete engine as a portable model

> **Nature of this document.** Continuation of `REA_rodada10.md` §2 (MiniSynth state layout, parameter names, oscillator
> kinds) and `REA_rodada21.md` §3 (tuning, envelopes, LFO scales), with the same goal as rounds 32 (Harmless), 37 (Ogun)
> and 39 (GMS/Morphine): describe the engine precisely enough for LYNX to port it as an exact instrument instead of the
> approximation `synthFromMiniSynth` in `src/core/FlSynths.cpp`. *Clean-room* reverse engineering, behavioural only. No
> Image-Line code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was
> touched and no Image-Line content (code, audio, wave or table data) was copied into the repository; the wave tables are
> built at run time from the user's installation.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the official
> Image-Line documentation (**[DOC]**), from the data (**[DATA]**) or from running the installation's own DLL, unmodified
> (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the
> code are stated as **inference**. Results of our own numerical model (which implements only what is marked DECOMP/EXEC)
> are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line documentation |
| **[DATA]** | checked in `.flp`/`.flms`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target:** `Plugins/Fruity/Generators/Minisynth/MiniSynth_x64.dll`, SHA-256
  `e00ca0194eccd4987f00a1976f3da3403c7ba78cfb5bf771f2fbf1ed0848ec85` (identical to rounds 10/21; copy in `r41/bin/`),
  C++/MSVC, image base 0x180000000, one export `CreatePlugInstance`. Data file `Minisynth/oscdata.dat` (8192 bytes, read
  at run time by the plugin and by LYNX).
- **Structure (answers the round's first question).** MiniSynth is a native FL generator built on Image-Line's portable
  plugin framework, the same family as GMS (round 39 §1.1): RTTI `CFLWrapper` → `CPluginWrapper`/`TCPPFruityPlug` (the FL
  plugin interface) → `CPluginRenderer`/`CSynthesizer` → `CMiniSynth`, voices `CMiniSynthVoice` (`CVoiceGenerator`), editor
  classes `CMiniSynthEditor`/`CMiniSynthKnob`… The string "Fruity Wrapper" that `CreatePlugInstance` copies is only the
  framework's default host-product name (it is compared later to decide whether editor key messages are forwarded to the
  parent window, `FUN_180006860`/`FUN_1800068b0`); the engine is **not** a VST loaded through FL's Fruity Wrapper
  **[DECOMP]**. The class names and the "FL Studio Mobile" origin of round 10 stand.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=3G`), project `/tmp/r41gh`, our own script
  `r41/gh/scripts/DecompAll.java` → full decompilation `r41/out/ms_all.c` (0x180001000…0x180050000) and the engine range
  `r41/out/ms_core.c`; vtables from RTTI (`r41/py/vft.py` → `r41/out/ms_vft.txt`); `objdump -M intel` (`r41/py/dis.sh`)
  wherever Ghidra dropped arguments of CRT calls or the operation order mattered (`r41/out/voice_render.s`,
  `r41/out/filter.s`); `r41/py/symsse.py` (our own symbolic reader of straight-line SSE2 code) to recover the grouping of
  the filter matrix squaring; constants with `r41/py/k.py`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r41/run/ms.c` → `ms.exe` (a copy of round 39's `gm.c`, same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified,
  minimal fake host, state restored through the plugin's own SaveRestoreState from a read-only `IStream`,
  `TerminateProcess`, `timeout`); `run/ms_sp.c` also answers host Dispatcher 0x24 (song position) like round 39's
  `gm_sp.c`; `run/callf.c` → `callf.exe` calls one of the DLL's internal math routines on test arguments (used for §7).
  Peeks `arr=…` read engine and voice fields through the wrapper's `+0x228` engine pointer. No process or dialog was left
  behind; outputs were deterministic run to run.
- **Model** (pure Python, `python3 -I`; under `r41/py/`): `msmodel.py` (wrapper, engine, voices, tables, effects),
  `mspatch.py` (state). Drivers: `msrun.py` (EXEC), `mcmp.py`, `mbat.py` (installation states), `pbat.py` (91 presets),
  `tvar.py` (variants), `tmod.py` (parameter overrides), `tmath.py` (§7), `t_tab.py` (tables), `ccmp.py` (LYNX's C++ engine
  against the DLL), `gold.py` (test goldens). Logs in `r41/out/` (`pbat2.log`, `tvar1.log`, `ccmp_presets.log`).
- **Data:** `r41/out/blobs41.pkl` (`py/survey41.py`): the 12 MiniSynth 0xD5 states of the installation's projects (10 in
  the demo "Eric Kauffmann & NrgMind – Exoplanet", 1 in `System/Render tests/MiniSynth.flp`, 1 in the template "SFX
  transitions"); `r41/out/presets41.pkl` (`py/presets.py`): the 91 factory presets `Patches/Plugin presets/Generators/
  MiniSynth/**/*.flms` (a `.flms` file is the 0xD5 state without "SSLF" + size).

---

## 0. Data survey **[DATA]**

| Set | Kinds (P5) | Modes (P0) | Effects on (P25/P28/P33/P37) | Other |
|---|---|---|---|---|
| 12 project states | SUPERSAW ×8, SQR+SAW ×2, SAW ×2 | all POLY | distortion ×1 (Pluck Harmonies) | LFO → pitch ×7, → OSCMOD ×1 (Riser); FLT Vel 1.0 ×3; noise ×5 |
| 91 presets | all ten kinds (SAW 14, SAW+SAW 11, PULSE 17, SQR+SAW 7, SQR+SQR 6, SUPERSAW 9, BELL 16, DEAF SAW 3, SPREAD 3, SPREAD 2 5) | POLY 52, MONO 8, LEAD 31 | distortion 37, delay 57, chorus 81, phaser 22 | slide > 0 in 46, noise in 50, LFO dest FLT 56 / FRQ 21 / OSCMOD 14 |

One factory preset ("Keys Brass Buttons") starts with `99 46 4C 4D` ("\x99FLM") instead of "\x99FLS"; the plugin reads it
normally (§1.1).

---

## 1. State and parameters

### 1.1 State layout and loading **[DECOMP `FUN_180002f10`, `FUN_180007700`, `FUN_180025e70`, `FUN_180025d60`; DATA]**

- 0xD5 = `"SSLF"` + u32 length + body. SaveRestoreState reads the magic and the length and hands the body to the engine.
  If the body starts with a 60-byte "CcnK….FPCh" (fxp) header, it is skipped.
- Body (as written by the plugin): signature `99 46 4C 53 0D 0A 1A 0A`, `"HEAD"` + u32 (6 + 2L), u16 1, u16 1, u16 L,
  L UTF-16 characters (preset name), `"DATA"` + u32 size (0xB0), `"0rev"` + 43 floats.
- The reader checks **no tag and no signature**: it takes L from body offset 0x14, the DATA size from right after the name
  + 4, and for i = 0…42 subtracts 4 from that size and sets parameter i (through the normal set-parameter routine, §1.2)
  while the remainder is still > 3. A DATA size below 4 rejects the state. Parameters not present keep their values.
- The constructor sets all 43 parameters to their defaults (§1.2) before any state is loaded.

### 1.2 Parameters (`FUN_180023c20` set, `FUN_1800234b0` default, `FUN_180023c00` raw get) **[DECOMP + EXEC]**

Every value is clamped to [0, 1] and stored raw (the effects read the raw values every block); NaN is not filtered by the
plugin. Derived values (all float unless noted):

| i | Name | Default | Derived |
|---|---|---|---|
| 0 | Voices Mode | 0.375 | `int(2p)`: 0 POLY, 1 MONO, 2 LEAD (§3.2) |
| 1 | Transpose | 0.5 | `float(trunc((p − 0.5)·24))` semitones |
| 2 | Slide Time | 0 | per-block glide factor `k = (double)((1−p)²)²·0.9 + 0.01` (the squares in float) |
| 3 | MW Dest | 0 | `int(p)`: 0 filter, 1 LFO amount |
| 4 | MW Amount | 0 | p |
| 5 | OSC Kind | 0 | `int(9p)` (§3.4) |
| 6 | OSC Mod | 0 | m = p |
| 7 | OSC Noise | 0 | p² |
| 8 | FLT Freq | 1 | F = p (§4.2) |
| 9 | FLT Peak | 0 | 0.99·p |
| 10 | FLT Kbd | 0 | p |
| 11 | FLT Vel | 0 | p |
| 12–15 | EGF Atk/Dec/Sus/Rel | 0/1/1/1 | rates `E(p, attack)` (§4.4), sustain = p |
| 16 | EGF Amnt | 0.5 | 2p − 1 |
| 17 | LFO Shape | 0 | `int(2p)`: sine, falling saw, square |
| 18 | LFO Dest | 0 | `int(2p)`: 0 cutoff, 1 pitch, 2 OSC Mod |
| 19 | LFO Rate | 0.25 | `int(11p)` → period R = 0.25, 0.375, 0.5, 0.75, 1, 2, 4, 8, 16, 64, 128, 256 beats |
| 20 | LFO Amnt | 0.5 | a = 2p − 1 |
| 21–23 | AMP Atk/Dec/Sus | 0/1/1 | `E(p, attack)`, sustain = p |
| 24 | AMP Rel | 0.07 | `10·E(0.95p + 0.05, decay)` (round 21 §3.4 confirmed) |
| 25–27 | DIST on / Overdrive / Decimator | 0/0/0 | raw (§6.1) |
| 28 | DELAY on | 0 | switching clears the line (§6.2) |
| 29 | Delay Time | 0.7 | `T = (trunc(3p) + 1)·0.25` (double) |
| 30–32 | Feedback / Filter / Mix | 0.5/1/0.5 | raw |
| 33 | CHORUS on | 0 | switching clears the line (§6.4) |
| 34–36 | Rate / Depth / Mix | 0.25/0.5/0.5 | raw |
| 37 | PHASER on | 0 | switching clears the states (§6.3) |
| 38 | Speed | 0.11222222 | `int(11p)` → the same 12 beat values as P19 (float) |
| 39–42 | Width / Feedback / Mix / Freq | 0.4/0.1/1/0.5 | raw |

---

## 2. The FL wrapper and the engine call

### 2.1 Notes and host levels **[DECOMP `FUN_1800048f0`, `FUN_180004a10`, `FUN_180003dd0`, `FUN_1800077a0`; EXEC]**

- TriggerVoice: key = `trunc(Init.Pitch·0.01)` clamped 0…127; the returned voice handle **is the key**; MIDI velocity
  `v7 = trunc(127·(float)ln(10·Init.Vol + 1)·0.32846) ` clamped 0…127 (the float `0.32846 = 1/ln 21`), the same law as GMS
  (round 39 §1.3). A second host voice on a key that already has one first sends a note-off for that key and kills the old
  host voice. The engine receives `vel = v7·(1/127)`.
- Voice_Release(handle) = note-off for that key; Voice_Kill is handled the same way.
- Before every Gen_Render the wrapper builds five 128-entry per-key tables from the host voices it holds: L and R from the
  host's ComputeLRVol(Final.Pan, Final.Vol), pitch offset `(float)(Final.Pitch·0.01 − key + 60)` semitones, cutoff offset
  = Final.FCut, resonance offset = Final.FRes. Keys without a host voice repeat the values of the previous key that has
  one (keys below the first one take the first one's values).
- After Gen_Render, host voices whose key is no longer held and has no sounding engine voice are killed.

### 2.2 Gen_Render **[DECOMP `FUN_180004780`, `FUN_180004100`; EXEC]**

The host block is cut into engine calls of at most 512 samples (engine buffer of 516 floats per channel, minus 4); each
call writes into zeroed engine buffers which are then **added** to the host buffer. While the transport plays (Dispatcher
0x0C), the wrapper reads the song position (host Dispatcher 0x24, beats) at the start of Gen_Render and advances it by
`((bpm·(1/60))·n)·(1/rate)` after each engine call; stopped, the position is 0. A change of the playing state reaches the
engine before the next engine call.

### 2.3 Engine call (`FUN_180025a40`) **[DECOMP + EXEC]**

In blocks of at most **32 samples**, with a local song position starting at the wrapper's value:
1. LFO step (§5) with the current position, then the position advances by `((m/rate)·bpm)·(1/60)`.
2. Every one of the 99 voice slots in order: if active, its block render (§4) adds into the engine buffer.
3. A silence counter: if any voice was active in this block **or any earlier block of the same engine call**, it is reset
   to 0; otherwise it grows by m unless it holds the marker 0xFFFFFFFF.
4. If the counter is below `trunc(rate·0.5)`: distortion, delay, phaser, chorus (§6), in that order; the "something ran"
   flag of step 3 is then set as well.

**Consequence [inference, checked by EXEC]:** with engine calls longer than 32 samples, the effects keep running for ever
once any note has sounded (the counter only accumulates within the first block of a call); before the first note after a
rate set (counter = marker) they do not run and the output is exactly 0.

### 2.4 Creation, sample rate, polyphony **[DECOMP `FUN_180022a30`, `FUN_1800248c0`, `FUN_1800029b0` case 4, `FUN_18001f180`, `FUN_180003380`; EXEC]**

- Constructor: 99 voices are created, each one reset once (§3.6), then the defaults are set, the 1024 tables are built
  from `oscdata.dat` (§3.3) and the engine is set to 44.1 kHz with a sample-rate set (`FUN_1800248c0`).
- Sample-rate set: when the rate changes, the delay line (`trunc(1.5·rate)` floats) and the chorus line
  (`trunc(0.01·rate)` floats) are reallocated; every time: held-key velocities cleared, LFO phase 0, all 99 voices reset
  with the new rate (§3.6), decimator counter and held values 0, silence counter = marker, the three line effects marked
  for clearing. The wrapper's Dispatcher 4 calls it only when the rate differs from the engine's current one (so at 44.1
  kHz the constructor's set is the only one) **[EXEC: the random state after creation matches only with this count]**.
- ProcessEvent 1 (max polyphony) sets the poly voice limit to its value, 0 meaning 99. **Before this event the poly limit
  is 0 and no POLY note sounds** [EXEC]. FL's value at channel creation is not confirmed (LYNX sends 0, as for GMS).
- ProcessEvent 0 = tempo (float → the engine's bpm; 120 before any event).

---

## 3. Voices

### 3.1 Voice record and activity **[DECOMP `FUN_180021070`, `FUN_180021260`, `FUN_180021320`, `FUN_180021330`, `FUN_1800213a0`]**

A voice is active while its key is ≥ 0. Reset (`FUN_180021260`): optional new rate (stored as double of the float), 8
oscillator phases drawn from the generator (§3.6), key −1, velocity 0, filter states 0, both envelope levels 0, "released"
flag set, smoothed L/R levels −1 (meaning "jump to target"). Not reset: envelope targets/rates, DC-blocker state, drift,
glide position, age.

### 3.2 Note on / off (`FUN_180024520`) **[DECOMP + EXEC]**

- The engine keeps the velocity of every key (0 = released) and the last note played (initially 0) and an age counter
  (initially 1).
- **Note on, POLY:** among the first `min(99, poly limit)` slots, the *last* inactive one; if none, the one with the
  smallest age (ties → the last such slot). **MONO and LEAD:** always slot 0. The counter is incremented and stored as
  the voice's age. The glide start is the last note played; in LEAD it is the new key itself when no other key is held.
  Then the voice starts: velocity and key set; unless (LEAD and the voice is not released), the filter-envelope level is
  set to 0, both envelopes get target 4.0 with their attack rates, and the voice is marked not released (the amp
  envelope level is **not** reset: a retriggered voice attacks from its current level). The last note becomes this key.
- **Note off:** ignored while the sustain flag (MIDI CC) is set. For every voice playing that key: in LEAD, if another key
  is held the voice moves to it (preference: the highest held key below, else the lowest held key above) with that key's
  velocity and without retrigger, and it becomes the last note; otherwise (and always in POLY/MONO) the voice is
  released: both envelope targets 0 with the release rates.
- POLY notes therefore also glide from the last note played, with the P2 factor (≈ 3 ms at P2 = 0).

### 3.3 Wave tables **[DECOMP constructor asm `0x180022dd0…0x180022e95`; EXEC: tables peeked equal the model bit for bit]**

`oscdata.dat` = 1024 float amplitudes `a_h` then 1024 float phases `φ_h`. With a running float buffer of 2048 samples,
for h = 0…1023: sample j += `a_h·cos(j·(h+1)·2π/2048 + φ_h)` (double cosine, the product and the sum in double, the
result rounded to float), and the buffer is copied into **table 1023 − h**. Table T therefore holds harmonics 1…1024 − T.

### 3.4 Oscillator set-up (`FUN_180021d10`, every 32-sample block) **[DECOMP + EXEC]**

Let f = the voice frequency (§3.5), m = OSC Mod (+ 12·L when the LFO goes to OSC Mod, L = LFO value §5), |m| its
absolute value, `r = powf(2, (float)trunc(24m − 12)·(1/12) + 0.00625)` (whole semitones −12…+12 plus 7.5 cents; round 21
confirmed). Each oscillator reads the same table with a 32-bit phase; `Δ(x) = (int)(long long)(x·4294967295.0)`.

| Kind | n | Gains | Frequencies | Phases set every block |
|---|---|---|---|---|
| 0 SAW | 3 (1 if Δ(1−|m|) = 0) | 1, −1, 1 | f, f, f | p1 = p0 + Δ(1−|m|), p2 = p0 |
| 1 SAW+SAW | 2 | 1, 1 | f, f·r | — |
| 2 PULSE | 2 | 1, −1 | f, f | p1 = p0 + Δ(1−|m|) |
| 3 SQR+SAW | 3 | 1, −1, 1 | f, f, f·r | p1 = p0 + 0x7FFFFFFF |
| 4 SQR+SQR | 4 | 1, −1, 1, −1 | f, f, f·r, f·r | p1 = p0 − 0x70000001, p3 = p2 − 0x70000001 |
| 5 SUPERSAW | 8 | 0.5 each | `((0.0125k − 0.05)·m + 1)·f`, k = 0…7 | — |
| 6 BELL | 6 | 0.5 each | f | p_k = p0 + k·Δ(|m/2|) |
| 7 DEAF SAW | 2 | 1, 1 | f, f | p1 = p0 + Δ(1−|m|) |
| 8 SAW SPREAD | 3 | 1, 1, 1 | `(1 − 0.01m)·f·0.5`, f, `(f + f)·(1 + 0.01m)` | — |
| 9 SAW SPREAD 2 | 3 | 1, 1, 1 | same with 1.5 instead of 0.5 | — |

(So the SQR+SQR "squares" are 56.25 %/43.75 % pulses; the SQR+SAW square is a 50 % one.) Phase increment `(int)(long
long)((freq/rate)·4294967295.0)`. **Table choice:** only the first oscillator's frequency is used for all of them — if
`(float)freq₀ > (float)rate·(1/2048)`, table `1023 − trunc((0.5/freq₀)·rate)` (float, clamped 0…1023), else table 0.

### 3.5 Pitch, drift and glide (`FUN_180022250`, every block) **[DECOMP + EXEC]**

One generator draw u (§3.6): `drift ← ((float)(u·2⁻³¹ − 1) − drift)·0.01 + drift`; glide `g ← (key − g)·k + g` (k from P2);
note = `((g + transpose) + 0.08·drift)` (float) + pitch-bend + (12·L if LFO → pitch) + the per-key pitch offset (§2.1),
in double; `f = 2^((note − 57)/12)·220`. The drift also moves the cutoff (§4.2).

### 3.6 Random generator **[DECOMP; EXEC: state peeked]**

One 32-bit generator global to the DLL (shared by every MiniSynth instance in the process, initial value 0). Draws: per
sample and active voice `x ← x·0x0BB38435 + 0x3619636B` (the old value feeds the noise, the new one's low 22 bits the
dither); per block and voice one draw of the same rule (drift); a voice reset takes 8 values, each after 128 steps of
`x ← x·0xB464B2F9 + 0x9D6F2492`.

---

## 4. Voice render (`FUN_180022440`)

### 4.1 Control per block **[DECOMP + EXEC]**

v = velocity, but 0 when the amp envelope level is below 1e-5 with target 0. Targets `tL = v·L[key]`, `tR = v·R[key]`;
negative smoothed levels jump to their targets. If the voice is released, v = 0 and both smoothed levels are below 1e-5,
the voice is reset (the 1024 generator steps of §3.6) and renders nothing. Otherwise: pitch (§3.5), oscillators (§3.4)
and filter (§4.2).

### 4.2 Filter **[DECOMP `FUN_1800213e0` + asm `0x1800213e0…0x180021cb7`; EXEC]**

Octaves (double, in this order): `((vel − 0.5)·FLTVel)·14 + ((note − 48)·Kbd)/12`, `+ 4`, `+ (0.103·drift + 10.3·F)`,
`+ 4·L` if the LFO goes to cutoff, `+ 4·(MWamt·MW)` if the mod wheel goes to the filter; `fc = 2^(FCut[key] + octaves)`;
with the filter envelope level E: `fc' = min((2^(10·EGFamt)·fc − fc)·E + fc, 20000)`; `g = clamp(fc'·6.25/(4096·rate), 0, 1)`;
`k = clamp(FRes[key] + 0.99·Peak, 0, 1)·(−4.05)`.

The filter is a 4-pole cascade with feedback, integrated **exactly per sample by a matrix power**: with `a = 1 − g`, the
sub-step matrix (state y1…y4, input x held constant) is

```
y1' = a·y1                     + k·g·y4      + g·x
y2' = a·g·y1  + a·y2           + k·g²·y4     + g²·x
y3' = a·g²·y1 + a·g·y2 + a·y3  + k·g³·y4     + g³·x
y4' = a·g³·y1 + a·g²·y2 + a·g·y3 + (k·g⁴ + a)·y4 + g⁴·x
```
(products accumulated left to right: `a·g` then `·g`…), and the per-sample transition is its **4096th power**, obtained
by squaring it 12 times. With A the 4×5 matrix (row i = output yᵢ', columns y1…y4, x) the squaring computes, exactly in
this grouping (needed for bit-exactness):

```
A00 = (A10·A01 + A00·A00) + (A20·A02 + A30·A03)        A01 = ((A11 + A00)·A01 + A21·A02) + A31·A03
A02 = ((A00 + A22)·A02 + A12·A01) + A32·A03            A03 = ((A33 + A00)·A03 + A13·A01) + A23·A02
A04 = ((A34·A03 + A04) + A24·A02) + (A14·A01 + A04·A00)
A10 = (A00·A10 + A10·A11) + (A20·A12 + A30·A13)        A11 = (A01·A10 + A11·A11) + (A21·A12 + A13·A31)
A12 = (A11·A12 + A10·A02) + (A12·A22 + A13·A32)        A13 = (A33·A13 + A23·A12) + (A13·A11 + A03·A10)
A14 = ((A34·A13 + A14) + A24·A12) + (A04·A10 + A14·A11)
A20 = (A30·A23 + A20·A22) + (A00·A20 + A10·A21)        A21 = (A01·A20 + A11·A21) + (A23·A31 + A21·A22)
A22 = (A22·A22 + A23·A32) + (A21·A12 + A20·A02)        A23 = (A03·A20 + A13·A21) + (A33·A23 + A23·A22)
A24 = ((A34·A23 + A24) + A24·A22) + (A04·A20 + A14·A21)
A30 = (A33·A30 + A32·A20) + (A00·A30 + A10·A31)        A31 = (A33·A31 + A32·A21) + (A30·A01 + A11·A31)
A32 = (A31·A12 + A30·A02) + (A32·A22 + A33·A32)        A33 = (A33·A33 + A23·A32) + (A03·A30 + A13·A31)
A34 = ((A34·A33 + A34) + A24·A32) + (A04·A30 + A14·A31)
```
(all right-hand sides use the previous matrix).

### 4.3 Per sample **[DECOMP + asm `0x180022510…0x1800228ac`; EXEC]**

1. Draw (§3.6): r = old value, d = new value & 0x3FFFFF.
2. Oscillators: `s = Σ (double)(T[(p_k + d) >> 21]·gain_k)` (32-bit wrap; 11-bit index with 21-bit dither), then
   `p_k += inc_k`.
3. Noise `(float)(r·2⁻³²·2 − 1)·noise` (float), input `x = ((noise + s)·0.2 + 0.07)·Eamp` (double; note the constant
   offset 0.07 before the filter).
4. New states: `y4 = ((x·A34 + y4·A33) + y3·A32) + (y1·A30 + y2·A31)` and likewise
   `yᵢ = ((y4·Aᵢ3 + x·Aᵢ4) + y3·Aᵢ2) + (y1·Aᵢ0 + y2·Aᵢ1)` for i = 1, 2, 3 (all from the old states), then the first stage
   saturates: `y1 ← y1/(y1² + 1)`.
5. Output `o = 5·y4`; DC blocker `dc ← (float)((o − dc)·0.005 + dc)`, `o −= dc`.
6. Envelopes (float): `E ← (target − E)·rate + E` for the filter and amp envelopes; above 1.0 a level is set to 1.0 and
   switches to target = sustain with the decay rate.
7. Levels (float): `gL ← (tL − gL)·0.005 + gL`, same for gR; `L += gL·o`, `R += gR·o` (double, rounded to float).

### 4.4 Envelope rates **[DECOMP `FUN_1800229c0`]**

`E(p, attack)`: if p ≥ 0.001, `k = 0.95p + 0.05`, `d = k⁴·(500 attack | 50 otherwise)`; if d ≥ 0.001 the per-sample
coefficient is `(float)(0.0002/d)`, otherwise 1.0. The coefficients do **not** depend on the sample rate (times scale
with 44.1 kHz/rate — round 21's inference confirmed by the code). Attack targets 4.0 and switches to decay at 1.0.

---

## 5. LFO (`FUN_1800249b0`, once per 32-sample block, engine-wide) **[DECOMP + EXEC]**

Value from the current phase φ: sine `(float)sin(2πφ)`, falling saw `(float)(2(1 − φ) − 1)`, square `φ < 0.5 ? 1 : −1`.
Amount a (P20) and a₂ = a (+ MWamt·MW when the mod wheel goes to the LFO): a > 1 → raw value; a < −1 → negated; else
`L = a₂²·value`, negated when a < 0. Then the phase: playing → `φ = frac(position/R)`; stopped →
`φ = frac(φ + ((1/60)/R·bpm)/rate·m)`. Destinations: cutoff +4L octaves, pitch +12L semitones, OSC Mod +12L
(rounds 10/21 confirmed). The voices of a block use the value computed at the start of that block.

---

## 6. Effects (on the summed engine buffer, every 32-sample block while they run, §2.3)

### 6.1 Distortion (`FUN_180025150`) **[DECOMP + asm; EXEC]**

On when P25 ≠ 0. `D = 20·P26²`, gain `g = (float)(((1 − D/16)⁸)·0.7 + 0.3)` (the power by three float squarings).
Per channel: x ≥ 0 → `1 − powf(10, −D·x)·(1 − x)`, x < 0 → `powf(10, D·x)·(x + 1) − 1`, times g. Decimator: `H =
trunc(64·clamp(P27², 0, 1))`; when H ≠ 0 a counter c: if c > 0 the held L/R pair replaces the output, else the current pair
is held and c += H; then c = trunc(c − 1).

### 6.2 Delay (`FUN_1800257c0`) **[DECOMP + asm; EXEC]**

Mono line of `N = trunc(1.5·rate)` floats. Length: `q = T·40/bpm`, halved while > 1, `len = clamp(trunc((N − 1)·q), 1, N)`
(T = 0.25…1 → ¼…1 beat at 120 BPM). Per sample at the read/write position p (wrapping at len): low-pass state
`s = (float)(((double)(float)(fb·line[p]) + 1e-30 − s)·c + s)` with `c = 0.99·Filter + 0.01`, fb = P30; `line[p] = (R + L) + s`;
wet `s·P32²` added to both channels; a wet level above 1e-5 resets the silence counter.

### 6.3 Phaser (`FUN_180024b50`) **[DECOMP + asm; EXEC]**

Bounds `hi = 0.5·40000/rate`, `lo = 0.5·100/rate`; width `w = powf(P39, 3)·0.5`; feedback `fb = powf(P40, 1/3)·0.98`;
target centre `T = max(P42², 1e-5)` approached per sample by `F ← (T − F)·0.001 + F` (F starts at T); output level
`max(P41, 1e-5)`. LFO per block: playing → `φ = frac(position/S)`, stopped → `φ = frac(φ + bpm/(S·60·rate)·m)` (S from
P38, float), `c = cos(2πφ)`; left uses `(1 + c)·w`, right `(1 − c)·w`. Per sample and channel:
`g = clamp((F + offset)·(hi − lo) + lo, 1e-5, 0.499)`, `α = (1 − g)/(1 + g)`; input `x + 1e-30` plus `fb·` the channel's last
output; six first-order all-pass stages `v = s − α·in; s = v·α + in; in = v`; the last v is stored and added times the
output level.

### 6.4 Chorus (`FUN_1800253f0`) **[DECOMP + asm; EXEC]**

Mono line of `N = trunc(0.01·rate)` floats written with L + R. `D = (P35·0.9)·0.4 + 0.04`, mix `P36²`, rate
`r = 4·P34² + 0.2`; two phases (start 0.62 and 0.1) advancing by `r/rate` and `(r + 0.05)/rate`. Left tap at
`N·D·((float)(sin(2πφ₁)·D + 0.5))²` samples after the write position, linearly interpolated; **the right tap at only
`sin(2πφ₂)·D + 0.5` samples** (0.5 ± D, i.e. almost undelayed — a quirk of the code, reproduced). Taps times mix are added
to their channels.

---

## 7. Precision limits **[EXEC `run/callf.exe` + model]**

The DLL's own math routines differ from correctly rounded/glibc results in the last bit for a few percent of arguments
(`tmath.py`, 200 000 random arguments): its `sin` (`FUN_18004bf10`) differs from glibc's on 3.2 %, its `cos`
(`FUN_18004b090`) on 3.2 %, its `pow` (`FUN_180035b50`) on 0.2 %. Everything else in the engine (integer phases, the
generator, float/double arithmetic with the orders given above) is reproduced exactly; these last-bit differences reach the
output only through the chorus (sine → tap position), the distortion (`powf`) and occasionally the LFO and the phaser.

## 8. Verification against the DLL **[EXEC + model]**

Events (unless noted): C5 Init.Vol 0.8 0–20000, E5 0.6 (Final.Vol 0.7) 3000–26000, G5 1.0 (+25 cents) 9000–33000, C4 0.9
40000–52000 (installation states: offs at 30000/36000/44100, C4 50000–70000), host block 512, L = R = Final.Vol, tempo 120,
ProcessEvent 1 = 0.

| Set | Bit-identical | Others |
|---|---|---|
| 12 installation states (`mbat.py`, 2 s) | **11/12** | Pluck Harmonies −144 dB (12 samples, `powf` in the distortion) |
| 91 factory presets (`pbat.py`, 1.5 s, `out/pbat2.log`) | **47/91** | all within −107…−147 dB; each of the four worst became bit-identical with the chorus switched off (`tmod.py`) |
| Variants (`tvar.py`, `out/tvar1.log`) | 48 kHz (2 states), host blocks 64/100/1000/2048, tempo 90/97, transport playing at 3.3 beats (LFO and phaser song-locked), max polyphony 1 and 2 (stealing), MONO 48 kHz, same-key retrigger, Init.Vol 0.05/2.0, LEAD same key | pan law A −140 dB (the harness' and Python's cos/sin of the host law differ in the last bit) |

**LYNX's C++ engine** (`build-local/minisynthrender`, `ccmp.py`): identical results to the model on all 12 installation
states and all 91 presets (same per-preset mismatch counts against the DLL; 11/12 and 47/91 bit-identical to the DLL).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| 1 | DLL structure | native FL generator on IL's CFLWrapper/CSynthesizer framework (as GMS); "Fruity Wrapper" is only the framework's default host name **[DECOMP]** |
| 2 | State | "SSLF" + .flms; reader takes the name length and DATA size, no tag check; 43 floats via set-parameter **[DECOMP+DATA]** |
| 3 | Engine call | ≤ 512 per call, 32-sample blocks, 99 voice slots, effects gated by a silence counter that in practice never stops them after the first note **[DECOMP+EXEC]** |
| 4 | Oscillators | table saws (1024 band-limited tables from oscdata.dat), 10 kinds as sums of 1–8 phased/detuned saws, dithered 11-bit read, table chosen by the first oscillator **[DECOMP+EXEC]** |
| 5 | Filter | 4-pole cascade with feedback −4.05·res, exact per-sample matrix (4096 Euler sub-steps by 12 squarings), saturating first stage, +0.07 input offset and a DC blocker **[DECOMP+EXEC]** |
| 6 | Envelopes / LFO | per-sample exponential followers `0.0002/((0.95p+0.05)⁴·K)`, rate-independent; LFO per 32 samples, squared amount, beat periods, song-locked while playing **[DECOMP+EXEC]** |
| 7 | Voices | POLY last free slot / oldest; MONO/LEAD slot 0; glide from the last note (also in POLY); LEAD returns to held keys **[DECOMP+EXEC]** |
| 8 | Effects | distortion + decimator, tempo delay, 6-stage stereo phaser (cosine LFO), chorus with an almost undelayed right tap **[DECOMP+EXEC]** |
| 9 | Random | one process-global LCG (two rules), 0 at load; 99 voice resets at construction and per rate set **[DECOMP+EXEC]** |
| 10 | Verification | model and LYNX's C++ engine bit-identical to the DLL on 11/12 installation states and 47/91 presets, the rest within −107…−147 dB (CRT last-bit) **[EXEC+model]** |

---

## 9. For LYNX — port status

Ported 1:1 from `r41/py/msmodel.py` in `src/core/MiniSynth.{h,cpp}` (`InstrumentKind::minisynth`, appended to the
enum). The importer keeps the raw 0xD5 state in `ChannelDef::pluginState` and the path of the user's
`Plugins\Fruity\Generators\Minisynth\oscdata.dat` in `ChannelDef::samplePath`; the 1024 tables are built once per file and
cached. Without the file (or with an unreadable state) the old internal-synth conversion `synthFromMiniSynth` stays.

- Host side as for GMS (round 39 §3.1): Final.Vol/Pan recovered from LYNX's channel gains and fed through the host's
  law A per note (not applied a second time); Final.Pitch = 100·(key − 60) + channel pitch + note cents; Init.Vol =
  `(11^(1.28·vel) − 1)/10`; tempo = song tempo; max-polyphony event 0; while the transport plays the song position locks
  the LFO and the phaser LFO.
- Random: one generator per instance, started like a freshly created plugin at 44.1 kHz (then the host rate) — FL's is
  shared by all instances of the process, so noise, dither, drift and the random start phases cannot match a given FL
  render (by design, as GMS).
- `tools/minisynthrender.cpp` renders a state exactly like the model's driver (`ccmp.py` compares it with the DLL).
- `tests/tests.cpp`: `minisynth_engine` (two states written from scratch on a synthetic oscdata against golden samples of
  the model, the parser, a missing table file, and the instrument against the reference renderer) and, with FL's Data
  folder, every installation MiniSynth channel on the exact engine plus two Exoplanet channels (Main Lead 1, Riser) against
  goldens of the model that are bit-identical to the DLL's own output for those events.
- The channel settings panel shows 14 of the plugin's knobs (raw 0…1 values inside the state).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `CreatePlugInstance` `0x1800022c0`, `FUN_180006860`/`FUN_1800068b0`; RTTI (`out/ms_vft.txt`) | framework, "Fruity Wrapper" default host name | DECOMP |
| CFLWrapper VMT `0x180061410`: `FUN_1800029b0` (Dispatcher), `FUN_180002f10` (SaveRestoreState), `FUN_180003380` (ProcessEvent), `FUN_180004780` (Gen_Render), `FUN_1800048f0` (TriggerVoice), `FUN_180004a10` (Release) | plugin interface, velocity, voice handle = key, tempo / max-polyphony events, rate set | DECOMP + EXEC |
| `FUN_180004100`, `FUN_180003dd0`, `FUN_180004840` | engine calls ≤ 512, song position, per-key tables, host voice kill | DECOMP + EXEC |
| CMiniSynth VMT `0x180063848`; `FUN_180022a30` (constructor), `FUN_1800248c0` (rate), `FUN_180023310`/`FUN_1800233b0` (lines), `FUN_18001f180` (polyphony) | creation, buffers, resets | DECOMP + EXEC |
| `FUN_180023c20`, `FUN_1800234b0`, `FUN_180023c00`, `FUN_1800229c0`, `FUN_180025e70`, `FUN_180025d60`, `FUN_180007700` | parameters, defaults, envelope rates, state read/write | DECOMP + EXEC |
| `FUN_180024520`, `FUN_180022950`, `FUN_180022970`, `FUN_1800077a0` | note on/off, allocation, LEAD | DECOMP + EXEC |
| CMiniSynthVoice VMT `0x180063808`: `FUN_180021070`, `FUN_180021260`, `FUN_180021330`, `FUN_1800213a0`, `FUN_180022440` (asm `out/voice_render.s`), `FUN_180022250` | voice reset, start, release, control, per-sample loop | DECOMP + EXEC |
| `FUN_180021d10` (asm `0x180021d10…0x180021dcc`), `FUN_1800371f0` (powf) | oscillator kinds, detune ratio, table choice | DECOMP + EXEC |
| `FUN_1800213e0` (asm `out/filter.s`, `py/symsse.py`), `FUN_180035b50` (pow) | cutoff law, matrix exponential | DECOMP + EXEC |
| constructor asm `0x180022dd0…0x180022e95`, `FUN_1800489f0` (vector cos) | wave tables | DECOMP + EXEC (`t_tab.py`) |
| `DAT_180079cf4` and its three users | random generator | DECOMP + EXEC |
| `FUN_1800249b0`, `FUN_18004bf10` (sin) | LFO | DECOMP + EXEC |
| `FUN_180025a40` | engine call, silence counter, effect order | DECOMP + EXEC |
| `FUN_180025150`, `FUN_1800257c0`, `FUN_180024b50` (`FUN_18004b090` = cos), `FUN_1800253f0` | the four effects | DECOMP (asm) + EXEC |
| `out/blobs41.pkl`, `out/presets41.pkl`, `py/survey41.py`, `py/presets.py` | §0 | DATA |
| `run/ms.exe`, `run/ms_sp.exe`, `run/callf.exe`; `mbat.py`, `pbat.py`, `tvar.py`, `tmod.py`, `tmath.py`, `ccmp.py` | §7–§8 | EXEC + model |

### Open questions

- **FL's own host answers:** the max-polyphony value FL sends at channel creation (without the event no POLY note sounds),
  the velocity → Init.Vol law (round 28 inference), the song-position format of Dispatcher 0x24 (beats, as round 39) — not
  observed from FL itself.
- **Random generator:** process-global in FL; its state at any point of an FL session (other MiniSynth instances, rate
  changes) is not reproducible.
- **CRT last-bit differences** (§7): reproducing them would need the DLL's own sin/cos/pow algorithms; not attempted.
- **Not exercised:** pitch bend, mod wheel and sustain (MIDI messages the wrapper forwards; FL delivers bends through
  Final.Pitch), Final.FCut/FRes ≠ 0 (note properties), parameter automation during notes beyond the per-block reading,
  the fxp-header branch of the state reader.
