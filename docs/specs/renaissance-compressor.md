# Waves Renaissance Compressor (RComp): spec dossier

LYNX substitute: **Compressor** (`EffectKind::compressor`, `src/core/Effects.cpp` class `Compressor`), translated by
`translateWavesRComp` in `src/core/PluginSubstitutes.cpp`.
Round 125 (spec125). Compiled 2026-10-21. Clean-room: built only from the public user guide, published papers and
patents, and black-box renders of the official trial that rounds 113 and 119 already made. No Waves binary was
opened, disassembled or instrumented.

Tags: [DOC] vendor manual, [PAPER] academic, [PATENT], [MEAS] measurement (ours = repo doc or the raw renders listed
in §9), [model] our inference (the basis is stated each time). Inline refs `[Sn]` point to §9.

---

## 1. Identity

| Item | Value |
|---|---|
| Vendor / product | Waves Audio Ltd, *Renaissance Compressor* ("RComp"). Inside a WaveShell it is the component "RCompressor Stereo" (or Mono) [S11][MEAS] |
| What it is | A digital compressor/expander. The guide says it "combines the proven technologies found in the C1 Compressor/Gate and the L1 Ultramaximizer, coupled with Waves Auto Release Control (ARC) technology" [S1][DOC]. It is not an emulation of one analog unit. "Opto" is a release *behaviour* modelled on opto cells, not an LA-2A model [S1][DOC] |
| Internal resolution | "64-bit floating point… dithered to 32-bit floating point at the final output" [S1][DOC] |
| Versions she uses | Her projects reference WaveShell1-VST3 **15.5** (RComp, DeEsser and Doubler are not installed on her laptop). Our measurements used the V17 trial (WaveShell1-VST3 17.1) [S11][MEAS] |
| VST3 class ID (stereo) | `5554535643527253636f6d7072657373` [S13] |
| Corpus | 727 slots / 399 projects in the 412-project inventory, **2 states**. Both states have the *same* RealWorld list; the binaries differ only by one wrapper byte [S15][MEAS]. Round 113 counts 48 slots in her 24 main projects, always in the vocal chain SSL E-Channel → NS1 → **RComp** → DeEsser → CLA-2A [S11][S12] |
| Her state | Threshold −15 dB, ratio 2.95:1, attack 0.5 ms, release 160 ms, **ARC** on, gain 0 dB, **Warm**, **Electro**, mix 100 %, trim 0 dB [S11][MEAS] |

In the original chain this plugin does real work: on her raw take it lowers the level by **−9.6 dB**, with no make-up
at gain 0 [S11][MEAS]. FL on her laptop leaves the slot empty because the 15.5 shell is missing (round 113 §1, round
119 §5). So "identical to FL" there means "bypassed", while "identical to the intended sound" means this compressor.

---

## 2. Signal flow

```
in L/R ──┬──────────────────────────────────────────────[delay ≈ 64 smp @48k]──►(×)──► mix (dry/wet) ──► output gain ──► limiter (0 dBFS) ──► trim ──► out
         │                                                                        ▲
         └─► level detector (linked; peak-like) ─► dB ─► gain computer ────────────┘
                 (no side-chain filter: the                (threshold, ratio,       smoothing: attack + release,
                  GR on pink is spectrally flat)            soft knee ≈ "6 dB below  release = Manual or ARC, ARC shaped by
                                                            threshold")             Electro/Opto; Warm/Smooth = character
```

- **Detector** [MEAS + model]: no side-chain EQ. At −18 dBFS RMS, pink noise gets −6.2…−6.4 dB in every octave from
  63 Hz to 16 kHz (§4.4). Stereo linking is not measured; LYNX links with max(|L|,|R|). The detector is closer to
  peak than to RMS (§4.3).
- **Latency** [MEAS]: the output trails the input by **64 samples at 48 kHz** (1.33 ms). This comes from
  cross-correlating the pink renders; the sine renders agree modulo their period (16 mod 48 at 1 kHz, 0 mod 8 at
  6 kHz). It may be look-ahead (L1 heritage [S1]). Whether this delay is reported to the host is not confirmed.
- **Output section** [DOC]: an "Output gain fader" of −30…+30 dB feeds a built-in limiter with "Threshold and Output
  Ceilings… set to 0 dBFS", which acts only when the output would pass 0 dBFS. The limiter's GR adds to the meter.
  A **Trim** of −18…+18 dB comes after it ("post-limiter") [S1].
- **Saturation**: "Warm adds low frequency harmonics as deeper compression is applied. This also results in greater
  gain reduction" [S1][DOC]. On steady 1 kHz and 6 kHz sines our renders show no harmonics: H2–H5 stay below −150 dB
  at every level, Warm, 0 dB in [S15][MEAS]. So the Warm harmonics are not a static waveshaper. They are most likely
  gain modulation inside the waveform period on low-frequency material (detector ripple that is not smoothed away)
  [model].

---

## 3. Controls

The normalised laws come from moving each VST parameter alone (text at 0.25 and 0.75) and reading her state back
through the plugin (`rcomp_map.txt`, `rcomp_probe.txt`) [S15][MEAS]. The ranges and defaults come from the guide
[S1][DOC]. The RealWorld list has 28 values. Index = position in that list (`wavesRealWorldParams`).

| Control | Range | Unit | Default | Law (VST normalised v) | Audible effect | RealWorld index | Her value |
|---|---|---|---|---|---|---|---|
| Threshold | −60 … 0 [S1] | dBFS | 0.0 [S1] | linear: −60 + 60 v (0.25 → −45, 0.75 → −15) [MEAS] | "input level around which compression… starts acting to a significant degree" [S1] | **1** | −15 |
| Ratio | 0.50 … 50.0 (1.01–50 compression, 0.99–0.50 expansion) [S1] | :1 | 1.0 [S1] | decreasing with v (0.25 → 10.46, 0.452 → 2.95, 0.75 → 0.79); exact law not fitted [MEAS] | slope above the knee | **9** (stored as the ratio itself, 2.96) | 2.95 |
| Attack | 0.5 … 500 [S1] | ms | 16 [S1] (RW default 16.03) | log: 0.5·1000^v (0 → 0.5, 0.25 → 2.81, 0.75 → 88.9) [MEAS] | onset time ("how long… for the gain adjustment to reach its target") [S1] | **8** | 0.5 |
| Release | 5 … 5000 [S1] | ms | 160 [S1] (RW 160.31) | log: 5·1000^v (0.25 → 28.1, 0.502 → 160, 0.75 → 889) [MEAS] | Manual: release time; ARC: "overall scaling factor" [S1] | **0** | 160.31 |
| ARC / Manual | ARC, Manual | switch | ARC? (not stated in the guide) | 0 = ARC, 1 = Manual [MEAS] | program-dependent release | **17** | 0 = ARC |
| Electro / Opto | Electro, Opto | switch | n/s | 0 = Electro, 1 = Opto [MEAS] | release curve shape vs GR (§4.2) | **22** | 0 = Electro |
| Warm / Smooth | Warm, Smooth | switch | n/s | 0 = Warm, 1 = Smooth [MEAS] | LF harmonics plus more GR (Warm) [S1] | **16** | 0 = Warm |
| Gain (output gain) | −30 … +30 [S1] | dB | 0.0 [S1] | linear: 0.25 → −15, 0.75 → +15 [MEAS] | make-up before the 0 dBFS limiter (no auto make-up: −9.6 dB on her take at 0) | **2** | 0 |
| Mix | 0 … 100 % (0.1 % steps) [S1] | % | 100 [S1] | linear [MEAS] | dry/wet | **26** probably (=100; the probe saw no text change) [model] | 100 |
| Trim | −18 … +18 (0.1 dB steps) [S1] | dB | 0.0 [S1] | linear: 0.25 → −9, 0.75 → +9 [MEAS] | post-limiter level | **27** | 0 |
| Bypass | — | — | Off | — | — | — | Off |

RealWorld indices 3–7 and 23–25 are `*` (empty). Indices 10–15 and 18–21 change when edited (56.23, 10, 0, 0,
149.62, 120, 1, 0.4, 0, 1.5) but no visible control follows them [S15][MEAS]. They may be internal, meter or
unexposed parameters. Their default and her values are identical, so they cannot explain any difference in her
corpus [model].

---

## 4. Known curves and behaviour

### 4.1 Static curve at her state (measured)
1 kHz sine stairs from −60 to 0 dBFS peak in 3 dB steps, 0.6 s per step, steady gain in the last 150 ms of each step
[S11][S15][MEAS]. Full resolution (gain in dB):

```
in dBFS peak  -60…-30  -27   -24   -21   -18   -15   -12    -9    -6    -3     0
gain             0.0   -0.1  -0.4  -1.0  -1.8  -3.0  -4.6  -6.4  -8.5 -10.7 -12.8
```
The 6 kHz stairs are identical to 0.1 dB, so there is no frequency dependence of the static curve.

What these numbers say:
- **Reduction starts about 12 dB below the displayed threshold** (−27 dBFS peak), not at "about 6 dB below" as the
  guide says [S1]. At the displayed threshold (−15 dBFS peak) the reduction is already 3.0 dB.
- **The top slope is steeper than the displayed ratio.** From −9 to 0 dBFS the GR grows 6.4 dB per 9 dB, a local
  ratio of ≈3.5:1 against the displayed 2.95:1. A plausible cause is "Warm… results in greater gain reduction" [S1].
  A Smooth measurement is needed to separate it (§8).
- Least-squares fits of the soft-knee gain computer of [S3] (eq. 4.5.1) to these points, with x = sine peak dBFS
  [MEAS fit, `scratchpad/s125rc/rk.py`]:

| fit | T (dBFS peak) | R | W (knee width dB) | RMS error |
|---|---|---|---|---|
| free | −17.75 | 3.60 | 21.5 | 0.03 dB |
| R fixed at the displayed 2.95 | −19.0 | 2.95 | 18.0 | 0.09 dB |
| T fixed at −15 | −15 | 7.85 | 28.5 | 0.10 dB (implausible R) |
| current LYNX translation (hard knee, T −20, R 2.95) | | | | 0.53 dB (round 113 [S11]) |

So at her state the original behaves like a **very soft knee (18–22 dB wide) centred 3–4 dB below the displayed
threshold**.

### 4.2 Release, ARC and Electro / Opto (what the guide says)
Quoted from [S1][DOC]:
- ARC "dynamically tracks the signal for best release values. It calculates the release time of each sample for
  optimum level with minimal artifacts… Set the release time as an overall scaling factor and, depending on the input
  signal, ARC adjusts it from there."
- **Electro**: "a release time that becomes increasingly faster as the gain reduction approaches zero, but only when
  gain reduction is less than 3 dB. When gain reduction is above 3 dB, the release time becomes slower, behaving like
  a leveler in high gain-reduction situations."
- **Opto** is "the inverse of Electro… Release time gets slower as the gain reduction comes back to zero. This is
  true only when the gain reduction is less than 3 dB; when greater than 3 dB, the release time is faster."
- ARC "can deliver very high RMS levels (lower peak/RMS ratio)".

ARC also exists in the L2 and L3 (where it is called "Adaptive Release Control"). We found **no Waves patent**
describing ARC or the Electro/Opto release laws. The closest Waves dynamics patent is US 7,391,875 (M. Shashoua,
peak-limiting mixer). Its background summarises prior-art limiters: release "between few milliseconds to several
hundred milliseconds", "adaptive release… dynamically adjusted according to the signal being processed", look-ahead
with attack aligned to the delay [S8][PATENT]. No ARC formula is public.

### 4.3 Detector type (measured on her take and on pink noise)
- **Pink noise at −18 dBFS RMS gets 6.3 dB of GR.** A sine at the same RMS (−15 dBFS peak) gets 3.0 dB [S15][MEAS].
  Pink noise's crest factor is ~10–12 dB. The extra GR means the detector follows **peaks** and holds them with a
  release of the order of 100 ms. A pure RMS detector would give both signals about the same GR [model].
- **Her raw take (12.8 s, frame = 10 ms)**: the measured GR (percentiles 5/25/50/75/95 % = −10.8 / −9.6 / −6.4 /
  −2.1 / 0 dB) was compared with a feed-forward model: linked |x| peak in dB → soft-knee curve (§4.1) → branching
  smoothing [S3], attack 0.5 ms, 64-sample latency [MEAS fit, `vox2.py`]:

| model | frame RMS error | bias (model − meas) |
|---|---|---|
| hard knee T −20, R 2.95, release 300 ms (≈ today's translation) | 1.71 dB | −1.27 dB (too much GR) |
| free knee (T −17.75, R 3.6, W 21.5), constant release 40 / 120 / 140 / 160 / 200 / 600 ms | 1.55 / 0.70 / **0.63** / 0.66 / 0.87 / 2.78 dB | +1.23 / +0.38 / +0.21 / +0.04 / −0.25 / −1.85 |
| same, RMS detector (10 ms) | 1.78 dB | +0.87 |
| free knee, two-rate "Electro" (release × 0.5 while GR < 3 dB, × 1.5 above), base 110 ms | 0.55 dB | +0.16 |
| **R 2.95, T −19, W 18, two-rate Electro, base 110 ms** | **0.53 dB** | +0.14 |
| two-rate "Opto" (inverse), base 160 ms | 1.00 dB | +0.52 |

Conclusion [model]: a **peak** detector with a release close to the knob value (110–160 ms) explains her take within
~0.5–0.6 dB. Electro's documented shape (faster near 0 GR, slower above 3 dB) improves the fit; the inverse (Opto)
makes it worse. That agrees with her Electro setting and supports the guide's description. ARC's own per-sample
adaptation cannot be separated from these data, because the take has no clean release events (§8).

### 4.4 Spectral neutrality
Pink at −40 dBFS RMS: 0.0 dB in every octave (no static EQ, no Warm colour below the threshold). Pink at −18 dBFS:
−6.4 (63 Hz) … −6.2 (1 kHz) … −6.3 (16 kHz) [S15][MEAS]. The 0.2 dB spread is within noise. The Warm LF effect does
not show on pink at her state.

### 4.5 Attack
Attack is 0.5 ms at her state. The first 5 ms window after each 3 dB step is only 0.2–0.4 dB below the steady
value, so the attack is essentially complete within 5 ms [S15][MEAS]. The guide defines attack as the time for the
gain "to reach its target" [S1]. Whether that means a 1/e constant or a 10–90 % time (as in [S4]) is unknown (§8).

### 4.6 Sample rate, oversampling
Not documented. All our data are at 48 kHz.

---

## 5. Published science

### 5.1 Gain computer (Giannoulis, Massberg & Reiss 2012 [S3]; reproduced in [S4])
With x_G the detector level in dB, T the threshold, R the ratio and W the knee width:

```
            ⎧ x_G                                          2(x_G − T) <  −W
  y_G   =   ⎨ x_G + (1/R − 1)(x_G − T + W/2)² / (2W)       2|x_G − T| ≤  W
            ⎩ T + (x_G − T)/R                              2(x_G − T) >  W
  gain reduction (dB)  c = x_G − y_G   (≥ 0)
```
For R < 1 the same formula gives upward expansion, which RComp supports (0.5–0.99:1 [S1]).

### 5.2 Detector and smoothing topologies [S3]
One-pole coefficient α = exp(−1/(τ·fs)). [S4] defines its times as 10–90 % with α = exp(−ln 9/(fs·T)).
- *Branching* (smooth):
  `y[n] = α_A y[n−1] + (1−α_A) x[n]` if `x[n] > y[n−1]`, else `α_R y[n−1] + (1−α_R) x[n]`.
- *Decoupled* peak detector:
  `y1[n] = max(x[n], α_R y1[n−1] + (1−α_R) x[n]);  y[n] = α_A y[n−1] + (1−α_A) y1[n]`.
- [S3] recommends a **feed-forward** design with the smoothing in the **log (gain) domain after the gain computer**:
  `c_s[n] = branching(c[n])`. This is what LYNX's Compressor already does (it smooths the dB reduction). For a
  feed-back design (detector on the output) the static slope with "ratio" parameter k = R − 1 is still 1/R, but the
  knee and time constants interact with the output level [S3].
- Program-dependent timing: Giannoulis et al. 2013 [S5] automate attack and release from side-chain features (crest
  factor). Trade-press explanations of "auto release" describe switching between a fast and a slow release constant
  according to the current gain reduction [S6]. Neither is Waves' ARC, but both are the family it belongs to.

### 5.3 Two-rate / GR-dependent release (model for Electro / Opto)
A release time that depends on the current smoothed reduction c_s, as the guide describes, can be written:

```
  τ_R(c_s) = τ_knob · h(c_s)
  Electro:  h(c) = h0 + (1 − h0)·(c/3)               for c < 3 dB   (faster toward 0 GR; h0 < 1)
            h(c) = 1 + k_E·(c − 3)                    for c ≥ 3 dB   (slower: "leveler")
  Opto:     h(c) = h0' − (h0' − 1)·(c/3)              for c < 3 dB   (slower toward 0 GR; h0' > 1)
            h(c) = 1 / (1 + k_O·(c − 3))              for c ≥ 3 dB   (faster)
  c_s[n] = c[n] + α(τ)·(c_s[n−1] − c[n])   when c[n] < c_s[n−1],   α(τ) = exp(−1/(τ·fs))
```
h0, k_E, h0' and k_O are unknown and must be fitted (§7, §8). Our two-step proxy (×0.5 / ×1.5 around 3 dB, base
110 ms) already brings her take to 0.53–0.55 dB [MEAS fit]. Opto-cell literature models release with two time
constants and a memory of past GR (the slow second stage of the T4 cell). The guide's Opto mode is a shape on the
release time, not a cell model [S1][model].

### 5.4 Warm vs Smooth [model]
Two readings fit the guide's text [S1]. (a) Warm lets the gain follow the low-frequency waveform: the detector or
smoothing has less ripple filtering, so a 50–100 Hz signal under heavy GR is amplitude-modulated inside each cycle,
which creates harmonics and slightly more average GR. (b) Warm adds a separate LF saturator whose drive tracks GR.
Our steady 1 kHz / 6 kHz data show no harmonics at all, which is compatible with (a). The extra steepness of the
measured curve (≈3.5:1 vs 2.95) is compatible with "greater gain reduction". Decide with the LF measurements in §8.

---

## 6. What LYNX does today

- **Substitute**: Compressor (`Effects.cpp`). It has a linked stereo *peak* detector (max |L|,|R|) and a **hard
  knee**. The reduction is computed in dB and smoothed in dB by branching one-poles, α = exp(−1/(t·fs)). There is
  no latency and no limiter. Make-up is 0…24 dB (no negative gain). Auto gain is optional. Ranges: threshold −60…0,
  ratio 1…20, attack 0.1…100 ms (log), release 10…1000 ms (log) (`Effects.cpp` lines 88–92) [S14].
- **Translation** (`translateWavesRComp`, round 113) [S14][S11]: threshold = RW1 − 5 dB, ratio = RW9, attack = RW8,
  release = **300 ms if ARC** (RW17 = 0) else RW0, make-up = RW2 (clamped 0…24), auto gain off. Warm/Smooth,
  Electro/Opto, mix and trim are not translated. The default when the state is unreadable is 2:1 above −15 dBFS
  with auto gain.
- **Measured residual**: 0.53 dB RMS on the 1 kHz / 6 kHz stairs. Take level −9.48 vs −9.56 dB [S11][MEAS]. In the
  whole chain against the official trials (round 119 §5), the "+RComp" stage leaves reference − LYNX = +0.34 / +0.49
  / +0.44 dB level, IQR 0.82 / 0.70 / 0.81 dB, octave spread 2.70 / 0.42 / 2.29 dB (game over / kwe / luisa). The
  spread is dominated by NS1's 63 Hz octave, not by RComp [S12][MEAS].
- **Frame-level error on her take** (our proxy of today's model, §4.3): 1.71 dB RMS, −1.27 dB bias. The level matches
  on average (power-weighted, loud frames dominate), but quiet and medium frames get too much GR: the hard knee at
  −20 starts later and then too steeply, and 300 ms releases too slowly.
- Single-state calibration: only her one state exists in the corpus, so the −5 dB / 300 ms offsets are fitted at her
  settings only [S11].

---

## 7. Implementation guidance for LYNX (ordered)

1. **Keep the topology**: feed-forward, linked peak detector, gain computed and smoothed in dB (as today, and as [S3]
   recommends). Add the **64-sample (at 48 kHz) delay** to the audio path and report it as latency.
   Scale it as 1.333 ms × fs until other rates are measured (§8). This also lets the 0.5 ms attack catch the onset,
   as in the original.
2. **Soft knee** using [S3] eq. 5.1, with a translation of her values that reproduces §4.1:
   `T_eff = RW1 − 4 dB`, `R_eff = RW9` (2.95), `W = 18 dB`. This gives 0.09 dB RMS on the stairs. If Warm turns out
   to steepen the ratio (§8), use `R_eff ≈ 3.6, T_eff = RW1 − 2.75, W = 21.5` for Warm (0.03 dB RMS) and the nominal
   ratio for Smooth. The knee width probably grows with the ratio or is fixed in dB; with one state we cannot tell.
   Default to a fixed 18 dB until a second ratio is measured.
3. **Release**: drop the fixed "300 ms for ARC". Use τ_knob = RW0 (160 ms) with the GR-dependent law of §5.3.
   - Electro (RW22 = 0): start from the two-step proxy that fits her take (τ = 0.5·τ_base below 3 dB GR,
     1.5·τ_base above, τ_base ≈ 0.7·τ_knob = 110 ms for 160) and smooth the switch over ±1 dB so no gain
     discontinuity is audible.
   - Opto (RW22 = 1): the mirror law (§5.3).
   - Manual (RW17 = 1): a constant τ = RW0 [S1].
   - ARC's extra per-sample adaptation is unknown. Treat ARC as "Electro/Opto shaping on", Manual as "constant
     release", until §8 item 3 is measured.
4. **Attack**: RW8 directly (0.5–500 ms). Keep LYNX's 1/e coefficient until §8 item 4 decides between 1/e and 10–90 %.
5. **Output stage**: gain −30…+30 dB (extend LYNX's 0…24 make-up to signed), then a 0 dBFS output limiter (any
   transparent peak limiter; it never engages on her take at gain 0), then mix, then trim ±18 dB (RW27) [S1].
   Note: the guide's control list implies mix is applied before the limiter/trim; the exact order of mix and gain is
   undocumented (§8).
6. **Warm**: until measured, implement Warm as "no extra LF ripple filter" (the default fast detector) and Smooth as
   the same detector followed by a 2nd-order low-pass on c (≈ 20–30 Hz) to remove in-cycle modulation [model].
   Calibrate after §8 item 2.
7. **Range extension** in LYNX's Compressor (or a dedicated mode): ratio 0.5…50 including expansion, attack to 500 ms,
   release 5…5000 ms, threshold −60…0. All are direct from the RealWorld values, so no normalisation laws are needed.
8. **Tests / pass criteria** (signals already in `C:\Users\kauan\lynx-test\r113\wv\m\`):
   - 1 kHz and 6 kHz stairs: ≤ 0.15 dB RMS vs `rcomp_stairs*.wav` (today 0.53).
   - Pink −18 / −40 dBFS: ≤ 0.3 dB per octave.
   - Her take `in_vox.wav` → `rcomp_vox.wav`: 10 ms frame GR error ≤ 0.6 dB RMS and |bias| ≤ 0.2 dB (proxy model
     0.53 / 0.14); whole-take level within 0.1 dB.
   - Latency: cross-correlation peak at 64 samples (48 kHz).
   - Chain test of round 119 §5: the "+RComp" column level difference ≤ 0.2 dB (today +0.34…+0.49).

---

## 8. Open questions to measure (official trial, black-box only, tools/fxmeasure)

| # | Measurement | Test signal | Decides |
|---|---|---|---|
| 1 | Static curve vs **Warm / Smooth** at her state | 1 kHz stairs (as round 113) with RW16 = 1 | whether Warm steepens the ratio (3.5 vs 2.95) and shifts the knee |
| 2 | **Warm harmonics** | 50 / 100 / 200 Hz sines at −6 dBFS, Warm vs Smooth, attack 0.5 ms, release 5 / 160 ms | in-cycle gain modulation vs waveshaper; H2/H3 levels vs GR |
| 3 | **Release law** (ARC × Electro/Opto × Manual) | 1 kHz tone bursts: 0 dBFS for 1 s, then a step down to −30 / −20 / −15 dBFS; release knob 50 / 160 / 1000 ms; GR envelope at 1 ms resolution | h(c) of §5.3 (time constant as a function of current GR), ARC's dependence on burst length and crest (repeat with 100 ms / 3 s bursts) |
| 4 | **Attack definition** | step −30 → 0 dBFS, attack 5 / 50 / 500 ms | 1/e vs 10–90 %, and whether the attack is in dB or linear |
| 5 | Knee vs ratio | stairs at ratios 1.5 / 2.95 / 10 / 50, threshold −15 | is W fixed (18 dB) or ratio-dependent; is the effective threshold offset (−4 dB) constant |
| 6 | Detector: peak vs RMS vs crest | equal-RMS sine vs square vs pink at −18 dBFS RMS; 2-tone beat 1000/1010 Hz | peak-hold vs RMS window, and linking (L-only signal vs both) |
| 7 | **Latency** at 44.1 / 96 kHz and the host-reported latency | impulse | fixed samples vs fixed ms; report to host |
| 8 | Output limiter | gain +12 dB on 0 dBFS sine and drums | ceiling, release, whether trim is after it |
| 9 | Mix law and position | mix 50 % on stairs | linear dry/wet, before or after gain |
| 10 | Unknown RW 10–15, 18–21 | change each in a state, render stairs | whether any affects sound (e.g. 56.23 / 149.62 could be meter or hidden times) |

---

## 9. Sources

All accessed 2026-10-21.

1. Waves, *Renaissance Compressor User Guide* (controls, ranges, ARC, Electro/Opto, Warm/Smooth, limiter, mix,
   trim): https://assets.wavescdn.com/pdf/plugins/renaissance-compressor.pdf [DOC]
2. Waves, *DeEsser Plug-in Manual* (Waves product family conventions): https://assets.wavescdn.com/pdf/plugins/deesser.pdf [DOC]
3. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis",
   JAES 60(6):399–408, 2012: https://aes2.org/publications/elibrary-page/?id=16354 (also
   https://code.soundsoftware.ac.uk/publications/141) [PAPER]
4. MathWorks, *Audio Toolbox `compressor`* reference (soft-knee and smoothing equations, citing [3]):
   https://au.mathworks.com/help/audio/ref/compressor.html [DOC]
5. D. Giannoulis, M. Massberg, J. D. Reiss, "Parameter Automation in a Dynamic Range Compressor", JAES 61(10):716–726,
   2013: https://aes2.org/publications/elibrary-page/?id=16965 [PAPER]
6. H. Robjohns, "Q. Why does auto-release sound so bad with parallel compression?", Sound On Sound, April 2019
   (trade press: two release constants switched by GR):
   https://www.soundonsound.com/sound-advice/q-why-does-auto-release-sound-so-bad-parallel-compression [DOC]
7. Waves Audio patents assigned to Waves Audio Ltd (search for an ARC patent; none found):
   https://patents.justia.com/assignee/waves-audio-ltd and Google Patents [PATENT]
8. M. Shashoua (Waves Audio Ltd), US 7,391,875 B2, "Peak-limiting mixer for multiple audio tracks" (prior-art review
   of release / adaptive release / look-ahead): https://patents.google.com/patent/US7391875B2/en [PATENT]
9. Waves L3 / ARC naming ("Adaptive Release Control"), Sound On Sound review: https://www.soundonsound.com/reviews/waves-l3 [DOC]
10. LYNX docs/PLUGIN_FALLBACK.md: [PLUGIN_FALLBACK](../PLUGIN_FALLBACK.md) [MEAS]
11. LYNX REA round 113 §1, §3, **§3c**, §4b: [REA_rodada113.md](../analise/REA_rodada113.md) [MEAS]
12. LYNX REA round 119 **§5** (whole chain vs official trials): [REA_rodada119.md](../analise/REA_rodada119.md) [MEAS]
13. LYNX `src/core/PluginSubstitutes.cpp` (`[waves-rcomp]` entry, class ID, `translateWavesRComp`) [MEAS]
14. LYNX `src/core/Effects.cpp` (Compressor class, laws lines 88–92) [MEAS]
15. Raw black-box data of round 113 (official V17 trial, fxmeasure): `~/.claude/jobs/f8789b49/tmp/r113/wv/rcomp_map.txt`,
    `rcomp_probe.txt`, `states.txt`, `RComp_0/1.bin`; renders `C:\Users\kauan\lynx-test\r113\wv\m\{in,rcomp}_{stairs,
    stairs6k,pink18,pink40,vox}.wav`; corpus inventory `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`.
    Re-analysed for this dossier (full stairs, latency, harmonics, soft-knee fits, take model): scripts in the
    spec125 scratchpad `s125rc/{an,rk,vox,vox2,thd}.py` [MEAS]
