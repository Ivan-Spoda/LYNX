# Round 161b (linuxport161): Linux GUI speed, plugin sandbox, yabridge, AppImage, crash reports

2026-10-25. Part of the Linux-only round before 0.1.17 (the sibling round 161a did the PipeWire/ALSA audio backend; this
round did not touch the audio device layer). Test machine: WSL2 Ubuntu 26.04 on a Ryzen 5 3600, WSLg (X11 through
XWayland), sound through WSLg's PulseAudio server; no sudo, so every test library sits in a user prefix (`~/opt/r161b`).

Evidence markers: [MEAS] measured here (script / data named); [TEST] a LYNX test mode run here (`--sandboxtest`,
`--uiperf`, `--opentest`, ...), result file named; [CODE] read in LYNX's or JUCE's source; [SHOT] seen on a screenshot.

Data and tools (outside the repo): `~/opt/r161b` (env.sh, run.sh, build.sh, uiperf-all.sh, perfcg.py, perfsym.sh,
fake-appimagetool.sh, yabridge/Wine prefix, test plugins under `root/`, crash test plugin `src/crashsynth.lv2` from
`testsrc/crashsynth.c`), result files in `~/opt/r161b/out/`; projects: the 10 FL projects of `D:\lynx-work\flrea151`.

## 1. Summary

| # | Item | Before | After | Status |
|---|---|---|---|---|
| 2 | Mixer maximised while playing (10 projects, mean frame) | 1.1–27.0 ms, 76 frames > 16 ms | 0.8–4.2 ms, 0 frames > 16 ms | done |
| 2 | Mixer scroll (mean frame) | 11.1–41.3 ms, 209 frames > 16 ms | 7.3–11.0 ms, 1 frame > 16 ms | done |
| 2 | Playlist scroll down/up (mean frame) | 5.2–34.9 ms, 96 frames > 16 ms | 2.1–3.9 ms, 0 | done |
| 2 | Whole window repainted (mean frame) | 14.6–39.4 ms, 214 frames > 16 ms | 10.3–16.2 ms, 18 (15 of them in one project) | better, not solved |
| 3 | Plugin sandbox on Linux | "Windows only" | VST3 + LV2, hang noticed in 0.5 s, crash in ~5 ms, LYNX keeps running | done |
| 4 | Windows plugins through yabridge 5.1.1 + Wine 10.0 | not checked | scan, window, playback, export, sandbox all work | done (checked, docs) |
| 5 | AppImage menu entry / `.lynx` file type | only the .tar.gz (`install.sh`) put LYNX in the menu | switch in the setup wizard and Options › General, `--menu-entry on/off`; follows a moved AppImage | done |
| 5 | AppImageUpdate | not checked | 0.1.15 AppImage updated itself to 0.1.16, SHA-256 equal to the release's | done (checked) |
| 6 | Crash files on Linux | JUCE's backtrace only, empty "frames" part, no signal | `signal 11 (segmentation fault)` + module+offset frames | done |
| 6 | Hang watch on Linux | not checked | `--opentest` hang watch OK; `--send-test-report --dry` builds the report | done (checked) |

## 2. GUI speed on Linux (X11 / XWayland)

[CODE] JUCE 8.0.4 on Linux draws everything with its software renderer (`LinuxRepaintManager` → an `Image` per repaint
region, `Image::clear` every time, then `XPutImage`/shm). Paths are scan-converted into `EdgeTable`s; a filled rounded
rectangle or a knob arc is a full path fill each time. Untransformed ARGB image blits are cheap, alpha-mask blits
(`clipToImageAlpha`) are not.

[MEAS] New test mode `LYNX --uiperf project report` (Main.cpp): opens the project in a real window (1689×950, scale 1),
then times painted frames (MainWindow paint → paintOverChildren) for: desktop while playing, Mixer maximised while
playing, Mixer scroll (60 wheel steps), Playlist maximised while playing, Playlist scroll down/up and sideways (80),
whole window repainted (40), Channel Rack scroll (60); also process CPU and the mouse-position query cost.
Profiles with `perf` on a frame-pointer build (`build-linux-fp`), symbolized with `perfcg.py` (this perf can't).

Hot spots found (Thief, Mixer maximised): ~70 % of the message thread in `fillRoundedRectangle` (mixer strips, plates,
buttons) and `drawKnob` (arc paths) → EdgeTable scan conversion. The mouse-position query (XQueryPointer, suspected
before) costs 0.05–0.09 ms: not a problem.

Fix (`src/ui/Theme.cpp`, Linux/BSD only; other systems unchanged):
- `theme::fillRoundRect (g, rect, corner, colour)`: small shapes (≤ 48×48 physical px) come from a cached ARGB sprite;
  larger ones are 9-sliced (4 cached corner sprites + plain rectangles), positions quantised to 1/8 px; integer scale
  1–3 only (else the path fill as before); clip-culled. Worst anti-aliasing difference to JUCE's own fill: 48/255 on
  an edge pixel (not visible). Mixer and Channel Rack use it for all their rounded rectangles.
- `drawKnob`: clip-culled everywhere; on Linux an ARGB sprite per (size, value quantised to 1/1440, colours).
- Sprite cache capped at 3000 entries.

[MEAS] `uiperf-all.sh` over the 10 projects, same machine, before = main (005ee1a4) / after = this round
(`~/opt/r161b/out/perf-before`, `perf-after`). Mean painted-frame time, in brackets frames over 16 ms:

| Project | desktop playing | Mixer max. playing | Mixer scroll | Playlist max. playing | Playlist scroll | whole window |
|---|---|---|---|---|---|---|
| thief | 2.0→0.7 | 6.3→1.6 (1→0) | 16.3→8.3 (30→1) | 2.1→1.3 (1→0) | 5.2→2.1 | 19.1→12.3 (36→0) |
| neverout | 1.9→0.7 | 5.9→1.7 (1→0) | 18.0→11.0 (42→0) | 1.5→1.1 | 8.4→3.0 | 17.0→10.4 (27→0) |
| oxygen | 2.0→0.7 | 27.0→3.2 (37→0) | 41.3→10.4 (57→0) | 4.7→1.1 (2→0) | 22.2→3.2 (26→0) | 28.7→13.0 (40→0) |
| awake | 2.1→0.7 | 4.6→1.1 (1→0) | 15.1→10.1 (21→0) | 4.8→1.4 (1→0) | 34.9→3.9 (70→0) | 39.4→16.2 (39→15) |
| gente morta | 2.4→0.7 | 20.9→4.2 (34→0) | 15.5→9.2 (21→0) | 2.7→1.5 (1→0) | 7.3→3.3 | 17.9→13.8 (37→3) |
| twin swag | 2.0→0.7 | 1.1→0.8 | 11.1→7.3 | 2.3→1.3 | 7.1→2.8 | 15.4→10.3 (7→0) |
| MINA INSEGURA | 2.0→0.7 | 4.4→1.0 | 13.9→8.0 (17→0) | 2.5→1.4 (1→0) | 7.0→2.8 | 15.6→11.8 (16→0) |
| yunk | 2.1→0.6 | 5.5→1.5 (1→0) | 13.6→7.8 (5→0) | 2.4→1.2 | 7.1→3.7 | 14.9→11.2 (6→0) |
| WIU | 1.9→0.8 | 5.1→1.5 (1→0) | 13.3→7.7 (2→0) | 2.3→1.1 | 6.7→3.3 | 14.8→10.8 (4→0) |
| wiu-big | 2.0→0.8 | 6.1→1.4 | 14.8→7.8 (14→0) | 2.4→1.2 | 6.1→3.2 | 14.6→11.7 (2→0) |

All 20 runs ended `result=OK`. Load times are not compared (the "before" pass ran first with a cold waveform cache).

Not solved: a full-window repaint still costs 10–16 ms. What remains is JUCE's own per-repaint `Image::clear`, overdraw
from nested `fillAll`s and glyph rendering (text can't be sprite-cached without knowing its colour from the outside).
Further gains would need either an OpenGL context for the main window or fewer whole-window repaints.

## 3. Plugin sandbox on Linux

[CODE] Before: `createSandboxedPlugin` returned "Windows only" off Windows. Now `engine/PluginSandbox.cpp` has a
`SandboxChannel`: Windows keeps its named file mapping + events; Linux/BSD use POSIX shared memory
(`/LYNX-sandbox-<pid>-<n>`, unlinked as soon as the child has opened it) holding two process-shared `sem_t` and the audio
block; waits use `sem_clockwait` on CLOCK_MONOTONIC with the same 0.5 s deadline. On Linux a watcher thread polls a
`pidfd` of the child: when the child dies the plugin is marked dead and the waiting audio block is woken at once (before:
up to the 0.5 s timeout per block). The child process also knows LV2 (`PluginHost::findLv2Plugins`, lilv) and `.lv2`
bundle folders can be added as files.

[TEST] `LYNX --sandboxtest <plugin> report` (extended: instruments get a note, effects a 440 Hz tone; parameter → state
→ in-process plugin compared to the same steps without the sandbox; a hung child; a second instance that aborts;
`--expect-crash` for a plugin that crashes by itself; `--editor` opens the plugin window in the child):

| Plugin | Result | Hang noticed | Crash noticed |
|---|---|---|---|
| Nekobi (DPF) VST3 and LV2, Kars VST3 | OK | 0.50 s | 5 ms (510 ms before the pidfd watcher) |
| Dragonfly Hall Reverb LV2 | OK | 0.50 s | 5.4 ms |
| Dragonfly Room Reverb VST3 | OK | 0.50 s | 5.3 ms |
| LSP Multi-Sampler x12 VST3 | OK | 0.50 s | 5.4 ms |
| crashsynth.lv2 (test plugin, segfaults 0.5 s after start) `--expect-crash` | OK: dead=1, muted, LYNX keeps running | — | during the first second |
| Valhalla Supermassive (Windows VST3 through yabridge) | OK | 0.50 s | 12 ms |

[MEAS] No `/dev/shm/LYNX-*` left after the runs. The sandbox switch in Options › Plugins was already shown on every OS.

## 4. yabridge + Wine 10.0

[MEAS] yabridge 5.1.1 and Wine 10.0 unpacked into a user prefix (`~/opt/r161b/wine`, wrapper `yab.sh`; the prefix
needed `zlib1.dll` in system32/syswow64 and a `share/wine/wine` symlink — packaging details of an unpacked Wine, not
LYNX issues). Valhalla Supermassive (free Windows plugin) as VST3 and VST2 bridges:
- scan of the yabridge folder: 4 plugins in 3.7 s; `--plugintest` OK;
- [SHOT] plugin window shown in-process and from the sandbox child;
- export of the showcase project with the plugin on the master (52 s of audio): in-process VST3 6.0 s, sandboxed
  VST3 7.4 s, VST2 5.2 s; the sandboxed export is bit-identical to the in-process one.
Documented in `docs/PLATFORMS.md` (“Windows plugins on Linux”). Nothing extra is needed by LYNX.

## 5. AppImage

- [TEST] Offline start: the AppImage (built locally with `scripts/package-linux.sh`) starts with no network
  (`unshare -rn`, `APPIMAGE_EXTRACT_AND_RUN=1`, no FUSE in WSL).
- Desktop integration (new, `platform/FileTypes.cpp`): when LYNX runs as an AppImage (`$APPIMAGE`), the setup wizard
  (default on) and Options › General offer “Show LYNX in the applications menu and open .lynx files with it”. It writes
  `~/.local/share/applications/com.lynx_share.LYNX.desktop` (Exec = the AppImage's path, quoted per the desktop entry
  spec, `X-LYNX-AppImage=true` marker), the icon and the MIME type, then runs `update-mime-database` /
  `update-desktop-database` if present. Each start rewrites the entry if the AppImage has moved. `--menu-entry on|off`
  does the same from the command line. Turning it off leaves the icon if the .tar.gz `install.sh` entry uses it.
  [TEST] on/off and the move (to a folder with a space) checked with `XDG_DATA_HOME` pointing at a scratch folder.
- [TEST] AppImageUpdate: the released 0.1.15 AppImage carries `gh-releases-zsync|Ka1-S|LYNX|latest-all|LYNX-*-x86_64.AppImage.zsync`;
  `appimageupdatetool -j` reported the update, `-O` updated the file to 0.1.16 (fetched 11.4 MB, reused 1.1 MB) and its
  SHA-256 equals the release's `SHA256SUMS.txt` line.
- Local note: appimagetool 1.9.0 exits 1 without a message in this WSL (it also failed on a minimal AppDir); for local
  tests `~/opt/r161b/fake-appimagetool.sh` (runtime + mksquashfs) was used via `APPIMAGETOOL=`. The release CI is not
  affected.

## 6. Crash reports and hang watch on Linux

- [TEST] Hang watch: `--opentest` → `hang watch: OK` (stack file with 12 frames, LYNX+offset names).
- [TEST] A real crash (SIGSEGV sent to a running LYNX): crash file and recovered project written. Before this round
  the crash file only had JUCE's backtrace and an empty "frames" part on Linux; now `crashguard::exceptionBacktrace`
  writes `signal 11 (segmentation fault)` and the module+offset frames (JUCE passes the signal number).
- [TEST] `--send-test-report out.txt --dry`: report built (1.9 KB JSON, kind crash, home folder replaced by `<user>`).
- Seen, not changed: the report's settings say `gpuRendering: true` on Linux, where the option doesn't exist.

## Where I stopped

Everything above is committed on feature/linuxport161 and landed (see the final report for commit ids). Open:
- Full-window repaint on Linux still 10–16 ms (section 2); next step would be an OpenGL-backed main window or fewer
  whole-window repaints.
- `--symbolize` reads Windows `LYNX.exe+0x` names only; Linux crash/hang frames (`LYNX+0x`) need `addr2line` on a
  build with symbols.
- macOS still has no sandbox (the POSIX channel would mostly work there, but `sem_clockwait` and pidfd are Linux-only;
  untested, so off).
