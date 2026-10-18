# REA round 110: open items of round 107 (kwe stems, insert fader law, route knobs, demo baselines)

Follow-up of docs/analise/REA_rodada107.md. Evidence markers as in round 39: [DECOMP] read in the disassembly of
FL's own FLEngine_x64.dll (FL Studio 2026, SHA-256 6d9c5ab0…, the same file as round 2), [EXEC] FL Studio's
command-line render (`FL64.exe /R /Ewav`) on this PC, [DATA] the project files, [model] our reading.
FL captures and renders stay in tmp/r110 and C:\Users\kauan\lynx-test\r110 (never in the repo).

## 1. `kwe`: the beat stems "stop at 78 s"

[DATA] The stems (channels 1–4, `…_drums/_bass/_instruments/_vocals.wav`, 44.1 kHz float stereo, 106.4 s) are NOT
time-stretched: their 0xD7 TIME field (+0x60) is 0. The "TIME 140" noted in round 107 §7 was the first dword of the
event, not the stretch time. The stems' playlist items:

    item   pos (ticks @96)  length  start/end offset (ms)     note
    0..3   0                ~15230  0 / 73216                 first 73.2 s of each stem
    22..24 15252 (73.3 s)   ~3172   77019.27 / 92278.9        the last part: 77.0 s into the stems
    29..31 16032..16035     93..96  77076.9 / 77538.5          short overlapping copies (77.1 → 77.5 s)

Items 29–31 lie inside items 22–24 on the same channels and end at ≈ 77.5 s. Before round 107 §5 the audio-clip
sampler stopped every voice of the channel when any clip of that channel ended, so the end of the short copies cut
the long clips: silence from ≈ 78 s. [EXEC] The same file rendered now (main 83c6438, which has the round 107 §5
per-clip keys) plays the stems to the end (novox variant: −12.7 / −14.6 / −15.4 dB in 60–70 / 70–80 / 80–89 s; the
pre-§5 render was silent from 78 s). So item 1 was already fixed by round 107's overlapping-clip fix; the
"stretch 0.73" lead was a misreading.

[EXEC] A drums-only variant rendered by FL on this PC (channel 1 → insert 1, everything else at volume 0) against
LYNX: same level in every second (≤ 0.01 dB), lag 0, but from 73.3 s on the waveforms differed (local ESR −8 dB):
LYNX played the 73.3 s clip 2 samples late. Cause: the clip's start offset is given in milliseconds (77 019.27 ms)
and LYNX rounded it to whole ticks (960 PPQ at 130 BPM = 21.2 samples per tick). Fix: `Clip::offsetFraction` keeps
the sub-tick rest of an imported millisecond offset (also for stretched clips); the scheduler adds it to the sample
start; saved in .lynx (12th clip field, only when not 0); typing a new offset in ticks drops it; "fit to tempo"
scales it with the offset.

[EXEC] With the sub-tick offset kept, the lag is 0 but the 73.3 s clip was still only −23 dB ESR against FL (the
rest of the song −85.6 dB = exact): the clip's start frame (77 019.27 ms × 44.1 = 3 396 549.807) is fractional and
LYNX interpolated from it. FL starts an audio clip on a whole source frame: snapping the start to `round` or `ceil`
makes 77.6–86 s exact too (−85.5 dB ESR; `floor` is 1 sample late, −30 dB). `round` is used (ceil and round cannot
be told apart here: the fractional part is 0.807); the 73.4–77.6 s window stays at −30 dB (the short overlapping
copies, items 29–31, their fades/cuts). Sampler notes (channel mode) are unchanged (their region start is floored).

## 2. Insert fader law

[DECOMP] FL computes an insert's gain in `0x127b050` (round 2's FUN_0127b050): volume field T+0x384 (int); 12800
→ gain 1 exactly (short path); 0 → 0; otherwise v = vol · (1/12800) in float, g = (exp (v · K) − 1) · 0.1 in double,
stored as float, where K is a float initialised at start-up (`0x6a6937`) to ln (11) (the log helper `0x40c1d0` is
called on the constant 11.0). That is g = (11^v − 1) / 10 — LYNX's `faderGain` (position = vol / 16000, × 1.25).
Polarity negates g, then the pan law; nothing else depends on the volume.

[EXEC] FL renders on this PC of the drums-only `kwe` variant with insert 1's volume (0xE1, 0x1FC0) set to 16000,
12800, 10795, 9600, 6400, 3200 and 800, the route 1 → Master level (0x1F40 in this FL 24.2 file) at 16000 and 6400,
and the Master volume at 9600, against LYNX renders of the same files (level 30–86 s, LYNX clipped to ±1 like
FL's 16-bit file):

    variant         FL dB    LYNX dB   FL rel   LYNX rel
    vol 12800      -13.87   -13.87     0.00     0.00
    vol 16000       -9.68    -9.68    +4.19    +4.19   (both clip; unclipped LYNX is +5.59)
    vol 10795      -17.53   -17.53    -3.66    -3.66
    vol 9600       -19.81   -19.81    -5.94    -5.95
    vol 6400       -26.57   -26.57   -12.70   -12.70
    vol 3200       -35.58   -35.57   -21.70   -21.71
    vol 800        -49.69   -49.69   -35.82   -35.82
    route 16000     -9.68    -9.68    +4.19    +4.19
    route 6400     -26.57   -26.57   -12.70   -12.70
    master 9600    -19.81   -19.81    -5.94    -5.95

Every value within 0.01 dB, sample-aligned (lag 0). The fader law needs no change. The 0.3 dB of round 107 §7
(10795 → 12800 moved FL by +4.0 dB on her laptop, the curve says +3.7) was measured on her vocal chain with
plugins still in the path and FL's clipping of a 16-bit file; it is not the fader: with nothing else in the path FL
moves +3.66 dB for the same change. Not reproduced, closed.

## 3. Route knobs in LYNX's mixer

FL shows a route switch with a level knob under every insert the selected insert can send to, including Master.
LYNX had the knob only for insert → insert sends; the route to Master (`MixerTrack::masterLevel`, imported since
round 107) had no control. Now:
- `Song::routeLevel (from, to)` / `setRouteLevel (from, to, level)` read/set any existing route's level (to = 0:
  Master), clamped to 0..maxFaderGain (+5.6 dB).
- The mixer draws the knob under the Master strip too when the selected insert routes there; same gestures as the
  send knob (drag, wheel, double-click/reset = 0 dB, right-click: value menu and "Remove the route to Master").
  Enabling a route starts it at 0 dB and dragging right after the click sets its level (now on the fader curve with
  the dB tooltip; it was a linear 0..1 drag before).
- pt-BR: "Level to Master" = "Nível para o Master", "Remove the route to Master" = "Remover a rota para o Master".

## 4. Demo regression baselines and route levels

[DATA] 16 of the 30 regression demos (tmp/demos.txt) have route levels other than 12800 (or a non-zero level on a
route to Master), so round 107's mapping (fader curve of value/16000; before: value/12800 linear, clamped to 0 dB,
and no level on the route to Master) changes them. A/B with one binary (main 83c6438 renderwav): each demo as is
and a copy whose route levels were rewritten so that the new mapping gives the old gains (tmp/r110/oldmap.py),
both compared with FL's own render of the demo (flref cache, scripts/flref_compare.py; whole song):

    demo                                   NEW: rms_db  spec_db env_corr     OLD: rms_db  spec_db env_corr
    NewStuff / Ookay - Thief                   -0.75     2.73    0.764            -0.73     2.73    0.763
    Astes - Bien Duro                          +0.83     1.94    0.631            +0.83     1.94    0.631
    Atthar … Right Night feeling               -0.63     2.44    0.863            -0.71     2.55    0.865
    Bombs Away … I'm Awake                     -1.22     1.40    0.816            -1.23     1.39    0.816
    Chan Gibbons x Ceebeaats - Time            -0.53     0.74    0.942            -0.48     0.70    0.940
    Cuebur & Thandi Draai - Africa's Calling   -0.57     1.55    0.708            -0.52     1.54    0.663
    DEDZII - NIGHT TERROR                      -1.33     3.25    0.822            -1.33     3.25    0.822
    Edlan & Ella Noël - Song For You           +0.03     1.39    0.962            +0.45     1.43    0.944
    Electroconductor … SH-1 Rework             -0.16     3.90    0.918            +0.06     3.91    0.917
    Eric Kauffmann & NrgMind - Exoplanet       -0.14     0.73    0.618            +0.49     0.45    0.638
    Jay Cactus x Confz - Mic Check Ready       -0.65     0.83    0.968            -0.47     0.95    0.967
    LollieVox & Lezina J - Madonna             +0.41     0.89    0.959            +0.41     0.89    0.959
    Mehdi Ebrahiminejad - Never Out            -1.75     1.76    0.742            -1.69     1.80    0.738
    Mehran Abbasi - With You                   -1.56     1.68    0.778            -1.61     1.71    0.779

(NewStuff.flp is the same file as Ookay - Thief.flp, byte for byte; the regression list has it twice. SeamlessR -
Bass Antics Performance has no usable FL reference: FL's command-line render of the performance-mode project is
1.9 s of nothing, so the pair metrics are meaningless there and it is left out.)

The route-level change moves these demos by at most ±0.6 dB overall, mostly towards FL (Edlan: +0.45 → +0.03 dB,
envelope correlation 0.944 → 0.962; Cuebur 0.663 → 0.708); the whole-song metrics are dominated by other,
unrelated differences (≥ 0.5 dB, spec 0.7–3.9 dB). Exoplanet's level gets closer (+0.49 → −0.14 dB) while its
spectrum distance grows (0.45 → 0.73 dB: its sends feed reverbs on send tracks 100–103 whose own levels are not
exact in LYNX). The law itself is the measured one (§2, round 107 §3), so the new values are the reference: the
regression baseline (C:\Users\kauan\lynx-test\reg2, exe of a510cac) is re-taken as reg3 with the landed build; the
peaks of the 15 demos above (and of anything with an audio clip at a fractional start frame, §1) are expected to
move, everything else must not.

## Where I stopped / next steps
- Done: items 1–4. Item 1 was two things: round 107 §5 had already fixed the "stop at 78 s" (overlapping copies),
  this round keeps the sub-tick part of millisecond clip offsets (`Clip::offsetFraction`) and starts audio clips on a
  whole source frame (both verified sample-exact against FL's render). Item 2: the fader law needs no change (10 FL
  renders within 0.01 dB). Item 3: route-to-Master knob in the mixer. Item 4: table above; the regression baseline
  moves to reg3 (landed build).
- Open: ceil vs round of the clip start frame (needs a clip whose start frame has a fractional part < 0.5);
  the −30 dB residual of the short overlapping copies (items 29–31) in 73.4–77.6 s of `kwe`; the route-level
  demos' remaining differences are unrelated to this round (Exoplanet's send-track reverbs, etc., see the table);
  SeamlessR / performance-mode projects have no FL reference.
