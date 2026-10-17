# Measurements 99 — FabFilter effects, black-box (and LYNX's Facet EQ / Apex Limiter)

Goal: learn how the reference mixing / mastering effects behave **from the outside**, so LYNX's own EQ and limiter
(and later its compressor, de-esser, gate, saturator and reverb) can be designed against measured targets.

Rules agreed with the user (STRICT): the official 30-day trials installed by the user were **only hosted as a DAW
hosts them** — VST3 parameter API (names, ranges, value ↔ text), test signals in, recorded audio out. No
disassembly / decompilation / string dumps of the binaries or installers, nothing bypassed, no presets, graphics or
manual text copied. Every number below is our own measurement ([EXEC]); formulas are fitted models ([fit]) with
their residuals; anything not measured is marked "not measured" / "not confirmed". Raw captures stay in
`tmp/r99` (never in git).

Plugins and versions (VST3, 48 kHz unless noted): Pro-Q 4 4.1.3, Pro-C 3 3.0.2, Pro-L 2 2.2.6, Saturn 2 2.1.3,
Pro-DS 1.3.2, Pro-MB 1.3.3, Pro-G 1.4.2, Pro-R 2 2.0.6, Timeless 3 3.1.0 (plus Volcano 3, Twin 3, Micro, One, Simplon:
parameter tables only).

## 0. The harness

`tools/fxmeasure/fxmeasure.cpp` — a console VST3 host (JUCE's VST3 hosting, the same code LYNX uses) driven by a
script: `load`, `rate`, `sidechain`, `nonrealtime`, `params` (index, id, name, unit, default, steps, value → text at
k/N), `texts`, `set` (normalized), `settext` (the plugin's own text → value; enumerations and stepped numbers are
found by scanning the plugin's value → text mapping), `get`, `defaults`, `reset`, `settle <s>` (silence, lets
smoothing and envelopes settle), `process in.wav out.wav [sidechain.wav]` (32-bit float), `latency`,
`savestate` / `loadstate`. A playhead at 120 BPM is provided; Win32 messages are pumped between commands so
asynchronous latency / parameter notifications arrive. Built with `tools/fxmeasure/build.sh` against LYNX's JUCE
objects; it needs the common-controls manifest resource (without it the process stalls before `main`).
Python side (numpy only): `tools/fxmeasure/py/` — `sig.py` (WAV I/O, signals, a `Run` that batches one plugin load),
`dyn.py` (sine stairs, bursts, analytic-signal gain envelopes), one script per plugin.

Signals: unit impulses (65536 samples, amplitude 0.25) for linear responses; 1 kHz sine stairs (0.5–0.6 s per 2 dB
step, level read from the last 0.3 s) for static curves; −40 → −6 dBFS (or −30 → −2) sine bursts for attack /
release (gain envelope = |analytic(out)| / |analytic(in)|, 1 ms smoothing; 10 ms for the 100 Hz limiter bursts, so
attack times under ~4 ms are not resolved there); sines at −18 / −6 dBFS for harmonics; 8–10 s impulses for
reverbs. Every capture is preceded by `reset` + `settle`.

## 1. Pro-Q 4 (EQ)

### 1.1 Parameter laws [EXEC]
24 bands × 24 parameters, then globals. Per band: Frequency log 10 Hz … 30 kHz (norm 0.575 = 1 kHz); Gain linear
±30 dB; Q log 0.025 … 40 (norm 0.5 = 1.000); Shape 10 values (Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch,
Band Pass, Tilt Shelf, Flat Tilt, All Pass); Slope 0, 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct, Brickwall; stereo
placement; dynamic range ±30 dB, threshold, attack / release %, side-chain filter. Globals: Processing Mode (Zero
Latency, Natural Phase, Linear Phase), Processing Resolution (Low … Maximum), Character (Clean, Subtle, Warm), Gain
Scale 0–200 %, Output Level, Output Pan, Auto Gain.

### 1.2 Prototypes (zero latency, 48 kHz, 897 impulse responses) [fit]
All shapes are **analog prototypes** evaluated on the analog frequency axis (no bilinear cramping); the fits below
are on magnitude over 20 Hz – 20 kHz.
- **Bell** (slope 12): H(s) = (s² + s·A/Qb + 1) / (s² + s/(A·Qb) + 1), A = 10^(gain/40), **Qb = Q/√2** (symmetric
  boost/cut). 108 configs (100 Hz / 1 kHz / 10 kHz × ±6/12/24 dB × Q 0.1–10): median error 0.020 dB, max 0.375 dB
  (10 kHz bands, the digital realisation near Nyquist).
- **Low / High Shelf** (slope 12): the cookbook analog shelf with **Qs = 0.7071 · Q^0.4697**, independent of gain
  (100 Hz: error ≤ 0.001 dB for Q 0.1…5; 1 kHz ≤ 0.055 dB; 10 kHz ≤ 0.39 dB). Q = 1 is the Butterworth shelf.
  The frequency is the half-gain (geometric middle) point.
- **Low / High Cut**: Butterworth of order n = slope/6 with the **highest-Q section multiplied by Q** (Q = 1 =
  Butterworth; the level at the cut frequency is 20·log10(Q/√2): −9.03, −6.02, −3.01, +3.01, +10.96 dB for Q 0.5,
  0.707, 1, 2, 5, for every slope). 6 dB/oct ignores Q. Median error 0.02 dB (where the response is above −60 dB);
  the high cut reaches a zero at Nyquist (deep stop band differs from the analog one). Brickwall: < −90 dB one
  half-octave away, −0.05 dB at the cut frequency.
- **Notch**: (s² + 1) / (s² + s/Qb + 1), Qb = Q/√2 — median 0.002 dB.
- **Band Pass**: (s/Qb) / (s² + s/Qb + 1), Qb = Q/√2 — within 0.3 dB (higher slopes steepen the skirts).
- **Tilt Shelf**: a high shelf of gain 2·g, normalized by −g (−g below, +g above, 0 dB at the frequency), same Qs law —
  median 0.022 dB. Higher slopes make the transition steeper (6 dB/oct: ±5.1 dB one octave out; 12: ±9.0; 24:
  ±11.7).
- **Flat Tilt**: a straight line through the band frequency with **slope 2·g / log2(1000) dB/oct** (±g across 20 Hz –
  20 kHz); within 0.13 dB of the ideal line from 20 Hz to 20 kHz; slope and Q ignored.
- **All Pass**: flat magnitude; phase at the band frequency 90° (6 dB/oct), 180° (12), 0/360° (24 …).
- **Bell slopes**: Pro-Q 4 bells take the slope too: 12 = the bell above; 24 at Q 2 / +12 dB / 1 kHz gives +0.73 dB
  one octave away (standard bell: about +1.8 dB); Brickwall = 0 dB outside the band. Not fitted (golden data only).
- Gain Scale multiplies every band gain (200 % doubles dB).

### 1.3 Processing modes and sample rates [EXEC]
| Mode | latency @48 kHz | magnitude | phase |
|---|---|---|---|
| Zero Latency | 0 | analog prototype, ≤ 0.4 dB off near Nyquist | minimum phase, digital (10 kHz bell: 41.2° at 5 kHz) |
| Natural Phase | 320 samples (6.67 ms) | analog within 0.02 dB (1 kHz) / 0.24 dB (10 kHz) | the **analog** prototype's phase (10 kHz bell: 36.7° at 5 kHz) |
| Linear Phase (Medium) | 5120 samples | analog | zero after removing the latency |

At 44.1 kHz the zero-latency error near Nyquist grows a little (10 kHz bells up to 2 dB vs analog), at 96 kHz it
shrinks (max 1.1 dB, median 0.02). A few subset captures (band pass at 10 kHz after a 1 kHz band) still showed the
previous frequency — the 50 ms settle was too short for that jump; they are excluded.

### 1.4 Golden table (dB at 50, 100, 200, 500, 1k, 2k, 5k, 10k, 15 kHz; 48 kHz, zero latency) [EXEC]
| Band | 50 | 100 | 200 | 500 | 1k | 2k | 5k | 10k | 15k |
|---|---|---|---|---|---|---|---|---|---|
| Bell 1 kHz +12 Q1 | 0.08 | 0.32 | 1.20 | 5.69 | 12.00 | 5.69 | 1.18 | 0.30 | 0.13 |
| Bell 1 kHz −12 Q1 | −0.08 | −0.32 | −1.20 | −5.69 | −12.00 | −5.69 | −1.18 | −0.30 | −0.13 |
| Bell 100 Hz +6 Q5 | 0.22 | 5.99 | 0.22 | 0.02 | 0.01 | 0.00 | 0.00 | 0.00 | 0.00 |
| Bell 10 kHz +6 Q2 | 0.00 | 0.00 | 0.00 | 0.01 | 0.03 | 0.14 | 1.14 | 6.00 | 2.40 |
| Low Shelf 1 kHz +12 Q1 | 12.00 | 12.00 | 11.97 | 11.10 | 6.00 | 0.90 | 0.03 | 0.00 | 0.00 |
| High Shelf 1 kHz −6 Q0.5 | −0.01 | −0.06 | −0.22 | −1.16 | −3.00 | −4.84 | −5.79 | −5.95 | −5.98 |
| High Cut 1 kHz 24 dB Q1 | 0.00 | 0.00 | 0.00 | −0.02 | −3.01 | −24.10 | −55.93 | −80.22 | −95.09 |
| Low Cut 100 Hz 48 dB Q2 | −47.89 | 2.98 | 0.21 | 0.02 | 0.01 | 0.00 | 0.00 | 0.00 | 0.00 |
| Low Cut 1 kHz 18 dB Q0.5 | −78.09 | −60.13 | −42.45 | −20.97 | −9.03 | −2.90 | −0.50 | −0.12 | −0.05 |
| Notch 1 kHz Q2 | −0.01 | −0.02 | −0.09 | −0.87 | (−∞) | −0.87 | −0.09 | −0.02 | −0.01 |
| Band Pass 1 kHz Q1 | −23.01 | −16.99 | −10.98 | −3.27 | 0.00 | −3.28 | −11.02 | −17.13 | −20.64 |
| Tilt Shelf 1 kHz +6 Q1 | −6.00 | −6.00 | −5.97 | −5.10 | 0.00 | 5.10 | 5.97 | 6.00 | 6.00 |

Dynamic EQ: parameters enumerated (range, threshold incl. Auto, attack/release in %, external side chain), **not
measured** this round.

## 2. Pro-C 3 (compressor)

Styles (11): Clean, Versatile, Punch, Upward, TTM, Vari-Mu, Classic, Opto, Vocal, Bus, Pumping. Ranges [EXEC]:
threshold −60 … 0 dB, ratio 1 … 100:1, knee 0 … 72 dB, range 0 … 60 dB, attack 0.005 … 250 ms, release 10 ms … 2.5 s,
hold 0 … 500 ms, lookahead 0 … 20 ms (needs "Maximum Lookahead" > Off; then the latency is the maximum chosen:
960 samples for 20 ms, whatever the lookahead knob says), oversampling Off … 32x, character (tube / diode / bright)
with drive, side-chain EQ (6 bands, Pro-Q shapes), stereo link, wet/dry gain, mix 0–200 %.

### 2.1 Static curves (1 kHz sine stair; threshold −18 dB, attack 10 ms, release 200 ms, auto gain off; gain in dB vs input dBFS) [EXEC]
Bus and Pumping follow the ideal 4:1 hard-knee curve exactly (−3.00 dB at 4 dB over). The others reduce **less**
than the ideal ratio on a steady sine (Clean −10.6 dB at 16 dB over vs −12 ideal; ratio 100 gives −14.3 vs −15.8):
their detectors are program-dependent (RMS-like / release ripple), so the "ratio" is a nominal one. Vari-Mu has a
very soft onset (−1.3 dB at 4 dB over). Upward expands quiet signals (gain +16 dB at −50 dBFS for 4:1) and holds a
fixed reduction above threshold. TTM compresses below threshold too (upward/downward, "two-way"). Vocal reduces
everywhere (−0.16 dB at −50 dBFS, soft and long knee).

| Style | knee | ratio | -30 | -24 | -18 | -14 | -10 | -6 | -2 | 0 |
|---|---|---|---|---|---|---|---|---|---|---|
| Clean | 0 | 4 | 0.00 | 0.00 | 0.00 | -2.07 | -4.80 | -7.67 | -10.60 | -12.08 |
| Versatile | 0 | 4 | 0.00 | 0.00 | 0.00 | -2.37 | -5.12 | -7.92 | -10.78 | -12.20 |
| Punch | 0 | 4 | 0.00 | 0.00 | 0.00 | -2.61 | -5.46 | -8.38 | -11.34 | -12.82 |
| Upward | 0 | 4 | 1.12 | -3.38 | -7.88 | -7.89 | -7.89 | -7.89 | -7.89 | -7.89 |
| TTM | 0 | 4 | -2.10 | -4.17 | -4.34 | -4.54 | -6.97 | -9.40 | -11.81 | -13.02 |
| Vari-Mu | 0 | 4 | -0.00 | -0.00 | -0.01 | -1.33 | -3.87 | -6.82 | -9.95 | -11.56 |
| Classic | 0 | 4 | 0.00 | 0.00 | 0.00 | -1.96 | -4.94 | -8.04 | -11.19 | -12.79 |
| Opto | 0 | 4 | 0.00 | 0.00 | 0.00 | -2.62 | -5.57 | -8.54 | -11.53 | -13.03 |
| Vocal | 0 | 4 | -1.50 | -2.76 | -4.84 | -6.87 | -9.47 | -12.50 | -15.84 | -17.59 |
| Bus | 0 | 4 | 0.00 | 0.00 | -0.00 | -2.98 | -5.97 | -8.97 | -11.97 | -13.47 |
| Pumping | 0 | 4 | 0.00 | 0.00 | 0.00 | -3.00 | -6.00 | -9.00 | -12.00 | -13.50 |
| Clean | 12 | 4 | 0.00 | 0.00 | -0.74 | -2.37 | -4.89 | -7.77 | -10.78 | -12.31 |
| Clean | 48 | 4 | -0.40 | -1.20 | -2.90 | -4.50 | -6.44 | -8.68 | -11.19 | -12.54 |
| Versatile | 12 | 4 | 0.00 | 0.00 | -0.88 | -2.59 | -5.16 | -7.95 | -10.80 | -12.23 |
| Versatile | 48 | 4 | -0.96 | -2.24 | -4.07 | -5.59 | -7.34 | -9.34 | -11.57 | -12.78 |
| Classic | 12 | 4 | 0.00 | 0.00 | -0.05 | -2.04 | -5.01 | -8.09 | -11.21 | -12.79 |
| Classic | 48 | 4 | 0.00 | 0.00 | -0.42 | -2.59 | -5.47 | -8.50 | -11.60 | -13.17 |
| Opto | 12 | 4 | 0.00 | 0.00 | -1.02 | -2.89 | -5.60 | -8.56 | -11.59 | -13.07 |
| Opto | 48 | 4 | -0.51 | -1.66 | -3.59 | -5.25 | -7.21 | -9.44 | -11.92 | -13.25 |
| Clean | 0 | 1.5 | 0.00 | 0.00 | 0.00 | -0.87 | -2.04 | -3.29 | -4.58 | -5.24 |
| Clean | 0 | 2 | 0.00 | 0.00 | 0.00 | -1.34 | -3.12 | -5.02 | -6.97 | -7.95 |
| Clean | 0 | 8 | 0.00 | 0.00 | 0.00 | -2.45 | -5.66 | -9.02 | -12.44 | -14.16 |
| Clean | 0 | 20 | 0.00 | 0.00 | 0.00 | -2.68 | -6.17 | -9.82 | -13.54 | -15.41 |
| Clean | 0 | 100 | 0.00 | 0.00 | 0.00 | -2.84 | -6.52 | -10.37 | -14.28 | -16.25 |
| Opto | 0 | 1.5 | 0.00 | 0.00 | 0.00 | -1.11 | -2.42 | -3.74 | -5.07 | -5.73 |
| Opto | 0 | 2 | 0.00 | 0.00 | 0.00 | -1.71 | -3.66 | -5.64 | -7.64 | -8.64 |
| Opto | 0 | 8 | 0.00 | 0.00 | 0.00 | -3.08 | -6.53 | -10.00 | -13.49 | -15.24 |
| Opto | 0 | 20 | 0.00 | 0.00 | 0.00 | -3.36 | -7.10 | -10.88 | -14.67 | -16.56 |
| Opto | 0 | 100 | 0.00 | 0.00 | 0.00 | -3.55 | -7.49 | -11.46 | -15.45 | -17.45 |

### 2.2 Time constants (burst −40 → −6 dBFS at 1 kHz, threshold −30 dB, ratio 4) [EXEC]
Times are from the burst start (attack) / end (release) until 50 / 90 % of the gain change; "GR" is the gain during
the burst. Notable: Punch, Bus and Vari-Mu releases do not change with the attack setting; Bus release barely
follows the release knob (199 → 462 ms for 20 → 1500 ms: program-dependent); Opto releases follow the knob nearly
linearly (9 → 405 ms); TTM recovers in ~1 ms; Classic with very fast attack shows a slow second attack stage
(90 % only after 100–150 ms); hold adds roughly the hold time to the release (Clean: +70 ms for 50 ms, +265 ms for
200 ms); lookahead moves the gain change earlier by up to the lookahead time; auto release shortens the long tail
(Clean: 90 % in 261 ms instead of 308).

| Style | attack | release | hold | lookahead | 50 % att | 90 % att | 50 % rel | 90 % rel | GR at −6 dBFS |
|---|---|---|---|---|---|---|---|---|---|
| Clean | 10 | 200 | 0 | – | 4.08 | 16.21 | 83.9 | 308.2 | -16.51 |
| Versatile | 10 | 200 | 0 | – | 8.04 | 23.23 | 61.6 | 348.1 | -16.57 |
| Punch | 10 | 200 | 0 | – | 4.38 | 16.88 | 72.6 | 240.7 | -17.30 |
| Upward | 10 | 200 | 0 | – | 3.19 | 10.19 | 219.4 | 618.6 | -2.63 |
| TTM | 10 | 200 | 0 | – | 2.44 | 5.94 | 0.5 | 1.4 | -13.45 |
| Vari-Mu | 10 | 200 | 0 | – | 5.60 | 17.27 | 35.9 | 112.1 | -10.46 |
| Classic | 10 | 200 | 0 | – | 4.52 | 19.94 | 103.9 | 551.9 | -17.24 |
| Opto | 10 | 200 | 0 | – | 4.83 | 23.88 | 101.0 | 233.4 | -17.52 |
| Vocal | 10 | 200 | 0 | – | 13.62 | 93.17 | 127.7 | 282.9 | -23.91 |
| Bus | 10 | 200 | 0 | – | 3.73 | 9.04 | 238.7 | 644.9 | -17.98 |
| Pumping | 10 | 200 | 0 | – | 1.29 | 5.54 | 158.1 | 240.7 | -18.00 |
| Clean | 0.1 | 200 | 0 | – | 0.58 | 6.27 | 105.6 | 358.0 | -16.83 |
| Clean | 1 | 200 | 0 | – | 0.79 | 4.12 | 101.4 | 348.6 | -16.79 |
| Clean | 30 | 200 | 0 | – | 11.15 | 41.02 | 66.0 | 254.9 | -15.92 |
| Clean | 100 | 200 | 0 | – | 31.54 | 106.83 | 45.8 | 174.2 | -14.22 |
| Clean | 10 | 20 | 0 | – | 3.44 | 11.65 | 12.4 | 141.6 | -14.54 |
| Clean | 10 | 50 | 0 | – | 3.75 | 13.23 | 25.3 | 163.3 | -15.49 |
| Clean | 10 | 500 | 0 | – | 4.25 | 18.50 | 186.4 | 557.1 | -16.99 |
| Clean | 10 | 1500 | 0 | – | 4.42 | 21.29 | 348.0 | 757.5 | -17.40 |
| Versatile | 0.1 | 200 | 0 | – | 0.92 | 2.52 | 53.8 | 347.9 | -17.59 |
| Versatile | 1 | 200 | 0 | – | 1.50 | 4.62 | 56.0 | 348.9 | -17.42 |
| Versatile | 30 | 200 | 0 | – | 23.19 | 57.92 | 70.5 | 350.1 | -15.73 |
| Versatile | 100 | 200 | 0 | – | 62.81 | 163.83 | 102.5 | 361.4 | -13.52 |
| Versatile | 10 | 20 | 0 | – | 6.42 | 16.75 | 71.0 | 242.2 | -13.43 |
| Versatile | 10 | 50 | 0 | – | 7.23 | 19.54 | 58.2 | 255.2 | -15.04 |
| Versatile | 10 | 500 | 0 | – | 8.35 | 25.15 | 90.9 | 536.8 | -17.13 |
| Versatile | 10 | 1500 | 0 | – | 8.58 | 26.83 | 137.2 | 671.2 | -17.50 |
| Punch | 0.1 | 200 | 0 | – | 0.44 | 0.85 | 72.6 | 240.7 | -17.94 |
| Punch | 1 | 200 | 0 | – | 0.81 | 2.35 | 72.6 | 240.7 | -17.83 |
| Punch | 30 | 200 | 0 | – | 12.02 | 44.08 | 72.6 | 240.7 | -16.64 |
| Punch | 100 | 200 | 0 | – | 35.46 | 120.67 | 72.7 | 240.8 | -15.35 |
| Punch | 10 | 20 | 0 | – | 4.06 | 14.31 | 18.4 | 60.3 | -16.39 |
| Punch | 10 | 50 | 0 | – | 4.19 | 15.15 | 27.4 | 90.4 | -16.73 |
| Punch | 10 | 500 | 0 | – | 4.48 | 17.92 | 158.2 | 499.8 | -17.58 |
| Punch | 10 | 1500 | 0 | – | 4.56 | 18.81 | 309.3 | 733.1 | -17.78 |
| Classic | 0.1 | 200 | 0 | – | 0.77 | 151.50 | 324.4 | 749.9 | -18.98 |
| Classic | 1 | 200 | 0 | – | 1.08 | 100.83 | 276.0 | 731.5 | -18.65 |
| Classic | 30 | 200 | 0 | – | 11.33 | 43.71 | 72.0 | 351.4 | -16.06 |
| Classic | 100 | 200 | 0 | – | 29.94 | 101.52 | 53.4 | 221.0 | -13.78 |
| Classic | 10 | 20 | 0 | – | 4.21 | 129.77 | 33.8 | 627.5 | -16.27 |
| Classic | 10 | 50 | 0 | – | 4.33 | 22.88 | 41.6 | 586.7 | -16.73 |
| Classic | 10 | 500 | 0 | – | 4.60 | 20.23 | 200.8 | 632.3 | -17.47 |
| Classic | 10 | 1500 | 0 | – | 4.71 | 21.40 | 335.9 | 753.2 | -17.69 |
| Opto | 0.1 | 200 | 0 | – | 0.83 | 8.21 | 104.4 | 241.4 | -17.59 |
| Opto | 1 | 200 | 0 | – | 1.12 | 8.85 | 104.4 | 241.3 | -17.58 |
| Opto | 30 | 200 | 0 | – | 13.10 | 58.00 | 97.7 | 225.7 | -17.45 |
| Opto | 100 | 200 | 0 | – | 40.94 | 165.98 | 91.2 | 210.6 | -17.14 |
| Opto | 10 | 20 | 0 | – | 4.42 | 17.27 | 9.0 | 18.5 | -16.55 |
| Opto | 10 | 50 | 0 | – | 4.65 | 20.12 | 24.1 | 53.8 | -17.07 |
| Opto | 10 | 500 | 0 | – | 4.92 | 25.96 | 254.0 | 587.8 | -17.72 |
| Opto | 10 | 1500 | 0 | – | 4.98 | 27.60 | 405.1 | 790.4 | -17.85 |
| Vari-Mu | 0.1 | 200 | 0 | – | 0.67 | 1.71 | 36.3 | 112.7 | -11.42 |
| Vari-Mu | 1 | 200 | 0 | – | 1.42 | 4.65 | 36.2 | 112.5 | -11.16 |
| Vari-Mu | 30 | 200 | 0 | – | 11.00 | 31.81 | 35.7 | 111.8 | -9.89 |
| Vari-Mu | 100 | 200 | 0 | – | 21.46 | 57.96 | 35.4 | 111.4 | -9.12 |
| Vari-Mu | 10 | 20 | 0 | – | 4.21 | 10.29 | 3.5 | 9.3 | -7.65 |
| Vari-Mu | 10 | 50 | 0 | – | 4.92 | 13.19 | 8.5 | 25.9 | -9.14 |
| Vari-Mu | 10 | 500 | 0 | – | 5.90 | 19.62 | 91.4 | 284.1 | -10.96 |
| Vari-Mu | 10 | 1500 | 0 | – | 6.10 | 21.83 | 244.7 | 649.6 | -11.30 |
| Bus | 0.1 | 200 | 0 | – | 0.48 | 0.85 | 238.2 | 644.6 | -18.00 |
| Bus | 1 | 200 | 0 | – | 0.75 | 1.31 | 238.3 | 644.7 | -17.99 |
| Bus | 30 | 200 | 0 | – | 10.65 | 26.25 | 239.0 | 645.1 | -17.96 |
| Bus | 100 | 200 | 0 | – | 34.83 | 86.23 | 239.5 | 645.4 | -17.93 |
| Bus | 10 | 20 | 0 | – | 3.75 | 9.17 | 199.3 | 621.9 | -17.93 |
| Bus | 10 | 50 | 0 | – | 3.73 | 9.12 | 204.0 | 625.2 | -17.95 |
| Bus | 10 | 500 | 0 | – | 3.71 | 8.98 | 331.6 | 707.9 | -17.99 |
| Bus | 10 | 1500 | 0 | – | 3.69 | 8.90 | 461.6 | 800.9 | -18.00 |
| Vocal | 0.1 | 200 | 0 | – | 0.48 | 0.88 | 121.8 | 255.8 | -24.26 |
| Vocal | 1 | 200 | 0 | – | 1.52 | 5.83 | 123.3 | 263.2 | -24.18 |
| Vocal | 30 | 200 | 0 | – | 40.42 | 169.83 | 132.3 | 300.1 | -23.57 |
| Vocal | 100 | 200 | 0 | – | 125.69 | 383.85 | 141.5 | 330.3 | -22.87 |
| Vocal | 10 | 20 | 0 | – | 13.88 | 99.81 | 124.9 | 275.8 | -23.91 |
| Vocal | 10 | 50 | 0 | – | 13.81 | 98.73 | 125.3 | 276.7 | -23.91 |
| Vocal | 10 | 500 | 0 | – | 13.46 | 81.25 | 132.5 | 298.0 | -23.92 |
| Vocal | 10 | 1500 | 0 | – | 13.33 | 55.31 | 149.0 | 355.7 | -23.93 |
| Clean | 10 | 200 | 50 | – | 4.08 | 16.23 | 153.3 | 360.7 | -16.52 |
| Clean | 10 | 200 | 200 | – | 4.08 | 16.23 | 349.0 | 517.3 | -16.52 |
| Clean | 1 | 200 | 0 | 1 | 1.17 | 9.83 | 101.7 | 348.9 | -16.80 |
| Clean | 1 | 200 | 0 | 5 | 2.94 | 19.69 | 103.7 | 350.8 | -16.80 |
| Clean | 1 | 200 | 0 | 20 | 6.17 | 200.06 | 111.2 | 358.1 | -16.80 |
| Clean | 10 | auto | 0 | – | 4.10 | 16.56 | 83.4 | 260.6 | -16.58 |

## 3. Pro-L 2 (limiter)

Styles (8): Transparent, Punchy, Dynamic, Allround, Aggressive, Modern, Bus, Safe. Gain 0 … 30 dB, lookahead 0 … 5
ms, attack 0 … 10 s (log), release 0 … 10 s (log), channel link (transients / release), oversampling Off … 32x, true
peak, dither 16–24 bits with noise shaping, output level −30 … 0 dBTP.

- **Latency** (48 kHz) does **not** depend on the lookahead knob: 2880 samples (60 ms) with oversampling off and
  true peak off; true peak adds 235; oversampling adds 225 (2x) … 239 (32x); both 3138 … 3142. [EXEC]
- **Ceiling semantics**: the limiter limits to 0 dBFS after Gain and then applies Output Level, i.e. Output Level is
  the ceiling. A −1 dBTP / −4 dB sample-peak fs/4 sine with Gain +3: true peak **off** and no oversampling →
  sample peaks −1.01 dB untouched, true peak +2.0 dB (a sample-peak limiter); oversampling 4x alone → true peak
  −0.001; true peak on → −0.045 (no OS) / −0.061 (4x). [EXEC]
- **Static**: every style holds the sample peak at 0.99989 (−0.001 dB) in steady state (+10 dB into a 1 kHz stair);
  Output Level −1 → peak 0.8911 (−1.00 dB). [EXEC]
- **Gain-reduction transients** (100 Hz burst −30 → −2 dBFS, Gain +10 dB, so 8 dB of limiting): see the table.
  Aggressive, Modern, Bus and Safe reach the full reduction within ~6 ms whatever the attack knob says (the attack
  knob shapes the *slow* component in the other styles: Transparent 50 % in 70 ms at attack 250 ms vs 4 ms at 1 ms);
  release times scale with the release knob but differ a lot by style for the same setting (+6 dB recovery at release
  500 ms: Dynamic 159 ms, Bus 321 ms, Aggressive 424 ms, Modern 592 ms, Allround 596 ms, Punchy 720 ms, Safe 795
  ms). In steady state the slower styles leave 0.3–0.9 dB less reduction than the fast ones on a 100 Hz tone.

| Style | release ms | attack ms | net gain (dB, +10 dB drive) | 50 % attack ms | 90 % attack ms | +3 dB recovery ms | +6 dB recovery ms |
|---|---|---|---|---|---|---|---|
| Transparent | 500 | 250 | 2.87 | 69.90 | 200.94 | 71.77 | 338.06 |
| Transparent | 10 | 250 | 3.33 | 29.92 | 100.58 | 12.96 | 44.44 |
| Transparent | 100 | 250 | 3.18 | 40.67 | 134.88 | 19.04 | 98.94 |
| Transparent | 1000 | 250 | 2.70 | 84.60 | 241.19 | 144.58 | 620.67 |
| Transparent | 500 | 1 | 2.06 | 4.25 | 6.19 | 76.58 | 259.23 |
| Transparent | 500 | 30 | 2.39 | 15.54 | 49.35 | 78.77 | 289.02 |
| Punchy | 500 | 250 | 2.47 | 20.94 | 276.10 | 113.75 | 720.19 |
| Punchy | 10 | 250 | 3.11 | 6.75 | 94.83 | 11.42 | 76.85 |
| Punchy | 100 | 250 | 2.81 | 14.69 | 165.83 | 23.23 | 201.23 |
| Punchy | 1000 | 250 | 2.33 | 20.79 | 315.00 | 244.27 | 1332.21 |
| Punchy | 500 | 1 | 2.01 | 4.29 | 6.35 | 141.77 | 556.23 |
| Punchy | 500 | 30 | 2.08 | 5.33 | 59.83 | 131.42 | 570.4 |
| Dynamic | 500 | 250 | 2.74 | 20.71 | 60.21 | 41.38 | 159.15 |
| Dynamic | 10 | 250 | 3.20 | 14.69 | 35.58 | 12.85 | 45.42 |
| Dynamic | 100 | 250 | 3.05 | 15.38 | 44.71 | 17.04 | 67.44 |
| Dynamic | 1000 | 250 | 2.58 | 25.25 | 70.21 | 70.71 | 262.79 |
| Dynamic | 500 | 1 | 2.10 | 4.44 | 6.67 | 39.56 | 127.0 |
| Dynamic | 500 | 30 | 2.43 | 9.52 | 24.21 | 41.27 | 142.44 |
| Allround | 500 | 250 | 2.71 | 185.10 | 600.58 | 316.29 | 595.52 |
| Allround | 10 | 250 | 3.47 | 3.44 | 45.06 | 11.31 | 26.12 |
| Allround | 100 | 250 | 3.18 | 44.73 | 305.46 | 77.83 | 156.08 |
| Allround | 1000 | 250 | 2.49 | 250.02 | 680.69 | 522.94 | 1041.94 |
| Allround | 500 | 1 | 2.04 | 4.56 | 8.62 | 336.69 | 549.5 |
| Allround | 500 | 30 | 2.43 | 40.75 | 124.08 | 341.38 | 587.04 |
| Aggressive | 500 | 250 | 2.00 | 4.33 | 6.54 | 211.92 | 423.58 |
| Aggressive | 10 | 250 | 2.00 | 4.33 | 6.54 | 18.75 | 36.94 |
| Aggressive | 100 | 250 | 2.00 | 4.33 | 6.54 | 57.02 | 104.98 |
| Aggressive | 1000 | 250 | 2.00 | 4.33 | 6.54 | 297.94 | 531.33 |
| Aggressive | 500 | 1 | 2.00 | 4.38 | 6.58 | 242.29 | 436.29 |
| Aggressive | 500 | 30 | 2.00 | 4.38 | 6.58 | 238.96 | 434.48 |
| Modern | 500 | 250 | 2.00 | 4.19 | 6.54 | 208.35 | 592.42 |
| Modern | 10 | 250 | 2.00 | 4.19 | 6.54 | 115.21 | 310.92 |
| Modern | 100 | 250 | 2.00 | 4.19 | 6.54 | 132.33 | 362.5 |
| Modern | 1000 | 250 | 2.00 | 4.19 | 6.54 | 304.73 | 880.77 |
| Modern | 500 | 1 | 2.00 | 4.04 | 5.79 | 209.65 | 594.1 |
| Modern | 500 | 30 | 2.00 | 3.92 | 5.88 | 209.19 | 593.23 |
| Bus | 500 | 250 | 2.00 | 4.31 | 6.54 | 95.38 | 320.5 |
| Bus | 10 | 250 | 2.00 | 4.29 | 6.52 | 8.69 | 16.25 |
| Bus | 100 | 250 | 2.00 | 4.29 | 6.54 | 12.5 | 51.33 |
| Bus | 1000 | 250 | 2.00 | 4.31 | 6.54 | 240.02 | 689.67 |
| Bus | 500 | 1 | 2.00 | 4.15 | 6.48 | 114.29 | 334.44 |
| Bus | 500 | 30 | 2.00 | 4.15 | 6.48 | 111.06 | 332.62 |
| Safe | 500 | 250 | 2.00 | 4.83 | 6.06 | 225.52 | 795.08 |
| Safe | 10 | 250 | 2.00 | 4.29 | 6.21 | 54.67 | 186.04 |
| Safe | 100 | 250 | 2.00 | 4.31 | 6.00 | 93.27 | 327.31 |
| Safe | 1000 | 250 | 2.00 | 4.94 | 6.10 | 363.98 | 1261.54 |
| Safe | 500 | 1 | 2.00 | 4.83 | 6.06 | 225.52 | 795.08 |
| Safe | 500 | 30 | 2.00 | 4.83 | 6.06 | 225.52 | 795.08 |

## 4. Saturn 2 (multiband saturator)

28 styles: Subtle / Clean / Warm / Broken Tube, Subtle / Clean / Warm / Old Tape, American Tweed / American Plexi /
British Rock / British Pop / Smooth / Crunchy / Lead / Screaming / Power Amp, Subtle / Gentle / Heavy Saturation,
Subtle / Gentle / Warm Transformer, Smudge, Breakdown, Foldback, Rectify, Destroy. Per band: drive 0–100 %, drive
pan, dynamics ±1, feedback amount / frequency, tone (bass, mid, treble, presence ±24 dB), mix, level; crossovers
40 Hz – 18 kHz at 6 … 48 dB/oct; 6 XY controllers, 6 XLFOs (16 steps), 6 envelope generators, 50 modulation slots.
Measured with one band, tone flat, mix 100 %, sine at −6 dBFS (table: drive 0 / 50 / 100 %; harmonics in dB re the
fundamental). [EXEC]

Families: tube = even + odd harmonics; tape and "Saturation" = odd only (H2 below −140 dB: symmetric curves);
transformers are clean (THD 0.003 %) until high drive; amp styles distort heavily already at drive 0 and fold the
level down (fundamental −5 … −19 dB); output is level-compensated as drive rises (tape: −4.9 dB fundamental at
50 %, −9.9 dB at 100 %). Rectify removes the fundamental (full-wave: even harmonics + DC); Foldback keeps odd
harmonics only; Smudge at 100 % drive and −6 dBFS returned silence (not explained: "not confirmed").

| Style | drive % | level dBFS | f Hz | fundamental gain dB | THD % | H2 | H3 | H4 | H5 | H7 | DC |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Subtle Tube | 0 | -6 | 1000 | -0.74 | 0.03 | -78.3 | -70.0 | -141.9 | -88.1 | -98.0 | 0.0000 |
| Subtle Tube | 50 | -6 | 1000 | -0.82 | 0.58 | -53.1 | -45.5 | -93.9 | -62.4 | -72.3 | 0.0001 |
| Subtle Tube | 100 | -6 | 1000 | -1.88 | 7.91 | -32.3 | -22.7 | -50.0 | -35.5 | -44.3 | 0.0006 |
| Clean Tube | 0 | -6 | 1000 | -0.57 | 1.91 | -42.4 | -35.2 | -64.8 | -54.7 | -63.9 | 0.0000 |
| Clean Tube | 50 | -6 | 1000 | -0.86 | 5.81 | -29.9 | -26.5 | -48.1 | -40.2 | -48.7 | 0.0000 |
| Clean Tube | 100 | -6 | 1000 | -1.55 | 16.37 | -22.6 | -17.4 | -33.7 | -26.9 | -33.6 | 0.0000 |
| Warm Tube | 0 | -6 | 1000 | 0.49 | 4.09 | -28.2 | -38.4 | -56.1 | -82.9 | -120.1 | 0.0012 |
| Warm Tube | 50 | -6 | 1000 | -1.59 | 12.90 | -28.2 | -18.6 | -30.7 | -38.3 | -49.4 | 0.0019 |
| Warm Tube | 100 | -6 | 1000 | -3.93 | 33.55 | -23.9 | -11.3 | -35.4 | -16.7 | -21.0 | 0.0024 |
| Broken Tube | 0 | -6 | 1000 | -1.21 | 4.86 | -26.4 | -42.4 | -59.1 | -85.2 | -139.2 | 0.0000 |
| Broken Tube | 50 | -6 | 1000 | -19.38 | 37.32 | -23.8 | -10.5 | -27.6 | -16.3 | -19.9 | 0.0000 |
| Broken Tube | 100 | -6 | 1000 | -22.87 | 44.65 | -161.0 | -9.5 | -167.5 | -13.8 | -16.6 | 0.0000 |
| Subtle Tape | 0 | -6 | 1000 | -0.90 | 0.00 | -179.0 | -90.3 | -181.3 | -153.3 | -157.5 | 0.0000 |
| Subtle Tape | 50 | -6 | 1000 | -0.90 | 0.02 | -183.7 | -75.1 | -194.3 | -158.5 | -155.2 | 0.0000 |
| Subtle Tape | 100 | -6 | 1000 | -1.55 | 5.59 | -177.6 | -25.1 | -174.0 | -61.0 | -103.2 | 0.0000 |
| Clean Tape | 0 | -6 | 1000 | -0.37 | 1.56 | -145.1 | -36.1 | -152.7 | -74.3 | -116.2 | 0.0000 |
| Clean Tape | 50 | -6 | 1000 | -4.87 | 22.06 | -150.8 | -13.3 | -148.4 | -27.0 | -58.7 | 0.0000 |
| Clean Tape | 100 | -6 | 1000 | -9.91 | 42.28 | -149.8 | -9.6 | -149.8 | -14.3 | -17.5 | 0.0000 |
| Warm Tape | 0 | -6 | 1000 | 0.44 | 1.76 | -139.7 | -35.1 | -147.0 | -73.2 | -116.6 | 0.0000 |
| Warm Tape | 50 | -6 | 1000 | -4.45 | 23.55 | -140.6 | -12.8 | -143.5 | -25.1 | -60.2 | 0.0000 |
| Warm Tape | 100 | -6 | 1000 | -9.87 | 42.31 | -146.6 | -9.7 | -146.6 | -14.3 | -17.5 | 0.0000 |
| Old Tape | 0 | -6 | 1000 | -1.78 | 2.82 | -152.1 | -31.1 | -151.5 | -49.2 | -60.9 | 0.0000 |
| Old Tape | 50 | -6 | 1000 | -11.29 | 34.85 | -150.0 | -10.2 | -152.0 | -16.6 | -24.3 | 0.0000 |
| Old Tape | 100 | -6 | 1000 | -15.84 | 42.63 | -140.0 | -9.2 | -139.7 | -13.7 | -18.3 | 0.0000 |
| American Tweed Amp | 0 | -6 | 1000 | -5.54 | 35.13 | -23.2 | -9.4 | -26.9 | -33.4 | -64.7 | 0.0000 |
| American Tweed Amp | 50 | -6 | 1000 | -19.95 | 80.06 | -20.4 | -2.3 | -22.7 | -15.2 | -28.3 | 0.0000 |
| American Tweed Amp | 100 | -6 | 1000 | -21.71 | 83.95 | -31.3 | -1.8 | -32.2 | -13.9 | -26.1 | 0.0000 |
| American Plexi Amp | 0 | -6 | 1000 | -8.86 | 47.29 | -25.3 | -6.6 | -36.2 | -26.0 | -69.0 | 0.0000 |
| American Plexi Amp | 50 | -6 | 1000 | -20.49 | 61.47 | -26.4 | -4.4 | -28.8 | -20.8 | -56.4 | 0.0000 |
| American Plexi Amp | 100 | -6 | 1000 | -22.19 | 63.12 | -35.1 | -4.1 | -36.9 | -20.0 | -54.9 | 0.0000 |
| British Rock Amp | 0 | -6 | 1000 | -9.96 | 43.68 | -27.7 | -7.4 | -30.6 | -23.3 | -39.3 | 0.0000 |
| British Rock Amp | 50 | -6 | 1000 | -20.44 | 51.51 | -65.2 | -6.0 | -65.6 | -19.1 | -31.1 | 0.0000 |
| British Rock Amp | 100 | -6 | 1000 | -20.43 | 51.53 | -154.1 | -6.0 | -157.2 | -19.1 | -31.1 | 0.0000 |
| British Pop Amp | 0 | -6 | 1000 | -10.58 | 80.33 | -22.2 | -2.4 | -25.6 | -12.1 | -31.6 | 0.0000 |
| British Pop Amp | 50 | -6 | 1000 | -24.45 | 104.04 | -28.2 | -0.4 | -22.9 | -7.8 | -23.7 | 0.0000 |
| British Pop Amp | 100 | -6 | 1000 | -26.39 | 106.36 | -171.1 | -0.3 | -171.4 | -7.3 | -22.7 | 0.0000 |
| Smooth Amp | 0 | -6 | 1000 | -11.29 | 1.66 | -37.5 | -40.6 | -51.4 | -82.6 | -124.8 | 0.0000 |
| Smooth Amp | 50 | -6 | 1000 | -12.61 | 39.72 | -9.5 | -13.6 | -34.5 | -27.6 | -40.4 | 0.0000 |
| Smooth Amp | 100 | -6 | 1000 | -15.65 | 99.77 | -6.5 | -2.7 | -9.5 | -11.2 | -19.0 | 0.0000 |
| Crunchy Amp | 0 | -6 | 1000 | -18.57 | 84.48 | -21.9 | -2.5 | -23.1 | -9.0 | -18.0 | 0.0000 |
| Crunchy Amp | 50 | -6 | 1000 | -29.39 | 139.12 | -28.3 | 0.9 | -24.6 | -2.6 | -8.9 | 0.0000 |
| Crunchy Amp | 100 | -6 | 1000 | -32.76 | 154.05 | -35.4 | 1.6 | -29.9 | -1.4 | -7.2 | 0.0000 |
| Lead Amp | 0 | -6 | 1000 | -16.53 | 49.18 | -13.6 | -7.6 | -22.8 | -17.6 | -27.8 | 0.0000 |
| Lead Amp | 50 | -6 | 1000 | -31.43 | 136.58 | -24.7 | 0.6 | -22.7 | -2.9 | -7.5 | 0.0000 |
| Lead Amp | 100 | -6 | 1000 | -32.48 | 145.45 | -51.4 | 1.0 | -48.9 | -2.1 | -6.3 | 0.0000 |
| Screaming Amp | 0 | -6 | 1000 | -5.22 | 18.40 | -14.7 | -50.6 | -52.2 | -55.2 | -69.5 | 0.0000 |
| Screaming Amp | 50 | -6 | 1000 | -22.71 | 80.32 | -45.9 | -5.7 | -48.3 | -4.5 | -17.6 | 0.0000 |
| Screaming Amp | 100 | -6 | 1000 | -22.71 | 80.50 | -118.2 | -5.7 | -124.6 | -4.5 | -17.6 | 0.0000 |
| Power Amp | 0 | -6 | 1000 | 2.49 | 2.39 | -45.1 | -32.7 | -62.4 | -66.7 | -95.9 | 0.0000 |
| Power Amp | 50 | -6 | 1000 | -10.20 | 17.24 | -51.0 | -15.7 | -48.4 | -26.1 | -43.1 | 0.0000 |
| Power Amp | 100 | -6 | 1000 | -10.38 | 17.53 | -102.8 | -15.5 | -100.2 | -25.8 | -42.7 | 0.0000 |
| Subtle Saturation | 0 | -6 | 1000 | -1.00 | 0.02 | -178.7 | -74.0 | -175.2 | -92.3 | -101.9 | 0.0000 |
| Subtle Saturation | 50 | -6 | 1000 | -1.03 | 0.28 | -175.1 | -51.2 | -174.6 | -68.2 | -77.8 | 0.0000 |
| Subtle Saturation | 100 | -6 | 1000 | -1.75 | 5.47 | -182.7 | -25.3 | -189.7 | -42.2 | -51.8 | 0.0000 |
| Gentle Saturation | 0 | -6 | 1000 | -0.34 | 1.45 | -147.3 | -36.8 | -141.8 | -76.5 | -121.7 | 0.0000 |
| Gentle Saturation | 50 | -6 | 1000 | -4.58 | 21.32 | -151.6 | -13.6 | -152.8 | -28.3 | -46.6 | 0.0000 |
| Gentle Saturation | 100 | -6 | 1000 | -9.91 | 42.28 | -137.1 | -9.6 | -137.1 | -14.3 | -17.5 | 0.0000 |
| Heavy Saturation | 0 | -6 | 1000 | 0.42 | 6.56 | -161.3 | -23.9 | -153.5 | -37.8 | -47.1 | 0.0000 |
| Heavy Saturation | 50 | -6 | 1000 | -16.69 | 37.18 | -163.7 | -10.4 | -159.2 | -15.7 | -19.4 | 0.0000 |
| Heavy Saturation | 100 | -6 | 1000 | -20.81 | 42.19 | -99.9 | -9.7 | -99.9 | -14.3 | -17.6 | 0.0000 |
| Subtle Transformer | 0 | -6 | 1000 | -0.98 | 0.00 | -178.2 | -90.6 | -178.8 | -151.0 | -153.5 | 0.0000 |
| Subtle Transformer | 50 | -6 | 1000 | -0.92 | 0.00 | -185.1 | -90.5 | -176.2 | -153.5 | -155.8 | 0.0000 |
| Subtle Transformer | 100 | -6 | 1000 | -0.58 | 5.17 | -30.4 | -27.8 | -46.6 | -41.4 | -60.7 | 0.0000 |
| Gentle Transformer | 0 | -6 | 1000 | -0.96 | 0.00 | -148.2 | -90.5 | -147.2 | -143.2 | -145.2 | 0.0000 |
| Gentle Transformer | 50 | -6 | 1000 | -1.04 | 2.53 | -34.7 | -36.4 | -42.6 | -47.1 | -73.9 | 0.0000 |
| Gentle Transformer | 100 | -6 | 1000 | -8.94 | 36.35 | -43.8 | -10.2 | -48.1 | -16.0 | -20.8 | 0.0000 |
| Warm Transformer | 0 | -6 | 1000 | -1.15 | 0.00 | -150.6 | -90.9 | -148.9 | -155.6 | -146.3 | 0.0000 |
| Warm Transformer | 50 | -6 | 1000 | -6.97 | 26.30 | -42.5 | -12.1 | -40.5 | -21.6 | -34.3 | 0.0000 |
| Warm Transformer | 100 | -6 | 1000 | -15.03 | 44.49 | -56.4 | -9.5 | -56.2 | -13.8 | -16.7 | 0.0000 |
| Smudge | 0 | -6 | 1000 | -0.22 | 0.96 | -186.6 | -40.3 | -200.7 | -151.8 | -170.8 | 0.0000 |
| Smudge | 50 | -6 | 1000 | -31.78 | 3.87 | -33.7 | -30.6 | -42.1 | -44.1 | -47.1 | 0.0002 |
| Smudge | 100 | -6 | 1000 | -240.00 | 0.00 | -240.0 | -240.0 | -240.0 | -240.0 | -240.0 | 0.0000 |
| Breakdown | 0 | -6 | 1000 | -0.41 | 0.92 | -140.2 | -40.7 | -147.7 | -155.5 | -142.2 | 0.0000 |
| Breakdown | 50 | -6 | 1000 | -51.25 | 285.86 | -17.1 | -13.8 | -1.5 | 2.9 | 1.1 | 0.0000 |
| Breakdown | 100 | -6 | 1000 | -72.63 | 246.18 | -7.1 | -12.9 | -8.7 | -4.7 | -1.6 | 0.0005 |
| Foldback | 0 | -6 | 1000 | -0.48 | 1.94 | -240.0 | -34.3 | -240.0 | -70.0 | -107.9 | 0.0000 |
| Foldback | 50 | -6 | 1000 | -33.20 | 451.17 | -240.0 | -11.5 | -240.0 | 2.1 | 8.5 | 0.0000 |
| Foldback | 100 | -6 | 1000 | -34.33 | 221.25 | -240.0 | -7.0 | -240.0 | -40.2 | 4.9 | 0.0000 |
| Rectify | 0 | -6 | 1000 | -144.63 | 913145900.01 | 138.8 | 4.3 | 127.9 | 0.8 | -5.9 | 0.0008 |
| Rectify | 50 | -6 | 1000 | -154.38 | 90371478.14 | 111.4 | -0.1 | 111.3 | -0.1 | -0.1 | 0.0000 |
| Rectify | 100 | -6 | 1000 | -105.15 | 308696.53 | 62.0 | -0.1 | 62.0 | -0.1 | -0.1 | 0.0000 |
| Destroy | 0 | -6 | 1000 | -0.85 | 3.44 | -142.9 | -29.3 | -158.7 | -61.4 | -92.6 | 0.0000 |
| Destroy | 50 | -6 | 1000 | -26.55 | 5.36 | -124.2 | -25.4 | -129.6 | -56.6 | -49.3 | 0.0000 |
| Destroy | 100 | -6 | 1000 | -46.68 | 12.13 | -104.2 | -23.6 | -102.7 | -48.1 | -50.9 | 0.0000 |

| Style | gain 100 Hz | THD 100 Hz % | gain 1 kHz | THD 1 kHz % | gain 5 kHz | THD 5 kHz % |  (drive 50 %, −6 dBFS)
|---|---|---|---|---|---|---|
| Subtle Tube | -0.68 | 0.46 | -0.82 | 0.58 | -0.79 | 0.56 |
| Clean Tube | -0.66 | 5.53 | -0.86 | 5.81 | -0.56 | 5.91 |
| Warm Tube | -0.77 | 9.88 | -1.59 | 12.90 | -1.90 | 11.59 |
| Broken Tube | -19.93 | 37.83 | -19.38 | 37.32 | -19.38 | 30.83 |
| Subtle Tape | -0.90 | 0.02 | -0.90 | 0.02 | -0.90 | 0.02 |
| Clean Tape | -4.87 | 22.06 | -4.87 | 22.06 | -4.87 | 21.60 |
| Warm Tape | -3.11 | 18.73 | -4.45 | 23.55 | -4.54 | 20.52 |
| Old Tape | -11.30 | 36.12 | -11.29 | 34.85 | -11.04 | 3.32 |
| American Tweed Amp | -8.64 | 20.47 | -19.95 | 80.06 | -19.63 | 0.16 |
| American Plexi Amp | -19.78 | 75.83 | -20.49 | 61.47 | -26.35 | 0.00 |
| British Rock Amp | -23.37 | 81.03 | -20.44 | 51.51 | -25.69 | 0.08 |
| British Pop Amp | -23.95 | 115.54 | -24.45 | 104.04 | -18.03 | 0.20 |
| Smooth Amp | 5.45 | 4.79 | -12.61 | 39.72 | 2.29 | 2.66 |
| Crunchy Amp | -13.00 | 16.70 | -29.39 | 139.12 | -16.13 | 0.10 |
| Lead Amp | -5.32 | 8.53 | -31.43 | 136.58 | -20.07 | 0.39 |
| Screaming Amp | -20.64 | 72.98 | -22.71 | 80.32 | -13.41 | 0.56 |
| Power Amp | -12.20 | 31.94 | -10.20 | 17.24 | -22.52 | 0.26 |
| Subtle Saturation | -1.03 | 0.28 | -1.03 | 0.28 | -1.03 | 0.28 |
| Gentle Saturation | -4.58 | 21.32 | -4.58 | 21.32 | -4.58 | 20.97 |
| Heavy Saturation | -16.68 | 37.71 | -16.69 | 37.18 | -16.69 | 30.20 |
| Subtle Transformer | -0.84 | 0.00 | -0.92 | 0.00 | -0.86 | 0.00 |
| Gentle Transformer | -0.89 | 2.09 | -1.04 | 2.53 | -0.82 | 2.07 |
| Warm Transformer | -5.55 | 20.86 | -6.97 | 26.30 | -6.68 | 25.91 |
| Smudge | -33.99 | 58.68 | -31.78 | 3.87 | -18.88 | 0.41 |
| Breakdown | -37.16 | 71.18 | -51.25 | 285.86 | -44.12 | 262.33 |
| Foldback | -33.20 | 451.19 | -33.20 | 451.17 | -33.20 | 26.61 |
| Rectify | -164.20 | 44731808.83 | -154.38 | 90371478.14 | -146.27 | 20505707.92 |
| Destroy | -8.81 | 8.23 | -26.55 | 5.36 | -63.59 | 31.37 |

## 5. Pro-DS (de-esser)

Modes Single Vocal / Allround; Wide Band / Split Band; threshold −∞ … 0 dB; range 0 … 24 dB; look-ahead 0 … 15 ms;
detection band HP 2 … 20 kHz / LP 2 … 20 kHz; stereo link; oversampling Off / 2x / 4x. [EXEC]
- Latency 720 samples (= 15 ms, the maximum look-ahead) in wide band, 1232 in split band — independent of the
  look-ahead setting.
- 8 kHz / 12 kHz sine stairs (threshold −30, range 12, HP 6 kHz): no reduction below threshold, then −0.4 dB per dB
  (≈ 1.7:1): −2.1 dB at 6 dB over … −11.7 dB at 30 dB over. 1 kHz never reduced; 4 kHz reduced only in Allround
  (−1.6 … −6.4 dB at 6 … 30 dB over).
- Mixture (500 Hz at −12 dBFS + 8 kHz stair): **Single Vocal did not reduce at all** — its detection is relative to
  the voice (broadband) level. Allround reduced 2.1 … 11.7 dB; in wide band the 500 Hz part got exactly the same
  reduction, in split band only 0.2 … 0.7 dB (crossover leakage).

## 6. Pro-MB (multiband dynamics)

One band 300 Hz – 3 kHz, ratio 1, band level −12 dB (so the plot shows the band's crossover shape), slopes 6 / 12 /
24 / 48 dB/oct in the three processing modes. [EXEC] Latency: Dynamic Phase and Minimum Phase 960 samples
(20 ms), Linear Phase 4032. The band's level at its centre reaches −4.7 (6 dB), −8.0 (12), −11.1 (24), −12.0 (48)
dB — shallow slopes overlap so the −12 dB never fully develops; Dynamic Phase and Minimum Phase agree within 0.05 dB
from 24 dB/oct up; at 6 dB/oct Minimum Phase reaches −8.9 dB vs −4.7 dB in Dynamic Phase (different band split
topologies), Linear Phase −4.8 dB with wider skirts (−0.4 dB at 20 Hz).

| mode / slope | 40 Hz | 161 Hz | 321 Hz | 644 Hz | 1288 Hz | 2580 Hz | 5165 Hz | 10.3 kHz |
|---|---|---|---|---|---|---|---|---|
| Dynamic 6 | −0.07 | −0.91 | −2.53 | −4.52 | −4.69 | −2.79 | −1.00 | −0.22 |
| Dynamic 12 | −0.13 | −1.76 | −4.66 | −7.80 | −8.05 | −5.10 | −1.93 | −0.44 |
| Dynamic 24 | 0.00 | −0.51 | −4.81 | −10.86 | −11.15 | −5.78 | −0.63 | −0.02 |
| Dynamic 48 | 0.00 | −0.05 | −5.60 | −11.94 | −11.97 | −7.53 | −0.07 | 0.00 |
| Minimum 6 | −0.08 | −1.20 | −3.70 | −8.33 | −8.92 | −4.17 | −1.32 | −0.28 |
| Linear 6 | −0.79 | −2.47 | −3.74 | −4.74 | −4.81 | −3.89 | −2.62 | −1.55 |

## 7. Pro-G (gate / expander)

Styles Classic, Clean, Vocal, Guitar, Upward (+ Ducking in expert mode). Threshold −60 … 0, ratio 1 … 100,
range 0 … 100 dB, attack 0 … 1000 ms, release 0 … 5 s, hold 0 … 250 ms, knee 0 … 30 dB, look-ahead 0 … 10 ms. [EXEC]
Static (threshold −30 dB, 1 kHz stair −80 … 0): ratio 100 → full range below threshold, unity above (Classic
−100 dB, Clean −99.7); ratio 2 → expansion of exactly 1 dB per dB below the knee point, but the point where it starts
depends on the style (setting −30 dB): Classic −37.9, Clean −34.3, Vocal −30.2, Guitar −30.0 dB (Classic and Clean
probably use a hysteresis / close threshold below the open one — not confirmed); range 20 dB caps the reduction at
−20.0. Upward: no change below threshold, +2 dB at 0 dBFS (upward expansion above). Timing (−60 → −10 → −60 dBFS
burst, threshold −30, ratio 100):

| style | attack | release | hold | open: −1 dB after (ms) | close: −6 / −20 / −40 dB after (ms) |
|---|---|---|---|---|---|
| Classic | 0.1 | 150 | 0 | 0.0 | 39 / 147 / 1304 |
| Classic | 10 | 150 | 0 | 10.8 | 40 / 158 / 1145 |
| Classic | 0.1 | 1000 | 0 | 0.0 | 103 / 194 / 1304 |
| Classic | 0.1 | 150 | 100 | 0.0 | 87 / 147 / 1304 |
| Clean | 0.1 | 150 | 0 | 0.3 | 113 / 290 / 1250 |
| Clean | 0.1 | 1000 | 0 | 0.3 | 258 / 614 / 1491 |
| Vocal | 0.1 | 150 | 0 | 37.3 | 112 / 229 / 1147 |
| Guitar | 0.1 | 150 | 0 | 1.6 | 129 / 155 / 1175 |
| Guitar | 0.1 | 150 | 100 | 1.6 | 190 / 203 / 1175 |

(Vocal opens slowly — 37 ms to −1 dB even at attack 0.1 ms.)

## 8. Pro-R 2 (reverb)

Space 0.2 … 10 s, decay rate 25 … 400 %, distance, brightness, style (Modern, Vintage, Plate), character, thickness,
width, ducking, predelay (+ sync), auto gate, freeze, a 6-band decay-rate EQ and a 6-band post EQ (Pro-Q shapes).
Impulse responses (10 s, mix 100 %, predelay 0, brightness 0, distance 50 %); RT60 per octave from the slope of the
smoothed band energy between −5 and −35 dB (Gaussian octave filters): [EXEC]

| style | space | decay rate | onset ms | 125 | 250 | 500 | 1k | 2k | 4k | 8k |
|---|---|---|---|---|---|---|---|---|---|---|
| Modern | 0.5 s | 100 % | 6.67 | (5.8) | 0.50 | 0.46 | 0.43 | 0.40 | 0.39 | 0.38 |
| Modern | 2.5 s | 100 % | 6.67 | 2.08 | 2.63 | 2.32 | 2.22 | 2.06 | 1.92 | 1.80 |
| Modern | 6 s | 100 % | 6.67 | 6.63 | 6.44 | 5.69 | 5.04 | 4.70 | 4.20 | 3.68 |
| Modern | 2.5 s | 50 % | 6.67 | (2.8) | 1.27 | 1.15 | 1.06 | 1.06 | 0.98 | 0.95 |
| Modern | 2.5 s | 200 % | 6.67 | 5.52 | 5.38 | 4.69 | 4.31 | 3.89 | 3.46 | 2.99 |
| Vintage | 0.5 s | 100 % | 13.65 | 0.44 | 0.39 | 0.45 | 0.39 | 0.38 | 0.40 | 0.36 |
| Vintage | 2.5 s | 100 % | 13.65 | 1.98 | 1.96 | 1.91 | 2.03 | 1.81 | 1.89 | 1.82 |
| Vintage | 6 s | 100 % | 13.65 | 7.20 | 6.72 | 6.18 | 5.22 | 4.02 | 3.10 | 2.41 |
| Plate | 2.5 s | 100 % | 0.08 | 2.19 | 2.29 | 2.11 | 2.02 | 2.04 | 2.00 | 2.03 |
| Plate | 6 s | 100 % | 0.08 | 10.2 | 9.94 | 8.04 | 6.17 | 3.07 | 3.37 | 2.62 |

Reading: the Space value is roughly the mid-band (500 Hz – 1 kHz) RT60 for short and medium spaces; long spaces add a
natural high-frequency damping (Modern 6 s: 5.7 s at 500 Hz → 3.7 s at 8 kHz; Vintage stronger); Decay Rate scales
the decay times (50 % ≈ ×0.48, 200 % ≈ ×1.9). Plate starts at once (0.08 ms), Modern after 6.7 ms, Vintage after 13.7
ms. Left/right are decorrelated (correlation of the first second ≈ 0). Values in parentheses have little energy in
that band. Plate 0.5 s measured 3.7–4.2 s right after the 6 s Vintage capture — most likely the space parameter was
still gliding; excluded ("not confirmed").

## 9. Timeless 3 (delay) — basics

Delay time 5 ms … 5 s (+ sync, offset 50–200 %, per-side time pan), read mode Tape / Stretch, freeze, ping pong,
16 taps, feedback 0–200 % with pan / cross mix / invert, 4 filters (styles and shapes of their own, 6–48 dB/oct),
drive, diffuse, width, wet level, mix, plus the same modulation system as Saturn 2. Impulse at delay 350 ms, mix
100 %, drive and diffuse off (peak sample level of each echo, re the impulse): [EXEC]
- No latency; first echo at 351 ms left / 354.5 ms right in the default stereo setting (the right side runs ≈ 1 %
  longer, and each following echo keeps that ratio: 701 / 708 ms, 1051 / 1062 ms); the echo peak is −14.2 dB
  (the default filtering smears the impulse; Stretch: −15.8 dB).
- Feedback 50 %: −14.4, −23.8, −32.2, −37.9, −44.1, −50.6, −57.4, −64.5 dB (≈ 6–9 dB per repeat). Feedback 100 %:
  −14.2, −17.8, −20.2, −19.9, −20.0, −20.6, −21.4, −22.4 dB — the loop holds its level (≈ −0.5 to −1 dB per repeat:
  the loop's filters / saturation, not confirmed which).
- Ping pong L-R: odd echoes only on the left, even echoes only on the right (the other side ≥ 100 dB lower).

## 10. LYNX's own modules from these targets

### 10.1 Facet EQ (`EffectKind::facetEq`, `src/core/FacetEq.{h,cpp}`) — the EQ role
8 bands × (shape, frequency, gain, Q, slope) + output (±24 dB) + gain scale (0–200 %), 42 controls, zero latency.
Shapes: Bell, Low / High Shelf, Low / High Cut (6 … 96 dB/oct), Notch, Band Pass, Tilt Shelf. Parameter laws as
measured in §1.1 (10 Hz – 30 kHz, ±30 dB, Q 0.025 – 40 with 0.5 = 1). Prototypes exactly as fitted in §1.2.
Digital realisation (our design, from the published "matched second-order filter" idea): poles by impulse
invariance; the numerator is solved so the digital squared magnitude equals the analog one at DC, at Nyquist and at
the band frequency (squared-magnitude form A0·cos² + A1·sin² + A2·4·cos²·sin²); high-pass sections use a double
zero at DC matched at the band frequency; notches put their zeros on the unit circle; when a cut near Nyquist is not
realisable, the reciprocal section is designed and inverted. First-order sections are matched at DC and Nyquist.
Against the measured reference (897 responses at 48 kHz, 20 Hz – 20 kHz, where the reference is above −50 dB):
median error Bell 0.044 dB, shelves 0.006, tilt 0.009, cuts 0.055 / 0.074, notch 0.002, band pass 1.2 (all of it
at 18–20 kHz); worst cases are 10 kHz bands with low Q (up to 5.9 dB for a −24 dB, Q 0.5 bell at 10 kHz — the
reference keeps the analog shape to 20 kHz; Facet does not). The golden test (`tests/FabMeasureTests99.inc`):
89 points of §1.4 within 0.35 dB (worst 0.31 dB, band pass at 10 kHz).
UI: the generic LYNX effect window (knob grid) until a dedicated editor is designed (plugui93 layout system).
Not done: dynamic EQ, natural / linear phase modes, flat tilt, all pass, bell slopes, mid/side placement.

### 10.2 Apex Limiter (`EffectKind::apexLimiter`, `src/core/ApexLimiter.{h,cpp}`) — the mastering limiter role
Controls: gain 0–30 dB, ceiling −30 … 0 dB (default −0.1), look-ahead 0–5 ms, release 1–1000 ms, style (Clean,
Punch, Loud, Safe), true peak, channel link. Algorithm (ours): required gain per sample (ceiling / peak; with true
peak the peak includes 3 interpolated points per sample from a 4× windowed-sinc estimator) → running minimum over
look-ahead + 1 samples → moving average over the look-ahead (so the gain has reached its target when the peak leaves
the delay) → release on a fast and a slow stage mixed by style → safety clip at the ceiling. Constant latency
(5 ms + 4 samples = 244 at 48 kHz), like the reference whose latency also ignores its look-ahead knob (§3).
Tests: ceiling never exceeded for all styles / true peak on-off with 8 dB of limiting; below the ceiling the output
is the input delayed by exactly the latency (error < 1e-6); an fs/4 sine at 45° is held with its true peak at the
ceiling. Release to within 1 dB after an 8 dB, 1 s burst (release 150 ms): Clean 0.92 s, Punch 0.32, Loud 0.15,
Safe 1.63 s.

Both are in the mixer's effect list right after the existing EQ / limiter, saved as `facetEq` / `apexLimiter` in
`.lynx` projects. Plugin substitutes (round 98): a missing FabFilter Pro-Q now opens as Facet EQ and Pro-L as Apex Limiter (default
settings; their states are not translated).

## 11. Where I stopped / next steps

- Done: harness (`tools/fxmeasure`), parameter tables of all 14 plugins (tmp/r99/data/params), Pro-Q 4 shapes /
  slopes / modes / rates, Pro-C 3 static + timing, Pro-L 2 latency / ceiling / true peak / timing, Saturn 2
  harmonics, Pro-DS, Pro-MB crossovers, Pro-G, Pro-R 2 decay, Timeless 3 echoes (§9), notes for vocal96
  (tmp/r96/NOTES_from_r99.md), Facet EQ and Apex Limiter with tests.
- Not measured: Pro-Q 4 dynamic EQ and spectral dynamics, Pro-C 3 side-chain EQ / character / oversampling,
  Pro-L 2 dither and loudness metering, Saturn 2 modulation and multiband interplay, Pro-MB dynamics per band,
  Pro-R 2 decay-rate EQ, Volcano 3 / Twin 3 / Micro / One / Simplon beyond their parameter tables.
- Next: a dedicated Facet EQ editor (curve display, draggable nodes) following UI_STUDY_plugins93; natural-phase mode
  (exact analog phase via a short FIR correction) to close the 10 kHz gap; a compressor module against §2.
