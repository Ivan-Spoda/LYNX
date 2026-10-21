# Waves CLA-76 Compressor / Limiter — spec dossier (LYNX substitute: Compressor)

Round 125 (spec125). Clean-room dossier from the Waves user guide (facts read through public mirrors and search
excerpts — waves.com refused automated fetches), the UREI 1176LN documentation and history, FET application notes,
the compressor-design literature, LYNX's source and her saved state. No Waves binary was opened, decompiled or
instrumented.
Tags: [DOC] vendor document · [PAPER] academic · [SCHEMATIC] hardware documentation · [MEAS] measurement ·
[DATA] her projects / LYNX files · [model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** CLA-76 models two of Chris Lord-Alge's UREI 1176 units — "Bluey" and "Blacky" (default)
— differing in gain stages, time constants, THD and noise [S1][S2][DOC]. Controls: Input and Output (−∞…0 dB,
non-linear scale matched to the hardware; defaults −30 / −18), Attack 1…7 (7 = fastest, < 50 µs), Release 1…7
(1 = 1 s, 7 = 50 ms), Ratio 4/8/12/20/All, Meter, Revision, and **Analog (Off / 50 Hz / 60 Hz, default 60 Hz)** which
adds the hardware's noise floor and hum [S1][DOC]. The hardware is a FET-based, feedback-detector peak limiter
(attack 20–800 µs, release 50 ms–1.1 s, ratios 4–20:1) whose "all buttons" mode moves the ratio between 12 and 20:1,
shifts bias and timing, and flattens the curve [S3][S4][DOC]. Her corpus holds **one** CLA-76 state (6 slots, all in
`fuentes drogas antiquadas`), and it is the **unmodified Waves factory preset "Start Me Up"** (`<LoadMenuCategory>
Factory`, `ReadOnly true`, active setup A) [DATA]. That makes this the easiest Waves role to match exactly: render
the official trial with that factory preset and fit. LYNX today plays a generic 2:1 compressor at −15 dBFS with
auto gain [DATA].

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves CLA-76 Compressor / Limiter (Chris Lord-Alge Signature series) | [S1][S2][DOC] |
| Version in her state | `PluginVersion 16.0.23.24`, sub-component `76CM`, preset `GenericType="1176"` | [DATA] |
| Hardware modelled | two UREI/Universal Audio 1176 units from CLA's studio, "Bluey" and "Blacky"; the guide speaks of a Revision B "Silverface Bluestripe" and a Revision D-LN "Blackface" (which name maps to which is not stated explicitly) | [S1][S2][DOC] |
| Components | mono / stereo (standard Waves) | [S1][DOC] |

**Hardware lineage.** UREI 1176 Peak Limiter, introduced 1967 (Rev. A, designed by Bill Putnam), the first true
solid-state peak limiter; gain reduction by a FET "in a feedback configuration"; attack < 20 … 800 µs, release
50 … 1100 ms, ratios 4, 8, 12, 20:1 [S3][S4][DOC]. Revisions [S3][DOC]: A (1967, blue/brushed face, serials 011–125),
AB, B (217–1078), **C (1970) = 1176LN** (Brad Plunkett's low-noise circuit, −6 dB noise, black face), **D**
(1239–2331) and **E** (2332–2611) — "reputed to sound best", F (1973: class-A output → push-pull), G, H; the UA
reissue (2000) is based on D/E. "Bluestripe" = the early blue-faced units; "Blackface" = Rev. C onward [S3][model].

**Her usage [DATA].** 412-project inventory: 6 slots, 6 projects, 1 state (`tmp/r113/wv/CLA-76_0.bin`, e.g. `fuentes
drogas antiquadas`). The same project also uses HG-2, Maag EQ4, Pro-Q 4 and Pro-DS — a one-off chain, not her vocal
preset. Her corpus also contains UADx 1176 slots (`luisa sonsa`, [round 107](../analise/REA_rodada107.md)) — a
different plugin, not covered here.

**Her state (RealWorld list, 106 values; setup A active, preset "Start Me Up") [DATA]:**

| index | Setup A ("Start Me Up") | Setup B (= Waves defaults) | meaning |
|---|---|---|---|
| 0 | 8.7 | 30 | Input (stored as positive magnitude of the dB setting; default −30) [DATA + S1; model for the sign] |
| 1 | 16.9 | 18 | Output (default −18) [same] |
| 2 | 4.3 | 3 | Attack knob 1…7 (default 3) [DATA + S1] |
| 3 | 4.31 | 4 | Release knob 1…7 (default 4) [DATA + S1] |
| 59 | 4 | 3 | differs — candidate Ratio index or Revision [model, unconfirmed] |
| 103 | 41.3 | 100 | differs — candidate Mix % (newer CLA-76 builds advertise a Mix control) [model, unconfirmed] |
| all others | identical in A and B | | model constants / switches (Analog, Revision, Meter …) [model] |

Indices 0–3 match the documented defaults exactly in setup B, so their meaning is solid; 59 and 103 need the probe
(§8).

## 2. Signal flow (hardware topology, as the plug-in models it)

```
in ─► Input attenuator (pot) ─► input transformer / stage ─► FET shunt attenuator (gain cell) ─► preamp
   ─► class-A output amp + transformer ─► Output pot ─► out
                       │
   output-stage signal ─► sidechain amp (gain set by Ratio network) ─► rectifier ─► RC (Attack/Release) ─► FET gate
```
The detector senses the **output** (feedback topology); Input sets how hard the fixed threshold is hit (no threshold
knob); Output only sets level [S3][S4][DOC], [model from the topology]. CLA-76 adds the modelled noise/hum (Analog)
and the Revision's gain-stage colour [S1][DOC].

## 3. Controls (CLA-76)

| Control | Range | Default | Law / behaviour | State index | Source |
|---|---|---|---|---|---|
| Input | −∞ … 0 dB (0.1 dB steps) | −30 | non-linear scale matched to the original; drives the gain cell | 0 | [S1][DOC], [DATA] |
| Output | −∞ … 0 dB | −18 | make-up/level | 1 | [S1][DOC], [DATA] |
| Attack | 1 … 7 | 3 | 1 = slowest (≈ 0.8–1 ms), 7 = fastest (< 50 µs) | 2 | [S1][DOC], [DATA] |
| Release | 1 … 7 | 4 | 1 = 1 s (slowest), 7 = 50 ms (fastest) | 3 | [S1][DOC], [DATA] |
| Ratio | 4:1, 8:1, 12:1, 20:1, All | n/d | All = "all buttons in" mode | ? (59) | [S1][DOC] |
| Meter | In, GR, Out | GR | display only | ? | [S1][DOC] |
| Analog | Off, 50 Hz, 60 Hz | 60 Hz | noise floor + mains hum of the original supplies | ? | [S1][DOC] |
| Revision | Bluey, Blacky | Blacky | gain stages, time constants, THD, noise | ? | [S1][DOC] |
| Mix (newer builds) | 0 … 100 % | 100 | parallel compression | ? (103) | [S2][DOC], [model] |

## 4. Known curves & behaviour (hardware, as documented)

- Attack 20–800 µs, release 50 ms–1.1 s (1176LN spec) [S4][DOC]; Waves: attack "under 50 µs" at 7, 1 ms at 1 [S1][DOC].
- Ratio buttons also change the threshold ("set higher on higher ratios") [S5][MEAS forum]; the knee is soft at 4:1
  and hardens with ratio [S3][DOC].
- **Program dependence** (per UA's Will Shanks, quoted in [S3]): the ratio is reached on transients and rises after
  them; release is faster after transients and slower after sustained heavy compression [S3][DOC].
- **All-buttons mode**: ratio between 12:1 and 20:1, internal bias points shift (attack/release change), curve
  "plateau-like", slight attack lag on initial transients ("reverse look-ahead") and audible distortion [S3][DOC].
- Distortion: FET channel resistance depends on V_DS → even-order distortion rising with gain reduction and with low
  frequencies (fast release ripples the gain at LF) [S6][DOC], [model]. Revisions differ in THD/noise [S1][DOC].
- Noise: Analog adds hiss + 50/60 Hz hum by default — audible on quiet passages and in renders [S1][DOC].

## 5. Published science

**FET as a voltage-controlled resistor.** In the ohmic region a JFET's channel behaves as a resistor set by V_GS
[S6][DOC]; the standard approximation is `r_DS ≈ r_DS(on) / (1 − V_GS/V_P)` for |V_DS| small (V_P = pinch-off)
[model, standard JFET ohmic-region law]. As a shunt element after a series resistor R_s the cell's gain is
`G = r_DS/(R_s + r_DS)`. The quadratic term of I_D(V_DS) causes distortion; feeding half of V_DS to the gate cancels
it to first order (classic linearisation) [S6][DOC], [model].

**Feedback compressor (static and dynamic).** Giannoulis, Massberg & Reiss [S7][PAPER]: feed-forward gain computer
`y_G = x_G` (x_G ≤ T), `T + (x_G − T)/R` (x_G > T), with a soft knee of width W:
`y_G = x_G + (1/R − 1)(x_G − T + W/2)²/(2W)` for |x_G − T| ≤ W/2. In a **feedback** design the detector sees the
output, so the effective static slope is 1/R only in the limit; with a detector gain k the steady state satisfies
`g = f(k·g·x)` — the effective ratio drops and the knee softens [S7][PAPER]. Smoothing: "decoupled" or "branching"
peak detector `y_L[n] = α_A y_L[n−1] + (1−α_A) x_L[n]` if x_L > y_L, else `α_R y_L[n−1] + (1−α_R) x_L[n]`,
α = exp(−1/(τ f_s)) [S7][PAPER]. Program-dependent release: add a second, slower RC stage that charges only during
long gain reduction (release = fast stage + slow stage weighted by how long GR lasted) [model].

## 6. What LYNX does today

`[waves-compressors]` (`CLA-76; CLA-3A; C1 comp; SSLComp; API-2500`) → **Compressor**, preset `Threshold=0.75;
Ratio=0.0526; Auto gain=1` → −15 dBFS, 2:1, attack ≈ 10 ms, release ≈ 120 ms (defaults), feed-forward hard-knee peak
detector, linked stereo, slow (1.5 s) in/out power matching ±12 dB [DATA, `src/core/Effects.cpp`,
`PluginSubstitutes.cpp`]. No translation of the CLA-76 state; no measurement of CLA-76 (the round-113 Compressor
fit, `+2.2 +0.5 +0.1 −0.2 −0.2 +0.3 +0.9 +0.9` dB, is for **RComp**, [round 113 §3](../analise/REA_rodada113.md)).
Differences that matter: µs attack vs 10 ms, 4–20:1 vs 2:1, feedback vs feed-forward, no FET colour, no hum/noise.

## 7. Implementation guidance for LYNX

Priority: **low** for her catalogue (one project, 6 slots) but **cheap to get exactly right** (one factory preset).

1. **Measure "Start Me Up" on the official trial** (tools/fxmeasure; load the factory preset by name, or set the
   four known values + probe 59/103) — §8 items 1–4. Store the target curves.
2. **Build an "FET 1176" mode** (in Compressor, or a small `Fet76` effect):
   - Input gain `g_in` (dB law from §8), detector on the **output** of the gain cell (feedback), sidechain gain per
     ratio (threshold rises with ratio), soft knee per ratio fitted to the static curves;
   - attack τ_A from the 1…7 law (20 µs … 0.8 ms, log-spaced unless §8 shows otherwise), release two-stage
     (50 ms … 1.1 s fast stage + slow stage) fitted to step responses;
   - All mode: separate fitted curve + slower attack;
   - gain-cell colour: `y = x·G + c₂·G(1−G)·x²` (even harmonic growing with GR) + output-stage soft clip; fit c₂ to
     THD vs GR at 100 Hz / 1 kHz;
   - Analog: add −xx dBFS pink-ish noise + 50/60 Hz hum with harmonics at the measured level (or omit and document:
     hum in a substitute is rarely wanted — but "identical" requires it; make it a switch, default per measurement);
   - Mix (if index 103 is Mix): dry/wet with latency-aligned dry.
3. **Translate** `waves-cla-76`: indices 0–3 (+59, +103 once confirmed) → the new mode.
4. **Pass criteria:** static GR curve within 0.5 dB (−40…0 dBFS, 1 kHz) for her setting; attack/release envelope
   (−30 → −10 → −30 dBFS bursts) within 10 % in time constants; THD within 2 dB at 3 levels; level-normalised
   octave-band error ≤ 0.5 dB RMS on the `fuentes drogas antiquadas` stem; noise floor within 2 dB.

## 8. Open questions to measure (official trial, black-box only)

1. Render "Start Me Up" (setup A) vs her saved state: confirm they are identical (null test) → confirms "unmodified
   factory preset".
2. Probe indices 59 and 103 (and the switch indices 4–9): change Ratio, Revision, Analog, Meter, Mix one at a time,
   save, diff the RealWorld list (the round-113 §3c method).
3. Input-dB → GR law: 1 kHz stair −50…0 dBFS at Input −30/−20/−10/0, each Ratio + All, Attack 7 / Release 7 and
   Attack 1 / Release 1, both Revisions.
4. Time constants: bursts −40 → −10 dBFS at every Attack 1…7 and Release 1…7; sustained-GR test (4 s burst) for
   release program dependence.
5. THD vs GR (100 Hz, 1 kHz, 5 kHz) per Revision; All-mode distortion.
6. Analog off / 50 / 60 Hz: noise spectrum and hum level with silence in.
7. Latency (impulse), oversampling (alias test with a 15 kHz sine at fast settings).

## 9. Sources

1. Waves, *CLA-76 Compressor / Limiter User Guide* — https://www.waves.com/1lib/pdf/plugins/cla-76-compressor-limiter.pdf (vendor PDF; automated fetch refused — facts taken from the search excerpt of that PDF and the identical mirrors https://manualmachine.com/wavesaudio/cla76/6482905-user-guide/ and https://manuals.plus/waves/cla-76-compressor-limiter-plug-in-manual) — accessed 2026-10-21 — [DOC]
2. Waves, CLA-76 product page and "Intro to the CLA Classic Compressors" (models CLA's hand-picked Bluey/Blacky units; Mix for parallel compression) — https://www.waves.com/plugins/cla-76-compressor-limiter , https://www.waves.com/intro-to-the-cla-classic-compressors — accessed 2026-10-21 (search-result text) — [DOC]
3. "UREI 1176 Peak Limiter", History of Recording (revision table A–H, LN, all-buttons mode, program dependence per W. Shanks/UA) — https://www.historyofrecording.com/Urei_1176.html — accessed 2026-10-21 — [DOC]
4. UREI / Universal Audio, *1176LN Solid State Limiting Amplifier* manual (attack 20–800 µs, release 50 ms–1.1 s) — https://c3.zzounds.com/media/1176LN_manual-1a90c9dff91ec30316551923e877c4d0.pdf — accessed 2026-10-21 (figures via search excerpt; fetch refused) — [SCHEMATIC]
5. KVR forum thread "New Toneboosters Compressor" (1176 threshold rises with ratio) — https://www.kvraudio.com/forum/viewtopic.php?p=7812080 — accessed 2026-10-21 — [MEAS] (forum, low weight)
6. Siliconix / Vishay, AN105 *FETs As Voltage-Controlled Resistors* — https://www.vishay.com/docs/70598/70598.pdf — accessed 2026-10-21 — [DOC]
7. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", *J. Audio Eng. Soc.* 60(6):399–408, 2012 — https://www.aes.org/e-lib/browse.cfm?elib=16354 — accessed 2026-10-21 — [PAPER]
8. LYNX: her state `~/.claude/jobs/f8789b49/tmp/r113/wv/CLA-76_0.bin`; `src/core/PluginSubstitutes.cpp` (`[waves-compressors]`), `src/core/Effects.cpp` (`class Compressor`, laws at lines 88–91); [round 113](../analise/REA_rodada113.md) — [DATA]
