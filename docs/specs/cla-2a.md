# Waves CLA-2A — spec dossier (LYNX substitute: Glow Leveler)

Round 125 (spec125). Clean-room dossier: built from the public Universal Audio LA-2A user's guide (with its
schematic excerpts), public papers on opto-compressor / photoresistor modelling and on LA-2A measurements, Waves'
public product information, and LYNX's own black-box measurements of the official Waves trial
([REA round 113 §3c](../analise/REA_rodada113.md), [REA round 119 §1, §5](../analise/REA_rodada119.md)). Section 4
also re-reads the renders round 113 already made (stairs, pink noise) for timing, tone and harmonics that round 113
did not tabulate; nothing new was run through the plugin. No Waves binary was opened, decompiled or instrumented;
licensing was not touched. Tags: [DOC] vendor document · [PAPER] academic · [SCHEMATIC] hardware schematic /
service document · [MEAS] measurement · [model] our inference (with what it rests on). All web sources accessed
2026-10-21.

**Summary.** The CLA-2A is Waves' model of Chris Lord-Alge's own Teletronix LA-2A [S1][DOC]. The LA-2A is a
feedback leveler. An electroluminescent (EL) panel driven by the side chain lights a CdS photoresistor, and that
photoresistor is the shunt leg of a voltage divider. The attack is about 10 ms. The release has two stages (about
60 ms to 50 %, then 0.5–5 s or 1–15 s depending on how much and how long it compressed). The tube stages add
distortion [S4][S6][S8]. Her CLA-2A is one preset almost everywhere: **Gain 48.96, Peak Reduction 62.6, Compress,
Hi Freq 50, Analog Off, Mix 100 %, Trim 0, Auto Makeup Off**
([round 113 §3c](../analise/REA_rodada113.md)). Measured at that state [MEAS]:
- +16.0 dB below threshold; compression starts about −30 dBFS; 21.8 dB of gain reduction at 0 dBFS (1 kHz).
- The static curve fits a **feedback power law** `1/g − 1 = (y/y0)^γ` with **γ = 4.54, y0 = −26.7 dBFS** (residual
  ≤ 0.25 dB). That is the same functional form as Glow Leveler's cell. Glow uses γ = 2, which gives 3:1 and the
  softer knee round 113 saw. The fit gives about 4.2:1 locally and 5.5:1 asymptotically.
- The attack speeds up with the amount of light: the time constant goes from 66 ms just above threshold to 4 ms
  at 20 dB of gain reduction.
- Under heavy gain reduction the audio path gets a **high-frequency tilt**: +1 dB at 6 kHz and +6 dB at 20 kHz
  relative to 1 kHz at about 15 dB of gain reduction. It fits a divider zero at about 11.6 kHz.
- **Mostly 2nd-harmonic distortion**: about −83 dBc at −60 dBFS input, rising 1 dB per dB of input to about
  −51 dBc under compression.

Glow Leveler today has none of the tone. It has a 3:1 law and a 30 %-weighted 400 ms slow attack component that the
CLA-2A does not show.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves — CLA-2A Compressor / Limiter (part of the "CLA Classic Compressors") | [S1][DOC] |
| What it models | "Chris Lord-Alge's favorite hardware unit" — a Teletronix LA-2A kept in his studio; Chris supervised the modelling and made the presets | [S1][DOC] (retailer copy of Waves' text) |
| Features (vendor) | Compress / Limit; "HF Freq 0 to 100"; "Sidechain frequency filter"; "Analog Off, 50Hz, 60Hz"; zero latency; mono and stereo components | [S1][DOC] |
| Version she uses | WaveShell1-VST3 16.0 / 15.5 in her projects; measured on WaveShell1-VST3 **17.1** "CLA-2A Stereo" | [round 119 §1, §5](../analise/REA_rodada119.md) [MEAS] |
| Format seen in her projects | VST3, class ID `4c545356324163536c612d3261207374`, `<PluginName>` CLA-2A | `src/core/PluginSubstitutes.cpp` `[waves-cla-2a]` [DATA] |
| Latency | 0 samples (stairs cross-correlation, 48 kHz) | round-113 renders [MEAS]; "zero-latency" [S1][DOC] |

**Hardware lineage.** The Teletronix LA-2A was designed by James F. Lawrence II (Teletronix, Pasadena, founded
1958) and released in 1962. Babcock bought it in 1965. Bill Putnam's Studio Electronics (later UREI) took it over
in 1967, and production ended around 1969. Reissues followed from UREI (1979), Harman/JBL (1992) and Universal Audio
(1999) [S5][S4][DOC]. The UA guide says there were "three different variations" in that period [S4][DOC]. UA's
plug-in notes describe the generations as Silver (UREI, faster), Gray (mid-1960s Pasadena, medium) and LA-2
(slowest, "mellow") [S6][DOC]. Which generation CLA's unit is has not been published, as far as we found.

**Her usage.** 733 slots in 405 projects, 2 distinct states in the main corpus
(`~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`). Counting autosaves and backups, there are 12 distinct
lists (`tmp/r113/wv/states.txt`):
- the dominant one (1738 slots) has gain 48.96 and peak reduction 62.6;
- the others have gain 48.56–50.16 and peak reduction 60.2–63.1, plus one at 45.72;
- every one is Compress, Hi Freq 50, Analog Off (RW 140 = 2) and Trim 0.

The older lists have 207 values and the newer ones 208. Value 207 is the Auto Makeup switch, which newer versions
added [DATA].
The parameters read back through the plugin are Bypass Off, Gain 48.96, Peak Reduction 62.60, Comp/Limiter Comp,
Hi Freq 50.0, Analog Off, VU Display GR, Mix 100.0 %, Trim 0.0 dB and Auto Makeup Off
(`tmp/r113/wv/cla2a_map.txt`) [MEAS]. Her CLA-2A sits **last on the vocal insert**, after SSL E-Channel → NS1 →
RComp → DeEsser. It sees a take that RComp has already compressed, peaking around −12 dBFS
([round 113 §3c](../analise/REA_rodada113.md), [round 119 §5](../analise/REA_rodada119.md)).

## 2. Signal flow

The hardware, from the UA guide's block diagram and description [S4][SCHEMATIC]:

```
in ─► input transformer ─► T4 attenuator (series R  ── node ── CdS photocell to ground) ─► Gain pot ─►
      12AX7A voltage amp ─► 12BH7A cathode follower ─► output transformer ─► out
                                │ (node, after the cell: FEEDBACK tap; Limit = mix of input and output*)
                                ▼
      Peak Reduction pot R2 ─► 12AX7A voltage amp ─► pre-emphasis (R37 "RESP") ─► 6AQ5 EL driver ─► EL panel
                                                                                        │ light
                                         T4: EL panel ─► CdS cell #1 (gain) and CdS cell #2 (VU meter in GR mode)
```

- The guide calls it "a feed-back style compressor", because "the signal that is used to drive the side-chain
  circuit is affected by the gain reduced signal" [S4][DOC].
- The photocell "acts as the bottom leg in a voltage divider". The brighter the light, the lower its resistance
  and the lower the output [S4][DOC]. (UA's later marketing text says the opposite, "impedance increases", and
  so does [S8]'s description. The guide's divider explanation is the physically correct one.)
- *Limit mode: forum analyses of the schematic say that in Compress the side chain is taken across the cell (pure
  feedback). In Limit it is taken from a junction of two resistors and becomes "a mix … of the output signal and
  the input signal" [S17][MEAS]. That is unconfirmed; the UA guide only says Limit gives "a higher compression
  ratio" [S4][DOC].
- Fig. 5 of the guide lists R5 68 k, R6 68 k, R7 2.7 k, Gain R1 100 k, and 0.0047 µF listed next to the EL element ("HA-100X").
  The side chain (Fig. 6) shows R2 100 k (Peak Reduction), C8 .03, R31 1 k, C9 .02, R33 220 k, R37 1 M "RESP"
  (the HF-emphasis set-screw), R3 1 M "STEREO ADJ", V3 12AX7A, V4 6AQ5A, R34 22 k 2 W, R35 220 k, R36 1 k and
  C10 50 µF [S4][SCHEMATIC]. The exact topology cannot be read reliably from the text extraction; a schematic
  image would settle it.
- Tubes: two 12AX7A, one 12BH7A and one 6AQ5 [S4][DOC].

**Where the colour lives** [model, from §4 measurements + hardware]:
- **Nonlinearity:** mostly 2nd harmonic, after the gain-reduction element (it tracks the output level, not the
  input level; §4.5).
- **HF tilt:** grows with gain reduction, so it is in the attenuator itself (§4.4).
- **Noise and hum:** only when Analog is 50 / 60 Hz. Her state is Off.
- **Detector frequency dependence:** the R37 pre-emphasis, the EL panel's frequency-dependent brightness [S15],
  and Waves' Hi Freq control (§4.3).

## 3. Controls

The VST3 parameter list was read back from the trial (`tmp/r113/wv/cla2a_map.txt`) [MEAS]. RealWorld indexes are
from the probe (`tmp/r113/wv/cla2a_probe.txt`) [MEAS]. The default list has Gain 40 at RW 38 and Peak Reduction 46
at RW 74 (`cla2a_map.txt`) [MEAS].

| Control | Range | Unit | Default | Law / taper | Audible effect | State mapping |
|---|---|---|---|---|---|---|
| Gain | 0–100 | knob | 40 (list default) [MEAS]; "unity" at 32.28 per a third-party manual summary [S3] | Not linear in dB: noise at −70 dBFS gives 20 → −18.5, 30 → −3.0, 40 → +8.0, 48.96 → +15.5, 60 → +24.2, 70 → +29.2, 80 → +31.8 dB [MEAS r113]. Interpolating 32.28 gives −0.5 dB, which fits "unity" [model]. VST normalized 0.25 → 31.32, 0.75 → 54.08, 0.6928 → 48.96 [MEAS] | Make-up gain after the cell; "does not affect the compression" on the hardware [S4] | RW 38 (her 48.96) |
| Peak Reduction | 0–100 | knob | 46 [MEAS list default] | "Nonlinear", matched to the hardware [S2][DOC]. VST normalized 0.25 → 34.16, 0.75 → 56.16, 0.81 → 62.6 [MEAS] | Side-chain gain, so it sets both the threshold and the amount of compression [S4] | RW 74 (her 62.6) |
| Comp / Limiter | Comp, Limit | switch | Comp | — | Limit = higher ratio; about 3:1 / 100:1 per [S3]; Compress is 4:1 soft knee and Limit about ∞ per a hardware review [S7] | RW 112 (0 = Comp; her 0) |
| Hi Freq | 0–100 | — | 50 [MEAS list default] | Unknown | Detector HF emphasis ("Sidechain High Frequency Filter" [S2]; per [S3] it changes "the frequency emphasis of the compressor's detector", not the audio path). Equivalent to R37 [model] | RW 153 (her 50) |
| Analog | 60Hz, 50Hz, Off | enum | per [S3] the "initial" value is 60 Hz | — | Modelled hum and noise [S1][S3] | RW 140 (1 = 60 Hz, 2 = Off; her 2 = Off). The normalized value 1.0 is Off [MEAS] |
| VU Display | (In), GR, Out | enum | GR | — | Meter only | RW 17 |
| Mix | 0–100 | % | 100 | Unknown (linear or equal-power?) | Parallel blend | Not located in RW (her 100 %) |
| Trim | −18..+18 [S3] | dB | 0 | dB | Output after the mix [S3] | RW 206 (her 0) |
| Auto Makeup | Off, On | switch | Off | — | Ties gain to Peak Reduction [S3] | RW 207 (absent in 207-value lists; her 0) |

The other ~195 RW values never moved when a control was probed (`cla2a_probe.txt`). They look like internal
calibration tables, for example the monotone sequence −0.2 … −29.8 at RW ~120–140 and the time-like sequence
0.0001 … 8.63 at RW ~165–190 [MEAS]. **Do not interpret them.** They are identical in all her lists, and reading
their meaning would require reverse engineering the plugin, which this project does not do.

## 4. Known curves & behaviour

### 4.1 Static curve at her state

1 kHz sine stairs: 3 dB steps of 0.6 s each, rising from −60 dBFS peak, 48 kHz. Gain in dB (output/input), for
time windows after each step [MEAS]. The 450–600 ms column matches
[round 113 §3c](../analise/REA_rodada113.md).

| in dBFS | 0–20 ms | 20–60 | 60–150 | 150–300 | 450–600 (quasi-static) | GR |
|---|---|---|---|---|---|---|
| −60 … −33 | 16.0 | 16.0 | 16.0 | 16.0 | **16.0** | 0 |
| −30 | 16.0 | 16.0 | 15.9 | 15.9 | 15.8 | 0.2 |
| −27 | 15.6 | 15.2 | 14.8 | 14.5 | 14.4 | 1.6 |
| −24 | 13.9 | 13.1 | 12.6 | 12.5 | 12.4 | 3.6 |
| −21 | 11.6 | 10.7 | 10.3 | 10.2 | 10.2 | 5.8 |
| −18 | 9.2 | 8.3 | 8.0 | 8.0 | 7.9 | 8.1 |
| −15 | 6.7 | 5.9 | 5.7 | 5.7 | 5.6 | 10.4 |
| −12 | 4.3 | 3.5 | 3.4 | 3.3 | 3.3 | 12.7 |
| −9 | 1.9 | 1.2 | 1.1 | 1.1 | 1.0 | 15.0 |
| −6 | −0.6 | −1.1 | −1.1 | −1.2 | −1.2 | 17.2 |
| −3 | −3.0 | −3.4 | −3.4 | −3.5 | −3.5 | 19.5 |
| 0 | −5.3 | −5.7 | −5.8 | −5.8 | −5.8 | 21.8 |

**Feedback power-law fit** [model, least squares on the quasi-static column, −24 … 0 dBFS]. Let `g` be the
linear gain of the cell, `x` the input and `y = g·x` the cell output before make-up (the hardware's
side-chain tap). Then

```
1/g − 1 = (|y| / y0)^γ        γ = 4.54,  y0 = −26.7 dBFS (peak),  makeup = +16.0 dB
```

This predicts GR = 0.06 / 0.9 / 2.1 / 3.8 / 7.9 / 12.5 / 17.2 / 22.0 dB at −36 / −30 / −27 / −24 / −18 / −12 / −6 /
0 dBFS, against 0 / 0.2 / 1.6 / 3.6 / 8.1 / 12.7 / 17.2 / 21.8 measured (≤ 0.25 dB from −24 up, about 0.6 dB high
at −27 / −30, where the real knee is slightly sharper).
- **Local slope:** dGR/dx ≈ 0.76 between −18 and 0 dBFS, i.e. **about 4.2:1**. The asymptote is 1 + γ = 5.5:1.
- **Agreement with the literature:** "a soft knee and a 4:1 ratio" for Compress [S7]; Yu & Fazekas fit about 4:1
  for the hardware in Compress [S9][PAPER]; UA says "roughly 3:1" on average [S6].
- **Interpretation:** `1/g − 1` is the cell conductance relative to the series resistor (§5.1). `(|y|/y0)^γ` is the
  product of the EL light-vs-drive law and the CdS conductance-vs-light law [model].

### 4.2 Attack: level-dependent (light-dependent) speed

From the same stairs, using 1 ms RMS windows: the excess gain above the final value after each +3 dB step
[MEAS, this dossier's reading of the round-113 render].

| step to | excess at 1 ms | 5 ms | 10 ms | 20 ms | 50 ms | 100 ms | 200 ms | t(37 %) | t(10 %) |
|---|---|---|---|---|---|---|---|---|---|
| −27 | 1.33 | 1.23 | 1.12 | 0.95 | 0.61 | 0.32 | 0.11 | **66 ms** | 178 ms |
| −21 | 2.19 | 1.77 | 1.41 | 0.96 | 0.41 | 0.19 | 0.11 | 25 ms | 87 ms |
| −15 | 2.13 | 1.50 | 1.02 | 0.54 | 0.19 | 0.12 | 0.08 | 13 ms | 43 ms |
| −9 | 2.01 | 1.10 | 0.64 | 0.32 | 0.14 | 0.10 | 0.05 | 8 ms | 30 ms |
| −3 | 1.99 | 0.73 | 0.31 | 0.14 | 0.09 | 0.06 | 0.03 | **4 ms** | 14 ms |

- **Two components.** A main one whose time constant falls about 16× from the knee to 20 dB of gain reduction,
  and a small slow tail (≈ 0.05–0.1 dB still left at 200 ms, gone by 400 ms).
- **The first millisecond.** It already absorbs about 1 dB of each 3 dB step, the immediate part of a feedback
  loop.
- **Agreement with the literature.** Hardware specs give an "average" 10 ms [S5][S6]. Photoresistor physics says
  "the attack time decreases with the received light" [S14][PAPER]. Parker & D'Angelo's vactrol model makes the
  rate depend on the current state, "so that it responds quicker when at high values" [S13][PAPER].
- **Feedback alone does not explain it** [model]. Linearizing the law of §4.1 gives an effective time constant
  `τ_eff = τ_cell / (1 + γ (1 − g))`. That accounts for only a 2× speed-up between −24 and 0 dBFS. The cell's own
  attack time must therefore fall from about 150 ms when dim to about 20 ms when bright, roughly
  `τ_a ∝ C^−0.65` with `C = 1/g − 1`. This is a starting point for the fit, not a measurement.

### 4.3 Release and memory

- **Hardware.** "approximately 0.06 seconds for 50% release, 0.5 to 5 seconds for complete release depending upon
  the amount of previous reduction" (UA reissue spec) [S4][DOC]. Another statement is "40–80 milliseconds to
  approximately half of its off resistance", the rest "as much as several seconds". Release is slower after long
  or heavy compression ("memory") [S4][DOC]. Wikipedia and UA's plug-in notes say 1–15 s for full release
  [S5][S6].
- **CLA-2A, indirect** [MEAS, caveat]. The 6 kHz stairs were rendered right after the 1 kHz stairs, which ended
  at 0 dBFS with 21.8 dB of gain reduction. In between came the host's `reset` and 0.5 s of settling. The 6 kHz
  run begins below its own low-level gain and climbs over seconds while the input is far below threshold:
  - 14.3 dB at −60 (t ≈ 1.5 s after the 1 kHz stairs ended), then 14.5 / 14.7 / 14.9 / 15.0 / 15.1 / 15.2 / 15.2
    at t ≈ 2.1 … 5.7 s;
  - the steady path gain at 6 kHz is ≈ 15.7 dB (from pink noise, §4.4).
  - So the leftover gain reduction is ≈ 1.4 dB at 1.5 s and ≈ 0.5 dB at 5.7 s, a slow tail with τ of about 4 s.
  - It also shows that **the host's `reset` does not clear the modelled cell's memory**.
  - Round 113 read "6 kHz: 14.3 at −60" as the plugin's tone. It is more likely this release tail [model]. The
    pink-noise response is flat at 6 kHz (−0.26 dB).
  - The release has not been measured directly (§8).

### 4.4 Frequency response: flat when idle, HF tilt under gain reduction

Pink noise through her state; cross-spectral H = Syx/Sxx in 1/3-octave bands, dB
[MEAS, this dossier's reading of the round-113 renders]:

| input | 31.5 | 63 | 125 | 250 | 500 | 1k | 2k | 3k | 4k | 6k | 8k | 10k | 12k | 16k | 20k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| −40 dBFS RMS (≈ 2 dB GR) | 13.84 | 13.96 | 13.95 | 13.97 | 13.98 | 13.97 | 13.93 | 13.88 | 13.82 | 13.71 | 13.64 | 13.65 | 13.74 | 14.06 | 14.38 |
| −18 dBFS RMS (≈ 15 dB GR) | 0.94 | 1.11 | 1.11 | 1.13 | 1.18 | 1.23 | 1.33 | 1.51 | 1.73 | 2.34 | 3.06 | 3.86 | 4.66 | 6.12 | 7.11 |

- **Idle:** flat within ±0.3 dB from 63 Hz to 12 kHz. There is a slight LF roll-off (−0.13 dB at 31.5 Hz, phase
  +4.7° at 50 Hz, about a first-order high-pass near 4 Hz) [model]. The hardware is specified "+/- 0.1 dB, 30
  cycles to 15 kilocycles" [S4].
- **Under gain reduction, the HF rises.** Relative to 1 kHz the −18 dBFS row fits
  `10·log10(1 + (f/fz)²)` with **fz ≈ 11.6 kHz**: predicted +0.28 / +1.03 / +3.2 / +4.6 / +6.0 dB at 3 / 6 / 12 /
  16 / 20 kHz, measured +0.28 / +1.11 / +3.4 / +4.9 / +5.9.
  - **The circuit reading** [model]. A divider whose series arm `R_s` has a small capacitance `C_s` across it,
    with the cell `R_c` to ground: `H(s) = g0 (1 + s/ωz) / (1 + s·g0/ωz)`, where `g0 = R_c/(R_c+R_s)` is the DC
    gain of the cell and `ωz = 1/(R_s C_s)`.
  - With `R_s ≈ 136 kΩ` (R5 + R6, if they are the series arm) this needs `C_s ≈ 100 pF`, a stray-capacitance
    size. That is a hypothesis; the schematic does not show such a capacitor in the text we could read.
  - Idle (`g0 ≈ 1`) the pole and zero cancel, so the response is flat. With gain reduction the pole moves to
    `ωz/g0`, so the HF escapes the gain reduction (at most +GR dB at the very top).
  - Whether it is the LA-2A's or Waves' own is unknown. It is part of her sound either way: her vocal through the
    CLA-2A sits at 5–15 dB of gain reduction.
- **The detector is more sensitive at 6 kHz.** 6 kHz stairs above threshold, quasi-static
  (0.45–0.6 s window) [MEAS]:

  | in dBFS | −27 | −24 | −21 | −18 | −15 | −12 | −9 | −6 | −3 | 0 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | gain dB | 8.8 | 6.8 | 4.8 | 2.8 | 0.8 | −1.3 | −3.4 | −5.6 | −7.7 | −9.7 |

  (the −33 / −30 steps fell in a demo mute). Taking 15.7 dB as the 6 kHz base, GR(6 kHz, x) ≈ GR(1 kHz, x + 6 dB).
  The side chain is ≈ 5–6 dB more sensitive at 6 kHz than at 1 kHz at Hi Freq 50, including the ≈ +1 dB the
  audio-path tilt feeds back. The candidates are Waves' Hi Freq, the R37 pre-emphasis [S4], and the EL panel's
  brightness, which rises roughly in proportion to frequency at constant voltage [S15][PAPER]. Not separated yet.
  See §8.
- **Pink noise compresses harder than RMS would suggest.** Pink at −18 dBFS RMS (peak −4.9) gets ≈ 14.9 dB of
  gain reduction. A 1 kHz sine at −15 dBFS RMS (−12 peak) gets 12.7 dB. This fits a peak-sensitive,
  HF-emphasised detector [model].

### 4.5 Harmonic signature

1 kHz stairs, steady part (8192-point Hann FFT 0.38 s into each step); harmonic level relative to the fundamental,
dBc [MEAS, this dossier's reading of the round-113 render]:

| in dBFS | −60 | −48 | −36 | −30 | −27 | −24 | −18 | −12 | −6 | 0 |
|---|---|---|---|---|---|---|---|---|---|---|
| H2 | −82.7 | −71.0 | −60.2 | −55.6 | −54.5 | −53.8 | −52.9 | −52.1 | −51.3 | −50.6 |
| H3 | −87.1 | −75.5 | −65.2 | −61.3 | −60.4 | −59.9 | −59.1 | −57.9 | −56.1 | −53.5 |
| H5 | −104 | −92.5 | −80.8 | −75.6 | −74.0 | −73.0 | −71.2 | −69.3 | −67.3 | −64.6 |

- **Below threshold:** H2 and H3 rise 1 dB per dB of input. That is a quadratic- and cubic-like term at a stage
  whose level follows the input there.
- **Above threshold:** they rise only ≈ 0.4 dB per 3 dB, following the compressed output level. So the
  nonlinearity sits after the cell [model].
- **Shape:** H2 dominates H3 by 5–6 dB; H4 is far lower (≈ −72 … −80 dBc in the same table); H5 > H4. THD at
  0 dBFS in ≈ −49 dB (≈ 0.35 %).
- **The 6 kHz stairs** give nearly identical H2 / H3 (−49.3 / −53.4 dBc at 0 dBFS).
- **Hardware comparison** [S8][PAPER]. Moore, Till & Wakefield (AES 140) measured an LA-2A:
  - THD 0.20 / 0.50 / 0.90 % at 0 / +9 / +16 dBu with no compression;
  - **2.13 % at 125 Hz** and 2.64 % at 1 kHz during 8 dB of gain reduction (1.18 % / 2.38 % in the release);
  - "a fairly typical exponential drop in level of the harmonic artefacts".
  The hardware is dirtier while compressing (cell and tube modulation). The CLA-2A at her levels is cleaner
  (≈ 0.3 %) [MEAS]. Distortion at 125 Hz under gain reduction (cell ripple at low frequencies) has **not** been
  measured on the CLA-2A (§8).

### 4.6 Stereo, noise, sample rate

- **Stereo:** the CLA-2A Stereo returned identical L / R for identical input (max |L−R| 6e−8) [MEAS]. Linking is
  not proven (§8). The hardware links two units through terminal 6 with the R3 balance trim [S4].
- **Noise:** Analog is Off in her state. The 60 / 50 Hz modes add hum and noise [S1]. The levels were not measured.
- **Sample rate:** only 48 kHz was measured.
- **Demo mode:** the trial mutes about 1 s every 20 s [round 119 §1](../analise/REA_rodada119.md). Mask exact
  zeros in every measurement.

## 5. Published science

### 5.1 The T4 cell as a divider with a light-dependent conductance

From the UA guide [S4][DOC]: `v_out = v_in · R_c / (R_s + R_c)`. With a conductance `G_c = 1/R_c`:

```
g = 1 / (1 + R_s·G_c)          (no gain reduction when dark: R_c → MΩ;  "up to 40 dB" [S4] needs R_s·G_c ≈ 99)
```

CdS static law: `R_c ∝ E^−γ_cds` (illuminance `E`). PerkinElmer-style datasheets define the slope ("gamma") as
`log(R10/R100) / log(100 lux / 10 lux)` [S16][DOC]. Rise is specified as the time to 63 % of the final current and
decay as the time to 37 % [S16]. The EL panel's light rises steeply with drive voltage (about 4× brightness for 2×
RMS voltage) and roughly in proportion to frequency [S15][PAPER]. So the static loop is
`R_s·G_c = (k·|v_sc|)^γ` with γ = a_EL·γ_cds. That is exactly the §4.1 fit, with γ = 4.54 at her setting [model].

### 5.2 Program-dependent dynamics

- **Two-stage release and memory (hardware literature).** "After the light is removed from the cell, it releases
  quickly (40-80 milliseconds) to approximately half of its off resistance. The remainder … several seconds." The
  recovery time "depends on how long light had been shining on it and how bright the light" [S4][DOC].
- **Parker & D'Angelo (DAFx-13), heuristic vactrol model.** A one-pole low-pass (integrator) whose rate switches
  between an attack and a decay value depending on the sign of the input's derivative (decay 250 ms for the
  VTL5C3). The chosen rate "is then modulated further by the current output value of the vactrol model, so that
  it responds quicker when at high values" [S13][PAPER]. As equations [model, our transcription of that
  structure]:
  ```
  dC/dt = (L − C) / τ(L, C),   τ = τ_att·h(C) if L > C else τ_dec·h(C),   h decreasing in C
  ```
- **Najnudel, Müller, Hélie & Roze (DAFx-23), physical model.** Port-Hamiltonian, passive model of a
  photoresistor (VTL5C3/2). Free-carrier generation by photo-absorption and Shockley-Read-Hall recombination
  through defect levels that act as electron and hole traps. Program-dependent attack and release emerge from it;
  attack times range from tens of µs to hundreds of ms, and "the attack time decreases with the received light".
  It is identified from measurements and run inside a minimal optical compressor [S14][PAPER]. The traps are the
  physical origin of the "memory": slow carrier release keeps conductance up after long or bright exposure [model].
- **Yu & Fazekas (2025), "4A-2A", grey-box.** A five-parameter feed-forward compressor (threshold, ratio, attack,
  release, make-up) fitted per Peak Reduction setting by Newton-Raphson to the SignalTrain LA-2A recordings.
  Smoothing on the gain: `g[n] = α ĝ[n] + (1−α) g[n−1]`, with α = α_at when the target gain falls and α_rt
  otherwise, and `α = 1 − exp(−2.2 T / t)`. Findings:
  - ratio ≈ 4:1 in compress, slightly higher in limit;
  - attack and release vary exponentially with Peak Reduction;
  - no consistent solution below Peak Reduction 40;
  - the authors note the real unit is feedback and its two-stage release is not captured; a GRU on top
    (4A-2A-G) is best [S9][PAPER].
- **SignalTrain (Hawley, Colburn, Mimilakis 2019).** Data: a real LA-2A, Peak Reduction 0–100 in steps of 5, both
  switch positions, about 20 min of music and synthetic signals at 44.1 kHz. Used by the 4A-2A paper and by
  Steinmetz & Reiss's temporal convolutional network model (AES 152, 2022) [S10][S11][PAPER]. It is a public
  hardware reference for checking behaviour at Peak Reduction values other than hers. Check the dataset licence
  before use.
- **Wright & Välimäki (DAFx20in22).** Grey-box compressor = conventional gain computer + smoothing inside a
  trainable framework, at less than 10 % of the black-box cost [S12][PAPER]. Which devices the paper fitted was
  not verified here.

### 5.3 Feedback compressor algebra (Giannoulis, Massberg & Reiss 2012)

The JAES tutorial covers feed-forward and feedback topologies, peak and RMS detectors, and log-domain smoothing
with separate attack and release ("smooth branching") [S18][PAPER]. The following short derivation is ours
[model]:
- **Why a dB-domain gain computer is a poor fit here.** Put a dB-domain gain computer of ratio R on the *output*
  (feedback): GR = (1 − 1/R)(y − T) and y = x − GR. Then dGR/dx = (1 − 1/R)/(2 − 1/R), so the input-referred
  ratio is only 2 − 1/R, at most 2:1.
- **The power-law cell has no such cap.** It is intrinsically feedback, and its asymptote is 1 + γ (here 5.5:1),
  which is why it is the right structure for this plugin. One sample of
loop delay (computing g from y[n−1]) is acceptable at 48 kHz, because the loop's fastest time constant is
≥ 4 ms [model].

## 6. What LYNX does today

**Glow Leveler** (`src/core/MixSuite.cpp`, namespace `glow`, lines ≈ 518–599; parameters Peak reduction, Gain,
Limit, Emphasis, Mix):
- **Detector:** the previous output sample, through a detector high-shelf ("Emphasis": up to +10 dB at 2.5 kHz).
  Peak of |L|, |R| (linked), then a "panel" follower (1 ms up, 30 ms down).
- **Light:** `light = (sc·panel)^γ`, with γ = 2 (compress) or 9 (limit), where sc = −20 … +40 dB.
- **Cell:**
  - fast conductance: attack 10 ms, release 50 ms;
  - slow conductance: attack **400 ms**, release `0.4 + 4.6·memory` s;
  - memory: rises over 2 s while light > 0.1 and falls over 8 s;
  - `C = 0.7·fast + 0.3·slow`, `g = 1/(1+C)`.
- **Make-up:** 0 … +30 dB, then the dry/wet Mix.
- **No tone stage:** no HF tilt, no harmonics, no noise.

**Translation** `translateWavesCla2a` (`src/core/PluginSubstitutes.cpp` ≈ 1078–1100; table entry ≈ 382–394):
- Gain RW 38 goes through the measured gain table, + 1.5 dB, into Glow's 0–30 dB gain.
- Peak Reduction RW 74 becomes Glow's peak reduction `0.40 + 0.021·(knob − 40)`, i.e. +1.26 dB of side-chain gain
  per knob unit. Her 62.6 gives 0.875, which is +32.5 dB.
- Comp / Limit comes from RW 112. Emphasis is fixed at 0. Hi Freq, Analog, Mix, Trim and Auto Makeup are ignored.
- Without a readable state, the preset is peak reduction 0.30 and gain 0.067.

**Measured residual error.**
- **Envelope:** on her take after RComp, Glow's 50 ms level envelope follows the original within 0.3–1.4 dB at
  Peak Reduction 40 / 62.6 / 75 / 85. Glow saturates above 75.
- **Stairs:** "Glow's knee is softer (3 dB RMS)" ([round 113 §3c](../analise/REA_rodada113.md)). Glow alone on a
  take moved the octave spread from 0.85 to 0.39 dB
  ([round 113 §3](../analise/REA_rodada113.md)).
- **Whole chain** ([round 119 §5](../analise/REA_rodada119.md)). Adding the CLA-2A stage moves reference − LYNX
  level from +0.35 / +0.49 / +0.45 to **+0.67 / +1.27 / +1.07 dB**. The 0.5 s-window IQR goes from 0.79 / 0.66 /
  0.78 to **1.36 / 0.90 / 1.48 dB** (game over / kwe / luisa). The CLA-2A stage is the **largest single
  contributor** to the chain's level and envelope error.

Why, from §4 [model]:
1. **Ratio.** γ = 2 gives 3:1 with a broad knee, where the CLA-2A is 4.2:1 local and 5.5:1 asymptotic (γ = 4.54).
   Matching the take's envelope at one level needs a compromise that fails at others.
2. **Slow attack.** The 30 %-weighted 400 ms slow attack makes Glow overshoot for hundreds of ms after each phrase
   onset. The CLA-2A has ≤ 0.1 dB left after 200 ms.
3. **Fixed attack.** Glow's attack is fixed at 10 ms. The CLA-2A goes from 66 ms near the knee to 4 ms deep in
   compression.
4. **HF tilt.** None in Glow. That is +1 … +6 dB above 6 kHz under her typical gain reduction, part of the
   "reference louder" result.
5. **Detector emphasis.** Hi Freq 50 is about 5–6 dB at 6 kHz; Glow's emphasis is 0.
6. **2nd harmonic.** None in Glow (about −52 dBc under compression).

## 7. Implementation guidance for LYNX

Keep Glow's architecture (feedback, `g = 1/(1+C)`, linked peak detector). Change its laws and add the tone stage.
Calibrate at her state first: one state covers practically the whole corpus. Ordered steps:

1. **Static law.**
   - Make γ a calibrated constant: 4.54 for Compress. Keep 9 for Limit until it is measured; probably larger
     (§8).
   - Re-derive the translation so that at her Peak Reduction 62.6 the panel level × sc puts
     `y0 = −26.7 dBFS peak` at the cell output.
   - Check that Glow's panel follower reads a steady sine's peak; otherwise correct y0 by the follower's ripple
     loss.
   - **Pass:** the 1 kHz stairs quasi-static column of §4.1 within ≤ 0.3 dB RMS (−36 … 0) and ≤ 0.5 dB at every
     point.
2. **Cell dynamics.** Use one main conductance state plus a small slow state:
   ```
   L   = (sc · panel)^γ                                  // target conductance
   τ_a = clamp(τ_a0 · (max(C,ε))^−κ, 3 ms, 200 ms)       // start: τ_a0 ≈ 100 ms, κ ≈ 0.65  (§4.2)
   C_f += (L − C_f) · (1 − exp(−T/τ))  with τ = τ_a if L > C_f else τ_r1 (≈ 60–90 ms, i.e. 50 % release in ≈ 60 ms)
   C_s += … attack ≈ 100–150 ms, weight ≈ 0.03–0.05 (the 0.05–0.1 dB tail at 200 ms), release τ_r2(m)
   m   = memory of (light × time): τ_r2 = 0.5 s … 5 s (UA spec) — calibrate per §8
   C   = (1 − w_s) C_f + w_s C_s ;   g = 1/(1 + C)
   ```
   **Pass:** the step table of §4.2 (excess at 1 / 5 / 10 / 20 / 50 / 100 / 200 ms) within 0.2 dB for every step;
   t37 within ±20 %.
3. **Release and memory.** Calibrate τ_r1, τ_r2 and m against the release measurements of §8 (Q2 and Q3). Until
   they exist, use the UA spec (50 % in 0.06 s; full in 0.5–5 s, growing with previous gain reduction × duration)
   and the indirect CLA-2A tail of §4.3 (≈ 1.4 dB left at 1.5 s and ≈ 0.5 dB at 5.7 s after about 7 s of 4–22 dB
   gain reduction).
4. **Detector emphasis.** Fit Glow's emphasis shelf so that GR(6 kHz, x) = GR(1 kHz, x + 5.5 dB) at Hi Freq 50
   (6 kHz stairs table, §4.4). Then map RW 153 (Hi Freq) once its law is measured (§8 Q4). A first-order high
   shelf in the side chain, like R37, is the natural structure.
5. **Gain-dependent HF tilt in the audio path.** Implement the divider's frequency dependence exactly, as one TPT
   (zero-delay) one-pole per channel:
   ```
   ωp = ωz / g0  (ωz = 2π·11.6 kHz; clamp ωp below ≈ 0.45·fs or run 2× oversampled)
   lp = OnePoleLP_ωp(x)   ;   out = g0·lp + (x − lp)      // = g0 (1 + s/ωz)/(1 + s g0/ωz)
   ```
   Here g0 is the cell gain of this sample. Because the side chain is fed back from `out`, the detector sees the
   tilt too, as in the hardware. At g0 = 1 it is an identity.
   **Pass:** the −18 dBFS pink row of §4.4 within ≤ 0.3 dB per 1/3-octave band, 1–20 kHz. The −40 row stays flat
   (±0.3 dB).
6. **Make-up gain and output stage.**
   - Gain law from the round-113 table, via monotone cubic interpolation instead of piecewise linear. Remove the
     +1.5 dB fudge once steps 1–5 are in, and re-check.
   - Extend Glow's gain range above 30 dB: knob 80 is +31.8.
   - Then a memoryless asymmetric waveshaper after the make-up:
     `y = x + a2·x² + a3·x³`, with DC removed by a ≈ 5 Hz high-pass, which also gives the measured LF phase.
     Scale it to the output level so that H2 = −82.7 dBc at −60 dBFS in and about −51 dBc at 0 dBFS in, with H3
     5–6 dB below H2.
   - Since H2 follows the output level, `a2 ≈ 2·10^(−82.7/20) / A_out(−60 in)` with `A_out = 10^(−44/20)`, which
     gives a2 ≈ 0.023. Measure H2 versus the Gain knob before trusting that the waveshaper sits after the make-up
     (§8 Q6).
   - **Pass:** H2 / H3 within 2 dB of §4.5 at every level, at 1 kHz and 6 kHz.
7. **Trim / Mix / Auto Makeup / Analog.**
   - Her values are neutral (Trim 0, Mix 100, Auto Makeup Off, Analog Off). Translate Trim (dB after the mix) and
     Mix (law to be measured).
   - Analog hum and noise is optional; it only matters for other people's projects.
8. **Translation update** (`translateWavesCla2a`): emphasis from RW 153, γ by RW 112, and make-up gain without the
   fudge. Keep the [EXEC] tests in `tests/` updated.
9. **End-to-end acceptance on her material** (renders of the official trial already exist:
   `C:\Users\kauan\lynx-test\r113\wv\m\cla2a_vox.wav`, `clapr*_voxr.wav`; chain: round 119 §5):
   - 50 ms level envelope of her take after RComp: ≤ 0.5 dB RMS, ≤ 1.5 dB max;
   - overall level within ±0.2 dB;
   - octave bands 63 Hz–16 kHz ≤ 0.5 dB RMS;
   - the chain's +CLA-2A stage adds ≤ 0.2 dB to the level error and ≤ 0.2 dB to the IQR of round 119 §5.

## 8. Open questions to measure (official trial, black-box only, fxmeasure; mask demo mutes; load a dummy plugin first)

| # | Question | Test signal / method | Decides |
|---|---|---|---|
| Q1 | Static curve vs Peak Reduction | 1 kHz stairs −60…0 dBFS (3 dB × 0.6 s), PR 0, 20, 40, 50, 62.6, 75, 85, 100, Comp and Limit; `reset` + 10 s of silence between runs | y0(PR), γ(PR), and the Limit γ / mix law; replaces the take-fitted `0.40 + 0.021·(knob−40)` |
| Q2 | Two-stage release | 1 kHz burst at −6 dBFS for 0.1 / 0.5 / 2 / 10 s, then −40 dBFS probe tone for 20 s; read GR(t) | τ_r1, τ_r2 and the memory law (duration dependence) |
| Q3 | Release vs depth | Same, burst at −24 / −12 / 0 dBFS for 2 s | Memory vs brightness (gain reduction depth) |
| Q4 | Hi Freq law | Hi Freq 0 / 25 / 50 / 75 / 100: stairs at 100 Hz, 1, 3, 6, 10 kHz; plus pink at −40 dBFS (audio path unchanged?) | The side-chain shelf (corner, depth) vs knob; confirms it is detector-only |
| Q5 | Gain-dependent HF tilt | Pink at −50 … −10 dBFS RMS in 5 dB steps; cross-spectrum per level; also sine sweep under a steady 1 kHz compressing tone | fz and whether it scales as 1/g0 (divider) or is a fixed shelf |
| Q6 | Where the harmonics are | 1 kHz at −30 dBFS (idle) with Gain 20 / 40 / 60 / 80; then compressing levels | Waveshaper before or after the make-up; level law |
| Q7 | LF distortion under compression | 125 Hz and 60 Hz stairs; H2 / H3 / IMD at 10 dB gain reduction | Cell ripple at low frequencies (the hardware shows 2.1 % at 125 Hz [S8]) |
| Q8 | Detector ballistics | 1 kHz tone bursts of 1 / 3 / 10 / 30 ms at −6 dBFS; 50 Hz vs 1 kHz at the same RMS | Peak vs RMS vs EL-panel response; ripple |
| Q9 | Stereo linking | L only at −6 dBFS, R silent (and R at −30) | Linked (max / sum) or dual mono |
| Q10 | Mix law and Trim | Mix 0 / 25 / 50 / 75 % with a compressing tone; Trim ±6 | Linear vs equal-power blend |
| Q11 | Sample-rate behaviour | Q1 / Q5 at 44.1 and 96 kHz | Oversampling, tilt near Nyquist |
| Q12 | Does `reset` clear the cell? | Stairs to 0 dBFS, `reset`, immediate −60 dBFS tone | Measurement hygiene (§4.3 caveat) |
| Q13 | Analog modes | Silence and a −40 dBFS tone with Analog 60 / 50 Hz | Hum / noise spectrum and level (other users' projects) |

## 9. Sources

Repo (relative to `docs/specs/`):
- [round 113 §3, §3c, §4b](../analise/REA_rodada113.md): CLA-2A RealWorld layout, her state, stairs, gain law,
  Glow fit [MEAS].
- [round 119 §1, §5](../analise/REA_rodada119.md): trial loading quirks, demo mutes, chain errors [MEAS].
- [PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md): substitute table.
- `../../src/core/MixSuite.cpp` (Glow Leveler) and `../../src/core/PluginSubstitutes.cpp` (`[waves-cla-2a]`,
  `translateWavesCla2a`).
- Raw files: `~/.claude/jobs/f8789b49/tmp/r113/wv/cla2a_map.txt`, `cla2a_probe.txt`, `states.txt`;
  `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`.
- Renders re-read here: `C:\Users\kauan\lynx-test\r113\wv\m\` (`in_/cla2a_` `stairs`, `stairs6k`, `pink40`,
  `pink18`) [MEAS].

Web (all accessed 2026-10-21):
1. [S1] Vintage King — Waves CLA Classic Compressors (CLA-2A features, vendor copy) —
   https://vintageking.com/products/waves-cla-classic-compressors-tdm — [DOC]
2. [S2] Waves CLA-2A user guide (B&H-hosted PDF; read only through search excerpts, because direct download is
   blocked): Gain 0–100, initial 40; Peak Reduction scale non-linear; Sidechain High Frequency Filter; Analog
   section — https://www.bhphotovideo.com/lit_files/981215.pdf — [DOC]
3. [S3] J. Nilo, "Waves CLA-2A explained" (third-party summary of the current manual: ranges, initial and reset
   values, Hi Freq, Auto Makeup, 0 VU = −18 dBFS; unverified against the manual) —
   https://josephnilo.com/blog/waves-cla-2a-explained/ — [DOC]
4. [S4] Universal Audio, Model LA-2A Leveling Amplifier User's Guide, rev 1.3 (2000): specs, R37, R3, theory of
   operation, schematic excerpts — https://www.adorama.com/col/productManuals/UALA2AD.pdf — [DOC][SCHEMATIC]
5. [S5] Wikipedia, "LA-2A Leveling Amplifier" — https://en.wikipedia.org/wiki/LA-2A_Leveling_Amplifier — [DOC]
6. [S6] Universal Audio, "Tips & Tricks — Teletronix LA-2A Classic Leveler Plug-In Collection" (R37 Emphasis,
   timing, model generations) — https://uaudio.com/blog/la-2a-collection-tips-tricks — [DOC]
7. [S7] A. Tubbs, "Warm Audio WA-2A" review, Sound On Sound, June 2016 (LA-2A clone; Compress 4:1 soft knee,
   Limit ≈ ∞) — https://www.soundonsound.com/node/4913644 — [DOC]
8. [S8] A. Moore, R. Till, J. P. Wakefield, "An Investigation into the Sonic Signature of Three Classic Dynamic
   Range Compressors", AES 140th Convention, Paris, 2016 —
   https://eprints.hud.ac.uk/id/eprint/28482/1/MooreINvestigationAES.pdf — [PAPER][MEAS]
9. [S9] C.-Y. Yu, G. Fazekas, "Sound Matching an Analogue Levelling Amplifier Using the Newton-Raphson Method",
   arXiv:2509.10706, 2025 — https://arxiv.org/html/2509.10706v1 — [PAPER]
10. [S10] S. H. Hawley, B. Colburn, S. I. Mimilakis, "SignalTrain: Profiling Audio Compressors with Deep Neural
    Networks", arXiv:1905.11928 (AES 147, 2019) — https://arxiv.org/pdf/1905.11928 — [PAPER]
11. [S11] C. J. Steinmetz, J. D. Reiss, "Efficient neural networks for real-time modeling of analog dynamic range
    compression", AES 152nd Convention, 2022 — https://qmro.qmul.ac.uk/xmlui/handle/123456789/80280 — [PAPER]
12. [S12] A. Wright, V. Välimäki, "Grey-Box Modelling of Dynamic Range Compression", DAFx20in22, pp. 304–311 —
    https://dafx.de/paper-archive/details/Oz5HVaU0tKAjfD3ivOlv5g — [PAPER]
13. [S13] J. Parker, S. D'Angelo, "A Digital Model of the Buchla Lowpass-Gate", DAFx-13, Maynooth, 2013 (§3.2
    vactrol model) — https://dafx.de/paper-archive/2013/papers/44.dafx2013_submission_56.pdf — [PAPER]
14. [S14] J. Najnudel, R. Müller, T. Hélie, D. Roze, "Power-Balanced Dynamic Modeling of Vactrols: Application to
    a VTL5C3/2", DAFx-23, Copenhagen, 2023 — https://www.dafx.de/paper-archive/2023/DAFx23_paper_50.pdf — [PAPER]
15. [S15] Philips Technical Review vol. 19 (1957), electroluminescence (emittance roughly proportional to frequency
    at constant RMS voltage, audio band); with SunTronic EL application guide (brightness vs voltage) —
    https://worldradiohistory.com/Archive-Company-Publications/Philips-Technical-Review/50s/Philips-Technical-Review-1957.pdf ;
    https://sunchemical.com/wp-content/uploads/2025/04/SunTronic-Electroluminescent-Materials-Application-Guide_MAR2125.pdf
    — [PAPER][DOC] (read through search excerpts)
16. [S16] PerkinElmer / Advanced Photonix, "Photoconductive Cell" application notes (gamma definition, rise /
    decay definitions) — https://wp.optics.arizona.edu/milster/wp-content/uploads/sites/48/2016/06/APP_PhotocellIntroduction.pdf
    — [DOC] (read through search excerpts)
17. [S17] GroupDIY forum, "LA2A feedforward/feedback design?" and "Please explain LA-2 limit compress switch"
    (schematic readings of the Compress / Limit side-chain tap; read through search excerpts) —
    https://groupdiy.com/threads/la2a-feedforward-feedback-design.50129/ ;
    https://groupdiy.com/threads/please-explain-la-2-limit-compress-switch.79347 — [MEAS] (forum, unverified)
18. [S18] D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and
    Analysis", JAES 60(6), 2012 —
    https://www.eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf
    — [PAPER]
19. [S19] O. Stroe, PhD thesis on optical compressors, University of Huddersfield (modified opto-cell parameters
    change attack / release and THD; EL colour shift and frequency-dependent brightness; abstract-level only) —
    https://eprints.hud.ac.uk/id/eprint/34532/1/Octavian%20Stroe%20FINAL%20THESIS.PDF — [PAPER]
