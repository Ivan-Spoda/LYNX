# Behavioural analysis of FL Studio 2026: round 65 (REA) — Plucked!, BeepMap and Sawer: exact engines

> **Nature of this document.** First decoding of three FL generators LYNX did not play at all (imported channels were
> reported unsupported and fell back to the default synth patch): Plucked! (a plucked-string model), BeepMap (an image
> synthesiser) and Sawer (a virtual-analogue synth). *Clean-room* reverse engineering, behavioural only. No Image-Line
> code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No FLEX pack or encrypted content
> was touched; no Image-Line preset, image or audio data was copied into the repository (BeepMap's bitmap comes from
> the project's own plugin state, Sawer's oscillator cycle `data.saw` is read at run time from the user's
> installation).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the official
> Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's own DLL,
> unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Inferences made
> *from* the code are stated as **inference**. Results of our own implementation (which implements only DECOMP/EXEC
> facts) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own implementation, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r65/`.
- **Targets** (copies in `bin/`):

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Generators/Plucked!/Plucked!_x64.dll` | `290e4edb57352b9aea3cd865a813d3d16a9d3375abda59464fb10eb4e801be3d` | Delphi, image base 0x400000, all DSP inside |
  | `Plugins/Fruity/Generators/BeepMap/BeepMap_x64.dll` | `249560b7bd3471631ff459a4a409c4424de39dc0b0df2eee09b9e59820e8779c` | Delphi, image base 0x400000, all DSP inside |
  | `Plugins/Fruity/Generators/Sawer/Sawer_x64.dll` | `6f70e68f125e56bc037c557cd32eaa6b8b63a84f015e05ed20027d2ed5f5ac25` | Delphi front-end |
  | `Plugins/Fruity/Generators/Sawer/engine_x64.dll` | `b86e837cd8dc2687b2d4bcd02899896061705f79f4bfaaaaa91da0e7073eb4e0` | C++/MSVC engine (RTTI `CSynthesizer`, `CPluginWrapper`, `TCPPFruityPlug`; export `CreatePlugInstance`) |
  | `Plugins/Fruity/Generators/Sawer/data.saw` | `d5bed51900e13c2742a0255eb273cf0bdb0737d2015633b00bb58018735db49b` | 8192 float32 (one oscillator cycle), plain |

- **Tools:** Ghidra 12.1.4 headless (JDK 21; projects `~/ghproj/r65_{pl,bm,sw,se}`, scripts `gh/imp.sh`, `gh/g.sh`,
  `gh/scripts/DecompAll.java`); decompilations `out/pl_plug.c` (Plucked! `0x6b6000…0x6bc000`), `out/bm_plug.c`
  (BeepMap `0x71a000…0x734000`), `out/se_all.c` (Sawer engine, all of `.text`, split per function into `sw/fn/`);
  `objdump -M intel` of every DLL (`dis/*.asm`) wherever the operation order matters. Function matching against
  3x Osc (round 47's `r47/py/fpair.py` on `r47/out/all.asm`). Our PE reader `py/pe.py` reads constants.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `run/pl.c` → `pl.exe`, a copy of round 57's `ag.c` (same
  safety measures: `SetErrorMode`, vectored fault handler that ends the process, the installation's DLL loaded
  unmodified from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the
  plugin's own SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Additions this
  round: the host's `WaveTables[0…5]` at host `+0x18…` built with FLEngine's formulas and its ComputeLRVol (both from
  round 47's `ox.c`); host temp buffers at `+0x68…+0x78`; host slots 11 and 12 ("add a mono / stereo buffer into the
  output with left/right levels and ramps", plain products — FL's own ramp law is **not confirmed**, as in round 29);
  `rs=<va>,<seed>` sets the DLL's Delphi `RandSeed` after the state load and logs it at every note-on; `hostkill`
  makes the fake host really kill a voice (Voice_Kill, slot 11 of the plugin) after the block in which the plugin
  asked for it; `arr=-3,…` peeks inside a second module (Sawer's `engine_x64.dll`); `law=x87|none` picks the host pan
  law. Drivers `py/run65.py`; comparisons `py/plcmp.py`, `py/plcpp.py`, `py/bmcpp.py`, `py/bat_bm.py`, `py/t_bm1.py`;
  goldens `py/gold_pl.py`, `py/gold_bm.py`; our scratch C++ drivers `cpp/plref.cpp`, `cpp/bmref.cpp` (they link the
  worktree's engines). No process or dialog was left behind; outputs are deterministic for a given `RandSeed`.

---

## 0. Data survey **[DATA, `py/survey65.py`, `py/states.py`, `st/`]**

No project of the installation (Demo projects, Templates, System) uses Plucked!; `System/Render tests/Sawer.flp` and
`System/Render tests/Beepmap.flp` hold one channel each. Presets: Plucked! 1 (`Default`, = the plugin database
entry), BeepMap 7 (+ database entry), Sawer 421 (+ database entry). All Sawer states are 620 bytes; all BeepMap
states are version 0 with a 32-bit BMP (the render test's is stored top-down, the others bottom-up).

---

## 1. Plucked!

### 1.1 State and parameters **[DECOMP `FUN_006ba380` (SaveRestoreState), `FUN_006ba660` (ProcessParam); DATA]**

The state is 20 bytes: five int32 knobs, decay (+0x120, 0…256), colour (+0x124, 0…128), normalize, gate, widen
(+0x128…+0x130, switches); the loader re-applies the five through ProcessParam. Default preset: 128, 64, 1, 0, 1. Names
**[DOC, `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Plucked!.htm`]**: Decay,
Color, Normalize, Gate, Widen. Setting the decay sets a flag that makes every voice recompute its feedback gain at
its next render. ProcessEvent 1 (max polyphony) is stored at +0xe0.

### 1.2 Note-on: the delay line **[DECOMP `FUN_006baf40` (TriggerVoice), `FUN_006bab70`; EXEC]**

- Pitch: `c = round(Final.Pitch)` (cents relative to C5; Delphi `Round` = `cvtsd2si`, half to even); `c < −3600` →
  `c = ((c + 100) rem 1200) − 2500`, `c ≥ 3600` → `c = (c rem 1200) + 2400` (truncating remainders). Ratio
  `r = Power(2.0f, float(c − 2400)·0.00083333335f + 1.0f)` (Delphi `Power(Single, Single)`: an integral exponent goes
  through IntPower, otherwise `exp(e·ln b)`).
- Line length `N = round(fs / (double(r)·523.251130601197))` samples (×2 with Widen: a second line for the right).
- Normalize on: `inv = 1.0f / r` (used by §1.3).
- Colour: `lv = clamp(colour + round(Final.FRes·128), 0, 128)`, `c = lv·0.0078125f`, `a = (1 − c)·0.5f`. The line
  is filled with `+a` on its first `N >> 1` samples and `−a` on the rest (through a fill routine; when the plugin has
  been handed a sample at +0x134 it is resampled into the line instead — never in LYNX, **not confirmed** how FL
  hands one). Then white noise of depth c: with Widen, first the right line, `R[i] = float(L[i] + (2·rnd − 1)·c)`, then
  `L[i] = float(L[i] + (2·rnd − 1)·c)`, `rnd` = Delphi `Random` (RandSeed·0x08088405 + 1, ×2^−32), in double.
- Voice stealing when the max polyphony is reached: the sounding voice with the smallest "last peak" (§1.4; 1.0 for a
  new voice) is marked released.

### 1.3 Feedback gain **[DECOMP `FUN_006bb100`; EXEC]**

`lv = clamp(decay + round(Final.FCut·256), 0, 256)`, `d = ln(float(lv) + 1) / ln 257` (double),
`g = float(float(d)·0.0499f + 0.95)`; with Normalize `g = Power(g, inv)`; stored as `g·0.5f`. Recomputed when
Final.FCut changes or the decay knob moved.

### 1.4 Render **[DECOMP `FUN_006bb570` (Gen_Render), `FUN_006bb200`; EXEC]**

Per voice and host block: levels from the host's ComputeLRVol(Final.Pan, Final.Vol) (zero once released), the first
block's levels also become the "previous" ones (no ramp); per sample `y = line[p]; line[p] = g·(y + y_prev)` (float),
output y (the right line likewise with its own previous sample); the block goes to the host through slot 11 (mono) or
12 (stereo, Widen) with the levels and the previous levels (FL ramps between them — law **not confirmed**). Level
watch: the peak of the voice's own signal is accumulated; once more than 256 samples have passed (counted per block),
the peak is kept as "last peak" and, if it is below 0.004, the plugin asks the host to kill the voice. A released
voice (Gate on, §1.5) is killed as soon as its previous levels are both zero.

### 1.5 Note-off **[DECOMP `FUN_006bb090`]**

With Gate on, Voice_Release marks the voice released (its levels become 0); with Gate off a note-off does nothing:
the string rings until the level watch ends it.

### 1.6 Verification **[EXEC + model; `py/plcmp.py`, `py/plcpp.py`]**

The Python model (`py/plmodel.py`) and LYNX's C++ engine against the DLL with the same RandSeed: 14 single-note cases
(keys 0…127 — both pitch folds, integral and fractional Power exponents —, every switch, Mod X/Y, 44.1/48/96 kHz) and
five multi-note batteries (gate releases, voice stealing at max polyphony 2, pans, cents, blocks 256/512/1000, host
kills honoured): **all bit-identical**, kill timings identical. The one exception is the host's pan law: against the
harness's replay of FL's x87 body, LYNX's emulation of it (`flvp::panLaw`) differs by one float ulp for pan 0.5 at
level 0.8 (−146.5 dB; the case with plain levels is bit-identical).

### 1.7 LYNX port **[model]**

`InstrumentKind::plucked`, `src/core/Plucked.{h,cpp}`: the importer maps "Plucked!" with a readable state (≥ 20 bytes)
to it (there was no conversion before). Host side as for the other FL voice generators (round 47 §2.1): Final.Vol =
channel volume × Init.Vol (from LYNX's velocity), Final.Pan = note + channel pan, Final.Pitch from key, channel pitch
and note cents, Mod X/Y = 0; FL's x87 host pan law. Not modelled / by design: FL's random seed (process-global, drawn
from the clock — every FL render differs; LYNX starts each instrument at Delphi's default seed 0), FL's host ramp
law (LYNX ramps linearly over the block when a level changes), a channel sample as exciter. Core test
`plucked_engine` (five DLL fingerprints at 1e-7, the instrument's gate and ring-out).

---

## 2. BeepMap

### 2.1 State and parameters **[DECOMP `FUN_0072ebc0` (SaveRestoreState), `FUN_0072efc0` (ProcessParam), `FUN_0072f370`; DOC; DATA]**

int32 version (> 1 refused). Version 0: 15 int32 settings at +0x120…+0x158 (60 bytes); version 1: the same + the
processor's 364-byte channel snapshot (taken only when complete; version 0 keeps the processor's defaults). Then an
int32 size and a bitmap stream (a BMP file, loaded by Delphi's bitmap class and converted to 32 bits per pixel). The
seven automatable settings **[DOC, `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/BeepMap.htm`]**:
0 Frequency, 1 Length, 2 Scale (menu, three modes), 3 Use blue, 4 Grainy, 5 Loop, 6 Widen; setting 14 is "Max
bitmap height"; 7…13 are zero in every state and unused by the audio path (**inference** from the loader and
ProcessParam, which only handles 0…6). The constructor loads `Images\Default.bmp` from the plugin's data folder
(host data-path query 1020).

### 2.2 Bitmap **[DECOMP `FUN_007300d0`; EXEC]**

A bitmap at least 3 rows high is copied into a 32-bit work bitmap; one taller than Max height is first scaled down
with the image class's stretch routine (**not decoded**; no state of the installation needs it — LYNX picks rows by
nearest neighbour). Row 0 is the image's top row (EXEC: the bottom-up and the top-down presets both render
identically to the model). Level scale `s = float(1 / (√height·255))`.

### 2.3 Settings **[DECOMP `FUN_0072efc0`, Dispatcher `FUN_0072e670`; EXEC]**

- Length: samples per column `spc = max(1, MulDiv(round(float(float(e − 1)·256)) + 64, fs, 44100))` with
  `e = exp(float(P1/4096)·ln 33)`; `inv = float(1/spc)`. Re-applied by Dispatcher 4 (sample rate), which also sets
  the increment base `float(523.2511306·2^32/fs)` and hands the rate to the processor library.
- Frequency: setting 0 invalidates every voice's pitch cache (increments recomputed at the next render).

### 2.4 Note-on **[DECOMP `FUN_0072f5b0` (TriggerVoice), `FUN_00730880`]**

Voice stealing as 3x Osc (first unreleased voice starts its end fade when the max polyphony is reached). One
0x30-byte record per row: phase `Random(2^24) << 8`, then with Widen a right-channel phase offset `Random(2^24) << 8`
(else 0) — Delphi `Random(n) = (seed·n) >> 32`; then Image-Line's voice processor's trigger.

### 2.5 Columns and rows **[DECOMP `FUN_0072fe80`, `FUN_0072fb20`, `FUN_0072f7e0`; EXEC]**

- The voice counts samples; at each column boundary (every `spc` samples, the first at sample 0) the column index
  advances and the column is set up; at the end of the image the index wraps to 0 with Loop, otherwise every target
  becomes 0 (the levels ramp down over one column) and, once the index passes the width, the voice reports it is
  done (the processor's per-voice routine then has the host kill it).
- Column set-up, per row: levels below 2^−24 are flushed to 0; targets `tL = R·s`, `tR = G·s` (red → left, green →
  right **[DOC + DECOMP]**); weight `w = 256`, or `256 − B` with Use blue; then the increments (below); then (Scale
  mode 2 only) rows whose frequency exceeds `k = round(float(exp(float(P0·0.000104166)·ln 11) − 1)·1102.5)` Hz have
  their targets scaled by `max(0, float(1 − (f − k)·0.0020833))`; ramps `dL = (tL − levelL)·inv`, `dR` alike; a row is
  "active" unless its levels and targets are all zero; Grainy's increment `2^32 / spc`.
- Increments from `c = round(Final.Pitch)` (cents from C5), H = height, row r (0 = top):
  - Scale 0: `c_r = c − ((2r − (H−1))·P0·w) / ((H−1)·512)` (integer, truncating), clamped to ±4800;
    increment `round(float(523.25·2^32/fs)·exp(c_r·ln2/1200))`.
  - Scale 1: base `b = min(round(exp(c·ln2/1200)·523.2511306), fs/4)`, `f_r = b − ((2r − (H−1))·⌊1.5·P0⌋·w) /
    ((H−1)·512)`.
  - Scale 2: `b` from `c − 2400`, `f_r = (H − r)·b·(9 − w/32)` (harmonics).
  - Modes 1 and 2 clamp `f_r` to `[40, ⌊(fs/4)/b⌋·b]` Hz and use `⌊f·2^32/fs⌋`.
- Oscillators (the x87 bodies `FUN_007309a0` / `FUN_00730930`, picked on this machine like 3x Osc's mixers): the
  host's 16384-point sine table read by the top 14 bits of the phase, left at `phase`, right at `phase + offset`; the
  levels start from the stored floats and step by their ramps in 64-bit precision within one call (a call covers one
  host block or the part of it inside one column) and are stored back as floats; row 0 writes the buffer, the others
  add to it (inactive rows only advance their phase). Grainy then multiplies the stereo buffer by the sine table at
  the grain phase.

### 2.6 Voice processor **[DECOMP: function matching; EXEC]**

The per-voice routine, block start, mix, trigger, release, end fade, articulator and filter code is
instruction-for-instruction the 3x Osc processor library (`FUN_00721790` ≡ 3x Osc `FUN_0072b350`, `FUN_00721890` ≡
`FUN_0072b450`, `FUN_00721950` ≡ `FUN_0072b510`, `FUN_00722490` (release) ≡ `FUN_0072c050`, `FUN_00722510` ≡
`FUN_0072c0d0`, … 32 functions, `fpair.py`): BeepMap's oscillator bank is the processor's per-voice callback; the
voice ends when the processor's end fade finishes or the image has played.

### 2.7 Verification **[EXEC + model; `py/bmcpp.py`, `py/bat_bm.py`]**

LYNX's C++ engine against the DLL with the same RandSeed and host kills: the 9 preset/database/render-test states
(one note, 3 s) **bit-identical**; a battery of 24 synthetic variants (4 bitmaps × 3 Scale modes × Use blue, random
Grainy/Loop/Widen/Frequency/Length, 44.1/48 kHz, blocks 64…1000, two notes with pans and cents, releases, max
polyphony 1): 7 bit-identical, the other 17 between −95 and −140 dB — **all** of their difference lies within the
processor's 441-sample end fades after a release or a steal (outside them every sample is identical): FL builds the
end-fade table with IPP's cosine, LYNX's shared processor with the exact one (round 42 §3.8).

### 2.8 LYNX port **[model]**

`InstrumentKind::beepmap`, `src/core/BeepMap.{h,cpp}`, reusing the 3x Osc processor port (`core/FlVoiceProcessor.h`).
The importer maps "BeepMap" with a readable state (version 0/1, an uncompressed 24/32-bit BMP of at least 3 rows) to
it with the project's PPQ. Not modelled: FL's image scaler for bitmaps above Max height, other image formats, FL's
process-global random seed (LYNX: per instrument). Core test `beepmap_engine` (a synthetic 9×6 bitmap in every Scale
mode and switch, two installation presets; DLL fingerprints at 1e-7; the instrument ends after the image and fades on
a note-off).

---

## 3. Sawer

(In progress.)
