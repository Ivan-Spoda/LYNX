# REA round 115 ("flui115"): FL Studio main window, round 3 - Browser "Current project", toolbar editing, colours per item

User request (2026-10-20): the Browser on the left should list the audio files the project uses, each with its own
colour and details, as FL's does; FL's toolbar can be rearranged (remove, change, move items) - LYNX should allow the
same, discoverable the same way; and a broad pass over the details of the main window a user notices.

Clean-room: only layouts, behaviour and wording of FL's interface were observed and are described here in our own
words; LYNX draws everything in its own dark, flat, red-accent style. No FL artwork, icons or code were copied. FL
captures stay outside the repository (`tmp/flui115/flref`).

## Method

- [EXEC] FL Studio 2026 (FL64.exe, build 5654) on this PC at 1920x1080, driven under `~/.cache/lynx-fl.lock` with a
  CopyFromScreen/SendInput helper (`tmp/flui115/fl.sh`, `fl115.ps1`: click, right-click, keys, drag, wheel). Projects:
  a copy of the girlfriend's `game over.flp` (tmp/gf/proj2; its saved window layout hides the Browser, and FL could
  not show it again until View > Arrange windows > Desktop (default)) and the demo `Ookay - Thief.flp`.
- [EXEC] FL's hint bar read while hovering each element (it names the element and its keys).
- The REA native-UI tools (`observe_native_ui`, `capture_native_ui_scenario`) are bound to macOS Mach-O targets and
  answered "target_unavailable" for the Windows FL64 process, so the screens were captured with the helper instead.
- [EXEC] LYNX `--snapshot` of the same project before (main 664e83d) and after (this branch).

## Findings

### FL's Browser

- Header row: a tab strip ALL / PROJECT / PLUGINS / PRESETS / SOUNDS / STARRED. Narrow Browser: one icon per tab
  (hints "Browser: ALL", "Browser: PROJECT"...); wide Browser: the words.
- ALL lists "Current project" first, then the content folders (Recent files, Plugin database, Plugin presets, Channel
  presets, Mixer presets, Scores, Audio, Backup, ..., User data); **each folder kind has its own icon and colour**
  (green folders, blue database, pink presets, ...).
- PROJECT shows "Current project" with: History, Patterns, Effects, Generators, Remote control, Initialized controls,
  Project data, Samples. An open section sits on a lighter band.
  - Patterns: one row per pattern, **written in the pattern's colour**. Click / right-click make it the current
    pattern (the toolbar's pattern box follows); right-click menu: Expand all, Collapse all | Rename and color... |
    Insert one, Clone, Delete | Move up, Move down | Split by channel. Hint: the pattern's name.
  - Samples: every audio file the project uses (hint "Samples used in project"); hover: the row turns bold, a star
    (favourite) appears at the right, the hint bar shows the file's full path. Right-click selects it and shows the
    waveform at the bottom (with "44.1 kHz 32") and the menu: Send to selected channel or focused plugin, Open in new
    channel | Send to playlist as an audio clip / audio track / instrument track | Edit in audio editor / pitch
    corrector / time warper | Open in new sampler / Granulizer / Slicer / Slicer 2 / Slicex channel | System: Windows
    shell menu, Open parent folder, Locate file | Tags: Favorite, Tags, Add tag, Edit tags.
  - Effects: "Plugin name (insert name)" with the insert's name dimmed, and a thumbnail of the plugin's window; hint
    "Effect plugins in use" with the count on the right ("77 plugins"). Menu: Expand all, Collapse all, View editor.
  - Generators: "Channel name (insert name)", the icon by kind (keyboard for a synth, wave for a sampler), a thumbnail
    for plugin channels; hint "Generator plugins in use", count "45 plugins".
  - History: the undo steps ("Last reset"); hint "Undo history", value "Max set to 100 levels".
  - Remote control / Initialized controls: the linked / initialised parameters ("Balance - BELL - Volume", "Tempo").
  - Project data: the files in the project's folder ("Found 20 files").
- A folder's right-click menu: Expand all, Collapse all.

### FL's toolbar

- Right-click on an empty part of the toolbar: **Toolbars** - Edit, Separators, Flat buttons, Show FL Cloud status in
  the hint bar, Show detected MIDI chords near the hint bar | Hint bar, Hide (Ctrl+F11); **Factory presets** - Compact,
  Default (hint above menu), Default (macOS), Default, FL Studio 12, FL Studio 24/25 (macOS/Windows), Single line
  centered, Single line.
- Edit: a tray opens under the toolbar with every panel and button, each with its name (Flex space, Fixed space,
  Position panel, Time panel, Pattern panel, Output monitor panel, CPU panel, Menu panel, ...); the ones already on
  the toolbar are dimmed. The tray's footer says "Drag controls to and from the toolbar" and shows the preset name
  with an arrow and a close button. Hovering a toolbar item in edit mode: hint "<item> / Drag to move...". The items
  do not react to clicks while editing.
- Our synthetic drags (SendInput) did not move FL's panels, so the drag itself is taken from the manual [DOC][tbe]
  (docs/WORKFLOW_FL.md 5.1): drag onto the toolbar to add, off it to remove, spacers absorb the spare width.

### Other main-window details (FL)

- Context menus about an item are **tinted with the item's colour**: the mixer track menu's header "Mixer track
  (KICK)" and the whole menu take the insert's colour.
- Undo (Ctrl+Z) writes "Undone: playlist delete clip" in the hint bar with the level on the right ("Level 2/3").
- Right-click on a Playlist clip with the default tool deletes it (as LYNX).
- Mixer track menu (FL order): Rename, color and icon... (F2), Change color..., Random color, Gradient color...,
  Change icon..., Assign to new audio track(s) | File >, Open audio editor (Ctrl+E), Open audio editor and record
  (Shift+E) | Reset selected track(s) to default | Routing: Channel routing >, Track routing >, Select >, Reset
  routing, Unroute tracks routed here | Solo (S), Alt solo (Alt+S), Arm, Disarm | Compensate for master latency |
  Group: Separator, Create group..., Auto color group | Layout: Dock to >, Move left/right (Alt+Left/Right) |
  Delete (Del), Insert one, Insert multiple..., Insert from state > | CPU: Allow threaded processing.

## What LYNX does now (this round)

- **Browser** (`ui/BrowserComponent.*`, logic in `core/ProjectContents.*`, unit-tested):
  - tabs ALL / PROJECT (LYNX's own text tabs with an accent underline); the folder box is gone: ALL lists "Current
    project" and then every sample folder (FL's Packs, demo projects, MIDI, presets, Documents, Music, Desktop,
    Downloads, the user's folders) as top-level folders, **each with its own colour**;
  - Current project: History, Patterns, Effects, Generators, Automation, Project folder, Audio files - each section
    shows its count on the right and sits on a raised band when open; the counts and items follow the project
    (re-listed 0.3 s after a change, keeping what is open and selected);
  - items: patterns in their colours (bold = current; "not in the Playlist"), audio files in the colour of the channel
    that plays them, with "2x" when several channels use them and "missing" in red when the file is gone, generators
    "name (insert)" in the channel colour with an icon by kind, effects "name (insert)" in the insert's colour,
    automation in the colour of what it moves, history steps (newest first, the current one bold, undone ones grey);
  - gestures: click a pattern = current pattern, double-click = Piano Roll, drag onto the Playlist = a pattern clip;
    click an audio file = preview, double-click = new channel, drag = like any file; click a generator = select the
    channel, double-click = its settings; double-click an effect = its editor (Mixer opens on the insert);
    double-click automation = its editor; click a history step = go back to it;
  - right-click menus: pattern = the pattern menu (after selecting it, as FL); audio file = Send to the selected
    channel, Open in a new channel, Send to the Playlist as an audio clip (at the playhead on the first free track,
    reusing an audio-clip channel of the same file), Edit in the audio editor, Show its channel, Show in the file
    manager; generator = Open settings, Show its mixer insert; effect = View editor, Show its mixer insert; every
    section = Expand all / Collapse all;
  - hints: every row explains its gestures, with a value on the right (clips, "used 2x", "slot 3", "Level 4/9", the
    section's count); the PROJECT tab's search filters the project's items.
- **Toolbar** (`core/ToolbarLayout.h`, `ui/Toolbar.*` EditOverlay, `MainComponent`):
  - right-click on the toolbar, FL's order: **Edit toolbar**, Separators, Hide the toolbar panels (Ctrl+F11),
    Presets > (LYNX's own: Default, Compact, Minimal, Mixing), then the panels as switches, icon size, UI scale,
    Customize toolbar..., Reset; the toolbar's background hint says so;
  - **edit mode**: panels get outlines and name tabs and stop reacting to clicks; a tray under the toolbar lists every
    panel (those on the toolbar dimmed with a tick); drag a panel sideways to move it (an accent line shows where it
    lands), drag it into the tray to remove it, drag one from the tray onto the toolbar to add it there; double-click
    in the tray adds/removes; right-click a panel: Remove / Move left / Move right; preset box, Reset and Done (Esc /
    Enter) on the tray;
  - Ctrl+F11 keeps only the menu and the hint bar on one slim row (FL hides the toolbar entirely; LYNX keeps the menu
    so the way back stays visible); "Separators" switches the panel dividers.
- **Colours per item in menus**: the menus of a mixer track, a channel, a Playlist track, a Playlist clip, a pattern
  and the Browser's items start with a title band in the item's colour (kind in small caps, then the name).
- **Mixer track menu**: Rename and color..., Change color >, Random color at the top (FL's first section).
- **Undo / redo**: the hint bar says "Undone: <step>" / "Redone: <step>" with "Level n/m" on the right.
- Tests: `flui115_project_samples_once_with_colour_and_uses`, `flui115_project_generators_effects_patterns_automation`,
  `flui115_toolbar_drop_and_place`, `flui115_toolbar_presets_and_flags`. Snapshots: `-browser-project`,
  `-toolbar-edit-1600`, `-toolbar-hidden-1600`.

## FL vs LYNX: main-window details (round 115 diff)

| # | Area | FL Studio 2026 | LYNX | Status |
|---|---|---|---|---|
| 1 | Browser | Current project: patterns, samples, effects, generators, history... | same sections (+ automation), live counts, colours, details | done (115) |
| 2 | Browser | Items in the colour of what they belong to | patterns, audio files, generators, effects, automation coloured | done (115) |
| 3 | Browser | Top-level folders each with own icon colour | own colour per folder | done (115) |
| 4 | Browser | Tabs ALL / PROJECT / PLUGINS / PRESETS / SOUNDS / STARRED | ALL, PROJECT, STARRED | partly: STARRED done (115, landing); PLUGINS (installed plugins), PRESETS to do |
| 5 | Browser | Hover star = favourite; STARRED tab; tags | right-click > Starred on files and folders; STARRED tab (folders, then files; search looks in the starred folders) | partly (115, landing): hover star and tags to do |
| 6 | Browser | Effect / generator rows show a thumbnail of the plugin window | icon only | to do (LYNX could draw a mini of its own editors) |
| 7 | Browser | Sample menu: Send to playlist as audio clip / track, open in sampler, slicer..., pitch corrector, time warper, Locate file | send to channel / new channel / Playlist audio clip, audio editor, file manager | partly (slicer channel, audio track, "Locate file" to do) |
| 8 | Browser | Remote control / Initialized controls sections | MIDI links and init values not listed | to do |
| 9 | Browser | Section hint with a count on the right ("77 plugins") | count on the row and in the hint value | done (115) |
| 10 | Browser | Drag a sample / pattern from the project into the Playlist | audio files and patterns | done (115) |
| 11 | Toolbar | Right-click: Edit, Separators, Flat buttons, Hint bar, Hide (Ctrl+F11), Presets | Edit, Separators, Hide (Ctrl+F11), Presets, panels, sizes | done (115); "Flat buttons" n/a (LYNX is flat), extended hint panel to do |
| 12 | Toolbar | Edit mode with a tray, drag to move / add / remove | edit mode with tray, drag move / add / remove, presets, Done | done (115) |
| 13 | Toolbar | Single items (each button) and spacers are draggable | whole panels (groups) only | to do: per-button items, spacers |
| 14 | Toolbar | Factory presets incl. single-line layouts | 4 LYNX presets, two-row layout only | partly (single-line layout to do) |
| 15 | Menus | Item menus tinted with the item's colour | coloured title band | done (115) |
| 16 | Mixer | Track menu: rename/colour/random colour/icon first | rename and colour, change colour, random colour, Insert one, Delete | done (115); icons, gradient colour, insert multiple, group, dock to: to do |
| 17 | Hint bar | Undo says what and the level ("Level 2/3") | "Undone: ...", "Level n/m" | done (115) |
| 18 | Hint bar | Hovered sample shows its full path | full path in the hint | done (115) |
| 19 | Playlist | Track icons (0xEE offset 8) | none | to do (round 104 leftover) |
| 20 | Channel Rack | Channel menu header coloured | coloured title band | done (115) |
| 21 | Mixer | Insert icons, vertical names, compact height | horizontal names | to do (round 90 item 10) |
| 22 | Browser | Waveform preview with sample rate and bit depth | duration, kHz, stereo/mono | partly (bit depth to do) |

## Where I stopped / next steps

- Implemented and committed on `feature/flui115` (pushed to `origin/wip/flui115`): Browser tabs and Current project,
  toolbar edit mode, coloured menu headers, mixer colour items, undo hint. Verification (build-local with the FL Data
  dir, Windows build, snapshots side by side with `tmp/flui115/flref`) - see the landing notes below.
- Landing (2026-10-20): merged origin/main (round 112/113, i18n conflicts kept both sides), then added
  - **STARRED tab**: right-click a file or folder > Starred (ticked when on); the list is kept in
    `browser-starred.txt` in the settings folder; the tab lists starred folders (open like ALL's) then files,
    by name, missing ones left out; a search there looks in the starred folders.
  - **Mixer track menu, FL's second section**: Insert one (a blank insert here, the others move right) and Delete
    (the following inserts move left, a blank one takes the end; channels on it go to the Master, sends to it and
    sidechains from it are removed); default names "Insert n" follow their new number
    (`mixerops::insertTrack` / `deleteTrack`, test `flui115_mixer_insert_one_and_delete`).
- Next candidates (table above, "to do"): hover star and tags, PLUGINS tab, thumbnails of LYNX's own editors in
  Effects/Generators, per-button toolbar items and spacers, a single-line toolbar preset, the rest of FL's mixer track
  menu (insert multiple, groups, icons, gradient colour).
