# UI study: Playlist clips, the clip menu and stuck preview notes (uifix round, 2026-10-16)

User feedback on v0.1.4-alpha: (1) "when I press to play it plays, but when I let go it keeps playing";
(2) audio clips in the Playlist look "very ugly, too detailed in a bad way"; (3) no FL-style clip menu.
FL Studio 2026 was used as the visual reference only (clean-room: screenshots stayed outside the repo,
nothing of FL's art was copied; the drawing is LYNX's own JUCE code in LYNX's palette).

## 1. Stuck preview notes

Every press-to-preview path was audited:

| Path | Before | Now |
| --- | --- | --- |
| Channel Rack: click/paint a step | note-on sent for every painted step, **never a note-off** (synths and long samples kept sounding) | the step sounds while the button is held; the previous step's note stops when the next is painted; all stop on mouse up |
| Channel Rack: Ctrl+click audition | off on mouse up only | same, through `HeldNotes`; also stops if the rack is hidden mid-gesture |
| Piano Roll keyboard / note clicks / drags | the note-off went to the channel selected *at release time* (switching channel while holding stranded the note) | each held note remembers its channel; stops on mouse up, when hidden, on destruction |
| Piano Roll Listen tool | same channel problem | same fix |
| Typing piano (PC keyboard) | off computed from the key code again, through the scale snap again (a snap change while held stranded the note); switching to another app with a key down left it sounding | each key remembers its notes (tag = key code); the engine keeps a ledger of the snapped key (`KeyLedger`); app focus loss releases everything; keys released while another window had focus are caught by the timer |
| MIDI keyboard | note-off sent to the channels selected *now* and snapped with the snap *now* | `InputHeldNotes` (fixed size, MIDI-thread safe) sends the off to exactly what the on reached; a repeated note-on first stops the previous one |
| Playlist Alt+right-click preview / Listen tool | stop on mouse up | also stops if the Playlist is hidden mid-gesture |
| Browser sample preview | plays the whole file (as FL) | unchanged (by design) |

Core: `src/core/HeldNotes.h` (`HeldNotes`, `KeyLedger`, `InputHeldNotes`), tests in `tests/UiFixTests.inc`.

## 2. Clip look (FL reference → LYNX)

See section "Reference notes" below.

## 3. Clip menu

See section "Menu" below.
