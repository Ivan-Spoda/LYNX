# FL Studio 2026 behavioural analysis: round 12 (REA) — GMS, Ogun/Autogun and remaining generators (DrumSynth Live, Sakura, Morphine)

> **Nature of this document.** Continuation of `REA_rodada10.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode has been copied: what is here are descriptions,
> data layouts and formulas written in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence.

- **Targets** (local copies in `tmp/r12/bin/`, originals in `Plugins/Fruity/Generators/<name>/`):
  - `GMS/GMS_x64.dll` (SHA-256 `20c88eba…ec9c`, **C++/MSVC**, classes `GMSynth` / `CPluginRenderer`; waveforms
    in `GMS/Synth Waves/*.wav`)
  - `Ogun/Ogun_x64.dll` (SHA-256 `c2c717df…e4c6`, Delphi, `TTestPlug`, VMT `0x8621f8`)
  - `Autogun`, `Fruity DrumSynth Live`, `Sakura`, `Morphine`: format + DFM/strings only.
- **Tools:** Ghidra 12.1.4 *headless* in dedicated projects (`/tmp/r12gh_<dll>`), scripts in
  `tmp/r12/gh/` (the shared REA had the binary from the other round loaded). Collection: `tmp/r10/blobs.pkl`
  (`.flp` + `.fst`, `0xD5` after `0xC9`). For GMS also the 306 native `*.gmsynth` presets in
  `Data/Patches/Plugin presets/Generators/GMS/` (same content as the `0xD5`, without the SSLF envelope).
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the `.flp`/`.fst`/`.gmsynth` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

### Counts in the data

| Plugin (`0xC9`) | Channels in `.flp` | Total (`.flp`+`.fst`) | Size of `0xD5` | Container |
|---|---|---|---|---|
| GMS | 10 | 15 (+306 `.gmsynth`) | 4968 B | `SSLF` + native `Syn4` preset |
| Ogun | 23 | 258 | 600 B…240 KB | u32 version + zlib |
| Autogun | 9 | 13 | 2.4…172 KB | identical to Ogun |
| Fruity DrumSynth Live | 9 | 15 | 19,467 B (v3/v4), 4232 B (v1) | own binary format |
| Morphine | 7 | 11 | 47…342 KB | `SSLF` + `Mor0` chunks |
| Sakura | 5 | 510 | 596 B | `SSLF` + `Skr1` + 146 floats |

---

## 1. GMS (Groove Machine Synth) **[DECOMP+STR+DATA]**

GMS is a 3-oscillator subtractive synth with **single-cycle wavetables** (each oscillator loads a `.wav` of
1024 samples from the `Synth Waves` folder), noise, osc2→osc1 modulation (sync / FM / AM), unison up to 16 voices,
a 4-pole *ladder* filter with saturation, amplitude ADSR, 2 AD modulation envelopes, 2 LFOs, a rack of
10 fixed effects and a small MIDI matrix. Origin: FL Studio Groove / Mobile. All parameters are
**floats 0…1**.

### 1.1 Container and layout **[DECOMP+DATA]**

`SaveRestoreState` (`FUN_1800c82f0`): the `0xD5` is `"SSLF"` + u32 `n` + `n` bytes; the `n` bytes are exactly the
`.gmsynth` file (the same one the plugin's preset browser writes). Preset reader: `FUN_1800f4e60`;
writer: `FUN_1800f43d0`.

| Off (in preset) | Size | Content |
|---|---|---|
| 0x000 | 4 | version tag: `"4nyS"` (u32 `0x53796E34`, current) or `"3nyS"` (`0x53796E33`, old). `0x53344445` = "demo" variant (the reader refuses it if the plugin is in demo mode) |
| 0x004 | 7 × 512 | 7 path *slots* (ASCII, zero-terminated). Only the first 3 are used: **waveform of oscillators 1, 2, 3** (absolute path of the `.wav`, e.g.: `…\GMS\Synth Waves\Saw Mean.wav`); the other 4 are zeroed |
| 0xE04 | 3 × i32 | **only in `Syn4`**: UI state (selected FX slot, selected envelope 0/1, selected LFO 0/1). Ignore |
| 0xE10 (`Syn3`: 0xE04) | 212 × f32 | **parameters 0…211** (§1.2), value in 0…1 |
| +0x350 | 512 | preset name (ASCII; after the zero comes memory garbage — ignore) |

Sizes: `Syn4` = 4 + 3584 + 12 + 848 + 512 = **4960** (+8 for SSLF = 4968 in the `.flp`); `Syn3` = **4948**.
In the data: 206 `Syn3` presets, 115 `Syn4` (includes the 15 from `.flp`/`.fst`) **[DATA]**.

Reader details **[DECOMP]**:
- The wave file name is resolved by **base name**: it tries the stored path, then the plugin's
  `Synth Waves` folder and the user folder. `"Devils Horn"` is renamed to `"Double Spike"` (current
  file). For LYNX the base name without extension is enough.
- The `.wav` is resampled to 1024 points and turned into 512 band-limited tables (FFT); the table
  used depends on the note frequency (`FUN_1800ef980`).
- Parameters ≥ 197 may contain **uninitialised garbage** in old presets (e.g.: "Basic Acid Bass" has
  1.8e31 at 200). Sanitise: non-finite value or outside [0,1] ⇒ 0 **[DATA]**.

### 1.2 Parameter indices **[DECOMP]** (names from `FUN_1800f28f0`)

Even/odd pairs = instance 1/2 (envelope 1/2, LFO 1/2).

| i | Internal name | Default (`FUN_1800f1bd0`) |
|---|---|---|
| 0 | CHN: Mix Level | 0.75 |
| 1 | CHN: Panning | 0.5 |
| 2 | CHN: Pitch | 0.5 |
| 3 | CHN: Note Length | 0.444 (not used in the sound) |
| 4, 5, 6, 7 | CHN: Amp Attack / Decay / Sustain / Release | 0 / 1 / 1 / 0.1 |
| 8 | CHN: Amp Level | 0.5 |
| 9 | CHN: Shuffle Enable | 0 |
| 10, 11 | CHN: Slide Time / Slide Enable | 0 / 0 |
| 12, 13, 14, 15 | Filter Cutoff / Resonance / KBD Tracking / Type | 1 / 0 / 0 / 0 |
| 16, 17 | Envelope 1/2 Amount | 0 |
| 18, 19 | Envelope 1/2 Invert | 0 |
| 20, 21 | Envelope 1/2 Attack | 0 |
| 22, 23 | Envelope 1/2 Decay | 1 |
| 24, 25 | Envelope 1/2 Destination | 0 |
| 26, 27 | LFO 1/2 Retrig | 0 |
| 28, 29 | LFO 1/2 Amount | 0 |
| 30, 31 | LFO 1/2 Invert | 0 |
| 32, 33 | LFO 1/2 Shape | 0 |
| 34, 35 | LFO 1/2 Rate | 0 |
| 36, 37 | LFO 1/2 Sync | 0 |
| 38, 39 | LFO 1/2 Destination | 0 |
| 40 | SYNTH: Mono Voice | 0 |
| 41 | SYNTH: Unison Voices | 0 |
| 42 | SYNTH: Unison Detune | 0.25 |
| 43 | SYNTH: Unison Stereo | 0 |
| 44 | SYNTH: Octave | 0.5 |
| 45 | SYNTH: Noise Mix | 0 |
| 46, 47 | SYNTH: Unison Phase / Unison Phase Retrig | 0 |
| 48, 49 | SYNTH: Mix Oscillator 2 / Mix Oscillator 3 | 0 |
| 50, 51 | SYNTH: Modulation / Mod Kind | 0 |
| 52, 53, 54 | Phase Offset Osc 2 / Phase Retrig Osc 2 / Osc 2 Invert | 0 |
| 55, 56 | Osc 1 Pitch / Osc 1 Detune | 0.5 |
| 57, 58 | Osc 2 Pitch / Osc 2 Detune | 0.5 |
| 59, 60 | Osc 3 Pitch / Osc 3 Detune | 0.5 |
| 61…116 | unnamed (values 0/0.5/1 in the data; not read by the engine we analysed) **[HYP: internal FX state]** | |
| 117+k, 127+k | FX k "Param X" / "Param Y" (k = 0…9) | |
| 137+k | FX k LFO Rate | |
| 147+k | FX k "Glue" (FX k is active if ≠ 0) | |
| 157+k, 167+k, 177+k | FX k LFO Shape / LFO Sync / LFO Amount | |
| 187…196 | unnamed | |
| 197 | (not used in the sound) | |
| 198, 199 | **Velocity →** amount (bipolar `2x−1`) / destination (integer, table below) | |
| 200 | Velocity → Amp Level (bool) | |
| 201, 202 | MIDI source 2 → amount / destination (source `+0x3A14`) **[HYP: mod wheel]** | |
| 203, 204 | MIDI source 3 → amount / destination (source `+0x3A08`) **[HYP: aftertouch]** | |
| 205 | Pitch-bend range: `int(12x)` semitones ("%d smt"; default 0.167 = 2) | |
| 206 | Output EQ on (bool) | |
| 207…211 | 5-band EQ: gain = `(x − 0.5)·24` dB | |

The 10 fixed FX (`FUN_1800f2710`): 0 Distortion, 1 Crusher, 2 Flanger, 3 Phaser, 4 Low Pass, 5 Hi Pass,
6 Delay, 7 Reverb, 8 Panner, 9 Trancer; Param X/Y per FX: Distortion/Filter, Crush/Filter, Time/Feedback,
Depth/Feedback, Frequency/Bandwidth (×2), Feedback/Filter (×2), Panning/Binaural, Release/Depth **[DECOMP]**.

MIDI matrix destinations (198/201/203, `FUN_1800ef810`): 1 cutoff, 2 resonance, 4 modulation, 6/10 env 1/2
attack, 7/11 env 1/2 decay, 8/12 env amount, 14/17 LFO 1/2 rate, 15/18 LFO 1/2 amount, 20 amp attack,
21 amp decay. The modulation adds `(2·amount − 1)·source` to the parameter and clamps to [0,1] (`FUN_1800f50c0`). In the
data, 69 presets use **velocity → cutoff** (destination 1) **[DATA]**.

### 1.3 Conversions to real units **[DECOMP]**

Notation: `x` = the parameter's 0…1 value; "trunc" = truncation toward zero **in float32** (the plugin does
`(int)(float)`; 0.16666667·72 − 36 gives −24 in float32 — emulate in `float`, not in `double`).

**Tuning** (`FUN_1800f68f0`, `FUN_1800f5250`):
- Base note in MIDI semitones: `note + Δchannel + 12·oct`, with `Δchannel = −trunc(24 − 48·x2)` (±24 st) and
  **`oct = trunc(4·x44) − 2`** (−2…+2; x = 0.5 ⇒ 0). Data: presets in the *Basses* folder cluster at oct −1/−2 **[DATA]**.
- Frequency: **`f = 220 Hz · 2^((note − 57)/12)`** (MIDI note; 57 = A 220 Hz), plus pitch-bend
  (`trunc(12·x205)` st × bend), plus pitch modulation (below).
- Oscillators k = 1, 2, 3: **semitones = trunc(72·x − 36)** (±36), **cents = trunc(100·x − 50)** (±50);
  ratio = `2^((st + cents/100)/12)`.
- Portamento (Slide, 10/11): if on, the new voice starts at the pitch of the last note; the pitch chases the
  note with a per-sample coefficient `8.82/(T·fs)`, `T = 2·x10³ + 0.001` s ⇒ time constant
  **τ = T/8.82** (reaches ~99 % in 4.6τ ≈ 0.52·T).
- Mono (40 ≠ 0): always reuses voice 0 (mono with implicit *legato* if Slide is on).

**Mix** (`FUN_1800f68f0`): with `a = x48²`, `b = x49²`, `n = x45²`:
`g_osc1 = (1−a)(1−b)(1−n)`, `g_osc2 = a(1−b)(1−n)`, `g_osc3 = b(1−n)`, `g_noise = n`. (It is a cascade of
*crossfades*: "Mix Osc 2" swaps osc1 for osc2, "Mix Osc 3" swaps the result for osc3, "Noise" swaps everything for
uniform white noise ±0.5.) Osc2 Invert (54) inverts the sign of osc2.

**osc2 → osc1 modulation** (`FUN_1800f7530`; Mod Kind = `trunc(2·x51)`; amount
`m = max(0, x50² + envs/LFOs with destination "mod")`):
- **0 = Sync:** osc1 runs at `ratio1·(1 + 3·m)` and has its phase reset whenever a "master" at the natural ratio1
  completes a cycle (*hard sync*; m = 0 ⇒ no effect).
- **1 = FM/PM:** osc1's phase receives `osc2·15·m` (in quarter cycles **[HYP on the exact scale]**).
- **2 = AM:** `osc1 × (1 − 3·m·osc2)`.
- Data: sync 162, AM 112, FM 47 presets (m > 0 in 146) **[DATA]**.

**Unison** (41–43): voices `U = 1 + trunc(15·x41)` (1…16). Total spread in octaves
`s = (0.4·x42)²` (default 0.25 ⇒ 0.01 oct = 12 ct); voice k (0…U−1) is multiplied by
`2^((k/(U−1) − 0.5)·s)` ⇒ detuning **±600·s cents** at the extremes. Voice pan alternates `±x43`
(with odd U voice 0 stays in the centre). Compensation gain `1/(1 + 0.15·(U − 1))`. Unison Phase (46) spreads
the initial phases (`k·4·x46` × phase unit **[HYP]**).

**Amplitude** (`FUN_1800f53e0`, `FUN_1800f58e0`, `FUN_1800f7530`): the envelope `e` is a per-sample
exponential follower `e += (target − e)·c`, with `c = 8.82/(T·fs)`; **the output is `e²`**.
- Attack: target 5.0 starting from 0, `T_A = 50·x4³ + 0.001` s; moves to decay when `e ≥ 1` ⇒ **real
  attack time ≈ 0.0253·T_A = 1.265·x4³ s** (x4 = 1 ⇒ 1.27 s).
- Decay: target = sustain `S = x6`, `T_D = 50·x5⁴ + 0.001` s ⇒ τ = T_D/8.82 (x5 = 1 ⇒ τ = 5.7 s; default).
  **Sustain level at the output = S²**.
- Release: target 0, `T_R = 25·(0.55·x7 + 0.1)³ + 0.001` s, τ = T_R/8.82 (x7 = 0.1 ⇒ 11 ms; x7 = 1 ⇒ 0.30 s).
  Since the output is `e²`, the level falls to −40 dB in **≈ 2.3·τ**.
- Amp Level (8): gain `10^((50·x8 − 25)/20)` (−25…+25 dB; 0.5 = 0 dB); if 200 ≠ 0, × velocity.
- Channel output: Mix Level (0) `10^((80·x0 − 60)/20)` (0.75 = 0 dB; below −80 dB = muted) and linear
  pan: `L = (1 − x1)·g`, `R = x1·g` (centre = −6 dB on each side), smoothed.

**Modulation envelopes 1/2** (AD, no sustain; x = parameter 20/21 and 22/23): attack with target 5 and
`T_A = 300·x⁵ + 0.001` s ⇒ **real attack ≈ 7.59·x⁵ s**; decay to 0 with `T_D = 70·(0.8·x + 0.2)⁵ + 0.001`,
τ = T_D/8.82 (x = 0 ⇒ 2.5 ms; x = 1 ⇒ 7.9 s). Amount `A = x16²` (negative if Invert). Destination
`trunc(3·x24)`: **0 cutoff, 1 modulation, 2 pitch, 3 "LFO"** (multiplies the output of the LFO with the same number:
it becomes a *fade-in/out* of the LFO).

**LFOs 1/2** (`FUN_1800efb10`, `FUN_1800ef9f0`, `FUN_1800f5f00`):
- Shape `trunc(3·x32)`: **0 bipolar sine** `sin(2πφ)`, **1 unipolar triangle 0…1**, **2 descending
  sawtooth 1→0**, **3 square 1/0**.
- Sync `trunc(2·x36)`: 0 = free: **f = 25·(1 − x34)² + 0.01 Hz** (x = 0 ⇒ 25 Hz; x = 1 ⇒ 0.01 Hz);
  1 = synced: period = `v` beats with `v = [1/8, 1/4, 3/8, 1/2, 3/4, 1, 2, 4, 8][trunc(9·x34)]`
  (labels "1/8"…"8/1"); 2 = synced ×4 (`4v` beats) **[DECOMP; HYP on the "beats" vs "bars" reading]**.
- Retrig (26): resets the phase on note-on. Amount `x28²` (negative if Invert).
- Destination `trunc(3·x38)`: **0 cutoff, 1 modulation, 2 pitch, 3 volume** (added to the amplifier's `e²`).
- Validation: "Never Out — Disco Fuzz" has a 5.9 Hz sine LFO on pitch with ±0.28 st (= vibrato) **[DATA]**.

**Destination scales** (`FUN_1800f68f0`, `FUN_1800f7530`): pitch: envelope **±24 st × A**, LFO
**±12 st × amount**; cutoff: added directly on the normalised cutoff scale (1.0 = 9.77 octaves, below);
modulation: added to `x50²`.

**Filter** (`FUN_1800f32f0`, table in `FUN_1800f0b20`): 4-pole *ladder* of 1 pole each, with
rational saturation at the input and **4× oversampling**. For a normalised cutoff `u` (= x12 + keytrack +
envelopes/LFOs, limited to [0,1)): coefficient `g = 0.0005·2^(9.7734·u)` (16-bit table) and
feedback `k = x13 · 0.7565·(5 + 7.5·g)` (≈ 3.8…6.3 · resonance; self-oscillates near x13 ≈ 1).
Equivalent frequency (1st-order pole at 4·fs): **`fc ≈ −ln(1 − g)·4·fs/(2π)`** ⇒ at 44.1 kHz:
u = 0 → 14 Hz; 0.25 → 77 Hz; 0.5 → 420 Hz; 0.75 → 2.4 kHz; 1 → 16 kHz. The table does not depend on `fs`
**[HYP: at 48 kHz everything rises ×1.09]**.
- Keytracking (14): `u += x14·(note − 57)/128` (x14 = 1 ⇒ ~0.92 octave per octave).
- Type `trunc(2·x15)`: **0 24 dB low-pass** (output of the 4th stage), **1 band-pass** (3rd − 4th
  stage), **2 "high-pass"** (saturated input − 4th stage) **[DECOMP for the formulas; HYP for the names]**.
  Data: LP 283, type 1 in 32, type 2 in 6 **[DATA]**.

### 1.4 The waveforms (`Synth Waves`) **[DATA]**

Harmonic analysis (h1…h12 relative to the largest) of the 29 `.wav` files (script `tmp/r12/waves.py`):

| Wave | Profile | LYNX |
|---|---|---|
| Sine, Keys, Smooth Sub | dominant fundamental (Smooth Sub: h2 0.8, h3 0.4, decays fast) | `sine` (Smooth Sub: `triangle`) |
| Triangle | odd 1/k² | `triangle` |
| Square, Square Smooth, Square Retro | odd (Retro: odd ~0.1 constant) | `square` |
| Saw, Saw Mean, Saw Retro | ~1/k | `saw` |
| Beast, Crunch, Electro, Grubby, Ogre, Troll, String, Rhodes, Dirt, Fox, Funky, Filth, Strat, Silk, Vocal, Throat | rich/formant spectra (no odd pattern) | `saw` (Rhodes, String: `triangle`) |
| **Digital** | even harmonics only (h1 ≈ 0) = square one octave up | `square` **+12 st** |
| **Double Spike** (formerly "Devils Horn"), Liquid | dominant h2 | `saw` **+12 st** |

Usage in the presets: Saw 289, Sine 153, Square 81, Saw Retro 56, Saw Mean 53, Smooth Sub 34…

### 1.5 Validation against the data **[DATA]**

Script `tmp/r12/gms_dec.py` (full output in `tmp/r12/gms_all.txt`). Examples consistent with §1.3:
- "Basic Acid Bass": oct −1, cutoff 0 (14 Hz) + env 1 → cutoff 0.65 (≈ +6.3 oct ⇒ peak ~1.1 kHz) with
  τ = 0.42 s, resonance 0.87 = classic acid.
- "Never Out — Bass Impacter": env 1 → pitch, amount 1 (±24 st), decay τ = 12 ms = kick "drop".
- "Bell Pure Crystal": cutoff 0.205 + **velocity → cutoff** +0.94: the brightness comes from velocity.
- Mono in 82/321, unison ≥ 2 in 267/321, full sustain in 203/321.

---

## 2. Ogun and Autogun **[DECOMP+STR+DATA]**

Ogun is an additive "metals/bells" synth (harmonics generated from a *seed*, with per-harmonic decay and
Sytrus-style "articulators"). **Autogun is the same engine and the same format** (the difference is the UI, which
picks the preset at random by number; the full state is stored) **[DATA: same fields, same versions]**.

### 2.1 Container and layout (`SaveRestoreState` = `FUN_00864d00`, VMT+3)

| Off | Content |
|---|---|
| 0x0 | u32 version. If > 0xFFFF, the version is the high word (FL writes `0x00130003` = v19 and `0x00120003` = v18; old `.fst` files store 0…14 directly). ≥ 0x130004 is refused |
| 0x4 | **zlib stream** with the rest |

Decompressed content (v ≥ 14):

| Off | Content |
|---|---|
| 0x000 | **64 × i32** = parameters `P[0…63]` (internally `P+0x12C + 4i`). Old versions store less: 12 B of header (8 if v < 8) + `0xF4` B reduced by version (−1 if v < 1, −3 if v < 2, −1 if v < 7, −16 if v < 9, −1 if v < 11, −1 if v < 12, −6 if v < 13) |
| 0x100 | 3 bytes: Delay tempo-based, Delay enable, (?) |
| 0x103 | 3 × i32: type of the 3 EQ bands (0 low shelf, 1 peaking, 2 high shelf) |
| 0x10F | comment/name (v ≥ 18: string; v < 18: u32 size + bytes) — e.g.: "Metal Drum\rAstes\rX/Y change timbre" |
| … | 13 articulators (point envelopes + LFO + keyboard/velocity/X/Y maps, same object as Sytrus), then more EQ articulators and the timbre table; large blobs (up to 1.2 MB decompressed) **were not decoded** |

### 2.2 Parameters (DFM `TSynthEditorForm` Tag = index) **[STR+DECOMP]**

| i | Name | Range (def.) | Conversion / note |
|---|---|---|---|
| 0 | Master level | 0…256 (128) | gain `v/256`? **[HYP: linear]** |
| 1 | Master coarse pitch | −24…24 (0) | semitones |
| 2 | Master fine pitch | −50…50 (0) | cents |
| 3, 4 | Modulation X / Y | 0…512 (256) | macro controls (mapped in the articulators) |
| 5 | Timbre pre-decay | 0…1000 (100) | `v/1000` |
| 6 | **Timbre decay time** | 0…10000 (4000) | **10000 = infinite**; otherwise **t = (21^(v/10000) − 1)·997.5 + 50 ms** (4000 ⇒ 2.4 s; 0 ⇒ 50 ms; 9000 ⇒ 15 s) |
| 7 | **Timbre release time** | 0…10000 (2000) | same formula (2000 ⇒ 0.89 s) |
| 8 | Timbre fullness | −1000…1000 (0) | `v/1000` (negative = darker **[HYP]**) |
| 9 | Timbre morphing / randomness | 0…1000 (300) | `v/1000` |
| 10, 11 | Timbre seed 1 / 2 | 0…9999 | 0 = "None"/"Random"; the value goes through an integer *hash* and becomes the seed of the harmonics generator |
| 12 | **Unison order** | 1…9 (1) | number of voices |
| 13 | Unison panning | 0…128 (128) | |
| 14 | Unison volume | 16…128 (128) | |
| 15 | Unison pitch | 0…256 (128) | spread **[HYP: like Harmless 67]** |
| 16 | Unison phase | 0…128 (64) | |
| 17 | **Filter frequency** | 0…256 (128) | `u = v/256` (§2.3) |
| 18 | **Filter resonance** | 0…256 (128) | `r = v/256` |
| 19…25 | Chorus order, depth, speed, delay, spread, cross, mix | | |
| 26…30 | Delay feedback (0…127), time (ticks/192 per beat, def. 144), stereo offset, volume, mode (0 normal, 1 inverted, 2 ping-pong) | | |
| 31…39 | Reverb lowcut, highcut, predelay, room, diffusion, decay, high damping, color, wet | | |
| 40…42 | EQ levels bands 1–3 | ±1800 | hundredths of a dB **[HYP]** |
| 43…48 | EQ freq. 1–3, width 1–3 | 0…65536 | |
| 49 | **Filter type** (`FTypeSelect`, field `+0x1F0`) | 0…3 | 0 low pass, 1 band pass, 2 band stop, 3 high pass |
| 50 | **Filter mode/order** (`FOrderSelect`, `+0x1F4`) | 0…3 | 0 **off** (default), 1 single, 2 dual, 3 triple (cascade of 2-pole sections) |
| 51 | FLAT (`FFlatBtn`, `+0x1F8`) | bool | resonance follows the cutoff |
| 52 | Richness + 5 (`RichnessWheel` 1…11, `+0x1FC`) | 6…16 | **[HYP: log2 of the number of partials]** |
| 53…62 | packed bytes: reverb on/tempo, mod smooth, declick, alt decay, etc. | | |
| 63 | Magic dust (`SGSlider`) | 0…1000 (430) | |
| ≥ 49 (automation) | articulator parameters (8 per articulator) | | `FUN_008698e0` |

Field→control mapping confirmed by `FUN_008649e0` (updates the UI from the state) + the form's published
field table (`tmp/r12/fields.py`) **[DECOMP+STR]**. Articulator order (combo
`ArtSelect` without separators): 0 Volume, 1 Filter frequency, 2 Filter resonance, 3 Pitch, 4 Unison pitch,
5 Unison phase, 6 Timbre pre-decay, 7 decay time, 8 release time, 9 fullness, 10 morphing, 11 rotation,
12 EQ shifting; then Forced harmonics, Impulse EQ, Decay EQ, Release EQ **[STR; order in the stream HYP]**.

### 2.3 Filter (`FUN_00867cc0`) **[DECOMP; final scale HYP]**

Cascaded biquads (order = P50), with `u = P17/256 + articulator`, `r = P18/256 + articulator`:
- **LP:** `c = (1501^u − 1)·0.000665`; angular frequency `ω = (c + 0.003)·2π·s`; `Q = 0.1` (r ≤ 0),
  `50` (r ≥ 1), otherwise `(2001^r − 1)·0.02495 + 0.1`. With `s = 0.5` (**[HYP]**: field `+0xF3C`),
  **`fc ≈ (fs/2)·(c + 0.003)`** ⇒ u = 0.5 → ~620 Hz, u = 0.75 → ~5 kHz, u = 1 → Nyquist.
- **BP/BS:** `c = min(1.0001; (1501^u − 1)·0.0004)`, `Q = (101^r − 1)·0.1199 + 0.01` (0.01…12).
- **HP:** like LP with `c ≤ 0.6`.
- Default: mode **off** (the filter only exists if P50 > 0). In the 23 channels: order 0 in 9, 1 in 3, 2 in 3, 3 in 8
  **[DATA]**.

### 2.4 Data (23 Ogun + 9 Autogun in `.flp`) **[DATA]**

Script `tmp/r12/ogun_dec.py`. Versions 17–19. Examples: "Electric Loop — Kick": decay 100 ms, release 50 ms,
triple LP u = 0.24; "Swebass": +12 st, unison 5, triple LP; "Turbular Bells 2": infinite decay,
release 20 s, −6 st; the Autogun "Pad"s: −24/−12 st, decay ~12 s, unison 4–5, LP u ≈ 0.47.

---

## 3. Fruity DrumSynth Live **[DATA+STR]**

`0xD5` (v3/v4, 19,467 B): u32 version, **u32 index of the selected sound**, then **120 Pascal names**
(u8 size + ASCII) = the embedded library (`808 Bongo H`, …, `909 Hat C/O/P`, `909 Snare`, …) and, from
0x4C7 on, the sounds' parameter blocks (not decoded). The index points into that list:
"Openhat" → `909 Hat O`, "Clap" → `808 Clap`, "Ride" → `909 Ride`, "Snare" → `909 Snare` (8/8 consistent).
Version 1 (4232 B, `Default.dsl`): 24 B records with name + binary data, index 0.

LYNX: play a drum sample chosen by **name** (prefix 808/909 + category Kick/Snare/Hat
C/O/P/Clap/Ride/Crash/Tom/Cowbell/Clave/Rim/Maracas/Bongo) from the LYNX default kit; if there is none, approximate
with the same scheme as Fruit Kick (round 10 §4.3) for kicks/toms and filtered noise for cymbals.

## 4. Sakura **[STR+DATA]**

`0xD5` = `"SSLF"` + u32 0x24C + tag `"1rkS"` (`Skr1`) + **146 × f32**. In the 510 examples float `k` matches the
DFM **Tag k**, normalised 0…1 (switches with few values at k = 7, 12, 23; "Tune" k = 1 with median
0.5; "Delay time sync" k = 112 is an integer 0…8). Useful Tags: 1 Tune, 2–5 **Amp attack/decay/sustain/release**,
6/11 String 1/2 decay, 17 String mix, 24–25 Lo-Cut cutoff/res, 26–30 Hi-Cut attack/decay/sustain/cutoff/res,
31 exciter noise, 34–37 EG ADSR, 38 LFO rate, 63–77 delay/chorus/reverb. Real scales not read
(**[HYP]** ADSR with a Harmless-like curve). LYNX: osc1 `saw` + osc2 `triangle`, LP with cutoff ≈ Hi-Cut, amplitude
ADSR from Tags 2–5, short decay (plucked string) if String decay < 0.5.

## 5. Morphine **[DATA]**

`"SSLF"` + chunks with reversed FourCC: `Mor0` (root) ⊃ `MPR0` (program) ⊃ `MVC0`, `MZH0`, `MSP0`, `MBP0`…
(additive with up to 4 generators and *morph*; 47–342 KB). Not decoded. LYNX: generic preset (saw + medium
LP + ADSR 0.01/0.3/0.7/0.3) and a warning in the log.

---

## Recommendations for LYNX

1. **Dispatch by `0xC9`:** "GMS", "Ogun", "Autogun" (same parser as Ogun), "Fruity DrumSynth Live",
   "Sakura", "Morphine". Keep applying the channel events (`0xDA`/`0xDB`) as in rounds 3/4.
2. **GMS parser:** validate `"SSLF"`; read the tag (`Syn4`/`Syn3`); 3 wave names (base name); skip 4 slots +
   (only `Syn4`) 12 B; 212 floats; sanitise indices ≥ 197. Do the conversion in `float32` with truncation.
3. **GMS → LYNX synthesiser mapping** (3 oscillators, AM via osc3, ADSR, SVF, filter and
   pitch envs, 4 LFOs, mono, porta):

   | LYNX | GMS source |
   |---|---|
   | osc k `wave` | table §1.4 by the `.wav` name (with the +12 st for Digital/Double Spike/Liquid) |
   | osc k `octave`/`semitones`/`detune` | global `oct` + `st_k` (§1.3) + Δchannel: add everything up in semitones `S`, use `octave = clamp(round(S/12), −3, 3)` and `semitones = S − 12·octave` (if it goes outside ±11, saturate and log); `detune = cents_k` |
   | osc k `level` | `g_osc1`, `g_osc2`, `g_osc3` (§1.3 Mix). Noise: if `g_noise > 0.05` and some osc has level < 0.05, use that slot as `noise` with level `g_noise` |
   | osc `pan` | 0 (the channel pan goes to the LYNX pan: `2·x1 − 1`) |
   | AM (osc3 modulates) | Mod Kind = AM with m > 0: move **GMS osc2 to LYNX slot osc3** (modulator) with depth ≈ `min(1, 3·m)`; GMS osc3 takes the free slot if its level is relevant |
   | Sync / FM | no equivalent: Sync with m > 0 ⇒ raise osc1 by `12·log2(1 + 3m)` st and switch `wave` to `saw`; FM ⇒ ignore and log |
   | Unison | if U ≥ 2 and there are free/equal oscillators: duplicate osc1's shape into the free slots with detune `±600·(0.4·x42)²` ct and pan `±x43`; level × `1/(1 + 0.15(U−1))` |
   | Amp ADSR | attack `1.265·x4³` s; decay = `2.3·T_D/8.82` (−40 dB at the output) with `T_D = 50·x5⁴`; **sustain = x6²**; release `2.3·T_R/8.82`. If the LYNX ADSR is exponential by τ, use `τ_LYNX = T/(2·8.82)` (output is `e²`) |
   | SVF | type 0 → `lowpass`, 1 → `bandpass`, 2 → `highpass`; cutoff Hz = `−ln(1 − 0.0005·2^(9.7734·u))·4·fs/(2π)` with `u = x12` (+ keytrack `x14·(note−57)/128`, evaluated at the channel's average note); LYNX resonance = `min(0.95; x13)` |
   | Filter env | envelope (1 or 2) with destination 0: attack `7.59·x⁵` s, hold 0, decay `4.6·T_D/8.82` with `T_D = 70(0.8x + 0.2)⁵`, sustain 0, release = amp release; amount in octaves = `9.77·A` (A = x16², with the Invert sign) |
   | Pitch env | envelope with destination 2: same times; amount in semitones = `24·A` |
   | Env destination 3 | *fade* of the LFO with the same number: approximate with `fadeIn = attack` of the envelope on the corresponding LFO |
   | LFOs | GMS LFO 1/2 → LYNX LFO according to destination: 0 → **cutoff** (amount `9.77·x28²` oct), 2 → **pitch** (`12·x28²` st), 3 → **volume** (`x28²`), 1 (modulation) → ignore unless AM is active. Shape 0 → `sine`, 1 → `tri`, 2 → `tri` with a warning (saw), 3 → `pulse`. Rate: free ⇒ period `1/(25(1−x)² + 0.01)` s; sync ⇒ `v` (or `4v`) **beats**. Retrig ⇒ LFO restarts per note. The LYNX pan LFO stays free |
   | Mono / porta | mono = x40 ≠ 0; porta on if x11 ≠ 0; time in beats = `0.52·(2·x10³)·BPM/60` |
   | Channel volume | `10^((80x0 − 60)/20) · 10^((50x8 − 25)/20)` |
   | Velocity → cutoff | if destination 199 = 1: add `(2·x198 − 1)·0.8` to `u` (typical velocity 0.8) **[HYP]** or, if LYNX has *velocity→cutoff*, map directly |
   | FX (Glue ≠ 0) | Delay/Reverb to channel FX if they exist; others: log |

4. **Ogun/Autogun (coarse approximation):** osc1 `saw` (or `sine` if fullness ≤ −800 and richness ≤ 3;
   `triangle` if fullness < −300), osc2 the same with +12 st and level 0.3 for "metals" (Tibetan/Bell/Gong in the name)
   **[HYP]**; transposition `P1 + P2/100` st; ADSR: attack 0, **decay = t(P6)** with sustain 0 (or sustain 1
   if P6 = 10000), release = t(P7) (§2.2); unison P12 → detune ±10 ct on the free oscillators; filter only
   if P50 > 0: type P49 (LP/BP/notch/HP), cutoff Hz §2.3, LYNX resonance `= min(0.95; P18/256)`. Volume
   `P0/256`. Log that the additive timbre and the articulators were ignored.
5. **DrumSynth Live:** use the name of the selected sound (§3) to choose the kit sample.
6. **Sakura:** ADSR from Tags 2–5 as 0…1 → times with the Harmless curve (round 10 §1.2) **[HYP]**.
7. **Suggested validation:** render in FL "Basic Acid Bass", "Bass Impacter", "Bell Pure Crystal" (GMS) and
   "Electric Loop — Kick" (Ogun) and compare envelope, cutoff and pitch with LYNX; measure the GMS filter at
   48 kHz to confirm whether the cutoff table ignores `fs`.

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| GMS `FUN_1800c82f0` | `SaveRestoreState`: `"SSLF"` envelope + size + preset | DECOMP |
| GMS `FUN_1800f43d0` / `FUN_1800f4e60` | Writes / reads the preset (`Syn4`/`Syn3`, 7×512 paths, 3 ints, 212 floats, name) | DECOMP |
| GMS `FUN_1800f1570` | Loads the `.wav`, renames "Devils Horn", generates 512 tables | DECOMP |
| GMS `FUN_1800f28f0` | Names of parameters 0–60 and of the FX (117–186) | DECOMP |
| GMS `FUN_1800f1bd0`, `FUN_1800f1ee0`, `FUN_1800f2f70` | Defaults, value text (dB, %, smt, time fractions), host→internal index | DECOMP |
| GMS `FUN_1800f2710`, `FUN_1800f27b0`, `FUN_1800f2850` | Names of the 10 FX and their Param X/Y | DECOMP |
| GMS `FUN_1800f50c0`, `FUN_1800ef810` | Parameter read with MIDI matrix; destination table | DECOMP |
| GMS `FUN_1800f5250`, `FUN_1800f53e0`, `FUN_1800f58e0` | Note-on/off: tuning, octave, ADSR, AD envs, porta, phases | DECOMP |
| GMS `FUN_1800f68f0` (+ disassembly `0x1800f6de6…0x1800f72d9`) | Per voice: mix, unison, pitch, oscillator ratios, ±24/±12 scales | DECOMP |
| GMS `FUN_1800f7530` | Render: envelopes, destinations, sync/FM/AM, noise | DECOMP |
| GMS `FUN_1800f5f00`, `FUN_1800efb10`, `FUN_1800efa80`, `FUN_1800ef9f0` | LFOs: free/sync rate, shapes | DECOMP |
| GMS `FUN_1800f32f0`, `FUN_1800f0b20`, `FUN_1800efbc0` | 4× oversampled ladder filter; cutoff/feedback table | DECOMP |
| `Synth Waves/*.wav` + `tmp/r12/waves.py` | Spectrum of the 29 shapes | DATA |
| 321 GMS presets (`tmp/r12/gms_dec.py`, `gms_all.txt`) | Versions, ranges, musical consistency | DATA |
| Ogun VMT `0x8621f8` (`tmp/r10/findself.py`) | `SaveRestoreState` `FUN_00864d00`, `ProcessParam` `FUN_008684e0` | DECOMP |
| Ogun `FUN_008698e0`, `FUN_00868c70` | Parameter index / text (ms formula for the times, "Infinite") | DECOMP |
| Ogun `FUN_008649e0` + `tmp/r12/fields.py` | Fields `+0x1F0…+0x1FC` = filter type/order/flat/richness | DECOMP+STR |
| Ogun `FUN_00867cc0` | Filter coefficients per type | DECOMP |
| Ogun/Autogun/Sakura DFM `TSynthEditorForm` (`tmp/r12/ogun.dfm.txt`) | Tags, ranges, defaults | STR |
| 258 Ogun, 13 Autogun, 15 DrumSynth, 510 Sakura, 11 Morphine | Containers, versions, distributions | DATA |

### Open questions

- GMS: exact scale of FM (phase unit) and of *Phase Offset Osc 2*/*Unison Phase*; whether the cutoff table
  is really independent of `fs`; "beats" vs "bars" semantics of the synced LFO (×1 and ×4); MIDI sources
  2 and 3 of the matrix (mod wheel/aftertouch?); meaning of indices 61–116 and 187–197.
- Ogun: format of the articulators (envelope points/LFO/maps — probably the same object as Sytrus,
  see `REA_rodada11.md`), of the timbre table and of *richness*; scale of Master level; value of field
  `+0xF3C` (cutoff scale); packed bytes 53–62.
- DrumSynth Live: format of the parameter blocks of the 120 sounds (it would be possible to synthesise without samples).
- Sakura: real scales of the 146 floats; Morphine: `Mor0` chunks.
