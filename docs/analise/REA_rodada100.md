# Round 100 — Valhalla DSP reverbs, black-box, and LYNX's own reverbs fitted to them

Rules (as round 99, `MEASURE_fabfilter.md`): the OFFICIAL demo versions the user downloaded from the vendor's site
(VST3, `C:\Program Files\Common Files\VST3\Valhalla*.vst3`) were only hosted the way a DAW hosts them — VST3
parameter API (names, value ↔ text through set/get), state through `getStateInformation`, test signals in, audio
out — with `tools/fxmeasure`. No disassembly / decompilation / string dumps of the binaries, nothing bypassed, no
presets, graphics or manual text copied. Numbers are our own measurements [EXEC]; formulas are fitted models [fit];
anything not measured is "not measured" / "not confirmed". Raw captures stay in `tmp/valh100` (never in git).
The LYNX engines are our own code built from published reverb literature (Schroeder / Moorer all-pass and comb
structures, Jot's feedback delay networks, Dattorro's plate figure-of-eight tank, Gerzon / Stautner–Puckette
mixing matrices).

Plugins (VST3, 48 kHz): ValhallaVintageVerb 4.0.5, ValhallaRoom 2.0.5, ValhallaPlate 1.6.3, ValhallaSupermassive
5.0.0, ValhallaShimmer 1.2.2, ValhallaFutureVerb 1.0.2.

## 0. Harness changes

- `fxmeasure`: JUCE-based plugins that report their main buses disabled until the host asks for a layout were
  prepared with 0 channels (VintageVerb, Room, Plate, Shimmer). `prepare()` now asks for stereo in / stereo out
  (mono in / stereo out as fallback).
- These plugins answer `getText (value)` with the CURRENT value's text whatever value is asked, so the parameter
  laws below were read by `set` + reported text at 41 (enumerations: 241) points.

## Where I stopped

- Measuring (in progress).
