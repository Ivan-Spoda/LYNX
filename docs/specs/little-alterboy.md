# Soundtoys Little AlterBoy — spec dossier (no LYNX role yet)

Round 125 (spec125). Clean-room dossier built from the vendor's user guide (Little AlterBoy User's Guide, © 2019,
read through a distributor's public copy), the vendor's product listings, published pitch-shifting literature, LYNX's
own vocal-pitch code and her saved project data. No Soundtoys binary was opened, decompiled or instrumented; the
encoded preset payload was **not** decoded.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [DATA] read from her projects / LYNX files ·
[model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** Little AlterBoy is a "monophonic voice manipulator": real-time pitch shift (±12 semitones),
independent formant shift with a LINK button that ties formant to pitch (varispeed-like), three modes — TRANSPOSE
(plain shift), QUANTIZE (snap to the nearest chromatic semitone "as quickly as possible") and ROBOT (lock to one
note, C one octave above middle C at Pitch 0) — plus a DRIVE stage using a tube-saturation model from Decapitator and
a dry/wet MIX [S1][DOC]. It descends from Soundtoys' (Wave Mechanics') PurePitch, "the world's first real-time formant
shifting plug-in" [S1][DOC]. Her corpus holds it in **696 slots / 399 projects / 17 states** (412-project inventory)
and 1307 slots / 52 states in the whole copy [DATA]; **67 % of those slots carry one identical state** and the next
two cover another 17 % [DATA]. The state is a Soundtoys **encoded** text chunk (`PRESET =` + a 6-bit printable
payload), so neither LYNX nor this dossier knows her settings — the first job (§8) is a black-box state→parameter
read-out in the official trial. LYNX has no AlterBoy role today, but it already owns the two hard parts (YIN pitch
detection and a formant-capable TD-PSOLA in `core/VocalPitch.h`), so a substitute is a thin front-end (§7).

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Soundtoys, Inc. (Burlington, VT), Little AlterBoy — "Voice Manipulation" | [S1][DOC] |
| Version she uses | state header `WIDGET = Little AlterBoy; VERSION = 4; SAVED_BY_VERSION = 5.0.1;` in all 52 distinct states | [DATA] |
| Current version | 5.5.5.19885 (KVR listing); formats VST, VST3, AU, AAX; iLok protection; Windows 7+ / macOS 10.10+ | [S2][DOC] |
| Format in her projects | VST2 `C:\Program Files (x86)\VstPlugins\SoundToys\x64\LittleAlterBoy.dll`, FL VST2 unique ID `StLa` | [DATA], [S8 round 98][DATA] |
| History | first offered free ("Limited Time Only") at SXSW 2015 (forum post dated 18 Mar 2015 linking soundtoys.com/sxsw2015/) | [S3][DOC] |
| Lineage | "inspired by PurePitch (our very first plug-in, and the world's first real-time formant shifting plug-in) and mashed up with a tube saturation model from our popular Decapitator plug-in" | [S1 p.3][DOC] |
| Intended input | "designed to process only clean monophonic vocal tracks"; insert it "BEFORE any other effects" | [S1 p.3][DOC] |

Note on the name: there was a full "AlterBoy" in the original Soundtoys bundle (PurePitch-era); this dossier covers
only Little AlterBoy, the product in her projects. Details of the older AlterBoy were not verified.

### Her usage [DATA]

- Inventory (412 main projects): **696 slots, 399 projects, 17 distinct states** — present in nearly every project, i.e.
  part of her template (vocal FX chain, [REA round 113 §4b](../analise/REA_rodada113.md): "Little AlterBoy (39)" main-project slots).
- Whole copy incl. autosaves (`tmp/vm124/inv.txt`): 1307 slots, 59 main slots in 30 main projects, 52 distinct states.
- Distribution (`tmp/vm124/st/index.tsv`, slots per state): state #0 **875 slots (67 %)**, #1 178, #2 52, #3 49, #4 25,
  #5 16, then ≤ 10 each. Payload length 692 characters in 50 states, 680 in 2.
- Payload structure (from comparing her own files, not decoding): an ASCII header `0101000006800101 0001` (2 states:
  `…0668…`), then characters from the 64-symbol range `0x30–0x6F` (`'0'`…`'o'`), i.e. a 6-bit text packing (692 chars ≈
  519 bytes) [DATA]. Edits are **local**: state #7 differs from #0 in only the last 4 characters, #1 / #3 / #13 in 11
  characters near offset 672, #2 in 379 characters from offset 43 [DATA]. So the payload is not diffused by
  encryption-like mixing; a black-box diff (§8) can map each control to its characters without opening the binary.
  We did not decode it (Soundtoys states are opaque by design; decoding is left to an approved black-box procedure).

---

## 2. Signal flow

```
in (mono vocal; stereo input: handling not documented) ──┬────────────────────────── dry ─────────────┐
                                                          │                                           (MIX)── out
                                                          └─► pitch detector (needed by QUANTIZE/ROBOT;│
                                                               TRANSPOSE may not need it — see §4)     │
                                                              ─► pitch/formant shifter                 │
                                                                  pitch ratio  α = 2^(semi/12)          │
                                                                  formant ratio β (LINK on: β = α)       │
                                                                  TRANSPOSE: semi = PITCH               │
                                                                  QUANTIZE : semi = PITCH + (nearest     │
                                                                             semitone − detected)       │
                                                                  ROBOT    : semi = (C5 + PITCH) − detected
                                                              ─► DRIVE (Decapitator tube model) ─ wet ──┘
MIDI (optional): host MIDI mapped to PITCH → played intervals / vocoder-style robot
```

Sources: controls and their roles [S1 p.4–6][DOC]; the Drive is "analog modeled tube saturation" applied to "your
altered audio" (i.e. on the processed path) [S1 p.5][DOC]; Mix "adjusts the balance between the dry/original and the
wet/processed vocal sound" [S1 p.6][DOC]. The detector-in-TRANSPOSE question and the exact Drive position relative to
Mix are [model] (the guide only says Drive acts on "your altered audio").

---

## 3. Controls

| Control | Range | Unit | Default | Law / taper | Audible effect | State mapping |
|---|---|---|---|---|---|---|
| PITCH | −12.0 … +12.0 | semitones | 0.0 (not stated; [model]) | display in 0.1 st; "in semitones up to an entire octave above or below"; "all the way counter-clockwise to −12.0 will shift … down by one whole octave" | transposition (TRANSPOSE/QUANTIZE); target note offset from C (ROBOT) | encoded |
| LINK | on / off | — | not stated | — | ON: formant follows pitch, "similar to that of a sample speeding up or slowing down"; OFF: "more natural-sounding", formant independent | encoded |
| FORMANT | not stated (guide's tip uses −1.0 … −3.0) | likely semitones [model] | 0 [model] | — | UP: "more feminine", extreme: "chipmunky"; DOWN: "deeper, richer, and more masculine" | encoded |
| MODE | TRANSPOSE / QUANTIZE / ROBOT | — | TRANSPOSE ("the 'Normal' operational mode") | switch | §4.2 | encoded |
| DRIVE | not stated | — | not stated | — | "analog modeled tube saturation"; "small amounts … warmth … large amounts … grit and edge" | encoded |
| MIX | dry … wet | % [model] | not stated | — | blend for harmony / doubling | encoded |
| (Output / I/O) | not documented in the guide | — | — | — | — | encoded |
| MIDI | host MIDI mapped to PITCH | — | — | — | "intervals to be played in TRANSPOSE and QUANTIZE modes, and in ROBOT mode, super awesome vocoder-style effects" | — |

All quotes [S1 p.4–6][DOC]. Third-party reviews: Pitch "+/- 12 semitones (1 octave)" and MIDI with "C3 as Pitch = 0"
via a side-chain/MIDI routing [S4][DOC, third-party]; one review says Robot "fixes all pitches to C4" [S4] whereas the
vendor says "one octave above middle C" [S1 p.5] — with middle C = C4 that is **C5 (523.25 Hz)**; with FL/Yamaha naming
(middle C = C5) it would be C6. **Unresolved** — measure (§8). The FORMANT range, DRIVE range, MIX law and whether an
Output control exists were not found in public documentation; treat them as unknown.

---

## 4. Known curves & behaviour

### 4.1 Pitch/formant shifting

- Pitch: ±12 st [S1]. LINK ON = formants move with pitch (the classic varispeed/"chipmunk" sound); LINK OFF = formant
  preserving shift with an independent FORMANT offset [S1 p.4]. The vendor tip "LINK … will sometimes produce smoother
  results, especially for more challenging vocal tracks or pitched instrument tracks" [S1 p.4][DOC] implies LINK ON
  uses a simpler (resampling-like) path that does not depend on accurate pitch marks or envelope estimation [model].
- The vendor's advice to shift formant up a little when up-shifting and down a little when down-shifting [S1 p.5]
  confirms that LINK OFF keeps the formants fixed (pure formant preservation) [model].
- Latency: not documented; not measured. Reported plug-in latency and actual delay must be measured (§8).

### 4.2 Modes [S1 p.5][DOC]

- **TRANSPOSE**: "simply shift the pitch of the incoming vocal without automatic correction."
- **QUANTIZE**: "tunes your vocal to the nearest chromatic semitone as quickly as possible … similar to the automatic
  tuning effect found on countless pop/rap/R&B hits" — i.e. chromatic scale, zero retune time, no key/scale control.
  The vendor tip pairs it with "a healthy dose of DRIVE" for the "much-abused pop vocal effect".
- **ROBOT**: "locks your vocal track to a single note … when the PITCH knob is set to 0.0 the vocal pitch will be locked
  one octave above middle C. To produce notes other than 'C', simply adjust the PITCH control." With MIDI mapped to
  PITCH this becomes a monophonic "vocoder-style" melody re-pitcher.
- Unvoiced segments (consonants, breaths) in QUANTIZE/ROBOT: behaviour not documented [model: passed at the last ratio
  or unshifted — measure].

### 4.3 Drive

- "Analog modeled tube saturation" [S1 p.5]; "a tube saturation model from our popular Decapitator plug-in" [S1 p.3].
  Decapitator's styles are A (Ampex 350 tape preamp), E (Chandler/EMI TG channel), N (Neve 1057), T (Thermionic Culture
  Vulture, triode) and P (Culture Vulture, pentode) [S5][DOC]; which one Little AlterBoy uses is **not stated** — the
  "tube" wording points at T or P [model]. Harmonic signature, gain compensation and level dependence: not measured.

### 4.4 Stereo, sample rate

- "Monophonic" refers to the pitch content (one voice). How a stereo input is handled (summed, linked, or per channel)
  is not documented. Her takes are dual-mono 44.1 kHz ([REA round 107 §1](../analise/REA_rodada107.md)).

---

## 5. Published science

1. **TD-PSOLA** (Moulines & Charpentier 1990) [S6][PAPER]. Analysis marks `t_a(k)` one pitch period P(k) apart (on
   glottal closures / energy peaks); grains `g_k[n] = w[n]·x[n + t_a(k)]` with a Hann window of length 2P; synthesis
   marks advance by the *target* period, `t_s(k+1) = t_s(k) + P(k)/α`; output `y[n] = Σ_k g_{k'}[n − t_s(k)]`, the
   analysis grain k' nearest in time to t_s(k) (grains repeated/dropped to keep duration). Because each grain keeps its
   own spectral envelope, **formants are preserved** while the harmonic spacing becomes α·f0. **Formant shift by β**:
   resample each grain by β before overlap-add, `g'_k[n] = g_k[n·β]` (with the window re-applied), which scales the
   envelope `E(f) → E(f/β)`; LINK ON ≡ β = α ≡ resampling the whole signal. Strengths: very low cost, natural voice;
   weaknesses: needs reliable pitch marks, artefacts on breathy/unvoiced segments.
2. **Phase vocoder pitch shifting** [S7][PAPER]. STFT with hop R; per bin instantaneous frequency
   `ω̂_k = ω_k + princarg(φ_k(m) − φ_k(m−1) − ω_k·R)/R`; shift by time-stretch + resample, or directly by Laroche &
   Dolson's peak shifting: detect spectral peaks, move each peak's region of influence by Δω = (α−1)·ω_peak, and
   update phases `φ ← φ + Δω·R` for phase coherence ("identity phase locking"). Polyphonic-safe, but transient smearing
   and phasiness; the formants move with α unless an envelope is applied.
3. **Spectral-envelope preservation / warping** [S8][PAPER]. Estimate the envelope E(ω) (cepstral liftering:
   `c = IFFT(log|X|)`, keep quefrencies n < N_c with N_c ≈ fs/(2·f0); or Röbel & Rodet's iterative "true envelope":
   `A_0 = log|X|`, `V_i = cepstral_smooth(A_i)`, `A_{i+1} = max(A_i, V_i)` until convergence), then apply
   `Y(ω) = X_α(ω) · E(ω/β) / E_α(ω)` — whiten the shifted spectrum and re-impose the original envelope warped by β.
   This is the phase-vocoder route to independent pitch and formant (the "Formant" knob).
4. **LPC alternative** [model, textbook]: an all-pole envelope `1/A(z)` from linear prediction; inverse-filter to a
   residual, pitch-shift the residual, re-filter with `A(z/γ)`-warped or frequency-warped coefficients for formant
   shift. Cheap but buzzy on high-pitched female voices.
5. **Pitch detection for QUANTIZE/ROBOT**: YIN (de Cheveigné & Kawahara 2002) — difference function
   `d(τ) = Σ (x[j] − x[j+τ])²`, cumulative-mean normalisation `d'(τ) = d(τ)·τ / Σ_{j≤τ} d(j)`, absolute threshold,
   parabolic interpolation [cited in LYNX `VocalPitch.h`][PAPER]. QUANTIZE target: `m = 69 + 12·log2(f0/440)`,
   `m* = round(m)`, ratio `α = 2^((m* − m + PITCH)/12)`; ROBOT: `α = f_target/f0` with `f_target = 523.25·2^(PITCH/12)`
   Hz if the fixed note is C5 (§3 caveat).
6. **Tube saturation** [model, textbook]: an asymmetric static curve gives even harmonics (triode-like), e.g.
   `y = tanh(k·(x + b)) − tanh(k·b)` followed by a DC blocker; pentode-like = more symmetric, harder knee. Calibrate
   against measured harmonic levels, not against this formula.

Historical note: Soundtoys/Wave Mechanics' PurePitch was a Pro Tools TDM plug-in with independent pitch and formant
control [S9][DOC, third-party listing]; the algorithm it used is not published.

---

## 6. What LYNX does today

- **No LYNX module plays the role** — "Not mapped yet (no LYNX module plays their role): Little AlterBoy, …"; LYNX
  leaves the slot out, as FL does when the plugin is missing [DATA PLUGIN_FALLBACK.md; REA round 113 §4b].
- The state cannot be read: Soundtoys chunks are "text `WIDGET = …; VERSION = …; PRESET =<encoded>` (not readable)"
  [REA round 98](../analise/REA_rodada98.md); for Soundtoys, LYNX's policy is "encoded chunks → default presets".
- Reusable LYNX code [DATA src/core/VocalPitch.h, LynxTune.{h,cpp}]: YIN detector (decimated ~11 kHz + full-rate
  refinement), TD-PSOLA with two-period Hann grains and per-grain resampling for formant shift (`Psola::setShift(ratio,
  formant)`), scale masks incl. chromatic, hysteresis note picker; LYNX Tune exposes formant preserve on/off and formant
  shift ±7 st. Ember Saturator already plays the Decapitator role ([PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md)).

---

## 7. Implementation guidance for LYNX (minimal viable substitute)

Build a small effect (working name e.g. "Voice Shift"; follow LYNX naming rules) as a front-end on `vocal::Psola` + YIN:

1. **First, learn her settings** (blocking): §8 item 1 — read her 3 main states (#0, #1, #2 cover 84 % of slots) back
   as parameter values in the official trial. If state #0 is the plugin default (pitch 0, formant 0, TRANSPOSE, Mix
   100 %, Drive 0), the "identical" substitute is close to a pass-through plus the plugin's latency and colour — check
   this before building anything elaborate.
2. **Parameters** (store normalised like the original once its parameter list is known): Pitch −12…+12 st (0.1 st
   display), Formant (range from §8), Link, Mode {Transpose, Quantize, Robot}, Drive, Mix, (Output if it exists).
3. **Engine**:
   - Link ON (or Formant = Pitch): ratio-only path — PSOLA with `formant = ratio` (equivalently resampling).
   - Link OFF: `Psola::setShift(α, β)` with β = 2^(Formant/12) (or the measured formant law).
   - Transpose: α = 2^(Pitch/12), no correction (detector still needed for PSOLA marks).
   - Quantize: chromatic mask, **zero retune smoothing** (only enough slew to avoid clicks; measure the reference's
     transition time on a glide), no hysteresis beyond what the reference shows.
   - Robot: fixed target `f = C_ref·2^(Pitch/12)` (C_ref from §8), α = f/f0 per period.
   - Drive: a tube-style waveshaper on the wet path (reuse Ember's curve family), fitted to measured THD vs Drive.
   - Mix: dry/wet with the measured law (linear vs equal-power), dry delayed by the engine latency.
4. **Latency**: report PSOLA's latency to the host; if the original reports a different latency, match the
   **reported** value too (FL compensates by the reported number).
5. **Translation**: Soundtoys states stay unreadable in-app unless an approved black-box map exists (§8 item 2). Until
   then: if her dominant state #0 is identified (§8.1), ship a hard-wired "her preset" for the exact payload hash of
   states #0–#2 (hash → parameter set, measured in the trial) and the default for others [model: same approach as
   reading a known chunk; no decoding of unknown chunks].
6. **Tests / pass criteria** (render her real vocal take through trial vs substitute at her settings):
   f0 track of the output within ±5 cents median, ±15 cents P95 on voiced frames; formant positions (LPC F1–F3 on
   sustained vowels) within ±3 %; long-term octave-band spectrum within 0.5 dB RMS; output latency ±1 sample;
   Quantize step transition time within ±2 ms; Drive THD at 1 kHz −18 dBFS within ±1 dB per harmonic (H2–H5).

---

## 8. Open questions to measure (official trial, black-box only)

1. [ ] **State → parameters**: load each of her states (#0, #1, #2, #3 first) into the trial in a host via the normal
       VST2 `effSetChunk`, then read every parameter through the VST2 parameter API (`getParameter`, `getParameterName`,
       `getParameterDisplay`). Decides her actual settings. (Host API only — no binary analysis.)
2. [ ] **Per-control payload map**: from the default state, change one control at a time in the trial, save the chunk,
       diff the payload (characters change locally, §1). Decides whether LYNX can translate any Soundtoys AlterBoy state
       without the plugin. Policy check with the coordinator before using a payload map in-app.
3. [ ] **Parameter list & laws**: names, count, text for 0…1 in 0.01 steps (Pitch, Formant, Drive, Mix, Output) — decides
       ranges/units/defaults (formant range unknown).
4. [ ] **Latency**: impulse / click train; reported vs actual delay at 44.1 and 48 kHz, each mode.
5. [ ] **Robot reference note**: a sung vowel at ~200 Hz, ROBOT, Pitch 0 → output f0 (C4 = 261.6, C5 = 523.3 or other).
6. [ ] **Quantize dynamics**: a sine glide 200→400 Hz over 2 s and a vibrato tone (5.5 Hz, ±50 c) — step timing, slew,
       hysteresis at the semitone boundary.
7. [ ] **Formant law**: a synthetic vowel (fixed f0, LPC formants at 700/1220/2600 Hz) with Formant at −12…+12 steps,
       LINK off — measured formant ratio per step.
8. [ ] **LINK ON vs resampling**: same vowel, Pitch +5, LINK on — compare to an ideal resampling (formant ratio = pitch
       ratio?).
9. [ ] **Drive**: 1 kHz sine at −30 / −18 / −6 dBFS, Drive 0/25/50/75/100 % — harmonics H2–H7, gain change, and whether
       Drive is before or after Mix (run with Mix 50 %).
10. [ ] **Mix law**: Pitch +12, Mix 0/25/50/75/100 % on a sine — dry and wet amplitudes (linear vs equal-power).
11. [ ] **Unvoiced handling**: white-noise burst and an "s" sample in QUANTIZE/ROBOT — passed, shifted or gated?
12. [ ] **Stereo**: different signals in L and R — summed to mono, or per-channel?
13. [ ] **FL slot**: whether her AlterBoy slots are enabled and their FL wrapper mix value (FL project data) — decides
       how much of it is actually audible.

---

## 9. Sources

1. Soundtoys, "Little AlterBoy User's Guide — For Mac and Windows — Voice Manipulation" (© 2019, 8 pp.) —
   https://r2.gear4music.com/media/70/702833/download_702833.pdf (distributor-hosted copy) — accessed 2026-10-21 — [DOC]
2. KVR Audio, "Little AlterBoy by Soundtoys" product page (version 5.5.5.19885, formats, features) —
   https://www.kvraudio.com/product/little-alterboy-by-soundtoys — accessed 2026-10-21 — [DOC]
3. Image-Line forum post "get your FREE Soundtoys Plugin, 'Little AlterBoy' here" (18 Mar 2015) —
   https://forum.image-line.com/viewtopic.php?p=997607 — accessed 2026-10-21 — [DOC, third-party]
4. FaderPro, "Soundtoys Little Alterboy: Endless pitch possibilities" —
   https://blog.faderpro.com/plugins/soundtoys-little-alterboy/ — accessed 2026-10-21 — [DOC, third-party review]
5. Soundtoys, Decapitator Manual (styles A/E/N/T/P) — https://www.soundtoys.com/wp-content/uploads/Decapitator-Manual.pdf
   (content via search excerpt) — accessed 2026-10-21 — [DOC]
6. E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using
   diphones", Speech Communication 9(5–6), 453–467, 1990 —
   https://researchportal.ip-paris.fr/en/publications/pitch-synchronous-waveform-processing-techniques-for-text-to-spee/
   — accessed 2026-10-21 — [PAPER]
7. J. Laroche, M. Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects",
   IEEE WASPAA 1999 — https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf — accessed 2026-10-21 — [PAPER]
8. A. Röbel, X. Rodet, "Efficient spectral envelope estimation and its application to pitch shifting and envelope
   preservation", DAFx-05, Madrid, 2005 — https://dafx.de/paper-archive/details/EDSTSaO17Gd_0LV03iwDfw — accessed
   2026-10-21 — [PAPER]
9. Vintage King listing "Soundtoys PurePitch Native" (discontinued; "first pitch-shifter that could shift vocal
   formants") — https://vintageking.com/soundtoys-purepitch-native — accessed 2026-10-21 — [DOC, third-party]
10. A. de Cheveigné, H. Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4), 2002 —
    cited in LYNX `src/core/VocalPitch.h` — [PAPER]
11. LYNX REA round 98 (FL wrapper state formats; Soundtoys chunks) — [../analise/REA_rodada98.md](../analise/REA_rodada98.md) — [DATA]
12. LYNX REA round 113 §4b (her corpus priorities) — [../analise/REA_rodada113.md](../analise/REA_rodada113.md) — [DATA]
13. LYNX REA round 107 §1 (her takes: 44.1 kHz dual-mono) — [../analise/REA_rodada107.md](../analise/REA_rodada107.md) — [DATA]
14. LYNX source: `src/core/VocalPitch.h`, `src/core/LynxTune.{h,cpp}`, [../PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md) — [DATA]
15. Her corpus: `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`, `tmp/vm124/inv.txt`,
    `tmp/vm124/st/Little_AlterBoy_*.bin` + `index.tsv` — [DATA]
