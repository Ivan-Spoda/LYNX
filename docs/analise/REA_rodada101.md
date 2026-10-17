# Round 101 — Valhalla delay / modulation plugins, black-box, and LYNX's own equivalents

Goal: measure from the outside how the official Valhalla DSP demos installed on this PC behave —
ValhallaDelay 3.0.5, ValhallaFreqEcho 1.2.0, ValhallaSpaceModulator 1.1.6, ValhallaUberMod 1.1.6 (VST3, 48 kHz) —
and build LYNX's own delay / modulation effects "inspired by" them, registered as substitutes for projects that
use them on a machine without the plugins. (The Valhalla reverbs are round 100.)

Rules (same as round 99, `MEASURE_fabfilter.md`): the plugins were **only hosted as a DAW hosts them** through
`tools/fxmeasure` (VST3 parameter API: names, value ↔ text; `getStateInformation`; test signals in, audio out). No
disassembly, decompilation, string dumps or resource extraction of the binaries; no presets, graphics or manual
text copied. Every number below is our own measurement [EXEC]; formulas are fitted models [fit]; anything not
measured is "not measured" / "not confirmed". Raw captures and scripts stay in `tmp/r101` (never in git).
LYNX's effects are written from published DSP literature (delay-line interpolation, Hilbert-transform frequency
shifting, granular / delay-line pitch shifting, barber-pole flanging, BBD / tape echo descriptions in AES / DAFx
papers and textbooks) and these measurements; nothing else.

## 0. Harness notes

- `tools/fxmeasure`: Valhalla's plugins come up with their **main buses disabled** (0 in / 0 out); the host now asks
  for stereo in / out when the main bus has no channels (fix in `prepare()`).
- Their parameters' `getText (value)` ignores the value and always prints the *current* value, so value → text laws
  were read by `set` + reading the echo of the parameter's current text (201 points per parameter).
- A parameter set twice in the same block (e.g. `defaults` then `set`) can keep the first value (seen on the delay
  time): the scripts run `settle 0.05` after `defaults`.
- The delay time glides (tape-style) when changed, and the modulation (ModDepth default 50 %) moves the echo: time
  measurements use ModDepth = Wow = Flutter = 0 and 3–8 s of settling.

## 1. Saved state [EXEC]

`getStateInformation` (JUCE host format) wraps the VST3 component state, which is — as in FL's wrapper records of
round 98 — `VstW` + `CcnK … FBCh` + a 4-character plugin code (`dLay`, `FqEh`, `SpMd`, `uber`) + a block of zeros,
then the JUCE XML. The XML root element holds **every parameter as its normalised 0..1 value**, attribute name =
parameter name (FreqEcho: `wetDryMix` for the parameter "wetDry"; root `<MYPLUGINSETTINGS>`), plus `pluginVersion`,
`presetName`, `mixLock`, `uiWidth`, `uiHeight`. So every saved setting is readable and the value laws below
translate it.

| Plugin | root element | attributes (normalised) |
|---|---|---|
| ValhallaDelay | `ValhallaDelay` | Mix DelayStyle DelayLSync DelayLNote DelayL_Ms DelayRSync DelayRNote DelayR_Ms DelaySpread DelaySpacing DelayRatio RepeatSwell TapA–TapD Feedback Width DriveIn Age Diffusion DiffSize LowCut HighCut ModRate ModDepth Wow Flutter FreqShift FreqDetune PitchShift PitchDetune Mode Era Ducking Reserved2–4 |
| ValhallaFreqEcho | `MYPLUGINSETTINGS` | wetDryMix shift delay sync feedback lowCut highCut stereo |
| ValhallaSpaceModulator | `ValhallaSpaceModulator` | wetDry rate depth feedback manual Mode |
| ValhallaUberMod | `ValhallaUberMod` | Mix Depth StereoWidth Delay Feedback Spread Slope Skew Random TapGain Diffusion DiffSize DiffModRate DiffModDepth LowCut HighCut SpatialXover DetuneRate DetuneDepth VibratoRate VibratoDepth OverMod DriveInGain DriveOutGain DriveNoiseGain FeedbackRotate SmoothingTime ColorMode DelaySync type DiffEnable Drive DrivePrePost Speed InputPan |

## 2. Parameter laws (normalised v → value) [EXEC, fit]

x = 2v − 1 for bipolar laws. "skew k": lo + (hi − lo)·v^k (JUCE-style centre skew); errors are the text rounding.

**ValhallaDelay** (39 parameters incl. Bypass): Mix 0–100 % linear; DelayStyle Single / Dual / Ratio / PingPong / Quad
= floor(5v); Sync Msec (v < 0.5) / Note / Dotted (≥ 0.75) / Triplet (1.0) = floor(4v) with 0, 1 = Msec; Note 1/64,
1/32, 1/16, 1/8, 1/4, 1/2, 1/1, 2/1, 4/1, 8/1 = floor(10v); **Delay ms piecewise linear** through (0, 0), (0.5, 500),
(0.76, 1000), (0.82, 1500), (0.88, 3000), (0.94, 6000), (1, 20000) ms (exact on 201 points); Spread ±20 ms linear;
Spacing ±100 % linear; Ratio 1–100 % linear; RepeatSwell / TapA–D two-state (≥ 0.5); **Feedback 0–200 %** linear;
Width ±100 % linear; DriveIn 0–24 dB linear; Age 0–100 %; Diffusion 0–100 % (0 = OFF); DiffSize 0–100 %; LowCut
10–2000 Hz linear; HighCut 200–20000 Hz linear; ModRate 0.01–10 Hz skew 2.3275 (centre 2 Hz); ModDepth / Wow /
Flutter 0–100 %; FreqShift ±1000 Hz·|x|^5; FreqDetune ±10 Hz·|x|^2.5; PitchShift ±24 semitones linear; PitchDetune
±100 cents linear; **Mode** = floor(24v), 1 Tape, 2 HiFi, 3 BBD, 4 Digital, 5 Ghost, 6 Pitch, 7 RevPitch, 8 BBDuck,
9 Clarity, 10 DuckTape, 11 PitchDuck, 12 LoFi, 13 Quartz, 14 PhaserDDL, 15 RichPitch, 16 ChromeTape, 17 Analog
(0 and ≥ 18 show Tape; default 1/24); Era Past / Present (≥ 0.67) / Future (1.0); Ducking 0–100 %.

**ValhallaFreqEcho**: wetDry 0–100 %; shift ±1000 Hz·|x|^5; delay 0.01–1000 ms skew 3.322 (centre 100 ms); sync
0–23 (index, see §4); feedback 0–100 %; lowCut 20–3000 Hz linear; highCut 50–15000 Hz linear; stereo mono / stereo
(≥ 0.505).

**ValhallaSpaceModulator**: wetDry 0–100 %; rate ±10 Hz·|x|^3.333; depth 0–50 ms skew 2.322 (centre 10 ms);
feedback ±100 % linear; manual ±50 ms·|x|^4; Mode = round-ish(11v): Up, Up/Down, TZFlange+, TZFlange−, TriFlange,
Ocho, Doubler, VariUp, VariUpDown, Ensemble360, Symphonic (index k at k/11, k = 1..11).

**ValhallaUberMod**: Mix, Depth 0–100 %; StereoWidth 0–200 %; Delay 0–1000 ms linear; Feedback, Spread 0–100 %;
Slope, Skew ±100 %; Random 0–100 %; TapGain ±12 dB; Diffusion 0–100 %; DiffSize 10–500 ms; DiffModRate 0.01–10 Hz;
DiffModDepth 0–100 %; LowCut 10–2000 Hz; HighCut 100–20000 Hz; SpatialXover 10–2000 Hz; DetuneRate 0.01–10 Hz;
DetuneDepth 0–100; VibratoRate 0.05–20 Hz; VibratoDepth 0–100; OverMod 1–100×; DriveInGain 0–24 dB; DriveOutGain
−24–0 dB; DriveNoiseGain −120…−30 dB; FeedbackRotate 0–100 %; SmoothingTime 1–1000 ms; DelaySync 0–17; type 0–24
(shown as a number); InputPan 0–8 — all linear. Two-state (≥ 0.505): ColorMode Bright / Dark, DiffEnable, Drive,
DrivePrePost Pre / Post, Speed 1X / 1/2X.

## 3. ValhallaDelay [EXEC]

### 3.1 Time, sync, styles
- Echo position = time + 11 samples in Digital at 48 kHz (0 ms → 43 samples, 1 ms → 59, 100 ms → 4811, 2000 ms →
  96009). Tape / HiFi / Clarity / Quartz / ChromeTape +6, BBD +26…30, BBDuck / Analog +3…15, Pitch / PitchDuck +59
  (the pitch shifter's window), LoFi / ChromeTape at Age 0.5 −180 (not confirmed why), RevPitch: reversed chunks.
- Sync at 120 BPM (playhead tempo): 1/4 = 500.0 ms, 1/2 = 1000, 1/1 = 2000, 1/16 = 125, 1/64 = 31.25; dotted × 1.5
  (1/4. = 750), triplet × 2/3 (1/4T = 333.3) — all + the 11-sample offset.
- A time change glides (the delay line is read at a moving position: pitch bends while it moves, tape-like).
- Styles (L 100 ms, R 300 ms, feedback 50 %): **Single** both channels use the left time; **Dual** L uses L time and
  R uses R time, two independent loops; **Ratio** the right time = left × Ratio (61.8 % → 62.1 ms) with cross-terms
  (L gets an echo at L + R, R a negative one at 2R); **PingPong** first echo on L after L time, then R after +R time,
  then L after +L time …; **Quad** four taps at ¼, ½, ¾, 1 of the left time, each −6 dB, feedback from the last;
  Spacing (Quad) moves the inner taps; **Spread** +10 ms moves L by +2.5 ms and R by −2.5 ms.
- Mix: **equal power** — dry cos(πm/2), wet sin(πm/2) (m = 25 %: −0.69 / −8.3 dB; 50 %: −3.0 / −3.0 dB).

### 3.2 Feedback law
Per-pass loop gain (Digital, 50 ms, impulse-energy decay): Feedback 20 % → −14.2 dB/pass, 50 % → −6.3, 80 % →
−2.2, 100 % → −0.3; ≥ 120 % grows to a bounded level (saturation in the loop). **Loop gain ≈ Feedback/100** (0–200 %
range, so the knob's middle is unity), minus the mode's filtering. Tape at 100 %: −1.4 dB/pass (its band limit and
saturation); BBD at 100 %: about +0.2 dB/pass (its make-up gain).

### 3.3 Mode responses (first echo, Feedback 0, Age 0 unless noted; sweep at −20 dBFS)
Gain in dB at 31.5 Hz … 20 kHz (31.5, 63, 125, 250, 500, 1k, 2k, 4k, 6k, 8k, 10k, 12k, 16k, 20k):

| Mode | response | noise (Age 0 / 0.5 / 1) | harmonics of a 1 kHz sine |
|---|---|---|---|
| Tape | −6.8 −6.2 −5.9 −5.3 −3.9 −2.2 −1.4 −2.4 −4.1 −6.2 −8.4 −10.5 −14.1 −16.7 (100 ms) | none | 2nd + 3rd: −55 dB at −30 dBFS, −36/−42 at −10, −29/−26 at 0 dBFS (gain −2.1 → −5.8 dB: soft compression) |
| HiFi | −0.9 −0.2 … −0.6 (4k) −1.6 (8k) −3.0 (12k) −5.9 (20k) | none | 2nd −43, 3rd −50 at −10 dBFS |
| BBD | flat −0.3 to 1k, −2.3 (1.5k) −12 (2k) −57 (3k) −95 (4k): steep low-pass ≈ 1.6 kHz, **independent of the time** | −240 / −72 / −60 dBFS | 2nd/3rd ≈ −50 |
| Digital | flat to 4k (−0.9), −1.7 (6k) −5.8 (8k) −34 (10k) −54 (12k): ≈ 8.3 kHz brick-wall | none (Age adds aliasing / grit) | odd only (3rd −57 at −10 dBFS) |
| Ghost | as HiFi | none | 3rd |
| Pitch / PitchDuck | as Digital | none | 3rd |
| BBDuck | low-pass that **narrows with time**: 20 ms ≈ 14 kHz, 100 ms ≈ 4.5 kHz, ≥ 300 ms ≈ 600 Hz (clocked BBD) | −84 / −72 at Age 0.5 / 1 | 3rd |
| Clarity / Quartz | flat to 16k (−2.8 / −1.0 dB); **Age = low-pass** (Clarity: 0.5 → −8.6 dB at 6k, 1 → −15 dB at 4k) | none | ≤ −100 dB |
| DuckTape / ChromeTape | ≈ Tape | none | ≈ Tape |
| LoFi | Tape curve at Age 0; Age adds sample-rate / bit reduction (ragged top end) | none | 3rd |
| PhaserDDL | Tape-like with phaser notches (−24 dB at 250 Hz at Age 0) | none | 2nd/3rd |
| RichPitch | as Digital (detune voices, §3.4) | none | — |
| Analog | flat (−1 dB) to 2k, −2.2 (3k) −11.6 (4k) −59 (6k): steep low-pass ≈ 3.3 kHz | −84 / −72 at Age 0.5 / 1 | 3rd −31 at −10 dBFS |

Tape's bandwidth **falls with the delay time** (tape speed): at 8 kHz −5.8 dB (20 ms), −6.6 (100 ms), −10.7
(300 ms), −19.6 (1000 ms); its bass bump moves down too. LowCut is a 1st-order high-pass (209 Hz: −5.7 dB at
125 Hz); HighCut is gentler than 1st order (4160 Hz: −2.1 dB at 1 kHz, −6.1 at 4 kHz) [model: 1st-order low-pass
at ≈ 0.5 × the shown frequency is within 1.5 dB].

### 3.4 Modulation, frequency and pitch shift
- **Wow / Flutter** (tape modes only — Tape, ChromeTape, DuckTape; none in LoFi / Analog): Wow 100 % = random
  pitch wobble 14 c rms (54 c peak) concentrated at 1.5–2.5 Hz; Flutter 100 % = 14 c rms at 7–15 Hz; 50/50 = 8.5 c.
- **ModRate / ModDepth** (Digital, BBD, Quartz, Clarity; not in Tape / Pitch / Ghost): a sine LFO on the delay
  time; the delay swing grows with the **square** of the depth: ±6.1 ms at 100 %, ±1.5 ms at 50 % (Clarity ±10.4
  ms). HiFi: a slow random (0.1–1.4 Hz) instead of a sine.
- **Ghost**: a frequency shifter in the echo path: FreqShift 0.75 → every echo +31.25 Hz exactly (L and R), 0.25 →
  −31.25 Hz; FreqDetune +1.01 Hz → L +1.01, R −1.01 Hz. No effect in the other modes.
- **Pitch**: PitchShift +6 semitones → 1414.4 Hz from 1 kHz (exact), −6 → 707.3; PitchDetune +50 c → L +50 c,
  R −50 c. RevPitch: the same shift on reversed grains. RichPitch: no shift; PitchDetune gives two ±50 c detuned,
  chorused voices (≈ 50 c rms, ≈ 1 Hz).

## 4. ValhallaFreqEcho [EXEC]

- Echo at the delay time + 2 samples (1 ms → 50 samples, 100 ms → 4799 + 1).
- **Mix linear**: dry 1 − m, wet m (25 %: −2.5 / −12.0 dB).
- **Loop gain = 1.5 × feedback** (sine, per pass: 10 % → 0.151, 25 % → 0.374, 50 % → 0.748, 60 % → 0.892, 70 % →
  1.02), bounded by a soft limiter (it settles around +8 dB over a −20 dBFS input at 80–90 %, below 0 dBFS at 100 %).
- **Shift per pass**: every trip around the loop adds the shift (100 Hz: 1100, 1200, 1300, 1400 Hz…), first echo
  at 0 dB, −3.2 / −2.9 / −2.7 dB per pass at 50 %. **Stereo**: the right channel shifts the opposite way (900, 1000,
  1100 …); mono: both the same.
- Filters in the echo path: LowCut = 1st-order high-pass (200 Hz: −2.0 dB at 250 Hz, −5.4 at 125, −10.3 at 63);
  HighCut = 1st-order low-pass (5000 Hz: −2.1 at 4k, −5.8 at 8k).
- **Sync** (120 BPM), index = floor(23v) computed in float (k/23 lands on k or k − 1): 0 off (ms), 1–8 straight
  1/64 … 2/1, 9–15 dotted 1/64. … 1/1., 16–23 triplets 1/64T … 2/1T (index 4 = 1/8 = 250 ms, 13 = 1/4. = 750 ms,
  20 = 1/4T = 333.3 ms).

## 5. ValhallaSpaceModulator [EXEC]

Impulse train (one per 50 ms), wet only, rate 0.5 Hz, depth 10 ms, manual 0:
- **Up**: two crossfaded taps ramping over 2.5 … 7.7 ms (a barber-pole: one fades in at one end while the other
  fades out at the other); **Up/Down**: the same as a triangle (2.4 … 7.9 ms). Negative rate reverses the ramp.
- **TZFlange±**: a fixed tap near the top (≈ 23 ms) plus one sweeping 6.5 … 23.7 ms; "−" inverts the moving tap
  (through-zero cancellation).
- **TriFlange**: taps spread 0.2 … 10 ms (3-phase); **Ocho**: three taps per channel, L/R offset (1.5 / 4 / 6.5 ms
  vs 2.75 / 5.2 / 7.7 ms); **Doubler**: one voice wandering 1 … 8.7 ms; **VariUp / VariUpDown**: one voice, 0.1 … 9.8
  ms; **Ensemble360 / Symphonic**: two voices per channel over 0 … 10 ms.
- Static (depth 0): 0.2–0.6 ms base delay. Manual adds a fixed offset (2 → 2.15 ms, 20 → 19.5 ms).
- Feedback sign is kept (−50 % → negative second echo); +100 % ≈ 0.7 per pass (impulse peaks, approximate).
- **Mix equal power** (50 %: dry −3.0 dB).

## 6. ValhallaUberMod [EXEC]

- `type` = floor(24v) chooses the tap count: 0, 1 → 1 tap; 2 → 2; 3–5 → 3; 6 → 4; 7, 8 → 8; 9 → 16 (alternating
  polarity at 9.5); ≥ 10 → dry only in this version (not confirmed whether the editor offers them).
- Taps at Delay × (1 − Spread × k / N), k = 0 … N − 1 (Spread 50 %: 2 taps 75 / 100 ms, 4 taps 62.5 … 100 ms, 16
  taps 53.1 … 100 ms); tap gain −3.3 dB / √N with **inverted polarity** (N = 1: −0.684); TapGain ±12 dB scales them.
- Skew −100 % moves the left taps to half the delay (L 50 ms, R 100 ms), +100 % the right ones.
- Feedback 50 % with FeedbackRotate 50 %: the repeats alternate between the channels (L 100, 301, 401 ms; R 100,
  201, 301 ms) — a rotation of the stereo feedback.
- Modulation (Depth = master): DetuneDepth 100 → slow random ±, 18 c rms at ≈ 0.3 Hz, L and R opposite; VibratoDepth
  100 → 6 Hz sine (VibratoRate), 123 c rms; both linear in Depth.

## 7. LYNX equivalents (src/core/EchoSuite.{h,cpp})

Four new `EffectKind`s (appended: `echoform`, `spiralEcho`, `orbit`, `latticeMod`), on every insert, with LYNX-style
panels (`PluginLayout.cpp`: echo-train / tap-layout / spiral graphs in `PluginEditors.cpp`), pt-BR strings, and
substitute entries that translate the saved XML (`PluginSubstitutes.cpp`). Delay lines are allocated in `prepare()`
(Echoform: 20.6 s per side), nothing allocates in `process()`, latency 0. The parameter laws are the measured ones
(§2), so the substitutes' maps are one to one; enumerations that differ use the new `| steps …` map law (ValhallaDelay's
17 modes → Echoform's 12 characters), and `| pwl …` is available for non-linear laws.

DSP (our own, from the literature): 4-point Hermite fractional delay lines; one-pole and 8th-order Butterworth
(cookbook sections) filters; tanh shapers with a bias for even harmonics, fitted to §3.3's harmonic levels
(tape k 0.95 / b 0.11, hi-fi 0.55 / 0.17, analog 1.525 / 0, digital 0.375 / 0); the 90° allpass pair (Niemitalo's
published IIR) for single-sideband shifting; a two-tap crossfaded delay-line pitch shifter (50 ms window, its half-window
delay taken off the echo time); reversed grains read backwards with Hann crossfades; Schroeder allpass diffusers;
cosine-interpolated random LFOs (wow 4 Hz segments, flutter 18 + 26 Hz) and sine LFOs.

LYNX vs the measurements [EXEC on both sides, same scripts; LYNX through a dev driver]:

| quantity | reference | LYNX |
|---|---|---|
| Echoform echo at 100 ms (digital) | 4811 samples | 4806 (+6 group delay of the 8 kHz filter) |
| sync 1/4 · 1/4. · 1/4T at 120 BPM | 24011 · 36012 · 16011 | 24007 · 36007 · 16007 |
| loop gain per pass, feedback 20 / 50 / 80 / 100 % | −14.2 / −6.3 / −2.2 / −0.3 dB | −14.0 / −6.1 / −2.0 / −0.05 dB |
| mix 25 % / 50 % (dry · wet) | −0.69 · −8.74 / −3.01 · −3.41 dB | −0.69 · −8.45 / −3.01 · −3.12 dB |
| tape 100 ms at 31 Hz / 1 k / 8 k / 20 k | −6.8 / −2.2 / −6.2 / −16.7 dB | −7.3 / −2.5 / −6.2 / −15.3 dB (after the gain fix) |
| bucket at 1.5 k / 2 k / 3 k | −2.3 / −12 / −57 dB | ≈ −4 / −13.8 / −63 dB |
| digital at 8 k / 10 k | −5.8 / −34 dB | −4.8 / −22 dB |
| wow · flutter · both 50 % (c rms) | 14 · 14 · 8.5 | 12.5–16.5 · 14–15 · 10 |
| LFO 2 Hz at 100 % / 50 % depth (c, fundamental) | 127 / 32 | 125 / 31 |
| shifter +31.25 Hz; detune +1.01 Hz | 1031.25; L +1.01 R −1.01 | 1031.25; L +1.01 R −1.01 |
| pitch +6 st; detune +50 c | 1414.4 Hz; L 1029.3 R 971.5 | 1414.2 Hz; L 1029.3 R 971.5 |
| Spiral Echo loop gain 10 / 25 / 50 / 60 % | −16.4 / −8.5 / −2.5 / −1.0 dB | −16.6 / −8.6 / −2.6 / −1.0 dB |
| Spiral Echo shift +100 Hz, stereo, 4 repeats | L 1100 1200 1300 1400, R 900 1000 1100 1200 | the same |
| Spiral Echo filters 200 Hz / 5 kHz at 31 Hz … 16 kHz | −16.0 −10.3 −5.4 −2.0 −0.6 −0.2 −0.6 −2.1 −5.8 −9.3 −12.6 | −16.2 −10.5 −5.6 −2.3 −0.8 −0.4 −0.8 −2.2 −5.2 −7.5 −9.0 |
| Orbit: mix 50 % dry; feedback −50 % | −3.0 dB; second echo × −0.22 | −3.0 dB; × −0.25 |
| Lattice taps (N = 2, 4, 16), gain | 75/100, 62.5…100, 53.1…100 ms; −1/√N | same positions; −1/√N |
| Lattice feedback 50 % | −6.02 dB/pass | −6.06 dB/pass |

Stereo in Spiral Echo follows the measurement: the loop always shifts upwards by the shift, and the right output takes
each repeat with the opposite shift (so it trails the left by two shifts). Known differences (by design or not
measured): Orbit's
voice shapes are our own designs inside the measured delay ranges; Lattice's tap polarity alternation (type 9.5) and
`type` ≥ 10, the reference's diffusion network, ducking curve, "era" voicing and the duck variants are not modelled.

## Where I stopped

- Done: measurements §1–§6, Echoform / Spiral Echo / Orbit / Lattice Mod, substitutes with translation, tests
  (`tests/EchoSuiteTests101.inc`), PLUGINS.md / PLUGIN_FALLBACK.md / website lists.
- Next: land (build-local + Windows build + selftest); later: Orbit voice shapes against
  finer captures, Lattice polarity patterns.
