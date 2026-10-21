# REA round 119: the Waves trials — H-Delay, NS1, Doubler, and the whole vocal chain

The user installed the Waves trials (Waves Central, WaveShell1-VST3 17.1 / 17.2 / 17.4). Scope: check what the shell
exposes and CLA-2A; translate her H-Delay states; give NS1 and Doubler a LYNX role (own engines, state translation,
editors that follow the originals' workflow); compare her vocal chain against the real plugins.

Evidence markers: [DATA] read from her project files / plugin states, [EXEC] black-box runs of the official trials
in tools/fxmeasure / tmp/look114 uiview (host, feed signals, read outputs, read parameters back, screenshot the editor
as a user sees it), FL's command-line render, LYNX's renderwav; [model] our reading. No Waves binary was opened,
decompiled or patched; licensing was not touched. Scripts: tmp/w119/py (hdmeas, ns1a/2/3, db1/2, probe, qfit,
cmpst, chain2, chaincmp); measurements in C:\Users\kauan\lynx-test\w119.

## 1. What the shell exposes, CLA-2A, demo mode

- [EXEC] Shell 17.1 lists 611 plugins (17.2 / 17.4 hold only a few); CLA-2A, NS1, Doubler2 / Doubler4, H-Delay,
  Waves Tune Real-Time, SSLChannel, RCompressor, DeEsser all load and process headless.
- [EXEC] "CLA-2A opens Q10": the FIRST plugin our host instantiates from the shell in a process comes up as Q10
  whatever was asked (also seen as a wrong parameter count). Loaded second (any plugin first), CLA-2A opens its own
  editor (screenshot tmp/w119 ref). Not an activation problem; fxmeasure / uiview scripts now load a dummy first.
- [EXEC] The trials run in DEMO mode: editors show "Demo: Get Full Access" / "Demo with audio mutes" and the audio
  goes to exact zero for ~1 s about every 20 s. Every measurement below masks those runs (exact-zero detection +
  50 ms margins). The user must start / activate the trials in Waves Central (tmp/waves119/ASK_USER.md).

## 2. H-Delay → Echoform

[EXEC] RealWorld layout (one digit edited per value, parameters read back): 1 sync (0 BPM, 1 host, 2 ms), 2 BPM,
3 time ms, 4 note (3 per length — triplet, straight, dotted — from 1/64; 18 = 1 bar, 19 = 2 bars), 6 mix %, 7 feedback,
8 / 9 phase L / R, 10 ping-pong (1) / stereo (0), 14 output dB, 15 low-pass Hz (20100 = off), 17 high-pass (19 = off),
19 / 20 modulation depth % / rate Hz, 21 lo-fi, 23 analog (0 off, 2 = Mod2), 45 ping-pong start side.
[DATA] Her 70 distinct states: host sync 61 (1/2 ×37, 1/4 ×22), own BPM 9, ping-pong 68, feedback 60 in 38, mix 100
in 60, analog Mod2 / hi-fi / no filters / no modulation in all. FL's slot mix is 0–7 % in her projects.

[EXEC] Impulses (each echo's energy), 120 BPM host:
- ping-pong: the mono sum feeds the LEFT echo at T; the right side repeats it at 2T at nearly the same level (-0.3 dB);
  the loop gain per left-right pair is 0.93 × (feedback/100)² (feedback 30 / 60 / 90: -20.9 / -9.5 / -2.7 dB);
- stereo mode: each side on its own, 0.98 × feedback/100 per repeat;
- echoes flat ±0.5 dB 30 Hz–20 kHz; Mod2 adds hum / hiss (~-85 dBFS) and compresses loud input (1 kHz bursts: echo
  gain +3 dB at -34 dBFS, 0 at -10, -2.3 at -4 dBFS); the mix is an equal-power dry / wet (25 %: -0.7 / -8.3 dB).

LYNX: Echoform gets two parameters (appended; older projects load the defaults = unchanged sound): **Output** (-18 …
+18 dB on the echoes) and **Pairs** (ping-pong in pairs: left → right at full level, the loop gain once per pair).
`translate = waves-h-delay`: host sync → note sync (follows the project tempo), own BPM / ms → the time in the list,
feedback → the measured loop laws, mix, output (+ the analog level calibrated on her takes, -1.4 dB), filters, clean
mode. [EXEC] Her state on her three takes, reference − LYNX: left -1.1 / +1.7 / -0.7 dB, right -4.3 / +0.1 / -3.0 dB
(the reference's analog compression of loud takes; the right side follows the left one at unity in LYNX), octave
bands within ±1.5 dB above 250 Hz (the reference's 60 Hz hum harmonics make 125 Hz +2…+15 dB).

## 3. NS1 → Quell Denoiser (new)

[EXEC] One parameter (NS1 0–100 %, RealWorld value 229). Her states: 23.1 % (most), 29.8, 39.5, 69.6 %.
Black-box: loud material untouched (vocal level change -0.1 dB), quiet parts down up to ~25 dB, lows < 105 Hz
pulled down even when fairly loud, the top band a steady -2 dB; fast opening (< 50 ms), closing in ~60 ms; the higher
the knob the higher the thresholds (100 % attenuates even loud tones); some slow adaptation (a steady noise's
attenuation drifts by a few dB over seconds — [model] a noise-floor tracker; not reproduced).

Quell (core/VoiceTools): a phase-coherent 6-band LR4 split (105 / 311 / 921 / 2727 / 8075 Hz, all-pass compensated),
a soft-knee downward expander per band (threshold, depth, slope, a constant trim per band, all interpolated over
Amount), linked stereo, fast open / slower close, Listen (what is removed), Output. The tuning was fitted (coordinate
descent, tmp/w119/py/qfit.py) to the reference's band gains (STFT, 20 ms frames, 6 bands) on three takes (raw and
-12 dB) at 23 / 40 / 70 %: mean |error| 3.46 → 1.75 dB per frame and band. [EXEC] Her states on her takes,
reference − LYNX: level -0.08…+0.15 dB, octave bands 0.13–0.61 dB RMS.

Editor (screenshot of the official trial: reduction meter left, one tall fader right with its read-out): Quell shows
the reduction meter (0 … -30 dB, live) left, the Amount fader right with its value, Listen and Output small at the
bottom — LYNX's own drawing.

## 4. Doubler → Gemini Doubler (new)

[EXEC] Doubler2 parameters read back and laws measured (impulses, 1 kHz / 200 Hz tones, DFT):
- voice pan: equal power over ±45° (−45 = left only, 0 = -3 dB each side); source L+R = (L + R)/2, L-R = difference;
- direct pan is a stereo rotation (−45°: L' = (L + R)/√2, R' = (R − L)/√2); Align delays the direct signal 6.67 ms
  (320 samples at 48 kHz) and every pass through a voice by its delay + 10.3 ms; without Align a voice takes its
  delay alone; the plugin reports no latency (FL mixes the delayed direct with its dry: part of her sound);
- detune exact (±100 c: -100.3…+100.2 c measured), octave = one octave down, depth ±200 c on a triangle LFO at
  rate / 2 Hz (rate 1.0 → 2 s period, 6.3 → 0.32 s), feedback = %/100 per repeat (39 % → -8.2 dB);
- the shelves are first order: a high shelf of gain g at f0 has its corners at f0 and f0·max(g, 1/g) (-12 dB at
  3538 Hz: -2.1 dB at 3.5 kHz, -6.8 at 8 k, -11.9 at 16 k); the low one mirrored [model: its measurement was muted].
[DATA] Her Doubler (1969 slots): voices 78.2 / 100 ms, feedback 39 %, -1 / 0 c, hard left / right, direct aligned,
EQ off — a stereo slap-back rather than a double; FL slot mix 0.31 on her vocal FX bus.

Gemini (core/VoiceTools): two voices, each a pitch-shifting delay (two power-complementary grains reading a Hermite-
interpolated delay line, 13.3 ms / 53.3 ms windows for the 80 / 20 Hz ranges), the laws above, `translate =
waves-doubler` maps every control one to one. [EXEC] Her two states on her takes, reference − LYNX: level -0.20…+0.39
dB, octave bands 0.25–0.93 dB RMS (Doubler_2, octave voices: +2…+6 dB at 125 Hz on one take). Limit: the reference's
shifter is pitch-synchronous; Gemini's octave puts its energy on 75 Hz lines around the target (tests note it).

Editor (screenshot of the official trial): the three pictures on top (voices on a half circle — angle = pan, radius =
gain; delay against detune; the EQ curve), the Direct / FB low cut / Range / shelves row, one row per voice (on, gain,
pan, delay, feedback, octave, detune, depth, rate, source), the output fader on the right.

## 5. Whole vocal chain

[EXEC] FL here does NOT load her projects' Waves slots: they reference WaveShell1-VST3 16.0 / 15.5, FL only has the
17.x shells. Proof: `kwe` (all 15.5) renders identically to LYNX with every substitute bypassed (0.00 dB IQR,
0.05 dB octave spread); `game over` matches LYNX with only Aether (Fresh Air) on (IQR 1.1 dB, 8–16 kHz within 0.8 dB).
So FL's /R render is not a "real Waves" reference here (it is "plugins missing").

Instead, her chain was run in series with the official trials (SSL E-Channel → NS1 → RComp → DeEsser → CLA-2A, each
with the project's own state, her raw lead take) against LYNX's substitutes in series (translated from the same
states). Reference − LYNX after each stage (level, 0.5 s-window IQR, octave spread 63 Hz–16 kHz):

    project      +SSL               +NS1               +RComp             +DeEsser           +CLA-2A
    game over    +0.44 (0.32/0.47)  +0.58 (0.35/1.68)  +0.34 (0.82/2.70)  +0.35 (0.79/2.68)  +0.67 dB (1.36/2.21)
    kwe          +0.38 (0.39/0.90)  +0.42 (0.54/0.57)  +0.49 (0.70/0.42)  +0.49 (0.66/0.41)  +1.27 dB (0.90/0.54)
    luisa sonsa  +0.25 (0.29/0.64)  +0.37 (0.44/1.41)  +0.44 (0.81/2.29)  +0.45 (0.78/2.27)  +1.07 dB (1.48/1.76)

The spreads are dominated by the 63 Hz octave (reference +5…+7 dB: NS1's low band after the SSL's high-pass); from
125 Hz up every octave is within ±1.7 dB, mostly ±0.5. Without RComp / DeEsser (what FL on her laptop has): +0.39 /
+0.75 / +0.70 dB. FX bus: H-Delay §2, Doubler §4 (per plugin).

## Where I stopped / next steps
- Done: §1–§5; tests `tests/VoiceToolsTests119.inc` (Quell, Gemini laws, Echoform pairs / output, the three
  translations), SubstituteTests updated (H-Delay → Echoform, NS1 / Doubler have roles), pt-BR strings.
- Needs the user: activate the Waves trials (demo mutes); for FL to play her projects' Waves slots, FL would need the
  16.0 / 15.5 shells (or a Waves "replace plugin" in each project) — LYNX maps them by class ID already.
- Next: Quell's noise-floor adaptation and its low band after a high-pass (the 63 Hz octave); H-Delay's analog
  compression / hum (Echoform has no such stage); Gemini pitch-synchronous octave; draggable dots in Gemini's
  pictures; Doubler4's voices 3–4.
