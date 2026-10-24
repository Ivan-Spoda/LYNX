# REA round 147b (parity147b): Ember's mid / side and pans, LYNX Tune's keyboard, Ridge

2026-10-24. Sub-round of parity147 (docs/PARITY.md, tier 3): Ember Saturator ← Saturn 2's channel mode M/S, drive
pan and band pan; LYNX Tune ← Waves Tune Real-Time's keyboard (note statuses, range, navigation); Ridge Strip ← SSL
E-Channel's auto make-up and ANALOG. Evidence markers: [EXEC] measured here on the official trial, hosted black-box in
our measurement host (`tools/fxmeasure`: public VST3 parameters, value texts, audio in / out, the component state as a
DAW stores it); [MANUAL] vendor manual / spec dossier; [model] LYNX's own choice. Nothing was decompiled; no graphics,
presets or names were copied. Raw data: `D:\lynx-work\parity147\b\` (params.txt = Saturn 2's parameter list, m1.txt /
m1.log / o_*.wav = the runs, s2_*.bin = saved states).

## 1. Ember Saturator: Saturn 2's channel mode, drive pan, band pan

### What the trial does [EXEC]

Host parameters (Saturn 2, 1089 parameters): `Channel Mode` (index 695, Left/Right | Mid/Side), per band `Drive Pan`
(−1 … +1, band field 5) and `Pan` (band field 12, value texts "Left: 0 dB / Right: −6.02 dB" at −0.5), global Input /
Output Pan. Saved state (FFBS, 951 values): the channel mode at 695, drive pan at 7 + 17b + 5, band pan at 7 + 17b + 12
(−1 … +1); the parameter indices and the state slots coincide (s2_def / s2_ms / s2_dp.bin differ only there).

One band, Warm Tube, drive 50 %, a 1 kHz sine at −12 dBFS (harmonics H2 / H3 / H5 per channel, `harm.py`):

| Run | Result |
|---|---|
| L/R, drive pan −1 | left = the centred result (H2 −32.6, H3 −44.0 dB); right = exactly the drive-0 % run (−43.7 / −59.3 / −115.8) |
| L/R, drive pan −0.5 | right between the 20 % and 30 % runs: the 25 % run, i.e. **the far channel's drive × (1 − \|pan\|)** on the 0–100 % scale |
| M/S, L = R (pure mid) | identical to L/R (to the last digit): M = (L + R) / 2, decoded L = M ± S (no √2 scaling) |
| M/S, drive pan −1 / +1 | the mid / the side keeps the full drive (−1 = mid, +1 = side) |
| M/S, left-only tone, centred | L keeps only odd products, R only even ones: the side is (R − L) / 2 (a left-only tone gives S = −M) |
| band pan, L/R | the far channel × (1 − \|pan\|) (pan +1: left silent) |
| band pan, M/S | the same law on mid / side (pan −1: the side silent, +1: the mid silent) |

### LYNX (src/core/MixSuite.{h,cpp} `ember::`, editor `EmberEditor` in src/ui/ProEditors.cpp)

Appended after the Classic voicing (round 136): `Channel mode` (0 L/R, 1 M/S), `B1 … B4 drive pan`, `B1 … B4 pan`
(Ember: 92 parameters). Defaults = the old sound (centred, L/R).

- M/S: the whole engine runs on M = (L + R) / 2 (channel 0) and S = (R − L) / 2 (channel 1), decoded L = M − S,
  R = M + S (the trial's sign).
- Drive pan: channel 0 / 1's drive (dB, Ember's 0–36 dB = Saturn's % × 30 dB in the translation) × (1 − |pan|) on the
  far side; with Auto gain each channel gets its own make-up.
- Band pan: the band's output gains (1 − |pan| on the far side), single-band and multiband.
- Editor: DRIVE PAN beside the drive knob, PAN under LEVEL in the band panel, "Channel L/R | M/S" in the bottom bar;
  splitting / removing a band carries its pans. The generic layout has a "Stereo" section.
- Saturn 2 states: the channel mode and every band's drive pan / pan translated.
- Tests: tests/Parity147bTests.inc (laws, M/S identity for a mid signal, side removal with pan, multiband pan,
  translation).

## 2. LYNX Tune: the Waves Tune Real-Time role's keyboard

[MANUAL] (docs/specs/waves-tune-real-time.md, the v16 guide): a multi-octave keyboard with per-note statuses (Legal,
Illegal, Illegal-Pitch-Up, Illegal-Pitch-Down, Bypass; a click cycles), the range (boundary handles + presets Bass,
Baritone, Tenor, Alto, Mezzo Soprano, Soprano, Generic; notes outside → bypassed), Group Octaves, Reset Scale,
Send / Receive, view buttons (− / + and ◀ / ▶ under the keys, `parity129/ref/wtune.png`). [EXEC] the keyboard's statuses
and range are not host parameters of the trial (§3), so they were not measured.

LYNX (src/core/LynxTune.{h,cpp}, the `tunekeys` display in src/ui/PluginEditors.cpp; appended after `outputTrim`):

| Parameter | Values | Law |
|---|---|---|
| C … B dir | Nearest / Up / Down (default Nearest) | an illegal note's direction: a sung pitch nearest to it goes to the next legal note up / down (`targetNote`), after the tolerance hysteresis |
| Range low / high | MIDI notes 0 … 127 (default 0 / 127) | sung notes outside pass uncorrected (like a bypassed note) |

Editor: the keyboard strip holds BYPASS / REMOVE, RESET SCALE, the seven range presets, − / + (2–7 octaves shown) and
◀ / ▶ (scroll), LATCH; a range bar over the keys (lit between the boundaries; click / drag moves the nearer one); keys
outside the range are dimmed; REMOVE cycles legal → illegal (cross) → illegal up (▲) → illegal down (▼) → legal; the
octave names follow MIDI (C4 = 60). Range presets [model]: the classical voice ranges widened by a third (Bass C2–G#4,
Baritone F2–C#5, Tenor G#2–E5, Alto C#3–A5, Mezzo F3–C#6, Soprano G#3–E6, Generic = everything); a preset also picks
the detector's input type (low male / alto-tenor / soprano). Group Octaves stays on (per-octave statuses would need
128 per-key values), Send / Receive not done. Tests: the direction law, the range bypass on a 450 Hz tone
(corrected to 440 Hz in range, 450 Hz outside), the defaults.

## 3. Ridge Strip: auto make-up and ANALOG

Round 145 (vox145) landed first; Ridge's new parameters follow its Vocal-strip block: `Auto make-up`, `Analog`
(defaults off = the old sound).

[EXEC] SSL E-Channel trial (Waves, demo mode; the shell needs ~10 min to describe its 714 plug-ins the first time —
round 144's "hang" — then loads from fxmeasure-cache.xml; loading DeEsser Mono first makes the shell give the
requested plug-in, as in round 113). Host parameters: `Analog` (index 1, Off / On), `Gain`, `InputLevel`,
`CompThresh`, `CompRatio` … (164 parameters). Silence, a 1 kHz / −20 dBFS and a 100 Hz / −6 dBFS sine, Analog off and
on (blocks muted by the demo are masked out):

| | Analog off | Analog on |
|---|---|---|
| silence | digital zero | white noise, −110.7 dBFS RMS per channel (≈ −154.5 dB/Hz from 20 Hz to 20 kHz), L / R correlation −0.001 |
| 1 kHz / −20 dBFS: H2 / H3 | −134.1 / −122.1 dB | −134.0 / −121.6 dB |
| 100 Hz / −6 dBFS: H2 / H3 | −106.1 / −80.0 dB | −106.1 / −80.1 dB |
| output fader −12 dB / input −12 dB / compressor 4:1 at −20 | — | the noise stays at −110.9 / −110.8 / −110.2 dBFS |

So ANALOG is the console emulation's noise floor, added at the very output. LYNX: Gaussian (Irwin–Hall) white noise
at that level, one generator per channel, in both of Ridge's own voicings (classic and E console); translated from
E-Channel states (RealWorld 31).

Auto make-up: the reference's compressor always gives back level for its threshold and ratio; round 123 measured that
law (`VoiceMatchDynamics.inc`, the E voicing has it). "Auto make-up" applies it to Ridge's own compressor
(`ridge::autoMakeupDb`: the console's make-up for threshold dBFS + 18 on its dBu scale and the same ratio; 0 at 1:1 or
with the dynamics off). Editor: "Auto make-up" in the Compressor section, "Analog" in the Dynamics section.

WTRT note: Waves Tune Real-Time's host parameters (143) were listed this way too — Speed, Note Transition, Link,
Correction (+ on / off), Reference, Tolerance time / cents, Vibrato depth / on, Scale type (44), Scale root (17); the
keyboard statuses and the range are not host parameters (editor state only), so the range presets stay [model].

## Scores (docs/PARITY.md)

| LYNX engine | Original | Config | Design |
|---|---|---|---|
| Ember Saturator | Saturn 2 | 6 → 7 | 7 → 7 |
| LYNX Tune | Waves Tune Real-Time | 8 → 9 | 6 → 7 |
| Ridge Strip | SSL E-Channel | 7 → 8 | 7 → 7 |

## Where I stopped

- Ember: Saturn's global Input / Output pan (measured value texts, not implemented), 6 bands, 28 styles, modulation,
  linear phase.
- LYNX Tune: Group Octaves off (per-octave statuses), Send / Receive between instances, MIDI reference tone; the
  range presets' spans are LYNX's own (the trial keeps them in its editor state, not in host parameters).
- Ridge: Split / Dyn S.C. semantics, the threshold's units / direction in the classic engine (unchanged).
- Waves in our host: the first load of the shell takes ~10 min (it describes all 714 plug-ins; cached afterwards in
  the data folder's fxmeasure-cache.xml); load DeEsser Mono first to get the requested plug-in.
