# REA round 135w — parity of the Waves roles and of Echoform (H-Delay, ValhallaDelay, EchoBoy / PrimalTap)

Sub-round of parity135 (branch `feature/parity135w`). English-first; clean-room; third-party plugins black-box only
(hosted in our own host, public parameter API, test signals; nothing decompiled, no assets / presets / names copied).
Evidence markers: [EXEC] measured here, [DATA] read from saved states, [MANUAL] from the vendor guide, [model] our law.
No Waves plug-in was hosted or measured in this sub-round: the Waves trials on this PC run in demo mode (periodic audio
mutes), so Waves facts here come from earlier rounds' captures (parameter lists / RealWorld probes, not demo-muted
audio). The only new audio measurement is EchoBoy's (Soundtoys, activated trial).

## 1. Echoform (docs/PARITY.md: H-Delay, ValhallaDelay, EchoBoy / PrimalTap rows)
New parameters, appended after `hybrid` (every default = the old sound): Tempo source + BPM (30–300), Start side,
Filter link, Tap A–D, Swell, Groove, Feel, Accent, Prime numbers, Cross feedback, Balance, Freeze. Layout 780×300 →
950×330: a TWEAK section (Groove, Feel, Accent, X-feed, Balance, Primes), an OUT meter with a clip lamp at the right
edge, a second row (Quad taps / ping-pong start side, Project | Own BPM + BPM, Link EQ, Freeze), Repeat | Swell where
the right sync menu sits in the two-time styles. Engine (`EchoSuite.cpp`):
- own BPM: `tempoFor()` — the note values follow the echo's own tempo; [DATA] the H-Delay translator keeps her nine
  own-BPM states as note + tempo (RealWorld 1 = 0, RealWorld 2 = BPM) instead of folding them into milliseconds;
- start side: the ping-pong loop's first side is swapped (both engines, the hybrid one too); [EXEC] round 119's probe
  (`tmp/w119/wv/H-Delay_0_probe.txt`): RealWorld 45 = "Ping Pong Start Direction" 0 Left / 1 Right — translated;
- quad taps: a muted tap is not heard; Repeat feeds back the last tap (heard or not), Swell the mean of the taps heard
  [model];
- Groove / Feel [EXEC] on the official EchoBoy trial (Single echo, "Digital Delay" style, 250 / 500 ms, impulse):
  Groove +0.25 → the odd repeats 113…125 ms late at 500 ms (even ones on the grid), −0.25 → early; Groove +0.125 →
  +52 ms; Feel +0.125 → every repeat +56…62 ms at 500 ms. Law: odd repeats ± groove × T, the train ± feel × T. (At 250 ms
  Feel ±0.25 measured ±105 ms rather than ±62: not resolved, the envelope picker smears a tape-like style; left as is.)
  Implementation: Feel reads the heard echoes from their own tap (feedback from the plain tap); Groove / Accent use a
  loop of two echo times (odd repeats at T (1 + feel + groove), even ones at T (2 + feel) with g), so they work in
  the plain-read characters (Tape … Clean) and the Single / Ratio / Dual styles; the shifter / pitch / reverse /
  phaser characters ignore them.
- Accent [MANUAL] (not a host parameter on the trial): odd repeats × (1 + a), even × (1 − a) [model];
- Prime numbers: the delay in samples snaps to the nearest prime; Cross feedback: each side's loop takes that share
  from the other side; Balance: the echoes' L / R level; Freeze: no input, unity loop, no colour / cuts / limiter.
- meters: `controlOutput` 2 / 3 = input / output peak (Echoform added to `hasLiveMeters`); UI: the "outmeter"
  display (range in `steps`, default 48 dB), click = reset of the peak and the clip lamp. Filter link is UI only
  (`echoform::linkedEdit`: the other cut moves by the same frequency ratio).

## 2. Hush ← DeEsser
OUT meter (−30 … 0 dBFS) beside IN / GR in the Hush editor with a clip lamp and the highest peak (click resets); a
"Classic split (band detector)" preset folder with the reference's five frequencies (5.5 / 4.5 / 3.4 / 6.8 / 5.1 kHz)
under LYNX names, classic voicing, band detector, split.

## 3. Gemini ← Doubler2 / Doubler4
`gemini::voices` 2 → 4: voices 3–4 appended after voice 2 (their indices are the old `paramCount` onwards), off by
default, odd voices on the left input; "Voices 1-2 | Voices 3-4" pages over the same rows; graphs draw voices 3–4
when on. RESET (momentary toggle, appended last): a flip restarts every voice's LFO. Translator: blocks 46 / 56 only
for a state whose PluginName is Doubler4 (a Doubler2 state carries those blocks too [DATA, spec]); their layout is
assumed like 26 / 36 [model], sources 3 / 4 not probed (own side). Clip lamp on the iometer (OUT turns red; click
resets).

## 4. LYNX Tune ← Waves Tune Real-Time
Appended block (round 135w): Speed link (on = old behaviour), Speed (0.1–800 ms, [EXEC] the trial's taper from round
120: 0–0.5 → 0.1–50, 0.5–⅔ → 50–120, ⅔–1 → 120–800), Tolerance cents (Off–40 c, added to the 0.03 st hysteresis),
Tolerance time (Off–300 ms: a new note must persist), Keep vibrato + Vibrato amount 0–200 % (multiply the Natural
vibrato gain; off = flattened), Correction 0–100 % (scales the shift, transpose kept whole). Speed's law [model]
follows round 120 §4.1 [EXEC] (on the trial Speed only shows below ~5 ms): with Link off a share min (1, 1 ms / Speed)
of the in-note deviation is flattened. Translator: RW 20 → Speed (Link stays on unless Speed < 5 ms, so her renders
do not move), RW 22 → Correction [model]. A "Real-time" page holds them with IN / OUT meters (meter outputs 3 / 4;
the iometer display takes its first output from `steps`). Sub-round f owns Auto-Tune's Classic / Learn in the same
enum: both blocks are appended, the parent merges.

## 5. Compressor ← Renaissance Compressor
Appended: Release mode (ARC / Manual), Release curve (Electro / Opto), Character (Warm / Smooth), Limiter. The
vintage voicing (`vmatch::progcomp`, fitted on her ARC + Electro + Warm state) is unchanged at the defaults; Manual =
one release time, Opto = the slow path's curve inverted, Smooth = ratio as shown [model — none measured yet]. The
0 dBFS output limiter (instant gain drop, 50 ms recovery) and its pill (meter 4) work in both voicings; the RComp
translator reads RW 17 / 22 / 16 [EXEC round 113 list] and switches the limiter on (the reference always has it).

## 6. PARITY.md
Stale rows of the Waves tables (header / presets / A-B rows solved by round 129's preset bar, Glow's VU selector /
Trim / Auto makeup, Ridge's meter, RComp's meters / mix / trim / knee / latency, CLA-76 → Crest since round 127) marked
✅ with the round that did them; the round-135w rows marked; a 135w score table under the Waves score table.

## Where I stopped
- Done: §1–§6, tests/Parity135wTests.inc (defaults, own BPM, primes, start side, quad taps, freeze, meters, groove /
  feel / accent timings, filter link, H-Delay own BPM + start side translation, Gemini voices 3–4 + Doubler4
  translation, LYNX Tune real-time defaults / taper / meters, RComp switches + limiter).
- Fixed on the way: H-Delay RealWorld 10 — the probe shows 1 = PingPong, 2 = Stereo; the translator read every value
  ≥ 0.5 as ping-pong (her states are all 1: nothing changes for her).
- Not done: Era (ValhallaDelay), EchoBoy's Rhythm mode and FB Balance, Doubler4 source 3 / 4 probe, Glow ANALOG
  (hum + noise), the measured laws of Manual / Opto / Smooth / Tolerance (r136 can fit them on the trials).
