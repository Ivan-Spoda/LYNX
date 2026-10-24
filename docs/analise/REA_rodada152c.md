# REA round 152c (parity152c): Facet EQ ← Pro-Q 4 — spectral dynamics, EQ match

2026-10-24. Sub-round of parity152 (docs/PARITY.md: the two ❌ rows of Facet EQ). Evidence markers: [EXEC] measured here
on the official FabFilter Pro-Q 4 trial (v4.1.3, evaluation, ends ~2026-11-16), hosted black-box in `tools/fxmeasure`
(public VST3 parameters and value texts, audio in / out, 48 kHz); [model] LYNX's own choice. Nothing was decompiled; no
graphics, presets or names were copied. Raw data: `D:\lynx-work\parity152\c\` (pq4.tsv = the parameter list, m1 / m2
= the runs and logs, s_* / t_* / comb* / step / pink = the signals, o_* / u_* = the renders, probes.npy); scripts in the
job's `tmp/parity152/c/py` (sig.py, an1.py, fit1.py, fit2.py).

## 1. What the host exposes [EXEC]

Per band: `Spectral Enabled` (2 steps), `Spectral Density` (0 … 100 %, default 50 %), `Spectral Tilt` (Off / On).
Nothing for EQ match (the "Analyzer External Spectrum" parameter only selects a display source). A spectral band reports
**5120 samples** of latency at 48 kHz (the Medium linear-phase figure), whatever the processing mode.

## 2. The spectral law [EXEC]

Signals: 17 partials at −60 dBFS every ¼ octave 250 Hz – 4 kHz plus a loud tone at 700 / 1000 / 1400 Hz (−50 … −10
dBFS); a lone 1 kHz tone −80 … −45 dBFS; the partials alone; pink noise; a −50 → −10 → −50 dBFS step. Band 1 a dynamic
bell at 1 kHz, Q 1, range −12 dB, threshold −40 dB, Spectral on (density 0 / 25 / 50 / 75 / 100 %, tilt off / on).

- Only the loud partial moves: −12.0 dB at 1 kHz with the partials ¼ octave away untouched (the plain dynamic band
  pulls them down with it: −9.2 at 1414 Hz). Off the centre the cut is smaller than the band's shape (700 Hz: −7.5 where
  the dynamic bell gives −9.2).
- A lone tone is cut from about −70 dBFS with a threshold of −40: −0.5 / −5.8 / −11.1 / −12 dB at −70 / −65 / −60 /
  −55 dBFS — the per-bin threshold sits ~30 dB below the knob, ~1 dB per dB.
- The quieter partials are left alone when a loud one is present: the threshold follows the band's loudest bin.
- Fitted (fit1.py, 22 cases, 0.33 dB rms): W = L + 0.18 · 20 log10 S (S = the band's shape 0 … 1), T = max (thr −
  30.5, max W − 11.6), gain = 1.066 · (W − T) up to range × S^2.18.
- Density: no visible effect at ¼-octave spacing; 100 % cuts a pure tone 0.8 dB less (−11.2). Tilt: with pink noise the
  cut moves up the spectrum (peak 700 Hz → 1–1.4 kHz); a fit through the per-bin model did not converge cleanly
  (fit2.py), so LYNX uses 4.5 dB/oct (the analyser's default pink tilt) [model].
- Attack: within a few ms at the default settings (both modes); LYNX uses Facet's attack / release per bin [model].

## 3. LYNX (src/core/FacetEq.{h,cpp})

Appended parameters (Facet: 114): `n Spectral` (off = the old band), `n Density` (50 %), `n Spectral Tilt`. A dynamic
band with Spectral on leaves the IIR / FIR path static and moves in `SpectralStage`: an STFT of the detector (the input
or the side chain, before the EQ) and of the signal after the EQ (4096 points × the rate factor, hop ¼, Hann² overlap-
add, left + j·right in one FFT), per-bin levels smoothed with the attack / release, a ±2-bin peak hold so a partial's
window lobe moves as one, the fitted law per band, density = a triangular blur of the gains blended by 0.15 · density³,
tilt ±4.5 dB/oct around the band frequency. Latency = the phase mode's + 5120 (× rate factor), known from the slot's
parameters before the first block. `spectralGainsDb` exposes the steady law (tests). Editor: **Spectral** and **Tilt**
in the band panel beside Dynamic (right-click Spectral: density), the latency read-out includes it.

**EQ match** [model] (`facet::matchEq`, editor bottom bar "EQ match:"): capture the reference (played through the slot)
and this track from the analyser's input spectrum (power averages), then Match 25 / 50 / 75 / 100 %, up to 2 … 8 bands,
smoothing 1/12 … 1 octave: the difference without its mean level (100 Hz – 10 kHz), Gaussian-smoothed in log frequency,
clamped ±18 dB, fitted greedily into the unused bands (a bell at the largest residual with Q from its half-gain width;
a shelf when the residual runs to an end), three passes of gain refinement; one undo step.

## Tests

tests/Parity152cTests.inc: the parameter block, defaults and texts, the latency (5120 at 48 kHz, 10240 at 96 kHz); the
law against the trial (the loud partial −12 with the others untouched, a lone tone at −70 / −65 / −60 dBFS, 700 Hz); a
render (1 kHz −10 dBFS + 1414 Hz −60: −11.9 / 0.0 dB; the dynamic band −12.0 / −9.2 as the trial), density 100 %
(−11.2); EQ match (a +6 dB bell + −4 dB shelf difference rebuilt within 0.15 dB rms with 5 bands; identical spectra →
nothing).

## Where I stopped

- Done: §1 – §3, PARITY rows (spectral dynamics ✅, EQ match ⚠️) and the score note.
- Left: the tilt's exact law (pink-noise fit), Pro-Q 4's per-bin attack / release laws, the off-centre cut (LYNX
  −6.6 dB at 700 Hz vs −7.5), EQ match against a live side chain or another instance, Pro-Q's own match rules (not
  reachable black-box).
