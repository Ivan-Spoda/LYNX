# REA round 152 (parity152): docs/PARITY.md, tier 5

2026-10-24. Continues round 150 (`REA_rodada150.md` / `150a` / `150b`, "Where I stopped"). This round (parent): Prism
Multiband ← Pro-MB — the slow attacks (50 – 100 %), the narrow Dynamic-phase bands, the band's minimum width, the free
layout checked by hand in the running app; Foil Plate ← Little Plate — the one-knob Plate view. Sub-rounds: 152a
(Compressor ← Renaissance Compressor, Glow Leveler ← CLA-2A — `REA_rodada152a.md`), 152b (Ember ← Saturn 2, LYNX Tune
"Group Octaves" — `REA_rodada152b.md`).

Evidence markers: [EXEC] run here on the official FabFilter Pro-MB 1.3.3 trial (evaluation, ends ~2026-11-16), hosted
black-box in `tools/fxmeasure` (public VST3 parameters and value texts, audio in / out, 48 kHz); [model] LYNX's own
choice. Nothing was decompiled; no graphics, presets or names were copied. Raw data: `D:\lynx-work\parity152\mb\`
(n1 … n5 = the scripts, *.log = their logs, n_* / l_* / m_* / k_* / w_* = the impulse renders, ktab*.json = the fitted
tables; the analysis scripts are copied next to them: fitatt*.py + det3.c / det4.c = the detector, nb.py / dbl*.py /
edgek.py / ktab*.py = the band shapes, geninc.py = the table generator). Round 150's step renders
(`D:\lynx-work\parity150\mb\att*.wav`) were refitted.

## 1. The detector's attack lives in the gain stage [EXEC]

Round 150 fitted Pro-MB's attack as the level follower's attack (dB domain) and found the slow attacks off by 0.5 – 1.3
dB rms (up to 3 dB in the first 100 ms at 100 %). Refitting the same renders (one band over the whole spectrum,
threshold −30, 4 : 1, knee 0, release 20 %, 1 kHz steps −30 → −6 → −30 → −18 → −30 dBFS) with six model variants
(`fitatt.py`): the level follower in dB or in |x|^q, one or two attack poles, with or without a smoothing of the
growing gain change after the curve:

| model | att 50 % | att 75 % | att 100 % (rms dB over the attack segments) |
|---|---|---|---|
| level attack, dB (round 150) | 1.09 | 2.27 | 3.06 |
| level attack, two poles | 0.22 | 0.16 | 0.27 |
| fast level follower + **gain-stage attack** | 0.19 | 0.08 | 0.07 |

The reference's attack is in the gain stage: a fast level follower (τ ≤ 1.3 ms) and the growing gain change smoothed
by its own τ (0.13 ms at 0 % … 271 ms at 100 %), the release unchanged (round 150's τr1 / τr2 come out the same: 49 –
73 / 118 – 129 ms at release 20 %). Over the first 400 ms after a step the one-pole gain stage still left 0.6 / 0.7 dB at
35 / 50 % (fast start, slow tail); smoothing in the domain 10^(k·|gain| / 20) fixes it (k = 0.66 / 0.84 / 0.36 at
35 / 50 / 75 %, 0 elsewhere; `fitatt5.py`): worst error 0.03 – 0.41 dB at every attack.

| attack | 0 | 10 | 20 | 35 | 50 | 75 | 100 % |
|---|---|---|---|---|---|---|---|
| level follower τ (ms) | 0.012 | 0.012 | 0.022 | 0.092 | 0.418 | 0.539 | 1.30 |
| gain-stage τ (ms) | 0.134 | 0.166 | 0.695 | 6.95 | 30.3 | 115.6 | 271 |
| k | 0 | 0 | 0 | 0.66 | 0.84 | 0.36 | 0 |

LYNX (`DynamicsSuite.cpp` `measuredAttackLevel` / `measuredGainAttack` / `measuredGainAttackK`, the band detector's
`follow`): tests at 50 / 75 / 100 % against the trial's gain after 10 … 500 ms (`tests/Parity152Tests.inc`):
e.g. 100 %: −2.81 / −5.24 / −8.94 / −14.49 dB at 50 / 100 / 200 / 500 ms (Pro-MB −2.88 / −5.25 / −8.97 / −14.72; round
150: 0.00 / −2.51 / −6.36 / −8.23).

## 2. Narrow Dynamic-phase bands; the minimum band width [EXEC]

Renders (impulses, one static band in the free layout, range 0, level ±1 … ±30 dB, low edge 300 Hz, upper edge
300 · r for r = 1.25 … 10 and 100 · r for r = 20 … 80, slopes 12 / 24 / 36 / 48 dB/oct, Dynamic phase; Linear and
Minimum phase for r ≤ 3; 725 renders):

- **Minimum width 0.8 octave**: in every processing mode a band narrower than 2^0.8 = 1.741 behaves as one whose upper
  edge is at 1.741 × the lower (300 – 375 and 300 – 450 Hz both act as 300 – 522 Hz: 0.005 – 0.07 dB; the parameter
  keeps the typed value). LYNX's free layout kept bands ≥ 1.05 apart; now 0.8 octave (the upper edge moves).
- **Dynamic phase changes the band's shape** for cuts and narrow bands (Linear / Minimum phase are the formula
  1 + (g − 1)·A exactly): up to 1.7 dB deeper at the edges (24 dB/oct, 300 – 900 Hz, −30 dB; 12 dB/oct, −30 dB, r = 10). Every case is the same formula with the **edges moved out** (ω_l / k, ω_h · k, on
  the bilinear axis) and **another gain G**: worst error ≤ 0.05 dB over 40 Hz – 8 kHz in all 518 fitted cut / boost cases
  (`ktab*.py`). Boosts keep their edges (k = 1) and lose a little gain (12 dB/oct, r = 1.75, +6 dB: G = +5.26 dB);
  cuts widen (k up to 1.62) and lose depth (12 dB/oct, r = 1.75, −30 dB: G = −7.6 dB). The effect fades with the
  width and the slope (48 dB/oct: k ≤ 1.08, nothing from r = 6). A closed rule was looked for (a double root of the
  numerator's quadratic in Ω^m fits the 12 dB/oct and the deep 24 / 48 dB/oct cuts, not the rest); LYNX uses the
  measured table.

LYNX (`core/ProMbNarrow.inc`, `minphase::narrowAdjust`; `Band::designM` and `bandMagnitudeM` with the round-152
flag): trilinear in slope (12 … 48; mixed slopes use their mean [model]), gain dB (−30 … +30) and log r (1.75 … 80;
fading out by r = 320 [model]). Tests: six measured bands (the curve and a rendered tone at five frequencies; e.g.
24 dB/oct 300 – 900 Hz −30 dB: −7.28 / −13.34 dB at 300 / 520 Hz on the trial).

Compatibility: one appended parameter **Revision** (round 150 | round 152; new instances and translated Pro-MB states
= round 152) selects §1 and §2; a project saved before keeps round 150's law (`upgradeLoadedEffectParams`); the
editor's "Old det." button updates both.

## 3. Foil Plate ← Little Plate: the one-knob Plate view

Little Plate's panel is one big Decay knob with Low Cut, Mod and Mix (the trial's screenshot, round 135). Foil keeps
its fuller panel (**Full**) and gets a **Plate** view (appended **View**, Full | Plate tabs at the bottom right): Low
Cut on the left, the big Decay with its ∞ under it, Mod and Mix on the right, the Measured plate switch. The Little
Plate substitute opens in it (its preset sets View = Plate). pt-BR "Completo|Placa".

## 4. Prism's free layout by hand in the running app

(pending)

## Tests

tests/Parity152Tests.inc: the gain-stage attack at 50 / 75 / 100 % against the trial (and round 150's law for old
projects), the upgrade; the narrow Dynamic-phase bands (curve and tone, six measured cases, the 0.8-octave minimum);
Foil's View (texts, the two views' controls). Prism's parameter-count checks follow the appended parameter.

## Where I stopped

(pending)
