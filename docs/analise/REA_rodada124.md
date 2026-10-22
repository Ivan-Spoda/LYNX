# REA round 124: her voice through LYNX's substitutes, matched to the real plugins

Goal (round 123 brief): when one of her projects uses a plugin this PC lacks, LYNX's substitute must sound like that
plugin at every setting her corpus uses. This round covers Waves NS1 (→ Quell Denoiser), Waves Doubler (→ Gemini
Doubler), Waves H-Delay (→ Echoform) and Slate Fresh Air (→ Aether), plus an inventory of every third-party plugin in
her corpus. The extras with an official trial here (Valhalla, Pro-Q 4, S1 Imager…) were done by the fork vmatch124x:
docs/analise/REA_rodada124_extras.md.

Evidence markers: [DATA] read from her project files, [EXEC] black-box runs of the official trials in tools/fxmeasure
(host the plugin, feed signals, read outputs, set / read parameters, save / load states), [model] our reading. No
third-party binary was opened, decompiled or patched, and licensing was not touched. LYNX's engines are our own code.
Scripts: tmp/vm124/py (vm.py harness, fa*.py, db*.py, hd*.py, ns*.py), the fit simulators tmp/vm124/fa/famodel.cpp and
tmp/vm124/ns/nsmodel.cpp. Captures: C:\Users\kauan\lynx-test\vm124 (out/ = official plugins, lx/ = LYNX).

## 0. Her corpus: every third-party plugin (step 0)

[DATA] tmp/vm124/py/inv.py over tmp/gf/all + tmp/gf/proj2: 910 .flp files (51 main projects, the rest FL's
backups), every plugin slot of FL's wrapper records, distinct saved states dumped to tmp/vm124/st (index.tsv).
"Trial here" = an official vendor installer / trial on this PC (Waves Central 17.x shells, Slate, Valhalla, FabFilter,
SSL, Harrison, Antares; see the extras doc §0 for the provenance checks). "Readable" = LYNX reads the saved state.

| Plugin | slots (all files) | main projects | distinct states | trial here | LYNX substitute | readable | match (this round) |
|---|---|---|---|---|---|---|---|
| Waves NS1 | 2240 | 40 | 6 (4 settings) | yes (17.1) | Quell Denoiser | yes | §6 |
| Waves CLA-2A | 2178 | 39 | 12 | yes | Glow Leveler | yes | vmatch123 |
| Waves SSL E-Channel | 2172 | 38 | 6 | yes | Ridge Strip | yes | vmatch123 |
| Waves RComp | 2172 | 38 | 2 | yes | Compressor | yes | vmatch123 |
| Waves DeEsser | 2172 | 38 | 2 | yes | Hush De-esser | yes | vmatch123 |
| Auto-Tune Pro | 2165 | 38 | 1838 | yes | LYNX Tune | yes | tune120 |
| Waves H-Delay | 2072 | 39 | 70 (56 settings) | yes (demo mutes, §5) | Echoform (hybrid) | yes | §5: exact (Analog off), Mod2 §5 |
| Waves Doubler (Doubler2) | 2050 | 38 | 3 (2 settings) | yes | Gemini Doubler | yes | §4: right voice exact, detuned voice spectral |
| Polyverse Filterverse | 2028 | 36 | 97 | **no** | none (slot left out, as FL) | no | not possible here |
| Soundtoys Little AlterBoy | 1307 | 30 | 52 | **no** | none | encoded | vmatch127 |
| Slate Fresh Air | 1247 | 30 | 24 | yes (free) | Aether (dynamic) | yes | §3: −35…−59 dB null |
| ValhallaVintageVerb | 1106 | 29 | 57 | yes (demo) | Epoch Verb | yes | extras §3 |
| Soundtoys Decapitator | 893 | 12 | 63 | **no** | Ember Saturator | encoded | vmatch127 |
| Soundtoys PanMan | 386 | 15 | 50 | **no** | Auto-pan | encoded | vmatch127 |
| Soundtoys EchoBoy | 236 | 6 | 22 | **no** | Fruity Delay 3 | encoded | vmatch127 |
| Arturia Analog Lab V (instrument) | 170 | 6 | 87 | **no** | none | no | — |
| FabFilter Pro-Q 4 | 83 | 5 | 4 | yes | Facet EQ | yes | extras §2 (landed) |
| Waves Tune Real-Time | 80 | 4 | 11 | yes | LYNX Tune | yes | tune120 |
| FabFilter Pro-DS | 74 | 4 | 2 | yes | Hush De-esser | yes | measured, handed to vmatch127 (extras §5) |
| UADx Neve 1073 / 1176 Rev A / LA-2A | 68 each | 3 | 3 / 3 / 1 | **no** | none | no | — |
| Antares Auto-Key | 52 | 2 | 4 | yes | none (key detector, no audio role) | — | tune120 |
| ValhallaDelay | 48 | 1 | 4 | yes (demo) | Echoform | yes | extras §6 |
| Sugar Bytes Effectrix 2 | 22 | 5 | 5 | **no** | none | no | — |
| Auto-Tune EFX+ | 17 | 1 | 6 | yes | LYNX Tune | yes | tune120 |
| Waves S1 Imager | 12 | 2 | 2 | yes | Stereo Shaper (new, extras §4) | yes | extras §4 |
| SSL Native Vocalstrip 2 | 12 | 0 (backups) | 2 | yes, but hangs on load (licence dialog?) | Ridge Strip | — | blocked (ASK_USER) |
| Cradle The God Particle | 10 | 1 | 3 | **no** | none | no | — |
| Softube Drawmer S73 | 8 | 1 | 6 | **no** | none | no | — |
| ValhallaFutureVerb / Supermassive | 6 / 3 | 1 / 1 | 1 / 2 | yes | Horizon / Nebula | yes | extras §6 |
| Waves CLA-76 | 6 | 1 | 1 | yes | Compressor | yes | vmatch127 |
| Plugin Alliance BB HG-2 / Maag EQ4 | 6 / 6 | 1 / 1 | 1 / 1 | **no** | none | no | — |
| Bloom Drum Machine (instrument) | 3 | 0 | 3 | **no** | none | no | — |
| FabFilter Pro-Q 3 (VST2) | 1 | 1 | 1 | Pro-Q 4 only | Facet EQ | yes (Pro-Q 3 layout) | — |
| Harrison 32Classic | 1 | 0 (backup) | 1 | yes, hangs on load | none | no | blocked (ASK_USER) |

Trials the user could install to let LYNX match more of her corpus (vendor sites, official trials only): Polyverse
Filterverse (36 projects!), Soundtoys 5 (Little AlterBoy, Decapitator, PanMan, EchoBoy — 30 / 12 / 15 / 6 projects),
UADx Neve 1073 / 1176 / LA-2A, Sugar Bytes Effectrix 2, Cradle The God Particle, Softube Drawmer S73, Plugin Alliance
HG-2 / Maag EQ4. And: open SSL Vocalstrip 2 / Harrison 32C once by hand so their licence dialog is answered; check in
Waves Central that H-Delay and Doubler are covered by the activated licence (§5, tmp/vm124/ASK_USER.md).

## 1. Harness

- `ref()` (tmp/vm124/py/vm.py) hosts the official plugin in fxmeasure (Waves: a dummy shell plugin first, round 119)
  and loads her raw component state; `lynx()` runs LYNX's substitute through `fxlynx` with the SAME state, i.e.
  through LYNX's own substitute lookup and translation (`SUBST_STATE`, `SUBST_NAME`); `fxlynx_before` is the same tool
  built from main before this round (99bbdac), for the "before" columns.
- Metrics (`compare`): null = residual (reference − LYNX) in dB below the reference, level, 50 ms level envelope
  (RMS / max dB over windows within 50 dB of the peak), 1/3-octave spectrum 31.5 Hz – 16 kHz (RMS / max dB).
- Signals: her raw lead takes (game over, kwe, luisa sonsa; 48 kHz), pink noise at −20 / −40 dBFS, sine stairs,
  tone bursts, impulses, impulse trains.

## 2. Demo mutes

[EXEC] H-Delay (and once Doubler) still went silent for ~1 s at intervals in some runs although the user activated
Waves (e.g. 20 s and 60 s into a session), and NS1 did in one 21-run session (none in another 60 s run): the Waves
licence does not seem to cover these three reliably. Those windows are masked (reference silent while LYNX is not);
tmp/vm124/ASK_USER.md asks the user to check the licence in Waves Central.

## 3. Fresh Air → Aether: the dynamic air model

[EXEC] What the official trial does (levels, frequencies, phase; tmp/vm124/py/fa1–fa9, fa44, faall):
- Linear structure, confirmed to 0.05–0.6 % residual: y = s·x + gMid(t)·kMid·HP₂(2993 Hz, Q 0.629)(x) +
  gHigh(t)·kHigh·HP₂(9000 Hz, Q 0.707)(x) — the dry signal plus two RBJ high-passed bands, each turned down by its
  own compressor. The ratio (H(L) − H(−6 dBFS)) / (H(−70) − H(−6)) is real and constant across frequency at every
  level, which proves one scalar gain per band. Mid and High add up (combined settings within −58…−71 dB of the
  sum of the single-band responses). The analog corners are the same at 44.1 and 48 kHz. Latency 0.
- s, kMid, kHigh measured on a 0.025 grid of the knobs (s rises to 1.137 at Mid 0.625 then falls; kHigh 0 → 12.38).
  Trim is the float at offset 2526 of the 2530-byte state (dB, −12 … 0); all 24 of her states hold 0.
- Mid band: a linked (max of L/R) peak detector, instant attack (a 0.5 ms burst gives the full reduction), 5 ms hold,
  96 ms release, 4:1 above −30.1 dBFS (sine peak through the band filter), gain released through a 7.7 ms one-pole.
- High band: two 4:1 stages: a peak detector with 2-pole gain smoothing, then a feedback stage whose threshold rises as
  High air goes down (−41.1 + 15.6 (1 − h)² dB) and which overshoots after loud onsets (seen as a 50–75 ms dip).
  Parameters fitted (LM over takes, bursts and stairs; famodel.cpp).
- LYNX: Aether gets three appended parameters (Dynamic, Mid air, High air; older projects load Dynamic = off = the
  previous sound); `translate = fresh-air` turns Dynamic on and copies the two amounts and the Trim. The editor's two
  big knobs drive Mid / High air in the dynamic model; the bar shows the Dynamic switch.

Her 24 states, LYNX − official (before = round-113 static shelf), her three takes (go shown; kwe / lu alike) and pink
noise at −20 dBFS (tmp/vm124/faall.json):

    state  mid/high     slots   take: null  1/3-oct RMS/max  env RMS        pink −20: null  1/3-oct RMS/max
    0   0.292/0.252   733   −15.0 → −52.4   1.53/2.04 → 0.02/0.08  1.59 → 0.02   −15.7 → −42.0   1.43/1.85 → 0.03/0.11
    1   0.215/0.245   130   −15.4 → −53.2   1.47/2.16 → 0.02/0.08  1.47 → 0.02   −16.1 → −42.2   1.36/1.73 → 0.03/0.11
    2   0.415/0.130    83   −16.1 → −53.8   1.33/1.67 → 0.01/0.04  1.57 → 0.02   −16.6 → −47.0   1.29/1.66 → 0.02/0.05
    3   0.330/0.320    61   −14.2 → −50.1   1.68/2.38 → 0.03/0.11  1.73 → 0.02   −15.2 → −38.9   1.55/2.01 → 0.05/0.16
    4   0.486/0.604    45   −12.6 → −38.1   2.31/4.50 → 0.16/0.46  2.28 → 0.04   −14.3 → −25.8   1.87/2.49 → 0.23/0.69
    5   0.488/0.374    39   −13.5 → −47.4   1.80/2.52 → 0.04/0.14  1.94 → 0.03   −14.8 → −36.1   1.66/2.21 → 0.06/0.21
    6   0.338/0.360    34   −13.9 → −48.6   1.76/2.63 → 0.04/0.14  1.79 → 0.03   −15.0 → −36.7   1.61/2.09 → 0.06/0.20
    7   0.420/0.502    29   −13.0 → −42.2   2.06/3.63 → 0.10/0.30  2.06 → 0.03   −14.5 → −28.8   1.78/2.35 → 0.16/0.51
    8   0.392/0.568    15   −12.8 → −39.4   2.22/4.27 → 0.15/0.41  2.15 → 0.04   −14.4 → −25.8   1.83/2.40 → 0.23/0.72
    9   0.354/0.666    13   −12.6 → −36.6   2.48/5.28 → 0.21/0.59  2.30 → 0.06   −14.2 → −26.4   1.89/2.46 → 0.22/0.65
    10  0.416/0.668    13   −12.5 → −36.5   2.48/5.21 → 0.21/0.58  2.36 → 0.06   −14.1 → −26.5   1.91/2.51 → 0.21/0.63
    11  0.412/0.444    11   −13.3 → −44.9   1.94/3.15 → 0.07/0.22  1.97 → 0.03   −14.6 → −31.9   1.73/2.27 → 0.11/0.36
    12  0.256/0.772     6   −12.5 → −35.2   2.81/6.56 → 0.25/0.67  2.47 → 0.10   −14.0 → −28.5   1.90/2.44 → 0.17/0.47
    13  0.458/0.252     5   −14.5 → −51.0   1.57/1.98 → 0.02/0.06  1.77 → 0.03   −15.4 → −42.1   1.50/1.98 → 0.03/0.09
    14  0.706/0.314     5   −13.8 → −47.6   1.64/2.13 → 0.03/0.07  2.04 → 0.04   −15.3 → −39.5   1.55/2.12 → 0.04/0.11
    15  0.228/0.304     5   −14.8 → −51.1   1.60/2.46 → 0.03/0.11  1.57 → 0.02   −15.6 → −39.5   1.46/1.86 → 0.05/0.15
    16  0.348/0.310     5   −14.3 → −50.3   1.66/2.29 → 0.03/0.10  1.73 → 0.02   −15.2 → −39.4   1.55/2.01 → 0.04/0.14
    17  0.288/0.286     4   −14.7 → −51.5   1.59/2.24 → 0.03/0.09  1.63 → 0.02   −15.5 → −40.5   1.48/1.91 → 0.04/0.13
    18  0.268/0.248     4   −15.1 → −52.7   1.51/2.07 → 0.02/0.08  1.55 → 0.02   −15.9 → −42.1   1.41/1.81 → 0.03/0.11
    19  0.336/0.372     3   −13.8 → −48.1   1.78/2.72 → 0.04/0.15  1.80 → 0.03   −14.9 → −36.0   1.62/2.10 → 0.07/0.22
    20  0.486/0.668     1   −12.4 → −36.5   2.48/5.14 → 0.20/0.56  2.41 → 0.06   −14.1 → −26.6   1.92/2.54 → 0.20/0.61
    21  0.379/0.407     1   −13.5 → −46.6   1.86/2.91 → 0.05/0.18  1.89 → 0.03   −14.7 → −34.0   1.68/2.19 → 0.09/0.28
    22  0.163/0.095     1   −18.4 → −59.3   1.08/1.63 → 0.01/0.04  1.11 → 0.01   −18.5 → −49.5   1.02/1.24 → 0.01/0.05
    23  0.140/0.117     1   −18.1 → −58.7   1.11/1.77 → 0.01/0.05  1.11 → 0.01   −18.4 → −48.1   1.03/1.27 → 0.02/0.06

Targets met (spectrum ±0.25 dB, envelope ±0.25 dB, null ≤ −30 dB) for every setting with High air ≤ 0.5 — 1171 of
her 1247 slots. Above High air 0.5 (states 4, 8–10, 12, 20: 93 slots) the high band's second stage is only
approximated: takes within 0.16–0.25 dB RMS / 0.46–0.67 dB max per third-octave, null −35…−39 dB. A steady 16 kHz
sine at −30 dBFS (just past the second knee) is the worst single point: 0.36 dB. Golden test:
`fresh_air_dynamic_model_matches_the_trial_round124` (14 reference points, ±0.25 dB; that one ±0.4).

## 4. Doubler → Gemini Doubler

[DATA] Her states are all Doubler2 stereo (`WD2S`): 2 settings — her main one (2048 slots: voices 78.2 ms / −1 cent
hard left and 100 ms / 0 cent hard right, feedback 39 %, direct aligned) and the factory "Dark Fader" (2 slots,
MÚSICA.flp: ±6 cents, octave voices). No Doubler4 in her corpus (voices 3–4 off in every state).

[EXEC] (impulses, impulse trains at ±1 … ±100 cents, sines, 44.1 / 48 kHz; tmp/vm124/py/db1–db4, dbph, dbval):
- Align = exactly 320 samples on the direct signal at 44.1 and 48 kHz (a sample count, not 6.67 ms).
- A voice at 0 cents is its delay + 495 samples, a single sample (no smearing); every feedback repeat one sample
  more; the repeat gain is feedback % × a first-order (bilinear) 16 Hz high-pass: first sample 0.39 × 0.998954, −3 dB /
  +45° at 16 Hz.
- The pitch shifter is ONE read head, linearly interpolated (an impulse splits 0.956 / 0.044), whose offset over the
  delay starts at 495 and drifts by 1 − 2^(cents/1200) per sample inside ~496 samples (pitch down: 79.4 … 575.7, then a
  fade over 1.3 samples of offset and a jump back; pitch up: 62 … 558, a second head enters at the top). The jump size
  depends on the material (495 on impulse trains, 305 on pink noise, 256 after silence): the splice point is chosen
  by the signal [model: a correlation search], so the detuned voice's position can't be predicted sample for sample.
  `reset` does not reset the head; it starts at instantiation.
- LYNX: Gemini's voices (non-octave, aligned) now use this head: 495-sample start, linear reads, the measured ranges and
  fades, 320-sample align, first-order feedback high-pass. The octave voice keeps the two-grain shifter.

Her states, LYNX − official, her three takes and pink noise (tmp/vm124/dbval_ba.json; L = the −1-cent voice side):

    setting                take   L null  1/3-oct RMS/max  env       R null          1/3-oct RMS/max
    main (2048 slots)      go     0.1 → 0.5   1.30/3.27 → 0.78/2.86  2.05 → 1.17   −19.9 → −133.6  0.29/1.32 → 0.00/0.00
                           kwe    0.9 → −0.1  1.32/4.23 → 0.82/3.21  2.24 → 1.61   −24.4 → −126.5  0.29/1.31 → 0.00/0.00
                           lu     0.5 → 1.7   1.38/3.89 → 0.77/3.20  1.56 → 1.58   −20.4 → −131.9  0.31/1.45 → 0.00/0.00
                           pink   −4.8 → −4.1 0.79/2.14 → 0.26/0.66  0.73 → 0.41   −12.1 → −108.1  0.32/1.45 → 0.00/0.00
    Dark Fader (2 slots)   go/kwe/lu  ≈ −0.5 (unchanged)  1.5–2.8 dB RMS (octave voices)

The right side (0-cent voice + direct + repeats) is now sample-exact (−108…−134 dB). The left side carries the
−1-cent voice: same timing law, level and repeats, but its read position depends on where the reference's splices
landed, so it can't null; its spectrum is within 0.8 dB RMS (comb positions of a few samples' difference). Golden
test: `doubler_her_state_timing_is_sample_exact_round124`.

## 5. H-Delay → Echoform's hybrid engine

[EXEC] (impulses −66 … +6 dBFS, impulse pairs, bursts, her takes, Analog Off and Mod2, 44.1 / 48 kHz; py/hd1–hd3,
hdval):
- Ping-pong: the left line takes (L + R)/√2 + fb² × the right echo, the right line the left echo; the left echo comes
  T after the input, every later pass T + 1 samples; host sync at 120 BPM: T = 48000 / 44100 (1/2 note).
- Every write into the lines goes through y = v·g with g = min (1 / (1 + |v|), released toward 1 with a 10 ms time
  constant): a lone 0 dBFS impulse on L comes back as 0.41421 (= x / (1 + x) at x = 1/√2), the right echo 0.29289,
  the next left one 0.09538; a −40 dBFS impulse 10 ms after a 0 dBFS one comes back reduced by the same released gain
  (480 samples at 48 kHz, 441 at 44.1: a time constant, not a sample count). The two lines SHARE g (a loud left pass
  turns the right one down): without that her takes only nulled to −16…−29 dB.
- Analog Off: exactly that, no noise. Mod2 ("analog 2", every state of her corpus): the same loop between an input
  gain 0.887 and an output gain 0.978, a +0.63 dB low shelf at 942 Hz on the echoes (fitted on her takes) and a hiss
  at ≈ −85 dBFS (not reproduced).
- LYNX: Echoform gets an appended "Hybrid" parameter (off / clean / analog; older projects: off = unchanged). The
  translator uses it whenever the state has no filter, modulation or lo-fi (all of her states); feedback, mix and
  output are taken as they are (round 119's static −1.4 / −4.4 dB offsets are gone).

- The mix is equal-power (cos / sin of mix × 90°, Analog off, 0 … 100 % in 10 % steps) and the Output level applies
  to the whole output, dry included; with Analog on the dry path goes through the analog stage too (× 0.928 on her
  mix-41 state).

[EXEC] Analog Off (the engine alone), her state 0, her takes: null −110 dB (go, kwe) — sample-exact. Her 56 distinct
settings (all Mod2), LYNX − official on her three takes (worst of takes and sides; before = round 119's Echoform
translation; tmp/vm124/hdval_all2.txt):

    slots  setting                                              worst null    1/3-oct RMS   |level| (dB, before → after)
    1319   host, note idx 16, fb 60, mix 100, ping-pong, out +0.0  -2.3 → -32.4   3.3 → 0.2   4.3 → 0.1
     180   own BPM 125, 360 ms, fb 60, mix 100, ping-pong, out +0.0  -2.7 → -32.0   3.4 → 0.2   4.1 → 0.1
      74   host, note idx 13, fb 60, mix 100, ping-pong, out +18.0  -2.3 → -32.5   3.7 → 0.2   4.3 → 0.1
      42   host, note idx 16, fb 5, mix 100, ping-pong, out +0.0  -3.3 → -32.6   3.1 → 0.2   4.1 → 0.1
      38   host, note idx 16, fb 11, mix 100, ping-pong, out +0.0  -3.3 → -32.6   3.1 → 0.2   4.1 → 0.1
      32   host, note idx 16, fb 62, mix 41, ping-pong, out +0.0 -10.6 → -28.2   1.2 → 0.3   1.4 → 0.0
      21   host, note idx 13, fb 60, mix 100, ping-pong, out +6.8  -2.3 → -32.5   3.7 → 0.2   4.3 → 0.1
      16   host, note idx 16, fb 41, mix 100, ping-pong, out +0.0  -3.0 → -32.5   3.2 → 0.2   4.1 → 0.1
      10   own BPM 125, 960 ms, fb 60, mix 100, ping-pong, out +0.0  -2.2 → -32.4   3.3 → 0.2   4.3 → 0.1
      10   host, note idx 13, fb 40, mix 100, ping-pong, out +0.0  -3.2 → -32.6   3.4 → 0.2   4.1 → 0.1
       7   host, note idx 13, fb 31, mix 21, ping-pong, out +0.0 -16.5 → -27.9   0.9 → 0.4   0.8 → 0.0
       7   host, note idx 13, fb 31, mix 15, ping-pong, out +2.1 -14.8 → -27.8   1.4 → 0.4   1.4 → 0.0
       6   host, note idx 13, fb 43, mix 100, ping-pong, out +0.0  -3.1 → -32.6   3.5 → 0.2   4.1 → 0.1
       4   host, note idx 13, fb 6, mix 100, stereo, out +0.0 -13.7 → -31.7   2.0 → 0.2   1.8 → 0.1
       4   host, note idx 13, fb 11, mix 100, ping-pong, out +0.0  -3.5 → -32.6   3.4 → 0.2   4.0 → 0.1
       3   own BPM 122, 246 ms, fb 60, mix 100, ping-pong, out +0.0  -3.1 → -32.0   3.1 → 0.2   4.2 → 0.1
       2   host, note idx 13, fb 15, mix 50, ping-pong, out +0.0  -9.8 → -28.5   1.4 → 0.3   1.6 → 0.0
       2   own BPM 134, 149 ms, fb 60, mix 100, ping-pong, out +0.0  -2.1 → -31.8   3.3 → 0.2   4.5 → 0.1
       2   host, note idx 12, fb 60, mix 100, ping-pong, out +0.0  -3.3 →  -9.2   3.3 → 0.2   4.1 → 0.1
       2   host, note idx 16, fb 60, mix 54, ping-pong, out +4.6  -6.0 → -28.6   2.0 → 0.3   1.9 → 0.0
       2   host, note idx 13, fb 60, mix 100, ping-pong, out +4.9  -2.3 → -32.5   3.7 → 0.2   4.3 → 0.1
       1   host, note idx 13, fb 61, mix 11, ping-pong, out +0.0 -18.9 → -27.8   0.9 → 0.4   0.7 → 0.0
       1   host, note idx 10, fb 60, mix 100, ping-pong, out +0.0  -2.6 → -31.9   3.2 → 0.2   4.2 → 0.1
       1   host, note idx 10, fb 54, mix 6, ping-pong, out +0.0 -20.5 → -27.8   0.9 → 0.4   0.7 → 0.0
       1   host, note idx 9, fb 52, mix 100, stereo, out +0.0 -12.1 → -13.3   1.6 → 0.2   1.2 → 0.1
       1   host, note idx 10, fb 60, mix 100, ping-pong, out +10.5  -2.6 → -31.9   3.2 → 0.2   4.2 → 0.1
       1   host, note idx 16, fb 19, mix 100, ping-pong, out +0.0  -3.2 → -32.6   3.1 → 0.2   4.1 → 0.1
       1   own BPM 114, 526 ms, fb 60, mix 100, ping-pong, out +8.2  -2.7 → -32.4   3.6 → 0.2   4.3 → 0.1
       1   host, note idx 13, fb 38, mix 100, ping-pong, out +8.2  -3.3 → -32.6   3.4 → 0.2   4.1 → 0.1
       1   host, note idx 13, fb 40, mix 100, ping-pong, out +18.0  -3.2 → -32.6   3.4 → 0.2   4.1 → 0.1
       1   host, note idx 13, fb 37, mix 100, ping-pong, out +18.0  -3.3 → -32.6   3.4 → 0.2   4.0 → 0.1
       1   host, note idx 13, fb 54, mix 100, ping-pong, out +18.0  -2.7 → -32.5   3.6 → 0.2   4.2 → 0.1
       1   own BPM 120, 500 ms, fb 38, mix 100, ping-pong, out +8.2  -3.3 → -32.6   3.4 → 0.2   4.1 → 0.1
       1   own BPM 105, 571 ms, fb 34, mix 100, ping-pong, out +11.6  -4.1 →  -9.3   3.2 → 0.2   4.0 → 0.1
       1   host, note idx 11, fb 21, mix 51, ping-pong, out +0.0  -9.6 → -28.5   1.4 → 0.3   1.6 → 0.0
       1   host, note idx 13, fb 60, mix 100, ping-pong, out +0.0  -2.3 → -32.5   3.7 → 0.2   4.3 → 0.1

Targets met for every 100 %-mix ping-pong setting (≈ 1900 of 2072 slots): null −32 dB (the Mod2 hiss and the analog
stage's small level dependence are what is left), spectra 0.2 dB RMS, level 0.1 dB. Mix below 100 %: −28 dB. Not
matched: the two settings with a fractional delay (1/8 note at 120 BPM host = 15 999.99 samples; 571.4 ms) — the
reference interpolates (a windowed-sinc-like kernel), Echoform's hybrid line rounds to whole samples: −9 dB null,
spectra still 0.2 dB; and one stereo-mode setting (1/16, feedback 52 %, 1 slot): −13 dB. Golden test:
`h_delay_hybrid_engine_matches_the_trial_round124`.

## 6. NS1 → Quell Denoiser (partly matched)

[DATA] Her NS1 states hold 4 settings (RealWorld 229): 23.1 % (2087 slots), 29.8 % (82), 69.6 % (68), 39.5 % (3).

[EXEC] (pink / white noise at −60 … 0 dBFS and amounts 0 … 1 in 0.05 steps, band-limited loud noise + quiet probe,
level steps, tone vs gated tone, her takes at 0 / −12 / −24 dB; py/ns2–ns6):
- Zero latency. At amount 0 the output is the input through two all-passes, RBJ 60.07 Hz Q 1.0 and 16 kHz Q 0.90
  (phase residual 0.08° RMS) — the trace of a three-way split (below 60 Hz, 60 Hz – 16 kHz, above 16 kHz) whose middle
  is processed without any phase change when open. Round 119's Quell (6-band LR4 split) has a very different phase,
  so it could never null (−1 dB on her takes).
- With the amount above 0, a fixed tilt appears on loud material: −0.5 dB at 4 kHz, −1.4 at 8 k, −3.6 at 16 k, the
  same from 15 to 80 % (her takes' loud frames show it too), and the band below 60 Hz is cut (−15 dB at 25 %, −38 dB
  from 50 %).
- Level dependence is absolute, not floor-relative: her take at −12 / −24 dB is reduced more (23.1 %: 63 Hz −4.3 →
  −7.7 dB, mid bands −0.1 → −1.2 dB). One loud band opens a wide neighbourhood (a 500–900 Hz band opens 200 Hz – 8 kHz).
- It adapts slowly: steady pink noise at −40 dBFS is reduced 9.4 dB at first and 0.4 dB after 20 s (69.6 %); after
  3 s at −20 dBFS, −50 dBFS noise is reduced 26–33 dB and recovers over ~15 s; bursts shorter than 200 ms don't move
  that reference, 1 s does. [model] a slow level reference per band that sets the thresholds — not reproduced yet.
- NS1 too muted ~1 s every ~20 s in one 21-run session (ASK_USER).

LYNX: Quell gets an appended "Matched" switch (older projects: off = the round-119 engine). On: the measured all-pass
pair, the band below 60 Hz and above 16 kHz as gain-reduced parts of that split, seven octave peaking cuts from 173 Hz
(0 dB = exactly transparent), linked power detectors, a soft-knee downward expander whose thresholds move with the
amount, cross-band opening (spread), fast open / slower close, and the fixed 9 kHz −3 dB tilt. Fitted (LM over her
takes and pink −40 dBFS at her four settings; tmp/vm124/ns/nsmodel.cpp, ns/best2.json; the C++ engine equals the
fit model to −118 dB). `translate = waves-ns1` turns it on.

Her four settings, LYNX − official, her three takes (py/nsval.py, tmp/vm124/nsval.json):

    amount  take   null before → after   level before → after   1/3-oct RMS/max before → after   env RMS/max before → after
    23.1 %  go     −1.3 → −18.2          −0.15 → +0.06          1.09/4.15 → 0.95/3.30            0.80/5.07 → 0.63/3.59
            kwe    −5.8 → −19.7          +0.06 → +0.05          2.54/10.12 → 1.66/7.15           1.36/7.61 → 1.35/6.96
            lu     −1.2 → −18.9          −0.14 → +0.07          2.06/6.18 → 1.26/5.08            1.18/7.24 → 0.99/5.61
    29.8 %  go     −1.3 → −19.4          −0.14 → +0.06          1.53/5.38 → 1.19/5.11            0.79/5.17 → 0.61/2.14
            kwe    −5.8 → −20.1          +0.07 → +0.05          3.30/13.61 → 2.00/9.15           1.64/10.38 → 1.51/9.07
            lu     −1.2 → −20.1          −0.12 → +0.07          2.57/9.33 → 1.64/6.96            1.19/7.54 → 1.04/5.99
    39.5 %  go     −1.3 → −20.3          −0.13 → +0.06          1.84/6.00 → 1.07/4.50            0.81/5.37 → 0.67/3.19
            kwe    −5.8 → −20.7          +0.08 → +0.04          3.91/16.10 → 1.96/9.29           1.62/8.02 → 1.38/6.65
            lu     −1.2 → −20.9          −0.10 → +0.07          2.92/11.35 → 1.78/6.87           1.28/7.81 → 1.13/6.07
    69.6 %  go     −1.4 → −20.5          −0.10 → +0.04          1.84/5.82 → 3.13/12.10           0.65/5.91 → 0.92/3.22
            kwe    −6.0 → −19.2          +0.10 → +0.00          4.68/18.41 → 2.18/8.05           2.02/10.18 → 1.68/7.72
            lu     −1.2 → −20.6          −0.04 → +0.04          2.04/5.89 → 2.25/9.11            1.62/9.90 → 1.48/7.76

Better everywhere on the null (−1 → −18…−21 dB: the right phase) and level, better on most spectra, but NOT within
the round's targets: 250 Hz – 3 kHz agrees within 0.3 dB, the rest is off by 1–2 dB (above 5 kHz) and up to 5–12 dB
at 31–50 Hz, and quiet passages (breaths, room tone) differ by several dB because NS1's slow reference is missing.
At 69.6 % (68 slots) the 40–50 Hz band is worse than before. What is left, in order: the adaptive reference
(per band, ~1 s attack, ~1–2 dB/s release), the 31–60 Hz band at high amounts, the exact HF tilt / 16 kHz band shape.

## Where I stopped
- Done and landed: the inventory (§0); Fresh Air → Aether dynamic air model (§3); Doubler → Gemini measured
  shifter / timing (§4); H-Delay → Echoform hybrid engine (§5); NS1 → Quell "Matched" engine, partly (§6); golden
  tests `tests/Match124Tests.inc`; the fork's Pro-Q 4 / S1 Imager / VintageVerb work (extras doc) lands with it.
  `tests/SubstituteTests.inc`: the "Waves sub-plugin without a substitute" example is now Vocal Rider (S1 Imager has
  one since the extras).
- Not matched, and why: the Doubler's detuned voice (its splice point follows the material; start phase set at
  instantiation), H-Delay's fractional delays and one stereo setting (§5), NS1's slow adaptive reference and its
  31–60 Hz band at high amounts (§6), Fresh Air above High air 0.5 within 0.7 dB instead of 0.25 (§3); everything that
  has no official trial here (§0 table).
- NS1's adaptive reference was tried (per-band reference, 141 ms rise, 1.1 dB/s fall, thresholds follow it by 0.49;
  tmp/vm124/ns/fit3.txt): it fixed the noise-adaptation tests (pink −50 dBFS from a cold start: 5.3 → 1.2 dB band
  error; after a loud passage 3.7 → 2.9) but made her takes worse (up to +1.5 dB band error at 69.6 %), so it is NOT
  in the landed engine.
- Waves still in demo after the user's activation (ASK_USER): mutes every 20 s of processed audio in NS1, H-Delay and
  Doubler2 (21:40); every number here masks them.
- Next: NS1's adaptive reference with a better structure (global + per band, fitted on takes and conditioning
  signals together); H-Delay's fractional-delay
  interpolation; Gemini octave voices with the measured shifter; the user's trial installs / licence checks (§0, §2).
