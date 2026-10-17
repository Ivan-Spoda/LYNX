# Round 84: Export options, New project, Clip properties + arrangements, Plugin performance monitor, Touch keyboard

Branch `feature/dialogs84`. English, clean-room: FL Studio was used only for window names, option labels and the
behaviour a user sees; LYNX's windows are its own layout in its own style (dark, red accent).

## 1. FL Studio's windows (evidence)

- [DATA] REA `inspect_pe_resources` on `FLEngine_x64.dll` (FL Studio 2026; evidence `ev_396266e7…`): RT_RCDATA
  (type 10) forms `TWAVRENDERFORM` (index 263), `TRENDERFORM` (247), `TMEMP3OPTIONSFORM` (196), `TMEOGGOPTIONSFORM`
  (198), `TNEWPROJFORM` (213), `TPLCLIPPROPFORM` (217), `TPLMERGEARRANGEMENTFORM` (218), `TPLUGINMONITORFORM` (221),
  `TTOUCHKEYBFORM` (260). Component trees read with round 81's binary-DFM reader (`tmp/r81/py/dfm.py`, captions,
  hints and item lists only).
- [DATA] "WAV rendering" (`TWAVRenderForm`): Project type: Mode Song / Pattern; Tail "Cut remainder" (don't render
  the tail, for loops) / "Leave remainder" (songs) / "Wrap remainder" (loops); "Total time:"; a warning when the
  Master track is muted. Miscellaneous: save tempo information, note / loop / playlist markers, "Trim PDC silence"
  (on), "Split mixer tracks" (with Include Master / Current), "Enable master effects", "Enable insert effects".
  Quality: resampling (2-point linear … 512-point sinc), "Dithering" (16-bit output), "HQ for all plugins", "Disable
  maximum polyphony". Output format: WAV / MP3 / OGG / MID / M4A / FLAC check boxes (several at once), WAV bit
  depth 16 int / 24 int / 32 float, MP3 bitrate slider, OGG bitrate slider, FLAC compression level slider and bit
  depth 16 / 24, channels Stereo / Mono (merged) / Mono (left only) / Mono (right only). Upload panel (cloud) — not
  applicable to LYNX.
- [DATA] "MP3 export" / "Ogg Vorbis export" (Edison's): one bitrate slider; OGG adds "Quality" and bit depth 16/24.
- [DATA] "New project" (`TNewProjForm`): Time settings (Timebase PPQ 24…960, numerator 1–16, denominator 2/4/8/16),
  Name and location (name, folder, "Create new project folder", "Copy used samples to the project folder"),
  Information (title, author, genre, web link), Create / Cancel / Skip, "Don't show this in the future".
- [DATA] "Clip properties" (`TPLClipPropForm`): Levels (pitch, fine, gain, pan, reverse, mute — for several clips:
  "Don't change / Mute all / Unmute all / Flip state", Set / Offset / Increment), Time (start time, duration, "Reset
  stretch factor"), "Channel settings", Apply / Reset.
- [DATA] "Merge arrangement" (`TPLMergeArrangementForm`): source arrangement, mode Insert / Merge / Replace,
  position Start / Current / End, Accept / Reset.
- [DATA] "Plugin performance monitor" (`TPluginMonitorForm`): a grid of plugins, Find, "Freeze", "Reset on
  transport".
- [DATA] "Touch keyboard" (`TTouchKeybForm`): two keyboards (white key 33, black key 24 units), pads mode (rows ×
  columns), show note labels, velocity, scroll lock, root note, key size (extra small … extra large), keyboard
  height (full / half / quarter), presets.

## 2. What LYNX does now

### Export window (`src/ui/ExportDialog.cpp`, core `src/core/ExportOptions.*`, engine `src/engine/Exporter.*`)
- Left column "What to render": range Song (Playlist) / Current pattern / Playlist selection (the time selection =
  the loop region; clips crossing its edges are cut there, offsets follow), how many times it plays (1–16; the clips
  are repeated in the render copy, so releases and effects carry over from one pass into the next), tail Cut / Leave /
  Wrap remainder (wrap: the rendered tail is added back from the start, wrapping more than once if longer than the
  loop, then the file is cut to the exact length), longest tail 3/6/12/30 s, files: one (the mix) / one per mixer
  insert / one per channel (each channel alone through its own mixer path), "Also the full mix".
- Right column "Output format": WAV / MP3 / OGG / FLAC. WAV 16 / 24 integer or 32 float; MP3 96–320 kbps CBR
  (LAME, joint stereo, ID3v2 title/artist when ASCII; project rates above 48 kHz are encoded at 48 / 44.1 kHz by LAME's
  resampler); OGG 64–500 kbps (JUCE's Vorbis encoder settings); FLAC 16 / 24 bits, compression 0–8. Sample rate
  44.1–192 kHz, channels stereo / mono mixed / left / right, dither off / TPDF / TPDF + noise shaping (16 and 24-bit
  integer output only).
- Mix: normalize (peak to 0 dBFS, measured on what is kept after the tail step), Master effects on/off, trim the PDC
  silence. A status line shows the total time (+ tail), the MP3 rate actually used, the stem count, and warnings
  (Master muted, no selection). Choices are remembered in `%APPDATA%\LYNX\export-settings.txt`.
- Engine: when nothing needs the whole file first (WAV/FLAC/OGG, stereo, no normalize, tail "leave") the export
  streams into the writer exactly as before (the command line, self-test, FL comparisons and "Render to audio" use
  `ExportSettings`, which maps to these options with the old behaviour, dither only for 16-bit WAV). Otherwise
  `renderOffline` hands every block to a capture callback, then tail → normalize → mono → dither (same 512-sample
  block order) → writer or MP3 encoder.
- `Dither` gained a no-shaping mode (plain TPDF).

### MP3 encoder (`src/core/Mp3Encoder.*`, `third_party/lame`)
LAME 3.100's `libmp3lame` (LGPL-2.0-or-later, unmodified sources, encoder only, `src/config.h` written for LYNX), built
by build-local.sh, build-windows-zig.sh and CMake (`lame_encoder`). `scanMp3` parses frame headers for the tests.

### New project window (`src/ui/NewProjectDialog.*`, core in `src/core/Templates.*`)
Templates: LYNX's default song, the built-in ones (Empty, Basic beat, Trap with 808, House) and the user's `.lynx`
files in `Documents\LYNX\Templates` (File › Save as template... writes there; "Open the templates folder").
Name, folder (Choose...), "Save it there now", "Create a folder for the project" (path preview), tempo and time
signature (follow the template until changed), title / author / genre / web link, "Don't show this window for File ›
New" (then File › New makes the default project at once; File › New project... always opens the window). The last
folder, author and choices are remembered (`%APPDATA%\LYNX\new-project.txt`). FL's PPQ choice is not offered (LYNX has
one internal timebase).

### Clip properties (`src/ui/ClipProperties.*`, core `src/core/PlaylistOps.*`)
Clip menu › Properties...: the source's name and colour (pattern / audio channel / automation; they change every
clip of that source), which source the clip uses, track, start / length / offset as bar:beat:tick text, muted; audio
clips: gain (dB), fades (beats), the channel's pitch and stretch-to-beats with keep-pitch. Several clips selected:
mute (don't change / mute all / unmute all / flip) and gain (set / change by) for all of them. Reset puts the clip back
as it was when the window opened. Every change is one undo step; the window follows undo and closes if its clip
disappears.

### Arrangements (`showArrangements`, Playlist arrangement menu › Arrangements and merging...)
List with clip count and length; Show (make current), New, Duplicate, Rename..., Remove, Move up/down. Merge into
the current arrangement: source, mode Insert (clips crossing the position are split, everything after it and the
markers move later by the source's length) / Merge (laid over) / Replace (the source's length is cleared first,
crossing clips cut), position Start / Playback position / End (next bar line after the last clip).

### Plugin performance monitor (`src/ui/PluginMonitorWindow.*`, core `src/core/PluginMonitor.*`)
`RackRenderer` now times each channel's instrument and each effect slot (`ProcessTimes`, relaxed atomics,
steady_clock; only while the window is open, otherwise one relaxed load per block). The window shows, per instrument
and effect, now / average / peak as a share of real time (100% = one core for the audio's duration), the whole rack's
load, Find, Plugins only, Freeze, Reset on play, Reset, sortable columns.

### Touch keyboard (`src/ui/TouchKeyboard.*`, core `src/core/TouchKeyboard.h`)
View › Touch keyboard: plays the selected channel (records like the typing piano). Velocity from where the key is
touched (0.2 at the top → 1.0 at the bottom, square-root curve) or fixed; multi-touch (one HeldNotes tag per pointer);
dragging glides key to key (the old note stops first); octave − / + (keeps every key inside 0–127, lowest key a C),
small / medium / large keys (35 / 28 / 21 white keys), note names. No stuck notes: every note goes through HeldNotes
and is stopped on mouse up, on leaving the keys, on focus loss, when the window hides or closes, when the selected
channel changes and before an octave shift or key-size change.

## 3. Tests (`tests/DialogsTests84.inc`)
Options text round trip and clamping; render song for song / pattern / selection (edge cuts), loops, tail, master
effects; tail cut / leave / wrap (multi-wrap, padding), normalize gain, mono; stems (tracks, channels, muted clips,
file names); plain TPDF dither; MP3 frames (44.1 kHz stereo 320 kbps, 48 kHz mono 128 kbps, 96 → 48 kHz); monitor
sources, loads, smoothing, averages, reset, find, slot reuse; clip properties read / apply / clamps / groups;
bar:beat:tick text; arrangement insert / merge / replace / clear range / move / rename / duplicate; new project path
and setup; touch keyboard geometry, velocity, octave clamp and note-on/off balance through HeldNotes.

## Where I stopped / next steps

- (in progress) builds and snapshots.
- Not done: FL's export of several formats at once, MIDI export from the render window, "HQ for all plugins" /
  resampling quality / polyphony switches, markers and tempo in the WAV file; New project PPQ (LYNX has one
  timebase), "copy used samples to the project folder"; Clip properties pan (LYNX clips have no pan) and per-clip
  pitch (it is the audio channel's); Touch keyboard pads mode and presets.
