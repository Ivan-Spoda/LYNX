# UI study, round 93 — native plugin windows in FL's arrangement

Goal (user request): every native LYNX plugin opens a window that **looks and works like the FL Studio plugin it is
compatible with** — same controls, same arrangement and sections, same captions, the same interactions (knob drag,
right-click menu, presets bar) — so an FL user finds everything where they expect it, while the drawing stays LYNX's
own (dark surfaces, red accent, own vector knobs, sliders and switches; no FL bitmap, skin, font, logo or icon).

Method:
- **[EXEC] FL captures.** Each plugin was opened in FL Studio 2026 (v26.1.7 build 5654) from the installation's own
  `Default.fst` preset (or the first preset of its folder) in a fresh FL instance, and the plugin window was cropped by
  diffing against an empty FL screen (`tmp/r93/fresh.sh`, `py/win.py`). Pages that are hidden at first (Fruity Limiter's
  COMP page, LuxeVerb's feedback pitch mode) were captured after clicking them. The captures stay in `tmp/r93/flref`
  (never in the repository).
- **[DECOMP]** The effect DLLs (`Plugins/Fruity/Effects/*/*_x64.dll`) carry no form resources for their panels (their
  RCDATA holds only the shared wrapper forms: `TSynthEditorForm`, `TNameEditForm`, `TPaletteEditorForm`…; read with
  `tmp/r93/py/pres.py`), so the control positions are **built in code** and were measured on the captures: one design
  unit = one pixel of FL's window at 100 % zoom, origin at the top-left of the plugin body (under FL's wrapper title).
  Ranges, units and value texts are not re-derived here: they are the ones the exact engines already use (rounds 48–73,
  `formatEffectParam`), checked against the captures' read-outs (e.g. Fruity Compressor `0.0 dB`, `1.0 : 1`,
  `15.0 ms`, `200 ms`, `Hard`; LuxeVerb `6.78 s`, `955 Hz`, `+12.0 st`).

## 1. The layout system

`src/core/PluginLayout.{h,cpp}` (no JUCE, tested): a `Layout` is FL's body size plus a list of items — `section`
(titled panel or plain cell), `label`, `knob`, `hslider`, `vslider`, `radio` (column of options), `tabs` (segmented
row), `toggle`, `stepper` (number box with arrows), `value` (read-out), `display` (a graph). Value items name the
effect parameter they edit by its `EffectInfo` name, so layouts never depend on indices. Items can belong to a page
(FL's LIMIT | COMP) or be shown only while another parameter is in a range (LuxeVerb's pitch mode, the tempo-synced
delay knobs that replace the free ones while **T** is on).

`src/ui/PluginEditors.{h,cpp}` draws a layout at any scale (windows open at 125 % and resize) with the LYNX control
contract on every control: drag (Ctrl = fine), wheel, Alt+click / middle-click / double-click = default, right-click =
type value, automation clip, MIDI link; value tip while dragging; hint bar text. The **presets bar** on top follows
FL's wrapper: options arrow (presets, reset, the plugin's hidden switches, bypass), bypass LED, plugin and insert name,
the preset name (click = the FL presets of that plugin found in the user's installation) and previous / next arrows.
Effects without a studied arrangement get a generated grid of knobs, so every native effect has a window.

Opening: double-click an effect's slot in the Mixer (FL opens the window on a click), or the **Editor** button on the
title row of the selected effect's knobs.

## 2. Arrangements (FL Studio 2026, measured)

| Plugin | Body (units) | Arrangement |
|---|---|---|
| Fruity Limiter | 633 × 306 | display across the top (623 × 158) with its view strip; LOUDNESS / ENVELOPE / NOISE GATE header strip at y 161; GAIN, SAT; inset panel with CEIL, ATT, REL, SUSTAIN and two CURVE steppers (LIMIT page) or THRES, KNEE, RATIO, ATT, REL, SUSTAIN, one CURVE stepper and SIDECHAIN (COMP page); LIMIT / COMP tabs at the bottom-left of the inset; gate REL, GAIN, THRES |
| Fruity Compressor, Filter, Chorus, Flanger (FL's generic "Fruity" panel) | 501 × (50·rows + 5) | cells of 160 × 45 in three columns, 165 / 50 apart: knob at the left, caption on a plate, the value (or a slider for Fruity Filter) under it; Filter's X2 / CENTER are half cells with Off / On |
| Fruity Fast Dist | 386 × 122 | PRE, THRESH, A/B switch, MIX, POST on the left panel; transfer curve on the right |
| Fruity Stereo Enhancer | 405 × 116 | STEREO SEP knob; PRE / POST, PHASE OFFSET knob, INVERT none / left / right; PAN and VOL |
| Fruity Delay 3 | 752 × 220 | INPUT (wet fader) · DELAY TIME (TIME, TEMPO SYNC, KEEP PITCH, SMOOTHING slider, OFFSET) · DELAY MODEL (mono / stereo / ping pong / off, STEREO) · FEEDBACK (LEVEL, CUTOFF, RES, LP / HP / BP / off, SMP RATE, BITS) · OUTPUT (WET, TONE, DRY); MODULATION (RATE, TIME, CUTOFF) · DIFFUSION (LEVEL, SPREAD) · FEEDBACK DISTORTION (limit / sat, LEVEL, KNEE, SYMMETRY, curve) |
| Fruity Multiband Compressor | 805 × 160 | input meter, IIR / FIR, LIMITER, GAIN; band graph with four crossover knobs; LOW / MID / HIGH BAND each with A·M·B mode, THRES RATIO KNEE / ATT REL GAIN and its meters |
| Fruity Reeverb 2 | 538 × 166 | room view; MID / SIDE, H.CUT, L.CUT; DELAY + TEMPO, SIZE, MOD, DIFF, SPEED; BASS, DEC, CROSS, DAMP; DRY / ER / WET faders and SEP |
| LuxeVerb | 947 × 456 | INPUT · REVERB · PITCH · OUTPUT tiles (value plate, knob, caption), the PITCH section switching between FEEDFORWARD (direct amp fader, pitch / amp / delay ×2) and FEEDBACK (high / low cut, pitch-shift and gain faders, delay, reverb mix); ENVELOPE (off / wet / decay, threshold, scale, low cut, offset, sidechain, attack, decay, smooth) and the analyser |
| Fruity Parametric EQ 2 | 707 × 356 | response graph (C1…C9 octave plates and SUB / BASS / LOW MID / MID / HIGH MID / PRS / TREBLE plates above, 20 Hz…10 kHz under it, ±18 dB scale at its right) with numbered band handles; the output fader; seven band columns (type button with the band's shape, gain fader, FREQ and BW knobs); FL's bottom row (LIN, HQ, monitor, compare) is replaced by the selected band's read-out |
| Gross Beat | 840 × 563 | time panel (mix knob), HOLD, scratch dial; 36 time slots and 36 volume slots in 4 × 9 grids (names from the saved state); ATT / REL / TNS; volume panel; envelope editor (the selected time and volume slots, read-only here) with the slot's hold / trigger sync / position sync under it |
| 3x Osc (channel page) | 560 × 390 | three oscillator rows: shape buttons (sine, square, rounded, custom / triangle, saw, noise, ± invert), PHASE OFS and DETUNE faders, COARSE, FINE, pan, volume (osc 2 and 3); AM OSC 3, HQ, PHASE RAND below |
| FL Keys (channel page) | 480 × 186 | ENVIRONMENT (decay, release, pan, stereo), MISC (overdrive, LFO, treble, stretch), VELOCITY (vel → muffle, muffle, vel → hardness, hardness, sens), TUNING (tune, detune), the sample set's name; FL's keyboard strip is left out (LYNX's touch keyboard is in the Channel Rack) |
| Fruity Delay 2 | 404 × 102 | INPUT (pan, vol) · FEEDBACK (normal / invert / ping-pong, vol, cut) · TIME (time, offset) · DRY |
| Fruity Reeverb, Free Filter | Fruity cells | two columns of five (Reeverb); Free Filter: TYPE, FREQ, CENTER / Q (slider), GAIN |
| Fruity Flangus | 366 × 176 | the order bar and seven faders (depth, speed, delay, spread, cross, dry, wet) in columns |
| Fruity Soft Clipper | 274 × 133 | THRES, POST, name plate, meter strip, transfer curve |
| Fruity Blood Overdrive | 428 × 186 | PRE BAND, COLOR · big PRE AMP with x100 · POST FILTER, POST GAIN, captions on plates |
| Fruity PanOMatic | 307 × 166 | PAN, VOL; LFO shape buttons, AMT, SPD, off / pan / vol; the pan-volume pad |
| Fruity Stereo Shaper | 276 × 316 | mixer matrix of four faders (R→L, left, right, L→R) over LEFT / RIGHT; pre / post, DELAY, PHASE, in/out difference |
| Transient Processor | 553 × 325 | in / out meters, SPLIT FREQ, ATTACK and RELEASE panels with their shape controls, DRIVE, SPLIT BALANCE, GAIN, off / on, the view |
| Fruity WaveShaper | 277 × 358 | the saved shaping curve (read-only here), bipolar / unipolar, PRE, MIX, POST, smooth, oversample, HQ, center |
| Vintage Chorus | 754 × 214 | MODE (I / II / edit) · DELAYS (time 1 / 2, feedback, high-pass faders) · MODULATION (start phase, wave, L/R phase, tempo sync, speed) · LEVELS (mono input, invert wet, noise gate, noise, gain, mix) |
| Hyper Chorus | 754 × 214 | three movement pads (delay / feedback, modulation / rate, movement / rate; drag sets both), the voices view, one row of knobs and the delay-lines switch |
| Fruity Balance | 232 × 150 | Balance and Volume knobs (captions above, values under them) around the meter |
| Soundgoodizer | 215 × 300 | one big Amount knob, presets A–D under it; LYNX edits the slot's Maximus container (Amount = band mix, the preset = the installation's `Data\Presets\a…d.dat`) |
| BooBass (channel page) | 343 × 154 | BASS, MID, TREBLE stepping down along the body |
| Sytrus MAIN page | 850 × 346 | VOL / LFO / PITCH faders, VOL and FILTER ADSR faders, UNISON (order, pan, volume, pitch, sub, phase, envelope variation), MODULATION X/Y pad and knobs, QUALITY (HQ envelopes and oversampling, draft / render), PORTA; the operator / filter / FX pages and the FM / RM matrix are not editable in LYNX and say so |
| Harmor (channel page) | 1000 × 600 | the controls LYNX edits at their FL places on the part page (timbre mix, blur, tremolo, harmonizer, unison order and pitch, vibrato, part volume, filter 1 freq / res, pluck, phaser mix) and the global section (volume, part A / B); FL's section titles for orientation |
| Fruity Squeeze | 282 × 223 | SQUARIZE amount and PUNCHER (preserve, impact, relation, amount) on plates; FILTER (on, type, freq, res, pre / pos); MIX and GAIN faders. SQUARIZE AMOUNT is mapped to the engine's `Steps` control — **not confirmed** |
| Maximus | 633 × 391 | band curve (the selected band's drawn curve) and the bands view on top; LOW / MID / HIGH / MASTER selector; per band: mode (on / comp off / muted / off), PRE / POST gain, stereo separation, saturation THRES / CEIL, ATT, REL, SUSTAIN, two CURVE steppers, PEAK / RMS, REL 2; LMH DEL, LMH MIX; crossover LOW / HIGH frequency with 12 / 24 dB slopes; LOW CUT. Edits the 59 parameters of the slot's Maximus state (REA_rodada8 §1.2); the curve points, SOLO, LIN and the monitor options are not editable |
| Vintage Phaser | 754 × 214 | PHASING (min / max faders, HQ, manual, feedback) · MODULATION (start phase, type, L/R phase, tension, speed + T) · DELAY (on, mix, stereo, big time + T, feedback, LP, HP, keep pitch) · LEVELS (noise, noise gate, mono input, mix); the engine's other controls (manual freq., rates, depth scale, pull-down, drive, colour, HP filters, engine V2, invert feedback) on an ADVANCED page — where FL shows them is **not confirmed** |

Mapping notes:
- 3x Osc's shape buttons follow FL's order (sine, square, rounded, custom on the first row; triangle, saw, noise on the
  second) and set the engine's shapes 0 sine, 1 triangle, 2 square, 3 saw, 4 rounded, 5 noise, 6 custom
  (REA_rodada47 §1.2); the ± button is the oscillator's invert switch. Captions sit between the rows as in FL (the third
  row has none).
- Fruity Limiter COMP page: FL captions the compressor detector window **SUSTAIN** like the limiter's peak window; it is
  mapped to the engine's RMS window (p14) — by analogy with the LIMIT page's SUSTAIN = peak window (p7), **[model]**.
- LuxeVerb's PITCH header switch is the engine's `Feedback` control (on in the "Exact Mass of Jupiter" preset whose
  pitch section FL draws enabled; off and greyed in `Default`) **[EXEC + DATA]**.
- Fruity Chorus' LFO wave knobs are continuous in FL (the engine reads `x·3`), so they are not stepped.
- Switches that FL keeps out of the panel (Fruity Limiter's exact ceiling, Fruity Compressor's look-ahead) are in the
  presets bar's options menu.

## 3. Adding or changing a plugin page (notes for other plugin agents)

Everything below is reusable as is; a new page is data in `src/core/PluginLayout.cpp` plus, only when FL draws
something special, a display in `src/ui/PluginEditors.cpp`.

1. **Capture FL.** Take `flock ~/.cache/lynx-fl.lock` (release it as soon as the captures are done, other agents use
   FL too), open the plugin's `Default.fst` (or first preset) with `tmp/r93/grab.sh <name> <fst>` (uses
   `fl.sh` and `/mnt/c/Users/kauan/lynx-test/flref/cap.ps1`), keep the PNG in `tmp/<round>/flref` (never in the repo).
   Measure at 2× with `tmp/r93/py/grid.py` (draws a ruler in FL pixels). Body origin = top-left under FL's wrapper
   title: subtract about 2 px in x and 25–27 px in y from the window capture. One design unit = one FL pixel at 100 %.
2. **Write the layout** as a function `Layout <plugin>()` with the builders at the top of `PluginLayout.cpp`:
   `section`, `label`, `knob (cx, cy, diameter, param, caption, captionY, style)`, `slider (Item::vslider|hslider …)`,
   `radio`, `tabs`, `pageTabs`, `toggle` (`boxed` = a lit letter button such as FL's **T**), `stepper`, `value`,
   `display`, and the modifiers `onPage (item, page)` and `when (item, param, lo, hi)` (shown only while another
   parameter is in a range). Generic FL "Fruity" panels: `fruityGrid` + `fruityCell` / `fruityHalfCell`.
   Register it in `layoutFor` and in `flLayoutKinds()` (generators: `instrumentLayout` / `instrumentLayoutKinds`).
3. **Name parameters, not indices.** `param` is the `EffectInfo` name of the effect's parameter (`paramIndex`).
   Effects whose controls live in the slot's state instead of `EffectSlot::params` declare it in
   `plugui::usesStateParams` and expose a table like `maximusfx::stateParams()` (name, raw min / max / default),
   `stateParam`, `withStateParam` and `stateParamText`; the editor then reads and writes the state (Maximus is the
   example; such controls are not automatable).
4. **Every parameter must have a control.** The test `plugin_layouts_name_real_parameters` fails when a parameter of
   an FL-arranged effect has no control and no entry in `menuSwitches` (on / off switches FL keeps in a menu: they go
   to the presets bar's options menu) or `editedElsewhere`. When FL keeps controls out of its panel and their place is
   not known, put them on a second page (`pages` + `pageTabs`, e.g. Vintage Phaser's ADVANCED) and say so.
5. **Captions** are English inside `TR_NOOP(...)`; every new caption and every `"A|B|C"` option string needs a line
   in `resources/i18n/pt-BR.txt`, then `python3 -I tools/make_i18n.py` (tests check both).
6. **Look at it.** `LYNX.exe --plugui <folder>` writes one PNG per page of every FL-arranged editor from FL's default
   preset; `tmp/r93/shots.sh <changed sources>` rebuilds (`tmp/r93/qbuild.sh`), renders and writes FL | LYNX
   side-by-side images to `tmp/r93/compare` (add the pair to `py/compare.py`).
7. **Interactions come for free:** drag (Ctrl fine), wheel, Alt/middle/double-click reset, right-click menu (type in,
   automation clip, MIDI link), value tips, hint bar, presets bar with the installation's FL presets, bypass, reset.
   Vertical `tabs` (taller than wide) draw as a column with band colours (Maximus' band selector).

## Where I stopped / next steps
- Landed in batches 1–3 (merge of feature/plugui93, 2026-10-17); batch 4 below.
- Batch 4 (2026-10-17): Maximus (state-backed controls), Vintage Phaser; the layout notes (§3).
- Not done: Fruity Send and Fruity Phaser (mapped to LYNX's generic utility / chorus, not FL's controls: they need an
  engine with FL's parameters first), Newtone (pass-through placeholder), Delay Bank, Vocoder, Pitch Shifter, Convolver, EQUO, Multiband Delay, Frequency Splitter, Hardcore,
  Effector (captures of most in `tmp/r93/flref`; they keep the generated grid of knobs).
