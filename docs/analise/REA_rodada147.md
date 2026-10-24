# REA round 147 (parity147): docs/PARITY.md, tier 3

2026-10-24. Continues round 144 (`REA_rodada144.md` "Where I stopped"). This round: Prism Multiband's Dynamic phase
rebuilt on measurements of the Pro-MB trial, Prism's expert panel (side-chain filtering, stereo link per band).
Sub-rounds: 147a (Echoform ← EchoBoy / PrimalTap: saturation types, echo styles, Multiply — `REA_rodada147a.md`),
147b (Ember M/S, LYNX Tune keyboard, Ridge auto make-up / ANALOG — `REA_rodada147b.md`).

Evidence markers: [EXEC] run here on the official trial, hosted black-box in `tools/fxmeasure` (public VST3 API:
parameters, value texts, audio in / out, the component state as a DAW stores it); [model] LYNX's own choice. Nothing
was decompiled; no graphics, presets or names were copied. Raw data: `D:\lynx-work\parity147\mb\` (m1 = static band
responses per mode, m2 = a compressing band on tone bursts, m3e = stereo link, m4 / m5 = the free side-chain filter,
st = saved states); analysis scripts in the job's `tmp/parity147/py` (mb2.py, link.py, scf2.py, burst.py).

## 1. Prism Multiband: Pro-MB's Dynamic phase, measured and rebuilt

[EXEC] Pro-MB 1.3.3, 48 kHz, look-ahead on, static bands (range 0, level ±12 dB), impulse responses of 26 cases
(band below 1 kHz, above 1 kHz, 300 Hz – 3 kHz, 800 – 1250 Hz; slopes 6 / 12 / 24 / 36 / 48 dB/oct; two adjacent
bands −12 / −12 and −12 / +6; three bands) in Linear and Dynamic phase. Findings:

- Per band, Dynamic phase is **exactly the minimum-phase version of the Linear-phase magnitude** (the cepstral
  minimum phase of |H_linear| reproduces the measured phase within 0.2°: e.g. the band below 1 kHz at −12 dB, 24
  dB/oct: −4.08 dB and +53.0° at 1 kHz; 12 dB/oct +36.8°, 48 dB/oct +70°; the middle band 300 Hz – 3 kHz: −46.3° at
  300 Hz, +2.6° at 1 kHz, +46.3° at 3 kHz).
- With several bands working, the bands are **in cascade**, not summed: two adjacent bands at −12 dB give −8.15 dB
  at their shared 1 kHz crossover (the product of two −4.08 dB, while linear phase is a flat −12 dB); −12 / +6 dB
  give −0.57 dB (−4.08 + 3.52). The three-band case matches the product to 0.1 dB.
- The linear-phase magnitude of a band is 1 + (g − 1)·A, A = |low pass of its upper crossover| · |high pass of its
  lower one|, Linkwitz-Riley shapes (−6 dB at the crossover, 1 / (1 + x^{2n})); Pro-MB's 6 dB/oct slope follows a
  different law (LYNX has no 6 dB/oct slope).
- At rest (level 0, no gain change) the output is the input exactly (round 144b).

LYNX (`core/MinPhaseBand.h`, used by `prism::Multiband` in src/core/DynamicsSuite.cpp) [model, textbook]:
each working band runs F = S², |S|² = |F|, built by closed-form spectral factorisation on the bilinear transform's
prewarped axis (so the magnitudes are exactly those of LYNX's digital crossovers): S's poles are the two Butterworth
sets of the band's crossovers, its zeros the left-half-plane roots of 1 + (g − 1)·A written as a quadratic in
Ω^{2n} — each root gives the 2n solutions of s^{2n} = (−1)^n·u on a circle, so no general root finder is needed;
the edge bands reduce to a Butterworth set scaled by g^{±1/2n}. The look-ahead delayed input passes the working
bands' filters in cascade; the filters are redesigned every 8 samples from the detectors' gains (only when a gain
moved), bands that cannot work are not run (exact transparency), the pan's / link's two gains get one filter each
(left / right or mid / side), oversampling runs the cascade at the high rate. The editor's curve is the product of
the bands' magnitudes. Results (tests/Parity147Tests.inc): every measured case within 0.02 dB and 0.1°, the tone
gains −11.99 / −10.60 / −4.07 / −0.38 / 0.00 dB at 50 / 500 / 1k / 2k / 10 kHz (Pro-MB −12.0 / −10.6 / −4.1 / −0.4 /
0.0), two bands −8.15 / −0.57 dB (Pro-MB −8.15 / −0.57).

[EXEC] A compressing band on 200 Hz bursts (−6 / −30 dB, threshold −30, 4 : 1, range −12, attack / release 20 %):
both reach −11.9 dB in the loud half; in the quiet half Pro-MB starts to recover at once (−11.3 dB after 20 ms,
−6.9 after 100 ms) while LYNX holds about 50 ms and then recovers faster (−7.6 after 100 ms) — the detector's
release law (round 108), not the new filters; the 3 kHz tone in the other band passes untouched in both.

Compatibility: a new parameter **Dynamic engine** (appended; new instances and translated Pro-MB states = measured).
A project saved before round 147 has a shorter parameter list: `upgradeLoadedEffectParams()` (core/Effects.h, called
by engine/ProjectFile.cpp) sets the classic engine for it, so old projects sound the same. The editor's bottom bar
shows **Classic dynamic** for such a slot; one click switches it to the measured engine.

## 2. Prism Multiband: the expert panel (side-chain filtering, stereo link)

[EXEC] Pro-MB applies its expert settings only while its Expert Mode is on (with it off, the measurements and the
saved states' link / filter values are ignored). Saved state per band (base 22b): 15 side-chain filtering (0 band, 1
free), 16 / 17 low / high (log2 Hz), 18 side-chain input, 19 stereo link (the parameter's own value), 20 link mode;
slot 142 = Expert mode.

Stereo link, one band over the whole spectrum, threshold −30 dB, 4 : 1, range −30, knee 0; 200 Hz at −6 dB left /
−36 dB right (and a mostly-side signal):

| Link | Pro-MB | LYNX |
|---|---|---|
| 0 % | L −17.74, R 0.00 | L −17.87, R 0.00 |
| 50 % | L −17.74, R −13.47 | L −17.87, R −13.56 |
| 100 % | both −17.74 | both −17.87 |
| 100 %, 50 % mid-only | M −15.95, S −4.74 | M −16.06, S −4.98 |
| 100 %, 100 % mid-only | M −13.47, S 0.00 | M −13.56, S 0.00 |
| 100 %, 100 % side-only | M 0.00, S −13.06 | M 0.00, S −13.15 |

Laws read from it [EXEC → model]: up to 100 % each channel's detector hears (1 − k)·|own| + k·max(|L|, |R|)
(linear amplitude); above 100 % the mid's (side's) detector moves from the louder channel to the mid (side) itself
and the other part's gain from the linked gain to unity; the gains then act on mid and side. (LYNX's 0.13 dB
deeper reduction is its existing ratio / detector law, round 108.)

Free side-chain filter (threshold −60, 2 : 1: the detector level of a −20 dB probe read from its gain): 4th-order
sides, −23 … −24 dB one octave outside; 2 – 6 kHz: +0.8 dB inside, −1.9 / −2.3 dB at the edges; 1 – 8 kHz flat;
narrow ranges boost: +5.7 dB at the centre of one octave, +10.6 for 2 – 3 kHz, +13.7 for 2 – 2.5 kHz. LYNX: a
4th-order high pass and low pass, each section's Q 1.25 × Butterworth (the fit: 0.4 – 0.6 dB rms for one octave and
wider; 2 – 4 kHz probe: −22.6 / +2.1 / +4.4 / −25.2 dB at 1k / 2k / 3k / 8k, Pro-MB −22.4 / +2.1 / +5.5 / −23.5).

LYNX (appended, defaults = the old detector): per band **link** (the reference's value law), **link mode** (mid /
side), **SC filter** (band / free), **SC low / high** (30 Hz – 30 kHz, 30 · 1000^v). Editor: **Expert >>** in the band
panel opens its right part (Side chain: Band | Free, Low, High; Stereo link: Link, Mid | Side). Pro-MB states:
translated when their Expert mode is on (an external side-chain input on any band turns LYNX's side-chain switch to
external). Tests: tests/Parity147Tests.inc.

## Where I stopped

- Prism: per-band slopes (Pro-MB: 6 – 48 dB/oct per side, continuous; LYNX: one 12 / 24 / 48 for every crossover —
  the closed-form dynamic filters assume one order per band), free band placement with gaps, one side-chain input
  per band, the free side-chain filter's extra boost below one octave, the detector's release shape (§1).
