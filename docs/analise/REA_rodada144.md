# REA round 144 (parity144): docs/PARITY.md, the next tier

2026-10-24. Continues round 137 (`REA_rodada137.md` "Where I stopped"): EchoBoy's rhythm editor, UberMod's DelaySync
translation, Pro-Q 4's linear-phase resolution, Pro-MB's oversampling / processing modes, then the next lowest pairs.
Sub-rounds: 144a (Lattice Mod DelaySync, Facet EQ linear-phase resolution — `REA_rodada144a.md`), 144b (Prism
Multiband — `REA_rodada144b.md`). Ember, PanMan / Auto-pan and LYNX Tune were left alone (round 138 owns them).

Evidence markers: [EXEC] run here on the official trial, hosted black-box in `tools/fxmeasure` (public VST3 API:
parameters, value texts, audio in / out; editor screenshots as a user sees them); [MANUAL] vendor manual / spec
dossier; [model] LYNX's own choice. Nothing was decompiled; no graphics, presets or names were copied. Raw data:
`D:\lynx-work\parity144\` (eb = EchoBoy).

## 1. Echoform: EchoBoy's rhythm editor

[EXEC] On the EchoBoy trial (5.5.5, Rhythm echo, Digital Delay style, 120 BPM), its host parameter RhythmMode lists
Time / Note / Dotted / Triplet / Pattern / CustomTime / CustomBeats. With only host parameters set, Pattern,
CustomTime and CustomBeats give **no echo at all** (21 impulse runs over repeats 4 / 8 / 16, 1/8 and 1/16, decay
0 / 20 dB: silent outputs): their taps live in the plug-in's rhythm editor (not host parameters), so they cannot be
measured black-box without driving its GUI; the screenshot `eb/ed_pat_c.png` shows the editor opens behind TWEAK.
[MANUAL] (docs/specs/echoboy.md §): up to 16 taps on a 1–8-beat pattern, a grid of ½ … 1/64, a Shape menu (Decay,
Reverse, Swell, Fade, Nonlin), pattern presets.

LYNX (all appended after round 136's `tapeVoicing`, Even = round 137's measured law, so older projects sound the same):

| Parameter | Values | Law |
|---|---|---|
| Rhythm edit | Even / Free / Grid | Even: round 137's evenly spaced slots; Free: each heard tap at its own time (a fraction of the pattern length, so it follows the tempo); Grid: that time snapped to the grid note, never onto the pattern's start or end |
| Rhythm grid | 1/2 … 1/64 (default 1/16) | the Grid edit's step |
| Rhythm shape | Decay, Reverse, Swell, Fade, Nonlin | [model] how Decay spreads over the slots u = 0 … 1: −D·u; −D·(1−u); −D·|1−2u|; a straight fade in level to −D; −D·u³ — still equal-power normalised (the measured normalisation) |
| Tap 1 … 15 time | 0 … 100 % of the pattern | default k / 16 |

The editor is a new row under the panel ("Rhythm editor", shown with Rhythm on): the taps on the pattern's time
axis with beat lines and the grid; drag a tap up / down = its level, sideways = its time (a sideways drag in Even
turns the pattern into Free taps at the same times first); double-click empty space = a new tap, double-click a tap
= remove it (Free / Grid) or its level back to 100 % (Even); right-click or **Patterns ▾** = 12 LYNX-authored
patterns (Gallop, Offbeat eighths, Dotted bounce, Tresillo, Son clave 3-2, Rumba clave, Ghost sixteenths, Stutter,
Build-up, Triplet roll, Swing eighths, Long and short — traditional figures written as grid steps; nothing taken
from any product), "Even spacing" and "All taps at 100 %". The small echo train in the Delay section shows three
passes of the pattern, as in round 137.

## 2. Echoform: EchoBoy's Style Edit role

With Rhythm off the same row holds **Style edit** [MANUAL roles, model laws]: Input (−24 … +24 dB, the echo path
only), Bass / Mid / Treble inside the loop (120 Hz shelf, 1.2 kHz bell Q 0.7, 5 kHz shelf, ±18 dB: every repeat is
shaped again), FB bal. (±100 %, each side's feedback, Dual's FB Balance), Diffusion Loop / Post (Post: the echoes
diffused on their way out, the loop stays clean), Wobble shape (sine, triangle, square, saw, random) and Sync (free,
1/16 … 2 bars) for the LFO modes. Every default is the old sound (tested: identical output).

The slot's parameter array grew from 96 to 128 values (`maxEffectParams`, Song.h): Echoform now has 102
parameters. Saved projects store parameters by index, so older files load unchanged.

## 3. Hush De-esser: DeEsser's Attn meter hold

The GR bar now holds its deepest reduction (a line and the number at the foot of the bar); click it to reset
(the reference's infinite peak hold).

## 4. PanMan's Feel / Groove (measured, not implemented)

[EXEC] PanMan 5.5.5 trial: Feel and Groove are host parameters (±0.25 of a step, default 0), Rate is 0.01 … 10 Hz
(0.01 · 1000^v). In the mode reachable through host parameters alone (the free LFO; PanMan's rhythm modes are
chosen in its editor, not by a host parameter), a 1 kHz tone at 1 Hz gives bit-identical outputs for Feel ±0.25 and
Groove ±0.25: they act only in the rhythm modes, which cannot be driven black-box without its GUI. Auto-pan's
Feel / Groove row stays ❌ (it would reuse EchoBoy's measured law, round 135w, as a model).

## 5. Sub-rounds

- 144a (`REA_rodada144a.md`): Lattice Mod's DelaySync measured (18 steps at k / 17: free, 1/16 … 2/1, dotted,
  triplet; synced delay capped at 2750 ms) and translated; Facet EQ's linear-phase Resolution (Low … Maximum, the
  trial's latencies 3072 … 66560 samples at 48 kHz), translated from Pro-Q 4 states.
- 144b (`REA_rodada144b.md`): Prism Multiband's Processing (Dynamic / Linear / Minimum phase), Oversampling off /
  2x / 4x, mid / side band pan, analyser resolution / tilt — measured on the Pro-MB trial, translated.

## Where I stopped

- Not done: Prism's Dynamic phase still the old engine (it differs from Pro-MB once a band works, 144b); Facet's
  resolution change by automation waits for the next prepare(); PanMan Feel / Groove (above) and its Tweak page;
  EchoBoy's saturation types, ~30 style voicings, PrimalTap's Multiply; Glow's ANALOG hum (the Waves shell hung
  in our host this round: CLA-2A not loaded, nothing measured); Ridge's auto make-up / ANALOG; Ember's M/S and
  drive pan; Waves Tune RT keyboard navigation (rounds 138 / 145 own the sound of those).
- Round 138 (vox138) landed first with `tapeVoicing` at index 75 (right after `lofi`); round 144's parameters
  follow it: `rhythmEdit` … `loopTreble` = indices 76 … 101 (Echoform: 102 parameters).
