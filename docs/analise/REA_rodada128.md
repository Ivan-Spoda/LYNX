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
(filled in per batch)

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

## Where I stopped
- Done: §1, §2, docs/PARITY.md (tables + before scores), Desktop page.
- Next: shared editor header (undo / redo, A/B, presets), then per-engine gaps in the order of §2.
