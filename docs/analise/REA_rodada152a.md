# REA round 152a (parity152a): Compressor ← Renaissance Compressor; Glow Leveler ← CLA-2A

2026-10-24. Sub-round of parity152 (docs/PARITY.md, tier 5; round 150 §5 "Left": RComp's threshold default, its bipolar
gain-change meter, the vintage voicing's compression away from her 2.95 : 1; then Glow ← CLA-2A, one of the lowest
pairs at 7 / 7). Evidence markers: [EXEC] measured here on the official Waves trials (RCompressor Stereo and CLA-2A Stereo
17.1, **demo mode**: the audio mutes ~1 s every 20 s, so every reading is mute-masked), hosted black-box in
`tools/fxmeasure` (public VST3 parameters and value texts, audio in / out, the component state as a DAW stores it);
[model] LYNX's own choice. Nothing was decompiled; no graphics, presets or names were copied. The Waves shell quirk
still applies: load another shell plug-in first (DeEsser Mono), then the parameter list is the plug-in's own (round
147 saw only the shell's generic controls without it). Raw data: `D:\lynx-work\parity152\a\` (m1 / m2 / h1 = RComp
runs, c1 … c9 = CLA-2A runs, *.log their logs, st3.wav / ud.wav / silt.wav / t40.wav / lq.wav the probes, w_ / s_ /
m_ / o_ / t15_ / t45_ / h_ = RComp renders, lx_ / ly_ / hl* = LYNX renders, cl_ / am* / an_ = CLA-2A renders, gl_ / ol_ /
lf_ = LYNX / lab renders); scripts in the job's `tmp/parity152/a` (py/st.py, dtab.py, traj.py, limfit.py; lab/optolab.cpp
= the opto loop with the Limit constants as arguments, for the fit; fxlynx.cpp).

## 1. Compressor ← RComp [EXEC]

**Fresh instance** (`defaults` + `get`): Threshold **0.0 dB**, Ratio 1.00, Attack 16.0 ms, Release 160 ms, Gain 0.0 dB,
ARC, Warm, Electro, Mix 100, Trim 0. LYNX's Compressor is also FL's generic compressor, so a new instance keeps its
−18 dB (it compresses at once); the reference's start is the factory preset **RComp-style start** (vintage voicing,
RComp ranges, the values above, limiter on). Three more LYNX starts: Vocal program, Bus glue, Upward lift (0.79 : 1).

**Static curve per ratio**: 1 kHz stairs −60 … 0 dBFS in 3 dB steps (0.8 s each; the last 0.4 s of each step, 10 ms
windows, median), threshold −30 dB, ARC / Electro / Warm, attack 16 ms, release 160 ms, ratios 1.05, 1.1, 1.2, 1.5, 2,
2.95, 4, 6, 10.46, 20, 50; Smooth at 2 / 4 / 10.46 / 50, Manual and Opto at 4 / 10.46, thresholds −15 / −45 at 2.95 /
10.46 / 50. Findings:
- the curve is the same at every threshold (shifted), and **Warm = Smooth, ARC = Manual, Electro = Opto** statically
  (identical stairs; the renders differ only at the steps' transients, −23 … −31 dB);
- 20 : 1 and 50 : 1 are the same curve (−58 dB difference);
- the knee starts ≈ 12 dB under the threshold, and the slope over the threshold first grows and then falls again at
  low ratios (2 : 1: 0.55 dB/dB at +6 … +9 dB, 0.18 at +30) — round 123's knee + ratio fit (made at her 2.95 : 1, levels
  up to +18 dB over) is 0.6 … 1.8 dB rms off at the other ratios, up to 3.5 dB at 1.5 : 1, +30 dB over, and 1.2 – 1.7 dB too
  deep at 10.46 : 1 around +12 … +18 dB (round 150's note).

LYNX (`vmatch::progcomp`, `core/RCompStatic.inc`): the trial's stairs minus round 123's render, per ratio (rows) and level
over the threshold (−15 … +30 dB), added to the static target; bilinear in (ln ratio, level), zero at 1 : 1, the last
column past +30 dB. The 2.95 : 1 row is zero up to +18 dB over (the measured row is within 0.07 dB there), so her state is
unchanged (≤ 0.01 dB up to −3 dBFS at her −15 dB threshold, test). Smooth takes Warm's curve. Result: every measured
ratio within 0.07 dB (rms ≤ 0.04), holdouts not in the table 1.2 / 3.3 / 8 / 14 : 1 (threshold −20 for 14) within
0.06 dB (round 150's curve: 0.6 … 1.0 dB rms, 0.8 … 2.4 dB max).

Appended parameter **Vintage curve** (index 18): Measured (new instances, translated states) | round 150 (a project
saved before round 152: `upgradeLoadedEffectParams`); the editor shows **Old | Measured** tabs above the meter with the
vintage voicing.

**Bipolar gain-change meter**: control output 5 = the block's signed gain change in dB (vintage: the largest |gain
change| of the block; classic: the envelope, + for the expansion below 1 : 1 — the classic reduction meter no longer
shows the expansion as reduction). Display `gcmeter` in the Compressor panel (replaces the 0 … −30 reduction bar): the
reference's scale +6 / +3 / 0 / −3 / −6 / −12 (placed as on the trial's meter) and −24 below, the bar from the 0 dB
line down for reduction and up for the boost, the held largest change under it (click = reset).

## 2. Glow Leveler ← CLA-2A [EXEC]

Parameters (shell quirk respected): Gain 0 … 100 (default **40.00**), Peak Reduction 0 … 100 (**46.00**),
Comp/Limiter (Comp), Hi Freq 0 … 100 (**50.0**), Analog 50Hz | 60Hz | **Off**, VU Display In | **GR** | Out, Mix
(100 %), Trim (0 dB), Auto Makeup (**Off**). RealWorld indices read back by setting each parameter and diffing the
saved component: **204 Mix %, 207 Auto Makeup, 140 Analog (0 = 50 Hz, 1 = 60, 2 = off), 17 VU display**.

- **Defaults**: a new Glow is the reference's fresh instance — the Opto classic voicing (round 123's measured leveler,
  dials 0 … 100), Gain 40, Peak reduction 46, HF emphasis 50 (+8.45 dB at −40 dBFS on both). Glow's own (Modern)
  presets now say `Voicing = 0.00` and `Emphasis = 0 %` so they sound as before.
- **Limit** (up / down 1 kHz stairs −48 … 0 … −48 dBFS, 0.5 s steps, peak reduction 46 / 62.6 / 80, Gain 40): the trial
  over-compresses at 62.6 (from −12 dBFS the gain falls by more than the input rises: −6.5 → −9.9 → −13.7 dB) and stops
  at a 30.4 dB reduction at 80 (−22.1 / −22.4 dB at −3 / 0 dBFS). Fitted with the opto loop in a lab copy
  (`optolab.cpp`, coordinate search): the cell's exponent 10.63 (round 123's [model]: 12), level shift +0.83 dB, **1.5 %
  of the input added to the side chain** (what lets a feedback leveler over-compress) and the cell's deepest reduction
  30.44 dB. Trajectory rms 0.34 / 0.52 / 0.50 dB (round 123's Limit 0.37 / 1.00 / 1.32); the rest is the release after
  loud passages (≈ 1 dB slower than the trial's in both modes — round 123's slow conductance, fitted on her takes, not
  touched).
- **AUTO MAKEUP** is not a level follower: it is a gain that follows the Peak Reduction dial (a −40 dBFS tone, fresh
  instances; Auto Makeup must be processed once before the dial moves, as a user would): −1.48 dB at 0, −0.86 at 40, 0 at
  46, +2.85 at 50, +6.07 at 60, +6.78 at 62.6, +11.37 at 80, +16.65 at 100 (≈ 0.26 dB per step above 60). It is relative to
  the dial where Auto Makeup was switched on (switched on at 62.6, then 80 gives +4.46 and 46 gives −6.8); LYNX uses the
  dial's default 46 as the reference point [model].
- **ANALOG** (6 s of silence, Gain 40 / 70, Trim +18, Mix 0, Peak reduction 100): −89.0 … −89.3 dBFS RMS per channel at
  50 and 60 Hz alike, the same at Gain 40 and 70 and Peak reduction 100, +18 dB with Trim +18, nothing at Mix 0 % — so it
  is added in the wet path after the gain. Noise flat per third octave (−105 dB per band, −101 at 16 kHz), L / R
  correlation 0.05; hum on both channels in phase, mains harmonics (50 Hz: −117.2, **−98.7**, −118.2, −105.2, −122.5 …
  dBFS peak for 1 … 5 × 50 Hz; 60 Hz alike), DC 5 µ.

LYNX (`glow::` in core/MixSuite.{h,cpp}, `vmatch::opto` in core/VoiceMatch.{h,cpp} + VoiceMatchOpto.inc; Glow: 10
parameters): appended **Opto law** (Measured — new instances, translated states | Old — a project saved before round
152 keeps round 123's Limit and no auto make-up in the classic voicing) and **Analog** (Off | 50 Hz | 60 Hz: Kellet pink
noise over Gaussian noise, −89.5 dBFS RMS, two generators, plus the 12 measured hum harmonics, in the wet path of both
voicings). Editor: Analog tabs bottom left (the reference's place), Old | Measured top left with the classic voicing.
Translation (`translateWavesCla2a`): + Mix, Auto Makeup, Analog, the measured law (8 values).

## 3. Tests

tests/Parity152aTests.inc: the Compressor's appended parameter, texts and upgrade; the measured curve at 2 / 3.3 / 8 /
10.46 / 50 : 1 against the trial (−24 / −12 / 0 dBFS within 0.25 dB; 3.3 and 8 are holdouts), Smooth = Warm, her state
unchanged; the signed meter (compression < 0, expansion > 0, both voicings); the RComp-style start preset; Glow's
defaults, upgrade and texts; Limit at 62.6 / 80 against the trial (within 1.2 dB at −6 / 0 dBFS); Auto Makeup at 46 /
62.6 / 80 (within 0.05 dB) and none with the old law; Analog's level, its 100 Hz hum, Trim and Mix; the translation.

## Where I stopped

- Done: §1, §2; PARITY rows (Compressor: threshold default, gain-change meter, static curve per ratio; Glow: Gain / Peak
  reduction / Hi Freq defaults, Analog, Mix, Auto Makeup, static curve / Limit, tilt / H2, attack / release, translation)
  and scores: Compressor ← RComp 9 / 7 → **10 / 8**, Glow ← CLA-2A 7 / 7 → **9 / 8**.
- Compressor left: Warm / Smooth's and Electro / Opto's transient behaviour (statically identical; not separated), the
  ratio value box inside the meter (LYNX: the knob under it).
- Glow left: the classic voicing's release after loud passages (≈ 1 dB slower than the trial on 0.5 s stairs, Comp and
  Limit), Auto Makeup's reference point (the dial where it was switched on, not saved by LYNX), the VU selector is not
  translated (an editor view in LYNX).
