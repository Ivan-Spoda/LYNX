# Parity audit: LYNX's "inspired by" plugins against the originals

Rounds 128–129 (2026-10-21). The user's question: "people tell me details are missing in the plugins — is it
configuration or design?" Round 114 scored only the layout. This audit checks **everything**, item by item:

- **Configuration**: every control, mode, menu option, switch, range / unit / default, per-band option, context
  menu, mouse gesture, side chain, oversampling / quality, latency, A/B, undo / redo, presets, MIDI learn, analyser
  options, meters, window size, help.
- **Design**: what is shown where (display contents, readouts, labels, sections, proportions), drawn in LYNX's own
  style (skin122 / skin126 own the look; this audit owns the *what* and *where*).

Sources: each original's official trial installed on this PC, hosted black-box in our own host (parameter lists read
through the public VST3 API — every control, range and option text — plus screenshots of each view as a user sees
it), the official manuals, and `docs/specs/*.md`. Nothing was decompiled; no graphics, presets or names were copied.
Reference screenshots stay outside git (`D:\lynx-work\parity128\ref`); the side-by-side pages are on the Desktop
(`LYNX-paridade`).

Legend: ✅ same · ⚠️ partial (note says what differs) · ❌ missing · — not applicable / deliberately left out.
Scores are 0–10 (10 = a user of the original finds every control and every piece of information where they expect).

Round 128 covers the FabFilter / Slate / Antares roles (below). Round 129 covers the Waves / SSL / Valhalla roles
(its own section).

---

# Waves / SSL / Valhalla / Soundtoys roles (round 129)

Trials: Waves (WaveShell 17.1, demo mode) and Valhalla DSP (demos) hosted here; the reference screenshots are from
rounds 100 / 101 / 113 / 119 and this round (outside git: `D:\lynx-work\parity129\ref`, `D:\lynx-test-archive`).
The Soundtoys 5 trials were installed during this round but every one of them stops on its iLok activation window
until the user starts the trial, so their tables are from the official manuals ("manual only"). Black-box only.

| LYNX engine | Original (trial here) | Config before | Design before | Config after | Design after |
|---|---|---|---|---|---|
| Glow Leveler | CLA-2A (Waves) | 5 | 6 | … | … |
| Ridge Strip | SSL E-Channel (Waves) | 6 | 6 | … | … |
| Compressor (stand-in) | Renaissance Compressor (Waves) | 2 | 1 | … | … |
| Hush De-esser | DeEsser (Waves) | 7 | 5 | … | … |
| Quell Denoiser | NS1 (Waves) | 8 | 8 | … | … |
| Gemini Doubler | Doubler2 / Doubler4 (Waves) | 7 / 4 | 6 | … | … |
| Echoform | H-Delay (Waves) | 5 | 4 | … | … |
| LYNX Tune | Waves Tune Real-Time | 4 | 5 | … | … |
| Epoch Verb | ValhallaVintageVerb | 7 | 8 | … | … |
| Atrium | ValhallaRoom | 7 | 6 | … | … |
| Foil Plate | ValhallaPlate | 8 | 9 | … | … |
| Nebula | Valhalla Supermassive | 7 | 8 | … | … |
| Halo | ValhallaShimmer | 6 | 8 | … | … |
| Horizon | ValhallaFutureVerb | 6 | 7 | … | … |
| Echoform | ValhallaDelay | 6 | 6 | … | … |
| Spiral Echo | ValhallaFreqEcho | 9 | 9 | … | … |
| Orbit | ValhallaSpaceModulator | 7 | 7 | … | … |
| Lattice Mod | ValhallaUberMod | 6 | 7 | … | … |
| Ember Saturator | Decapitator (Soundtoys, manual only) | 4 | 4 | … | … |
| Fruity Delay 3 (stand-in) | EchoBoy / PrimalTap (Soundtoys, manual only) | 2 | 2 | … | … |
| Auto-pan (stand-in) | PanMan (Soundtoys, manual only) | 2 | 2 | … | … |
| Reeverb 2 (stand-in) | Little Plate (Soundtoys, manual only) | 4 | 3 | … | … |
| — | Little AlterBoy (Soundtoys, manual only) | 0 | 0 | … | … |

## The frame shared by every original (Waves bar, Valhalla preset row, Soundtoys control strip)

| Original feature | Where in the originals | LYNX before | Note |
|---|---|---|---|
| Undo / Redo of the plugin's own edits | Waves bar (every Waves plugin) | ⚠️ | only the song's Ctrl+Z in the main window |
| A/B compare (Setup A / B) + copy A→B | Waves bar; Soundtoys strip | ❌ | |
| Preset name, ◀ ▶, menu of factory presets | Waves bar; Valhalla "PRESET: Default ◀ ▶" row; Soundtoys strip | ❌ | the generic panels listed FL Studio presets only: "(no preset)" for every LYNX engine |
| Save / load the user's own presets | Waves Save / Load; Valhalla preset menu (save, copy); Soundtoys | ❌ | |
| Name of the preset a project was saved with | Waves / Valhalla show it | ❌ | translated substitutes showed "(no preset)" |
| Per-control description line inside the window | Valhalla (bottom strip) | ⚠️ | LYNX shows the hint in the main window's hint bar |
| Mix lock (Mix stays while presets change) | Valhalla (click the MIX label) | ❌ | |
| Typed values, reset, fine drag, wheel | all | ✅ | right-click value entry, double-click / Alt-click reset, Ctrl fine, wheel |
| MIDI learn | Waves (right-click) | ✅ | right-click → automation / controller link |
| Resizable window | Waves ≡ menu, Valhalla corner | ✅ | the generic panels resize at a fixed aspect ratio |

## Glow Leveler ← CLA-2A (Waves)
Sources:
- `docs/specs/cla-2a.md` (§1, §3, §6);
- trial screenshot `/mnt/c/Users/kauan/lynx-test/w119/ref/CLA-2A_Stereo.png`;
- LYNX screenshot `lynx-before/Glow Leveler.png`;
- `glowLeveler()` in PluginLayout.cpp, `glow::Leveler` and `paramText` in MixSuite.cpp, the `vumeter` display in
  PluginEditors.cpp.

Trial on this PC: yes (the new parity129 capture is missing; the w119 capture was used).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Header: Undo / Redo | WaveSystem bar, top left | song-level Ctrl+Z only | ❌ | |
| Header: Setup A/B + A→B copy | bar ("A→B", "Setup A") | none | ❌ | |
| Header: preset name + Load menu | bar ("A: Start Me Up"), factory presets by CLA | presets box lists FL presets only, "(no preset)" | ❌ | no factory or user presets for Glow |
| Header: Save | bar | none | ❌ | |
| Header: ≡ menu (help, resize) | bar, right | options menu (Presets, Reset, Bypass); window resizable | ⚠️ | |
| Bypass | VST param | slot LED / options menu | ✅ | |
| GAIN knob | 0–100 scale, default **40** (≈ +8 dB below threshold), left of the VU; nonlinear law (20 → −18.5 dB, 40 → +8, 80 → +31.8) | "Gain" 0…+30 dB linear, default **+0.0 dB**, big knob on the left | ⚠️ | right place; different unit (dB, not 0–100), different range (no attenuation below unity), different default (CLA 40 ≈ +8 dB vs 0 dB) |
| PEAK REDUCTION knob | 0–100, default **46**, right of the VU, nonlinear | "Peak reduction" 0–100 (side chain −20…+40 dB linear), default **0** = off, big knob on the right | ⚠️ | right place and right scale; the default does no levelling, and the taper differs (the translator uses 0.40 + 0.021·(knob − 40)) |
| COMPRESS / LIMIT switch | toggle, left side under the logo; default Compress | "Compress | Limit" tabs under the VU | ✅ | different place (centre, not left) but easy to find; Limit γ = 9 (~10:1) vs about 100:1 on the hardware |
| VU meter | analog VU in the centre; −20…+3 scale, red above 0 | analog VU in the centre, same −20…+3 scale | ✅ | good match |
| VU DISPLAY selector | rotary knob INPUT / **GR** (default) / OUTPUT, top right | hidden: clicking the meter switches GR ↔ Output; no Input | ⚠️ | no visible selector, and Input is missing; add a 3-position switch beside the VU |
| VU readout text | none (needle only) | "Gain reduction −x.x dB" or "Output x VU" inside the face | ➕ | |
| VU calibration | 0 VU = −18 dBFS (spec, unverified) | output mode 0 VU = −18 dBFS | ✅ | |
| HI FREQ (side-chain HF emphasis) | knob 0–100, default **50** (≈ +5–6 dB detector sensitivity at 6 kHz), with a "FLAT" mark; bottom strip | "Emphasis" 0–100 % (+0…+10 dB shelf at 2.5 kHz in the detector), default **0 %**, small knob under the switch | ⚠️ | the function exists; different default (CLA's default is not flat), name and law; not translated (fixed to 0) |
| ANALOG | 3 buttons 50Hz / 60Hz / OFF, bottom left; default 60 Hz per a 3rd-party summary, Off in her state | none | ❌ | needs DSP (hum + noise); low priority, Off is the common choice |
| MIX | knob 0–100 %, default 100, bottom strip | "Mix" 0–100 %, default 100 %, small knob | ✅ | |
| TRIM | knob −18…+18 dB, default 0, bottom right (after the mix) | none (Gain is pre-mix and 0…+30 only) | ❌ | new parameter (output trim ±18 dB) |
| AUTO MAKEUP | toggle under the VU-display knob, right; default Off | none | ❌ | new parameter: makeup tied to peak reduction |
| Mono / stereo components | separate Mono and Stereo plugins | stereo-linked engine | ⚠️ | no mono variant needed in a DAW slot |
| Attack / release | fixed (opto cell, program dependent: about 66 ms near the knee to 4 ms deep; two-stage release 60 ms then 0.5–15 s) | fixed: fast cell 10 ms / 50 ms + slow cell 400 ms / 0.4–5 s with memory | ⚠️ | no control in either (correct), but the behaviour differs (spec §6: slow 400 ms attack and a fixed 10 ms) |
| Static curve | γ ≈ 4.54 (≈ 4.2:1 local, 5.5:1 asymptotic) | γ = 2 (≈ 3:1) | ⚠️ | engine (spec §7) |
| HF tilt under GR / 2nd harmonic | yes (+1…+6 dB above 6 kHz under GR; ≈ −52 dBc H2) | none | ❌ | engine |
| Section layout | Gain left, VU centre, Peak Reduction right; Comp/Limit left; VU selector + Auto Makeup right; bottom strip Analog · Hi Freq · Mix · Trim | Gain left, VU centre, Peak reduction right; Comp/Limit under the VU; Emphasis + Mix small at the bottom | ⚠️ | the main row matches; the bottom strip has 2 of its 4 items, and the right column (selector, auto makeup) is missing |
| Proportions | 659×296 window, big knobs, VU in the middle third | 540×270, same composition | ✅ | |
| Knob scales | 0–100 printed around Gain and Peak Reduction | arc only; the value appears under the knob | ⚠️ | a CLA user reads Gain as 0–100; LYNX shows dB |
| Value entry / fine / reset | Waves value handling | right-click entry, Ctrl fine, double-click reset | ✅ | |
| Saved-state translation | — | Gain (via the measured table), Peak Reduction, Comp/Limit translated; Hi Freq, Analog, Mix, Trim, Auto Makeup and VU ignored | ⚠️ | PLUGIN_FALLBACK row 140 / round 113 §3c |

**Scores:** configuration 5/10, design 6/10. The main row is correct. A CLA-2A user misses Trim, Auto Makeup,
Analog, the 3-way VU selector, the 0–100 Gain scale and CLA's defaults (Gain 40 / PR 46 / Hi Freq 50).

**Top gaps:**
1. **Defaults and Gain scale.** Default Gain should give ≈ +8 dB, Peak reduction ≈ 46, Emphasis ≈ the Hi Freq 50
   equivalent. Show Gain as a 0–100 knob value (the dB value can go in the tooltip), following the measured table
   already used in `translateWavesCla2a`.
   Where: `glowInfo()` (MixSuite.cpp), `paramText`. Engine: only the Gain mapping law. Effort S.
2. **VU DISPLAY selector Input / GR / Output** as a visible 3-way switch beside the VU (today a hidden click with
   no Input).
   Where: `glowLeveler()` + the `vumeter` display (PluginEditors.cpp; it needs input-peak live output 2, which the
   engine already sets). UI only; it could be a non-audio parameter so it is saved. Effort S.
3. **TRIM ±18 dB** (post-mix) and **AUTO MAKEUP** toggle.
   Where: new parameters in `glowInfo()`, `glow::Leveler::process`, the layout's right column / bottom strip, and
   the translator (RW 206 / 207). Engine + UI. Effort S.
4. **Rename/relabel Emphasis as "HF emphasis (side chain)"** with a flat point, and translate Hi Freq (RW 153)
   once its law is measured (spec §8 Q4). Engine law + translator. Effort S–M.
5. **ANALOG Off / 50 / 60 Hz** (hum + noise, default Off). Engine (noise generator). Effort M; low priority.
6. **Header: A/B + copy, user presets (Load/Save), per-plugin undo.** These are shared generic-editor work
   (PluginEditors.cpp header) and benefit every engine. UI + state storage. Effort L. Also add a few factory
   presets for Glow.
7. **Engine fidelity** (spec §7: γ ≈ 4.5, level-dependent attack, HF tilt, H2). DSP. Effort M–L. A user hears this
   as "details missing" even when every control is there.

---

## Ridge Strip ← SSL E-Channel (Waves); also SSL Native Vocalstrip 2 (role only)
Sources:
- `docs/specs/ssl-e-channel.md`, `docs/specs/ssl-native-vocalstrip-2.md`;
- Waves "SSL E-Channel & G-Channel User Manual" (assets.wavescdn.com/pdf/plugins/ssl-e-channel.pdf);
- trial screenshot `look114/ref/sslch_main.png` and parameter list `look114/sslch_params.txt`;
- LYNX: `ridgeStrip()` (PluginLayout.cpp), `ridge::` (MixSuite.cpp), `lynx-before/Ridge Strip.png`.

Trial on this PC: yes (older capture used).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Header: Undo/Redo, A/B, A→B, preset Load/Save, name | WaveSystem bar | generic header, "(no preset)" | ❌ | |
| Overall layout | left = Filters + EQ (HF top → LF bottom); right = Dynamics (Compress, Expand, GR LEDs), EQ TO / DYN TO routing, Analog, Gain block with meters + Out fader | left = Filters, HF, HMF, LMF, LF, EQ curve; right = Compressor, Expander/gate, Dynamics (GR bars, routing), Gain | ✅ | same reading order and columns |
| HPF | 16–350 Hz, 18 dB/oct, fully left = Out (default Out) | "High-pass" 16–350 Hz 3rd order, 0 = off | ✅ | |
| LPF | 3–22 kHz, 12 dB/oct, fully left = Out | "Low-pass" 22 k→3 kHz 12 dB/oct, 0 = off | ✅ | |
| SPLIT (filters before dynamics) | button under the LPF, default off | none: the filters always run before the dynamics | ⚠️ | Ridge is effectively "Split on"; Split off (filters travel with the EQ) is missing |
| HF band: gain / freq / BELL | ±16.5 shelf / ±18 bell, 1.5–16 kHz, default 8 kHz; BELL button | gain ±15 (brown) / ±18 (black), 1.5–16 kHz, default 7.87 kHz; Bell toggle | ✅ | the readout "+6.0 dB / +7.2" (both curves at once) is confusing |
| HMF: gain / freq / Q | 600 Hz–7 kHz (default 3.5 k), Q 0.1–3.5 (default 1.5), gain ±15…±18 depending on Q | 600–7 kHz (default 2.0 k), Q 0.5–2.5 (default 1.0), gain ±15/±18 | ⚠️ | Q range is narrower, and the knob runs the opposite way (SSL: clockwise = wider, Q 3.5 → 0.1); different defaults |
| LMF: gain / freq / Q | 200 Hz–2.5 kHz (default 500 Hz), Q 0.1–3.5 (default 2.5) | 200–2.5 k (default 593 Hz), Q 0.5–2.5 (default 1.0) | ⚠️ | as HMF |
| LF band: gain / freq / BELL | ±16.5 shelf / ±18 bell, 30–450 Hz, default 60 Hz | ±15/±18, 30–450 Hz, default 89 Hz; Bell | ✅ | default differs |
| EQ curve family | E = "Black" 242 only (G-Channel = 383) | Brown \| Black tabs, default **Brown** | ➕ | E-Channel users expect Black; the translator uses Brown because it was fitted |
| EQ TO: BYP | button, EQ section bypass ("hardware-flat") | "EQ in" toggle | ✅ | |
| EQ TO: DYN S.C. (filters **and EQ** into the side chain) | button | "To sidechain" = filters only, **and it removes them from the audio** | ⚠️ | different semantics; the EQ cannot be sent to the side chain (de-essing use) |
| DYN TO: BYP | button | "Dyn in" toggle | ✅ | |
| DYN TO: CH OUT (dynamics post-EQ) | button, default **pre-EQ** | "EQ>Dyn \| Dyn>EQ" tabs, default **EQ>Dyn** | ⚠️ | the default order is the opposite of SSL's (the translator sets Dyn>EQ) |
| Comp RATIO | 1…∞, default 1 (off) | 1 + 19v² … ∞, default 1 (off) | ✅ | |
| Comp THRESHOLD | +10…−20 dBu (= −8…−38 dBFS), default 0 dBu (−18 dBFS); clockwise = lower | −40…0 dBFS, default 0 dBFS; clockwise = higher | ⚠️ | different unit and reference, opposite direction, default 18 dB higher |
| Comp RELEASE | 0.1–4 s, default 0.4 | 0.1–4 s, default 0.29 | ✅ | |
| Comp F.ATK | fast 1 ms; off = auto, program dependent | "Fast attack" 1 ms; off = fixed 10 ms | ⚠️ | no program-dependent attack |
| Auto make-up from ratio + threshold | always on (the manual) | none (the translator adds +0.9 dB output) | ❌ | engine |
| Comp Mix | — | "Mix" 0–100 % | ➕ | |
| Comp curve display | — | static transfer curve | ➕ | |
| Expander THRESHOLD | −30…+10 dBu (−48…−8 dBFS), default −30 | −80…0 dBFS, default −60 | ⚠️ | |
| Expander RANGE | 0–40 dB, default 0 | 0–60 dB, default 0 (off) | ⚠️ | |
| Expander RELEASE | 0.1–4 s, default 0.2 | 0.1–4 s, default 0.29 | ✅ | |
| Expander F.ATK | 1 ms / auto | "Fast" 0.1 ms / 1.5 ms | ⚠️ | |
| GATE switch (Exp default) | button + LED; expander is the default | "Gate \| Expand" tabs, default **Gate** | ⚠️ | default is the opposite |
| Threshold hysteresis | variable (program dependent) | fixed 4 dB (gate) | ⚠️ | |
| GR indicators | two LED ladders (comp, exp) 3/6/10/14/20 dB + gate LED | two horizontal bars "Comp" / "Gate" 0–30 dB | ✅ | different form, same information; no gate-open LED |
| ANALOG on/off | switch, centre right; default (her state) Off | none | ❌ | engine (colour) |
| INPUT trim | ±18 dB, knob in the Gain block | "Input" ±20 dB | ✅ | |
| Phase Ø | button + LED | "Phase" toggle | ✅ | |
| OUTPUT fader | fader −24…+12 dB beside the meter | "Output" knob ±20 dB | ⚠️ | knob, not a fader; different range |
| Level meter | stereo bar meter −50…0 dBFS with an INPUT / OUTPUT selector and a numeric peak readout ("−7.2") | none | ❌ | UI (the engine already exports in/out peaks via meters.set) |
| EQ response display | — | "Equalizer" curve | ➕ | |
| Latency | 1 sample | 0 | ✅ | irrelevant |
| Vocalstrip 2 (SSL Native) role | de-esser, de-ploser, 3-band EQ, compander with knee/makeup/drive, process order | Ridge at neutral defaults, no translation | ❌ | spec: translate the compander, better to LYNX Compressor/Crest |
| Saved-state translation | — | filters, 4 bands, comp (threshold, ratio, release, fast), output, dyn/EQ bypass translated; Split, Dyn S.C., Ch Out, expander, Analog, Input, Phase not translated (order forced to Dyn>EQ) | ⚠️ | round 113 §3c |

**Scores:** configuration 6/10, design 6/10. An SSL user finds every knob in the expected column. What is missing
or wrong: Split / Dyn S.C. semantics, Analog, auto make-up, the In/Out meter with the output fader, and
threshold units and directions.

**Top gaps:**
1. **In/Out level meter with an INPUT/OUTPUT selector and peak readout** in the Gain block.
   Where: `ridgeStrip()` (a new display id or reuse of a level-meter display) and PluginEditors.cpp. UI only;
   `meters.set` already exports in/out peaks. Effort S.
2. **Defaults to SSL's:** order Dyn>EQ, Expand (not Gate), Black curve for new instances, Q defaults 2.5 / 1.5,
   LMF 500 Hz, HMF 3.5 kHz, LF 60 Hz, comp release 0.4 s.
   Where: `ridgeInfo()`. Caveat: a default change affects existing projects that rely on unsaved defaults; check the
   state loader. Effort S.
3. **Routing semantics:** add a **Split** switch (filters pre-dynamics vs with the EQ). Turn "To sidechain" into
   SSL's **Dyn S.C.** (filters + EQ feed the side chain, audio unchanged).
   Where: `ridge::Strip::process` (new parameter "Split"; change `filtersToSidechain`). Engine. Effort M. Watch
   compatibility: today's "Filters to SC" behaviour must be kept or migrated.
4. **Auto make-up** M(threshold, ratio) (spec §7.4) and **program-dependent attack** when F.ATK is off.
   Engine. Effort M.
5. **Threshold display in dBu (0 dBu = −18 dBFS), SSL direction, ranges +10…−20 (comp) and −30…+10 (exp), range
   0–40.** `paramText` + laws. This is a range change, so the translator must be updated as well. Effort M.
6. **Q knob 0.1–3.5, reversed direction, gain range depending on Q**; show only the active curve's gain in the
   readout. Engine law + `paramText`. Effort S–M.
7. **Analog on/off** (console colour). DSP. Effort M.
8. **Header A/B, presets, undo**: shared generic work (see Glow #6).
9. **Vocalstrip 2 translator** (XML) to Compressor/Crest (spec ssl-native-vocalstrip-2 §7). Effort S.

---

## Compressor (generic stand-in) ← Renaissance Compressor / RComp (Waves); also CLA-76
Sources:
- `docs/specs/renaissance-compressor.md`, `docs/specs/cla-76.md`;
- Waves "Renaissance Compressor User Guide" (assets.wavescdn.com/pdf/plugins/renaissance-compressor.pdf);
- **new trial capture `/mnt/c/Users/kauan/lynx-test/parity129/ref/rcomp.png`** (371×525, latency 64 reported by
  the host in ref1.log);
- LYNX `effectInfo "Compressor"` + `class Compressor` + `formatEffectParam` (Effects.cpp). The panel is the
  generated generic one: a plain knob row Threshold · Ratio · Attack · Release · Makeup · Auto gain, with no meters
  and no display.

Trial on this PC: yes.

What the trial shows (rcomp.png):
- left: **Thresh** slider over a stereo **Input** meter, with numeric peak readouts;
- centre: a large **gain-change meter** (+6…−12 dB, bipolar for expansion) with the **Ratio** value box overlaid,
  and the GR readout below;
- right: **Gain** fader over a stereo **Output** meter, with a **limiter-activity** pill under the value;
- bottom: **Attack** and **Release** horizontal sliders with value boxes; **ARC**, **Electro**, **Warm** toggle
  buttons; **Mix** knob (0–100) and **Trim** knob (±18).
- There is **no transfer-curve graph**.

| Original feature | Original (range / default / where) | LYNX Compressor now | Status | Note |
|---|---|---|---|---|
| Header: Skins menu | Light / Dark / Legacy | — | ➕/n.a. | not needed |
| Header: Undo/Redo, A/B, A→B, Load/Save, preset name | WaveSystem bar ("A: Default Preset") | generic header, "(no preset)" | ❌ | |
| Threshold | −60…0 dBFS, default **0**, vertical slider integrated in the input meter | knob −60…0 dB, default **−18** | ⚠️ | range ✅; default and form differ |
| Ratio | 0.50…50 (below 1 = expansion), default **1.0**, value box inside the GR meter | knob 1…20, default **4:1** | ⚠️ | no expansion, no 20–50:1 |
| Gain (output) | −30…+30 dB, default 0, vertical fader on the output meter | "Makeup" 0…+24 dB | ⚠️ | no negative gain |
| Attack | 0.5…500 ms, default 16 ms, horizontal slider | 0.1…100 ms, default 10.2 ms | ⚠️ | |
| Release | 5…5000 ms, default 160 ms, horizontal slider | 10…1000 ms, default 120 ms | ⚠️ | |
| ARC / Manual | toggle, bottom | none (the translator fakes ARC with a fixed 300 ms) | ❌ | engine |
| Electro / Opto | toggle (GR-dependent release around 3 dB) | none | ❌ | engine |
| Warm / Smooth | toggle | none | ❌ | engine |
| Mix | 0–100 %, default 100, knob | none (only the DAW slot dry/wet) | ⚠️ | |
| Trim | −18…+18 dB, post-limiter, knob | none | ❌ | |
| Built-in 0 dBFS output limiter + activity indicator | always on; pill under Gain | none | ❌ | |
| Knee | soft, starts ≈ 6 dB below the threshold (fixed) | hard knee | ❌ | engine |
| Input meter (stereo) + peak readouts | left | none | ❌ | |
| Gain-change meter (bipolar, +6…−12) + readout | centre, the main display | none (no live output) | ❌ | |
| Output meter (stereo) + peak readouts | right | none | ❌ | |
| Latency / look-ahead | 64 samples @ 48 kHz | 0 | ⚠️ | |
| Auto gain (power-matched) | — | "Auto gain" toggle | ➕ | |
| External side chain | — | yes (DAW side-chain routing) | ➕ | |
| Gestures | drag, value windows accept typed values | generic contract | ✅ | |
| Saved-state translation | — | threshold (−5 dB), ratio, attack, release (ARC → 300 ms), gain; Warm/Smooth, Electro/Opto, mix, trim not translated | ⚠️ | round 113 §3c; 0.53 dB RMS on the stairs |
| CLA-76 (same stand-in) | Input/Output −∞…0, Attack/Release 1–7, Ratio 4/8/12/20/All, Meter In/GR/Out, Analog, Bluey/Blacky, Mix | Compressor at a fixed preset, no translation | ❌ | |

**Scores:** configuration 2/10, design 1/10. A plain row of 6 knobs with no meters cannot be read as RComp.

**Answer: does RComp need its own LYNX engine/editor?** **No new engine.** **Crest Compressor**
(`src/core/DynamicsSuite.cpp` `crest::`, editor `CrestEditor` in `src/ui/ProEditors.cpp`) already has almost
everything an RComp user looks for:
- threshold −60…0 and ratio 1…100;
- **soft knee 0–72 dB**;
- attack 0.005–250 ms and release 10–2500 ms;
- **Auto release** (≈ ARC), and an **opto** and a **classic** style (release-shape families);
- **Character Warm** (≈ Warm);
- Mix, Input/Output ±36 dB, Auto gain, look-ahead 0–20 ms (it can reproduce the 64-sample delay), oversampling;
- **IN / OUT / GR meters**, a GR history display and a knee/curve side panel.

Glow is the wrong host: it is a fixed-time opto leveler with no threshold, ratio or attack.

Recommendation:
1. **Re-route `[waves-rcomp]` (PluginSubstitutes.cpp) from Compressor to Crest Compressor.** Write a
   `translateWavesRComp` for Crest along the lines of spec §7:
   - threshold RW1 − 4, ratio RW9, **knee 18 dB**, attack RW8, release RW0;
   - auto release = ARC (RW17 = 0);
   - Character Warm when RW16 = 0;
   - Mix (RW26), Output = gain RW2, look-ahead 1.33 ms.
   - Re-fit on the round-113 stairs and take (target ≤ 0.15 dB RMS). Effort M.
2. **Add the two things Crest lacks**, both in `crest::`:
   - an **"Electro / Opto" release-shaping option**: GR-dependent τ around 3 dB (spec §5.3), as a "release
     behaviour" parameter that is active with Auto release;
   - a **post-output 0 dBFS safety limiter + trim**.
   - Expansion ratios 0.5–0.99 can be left out at first: her state never uses them, and Sentry Gate covers
     downward expansion.
   - Effort M.
3. **Keep "Compressor"** as FL's generic effect. Give its generated panel at least a GR meter: add
   `controlOutput` to `class Compressor` and a layout with a `grmeter` (S). Point CLA-3A / C1 / SSLComp /
   API-2500 at Crest as well with style presets (classic for C1/SSLComp, opto for CLA-3A). CLA-76 → Crest
   "classic" style with µs attack (Crest goes to 5 µs) and a ratio set 4/8/12/20 (translator after spec §8
   measurements).
4. **Editor (design):** Crest's display-first layout is a different design from RComp's three vertical meters,
   but every RComp control has an obvious home: Thresh = threshold line + knob, Ratio knob, Gain knob + Out meter,
   Attack/Release knobs, Auto (= ARC), Style menu (Electro/Opto would sit here), Character (Warm), Mix. An RComp
   user should rate it 7/10 for findability. Optionally add a compact "classic" view toggle (input meter + threshold,
   GR meter + ratio, output meter + gain): UI only, L. It is not required.

**Top gaps** (if Compressor stays the substitute):
1. GR / In / Out meters (S).
2. Soft knee 6–18 dB (S).
3. Signed gain −30…+30 + trim + 0 dBFS limiter (M).
4. ARC + Electro/Opto release (M).
5. Warm/Smooth (M).
6. Ranges: ratio 0.5–50, attack 0.5–500, release 5–5000 (S).
7. Mix (S).
8. Header A/B / presets / undo (L, shared).

The re-route to Crest gets 1, 2, 5, 7 and most of 3/6 for free.

---

## Hush De-esser ← DeEsser (Waves)
Sources:
- `docs/specs/deesser.md`;
- Waves "DeEsser Plug-in Manual" (assets.wavescdn.com/pdf/plugins/deesser.pdf): chapters 2 (Quick Start),
  4 (Controls and Displays), 5 (Internal design) and 7 (Factory Presets);
- `HushEditor` (ProEditors.cpp), `hush::` + `paramText` (MixSuite.cpp), `lynx-before/Hush De-esser.png`.
- **Trial screenshot not available**: `deesser.png` was not produced in this run, so the original's layout is
  taken from the manual's text.

Trial on this PC: yes (no screenshot this round).

| Original feature | Original (range / default / where) | LYNX Hush now | Status | Note |
|---|---|---|---|---|
| Header: Undo/Redo, A/B, A→B, Load/Save, resize | WaveSystem bar | ProEditor header: title, slot, gesture hint only | ❌ | |
| Factory presets | Default (5506 Hz HP Split), Male Ess 4500, Male Shh 3385, Female Ess 6779, Female Shh 5077 (all BandPass + Split) | none | ❌ | 4–5 generic-named presets (frequency + detector) would cover it |
| Thresh | slider −80…0 dB, default **0**, aligned with the Energy Detector meter | "Threshold" big knob −60…0 dB, default **−30 dB**; also a draggable line on the waveform and an arrow on the meter | ⚠️ | 3 ways to set it ➕; range is 20 dB shorter; default differs |
| Freq | value window 2000–16000 Hz (log), default **5506 Hz** | horizontal slider 2–16 kHz with end labels, same log law; default **6.02 kHz** | ✅ | same law; set the default to 5506 (v = 0.487) |
| SideChain HighPass / BandPass | button, default HighPass | "Detection: High-pass \| Band" tabs (+ bottom-bar menu), default High-pass | ✅ | Hush BP Q 1.4; Waves "very sharp", Q unknown |
| Audio Split / Wideband | button, default **Split** | "Processing: Wide band \| Split band" tabs, default Split | ✅ | Hush's split is 2nd order at the frequency; Waves' is 1st order at ≈ Freq/π (engine) |
| Monitor Output / SideChain (+ yellow light) | button | "Audition" toggle under the frequency slider (plays the detection band) | ✅ | same function, different name |
| Energy Detector meter (side-chain energy, dB below 0 dBFS, blue) aligned with the threshold | main meter | "IN" meter = **input** peak, with a threshold arrow | ⚠️ | threshold is compared with an unfiltered level; switch IN to side-chain energy (as Waves) or add it |
| Attn meter (red, instantaneous GR) + **infinite peak hold, click to reset** | meter | "GR" bar (0–30 dB scale, shared with the IN scale) | ⚠️ | no peak hold / reset |
| Output meter −30…0 dBFS + peak readout (click to reset) | meter | none (bottom bar shows the Output **setting**) | ❌ | UI |
| Frequency-response display | **none in the classic DeEsser** (the manual lists no graph; Renaissance DeEsser has one) | small detector-band graph only in the generic `hushDeEsser()` layout; the display-first editor shows the scrolling waveform with the reduction in red | ➕ | nothing to add for parity |
| Range | none (the floor is the split's leakage) | "Range" 0–24 dB, default 12 | ➕ | translator fixes it at 12 dB |
| Release | none (hidden, unknown) | 10–250 ms, default 61 ms | ➕ | |
| Lookahead | none, 0 latency | 2 ms toggle, **on** by default (adds 2 ms of latency) | ➕/⚠️ | the default should be Off for Waves parity (spec §7.3) |
| Output gain | none | ±12 dB | ➕ | |
| Knee / ratio | hard knee, effective ratio ≈ 7, RMS energy | 3 dB soft knee, ratio ∞, peak | ⚠️ | engine (spec §7.2) |
| Gestures | slider drag, value-window typing | drag, Ctrl fine, wheel, reset, right-click entry, line/arrow drags | ✅ | |
| Saved-state translation | — | frequency (×0.81), threshold (+5 dB), Split/Wide, HP/BP, Monitor translated; range fixed at 12 dB | ✅ | round 113 §3c |

**Scores:** configuration 7/10 (every Waves control exists, plus extras), design 5/10. The scrolling-waveform
editor is Pro-DS-like. A DeEsser user finds Threshold, Freq, HP/BP, Split/Wide and Monitor (as "Audition")
quickly. What they miss: the side-chain energy meter beside the threshold, the Attn peak hold, the output meter,
presets, and the Waves defaults.

**Top gaps:**
1. **Defaults:**
   - Frequency 5506 Hz (v 0.487);
   - Lookahead Off;
   - a threshold default that does not de-ess until it is lowered: Waves starts at 0 dB, Hush at −30 dB.
   Where: `hushInfo()`. Effort S.
2. **Meter semantics:** make the IN meter show the **detector (side-chain) energy** the threshold is compared with
   (or add it as a second bar). Add **GR peak hold with click-reset** and an **OUT meter**.
   Where: `HushEditor` (ProEditors.cpp) plus a live output for the detector level in `hush::DeEsser`
   (`meters.set`). Engine export + UI. Effort S–M.
3. **Factory presets**: Default / Male S / Male SH / Female S / Female SH frequencies, with BP + Split.
   Generic names, no Waves trade names needed. This needs the shared preset mechanism (see Glow #6) or a Hush-only
   menu. Effort S once presets exist.
4. **Threshold range −80…0.** Law + translator. Effort S.
5. **Engine** (spec §7): 1st-order split at Freq/π, RMS detector, ratio ≈ 7 hard knee, no range clamp for
   translated states, 0 latency. DSP. Effort M.
6. **Header A/B, undo, presets** in the ProEditor header (shared with Crest, Apex and the others). Effort L.

---

## Saved-state translation summary (docs/PLUGIN_FALLBACK.md, round 113 §3c)

| Original | LYNX | Translated | Not translated |
|---|---|---|---|
| RComp | Compressor | threshold (−5 dB), ratio, attack, release (ARC → 300 ms), gain | Warm/Smooth, Electro/Opto, Mix, Trim (auto gain forced off) |
| CLA-2A | Glow Leveler | Gain (measured table + 1.5 dB), Peak Reduction (0.40 + 0.021·(k − 40)), Comp/Limit | Hi Freq (emphasis forced 0), Analog, Mix, Trim, Auto Makeup, VU |
| SSL E-Channel | Ridge Strip | HP, 4 bands (fitted laws), EQ/dyn bypass, comp threshold (−15.7)/ratio/release/fast, output (+0.9 make-up) | Split, Dyn S.C., Ch Out (forced Dyn>EQ), expander, Analog, Input, Phase, LP (forced off) |
| DeEsser | Hush | frequency (×0.81), threshold (+5), Split/Wide, HP/BP, Monitor | range fixed at 12 dB; look-ahead/release stay at defaults |
| CLA-76, CLA-3A, C1, SSLComp, API-2500 | Compressor | nothing (fixed preset: −15 dBFS, 2:1, auto gain) | everything |
| SSL Native Vocalstrip 2 / Channel Strip | Ridge Strip | nothing (neutral) | everything |

## Quell Denoiser ← NS1 Noise Suppressor (Waves)
Sources: docs/specs/ns1.md; REA_rodada119 §3; trial screenshot w119/ref/NS1_Stereo.png (default preset); LYNX
src/core/VoiceTools.cpp (`quellInfo`, `paramText`), PluginLayout.cpp `quellDenoiser()`, lynx-before/Quell Denoiser.png;
third-party control reference josephnilo.com/blog/waves-ns1-explained. Trial on this PC: yes.

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| NS1 fader (the only control) | 0–100 %, tall vertical fader on the right | "Amount" tall vertical slider on the right, 0–100 % | ✅ | same place, same unit |
| Fader default | 0.0 (default preset reads 0.0) | 30 % | ⚠️ | LYNX opens already suppressing; her states are 23–70 % (translated, so only new inserts are affected) |
| Fader scale | 0…100 printed both sides every 10 | none (bare track, pink fill) | ⚠️ | a user reads the setting off the scale |
| Fader read-out | numeric box under the fader ("0.0"), editable | "30.0%" value under the slider, right-click entry | ✅ | |
| Attenuation meter | left, vertical, 0 … −48 dB, ticks every 6 dB both sides | left, vertical, 0 … −30 dB, ticks every 6 dB | ⚠️ | NS1 can show 48 dB; Quell clips its scale at 30 (Quell max reduction ~25 dB so fine for the engine, but the scale differs) |
| Meter read-out | numeric box under the meter | "0.0 dB" under the meter | ✅ | |
| Meter meaning | "overall energy removed from the input" | broadband reduction | ✅ | |
| Meter peak hold / reset | numeric read-out box (Waves convention: click to reset) | live value only | ⚠️ | |
| Listen (hear the removed noise) | — | toggle bottom-left | ➕ | |
| Output trim | — | −12…+12 dB knob bottom-right | ➕ | |
| Mono / Stereo components | NS1 Mono, NS1 Stereo | one stereo engine, linked | ✅ | LYNX is per-slot stereo; fine |
| Adaptive noise-floor behaviour | "analyzes and adapts in real time"; steady noise drifts by a few dB over seconds | fixed 6-band expander (fitted), no adaptation | ⚠️ | engine, not UI (spec §4.3–4.4; 63 Hz octave +5…7 dB after SSL HPF) |
| Latency | not published; not measured (the first plugin our host loads from the shell is mis-instantiated, so its reported latency is not trusted) | 0 | ? | measure on the trial if it matters |
| Undo / Redo | toolbar | song-level Ctrl+Z only | ⚠️ | |
| Presets (load/save, factory) | toolbar | "(no preset)", no save | ❌ | |
| A/B, Setup A/B, copy A→B | toolbar | — | ❌ | |
| ◀ ▶ preset step | toolbar | ◀ ▶ | ✅ | empty list |
| Help / options menu | toolbar ≡ | options menu (reset, bypass) | ✅ | |
| MIDI learn | right-click | right-click MIDI link | ✅ | |
| Default / fine / wheel gestures | Alt-click, Ctrl-drag, wheel | double/Alt-click, Ctrl-drag, wheel | ✅ | |
| Bypass | host / Waves toolbar | LED in header + options | ✅ | |
| Window size | fixed 345×525-ish | resizable, fixed aspect, 300×420 | ✅ | |
| Logo / branding area | top-left logo, product name bottom-left | section caption "Noise suppressor" | ✅ | (trade dress not copied — correct) |

**Scores** — configuration: 8/10, design: 8/10.
**Top gaps**
1. Meter scale 0 … −48 dB (ticks 6 dB) instead of 0 … −30 — `quellDenoiser()` meter item / the grmeter range in
   src/ui/PluginEditors.cpp (`grmeter` scale per item; add a scale field) — UI only, S.
2. Amount default 0 % (or keep 30 % but say so) — `quellInfo()` default; translation unaffected — S (decide; a
   one-knob denoiser that does nothing on insert matches NS1, but LYNX users may prefer an audible default).
3. Fader scale 0…100 with ticks every 10 beside the slider — PluginEditors.cpp vslider drawing (a `scale` style
   flag) — UI, S.
4. Peak-hold on the meter read-out (click to reset) — PluginEditors.cpp grmeter — UI, S.
5. Adaptive noise-floor tracker (spec §7) — VoiceTools.cpp quell — engine, L.
6. (Global) A/B + user preset save — generic editor — M (shared with every engine).

## Gemini Doubler ← Doubler2 / Doubler4 (Waves)
Sources: docs/specs/doubler.md; REA_rodada119 §4; Waves Doubler user guide (assets.wavescdn.com/pdf/plugins/doubler.pdf,
text extracted); trial screenshot w119/ref/Doubler2_Stereo.png; LYNX VoiceTools.cpp (`geminiInfo`, laws),
PluginLayout.cpp `geminiDoubler()`, PluginEditors.cpp `geminipan` / `geminimap` / `geminieq`; lynx-before/Gemini Doubler.png.
Trial on this PC: yes (Doubler2 / Doubler4).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Pan / gain graph (half circle, voices + D marker) | top-left; angle = pan, radius = gain; **"Gain, Pan, Delay and Detune can be controlled using the graphs"** (drag) | `geminipan`, same place, D + 1 + 2 markers | ⚠️ | display only — not draggable |
| Delay / detune graph | top-centre; x 0…100 ms (labels 0 20 40 60 80 100), y +100…−100 c with "100 / 0 / 100" labels; draggable | `geminimap`, x labels 0…100, no y labels, not draggable | ⚠️ | add the cents labels; drag |
| EQ graph | top-right; L and H markers on the curve, x labels 62 / 250 / 1K / 4K, 0 dB tick; drag markers, **double-click a marker = filter on/off** | `geminieq` curve only, no markers, no axis labels | ⚠️ | |
| Direct on/off | row 2, left; On | toggle "Direct", On | ✅ | |
| Direct gain | 0…−48 dB (manual default −12; trial default 0) | 0…−48 dB, default 0 dB | ✅ | |
| Direct pan / stereo rotation | ±45°, centre | ±45°, rotation law measured | ✅ | |
| Align (direct) | NO/YES button; default Off | toggle, default **On** | ⚠️ | default differs |
| FB high-pass | 16…12000 Hz, default 16; numeric box with a HP icon | "FB low cut" knob 16 Hz…21.4 kHz, default 16 | ⚠️ | range top 12 kHz in the original |
| Range | 80 Hz / 20 Hz, manual default 20 Hz (trial screenshot shows 80HZ) | tabs 80 Hz / 20 Hz, default 80 | ✅ | |
| Modulation RESET | button, automatable, restarts every LFO in sync | — | ❌ | needs a trigger param + engine reset |
| Low shelf on / gain / freq | L marker button + gain knob + freq box; gain +12…−24 dB, below −24 "the floor drops to make a cut filter"; default On | L toggle, gain −30…+12, freq 16 Hz–21 kHz, default **off** | ⚠️ | no cut mode below −24; default off vs On |
| High shelf on / gain / freq | H marker + gain + freq box | H toggle, gain, freq, default off | ⚠️ | same remarks |
| EQ acts on voices only | yes | yes | ✅ | |
| Voice on/off | coloured numbered buttons 1 / 2 | toggles "1" / "2" | ✅ | |
| Voice gain | 0…−48 dB, default −12 | 0…−48 dB, default −6 | ✅ | default differs slightly |
| Voice pan | ±45°, defaults V1 22.5 / V2 −22.5 (V3 45, V4 −45) | ±45°, defaults −45 / +45 | ⚠️ | defaults |
| Voice delay | 0…100 ms, default 8 ms; min 8 / 14 ms when not aligned | 0…100 ms, defaults 9.4 / 23.7 | ✅ | the 8/14 ms minimum not enforced (measured: not enforced by the trial either, spec §4.2) |
| Voice feedback (FDBK) | 0…100 %, default 0, warning > 25 % | 0…100 %, 0 | ✅ | |
| Octaver | ▼ arrow button, octave down | "Oct" toggle | ✅ | |
| Detune | ±100 c | ±100 c | ✅ | |
| Mod depth | ±200 c, positive starts upward | ±200 c | ✅ | |
| Mod rate | 0.1…200 (trial up to 400), default 1.0 | 0.1…400, default 1.0 | ✅ | |
| Value editing in the voice strip | numeric boxes (drag / type); gain & pan as small knobs | knobs everywhere with values under them | ⚠️ | design only; drag/type works on both |
| Per-voice column order | Voice · Gain · Pan · Delay · Fdbk · Oct · Detune · Depth · Rate | 1 · Gain · Pan · Delay · Feedback · Oct · Detune · Depth · Rate · Source | ✅ | same order |
| Source per voice (In 1 / In 2) | bottom-left drop-downs "In 1 L▾", "In 2 R▾" (L / L+R / L−R) | tabs at the end of each voice row | ⚠️ | different place, same options |
| Master output gain | right column, vertical fader 0 … −∞ (scale 0 / 24) | vertical fader −24…0 dB, no scale | ⚠️ | no −∞, no scale marks |
| Output meters | stereo bar meters beside the fader, scale 0…−24, peak counters under them (click to reset) | — | ❌ | |
| Clip light | red, click to reset | — | ❌ | |
| Doubler4: voices 3 and 4 | 4 rows, each with its source | 2 voices only | ❌ | engine + params + translation (spec §7 item 6) |
| Components Mono / Mono→Stereo / Stereo | 3 | 1 stereo | ⚠️ | M→S = stereo on a mono-fed track; fine |
| Undo / Redo, presets, A/B, Setup A/B, Save | toolbar | see header note | ❌ | |
| MIDI learn, gestures | right-click; Alt-click etc. | yes | ✅ | |
| Window | fixed 800×580 | resizable 800×460 | ✅ | |

**Scores** — configuration: 7/10 for Doubler2 users, 4/10 for Doubler4 users; design: 6/10.
**Top gaps**
1. Draggable markers in the three graphs (pan/gain, delay/detune, EQ L/H; double-click a shelf marker toggles it) —
   PluginEditors.cpp `geminipan` / `geminimap` / `geminieq` + a display-drag hook in mouseDown/mouseDrag (today only
   `vumeter` handles clicks) — UI only, M.
2. Doubler4 voices 3–4 — VoiceTools.cpp gemini (append V3/V4 params so old projects load unchanged), `geminiDoubler()`
   rows 3–4 (layout grows ~150 px or a 2/4 voice switch), translator `waves-doubler` blocks 46/56 — engine, M.
3. Output meters + clip light beside the output fader — `geminiDoubler()` add `meter(...)`/vumeter items in the Output
   section — UI, S.
4. Modulation Reset button — new trigger param ("Mod reset") + LFO phase reset in gemini::Gemini — engine, S.
5. Graph labels: cents scale on the delay/detune graph, frequency labels + 0 dB tick + L/H markers on the EQ graph —
   PluginEditors.cpp — UI, S.
6. Shelf "cut" below −24 dB and defaults (shelves On, Align Off, pans ±22.5, gain −12) — VoiceTools.cpp — engine +
   defaults, S (defaults must not change translated states: translation sets every value).
7. Output fader to −∞ with a scale — VoiceTools.cpp `outputDb` law + vslider scale — S.

## Echoform ← H-Delay Hybrid Delay (Waves)
Sources: docs/specs/h-delay.md; REA_rodada119 §2; josephnilo.com/blog/waves-h-delay-explained (citing the H-Delay
guide); trial screenshot w119/ref/H-Delay_Stereo.png; LYNX EchoSuite.cpp (`echoformInfo`, laws, `paramText`),
PluginLayout.cpp `echoformLayout()` (ValhallaDelay-style section row), lynx-before/Echoform.png. Trial on this PC: yes.

How an H-Delay user finds things on Echoform today: Delay → "DELAY" big knob (✓, same idea); BPM/HOST/MS → a small
menu "ms|Note|Dotted|Triplet" under the knob (no own-BPM mode); note buttons → note knob + the dotted/triplet menu;
Feedback → small knob in its own section (H-Delay: the second big knob); Mix → top-left "MIX" (H-Delay: top-right
"DRY/WET"); LoFi / Analog → hidden inside the 12-entry MODE menu at the bottom; Ping-pong / Dual → the STYLE knob
(5 styles) + "Pairs"; Phase L/R → nowhere; HiPass / LoPass → "EQ: High / Low" (stacked, reversed order); Mod
depth/rate → "MOD" section, which shows Wow/Flutter in the default Tape mode; output → "OUTPUT: Level"; tap → nowhere;
delay display → an echo-train picture (not the note/BPM read-out).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Delay knob | big, left; 1–3500 ms or note 1/64T … 2 Bar; default 1/8 dotted (Host) | big "Delay" knob in DELAY section; 0–20000 ms (piecewise law) or note 1/64…8/1; default 300 ms | ⚠️ | same role; default ms not a note |
| Note values on one knob | one knob steps triplet / straight / dotted for each length (1/64T … 2 Bar) | note knob (10 lengths) + separate kind menu (Note / Dotted / Triplet) | ⚠️ | two controls for one; reachable |
| Sync HOST | button under the display; default | menu entry "Note" (follows project tempo) | ⚠️ | named differently |
| Sync BPM (own tempo) + BPM value | button + BPM number in the display | — | ❌ | translator folds BPM into ms |
| Sync MS | button | menu entry "ms" | ✅ | |
| Central display | note value (e.g. "1/8 D") and tempo "120 BPM" (or ms); click-drag / type to edit | value text under the knob + echo-train picture (echoes on a time axis) | ⚠️ | no note/tempo read-out block; echotrain ➕ |
| Tap pad | big pad far left; last two taps; Host: snaps to a note, BPM: sets BPM, MS: sets ms | — | ❌ | |
| Ping Pong | button above the display; Off; start-side value | STYLE = "Ping-pong" (5th of 5) + "Pairs" toggle (alternate / H-Delay pairs) | ⚠️ | no button; start side ❌ |
| Ping-pong start side (L→R / R→L) | value box | — | ❌ | translator ignores RealWorld 45 |
| Dual (independent L/R times) | button; drag L or R in display | STYLE = "Dual" + Delay R / Sync R | ✅ | |
| Phase L / Phase R (ØL ØR) | two buttons beside Ping Pong / Dual; Off | — | ❌ | new params |
| Feedback | big knob right of display, 0–200 %, default 60 | small knob, 0–200 %, default 70 | ⚠️ | size / place / default |
| Dry/Wet | top-right knob 0–100, default 100, equal power | "Mix" top-left, 0–100 %, default 50 %, equal power | ⚠️ | place + default |
| Output | knob −18…+18 dB, default 0 | "Level" −18…+18 dB, default 0 (round 119) | ✅ | |
| Output meter | vertical, 0 … −48 dB, beside Dry/Wet | — | ❌ | |
| Peak / clip LED | red lamp above the meter | — | ❌ | |
| LoFi | switch under Tap; Off; steep LP (≈ 5.8 kHz, 8th order+) inside the loop | MODE "Lo-Fi" (a menu entry that also sets the whole character) | ⚠️ | not an independent switch; law differs |
| Analog | knob Off / 1 / 2 / 3 / 4, default 2 | MODE menu (Analog, Bucket brigade, Tape …) + Drive / Age | ⚠️ | no 0–4 positions, no hiss / loop compression of Mod2 |
| Mod Depth | 0–100, default 0; bottom-left MODULATION group | "Depth" in MOD section, only in non-tape modes; default 50 % | ⚠️ | default Tape mode shows Wow/Flutter instead |
| Mod Rate | 0.1 … 6000 Hz (BPM sync in concept), default 0.1 | 0.01 … 10 Hz, default ≈ 0.5 Hz | ⚠️ | range / default |
| HiPass | Off, 20 Hz–20 kHz, default Off; bottom FILTERS group (left) | "Low" (EQ section, lower knob) 10 Hz–2 kHz linear | ⚠️ | no Off label, range to 2 kHz, linear taper |
| LoPass | 20 Hz–20 kHz, Off, default Off (right) | "High" (upper knob) 200 Hz–20 kHz | ⚠️ | range, order reversed |
| Filter LINK | button between HP and LP; moves both | — | ❌ | |
| Components Mono / Stereo / M>S | 3 | one stereo engine + Width | ⚠️ | Width ➕ |
| Delay changes glide (tape-like) | yes | depends on mode | ⚠️ | |
| Loop limiter (fb > 100 % stays bounded; −3 dB loud passes) | engine | Drive/Age shaper in some modes; no loop limiter in Clean | ⚠️ | engine (spec §7 item 1) |
| Style Single / Ratio / Quad | — | STYLE | ➕ | |
| Diffusion amount / size | — | DIFFUSION section | ➕ | |
| Ducking | — | OUTPUT section | ➕ | |
| Width | — | FEEDBACK section | ➕ | |
| 12 modes (Tape, Shifter, Pitch, Reverse, …) | — | MODE menu | ➕ | |
| MIDI learn (all but meter) | right-click | right-click MIDI link | ✅ | |
| Gestures (Alt-click reset, Ctrl fine, wheel, type) | yes | yes | ✅ | |
| Undo / Redo, presets, A/B, Setup A/B, Save | toolbar | see header note | ❌ | |
| Window | fixed ~750×290 | resizable 780×300 | ✅ | |

**Scores** — configuration: 5/10, design: 4/10 (the controls exist for ~60 % of H-Delay, but in ValhallaDelay's
places and names; Phase, Tap, own BPM, Link, meter are missing).
**Top gaps**
1. Tap pad — a `tap` button item in `echoformLayout()` (DELAY section) + editor handler (PluginEditors.cpp) that
   averages the last two taps and writes Time L (ms mode) or the nearest Note L/kind (note mode) — UI only, S/M.
2. Phase L / Phase R toggles — append params "Phase L", "Phase R" (polarity of each delay line) in EchoSuite.cpp,
   two toggles in the FEEDBACK or DELAY section; translator RealWorld 8 / 9 — engine (trivial DSP) + UI, S.
3. Own BPM sync — append "Sync source" (host / own BPM) + "BPM" params; ms/note text shows tempo; translator keeps
   the BPM instead of folding it to ms — engine + UI, M.
4. Ping-pong as a button + start side — keep STYLE but add a visible "Ping-pong" toggle that sets Style, append
   "Start side" param; translator RealWorld 45 — engine (swap L/R at the input) + UI, S.
5. Output meter + clip lamp in the OUTPUT section — `echoformLayout()` meter item (output level) — UI, S.
6. LoFi as an independent switch (in-loop ≈ 5.8 kHz 8th-order LP, spec §4.2) and an "Analog 0–4" selector bound
   to the loop limiter / colour / hiss of spec §7 — EchoSuite.cpp — engine, M/L.
7. Filters: HP and LP over 20 Hz–20 kHz (log taper) with "Off" at the ends, HP left / LP right, + LINK — append new
   params (keep the old ones for old projects) — engine + UI, M.
8. A note/tempo read-out block in place of (or above) the echo-train picture: "1/8 D · 120 BPM" / "375 ms" — UI, S.
9. Defaults when inserted fresh (Mix 100 on an FX bus is H-Delay's; LYNX's 50 % suits inserts) — decide; S.

**Waves delays mapped to Fruity Delay 3 (one table)**

| Original | Maps to now | Better LYNX engine | Recommendation |
|---|---|---|---|
| Waves SuperTap (2-Taps / 6-Taps: per tap delay, gain, pan, feedback, filter, pitch mod; tap pad, BPM/ms) | Fruity Delay 3 at FL's default (no translation) | **Fruity Delay Bank** (FL's multi-tap bank, exact engine) for the per-tap layout; **Lattice Mod** (multi-tap with spread/slope/tap gain) as a creative alternative | Move SuperTap → Fruity Delay Bank (taps = SuperTap taps; times/gains/pans translatable once the RealWorld list is read on the trial); keep FD3 only as fallback |
| (H-Delay already → Echoform, round 119) | Echoform | — | keep |
| Other Waves delays seen in the substitute table | none (only SuperTap and H-Delay are listed under `waves =`) | Echoform for Manny Marroquin Delay / Abbey Road tape-style delays if they appear | add rows only when found in her projects |

## LYNX Tune ← Waves Tune Real-Time (Waves)
Sources: docs/specs/waves-tune-real-time.md (v16 user guide [S1], tapers read from the trial in round 120);
REA_rodada120.md; josephnilo.com/blog/waves-tune-real-time-explained (control reference citing the guide); LYNX
LynxTune.cpp (`info`, `paramText`, laws), PluginLayout.cpp `lynxTune()` (Auto-Tune-Pro "auto" workflow, round 120;
re-skinned round 122: `pitchring`, `pitchtrace`), lynx-before/LYNX Tune.png. Trial on this PC: yes — but the
round-129 screenshot (ref/wtune.png) had **not been produced** when this was written (the capture run stopped
after rcomp.png), so the "where" column for Waves Tune RT comes from the guide, not from a picture. Re-check the
positions against wtune.png once it exists.

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Speed | 0.1–800 ms, default 15; piecewise taper (½ travel = 50 ms); in-note drift / vibrato tightness | — (one "RETUNE SPEED" knob only) | ❌ | measured effect small (only < 5 ms), but a WTRT user looks for two time knobs |
| Note Transition | 0.1–800 ms (0.1 ms steps), default 120 | "RETUNE SPEED" 0–400 ms (quadratic), default 20 ms; translator maps NT → retune (τ = NT, matched ±10 %) | ⚠️ | name, range (800), default; behaviour matched |
| Link (Speed ↔ NT) | Off | — | ❌ | follows from Speed |
| Tolerance Cents | Off–40 c (added to the 50 c switch threshold) | — | ❌ | LYNX has fixed hysteresis |
| Tolerance Time | Off–300 ms | — | ❌ | |
| Vibrato On/Off | On | — | ❌ | (Flex-Tune / Humanize are Auto-Tune concepts, not equivalents) |
| Vibrato Depth | 0–200 %, default 100 | — | ❌ | |
| Correction On/Off | On | slot bypass / header LED | ⚠️ | no in-panel switch |
| Correction % | 1–100 %, default 100 | MIX 0–100 % (dry/wet) | ⚠️ | not the same: Mix blends dry + shifted (phasey); Correction scales the pitch shift |
| Formant | Corrected / Non-Corrected, default Corrected | "FORMANT" toggle, On | ✅ | |
| Formant shift | — | ±7 st | ➕ | |
| Reference pitch | ±100 c (415.3–466.16 Hz), 440 | "A4" knob 430–450 Hz | ⚠️ | range ±40 c only |
| Root | 17 names (sharps and flats) | KEY menu, 12 names (sharps) | ✅ | |
| Scale type | 44 (User, Chromatic, Major, Natural/Harmonic/Melodic minors, Justonic, modes, Blues, Whole tone, Diminished, Bebop, pentatonics, Pythagorean, Maqamat, Indian thaats …) | SCALE menu, 13 + Custom | ⚠️ | her states use Major / Natural Minor only (covered) |
| Range | keyboard span with draggable boundary handles + presets Bass / Baritone / Tenor / Alto / Mezzo / Soprano / Generic; notes outside → **bypassed** | "Input type" menu (Soprano / Alto-Tenor / Low male / Instrument) = detector range | ⚠️ | 4 presets vs 7, no handles, outside notes not bypassed |
| Keyboard | multi-octave piano at the bottom, per-note status area | one octave of on/off key toggles at the bottom ("TARGET NOTES") | ⚠️ | same place, less content |
| Note status per key | Legal / Illegal / Illegal-Pitch-Up / Illegal-Pitch-Down / Bypass (click cycles) | on / off | ⚠️ | Up/Down/Bypass missing |
| Group Octaves | switch (per-note edits apply to all octaves) | always grouped | ⚠️ | |
| Reset Scale / Send Scale to All / Receive (key) | buttons | — (key comes from translation / project key) | ❌ | |
| Keyboard view / navigation buttons | yes | — | ❌ | one octave only |
| Central display | target note name + correction amount in cents | `pitchring`: note name, ±100 c ring, sung offset + octave | ✅ | |
| Scrolling pitch graph | not in WTRT (guide lists none) | `pitchtrace` (input vs corrected, 8 s) | ➕ | Auto-Tune-style; useful |
| Input / output meters | −28 … 0 dBFS | — | ❌ | |
| MIDI Target Pitch | MIDI note forces the target (Speed/NT timing) | "MIDI channel" (held pitch classes become the targets) | ⚠️ | pitch class only, no octave |
| MIDI Reference Tone + level | tone generator ±18 dB | — | ❌ | |
| Latency | reports 0; real 0–4 ms dynamic | ~25 ms reported + PDC; "Low latency" toggle | ⚠️ | ➕ toggle; different by design |
| Transpose | — | ±12 st stepper | ➕ | |
| Tracking | — | knob | ➕ | |
| Humanize / Flex-Tune | — | knobs | ➕ | Auto-Tune Pro role |
| Mono / Stereo components (detect on (L+R)/2) | 2 | one stereo slot | ✅ | |
| Undo / Redo, presets (factory/user/favourites, search), A/B, Setup A/B, Save | toolbar | see header note | ❌ | |
| MIDI learn, gestures | right-click; Alt-click etc. | yes | ✅ | |
| Window | fixed | resizable 800×548 | ✅ | |

**Scores** — configuration: 4/10 (for a Waves Tune RT user: key/scale/formant/time are there, but Speed, Tolerance,
Vibrato, Correction %, note statuses and range bypass are not), design: 5/10 (note display centre + keyboard at the
bottom match; the knob row is Auto-Tune's).
**Top gaps**
1. Vibrato On + Depth 0–200 % — LynxTune.cpp (spec §7.6 model: split slow note component / residual FM, scale the
   residual) + two controls in the correction row — engine, M. Needed before Tolerance because a WTRT user's
   quick-start is "Vibrato Off for the hard effect".
2. Correction % (1–100, scales the shift, not a dry mix) — LynxTune.cpp multiply `wanted`; knob next to MIX — engine,
   S (spec §7.7).
3. Tolerance Cents / Time — LynxTune.cpp target hysteresis (50 c + cents, hold time) + two knobs — engine, S/M.
4. Note statuses Up / Down / Bypass on the keyboard (click cycles states) and a multi-octave keyboard with range
   handles that bypass notes outside — params (per-key 0…4 instead of on/off; append, keep old on/off meaning) +
   `lynxTune()` keyboard + PluginEditors.cpp — engine + UI, M/L.
5. Retune range to 800 ms and a second "Speed" knob (in-note, acts only on fast deviations; spec §8.1 to measure
   first) — LynxTune.cpp `retuneMs` (append a new param to keep old states) — engine, M.
6. Reference ±100 c (415–466 Hz) — `referenceHz` law (append a param or remap with translation) — S.
7. Input / output meters (−28…0 dBFS) — `lynxTune()` meter items — UI, S.
8. More scale types (Natural minor named as such, Justonic, Bebop, Whole tone, Diminished, Maqamat …) —
   VocalPitch scale table — engine, S/M; low priority (her states: Major / Natural Minor).

## Other Waves stand-ins (short tables)

**Waves L1 / L2 / L3 / L4 → "Limiter"** (Effects.cpp: Ceiling / Gain / Release, fixed 1.5 ms look-ahead, no panel
of its own — FL "Fruity Limiter" is a different engine)

| Original control | LYNX "Limiter" | Apex Limiter (FabFilter Pro-L role) |
|---|---|---|
| Threshold (L1/L2: 0…−30 dB, drives gain) | Gain 0…+24 dB (inverse of threshold) | Gain |
| Out Ceiling (0…−30 dB) | Ceiling −12…0 dB (default −0.3) | Ceiling (true-peak option) |
| Release (0.01…1000 ms) | Release 10…1000 ms | Release |
| ARC auto release (L2/L3) | — | Style (Clean / Punch / Loud / Safe) ≈ adaptive release |
| Look-ahead (fixed 1.5 ms in L2) | fixed 1.5 ms | Lookahead knob |
| Quantize / Dither type / Noise shaping (L1/L2) | — | — (render-time dither belongs to export) |
| Domain analog/digital (L2), Separation / Profile (L3 multiband) | — | Link (stereo) only |
| Attenuation meter + I/O meters | none | GR trace + meters (display-first editor) |

Recommendation: **map L1 / L2 / L3 / L3-16 / L4 to Apex Limiter**, Threshold → Gain (−threshold), Ceiling →
Ceiling, Release → Release, ARC on → Style "Clean"/"Safe" (adaptive), Lookahead 1.5 ms. L3-LL Multi's 5 bands have
no LYNX multiband limiter (Maximus via FL is the closest; keep Apex as a full-band stand-in). Keep "Limiter" only as
the no-state fallback. Effort S (substitute table row + small translator once the RealWorld lists are read on the
trials).

**Waves RVerb / H-Reverb / TrueVerb → Reeverb 2** (FL's Reeverb 2 clone, `preset Dry=0; Wet=0.8`, slot mix 25 %)

| Original | Key controls | Best LYNX engine | Why |
|---|---|---|---|
| RVerb | Type (Hall / Room / Plate…), Pre-delay, Time, Size, Diffusion, Decay shape, Early ref level, Reverb level, Damping (5-band), EQ (5-band), Dry/Wet | **Atrium** (type / space, early/late, early size, late size, bass/high mult + xovers, predelay, decay, mix) | early/late split, room / hall / plate types and damping multipliers all exist; Reeverb 2 has no type or ER level control beyond "Early ref." |
| H-Reverb | FIR-based hall, Pre-delay, Time, Size, Build-up, Early/late, Damping & EQ, Dynamics (ducking), Modulation, Drive, Output | **Epoch Verb** (predelay, decay, size, attack ≈ build-up, bass mult/xover, high shelf, early/late diffusion, mod rate/depth, colour/mode) | the "Attack"/build-up and colour modes have H-Reverb counterparts; ducking missing everywhere |
| TrueVerb | Room size, Distance (direct/early/late balance), Early reflections, Reverb (time, density, damping), EQ | **Atrium** (room space, early send/size, early/late) | distance = early/late + direct level |

Recommendation: RVerb → Atrium, H-Reverb → Epoch Verb, TrueVerb → Atrium; keep Reeverb 2 only as fallback when no
state is readable. Without a translator, pick a neutral hall preset with Mix = 100 % wet and LYNX slot mix 25 % as
today. Effort S (table) / M (translators after reading the RealWorld lists on the trials).

**Waves Q10 / Renaissance EQ / SSLEQ → Parametric EQ** (FL's Parametric EQ 2 clone, 7 bands)

| Original | Bands / types | Best LYNX engine | Why |
|---|---|---|---|
| Q10 | 1–10 bands (Q1, Q2, Q4, Q6, Q8, Q10 components), each Bell / Shelf / Cut / Notch, gain ±18 dB, Q | **Facet EQ** (8 bands, shape / freq / gain / Q / slope, display-first) — or Parametric EQ when ≤ 7 bands | per-band shapes and slopes; display with the curve; 10-band presets need 2 bands merged or dropped |
| Renaissance EQ (REQ 2 / 4 / 6) | 2/4/6 bands, Bell / Shelf (resonant "Renaissance" shelves) / HP / LP, Gain, Q | **Facet EQ** | 6 bands fit in 8; shelf / cut shapes present |
| SSLEQ (SSL G/E EQ) | HPF, LPF, LF (shelf/bell), LMF (Q), HMF (Q), HF (shelf/bell), E/G curve | **Ridge Strip** (EQ on alone: HPF, LPF, LF/LMF/HMF/HF with bell switches and Q, EQ curve E/G) | identical band structure — the SSL E-Channel already maps there with measured laws (round 113) |

Recommendation: SSLEQ → Ridge Strip with dynamics off (reuse `translateWavesSslChannel`'s EQ part; S), Q10 and REQ
→ Facet EQ (translator per band: type, freq, gain, Q; M). Parametric EQ remains the fallback.

## Epoch Verb ← ValhallaVintageVerb (Valhalla DSP)
Sources: docs/specs/valhalla-vintageverb.md; REA_rodada100.md §1/§7b; product page (valhalladsp.com/shop/reverb/valhalla-vintage-verb/, read from the web.archive.org 2025 snapshot because the live site returns 403); ref ui_ValhallaVintageVerb.png. LYNX: `epochVerbLayout()`, `verbs::info/paramText` (src/core/LynxVerbs.cpp:1258, 1300+), lynx-before/Epoch Verb.png. Trial on this PC: yes (4.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 100 %, equal-power; top left | same law/default, top left | ✅ | |
| PREDELAY | 0–500 ms skew 2.32, 20 ms; under Mix | same | ✅ | |
| DECAY (big knob) | 0.2–70 s skew 3.36, 4.00 s | same, big knob | ✅ | |
| DAMPING HighFreq | 100–20000 Hz, 6000 Hz | same (reads "6.00 kHz") | ✅ | |
| DAMPING HighShelf | −24…0 dB, −24 dB | same | ✅ | |
| DAMPING BassFreq | 100–10000 Hz skew 4.04, 300 Hz | same, caption "Bass freq" | ✅ | |
| DAMPING BassMult | 0.25–4 X, 1.50 X | same | ✅ | |
| SHAPE Size / Attack | 0–100 %, 100 % / 50 % | same | ✅ | Slow-attack build-up shape is not reproduced (round 100). |
| DIFF Early / Late | 0–100 %, 100 % | same, box named "Diffusion" | ✅ | |
| MOD Rate / Depth | 0.1–10 Hz 2.53 Hz / 0–100 % 38 % | same | ✅ | |
| EQ HighCut / LowCut | 100–20000 Hz 8 kHz / 10–1500 Hz 10 Hz, 6 dB/oct | same | ✅ | |
| MODE menu | 22 algorithms, Concert Hall; bottom left | 22 entries in the same order, own names (Nave, Grit…, Drift…, Gated, Grand Hall, Vintage Chamber/Hall); ◀ ▶ on the menu | ⚠️ | Positions match, so translation is exact. Users won't recognise the names (renaming is deliberate). Gated envelope and per-mode early patterns are approximate. |
| COLOR menu | 1970s / 1980s / Now, 1970s | same names/default | ⚠️ | DSP is only a band limit: no reduced internal rate, no "dark and noisy" quantised modulation (spec §7.4, §7.8). |
| PRESETS row | "PRESETS: Default ◀ ▶" bottom right; click name = menu (factory folders, select/copy/save) | header box lists FL presets only → "(no preset)"; ◀ ▶ do nothing for this engine | ❌ | Her corpus is 89 % two factory presets (spec §1). |
| Preset name of the loaded state | shown in the preset field | translated states show "(no preset)" (presetName dropped) | ❌ | |
| Mix lock | click the MIX label to lock Mix while browsing presets (state `mixLock`) | none; `mixLock` not read | ❌ | Only matters once presets exist. |
| Per-control description line | one sentence at the bottom of the window for the hovered control (the "manual") | hint bar shows name + value + gestures only | ❌ | |
| Header info | version / format / demo note top right | title + insert name | ➕/n/a | |
| Resizable UI | yes (state stores uiWidth/uiHeight) | resizable, fixed aspect | ✅ | |
| Value text entry / reset / fine drag | not documented on the product page | right-click entry, double-/Alt-click reset, Ctrl fine, wheel | ➕ | |
| A/B, undo | none in the original | song-level undo only | ✅ | Nothing missing. |
| Tempo sync | none | none | ✅ | |
| Meters / display | none | none | ✅ | |
| Bypass parameter | Bypass (VST param) | slot on/off + options menu Bypass | ✅ | |
| Value readout under each knob, caption above | yes | yes (round 114 `captionsAbove`) | ✅ | |
| Box grouping and order | Mix/Pre · Decay · DAMPING 2×2 · SHAPE · DIFF · MOD · EQ | identical order and grouping | ✅ | |
| Translation of saved state | — | every parameter (class ID + names, `steps` for Mode/Color) | ✅ | mixLock, presetName and ui size are dropped. |

**Scores** — configuration: 7/10, design: 8/10.
**Top gaps**
1. **Factory/user preset browser for LYNX engines.** LYNX would need its own named starting points plus save/copy, and the existing ◀ ▶ should step through them. Code: src/ui/PluginEditors.cpp (`presetFiles()`, header items 2–4), presets stored per EffectKind. UI + data only; shared by all six engines. **M–L**
2. **Show the translated state's preset name.** Keep `presetName` from the XML in the substitute and show it in the header box. Code: PluginSubstitutes.cpp + PluginEditors.cpp `presetName`. UI/data only. **S**
3. **Per-control description line.** Add a `help` text to EffectParamInfo (LynxVerbs.cpp `info`) and show it in the hint bar, or in a footer line inside the panel the way the reference does. Code: PluginEditors.cpp `hintAt`. UI only. **M** (writing about 100 texts across the six engines)
4. **Colour character for 1970s/1980s.** Quantised, low-rate modulation and a half-rate tank for 1970s. Code: LynxVerbs.cpp EpochFx `configure` plus the engine's mod read. Engine/DSP. **M**
5. **Mix lock.** A toggle on the Mix caption, stored in the effect state and read from `mixLock`. Code: PluginEditors.cpp + PluginSubstitutes. UI only, once presets exist. **S**

## Atrium ← ValhallaRoom (Valhalla DSP)
Sources: REA_rodada100.md §2/§7b; product page (valhalladsp.com/shop/reverb/valhalla-room/, archive snapshot); ref ui_ValhallaRoom.png. LYNX: `atriumLayout()`, lynx-before/Atrium.png. Trial on this PC: yes (2.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX fader | 0–100 %, 100 % | vertical fader, 100 % | ✅ | |
| PREDELAY fader | 0–500 ms, 10 ms; fader sits at ~30 % height at 10 ms | 0–500 ms linear, 10 ms sits near the bottom | ⚠️ | The reference fader is drawn with a skewed taper. In LYNX, small predelays are hard to set by drag. |
| DECAY fader | 0.1–100 s (param linear), 2.00 s at ~35 % height | LYNX's own law, 2 s at the centre | ⚠️ | Translation uses a `curve`, but automation recorded on the original won't line up 1:1. |
| LO CUT fader | 0–1000 Hz, 0 Hz | same | ✅ | |
| HI CUT fader | 100–15000 Hz, 8000 Hz, 2nd order | same | ✅ | |
| DEPTH fader (early/late) | 0–100 %, 50 %; caption "DEPTH" | same value, caption "Late mix" | ⚠️ | Wording differs. |
| Fader captions | above the faders | below the faders (values under them) | ⚠️ | Minor. |
| EARLY / LATE tabs | two tabs switch the knob grid (state `earlyLateTabState`) | both groups shown side by side, no tabs | ⚠️ | All controls are visible, but the window is wider (900 px). The `pageTabs` mechanism already exists in PluginLayout. |
| Early Size | 1–1000 ms, 30.0 ms | same | ✅ | |
| Early Cross | 0–1, 0.10 | same | ✅ | |
| Early Mod Rate / Depth | 0.05–5 Hz 0.50 Hz / 0–1 0.00 | same | ✅ | |
| Early Send | 0–1, 0.00 | same | ✅ | |
| Diffusion | 0–1, 1.00 | same | ✅ | |
| Space (2.0) | 0–100 %, 0 %; feedback around predelay + early | same, in Early | ✅ | |
| Late Size / Cross | 0–1, 0.5 / 1.0 | same | ✅ | |
| Late Mod Rate / Depth | 0.05–5 Hz / 0–1 | same | ✅ | |
| Bass Mult / Xover | 0.5–2 X @ 100–10000 Hz, 1 X @ 1 kHz | same | ✅ | |
| High Mult / Xover | 0.1–1 X @ 100–15000 Hz, 0.5 X @ 8 kHz | same | ✅ | |
| REVERB MODE menu | 12 types, Large Room | 12 in the same order; last four renamed (Deep/Mirror/Vast/Distant Space) | ⚠️ | Dark types are band-limited only to −20 dB at 16 kHz (reference −60) and there is a 250 Hz dip (round 100 residuals). |
| PRESET row | "PRESET: Default ◀ ▶" (factory folders incl. "Space" folder, save/copy) | none for LYNX engines | ❌ | |
| GUI Mode (colour schemes Electric Blue / Old School) | top-right selector | LYNX theme only | n/a | Trade dress; do not copy. |
| Description line | bottom of the window | hint bar name/value only | ❌ | |
| Mix lock | state `mixLock` (MIX label is highlighted) | none | ❌ | |
| Resizable | "Resizable GUI" | yes, fixed aspect | ✅ | |
| Gestures / text entry | not documented | LYNX's full set | ➕ | |
| True-stereo routing | stereo in/out | stereo | ✅ | |
| Translation | — | every parameter; decay through `curve` | ✅ | earlyLateTabState, mixLock and presetName are dropped. |

**Scores** — configuration: 7/10, design: 6/10.
**Top gaps**
1. **Early/Late page tabs.** Put the two groups on `page` 0/1 with `pageTabs`, and optionally restore the page from `earlyLateTabState`. Code: PluginLayout.cpp `atriumLayout()`. UI only. **S**
2. **Preset browser and translated preset name.** Same work as Epoch gaps 1–2. **M–L / S**
3. **Display taper on the Predelay/Decay faders**, so the default and small values sit where they do on the original. Use a slider skew in the layout item while keeping the parameter law. Code: PluginLayout.cpp / PluginEditors.cpp slider mapping. UI only. **S**
4. **Fader captions above and "Depth" wording.** Code: `atriumLayout()`. UI only. **S**
5. **Description line.** Same as Epoch gap 3. **M**
6. **Dark-type band limit and the 250 Hz dip.** Code: `atriumTypes` table in LynxVerbs.cpp. Engine tuning. **S–M**

## Foil Plate ← ValhallaPlate (Valhalla DSP)
Sources: REA_rodada100.md §3/§7b; product page (valhalladsp.com/shop/reverb/valhalla-plate/, archive snapshot); ref ui_ValhallaPlate.png. LYNX: `foilPlateLayout()`, lynx-before/Foil Plate.png. Trial on this PC: yes (1.6.3 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 100 % | same | ✅ | |
| PREDELAY | 0–500 ms skew, 0.0 ms | same | ✅ | |
| DECAY (very large knob) | 0.5–30 s piecewise, 3.0 s | same law, big knob | ✅ | |
| SIZE | 0–200 %, 100 % | same | ✅ | |
| WIDTH | 0–200 % (0 = mono, 200 = anti-correlated), 100 % | same | ✅ | |
| EQ LowFreq | 20–2000 Hz, **700 Hz** | default 0.348774 → **711 Hz** | ⚠️ | Default is off by about 1.5 %. Should be 0.3434. |
| EQ HighFreq | 200–20000 Hz, **10000 Hz** | default 0.5 → **10.10 kHz** | ⚠️ | Should be 0.4949. |
| EQ LowGain / HighGain | ±12 dB, 0 dB | same | ✅ | |
| MOD Rate | 0.05–5 Hz skew 2.38, 0.50 Hz | same | ✅ | |
| MOD Depth | 0–100 %, 0 % | same | ✅ | |
| MODE menu | 12 plate types, Chrome | 12 in the same order, own metal names; ◀ ▶ | ⚠️ | Positions match. Per-type onsets were fitted. Mono-in types (Adamantium/Titanium/Osmium) are not modelled as mono-in. |
| PRESETS row | "PRESETS: Default ◀ ▶" | none | ❌ | |
| Description line | bottom | hint bar name/value | ❌ | |
| Mix lock | state `mixLock` | none | ❌ | |
| Resizable | yes (uiWidth/uiHeight in state) | yes | ✅ | |
| Layout order | Mix/Pre · Decay · Size/Width · EQ 2×2 · MOD · Mode | identical | ✅ | |
| Value readouts / captions | above/below | same | ✅ | |
| Gestures | not documented | full set | ➕ | |
| Translation | — | every parameter (Type `steps`) | ✅ | |

**Scores** — configuration: 8/10, design: 9/10.
**Top gaps**
1. **Preset browser and preset name.** Same work as Epoch. **M–L / S**
2. **Fix the EQ frequency defaults** to 700 Hz and 10 kHz (0.343434 / 0.494949). Code: LynxVerbs.cpp `foilInfo`. Data only. **S**
3. **Description line.** **M**
4. **Mono-in behaviour for the three mono-in types.** Code: FoilFx `configure` (sum the input). Engine. **S**

## Nebula ← Valhalla Supermassive (Valhalla DSP)
Sources: REA_rodada100.md §4/§7b; product page with its control tooltips (valhalladsp.com/shop/reverb/valhalla-supermassive/, archive snapshot); ref ui_ValhallaSupermassive.png. LYNX: `nebulaLayout()`, lynx-before/Nebula.png. Trial on this PC: yes (5.0.0, free).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX | 0–100 %, 50 % | same | ✅ | |
| MIX lock | click the boxed MIX label = lock/unlock (documented) | none | ❌ | Visible affordance on the reference. |
| WIDTH | −100…+100 % (negative swaps L/R), 100 % | same | ✅ | |
| DELAY (big) | 0–2000 ms piecewise, 300.0 ms | same | ✅ | |
| Delay sync combo | Msec / Note / Dotted / Triplet as a combo under Delay; note values 1/64…4/4 (7) | 4 tabs + a separate always-visible note menu (1/64…1/1) | ⚠️ | Same options. In the original the sync setting changes what the Delay knob sets; LYNX shows both controls at once. |
| WARP (big) | 0–100 %, 0 % | same | ✅ | |
| CLEAR button | under Warp; flushes the delay network | missing | ❌ | The parameter is not translated either (it is a button). |
| FEEDBACK | 0–100 %, 50 % (gain per pass = √fb) | same | ✅ | |
| DENSITY | 0–100 %, 0 % | same | ✅ | |
| MOD Rate / Depth | 0.01–10 Hz skew 2.33, 0.50 Hz / 50 % | same | ✅ | |
| EQ High / Low | 200–20000 Hz 20 kHz / 10–2000 Hz 10 Hz, 6 dB/oct output | same | ✅ | |
| MODE menu + ◀ ▶ (5.0) | 22 modes, Gemini | 22 in the same order, own names; ◀ ▶ | ⚠️ | Density-50 % decay and ×8 shapes at long delays are approximate. |
| PRESET row | "PRESET: Default ◀ ▶"; click = select/copy/save | none | ❌ | |
| Description line | bottom left | hint bar name/value | ❌ | |
| Resizable | yes (uiWidth/uiHeight) | yes | ✅ | |
| Layout | Mix/Width · Delay+Warp box with sync/Clear · Fb/Density · MOD · EQ | same order | ✅ | Clear is missing. |
| Gestures | not documented | full set | ➕ | |
| Translation | — | every parameter except Clear; Sync/Note/Mode `steps` | ✅ | |

**Scores** — configuration: 7/10, design: 8/10.
**Top gaps**
1. **Clear button.** A momentary button in `nebulaLayout()` that calls the effect's buffer flush (`VerbEffect` already has reset/clear). Code: PluginLayout.cpp + PluginEditors.cpp action + LynxVerbs.cpp. Small engine hook. **S**
2. **Mix lock on the MIX caption.** **S**
3. **Preset browser and preset name.** **M–L / S**
4. **Sync as a single combo**, with the Delay knob showing and setting note values when not in Msec. The Note menu would then be hidden or merged. Code: `nebulaLayout()` + paramText. UI only (no parameter-conditional visibility exists in Layout yet). **S–M**
5. **Description line.** **M**

## Halo ← ValhallaShimmer (Valhalla DSP)
Sources: REA_rodada100.md §5/§7b; product page with mode descriptions (valhalladsp.com/shop/reverb/valhalla-shimmer/, archive snapshot); ref ui_ValhallaShimmer.png. LYNX: `haloLayout()`, HaloFx (LynxVerbs.cpp:997–1040), lynx-before/Halo.png. Trial on this PC: yes (1.2.2 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Row of 9 knobs | mix, shift, feedback, diffusion, size, low cut, high cut, mod rate, mod depth | same order, one row | ✅ | |
| mix | shown as 0.50 (raw 0–1) | 50 % | ➕ | LYNX shows units. |
| shift | ±12 st, shown 12.00 | +12.0 st | ✅ | |
| feedback | 0–1, 0.000 | 0 % | ✅ | |
| diffusion | 0–1, 0.910 | 92 % (0.919) | ⚠️ | Tiny default difference. |
| size | 0–1, 0.50 (sets decay, about 1.5 + 12.5·size s) | 50 % | ✅ | No seconds readout in either. |
| low cut | starts at 10 Hz, default 10 | display 10–1000 Hz, but the **DSP uses 250·v Hz** | ⚠️ | **Display and DSP disagree** (bug: v = 1 reads 1000 Hz, filters at 250 Hz). |
| high cut | 100–15000 Hz, 8000 | same | ✅ | |
| mod rate | default shown 0.59 | 0.55 Hz (law not confirmed) | ⚠️ | |
| mod depth | 0.30 | 30 % | ✅ | |
| reverb mode | **4: Mono / Small Stereo / Medium Stereo / Big Stereo**, default mono (mono-in for Mono) | **3: Wide / Dense / Bright**, default Wide; flat button (no ◀ ▶) | ❌ | The translation collapses 4 states into 3 groups. |
| pitch mode | **Single (up or down by Shift) / Dual / SingleReverse / DualReverse / Bypass**, default single | Up / Up + down / Up, bright / Up + down, bright / No shift | ⚠️ | "Up" is misleading: Single follows the Shift sign. The reverse-grain modes are approximated by a +4 dB shelf. Bypass ✅ (feedback still lengthens the decay). The caption is "Shift mode" where the original says "pitch mode". |
| color mode | Bright / Dark (Dark = steep loss above ~10 kHz) | Bright / Dark (5 kHz loop band limit) | ✅/⚠️ | Dark is darker than described. |
| Programs | 8 host programs (Program param, `currentProgram`) | none | ❌ | |
| Description line | none visible in 1.2.2's UI (the page says tooltips) | hint bar | ✅ | |
| Resizable | not in 1.2.2 (fixed small UI) | resizable | ➕ | |
| Gestures | not documented | full set | ➕ | |
| Translation | — | parameter0–11; modes through `steps` (Reverb mode 3 groups, Shift mode 5, Color 2) | ⚠️ | Mode lists were "not confirmed" in round 100. The product page now confirms 4 reverb modes and 5 pitch modes. |

**Scores** — configuration: 6/10, design: 8/10.
**Top gaps**
1. **Four reverb modes in the original's order** (mono-in, then three stereo sizes; LYNX's own names, e.g. "Mono / Small / Medium / Large"). Re-measure the breakpoints (likely k/4) and update the `steps` map. Code: LynxVerbs.cpp `haloModes`, `haloLayout()`, PluginSubstitutes.cpp `[valhalla-shimmer]`. Engine + data. **M**
2. **Pitch modes that match the original's behaviour.** Single (sign of Shift), Dual, reversed-grain Single/Dual (reverse each grain before shifting in the two-head shifter), Bypass. Code: HaloFx `configure` + shifter. Engine/DSP. **M**
3. **Fix the low-cut display/DSP mismatch** so both use the same law. Code: HaloFx `configure` line `s.outLowCut = 250.0 * p[5]` vs `paramText` case 5. **S**
4. **Mode menus with ◀ ▶** (the Reverb mode button is flat), and caption "Pitch mode". Code: `haloLayout()`. UI only. **S**
5. **Programs / presets.** Same as the shared preset work. **M–L**

## Horizon ← ValhallaFutureVerb (Valhalla DSP)
Sources: REA_rodada100.md §6/§7b; product page and mode descriptions (valhalladsp.com/shop/reverb/valhallafutureverb/, archive snapshot); user-manual PDF found but not readable here (equipboard copy, no PDF tool installed); ref ui_ValhallaFutureVerb.png. LYNX: `horizonLayout()`, HorizonFx (LynxVerbs.cpp:1053–1230), lynx-before/Horizon.png. Trial on this PC: yes (1.0.2 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| MIX / WIDTH | 0–100 % 50 % / width 100 % | same | ✅ | |
| Mix lock | boxed MIX label (lock) | none | ❌ | |
| ECHO Delay (big) | 0–2000 ms piecewise, 30.0 ms | same | ✅ | |
| Echo sync | "Msec ⇕" combo under Delay (ms / note / dotted / triplet) | tabs + note menu (1/64…1/2, 6 values) | ⚠️ | Note list not confirmed against the original. |
| Feedback / Level | 0–100 %, 0 % / 0 % | same | ✅ | |
| Drive | 0–24 dB, 3.0 dB | same | ✅ | |
| Spread | 0–100 %, 0 % | same | ✅ | |
| Detune | ±30 ct; **shown only in echo modes that use it** | always shown (small knob, +4.0 ct) | ⚠️ | |
| Mode-dependent visibility | "Only the parameters that are active in a given ECHO MODE will be visible" | all always visible | ❌ | Layout has `page` but no parameter-conditional visibility. |
| REVERB Decay (big) | 0.2–70 s piecewise, 3.00 s | same | ✅ | |
| Echo→Rev routing | toggle button under Decay | 2 tabs under Decay | ✅ | |
| Size / Level | 50 % / 100 % | same | ✅ | |
| Early/Late / Density | 50 % / 100 % | same | ✅ | |
| Attack | exists; not visible in Chamber (mode-dependent) | always shown, small | ⚠️ | |
| MOD Rate / Depth | 0.01–10 Hz 0.10 Hz / 40 % | same | ✅ | |
| EQ HighCut / LowCut | 1000–20000 Hz 4500 Hz / 20–1000 Hz 100 Hz; they also set the high/low **decay** (loop) | same ranges/defaults; output filters only | ⚠️ | Missing the in-loop RT part (measured in round 100; the vendor says Low Freq EQ controls the bass decay of Chamber/Plate/Cathedral). |
| ECHO MODE | 12: Modern, Tape, Digital, Analog, Detune, Reverse, ReverseOctUp/Down/UpDown, Sparkle, Swarm, LoFi; default Tape | 12 in the same order (Clean…Lo-fi), default Tape, ◀ ▶ | ⚠️ | Behaviour is LYNX's interpretation (not measured): Digital 12-bit companding, Reverse 2–8 grains by Spread, Sparkle long grains. |
| REVERB MODE | 8: Room, Chamber, Plate, Hall, Cathedral, Space, Frozen, Nonlin; default Chamber | 8 in the same order (Freeze, Gated), default Chamber | ✅ | |
| COLOR | Bright / Neutral (HF RT capped 1–1.5 s) / Dark (steep 8 kHz) / Studio (steep 10 kHz + steep 600 Hz low cut); default Neutral | Bright / Neutral (16 kHz cut) / Dark (11 kHz) / "Vintage" (9 kHz + 250 Hz, gentle) | ⚠️ | The cut frequencies and slopes differ from the documented ones. |
| Preset bar | top centre "◀ Default Preset ▶" | none | ❌ | |
| Description line | bottom | hint bar name/value | ❌ | |
| Resizable | yes (uiWidth/uiHeight) | yes | ✅ | |
| Gestures | not documented | full set | ➕ | |
| Layout | Mix/Width · ECHO box · REVERB box · MOD · EQ · mode row | same order | ✅ | |
| Translation | — | every parameter (modes `steps`, routing) | ✅ | |

**Scores** — configuration: 6/10, design: 7/10.
**Top gaps**
1. **Preset bar.** Same as the shared preset work. **M–L**
2. **Colour modes to the documented values**: Dark = steep 8 kHz; the 4th colour = steep 10 kHz plus steep 600 Hz high-pass; Neutral = an HF RT cap (≈ 1–1.5 s) instead of an output cut. Code: LynxVerbs.cpp `horizonColourCut` / HorizonFx `configure`. Engine. **S**
3. **EQ in the loop.** Low/High EQ should also scale the low- and high-band RT. Code: HorizonFx `configure` (the absorption bands already exist). Engine. **S–M**
4. **Echo-mode / reverb-mode dependent visibility** (Detune, Attack, Drive…). Needs a `visibleWhen param/value` field in Layout items. Code: PluginLayout.h/.cpp + PluginEditors.cpp. UI framework change. **M**
5. **Mix lock, description line.** **S / M**

---
**Cross-cutting findings (all six):**
- **Shared gaps.** No preset browser or preset name, no mix lock, and no per-control description line. These are the first things a Valhalla user touches. A/B, undo, meters and oversampling are absent in the originals too, so they are not gaps.
- **Measured-value checks.** Every measured parameter law and almost every default is identical. The only defaults that are off are Foil Plate's EQ frequencies (711 Hz vs 700 Hz, 10.1 kHz vs 10 kHz).
- **The real configuration differences:**
  - Halo's mode lists (Shimmer has 4 reverb modes and reverse-grain pitch modes).
  - Halo's low-cut display/DSP bug.
  - Horizon's colour definitions and loop EQ.
  - Nebula's missing Clear button.
- **Translation.** Every saved value maps to the substitute, but `mixLock`, `presetName`, ui size, Room's tab state and Shimmer's program are dropped. Shimmer's 4 reverb modes collapse into 3.
- **Web access.** The valhalladsp.com pages return a Cloudflare 403, so they were read from web.archive.org snapshots via curl.

Sources: [VintageVerb](https://valhalladsp.com/shop/reverb/valhalla-vintage-verb/), [Supermassive](https://valhalladsp.com/shop/reverb/valhalla-supermassive/), [Room](https://valhalladsp.com/shop/reverb/valhalla-room/), [Plate](https://valhalladsp.com/shop/reverb/valhalla-plate/), [Shimmer](https://valhalladsp.com/shop/reverb/valhalla-shimmer/), [FutureVerb](https://valhalladsp.com/shop/reverb/valhallafutureverb/), [FutureVerb Reverb Modes](https://valhalladsp.com/2025/10/03/valhallafutureverb-the-reverb-modes/), [FutureVerb manual PDF](https://images.equipboard.com/uploads/item/manual/159855/valhalla-futureverb-manual.pdf) (downloaded, not parsed).

## Echoform ← ValhallaDelay (Valhalla DSP)
**Sources:** docs/analise/REA_rodada101.md §1–3 and §8 (our black-box measurements); [ValhallaDelay: The Controls](https://valhalladsp.com/2019/04/16/valhalladelay-the-controls/), [The STYLE Control](https://valhalladsp.com/2019/04/16/valhalladelay-the-style-control/), [1.5.2 update: three new modes and ducking](https://valhalladsp.com/2019/11/26/valhalladelay-update-three-new-modes-and-ducking/) (the pages were read through archive.org, because the live site returns 403); reference screenshots `valhd101/ref/delay*.png` (Single, Quad, Ratio, Dual, Note, Digital, Ghost, Pitch); LYNX `lynx-before/Echoform.png`; `echoformLayout()` and `echosuite::echoformInfo()` / `paramText()`. **Trial on this PC:** yes (3.0.5 demo).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, equal-power law; top of the left box | Mix 0–100 %, 50 %, equal-power, same place | ✅ | |
| Mix lock | Click the "MIX" label to lock Mix while browsing presets | — | ❌ | Only useful once presets exist |
| Style | Single / Dual / Ratio / PingPong / Quad knob under Mix, default Single | Style knob with 5 steps, same place; order Single / Ratio / Quad / Dual / Ping-pong; the substitute reorders | ✅ | The knob order is different, so turning it gives a different sequence than the original |
| Style changes which controls are shown | Spread / Ratio / Spacing / Delay R swap in place | Same swaps (`when` items) | ✅ | |
| Delay time | 0–20 000 ms, piecewise law, default 300 ms, big knob | Same law and default | ✅ | |
| Delay label L / R in Dual and PingPong | Captions read "DELAY L" and "DELAY R" | Captions read "Delay" and "Delay R" | ⚠️ | Left caption doesn't change |
| Sync combo under the time knob | Msec / Note / Dotted / Triplet; "DELAY Sync" becomes L and R in Dual / PingPong | Same 4 choices, same place, R menu only in Dual / Ping-pong | ✅ | |
| Note value | 1/64 … 8/1 (10 values); the knob turns into a note knob | Same 10 values; note knob replaces the time knob | ✅ | |
| Spread (Single) | ±20 ms, 0 ms | Same, ±¼ applied per side as measured | ✅ | |
| Ratio (Ratio) | 1–100 %, 61.8 % | Same | ✅ | |
| Spacing (Quad) | ±100 %, 0 % | Same | ✅ | |
| REPEAT / SWELL button (Quad) | Feedback from TapD (Repeat) or from all active taps (Swell) | — (always takes feedback from the last tap) | ❌ | Needs an engine change |
| TapA / B / C / D on/off (Quad) | 4 buttons under the time knob, greyed out in the other styles | — (all 4 taps always on) | ❌ | Needs an engine change |
| Feedback | 0–200 %, 70 % | Same | ✅ | |
| Width | ±100 %, 100 % | Same | ✅ | |
| Color: Drive | 0–24 dB, 0 dB | Same | ✅ | |
| Color: Age | 0–100 %, 50 %; meaning depends on mode (asperity, splice, BBD noise, bit depth) | Age 50 %, per-character meaning | ⚠️ | Not every mode-specific Age effect is modelled (e.g. tape splices, bit-depth Age in Pitch) |
| Diff Amount | 0–100 %, 0 = OFF | Same, shows "off" | ✅ | |
| Diff Size | 0–100 %, 100 % | Same | ✅ | |
| Mod: Wow / Flutter (tape modes) | 0–100 %, 50 / 50 | Same, shown in Tape only | ✅ | Original also shows them in DuckTape and ChromeTape; LYNX maps both to Tape |
| Mod: Rate / Depth (HiFi / BBD / Digital) | 0.01–10 Hz, 0.50 Hz; 0–100 %, 50 % | Same values, shown for Lo-Fi … Phaser | ✅ | |
| Section title changes to FREQ (Ghost) / PITCH (Pitch) | The section header is renamed | Header always reads "Mod" | ⚠️ | UI only |
| Freq Shift / Detune (Ghost) | ±1000 Hz (\|x\|^5), 0 Hz; ±10 Hz, 0.10 Hz | Same laws and defaults (Shifter mode) | ✅ | |
| Pitch Shift / Detune (Pitch / RevPitch) | ±24 st, 0; ±100 c, 0 | Same (Pitch, Reverse) | ✅ | |
| EQ High / Low | 200–20 000 Hz, 20 kHz; 10–2000 Hz, 10 Hz; in the feedback path | Same, High above Low | ✅ | |
| Ducking | 0–100 % one-knob ducking (feedback + output); comes with the duck modes | Ducking knob in an "Output" section, active in every mode, simple envelope curve | ⚠️ | Placement and behaviour not confirmed against the original; curve not measured |
| Mode list | 17 modes: Tape, HiFi, BBD, Digital, Ghost, Pitch, RevPitch, BBDuck, Clarity, DuckTape, PitchDuck, LoFi, Quartz, PhaserDDL, RichPitch, ChromeTape, Analog | 12 characters | ⚠️ | BBDuck → bucket (loses brightness that follows the delay time), Clarity / Quartz → Clean (loses Clarity's limiter and Era waveforms), DuckTape / ChromeTape → Tape, PitchDuck → Pitch; built-in ducking lost |
| Mode selector with ◀ ▶ | Bottom left, "MODE: Tape ◀▶" | Bottom left menu with ◀ ▶ | ✅ | |
| Era | Past / Present / Future, ◀ ▶, bottom middle; changes filters, saturation, diffusion and Clarity's waveforms | — | ❌ | Needs a new parameter and per-mode voicing |
| Colour theme follows the mode | The whole panel recolours per mode family (green tape, blue digital, grey Ghost, navy Pitch) | Fixed theme | ⚠️ | Design cue only; a LYNX accent per mode family would do, without copying the original's colours |
| Preset browser | "PRESET: Default ◀▶" bottom right, large factory library | Header box shows "(no preset)" | ❌ | Applies to all LYNX engines |
| Description line | Bottom strip describes the current Style / Mode, and the hovered control | Hint only in the main window's hint bar | ❌ | UI only |
| Text entry / reset gestures | Not confirmed in our refs | Right-click value entry, double-click / Alt-click default, Ctrl fine drag, wheel | ✅ | |
| Resizable window | Resize corner; uiWidth / uiHeight saved in the state | Resizable, fixed aspect ratio | ✅ | uiWidth / uiHeight not translated (harmless) |
| Output level | — | Level ±18 dB | ➕ | |
| Ping-pong "Pairs" toggle | — | Pairs / alternate | ➕ | |
| Echo-train display | — | Echoes on a time axis | ➕ | |
| Saved-state translation | — | Translates everything except Era, TapA–D, RepeatSwell; 17 modes → 12 characters; duck variants lose built-in ducking | ⚠️ | `PluginSubstitutes.cpp [valhalla-delay]` |

**Scores:** configuration 6/10, design 6/10.

**Top gaps:**
1. **Era selector** (bottom row, between Mode and Preset). Needs a new `Era` parameter in `echoformInfo()`, per-character voicing in `Echoform::configure()` and a map line for `xml:Era`. Engine change, L.
2. **Quad TapA–D and Repeat/Swell buttons** under the time knob, shown only in Quad. Needs 5 new parameters, a tap mask in the quad read loop and Swell feedback (sum of active taps) in `EchoSuite.cpp` around lines 565 and 618. Also map TapA–D and RepeatSwell. Engine change, M.
3. **Description strip** under the bottom row: a one-line text for the current style and mode, plus the hovered control's hint. Add it in `PluginEditors.cpp`; it could become a generic "info line" layout item. UI only, S–M.
4. **Missing modes**: give BBDuck, Clarity (limiter), DuckTape / PitchDuck (built-in ducking) and Quartz their own characters, and update the map's `steps`. Engine change, M–L.
5. **Ducking**: show it only in the duck characters, or keep it global but measure the original's curve. Engine plus layout, M.
6. **Section title follows the mode** ("Freq" / "Pitch"), and the left delay caption reads "Delay L" in Dual / Ping-pong. Add a `when` on section labels in `echoformLayout()`. UI only, S.
7. **Preset browser plus Mix lock**: needs a LYNX factory / user preset system (affects all engines). L.

---

## Spiral Echo ← ValhallaFreqEcho (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §4; [FreqEcho product page](https://valhalladsp.com/shop/delay/valhalla-freq-echo/) (archive.org); `ref/FreqEcho.png`; `lynx-before/Spiral Echo.png`; `spiralLayout()` and `spiralInfo()`. **Trial on this PC:** yes (1.2.0).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, linear law; first knob | Same | ✅ | |
| Delay | 0.01–1000 ms (skew 3.322); screenshot shows **0.01 ms** (knob at minimum) | Same law; default **100 ms** (v = 0.5) | ⚠️ | Default differs |
| Shift | ±1000 Hz (\|x\|^5), 0 Hz; big centre knob | Same, big centre knob | ✅ | |
| Feedback | 0–100 % (loop gain 1.5×), 50 % | Same | ✅ | |
| Low Cut | 20–3000 Hz, 200 Hz | Same | ✅ | |
| High Cut | 50–15 000 Hz, 5000 Hz | Same | ✅ | |
| Delay Sync | Free plus 23 notes (straight / dotted / triplet), under Mix / Delay | Same 24 entries, same place | ✅ | |
| Output Mode | Mono / Stereo, Stereo default, bottom right | Same | ✅ | |
| Per-pass shift, opposite shift in stereo | — | Matches the measurement | ✅ | |
| Presets | None | None | ✅ | |
| Tooltip / help line | Product page says tooltips show bottom left; none visible in the screenshot | Main-window hint bar | ⚠️ | |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | All 8 settings | ✅ | |

**Scores:** configuration 9/10, design 9/10.

**Top gaps:**
1. **Delay default**: set `{ "Delay", 0.0f }` in `spiralInfo()` if the 0.01 ms default is confirmed on a fresh instance (the screenshot suggests it). Parameter default only, S.
2. Optional: a small spiral / spectrum display. Not needed for parity.

---

## Orbit ← ValhallaSpaceModulator (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §5; [SpaceModulator product page](https://valhalladsp.com/shop/modulation/valhalla-space-modulator/) (archive.org); `ref/SpaceModulator.png`; `lynx-before/Orbit.png`; `orbitLayout()` and `orbitInfo()`. **Trial on this PC:** yes (1.1.6).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Mix | 0–100 %, 50 %, equal power | Same | ✅ | |
| Rate | ±10 Hz (\|x\|^3.333), 0.10 Hz | Same (+0.10 Hz) | ✅ | |
| Depth | 0–50 ms (skew 2.322), 10 ms | Same | ✅ | |
| Feedback | ±100 %, 0 % | Same | ✅ | |
| Manual | ±50 ms (\|x\|^4), 0 ms | Same | ✅ | |
| Five knobs in a row, each its own colour | Mix, Rate, Depth, Feedback, Manual | Same order, each knob its own LYNX colour | ✅ | |
| Mode | 11 modes (Up, Up/Down, TZFlange±, TriFlange, Ocho, Doubler, VariUp, VariUpDown, Ensemble360, Symphonic), clickable text "Mode: Up" bottom left | 11 modes renamed (Rise …), menu with ◀ ▶ bottom left | ✅ | |
| Mode voices | Measured tap ranges per mode | Own designs within the measured ranges | ⚠️ | Round 101 marks finer captures as still to do |
| Preset browser | "PRESETS: Default ◀▶" bottom middle / right | "(no preset)" | ❌ | |
| Description line | Bottom strip, e.g. a one-line summary of the current mode | — | ❌ | UI only |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures / text entry | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | All 6 settings | ✅ | |

**Scores:** configuration 7/10, design 7/10.

**Top gaps:**
1. **Mode description line** under the mode row, one sentence per mode. Same info-line item as Echoform. UI only, S.
2. **Factory presets** with ◀ ▶ next to the mode. Needs the shared preset system, L.
3. **Voice shapes per mode** checked against finer captures (the round 101 follow-up), in `orbit::Orbit::process`. Engine change, M.

---

## Lattice Mod ← ValhallaUberMod (Valhalla DSP)
**Sources:** REA_rodada101 §2 and §6; [UberMod: The Modes](https://valhalladsp.com/2012/01/06/valhallaubermod-the-modes/), [The TAPS Parameters](https://valhalladsp.com/2012/01/26/valhallaubermod-the-taps-parameters/), [The DIFF Parameters](https://valhalladsp.com/2012/01/27/valhallaubermod-the-diff-parameters/) (archive.org); `ref/UberMod.png` and `uber_*.png`; `lynx-before/Lattice Mod*.png`; `latticeLayout()`, `latticeInfo()` and `lattice::tapCount()`. **Trial on this PC:** yes (1.1.6).

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Faders Mix / Depth / Width / Delay / Feedback, values under them | 50 % / 50 % / 100 % (0–200) / 10 ms (0–1000) / 0 % | Same faders, values and defaults | ✅ | |
| **MODE** menu (under the faders) | Named algorithms: 2TapChorus (default), 4TapEnsemble, SuperSix, 6TapRandom, DualEnsemble, 8Tap, 16Phase, 16Tap, 32Tap; each has its own LFO design (antiphase triangle, 3-phase, randomised triangle, 16-phase) | "Taps" menu with 1 / 2 / 3 / 4 / 8 / 16 (taps per channel); default **4** (= 8Tap) | ❌ | Wrong default; SuperSix, 6TapRandom and DualEnsemble all collapse to "3"; 16Phase and 16Tap collapse to "8"; one random-detune plus one vibrato LFO for every mode |
| Sync | Free plus 17 note values, next to the mode | 18-entry list (Free, 1/64 … 1/1), next to Taps | ⚠️ | List order is a guess; the substitute doesn't translate `DelaySync` |
| Page tabs | MOD / TAPS / DIFF / EQ / DRIVE / WARP, top right | Same 6 tabs, same order | ✅ | |
| MOD page | SlowRate 0.50 Hz, SlowDepth 50 %, VibRate 6.00 Hz, VibDepth 0 %, OverMod 1.0× | Same 5 knobs and defaults | ✅ | |
| TAPS page | Spread 50 %, Slope 0, Skew 0, Random 0, TapGain 0 dB | Same | ✅ | Smoothing of tap-time changes (Warp Smoothing) should be checked |
| DIFF page | On/Off (OFF), Diffusion 50 %, Size 20 ms, ModRate 0.50 Hz, ModDepth 50 % | Same | ✅ | |
| EQ page | Mode Bright / Dark, Low Cut 10 Hz, High Cut 20 kHz, SpatialEQ 300 Hz | Same | ✅ | |
| DRIVE page | On/Off, InGain 9 dB, OutGain −6 dB, NoiseGain −90 dB, Pre/Post (POST) | Same | ✅ | |
| WARP page: Speed, fbMix, Smoothing | 1X / ½X, 50 %, 100 ms | Same | ✅ | |
| WARP page: InputPan | 0–8 steps, shows "Stereo" | — | ❌ | Not in the engine; substitute drops it |
| 16-tap alternating polarity / type ≥ 10 | Measured | Not modelled | ⚠️ | |
| Preset browser | "PRESET: Default ◀▶" next to the page area | "(no preset)" | ❌ | |
| Description / tooltip line | Bottom strip shows hover help (e.g. the OverMod warning) | Main-window hint bar | ❌ | UI only |
| Tap display | — | Tap graph under the pages | ➕ | |
| Resizable | Fixed size | Resizable | ➕ | |
| Gestures / text entry | Not confirmed | Full LYNX set | ✅ | |
| Saved-state translation | — | Everything except DelaySync and InputPan | ⚠️ | `PluginSubstitutes.cpp [valhalla-ubermod]` |

**Scores:** configuration 6/10, design 7/10.

**Top gaps:**
1. **Named Mode menu with the correct default**: replace "Taps" with 9 LYNX-named algorithms using the measured `type` index (2TapChorus = index 1), and set the default to index 1 (1.5/24) instead of 6.5/24. Changes `latticeLayout()`, `latticeInfo()` and `tapCount()`. UI plus default, S.
2. **LFO structure per mode** (antiphase triangle, 3-phase, randomised triangle, 16-phase) in `LatticeMod::process`, so SuperSix, 6TapRandom, DualEnsemble and 16Phase sound different from each other. Engine change, M–L.
3. **Sync translation**: measure the 18 sync steps, confirm the list, then add `map = xml:DelaySync -> Sync`. Engine plus map, S.
4. **InputPan** on the WARP page: new parameter, input routing and map line. Engine change, M (meaning to be measured first).
5. **Description line plus presets**: the shared items described in the other plugins' gaps. S / L.

---

**Common to all four:**
- **No presets or Mix lock.** The original Delay, SpaceModulator and UberMod all ship factory presets; FreqEcho has none.
- **No in-panel help line.** Valhalla's plugins "self-document" through tooltips at the bottom of the panel; LYNX only shows hints in the main window's hint bar.

A single "info line" layout item in `PluginEditors.cpp` would close the help-line gap for all four in one change.

## Ember Saturator ← Decapitator (Soundtoys)
Sources: docs/specs/decapitator.md (vendor guide v5 pp. 4–9, reviews); `mixsuite::ember` in src/core/MixSuite.{h,cpp} (`emberInfo`, `Saturator::chunk`); `EmberEditor` in src/ui/ProEditors.cpp; ProEditor header in src/ui/ProEditorBase.h; screenshot lynx-before/Ember Saturator.png. Trial on this PC: no. **Manual only (trial not installed).** I judged only Decapitator-workflow fit. Saturn parity is out of scope.

| Original feature | Original (range / default / where) | LYNX now | Status | Note |
|---|---|---|---|---|
| Drive | 0…10 scale, big centre knob | Drive 0…+36 dB, big knob in band panel + drag handle on spectrum; default 0 | ⚠️ | dB readout, not 0–10; taper not matched to Decap (never measured) |
| Punish | +20 dB pre-gain switch | — | ❌ | Ember's 36 dB drive covers the gain but there is no one-click switch, and a 0–10 + Punish law is missing |
| Style A (Ampex 350 preamp) | button | Tube (biased tanh) | ⚠️ | 4 generic styles, not 5 named voicings |
| Style E (EMI TG) | button | — | ❌ | |
| Style N (Neve 1057, "FuzzFace") | button | Transistor / Fuzz (nearest) | ⚠️ | |
| Style T (triode, even harmonics) | button | Tube (asymmetric → even) | ⚠️ | closest match |
| Style P (pentode, odd harmonics) | button | Tape (x/√(1+x²), odd) | ⚠️ | |
| Style selector form | 5 adjacent letter buttons, one click | dropdown "Tube/Tape/Transistor/Fuzz" | ⚠️ | needs 2 clicks; styles cannot be A/B'd by ear quickly |
| Per-style EQ / dynamic response | built into each style | Tape only: ±6 dB 3 kHz pre/de-emphasis | ⚠️ | no style-specific pre/post EQ or bias sag |
| Low Cut | 20 Hz…1 kHz, before saturation | 20…500 Hz (0 = off), before drive, 12 dB/oct | ⚠️ | range ends at 500 Hz; reviewer vocal setting (~800 Hz) is out of reach |
| Thump | resonant LF bump at Low Cut freq | — | ❌ | Q > 0.707 on the HPF |
| Tone | Dark…Bright tilt, **before** saturation, centre default | "Tilt" ±6 dB around 1 kHz, default 0 — applied **after** the curve | ⚠️ | pre/post order is wrong: Decap's Tone changes what gets distorted |
| High Cut | 1…20 kHz, 6 dB/oct, after saturation | 2…20 kHz (1 = off), 12 dB/oct, after | ⚠️ | range and slope differ |
| Steep | High Cut 30 dB/oct | — | ❌ | Ember has only a 12 dB/oct high cut |
| Auto (gain) | on (per review); Output tracks −Drive | "Auto gain" toggle, default on; RMS makeup of a −12 dBFS sine through the curve | ⚠️ | different law; Decap moves the visible Output knob, LYNX does not |
| Output | −24…0 dB | "Out" ±18 dB in bottom bar (bar value) | ⚠️ | range differs; the value is hidden in the bottom bar, not on the main panel |
| Mix | Dry…Wet, 100 % implied | "Dry / wet" 0–100 % (default 100) + per-band Mix | ✅ | the band Mix sits beside it and can be confused with it |
| Dry path latency alignment | n/d | dry delayed by oversampler latency | ✅ | |
| Oversampling / quality | not user-exposed | 2×/4×/8× menu (bottom bar) | ➕ | |
| Multiband (up to 4 bands, crossovers, slopes) | — | yes | ➕ | Saturn role; adds clutter for a Decap user |
| Band Level ±18 dB | — | yes | ➕ | |
| VU meter | analog VU on the panel | none on Ember's editor (engine stores in/out peak, nothing draws it) | ❌ | no level feedback while driving |
| Spectrum display | — | post spectrum fills the window | ➕ | |
| Preset menu (factory + user, save) | Soundtoys control strip | ProEditor header: no preset box at all | ❌ | |
| Prev / next preset | yes | — | ❌ | |
| A/B compare | Soundtoys strip (not verified in this guide) | — | ❌ | |
| Undo/redo | — / host | song-level Ctrl+Z (`set(..., undoable)`) | ⚠️ | |
| Bypass | host / power | LED in header + mixer slot on/off | ✅ | |
| Knob gestures | drag, fine, double-click default (Soundtoys convention) | drag, Ctrl fine, wheel, dbl/Alt reset, right-click value/automation menu | ✅ | |
| Value readouts | on hover / drag | below every control + value tooltip | ✅ | |
| Help / tooltips | manual | hint bar + header gesture text | ✅ | |
| Resizable window | fixed size | fixed 860×520 (ProEditorWindow, not resizable) | ✅ | equivalent |
| One-screen compact layout | ~1 panel: VU top, Drive centre, styles row, filters row | spectrum on top, band panel floating at the bottom, global settings in the bottom bar | ⚠️ | a Decap user hunts for Output and filter switches |
| Section grouping "Low Cut / Tone / High Cut" | one row with Thump/Steep switches beside | three faders under "Tone (all bands)" | ⚠️ | labels exist, switches do not |

**Scores**: configuration 4/10, design 4/10.

**Top gaps** (ordered by what a user touches first):
1. **Style row with 5 Decap-like voicings** (A/E/N/T/P equivalents, under LYNX names, no hardware or trade names). Today the 4-entry dropdown is in `EmberEditor::layoutControls`. Add 1-click tab buttons (`K::tabs`). Two new curves plus per-style pre/post EQ in `ember::shape` and `Saturator::chunk`. Needs engine changes. Effort M (L to fit measured curves).
2. **Move Tone before the drive.** Today it is applied after the curve in `Saturator::chunk`. Engine change (or a "pre/post" option to keep Saturn translations). Effort S.
3. **Punish switch (+20 dB) and Thump / Steep switches.** New parameters in `ember::Param` / `emberInfo`, appended so saved states stay valid. Thump = resonant Q on `lowcut.filter`. Steep = 5th-order HC with a 6 dB/oct default mode. Toggles go next to the faders. Engine change. Effort S–M.
4. **Filter ranges.** Low Cut to 1 kHz and High Cut down to 1 kHz (`lowCutHz` / `highCutHz`). This changes the value law, so it needs a translation guard for existing projects. Effort S.
5. **Output on the main panel, with Auto following Drive visibly.** Output −24…0 range option. UI change plus an auto-gain law change. Effort S.
6. **In/out level meter (VU-style)** in the band panel or at the right edge. The data already exists (`meters.set`). UI only (`EmberEditor::paintBody` with `drawMeter`). Effort S.
7. **Drive readout as 0–10** when in single-band mode, optionally. UI / `paramText`. Effort S.
8. **Preset box, prev/next and A/B** in the ProEditor header (shared by all display-first editors). UI + preset storage. Effort M.

---

## Fruity Delay 3 (stand-in) / Echoform ← EchoBoy (Soundtoys), with PrimalTap
Sources: docs/specs/echoboy.md (Pro Tools-era guide pp. 22–54, store page); `[soundtoys-echoboy]` / `[soundtoys-primaltap]` in src/core/PluginSubstitutes.cpp (→ Fruity Delay 3, no preset, no translation); `fruityDelay3()` and `echoformLayout()` in src/core/PluginLayout.cpp; `echoform::*` in src/core/EchoSuite.cpp; screenshots lynx-before/Fruity Delay 3.png and Echoform.png. PrimalTap facts are from search snippets of the manual (gear4music PDF copy), not a full read. Trial on this PC: no. **Manual only (trial not installed).**

| Original feature | Original (range / default / where) | Fruity Delay 3 now | Echoform now | Status (best) | Note |
|---|---|---|---|---|---|
| Echo Mode: Single | per-channel echo | Delay model Mono/Stereo | Style "Single" (+ Spread) | ✅ (Echoform) | |
| Echo Mode: Dual | two independent times, FB mix / balance | Offset only | Style "Dual" (Time L/R, sync L/R) | ⚠️ | no FB cross-mix or FB balance |
| Echo Mode: Ping-Pong | mono in, L then R | Ping pong | Style "Ping-pong" (+ Pairs) | ✅ | |
| Echo Mode: Rhythm (≤16 taps, 1–8 beats, grid ½…1/64, Shape) | Rhythm editor | — | — (Ratio/Quad are not a pattern) | ❌ | biggest structural gap |
| Echo Style menu (~30 voicings) | main panel menu | — | Mode menu: 12 (Tape, Lo-Fi, Hi-Fi tape, Bucket brigade, Analog, Digital, Clean, Phaser, Shifter, Pitch, Reverse, Detune) | ⚠️ | covers families: tape/BBD/digital/lo-fi; no radio/telephone, chorus/vibrato, "Diffused/Verbed" variants |
| Echo Time ms | ms, pitch-bends when moved | Time + Keep pitch | Time L/R 0–20 s, default 300 ms | ✅ | |
| Note / Dotted / Triplet | Note/Dot/Trip buttons | Tempo sync | Sync menu ms/Note/Dotted/Triplet, 1/64…8/1 | ✅ | |
| Tempo source | host, tap, MIDI, 30–300 BPM | host | host only | ⚠️ | no tap tempo, no free BPM |
| Mix | Dry…Wet, 12 o'clock = 50/50 | Wet/Dry faders | Mix 0–100 % (default 50) | ✅ | |
| Feedback | Min…Max, can run away | Level | 0–200 %, default 70 % | ✅ | |
| Saturation | Less…More, style-dependent, centre default | Feedback distortion Sat/Limit | Drive 0–24 dB (Colour) | ⚠️ | not style-dependent; default 0, not centre |
| HighCut | style-dependent darkening | FB cutoff/res, LP/HP/BP | High cut 200 Hz–20 kHz + Low cut 10 Hz–2 kHz | ✅ | |
| Prime Numbers | detunes repeat times | — | — | ❌ | trivial: snap times to primes in samples |
| Groove (shuffle…swing) | alternate repeats | — | — | ❌ | |
| Feel (rushin'…draggin') | global pre-delay ± | Offset (partial) | Spread ±20 ms (partial) | ⚠️ | |
| Input level (echo path only) | −24…+24 dB | Input Wet | — | ⚠️ | |
| Output level (echo only) | −24…+24 dB | Wet | Level ±18 dB | ✅ | |
| Tweak: Accent | bipolar | — | — | ❌ | |
| Tweak: Width | min…max, past 3 o'clock out-of-phase | Stereo | Width ±100 % | ✅ | |
| Tweak: L/R Offset | 0–25 ms, default 8 | Offset | Spread ±20 ms | ✅ | |
| Tweak Dual: FB Mix / Balance / FB Balance | knobs | — | — | ❌ | |
| Style Edit: 3-band EQ gain + per-repeat Decay, Mid Res | ±18 dB etc. | FB filter only | Low/High cut only | ❌ | the "EQ in the loop vs first repeat" idea is missing |
| Style Edit: saturation type (Clean, Tape, Warm, Pump, Dirt, Limit×4) | menu | Limit/Sat + knee/symmetry | via Mode only | ⚠️ | |
| Style Edit: Diffusion + Size, Loop/Post | knobs + switch | Diffusion level/spread | Diffusion amount/size | ⚠️ | no Loop/Post choice |
| Style Edit: Wobble, Rate, Shape (5), Sync | knobs/menu | Mod rate/time | Wow, Flutter (Tape) / Rate, Depth | ⚠️ | no shape menu, no LFO sync |
| Ducking | — (not in EchoBoy) | — | Ducking | ➕ | |
| Pitch / shifter / reverse modes | — | — | yes | ➕ | |
| Echo display | Tweak "visualiser" of delay tails | — | "echotrain" display of repeats | ✅ (Echoform) | |
| Freeze (PrimalTap) | holds the loop; ≤2048 ms at 8× | — | — | ❌ | |
| Multiply 2×/4×/8× (PrimalTap; halves the rate per step, max 2.5 s) | knob | Smp rate / Bits (lo-fi) | Lo-Fi mode | ⚠️ | no time-for-bandwidth trade |
| VCO Rate/Depth per tap, phase flip (PrimalTap) | knobs | Mod rate | Rate/Depth (one LFO) | ⚠️ | |
| Preset menu / user presets / A/B | Soundtoys strip | FL factory list only; no user save, no A/B | "(no preset)" | ❌ | |
| Gestures, value entry, automation | standard | generic editor: full set | full set | ✅ | |
| Translation of the saved state | encoded | none, FL defaults (ms delay, FB, etc. unrelated) | none | ❌ | state unreadable; default only |

**Scores**: Fruity Delay 3 as the stand-in: configuration 2/10, design 2/10. Echoform: configuration 5/10, design 5/10.

**Verdict:** **Echoform is clearly the better EchoBoy (and PrimalTap) substitute.** It has the four-mode-like Style switch (Single/Dual/Ping-pong + Ratio/Quad), a character Mode menu standing in for EchoBoy's Style, Drive ≈ Saturation, Wow/Flutter ≈ Wobble, Diffusion, note/dotted/triplet sync and an echo display. Fruity Delay 3 is a clean FL delay with none of the "style" concept, played at FL defaults. The spec's own recommendation (§7.1) agrees. H-Delay already moved to Echoform in round 119.

**Top gaps** (ordered by what a user touches first):
1. **Point `[soundtoys-echoboy]` and `[soundtoys-primaltap]` at Echoform**, with a conservative default: Tape mode, low Age, 1/4 note at host tempo, ~25 % FB, mix ~25–30 %. File: src/core/PluginSubstitutes.cpp. Table only. Effort S.
2. **Rhythm mode** (≤16 taps on a 1–8-beat grid, Shape presets) as a 6th Echoform Style. New params + DSP in `echoform::Echoform`, a grid display in PluginEditors.cpp. Effort L.
3. **Groove / Feel / Accent / Prime Numbers.** Four new Echoform params (timing offsets per repeat, a prime-snap switch). Engine change, small DSP. Effort S–M.
4. **Saturation centre default and style-dependent saturation/HC** (Drive interacting with Mode). Engine change. Effort M.
5. **Dual-mode FB Mix / FB Balance; EQ-in-loop "Decay" per band.** Engine change. Effort M.
6. **Freeze switch** (PrimalTap). Engine change. Effort S.
7. **Tap tempo** in the Delay section. UI + param. Effort S.
8. **Preset box / A/B / user presets** for LYNX engines (generic editor, src/ui/PluginEditors.cpp). Effort M.

---

## Auto-pan (stand-in) ← PanMan (Soundtoys)
Sources: docs/specs/panman.md (v5 guide pp. 5–25); `AutoPan` and `autoPanInfo` in src/core/Effects.cpp. There is no custom layout (generic generated panel). Trial on this PC: no. **Manual only (trial not installed).**

| Original feature | Original | LYNX now | Status | Note |
|---|---|---|---|---|
| Offset | −105…+105°, default 0 | Pan −100…+100 % (balance) | ⚠️ | no "beyond the speakers" |
| Width | 0…210°, default 180 | Depth −100…+100 % (`2p−1`) | ⚠️ | **bug: the substitute preset `Depth=0.5` gives depth 0, so the substitute is a pass-through** (comment says "moderate sweep") |
| Rate (Hz, free) | 0.1–10 Hz | — (beats only) | ❌ | |
| Rate (synced) | note values | Period 0…64 beats, default 1 beat | ⚠️ | shown as raw beats, not note names |
| Direction L→R / Back-and-forth / R→L | menu | triangle only (+ Phase) | ⚠️ | |
| Smoothing | Hard…Soft | Tension (curve shape) | ⚠️ | |
| Sources: Rhythm Step (3/4/5), Rhythm Shape editor, Random, PingPong / Random Step (audio-triggered) | source menu | — | ❌ | |
| Feel / Groove | knobs | — | ❌ | |
| Tweak: envelope (Threshold, Rate Mod, Width Mod, Attack/Release 0–5000 ms, Env/Gate) | Tweak page | — | ❌ | |
| Trigger (threshold, HPF/LPF filter, Divider 1–12, Manual) | Tweak | — | ❌ | |
| Analog / Digital input stage | switch | — | ❌ | |
| Input / Output level + LEDs | knobs | — | ❌ | |
| Pan law | undocumented | linear balance or "FL pan law" toggle | ➕ | |
| Control only (automation source) | — | yes | ➕ | |
| Pan position display | animated ball/position | none (generic knobs) | ❌ | |
| Presets / A/B | Soundtoys strip | none | ❌ | |

**Scores**: configuration 2/10, design 2/10.

**Top gaps:**
1. **Fix the substitute preset** (`Depth=0.5` → e.g. `0.75`, or make "no movement" an explicit, commented choice). File: src/core/PluginSubstitutes.cpp. Table only. Effort S.
2. **Free Hz rate, note-name sync readout, Direction (ramp up / down / triangle).** AutoPan params + `paramText`. Engine change. Effort S.
3. **Rhythm Step (3/4/5) and Random sources.** Engine change. Effort M.
4. **A dedicated layout with a pan-position display.** PluginLayout.cpp function plus a display in PluginEditors.cpp. UI only. Effort M.
5. **Envelope Tweak (Rate/Width mod) and audio trigger (PingPong).** Engine change. Effort M–L.
6. Alternatively, write a new LYNX panner engine and keep Auto-pan FL-exact. Effort L.

---

## Reeverb 2 (stand-in) ← Little Plate (Soundtoys)
Sources: store/press pages and the KVR review. The official PDF (soundtoys.com/wp-content/uploads/Little-Plate-Manual.pdf) returned 403. `[soundtoys-littleplate]` (Dry=0, Wet=0.8, mix 0.25); `reeverb2Info` in Effects.cpp; screenshot Fruity Reeverb 2.png; Foil Plate (`foilInfo`, src/core/LynxVerbs.cpp) checked as an alternative. Trial on this PC: no. **Manual only (trial not installed).**

| Original feature | Original | Reeverb 2 now | Foil Plate (alt.) | Status | Note |
|---|---|---|---|---|---|
| Decay (one big knob) | ~0.5 s … ∞ (EMT 140 hardware ≈ 1–5 s) | "Dec" small knob among 17 controls | Decay 0.5–30 s | ⚠️ | no infinite hold in either |
| Low Cut | HPF to tame boom (range not published) | L.Cut | Low EQ freq/gain shelf ±12 dB | ✅ / ⚠️ | |
| Mix | dry/wet | Dry/ER/Wet faders (+ slot mix) | Mix | ⚠️ / ✅ | three faders replace one knob |
| Mod switch | on/off "chaotic variations" | Mod depth + speed | Mod rate/depth | ✅ | |
| Plate character (EMT 140) | single voicing | generic room algorithm | 12 plate types | ❌ / ✅ | |
| No predelay / size / damping exposed | minimal panel | many extra controls | Predelay, Size, Width, High EQ | ➕ | extras clutter a 3-knob plugin |
| Presets (25 factory + user) | Soundtoys strip | FL factory only / none | none | ❌ | |
| 3-control layout | Decay big, Low Cut + Mix small, Mod switch | 17-control FL panel | 12-control panel | ⚠️ | |

**Scores**: configuration 4/10, design 3/10 with Reeverb 2. Foil Plate would score about 6 / 5.

**Top gaps:**
1. **Route `[soundtoys-littleplate]` to Foil Plate** (plate model, decay to 30 s, mod) with a medium decay and a sensible low EQ. Table only. Effort S.
2. **∞ decay at the top of Decay.** `foilDecayKnots` + a freeze path in `FoilFx`. Engine change. Effort S–M.
3. **A true Low Cut HPF on Foil.** New param. Effort S.
4. Optionally, a compact layout (big Decay knob). UI only. Effort S.

---

## (no role) ← Little AlterBoy (Soundtoys)
Sources: docs/specs/little-alterboy.md (2019 guide pp. 3–6); `LynxTune.cpp` (`info`, `paramText`); `flPitchShifterInfo` in Effects.cpp; `vocal::Psola` + YIN in core/VocalPitch.h. Trial on this PC: no. **Manual only (trial not installed).** Usage weight: 696 slots / 399 projects; 67 % of slots share one state.

| Original feature | Original | LYNX Tune | FL Pitch Shifter | Status | Note |
|---|---|---|---|---|---|
| PITCH | −12…+12 st, 0.1 st display | Transpose ±12 st, **integer** | Pitch (continuous) | ⚠️ | |
| FORMANT | range unpublished (tips use −1…−3) | Formant shift ±7 st | Formant | ⚠️ | |
| LINK (formant follows pitch) | on/off | Formant preserve off ≈ Link on | Formant correction | ✅ | inverse sense |
| Mode TRANSPOSE | plain shift | Transpose with Scale "chromatic" + correction | Pitch | ⚠️ | LYNX Tune always corrects; there is no "no-correction" mode |
| Mode QUANTIZE | chromatic, as fast as possible | Scale chromatic + Retune 0 ms | — | ✅ | |
| Mode ROBOT | lock to one C (C5?) + Pitch | one-key mask + Retune 0 (nearest octave, not fixed) | — | ⚠️ | |
| MIDI → Pitch | host MIDI | MIDI param (target notes) | — | ⚠️ | |
| DRIVE (Decap tube) | knob | — (chain Ember) | — | ❌ | |
| MIX | dry/wet | Mix | Mix | ✅ | |
| Simple 6-control panel | one screen | full tuner UI (key, scale, 12 keys…) | FL panel | ❌ | |
| Substitute entry | — | none (`PLUGIN_FALLBACK.md`: "not mapped yet") | — | ❌ | |

**Scores**: configuration 0/10 today (no role; about 5/10 if mapped to LYNX Tune), design 0/10.

**What a LYNX engine needs:**
- Pitch ±12 st (0.1 st)
- Formant (range to be measured)
- Link
- Mode {Transpose, Quantize, Robot (fixed reference note)}
- Drive (reuse an Ember tube curve)
- Mix
- Reported latency

**Can an existing engine cover it?** LYNX Tune already owns YIN + the formant-capable PSOLA and covers Quantize, Mix, Link-ish and MIDI. It lacks a no-correction transpose, a fixed-note Robot, fractional Pitch and Drive. FL's Pitch Shifter covers only Transpose + Link.

**Top gaps:**
1. **Read her dominant state first** (spec §8.1, needs the trial). If it is the default, a near pass-through is enough. Effort S once the trial is available.
2. **Small "Voice Shift" engine** on `vocal::Psola` + YIN with the six controls above, or a "Voice" mode inside LYNX Tune (bypass correction, fixed-note target, 0.1 st transpose, Drive). New engine, layout and `[soundtoys-littlealterboy]` entry (vst2 `StLa`) in PluginSubstitutes.cpp. Effort M.
3. **Drive stage** reusing `ember::shape` on the wet path. Effort S.

Sources (web): [Little Plate manual (403)](https://www.soundtoys.com/wp-content/uploads/Little-Plate-Manual.pdf), [KVR Little Plate](https://www.kvraudio.com/product/14315), [Soundtoys "Introducing Little Plate"](https://store.soundtoys.com/introducing-little-plate/), [Decapitator guide (B&H copy)](https://static.bhphoto.com/lit_files/89645.pdf), [PrimalTap manual copy](https://r2.gear4music.com/media/69/699356/download_699356.pdf), [PrimalTap at Plugin Boutique](https://www.pluginboutique.com/product/2-Effects/10-Delay/1857-PrimalTap), [SOS Soundtoys 5 review](https://www.soundonsound.com/reviews/soundtoys-5), [Mixdown: Soundtoys 5 rhythm editor](https://mixdownmag.com.au/features/soundtoys-5-and-its-rhythm-editor-right-on-time/).

