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

(in progress)

## Where I stopped / next steps
- Items 1–3 implemented on feature/route110; item 4 renders running (tmp/r110/demo).
