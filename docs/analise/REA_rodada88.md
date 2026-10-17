# Round 88: audio input per mixer insert, recording dialog, Zoom tool, clip menu discoverability

Branch `feature/input88`. English, clean-room: FL Studio was studied for names, options and behaviour a user sees;
LYNX's controls are its own layout in its own style. Work dir `tmp/r88`.

User feedback on v0.1.5: (1) "there is no microphone selection" (Settings › Audio had an Input device box and a
"Record input audio" switch, but nothing in the mixer); (2) the Zoom tool zooms OUT on a click; (3) the clip menu
(Make unique…) is hard to find.

## 1. FL Studio (evidence)

- [DATA] `FLEngine_x64.dll` (FL Studio 2026) RT_RCDATA form `TFXFORM` (the Mixer; read with round 81's DFM
  reader, `tmp/r81/dfm_all.txt`): the track side panel has `MixInSelect` (hint "Audio input source") at its top,
  `MixOutSelect` ("Audio output target") at its bottom, `MixInBtn` ("Enable/disable direct input recording": the
  arm button, a disk glyph) and `InputLatencyBtn`. Mixer menu "Disk recording": "Render armed tracks to wave
  file(s)…", "Auto-unarm", "Auto-create audio clip" ("Record / render to audio clip (song mode only)"), "Latency
  compensation", "Ignore silent takes", "Auto name playlist tracks", "Group loop recorded takes", "Apply track level
  and mute when recording", format 16-bit int / 24-bit int / 32-bit float. Track menu: "Arm selected track(s)",
  "Disarm selected track(s)", "Open audio editor and record". View menu: "Always show external input levels" (hint
  "Show external input levels even when monitoring is off").
- [DATA] Popup `AudioRecorderRoutingMenu` (same form): "Record": "External input only" (hint "Only record audio
  from the selected external input"), "External and mixer input", "Post effects", "Post EQ", "Post level and
  panning", "Post track"; "Monitor external input": "Off", "When armed", "Auto", "On". The Playlist's audio track
  header has the same menu (`PLTrackMonitorMenu`) plus "Select audio input" / "Arm recording" / "Disarm recording".
- [DATA] Strings in `FLEngine_x64.dll`: the recording dialog is titled "Recording" with the question "What would
  you like to record?" and the choices "Audio, into the Edison audio editor/recorder", "Audio, into the playlist as
  an audio clip", "Notes and automation", "Everything"; notes "Right-click the record button to display the
  recording filter." Messages: "Audio input needed|Please select an audio input first.", "Recording aborted|You
  didn't select an audio input so recording couldn't start."
- [DATA] Main form popup `RecMenu` (the record button's right-click): "Recording filter": "Automation", "Notes",
  "Audio", "Clips"; options "Recording starts playback", "Disarm on stop", "Enable recording markers", quantize
  (note start / end / leave duration, record to step sequencer, automations), "Midi loop recording (song mode)".
- [DOC] FL manual, Piano roll: "Zoom to selection (Z) – (Left-click and drag) to zoom to selection. (Left-click on
  background) zooms out. In other modes use (Ctrl+Right-Click) and drag to zoom. (Right-Click) toggles
  zoom/unzoom." Playlist: the clip menu is "at the top-left corner of each Clip. Click to open"; in Draw mode
  "Right-Click to delete Clips. Right-Click and drag to delete multiple clips."

So LYNX's old Zoom tool (click zooms out) was FL's behaviour; the user expects a click to zoom in. See §2.2.

## 2. What LYNX does now

(filled in as the work lands)

## Where I stopped / next steps

- Started: doc, evidence. Next: core input sources + monitoring, engine live input, mixer panel UI.
