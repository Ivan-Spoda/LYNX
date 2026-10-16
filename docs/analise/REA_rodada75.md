# Behavioural analysis of FL Studio 2026: round 75 (REA) — Harmor: image synthesis and the remaining voice gaps

> **Nature of this document.** Continuation of `REA_rodada34.md` (Harmor's voice as an additive engine) and
> `REA_rodada38.md` (its effects rack): the items round 34 left open — image synthesis, arpeggio envelope markers, the
> noise resonance, the "pluck replaces blur decay" option, phaser and harmonizer masks, timbre blend modes, the global
> envelope scales and the harmonic-randomness module — plus two small discrepancies found on the way (X/Y/Z smoothing,
> a random draw for one-voice random unison) and the cause of demo 78's residual. *Clean-room* reverse engineering,
> behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No FLEX pack was opened; no Image-Line content (image data, presets, audio) was copied into the
> repository — the images LYNX plays are the ones stored in the user's own projects.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the official
> Image-Line manual (**[DOC]**), from the data (**[DATA]**) or from running the installation's own DLL, unmodified
> (**[EXEC]**). Results of our own numerical model (which implements only DECOMP/EXEC facts) are marked **[model]**.
> What could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (`https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Harmor.htm`) |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from the DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model (`r75/py/hmodel.py` + `imgm.py`, `henv.py`, `hart.py`) |

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r75/`.
- **Target:** `Plugins/Fruity/Generators/Harmor/Harmor_x64.dll`, SHA-256
  `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` (unchanged since round 11; copy in `bin/`).
- **Tools:** headless Ghidra 12.1.4 (project `~/ghproj/r75`, scripts `gh/scripts/Dec*.java`; outputs `out/img79.c` =
  the image object's code `0x790000…0x7a0000`, `out/plug_all.c` = the plugin `0x860000…0x8a0000`, `out/lib78.c` = the
  shared library, `*_ipp.c` copies with the IPP dispatch names substituted by `py/ippsub.py` from `out/hm_disp.c`);
  `objdump -M intel` (`out/voice.asm`, `out/plug.asm`, `out/trig.asm`); constants with `py/k.py`.
- **EXEC harness:** round 38's `run/hm38.c` (round 34's harness plus host data files), unmodified, built as
  `run/hm.exe` (Zig 0.14.1, Windows x64): loads the installation's DLL unmodified, restores states through the
  plugin's own SaveRestoreState, triggers notes through TriggerVoice, reads fields and arrays through pointer paths
  (`peek=`, `pp=`, `arr=`, `gd=`, `fp=`), dumps the Mersenne-Twister state before the first note; vectored fault handler,
  `TerminateProcess`, `timeout`. Drivers `py/hmrun.py`, `py/demo.py`, `py/bat_demo.py` (demo battery), `py/synth_img.py`
  (synthetic battery), `py/dbg*.py`; C++ comparison `hport/check.py`, `hport/cmpcm.py`, `hport/synth_cpp.sh`.
- **Data [DATA]:** the 99 Harmor channels of the installation's demo projects (`r14/blobs14.pkl`, as round 34); 8 of
  them carry image data (demos 3, 13, 14, 29, 74, 75, 77, 87), 3 arpeggio markers (20, 40, 46), 1 the noise
  resonance (79), 1 the pluck blur option (15).

Error measure everywhere: `20·log₁₀(‖model − DLL‖ / ‖DLL‖)` over the whole render, worst channel; 44.1 kHz, host block
512, three overlapping notes (C5 vel 1 0–20000, G5 vel 0.6 8000–26000, G4 vel 0.95 16000–30000 samples) unless said
otherwise, effects rack off, the DLL's Mersenne-Twister state given to the model.

---

## 1. Image synthesis

[DOC] "The IMG (Image Resynthesis) section can load images OR samples"; "There are independent images that control the
Pitch/Frequency and Gain of partials"; "Pixel brightness is interpreted as partial level"; "In short, the difference
between the two modes is quality (resynthesis) vs image editing flexibility (image synthesis)."

### 1.1 The image object in the state **[DECOMP `FUN_00793300` + DATA]**

After a part's 26 shaping curves comes the image object (part `+0x130`), then a string (the image/sample file name;
one empty byte in every demo):

| Field | Size | Meaning |
|---|---|---|
| version | i32 | 0 (≥ 1 not read here) |
| mode | i32 | object `+0x20`: 0 image synthesis, 1 resynthesis (planes not stored: the sample is analysed at load) |
| loop mode | i32 | `+0x58`: 0 one-shot, 1 loop, 2 ping-pong ([DOC] "Looping: One shot, Loop, or Ping pong") |
| gain-table amount | f32 | `+0x100` (0 in every demo; > 0 builds a per-column gain table `+0x50`, §1.9) |
| loop start, loop end, crossfade | 3 × f64 | `+0xe8`, `+0xf0`, `+0xf8` (−1, −1, 0 = whole image, no crossfade) |
| columns, rows | 2 × i32 | `+0xcc`, `+0xc4` (stride `+0xc8` = rows rounded up to 8) |
| 3 planes | per plane: i32 count, then `columns × rows` f32 | plane 0 **gain** (pixel 0…255), plane 1 **frequency** offset (−1…1 in the data), plane 2 (resynthesis phases; count 0 in image mode) |

Demo images [DATA]: 4×235 … 473×255 pixels; all image mode (no resynthesis), loop modes 0 and 2. The part's
`+0x5c` = 0 (no image), 1 (image synthesis), 2 (resynthesis) **[DECOMP `FUN_008783e0`]**.

### 1.2 Parameters **[DECOMP `FUN_00879540` cases 0x52…0x5e, `FUN_00878300`, DFM]**

| Part param | Control (DFM) | Law |
|---|---|---|
| 82 | image time offset smoothing | `v = (|p|·0.001)²`; coefficient `exp(ln 0.01 / (2v·fs/B))` (`+0x154`); threshold `+0x150` = 0 (p = 0), 2³¹ (p < 0), `250v/columns` |
| 83 | image time offset | `p · 2⁻²⁰` (`+0x148`, fraction of the image) |
| 84, 85 | fine / coarse speed | columns per control block `p84·10⁻⁴ · 2^(p85·10⁻⁴) · B/(0.1·fs)` (`+0x140`; 100 ms per column at fine 1.0) |
| 86 | image sharpening | `(p·0.001)²` (`+0x158`): used by the resynthesis branch only |
| 87, 91 | gain / freq interpolation | 0 Block, 1 Cap, 2 Bell, 3 Linear, 4 Cubic, 5 Rise, 6 Fall (§1.4) |
| 88 | gain pixel scale | `x = 2^(p·0.016) − 1` → `FUN_007975f0` (§1.5) |
| 89 | gain mix | `p·0.001` (`+0x178`) |
| 90, 92 | freq mode / pixel scale | `+0x16c`, `+0x174 = s·(object +0x104 = 1)`, `s = p92·0.001` (×ln 2 for Oct; `max(0, 32(s+2))` Wide Hz; `(s+2)/8` Unison) |
| 93, 94 | formant shift / mix | cents (`+0x2374` articulator ×12000 is added) / `p·0.001` (`+0x160`) |

Articulators 27…30 are the image time offset, fine speed, formant shift and freq pixel scale ([DOC] articulator
list; ArtSelect combo [DATA]).

### 1.3 Time position per control block **[DECOMP `FUN_0088ab10` + EXEC]**

Per record (voice `+0x150` accumulator, `+0x158` position, `+0x168` smoothed offset initialised to the offset at the
note start): the offset moves toward the target (jumps when the difference exceeds the threshold, otherwise
`target + diff·coef` in float; on looping images a difference above 0.5 first wraps by ±1 and the result is taken
with Delphi's `Frac`); `d = (offset + A27·2)·columns + accumulator`; then one-shot clamps to `[0, columns]`, loop wraps
into `[start, end)`, ping-pong wraps over twice the loop length and reflects at the end; finally the accumulator
advances by `speed · A28`. A one-shot image whose position reaches the end **ends the record** (state 9, the 10 ms
fade starts in that block) when the time-offset articulator (27, or 28 when filter 2 precedes filter 1: part
`+0xb64`) has no envelope (`FUN_008856b0`).

Loop points (`FUN_00798450`): start/end clamped to the image; loop mode 1 rounds them to columns; ping-pong doubles the
length; the **crossfade** (`+0xf8`, ≤ 0.5) shortens the loop to `len·(1 − x)`, starts fading at `start + len'` with
rate `1/(end − that)`, and reads a second column one loop length back, mixed `out + (second − out)·t`
(`FUN_00797bd0` → `FUN_00793020`). Column indices outside the image clamp, or wrap for loops that cover the whole image
(`FUN_00798400`).

### 1.4 Reading a column **[DECOMP `FUN_00797bd0`, `FUN_007979e0`, `FUN_00797b20`]**

`i = floor(pos)`, `t = pos − i` (float). Block copies column `i`; Cubic is the Catmull-Rom form on columns
`i−1…i+2` evaluated in float in the order `((((p1−p2)·1.5 + (p3−p0)·0.5)·t + p0 − p3·0.5 + p2 + p2 − p1·2.5)·t +
(p2−p0)·0.5)·t + p1`; the others blend columns `i` and `i+1` with `t` shaped as: Linear `t`; Cap `u = 2t` (or
`2 − 2t` past ½), `t' = u⁹/2` (or `1 − u⁹/2`); Bell the same with `u³`; Rise `1 − (1−t)⁶`; Fall `t⁶`.

### 1.5 Pixels to partial levels **[DECOMP `FUN_007981f0`, `FUN_007975f0`, `FUN_007976e0`, `FUN_00798130`, `FUN_00795720` + EXEC]**

- Normalisation at load: amplitude of a pixel `a = (256^(p/255) − 1)/255`; per column `Σ (a_i/(i+1))²` (float, IPP
  accurate sum); `k38 = 1/max(√max_col, 2⁻¹²)`.
- Gain pixel scale `x` (param 88): `k30 = ln(x+1)/255`, `k34 = k38·0.192113518714905 / (x·(e^(170·k30) − 1)/x)`;
  `x = 0` → linear `k30 = k38·0.00113`, `k34 = 0`.
- Per block: `level_i = expm1(max(pix_i·k30, 0))·k34·g` (`g` = per-column gain table, 1 while `+0x100 = 0`); linear
  case `max(pix_i·k30·g, 0)`. **[EXEC: the DLL's level column matched to 2·10⁻⁷ relative on demo 74 before the formant.]**

### 1.6 Formant shift **[DECOMP `FUN_00888720`, `FUN_008886e0`, `FUN_008886b0`, `FUN_00889c50`, `FUN_00795090`, `FUN_00795720` + EXEC]**

Active unless the mix is 0 or (the shift is 0 and no mutation curve): shift `x = p93 + A29·12000 − (pitch + unison
detune in cents, voice +0x1b0) + part pitch knob (part +0x30)` — so formants stay put when the note moves.
`fac = 2^(−x/1200)`; envelope of the column: a backward then a forward peak-follower with coefficient
`1 − exp(ln 0.01/(3·f0))` (`f0` = the column's fundamental position, at least 2), floored at 2⁻¹⁶, entries below
`round(f0)` set to the value there; fine structure = level / envelope (IPP `Div_32f`); positions
`(fac − 1) + fac·i` — or `tab_i·fac − 1` with the **formant mutation** curve (`tab_i = (i+1)·2^(−2v_i)`, `v` the curve
sampled per partial, centre ½) — clamped to `[0, stride−1]`; output = interpolated envelope × fine structure, mixed
with the input by the formant mix.

The **pitch track** `+0x48` (one fundamental position per column) is computed at load from both planes (frequency
plane needed, ≥ 16 rows): weighted spectrum `q = ¼(level/(i+1+f_i)²)² + ¾ q_prev` (running over columns); the best of
2…round(64·(rows+1)/512) candidates by `Σₖ₌₁⁶ √(q[harmonic k]·wₖ)`, `w = 1, ½, ⅓, ¼, ⅕, ⅙`; a ±1 refinement on the
level; a voicing test over up to round(50·(rows+1)/512) harmonics (`3·other ≤ sum`, else 10000 = unvoiced); then
outliers replaced by the 5-column mean, unvoiced runs filled from both neighbours, a 9-tap Hann smoothing with edge
replication. **[EXEC: the DLL's `+0x48` matched to 10⁻⁶ on demos 3, 13, 29.]**

### 1.7 Applying the gain plane **[DECOMP `FUN_0088ab10`, `FUN_00882850`, `FUN_00882c40` + EXEC]**

On the timbre mix `T` (voice `+0x1a0`): `T'[4+i] = level_i·T[4+i]` over `min(n − 4, stride)` partials,
`T'[1] = T[1]·level_0`, subs `T[2]·(level_0+level_1)/2`, `T[3]·(level_1+level_2)/2` (subs around the fundamental) or
`T·level_0` (below), the rest 0; gain mix `T' + (T − T')·(1 − mix)`; with auto gain and combo `+0x47 = 1` the auto-gain
reference becomes the energy of `T'`. The module chain then runs on `T'`; protection still compares with `T`.

### 1.8 Applying the frequency plane **[DECOMP `FUN_0088ab10` 0x88cd2a…0x88d506 + EXEC]**

With a frequency plane the frequencies are rebuilt every block. After the normal ratios, unison and first-block
phases: the column (param 91 interpolation) times the **image freq local mask** (curve 25 per partial, trimmed on 1.0,
`FUN_0088a320`), then by mode — **Oct** `f·2^(pix·s·A30)` (IPP `Exp`), **Hz** `f + pix·f₀/fs·d·s·A30` (`d` = the
harmonic spacing), **Wide Hz** `f + (pix+1)·f₀/fs·d·|s·A30|`, **Unison** squares `pix+1` per partial (subs from it) and
uses it as the per-partial unison detune (replacing the global detune; Hz-unison or the unison-pitch curve folded in).
Resynthesis (`+0x5c = 2`) has its own branch (phases, sharpening, `+0x164` refresh) — not modelled.

### 1.9 Not covered

Resynthesis mode (the sample analysis `FUN_00795720` mode 1 and the voice branch for `+0x5c = 2`) and the per-column
gain table (`+0x100 > 0`, `FUN_00798450` tail) — none in the data; LYNX ignores such images (the part plays its timbres)
and reports it.

---

## 2. Arpeggio envelope markers **[DECOMP `FUN_0079e680`, `FUN_007a0c60`, `FUN_007a09f0`, `FUN_007a24b0`, `FUN_007a2cc0`, Gen_Render 0x891d95 + EXEC]**

[DOC] "Harmor uses special arpeggiator envelope markers active when playing chords"; "At the break-point applies the
following segment to the note below the previous note in the chord modulated" / "…to the same note…" / "…to the note
above…".

- Point byte `+0xD`: 1 below, 2 same, 3 above. An envelope is an arpeggio envelope (`+0x8d`) when a marked point lies
  at or before the sustain point and its loop is not of zero length (`+0x8c`: time(release point) − time(loop) ≤ 0).
- The chord (Gen_Render, after any note-on/off/kill, `+0xe90`): the distinct pitches of the records still playing,
  ascending (descending with global 16); every such record gets its index and the chord size on the envelopes and LFO
  envelopes of articulators 0…30 (`FUN_007a24b0`).
- At each break-point with a marker (stage < 6): position `a = (max(a,0) − 2 + marker + size) mod size`; the record is
  **muted** when `a ≠` its index: its envelope holds the level it had at that marker (`+0x48`) and does not move during
  the segment; the playing one runs the segment.
- A global (shared) envelope copied by a new record inside an arpeggio joins muted at the held level (`FUN_007a2cc0`).
- Release: the playing note of a chord of ≥ 2 keeps going until the next marker (stage 5), then releases (a released
  note leaves the chord at the next rebuild); a muted note releases at once; the release unmutes.

**[EXEC + model: demos 20, 40, 46: −2.2/−2.2/−1.2 dB → −78.3, −82.7, −82.3 dB; descending order −77.7…−82.7 dB.]**

## 3. Noise resonance (type 109) **[DECOMP `FUN_00886eb0` noise branch, TriggerVoice + EXEC]**

[DOC] "Special effect. Harmonics are sounded randomly. Use the RES to control the amount of randomness, width controls
the length/duration of changes in harmonics."

Per filter with `r > 0`: width `w = knob·A` (articulator 10/11); on change, length `L = 9999 / ((801^(w/4) − 1)·1.24750567 +
1.99546485)` and step `B·0.1000100010001/fs·L`; threshold `a = r⁶·L·2` (×2 in Hz mode), top `a + 1`. Uniform draws
(IPP's generator) in `[0, a+1)` from the record's seed (`+0x27c`, the note's second MT draw), the same pattern every
block until the position reaches 1, then the seed is re-mixed (a fixed 32-bit integer hash). Octave mode subtracts
`5.637·10⁻⁵·Hz` (runtime constant `0x94e11c`); "on/off" (adaptive off): below `a` → 0, otherwise `√(a+1)`; adaptive:
`max(x − a, 0)` (÷ `(1 − 5.637·10⁻⁵·Hz)` in octave mode), squared, × `2√(a+1)`; the resonance mask applies as for the
normal resonance. **[EXEC + model: demo 79 −76.6 dB (precision 1: −107 dB); synthetic adaptive/Hz/width −65.9…−80 dB.]**

## 4. Smaller items

- **"Pluck replaces blur decay"** (combo `+0x46`) **[DECOMP `FUN_00886610`, `FUN_00888da0`]**: the time blur runs
  always and decays each partial at the pluck rate (`exp(table·f5)`, `f5 = ln 0.01·B/fs/(20000·2^(17(v−1)) + 0.25)`,
  `v` = pluck knob + articulator 15), recomputed when `v` or the partial count changes. Demo 15: −85.1 dB.
- **Phaser mask** (curve 22, a frequency table like the filter masks) **[DECOMP `FUN_00887f10`]**: `T + (P·M·mix −
  M·mix)·T`; for the frequency phaser (209) the exponent is `M·P·mix³`. Synthetic −67.6…−92.7 dB.
- **Harmonizer mask** (curve 23 per partial, squared where ≠ 1) **[DECOMP `FUN_0088a0c0`, `FUN_00885cb0`]**: result
  `(H·k − orig)·M + orig` instead of `H·k`. Synthetic −79.3…−79.5 dB.
- **Timbre blend modes** (combo `+0x00`) **[DECOMP `FUN_0088f310`]**: timbre 2 combined with the raw timbre 1 (before
  the local EQ): 1 `max(T1 − T2, 0)`, 2 `T1·T2`, 3 `max`, 4 `min`; **5 pluck blend**: timbre 2 × the pluck table
  (the DLL's table `2^(16(v−1))`, part `+0x258` — the reciprocal of round 34's), at least 2⁻¹⁶, no local EQ; the voice
  plays timbre 1, and the timbre-mix value steers the pluck decay `exp(f5 / ((tab − T2)·(1 − mix) + T2))`. Synthetic
  −76.7…−80.6 dB.
- **Global envelope scales** (articulators 34…37) **[DECOMP `FUN_0088ab10`, `FUN_007a2c80`, `FUN_007a2d60`, `FUN_007a2580`]**:
  on the first block every envelope's per-state scales (state `+0x50…+0x5c`) become attack×A34, decay×A35, sustain
  offset +A36, release×A37; the sustain offset used on entering the sustain is that value (0 without sustain point); a
  decay scale ≤ 0 stops loops; the attack skip uses the scaled attack. Synthetic −78.6…−84.9 dB (each scale changes
  the render by +2…+3.5 dB, so the cases are not vacuous).
- **Harmonic randomness** (curve 4) **[DECOMP `FUN_008895a0`, `FUN_00886e30`, TriggerVoice]**: table = the curve
  rendered like a timbre, centre ½, trimmed on 0, ×8 ln 2; per record `exp(u·table)` with `u` uniform draws from the
  note's draw sequence after the MT draw; the module multiplies the partials. Never active in the demos (one disabled
  part has it); synthetic −77.0…−93.4 dB.
- **X/Y/Z smoothing** **[DECOMP `FUN_008826b0` + EXEC]**: the record's X/Y/Z (voice `+0x3334`) follow the knobs with a
  100 ms one-pole per block computed as `((t + c) + (y − t)·c) − c` in float — the `+c −c` rounding leaves the value a
  few ulps from the knob (X 0.32421881 for a knob of 0.32421875 on demo 75), which a time-offset map times 65 columns
  turns into a visible image position error. Off (exact) when global misc `+0x39` is 0.
- **Random unison with one voice** **[DECOMP TriggerVoice]**: the per-voice detune draw happens for unison type 3
  whatever the voice count — it advances the draw sequence used afterwards (harmonic randomness).
- **Pluck rates** are recomputed when the bank grows (voice `+0x1cc = −1`) **[DECOMP]**.

## 5. Demo 78 and 74: IPP's 11-bit functions **[EXEC]**

Round 34 could not explain demo 78's −55 dB. Both 74 and 78 use the per-partial unison-pitch curve at precision
"Average": the DLL applies it with IPP's `Exp_32f_A11` (≈ 20 correct bits here: the frequencies differ by 1.2·10⁻⁶
relative), which the phase accumulates. With the precision patched to 1 the same channels reach −77.5 / −84.5 dB
**[EXEC]**. The IPP routines live in IPP's own DLL loaded by Harmor (pointers `0x93a1b0`, `0x93a1e0`, `0x93a150` lead
outside the plugin): reproducing them would mean copying Intel's code, so LYNX computes these functions exactly and
the residual is documented.

## 6. Verification

**Demo battery [EXEC + model]** (`out/bat_v2.log`, model; C++ `out/cpp_all_v2.log`):

| | round 34 | round 75 model | round 75 LYNX C++ |
|---|---|---|---|
| ≤ −60 dB | 84 / 99 | 96 / 99 (+ 1 silent in both) | 97 / 99 |
| ≤ −70 dB | 75 | — | 88 |
| ≤ −80 dB | 43 | — | 52 |
| median | −79.9 | −80.0 | −80.1 |
| not passing | 15 | 74, 78 (IPP A11, §5) | 74, 78 |

Image channels (model = C++ to −150 dB except 74/87, see below): 3 −77.7, 13 −85.7, 14 −79.8, 29 −77.7, 74 −57.4
(−77.5 at precision 1), 75 −94.5, 77 −79.2, 87 −99.6 dB.

**Synthetic battery** (`py/synth_img.py`, `hport/synth_cpp.sh`; demo states patched): interpolation 0…6 (−95.7…−96.4),
speed, loop, loop region, ping-pong, backwards, crossfade (−96.3, −96.4, −93.7; the crossfade alone changes the render
by −15.6 dB), gain scale 0/800, gain mix, time offset + smoothing (−95.0), formant shift (−95.8), Oct/Wide Hz/Unison
(−90.1, −106.7, −90.8), freq interpolation, freq scale −700 (−92.6), noise variants, arpeggio descending, pluck blur,
masks, blend modes 1…5, global scales, harmonic randomness — all ≤ −65.9 dB, those at precision 1 ≤ −90 dB.

**LYNX vs model:** −150 dB or better on every image, arpeggio, noise and synthetic case; demos 74 and 87 differ by
−77.6 / −97.1 dB **without** their images too (pre-existing: LYNX's per-partial `exp` is exact where numpy's float32
`exp` is not — LYNX is the closer one to the DLL there).

## 7. For LYNX

Implemented in `src/core/Harmor.cpp` (structures `ImageData`, `Image`, `ImagePos`; `Engine::imageGain`,
`imageFreq`, `noiseResonance`, `arpChord`, `pluckRates`; arpeggio and per-state scales in `EnvDef`/`EnvState`),
tests `harmor_image` and `harmor_round75_features` (goldens from the model). Remaining fallbacks: resynthesis images,
the per-column gain table, unknown shapes / clip modes > 4, blend modes > 5.
