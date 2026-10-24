# REA round 152b (parity152b): Ember ← Saturn 2 (own crossover slopes, linear phase, modulation, Breakdown); LYNX Tune ← Waves Tune Real-Time (Group Octaves off, reference tone)

2026-10-24. Sub-round of parity152 (docs/PARITY.md, tier 5), continuing round 150b's "Where I stopped". Evidence
markers: [EXEC] measured here on the official Saturn 2 trial (v2.1.3, FabFilter evaluation, ends ~2026-11-16), hosted
black-box in `tools/fxmeasure` (public VST3 parameters and value texts, audio in / out, the component state as a DAW
stores it); [MANUAL] vendor manual / spec dossier (docs/specs/waves-tune-real-time.md); [model] LYNX's own choice.
Nothing was decompiled; no graphics, presets or names were copied (Ember's characters keep LYNX's names). Raw data:
`D:\lynx-work\parity152\b\` (p.tsv = the 1 088 parameters with value texts, e1 / e2 / e3 = the runs and their
renders, st_*.bin = saved states, lx\ = LYNX renders); scripts in the job's `tmp/parity152/b/py` (an_x.py crossovers,
mem.py / mem2.py memory, an_m.py / an_ef.py / fit_ef*.py modulation, fxst*.py / an3.py effect styles, fitcol.py the
Collapse level curve, lx*.py LYNX against the trial).

## 1. Saturn 2's parameters [EXEC]

1 088 host parameters: the 6 bands (17 each, incl. `Band n Crossover Slope` 6 / 12 / 24 / 36 / 48 dB/oct per band),
`Processing Mode` (Minimum / Linear Phase), `High Quality Mode` (Off / Good / Superb), `Unrestricted Feedback`, and the
modulation: 6 XY controllers, 6 XLFOs (frequency 0.02 – 500 Hz, sync, 1 … 16 steps each with value / glide / glide
function Linear-Sqr-Sqrt-Sine / random), 6 envelope generators, 4 envelope followers (input, mode Envelope /
Transient, attack 1 ms – 1 s, release 50 ms – 5 s), 10 MIDI sources, 50 slots (source of 39, target of 232, level,
inverted, bypassed). The state (951 floats) holds the first 951 parameters in their own units.

## 2. Crossover slopes and processing mode [EXEC]

Two bands at 1 kHz, an impulse, each band alone (the other muted, mix 0 % = the band's dry signal) and both:

- **Minimum phase**: 6 dB/oct = the first-order pair (−3.01 dB and ±45° at the crossover, low + high = 1), 12 = LR2
  (the sum an all-pass, −90° at fc), 24 = LR4, 36 = LR6, 48 = LR8 — every band within 0.05 dB of LYNX's `LrSplit`
  designs (round 150) at 100 Hz … 10 kHz (an_x.py / an_xl.py).
- **Linear phase**: zero-phase bands, low = 1 / (1 + (f / fc)^m), m = slope / 6, on the analog frequency axis (6 dB/oct:
  −0.82 dB at 100 Hz, −6.02 at 1 kHz, −20.82 at 10 kHz), the bands sum to the delayed input; latency **3072 samples**
  at 48 kHz (Pro-MB's). LYNX within 0.05 dB.
- High Quality Mode: latency 0 / 8 / 9 samples (minimum phase), 3072 / 3156 / 3164 (linear phase).

LYNX (`src/core/MixSuite.{h,cpp}` `ember::`, appended): `Slope 1 … 5` (Global = the old Crossover slope, else
6 / 12 / 24 / 36 / 48) and `Processing` (minimum / linear phase). Linear phase: bands 1 … n−1 are FFT-convolved
zero-phase FIRs (the shared `peqfl::FftConvolver`, Prism's 4096-point size), the top band = the delayed input minus
them (an exact sum), the dry paths delayed alike; the single band and the Classic voicing are delayed by the same
amount; latency = 3072 + LYNX's 55-sample oversampler. Crossover modulation is ignored in linear phase.

## 3. The amps' level "memory" [EXEC]

Round 150b's renders (1 kHz −36 → −6 → −36 dBFS) demodulated in 2 ms windows: after the loud part every style is
back within 1.5 dB after 10 ms and within 0.1 dB from 50 ms (Smooth Amp −5 dB at 10 ms, +1.5 at 20, +0.6 at 50;
Plexi −1.5 at 10 ms); during the loud part the gain settles within 20 ms. That is the tone filters' / bias's
transient, not a 20 – 200 ms memory: nothing to model.

## 4. Modulation [EXEC]

1 kHz at −20 dBFS, one slot → `B1 Output Level`:

- **Slot level**: the target moves by level × source in its own 0 … 1 range (B1 Output Level: 72 dB, so 0.050 → ±3.6 dB,
  0.160 → ±11.52 dB, exactly; clamped at the range). The level knob's law is a table (0.5 → 0.160, 0.75 → 0.498).
- **XLFO** (2 steps +1 / −1): glide 0 = a square; glide 1 with Linear / Sqr / Sqrt / Sine = triangle, ease-in, ease-out,
  sine (each step glides from the other value over the whole step); 4 steps play in order, the frequency is the
  whole pattern's (4 steps at 1 Hz = 0.25 s each). The frequency knob's law is a table (0.25 → 1.02 Hz, 0.5 → 3.52,
  0.75 → 32.5). The phase runs free (not reset by the host's reset).
- **Envelope follower** (steps −40 → −30 / −20 / −10 / 0 → −40 dBFS): the source is (dB + 60.7) / 30 of a mean-square
  follower (attack τ = the attack time, release τ = release / 10) — 0.646 at −40 dBFS, +0.33 per 10 dB, not clamped at 1;
  fit rms 0.018 source units (fit_ef3.py); with attack 100.9 ms the steady value drops to 0.42 (model 0.39).

LYNX (appended): `LFO 1 / 2 rate` (the reference's table), `LFO 1 / 2 shape` (Sine, Triangle, Square, Ease in, Ease out
= the two-step XLFO), `Env attack / release` (the reference's tables), `Mod 1 … 4 source` (Off, LFO 1, LFO 2,
Envelope), `target` (27: in / out gain, mix, each band's drive / level / mix, the five crossovers), `amount` (signed,
the reference's level law), applied every 32 samples to a copy of the parameters in the reference's target ranges
(gains ±36 dB, drive 0 … 100 % = 30 dB of Ember's, level −∞ … +36 dB, crossover 40 Hz … 18 kHz), clamped to Ember's
ranges. Check (lx2.py): the five LFO shapes ±11.52 dB as the trial; the follower 0.018 units rms away from Ember's
+18 dB level limit. Single band: band 1's mix and level now act (the reference's) — with the measured law only.

## 5. The effect styles [EXEC]

- **Breakdown** pitches down by 2^(−2 · drive) (24 semitones at 100 %; 870 / 760 / 660 / 570 / 500 Hz for 1 kHz at
  10 … 50 %), the same at every level and input frequency; its output level is nearly constant above −24 dBFS at high
  drive. LYNX's **Collapse**: a two-tap delay-line pitch shifter (50 ms window [model]) after the character's curve,
  ratio 2^(−2 · drive), and a refitted level curve (seven levels × six drives: 0.02 – 0.37 dB rms, fitcol.py; LYNX
  against the trial ≤ 0.8 dB, lx4.py). `Effect law` (appended, measured by default; projects saved before round 152
  keep the static stand-in through `upgradeLoadedEffectParams`).
- **Smudge**: a delayed, smeared copy (the impulse's energy 45 – 136 ms late at 30 %, 136 – 272 ms at 40 %, later
  with more drive) with ±45 Hz sidebands; **Destroy**: a quantiser with a level drop (11 … 692 distinct levels on a
  50 Hz sine, not monotonic in the drive, no sample-and-hold). Both stay level-only stand-ins.
- **Foldback / Rectify**: static (round 150b's sine fold / full-wave rectifier).

## 6. Translation (FabFilterStates.cpp `fromSaturn2`) [EXEC: slots read back from saved states]

Each band's slope (state 7 + 17b + 16), 696 the processing mode, XLFO k at 135 + 73k (0 frequency knob value, 8 step
count, step j at 9 + 4j: value, glide, function), 646 / 647 … the follower (attack / release knob values at 649 / 650),
700 the slot count, slot k at 701 + 5k (source, level, bypassed, inverted, target). XLFO 1 / 2 → LFO 1 / 2 (the shape
from step 1's glide and function when two steps, else a sine; the step span scales the amount), EF 1 → Envelope, the
first four usable slots → Mod 1 … 4 (targets mapped: input / output gain, mix, band drive / level / mix, crossover).

## 7. LYNX Tune: Group Octaves off and the reference tone [MANUAL]

Waves Tune Real-Time's Group Octaves (S1 p.17): off = a key's edit applies to that key only. LYNX (appended
`Group octaves`, on by default, and a 12-value key map: MIDI 24 … 119, eight keys per value as base-6 digits — an
exact integer / 2^24 in each float): a key's own status (legal, removed, removed up, removed down, bypass) overrides
its note name's; the correction picks the target per key (`targetNoteKeys`: the nearest allowed key within an
octave with the hysteresis, a removed-up / -down key goes that way). Keyboard strip: "Group oct." button; with it off
REMOVE cycles that key's status and BYPASS toggles it, an outline marks keys with their own status; Reset clears them;
Send / Receive carry the switch and the map. The MIDI reference tone (S1 p.21): `Reference tone` + `Tone level`
(±18 dB) — the notes held on the MIDI channel play as sines at the reference pitch (0 dB = −12 dBFS peak [model]).

## Tests

tests/Parity152bTests.inc: the parameter block, defaults and upgrade; the laws (slopes, LFO rate / shapes, amount,
follower, collapse ratio, linear shape); the minimum-phase low band at 6 / 12 / 24 / 36 dB/oct against the trial's
numbers and the all-pass sum; linear phase (the trial's band levels, symmetry, exact sum, latency, the single band);
the LFO (±11.52 dB) and the follower (+15.4 dB at −20 dBFS) on the band level; Collapse at 100 % / 50 % (250 / 500 Hz),
the static law at 1 kHz; the Saturn 2 translation (slopes, processing, LFO, follower, two slots, inverted); LYNX Tune's
key map (round trips, statuses, per-key targets, the REMOVE cycle, Send), a 330 Hz tone pulled to F4 with E4 removed
(up) on its own key, the reference tone (440 Hz at 0.25, silence after release).

## Where I stopped

- Ember: the 16-step LFO editor (two-step shapes only), EGs / XY / MIDI modulation sources, more than four slots, the
  follower's Transient mode and band inputs, Smudge / Destroy's real behaviour, the High Quality mode's latencies
  (Saturn 0 / 8 / 9 samples; LYNX always oversamples, 55 samples), linear phase ignores crossover modulation.
- LYNX Tune: MIDI Target Pitch with octaves, 44 scale types, ±100 c reference.
