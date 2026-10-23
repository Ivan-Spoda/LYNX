# Round 131 — issue #4 bugs: window z-order and focus, MIDI inputs + activity light, pattern length, Song loop

Branch `feature/bugs131`. Issue: github.com/Ka1-S/LYNX/issues/4 (BURNER1PA, "Various Critical Bugs & Features").
Only the bug part; the feature requests are round 132. Evidence markers: [EXEC] run here (LYNX.exe on this PC, core
tests), [CODE] read in our source or JUCE 8.0.4's, [model] a design choice / FL behaviour as understood.

## 1. Windows: the main window acted "always on top"
Report: the Audio editor opens behind the main window; the Plugin picker / Quick-add palette (F8) goes behind and its
search box can't keep the focus; VST plugin windows lose the focus, so their drop-down menus can't open.

Two causes [CODE]:
1. `MainComponent::timerCallback` (30 Hz) gave the keyboard focus back to the main component whenever it did not have
   it and LYNX was the foreground process — also while another LYNX window (palette, Audio editor, plugin editor,
   Settings…) was the active one. `grabKeyboardFocus` on a component of an inactive window activates that window:
   30 times a second the main window was pulled to the front, the palette's search box lost its focus and the plugins'
   native drop-downs closed (a menu closes when its window loses the activation).
2. `keepAboveMainWindow` made a window "owned" by the main window (Windows keeps owned windows above their owner) by
   setting the owner of its native window once. JUCE recreates the native window when a style changes after it is on
   screen — `ResizableWindow::setResizable` → `recreateDesktopWindow()` [CODE juce_ResizableWindow.cpp:285]. The Audio
   editor is opened with `openToolWindow` and made resizable right after (`AudioEditor.cpp openWindow`), so its new
   native window had no owner (and no dark title bar / LYNX icon either) and fell behind the main window as soon as the
   main window was activated (by cause 1, or by a click).

Fixes:
- The focus is reclaimed only while the main window itself is the active one (`shouldReclaimKeyboardFocus`: its peer
  has the native focus) — the original purpose (a closed dialog left nothing focused, the PC keyboard must keep
  playing) still works.
- `keepAboveMainWindow` attaches a ComponentListener that re-applies the ownership, the chrome and the icon every time
  JUCE gives the window a new peer (`componentParentHierarchyChanged`), removed when the window is deleted. Every
  window that uses it (plugin editors, channel settings, tool windows, detached panels, dialogs) is covered.
- No Settings option was added: with the two fixes the windows behave like FL's (owned by the main window: above it,
  not above other programs; focused when opened). The sandboxed plugin windows (opt-in "separate process") stay
  "always on top": owning a window of another process would join the two processes' input queues, which defeats the
  sandbox (a hung plugin would hang LYNX's UI) [model].

Reproduction and check [EXEC]: new hidden CLI `LYNX.exe --windowtest report.txt` (src/Main.cpp) — a main window with
the real MainComponent, then the Quick-add palette, the Audio editor, a "click" on the main window, a plugin-like
window with a focused text field, and the focus going back to the main window; it checks activation, keyboard focus,
ownership and the Windows z-order (`isAboveMainWindow`). Run on this PC (D:\lynx-work\bugs131):

| check | before the fix | after |
|---|---|---|
| quick-add palette active / search box focused | FAIL / FAIL | ok / ok |
| audio editor active / owned by the main window / above it | FAIL / FAIL / FAIL | ok / ok / ok |
| audio editor still above after clicking the main window | FAIL | ok |
| plugin window active / keeps the keyboard focus | FAIL / FAIL | ok / ok |
| main window takes the focus back when it is active | ok | ok |

(The "before" binary is the same tree with the two fixes reverted; the test brings LYNX to the foreground itself,
since a process started from WSL may not be allowed to.)

## 2. MIDI controllers silent until disabled and re-enabled in Settings; MIDI activity light
[CODE] LYNX opened the enabled MIDI inputs once at start-up and again only when JUCE's device-list notification fired.
On Windows that notification comes from WM_DEVICECHANGE and only fires when the WinMM list differs at that moment
(juce_MidiDeviceListConnectionBroadcaster.cpp); a device that the system lists late (USB enumeration, the Windows MIDI
Services shim — installed on this PC [EXEC Get-PnpDevice: "MIDI 2.0 Virtual/Loop Devices"]), that was busy when LYNX
started (another program had it), or that was unplugged and plugged back with its old handle still "open", stayed
closed or dead. Turning it off and on in Settings closed and reopened it — the user's workaround.

Fix (`AudioEngine::enableAllMidiInputs`, core `prefs::planMidiInputs`):
- every 2 s (and on the device-list notification) the engine compares the available devices with the MIDI settings:
  an enabled device that is not open is opened (retried until it works); one that is "open" but was missing at the
  previous check is closed and opened again; one turned off is closed. At start-up the devices JUCE restored from the
  saved device state are reopened once.
- Settings › MIDI › "Refresh device list" also reconnects the inputs (close + open).
- toolbar: a small MIDI light in the free cell of the recording group (row 2, after the typing piano) that blinks green
  for ~0.2 s on each message from an enabled input (FL has the same light); its hint/tooltip lists the open inputs and
  a click reconnects them. Strings in pt-BR.
Tests: `bugs131_midi_inputs_open_when_they_appear_and_reopen_after_a_reconnection` [EXEC core]. No MIDI controller
or loopback port is available on this PC ([EXEC] a WinMM probe sees only Waves' StudioRack / SG ports, which don't loop
back), so the end-to-end path with a real device is not measured here.

## 3. Pattern length ignored at first (2-bar empty pattern, recording loops after 1 bar)
[CODE] Pattern mode already looped the pattern's set length. The 1-bar loop came from Song mode:
- with an empty Playlist the song length was the minimum, 1 bar, so the transport looped 1 bar until the first take
  placed a clip ("at first");
- a pattern clip keeps the length it had when it was placed: after "Length > 2 bars" the clip of the empty pattern
  was still 1 bar and the song kept looping 1 bar. FL's pattern clips follow their pattern's length [model].
Fixes: `Song::songLength()` with no clip = the current pattern's length; `followPatternLengths` (called by
`AudioEngine::editSong` after every edit): the clips of a pattern whose length changed get the new length when they
had the old one and start at the pattern's start (auto-sized); a clip resized or cut by hand keeps its length; edits
that add / remove patterns are left alone. Recording past the end of an automatic-length pattern grows its clips too.
Tests: `bugs131_pattern_length_is_respected_on_the_first_recording_in_song_mode`,
`bugs131_pattern_clips_follow_their_pattern_length` [EXEC core].

## 4. Song mode does not loop back to the start after the last clip
[CODE] Without a loop region the transport position counted on forever in Song mode; the Playlist, the renderer and
the displays wrapped it with `fmod (position, songLength)`, but several consumers used the raw position:
- hosted plugins' play head (`TimelinePlayHead`): plugins synced to the host (arpeggiators, LFOs, step sequencers, beat
  slicers) saw the song "continue forward" — in Pattern mode too;
- the clip placed for a take in Song mode (`placePatternClipAt` at the raw take start) and audio takes: after the song
  had looped once, they landed after the song's end and made it longer, so it no longer came back where expected;
- the song length changing while playing made the wrapped position jump.
Fix: in Song mode without a loop region the transport itself loops over the whole song (`playbackLoop`, the same
code path as the loop region: at the song's end the position goes back to 0, like FL). The plugins' play head reports
the position within the song / within the pattern (`hostPositionTick`). A note held across the loop's end keeps its
played length (NoteRecorder; this also fixes it for loop regions). Take positions wrap into the song.
Tests: `bugs131_song_mode_loops_back_to_the_start_at_the_song_end` [EXEC core].

## Verification
- core tests: `scripts/build-local.sh` with the FL Data dir: 1434587 checks, 0 failures (after merging main with
  feat132) [EXEC].
- Windows: `OUT=build-win-bugs131 scripts/build-windows-zig.sh` OK; `--windowtest` result=OK; `--selftest` result=OK;
  `--snapshot` toolbar shows the MIDI light [EXEC]. One `--windowtest` run out of six failed on the activation
  checks while other rounds were opening windows on the same desktop (something else took the foreground); the
  reruns passed.

## Where I stopped
- Landed on main (see the round's final report for the commit). Issue #4 got a short comment; not closed (main
  session does it after 0.1.13-alpha).
- Not measured: a real MIDI controller (none on this PC); FL's exact behaviour when playback starts after the song's
  end (LYNX now jumps to the start at once).
- Audio takes recorded across the song's loop point are still one long take from where recording started (FL makes
  one take per pass) — a possible follow-up.
