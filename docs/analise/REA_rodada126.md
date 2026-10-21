# REA round 126 — a much bolder look for every plugin editor

Branch `feature/skin126`. English-first; clean-room. User feedback on round 122's before / after: "too subtle — make
it much bolder (bem mais ousada): plugins that grab attention and make people want to play with them."

Rule kept: **only the drawing changed.** Every editor keeps its layout, control placement, sizes, information and
gestures (the inspired-by editors follow the originals' workflow; parity128 / parity129 add missing controls through
the same shared components, so they pick the style up automatically). Identity: LYNX's dark base with the red brand
mark + the family accents of round 122; no vendor graphics or trade dress.

Evidence markers: [EXEC] LYNX runs (`--plugui` renders, `--editorcpu` measurements), [model] design choices.

## 1. What changed in the kit (`src/ui/PluginSkin.*`) [model]
- **Canvas with aurora** — every editor's background is a deep gradient with large soft blooms of its family accent
  (upper left), a cooler second hue (right), the LYNX red low on the right, fine grain and a vignette. Rendered once
  per size / accent into a cached image (`cachedLayer`), so a 30 Hz display pays one blit.
- **Display backdrops** (`skin::backdrop`, the display-first editors' body) — the same aurora, stronger, darker base.
- **Header name plate** — glass header with the accent washing in behind the title and the brand red from the right,
  a bigger bold title lit from behind in the accent, a glowing accent edge; the LYNX mark sits in a red glow.
- **Glass sections** — translucent bodies (the aurora shows through), soft shadows, sheen, rim light on top, dark lip;
  section titles carry a lit LED in the accent and accent hairlines.
- **Knobs** — bigger 3D caps (turned side wall, face lit from the upper left, machining rings, specular highlight),
  a rim that reflects the accent from below (stronger as the value rises), a thick lit arc with a wide glow and a
  white-hot tip, an LED pointer (accent glow, white core, lit end dot), dim LED ticks on big knobs, a halo while
  dragging. Big knobs (≥ 52 px) light the panel around them, brighter as they are turned up (live, one gradient).
- **Illuminated buttons / switches** — lit = the light spills out around them; unlit = raised with an accent edge on
  hover; switches glow when on; slider thumbs carry an accent LED line, fills glow.
- **Meters** — an LED ladder that is visible even when silent, lit gradient with a glow, a white hot edge, glass,
  glowing peak hold; gain-reduction meters the same in their colour.
- **Curves** (`glowStroke`) — wider glow plus a white-hot core.
- **Wells** — deeper inner shadows; displays get light rising from their bottom edge in the accent.
- **Floating panels** — glass tinted with their edge colour, the edge glowing.
- **Open animation** — an editor window's content fades and scales in over 220 ms (`skin::playOpenAnimation`).
- **Reduced effects** (Settings > General, pt-BR string included) — no glows, blooms, grain or open animation.

## 2. Flagships [model]
- **Facet EQ** — the analyser is a deep accent fill rising to a lit edge; the area between the sum curve and 0 dB is
  lit; the sum is a white line in a two-layer red glow; every node glows in its band colour.
- **Crest Compressor** — the aurora backdrop behind the history, a lit GR meter (glow, hot edge, LED ladder), big
  knobs that light the display around them; the history itself stays glow-free (it is redrawn at 30 Hz, section 4).
- **LYNX Tune** — the correction ring is the hero: a rose bloom behind it that grows with the correction, a bezel,
  glowing lit segments brightening to a white-hot tip, dim glass LEDs when unlit, the target note glowing; the pitch
  graph's newest corrected point is a lit bead.
- **Epoch Verb** (and every FL-arranged panel) — the big Decay knob lights the panel around it; glass sections.

## 3. Before / after [EXEC]
`LYNX.exe --plugui` with main (round 122's kit, before) and this branch (after), 87 PNGs each. Early preview sheet of 12
editors (flagships first): `C:\Users\kauan\OneDrive\Área de Trabalho\LYNX-visual-ousado\index.html`.

## 4. CPU of an open editor [EXEC]
`LYNX.exe --editorcpu` (2 s settle, 8 s measured, % of one core), 5 runs of main vs 5 runs of the first bold build,
medians. The machine was shared with several other agents' builds: single runs jumped to 15–25 % at random (e.g.
Epoch Verb idle *before* 23.8 % once), so only medians are meaningful.

| editor | mode | before (main) | bold, first build |
|---|---|---|---|
| Facet EQ | idle | 3.32 | 2.73 |
| Crest Compressor | idle | 3.12 | 3.32 |
| Apex Limiter | idle | 3.32 | 2.93 |
| Prism Multiband | idle | 2.93 | 3.12 |
| Epoch Verb | idle | 2.34 | 3.51 |
| Glow Leveler | idle | 2.54 | 2.54 |
| Fruity Limiter | idle | 2.93 | 2.93 |
| LYNX Tune | idle | 3.32 | 3.51 |
| Facet EQ | animating | 17.36 | 21.85 |
| Crest Compressor | animating | 19.90 | 30.06 |
| Apex Limiter | animating | 23.03 | 34.93 |
| Prism Multiband | animating | 32.59 | 34.95 |
| Epoch Verb | animating | 2.73 | 2.34 |
| Glow Leveler | animating | 10.14 | 13.27 |
| Fruity Limiter | animating | 2.93 | 2.93 |
| LYNX Tune | animating | 17.16 | 23.42 |

- **Idle stays where it was** (~3 %, window + message loop): nothing new animates on a silent track.
- The first bold build cost +10 points on Crest and +12 on Apex while animating. Causes found: a lit edge stroked on
  every 100-column-a-second history fill, Crest's wide gain-reduction glow (a 6 px stroke over 760 points at 30 Hz)
  and live radial gradients for the knob / LED blooms. Fixed: the history fills keep their gradient but no stroke,
  the GR trace is a plain line again, and `skin::bloom` blits a cached sprite (per colour and strength) instead of
  computing a radial gradient per pixel; LYNX Tune's unlit ring segments lost their extra highlight stroke.
  Re-measured numbers: section 6.

## 5. Plugin styles: three bolder directions (switchable) [model]
After the preview the user asked for "mais ainda" (much more). The kit now has a **style** (`skin::Style`, Settings >
General > Plugin style, pt-BR included; `--pluginstyle <name>` for CLI renders), each a full set of the kit's
drawing functions (`src/ui/PluginStyles.inc`), so every editor — and every control the parity agents add through the
shared components — follows it:
- **Glow** (default, sections 1–2).
- **Neon** — near-black with full-panel washes of the family colour and a shifted hue, a faint neon grid, neon-outlined
  glass sections, coloured header pills, LED segment rings + a glowing neon value ring on every knob over a glossy
  black cap, neon buttons / switches / faders, LED-block meters with bloom, heavy glowing curves, a big neon title.
- **Hardware** — premium studio gear in LYNX's own design: an anodized faceplate tinted by the family with brushed
  metal, bevels and screws, recessed milled panels with silk-screened caps, an engraved silver name badge,
  machined aluminium knobs (knurled skirt, turned top, engraved pointer, printed scale, a thin lamp ring), metal
  fader caps, chunky lit buttons, backlit glass displays in bezels, LCD read-outs, LED ladders in bezels.
- **Vivid** — a saturated colour block per family with big playful circles, dark rounded cards, oversized flat knobs
  (drawn 10 % bigger), white arcs and chunky white pointers, solid pills, flat chunky meters, heavy uppercase title,
  and a springy open animation (a small overshoot).
Hardware and Vivid draw knobs a little bigger than the cell's circle (4 % / 10 %); no layout, control position,
caption or gesture moves.

## Where I stopped
- Glow kit + flagships done and measured; CPU fixes in; the three styles written; build queued on the Windows lock;
  next: render the variants sheet for Crest / Epoch Verb / LYNX Tune, re-measure CPU, land.
