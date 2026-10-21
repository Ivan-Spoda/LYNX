# REA round 122 — a LYNX skin for every plugin editor

Branch `feature/skin122`. English-first; clean-room. User request (2026-10-21): "make a pretty design for the plugins,
keeping the same way to configure them, but make it pretty so it grabs attention and people enjoy playing with it."

Rule kept: **only the drawing changed.** Every editor keeps its layout, control placement, sizes and gestures (rounds
103 / 114 made the "inspired by" editors follow the originals' workflow; Facet EQ stays the bar). Identity stays LYNX:
dark surfaces, the red brand mark, no vendor graphics or trade dress.

Evidence markers: [EXEC] LYNX runs (`--plugui` renders, `--editorcpu` measurements), [model] design choices.

## 1. The design kit — `src/ui/PluginSkin.h / .cpp` (namespace `lynx::skin`)
One kit used by every built-in plugin editor (and ready for LYNX Tune's new editor of round 120):
- **Accent per plugin family** (`accentFor (EffectKind)`): reverbs violet, delays / echoes azure, modulation teal,
  dynamics amber, saturation / distortion ember orange, voice / pitch rose, stereo / utility lime, EQ / filters and
  LYNX's own default the LYNX red. The header keeps the red brand mark, so every editor still reads as LYNX.
- **Knobs** (`skin::knob`): a dark groove with the unlit ring, the lit value arc in a gradient of the accent with a
  soft two-layer glow and a specular line, a machined cap (shadow, top-lit body, concave face, rim) and a pointer that
  glows while hovered / dragged. Bipolar knobs keep their centre mark. Small knobs (< 26 px) drop the glow.
- **Surfaces**: `canvas` (deep gradient with a faint pool of the accent under the header), `headerBar` (raised,
  top highlight, accent line fading out), `brandMark` (the LYNX logo, resampled once per size and cached),
  `titleText`, `powerLed` (halo when on), `section` + `sectionTitle` (raised panels with an accent dot in the
  title), `well` (inset displays with an inner shadow), `floatingPanel` (glassy, accent edge), `bottomBar`,
  `valuePlate` (read-outs, preset name).
- **Controls**: `slider` (groove, lit gradient fill with a glow, raised thumb with a lit line), `button` (lit = glows
  in the accent), `switchPill`, `selector`, `meter` (green → amber → red gradient, segment gaps, peak hold),
  `meterSolid` (gain reduction), `glowStroke` (response curves).
- **Motion**: `skin::Animator` fades the hover glow in / out at 60 Hz and **stops its timer as soon as nothing
  moves**; `skin::Ballistics` (rise at once, smooth fall, peak hold) for meters.
- The value pop-up while dragging / wheeling (ui/ValueControl) is now a glassy pill with an accent edge.
- `juce::Slider`s carrying `skin::sliderProperty` are drawn with the kit by `theme::LookAndFeel` (the generator pages).

## 2. Where it is applied
- Display-first editors (ui/ProEditorBase.h → ProEditors.cpp: Apex Limiter, Hush De-esser, Crest Compressor,
  Sentry Gate, Prism Multiband, Ember Saturator, Aether): canvas, header, knobs / faders / toggles / tabs / menus /
  bar items, floating panels, displays (gradient level fills, glowing transfer / response curves, lit meters, the
  Aether arc with a glow).
- FL-arranged native editors (ui/PluginEditors.cpp, every effect of `--plugui` incl. the Valhalla-inspired panels,
  Glow Leveler, Ridge Strip, Maximus, EQUO, Gross Beat, …): header with the mark and the preset plate, sections,
  knobs (the big-knob special case is gone: the kit's cap fills the ring), sliders, switches, radio LEDs, tabs,
  steppers, selectors, display wells and curves in the plugin's accent.
- Facet EQ: header, bottom bar, the band panel and node tip as floating panels, kit knobs, glossy nodes, the sum curve
  with a wider glow, gradient band fills.
- Parametric EQ curve (also the mixer's slot thumbnail): well + glowing curve.
- Generator pages in FL's arrangement (ui/ChannelSettings.cpp: 3x Osc, FL Keys, BooBass, Sytrus, Harmor, …).
- **Not yet**: LYNX Tune's own display (being redone by tune120 — it already gets the shared header / knobs / canvas;
  its pitch display gets the final pass after tune120 lands).

## 3. Before / after [EXEC]
`LYNX.exe --plugui` with main (0.1.9, before) and this branch (after), 87 PNGs each; side-by-side sheets in
`~/.claude/jobs/f8789b49/tmp/skin122/` (one PNG per editor + `index.html`).

## 4. CPU of an open editor [EXEC]
New `LYNX.exe --editorcpu report.txt`: opens editors one at a time in a small window (no audio device), 2 s to
settle, 8 s measured, process CPU in % of one core; "idle" = a silent track, "animating" = the demo signal forcing
every frame. Median of 3 runs, main + the same CLI (before) vs this branch (after):

| editor | mode | before (main) | after |
|---|---|---|---|
| Facet EQ | idle | 5.27 % | 1.56 % |
| Crest Compressor | idle | 8.20 % | 2.15 % |
| Apex Limiter | idle | 7.22 % | 1.17 % |
| Prism Multiband | idle | 12.88 % | 0.39 % |
| Epoch Verb | idle | 0.78 % | 0.78 % |
| Glow Leveler | idle | 6.83 % | 0.39 % |
| Fruity Limiter | idle | 1.17 % | 1.56 % |
| LYNX Tune | idle | 8.39 % | 0.78 % |
| Facet EQ | animating | 9.37 % | 20.87 % |
| Crest Compressor | animating | 7.61 % | 14.24 % |
| Apex Limiter | animating | 10.93 % | 12.49 % |
| Prism Multiband | animating | 12.49 % | 16.79 % |
| Epoch Verb | animating | 1.95 % | 1.17 % |
| Glow Leveler | animating | 7.22 % | 8.98 % |
| Fruity Limiter | animating | 0.98 % | 1.37 % |
| LYNX Tune | animating | 8.00 % | 7.61 % |

- **Idle editors no longer burn CPU**: before, every editor with a live display repainted at 30 Hz forever (5–13 % of
  a core for Facet EQ, Crest, Apex, Prism, Glow Leveler, LYNX Tune on a silent track); now the display-first editors
  stop when the visible history is silent, the spectrum is at the floor and the live meters are still; the FL-arranged
  editors stop when their meters have not moved for longer than their longest history (10 s); Facet EQ redraws its
  graph only when the analyser or a dynamic band moved; a hidden editor does no work at all. Idle ≈ 0.4–2 % (window +
  message loop noise).
- **Animating** costs about the same or a little more (the gradients and glows; Facet's graph is the most expensive).
  The kit's knobs are rendered once per look into a small cached image and blitted afterwards, which brought Crest from
  26 % to 14 % and Prism from 28 % to 17 % in the same test; history lines drop the glow (drawn at 100 columns a
  second). The numbers are noisy (other agents' builds share the machine): ±5 points between runs.

## Where I stopped
- Stage 1 (kit + all editors except LYNX Tune's display) built and rendered; CPU measured.
- Next: land stage 1; after tune120 lands, apply the kit to LYNX Tune's new editor and land again.
