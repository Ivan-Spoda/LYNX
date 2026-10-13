# Behavioural analysis of FL Studio 2026: round 13 (REA) — FLEX (`.flexpack` packs), Kepler and minor generators

> **Nature of this document.** Continuation of `REA_rodada12.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode has been copied: what is here are descriptions,
> data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. **FLEX packs are protected (encrypted content); LYNX does not
> circumvent this protection** and this document does not describe how to do so (§1.3).

- **Targets** (local copies in `tmp/r13/bin/`, originals in `Plugins/Fruity/Generators/<name>/`):
  - `FLEX/engine_x64.dll` → `flexeng_x64.dll` (SHA-256 `bd6e8a4a…cdcc`, C++/MSVC, framework `d1framework`,
    pack, preset and zone reader). `FLEX/FLEX_x64.dll` (`76acd4ab…3650`) is only the shell/UI; the v2 engine
    (`engineV2_x64.dll`, `.flex2preset` presets) was not decompiled.
  - `Kepler/engine_x64.dll` → `kepeng_x64.dll` (`baf37a1d…63ab`, C++/MSVC).
  - BassDrum, Toxic Biohazard, PoiZone, Wave Traveller: format + strings only. BooBass and Drumpad are already in
    round 10 (§5, §6); here only additions.
- **Tools:** Ghidra 12.1.4 *headless* in dedicated projects (`/tmp/r13gh_<dll>`), scripts in
  `tmp/r13/gh/scripts/` (`Decomp`, `StrRefs`, `Scalars` — search for FourCCs/immediate constants —, `Dis`).
  `0xD5` collection: `tmp/r11/blobs.pkl` (`.flp` + `.fst`, `0xD5` after `0xC9`).
- **Our scripts (Python):** `tmp/r13/packls.py` (pack directory). (The analysis scripts and copies of decrypted
  content have been deleted: see §1.3.)
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst`/packs |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

### Counts in the data (channels in demo `.flp` files)

| Plugin (`0xC9`) | Channels in `.flp` | Total (`.flp`+`.fst`) | `0xD5` size | Container |
|---|---|---|---|---|
| **FLEX** | **57** | 195 | 2707 / 2694 / 2626 B | fixed binary: pack + preset + 128 floats |
| Drumpad (+ "DrumPad") | 10 (+1) | 976 | 292/300 B | `SSLF` + `3PMD` (round 10 §6) |
| BassDrum | 5 | 10 (+52 "Bassdrum" in `.fst`) | 1384 B | `SSLF` + `1kiK` |
| Kepler | 4 | 285 | 178…189 B | mode name `Kepler-1` + floats |
| BooBass | 3 | 7 | 16 B | u32 + 3 × i32 (round 10 §5) |
| Toxic Biohazard | 3 | 7 | 10 050 B | `SSLF` + `TBi2` |
| Wave Traveller | 3 | 13 | 332 B…38 KB | u32 1000 + `.wav` path + curve |
| PoiZone | 2 | 391 | 700 B | `SSLF` + `Ver4` |
| Kepler Exo | 0 | 192 | 793 B | `Kepler-2` |

---

## 1. FLEX — overview **[DECOMP+DATA]**

**Short answer: the content of the packs (samples and presets) is protected by FLEX and LYNX does not
extract it.** The samples are content licensed for use inside FLEX; decrypting them to play in another
program would be circumventing the protection, which the project does not do. What is open and usable: the **plugin
state** in the `.flp` (pack name, preset name and the 45 global parameters, §1.1) and the pack's **directory**
(entry names, §1.2). From that comes the *fallback*: choosing a LYNX synth patch by the category
deduced from the names.

Of the 57 FLEX channels in the demos, **56 point to installed packs** (the 57th uses "Synthesizer Science", which
does not ship with the installation) **[DATA]**:

| Pack | Channels | Nature of the presets used |
|---|---|---|
| Melodic Techno | 18 | almost all **synthesis** (internal oscillators); 1 preset uses 1 sample |
| General Midi Library | 11 | **multi-sample** (Piano Dark: 19 zones; Flute: 9; Harp: 9…) |
| Arksun Cityscape | 11 | **synthesis** (no samples in the pack) |
| Olbaid Compendium | 9 | **synthesis** |
| Mobile Synth Pluck | 4 | multi-sample (11 zones each) |
| Mobile Tuned 808 Bass | 2 | 1 sample with loop |
| Core 808s (`.flex2pack`) | 1 | v2 engine: 1 sample per preset (`.usedsamples`) |
| Synthesizer Science | 1 | pack missing → *fallback* |

In other words: ~18 channels play as a multi-zone sampler with practically full fidelity; ~37 are FLEX
synthesis presets (up to 4 oscillators per zone), which need mapping to the LYNX synthesizer (§1.8).

### 1.1 Plugin state (`0xD5`, 2707/2694/2626 B) **[DATA; partial DECOMP]**

| Off | Size | Content |
|---|---|---|
| 0x000 | 4 × u32 | `1`, `1`, `0x812`, `0` (fixed header; `0x812` = bytes following the pack name) |
| 0x010 | 1024 | **pack name** (ASCII, zero-terminated; e.g.: `General Midi Library`) |
| 0x410 | 1 | 0 |
| 0x411 | 1024 | **preset name** (ASCII; e.g.: `Piano Dark`) — corresponds to the pack's `<name>.flexpreset` (or `.flex2preset`) entry |
| 0x811 | 0x2D | block of state integers/bytes (versions, UI page indices) — ignore |
| 0x83E | u32 | **128** (number of floats that follow) |
| 0x842 | 128 × f32 | **FLEX global parameters** (the first 45 are those of table §1.6; the rest are 0) |
| 0xA42 | 0…0x51 | tail (small integers; varies with version: 2707 = 0x51 B, 2694 = 0x44 B, 2626 = 0) |

The 45 state floats **override** the 45 globals stored in the preset: in "Piano Dark" from the demo
`NewStuff.flp`, `Env1 Attack Mod` = 0.742 in the state versus 0.5 in the preset, and macros 1–3 have been moved
(0.685/0.469/0.566) **[DATA]**. LYNX must use the state values, not the preset ones.

### 1.2 Pack container (`.flexpack` / `.flex2pack`) **[DECOMP `FUN_18038b980`, `FUN_18038fff0` + DATA]**

**Big-endian** integers in the directory; the entry contents are little-endian once decrypted.

| Field | Type | Content |
|---|---|---|
| version | u32 BE | `0x00000002` (`.flexpack`) or `0x01000002` (`.flex2pack`) |
| n | u32 BE | number of entries |
| entry × n | — | u32 BE `L` (name length **including** the zero) + `L` name bytes + **u64 BE absolute offset** in the file + **u64 BE size** |
| data | — | the entries, contiguous, right after the directory, in the same order |

Typical entries: index 0 `presetsIndex.presetsIndex`, index 1 `pcf.pcf`, then `<preset>.flexpreset`,
`<sample>.flac`, and in `.flex2pack` pairs `<preset>.flex2preset` + `<preset>.usedsamples`. There are junk entries
(`desktop.ini`). In "Mobile Tuned 808 Bass" there are **two** `presetsIndex`/`pcf` (concatenated pack). The `.ini`
next to the pack only has `[Ini] Version=N`.

### 1.3 Content protection

The pack entries are encrypted by the FLEX engine and validated on opening. **LYNX does not decrypt the
packs** (content licensed for use in FLEX); therefore the details of the scheme are not documented here,
and sections 1.4–1.8 (internal preset format, read during the analysis) remain only as a behavioural
reference, with no use in the import.

### 1.4 `presetsIndex` and `usedsamples` **[DATA]**

Decrypted `presetsIndex`: u32 version (2), u32 n, then n × (string name, string category, string tags),
each string = u32 LE length (including the zero) + bytes. Categories = `typetags.txt` (Arpeggio, Bass, Bell,
Brass, Guitar, Keys, Lead, Drone, Drum, Organ, Pad, Piano, Pluck, Sequence, String, SFX, Wind); style
tags = `styletags.txt` (Basic, Complex, Bright, Dark, Hard, Soft, Short, Long, …). Useful for the *fallback*.

`.usedsamples` (`.flex2pack` only): text `"<sample>.flac : <n>"` per line — the preset's list of samples.

### 1.5 `.flexpreset` preset (v1 engine) **[DECOMP `FUN_1803f1300`, `FUN_1803c9aa0`, `FUN_180427bf0`, `FUN_18041f770` + DATA]**

All little-endian. "str" = u32 length (including the zero) + bytes.

| Order | Content |
|---|---|
| 1 | FourCC `flex` + FourCC `00su` (in the file they appear as `xelf00su`) |
| 2 | u32 version (< 36; seen 0x11, 0x13, 0x23) |
| 3 | u32 N (44 or 45) + N × f32: **global parameters** (§1.6) |
| 4 | u32 G (≤ 16) + G **groups**, each: FourCC `mgrp`, u32 version (< 5), u32 P (= 300) + P × f32 (group parameters), **u32 Z + Z zones** (below), then modulation/effects blocks (not decoded) |
| 5 | final blocks (global mod matrix, macros, macro names as strings: `FATDist`, `Cutoff`, `Space`…) |

**Zone** (`FUN_180427bf0`):

| Field | Type | Content |
|---|---|---|
| marker | u8 | `0xFF` |
| version | u8 | 2…8 (7 in GM, 8 in Melodic Techno) |
| name | str | zone name (`GM Piano (6)`, `Empty Zone`…) |
| **rootKey** | i32 | MIDI root note (60 = FL's C5) |
| **lowKey, highKey** | 2 × i32 | key range (inclusive) |
| **lowVel, highVel** | 2 × i32 | velocity range 0…127 |
| ? | i32 | 0 in the data |
| (version > 1) | 2 × i32 | 0 in the data |
| S | u32 | number of oscillator *slots* (4) |
| *slot* × S | — | see below |
| M | u32 | number of zone parameters (746 or 751; < 745 receives defaults) |
| M × f32 | — | zone parameters 0…1 (§1.7). **The index in the file is the internal index** used by `setParam` |

**Slot** (`FUN_18041f770`), one per zone oscillator:

| Field | Type | Content |
|---|---|---|
| version | u32 | 2 or 3 |
| flag | u8 | 0 in the data |
| **loopMode** | u32 | 0 = no loop, 1 = continuous loop (only values seen) |
| **sampleStart** | i64 | playback start, in frames |
| **loopStart** | i64 | loop start, in frames |
| **loopEnd** | i64 | loop end (or sample end when `loopMode` = 0), in frames; 0 = to the end |
| file | str | sample name with **`.wav`** extension (empty = synthetic oscillator). In the pack the entry is the same base name with **`.flac`** |
| (version ≥ 2) | u32 | 1 in the data |
| (version ≥ 3) | i64 | 0 in the data |

Example ("Piano Dark", GM): 19 zones, one sample each, contiguous ranges 0…127 (`GM Piano (10)` root 24
keys 0–25; …; `GM Piano (8)` root 94 keys 94–127), no loop **[DATA]**.

**Pitfall in the loop points [DATA]** (`tmp/r13/survey2.py`, 2070 *slots* with a sample): in 545 *slots*
of "General Midi Library" (all with FLAC at 22 050 Hz) `loopEnd` is **greater than the FLAC length**,
almost always by a factor ≈ 2.0 (also 1.45, 1.5, 1.72…). The samples were resampled for the pack and
the points stayed at the original rate (44.1 kHz, 32 kHz…). In the other packs (44.1 kHz) everything is inside the
file. How FLEX handles this was not read in the engine **[HYP]**; see §Recommendations for the practical rule.

FLAC: 22 050 Hz (1407 *slots*), 44 100 Hz (456), 32 000 Hz (151) and rare rates (20 279…96 000); 16 bits;
almost all mono. No loop/cue blocks in the FLAC (only STREAMINFO + VORBIS_COMMENT) **[DATA]**.

### 1.6 Global parameters (45) **[DECOMP `FUN_1803e96a0` (names), `FUN_1803eefc0` (application) + DATA]**

| i | Name | i | Name |
|---|---|---|---|
| 0–4 | Env1 Attack/Hold/Decay/Sustain/Release **Mod** | 23 | Master Filter Type |
| 5–9 | Env2 Attack/Hold/Decay/Sustain/Release **Mod** | 24–29 | Delay Type / Mix / Time / Feedback / Mod / Color |
| 10–17 | **Macro 1…8** | 30–34 | Reverb Mix / Decay / Size / Mod Depth / Color |
| 18 | Cutoff Mod | 35, 36 | Pre-Limiter Level, Post-Limiter Level |
| 19 | Env Amount Mod | 37, 38 | Delay Active, Reverb Active |
| 20 | Resonance Mod | 39 | **Transpose** (0.5 = 0) |
| 21 | **Master Filter Cutoff** | 40 | Global Filter Active |
| 22 | Master Filter Resonance | 41–44 | Limiter Active, Limiter LMH Mix, Limiter Type, Reverb Mod Speed |

The "Mod" ones (0.5 = neutral) are relative offsets applied on top of envelopes 1 and 2 of **all**
zones; "Cutoff/Env Amount/Resonance Mod" likewise for the zones' filter 1. Exact scales not read **[HYP]**.

### 1.7 Zone parameters (indices in the file) **[DECOMP `FUN_18041faf0` (name registration, index + 1) + DATA]**

The name registration uses `file_index + 1`; the correspondence was confirmed in the data (disabled
oscillators have `Active` = 0 exactly at the predicted index; the reader forces `Osc k Pitch` = 0.5 at indices
43/107/171/235 in old presets). Oscillator blocks with stride **64**, envelopes with stride **30**.

| Index (file) | Meaning |
|---|---|
| 2 | filter 1 type (`round(x·127)`; 3 = 0.0236 is the most common) **[HYP]** |
| 3, 4, 5 | Filter 1 Cutoff, Resonance, Env Amt |
| 7, 8, 9 | Filter 2 Cutoff, Resonance, Env Amt |
| 17, 19 | Filter 1/2 Insert Amount |
| 33 + 64k | **Osc k Active** (k = 0…3) |
| 34 + 64k | **oscillator type** `t = round(x·127)`: 0 Sample, 1 Waveform Dual + FM, 2 Noise, 3 WaveTable, 4 WaveTable Xfade, 5 Waveform Simple (default for disabled ones), 6 WaveTable HQ, 7 Waveform Simple + FM (`FUN_1803e1df0`) |
| 39 + 64k | Osc k Transpose (Semi), 0.5 = 0 |
| 40 + 64k | Osc k Start Phase |
| 43 + 64k | Osc k Pitch (fine), 0.5 = 0 |
| 48 + 64k, 49 + 64k | Osc k Wave 1/2 Mix, PW |
| 50/51 + 64k | PW Amount 1/2 |
| 52…55 + 64k | FM coming from oscillators 1…4 |
| 78 + 64k | Osc k Sample Start |
| 86 / 88 + 64k | Wavetable Position / Morph Amount |
| 354 + 30e | **Env e+1** (e = 0…3): +0 Attack, +3 Hold, +5 Decay, +8 Sustain, +10 Release (+1, +2, +4… = curves/flags) |
| 665…668 | Osc 1…4 Vol (0.879 = default) |
| 669…672 | Osc 1…4 Pan (0.5 = centre) |
| 673…676 | AM of osc 1 by osc 1…4 |
| 677, 678 | OSCs Volume, OSCs Pan |
| 743, 750 | Oscs Pitch, Oscs Pitch Semitone Quantized |

Env1 is the **amplitude envelope** **[DATA+HYP]**: on the piano, A = 0, D = 0.5, S = 1, R = 0.425; on the flute,
A = 0.4, R = 0.25; on the pad "PAD Minoo", A = 0.38, R = 0.45. Env2 is the modulation envelope wired to
`Filter 1 Env Amt` in the bass/pluck presets (D 0.53…0.76, low S).

Scales **[HYP, not read in the engine]**: transposition ≈ `46·(2x − 1)` semitones (the very frequent values 0.24 / 0.367 /
0.63 give −23.9 / −12.2 / +12.0 → round to integer when the distance is < 0.3);
envelope times ≈ `t = 10·x³` s (cubic curve typical of FL Mobile; validate); oscillator volume with
0.879 ≈ 0 dB.

The waveform type inside "Waveform Simple"/"Dual" and the *wavetable* tables live in objects
stored after the parameters (`FUN_18041c970`, versions ≥ 5) — **not decoded**.

### 1.8 `.flex2preset` presets (v2 engine, "Core 808s"/"Core Synths" only) **[DATA]**

Different format (objects with 32-hex GUIDs, macro names `Tone/Drive/Lo-Fi/Wobble/Chorus/Phaser/
Echo/Space`, zone `New Zone`). Not decoded; the sample used comes from the corresponding `.usedsamples`
(1 sample per preset in the 808s). Right after the sample name there is an integer 0x34 (52) **[HYP: root note]**;
the names `MSXII_808s_C_xx` indicate C.

---

## 2. Kepler (4 channels) **[DECOMP `FUN_18001f170`, `FUN_180016300`, name table `FUN_18001fa30` + DATA]**

Juno-style analogue polyphonic (DCO with pulse/saw/sub, HPF, VCF, one ADSR, chorus I/II). The state
starts with u32 0, u32 size−12, u32 0, str mode name (`Kepler-1` = reduced panel; `Kepler-2` =
full model, used by Kepler Exo). In `Kepler-1` mode:

| Off | Type | Content |
|---|---|---|
| 0x19 | u32 | version (4) |
| 0x1D | u32 | n (34; 36 in newer versions) |
| 0x21 | n × f32 | panel controls, 0…1, in the order below |
| after | u8, f32, f32, u8, u8, u8 | flags (porta/legato?), values 1.0 and 0.5, quality mode **[HYP]** |

Float order (the reader converts each position to the internal index with the table of `FUN_180016300`;
names resolved via the jump table of `FUN_18001fa30`):

| k | Control | k | Control |
|---|---|---|---|
| 0–4 | Arpeggiator Active / Mode / Range / Rate / Sync | 19, 20 | **Cutoff**, **Resonance** |
| 5–8 | LFO1 Rate / BPM Sync / Delay / Trig Mode | 21, 22 | Env1→Cutoff Polarity, **Env1→Cutoff Amount** |
| 9 | LFOs Trigger | 23, 24 | LFO1→Cutoff, Cutoff Key Follow |
| 10 | LFO1→DCO1 Amount (vibrato) | 25 | VCA Source (envelope/gate) |
| 11, 12 | DCO1 Pulse PW Amount, PW Mod Source | 26–29 | **Env1 Attack / Decay / Sustain / Release** |
| 13, 14 | DCO1 **Pulse Active**, **Saw Active** | 30 | Chorus Mode (0 off, I, II, I+II) |
| 15, 16 | DCO1 **Sub Osc Active**, Sub Osc Volume | 31 | Hold |
| 17 | Noise Osc Volume | 32 | Octave Transpose (0.5 = 0) |
| 18 | HP Freq | 33 | Main Volume |
| 34, 35 | LFO1 Retrig Phase Active / Phase (only n = 36) | | |

Examples: "Pads - Kepler" = pulse + saw, PW 0.25, LFO→DCO 0.19, cutoff 0.646, env→cutoff 0.335,
ADSR 0.3/0.5/0.5/0.317, chorus 0.66; "AceBass" = cutoff 0.105, env→cutoff 0.66, ADSR 0/0.352/0.196/0
**[DATA]**. Scales **[HYP]**: exponential cutoff ~ `20·2^(10x)` Hz; times ~ `10·x³` s.

## 3. BassDrum (5 channels) **[STR+DATA]**

`"SSLF"` + u32 0x560 + tag `1kiK` + preset name (fixed field) + **noise sample path** (field of
~512 B, absolute Mac/Windows path; the base name exists in `BassDrum/Noise Samples/`) + **23 floats
0…1** starting at ~0x50C. The parameter names in the engine are exactly 23: Base, Peak, Duration, Var. Peak,
Var. Slide, Slide Time, Var. Base, Click Freq, Click Amount, Var. Phase, Click Cut, Drive, Attack, Sampling
Ratio, Butt, Noise A/D/S, Noise Mix, Noise Filter, Noise Start, Noise Pitch, Output Level — the **order of the
floats was not confirmed** (the strings are shuffled by the compiler's *pooling*) **[HYP: order
above]**. LYNX: same scheme as Fruit Kick (round 10 §4): sine with a Peak→Base pitch envelope over
Slide Time, decay = Duration, click = short noise (Click Amount), Drive → saturation if available.

## 4. Wave Traveller (3 channels) **[DATA]**

`0xD5`: u32 1000 (version), u32 (header block size), u32 1 000 005, **u32 root note** (60…67),
u32 L + **`.wav` path (L bytes, with `%FLStudioFactoryData%`)**, then records `tag = 2000` (88 or
296 B) and ~36 KB of the *scratch* curve. Demos: `Legacy\Vocals\FLS_ThisDJisSoFunky.wav`,
`Vocals\VOC Lets Go.wav`, `Drums\Snares\808 Snare.wav`. LYNX: sampler channel with that sample
(`%FLStudioFactoryData%` → FL installation folder), root = field 4; ignore the scratch curve (log).
The motion presets are `Wave Traveller/Presets/*.spln` (Accelerate, Backwards, PingPong, Slowdown…).

## 5. Toxic Biohazard (3 channels) and PoiZone (2 channels) **[STR+DATA]**

- **Toxic Biohazard:** `"SSLF"` + u32 0x273A + `TBi2`, fixed 10 050 B: ~2 KB at the start of **drawn
  waveforms** (only in presets with their own wave), parameter block around 0x204C…0x22C0 and the preset
  name ("HiEnergy"). 6-operator FM (strings O1…O6 Coarse/Fine/Enable/Init/Dec/VelSens/Phase,
  Flt Cutoff/Res/EG, Master Atk/Dec/Sus/Rel, Lfo1/2). Not decoded. LYNX: "generic FM" preset
  (osc1 sine + osc2 sine +12 st with AM, ADSR 0/0.4/0.6/0.3) and log.
- **PoiZone:** `"SSLF"` + u32 0x2B4 + `Ver4` + ~47 floats 0…1 starting at 0x0C, a *gate* step
  table (−1 = empty) and the preset name at 0x27C (e.g.: "ATMO Burst of Hopes"). Parameters (engine names):
  OscA Shape/PW/Sync/Ring, OscB Shape/PW/Pitch/Detune, Osc/Noise Balance, EG A/D/S/R + Amount,
  Flt. Cutoff/Reso/KbdTrk/VelTrk, Amp. A/D/S/R, LFO Rate/Amount/Dest/Retrig, Unison, Glide, Delay, Chorus,
  Gate 1…16. Order not confirmed **[HYP]**. Maps well to the LYNX synthesizer (2 osc + noise,
  2 ADSR, LP), but requires reading the index table in the engine (same technique as Kepler §2).

## 6. BooBass and Drumpad (addendum to round 10)

No format news. Updated count: Drumpad 10 channels (+1 "DrumPad"), BooBass 3.

---

## Recommendations for LYNX

1. **Dispatch by `0xC9`:** "FLEX", "Kepler" (and "Kepler Exo" → *fallback*), "BassDrum"/"Bassdrum",
   "Wave Traveller", "Toxic Biohazard", "PoiZone". Keep applying the channel events (`0xDA`/`0xDB`).
2. **FLEX — fallback without opening the packs:** read the pack and preset names from `0xD5` (§1.1) and choose
   a LYNX synth patch by the category deduced from the names (Bass → saw −1 oct + LP + short decay; Pad →
   2 detuned saws + slow attack; Pluck → saw + LP with envelope; Lead → saw + square; Keys/Bell/Piano →
   sine + triangle; 808/Kick → sine with pitch drop), applying the state globals that are
   independent of the preset (*Master Filter Cutoff*, *Transpose*) **[HYP]**, and record it in the report.
3. (removed: extraction of samples from the packs)
4. (removed: mapping of the internal presets, which would require opening the packs)
5. **Kepler → LYNX synthesizer:** osc1 `saw` (if Saw Active) with level 1; osc2 `square` (if Pulse
   Active; PW ignored unless LYNX has PWM); osc3 `square` −1 octave with level = Sub Osc Volume (if
   Sub Active) — or `noise` if Noise Osc Volume > Sub; cutoff/resonance (§2); `filterEnv` = Env1 ADSR
   with amount `Env1→Cutoff Amount` (sign from Polarity); amplitude ADSR = Env1 (or *gate* if VCA Source
   = gate); LFO1 → pitch with amount `LFO1→DCO1` and → cutoff with `LFO1→Cutoff`; chorus → the channel's chorus
   FX; `Octave Transpose` → oscillator octave. Arpeggiator Active → the channel's `ArpSettings`.
6. **BassDrum / Wave Traveller / Toxic / PoiZone:** as in §3–§5. For Wave Traveller, play the sample
   (it is the cheapest and most faithful gain); the other three stay as approximations with a log.
7. **Suggested validation:** render in FL "Piano Dark", "Harp" (loop outside the file!), "Synpluck Cube",
   "808 Clean" and "BAS Lottunma" and compare with LYNX (per-zone tuning, loop, envelope); measure the
   transposition of an oscillator with x = 0.24 to pin down the semitone scale.

## Evidence index

| Function / data | What it does | Marker |
|---|---|---|
| FLEX eng. `FUN_18038b980` | Opens pack: extensions `.flexpack`/`.flex2pack`, entry `presetsIndex.presetsIndex` | DECOMP |
| FLEX eng. `FUN_1803f1300` | `.flexpreset` reader (`flex`/`00su`, version, globals, groups) | DECOMP |
| FLEX eng. `FUN_1803c9aa0` | `mgrp` group: 300 parameters, zones, modulation blocks | DECOMP |
| FLEX eng. `FUN_180427bf0` | Zone: root, ranges, *slots*, parameters; conversions from old versions | DECOMP |
| FLEX eng. `FUN_18041f770` | *Slot*: loop mode, start, loop, file | DECOMP |
| FLEX eng. `FUN_1803e96a0`, `FUN_1803eefc0` | Names and application of the 45 global parameters | DECOMP |
| FLEX eng. `FUN_18041faf0` | Name registration of the zone parameters (index + 1) | DECOMP |
| FLEX eng. `FUN_1803e1df0` | Names of the 8 oscillator types | DECOMP |
| `tmp/r13/survey.py`, `survey2.py` | 2070 *slots*; loops × length; rates | DATA |
| 195 FLEX blobs (`tmp/r11/blobs.pkl`) | `0xD5` layout; 56/57 packs present | DATA |
| Kepler eng. `FUN_18001f170` | `Kepler-1`/`Kepler-2`: version, n floats, final flags | DECOMP |
| Kepler eng. `FUN_180016300` | Panel position → internal index | DECOMP |
| Kepler eng. `FUN_18001fa30` (jump table `0x21934`/`0x216b8`) | Names of the 258 parameters | DECOMP |
| BassDrum / Toxic / PoiZone engine strings | Parameter names | STR |
| Wave Traveller / BassDrum / Toxic / PoiZone blobs | Headers, paths, preset name | DATA |

### Open questions

- FLEX: real scales (envelope time, cutoff, transposition in semitones, volume); waveform selection
  in "Waveform Simple/Dual" and contents of the *wavetable* objects (version ≥ 5); how the engine handles
  `loopEnd` beyond the end of the FLAC (rescale? *clamp*?); semantics of the 300 group parameters and of the
  modulation matrix (macros → destinations); the *slot*'s 2nd integer (= 1) and the version-3 i64.
- FLEX v2 (`.flex2preset`, `engineV2_x64.dll`): GUID object format, root note and loop.

- Kepler: scales; meaning of the final 12 bytes; `Kepler-2` model (Exo, 0 channels in the demos).
- BassDrum/PoiZone: float order (apply the Kepler jump-table technique to their engines).
- Toxic Biohazard: layout of the parameter block and of the drawn waveforms.
