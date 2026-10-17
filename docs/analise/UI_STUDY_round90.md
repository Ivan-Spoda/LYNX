# UI study, round 90 ("uilook90"): FL Studio 2026 vs LYNX on a real project

What an FL user notices first when the same project is opened in FL Studio 2026 and in LYNX, and which of those
differences round 90 fixed. Clean-room: only layout, behaviour and what a user sees are compared; LYNX keeps its own
dark, flat, red-accent look. No FL artwork, icons or code were copied. FL captures stay outside the repository
(`tmp/r90`, `lynx-test/uilook90/flref`).

## Method

- [EXEC] FL Studio 2026 (FL64.exe) opened `Data/Demo projects/Demo songs/Ookay - Thief.flp` at 1920x1080 (125 %
  Windows scaling); screens captured with the PowerShell CopyFromScreen helper: Playlist (start-up), Channel Rack
  (F6), Mixer (F9), Piano Roll (F7, pattern "Synth Bass #2"), a channel's plugin window (Harmless), the rack after
  the plugin opened (piano-roll preview row). One FL instance under `~/.cache/lynx-fl.lock`, closed afterwards.
- [EXEC] LYNX `--snapshot` of the same .flp at 1920x1080 (main 65e30ca = "before"; this branch = "after").
- [DATA] FL's form resources (`FLEngine_x64.dll` RCDATA `TPF0` streams) as inventoried in
  `docs/analise/UI_windows_inventory.md` (round 81): `TStepSeqForm` "Channel rack", `TFXForm`, playlist/piano roll
  code forms. Used for names and window roles only.
- [DATA] FLP playlist track record 0xEE (70 bytes in FL 2026 projects): u32 track index (1-based), **u32 colour
  0x00BBGGRR** (0x565148 = FL's default grey, the same value as the default pattern colour), u32 icon, u8 enabled,
  then a float 1.0 and flags (grouping/height not decoded: "not confirmed"). Checked on the 20 demo songs:
  10 275 track records, the default grey on all but the named, coloured ones (Thief: DRUMS red, BASSES blue,
  VOCALS green, FX yellow, TEMPO red).

## What a FL user notices (biggest first)

| # | Area | FL Studio 2026 | LYNX before (65e30ca) | Round 90 |
|---|---|---|---|---|
| 1 | Desktop after opening a project | Playlist fills the work area and shows the whole arrangement (Thief: ~103 bars across 1 620 px); the Channel Rack is a compact window (~490 px, 16 steps visible, horizontal scrollbar for longer patterns) | The Channel Rack grows to the pattern's full step count (48 steps, ~1 370 px wide) and hides nearly the whole Playlist; the Playlist opens at its default zoom (about 3 bars visible beside the rack) | **Fixed**: the rack's fitted width is capped at 32 steps (scrollbar beyond); an opened project zooms the Playlist out to show the whole song when it is longer than the view |
| 2 | Channel Rack rows with piano-roll notes | The step buttons are replaced by a miniature of the notes (pitch and length), clicking it opens the Piano Roll | Step buttons with a few cells lit where notes start; pitch and length invisible, a click toggles a step and edits the melody | **Fixed**: preview strip (notes drawn by pitch/length, LYNX colours), click opens the Piano Roll for that channel; plain step rows keep the buttons |
| 3 | Channel colours in the rack | The channel name button is filled with the channel's colour; colour is the main way channels are found | Neutral grey button with a 4 px colour strip | **Fixed**: the name button is tinted with the channel colour (dark, toned down: LYNX look), the strip stays |
| 4 | Mixer insert colours | Each insert strip is tinted with its colour, so groups (drums red, vocals green...) read at a glance | Neutral strips with a 3 px colour band | **Fixed**: strips tinted with the insert colour (selection/hover states kept) |
| 5 | Playlist track colours | Track headers carry the track colour from the project (group headers, coloured blocks) | Every header the same grey; FLP track colours were not imported | **Fixed**: colour imported from FLP 0xEE, kept in .lynx, header tinted, "Rename and color..." in the track menu |
| 6 | Playlist track groups | Group tracks (BASSES, VOCALS, FX) with collapse arrows | No grouping | open (FLP group fields not decoded) |
| 7 | Rack: audio-clip channels | Step buttons shown on every channel | Text "audio clip - plays from the Playlist" | open (deliberate LYNX choice; FL's behaviour for steps on audio clip channels "not confirmed") |
| 8 | Rack title | Channel filter group ("All") selector, swing knob, graph/keyboard view buttons | Pattern name and swing knob | open (filter groups need the FLP filter data) |
| 9 | Mixer side panel | Always 10 numbered slots with mix knob and LED, built-in 3-band insert EQ, input and output selectors | Effect list (click to add) with the selected effect's controls below; mix knob per slot | open: insert EQ panel and output selector; input selector belongs to round 88 |
| 10 | Mixer strip names | Vertical names, compact window (~480 px tall) | Horizontal truncated names, very tall faders when maximised | open (layout choice) |
| 11 | Piano Roll | Overview strip with a note preview under the title; the view opens fitted to the pattern | No overview strip; opens at the default zoom | open |

Not compared in depth: Browser (FL 2026 docks it left, LYNX too), plugin wrappers (LYNX draws its own editors).

## Changes in round 90

- **Channel Rack** (`ui/ChannelRackComponent.*`, `core/RackOps.*`): `showsNotePreview` decides per row whether
  the channel's notes are plain steps (on the step grid, one step long, key 60, no slide/porta); otherwise the row
  draws a miniature (beat/bar lines, notes by time, length and pitch mapped onto the row's key range, channel
  colour) and a click opens the Piano Roll; drag-painting steps skips those rows; the hint says what a click does.
  Name buttons are tinted with the channel colour. `MainComponent::fitChannelRack` caps the fitted width at 32
  steps (`widthForSteps`), longer patterns scroll.
- **Playlist** (`ui/PlaylistComponent.*`): `showSongAfterOpen` after opening a .flp/.lynx (scroll to the start,
  zoom out to the whole song when it is longer than the view; waits for a layout when the panel has no size yet).
  Track headers tinted with the track colour; the track menu's "Rename and color..." (and double-click) use the
  shared name-and-colour dialog.
- **Model / files**: `PlaylistTrack::colour` (0xFFRRGGBB, 0 = default); FLP 0xEE colour at offset 4 imported
  (FL's default grey 0x485156 = no colour); .lynx `playlistTrackColours` list ([index, 0xRRGGBB]; older versions
  ignore it); undo history names the change "Playlist tracks".
- **Mixer** (`ui/MixerComponent.cpp`): strips tinted with the insert colour (selected strips a little more).
- Tests: `uilook90_rack_note_preview_rule`, `uilook90_flp_track_colours` (Thief: DRUMS 0xFF770E0B, BASSES
  0xFF16296C, default tracks uncoloured); the selftest's project round trip checks a track colour.

## Where I stopped / next steps

- Next candidates: items 6, 8, 9, 11 above.
