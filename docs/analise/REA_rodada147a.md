# REA round 147a (parity147a): Echoform ← EchoBoy's styles and saturation, PrimalTap's Multiply

2026-10-24. Sub-round of parity147 (docs/PARITY.md "Echoform ← EchoBoy / PrimalTap": left after round 144 were the
saturation types, the ~30 style voicings and PrimalTap's Multiply). Evidence markers: [EXEC] measured here on the
official Soundtoys trials (EchoBoy 5.5.5, PrimalTap 5.5.5), hosted black-box in `tools/fxmeasure` (public VST3
parameters, value texts, audio in / out); [model] LYNX's own choice. Nothing was decompiled, no graphics, presets or
names copied: the voicings carry LYNX's own names. Raw data and tables: `D:\lynx-work\parity147\a\` (scripts
m1 … m8 / p1 … p2, renders v*/lp*/st*/pt*, analyses an3.txt, an7.txt, fitsat2.txt, fitcomp.txt, wob.txt,
fittone.txt, pt.txt; the fitting scripts in tmp/parity147/py).

## 0. Host notes [EXEC]

- EchoBoy's `Style` host parameter has 32 steps: 31 factory styles and "Edited Style"; `Saturation` reads 0 … 24 dB.
- A style change takes effect only after the plug-in has processed some audio with the new value: a render made right
  after a style change (even after a 1 s silent settle and 1.5 s of message pumping) is silent. Every measurement
  therefore runs two short dummy renders after the style is set, then sets Saturation again.
- In Single mode both sides are echoed with the Tweak L/R offset: left T − 3.83 ms, right T + 4.17 ms (all styles).
- Mix 100 % at Saturation ≈ 0 gives −2.8 dB on the digital style (the level is part of every style's tone below).
- No style adds hiss: the silent stretch after the signals is digital silence in all 31 (the diffused / verbed
  styles only ring out).

## 1. What was measured per style [EXEC]

Single, 250 ms (400 ms for the loop runs), feedback 0, mix 100 %, Saturation 0.25 / 6 / 12 / 18 / 24 dB:
- **tone**: 200 ms white-noise bursts (−30 dBFS rms) with feedback 0.5 (shown 0.50, loop gain −6.02 dB): repeat 1 =
  output tone × loop tone, repeat 2 / repeat 1 = feedback × loop tone, 28 third-octave bands (an7.txt). Most styles
  put their colour on the first echo only (telephone: repeat 1 −14.7 dB at 100–200 Hz, repeat 2 / 1 only −5.5 dB);
  a few colour every pass (multi-head tape −21 dB at 16 kHz per pass, valve tape and the "honky" style a −8 / −12 dB
  low cut per pass);
- **saturation**: a 1 kHz sine at −40 / −30 / −20 / −12 / −6 / 0 dBFS (0.5 s each): the echo's fundamental gain, 2nd
  and 3rd harmonic (an3.txt). Two families: shapers (tape, BBD, radio-receiver, distorted styles: h3 up to −12 dB at
  0 dBFS) and compressors (cheap tape, the drum, FM, shortwave, transmitter, analog chorus, vibrato, limited,
  "queeked", splattered: the output level flattens while harmonics stay below −45 dB); a −30 → 0 dBFS step shows the
  compressors' attack (settled in 10–20 ms) and release (≈ 100–200 ms back to the low-level gain, st*.wav);
- **wobble**: a 6 s steady 1 kHz tone, the echo's instantaneous frequency (cents rms, its main rate, how peaked the
  spectrum is, the left / right correlation) (wob.txt). E.g. vibrato 16.7 c rms at 5.5 Hz, multi-head tape 12.3 c at
  5.7 Hz (random-like), the BBD pedal 5.8 c at 0.85 Hz (sine), digital and studio tape 0;
- **spread**: the share of the impulse's energy within 6 ms of its peak (diffused 0.18, verbed 0.17, haze 0.45 …).

## 2. LYNX (src/core/EchoSuite.{h,cpp} `echoform::`, data src/core/EchoVoicings.inc)

Three parameters appended after round 144's `loopTreble` (Echoform: 105 parameters; defaults = the old sound):

| Parameter | Values | Law |
|---|---|---|
| Echo voicing | Off, then 31 voicings (Mastering reel, Studio reel, Cartridge echo, Multi-head tape, Valve tape, Worn cassette, Pedal BBD, Compact BBD, Oil drum, Magnetic drum, Phone line, Medium wave, Broadcast FM, Distant station, Walkie-talkie, Pristine digital, Warm analog, Digital / Analog / Pedal ensemble, Seasick, Hot glow, Thick, Battered, Squashed, Fuzzed, Honky, Haze, Smeared, Scattered, Roomy) | voicing k = EchoBoy's factory style k − 1 (`voicingForEchoBoyStyle`). Replaces the Mode menu's character: write path = saturation → loop tone, read path = output tone; the voicing's wobble and diffusion; every other control (styles, feedback, rhythm, cuts, loop EQ, ducking …) keeps working |
| Saturation type | Auto, Clean, Tape, Tube, Squash, Grit, Ceiling | Auto = the voicing's own curve (or the mode's shaper); the others = the measured curves of the digital, studio tape, valve tape, FM radio, distorted and limited styles, also without a voicing (in place of the mode's shaper) |
| Multiply | 1x, 2x, 4x, 8x | PrimalTap's law (§3) |

Models [model, fitted to the §1 data]:
- tones: RBJ biquads — output: gain, 0–2 high passes, 0–4 low passes, 3 bells; loop: a low shelf, a bell, 0–2 low
  passes (fittone.txt: output ≤ 0.3 dB rms for 24 of 31 styles, the worst 1.6 dB; loop ≤ 0.5 dB);
- shaper: y = G · (f (d·x + b) − f (b)) / (f′(b) · d), f (u) = u / (1 + |u|^p)^(1/p), p and b per voicing, d and G per
  Saturation step (0 / 6 / 12 / 18 / 24 dB), linear in dB in between (fitsat2.txt: gain within 0.0–1.5 dB rms);
- compressor (the dynamic styles): a peak detector (attack 4 ms, release 120 ms), a 6 dB soft knee, ratio per voicing,
  threshold and make-up per Saturation step (fitcomp.txt: ≤ 0.6 dB rms);
- wobble: a delay swing of √2 · c / (1731 · 2π f) s (sine) or a smooth random swing at the measured depth / rate,
  the right side in step, 90° or 180° from the left as measured;
- diffusion: Echoform's diffusion at (1 − the measured peak share) / 0.85 (at least the user's amount).

**Check, LYNX against the trial** (the same signals rendered through Echoform with each voicing, `cmp2.txt`): first-echo
tone 0.05–2.8 dB rms over 60 Hz – 14 kHz (median 0.7), per-pass loop tone median 0.6 dB (worst 3.9 dB, the oil-drum
voicing's top octave), steady-sine gain over −40 … 0 dBFS at Saturation 0 / 12 / 24 median 0.6 dB rms (worst 2.0),
wobble depth within 0.2 cents of the measured one (the random wobbles calibrated once against the trial: the smooth
random LFO's pitch / rms relation differs from the sine's), the 3rd harmonic only within ≈ 10 dB (median; the
shaper reproduces the level curve, not every harmonic). The loop tones are fitted with low-pass Q ≤ 0.9 and at most
+0.5 dB anywhere up to 22 kHz, so no voicing's loop can ring above the measured band.

The EchoBoy stand-in: her saved states keep round 136's fitted tape voicings (recognised by hash); any other EchoBoy
state now plays the Studio reel voicing (EchoBoy's default style). LYNX does not decode EchoBoy's encoded preset, so a
foreign state's style is not read (the mapping table `voicingForEchoBoyStyle` is ready for when it is known).

## 3. PrimalTap's Multiply [EXEC]

`Multiply` host parameter: 1x / 2x / 4x / 8x (4 steps). With Time A / B 100 / 500 / 1000 / 2000 ms (feedback 0, mix 100
%): the delay is exactly Time × factor (2x at 1000 ms = 1999.94 ms); the band falls with the factor — 1x −6.4 dB at
12 kHz / −55 dB at 16 kHz, 2x −2.2 dB at 6 kHz / −49 dB at 8 kHz, 4x −1.4 dB at 3 kHz / −45 dB at 4 kHz, 8x −0.9 dB at
1 kHz / −43 dB at 2 kHz (pt.txt) — a brick wall at ≈ 12.4 kHz / factor; image / alias products of a 1 kHz tone stay
50–56 dB down. 8 s total works (1000 ms × 8), 16 s (2000 ms × 8) gives silence. LYNX: the times × factor (up to the
line's 20 s) and two 8th-order Butterworth low-passes at 12.4 kHz / factor inside the loop. PrimalTap states are not
translated (no state data from her projects).

## 4. Tests

tests/Parity147aTests.inc: parameter block and texts; the voicings' tones against the measured first-echo tone (five
styles × 125 Hz / 1 kHz / 4 kHz); the saturation curves (studio tape at 24 dB, the FM and limited compressors) as
functions and through the engine; the vibrato voicing's wobble (cents rms of an echoed tone) and the digital one's
steadiness; Multiply's delays and band; a saturation type without a voicing; the EchoBoy stand-in's default voicing.

## Where I stopped

- Done: §2, §3, PARITY rows (Echo Style menu, Saturation, saturation type, Multiply) and the score note. Editor: Voicing / Sat. menus and the Mult. tabs in the Style edit row (`D:\lynx-work\parity147\a\ui\Echoform.png`). build-local with the FL Data dir: 1 645 927 checks, 0 failures; Windows build OK.
- Not done: the chorus / "fat" styles' several modulated voices (LYNX: one wobble per side; the measured tone and
  level match, the comb-like movement of two voices does not); EchoBoy's Style Edit details per voicing (per-repeat
  decay, Mid Res) and its own saturation-type menu (editor only, not a host parameter); the styles' behaviour in
  Dual / Ping-Pong (measured in Single only); PrimalTap's per-tap VCO and its 1x anti-alias band (Echoform stays full
  band at 1x, as before).
