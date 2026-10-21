# Minor plugins of her corpus — combined short dossier

Round 125 (spec125). One page per lower-usage third-party plugin found in the girlfriend's FL Studio projects:
what it is, its controls in a line or two, what her saved state says (where readable), which LYNX module could play
its role, and the priority. Built from vendor pages / manuals, reviews, LYNX's source and her project data only; no
plugin binary was opened, decompiled or instrumented.
Tags: [DOC] vendor document / page · [MEAS] measurement · [DATA] her projects / LYNX files · [model] our
inference. All web sources accessed 2026-10-21.

Counts: "inventory" = `tmp/r113/inventory_all.txt` (412 projects, [round 113 §4b](../analise/REA_rodada113.md));
"full copy" = the same extraction over `tmp/gf/all` (907 .flp including autosaves and backups) [DATA].
None of these plugins has a LYNX substitute today (`src/core/PluginSubstitutes.cpp`), so their slots play as
pass-through / are left out, as FL does when a plugin is missing ([PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md)) [DATA].

## Priority overview

| Plugin | Inventory slots / states | Full copy slots / states | Kind | Proposed LYNX role | Priority |
|---|---|---|---|---|---|
| Cradle The God Particle | 10 / 3 | 10 / 3 | mix-bus chain (EQ, character, limiter) | Ridge Strip / Ember + Apex Limiter chain | medium |
| Black Box Analog Design HG-2 | 6 / 1 | 6 / 1 | tube saturation (pentode/triode + parallel sat + air) | Ember Saturator (tube, multiband/parallel) + Aether air | low-medium |
| Maag EQ4 | 6 / 1 | 6 / 1 | console EQ with Air Band | **flat in her state → pass-through is exact**; else Facet EQ + Aether | low |
| Harrison 32Classic channel strip | 1 / 1 | 1 / 1 (empty state) | console channel strip (32C EQ, filters) | Ridge Strip / Facet EQ (defaults) | very low |
| Sugar Bytes Effectrix2 | 1 / 1 | 22 / 5 | step-sequenced multi-effect | none (leave out) | very low |
| Excite Audio Bloom Drum Machine | 3 / 3 | 3 / 3 | instrument (drum sampler + sequencer) | not substituted (instrument) | n/a |
| Arturia Analog Lab V | 81 / 6 | 170 / 87 | instrument (preset player of Arturia's V Collection engines) | not substituted (instrument) | n/a |
| Antares Auto-Tune EFX+ / Auto-Key | 17 / 6 and 52 / 4 | — | pitch effect / key detector | see [auto-tune-pro.md](auto-tune-pro.md) (LYNX Tune) | (covered there) |

Instruments are never substituted: LYNX cannot invent the notes' timbre; their slots stay silent, exactly as FL
plays a project whose instrument is missing [model, LYNX policy in PLUGIN_FALLBACK.md].

---

## 1. Cradle — The God Particle

- **What it is.** A mix-bus processor built around Grammy-winning mixer Jaycen Joshua's bus chain; "the only
  plug-in" on his mix bus [S1][DOC]. Reviewers describe it as a mix of EQ, multiband compression, saturation,
  widening and limiting, with the internals undisclosed [S2][DOC].
- **Controls.** Input (with a target level marker), three EQ macros (Low / Mid / High) before the character
  section, Character amount 0–200 % (default 100 %), adaptive limiter, output [S3][S1][DOC].
- **Her states (readable text snapshot, "Cradle snapshot 0.4.5", plug-in 1.2.4.0) [DATA]:** factory preset
  "default"; `hostOnly.inputGain` = 0 or −3.84 (dB), `outputGain` = 0; `preset.signalChain.masterLimiter` bypass 0,
  `preGain` ≈ 5.0 (dB, preset constant); `oversampled.characterMacro.offset` = 0.5, `characterBypass` 0,
  `low/mid/highEQMacro.offset` = 0, `eqBypassMacro` 0. I.e. **default settings, sometimes with −3.84 dB input trim**
  — the plug-in's own default chain with +5 dB into the limiter.
- **LYNX role.** A fixed "bus chain" substitute: Ridge Strip (gentle EQ, neutral) → Ember (light character) →
  Apex Limiter (≈ +5 dB drive, ceiling −0.1/−1 dBTP) with the input trim from the state. Because the original
  is a loudness/limiter chain, the most audible part to match is the **limiter's gain** (+5 dB) — without it the
  substitute is ~5 dB quieter and less dense [model].
- **Priority:** medium (10 slots on mix buses; level matters). **Measure:** official trial at default with pink
  noise and a mix at −18/−10 LUFS: input→output level curve, spectral tilt, true-peak ceiling, latency.

## 2. Black Box Analog Design HG-2 (plug-in by Brainworx / Plugin Alliance; also a UAD version)

- **What it is.** Emulation of the HG-2 hardware tube saturator: main path input transformer → **6U8A pentode
  stage → triode stage** → passive output attenuation → output transformer; a **parallel saturation** path
  (12AX7, "Freq Select" 3-way) mixed in by the Saturation knob; Alt Tube (more aggressive parallel voicing); Air
  (harmonic lift from ≈ 10 kHz) [S4][S5][DOC].
- **Controls.** Pentode, Triode, Saturation (+ Freq select, Alt Tube), Air, Density, Calibration, Input, Output,
  Mix, Power, Bypass (names from her state's parameter codes `Pent Trde Satr SFrq AltT ArIn Airm Dens Cali InGn Outp
  OMix Powr Byps`; four values per parameter — likely four A/B/C/D snapshot slots) [DATA], [model].
- **Her state (1, 6 slots, `fuentes drogas antiquadas`) [DATA]:** normalised values (first slot): Pentode 0.6125,
  Triode 0.5, Saturation 0.549, Saturation-in 1, Freq select 0.5, Alt Tube 0, Air-in 1, Air 0.6125, Density 0.5,
  Calibration 0.5, Input 0.5, Output 0.5, Mix 1.0. The value→unit laws are unknown [model].
- **LYNX role.** Ember Saturator, tube style, with a parallel band (Ember's multiband mode) for the Saturation path
  and Aether's air shelf for Air. Fit drive per stage to the HG-2 trial's harmonic table.
- **Priority:** low-medium (one project). **Measure:** 1 kHz / 100 Hz sines at −18/−6 dBFS, harmonics vs Pentode,
  Triode, Saturation; Air shelf response; default calibration level.

## 3. Maag Audio EQ4 (Plugin Alliance)

- **What it is.** Emulation of the Maag EQ4 console EQ, famous for its **Air Band**: fixed bands Sub (10 Hz bell),
  40 Hz, 160 Hz, 650 Hz (bells), 2.5 kHz (shelf), plus an Air Band boost-only shelf selectable 2.5 / 5 / 10 / 20 /
  40 kHz up to +15 dB; all bands interact (sum), so Air also lifts overall gain slightly [S6][DOC]. The later EQ4-MS
  lists six Air frequencies [S6][DOC].
- **Her state (1, 6 slots, `fuentes drogas antiquadas`) [DATA]:** all band gains 0.5 (centre = 0 dB), Air gain 0,
  Air frequency 0, Level 1.0 (all four snapshot slots identical) → **flat / unity**. [model: 0.5 = 0 dB for
  boost-cut knobs; Air gain 0 = no boost.]
- **LYNX role.** For her corpus a **pass-through is already exact** (only the plug-in's own analog colour, if any,
  is lost — check §measure). General case: Facet EQ bells/shelf at the fixed frequencies + Aether air shelf.
- **Priority:** low. **Measure:** impulse at the default/flat state (any colour or noise?), Air Band curves at each
  frequency and +3/+6/+15 dB.

## 4. Harrison 32Classic Channel Strip

- **What it is.** Harrison's plug-in version of its 32C console channel: 4-band EQ with **proportional-Q** mids,
  shelving high/low (switchable to bell), ±15 dB (±18 in shelf mode per the manual), bands HI ≈ 900 Hz–13 kHz,
  HI-MID 400 Hz–6 kHz, LO-MID 200 Hz–3.1 kHz, LO 40–600 Hz; 12 dB/oct HPF ≈ 20 Hz–3.1 kHz and LPF ≈ 160 Hz–20 kHz
  (Mixbus 32C manual describing the same 32C EQ) [S7][DOC].
- **Her state:** the only slot saves an **empty** state (0 bytes) → plug-in defaults [DATA].
- **LYNX role.** Ridge Strip or Facet EQ at neutral settings (equivalent to the default-flat strip) [model].
- **Priority:** very low (1 slot).

## 5. Sugar Bytes Effectrix2

- **What it is.** A 32-step, host-synced multi-effect sequencer: Looper A/B, Grain, Ring modulator, Vinyl,
  Spectrum, Tonalizer, Crush, Filter, Phaser, Modulation (chorus/flanger), Delay, Reverb, Level; per-effect dry/wet
  and modulation sequencers; up to 12 patterns; reorderable signal path [S8][S9][DOC].
- **Her states:** binary (not readable as plain text), 5 distinct states in the full copy [DATA].
- **LYNX role.** None realistic without the pattern data; leave out (as now) [model].
- **Priority:** very low.

## 6. Excite Audio — Bloom Drum Machine (instrument)

- Drum sampler with a sequencer and three built-in effects (reverb, dynamics, bitcrush), released Nov 2024; VST/VST3/
  AU/AAX [S10][DOC]. Her 3 states are large (≈ 0.5 MB) JSON-like blobs naming `Bitcrusher`, `Reverb`, `Dynamic`
  processors, musical key / scale and per-key sample IDs [DATA].
- **Not substituted** (instrument; its samples are the sound). Priority n/a.

## 7. Arturia Analog Lab V (instrument)

- Arturia's preset-player front end for the engines of its V Collection (vintage synths, keys) [model; Arturia
  product line]. 81 slots / 6 states (inventory), 170 / 87 (full copy) [DATA].
- **Not substituted** (instrument). Priority n/a — but it is the most-used third-party instrument in her corpus, so
  a missing-instrument notice in LYNX's open report is useful [model].

## 8. Antares Auto-Tune EFX+ and Auto-Key

Covered by the Auto-Tune dossier: [auto-tune-pro.md](auto-tune-pro.md). PLUGIN_FALLBACK maps Auto-Tune EFX+ by name
to LYNX Tune (`names = … Auto-Tune EFX+ …` in `[antares-auto-tune]`) [DATA]. Auto-Key is a key-detection utility
(no audio processing role to substitute) [model].

## Implementation guidance (summary)

1. **God Particle:** add a `[cradle-god-particle]` entry → a LYNX chain (Ridge Strip neutral → Ember light →
   Apex Limiter); translate `hostOnly.inputGain` / `outputGain` and the limiter `preGain` from the readable snapshot;
   calibrate the limiter drive/ceiling against the trial (loudness within 0.5 LU, octave-band ≤ 1 dB).
2. **Maag EQ4:** register a `[maag-eq4]` entry that reads the BXCk chunk; when every gain is 0.5 and AirG is 0,
   insert nothing (exact); otherwise Facet + Aether.
3. **HG-2:** `[bx-hg2]` → Ember tube with parallel band + Aether air; translate after measuring the laws.
4. **Harrison 32C, Effectrix2, Bloom, Analog Lab V:** no action beyond listing them in the missing-plugin report.

## Sources

1. MusicTech, "The God Particle is 'the only plug-in' on … Jaycen Joshua's mix bus" — https://musictech.com/news/gear/the-cradle-jaycen-joshua-the-god-particle/ — accessed 2026-10-21 — [DOC]
2. MusicTech, "Cradle The God Particle review" — https://www.musictech.com/reviews/plug-ins/cradle-the-god-particle-review-a-grammy-winning-mix-bus-chain-in-a-simple-package/ — accessed 2026-10-21 — [DOC]
3. Cradle support, "GUI Overview" (The God Particle) — https://support.cradle.app/hc/en-us/articles/4411750433044-GUI-Overview — accessed 2026-10-21 — [DOC]
4. Universal Audio, *Black Box Analog Design HG-2 Manual* — https://media.uaudio.com/support/manuals/dd/Black+Box+Analog+Design+HG-2+Manual.pdf — accessed 2026-10-21 (search excerpts) — [DOC]
5. Black Box Analog Design, HG-2 product page — https://blackboxanalog.com/hg-2/ — accessed 2026-10-21 — [DOC]
6. Mix magazine, Maag Audio EQ4 plug-in announcement (band list, Air Band 2.5–40 kHz, +15 dB) — https://www.mixonline.com/?p=52587 ; Thomann EQ4-MS listing — https://www.thomann.co.uk/plugin_alliance_maeag_audio_eq4_ms.htm — accessed 2026-10-21 — [DOC]
7. Harrison, *Mixbus 32C manual — EQs* (32C EQ and filters) — https://rsrc.harrisonconsoles.com/mixbus/mixbus32c-live-manual/9/en/topic/what-s-different-about-mixbus-32c , https://rsrc.harrisonconsoles.com/mixbus/mixbus32c-live-manual/8/en/topic/eqs — accessed 2026-10-21 — [DOC]
8. MusicRadar, "Sugar Bytes Effectrix2" — https://www.musicradar.com/news/sugar-bytes-effectrix2 — accessed 2026-10-21 — [DOC]
9. Dubspot plugin page, Effectrix 2 (specs) — https://blog.dubspot.com/plugins/effectrix-2 — accessed 2026-10-21 — [DOC]
10. KVR Audio, "Excite Audio releases Bloom Drum Machine" — https://www.kvraudio.com/news/excite-audio-releases-bloom-drum-machine-vst-au-aax-62353 — accessed 2026-10-21 — [DOC]
11. LYNX data: her states extracted from `tmp/gf/all` (BXCk chunks of HG-2 / Maag EQ4, Cradle text snapshots, empty Harrison state); `src/core/PluginSubstitutes.cpp`; [PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md); [round 113](../analise/REA_rodada113.md) — [DATA]
