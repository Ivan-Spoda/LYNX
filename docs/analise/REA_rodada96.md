# REA round 96 — LYNX's own mixing suite (vocal chain)

Goal: native LYNX effects for the vocal chain the girlfriend's projects use (tmp/gf/PLUGIN_QUEUE.md items 2, 3, 4/5, 6
and 9/12): a channel strip, an optical leveler, a de-esser, an air band + exciter and an analog saturator, each with
its own LYNX name, built **only from published DSP knowledge**. This round is not a port: there is no reference
binary. No third-party plugin, installer or preset was opened, run, inspected or rendered (several copies on the
laptop the queue came from are protected or unofficial; they were not touched). The products are named only in
docs/PLUGINS.md, nominatively ("in the role of …"), with a trademark note.

Code: `src/core/MixSuite.{h,cpp}` (no JUCE), tests `tests/MixSuiteTests96.inc`; `EffectKind::ridgeStrip`,
`glowLeveler`, `hushDeEsser`, `aether`, `ember` (appended to the enum). All five are in the mixer's effect list and
the F8 quick-add palette, saved in `.lynx` projects by id (`ridgeStrip`, `glowLeveler`, `hushDeEsser`, `aether`,
`ember`).

Evidence markers: **[LIT]** = published literature (cited), **[TEST]** = measured by the unit tests on LYNX's own
engine, **[design]** = our own choice.

## 1. Sources

- Biquads: R. Bristow-Johnson, *Cookbook formulae for audio EQ biquad filter coefficients* (peaking, shelves with the
  shelf slope S, low / high / band-pass). [LIT]
- Multirate: A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing* — interpolation / decimation by 2,
  half-band FIR filters (every second tap zero), Kaiser window and Kaiser's formula for β. [LIT]
- Dynamics: D. Giannoulis, M. Massberg, J. D. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and
  Analysis*, JAES 60(6), 2012: soft-knee gain computer (their eq. 4), smooth branching peak detector in the log
  domain, feed-forward topology. Gate / downward expander: the same gain computer below the threshold. [LIT]
- Electro-optical leveling: the behaviour published for electroluminescent-panel + photoresistor cells in feedback
  levelers (public service documentation and circuit analyses of the classic studio units): attack around 10 ms,
  release in two stages (about half the reduction recovered in roughly 50–60 ms, the rest over 0.5 to several
  seconds), the slow stage lengthening after long or heavy reduction; ratio roughly 3:1 in "compress" and much
  firmer in "limit". [LIT] The model in §3 is ours. [design]
- Waveshaping, exciters, tape emphasis: U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., ch. 4 (nonlinear
  processing: overdrive / distortion / tube curves, exciters and enhancers, tape pre- / de-emphasis); J. Pakarinen,
  D. T. Yeh, *A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers*, CMJ 33(2), 2009
  (asymmetric curves give even harmonics; oversampling against aliasing). [LIT]
- Console channel EQ "brown" vs "black" curve families: the public descriptions of the two classic console EQ
  generations — constant-Q bells and plain shelves on one, proportional-Q bells (narrower as the gain grows) and
  shelves with a small overshoot dip on the other, ±15 vs ±18 dB. [LIT] Our curves are cookbook biquads chosen to
  show those traits, not measurements of any unit. [design]

## 2. Shared pieces

**Oversampler** (`mixsuite::Oversampler`): 2x, 4x or 8x by cascaded half-band FIR stages of 95, 23 and 15 taps
(N = 4k + 3, so the centre index c is odd and the odd phase of each interpolator is a pure delay), Kaiser β 10 / 9 /
8.5, designed in the constructor. A stage's round trip costs c samples at its input rate, so the three paths cost
47, 52.5 and 53.75 base samples; a short pad at the highest rate makes each whole and a base-rate pad brings every
factor to the **same 55 samples**, so changing the quality never moves the PDC. [design, TEST]
- Impulse peak exactly at 55 for 2x / 4x / 8x; identity error on a 1 kHz sine −126 / −109 / −119 dB. [TEST]

**Latency contract** (REA round 87): every latency is fixed at `prepare()` and never depends on parameters —
Ridge 0, Glow 0, Hush 2 ms (88 samples at 44.1 kHz, 96 at 48 kHz), Aether 55, Ember 55. The dry paths of the mix
knobs are delayed by the same amount, so mix = 0 returns the input exactly (error < 1e-6). [TEST]

**Meters**: `Effect::controlOutput (k)` — 0 = gain reduction (0..1 over 0..30 dB; Ridge: compressor), 1 = Ridge's
gate reduction, 2 / 3 = input / output peak of the last block. They are relaxed atomics (written by the audio thread,
readable by the UI) and also make the suite usable as modulation sources.

**Real time**: all buffers are fixed-size members (`std::array`); the oversampled effects process in chunks of 2048
so any host block size works without allocation.

## 3. The effects

### Ridge Strip (channel strip)
Signal flow: input trim → HPF (3rd-order Butterworth, 16–350 Hz, 18 dB/oct) → LPF (2nd-order, 22–3 kHz, 12 dB/oct)
→ EQ and dynamics in the chosen order (EQ → dyn or dyn → EQ) → output trim / phase. With *Filters to SC* the
filters leave the audio path and shape only the detector's input (de-booming a vocal's compression without changing
its tone).
- EQ: LF 30–450 Hz (shelf or bell), LMF 200 Hz–2.5 kHz and HMF 600 Hz–7 kHz bells with Q 0.5–2.5, HF 1.5–16 kHz
  (shelf or bell). *Brown*: ±15 dB, constant Q, shelves with S = 1. *Black*: ±18 dB, Q × (0.5 + |gain| / 12)
  (proportional Q), shelves with S = 1.6 (overshoot dip). [design]
  - Measured [TEST]: brown LMF +12.00 dB at 1 kHz (+3.93 dB an octave up); the same knob in black +14.40 dB with
    +2.42 dB an octave up (narrower); for a 2 dB boost black is wider than brown. HPF at 100 Hz: −3.01 dB at 100 Hz,
    −18.13 dB at 50 Hz, −36.13 dB at 25 Hz. A processed 12 kHz tone matches the computed curve to 0.01 dB.
- Compressor: feed-forward, 6 dB soft knee, ratio 1:1…20:1 and ∞, threshold −40…0 dBFS, attack 10 ms (1 ms with
  *Fast attack*), release 0.1–4 s, parallel *Comp. mix*; detector = stereo-linked peak with instant attack and a
  30 ms release (keeps low-note ripple out of the gain).
  - Measured GR curve, threshold −20 dB, 4:1 [TEST]: in −40/−30/−20/−10/0 dBFS → out −40.00/−30.00/−20.56/−17.49/
    −14.99 (static curve −40/−30/−20.56/−17.50/−15.00).
- Gate / expander: range 0–60 dB (0 = off), threshold −80…0 dBFS with 4 dB hysteresis (gate), 1:2 downward
  expansion limited to the range (expander), attack 1.5 ms (0.1 ms fast), release 0.1–4 s.
  - Measured [TEST]: gate at −40 dBFS / 60 dB: a −50 dB tone comes out below −100 dB, a −30 dB tone unchanged; the
    expander puts a −50 dB tone at −59.3 dB (ideal −60).

### Glow Leveler (optical leveler)
Feedback topology (the cell sees the output): sidechain (optional high-shelf *Emphasis*, up to +10 dB at 2.5 kHz) →
"panel" envelope (1 ms rise, 30 ms fall) → light = (sidechain gain × panel)^γ, γ = 2 (compress) or 9 (limit), so the
asymptotic ratio is 1 + γ → cell = 0.7 × fast conductance (10 ms attack / 50 ms release) + 0.3 × slow conductance
(400 ms attack; release 0.4 s, growing to 5 s with a *memory* that charges in ~2 s of leveling and discharges in
~8 s) → gain = 1 / (1 + conductance). Peak reduction = sidechain gain −20…+40 dB; Gain = 0…+30 dB make-up; Mix.
[design on LIT behaviour]
- Measured [TEST] (peak reduction 60 %): compress −12 → 0 dBFS in gives −16.76 → −11.20 dB out (ratio 2.2:1),
  limit −15.95 → −14.06 (6.4:1). After a 0.5 s burst (12.8 dB GR): 56 % left after 0.1 s, 31 % after 0.5 s, 17 %
  after 1.5 s; after a 6 s burst: 61 / 44 / 37 % — the release is two-staged and program dependent.

### Hush De-esser
2 ms lookahead delay (always present → constant latency; with *Lookahead* off the detector hears the delayed signal
too). Detector: 2nd-order high-pass or band-pass (Q 1.4) at 2–16 kHz, stereo-linked peak envelope (instant attack,
release 10–250 ms); reduction = (level − threshold) with a 3 dB knee, capped at *Range* (0–24 dB), smoothed with an
attack of a third of the lookahead. *Split*: complementary split (low = 2nd-order low-pass, high = input − low, so
the bands sum back exactly) with the reduction on the high band only; *Wideband*: on the whole signal; *Listen*:
the detector band.
- Measured [TEST] (threshold −40 dB, range 12 dB, split at 2.83 kHz; 300 Hz + 7 kHz tones at −10 dB): 300 Hz
  −0.02 dB, 7 kHz −15.3 dB (the split's low-pass leakage adds to the 12 dB of gain), GR meter 12.0 dB; wideband ducks
  the 300 Hz tone by 12.0 dB; a dull signal passes as a pure 2 ms delay (error 2.8e-7); listen: 300 Hz −38.7 dB,
  7 kHz −0.1 dB.

### Aether (air band + exciter)
Wet = broad high shelf (S = 0.6, 5–20 kHz, 0…+15 dB) on the latency-aligned input + harmonics: 2nd-order high-pass at
the exciter frequency (1–12 kHz) → 4x oversampled generator (even part (√(1 + 16x²) − 1)/4 ≈ 2x², odd part
3·(x − tanh(3x)/3), blended by *Character*) → the same high-pass again (removes the DC and low intermodulation) ×
*Harmonics*. Mix and output.
- Measured [TEST]: neutral = exact 55-sample delay; +10 dB air at 10 kHz → +8.78 dB at 16 kHz, 0.00 dB at 200 Hz;
  exciter at 3 kHz on a 5 kHz tone: H2 −5.0 dB (even); a 300 Hz tone gets no 2nd harmonic (−110 dB); odd character:
  H3 −11.8 dB, H2 −155 dB.

### Ember Saturator
Low cut (pre, 20–500 Hz) → (tape: +6 dB pre-emphasis shelf at 3 kHz) → drive 0…+36 dB → oversampled curve (2x / 4x /
8x) → DC blocker (8 Hz) → auto gain (inverse RMS gain of the curve on a −12 dBFS sine at that drive) → (tape:
de-emphasis) → tone tilt (±6 dB shelves at 1 kHz) → high cut (post, 2–20 kHz) → mix with the delayed dry → output.
Curves (unit slope at 0): tube (tanh(x + 0.35) − tanh 0.35)/(1 − tanh² 0.35) (asymmetric), tape x/√(1 + x²),
transistor x/(1 + x⁴)^¼, fuzz (near-hard positive clip at 0.6, tanh(1.5x)/1.5 negative). [design on LIT]
- Measured THD on a −12 dBFS 1 kHz sine at +18 dB drive [TEST]: tube 20.3 % (H2 −18.0, H3 −16.6 dB), tape 13.4 %
  (H2 −156, H3 −17.7), transistor 19.8 % (H3 −14.3), fuzz 28.5 % (H2 −29.1, H3 −11.7); at −40 dBFS without drive all
  below 0.17 % (tube's asymmetry gives a little H2 even there).
- Aliasing, 15 kHz at −6 dBFS, transistor, +24 dB drive [TEST]: worst alias bin (0.9 / 1.2 / 13.2 / 16.8 kHz)
  −14.9 dB at 2x, −102.4 dB at 8x.

## 4. UI
The editor windows use the plugin layout system of round 93 (`src/core/PluginLayout.*`, generated grids until the
arrangements below land); pt-BR strings for every caption.

## 5. Verification
- build-local with the FL Data dir after merging main 90f0cce: 1169845 checks, 0 failures.
- `--rtstress` (Windows build, Ryzen 5 3600, WASAPI 48 kHz / 480 samples), demo project with all five effects at
  their defaults on mixer tracks 1-8 (40 instances), 20 s: 2000 callbacks, 0 over the deadline, 0 late device calls,
  average load 13.0 %, max 34.8 %; heaviest unit an Ember Saturator at 2.2 % (peak 0.1 ms). [TEST]

## Where I stopped / next steps
- Batch 1 (engines, tests, docs, website list) landed via tmp/coord/land.sh vocal96.
- Batch 2 (not done): editor arrangements (sections, value read-outs, GR meter, pt-BR captions) on round 93's layout
  system (src/core/PluginLayout.*, feature/plugui93), which is not on main yet; until then the five effects use
  LYNX's generic knob editor. Batch 1 adds no tr() strings (engines live in src/core).
- subst98 (plugin substitution table) and ffmeas99 (FabFilter measurements, tmp/r99) had nothing landed: register the
  suite there / fit de-esser and saturator to the measurements once they land.
