# Soundtoys PanMan — spec dossier (LYNX substitute: Auto-pan)

Round 125 (spec125). Clean-room dossier built from the vendor's user guides (PanMan v5 guide, read through a public
mirror; the PanMan blurb in the EchoBoy guide), LYNX's own source and her saved project data. No Soundtoys binary was
opened, decompiled or instrumented; the encoded preset payload was **not** decoded.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [DATA] read from her projects / LYNX files ·
[model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** PanMan is a "rhythmic autopanning" effect [S1][DOC] with six modulation sources — LFO
(default), Rhythm Step, Rhythm Shape, Random, and two audio-triggered modes (PingPong, Random Step) [S1 p.7][DOC] —
an Offset / Width pair in degrees (±105°, so it can pan "beyond the speakers") [S1 p.5][DOC], a Smoothing control, an
envelope-follower "Tweak" panel that modulates rate and width from the input level [S1 p.9–13][DOC], and Soundtoys'
input/output stage with an **Analog / Digital switch that adds saturation at all levels in Analog mode** [S1 p.6][DOC].
It was "inspired by the legendary PanScan hardware processor" [S2 p.63][DOC]. Her projects hold it in 60 slots /
5 states (412-project inventory) — and in 385 slots / 50 distinct states across the full 907-file copy including
autosaves [DATA]. The state is a Soundtoys encoded preset, so LYNX cannot read it [DATA]. **Important finding:** the
current substitute preset `Depth=0.5` maps to an Auto-pan depth of **zero** (`depth = 2·p − 1`), so today the
substitute is a pass-through, not "a moderate sweep" as the table comment says [DATA, src/core/Effects.cpp].

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Soundtoys PanMan (Soundtoys 5 bundle; state says `SAVED_BY_VERSION = 5.0.1`, `VERSION = 4`) | [S1][DOC], [DATA] |
| Format in her projects | VST2, unique ID `StPn` | [S3][DATA] |
| Lineage | "Inspired by the legendary PanScan hardware processor" (EchoBoy-era product blurb); the v5 guide says the designers studied classic analog autopanners — the PanScan, the Cyclosonic FS-1 and the Spanner | [S2 p.63][DOC], [S1 p.4][DOC] |
| What it is | auto-panner with tempo-synced rhythm modes, envelope-driven modulation and audio-triggered pan jumps | [S1][DOC] |

**Her usage [DATA].** Inventory of 412 projects ([round 113 §4b](../analise/REA_rodada113.md), `tmp/r113/inventory_all.txt`):
60 slots in 60 projects, 5 distinct states. Re-running the same extraction over the full copy
(`tmp/gf/all`, 907 .flp incl. autosaves/backups): 385 slots, 50 distinct states; the most used states sit in
`MUSICA Q MAIS GOSTO` (52 slots), `twin swag` (50), `gente morta` (29 + 28), `game over` (25). Payload format:
`WIDGET = PanMan; VERSION = 4; SAVED_BY_VERSION = 5.0.1; PRESET =<ASCII-armoured encoded blob>` ([round 98](../analise/REA_rodada98.md)).
The blob is not a plain parameter list; we did not try to decode it (clean-room rule: settings are learned only from
saved data that is readable or from black-box runs of the official plugin).

## 2. Signal flow

```
in L/R ─► Input level ─► [Analog: saturating stage at all levels | Digital: clean until clip]  (S1 p.6)
        ─► pan stage: position θ(t) = Offset + Width·m(t), θ ∈ [−105°, +105°]  (±90° = hard L/R)
        ─► Output level ─► out L/R
m(t) sources: LFO (Rate Hz | synced) · Rhythm Step (3/4/5 steps) · Rhythm Shape (synced LFO shape, editor) ·
              Random (per rhythmic step) · PingPong (trigger) · Random Step (trigger)
side paths:  input ─► [trigger filter HPF/LPF/off, gain] ─► level detector ─► trigger (threshold, divider 1–12)
             input ─► envelope (Threshold, Attack 0–5000 ms, Release 0–5000 ms, Env/Gate) ─► Rate Mod, Width Mod
```
Whether the pan stage is a balance (attenuate one side) or a true mono-sum pan, its pan law, and how a stereo input is
handled are **not documented** [model; open question §8].

## 3. Controls

| Control | Range | Unit | Default | Law / behaviour | Source |
|---|---|---|---|---|---|
| Offset | −105 … +105 | degrees | 0 (centre) | ±90° = fully one channel; beyond = "past the speaker" | [S1 p.5][DOC] |
| Width | 0 … 210 total (±105) | degrees | 180 | modulation span around Offset; total capped at ±105° | [S1 p.5][DOC] |
| Smoothing | Hard … Soft | — | n/d | how gradually the pan moves between positions; with L→R / R→L shapes sets the snap-back | [S1 p.5][DOC] |
| Input / Output level | n/d | dB | "roughly unity" | yellow LED = 6 dB below clipping | [S1 p.5][DOC] |
| Analog switch (+ flavour button) | Analog / Digital | — | n/d | Analog: saturation at all levels (more CPU); Digital: clean, harsh clip when hot | [S1 p.6][DOC] |
| Modulation source | LFO, Rhythm Step, Rhythm Shape, PingPong, Random, Random Step | — | LFO | changes the front-panel controls | [S1 p.7][DOC] |
| Rate (LFO) | 0.1 … 10 | Hz | n/d | free-running LFO | [S1 p.8][DOC] |
| Direction | L→R, Back-and-Forth, R→L | — | n/d | ramp up / triangle / ramp down | [S1 p.8, p.19][DOC] |
| Steps (Rhythm Step) | 3, 4, 5 | — | n/d | positions spread evenly over ±90°·(Width/180) about Offset | [S1 p.14–15][DOC] |
| Rhythm | note values, "Edit" (custom) | — | n/d | interval between steps; synced to host | [S1 p.15][DOC] |
| Feel | Rushin' … Draggin' | — | centre | shifts the whole pattern earlier / later; points wrap around | [S1 p.15–16][DOC] |
| Groove (Rhythm Shape) | shuffle … swing | — | centre | swing applied to the pattern | [S1 p.19][DOC] |
| Smoothing Mode (editor) | Linear, Sine, Exp, Sym, (5th not in excerpt), Rev | — | — | transition shape between points | [S1 p.17–18][DOC] |
| Tweak: Threshold | n/d | dB | n/d | envelope threshold; depth ∝ level above threshold (Env) | [S1 p.9–10][DOC] |
| Tweak: Rate Mod | bipolar | octaves | 0 | 1.00 = ×2 rate, 2.00 = ×4 (the guide's "3.00 = ×9" is a typo for ×8) | [S1 p.10][DOC] |
| Tweak: Width Mod | bipolar | degrees | 0 | adds/subtracts width, total capped at ±105° | [S1 p.11][DOC] |
| Tweak: Attack / Release | 0 … 5000 | ms | n/d | envelope smoothing of the modulation | [S1 p.11–12][DOC] |
| Tweak: Env / Gate | Env, Gate | — | Env | Gate = modulation jumps to its maximum while above threshold | [S1 p.13][DOC] |
| Trigger threshold + Manual Trigger | n/d | dB | n/d | fully up disables audio triggering | [S1 p.24][DOC] |
| Trigger filter | HPF / LPF / off, cutoff, gain, Monitor | — | n/d | filters the detector only | [S1 p.24][DOC] |
| Trigger Divider | 1 … 12 | triggers | n/d | transitions every N triggers | [S1 p.25][DOC] |

State mapping: **none known** (encoded preset). No RealWorld-style list exists for Soundtoys [DATA].

## 4. Known curves & behaviour

- 3 steps: −90, 0, +90°; 4 steps: −90, −30, +30, +90°; 5 steps: evenly spaced over ±90° (the excerpt lists
  "−90, 45, 0, 45, 90", clearly −90, −45, 0, +45, +90) at Offset 0 / Width 180 [S1 p.15][DOC].
- PingPong defaults to hard left ↔ hard right; Offset / Width move the two positions [S1 p.23][DOC].
- Rate modulation pushes a host-synced LFO out of sync while active and re-locks when it returns to base [S1 p.10][DOC].
- Not documented: pan law (−3 / −4.5 / −6 dB centre), behaviour beyond ±90° (polarity-inverted crossfeed is the usual
  way to place a source "outside the speakers"), LFO phase reference to the bar, stereo-input handling, latency,
  saturation curve and level of the Analog stage [model].

## 5. Published science

- **Pan laws.** A source at azimuth θ_p mapped to gains g_L = cos(φ), g_R = sin(φ), φ = (θ_p + 90°)/180°·π/2 gives a
  constant-power (−3 dB centre) pan; the linear law g_L = 1 − x, g_R = x gives −6 dB at centre [S4][PAPER]. The
  stereophonic "tangent law" relates the gain ratio to perceived angle:
  tan θ_img / tan θ_0 = (g_L − g_R)/(g_L + g_R), θ_0 = speaker half-angle [S4][PAPER]. PanMan's ±105° range implies a
  mapping that continues past ±90° — with the tangent law this requires g_L·g_R < 0 (out-of-phase crossfeed) [model].
- **Envelope follower** for Rate/Width Mod: one-pole attack/release smoother on |x| or x² (the classic
  "branching" detector of Giannoulis et al.) [S5][PAPER]:
  `e[n] = α_A e[n−1] + (1−α_A)|x[n]|` if |x[n]| > e[n−1], else `α_R e[n−1] + (1−α_R)|x[n]|`, α = exp(−1/(τ f_s)).
- **Saturating input stage** (Analog mode): any memoryless soft clipper with anti-aliasing (see the Decapitator
  dossier [decapitator.md](decapitator.md) §5 for ADAA) [model].

## 6. What LYNX does today

- `[soundtoys-panman]` in `src/core/PluginSubstitutes.cpp`: `lynx = Auto-pan`, `vst2 = StPn`, `names = PanMan`,
  `preset = Depth=0.5`; table comment: "its state is encoded: a moderate sweep (round 113, was 75 %)" [DATA].
- Auto-pan (`class AutoPan`, `src/core/Effects.cpp`) parameters: Period (`p0·64` beats, default 1/64 → 1 beat),
  Depth (`depth = 2·p1 − 1`), Phase, Pan (`fixed = 2·p3 − 1`), Control only, FL pan law (default off), Tension; a
  triangle LFO locked to the song position; without the FL law it is a **linear balance**
  (`L·=(1−pan)` for pan > 0, `R·=(1+pan)` for pan < 0) [DATA].
- Consequence: `Depth=0.5` → depth 0 → `pan = fixed = 0` → gain 1 on both sides: **the substitute is an exact
  pass-through** [model from the code; one render with LYNX_PARAM would confirm]. The round-113 table entry
  ("was 75 %") therefore moved the substitute from depth +0.5 (±50 % balance swing at 1 beat) to no movement at all.
  Since we cannot read her PanMan settings, "no movement" may actually be the safer default; but it should be a
  deliberate choice and the comment fixed.
- No measured residual: Soundtoys trials were never installed ([round 113 "Next"](../analise/REA_rodada113.md),
  [round 114](../analise/REA_rodada114.md)).

## 7. Implementation guidance for LYNX

Priority: **medium-low** for sound identity (a panner on a vocal/FX bus; level-neutral on average) but **high for
correctness of intent**: a pass-through silently drops an audible effect in 385 slots.

1. **Decide the fallback explicitly.** Keep the pass-through (Depth 0.5) *or* pick the PanMan factory default
   (LFO, Back-and-Forth, Offset 0, Width 180°) if the default rate becomes known. Fix the comment in
   `PluginSubstitutes.cpp` and PLUGIN_FALLBACK.md either way.
2. **Make Auto-pan able to play PanMan's role** (if states become readable through §8 measurements):
   - add a degree-based mapping: θ = Offset + Width/2·w(t), clamp ±105°; convert to gains with a constant-power law
     for |θ| ≤ 90° and a small polarity-inverted crossfeed beyond (calibrate against §8 item 2);
   - shapes: triangle (Back-and-Forth), ramp up/down (L→R, R→L), N-step sample-and-hold with Smoothing as a one-pole
     glide (time constant from §8), random per step;
   - sync: note values via the existing song-position phase; Feel = phase offset in fractions of a step;
   - optional envelope Rate/Width modulation (Tweak) and audio-triggered PingPong / Random Step with a 1–12 divider.
3. **Stereo input:** follow the measured behaviour (§8 item 3): balance (keep both channels) vs mono-sum + pan.
4. **Analog mode:** if §8 shows saturation at nominal levels, insert Ember's tube curve with the measured drive;
   otherwise skip.
5. **Pass criteria:** on a 1 kHz sine and on her vocal, per-channel gain envelope within ±0.5 dB of the original
   over two LFO periods, LFO phase within ±5° of the bar, and level-normalised octave-band error ≤ 0.5 dB.

## 8. Open questions to measure (official trial, black-box only)

1. **Encoded state → controls.** Set each control on the trial to 3–5 values, save the state each time, and
   correlate blob changes with settings (the same probe method as Waves RealWorld in round 113 §3c, but only if the
   saved blob turns out to be a reversible, non-cryptographic encoding of plain values; if it is encrypted or
   obfuscated, stop and rely on render-matching instead).
2. **Pan law.** Mono 1 kHz sine, Width 0, Offset −105…+105° in 15° steps: L/R gains and polarity → law, centre
   attenuation, out-of-speaker crossfeed.
3. **Stereo handling.** L-only and R-only inputs at Offset ±45°: does R leak into L (pan) or only attenuate (balance)?
4. **LFO.** Rate 0.1…10 Hz, Back-and-Forth / L→R / R→L: waveform shape from the gain envelope; synced rates' phase
   relative to bar 1 at 120 BPM.
5. **Smoothing.** Rhythm Step 1/4 note, Smoothing Hard/mid/Soft: glide time constant.
6. **Analog vs Digital.** −18 / −6 / 0 dBFS 1 kHz sine at default levels: THD per mode; is Analog the default?
7. **Defaults.** Fresh instance: every control's default value (Rate, Rhythm, Smoothing, Input/Output, Analog).
8. **Latency** (impulse) in each mode.

## 9. Sources

1. Soundtoys, *PanMan User's Guide, Version 5* — official PDF https://www.soundtoys.com/wp-content/uploads/PanMan-Manual.pdf (vendor site refused automated fetch; read page by page through the public mirror https://www.manualsdir.com/manuals/742027/soundtoys-panman.html , pp. 2–25) — accessed 2026-10-21 — [DOC]
2. SoundToys, *EchoBoy — The Ultimate Echo Machine* (Pro Tools-era user guide), product-line page 63 ("PanMan … Inspired by the legendary PanScan hardware processor") — https://barryrudolph.com/recall/manuals/echoboy.pdf — accessed 2026-10-21 — [DOC]
3. LYNX round 98 / 113 notes on Soundtoys identifiers and encoded presets — [../analise/REA_rodada98.md](../analise/REA_rodada98.md), [../analise/REA_rodada113.md](../analise/REA_rodada113.md) — [DATA]
4. V. Pulkki, "Virtual Sound Source Positioning Using Vector Base Amplitude Panning", *J. Audio Eng. Soc.* 45(6), 1997 (tangent law, amplitude-panning gain laws) — https://www.aes.org/e-lib/browse.cfm?elib=7853 — accessed 2026-10-21 — [PAPER]
5. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", *J. Audio Eng. Soc.* 60(6), 2012 (level detectors) — https://www.aes.org/e-lib/browse.cfm?elib=16354 — accessed 2026-10-21 — [PAPER]
6. LYNX source: `src/core/PluginSubstitutes.cpp` (`[soundtoys-panman]`), `src/core/Effects.cpp` (`class AutoPan`, `autoPanInfo`) — [DATA]
