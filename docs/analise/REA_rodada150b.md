# REA round 150b (parity150b): Ember's six bands, pans and measured characters; LYNX Tune's Send / Receive

2026-10-24. Sub-round of parity150 (docs/PARITY.md, tier 4). Ember Saturator ← Saturn 2: six bands, the global input /
output pan, the 28 styles measured black-box and given as LYNX-named voicings ("characters"); LYNX Tune ← Waves Tune
Real-Time: Send / Receive between instances. Evidence markers: [EXEC] measured here on the official Saturn 2 trial
(v2.1.3, FabFilter evaluation, ends ~2026-11-16), hosted black-box in `tools/fxmeasure` (public VST3 parameters, value
texts, audio in / out, the component state as a DAW stores it); [MANUAL] vendor manual / spec dossier; [model] LYNX's
own choice. Nothing was decompiled; no graphics, presets or names were copied (the 28 voicings carry LYNX's own names).
Raw data: `D:\lynx-work\parity150\b\` (sig.wav = the probe, o_sKK_dDDD.wav = style KK at drive DDD %, m1.txt / m1.log
= the run, m2 = the pans, s3_*.bin = saved states, an1.json / fitsat.json / fittone.json = the analysis); scripts in
the job's `tmp/parity150/b/py` (sig.py, an1.py, fitsat.py, fittone.py, geninc.py).

## 1. What was measured [EXEC]

- Styles: the `Band 1 Style` host parameter has 28 steps (value k / 27), in six families: four tubes, four tapes, nine
  amps, three saturations, three transformers, five effects (the reference's names are not used in LYNX).
- Probe (48 kHz, both channels): 2 s white noise at −60 dBFS rms (the small-signal tone, H1 cross-spectrum, third-octave
  bands), a 1 kHz sine at −48 / −36 / −24 / −18 / −12 / −6 / 0 dBFS (0.4 s each: fundamental gain, H2 … H7), 100 Hz and
  5 kHz at −12 dBFS, and a 1 kHz −36 → −6 → −36 dBFS step (memory). Every style at drive 0 / 20 / 40 / 60 / 80 / 100 %
  (168 renders, one band, mix 100 %, defaults otherwise).
- Findings: the small-signal tone is independent of the drive (only its level moves); the amp styles carry a strong
  tone (e.g. a −46 dB cut at 10 kHz and +7 dB at 4 kHz against 1 kHz for the first amp); the level curves are static
  for the tubes, tapes, saturations and transformers (a few amps and the warm tube recover over 20–200 ms after a
  loud passage — a memory LYNX does not model); "drive 0 %" already gives +0.1 … +4 dB of level; the output level
  falls as the drive rises (the style's own make-up: at 100 % the heavy styles end near −25 dBFS for a 0 dBFS input).
  The five effect styles are not static curves: one folds, one is time-variant (its 1 kHz output swims ±5 dB in
  10 ms windows), two gate / pulse (the fundamental is −100 dB while energy stays), one decimates.
- Input / output pan: value texts "Left: 0 dB / Right: −6.02 dB" at −0.5 (the far side × (1 − |pan|), as the band pan);
  in Mid/Side mode a left-only tone with input pan −0.5 gives L −2.5 dB, R −12.0 dB (the side × 0.5) — the pans act on
  mid / side there; the input pan acts before the drive (at drive 100 % the right channel is only 5.8 dB lower), the
  output pan after the mix. State slots (read back after setting them): 1 input pan, 3 output pan (−1 … +1), 6 band
  count (1 … 6), band b at 7 + 17b (3 style index, 15 crossover log2 Hz) — band 5 / 6 at 75 / 92.

## 2. LYNX (src/core/MixSuite.{h,cpp} `ember::`, data src/core/EmberCharacters.inc)

Appended after round 147's pans (Ember: 133 parameters; `maxEffectParams` raised from 128 to 160 in core/Song.h for it):
`More bands` (0 = the Bands parameter, 1 = five, 2 = six), `Crossover 4 / 5`, bands 5 / 6's style / drive / mix /
level, their nine round-128 fields and their drive pan / pan, `Input pan`, `Output pan`, `B1 … B6 character`.
Defaults = the old sound. Helpers: `activeBands()`, `bandCountValues()`, `crossoverParam()`, `characterParam()`.

- Six bands: the split is now a loop (split k cuts the rest, the bands below pass its all-pass copy — the same filters
  in the same order as the four-band code, so old projects are unchanged). Test: six bands switched off sum flat
  within 0.05 dB from 60 Hz to 12 kHz.
- Characters [model, fitted to §1]: y = EQ (A · g (D · x) / D), A the small-signal gain, g (u) = (f (u + b) − f (b)) / f′(b), f (u) = u / (1 + |u|^p)^(1/p)
  (p, b per voicing), D and A per drive step (0 … 100 % of the reference = Ember's drive / 30 dB, linear in dB in
  between), the EQ (0–2 high passes, 0–4 low passes, 3 bells, cookbook sections) after the curve and the band's DC
  blocker. Fit: level curves 0.0–1.2 dB rms for the 23 saturation / amp / transformer voicings, tone 0.0–1.4 dB rms
  (40 Hz – 16.5 kHz); the fold voicing is a sine fold (≤ 1.3 dB to 60 % drive, 3–6 dB above); "Blur" and "Wreck"
  follow the reference's level only; "Collapse" (the reference gates / breaks up) uses the gentle saturation's law with
  its own tone and "Octave rectifier" is a full-wave rectifier at the level measured at drive 0 — both LYNX stand-ins.
  (an1.py read amplitudes 1.76 dB high — five Hann bins' energy — corrected in geninc.py.) A character replaces the Style
  menu's curve (and the Decapitator voicing on band 1); Ember's auto gain does not act on top of it (the measured level
  is the voicing's); feedback, dynamics, tone, pans, solo / mute and PUNISH keep working.
- Input / output pan: in `process`, after the mid / side encoding (input) and before the decoding (output).
- Translation (FabFilterStates.cpp `fromSaturn2`): six bands, each band's style → its character, crossovers 4 / 5,
  input / output pan.
- Editor (EmberEditor): a Character menu beside the Style menu in the band panel, a Bands button (1 … 6) in the bottom
  bar, In pan / Out pan in the strip; splitting / removing carries the character; the band tag shows the character.
  Generic layout: a "Bands 5 / 6, pans, characters" section.

## 3. LYNX Tune: Send / Receive (src/core/LynxTune.{h,cpp}, the `tunekeys` strip)

[MANUAL] Waves Tune Real-Time's "Send Scale to All" and "Receive" (docs/specs/waves-tune-real-time.md). LYNX: the shared
scale is the key, scale, More scales, the twelve note switches, bypasses and directions and the range
(`sharedScaleList()`). SEND (`sendScaleToAll`) publishes it to a process-wide slot and copies it to every other LYNX Tune
in the project (mixer inserts and Patcher branches) whose RECEIVE is on — one undo step. RECEIVE is a saved switch
(appended `Receive`, on by default; off = the instance keeps its scale); switching it on takes the scale sent last in
this LYNX session. The strip got narrower buttons (Reset, Send, Receive, the range presets). Tests: the copy, the locked
instance, the slot, a non-Tune slot.

## Tests

tests/Parity150bTests.inc: the parameter block (unique band parameters, the new bands' defaults, texts), six bands
summing flat and a muted top band, the characters against the measured gains (8 cases, ≤ 1 dB) and an amp's tone
(+7.3 dB at 4 kHz, < −30 dB at 10 kHz), a character in the multiband mode, input / output pan (incl. mid / side, the
measured −2.5 / −12.0 dB), the Saturn 2 translation (six bands, styles, crossover 5, pans), LYNX Tune Send / Receive.

## Where I stopped

- Ember: the amps' / warm tube's level memory (20–200 ms recovery), the effect styles' time-varying behaviour
  (Breakdown, Rectify, Smudge, Destroy are static stand-ins), per-band crossover slopes (Saturn: per band 6–48 dB/oct;
  Ember: one global slope), modulation, linear phase.
- LYNX Tune: Group Octaves off (per-octave statuses), the MIDI reference tone.

Verification: build-local with the FL Data dir 1 744 735 checks, 0 failures; Windows build OK; editor screenshots
(`LYNX.exe --plugui`) in `D:\lynx-work\parity150\b\ui2\` (Ember Saturator.png) and `ui\` (LYNX Tune.png).
