# REA round 104 ("flui104"): FL Studio screens, round 2 - Playlist groups, Channel Rack filters, Mixer panel

Continues docs/analise/UI_STUDY_round90.md (items 6, 8, 9 of its table). Clean-room: only layouts, behaviour and
file fields are compared and described in our own words; LYNX keeps its own dark, flat, red-accent look. No FL
artwork, icons or code were copied. FL captures stay outside the repository (`tmp/flui104/flref`).

## Method

- [EXEC] FL Studio 2026 (FL64.exe, 25.2.3 demo project files) opened `Ookay - Thief.flp` at 1920x1080; screens and
  menus captured with a CopyFromScreen helper (right-click and key support added: `tmp/flui104/fl.sh`), under
  `~/.cache/lynx-fl.lock`, one FL instance.
- [EXEC] Hints read from FL's hint bar while hovering each control (mixer panel).
- [DATA] FLP fields located by toggling one thing in FL, saving a copy and diffing the events
  (`tmp/flui104/py/tracks.py`, `chev.py`): collapsed vs expanded group; channel filter groups matched against
  the channels FL lists under each filter.
- [EXEC] LYNX `--snapshot` of the same project (before: main 5dcca34; after: this branch).

## Findings

### Playlist (FL)

- Track header layout: name on the left, a track icon, for a group header a fold arrow (up = open, down =
  folded), and the mute LED on the far right; the selected track is marked next to the LED. Group members are
  indented under their header and the whole group sits on one rounded block in the header's colour.
- A folded group keeps its header; each member shrinks to a thin row (about 7 px at 1920x1080) that still shows
  its clips as thin bars in the grid.
- Click on a header selects the track (clips are not selected); double click opens the source of the track's
  clip (here the pattern in the Piano Roll); the LED mutes; muting a group header greys only the header.
- Track menu (FL order): Rename, color and icon... / Change color... / Random color / Change icon... / Auto name /
  Auto name clips / Reset | Track mode > / Performance settings > | Size > (33, 50, 100, 150, 200 %) / Lock to
  this size | Group with above track / Ungroup (disabled on a header) / Auto color group / Collapse grouped
  tracks | Current clip source > (the track's pattern sources) / Lock to content / Merge clips / Consolidate
  track(s) > / Mute all clips / Unmute all clips | Show ghost notes | Insert one / Clone... / Delete | Move up /
  Move down.
- Picker (left column): three exclusive tabs at the top (patterns, audio clips, automation clips), one list per
  tab; pattern boxes show a faint miniature of the notes; "+" at the bottom of the pattern list; right click on a
  pattern opens the pattern menu (Find first empty..., Rename and color..., Clone, Delete, Move up/down,
  Split by channel, render items...). Selecting a clip of another kind switches the tab.

### Playlist track record 0xEE (FLP) [DATA]

70 bytes in FL 20+ projects (offsets from the start of the record):

| offset | size | meaning | evidence |
|---|---|---|---|
| 0 | u32 | track index, 1-based | round 90 |
| 4 | u32 | colour 0x00BBGGRR | round 90 |
| 8 | u32 | icon index (BASSES 1, VOCALS 0x1E, FX 0x73...) | values differ per group header; meaning of each value not confirmed |
| 12 | u8 | enabled (0 = muted) | round 90 |
| 13 | f32 | size, 1.0 = 100 % | FL's Size menu ticks 100 % on all Thief tracks; other values not confirmed by a save |
| 30 | u32 | trigger sync (5 = "4 beats") | matches the Performance settings submenu of the captured track |
| 38 | u32 | tolerant (1) | ticked in the same submenu |
| 46 | u8 | grouped with the track above | 0 on DRUMS, MELODICS, BASSES, VOCALS, FX, TEMPO and the empty tracks, 1 on every member |
| 61 | u8 | group expanded | 1 -> 0 when FL collapsed BASSES and saved (only byte that changed in the record) |

Other bytes: not confirmed. A grouped track belongs to the nearest ungrouped track above it (the header).

### Channel Rack (FL)

- Title bar: options arrow, a filter-group box ("All"), the title, the swing knob, ..., the graph editor and step
  view switches, close.
- The filter box lists "All", then the project's groups (Thief: Audio, Automation, Unsorted); choosing one shows
  only those channels. "Group selected..." (Alt+G, in the rack's options menu) asks for a "Filter group name"
  and puts the selected channels in it.
- FLP [DATA]: event 0xE7 (text) = one filter group name, in order; event 0x91 (u32, in each channel's section) =
  the channel's group index. Checked on Thief: the channels FL lists under "Audio" have 0, the automation
  channels 1, Synth Bass / Saw Bass / White Noise / Piano Dark 2.
- Channel menu (two columns): left - Piano roll / Graph editor | Rename, color and icon... / Change color... /
  Random color / Change icon... | Load sample... / Cut itself / Show waveform preview | Insert > / Replace > /
  Patcherize / Clone / Delete... | Assign to new instrument track; right - Cut / Copy / Paste | Fill each 2 / 4 /
  8 steps / Advanced fill... / Auto fill | Rotate left / Rotate right | MIDI channel through / Receive notes
  from > | Create DirectWave instrument... / Burn MIDI to >.

### Mixer side panel (FL)

Top to bottom: input row (an icon "Select what is included when recording audio", the input box "(none)",
"Input latency"); 10 slot rows, each an options arrow ("FX slot N options"), the name ("Slot N" in grey italics
when empty; "Effect slot N"), a mix knob ("Mix level: 100%") and an LED ("Mute / solo"); the built-in EQ: a
response graph ("Tweak band N" - its handles drag), three level faders ("EQ band 1 (low shelf) level: 0.0dB"),
a row of frequency knobs ("EQ band 1 (low shelf) freq: 90Hz") and a row of width knobs ("width: 0.27") with the
label "Equalizer"; "Track latency"; the output box ("Audio output target": "Out 1 - Out 2" on Master).
The slot arrow menu: Replace > / Delete... / Patcherize / presets / spare state / parameters / CPU / Move up /
Move down / View editor / Rename, color and icon... / Change color... / Random color / Change icon... / Scaling >
/ Help.

## What LYNX does now (this round)

- Model: `PlaylistTrack::grouped` / `collapsed`; `Song::channelGroups`, `ChannelDef::group`; imported from the
  FLP (0xEE offsets 13, 46, 61; 0xE7; 0x91) and kept in .lynx (`playlistTrackGroups`, `channelGroups`,
  channel `group`). Group rules in `core/NoteEdit` (`isGroupHeader`, `groupHeaderOf`, `groupMembers`,
  `isFoldedAway`, `groupWithAbove`, `ungroupTrack`, `setGroupCollapsed`, `colourGroup`).
- Playlist: FL's header layout (name, fold arrow, LED on the right, accent mark on the selected track), group
  blocks and indented members, folded members as 7 px rows with clip bars; click selects, double click opens
  the source of the track's first clip (empty track: rename); the track menu in FL's order with the items LYNX
  can do (no icons, track modes, performance settings, ghost notes, consolidate, lock to content, merge clips).
  Size is relative to LYNX's default row (38 px). The Picker has FL's three tabs, note miniatures, "+" and the
  pattern menu on right click; it follows the pencil's source kind.
- Channel Rack: the title bar holds the filter box, the swing knob and a graph editor switch (new `Icon::graph`);
  rows outside the filter are hidden; new channels join the group shown; Alt+G "Group selected..."; the filter
  menu also moves the selection to a group, renames or deletes the shown group. Channel menu in FL's two-column
  order (Cut / Copy / Paste of the channel's notes, Cut itself = the channel's own cut group); LYNX's extras
  (settings windows, arpeggiator, swing mix, sort, send to inserts) stay in their places.
- Mixer: slots always show their arrow, name or "Slot N", mix knob and LED (FL's order); the arrow opens the slot
  menu; under LYNX's inline knobs, the built-in 3-band EQ (graph with draggable band handles drawn with the
  mixer's own filters, level faders, frequency and width knobs) and the output target box (Master: the device's
  first active output pair; inserts "(none)").
- Tests: `flui104_track_group_rules`, `flui104_flp_track_groups_and_filter_groups`; selftest round trip of
  groups and filter groups.

Not done (FL items without a LYNX counterpart, or left for later): track icons, track modes/performance
settings, "Lock to this size", the mixer's input/track latency fields, per-insert device outputs.

## Where I stopped / next steps

- Implemented pieces 1-3 above; Windows build + snapshots compared with FL (see shots in
  `lynx-test/flui104/<name>`).
