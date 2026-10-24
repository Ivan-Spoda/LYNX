# REA round 150 (parity150): docs/PARITY.md, tier 4

2026-10-24. Continues round 147 (`REA_rodada147.md` / `147a` / `147b`, "Where I stopped"). This round (parent):
Prism Multiband ← Pro-MB — every slope from 6 to 48 dB/oct, Pro-MB's free band placement with gaps, a side-chain
input per band, the narrow free side-chain filter, the detector's release (Pro-MB's immediate recovery) and its soft
range. Sub-rounds: 150a (Echoform ← EchoBoy: multi-voice chorus styles, the styles in Dual / Ping-Pong —
`REA_rodada150a.md`), 150b (Ember ← Saturn 2: six bands and the style list; LYNX Tune Send / Receive —
`REA_rodada150b.md`).

Evidence markers: [EXEC] run here on the official FabFilter Pro-MB 1.3.3 trial (evaluation, ends ~2026-11-16), hosted
black-box in `tools/fxmeasure` (public VST3 parameters and value texts, audio in / out, 48 kHz); [model] LYNX's own
choice. Nothing was decompiled; no graphics, presets or names were copied. Raw data: `D:\lynx-work\parity150\mb\`
(scripts s1 … s10 = the runs, *.log = their logs and latencies, the renders next to them); analysis scripts in the
job's `tmp/parity150/py` (sl.py, s3a.py, m6.py / m6b.py = the 6 dB/oct models, mixtab2.py = the mixed-slope table,
gtraj.py / detfit*.py + `tmp/parity150/sim/det.c` = the detector fit, scfit3.py = the narrow side-chain filter).

## 1. Slopes 6 … 48 dB/oct [EXEC]

Pro-MB's `Band n Low / High Slope` are continuous: 0 → 6, 0.25 → 12, 0.5 → 24, 0.75 → 36, 1 → 48 dB/oct
(6 + 24·v below 0.25, then 48·v; texts in steps of 0.1 dB/oct).

- **Linear phase**: the band's shape is exactly 1 / (1 + x^m) per side, m = slope / 6, for every value (39 slopes from
  6.6 to 46.8 dB/oct: the exponent read back from the response is m ± 0.002). Pro-MB's axis is the analog one (x = f / fc;
  LYNX's prewarped axis differs by ≤ 0.35 dB at a 3 kHz crossover with 6 dB/oct, ≤ 0.07 dB at 24).
- **Dynamic and Minimum phase** quantise the slope: below 9 → 6, 9 … < 18 → 12, 18 … < 30 → 24, 30 … < 42 → 36,
  from 42 → 48 (every one of 39 values matched one of the five shapes within 0.00 dB). 36 dB/oct is LR6 in Minimum
  phase and n = 3 in the measured Dynamic phase (exact, as 12 / 24 / 48).
- **6 dB/oct** is a first-order design of its own:
  - an edge band: the first-order shelf (s + g·ω)/(s + ω) (low band) / (g·s + ω)/(s + ω) (high band), its pole at the
    crossover (0.000 dB, 0.00° on five gains −30 … +18 dB). Minimum phase: the first-order pair (low + high = 1).
  - a middle band with two 6 dB/oct sides (fitted pole / zero sets, then the law): two first-order shelves of gain g
    at ω_l / k and k·ω_h — a bell (s² + a_n s + ω0²)/(s² + a_d s + ω0²) whose centre gain equals the linear-phase
    shape's there (T = 1 + (g − 1)·(√r / (1 + √r))², r = ω_h / ω_l); closed form u² = (1 − T g) / (g (T − g)),
    a_d = ω0 (u + 1/u), a_n = ω0 (g u + 1/(g u)); in deep cuts a_n stops at 2.345·ω0 (a_d = a_n / T). Within 0.02 dB of
    all 60 measured cases (−30 … +18 dB, r = 2 … 100).
  - a middle band with one 6 dB/oct side: the first-order shelf times the other side's squared Butterworth shelf, both
    of a common gain G, the corners moved out by k6 / kn. Boosts and small cuts: G = g, no move (exact, e.g. 6 / 24 at
    +6 and +18 dB: 0.000 dB); deeper cuts move (e.g. 6 / 24, 300 Hz – 3 kHz at −30 dB: G = −14 dB, k6 = 2.04, kn = 1.40).
    The rule behind the deeper cuts is not a simple fit to the linear shape (minimax / rms fits give other numbers), so
    LYNX uses the measured table: 6 / 12 and 6 / 24 at r = 2, 4, 10, 34 and 6 / 36, 6 / 48 at r = 10, every level
    −30 … −2 dB (`src/core/ProMbSixDb.inc`; the model with the table's numbers is within 0.001 dB of the 6 / 12, 6 / 24
    renders, 0.01 / 0.9 dB for 6 / 36, 6 / 48).
- Narrow even-order bands (r ≈ 3) in Dynamic phase are 0.3 dB deeper at their edges in Pro-MB than the minimum phase
  of its own linear shape (100 – 300 Hz and 1 – 3 kHz alone); not modelled.

LYNX (`core/MinPhaseBand.h`: `dynamicSideCode`, `SixDbModel`, `Band::designM`; `core/LrCrossover.h`: 6 and 36 dB/oct):
Dynamic phase's measured engine designs each band from its side codes (the 2n / 2n design of round 147 unchanged, the
6 dB/oct models above); Minimum phase and the classic engine run the quantised slope in the crossover tree; Linear
phase (free layout) builds each band's FIR from 1 / (1 + x^m) with the continuous m.

## 2. Free band placement and gaps [EXEC]

Pro-MB's bands each have their own low / high crossover; the room between them is untouched. Two bands 100 – 300 Hz
and 1 – 3 kHz at −12 dB (24 dB/oct): Minimum phase = the crossover tree over all four edges with the gaps at unity
(0.001 dB, 0.00°); Linear phase = 1 + Σ (g − 1)·A_b; Dynamic phase = the product of the two bands' own filters
(0.000 dB).

LYNX (`prism::layoutOf`, appended parameters): **Band layout** Split (the old contiguous bands) | Free; per band
**low / high edge** (30 Hz – 30 kHz, Pro-MB's 30 · 1000^v) and **low / high slope** (Pro-MB's value law). The free
layout orders the bands by their low edge, keeps them from overlapping and adds the gaps as segments (up to 13); every
processing mode runs on these segments (the gaps at unity), the measured Dynamic phase on each band's own edges and
slopes. Editor: Split | Free in the column right of the display (switching keeps the sound: the free bands take the
crossovers, the split takes the middle of each gap); in the free layout each band shows its own edges (drag one edge,
or the node to move the band), double-click or right-click in a gap = a new band there (an octave wide, inside the
gap), the band panel's expert part has the band's two slopes (6 … 48) and its side-chain input. Pro-MB states are now
translated to the free layout (every present band, its edges and slopes; disabled bands stay, bypassed).

## 3. The detector: soft range and release [EXEC]

One band over the whole spectrum, threshold −30 dB, 4 : 1, knee 0, 1 kHz steps −30 → −6 → −30 → −18 → −30 dBFS
(`step.wav`), attack and release swept 0 … 100 %, ratio 2 / 4 / 100, range −30 / −12 / −6, knee 24, threshold −40:

- the range has a **soft knee of 12 dB**: a 9 dB change under a 12 dB range gives 8.61 dB (x − (x − R + 6)² / 24), 9 under
  6 gives 5.62, 18 under 12 gives 12.0 — LYNX's classic detector clips hard (9 / 6 / 12);
- the release acts **after the curve and the range**: with a −12 dB range the gain starts back at once (−11.53 dB after
  20 ms, −10.01 after 50 ms) where a detector before the curve would hold until its level fell below the range; the
  normalised release is the same for ratio 2, 4 and 100;
- the fit (C model `tmp/parity150/sim/det.c`, dB domain): a level follower (attack τa, release τr1) → the curve with the
  soft range → the gain's own release τr2 (a growing change at once). Per setting (attack / release %):
  τa 0.065 / 0.081 / 0.17 / 1.2 / 8.4 / 35 / 99 ms at 0 / 10 / 20 / 35 / 50 / 75 / 100 % (LYNX's attack law was
  already close), τr1 8.5 / 9.2 / 56 / 123 / 172 / 298 / 905 ms and τr2 103 / 104 / 125 / 176 / 243 / 681 / 2449 ms.
  RMS error of the gain trajectory 0.08 – 0.36 dB for attack ≤ 35 % (the classic detector: 0.6 – 1.2 dB); slow attacks
  (50 – 100 %) fit only to 0.5 – 1.3 dB (the reference's attack has a slower tail than one pole).

LYNX: **Detector** (appended; new instances and translated Pro-MB states = measured, projects saved before round 150
keep the classic one through `upgradeLoadedEffectParams`, the editor shows "Old det." for them, one click updates).

## 4. Side chain per band; the narrow free filter [EXEC]

- `Band n Side Chain Input` (Plug-in Input | External Input, Expert mode): two bands split at 1 kHz, band 1 external
  and a loud 200 Hz side chain: band 1 −10.31 dB, band 2 (plug-in) −0.12 dB. LYNX: per band **SC input** Global (the
  old External SC switch) | Plug-in | External; translated from Pro-MB states (Expert on).
- The free side-chain filter of round 147 (measured: +13.7 dB at the centre of 2 – 2.5 kHz, +10.6 for 2 – 3 kHz, +5.7
  for one octave) refitted: the 4th-order Butterworth high / low pass (section Q × 0.98 … 1.06) times a bell at the
  band's centre: 16.9 dB / Q 4.81 for 0.32 octave, 12.6 / 2.57 for 0.58, 4.85 / 0.76 for 1, 1.36 / 0.20 for 1.58, none
  from 3 octaves (rms 0.09 / 0.10 / 0.21 / 0.28 dB). LYNX's ScFilter uses it with the measured detector (projects saved
  before keep round 147's filter).

## 5. Tests

tests/Parity150Tests.inc: the slope laws and the quantisation, the layout (order, gaps, no overlap), Dynamic phase with
6 dB/oct (edge shelf, bell, 6 / 24 at −20 / −12 / +6 dB, 6 / 12) and 36 dB/oct against the trial's numbers (curve and
rendered tones), Linear phase at 9 dB/oct, Minimum / Linear phase with a gap, the soft range and the release after a
step (against the trial), the per-band side-chain input, the narrow free side-chain filter, the Pro-MB translation to
the free layout. tests/DynamicsTests108.inc: the Pro-MB translation now expects the free layout.

## Where I stopped

(filled in at the end of the round)
