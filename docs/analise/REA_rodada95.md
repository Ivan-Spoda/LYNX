# Round 95 (pitch95) — LYNX Tune (own vocal pitch corrector) and the Newtone engine

> Two parts with different rules. **§1–3 (Newtone)** is FL-compatibility work: behavioural analysis of FL Studio's
> Newtone, clean-room as in every REA round (layouts and behaviour in our own words; no Image-Line or zplane code,
> pseudocode or data in the repository; markers [DECOMP], [DATA], [EXEC], [model], "not confirmed"). **§4–6 (LYNX Tune)**
> is an original LYNX design written **only from published DSP literature**: no third-party pitch-correction product
> (Antares, Waves, Celemony …) was opened, run, measured or reverse-engineered, not even as a black box.
> Scratch: `~/.claude/jobs/f8789b49/tmp/r95/`. Date: 2026-10-17.

## 1. Newtone in the projects that use it [DATA]

Newtone sits on the master of four of the girlfriend's vocal projects (tmp/gf: *Liwn Passar voz*, *ARTE MORTA*, *MINA
INSEGURA*, *PLAY WITH ME*). Decoding their states with the round-73 layout:

| Project | Option flags | Slave to host playback | Notes | Edits (Cur ≠ Orig) |
|---|---|---|---|---|
| Liwn Passar voz | 1 | off | 2 | none |
| ARTE MORTA | 2 | off | 13 | note 6 −1 st |
| MINA INSEGURA | 1 | off | 13 | note 10 +1 st |
| PLAY WITH ME | 2 | off | 13 | note 11 −1 st; two notes stretched by 1 sample |

None has the slave switch (flag 0x4) on, so in FL each of these Newtone inserts **passes its input through unchanged**
(REA_rodada73 §3 [EXEC]): the singer rendered the corrected take to audio and Newtone only keeps the edit session.
LYNX already reproduces this exactly; nothing in these projects depends on Newtone's pitch engine.

## 2. Newtone's render of an edited track [EXEC, DECOMP]

Method: the round-73 harness (our own code) runs the installation's `Newtone_x64.dll` unmodified in slave mode, playing
from song time 0 with a zero input, so the output is the engine's render alone. Three synthetic vowels of our own
(`tmp/r95/nt/v1..v3.wav`: harmonic source through 700/1220/2600/3300 Hz resonators; notes A3 / C4 with vibrato / D4,
a legato version with 80 ms glides, and an instant step). The engine analysed each one itself (Dispatcher 18 + save),
then one field at a time was edited in the saved XML (`tmp/r95/nt/py/run.py`) and rendered (≈ 60 renders, all kept in
`tmp/r95/nt/`). Measures: our own YIN per frame, RMS per note, LPC envelopes.

### 2.1 The engine's analysis
- Notes tile the whole file back to back (gaps join the note before); boundaries fall ~376–600 samples before the real
  pitch change. `iStartIdx` / `iStopIdx` index the pitch marks (one per period when voiced, ~243 samples apart in
  silence). `fOrigPitch` comes from whole-sample periods (`fs / floor(fs/f0)`: 220 Hz → 57.0393).
- `fVolumeFadeInTime` = `fVolumeFadeOutTime` = 0.2 × `iOrigLength`; `fPitchSmoothInTime` ≈ 3200–3300 samples on every
  voiced note; the rule for `fPitchSmoothOutTime` (522–13059) is not confirmed. All times are in samples.
  `fBestVibFreq` is a vibrato *period* in seconds (5.5 Hz → 0.178).

### 2.2 What the render reads
- The loader [DECOMP `0x180280560`] reads every `Pitch_N` attribute, then updates the note. Ignored on load [EXEC]:
  `fCurPitch`, `fRootOffset`, `fScaleOffset`, `fCurInvPitchFactor`, `GlobalTune`, `GlobalDrift`,
  `GlobalTransitionFactor`, `TargetTuningFreq` (+ its compensation flag), `GlobalTimeVariation`, `GlobalTimeQuantize`,
  `fTimeVariationOffset` — their effect is already folded into the per-note fields when the editor saves.
- An unedited state renders the source **bit-identically, without latency** (−144 dB).

### 2.3 Pitch, drift, vibrato
- Inside a note: out = in + S semitones, S = `fCurrentPitchOffset` + `fAdditionalPitchOffset` (+2.000, +2.996 for 2 + 1;
  frame jitter ±0.05 st); the formants stay (envelope warp 1.000).
- Drift: out = C + d·(in − C) + S with d = `fDrift` (slopes 0.105 for d = 0, 0.528 for 0.5); C ≈ the note's mean pitch
  (exact definition not confirmed). Vibrato in the source scales with it.
- Vibrato: a sine of frequency 1/`fVibFreq` Hz (0.1 → 9.91 Hz, 0.02 → 50.0), starting at phase 0 at the note start, peak
  semitones linear from `fVibAmpStart` to `fVibAmpEnd` across the note; on whenever `fVibFreq` > 0.
- Tremolo (`fTremAmp`): a gain modulation in step with the vibrato (0.5 gave 0.65–1.31) — formula not confirmed;
  `fTremAmp` 1 with no vibrato makes FL's engine output NaN.

### 2.4 Transitions, formant, volume, timing
- Note A → B (b = B's `iCurStartSamplePos`, both voiced, adjacent): over [b − OutA/2, b) A plays at
  S_A + 0.0435·(S_B − S_A) (OutA = A's `fPitchSmoothOutTime`; 0 removes it); from b, a linear ramp from that value to S_B
  over B's `fPitchSmoothInTime` (±0.02 st with 8000). Across a silent gap the change is instant. Why 4.35 %: not confirmed.
- Formant: E_out(f) = E_in(f / F), F = `fCurFormantFactor` × `GlobalFormantShift` (1.2 → 1.200, 0.8 → 0.790,
  1.2·1.1 → 1.315); pitch unchanged.
- Volume: gain `fCurVolume` exactly (0.5 → −6.01 dB), switched over ~one period [model: per grain]. Fade-in: linear from
  `fVolumeFadeInGain` to 1 over `fVolumeFadeInTime` from the note start; fade-out not confirmed.
- `bIsEnabled` = 0: exact zeros.
- Timing: output sample t of a note reads source position `iOrigStartSamplePos` + (t − `iCurStartSamplePos`) ×
  `fCurInvStretchFactor`, pitch kept (1/0.8333 → 1.2017× length). A note owns [`iCurStartSamplePos`,
  `iCurEndSamplePos`); an earlier note that runs long cuts the head of the next; a moved note leaves silence behind;
  `iCurLength` alone does nothing.

### 2.5 Alignment
After a pitch, drift or time edit, the later unedited notes are the exact source moved by a whole number of samples
(+13, −74, −27, −62 in our tests): FL copies unedited spans but its PSOLA timeline shifts them [model] — the "±50
samples at note boundaries" of round 73.

## 3. LYNX's Newtone engine

`newtonefx::renderEdits` (src/core/NewtoneFx.cpp) renders a Newtone track's edits over its source, off the audio thread
when the rack builds the slot (`newtonefx::make (source, state)`); the slave-mode playback of round 73 then plays the
render instead of the plain sample. Not FL's code or analysis: LYNX's own YIN analysis (core/VocalPitch.h) places one
analysis mark per period, locked to the peaks of a low-passed copy, and a time-mapped TD-PSOLA builds the edited notes:
synthesis marks one *target* period apart, each grain taken from the analysis mark nearest the mapped source time
(so stretched notes repeat or drop periods), resampled by the formant factor, gain F/r. §2's rules are applied as
measured: S per note, the A→B transition shape, drift around the note's mean pitch, vibrato, formant factor × global,
volume and fade-in (fade-out modelled as the mirror image), timing and ownership, disabled notes. Unedited notes (and
everything before a transition region) are copied sample for sample; 64-sample cross-fades join plain and rendered spans.

Against FL's renders of the same states (`tmp/r95/nt/lx/ntr.cpp`, per-note mean shift / level on v1):

| Edit | FL | LYNX |
|---|---|---|
| none | bit-identical | bit-identical |
| `fCurPitch` +2 / −1 | no change | no change |
| offset +2 +1 | +2.996 st, −0.52 dB | +2.995 st, −1.69 dB |
| drift 0 / 0.5 | spread −0.35…+0.31 / −0.15…+0.20 st | −0.29…+0.30 / −0.19…+0.16 st |
| volume 0.5 | −6.01 dB | −6.03 dB |
| disabled | silence | silence |
| formant 1.2 | pitch ±0.01 st, −1.63 dB | pitch ±0.06 st, −0.12 dB |

Status: **Close** (same behaviour per note, not FL's waveform). Not reproduced: FL's whole-sample re-alignment of later
notes (§2.5: LYNX keeps them in place), `fRampIn` / `fRampOut`, tremolo, the exact drift centre, stereo sources and
project rates other than the sample's (resampled linearly as before). Newtime's warping is still not reproduced.
In the four projects of §1 nothing changes (not in slave mode).

## 4. LYNX Tune — design (own; published DSP only)

Role: the real-time automatic pitch corrector of a vocal chain (#1 of `tmp/gf/PLUGIN_QUEUE.md`, used 2–6 times in each
of her vocal projects). Sources: YIN (de Cheveigné & Kawahara, JASA 2002), TD-PSOLA (Moulines & Charpentier, Speech
Communication 1990; Zölzer, *DAFX* ch. 7), the RBJ cookbook biquads, McLeod's NSDF (tests only). No Antares / Waves /
Celemony binary, manual text, preset or graphic was used or looked at.

- **Detector** (`vocal::PitchDetector`): 4th-order Butterworth low-pass (≤ 2 kHz), decimation to ~11 kHz, YIN with
  window = longest period, absolute threshold 0.05–0.30 ("Tracking"), parabolic interpolation, then a full-rate
  difference-function refinement around the coarse period (±1 decimation step, ≤ 2048-sample window). New estimate every
  ~2.9 ms. Accuracy on synthetic vowels: < 1 cent at 44.1/48/96 kHz, 82–700 Hz; noise and silence unvoiced.
- **Target**: nearest allowed note of key × scale (13 scales + custom) ∩ the 12 note switches, with 0.1 st hysteresis
  (reset after 60 ms unvoiced); or, with MIDI on, the nearest pitch class held on a chosen Channel Rack channel (pattern
  notes or live keys; none held → the scale). A4 reference 430–450 Hz.
- **Retune**: the applied correction follows (target − sung) through a one-pole with τ = 400·v² ms (0 = instant).
  **Humanize** adds up to 250 ms to τ as a note is held (smoothstep from 120 ms to 420 ms of sustain): short notes snap,
  long notes keep their vibrato. Transpose ±12 st.
- **Shifter** (`vocal::Psola`): streaming TD-PSOLA, two-period Hann grains, marks locked to peaks of a 900 Hz low-pass,
  placed at the detector's window centre (bounded by the latency) and carrying the shift wanted for that period, so the
  output period is exactly fs / target. Unvoiced: plain OLA at the synthesis time (transparent). Unshifted voiced grains
  (< 1.7 cents) lock to their marks, so an in-tune voice comes out as the input itself. Formant preserve on: grains
  unresampled (formants kept); off: resampled by the ratio; formant shift ±7 st on top.
- **Latency** (constant per setting, reported to PDC): 2 × the longest period of the voice range + 4 (alto / tenor 80 Hz:
  1107 samples = 25 ms at 44.1 kHz); **low latency** 1.25 × (694 = 15.7 ms). Ranges: soprano 150 Hz, alto / tenor 80,
  low male 55, instrument 40.
- Real time: every buffer allocated at construction (192 kHz capacity); `process` never allocates (tested with a
  counting `operator new`), handles any block size, NaN/Inf inputs, parameter changes per block.

Routing for MIDI targets: `RackRenderer` keeps a held-notes mask per channel (pattern events, live notes, chokes, stop
/ seek and panic clear it) and hands it to every insert effect (`Effect::setHeldNotes`, forwarded by the parallel
container). UI: the mixer's effect panel shows LYNX Tune's 12 note switches as a one-octave keyboard (scale notes lit,
click = switch off / on; pt-BR caption) above its knobs.

## 5. LYNX Tune — measurements (tests/LynxTuneTests95.inc)

Synthetic vowels with known pitch; output measured with an independent NSDF estimator.
- Instant retune: A3 +30 c, C4 −40 c, C#4 +40 c → A3 / C4 / D4 in C major, within 3 cents (measured ≤ 1 c); chromatic →
  C#4; D switched off → C4; E major at A4 = 432 Hz → 216 Hz; transpose +2 at 48 kHz in low-latency mode.
- Vibrato ±25 c at 5.5 Hz: spread after instant retune < 20 % of the input's; at 400 ms > 60 %; humanize more than
  doubles what a 4 ms retune keeps.
- Formants (LPC peak of F1): preserve on ±5 %; off moves it up > 7 % for +2 st; formant shift −3 st moves it down > 7 %.
- MIDI: A#5 held on channel 3 → A#3 target; nothing held → scale.
- Transparency: an in-tune voice with a noise burst → input delayed by the latency, residual < −25 dB (measured −34 dB
  on the steady part); mix 0 = the input delayed exactly. No allocation over 8 blocks of mixed sizes with every
  parameter moving.

## 6. Files
`src/core/VocalPitch.{h,cpp}` (detector, offline analysis, scales, streaming PSOLA), `src/core/LynxTune.{h,cpp}`,
`src/core/NewtoneFx.{h,cpp}` (notes, renderEdits, make with state), `EffectKind::lynxTune`, Effects.cpp (info, texts,
factory, parallel forwarding), Effects.h (`NoteMask`, `setHeldNotes`), RackRenderer (held notes, Newtone state),
ProjectFile ("lynxTune"), MixerComponent (menu entry, keyboard strip), FlpImport (report), pt-BR, docs/PLUGINS.md, website.

## Where I stopped / next steps
- Done: LYNX Tune (engine, MIDI-target routing, mixer keyboard strip, tests, docs, website) and LYNX's Newtone edit
  renderer (§3).
- Next: an FL-style LYNX Tune editor page in the plugin-layout system of round 93 (`src/core/PluginLayout.cpp`, not in
  main when this round ended) with a live pitch / correction meter (`controlOutput` 0–2 already exposes sung note, target
  and correction); Newtone's tremolo, ramps and later-note re-alignment (§2.5); Newtime's warping; register LYNX Tune as
  the substitute for Auto-Tune / WavesTune in round 98's plugin-substitution map once that lands.
