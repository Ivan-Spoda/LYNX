# Round 140 — first-start wizard: no overlaps, shown over the main window

Branch `feature/wizard140`. Trigger: the AppImage catalog test (AppImage/appimage.github.io PR #10614) posts a
screenshot of LYNX's first start, and it showed only the 680x600 "Welcome to LYNX" window with a broken layout.
Evidence markers: [EXEC] run here, [CODE] read in our source or JUCE 8.0.4's, [WEB] read on the web.

## 1. What the catalog test captures
[WEB] appimage.github.io `code/worker.sh`: Xvfb at **800x600**, IceWM as window manager, the largest window is moved
and resized to fit the screen, then the **active window** is captured (falls back to the whole screen). LYNX showed
the setup wizard as its own window (`WizardWindow`, a `DialogWindow` above the main window), so the active window —
and the catalog picture — was the wizard alone.

## 2. Why it overlapped
[CODE] `SetupWizard::resized` gave the `AudioDeviceSelectorComponent` a fixed 110 px and laid "FL Studio" and the
plugin folders out below that. But the selector sizes itself: its `resized()` ends with
`setSize (getWidth(), r.getY())` (juce_AudioDeviceSelectorComponent.cpp:1184), and its height depends on the backend
and the device: on ALSA with an output device it shows Output + Test, an "Active output channels" list, and
"Show advanced settings..." (about 210 px; more when the advanced settings are opened, less on a backend without
devices). The selector drew outside its 110 px over the next sections — exactly the catalog picture.

## 3. Fix
- `SetupWizard` is now a panel with a scrolling body (`juce::Viewport`) and a fixed footer (Skip / Finish).
  `layoutBody(width)` gives the device selector the width, reads back the height it chose for itself and lays the
  rest out below it; wrapping texts (intro, FL Studio status, scan status — longer in pt-BR) get their real text
  height. A `ComponentListener` on the selector re-lays the wizard out (asynchronously) whenever it changes its own
  height (device type / device changed, advanced settings shown or hidden). When the panel is shorter than its
  content, the body scrolls.
- The wizard is no longer a separate window: `makeSetupWizardOverlay` returns an overlay that `MainComponent` puts
  over its whole area (first start and Options > Setup wizard). The DAW stays visible, dimmed, behind a centred panel
  (max 680 px wide, as tall as the content or the window allows). The overlay takes the keys (typing piano and
  shortcuts don't reach the DAW while it is open); Escape = Skip (it used to be the window's close button). Content
  and behaviour are unchanged: language, audio device + test, FL Studio status, plugin folders, background scan,
  scan-at-start, Skip / Finish, language restart offer.
- The main window's first size is clamped to the display (it was `max(design size, 88% of the display)`, so on the
  catalog's 800x600 screen the 1180x642 design size was larger than the screen).
- `--snapshot` adds `-desktop-setup.png` (wizard over the default desktop) and `-desktop-setup-small.png` (1024x600,
  scrolling); `-setup.png` is now the panel at its natural size.

## 4. Verification
- [EXEC] Linux: full JUCE build in WSL (worktree build-linux), Xvfb + xwd from extracted Ubuntu packages (no sudo;
  `Xvfb` run inside `unshare -rm` with an overlay on /usr/bin so it finds `xkbcomp`), fresh HOME/XDG dirs, ALSA with
  a null default PCM (the catalog's case: Output "default", 4 channel pairs, "Show advanced settings..."). Helper:
  tmp/wizard140/xvfbshot.sh. Screenshots in tmp/wizard140/:
  - `x800b.png` 800x600 — main window 800x600 fills the screen, wizard panel on top, nothing overlaps, body scrolls;
  - `x800c.png` — after clicking "Show advanced settings..." (xdotool): Sample rate / buffer size appear and the
    FL Studio section moves down below them;
  - `x800a.png`, `x1600a.png` — no ALSA device (empty device type list): short selector, no gaps or overlap;
  - `x1280.png` 1280x800; `x800skip.png` — after Skip: the overlay is gone, the DAW is usable.
  - `appimg-shot.png` — the catalog's broken picture, for comparison.
- [EXEC] Windows: see §5.

## 5. Windows check
[EXEC] Windows build (zig/MinGW), `LYNX.exe --snapshot` on this PC (Windows Audio backend, FL Studio 2026 found, 624
plugins known): `s-desktop-setup.png` (1280x720: the wizard over the desktop, the two-line FL Studio text gets its
height), `s-desktop-setup-small.png` (1024x600: the body scrolls, footer stays), `s-setup.png` (the panel alone,
680x617) — copies in tmp/wizard140/. A GUI start with a fresh profile was not done on Windows: the settings folder is
the real %APPDATA%\LYNX (no override), shared with the user and the other rounds' tests, and removing its
setup-done.txt would make the wizard appear in their runs; the first-start path is the same code as on Linux
(`MainComponent::showSetupWizard` after the window is revealed), verified there.

## Not done / notes
- At 800 px wide, the toolbar's right end ("Main (toolbar snap)") is clipped under the transport — pre-existing,
  visible in `x800skip.png`; not part of this round.
- The selector leaves an empty row under its last control (JUCE reserves one); the wizard trims part of it.

## Where I stopped
Done; landed (see the final report). The catalog picture updates when the AppImage catalog re-runs its test on the
next LYNX release (0.1.14).
