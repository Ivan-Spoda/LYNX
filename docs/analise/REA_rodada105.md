# Round 105 — FL arrangements for the remaining native plugin windows

Follows docs/analise/UI_STUDY_plugins93.md (method, layout system, §3 "adding a plugin page"). Clean-room: positions
measured on FL Studio 2026 captures (kept in `tmp/r93/flref`, `tmp/r105/flref`, never in the repository); LYNX draws
everything with its own vector controls. Fruity Phaser and Fruity Send engines: separate notes
(`REA_rodada105_phaser.md`, `REA_rodada105_send.md`).

## 1. Controls stored in the slot state (`core/StateParams.{h,cpp}`)

FL's Delay Bank, Multiband Delay, EQUO, Convolver (and Maximus, round 93) keep most settings in the saved plugin state,
which LYNX carries in `EffectSlot::state`. Round 93 edited Maximus' state through a Maximus-only table; this round
generalises it: `stateparams::tableFor (kind)` lists each state control (name, FL raw range and default) with `get`,
`with` (the state with one value replaced, clamped) and `text`. The editor numbers them after the effect's own
parameters (`plugui::paramIndex`, `stateIndex`, `stateValue01`, `stateRaw`), so layouts name them like any parameter.
State controls are not automatable.

| Effect | State controls | Index layout |
|---|---|---|
| Maximus | 59 (unchanged) | as `maximusfx::stateParams` |
| Fruity Delay Bank | 8 banks × (16 controls + 11 options) + solo + oversampling | bank b from 27·b: controls `4 + 16·b + k`, then filter 1/2 type and order, filter 1 post, mode, steps, shape, on, tap, sync |
| Multiband Delay | 8 banks × 3 targets × 16 bands + target + bank | FL's own `bank·48 + target·16 + band` |
| EQUO | 9 banks × 31 bands × 3 targets (thousandths of FL's 0…2 cells) | FL's table order `(bank·31 + band)·3 + target` |
| Fruity Convolver | predelay, self-convolution blend, stretch, EQ amount, normalise | knobs 1…4 and flag 3 of the FL state inside the slot blob |

Unknowns: the upper end of Delay Bank's DIV (pattern steps) fader is **not confirmed** (0…16 used; the installation's
presets use 0…13).

### Live edits
`Effect::acceptState (bytes)` (new, default false): when only a slot's state changed, `RackRenderer::build` offers the
new state to the running effect instead of rebuilding it, so delay lines and filters keep running while a control is
dragged. The hand-off is a single-slot lock-free mailbox (`core/StateHandoff.h`) taken at the start of `process()`.
- Delay Bank: bank controls 4…130 go through the engine's `setControl` (FL's ProcessParam); option, solo or
  oversampling changes rebuild the engine (as FL re-initialises the bank).
- Multiband Delay: the cells whose engine value changed go through `setParameter` (FL's parameter path).
- EQUO: `Engine::setBands` takes the new table and recomputes the band targets (they glide through the smoother).
- Convolver and Maximus rebuild (their kernels / presets are derived from the whole state).

## 2. Arrangements (FL Studio 2026, measured)

| Plugin | Body | Arrangement |
|---|---|---|
| Fruity Vocoder | 416 × 306 | FREQ (FORM fader, MIN, MAX, SCALE, BW, INVERT) · ENV (ATT, REL) · MIX (L / R input buttons = swap, three listen LEDs = modulator / carrier / mix, MOD, CAR, WET faders); band display with HOLD, BANDS 4…128, FILTER 1…3. Side inputs in the options menu |
| Frequency Splitter | 776 × 340 | band graph; CROSSOVER (slope stepper, FREQ, BANDS 3, FREQ, slope) · MAIN OUT (low / mid / high level + switch) · SENDS (output switch, level, mute per band); LINEAR PHASE, PRECISION, SPEED. Solos in the options menu (FL: behind the arrow buttons, not confirmed) |
| Fruity Delay Bank | 583 × 278 | bank LEDs (on) and tabs 1…8 (pages), OVERSAMPLE, DRY / WET / IN / FB; per bank IN · FILTER (type menu, sections 1–3, POST, GAIN, CUT, RES) · FEEDBACK (tap, T, OFF / NORM / INV / P.PONG, TIME / OFS / SEP faders, PAN, VOL) · FB FILTER · GRAIN (DIV, SH) · OUT (NEXT, PAN, VOL) |
| EQUO | 572 × 245 | graphic EQ bars (31 bands of the selected bank and target, drawn by dragging), BANK M / 1…8, VOL / PAN / SEND; MORPH (SHIFT, MIX, MORPH, SMOOTH, VOL), SETUP (SEND ONLY, ADD MODE, OVERSAMPLE, BW) |
| Multiband Delay | 568 × 292 | 16 bars (selected bank / target, drawn by dragging) with the crossover frequencies; DELAY / VOL / PAN, BANK 1…8; GENTLE / STEEP / LINEAR PHASE · SCALE (×0.1, harmonic, weighted), FEEDBACK · SMOOTHING TIME, KEEP PITCH · MORPH · WET (COMP / SAT / CLIP), HP · MIX |
| Fruity Convolver | 722 × 300 | dry, in (separation), wet, wet (separation), delay, self-conv, stretch, eq; NORMALIZE; impulse info and waveform. FL's sample-editor toolbar and its equalizer page are not reproduced |

## Where I stopped / next steps
- Done: state-param tables + live edits, six arrangements above, tests (`tests/PanelsTests105.inc`).
- Waiting for FL captures (`tmp/r105/grab105.sh`): Pitch Shifter VOICE page, Hardcore, Effector, Vocodex.
