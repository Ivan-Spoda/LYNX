# ValhallaVintageVerb — spec dossier (LYNX substitute: Epoch Verb)

Round 125 (spec125). Clean-room dossier built from Valhalla DSP's own product page and blog posts (the plugins are
"self documenting": there is no PDF manual; the tool tips are the manual [S1][DOC]), the published reverb literature,
our black-box measurements of the official demo ([REA round 100](../analise/REA_rodada100.md)), LYNX's source and her
saved project data. No Valhalla binary was opened, decompiled or instrumented.
Tags: [DOC] vendor page/blog · [PAPER] academic · [MEAS] measurement (ours = REA round) · [DATA] read from her projects
or LYNX files · [model] our inference. All web sources accessed 2026-10-21. A short section on ValhallaDelay and
ValhallaFutureVerb (her other two Valhalla plugins) is at the end (§10).

**One-paragraph summary.** VintageVerb is Sean Costello's algorithmic reverb "inspired by the most beloved reverb
hardware from the 1970s and 1980s": 22 reverb modes (algorithms) and 3 colour modes (1970s / 1980s / Now) [S1][DOC].
Her corpus uses it heavily (708 slots / 410 projects / 37 states in the 412-project inventory; 1106 slots / 57 states
across the full copy with autosaves) [DATA], and **67 % of those slots are one factory preset family, "Detuned & Big":
mode Cathedral, colour 1980s, Mix 42 %, Decay ≈ 4.4 s, ModDepth 100 % at 0.86 Hz, BassMult 0.7× below 474 Hz, HighCut
≈ 10.6 kHz** [DATA]. Another 22 % is "Big Shimmerspace Huge" (Concert Hall, 1980s, 100 % wet). The saved state is
plain XML with the normalised VST3 values, so LYNX already translates every control into Epoch Verb (round 100) with
mid-band RT within ±8 % per mode [MEAS round 100 §7]. What is **not** reproduced yet is exactly what dominates her sound:
the Cathedral build-up shape, the 1980s colour's "dark and noisy" modulation artefacts, and the Cathedral/1980s
spectrum at her settings. §7 orders the work.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Valhalla DSP, ValhallaVintageVerb; designer Sean Costello (with Don Gunn on the Dirty modes) | [S1][S2][DOC] |
| Version she uses | 4.0.5 (every saved state: `pluginVersion="4.0.5"`) — the current version (updated 15 March 2024) | [DATA], [S1][DOC] |
| Formats | 64-bit VST2.4 / VST3 / AAX / AU; her projects use the VST3 (`ValhallaDSP\ValhallaVintageVerb.vst3`, class `7654535665657633616c68616c6c6176`) | [S1][DOC], [DATA round 98] |
| Launch / history | released 18 Dec 2012 with 8 modes; Sanctuary 2013; Dirty Hall/Plate, Smooth Plate/Room/Random, Nonlin 2014; Chaotic Hall/Chamber 2017; Chaotic Neutral 2019 (1.7.1); Cathedral Nov 2021 (2.1.5/2.2.0 — the page lists both); Palace 2023 (3.0.0 blog, "2.5.0" in the change list); Chamber1979 and Hall1984 in 4.0.0 | [S1][S2][S3][S4][DOC] |
| Hardware lineage | not an emulation of one unit. The vendor names the Lexicon 224XL and AMS RMX16 as the units whose artefacts the Dirty modes emulate ("floating point convertor quantization, steep lowpass antialiasing filters, fixed point quantization of the audio signal, and fixed point quantization of the modulation signals"); Sanctuary is "inspired by a classic German digital reverberator from the 1970s" (we assume the EMT 250 — [model], not stated); Palace by "a classic 'room simulator' algorithm from the 1980s" (unit not named); Cathedral is Costello's own 2014 algorithm for the Spin Semiconductor FV-1 chip | [S2][S3][S4][DOC] |
| Demo behaviour | the official demo mutes ~1.3–1.5 s every 45 s of processed audio (no noise injected) | [MEAS round 100 §0] |

### Her usage [DATA]

Inventory (412 main projects, `tmp/r113/inventory_all.txt`): **708 slots, 410 projects, 37 distinct states**. Whole
copy incl. autosaves (`tmp/vm124/inv.txt`): 1106 slots, 56 main slots in 29 main projects, 57 distinct states.
Every state is `pluginVersion="4.0.5"`. Weighted by slots (whole copy), decoded with the laws of §3:

| Preset name in state | slots | mode / colour | typical values (her variants) |
|---|---|---|---|
| Detuned & Big | 740 (67 %) | Cathedral / 1980s | Mix 42 % (24–64 %), PreDelay 0, Decay 4.36 s (0.53–17.9 s), Size 100 %, Attack 37.2 %, BassMult 0.70× @ 474 Hz, HighShelf 0 dB @ 9.4 kHz, Early/Late diffusion 100 %, ModRate 0.86 Hz, ModDepth 100 %, HighCut 10.6 kHz (2.4–10.6 kHz), LowCut 10 Hz (10–506 Hz) |
| Big Shimmerspace Huge | 241 (22 %) | Concert Hall / 1980s | Mix 100 % (a send), PreDelay 0, Decay 1.78–3.28 s, Size 100 %, Attack 73.6 %, BassMult 1.0× @ 474 Hz, HighShelf 0 dB @ 6 kHz, diffusions 100 %, ModRate 2.53 Hz, ModDepth 38 %, HighCut 0.6–10.5 kHz, LowCut 117–366 Hz |
| Default | 88 (8 %) | Concert Hall or Plate / 1970s (one Palace / Now) | factory defaults (Mix 100 %, PreDelay 15–34 ms, Decay 0.84–4.4 s, HighShelf −24 dB @ 6 kHz, BassMult 1.5× @ 300 Hz, HighCut 8 kHz) with small edits |
| Almost Slapback Wash | 22 | Sanctuary / Now | Mix 100 %, PreDelay 82 ms, Decay 6.05 s, Size 60 % |
| 80s Hall Reverb, Forgotten Sanctuary, Dancing Diffusion, Toms-O-Doom - EB | 15 | Concert Hall 1980s, Sanctuary Now, Chaotic Neutral 1980s, Room 1980s | — |

So ≈ 89 % of her VintageVerb sound is **two factory presets at colour 1980s**: Cathedral (inserted, Mix ≈ 42 %) and
Concert Hall (on a send, 100 % wet). She edits Mix, Decay, HighCut and LowCut per project, little else.

**Enumeration gotcha** [DATA, model]: presets store an enumeration as the float32 of k/n, which can sit a hair *below*
k/n (ReverbMode 20/24 is stored as 0.833333313…, ColorMode 2/3 as 0.6666667, ValhallaDelay Mode 7/24 as 0.29166666).
A translator must decide with thresholds slightly below k/n (or round(n·v)), never `floor(n·v)` in double precision.
LYNX's `steps` maps already do this (thresholds at 0.6662, 0.8328, …) [DATA, src/core/PluginSubstitutes.cpp]. With that
rule her dominant presets are **1980s**, not 1970s (ColorMode 0.6666667 → 1980s; 0.3333333 → 1970s; 1.0 → Now).
Round 100 measured "seventies [0, 0.6667), eighties [0.6667, 1), now {1.0}" [MEAS round 100 §1]; the stored 1/3 for the
1970s and 2/3 for the 1980s suggest a 4-step parameter whose first two steps are both 1970s [model].

---

## 2. Signal flow

```
in L/R ──┬──────────────────────────────────────────────────────────────── dry · cos(Mix·π/2) ──┐
         │                                                                                         (+)── out L/R
         └─► PreDelay (0–500 ms) ─► [colour: 1970s = internal downsampling + ~10 kHz band limit]   │
               ─► early section (mode-specific: discrete taps / input all-pass diffusers;          │
                  EarlyDiffusion; Attack shapes build-up or early/late balance)                    │
               ─► late tank (mode-specific: hall / plate / room / chamber / random / FV-1 ring…)   │
                  · modulated delays (ModRate, ModDepth; chorus / random / chaotic per mode;       │
                    1970s/1980s: "dark and noisy" modulation, Now: clean)                          │
                  · LateDiffusion (in-loop all-pass density)                                       │
                  · Decay (RT60) with BassMult × below BassXover; HighShelf/HighFreq damping       │
                  · Dirty/Sanctuary/Chaotic: fixed-point / converter / tape nonlinearity          │
               ─► output taps (stereo decorrelated, L/R corr ≈ 0) ─► HighCut, LowCut (1st order)  │
               ─► wet · sin(Mix·π/2) ───────────────────────────────────────────────────────────────┘
```

Facts behind the diagram: equal-power mix law [MEAS round 100 §0]; HighCut/LowCut are first-order (6 dB/oct) output
filters [MEAS round 100 §1]; the high shelf acts partly on the output and partly in the loop (8 kHz RT saturates near
−12 dB) [MEAS round 100 §1]; L/R correlation of the first second ≈ 0 in every mode [MEAS round 100 §1]; the 1970s
colour "replicates the reduced bandwidth of the earliest digital reverberators (10 kHz maximum output frequency).
Downsampled internally to reproduce the artifacts of running at a lower sampling rate" [S1][DOC]; Sanctuary includes
"the bit reduction and floating-point gain control used in the A/D and D/A convertors" [S1][DOC]. Where exactly the
colour stage sits (input only, in the loop, or the whole engine) is not documented — [model]: the whole engine runs
at the reduced rate in 1970s, since "running at a lower sampling rate" produces the artefacts.

---

## 3. Controls

18 parameters in VST3 order (the order of the XML attributes in her state) [MEAS round 100 §1]. Laws: normalised
v → value; "skew k" = lo + (hi − lo)·v^k (JUCE centre skew) [MEAS round 100 §1]. The state is `VC2!` + XML
`<ValhallaVintageVerb pluginVersion presetName Mix= PreDelay= …>` with normalised values [MEAS round 100 §0], so the
"state mapping" column is simply the attribute name.

| Control (attr) | Range / unit | Default | Law | Audible effect | Her Detuned & Big |
|---|---|---|---|---|---|
| Mix | 0–100 % | 100 % | linear; equal-power dry cos / wet sin | wet/dry | 42 % |
| PreDelay | 0–500 ms | 20 ms | skew 2.3219 (100 ms centre) | gap before reverb | 0 ms |
| Decay | 0.2–70 s | 4 s | skew 3.3596 (7 s centre) | RT60 (mid-band RT ≈ 1.0–1.2 × Decay for 1–8 s; floor at short decays) | 4.36 s |
| Size | 0–100 % | 100 % | linear | delay-line scale: onset, density, slight RT change | 100 % |
| Attack | 0–100 % | 50 % | linear | build-up / early-late balance; per-mode meaning (Ambience: early vs late; Nonlin: truncated → flat gate → reverse) | 37.2 % |
| BassMult | 0.25–4 × | 1.5 × | skew 2.3219 (1× centre) | RT multiplier below BassXover | 0.70 × |
| BassXover | 100–10000 Hz | 300 Hz | skew 4.044 (700 Hz centre) | bass RT crossover | 474 Hz |
| HighShelf | −24…0 dB | −24 dB | linear | high damping amount (output shelf + in-loop) | 0 dB |
| HighFreq | 100–20000 Hz | 6 kHz | skew 1.754 (6 kHz centre) | damping shelf frequency | 9.4 kHz |
| EarlyDiffusion | 0–100 % | 100 % | linear | early echo density (0 = discrete pattern) | 100 % |
| LateDiffusion | 0–100 % | 100 % | linear | tail density (0 lowers RT to ≈ 2.1 s at Decay 2) | 100 % |
| ModRate | 0.1–10 Hz | 2.53 Hz | linear | modulation speed | 0.86 Hz |
| ModDepth | 0–100 % | 38 % | linear | modulation depth (spread ≈ 7.4·(rate/2)^0.4·√depth Hz on a 1 kHz tone) | 100 % |
| HighCut | 100–20000 Hz | 8 kHz | skew 1.754 | 1st-order output low-pass | 10.6 kHz |
| LowCut | 10–1500 Hz | 10 Hz | linear | 1st-order output high-pass | 10 Hz |
| ColorMode | 1970s / 1980s / Now | 1970s | steps (§1 gotcha) | bandwidth, sampling rate, modulation artefacts | 1980s |
| ReverbMode | 22 modes | Concert Hall | k at [k/24, (k+1)/24), k = 1…22 (0 and ≥ 23 = Concert Hall) | algorithm | Cathedral (k = 19) |
| Bypass | — | — | JUCEPrivateData | — | — |

All ranges, laws, defaults [MEAS round 100 §1]. Mode order (k = 1…22): Concert Hall, Plate, Room, Chamber, Random
Space, Chorus Space, Ambience, Bright Hall, Sanctuary, Dirty Hall, Dirty Plate, Smooth Plate, Smooth Room, Smooth
Random, Nonlin, Chaotic Chamber, Chaotic Hall, Chaotic Neutral, Cathedral, Palace, Chamber1979, Hall1984
[MEAS round 100 §1]. The editor groups them: Mix/PreDelay, big Decay, DAMPING (HighFreq, HighShelf, BassFreq, BassMult),
SHAPE (Size, Attack), DIFF (Early, Late), MOD (Rate, Depth), EQ (HighCut, LowCut), MODE and COLOR menus
[MEAS round 100 §7b]. Note the editor calls BassXover "BassFreq"; 4.0.5 "restored Damping BassFreq range to original
100 Hz – 10000 Hz" [S1][DOC] — consistent with the measured law.

---

## 4. Known curves & behaviour

### 4.1 Per-mode impulse metrics (Decay 2 s, Size 100 %, PreDelay 0, cuts open, BassMult 1, HighShelf 0, Now) [MEAS round 100 §1]

| mode | onset ms | env. peak ms | RT60 0.5–2 kHz s | e100 dB |
|---|---|---|---|---|
| Concert Hall | 5.5 | 8 | 2.46 | −3.1 |
| Cathedral | 1.4 | 47 | 2.79 | −11.4 |
| Plate | 2.4 | 112 | 1.90 | −9.8 |
| Sanctuary | 1.4 | 15 | 2.08 | −9.8 |
| Palace | 55 | 65 | 3.63 (125 Hz: 5.6) | −9.1 |
| Chaotic Neutral | 4.0 | 153 | 3.50 | −18.8 |
| Room | 1.4 | 179 | 2.18 | −10.4 |

(full 22-mode table in round 100 §1). Her two main modes differ sharply: Concert Hall is an **early-loaded** hall (−3.1
dB in the first 100 ms, envelope peak at 8 ms), Cathedral a **slow-building** one (peak 47 ms, −11.4 dB early energy,
RT ≈ 1.4 × Decay at 2 s).

### 4.2 Decay, size, damping, modulation [MEAS round 100 §1]

- RT vs Decay: mid-band RT ≈ Decay × 1.0–1.2 for 1–8 s (Concert Hall 1.60 / 2.45 / 4.63 / 8.68 s at 1 / 2 / 4 / 8 s),
  with a floor at short decays (Concert Hall 1.38 s at 0.5 s). Early energy does not change with Decay.
- Size 0 % shortens the onset (3.0 ms) and slightly lowers RT (≈ 2.2 s at Decay 2).
- Attack: little effect below 75 %; at 100 % the onset moves to 50 ms.
- BassMult: RT below the crossover × mult for mult ≥ 1 (125 Hz: 4.3 s at 2×, 7.9 s at 4×); weaker below 1 (× 0.68 at
  0.25×). Her 0.70× gives a modest bass RT reduction — not measured on Cathedral.
- HighShelf/HighFreq: gentle output shelf + in-loop damping saturating near −12 dB. At HighShelf 0 dB (her presets) the
  damping section is neutral [model: from the law; not measured at 0 dB on Cathedral].
- Colour: 1970s darkens the tail (Concert Hall RT at 8 kHz 1.74 s vs 2.53 s Now; 1980s 2.18 s).
- Modulation (1 kHz tone, RMS spectral spread of the tail): 1.4 Hz at depth 0; depth 100 %: 5.8 / 14.9 / 21.2 Hz at
  0.5 / 2 / 6 Hz rate. At her 0.86 Hz / 100 % the fit gives ≈ 7.4·(0.43)^0.4 ≈ 5.4 Hz spread on Concert Hall
  [model: fit of round 100; Cathedral's "ensemble modulation" not measured separately].

### 4.3 Colour modes (vendor descriptions) [S1][S3][DOC]

- **1970s**: "dark and noisy with the potential for strange and random artifacts"; 10 kHz maximum output frequency;
  downsampled internally; "the modulation is dark and noisy and can produce strange and random sidebands with
  sustained notes. This is intentional."
- **1980s**: "Full bandwidth/sampling rate, for a brighter sound than the 1970s. The modulation is still dark and noisy
  but will produce different artifacts than the 1970s mode as it is running at the full sampling rate."
- **Now**: full bandwidth; "the modulation is clean and colorless".
- For Palace only, the vendor gives numbers: "Setting COLOR to 70s results in an 8 kHz bandwidth, 80s to 15 kHz, and
  NOW to full bandwidth" [S3][DOC]. So "full bandwidth" for 1980s in the general text is not literally true for every
  mode; per-mode bandwidths must be measured (§8).

### 4.4 The modes she uses (vendor descriptions)

- **Cathedral** — "An updated version of an FV-1 algorithm created by Sean Costello in 2014 … BIG yet clear, for long,
  open reverb decays with ensemble modulation and a realistic rolloff of high frequencies" [S2][DOC]. "The original
  FV-1 algorithm corresponds to a Size setting of 50%, while sizes greater than 50% will get closer to a real-world
  cathedral"; it has "a much more comprehensive Damping section"; the ensemble modulation is kept, with Rate and Depth
  [S4][DOC]. She runs it at Size 100 %, i.e. twice the FV-1 delay scale [model: if Size scales lengths linearly].
- **Concert Hall** — "Based on the hall algorithms of the late 1970s and early 1980s. Huge spatial image, echo density
  that can be adjusted from very sparse to very dense, and lush chorusing modulation. Fairly 'clean' tone, without
  converter coloration" [S1][S2][DOC].
- **Plate / Sanctuary / Palace** (minor shares): Plate "inspired by early 1980s plate algorithms. Highly diffuse,
  bright initial sound"; Sanctuary: discrete early reflections, rapidly building density, detuned modulation, bit
  reduction + floating-point gain control; Palace: a "room simulator" with Early Diffusion 40 % = the early-80s
  hardware, Late Diffusion 0 % = "reflection off the back of the concert hall", Attack controls early-reflection
  level [S1][S3][DOC].
- Families: Dirty = Lexicon 224XL / AMS RMX16 artefacts; Smooth = "modern update of the 'classic' Lexicon reverb
  algorithms … shaping the reverb output taps for a natural exponential decay"; Chaotic = ValhallaDelay tape-mode
  wow/flutter and "pre-emphasis / nonlinearity / de-emphasis" saturation [S2][DOC].

### 4.5 Latency, sample rate, stereo

- Latency: not measured; the impulse onsets of 1.4 ms (Room, Sanctuary, Cathedral) bound any latency to ≤ 1.4 ms at
  48 kHz [MEAS round 100 §1, model].
- Sample-rate behaviour: not measured at 44.1 kHz (her takes are 44.1 kHz, [DATA round 107 §1]); the 1970s colour's
  internal rate is not documented.
- Stereo: L/R correlation ≈ 0 for every mode [MEAS round 100 §1]; mono-in behaviour (her takes are dual-mono) not
  measured.

---

## 5. Published science

VintageVerb's code is closed; these are the published structures its family descends from and that LYNX's engine
is built on. Costello's own reading list highlights Schroeder, Moorer, Jot & Chaigne and Dattorro [S5][DOC].

1. **Schroeder (1962)** [S6][PAPER]: parallel feedback combs into series all-passes. Comb:
   `y[n] = x[n−M] + g·y[n−M]`; for a target RT60 T at sample rate fs, `g = 10^(−3·M / (fs·T))` (−60 dB after T).
   All-pass: `y[n] = −g·x[n] + x[n−M] + g·y[n−M]` (flat magnitude, smears phase → echo density).
2. **Moorer (1979)** [S7][PAPER]: low-pass in each comb loop (frequency-dependent decay), early reflections from a
   tapped delay ahead of the late reverb; the canonical "early + late" split VintageVerb's Attack/EarlyDiffusion
   expose.
3. **Jot & Chaigne (1991)** [S8][PAPER]: feedback delay network with a unitary (lossless) feedback matrix A
   (Hadamard, Householder), `s_i[n] = Σ_j A_ij·(h_j * s_j)[n − M_j] + b_i·x[n]`, and **absorption filters** per line
   chosen so every mode decays at the same frequency-dependent rate:
   `20·log10|h_i(e^{jω})| = −60·M_i / (fs·T60(ω))`, plus a tone-correction filter so the spectrum does not tilt. This is
   how BassMult/BassXover and HighShelf/HighFreq become per-band RT60s. Costello summarises the idea as giving each
   delay line its own damping filter so all lines decay at the same rate, reducing metallic resonances [S5][DOC].
4. **Dattorro (1997)** [S9][PAPER]: the plate tank "in the style of Griesinger" (Lexicon lineage): bandwidth
   low-pass → 4 input all-pass diffusers (coefficients 0.75, 0.75, 0.625, 0.625) → a figure-of-eight tank of two
   halves, each `modulated all-pass (decay diffusion 1 = 0.70) → delay → damping low-pass → ×decay → all-pass (decay
   diffusion 2 = 0.50) → delay`, cross-fed; output = sums/differences of taps from both halves (decorrelated L/R).
   It is specified at a 29.761 kHz sample rate with a slow sinusoidal excursion (≈ 16 samples peak) on the tank
   all-passes — the "chorused modulation" family. (Numbers as published in [S9]; check the paper before use.)
5. **Survey** [S10][PAPER]: Välimäki et al. classify delay-network, convolution and physical-model reverbs and discuss
   time-varying (modulated) delay lines that break up modal ringing — the purpose of VintageVerb's Mod section.
6. **Vintage colour (no paper; vendor text + model)**: the 1970s/1980s artefacts are (a) reduced internal sampling
   rate with steep anti-alias/anti-image filters (Palace: 8 / 15 kHz bandwidth [S3]); (b) "fixed point quantization of
   the modulation signals" — the delay modulation moves in coarse steps, so interpolated reads produce sidebands and
   noise ("sounds like an old modem with high amounts of modulation") [S2][DOC]; (c) for Dirty/Sanctuary only,
   converter quantisation and floating-point gain ranging [S1][S2]. [model]: (b) can be reproduced by quantising the
   LFO-driven delay offset to a fixed step (e.g. 1/16 or 1/32 sample) and updating it at a reduced control rate, with
   linear (not all-pass/Hermite) interpolation; (a) by running the tank at fs/2 (1970s) with a 10 kHz elliptic
   band limit before and after.
7. **FV-1 (Cathedral's origin)**: the Spin Semiconductor FV-1 is a fixed-program reverb DSP with very limited delay
   memory, "optimized to work with specific reverb architectures" [S4][DOC]; its standard reverb programs are
   all-pass-diffuser → all-pass/delay ring ("loop") tanks with LFO-modulated all-passes [model: from the FV-1's
   published application programs; the chip's datasheet was not consulted here — verify before relying on it].

---

## 6. What LYNX does today

- Substitute: **Epoch Verb** (`EffectKind::epochVerb`, `src/core/LynxVerbs.{h,cpp}`), matched by the VST3 class ID and
  the names; every attribute is translated (identities; `steps` maps for ColorMode and ReverbMode)
  [DATA: src/core/PluginSubstitutes.cpp `[valhalla-vintageverb]`; PLUGIN_FALLBACK.md].
- Engine (shared with Atrium, Foil Plate, Nebula, Halo, Horizon): pre-delay; 14 early taps per channel; 4 Schroeder
  input all-passes per channel (Dattorro's lengths); optional build-up stage (40 taps) and gate; an 8-line FDN (two
  4-line halves, normalised Hadamard, rotation by cross·45°), per line a modulated 4-point Hermite read, an in-loop
  all-pass, a 3-band Jot & Chaigne absorption filter and an optional loop band limit; orthogonal output taps, 6/12
  dB/oct cuts, shelves; equal-power mix [MEAS round 100 §7].
- Per-mode table (`epochModes`): shape, line scale, RT/Decay ratio and floor, bass RT ×, early level/span, build-up,
  modulation scale, gain, own pre-delay, gate — fitted to round 100's captures. Cathedral row: shape 0 (hall), scale
  1.40, RT/Decay 1.30, early 0.10 over 40 ms, build 20 ms, own pre-delay 0.5 ms [DATA: LynxVerbs.cpp].
- Colour: 1970s = loop band limit 9 kHz + output high cut 10 kHz; 1980s = 14 kHz loop + 16 kHz output; Now = none
  [DATA: `epochColourBw`, `epochColourCut`]. No reduced internal rate, no modulation quantisation, no converter
  artefacts.
- Measured residuals (Epoch vs VintageVerb, 22 modes, Decay 2 s) [MEAS round 100 §7]: mid-band RT within ±8 % on every
  mode; onsets within ~1 ms except Random Space (+9 ms); e100 within ±2 dB on 19 of 22; RT vs Decay 0.5–8 s within
  ±10 % on six checked modes; **not reproduced**: build-up shape of the slow-attack modes (envelope peak up to 30 %
  early), Nonlin's gate envelope, each mode's exact early pattern, the colour modes' character beyond a band limit.
- Not yet checked: Epoch at **her** settings (Cathedral 1980s, Decay 4.4 s, ModDepth 100 %, HighCut 10.6 kHz, Mix 42 %)
  against the demo; Cathedral's per-octave RT (only mid-band was fitted); 44.1 kHz.

---

## 7. Implementation guidance for LYNX (ordered)

1. **Lock the translation of her two presets.** Unit-test that `Detuned & Big` decodes to Cathedral / 1980s and
   `Big Shimmerspace Huge` to Concert Hall / 1980s (float32 k/n values from §1), and that every one of her 57 states
   round-trips (`tmp/vm124/st/ValhallaVintageVerb_*.bin`). Pass: identical mode/colour indices to the rule
   "threshold slightly below k/n".
2. **Capture the reference at her settings** (official demo, fxmeasure, 44.1 and 48 kHz; respect the 45 s mute
   window as in round 100): (a) unit impulse, 12 s; (b) her real vocal take through the preset; (c) 1 kHz and 220 Hz
   sustained sines 8 s (modulation sidebands); (d) pink noise burst 500 ms; for both presets and for Cathedral at
   Decay 1.85 / 2.9 / 4.36 / 7.37 s, HighCut 4.9 / 10.6 kHz (her variants).
3. **Fit Cathedral first** (67 % of slots): per-octave RT60 (125 Hz–8 kHz) from T20; EDC shape over the first 300 ms
   (the slow build: peak 47 ms at Decay 2 s); e100. If the hall-shaped FDN cannot produce the build-up, add a
   Cathedral-specific front end modelled on the FV-1 "ring" architecture (§5.7): 4 input all-passes, then a loop of
   4 × (all-pass + delay) with LFO-modulated all-passes and one damping LP per segment, scaled ×2 for Size 100 %.
   Pass: RT60 per octave within ±5 %, EDC difference ≤ 1 dB over 0–300 ms, onset ±1 ms, e100 ±1 dB.
4. **Model the 1980s colour beyond a band limit**: measure the tail spectrum above 10 kHz at 1980s vs Now (is it
   15 kHz like Palace? [S3]); measure the modulation sideband spectrum on the 1 kHz sine at ModDepth 100 % / 0.86 Hz in
   1980s vs Now. Implement "noisy" modulation as quantised delay offsets updated at a reduced control rate (§5.6) and
   tune step size/update rate until the sideband floor (−40…−80 dB around the tone) matches within 3 dB.
5. **Concert Hall at the Shimmerspace settings** (22 %): same captures, Attack 73.6 %, ModRate 2.53 / 38 %, LowCut
   117–366 Hz. Pass as in 3.
6. **Level and mix**: with Mix 42 % (equal-power: dry 0.791, wet 0.613 [model from cos/sin law]) the wet level relative
   to dry decides how big the reverb sounds. Check Epoch's wet gain against the demo on pink noise: ≤ 0.5 dB RMS
   difference of the wet path. Also confirm her FL slot mix knob (wrapper dry/wet) is applied identically.
7. **Her takes as the final test**: render her vocal take through demo vs Epoch at her exact state; pass: octave-band
   long-term spectrum within 0.5 dB RMS (63 Hz–16 kHz), short-term (100 ms) loudness envelope correlation ≥ 0.98,
   and a blind A/B by ear on the sustained notes (modulation colour).
8. **1970s colour** (8 % of slots: the Default preset family): run the tank at fs/2 with ~10 kHz band limits; verify
   8 kHz RT (1.74 s on Concert Hall at Decay 2) and the "random sidebands" on sustained sines.

---

## 8. Open questions to measure (official demo, black-box only)

- [ ] **Cathedral per-octave RT60 and EDC** at Decay 4.36 s, Size 100 %, Attack 37.2 %, Now/1980s/1970s — impulse.
      Decides the Cathedral loop structure and damping.
- [ ] **Size law on Cathedral** (0 / 50 / 100 %): onset, peak, RT — impulse. Tests "50 % = FV-1 original" scaling.
- [ ] **1980s bandwidth per mode** (Cathedral, Concert Hall): white-noise burst; tail spectrum 8–20 kHz. Decides the
      loop/output band limits (Palace says 15 kHz [S3]).
- [ ] **Modulation artefacts per colour**: 1 kHz and 220 Hz sines, 8 s, ModDepth 100 % at 0.86 Hz and 38 % at 2.53 Hz;
      high-resolution spectrum of the tail (sidebands, noise floor). Decides the "noisy modulation" model.
- [ ] **Internal rate of the 1970s colour**: a swept sine 5–20 kHz into 1970s; look for aliasing images / the cut slope.
- [ ] **BassMult 0.70 × @ 474 Hz on Cathedral**: 125/250 Hz RT — impulse.
- [ ] **Attack on Cathedral** (0 / 37 / 75 / 100 %): build-up envelope — impulse.
- [ ] **Mono-in / dual-mono**: her takes are dual-mono 44.1 kHz; impulse in L only vs L+R — decides input summing.
- [ ] **Level**: pink noise at −20 dBFS RMS, Mix 100 %, both presets — wet gain.
- [ ] **ColorMode value 0 vs 1/3**: both read as 1970s in round 100; confirm by spectrum (decides whether a 4th hidden
      colour exists).
- [ ] **44.1 vs 48 kHz** invariance of RT and bandwidth — impulse at both rates.

---

## 9. Sources

1. Valhalla DSP, "Valhalla VintageVerb" product page (modes, colours, version history) —
   https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/ (read via the web.archive.org 2025 snapshot) — accessed
   2026-10-21 — [DOC]
2. S. Costello, "Valhalla VintageVerb: The MODES" (2023-02-10, upd. 2025-05-09) —
   https://valhalladsp.com/2023/02/10/valhallavintageverb-the-modes/ — accessed 2026-10-21 — [DOC]
3. S. Costello, "ValhallaVintageVerb Updated to 3.0.0. New Reverb Mode: Palace!" (2023-01-25) —
   https://valhalladsp.com/2023/01/25/valhallavintageverb-updated-to-3-0-0-new-reverb-mode-palace/ — accessed
   2026-10-21 — [DOC]
4. S. Costello, "Valhalla VintageVerb Updated to 2.2.0: Cathedral Mode!" (2021-11-23) —
   https://valhalladsp.com/2021/11/23/valhallavintageverb-updated-to-2-2-0-cathedral-mode/ — accessed 2026-10-21 — [DOC]
5. S. Costello, "Getting Started With Reverb Design, Part 2: The Best Papers" (2021-09-22) —
   https://valhalladsp.com/2021/09/22/getting-started-with-reverb-design-part-2-the-foundations/ — accessed
   2026-10-21 — [DOC] (content seen through search excerpt)
6. M. R. Schroeder, "Natural sounding artificial reverberation", JAES 10(3), 219–223, 1962 —
   https://aes2.org/publications/elibrary-page/?id=849 — accessed 2026-10-21 — [PAPER]
7. J. A. Moorer, "About This Reverberation Business", Computer Music Journal 3(2), 13–28, 1979 —
   http://www.jstor.org/stable/3680280 — accessed 2026-10-21 — [PAPER]
8. J.-M. Jot, A. Chaigne, "Digital Delay Networks for Designing Artificial Reverberators", AES 90th Convention,
   preprint 3030, 1991 — https://aes2.org/publications/elibrary-page/?id=5663 — accessed 2026-10-21 — [PAPER]
9. J. Dattorro, "Effect Design Part 1: Reverberator and Other Filters", JAES 45(9), 660–684, 1997 —
   https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf — accessed 2026-10-21 — [PAPER]
10. V. Välimäki, J. Parker, L. Savioja, J. O. Smith, J. Abel, "Fifty Years of Artificial Reverberation", IEEE TASLP
    20(5), 1421–1448, 2012 — https://www.academia.edu/82609908 — accessed 2026-10-21 — [PAPER]
11. LYNX REA round 100 (Valhalla reverbs black-box; Epoch Verb fit) — [../analise/REA_rodada100.md](../analise/REA_rodada100.md) — [MEAS]
12. LYNX REA round 101 (ValhallaDelay laws, Echoform) — [../analise/REA_rodada101.md](../analise/REA_rodada101.md) — [MEAS]
13. LYNX REA round 98 (FL wrapper state formats, class IDs) — [../analise/REA_rodada98.md](../analise/REA_rodada98.md) — [DATA]
14. LYNX source: `src/core/LynxVerbs.cpp` (Epoch Verb), `src/core/PluginSubstitutes.cpp` (translation),
    [../PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md) — [DATA]
15. Her corpus: `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`, `tmp/vm124/inv.txt`, `tmp/vm124/st/*.bin` — [DATA]

---

## 10. Her other Valhalla plugins (short)

### 10.1 ValhallaDelay 3.0.5 (48 slots, 1 main project "eu", 4 states) → Echoform (round 101)

- State: same JUCE XML scheme, 39 attributes (Mix, DelayStyle, DelayL/R Sync/Note/Ms, Spread, Spacing, Ratio,
  RepeatSwell, TapA–D, Feedback, Width, DriveIn, Age, Diffusion, DiffSize, LowCut, HighCut, ModRate, ModDepth, Wow,
  Flutter, FreqShift, FreqDetune, PitchShift, PitchDetune, Mode, Era, Ducking) [MEAS round 101 §1].
- Laws [MEAS round 101 §2]: Delay ms piecewise linear through (0,0), (0.5,500), (0.76,1000), (0.82,1500), (0.88,3000),
  (0.94,6000), (1,20000) ms; Feedback 0–200 % linear; PitchShift ±24 st linear; Mode = 17 modes at k/24 (1 Tape, 2 HiFi,
  3 BBD, 4 Digital, 5 Ghost, 6 Pitch, 7 RevPitch, 8 BBDuck, 9 Clarity, 10 DuckTape, 11 PitchDuck, 12 LoFi, 13 Quartz,
  14 PhaserDDL, 15 RichPitch, 16 ChromeTape, 17 Analog); Era Past / Present (≥ 0.67) / Future (1.0).
- Her states [DATA]: **"PITCH-Amethyst" in 45 of 48 slots**: Mix 100 % (send), Style Ratio (0.432 → index 2),
  L sync Note 1/1, Feedback 95 % (0.475 × 200 %), PitchShift +7 st (0.6458), PitchDetune ≈ −1.6 c, HighCut ≈ 10 kHz,
  Era Past, Mode stored 0.29166666 = float32(7/24) → **RevPitch** under the §1 rule (LYNX's threshold 0.2912 maps it
  to RevPitch); others: "Default" (2 slots), "The Other Side" (1).
- LYNX: Echoform translates every setting except Era / taps / repeat-swell [DATA PLUGIN_FALLBACK.md]. Open: measure
  RevPitch at +7 st / 95 % feedback against Echoform (reverse-grain length, pitch accuracy, feedback colour).

### 10.2 ValhallaFutureVerb 1.0.x (6 slots, 1 main project "BEAT", 1 state) → Horizon (round 100)

- Her state [DATA]: `pluginVersion="1.0.0v8"`, preset "Mix Bus-Add Space": Mix 26 %, EchoLevel 0 (echo off), Routing
  Echo→Reverb, ReverbDecay 0.0208 → ≈ 0.4 s (piecewise law 0.2→5 s on [0, 0.5]), Size 21 %, Attack 50 %, EarlyLate
  18.7 %, Density 100 %, ReverbMode 1/24 = Room, Color 1/6 (4/24) = Neutral, LowEQ / HighEQ 0.16 / 0.37 [laws in
  MEAS round 100 §6]. Note: round 100 measured version 1.0.2; her state says 1.0.0v8.
- LYNX: Horizon translates every parameter [DATA PLUGIN_FALLBACK.md]. Minor role (6 slots); measure the short Room at
  Decay ≈ 0.4 s only if her "BEAT" project is a priority.
