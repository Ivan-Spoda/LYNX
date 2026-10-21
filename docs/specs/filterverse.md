# Polyverse Filterverse — spec dossier (no LYNX role yet)

Round 125 (spec125). Clean-room dossier built from the vendor's user manual (Filterverse User Manual V1.2.0, public
PDF on polyversemusic.com), press reviews, the published virtual-analog filter literature and her saved project data
(the plugin state is readable JSON). No Polyverse binary was opened, decompiled or instrumented.
Tags: [DOC] vendor document / press · [PAPER] academic/textbook · [MEAS] measurement · [DATA] read from her projects or
LYNX files · [model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** Filterverse is **Polyverse Music's** multi-filter plug-in (public beta March 2024, release
late 2024): three filter slots (≈ 25–26 filter models each: state-variable, ladder, Sallen-Key, Butterworth, Chebyshev,
EQ, multi-peak, phaser, comb, plus "non-filters" such as reverb, decimator, cabinet and tempo delays), five
serial/parallel routings, per-filter In (+18 dB drive) / Out / Dry-Wet / Phase / Pan / Mid-Side, eight modulation slots
(ADSR, sequencer/LFO, oscillator, envelope follower, random, pitch detector, MIDI/CV, Meta Knob), master Dry/Wet, Main
Out and an output clipper (Off / Clip / Soft Clip) [S1][DOC]. Her corpus holds it in **695 slots / 398 projects** of
the 412-project inventory, and **2017 of all 2028 slots (whole copy) carry the very same parameter block: the factory
"Init" preset, untouched** (one project, "yunk", differs; ten "WIU" autosaves differ only in a UI value) [DATA].
Per the manual, Init = **three Swiss Army Knife state-variable filters in series, lowpass, 2-pole, Cutoff 0 semitones
(12 o'clock), Resonance 0, Drive 0** [S1 p.44][DOC] — i.e. potentially a steep low-pass around 500–660 Hz on whatever
bus it sits on. Whether that is what she hears depends on things the state does not tell us in clear: the plugin
(and FL slot) on/off, the master and per-filter Wet, and FL automation. Note also: on her current laptop Filterverse
is **not installed**, so FL itself skips it today [DATA REA round 121; gf/PLUGIN_QUEUE.md]. §7–§8 resolve this before
anything is built.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Polyverse Music Inc., Filterverse ("An endless universe of filtering possibilities") | [S1 p.7–8][DOC] |
| Version she uses | `"softwareId": "Filterverse", "softwareVersion": "1.0.1"` in every state | [DATA] |
| Manual | User Manual V1.2.0 (137 pp.), written by Stephen Fortner; factory presets by Assaf Dar Sagol, Erez Eisen, Navi Retlav, Pandocrator, Databroth; thanks to Julius O. Smith (Fractazer) and Geraint Luff (BarberPole) | [S1 p.1, p.7][DOC] |
| Release | public beta announced 21 Mar 2024 ($79; beta adopters get 1.x free) [S4]; release news 27 Nov 2024 ($99 intro, $149 list; "25 filters") [S5]; SOS review Apr 2025 ("26 filter types", $149; "Cubase reported only 0.2ms latency") [S2]; Rekkerd review Feb 2025 [S3] | [S2–S5][DOC] |
| Formats | VST2, VST3, AU, AAX (Win/macOS) [S5]; her projects use the VST3 `Polyverse Music\Filterverse.vst3` | [S5][DOC], [DATA] |
| Hardware lineage | none emulated: "our implementations do not attempt to recreate or clone classic hardware versions of these filters"; topologies named: state-variable (Oberheim SEM / Matrix-12 popularised), Moog ladder, Sallen-Key, Butterworth, Chebyshev | [S1 p.59–66][DOC] |

### Her usage [DATA]

- Inventory (412 main projects): **695 slots, 398 projects, 1 distinct state**. Whole copy incl. autosaves
  (`tmp/vm124/inv.txt`): 2028 slots, 112 main slots in 36 main projects, 97 distinct state blobs.
- The 97 blobs differ only in `dateModified` and the FL wrapper bytes; their `"parameters"` blocks fall into three
  groups (by slots): **2017 slots identical**, 10 slots ("WIU" autosaves) differing only in `" Expanded Modslot"`
  (a UI value), 1 slot ("yunk.flp") with a different block.
- Every state's preset header: `"name": "Init", "author": "Polyverse Music", "description": "This preset has all
  parameters reset to their initial values … This preset cannot be edited."`, `"dateCreated": 1703291174`.
- Where it sits: in her template's FX insert fed by sends, together with H-Delay, Doubler2, Reeverb 2, Stereo Enhancer,
  Decapitator ([REA round 107 §1](../analise/REA_rodada107.md)).
- On her current laptop FL lists Filterverse as missing ([REA round 121](../analise/REA_rodada121.md); queue note
  "NOT installed", `gf/PLUGIN_QUEUE.md`).

### The saved state [DATA]

FL's VST3 wrapper record holds JUCE's `VstW`/`CcnK`/`FBCh` header with plugin code `PvFv`, then ≈ 101 KB of UTF-8
JSON, then `JUCEPrivateData` with Bypass. JSON keys: `name, id, author, description, softwareId, softwareVersion,
dateCreated, dateModified, tags, parameters (1931 entries), metadata (8 Meta-Knob names), Lock-.Power Btn, Lock- Wheel
Up, Lock- Wheel Down, Lock-.Panic, presetMIDIAssignments, Microtuning-Name ("Chromatic"), Microtuning-Description,
Microtuning-Root-Note (69), Microtuning-Root-Freq (440.0), Microtuning-Midi-Table (packed), pluginPrefs`.

Parameter families (1931 keys): globals `" Clipping Style", " Expanded Modslot", " Filter Order", " Filter Routing",
" Main Out", " Selected Tab", " Voice Glide", " Voice Retrig", " Wet", " Wheel Down", " Wheel Up", ".Panic",
".Power Btn"`; per filter F1–F3: `Bandwidth, Bipolar, Cutoff, Filter In, Filter Out, Filter Wet, Mid Side Mode, Pan,
Phase, Poles, Resonance, Type, Filter Power, Selected Filter`, each modulatable target with `ModN Left/Right` and
`Mod N Stereo` (8 slots); per modulator M1–M8: Meta (Knob, Glide, Polarity, Quantize), Osc (Coarse, Fine, Formant,
Glide, MIDI, Pan Send, Partial, Phase, Retrigger, Send, Shape, Table), Rand (Division, Glide, Quantize, Range, Rate,
Retrig, Rhythm, Sync, Unipolar), Seq (Length, Pattern, Rate, Retrig, Shape, Swing, Sync, Unipolar, Step1–16), Envelope
A/D/S/R/Vel Sens, Follower A/R/Thresh/Input Source, Midi Glide/Source, Mod. Power, Selected Modulator; pitch detector
`MG Pitch Detector Boost/Glide/Input Src./Range`; `MG Main Out` and `MG Wet` modulation. The names map to the manual's
Slider A = Type, B = Poles, C = Bandwidth ("wild card": drive, depth…), D = Bipolar (spread/width/gain) [model, from
S1 p.34 and the key names].

**The stored values are not plain normalised settings** [DATA, model]: in the Init block, two-state controls read
e.g. `F1 Filter Power` 0.404, `M1 Mod. Power` 0.983, `" Wet"` 0.772, `F1 Cutoff` 0.724, and the "yunk" block differs
in 1926 of 1931 values — not what a default preset with a few edits looks like. Only four values look literal
(`" Selected Tab"` 0, `" Voice Glide"` 1, `" Wheel Down"` 0.5, `" Wheel Up"` 0.0833 = 1/12). So the values cannot be
read as settings without the plugin; we did **not** try to decode them. The preset name + "cannot be edited" + 2017
identical blocks are, however, strong evidence that her slots are the factory Init [model].

---

## 2. Signal flow (Init configuration in bold)

```
in L/R ─┬───────────────────────────────────────────────────────── master dry ──────────────┐
        │                                                                                  (Master Dry/Wet)─► [Clip: Off/Clip 0 dB/Soft]─► Main Out ─► out
        └─► routing of 3 filter slots (tab order = position):  **serial: tab1 → tab2 → tab3**      ▲
              each slot:  Filter In (+18 dB max) ─► [M/S encode if MS] ─► filter model             │
                          (Cutoff, Resonance, Slider A–D; most models saturate internally)          │
                          ─► [M/S decode] ─► Phase (wet polarity) ─► Dry/Wet (per filter) ─► Pan ─► Filter Out
              **each slot = Swiss Army Knife, lowpass, 2-pole, Cutoff 0 st, Resonance 0, Drive 0**
        modulation: 8 slots (ADSR, Seq/LFO, Osc, Env follower [in/sidechain/out], Random, Pitch detect, MIDI/CV, Meta)
```

Sources: routing schemes and tab-order rule [S1 p.27–28]; Filter In/Out, Dry/Wet, Phase, Pan, M/S [S1 p.29–31,
p.135]; master Dry/Wet, Main Out after everything and independent of the master mix, clipper "upstream of the output
knob" [S1 p.24–25]; Init [S1 p.44]. The clipper's position relative to the master Dry/Wet is not stated [model:
before Main Out, after the mix].

Routing schemes, left to right [S1 p.27]: (1) full serial; (2) full parallel; (3) tab 1 → (tab 2 ∥ tab 3);
(4) (tab 1 ∥ tab 2) → tab 3; (5) "parallel routing of filters in first two tabs, which are serial relative to each
other; with filter in third tab" (wording ambiguous; [model]: (tab 1 → tab 2) ∥ tab 3).

---

## 3. Controls

### 3.1 Global [S1 p.24–25, 52–56][DOC]

| Control | Range | Default | Effect | State key |
|---|---|---|---|---|
| Master Dry/Wet | dry … wet | not stated (manual: double-click default "sometimes a middle value — 50 percent for dry/wet mixes") | overall mix, preserving per-filter mixes | `" Wet"` |
| Main Out | level | not stated | last stage; independent of the mix | `" Main Out"` |
| Clipping mode | Off / Clip (hard at 0 dB, before Main Out) / Soft Clip | not stated | output safety/saturation | `" Clipping Style"` |
| Filter routing | 5 schemes | Init: serial | §2 | `" Filter Routing"`, `" Filter Order"` |
| Power (bypass) | on/off | — | whole plugin | `".Power Btn"` (lockable) |
| CPU/Quality | low / mid / high | mid [S2] | detail of the DSP; saved **per instance**, not per preset | (instance setting) |
| Tuning | Root note / Root frequency / Scala | A4 = 440 Hz | sets the Hz of the Cutoff knob's 0 position | `Microtuning-*` |
| Padlocks | lock levels/mix/clip across preset changes | — | — | `Lock-*` |

### 3.2 Per filter slot [S1 p.26–34][DOC]

| Control | Range | Unit | Default (Init) | Law | Effect | State key |
|---|---|---|---|---|---|---|
| On/Off | on/off | — | on [model: "three … filters routed in simple series"] | — | slot active | `Fn Filter Power` |
| Filter model | ≈ 25–26 models + Bypass | — | Swiss Army Knife | — | §3.3 | `Fn Selected Filter` |
| Filter In | up to +18 dB boost | dB | not stated | — | drives internal saturation | `Fn  Filter In` |
| Filter Out | level | dB | not stated | — | output level of the slot | `Fn  Filter Out` |
| Dry/Wet | dry … wet | % | not stated | — | per-slot mix | `Fn  Filter Wet` |
| Phase | normal / inverted | — | normal | — | inverts the filtered (wet) signal only | `Fn  Phase` |
| Pan | −100 … +100 | % | centre | — | slot pan (mono modulation) | `Fn  Pan` |
| MS | on/off | — | off | — | process in Mid/Side; stereo mod labels become Mid/Side | `Fn  Mid Side Mode` |
| Cutoff | pitch-scaled; ± octaves around 0 | semitones / Hz | 0 st (12 o'clock) | exponential (semitone) law: `f = f_root·2^(st/12)` | cutoff/centre | `Fn  Cutoff` |
| Resonance | 0 … >10 (self-oscillation above 10) | — | 0 | — | resonance/Q | `Fn  Resonance` |
| Slider A (Type) | model-specific | — | lowpass | morph | LP ↔ notch ↔ BP ↔ peak/AP ↔ HP (SAK) | `Fn  Type` |
| Slider B (Poles) | model-specific | — | 2-pole | morph/steps | slope | `Fn  Poles` |
| Slider C | model-specific (Drive for SAK) | — | 0 | — | drive/depth/bandwidth | `Fn  Bandwidth` [model] |
| Slider D | bipolar, model-specific | — | centre | — | spread/width/gain | `Fn  Bipolar` [model] |

**Cutoff reference — conflicting manual statements**: "The zero in the 12 o'clock position above the knob is tuned to
the musical note **E5** by default" [S1 p.32] vs "The center of the frequency knob is always set to **C5**. The
frequency of that note is determined by the root note, the root frequency, and the scale" [S1 p.54]. With A4 = 440 Hz,
12-TET: E5 = 659.26 Hz, C5 = 523.25 Hz. **Measure** (§8).

### 3.3 Filter models (Appendix 1) [S1 p.3–5, 58–110][DOC]

- Classic: **Swiss Army Knife** (SVF; LP / notch / BP / peak(all-pass) / HP morph; 2 or 4 poles, crossfaded; Drive =
  "screaming overdrive" output saturator with pre/post filtering; self-oscillates; "adding more resonance will not
  reduce the amplitude of the bass"; "the resonance peak … is rather wide"), Bread & Butter (ladder, 1–4 poles,
  narrow peak, bass drops with resonance), Evil (Sallen-Key, 1–6 poles), Damage, Deep Cutter (Butterworth 2–16 poles),
  Brick Wall (Chebyshev 6/12 poles, linear, no saturation), Equalize (low shelf / bell / high shelf, ±18 dB, linear),
  Clear Glass (linear 12 dB/oct; "when … open … bypasses the signal entirely").
- Multi-peak: Dueling Bands, The Valley, Vowel, Ripples, Double-Slit.
- All-pass / phasers: Next Phase, Deep Phase, Fractazer.
- Comb / flangers: Flange, Vacuumb, Strings, Robo Comb, Fibonacci Chorus, Crystal.
- Non-filters: Space (reverb), Coils, Decimate, BarberPole, Cabinator; delays Tempolay, Tempong, TemPitch, yaleD opmeT,
  Granular, Time Warp; and **Bypass** (no filtering; keeps pan/phase/M-S/dry-mix).
- "Most of the filters in this collection are non-linear … have a set headroom and can therefore saturate"; Brick Wall
  and Clear Glass are linear [S1 p.59]. The manual's pole table: 1 = 6, 2 = 12, 3 = 18, 4 = "36" (sic; 24 expected),
  8 = 48, 12 = 72, 16 = 96 dB/oct [S1 p.58].

### 3.4 Modulation [S1 p.35–39, 111–131][DOC]

8 slots, each any of: Meta Knob (polarity, quantize, glide), ADSR (A, D, S, R, velocity sens.), MIDI/CV (glide),
Envelope Follower (inputs In/SC/Out L/R, threshold, attack, release; threshold at minimum = continuous follower),
Sequencer/LFO (pattern, shape, rate, tempo sync, retrigger, unipolar, swing, up to 16 steps), Random (rate, range,
rhythm, division, scale quantize), Oscillator (audio rate; coarse, fine, shape, phase, MIDI, audio out/pan send),
Pitch Detect (one per preset; Low/Mid/High range; glide; "+12 semitones" amount makes Cutoff track pitch).
Amounts are bipolar per target, optionally stereo (L/R or Mid/Side); "most modulation amount sliders have a curve …
more resolution … between zero and the 25 percent mark, and then linear". In Init, no slot is active [model: Init =
"all parameters reset"].

---

## 4. Known curves & behaviour

- **Swiss Army Knife** (the only model in Init): state-variable, 12 or 24 dB/oct, LP→notch→BP→peak→HP continuous
  morph, bass level independent of resonance, wide resonance peak, saturating, gain pushes shift the resonance
  position [S1 p.60]. Its gain at the cutoff with Resonance 0 is **not documented** (Q = 0.5 vs 0.707 changes Init's
  attenuation at the cutoff from −18 dB to −9 dB over three stages) — measure.
- **Cutoff**: pitch-linear; knob labels jump by octaves; range extendable by modulation [S1 p.32]. Full range: not
  documented.
- **Resonance**: "Going higher than a Resonance value of 10 can result in the filter self-oscillating" [S1 p.33].
- **Levels**: Filter In up to +18 dB [S1 p.29]; clipper hard-clips at 0 dB [S1 p.25].
- **Latency**: "Cubase reported only 0.2ms latency" (one instance; preset not stated) [S2][DOC]. Quality mode
  default "mid" [S2]. Oversampling: not documented.
- **M/S**: encode before the filter, decode after; input-level modulation and pan stay L/R [S1 p.135].
- **Init sound** [model, from S1 p.44 + standard SVF theory, unverified]: three cascaded 2-pole low-passes at the
  same cutoff (≈ 523 or 659 Hz) = a 6-pole low-pass, −36 dB/oct far above the cutoff. With Q = 0.707 per stage,
  `|H(r)| = 1/√((1−r²)² + (r/Q)²)` cubed gives ≈ −24 dB (fc 659 Hz) to −35 dB (fc 523 Hz) at 1 kHz and ≈ −58 dB (fc 659 Hz) to −70 dB (fc 523 Hz) at
  2 kHz, i.e. only the lows/low-mids of the bus survive — if all three slots are on and fully wet.

---

## 5. Published science

1. **TPT (zero-delay-feedback) state-variable filter** (Zavalishin, *The Art of VA Filter Design*) [S6][PAPER]. Analog
   prototype `H_LP(s) = 1/(s² + 2R·s + 1)`, `H_BP = s/(…)`, `H_HP = s²/(…)` with damping R = 1/(2Q) and s normalised
   to ω_c. Discretised with trapezoidal integrators and pre-warping `g = tan(π·f_c/f_s)`:
   ```
   hp = (x − (2R + g)·s1 − s2) / (1 + 2R·g + g²)
   v1 = g·hp;  bp = v1 + s1;  s1 = bp + v1
   v2 = g·bp;  lp = v2 + s2;  s2 = lp + v2
   notch = x − 2R·bp  (= lp + hp);   allpass = x − 4R·bp;   peak = lp − hp
   ```
   |H_LP(j·ω_c)| = 1/(2R) = Q: Q = 0.5 → −6 dB, Q = 0.707 → −3 dB at the cutoff. 4-pole = two cascaded sections.
   Morphing types = crossfading these outputs (SAK's slider A [S1 p.60]). Nonlinear versions put a saturator (tanh) on
   the integrator inputs/feedback — the source of "pushing gain … shifts the position of the resonance".
2. **Ladder filter** (Bread & Butter): four one-pole stages in a global feedback loop, `k` = resonance (self-oscillation
   at k = 4), bass loss with resonance; ZDF solution as in [S6] ch. 5.
3. **Butterworth / Chebyshev** (Deep Cutter / Brick Wall): cascades of biquads from the standard pole placements
   (Butterworth poles on the unit circle at angles π(2k+n−1)/(2n); Chebyshev type-I with passband ripple) [S6].
4. **Pitch-scaled cutoff**: `f_c = f_ref · 2^(st/12)`, `f_ref` = 12-TET frequency of the root note from A4 = 440 Hz
   [S1 p.32, p.54].

---

## 6. What LYNX does today

- **No LYNX module plays the role**: "Not mapped yet (no LYNX module plays their role): Little AlterBoy, Filterverse, …";
  the slot is left out, as FL does when the plugin is missing [DATA PLUGIN_FALLBACK.md; REA round 113 §4b].
- So today LYNX = FL on her laptop (both skip it). The question is whether "identical" should mean "as FL plays it
  now" (skipped) or "as it sounded when she made the project" (Init through the plugin).
- Reusable LYNX code: FL-native filter engines in `src/core/FruityFilters.{h,cpp}` (FilterEngine classes), SVFs and
  output clippers elsewhere in the effects code [DATA]; nothing Filterverse-specific.

---

## 7. Implementation guidance for LYNX (minimal viable substitute)

1. **Decide whether it is audible at all** (blocking, cheap): (a) in FL project data, read whether her Filterverse
   slots and the FX insert are enabled, the FL slot mix, the send levels, and whether any FL automation clip targets
   a Filterverse parameter (§8.1–8.2); (b) in the official trial, load her Init state and read the real parameter
   values back through the VST3 API (§8.3). If the plugin is bypassed / the slot is off / the master wet is 0, the
   correct substitute is **a pass-through that reports Filterverse's latency** — stop there.
2. If Init is active: build "Init-exact" first — a 3-slot serial SVF chain, each slot a TPT SVF (§5.1) in LP 2-pole,
   Resonance 0, cutoff at the measured reference (523.25 or 659.26 Hz), with the measured Q at Resonance 0, the
   measured soft saturation (Filter In 0 dB, Drive 0), master Wet / Main Out / clipper as measured. Pass: magnitude
   response within ±0.5 dB from 20 Hz to the point where the reference falls below −60 dB; group delay within ±0.1 ms
   below 2 kHz; THD at −6 dBFS 200 Hz within ±2 dB per harmonic; latency equal to the reference's reported latency.
3. Then a general but small module (working name e.g. "Tri Filter"; LYNX naming rules apply): 3 slots × {SVF
   LP/notch/BP/peak/HP morph with 2/4 poles + drive, ladder 1–4 poles, Butterworth 2–16, Bypass}, 5 routings, per-slot
   In/Out/Wet/Phase/Pan/MS, master Wet/Out/Clip. Modulators only if any of her states uses them (none known — §1).
4. **Translation**: her states' values are not readable as settings (§1). Ship a hash rule: the Init parameter block
   (md5 of the canonicalised `parameters` object, prefix `2520a3`, and the `21f85b` UI-only variant) → the measured
   Init settings; anything else (the single "yunk" state) → Init with a log line. Re-check with §8.3 whether the JSON
   becomes readable when read back through the plugin.
5. Test on her bus: render her FX insert (send of a vocal take) with trial vs substitute; pass: octave-band spectrum
   within 0.5 dB RMS (63 Hz–16 kHz) where the reference is above −60 dB, level within 0.2 dB.

---

## 8. Open questions to measure

1. [ ] **FL side** [DATA]: are her Filterverse mixer slots enabled; FL slot mix knob; send level into the FX insert;
       any automation clip or Patcher route targeting Filterverse parameters (`.flp` events). Decides audibility.
2. [ ] **Template history**: in which projects was Filterverse actually installed when she mixed (FL's "missing plugin"
       list vs project dates) — decides whether her renders/bounces ever contained it.
3. [ ] **State read-back** (official trial, host API only): load her Init state → read every VST3 parameter's value
       and text (`getParamNormalized`, `getParamStringByValue`); check `F1–F3 Filter Power`, `Selected Filter`,
       `Type`, `Poles`, `Cutoff`, `Resonance`, `Filter In/Out/Wet`, `Wet`, `Main Out`, `Clipping Style`, `Power Btn`,
       `Filter Routing`. Same for the "yunk" state.
4. [ ] **Init frequency response**: log sweep −30 dBFS, 20 Hz–20 kHz, stereo, at 44.1 and 48 kHz, quality low/mid/high
       — decides cutoff reference (C5 vs E5), Q at Resonance 0, slope, quality-mode differences.
5. [ ] **Init level dependence**: 200 Hz sine at −30 / −18 / −6 / 0 dBFS — THD and compression (internal saturation).
6. [ ] **Latency**: impulse; reported vs measured (SOS saw 0.2 ms in Cubase).
7. [ ] **Default master Wet / Main Out / clipper** in Init — from §8.3 or by nulling against the dry input.
8. [ ] **Single-slot SAK laws** (if a general module is wanted): cutoff knob → Hz table, Resonance → Q table, Poles
       2↔4 morph, Type morph points, Drive curve.

---

## 9. Sources

1. Polyverse Music, *Filterverse User Manual V1.2.0* (137 pp.) —
   https://polyversemusic.com/downloads/manuals/Filterverse%20Manual.pdf — accessed 2026-10-21 — [DOC]
2. J. Walden, "Polyverse Music Filterverse" review, Sound On Sound, Apr 2025 —
   https://www.soundonsound.com/reviews/polyverse-music-filterverse — accessed 2026-10-21 — [DOC, press]
3. "Review: Filterverse multi-filter plugin by Polyverse Music", rekkerd.org, 18 Feb 2025 —
   https://rekkerd.org/review-filterverse-multi-filter-plugin-by-polyverse-music — accessed 2026-10-21 — [DOC, press]
4. "Filterverse — A Universe Of Filters" (public beta), Sonicstate, 21 Mar 2024 —
   https://www.sonicstate.com/news/2024/03/21/polyverse-releases-filterverse-public-beta/ — accessed 2026-10-21 — [DOC, press]
5. S. Wearne, "Polyverse Music announce release of Filterverse", RouteNote, 27 Nov 2024 —
   https://create.routenote.com/blog/?p=17593 — accessed 2026-10-21 — [DOC, press]
6. V. Zavalishin, *The Art of VA Filter Design*, rev. 2.1.0, 2018 —
   https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf — accessed 2026-10-21
   — [PAPER]
7. LYNX REA round 107 §1 (her template, FX insert) — [../analise/REA_rodada107.md](../analise/REA_rodada107.md) — [DATA]
8. LYNX REA round 121 (FL lists Filterverse as missing) — [../analise/REA_rodada121.md](../analise/REA_rodada121.md) — [DATA]
9. LYNX REA round 113 §4b, round 98 (state formats) — [../analise/REA_rodada113.md](../analise/REA_rodada113.md),
   [../analise/REA_rodada98.md](../analise/REA_rodada98.md) — [DATA]
10. [../PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md); `src/core/FruityFilters.{h,cpp}` — [DATA]
11. Her corpus: `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`, `tmp/vm124/inv.txt`,
    `tmp/vm124/st/Filterverse_*.bin` + `index.tsv`, `tmp/gf/PLUGIN_QUEUE.md` — [DATA]
