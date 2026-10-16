# REA round 73 — Newtone and Newtime (WORK IN PROGRESS, paused 2026-10-16)

> Clean-room, behavioural only: layouts and behaviour in our own words; no Image-Line/zplane code, pseudocode or data in
> the repository. Markers: [DECOMP], [DATA], [EXEC], [model], "not confirmed". Scratch: `~/.claude/jobs/f8789b49/tmp/r73/`.
> **Status: not landed; the C++ port is NOT build-verified yet.**

Fruity Dance (also in this round's queue) was already done in round 68 (silent channel, state kept); nothing added here.

## 1. Targets
- `Plugins/Fruity/Effects/Newtone/Newtone_x64.dll` (Delphi front-end, SHA-256 3a47d994…), `Newtone_Engine_x64.dll`
  (a JUCE 6 VST2 by zplane, "elastique Pitch", SHA-256 24bb7d31…), `Plugins/Fruity/Effects/Newtime/Newtime_x64.dll`
  (SHA-256 b9d4f90f…; it loads `..\Newtone\Newtone_engine_x64.dll`). The engine is loaded **in process** [EXEC].
- Installation states [DATA]: only plugin-database entries and `Data/System/PitchCorrectionTrack.fst` /
  `WarpCorrectionTrack.fst`, all with an empty project (0 tracks). No demo project uses either plugin.

## 2. State layout [DECOMP SaveRestoreState Newtone `0x792690`, Newtime `0x7931f0`]
- i32 version (Newtone writes 0, loads ≤ 0; Newtime writes 1, loads ≤ 1), then records `{i32 id, i64 size, data}`:
  - 1 (0x28 B): i32 editor width, i32 editor height, u32 option flags, then engine bytes (Newtone: +12 clamped 0…255,
    +13 clamped 0…23; Newtime: +12 0…255, +14 a mode 0/1/2).
    Flags: 0x4 **slave to host playback** (button `SyncBtnClick`), 0x8 ignore host selection, 0x10 ignore own selection,
    0x1 = NOT (+0x204), 0x2/0x20/0x80/0x100/0x200/0x400/0x1000 engine/editor options (Newtime always sets 0x40).
  - 2: FL's generic VST-wrapper record (i32 format −9, flags byte, size as i32 or −2 + i64, i32 program) around the
    engine chunk = JUCE binary XML (`VC2!`, u32 size, text). XML: `NewToneProject` (NumOfTracks, PitchView_X/Y/W/H),
    `Tracks/Trk_N` (TrackNum, Type = `class CzplJPositionablePSOLASource` (Newtone) or `class CzplJTimelineWarpSource`
    (Newtime), FilePath = the engine's working copy in %TEMP%), child `AudioObject` (global settings, `Pitch_N` notes
    with Orig*/Cur* values) [DECOMP engine `0x18026ab40`/`0x18026e780` + EXEC].
  - 3: the source sample path as the host stored it, ANSI bytes, no terminator (save stream flag 0) [DECOMP 0x78df30].
    On load a non-empty path is re-read into a new working copy and replaces the XML FilePath.
  - 4 (12 B), 5 (16 B), 6 (32 B): engine settings (kept raw).

## 3. Render [DECOMP Eff_Render Newtone `0x790e80`, Newtime `0x791a70`, Dispatcher `0x78f9f0` + EXEC]
- Without engine: input copied to output. With engine: the engine processes the input; with no track, or when not
  in slave mode, or when FL is not playing, the output is the input **bit-identical** [EXEC, 44.1 kHz, both plugins].
- Slave mode: Dispatcher 12 (SetPlaying) starts/stops the engine transport, 13 (SongPosChanged) re-seeks; position =
  host time 36 (format 1, ms) × 0.001 × rate, minus host time 38 when "ignore host selection" is on; own selection
  offsets/limits it. Then the position advances by block length.
- Harness tips (run/fx.c): answer zobj method 0 id 0x3fc with a user-data folder (else "Unable to create directory"),
  send SetTimeSig (14) before playing (else integer divide by zero), load a sample with Dispatcher 18 (path).

## 4. Measured output in slave mode [EXEC, our own synthetic samples]
- Newtone, unedited track, from song start, project rate = sample rate: output = input + source sample,
  residual −138 dB (rounding), except the last ~33 samples of the source (−36 dB, an end fade).
- Newtone starting mid-song: the PSOLA engine aligns to its pitch marks: ±~50 samples off the nominal position and the
  offset changes at note boundaries — not reproduced.
- Newtone at 48 kHz with a 44.1 kHz source: not a plain resampling (alignment wanders) — not confirmed / not reproduced.
- Newtime, unwarped track, from 0: the élastique timeline stretcher is not transparent at ratio 1 (−5.4 dB vs source).
- Pitch/time edits (zplane engines): not reproduced.

## 5. Port (in branch feature/r73, NOT build-verified)
`src/core/NewtoneFx.{h,cpp}` (state parser, edit detection, effect: pass-through + source sample added in slave mode
while playing, linear resampling at other rates), `EffectKind::newtone/newtime` (+ `EffectSlot::samplePath`),
`Effect::setPlaying` (RackRenderer passes the transport; parallel container forwards it; smart-disable exempts the
two kinds), FlEffects (`effectFromFl`), FlpImport (source from record 3 via resolveFlPath, else XML FilePath; report),
ProjectFile ("newtone"/"newtime", "sample"), mixer slot menu "Source sample", pt-BR strings. Tests not written yet.
