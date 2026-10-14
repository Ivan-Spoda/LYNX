# Behavioural analysis of FL Studio 2026: round 34 (REA) — Harmor: the complete voice as an additive engine

> **Nature of this document.** Continuation of `REA_rodada11.md` §4, `REA_rodada18.md` §1, `REA_rodada19.md` §2,
> `REA_rodada23.md` §1, `REA_rodada24.md` §3 and `REA_rodada25.md` (Harmor), with the same goal as round 32 for
> Harmless: describe the whole Harmor voice precisely enough for LYNX to port it as an exact instrument instead of the
> current approximation (`synthFromHarmor` in `src/core/FlSytrus.cpp`). *Clean-room* reverse engineering, behavioural
> only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is here are data layouts, formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched (no FLEX pack was opened) and no Image-Line content (code, audio, tables)
> was copied into the repository.
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
- **Target:** `Plugins/Fruity/Generators/Harmor/Harmor_x64.dll`, SHA-256
  `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` (= rounds 11, 18, 19, 23, 24, 25; re-hashed this
  round, copy in `r34/bin/`), Delphi, `TTestPlug`, VMT `0x86dbd0`. VMT slots used: 1 Dispatcher `FUN_0086dda0`,
  3 SaveRestoreState `FUN_0086f2f0`, 5 ProcessEvent `FUN_00879280`, 6 ProcessParam `FUN_00879540`,
  **8 Gen_Render `FUN_00891320`**, **9 TriggerVoice `FUN_00883cd0`**, **10 Voice_Release `FUN_00885390`**,
  11 Voice_Kill `FUN_00885730` **[DECOMP]**.
- **Key functions of this round:** voice control block `FUN_0088ab10`; module-chain builder `FUN_008737e0`; modules —
  local EQ `FUN_00886e00`, prism `FUN_00886aa0`, harmonic randomness `FUN_00886e30`, harmonizer `FUN_00885cb0`,
  filter `FUN_00886eb0` (one call per filter), phaser `FUN_00887f10`, pluck `FUN_00888500`, blur `FUN_00886610`,
  protection `FUN_00886110` (kernel `FUN_00882750`), clip `FUN_00886160`, volume `FUN_008860c0`; timbre mix
  `FUN_00885b70`/`FUN_00885af0`; per-partial curve sampler `FUN_00888ed0`; per-part tables — partial ratios
  `FUN_0088fcf0`, local EQ `FUN_00889ec0`, protection `FUN_00889470`, clip `FUN_00889750`, harmonic randomness
  `FUN_008895a0`, unison-pitch curve `FUN_00889b30`, blur mask `FUN_0088a1f0`, filter range `FUN_00874240`, maximum
  frequency `FUN_00873d10`, pitch ratio `FUN_00873c60`; table builder `FUN_00874ed0` (filters, resonance, phaser,
  masks, global EQ; trimming `FUN_00629eb0`/`FUN_00874e40`/`FUN_0062d310`, RMS normalisation `FUN_00874c70`); table
  reads `FUN_00874930` (nearest `FUN_00874860`, interpolated via `FUN_008747f0`), wrapped read `FUN_00874b50`; array
  trim `FUN_00874640`; partial count `FUN_0088fbc0`; energy `FUN_00882f00`; parameter handling `FUN_00879540`; state
  load (part combos) around `0x8789xx`; the generic point curve / envelope (`TMEnvelope`): start `FUN_007a07a0`, step
  `FUN_007a0c60`, release `FUN_007a09f0`, point value `FUN_0079fef0`, per-point coefficients `FUN_0079e680`, neighbour
  `FUN_0079e5e0`, table render `FUN_007a2640`, absolute step `FUN_007a1930`/`FUN_007a25e0`; LFO waveform
  `FUN_0060fe20`/`FUN_0060ffe0`/`FUN_00610240`, LFO start `FUN_007a34c0`/`FUN_007a33e0`; arpeggio fields
  `FUN_007a24b0`; master volume `FUN_007a4a90`/`FUN_007a4c00`/`FUN_007a4cc0`; Mersenne Twister `FUN_007a9c30`.
  **The additive oscillator bank** is the same library object as Harmless (round 32 §1.3) at `0x783xxx…0x784xxx`
  (render `FUN_00784840`, kernels `FUN_00783ec0`/`FUN_00783ff0`, resync `FUN_00783d70`, targets `FUN_00783980`/
  `FUN_00783a20`, activation `FUN_00784260`/`FUN_00784490`, entering partials `FUN_007845a0`).
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `GHIDRA_HEADLESS_MAXMEM=2G`), project `/tmp/r34gh_Hm`: full decompilation of
  `0x860000…0x8a0000` in `r34/out/plug_all.c` and of the library range `0x778000…0x7b0000` in `r34/out/lib78.c`
  (copies with IPP import names substituted: `plug_all_ipp.c`, `lib78_ipp.c`, made by our `r34/py/ippsub.py`), voice
  `voice_ipp.c` + `voice.asm`, TriggerVoice `trig.asm`, ProcessParam `pp.c`, table builder `tb.c`, curve/envelope
  `envfun.c`, LFO `lfo_ipp.c`; `objdump` for instruction-level checks; constants with `r34/py/k.py`.

**EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r34/run/hm.c` → `hm.exe`, derived from round 32's `hl.c`
(same safety measures: `SetErrorMode`, vectored fault handler that ends the process, the installation's DLL loaded
unmodified from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's
own SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, everything under `timeout`). Notes are
triggered with the SDK `TVoiceParams` record (`Init.Pitch = 100·key`, `Final.Pitch = 100·key − 6000`, Final.Vol/Pan as
given; velocity answered through Voice_ProcessEvent 3); host blocks are split at every event sample. New this round:
the **Mersenne-Twister state is dumped** (plugin `+0xE88` → index `+8`, 624 words `+0xC`) right before the first
TriggerVoice, so the model can reproduce the DLL's time-seeded random draws; `save=` writes a state through a writable
`IStream`; `arr=voice,block,count,type,path` reads arrays/fields through pointer paths (voice −1 = plugin, −2 = module
image, block −3 = right after TriggerVoice), `peek=`/`pp=` read single fields. No process or dialog was left behind.

**Model (pure Python + numpy):** `r34/py/hmodel.py` (voice, parts, render) with `hart.py` (articulators),
`henv.py` (envelope time engine), `hcurve.py` (exact point-curve evaluator, modes 0–12), `tabs.py` (tables and reads),
`hphaser.py` (phaser tables 200–208), `hrand.py` (MT19937 + IPP uniform generator), `hst.py`/`harm.py`/`syt.py` (state
parser and patch helpers). Driver/verification: `hmrun.py` (runs `hm.exe`), `demo.py` (DLL renders of the demo
channels, cached in `r34/run/o/demo2_XX.npz` with the MT state), `bat_demo.py` (demo battery), `synth_bat.py`
(synthetic battery), `tcurve3.py` (curve-mode check), `gen_table.py`, debugging helpers `dbgp.py`, `dbgt2.py`, `dbgn.py`,
`dbgv.py`, `dbg3.py`. Data: `r14/blobs14.pkl` (the 99 Harmor channels of the installation's demo projects).

Manual page: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Harmor.htm`
**[DOC]** — quotations used below are marked with the page anchor text they come from.

---

## 1. Harmor — the voice

### 1.1 Structure and data flow **[DECOMP]**

- **Two parts** (A, B), each a full additive synth; the part blocks live at plugin `+0x11E8 + k·0xB70`. Part parameters
  are the 386 integers at plugin `+0x138 + (base + i)·4` (base 0 for A, 386 for B); the 53 global parameters follow at
  index 772 + i. Each part also has a 0x30-byte module-order block (`blk30`, 12 integers = processing order, see §1.4),
  a 0x4B-byte combo block (`misc`), 43 articulators and 26 shaping curves (state layout: round 11 §4.1). A part sounds
  only when `misc[0x44]` (enabled) is set.
- **Voice records:** TriggerVoice creates, for every enabled part, one record per unison voice (`p34` = 1…9); every
  record owns its own oscillator bank (`+0x140`), articulator states (`+0x2D0 + k·0x120`, 43 of them) and module
  buffers. A record is linked to its part (`+0x58`).
- **Per control block** (`FUN_0088ab10`, §1.2) a record evaluates its articulators, its pitch, the partial
  frequencies/count/phases, builds the partial target amplitudes through the part's module chain (§1.4) and hands
  frequencies and targets to the bank, which renders samples (§1.3). Gen_Render (`FUN_00891320`) mixes the records with
  per-record gains/pan (§1.17), applies the part tremolo (§1.18), runs the effects rack (not decoded, §1.24) and the
  master volume (§1.17).

### 1.2 Control block, precision and plugin rates **[DECOMP + EXEC]**

- **Control block** `B = 4·max(1, round(cr·0.01·0.001·fs·0.25))` samples with `cr` = global misc int `+24` (stored
  ×100 ms; default 200 → 2 ms → **B = 88 at 44.1 kHz**). Every record counts its own blocks from its note start.
- **Precision** (global misc int `+28` → plugin `+0x1140`; 0 = "Average", 1, 2 = "Perfect"; a flag at `+0xE3D` forces
  2) changes: (a) every table read (filters, resonance, masks, EQ, phaser) from **nearest entry** (precision 0;
  `cvtps2dq`, i.e. round-to-nearest of the clamped float index — round 25's "truncation" was wrong) to **linear
  interpolation** (≥ 1: index = round(x − 0.5), fraction = x − index; out-of-range → first/last entry); (b) the IPP
  accuracy variant of the vector exp/sin/ln (A11 at 0, A21/A24/A53 above); (c) the bank threshold and sine method as in
  Harmless. [DOC] Average: "Where all computations are made by a team of college-level mathematicians."; Perfect: "All
  calculations are done by Isaac Newton, Pierre de Fermat, Carl Gauss & Albert Einstein." **[EXEC: demos 42/80–83/86
  went from −56…−68 dB to −66…−100 dB once the interpolated read was implemented; synthetic `prec1` −91.7, `prec2`
  −115.9 dB]**.
- **Frequency clamp:** after every block whose frequencies changed (and right after the ratio multiply in log mode)
  all partial frequencies are clamped to plugin `+0x10D8` = 23.7772331 cycles/sample at 44.1 kHz **[DECOMP + EXEC
  value]** (= 2²⁰/fs — **inference** from the value).

### 1.3 The partial bank **[DECOMP + EXEC]**

Same library object as Harmless (round 32 §1.3: double phase accumulators advanced by `f·m` per segment, float
recurrence resynced from the accumulators, output `Σ a_i[k]·sin(2π(φ_i + k·f_i))`, per-sample one-pole amplitude
glide `a ← T + (a − T)·k`, a new record starts from `a = 0`), with these Harmor specifics:

- **Positions:** 520 (0 = self-oscillation partial, 1 = Sub 1, 2 = Sub 3, 3 = Sub 4, 4…519 = harmonics 1…516).
- **Ratios** (`FUN_0088fcf0`, part `+0xE8`), `d = p42/(p43·10000)`:
  linear partial mode (part misc `+0x1C` ≥ 1): harmonic h → `1 + (h − 1)·d`; **logarithmic mode** (`+0x1C` = 0):
  harmonic h → `q^(h−1)` with `q = 2^(d/8)` (computed by repeated multiplication) **[EXEC: ratios read back exactly]**.
  Subs: submode (misc `+0x18`) 0 → 0.5, `1 + 0.5·d`, `1 + 1.5·d`; submode > 0 ("below") → 0.5, 0.25, 0.125.
- **Amplitude glide coefficient per partial:** from the glide combo misc `+8` = v, `N = max(1, 25·g7(|v|/1000)·0.001·fs)`
  samples with `g7(x) = (7^x − 1)/6` (1 at x = 1); for v ≥ 1 `k = exp(Hz·ln 0.01/(N·2^(1/4)·308))` per partial (from its
  Hz), otherwise `k = exp(ln 0.01/N)` for all partials **[DECOMP + EXEC]**.
- **Activation threshold** 5.96·10⁻⁸ (`0x918140`): inaudible, kept for completeness **[DECOMP]**.
- **Partials entering later** (count grows after a pitch drop): `φ_j += f_j·(φ_{n0−1} − Φ)/f_{n0−1}` for the new
  partials, `Φ ← φ_{n−1}` (before the add); **done at the end of the control block, after the module chain**, i.e. with
  the prism/phaser-modified frequencies (`FUN_007845a0` is called after the targets) **[DECOMP + EXEC: demos 93–96 went
  from −30 to −70…−79 dB]**.

### 1.4 Per control block — order of the stages **[DECOMP `FUN_0088ab10`; EXEC per-stage array peeks + batteries]**

1. **Articulators** (§1.12): the 12 mapping/global articulators 31–42 first, then (first block only) the global
   section — art 40 adds `out·2³²` to every per-voice LFO phase, the global attack/decay skip (art 33, plus velocity
   when p14 ≠ 0) advances the envelopes, and non-neutral global envelope scales (arts 34–37) are **not modelled**
   (the model raises) — then articulators 0–30.
2. **Pitch** (§1.5), **partial count** (§1.6), **frequencies** — recomputed only when the cents value, the unison
   detune term `a4` or the prism value changed, or when a module invalidated the cached pitch (prism-from-level,
   frequency phaser, blur width change).
3. **Initial phases** (first block, §1.7).
4. **Timbre** `T = T1·(1 − m) + T2·m`, `m = min(p4/1000·A3, 1)` (blend combo misc `+0` ≠ 0 is not modelled); Sub 1/3/4
   targets × `p5/1024`, `p6/1024`, `p7/1024`, position 0 × `(p62/1024)²` (§1.10). This array (`+0x1A0`) is also the
   source of the protection module.
5. **Module chain** in the order of the 12 `blk30` integers (`FUN_008737e0`): 0 local EQ (only when not first — when it
   is first it is folded into the timbres), 1 prism, 2 harmonic randomness (only when its table count > 0), 3
   harmonizer, 4/5 filter 1/2, 6 phaser, 7 pluck (unless misc `+0x46`), 8 blur, 9 protection (only when its table count
   > 0), 10 clip (only when its table count > 0), 11 volume (dropped when it is the last module; then applied at the end).
6. **Hz and log₂ arrays** of the partial frequencies (float Hz clamped below at 16.3516 Hz; log₂ via IPP Ln).
7. **Subs below**: when submode > 0, positions 0–3 × `(min(Hz·c, 1)²)²` with the runtime constant `c` =
   0.0087365862 (bits `0x3C0F23E6`, plugin global `0x94E120`) **[DECOMP + EXEC value]**.
8. **Self-oscillation partial** (position 0): frequency = filter-1 cutoff × `2^(p60/1200)`, target
   `(p62/1024)²·r₁·pluck_level₁·v_b8` (r₁ = filter-1 resonance amount, §1.9).
9. **Top fade** of positions ≥ 456 (cosine over 66 positions), **global EQ** (§1.11), **volume factor**
   `v = A1²·g3(p12/1000)` (unless already applied by the volume module), **auto-gain** (§1.16), then the targets are
   stored; frequency clamp (§1.2); glide coefficients; entering-partial phases (§1.3).

### 1.5 Pitch **[DECOMP + EXEC]**

- Cents `c = Final.Pitch + g14 + 1200·log₂(p40/(p41·10⁴)) − 1200 + o_pitch` (`o_pitch` = legato/porta offset, §1.19),
  `+ sin(2π·φ_vib)·(p45·0.1)·A25` (vibrato; φ_vib = part phase `+0x4C`, advancing `2^(3 − p46/1000)/fs` per sample in
  Gen_Render, read at the record's block start), `+ 2·p44·A24` (pitch articulator).
- `log₂ f₀ = log₂ 440 + ((c·0.01 + 60) − 57)/12` (float ops as in Harmless); `f₀/fs` stored as float.
- Unison detune (§1.14) multiplies `f₀/fs` by `2^(a4/1200)` (types 0–3, no per-partial unison curve) with
  `a4 = u_det·A26²`.

### 1.6 How many partials **[DECOMP + EXEC]**

`n = round(q) + 1 + M + 4`, rounded up to a multiple of 8, capped at 520, **only grows**; M = harmonic-blur half width
(§1.15). Linear mode `q = f_max/f₀` with `f_max = f_EQ·2^(0.0625 − log₂ d)` (f_EQ = highest non-zero frequency of the
global EQ table, §1.11; `0.0625` = plugin `+0x10CC`); log mode `q = (log₂ f_EQ − log₂ f₀)·8/d` (`FUN_00873d10`,
`FUN_0088fbc0`). With the multiplicative prism on (§1.10) `q /= max(0.01, 1 − |prism|)`.

### 1.7 Initial phases **[DECOMP + EXEC]**

Per record, first block (`FUN_0088ab10`, `objdump 0x88ca39…0x88cd05`):

- Phase table `P_i` = shaping curve "harmonic phase" (shp 1, centre 0.5) sampled per position, `+1`; position 0 = 0.
- Unison phase term (u = voice index/n, x = `p39/1000·A41`, made `−|x|` for Hz unison): x ≤ 0 → `d = p0/1000 + u·x`;
  x = 1 → **free phase**: spectral random phase with the record's own seed and d = 0; otherwise
  `d = p0/1000 + D − round(D)`, `D = (2^(16x)·2 − 2)·exp(u_det·ln2/1200)`.
- Spectral phase randomness `r = sign(p1)·(p1/1000)²`: r < 0 → `φ_i = P_i + U(0, |r|)` (520 draws of the IPP 64-bit
  uniform generator from the record's seed, §1.21); r > 0 → `d += r·frac(f_h1·277·count)` with `count` = part sample
  counter (`+0x128`, samples rendered since the plugin started).
- `Φ = φ_{n−1}` (stored before the offset); `φ_i += f_i·d/f_h1` for the n partials; positions 0, 1 (and 2, 3 with subs
  below) are set to 0 **[EXEC: phases read back exactly; demos 5/49/89–96 pass only with this]**.

### 1.8 Timbres, local EQ and phases of the timbre **[DECOMP + EXEC]**

Round 23 §1 (timbre curves T1/T2 = shp 2/3 rendered at 516 positions, `(4v)²`; local EQ shp 0 sampled at
`log₂(h)·length/9`, `(4v)²/h` in linear mode and `v²` in log mode) is confirmed by the battery. The per-position curve
sampler `FUN_00888ed0` (index 4+i at `log₂(i+1)·length/9`, position 1 = position 4, positions 2/3 at the sub ratios,
position 0 = 1; a constant curve fills every entry with `y − centre`) is used by local EQ, prism, pluck, protection,
clip, unison-pitch curve and harmonic randomness.

### 1.9 Filters and resonance **[DECOMP `FUN_00886eb0`, `FUN_00874240`, `FUN_00874ed0`; EXEC]**

- **Range:** `lo₀ = 6.19802618` (log₂ 73.416 Hz), `hi₀ = 14.1146927` (log₂ 17 739.7 Hz); `w = p54·0.004`,
  `W = (2/3)/2^(1−w)`, `a = W·e_lo`, `b = W·e_hi` (table extents); `lo = lo₀ − (a − sign(a)·min(a,1))`,
  `hi = hi₀ + (b − sign(b)·min(b,1))`; knob `= lo + p52/8192·(hi − lo)`.
  **Custom shapes (types 17/18):** the extents come from the trimmed table — `e_lo` (table `+0x18`) from the run of
  1.0 at the **end** of the table, `e_hi` (`+0x1C`) from the run of 1.0 at the **start** (`1 − 2k/N`) **[DECOMP + EXEC:
  demos 11/33 went from −23/−19 to −77 dB; round 25 had them swapped]**.
- **Position:** knob smoothed per record (`s ← k + (s − k)·0.912201345` per block, state initialised to the knob at
  the first block), key tracking `(log₂ f₀ − 7.03136)·p55/1000`, articulator `+ A4·2(hi − lo)`; envelope term
  `p56/1000·A31·E·2(hi − lo)` or, with "adaptive" (p51 > 0), `a = 4·amt·E` → `L += a·(hi − lo_kt)` (a ≤ 0) or
  `a·(hi − L)`; then the articulator time smoothing; `f_c = 2^L`. The filter-frequency envelope depth of arts 4/5 is
  zeroed at trigger (round 18/19).
- **Read:** shape `g = tab(log₂f·s + o − L·s)`, `s = 2^(1 − w·A6)·S` (S = table `+0x20`, 1024 or `N/4` for custom
  shapes), or in Hz scale (p53 ≠ 0) `tab(Hz·0.9s/f_c + o − 0.9s)`; mask (shp 18/19, 2160-entry table, 240/oct) →
  `T += (g·M − M)·T`, else `T ·= g`.
- **Resonance** (r = p57/1000·A8): `e = rtab(log₂f·w_r + o − (L + p60/1200 + 8·A12)·w_r)`, `w_r = k₀·2^(1 − p59·0.004·A10)`
  (k₀ = S, adaptive p61: `S·2^(−27/12)·440/f₀`; Hz scale variants as round 25); gain `exp(r·ln2·e·mask)`.
  **Noise resonance (type 109) is not modelled** (structure read: random partial gates with a per-record seed `+0x27C`
  hashed every `+0x290` blocks, energy compensation; one IPP routine is reached through a runtime pointer that was not
  resolved) — **not confirmed**.
- **Serial/parallel** filter mix `p74·A14` as round 19/25.
- All shapes used by the demos: codes 0–9, 14, 17, 18 (filter), 100–108, 110, 111 (resonance), 400+ (masks), 300 (EQ):
  closed forms in `tabs.py` **[EXEC: table memory compared, ≤ 2·10⁻⁷]**.

### 1.10 Prism, self-oscillation and subs **[DECOMP `FUN_00886aa0` + EXEC]**

- `pv = p26/1000·A23`. Additive mode (misc `+0x0C` = 0): `prism = sign(pv)·pv²·4`, `f_i += tab_i·f₀·d·prism`;
  multiplicative mode: `prism = clamp(pv, ±1)`, `f_i = (f_i − f₀)·(tab_i·prism + 1 − prism) + f₀` (a flat curve uses a
  single factor). `tab` = prism curve shp 7 (centre 0.5) ×2 (+1 in multiplicative mode).
- **Prism from level** (misc `+0x10` = 1/2): `tab_i ×= min(T_i, 1)` (mode 2: `1 − min(T_i, 1)`) using the targets at that
  point of the chain, (+1 in multiplicative mode), and the cached pitch is invalidated so everything is recomputed
  every block **[DECOMP + EXEC: demo 10 −63 dB; synthetic −79/−80 dB]**.
- [DOC] "Prism shifts the position of the partials from their original relationship to the fundamental frequency."

### 1.11 Global EQ **[DECOMP + EXEC]**

Round 25's table (code 300: 2421 entries, 240 per octave from log₂ 16.35 − 1, cosine skirts, `(4v)²`) with the
**mix** float of misc `+0x30` (part `+0x2C8`): curve values `v·mix + (1 − mix)·0.25` before the skirts
**[DECOMP `tb.c` + EXEC: demo 78 −53.7 → −55.2 dB; synthetic `eqmix0.5` −79.9 dB]**.

### 1.12 Articulators, envelopes, LFOs **[DECOMP + EXEC]**

- 43 articulators per part (0 pan, 1 volume, 2 FX, 3 timbre mix, 4/5 filter freq, 6/7 filter width, 8/9 resonance,
  10/11 res width, 12/13 res offset, 14 filter mix, 15 pluck, 16–18 phaser mix/width/offset, 19/20 harmonizer, 21 clip,
  22 blur, 23 prism, 24 pitch, 25 vibrato, 26 unison pitch, 27–30 image, 31/32 filter env amounts, 33–37 global
  envelope controls, 38–40 global LFO amount/speed/phase, 41 unison phase, 42 …), combination modes from the bit sets
  `0x89732C`/`0x897334` (multiplicative / additive with centre 0.5 / ×2), map curves (key, velocity, X/Y/Z, random,
  unison position), knobs p95…p342 (8 per articulator), time smoothing p343…p385
  (`N = fs(2^(0.009v) − 1)/(B·51.1)` blocks, `coef = exp(ln 0.01/N)`, v > 1000 → none) — as decoded in rounds 18/19 and
  completed here.
- **Envelope engine** (`FUN_007a0c60`): ADSR knobs scale attack/decay/release times by `knob/128`; **the sustain-level
  knob** (`knob₂/128`) is copied into the state when the envelope enters its decay stage and offsets every point value
  by `w_i·S` (w_i = per-point weight of `FUN_0079e680`), **clamped to [0, 1]** (curve `+0x21` flag, range `+0x70/+0x78`)
  **[DECOMP + EXEC: demos 89/92 went from −10 to −80 dB]**.
- **Curve shapes** (point mode byte, `FUN_007a1930` + `FUN_0079e680`): modes 0–9 as round 23; **mode 10** = monotone
  cubic through the neighbouring points (harmonic-mean tangents from the neighbours' slopes, blend `1 − |tension|`);
  **mode 11** = `k14/(1 − k10·t) − k18` (or with `1 − t`), `k10 = 1 − (1 − 0.9|T|)³`; **mode 12** = the same folded
  into two halves **[DECOMP + EXEC: every mode patched into an envelope of a demo, −80…−83 dB]**.
- **LFO:** waveform `FUN_0060ffe0` (tension/skew/pulse width), phase increment `(10001^(v/65536) − 1)·6871947.67·rate`
  per block; per-voice LFOs start at `start + offset` (`FUN_007a34c0`), **global LFOs use the plugin-level phase, which
  starts at `start` without the offset** (it is initialised before the offset field is loaded) **[EXEC: demos 66/91
  went from −18/−7 to −79/−75 dB]**; the LFO amount envelope; art 40 adds a phase offset at the first block.
- **Global envelopes** (flag 2): a new record copies the state of an existing record of the same part (continuing it).
- **Arpeggio envelope points** (point byte `+0xD` ≠ 0): **not modelled.** Read: Gen_Render rebuilds, after a release or
  kill, the list of distinct pitches of the active records (ascending; plugin `+0xD88` selects the order) and writes
  each record's index and the list size into every envelope state (`FUN_007a24b0`); at an arpeggio point the envelope
  steps its own index by `byte − 2` modulo the size and **holds its value unless the index is the record's own**.
  The exact first-block behaviour of new records was not reproduced — **not confirmed** (demos 20, 40, 46).

### 1.13 Harmonizer, pluck, phaser **[DECOMP + EXEC]**

- **Harmonizer** (p27…p33, curve shp 16 for the distribution, mask shp 23): as round 24 (copies of the harmonic
  series at shifted positions, weights `min(1, c³·S)·mix²` (c² when the local EQ precedes it) times the distribution
  curve, normalised by the sum (√ in the second case)) — confirmed by the battery.
- **Pluck** (p75 + A15): starts 2 ms after the note, `T = 2^((v − 1)·17)·20000`, per-partial factor
  `exp(tab_i·(ln 0.01·B/fs)/(T + 0.25))` per block with `tab_i = 2^(−16(c_i − 1))` (c = pluck curve shp 14); the level
  multiplies the targets and position 1's level enters the self-oscillation partial.
- **Phaser** (p76…p81, type = misc `+0x28`): types 200–208 (closed forms of round 32), **209 "frequency"** and **210
  "custom"** from the custom phaser curve (shp 15, 1024 entries, f18 = f1c = 1024): 209 = `(y − 0.5)·2`, 210 = `y²`
  normalised to RMS 1 (`FUN_00874c70`) **[EXEC: table memory read back exactly]**. Index (log, Hz and harmonic
  scales) and plugin phase as round 32; amplitude types `T ·= P·mix + 1 − mix`; **type 209 multiplies the
  frequencies by `2^(P·mix³)`** (float exp at precision 0, double above) and invalidates the cached pitch **[EXEC: demo
  85 −87 dB at precision 1; synthetic −91/−92 dB at precision 1/2 but −24 dB at precision 0, where the result depends on
  the error pattern of IPP `Exp_32f_A11`, exactly as Harmless' Freq phaser in round 32]**. Phaser masks (shp 22) are not
  modelled.

### 1.14 Unison **[DECOMP + EXEC]**

- n = p34, type = p35 (0 classic, 1 blurred, 2 uniform, 3 random, 4 Hz). Detune factor `d_k` from the DLL's per-type
  tables (`0x928290`, DATA) and `uni_s = (41^(p38/256) − 1)·1.875`; `u_det = d_k·uni_s`; pan `clamp(u·uni_pan)` with
  `uni_pan = p37/192` (≤ 192) or `1 + (p37 − 192)·0.046875`; gain `1 − (3^(1−1/n) − 1)/2`, sign inverted on alternate
  voices when p36 is on; unison phase §1.7.
- **Random (type 3):** `d_k += U(±0.5/(n − 0.5))` from the note seed.
- **Hz (type 4):** per record 520 values `D_i = d_k + U(±1/n)` from the note seed; every block the frequencies get
  `+ D_i·(a4/150)·f₀/fs` (cycles/sample, added in double), `u_det = uni_s` **[DECOMP `trig.asm 0x884bac…` + EXEC:
  demos 38/41/84 −84…−89 dB]**.
- **Per-partial unison pitch curve** (shp 8, `FUN_00889b30`): when it is not the flat default 0.5, the detune is applied
  per partial: `f_i ·= exp(tab_i·a4·ln2/1200)` with `tab_i = (2y_i)²` (with Hz unison it multiplies the offsets) instead of
  the single factor on f₀ **[DECOMP + EXEC: demo 43 −14 → −97 dB]**.
- [DOC] "Set unison phase to 100% (full blur) for a smooth unison on high pitch notes, lower notes may sound noisy
  however."

### 1.15 Blur **[DECOMP `FUN_00886610` + EXEC]**

- **Time blur** (p21 attack, p22 decay): per-part coefficients `exp(ln 0.01/N)`,
  `N = (2^(0.01·p) − 1)·0.001·fs/B` blocks (0 when N underflows); per record a buffer chases the targets,
  `b ← T + (min(T, b) − T)·c_att + (b − min(T, b))·c_dec`, starting from 0 at the first block **[EXEC: synthetic
  −79.8 dB; changes the DLL output by +1.6 dB]**. [DOC] Time: "Adjust the leading and trailing blur amount
  independently."
- **Harmonic blur** (p23 × A22): width `W = (17^(p23·A22·10⁻⁴) − 1)·15.9375 + 1`, half size
  `M = min(ceil(W)|1, 257) >> 1`; kernel of 2M+1 taps: linear ramp `1 − s·M … 1` (`s = 1/(0.5W + 1)`) mirrored, shaped
  per side by p24/p25 (`expm1(k·c)/e`, `e = 1000001^(p/1000) − 1`, `c = ln(e + 1)`), normalised to sum 1; partials 4…
  are convolved (left edge padded with partial 4, right with 0); M also adds partials to the count (§1.6).
- **Blur mix** `p20²·10⁻⁶` and **blur mask** (shp 24 sampled per partial index, squared; all-1 → none):
  `T += (blurred − T)·mask·mix` **[EXEC: demos 18/21/55 −69…−80 dB]**.
- The variant selected by misc `+0x46` (part `+0x278`, driven by `+0x250` + art 15) is **not modelled** (demo 15).
- [DOC] Harm: "Harmonic blur amount, equivalent to a vertical smearing of partials."

### 1.16 Protection, clip, auto-gain, volume module **[DECOMP + EXEC]**

- **Protection** (p8): amount `(11^(p8/1024) − 1)·0.1` (1 at p8 = 1024); table = curve shp 5 sampled per partial,
  squared from index 1, trimmed at the trailing zeros; `T_i += (T0_i − T_i)·tab_i·amount` with T0 = the timbre array of
  step 4 **[EXEC: demos 22/24/34/48 −67…−75 dB; synthetic −80.5]**. [DOC] "Prevents filtering functions from removing
  the first few harmonics, that are essential for producing 'weight'".
- **Clip** (level `p9/1024·A21`, mode misc `+0x14`, curve shp 6): per-mode factor {4, 128, 32, 2, 2}; table entries
  `(2y)²·factor` (reciprocal for modes 1/2; mode 0 keeps 1.0 as "no clip"), trimmed; with `q = (level²)²`:
  mode 0 (level < 1) `T = min(T, tab·q)`; mode 1/2 (level > 0) `T −= (1 − min(T·tab/q, 1))^16 (or ^2)·T`;
  mode 3 `T = T if tab·q < T else 0`; mode 4 `T = max(T − tab·q, 0)`; flat curves use one constant **[EXEC: demos
  35/51–55 −69…−85 dB; synthetic modes 0–4 −74…−81 dB]**. [DOC] "Each is a different threshold curve, similar to a
  compression curve".
- **Auto-gain** (p13): after the global EQ, `E = Σ T²` (8 float lanes), `g = clamp(√(1.6428422/E), 2⁻¹², 4096)`
  (4096 when E = 0), volume factor `×= (p13/1000)²·g + 1 − (p13/1000)²` **[EXEC: synthetic −81.6 dB]**.
- **Volume module** (when it is not last): applies the volume factor at its position and passes it to the
  self-oscillation term **[EXEC: demo 12 −78 dB]**.

### 1.17 Output level, pan, master volume **[DECOMP + EXEC]**

- Record gain (fixed for a host block) `= g_part·(Final.Vol + o_vol)·g_uni`, pan `= Final.Pan + o_pan + u_pan + 4·A0`
  (clamped), law `√2·g·cos((1+p)π/4)` / `sin(…)`; dry `g·(1 − |FX|)`, FX path `g·FX` (`FX = p10/1000·A2`); both paths
  through 15 ms one-pole ramps per sample. `g_part = g3(g11/1000)·g3(|p11|/1000)·sign(p11)·k`, `k = 0.5` with one part,
  `clamp(cos(m·π/2))·0.5625` with two (m = g10/1000 for A, 1 − m for B), `g3(x) = (3^x − 1)/2` (1 at x = 1).
- **Master volume** (g12, "post-FX"): stereo gain `g3(g12/1000)` on the plugin output with its own 15 ms ramp from the
  value copied at state load (`plug_all_ipp.c:6744`; the harness observes 1.0 at the first sample) — whether FL's own
  loading sequence starts the ramp from the same value is **not confirmed** **[DECOMP + EXEC: ramp coefficient
  0.993062496 read back; demos 55/63 went from −1/−0 to −69/−76 dB]**.

### 1.18 Stereo tremolo **[DECOMP + EXEC]**

Per part: `g = 0.21 − (0.55·cos(2π·r·i + θ) + 0.55)²`, gain `1 + g·depth`, L phase `θ = 2π·φ`, R phase `θ + 2·gap`,
`gap = frac(p19/1000)·π`, `r = 2^(6 − p18/1000)/fs`, depth p17 ramped (15 ms one-pole, reset when no record sounds),
and every record fades the tremolo in over its first 0.1 s. [DOC] Gap: "When set to 0, the effect will be monophonic
affecting volume only."

### 1.19 Legato, portamento, mono, voice end **[DECOMP + EXEC]**

- Mono modes (global misc `+8`: 1 kill same note, 2 kill all, 3 release), legato (g4, limit g3) hands the sound of the
  closest held note to the new one; portamento (g5) and legato glide the level offsets (pan/vol/pitch/…) by a
  second-order exponential `c = exp(ln 0.01/(t·0.7071))` or linearly (p47), pitch wrap ±1250/1200 cents; porta zeroes
  the pan/vol/velocity offsets — as decoded earlier this round and verified on demos 0, 1, 2, 21, 42, 66, 91.
- **Voice end:** with the volume envelope on, when it has finished (stage > 8); without it, at release, immediately,
  with a 10 ms raised-cosine declick starting at the next host block; kills (mono) use the same declick.
  [DOC] "Harmor the voice ends when the volume envelope has ended."

### 1.20 Harmonic randomness **[DECOMP]**

Curve shp 4 (centre 0.5, `FUN_008895a0`) → trimmed table (+0xA0/+0xA8/+0xAC); per record a uniform draw of `+0xAC`
values from the note seed, `×` the table, exp → a per-partial factor used by module 2. Every demo channel has the flat
default curve (count 0, module absent); **the module itself was not executed** (not confirmed).

### 1.21 Random sources **[DECOMP + EXEC]**

MT19937 (`FUN_007a9c30`, time-seeded at plugin creation); per note `seed = MT >> 1`, consumed in this order by IPP
`RandUniform_Direct_32f/64f` calls: random-unison offsets (n values), per record the random-map values (count
`max map articulator + 1`, only while "new voice" draws are due — global misc `+12`), Hz-unison offsets (520), then
per record `MT >> 1` is the record's own seed (`+0x14C`) and the spectral-phase seed `+0x148` (shared by the records of
a note unless misc `+12` = 3), then harmonic randomness. With the DLL's MT state dumped by the harness the model
reproduces every draw **[EXEC: phases and offsets read back exactly]**.

### 1.22 Image / resynthesis **[DOC + DECOMP structure]**

[DOC] "Pixel brightness is interpreted as partial level." — when a part has image/resynthesis data (part tail > 1 KiB in
the state), the image object (`+0x130`, `FUN_00798xxx`) replaces/mixes the timbre per block and arts 27–30 drive it.
**Not modelled** (8 demo channels carry image data: 3, 13, 14, 29, 74, 75, 77, 87). Channels whose tail only holds a file name (no data) behave like normal parts **[EXEC]**.

### 1.23 Not executed / not modelled (summary)

Image synthesis (§1.22), arpeggio envelopes (§1.12), noise resonance (§1.9), blur mode misc `+0x46` (§1.15), phaser
and harmonizer masks, timbre blend modes ≠ 0, non-neutral global envelope scales, polyphony limit/stealing (plugin
`+0xE0`, read in TriggerVoice, not executed), host release velocity (Voice_ProcessEvent), effects rack (§1.24).

### 1.24 Effects rack **[DECOMP — structure only]**

Distortion (global misc `+48` type), compressor (`+52`), chorus (g32), delay (g39), reverb (g22) run on the plugin
output before the master volume; the DSP was not decoded — **not confirmed**. All verifications switch them off.

### 1.25 Verification against the DLL **[EXEC + model]**

Relative error `20·log₁₀(‖model − DLL‖/‖DLL‖)` over the whole render (both channels), 44.1 kHz, host block 512,
states loaded through the DLL's own SaveRestoreState, effects rack off, the DLL's MT state given to the model.

**Synthetic feature cases** (`r34/out/synth_v1.log`; two overlapping notes with releases on demo channel 92 "Saw" —
uni 9, harmonizer, vibrato, volume/filter/vibrato envelopes — or 36):

| Group | Cases (name: dB) | Worst |
|---|---|---|
| Bases | base92 −80.2; base36 −83.6 | −80.2 |
| Clip modes | clip0_300 −73.9; clip1_300 −80.7; clip2_500 −77.7; clip3_200 −77.6; clip4_200 −79.7 | −73.9 |
| Protection / auto-gain | prot500 −80.5; autog700 −81.6 | −80.5 |
| Blur | tblur_att200_dec500 −79.8; hblur3000 −82.7; hblur3000_shape −83.2; hblur3000_mix400 −81.0 | −79.8 |
| Partials | subs_below −79.4; log_partials −78.5 | −78.5 |
| Prism from level | prism_lvl1 −80.4; prism_lvl2_mul −79.1 | −79.1 |
| Phaser 209/210 | phaser210 −72.7; phaser209 (precision 0) −24.0; phaser209_prec1 −91.0; phaser209_prec2 −92.3 | −24.0 (IPP-limited, see §1.13) |
| Hz unison | uni3_hz −79.0; uni5_hz −80.5 | −79.0 |
| Precision | prec1 −91.7; prec2 −115.9 | −91.7 |
| Master volume / EQ mix | master_vol600 −80.0; master_vol1400 −80.3; eqmix0.5 −79.9 | −80.3 |
| Custom filter / resonance | ftype17 −81.5; ftype18 −81.1; restype110 −82.2 | −81.1 |
| Curve modes (`tcurve3.py`, F1 envelope of 92) | 0 −81.6/−83.0; 1 −81.1; 3 −81.2; 6 −81.8; 10 −81.0; 11 −82.5/−79.7; 12 −81.6 | −79.7 |
| Curve modes (volume envelope of 92, 4 points) | 2 −81.6; 4 −81.8; 5 −81.9; 8 −81.6; 9 −81.8; 10 −81.7; 11 −82.0; 12 −81.9 | −81.6 |

**Demo channels** (`r34/out/bat_v12.log`, table in the appendix): the **99 Harmor channels** of the installation's demo
projects, every parameter as saved except the effects rack, three overlapping notes (C5 vel 1 0–20000, G5 vel 0.6
8000–26000, G4 vel 0.95 16000–30000 samples), 1 s:

- **84 of 99 channels at −60 dB or better** (75 ≤ −70 dB, 43 ≤ −80 dB; median of the measured channels −79.9 dB; best
  −107.0 dB; worst of the passing ones −63.2 dB; channel 67 is silent in both). They cover: both parts and their mix,
  all unison types incl. random and Hz (with the DLL's draws), every curve mode, linear and log partials, subs above
  and below, self-oscillation, prism (both modes and from level), harmonizer, filter/resonance types 0–9/14/17/18,
  100–108/110/111, precision 0/1/2, phaser types 200–210, pluck, time and harmonic blur with mask, protection, clip,
  auto-gain, volume module order, global and per-voice LFOs/envelopes, legato/porta/mono, tremolo, master volume.
- **Not passing (15):** 8 with image/resynthesis data (3, 13, 14, 29, 74, 75, 77, 87 — not modelled), 3 with arpeggio
  envelopes (20 −2.2, 40 −2.2, 46 −1.2 dB), 1 noise resonance (79), 1 blur mode `+0x46` (15), and two measured
  residuals: **28** (−58.2 dB: positive spectral phase randomness multiplies a one-ulp float difference of the
  detuned `f₀/fs` — Delphi's `System.Exp` and Python's `exp` differ in the last bit — by `277·count`) and **78**
  (−55.2 dB: targets, frequencies and phases match to 10⁻¹⁰ at block boundaries, the residual sits around 4 kHz;
  **cause not found**).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| R1 | Engine | per part × unison voice record: 520-position additive bank (self-osc, Sub 1/3/4, 516 harmonics), sine bank with per-sample amplitude glide — same library as Harmless **[DECOMP+EXEC]** |
| R2 | Control rate | B = 4·max(1, round(cr·fs/400 000)) samples (cr = 200 → 88 at 44.1 kHz) **[DECOMP+EXEC]** |
| R3 | Partials | ratios linear `1+(h−1)d` or log `2^((h−1)d/8)`; count from the global-EQ limit, +blur half width, only grows; frequency clamp 2²⁰/fs **[DECOMP+EXEC]** |
| R4 | Pipeline | arts → pitch/count/frequencies → phases → timbre mix → user-ordered chain (EQ, prism, harm. random, harmonizer, filters, phaser, pluck, blur, protection, clip, volume) → subs-below fade → self-osc → top fade → global EQ → volume → auto-gain **[DECOMP+EXEC]** |
| R5 | Phases | phase curve + unison term + spectral random/positive randomness; entering partials at block end **[DECOMP+EXEC]** |
| R6 | Filters | range with table extents (custom shapes: trailing/leading runs), smoothed knob, key track, env/adaptive env, nearest or interpolated read by precision **[DECOMP+EXEC]** |
| R7 | Resonance | `exp(r·ln2·e·mask)` with offset/width laws; noise type not modelled **[DECOMP+EXEC]** |
| R8 | Envelopes/LFOs | exact curve modes 0–12, ADSR time scales, sustain-level offset with [0,1] clamp, global LFO phase without offset; arpeggio points not modelled **[DECOMP+EXEC]** |
| R9 | Unison | per-type tables, random and Hz types, per-partial pitch curve, gain/pan/phase **[DECOMP+EXEC]** |
| R10 | Modules | prism (incl. from level), harmonizer, pluck, phaser 200–210 (209 on frequencies), time/harmonic blur with mask, protection, clip modes 0–4, auto-gain, volume order **[DECOMP+EXEC]** |
| R11 | Output | part gain law, pan law, FX path, 15 ms ramps, tremolo, master volume with ramp **[DECOMP+EXEC]** |
| R12 | Randomness | MT19937 per plugin, per-note seed, IPP uniform draws in a fixed order; reproduced with the dumped MT state **[DECOMP+EXEC]** |
| R13 | Not covered | image/resynthesis, arpeggio envelopes, noise resonance, blur mode `+0x46`, masks of phaser/harmonizer, blend modes, global env scales, polyphony limit, effects rack **[not confirmed]** |
| R14 | Verification | ~45 synthetic feature cases −72…−116 dB (Freq phaser at precision 0: −24 dB, IPP-limited); **84/99 demo channels ≤ −60 dB** (median −79.9) **[EXEC+model]** |

---

## 2. For LYNX

Reference implementation: `r34/py/hmodel.py` + `hart.py`, `henv.py`, `hcurve.py`, `tabs.py`, `hphaser.py`, `hrand.py`,
`hst.py`/`harm.py`/`syt.py`. Call it as

```python
import sys; sys.path.insert(0, '/home/kauan/.claude/jobs/f8789b49/tmp/r34/py')
import hmodel, hst
y = hmodel.render(state_bytes, [(start, key, vel, final_vol, final_pan, release_or_None), ...],
                  fs=44100, N=n_samples, blk=512, mt=None)        # (N, 2) float64
```

(`mt` = the DLL's Mersenne-Twister state for verification only; with `mt=None` the model raises `NotImplementedError`
for features that need random draws — LYNX must supply its own uniform generator there.)

1. **Replace `synthFromHarmor`** (spectral oscillators + biquad approximation in `src/core/FlSytrus.cpp`) by a dedicated
   `HarmorInstrument` (same pattern as `HarmlessInstrument`): parse the whole `0xD5` state (both parts: 386 parameters,
   `blk30`, `misc`, 43 articulators with envelope/LFO/9 maps, 26 curves; the 53 globals and the global misc) and keep
   it in the channel; no conversion to `SynthSettings`.
2. **Reuse the Harmless bank** (`core/Harmless.cpp`) with 520 positions, the Harmor glide coefficient (§1.3) and the
   entering-partial rule applied at the end of the block.
3. **Port the curve object exactly** (`hcurve.py` + `henv.py`): all point modes 0–12, ADSR scales, sustain-level offset
   with the [0,1] clamp, release; it is used by timbres, EQ, every module table, envelopes and LFO amount envelopes.
4. **Control block** of §1.2 per record from its note start; per block run §1.4 in exactly that order; recompute
   frequencies only on the change conditions; table reads nearest (precision 0) or linear (precision ≥ 1). All tables
   are regenerated from closed forms or from the patch curves — nothing is copied from FL (the unison detune factors
   are 3×9 constants of the DLL's data, documented as DATA like round 32's).
5. **Modules** as §1.9–§1.16 in the part's `blk30` order; the three conditional modules (harmonic randomness,
   protection, clip) are present only when their table count is non-zero.
6. **Plugin-wide state:** part vibrato/tremolo/phaser phases, part sample counter (for positive phase randomness),
   global LFO phases (start without offset), master-volume ramp, MT-style random generator; reset on transport stop.
7. **Host levels:** compute `Final.Vol/Pan/Pitch` per note as for Harmless (round 28 §4.4 law), pass velocity/128,
   apply the record gain and pan law of §1.17 and **do not apply the channel volume/pan a second time**.
8. **Random:** FL's draws are time-seeded and cannot be reproduced; use any per-note uniform generator in the same
   order (log "random phases/unison differ from FL by design").
9. **Not covered by this round** (keep a documented fallback and log it): image/resynthesis parts (LYNX can render the
   timbre without the image and log it), arpeggio envelopes (treat arpeggio points as normal points), noise resonance
   (use the normal resonance path or skip), blur mode `+0x46`, phaser/harmonizer masks, blend modes ≠ 0, global
   envelope scales, the effects rack (route to LYNX's own effects), polyphony limit (use LYNX's voice limit).
10. Update `docs/STATUS.md` ("Harmor's per-partial resonance and image synthesis" open item; the Harmor bullet) only
    after the C++ port matches `hmodel.py` on the same states (44.1/48 kHz) to about −60 dB; keep image synthesis,
    arpeggio envelopes, noise resonance and the FX rack listed as open.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| VMT `0x86dbd0` slots 1, 3, 5, 6, 8–11 | plugin interface | DECOMP |
| `FUN_00883cd0` (+ `trig.asm 0x884aee…0x884ca7`) | records per part/unison, unison pan/phase/detune, random draws order (types 3/4, maps, seeds), arpeggio/global-envelope copy | DECOMP + EXEC |
| `FUN_0088ab10` (+ `voice.asm`) | control-block pipeline, pitch, count, phases (`0x88ca39…0x88cd05`), Hz unison, per-partial unison curve, subs-below fade, self-osc, auto-gain (`0x88e7b8…0x88e8a5`), frequency clamp, harmonic-blur kernel (`0x88b770…0x88bbd8`) | DECOMP + EXEC |
| `FUN_008737e0` | module order and presence rules | DECOMP + EXEC |
| `FUN_00886aa0`, `FUN_00885cb0`, `FUN_00886eb0`, `FUN_00887f10`, `FUN_00888500`, `FUN_00886610`, `FUN_00886110`, `FUN_00886160`, `FUN_008860c0` | prism, harmonizer, filter/resonance/noise, phaser (incl. 209), pluck, blur, protection, clip, volume | DECOMP + EXEC |
| `FUN_00882750`, `FUN_00882d10`, `FUN_00882f00`, `FUN_00883060…FUN_00883440`, `FUN_00882680`, `FUN_00882a10` | module kernels (protection, time blur, energy, clip modes, subs below, masked mix) | DECOMP |
| `FUN_00888ed0`, `FUN_00874640`, `FUN_00889470`, `FUN_00889750`, `FUN_00889b30`, `FUN_008895a0`, `FUN_0088a1f0`, `FUN_00889ec0`, `FUN_0088fcf0`, `FUN_00873d10`, `FUN_0088fbc0` | per-part tables, trims, ratios, f_max, count | DECOMP + EXEC |
| `FUN_00874ed0` (`tb.c`), `FUN_00874240`, `FUN_00874c70`, `FUN_00874930`, `FUN_00874860`, `FUN_008747f0`, `FUN_00874b50` | tables, extents, RMS normalisation, nearest/interpolated/wrapped reads | DECOMP + EXEC (table memory) |
| `FUN_00879540` (`pp.c`) cases 8, 9, 0xD, 0x14–0x19, 0x22, 0x27, 0x310 | protection, clip level, auto-gain, blur, unison, master volume laws | DECOMP |
| state load around `plug_all_ipp.c:6820…6866` | misc → part fields (`+0x60`, `+0x668`, `+0x66C`, `+0x100`, `+0x104`, `+0xC0`, subs, `+0xE0`, …, `+0x2C8`, `+0x238`) | DECOMP |
| `FUN_007a07a0`, `FUN_007a0c60`, `FUN_007a09f0`, `FUN_0079fef0`, `FUN_0079e680`, `FUN_0079e5e0`, `FUN_007a1930`, `FUN_007a24b0` | curve/envelope engine, modes 10–12, sustain offset + clamp, arpeggio fields | DECOMP + EXEC |
| `FUN_0060fe20`, `FUN_0060ffe0`, `FUN_00610240`, `FUN_007a34c0`, `FUN_007a33e0` | LFO waveform and start phases | DECOMP + EXEC |
| `FUN_007a4a90`, `FUN_007a4c00`, `FUN_007a4cc0` (Gen_Render `plug_all_ipp.c:23235`) | master volume ramp | DECOMP + EXEC |
| `FUN_007a9c30`, IPP `RandUniform_Direct_32f/64f` | random sources | DECOMP + EXEC |
| Gen_Render `plug_all_ipp.c:23020…23090`, `FUN_00885350` | arpeggio list rebuild after release/kill | DECOMP |
| bank `FUN_00784840`, `FUN_007845a0`, `FUN_00784260`, `FUN_00784490`, `FUN_00783980` | bank render, entering partials, activation | DECOMP |
| runtime globals `0x94e120` (0.0087365862), `0x93b524` (ln 11), `0x93b52c` (ln 3), `0x93b2c8` (ln 0.01), plugin `+0x10D8` (23.777), `+0x10BC` (0.993062496) | constants read from the running DLL | EXEC |
| unison tables `0x928290` | detune factors per type | DATA + EXEC |
| `r34/run/hm.exe` + `r34/py/bat_demo.py`, `synth_bat.py`, `tcurve3.py` | §1.25 | EXEC + model |
| `r14/blobs14.pkl` (99 Harmor demo channels) | demo states | DATA |
| Harmor manual page (URL above) | quotations in §1.2, 1.10, 1.13–1.19, 1.22 | DOC |

### Open questions

- Image/resynthesis synthesis (image object `+0x130`, `FUN_00798xxx`), arpeggio envelopes (first-block behaviour of a
  new record joining a chord), noise resonance (one IPP routine behind a runtime pointer), blur mode `+0x46`, phaser and
  harmonizer masks, blend modes, global envelope scales, harmonic randomness (never active in the data), polyphony limit
  and stealing, effects rack DSP — not decoded or not executed.
- Demo 78's −55 dB residual around 4 kHz: cause not found (targets, frequencies and phases agree at block boundaries).
- Sample-exactness limits: IPP `Exp_32f_A11` (frequency phaser at precision 0) and Delphi `System.Exp` last-ulp
  differences amplified by positive phase randomness (demo 28).
- The master volume's starting value after a preset load in FL (the harness starts from 1.0) and the formula of
  `+0x10D8` (2²⁰/fs inferred from one sample rate).

---

## Appendix — per-channel results of the demo battery (`r34/out/bat_v12.log`)

Error = `20·log₁₀(‖model − DLL‖/‖DLL‖)` in dB (−999 = both silent). Features: `g:` global legato/porta/mono/"new voice"
random mode; per part the non-default modules (uni = unison order/type, harmz = harmonizer, vib = vibrato, trem =
tremolo, phrand = spectral phase randomness, hblur/tblur = blur, prot = protection, clip = clip level, autog = auto-gain,
res1/res2 = resonance, cut1 = filter-1 cutoff, mix12 = filter mix) and the articulators with an active envelope/LFO.

| # | Channel | Error (dB) | Features (part: parameters/articulators in use) |
|---|---|---|---|
| 0 | 4.6 Lead | -82.1 | g:legato=0 porta=1 mono=2 vrand=1; P1 vib=392 trem=132 res1=39 res2=585 cut1=2720 mix12=0 Vol.lfo F1.lfo |
| 1 | 4.5.2 Wasp high | -82.5 | g:legato=1 porta=0 mono=0 vrand=1; P0 vib=460 cut1=4368 F1.env Vib.env; P1 uni3/t1 vib=460 cut1=4368 F1.env Pitch.lfo Vib.env |
| 2 | 4.5.1 Wasp | -81.9 | g:legato=1 porta=0 mono=0 vrand=1; P0 vib=460 cut1=4368 F1.env Vib.env; P1 uni3/t1 vib=460 cut1=4368 F1.env Pitch.lfo Vib.env |
| 3 | 4.4 Glass pluck | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=1; P0 harmz=1000 autog=602 cut1=5968 Vol.env ImS.env |
| 4 | Harmor Bell | -79.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=1000 harmz=909 vib=232 pluck=157 Vol.env; P1 uni2/t1 prism=1000 harmz=909 vib=136 pluck=238 Vol.env |
| 5 | Saw PhaseRN | -78.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni8/t1 harmz=555 vib=500 phrand=-1000 res1=348; P1 uni9/t1 phrand=-1000 res1=282 cut1=6252 |
| 6 | Harmor #4 | -82.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t0 vib=236 cut1=0 F1.env |
| 7 | Brass Synth | -65.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni5/t1 harmz=1000 vib=96 phrand=-1000 Vol.env; P1 uni5/t1 prism=1000 harmz=1000 phrand=-624 Vol.env |
| 8 | Dark Pad | -68.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t1 harmz=558 phrand=-608 cut1=2160 |
| 9 | Harmor #2 | -80.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t1 harmz=582 pluck=195 cut1=2560 Vol.env |
| 10 | Harmor #5 | -63.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni6/t1 prism=1000 harmz=571 trem=216 phaser=864 cut1=5984 |
| 11 | Analog bass 2 | -77.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 harmz=609 autog=616 pluck=643 res1=33 cut1=608 Vol.env F1.env; P1 uni2/t1 harmz=351 autog=556 pluck=643 res1=33 cut1=608 Vol.env F1.env |
| 12 | Dedziiboost | -78.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 harmz=631 vib=64 hblur=4150 phrand=1000 res1=738 cut1=4383 |
| 13 | Vocal | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=185 autog=495 |
| 14 | Pluck | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=185 autog=495 pluck=408 Vol.env |
| 15 | Main Lead #6 | not modelled (blur mode misc+0x46) | g:legato=0 porta=0 mono=0 vrand=1; P0; P1 uni2/t2 prism=4 harmz=1000 hblur=2220 Vol.env |
| 16 | Main Lead #7 | -78.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t2 cut1=1840 Vol.env |
| 17 | Main Lead #7 | -81.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t2 |
| 18 | B - Harmor | -80.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 hblur=1780 pluck=472 |
| 19 | Blips | -63.4 | g:legato=1 porta=1 mono=0 vrand=1; P0 prism=1000 harmz=800 autog=1000 prot=5 phaser=628 selfosc=6 clip=262 res1=504 res2=427 cut1=1625 Pan.lfo Vol.env F1.env F2.env F1w.lfo F2w.lfo Pitch.env |
| 20 | Shine By mehdi | -2.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 autog=1000 prot=720 selfosc=894 phrand=-1000 res1=1000 cut1=4976 Vol.env F1.env F1.lfo Pitch.env; P1 autog=1000 prot=876 pluck=0 res1=1000 cut1=0 Vol.env F1.env F1.lfo Pitch.env |
| 21 | Bajo Melbourne | -76.5 | g:legato=1 porta=1 mono=1 vrand=1; P0 uni3/t0 vib=60 hblur=6494 phrand=-1000 pluck=500 cut1=4096 Vol.env F1.env F1.lfo Blur.env |
| 22 | Basic wobbling | -74.5 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni4/t1 prot=192 res1=747 cut1=3632 F1.lfo |
| 23 | Bajo Bounce | -79.1 | g:legato=1 porta=1 mono=0 vrand=1; P0 uni2/t1 phrand=-812 pluck=427 cut1=4096 Vol.env F1.env |
| 24 | Duck Duck Pluck | -74.6 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni4/t1 prot=192 res1=747 cut1=3632 F1.lfo |
| 25 | Sub Bass | -77.7 | g:legato=0 porta=0 mono=0 vrand=1; P0 phrand=-840 pluck=414 cut1=0 F1.env |
| 26 | Forest | -69.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 prism=1000 harmz=1000 vib=704 phaser=832 phrand=-1000 pluck=181 Vol.env Vib.env |
| 27 | Moving choir | -63.8 | g:legato=0 porta=0 mono=0 vrand=3; P0 uni4/t1 vib=160 phaser=180 res1=63 cut1=2656 mix12=-268 Vol.env F1.env F1.lfo F2.env Vib.env ImT.env; P1 uni3/t1 cut1=4992 Pan.lfo Vol.env F1.env ImT.env |
| 28 | (unnamed) | -58.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni6/t1 harmz=336 autog=840 res2=52 Vol.env F2.env; P1 uni5/t1 harmz=336 vib=220 autog=704 phrand=448 res1=411 res2=52 Vol.env F2.env |
| 29 | (unnamed) | not modelled (image/resynthesis data loaded) | g:legato=1 porta=0 mono=2 vrand=1; P0 phrand=-1000 res1=138 cut1=1504 ImT.lfo |
| 30 | Filthy | -76.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 phaser=777 pluck=505; P1 uni2/t1 pluck=505 res2=189 |
| 31 | Lead | -79.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 phaser=1000 res1=96 cut1=6704 PhW.lfo Pitch.lfo |
| 32 | Lead #2 | -79.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 phaser=1000 res1=96 cut1=6704 PhW.lfo Pitch.lfo |
| 33 | Analog bass 2 | -77.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 harmz=609 autog=616 pluck=643 res1=33 cut1=608 Vol.env F1.env; P1 uni2/t1 harmz=351 autog=556 pluck=643 res1=33 cut1=608 Vol.env F1.env |
| 34 | Spacebass | -71.5 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni4/t1 prot=1024 phaser=918 phrand=1000 cut1=7040 PhOfs.lfo |
| 35 | H 59 Dream of - 4 bar vib | -84.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 clip=0 res1=51 res2=48 Vib.env ImT.lfo |
| 36 | sub bass | -84.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 37 | crushed bass | -81.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 phrand=1000 cut1=3680 Vol.env F1.env |
| 38 | distorted bass | -84.1 | g:legato=0 porta=0 mono=0 vrand=0; P0 uni3/t4 prism=40 prot=1024 res1=63 cut1=5008 UniP.env; P1 cut1=4096 |
| 39 | yaaah bass | -74.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni3/t1 harmz=234 prot=1024 phaser=376 res1=721 res2=156 cut1=2656 F2.env R1w.env PhW.lfo |
| 40 | arp bass | -2.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 harmz=624 vib=12 phaser=1000 res1=87 res2=585 cut1=5680 mix12=0 Vol.env F1.env |
| 41 | doodad | -85.5 | g:legato=0 porta=0 mono=0 vrand=0; P0 uni3/t1 prism=288 Pitch.env UniP.env; P1 uni2/t4 prism=-156 Pitch.env UniP.env |
| 42 | lead synth | -92.7 | g:legato=1 porta=0 mono=0 vrand=1; P0 vib=8 res2=585 mix12=0 Vol.lfo; P1 vib=12 phaser=60 res1=832 cut1=2032 Vol.lfo |
| 43 | saw chords | -97.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni3/t1 harmz=481 cut1=0; P1 uni4/t0 cut1=0 |
| 44 | chord pluck | -79.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni3/t0 phrand=1000 cut1=3680 Vol.env F1.env |
| 45 | bg melody | -87.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 harmz=15 pluck=550 cut1=3216 Vol.env; P1 uni4/t1 pluck=586 cut1=4368 F1.env |
| 46 | arp | -1.2 | g:legato=0 porta=0 mono=0 vrand=1; P1 prism=76 vib=216 trem=1000 phaser=1000 res1=253 mix12=0 Vol.env |
| 47 | chord top layer | -78.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 pluck=397 cut1=6016 |
| 48 | drop pad | -67.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t1 prism=-1000 vib=87 hblur=4140 prot=588 clip=572 res1=510 cut1=6656 Vol.env F1.env F1.lfo F2.env Prism.env UniP.env; P1 uni6/t1 prism=-788 vib=87 hblur=3420 prot=500 clip=588 res1=528 res2=415 cut1=6656 Vol.env F1.env F2.env Prism.env UniP.env |
| 49 | pad | -77.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni4/t1 prism=748 harmz=345 vib=132 phaser=495 res1=294 res2=174 cut1=3248 mix12=-500 Vol.env F1.lfo F2.lfo |
| 50 | pling riser | -84.1 | g:legato=0 porta=0 mono=0 vrand=1; P0 pluck=388 cut1=2156 |
| 51 | HW Last night on earth | -83.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=56 clip=0 res1=51 res2=48 Vib.env ImT.lfo |
| 52 | HX Special Sustained Ahh | -75.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t3 clip=0 res1=51 res2=48 cut1=0 Vol.env Vib.env |
| 53 | HW Beam me up | -83.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=56 clip=0 res1=51 res2=48 Vib.env ImT.lfo |
| 54 | HW See the stars | -83.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=56 clip=0 res1=51 res2=48 Vib.env ImT.lfo |
| 55 | HX Special Robot | -69.0 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 hblur=4958 phrand=-1000 clip=0 res1=51 res2=51 cut1=862 Vib.env ImT.lfo |
| 56 | H Tevlo Lyric Embrace the cold | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 57 | H Tevlo Lyric See the cracks | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 58 | H Tevlo Lyric Stay with me | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 59 | H Tevlo Lyric Take me away | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 60 | H Tevlo Lyric Im loosing oxygen | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 61 | H Tevlo Woah 01 | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 62 | lead synth | -87.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=800 trem=236 pluck=358 res1=25 Vib.env; P1 harmz=219 vib=788 trem=68 phaser=9 pluck=358 res1=25 Pitch.lfo |
| 63 | supporting lead synth | -75.7 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 trem=500 phaser=1000 res1=96 cut1=6704 PhW.lfo Pitch.lfo |
| 64 | sine | -80.4 | g:legato=0 porta=1 mono=0 vrand=1; P0 pluck=22 Pan.lfo |
| 65 | saw pluck | -79.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni5/t1 vib=104 phrand=1000 pluck=900 res1=51 cut1=4096 Vol.env F1.env; P1 uni3/t1 vib=104 phrand=1000 pluck=897 res1=51 cut1=4096 Vol.env F1.env |
| 66 | wobble chords | -79.1 | g:legato=0 porta=1 mono=0 vrand=1; P0 uni3/t1 harmz=201 vib=100 phrand=1000 cut1=1040 Pan.lfo Vol.lfo F1.env F1.lfo |
| 67 | offbeat pluck | -999.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 vib=144 res1=82 res2=231 cut1=2640 Pan.lfo F1.env |
| 68 | pad | -81.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni7/t1 harmz=360 vib=220 tblurA=12 autog=524 cut1=3808 Vol.env F1.env Vib.env |
| 69 | saw chords | -81.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni5/t1 vib=192 phrand=1000 res1=51 Vol.env F1.env; P1 uni3/t1 vib=192 phrand=1000 res1=51 Vol.env F1.env |
| 70 | sub wobble 1/16 | -84.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 Vol.lfo |
| 71 | sub wobble 1/8 | -85.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 Vol.lfo |
| 72 | sub | -84.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 |
| 73 | grittier bass | -83.7 | g:legato=1 porta=1 mono=0 vrand=1; P0 uni9/t3 harmz=1000 cut1=6224 mix12=0; P1 uni9/t1 |
| 74 | robot bass | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 vib=552 |
| 75 | evil plucked bass | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t4 vib=564 phaser=397 res1=537 cut1=4928 mix12=432 Vol.lfo F1.lfo |
| 76 | growl bass | -67.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni3/t2 harmz=403 vib=580 trem=616 phaser=247 phrand=-504 res1=757 cut1=6096 Vol.env Vol.lfo R1w.env Vib.env |
| 77 | talking bass | not modelled (image/resynthesis data loaded) | g:legato=1 porta=0 mono=2 vrand=1; P0 phrand=-1000 res1=138 cut1=1504 ImT.lfo |
| 78 | tonal riser | -55.2 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni5/t1 vib=620 trem=1000 phaser=1000 cut1=0 |
| 79 | Bass | not modelled (noise resonance) | g:legato=1 porta=0 mono=0 vrand=1; P0 uni7/t4 phaser=934 phrand=1000 res1=132 res2=95 Vol.env |
| 80 | Harmor Bass | -107.0 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t3 prism=44 phaser=303 |
| 81 | Step 1 | -103.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=-152 phaser=516 |
| 82 | Step 2 | -99.9 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t0 prism=136 |
| 83 | Emblem | -87.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni8/t1 prism=-88 trem=272 cut1=4464 Vol.env F1.env; P1 res1=321 cut1=3792 Vol.env Vol.lfo TMix.lfo F1.lfo Pitch.env |
| 84 | Arp | -88.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t4 prism=68 vib=104 phaser=486 pluck=757 res1=141 cut1=16 |
| 85 | Lead | -87.3 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni3/t0 harmz=276 phaser=114 pluck=658 res1=135 cut1=0 |
| 86 | Riser | -99.6 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t0 phaser=945 res1=174 cut1=2160 |
| 87 | Piano | not modelled (image/resynthesis data loaded) | g:legato=0 porta=0 mono=0 vrand=0; P0 uni3/t0 vib=208 autog=384 phaser=774 pluck=0 clip=396 res1=117 cut1=4832 Vol.env Pluck.env |
| 88 | Punchy Buzz | -77.8 | g:legato=1 porta=0 mono=0 vrand=1; P0 harmz=492 vib=280 res2=585 cut1=2624 mix12=0 F1.env Pitch.env; P1 harmz=1000 vib=280 res1=61 Pitch.lfo |
| 89 | Pad | -80.4 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t1 harmz=565 res1=188 cut1=6058 Vol.env Vib.env |
| 90 | Sub | -75.6 | g:legato=1 porta=0 mono=0 vrand=1; P0 cut1=1598 |
| 91 | Baas | -74.7 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 vib=92 cut1=2709 F1.lfo PhOfs.env Vib.lfo |
| 92 | Saw | -80.5 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni9/t1 harmz=212 vib=445 res1=128 cut1=4096 Vol.env F1.env Vib.env |
| 93 | Quoa | -70.5 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=65 phaser=1000 PhOfs.env Pitch.env; P1 phaser=1000 PhOfs.env Pitch.env |
| 94 | Quoa #2 | -74.6 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=65 vib=419 F2.env PhOfs.env Pitch.env; P1 vib=419 PhOfs.env Pitch.env |
| 95 | Quoa #3 | -77.5 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=65 vib=723 F2.env PhOfs.env Pitch.env; P1 vib=723 PhOfs.env Pitch.env |
| 96 | Quoa #4 | -79.2 | g:legato=1 porta=0 mono=0 vrand=1; P0 uni2/t1 prism=65 vib=254 F2.env PhOfs.env Pitch.env; P1 vib=254 PhOfs.env Pitch.env |
| 97 | Pluck | -78.8 | g:legato=0 porta=0 mono=0 vrand=1; P0 uni2/t1 harmz=1000 res1=204 cut1=7332 F1.env Pitch.env |
| 98 | Sat | -78.3 | g:legato=1 porta=0 mono=0 vrand=1; P0 Pitch.env |
