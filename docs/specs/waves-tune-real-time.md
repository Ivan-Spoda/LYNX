# Waves Tune Real-Time: spec dossier

LYNX substitute: **LYNX Tune** (`src/core/LynxTune.cpp`; translation `translateWavesTune` / `wavesTuneScale` in
`src/core/PluginSubstitutes.cpp`). Written for round 125 (spec125). Clean-room: public Waves user guides, her
project files, and our own black-box measurements of the official trial (round 120). No Waves binary was opened,
decompiled or instrumented.

Tags: [DOC] vendor manual · [PAPER] · [MEAS] measurement (ours = REA doc link) · [DATA] her project files ·
[model] our inference (basis given). Inline refs `[Sn]` point to §9. All sources were accessed on 2026-10-21.
For the shared pitch-correction science (YIN, PSOLA, phase vocoder, Hildebrand patent), see
[auto-tune-pro.md §5](auto-tune-pro.md).

---

## 1. Identity

| | |
|---|---|
| Vendor / product | Waves Audio, **Waves Tune Real-Time**: real-time vocal pitch correction for studio and live use (SoundGrid / eMotion / MultiRack compatible) [S1 p.4][DOC] |
| Components | **Mono** and **Stereo**. For a stereo source, detection uses the sum (L+R)/2 [S1 p.5][DOC] |
| Versions | Her states save `PluginVersion 16.0.23.24` [DATA]. Measured trial: WaveShell1-VST3 **17.1**, "Waves Tune Real-Time Mono" (in our host it only loads after a dummy DeEsser) [MEAS][S4 §2] |
| Approach | "formant-corrected pitch shifting with smooth, controlled note transitions". Natural vibrato "can be preserved or manipulated" [S1 p.4][DOC] |
| Latency | "reports zero latency to any host … The latency is dynamic, from 0 ms to 4 ms, based on the pitch/frequency of the current input note" [S1 p.5][DOC] |

**Her usage** [DATA]:
- The round-113/120 scan of her main projects found **11 distinct states (59 slots)** (`tmp/r113/wv/Waves_Tune_Real-Time_*.bin`)
  [S4 §1]. The whole-laptop inventory lists only **6 slots / 6 projects / 1 state** under the "Waves Waves Tune
  Real-Time" name (`tmp/r113/inventory_all.txt`). The two counts use different matching (shell sub-plugin vs name),
  **to reconcile**. In `tmp/gf/proj2`, only `luisa sonsa` uses it (insert 1).
- Active scales (SETUP_A) [S4 §1]:

| States | Scale (from `<PluginSpecific TagName="Scale">` XML) | RealWorld [20] Speed (ms) | [21] Note Transition (ms) |
|---|---|---|---|
| 0,5,6,7,8 | Natural Minor, root E | 16.1 – 31.7 | 20.9 – 54.2 |
| 1,3 | Major, root F | 16.1 | 54.2 |
| 2,10 | Natural Minor, root G# | 42.7 | 16.2 |
| 4 | Natural Minor, root B | 0.1 | 0.1 (hard-tune) |
| 9 | Major, root C | 21.4 | 54.2 |

- SETUP_B of every state is untouched: Speed 15, Note Transition 120 = the documented defaults [S1 pp.8–9].
- RealWorld [22] = 100 in every state, which is probably Correction % [model: matches its 100 % default and
  range]. Vibrato on/off and depth indices are **not yet identified** (§8).
- `luisa sonsa` uses E natural minor, Speed 21.4, Note Transition 54.2 ms. This independently confirms that the same
  project's Auto-Tune Pro "E, 1" means E minor [S4 §1].

## 2. Signal flow

```
 in (mono; stereo: detection on (L+R)/2)
   │
   ├──► pitch detector (dynamic delay 0–4 ms, longer for low notes)  →  "Range": notes outside
   │      the keyboard range → bypassed (no correction)
   │          │ detected pitch p
   │          ▼
   │    target: nearest Legal note of Root/Scale/keyboard statuses (Legal, Illegal, Illegal-Up,
   │      Illegal-Down, Bypass) or MIDI "Target Pitch";  Tolerance (Cents, Time) delays a target change
   │          │ target T
   │          ▼
   │    correction: Note Transition (between notes) and Speed (within a note), Vibrato on/depth
   │      (keep / scale the natural FM), Correction % (fraction of T − p applied)
   │          ▼
   └──► formant-corrected pitch shifter (or "Non-Corrected" = resampling: formants move)
              ▼
            out (no added latency reported)
```

No EQ, dynamics or saturation is documented. The meters cover −28 … 0 dBFS only [S1 p.13][DOC]. MIDI
"Reference Tone" mode adds a tone generator (±18 dB) only when you play notes [S1 p.21][DOC].

## 3. Controls

Ranges and defaults from the v16 user guide [S1] (identical in the older guide [S2]). Tapers were read from the
trial's parameter texts at 2001 normalised steps (`C:\Users\kauan\lynx-test\tune120\t_speed.tsv`, `t_nt.tsv`,
`t_vd.tsv`, `t_tc.tsv`, `t_corr.tsv`, `t_st.tsv`) [MEAS]. State mapping from [S4 §1–2].

| Control | Range | Unit | Default | Law / taper (normalised v) | Audible effect | State |
|---|---|---|---|---|---|---|
| Speed | 0.1 – 800 | ms | 15 | Piecewise linear [MEAS]: v 0–0.5 → 0.1–50 ms; 0.5–0.666 → 50–120; 0.666–1 → 120–800 (e.g. v .25 = 25.1, .75 = 291, .9 = 596.4). The guide calls it "logarithmic" [S1 p.8] | "correction speed … also defines the tightness in correcting a sustained pitch that's drifting out of tune" [S1 p.8][DOC]. **Measured: no effect on sung-note step responses between 5 and 400 ms (§4.1)** | RealWorld [20] |
| Note Transition | 0.1 – 800 (0.1 ms steps) | ms | 120 | same taper as Speed | "When a new pitch is introduced … how fast correction will be applied". Fast = jumpy/quantised, slow = keeps glissando [S1 p.9][DOC]. **Measured: governs all corrections (§4.1)** | RealWorld [21] |
| Link | Off/On | — | Off | — | Ties Speed and Note Transition together [S1 p.9] | ? |
| Tolerance Cents | Off – 40 | cents | Off | linear (v .25 = 10 c, .5 = 20 c) | Added to the **50 c base threshold** for a target change (Western scales) [S1 p.10][DOC] | ? |
| Tolerance Time | Off – 300 | ms | Off | — | Time beyond the 50 c threshold before the transition starts [S1 p.10][DOC] | ? |
| Vibrato On/Off | Off/On | — | **On** | — | On: Note Transition does not affect natural vibrato. Off: "fast … settings will result in a fast pitch quantization effect" [S1 p.11][DOC] | ? |
| Vibrato Depth | 0 – 200 | % | 100 | linear (v .5 = 100 %) | 100 % preserves vibrato "even at fast Note Transition"; < 100 reduces, 0 flattens, > 100 exaggerates. Amplitude modulation is untouched. Also covers "flutter in the voice" [S1 p.11][DOC] | ? |
| Correction On/Off | On/Off | — | On | — | Global correction switch [S1 p.12] | ? |
| Correction % | 1 – 100 | % | 100 | linear | Fraction of the detected correction applied (40 c sharp at 50 % → 20 c down) [S1 pp.12–13][DOC] | [22]? |
| Formant Correction | Corrected / Non-Corrected | — | Corrected | — | Non-corrected = "simple sample rate conversion" (chipmunk/darker) [S1 p.13][DOC] | ? |
| Reference Pitch | ±100 c (415.3 – 466.16 Hz) | Hz | 440 | — | Tuning offset of the grid [S1 p.14][DOC] | ? |
| Root | 17 names (C, C#, Db, …) | — | C | — | Root of the scale [S4 §2] | `<Scale>` XML `<Root>` |
| Scale Type | 44 entries: User Scale, Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor (Asc), Melodic Minor (Asc+Dec), Major/Minor Justonic, Dorian, Mixolydian, Blues, Phrygian, Locrian, Lydian, Whole Tone, Diminished (W-H/H-W), Bebop, pentatonics, Pythagorean, Maqamat (Rast, Saba, Bayati, Nahawand, Hijaz, Kurd, Huzam), Hindustani/Carnatic thaats … [MEAS t_st.tsv] | — | Chromatic (guide) | — | Defines the correction grid and note statuses [S1 p.14][DOC] | RealWorld [5] (0 User, 1 Chromatic, 2 Major, 3 Natural Minor), plus the `<Scale>` XML with 12 `NoteStatus` values (`0` = legal, `x` = illegal) |
| Range | keyboard span | notes | — | — | Notes outside the range are **bypassed** [S1 p.15][DOC] | ? |
| Keyboard note status | Legal / Illegal / Illegal-Pitch-Up / Illegal-Pitch-Down / Bypass | — | from scale | — | Up/Down force the direction of retargeting [S1 p.16][DOC] | `<Scale>` XML |
| Group Octaves, Send Scale to All, Receive Key | — | — | — | — | Workflow; Receive Key also accepts Waves Key Detector [S1 p.17] | — |
| MIDI: Reference Tone / Target Pitch | — | — | — | — | Target Pitch: MIDI note forces the target, using the Speed/Note Transition timing [S1 p.21][DOC] | — |

## 4. Known curves & behaviour

### 4.1 Measured correction law (official trial) [MEAS][S4 §2]
Test: harmonic "voice", held notes 30 c sharp, 48 kHz, chromatic unless noted. t50 / t90 of the correction:

| Note Transition (ms) | 0.1 | 20.9 | 54.2 | 120 | 250 |
|---|---|---|---|---|---|
| From silence (ms) | 30/30 | 30/64 | 50/144 | 96/298 | 188/590 |
| Mid-note drift (ms) | 14/40 | 20/50 | 42/120 | 88/278 | 172/574 |
| Vibrato kept (5.5 Hz, ±40 c) | 45 % | 57 % | 88 % | 97 % | 99 % |

- Law: **first-order pull with τ = Note Transition, plus ~12 ms detection**: **t50 ≈ 12 ms + 0.69·NT,
  t90 ≈ 12 ms + 2.3·NT**.
- **Speed 0.1 … 400 ms (NT 120) changes none of these.** Only below ~5 ms does vibrato retention drop from 97 % to
  82 %.
- Instant note changes are followed at once. A sung slide passes through. On an octave glide the correction averages
  **−3 c**. Notes exactly between two targets go to the **lower** one.
- Interpretation [model]: the guide says Speed handles in-note drift and NT handles note changes [S1 pp.8–9]. Black-box,
  NT dominates every case tested, including mid-note drift. Speed may only act on very small or very fast
  deviations (flutter), which matches its effect on vibrato below 5 ms.
  - Possibly the vibrato path (Vibrato On) separates fast FM from the correction and Speed applies to that path.
    **Vibrato state during the measurement is not recorded (§8).**
- Vibrato retention vs a one-pole high-pass with τ = NT: ωτ/√(1+(ωτ)²) at 5.5 Hz gives 0.59 (20.9 ms), 0.88
  (54.2), 0.97 (120). Measured: 57 / 88 / 97 %, an excellent match [model: arithmetic]. So from NT ≈ 20 ms up, the
  natural vibrato goes through the same one-pole as everything else.
  - **The exception is NT 0.1 ms**: the one-pole predicts ~0 %, yet 45 % of the vibrato survives (Auto-Tune at R 0
    keeps 3 %).
  - So below ~20 ms something else limits how fast *vibrato* is corrected. Candidates: detector smoothing or lag at
    5.5 Hz, the Vibrato-On path, or Speed. A one-pole with τ = Speed (default 15 ms) gives ωτ = 0.52 → 46 %
    retention, very close to the 45 % measured.
  - A plain τ = max(NT, Speed) is **ruled out**: at NT 120, Speed 200 / 400 did not slow the step response [S4 §2].
  - Speed therefore probably acts only on fast in-note deviations (vibrato/flutter), as the guide says ("tightness
    in correcting a sustained pitch"). Steps at NT 120 never exercised it.
  - Her states with NT < Speed (2/10: NT 16.2, Speed 42.7; 7: NT 20.9, Speed 31.7) are where this could matter
    (§8.1).

### 4.2 Target selection and tolerance [DOC]
- The base switch threshold is **±50 c** from the current note (Western scales). Tolerance Cents adds 0–40 c of
  hysteresis. Tolerance Time requires the excursion to last 0–300 ms. "Until the Tolerance threshold is met, the
  (internally detected) target pitch doesn't change" [S1 pp.9–10].
- With Tolerance Off (as in her states [model: no RealWorld value identified, defaults Off]), retargeting is
  immediate at the midpoint. Measured: the exact midpoint goes to the lower note [S4 §2].
- Illegal-Up / Illegal-Down statuses force the direction. Bypass passes uncorrected [S1 p.16].

### 4.3 Vibrato [DOC]
- Vibrato On + Depth 100 % keeps natural vibrato "even at fast Note Transition values". Depth scales the FM
  (0 – 200 %). AM is untouched [S1 p.11].
- Quick-start advice for the quantised effect: Vibrato Off (or On with Depth 0 for wide vibratos), Speed and NT at
  minimum [S1 p.6]. Her state 4 (Speed = NT = 0.1 ms, B minor) is that hard-tune setting.

### 4.4 Formant [DOC]
- Default **Corrected** ("formant-corrected pitch shifting"). Non-Corrected = sample-rate conversion [S1 p.13].
  Her states' Formant flag index is unknown. Assume Corrected (the default) until identified.

### 4.5 Latency [DOC]
- Reports **0** to the host. The real delay is **0 – 4 ms, depending on input pitch** (longer for low notes) [S1 p.5].
  In FL this delay is therefore *not* compensated. The vocal comes out up to ~4 ms late relative to the other
  tracks.
- Round 120 did not measure the delay [S4]. LYNX Tune instead reports ~25 ms and is compensated, so LYNX's vocal is up
  to 4 ms *earlier* than FL's [model]. This is inaudible on a single vocal and matters only against a parallel dry
  copy.

## 5. Published science (short)
- Waves publishes no algorithm description. The "formant-corrected" pitch shifter plus a pitch-dependent 0 – 4 ms
  delay (about one period of the lowest note: 4 ms = 250 Hz period × 1) suggests a **time-domain, pitch-synchronous
  shifter (PSOLA-like) with a one-period look-ahead** [model, from the latency statement]. A phase vocoder would need
  a fixed frame latency [S3][PAPER].
  - 4 ms is only one period at 250 Hz. Lower notes (100 Hz = 10 ms period) cannot be analysed within 4 ms, so the
    detector must work on past samples with near-zero look-ahead [model].
- TD-PSOLA equations and formant preservation: [S3][PAPER] and [auto-tune-pro.md §5.3](auto-tune-pro.md).
- Pitch detection (YIN difference function): [S5][PAPER].
- First-order correction smoothing: the same "Decay" structure as Hildebrand's patent [S6][PATENT], see
  auto-tune-pro.md §5.1.

## 6. What LYNX does today

- **Translation** (`translate = waves-tune`): the active setup's Root and NoteStatus list become LYNX Tune's key
  and scale (named LYNX scale if the notes match, else the custom scale with the same note switches). The state also
  votes for the project key. **Note Transition → LYNX retune (ms, τ = NT)**, humanize 0. Speed, Correction,
  Tolerance and Vibrato are not translated [S4 §1–2][S7].
- **Engine refit (round 120)** [S4 §2b]: each phrase starts at the sung pitch (the previous phrase's correction is no
  longer carried over), and glide hysteresis dropped from 0.1 to 0.03 st (glide bias −15 c → −8 c, Waves −3 c).
  - LYNX at retune = NT: 52/134 ms at 54.2 (Waves 50/144), 98/284 at 120 (96/298), 185/570 at 250 (188/590).
  - Drift 48/126 vs 42/120. Vibrato 87 % vs 88 % (54.2), 58 % vs 57 % (20.9).
- **Her takes** (`luisa sonsa`, 3 takes, 37 s, 2195 voiced 10 ms frames), |pitch − Waves|:

| LYNX Tune as translated | median | mean | frames > 20 c |
|---|---|---|---|
| Before (project key, 50 ms, humanize 50 %) | 6.9 c | 13.2 c | 15.3 % |
| **Now** (her scale, retune 54.2 ms, refit engine) | **3.4 c** | **10.1 c** | **10.4 %** |
| No correction | 18.5 c | 22.4 c | 46.0 % |

## 7. Implementation guidance for LYNX (ordered)

1. **Keep NT → τ.** The law t50 ≈ 12 + 0.69·NT and t90 ≈ 12 + 2.3·NT is already matched within ~10 %.
   - Open refinement [model, §4.1]: Speed seems to set how fast in-note vibrato or flutter is corrected when NT is
     very short (45 % of the vibrato is kept at NT 0.1 with Speed 15).
   - Don't model it until §8.1 is measured: Speed 0.1 / 42.7 at NT 16.2, on vibrato and on drift. That is her
     states 2/10.
2. **Onset from silence: add the ~12 ms detection offset explicitly.** Waves' from-silence t50 = 30 ms at
   NT ≤ 20.9, i.e. the first correction lands about 30 ms after the onset [S4 §2]. Check LYNX's first-voiced-frame
   delay against this.
   - Pass: from-silence t50/t90 within ±5 ms at NT 0.1 / 20.9 / 54.2.
3. **Glide bias −8 c → −3 c.** Most of the remaining bias is detector lag on moving pitch [S4 §2b]. Use the same fix
   as for Auto-Tune (auto-tune-pro.md §7 item 2): a shorter analysis window or slope-based prediction.
   - Pass: octave-glide mean correction −3 ± 2 c.
4. **Midpoint rule.** Waves sends a note exactly between two targets to the lower one [S4 §2]. In LYNX's
   `vocal::nearestNote`, break ties downward.
5. **Tolerance (if her states use it; §8.3).** Add a "switch threshold" = 50 c + Tolerance Cents and a "hold time"
   = Tolerance Time before `target` changes. Both are simple additions to LYNX's hysteresis.
6. **Vibrato On/Depth (if her states use it; §8.3).** Model [model]: split p(t) into a slow note component (one-pole
   at NT) and a residual FM r(t) = p − LP_NT(p). Correction = LP_NT(T − p_slow) + (1 − depth)·r(t). With depth = 1
   the vibrato passes untouched even at NT 0.1. Calibrate on mksig's vibrato with Vibrato On at NT 0.1 / 20.9.
7. **Correction %** (100 in all her states): multiply `wanted` by Correction/100. Trivial, and makes the translation
   complete.
8. **Formant Non-Corrected** → LYNX formant preserve Off.
9. **Latency.** No change needed. Optionally offer a "report 0" mode for live use, but PDC alignment is more
   correct.
10. **Test plan.** Reuse `tmp/tune120/py/mksig.py`, `wtrun.py`, `ana.py`, `take.py`.
    - Pass: step t50/t90 ±10 %, vibrato ±5 points, glide bias ±2 c.
    - Her takes: median ≤ 3 c, frames > 20 c ≤ 8 %.

## 8. Open questions to measure (official trial, black-box)

1. **Speed's real role**: Speed 0.1 / 15 / 42.7 / 400 at NT 0.1 and at NT 16.2, on (a) the 5.5 Hz ±40 c vibrato,
   (b) a ±10 c 8 Hz flutter, (c) a 2 Hz slow drift, (d) the held-note step. Round 120 swept Speed only at NT 120,
   on steps and vibrato only. Decides whether Speed is a second, in-note time constant that LYNX must model.
2. **Vibrato state of the round-120 measurement and of her states**: toggle Vibrato and Depth in the trial, save
   states, and diff the RealWorld lists to find their indices (and those of Tolerance, Correction %, Formant,
   Range, Link). Then read her 11 states.
3. **Vibrato On behaviour**: Depth 0 / 50 / 100 / 200 % at NT 0.1 and 54.2 on a 5.5 Hz ±40 c vibrato. Decides the
   §7.6 model.
4. **Tolerance**: Cents 20 and Time 100 ms on a note crossing the midpoint. Confirms the 50 c + Cents rule and the
   timer semantics.
5. **Actual delay**: cross-correlate input and output on 110 / 220 / 440 Hz voiced tones. Confirms the documented
   0 – 4 ms dynamic delay and whether it varies within a phrase (pitch-dependent delay = a small time-warp).
6. **Formant Non-Corrected spectrum**: the envelope shift on a ±2 st correction. Decides whether plain resampling
   (LYNX formant = ratio) is equivalent.
7. **Range bypass**: notes below or above the keyboard range pass uncorrected. Do her states narrow the range?
8. **Inventory discrepancy**: 11 distinct states (r113/wv) vs 1 state in inventory_all. Re-run on the same root.

## 9. Sources

1. Waves, *Waves Tune Real-Time User Guide* (v16 update) — https://assets.wavescdn.com/pdf/plugins/tune-real-time-v16-update.pdf — accessed 2026-10-21 — [DOC]
2. Waves, *Waves Tune Real-Time User Guide* (earlier edition) — https://assets.wavescdn.com/pdf/plugins/tune-real-time.pdf — accessed 2026-10-21 — [DOC]
3. E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones", Speech Communication 9(5–6):453–467, 1990 — https://doi.org/10.1016/0167-6393(90)90021-Z — accessed 2026-10-21 — [PAPER]; J. Laroche, M. Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects", IEEE WASPAA 1999 — https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf — accessed 2026-10-21 — [PAPER]
4. LYNX REA round 120 §1, §2, §2b, §4 (states, measurements, refit, takes) — [../analise/REA_rodada120.md](../analise/REA_rodada120.md); raw: `~/.claude/jobs/f8789b49/tmp/tune120/py/` (mksig, wtrun, ana, take), `C:\Users\kauan\lynx-test\tune120\t_*.tsv`, `w_*.wav` — [MEAS]/[DATA]
5. A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4):1917–1930, 2002 — https://cagnazzo.wp.imt.fr/files/2013/05/2002_JASA_YIN.pdf — accessed 2026-10-21 — [PAPER]
6. H. A. Hildebrand, US 5,973,252 "Pitch detection and intonation correction apparatus and method" — https://patents.google.com/patent/US5973252A/en — accessed 2026-10-21 — [PATENT]
7. LYNX source and substitute table: [../../src/core/LynxTune.cpp](../../src/core/LynxTune.cpp), `src/core/PluginSubstitutes.cpp` (`translateWavesTune`, `wavesTuneScale`), [../PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md) — repo
8. Her states: `~/.claude/jobs/f8789b49/tmp/r113/wv/Waves_Tune_Real-Time_*.bin`; inventory `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`; round 113 context [../analise/REA_rodada113.md](../analise/REA_rodada113.md) §1, §4b — [DATA]
