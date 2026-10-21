# Waves DeEsser: spec dossier

LYNX substitute: **Hush De-esser** (`EffectKind::hushDeEsser`, `src/core/MixSuite.cpp` namespace `hush`), translated
by `translateWavesDeEsser` in `src/core/PluginSubstitutes.cpp`.
Round 125 (spec125). Compiled 2026-10-21. Clean-room: built only from the public manual, papers and patents, and
black-box renders of the official trial that round 113 already made. No Waves binary was opened, disassembled or
instrumented.

Tags: [DOC] vendor manual, [PAPER] academic, [PATENT], [MEAS] measurement (ours = repo doc or the raw renders listed
in §9), [model] our inference (the basis is stated each time). Inline refs `[Sn]` point to §9.

---

## 1. Identity

| Item | Value |
|---|---|
| Vendor / product | Waves Audio Ltd, *DeEsser* (the classic one, not *Renaissance DeEsser*). WaveShell components "DeEsser Mono" / "DeEsser Stereo" [S1][S12] |
| What it is | A side-chain-filtered compressor for "ess, shh and chh sounds". Its "very sharp filters in the SideChain" feed an energy detector and a **hard-knee compressor**, and the attenuation is applied either to the whole signal (Wideband) or to the high band of a split (Split) [S1][DOC]. It has no analog lineage |
| Versions she uses | Her projects reference WaveShell1-VST3 **15.5** (not installed on her laptop, so FL leaves the slot empty there). Our measurements used the V17 trial (WaveShell1-VST3 17.1) [S9][MEAS] |
| Corpus | 727 slots / 399 projects in the 412-project inventory, **2 states with an identical RealWorld list** (the binaries differ by one wrapper byte) [S12][MEAS]. 48 slots in her 24 main projects, always after RComp in the vocal chain [S9] |
| Her state | **Freq 5506 Hz, threshold −24.2 dB, SideChain HighPass, Audio Split, Monitor Output.** This is the factory **Default** setup with only the threshold pulled down from 0 dB [S1][S12][MEAS] |

Her state is the default described in the manual: "The default frequency setting is 5506Hz, which will attenuate down
to about 3kHz, the main attenuation taking place from 5kHz up. The default SideChain filter mode is HighPass… The
default audio setting is Split mode" [S1][DOC].

**How much it acts on her material** [MEAS]: on her raw take (peak −2.3 dBFS) it changes the >3 kHz band by at most
−1.1 dB. Only 4 of 1104 10-ms frames go below −1 dB, and the median is 0.0 dB. The take level changes −0.05 dB
[S9][S12]. In her real chain it comes after RComp (−9.6 dB), so it works even less. In round 119's chain test the
"+DeEsser" stage moves the reference-minus-LYNX level by only 0.00–0.01 dB [S10]. **The main risk for the substitute
is doing too much, not too little.**

---

## 2. Signal flow

From the manual's "Internal Design" chapter [S1][DOC], with our measured details in brackets:

```
in ──┬───────────────────────────────────────────────────────────────────────┐   (no latency measured: 0 samples)
     │                                                                       ▼
     │                                         Split:  low = LP1(x)  ──────────(+)──► out
     │                                                 high = x − low ──(× g)──┘
     │                                         Wideband: out = g · x
     │                                                         ▲
     └─► SideChain filter ─► energy detector ─► hard-knee compressor ─► g (attenuation, dB ≥ 0)
         (HighPass or BandPass at Freq;          ("dB below 0 dBFS")     (threshold = Thresh)
          "very sharp")
     Monitor = SideChain: out = filtered side-chain signal (to tune Freq)
```

- The side-chain filter "is only related to the SideChain, not the audio" [S1]. **[MEAS]**: the audio split is *not*
  at Freq. Both the 6 kHz stairs and pink noise fit a **first-order complementary split at ≈ 1.72–1.75 kHz** for
  Freq = 5506 Hz (§4.2). That is ≈ Freq/π.
- Hard knee: "When the SideChain energy is below threshold, the compressor generates no attenuation. When energy
  exceeds the threshold, attenuation is generated — more energy above the threshold means more attenuation" [S1].
- No range control. The attenuation floor seen on a 6 kHz sine (~ −10…−11 dB) is the low band of the gentle split
  leaking through (§4.2) [MEAS + model].
- Saturation and noise: none. 1 kHz stairs are bit-transparent up to 0 dBFS (H2–H5 < −145 dB). On 6 kHz under
  attenuation, H3 is around −91…−98 dBc, from the gain moving inside the cycle [S12][MEAS].

---

## 3. Controls

The laws come from moving each VST parameter alone and reading her state back (`deesser_map.txt`,
`deesser_probe.txt`) [S12][MEAS]. The definitions come from the manual [S1][DOC]. The RealWorld list has 15 values.

| Control | Range | Unit | Default | Law (VST normalised v) | Audible effect | RealWorld index | Her value |
|---|---|---|---|---|---|---|---|
| Thresh | −80 … 0 [MEAS] | dB (side-chain energy, "dB below 0 dBFS" [S1]) | 0 (RW default list) [MEAS] | linear: −80 + 80 v (0.25 → −60, 0.75 → −20, 0.6975 → −24.2) [MEAS] | "level above which attenuation takes place in the SideChain"; too low = "muffled" [S1] | **2** | −24.2 |
| Freq | 2000 … 16000 [MEAS] | Hz | 5506 [S1] | log: 2000·8^v (0.25 → 3364, 0.487 → 5506, 0.75 → 9514) [MEAS] | side-chain filter frequency; also moves the audio split (§4.2) [model] | **0** | 5506 |
| SideChain | HighPass, BandPass | switch | HighPass [S1] | VST v: BandPass at 0.25, HighPass at 0.75 [MEAS] | HP "looks at all the frequencies above", BP "a narrow band around the set frequency" [S1] | **13** (1 = HP, 2 = BP) | 1 = HP |
| Audio | Split, Wideband | switch | Split [S1] | — | Split: attenuation on the high band only; Wideband: on everything [S1] | **14** (0 = Split, 1 = Wideband) | 0 = Split |
| Monitor | Output, SideChain | switch | Output | — | listen to the side-chain [S1] | **10** (1 = SideChain) | 0 |
| Bypass | — | — | Off | — | — | — | Off |

Meters [S1]: "Energy Detector — Energy of the SideChain (dB below 0 dBFS)", aligned with the threshold slider; "Attn —
instantaneous gain reduction"; output peak hold.
RealWorld 1, 3, 4, 5, 11, 12 (2, 7, 0.5, 10, 3.98, 32) change when edited but no visible control follows them
[S12][MEAS]. They may be hidden parameters (for example attack/release, ratio, BP Q) or meter settings. Her values
equal the defaults.
Factory presets [S1]: Male Ess 4500 Hz, Male Shh 3385 Hz ("will reach down to about 1700Hz"), Female Ess 6779 Hz,
Female Shh 5077 Hz. All presets use **Split + BandPass**; the Default uses Split + HighPass.

---

## 4. Known curves and behaviour

### 4.1 Static curve at her state (measured)
6 kHz sine stairs from −60 to 0 dBFS peak in 3 dB steps, steady gain in the last 150 ms of each step [S9][S12][MEAS]:

```
in dBFS peak  -60…-21  -18   -15   -12    -9    -6    -3     0
gain at 6 kHz    0.0   -1.4  -3.6  -5.6  -7.3  -8.6  -9.6 -10.2
gain at 1 kHz    0.0 at every level (side-chain HP rejects it)
```
(The −33 dBFS step of the 6 kHz file has a dropout in the input itself and is ignored.)
- Attack: the first 5 ms after a step already sits within 0.2–0.3 dB of the steady value, so attack is fast (≲ 2 ms)
  [MEAS]. Release is not measurable from rising stairs (§8).
- Latency: 0 samples (cross-correlation of the pink renders) [MEAS].

### 4.2 Model that reproduces the curve to 0.01 dB [MEAS fit, `scratchpad/s125rc/de.py`]
Hard-knee compressor on the side-chain level, with output = low band + g × high band, and a first-order split:

```
  L_sc   = side-chain level of the 6 kHz sine (dB, peak-referenced)
  c      = (L_sc − T') (1 − 1/R)   for L_sc > T',  else 0          (hard knee [S1])
  g      = 10^(−c/20)
  H(s)   = s/(s + ω_s)                (first-order high-pass, split corner f_s)
  out/in = | (1 − H) + g·H |          (complementary first-order split)
```
Best fit: **T' = −19.75 dBFS sine peak, R ≈ 7.0, f_s = 1719 Hz, first order: 0.014 dB RMS**.
A 2nd-order Butterworth split fits worse (f_s 1234 Hz, 0.022 dB). Ratio ∞ (a pure limiter) fits much worse
(0.12 dB).

Cross-checks:
- **Pink noise at −18 dBFS RMS** (time-averaged spectrum change): −0.1 (63 Hz), −0.3 (500), −0.6 (1 k), −1.4 (2 k),
  −1.9 (3 k), −2.2 (4 k), −2.4 (5 k), −2.5 (6 k), −2.7 (8–10 k), −2.8 dB (12–16 k). This fits the same first-order
  complementary split at **1753 Hz** with an average 2.85 dB high-band attenuation (0.05 dB RMS) [MEAS fit, `fit.py`].
  Two independent signals give the same corner.
- The manual says the default (5506 Hz) "will attenuate down to about 3kHz", and Male Shh (3385 Hz) "will reach down
  to about 1700Hz, but with lesser and lesser maximum gain reduction" [S1][DOC]. Both say the audio split sits well
  below Freq and scales with it: roughly 0.5 × Freq for "noticeable" attenuation, which for a first-order split at
  0.31 × Freq is where about half the high-band attenuation appears. We therefore model **f_s = Freq/π ≈ 0.318 ×
  Freq** (5506/π = 1753 Hz) [model]. The exact law vs Freq is unmeasured (§8).
- Threshold reference: the displayed threshold is −24.2 and the fitted T' = −19.75 dBFS sine peak, a difference of
  4.45 dB. An **RMS ("energy") detector** reads a sine 3.01 dB below its peak. The rest (1.44 dB) is the side-chain
  HP loss at 6 kHz for a filter with its corner at 5506 Hz (a 2nd-order Butterworth would lose 2.3 dB there, a
  4th-order 1.8 dB, a steeper one less). So T' = T + 3.01 + L_HP(6 kHz) [model]. The detector integration time and
  the filter order are open (§8).
- The floor: as g → 0 the output at 6 kHz tends to |1 − H| = |1/(1 + j·6000/1719)| = **−11.2 dB**. That is why the
  original "flattens about 10 dB down" (round 113) without any range control [model, consistent with S1].

### 4.3 BandPass, Wideband, Monitor
Not measured at her state (she uses HP + Split). The manual describes BP as "a narrow band around the set frequency"
and says it is the mode of all factory presets [S1]. Its Q is unknown (§8).

### 4.4 Sample rate
Not documented. All our data are at 48 kHz.

---

## 5. Published science

### 5.1 De-esser topologies
- **Wideband (classic)**: a compressor whose detector hears a high-passed or band-passed copy, with the gain applied
  to the whole signal. The manual calls it "the most traditional approach for purist or vocal-only applications"
  [S1]. Orban's US 5,574,791 is a hardware example: two RMS log detectors (high-pass, and a band-pass that
  approximates 70-phon loudness weighting). Their *difference* (log of the HF-to-loudness-weighted power ratio)
  minus a threshold drives a VCA on the full band, with attack ≈ 1 ms and release ≈ 30 ms [S4][PATENT].
- **Split-band**: the gain acts only on a high band, low = LP(x), out = low + g·(x − low). This is a dynamic high
  shelf. With a complementary first-order pair the bands always sum flat at g = 1. Waves DeEsser's Split is this
  form, with a gentle corner well below Freq (§4.2).
- **Adaptive / relative-threshold de-essers**: Wolters, Sapp & Becker-Schweitzer (AES 104th Conv., 1998, preprint
  4677) adapt a time-variant band-pass from psychoacoustic sibilance measures [S5][PAPER]. Patents describe adaptive
  filtering de-essers (US 6,373,953) and sibilance detection adapted to specific sounds (US 12,462,826) [S6][PATENT].
  Waves DeEsser is **not** relative: the manual describes a plain threshold on the filtered energy [S1]. Its
  threshold is absolute, so level changes ahead of it (RComp, SSL) change its action.

### 5.2 Equations for the LYNX implementation
Side-chain HP (Butterworth order N, corner f_c = Freq) or BP (RBJ constant-skirt band-pass, centre Freq, Q unknown).
Energy detector (one-pole on x², time constant τ_e):

```
  e[n]  = α_e e[n−1] + (1 − α_e) s[n]²,      α_e = exp(−1/(τ_e fs))
  L[n]  = 10 log10(e[n])                      (RMS dB; a sine reads peak − 3.01)
  c[n]  = max(0, L[n] − T) · (1 − 1/R)        (hard knee, R ≈ 7)
  c_s   = branching attack/release smoothing of c (Giannoulis et al. [S7])
  Split: low = LP1(x; f_s),  f_s ≈ Freq/π;   out = low + 10^(−c_s/20) · (x − low)
  Wideband: out = 10^(−c_s/20) · x
  LP1 by bilinear transform:  K = tan(π f_s / fs),  b0 = b1 = K/(1+K),  a1 = (K−1)/(K+1)
```

---

## 6. What LYNX does today

- **Hush De-esser** (`MixSuite.cpp`, round 96) [S11]: the detector is HP (2nd-order, Q 0.707) or BP (Q 1.4) at the
  frequency (2–16 kHz log). The detector is a *peak* with instant attack and a release of 10–250 ms. There is a 3 dB
  soft knee and **GR = level − threshold (ratio ∞), clamped to Range (0–24 dB)**. GR smoothing: attack 0.5 ms, or
  2 ms/3 with look-ahead (fixed 2 ms delay, latency reported). Split = complementary **2nd-order low-pass at the
  frequency** (`split.filter(sr, 0, f, 0.707)`), out = low + (x − low)·g. Wideband, listen, output ±12 dB.
- **Translation** (`translateWavesDeEsser`, round 113) [S11][S9]: frequency = 0.81 × RW0 (5506 → 4460 Hz), threshold
  = RW2 + 5 dB (−19.2), range fixed 12 dB, mode = RW14, detector = RW13 (2 = BP), listen = RW10. Look-ahead and
  release are left at Hush's defaults (on, 0.56 → ~61 ms). The default when the state is unreadable is 5.5 kHz,
  −20 dBFS, 4 dB range.
- **Measured residual** [S9][MEAS]: 0.84 dB RMS on the stairs (6 kHz weighted × 3); 1 kHz untouched in both. In
  Hush-alone renders on `game over` (vs no substitute, level-normalised), the octaves from 125 Hz to 16 kHz read −0.6
  +0.1 +0.1 +0.1 −0.2 −0.6 −1.5 −1.2 dB (before the round-113 fix: −3.7 / −4.8 at 8 / 16 kHz). In the chain against
  the official trials, the DeEsser stage adds ≤ 0.01 dB level difference [S10].
- **Structural mismatches** [model, from §4.2]:
  1. The original's split is first-order at ≈ Freq/π. Hush's is 2nd-order at the frequency, so Hush does not reach
     down to 2–3 kHz as the original does (pink: −1.4 dB at 2 kHz, −1.9 at 3 kHz in the original).
  2. Hush's 12 dB range and ratio-∞ knee were fitted to imitate a floor that is really the split's low-band leakage.
     On real sibilants that are broadband (4–12 kHz), the original's attenuation at 10–16 kHz can go deeper than at
     6 kHz (the leakage at 12 kHz is |1/(1+j·7)| = −17 dB), while Hush clamps every band at 12 dB.
  3. Detector: the original reads RMS energy with a finite ratio (≈7); Hush reads peak with ratio ∞ and a +5 dB
     threshold offset to compensate.

---

## 7. Implementation guidance for LYNX (ordered)

1. **Split filter**: add a "Waves" split variant to Hush, a first-order complementary pair with
   `f_s = Freq / π` (bilinear LP1 of §5.2), out = low + g·(x − low). This is the main structural fix, and it removes
   the need for a range clamp: set Range to its maximum (24 dB) or bypass the clamp for translated states.
2. **Detector**: side-chain HP at Freq, start with a **4th-order Butterworth** ("very sharp" [S1]; its 6 kHz loss of
   1.8 dB is close to the 1.44 dB inferred); BP: RBJ band-pass, Q to be measured (start at Q ≈ 2). Then an **RMS
   energy** one-pole with τ_e ≈ 1–5 ms (unknown, §8), and a **hard knee with ratio 7** at threshold T = RW2 (no
   offset once the detector is RMS). Calibrate T so a 6 kHz sine starts reducing at −19.75 dBFS peak.
3. **Timing**: attack fast (≤ 1 ms on c). Release unknown: keep ~30 ms (Orban's typical value [S4]) until §8 item 2.
   Keep latency 0 (the original reports none). Drop Hush's 2 ms look-ahead for translated states.
4. **Translation**: frequency = RW0 (no 0.81 factor once the split is right), threshold = RW2, detector = RW13,
   mode = RW14, listen = RW10.
5. **Tests / pass criteria** (signals in `C:\Users\kauan\lynx-test\r113\wv\m\`):
   - 6 kHz stairs vs `deesser_stairs6k.wav`: ≤ 0.2 dB RMS (model 0.014; today 0.84 weighted).
   - 1 kHz stairs: exactly 0 dB.
   - Pink −18 dBFS vs `deesser_pink18.wav`: ≤ 0.3 dB per octave 500 Hz–16 kHz (in particular −1.4 dB at 2 kHz).
   - Pink −40 dBFS: 0.0 dB.
   - Her take `in_vox.wav` vs `deesser_vox.wav`: >3 kHz 10 ms-frame energy within 0.2 dB RMS (bypass already scores
     0.16, so above all **do not over-de-ess**: no frame below −1.5 dB).
   - Round 119 chain: the "+DeEsser" column level difference stays ≤ 0.05 dB.

---

## 8. Open questions to measure (official trial, black-box only)

| # | Measurement | Test signal | Decides |
|---|---|---|---|
| 1 | Split corner vs Freq | pink −18 dBFS (or a 6 kHz stair) at Freq 3385 / 5506 / 9514 Hz, Split, HP | f_s = Freq/π or another law |
| 2 | Attack / release | 6 kHz bursts 0 dBFS 200 ms ↔ −40 dBFS, GR envelope at 0.5 ms resolution | detector τ and GR smoothing (release has no control) |
| 3 | Side-chain filter order and loss | sine stairs at 3, 4, 5, 5.5, 7, 10 kHz, Wideband (to avoid the split) | HP order (onset level vs frequency) and the T' = T + 3.01 + L_HP rule |
| 4 | BandPass Q | same sweep with SideChain = BP | BP bandwidth |
| 5 | Ratio across thresholds | 6 kHz stairs, Wideband, threshold −40 / −24.2 / −10 | constant R ≈ 7 vs level-dependent |
| 6 | RMS vs peak detector | equal-RMS 6 kHz sine vs 6 kHz square-burst / noise band | energy (RMS) as the manual says |
| 7 | Wideband floor | 6 kHz stairs, Wideband | confirms no range clamp (GR should keep growing) |
| 8 | Hidden RW 1, 3, 4, 5, 11, 12 | edit each in a state, render stairs | whether any is attack/release/ratio |
| 9 | Stereo link | L-only sibilant | linked vs independent channels |
| 10 | Monitor output level | SideChain monitor on pink | gain of the listen path |

---

## 9. Sources

All accessed 2026-10-21.

1. Waves, *DeEsser Plug-in Manual* (controls, internal design, FAQ, factory presets):
   https://assets.wavescdn.com/pdf/plugins/deesser.pdf (same text also at
   https://images.equipboard.com/uploads/item/manual/4729/waves-deesser-manual.pdf) [DOC]
2. Waves, *Renaissance Compressor User Guide* (family conventions, WaveSystem): https://assets.wavescdn.com/pdf/plugins/renaissance-compressor.pdf [DOC]
3. (Sound On Sound review of the DeEsser, Split vs Wideband use): https://www.soundonsound.com/node/4924112 (not
   fetched in full; only used for context) [DOC]
4. R. A. Orban (AKG Acoustics / Orban), US 5,574,791, "Combined de-esser and high-frequency enhancer using single
   pair of level detectors", 1996: https://patents.google.com/patent/US5574791A/en [PATENT]
5. M. Wolters, M. Sapp, J. Becker-Schweitzer, adaptive de-esser algorithm, AES 104th Convention (1998), preprint 4677:
   https://aes2.org/publications/elibrary-page/?id=8503 (abstract via index; full text not read) [PAPER]
6. US 6,373,953 "Apparatus and method for De-esser using adaptive filtering algorithms":
   https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6373953; US 12,462,826 "Adapting sibilance
   detection based on detecting specific sounds in an audio signal":
   https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12462826 (titles only) [PATENT]
7. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES
   60(6):399–408, 2012: https://aes2.org/publications/elibrary-page/?id=16354 [PAPER]
8. LYNX docs/PLUGIN_FALLBACK.md: [PLUGIN_FALLBACK](../PLUGIN_FALLBACK.md) [MEAS]
9. LYNX REA round 113 §1, §3, **§3c** (layout, her state, stairs, Hush fit): [REA_rodada113.md](../analise/REA_rodada113.md) [MEAS]
10. LYNX REA round 119 **§5** (whole chain vs official trials): [REA_rodada119.md](../analise/REA_rodada119.md) [MEAS]
11. LYNX source: `src/core/MixSuite.cpp` (namespace `hush`, `hushInfo`), `src/core/PluginSubstitutes.cpp`
    (`[waves-deesser]`, `translateWavesDeEsser`) [MEAS]
12. Raw black-box data of round 113 (official V17 trial, fxmeasure): `~/.claude/jobs/f8789b49/tmp/r113/wv/deesser_map.txt`,
    `deesser_probe.txt`, `states.txt`, `DeEsser_0/1.bin`; renders `C:\Users\kauan\lynx-test\r113\wv\m\{in,deesser}_
    {stairs,stairs6k,pink18,pink40,vox}.wav`; inventory `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`.
    Re-analysed for this dossier (full stairs, latency, pink spectrum, split/knee fits, take activity): spec125
    scratchpad `s125rc/{an,de,fit,thd,dvox}.py` [MEAS]
