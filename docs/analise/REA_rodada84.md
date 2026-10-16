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

## Where I stopped / next steps

- (in progress)
