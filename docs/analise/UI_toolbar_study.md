# UI study: a lighter header and quick tool-name tips (2026-10-16)

User request (pt-BR, translated): "The header design is a bit thick/heavy; I want the icons even more delicate,
and on mouse-over each tool should show its name, very small, just to guide whoever is using it."
Reference: FL Studio 2026 running on the user's machine (1920x1080, 100% scale), captured on screen for study
only. Clean-room: no FL bitmap, icon, skin, font or screenshot is in the repository; the reference captures stay
outside it. What follows are measurements and observations in our own words, and what LYNX took from them.

## What the reference shows [EXEC: on-screen capture]
- **Header height.** The main toolbar is two rows of about 40 px each, about 82 px in all (menu + hint panel on the
  left, transport and displays, then two rows of small square buttons). The panel title rows are about 22 px.
- **Icon size and weight.** Toolbar glyphs are about 16-20 px inside buttons about 30 px apart; panel title-bar
  glyphs (the editor tools, the window buttons) are smaller, about 12-14 px, drawn as thin light-grey lines with
  no background at rest. Window buttons (minimize / maximize / close) are about 10 px glyphs.
- **Backgrounds.** Title-bar tools have no plate at rest; toolbar buttons sit on low-contrast plates. The
  transport (play / stop / record) is one compact segmented group, one row high, not two.
- **Hints.** Pointing at a control writes its name and help in the hint panel under the menu (LYNX already has
  this: the hint bar, G12). A floating pop-up bubble for toolbar icons was not observed in the captures:
  "not confirmed". The quick-tip bubble is therefore LYNX's own addition, kept small so it only names the tool.

## What LYNX changed (own design, LYNX palette and red accent)
- Icons: one fine line for the whole family, `icons::strokeWidth(size)` = clamp(1.0 + 0.019 x size, 1.2, 1.6)
  logical px (about 1.27 px at 14 px, 1.3 px at 16 px, 1.4 px at 20 px); HiDPI scales it with the display.
- Toolbar: rows 28 px (was 32), icons 16 px (was 24), header 64 px (was 74) at the default icon size; small and
  large sizes follow (24/34 px rows, 14/20 px icons). Editor tool strips 16 px icons (was 20), title-bar window
  buttons 13-14 px.
- Flat buttons everywhere in the header: hover is a faint white wash (7.5%), "on" a faint accent wash (16%) with
  the accent on the icon; Pattern/Song, metronome and recording options use it too. Play / stop / record are one
  slim row in a quiet rounded well instead of three tall plates. Tempo and time displays use a lighter well and
  smaller digits.
- Quick tips: a small dark bubble (11 px text, shortcut in a dimmer 10 px) centred under the control, shown about
  400 ms after the mouse rests, swapped at once when moving to the next control, hidden on click. Names and
  shortcuts come from the key map, translated in pt-BR. Snapshots: `-toolbar-tips.png`, `-tips-sheet.png`.
