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

## Where I stopped
- Done: §1, §2, docs/PARITY.md (tables + before scores), Desktop page.
- Next: shared editor header (undo / redo, A/B, presets), then per-engine gaps in the order of §2.
