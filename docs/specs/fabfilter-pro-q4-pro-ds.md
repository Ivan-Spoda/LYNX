# FabFilter Pro-Q 4 and Pro-DS — spec dossier (LYNX substitutes: Facet EQ / Hush De-esser)

Round 125 (spec125). Clean-room dossier from FabFilter's online help, LYNX's own black-box measurements of the
official trials ([MEASURE_fabfilter.md](../analise/MEASURE_fabfilter.md), rounds 99 / 108), LYNX's source and her
saved states. No FabFilter binary was opened, decompiled or instrumented.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [DATA] her projects / LYNX files ·
[model] our inference. All web sources accessed 2026-10-21.

**Summary.** Both plugins are already **measured** in LYNX (Pro-Q 4 4.1.3 shapes, Q laws, slopes, processing modes;
Pro-DS 1.3.2 static curve, modes, latency) [MEAS]. Facet EQ was built on those Pro-Q prototypes and even uses the
same Q parameter law (`Q = 0.025·1600^v`) [DATA]. Yet both substitutes run at **defaults** (Facet flat, Hush
default) because their states are not translated [DATA]. Her states are plain float lists ("FFBS" / "FabF" layouts,
round 108 §12.6) and decode cleanly: four Pro-Q 4 states (1–2 static bands each, Zero Latency, Clean) and two Pro-DS
states (Single Vocal, wide band, range 6 dB, HP 7 kHz) [DATA]. **Translating them is the whole job for Pro-Q 4**
(Facet already reproduces the shapes to ≤ 0.4 dB) and most of the job for Pro-DS (Hush needs a Single-Vocal-style
relative detector and a 15 ms look-ahead to be identical).

---

## 1. Identity

| Item | Pro-Q 4 | Pro-DS | Source |
|---|---|---|---|
| Vendor | FabFilter Software Instruments | same | [S1][S2][DOC] |
| Measured version | 4.1.3 (VST3) | 1.3.2 (VST3) | [MEAS, MEASURE_fabfilter §0] |
| What it is | 24-band EQ: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass; slopes 6…96 dB/oct + Brickwall; dynamic EQ and spectral dynamics per band; Zero Latency / Natural Phase / Linear Phase; Character Clean / Subtle / Warm | de-esser: Single Vocal ("highly intelligent detection algorithm, which splits sibilance from non-sibilance") or Allround (frequency range + threshold); Wide Band or Split Band | [S1][S3][S2][DOC] |
| Hardware lineage | none (digital-native) | none | — |

**Her usage and states [DATA]** (412-project inventory: Pro-Q 4 6 slots / 1 state, Pro-DS 6 slots / 1 state; the
full 907-file copy holds 4 and 2 distinct states). Decoded with the round-108 layout (one float per VST3 parameter
in list order; frequency log2(Hz), gain dB, Q / thresholds normalised, enumerations as indices; Pro-Q 4 stores 23
floats per band):

| State (slots in full copy, example project) | Bands | Globals |
|---|---|---|
| Pro-Q 4 `446df320` (6, `fuentes drogas antiquadas`; = the inventory's one state) | 1: **Low Cut 82.4 Hz, 24 dB/oct, Q 0.471** (−9.5 dB at fc) | Zero Latency, Clean, gain scale 100 %, output 0 dB, auto gain off [model for the globals] |
| Pro-Q 4 `332402a8` (63, `luisa sonsa`) | 1: **Bell 153.5 Hz, −1.50 dB, Q 1.00** | same |
| Pro-Q 4 `5ae14e2f` (9, `twin swag`) | 1: **Low Cut 508.8 Hz, 12 dB/oct, Q 1.077**; 2: **High Cut 9868 Hz, 12 dB/oct, Q 1.061** | same |
| Pro-Q 4 `a2e69f80` (4, `NADA FORA LIWN`) | 1: **Bell 116.8 Hz, −4.56 dB, Q 4.27**; 2: **Bell 9899 Hz, −0.12 dB, Q 1.96** | same |
| Pro-DS `10f0abd7` (67, `luisa sonsa`) | Single Vocal, threshold norm 0.2715 (≈ −43.7 dB by interpolating the 0.2 → −48 / 0.3 → −42 table points), range 6 dB, Wide Band, stereo link 100 %, field 6 = 12 (look-ahead 12 ms?), look-ahead on, HP 7.0 kHz, LP 14 kHz | preset name "Default Setting" |
| Pro-DS `1bcb65e5` (6, `fuentes drogas antiquadas`) | as above but threshold norm 0.4 = **−36 dB** (the default) | |

All Pro-Q 4 bands have dynamics disabled (dynamic range 0) — they are static EQ [DATA]. The Pro-DS threshold unit and
field 6 are [model] (the parameter table's text mapping is non-linear; confirm by §8).

## 2. Signal flow

```
Pro-Q 4:  in ─► [bands in parallel-cascade, each: static shape × (dynamic gain from its side chain)] ─► Character
          (Clean | Subtle | Warm saturation) ─► Gain Scale · Output, pan ─► out
          processing: Zero Latency (min-phase IIR, analog-matched magnitude) | Natural Phase (analog phase, 320
          samples @48k) | Linear Phase (5120 samples @48k, Medium)
Pro-DS:   in ─► delay (look-ahead, constant 15 ms latency) ─────────────► × gain ─► out (Wide Band)
             └► HP/LP side-chain band (2–20 kHz) ─► detector (Single Vocal: relative to voice level | Allround:
                absolute vs threshold) ─► gain computer (range cap) ─► gain
          Split Band: gain applied only above a crossover set from the HP side-chain frequency
```
[S1][S2][S3][DOC], [MEAS, MEASURE_fabfilter §1, §5]

## 3. Controls

**Pro-Q 4 (per band; 24 bands)** [MEAS, MEASURE_fabfilter §1.1], [DATA proq4_params.tsv]: Used, Enabled,
Frequency 10 Hz–30 kHz (log; state = log2 Hz), Gain ±30 dB, Q 0.025–40 (`Q = 0.025·1600^v`), Shape (10),
Slope (0, 6, 12, 18, 24, 30, 36, 48, 72, 96, Brickwall; state = index), Stereo Placement, Speakers, Dynamic Range
±30 dB, Dynamics Enabled / Auto, Threshold (−90…−6 dB, Auto), Attack / Release %, external side chain, side-chain
filter, Spectral Enabled / Density / Tilt, Solo. Globals: Processing Mode, Resolution, Character, Gain Scale
0–200 %, Output Level, Output Pan (+ mode), Invert, Auto Gain.

**Pro-DS** [DATA prods_params.tsv], [S2][DOC]:

| # | Control | Range | Default |
|---|---|---|---|
| 0 | Mode | Single Vocal / Allround | Single Vocal |
| 1 | Threshold | −∞ … 0 dB (norm 0.4 = −36 dB) | −36 dB |
| 2 | Range | 0 … 24 dB | 6 dB |
| 3 | Band Processing | Wide Band / Split Band | Wide Band |
| 4–5 | Stereo Link (0–100 %, then 100 % + Mid-only share), Link mode Mid/Side | | 100 %, Mid |
| 6–7 | Lookahead 0 … 15 ms, enable | | 10 ms, on |
| 10–11 | High-Pass / Low-Pass side-chain frequency, 2–20 kHz log (`f = 2000·10^v`) | | 7.5 kHz / 14 kHz |
| 14 | Oversampling Off / 2× / 4× | | Off |
| 15–18 | Input / Output level (−∞ … +36 dB), pans | | 0 dB |

## 4. Known curves & behaviour (our measurements)

- **Pro-Q 4 prototypes (zero latency):** Bell `H(s) = (s² + s·A/Q_b + 1)/(s² + s/(A·Q_b) + 1)`, A = 10^(G/40),
  **Q_b = Q/√2**; shelves = cookbook analog shelf with **Q_s = 0.7071·Q^0.4697**; cuts = Butterworth of order
  slope/6 with the highest-Q section × Q (level at fc = 20·log10(Q/√2)); magnitude matched to the analog prototype
  (median 0.02 dB, ≤ 0.4 dB near Nyquist at 48 kHz) [MEAS, MEASURE_fabfilter §1.2].
- Natural Phase = 320 samples latency, analog phase; Linear Phase (Medium) = 5120 samples [MEAS §1.3].
- Character: "Clean, Subtle or Warm … Subtle adds a subtle, vintage type of saturation … Warm is more audible tube-
  style" [S3][DOC]; not measured. Her states: Clean [DATA, model].
- **Pro-DS:** latency 720 samples (= 15 ms) wide band, 1232 split band, independent of the look-ahead knob;
  above threshold ≈ −0.4 dB per dB (≈ 1.7:1) until Range; **Single Vocal did not reduce an 8 kHz stair under a
  −12 dBFS 500 Hz tone** (detection relative to the broadband voice level); Allround reduced 2.1–11.7 dB; split band
  leaks 0.2–0.7 dB onto the low band [MEAS, MEASURE_fabfilter §5].
- "In Single Vocal mode … lower the Threshold all the way to −INF dB … all sibilance will then be reduced by
  (roughly) the same amount (specified by Range)" [S2][DOC].

## 5. Published science

- **Analog-matched digital EQ.** Zero-latency magnitude matching without bilinear cramping: Orfanidis's prescribed-
  Nyquist-gain peaking EQ, Massberg's matched low-pass, and Vicanek's closed-form matched biquads (poles by impulse
  invariance `a1 = −2e^{−qω0}cos(√(1−q²)ω0)`, `a2 = e^{−2qω0}`, q = 1/(2Q); numerator fitted to the analog magnitude
  at DC and at ω0 / Nyquist) [S4][PAPER]. Facet EQ uses this approach ("magnitude-matched digital section … pole by
  impulse invariance") [DATA, `src/core/FacetEq.cpp`].
- **De-essing** = dynamic attenuation driven by a band-limited side chain; a "relative" detector compares the
  sibilance-band level with the broadband (or low-band) level, i.e. `d = L_band − L_wide` in dB, so loud vowels do
  not trigger [model consistent with MEAS §5]. Static law: `GR = −min(Range, (d − T)·(1 − 1/R))`, R ≈ 1.7 measured.

## 6. What LYNX does today

- `[fabfilter-pro-q]` → **Facet EQ** (8 bands, Pro-Q shapes, same Q law, dynamic per band), **no translation**,
  flat [DATA, `PluginSubstitutes.cpp`, `FacetEq.cpp`, round 98/99].
- `[fabfilter-pro-ds]` (and Harrison De-Esser) → **Hush De-esser** default: frequency 2–16 kHz (log), threshold
  −60…0 dB (linear), range 0–24 dB, wideband switch, band detector, fixed 2 ms look-ahead, release 10–250 ms
  [DATA, `src/core/MixSuite.cpp`]. Round 113 calibrated Hush for **Waves DeEsser**, not Pro-DS
  ([round 113 §3, §3c](../analise/REA_rodada113.md)).
- On her laptop Pro-Q 4 and Pro-DS are installed and hosted, so the substitutes only play on machines without
  FabFilter ([round 113 §1](../analise/REA_rodada113.md)) [DATA].

## 7. Implementation guidance for LYNX

Priority: **high value / low cost for Pro-Q 4** (63 slots of `luisa sonsa` use the −1.5 dB bell), **medium for
Pro-DS** (67 slots in `luisa sonsa`).

1. **Pro-Q 4 → Facet translator** (`translate = proq4`, next to the round-108 FabFilter translators in
   `src/core/FabFilterStates.cpp`): parse "FFBS" v1, count 600; for each band b (stride 23): Used (0), Enabled (1),
   Freq = 2^v (2), Gain dB (3), Q norm (4) → Facet Q param **unchanged** (same law), Shape index (5) → Facet shape
   (Bell, LowShelf, LowCut, HighShelf, HighCut, Notch, BandPass, TiltShelf; Flat Tilt / All Pass: approximate or
   skip), Slope index (6) → Facet slope (map 6…96 dB/oct; Brickwall → steepest), dynamics (8–15) → Facet range /
   threshold when Dynamic Range ≠ 0. Globals at 552…: Processing Mode, Character, Gain Scale, Output, Auto Gain.
   Take the first 8 used bands (her states use ≤ 2).
2. **Verify** each of her four states against the golden prototypes (MEASURE §1.4 method): ≤ 0.1 dB RMS
   20 Hz–20 kHz at 48 kHz; low cut 82 Hz / 24 dB / Q 0.471 must show −9.5 dB at 82.4 Hz.
3. **Pro-DS → Hush translator** (`translate = prods`): "FabF" v2, name, count 20: Mode, Threshold (norm → dB
   through the parameter's text table; confirm), Range dB, Wide/Split, HP/LP (`2000·10^v`), look-ahead.
   Map HP → Hush frequency, Range → Hush range, Wide/Split → Hush wideband switch.
4. **Make Hush able to be Pro-DS:** add a "relative (single vocal)" detector: sibilance-band level minus a
   broadband (or < 4 kHz) level, threshold on the difference, slope ≈ 0.6 dB/dB (1.7:1) to the range cap, release
   fitted to §8; constant 15 ms latency in Pro-DS mode (720 samples @48 k) so the PDC matches.
5. **Pass criteria:** Pro-DS state `10f0abd7` on her `luisa sonsa` vocal: GR envelope within 1 dB on sibilants,
   level-normalised 2–16 kHz third-octave error ≤ 0.5 dB RMS; no reduction on vowels (≤ 0.2 dB).

## 8. Open questions to measure (official trials, black-box only)

1. Pro-DS threshold storage: set −48 / −43.7 / −36 / −24 dB, save, read the stored float → confirm unit/law; and
   field 6 (look-ahead ms?).
2. Pro-DS Single Vocal detector: sibilance-band vs broadband level grid (8 kHz tone −40…0 dBFS × 500 Hz tone
   −40…0 dBFS) → the relative-detection law; attack/release from bursts.
3. Pro-DS Split Band crossover vs HP setting (slope, phase).
4. Pro-Q 4 Character Subtle / Warm harmonics vs level (only if a state uses them; hers are Clean).
5. Pro-Q 4 dynamic EQ and spectral dynamics (not in her states; low priority).

## 9. Sources

1. FabFilter, *Pro-Q 4 Help* (online) and PDF — https://www.fabfilter.com/help/pro-q/using/overview , https://www.fabfilter.com/downloads/pdf/help/ffproq4-manual.pdf — accessed 2026-10-21 — [DOC]
2. FabFilter, *Pro-DS Help — Basic controls / Advanced controls* — https://www.fabfilter.com/help/pro-ds/using/basiccontrols , https://www.fabfilter.com/help/pro-ds/using/advancedcontrols — accessed 2026-10-21 — [DOC]
3. FabFilter, *Pro-Q 4 Help — Character modes* — https://www.fabfilter.com/help/pro-q/using/character-mode — accessed 2026-10-21 — [DOC]
4. M. Vicanek, "Matched Second Order Digital Filters", 2016 (with references to Orfanidis JAES 45(6) 1997 and Massberg AES 131, 2011) — https://www.vicanek.de/articles/BiquadFits.pdf — accessed 2026-10-21 — [PAPER]
5. LYNX measurements: [MEASURE_fabfilter.md](../analise/MEASURE_fabfilter.md) §1 (Pro-Q 4), §5 (Pro-DS), §12.6 (state layouts); [REA_rodada108.md](../analise/REA_rodada108.md) §5 (FabFilter translators); [REA_rodada113.md](../analise/REA_rodada113.md) §1, §3 — [MEAS]
6. LYNX data: parameter tables `~/.claude/jobs/f8789b49/tmp/r99/data/params/proq4_params.tsv`, `prods_params.tsv`; her states extracted from `tmp/gf/all`; source `src/core/FacetEq.cpp`, `src/core/MixSuite.cpp` (Hush), `src/core/PluginSubstitutes.cpp` — [DATA]
