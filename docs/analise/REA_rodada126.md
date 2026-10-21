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
- **Crest Compressor** — the gain-reduction trace burns red with a glow; the GR meter is a lit meter; the level fills
  have lit edges; aurora backdrop.
- **LYNX Tune** — the correction ring is the hero: a rose bloom behind it that grows with the correction, a bezel,
  glowing lit segments brightening to a white-hot tip, dim glass LEDs when unlit, the target note glowing; the pitch
  graph's newest corrected point is a lit bead.
- **Epoch Verb** (and every FL-arranged panel) — the big Decay knob lights the panel around it; glass sections.

## Where I stopped
- Kit + flagships written; first build running.
