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

So LYNX's old Zoom tool (click zooms out) was FL's behaviour; the user expects a click to zoom in. See §2.3.

## 2. What LYNX does now

### 2.1 Audio input through the mixer
- **Model** (`core/Song.h`, `core/LiveInput.h`, tested in `tests/InputTests.inc`): each `MixerTrack` has an input
  source (`inputChannel`, 0-based device channel, -1 = none; `inputStereo` = that channel and the next) and a
  monitoring mode `InputMonitor` off / when armed / auto / on (FL's four names). Default: auto. "Auto" is not
  documented by FL [model]: LYNX hears the input while the insert is armed, except during playback without
  recording (so a take plays back without the live input on top). Saved in `.lynx` (`input`, `inputMono`,
  `monitor`); compared by the undo history. FLP import of FL's insert input: not done (field not located).
- `inputSources(names)`: the device's stereo pairs first ("In 1 - In 2", "In 3 - In 4", ...), then every channel
  alone, as FL's input box lists them; works for ASIO (many channels) and for WASAPI/DirectSound devices (usually
  one or two). `inputSourceName` names a stored source even when another device is open ("In 5 - In 6").
- **Engine**: the audio callback maps the device's open input channels (`audioDeviceAboutToStart`: device channel
  -> callback index) into `LiveInput`, which the renderer reads in the same call. `RackRenderer::mixLiveInput`
  adds each insert's source into the insert's buffer before its effects (so the input goes through effects, EQ,
  fader and routing), when `inputMonitored(...)`; the input peak is metered per insert either way (FL's
  "Always show external input levels"). Arm states live in atomics (`LiveInput::armed`).
- **Device input**: `AudioEngine::openAudioInput` opens **all** input channels of the chosen (or default) input
  device of the current driver: for WASAPI/DirectSound the separate input device (probed for its channel count),
  for ASIO the same device's inputs. It runs when an input is chosen in Settings › Audio, when an insert gets an
  input source, when an insert is armed, and when a project with insert inputs is opened. Before, Settings opened
  channels 1-2 only and nothing else opened the input.
- **Takes**: one WAV per armed insert with an input source (up to 16 at once; "External input only", pre
  effects), written by a `ThreadedWriter` from the audio thread (try-lock only). On stop each becomes an audio
  channel routed to its insert plus a Playlist clip on its own new track, latency-compensated (as before).
  Without armed inserts, Settings' "Record input audio" still records channels 1-2 into a new insert.
  Arming is not saved; opening a project disarms everything.
- **Problems are explained** (`AudioEngine::RecordingNotice` codes -> translated text in `mixerinput::explain`):
  no device, no input device, no input open, armed insert without input (FL's "Audio input needed - Please select
  an audio input first."), driver error; device problems offer an "Audio settings..." button.
- FL's other "Record" positions (external and mixer input, post effects, post EQ, post level, post track), input
  latency per insert, 16/32-bit take formats and "Render armed tracks to wave file(s)": not done.

### 2.2 UI
- **Mixer side panel** (above the effect slots, where FL has `MixInSelect`): a row with the **arm** switch and the
  **audio input source** box (mic icon, source name or "No audio input", a stereo input level bar along its
  bottom). Clicking the box opens the input menu: (none), stereo pairs, single channels "(mono)", "Monitor external
  input" Off / When armed / Auto / On, "Audio settings (input device)...". Right-click on an arm switch opens the
  same menu.
- **Strips**: the arm dot in the header; arming an insert without an input opens its input menu at once (hint "Audio
  input needed: choose this insert's input, then it is armed") and arms it when one is picked. A thin input level
  bar under the header of every insert with an input source (click: input menu). Strip menu: "Audio input:
  <name>..." and "Arm / Disarm recording".
- **Record button**: clicking it (to start recording) asks "What would you like to record?" (FL's dialog, own
  layout): "Audio, into the Playlist as an audio clip" (records audio only; arms the selected insert — or the first
  with an input, or a free one — showing the Mixer, and asks for its input when it has none), "Notes, into the
  current pattern", "Everything"; "Don't ask again". FL's "Audio, into the Edison audio editor/recorder" is left
  out (LYNX has no audio editor yet). The record button's right-click / options menu now starts with FL's
  **recording filter** (Notes, Audio) plus "Ask what to record when recording starts", "Arm the selected mixer
  insert for audio (choose its input)..." and "Audio settings (input device)..."; FL's Automation and Clips
  filters have no LYNX counterpart yet. Saved in `ui-options.txt`.
- **Settings › Audio**: choosing an input device opens all its channels; a line lists the open input channels
  ("Input channels (2): Microphone 1, Microphone 2", or how to get some); the recording note points to the Mixer.
- **CLI**: `--devices` also lists input devices; `--devicetest type device report [input]` opens an input too
  ("*" = the driver's default input / the ASIO device's own) and reports the input devices, the channel names,
  active inputs, input latency and each channel's peak over 1 s (result FAILED if no input reached the callback).

### 2.3 Zoom tool (Playlist and Piano Roll)
FL's documented behaviour is "left-click on background zooms out" (§1), which is what LYNX did and what the user
found backwards. Per the user's request LYNX now deviates: **click zooms in** (×2 around the point), **drag a range
zooms to it**, **right-click zooms out** (×½ around the point; Ctrl+right-click keeps zoom-all / back). Tool tip and
hint bar text updated (English + pt-BR).

### 2.4 Clip menu
FL: the clip menu opens from the icon at a clip's top left; right-click deletes clips in Draw mode. LYNX had the
icon (a hover chevron over the kind glyph) and also opened the menu on a right-click anywhere on the name strip.
Now right-click deletes on the name strip too (FL), the menu opens with a click or right-click on the icon, and the
hint bar says so when the mouse is over the header ("The icon at the top left opens the clip menu (make unique,
rename...)") or the icon ("Clip menu: [LMB] or [RMB] opens it (make unique, rename and colour, chop, select
source...)").

## 3. Verification

- [EXEC] `tests/InputTests.inc`: source lists (pairs then monos, odd channel counts, single-channel devices), names
  of stored sources, the four monitoring modes, `inputChannelsOf`, and the renderer: an insert's stereo input
  reaches the Master at unity, a monitoring-off insert is metered but silent, auto monitoring follows arm /
  playback / recording, a muted insert stays silent, no input open = nothing.
- [EXEC] `LYNX.exe --devicetest <type> <device> report "*"` on this machine (USB Audio CODEC + FL Studio ASIO):
  Windows Audio — input "Microfone (USB Audio CODEC )", 2 channels "Input channel 1/2", 99 callbacks, input peak
  -43 dB (room noise); DirectSound — "Driver de captura de som primário", channels Left/Right, -43 dB;
  ASIO "FL Studio ASIO" — In 1 / In 2, active, callbacks OK. `--devices` lists the input devices of each driver.
- [EXEC] Snapshots (`--snapshot`): `-mixer-input.png` (insert 3 armed with input "In 1 - In 2": the side
  panel's input row), `-record-dialog.png`, `-settings-audio.png` (input channel line). Shots in tmp/r88 only.
- Not tested live: a real take through the new per-insert writers with a microphone (needs a person at the
  machine); the code path is the one of the old single take, run once per armed insert.

## Where I stopped / next steps

- Done; landing via tmp/coord/land.sh input88. Open: FL's other record positions (post FX/EQ/fader/track), FLP import
  of insert inputs, per-insert input latency, Edison-like audio editor target, automation/clips recording filters.
