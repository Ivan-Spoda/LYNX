# REA round 108 — LYNX's dynamics suite (Crest Compressor, Prism Multiband, Sentry Gate, Ember multiband)

Goal: LYNX's own native dynamics processors in the roles of the reference mixing plugins (a style compressor, a
multiband dynamics, a gate / expander, a multiband saturator), designed against our black-box measurements
(docs/analise/MEASURE_fabfilter.md §2, §4, §6, §7 and the new §12), registered as substitutes of the missing
FabFilter plugins with their saved settings translated.

Rules followed: the official trials installed on this PC were only hosted through the VST3 API (tools/fxmeasure):
parameters set through the API, test signals in, audio out, and the plugin's own saved state compared before / after
a parameter change. No disassembly, no decompilation, no string dumps; nothing copied from manuals, presets or
graphics. Evidence markers: [EXEC] our measurement, [fit] a fitted model with its residual, [model] our own design,
"not confirmed" for what we could not establish. Raw captures stay in tmp/r108 (never in git).

## 1. New measurements (script `tools/fxmeasure/py/dyn108_meas.py`) [EXEC]

Reported in MEASURE_fabfilter.md §12 (side-chain EQ, character, oversampling of the compressor; per-band dynamics
of the multiband; the multiband saturator's crossover sum; the state layouts). Summary:

- **Compressor side-chain EQ**: the detector is filtered by the same analog prototypes as the reference EQ (§1.2):
  a +12 dB bell at 1 kHz moves the onset of a 1 kHz tone exactly 12 dB lower, a 1.18 dB shift for a 5 kHz tone
  (the bell's value there); a 12 dB/oct low cut at 1 kHz moves a 1 kHz tone 3.01 dB and keeps a 100 Hz tone 20 dB
  over the threshold uncompressed; the side-chain level is a pure shift (+6 dB = onset 6 dB lower).
- **Compressor character** (no compression): drive is applied before the curve and taken back after it (the
  fundamental stays at 0 dB until the curve saturates). Tube: perfectly clean up to about +9 dBFS into the curve,
  then even + odd (+18 dBFS: H2 −17.6, H3 −22.0 dB, fundamental −1.34 dB); a tiny HF lift (+0.32 dB at 20 kHz).
  Diode: odd harmonics from low levels, H3 growing with the square of the level (−89.6 / −65.7 / −42.3 dB at
  −18 / −6 / +6 dBFS into the curve), the fundamental slightly *expanding* (+0.2 dB at +6), asymmetric at the top.
  Bright: second harmonic from very low levels (−66 dB at −30 dBFS), odd harmonics from about −6 dBFS. Pre and
  post routing measure the same on a steady tone.
- **Compressor oversampling**: latency 34 / 40 / 44 / 46 / 48 samples (2x … 32x at 48 kHz); the pass band is the
  same for every factor (−1.12 dB at 22 kHz, −3.8 at 23 kHz); a fast compressor on a 15 kHz tone leaves spurious
  components at −64.6 dB without oversampling, −68.9 (2x), −101.6 (4x), −126.8 (32x).
- **Multiband per-band dynamics** (one band 300 Hz – 3 kHz, 24 dB/oct, threshold −30, ratio 4): compression
  −1.34 / −5.70 / −9.98 / −14.11 / −18.03 dB at −28 / −22 / −16 / −10 / −4 dBFS on a 1 kHz tone (the ideal hard-knee
  curve, less what the band split leaves outside the band); 100 Hz untouched. The *range* caps the gain change and
  its sign picks the direction: compression with range −12 caps at −11.5; compression with a positive range lifts
  quiet signals (upward compression, see §1.1); expansion with a negative range is downward expansion below the
  threshold (−10.05 dB at −34 dBFS, cap −25.9 for −30); expansion with a positive range is upward expansion above it
  (+5.78 dB at −22 dBFS, cap +11.87 for +12). Attack % → 50 % of the reduction after 0.58 ms (0 %), 0.83 (20 %),
  11.3 (50 %), 168 ms (100 %); release % → half the recovery after about 90 ms (0 %), 120 (20 %), 330 (50 %) and
  well over a second (100 %). Look-ahead moves the attack earlier; the latency is 960 samples whatever it says.
- **Multiband saturator crossovers**: with clean bands the bands sum flat (±0.05 dB); the phase is that of
  Linkwitz-Riley crossovers (180° per crossover at 12 dB/oct, 360° at 24, 720° at 48); 6 dB/oct has no phase shift
  (a complementary split).
- **State layouts** (§5).

### 1.1 Upward compression in the multiband (not reproduced)
The reference's upward compression (compression mode, positive range) does not follow the textbook mirror
(gain = (threshold − level)(1 − 1/R)): its slope grows with the range — measured gain ≈ range·(R − 1)·(threshold −
level)/36 dB, capped at the range, at threshold −30 with ranges +12 / +24 and ratios 1.5 … 100. Whether the 36 is a
constant or depends on the threshold is not confirmed (one threshold measured). Prism uses the textbook mirror.

## 2. Crest Compressor (`EffectKind::crestCompressor`, core/DynamicsSuite.h) [model]

38 controls: style (11), threshold −60 … 0, ratio 1 … 100 (100^(v^2.2)), knee 0 … 72, range 0 … 60, attack
0.005 … 250 ms, release 10 ms … 2.5 s, auto release, hold 0 … 500 ms, look-ahead 0 … 20 ms with a *maximum
look-ahead* switch (off / 1 / 2 / 5 / 10 / 20 ms) that sets the latency (as the reference: §2), character (off /
warm / crisp / bright) pre or post with drive ±24 dB, side-chain level ±36 dB, external side chain, listen, stereo
link, a 3-band side-chain EQ (Facet EQ's prototypes and laws: shape, frequency, gain, Q, slope), oversampling off /
2x / 4x / 8x, auto gain, mix, input and output ±36 dB.

Structure: feed-forward; the level detector runs in the log domain with branching attack / release (Giannoulis et
al., JAES 2012 §3) on the side chain (peak of the two channels blended by the stereo link); the gain computer is the
soft-knee curve of the same paper, plus the style's own curve family; the gain is delayed against the side chain by
(maximum look-ahead − look-ahead). With oversampling the side chain is upsampled through the same half-band path as
the audio, the detector runs at the high rate and the gain multiplies the upsampled audio (with the character
stage), so the gain modulation does not alias.

Style families (fitted; τ = scale × knob, release τ = scale × 200 ms × (knob / 200 ms)^exp, knee = add + scale ×
knob):

| style | attack scale | release scale | release exp | knee add | knee scale | curve |
|---|---|---|---|---|---|---|
| Clean | 0.265 | 1.27 | 0.77 | 6 | 0.50 | downward |
| Allround | 0.56 | 0.94 | 0.15 | 6 | 1.00 | downward |
| Punch | 0.31 | 1.10 | 0.65 | 4 | 0.80 | downward |
| Upward | 0.31 | 2.50 | 1.00 | 0 | 1.00 | lifts toward threshold − 10.5 dB, holds above |
| Two-way | 0.235 | (1 ms) | 0 | 0 | 1.00 | offset −5.75·s, upward below thr − 6 (0.473·s), downward above thr + 4 (0.808·s); peak over 1.3 ms |
| Vari-mu | 0.335 | 0.65 | 0.98 | 0 | 1.00 | s·o·share(s, o), the measured share table (§2.1 data / 0.96) |
| Classic | 0.30 | 1.57 | 0.53 | 4 | 0.17 | downward |
| Opto | 0.32 | 1.52 | 0.88 | 4 | 0.65 | downward |
| Vocal | 1.08 | 1.70 | 0.04 | 0 | 1.00 | softplus: −min(range, s'·K·ln(1 + e^((x − thr + 2.1)/K))), s' = min(1, 1.1667 s), K = 8 + knee/2 |
| Bus | 0.24 | 3.57 | 0.19 | 0 | 1.00 | downward |
| Pumping | 0.08 | 2.36 | 1.00 | 0 | 1.00 | downward |

Residuals against §2 (1 kHz stairs, our C++ engine through the measurement protocol): static curves within 1.0 dB
everywhere for every style at ratio 4 / knee 0 (Opto, Punch, Bus, Pumping, Upward, Two-way within 0.4 dB), within
1 dB for the knee 12 / 48 and ratio 1.5 / 100 rows we fitted; 50 % attack and release times within ~15 % at
attack 10 / release 200 for every style. Not matched: the long 90 % release tails (the reference's release is
closer to exponential in the gain: Clean 90 % at 308 ms vs our 182), Classic's slow second attack stage, the
burst reduction of Vari-mu (the reference reduces less on a long 24 dB overshoot than its static curve says) and
Upward's burst behaviour.

Character [fit to §12.2]: warm = linear to +6.5 / −3.0 (positive / negative), then tanh limits of 2.0 / 1.5;
crisp = u = x + 0.0083x³, then tanh limits 16 / 12; bright = x + 0.007·0.1·(√(1 + (x/0.1)²) − 1), then
u / (1 + |u/L|^1.5)^(1/1.5) with L = 7.75 / 5.5. The drive multiplies before and divides after. Against §12.2: warm
at +18 dBFS H2 −17.3 / H3 −21.9 / fundamental −1.59 (measured −17.6 / −22.0 / −1.34); crisp H3 −91.8 / −67.8 /
−44.2 at −18 / −6 / +6 (−89.6 / −65.7 / −42.3); bright H2 −64.9 / −54.4 / −47.0 / −36.6 at −30 / −18 / −6 / +6
(−66.1 / −56.3 / −50.1 / −34.7).

## 3. Prism Multiband (`EffectKind::prismMultiband`) [model]

Four bands from three Linkwitz-Riley crossovers (12 / 24 / 48 dB/oct) in a tree; each band: threshold −90 … 0
(−90·(1 − √v)), ratio, range ±30 dB (sign = direction), compress / expand, knee 0 … 48, attack and release on the
reference's 0 … 100 % scale (detector τ = 0.11 + 89·a^3.9 ms; 130 + 3500·r³ ms), level ±30 dB, on, solo; global look-ahead
0 … 20 ms (latency only when > 0), mix, input, output. **Transparent at rest**: the output is the input plus, for each
band, (gain − 1) × the band — a band without gain change leaves the signal bit-exact (the reference's "dynamic
phase" idea in our own construction: the crossovers' phase only shows where a band works). Per-band detector: log-
domain branching level of the band (peak of the two channels).

Against §12.4 (same settings): −1.0 … −18 dB within 1.5 dB of the measured band compression, 100 Hz untouched,
expansion and upward expansion within the measured shape; attack 50 % and release 0 % times inside the measured
ranges. The band level alone gives −11.x dB in the band's middle at 24 dB/oct (measured −11.15).

## 4. Sentry Gate (`EffectKind::sentryGate`) [model]

18 controls: style (Classic, Clean, Vocal, Guitar, Upward), threshold −60 … 0, ratio 1 … 100, range 0 … 100 dB,
attack 0 … 1 s (1000·v⁴, the reference's law), release 0 … 5 s (5000·v³), hold 0 … 250 ms, knee 0 … 30, look-ahead
0 … 10 ms with an on switch (latency 10 ms when on), side-chain high / low pass 5 Hz … 30 kHz (the reference's log
law), external side chain, listen, upward threshold −30 … 0 / ratio 1 … 3 (Upward style), dry 0 … 100 %, output ±24.

Model: peak detector (instant attack, 5 ms release) → expander curve below a start point that sits
offset / (ratio − 1) under the threshold (offset by style: Classic 7.9, Clean 4.3, Vocal 0.2, Guitar 0 dB — the §7
measurement at ratio 2; a high-ratio gate opens right at the threshold) → the gain moves in the linear domain with
the attack when opening (Vocal never faster than 17 ms, Guitar 0.7 ms) and, after hold × style share, with
release τ = base + scale × knob (Classic 45 ms + 0.10×, Clean 120 + 0.25×, Vocal 100 + 0.20×, Guitar waits 110 ms
then 20 + 0.05×). Upward: no gating, upward expansion above the upward threshold.

## 5. FabFilter state translation (core/FabFilterStates.h) [EXEC + model]

The component state (what FL stores for a VST3, and what LYNX's own host wraps as base64 in `<IComponent>`) is
"FFBS" + version + count (Pro-C 3: v1, 100; Saturn 2: v1, 951) or "FabF" + version + name length + preset name +
one word + count (Pro-MB: v2, 151; Pro-G: v2, 34), then one little-endian float per VST3 parameter in the parameter
list's order. Observed by setting each parameter through the API to 11 values and saving the state: the float that
moved is the parameter's index; its value is the plug-in's own unit — dB (threshold, knee, range, gains, drive),
milliseconds (look-ahead), log2(Hz) (frequencies: 9.9658 = 1 kHz), enumeration indices (styles, shapes, slopes,
oversampling), percent (Pro-MB attack / release, Saturn mixes), or the normalized value (ratio, attack, release, Q,
some levels in a −1 … 1 encoding: v ≥ −0.6 → 36·v dB, below → 0.0832·((v + 1)/0.4)² linear).

Translators (run only when the header and the count match): Pro-C 3 → Crest (27+ controls incl. 3 side-chain EQ
bands), Pro-MB → Prism (enabled bands laid out low → high, gaps become bypassed bands), Pro-G → Sentry (every
control), Saturn 2 → Ember multiband (bands ≤ 4, crossovers, style family, drive 0 … 100 % → 0 … 30 dB, mixes,
levels). Laws without a closed form use the 11-point value ↔ text tables read through the API. The original state is
kept byte for byte in the slot (`substituteFor`), so the project reopens with the real plugin where it exists.
Pro-C 2 / Saturn 1 states (other layouts) keep the substitute's defaults.

## 6. Ember multiband (extends round 96's saturator) [model]

19 controls appended (older projects load with one band = the old behaviour, bit for bit): bands 1 … 4, three
crossovers 30 Hz … 18 kHz, slope 12 / 24 / 48 dB/oct, band 1 mix / level, and style / drive / mix / level for bands
2 … 4 (band 1 = the existing Style and Drive). Linkwitz-Riley tree with the lower bands passed through the higher
splits' all-passes, so the bands sum flat (test: ±0.25 dB of the one-band path at 60 Hz … 12 kHz, all slopes);
each band oversampled on its own with its own auto gain; low cut before, tone / high cut / mix / output after.

## 7. UI

Own panels (PluginLayout, dark / red): Crest — transfer curve with the live input dot, scrolling gain-reduction
history over the input / output levels, GR meter, level bars, style grid, character, side-chain section and the
side-chain EQ curve with its three bands; Prism — the band display (crossovers, live per-band gain change bars and
the resulting response curve) and four band columns; Sentry — gate curve with the live dot, GR history and meter;
Ember — a Bands section (split display, band count, slope, crossovers, per-band rows shown with > 1 band). The live
graphs are in src/ui/DynamicsDisplays.cpp; meters per effect slot went from 2 to 8 (ProcessTimes::meterOutputs).
pt-BR strings for every new caption.

## 8. Where I stopped / next steps

- Done: measurements (§1 / MEASURE §12), the three engines, Ember multiband, translators, substitutes, panels,
  tests (tests/DynamicsTests108.inc), pt-BR, docs/PLUGINS.md.
- Not done / ideas: the reference's release tails (closer to exponential in gain), Classic's two-stage attack,
  upward compression with the reference's range-dependent slope (§1.1), Prism with more than four bands, dynamic EQ
  in the side chain, mid / side processing, a screenshot review of the panels on Windows (`LYNX.exe --plugin-shots`).
