# Round 81: missing-files window, Settings window, UI windows inventory

Branch `feature/dialogs81` (landed first) and `feature/settings81` (on top of it). English, clean-room: FL Studio
was used only for window names, labels and visible behaviour; no artwork or layout copied.

## 1. FL Studio's windows (evidence)

- [DATA] REA `inspect_pe_resources` on `FLEngine_x64.dll` (FL Studio 2026): 327 resources, 128 RT_RCDATA; 108 are
  Delphi binary forms (`TPF0`). A small reader (`tmp/r81/py/dfm.py`, outside the repo) printed each form's component
  tree with `Caption`/`Hint`/`Items` only. Full table and LYNX status: `docs/analise/UI_windows_inventory.md`.
- [DATA] Missing files in FL: form `TMissingPluginsForm` (an icon, a string grid, a memo, a "browse sample" open
  dialog, a Copy menu) and the messages "%d sample(s) required by this project could not be located.", "Browse for
  sample...", "Skip all", "Auto locate", and "Missing data folders|Some of the samples and files that you located
  are not in any of the FL Studio search folders. Do you want to add those locations to the list now?". So FL lists
  the missing items, lets the user locate them (and uses the located folders as search folders). LYNX's window is
  its own design around the same job.
- [DATA] FL's Settings window is one form (`TMIDIForm`, caption "Settings") with pages Audio, MIDI, General, File,
  Project, Info, Debug, About, Theme, Account. Project page: Time settings (Timebase PPQ, beats per bar, steps per
  beat), data folder, pan law, playlist crossfades. Info page: Title, Author, Genre, Web link, Comments, "Show info
  on opening", working time. File page: "Browser extra search folders", plugin folders, backup.
- [DATA] `.flp` event 0xC3 holds the Info page's comments (UTF-16 text with CR line breaks), checked on the
  installation's demo projects (e.g. "Asher Postman - Future House", "Astes - Bien Duro"); 0xC2 title, 0xCE genre,
  0xCF artist, 0xC5 web link were already parsed.

## 2. Missing files window (`src/core/MissingFiles.*`, `src/ui/MissingFilesDialog.*`)

- Core (no JUCE, tested): `findMissingSamples` (channel samples of sampler / audio clip / Granulizer / 3x Osc,
  sampler zones except FL Keys' installation wavetables, effect samples also inside parallel containers),
  `relocationScore` (same file name, + shared trailing folder names), `relocateByName`, `searchFolderForMissing`
  (recursive, bounded, cancellable walk through a caller-supplied lister), `relocationMap`, `relocatedPath`,
  `applyRelocations` (case/separator-insensitive), folder list parse/save/remember.
- FL import: `FlpImportReport::missingFiles` (path + channel/insert that uses it) and `relocatedFiles` (what
  `locateFlSample` re-rooted); `FlpImportOptions::relocations` makes a re-import use the samples the user located.
  Probed candidates (DrumSynth fallbacks) and FL Keys wavetables are not reported.
- AudioEngine: `importFlpFile(..., relocations, asEdit)`; with `asEdit` the re-import replaces the song as one undo
  step (project marked modified). `getMissingPluginDetails()`: missing plugin, who used it, what replaced it.
- Window: three pages (Samples / Plugins / Found automatically), Search folder... (background thread, Stop),
  Locate... (then the same folder is checked for the others), Skip, "Remember the folders I search"
  (`%APPDATA%\LYNX\search-folders.txt`). On opening it searches by itself the remembered folders, the Browser
  folders and the project's folder. Apply: .lynx → `applyRelocations` as one undo step; .flp → re-import.
  Shown after opening a project when something is missing (the FL import report follows it); Help menu
  "Missing files..." opens it any time.

## 3. Settings window (`src/ui/SettingsDialog.*`)

- Pages Project (tempo, beats per bar, swing, master pitch; timebase/steps read-outs), Info (title, author, genre
  with a list, web link + Open, comments, "Show this page when the project is opened") and Files (search folders,
  Browser folders, plugin folders). Project/Info edits are undo steps ("Tempo", "Time signature", "Project info").
- `Song::info` (`ProjectInfo`) saved in .lynx (`"info"` object, only when not empty) and imported from .flp.
- Options menu: Project settings..., Project info..., File settings (search folders)...

## Where I stopped / next steps

- Landed: dialogs81 (missing files + inventory). settings81: built and snapshotted, landing after dialogs81.
- Queue of windows not done: `UI_windows_inventory.md` "Queue" and `tmp/backlog.md` "UI windows".
