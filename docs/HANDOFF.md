# Handoff — state at the machine migration (2026-10-14)

`main` (pushed) is green: `scripts/build-local.sh` → 1 115 391 checks, 0 failures; `--selftest` OK; Windows build OK.

## Merged and published in this period
- Exact FL engines: Sytrus (REA 31), Harmless + its effects rack (REA 32/36), Harmor + its effects rack (REA 34/38),
  Vocodex (REA 35), Effector Reverb, Toxic filter envelope, Kepler LFO sync (REA 33).
- UI: dark theme with red accent, EN/pt-BR, FL-style floating-panel desktop (Playlist background, Channel Rack pop-up,
  F5–F9), editor tools in the editors' title bars, global snap, note/clip operations, FL-style main toolbar, new icon
  set, splash screen with a synthesized start-up sound (`docs/WORKFLOW_FL.md` has the study, gap tables and layout notes).

## Work in progress (pushed as `wip/*` branches — unverified, may not build)
| Branch | What | Source of truth | State when stopped |
|---|---|---|---|
| `wip/ogun` | Port Ogun's exact engine (`InstrumentKind::ogun`) | `docs/analise/REA_rodada37.md`, model `rea-work/r37/py/ogmodel.py` | engine ported, was running the C++-vs-model battery |
| `wip/gms-morphine` | Port GMS and Morphine engines | `REA_rodada39.md`, models `rea-work/r39/py/gmsmodel.py`, `mormodel.py` | just started (first file) |

Not started: REA round 41 (MiniSynth, DX10, Fruit Kick, BassDrum, then PoiZone/Toxic/Kepler full engines).

Resume a branch: `git worktree add -b feature/<x> ../lynx-wt-<x> origin/wip/<x>`, then build with a private zig cache
(`export ZIG_GLOBAL_CACHE_DIR=$HOME/.cache/zig-<x> ZIG_LOCAL_CACHE_DIR=$HOME/.cache/zig-<x>`), fix, verify, merge into
`main` and delete the `wip/*` branch.

## Process after every change
1. `scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"` → "0 failures".
2. `scripts/build-windows-zig.sh` (one zig Windows build at a time).
3. Regression: export the 30 demos listed in `rea-work/demos.txt` into a new `regN` folder under `lynx-test` and
   compare `peak=` / `render_ms=` with the previous one; `--selftest`; snapshots for UI changes.
4. Commit (sole author, no co-author trailer), push, copy `LYNX.exe` to the Desktop.

## Open items worth knowing
- Exact-engine export costs more CPU (FL's render settings): the 30-demo export total went from ~342 s to ~380–570 s.
- Sytrus channels have no settings panel yet (the channel button opens nothing for them).
- Harmor: image/resynthesis, arpeggio-envelope points and noise resonance are approximated (82/99 demo states within
  −60 dB with the rack).
- Performance-mode projects are exported with every clip playing from the start.
