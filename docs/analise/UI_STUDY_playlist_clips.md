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

What the reference showed (FL 2026, default skin, measured on screenshots at 125 % scaling):

- A clip fills its track; a 1 px near-black edge separates neighbours; square corners.
- Header bar ≈ 25 % of the track height (≈ 11 logical px), a faint vertical gradient, in the clip colour
  greyed towards neutral (pattern headers are almost grey); light text; a small kind icon at the left, which
  is also where the clip menu opens.
- Body: a deep tint of the clip colour.
- Audio: one filled shape — the peak envelope mirrored around the centre, no outline, no RMS layer — in a
  very light tint of the clip colour, using the whole body height. Pattern clips: the notes as thin (2–3 px)
  light bars.

What LYNX does now (own drawing code, LYNX palette, `PlaylistComponent.cpp` "clip look" + `WaveformView.cpp`):

- `paintClipFrame`: body = clip colour 72 % towards `bg1`; header = clip colour 50 % towards a neutral slate,
  with a 12 % lighter top and a darker bottom edge; LYNX's own kind glyphs (stepped bars / mini waveform /
  rising curve); name in bold caption type; 1 px `bg0` edge, rounded `radius::sm` corners (LYNX style).
- Audio clips take their channel's colour (default: the theme's audio blue).
- `paintCleanWaveform`: per physical pixel, the peak of the column (at least 1.5 summary columns wide, so
  zooming in shows a smooth outline instead of single cycles), a 1-2-1 blend that never drops below the true
  peak, mirrored and filled as one anti-aliased path in `clipContentColour` (clip colour 70 % towards white).
  Display gain: normalised, at most +12 dB, times the clip gain. Stereo lanes still optional.
- The waveform follows Reverse and the stretch (`clipops::framesPerTick`).
- Pattern clips: 2 px rounded note bars spread by pitch (min. 6-key span, centred), repeating along clips
  longer than the pattern.
- The spectral colouring is now opt-in (Options > Waveforms); the old settings key is ignored so every user
  gets the clean style once.

## 3. Clip menu

FL's audio clip menu (two columns): Preview, Muted, Rename and color, Change color, Random color, Change icon,
Select source channel, Channel settings, Make unique, Select all similar clips, Delete, Properties, Region
(Select region, Chop: auto-slicing modes, time/repeating/marching/pattern chops), Sample (Make unique as sample,
Edit sample, Pitch-correct, Time-warp, Extract stems, Deverb, Detect tempo, Fit to tempo), Automation
(Automate, Crossfade with), Fades (Reset fades, Fade in/out curve).

LYNX's clip menu (`PlaylistComponent::buildClipMenu`, every action one undo step):
Rename and color… (pattern or audio channel), Make unique (pattern, audio: copy of the channel, automation),
Select all clips of this source, Split here, Chop to grid, Merge clips (same source, same track, continuing
time and offset), Reverse (audio; the offset is mirrored so the same audio stays in the clip), Normalize,
Clip volume ▸, Fit to tempo ▸ (power-of-two beats at the project tempo, keep pitch or resample; half / twice
as long; original speed — every clip of the file keeps showing the same audio), Remove fades, Open in a new
Sampler channel, Show the file in Explorer, Open in Channel Rack / Piano Roll (pattern), Edit points / Delete
automation (automation), Mute, Cut, Copy, Paste here, Delete.

Left out (not doable well now): Preview item (Alt+right-click previews), Change icon, Properties, Detect
tempo, pitch correction / time warp / stems / deverb, Automate, Crossfade with, fade curves, auto-slicing
chop modes, Select region, Show in Browser (Explorer instead).

The actions are exercised through the menu itself by `--snapshot` (`-clip-actions.txt`), the core operations
by `tests/UiFixTests.inc`.
