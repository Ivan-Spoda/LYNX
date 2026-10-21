# Waves SSL E-Channel — spec dossier (LYNX substitute: Ridge Strip)

Round 125 (spec125). Clean-room dossier, built only from the public Waves user manual, Solid State Logic's own
public user guides and product pages for the E-Series EQ and dynamics, the DSP literature, and LYNX's own black-box
measurements of the official Waves trial ([REA round 113 §3c](../analise/REA_rodada113.md),
[round 119 §5](../analise/REA_rodada119.md)). No Waves or SSL binary was opened, decompiled or instrumented. The
analysis in §4.2–§4.4 is a **re-analysis (2026-10-21) of the renders round 113 already made** with the official
trial. No new plugin runs were made.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [model] our inference.
All web sources accessed 2026-10-21. Companion: [ssl-native-vocalstrip-2.md](ssl-native-vocalstrip-2.md).

**One-paragraph summary.** The Waves SSL E-Channel models the SSL SL4000 E-series channel: the dynamics section of
the 611E strip (soft-knee compressor/limiter with **automatic make-up gain**, expander/gate) plus the **"Black Knob"
242 EQ** (HPF 18 dB/oct, LPF 12 dB/oct, four bands, LF/HF shelf↔bell). It has 1 sample of latency, and its
dynamics sit before the EQ by default [S1][DOC]. Her state (identical in 99 % of 727 slots) is: HP 116 Hz with
Split (filters before the dynamics) → compressor −1.3 dB(u), 1.8:1, fast attack, 0.35 s release → LF −6 dB shelf
at 60 Hz, HMF +3 dB at 3.5 kHz Q 1.5, HF +4.8 dB shelf at 10.16 kHz; Analog off
([round 113 §3c](../analise/REA_rodada113.md)) [MEAS]. The key new findings are these:
**(a)** every band of the Waves model is fitted to ≤ 0.1 dB by a standard textbook digital section, but the
**knob legend is not the filter parameter**. The HF "shelf" is really a very-low-Q digital bell (Q ≈ 0.27 at
12.4 kHz for her 10.16 kHz setting, unity gain at Nyquist). The LF shelf is first-order (6 dB/oct, as SSL documents
for the E EQ shelves [S2]). The HMF peak sits ~1.3× above its legend with a broader Q.
**(b)** Round 113's single-band impulse renders were taken **without letting the parameters settle**. The long LF
and HP impulse responses were therefore distorted: the LF −6 dB @ 60 Hz cut looked like −2 dB from 372 Hz, but the
settled response is −5.7 dB from 139 Hz. That error lives below 150 Hz.
**(c)** Built from four textbook sections (§7.1), her whole EQ matches the settled measurement to **0.18 dB RMS
over 30 Hz–20 kHz** (0.08 dB above 150 Hz). Ridge as fitted today manages 1 dB RMS (0.3 dB above 150 Hz).
So build an SSL-specific EQ from measured laws instead of retuning Ridge's knobs.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves Audio — SSL E-Channel (SSL 4000 Collection: E-Channel, G-Channel, G-Master Bus Compressor, G-Equalizer), made "in partnership" with Solid State Logic | [S1][DOC] |
| What it models | "modeled after the SL4000 E Series console, combining the dynamics section of the SL4000 channel strip with the 'Black Knob' 242 EQ" | [S1][DOC] |
| Latency | "The E-Channel and G-Channel have latency of one sample" (fxmeasure reports `latency=1` too) | [S1][DOC]; `ssl1/b.log` [MEAS] |
| Components | `SSLChannel Stereo` (her projects; VST3 class ID `5354535648437353736c6368616e6e65`); a mono component also exists | `PluginSubstitutes.cpp` [MEAS]; [S1] |
| Shell / version | her projects: WaveShell1-VST3 16.0 (15.5 on some); measured with the V17.1 trial (`SSLChannel Stereo by Waves v17.1.42.50`, 164 VST parameters, 57 RealWorld values) | [round 113 §1, §3c](../analise/REA_rodada113.md), `ssl1/b.log` [MEAS] |
| Level alignment | "The plug-in is aligned so that −18 dBFS = 0dBu"; meters in dBFS, "all parameters are expressed as dBu" | [S1][DOC] |

**Hardware lineage.** The SL4000 was the first console with dynamics on every channel [S1]; SSL dates channel dynamics and EQ on an in-line console to the 4000 B of 1977 [S6][DOC]. Two EQ cards
were fitted to E consoles. The original "Brown Knob" **02** EQ has log-symmetric mid bands ("±3dB up/down points
retain the same musical interval from the centre frequency regardless of frequency and amplitude") and ±15 dB, and
its two shelving sections are "traditional 6dB/octave designs with an option for a fixed Q parametric response
(BELL)". The later **"Black Knob" 242** EQ was developed in the mid-1980s with George Martin for AIR. It has
"enhanced cut and boost ranges (±18dB instead of ±15dB) together with a different control law for tighter control
of low frequencies" [S2][DOC]. Waves models the 242 [S1]. The dynamics are those of the 611E strip: an RMS detector
in the side chain and a Class-A VCA gain element [S3][S5][DOC].

**Her usage.** Inventory: **727 slots in 399 projects, 3 distinct states** (`tmp/r113/inventory_all.txt`). The
raw state dump (`tmp/r113/wv/states.txt`, 2165 slots incl. backups) shows six variants of one preset [MEAS]:

| Variant | Slots | Difference from her main state |
|---|---|---|
| SSLChannel_0 / _1 / _3 | 1790 / 248 / 52 | main state (below) |
| SSLChannel_2 | 60 | release 0.36 s instead of 0.35 |
| SSLChannel_4 | 14 (e.g. *luisa sonsa*) | **ratio 1** (compressor off, so no make-up) and release 0.40 |
| SSLChannel_5 | 1 | HP 71 Hz |

Her main state, read back through the official trial
([round 113 §3c](../analise/REA_rodada113.md), `tmp/r113/wv/ssl_map.txt`) [MEAS]:
CompThresh **−1.3 dB**, CompRatio **1.8**, CompFast **On**, CompRelease **0.35** (shown "ms", really seconds,
§3); expander inactive (Range 0, Thresh −30, Exp mode, release 0.20); EQ in; dynamics in, pre-EQ (DynaminCHOut
Off); EQ not in the side chain; **FilterSplit On**; **HP 116 Hz**; LP Out; **LF −6.0 dB, 60 Hz, shelf**; LMF 0 dB
(0.50 kHz, Q 2.5); **HMF +3.0 dB, 3.50 kHz, Q 1.5**; **HF +4.8 dB, 10.16 kHz, shelf**; output Gain 0.0 dB;
InputLevel 0.0 dB; Phase +; **Analog Off**.

## 2. Signal flow

```
in -> InputLevel (±18 dB) -> Phase (Ø) ->
   [Split ON : HPF 18 dB/oct -> LPF 12 dB/oct] ->
   DYNAMICS (default pre-EQ; "CH OUT" moves it after the EQ)
      side chain: audio  (or filters+EQ when "Dyn S-C" / EQDynamic is on) -> RMS level [S3]
      expander/gate gain + compressor gain -> one gain element (VCA) [S1]
      automatic make-up gain from Threshold and Ratio [S1][S3]
   -> [Split OFF: HPF -> LPF here, "same path as the entire EQ section"] -> EQ: LF, LMF, HMF, HF
   -> output Gain fader -> out            (Analog On adds the "analogue emulation" [S1]; off in her state)
```
- Order and switches per the manual: "Dynamics can be switched to pre-equalizer (default) or post-equalizer (CH
  OUT)"; "Normally, low pass and high pass filters follow the same path as the entire EQ section. However, when
  Split is selected, the low pass and high pass filters are placed before the dynamics processors"; "Dyn S-C …
  switches the filters and eq into the dynamics sidechain" [S1][DOC].
- **Her path:** HPF 116 → compressor → LF/HMF/HF EQ → out [S1] + her state [MEAS]. With linear test levels
  (−60 dBFS impulse), Split on and off give identical responses (`h0` vs `h3`, §4.3) [MEAS].
- Both Bypass switches "do[…] not result in a flat signal, but rather one which mimics the flat response of the SSL
  channel strip hardware" [S1][DOC]. With Analog Off, our renders show a flat path: away from each
  tested band the single-band renders `ssl1/o*.wav` stay within ±0.1 dB from 30 Hz to 20 kHz [MEAS]. Whether Analog On adds noise, hum or
  harmonics is **not measured** (§8).
- Stereo: one gain element for both channels is implied by "the same gain change circuitry is used" [S1]. Stereo
  linking is not documented (not verified).

## 3. Controls

State mapping: RealWorld list index (57 values), from probing each control alone (`tmp/r113/wv/ssl_probe.txt`) and
the 401-point text tables of the VST parameters (`C:\Users\kauan\lynx-test\r113\wv\ssl1\*.tsv`: normalised value
→ displayed text) [MEAS]. Ranges are from the manual [S1] unless marked.

| Control | RW idx | Range (manual) | Displayed by the trial (norm → text) [MEAS] | Default | Her value | Audible effect |
|---|---|---|---|---|---|---|
| Comp Threshold | 0 | +10 … −20 dB(u) | **linear: thr = 10 − 30·x** (0.25 → 2.5, 0.377 → −1.3, 0.75 → −12.5) | 0 | −1.3 | onset of compression; also sets make-up gain |
| Comp Ratio | 1 | 1 … ∞ | 0 → 1.0, 0.16 → 1.8, 0.25 → 2.3, 0.5 → 5.0, 0.75 → 16, 0.997 → 99, 1 → +Inf | 1 | 1.8 | slope; 1 = compressor off |
| Comp F.ATK | 2 | fast 1 ms / off = "auto-sensing and program dependent" | RW 1 = On, 2 = Off (0 also seen as default) | 0 | 1 (On) | attack |
| Comp Release | 3 | 0.1 … 4 s | 0 → 0.10, 0.25 → 0.17, **0.463 → 0.35**, 0.5 → 0.40, 0.75 → 1.67, 1 → 4.00 (labelled "ms" but the manual's unit is s) | 0.4 | 0.35 s | recovery |
| Exp Threshold | 5 | −30 … +10 dB | 0.0005 → −30.0, 0.25 → −22.8, 0.75 → +2.2 (non-linear taper) | −30 | −30 | — |
| Exp Range | 6 | 0 … 40 dB | 0 → 0, 0.25 → 6.7, 0.75 → 33.3 | 0 | **0 (inactive)** | depth |
| Exp/Gate | 7 | Exp (default) / Gate | 0 Exp, 1 Gate | 0 | Exp | mode |
| Exp F.ATK | 8 | 1 ms fast / auto | 0 Off, 1 On | 0 | Off | — |
| Exp Release | 9 | 0.1 … 4 s | 0.35 → 0.20 | 0.2 | 0.20 | — |
| Dyn Bypass | 11 | — | 0 / 1 | 0 | 0 (dynamics in) | — |
| Dyn → Ch Out | 12 | dynamics post-EQ | 0 / 1 | 0 | 0 (pre-EQ) | order |
| LF Type (Bell) | 13 | shelf / bell ("Q of the LF and HF bells are calculated based on frequency settings") | 0 shelf, 1 bell | 0 | shelf | — |
| LF Gain | 14 | ±16.5 dB shelf, ±18 bell | display ±15.0 dB linear | 0 | −6.0 | see §4.3 |
| LF Frq | 15 | 30 – 450 Hz | 0 → 30, 0.23 → 60, 0.25 → 67, 0.5 → 200, 0.75 → 343, 1 → 450 | 60 | 60 Hz | — |
| LMF Gain / Frq / Q | 16 / 17 / 18 | ±15…±18 dB (gain range grows with Q); 200 Hz – 2.5 kHz; Q 0.1 – 3.5 | freq 0 → 0.20, 0.25 → 0.47, 0.5 → 1.00, 0.75 → 1.83, 1 → 2.50 kHz; Q 0 → 3.50, 0.2 → 2.50, 0.25 → 2.25, 0.5 → 1.50, 0.75 → 0.88, 1 → 0.10 | 0 / 0.5 / 2.5 | 0 dB / 0.5 kHz / 2.5 | — |
| HMF Gain / Frq / Q | 19 / 20 / 21 | as LMF; 600 Hz – 7 kHz | freq 0 → 0.60, 0.25 → 1.03, 0.5 → 3.00, 0.55 → 3.50, 0.75 → 5.50, 1 → 7.00 kHz; Q as LMF | 0 / 3.5 / 1.5 | **+3.0 / 3.5 kHz / 1.5** | presence |
| HF Type (Bell) | 22 | shelf / bell | 0 shelf, 1 bell | 0 | shelf | — |
| HF Gain | 23 | ±16.5 shelf / ±18 bell | display ±15.0 dB linear | 0 | **+4.8** | air |
| HF Frq | 24 | 1.5 – 16 kHz | 0 → 1.50, 0.25 → 3.00, 0.5 → 8.00, 0.656 → 10.16, 0.75 → 12.67, 1 → 16.00 kHz | 8 | **10.16 kHz** | — |
| EQ Bypass | 25 | — | 0 / 1 | 0 | 0 (EQ in) | — |
| EQ → Dyn S-C | 26 | filters+EQ into the side chain | 0 / 1 | 0 | 0 | — |
| HP Frq | 27 | 18 dB/oct, 16 – 350 Hz (−3 dB point); fully left = out | 0 → Out, 0.0025 → 16, 0.25 → 37, 0.4875 → 116, 0.5 → 120, 0.75 → 267, 1 → 350 Hz | 0 (out) | **116 Hz** | low cut |
| LP Frq | 28 | 12 dB/oct, 3 – 22 kHz (−3 dB point) | 0.25 → 10.7 kHz, 0.75 → 3.7 kHz; RW 30 = Out | 30 (Out) | Out | — |
| Filter Split | 29 | filters before the dynamics | RW 1 = On, 2 = Off (0 = default; meaning of 0 vs 2 not confirmed) | 0 | **1 (On)** | order |
| Output Gain | 30 | (fader) | linear −24 … +12 dB (0.25 → −15, 0.667 → 0, 0.75 → +3) | 0 | 0 dB | level |
| Analog | 31 | analogue emulation on/off | 0 Off, 1 On | 0 | **Off** | colour/noise |
| VU In/Out | 34 | meter source | 1 Output, 2 Input | 1 | Output | none |
| Phase | 39 | Ø | 0 +, 1 − | 0 | + | polarity |
| Input Trim | 40 | ±18 dB | 0.25 → −9, 0.75 → +9 (linear ±18) | 0 | 0 dB | level into everything |
| (unlabelled) | 37, 38, 47–54, 55, 56 | — | 37: 7; 38 and 47–54: −18 (probably meter/calibration values); 55/56: 0 | — | defaults | not confirmed |

Display ranges differ from the manual: the trial shows **±15 dB** at the ends of every gain knob, while the manual
gives ±16.5 dB (shelf) / ±18 dB (bell) and "±18 dB when Q is set at 3.5, to ±15 dB when Q is set at 0.1" for the
mids [S1][MEAS]. The real filter gains differ from the legend anyway (§4.3). Use the **legend value saved in the
state** as the input to the measured laws, never the manual range.

## 4. Known curves & behaviour

### 4.1 Documented (vendor)

- **Filters:** HPF 18 dB/oct, 16–350 Hz; LPF 12 dB/oct, 3–22 kHz; both "(−3 dB point)" [S1][DOC]. SSL's Native
  Channel Strip documents the same slopes with HPF 20–500 Hz [S6][DOC].
- **Shelves (E EQ, SSL):** first-order "6dB/octave designs" with a "fixed Q" bell option (02 card) [S2][DOC].
  Waves: "The Q of the LF and HF bells are calculated based on frequency settings and are not user-definable" [S1].
- **Mids:** "Q can be adjusted in the LMF and HMF ranges", Q 0.1–3.5. The gain range grows with Q
  (±15 → ±18 dB) [S1][DOC]. The 02 card is log-symmetric about the centre [S2]. SSL's Native strip describes
  the E curve as "slightly more pointed" than G's "more rounded shape at low gains", with "at full boost or cut,
  both curves are identical" [S6][DOC]. SSL's own bell-shape taxonomy distinguishes "Classic Symmetrical
  … Almost constant Q" from "Proportional" bells (wider below ±6 dB, narrower above) [S6][DOC].
- **Compressor:** "soft-knee compressor/limiter"; ratio 1…∞; threshold +10…−20 dB; release 0.1–4 s; F.ATK =
  1 ms, otherwise "auto-sensing and program dependent … responding to the audio material's wavefront"; "Automatic
  gain make-up, calculated from the Ratio and Threshold settings"; "threshold circuitry uses variable hysteresis"
  [S1][DOC]. SSL's hardware re-issue of the 611E dynamics: "true RMS converter … in the side chain"; normally an
  "over-easy" knee (a button gives a hard knee); "FAST ATT: Provides a faster attack time (3mS for 20dB gain
  reduction). When off the attack time is slower … (30mS for 20dB gain reduction)"; release "logarithmic" (a LIN
  REL option "also raises the threshold by 6dB"); "THRESHold and RATIO controls also provide automatic make-up
  gain … maintaining a steady output level regardless of the amount of compression" [S3][DOC]. SSL Native Channel
  Strip: auto attack "3ms to 30ms (automatic)", fast 3 ms; RMS mode = soft knee, Peak mode = hard knee [S6][DOC].
  The **Waves** fast attack is documented as 1 ms [S1]. Not measured.
- **Expander/gate:** threshold −30…+10 dB, range 0–40 dB, release 0.1–4 s, F.ATK 1 ms (Waves) [S1]. Hardware: gate
  "20:1" or expander "2:1"; "a controlled linear attack time of 1.5ms per 40dB … fast attack time (100µs per
  40dB)"; "Variable hysteresis … increases as the threshold is lowered" [S3][DOC]. (Off in her state.)

### 4.2 Measured dynamics (her state, round 113)

1 kHz sine stairs, EQ bypassed, gain (dB) vs input peak level ([round 113 §3c](../analise/REA_rodada113.md)) [MEAS]:

    input dBFS peak   -60   -24   -18   -12    -6     0
    gain dB           +0.9  +0.9  +0.4  -0.7  -2.2  -4.0

- **+0.9 dB below the threshold** is the automatic make-up at −1.3 dB / 1.8:1. Impulse at −60 dBFS: her state vs
  the same state at ratio 1:1 or with dynamics bypassed is exactly +0.9 dB at every frequency (`h0` vs `h1`/`h2`)
  [MEAS].
- Threshold in dBFS: −1.3 dBu − 18 = **−19.3 dBFS** (sine RMS, if the detector is RMS as in the hardware [S3]) [model
  from S1 alignment]. Gain reduction starts between −21 and −15 dBFS RMS on the stairs (consistent).
- **Static slope above the knee is ~1.4:1, not 1.8:1.** From −12 to 0 dBFS the gain falls 3.3 dB over 12 dB of input
  (slope 0.275, so an effective ratio of 1.38). A hard 1.8:1 at −19.3 dBFS RMS would give 7.2 dB of reduction at
  0 dBFS, against the ~4.9 dB measured [model from MEAS]. Either the ratio legend is not the static slope, or the
  over-easy knee is very wide. Round 113's fit for Ridge (peak detector, 6 dB knee) used threshold − 15.7 dB and
  ratio 1 + 0.375 (R − 1) = 1.3 [MEAS].
- Attack and release, program dependence, and the make-up law at other settings are **not measured** (§8).

### 4.3 Measured EQ: the settled her-state response and the per-band laws

**Method (re-analysis, 2026-10-21).** Round 113 rendered (a) her full state and five variants with `loadcomponent`
→ `reset` → `settle 0.3` → a 1 mV impulse (−60 dBFS), files `ssl1/h0…h5.wav`, and (b) twenty single-band
settings from `defaults`, impulse rendered **without settle** (`ssl1/o0…o19.wav`, `py/sslbands.py`). Transfer
functions were taken as FFT(out)/FFT(in) (32768 points, 48 kHz). Bands of her state were isolated as ratios of the
settled renders (her ÷ her-with-that-band-at-0 dB). Each band was then fitted by grid search with RBJ biquads
(bilinear, 48 kHz) [S7] and first-order shelves [MEAS].

**Her full state, settled (ratio 1:1 so the +0.9 dB make-up is out), dB** [MEAS]:

    Hz      40     60    100    150    200    300    500    1k     2k    3.5k   4.5k    6k     8k    10k   12.5k   16k    20k
    dB   -28.6  -17.7   -6.4   -2.9   -1.8   -0.9   -0.3   +0.3   +1.5   +4.0   +5.0   +5.1   +4.8   +4.7   +4.6   +4.0   +2.3

**Band fits that reproduce it** (each band isolated from settled renders, except the HP; see the note on (b)):

| Her band (legend) | Best textbook section (48 kHz, BLT) | Fit RMS | Note |
|---|---|---|---|
| HF +4.8 dB, 10.16 kHz, shelf | **RBJ peaking, f0 = 12.42 kHz, Q = 0.274, +4.05 dB** | 0.03 dB | a first-order or RBJ high shelf fits worse (0.33–0.36 dB): the boost falls to +2.3 dB at 20 kHz and toward 0 dB at Nyquist |
| HMF +3 dB, 3.5 kHz, Q 1.5 | **RBJ peaking, f0 = 4.52 kHz, Q = 0.835, +2.70 dB** | 0.04 dB | centre 1.29× the legend, Q 0.56× the legend, gain 0.9× |
| LF −6 dB, 60 Hz, shelf | **first-order low shelf, mid-gain frequency 139 Hz, −5.7 dB** | 0.14 dB | derived as her ÷ (HP render × fitted HMF × fitted HF) |
| HP 116 Hz | 3rd-order high-pass: 2nd-order section fc 92.7 Hz, Q 1.24 × 1st-order fc 131 Hz | 0.49 dB | a plain Butterworth at fc 100.9 Hz fits at 0.68 dB; the slope below 40 Hz is ~22 dB/oct |
| **Whole EQ** (the four above in series) | — | **0.18 dB RMS, 30 Hz–20 kHz (max 1.1 dB); 0.08 dB above 150 Hz (max 0.18 dB)** | vs the settled render `h1` |

**Single-band renders (b), not settled** [MEAS]: best fits at 48 kHz.

| Setting (legend) | Best fit | RMS | Shape facts |
|---|---|---|---|
| HF +4.8 @ 3 k | peak f0 7.43 k, Q 0.15, +5.0 | 0.09 | peak 5.06 dB at 7.35 kHz |
| HF +4.8 @ 6 k | peak f0 8.97 k, Q 0.22, +4.5 | 0.05 | |
| HF +4.8 @ 10.16 k | peak f0 12.48 k, Q 0.28, +4.0 | 0.05 | same as the settled fit (HF is not affected by settling) |
| HF +4.8 @ 16 k | peak f0 14.37 k, Q 0.35, +3.75 | 0.04 | peak only +3.6 dB at 14.4 kHz |
| HF +12 @ 10.16 k | peak f0 15.07 k, Q 0.16, +12.25 | 0.09 | +11.2 dB at 20 kHz |
| HF −6 @ 10.16 k | 1st-order high shelf, mid 5.56 k, −4.5 (peak fit 0.12) | 0.09 | cut keeps falling to −5.1 dB at 22 kHz |
| HMF +3 @ 3.5 k, Q 1.5 | peak 4.86 k, Q 0.78, +2.5 | 0.08 | settled: 4.52 k, Q 0.835, +2.7 |
| HMF +9 @ 3.5 k, Q 1.5 | peak 4.63 k, Q 0.78, +8.5 | 0.17 | Q does not change with gain, so not proportional-Q |
| HMF +6 @ 1 k, Q 1.5 | peak 1.42 k, Q 1.00, +5.5 | 0.04 | |
| HMF +6 @ 3.5 k, Q 0.5 | peak 4.63 k, Q 0.35, +4.75 | 0.18 | |
| HMF +6 @ 3.5 k, Q 2.5 | peak 4.42 k, Q 1.13, +5.0 | 0.09 | |
| LMF −6 @ 1.5 k, Q 1.5 | peak 1.12 k, Q 1.00, −3.75 | 0.04 | centre **below** the legend (0.75×) |
| LMF +6 @ 500, Q 2.5 | peak 1.64 k, Q 2.65, +3.0 | 0.04 | **anomalous** (3.3× the legend); re-measure (§8) |
| LF −6 @ 60 | 1st-order shelf mid 372 Hz, −2.0 | 0.06 | **contradicted by the settled her state** (−5.7 dB, 139 Hz): not settled |
| LF +6 @ 60 | 1st-order shelf mid 129 Hz, +5.0 | 0.04 | probably affected the same way |
| LF −6 @ 200 | 1st-order shelf mid 355 Hz, −5.75 | 0.04 | |
| LF +9 @ 400 | 1st-order shelf mid 810 Hz, +9.25 | 0.08 | |
| HP 50 / 116 / 250 Hz | 3rd-order, −3 dB at 27.8 / 96.7 / 187.5 Hz | 0.06 / 0.49 / 0.28 | the legend is not the −3 dB point; non-monotonic ratio (0.56 / 0.83 / 0.75) |

What this says about the model [model, from the fits above]:
- **HF section = a low-Q bell with unity gain at Nyquist**, i.e. what the bilinear transform of an analog shelf
  or bell gives when the digital design pins H(z = −1) = 1. That is the "cramping" behaviour [S6][S8]. Its legend
  frequency maps roughly to f0 ≈ 7.4 k → 14.4 kHz for legends 3 → 16 kHz (strongly compressed), with Q 0.15–0.35.
  Because a Nyquist-pinned design depends on the sample rate, **the curve at 44.1 kHz will differ** (not measured).
- **LF section = first-order shelf**, as documented for the E EQ [S2]. Cutting at 200 Hz gives a mid-gain point
  1.8× the legend (−5.75 dB); her settled −6 dB @ 60 Hz cut gives 139 Hz (2.3×), −5.7 dB.
- **Mid bells are constant-Q-like**, not proportional: the half-gain Q is the same at +3 and +9 dB. The effective Q
  is roughly 0.35 / 0.8–1.0 / 1.13 for legends 0.5 / 1.5 / 2.5, and the centre sits 1.26–1.43× above the HMF
  legend.
- Round 113's laws for Ridge are consistent with this for HF and HMF: "HF shelf at 0.36× the frequency and 0.8×
  the gain; HMF at 1.37×, gain 0.8×, Q 0.5 + 0.27 (Q − 0.5)". Its LF-cut law ("0.32× the gain from ~400 Hz") comes
  from the unsettled LF render and is wrong by up to ~2.5 dB at 60–100 Hz.

### 4.4 Other behaviour

- **Parameter smoothing:** the difference between the settled and unsettled renders (LF −2.0 dB vs −5.7 dB; HMF
  +2.5 vs +2.7 dB) shows the trial smooths parameter changes over tens of milliseconds, or longer for the
  LF shelf [MEAS]. LYNX only applies a static state, so it needs no smoothing model. Every future measurement needs
  `settle` (≥ 0.5 s) before the stimulus.
- **Saturation / noise:** Analog Off in her state. With Analog Off the renders show no noise floor above the
  numerical one at −60 dBFS [MEAS]. Analog On is not measured.
- **Sample rate:** measured only at 48 kHz.

## 5. Published science

No peer-reviewed paper models the SSL 4000 E channel specifically (searched 2026-10-21; vendor marketing only)
[model]. The relevant generic results are below.

**5.1 Analog prototypes and the bilinear transform.** RBJ cookbook [S7][PAPER]. With ω0 = 2π f0/fs,
α = sin ω0/(2Q), A = 10^(G/40):

    peaking:  b = [1 + αA, −2cos ω0, 1 − αA],   a = [1 + α/A, −2cos ω0, 1 − α/A]
    low shelf (S = shelf slope): α = sin ω0/2 · sqrt((A + 1/A)(1/S − 1) + 2)
      b0 = A[(A+1) − (A−1)cos ω0 + 2√A α], b1 = 2A[(A−1) − (A+1)cos ω0], b2 = A[(A+1) − (A−1)cos ω0 − 2√A α]
      a0 = (A+1) + (A−1)cos ω0 + 2√A α,    a1 = −2[(A−1) + (A+1)cos ω0],   a2 = (A+1) + (A−1)cos ω0 − 2√A α

The RBJ peaking filter has unity gain at DC and at Nyquist by construction. That is the shape measured for the
Waves HF band (§4.3).

**First-order shelf (6 dB/oct, the E EQ's shelving law [S2]).** Analog, with gain G (linear) at DC, unity at HF, and
the mid-gain (√G) frequency ωm:

    H_LS(s) = (s + ωm·√G) / (s + ωm/√G)          H_HS(s) = (√G·s + ωm) / (s/√G + ωm)

Bilinear with prewarp at fm, K = tan(π fm/fs), r = √G:

    low shelf:   b0 = 1 + K r,  b1 = K r − 1,  a0 = 1 + K/r,  a1 = K/r − 1   (normalise by a0)

(Our grid fits of RBJ shelves with S = 0.5 landed on exactly the same f and gain as this first-order form.)

**3rd-order high-pass (18 dB/oct).** A first-order section (fc1) cascaded with a second-order one (fc2, Q). The
Butterworth case is fc1 = fc2 = fc, Q = 1. Measured: fc2 = 0.80 × legend, Q 1.24, fc1 = 1.13 × legend at
116 Hz (§4.3).

**5.2 Avoiding cramping near Nyquist (if LYNX must run at other sample rates).** Vicanek's matched second-order
filters [S8][PAPER] take the poles from impulse invariance (z = e^(s·T)). For a prototype denominator
s² + 2qω0 s + ω0² (ω0 in rad/sample):

    a1 = −2 e^(−q ω0) cos(√(1−q²) ω0)   (q ≤ 1),   −2 e^(−q ω0) cosh(√(q²−1) ω0)   (q > 1)
    a2 = e^(−2 q ω0)

Then they solve for the numerator from the magnitude constraints. With φ0 = 1 − sin²(ω/2), φ1 = sin²(ω/2),
φ2 = 4φ0φ1, write
A0 = (1+a1+a2)², A1 = (1−a1+a2)², A2 = −4a2, so |H|² = (B0φ0 + B1φ1 + B2φ2)/(A0φ0 + A1φ1 + A2φ2).
For the **peaking EQ** H(s) = (s² + s ω0 √G/Q + ω0²)/(s² + s ω0/(√G Q) + ω0²) (so q = 1/(2√G Q)), require B0 = A0
(unity at DC), |H(ω0)|² = G² and a turning point at ω0:

    R1 = (A0φ0 + A1φ1 + A2φ2)·G²,   R2 = (−A0 + A1 + 4(φ0 − φ1)A2)·G²   (φ's at ω0)
    B2 = (R1 − R2 φ1 − B0) / (4 φ1²),    B1 = R2 + B0 + 4(φ1 − φ0)B2
    W = (√B0 + √B1)/2,  b0 = (W + √(W² + B2))/2,  b1 = (√B0 − √B1)/2,  b2 = −B2/(4 b0)

**Do not "de-cramp" the HF band to match the Waves trial at 48 kHz.** The trial is itself Nyquist-pinned (§4.3), so
a plain RBJ peaking filter reproduces it best there [model from MEAS]. A matched design is only the right choice if
measurements at 44.1/96 kHz show that the Waves curve keeps its analog shape (§8).
Orfanidis' prescribed-Nyquist-gain peaking EQ is discussed in [S8] as the alternative [PAPER]. The design
landscape is reviewed in Välimäki & Reiss [S9][PAPER].

**5.3 Compressor.** Feed-forward, log-domain gain computer with a soft knee (Giannoulis, Massberg, Reiss)
[S10][PAPER]. With x = detector level (dB), threshold T, ratio R, knee width W:

    y = x                                    2(x − T) < −W
    y = x + (1/R − 1)(x − T + W/2)² / (2W)    2|x − T| ≤ W
    y = T + (x − T)/R                         2(x − T) > W
    gain g = y − x + M      (M = automatic make-up)

Detector (hardware: true RMS [S3]): z[n] = β z[n−1] + (1 − β) x²[n], level = 10 log10 z, with β set by an RMS
window of a few ms [model]. Smoothing in the gain domain uses the "branching" attack/release one-pole of [S10]:
gs[n] = αA gs[n−1] + (1 − αA) g[n] if g[n] < gs[n−1], else the αR version, with α = e^(−1/(τ fs)).
For the SSL fast attack the vendors give τ such that 20 dB of reduction takes ~3 ms (SSL) or "1 ms" (Waves); the
auto attack is 3–30 ms, program dependent [S1][S3][S6]. Release 0.1–4 s, logarithmic, i.e. exponential in dB
[S3][model].

**Auto make-up hypothesis** [model]: M = (1 − 1/R)·(T0 − T), the gain that keeps a reference level T0 at unity.
Her +0.9 dB at T = −1.3, R = 1.8 gives T0 ≈ +0.7 dB(u), which is only one point. The law must be measured (§8).

## 6. What LYNX does today

- `[waves-ssl-channel]` in `src/core/PluginSubstitutes.cpp` maps SSLChannel to **Ridge Strip** with
  `translate = waves-ssl-channel` (`translateWavesSslChannel`). Ridge is in `src/core/MixSuite.cpp` (namespace
  `ridge`) [MEAS].
- **Ridge Strip:** HPF = first-order + RBJ high-pass with Q 1 (3rd-order Butterworth), 16–350 Hz; LPF RBJ Q 0.707;
  LF/HF **RBJ second-order shelves** (S = 1 "brown", 1.6 "black") or bells with Q 0.9; LMF/HMF RBJ peaking (black =
  proportional Q); compressor: peak detector with instant attack and a 30 ms release level envelope, 6 dB soft knee
  [S10], attack 10 ms or 1 ms (fast), release 0.1–4 s log, threshold −40…0 dBFS, ratio 1 + 19v² (∞ at the top);
  gate/expander (expander 1:2, gate hysteresis 4 dB); order switch; no auto make-up [MEAS].
- **Translation (round 113):** curve = brown; dynamics before EQ; HP via the table 16→9, 50→27, 116→99, 250→194,
  350→272 Hz. LF cut: gain × (0.32 @ 60 → 0.88 @ 200 Hz), frequency × 6.7. LF boost: × 0.75…1.08, frequency × 2.4.
  LMF gain × 0.65. HMF gain × 0.8, frequency × 1.37, Q → 0.5 + 0.27 (Q − 0.5). HF gain × 0.8, frequency × 0.36.
  Compressor threshold − 15.7 dB, ratio 1 + 0.375 (R − 1), release, fast; output + 0.9 dB when ratio > 1.01 [MEAS].
- **Residual error:** her whole state 0.69 dB RMS on the stairs; pink at −40 dBFS within 1 dB RMS from 40 Hz to
  18 kHz (0.3 dB above 150 Hz); take level −2.1 vs −1.6 dB ([round 113 §3c](../analise/REA_rodada113.md)). In her
  chain against the official trials, the "+SSL" stage is +0.25…+0.44 dB in level with an octave spread of
  0.47–0.90 dB, 63 Hz–16 kHz ([round 119 §5](../analise/REA_rodada119.md)) [MEAS].
- **Why Ridge cannot get closer by retuning** [model from §4.3]: Ridge's HF is a second-order shelf that stays at
  full gain up to Nyquist. Waves' HF is a low-Q bell that falls by ~2 dB at 20 kHz. Ridge's LF shelf is
  second-order (12 dB/oct transition), Waves' is first-order. The LF-cut law in the translator was fitted to an
  unsettled render (§4.3). These are shape differences, not parameter offsets.

## 7. Implementation guidance for LYNX

Goal: her state within **≤ 0.25 dB RMS (30 Hz–20 kHz) on the settled impulse response**, ≤ 0.5 dB on the 1 kHz
stairs, and ≤ 0.3 dB RMS per octave band (63 Hz–16 kHz) on her raw take versus the official trial in series.

1. **Add a dedicated "SSL E" EQ mode** (an `EqDesign` variant inside Ridge, or a small `SslEChannel` effect used
   only as the substitute). Do not change Ridge's existing curves: other projects and presets use them.
   Sections, in this order: HPF → LPF → [dynamics] → LF → LMF → HMF → HF (for her: Split on, Ch Out off).
2. **Her state, exact coefficients at 48 kHz** (BLT; §4.3, composite 0.18 dB RMS):
   - HPF: first-order high-pass fc 131.2 Hz × RBJ high-pass fc 92.7 Hz, Q 1.24.
   - LF: first-order low shelf (§5.1), fm 139 Hz, −5.7 dB.
   - HMF: RBJ peaking f0 4520 Hz, Q 0.835, +2.70 dB.
   - HF: RBJ peaking f0 12417 Hz, Q 0.274, +4.05 dB.
   - LMF at 0 dB: bypass. Output + make-up per item 4.
   At other sample rates, keep these analog parameters and the BLT until item 6's measurement says otherwise.
3. **General laws (from the legend values in the state):** interpolate the measured tables of §4.3 in log-frequency.
   - HF: (legend f, gain) → (f0, Q, g) of an RBJ peak. Points at +4.8 dB: 3k → (7.43k, 0.15, 5.0), 6k → (8.97k,
     0.22, 4.5), 10.16k → (12.42k, 0.274, 4.05), 16k → (14.37k, 0.35, 3.75). Gain dependence at 10.16k: +12 →
     (15.07k, 0.16, 12.25). Cuts: first-order high shelf, mid 0.55 × legend, gain 0.75 × legend (one point).
   - HMF/LMF: centre ≈ 1.3–1.4 × legend (HMF), Q_eff ≈ {0.35, 0.83, 1.13} at Q legend {0.5, 1.5, 2.5}, gain ≈
     0.8–0.95 × legend, constant-Q in gain.
   - LF: first-order shelf, fm ≈ 1.8–2.3 × legend, gain ≈ 0.95 × legend (cuts at 60 and 200 Hz).
   - HP: −3 dB at 0.56 / 0.83 / 0.75 × legend for 50 / 116 / 250 Hz (fit as above).
   Only her settings are covered well. Every other point carries the caveats of §8; leave the band at face value
   (current Ridge law) where no point exists, and log it.
4. **Dynamics:** keep Ridge's compressor but match the measured static curve. Prefer an RMS detector (the hardware's
   [S3]) with a ~5–10 ms window over Ridge's peak envelope. Then refit the threshold offset, the ratio law
   (effective static slope ≈ 1.4 for legend 1.8) and the knee width on the 1 kHz stairs. Add the automatic make-up
   as M(T, R), +0.9 dB at her setting, 0 dB at ratio 1. Fast attack: 1 ms. Release: legend seconds (0.35 s), one
   pole in dB. Gate/expander: Ridge's expander is 1:2 like the hardware's 2:1 [S3]. Range/threshold at face value
   (inactive in her state).
5. **Translator:** rewrite `translateWavesSslChannel` to fill the new mode from RW 13–30 (and 0–3, 5–12, 39, 40)
   with the laws above. Handle the variants: ratio 1 (*luisa sonsa*) means no compression and no make-up; HP 71 Hz
   interpolates the HP table; release 0.36 / 0.40.
6. **Tests** (extend `tests/Substitutes113Tests.inc` or a new `.inc`): (a) the response of the SSL-E mode at her
   state at 48 kHz against the table of §4.3, ≤ 0.25 dB RMS 30 Hz–20 kHz; (b) the static curve at her state against
   the stairs of §4.2 (≤ 0.3 dB per step); (c) ratio-1 variant: flat, 0 dB make-up.
7. **Re-run round 119's chain test** (her take, official trials in series vs substitutes). Pass: "+SSL" stage
   within ±0.2 dB level and ≤ 0.3 dB octave spread from 63 Hz to 16 kHz.

## 8. Open questions to measure (official trial, black-box, always `reset` + `settle ≥ 0.5 s` before the stimulus)

- [ ] **Re-render all single-band impulses with settle** (the 20 settings of `py/sslbands.py`). Decides the LF and
      HP laws (the unsettled LF renders are known wrong) and whether the LMF +6 @ 500 Hz Q 2.5 peak at 1.64 kHz is
      real.
- [ ] **Band grids:** HF legend {1.5, 3, 6, 10.16, 13, 16} kHz × gain {−12, −6, −3, +3, +6, +12}; HMF/LMF centre
      {low, mid, high} × Q {0.1, 0.5, 1, 1.5, 2.5, 3.5} × gain {±3, ±9, ±15}; LF {30, 60, 100, 200, 450} Hz × gain
      {±6, ±12}. Decides the general laws of §7.3 (centre, Q, gain mapping, cut/boost asymmetry).
- [ ] **Bell mode** for LF and HF ("Q … calculated based on frequency"): one sweep each. Decides the bell law.
- [ ] **LP filter** at 3, 6, 10.7, 22 kHz: −3 dB point and Q.
- [ ] **Sample rate:** her state at 44.1 and 96 kHz. Decides whether the HF bell is pinned at Nyquist per rate
      (BLT at host rate) or oversampled / matched. Her projects' rates also matter (check FL project rates).
- [ ] **Static compressor curves:** 1 kHz stairs −60…+0 dBFS for T ∈ {+10, 0, −1.3, −10, −20} × R ∈ {1.5, 1.8, 2, 4,
      10, ∞}. Decides the threshold reference, the ratio law (legend vs slope), the knee width, and M(T, R).
- [ ] **Detector:** a sine and a square of equal RMS (RMS detector: same reduction), and equal peak.
- [ ] **Attack:** a −30 → −6 dBFS 1 kHz step, F.ATK on and off. Decides τ (1 ms vs 3 ms per 20 dB) and the
      program dependence of the auto attack (repeat with a 10 dB and a 25 dB step).
- [ ] **Release:** a −6 → −30 dBFS step for release legends 0.1, 0.35, 1, 4 s. Decides the curve
      (exponential in dB?) and the legend → τ mapping.
- [ ] **Analog On:** silence and a −20 dBFS 1 kHz sine with Analog On. Decides the noise floor / hum and the
      harmonics (only if a corpus state uses it; hers does not).
- [ ] **Stereo link:** a tone in L only, with R silent.
- [ ] **RW 29 / RW 2 encodings** (0 vs 2 = Off?) and the unlabelled RW 37/38/47–56.

## 9. Sources

1. Waves, "SSL E-Channel & G-Channel User Manual" (SSL 4000 Collection) — https://assets.wavescdn.com/pdf/plugins/ssl-e-channel.pdf — accessed 2026-10-21 — [DOC]
2. Solid State Logic, "E Series EQ Module for 500 Series Racks — User Guide" (Brown 02 / Black 242) — https://solidstatelogic.com/assets/uploads/downloads/SSL_500_Series_E_EQ_Module_User_Guide.pdf — accessed 2026-10-21 — [DOC]
3. Solid State Logic, "E Series Dynamics Module for 500 Series Racks — User Guide" (611E dynamics) — https://solidstatelogic.com/assets/uploads/downloads/500_Series_E_DYN_Module_User_guide_1.pdf — accessed 2026-10-21 — [DOC]
4. Solid State Logic, "E-Series EQ Module" product page — https://www.solidstatelogic.com/products/e-series-eq-module — accessed 2026-10-21 — [DOC]
5. Solid State Logic, "E-Series Dynamics Module" product page — https://www.solidstatelogic.com/products/e-series-dynamics-module — accessed 2026-10-21 — [DOC]
6. Solid State Logic, "SSL Native v6.5 User Guide" (Channel Strip E/G EQ curves and dynamics values; X-EQ 2 bell-shape definitions and anti-cramping) — https://www.solidstatelogic.com/assets/uploads/downloads/plug-ins/SSL%20Native%20v6.5%20-%20User%20Guide.pdf — accessed 2026-10-21 — [DOC]
7. R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (W3C Audio EQ Cookbook) — https://www.w3.org/TR/audio-eq-cookbook/ — accessed 2026-10-21 — [PAPER]
8. M. Vicanek, "Matched Second Order Digital Filters", 2016 — https://www.vicanek.de/articles/BiquadFits.pdf — accessed 2026-10-21 — [PAPER]
9. V. Välimäki, J. D. Reiss, "All About Audio Equalization: Solutions and Frontiers", Applied Sciences 6(5):129, 2016 — https://acris.aalto.fi/ws/portalfiles/portal/9936551/applsci_06_00129.pdf — accessed 2026-10-21 — [PAPER]
10. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6) pp. 399–408, 2012 — https://aes2.org/publications/elibrary-page/?id=16354 — accessed 2026-10-21 — [PAPER]
11. ModularGrid, "E-Series EQ Module" (Brown 02 before mid-1985, Black 242 later) — https://modulargrid.net/a/modules/view/4737 — accessed 2026-10-21 — [DOC, third-party]
12. LYNX repo: [REA round 113 §3c, §4b](../analise/REA_rodada113.md), [REA round 119 §5](../analise/REA_rodada119.md), [PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md), `src/core/MixSuite.cpp` (Ridge), `src/core/PluginSubstitutes.cpp` (`translateWavesSslChannel`) — [MEAS]
13. Raw data: `~/.claude/jobs/f8789b49/tmp/r113/wv/ssl_map.txt`, `ssl_probe.txt`, `states.txt`, `tmp/r113/inventory_all.txt`, `tmp/r113/py/sslbands.py`, `sslher.py`, `sslcomp.py`; renders and tables `C:\Users\kauan\lynx-test\r113\wv\ssl1\` (`o0–o19.wav` single bands, unsettled; `h0–h5.wav` her state and variants, settled; `*.tsv` control texts; `b.log`) — [MEAS]
