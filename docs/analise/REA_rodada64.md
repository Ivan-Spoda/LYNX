# Behavioural analysis of FL Studio 2026: round 64 (REA) — Wasp / Wasp XT (a 64-bit placeholder) and SimSynth (the complete engine)

> **Nature of this document.** Same goal as rounds 41 (MiniSynth), 42 (DX10) and 47 (3x Osc): describe two generators
> that LYNX did not play precisely enough to port them as exact instruments. *Clean-room* reverse engineering,
> behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts, formulas and behaviours
> described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No
> encrypted content was touched and no Image-Line content (code, audio, tables) was copied into the repository: every
> table the engine needs is recomputed from its formula.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Where there was not enough
> evidence, the item is marked **"not confirmed"**; inferences made *from* the code are stated as **inference**.
> Results of our own C++ engine (which implements only DECOMP/EXEC facts) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of LYNX's engine, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (copies in `r64/bin/`):
  - `Plugins/Fruity/Generators/Wasp/Wasp_x64.dll` and `Wasp XT/Wasp XT_x64.dll`: **byte-identical**, SHA-256
    `d440c2d76298a4fe5e8bcf107f5eb6f1be5c7b3a5591e03b2a26c8a14aa01ffd`, Delphi, image base 0x400000; both folders'
    `Plugin.nfo` carry `ps_dummy64bit=1` (no other generator folder of the installation does).
  - `Plugins/Fruity/Generators/SimSynth/SimSynth_x64.dll` (Delphi front-end, SHA-256
    `3a13b8278090e0927e1af5405e4154e555fcbbc2ed3d358bf6e405e92cea3da4`) and `SimSynth_Eng_x64.dll` (C++/MSVC engine,
    image base 0x180000000, one export `CreatePlugInstance`, SHA-256
    `35ab98a478426c71b90c3074613ec978e0b0cad5c4c7498b439321fbbc8971c5`).
  - `FLEngine_x64.dll` (= rounds 30/47) only for the host's integer-pan ComputeLRVol (host slot 3, `0x132acd0`).
- **Tools:** Ghidra 12.1.4 headless (JDK 21), projects `~/ghproj/r64_wasp`, `r64_sse`, `r64_sim`; our scripts
  `r64/gh/scripts/DecRange.java` (whole ranges → `r64/out/sse_all.c`, engine part `sse_core.c`) and `DecAt.java`
  (single functions → `r64/out/w_*.c`, `s_*.c`); `objdump -M intel` (`r64/out/sse_core.s`, `f6660.s`, `f8180.s`,
  `fae50.s`) wherever the float order or an argument register mattered; `r64/py/pe.py`, `classes.py` (Delphi VMTs).
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r64/run/ssx.c` → `ssx.exe`, a copy of round 47's `ox.c`
  (same safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded
  unmodified, minimal fake host, state restored through the plugin's own SaveRestoreState from a read-only `IStream`,
  `TerminateProcess`, `timeout`). Additions: host slot 3 = FL's integer-pan ComputeLRVol (§2.4); `oldvp` writes the
  old voice record (integer Final.Pan and Final.Pitch, §2.4); `eg=` dumps globals of the engine DLL (random table, its
  index, block and chorus counters) after creation, after the state and after the rate set; host Voice_Kill with
  KillHandle = 0 makes the harness forget the note instead of killing the voice (`SS_KH=1`). `r64/run/wx.c` (the plain
  copy) ran the Wasp DLLs; `callf.exe` identified the engine's C-runtime math routines. No process or dialog was left
  behind; outputs were deterministic run to run.
- **Drivers** (`python3 -I`, under `r64/py/`): `collect64.py` (states), `ssrun.py` (EXEC), `cmp.py` (EXEC against
  LYNX's `build-local/simsynthrender`), `bat.py` (every state, scenarios A–D), `gold.py` (test goldens). Logs in
  `r64/out/` (`bat2.log`, `bat3.log`).
- **Data:** `r64/out/states64.pkl` — every Wasp / Wasp XT / SimSynth 0xD5 state of the installation: Wasp 47 presets
  (version 1, 169 bytes = int version + 41 ints 0…512 + 1 byte), Wasp XT 40 presets (version 11, 213 bytes, 52 ints),
  SimSynth 80 states (**76 distinct**: 73 plugin presets, 2 channel presets, 3 plugin-database entries, the render test
  `System/Render tests/SimSynth.flp`), all 564 bytes, versions 7 and 14. **No demo or template project uses any of the
  three plugins.**

---

## 1. Wasp and Wasp XT in FL Studio 2026 **[DECOMP + EXEC]**

### 1.1 The DLL is a shared placeholder

- `CreatePlugInstance` (`0x6a51d0`) upper-cases the DLL's file name and looks it up in a list (`FUN_006a4c00`): EFFECTS,
  FRUITY LSD, REWIRED, DASHBOARD, WAVE TRAVELLER, FRUITY SCRATCHER, **WASP**, **WASP XT**, FRUITY SOUNDFONT PLAYER,
  SYNTHMAKER, BUZZ GENERATOR ADAPTER, BUZZ EFFECT ADAPTER, RAZER CHROMA, FL SLAYER. A known name selects a plugin-info
  record and a number handed to host Dispatcher 26 (Wasp and Wasp XT: 0x8214; its meaning is **not confirmed**), the
  info flags get 0x8000, and the object created is class `TTestPlug` (strings "DummyWasp", "DummyWaspXT", "This
  plugin doesn't have a 64 bit version."). In this installation only the two Wasp folders contain this DLL
  (MD5 survey of `Plugins/Fruity`).
- Info records: Wasp `0x6f2680`, Wasp XT `0x6f2728`: flags `0x408011` (generator, note input), **41 / 56 parameters**,
  4 voices. Neither has a rendering engine anywhere in the installation: FLEngine only holds translation strings
  ("Wasp DXi Editor"), and no other Wasp binary exists.
- `TTestPlug` methods (VMT `0x6a4ae8`): SaveRestoreState (`0x6a52e0`) stores the incoming stream's bytes verbatim and
  writes them back unchanged; ProcessParam returns its value argument; **TriggerVoice returns −1 (no voice)**;
  Gen_Render and the voice calls do nothing.

### 1.2 EXEC

`wx.exe` on both DLLs with the presets *Basswasp* (Wasp) and *Afraid Of The Dark Pad* (Wasp XT), two notes, 1 s at
44.1 kHz: TriggerVoice returned −1 for both notes, the output buffer stayed exactly 0, and the state saved back after
loading was byte-identical to the one loaded.

**Consequence:** in FL Studio 2026 a Wasp or Wasp XT channel is silent and keeps its saved settings. LYNX does the same
(`InstrumentKind::wasp`, `src/core/Wasp.*`); before this round LYNX played such channels with its built-in synth.

---

## 2. SimSynth: structure, state, host interface

### 2.1 Front-end and engine **[DECOMP + EXEC]**

`SimSynth_x64.dll` is the same Delphi `TTestPlug` framework (info `0x725c1c`: flags `0x10408011`, **26 parameters**
exposed to FL, 16 voices). Its constructor (`FUN_006cd010`) loads `SimSynth_Eng_x64.dll` from its folder, creates the
engine with the **host object FL gave it**, and every plugin call (Dispatcher except editor messages, SaveRestoreState,
ProcessParam, TriggerVoice, Voice_Release/Kill/ProcessEvent) is forwarded. Gen_Render (`FUN_006cdc60`) zeroes its own
buffer, calls the engine's Gen_Render on it and copies only the number of samples the engine reports back (§4.3).

Engine class `SSPlug` (VMT `0x18006b998`: Dispatcher `0x18000a4a0`, SaveRestoreState `0x18000a610`, ProcessParam
`0x18000ac40`, Gen_Render `0x18000ae50`, TriggerVoice `0x18000b700`, Voice_Release `0x18000bc20`, Voice_Kill
`0x18000bc50`; ProcessEvent and Voice_ProcessEvent return 0). Inside: a "Preset" object (plugin `+0x258`) holding the
45 parameter values, 45 modulation offsets (always 0 here), the 19 switches, a **patch-data block** (0xC1A0 bytes: three
4096-point wave tables and the derived integer settings) and a list of **12 voice objects** (0x340 bytes each, class
`patch`), plus 12 host-voice slots (`TPlugVoice`) and a plugin-level chorus.

### 2.2 Process-wide data **[DECOMP `FUN_180008f40`, `FUN_180008830`; EXEC]**

Rebuilt whenever the rate passed differs from the one they were built for (a global): the voice set-up asks for
**2 × sample rate** once the "quality" byte of the patch data is set (§2.3), else the sample rate.

- Pitch tables for cents i = 0…29999: `f = (float)(2^(i/1200)·4.088681697845459)` (C-runtime `pow`), increment
  `(int32)(int64)((2^32/rate)·f)` (float product), period `(int32)(int64)(rate/f)`. Lookups of the second (unison)
  increment read 20 entries further; past the end they read the first 20 periods (contiguous globals).
- Cent → index map (16-bit): entries 0…41 → 10, **42…73 → 0** (never written), 74…0x754B → i − 32, above → 0x752C;
  lookups add 32 to the cent value.
- **Random table:** 65536 floats `rand()/32767` from the C runtime of the DLL (MSVC LCG). Every voice set-up ends with
  `srand(666)`, so every rebuild after the first one is the sequence from seed 666 **[EXEC: dumped table = seed-666
  sequence]**. One **16-bit read index shared by everything** (oscillator start phases, LFO sample & hold, noise).
- Wave sources (4096 points, float): half cosine `sin(π/2·x)` with x from 1 stepping −1/2048; sine `sin(y)` with y
  stepping 0.0015339808; `sine⁴` (`((s·s)·s)·s`); triangle 0 → +1/1024 per point (1024 points), −1/1024 (2048),
  +1/1024 (1024). `sin`/`cos`/`exp`/`pow`/`fmod` were identified by calling the DLL's own routines (`callf.exe`).

### 2.3 Creation, state, sample rate **[DECOMP + EXEC]**

1. `CreatePlugInstance`: chorus set-up (§4.2); the 12 voices are linked one by one, each set up from the fresh patch
   data at the plain rate; per-voice spread values (§3.2); quality byte := 1 (no set-up), render-mode byte := 0 with
   every voice set up again (tables now at 2× rate); the engine's built-in patch (doubles `0x18006b790`, switches
   `0x18006b900`) applied with set-ups; then the **front-end sends its 58 control values** (all with voice updates).
2. SaveRestoreState (load, `FUN_18000a610`): **564 bytes** = 45 doubles P[0…44], 19 ignored ints, 19 switch ints
   S[0…18], 12 ignored ints, version. Version fix-ups: v = 0 → P39 ×0.75; v < 5 → for k = 1, 8, 15
   `x = (int)(24·P[k] − 12)`, +12 if `P[k+5] == 0.3`, `P[k] = (x + 24)/48`; v < 13 → `x = (int)(48·P[k] − 24)`,
   `P[k] = (x + 24)/48`. Then: offsets 0, pitch base 2400, chorus switch, gain = (float)P39, the patch applied (switches,
   then parameters, without voice updates; then every voice set up, silent). The engine writes version 14.
3. **The front-end then reads back and writes again all 58 host parameters** (0…25, then 26…57, `FUN_006cdd90`): a knob
   comes back as `⌊scale·P⌋` and is written as `v/scale` (scale 65535; the three coarse-pitch knobs: 20160 read, divided
   by 420 with round-to-nearest, written ×420) — **every knob value is quantised to 1/65535** and the wave selectors
   and switches are re-applied with voice updates (they redraw oscillator start phases).
4. Dispatcher 3 (block size) only sizes a buffer. Dispatcher 4 (sample rate): rate globals (`rate`, `44100/rate`,
   `rate/1000`), chorus set-up, then Dispatcher 1 (ProcessMode, value ignored): render-mode byte 0 (no set-up), quality
   byte 1 **with every voice set up again**. The quality byte is therefore always 1 in FL: only the two-reads-per-sample
   oscillator (§3.2) and the 32-sample filter update (§3.7) are reachable.

EXEC: the random index after creation is **276**, after the *Default* preset 648, after the rate set 696; LYNX's engine
reproduces these counts for every state (they fix every oscillator's start phase).

### 2.4 Host interface **[DECOMP + EXEC]**

- **Old voice record.** SimSynth's info flags lack bit 21 (`0x200000`), which every plugin that reads float voice
  levels has (MiniSynth `0x8200011`, 3x Osc `0x11600039`, DX10 `0x11608431`, measured in the same harness). The engine
  passes Final.Pan's 32 bits as an **integer** to host slot 3 (FL's ComputeLRVol with integer pan: `pan/64`, then the
  usual law, round 30 §1.3) and Gen_Render reads Final.Pitch's 32 bits as an **integer** (`mov edx,[r+0x1c]`). Our
  **inference** from the FL SDK's flag `FPF_NewVoiceParams`: FL hands SimSynth the old record, Pan an integer −64…64 and
  Pitch an integer (cents); Vol, FCut, FRes are floats. With float values (wrong record) every note but C5 plays at the
  wrong pitch **[EXEC]**; with integers everything is consistent. FL's rounding when it fills the old record is
  **not confirmed** (LYNX rounds to nearest).
- TriggerVoice converts Final.Pitch **as a float** (`cvttss2si`) — with the integer record that is garbage, but it only
  lives until the next Gen_Render, which sets the pitch again before any sample is rendered (no audible effect).
- **Slots:** the first free of 12; if none, the voice released longest ago (block counter − release stamp, released
  ones only), else the one with the **highest start stamp** (the newest), ties to the lowest slot. A stolen voice is
  silenced at once and the host gets Voice_Kill(old handle, KillHandle = 0) (the plugin reuses the handle).
- On trigger: L/R from ComputeLRVol(Final.Pan, Final.Vol·gain), velocity `(int)(Final.Vol·127)/255` (stored, **unused**),
  note-on of the voice (§3.3), release stamp 0, start stamp = block counter.
- Release: release stamp := block counter, both envelopes released. Kill: the voice silenced, slot freed.

---

## 3. The voice

### 3.1 Parameters → patch data **[DECOMP `FUN_180001820`, `FUN_180002340`, table `0x1800831e0`]**

Values are clamped to 0…1 after adding the (zero) offset. Per oscillator k (base 7k):

| P | meaning | data |
|---|---|---|
| 7k | shape | `2 − (int)(P·−96)`; table rebuilt |
| 7k+1 | coarse | `((int)(⌊48P⌋ − 24))·100` cents |
| 7k+2 | fine | `(int)(200P − 100)` cents |
| 7k+3 | level | `(int)(100P)` → oscillator gain `(L/100)²` |
| 7k+4 | LFO → pitch | `(int)(200P − 100)`, ×12 cents per unit of LFO |
| 7k+5 | envelope → pitch | `(int)(200P − 100)`, ×12 cents per unit of envelope |
| 7k+6 | wave | 0.2 pulse (kind 0), 0.1 half cosine (1), 0 triangle (2), 0.3 sine/sine⁴ mix (4), 0.4 noise (3, no rebuild); then the coarse parameter is re-applied |

P21 LFO delay `(int)(100P)`; P22 LFO rate `(int)(999P)`; P24 LFO shape 0.2 square, 0.1 saw, 0 triangle, 0.4 sample &
hold; P26 cutoff `(int)(255P)`; P27 resonance `(int)(255P)`; P28 envelope → cutoff `(int)((P − 0.5)·255)`; P29 LFO →
cutoff likewise; P30 key tracking; P32/P33 filter mix angles `(int)(255P)`; P35–38 amp attack `(int)(P³·8000)` ms, decay
`(int)(P²·8000)`, sustain `(int)(255P)`, release `(int)(P²·8000)` (≥ 2 ms); P39 output gain (float P); P41–44 the filter
envelope likewise (release ≥ 1 ms). P23, P25, P31, P34, P40 are stored but never reach the sound.
Switches: S0/S4/S8 oscillator on, S1/S5/S9 +1 octave layer, S2/S6/S10 +2 octave layer, S3/S7/S11 unison copy, S12 LFO
on, S13 LFO retrigger, S14 filter on, S15 (filter kind flag, unused), S16 filter envelope **off** = use the amp
envelope, S17 ring modulation (osc 1 × osc 2), S18 chorus.

### 3.2 Wave tables and oscillators **[DECOMP `FUN_180007ae0`, `FUN_180006660`, `FUN_1800070a0`, `FUN_180003940`, `FUN_1800039f0`, `FUN_180003d60`; EXEC]**

- Kind 0 (pulse): `v = 0.65v + 0.2275` while `(float)i < shape·0.01·4096` (at least one point if positive), then
  `v = 0.65v − 0.2275` up to point 4088, then `v = 0.65v`. Kind 4: `sine·(1 − m) + sine⁴·m`, `m = (shape + 2)/96`.
- Octave layers: +1 → `t[j] += src[2j mod 4096]`; +2 → `t[j] += src[4j mod 4096]`; both → `(oct1 + oct2) + src`.
- Then (render-mode byte 0, always) `t[i] = (float)((double)(t[i+1] + t[i])·0.5)` for i = 0…4094.
- **Table oscillator** (the only one FL reaches): 32-bit phase, table index `phase >> 20`, per output sample
  `a = (t[φ] + prev) ; φ += inc ; a = (a + t[φ])·0.333333 ; φ += inc ; prev = a`; the unison copy runs the same on its
  own phase with the +20-cent increment and is added. Pitch: `p = base + offset` (16-bit); when it changes, index =
  map[detune + 32 + p], `inc = incTable[index] + detune`, unison `incTable[index + 20] + detune`, with
  `detune = spread(voice) + k` and spread = 0, −8, 8, −6, 6, −5, 5, … (the voices' list order, step 8/6, truncated).
- Start phase at every voice set-up: `(int)(r·2³¹)` from the random table (r = 1 gives the integer-indefinite value).
- **Noise:** `s1 += s2·c; t = (r − s1) − s2; s2 += t·c; out = 16·s2` with r from the random table and
  `c = 0.3025` (set to `(0.9·P + 0.1)²` when the coarse knob of a noise oscillator is moved). Its state persists while the
  oscillator stays noise.
- Kind changes create a new oscillator object; otherwise it persists across notes (phases, smoothing memory).

### 3.3 Note-on (`FUN_1800061a0`) **[DECOMP + EXEC]**

`pitch = max(100, (int)Final.Pitch + 2400 + 2400) + 2400` (cents above 4.0887 Hz: key 60 → 7200); oscillator bases
`pitch + coarse + fine` (oscillators 2/3 get −4/+4 cents that the next Gen_Render removes again); resonance offset
`(int)(Final.FRes·255)`; cutoff base `pitch/12700·keytrack + Final.FCut`; filter states cleared if the amp level is 0;
envelopes restarted (counters, level 0); unison phases := main phases; LFO reset with the block-sample counter (or 0
with retrigger).

### 3.4 Envelopes (`FUN_1800045f0`, render loop) **[DECOMP + EXEC]**

Linear segments with counts `(int)((float)ms·rate/1000)`: attack 0 → 1, decay → sustain, hold, release → 0 (release
from the current level over the release count: `step = −level/count`). Entering a segment jumps to the previous
segment's target. The amp output is `level` during the attack and `level²` after; reaching a zero sustain ends the
voice.

### 3.5 LFO (`FUN_180004a30`, `FUN_180004b80`, `FUN_180004c10`) **[DECOMP + EXEC]**

Half-period `P = max(64, (uint)((1 − rate/999)³·samplerate·4))` samples; a fraction f falls from 1 by 1/P per sample;
each half period toggles a phase bit and draws a random value. Two outputs (pitch, cutoff): square 0/1, saw f,
triangle (1 − f, f) / ∓2·min(f, 1 − f), sample & hold. Delay: a gain ramp 0 → 1 over `delay·3528` samples.

### 3.6 Filter (`FUN_180004130`, `FUN_180004020`, `FUN_180005fb0`) **[DECOMP + EXEC]**

Two cascaded state-variable stages in double precision with `f = clamp(param + cutoff/255, 1e−4, 1)`,
`f1 = min(1, f·44100/rate)`, `f2 = min(1, f²·44100/rate)`, damping `1.1 − (1 − (1 − res/255)²)`; the output mixes
low, band and high with `cos A·cos B`, `sin B`, `sin A·cos B` (A, B = mix knobs ·π/2/255).

### 3.7 Per sample (`FUN_1800050c0`) **[DECOMP + EXEC]**

Amp envelope, filter envelope, LFO; every 32 samples the oscillator pitches (`offset = lfo·LFO→pitch + env·env→pitch`,
truncated) and the filter coefficients (cutoff `+ env·amount + lfo·amount`, amounts `/128`) are updated; oscillators ×
gains (sum, or osc1·2·osc2 with ring modulation), filter, then `a ← a + (env² − a)·0.03` (flushed below 5.96e−8) and
`out = s·a`.

---

## 4. Gen_Render (`FUN_18000ae50`) **[DECOMP + EXEC]**

### 4.1 Voices

For each slot with a sounding voice: when no ramp runs, new L/R from ComputeLRVol(Final.Pan, Final.Vol·gain) start a
128-sample linear ramp if they changed; the voice's pitch and cutoff base are set from Final.Pitch (integer) and
Final.FCut, a changed Final.FRes resets the resonance; the voice renders and is added with its L/R.

### 4.2 Chorus (switch S18)

A 65536-sample line; the left mix is written, the read position starts 20 ms behind and moves at a speed bouncing
between 0.9928 and 1.0072 with step `2·0.0144/((333 + c)·rate/1000)` (c = a process-wide counter: 0, then +5 mod 53 per
set-up — 5 after creation, 10 after the rate set), linear interpolation. **With the chorus on, the left output is the
delayed left mix only and the right output the dry right mix.** A 20 ms tail is played after the voices stop.

### 4.3 Written count, kills, counters

With no voice sounding and no tail the engine reports 0 samples written (the front-end copies nothing); during a tail it
reports the processed count minus one when the tail ends inside the block. Voices whose envelope ended are reported to
the host (Voice_Kill with KillHandle = 1). The sample counter (LFO phase origin) grows by the reported count, the block
counter by 1.

---

## 5. Verification

### 5.1 EXEC against LYNX's engine **[EXEC + model]** (`r64/py/bat.py`, logs `r64/out/bat2.log`, `bat3.log`)

All 76 distinct SimSynth states of the installation, **bit-identical (0 difference)** in every scenario:

| Scenario | Notes | Rate / block | Result |
|---|---|---|---|
| A | C5, 0.5 s | 44.1 kHz / 512 | 76 / 76 exact |
| B | 5 staggered notes, Final.Vol 0.5–1, pans 16 and −32, ±cents, FCut/FRes | 44.1 kHz / 512, 48 kHz / 512, 22.05 kHz / 64 | 76 / 76 exact each |
| C | 14 overlapping notes (voice stealing) | 44.1 kHz / 512 | 76 / 76 exact |
| D | short notes, retriggers, extreme pans, release inside the attack | 96 kHz / 97, 44.1 kHz / 1024 | 76 / 76 exact each |

Two synthetic states (all three wave kinds with octave layers and unison, LFO square with delay, filter envelope,
chorus; triangle × noise ring modulation, sample & hold LFO, version 7) are exact too and provide the test goldens
(`tests.cpp` `simsynth_engine`). The C-runtime `sin`/`cos`/`pow` of LYNX's build agree with the DLL's on every value
these renders reach.

### 5.2 What LYNX does not reproduce

- The process-wide random index and chorus counter are kept per instance (as a freshly created plugin): in an FL
  session with several SimSynth channels, or after several sample-rate changes, FL's oscillator start phases and chorus
  rate differ.
- FL's order of creation / state restore / rate set at project load, and its rounding when it fills the old voice
  record, are **not confirmed** (LYNX: create → state → rate; nearest).
- Parameter automation of SimSynth is not wired (state edits restart the plugin, as a state restore does in FL).

---

## 6. Port

`src/core/SimSynth.{h,cpp}` (engine, state parsing, instrument, `simsynth::renderReference` for the harness),
`tools/simsynthrender.cpp`, `src/core/Wasp.{h,cpp}` (silent placeholder); `InstrumentKind::simsynth` and
`InstrumentKind::wasp` (appended), FLP import (`SimSynth` → exact engine; `Wasp`/`Wasp XT` → placeholder, state kept),
project file ids, channel rack names, the channel settings panel (SimSynth: 14 knobs on the state's doubles), tests
`simsynth_engine` and `wasp_is_a_silent_placeholder`.
