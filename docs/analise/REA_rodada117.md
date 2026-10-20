# Round 117 — LYNX on macOS and Linux

Goal (user, 2026-10-20): "Make the version for Mac and Linux too! And update the website and GitHub." Target:
v0.1.8-alpha ships Windows + macOS + Linux. Platform overview: `docs/PLATFORMS.md`.

## Findings

- [EXEC] The code was already mostly portable: of ~15 files with `_WIN32`/`JUCE_WINDOWS`, only `ClapFormat.cpp` and
  `Vst2Format.cpp` failed to compile on Linux (`juce::HWNDComponent` used unguarded). Everything else was guarded with
  a working (or stub) fallback.
- [EXEC] First native Linux build (Ubuntu 26.04 in WSL, GCC 15, JUCE 8.0.4 via CMake): `LYNX --selftest` →
  `result=OK` in 0.3 s (wavpack=NO_FL: FL Studio is not installed for Linux).
- Settings paths were `userApplicationDataDirectory/LYNX` in 7 places; JUCE maps that to `~/.config` (Linux, fine) but
  to `~/Library` on macOS (wrong place). Now `platform::settingsFolder()` (Application Support on macOS, XDG on Linux).
- The worker pool's non-Windows semaphore was `std::counting_semaphore`; replaced by explicit OS semaphores
  (libdispatch on macOS, futex-backed POSIX `sem_t` on Linux) so the wake-up latency does not depend on the C++
  library's implementation (the reason it was avoided on Windows, REA round 87).
- CLAP on Linux needs the host's `timer-support` and `posix-fd-support` for plugin GUIs (X11 event handling);
  both added (timer-support on every OS).
- macOS `.clap` plugins are bundles: the binary is `Contents/MacOS/<name>`; `clap_entry.init` still gets the bundle
  path.
- FL Studio data is only looked for where it can exist: Windows install; Linux → a Windows FL Studio in a Wine prefix
  (`$WINEPREFIX` or `~/.wine`); macOS → `/Applications/FL Studio*.app/Contents/Resources/FL` ("not confirmed": no
  Mac to check the bundle layout). `LYNX_FL_INSTALL` overrides.
- Float determinism: all builds now use `-ffp-contract=off` (Apple Silicon has FMA; clang would fuse `a*b+c`
  otherwise and change the exact engines' results).

## Windows-only features (and why)

- ASIO, LYNX ASIO driver: ASIO is a Windows driver model.
- VST2 hosting: the loader reads Windows DLLs; VST2 is deprecated, macOS/Linux builds of VST2 plugins are rare.
- Plugin sandbox (separate process): built on Win32 shared memory + events; a POSIX port is possible later.
- Dark native title bar / owned tool windows: Windows DWM APIs.

## Where I stopped

- Linux app builds and self-tests locally; CI (tests.yml app jobs, release.yml dispatch on wip/port117) running.
- Next: export comparison Linux vs Windows on demo projects, `--snapshot` PNGs on Linux, AppImage locally,
  macOS CI iteration, website/README (in progress), land.
