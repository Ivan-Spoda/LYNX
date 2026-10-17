# Round 100 — Valhalla DSP reverbs, black-box, and LYNX's own reverbs fitted to them

Rules (as round 99, `MEASURE_fabfilter.md`): the OFFICIAL demo versions the user downloaded from the vendor's site
(VST3, `C:\Program Files\Common Files\VST3\Valhalla*.vst3`) were only hosted the way a DAW hosts them — VST3
parameter API (names, value ↔ text through set/get), state through `getStateInformation`, test signals in, audio
out — with `tools/fxmeasure`. No disassembly / decompilation / string dumps of the binaries, nothing bypassed, no
presets, graphics or manual text copied. Numbers are our own measurements [EXEC]; formulas are fitted models [fit];
anything not measured is "not measured" / "not confirmed". Raw captures and scripts stay in `tmp/valh100` (never
in git). The LYNX engines are our own code built from published reverb literature (Schroeder / Moorer all-pass and
comb structures, Jot & Chaigne's feedback delay networks with absorption filters, Dattorro's input diffusers, the
two-head delay-line pitch shifter), `src/core/LynxVerbs.{h,cpp}`.

Plugins (VST3, 48 kHz, block 512): ValhallaVintageVerb 4.0.5, ValhallaRoom 2.0.5, ValhallaPlate 1.6.3, Valhalla
Supermassive 5.0.0 (free, not a demo), ValhallaShimmer 1.2.2, ValhallaFutureVerb 1.0.2.

## 0. Harness, demo limitation, state layout

- `fxmeasure`: JUCE-based plugins that report their main buses disabled until the host asks for a layout were
  prepared with 0 channels (VintageVerb, Room, Plate, Shimmer). `prepare()` now asks for stereo in / stereo out
  (mono in / stereo out as fallback).
- These plugins answer `getText (value)` with the CURRENT value's text whatever value is asked, so every law below
  was read by `set` + reported text at 41 / 201 points (enumerations: 241) [EXEC].
- **Demo limitation** [EXEC]: the demos mute their output for about 1.3–1.5 s once every 45 s of *processed* audio,
  counted from prepare (not wall clock: 200 s of audio took 1.8 s): [45k − 1.45, 45k] s for VintageVerb, Room, Plate,
  Shimmer; [45k − 4.3, 45k − 3.0] s for FutureVerb. No noise is injected (silence in → silence out). Work-around:
  the Python driver tracks the processed time and pads with silence so every capture lies inside
  [45k + 0.05, 45k + 40.5] s; captures are ≤ 38 s. Supermassive (free) has no such mute.
- **State** [EXEC]: `getStateInformation` = JUCE's `VST3PluginState` XML whose `IComponent` (JUCE base-64) is the
  plugin's component state: `VstW` header, `CcnK`/`FBCh` chunk header with a 4-character plugin code (`vee3`,
  `Ruum`, `plat`, `sMas`, `shmr`, `Hero`), then `VC2!` + size + UTF-8 XML with one element whose attributes are the
  **normalised VST3 values** of every parameter in parameter-list order, plus `pluginVersion`, `presetName`,
  `mixLock`, `uiWidth/uiHeight` (Room adds `earlyLateTabState`), then `JUCEPrivateData` with Bypass. Element names:
  `ValhallaVintageVerb`, `ValhallaRoom`, `ValhallaPlate`, `ValhallaSupermassive`, `ValhallaFutureVerb`; Shimmer uses
  `MYPLUGINSETTINGS` with `currentProgram` and bare `parameter0…11` in list order. This is the layout FL's wrapper
  records keep (round 98), so LYNX can read every value.
- **Mix law** (all six) [EXEC]: equal-power — dry = cos(m·π/2), wet = sin(m·π/2) (Mix 25 %: dry 0.922–0.924, wet
  0.384; 50 %: 0.707 / 0.709; 75 %: 0.383 / 0.926).

Analysis: impulse 0.5 (6–20 s), RT60 per octave from the Schroeder curve fitted between −5 and −25 dB (T20) on
octave bands (FFT masks), onset = first sample above −40 dB of the peak, "peak" = time of the energy-envelope
maximum (5 ms window), e100 = energy of the first 100 ms relative to a unit impulse.

## 1. ValhallaVintageVerb 4.0.5

Parameters (18, VST3 order): Mix, PreDelay, Decay, Size, Attack, BassMult, BassXover, HighShelf, HighFreq,
EarlyDiffusion, LateDiffusion, ModRate, ModDepth, HighCut, LowCut, ColorMode, ReverbMode, Bypass.

Laws [EXEC] (normalised v → unit; the skewed ones are JUCE ranges with a centre value: k = ln 0.5 / ln((centre −
lo)/(hi − lo))): PreDelay 0–500 ms, v^2.3219 (centre 100 ms); Decay 0.2–70 s, v^3.3596 (centre 7 s); Size, Attack,
diffusions, ModDepth, Mix 0–100 % linear; BassMult 0.25–4 X v^2.3219 (centre 1); BassXover 100–10000 Hz v^4.044
(centre 700); HighShelf −24…0 dB linear; HighFreq and HighCut 100–20000 Hz v^1.754 (centre 6000); ModRate 0.1–10 Hz
linear; LowCut 10–1500 Hz linear. ColorMode: seventies [0, 0.6667), eighties [0.6667, 1), now {1.0}. ReverbMode: 22
algorithms at v ∈ [k/24, (k+1)/24), k = 1…22 (k = 0 and k ≥ 23 also Concert Hall): Concert Hall, Plate, Room,
Chamber, Random Space, Chorus Space, Ambience, Bright Hall, Sanctuary, Dirty Hall, Dirty Plate, Smooth Plate,
Smooth Room, Smooth Random, Nonlin, Chaotic Chamber, Chaotic Hall, Chaotic Neutral, Cathedral, Palace,
Chamber1979, Hall1984. Defaults: Mix 100 %, PreDelay 20 ms, Decay 4 s, Size 100 %, Attack 50 %, BassMult 1.5 X,
BassXover 300 Hz, HighShelf −24 dB, HighFreq 6 kHz, diffusions 100 %, ModRate 2.53 Hz, ModDepth 38 %, HighCut 8 kHz,
LowCut 10 Hz, seventies, Concert Hall.

Per mode (Decay 2 s, Size 100 %, PreDelay 0, HighCut 20 kHz, LowCut 10 Hz, BassMult 1, HighShelf 0, colour "now")
[EXEC]: onset (ms), envelope peak (ms), RT60 500 Hz–2 kHz (s), e100 (dB):

| mode | onset | peak | RT | e100 | | mode | onset | peak | RT | e100 |
|---|---|---|---|---|---|---|---|---|---|---|
| Concert Hall | 5.5 | 8 | 2.46 | −3.1 | | Smooth Plate | 4.7 | 7 | 2.42 | −10.1 |
| Plate | 2.4 | 112 | 1.90 | −9.8 | | Smooth Room | 9.1 | 12 | 2.01 | −9.7 |
| Room | 1.4 | 179 | 2.18 | −10.4 | | Smooth Random | 7.7 | 10 | 2.21 | −8.9 |
| Chamber | 1.6 | 174 | 2.13 | −11.6 | | Nonlin | 1.7 | 684 | (gated) | −11.9 |
| Random Space | 51 | 311 | 2.19 | −23.2 | | Chaotic Chamber | 4.7 | 7 | 2.22 | −10.2 |
| Chorus Space | 10.2 | 334 | 2.20 | −22.4 | | Chaotic Hall | 5.5 | 8 | 2.50 | −9.1 |
| Ambience | 7.8 | 48 | 2.04 | −6.2 | | Chaotic Neutral | 4.0 | 153 | 3.50 | −18.8 |
| Bright Hall | 6.0 | 8.5 | 2.61 | −3.1 | | Cathedral | 1.4 | 47 | 2.79 | −11.4 |
| Sanctuary | 1.4 | 15 | 2.08 | −9.8 | | Palace | 55 | 65 | 3.63 (125 Hz: 5.6) | −9.1 |
| Dirty Hall | 5.5 | 8 | 2.49 | −3.9 | | Chamber1979 | 11.4 | 123 | 2.45 | −8.0 |
| Dirty Plate | 1.5 | 75 | 1.86 | −5.2 | | Hall1984 | 5.5 | 8 | 2.45 | −3.5 |

- RT60 vs Decay [EXEC]: mid-band RT ≈ Decay × 1.0–1.2 for 1–8 s (Concert Hall 1.60/2.45/4.63/8.68 s at 1/2/4/8 s;
  Plate 1.06/1.91/3.56/6.97; Room 1.08/2.12/4.14/8.12); a floor at short decays (Concert Hall 1.38 s at 0.5 s,
  Chorus Space 1.23, Palace 2.57). The early energy does not change with Decay (e100 constant per mode).
- Colour: seventies darkens the tail (Concert Hall RT at 8 kHz 1.74 s vs 2.53 s "now"; eighties 2.18 s); several
  modes are also darker in their spectrum.
- Size: shorter onset (3.0 ms at 0 %) and slightly lower RT (≈ 2.2 s at 0 %). Attack: little effect below 75 %; at
  100 % the onset moves to 50 ms. BassMult: RT below the crossover × mult for mult ≥ 1 (125 Hz: 4.3 s at 2 X, 7.9 s
  at 4 X), weaker below 1 (×0.68 at 0.25 X). HighShelf/HighFreq: a gentle shelf in the output plus some in-loop
  damping that saturates near −12 dB (8 kHz RT 1.86–2.01 s for −24…−6 dB at 6 kHz; 1.46 s at 1 kHz / −12 dB).
  HighCut and LowCut: first-order (6 dB/oct) output filters. LateDiffusion 0 lowers RT to ≈ 2.1 s; EarlyDiffusion 0
  shows the discrete early pattern (onset 21.5 ms by the −40 dB rule). Stereo: L/R correlation of the first second
  ≈ 0 for every mode.
- Modulation (1 kHz tone, tail spectrum RMS spread around 1 kHz) [EXEC]: 1.4 Hz with depth 0; depth 100 %: 5.8 Hz
  at 0.5 Hz rate, 14.9 Hz at 2 Hz, 21.2 Hz at 6 Hz; depth 50 %: 4.3 / 8.9 / 17.1 Hz [fit: spread ≈ 7.4·(rate/2)^0.4
  ·√depth Hz].

## 2. ValhallaRoom 2.0.5

Parameters (23): mix, predelay, decay, HiCut, earlyLateMix, lateSize, lateCross, lateModRate, lateModDepth,
RTBassMultiply, RTXover, RTHighMultiply, RTHighXover, earlySize, earlyCross, earlyModRate, earlyModDepth, earlySend,
diffusion, type, space, LoCut, Bypass. All laws linear [EXEC]: predelay 0–500 ms, decay 0.1–100 s, HiCut 100–15000 Hz,
mod rates 0.05–5 Hz, RTBassMultiply 0.5–2 X, RTXover 100–10000 Hz, RTHighMultiply 0.1–1 X, RTHighXover 100–15000
Hz, earlySize 1–1000 ms, LoCut 0–1000 Hz, the others 0–1 or %. type: Large Room [0, 2/12), then Medium Room, Bright
Room, Large Chamber, Dark Room, Dark Chamber, Dark Space, Nostromo, Narcissus, Sulaco, LV-426 at k/12 (k = 2…11),
Dense Room at 1.0. Defaults: decay 2 s, predelay 10 ms, HiCut 8 kHz, early/late 50 %, late size 0.5, cross 1.0,
RTBassMultiply 1 X @ 1 kHz, RTHighMultiply 0.5 X @ 8 kHz, earlySize 30 ms.

[EXEC] (decay 2 s, HiCut 15 kHz): **RT60 = decay** in every band up to 4 kHz (Large Room 0.48/1.0/2.0/4.1/7.7 s for
0.5/1/2/4/8 s); 8 kHz follows RTHighMultiply (1.48 s at 0.5 X; 1.82 s at 1 X; 1.12 s at 0.1 X). RTBassMultiply 0.5 /
2.0 → 125 Hz RT 0.99 / 4.06 s. Onset 1.4 ms, envelope peak 13–17 ms (early pattern); earlyLateMix 0 = early only
(dies within ~100 ms), 100 = late only (onset 30 ms); earlySize sets the early pattern's span (peak 6 / 33 / 36 ms at
10 / 100 / 300 ms). lateCross barely changes RT. Types 5–11 (Dark Chamber … Dense Room) are band-limited (≈ −60 dB
at 16 kHz). HiCut is second order; LoCut is gentle (−8 dB at 63 Hz for 200 Hz). space raises the level and slows the
build-up (+3.6 dB, peak 35 ms at 100 %).

## 3. ValhallaPlate 1.6.3

Parameters (13): Mix, PreDelay, Decay, Size, Width, ModRate, ModDepth, LowEQFreq, LowEQGain, HighEQFreq, HighEQGain,
Type, Bypass. Laws [EXEC]: PreDelay as VintageVerb; Decay 0.5–30 s piecewise (linear 0.5→3 s on [0, 0.5], 3→5.9 s on
[0.5, 0.75], then knots 6.7, 8.0, 9.4, 11.3, 13.1, 15.7, 18.4, 21.7, 25.8, 30 s every 0.025); Size, Width 0–200 %;
ModRate 0.05–5 Hz v^2.381 (centre 1 Hz); LowEQFreq 20–2000 Hz, HighEQFreq 200–20000 Hz linear; gains ±12 dB. Type:
Chrome [0, 2/24), Steel, Cobalt, Brass, Copper, Aluminum, Unobtanium, Osmium, Adamantium, Titanium, Radium, Lithium at
k/24 (k = 2…12), Chrome again from 13/24.

[EXEC] (Chrome): RT at low/mid = Decay; a constant high-frequency absorption — 1/RT(f) = 1/Decay + c(f), c ≈ 0.345
(8 kHz), 0.18 (4 kHz), 0.066 (2 kHz), 0.02 (1 kHz) s⁻¹ (RT 8 kHz 1.19 s at Decay 2 s, 2.44 s at 16 s). Band-limited
(≈ −70 dB at 16 kHz). Onset per type: 21 ms (Chrome), 1.5 (Steel), 56 (Cobalt), 18 (Brass), 7 (Copper), 19
(Aluminum), 7 (Unobtanium), 82 (Osmium), 22 (Adamantium), 56 (Titanium), 21 (Radium). Width 0 % = mono (corr 1.00),
200 % = anti-correlated (−0.21); Size 0 → onset 12 ms, 200 % → 31 ms.

## 4. Valhalla Supermassive 5.0.0

Parameters (19): Mix, DelaySync, DelayNote, Delay_Ms, DelayWarp, Clear, Feedback, Density, Width, LowCut, HighCut,
ModRate, ModDepth, Mode, Reserved1–4, Bypass. Laws [EXEC]: Delay_Ms 0–2000 ms piecewise (linear 0→300 ms on
[0, 0.5], 300→588.5 on [0.5, 0.75], 675 ms at 0.775 then +125 ms per 0.025 to 1425 at 0.925, 1583.3, 1791.7, 2000);
DelaySync Msec [0, 0.5), Note, Dotted [0.75, 1), Triplet {1}; DelayNote 1/64 … 4/4 (7 values, breakpoints 0.1458,
0.2875, 0.4292, 0.575, 0.7167, 0.8583); Width −100…+100 %; LowCut 10–2000 Hz, HighCut 200–20000 Hz linear; ModRate
0.01–10 Hz v^2.328 (centre 2 Hz). Mode: Gemini, Hydra, Centaurus, Sagittarius, Great Annihilator, Andromeda, Lyra,
Capricorn, Triangulum, Large Magellanic Cloud, Cirrus Major, Cirrus Minor, Cassiopeia, Orion, Aquarius, Pisces,
Scorpio, Libra, Leo, Virgo, Pleiades, Sirius at k/24 (k = 1…22).

[EXEC] (Delay 100 ms, Feedback 50 %, ModDepth 0): **gain per pass of the delay = √Feedback** (RT 2.12 / 4.08 / 9.66 /
25.5 s for 25 / 50 / 75 / 90 % at 200 ms). Per mode, at Density 0 / 100 %: onset multiple of the delay, RT
(s), L/R correlation:

| mode | ×d (0 / 100) | RT 0 | RT 100 | corr 100 | | mode | ×d | RT 0 | RT 100 | corr 100 |
|---|---|---|---|---|---|---|---|---|---|---|
| Gemini | 1 / 1 | 2.05 | 2.51 | 0.93 | | Cirrus Minor | 1 / 1 | 2.00 | 2.01 | 0.47 |
| Hydra | 1 / 2 | 6.82 | 7.60 | −0.31 | | Cassiopeia | 4 / 1 | 4.29 | 32.4 | 1.00 |
| Centaurus | 1 / 2 | 2.64 | 2.77 | −0.25 | | Orion | 8 / 1 | (∞) | 41.2 | −0.30 |
| Sagittarius | 1 / 1 | 5.09 | 5.32 | 0.25 | | Aquarius | 1 / 1 | 2.00 | 2.22 | 0.54 |
| Great Annihilator | 1 / 2 | 7.24 | 7.91 | −0.21 | | Pisces | 1 / 1 | 2.05 | 2.59 | 0.37 |
| Andromeda | 2 / 2 | 14.0 | 13.5 | 0.29 | | Scorpio | 1 / 1 | 2.00 | 2.09 | 0.95 |
| Lyra | 1 / 1 | 2.05 | 2.32 | 0.77 | | Libra | 4 / 1 | 4.17 | 19.2 | 0.00 |
| Capricorn | 1 / 1 | 2.05 | 2.43 | 0.87 | | Leo | 8 / 1 | 8.26 | 25.2 | 0.31 |
| Triangulum | 8 / 1 | 8.53 | 25.5 | 0.39 | | Virgo | 1 / 2 | 2.00 | 2.68 | −0.43 |
| Large Magellanic Cloud | 4 / 1 | 4.26 | 24.5 | 0.63 | | Pleiades | 1 / 1 | 2.02 | 2.01 | −0.10 |
| Cirrus Major | 2 / 1 | 4.01 | 10.0 | 0.72 | | Sirius | 1 / 1 | 2.31 | 2.07 | 0.84 |

Warp (Gemini, Density 100 %): onset 100 / 95 / 51 / 17 / 1.4 ms and RT 2.51 / 1.97 / 1.48 / 0.95 / 0.63 s for 0 / 25
/ 50 / 75 / 100 % — warp shortens the delay lines [fit: smoothstep]. Density 0 with Warp 0 is a plain echo. LowCut /
HighCut are output filters (RT unchanged).

## 5. ValhallaShimmer 1.2.2

Parameters (14): wetDry, shift, feedback, diffusion, size, lowCut, highCut, modRate, modDepth, reverbMode, shiftMode,
colorMode, Bypass, Program (8 programs). The plugin reports bare 0..1 values (no units): laws from audio [EXEC]:
- shift: −12 + 24·v semitones (tail components of a 1 kHz tone at 507, 597, 707, 839, 1000, 1177, 1422, 1703, 2007
  Hz for v = 0 … 1 in steps of 0.125).
- size sets the decay (feedback 0): RT 1.5 / 4.6 / 8.0 / 11.1 / 13.9 s for 0 / 0.25 / 0.5 / 0.75 / 1 [fit ≈ 1.5 +
  12.5·size s]; feedback adds the shifted recirculation (RT 15.6 s at 0.75, 18 s at 0.9, size 0.5).
- highCut: 100 + 14900·v Hz first order (v = 0: −12 dB at 4 kHz re 1 kHz; v = 0.25 → −3 dB at 4 kHz; the default
  0.5302 = 8 kHz as in ValhallaRoom). lowCut ≈ 0–1000 Hz (not confirmed). modRate / modDepth laws not confirmed.
- reverbMode: three groups [0, 0.5], (0.5, 0.775), [0.775, 1] (energy / RT changes); colorMode: two, split at 0.5
  (the upper one ≈ 4 dB darker); shiftMode (shift +12, feedback 0.6): [0, 0.2] up, [0.3, 0.4] up + a lower voice,
  [0.5, 0.6] up (brighter), [0.7, 0.8] up + lower, [0.9, 1] no shift. Exact breakpoints not confirmed.
- diffusion 0 gives no output for the first ~1.1 s (not understood; not reproduced).

## 6. ValhallaFutureVerb 1.0.2

Parameters (33): Mix, Width, EchoSync, EchoNote, Echo_Ms, EchoFeedback, EchoSpread, EchoDrive, EchoDetune, EchoLevel,
Routing, ReverbDecay, ReverbSize, ReverbAttack, ReverbEarlyLate, ReverbDensity, ReverbLevel, ModRate, ModDepth,
LowEQFreq, HighEQFreq, EchoMode, ReverbMode, Color, Reserved1–8, Bypass. Laws [EXEC]: ReverbDecay 0.2–70 s piecewise
(linear 0.2→5 s on [0, 0.5], 5→9.81 on [0.5, 0.75], then 12.5, 16.67, 21.25, 27.5, 33.75, 41.67, 50, 57.5, 63.75, 70);
Echo_Ms 0–2000 ms piecewise (0→100 ms on [0, 0.5], to 197.2 at 0.675, then 250, 312.5, 375, 456.2, 550, 643.8, 762.5,
918.8, 1075, 1250, 1500, 1750, 2000 every 0.025 from 0.7); EchoDrive 0–24 dB, EchoDetune ±30 cents linear; ModRate
0.01–10 Hz v^2.328; LowEQFreq 20–1000 Hz v^1.807 (centre 300); HighEQFreq 1000–20000 Hz v^1.078 (centre 10 kHz).
EchoMode: Modern, Tape, Digital, Analog, Detune, Reverse, RevOctUp, RevOctDown, RevOctUpDown, Sparkle, Swarm, LoFi at
k/24 (k = 1…11; LoFi [0.5, 0.5417)); ReverbMode: Room, Chamber, Plate, Hall, Cathedral, Space, Frozen, Nonlin at k/24;
Color: Bright [0, 4/24), Neutral, Dark, Studio from 8/24; Routing Echo→Reverb / Reverb→Echo split at 0.5.

[EXEC] (EchoLevel 0, Decay 2 s, EQ open): onsets 40–80 ms (an own pre-delay), RT ≈ 0.8 × Decay (Room, Chamber), ≈ 1.0
× (Plate, Hall), Frozen 6.6 s at Decay 2 s (slow swell: peak 1.85 s); Color Dark ≈ −54 dB at 16 kHz, Studio removes
the lows (−37 dB at 63 Hz) and highs; HighEQFreq 3 kHz also lowers the high-band RT (output and loop).

## 7. LYNX engines (core/LynxVerbs.h)

One engine, six front-ends (EffectKind `epochVerb`, `atrium`, `foilPlate`, `nebula`, `halo`, `horizon`, appended
at the end of the enum):
- input: pre-delay, early-reflection taps (14 per channel, fixed pseudo-random pattern, exponential decay, optional
  two smearing all-passes), four Schroeder all-pass input diffusers per channel (Dattorro's lengths), an optional
  build-up stage (40 taps spread over the build time, rising then falling) and a gated mode (dense taps, flat).
- late tank: 8-line FDN, two halves of four (left- and right-fed) mixed by a normalised 4-point Hadamard and rotated
  into each other by cross × 45° (orthogonal for every setting); per line a modulated 4-point-Hermite read, an
  in-loop all-pass (diffusion), a three-band absorption filter (gains from the wanted RT60 at low / mid / high,
  Jot & Chaigne) and an optional loop band limit; line sets for halls, rooms, plates and a delay network whose lines
  shrink from the delay time with warp.
- shimmer: two-head crossfaded pitch shifter (plus an optional second voice) in the feedback, soft-limited.
- output: two non-zero-sum orthogonal tap patterns (keeps the lows), width (M/S), 6 or 12 dB/oct cuts, a 24 dB/oct
  band limit, shelves; the front-end does the equal-power Mix.
- Horizon adds its echo (two lines with spread, feedback, tone, tanh drive, wow, the shifter for detune / reverse /
  octave modes, sample-and-hold lo-fi) before or after the reverb.

Parameter laws = the measured laws (§1–§6) so translations are identities, except Atrium's Decay (0.1–100 s with 2 s
at the centre instead of the reference's linear law) and Halo's (§5). Mode tables (per algorithm: line set, scale,
RT/Decay ratio and floor, bass-RT factor, early level / span, build-up, modulation scale, gain, own pre-delay, gate)
were fitted with a harness that renders LYNX's engine with the same normalised values as each capture and compares the
same metrics (tmp/valh100/fit).

Fit, Epoch Verb vs VintageVerb (22 modes, Decay 2 s) [EXEC]: mid-band RT within ±8 % on every mode (Concert Hall
2.52 / 2.46 s, Plate 1.84 / 1.90, Room 2.16 / 2.18, Chorus Space 2.36 / 2.20, Chaotic Neutral 3.61 / 3.50, Palace 3.84 /
3.63 with 125 Hz 5.49 / 5.59); onsets within ~1 ms except Random Space (+9 ms); e100 within ±2 dB on 19 of 22 modes;
RT vs Decay 0.5–8 s within ±10 % (Concert Hall, Plate, Room, Chorus Space, Smooth Room, Palace); bass multiply, high
shelf, high / low cut, pre-delay and mix as in §1. Not reproduced: the build-up shape of the slow-attack modes (peak
earlier by up to 30 %), Nonlin's exact gate envelope (plateau to ~1.1 s instead of a rise to 0.7 s), the exact early
pattern of each mode, the colour modes' converter character beyond a band limit.

## 7b. Editors (UI rule)

The official demos' own editors were opened in the measurement host (`fxmeasure editor <png>`: the plugin's editor
in a plain window, captured with PrintWindow; pictures kept in tmp/valh100/ref, never in git) and their layout logic
studied [EXEC]:
- VintageVerb: Mix and PreDelay knobs stacked on the left, a large Decay knob, then titled boxes DAMPING (HighFreq,
  HighShelf / BassFreq, BassMult as 2 × 2), SHAPE (Size, Attack), DIFF (Early, Late), MOD (Rate, Depth), EQ (HighCut,
  LowCut); MODE and COLOR menus along the bottom, a one-line description of the hovered control under them.
- Room: six vertical faders on the left (Mix, Predelay, Decay, Lo Cut, Hi Cut, Depth = early/late balance) and an
  EARLY / LATE tab pair switching the knob grid on the right; Reverb Mode menu under the faders.
- Plate: Mix / PreDelay, a very large Decay knob, Size / Width, EQ box (LowFreq, HighFreq / LowGain, HighGain), MOD box
  (Rate, Depth); Mode menu below.
- Supermassive: Mix / Width, a box with two large knobs Delay and Warp and the sync selector (Msec / note) plus Clear
  under them, Feedback / Density, MOD (Rate, Depth), EQ (High, Low); Mode menu below.
- Shimmer: one row of nine knobs (mix, shift, feedback, diffusion, size, low cut, high cut, mod rate, mod depth) and three
  mode selectors below (reverb mode, pitch mode, color mode). Its read-outs confirmed shift = ±12 and the low cut
  starting at 10 Hz.
- FutureVerb: Mix / Width, an ECHO box (big Delay with the sync selector, Feedback, Level, Drive, Spread), a REVERB box
  (big Decay with the Echo→Rev routing switch, Size, Level, Early/Late, Density), MOD and EQ boxes; ECHO MODE, REVERB
  MODE and COLOR selectors along the bottom.
- The demos state "reverb fades out every 45 seconds" in their header — matching §0.

LYNX's editors (core/PluginLayout.cpp) keep that grouping and order with LYNX's own drawing (knobs, section boxes,
menus, colours, fonts); Atrium shows the early and late knob groups side by side instead of behind tabs. No graphics,
logos or colours were taken.

## 8. Translation (core/PluginSubstitutes.cpp)

All six entries map every parameter from the XML attributes: identities, `steps` maps for the enumerations
(breakpoints of §1–§6) and a `curve` map for Atrium's decay. New map shapes: `| curve x:y …` (piecewise linear) and
`| steps x:y …` (enumerations). The original record stays byte for byte in `substituteFor`.

## Where I stopped

- Measuring done (§0–§6). Engines, wiring (EffectKind, project file ids, mixer list, layouts, substitutes table with
  curve / steps maps, pt-BR strings), tests (tests/LynxVerbTests100.inc), PLUGINS.md / PLUGIN_FALLBACK.md / website
  data written. Epoch Verb fitted; Atrium / Foil Plate / Nebula / Halo / Horizon second fit pass running
  (tmp/valh100/py/fit2_*.txt). Then: build-local (0 failures), land.
