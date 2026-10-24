# REA round 150a (parity150a): Echoform ← EchoBoy — the styles in Dual / Ping-Pong, the L/R offset, the wobbles

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

tests/Parity150aTests.inc: the parameter block (appended, defaults, texts) and the upgrade of a pre-150 project;
the offset law; an impulse through the digital voicing in Single (left 246 / 506 / 746 / 1006 ms, right 254 / 494 /
754 / 994 ms, the per-repeat drop), left-only input (right silent), Dual (right 379 / 744 ms), Classic (both at
250 ms); Ping-Pong (left 250, right 625 = the left echo within 0.1 dB, left 875 ms 6 dB down, +3 dB against Single
for L = R, −3 dB for a left-only input), the pair tones (flat for the digital voicing, band-pass for the phone
line's, none above +1 dB); the triangle wobble (peak ≈ rms of the pitch deviation) against the vibrato's sine.
tests/Parity147aTests.inc: the parameter-block check follows the appended parameters.

## Where I stopped

- Done: §2 – §4, PARITY rows (Echo Mode Dual / Ping-Pong, Tweak L/R Offset, Echo Style menu) and the score note. build-local with the FL Data dir: 1 648 142 checks, 0 failures; Windows build OK; editor screenshot `D:\lynx-work\parity150\a\ui\shots\Echoform-voicing.png` (`--plugui` now also writes it).
- Not done: the offset law at offsets other than 8 ms (the Tweak panel is not reachable through host parameters);
  the Diffused style's static right side (its wobble only on the left; Echoform swings both); EchoBoy's per-style
  Style Edit details (per-repeat decay, Mid Res); PrimalTap's per-tap VCO; Auto-pan ← PanMan Tweak and Foil ←
  Little Plate (item 3 of the brief: not started in this sub-round).
