# Round 82: Settings › General, Audio and MIDI; Plugin manager; Shortcut manager

Branch `feature/dialogs82`. English, clean-room: FL Studio was used only for window names, option labels and the
behaviour a user sees; LYNX's windows are its own layout in its own style (dark, red accent).

## 1. FL Studio's windows (evidence)

- [DATA] REA `inspect_pe_resources` on `FLEngine_x64.dll` (FL Studio 2026; evidence
  `ev_396266e7…`): 327 resources; the forms used here are RT_RCDATA (type 10) entries `TMIDIFORM` (the Settings
  window, index 206), `TPLUGINLISTFORM` (220), `TSHORTCUTEDITORFORM` (251) and `TSHORTCUTFORM` (252). Their
  binary-DFM component trees were read with round 81's reader (`tmp/r81/py/dfm.py`, captions/hints/items only).
- [DATA] Settings › Audio (`AudioPage`): panels "Input / output" (output and input device lists, status, "Auto
  close", sample rate list 44100…192000, buffer length + offset (DirectSound), ASIO panel button, ASIO clock source,
  "Triple buffer", "Safe overloads", priority, playback tracking, underrun counter), "Mixer" (resampling quality,
  browser preview track, metronome track, "Reset plugins on transport", "Play truncated notes on transport"),
  "CPU" (multithreaded generator/mixer processing, smart disable, align tick lengths).
- [DATA] Settings › MIDI (`MIDIPage`): "Input" (device grid, Enable, Controller type, Port, "Link note on velocity
  to" (none/Velocity/Mod X/Mod Y) with a mapping curve, release velocity, "Pickup (takeover mode)", "Auto accept
  detected controller", foot pedal, generator muting / omni preview / performance mode / song marker MIDI channels),
  "Output" (device grid, Port, "Send master sync", sync type MIDI clock / MTC, offset, "Send all notes off"),
  "External sync", "Refresh device list", "Enable gamepads", FL Studio Remote.
- [DATA] Settings › General (`GeneralPage`): System (file association), Display (main/pop-up/toolbar scaling,
  animations, transparency, colourful waveforms), Advanced, Miscellaneous (splash screen, auto-name channels,
  startup project, default template, silent startup, …), Input (mouse/pencil/touch, "Manage shortcuts"), Updates,
  Language (language, note names, sharps/flats), Hints and warnings, Undo history (maximum undo levels, undo knob
  tweaks, alternate undo mode). Settings › File › Backup: "Autosave" (Never / every 15 / 10 / 5 minutes …),
  "Maximum" backups (100), location; Plugins panel: "Manage plugins" opens FL's separate Plugin Manager app.
- [DATA] `TPluginListForm`: find box, a plugin grid, progress, "Cancel plugin scan", "Skip this plugin", "Manage
  plugins". FL's full plugin manager is a separate application (not in FLEngine), so only the in-app list was read.
- [DATA] `TShortcutEditorForm` "Manage shortcuts": an "Actions" grid with a filter box, fixed combos for a few
  bindings (toggle browser / plugin picker / project picker, MIDI import window modifiers, audio clip double-click),
  Apply / "Reset all", a row menu "Discard changed shortcut" / "Restore default shortcut".

## 2. What LYNX does now

### Core (`src/core/Preferences.*`, no JUCE, tested in `tests/PrefsTests.inc`)
- `parseKeyValues` (settings files), autosave choices and backup rotation (`backupName` "Name (YYYY-MM-DD
  HH.MM.SS)", `isBackupOf`, `backupsToRemove` keeps the newest N per project), undo level clamp,
  `bufferLatencyMs`.
- MIDI: `VelocityCurve` (as played / soft / hard / fixed) + `mapVelocity`; `MidiPrefs` per input device (on/off, MIDI
  channel filter, a fixed Channel Rack channel or the selected ones), text round trip; takeover
  (`pickupReached`, `PickupState` + `pickupUpdate`: once caught the controller follows until the parameter is moved
  by something else).
- Plugin manager: `PluginRow`/`visiblePlugins` (filter all/instruments/effects/favourites/failed, multi-word
  search, sort by column), favourites file format.
- `keys::KeyMap`: `actionsWith` (clash in the same context), `isDefault`, `overridesJson` (keys.json keeps only
  the changed actions; it reads back to the same map).

### Settings window (`src/ui/SettingsDialog.*`, `src/ui/SettingsPages.*`)
Tabs in FL's order: Audio, MIDI, General, Files, Project, Info.
- **Audio**: driver type (ASIO / Windows Audio / DirectSound…), output and input device, sample rate, buffer size
  with its latency in ms, "Driver settings..." (the ASIO panel; the device is reopened after it), "Play a test
  sound", status (device, rate, output latency), audio CPU load and dropout (xrun) count, "Record input audio".
  The device setup is now **saved** (`%APPDATA%\LYNX\audio-device.xml`) and reopened at the next start (before,
  LYNX always opened the default device).
- **MIDI**: one row per input device (on/off — off closes the device —, "Any MIDI channel"/channel 1–16, "Plays the
  selected channels"/a fixed channel), refresh; velocity curve; controller type (generic, MIDI learn); pickup;
  the project's controller links with Remove / Remove all. Saved in `%APPDATA%\LYNX\midi-settings.txt`; the
  engine applies them on the MIDI thread (`AudioEngine::setMidiPrefs`).
- **General**: language (restart offered), interface size, waveforms (clean/spectral, stereo), splash screen,
  startup sound, background plugin scan at start, typing piano, keep channel editors open, remember seek time,
  pattern-clip double-click target, maximum undo levels (25…1000, `AudioEngine::setUndoLevels`), backups (never /
  every 1…30 minutes, kept per project 1…50, open folder), plugins in a separate process, Plugin manager... and
  Manage shortcuts... buttons. The Options menu lost the items that moved here (and "Edit keys.json" / "Reload").

### Plugin manager (`src/ui/PluginManager.*`, Options › Plugin manager...)
Table (favourite star, name, vendor, type, format, status, file), search, filters with counts, sortable columns;
status OK / Failed to load (the scan's blacklist) / File not found; row menu: favourite, Try again (targeted
background scan), Remove from the list, Show in folder, Copy path; Find new plugins, Rescan all... (forgets
everything, then scans), Stop, Plugin folders... (Settings › Files), Add plugin file... Favourites
(`%APPDATA%\LYNX\plugin-favourites.txt`) come first, starred, in the plugin menus and the quick-add palette.

### Shortcut manager (`src/ui/ShortcutManager.*`, Options › Manage shortcuts...)
Every key-map action with where it works and its keys (key caps), search by name or key, place filter, "Changed
only"; Add shortcut / Change (press the keys; Esc cancels) / Remove / Reset to default / Reset all...; a clash in
the same place is shown with "Assign anyway" (moves the keys) or Cancel. Saved at once to keys.json (changed actions
only); the main window reloads its key map and hints.

## Not done / next
- Audio: FL's mixer options (resampling quality, preview/metronome track, multithreading switches) have no LYNX
  setting yet; "safe overloads"/priority/triple buffer are driver-specific and left out.
- MIDI: output devices, MIDI clock / MTC sync out and external sync in; release velocity, foot pedal mode, controller
  scripts; per-input velocity curve editor.
- Project page: pan law and data folder (the engine has no pan-law option yet).
