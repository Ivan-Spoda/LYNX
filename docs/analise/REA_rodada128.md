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

## Where I stopped
- Done: §1, §2, docs/PARITY.md (tables + before scores), Desktop page.
- Next: shared editor header (undo / redo, A/B, presets), then per-engine gaps in the order of §2.
