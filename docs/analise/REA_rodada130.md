# Round 130 — Linux AppImage for the AppImage catalog (glibc 2.35)

Goal: the AppImage catalog PR (AppImage/appimage.github.io#10614) failed its test on Ubuntu 22.04 (glibc 2.35, the
oldest Ubuntu LTS in standard support): `LYNX-0.1.10-alpha-linux-x86_64.AppImage` needed `GLIBC_2.36`/`GLIBC_2.38`
and quit at once; the name check also warned about "linux" in the file name.

Evidence markers: [EXEC] = run here or in CI, [DOC] = from documentation / the catalog's test report.

## Findings

- [DOC] The catalog's log: `libm.so.6: version GLIBC_2.38 not found`, `libc.so.6: version GLIBC_2.36 not found`;
  "not self-contained: uses the C library of the system; references glibc 2.38". Name warning: AppImages should be
  `App-1.0-x86_64.AppImage`, not `...-linux-x86_64.AppImage`.
- [EXEC] The old binary (built on ubuntu-24.04, and the local WSL build on glibc 2.43) imports `__isoc23_strtol*`,
  `__isoc23_sscanf`, `fmod`/`fmodf` (GLIBC_2.38), `arc4random` (GLIBC_2.36) and, locally, `log10f`/`sqrtf`/`atan2f`
  (GLIBC_2.43): newer glibc headers redirect these calls to new symbol versions. Only building against an older
  glibc avoids them. libstdc++/libgcc were already static (`CMakeLists.txt`), so no `GLIBCXX_` import exists.
- [EXEC] The binary's NEEDED libraries are only `libfontconfig.so.1 libfreetype.so.6 libasound.so.2 libm libc ld-linux`
  — all on the AppImage excludelist (expected on every desktop system), so nothing else has to be bundled.

## Changes

- `.github/workflows/release.yml`, job `linux`: runs in an `ubuntu:22.04` container (glibc 2.35) with GCC 13.4 from
  `ppa:ubuntu-toolchain-r/test` (C++20, same major as ubuntu-24.04's default). New steps:
  `scripts/check-linux-compat.sh` (fails when any `GLIBC_` symbol version is above 2.35, or the binary needs the
  system's libstdc++/libgcc_s or imports `GLIBCXX_`/`CXXABI_`) and `scripts/check-appimage-start.sh` (fresh `$HOME`,
  no network through a network namespace — the container gets `--cap-add=SYS_ADMIN` for that — Xvfb, passes when LYNX
  is still running after 12 s and a top-level "LYNX" window exists; `REQUIRE_OFFLINE=1` in CI).
- `scripts/package-linux.sh`: AppImage named `LYNX-<label>-x86_64.AppImage` (the .tar.gz keeps
  `LYNX-<label>-linux-x86_64.tar.gz`); desktop file inside the AppImage named after the AppStream id
  (`com.lynx_share.LYNX.desktop`); AppStream metadata `usr/share/metainfo/com.lynx_share.LYNX.appdata.xml` from
  `resources/linux/com.lynx_share.LYNX.appdata.xml` with the build's version and commit date; `--no-appstream` dropped.
- README, `docs/PLATFORMS.md`, `docs/RELEASE.md`, `website/index.html`: Linux minimum now glibc 2.35 (Ubuntu 22.04,
  Debian 12, Fedora 36); new AppImage name; the website's download detection (`/\.AppImage$/i`) already matches it.
  `SHA256SUMS.txt` is generated over whatever files exist (no name list to change).

## Verification

- [EXEC] CI run (workflow_dispatch on wip/appimage130): https://github.com/Ka1-S/LYNX/actions/runs/39975298403 —
  all jobs green. Linux job log: `ldd (Ubuntu GLIBC 2.35-0ubuntu3.15) 2.35`, `g++-13 (Ubuntu 13.4.0-6ubuntu1~22~ppa2)`;
  `glibc symbol versions: 2.2.5 … 2.34 2.35`, `highest: GLIBC_2.35 (allowed: GLIBC_2.35)`, `OK: runs with glibc 2.35 and
  no system C++ runtime`; self-test `result=OK`; AppImage start: windows `"LYNX setup"` (first-run dialog) and `"LYNX"`
  after 12 s, `OK: LYNX is running after 12 s with its window` (that run still had network: the container could not
  create a network namespace; fixed by the second run below).
- [EXEC] Downloaded artifact `LYNX-0.1.12-1b3d399-x86_64.AppImage`, extracted: `objdump -T usr/bin/LYNX` highest
  `GLIBC_2.35`, 0 `GLIBCXX` imports; `appstreamcli validate-tree --no-net` (AppStream 1.1.2) → "Validation was
  successful: pedantic: 1" (the pedantic note: upper-case letters in the id); `desktop-file-validate` OK.
- [EXEC] Second CI run with the offline start required: https://github.com/Ka1-S/LYNX/actions/runs/39977698864 —
  all jobs green; `highest: GLIBC_2.35`, `result=OK`, `network: none (unshare -n)`, windows `"LYNX setup"` and
  `"LYNX"`, `OK: LYNX is running after 12 s with its window`.
- Not done: a local ubuntu:22.04 container test (no docker/podman on this PC, no sudo); appimagelint (needs FUSE to
  mount the AppImage; not available in WSL without sudo). The CI container is the 22.04 test instead.

## Where I stopped

- Landed on main via the merge lock (see the final report for commit ids). No tag, no release, no comment on the
  catalog PR: the main session releases v0.1.13-alpha and comments `/retest` on AppImage/appimage.github.io#10614.
- The website text (glibc 2.35) should be deployed (`scripts/deploy-site.sh`) only after v0.1.13-alpha is published,
  because v0.1.12-alpha's AppImage still needs glibc 2.39.
- `tests.yml` still builds on ubuntu-24.04 (fine: it tests, it does not ship binaries).
