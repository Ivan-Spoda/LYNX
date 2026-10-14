# Behavioural analysis of FL Studio 2026: round 31 (REA) — Sytrus: the complete voice (operators, matrix, articulators, filters, effects, output)

> **Nature of this document.** Continuation of `REA_rodada11.md` (Sytrus state format), `REA_rodada18.md` §2 (FM/RM
> units), `REA_rodada23.md` §2 (operator waveform) and `REA_rodada28.md` §4 (host per-voice levels). *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched (no FLEX pack was opened) and no Image-Line preset or audio data was
> copied into the repository or this document (the demo channels and presets were only read in place).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical models (which
> implement only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[STR]** | names/hints read in the plugin's own form resources (DFM), used only to name controls |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Tools:** Ghidra 12.1.4 headless (JDK 21) on a **new** project `/tmp/r31gh_sy` (automatic analysis, full
  decompilation `r31/out/sy_all.c`; extracts `r31/out/trig.c`, `vinit.c`, `vblk.c`, `loop.c`, `setup.c`), a new project
  `/tmp/r31gh_rev` for the shared reverb DLL (`r31/out/rev_all.c`), `objdump` for the x87/SSE kernels, constants with
  `r18/k.py`; our own helpers `r31/py/fn.py` (function extractor), `r31/py/fptr.py` (resolves the plugin's run-time
  function-pointer slots by asking the running DLL), `r31/py/ipp.py` and `r31/out/ippmap.txt` (IPP names from the
  dispatcher `FUN_005bc580`). The DFM form `r11/sytrus.dfm.txt` (round 11) was used only for control names **[STR]**.
- **Work split:** the articulator, filter and post-voice FX parts were analysed in parallel by three sub-analyses
  (same rules, same DLL); their results are merged here as §4, §5 and §7, with the corrections found when everything was
  assembled into one voice model (marked "integration").

- **Targets** (hashed in this round):

  | Module | File (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Sytrus | `Plugins/Fruity/Generators/Sytrus/Sytrus_x64.dll` `d52a97c55e36c2457305ce4f2d6eed08aabd12dbec02c9f5f55ce8788e944f15` (= rounds 11/18/23) | Delphi 12 x64 (`TTestPlug`, VMT `0x860da0`) | VMT slots: 1 Dispatcher `FUN_00861360`, 3 SaveRestoreState `FUN_00862d70`, 5 ProcessEvent `FUN_00867890` (→ base `FUN_007797a0`), 6 ProcessParam `FUN_00867930`, **8 Gen_Render `FUN_008737a0`**, **9 TriggerVoice `FUN_0086c7d0`**, **10 Voice_Release `FUN_0086d5a0`**, 11 Voice_Kill `FUN_0086d870`; voice init `FUN_0086af20`; routing `FUN_0086dd40`; **per-voice block `FUN_00872290`**; **render loop `FUN_00870820`**; **per-sample operator kernel `FUN_00870580`** (x87; AVX twin `FUN_00877270` selected by CPU flag, `FUN_00877760`); block operator kernel `FUN_00870760` + phase lanes `FUN_00860f10`; operator frequency `FUN_00872210`; table/band-limit `FUN_0086ace0` + `FUN_008471d0`; pluck `FUN_0086a810`/`FUN_0086aba0`; matrix cell/curve `FUN_008650d0`/`FUN_00864ff0`, columns `FUN_00865120`, unit activity `FUN_008652a0`; unit parameters `FUN_0086a730` (op), `FUN_00869e30`, `FUN_00869e80` (filter), master `FUN_0086a1d0`, declick amount `FUN_0086a630`; tick/rate `FUN_008657b0`, `FUN_008629d0`, `FUN_00862b80`; kill `FUN_0086d770`; Delphi RTL: `FUN_0040ad30` exp, `FUN_0040c090` ln, `FUN_0040c510` sqrt, `FUN_0040c520` Round (banker's), `FUN_0040c610` Trunc, `FUN_0040acf0` Frac, `FUN_0040a030` Random (RandSeed at `0x8a2010`), SinCos `FUN_005fcbb0`; DSP table `FUN_0062e5f0`… (pan law `FUN_0062bc50`, ramps `FUN_0062bb20`/`FUN_0062bbf0`, stereo add `FUN_00629740`/`FUN_00629870`, ramped multiply `FUN_00629f70`, DC filters `FUN_00628e60`/`FUN_00628dd0`) |
  | Shared reverb | `FL Studio 2026/Shared/Reverb_x64.dll` `9881ff2ccd74227e454a3d5f1912568f4336abe06e68f3693162aa4d251c969f` | C++ (MSVC, `BaseReverb`) | process `FUN_180002980` (vtable `0x1800603c0` slot 2), parameter block slot 3, reset slot 5 |
  | IPP | `FL Studio 2026/Shared/dsp_ippv2_x64.dll` `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` (FL's copy of Intel IPP, loaded by Sytrus) | Intel IPP | standard primitives: `ippsTone_Direct_32f`, `ippsMulC_32f`, `ippsAddProductC_32f`, `ippsZero_32f`, `ippsVectorSlope_32f`, real FFT pack routines; `ippsRandUniform_Direct_32f` (CPU dispatcher at VA `0x1815b3fa0`, generic path `0x1815ed2e0`) analysed for the pluck noise (§2.6) |

- **Execution harness** (our own C, Zig 0.14.1 → Windows x64): `r31/run/syt.c` → `syt.exe` (wrapper `r31/run/r.sh`,
  Python driver `r31/py/runsyt.py`). It calls `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|
  SEM_NOOPENFILEERRORBOX)` first, installs a vectored exception handler that logs Delphi exceptions and ends the process
  on faults, loads the installation's `Sytrus_x64.dll` unmodified from `C:\Program Files\Image-Line\FL Studio 2026`,
  creates the plugin with its exported `CreatePlugInstance` and a minimal fake host, and drives it only through its own
  VMT: Dispatcher (`FPD_SetSampleRate` 4, `FPD_SetBlockSize` 3, optional `FPD_ProcessMode` 1 with `PM_IsRendering`),
  SaveRestoreState with a minimal read-only COM `IStream` over a state blob, ProcessEvent (`FPE_Tempo`), TriggerVoice
  with an SDK `TVoiceParams` record (float levels), Voice_Release, Gen_Render. Host pieces: dispatcher query `0x47`
  returns a dummy interface whose method 6 answers the plugin's "data file" request `0x3fc` with the installation path of
  `Plugins/Fruity/Generators/Sytrus/Data/Default.dat` (Sytrus reads it at construction; it is then replaced by the
  loaded state); host slot 6 (voice events) answers event 2 (note colour), 3 (velocity, float bits) and 5 (release
  value, float bits) per note; every other host method returns 0. Read-only diagnostics: `peek=`/`ppeek=`/`dpeek=`
  (module/plugin memory), `fptr=` (run-time function-pointer slots), `dumptab=u` (operator wavetable of the first voice),
  `dumpvoices` (voice fields), and the value of the DLL's Delphi `RandSeed` just before each TriggerVoice. It always
  ends with `TerminateProcess` (no DLL finalisation), runs under `timeout 120`, and no process or dialog was left behind.
  Copies with small additions used by the sub-analyses: `syt_env.c`, `syt_flt.c`, `syt_fx.c` (same rules).
- **Models (pure Python + numpy, `python3 -I`):**
  - `r31/py/sytmodel.py` — **the whole instance**: state → voices (operators, matrix, unison, portamento, voice end),
    articulator provider `FullArts`, filters, output buses, post-voice chain; entry `render(S, notes, fs, n, blk,
    render_mode, bpm, arts_cls=FullArts)`.
  - `r31/py/art_model.py` (articulators), `r31/py/filt_model.py` (filter units), `r31/py/fx_model.py` (post-voice chain),
    `r31/py/sywave_np.py` (operator wavetable, numpy port of round 23's model), `r31/py/sytstate.py` (state parser +
    serializer, byte-exact round trip on all 268 demo blobs).
  - Batteries: `r31/py/battery_core.sh` → `battery_core.out` (synthetic voice-core checks), `test_art.py` →
    `test_art.out`, `test_filt.py` → `test_filt.out` (+ `t_dyn2.out`, `t_ws.out`, `t_chain.out`), `test_fx.py` →
    `test_fx.out`, **`battery_demo.py` → `battery_demo_all.out` (all 268 demo channels, whole instance)**,
    `battery_render.py` → `battery_render.out` (FL-render settings), `ipprand.py` (generator check).
- **Dataset:** `r14/blobs14.pkl` (268 Sytrus channels of the demo projects; state versions v22 = 78, v23 = 9,
  v24 = 132, v25 = 49) and the factory presets `Data/Patches/Plugin presets/Generators/Sytrus` (read in place).

Manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Sytrus.htm`
(abbreviated `…/plugins/Sytrus.htm`); quotes are short.

**Corrections to earlier rounds (details in the sections):**
- Round 11 §1.6: envelope time unit is **0.5 s per unit** (1 beat with TEMPO), not 1 s; the articulator's first float
  is the **base value**, not flags; the LFO period law is in beats only with TEMPO (§4).
- Round 11 §1.2: the three delay records end with bytes **tempo, serial, on** (§7.4).
- Round 11 §1.7: Chocolate SVF cutoff law replaced (§5.4).
- Round 11 §1.4: operator block `+0x008` is the **Noise** amount; flag bytes 3–7 are **GLOBAL (free-running
  phase), CENTER (DC removal), DECLICK, PLUCK, BAND LIMIT** (§2) **[DECOMP + EXEC + STR]**.
- Round 18 §2: confirmed (FM `4·g` cycles of the target table, self-feedback `g·35/128`), refined by the exact
  per-sample order and the MOD ramp (§3); RM uses the *previous* sample's MOD level in one term (§3.2).
- Round 23 §2: confirmed (16 384-point table, peak normalisation, etc.); additionally the run-time table is
  **linearly interpolated**, **band-limited copies are 4 096 points live / 16 384 when FL renders** (§2.4), and the
  sine path comes from IPP's tone generator, whose values differ from an exact sine by ~10⁻⁸ (§2.3, §8.3).

---

## 1. Engine overview and timing **[DECOMP + EXEC]**

### 1.1 Objects

- **Plugin** (`P`): 10 *units* in records of 0x900 bytes at `P+0x518 + u·0x900` (u = 0…5 operators, 6…8 filters,
  9 effects unit). A unit record holds the 10 integer parameters at `+0x00`, the converted floats (`+0x28` PAN cell,
  `+0x2c` FX gain, `+0x30` OUT gain), the articulator array at `+0x38`, the operator block of the state at `+0x40`
  (0x828 bytes for operators), and converted values from `+0x868` on (`+0x86c` pitch range, `+0x870` volume,
  `+0x878` wave object, `+0x884/+0x888` cutoff/resonance, …, `+0x8cc` start phase, `+0x8d0` frequency offset,
  `+0x8d8` ratio factor).
- **Voice**: an object of 0x6728 bytes per sounding voice (`FUN_00416280(0x6728)` in TriggerVoice); per unit a
  sub-record of 0xA38 bytes at `voice + 0x88 + u·0xA38` (`U` below). One FL note creates `n_unison` voices (×2 with the
  unison "sub" layer), linked in a chain; all voices sit in the list `P+0x62c0`.
- **Per-voice routing lists** built at note-on (`FUN_0086dd40`): operators that need the per-sample kernel (FM/RM
  sources or targets, noise, pluck), the other operators, filter inputs, OUT list, FX list, DC list, declick list.

### 1.2 Rates, control tick and sub-blocks

- **Voice rate** `fs_v = fs·K`, `K = 2^order`, order = main-block int at `0x30C` live and `0x310` while FL renders
  (dispatcher `FPD_ProcessMode` bit `0x10`, `P+0x6336`) (`FUN_00862b80`, `FUN_008629d0`). The voices, the articulators
  and all rate-dependent constants below use `fs_v`; the post-voice chain decimates (§7.2). **[DECOMP + EXEC]**
- **Control tick** `T = 4·round(T₀·fs_v/44100/4)` (minimum 4) samples, `T₀ = {96, 8}[i]`, i = main-block int
  `0x304` live / `0x308` when rendering (table at `0x8fc9c4`; DFM "Extra high envelope accuracy (draft)" /
  "(rendering)" **[STR]**) (`FUN_008657b0`). EXEC: `T` = 48/96/104/208/416 at 22.05/44.1/48/96/192 kHz (i = 0);
  8 at 44.1 kHz and 64 at 8× oversampling (i = 1).
- **In the demo channels** the pair (live, render) of the tick index is (0, 1) in 163, (0, 0) in 55 and (1, 1) in 50
  of 268, and the oversampling pair is (0, 3) in 147, (0, 0) in 56, (1, 3) in 23, (2, 3)/(3, 3) in 13 each **[DATA]**:
  *when FL renders a project, most Sytrus patches run 8× oversampled with an 8-sample tick*, while live playback mostly
  uses no oversampling and a 96-sample tick.
- Each voice counts ticks from its own start (voice `+0x14`, initialised to `−T`). `Gen_Render` (`FUN_008737a0`)
  calls the per-voice function (`FUN_00872290`) for every voice of the list, last to first; the voice function cuts
  the host block at its tick boundaries and, **at each tick start**, evaluates all articulators (§4) and computes the
  per-tick targets (operator volume and MOD ramps, pan/gain targets, pitch factor, filter targets); then it runs the
  render loop (`FUN_00870820`) for the **sub-block** = (rest of the tick) ∩ (rest of the host block). Ramps that are
  defined "over the tick" use `T` even when the tick is split; ramps "over the sub-block" use the sub-block length.
- The articulator advance of the first evaluation is 0, later ones 1 tick.

### 1.3 What the host passes **[DECOMP + EXEC]**

| Host input | Use in Sytrus |
|---|---|
| `Final.Pitch` (cents, 0 = C5) | voice pitch `p = Final.Pitch + 100·MasterPitch` (main int 2), re-read at every Gen_Render; key for the maps `key = p/100 + 60` (≥ 0) |
| `Final.Vol` | output gain factor (× master level), per Gen_Render call |
| `Final.Pan` | added as `Final.Pan/2` to the pan position (§6.1) |
| `Final.FCut`, `Final.FRes` | Mod X / Mod Y map position offsets (§4) |
| voice event 3 (velocity, float) | velocity map, velocity options (§4.7), declick amount (§2.7) |
| voice event 5 (float) | release-knob multiplier when byte `P+0x4FE` is set (§4.7) |
| voice event 2 (note colour) | colour 14 inverts the PORTA button for that note (§6.4); colour 15 offsets the voice's stored `round(Init.Pitch)` by −100000 (its use was not traced) |
| `FPE_Tempo` (ProcessEvent 0) | tempo for TEMPO envelopes/LFOs, delays, reverb pre-delay |
| `FPE_MaxPoly` (ProcessEvent 1, base class `FUN_007797a0`) | `P+0xe0`: maximum number of note groups (§6.4) |
| `FPD_Flush` (Dispatcher 2) | clears the global sample counter, effect rings, decimators, DC stage, last-pitch memory |

Sytrus sends voice event 6 with value 0 at every TriggerVoice **unless** byte `P+0x4FC` is set (`FUN_0086c7d0`);
round 28 §4.1 describes that event as the host's "link velocity" switch (host side: round 28, not re-read here).
`Init.Vol`/`Init.Pitch` are only used for the voice key bookkeeping (`+0x24 = round(Init.Pitch)`).

---

## 2. Operators

### 2.1 Frequency **[DECOMP `FUN_00872210`, `FUN_0086a730`, `FUN_008629d0`, `FUN_0086c7d0` + EXEC]**

Per operator, in cycles per voice-rate sample (single-precision results, double intermediates):

```
base   = 523.251130601197 / fs_v                                (float; P+0xE4)
kR     = float( float(ratio_int · base) / 20000 )               (unit +0x8D8, ratio_int = block +0x000)
kO     = offset_int / float(fs_v · 100)                         (unit +0x8D0, offset_int = block +0x004, Hz·100)
f      = max(0, float( exp(p · ln2/1200) · kR · det + kO ))      (U+0x40; p = voice pitch in cents, det = unison factor)
f_eff  = float(f · P_art)                                       (U+0x368)
P_art  = float(exp(out₃ · 2·p2 · ln2/1200))                     (U+0x48; out₃ = pitch articulator, p2 = unit p[2] cents)
inc    = min(float((f_eff + Δφ_art) · N), N/2)                  (table samples per sample; N = table length)
```

- `f` is recomputed whenever the voice pitch changes (once per Gen_Render call), `P_art` once per tick (no ramp),
  `inc` at every sub-block. `Δφ_art` is the phase articulator's per-sample increment (§2.2).
- EXEC: C5 (`Final.Pitch` 0) at ratio 1 sounds 523.25 Hz; A5 880 Hz; ratio 1.5, offset 123.45 Hz, ratio 0 + offset
  440 Hz at 96 kHz, master pitch −12 — all sample-exact (≤ −111 dB, `t_core0`, `t_core1`). The division by 20000 is in
  single precision (a double division gave 1-ulp frequency errors visible after a few thousand samples).

### 2.2 Phase **[DECOMP `FUN_0086af20`, `FUN_00870820`, `FUN_00872290` + EXEC]**

- Initial phase in cycles: `φ₀ = p[0]/256 + φ_unison + out₄`, plus `counter·f` when the operator's **GLOBAL**
  flag (block byte `0x823`, DFM "Global / retriggered phase") is set, where `counter` is the plugin's running sample
  count (`P+0x62b8`, advanced by every Gen_Render, cleared by `FPD_Flush`); then `φ₀ += 64 − round(φ₀)`. When the
  table is attached (first render), `φ ← φ·N`.
- Phase wrap at every sub-block: operators of the per-sample group `φ += (64 − round(φ/N))·N`, block operators
  `φ += (1 − round(φ/N))·N`. (So the per-sample phase stays near 64·N; the double precision phase makes that harmless.)
- **Phase articulator** (art 4, additive): the change of its output between ticks is spread as a frequency offset:
  `Δφ_art = (out₄(new) − out₄(old))/T` cycles per sample during the next tick.

### 2.3 Wavetable and read-out **[DECOMP `FUN_00870580`, `FUN_00870760`, `FUN_00860f10`, `FUN_008471d0`, `FUN_0086ace0` + EXEC]**

- The wave of each operator is the 16 384-point table of round 23 §2 (base shape, harmonics, Half/Even, Skew, Sine
  shaper, peak normalisation, Tension, Absolute, Pre-filter, Gibbs window) plus one guard sample (`table[N] = table[0]`).
  EXEC: dumping the DLL's own table (`dumptab`) and comparing with our model (`sywave_np.py`): identical for shapes
  computed by the generic path (saw, triangle …); for pure sines, which the DLL builds with `ippsTone_Direct_32f`, the
  values differ by up to 6·10⁻⁸ (about 1 ulp; e.g. 4 322 of 16 385 entries) — §8.3 explains why that matters only for
  chaotic FM.
- **Read-out = linear interpolation**: `i = ⌊x⌋` (implemented as round-to-nearest of `x − 0.5`), `w = t[i&(N−1)] +
  (t[i+1] − t[i])·(x − i)`.
- **Block path** (operators that neither modulate nor are modulated, have no noise and are not plucked): SSE code
  with 4 lanes: lane k starts at `float(φ) + float(k/4·4·inc)` and every lane adds `4·inc` (float) per group of 4
  samples; index = truncation, fraction = `x − trunc(x)`; then `φ += n·inc` in double. Output × volume ramp.
- **Per-sample path** (`FUN_00870580`, x87): per sample and per operator **in index order 1…6**:
  `x = φ + A_fm·MOD` (A_fm = the operator's FM accumulator, MOD = its input-modulation level), `φ += inc` (double),
  `A_fm = 0`, wave read as above in extended precision, optional noise (§2.5), RM factor (§3.2), volume, store, then
  FM/RM contributions to the targets (§3.1). Intermediates stay in the x87 registers; only stores round to float.
  EXEC: matched with double-precision intermediates (≤ −134 dB, `t_core1`).

### 2.4 Band limiting **[DECOMP `FUN_0086ace0`, `FUN_008471d0`, `FUN_00848980`, `FUN_00848a50` + EXEC]**

When the operator's **BAND LIMIT** flag (block byte `0x827`, DFM "Band-limit waveform") is set and PLUCK is off,
the operator reads a band-limited copy of its wave, chosen when its table is attached (first render of the voice):

```
F    = max(0.1, f_eff·fs_v) Hz          ν = 22050 if GIBBS OFF (P+0x4F6) else fs_v/2
d    = (ν − 18000)/4
lo   = trunc((18000 + d)/F) + 1          hi = trunc((18000 + 3d)/F) + 1
M    = 4096 live, 16384 when FL renders (FFT orders 12/14, P+0x6336 copied to unit +0x869)
if lo > M/2: use the full 16 384 table   else: hi = min(hi, M/2) and
   gain(k) = 1 (k < lo);  1 − (k−lo+1)/(hi−lo+1) (lo ≤ k ≤ hi);  0 above
   with GIBBS OFF also ×(hi/π)·sin(kπ/hi)/k for k = 1…hi
   table = inverse real FFT (M points) of the full table's spectrum (harmonics 0…M/2) × gain
```

Tables are cached per (lo, hi). The operator then uses `N = M`. EXEC: saw/square at keys 36…100, 44.1/48 kHz, with and
without GIBBS OFF: −137…−139 dB (`t_core2`). Before the M = 4096 detail was found, the 16 384 assumption still gave
−75…−120 dB (the interpolation error of the shorter table is small); the per-sample FM channels exposed it.

### 2.5 Noise **[DECOMP `FUN_0086af20`, `FUN_00870580` + EXEC]**

Block byte `+0x008` = **Noise** (DFM `ShapeNoiseWheel`) when PLUCK is off: `n = g(v/128)` (`g(x) = (11^x − 1)/10`,
exactly 1 at x = 1). The operator becomes per-sample and its wave sample is replaced by
`w·(1 − n) + n·√(fs_v/44100)·(2·s/2³² − 1)`, `s` = a 32-bit LCG `s ← s·0x08088405 + 1` that is **one global variable
of the DLL** (`0x911c30`, 0 at load) shared by all noise operators of all voices and instances in the process.
EXEC: −142…−184 dB (`t_core1`). In the demos noise = 128 (full noise) in 107 of 617 active operators **[DATA]**.

### 2.6 Plucked mode **[DECOMP `FUN_0086a810`, `FUN_0086aba0`, `FUN_00870580` + EXEC]**

Block byte `0x826` = **PLUCK** (DFM "Plucked mode"): the operator becomes a recirculating delay line:

- Length `L = max(2, round(88200/(f·fs_v)))` entries (two entries per period at 44.1 kHz, independent of the rate).
- Excitation at note-on: one period of the operator's full wave, read with a 14.18 fixed-point phase starting at
  `round(φ₀·2³²)` and step `round(2³²/L′)` with linear interpolation, optionally mixed with uniform noise
  (`ippsRandUniform_Direct_32f`, seed `round(φ₀·2³²)`): `x·(1 − n) + r·1.5n` (n from the Noise field); then the mean is
  subtracted.
- Damping from the "plucked damp" articulator (art 5, clamped 0…1 at note-on): `a = 0.99·out₅ + 0.01`, `b = 1 − a`.
  Effective length `L′ = L·(1 + b·sin(1/L)/(1 − b·cos(1/L)))`; read speed `L′·f_eff` entries per sample as a
  32.32 fixed-point increment.
- Per sample: advance the read pointer by the integer part of the accumulated increment; each entry passed is
  replaced by `y = a·v + b·y_prev` (one-pole low-pass, in place); output = linear interpolation between the last two
  *unfiltered* values passed, by the fractional accumulator. Coefficients are recomputed when `f_eff` or the damp
  articulator changes.
- The noise uses `ippsRandUniform_Direct_32f` of FL's `Shared/dsp_ippv2_x64.dll` (SHA-256 `4dbb9e95…e551e0d4`),
  whose generator we described from its behaviour and checked against direct calls of the DLL's own function
  (`ipprand.py`, EXEC: outputs and returned seeds identical): a 32-bit LCG `L ← 69069·L + 1013904243` restarted at
  `0x436CBAE9` on every call, combined with a lag-2 subtract-with-borrow sequence (modulus 2³² − 18) seeded from the
  seed and two LCG steps of it; value = `(lo+hi)/2 + (hi−lo)·2⁻³²·int32(s + L)`.
- EXEC: saw/sine excitations, 44.1/48 kHz, with damp envelope, with FM on top: −133…−182 dB (`t_core3`, `t_full1`);
  demo channels with noise-excited plucks −107…−148 dB (§8.2).

### 2.7 CENTER, DECLICK and the global declick amount **[DECOMP `FUN_00870820`, `FUN_0086af20`, `FUN_0086a630` + EXEC]**

Applied to the operator's output buffer after rendering (before filters and output), only for the master voice of a
grouped unison (§6.3):
- **CENTER** (block byte `0x824`, DFM "DC offset removal"): `v += (x − s)·a; s += v; v ·= 0.96; y = x − s`,
  `a = 2.2835·10⁻⁵·44100/fs_v` (states in extended precision).
- **Attack declick**: a one-pole smoother whose coefficient rises linearly from 0 to 1 over `D` samples
  (`y += c·(x − y)`, `c += 1/D`), `D = round(max(0, (0.775 − vel)·A·S) + [DECLICK]·S)` with
  `S = 96·(fs_v/44100)²`, `A = round((101^(p/128) − 1)·10)`, `p` = the global int after the main block (state
  `P+0x4F0`), and `[DECLICK]` = block byte `0x825`. D < 1 → off.
- EXEC: −148…−184 dB (`t_core4`).

---

## 3. Modulation matrix

The state matrix `M[layer][row][col]` (layer 0 FM, 1 RM; **row = destination, column = source**; muted cells read as
0) uses the level curve `g(v) = sign(v)·(11^(|v|/256) − 1)/10` (exactly ±1 at ±256) (`FUN_00864ff0`)
**[DECOMP + EXEC]**.

### 3.1 FM, feedback and the per-sample order **[DECOMP `FUN_0086dd40`, `FUN_0086ace0`, `FUN_00870580` + EXEC]**

- Source j adds into target i's accumulator, after j's output (including its volume) is computed:
  `A_fm(i) += a·out_j` with `a = 4·g·N_i` (j ≠ i) or `a = g·35/128·N_i` (j = i, feedback), N_i = target table length.
  The target reads `x = φ + A_fm·MOD_i`, i.e. **phase modulation of `4·g·out·MOD` cycles** (round 18 confirmed).
- Order: operators of the per-sample group are processed in index order within each sample. Hence a modulator with a
  **lower** index acts on the **same** sample, one with a higher or equal index on the **next** sample.
- EXEC (`t_core1`): 2→1, 1→2, feedback 200/256, three-operator chain, two-way FM (1⇄2), RM, noise + FM: ≤ −134 dB
  when the DLL's own tables are used; with our own sine table the two-way and the strongest feedback cases diverge after
  a few samples (chaotic systems amplify the 10⁻⁸ table differences of §2.3), see §8.3.

### 3.2 Ring modulation and MOD **[DECOMP + EXEC]**

- Each RM source j holds `(a, b) = (g, 1 − |g|)` for target i. The target's RM accumulator `R_i` is set to its current
  MOD level after use; each source multiplies it: `R_i ← (a·out_j + b)·R_i`. The target's raw wave is multiplied by
  `1 + R_i(prev) − MOD_i(now)` before the volume. With MOD = 1 constant this is `Π(1 − |g| + g·out_j)`; sources with a
  lower index act on the same sample, others on the next.
- **MOD** = the operator's "input modulation" articulator (art 2), clamped to 0…1, ramped per sample: at each tick
  `inc = (target − current)/T` limited to ±0.01·44100/fs_v; the FM read uses the value *before* the sample's
  increment, the RM factor uses the new accumulator reset value.
- EXEC: RM 256 and −128, MOD envelope + pitch envelope + feedback: ≤ −131 dB (`t_core1`, `t_full1`).

### 3.3 Columns 6–8 and filter inputs **[DECOMP `FUN_00865120`, `FUN_0086dd40` + EXEC]**

- Column 6 = **PAN** cell: position offset `v/512` (so ±256 = hard left/right, §6.1); column 7 = **FX** send gain
  `g(v)`; column 8 = **OUT** gain `g(v)`.
- Rows 6–8 = filter units: cells in columns 0–5 are the **input gains** `g(v)` of operator j into filter k; filter k
  also receives filter k−1 × `g(p9 of k−1)` ("to next filter").
- A unit is rendered if it has an OUT or FX gain, an FM/RM target, or feeds a filter (it is rendered even when the filter
  it feeds has no output).

---

## 4. Articulators: envelopes, LFOs, maps and their delivery to the voice

*In this section `fs` is the voice rate `fs_v` of §1.2 and `T` the control tick of §1.2 (the 96-based value below is the
default table entry; with "extra high envelope accuracy" T₀ = 8 and the time advanced per tick is still `2T/fs_v`,
EXEC `t_os.py` −114 dB).*

### 4.1 Objects and where they live **[DECOMP]**

- Every unit has a fixed list of articulators (op 6, filter 8, FX 2 — round 11). Runtime class `TOpArticulator`
  (VMT `0x8605f8`; slot 2 load `FUN_00861180`, slot 7 per-tick evaluation `FUN_00861200`, slot 4 time setter
  `FUN_0078b8f0`), built on the library class `TArticulator` (VMT `0x781818`). Fields: `+8` combination mode,
  `+0x10` envelope (`TMEnvelope`, VMT `0x781338`), `+0x18` LFO (`TMLFO`, VMT `0x7814d8`), `+0x20` base value,
  `+0x24` envelope depth, `+0x28` LFO depth, `+0x30` key map, `+0x38` velocity map, `+0x40/+0x48` Mod X / Mod Y maps,
  `+0x50` random map, `+0x58` unison map (maps are `TMMap`, VMT `0x781668`).
- File → object: the articulator record is `i32 version`, then 12 bytes copied to `+0x20` (**base, envelope depth,
  LFO depth** as three f32 — what round 11 called "flags" is the base, always 0.0 in the data **[DATA]**), then the 8
  parts in the order envelope, LFO, key, velocity, Mod X, Mod Y, random, unison (`FUN_0078b4a0`, `FUN_0078bb60`,
  `FUN_00861180`).
- Point record at run time (32 bytes): f32 Δt, f32 level y, f32 tension, byte curve mode, byte "arp" flag, a sign
  byte and three f32 curve parameters + one f32 "modulation weight" computed after loading (`FUN_007859c0`). The file's
  f64 Δt/level are therefore used **rounded to float32**. Header fields: `+0x44` flags (bit 0 TEMPO, bit 1 GLOBAL, bit 2
  LFO unipolar shaping, bit 3 LFO no-advance), `+0x48` enabled, `+0x4c` decay point, `+0x50` loop start, `+0x54`
  sustain point / loop end (`FUN_007849c0`, `FUN_007859c0`).

### 4.2 Combination mode per target **[DECOMP + EXEC]**

Set once by the plugin constructor (`0x8665xx`, masks at `0x867210…0x867214`), never stored in the file. The rule
in the masks is: articulators 0 and 3 are additive in **every** unit (mask `0x09`), filter articulators 2 and 3
(unit mask `0x1c0`, articulator mask `0x0c`), operator articulator 4 (unit mask `0x3f`). *Integration correction:*
the sub-analysis listed no additive articulator for the effects unit; its panning articulator (0) is additive (EXEC: an FX
send with the unit's articulators at rest is centred, `t_os.py` −113 dB after the fix, −29 dB before):

| Unit | Additive (mode 1) | Multiplicative (mode 0) |
|---|---|---|
| operator | 0 panning, 3 pitch, 4 phase | 1 volume, 2 input modulation, 5 plucked damp |
| filter | 0 panning, 2 cutoff, 3 resonance | 1 volume, 4 low/gain, 5 band, 6 high, 7 waveshaper |
| FX (unit 9) | 0 panning | 1 volume |

- Setting mode 1 also sets the **level offset 0.5** of the envelope and of all six maps (`FUN_00781bc0` stores
  `0.5·mode` at `+0x18`): every level used by an additive articulator is `y − 0.5`. The LFO's own amplitude
  envelope always has offset 0.5 (`FUN_00789fa0`). Mode 0 uses offset 0.
- The operator pitch articulator (3) has the 0…1 level clamp switched off (`FUN_0078b990` with 0); all others clamp
  `y + w·mod` to 0…1 (4.4).
- **Default maps** are flat at the neutral value: **1.0 for multiplicative and 0.5 for additive** articulators
  (DATA: 9 027–9 110 flat maps at 1.0 for multiplicative, 7 373–7 498 at 0.5 for additive; the few other flat maps
  are user edits).
- **EXEC:** pan articulator envelope → measured output pan position `x = 2·out` exactly (max error 1.5·10⁻⁷ with an
  additive LFO, `d7.py`); pitch articulator: deviation **= 2·p[2]·out cents** (out = y − 0.5; y = 1, p2 = 1200 →
  +1200.0 ct; y = 0.75 → +600.0; y = 0.25, p2 = 2400 → −1200.0, `d3.py`).

### 4.3 Control rate, time units, tempo **[DECOMP + EXEC]**

- Articulators are evaluated once per **tick of T samples, T = 4·round(fs·96/44100/4)** (96 @ 44.1 kHz, 104 @ 48 kHz;
  `FUN_008657b0`, runtime `+0x62cc`). Ticks are counted from the voice start (voice `+0x14`), independent of the host
  block; the first evaluation (at the note start) uses **advance 0**, all later ones advance 1 (`FUN_00872290`).
- One tick advances every envelope by **Δ = 2·T/fs time units** without TEMPO and **Δ = (T/fs)·tempo/60** with the
  TEMPO bit (`FUN_008656a0` → `FUN_00789c10`; T/fs is taken as `T·(96/44100)/(fs·96/44100)` so it is exact).
  Hence **a point Δt of 1.0 lasts 0.5 s (no TEMPO) or 1 beat (TEMPO)** — this corrects round 11 ("seconds").
  EXEC: all envelope cases at 44.1/48 kHz and TEMPO 140 match (≤ −126 dB).

### 4.4 Envelope evaluation **[DECOMP `FUN_00787a40`, `FUN_00787e50`, `FUN_00787c90`, `FUN_007859c0` + EXEC]**

Per voice and envelope a state holds: stage, target point index, phase (f64, 0…1), rate (f64), segment start level
and delta (f32), modulation offset `mod` (f32), sustain offset, and the four knob factors A, D, S, R.

- **Knobs:** each articulator's A/D/R knobs scale segment durations by `v/128`, the S knob is an offset `v/128`
  (`FUN_00789de0`). For the **volume articulator of every unit** (index 1) the global Volume A/D/S/R (main Tags 3–6)
  and for **filter articulators 2, 3** the global Filter A/D/S/R (Tags 7–10) are applied at note-on:
  `A·gA/128·u`, `D·gD/128·(2 − u)`, `S + gS/128`, `R·gR/128`, with `u` = 1 for a single voice (unison
  "envelope variation", 4.8) (`FUN_0086af20` @ `0x86b4xx`). EXEC: `knobs`, `global_adsr` −128 dB.
- **Modulation weights** (per point, computed at load): with D = decay point, S = sustain point (or last), L = loop
  start (or S): points before D get `w = (time remaining to D)/(attack time)`, D gets 0; points in (D, L] get
  `w = (time from D)/(time D→L)`; points in (L, S] get 1; points after S get `w = (release time remaining)/(release
  time)`. Every level is `y + w·mod`, clamped to 0…1 (except pitch), minus the offset of 4.2.
- **Start:** disabled → stage "finished", output 0. Otherwise target point 0, stage "attack" (or "decay" if there is
  no decay point), rate `Δ/(Δt₀)` or immediate when Δt₀ = 0; output = level of point 0.
- **Each tick:** `phase += advance·rate`; while `phase ≥ 1`: carry the excess (converted to time), the segment start
  becomes the level of the reached point; if the reached point is ≥ the decay point (first time) the sustain offset
  becomes `mod` (so **the S knob raises the decay and sustain levels by w·S**); then: before the sustain point (or
  after release) → next point (the last point ends the envelope); at the sustain point with no loop → **hold**; with
  a loop → jump to the point after the loop start. The new segment lasts `Δt·knob` with knob = A before the decay
  point, D after it, **R after note-off when a sustain point exists** (D otherwise); a duration ≤ 0 is crossed at once.
  Output = `start + shape(phase)·delta` (float32).
- **Note-off** (`FUN_00787c90`, called for every envelope and LFO envelope by Voice_Release `FUN_0086d5a0`): if a
  sustain point exists the envelope jumps to the segment after it **from the current level** (`mod` absorbs the
  difference, then fades with the release weights), otherwise it just continues; stage becomes "released".
  EXEC: `early_release`, `nosus_release`, `loop`, `oneshot_nosus` −128 dB.
- **Curve shapes** (fraction of the segment at phase p, tension t of the arrival point):
  - 0 single curve: `k = sign(t)·(1000001^|t| − 1)`; `k > 0: 1 − (e^{(1−p)·ln(1+k)} − 1)/k`; `k < 0: (e^{p·ln(1+|k|)} − 1)/|k|`; t = 0 linear;
  - 1 double curve: the same curve on each half (rising half then mirrored);
  - 2 hold: stays at the start level, jumps at the end;
  - 3 stairs, 4 smooth stairs, 5 pulse, 6 wave: step/wave count `n = round((1000001^(1−0.6|t|) − 1)/1000)` (+1 for
    stairs with t ≥ 0); 7/8: `p − (d⁶ − d)·t` (d = p or 1 − p by sign; 8 per half); 9: blend toward
    `sin(p·π/2)` or `1 − cos(p·π/2)` by |t|.
  EXEC: `adsr_tension` −146 dB, `modes_1_2` −128 dB, `modes_3_9` −123 dB. Modes 10–12 are decoded in
  `FUN_00787e50` but not modelled (**not confirmed** by EXEC; absent from the 268 demo channels **[DATA]**: modes
  0 = 5179 points, 1 = 34, 2 = 10, 7 = 5, 3 = 3, 9 = 2).

### 4.5 LFO **[DECOMP `FUN_0078a240`, `FUN_0078a3c0`, `FUN_0060ed20`, `FUN_0060eee0`, `FUN_0078a700` + EXEC]**

- The LFO's point list is an **amplitude envelope** (offset 0.5 → level `y − 0.5`, so y = 1 gives ±0.5): it is
  evaluated like 4.4 (knobs fixed at 1). The **first segment (point 0 → point 1) is a delay**: the oscillator does not
  advance until it ends (the part of the tick after the end is used).
- Oscillator: a 32-bit phase. Increment per tick `round(min((10001^(speed/65536) − 1)·6871947.6736·Δ, 805306368))`
  ⇒ **f = (10001^(speed/65536) − 1)/312.5 Hz** without TEMPO (round 11's "625" is in beats: **period
  625/(10001^x − 1) beats** with TEMPO). EXEC `lfo_tri`, `lfo_tempo100` −146 dB.
- Wave from the 32-bit phase u with pulse width pw = PW/128, skew s = Skew/256: breakpoints
  `B7 = 2³²−1` (pw ≤ 1/128) or `(1−pw)·2³²−1`; `B5 = ((1−pw)/2 − s·(1−|pw|))·2³²` (clamped 1…B7−1);
  `B6 = B5 + pw·2³²` when pw < 1/128, else B5. Value: falls 1 → −1 on [0, B6), −1 on [B6, B5), rises −1 → 1 on
  [B5, B7], +1 above B7 — **a triangle by default**. Then the tension curve `K = sign(T)·(10¹⁸^{(21^{|T|/128}−1)/20} − 1)`
  is applied to |v| (bipolar) or to (v+1)/2 (flag bit 2, "unipolar"). Start phase = middle of the rising ramp (the
  upward zero crossing) + the 5th LFO integer (raw 32-bit offset). EXEC `lfo_uni_pw` −134 dB, `lfo_startphase` −146 dB.
- **GLOBAL** (bit 1): the voice starts from a per-plugin phase that `Gen_Render` advances by
  `round(n·K/T·increment)` per host block of n samples (`FUN_008737a0`, list `P+0x63a0`); afterwards the voice advances
  its own copy. *Integration:* the per-plugin phase is **`0xC0000000` (¾ of the 32-bit cycle) after the state is loaded,
  whatever the LFO's skew, pulse width or own phase offset** (read from the running DLL with `lfopeek` for an LFO with
  offset −2³⁰ and for LFOs with skew −18/+46); a voice copies it as it is (no offset added). EXEC demo channels 262
  (cutoff LFO) −100 dB and 217 (Low/Band/High LFOs) −114 dB after the fix, +0.4 / −14 dB before. EXEC `d6.py` (note started at
  block 5) −147 dB. When that global phase is reset (transport start) **not confirmed**.
- LFO output = (amplitude envelope) × wave. **Depth = master LFO level** `sign·(11^{|v|/128} − 1)/10` (main Tag 1):
  `FUN_0086a1d0` overwrites every articulator's `+0x28` with it, so the stored LFO depth is ignored. EXEC
  `master_lfo_neg` −119 dB.

### 4.6 Maps **[DECOMP `FUN_0078bbc0`, `FUN_00788b20`, `FUN_00874740`, `FUN_0086af20` + EXEC]**

- A map is evaluated as an envelope traversed by time = x (curve shapes as 4.4, no weights).
- **Key map:** x = key/120, key = `Final.Pitch/100 + 60` + master pitch (≥ 0; −12 for the "sub" unison layer).
  **Velocity map:** x = the host's voice velocity (voice event 3, 0…1). The two are combined by the articulator mode
  into a **static value** (product, or sum of `y − 0.5`). EXEC `keymap`, `velmap` −128 dB.
- **Unison map:** x = 0.5 for a single voice, `i/(n−1)` for unison voice i of n; combined into the static value.
  EXEC `d4.py` (n = 2, 3: maps 0→0/1→1, 0→0/1→0.5, 0→0/0.5→1/1→0 behave as predicted; full unison levels: §6.3). **Random map:** applied only when plugin byte `+0x441` (DFM "RANDOM", "Enable randomness"
  **[STR]**) is set and the map is not at its default; x = **Delphi `System.Random`** (`FUN_0040a030`): the DLL-wide
  `RandSeed` (`0x8a2010`) is advanced `s ← s·0x08088405 + 1` and x = s/2³², once per non-default random map, in the
  order units 0…9 / articulators 0…n / unison voices in creation order. `RandSeed` is randomised from the clock when
  the DLL loads, so this map is **not reproducible between FL sessions**; with the seed read from the running DLL
  just before TriggerVoice (harness), the model matches (integration EXEC, demo channels with random maps).
- **"Default" test** (`FUN_0078acd0`, TMMap VMT `0x781668` slot 16): a map is default when it is flat **at the
  neutral value** (1.0 multiplicative / 0.5 additive) — *integration correction*: the sub-analysis tested "flat"; demo channel
  202 has an additive Mod Y map flat at 0.0, which the DLL applies (−0.5 → one octave down on its operators; EXEC −112 dB
  after the fix, +2.5 dB before).
- **Mod X / Mod Y maps:** used only when the curve is not default; tabulated at 513 points (step 1/512)
  and read at index `round(ModX + 256·Final.FCut)` (Mod Y with Final.FRes), clamped 0…512; ModX/ModY = main ints
  190/191 (0…512, default 256). Re-read every tick. EXEC `modx_map`, `mody_map_fres`, `modx_fcut` −128 dB.

### 4.7 Articulator output and delivery to the voice **[DECOMP + EXEC]**

- Per tick (`FUN_00861200`): `out = static ∘ ModX ∘ ModY`, then
  - envelope on: mode 1 `out += env·envdepth`; mode 0 `out *= env·envdepth`;
  - LFO on: mode 1 `out += lfo·LFOdepth`; mode 0 `out *= 1 + 2·lfo·LFOdepth`.
  Disabled envelope/LFO → that factor is absent (output = static). At note-on the same value is formed with the base
  (`+0x20`) added to the first envelope level. The 0…1 clamp of articulators 2 and 5 at note-on applies only to the
  voice's *initial-value copy* that operators use (MOD, plucked damp), **not** to the articulator output itself
  (*integration correction*: a filter cutoff articulator, also index 2, keeps negative values; EXEC demo channel 17).
- **Volume:** articulator 1 output × operator volume `p[1]/256` is the **linear** amplitude target (not squared); per
  tick the voice computes `inc = (target − current)/T`, limited to `|inc| ≤ 0.01·44100/fs` per sample, and each
  sample multiplies the operator wave by `current += inc` (float32, increment applied before use) (`FUN_0062bbf0`,
  `FUN_00870580`). EXEC: every case above uses exactly this law.
- Pitch/phase/MOD/pluck/pan outputs are delivered the same way (pan: output position `(FinalPan/2 + unit pan + out)·2 − 1`
  per tick — §6.1).
- **Velocity options** (plugin bytes from the 9-byte flag group at `P+0x4F7`): byte 6 (`+0x4FD`) bit 0 → attack knob
  × `(2 − 2·vel)`; bit 1 → every enabled envelope starts `t = (attack time)·A·vel` into its attack; byte 7
  (`+0x4FE`) → release knob × the host's per-note release value (voice event 5). EXEC `vel_attack_scale`,
  `vel_attack_seek`, `release_mult` (harness value 0.4), `no_release_mult` −128…−146 dB. (Default.fst has byte 7 = 1.)
- **GLOBAL envelope** (bit 1): a new voice copies the state of the first still-held voice and adopts its tick grid
  (`FUN_00789e90`, `FUN_0086af20`). EXEC `d5.py` −131 dB.

### 4.8 Unison envelope variation **[DECOMP + EXEC (integration)]**

For unison voice i of n (x = i/(n−1)): `u = (|2x − 1| − 0.5)·Var·c_k/128 + 1`, Var = main Tag 17, `c_k` from the
table `{0.9877, 1.0069, 0.9929, 1.0181, 1.0031, 1.0129, 0.9887, 1.0117, 0.9977}` (k = (i + layer) mod 9, layer 1 for
the sub voices); attack × u, decay ×
(2 − u) (`FUN_0086c7d0`). *Integration:* EXEC-verified on the whole voice (3 voices, variation 100: −126…−129 dB,
`t_full1.py` "uni3-envvar").

### 4.9 End of voice and declick **[DECOMP `FUN_00872290`, `FUN_00616e20`, `FUN_0062bb20` + EXEC]**

- Note-off marks the voice "released"; at the next tick boundary the voice is finished if **every active articulator
  of every unit** (envelope or LFO enabled, or a Mod X/Y table) has reached the end of its envelope and LFO envelope.
  With none active (all envelopes disabled) this happens at the first tick after note-off. EXEC: pitch envelope with a
  1 s release keeps a disabled-volume voice sounding 1 s after note-off (`d8.py`).
- A finished voice's output gains go to 0 by a ramp of **at most 0.001·44100/fs per sample on the L/R gain** (0.7071
  at centre ⇒ 707 samples), recomputed per render segment (tick ∩ host block); the voice is freed when both gains are
  0. This is the "~800-sample decay" of an envelope-less note. EXEC `env_off_release` −165 dB.

### 4.10 Not confirmed / not modelled

(Random map, unison variation and portamento were verified when the voice was assembled: §4.6, §4.8, §6.4.)
Curve modes 10–12, the "arp" point flag (all 0 in the
demos), map lookup tables when a map's resolution field is non-zero (never observed), the reset point of the GLOBAL LFO
phase, and the host semantics of voice event 5 beyond "multiplier" (FL's value not measured).

---

## 5. The three filter units (units 6–8)

*In this section `fs` is the voice rate `fs_v` of §1.2.*

### 5.1 Where things live **[DECOMP]**

| Item | Location / function |
|---|---|
| Parameter conversion (unit p[0..9]) | `FUN_00869e80` (unit record `P+0x518+u·0x900`) |
| Filter block (0x30 B in the state) | copied to unit `+0x880`: `+0x00` **HQ**, `+0x01` **FLAT**, `+0x28` **waveshaper ON**, `+0x2C` **type** (0…12); trailing state byte (v ≥ 12) = **unipolar** shaper mode (`FUN_00862d70`, `FUN_00781b90`) |
| Per-voice filter state | voice `+0x88 + u·0xA38` (u = 6…8); stage records at `+0x190 + i·0x78` |
| Voice set-up of the filter state | `FUN_0086af20` (stage count, ALT, Q exponent, oversampling flag, rate factor) |
| Routing lists | `FUN_0086dd40` |
| Per-sub-block processing | `FUN_00870820` (filter part: loop over units 6, 7, 8) |
| Coefficients | `FUN_0086ebe0`; biquad designer `FUN_006253c0`; Chocolate table `FUN_0086ea60`; shaper table `FUN_00874040` |
| DSP kernels (selected at start-up by `FUN_00628580`; plain variants are the ones active on the test machine, read with `peek`) | Chocolate SVF `FUN_00626230` / ramped `FUN_006262d0`; Lime LP `FUN_00626640`, Lime HP `FUN_00626770`; biquad `FUN_00625bd0`; Mango linear `FUN_006269b0`, Mango driven `FUN_006267f0`; phaser `FUN_00780d10`; shaper `FUN_0062af60`/`FUN_0062afe0` (bipolar), `FUN_0062b130`/`FUN_0062b1b0` (unipolar); 2× up/down `FUN_0062c6d0`/`FUN_0062c6f0`; parameter ramp `FUN_0062bb20` |

Manual: "Select from one of 13 filter types"; "OFF turns the filter off. x1, x2, x3 are 12, 24 and 36 dB/Octave
filters"; "ALT x2 and ALT x3 provide alternative algorithms"; HQ "Improves filter quality"; FLAT "Applies an
alternative resonance algorithm"; Low/Band/High apply "only to the SVF"; "To next filter … sends the output of the
current filter module to the next one" **[DOC `…/plugins/Sytrus.htm`]**. DFM hints: HQ = "Oversample when required",
FLAT = "Alternate resonance / bandwidth / mode", mode list "off, single, dual, triple, alternate dual / quad,
alternate triple / penta" **[STR]**.

### 5.2 Parameters **[DECOMP `FUN_00869e80`, EXEC]**

`g(x) = (11^x − 1)/10` (exactly 1 at x = 1).

| p | Meaning | Internal value |
|---|---|---|
| 0 | mode 0…5 | stage count, ALT flag (5.3) |
| 1 | cutoff envelope/LFO amount | **replaces the cutoff articulator's envelope depth by p1/128** and its LFO depth by `p1/128 × Master LFO` (integration EXEC: LFO depth law checked with p1 = 40, `t_lfo_p1.py`: −180 dB vs −2.7 dB with the master level alone) |
| 2 | cutoff | `c₀ = p2/256` |
| 3 | resonance / bw / feedback | `r₀ = p3/256` |
| 4, 5, 6 | Low, Band, High | `g(p/128)` (Chocolate mix gains); p4 is also the shelf/peak gain and the Mango drive (5.5) |
| 7 | waveshaper pre amp | `p7/128` |
| 8 | waveshaper mix | `p8/128` |
| 9 | to next filter | `sign·g(|p9|/256)` |

Per voice the working values are **`c = p2/256 + A_cut`**, **`r = p3/256 + A_res`** (additive articulators) and
multiplicative articulators for volume (input gain), Low/Band/High and the waveshaper mix **[DECOMP `FUN_00870820`]**.
EXEC with a constant (two-point) envelope of level L: `A_cut = (L − 0.5)·p1/128` (L = 1, p1 = 128 → +0.5; p1 = 64 →
+0.25), `A_res = (L − 0.5)·depth` (file depth 1.0), volume and Low/gain articulators = L (multiplicative)
**[EXEC `t_env.py`, `t_env2.py`]**. With the articulators disabled, the additive ones give 0 and the multiplicative
ones 1 **[EXEC]**. (Envelope/LFO evaluation itself: §4.)

### 5.3 Mode, stages, ALT, Q split **[DECOMP `FUN_0086af20`; EXEC]**

- `n = mode − 1` = index of the last stage; **mode 0 (OFF) = no filtering, the signal passes through unchanged**
  (input gain, waveshaper and routing still apply) **[EXEC: −184 dB]**.
- Modes 4/5 (**ALT**) for types 0–11: ALT flag set and `n −= 2` → **2 or 3 stages** with staggered cutoffs: the
  normalised cutoff value `d` (5.4) is used for the **last** stage, and for each earlier stage it is raised to the
  power 0.5 (ALT x2) or 0.75 (ALT x3), repeatedly (stages are designed from the last to the first; table
  `0x8FD2A0` = {0.5, 0.75}). Stages are processed first-to-last.
- Q split: types 0–10 use `Q_stage = Q^(1/(n+1))` (also the shelf/peak gain `A^(1/(n+1))` and, for Chocolate, the
  Low/Band/High gains `g^(1/(n+1))`); Mango uses the exponent `1/(n/4 + 1)` on `(res + 1)`; band-pass/band-stop do
  not split Q.
- Cherry (type 12): mode m → **m first-order all-pass pairs, +5 pairs with FLAT** (ALT not used).
- Oversampling: **2× when (HQ and fs ≤ 176 399) or (type = Mango and fs ≤ 88 199)**: each input sample is duplicated,
  the filter runs at 2·fs, and pairs are averaged back (`(y₂ₖ + y₂ₖ₊₁)/2`) **[DECOMP + EXEC]**.
- Rate factor `k = 44100/fs` (host field `+0x20`), halved when oversampled. All cutoff laws below are therefore
  **absolute in Hz (independent of fs)** apart from the ceilings **[EXEC 22.05/44.1/48/96 kHz]**.

### 5.4 Coefficient laws per type **[DECOMP `FUN_0086ebe0`, `FUN_006253c0`; EXEC ≤ −100 dB]**

FLAT for types 1–10 first replaces `r` by `r·(c + 1)/2`.

| Type | Cutoff value `d(c)` (0 if c ≤ 0) | Frequency | Resonance law | Structure |
|---|---|---|---|---|
| 0 Chocolate SVF | `0.0396·(26^c − 1)` | SVF coefficient `F = T[round(2048·d)]`, `T[i] = 2·sin(k·asin(min(1, (i/2048 + 0.01)/2)))` capped at 1.175 (table stops there) ⇒ **fc = (44100/π)·asin((d + 0.01)/2) Hz** | damping `q = 1 − 0.8r` (r ≥ 1 → 0.2; r ≤ 0 → 1), FLAT ⇒ `q²`; stages > 1 use q = 1; FLAT additionally rescales damping per stage: stage 0 `q·k/F`, others `k/F` | Chamberlin SVF per stage: `lo += F·bp; hp = x − q·bp − lo; bp += F·hp`; stage output = `L·lo + B·bp + H·hp` (cascade) |
| 1/2 Lime LP/HP | `min(1.6, 0.01324·(76^c − 1))` | `t = cos(min((d + 0.007)·k·π/2, 1.2566))`; `A = 2 − 2t` | `Q = 1 + 0.049·(1001^r − 1)` (≤ 50), split; `B = √(−(t−1)³)·√2/(Q·(t−1)) + 1`, ≥ 0.1 | `v += (x − y)·A; y += v; v *= B`; LP out y, HP out x − y (extended precision) |
| 3 Vanilla LP | `0.000664667·(1501^c − 1)` | `ω = 2π·k·(d + 0.003)` ≤ 2.8495 ⇒ **fc = 44100·(d + 0.003) Hz** | `Q = 0.1 + 0.02495·(2001^r − 1)` (≤ 50), split; `α = sin ω/(2Q)` | RBJ low-pass biquad |
| 4/5 Vanilla BP / BS | `min(1.0001, 0.00039993·(1501^c − 1))` | `ω = 2π·k·(d + 0.0001)` | `BW = 0.01 + 0.1199·(101^r − 1)` (≤ 12); `α = sin ω·sinh(ln2/2·BW·m·ω/sin ω)` with m = mode (BP) or 1/mode (BS) | RBJ constant-0-dB BP / notch |
| 6 Vanilla HP | as 4 but ≤ 0.6001 | as 4 | as 3 | RBJ high-pass |
| 7 Vanilla AP | as 4 | as 4 | as 3 | RBJ all-pass |
| 8/10 Vanilla low/high shelf | as 4 | as 4 | `S = 0.1 + 0.45·(3^(1−r) − 1)` (1−r ≥ 1 → 1); gain `A = 0.125·8^(p4·A_low/64)` (p4 = 64 → 0 dB, 128 → +18 dB), split; `α·2√A = √A·√((A + 1/A)(1/S − 1) + 2)·sin ω` | RBJ shelf |
| 9 Vanilla peaking | as 4 | as 4 | `BW′ = 0.1 + 0.04995·(2001^(1−r) − 1)` (≤ 100); `α = sin ω/(2·BW′)`; A as 8 | RBJ peaking |
| 11 Mango LP | `min(3, 0.01495·(201^c − 1))` | `w = min((d + 0.01)·k/2, 0.65)`; `g = 1 − exp(−π·w·(1.873w³ + 0.4955w² − 0.649w + 0.9988))` | drive `D = 0.12·(251^(p4·A_low/128) − 1)`; `R = r·(0.6·min(1, D) + 1.9)` (r ≥ 1 → no r factor), split as `(R + 1)^(1/(n/4+1)) − 1`; feedback `fb = −R·(−3.9364w² + 1.8409w + 0.9968)`; D < 0.001: linear, out gain `(1 − fb)/2`; else coefficient `g/D`, drive `2D`, FLAT ⇒ `fb·tanh(4/D)`, out gain `(1 − fb)/(2·tanh(4/D))` | 4 one-pole stages with global feedback from the sum of the last stage's new and old value; driven version passes every stage difference through `s(v) = v·m/(m|v| + 12)`, `m = (3 + |v|)|v| + 6` |
| 12 Cherry phaser | `0.006666·(151^c − 1)` | `w = min((d + 0.0001)·k, 1)`, all-pass coefficient `a = (1 − w)/(1 + w)` | feedback `0.9·ln(10r + 1)/ln 11` (r ≥ 1 → 0.9) | `x + fb·y_prev` through 2·(pairs) first-order all-passes (`u = s − a·v; s = v + a·u`); **output = all-pass chain only (no dry)** |

Biquads run in double precision with the coefficient normalisation `y = b₀x + b₁x₁ + b₂x₂ − a₁y₁ − a₂y₂`.
The **cutoff/resonance ceilings** therefore differ per type (Vanilla LP: ω ≤ 2.85 rad ≈ 20 kHz at 44.1 kHz;
Chocolate: coefficient 1.175). Examples (computed from the laws above) for c = 0 / 0.5 / 1: Chocolate 70 Hz / 1.21 kHz / 7.35 kHz (the 1.175
cap corresponds to 8.8 kHz at 44.1 kHz and is only reached via articulators, c > 1); Vanilla LP 132 Hz / 1.24 kHz /
20.0 kHz (ω cap). Note: this replaces the Chocolate law of round 11 §1.7.

### 5.5 Signal path of one unit **[DECOMP `FUN_00870820`, `FUN_0086dd40`; EXEC `t_chain.py`]**

1. Input = Σ over operators i of `out_i · g_s(M_FM[6+u′][i])` (`g_s(v) = sign·g(|v|/256)`), plus the previous filter's
   output × `g_s(p9)` of that filter (**only filter k → filter k+1**; the matrix cells rows 6–8 × columns 6–8 are
   PAN / FX / OUT, not filter-to-filter). The operator signal here is **before panning** (EXEC: the nonlinear Mango
   only matches with the unpanned operator signal).
2. × volume articulator output (ramped over the sub-block with the 4-lane ramp of `FUN_00629f70`, slope limit
   0.01·44100/fs). *Integration:* the ramp's starting value for a new voice is the articulator's **note-on value**
   (voice `+0x380`), so an enabled volume envelope that starts at 0 fades the unit in over the first sub-blocks
   (EXEC demo channel 23: −186 dB after the fix, −18 dB before).
3. Filter stages (skipped for mode OFF), with the 2× oversampling of 5.3.
4. Waveshaper if ON (*integration*: the curve table is rasterised from the shaper envelope with the map evaluator of §4.6;
   the mix target `clamp(A_ws·p8/128, 0, 1)` is **ramped over each sub-block** — 4-lane ramp for `(1 − m)`, scalar ramp
   for `m` — from the previous target, starting on target for a new voice; the interpolated kernels are used while FL
   renders or when the last state byte `P+0x5F18` is set (EXEC demo channels 63/105: −105…−120 dB after the fix,
   −41…−45 dB before)); bipolar mode — `y = curve(|x·pre|)` as an int16 table of 16 384 points (`round(curve·32767)`),
   odd-symmetric, nearest point (default) or linear interpolation (when a global flag is set); unipolar mode —
   table of 32 768 points `round((curve − 0.5)·65535)` indexed by `(x·pre + 1)/2`; result /32768; output
   `(1 − mix)·x + mix·shaped`, `mix = clamp(A_ws·p8/128, 0, 1)` **[EXEC ≤ −105 dB with linear-segment curves]**.
5. Output routing as for operators: OUT `g_s(M[6+u′][8])`, FX send `g_s(M[6+u′][7])`, pan `M[6+u′][6]` (voice output
   stage, §6.1). A unit counts as active (and is processed) only if it has at least one input.

### 5.6 Time behaviour **[DECOMP + EXEC]**

- Articulators are evaluated once per control block (96 samples at 44.1 kHz; 104 at 48 kHz; 208 at 96 kHz; 48 at
  22.05 kHz — **[EXEC]**) and held. The unit is processed per sub-block = control block cut at host-block boundaries.
- At each sub-block the target coefficients are recomputed from `c`, `r`. Biquad, Mango and Cherry coefficients are
  replaced at the sub-block start. Chocolate (frequency coefficient per stage, stage-0 damping, Low/Band/High gains)
  and Lime (A, B per stage) **ramp linearly** from the previous target to the new one across the sub-block, the slope
  limited to `0.01·44100/fs` per sample (`FUN_0062bb20`); the first sub-block of a voice starts at the target.
- EXEC (cutoff/resonance read per control block from the DLL's own voice, then fed to the model): ≤ −110 dB for all
  types with host block = control period, ≤ −115 dB with host blocks of 512/100/37 samples (`t_dyn2.out`).

### 5.7 Verification summary **[EXEC]**

- `test_filt.out`: 366 static cases (13 types × modes 1–5 × 3 cutoff/res settings at 44.1 kHz, modes 1/4 at 48 and
  96 kHz, Low/Band/High mixes, FLAT, HQ at 44.1/96 kHz, units 7/8, shelf/peak gains, Mango drive, ceilings,
  22.05 kHz): all ≤ −100 dB relative error except one silent output (VanBP mode 5, c = 0.5, r = 0: rms 3·10⁻⁵, error
  −143 dB relative to the input).
- `t_chain.out`: to-next-filter chain, negative/partial input and OUT cells: ≤ −134 dB.
- `t_ws.out`: waveshaper bipolar/unipolar, pre amp, mix: ≤ −105 dB.
- `t_dyn2.out`: dynamic cutoff/resonance, all types, 44.1/48/96 kHz.

### 5.8 Not confirmed / open

- (Resolved in integration: the shaper table rasterised with the map evaluator of §4.6 is identical to the DLL's own
  table, read with `dumpshaper`, for a unipolar curve with tensions −0.75/+1.0 (demo channel 217). The interpolated
  kernels are selected when `P+0x6336` (rendering) **or** the last state byte `P+0x5F18` is set (`FUN_00870820`).)
- (Resolved in integration: the Chocolate Low/Band/High gains are `unit gain × articulator 4/5/6` (powered by
  `1/(n+1)` for n > 0) and ramp over each sub-block — Low through the 4-lane ramped multiply, Band/High through scalar
  ramped add-products (`FUN_00616f70`, `FUN_00616db0`); EXEC demo channel 19 (High-gain envelope) −140 dB after the fix,
  −28 dB before. The waveshaper mix ramp: see 5.5.)
- Note: the operator output itself depends slightly on the sub-block partition (≈ −64 dB at 48 kHz between host
  blocks of 96 and 512), because the block reader's float lanes and the phase wraps restart at each sub-block (§2.2,
  §2.3); the voice model reproduces it by rendering with the same partition.

---

## 6. Voice output, unison, portamento and voice management

### 6.1 Output gains and pan law **[DECOMP `FUN_00872290`, `FUN_0062bc50`, `FUN_00616e20`, `FUN_0062bb20`, `FUN_00629740`/`FUN_00629870` + EXEC]**

For every unit (operator or filter) with a non-zero OUT gain, once per tick:

```
pos  = Final.Pan/2 + u_pan + PAN_cell/512 + out₀          (out₀ = the unit's panning articulator, additive)
pan  = clamp(2·pos − 1, −1, 1)
G    = Final.Vol · Master · u_vol · g(OUT)                  (Master = g(main[0]/128), (11^x − 1)/10, exactly 1 at 128)
L    = G·cos((1 + pan)·π/4),   R = G·sin((1 + pan)·π/4)     (x87 fcos/fsin, stored as float)
```

`u_pan`, `u_vol` = the voice's unison pan (0.5 for a single voice) and unison weight (§6.3). The FX column send uses
the same law with `g(FX)`, the main-block FX pan int `p` added as `p/256` to `pos`, and the effects unit's
articulators: its panning articulator added to `pos` and its volume articulator multiplying `G` (§7.3).

The L/R gains are ramped **over each sub-block** (not over the tick): starting from the previous value, slope limited to
`0.001·44100/fs_v·u_vol` per sample (`FUN_0062bb20`; if the limited ramp would end within 2⁻²⁴ of zero it is made to
end exactly at zero); sample k of the sub-block uses `start + k·inc` (the increment is added *after* use). A new voice
starts on target. The unit's mono buffer is added to the stereo bus as `out_L += x·gL`, `out_R += x·gR` (float).

EXEC (`t_core0`, `t_core1`, `t_rel`): pan cells ±256 = hard L/R, −100/+200 mixed, Final.Pan −0.5 → 0.9239/0.3827,
Final.Vol 0.5 → ×0.5, master 100 → `g(100/128)`, 44.1/48/96 kHz: −122…−152 dB. Velocity (`Init.Vol`) does not scale
the output by itself (§1.3).

### 6.2 Note-off and end of voice **[DECOMP `FUN_0086d5a0`, `FUN_00872290`, `FUN_0086d770` + EXEC]**

- Voice_Release: voice state 5 ("released"), every envelope and LFO envelope of every unit gets its note-off (§4.4),
  the release-knob multiplier (§4.7) is applied, and the call is forwarded to the voice's unison chain.
- At each later tick start a released voice becomes state 9 when **all** active articulators of all units have
  finished (§4.9); in state 9 the OUT/FX gain targets are 0, so the gains ramp to 0 at the slope limit of §6.1
  (0.7071 at centre → 707 samples at 44.1 kHz); when every gain has reached 0 the voice is freed (state 10) and the
  host is told (host method `+0x28`).
- EXEC: envelope-less patches end exactly where the DLL ends them (last non-zero sample 4895 after a release at 4096,
  44.1 kHz; 3431 at 48 kHz; 2-operator pan patch; 3-voice unison): −122…−136 dB (`t_rel.py`).
- "Kill" (`FUN_0086d770`, used by MONO KEY and by the polyphony limit, §6.4) forces state 9 on the voice, all its
  articulators and its chain: the same gain ramp to 0 follows.

### 6.3 Unison **[DECOMP `FUN_0086c7d0`, `FUN_0086af20` + EXEC]**

Main-block ints 173…179: order n (1…9), pan (0…128), volume (16…128), pitch (0…256), sub (0…96), phase (0…128),
envelope variation (0…192) (DFM `Fat*` **[STR]**); mode byte `b = P+0x4FF` (0…3, 9th byte of the flag group) and the
**OSC** button `P+0x4F4` ("Unison for oscillators only" **[STR]**).

- Spread unit: `d = (11^(pitch/256) − 1)·5·ln2/1200` (b = 0) or `(41^(pitch/256) − 1)·1.875·ln2/1200` (b ≠ 0), i.e.
  ≤ 50 / ≤ 75 cents (round 11).
- Weight: `w = 1` for n = 1, else `w = 1 − (3^(1−1/n) − 1)/2`; with sub `s = sub/128`: main voices `w·(1 − s)`, sub
  voices `w·s`.
- Voice e = 0…n−1 (then the same for the sub layer, pass p = 1): `x = 2e/(n−1) − 1` (n > 1),
  - pan `u_pan = 0.5 + x·pan/256` (0.5 for every voice when OSC grouping is active);
  - volume `u_vol = w·V + w₂·(1 − V)`, `V = volume/128`, `w₂ = 2w(1 − |x|)` (or `w` when b ≠ 0 and n = 2);
  - detune factor `det = exp(±|x|·c[b][(e+p) mod 9]·d)`, sign + for even e, − for odd e (flipped again when b ≠ 0,
    n odd and 2e > n), with the table rows `c[0] = c[1] = {0.877, 1.069, 0.929, 1.181, 1.031, 1.129, 0.887, 1.117,
    0.977}`, `c[2] = {1, …, 1}`, `c[3] = {0.890472, 1.076426, 0.911577, 1.102536, 1.034126, 1.166861, 0.853971,
    1.206273, 1.0}` (`0x8fcad8`); the sub layer multiplies `det` by ½ (one octave down);
  - start phase `(e/n)·phase/128` cycles (b = 3: `frac(φ⁸·det·10⁵ (·½ for the sub layer))`, φ = phase/128);
  - envelope variation factor `u = (|x| − 0.5)·Var·v[(e+p) mod 9]/128 + 1` (§4.8), `v` = `{0.9877, …, 0.9977}`;
  - unison map position `x_u = e/(n−1)` (0.5 for n = 1); key map −12 per sub voice.
- **OSC grouping** (byte `P+0x4F4` and (n ≥ 2 or sub > 0)): the first voice is the master. Each voice renders its
  operators (with its own articulators, ticked by the master), then the operator outputs are summed into the master's
  operator buffers weighted by each voice's `u_vol` (second voice: copy × weight; others: += × weight; master last);
  only the master runs CENTER/DECLICK, the filters and the output, with `u_vol = 1` and `u_pan = 0.5`.
- EXEC (`t_uni.py`, `t_uni2.py`, `t_group.py`, `t_group2.py`): n = 2…9, sub, phase, pan, volume, all four modes b,
  grouped and ungrouped, with FM: −118…−183 dB.

### 6.4 Portamento, MONO KEY, GLOBAL PITCH and polyphony **[DECOMP `FUN_0086c7d0`, `FUN_0086af20` + EXEC]**

- **PORTA** (`P+0x4F8`, DFM Tag 596): for a new note, portamento is active when there was a previous note
  (`P+0x62E0`, last `p`; cleared by `FPD_Flush`) and `PORTA ≠ (note colour = 14)` — the colour-14 note inverts the
  button. The glide is made **through each operator's pitch articulator, only when that articulator's envelope is
  enabled, its range p[2] ≠ 0 and it is not GLOBAL**: `Δ = p_prev − p_now` (cents) becomes the envelope's starting
  offset `mod = Δ/2400`, which then fades with the envelope's modulation weights (§4.4), i.e. over the segments up to
  the decay point. Byte `P+0x4FB` (glide mode): 1 → Δ reduced by octaves while |Δ| > 1250 ct; ≥ 2 → |Δ| limited to
  `(6 − mode)·100` ct. Byte `P+0x4F9`: attack knob × |Δ|·p[2]/1 440 000. Byte `P+0x4FA`: the key used by the maps is
  the previous note's. EXEC (`t_porta.py`, two notes 48 → 67, modes 0/1/3, attack scaling, colour 14): −131 dB.
- **MONO KEY** (`P+0x443`, "Limit to 1 voice per key" **[STR]**): a new note kills (§6.2) the voices of the same key
  (`round(key)`) that are not already finishing.
- **GLOBAL PITCH** (`P+0x442`, "Operator 1 pitch articulator = global" **[STR]**): operators 2…6 multiply their
  frequency by operator 1's pitch-articulator factor as well (`f_eff = f·P_art(i)·P_art(1)`). EXEC demo channels
  162/163: −129…−131 dB after the fix (+5 dB before).
- **Polyphony:** `P+0xE0` = the host's `FPE_MaxPoly` (ProcessEvent 1). When a new note arrives and the number of
  counted note groups ≥ MaxPoly, the group with the largest age counter is killed; released groups count as oldest
  (their counter is set to `0x4FFFFFFF` at release). **[DECOMP only]** — the harness never sent FPE_MaxPoly.
- Mono/legato as FL channel settings are host behaviour (not in the plugin).

---

## 7. Post-voice chain: oversampling, effects unit (chorus), delays, reverb, EQ, DC stage, send

### 7.1 Order of operations in `Gen_Render` (`FUN_008737a0`) **[DECOMP + EXEC]**

Two stereo buses are cleared each block: **M** (main, filled by the matrix OUT column of every voice) and
**F** (FX bus, filled by the matrix FX column, col 7). A third buffer **T** is scratch. Per block:

1. The host block length is multiplied by the oversampling factor `K = 2^order` (`+0x62f4`); all voices render
   at `fs·K` into M and F.
2. FX tail counter (`+0x6308`): while at least one voice exists it is reloaded with
   `L_tail = ⌊fs·K·100/44100⌋ + (chorus ring length in voice-rate frames)` (`FUN_00869a80`); with no voice it
   decreases by the block length. "FX active" below means counter > 0.
3. If `order > min(order, 2)`: M and F are decimated by 2 per stage down to the **FX rate**
   `fs_fx = fs·2^min(order,2)` (separate filter states for M and F).
4. EQ in mode 2 ("out only") processes M, or in mode 3 ("effects only") processes F.
5. **Effects unit / chorus** (if its order N ≥ 1, main int `0x2D4`): when FX active, F is written into its ring and the chorus output T
   is **added to M**; T is also **added to F** when something downstream listens to F (any delay unit on, the
   reverb on, or an FL send track selected — flag `+0x5f28`, `FUN_00869480`). When not active only its LFO
   phases advance.
6. **Send to an FL mixer track** (main-block int `0x2F4` ≠ −1): F (decimated to `fs` with its own filter set if
   needed) times `g(send/128)` is added to the buffer the host returns for track `send+1` (host method `+0x128`,
   round 18 §5). **DECOMP only** (the harness host returns no buffer).
7. **Delay units 1→3**: if FX active, F is added into the unit's ring; the unit output × its volume gain is added
   to **M**, or to **F** when the unit's "serial" byte is set *and* a later delay unit or the reverb is on (so
   later units receive it).
8. **Reverb**: input = mono sum `F_L + F_R` (no ½), output (stereo) replaces F, then `M += F · wet`.
9. EQ in mode 1 ("out+effects") processes M.
10. M is decimated to `fs` (remaining stages).
11. If the global byte `P+0x4F5` is set: DC stage on M (§7.7).
12. M is **added** to the host output buffer. Nothing in this path applies a master level or master pan (those are
    per voice, §6.1).

So the dry FX-column signal itself never reaches the output: it is heard only through the chorus, delays and
reverb (or the FL send). **[DECOMP; EXEC: FX column + one delay with feedback 0 gives exactly the dry voice
delayed, −147 dB]**

### 7.2 Oversampling **[DECOMP + EXEC]**

- Order = main-block int at **0x30C** (`P+0x438`) normally, and at **0x310** (`P+0x43C`) while FL renders:
  the Dispatcher `FPD_ProcessMode` (id 1) sets `P+0x6336 = (value & 0x10) ≠ 0` (`PM_IsRendering`) and rebuilds
  everything (`FUN_00862b80`). `K = 2^order`; FX rate factor `min(K, 4)`. In the demo channels the pair
  (live, render) is (0, 3) in 147/268, (0, 0) in 56, (1, 3) in 23, (2, 3) and (3, 3) in 13 each **[DATA]** —
  i.e. most demo patches render with **8× oversampled voices and 4× FX**.
- Each 2:1 stage (`FUN_00841cd0`, coefficients `0x8f71e0`): a polyphase IIR half-band made of two cascades of
  three first-order allpasses `y[n] = x[n−1] + k·(x[n] − y[n−1])`, branch A on the first sample of each input
  pair with `k = 0.1473771136, 0.4824685428, 0.8830050258`, branch B on the second with
  `k = 0.0391515977, 0.3026468483, 0.6746159185`; output `0.5·(A + B)`; double-precision states rounded to float
  (and zeroed below 2⁻²⁴) at the end of each block. A block whose input and states are all below 10⁻⁶ skips the
  filters (`FUN_008420a0`).
- EXEC: orders 1, 2, 3 (and order 3 selected through render mode) match an analytic oversampled sine voice
  through the model to −98…−103 dB (limited by the voice table, not by the decimator).
- Render mode also switches a second pair of ints (`0x304`/`0x308`, `P+0x430/0x434`): the voices' control-tick
  table index (§1.2).

### 7.3 Effects unit (chorus) **[DECOMP `FUN_00869a80`, `FUN_00840920`, `FUN_008413b0`/`FUN_00841570`; EXEC]**

State ints (main block 0x2D0…0x2F0): pan, order N (0…9), depth (0…1024), speed (0…1024), delay (0…128),
spread (0…128), cross (−128…128), volume (−128…128), send (0…128) **[DATA + DFM `ChOrderSelect`…]**.
N = 0 removes the unit. With `d = depth/1024`, `s_p = speed/1024`, `dl = delay/128`, `sp = spread/128`,
`c = cross/128`, `v = volume/128`, `r = 44100/fs_fx`:

- `A = 109.25·(11^d − 1)`, `B = 500·2³¹ / (75000·(101^(1−s_p) − 1) + 50000)`, `C = 441·(11^dl − 1)`,
  `σ = (1001^sp − 1)/1000`, `mix(a, b) = σ·a + (1 − σ)·b`.
- **2N taps** `i = 0…2N−1`; taps `i < N` read the left FX channel with `f = i`, taps `i ≥ N` read the right
  channel with `f = 2N−1−i` and a half-cycle LFO offset; `w = 1 − f/N`; per-tap factor `q_i` from a fixed
  20-entry table (`0x8f702c`: 0.967, 1.039, 1.021, 0.1013, 0.977, 1.033, 0.997, 0.971, 1.061, 0.947, 1.031,
  0.1009, 1.063, 0.953, 0.991, 1.049, 0.983, 1.051, 1.019, 1.069), `dV = mix(q_i², 1)`.
- Gains: own side 1, other side `c·w` (left taps: L = 1, R = c·w; right taps mirrored), all multiplied by
  `(1 − (1001^(1 − 1/Σ|gains|) − 1)/1000)·v`.
- LFO depth (samples) `P0 = (A·w + 10)/r`; centre `P3 = P0 + max(mix(C·w·dV, C)/r, 8)`; LFO increment per sample
  `round(mix(B·dV/(A·w+10), B/(A+10))·r)` on a 32-bit phase (start phase `0` or `2³¹`, plus `round(w·σ·2²³)·256`).
- LFO shape: with phase φ as signed 32-bit and `Q = √(4·P0)`: `x = |φ·Q/2³¹| − Q/2`, delay
  `D = x·(Q − |x|) + P3` — a parabolic (sine-like) sweep of ±P0 around P3. Tap value = linear interpolation
  `s[n−k] + (D − k)·(s[n−k−1] − s[n−k])`, `k = ⌊D⌋` (the code uses the CPU conversion; EXEC fits `⌊D⌋`
  at −117 dB vs −57 dB for round-to-nearest — the exact rounding state inside Gen_Render is **not confirmed**).
- Output `T_L = Σ tap·gainL`, `T_R = Σ tap·gainR` (no dry). Phases free-run with the instance; they are reset
  only when the ring length or spread changes (`FUN_00840fc0`).
- EXEC (orders 1, 3, 4, 9; 44.1/48 kHz; and at 4× FX rate): −104…−124 dB.

The unit's two articulators (panning, volume) and the main-block **pan** act per voice on the FX-column send
(`FUN_00872290`, voice code): EXEC shows the FX pan int `p` shifts the FX-bus pan by `p/128` with the same
`cos/sin((1 + pan)·π/4)` law as the outputs (p = 64 → 0.3827/0.9239, p = 128 → hard right).

### 7.4 Delay units **[DECOMP `FUN_00869540`, `FUN_0083d140`, `FUN_0083d510/640/760`, `FUN_0083d480`, `FUN_0062bb20`; EXEC]**

Record (`P+0x5F30 + 0x38·k`, state 23 bytes): ints feedback (0…127), time (0…384), stereo offset (±512),
volume (±128), mode (0 normal, 1 "inverted", 2 ping-pong; DFM `DelModeSelect`), then bytes
**tempo, serial, on** (corrects round 11's "on, tempo, serial" order: byte 22 enables the unit, byte 20 is the
tempo flag). Laws (fs_fx = FX rate, bpm = FL tempo from `FPE_Tempo`, default 120):

- Delay `D = round(time·fs_fx/384)` samples (0…1 s), or with tempo `round(time·fs_fx·0.3125/bpm)`
  (= time/192 beats).
- Stereo offset: `o = round((1001^(|v|/512) − 1)·50·fs_fx/100000)` samples (≤ 0.5 s) added to the **right**
  channel's write position when `v > 0`, to the left when `v < 0`.
- Feedback `fb = v/128`, applied through a block-linear ramp limited to `0.001·44100/fs_fx` per sample; a new
  unit starts from 0.5 (EXEC; the constructor's default target is 0.5 — inference for why).
- Volume `g = sign(v)·(11^(|v|/128) − 1)/10`.
- Per sample: `y = ring[r]`, `ring[r] = 0`, `ring[r + D] += fb·(y or swapped)`, `r++`; input added at
  `ring[w + o_c]` with `w = r + D` before processing. Mode 0: out = (L, R), feedback (L, R); mode 1: out =
  (R, L), feedback (L, R); mode 2: out = (R, L), feedback (R, L). Hence `y[n] = x[n−D] + fb·y[n−D]` per path.
- EXEC: 10 cases (modes, offsets ±, tempo, serial chains, 44.1/48 kHz): −141…−154 dB.

### 7.5 Reverb **[DECOMP `FUN_00869840`, `FUN_0083fad0`, `FUN_0083f8e0`; Reverb_x64 `FUN_180002980`; EXEC]**

State (37/38 bytes): ints lowcut (19…3000; 19 = off), highcut (5…221; 221 = off), predelay (0…384),
room (1…100), diffusion (0…100), decay (1…200), damping (5…221; 221 = off), colour (0…4), wet (±128);
bytes on, tempo **[DATA + DFM `Rev*`]**. With `fs_fx`, `ln0.001 = −6.9078`:

- Input: one-pole low-pass `a += c_hc·(x − a)` with `c_hc = 1 − (y − √(y² − 1))`, `y = 2 − cos(2π·100·hc/fs_fx)`
  (highcut in **hundreds of Hz**; off → 1); then, if lowcut on, a first-order allpass with
  `c_lc = (tan(π·lc/fs_fx) − 1)/(tan(π·lc/fs_fx) + 1)` (lowcut in **Hz**) and `a ← (a − allpass(a))/2`
  (a high-pass).
- Line lengths: `S = ⌊fs_fx·(room + 15)/115⌋`, `L_i = ⌊t_i·S/20000⌋` with `t = 142, 107, 379, 277` (4 input
  allpasses), tank `672, 4453, 1800, 3720` (half 1: allpass, delay, allpass, delay) and `908, 4217, 2656, 3163`
  (half 2); output taps `266, 2974, 1913, 2496, 1690, 187, 1566` (left) and `353, 3627, 1728, 3173, 1811, 335,
  621` (right), same scaling. (These are the well-known plate-reverb proportions; **inference**.)
- Diffusion `δ = dif/100`: input allpass coefficient `exp(ln0.001·(L0+L1)/(2fs_fx)/(0.07δ))`, tank allpass 1
  `exp(ln0.001·(L4+L8)/(2fs_fx)/(0.35δ))`, tank allpass 2 `exp(ln0.001·(L6+L10)/(2fs_fx)/(0.51δ))`; δ = 0 → 0.
- Decay: `g = exp(ln0.001·(L5+L7+L9+L11)/(decay·fs_fx/10))^¼` (decay in tenths of a second) — loop gain per
  delay. Colour factor `c = {0.2, 0.6, 1, 1.5, 3}[colour]`: `ρ = exp(ln0.001·ΣL/(decay·fs_fx/10·c))^¼ / g`,
  shelf amount `(ρ − 1)/2`, then `ρ ← min(ρ, 1)` and a first-order allpass at 500 Hz:
  `c44 = (tan(500π/fs_fx) − ρ)/(tan(500π/fs_fx) + ρ)`; damping one-pole like the highcut (×100 Hz).
- Predelay `round(pre·fs_fx/384)` samples (or `·0.3125/bpm` with tempo), minimum 1.
- Each tank half: `u = (other half's last delay)·g`; `v = damp_LP((allpass_c44(u) + u)·shelf + u + predelayed
  input)`; allpass(c_ap1) → delay → ×g → allpass(c_ap2) → delay. Outputs: left = `t0(h2 d) − w6c·t1(h2 d) −
  w78·t2(h2 ap) − w68·t3(h2 last) + w74·t4(h1 d) + t5(h1 ap) + w70·t6(h1 last)`, right symmetric with the halves
  swapped, where `w_k = (1 − g)·k + g` for k = 0.6 (w74), 0.25 (w6c), 0.7 (w78), 0.2 (w68), 0.5 (w70).
  (Exact ring/tap indexing in `fx_model.Reverb`.)
- Wet gain `wet/256`; reverb input is the FX bus only.
- EXEC: 8 cases incl. all-off, extremes, tempo predelay, 44.1/48 kHz, 4× FX rate: −100…−153 dB.

### 7.6 Three-band EQ **[DECOMP `FUN_0086a440`, `FUN_00625990`, `FUN_006253c0`, `FUN_00625db0`, `FUN_008736f0`; EXEC]**

Mode int (DFM `EQModeSelect`: 0 none, 1 out+effects, 2 out only, 3 effects only) — placement as in §7.1.
Band `[level, freq, width, type]`: gain **level/100 dB** (±18 dB), `f = (101^(freq/65536) − 1)·159.9 + 10` Hz
(10 Hz…16 kHz, clamped to 0.4999·fs_fx), `w = width/65536`, type 0 low shelf, 1 peaking, 2 high shelf
(internal kinds 6/7/8). Biquads in the usual cookbook form with `A = 10^(dB/40)`, `ω = 2πf/fs_fx`:
peaking `α = sin ω/(2Q)`, `Q = (1001^w − 1)·0.0999 + 0.1`; shelves with
`β·sin ω`, `β = √((A² + 1)/(1.9w + 0.1) − (A − 1)²)`. Direct form I in double precision, state rounded to float
per block. A band with level 0 is processed for `fs_fx/5` samples after a change and then bypassed.
EXEC (6 band sets, 3 placements, 44.1/48 kHz): −117…−142 dB.

### 7.7 DC stage (global byte `P+0x4F5`) **[DECOMP `FUN_008629d0`, `FUN_00628dd0`; EXEC]**

At the host rate, per channel: `v += (x − s)·a; s += v; v ·= 0.96; y = x − s`, `a = 1.0070235/fs`
(a resonant second-order DC/sub-sonic remover). Enabled in 81/268 demo channels **[DATA]**.
EXEC: −185 dB (44.1 and 48 kHz).

### 7.8 Not confirmed / not covered

- FL send (§7.1 step 6): structure and gain law DECOMP only.
- The list at `+0x63a0` advanced in Gen_Render by `round(len/PPQ·x)` (tempo-synced positions, likely the
  articulators' tempo LFOs) — not analysed here.
- Integer conversion mode inside Gen_Render (chorus `⌊D⌋` fits EXEC; the code calls a plain `cvtsd2si`, and the
  MXCSR loaded at the start of Gen_Render, 0x9FC0, is round-to-nearest with flush-to-zero and denormals-are-zero)
  — the discrepancy is unexplained. (The voice code's roundings — Delphi `Round`, x87 `frndint` — were all matched by
  EXEC with round-to-nearest-even, §2/§8.)
- Why a fresh delay unit's feedback ramp starts at 0.5 (EXEC fact; mechanism inferred).


---

## 8. Verification

### 8.1 Synthetic voice-core battery **[EXEC + model]** (`r31/py/battery_core.sh` → `battery_core.out`)

Each case is rendered by the installation's `Sytrus_x64.dll` through `run/syt.exe` and by `sytmodel.render` from the
same state; the figure is the relative error `20·log10(rms(model − DLL)/rms(DLL))` per channel.

| Script | What it covers | Result |
|---|---|---|
| `t_core0.py` | single operator: key, 44.1/48/96 kHz, ratio, offset Hz, start phase, volume ±, pan cell | −111…−152 dB |
| `t_core1.py` | FM 2→1 / 1→2, feedback 200 and 256, 3-operator chain, two-way FM, RM ±, noise 128 / 40 + FM, two operators with pans, master level/pitch, saw | −118…−184 dB (two-way FM: see 8.3) |
| `t_core2.py`, `t_globphase.py` | BAND LIMIT (keys 36…100, 44.1/48 kHz, GIBBS OFF), GLOBAL phase (notes starting at blocks 0/3/5), shapes | −137…−184 dB |
| `t_core3.py` | PLUCK (sine/saw excitation, 44.1/48 kHz, with FM) | −133…−182 dB |
| `t_core4.py` | CENTER, DECLICK, global declick amount with velocity | −146…−184 dB |
| `t_uni.py`, `t_uni2.py` | unison n = 2…9, sub, phase, pan, volume, modes b = 0…3 | −117…−183 dB |
| `t_group.py`, `t_group2.py` | OSC-grouped unison, with FM | −120…−145 dB |
| `t_rel.py` | note-off without envelopes (gain ramp and voice end), 44.1/48 kHz | −122…−136 dB |
| `t_full1.py` | envelopes on FM operators, pluck damp envelope, MOD + pitch envelopes, unison envelope variation | −126…−182 dB |
| `t_filt_int.py`, `t_lfo_p1.py`, `t_choc.py` | operators → filters (types 0/1/3/4/11/12, cutoff envelope, cutoff LFO, chain, waveshaper with tension, filter volume envelope, Chocolate gain mixes) | −109…−187 dB |
| `t_os.py` | oversampling ×2/×4, render mode (×8, tick 8), FX-column send into the chorus | −113…−145 dB |
| `t_porta.py` | portamento modes, attack scaling, colour 14 | −131 dB |
| `dbg8.py` | two-way FM, maximal feedback, 3-op chain **with the DLL's own tables** | −185 dB |

The sub-analyses' batteries (articulators `test_art.out`, 31 cases −119…−165 dB, re-run after the integration changes
to `art_model.py`; filters `test_filt.out` 366 cases ≤ −100 dB, `t_dyn2.out`, `t_ws.out`, `t_chain.out`; post chain
`test_fx.out` 47 cases −98…−185 dB) are summarised in §4–§7.

### 8.2 The 268 demo channels, whole instance **[EXEC + model]** (`r31/py/battery_demo.py` → `battery_demo_all.out`)

Every Sytrus channel of the demo projects (state blobs from `r14/blobs14.pkl`, loaded unchanged through the plugin's own
SaveRestoreState) plays key 60 (C5), velocity 0.78125, tempo 120, 22 050 samples at 44.1 kHz with note-off at 15 435,
host block 512, live mode (each patch's own live oversampling/tick settings). The model renders the same state with
`sytmodel.render(…, arts_cls=FullArts)`; the Delphi `RandSeed` read from the DLL just before TriggerVoice is given to
the model (random maps). Two figures per channel: the model with its own wavetables, and the model fed the DLL's own
operator tables (dumped from the running voice; band-limited copies are always built by the model) to separate the
IPP sine generator effect of §8.3.

| Measure (268 channels, 175 use filters) | own tables | DLL operator tables |
|---|---|---|
| median relative error (L) | **−116.8 dB** | **−118.2 dB** |
| 10th / 90th percentile | −139.8 / −97.6 dB | −143.5 / −98.2 dB |
| best / worst | −186.1 / −33.9 dB | −186.1 / **−63.9 dB** |
| channels worse than −60 dB | 3 (ch. 27, 65, 115) | **0** |
| channels worse than −100 dB | 37 | 31 |

- The three channels above −60 dB with our own tables are chaotic FM patches (27: self-feedback 256 plus a two-way
  1⇄2 loop; 65: two-way 1⇄2 loop; 115: self-feedback 256 on a noise operator): with the DLL's operator tables they
  reach −144, −112 and −110 dB (§8.3).
- The weakest channel with DLL tables (212, −63.9 dB) has a −230 self-feedback on an operator modulated by five others
  and **band-limited** operators everywhere, whose tables the model always builds itself (its FFT differs from IPP's in
  the last bits), so the same chaos mechanism applies; 225/226 (−71/−77 dB) are noise-heavy resonant Vanilla HP
  patches with reverb.
- Right channel: median −118.6 dB, worst −65.6 dB, 0 above −60 dB.

**FL-render settings** (`battery_render.py` → `battery_render.out`): 14 demo channels rendered with the dispatcher's
`PM_IsRendering` flag (so each patch's *render* oversampling and tick apply: 12 of them run 8× oversampled with an
8-sample tick, two at 1×/96), 11 025 samples, note-off at 8 000, against `sytmodel.render(…, render_mode=True)`:
**−83.0 … −144.3 dB** (median −121 dB). This also exercises the 16 384-point band-limited tables of render mode, the
decimators of §7.2 and the FX at 4× rate.

Fixes found by this battery and folded into the sections above (each re-verified on the channel that exposed it):
filter volume ramp start (§5.5, ch. 23), random map with the DLL seed (§4.6, ch. 17), clamp only on the operator copy
(§4.7, ch. 17), band-limited table length 4096/16384 (§2.4, ch. 17), GLOBAL PITCH (§6.4, ch. 162/163), sequential phase
accumulation in the wave generator (§8.3, ch. 163), "default" map test (§4.6, ch. 202), Chocolate gain ramps (§5.8,
ch. 19), waveshaper mix ramp (§5.5, ch. 63/105), GLOBAL LFO start phase (§4.5, ch. 262/217), unison modes 2/3
(§6.3, ch. 9), filter mode OFF for the band-stop type (ch. 218), pluck noise generator (§2.6, ch. 44/71/122…231),
FX-unit panning articulator additive (§4.2).

### 8.3 Precision notes **[EXEC + model]**

- **IPP sine tables.** Pure-sine operators (no shape, skew or sine-shaper) are built with `ippsTone_Direct_32f`; its
  values differ from an exact sine by up to ~6·10⁻⁸ (about a quarter of the entries by one float step). In
  non-chaotic patches this is far below −120 dB. In **chaotic FM** (two-way FM loops, strong self-feedback) any
  difference grows exponentially: with our sine the "two-way FM" and "feedback 256" cases diverge after a few hundred
  samples, with the DLL's tables they match at −185 dB (`dbg8.py`). An independent implementation cannot be
  sample-exact in those patches (inference: neither can FL across CPUs with a different IPP code path); it is
  statistically equivalent.
- **Wave generator phase.** The per-partial phase is accumulated sequentially in double precision and wrapped with
  "while φ > period: φ −= period" (so a phase landing exactly on the period is *not* wrapped). A closed-form phase
  moved the discontinuity of a shaped partial by one sample in some tables (−53 dB on one demo table); the sequential
  form is exact. The partial sums are accumulated in float, then the table is scaled by `1/peak` (float multiply).
- **x87 kernels.** The per-sample operator and the pluck kernel keep intermediates in x87 registers; modelling them
  in double precision (53-bit) matched; single-precision intermediates did not (`dbg4.py`).
- **Global states.** The noise LCG (`0x911c30`) and `RandSeed` (`0x8a2010`) are process-wide: the result of a noise
  operator or random map depends on everything that ran before in the same FL process (the harness starts from the
  DLL's load state; `RandSeed` is clock-seeded at load).

---

## Summary table (question → result)

| # | Question | Result | Evidence |
|---|---|---|---|
| 1 | Operator frequency | `2^(p/1200)·(ratio/20000)·523.2511/fs_v·det + offset/(100·fs_v)`, × pitch-articulator `2^(2·p2·out/1200)` (× operator 1's with GLOBAL PITCH) | DECOMP + EXEC |
| 2 | Operator table | 16 384-point round-23 wave + guard, **linear interpolation**; BAND LIMIT → 4 096 (live) / 16 384 (render) band-limited copy with a linear fade between `(18000+d)/F` and `(18000+3d)/F` harmonics | DECOMP + EXEC |
| 3 | Phase | start `p0/256 + unison + phase-art (+ counter·f with GLOBAL)`; phase articulator as frequency offset Δ/T | DECOMP + EXEC |
| 4 | Noise / pluck / CENTER / DECLICK | block `+0x008` = noise `g(v/128)` (global LCG); PLUCK = filtered recirculating line of `88200/F` entries; DC remover; attack smoother | DECOMP + EXEC |
| 5 | FM / feedback / RM / MOD | PM `4·g·out·MOD` cycles (feedback `g·35/128`); RM `Π(1 − |g| + g·out)`; per-sample index order (lower index = same sample) | DECOMP + EXEC |
| 6 | Articulators | tick-rate, 0.5 s units (1 beat with TEMPO), additive for arts 0/3 of every unit, filter 2/3, op 4; maps default = flat neutral; random map = Delphi Random | DECOMP + EXEC |
| 7 | Volume and output | linear `art₁·p1/256` ramped per tick (≤ 0.01·44100/fs_v per sample); pan law `cos/sin((1+pan)π/4)`, gains ramped per sub-block (≤ 0.001·44100/fs_v) | DECOMP + EXEC |
| 8 | Filters | 13 types, modes/ALT, laws of §5.4, articulator ramps, waveshaper | DECOMP + EXEC |
| 9 | Unison | weights, pans, detune tables (4 modes), phases, sub layer, OSC grouping | DECOMP + EXEC |
| 10 | Voice end | released → finished when all active articulators end → gain ramp → freed | DECOMP + EXEC |
| 11 | Portamento / MONO KEY / GLOBAL PITCH | glide via the pitch envelope's start offset Δ/2400; kill same key; op 1 pitch for all | DECOMP + EXEC (MONO KEY DECOMP) |
| 12 | Internal rate | voices at `fs·2^order` (order live/render ints), control tick 96/8-based; FX at `fs·2^min(order,2)` | DECOMP + EXEC |
| 13 | Post-voice FX | chorus, 3 delays, reverb (Shared/Reverb_x64), EQ, DC, mixer send — §7 | DECOMP + EXEC (send DECOMP) |
| 14 | Demo channels | 268/268 within −60 dB (median −118 dB) with the DLL's operator tables; 265/268 with our own (the 3 others are chaotic FM, §8.3); FL-render settings on 14 channels −83…−144 dB | EXEC + model |
| 15 | Polyphony limit | `FPE_MaxPoly`, oldest/released group killed | DECOMP only |

---

## 9. For LYNX

**Goal:** replace the Sytrus → 3-oscillator approximation (`src/core/FlSytrus.cpp`, `synthFromSytrus` and
`operatorSpectrum`) with a dedicated Sytrus engine, as done for Sakura/Drumpad. The reference implementation is
`r31/py/sytmodel.py` + `art_model.py` + `filt_model.py` + `fx_model.py` + `sywave_np.py` + `ipprand.py`
(state reading: `sytstate.py`); port it 1:1 and compare against the Python output (same state, notes, block size,
44.1 kHz) — target ≤ −100 dB except chaotic-FM patches (8.3).

1. **Instance** (one per Sytrus channel): parse the state (round 11 layout; `sytstate.parse` is byte-exact on all
   demo blobs). Choose `order` and the tick index from main ints `0x30C/0x304` for playback and `0x310/0x308` for
   offline export (FL renders most demo patches 8× oversampled with an 8-sample tick, §1.2); `fs_v = fs·2^order`,
   `T = 4·round(T₀·fs_v/44100/4)`, T₀ = 96 or 8. Keep per instance: the noise LCG state, a `RandSeed` (seed it once
   from the clock, as FL does — log that random maps are not reproducible), the GLOBAL LFO phases (`0xC0000000` after loading,
   advanced per block by `round(n·K/T·inc)`), the running sample counter (GLOBAL operator phase), the last pitch
   (portamento), the post chain `fx_model.SytrusPost(cfg, fs, order, block, dc)` (`sytmodel.fx_cfg`).
2. **Note on** (per FL note): `p = Final.Pitch + 100·main[2]` cents (0 = C5 = 523.2511 Hz at ratio 1); velocity =
   FL note velocity 0…1 (FPV_GetVelocity); create `n` (×2 with sub) voices with the unison parameters of §6.3
   (`sytmodel.unison_params`); MONO KEY / MaxPoly kills per §6.4; portamento per §6.4; OSC grouping per §6.3.
3. **Voice set-up** (`Voice._setup`): articulators note-on (§4, `FullArts`), operator lists (§1.1, §3.1), per-operator
   frequency/phase/volume/MOD initial values (§2), filter units (§5), output gains start on target.
4. **Per host block**, per voice, per sub-block (tick ∩ block): at a tick start evaluate the articulators and set the
   ramps (`Voice._tick`); render the per-sample operators in index order with double-precision intermediates
   (`_persample`), the others with the 4-lane block reader (`_block`), apply CENTER/DECLICK, the filter units
   (`_filters_sub`), and add OUT/FX buffers with the ramped pan-law gains (`_mix`, `_mixfx`). Then the post chain,
   decimation and DC stage (§7). Voices end per §6.2.
5. **Tables:** generate the 16 384-point wave per operator with `sywave_np.wave` (sequential phase accumulation,
   float partial sums, peak normalisation, tension, absolute, pre-filter/Gibbs), cache by the operator block bytes;
   band-limited copies per (lo, hi) with an FFT (4 096 or 16 384 points). For sine-only operators any accurate sine
   is fine (IPP's tone generator is not reproduced, §8.3).
6. **Exactness that matters audibly:** the linear (not squared) volume law, the 0.5 s envelope unit, the per-sample
   FM order, the ramps (tick for volume/MOD, sub-block for pan gains and filters), the gain ramp-out at note-off (the
   ~16 ms "declick" of envelope-less patches), the 4 096-point band-limited tables, the additive/multiplicative
   articulator modes and the neutral map values.
7. **Approximations LYNX may choose** (log them): skip the post chain's oversampling when `order` is 0; run live
   settings for export (FL's render settings differ, §1.2) — then document that renders differ from FL's export.
8. **FX (from the post-chain analysis):** port `fx_model.py` as-is: `SytrusPost(cfg, fs, order, block, dc)` per
   instance, fed per host block with the voices' summed OUT (M) and FX (F) buses at `fs·2^order` and the "FX active"
   flag (voices present or tail counter > 0); keep float32 storage for the rings and the chorus LFO; delays, reverb and
   EQ tolerate double arithmetic (≤ −100 dB). The mixer send (main int `0x2F4` ≠ −1, §7.1 step 6) maps to LYNX's
   send to the following mixer track (round 18 §5) — DECOMP only.
9. Remove "Sytrus: approximated by 3 oscillators" from STATUS once the C++ engine matches the Python reference on the
   demo battery (`battery_demo.py` can be re-pointed at the LYNX renderer's output).

---

## Evidence index

| Source | What it established | Marker |
|---|---|---|
| `FUN_008737a0` (Gen_Render) | oversampling factor, voice loop order, global LFO/counter advance, post chain | DECOMP + EXEC |
| `FUN_0086c7d0` (TriggerVoice) | voice events 2/3/6, porta flag, MONO KEY, MaxPoly, unison parameters (tables `0x8fcad8`, `0x8fcb68`) | DECOMP + EXEC |
| `FUN_0086af20` (voice init) | articulator note-on, global ADSR/velocity/porta options, operator/filter set-up, declick count, noise, pluck | DECOMP + EXEC |
| `FUN_0086dd40`, `FUN_008650d0`, `FUN_00864ff0`, `FUN_00865120`, `FUN_008652a0` | matrix lists, amounts (4·g, 35/128), RM (g, 1−|g|), columns, activity | DECOMP + EXEC |
| `FUN_00872290` | tick loop, articulator delivery, MOD/pitch/phase/damp/volume ramps, output pan law, voice end, OSC grouping | DECOMP + EXEC |
| `FUN_00870820` | render loop: wraps, per-sample/block split, DC/declick lists, filters, mixing | DECOMP + EXEC |
| `FUN_00870580` (x87) / `FUN_00877270` (AVX twin) | per-sample operator, noise, pluck, RM/FM order | DECOMP + EXEC |
| `FUN_00870760`, `FUN_00860f10` | block operator reader (4 lanes) | DECOMP + EXEC |
| `FUN_00872210`, `FUN_0086a730`, `FUN_00869e30`, `FUN_008629d0` | frequency law, start phase, pitch range, base 523.25/fs | DECOMP + EXEC |
| `FUN_0086ace0`, `FUN_008471d0`, `FUN_00848980`, `FUN_00848a50`, `FUN_00847df0` | table attach, band limit, FFT sizes 2¹⁴/2¹², wave generator phase accumulation | DECOMP + EXEC |
| `FUN_0086a810`, `FUN_0086aba0`, `FUN_00628c90` | pluck line, damping, fixed-point read | DECOMP + EXEC |
| `FUN_0086a630`, `FUN_00628e60` | declick amount, CENTER filter | DECOMP + EXEC |
| `FUN_008657b0`, `FUN_00862b80`, `FUN_00861360` (Dispatcher 1/2) | tick table, oversampling order, render mode, flush | DECOMP + EXEC |
| `FUN_007797a0`, `FUN_00867890` (ProcessEvent) | MaxPoly, tempo | DECOMP |
| `FUN_0062bc50`, `FUN_0062bb20`, `FUN_0062bbf0`, `FUN_00616e20`, `FUN_00629740`, `FUN_00629870`, `FUN_00629f70`, `FUN_006299e0`, `FUN_00616f70`, `FUN_00616db0` | pan law, ramps, stereo adds, ramped multiplies | DECOMP + EXEC |
| `FUN_0078acd0` (TMMap slot 16), constructor masks `0x867210…0x867214` | default-map test, additive articulators | DECOMP + DATA + EXEC |
| `FUN_0078a5b0`, `FUN_008658f0`, list `P+0x63a0` | GLOBAL LFO phase | DECOMP + EXEC |
| `FUN_0040a030`/`FUN_00409ff0` (`RandSeed` `0x8a2010`) | random map | DECOMP + EXEC |
| `dsp_ippv2_x64.dll` `ippsRandUniform_Direct_32f` (generic path VA `0x1815ed2e0`) | pluck noise generator (`ipprand.py`) | DECOMP + EXEC |
| Sub-analysis sections §4, §5, §7 (functions listed there) | articulators, filters, post chain | DECOMP + EXEC |
| DFM `TSynthEditorForm` (`r11/sytrus.dfm.txt`) | control names (BAND LIMIT, DECLICK, PLUCK, CENTER, GLOBAL, OSC, MONO KEY, GLOBAL PITCH, RANDOM, SMOOTH, accuracy buttons) | STR |
| `r14/blobs14.pkl` | flag/value statistics, neutral map values, demo battery | DATA + EXEC |
| Manual `…/plugins/Sytrus.htm` | filter mode/type descriptions (§5) | DOC |

### Open questions

- Host side: FL's real values for voice event 5 (release value) and the "link velocity" semantics when Sytrus does
  not send event 6 (byte `P+0x4FC`); which UI setting FL maps to `FPE_MaxPoly`.
- The FL mixer send of the FX bus (DECOMP only), the reset point of the GLOBAL LFO phase and of the GLOBAL operator
  counter other than `FPD_Flush`.
- MONO KEY and the polyphony limit were not exercised by EXEC.
- Curve modes 10–12 and the "arp" point flag (absent from the demos); Mod X/Y "SMOOTH" (`P+0x440`, coefficient
  `0.01^(T/96·…)` at main int 192) read but not exercised.
- `ippsTone_Direct_32f` values (only matters for chaotic FM, §8.3); the AVX operator kernel `FUN_00877270` was not
  compared (the test machine selects the x87 one).
