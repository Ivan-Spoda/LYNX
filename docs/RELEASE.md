# Release steps

1. Bump the version in `CMakeLists.txt` (`project(LYNX VERSION x.y.z)`) and in `scripts/build-windows-zig.sh`
   (`JUCE_APPLICATION_VERSION_STRING`), and the download badge/text in `README.md`.
2. Core build and tests: `scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"` → `0 failures`.
3. Windows build: `flock ~/.cache/lynx-winbuild.lock scripts/build-windows-zig.sh`, then from a Windows folder
   `LYNX.exe --selftest report.txt` (`result=OK`) and `LYNX.exe --scantest <folder with a VST3/CLAP> report.txt`.
4. Package: `scripts/make-installer.sh` (defaults to `build-win/LYNX.exe`; generates `Demo.lynx` with `--showcase`).
   It writes to `dist/`:
   - `LYNX-x.y.z-setup-x64.exe` — per-user installer (NSIS, no admin rights): installs to
     `%LOCALAPPDATA%\Programs\LYNX`, Start-menu shortcut, optional desktop shortcut, `.lynx` association, optional
     "Open with LYNX" for `.flp` (FL Studio stays the default program), uninstaller in *Apps & features*.
   - `LYNX-x.y.z-windows-x64.zip` — portable build (extract and run).
   - `SHA256SUMS.txt`.
   NSIS: `makensis` on PATH, `$MAKENSIS`, or the NSIS zip release unpacked into `~/opt/nsis-<version>` (its
   `makensis.exe` runs through WSL interop). Quick check of an installer:
   `setup.exe /S /D=C:\some\test\folder`, then `C:\some\test\folder\Uninstall.exe /S`.
5. Tag and publish: `git tag vx.y.z-alpha && git push origin vx.y.z-alpha`, then
   `gh release create vx.y.z-alpha dist/LYNX-x.y.z-setup-x64.exe dist/LYNX-x.y.z-windows-x64.zip dist/SHA256SUMS.txt --notes-file notes.md`.
