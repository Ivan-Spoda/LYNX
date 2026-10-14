# Behavioural analysis of FL Studio 2026: round 32 (REA) — Harmless: the complete voice as an additive engine

> **Nature of this document.** Continuation of `REA_rodada10.md` §1, `REA_rodada21.md` §1, `REA_rodada24.md` §2 and
> `REA_rodada27.md` §1 (Harmless). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Intel IPP)
> code or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched (no
> FLEX pack was opened) and no Image-Line content was copied into the repository.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical model (which
> implements only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target:** `Plugins/Fruity/Generators/Harmless/Harmless_x64.dll`, SHA-256
  `4100bf85ad29dc665051a73ef62017272a25aefbda1cf2f1ace39e6be376440b` (= rounds 10/21/24/27, re-hashed this round), Delphi,
  `TTestPlug`, VMT `0x7791d0`. VMT slots used: 1 Dispatcher `FUN_007793e0`, 3 SaveRestoreState `FUN_0077adb0`,
  5 ProcessEvent `FUN_0077f5e0` (→ `FUN_00743100`), 6 ProcessParam `FUN_0077f630`, **8 Gen_Render `FUN_007886b0`**,
  **9 TriggerVoice `FUN_00782940`**, **10 Voice_Release `FUN_00783910`**, 11 Voice_Kill `FUN_00783b90`,
  12 Voice_ProcessEvent `FUN_00783c40` (returns 0) **[DECOMP]**.
- **Key functions of this round:** voice render `FUN_00784a10`; voice record allocation `FUN_00782810` (nested in
  TriggerVoice); rate set-up `FUN_0077f170`; quality `FUN_0077f480`; legato hand-over `FUN_00787ef0`, state swap
  `FUN_00784560`, level snapshot `FUN_00787ec0`; glide set-up `FUN_00784630`; voice fade-out `FUN_00783ac0`; filter-env
  release `FUN_00783d80`; harmonizer `FUN_00784220`; pluck rates `FUN_00784860`; phaser-table read `FUN_0077c760`/
  `FUN_0077c690`; filter/resonance read `FUN_0077c540`/`FUN_0077c470`/`FUN_0077c400`; resonance gain `FUN_00784180`;
  mask `FUN_00783f90`/`FUN_00783f00`; LHP `FUN_00784120`; colour `FUN_00787990`/`FUN_007873c0`; EQ `FUN_00787b40`;
  harmonic count `FUN_00787c30`; ratio table `FUN_00787ce0`; table builder `FUN_0077ca30` (codes 0…16, 100…108,
  200…209); global tables `FUN_0077b450`. **The additive oscillator bank** (a library object, voice `+0xC8`):
  constructor `FUN_00744e20`, buffers `FUN_00744fc0`, mode `FUN_007451e0`, reset `FUN_007452b0`, band-limit set-up
  `FUN_007453c0`, count `FUN_00745480`, one target `FUN_007454b0`, all targets `FUN_00745560`, render `FUN_00746320`
  (SIMD kernels `FUN_007459a0`/`FUN_00745ad0`/`FUN_00745c00`/`FUN_00745ca0`, lane sum `FUN_00746270`, resync
  `FUN_00745850` + `FUN_00745730`/`FUN_007457c0`/`FUN_00746a30`/`FUN_00746b00`), activation `FUN_00745ec0`/`FUN_00745d40`/
  `FUN_00745f70`, entering partials `FUN_00746080`. Library: smoother `FUN_005f5f60`/`FUN_005f5f30`/`FUN_005f5fe0`/
  `FUN_005f6040` (selector `FUN_005f6120`), gain ramp `FUN_00612db0` + `FUN_00604960`/`FUN_00604a90`, tension curve
  `FUN_005fd4d0`, Mersenne Twister `FUN_0074bb70` (seeding `FUN_0074ba60`, `FUN_0074bcf0`).
- **Tools:** Ghidra 12.1.4 headless, new project `/tmp/r32gh_H` (the round-21 project no longer existed): full
  decompilation of `0x740000…0x790000` in `r32/out/plug_all.c`, bank `r32/out/bank.c`, library `r32/out/lib1.c`,
  `lib2.c`, `curve.c`; round-21/27 dumps reused (`r21/out/h_range.c`, `pp.c`, `fshape.c`, `r27/out/h1…h5.c`, `hipp.c`);
  `objdump` for instruction-level checks; constants with `r18/k.py`; IPP names with `r27/py/ippmap.py`.

**EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r32/run/hl.c` → `hl.exe`. It calls
`SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)` first, installs a vectored fault
handler that ends the process, loads the installation's `Harmless_x64.dll` unmodified from
`C:\Program Files\Image-Line\FL Studio 2026\…`, creates the plugin with its exported `CreatePlugInstance` and a minimal
fake host, restores a `0xD5` state blob through the plugin's own SaveRestoreState (read-only COM `IStream`), sends
Dispatcher ProcessMode (1) / SetSampleRate (4) / SetBlockSize (3), triggers notes with the SDK `TVoiceParams` record
(float levels: `Init.Pitch = 100·key`, `Final.Pitch = 100·key − 6000`, Final.Vol/Pan/FCut/FRes given) through
TriggerVoice, releases through Voice_Release, renders through Gen_Render, and always ends with `TerminateProcess` (DLL
finalisation never runs); everything runs under `timeout`. Host stand-ins: Voice_ProcessEvent (slot 6) answers event 3
(velocity) with the float bits of the given velocity, events 4/5 with configurable floats (default 0.0 / 1.0) and 0
otherwise; ComputeLRVol (slot 31) returns `L = R = vol` (or FL's law A of round 28 §4.1 with the `panlaw` switch); other
host methods return 0. Options: `on=`/`on2=` (notes, `on2` adds Final.FCut/FRes), `off=`, `kill=`, `p=` (ProcessParam at
a sample), `disp=` (Dispatcher), `pe=` (ProcessEvent, e.g. max polyphony), `ev4=`/`ev5=`. Read-only peeks of plugin /
voice / bank fields and arrays (`pp=`, `peek=`, `arr=` with pointer paths, also right after TriggerVoice) were used to
read intermediate values and the per-voice random draws. No process or dialog was left behind.

**Model (pure Python + numpy, `python3 -I`):** `r32/py/hlmodel.py` (+ `hlmodel_tables.py`) — the whole voice and the
Gen_Render voice mix; state builder/driver `r32/py/hlrun.py`; case runner `r32/py/cmp1.py`; batteries `bat1.py`…`bat7.py`
(outputs `bat1.out`…`bat7.out`), `t_rand.py` (`t_rand.out`), `t_poly.py`; debugging helpers `dbg_amps.py`, `dbgseg.py`,
`dbgdemo.py`, `bisect.py`. Data: `r14/blobs14.pkl` (139 Harmless channels of the demo projects).

Manual page: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Harmless.htm`
(read in rounds 21/27; no new quotation is needed here).

---

## 1. Harmless — the voice

### 1.1 Plugin structure and data flow **[DECOMP]**

- **TriggerVoice** creates one *voice record* per unison sub-voice (order `n` = p65) and appends them to the voice
  list (`+0x2B8`); the first record is the handle returned to the host, the others are chained through record `+0x98`
  (each points back to the first through `+0xA0`). Each record owns an oscillator bank object (`+0xC8`).
- **Gen_Render** (per host block): computes the stereo-tremolo gain buffers (§1.17), refreshes pending colour/EQ/ratio
  tables, renders every voice record (in parallel worker threads when the "threaded" state flag is set and at least two
  records exist; the arithmetic is the same), deletes finished records, runs the effects rack on the mix (§1.20),
  advances the plugin-wide phases/counters and adds the mix to the host buffer.
- **Voice render** (`FUN_00784a10`), for each record, in two passes over the host block:
  1. the *control-block* pass: every `B` samples (B = control block, §1.2) the record recomputes its partial
     frequencies (only when its pitch changed) and its target amplitudes (always), and the bank renders the samples up
     to the next control boundary (or the end of the host block);
  2. the *envelope* pass: the volume envelope (§1.13) is generated sample by sample and multiplied onto the bank
     output; the mono result is spread to stereo with the host's per-voice L/R gains (§1.16) and the tremolo (§1.17).

### 1.2 Rates, control block and quality **[DECOMP + EXEC]**

- Everything runs at the host sample rate `fs` (no oversampling).
- **Control block** `B = 4·max(1, round(Q/4 · 44100/fs))` with `Q = 88, 44, 8` for quality 0, 1, 2 (table `0x7ee584`;
  the rate object `+0x20` holds `44100/fs`) **[DECOMP]**; EXEC: 88/80/40 at 44.1/48/96 kHz (quality 0), 44 at
  quality 1, 8 at quality 2. Control blocks are counted from the note start (record `+0x2C`), independent of host blocks.
- **Quality**: set by Dispatcher *ProcessMode* (`FUN_0077f480`): the editor's "quality" combo (state integer `0x1AC`:
  0 or 1) or **2 when the ProcessMode value has bit 1 set** (FL's rendering flag — **inference** from the SDK; EXEC:
  value 2 → quality 2) **[DECOMP + EXEC]**. Quality changes: control block (above); partial count ×1.5 (q ≥ 1) or all
  528 (q = 2); bank kernel (q = 2 uses a per-sample double-precision path, §1.3); activation threshold; lookup mode of
  the filter/resonance/phaser tables (nearest entry for q ≤ 1, linear interpolation for q = 2, §1.6); oscillator resync
  at every render segment for q ≥ 1; accuracy of the IPP calls — q0: `Ln_32f_A11`, `Sin_32f_A21`, `Exp_32f_A11`
  (Freq phaser); q1: `Ln_32f_A24`, `Sin_32f_A24`, `Exp_32f_A11`; q2: `Ln_32f_A24`, `Exp_32f_A24`, `Sin_64f_A26`.

### 1.3 The partial bank **[DECOMP + EXEC]**

**Positions.** 528 positions: 0 = self-oscillation partial, 1 = Sub 1, 2 = Sub 3, 3 = Sub 4, 4…527 = harmonics 1…524.

**Frequencies** (recomputed only in control blocks where the pitch value changed): with `f₀` the voice fundamental
(§1.5) and `D = 2^(p39/1200)`, the position ratios are 0.5 (position 1), `1 + 0.5D` (2), `1 + 1.5D` (3) and
`1 + (h − 1)·D` for harmonic h; the frequency in cycles/sample is `ratio·f₀/fs` (double). Grittiness (p40) adds
`f₀·g·sin((ratio − 1)·2π/15)/fs` to every position, `g = −0.0125·sign(x)·(65^|x| − 1)` **[DECOMP `FUN_00787ce0`, case
0x28; EXEC battery `grit±` −82…−84 dB]**. Position 0 is set every block to `f_c·2^(p61/1200)/fs` (f_c = filter cutoff in
Hz, §1.6).
The log₂-Hz and Hz arrays used by the filter, resonance and phaser are computed from these frequencies in float
(`Hz = float(f)·fs`, `log₂ = ln(Hz)/ln 2` with IPP `Ln_32f_A11` at q = 0, `Ln_32f_A24` at q ≥ 1).

**How many partials.** `n = max(1, round(17000/(f₀·D))) + 4` (q = 0), the same ×1.5 before rounding for q = 1, and 528
for q = 2; the bank count is `n` rounded up to a multiple of 8, capped at 528, and **only grows** during the life of a
record (a later pitch drop adds partials; a rise never removes them) **[DECOMP `FUN_00787c30`; EXEC count peek]**.

**Band limit** (q ≤ 1): every partial target is multiplied by
`b(f) = 0` below 16 Hz, `(1 − (32 − f)/16)²` for 16…32 Hz, 1 for 32 Hz…16 kHz, `(1 − (f − 16000)/1000)²` for
16…17 kHz and 0 above 17 kHz (`f` in Hz; the bank holds `16/fs, 32/fs, 16000/fs, 17000/fs`, the 17 000 being plugin
field `+0x2D0`) **[DECOMP `FUN_007453c0` + voice; EXEC]**. The gains are recomputed for all positions when the pitch
changed, and for position 0 when its frequency changed. **At quality 2 the targets are stored before this
multiplication, so no band limit is applied** (all 528 partials, including any above Nyquist) **[DECOMP
`FUN_00745560` (copy precedes the multiply) + EXEC: quality-2 cases −111 / −133 dB only with this rule]**.

**Oscillators.** Each active partial is a sine generated by the two-term recurrence `y ← 2cos(2πf)·y₁ − y₂`, 8 partials
per SIMD block, summed in float **[DECOMP `FUN_007459a0`/`FUN_00745c00`]**. The recurrence is only a fast way of
producing `sin(2π·φ)`: every partial keeps a double-precision **phase accumulator** (cycles; advanced by `f·m` after every
render segment of `m` samples) and its recurrence state is re-initialised from that accumulator (`sin(2π(φ − f))`,
`sin(2π(φ − 2f))`, IPP `Sin_32f_A21`, A24 at q ≥ 1) whenever its frequency changed and otherwise every
`round(8192·f)` samples **[DECOMP `FUN_00745850`/`FUN_00745730`/`FUN_007457c0`]**. Output sample k of a segment is
therefore `Σ a_i[k]·sin(2π(φ_i + k·f_i))` **[model reproduces the DLL to −80 dB and below]**. At q = 2 the bank evaluates
`sin` of every accumulator directly each sample in double precision (`Sin_64f_A26`) **[DECOMP `FUN_00746320` else-branch]**.

**Amplitude glide.** Each partial's amplitude chases its target with a per-sample one-pole:
`a ← T + (a − T)·k`, `k = exp(ln 0.01/(0.01·fs))` (99 % in 10 ms; bank reset `FUN_007452b0(… +0x5F8 …)`,
`+0x5F8` from `FUN_0077f170`); the output uses `a` before the update; a new record starts from `a = 0` (no jump to the
first targets) **[DECOMP + EXEC: first samples match]**.

**Activation.** Partials whose current and target amplitudes are both below a threshold are swapped out of the active
set (checked every 512 samples); a partial is activated again when its target reaches the threshold; phases keep running
for all positions. Threshold `10^(−96/20) ≈ 1.6·10⁻⁵` at q = 0, `5.96·10⁻⁸` otherwise (`FUN_0077f480`) **[DECOMP]**.
(Effect: below −96 dB; not audible.)

**Initial phases** (first control block of a record) **[DECOMP + EXEC]**:
`φ_i = P_i + ε_i + (f_i/f_h1)·(u + r)` for the partials in the bank, `φ_0…3 = 0` (positions 0–3 are zeroed
afterwards), and `φ_i = P_i + ε_i` for positions beyond the bank count (ε_i = the spectral-random part, drawn for all 528
positions, §1.19; 0 otherwise); `P_i` = colour phase (§1.4), `f_h1` = harmonic-1 frequency,
`u` = unison phase offset (0.5 for a single voice; §1.14) and `r` = phase randomness (§1.19). In words: the record
starts as the colour waveform delayed by `u + r` periods of the fundamental. **For a single voice `u = 0.5`, so a Saw
starts half a period in** (its first samples are negative) **[EXEC]**.

**Partials entering later** (bank count grows after a pitch drop): each new partial j gets
`φ_j = P_j + f_j·(φ_last − Φ)/f_last`, where `last` = the previous top partial and `Φ` = that partial's accumulator
value saved at the previous growth (or at note start) — i.e. its colour phase advanced by an estimate of the elapsed time
**[DECOMP `FUN_00746080`; EXEC porta cases −79/−82 dB only with this rule]**.

### 1.4 Amplitudes per control block — order of the stages **[DECOMP `FUN_00784a10`; EXEC per-stage array peeks + batteries]**

Per block the record builds the target array `T` in this order (positions 0…n−1):

1. **Colour × EQ** (table `A`, plugin `+0x570`, rebuilt only on parameter changes): `A_i = C_i·Σ_k g_k W_k(i)`
   (round 21 §1.1 colours, round 24 §2.1–2.2 windows and band gains). Colour amplitudes/phases are evaluated with the
   0-based index `x` (harmonic − 1; 0.5 and 1.5 for Sub 3/Sub 4) and the parity tests use `trunc(x)` (so Sub 3 is
   treated as an odd harmonic and Sub 4 as an even one) **[DECOMP `FUN_007873c0`]**; for *Custom* (colour 11) the
   amplitude and phase are entries `round(x)` and `round(x) + 524` of the 1048 floats of the state (round-half-even,
   so Sub 3 uses entry 0 and Sub 4 entry 2) **[DECOMP]**. Position 0 has `A = 1`, position 1 copies harmonic 1.
   The high-shelf window of band 5 is stored together with the other five in global tables; the six windows sum to 1.
2. **Harmonic mask** (positions ≥ 4): `T = A·(g_((h−1) mod 12)·m + 1 − m)` with `m` = mask mix plus the LFO term
   (round 21 §1.2). **When "pluck uses mask" (p34) is on, this stage is skipped** and the mask acts on the pluck rates
   instead (§1.11) **[DECOMP: `+0x1B0` test; EXEC `pluck_mask` −81 dB only with this rule]**.
3. Harmonizer if its position (p85) = 0 "pre-filter" (§1.10).
4. **Filter gain** `F_i` (§1.6): `T ·= F`.
5. **Resonance gain** `G_i` (§1.8): `T ·= G`.
6. Harmonizer if p85 = 1; **phaser** (§1.9); harmonizer if p85 = 2.
7. **Pluck** (§1.11); harmonizer if p85 = 3 "final".
8. **Low-harmonics protection** for positions 0…7: `T ← T + (A − T)·p_k·w` (round 24 §2.4 weights) — it pulls the
   *whole* result (mask, filter, resonance, phaser, pluck) back toward the colour×EQ value, not only the mask **[DECOMP
   `FUN_00784120` is called with the final array and `A`]**.
9. **Special gains** positions 0…3: `T_0…3 ·= {p62/256, p2/1024, p3/1024, p4/1024}`, then position 0 is overwritten
   with the self-oscillation amplitude `(p62/256)·a_res·L_1` (a_res = resonance amount §1.8, `L_1` = pluck level of
   position 1, §1.11) **[DECOMP: the gain table is contiguous with `A` (`A + 0x840`); EXEC: `T_0 = 4·0.0579 = 0.2317`
   for p62 = 1024, p59 = 500]**.
10. **Band limit** (§1.3) and hand-over to the bank as targets.

(A cosine fade over the top 64 positions, `(cos((j − 1)π/65) + 1)/2` from position 464, is applied to an internal copy
that is immediately overwritten; it has no lasting effect **[DECOMP]**; inference: only notes below ~37 Hz would reach
those positions anyway.)

### 1.5 Pitch **[DECOMP + EXEC]**

Per control block, in float arithmetic evaluated left to right (the order matters at the 1-ulp level and was needed to
reach −80 dB with 6–9 unison voices):
`c = Final.Pitch + (p38 − 1200) + o_pitch` (cents; `o_pitch` = glide offset, §1.15), then `+ vib`, then
`+ A_LFO·L`, then `+ d_uni` (§1.12, §1.14). Then
`log₂ f₀ = log₂ 440 + ((c·0.01 + 60) − 57)/12` where `c·0.01 + 60` is a **float** addition and the rest double,
stored as float; `f₀ = float(2^(log₂ f₀))` **[DECOMP + `objdump` `0x7855aa…0x785605`]**. With FL's convention
`Final.Pitch = 100·(key − 60)` (+ channel pitch, round 28 §4.1), key 60 with p38 = 0 sounds 261.63 Hz.

### 1.6 Filter: position, key tracking, smoothing and table read **[DECOMP + EXEC]**

- Range `lo, hi` and cutoff knob position `K = lo + (p54/8192)·(hi − lo)` as in round 21 §1.6 (margins from the shape
  record's `eL/eR` and `M = 682.67/W`, `W = 1024·2^(1 − 0.004·p56)` entries per octave).
- **Knob smoothing:** the record keeps `K_s` (init = K at its first block) and per block
  `K_s ← K + (K_s − K)·c_K`, `c_K = exp(ln 0.01/(0.1·fs/B))` (99 % in 0.1 s) **[DECOMP `+0x18C`, `+0x6D4`]**.
- **Position:** `kt = (log₂f₀ − 7)·x57`; `pos = clamp(K_s + kt + (Final.FCut + o_fcut)·(hi − lo)/2, lo + kt, hi)`;
  then the LFO (`A_c = 6·x58` octaves): `pos += U·min(A_c, hi − pos)` (A_c > 0) or `pos += U·max(A_c, lo + kt − pos)`;
  then the envelope E (§1.7) with amount `a52 = x52·(hi − lo)` (non-adaptive) or `x52` (adaptive), ×velocity if p53:
  non-adaptive `pos += (E − 0.5)·a52`; adaptive `pos += (hi − pos)·E·a52` (a52 > 0) or `pos −= (lo + kt − pos)·E·|a52|`
  **[DECOMP; EXEC batteries fenv*, lfo_cut*, keytrack*; host FCut verified with `on2=` (−83 dB)]**.
  `f_c = 2^pos` Hz.
- **Table read** (octave scale): index `x = log₂f_i·W + (o − pos·W)` in float; **quality ≤ 1: the entry nearest to x**
  (the SIMD conversion is `cvtps2dq`, round-to-nearest, after clamping x to [0, N − 1]) — **round 27 §1.3 said
  "truncated"; that was wrong** **[DECOMP `objdump 0x77c4d5`; EXEC: 17 filter types from −81 to −91 dB only with nearest]**;
  quality 2: linear interpolation between `⌊x⌋` and `⌊x⌋ + 1` (below 0 → T[0], at/above N − 1 → T[N − 1])
  **[DECOMP `FUN_0077c400`; EXEC quality-2 −111 dB]**. The lookup mode equals the quality, except that on CPUs without the
  feature bit `0x100` of the runtime CPU flags the mode is forced to 2 (`FUN_0077c450`) **[DECOMP]**; on the test machine
  the mode was 0 **[EXEC]**. Hz scale (p55): `x = Hz_i·(0.9W/f_c) + (o − 0.9W)`.
- Shapes: the 17 tables of round 27 §1.3 (now regenerated by our closed forms and used by the model).

### 1.7 Filter envelope **[DECOMP + EXEC]**

- Per-sample increments: attack `ia = 1/(fs·(1001^x48 − 1)·0.12 + 1)` (0 if p48 ≥ 1000), decay
  `id = −1/(fs·(1001^x49 − 1)·0.12 + 1)` (0 if p49 ≥ 1000) **[DECOMP cases 0x30/0x31]**.
- State `t` (record `+0x194`), step `s = ia·B` at note start. **At the start of every control block** `t += s`; when
  `t ≥ 1` while rising, the overshoot is carried into the decay: `t ← 1 − ((1 − t)/s)·id·B`, `s ← id·B`; `t` is clamped
  at 0 from below. So the value used in block k already includes block k's step.
- Value `E = curve(t)`: linear if the tension is 0; with `q = sign(v)·(1000001^|v/1000| − 1)` (float),
  `a = 1/|q|`, `b = ln(|q| + 1)`: `E = 1 − (e^{(1−t)b} − 1)·a` for q > 0 and `E = (e^{tb} − 1)·a` for q < 0
  (attack curve from p50 during the rise, decay curve from p51 afterwards) **[DECOMP `FUN_005fd4d0` + `objdump` of
  cases 0x32/0x33]**.
- **Release:** if the envelope is still rising, it jumps to the decay branch keeping the same E (the decay curve is
  inverted: `t = 1 − ln((1 − E)|q| + 1)/b` or `ln(E|q| + 1)/b`); otherwise nothing changes — the decay simply continues
  **[DECOMP `FUN_00783d80`; EXEC `fenv_rel` −82 dB]**.

### 1.8 Resonance **[DECOMP + EXEC]**

Round 27 §1.4 is confirmed by execution, with these completions:
- Amount `x = clamp(p59/1000 + (Final.FRes + o_fres)·0.5, 0, 1)`, `a = 0.25·(11^x − 1)/10` — the "per-voice
  modulation term" left open in round 27 is the host per-note **FRes** level (+ glide offset) **[DECOMP + EXEC `on2=`
  (−83/−84 dB)]**.
- Track motion (p64), octave/Hz scale, adaptive width, offset, and the gain law `G = max(0, 1 + R·max(0, (log₂f − log₂f₀
  + c)·a))` verified (8 shapes 100…107, Hz/adaptive variants: −82…−86 dB). The table read follows §1.6 (nearest /
  interpolated).
- **Noise (resonance type 8, code 108)** — decoded this round: instead of the peak, every partial is multiplied by an
  independent random gate: a uniform value in `[0, 1 + q]` (IPP `RandUniform_Direct_32f`) is set to 0 when below `q`
  and to `√(1 + q)` otherwise, `q = (4a)³·9999/ℓ` (a = resonance amount, ℓ = grain length in ms); so each partial is
  kept with probability `1/(1 + q)` at a gain that preserves the mean energy. The gates are held for `ℓ` ms: a per-record
  accumulator grows by `B/(ℓ·0.001·fs)` per control block and only when it passes 1 is the generator state advanced (the
  same draws are repeated otherwise). `ℓ = 2.49·(201^{(p61 + 2400)/4800} − 1) + 1.995` ms (p61 = "noise length", from
  2 ms to 500 ms) **[DECOMP `FUN_00784a10` noise branch, `FUN_0077f630` case 0x3D; EXEC with the record's seed peeked:
  −80…−89 dB (`bat7.out`)]**. The generator state starts at the record's random seed (§1.19).

### 1.9 Phaser (p71…p78, phaser type = state integer `0x19C`) **[DECOMP + EXEC]**

- **Tables** (codes 200…209, built once; each RMS-normalised to 1, with the first entry repeated at the end) — our own
  restatement, matching the executed builder to ≤ 5·10⁻⁷ (`r32/run/phshape.txt`, `hlmodel_tables.phaser_table`); with
  `θ = 2πi/N` and `L(d) = 10^(−72d/20)·(1 − d¹⁶)`:

  | Type | Code | N | Table before normalisation | f18 / f1c |
  |---|---|---|---|---|
  | Classic | 200 | 1024 | `1 − cos θ` | 1024 / 1024 |
  | Triangle | 201 | 1024 | `v²`, `v = abs(1 − 2((i + N/2) mod N)/N)` | 1024 / 1024 |
  | Eggs | 202 | 1024 | `sin(πi/N)` | 1024 / 1024 |
  | Deep | 203 | 1024 | `((1 − cos θ)/2)⁶` | 1024 / 1024 |
  | Deeper | 204 | 1024 | `L(a)`, `a = 1 − abs(1 − 2((i + N/2) mod N)/N)` | 1024 / 1024 |
  | Condom | 205 | 1024 | `L(1 − s²)`, `s = ((1 − cos θ)/2)²` | 1024 / 1024 |
  | Twins | 206 | 2048 | `L((1 − cos θ)/2) + 4·L(1 − ((1 − cos θ)/2)²)` | 2048 / 1024 |
  | Cascade | 207 | 2048 | entry `(i + N/8) mod N` = `(sin(2π(4i/(N−1) + 0.75)) + 1)·(1 − i/(N−1))` | 2048 / 512 |
  | Box | 208 | 2048 | zero except entries `N/4…3N/4 − 1`: `(1 + sign(s)·(1 − (1000001^(1 − abs(s)) − 1)/10⁶))³`, `s = sin(4π((i + 3N/8) mod N)/N)` for i < N/2 | 2048 / 2048 |
  | Freq | 209 | 2048 | `−cos θ` | 2048 / 2048 |

- **Read position** per block: `base = ((−p75/1000 − L·w_o − λ)·f18) − φ_ph·f1c`, `key = log₂f₀·p78/1000`, with
  `w = p73/10000`, `w_L = p74/2000`, `w_o = p76/2000`, `L` = bipolar LFO, `λ` = the unison LFO offset (§1.14), `φ_ph` =
  phaser phase:
  - p72 = 0 (octave): `x = log₂Hz_i·k + base − key·k`, `k = 2^{(w − 1)·8 + 2L·w_L}·12·f1c`;
  - p72 = 1 (Hz): `x = Hz_i·k + base − key·f18`, `k = 2^{(w − 1)·8 + 2L·w_L}·0.005·f1c`;
  - p72 = 2 ("harmonic", PWM): `x = Hz_i·k + base − key·f18`, `k = (2 − (w + L·w_L))·f1c/f₀`;
  then `P_i = T[round(x) & (N − 1)]` (q ≤ 1; the table is periodic) or the interpolated read with the same wrap (q = 2)
  **[DECOMP `FUN_0077c690`/`FUN_0077c760`]**.
- **Application:** types 0…8: `T_i ·= P_i·mix + 1 − mix`, `mix = p71/1000`. Type 9 *Freq* modulates **frequencies**
  instead: `f_i = Hz_i·exp(P_i·mix³·ln2/4)/fs` (±mix³/4 octave), the band limit is recomputed and the block is treated
  as a pitch change **[DECOMP]**.
- **Phase:** one plugin-wide phase (`+0x750`), advanced by `v·n` per host block with `v = sign(p77)·2^{|p77|/1000}·BPM/(3840·fs)`
  and wrapped modulo 4; inside a record the phase used by a block is the start-of-host-block phase advanced by the
  samples already rendered **[DECOMP]**. On a song-position event the phase is set to `4·frac(n_pos·v/4)` (n_pos = song
  position in samples) **[DECOMP Dispatcher event 0xD]**.
- EXEC: types 0–8 −75…−84 dB, octave/Hz/harmonic scales, key and LFO −72…−84 dB, quality 2 −133 dB. **Freq (type 9):
  −82 / −97 dB at quality 2, but only −23 / −53 dB at quality 0** because the exponential is IPP `Exp_32f_A11`
  (≈ 11-bit accuracy) whose exact errors accumulate in the phases; the law is confirmed, the sample-exact q ≤ 1 output
  depends on that IPP routine **[EXEC]**.

### 1.10 Harmonizer (p79…p85) **[DECOMP + EXEC]**

- Width `w = p80/1000` (× velocity if p82), plus LFO: `w += U·min(p81/1000, 1 − w)` (or `max(…, −w)` for negative).
  Strength `S = 1.331·2^{p83/256}`, mix² `M = (p79/1000)²`, type record `(s₀, add, mul, D)` = Oct up `(−1, 0, 2, 5)`,
  Oct up +1 `(0, 0, 2, 5)`, Oct up +2 `(1, 0, 2, 5)`, Harm up `(−1, 1, 1, 10)`, Harm up +1 `(0, 2, 1, 10)` (table
  `0x7ee634`) **[DECOMP + DATA]**.
- With `σ = 1/(w·D + 1)`, `c = 1 − σ`, step `q = 2`, total `Σ = 1`, and a snapshot `h` of the harmonic amplitudes
  (positions ≥ 4): while `c > 0`: `g = min(1, c³·S)·M`; `Σ += g`; every harmonic index `s₀ + q + j·q` (0-based, while
  inside the bank) receives `+ g·h_j`; then `q ← q·mul + add`, `c ← c − σ`. Finally all harmonics are divided by `Σ`.
  So "Oct up" adds copies of the spectrum shifted to 2×, 4×, 8×… the original harmonic numbers, "Harm up" to 2×, 3×,
  4×…, each copy weaker (c falls by σ per copy) **[DECOMP `FUN_00784220` + `objdump 0x784330…0x784511`]**.
- Position p85 as in §1.4. EXEC: 5 types × 4 positions −82…−83 dB, LFO −86, velocity −85, strength −87 dB.

### 1.11 Pluck (p31…p35) **[DECOMP + EXEC]**

- `T_p = 0.11975·(1001^x31 − 1)` s (p31 ≥ 1000 = off); with p32 the time is multiplied by velocity².
- Per-partial decay per control block: `δ_i = exp(ρ_i)` with `ρ_i = ratio_i·λ` (normal) or `λ/max(|A_i|, 0.001)`
  (p33 "alternative") and `λ = ln(0.01)·B/fs/(T_p + 0.25)`; i.e. harmonic h falls to 1 % in `(T_p + 0.25)/h` s
  (alternative: in `(T_p + 0.25)·|A_h|` s). With p34 the rates of harmonics are multiplied by the mask factor
  `g·m + 1 − m` (and the ordinary mask stage is skipped, §1.4) **[DECOMP `FUN_00784860`; IPP `Abs`, `Threshold_LT`,
  `DivCRev`, `Exp`]**.
- Levels `L_i` (start 0 except `L_1 = 1`): a counter starts at `min(1 − round(0.002·fs/B), −round(A/B))` (A = volume
  attack in samples), so the pluck starts after the attack; per block, when the counter is ≥ 0, partials not yet
  initialised get `L = 1` (counter < 1) or `δ^counter`, then `T ·= L` and `L ·= δ`; the counter increases every block.
  p35 ("only on release"): pluck inactive until note-off, then the counter is raised to ≥ 0 **[DECOMP voice + `FUN_00783910`]**.
- EXEC: −80…−84 dB (normal, attack, velocity, alternative, mask, on-release, with LHP, with self-oscillation).

### 1.12 LFO and vibrato **[DECOMP + EXEC]**

- The shapes of round 21 §1.7 are confirmed by execution (every shape on pitch −77…−84 dB), with three precisions:
  **Smooth saw** (1) is `Σ_{k=1…12} w_k·sin(2πk·d)`, `w_k = −11.1489·sin²(kπ/12)/k³` (global table `0x811A50` built in
  `FUN_0077b450`) **[DECOMP + EXEC]**; **Stairs** (5) uses the C remainder of `round(3d − 0.5)` by 3 — for negative phases
  (e.g. with the unison LFO offset) it yields values −3…−1 **[DECOMP + EXEC: a demo channel went from −10 to −70 dB with this]**;
  **Voice random** (11): `U = ρ·r²`, `L = 2U − 1`, `ρ` = a per-record draw of the plugin's random generator (§1.19)
  **[DECOMP + EXEC `t_rand` −80/−82 dB]**.
- Phase `d = φ_LFO + λ` (λ = unison LFO offset); φ advances `v·m` per segment, `v = sign(p89)·2^{|p89|/1000}·BPM/(3840·fs)`;
  retriggered (p87 = 0): φ = 0 at note start; global (p87 = 1): each record copies the plugin-wide phase at the start of
  every host block; the plugin phase is set to `frac(n_pos·v)` on a song-position event **[DECOMP]**.
- Attack ramp `r` starts at one per-sample step and grows by `m/(fs·(101^x88 − 1)·0.05 + 1)` per segment, capped at 1;
  the LFO is multiplied by `r²`; sources 9 (filter env), 10 (cutoff knob position), 12 (velocity) and 13 (pitch,
  `clamp((c + 0.6·p89 + 1200)/3600, ±1)`) as decoded.
- Destinations: pitch `±12·(101^|x42| − 1)` cents × L; mask mix and cutoff with U (§1.4, §1.6); phaser width/offset and
  harmonizer width (§1.9, §1.10).
- **Vibrato** (round 27 §1.2) confirmed: one plugin-wide 6 Hz phase (float, wrapped every host block), `0.075·p41`
  cents, fade-in `r_v²` with `r_v` from `3/fs` growing `3/fs` per sample **[EXEC `vibrato`, `vibrato_late` −84 dB]**.

### 1.13 Volume envelope and the end of a voice **[DECOMP + EXEC]**

Exact algorithm (record fields `+0x11C` state, `+0x120` counter, `+0x124` segment end, `+0x128` slope, `+0x130` last
raw value, smoothers `+0x144` "output" and `+0x168` "level"):
- Times in samples: `A = fs·(101^x26 − 1)·0.05` (× unison variation factor, × `(1 − min(1, 1.125·vel))` with p29),
  `D = fs·((1001^x27 − 1)·0.11995 + 0.05)` (p27 ≥ 1000 = hold), `R = fs·((101^x28 − 1)·0.0249 + 0.01)`, multiplied
  with p30 by the float the host returns for Voice_ProcessEvent 5 at release time (the harness answered 1.0; FL's value
  **not observed** — the SDK name of event 5 suggests a release-time factor, **inference**).
- **Note start:** output smoother set to 1 % in `N₀ = min(A, round(0.03·fs))` samples and started at 1 if A = 0, else at
  0; if `A < 0.2·fs` the ramp starts at counter `round(0.2fs·(1 − A/0.2fs)²) + 1` with slope `1/(0.2fs + 1)`, otherwise
  at 1 with slope `1/(A + 1)`; the attack segment ends at `round(A' + N₀) + 1` (A' = max(A, 0.2fs)).
- **Attack:** raw value `min(1, counter·slope)` per sample.
- **After the attack:** output smoother → 1 % in `0.08·fs`; level smoother → 1 % in D samples (or held at
  coefficient 1), started from the last raw value; raw = level smoother driven by 0 (exponential fall, no sustain).
- **Release** (applied at the start of the next render call after Voice_Release): output smoother → 1 % in `0.03·fs`,
  level smoother → 1 % in R, started from the last raw value.
- Output = output smoother applied to raw. Smoother: `s ← x + (s − x)·c`, `c = exp(ln 0.01/N)` (N ≥ 1), state flushed
  to 0 below `5.96·10⁻⁸`; the DLL picks a double-precision variant (`FUN_005f6040`) or a float one (`FUN_005f5fe0`)
  by a CPU feature byte — on the test machine the double one (float variant −69.5 dB on a 0.75 s decay, double −83.9 dB)
  **[DECOMP + EXEC]**.
- **End of a voice:** after a render, if the record is past its attack, does not belong to the most recently triggered note
  of its channel (the channel slot, kept in every mode) and
  the **last output envelope sample is below 10⁻⁴**, it is faded out: output smoother → 1 % in `0.03·fs`, level → 0
  immediately, and the record is removed 0.03·fs samples later (`FUN_00783ac0`; the same fade is used when a voice is
  stolen) **[DECOMP + EXEC `end_decay`, `end_release` −83/−84 dB]**. Round 21's "fixed 80 ms declick when a voice is
  stolen" is corrected: 80 ms is the post-attack output smoother; the steal/end fade is 30 ms.

### 1.14 Unison (p65…p70, p99) **[DECOMP + EXEC]**

Round 21 §1.4 confirmed by execution (orders 2…9, the three types, −82…−87 dB), with exact details:
- Sub-voice k = 0…n−1: `u_k = 2k/(n − 1) − 1` (float); `s = |u_k|`, negated for odd k, negated again when n is odd and
  2k > n; detune `d_k = s·τ_k·S` cents (`τ` = the type's float table at `0x7ee5c8`, `S = 1.875·(41^{p67/256} − 1)`,
  × velocity when p99 = 1); pan offset `u_k·p66/128`; phase offset `u = (k/n)·p68/128` (Blurred: `u = 2^{d_k/1200}·10⁵·(p68/128)⁸`),
  then `u ← u + 0.5 − round(u)`; attack factor `2^{(|s| − 1)·3·p69/128}`; LFO offset `λ = s·p69/256`; gain per
  sub-voice `1 − (3^{1−1/n} − 1)/2` (1 for n = 1).
- Each sub-voice is a full independent record (own bank, envelope, filter state, LFO phase).

### 1.15 Portamento, mono and legato (p43…p46) **[DECOMP + EXEC]**

Each record carries six level offsets `o = (pan, vol, pitch, fcut, fres, velocity)` (record `+0x50`) that are added to
the host's FinalLevels everywhere they are used, and a glide: per control block (at its very start) `o ·= γ` (p45 > 0:
`γ = exp(ln 0.01/N)`, exponential, N = porta time in blocks × velocity factor) or `o += slope` for `1 − round(N)`
blocks (p45 < 0: linear) **[DECOMP voice start + `FUN_00784630`]**. `N = sign(x45)·fs·(41^|x45| − 1)/(26.667·B)`;
velocity factor `(1 − 0.85·vel)²` with p46 (release velocity, host event 4, on legato-back) **[DECOMP]**.
The plugin keeps, per MIDI channel (the voice colour & 15; all FL notes use channel 0), the current note and the levels of
the last triggered note.
- **Portamento (p43, or colour 14):** a new note starts with pitch/fcut/fres offsets = (last note − new note) levels and
  glides to 0; pan/vol/velocity offsets are zeroed; the pitch offset is reduced by 1200 cents while its magnitude exceeds
  1250 (glides span at most ~1 octave) **[DECOMP: round 21's open condition resolved; EXEC `porta_wrap` −85 dB]**.
- **Mono (p44, or colour 13) with a note sounding:** no new sound: the sounding record(s) are handed over to the new
  note (state swap) with all six offsets = old − new levels gliding to 0, and the old note becomes a silent "held"
  record. Releasing the current note while held notes exist hands the sound back to the most recent held one (same glide);
  releasing a held note just discards it **[DECOMP `FUN_00787ef0`, `FUN_00784560`; EXEC `mono_legato`, `mono_porta`,
  `mono_uni3` −81/−82 dB]**.
- Per-record L/R gain changes are ramped across the host block with at most `0.001·44100/fs` per sample (host rate
  object `+0x34`) **[DECOMP `FUN_00612db0`; EXEC]**.

### 1.16 Velocity, host levels and the output level **[DECOMP + EXEC]**

- Velocity = the float the host returns for Voice_ProcessEvent 3 (FL: note velocity/128 — round 28 §4.1, **inference**), clipped to ≤ 1. Used by: p29, p32, p46,
  p53, p82, p99, LFO source 12 (always "velocity + velocity offset").
- **VEL → volume (p37):** when off, TriggerVoice sends the host "link velocity = 0" event (6), so FinalLevels.Vol = channel
  volume only; when on, nothing is sent (**inference**: FL's default is linked, Final.Vol = Init.Vol·channel volume —
  round 28 §4.1) **[DECOMP]**.
- **Per record, per host block:** `vol = g36·(Final.Vol + o_vol)·g_uni`, `pan = Final.Pan + o_pan + pan_uni`, handed to the
  host's ComputeLRVol (slot 31) which returns L/R (FL law A: `L = vol·cos((1 + pan)π/4)`, `R = vol·sin(…)`, round 28);
  `g36 = 0.75·(3^{x36} − 1)/2` (exactly 0.75 at x = 1) **[DECOMP + EXEC (L = R = vol and law A both matched)]**.
- So the absolute output of a note is `Σ_i a_i sin(…)·env·L/R`; e.g. the default Saw (`Σ sin/h`, peak ≈ π/2) at
  p36 = 1000, FL channel volume 10000/12800 (Final.Vol = 0.551) and centre pan peaks at ≈ 1.571·0.75·0.551·0.707 ≈ 0.46
  **[model, from the confirmed laws]**.

### 1.17 Stereo tremolo (p25) **[DECOMP + EXEC]**

Round 27 §1.1 confirmed (−74…−84 dB) plus: the depth applied in a host block is a ramp from the previous depth to
`p25/1000` with step ≤ `0.001·44100/fs` per sample; the previous depth snaps to the target whenever no record exists, so
a note that starts in the very first host block fades the tremolo in from 0 (this made `trem1000` go from −44 to −76 dB)
**[DECOMP Gen_Render `+0x7A0`, `FUN_00604a90` + `FUN_00612db0`; EXEC]**.

### 1.18 Polyphony and voice stealing **[DECOMP + EXEC]**

Max polyphony = host ProcessEvent 1 (`+0xE0`). On note-on, if the number of steal-able records reaches it, the one with
the smallest priority stamp is faded out (§1.13): stamp = start counter for held first sub-voices, `start − 2⁶³`
for released ones (released voices are stolen first, oldest first), "never" for secondary sub-voices and held mono
notes. Voice_Kill (slot 11) removes a record immediately. EXEC (`r32/py/t_poly.py`, harness option `pe=1,n`): limits 2
and 3 with four overlapping notes, with releases and with 3-voice unison, −69…−81 dB (the residual sits in the 30 ms
fades, about −60 dB locally).

### 1.19 Random sources **[DECOMP + EXEC]**

- The plugin's generator is a Mersenne Twister (MT19937; standard tempering masks) **seeded from
  `QueryPerformanceCounter`** at creation (`FUN_0074bcf0`), so its draws differ in every FL session (**not
  reproducible**, only the laws are). Per record it draws a 32-bit seed (`+0xD0`, copy `+0x1A8`) and an LFO random value
  `ρ = draw/2³²` (`+0xC4`; one shared draw for all sub-voices when the LFO is global) **[DECOMP]**.
- The same record seed is also the starting state of the Noise-resonance generator (§1.8).
- **Phase randomness p0** (`x = p0/1000`, internal `sign·x²`): p0 > 0 ("running"): `r = x²·frac(f_h1·277·N_c)` with
  `N_c` = plugin sample counter (samples rendered since creation/flush); p0 < 0 ("spectral random"): every position's
  phase also gets `U[0, x²]` from IPP `RandUniform_Direct_64f` seeded with the record's seed, and `r = x⁴·frac(…)`
  **[DECOMP + EXEC: with the DLL's seeds peeked, −84/−87 dB; the 64-bit generator is the round-24 recurrence with double output]**.

### 1.20 Effects rack (p90…p98, compressor type) — structure only

Order on the mix of all voices: chorus → delay → reverb → compressor, then added to the host buffer; the rack keeps
running for a tail after the last voice. The DSP of these units was **not decoded** in this round (**not confirmed**);
all verifications below switch them off (types 0).

### 1.21 Verification against the DLL **[EXEC + model]**

Relative error `20·log₁₀(‖model − DLL‖/‖DLL‖)` over the whole render (both channels, worst one reported), 44.1 kHz,
host block 512 unless stated, state blobs built by `hlrun.build` (v12) and loaded through the DLL's own SaveRestoreState.

**Synthetic patches** (`bat1.out`…`bat5.out`, `bat7.out`, `t_rand.out`, `t_poly.out`; clean patch = Saw, open filter, no modulation, 1 note C5
0…0.5 s + release, unless the name says otherwise):

| Group | Cases (name: dB) | Worst |
|---|---|---|
| Timbre, pitch, volume envelope, rates (`bat1.out`) | colour0 -83.9; colour1 -84.9; colour2 -85.6; colour3 -83.9; colour4 -84.0; colour5 -83.7; colour6 -83.8; colour7 -82.8; colour8 -85.8; colour9 -86.7; colour10 -85.2; eq -89.2; eq2 -84.0; mask_default -83.2; mask_irreg -82.6; lhp -82.6; lhp256 -86.5; subs -78.8; subs_col7 -75.8; detune+1200 -83.9; detune-1200 -80.8; detune25 -83.9; key36 -78.4; key84 -90.2; key96 -89.5; pitch-2400 -78.4; vol1500 -83.9; vol300 -83.9; fvol_vel -83.9; att100 -83.9; att300 -83.9; att600 -83.8; dec300 -83.9; dec600 -83.9; rel0 -83.9; rel600 -83.9; att_dec_rel -83.9; velatt -83.9; fs48k -87.0; fs96k -85.2; blk100 -84.1; twonotes -84.1 | -75.8 (`subs_col7`) |
| Filter, resonance, filter envelope, LFO, vibrato, unison, grittiness, phase randomness, quality (`bat2.out`) | ftype0 -82.9; ftype1 -81.2; ftype2 -83.2; ftype3 -88.0; ftype4 -83.5; ftype5 -91.1; ftype6 -82.9; ftype7 -88.5; ftype8 -83.6; ftype9 -83.3; ftype10 -83.3; ftype11 -83.3; ftype12 -83.1; ftype13 -83.5; ftype14 -85.6; ftype15 -83.6; ftype16 -89.7; cut1000 -81.4; cut6000_w0 -83.9; cut2500_w1000 -81.5; keytrack -88.9; keytrack- -79.7; hzscale -85.3; hzscale_lp -83.7; res0 -83.0; res1 -82.9; res2 -82.9; res3 -82.9; res4 -84.6; res5 -84.9; res6 -82.9; res7 -83.6; res_hz -82.9; res_adapt -86.3; res_hz_adapt -84.1; res_ofs -84.4; selfosc -83.3; fenv_na -82.7; fenv_ad+ -83.6; fenv_ad- -81.1; fenv_rel -82.2; fenv_vel -82.3; motion -77.0; lfo0_pitch -83.9; lfo1_pitch -83.8; lfo2_pitch -81.0; lfo3_pitch -84.1; lfo4_pitch -79.2; lfo5_pitch -80.4; lfo6_pitch -83.9; lfo7_pitch -83.3; lfo8_pitch -83.5; lfo9_pitch -84.3; lfo10_pitch -80.9; lfo12_pitch -82.9; lfo13_cut -83.7; lfo_cut -83.2; lfo_cut- -82.3; lfo_mask -83.3; lfo_att -84.1; lfo_global -84.3; lfo_neg -83.8; vibrato -84.4; vibrato_late -84.5; uni2_t0 -87.3; uni3_t2 -81.9; uni5_t0 -82.8; uni7_t1 -84.8; uni9_t2 -83.3; uni3_pan -83.2; uni_velthick -81.7; grit+ -83.8; grit- -82.5; phrand+ -83.9; quality1 -100.9; quality2 -111.0; quality1_lfo -98.4; master_pitch -85.7 | -77.0 (`motion`) |
| Pluck, tremolo, portamento/mono/legato, voice end (`bat3.out`) | pluck500 -80.8; pluck300_att -80.8; pluck_vel -80.5; pluck_alt -81.2; pluck_mask -81.3; pluck_rel -83.9; pluck_lhp -82.5; pluck_selfosc -82.0; trem88 -83.8; trem1000 -76.4; trem_late -80.6; trem_2notes -83.0; porta_poly -85.8; porta_overlap -79.6; porta_wrap -88.2; porta_lin -83.1; porta_vel -83.7; mono_legato -85.1; mono_retrig -87.4; mono_porta -83.9; mono_uni3 -84.9; end_decay -83.8; end_release -83.0 | -76.4 (`trem1000`) |
| Phaser (`bat4.out`) | phaser0 -81.9; phaser1 -81.9; phaser2 -82.9; phaser3 -80.3; phaser4 -78.6; phaser5 -75.4; phaser6 -83.5; phaser7 -82.8; phaser8 -76.6; phaser9 -23.0; ph_hz -83.9; ph_harm -81.9; ph_lfo -82.3; ph_key -72.3; ph_freq_hz -52.8; ph_q2 -132.6; ph_freq_q2 -82.0; ph_freq_hz_q2 -96.7 | -72.3 (`ph_key`) (Freq type at q0 excluded, see §1.9) |
| Harmonizer (`bat5.out`) | harm_t0_p0 -82.6; harm_t0_p1 -82.5; harm_t0_p2 -82.5; harm_t0_p3 -82.6; harm_t1_p0 -82.4; harm_t1_p1 -82.3; harm_t1_p2 -82.3; harm_t1_p3 -82.5; harm_t2_p0 -82.1; harm_t2_p1 -82.1; harm_t2_p2 -82.1; harm_t2_p3 -82.4; harm_t3_p0 -82.8; harm_t3_p1 -82.8; harm_t3_p2 -82.8; harm_t3_p3 -83.2; harm_t4_p0 -82.6; harm_t4_p1 -82.5; harm_t4_p2 -82.5; harm_t4_p3 -83.2; harm_lfo -85.6; harm_vel -84.8; harm_str -87.1 | -82.1 (`harm_t2_p0`) |
| Noise resonance, release factor, host FCut/FRes (`bat7.out`) | noise_len0 -91.8; noise_short -85.0; noise_long -94.4; noise_motion -89.4; noise_specrand_uni -83.7; relvel_p30 -86.0; fcut+0.3_fres+0.0 -83.0; fcut-0.4_fres+0.0 -83.0; fcut+0.0_fres+0.5 -82.9; fcut+0.2_fres-0.3 -84.2 | -82.9 (`fcut+0.0_fres+0.5`) |
| Random draws peeked from the DLL (`t_rand.out`) | {0: -500} -83.9 dB; {0: -1000, 65: 3, 67: 100} -86.1 dB; {86: 11, 42: 500, 58: 300, 54: 4000} -81.4 dB; {86: 11, 42: 300, 65: 2, 87: 0} -85.5 dB | |

Polyphony (`t_poly.out`): maxpoly 2 {} -68.7 dB; maxpoly 3 {} -73.2 dB; maxpoly 2 {28: 600} -80.6 dB; maxpoly 2 {65: 3} -72.7 dB.

**Demo channels** (`bat6.out`): the 139 Harmless channels of the installation's demo projects, every parameter as saved
except the effects rack (chorus/delay/reverb/compressor off), three overlapping notes (C5 0–20000, G5 vel 0.6
8000–26000, G4 vel 0.95 16000–30000 samples), 1 s:

- **135 of 139 channels ≤ −60 dB** (median −81.7 dB; 129 ≤ −70 dB; 78 ≤ −80 dB; best −107.1 dB; worst of these −64.2 dB,
  "Bass Action"). They cover every colour incl. Custom tables, unison 1…9 of all types, both phase-randomness modes (with the
  DLL's per-voice draws peeked), pluck, phaser types 0…8, harmonizer, tremolo, mono/porta/legato, Noise resonance, all
  filter and resonance types used in the demos.
- **The 4 remaining channels** (`Harmless Galactic`: Snare −26.2, High Hat +1.7, Galactic +0.6, Noisesweep −3.4 dB) all use
  the **Freq phaser at quality 0**, whose output depends on the error pattern of IPP `Exp_32f_A11` (§1.9) — the phases of
  the frequency-modulated partials drift apart; the law itself is confirmed at quality 2 (−82/−97 dB).
- Per-channel list with the features used: `bat6.out`.

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| H1 | Partials | 528 positions (self-osc, Sub 1/3/4, 524 harmonics); count `round(17000/(f₀D)) + 4` (×1.5 at q1, all at q2), bank count only grows **[DECOMP+EXEC]** |
| H2 | Rendering | sine oscillator bank (recurrence resynced from double phase accumulators), not an IFFT; per-sample one-pole amplitude glide 99 %/10 ms; band limit 16–32 Hz / 16–17 kHz squared ramps (none at q2) **[DECOMP+EXEC]** |
| H3 | Control rate | targets every B = 88/44/8 samples (q0/q1/q2) at 44.1 kHz, scaled by 44100/fs; frequencies only when the pitch changed **[DECOMP+EXEC]** |
| H4 | Amplitude pipeline | colour×EQ → mask → [harm] → filter → resonance → [harm] → phaser → [harm] → pluck → [harm] → LHP (toward colour×EQ) → special gains → band limit **[DECOMP+EXEC]** |
| H5 | Phases | colour phase + (u + r) fundamental periods; u = 0.5 for one voice; positions 0–3 start at 0; continuous across pitch changes **[DECOMP+EXEC]** |
| H6 | Filter read | nearest table entry (cvtps2dq), interpolated at q2; knob smoothed 99 %/0.1 s; host FCut adds (hi−lo)/2 per unit **[DECOMP+EXEC]** |
| H7 | Filter envelope | per-block t with carried overshoot; tension curves; release only ends a rising attack **[DECOMP+EXEC]** |
| H8 | Resonance | round 27 laws + amount includes host FRes·0.5; Noise type = energy-preserving random partial gates held ℓ ms **[DECOMP+EXEC]** |
| H9 | Phaser | 10 tables (closed forms), 3 scales, plugin-wide phase mod 4, Freq type modulates frequencies **[DECOMP+EXEC]** |
| H10 | Harmonizer | shifted spectrum copies (×2,4,8… or ×2,3,4…), weights min(1,c³S)·mix², normalised **[DECOMP+EXEC]** |
| H11 | Pluck | per-harmonic exponential decay 1 % in (T+0.25)/h s after the attack **[DECOMP+EXEC]** |
| H12 | Volume envelope | linear attack with 0.2 s minimum slope + output smoother (min(A,30 ms) → 80 ms → 30 ms), exponential decay/release to 1 %; end below 10⁻⁴ with 30 ms fade **[DECOMP+EXEC]** |
| H13 | LFO / vibrato / tremolo | all shapes incl. smooth saw sum and voice random; vibrato 6 Hz; tremolo with depth ramp **[DECOMP+EXEC]** |
| H14 | Unison | exact per-sub-voice detune/pan/phase/attack/LFO offset and gain **[DECOMP+EXEC]** |
| H15 | Porta / mono | level offsets with exponential or linear glide per block, ±1250-cent wrap (porta only), legato hand-over/back **[DECOMP+EXEC]** |
| H16 | Output level | `0.75·(3^x36−1)/2 · (Final.Vol + o) · g_uni` through the host L/R law **[DECOMP+EXEC]** |
| H17 | Randomness | MT19937 time-seeded per session → per-voice seeds; laws verified with peeked seeds **[DECOMP+EXEC]** |
| H18 | Effects rack | order chorus → delay → reverb → compressor; DSP **not confirmed** |
| H19 | Verification | ~200 synthetic cases −68…−133 dB (Freq phaser at q ≤ 1: −23/−53 dB, IPP-limited); 135/139 demo channels ≤ −60 dB (median −81.7), the 4 others are Freq-phaser q0 channels **[EXEC+model]** |

---

## 2. For LYNX

Reference implementation: `r32/py/hlmodel.py` (`Patch`, `Voice`, `Plugin`, `render`) + `hlmodel_tables.py` — port it
1:1. It reproduces the DLL to about −80 dB on single features and on the demo channels (§1.21).

1. **Replace the approximation of `synthFromHarmless`** (spectral oscillator + biquad + ADSR) by a dedicated
   `HarmlessInstrument` (same pattern as `SakuraInstrument`): parse the whole `0xD5` state (100 parameters, the 7 combo
   integers, the custom table) and keep it in the channel; no conversion to `SynthSettings`.
2. **Bank:** per voice record, 528 partial slots with double phase accumulators and float amplitudes; render with
   `sin(2π·φ)` (any accurate sine — the DLL's recurrence is an optimisation) and the per-sample amplitude glide
   `a ← T + (a − T)·exp(ln 0.01/(0.01·fs))`; partial count/band limit as §1.3 (band limit off in LYNX's offline render
   only if LYNX also wants to imitate FL's "HQ render" = quality 2 — log the choice).
3. **Control block** B of §1.2 counted from the note start; per block run §1.4 in exactly that order; recompute
   frequencies only when the cents value changed; use the nearest-entry table reads (§1.6) with the 17 + 9 + 10 closed-form
   tables of `hlmodel_tables.py` (no table is copied from FL; they are regenerated).
4. **Envelope pass** of §1.13 per sample, then `mono·L/R` with the per-block L/R ramp (§1.15) and the tremolo (§1.17).
5. **Host levels:** compute `Final.Vol/Pan/Pitch/FCut/FRes` per note as round 28 §4.4 does for Sakura/Drumpad (channel
   volume law, pan law A, `Final.Pitch = 100·(key − root) + channel pitch`), apply `vol = 0.75·(3^x36−1)/2·Final.Vol·g_uni`
   and the pan law, and **do not apply the channel volume/pan a second time**. Velocity passed as FL velocity/128;
   honour p37 (Final.Vol without velocity when off).
6. **Plugin-wide state:** vibrato phase, LFO global phase, phaser phase, sample counter and tremolo depth live in the
   instrument (not per voice); reset them on transport stop/flush; on a song-position change set the LFO/phaser phases
   as §1.9/§1.12 (`n_pos` in samples).
7. **Random:** FL's draws are time-seeded and cannot be reproduced; use any per-voice uniform generator for the seeds and
   ρ (log "random phases differ from FL by design").
8. **Mono/porta/legato** with the channel slot logic of §1.15; max polyphony and stealing of §1.18.
9. **Not covered by this round** (keep the current behaviour and log it): the effects rack (chorus/delay/reverb/compressor —
   LYNX can keep routing these to its own effects), the IPP-accuracy-limited sample exactness of the Freq phaser at
   quality ≤ 1, and FL's actual answers to host events 4/5 (use 0 and 1, as the harness, and log it).
10. Remove "per-partial filter, resonance, tremolo, vibrato, unison spread…: approximated" from STATUS.md only after the C++
    port matches `hlmodel.py` (same states, 44.1/48 kHz) to about −60 dB.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| VMT `0x7791d0` slots 1, 3, 5, 6, 8–12 | plugin interface | DECOMP |
| `FUN_00782940`, nested `FUN_00782810` | unison records, attack/pluck/LFO set-up, mono/porta entry, random draws, velocity (event 3, float), link-velocity event 6 | DECOMP + `objdump 0x7829dc…0x782a3b` |
| `FUN_0077f170`, table `0x7ee584` = {88, 44, 8}, consts `0x77f428…0x77f478` | control block, smoother coefficients (0.01/0.1/1.25 s), 0.03/0.2/0.002 s, 6/3/2.5 Hz | DECOMP + EXEC |
| `FUN_0077f480`, `FUN_0077c450`, threshold `0x7e9dd8` (−96 dB / `0x7d7774`) | quality → lookup mode, bank mode, threshold | DECOMP + EXEC |
| `FUN_007886b0` | Gen_Render order, tremolo, phases advance, effects order | DECOMP |
| `FUN_00784a10` | control block pipeline, pitch, filter, resonance, phaser, pluck, LHP, special gains, band limit, initial phases, envelope pass, voice end | DECOMP + EXEC |
| bank `FUN_00744e20…FUN_00746b00` | sine recurrence + double phases, resync, glide, activation, entering partials, q2 path | DECOMP + EXEC |
| `FUN_005f5f60`, `FUN_005f5f30`, `FUN_005f5fe0`, `FUN_005f6040`, `FUN_005f6120` | smoothers, variant choice | DECOMP + EXEC |
| `FUN_00783910`, `FUN_00783d80`, `FUN_00783ac0`, `FUN_00783b90`, `FUN_00787ef0`, `FUN_00784560`, `FUN_00787ec0`, `FUN_00784630` | release, filter-env release, fade/steal, kill, legato, glide | DECOMP + EXEC |
| `FUN_0077c470`/`FUN_0077c400`/`FUN_0077c540` (`cvtps2dq` at `0x77c4d5`) | nearest / interpolated reads | DECOMP + EXEC |
| `FUN_0077ca30` codes 200–209, `FUN_0077c970`, `FUN_0077c880`, consts `0x77efd0…0x77f048` | phaser tables, RMS 1, f18/f1c | DECOMP + EXEC (`phshape.txt`) |
| `FUN_0077c690`/`FUN_0077c760` (`cvtps2dq 0x77c6e2`) | phaser read with wrap | DECOMP |
| `FUN_00784220`, table `0x7ee634`, `objdump 0x784330…0x784511` | harmonizer | DECOMP + EXEC |
| `FUN_00784860`, consts `0x7849e8…0x784a00`; `+0x608`/`+0x610` from `FUN_0077f170` | pluck | DECOMP + EXEC |
| `FUN_005fd4d0` + `objdump 0x7801a7…0x7802d9` | tension curve record | DECOMP |
| `FUN_0077b450` (`0x811A50` smooth-saw weights, `0x811AC0` top fade, `0x811AC8` LHP, EQ windows) | global tables | DECOMP + EXEC (array peeks) |
| `FUN_0077f630` cases 0, 1, 5–23, 0x24–0x3F, 0x43–0x4E, 0x50–0x53, 0x58, 0x59 | parameter laws used by the model | DECOMP |
| `FUN_007873c0`, consts `0x787930…0x787980`, `FUN_005a43b0` → trunc | colour formulas, parity with trunc | DECOMP + EXEC |
| `objdump 0x7855aa…0x785605` | float `+60` in the pitch conversion | DECOMP + EXEC |
| unison table `0x7ee5c8` (float32) | detune factors | DATA + EXEC |
| `FUN_0074bb70`, `FUN_0074ba60`, `FUN_0074bcf0` | MT19937 seeded by QueryPerformanceCounter | DECOMP |
| `FUN_007793e0` event 0xD, const `0x779d00` | song position → LFO/phaser phases | DECOMP |
| `FUN_00743100` | max polyphony (ProcessEvent 1) | DECOMP |
| `FUN_00612db0`, host rate object `+0x34` = 0.001·44100/fs | ramp limit | DECOMP + EXEC |
| `r32/run/hl.exe` + `bat1…bat6.py`, `t_rand.py` | §1.21 | EXEC + model |
| `r14/blobs14.pkl` (139 demo channels) | demo states | DATA |

### Open questions

- Effects rack DSP (chorus types, delay, reverb types, compressor modes) — not decoded.
- Sample-exact output of the Freq phaser at quality 0/1 depends on the internal error pattern of IPP `Exp_32f_A11`
  (and, at a much smaller level, `Ln_32f_A11` for the log arrays); the laws are confirmed.
- Host meanings: Voice_ProcessEvent 5 (factor on the release time with p30) and 4 (release velocity for p46 on
  legato-back) were answered with 1.0 and 0.0 by the harness; FL's values were not observed. FL's default link-velocity
  state (p37 on) is an inference.
- Song-position repositioning of the LFO/phaser phases and the polyphony limit were read but not executed.
- Which of the two smoother variants FL uses on other CPUs (feature byte); both laws are described.
