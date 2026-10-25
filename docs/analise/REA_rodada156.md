# REA round 156 (hwout156): hardware output targets per insert, mono outputs

2026-10-24. A commenter asked: "can we finally output a mono signal to hardware?". In FL Studio a mixer track's
"Audio output target" lists only the device's stereo pairs; sending one track to a single interface output (a mono
monitor, a mono outboard insert, a click to a drummer's headphone amp) is a long-standing complaint. Until this
round LYNX had no per-insert hardware output at all: the output box showed "(none)" on inserts and the first device
pair on the Master.

Evidence markers: [DOC] Image-Line's online manual (behaviour only); [DATA] read from .flp files with a small
clean-room event dumper (no FL code); [CODE] LYNX's own source; [model] LYNX's own design choice. Nothing of FL was
decompiled in this round.

## 1. What FL does

- [DOC] Mixer "Audio input / output routing"
  (https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_iorouting.htm): "You can route any
  Mixer track to any ASIO output"; "By default, the Master Mixer track 'Output' is routed to the audio devices main
  outputs"; "It is even possible to set two or more Mixer tracks to receive the same input or send to the same
  output" (so outputs add); "When '(none)' is shown, this means no external audio device input or output is selected".
- [DOC] Same page: "By default the SOURCE track will maintain routing to the Master track … If you don't want to create
  a parallel path to the Master Mixer track" → **an insert with an output target still feeds the Master** (and its
  sends) unless the user unroutes it. LYNX does the same: the hardware output is a parallel path.
- [DOC] With the Primary Sound Driver only one track can use the primary output; with ASIO any track can use any
  output. LYNX has no such limit (it adds the buses itself in its device callback, any driver).
- [DOC] The manual never mentions mono outputs: FL lists pairs only. **Mono outputs (sum / left / right) are a LYNX
  extra.**

### .flp event ([DATA])

- `0x93` (DWORD) after the `0xEC` that opens an insert = its output target: `-1` = (none), `0` = the device's first
  stereo pair. REA_rodada2 §… already noted 0x93 as the "physical output" that closes the insert record.
- Survey of all 1162 .flp files on this PC (FL Data demos/templates, the girlfriend's projects on D:\lynx-work, tmp/gf):
  every Master is `0`; every insert is `-1` except one: insert 97 of the demo "Right Night feeling" (`0`, an empty
  insert). No other value occurs, so the meaning of 1, 2, … (FL's index into its own device list) is **unverified**.
- Import: an insert with `0` gets the stereo pair 1-2 (FL's meaning); other values are not imported; a Master set to
  `-1` is not imported either (the project would play silent on any device). [model]

## 2. What LYNX does now [CODE]

- `MixerTrack::outputChannel` / `outputMode` (src/core/Song.h): `outputDefault` (−2; Master = first pair, inserts =
  through the Master only), `outputNone` (−1) or a 0-based device output channel; mode `stereo` (channel and the next),
  `monoSum` ((L+R)/2, so a centred source keeps its level), `monoLeft`, `monoRight`.
- Optional by design (user requirement): the default is exactly today's behaviour; old and new projects play the
  same unless the user picks a target. .lynx saves `"output"` / `"outputMode"` only when set (old files unchanged).
  The mixer-track state file (Save mixer track state) does not carry it (it is routing, like the input).
- Menu (src/ui/MixerComponent.cpp `mixeroutput::showMenu`, the output box of the effects panel): "(none) - through
  the Master only" (ticked by default), the current target, a submenu **"Send to hardware output (optional)"** with a
  one-line explanation, "Stereo pair" items (1-2, 3-4, …) and "Mono (one output)" items, one per channel, each with
  "Sum of L + R (mono)" / "Left side only" / "Right side only"; "Back to Master only"; "Audio settings...". The Master:
  "Out 1 - Out 2 (default)", "(none)", the same submenu. A 2-output device still shows the menu with a short note.
  Listing order mirrors `inputSources()` (src/core/LiveInput.h): pairs first, then every channel alone.
- No trip to Audio settings: choosing a channel the device has but did not open opens it
  (`AudioEngine::openTargetedOutputs`, also on project load).
- Strip badge: an insert with a target shows a small "→ 3 m" / "→ 3-4" pill at the top of its fader; inserts left
  at default look exactly as before. A target the device doesn't have (or could not open) reads "(missing)" in the
  box and "?" on the badge, and outputs nothing.
- Engine: `RackState::hwRoutes` (built with the PDC plan) lists the targeted inserts; after the Master is mixed, the
  renderer adds each one's post-fader output into `HardwareOutput` buses (src/core/HardwareOutput.h, one per device
  channel, preallocated 64 × 8192 floats, a dirty mask, no locks/allocation). Several inserts on one channel add.
  `AudioEngine::routeOutputs` (device callback, after the graph) moves the Master to its own target when it isn't the
  default, then drains the buses into the open output channels (device channel → callback index map built in
  `audioDeviceAboutToStart`, as for inputs); closed/missing channels drop their sound. The buses work only while a
  device plays (`active`), never during an export (the export is the Master).
- Delay compensation: each route is delayed by `pdc.total − outLat[track]` (a `DelayLine` reused across edits like
  the send delays), so a direct output lines up with the Master output. Without plugin latency nothing is delayed.

## 3. Tests

- tests/HwOut156Tests.inc: target list and names ("(missing)"), routing math (pair, mono sum, L only, R only, adding),
  renderer → buses with two inserts sharing one channel, the Master still getting both, a pair whose second channel is
  not open (dropped), buses cleared after a drain, muted insert silent, out-of-range channel ignored, .flp `0x93`
  import.
- `--selftest` `hardware_outputs=OK`: .lynx save/load round trip of every mode and of the Master set to none; a default
  project writes no `"output"` key and loads with the defaults.

## Where I stopped

- Done: model, engine, menu, badge, pt-BR, .lynx field, .flp 0x93 (values −1/0), tests, menu screenshot (see the
  report; tmp/hwout156/).
- Not done: FL's meaning of 0x93 values ≥ 1 (needs an FL session with a multi-output ASIO device: set targets, save,
  read the values); a strip-level hover hint for the badge; the metronome/preview follow the Master's target (they
  are part of the graph output) — no separate target for them.
