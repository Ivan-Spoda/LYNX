# Slate Digital Fresh Air — spec dossier (LYNX substitute: Aether)

Round 125 (spec125). Clean-room dossier: built only from Slate Digital's public user guide and product page, press
coverage of the launch, public patents and papers on exciters / Dolby A, and LYNX's own black-box measurements of the
official trial ([REA round 113 §3b](../analise/REA_rodada113.md)). No Slate / Kilohearts binary was opened,
decompiled or instrumented; licensing was not touched. Tags: [DOC] vendor document · [PAPER] academic · [PATENT] ·
[MEAS] measurement · [model] our inference (with what it rests on). All web sources accessed 2026-10-21.

**One-paragraph summary.** Fresh Air is a free two-knob "dynamic high frequency processor" (Mid Air, High Air, plus
Trim, Link, Bypass) developed for Slate Digital by the Kilohearts team and described by the vendor as inspired by "a
classic Dolby-A noise reduction modded exciter" with "several advanced parallel processes" [S1][S2][DOC]. Press
explains it as the **Dolby-A trick**: high-passed bands are compressed and added back to the dry signal, so
quiet high-frequency content is lifted more than loud content, and no harmonics are generated [S5][S7][DOC].
Our measurement of the official trial (pink noise at −20 dBFS only) gives shelf-like lifts: Mid Air up to +7.2 dB
from ~3 kHz, High Air up to +11.1 dB from ~9 kHz, with a small **dip just below the High Air band** (−0.5 dB at
4 kHz for High 100 %) — the phase signature of a high-passed band summed in parallel, not of a minimum-phase shelf
[MEAS][model]. Her corpus uses **one setting in 720 of 727 slots: Mid 0.29 / High 0.25** (≈ +6.6 dB from 6.8 kHz in
LYNX's shelf fit) ([round 113](../analise/REA_rodada113.md), inventory). The level dependence ("dynamic") has
**never been measured** — the single biggest open question for matching it on her vocal, whose HF level varies by
tens of dB between breaths, consonants and sustained notes.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Slate Digital — Fresh Air ("a dynamic high frequency processor") | [S2][DOC] |
| Developer | "Developed by the Kilohearts Team, created by Slate Digital" | [S1][DOC] |
| Concept | "inspired by a classic Dolby-A noise reduction modded exciter"; "several advanced parallel processes under the hood" | [S1][S2][DOC] |
| Release | Free for Black Friday, 27 Nov 2020 (later offered free again) | [S5][S6][S7][DOC] |
| Formats | macOS AU / VST / VST3 / AAX, Windows VST / VST3 / AAX, 64-bit; iLok (cloud / dongle / machine) | [S2][S7][DOC] |
| Version measured here | 1.1.7 (official trial, installed 2026-10-20) | [round 113 §3b](../analise/REA_rodada113.md) [MEAS] |
| Latency | not published [S1][S2][DOC]; not measured by us | — |
| Hardware lineage | Dolby A-type noise reduction (Ray Dolby, JAES Oct 1967): 4 bands (LP 80 Hz, BP 80 Hz–3 kHz, HP 3 kHz, HP 9 kHz; 12 dB/oct), each a compressor with −40 dB threshold, 2:1, ≈10 dB low-level lift; the "Dolby trick" / "Lennon mod" runs only the encoder with the two low bands disabled, as a vocal "air" effect | [S8][S9][S10][DOC/PAPER] |

**Her usage** ([inventory](../analise/REA_rodada113.md) `tmp/r113/inventory_all.txt`) [MEAS]:
727 slots in 404 projects, **3 distinct states**:

| Mid Air | High Air | slots | Aether today (law of §6): air gain / frequency |
|---|---|---|---|
| 0.29 | 0.25 | 720 | +6.61 dB from 6796 Hz |
| 0.26 | 0.77 | 6 | +11.42 dB from 8388 Hz |
| 0.38 | 0.41 | 1 | +8.90 dB from 6793 Hz |

Trim is not read by LYNX today (state layout: only Mid / High located, at float offsets 1290 / 1294 of the
2530-byte component state; `freshAirAmounts` in `src/core/PluginSubstitutes.cpp`) [MEAS]. Whether her states
carry a non-zero Trim is unknown (§8).

## 2. Signal flow

Vendor-level facts: two "Air" controls, several parallel processes, a dynamic process, true bypass, an output Trim
and an output meter [S1][S2][DOC]. The structure below is our reading, combining the vendor's "Dolby-A modded
exciter" statement [S1], the press description "mixes two high-passed, compressed signals back into the audio
path" [S5], the Dolby A band plan [S8][S9] and our measured curves (§4) [model]:

```
in ──┬───────────────────────────────────────────────(dry, unity)──────────────┐
     │                                                                          │
     ├─ HP ~2–3 kHz (2nd order) ─► band compressor A (level-dependent gain) ─► × f(Mid Air) ─► (+)
     │                                                                          │
     └─ HP ~8–9 kHz (2nd/4th order) ─► band compressor B ─────────────────────► × f(High Air) ─► (+)
                                                                                │
                                                                    Trim (output gain) ─► meter ─► out
```

- Saturation / harmonics: none intended ("isn't an exciter that's going to generate harmonics" [S5]; the Dolby
  trick adds "no new harmonic content" [S10]) [DOC]. Not yet verified with a sine (§8).
- Sidechain: each band's compressor is presumably driven by its own band (Dolby A style: compression "inversely
  proportional to each band's level" [S10]) [model].
- Stereo linking: unknown (§8).

## 3. Controls

| Control | Range | Unit | Default | Law / taper | Audible effect | State mapping |
|---|---|---|---|---|---|---|
| Mid Air | 0–100 % (host value 0–1) | % | unknown | measured law §4 | lift of high mids / presence, "works well on vocals" [S1] | float at offset 1290 of the 2530-byte state [MEAS] |
| High Air | 0–100 % | % | unknown | §4 | lift of highs, "brightness and air" [S1] | float at offset 1294 [MEAS] |
| Link | on / off | — | unknown | locks Mid and High together [S1] | UI convenience | not located |
| Trim | unknown range | dB | 0 (assumed) | output gain [S1] | level-match after the boost | not located (exposed as host parameter "Trim" [MEAS]) |
| Bypass | on / off | — | off | "true bypass that removes all processing" [S1] | — | exposed as host parameter [MEAS] |
| Meter | dBFS; peak line + RMS ball; clip > 0 dBFS | — | — | display only [S1] | — | — |

Host parameters exposed by the trial: "Mid Air", "High Air" (0–100 %), "Bypass", "Trim"
([round 113 §3b](../analise/REA_rodada113.md)) [MEAS].

## 4. Known curves & behaviour

### 4.1 Measured magnitude (official trial, pink noise −20 dBFS RMS, 48 kHz; gain vs input, dB) [MEAS]

From [round 113 §3b](../analise/REA_rodada113.md):

```
               1k    2k    3k    4k    6k    8k   10k   12k   16k   20k
  Mid 0.25   +0.7  +0.5  +1.0  +1.6  +2.2  +2.5  +2.6  +2.7  +2.7  +2.7
  Mid 0.5    +0.7  +0.5  +1.5  +2.6  +3.6  +4.0  +4.2  +4.3  +4.4  +4.4
  Mid 1      -0.0  -0.1  +2.5  +4.6  +6.2  +6.7  +7.0  +7.1  +7.2  +7.2
  High 0.25  +1.0  +0.9  +0.7  +0.5  +0.2  +1.0  +2.3  +3.3  +4.3  +4.6
  High 0.5   +1.4  +1.1  +0.7  +0.2  +0.0  +2.2  +4.7  +6.3  +7.5  +7.8
  High 1     +1.7  +1.2  +0.4  -0.5  +0.3  +4.6  +7.8  +9.5 +10.8 +11.1
```

Observations:
- **Plateaus, not ever-rising**: every curve flattens above ~10 kHz (Mid) / ~16 kHz (High) → a shelf-like or
  "dry + high-passed band" shape [MEAS].
- **Diminishing returns with the knob**: Mid 0.25 / 0.5 / 1 → +2.7 / +4.4 / +7.2 dB plateau; High → +4.6 / +7.8 /
  +11.1 dB. Roughly linear in the band's *amplitude* weight: Mid plateau |1+g| with g ≈ 0.37 / 0.67 / 1.39 [model].
- **The Mid band's corner moves up with the amount** (parallel-HP fit: 1.7 → 1.85 → 2.6 kHz) [model, fit below].
  A compressor in the band would do that only through level, so this hints that the knob also changes the band's
  filter or drive.
- **Dip below the High band** (High 1: −0.5 dB at 4 kHz, +0.3 at 6 kHz before the rise): a minimum-phase high
  shelf cannot dip; `1 + g·HP(s)` with a 2nd/4th-order high-pass can, because the HP output approaches +180°
  phase below its corner and partly cancels the dry signal [model].
- **Broad +1…+1.7 dB at 1–2 kHz with High Air** (and +0.7 with Mid 0.25 / 0.5, but 0 with Mid 1): not explained by
  a high-passed band. Candidates: a low-level broadband gain stage (e.g. part of the "parallel processes"), a
  level-dependent effect on pink noise, or measurement bias at 10-point resolution. Unresolved (§8).

### 4.2 Parallel-band fit to the measured table [model]

We fitted `|1 + g·HP(f; fc, Q, order)|` (dry plus one gained high-passed band; the structure of §2) to each row
(points ≥ 2 kHz), and compared with LYNX's current single RBJ high shelf (S = 0.6). Method:
numpy grid search over fc (1.5–15 kHz, log), Q ∈ {0.3…1.2}, order ∈ {2, 4}, g ∈ [0.05, 3.5]; inputs = the table above
(spec125, script not kept — trivially re-run):

```
  row        parallel band (best)                   RMS   | RBJ shelf S=0.6        RMS
  Mid 0.25   HP2 fc 1707 Hz Q 0.4  g 0.40 (+2.9 dB)  0.08 | fc 3583 Hz  +2.8 dB  0.06
  Mid 0.5    HP2 fc 1845 Hz Q 0.4  g 0.70 (+4.6 dB)  0.10 | fc 3825 Hz  +4.6 dB  0.16
  Mid 1      HP2 fc 2583 Hz Q 0.5  g 1.39 (+7.6 dB)  0.15 | fc 3909 Hz  +7.8 dB  0.61
  High 0.25  HP2 fc 5060 Hz Q 0.3  g 0.99 (+6.0 dB)  0.68 | fc 14429 Hz +7.2 dB  0.47
  High 0.5   HP4 fc 7460 Hz Q 1.2  g 1.27 (+7.1 dB)  0.91 | fc 13814 Hz +12.2 dB 0.90
  High 1     HP4 fc 7655 Hz Q 0.9  g 2.83 (+11.7 dB) 0.88 | fc 9752 Hz  +13.0 dB 1.60
```

- Mid Air is very well described by **dry + a 2nd-order high-passed band** (≤ 0.15 dB RMS), with corners near
  the Dolby A band-3 edge (3 kHz, 12 dB/oct [S9]) [model].
- High Air is fitted equally badly by both shapes at this resolution (0.5–1.6 dB): it rises from ~0 dB at 6 kHz to
  +7.8 dB at 10 kHz (High 1) — steeper than any 2nd-order shelf — and its fitted corner (7.5–9 kHz for the strong
  settings) is close to the Dolby A band-4 edge (9 kHz [S9]) [model]. A finer sweep is needed (§8).
- Both bands overlap above 9 kHz, as Dolby A bands 3 and 4 do ("the two high-pass bands stack" [S9]) [DOC].

### 4.3 Dynamics ("dynamic process") — not measured

- Vendor: "Fresh Air is a dynamic process" [S1]; "advanced dynamics processing" [S2] [DOC].
- Dolby A law (what a "modded Dolby A" would do): per band, a compressor whose output is added to the dry path;
  below the threshold (−40 dB re Dolby level) the band is lifted by up to ≈10 dB, at high level the band's added
  component becomes negligible; 2:1 between [S8][S9][S10] [DOC/PAPER]. Hence: **quiet HF gets brighter, loud HF
  stays almost unchanged** [S10].
- Consequence for her vocal (model): at −20 dBFS pink noise the 3–20 kHz bands sit roughly 10–25 dB below full
  scale; on a loud sibilant (HF near −10 dBFS) the lift could be several dB smaller than our table, on breaths and
  reverb tails larger. A static shelf (Aether today) over-brightens sibilants and under-brightens tails, relative
  to a dynamic original [model, untested].
- Time constants, detector type (peak / RMS), threshold calibration (which dBFS = "Dolby level") and stereo
  linking: all unknown (§8).

### 4.4 Other behaviour
- Harmonics: none claimed [S5][S10]; unverified (§8).
- Latency / oversampling / sample-rate dependence: unpublished, unmeasured (§8).
- Real-material check: in FL (where Fresh Air *is* installed on this PC) the project "game over" rendered by FL
  matched LYNX with only Aether active (IQR 1.1 dB, 8–16 kHz within 0.8 dB)
  ([round 119 §5](../analise/REA_rodada119.md)) [MEAS] — i.e. on her actual vocal the static-shelf substitute is
  already within ~1 dB in the top octaves at her setting.

## 5. Published science

### 5.1 Dolby A / the "Dolby trick" (the vendor's stated inspiration)
- Dolby A (Dolby 1967 [S8]): four fixed bands, each with a "fixed-band circuit" (compressor) whose output is
  **added to the main path** (the "differential" / side-chain topology), so that the encoder is
  `y = x + Σ_b c_b(x_b)`, with `c_b` a limiting compressor of band b. Bands: LP 80 Hz, BP 80 Hz–3 kHz, HP 3 kHz,
  HP 9 kHz; filters 12 dB/oct; threshold −40 dB; 2:1; ≈10 dB lift at low level (noise reduction 10 dB, up to
  15 dB at 15 kHz where bands 3 and 4 stack) [S9][S11][DOC/PAPER].
- The trick (vocal "air"): encode only, with bands 1–2 disabled (also called "Lennon mod", "Air Hack",
  "Stretch Mod"); "quieter sounds get brighter while louder sounds remain almost unchanged"; no new harmonics
  [S10][DOC]. Sound On Sound describes rebuilding it with stock DAW plug-ins (HP → compressor → parallel sum)
  [S12][DOC].

A minimal per-band model consistent with this (our writing of the textbook law) [model]:

```
x_b[n]   = HP_b(x)[n]                                   (2nd order, fc_b ≈ 3 kHz / 9 kHz)
L_b[n]   = level detector of x_b (dB), attack/release τ_a, τ_r
G_b(L)   = G_max                               if L < T          (dB of the added path)
         = G_max − (1 − 1/R)·(L − T)             if L ≥ T, floored at −∞ (path fades out)
y[n]     = x[n] + Σ_b  k_b(knob_b) · 10^(G_b(L_b)/20) · x_b[n]
```
with T ≈ −40 dB re reference level, R = 2, and G_max chosen so that `|1 + g| ≈ +10 dB` at low level.

### 5.2 Harmonic exciters (contrast: what Fresh Air says it is NOT)
- Aphex Aural Exciter, US 4,150,253 (Knoppel, filed 1977, granted 1979): a 2-pole high-pass (example 4 kHz,
  claims also 1 kHz), a harmonic generator (diode soft clip → odd and even harmonics), optionally thresholded so
  only transients pass, mixed back at 20–70 % of the main path's level (≈ −14 to −3 dB) [S13][PATENT].
- Aether's own exciter (LYNX, `MixSuite.cpp`) follows the same idea (HP band → even/odd shaper at 4× → added) and
  must stay **off** for Fresh Air [model; S5].

### 5.3 Compressor design for the band dynamics
- Feed-forward log-domain gain computer with soft knee and smoothed attack/release detectors (branching / decoupled
  peak or RMS detectors) — Giannoulis, Massberg & Reiss, JAES 60(6), 2012 [S14][PAPER]. Gain computer:
  `y_G = x_G` (below T−W/2); `x_G + (1/R−1)(x_G−T+W/2)²/(2W)` (knee); `T + (x_G−T)/R` (above). For an *upward*
  (Dolby-style) lift, apply it to the added band's gain rather than to the signal [model].

## 6. What LYNX does today

- Substitute: **Aether** (`src/core/MixSuite.cpp`, namespace `aether`): an RBJ high shelf (S = 0.6, gain 0–15 dB,
  frequency 5–20 kHz, log map) plus an optional HP-band harmonic generator at 4× oversampling, mix and output
  [code]. Aether reports the oversampler's latency even when the harmonics are off [code].
- Translation `translate = fresh-air` (`translateFreshAir`, `src/core/PluginSubstitutes.cpp`) [code]:
  - gain = clamp(10.345 m + 20.548 h − 2.32 m² − 7.75 h² − 11.673 m h, 0, 15) dB
  - log2 f = clamp(12.8069 − 0.7108 m + 0.6746 h − 0.5369 m h) → 5–20 kHz
  - harmonics = 0 (exciter off); Trim not read.
- Measured error: within 0.43–0.69 dB RMS of the measured curves for her settings (up to 1.6 dB for High alone at
  100 %, never used) ([round 113 §3b](../analise/REA_rodada113.md)); on "game over" vs FL with the real plugin:
  8–16 kHz within 0.8 dB, IQR 1.1 dB ([round 119 §5](../analise/REA_rodada119.md)) [MEAS].
- Limits: static (no level dependence); a single shelf cannot make the measured sub-band dip or the steep 6→10 kHz
  rise of High Air; the 1–2 kHz broad lift is not modelled; her setting (0.29, 0.25) itself was never measured
  directly (the law interpolates between single-knob rows and the seven pairs fitted in round 113) [MEAS/model].

## 7. Implementation guidance for LYNX

Ordered; each step is independently shippable. Keep the old shelf path as a fallback (older projects).

1. **Measure first (§8 items 1–3)** — her exact setting (0.29 / 0.25) at five input levels and with a log sweep.
   Without the level data, any dynamics added would be guesswork; if the curve at her setting changes < 0.5 dB
   between −40 and −10 dBFS, stop at step 2.
2. **Replace the shelf by the parallel-band structure** (a new Aether "air model", or a dedicated mode):
   `y = x + gM·HP2(x; fM, QM) + gH·HPn(x; fH, QH)` with per-knob tables (fc, Q, g) fitted to the sweeps of step 1
   at 0, 0.25, 0.5, 0.75, 1 for each knob and her three pairs; interpolate linearly in the knob. Starting values
   from §4.2: Mid fc 1.7–2.6 kHz, Q 0.4–0.5, g = 0.40 / 0.70 / 1.39 at 0.25 / 0.5 / 1; High fc ≈ 7.5–9 kHz,
   4th order, g 1.0–2.8. Check whether the two bands interact (fit m+h pairs as a sum of the single-knob paths; if
   the pair residual is > 0.3 dB, add a cross term or a shared compressor).
3. **Add the band dynamics** if step 1 shows level dependence: per band, detector (start RMS, τ ≈ 1–10 ms attack,
   50–200 ms release, to be fitted), gain law of §5.1 (threshold T, ratio R, max lift) applied to the band's
   added gain; calibrate T against the absolute dBFS where the lift starts to shrink. Stereo: link by max of L/R
   detectors unless §8 item 5 says otherwise.
4. **Trim**: locate it in the state (§8 item 6) and apply it as output gain.
5. **Latency**: if Fresh Air reports 0 samples (§8 item 7), give the air model a zero-latency path (no
   oversampler when harmonics are off), so the dry/wet alignment in FL-style parallel routing matches.
6. **Pass criteria** (her three vocal takes `in_go`, `in_kwe`, `in_lu` from `tmp/w119/q`, and pink noise /
   sweeps at −40…−6 dBFS): octave bands 1–16 kHz within ±0.5 dB RMS of the trial at her setting at every level;
   0.5 s-window short-term HF (8–16 kHz) level IQR difference ≤ 0.5 dB on her takes (catches dynamics); null test
   after level match better than −25 dB in 2–16 kHz on sustained vowels.

## 8. Open questions to measure (official trial, black-box only, tools/fxmeasure)

- [ ] 1. **Level dependence at her setting** (0.29 / 0.25): pink noise at −50, −40, −30, −20, −10, −6 dBFS RMS;
  octave / third-octave gains. Decides static vs dynamic and gives T, R, max lift.
- [ ] 2. **Fine magnitude/phase**: exponential sine sweep (20 Hz–20 kHz, −30 dBFS) at Mid / High 0, .25, .5, .75,
  1 and her three pairs; deconvolve → magnitude **and phase** (a parallel HP band shows the phase wiggle and the
  sub-band dip; a minimum-phase shelf does not). Decides the filter orders, corners, Q, and the 1–2 kHz lift.
- [ ] 3. **Two-tone level test**: a 1 kHz tone at −10 dBFS plus a 10 kHz probe at −50 dBFS, then probe alone →
  is the HF band's gain driven by the band's own level (Dolby style) or by the broadband level?
- [ ] 4. **Time constants**: 10 kHz tone bursts stepping −50 → −10 → −50 dBFS; fit attack / release of the lift.
- [ ] 5. **Stereo linking**: HF burst in L only, quiet HF in R → does R's lift change?
- [ ] 6. **Trim location / range / default**: set Trim to known values, save the state, diff offsets (as was done
  for Mid / High in round 113).
- [ ] 7. **Latency**: impulse at Mid / High 0 and at her setting; read the reported latency and the impulse peak.
- [ ] 8. **Harmonics**: 1 kHz and 5 kHz sines at −20 / −6 dBFS → THD; confirms "no harmonics" (and whether the
  band compressors add level-dependent distortion through fast gain changes).
- [ ] 9. **Sample rate**: repeat item 2 at 44.1 and 96 kHz (corner frequencies fixed in Hz or warped?).

## 9. Sources

1. Slate Digital, *Fresh Air* user guide — https://docs.slatedigital.com/FreshAir/Fresh%20Air.html — accessed 2026-10-21 — [DOC]
2. Slate Digital, *Fresh Air* product page — https://slatedigital.com/fresh-air/ — accessed 2026-10-21 — [DOC]
3. LYNX REA round 113 §3b (Fresh Air measured) — [../analise/REA_rodada113.md](../analise/REA_rodada113.md) — [MEAS]
4. LYNX REA round 119 §5 (whole chain; FL render with Fresh Air vs Aether) — [../analise/REA_rodada119.md](../analise/REA_rodada119.md) — [MEAS]
5. MusicTech, "Get the most out of Fresh Air, Slate Digital's free exciter plug-in" (27 Nov 2020) — https://musictech.com/tutorials/slate-fresh-air-exciter-plugin-preview/ — accessed 2026-10-21 — [DOC] (press)
6. Production Expert, "Slate Digital Fresh Air plugin free for a limited time" — https://production-expert.com/production-expert-1/slate-digital-fresh-air-plugin-free-for-a-limited-time — accessed 2026-10-21 — [DOC] (press)
7. Bedroom Producers Blog, "Slate Digital releases Fresh Air" (27 Nov 2020) — https://bedroomproducersblog.com/2020/11/27/slate-fresh-air/ — accessed 2026-10-21 — [DOC] (press)
8. R. M. Dolby, "An Audio Noise Reduction System", JAES, Oct 1967, pp. 383–388 — https://aes2.org/publications/elibrary-page/?id=1083 — accessed 2026-10-21 — [PAPER] (abstract; full text not read)
9. Wikipedia, "Dolby noise-reduction system" (Dolby A bands, 12 dB/oct, −40 dB threshold, 2:1, 10 dB / 15 dB at 15 kHz) — https://en.wikipedia.org/wiki/Dolby_noise-reduction_system — accessed 2026-10-21 — [DOC] (secondary, cites [8])
10. AudioThing, "The Dolby® A Trick" — https://www.audiothing.net/blog/the-dolby-a-trick/ — accessed 2026-10-21 — [DOC] (vendor blog)
11. Radio-Electronics, "Dolby system" (Oct 1969; four bands, ≥10 dB low-level lift) — https://www.rfcafe.com/references/radio-electronics/dolby-system-radio-electronics-october-1969.htm — accessed 2026-10-21 (search snippet; page returned 403) — [DOC]
12. Sound On Sound, "Bright Idea" (Dolby A trick with Cubase plug-ins) — https://www.soundonsound.com/techniques/bright-idea — accessed 2026-10-21 (search summary) — [DOC]
13. C. A. Knoppel, US 4,150,253 "Signal distortion circuit and method of use" (Aphex Aural Exciter), 1979 — https://patents.google.com/patent/US4150253A/en — accessed 2026-10-21 — [PATENT]
14. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6) 2012, pp. 399–408 — https://aes2.org/publications/elibrary-page/?id=16354 — accessed 2026-10-21 — [PAPER]
15. LYNX source: Aether `src/core/MixSuite.cpp`; translation `translateFreshAir` / `freshAirAmounts` in `src/core/PluginSubstitutes.cpp`; inventory `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt` — [code / MEAS]
