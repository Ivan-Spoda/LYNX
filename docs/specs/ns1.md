# Waves NS1 Noise Suppressor — spec dossier (LYNX substitute: Quell Denoiser)

Round 125 (spec125). Clean-room dossier: built only from the public user guide, the vendor's product / launch
pages, a public Waves patent, the academic literature and LYNX's own black-box measurements of the official trial
([REA round 119](../analise/REA_rodada119.md)). No Waves binary was opened, decompiled or instrumented.
Tags: [DOC] vendor document · [PAPER] academic · [PATENT] · [MEAS] measurement · [model] our inference.
All web sources accessed 2026-10-21.

**One-paragraph summary.** NS1 is a one-fader, zero-setup, "automatic adaptive real-time" broadband noise
suppressor for voice [S1][DOC]. Waves publishes no algorithm details, band count, time constants or latency
[S1][S2][S3][DOC]. Our black-box tests ([round 119 §3](../analise/REA_rodada119.md)) show a frequency-dependent,
level-dependent downward gain (loud voice untouched, quiet parts down ~25 dB, lows below ~105 Hz pulled down even
when fairly loud, top band a steady −2 dB) with fast opening (< 50 ms), ~60 ms closing and a slow (seconds)
drift of the attenuation of steady noise [MEAS]. That combination is the signature of a **short-time spectral gain
suppressor driven by an adaptive noise-floor estimate** (minimum-statistics-like tracker + SNR-based gain rule with
decision-directed smoothing) rather than of a fixed-threshold multiband gate [model]. LYNX's Quell today is the
latter (6-band expander with absolute thresholds, fitted): it is within 0.13–0.61 dB RMS per octave on her raw
takes, but misses by +5…+7 dB in the 63 Hz octave when NS1 sits after the SSL E-Channel's high-pass
([round 119 §5](../analise/REA_rodada119.md)) — the case a noise-floor-relative rule explains naturally [model].

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves Audio Ltd — NS1 Noise Suppressor | [S1][DOC] |
| Launched | announced 14 Sep 2012 (SoundGrid / MultiRack Native compatible; Native and TDM) | [S4][DOC] |
| Components | NS1 Mono, NS1 Stereo (the guide's component list is garbled: "two components each in two channel configurations") | [S1][S2][DOC] |
| Marketing description | "intelligently differentiates between dialog and unwanted noise … instantly analyzes and adapts to your signal in real-time" | [S1][DOC] |
| Controls (per manual) | FADER "determines the amount of noise suppression"; ATTENUATION METER "displays the amount [of] overall energy removed [from] the input signal" | [S1][DOC] |
| Version she uses | saved in her projects under WaveShell1-VST3 16.0 / 15.5; measured with the 17.x trial shell | [round 119 §1, §5](../analise/REA_rodada119.md) [MEAS] |
| Hardware lineage | none (native DSP design; no analog original). Conceptual ancestors: single-ended hiss reducers / multiband downward expanders, and speech-enhancement spectral suppressors (§5) | [model] |

Third-party descriptions agree there are no documented attack, release, ratio, learn/profile or band controls,
and that the meter scale runs 0 … −48 dB [S13][DOC, third-party]. (The official trial's meter as screenshotted in
round 119 shows a reduction meter left and one tall fader right [MEAS].)

**Her usage.** Inventory: **727 slots in 399 projects, 2 distinct states** (`tmp/r113/inventory_all.txt`) [MEAS].
The six NS1 states probed in round 119 read back as 23.1 % (×3), 29.8 %, 39.5 %, 69.6 % (`tmp/w119/wv/NS1_read.tsv`);
23.1 % is her dominant setting [MEAS] ([round 119 §3](../analise/REA_rodada119.md)). Position in her vocal insert:
Auto-Tune Pro → SSL E-Channel → **NS1** → RComp → DeEsser → CLA-2A ([README](README.md),
[round 119 §5](../analise/REA_rodada119.md)). So in practice NS1 always sees a signal that has already been through
the SSL channel (her SSL state includes a high-pass) — this matters for §4.4.

## 2. Signal flow

Nothing official beyond "analyzes and adapts" exists [S1]. The black-box behaviour (§4) is consistent with:

```
in L/R ──┬───────────────────────────────────────────────► delay (= analysis latency, if any) ──► × G(k,t) ──► out
         │                                                                                       ▲
         └─► analysis (filter bank or STFT, k = band/bin)                                        │
               ├─► |X(k,t)|² smoothed ──► noise-floor tracker N(k,t)  (slow, minimum-like; seconds) │
               ├─► SNR estimates γ = |X|²/N, ξ (decision-directed)                                 │
               ├─► gain rule G = f(ξ, γ; Amount)  with floor G_min(Amount) and per-band offsets   │
               └─► time smoothing (fast open < 50 ms, close ~60 ms), stereo-linked ─────────────────┘
meter = overall energy removed (dB)
```

[model] — based on the measured asymmetry, frequency dependence and slow drift (§4) and on the standard
architecture of §5. Stereo linking, band layout and latency are **unmeasured** (§8). There is no audible saturation
or noise stage; the only "colour" is the gain itself.

## 3. Controls

| Control | Range | Unit | Default | Law / taper | Audible effect | State mapping |
|---|---|---|---|---|---|---|
| NS1 (fader) | 0 … 100 | % | not documented (LYNX preset uses 25 %) | linear in % on the fader read-out [MEAS]; the internal law of thresholds vs % is unknown — round 119 found "the higher the knob the higher the thresholds (100 % attenuates even loud tones)" | amount of suppression | RealWorld value **229** (0–100 %) [MEAS] ([round 119 §3](../analise/REA_rodada119.md); `translateWavesNs1` in `src/core/PluginSubstitutes.cpp`) |
| Attenuation meter | 0 … −48 (third-party reading of the scale) | dB | — | overall energy removed | display only | — |

There is no output, mix, listen or bypass-of-band control in NS1 [S1][S13]. Quell's Listen / Output are LYNX
additions (defaults keep them neutral).

## 4. Known curves & behaviour

All from the official trial, black-box, round 119 (`tmp/w119/py/ns1a.py … ns3b.py`, `nsana.py`, `qfit.py`) [MEAS]
unless tagged otherwise.

### 4.1 Static (level → gain)
- Loud vocal material: level change −0.1 dB (essentially transparent at speech peaks) [MEAS].
- Quiet passages: down up to ~25 dB [MEAS].
- Thresholds rise with the fader; at 100 % even loud tones are attenuated [MEAS].
- Frequency dependence: lows below ~105 Hz are pulled down even when fairly loud; the top band is a steady ~−2 dB
  [MEAS]. LYNX's fit expresses this as per-band thresholds/ranges/trims (Quell tuning, §6).

### 4.2 Dynamics
- Opening (gain recovery when signal appears): < 50 ms [MEAS].
- Closing (gain falls when signal stops): ~60 ms [MEAS].
- Slow adaptation: the attenuation of a **steady** noise drifts by a few dB over seconds [MEAS] → a noise-floor
  estimate that updates on a seconds time scale [model].

### 4.3 Interpretation: why a noise-floor tracker [model]
- "100 % attenuates even loud tones": a minimum-statistics tracker treats any **stationary** component (a steady
  test tone, hum, rumble) as noise after its search window (~1.5 s, [S9][S10]), so steady tones lose gain however
  loud they are, while speech (non-stationary, with pauses) keeps its gain. This explains the "loud tones attenuated"
  observation better than a high absolute threshold.
- "Lows < 105 Hz pulled down even when fairly loud": room rumble / handling noise / mains hum in the low band is
  stationary, so the tracked floor there is high and the SNR-based gain is low even when the band is loud.
- "Top band steady −2 dB": a gain floor / bias in a band where speech rarely exceeds the tracked floor (air band of
  a vocal) — or a fixed spectral weighting [model; distinguish with §8 item 4].
- Fast open, slower close: exactly the asymmetry the **decision-directed** a priori SNR produces (onsets follow the
  instantaneous γ within one frame; offsets decay through the previous-frame amplitude term) [S7][S10], plus any
  explicit gain smoothing.

### 4.4 The 63 Hz discrepancy after the SSL high-pass ([round 119 §5](../analise/REA_rodada119.md))
In her chain (SSL → NS1 → RComp → DeEsser → CLA-2A) the reference has **+5…+7 dB more** in the 63 Hz octave than
LYNX; from 125 Hz up every octave is within ±1.7 dB (mostly ±0.5) [MEAS]. Quell's low band uses **absolute**
thresholds fitted on raw takes (where the low band holds rumble at a fairly high level). After the SSL's high-pass
the low band is much quieter, so it falls far below Quell's absolute threshold and gets nearly the full range
(up to ~25 dB) of reduction. A noise-floor-relative rule behaves differently: the HPF lowers the tracked floor and the
signal together, so the band's SNR — and thus NS1's gain — is roughly unchanged, i.e. NS1 removes **less** in
absolute terms there [model]. Prediction to test (§8 item 2): NS1's per-band gain is (nearly) invariant to a
static pre-filter / level offset of the input; Quell's is not.

### 4.5 Unknowns
Latency (not reported in round 119), analysis resolution (bands vs FFT bins), stereo linking, sample-rate
behaviour, musical-noise handling, and the exact noise-tracker time constants are not measured (§8).

## 5. Published science

### 5.1 Waves' own patent (context, not proof of NS1's internals)
WO2014181330A1 / US 9,818,424 B2, "A method and apparatus for suppression of unwanted audio signals", Waves Audio Ltd,
inventors I. Neoran, S. Nedjar, R. Yam, priority 2013-05-06 [S5][PATENT]. It is a **post-processor** after a linear
reduction stage (echo canceller, beamformer, dereverberation, noise suppressor): per time–frequency cell it takes
envelopes of a reference input R and a processed input P, forms G1 ∝ P/(R + ε) (G1 = 1 when P ≥ R), expands it
(G2 = expansion of G1 for G1 < 1, soft-knee), applies G_out = G2/G1, and smooths the gain with a one-pole whose
coefficient depends on whether the band is rising or falling:
`state(i,n) = a·state(i,n−1) + (1−a)·G_out(i,n)`, `a = a_up or a_down` [S5][PATENT].
Relevance: shows Waves' toolbox around 2012–2013 (NS1's launch year) — T–F gains, expansion with soft knee,
attack/release-asymmetric gain smoothing, optional external noise-profile estimator setting thresholds. It does not
describe NS1 [model].

### 5.2 Spectral subtraction (Boll 1979) [S6][PAPER]
With the STFT `Y(k,l) = S(k,l) + D(k,l)` and a noise magnitude estimate `|D̂(k)|` (averaged during non-speech):

    |Ŝ(k,l)| = max( |Y(k,l)| − |D̂(k)| , 0 )        (half-wave rectified magnitude subtraction)
    Ŝ(k,l)   = |Ŝ(k,l)| · e^{j∠Y(k,l)}              (noisy phase kept)

Power form as a gain: `G = max( 1 − α·|D̂|²/|Y|² , β )^{1/2}` with over-subtraction α ≥ 1 and spectral floor β
(Berouti et al. 1979 — bibliographic data from memory, not re-verified online this round). Weakness: **musical
noise** (isolated short-lived spectral peaks surviving the subtraction) [S6][S10].

### 5.3 Wiener and MMSE-STSA / LSA (Ephraim–Malah 1984, 1985) [S7][S8][PAPER]
A posteriori SNR `γ(k,l) = |Y(k,l)|² / λ_D(k)`, a priori SNR `ξ(k,l) = λ_S(k,l) / λ_D(k)`.

- Wiener gain: `G_W = ξ / (1 + ξ)`.
- MMSE-STSA: with `v = ξ·γ / (1 + ξ)`,
  `G_STSA = (√π / 2) · (√v / γ) · exp(−v/2) · [ (1 + v)·I₀(v/2) + v·I₁(v/2) ]` [S7].
- MMSE log-spectral amplitude (1985): `G_LSA = ξ/(1+ξ) · exp( ½ ∫_v^∞ e^{−t}/t dt )` [S8].
- **Decision-directed** a priori SNR [S7]:
  `ξ̂(k,l) = α · |Â(k,l−1)|² / λ_D(k,l−1) + (1 − α) · max( γ(k,l) − 1, 0 )`, typically α ≈ 0.98.

Cappé 1994 showed why this removes musical noise: in noise-only frames ξ̂ is a heavily smoothed quantity, so the
gain does not fluctuate with γ; at speech onsets ξ̂ follows γ with about one frame of delay; at offsets it decays
smoothly [S10][PAPER]. A lower bound `ξ_min` (gain floor) further masks residual noise [S10]. This is the textbook
mechanism for "fast open, slower close" without explicit attack/release controls [model].

### 5.4 Noise-floor tracking without a VAD: minimum statistics (Martin 2001) [S9][PAPER]
Per bin, smooth the periodogram `P(k,l) = α(k,l)·P(k,l−1) + (1 − α(k,l))·|Y(k,l)|²` (Martin derives a time- and
frequency-varying optimal α), then take the minimum over a sliding window of D frames and bias-compensate:
`λ̂_D(k,l) = B_min · min_{l−D < m ≤ l} P(k,m)`. The window is typically ~1.5 s (split into sub-windows to cut memory
and delay) [S11][PATENT, secondary description]; the estimate lags a **rise** of the noise by about the window length
[S12][PAPER], while it follows a **fall** almost immediately (the minimum drops at once) [model from the definition].
Consequences that match NS1's black box: stationary tones are absorbed into the "noise" after ~a window; steady-noise
attenuation drifts over seconds [model].

### 5.5 Multiband downward expansion (the family Quell belongs to)
Per band b, level `L_b` (dB, detector-smoothed), threshold `T_b`, slope s (dB/dB), range R_b, soft knee W
(Giannoulis–Massberg–Reiss gain computer mirrored below threshold [S14][PAPER]):

    u = T_b − L_b
    r(u) = 0                          for u ≤ −W/2
         = s·(u + W/2)² / (2W)        for |u| < W/2
         = s·u                        for u ≥  W/2
    gain_dB = −min(R_b, r(u)) − trim_b

Bands split by Linkwitz–Riley crossovers with all-pass compensation so the sum is phase-coherent [S15][PAPER].
Difference from §5.3–5.4: thresholds are **absolute** (dBFS) instead of relative to a tracked floor; no notion of
stationarity [model].

## 6. What LYNX does today

- Role: `[waves-ns1]` → **Quell Denoiser**, `translate = waves-ns1`: RealWorld value 229 / 100 → Amount
  (`src/core/PluginSubstitutes.cpp`, `translateWavesNs1`) [MEAS-backed mapping].
- Engine (`src/core/VoiceTools.cpp`, namespace `quell`): 6-band LR4 split at 105 / 311 / 921 / 2727 / 8075 Hz with
  all-pass compensation; per band a power detector (attack 2 ms, release 5 ms; "a sine reads its peak level"); the
  soft-knee expander of §5.5 with slope 1.40 dB/dB, knee 19 dB; per-band threshold, range and constant trim each
  linearly interpolated over Amount (thr0 → thr1: −34→−21, −52→−42.5, −43.5→−39, −47.5→−40, −51→−42, −65→−48.5 dB;
  range 8→25, 27→26, 29→27.5, 26→26, 29→26, 13.5→17.5 dB; trim 0→1.5, 0→−0.75, 0→0.25, 0.5→0, 0.5→0.5, 2.3→3.2 dB);
  gain smoothing open 2 ms / close 45 ms (one-pole on the dB gain); linked stereo; zero latency.
- Fit: coordinate descent (`tmp/w119/py/qfit.py`) on the reference's STFT band gains (20 ms frames, 6 bands), three
  takes raw and −12 dB, Amount 23 / 40 / 70 %: mean |error| 3.46 → 1.75 dB per frame and band [MEAS].
- Residual on her takes, her states: level −0.08…+0.15 dB; octave bands 0.13–0.61 dB RMS [MEAS]
  ([round 119 §3](../analise/REA_rodada119.md)).
- Residual in her chain after "+NS1": octave spread 0.57–1.68 dB, dominated by the 63 Hz octave (+5…+7 dB in the
  reference) [MEAS] ([round 119 §5](../analise/REA_rodada119.md)).
- Not modelled: the seconds-scale noise-floor adaptation; stationarity discrimination; per-bin resolution.

## 7. Implementation guidance for LYNX (ordered)

1. **First decide absolute vs relative (cheap, data exists).** Round 119 already recorded the reference on the same
   takes raw and at −12 dB. Compare the reference's per-band gain trajectories between the two: if NS1's gains at
   −12 dB equal its raw gains (shifted ≤ 2 dB), the rule is SNR-relative (§5.3/5.4); if they look like the raw
   ones with thresholds 12 dB lower in input terms, it is absolute (§5.5). Then run §8 items 1–2 to confirm.
2. **Add a per-band noise-floor tracker to Quell (keep zero latency, keep the 6-band IIR split).** For each band,
   at a 5 ms control rate: smoothed power `P_b` (one-pole ~20 ms, the qfit frame), running minimum over sub-windows
   (e.g. 8 × 190 ms ≈ 1.5 s), bias factor `B_min` (~1.5–2, calibrate) → `N_b`. Also a hard lower clamp `N_b ≥ N_abs,b`
   (so silence/digital zero doesn't drive thresholds to −∞) [model, from §5.4].
3. **Make thresholds relative**: `T_b = max( N_b + Δ_b(Amount), T_abs,b(Amount) )`, i.e. a hybrid that reduces to
   today's tuning when the floor is low and tracks the floor when it is high. Δ_b(Amount) and the fallback absolute
   thresholds are new fit parameters; keep slope/knee/range/trim.
   Alternative (if §8 shows bin-level resolution and a latency): an STFT suppressor (e.g. 1024-point Hann, hop 256 at
   48 kHz, WOLA) with minimum-statistics λ_D, decision-directed ξ (α ≈ 0.98), LSA or Wiener gain with floor
   `G_min(Amount)`, then the same gain mapped through an Amount-dependent over-subtraction. Only choose this if NS1
   reports latency ≥ the frame; otherwise the IIR hybrid is the faithful choice.
4. **Gain smoothing**: keep asymmetric one-pole on the dB gain; re-fit open/close against step tests (§8 item 3) with
   targets open < 50 ms, close ≈ 60 ms (10–90 %); note the current 2 / 45 ms constants are one-pole τ, not 10–90 %
   times (10–90 % ≈ 2.2 τ → 4.4 / 99 ms — check which the fit actually reproduces).
5. **Refit** with `qfit.py` on an enlarged set: her three takes raw, −12 dB, **and after her SSL E-Channel state**
   (the real operating point), plus a 30-s steady pink-noise + speech file to exercise the tracker.
6. **Pass criteria**: (a) her NS1 states on her takes: level within ±0.2 dB, every octave 63 Hz–16 kHz within
   ±0.5 dB RMS; (b) chain test of round 119 §5 after "+NS1": 63 Hz octave within ±1.5 dB (today +5…+7), all other
   octaves unchanged or better; (c) steady-noise drift: attenuation vs time of a 20 s pink noise at −50 dBFS within
   ±1 dB of the reference at 1, 2, 5, 10 s; (d) 1 kHz steady tone at −20 dBFS, Amount 100 %: attenuation within
   ±2 dB of the reference after 5 s.
7. Keep `translate = waves-ns1` (value 229 / 100) unchanged; old projects must load identically except for the new
   engine's sound.

## 8. Open questions to measure (official trial, black-box only)

1. **Stationarity**: 1 kHz tone at −20 dBFS, steady for 10 s vs the same tone 50 % gated at 2 Hz, Amount 23 / 70 /
   100 %. Gain vs time. Decides: minimum-statistics-like tracker (steady tone loses gain over ~1–2 s) vs pure level
   expander (both the same).
2. **Level / pre-filter invariance**: her lead take at 0, −12, −24 dB and through her SSL E-Channel state (HPF);
   per-octave reference gain. Decides absolute vs relative thresholds; directly explains the 63 Hz octave.
3. **Noise-floor step**: pink noise −60 dBFS for 10 s → −40 dBFS for 10 s → −60 dBFS, with a 1 kHz 200 ms burst every
   second. Attenuation of the noise vs time after each step. Decides the tracker's rise lag (~window length) vs fall
   speed, i.e. the window length.
4. **Resolution**: two-tone tests (e.g. 100 + 115 Hz, 1000 + 1060 Hz) with one tone steady and one gated; does the
   gated tone get its own gain? Also a log sweep at −60 dBFS to map per-frequency gain at Amount 23 %. Decides FFT
   bins vs ~6 bands, and the top band's −2 dB (a floor or a weighting).
5. **Latency**: impulse / click through NS1 at Amount 0 and 23 %; cross-correlate. Also read the reported latency
   (host query). Decides IIR vs STFT implementation.
6. **Stereo linking**: noise in L only, speech in both; does R's gain follow L's? Decides linked vs independent.
7. **Musical noise**: white noise −50 dBFS at Amount 69.6 % (her highest); spectrogram of the output for isolated
   tonal blips. Decides whether a ξ-smoothing / gain floor is needed.
8. **Sample rate**: repeat item 4's sweep at 44.1 and 96 kHz. Decides whether band edges scale with sr (FFT) or are
   fixed in Hz.
9. Fader law: Amount 0, 10, 20 … 100 % on a fixed speech+noise file; total attenuation vs %. Decides whether Quell's
   linear interpolation of thresholds over Amount is right or needs a curve.

Remember the trial's demo mute (~1 s of exact zeros every ~20 s): mask it as round 119 did.

## 9. Sources

1. Waves NS1 Noise Suppressor User Guide — https://assets.wavescdn.com/pdf/plugins/ns1-noise-suppressor.pdf — accessed 2026-10-21 — [DOC]
2. Waves NS1 User Guide (v2) — https://assets.wavescdn.com/pdf/plugins/ns1-noise-suppressor-v2.pdf — accessed 2026-10-21 — [DOC]
3. Waves NS1 product page — https://www.waves.com/plugins/ns1-noise-suppressor — accessed 2026-10-21 — [DOC]
4. Sonicstate, "Waves Unveils NS1 Noise Suppressor" (launch news, 14 Sep 2012) — https://sonicstate.com/news/2012/09/14/waves-unveils-ns1-noise-suppressor-/ — accessed 2026-10-21 — [DOC]
5. I. Neoran, S. Nedjar, R. Yam (Waves Audio Ltd), WO2014181330A1 "A method and apparatus for suppression of unwanted audio signals" (family US 9,818,424 B2) — https://patents.google.com/patent/WO2014181330A1/en — accessed 2026-10-21 — [PATENT]
6. S. F. Boll, "Suppression of acoustic noise in speech using spectral subtraction", IEEE Trans. ASSP 27(2):113–120, 1979, doi:10.1109/TASSP.1979.1163209 — https://doi.org/10.1109/TASSP.1979.1163209 — accessed 2026-10-21 — [PAPER]
7. Y. Ephraim, D. Malah, "Speech enhancement using a minimum mean-square error short-time spectral amplitude estimator", IEEE Trans. ASSP 32(6):1109–1121, 1984 — https://malah.net.technion.ac.il/files/2017/12/Ephraim_Speech_Enhancement_ASSP84.pdf — accessed 2026-10-21 — [PAPER]
8. Y. Ephraim, D. Malah, "Speech enhancement using a minimum mean-square error log-spectral amplitude estimator", IEEE Trans. ASSP 33(2), 1985 — https://malah.net.technion.ac.il/files/2017/12/Ephraim_Speech_Enhancement_Minimum_ASSP85.pdf — accessed 2026-10-21 — [PAPER]
9. R. Martin, "Noise power spectral density estimation based on optimal smoothing and minimum statistics", IEEE Trans. Speech Audio Process. 9(5):504–512, 2001 — https://www.iks.rwth-aachen.de/publikationen/publications-detail//martin01a — accessed 2026-10-21 — [PAPER]
10. O. Cappé, "Elimination of the musical noise phenomenon with the Ephraim and Malah noise suppressor", IEEE Trans. Speech Audio Process. 2(2):345–349, 1994 — https://www.di.ens.fr/olivier.cappe/Publications/Self-archive/ephma94.pdf — accessed 2026-10-21 — [PAPER]
11. US 8,694,311 (describes the minimum-statistics search window as ~the last 1.5 s) — https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/8694311 — accessed 2026-10-21 — [PATENT]
12. J. S. Erkelens, R. Heusdens et al., IEEE TASL 2008 (minimum-tracker lag ≈ window length) — https://sps.ewi.tudelft.nl/pubs/erkelens08tasl.pdf — accessed 2026-10-21 — [PAPER]
13. J. Nilo, "Waves NS1 explained" (third-party description of controls/meter) — https://josephnilo.com/blog/waves-ns1-explained/ — accessed 2026-10-21 — [DOC, third-party]
14. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012 (cited in `src/core/VoiceTools.cpp`; not re-fetched) — [PAPER]
15. S. Linkwitz, "Active Crossover Networks for Noncoincident Drivers", JAES 24(1), 1976 (cited in `src/core/VoiceTools.cpp`; not re-fetched) — [PAPER]
16. LYNX REA round 119 (NS1 measured, Quell fit, whole chain) — [../analise/REA_rodada119.md](../analise/REA_rodada119.md) — [MEAS]
17. LYNX source: `src/core/VoiceTools.cpp` (Quell), `src/core/PluginSubstitutes.cpp` (`[waves-ns1]`, `translateWavesNs1`) — [MEAS-backed code]
18. Her corpus inventory and NS1 probes: `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`, `tmp/w119/wv/NS1_read.tsv`, `tmp/w119/py/qfit.py` — [MEAS]
