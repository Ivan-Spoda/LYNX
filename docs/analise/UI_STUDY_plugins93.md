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

## Where I stopped / next steps
- Batch 1 (landed): the effects above, Parametric EQ 2, Gross Beat, 3x Osc and FL Keys pages.
- Next: Maximus / Soundgoodizer (state-based: amount and A–D), Fruity Balance / Send, BooBass, Sytrus and Harmor main
  pages, then the remaining Fruity effects (Delay 2, Reeverb, Flangus, Soft Clipper, Blood Overdrive, PanOMatic, Stereo
  Shaper, Transient Processor, Fast LP, Free Filter, WaveShaper, Delay Bank, Hyper / Vintage Chorus, Vintage Phaser…),
  whose FL captures are in `tmp/r93/flref` (second batch).
