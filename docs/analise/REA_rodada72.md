# Behavioural analysis of FL Studio 2026: round 72 (REA) — DirectWave voice engine (WORK IN PROGRESS)

> **Status: work in progress, paused 2026-10-16 (plan limit).** Not landed. Clean-room, behavioural only; markers as in
> round 69 (**[DECOMP]**, **[EXEC]**, **[DATA]**, **[model]**, "not confirmed"). Scratch: `~/.claude/jobs/f8789b49/tmp/r72/`.

Target: `Plugins/Fruity/Generators/DirectWave/DirectWave_x64.dll` (SHA-256 `3432775d…6570`, C++/MSVC). Ghidra project
`~/ghproj/r72` (analysed, whole-DLL decompilation in `gh/all.c`), disassembly `dll/dw.asm`.

## Tools
- `run/dw.exe` (EXEC harness, copy of round 69's `fk.c`): loads the DLL unmodified via `CreatePlugInstance`, restores a
  demo 0xD5 state, answers host dispatcher id 14 (path with `%FLStudioData%`/`%FLStudioFactoryData%` → real path),
  triggers/releases voices, renders; `dumpzones=` writes the program record and every zone's record + sample data as the
  plugin holds it; logs the CRT `rand()` state (`DW_RANDLOG=1` per block).
- `run/mt.exe`: calls the DLL's internal math (`sinf/cosf/expf/powf`, cutoff law, biquad design) on input files.
- `tools/dwrender.cpp` + `src/core/DirectWave.{h,cpp}` (LYNX port, not yet in build lists); `py/cmp.py` compares.

## Findings so far
- **Engine layout [DECOMP]**: render `FUN_18002b130`: 16 MIDI slots (slot k plays program k; only programs with zones
  or the current slot run), control blocks of min(remaining, 256) samples (8 with a plugin setting), per block: two
  program LFOs, clear 8 buses (dry/delay/chorus/reverb L/R), voices (`FUN_180030090`, 256 voices, summed in voice-index
  order), delay `FUN_180011f10`, chorus `FUN_1800119c0`, reverb `FUN_18002d0b0`, two program shapers, add to output.
- **Voice [DECOMP]**: mod matrix (16 slots, amount `((a−½)·2)³`; 20 sources; destinations 0–31 plus 32–47 = other
  slots' amounts, two passes; velocity→gain multiplies, unclamped); 32.32 fixed-point linear interpolation (data with
  256 frames padding, int16 × 1/32767 or float); loop modes 0–4; amp envelope linear, output squared, rates
  `1/(v⁴·fs·10 + fs·0.001)`; 2 mod envelopes and 2 LFOs per block; filters A/B: cutoff `440·2^(127v/12−5)` smoothed per
  block (`expf(−1/(blockrate·0.005))`), Q `0.70710677/(1.01 − res)`, RBJ biquads (types 1–5, coefficient formulas in
  `DirectWave.cpp`) each run twice, optional clip ±(1−shape), serial or parallel; per-sample CRT `rand()·1e−14` added
  before filtering; per-voice limiter (`expf(−1000/fs)` attack, `expf(−10/fs)` release, gain `powf(s,−1)` above 1);
  gain = mod gain × zone gain × program gain × slot volume (100/127) × expression; pan law `sqrt((1−p_slot)(1−p))`,
  `sqrt(p_slot·p)`, smoothed per sample; sends squared.
- **Demos [DATA]** (`py/survey72.py`): filter A low-pass in most zones, mod matrix defaults vel→gain, wheel→LFO2→pitch,
  vel→cutoff (0.551), key→cutoff (0.125); program reverb on in ~30 programs, chorus 3, delay 1, shapers 2. Samples are
  Ogg-in-WAV decoded by `oggio_x64.dll` to float.
- **CRT [EXEC]**: the DLL's `sinf/cosf/expf` FMA paths are ported (`sinfCrt/cosfCrt/expfCrt`): 0 mismatches on
  370 000/350 000 inputs; its `powf` equals LYNX's existing `bassdrum::powfMsvc` (0/350 000).
- **First comparison [EXEC vs model]**: Directwave.flp render-test state, one note: residual −106 dB, sample 0
  bit-identical, rand sequence identical per block. Remaining difference is in the biquad recursion: a NumPy float32
  replay with the DLL's own coefficients reproduces the DLL filter state exactly, so the C++ `BiquadState::run` (or how
  the compiler evaluates it) is the next thing to fix.

## Not done / next steps
1. Fix the biquad recursion mismatch (compare C++ vs NumPy replay; check -ffp-contract / evaluation order).
2. Port program FX (reverb `FUN_18002d0b0`, chorus, delay, shapers), filter types 6/7, zone FX (0x1FE–0x201),
   time-stretch mode, HQ interpolation (`engine+0x23c`), glide/mono/legato, alternation groups.
3. Verify on all 57 demo states; port `pow` (double) if needed; Ogg decoder comparison (libvorbis vs oggio).
4. LYNX integration (InstrumentKind::directwave, importer, RackRenderer, tests, build lists).
5. Other round scope not started: sampler velocity/pan laws + channel filter, envelope tick phase, sinc edge cases.
