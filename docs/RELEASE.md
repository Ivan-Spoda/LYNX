# Release steps

Official release files come from GitHub Actions (`.github/workflows/release.yml`, see `docs/CODE_SIGNING.md`):
a tag push builds `LYNX.exe` from the tagged source, runs the self-test on Windows and attaches to a draft release:

- `LYNX-x.y.z-setup-x64.exe` — per-user installer (NSIS, `resources/installer/LYNX.nsi`, no admin rights): installs to
  `%LOCALAPPDATA%\Programs\LYNX`, Start-menu shortcut, optional desktop shortcut, `.lynx` association, optional
  "Open with LYNX" for `.flp` (FL Studio stays the default program), uninstaller in *Apps & features*. The workflow
  also installs and uninstalls it silently as a check.
- `LYNX-x.y.z-windows-x64.zip` — portable build (extract and run), with `Demo.lynx`.
- `LYNX.exe` and `SHA256SUMS.txt`.

Steps:
1. Bump the version in `CMakeLists.txt` (`project(LYNX VERSION x.y.z)`, the single source) and the download badge in
   `README.md`.
2. Locally: `scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"` → `0 failures`;
   `flock ~/.cache/lynx-winbuild.lock scripts/build-windows-zig.sh`; from a Windows folder `LYNX.exe --selftest r.txt`
   (`result=OK`) and `LYNX.exe --scantest <folder with a VST3/CLAP> r.txt` (a test CLAP: `tools/testplugin`).
3. Optional local package check: `scripts/make-installer.sh` (defaults to `build-win/LYNX.exe`) writes the installer,
   the zip and `SHA256SUMS.txt` to `dist/`. NSIS: `makensis` on PATH, `$MAKENSIS`, or the NSIS zip release unpacked
   into `~/opt/nsis-<version>` (its `makensis.exe` runs through WSL interop). Quick installer check:
   `setup.exe /S /D=C:\some\test\folder`, then `C:\some\test\folder\Uninstall.exe /S`.
4. `git tag vx.y.z-alpha && git push origin vx.y.z-alpha`; when the workflow finishes, edit the draft release notes
   and publish it.
