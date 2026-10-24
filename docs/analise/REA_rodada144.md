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

LYNX (all appended after `lofi`, Even = round 137's measured law, so older projects sound the same):

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

The slot's parameter array grew from 96 to 128 values (`maxEffectParams`, Song.h): Echoform now has 101
parameters. Saved projects store parameters by index, so older files load unchanged.

## 3. Hush De-esser: DeEsser's Attn meter hold

The GR bar now holds its deepest reduction (a line and the number at the foot of the bar); click it to reset
(the reference's infinite peak hold).

## Where I stopped

- Note for round 138 (vox138): its `tapeVoicing` is appended right after `lofi`, where this round put
  `rhythmEdit` (round 144's parameters: `rhythmEdit` … `loopTreble`, indices 75 … 100). Whichever lands second
  appends its parameters after the other's last one.
