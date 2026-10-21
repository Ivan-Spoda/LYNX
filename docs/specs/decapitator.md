# Soundtoys Decapitator — spec dossier (LYNX substitute: Ember Saturator)

Round 125 (spec125). Clean-room dossier built from the vendor's user guide (v5), independent reviews, the academic
literature on saturation modelling and anti-aliasing, LYNX's own source and her saved project data. No Soundtoys
binary was opened, decompiled or instrumented; the encoded preset payload was not decoded.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [DATA] her projects / LYNX files ·
[model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** Decapitator is an "analog saturation" processor with five styles, each "created by
analyzing various analog channels in our lab" to match "harmonic structure, frequency response, and dynamic
response" [S1 p.4][DOC]: **A** = Ampex 350 tape-drive preamp, **E** = Chandler/EMI TG channel, **N** = Neve 1057
input channel (germanium), **T** = Thermionic Culture Vulture triode setting (even harmonics), **P** = Culture Vulture
pentode setting (odd harmonics) [S1 p.5–6][DOC]. The chain is Low Cut (+ optional Thump resonant bump) → Tone tilt →
saturation (Drive 0–10, Punish +20 dB) → High Cut (6 dB/oct, or 30 dB/oct with Steep) → Mix → Output (−24…0 dB, with
Auto gain moving it opposite to Drive) [S1 p.6–9][DOC]. Her projects use it in 6 slots / 1 state (412-project
inventory) but 888 slots / 63 states across the full 907-file copy [DATA]. The state is encoded; LYNX plays a fixed
light Ember tube drive (+1.4 dB, auto gain), fitted in round 113 to minimise the octave-band change on `game over`
(0.88 → 0.46 dB spread) — a neutral-ish guess, not a model of her setting [MEAS, round 113 §3].

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Soundtoys Decapitator (Soundtoys 5; her states: `WIDGET = Decapitator; VERSION = 4; SAVED_BY_VERSION = 5.0.1`) | [S1][DOC], [DATA] |
| Format in her projects | VST2, ID `StDc` | [S6][DATA] |
| Positioning | "NOT a guitar amp modeler, and it's not a tape saturation plug-in" — the sound of analog gear "pushed hard to the point of saturation and beyond" | [S1 p.4][DOC] |
| History | first shipped in the Native Effects v4 era (SOS review of v4.02; Barry Rudolph review) ; announced as "coming soon … Instant Lo-Fi" in the EchoBoy v1 guide | [S2][S3][DOC], [S7 p.62][DOC] |

**Hardware lineage (per style)** [S1 p.5–6][DOC]:

| Style | Modelled unit | Vendor's description |
|---|---|---|
| A | Ampex 350 tape-recorder preamp (1950s; often pulled out and used as a mic pre) | "ridiculous amount of gain … ultra-smooth tube distortion" |
| E | Chandler Limited / EMI TG channel (Abbey Road console lineage) | mic pre, DI and EQ "with loads and loads of character" |
| N | Neve 1057 input channel (105x series, germanium transistors) | "think vintage FuzzFace" |
| T | Thermionic Culture Culture Vulture, triode setting | "loads of even harmonic distortion … warm and punchy" |
| P | Culture Vulture, pentode setting | "usually characterized by odd harmonic distortion" |

**Her usage [DATA].** 412-project inventory ([round 113 §4b](../analise/REA_rodada113.md)): 6 slots, 6 projects,
1 state. Full copy (`tmp/gf/all`, 907 .flp with autosaves): 888 slots, 63 states; the dominant states live in
`twin swag` (241 + 63 + 55 slots), `gente morta` (125 + 32). In `luisa sonsa` it sits with UADx Neve 1073 / 1176 /
LA-2A, Pro-Q 4 and Pro-DS ([round 107](../analise/REA_rodada107.md)). Payload: `PRESET =<encoded blob>`; the
presets in her files start with the factory-like header `0101000005…` — values not readable [DATA].

## 2. Signal flow

```
in ─► Low Cut (HPF, 20 Hz…1 kHz) ─► [Thump: resonant LF bump at the Low Cut frequency]
   ─► Tone (gentle tilt, Dark…Bright, AM-radio-style)            ── all three BEFORE the saturation (S1 p.7–8)
   ─► Drive gain (0…10) [+20 dB with Punish] ─► style nonlinearity A/E/N/T/P (with the style's own EQ and dynamics)
   ─► High Cut (LPF 1…20 kHz, 6 dB/oct; Steep = 30 dB/oct)        ── AFTER the saturation (S1 p.8–9)
   ─► Mix (dry … wet; dry = original signal) ─► Output (−24…0 dB; Auto: moves opposite to Drive) ─► out
```
The meter is a VU [S1 p.4 figure][DOC]. Where the dry tap of Mix is taken (before or after Low Cut/Tone) is not
documented [model; §8].

## 3. Controls

| Control | Range | Unit | Default | Law / behaviour | Source |
|---|---|---|---|---|---|
| Style | A, E, N, T, P | — | A in the manual screenshot (preset "Melted Triode") — default not stated | selects the saturation model | [S1 p.4–6][DOC] |
| Drive | 0 … 10 | knob scale | n/d | input gain into the model; "can get very loud" | [S1 p.4 fig., p.6][DOC] |
| Punish | on/off | — | off | "+20 dB of gain" on top of Drive | [S1 p.7][DOC] |
| Auto (gain) | on/off | — | on (per review) | turns Output down as Drive goes up | [S1 p.6–7][DOC], [S4][DOC] |
| Output | −24 … 0 | dB | n/d | "turns DOWN the output level" | [S1 p.4 fig., p.7][DOC] |
| Mix | Dry … Wet | % | n/d (100 % wet implied) | parallel blend, "restore the transients" | [S1 p.7][DOC] |
| Low Cut | 20 Hz … 1 kHz | Hz | n/d | HPF before saturation; slope not stated | [S1 p.4 fig., p.7][DOC] |
| Thump | on/off | — | off | "a few dB of low frequency boost right at the Low Cut frequency" (tape head-bump) | [S1 p.8][DOC] |
| Tone | Dark … Bright | — | centre | "very gentle sloping equalizer"; Dark = lows up / highs down, Bright = the opposite; before the saturation | [S1 p.8][DOC] |
| High Cut | 1 kHz … 20 kHz | Hz | n/d (max = off?) | LPF after saturation, 6 dB/oct | [S1 p.4 fig., p.8–9][DOC] |
| Steep | on/off | — | off | High Cut slope 30 dB/oct | [S1 p.9][DOC] |

State mapping: none (encoded preset) [DATA].

## 4. Known curves & behaviour

- Harmonic signature claims (vendor): T even-order dominant, P odd-order dominant [S1 p.5–6][DOC]; reviewers agree
  [S3][DOC]. No published harmonic spectra, transfer curves, oversampling factor or latency [model].
- Styles include "frequency response, and dynamic response" of the hardware [S1 p.4][DOC] → each style is more than a
  static curve: expect a style-specific pre/post EQ and level-dependent (memory) behaviour such as bias shift /
  sag [model].
- Thump = resonant HPF (Q > 0.707) rather than a separate bell [model from "resonant hump at the cutoff" S2].
- Steep 30 dB/oct = 5th-order low-pass [model].
- Reviewer setting on vocals: Drive ≈ 1, Low Cut ≈ 800 Hz [S3][DOC] — illustrates that vocal use is usually light.

## 5. Published science

**Static waveshaping.** A memoryless saturator y = f(g·x) adds harmonics whose parity follows the symmetry of f:
odd f (f(−x) = −f(x), e.g. tanh) → odd harmonics only; adding an even component (bias: f(x + b) − f(b)) → even
harmonics [S8][PAPER]. Triode stages are asymmetric (grid conduction on one half-wave, gradual cut-off on the other),
which is why triode emulations generate strong H2; push-pull or pentode output stages are closer to odd-symmetric
[S8][PAPER]. A widely used triode plate-current law (Koren) [S9][PAPER]:
`E1 = (V_pk/k_p)·ln(1 + exp(k_p(1/μ + V_gk/√(k_vb + V_pk²))))`, `I_p = (E1^{x}/k_g1)·(1 + sgn(E1))`.

**Dynamic (memory) effects.** Coupling-capacitor/grid-current interaction shifts the operating point with signal
level (blocking distortion), so the harmonic balance changes with programme level and time [S8][PAPER]. Transformers
add low-frequency saturation (flux ∝ ∫v dt, so lows saturate first) [S8][PAPER] — consistent with Decapitator's
explicit Low Cut "to prevent the excessive flabbiness" [S1 p.7][DOC].

**Aliasing.** A nonlinearity widens the bandwidth (tanh of a full-scale tone has components far above Nyquist); they
fold back as inharmonic aliases. Remedies: oversampling (×4–×8 with half-band FIR/IIR) or antiderivative
anti-aliasing (ADAA) [S10][S11][PAPER]. First-order ADAA (Parker, Zavalishin, Le Bivic, DAFx-16; generalised by
Bilbao, Esqueda, Parker, Välimäki, IEEE SPL 2017):
`y[n] = (F1(x[n]) − F1(x[n−1])) / (x[n] − x[n−1])`, with F1 the antiderivative of f; when |x[n] − x[n−1]| < ε use
`f((x[n] + x[n−1])/2)`. It adds a half-sample delay and a mild low-pass (sinc-like) to the signal. For tanh:
F1(x) = ln cosh x. Second-order uses F2 and divided differences of F2 [S10][S11][S12][PAPER].

**Parallel mix** (Mix knob) of a saturated and dry path requires the dry path to be delayed by exactly the
saturator's latency (oversampling filters, ADAA half-sample), or comb-filtering appears [model].

## 6. What LYNX does today

- `[soundtoys-decapitator]` → `Ember Saturator`, `preset = Drive=0.04` (Ember drive law `lin(v, 0, 36)` dB →
  +1.44 dB), style 0 = **tube**, Auto gain on, Mix 100 %, oversampling param 0.5 → **×4** (half-band FIR, Kaiser)
  [DATA, `src/core/MixSuite.cpp`, `PluginSubstitutes.cpp`].
- Ember tube curve: `y = (tanh(x + 0.35) − tanh 0.35)/(1 − tanh² 0.35)` (biased tanh, unit slope at 0, even + odd
  harmonics) followed by a DC blocker; auto makeup = RMS match of a −12 dBFS sine through the curve; Tone = ±6 dB
  shelves at 1 kHz (tilt); Low cut 20–500 Hz, High cut 2–20 kHz (12 dB/oct biquads); styles: tube, tape (with
  ±6 dB 3 kHz emphasis around the curve), transistor, fuzz [DATA].
- Measured effect on `game over` (Ember alone vs no substitutes, level-normalised octave bands 125 Hz … 16 kHz):
  before (+5.4 dB) `+1.1 +1.6 −0.2 −0.2 −0.0 +0.3 +1.5 −0.0` (spread 0.88 dB); after (+1.4 dB)
  `+0.2 +1.1 −0.2 −0.1 +0.0 +0.1 +0.6 −0.3` (0.46 dB) [MEAS, [round 113 §3](../analise/REA_rodada113.md)]. This is a
  change relative to *no* plugin, not an error against Decapitator (never measured: no Soundtoys trial installed,
  [round 114](../analise/REA_rodada114.md)).
- Ember's low/high cut ranges (20–500 Hz, 2–20 kHz) are narrower than Decapitator's (20 Hz–1 kHz, 1–20 kHz), its
  high cut is 12 dB/oct where Decapitator offers 6 or 30, and there is no Thump or Punish [DATA vs S1].

## 7. Implementation guidance for LYNX

Priority: **medium** (888 slots in the full corpus, mostly in a few beat/vocal projects; one setting per project).

1. **Keep the fallback light** until states are readable: any drive guess colours every slot. The round-113 choice
   is reasonable [MEAS].
2. **Add a "Decapitator mode" to Ember** (cheap, structural parity): Low Cut range 20 Hz–1 kHz with an optional
   resonant bump (2nd-order HPF with Q ≈ 1.2–2, value from §8), Tone tilt before the drive, High Cut 1–20 kHz with
   6 dB/oct (1st order) or 30 dB/oct (5th-order Butterworth: one 1st-order + two biquads) **after** the drive,
   Punish = +20 dB pre-gain, Output −24…0 dB, Auto gain moving Output by −k·Drive (k from §8).
3. **Five style curves** fitted to measurements (§8): for each style, a pre-EQ, a static curve
   (asymmetric for T/A/E, near-odd for P/N), a post-EQ, and optionally a slow bias-shift (envelope-controlled offset)
   for the "dynamic response". Fit with the harmonic table H2…H7 vs drive (≤ 1 dB per harmonic above −60 dBc).
4. **Anti-aliasing:** keep Ember's ×4 oversampling or add first-order ADAA on the static curve (ln cosh for tanh-like
   curves) — target: aliases < −90 dBc for a 5 kHz sine at drive 10 [S10][S11].
5. **Mix:** delay-compensate the dry path to Ember's oversampler latency (already done via `dry`), and verify null at
   drive 0, Mix 0 %.
6. **Pass criteria** per style at her state(s): steady-state harmonics within 1 dB; small-signal magnitude response
   within 0.3 dB (20 Hz–20 kHz); octave-band error on her vocal ≤ 0.5 dB RMS; level (LUFS-S) within 0.3 dB with Auto on.

## 8. Open questions to measure (official trial, black-box only)

1. **State readability.** Save presets with single-control changes; check whether the blob is a reversible encoding
   of plain values (if it is encrypted/obfuscated, do not attempt to break it; fit by rendering instead).
2. **Drive law.** 1 kHz sine at −18 dBFS, Drive 0…10 step 1, each style, Auto off: fundamental gain (dB per knob
   unit) and H2…H7 → drive taper and per-style curves; repeat at −6 dBFS and with Punish.
3. **Auto gain law.** Same sweep with Auto on: output level vs Drive (does it hold RMS, peak, or a fixed dB/step?).
4. **Small-signal EQ per style** (impulse at −60 dBFS, Drive 0 and 5): hidden pre/post EQ of A/E/N/T/P.
5. **Filters.** Low Cut at 20/100/1k Hz with Thump off/on (bump gain and Q); Tone extremes (tilt pivot and slope);
   High Cut 1/5/20 kHz with Steep off/on (confirm 6 / 30 dB/oct).
6. **Dynamics.** Tone burst −30 → −6 → −30 dBFS at Drive 6: any attack/release in harmonic balance or gain
   (bias shift, sag) per style.
7. **Aliasing / oversampling.** 7 kHz sine at Drive 10 at 44.1 and 96 kHz: alias spectrum → oversampling factor.
8. **Latency** (impulse, Drive 0) and whether Mix's dry path is delay-aligned.
9. **Defaults** of a fresh instance (Style, Drive, Output, Auto, Mix, filters).

## 9. Sources

1. Soundtoys, *Decapitator — Analog Saturation Effects*, User's Guide (v5), official PDF https://www.soundtoys.com/wp-content/uploads/Decapitator-Manual.pdf (read through the B&H-hosted copy https://static.bhphoto.com/lit_files/89645.pdf), pp. 4–9 — accessed 2026-10-21 — [DOC]
2. Sound On Sound review of SoundToys Native Effects v4.02 (Decapitator section) — https://www.soundonsound.com/node/4908143 — accessed 2026-10-21 — [DOC]
3. B. Rudolph, "SoundToys Decapitator" review — https://barryrudolph.com/newtoys/toys7/soundtoysdecapitator.html — accessed 2026-10-21 — [DOC]
4. Music Guy Mixing, "The Best Saturator Plugin – Why It's Decapitator" (Auto on by default) — https://www.musicguymixing.com/best-saturator-plugin/ — accessed 2026-10-21 — [DOC] (third-party, low weight)
5. LYNX round 113 §3 (Ember fit on `game over`), round 114 (no Soundtoys trial) — [../analise/REA_rodada113.md](../analise/REA_rodada113.md), [../analise/REA_rodada114.md](../analise/REA_rodada114.md) — [MEAS]
6. LYNX round 98 (Soundtoys VST2 IDs and encoded presets) — [../analise/REA_rodada98.md](../analise/REA_rodada98.md) — [DATA]
7. SoundToys, *EchoBoy* user guide (Pro Tools era), p. 62 ("Coming Soon — Decapitator") — https://barryrudolph.com/recall/manuals/echoboy.pdf — accessed 2026-10-21 — [DOC]
8. J. Pakarinen, D. T. Yeh, "A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers", *Computer Music Journal* 33(2), 2009 — https://doi.org/10.1162/comj.2009.33.2.85 — accessed 2026-10-21 — [PAPER]
9. N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996 — https://www.normankoren.com/Audio/Tubemodspice_article.html — accessed 2026-10-21 — [PAPER]
10. J. D. Parker, V. Zavalishin, E. Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", Proc. DAFx-16, Brno, 2016 — https://www.dafx.de/paper-archive/ — accessed 2026-10-21 — [PAPER]
11. S. Bilbao, F. Esqueda, J. D. Parker, V. Välimäki, "Antiderivative Antialiasing for Memoryless Nonlinearities", *IEEE Signal Processing Letters* 24(7):1049–1053, 2017 — https://aaltodoc.aalto.fi/items/470aab15-1702-4ccf-a148-24e6173079fb — accessed 2026-10-21 — [PAPER]
12. J. Chowdhury, "Practical Considerations for Antiderivative Anti-Aliasing" (notebook) — https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html — accessed 2026-10-21 — [PAPER] (tutorial)
13. LYNX source: `src/core/MixSuite.cpp` (Ember: `shape`, `autoMakeup`, oversampler), `src/core/PluginSubstitutes.cpp` (`[soundtoys-decapitator]`) — [DATA]
