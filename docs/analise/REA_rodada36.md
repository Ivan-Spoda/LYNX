# Behavioural analysis of FL Studio 2026: round 36 (REA) — Harmless: the effects rack (chorus, delay, reverb, compressor = Maximus)

> **Nature of this document.** Continuation of `REA_rodada32.md` §1.20 (Harmless, effects rack "not decoded"), with
> material shared with `REA_rodada31.md` §7 (Sytrus post-voice chain: same chorus/delay library, same shared reverb
> DLL), `REA_rodada8.md` §1–2 (Maximus/Soundgoodizer state format) and `REA_rodada35.md` §7 (Vocodex hosts the same
> Soundgoodizer DLL). *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied:
> what is here are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was
> copied into the repository (Harmless's four compression presets are read at run time from the user's installation;
> the tests use Maximus states we built ourselves).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLLs, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences are stated explicitly as **inference**. Results of our own numerical models (which implement only what is
> marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files, or a table read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (hashed in this round):

  | Module | File (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Harmless | `Plugins/Fruity/Generators/Harmless/Harmless_x64.dll` `4100bf85ad29dc665051a73ef62017272a25aefbda1cf2f1ace39e6be376440b` (= rounds 10/21/24/27/32) | Delphi | Gen_Render `FUN_007886b0`; ProcessParam `FUN_0077f630` (cases 0x5a…0x62); rack set-ups: chorus `FUN_00789570`, delay `FUN_00789080`, reverb `FUN_00789280`, compressor `FUN_00789800`; library: chorus `FUN_0074a790` (ctor), `FUN_0074a9b0` (set-up), `FUN_0074b0b0` (phase advance), `FUN_0074b0f0` (ring write), `FUN_0074b2f0` / `FUN_0074b180` / `FUN_0074b230` (x87 taps), `FUN_0074b7e0` / `FUN_0074b440` / `FUN_0074b600` (SSE taps); delay `FUN_007471d0` (ctor), `FUN_00747070`/`FUN_00747010` (ring), `FUN_00747510` (input add), `FUN_007475a0` / `FUN_007477f0` (x87 normal / ping-pong), `FUN_007481f0` / `FUN_007484b0` (SSE twins); reverb wrapper `FUN_0074a360`, `FUN_00749540` (loads the shared DLL), `FUN_00749b60` (coefficients), `FUN_0074a540` (process); ramp `FUN_00612db0`; hosted-plugin loader `FUN_0076f2c0`, preset loader `FUN_0076f7f0`, data-file request `FUN_0073f1e0` |
  | Shared reverb | `Shared/Reverb_x64.dll` `9881ff2ccd74227e454a3d5f1912568f4336abe06e68f3693162aa4d251c969f` (= round 31) | C++ (MSVC) | process `FUN_180002980` (round 31 §7.5) |
  | Soundgoodizer | `Shared/SG_x64.dll` `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac` (= round 35) | Delphi (`TTestPlug`, VMT `0x83e4d8`) | CreatePlugInstance `0x83e620`; VMT slots 1 Dispatcher `FUN_0083e670`, 3 SaveRestoreState `FUN_0083ef50`, 6 ProcessParam `FUN_00841b40`, **7 Eff_Render `FUN_00843a40`**; set-ups `FUN_00844e10` (crossover), `FUN_00840c80` (times), `FUN_008417e0` (modes), `FUN_00841380` (saturation), `FUN_00845b50` (curve → gain table), `FUN_00840b40` + `FUN_0083a5c0` (low cut), `FUN_00847ce0` (oversampling), `FUN_00845940` (reset), `FUN_00840a00` (latency); kernels `FUN_00624580` (biquad design), `FUN_00624e90` (biquad), `FUN_00628b80` (stereo matrix), `FUN_00605410` (control smoother), `FUN_00629710` + `FUN_007806b0` (peak detector), `FUN_0077f350` (lookahead delay), `FUN_0062a240` (saturation A), `FUN_0083a9c0` / `FUN_0083ab90` (2:1 up/down), `FUN_00627cc0` (state rounding), `FUN_00629890` (silence test); envelope `FUN_007876c0` (iterator start), `FUN_007885d0` (step/evaluate), point preparation in `FUN_00785xxx` (`0x7854f0`…) and `FUN_0060dfd0` (tension record) |
  | Compression presets | `Plugins/Fruity/Generators/Harmless/Data/Compression/1.fst` `3dc934f3…4d74`, `2.fst` `5224cc29…eb12`, `3.fst` `b9c7ad76…a5a`, `4.fst` `1cb33a3b…73b9` | FL preset (Maximus state) | read in place [DATA] |
  | Harmor (comparison only, §4) | `Plugins/Fruity/Generators/Harmor/Harmor_x64.dll` `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` | Delphi | byte search only (no decompilation in this round) |

- **Tools:** Ghidra 12.1.4 headless with a new project `/tmp/r36gh_SG` for `SG_x64.dll` (automatic analysis;
  dumps `r36/out/sg_plug.c` = `0x82c000…0x864000`, `sg_rec.c` (recursive from the VMT slots), `sg_env.c`
  (`0x781000…0x78b000`), `sg_rt.c`, `sg_rt2.c`, `sg_f2.c`, `sg_f3.c`, `sg_lc.c`, `sg_t.c`); the round-32 Harmless dump
  `r32/out/plug_all.c` plus `r36/out/d1.c` (functions outside it, from `/tmp/r32gh_H`); round 31's reverb dump
  `r31/out/rev_all.c`; `objdump` for x87/SSE details; constants with `r18/k.py`; IPP names with `r27/py/ippmap.py`
  (Harmless) and our `r36/py/ippsg.py` (SG's dispatcher `FUN_005bbf20`, dump `r36/out/sg_ipp.c`); `r36/py/fn.py`
  (function extractor).
- **EXEC harnesses** (our own C, Zig 0.14.1 → Windows x64; `SetErrorMode` first, vectored fault handler,
  `TerminateProcess` at the end, `timeout`; nothing was left running):
  - `r36/run/hl36.c` → `hl36.exe`: the round-32 Harmless harness plus (a) **answers to the host's data-file requests**
    (dispatcher query `0x47` returns an interface whose method 6, request `0x3fc`, returns the installation path of the
    requested `Harmless\Data\…` file; dispatcher `0x1d` returns the plugin folder), (b) peeks `fp=` (run-time function
    pointers), `gd=` (globals), `pb=` (a byte behind a pointer). Without (a) the plugin cannot open its compression
    preset and the hosted Soundgoodizer keeps its own default state — **the "peaks near 0.976" measured before this round
    came from that default state, not from Harmless's presets** (EXEC: preset 2 on a test patch peaks at 0.880 with the
    file answered, 0.976 without).
  - `r35/run/vcx_*.exe` (round 35's generic effect harness): hosts `SG_x64.dll` standalone — restores a state through its
    SaveRestoreState, sets parameter 5 through ProcessParam, renders through Eff_Render; `pp=`/`pd=`/`fp=`/`gd=` peeks.
- **Models (pure Python + numpy, `python3 -I`):** `r36/py/rack.py` (chorus, delay, reverb, the Gen_Render rack
  order; the reverb is round 31's model with Harmless's parameters), `r36/py/maxmodel.py` (the Maximus engine as used by
  the presets), `r36/py/mxstate.py` (`.fst` / Maximus state reader), `r36/py/mkstate.py` (our own Maximus states for
  tests). Drivers: `hx.py`, `sgx.py`; checks `t1.py` (Harmless compressor ≡ SG), `t2.py` (rack units),
  `maxtest.py` (Maximus vs SG), `t48.py` (48 kHz, other blocks), `syntest.py` (our own states), `batrack.py` → 
  `batrack_final.out` (139 demo channels, whole channel), `cpptest.py` (C++ port vs model), `dbg137.py`.
- **C++ checks:** `r36/cpp/rackcmp.cpp` (LYNX's `hlfx::Rack` on a file), `r36/cpp/chanrec36.cpp` (the previous agent's
  channel recorder with the installation's files readable), `r36/py/dllcmp36.py` (recorded LYNX channel vs the DLL).
- **Manual:** `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Harmless.htm`
  (abbreviated `…/plugins/Harmless.htm`).

---

## 1. The rack inside Harmless

### 1.1 Order of operations in Gen_Render **[DECOMP `FUN_007886b0`]**

The voices are mixed into a float stereo buffer M (round 32 §1.1). Then, per host block of n samples:

1. **Tail counter** (`+0x7b8`): when at least one voice record exists at the start of the block it is reloaded with
   `+0x7bc`; otherwise it decreases by n (if positive). `+0x7bc = round(0.004·fs) + (chorus ring length in frames)`
   (`FUN_00789570`; the ring is `host block size + chorus maximum delay`, §1.3) **[DECOMP; EXEC peek: 5421 at 44.1 kHz
   with chorus type 3 and 512-sample blocks]**. "Active" below means counter > 0.
2. **Chorus** (object `+0x7c0`, if its type > 0): active → M is written into the chorus ring, the taps produce T, M is
   multiplied by the dry gain `+0x7c8` (when ≠ 1) and `M += T`; not active → only the tap phases advance by n.
3. **Delay** (object `+0x7d0`, type > 0): active → `T = M·vol` (`+0x7e4`) is added into the delay ring at the write
   position (+ the stereo offset of each channel); then always: the delay produces T, `M += T`.
4. **Reverb** (object `+0x7f0`, type > 0): input `L + R` (no ½, run-time pointer `0x7ff590` → `FUN_006119f0`
   [EXEC]), the shared reverb renders stereo T, `M += T·wet` (`ippsAddProductC_32f`, `wet = +0x7f8`).
5. **Compressor** (list `+0x800 → +0x128`, non-empty when the compression type > 0): every hosted plugin processes M in
   place (its Eff_Render, §1.6).
6. Phases/counters advance and M is added to the host buffer (round 32).

So the rack runs on the sum of all voices, after the per-voice tremolo and level law, i.e. on a signal that already
includes the channel volume (Final.Vol) **[DECOMP]**. EXEC confirms the order: model (voices + this rack) vs the DLL
with every unit on: §3.

### 1.2 Parameters **[DECOMP `FUN_0077f630` cases 0x5a…0x62, set-ups; DATA; DOC]**

| Param | Name (manual) | Law | Notes |
|---|---|---|---|
| p90 | Chorus type | 0 off, 1…8 a fixed record (§1.3) | `FUN_00789570` |
| p91 | Chorus mix | m = p91·0.001 (float): dry `1 − (5^m − 1)/4`, wet volume `1 − (5^(1−m) − 1)/4` | both 0.691 at 500 |
| p92 | Delay type | 0 off, 1…3 (§1.4) | `FUN_00789080` |
| p93 | Delay vol | `(11^x − 1)/10`, x = p93·0.001 (exactly 1 at x = 1; ln 11 from the run-time float `0x7ff514` = 2.3978953 [EXEC]) | input gain into the ring |
| p94 | Delay fb | p94·0.001 (float) | through a ramp (§1.4) |
| p95 | Delay time | `round(p95·fs·0.3125/BPM)` samples = p95/192 beat (tempo `+0x2cc`, 120 by default [EXEC]) | min 1 |
| p96 | Reverb type | 0 off, 1…6 (§1.5) | `FUN_00789280` |
| p97 | Reverb vol | `0.5·(11^x − 1)/10`, x = p97·0.001 | wet gain |
| combo 3 | Compression type | 0 off, n = 1…4 loads `Data\Compression\<n>.fst` into a hosted `SG.dll` (§1.6) | read from the editor's combo (`+0x9f8 → +0x3b0`) when the state loads |
| p98 | Compression mix | sent unchanged as **parameter 5** of the hosted plugin (Maximus "LMH mix", §2.4) | `FUN_0077f630` case 0x62 |

[DOC `…/plugins/Harmless.htm`]: chorus "similar to Unison but operates across all voices"; delay Time "is tempo
synced"; the compressor types are "compression, limiting and distortion effects", "Limiting … disables the Mix control"
and **"Harmless uses the Maximus compression engine"** — all consistent with the code.

### 1.3 Chorus **[DECOMP + DATA + EXEC]**

- Records (table `0x7ee73c + 0x20·type`, 6 used ints per record) **[DATA]**: type → (order N, depth/1024, speed/1024,
  delay/128, spread/128, cross/128): 1 (4, 512, 768, 8, 128, −32), 2 (5, 1024, 856, 0, 128, 68), 3 (7, 512, 512,
  128, 128, 54), 4 (6, 561, 854, 0, 0, 38), 5 (1, 136, 312, 0, 0, 128), 6 (2, 86, 632, 0, 0, 0), 7 (1, 42, 944, 0, 0,
  −22), 8 (9, 81, 170, 0, 0, −66). The "volume" field of the unit is the wet gain of §1.2.
- The unit is the **same library code as Sytrus's effects unit** (round 31 §7.3: identical constants 1092.5, 101,
  75 000, 50 000, 2³¹, 500, 1001, 1000, 8, the 20 per-tap factors at `0x7ea5ac`), so its laws are round 31's:
  2N taps, parabolic LFO per tap, linear interpolation in the ring, gains normalised by `(1 − (1001^(1 − 1/Σ|g|) −
  1)/1000)·volume` **[DECOMP `FUN_0074a9b0`]**. Note: the sum Σ|g| and the maximum delay start at 0 (the decompiler
  shows them as the low half of a double constant) — EXEC fails at −24 dB with 1 and matches with 0.
- **Two tap kernels**, chosen once by a CPU-feature byte (`PTR_DAT_007f0870[0x16]`): x87 (`FUN_0074b180`, with the x87
  control word 0x0F3F: truncation toward zero, 64-bit mantissa) or SSE (`FUN_0074b440`, `cvtsd2si` = round to nearest).
  **On the test machine the byte is 0 [EXEC], so the x87 kernel runs: integer delay = ⌊D⌋** (this is also what round 31
  observed in Sytrus). Which kernel FL uses on another CPU is **not confirmed** (the meaning of byte 0x16 was not
  identified; inference: a CPU feature).
- Chorus read position: the ring is written block by block; tap value
  `s[n−k] + (D − k)·(s[n−k−1] − s[n−k])`, `k = ⌊D⌋`, first tap stored, the others added in extended precision then
  rounded to float.
- EXEC (`t2.py`, Harmless with a saw voice, model fed the DLL's own dry mix): types 1…8 −127.0…−143.5 dB (type 4 with
  mix 0: bit-identical).

### 1.4 Delay **[DECOMP + DATA + EXEC]**

- Records (table `0x7ee67c + 8·type`) **[DATA]**: type → (offset in ms, ping-pong flag): 1 (0, 0) normal, 2 (+1, 2)
  ping-pong with the **right** input written 1 ms later, 3 (−20, 2) ping-pong with the **left** input 20 ms later
  (`round(|ms|·0.001·fs)` frames; the sign picks the side).
- Ring of `(host block + offset + D)` frames; per block (active) the input `M·vol` is added at the write position
  (`read + D`) plus the channel offset; then per sample: `y = ring[r]`, `ring[r] = 0`, `ring[r + D] += fb·y` (normal),
  output `(L, R)`; ping-pong: output `(R, L)` and feedback `(R, L)` crossed; the written value is set to 0 when its
  magnitude is below 2⁻²⁴ (integer compare of the float bits, x87 kernel `FUN_007475a0`/`FUN_007477f0` — same CPU-byte
  choice as the chorus; the SSE twins compare an integer converted to float and in effect never flush).
- Feedback through `FUN_00612db0` (per-block linear ramp limited to `0.001·44100/fs` per sample). **Start of the
  ramp:** the constructor's target is 0.5 and a ring (re)allocation copies the current target into the ramp
  (`FUN_007474d0`); in the harness (state loaded, then the sample rate set) the ramp starts **at the state's
  feedback** — EXEC: with D = 1 (p95 = 0) a start at 0.5 gives −27…−56 dB, a start at the target is bit-identical.
  Whether FL sets the sample rate before or after loading a state (which would make a new unit start at 0.5) was
  not observed: **not confirmed**; LYNX uses the harness behaviour.
- EXEC: types 1…3 incl. D = 1 and feedback up to 0.9: **bit-identical** (44.1 and 48 kHz, blocks 512 and 256).

### 1.5 Reverb **[DECOMP + DATA + EXEC]**

- `FUN_00749540` loads `Reverb_x64.dll` (`CreateReverbInstance`) — the shared reverb of round 31 §7.5 — and
  `FUN_00749b60` writes its parameter block. Records (table `0x7ee67c + 0x20·type`, 8 ints) **[DATA]**: type →
  (lowcut Hz (0 = off), highcut ×100 Hz, predelay, room, diffusion, decay ×0.1 s, damping ×100 Hz, colour):
  1 (75, 40, 0, 40, 100, 15, 40, 2), 2 (0, 115, 18, 40, 100, 22, 50, 2), 3 (34, 21, 27, 90, 100, 45, 31, 3),
  4 (400, 60, 31, 60, 65, 23, 37, 1), 5 (100, 36, 0, 35, 100, 6, 36, 2), 6 (100, 60, 0, 17, 100, 5, 30, 2). Predelay
  `round(pre·fs/384)`, decay length `dec·fs/10`. The coefficient laws are round 31's (line lengths `MulDiv` with
  truncation [`objdump 0x5b8070`], diffusion 0.07/0.35/0.51, colour factors 0.2/0.6/1/1.5/3, 500 Hz shelf); the
  0.11 law written at block offset `+0x20` is not read by the process function (its input all-passes all use `+0x2c`).
- Input `L + R`, output added with `wet = 0.5·(11^x − 1)/10` (§1.2).
- EXEC: types 1…6 −129.3…−136.4 dB.

### 1.6 Compressor = Soundgoodizer hosting a Maximus state **[DECOMP + EXEC]**

- `FUN_00789800`: type > 0 → if no sub-plugin yet, `FUN_0076f2c0` loads `SG.dll` (resolved to `Shared\SG_x64.dll`)
  through its own `CreatePlugInstance` with Harmless as its host (rate, block size and process mode forwarded); then the
  path `<plugin folder>\Data\Compression\<n>.fst` is resolved through the host (request `0x3fc`, `FUN_0073f1e0`) and
  `FUN_0076f7f0` extracts the plugin state from the `.fst` and hands it to the sub-plugin's state loader; finally
  ProcessParam(0x62, p98) sends parameter 5. Type 0 removes the sub-plugin.
- **The four presets are Maximus states** (event 0xD5 after the plugin name "Maximus", version 7) **[DATA]**: 1 = master
  brickwall only (bands 0/2 off, band 1 "compression off" at unity); 2 and 3 = three-band maximisers (24 dB crossovers,
  band curves with knees and tensions, saturation, stereo separation; 3 adds a 20 Hz low cut); 4 = "distortion": every
  band in "compression off" with +18 dB post gain and saturation A, ×2 oversampling. The manual's "limiting … disables
  the Mix control" matches preset 1 (no band compresses, so the band mix has no effect) **[DATA + DOC]**.
- EXEC (`t1.py`): Harmless with compression n vs (Harmless with compression 0) → standalone `SG_x64.dll` with the state of
  `n.fst` and parameter 5 = p98: **−95.5…−∞ dB** (n = 1…4; p98 = 0, 300, 500, 1000). So the stage is exactly "SG with
  that state". (Inference: FL answers request `0x3fc` with the same installation file — the answer itself was not
  observed in FL.)

---

## 2. The Maximus engine (as run by `SG_x64.dll`) **[DECOMP + EXEC]**

Only what Harmless's four presets use was decoded; §2.9 lists the rest. Field names refer to the band record
`B = 0x380 + 0x100·b` (b = 0 low, 1 mid, 2 high, 3 master) of the plugin object.

### 2.1 State → working values **[DECOMP `FUN_0083ef50`, `FUN_00841b40`, `FUN_008417e0`; EXEC peeks]**

- The state block (59 parameters, 59 "backup" ints, UI block) is read raw into the object at `+0x12C`; parameter
  indices and laws as round 8 §1.2. **New:** the UI int at state offset `0x1F4` is the **oversampling order** (`+0x31C`;
  preset 4 has 1 → ×2) **[DECOMP + EXEC peek `+0x7C0` = 2]**; v ≥ 14 flags byte bit 6 selects a curve scale of 1.0
  instead of 0.975 (table `0x8b8ad0`).
- Per band: mode (`B+8`), pre = L7(pre/1000), post = L7(post/1000) with `L7(x) = (7^x − 1)/6` in float (1 at x = 1);
  parameter 5 (band mix) `mix = p5·0.001` and the flag `mix ≠ 1`.
- Time law `T(v) = 2·(501^(v/10000) − 1)` ms (`(501^x − 1)` kept as float), samples = `T·0.001·fs`.
- Stereo separation s = p10/1000: s > 0 → `(5^s − 1)/4`, s < 0 → s; capped at 1; 2×2 matrix
  `L' = L(1 − s/2) + R·s/2`, `R' = R(1 − s/2) + L·s/2` (positive narrows to mono, negative widens)
  **[DECOMP `FUN_00616320`; EXEC peeks: 0.845491529/0.154508501 for 500, 1.174/−0.174 for −348]**.

### 2.2 Signal flow per block **[DECOMP `FUN_00843a40`]**

1. **Silence counter:** a block is silent when every sample is within ±10⁻⁶. Non-silent → counter = latency +
   round(1.5·fs) (`+0x784`); silent → counter −= n. While counter > −n the block is processed; otherwise the plugin
   resets once (§2.8) and passes the block through untouched.
2. **Low cut** (p6 ≥ 0): four high-pass biquads at `0.8·5·2^(p6/16384)` Hz with α = sin ω·{0.19771184, 0.61932772,
   0.67684454, 1.07935691} (table `0x8b8580`) **[DECOMP `FUN_0083a5c0`; DATA]**, in place on the input.
3. **Oversampling** (order 1): 2:1 polyphase all-pass interpolator (§2.7) → the bands run at 2·fs.
4. **Crossover** (§2.3) and **bands 0…2** (§2.4): each band's output is summed (low band in place, others added).
5. The sum is decimated back (order 1) and runs through the **master band** (§2.4) in place; master mode 2 mutes.

### 2.3 Crossover **[DECOMP `FUN_00844e10`, `FUN_00624b50`, `FUN_00624580`, `FUN_00624e90`; EXEC]**

- `f_low = 30·2^(p0·ln(533.33)/ln2/65536)`, `f_high` the same from p2 (swapped if both bands are on and out of order);
  slope `sL = p1`, `sH = p3`, **−1 when the outer band is off** (mode 3), clamped to `0.4999·rate`.
- Biquads (RBJ form, `a0 = 1 + α`): low-pass, high-pass and all-pass, α = sin ω (slope 0 → one stage, Q = 0.5) or
  α = sin ω/√2 (slope 1 → two stages, Q = 0.7071 each: Linkwitz–Riley 24 dB); direct form I in double, output float,
  **the four states per channel rounded to float (and zeroed below 2⁻²⁴) at the end of every block** (`FUN_00627cc0`).
- Topology: low = (sL+1) low-pass stages at f_low (+ one all-pass at f_high when sH = 1); mid: (sL+1) high-pass stages
  at f_low applied **in place on the input** (so the high band sees them too), then (sH+1) low-pass stages at f_high;
  high = (sH+1) high-pass stages at f_high of that high-passed input. Band polarity `B+0x18` = table `0x8b8bbc`
  [slope]: −1 for 12 dB, +1 for 24 dB, 0 for an off band (applied in the band mix, §2.4) **[DECOMP + EXEC peeks]**.
- EXEC: preset 4 (24/12 dB crossovers at 52.5 Hz / 936 Hz, ×2 oversampling, saturation, no detector): −106 dB.

### 2.4 One band **[DECOMP `FUN_00843a40`, `FUN_00840c80`, `FUN_00845b50`, `FUN_00605410`, `FUN_007806b0`; EXEC]**

For mode < 2: stereo matrix (§2.1). For mode 0 ("on"), the gain is computed from the band signal:

1. **Detector (peak):** `max(|L|, |R|)` then the running maximum over the last W samples (monotonic-wedge
   algorithm), `W = max(L, round(T(sustain)·fs)·K, 1)` — L = lookahead (below), K = oversampling factor.
2. **Release stages** (`B+0x98`): `count = release curve` (p17) one-poles, each `s ← x + c·(s − x)` with c = 0 when the
   input is ≥ the state (instant rise) and `c = exp(ln 0.01/N)` otherwise, **N = 2·T(release)·fs·K/√count** (the per-stage
   time is divided by the **square root** of the stage count — `FUN_0040c3d0` is `sqrt` [DECOMP + EXEC peeks of the
   coefficients]); float states, flushed below 2⁻²⁴ at the end of a block.
3. **Static curve:** index `i = trunc(level·float(pre·8192.25))` (`ippsConvert_32f32s_Sfs`, round toward zero;
   negatives → 0); gain `= lerp(table[i], table[i+1], frac)·(pre·post)`, or `table_end·post/level` above the table
   (§2.5).
4. **Attack / release-2 stages** (`B+0x90`): `count = p15` one-poles on the gain, rising (gain ≥ state) with release 2
   and falling with attack: attack `N = 0.8·A/√count`, A = T(attack)·fs·K, or A = L when the attack is 0 ("Default");
   release 2 `N = 2·T(rel2)·fs·K/√count`, or the attack's N when 0. They start (reset) at `pre·post·table[0]`.
5. **Lookahead:** the audio (not the detector input) is delayed by `L` samples: bands 0…2 use
   `L = round(T(p4)·fs)·K` when the band is on/compression-off and at least one band compresses; the master uses
   `L = round(T(master attack)·fs)` when it compresses; otherwise 0.
6. When the band mix is not 1 (bands 0…2), the delayed band signal is kept as "dry".
7. Mode 0: audio × gain; modes 1–3: audio × post.
8. **Saturation A** (p11 < 0, mode < 2): odd table read with linear interpolation (§2.6).
9. Bands 0…2, mode < 2: band mix `y·(mix·pol) + dry·((1 − mix)·pol)` when mix ≠ 1, else `y·pol` (pol = §2.3 polarity);
   modes 2/3 contribute nothing (an "off" band was already merged into its neighbour by the crossover).

EXEC peeks confirmed every working value (W, L, coefficients, table sizes, matrices) for the four presets.

### 2.5 The drawn curve and its gain table **[DECOMP `FUN_00845b50`, `FUN_007876c0`, `FUN_007885d0`, point preparation; EXEC]**

- Envelope points in memory: dx (double), y (float), tension (float), type byte; dx ≤ 0 → 0.
- Evaluation (iterator: segment index, position t, rate 1/dx, start value, delta): the curve is evaluated
  incrementally at `c_i = ln(1 + 2·max(i,1)/8192)/(2 ln 3)` for i = 0…32 768 (`c` = editor x for level `i/8192`);
  value = `y_prev + shape(t)·(y_next − y_prev)` with the arriving point's shape:
  - **type 0**, tension τ ≠ 0: `q = 1000001^|τ| − 1` (float, sign of τ), `b = 1/|q|`, `c = ln(|q| + 1)`;
    τ > 0: `shape = 1 − (e^{(1−t)c} − 1)·b`; τ < 0: `shape = (e^{tc} − 1)·b`; τ = 0: linear;
  - **type 7**: `u = 1 − t` (τ > 0) or `t`; `shape = t − (u⁶ − u)·τ`.
  (Other segment types exist in the code — 1, 3…6, 8…12 — and are not used by the presets: **not modelled**.)
- Output level `(3^{2y} − 1)/2·s`, s = 0.975 (or 1, §2.1); table[i] = output/level; once the output equals s (within
  2⁻²⁴) the table stops at the last non-saturated index (`B+0x3C`), whose output end value is `B+0x40`; `B+0x48` =
  maximum output/s.
- EXEC: our own states with type-7 (τ = 0.36) and type-0 (τ = 0.14, 0.085) segments vs SG: −103 / −122 dB.

### 2.6 Saturation A **[DECOMP `FUN_00841380`, `FUN_0062a240`; EXEC]**

- Knee exponent `p = 20 − 19·ln(1 + 0.1·|thr|)/ln 101` (float; thr = p11 < 0 → p from 1 at −1000 to 20 near 0),
  ceiling `cl = L7(p12)`, top `= max(B50·post, cl)` with `B50 = B+0x48` (band compressing, peak detector) or 4.0.
- Table of 65 537 values: `t_i = (atan((i·d)^p))^{1/p} / (atan((65536·d)^p))^{1/p}`, `d = top/(cl·65536)` (IPP
  `Powx_64f_A53`, `Atan_64f_A53`), values below 2⁻²⁴ set to 0, converted to float.
- Applied per sample (x87, truncating): `v = x·65536/top`, `k = trunc(|v|)` (≤ 65 536), linear interpolation, sign of x,
  × cl. So the band output saturates smoothly at `cl`, sharper for smaller |thr|.
- Saturation B (p11 > 0, `FUN_00629fd0`/`FUN_0062a010`/`FUN_0062a090`, an exponential soft clip above a threshold) is not
  used by the presets: **not modelled**.

### 2.7 Oversampling filters **[DECOMP `FUN_0083a9c0`, `FUN_0083ab90`; EXEC peek of the coefficient table]**

Two cascades of three first-order all-passes `y = x[n−1] + k(x[n] − y[n−1])`, branch A k = 0.14737711, 0.48246854,
0.88300503, branch B k = 0.03915160, 0.30264685, 0.67461592 (the same as Sytrus's decimator, round 31 §7.2);
up: each input sample gives (B, A); down: `0.5·(B + A)` with A fed by even and B by odd samples; the all-pass outputs are
rounded to float (zero below 2⁻²⁴) at the end of each block.

### 2.8 Reset and latency **[DECOMP `FUN_00845940`, `FUN_00840a00`]**

Reset (at load and after 1.5 s of silence): attack stages → `pre·post·table[0]`, release stages → 0, lookahead
delays, crossover/low-cut/oversampling states → 0, counter → −0x3FFFFFFF (processing restarts with the next non-silent
block). Latency reported to the host = `max(L₀…₂)/K + L₃` (not compensated inside Harmless).

### 2.9 Not decoded / not used by Harmless's presets

RMS detector (`FUN_007808b0` class: `L² + R²`, box sum over W, scale, sqrt — structure only), saturation B, envelope
segment types other than 0 and 7, the linear-phase ("FFT", `+0x32C`, `engine_x64.dll`) path, solo/mute flags,
oversampling orders above 1, curve-scale flag of v ≥ 14 states (read, not exercised). LYNX rejects a preset that needs
any of them (none of the four does).

---

## 3. Verification **[EXEC + model]**

Relative error `20·log₁₀(‖model − DLL‖/‖DLL‖)`, both channels, worst reported, 44.1 kHz, blocks of 512 unless stated.

| Check | Cases | Result |
|---|---|---|
| Harmless compressor ≡ hosted SG (`t1.py`) | types 1…4, p98 0/300/500/1000 | −95.5…−∞ dB |
| Chorus (`t2.py`) | types 1…8 (mixes 0…1000) | −127.0…−∞ dB |
| Delay | types 1…3, D = 1…5512, fb ≤ 0.9 | bit-identical |
| Reverb | types 1…6 | −129.3…−136.4 dB |
| All three units | chorus 3 + delay 2 + reverb 4 | −131.3 dB |
| Maximus vs SG standalone (`maxtest.py`) | 4 presets × mixes × {saw, noise bursts with silences, loud two-tone} | −91.2…−123.0 dB |
| Our own Maximus states (`syntest.py`) | limiter only; 3 bands + ×2 + low cut + separation + saturation + tensions + release 2 | −122.2 / −103.1 dB |
| 48 kHz, blocks 512 / 256 / 300 (`t48.py`) | chorus 2/7, delay 3, reverb 1/6, all, SG presets 1…4 | rack −128.1…−∞ dB; SG −94.8…−123.9 dB |

**Whole channels — the 139 Harmless channels of the demo projects with their saved state, rack on** (`batrack.py` →
`batrack_final.out`; model = round 32's voice model with the DLL's per-voice random draws peeked, as in round 32,
followed by `rack.py` with `maxmodel.py`; three overlapping notes, 2 s so that delay/reverb tails are included):

- **135 of 139 channels ≤ −60 dB** (median −79.8 dB; 122 ≤ −70 dB; 69 ≤ −80 dB; best −100.7 dB; worst of these
  −62.4 dB, "Steptime"). They include every chorus type, every delay type (incl. D = 1 sample), every reverb type and
  all four compression presets with their saved mixes.
- **The 4 remaining channels are the same as in round 32** (Galactic: Snare −25.6, High Hat +1.7, Galactic +0.3,
  Noisesweep −2.4 dB): the Freq phaser at quality 0, whose output depends on IPP `Exp_32f_A11`'s error pattern
  (round 32 §1.9) — not a rack effect.
- Without the delay-ramp correction of §1.4, "Kick Stab" (delay time 0) was at −53 dB; with it −72.9 dB.
- Per-channel list: `r36/py/batrack_final.out`.

How the random parts are handled: the DLL seeds its generator from the clock (round 32 §1.19), so every DLL run
draws different per-voice seeds; the harness reads the draws of *that* run from the voice records right after note-on
and the model uses them. The rack itself has no random part. LYNX (C++) uses its own deterministic generator, so
for channels with spectral-random phases, Noise resonance or the "voice random" LFO, LYNX can only be compared with the
model fed LYNX's own draws (round 32's port check, −151 dB) — not sample by sample with an FL render.

**C++ port** (`cpptest.py`: `hlfx::Rack` vs `rack.py` + `maxmodel.py` on the same input, 44.1/48 kHz, blocks 512/333):
chorus −142.9…−144.7 dB, delay bit-identical, reverb −129.1…−136.4 dB, Maximus presets 1–2 bit-identical and 3–4
−144.9…−154.2 dB, full racks −98.2…−129.4 dB.
**End to end** (`chanrec36` + `dllcmp36.py`, LYNX's recorded channel vs the DLL fed the same notes and FL levels):
Galactic "Kick" (compression 3, deterministic voice) −79.0 dB over 28 notes; channels with clock-seeded or Freq-phaser
parts differ as described above, and identically with the rack switched off (not a rack effect).

---

## 4. Harmor shares the rack's building blocks **[DATA (byte search)]**

`Harmor_x64.dll` contains the same UTF-16 strings `SG.dll`, `Reverb_x64.dll`, `CreateReverbInstance`, a path built
from `System`, `\`, `Compression`, `.fst` (Harmor's presets are `Plugins/Fruity/Generators/Harmor/System/Compression/
1…4.fst`; `1.fst` is byte-identical to Harmless's `1.fst`, 2–4 differ), the 20 chorus per-tap factors (at VA
`0x91b6f0`), and **byte-identical code** for the chorus x87 tap kernel (Harmless `FUN_0074b180` = Harmor VA
`0x7a9240`), the chorus set-up (`FUN_0074a9b0` = `0x7a8a70`) and the reverb coefficient set-up (`FUN_00749b60` =
`0x7a7c20`); the delay kernel's first 24 bytes match at `0x7a5660`. Harmor also names separate reverb controls
(`dec`, `diff`, `damp`). **Conclusion:** the chorus, delay and reverb units and the SG/Maximus compressor are the same
library and DLLs; a Harmor port can reuse LYNX's `hlfx` units (and the Maximus engine with Harmor's own presets). Harmor's
Gen_Render order, parameter laws and preset tables were **not analysed** (not confirmed).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| R1 | Order | chorus → delay → reverb → compressor on the float voice mix, then added to the host buffer; tail counter round(0.004·fs) + chorus ring **[DECOMP + EXEC]** |
| R2 | Chorus | 8 fixed records of the Sytrus effects-unit library; dry/wet `1 − (5^x − 1)/4`; x87 kernel (⌊D⌋) on the test machine **[DECOMP + DATA + EXEC −127…−∞ dB]** |
| R3 | Delay | 3 records (normal; ping-pong with +1 ms R / +20 ms L); time p95/192 beat; vol `(11^x−1)/10`; fb ramp **[DECOMP + DATA + EXEC bit-identical]** |
| R4 | Reverb | Shared `Reverb_x64.dll` with 6 fixed records; wet `0.5·(11^x−1)/10` **[DECOMP + DATA + EXEC −129…−136 dB]** |
| R5 | Compressor | hosted `SG_x64.dll` loaded with Harmless's `Data/Compression/<n>.fst` (Maximus states), p98 = parameter 5 (band mix) **[DECOMP + EXEC ≤ −95 dB]** |
| R6 | Maximus | low cut, ×2 oversampling, LR crossovers with polarity, per band: separation matrix, peak-window detector, √-scaled release / attack / release-2 one-poles, drawn-curve gain table (tension types 0 and 7), lookahead, saturation A, band mix; master limiter **[DECOMP + EXEC −91…−123 dB]** |
| R7 | Demo channels (model incl. rack vs DLL) | 135/139 ≤ −60 dB (median −79.8), the 4 others are round 32's Freq-phaser channels **[EXEC + model]** |
| R8 | Harmor | same chorus/delay/reverb code, same SG + `.fst` mechanism (own presets) **[DATA]**; its parameter mapping not analysed |
| R9 | Correction | earlier "0.976 caps" measurement used SG's default state because the harness did not answer the plugin's data-file request |

---

## 5. For LYNX (what was implemented)

`src/core/HarmlessFx.{h,cpp}` (namespace `lynx::hlfx`), used by `src/core/Harmless.cpp`:

1. **Import** (`FlpImport.cpp`): when compression type n > 0, read `%FLStudioFactoryData%\Plugins\Fruity\Generators\
   Harmless\Data\Compression\<n>.fst` from the user's installation, extract the Maximus state (`maximusStateFromFst`) and
   append it to `modelParams` (`harmlessCompIndex`: byte count, then one byte per float). Missing → the compressor stays
   off and the report lists "Harmless compressor (preset not found)".
2. **Configure** (message thread): `makeMaximusPreset` builds everything rate-independent (gain tables, saturation
   tables, matrices); unsupported states are rejected. **Prepare/rebuild**: `Rack::prepare` (chorus/delay/reverb from
   the records above, the delay at the project tempo; Maximus coefficients at the sample rate).
3. **Render:** the voice mix is converted to float, `Rack::process` runs in place (voices flag = records existed at the
   block start), then it is added to the output. The instrument stays non-idle until the rack has been below −140 dB
   for longer than its longest delay; while idle, `Rack::advance` keeps the chorus phases, the delay ramp and the
   compressor's silence counter running (FL calls Gen_Render continuously).
4. Arithmetic follows the reference's float/double order (FMA contraction off); CPU-feature choice: the x87 kernels of
   the test machine.
5. Tests: `tests/tests.cpp` `harmless_rack` (rack goldens from the reference model, delay laws, two Maximus states of our
   own, `.fst` extraction, rejection of an RMS state) and the real-file check (all 134 demo channels with a compressor
   get their preset from the installation).
6. **Costs** (one core, 44.1 kHz, `rackcmp` on 60 s of noise): Maximus presets 1 / 2 / 3 / 4 = 0.18 / 0.77 / 0.87 /
   0.53 % of real time, chorus type 8 (18 taps) 0.47 %, delay 0.08 %, reverb 0.13 %.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Harmless `FUN_007886b0` | rack order, tail counter, chorus/delay/reverb/compressor calls | DECOMP |
| Harmless `FUN_0077f630` cases 0x5a…0x62 | parameter → set-up dispatch, p98 → SG parameter 5 | DECOMP |
| `FUN_00789570`, `FUN_00789080`, `FUN_00789280`, `FUN_00789800` | chorus / delay / reverb / compressor set-ups and laws | DECOMP |
| tables `0x7ee73c`, `0x7ee67c` | chorus, delay and reverb records | DATA |
| `FUN_0074a9b0`, `FUN_0074b180`/`230`, `FUN_0074b440`/`600`, `PTR_DAT_007f0870[0x16]` | chorus laws, x87/SSE kernels, CPU byte = 0 | DECOMP + EXEC |
| `FUN_007471d0`, `FUN_00747070`, `FUN_00747510`, `FUN_007475a0`/`7477f0`, `FUN_007474d0` | delay ring, kernels, ramp start | DECOMP + EXEC |
| `FUN_00749540`, `FUN_00749b60`, `FUN_0074a540`; Reverb_x64 `FUN_180002980` | shared reverb and its parameter block | DECOMP + EXEC |
| `FUN_0076f2c0`, `FUN_0076f7f0`, `FUN_0073f1e0` | SG sub-plugin, `.fst` state, data-file request 0x3fc | DECOMP + EXEC |
| `Harmless/Data/Compression/1…4.fst` | Maximus v7 states | DATA |
| SG `FUN_00843a40` | Maximus block flow | DECOMP |
| SG `FUN_0083ef50`, `FUN_00841b40`, `FUN_008417e0`, `FUN_00616320` | state, parameters, modes, separation matrix | DECOMP + EXEC peeks |
| SG `FUN_00844e10`, `FUN_00624b50`, `FUN_00624580`, `FUN_00624e90`, `FUN_00627cc0`, table `0x8b8bbc` | crossover design, biquad, state rounding, polarity | DECOMP + EXEC |
| SG `FUN_00840c80`, `FUN_006052f0`, `FUN_00605410`, `FUN_0040c3d0` (sqrt) | time constants, √-scaled stages, smoother | DECOMP + EXEC peeks |
| SG `FUN_00629710`, `FUN_007806b0`, `FUN_0077f350` | peak window, lookahead delay | DECOMP + EXEC |
| SG `FUN_00845b50`, `FUN_007876c0`, `FUN_007885d0`, `FUN_0060dfd0`, point preparation `0x7854f0…` | curve evaluation and gain table | DECOMP + EXEC |
| SG `FUN_00841380`, `FUN_0062a240`, IPP `Powx_64f_A53`, `Atan_64f_A53` | saturation A | DECOMP + EXEC |
| SG `FUN_00847ce0`, `FUN_0083aef0`/`af90`, `FUN_0083a9c0`/`ab90`, table `0x8cd028` | oversampling | DECOMP + EXEC |
| SG `FUN_00840b40`, `FUN_0083a5c0`, table `0x8b8580` | low cut | DECOMP + DATA + EXEC |
| SG `FUN_00845940`, `FUN_00629890`, `FUN_00840a00` | reset, silence, latency | DECOMP + EXEC |
| Harmor byte search | shared code/strings/presets | DATA |
| `r36/py/t1.py`, `t2.py`, `maxtest.py`, `syntest.py`, `t48.py`, `batrack.py`, `cpptest.py` | §3 | EXEC + model |

### Open questions

- Which chorus/delay kernel (x87 truncating vs SSE rounding) FL uses on CPUs where the feature byte `[0x16]` is set
  (its meaning was not identified).
- FL's call order (sample rate vs state load) decides whether a fresh delay starts its feedback ramp at 0.5 or at the
  state's value (the harness order gives the latter).
- FL's actual answer to the data-file request `0x3fc` (inferred to be the installation file).
- Maximus features not used by Harmless's presets (§2.9); Harmor's own rack mapping (§4).
