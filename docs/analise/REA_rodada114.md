# REA round 114 — the "inspired by" editors look and work like the originals

Branch `feature/look114`. English-first; clean-room. User request (2026-10-20): every LYNX engine that stands in for a
third-party plugin must look and feel as close as possible to the plugin it is inspired by — same layout, controls in
the same places, the same interactions (drag nodes, click to add, right-click menus, displays, meters) — only easier to
read, the way Facet EQ was redone after Pro-Q (round 103, the bar to meet).

Method (round 100 UI rule): the OFFICIAL trials installed on this PC were hosted in our own small VST3 host and
screenshotted as a user sees them; LYNX's editors were rendered with `LYNX.exe --plugui` and compared side by side.
Only the *layout logic and workflow* were taken — where the controls are, what the display shows, how it is edited —
drawn with LYNX's own colours, fonts, knobs and meters. No graphics, logos, names or trade dress were copied; the
reference screenshots stay in `tmp/look114/ref` (never in git).

## 1. Reference captures [EXEC]
- Host: `tmp/look114/host/uiview.cpp` (a copy of round 103's host with an interactive session: `open <wav>` plays a
  music loop through the plugin in real time while its editor is open, `click / rclick / dbl / drag / move / wheel`
  post mouse messages to the editor's window — the real cursor is never moved —, `snap <png>` captures the window with
  PrintWindow). The window sits at the bottom of the z-order, so nothing on the screen of other agents is covered.
- Captured: Pro-L 2 (main view, knob panel, gain slider), Pro-DS, Pro-C 3 (main, knee panel, side-chain panel),
  Pro-G (main, expert panel), Pro-MB (selected band with its floating panel, hover read-out), Saturn 2 (band panel).
  The Valhalla references are round 100 / 101's (tmp/valh100/ref, tmp/valhd101/ref).

## 2. Audit: closeness of each editor to its reference (0 = unrelated, 10 = same workflow)
Scores judge layout (where things are), control placement, the main display and the interactions.

| LYNX engine | Inspired by (trial here?) | Before | After | What changed |
|---|---|---|---|---|
| Facet EQ | Pro-Q 4 (yes) | 9 | 9 | the bar (round 103), unchanged |
| Apex Limiter | Pro-L 2 (yes) | 1 | 8 | had no editor (generated knob grid): now the display-first limiter view |
| Crest Compressor | Pro-C 3 (yes) | 5 | 8 | knobs float over the full-window level display; knee slides out; side chain opens below |
| Prism Multiband | Pro-MB (yes) | 3 | 8 | spectrum window with band regions and nodes, floating band panel |
| Sentry Gate | Pro-G (yes) | 6 | 8 | level history behind the transfer curve, draggable threshold, EXPERT strip |
| Hush De-esser | Pro-DS (yes) | 2 | 8 | scrolling waveform with the reduction, big THRESHOLD / RANGE, frequency slider, mode buttons, meters |
| Ember Saturator | Saturn 2 (yes) | 3 | 7 | spectrum with bands (split by double-click, drag drive handles / crossovers), floating band panel |
| Glow Leveler | CLA-2A (shell entry opens another plugin, §5b) | 4 | 7 | GAIN / PEAK REDUCTION around a VU needle, COMPRESS / LIMIT under it; the VU switches gain reduction / output |
| Aether | Fresh Air (yes, §5b) | 4 | 8 | display-first editor: arc output meter on top, two big knobs with LINK, power, small TRIM |
| Ridge Strip | SSL E-Channel (yes, §5b) | 5 | 8 | the vertical channel strip: filters and HF → LF bands on the left, dynamics, routing and gain on the right |
| LYNX Tune | Auto-Tune Pro (not installed) | 5 | 5 | unchanged (the trial is not installed, §7) |
| Epoch Verb | ValhallaVintageVerb (demo) | 7 | 8 | caption above / value under each knob, readable mode menus with ◀ ▶ steps |
| Atrium | ValhallaRoom (demo) | 7 | 8 | same |
| Foil Plate | ValhallaPlate (demo) | 7 | 8 | same |
| Nebula | ValhallaSupermassive (demo) | 7 | 8 | same |
| Halo | ValhallaShimmer (demo) | 7 | 8 | same |
| Horizon | ValhallaFutureVerb (demo) | 7 | 8 | same |
| Echoform | ValhallaDelay (demo) | 6 | 7 | same |
| Spiral Echo | ValhallaFreqEcho (demo) | 8 | 8 | same |
| Orbit | ValhallaSpaceModulator (demo) | 8 | 8 | same |
| Lattice Mod | ValhallaUberMod (demo) | 7 | 8 | same |

## 3. The display-first editors (src/ui/ProEditors.cpp, ProEditorBase.h)
One shared base (parameters by name, LYNX's control contract — drag, Ctrl = fine, wheel, double-click / Alt-click =
reset, right-click = value / automation / MIDI menu —, knobs, faders, switches, option buttons, a bottom bar of
"Caption: value" items, a scrolling level / gain-reduction history fed by the slot's analyser tap at 100 columns per
second or by the effect's block meters, a spectrum analyser, meters). `--plugui` renders them with a synthetic
programme run through each engine's static curve.

- **Apex Limiter** (Pro-L 2's logic): the display is the window — the input after the gain (grey), the output (blue)
  and the gain reduction hanging from the top (red), the biggest reductions tagged with their depth; the GAIN slider
  on the display's left edge (drag the box; wheel anywhere on the display); the red ceiling line can be dragged; the
  output and gain-reduction meters on the right with their peak holds; STYLE / LOOKAHEAD / RELEASE / CHANNEL LINK in
  a panel at the bottom left that folds away («); TP button and "True peak limiting / Style / Lookahead / Gain / Out"
  in the bottom bar. Apex now publishes live meters (gain reduction, input, output; core/ApexLimiter.cpp).
- **Hush De-esser** (Pro-DS's logic): the scrolling waveform on top with the removed part in red and the threshold as
  dashed lines (drag them, or the arrow on the input meter); the big THRESHOLD and RANGE with the listen button; the
  frequency slider (2–16 kHz) showing the detection region (everything above it, or a band around it) and Audition;
  the detection (high-pass / band) and processing (wide band / split band) buttons in the middle; RELEASE and OUTPUT
  on the right; input and reduction meters at the right edge.
- **Crest Compressor** (Pro-C 3's logic): the level display fills the window (input grey, output blue, gain reduction
  as a red line from the top, the threshold dashed — drag it); the knee / transfer curve slides out from the left edge;
  THRESHOLD, RATIO, the style button (character under it), ATTACK, RELEASE (auto) float over the display's bottom,
  KNEE / RANGE / LOOKAHEAD / HOLD under them, make-up GAIN with auto gain and MIX on the right; SIDE CHAIN opens a
  panel below the display (listen, internal / external, level, stereo link, the detector EQ with draggable nodes and
  the three band shapes); oversampling, look-ahead, character pre / post, in / out in the bottom bar.
- **Sentry Gate** (Pro-G's logic): THRESHOLD with RATIO / RANGE on the left; the display in the middle with the input
  level scrolling behind the transfer curve, the live level as a dot on the curve, the threshold dashed both ways
  (drag either line); the style button, ATTACK / RELEASE and HOLD / KNEE / LOOKAHEAD on the right; EXPERT opens the
  strip below (upward expansion, internal / external side chain, the side-chain high / low pass as a two-handle range
  slider with Audition, DRY, OUTPUT).
- **Prism Multiband** (Pro-MB's logic): the spectrum (pre / post analyser) is the window; each band a coloured region
  with a node on the gain scale — drag up / down = range (how far the band may move), left / right = move the band;
  drag an edge = crossover; wheel = threshold; S / M buttons per band (solo / bypass); the live gain change fills the
  region and the summed response is drawn over it; hovering a node shows its read-out; clicking a band opens its
  floating panel (on, solo, THRESHOLD, RANGE, ATTACK, RELEASE, OUTPUT, compress / expand, RATIO, KNEE, previous / next,
  close); right-click = band menu; gain scale 6 / 12 / 30 dB; crossover slope, look-ahead, analyser, mix, in / out in
  the bottom bar. (Prism keeps its four contiguous bands: "adding" a band = switching it on.)
- **Ember Saturator** (Saturn 2's logic): the spectrum on top with the bands side by side; double-click splits the band
  under the mouse (up to four; the new band copies its neighbour), drag a band's handle up / down = its drive, drag a
  crossover line to move it, Alt-click / double-click a handle = remove the band; the selected band's panel floats
  below (style button, MIX, the big DRIVE, LEVEL, the global tone faders — tilt, low cut, high cut —, auto gain,
  dry / wet, previous / next, remove); crossover slope, oversampling, band count and output in the bottom bar.

The window opens from the mixer slot as before (NativeEffectWindows routes these six kinds to ProEditorWindows); the
generic panels of round 96 / 108 stay in core/PluginLayout.cpp (tests, fallbacks).

## 4. Valhalla-inspired panels (generic editor)
- Every knob of the reverb / delay / modulation panels now shows its caption above and its value under it, as the
  reference panels do (`plugui::captionAbove`, applied by `captionsAbove()` in layoutFor).
- Mode / colour / type menus are readable (light, larger text) and wide ones step with ◀ ▶ at their ends (click the
  arrow = previous / next mode, click the middle = the list), like the references' MODE rows.

## 5. Engines without a trial on this PC
From the official product pages only (no plugin hosted):
- Glow Leveler (CLA-2A role): the classic optical leveler front panel — GAIN left, PEAK REDUCTION right, the VU meter
  between them (here showing the gain reduction, with needle inertia), COMPRESS / LIMIT under the meter.
- Aether (Fresh Air role): two big knobs side by side (mid air = the exciter, high air = the shelf) with the trim
  between them; LYNX's extra settings small underneath.
- Ridge Strip, LYNX Tune: left as they are until the user installs the official trials (see §7).

## 5b. With the official trials (2026-10-20 evening)
Captured in the round's host (tmp/look114/host, now `load <shell> … "<name>"` picks one plugin out of a Waves shell):
- **Fresh Air** (Slate Digital, v1.1.7): the window is a level arc on top (−∞ … 0 dB, red from −6 dB) and a knob
  panel under it: MID AIR and HIGH AIR big, a link button between them, POWER bottom left, a small TRIM bottom right;
  parameters Mid Air, High Air (0–100 %), Bypass, Trim (−12 … 0 dB). → **Aether** is now a display-first editor
  (ui/ProEditors.cpp, AetherEditor): the output peak on an arc meter with a hold dot, MID AIR / HIGH AIR big with
  LINK (moving one moves the other by the same amount), the power switch (the slot's on / off), TRIM small at the
  bottom right; LYNX's extra settings (exciter and shelf frequency, even / odd character, mix) in the bottom bar.
- **SSL E-Channel** (Waves, v17.1, demo mode): a tall strip, two columns. Left: FILTERS (HPF, LPF, split), then HF,
  HMF, LMF, LF top to bottom (gain, frequency, Q on the mid bands, BELL on HF / LF). Right: DYNAMICS — compressor
  (ratio, threshold, release, fast attack), expander / gate (range, threshold, release, fast attack, GATE), the
  gain-reduction LEDs —, routing (EQ to / DYN to), ANALOG, and the GAIN block (input / output VU, output fader,
  input trim, phase). Its 32 parameters map one to one onto Ridge Strip's. → **Ridge Strip**'s panel
  (core/PluginLayout.cpp) is now that strip in LYNX's style, plus the EQ curve under the bands and the static
  compressor curve (round 96's displays). Test: `look114_ridge_strip_reads_top_to_bottom_like_a_channel_strip`.
- **CLA-2A** (Waves): its shell entries ("CLA-2A Mono / Stereo") instantiate Q10 in a JUCE host here (both names,
  twice) — no capture. Glow keeps its product-page layout; the VU meter now works like the reference's meter
  selector: click it to switch gain reduction ↔ output level (0 VU = −18 dBFS).
- **Auto-Tune Pro**, **Decapitator**: not installed (only Antares Central / Auto-Tune Central; no Soundtoys) —
  see tmp/look114/ASK_USER.md.

## 6. Tests (tests/LookTests114.inc)
Apex's live meters (gain reduction of a +12 dB sine into the ceiling, input and output peaks); every knob of the ten
reverb / delay panels carries its caption above and stays inside the panel. The existing panel tests (every
parameter placed once, nothing overlapping, the leveler's meter) run on the changed layouts.

## 7. Where I stopped / next steps
- Done: §1–§6, §5b (Aether display-first editor from Fresh Air, Ridge Strip channel-strip layout from SSL E-Channel,
  Glow's VU selector).
- Still needs the user: install the Auto-Tune Pro trial (from Auto-Tune Central) and Soundtoys 5 (Decapitator) for
  LYNX Tune and Ember's second role; CLA-2A's shell entry opens Q10 in our JUCE host — check in Waves Central that
  the CLA-2A trial is activated (or open it once in FL) so it can be captured.
- Ideas: per-control description line at the bottom of the reverb panels (the references show one), Prism with free
  band placement (gaps), Ember per-band solo / mute, analyser freeze, Crest / Sentry side-chain EQ nodes for Sentry,
  Ridge Strip input / output VU (the reference's GAIN block), Ridge "ANALOG" (noise / saturation) switch.
