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

(sections filled below)
