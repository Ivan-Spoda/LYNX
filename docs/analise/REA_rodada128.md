# REA round 128 — parity audit of the FabFilter / Slate / Antares-role engines

Branch `feature/parity128`. English-first; clean-room; black-box only. User request (2026-10-21, translated): "the
plugins must be similar to the originals — people say details are missing; is it configuration or design?" Round
114 scored only the layout; this round audits every control, option, gesture and piece of displayed information
(`docs/PARITY.md`, section "FabFilter / Slate / Antares roles") and then implements the gaps, biggest first.
Round 129 (parity129) does the same for the Waves / SSL / Valhalla roles.

Engines: Facet EQ ← Pro-Q 4, Apex Limiter ← Pro-L 2, Crest Compressor ← Pro-C 3, Prism Multiband ← Pro-MB, Sentry
Gate ← Pro-G, Hush De-esser ← Pro-DS, Ember Saturator ← Saturn 2 (+ Decapitator's role; Soundtoys is not installed),
Aether ← Fresh Air, LYNX Tune ← Auto-Tune Pro (Auto mode).

## 1. Evidence [EXEC]
- Each official trial was hosted in round 114's host (`tmp/look114/host/uiview.exe`, copied to
  `D:\lynx-work\parity128`): the full parameter list through the public VST3 API (`params` at 5 points and 121
  points for every enumeration → `ref/*_params.tsv`, `ref/*_enum.tsv`, `ref/enums.txt`: every option text of every
  menu), and screenshots of the main view (evaluation dialog dismissed with "Evaluate") plus Pro-Q's band panel and
  node tooltip, Pro-L's folded panel, Pro-C's knee and side-chain panels, Pro-MB's band panel, Pro-G's expert strip,
  Auto-Tune's Auto view (`ref/*.png`). Capture driver: `tmp/parity128/py/cap.py`.
- LYNX's editors rendered with `LYNX.exe --plugui` from main 0e331e7 (`lynx-before/*.png`).
- Side-by-side page: Desktop `LYNX-paridade/FabFilter-Slate-Antares.html` (built by `tmp/parity128/py/html.py`).

## 2. Findings (summary — full tables in docs/PARITY.md)
- The biggest shared gap is the frame every original has before any knob: undo / redo, A/B + copy, a preset
  browser with save, help, and (FabFilter) input / output / oversampling in the bottom bar. LYNX's own engines had
  no presets at all and Ctrl+Z did not work inside their windows.
- Per engine, the user-visible gaps are main controls missing outright: Apex has no ATTACK and one channel-link knob
  (original: transients + release); Hush has no SINGLE VOCAL / ALLROUND mode, one frequency handle (original: two),
  no stereo link / side-chain switch / lookahead amount; Crest has no DRY knob or auto threshold; Sentry has no
  channel mode or side-chain gains; Prism has 4 fixed contiguous bands; Ember lacks per-band tone, feedback,
  dynamics, solo / mute and modulation; LYNX Tune lacks Natural Vibrato, Hold, per-note Bypass and the vibrato
  generator; Facet lacks per-band power and stereo placement.

## 3. Implementation
Work split: parity128 (this branch) did Facet EQ, Ember and the resizable editor windows; sub-branches parity128a
(Apex, Hush), parity128b (Crest, Sentry, Prism) and parity128c (LYNX Tune) are merged in (§3a–§3c). The shared
editor header (undo / redo, A/B, presets) is round 129's (parity129 builds one PresetBar for every LYNX editor), so
it is not duplicated here. Rules kept: new parameters are appended (projects are positional; a missing parameter
loads at its default) and every default leaves the old output sample for sample (tests below; the fitted models of
vmatch123/124/127 are not retuned).

### 3.1 Facet EQ ← Pro-Q 4 [EXEC: tests/Parity128Tests.inc]
- `maxEffectParams` 64 → 96 (Facet needs 88; the arrays are per slot, nothing else depends on 64).
- Band power ([64 + b], Pro-Q's band power button): a band switched off keeps its settings and does nothing
  (`analogSections` returns no section, `isDynamic` is false). Editor: the band's power button replaces the number dot
  in the panel, a power icon in the node read-out, "Band on" in the band menu, a hollow node while off.
- Stereo placement ([72 + b]: Stereo, Left, Right, Mid, Side). Zero latency: stereo bands on both channels, L / R bands
  on theirs, then one M/S pass for the mid / side bands (M = (L + R) / 2, S = (L − R) / 2; skipped when no band needs
  it, so the default path is the old one sample for sample). Natural / linear phase: the FIR holds the stereo bands
  only; placed bands run as the zero-latency sections after it (minimum phase — noted in PARITY.md). Editor: placement
  button in the panel (Pro-Q's "Stereo" button), submenu in the band menu, an L / R / M / S tag beside the node.
- Output stage: auto gain ([80], takes back the mean of the static curve over 32 log-spaced points 20 Hz – 20 kHz),
  polarity ([81]), pan ([82], −1 … +1, the far channel to −∞ like Pro-Q's "Left: 0 dB / Right: −INF") in left /
  right or mid / side ([83]). Bottom bar: "Auto gain" button and PAN / M/S value; OUT's right-click menu adds
  polarity, auto gain and the pan mode.
- Display settings saved with the slot: analyser range 60 / 90 / 120 dB ([84]), speed in 5 steps ([85]: the fall
  rate 0.3 … 6 dB per frame, medium = the old 1.5), tilt 0 … 6 dB/oct ([86], default 4.5 = the old fixed tilt), the
  graph's ±3 / 6 / 12 / 30 dB ([87], was editor-only), and Freeze (editor only) in the analyser menu.
- Tests: defaults bit-identical to a bell without the new controls; power off = flat, settings kept; Left / Right
  leave the other channel untouched and equal the stereo band on theirs; Mid leaves a pure side signal untouched and
  lifts a pure mid one by the gain, Side the opposite; a Left band in natural phase leaves the right channel = the
  delayed input; auto gain = the predicted dB; polarity = −output; pan right silences the left channel; M/S pan
  fully "side" removes a mono signal.

### 3.2 Ember Saturator ← Saturn 2 [EXEC: tests/Parity128Tests.inc]
- Appended: Input (±24 dB) and per band (×4) power, state (normal / solo / mute), tone Bass / Mid / Treble /
  Presence (±18 dB: LYNX's own curves — low shelf 120 Hz, bell 800 Hz, high shelf 3.5 kHz, bell 6 kHz — on the
  band's saturated signal), feedback amount + frequency (a band-pass loop around the shaper at the oversampled rate,
  10 Hz – 1 kHz, loop gain up to 0.9), dynamics −1 … +1 (the band's level follows its input level: ±0.5 dB per dB
  over −18 dBFS, clamped at ±18 dB). Every stage is skipped at its neutral value (old output unchanged).
- Editor: the band panel now follows Saturn's order — power / solo / mute, style, MIX, FEEDBACK, FREQ, DYNAMICS, the
  big DRIVE, the four TONE faders, LEVEL — with auto gain and dry / wet on its right; the global tilt / low cut /
  high cut (LYNX extras) and In moved to the bottom bar; the window grew to 960 × 540. Bands split / removed carry
  all their settings. The display tags switched-off / soloed / muted bands.
- Saturn 2 translation (src/core/FabFilterStates.cpp): input gain, per-band feedback (%, Hz), dynamics, tone (dB),
  state. Offsets 0 / 1 / 14 of each 17-value band block follow the plug-in's own VST3 parameter order [model];
  Ember's tone curves are LYNX's, not measured against Saturn — vmatch can refine them if her states use the tone.
- Tests: defaults; power off = the delayed input; mute = silence; a soloed high band removes a 220 Hz tone (< −20
  dB); bass +12 dB lifts 60 Hz by 10–12.5 dB and leaves 3 kHz; presence lifts 6 kHz; input +12 dB; expansion /
  compression; feedback bounded; translation of a Saturn state with tone / feedback / dynamics / mute.

### 3.3 Resizable editor windows
The display-first editors (Apex, Hush, Crest, Sentry, Prism, Ember, Aether) open in a resizable window that keeps
the editor's proportions (70 % … 200 %); the editor is drawn scaled (src/ui/ProEditors.cpp `ScaledHolder`). Facet EQ
already resized with its own layout.

### 3a. Apex Limiter and Hush De-esser (sub-branch parity128a)
Every new control is a parameter appended to the engine (saved projects are positional; older ones load the new
parameters at their defaults) and every default leaves the output **byte-identical** to the round-127 engines: a
harness (`tmp/parity128`, scratch) built the old and the new ApexLimiter.cpp / MixSuite.cpp side by side and compared
the raw output over 3 sample rates × 48 Apex settings (gain, style, link, true peak) × 32 Hush settings (threshold,
mode, detector, look-ahead, listen) [EXEC: `cmp` identical]. Tests: `tests/Parity128aTests.inc`.

- **Apex** (`core/ApexLimiter.*`, `ApexEditor`): 8 styles — the four old ones keep the indices their saved values
  round to (0, 1/3, 2/3, 1 → Clean 0, Punch 2, Loud 5, Safe 7) and Glue / Dynamic / Dense / Bus sit between them as
  new release laws (no existing law retuned); ATTACK 0–10 s (v⁴ law like the reference's text, 625 ms at the middle):
  how slowly the slow release stage dips (0 = at once = before); CHANNEL LINKING split into TRANSIENTS (the old
  Link) and RELEASE (adds linking of the release stage beyond the transients; default 0 so old projects with a
  partial link sound the same); bottom bar Dither Off / 16–24 bits (TPDF, ±1 LSB) with noise shaping None / Basic
  (1st order) / Optimized (2nd order) / Weighted (3-tap) error feedback; ADVANCED » fold-out with DC offset filter
  (5 Hz one-pole), Unity Gain (output ÷ gain), Audition Limiting (delayed input after gain − output); meter-scale menu
  next to TP (−16 / −32 / −48 dB, K-12 / 14 / 20 — labels relative to the K reference). Not done: oversampling
  (the engine's only oversampling is the 4x true-peak estimator), loudness meters, external side chain.
- **Hush** (`core/MixSuite.*` hush, `HushEditor`): MODE Single vocal / Allround — Allround subtracts
  0.5 dB per dB that the full-band level exceeds the detected band by more than 6 dB, so steady highs in a mix
  trigger less [model: our choice, the reference documents only the intent]; the frequency slider has two handles
  (2–20 kHz log): left = the existing Frequency (where the band starts), right = the new low-pass (where it ends; far
  right = off): detection, listen and split-band processing are band-limited, above the low-pass the signal passes;
  LOOKAHEAD knob 0–15 ms with its power button: the time is the delay and the reported latency (built with the slot
  — `mixsuite::makeHush` — so the delay compensation is right from the first block; moving it re-plans PDC); STEREO
  LINK 0–100 % (per-channel envelopes below 100 %) + MID (detect the mid signal only); SIDE CHAIN In / Ext (Ext
  detects the slot's side-chain source; a menu picks the track); DISPLAY hides the scrolling waveform and the window
  shrinks; In level in the bottom bar (Release moved to the bar too, as the reference has no release knob). Not
  done: oversampling. Pro-DS state translation untouched (vmatch127 owns it).

### 3b. Crest / Sentry / Prism (sub-branch parity128b)
New parameters are appended to each engine (old projects load them at their defaults) and the defaults leave the
output bit-identical: the round-108 goldens (tests/DynamicsTests108.inc) run unchanged. Tests:
tests/Parity128bTests.inc.
- **Crest Compressor ← Pro-C 3**: `Dry` (−∞ … +36 dB on the original's law: 72 dB per unit above 0.25, amplitude
  ∝ v² below; added to the mix, so MIX + DRY = the original's wet / dry pair) — the DRY knob sits under AUTO GAIN as in
  the reference, MIX moved to the bottom bar; `Auto threshold` (rule [model]: effective threshold = the detector
  input's 3 s RMS level + knob + 18 dB, so the default −18 dB sits at the programme's average level; the AUTO switch
  above THRESHOLD); `Link mode` Stereo / Mid / Side / Mid > side / Side > mid (the last four detect and process in
  mid / side; the "x > y" modes apply one channel's gain to the other) in the side-chain panel; `Meter scale` 9 / 18 /
  36 / 72 / 90 dB (the dB box at the display's bottom right; the display's level scale follows it); `Input meter`
  (click IN above the meters). Translated from Pro-C 3 [EXEC layout of round 108]: 2 auto threshold, 17 dry gain
  (relative to the wet gain folded into Output), 26 / 27 link mid-only range + mode, 97 meter scale, 99 input meter.
  Styles stay 11: Smooth / TTM / Op-El / Mastering would need their own measured families (vmatch's domain).
- **Sentry Gate ← Pro-G**: `Channel mode` L/R or M/S (bottom bar; the two detector channels become mid / side),
  expert `SC gain L/R` (−36 … +36 dB) and `SC mix L/R` (−1 … +1: 0 = the channel's own input, towards the first / second
  channel), `Wet pan` / `Dry pan` (balance), `Ducking` (the gate turned upside down: target = range floor / gate gain;
  static curve −range − gate), `Input` (−24 … +24 dB, bottom bar). The expert strip grew a second row (side-chain
  gain / mix, ducking, wet / dry pan). Translated from Pro-G: 12–15, 21, 23, 27, 28.
- **Prism Multiband ← Pro-MB**: `B1…B4 lookahead` (0–20 ms, default 20 ms = the whole global look-ahead as before;
  a band's gain is delayed by global − band look-ahead) as a LOOKAHEAD knob in the band panel; translated from each
  Pro-MB band's look-ahead (base + 12). The gain-scale button cycles 3 / 6 / 12 / 30 dB; the analyser button opens a
  menu (off / pre / post / pre + post, speed very slow … very fast, freeze) — base helpers `specFall` / `specFrozen`.
- Not done (noted in docs/PARITY.md): Crest host-sync / MIDI side-chain triggering, audition triggering, 6 side-chain EQ
  bands, oversampling 16x / 32x; Sentry oversampling; Prism free band placement / 6 bands, per-band expert (side-chain
  filter, external input, stereo link), processing modes, oversampling, analyser resolution / tilt.

### §3c LYNX Tune ← Auto-Tune Pro, Auto mode (sub-branch feature/parity128c)
- Engine (core/LynxTune.cpp, appended parameters, all neutral at their defaults so saved projects play unchanged —
  test `lynx_tune_round128_params_are_neutral_by_default`): **Natural vibrato** −12…+12 dB (the sung pitch's vibrato,
  measured against the pitch smoothed by two 80 ms one-poles, is scaled before the correction: +6 dB doubles it,
  −12 dB keeps a quarter); **per-note Bypass** (12 switches: a bypassed note stays a target but passes as sung, while
  a Removed note — the existing note switches — retargets to the next one); **Ignore vibrato** (targets chosen from
  the smoothed pitch, so a wide vibrato no longer flips to the neighbour note); **Create vibrato** (none / sine /
  square / sawtooth, 0.1–10 Hz, 0–100 c, onset delay and onset rate 0–1500 ms from each note's start); **More
  scales** (whole tone, diminished — a separate parameter because the Scale parameter's steps are saved in projects).
- AUTO KEY (core/VocalPitch.cpp `detectKey`): Krumhansl–Kessler major / minor profiles against the time per pitch
  class sung while the editor is open (fading over ~30 s).
- Editor (core/PluginLayout.cpp lynxTune, ui/PluginEditors.cpp): the original's arrangement — Auto Key under Scale;
  a MODE group (low latency, MIDI targets); formant / shift / transpose / A4 / mix on the right of the top bar;
  RETUNE SPEED, FLEX-TUNE, the ring (click = HOLD), NATURAL VIBRATO, HUMANIZE; a Pitch graph | Advanced page pair
  (Advanced = Create Vibrato + Ignore Vibrato); a four-octave keyboard at the bottom with BYPASS / REMOVE and LATCH
  (latch off = the edit lasts while the key is held). The Scale menu lists the two extra scales after a separator.
- Translation (PluginSubstitutes.cpp, inside translateAutoTunePro + `autoTuneProNoteEdits`): X62 natural vibrato,
  X44 ignore vibrato, X5/X7/X6/X46/X50 created vibrato, and the active scale's legacy `<Scales>` Bypass / Remove
  digits (one per scale degree: 7 for major / minor, 12 for chromatic, from the root) [DATA].
- Not done: Classic mode, Throat length (LYNX keeps its formant shift in semitones), the created vibrato's
  amplitude / formant / variation, per-note scale detune, historical tunings, single-octave keyboard edits,
  Learn scale from MIDI, Graph mode, Harmony Player.

## Where I stopped
- Done: §1–§3 (+ §3a–§3c from the sub-branches, merged), docs/PARITY.md with before / after scores, the Desktop page
  (`LYNX-paridade/FabFilter-Slate-Antares.html`: trial | LYNX before | LYNX after). build-local with the FL Data dir:
  1 426 990 checks, 0 failures (merge of all four branches).
- Left (biggest first): the shared header (undo / redo, A/B, presets — round 129's PresetBar, then wire it into Facet's
  own editor if parity129 did not); Prism free band placement / 6 bands / phase modes; Facet 24 bands, per-band
  attack / release, Flat Tilt / All Pass / Brickwall (needs a value remap of the shape and slope parameters), Character;
  Apex oversampling + loudness meters; Crest Host-Sync / MIDI triggering, 6 side-chain EQ bands; LYNX Tune Classic
  mode, throat length, scale detune; Ember 6 bands, 28 styles (needs measured models — vmatch), modulation system;
  Decapitator / Soundtoys (not installed).
