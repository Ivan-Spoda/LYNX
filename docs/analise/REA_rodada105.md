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
| Vocodex | its 20 controls, hold mode, order, unison, bands, draft, MOD / CAR inputs, the eight switches | patched in place at the controls block of FL's state (`vocodex::State::paramsAt`), the install-data trailer kept |
| Fruity Convolver | predelay, self-convolution blend, stretch, EQ amount, normalise | knobs 1…4 and flag 3 of the FL state inside the slot blob |

Vocodex ranges are FL's form values of REA_rodada20 §4.2.2; attack / release curve (0…2000), mod band width (0…4000),
unison pitch (0…1000) and unison pan (0…256) are **not confirmed** (chosen around the installation presets' values:
Default.fst has 0, 0, 2000, 250, 128). Vocodex's 2026 build creates its panel in code (no form resource), so its
control positions come from the capture only.

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

| Pitch Shifter | 744 × 280 | VOICE / MUSIC side tabs (the engine switch, `Granular`), the pitch graph (VOICE: an X/Y pad formant × pitch, **not confirmed** as FL's pad mapping); VOICE: PITCH, FORMANT faders, FORMANT CORRECTION, PITCH ANALYSIS (relative / C4…C6, fast / accurate, left / right / both); MUSIC: PITCH, DURATION faders, DENSITY, JITTER, RAND; DELAY (delay and feedback faders, T, OFF / POST / PRE / NO SHIFT); MIX. Value plates above the faders |
| Effector | 436 × 412 | input and output GAIN on the title strip; X PARAM, Y PARAM, BYPASS, DRY/WET; X/Y pad; the tempo divisions 8/1…1/8 (Mod rate); X MOD, Y MOD, LFO shape, TEMPO; the twelve effect buttons |
| Hardcore | 772 × 475 | INPUT, OUTPUT; EQ on + 8 band faders (125 Hz…16 kHz); CABINET (none, model A…T); five pedal boxes: LED (on), the knobs of the selected pedal with that pedal's captions (read off FL's pedal artwork: e.g. Distortion = DIST, COMB, INFERNO, GLASS; Phaser = SPEED, TUNE, WIDTH, INTENSITY), pedal type menu. FL's .hdprg preset browser is not reproduced. Hardcore's three unused parameters are now named `(unused 12/38/44)` (they all were `(unused)`) |
| Vocodex | 598 × 600 | Mixer (WET, SG, MOD, CAR, +NOISE, the MOD HP / CAR LP / NOISE HP knobs, MOD / CAR input numbers, L-R ENCODING), Contour (from car / consonants, to mod / noise), DRAFT, THREADED; envelope follower (HOLD, ATT, REL, their curves, PEAK / RMS, MIN TIMES, freeze), ORDER, band width, mod band width, mod pitch, unison, flatness, unison pitch / pan, bands; the band gain map (read-only) and the carrier. FL's map editor and carrier patch browser are not reproduced |

Also: `flPluginsForEffect` now names Fruity Vocoder, Vocodex, Effector and Frequency Splitter, so their windows' presets
bar lists the installation's FL presets.

## Where I stopped / next steps
- Done: state-param tables (Maximus, Delay Bank, Multiband Delay, EQUO, Convolver, Vocodex) + live edits, the ten
  arrangements above, tests (`tests/PanelsTests105.inc`), pt-BR strings.
- Fruity Phaser and Fruity Send engines: separate pieces (see their notes).
- Open: EQUO / Multiband Delay band modes (the per-band lock / off caps), EQUO's send target, Vocodex map editing,
  Convolver's EQ page and IR editing, Hardcore presets (.hdprg).
