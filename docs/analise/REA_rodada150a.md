# REA round 150a (parity150a): Echoform ← EchoBoy (Dual / Ping-Pong, L/R offset, wobbles); Auto-pan ← PanMan Tweak; Foil ← Little Plate

2026-10-24. Sub-round of parity150 (docs/PARITY.md, tier 4; round 147a's "Where I stopped": the chorus / "fat"
styles' several voices, the styles in Dual / Ping-Pong). Evidence markers: [EXEC] measured here on the official
EchoBoy 5.5.5 trial (Soundtoys, until 2026-11-22), hosted black-box in `tools/fxmeasure` (public VST3 parameters,
value texts, audio in / out); [model] LYNX's own choice. Nothing was decompiled; no graphics, presets or names were
copied (the voicings keep round 147a's LYNX names). Raw data: `D:\lynx-work\parity150\a\` (m1 / m2 / o scripts and
logs; S / D / P + A / B / C renders = Single / Dual / Ping-Pong × signals; lx*, lxc = LYNX renders; tracks, fits and
comparisons: track*.txt / json, an2.json, fitpp*.txt / json, cmp150*.txt, wobcmp.txt); scripts in the job's
`tmp/parity150/pya` (sig150, clu, track, track2, an1, an2, fitpp, fitpp2, genpp2, wobpatch, cmp150).

## 1. Signals and runs [EXEC]

All 31 factory styles × Single / Dual / Ping-Pong (host `Mode` 0 / ⅓ / ⅔), Echo 1 = 250 ms, Echo 2 = 375 ms, Mix
100 %, Saturation 0.25 dB (each style set with round 147a's two dummy renders first):
- A (feedback 0): an impulse train (every 40 ms, 6 s, L = R) and a steady 1 kHz tone (6 s, −20 dBFS);
- B (feedback 0.5): an impulse L = R, an impulse left only, a 200 ms white-noise burst L = R and one left only;
- C (Single, feedback 0, the 21 styles that move): an impulse train of 24 s, for the delay tracks;
- o (the digital style, feedback 0.75, Single at 100 / 250 / 500 ms and Dual): the repeat times.

## 2. The "several voices" of the chorus / fat styles [EXEC]

Each impulse of train A comes back as one cluster per side; its cross-correlation with the first cluster gives the
delay of that side at that moment (a delay track, sub-sample). Findings:
- **No style has several independently moving voices.** A sum of voices with moving relative delays makes a steady
  tone beat; the 1 kHz echo's envelope moves less than 0.05 dB rms in all 31 styles (an1S). The "fat", ambient,
  diffused and verbed styles return a dense pattern of taps (Fat: ~18 taps within ±5 ms, Ambient: taps ≈ 8.6 ms
  apart over 30 ms) that **moves as one** with the wobble (the pattern at pulse 75 is the pattern at pulse 0
  shifted). Echoform's voicings already model these as diffusion (round 147a, spread share).
- Each side has one modulated read. Shapes from 24 s tracks (fit residues, trackC.txt): sines — the BBD pedal style
  (0.81 Hz, ±0.93 ms), the analog delay (0.71 Hz, ±0.49), the analog chorus (1.63 Hz, ±0.38), vibrato (5.46 Hz,
  ±0.40), the oil-drum style (1.0 Hz, ±0.04); a **triangle** for the CE-1-like style (1.63 Hz, ±1.0 ms: residue
  0.008 ms against the sine's 0.067); the others smooth random swings (EchoPlex-like 0.34 ms sd, the digital chorus
  4–5 ms sd, the verbed style 12–13 ms sd at ≈ 0.04 Hz …). The left / right relation of the LFOs is not fixed: the
  same style gives L / R correlations of −1 … +0.8 from run to run (free-running LFOs), so Echoform keeps its phases.

LYNX: the voicing table's wobble fields re-derived from the tracks (`wobpatch.py`): sines at the measured rate and
depth, the triangle (`wobbleRandom` 2: a constant pitch of ±c cents between the turns), and for the random swings the
measured delay sd with the rate that gives the measured pitch (cents) through Echoform's own random LFO (calibrated
by rendering it the same way). Check (wobcmp.txt, delay sd of the left side, LYNX against the trial): median
|ln ratio| 0.40 → **0.11** (e.g. the digital chorus 8.75 → 3.63 ms against 4.08; the shortwave style 1.00 → 0.20
against 0.23; the CE-1-like 0.64 → 0.58 against 0.58).

## 3. The styles in Dual and Ping-Pong [EXEC]

Noise bursts (B), third-octave tone of each side's echoes (an2.json):
- **Dual = Single per side**: the first echo's tone, the per-pass tone and the wobble of each side are those of
  Single (≤ 0.1 dB, the same track); each side uses its own time (right echoes at 375 / 750 / 1125 ms); a left-only
  input leaves the right silent (no cross-feed at FB Mix's default).
- **Ping-Pong**: the input is (L + R) / √2 (L = R gives +3.0 dB against Single, a left-only input −3.0 dB); the
  left echo comes at Time 1 with Single's first-echo tone, the right one at Time 1 + Time 2 and is **exactly the left
  echo** (0.0 ± 0.0 dB in every band, every style; no own wobble on the right side's delay), then left again at
  Time 1 + Time 2 + Time 1 with the feedback gain once per pair and a **pair tone** that depends on the style (the
  digital style: flat; the telephone style: band-passed again each pair; the transmitter: −26 dB at 63 Hz per
  pair). No L/R offset in Ping-Pong (left exactly at Time 1).
- **The L/R offset** (Single and Dual; runs o1–o4): at EchoBoy's default 8 ms the left side's odd repeats come at
  n·T − 4 ms and its even ones at n·T + 6 ms, the right side's at n·T + 4 / n·T − 6 ms (measured −3.83 / +6.17 /
  +4.17 / −5.83 ms incl. 0.17 ms of tone delay), at 100, 250 and 500 ms alike. The offset is not a host parameter
  (Tweak panel), so other offsets were not measured.

## 4. LYNX (src/core/EchoSuite.{h,cpp} `echoform::`, data src/core/EchoPairTones.inc, src/core/EchoVoicings.inc)

Two parameters appended after round 147's `multiply` (Echoform: 107 parameters):

| Parameter | Values | Law |
|---|---|---|
| Voicing stereo | Classic / Measured (default Measured) | with a voicing: Single / Dual — the L/R offset; Ping-Pong — the measured law below. Classic = round 147 (a project saved before round 150 loads with Classic: `upgradeLoadedEffectParams`) |
| L/R offset | 0 … 25 ms (default 8) | odd repeats −offset/2 (left) / +offset/2 (right), even ones +¾ / −¾ offset (the measured −4 / +6 / +4 / −6 ms at 8 ms; the scaling [model]) |

- **L/R offset**: two loops per side, so every pass keeps the voicing's write path (saturation, loop tone,
  diffusion, cuts, loop EQ, LoFi, Multiply): loop A (read at T + odd offset) gives the odd repeats and feeds loop B
  (its own write-path state), loop B (read at T + even − odd offset) gives the even ones and feeds A (read at T − odd
  offset); the loop gain and FB balance once per pass. Used when the style is Single / Dual and Groove / Accent /
  Feel are at zero (those keep their own two-tap loop).
- **Ping-Pong** with a voicing: the first side takes (L + R) / √2 plus the loop gain × the pair tone of the second
  side's echo; the second side's line is a clean copy (no write path, no wobble) of the first side's echo; both sides'
  echoes pass the voicing's output tone; the pair law (loop gain once per pair) always.
- **Pair tone** per voicing (gain, 0–2 high passes, 0–2 low passes, two bells; ≤ +0.9 dB anywhere so the loop stays
  bounded), fitted to the trial's pair minus Echoform's own Single pass (`fitpp2.py`), so it also corrects the loop
  tone's fit error in that mode.
- Wobble: §2.
- Editor (generic layout, the Rhythm row while Rhythm is off and a voicing is chosen): **Stereo Classic | Measured**
  and **L/R offset** (slider + value). pt-BR: "Estéreo", "Clássico|Medido", "Desvio L/R".

**Check, LYNX against the trial** (cmp150.txt; the same B signal through Echoform with each voicing; dB rms over
the third-octave bands within 40 dB of the echo's peak, 60 Hz – 14 kHz; median / worst of the 31):

| | Classic (round 147) | Measured (round 150) |
|---|---|---|
| ping-pong: right echo = left echo | 4.41 / 11.0 | **0.05** / 1.08 |
| ping-pong: pair tone | 4.72 / 12.5 | **0.41** / 1.99 |
| ping-pong first echo against Single | 2.99 / 3.11 | **0.19** / 2.24 |
| Dual: right side per pass | 0.56 / 2.70 | 0.49 / 2.72 (round 147a's loop-tone fit) |

## 5. Tests
(§6 / §7: tests/Parity150aTests.inc second part — the Tweak block's parameters and upgrade, the measured static law at
−30 / −15 / −12 / −6 dBFS within 5°, the attack after 50 ms, the fold past the speakers (tan 7.5°, inverted) and the
clipped angle when off; the plate voicing's onset (< 3 ms, the types > 15 ms), its T20 at 3 / 10 s; Parity135sTests'
parameter count follows.)

tests/Parity150aTests.inc: the parameter block (appended, defaults, texts) and the upgrade of a pre-150 project;
the offset law; an impulse through the digital voicing in Single (left 246 / 506 / 746 / 1006 ms, right 254 / 494 /
754 / 994 ms, the per-repeat drop), left-only input (right silent), Dual (right 379 / 744 ms), Classic (both at
250 ms); Ping-Pong (left 250, right 625 = the left echo within 0.1 dB, left 875 ms 6 dB down, +3 dB against Single
for L = R, −3 dB for a left-only input), the pair tones (flat for the digital voicing, band-pass for the phone
line's, none above +1 dB); the triangle wobble (peak ≈ rms of the pitch deviation) against the vibrato's sine.
tests/Parity147aTests.inc: the parameter-block check follows the appended parameters.

## 6. Auto-pan ← PanMan: the Tweak page [EXEC]

PanMan 5.5.5 (Soundtoys trial) in `tools/fxmeasure`, Digital mode, a 1 kHz tone on both inputs; the pan read as
2·atan2 (|R|, |L|) − 90° from 20 ms / 5 ms rms windows (data `D:\lynx-work\parity150\a\pm\`, m1.txt / m1.log,
pan.py, pmfit.py):
- **The envelope's static law** (OffsetMod +90°, Width 0, stairs −60 … 0 … −60 dBFS in 3 dB steps, 3 s each): no
  modulation below −21 dBFS; −18 / −15 / −12 dBFS give 9 / 28 / 54°, −9 dBFS and above the full 90°, the same going up
  and down. Linear in amplitude: amount = (A − 0.1006) / (0.354 − 0.1006), A = the sine's peak, i.e. a threshold of
  −20 dBFS and full at −9 dBFS (11 dB). OffsetMod −90° mirrors it exactly. InputGain +12 dB and OutputGain +12 dB do
  not move it: the follower hears the input as it comes in.
- **Past the speakers**: Offset 45° + OffsetMod 90° gives 81° at −12 dBFS and 75° at full, the left channel at tan
  7.5° of the right with its polarity inverted (correlation −1.000): the total offset is clamped to ±105° and the pan
  angle 45° + offset / 2 folds past 90° (cos < 0). Round 136's Rhythm voicing clipped it at 0 … 90°.
- **Time constants** (steps −60 → −6 → −60 dBFS and a 100 ms burst): 52° after 10 ms, 88° after 50 ms; half way down
  after ≈ 200 ms, 2° after 1 s. A one-pole follower on the 1 ms mean square's peak-equivalent level fits the whole
  record within 2 % rms: attack 21.3 ms, release 291.5 ms, no output smoothing. Smoothing 100 % does not change it.
- **Feel / Groove** (±0.25, LFO at 1 Hz, Width 180°): no effect in the LFO mode (all runs identical); the rhythm modes
  hold them, but they cannot be chosen through host parameters.
- **ManualTrigger**: on / off between three renders of one instance without a reset gives the same LFO phase in each
  render (the LFO restarts per `process` run), so a trigger cannot be observed this way.
- Rate mod / Width mod: round 147's runs (RateMod ±3 ≈ 8× / ⅛ the rate, WidthMod −180° → no motion).

LYNX (src/core/RhythmPan.{h,cpp}, the Rhythm voicing of Auto-pan, appended after "Rhythm output"; Auto-pan: 36
parameters): **Rate mod** (±3 oct), **Offset mod** / **Width mod** (±180°), **Env threshold** (−60 … 0 dB, default
−20), **Env range** (1 … 40 dB, default 11), **Feel** / **Groove** (±0.25 of a step: the whole pattern / the odd steps
late or early — LYNX's law [model], the shape of EchoBoy's measured Groove / Feel), **Beyond speakers** (fold / clip;
new instances fold, a project saved before round 150 keeps the clipped angle — `upgradeLoadedEffectParams`). The
follower: mean square (1 ms) → √(2 ms) → attack 21.3 / release 291.5 ms; amount = (env − thr) / (full − thr) clamped;
offset + Offset mod × amount (clamped ±105°), width + Width mod × amount (0 … 210°), the rate × 2^(Rate mod × amount)
(an added phase, so the pattern stays on the song position when it is not modulated). Editor: a TWEAK row under the
voicing's panel. Her 50 PanMan states have no Tweak modulation (all 0) and widths ≤ 186°, so they sound as before.

## 7. Foil Plate ← Little Plate: the measured plate voicing [EXEC]

Little Plate 5.5.5 (Soundtoys trial), an impulse (−6 dBFS, both inputs), Mix 100 %, Decay 0.5 / 1 / 2 / 3 / 5 / 10 /
20 s and ∞, Mod on / off, Low Cut 100 / 300 / 1000 Hz, a left-only impulse, a 1 kHz tone (data
`D:\lynx-work\parity150\a\lp\`; T20 per octave from a 2048-point STFT's band energy, rt.py):

| Decay | 125 | 250 | 500 | 1k | 2k | 4k | 8k Hz (RT60, s) |
|---|---|---|---|---|---|---|---|
| 0.5 s | 0.69 | 0.75 | 0.77 | 0.89 | 0.96 | 0.97 | 0.88 |
| 3 s | 2.68 | 2.77 | 2.91 | 2.68 | 2.45 | 2.01 | 1.44 |
| 10 s | 14.3 | 11.0 | 8.06 | 6.45 | 5.33 | 4.14 | 2.80 |
| 20 s | 34.9 | 25.7 | 17.7 | 14.2 | 11.1 | 8.49 | 5.51 |
| ∞ | 78 | 77 | 80 | 87 | 109 | 123 | 51 |

The plate speaks after 0.7 ms (no plate delay); its tail is decorrelated (L/R +0.01); Mod leaves the decay unchanged
(≤ 0.1 s) and narrows the 1 kHz tail (spectral spread 5.4 → 2.4 Hz; not modelled); the low cut is 12 dB/oct (−3 dB at
the label, as Foil's). Foil's Silver plate before: onset 21.3 ms, 10 s → 10.0 s at 125 Hz, 2.84 s at 8 kHz.

LYNX (src/core/LynxVerbs.cpp `FoilFx`, data src/core/FoilVoicing.inc; appended **Voicing**: off / measured): no plate
delay (0.5 ms), the engine's RT60 and its bass (< 300 Hz) / high (> 3 kHz) multipliers per Decay label, log-interpolated
between the measured labels, fitted through Foil's own engine in four passes (plate.py): mid / bass / high RT within
5 % at every label to 20 s (∞: the highs die sooner than on the trial — the engine's high multiplier is capped), tail
energy within ±0.5 dB, broadband T20 2.23 / 9.0 s at 3 / 10 s (trial 2.61 / 11.05). Editor: "Measured plate" switch
beside Low cut. The Little Plate substitute plays it.

## Where I stopped

- Done: §2 – §4, PARITY rows (Echo Mode Dual / Ping-Pong, Tweak L/R Offset, Echo Style menu) and the score note. build-local with the FL Data dir: 1 648 142 checks, 0 failures; Windows build OK; editor screenshot `D:\lynx-work\parity150\a\ui\shots\Echoform-voicing.png` (`--plugui` now also writes it).
- Not done: the offset law at offsets other than 8 ms (the Tweak panel is not reachable through host parameters);
  the Diffused style's static right side (its wobble only on the left; Echoform swings both); EchoBoy's per-style
  Style Edit details (per-repeat decay, Mid Res); PrimalTap's per-tap VCO; Auto-pan ← PanMan Tweak and Foil ←
  Little Plate — done in §6 / §7 (after merging origin/main: build-local with the FL Data dir 1 648 225 checks, 0 failures; Windows build OK; `--plugui` also writes Auto-pan-tweak.png).
- Auto-pan: the Tweak page's divider and manual trigger (not observable black-box), PanMan's own threshold /
  sensitivity values other than the default, Feel / Groove's real law in the rhythm modes, the rhythm-shape editor.
- Foil: Mod's narrowing of the tail, the ∞ setting's long highs, a single-knob view.
