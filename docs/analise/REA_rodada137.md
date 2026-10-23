# REA round 137 (parity137): land round 135, then the PARITY.md gaps

2026-10-23. Two parts: (1) round 135 (parity135, stopped mid-landing) verified and landed; (2) round 137 continues
`docs/PARITY.md`: the ❌ / ⚠️ rows users meet first, and the rows marked "not measured" re-checked on the trials.

Evidence markers: [EXEC] measured here on the official trial / demo, hosted black-box in our measurement host
(`tools/fxmeasure`, public VST3 API only: parameters, value texts, audio in / out); [DATA] read from files;
[MANUAL] from the vendor's manual or product page; [model] LYNX's own choice. Nothing was decompiled; no graphics,
presets or names were copied. Raw measurement files: `D:\lynx-work\parity137\` (eb = EchoBoy, vd = Valhalla, pc =
Pro-C 3).

## 1. Round 135 landed

All four sub-branches (135f / s / v / w) were already merged into `feature/parity135`; it was merged with main again,
built with the FL Data dir (1 468 218 checks, 0 failures) and landed with `land.sh parity135`. The unverified merge
branch `land/parity135-unverified`, the parity135* worktrees and their wip branches were removed.

## 2. Echoform: EchoBoy's Rhythm echo mode [EXEC]

The biggest structural gap of the EchoBoy table. The trial's host parameters for the mode are RhythmMode (Time / Note /
Dotted / Triplet / Pattern / CustomTime / CustomBeats), RhythmNote (18 notes), RhythmTime (0–30 000 ms), RhythmRepeats
(1–16) and RhythmDecay (0–40 dB). Impulses through the trial (Digital Delay style, mix 100 %, 60 and 120 BPM):

| Setting (120 BPM) | Taps heard (ms : dB) |
|---|---|
| 1/8, 4 repeats, decay 0, fb 0 | 250 / 500 / 750 : −5.8 each |
| 1/8, 2 repeats | 250 : −2.8 |
| 1/16, 3 / 4 / 5 / 8 repeats | 2 / 3 / 4 / 7 taps at −4.6 / −5.8 / −6.8 / −8.8 |
| 1/16, 12 or 16 repeats | 8 taps (125 … 1000 ms) at −9.3 |
| 1/32, 16 repeats | 15 taps at −11.8 |
| 1/8, 4 repeats, decay 12 dB | −5.9 / −9.9 / −13.9 |
| 1/16, 8 repeats, decay 20 dB | −5.8 … −22.9 in 2.86 dB steps |
| 1/8, 4 repeats, feedback 0.5 | the three taps, then the pattern again every 1000 ms (slot 0 included), 6 dB down each time |
| 60 BPM: 1/8 × 8, 1/16 × 16, 300 ms × 8 | taps up to 2000 ms only; the feedback replays every 2000 ms |

Law (fits every row to 0.2 dB; the Digital Delay style adds +0.2 dB): `repeats` slots spaced by the note / time, slot 0
being the input itself (not heard on the first pass); only the slots that fit in **2 beats** are used; levels fall
by `decay` dB from slot 0 to the last slot used and are equal-power normalised over those slots (1 / √slots with no
decay); the feedback replays the whole pattern (slot 0 included) every 2 beats.

LYNX (`echosuite::echoform::rhythmPattern`): RHYTHM switch on the third row of the panel with Spacing (ms / note /
dotted / triplet) and its note or time, Repeats 1–16, Decay 0–40 dB, Length 1–8 beats (default 2 = the measured
pattern) and a bar per heard tap in place of the echo-train picture (drag = that tap's level, after the
normalisation — the role of the trial's rhythm editor). Rhythm overrides the style; every parameter is appended and
off by default. Not done: the Pattern presets and the drawn (custom-time) taps of the trial's editor (not host
parameters).

## 3. Echoform: ValhallaDelay's Era [EXEC]

The demo's Era parameter reads Past below 2/3 (default 1/3), Present from 2/3, Future at 1. The first repeat of an
impulse (300 ms, no feedback / modulation), level at 8 / 12 / 16 kHz relative to 1 kHz:

| Mode | Past | Present | Future |
|---|---|---|---|
| Tape (8 kHz) | −10.1 | −7.1 | −6.5 |
| Digital (12 kHz) | −44.9 | −6.6 | −6.3 |
| LoFi (8 kHz) | −10.0 | −5.3 | −1.8 |
| BBD (4 kHz) | −27 | −17 | −8 |
| Analog (8 kHz, rel.) | 0 | +13 | +24 |
| HiFi, Clarity | unchanged | unchanged | unchanged |

LYNX: an Era parameter (Past = the old sound) that scales each character's loop low-pass (tape × 1.13 / 1.27, lo-fi ×
1.45 / 2.2, bucket × 1.25 / 1.6, analog × 1.45 / 2, digital × 1.65 / 1.8); tabs bottom right; translated from the
saved state (`xml:Era`). The era's saturation / diffusion changes are not modelled.

## 4. Lattice Mod: Input pan [EXEC]

UberMod's InputPan (0–8, default 1) with left-only and right-only impulses: 0–1 stereo, 2 swapped, 3 (L + R) / 2 to
both outputs (−6 dB each), 4 to the left only, 5 to the right only, 6–8 like stereo (no difference with an impulse).
LYNX: `lattice::inputPan` (appended, the same value law), a menu on the WARP page, translated.
Also read: DelaySync (18 index texts "0" … "17"). Impulses at 120 BPM, the value sampled at k / 48, the tap time
after 2 s of settling: up to 0.10 the free Delay (500 ms; the two shortest notes, if they are 1/64 and 1/32, were not
seen inside the 2-tap chorus), 0.125 → 125 ms (1/16), 0.1875 → 250 (1/8), 0.25 → 500 (1/4), 0.3125 → 1000 (1/2),
0.354 → 2000 (1/1), 0.417 → 2750 (capped), 0.479 → 188 (1/16.), 0.542 → 375 (1/8.), 0.604 → 750 (1/4.), 0.667 → 1500
(1/2.), 0.708 → 2750 (1/1., capped), 0.771 → 83.7 (1/16T), 0.833 → 167 (1/8T), 0.896 → 333 (1/4T), 0.958 → 667 (1/2T),
1.0 → 1333 (1/1T): straight, dotted and triplet groups, not LYNX's by-length list. The exact boundaries (1/48
resolution) and the two shortest entries are still to read, so DelaySync stays untranslated.

## 5. Echoform ducking [EXEC] (was "not measured")

ValhallaDelay demo, a 1 kHz tone for 1 s into the echo (300 ms, 70 % feedback), the echoes' level during the tone
against ducking 0:

| Input (rms) | 25 % | 50 % | 75 % | 100 % |
|---|---|---|---|---|
| −29 dBFS | 0 | −1 | −8 | −22 |
| −15 dBFS | −0.8 | −7.1 | −21 | −36 |
| −9 dBFS | −1.9 | −12 | −26.6 | −41.7 |

A downward gain reduction keyed by the input: GR = input rms − T, T = −51 dB × ducking^1.28 (soft knee); after the
input stops it recovers at about 85 dB / s. Echoform now uses that law (it was an unmeasured envelope gain).

## 6. Crest: side-chain trigger, Host Sync and MIDI [EXEC]

Pro-C 3's Side Chain Input has Internal / External / Host Sync / MIDI, with Host Trigger Sync (1/1 … 1/16), Offset
(50–200 %, log) and Length (0–100 %). A steady sine under the threshold at 120 BPM: with Host Sync 1/4, the gain
reduction starts every 500 ms and holds for Length of the period (10 % = 50 ms, 50 % = 250 ms); "Offset" scales the
period (50 % = 250 ms, 200 % = 1 s; 1/8 at 100 % = 1/4 at 50 %); the depth is that of a 0 dBFS key (threshold −18 /
−40 / 0 dB at 4:1 with an 18 dB knee: 14 / 32 / 2 dB). MIDI with no notes: no reduction at all (the audio is not
detected). LYNX: Trigger Off / Host sync / MIDI in the side-chain panel, with Note, Rate (the trial's "offset"),
Length, and the Channel Rack channel whose held notes key it; pulses follow the song position while playing.

## 7. Smaller rows

- Voice Shift (Little AlterBoy role): MIDI control — the highest note held on the chosen channel replaces Pitch
  (relative to C4) in Transpose / Quantize, and is the note Robot sings [MANUAL; the trial's MIDI response was not
  measured: our host sends no notes].
- Echoform: H-Delay's LoFi switch independent of the mode (the in-loop 8th-order low-pass at 5.8 kHz of
  `docs/specs/h-delay.md` §4.2, translated from RealWorld 21); the echo-train picture reads the note and its tempo or
  the time (H-Delay's central display); ValhallaDelay's quad taps and Repeat / Swell now translated.
- Gemini Doubler: the three graphs are editable — drag a voice on the pan / gain half circle and on the delay /
  detune map, drag the EQ's L / H markers, double-click a marker = that shelf on / off; cent and frequency labels.
- Quell: the fader's 0–100 scale, the reduction read-out holds the deepest value (click = reset).
- Rows found already done and re-marked: ValhallaDelay "Delay L" caption, the Mod / Freq / Pitch section title, the
  description line (rounds 129 / 135); Halo's host programs are the vendor's presets — deliberately left out.

## Where I stopped

See the end of this file (updated at the end of the round).
