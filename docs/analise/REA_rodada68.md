# Behavioural analysis of FL Studio 2026: round 68 (REA) — FL Slayer (the complete engine) and Fruity Dance (visual only)

> **Nature of this document.** Same goal as rounds 41 (MiniSynth), 57 (Transistor Bass) and 64 (SimSynth): describe a
> generator that LYNX did not play at all, precisely enough to port it as an exact instrument. *Clean-room* reverse
> engineering, behavioural only. No Image-Line or reFX code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace
> the evidence. No encrypted content was touched and no installation content (code, audio, tables) was copied into the
> repository: the recorded pick excitations, the chord and glide tables are read at run time from the user's own DLL.
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

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r68/`.
- **Targets** (copies in `bin/`):
  - `Plugins/Fruity/Generators/FL Slayer/FL Slayer_x64.dll` (64-bit Delphi front-end, SHA-256
    `2acdf8e90dfe740dd50759ddf156c838b0026e5cb0a33eb33dfa0b0731d79963`) and `Slayer_Engine.dll` (**32-bit** MSVC 7.1
    DLL, image base 0x10000000, one export `main` — a VST 2 entry point; "Slayer engine copyright (c) 2003 reFX"; SHA-256
    `84b64c43cc6528401e1c4e84074785e100e8453c4db4c8322a5d5f608de724a9`).
  - `Plugins/Fruity/Generators/Fruity Dance/Fruity dance_x64.dll` (64-bit Delphi, SHA-256
    `884dc3f7b46b7690053407e55360434372cbdf058f9ea5b999ec9d5f8ebe88d0`).
  - `System/Tools/Bridge/32bit/ilbridge.exe` (FL's 32-bit plugin bridge, Delphi) only for its x87 control word (§2.1).
- **Not a placeholder.** Unlike Wasp (round 64, `ps_dummy64bit=1` in `Plugin.nfo`, the shared `TTestPlug` DLL), both
  `Plugin.nfo` files only name the vendor and both DLLs are their own builds (different hashes and sizes). The shared
  placeholder's name list contains "FL SLAYER", but this installation does not use the placeholder for it **[DATA + DECOMP]**.
- **Tools:** Ghidra 12.1.4 headless (JDK 21), projects `~/ghproj/r68_eng`, `r68_fe`, `r68_dance`; our scripts
  `gh/scripts/DecompAll.java`, `DecAt.java`, `DecCreate.java` (creates functions that are only reached through the
  VST object's vtable before decompiling them); `objdump -M intel` for every routine whose x87 order matters
  (`out/eng.s`, per function `out/f<address>.s`); `py/pe32.py` (constants), `py/res.py` + `py/dfm.py` (the front-end's
  form resource).
- **EXEC harnesses** (our own C, Zig 0.14.1, same safety measures as the earlier rounds: `SetErrorMode`, vectored fault
  handler ending the process, the installation's DLLs loaded unmodified, `TerminateProcess`, `timeout`):
  - `run/sv.c` → `sv.exe` (**32-bit**): loads `Slayer_Engine.dll` through `main` with a minimal VST host callback,
    answers the vendor/product strings and the time info, sets the x87 control word before every call (default
    0x033F, §2.1), drives the engine as the front-end does (§6): parameters, effOpen, sample rate, block size,
    resume, MIDI events with deltaFrames 0 at the start of the next block, processReplacing.
  - `run/sx.c` → `sx.exe` (64-bit, a copy of round 37's `og.c`): the front-end with a fake host, used to read the
    controls' maxima (§6.1). The front-end's bridge connection does not come up under the fake host (the bridged
    object never reports ready), so the engine itself is driven by `sv.exe`.
- **Drivers** (`python3 -I`, under `py/`): `collect68.py` (states), `svrun.py` (EXEC), `cmp.py`, `pk.py` (field
  peeks, both sides), `bat.py` (scenarios), `fuzz.py` (random parameter sets), `gold.py` (test goldens). Logs in `out/`.
- **Data:** `out/states68.pkl` — every 0xD5 state of the installation: **FL Slayer 64** (the 64 plugin presets, all
  version 0, 116 bytes), **Fruity Dance 4** (plugin-database entries, all identical, 40 bytes). **No demo or template
  project uses either plugin.**

---

## 1. FL Slayer in FL 2026: a bridged 32-bit VST engine **[DECOMP + EXEC]**

- The front-end (`CreatePlugInstance` `0x852080`, constructor `FUN_008521e0`) is the Delphi plugin framework with an
  editor form (`TSynthEditorForm`). It sets the bridge folder `System\Tools\Bridge\`, names its product "FL Slayer GUI"
  and loads `Slayer_Engine.dll` through FL's bridge (`FUN_008529c0` → bridged-plugin object `FUN_0075bf10`, IPC classes
  `IPC.Bridge.*`, `ILBridgeIntf.*`, strings `32bit\ilbridge.exe` / `64bit\ilbridge.exe`). The engine is a 32-bit DLL, so
  it runs inside `ilbridge.exe` (32-bit).
- Every FL call becomes a VST call on the bridged object: ProcessParam with the update flag → `setParameter(i, v/max)`
  (§6.1); TriggerVoice / Voice_Release → MIDI note on / off (§6.2); Gen_Render → one `processReplacing` of the whole
  block, whose two outputs are added to FL's buffer with the host gains (§6.3). The bridged MIDI events carry time 0
  (`FUN_0075da20`): they reach the engine with the next process call.
- **Engine identity:** `main` creates a 0x8298D8-byte object (`FUN_10007f30`): an AudioEffectX with 64 programs and
  28 parameters, no inputs, two outputs, synth flag, unique id 'FLSL', program 0 "POWER Metal". The bank of 64 factory
  programs is in the DLL; FL's 64 presets are the same sounds stored as front-end states (§6.1).
- **Vendor lock** **[DECOMP `FUN_100081b0` + EXEC]**: the engine renders only when the host reports the vendor
  "Image-Line" and the product "FL Slayer GUI"; otherwise `processReplacing` returns without writing (field `+0x8298d0`
  stays −1). `sv.exe` answers both strings.

## 2. Engine structure **[DECOMP + EXEC]**

### 2.1 Arithmetic and rounding

- All signal code is x87. `processReplacing` (`FUN_100087f0`) reads the control word and switches the rounding mode to
  **truncation** (`_controlfp(0x300, 0x300)`) for its whole duration, restoring it at the end. Every arithmetic result,
  every store to float/double and every `fistp` inside it therefore rounds toward zero; the set-up code (constructor,
  sample-rate set, parameter set, MIDI parsing) runs in the caller's mode.
- The precision field is not touched by the engine. FL's bridge resets the FPU to 0x033F (64-bit precision, round to
  nearest) around plugin calls (`ilbridge.exe` routine at `0x528ba0`, constant at `0x71b368`; also the Delphi default)
  **[DECOMP]**; the engine thus runs with **64-bit precision**. EXEC: the same render with 53-bit precision differs by
  −81 … −102 dB on the presets tried, with 64-bit precision LYNX is bit-identical (§7) **[EXEC + model]**. That the
  bridge's audio thread runs with this control word is an **inference** from the reset routine.
- The C runtime's `rand` (per-thread seed, initial value 1) feeds the noise exciter; `pow` and `sinh` (EQ, cabinet
  coefficients) are the C runtime's in double.

### 2.2 Set-up

Constructor (`FUN_10007f30`, `FUN_100081b0`, reset `FUN_10002ad0`), in this order: program 0's 28 values sent to
`setParameter`; the cabinet object created (§5.3) with its parameter 3 set to 0.416667; sample rate 44100; the reset:
- the shared 65536-point sine table `sin(k · 2⁻¹⁶ · 6.28318024)` (6.28318024 = the float constant the engine uses for
  2π), computed with `fsin` in extended precision and stored as floats;
- per-key pickup factors: the six values 1, 1.166, 1.333, 1.5, 1.666, 1.8333 repeated for 128 keys;
- a detune table of 255 floats `1.00057779^k`, k = −127 … 127 (computed by repeated multiplication in extended
  precision, the negative side as 1/x);
- the effect rate multipliers 0.25, 0.5, 1, 2; every filter, ring and buffer cleared; the feedback ring's write index
  `1 − trunc(44100 · (−0.005))` = 221 (computed with the rate of that moment, i.e. always 44100);
- then every string is cleared (including the release timers), the event queue and the held-key list emptied, CC 7 and
  CC 11 volumes 1, the pitch ratio 2 and semitone ratio 1, the chunk counter 0.

Sample rate (`FUN_10008340`): the cabinet's rate (coefficients recomputed only when it changes), the rate, 44100/rate,
a 128-entry note-frequency table (not used by the audio). Block size only sizes the internal two-channel buffer.
Resume (`effMainsChanged(1)`, `FUN_100080f0`): gated strings released, held keys and strum count cleared, the first
string's two delay lines cleared (the loop clears string 0 six times), idle counter 5.

### 2.3 processReplacing

- Tempo from the host's time info (`audioMasterGetTime`, 120 when the host returns none), stored as float before the
  rounding switch.
- The internal buffers are cleared; the block is cut at the **control-rate grid**: a counter starts at 0 and gains
  `rate · 0.005` (220.5 samples at 44.1 kHz) at every boundary. At each boundary the strings and the effects render the
  samples since the previous one, then every queued event with deltaFrames below `trunc(rate · 0.005) + position` is
  applied (in practice all of them: the front-end sends 0), then the exciter envelopes advance (§3.3). So a note starts
  at the first grid boundary at or after the start of the block that carries it.
- The block's output is the internal buffers added to the host's (FL hands zeroed buffers).
- Events (`FUN_10008fa0`): note on (velocity 0 → note off) with velocity v/127 and the detune byte; CC with value/127;
  CC 120 and 123–127 release every string at once, CC 121 resets volume, expression, pitch ratio 2 and semitone ratio 1;
  pitch bend 14-bit `b · 2⁻¹⁴` (`b ≤ 8192`) or `b/16383`. Applied at the boundary: CC 1 / pitch bend (§3.5), CC 7
  volume, CC 11 expression, CC 64 sustain (bit 0 of the hold flags; parameter 26 "Latch" is bit 1).

## 3. The strings **[DECOMP `FUN_10004140`, `FUN_100033d0`, `FUN_10003920`, `FUN_100032f0`, `FUN_100051f0`, `FUN_10003fe0` + EXEC]**

### 3.1 Notes and modes

Mode = `trunc(p24 · 5.99)` (0 single, 1 auto-chords, 2 power chords, 3 strumming, 4 solo fixed, 5 solo dynamic).
Strum step = `trunc(p25 · 10.99)` chunks.
- **Single:** one string. **Chords:** every gated string released first, then the chord of `key mod 12` from the DLL's
  12 × 6 table (offsets from note 48, −1 ends), each string delayed by one more strum step. **Power chords:** strings
  released, then three notes: below key 60 `key+12`, `key+17` (detune +5), `key+24` (detune +2), else `key−12`, `key−5`
  (detune −4), `key` (detune −2), delayed 0, 1, 2 strum steps. **Strumming:** each held note starts
  `count · step − chunks since the first` chunks late. **Solo modes:** a held-key list; the first key triggers, later
  keys only glide: with the fixed mode the step is `|target − current| / (tableA[t] · 200 / tempo)`, with the dynamic
  mode `tableB[t] · 200 / tempo / |target − current|` (t = `trunc(p25 · 6.99)`, the tables in the DLL); both start the
  slide noise at `0.3 · velocity`.
- Note-offs mirror the modes (chord notes, the power-chord notes — below 60 the engine releases +12, +17, +24, +29, +36,
  above it −12, −5, 0, +7, +12 —, the strum count decremented, the solo list popped: when empty every string is
  released, else the glide restarts towards the newest held key).
- A release keeps a string sounding while the sustain or latch flag is set (the key's "held" mark is cleared, and when
  both flags drop every unheld string is released at the next render).
- **String choice:** the string already on that note; else the first whose release timer passed `0.3 · rate`; else the
  first not gated; else the oldest. **Trigger:** gate 1, envelope state "attack", level, pick position, slide state and
  filters 0, resampler phase 0, release timer −1, detune, velocity, start delay.
- Each chunk, before rendering: a change of mode releases everything (the first render after creation does this too:
  the mode memory starts at −1); strings released longer than `0.2 · rate` samples, or everything once the idle counter
  reached 4, are cut — the engine passes the string's **index** as the key to release.

### 3.2 One string, per internal sample

The strings run at an internal 44.1 kHz and are resampled to the host rate (§3.6). Per string and chunk the engine
computes (velocity v, note n, the parameters p0…p27 as 0…1):
- loop damping `d = clamp((1−p7)(1−p9) + (1 + 3(v−1)/(2(1−v)+1))·p9) · (1 + (n−64)/32), 0.03…0.999)`;
- loop gain `0.99 + 0.01(1−p7)`, excitation tone factor (p3), velocity mix `p8·v + 1 − p8`, clip limit
  `(1 − 0.95·p4)²`, slap gain `1/(1 − 0.98·p4)`, pickup resonance `min(0.99, 2·sin((6000·p6 + 1000)·7.12e−5))`;
- delay lengths from the (gliding) pitch: `L = 2 · 44100/(8.1757·e^{0.05776·pitch}) / semitoneRatio / (1 + 0.1224·vib)
  · detune`, the two lines at `L·0.9992` and `L·1.0008`;
- the exciter: a level ramp (step `1/(rate·0.005)`) towards the envelope value (or 1 when the strings type is not
  "noise"), times either white noise from `rand()` (type 0) or a recorded pick read at a key- and type-dependent speed
  with linear interpolation (types 1–6, 15 tables in the DLL);
- the slide noise (after legato notes and semitone changes): a ramp `(φ − 0.5)·level` with φ += 0.01 mod 1, level
  ×0.9997 per sample, mixed with the exciter through a one-pole;
- two fractional delay lines (linear interpolation) with the loop gain, the feedback ring of the amp (§5.2) and the
  exciter added, both clipped and low-passed with `d`; their sum `0.85·(0.4·b + a) + fret noise` (×level ramp after the
  release) is clipped and written to the pickup line;
- one or two pickups (coils) at `(p2 + 0.05) · factor[n] · 100` samples (the second at twice that, opposite sign; the
  "dynamic" coil setting scales by the delay length), through a resonant state-variable filter; output × slap gain.

### 3.3 Exciter envelope (once per chunk)

Four states: attack (phase += 19.995 per chunk, i.e. one chunk), decay (rate `20/(800·x³)`, x from p3), rest (0) and
release (rate `20/(800·a³)` from the decay rate a, scaling the level reached at release). The string's start delay
(strum) counts chunks down before the envelope moves.

### 3.4 Pick tables **[DATA]**

15 tables of 16-bit samples (`count`, then the samples) at `0x100179b8`…`0x10022c1c` in the engine's data, referenced
by table numbers 1, 7, 14–22, 24, 26–28 (the strings type and the key select one; type 4 also by velocity). LYNX reads
them, the 12 × 6 chord table (`0x10015318`) and the two 7-entry glide tables (`0x100152e0`, `0x100152fc`) from the
user's DLL after checking its SHA-256.

### 3.5 Pitch bend and mod wheel

With glissando off (`trunc(p27 · 1.99) = 0`): the bend sets the pitch ratio `e^{128·b·0.01083}` and a semitone ratio
from a 25-step table (bend in semitones, 2^(k/12) rounded to 4 decimals, 0.5…2), the mod wheel a vibrato depth (the
`0.1224` term). With glissando on, the roles swap: the mod wheel steps semitones, the bend becomes continuous.

### 3.6 Resampler

Every output sample advances a phase by `44100/rate`; while the phase is ≥ 1 the string computes internal samples
(keeping the last four), then a cubic (Catmull-Rom-like) interpolation of the last four at the phase is added to the
output.

## 4. Effects block (`FUN_10005ef0`, before and after the amp) **[DECOMP + EXEC]**

Effect = `trunc(p20 · 15.99)`; FX par 1/2 = p21/p22. Before the amp (mono; 2–8 run only while a string sounds):
1 phaser (a frequency shifter: two 3-stage all-pass chains in quadrature mixed with the sine/cosine of an LFO, with
feedback), 2 tremolo (tempo-synced triangle), 3 wah-wah driven by an envelope follower, 4 wah-wah driven by an LFO
(both through the amp's resonant 4-pole filter), 5 ring modulator at the played pitch, 6 fuzz box (exponential
saturation then quantisation), 7 harmonizer (octave down from zero crossings), 8 talk box (three formant band-passes
swept by a triangle). After the amp and cabinet (stereo): 9 chorus, 10 flanger, 11 stereo phaser/shifter, 12 leslie
(two modulated taps through rotating band-passes; only while a string sounds), 13 tremolo, 14 dub delay (dry right,
delayed left), 15 multitap echo (tempo-synced lengths). Every LFO is a phase in 0…1 kept as float, folded into a
triangle with the engine's step order; tables and rounding as decoded (LYNX follows the disassembly line by line).

## 5. Amp, EQ, cabinet **[DECOMP + EXEC]**

### 5.1 EQ

Three RBJ biquads recomputed every chunk: low shelf 500 Hz, peaking 1 kHz (bandwidth 1.5), high shelf 2.5 kHz, gains
`20·p − 10` dB from p16–p18; the shelves use `√(2A)·sin ω` (slope 1).

### 5.2 Amp

Amp type `trunc(p12 · 3.99)`: 0 a DC-blocked signal through the EQ (gain `3·(0.25 + p13/2)`); 1 and 2 three one-pole +
sine-table shaper stages (stage coefficient from p14 "Presence", shaper gain from p13 "Drive") with the EQ after (1) or
before (2); 3 three state-variable band-pass stages with the shaper, then the EQ.
Then the amp's 4-pole filter with cubic clipping (cutoff and resonance from the cabinet type), a short comb (cabinet
type: 20/64/32/48 taps × rate/44100), the output gain of the cabinet type, and the feedback resonator (a sine
oscillator at `invRate · 12566` excited by the amp output, amplitude from the velocity, written ×8 into the ring the
strings read, p15 = feedback amount). Output × volume `CC7 · CC11 · 1.3`.

### 5.3 Cabinet

A separate object (`FUN_10009290`): a one-pole high-pass at `176.4/rate` and two RBJ shelving sections with slope S:
low shelf 130 Hz +10 dB (S 0.5), high shelf 10 kHz +21 dB (S 1) — coefficients in extended precision through doubles;
only the left channel is processed (the engine copies left to right afterwards).

## 6. The front-end **[DECOMP + EXEC]**

### 6.1 Parameters and state

- 29 controls (tags 0…28 of `TSynthEditorForm`'s wheels, selectors, combos and buttons); their maxima read from the
  live controls (`sx.exe`, field `+0x3ac`): strings 6, coils 2, amp 3, cabinet 3, effect 15, mode 5, latch 1,
  glissando 1, mod wheel 127, every knob 65535 **[EXEC]**. Parameter i < 28 is sent as `setParameter(i, value/max)`
  (single-precision division); parameter 28 as MIDI CC 1.
- State (`FUN_00853660`): int version, then 28 ints (version 0, every installation preset) or 29 (version 1, written by
  FL 2026: the mod wheel added); after loading, every parameter is sent again **[DECOMP + DATA]**.
- After the engine is loaded the front-end sends a centred pitch bend (`0x4000e0`). Flush (Dispatcher 2) sends CC 123.

### 6.2 Notes

TriggerVoice (`FUN_00853c50`): key = `Round((Final.Pitch + 6000)·0.01)` (float math, ≤ 0 → 0, clamped 0…127);
velocity = `Round(127 · ln(10·Init.Vol + 1) / ln(21.551182))`, at most 127 and **not** clamped below (Init.Vol 0 sends
a note-on with velocity 0, i.e. a note-off). Voice_Release sends a note off with the same key and velocity.
ProcessEvent: MIDI pitch (cents) → pitch bend `MulDiv(cents, 8192, 1200) + 8192` clamped; MIDI pan / volume → the host
gains below.

### 6.3 Output

The front-end adds the engine's left and right outputs to FL's buffer × `L, R = ComputeLRVol(pan 0, volume 1)` (host
slot 3, integer pan): `cos(π/4)` on both sides, 0.70710677 **[DECOMP]**. The plugin reads no per-voice levels
(UseVoiceLevels 0), so FL applies the channel's volume and pan to its output (**inference** from the SDK flag).

## 7. Verification **[EXEC + model]** (`py/cmp.py`, `py/bat.py`, `py/fuzz.py`; logs `out/cmp1.log`, `out/bat1.log`, `out/fuzz1.log`, `out/fuzz2.log`)

`sv.exe` (the installation's DLL) against `build-local/slayerrender` (LYNX's engine driven the same way):

| Set | Content | Result |
|---|---|---|
| presets | 64 presets, C5 vel 100 for 0.7 s, 44.1 kHz / 512 | 64 / 64 bit-identical |
| scenario A | 5 overlapping notes of different velocities incl. a held one | 64 / 64 bit-identical |
| scenario B | same, host blocks of 1000 | 64 / 64 bit-identical |
| scenario C | 48 kHz / 480, a chord inside a note | 64 / 64 bit-identical |
| scenario D | tempo 140, legato notes | 64 / 64 bit-identical |
| scenario E | pitch bends, mod wheel, sustain on/off, maximum bend | 64 / 64 bit-identical |
| scenario F | host blocks of 64, extreme keys, held notes | 64 / 64 bit-identical |
| random | 340 random parameter sets (every effect, amp, cabinet, strings type, mode, latch, glissando), 1–6 random notes, random bend / mod wheel, 22.05/44.1/48/96 kHz, blocks 64…1024, tempo 90/120/174.5 | 340 / 340 bit-identical |

`tests.cpp` `slayer_engine` holds 12 preset goldens from the DLL (energies and samples, compared exactly).

## 8. Fruity Dance **[DECOMP + DATA]**

A visual plugin ("A dancing FL-chan"): `Gen_Render` (`0x71b7c0`) is empty, TriggerVoice (`FUN_0071b630`) maps the key
through a 128-entry table to a dance move or option, asks the host to kill the voice at once and returns −1; the state
(int version; 0x1c bytes of settings and 8 bytes of control values — the installation's entries are version 1, 40
bytes —; version 2 adds a picture path) only drives the animation. It makes no sound in
FL; LYNX imports it as a silent channel (`InstrumentKind::fruitydance`) that keeps its state.

## 9. What LYNX does not reproduce

- The phase of the 5 ms control grid relative to the song: FL's engine has been running since the plugin was loaded,
  LYNX's starts with the render (one primer sample, needed because the engine's first render releases the notes,
  §3.1). Notes therefore start up to 5 ms differently from an FL render.
- The C runtime's `rand` seed: one per engine (start value 1); in FL it is per bridge thread (with several FL Slayer
  channels sharing a bridge process, the noise sequences differ) — **not confirmed**.
- The bridge's call order at load (open, parameters, rate, block size, resume) and its audio thread's control word are
  **inferred** (§2.1); the time info's tempo comes from LYNX's song tempo.
- MIDI pan / volume / pitch events (ProcessEvent 2–4) and parameter automation are not wired; state edits restart the
  engine.
- Program changes (the 64 built-in programs) are ignored: the front-end never sends them.

## 10. Port

`src/core/Slayer.{h,cpp}` (DLL loading with SHA-256 check, engine, front-end behaviour, instrument,
`slayer::renderReference` for the harness), `tools/slayerrender.cpp`; `InstrumentKind::slayer` and
`InstrumentKind::fruitydance` (appended); FLP import (`FL Slayer` → exact engine when the state and the user's
`Slayer_Engine.dll` are usable, else unsupported as before; `Fruity Dance` → silent, state kept); project file ids;
channel rack names; the channel settings panel (FL Slayer: 25 knobs on the state's integers); tests `slayer_state`,
`slayer_engine`, `slayer_and_fruity_dance_import`.

## Evidence index

| Item | Where | Kind |
|---|---|---|
| bridged 32-bit engine, product name, engine file | `FUN_008521e0`, `FUN_008529c0`, `FUN_0075bf10` | DECOMP |
| VST object, 64 programs, 28 parameters, 'FLSL' | `main`, `FUN_10007f30` | DECOMP + EXEC |
| vendor lock | `FUN_100081b0` (`+0x8298d0`) | DECOMP + EXEC |
| truncating rounding in process | `FUN_100087f0` (`_controlfp`) | DECOMP + EXEC |
| bridge FPU reset 0x033F | `ilbridge.exe` `0x528ba0`, `0x71b368` | DECOMP |
| control-rate grid, event queue | `FUN_100087f0`, `FUN_10009ac0`…`FUN_10009b40`, `FUN_10008fa0` | DECOMP + EXEC |
| modes, chords, glide | `FUN_100033d0`, `FUN_10003920`, `FUN_10003820`, `FUN_10003f40`, tables `0x10015318`, `0x100152e0`, `0x100152fc` | DECOMP + DATA + EXEC |
| strings, exciter, pickups, resampler | `FUN_10004140`, `FUN_10003fe0`, `FUN_100032f0`, `FUN_100051f0` | DECOMP + EXEC |
| effects 1–15 | `FUN_10005ef0`, `FUN_10005d20`, `FUN_10005400`, `FUN_10005550` | DECOMP + EXEC |
| EQ, amps, amp filter | `FUN_10002f60`, `FUN_10002ee0`, `FUN_100056a0`, `FUN_100058a0`, `FUN_10005aa0`, `FUN_100052d0` | DECOMP + EXEC |
| cabinet | `FUN_10009290`, `FUN_10009350`, `FUN_10009420`, `FUN_10009500`, `FUN_100096d0` | DECOMP + EXEC |
| controls' maxima | `TSynthEditorForm`, `sx.exe` (`+0x3ac`) | DATA + EXEC |
| state, parameter sends | `FUN_00853660`, `FUN_008534e0` | DECOMP + DATA |
| key, velocity, release | `FUN_00853c50`, `FUN_00853df0` | DECOMP |
| output gains | `FUN_00853170`, `FUN_0072dbb0` | DECOMP |
| Fruity Dance silent | `FUN_0071b7c0`, `FUN_0071b630`, `FUN_00719ef0` | DECOMP |
