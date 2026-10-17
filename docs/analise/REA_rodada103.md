# REA round 103 — Facet EQ editor, dynamic EQ and phase modes; LYNX Tune live pitch display

Branch `feature/facet103`. English-first; clean-room. The reference EQ (FabFilter Pro-Q 4, official trial installed by
the user) was only **hosted** with `tools/fxmeasure` (VST3 parameters in, audio out) — no disassembly, no strings, no
graphics or manual text. Raw captures stay in `tmp/r103` (never in git). Scripts: `tools/fxmeasure/py/pqdyn.py`.

## 1. Goal
1. Facet EQ (core/FacetEq.*, round 99): a dedicated interactive editor (analyser, draggable nodes, add / remove bands,
   Q on the wheel, shape menu, solo), per-band dynamic EQ (threshold / range, sidechain-able) and phase modes (zero
   latency / natural / linear, latency reported to PDC).
2. LYNX Tune (round 95): the live pitch display (detected pitch, target note, correction, scrolling pitch graph).

## 2. Measurements of the reference's dynamic EQ [EXEC]
Setup: one Bell band, 1 kHz, Q 1, gain 0 dB, Zero Latency, 48 kHz; 1 kHz sine stair −60 … 0 dBFS (sine peak), 2 dB
steps of 0.6 s, level read over the last 0.3 s of each step; bursts −50 → −6 → −50 dBFS for timing.

### 2.1 Static curve
- The per-band switch whose texts read "Dynamics Auto" (normalized < 0.5) / "Dynamics Manual" (≥ 0.5) behaves the
  other way round from its text: only with "Auto" (< 0.5) is the **threshold knob** used; with "Manual" (the default)
  the threshold is set automatically from the signal (−60 / −30 / −10 dB thresholds all gave the same curve). The
  measurements below use the knob.
- With threshold T and range R (−12 dB): gain change at T−4: −0.1 dB, T−2: −0.5, T: −1.3, T+2: −2.5, T+4: −4.1,
  T+6: −6.0, T+8: −8.0, T+10: −10.0, T+12: −12.0 (= R, then flat). So: **1 dB of gain per dB over the threshold (an
  infinite ratio), a ≈10 dB soft knee centred on T, clamped at the range**. A quadratic knee of width 10 dB
  ((x+5)²/20 for |x| < 5, x = level − T) reproduces every step within 0.1 dB [fit].
- Positive range mirrors it (+0.1 … +12.0 dB at the same levels): the band is boosted as the level rises.
- The static gain adds to it (gain +6, range −12: +6.0 below, −6.0 above).
- An off-centre tone (500 Hz, one octave below) starts acting ≈ 6 dB later and reaches the bell's −5.7 dB there: the
  detector listens through a band filter around the band (a 2nd-order band pass with the band's Q gives −5.1 dB one
  octave away) [fit, approximate].

### 2.2 Timing (gain envelope = |analytic(out)| / |analytic(in)|)
- Attack % → time to 63 % of the 12 dB move: 0 %: 1.2 ms, 25 %: 1.4 ms, 50 %: 3.5 ms, 75 %: 13 ms, 100 %: 38 ms
  (≈ 0.4 ms · 96^p, the bottom limited by the detector).
- Release % → exponential recovery time constant (from the dB curve after the drop): 0 %: ≈ 115 ms, 25 %: ≈ 160 ms,
  50 %: ≈ 270 ms, 75 %: ≈ 440 ms, 100 %: ≈ 720 ms.

### 2.3 Phase modes (from round 99, MEASURE_fabfilter §1.3)
Zero latency 0; natural phase 320 samples at 48 kHz (analog magnitude and phase); linear phase (medium) 5120.

## 3. LYNX's design (own) [model]
- Parameters appended at 42 … 63 (older projects load with every new control at the old behaviour: range 0 dB =
  static band, phase = zero latency, solo off, detector = input).
- Dynamic band (bell, shelves, tilt with range ≠ 0): detector = mid of the EQ input, or of the slot's sidechain
  when "Detector: Sidechain" is chosen and a sidechain track is routed (mixer slot menu "Sidechain from"); filtered
  by an RBJ band pass at the band frequency / Q (low pass for low shelf, high pass for high shelf), mean square
  over max(1 ms, 1.5 periods), level in sine-peak dB; gain law of §2.1; gain smoothed with attack 0.25 … 50 ms
  (default 3.5 ms) and release 20 ms … 4 s (default 283 ms). Zero-latency mode re-designs the band's sections every
  32 samples when the dynamic gain moved more than 0.01 dB.
- Natural phase: FIR (8192 taps at 48 kHz) of the analog prototype's complex response delayed by 192 samples,
  windowed by a half-Hann rise before the delay and a half-Hann decay after it, run as a uniformly partitioned FFT
  convolution with 128-sample blocks → latency 320 at 48 kHz (×2 up to 96 kHz, ×4 above).
- Linear phase: FIR of the analog magnitude, delay 4096, Hann window, 512-sample blocks → latency 4608 at 48 kHz.
- In the FIR modes the kernel is redesigned at most every 1024 samples (parameter changes, dynamic movement
  > 0.05 dB) and cross-faded over one convolution block.
- Solo: the band's region alone (the detector filter on the input), delayed by the mode's latency.
