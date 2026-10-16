# Behavioural analysis of FL Studio 2026: round 72 (REA) — DirectWave's engine (voices, modulation matrix, filters, zone and program effects)

> **Nature of this document.** Continuation of rounds 9 (DirectWave state) and 69 (sample players). *Clean-room*
> reverse engineering, behavioural only: data layouts, formulas and behaviours in our own words; addresses
> (`FUN_xxxxxxxx`) and offsets only trace the evidence. No Image-Line code, audio or wave data was copied; the engine
> reads the user's own samples at run time. No FLEX pack or encrypted content was touched.
>
> **No speculation.** **[DECOMP]** decompiled/disassembled code, **[DATA]** the installation's projects, **[EXEC]**
> the installation's DLL run unmodified, **[model]** our implementation (only DECOMP/EXEC facts), **not confirmed**
> where the evidence is missing.

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `~/.claude/jobs/f8789b49/tmp/r72/`.
- **Target:** `Plugins/Fruity/Generators/DirectWave/DirectWave_x64.dll`, SHA-256
  `3432775dd4e06b333fb04f210d834fb02d91932d702a3699b4c180501a746570` (C++/MSVC, static CRT).
- **Tools:** Ghidra 12.1.4 headless, project `~/ghproj/r72` (analysed; whole-DLL decompilation `gh/all.c`),
  `objdump -M intel` (`dll/dw.asm`, `dis.sh`) wherever the operation order matters; `py/pe.py` reads constants.
- **EXEC harness** `run/dw.c` → `run/dw.exe` (a copy of round 69's `fk.c`, our own code): loads the DLL unmodified
  through `CreatePlugInstance`, restores a 0xD5 state, answers host dispatcher id 14 (a path with `%FLStudioData%` /
  `%FLStudioFactoryData%` → the real path), sends the tempo (`ProcessEvent` 0 with the float bits), triggers / releases
  voices and renders; `dumpzones=` writes the program record plus every zone's record and samples as the plugin holds
  them; it logs the C run-time `rand()` state (`DW_RANDLOG=1`: after every block) through the DLL's own `_getptd`.
  `run/mt.exe` calls the DLL's internal math routines on input files.
- **Model / port:** `src/core/DirectWave.{h,cpp}` directly (no separate Python model); `tools/dwrender.cpp` renders a
  state with the samples dumped from the DLL; drivers `py/cmp.py` (one comparison), `py/batch.py` (every demo state,
  random notes, random host block sizes), `py/craft.py` / `py/synth.py` (crafted states), `py/crtcmp.py` (CRT math).

---

## 1. Engine structure **[DECOMP `FUN_18002b130`, `FUN_180015ba0`, `FUN_180016260`]**

- `Gen_Render` adds the engine's output to the host buffer. The engine has 16 MIDI slots; slot *k* plays program *k*
  (slot byte `+0x604`), and only slots whose program has zones (or the selected slot) run. Every demo uses slot 0.
- The host block is cut into **control blocks of at most 256 samples** (8 with a plugin setting, `engine+0x241`,
  off by default). Per control block: the program's two LFOs tick; eight buses are cleared (dry, delay, chorus,
  reverb × L/R); every active voice renders into its own buffer and adds to the buses **in voice-index order**; then
  the delay, the chorus, the reverb, the two waveshapers, and the dry bus is added to the output.
- `TriggerVoice`: note = `round(Init.Pitch/100)` clamped 0…127; the **velocity** for zone selection is
  `clamp(trunc(v·127), 1, 127)` of the host's velocity (voice event 3); the voice's own velocity source is the host's
  **Final.Vol**, and per control block the voice takes Final.Vol, Final.Pan and `(Final.Pitch + 6000) − 100·note`
  (in float, so the cent offset is quantised by Final.Pitch's float) from the host record. `ProcessEvent` 0 is the
  tempo (float bits; `engine+0xfa94`), 1 the polyphony.
- Slot controllers: bend, wheel, volume (default **100/127**), pan (0.5), expression (1), bend range.

## 2. Zone choice and voice allocation **[DECOMP `FUN_180024f90`, `FUN_180010070`, `FUN_180024e30`, `FUN_180033f00`]**

- Candidates: zones whose key range holds the note and whose velocity range, widened by the zone's alternation
  group's tolerance (`127·tol`), holds the velocity. Per alternation group (0…99; chunk `0x6E`: mode, probability,
  tolerance; group 0 is never written by the state: mode 0, probability 1): probability < 1 draws from `rand_s`
  (operating-system randomness, **not reproducible**); with several candidates mode 1 cycles from the last choice
  (−1 after a program load, kept across notes), mode 2 picks `rand()/(32767/n)`, mode 3 the same avoiding the last
  choice; mode 0 plays every candidate (layers).
- Exclusive group (header byte 5): releases the playing voices of that group. Voice mode (`0x64` byte 4): 0 poly,
  1 mono (every voice is marked ended and released), 2 legato (a playing voice takes the new note and id).
- A new voice takes the **first free record** (amplitude envelope state 0 and no pending start). The record is reused:
  what the start does not set — the LFOs' phase and held value (when the start phase is ≥ 1), the type-6/7 filter
  states — carries over from the previous note **[EXEC]**.
- Note-off releases the amplitude and both modulation envelopes (not in loop mode 1 "one-shot" unless forced) when the
  sustain pedal is up; in mono/legato modes releasing the last note restarts the lowest held note.

## 3. Voice, per control block **[DECOMP `FUN_180030090` + asm]**

1. First block: position 0, envelopes start (state 1, level 0), LFOs reset (fade 0, phase `2π·start` when start < 1),
   random = `rand()·2⁻¹⁵`, glide note = the previous note unless the program glide is 0 (or legato-only without
   legato).
2. Sources: the two modulation envelopes and the two zone LFOs tick (rates computed with the **block rate**
   `fs/n`); program LFO values, wheel, bend, envelope follower, random, position/frames, note/127, Final.Vol.
3. **Modulation matrix** (`0x204` ×16: source, destination, amount): effective amount `((a − ½)·2)³`. 32
   destinations (pitch, gain, pan, start/loop points, both filters' cutoff/res/shape, the amplitude ADSR, the zone
   effects' parameters, the four sends, stretch parameters) plus 32…47 = the amount of slot 0…15. Pass 1 (slot 15 →
   0) applies the slots that modulate an earlier slot's amount, pass 2 (slot 0 → 15) the rest; results clamp to 0…1,
   except velocity→gain, which multiplies (`gain × a·vel`, unclamped).
4. Pitch in cents `((glide − root)·keytrack + coarse·100) + fine + (pitch·2400 − 1200) + range·bend + host offset`;
   speed `pow(2, cents/1200)·rate/fs` in double, 32.32 fixed-point step.
5. Gains: `g = mod gain · zone gain · program gain · slot volume · expression`, `L = sqrt((1 − p_slot)(1 − p))·g`,
   `R = sqrt(p_slot·p)·g` (`p = Final.Pan/2 + zone pan`), smoothed per sample with `expf(−1/(fs·0.005))` + 10⁻¹⁴.
6. **Reading**: 32.32 position, linear interpolation (`(s1 − s0)·f + s0` mono, `s0(1−f) + s1·f` stereo; 16-bit data
   × 1/32767), 256 frames of silence on both sides; loop modes 0 none / 1 one-shot / 2 forward / 3 sustained / 4
   bounce, with the wrap-around interpolation at the loop end; at the end the envelope is released with time 0.25.
7. **Filters A/B** (types 0 off, 1–5 RBJ low/high/band/notch/all-pass with `Q = 0.70710677/(1.01 − res)`, each biquad
   applied **twice**, then a clip at ±(1 − shape); 6 a four-pole ladder in double precision (a step matrix squared 12
   times, soft saturation, shared DC blocker); 7 a vowel filter: eight constant-skirt band-passes per channel at
   formant tables morphed by the cutoff). Cutoff `440·2^(127v/12 − 5)` Hz, smoothed per block. Serial or parallel
   (header byte 8). Before filtering each sample gets `rand()·10⁻¹⁴` (one draw for both channels); after it a
   per-voice limiter (attack `expf(−1000/fs)`, release `expf(−10/fs)`, gain `powf(s, −1)` above 1). Quirks kept: with
   B = type 7 its parameters are set only when **A** is type 7; parallel B type 6 runs both channels through its left
   state.
8. **Zone effects** (`0x1FE…0x201`): ring modulator (sine at the cutoff law of its first parameter), sample-and-hold
   (step `1 − 0.99·p`), bit crusher (`3 + trunc(13·(1 − p))` bits), phaser (eight first-order all-passes, feedback ½,
   one `rand()` per channel and sample, then the second limiter); each with a smoothed mix.
9. Amplitude: linear ADSR with squared output (rates `1/(v⁴·fs·10 + fs·0.001)`), gains, then the sends (squared) to
   the delay/chorus/reverb buses and the dry bus (plain add when the dry send is 1).

## 4. Program effects **[DECOMP `FUN_180011f10`, `FUN_1800119c0`, `FUN_18002d0b0`, `FUN_18002c9a0`, asm]**

- **Delay** (`0x69`): 65 536-frame stereo line, allpass-interpolated read, time `max(fs·60/bpm·t, fs·0.001)` and
  feedback smoothed with `16/fs`, high- and low-pass (RBJ, Q 0.7071) in the feedback, optional cross feedback; starts at
  time 32768 and feedback ½.
- **Chorus** (`0x6B`): two 16 384-frame lines, delays `(c²·47.9 + 0.1)·fs/1000 + 12·d²·fs/1000·(1 + sin)`, quadrature
  sines, feedback; starts with delays 8192 and feedback ½.
- **Reverb** (`0x6A`): a plate (twelve lines with the classic plate lengths scaled by `((99·size + 1) + 15)·fs/115/20000`,
  four input all-passes, one-sample pre-delay, two-branch tank with damping one-poles, decay all-passes and fourteen
  output taps); decay `powf(expf(lengths·ln 0.001/…), ¼)`. It runs on `tanf`/`cosf`/`expf`/`powf`/`logf`.
- **Waveshapers** (`0x68`): `tanhf(s·x)·g` and `sinf(s·x)·g` with `g = 1 − 0.75·amount`, `s = 1/g³`.

## 5. Run-time library **[EXEC `run/mt.exe`, `py/crtcmp.py`]**

The plugin's static CRT takes its FMA code paths on this machine (`0x18111da18` = 1). Its `sinf`/`cosf` (Cody–Waite
reduction by π/2, double polynomials with fused steps) and `expf` (2^(j/64) table, fused cubic) are ported
(`dw::sinfCrt/cosfCrt/expfCrt`): **0 mismatches on 370 000 / 370 000 / 350 000 inputs** (the libm values differ in
~0.1 %). Its `powf` equals round 43's `bassdrum::powfMsvc` (0 / 350 000), its `tanhf` round 61's `psfl::tanhfCrt`
(0 / 250 000), its `tanf` on the reverb's arguments equals `(float)tan(double)` (0 / 8 000), `logf(0.001)` is a
constant. The C run-time `rand()` (LCG 214013/2531011, per thread, seed 1) feeds the dither noise; LYNX keeps one
generator per instrument.

## 6. Sample formats **[EXEC crafted states]**

The plugin keeps 8- and 16-bit PCM files as 16-bit integers (8-bit as `(u − 128)·256`), Ogg-in-WAV (all demo
samples), 24/32-bit and float files as floats (24-bit `v/2²³`, 32-bit `v/2³¹`); embedded audio keeps the state's
format. Ogg is decoded by `oggio_x64.dll`; libvorbis (as in LYNX's app) gives the same samples within ~4·10⁻⁷ but only
~15 % of them bit-equal **[EXEC + model]**, and a stand-alone libvorbis decode of one demo file skipped a 1024-frame
packet that oggio decodes (not investigated further).

## 7. Verification **[EXEC + model]**

- `py/batch.py all`: all **57 demo states**, 8 random notes each (velocity, Final.Vol/Pan, cent offsets, releases),
  random host blocks 100…1024, 3 s, with the plugin's own decoded samples: **all 57 bit-identical** at 44.1 kHz; at
  48 kHz (6 notes, another seed) all sounding output is bit-identical too, but 29 reverb programs differ in the reverb's
  idle self-noise (≤ 2·10⁻³⁰) before the first note: that noise comes from the pre-delay buffer's contents, which in the
  harness start at 10⁻³⁰ at 44.1 kHz (LYNX's choice) and at 0 at 48 kHz — they depend on what the plugin processed
  before, so FL's own idle noise is not reproducible in general **[EXEC, per-sample peeks]**.
- Crafted states (our own 16-bit samples in every zone; zone effects on, filter types 2–7, parallel routing):
  **bit-identical** (`py/craft.py`).
- Tests: `directwave_crt_math_and_state` (CRT goldens from the DLL), `directwave_matches_fl_with_demo_programs` (three
  demo programs — Gtr with shaper + chorus, Rhodes with shaper + reverb, the render-test program — with our own samples:
  FNV-1a of the DLL's 120 000-frame output), and `flp_real_files` counts the demo channels on the exact engine.

## 8. LYNX port **[model]**

- `src/core/DirectWave.{h,cpp}`, `InstrumentKind::directwave`: the importer (`dw::importChannel`) keeps the 0xD5 state
  in `pluginState`, one zone per zone of the played program (the first with zones) with the resolved file or the
  embedded audio, and each zone's sample format in `modelParams`; RackRenderer converts the decoded samples to the
  plugin's form (16-bit integers back from `round(x·32768)`). Levels as LYNX's other FL voice generators: host velocity
  = the note velocity, Final.Vol = `(11^(1.28·v) − 1)/10 ×` channel volume, Final.Pan = note pan + channel pan.
  The old multi-sample-sampler conversion stays as the fallback.
- Not reproduced: alternation probabilities below 1 (operating-system randomness), the HQ (sinc) interpolation and
  the 8-sample control blocks (plugin settings, off by default), the time-stretch modes (`0x1F9`), MIDI slots other
  than 0, the polyphony limit, Ogg sample bits (§6). LYNX renders in blocks of ≤ 256 of its
  own, so control-block boundaries follow LYNX's blocks, not FL's.

## 9. FLEngine items of round 69 §9 (sampler)

- **Voice levels [DECOMP FLEngine `0xd265e0`]**: per voice, Final.Pan = Init.Pan + channel pan, Final.Vol =
  Init.Vol × channel volume when the channel links velocity to volume (else the channel volume), Final pitch/cutoff/res
  = Init + channel, plus the channel's velocity/release cross-modulation table and a layer parent's levels; the pan law
  is a function pointer selected by FL's pan-law setting (`FUN_006a3a00`). The code that sets Init.Vol from the note
  velocity was not located (the scripting API's `0xd59d70` applies `(exp(x·k) − 1)·c` to `velocity/1.28`); LYNX's
  sampler keeps its own velocity/pan laws, since no FL rendering of a native sampler channel is available to check a
  change — **not confirmed**.
- **Channel filter [DECOMP `FUN_00d24650`, `FUN_00d247a0`]**: active when the cutoff is not 1 or the resonance not 0;
  type 0, types 1–5 and types 6–7 use three design routines (`FUN_00981df0/c50/f20`) and run-time function pointers,
  with an optional second pass (`+0x235`) and a gain correction; **not ported** (few demo channels use it).
- **Song-aligned tick phase, sinc kernel near a frame with a cutoff below π, double-precision kernels**: unchanged
  (round 69 §1.8, §3) — the tick flag is a per-block global of FLEngine's sequencer whose block alignment was not
  traced; the sinc case depends on Intel IPP's sine.

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| `FUN_18002b130`, `FUN_180015ba0`, `FUN_180016260`, `FUN_180016600`, `FUN_180016040` | engine render, Gen_Render, TriggerVoice, release, events | DECOMP |
| `FUN_180024f90`, `FUN_180010070`, `FUN_180024e30`, `FUN_180033f00`, `FUN_18000cf30` | zone choice, groups, voice modes, release, random pick | DECOMP |
| `FUN_180030090` (+ asm `gh/v.asm`) | voice: matrix, pitch, gains, readers, filters, zone FX, amp, sends | DECOMP + EXEC |
| `FUN_1800332d0/333a0/33550/33670` | linear readers | DECOMP |
| `FUN_180013f20`, `FUN_180011020`, `FUN_180025df0`, `FUN_180025fa0` | envelope, rate law, LFO | DECOMP |
| `FUN_180003a10`, `FUN_180014dd0`, `FUN_180010f30`, `FUN_180003980` | biquad design, double biquad, cutoff law, limiter | DECOMP + EXEC |
| `FUN_18003b860/4b0/680`, `FUN_180044f60`, `FUN_180044e50` | ladder and vowel filters | DECOMP + EXEC |
| `FUN_18002afd0` | phaser all-pass chain | DECOMP |
| `FUN_180011f10`, `FUN_1800119c0`, `FUN_18002d0b0`, `FUN_18002c9a0`, `FUN_180011970` | delay, chorus, reverb, chorus reset | DECOMP + EXEC |
| `FUN_1800893e0`, `FUN_180088010`, `FUN_180095140`, `FUN_180088510`, `FUN_1800953e8`, `FUN_18008e000` | CRT sinf, cosf, expf, powf, tanhf, rand | EXEC |
| `FUN_18003f130`, `FUN_18003f840`, `FUN_18003dcd0`, `FUN_180040750` | program, zone, top-level and flag readers/writers | DECOMP |
| FLEngine `0xd265e0`, `FUN_00d24ae0`, `FUN_00d24650`, `FUN_00d247a0`, `FUN_006a3a00`, `FUN_006a3810` | sampler voice levels, filter, pan law, ramps | DECOMP |
| 57 demo DirectWave states | programs, zones, effects | DATA + EXEC |
