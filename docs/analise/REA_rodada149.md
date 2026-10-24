# REA round 149 — GUI check of rounds 139–146 + FL REA round 4

Brief (2026-10-24): rounds 143 / 146 landed UI that nobody had looked at in the running app. First run LYNX on
Windows (ASCII path, our PowerShell helpers), use and screenshot those screens, fix what is wrong; then FL's same
screens side by side; then round 146's leftovers (pinned left / right mixer docks, group names, live notes through
the arpeggiator / echo / tracking, Fat mode, Repeat 0, Add to key, root-note keyboard, the wrapper settings page's
missing options, notes on instrument tracks, Consolidate with tempo changes).

- **Targets:** LYNX.exe (this branch, `build-win-flrea149`, run from `C:\Users\kauan\lynx-test\flrea149` with a copy of
  `Ookay - Thief.flp`); FL Studio 2026 (build 5654) on a copy of the same project (`D:\lynx-work\flrea149`), under
  `~/.cache/lynx-fl.lock`; `FLEngine_x64.dll` in our own copy of the round-134 Ghidra project (`~/ghproj/r149`);
  `Fruity Wrapper_x64.dll`'s form resources (our `dfm.py`).
- **Tools:** our `ui.ps1` (SendInput mouse / keys + CopyFromScreen, by process id), `wins.ps1`, `lx.sh` / `fl.sh`
  (tmp/flrea149); Ghidra 12.1.4 headless with `StrRefs`, `FindOps`, `DecAt`.
- **Evidence markers:** **[EXEC]** seen in the running program (screenshots in tmp/flrea149/shots and
  tmp/flrea149/flref, not in the repo), **[DECOMP]** Ghidra pseudocode, **[DATA]** form resources / strings,
  **[model]** our reading.
- Clean-room: behaviour, names and layout logic only; LYNX's code and drawing are our own.

---

## Part A — the rounds 139–146 UI used in the running app **[EXEC]**

Method: LYNX.exe started by process id (`lx.sh start`), driven with real mouse moves / clicks / drags and keys
(`ui.ps1`), every step screenshotted (tmp/flrea149/shots/s01…s92). The project: a copy of `Ookay - Thief.flp` (FL
demo, 27 inserts, ~70 channels, time-stretched audio clips). LYNX's background plugin scan ran during the first hour
(it raised third-party licence dialogs, see ASK_USER); FabFilter Micro's evaluation dialog was answered "Evaluate".

| # | Screen / action | Seen | Fixed in this round |
|---|---|---|---|
| A1 | **Any edit** (mute LED, a knob, a Patcher cable, a root-note key…) on Thief | the screen answered **3.5 s** later: `LYNX_TRACE_EDITS` (new, below) showed `publishSong` 3509 ms = rack build 412 ms + **stream prefetcher update 3.1 s** | the prefetcher looked each audio clip's file up with `SampleLibrary::load`, which **decoded the file again** whenever only a time-stretched copy was alive (the cache holds weak references): new `SampleLibrary::findLoaded` (never reads a file). The rack build rebuilt every stretched channel on every edit because `lastPublishedShape.song.bpm` was never set (the reuse test compares tempos): now copied. **Every edit: 3509 ms → 2.7 ms** |
| A2 | Mixer: Separator, Create group, Dock to › Right; scroll | separators / groups / docks right; the left / right blocks scrolled away with the middle | the left and right dock blocks stay pinned while the middle scrolls under them (FL), hit-testing the pinned blocks first (`MixerStrips::pinOffset`) |
| A3 | Hosted plugin window header *Plugin / ⚙ Wrapper settings* (FabFilter Micro) | works; the page was one long column, the shown tab painted red-on-red (unreadable) on hover | the page has FL's three tabs (Part C7), LYNX's normal tab look (the channel window's) |
| A4 | Parameter menu (channel window VOL knob) | items right; no sections; no Copy / Paste value, no Edit automation clip | FL's order and sections: Reset · **Automation** (Edit events…, Edit events in piano roll, Init song…, Delete initial value, Create automation clip, **Edit automation clip…**) · **Remote control** (Link to MIDI controller, Override global link…, internal controller, unlink) · **Value** (**Copy value**, **Paste value** — the position in the range, so it pastes into another control —, Type in value…). Tools › Last tweaked uses the same sections |
| A5 | Edit events in piano roll; draw in the lane with the mouse | works (events drawn / erased); the lane box shows "Synth Bas…" only; the drawn events showed seconds late (A1) | the lane's name is written in the lane (FL writes it next to *Control*) |
| A6 | Channel window, Misc tab | right; window title "Channel settings"; key tracking Mid said **C4** for key 60 (LYNX and FL say C5); empty bottom | the window is named like FL's (*Synth Bass (Synth Bass)*: channel, then its mixer track); Mid in FL's naming; the **Root note** row (Part C3) |
| A7 | Import MIDI window (File › Import › MIDI file…) | every option one per row, 450×561 | FL's layout: the selections on top (tracks, channels as a 2 × 8 grid, **Channel type** box LYNX synth / MIDI Out, controller), the options in FL's two columns, Accept |
| A8 | Tools › Last tweaked parameters (after moving a rack knob) | right (name, menu) | sections as A4 |
| A9 | Patcher (Add › Patcher; right-click › Add instrument › Synth; drag the Synth's Out onto "+ output" with the mouse) | works: auto-wired, the cable makes "Output 2" and a new "+ output" | — (the delay seen was A1) |
| A10 | Newtone (mixer slot › Newtone (beta), Load sample…, drag a note down) | works (analysis, 10 notes, pitch drag) | note names in FL's naming (D5, not D4); the piano roll's note tools (Lowest / Highest note, Key) too |
| A11 | Welcome wizard (Options › Setup wizard) | laid out without overlaps (round 140) | — |
| A12 | Latency box › Set in ms… | the value applies (PDC); Enter in the box did not confirm on the first try (not reproduced as a bug: the window lost focus to the automation) | — |

Diagnostics added: `LYNX_TRACE_EDITS=1` writes each publish's time and the rack build's phases to
`%APPDATA%\LYNX\edits.log`.

Side-by-side / before-after images: tmp/flrea149/compare/ (wrapper-settings, wrapper-latency-menu,
wrapper-processing, wrapper-troubleshooting, channel-misc, parameter-menu, pianoroll-event-lane, patcher,
import-midi, tools-last-tweaked, mixer-pinned-docks, before-after-import-midi, before-after-channel-misc,
before-after-parameter-menu).

---

## Part B — FL's same screens **[EXEC, FL Studio 2026 on a copy of Thief, under the FL lock]**

| Screen | FL Studio 2026 | LYNX after this round |
|---|---|---|
| Plugin wrapper (FL Studio VSTi) | header: plugin tab, *plugin wrapper settings* tab, wrench (Misc), ON / PAN / VOL / PITCH + RANGE / TRACK; settings tabs **Settings** (Info: Name, Type, Category, Latency ▸; Reload / Refresh; MIDI: Input / Output port, pitch-bend range, note release velocity, MOD X as poly aftertouch, All Notes Off on stop; Options; Presets; GUI; Automation), **Processing** (Options: threaded, smart disable, bridged, external window, ensure processor state, notify rendering mode, process inactive I/O; Connections), **Troubleshooting** (Compatibility: Reset plugin when FL resets, Send loop position, fixed size buffers + More, time offset, fast idle, Ctrl+Z, keyboard; Location: Path + Locate) | Plugin / ⚙ Wrapper settings; **Settings** (Info with the **Latency** box: Reset / Set in ms / samples / beats; MIDI Input / **Output** port), **Processing** (Notify about rendering mode, threaded, bridged, smart disable), **Troubleshooting** (fixed buffers and their two options, **Location** with Locate) |
| Misc tab | Levels, Polyphony, Time, Tracking (Vol / Key: Pan, Mod X, Mod Y, Mid), Group, Arpeggiator (direction icons, Time, Gate, Range, Repeat, Slide, Chord), Echo delay / fat mode, **Root note: C5** (Reset, Enable main pitch, Add to key, Fine tune, keyboard) | the same groups; Root note row with Reset, main pitch, Add to key and the 128-key keyboard; LYNX has no tracking / echo Mod X / Y knobs (they do nothing in LYNX) nor Fine tune |
| Parameter menu | Reset · Automation · Remote control · Value (A4) | same sections and items |
| Piano roll lane | *Control ▾ Velocity*; menu Note properties / Channel controls | lane box + the lane's name |
| Patcher (generator) | Map / Surface tabs, Audio / Parameters / Events, a Surface node, From / To FL Studio, minimap | From / To LYNX, nodes and cables, no Surface / minimap |
| Import MIDI data | tracks box, 2 × 8 channels, Channel type, options in two columns, Accept | same layout (A7) |
| Tools menu | … Last tweaked parameters › Last tweaked / Next to last tweaked | same section |

---

## Part C — round 146's leftovers

### C1. Pinned mixer docks **[model: FL's behaviour, round 146a "partial"]** — A2.

### C2. Group names **[DECOMP FLEngine `FUN_012aa200` (Create group) → `FUN_012be6f0`]**

`FUN_012be6f0 (track, string, …)` sets the mixer track record's +0xc string — the **name** (it also updates the
playlist audio track linked to it, +0x24 of the playlist track) — plus its icon (+0x14) and colour (+4). Create group
calls it for every selected track other than the current one whose +0xc is empty, with the current track's +0xc.
So FL's group gives its unnamed tracks the current track's name. **LYNX now:** `mixerops::createGroup` does the same.
Test `rea149_create_group_names_unnamed_tracks`.

### C3. Root note keyboard and Add to key **[EXEC FL's Misc tab; .flp 0x87 root note, 0xD7 byte 63 Add to key (round 146b)]**

`ChannelMisc::rootNote` / `addToKey` (imported, saved in .lynx). The Misc tab's Root note row: FL's title
*Root note: C5*, Reset, Enable main pitch, **Add to key**, a keyboard of every key (click sets the root). A sampler's
root is its pitch (the import already folds FL's root into `SamplerSettings::pitch`; the keyboard moves the pitch
by the change, within ±24 st); other instruments with *Add to key* play (C5 − root) semitones away (`renderChannel`
shifts the channel's keys; live notes too). Test `rea149_add_to_key_follows_the_root_note`.

### C4. Live notes through the arpeggiator / echo delay / tracking

Live-played notes of a channel with an arpeggiator, echo delay, key / velocity tracking or Add to key go through the
channel's note path (queued from the live queue, added to the first chunk's events); while the transport is stopped
the arpeggiator / echo run on their own clock (`RackRenderer::liveTick`). Arpeggiators / echoes restart when playback
starts or stops. Test `rea149_live_notes_play_through_arp_and_echo` (8 arpeggio notes in 1 s at 1/4 beat, 120 BPM;
the note + 3 echoes).

### C5. Fat mode **[DECOMP FLEngine `FUN_00d28c90`, the channel echo generator]**

Found by the channel's echo defaults (`FUN_00fe90d0`: +0x20c echoes = 4, +0x210 time = 0x90, +0x214 / +0x218 mod
Y / X = 0x80) and `FindOps` on `+ 0x20c]` / `+ 0x210]`. The generator loops `fat + 1` times: the second pass copies
the note's pan / velocity / pitch / mod fields back (`memcpy` of 0x14 bytes from +9) and **negates the pan step and
the pitch step**; with fat on, the velocity is multiplied by the feedback **once per pass** instead of once per echo
(every echo at feedback × the note's level); ping pong (+0x28e & 2) flips the pan step at the ends. **LYNX now:**
`NoteEcho` plays the mirrored second series and the constant level. Test `rea149_echo_fat_mode_like_fl`.

### C6. Arpeggiator Repeat 0 **[EXEC r146b: "Repeat 0 plays the chord at once each step"]**

`ArpSettings::octaveRepeat` 0 (imported, saved, the Repeat knob shows *Chord*): each step plays the whole chord, the
range's octaves in the direction's order. Test `rea149_arpeggiator_repeat_zero_plays_the_chord`.

### C7. Wrapper settings page **[DATA Fruity Wrapper `TSynthEditorForm` DFM; EXEC FL's page]**

The DFM gives FL's tabs and groups (Troubleshooting › Compatibility: FixedSizeBuffersBox, ResetOnFlushBox,
SendLoopPosBox, UseTimeOffsetBox, FastIdleBox…; Processing › Options: ProcessInactiveBox, RenderingModeBox,
AllowThreadedBox, BridgedBox, SecureCPUBox, ExternalWindowBox, AllowSmartDisableBox; Settings › MIDI: MIDIInPortWheel,
MIDIOutPortWheel, SendNoteOffVelocityBox, SendPitchBendRangeBox, SendPolyAftertouchBox, MidiEventBusCombo,
SendAllNotesOffBox; Info: LatencyCombo with `ManualLatency`) **[DATA]**; FL's latency menu is Reset / Set in ms / Set
in samples / Set in beats **[EXEC]**. New behaviour behind three of them:
- **Output port** (`PluginRef::midiOutPort`, imported from the wrapper's record 0x01): the note on / offs a hosted
  instrument sends out (only plugins that produce MIDI) play, one chunk later, the plugin channels whose input port it
  is (`Instrument::takeMidiOutput`, `RackState::midiOutTargets`).
- **Notify about rendering mode** (`notifyRender`, wrapper flag 0x4, on): off = the plugin is not told about exports.
- **Latency** (`latencyOverrideMs`): the delay compensation uses it instead of the plugin's report.
Saved in .lynx. Test `rea149_wrapper_latency_and_midi_output_port`; `rea146a_wrapper_settings_page_options` follows
FL's order.

### C8. Consolidate with tempo changes

The render of a track now keeps the other tracks' automation clips (the tempo map and what they automate), cut at
the render's start, and starts at the song's tempo there. Test `rea149_consolidate_keeps_the_tempo_map`.

### C9. Not done

Notes on instrument tracks (FL draws a channel's notes on its Playlist track), FL's wrapper options without LYNX
behaviour (Reset plugin when FL resets, Send loop position, time offset, note release velocity, pitch-bend range,
MOD X as poly aftertouch, All Notes Off on stop, Process inactive I/O), echo / tracking Mod X / Y, Fine tune, the
Patcher's Surface / minimap, a plugin picker instead of the long Plugin submenu.

---

## Where I stopped

- Done: the rounds 139–146 screens used in the running app on a big FL demo (Part A), with fixes — above all every
  edit taking 3.5 s on songs with time-stretched audio clips (now ~3 ms); FL's same screens captured and compared
  (Part B, tmp/flrea149/compare); round 146's leftovers C1–C8 (pinned docks, group names, root note keyboard and Add to
  key, live notes through the arpeggiator / echo / tracking, Fat mode from FL's echo generator, Repeat 0, the wrapper
  page's tabs with Output port / Notify about rendering mode / Latency, Consolidate with tempo changes).
- Not done: C9 (instrument-track notes on the Playlist, the wrapper options with no LYNX behaviour, Mod X / Y, Fine
  tune, Patcher Surface / minimap, a plugin picker — the Plugin submenu lists hundreds of plugins in four columns).
- Not confirmed: what FL's "Reset plugin when FL Studio resets" (on by default) is triggered by; FL's Repeat 0 with a
  range above 1 (LYNX: an octave per step); whether FL's Add to key also moves FL's own generators (LYNX: hosted plugins
  and LYNX's instruments other than the sampler).
