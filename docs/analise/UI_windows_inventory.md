# UI windows inventory: FL Studio 2026 vs LYNX (round 81)

What windows and dialogs FL Studio 2026 has, and where LYNX stands on each. Purpose: a queue of windows still to
build, in priority order (bottom of this file). Clean-room: only window names, labels and the behaviour a user sees
are used; no artwork, no layouts copied pixel for pixel.

## Sources

- [DATA] The Delphi form resources (RT_RCDATA, `TPF0` streams) of `FLEngine_x64.dll` (FL Studio 2026, 22 227 016
  bytes), listed with REA `inspect_pe_resources` (327 resources, 128 RCDATA, 108 of them forms) and read with a small
  binary-DFM reader written for this round (`tmp/r81/py/dfm.py`: component tree, `Caption`/`Hint`/`Items` only).
  Window titles below are the forms' `Caption`s.
- [DATA] UTF-16/ANSI strings of the same DLL for messages that are not in a form (missing samples/plugins).
- [DOC] FL Studio's manual (general knowledge of the menus that open each window).
- Native-UI capture of FL itself was not needed for this inventory (the forms list is complete for FLEngine; plugin
  windows live in the plugin DLLs and are covered by the REA plugin rounds).

FL's forms that are not user-facing windows were left out: `TFLBaseVectorForm`, `TMEBaseModalForm`,
`TPRBaseToolForm`, `TDragDockObjectForm`, `TQuickDockForm`, `TQuickPopupMenuWindow`, `TQuickEditToolbarContainer`,
`TMEResModule`, `TShortcutsModule`, `TTestForm`, `TTutorialHighlightForm`, `TNotificationForm`, `TBridgedEditorForm`.
Licensing / shop / cloud forms (`TUnlockForm`, `TSharewareForm`, `TRedeemCodeForm`, `TInAppShopForm`,
`TCloudAccountsForm`, `TWelcomeUnlockForm`, `TWelcomeGDPRForm`, Settings › Account) have no LYNX equivalent by design.

Status: **exists** = LYNX has a window that does the job; **partial** = part of it exists (often as a menu, not a
window); **missing** = nothing yet; **n/a** = not applicable to LYNX.

## Main windows

| FL form (Caption) | What it is | LYNX | Notes |
|---|---|---|---|
| TFruityLoopsMainForm "FL Studio" | main window, menus, toolbar | exists | MainComponent + Workspace |
| TStepSeqForm "Channel rack" | channel rack | exists | ChannelRackComponent |
| (piano roll form in FLEngine code) | piano roll | exists | PianoRollComponent |
| (playlist) | playlist | exists | PlaylistComponent (other agents own clips/menus) |
| TFXForm | mixer | exists | MixerComponent |
| TBrowserForm "FL Studio Browser", TSampleListForm "Browser" | browser | exists | BrowserComponent |
| TPluginForm | plugin wrapper window | exists | PluginWindow (+ LYNX's own editors) |
| TFLHintBarForm | hint bar | exists | HintBar |
| TToolbarForm / TTouchKeybForm "Touch keyboard" | on-screen keyboard | missing | typing keyboard as piano exists, no on-screen keys |
| TPythonForm "Script output" | Python scripting console | n/a | LYNX has no scripting |
| TPluginMonitorForm "Plugin performance monitor" | CPU per plugin | missing | |
| TGraphEditorForm | event graph (channel/mixer automation editor) | partial | automation clip editor (AutomationUi) |

## Settings ("Settings" = TMIDIForm, one window with pages)

| Page | Content in FL | LYNX | Notes |
|---|---|---|---|
| Audio | device, sample rate, buffer, resampling quality, preview/metronome track, CPU options | **exists (round 82)** | Settings › Audio: driver, output/input, rate, buffer + latency, driver panel, test sound, status/CPU/dropouts; device saved between sessions (no mixer/CPU options) |
| MIDI | input/output devices, controller type, ports, sync, velocity curves | **partial (round 82)** | Settings › MIDI: inputs on/off, MIDI channel filter, fixed target channel, velocity curve, pickup, controller links; no outputs/sync |
| General | GUI scaling, animations, startup project/template, splash, undo levels, language, note names | **exists (round 82)** | Settings › General: language, UI size, waveforms, startup, editing options, undo levels, backups (interval + kept), plugin sandbox |
| File | browser extra search folders, plugin folders, backup (autosave), user data folder | **exists (round 81)** | Settings › Files: missing-files search folders, Browser folders, plugin folders (autosave folder still a menu item) |
| Project | time settings (PPQ, beats per bar, steps per beat), data folder, pan law, playlist crossfades | **exists (round 81)** | Settings › Project: tempo, beats per bar, swing, master pitch, timebase read-out (no data folder / pan law) |
| Info | title, author, genre, web link, comments, working time | **exists (round 81)** | Settings › Info; kept in .lynx, imported from .flp (comments = event 0xC3); no working time |
| About | version, license | exists | About LYNX |
| Theme | theme selector | n/a | one LYNX theme |
| Debug | debug log | n/a | |

## Dialogs and tool windows

| FL form (Caption) | LYNX | Notes |
|---|---|---|
| TMissingPluginsForm + "%d sample(s) required by this project could not be located." | **exists (round 81)** | Missing files window: samples (search folder, locate, skip, remembered folders), plugins with replacements, auto-relocated list |
| TInfoForm "Song info" (shown on opening) | exists (round 81) | Settings › Info opens with the project when "Show this page when the project is opened" is on |
| TNewProjForm "New project" (PPQ, time signature, name/location, info) | partial | File › New + templates menu; no dialog |
| TProjectRenameForm "Rename project" | missing | |
| TRenderForm / TWAVRenderForm "WAV rendering" | exists | Export dialog: WAV 16/24/32f, FLAC, OGG, sample rate, tail, stems |
| TMEMP3OptionsForm "MP3 export" | missing | no MP3 encoder |
| TMEOggOptionsForm "Ogg Vorbis export" | partial | OGG export, no quality options |
| TExportScoreForm "Export as score sheet" | missing | |
| TMIDIImportForm "Import MIDI data" | partial | .mid opens as a project / into a channel, no options dialog |
| TDWPRenderForm "DirectWave Instrument rendering" | n/a | |
| TShortcutForm / TShortcutEditorForm "Manage shortcuts" | **exists (round 82)** | Shortcut manager: search, place filter, rebind by key press, clash check, reset; keys.json keeps changes only |
| TPaletteEditorForm "Palette editor" | missing | fixed LYNX palette |
| TThemeEditorForm "Theme selector" | n/a | |
| TTapTempoForm "Tempo tapper" | exists | tempo menu › Tap tempo |
| TAddMarkerSettingsForm "Marker settings" | partial | add/rename marker menu items |
| TPRTimeSigForm "Time signature change" | partial | playlist signature menu |
| TPLClipPropForm "Clip properties" | missing | (playlist is another agent's area) |
| TPLMergeArrangementForm "Merge arrangement" | missing | arrangements exist |
| TPRNotePropForm "Note properties" | exists | Note inspector in the Piano Roll |
| TEventEditForm "Piano roll - " (event editor) | missing | automation editor covers automation clips only |
| TEnvMapForm "Mapping" (velocity/controller curves) | missing | |
| TParamCtrlForm (link to controller) | exists | "Link to MIDI controller" |
| TMIDIInputForm "Remote control settings", TMIDIInputForm_Generic "Global link settings" | partial | MIDI learn exists, no settings dialog |
| TPlugListForm / TPluginListForm (plugin picker / database) | **exists (round 82)** | Plugin manager window + quick-add palette |
| (Plugin Manager is a separate FL app) | **exists (round 82)** | Plugin manager: list/status/failed/favourites, rescan, retry, folders |
| TLargePluginDataForm | n/a | |
| TNameEditForm (rename/colour) | exists | Rename and color dialog |
| TMsgForm / TFLMsgForm / TFLBasePluginsMsgDlg | exists | LYNX alert windows |
| TWelcomeWizard "Welcome to FL Studio" | exists | Setup wizard (language, audio, plugin folders) |
| TSpeechForm "Speech properties" | missing | speech synthesis: low value |
| TDemucsForm, TDeverbForm | missing | stem separation / de-reverb: large DSP projects |
| TScriptDialog "Configure script" | n/a | |
| (undo history in the Browser) | exists | Undo history window |

### Piano roll tools (TPR* forms)

| FL form (Caption) | LYNX | Notes |
|---|---|---|
| TPRQuantizeForm "Piano roll - quantizer" | partial | quantise starts / starts and lengths (menu), no dialog with strength/swing |
| TPRSliceForm "Piano roll - chopper" | partial | "Chop to grid" |
| TPRArpForm "Piano roll - arpeggiator" | exists | Arpeggiate tool |
| TPRStrumForm "Piano roll - strumizer" | exists | Strum tool |
| TPRFlipForm "Piano roll - score flipper" | exists | Flip tool |
| TPRRandomForm "Piano roll - randomizer" | exists | Randomise tool |
| TPRLevelScaleForm "Piano roll - level scaling" | exists | Scale levels tool |
| TPRKeyLimitForm "Piano roll - limiter / transposer" | exists | Limit tool |
| TPRLegatoForm "Piano roll - articulator" | partial | "Make legato" |
| TPRFlamForm "Piano roll - flamer" | missing | |
| TPRLFOForm "Piano roll - LFO" | missing | |
| TPRTripletForm "Piano roll - claw machine" | missing | |
| TPRScoreCreatorForm "Piano roll - riff machine" | missing | |
| TPRChordToolForm "Piano roll - generate chord progression" | missing | |
| TPRFillForm "Advanced fill" | partial | fill every N steps, Euclidean fill |

### Edison / audio editor tools (TME* forms)

Amp, Blur, Claw machine, Decimate, Clean up (denoise), Drum(loop) stretcher, Filter, Equalizer, Convert to
automation, Loop tuner, Convolution reverb, Level scaling, Smooth up, Time stretcher / pitch shifter, Vocal
denoiser, Sample properties: **missing** (LYNX has no audio editor; channel sampler settings cover reverse,
normalise, region and stretch). These belong to an "Audio editor" feature, not to single windows.

## Queue (not done in round 81), priority order

Round 82 did items 1 (General + Audio; not pan law / data folder), 2, 4 (inputs; not outputs/sync) and 6.

1. **Settings › General** (moves the scattered Options items into one page) and **Settings › Audio** as a page
   (today a separate dialog); Project page: pan law, data folder.
2. **Plugin manager**: scanned plugins (name, vendor, format, path, status), scan folders, rescan, failed plugins.
3. **Event editor** for a channel's or insert's parameter events (FL's TEventEditForm), next to automation clips.
4. **MIDI settings** page: inputs on/off, which channel each input plays, output/sync.
5. Piano roll tools still missing: quantizer dialog (strength, swing, start/end), chopper dialog, flamer, LFO,
   articulator dialog, claw machine, riff machine, chord progression.
6. **Shortcut manager** (a GUI over keys.json).
7. **Touch keyboard** window.
8. **Clip properties**, **Merge arrangement** (after the Playlist work of the other agents lands).
9. **Plugin performance monitor**.
10. Export options: MP3, OGG quality, dither choice; **MIDI import** options; **Export as score sheet**.
11. New project dialog (PPQ, time signature, folder, info), Rename project, Palette editor.
12. Audio editor (Edison-like) tools - large, its own project.
