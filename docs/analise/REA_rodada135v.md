# REA round 135v — Valhalla roles and the shared description line (sub-round of parity135)

Branch `feature/parity135v` (merged into `feature/parity135` by the parent). English-first; black-box only: the
Valhalla trials (ValhallaShimmer 1.2.2, ValhallaFutureVerb 1.0.2) hosted in our own host (fxmeasure / uiview, public
VST3 API, editor snapshots as a user sees them), nothing decompiled, no graphics / presets / names copied.

## 1. The description line of every LYNX engine [EXEC + MANUAL]
- `core/ParamHelp.{h,cpp}` (no JUCE, tested): one sentence per control in LYNX's own words. Lookup: the engine's own
  wording first (e.g. Echoform's "Ratio" is an echo ratio, Crest's a compression ratio), then the shared wording,
  then the name without a band / voice / side / section prefix or suffix ("B2 ratio", "V1 pan", "Time L", "Early
  size", "Comp. threshold"). Covers every control of the Valhalla-role engines and every control the other LYNX
  engines had in this round (Ridge, Glow, Hush, Aether, Ember, Crest, Prism, Sentry, Quell, Gemini, Compressor, Facet,
  Apex, LYNX Tune, Echoform): test `description_line_covers_every_control_round135`.
- `summary()`: while nothing is hovered the strip names the engine's mode / type / style and says what it is, from
  the words of the mode name ("Smooth Plate" → "a plate: dense from the first moment…, smoother, with no grain"),
  per family (reverbs, Echoform's machines, Orbit's modulations, Apex's styles, Ember's styles) or the engine's own
  line (Nebula, Crest, Sentry). [EXEC] FutureVerb's own strip does the same ("Modern: a clean predelay").
- Drawn in both editor families: the generic panels (PluginEditors.cpp, now also LYNX Tune's panel) and the
  display-first editors (ProEditorBase.h: an 18 px strip under the bottom bar; the editors grow by 18 px, every
  area is derived from the component's height so nothing else moved). pt-BR for all ~330 texts.

## 2. Mix lock on the MIX caption
A click on the MIX caption of a LYNX engine's knob or fader toggles round 129's per-engine mix lock; the caption is
boxed in the accent colour while locked; hint "MIX label: click to keep the Mix while presets change".

## 3. The original's preset name [DATA]
`originalPresetName()`: Valhalla's `presetName` attribute, Waves' `<Preset Name="…">`; `makeSubstituteSlot` stores it in
`EffectSlot::presetName` for LYNX engines, so the header shows it instead of "(no preset)". Her corpus: Valhalla
"Detuned & Big" 742 slots, "Big Shimmerspace Huge" 241, "Default" 156, "PITCH-Amethyst" 45 …; Waves "Start Me Up" 739,
"Echo" 738, empty 3 768.

## 4. Halo ← ValhallaShimmer: the four reverb modes [EXEC]
Left-only impulse through the trial at reverbMode 0 … 1: below 0.25 the input is summed (correlation 0.2, equal L / R
energy); from 0.25 up the left input stays on the left (R 86 dB below L); the decay changes at 0.75 (steps assumed at
0.25 / 0.5 / 0.75, the VST3 four-step rule). Halo: Mono (summed input), Small / Medium / Big stereo (the two sides kept
apart: `crossFeed = 0`; sizes 0.75 / 1.0 / 1.3, decay × 0.85 / 1 / 1.15 — LYNX's guess, vox136 may refit) on values
0.125 / 0.25 / 0.375 / 0.625; LYNX's Wide / Dense / Bright keep 0 / ½ / 1 (older projects unchanged, default Wide).
Translation: `steps 0:0.125 0.25:0.25 0.5:0.375 0.75:0.625`. Pitch mode caption and Single / Dual names.

## 5. Horizon ← ValhallaFutureVerb: mode-dependent controls [EXEC]
The trial's editor captured in every echo and reverb mode (settled value, two snapshots per mode because its
editor redraws one parameter change late): Drive in Modern / Tape / Digital / Analog (LoFi assumed), Detune in the
same place in Detune, Reverse, ReverseOctUp / Down / UpDown, Sparkle, Swarm; Attack replaces Early/Late in Space and
Frozen (Nonlin not captured). Layout: `when` conditions on Echo mode / Reverb mode.

## 6. Lattice Mod ← ValhallaUberMod: the MODE menu
Nine algorithms under LYNX's own names on the reference's type index (`lattice::tapCount`), caption "Mode". The engine
still plays the three 6-tap and the two 16-tap modes alike (engine work).

## 7. PARITY.md
Shared-frame rows (preset name ✅, description line ✅, mix lock), Epoch / Atrium / Foil / Nebula / Halo / Horizon /
Orbit / Lattice tables and scores ("After round 135"). Stale round-129 rows (Atrium tabs / Depth, Foil EQ defaults,
Nebula Clear, Halo low cut) corrected. ❌ in the file: 149 → 125.

## Where I stopped
- Done: §1–§7, tests/Parity135vTests.inc.
- Not done (left): Echoform / ValhallaDelay rows (sub-round w owns them; the description line, presets and mix lock
  apply there too — update those three rows when merging); Halo programs and reverse-grain pitch modes; Lattice per-mode
  LFO designs, InputPan, Sync translation; Horizon colours / loop EQ; Epoch 1970s / 1980s colour; Atrium fader
  tapers; FutureVerb Nonlin's controls not captured; Facet EQ's own editor has no description strip (Pro-Q has none).
