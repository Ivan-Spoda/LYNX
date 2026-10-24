# REA round 144b (parity144b): Prism Multiband — Pro-MB's processing modes, oversampling, band pan, analyser

2026-10-24. Sub-round of parity144: the lowest-scoring FabFilter pair, Prism Multiband ← Pro-MB (config 7 / design 7
after round 135). Evidence markers: [EXEC] measured here on the official Pro-MB trial, hosted black-box in our
measurement host (`tools/fxmeasure`: public VST3 parameters, value texts, audio in / out, the component state as a
DAW stores it); [model] LYNX's own choice. Nothing was decompiled, no graphics, presets or names copied. Raw files:
`D:\lynx-work\parity144\b\` (m1.txt / m1.log / resp.txt: impulses per mode, st*.bin: saved states).

## 1. What the trial does [EXEC]

Host parameters: `Processing Mode` (3 steps: 0 Linear Phase, 0.5 Dynamic Phase = default, 1 Minimum Phase),
`Oversampling` (0 Off, 0.5 2x, 1 4x), `Lookahead Enabled` (on by default). Saved state (FabF v2, 151 values): slot 139
= the mode index (0 / 1 / 2), slot 140 = the oversampling index (0 / 1 / 2), band pan (slot 22b + 14) = −1 … +1.

Reported latency at 48 kHz (one band 30 Hz – 1 kHz, level −12 dB, range 0):

| Mode | look-ahead on | look-ahead off | 2x | 4x |
|---|---|---|---|---|
| Dynamic phase | 960 | 0 | 994 | 1000 |
| Minimum phase | 960 | 0 | 994 | 1000 |
| Linear phase | 4032 | 3072 | 4066 | 4072 |

The look-ahead switch adds a fixed 20 ms (960) whatever the bands' look-ahead; linear phase adds 3072 samples (64 ms)
whatever the crossover (1 kHz or 100 Hz) or slope; oversampling adds 34 (2x) / 40 (4x) samples and pre-rings
(energy before the impulse −19 … −22 dB) — linear-phase half-band filters.

Magnitude of the band level −12 dB (crossover 1 kHz, 24 dB/oct), dB at 50 / 500 / 1k / 2k / 10k Hz: **−12.0 / −10.6 /
−4.1 / −0.4 / 0.0 in all three modes** (and with 2x / 4x); 6 / 12 / 48 dB/oct give −4.1 at 1 kHz too (wider / narrower
skirts). The modes differ only in phase (at 1 kHz):

| | level 0 dB | level −12 dB |
|---|---|---|
| Dynamic | the input, exactly | +53° (24 dB/oct; +31° at 6, +37° at 12, +70° at 48) |
| Minimum | an all-pass: flat, −180° at the crossover (−90° at 12 dB/oct, 0° at 48) | −180° |
| Linear | the input delayed, exactly | 0° (symmetric impulse, pre-ringing) |

So: linear = the crossover magnitudes with no phase; minimum = the classic Linkwitz-Riley sum (all-pass at rest);
dynamic = transparent at rest, a minimum-phase shelf-like response when a band works.

Band pan: the value texts read "Mid: 0 dB / Side: −6.02 dB" at 0.25 … "Mid: −INF / Side: 0 dB" at 1: a mid / side
balance with the same law as Prism's left / right PAN (the far side's gain = 1 − |pan|).

## 2. LYNX (src/core/DynamicsSuite.cpp `prism::`, editor `PrismEditor` in src/ui/ProEditors.cpp)

Appended parameters (defaults = the old sound): `Processing` (the reference's value law: 0 linear, 0.5 dynamic, 1
minimum), `Oversampling` (0 off, 0.5 2x, 1 4x), `Pan mode` (left / right, mid / side).

- **Dynamic phase** = the old engine (input + each band's gain change), bit-exact with older projects.
- **Linear phase** [model, latency as measured]: one stereo FFT convolver per band below the top one (the existing
  `peqfl::FftConvolver`, FFT 4096 at ≤ 50 kHz, ×2 per doubling of the rate; the FIR is the band's magnitude in the IIR
  tree, zero phase); the top band is the delayed input minus the others, so the bands always add up to the input
  (transparent at rest, checked to 1e-5). Latency 3072 samples at 48 kHz (+ the look-ahead), the detectors hear the
  linear bands (and a delayed side chain). Buffers allocated in prepare().
- **Minimum phase**: the sum of the bands, each lower band through the all-passes of the crossovers above it (flat
  at rest, the crossovers' phase always there).
- **Oversampling**: the split, the detectors and the gains stay at the base rate; the dry signal, the bands and the
  gains are interpolated by the same half-band filters (`mixsuite::Oversampler`), the gains applied and summed at the
  high rate and decimated back (no aliasing from fast gain changes). Latency + the oversampler's (the same for 2x /
  4x); 4x only up to 400 kHz.
- **Band pan mid / side**: PAN's two gains on the band's mid and side.
- The editor's curve follows the mode (linear: magnitudes; minimum: the all-pass compensated sum).
- Pro-MB states: mode, oversampling and every band pan translated (pan mode = mid / side for translated states).
- Editor: the bottom bar now reads like the reference's — Phase (Linear / Dynamic / Minimum), Oversampling,
  Lookahead, Analyzer, then Mix / In / Out; the crossover slope and the side-chain input moved into the band panel
  (where the reference has them, per band), with the L/R | M/S pan switch. The analyser menu gained Resolution
  (Low … Maximum: a shorter, faster window) and Tilt (0 … 6 dB/oct), shared by the FabFilter-role editors.
- Tests: tests/Parity144bTests.inc (latencies, transparency, magnitudes against the measured table per mode, the
  editor curve, oversampled magnitudes, translation, mid / side pan).

## Where I stopped

- Dynamic phase keeps the old construction, and it does not match the reference when a band works: a −12 dB low
  band (1 kHz, 24 dB/oct) gives +2.76 dB at 1 kHz (Pro-MB −4.1) and −11.0 dB at 50 Hz (Pro-MB −12.0), because the
  band's phase-shifted part is added to the dry input. Linear and minimum phase match the table within 0.05 dB. Matching it needs a minimum-phase
  dynamic-EQ band structure; changing it would change older projects' sound, so it is left for a DSP round.
- Not done: per-band crossover slopes, side-chain filtering, per-band stereo link (Pro-MB's expert panel); free band
  placement with gaps (bands stay contiguous).
