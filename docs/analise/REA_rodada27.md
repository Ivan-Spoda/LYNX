# Behavioural analysis of FL Studio 2026: round 27 (REA) — Harmless tremolo/vibrato and per-partial filter, Effector Filter/Flanger/Phaser/Reverb, Love Philter pan law, pulse curve and LFO tension

> **Nature of this document.** Continuation of `REA_rodada21.md` §1 and `REA_rodada24.md` §2 (Harmless),
> `REA_rodada14.md`/`REA_rodada18.md` §3/`REA_rodada19.md` §1 (Effector) and `REA_rodada16/17/20.md` (Fruity Love
> Philter). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Intel IPP, nor CRT) code or
> pseudocode was copied: what is here are formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched (no FLEX
> pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from
> running a function of the installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence,
> the item is marked **"not confirmed"**. Inferences made *from* the code are stated explicitly as **inference**.
> Results of our own numerical models (which implement only what is marked DECOMP/EXEC) are marked **[model]**.

- **Targets:**

  | Plugin | DLL (SHA-256, hashed against the installation in this round) | Language | Key functions |
  |---|---|---|---|
  | Harmless | `Plugins/Fruity/Generators/Harmless/Harmless_x64.dll` `4100bf85ad29dc665051a73ef62017272a25aefbda1cf2f1ace39e6be376440b` (= rounds 10/21/24) | Delphi | plugin render `FUN_007886b0` (tremolo generator), voice render `FUN_00784a10` (vibrato, filter, resonance, tremolo application), sample-rate setter `FUN_0077f170`, `ProcessParam` `FUN_0077f630` (cases 0x19, 0x29, 0x36…0x3F), shape-table builder `FUN_0077ca30` (+ `FUN_0077c970`, `FUN_0077c940`, RMS normaliser `FUN_0077c880`/`FUN_0077c920`, small-value flush `FUN_0060ff50`), shape selection `FUN_0077c0f0`, range `FUN_0077c170`, table lookup `FUN_0077c540` (nearest `FUN_0077c470`), resonance application `FUN_00784180`, resonance scale `FUN_00788060`, gain ramp `FUN_00604a90`/`FUN_00612db0`, sine `FUN_00409610`, IPP dispatcher `FUN_005acdb0`, table constructor `FUN_0077b450` |
  | Effector (engine) | `Plugins/Fruity/Effects/Effector/engine_x64.dll` `4bfdc0f61ffc38f004e305bf98f8183cfcf0f0c4df29509e391ebec57dc2071a` (wrapper `Effector_x64.dll` `2f538ec9bf0bbbdcad8cc0717955d22e0386320dc39a7f0cfcc5a61b512f72e5`; = rounds 14/18/19) | C++ (MSVC) | unit dispatcher `FUN_180018c20`, LFO `FUN_180017ec0`, Filter `FUN_180019110`, Flanger `FUN_180019d60`, Phaser `FUN_18001a680`, Reverb unit `FUN_18001a130`; unit resources `FUN_180018700`, unit reset `FUN_180018a50`; `CTapeDelay` constructor `FUN_180017f80`, segment alloc `FUN_180017e20`, reset `FUN_180018120`, delay setter `FUN_180018520`, feedback setter `FUN_180018560`, tick `FUN_1800181c0`; `fxReverb` (VMT `0x180057fc0`): constructor `FUN_18001c640`, buffer sizing `FUN_18001c090`, parameter setter slot 10 `FUN_18001cc30`, coefficient update `FUN_18001dae0`, process slot 43 `FUN_18001e9f0`; CRT `FUN_18003ff50` (expf), `FUN_18002e2d0` (powf) |
  | Fruity Love Philter | `Plugins/Fruity/Effects/Fruity Love Philter/Fruity Love Philter_x64.dll` `b9f11d6359c04d531a031c30141f46573dd8ce301e62b6de612c1329ba2ff547` (= rounds 16…20) | Delphi | pan/volume `FUN_00616620` (+ variant `FUN_006167a0`), pan laws `FUN_0062afb0`/`FUN_0062cff0` (mode 0) and `FUN_0062afe0`/`FUN_0062d060` (mode 1), pointer set-up `FUN_0062d840`, stereo matrix mixer `FUN_00629090`, gain ramp `FUN_0062aee0`, render `FUN_0085aa10` (call at `0x85c037`), articulator `FUN_008543d0`; envelope point preparation `FUN_00783ec0`, curve count `FUN_007858b0`, segment evaluation `FUN_00786350`; LFO set-up `FUN_007886b0`, evaluation `FUN_00788b70`, waveform `FUN_0060e450`/`FUN_0060e610`, tension coefficients `FUN_0060e3c0`; `ROUND` `FUN_0040c520`, cos `FUN_0040bb50` |

- **Tools:** Ghidra 12.1.4 *headless*, read-only, on the existing projects `/tmp/r21gh_Harmless` (program
  `Harmless_x64.dll`), `/tmp/r18gh_EffEng` (`Effector_eng.dll`) and `/tmp/r17gh_LP` (`LP.dll`). Runner `tmp/r27/g.sh`,
  scripts `tmp/r27/scripts/` (copies of round 26's); dumps in `tmp/r27/out/` (`h1.c`…`h5.c`, `hipp.c`, `h_voice.asm`,
  `e1.c`…`e7.c`, `e_*.asm`, `lp1.c`…`lp9.c`, `lp_*.asm`). Constants read from the PE with `tmp/r18/k.py`; IPP
  pointer → name pairs with `tmp/r27/py/ippmap.py` (parses the dispatcher dump). Our own models (pure Python,
  `python3 -I`): `tmp/r27/py/hlfilt.py` (Harmless tables, per-partial filter/resonance gain), `tmp/r27/py/effx.py`
  (Effector Filter/Phaser/Flanger + closed-form Filter response), `tmp/r27/py/rt60.py` (decay of executed reverb IRs).
  **Execution harnesses** (our own C, built with Zig 0.14.1 for Windows; pattern of round 25's `tab2.c`:
  `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, vectored fault handler that ends
  the process, `TerminateProcess` at the end without the DLL's shutdown, run under `timeout`; no process or dialog was
  left behind):
  - `tmp/r27/run/hless.c` → `hless.exe`: loads `Harmless_x64.dll`; mode `const` reads constants that the DLL fills at
    load time and runs `FUN_00788060` for the four (p60, p63) combinations (`hconst.txt`); mode `shape` runs the table
    builder `FUN_0077ca30` on a zeroed record for codes 0…16 and 100…108 and dumps header + table (`hshape.txt`).
  - `tmp/r27/run/eunit.c` → `eunit.exe`: loads the Effector `engine_x64.dll`, fills a zeroed unit record as the
    engine's setters do (kind, sample rate 44 100, X, Y, wet 1, LFO amount 0), creates the tape delays with the DLL's
    own `FUN_180018700` (flanger), and runs the DLL's Filter / Phaser / Flanger routine on a unit impulse in 256-sample
    blocks (`eu_<kind>_<X>_<Y>.txt`).
  - `tmp/r27/run/erev.c` → `erev.exe`: builds the engine's `fxReverb` with its own constructor, sets 44.1 kHz through
    VMT slot 6, writes the ten parameters exactly as the Effector reverb unit does every block (§2.4), and runs VMT
    slot 43 on an impulse; writes `rv_<X>_<Y>.bin` and prints the derived coefficient fields.
  - `tmp/r27/run/lppan.c` → `lppan.exe`: loads `Fruity Love Philter_x64.dll` and calls `FUN_00616620` for 11 pan values
    (`lppan.txt`).
- **Date:** 2026-10-14.
- **Dataset:** `tmp/r14/blobs14.pkl` (Harmless 139 demo channels, parser `tmp/r21/py/hl.py`).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by calling a function of the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated `…/plugins/`). Harmless page read via the web in this round (short quotes below).

**Conventions (Harmless).** Image base 0x400000. IPP names through the dispatcher `FUN_005acdb0` (pointer →
global → name): `0x7f0888` = `ippsTone_Direct_32fc`, `0x7efbc0` = `ippsAbs_32f`, `0x7ef5d8` = `ippsAddC_32f_I`,
`0x7eff80` = `ippsMul_32f_I`, `0x7ef3c8` = `ippsMul_32f`, `0x7effc8` = `ippsVectorSlope_32f`, `0x7ef5a8` =
`ippsThreshold_GT_32f_I`, `0x7f0310` = `ippsRealToCplx_32f`, `0x7efb28` = `ippsSubCRev_32f_I`, `0x7f07b8` =
`ippsAdd_32f_I`, `0x7f03d8` = `ippsMulC_32f_I`, `0x7f06d0` = `ippsConvert_64f32f`, `0x7f06f0`/`0x7efb58` =
`ippsLn_32f_A11/A24`, `0x7f00f8` = `ippsRandUniform_Direct_32f`, `0x7f0890`/`0x7efd10` = `ippsExp_32f_A11/A24`
**[DECOMP]**. Constants initialised at load and read by `hless.exe`: `0x7ff2b8` = ln 0.01, `0x7ff4e0` = ln 2,
`0x7ff500` = ln 2/12, `0x7ff514` = ln 11, `0x7ff52c` = ln 10/20, `0x7ff530` = log₂ 440 **[EXEC]**; static ones:
`0x7d04e4` = 2π, `0x7d051c` = 0.001, `0x7d0520` = 1/128 **[DECOMP + PE]**. `FUN_00409610` is the sine (quadrant 0 →
the kernel that starts at x) **[DECOMP]**.

---

## 1. Harmless

**What LYNX assumes** (`src/core/FlSynths.cpp`, `synthFromHarmless`): the filter is a biquad (LP/BP/BS/HP switch),
resonance `0.9·x59 (+0.1)`; p25 and p41 are not converted (STATUS "still approximated": per-partial filter,
stereo tremolo 2.5 Hz, vibrato 6 Hz, resonance width).

### 1.1 Stereo tremolo (p25) **[DECOMP `FUN_007886b0`, `FUN_00784a10` (end), `FUN_0077f170`, `FUN_0077f630` case 0x19, `FUN_00604a90`/`FUN_00612db0`]**

- **Depth:** `d = 0.001·p25` (field `+0x79C`) **[DECOMP]**. Data: p25 ≠ 0 in 32/139 channels (88 ×5, 300 ×2, 1000 ×2,
  …) **[DATA]**.
- **Rate:** `+0x7A4 = 2.5/fs` cycles per sample (constant 2.5 at `0x77f470`) **[DECOMP]**.
- **Generator (once per block, shared by all voices):** a *complex* tone of magnitude **1.25** at 2.5 Hz is written
  into a stereo-interleaved buffer — the real part goes to the left slot, the imaginary part to the right slot —, then
  every value is replaced by its absolute value, 1 is subtracted, the result is multiplied by the depth (ramped per block
  from the previous depth `+0x7A0` to `d`, slope limited by the host's maximum step), and 1 is added back
  **[DECOMP: `ippsTone_Direct_32fc` (magn 1.25 at `0x788fb0`), `ippsAbs_32f`, `ippsAddC_32f_I(−1)`, `FUN_00604a90`,
  `ippsAddC_32f_I(+1)`]**. So, with `θ = 2π·2.5·t + φ₀`:

  > **g_L(t) = 1 + d·(|1.25·cos θ| − 1)**, **g_R(t) = 1 + d·(|1.25·sin θ| − 1)**

  Each channel therefore pulses at **5 Hz** (rectified 2.5 Hz), with L and R a quarter of the 2.5 Hz period (0.1 s)
  apart — i.e. **in anti-phase at 5 Hz** (L peaks when R is at its trough) **[derived from the formula]**. Range per
  channel `[1 − d, 1 + 0.25d]` (d = 1 → 0…1.25; d = 0.088 → 0.912…1.022).
- **Phase:** `φ₀ = 2π·frac(N·2.5/fs)` with `N` = the plugin's running sample counter `+0x330` (incremented by the block
  length at the end of every render) **[DECOMP]** → free-running, not retriggered per note and not tied to the song.
  Where (if anywhere) `+0x330` is reset was **not confirmed**.
- **Application** (end of `FUN_00784a10`): each voice's stereo output (after its pan) is multiplied sample-by-sample by
  `(g_L, g_R)` **[DECOMP `ippsMul_32f_I`]**. A newly started voice fades into the tremolo over **0.1 s**: for its first
  `M = round(0.1·fs)` samples the factor is `(1 − r) + r·g` with `r` a linear ramp 0 → 1 (`+0x7A8 = 1/(M+1)`)
  **[DECOMP `ippsVectorSlope_32f`, `ippsThreshold_GT_32f_I`, `ippsRealToCplx_32f`, `ippsSubCRev_32f_I`; `+0x7AC =
  round(0.1·fs)` from `FUN_0077f170`]**.
- The tremolo runs only while `d ≠ 0` or the ramped depth is still ≠ 0; with no voices the ramped depth snaps to `d`
  **[DECOMP]**.
- Manual: "Stereo tremolo effect. This is a left/right panning of the sound that simulates the rotary speaker effect"
  **[DOC `…/plugins/Harmless.htm`]** — consistent with the anti-phase L/R modulation.

### 1.2 Vibrato (p41) **[DECOMP `FUN_00784a10` (control-block part), `FUN_007886b0` (end), `FUN_0077f170`, `FUN_0077f630` case 0x29]**

- **Depth:** `+0x31C = 0.075·p41` **cents** (constant `0x781070`); the branch is taken only when p41 ≠ 0 (parameter
  slot `+0x1CC`) **[DECOMP]**.
- **Rate:** global phase `+0x310`, advanced by `6/fs` per sample (`+0x314`, constant 6.0 at `0x77f460`) and wrapped to
  [0, 1) at every block; inside a voice the phase used for each control block is the global phase advanced by the
  samples already processed in that block **[DECOMP]** → **6 Hz, one phase shared by all voices, free-running**.
- **Shape and fade-in:** pitch offset (cents) `= sin(2π·φ)·0.075·p41·r²`, with `r` a per-voice ramp that starts at
  `3/fs` at note-on and grows by `3/fs` per sample up to 1 (`+0x318`) → **r reaches 1 after 1/3 s; the depth follows r²**
  **[DECOMP]**. Evaluated once per control block (block length `+0x2C8`) and added to the voice pitch in cents before
  the partial frequencies are recomputed **[DECOMP]**.
- Data: p41 ≠ 0 in 60/139 (72 ×5, 120 ×4, 164 ×3 …; 0.075·120 = 9 cents) **[DATA]**. Manual: "Vibrato effect. Fast
  pitch modulation." **[DOC]**.

### 1.3 Per-partial filter: the magnitude law **[DECOMP `FUN_00784a10`, `FUN_0077c540`, `FUN_0077c470`, `FUN_0077c0f0`, `FUN_0077ca30`; EXEC `hless.exe shape`]**

Each control block the voice computes, per partial `i`, its frequency `f_i` in Hz (`ippsConvert_64f32f` × fs) and
`log₂ f_i` (`ippsLn` × `1/ln 2`, the factor `0x811ad0 = 1/ln(2.0)` set in `FUN_0077b450`) **[DECOMP]**. Then:

- **Shape selection:** filter record `+0x650` gets code = **filter type combo index (0…16)**; resonance record `+0x678`
  gets **resonance type + 100** (100…108) **[DECOMP `FUN_0077c0f0`]**. Record layout: `+0` code, `+4` N, `+8` lookup
  mode, `+0xC` origin `o`, `+0x10`/`+0x14` extensions eL/eR, `+0x20` table (N + 1 floats, `T[N] = T[0]`).
- **Lookup:** `idx = x·scale + offset`; in mode < 2 the index is clamped to [0, N − 1] and **truncated** (nearest-lower
  entry); otherwise linear interpolation between the two neighbours (`*(0x811ad8)`). All executed records have mode 0
  after the build; who switches the mode (quality) was **not traced** **[DECOMP]**. Below the table → `T[0]`, above →
  `T[N−1]`.
- **Filter gain (octave scale, p55 = 0):** `F_i = T_type[(log₂ f_i − pos)·W + o]` with **`W = 1024·2^(1 − 0.004·p56)`
  entries per octave** (`+0x6E8`) and `pos` = final cutoff in log₂ Hz (cutoff, keytracking, envelope, LFO — round 21
  §1.6) **[DECOMP]**. **Hz scale (p55 = 1):** `F_i = T_type[(f_i/f_c − 1)·0.9·W + o]`, `f_c = 2^pos` **[DECOMP: scale
  `0.9·W/f_c`, offset `o − 0.9·W`]**.
- The partial amplitudes are multiplied by `F_i` **[DECOMP `ippsMul_32f_I`]**.

**Tables (our own restatement; every one reproduced from the executed DLL to ≤ 1.6·10⁻⁷ for codes 0…16, ≤ 6·10⁻⁶ for
the RMS-normalised resonance shapes)** **[DECOMP + EXEC, model `hlfilt.py`]**. Notation: `u = i/(N−1)`;
`L(d) = 10^(−72d/20)·(1 − d¹⁶)` (the "−72 dB ramp" of round 21); for the band types `d = |i − N/2|/(N/2)`.

| Code | Name | N | o (origin) | T(i) |
|---|---|---|---|---|
| 0 | Classic LP | 2048 | 0 | `L(u)` |
| 1 | Classic BP | 4096 | 2047.5 | `L(d)` |
| 2 | Classic BS | 4096 | 2047.5 | `1 − L(d)` |
| 3 | Classic HP | 2048 | 2047 | `L(1 − u)` |
| 4 | Crude LP | 1024 | 0 | `cos⁴(πu/2)` |
| 5 | Crude BP | 2048 | 1023.5 | `sin⁴(πu)` |
| 6 | Crude BS | 2048 | 1023.5 | `cos⁴(πu)` |
| 7 | Crude HP | 1024 | 1023 | `sin⁴(πu/2)` |
| 8 | Hill LP | 682 | 0 | `cos(πu/2)` |
| 9 | Leg LP | 1024 | 0 | `0.00075·(1001^(1−u) − 1) + 0.25·(1 − u)` |
| 10 | Hybrid LP | 1365 | 0 | `L(1 − cos(πu/2))` |
| 11 | Snake LP | 1024 | 0 | `sin²(1.5π(1 − u))·cos²(πu/2)` |
| 12 | Fin LP | 4096 | 2047.5 | `i < N/2`: `(1 − (1 + cos 2πu)/4)²`; `i ≥ N/2`: `L(2(i − N/2)/(N−1))` |
| 13 | Leaky LP | 1024 | 0 | `10^(−0.9·sin²(πu/2))` (floor −18 dB) |
| 14 | Phaser | 4096 | 2047.5 | `cos⁴(4πu)` |
| 15 | Band phaser | 4096 | 2047.5 | `cos⁴(6πu)·sin⁴(πu) + 0.125·sin²(πu)` |
| 16 | Flat band pass | 2048 | 1023.5 | edges `sin⁴(π·j/(2M))` for `j = min(i, N−1−i) < M = ⌊N/3⌋`, 1 in the middle |

Reading for the LP types with origin 0: **gain 1 for every partial below f_c**, and the curve spans `N/W` octaves above
it (Classic LP, p56 = 500: 2048/512 = **4 octaves** to −72 dB, the slope steepening towards the end); HP types mirror
this below f_c; band types are centred on f_c and span `N/W` octaves in total.

### 1.4 Resonance: shapes, amount and width **[DECOMP `FUN_00784a10`, `FUN_00784180`, `FUN_00788060`, `FUN_0077ca30`, `FUN_0077f630` cases 0x3C/0x3D/0x3F; EXEC `hless.exe`]**

- **Shapes** (resonance combo order of round 10: Classic, Spring, Pedestal, Sedge hat, Wide bump, Double cone, Well,
  Wormhole, Noise → codes 100…108). Executed records: eL = 1, eR = 0; values RMS-normalised by `FUN_0077c880` (the
  table is scaled so that its RMS equals a per-shape constant) **[DECOMP + EXEC]**:

  | Code | Name | N | o | Raw shape (before RMS scaling) | RMS | Peak (after) |
  |---|---|---|---|---|---|---|
  | 100 | Classic | 1024 | 511.5 | `sin⁶(πu)` | 7 | 14.745 |
  | 101 | Spring | 682 | 340.5 | `sinc(4π(i − 341)/(N−1))·sin²(πu)` (sinc(z) = sin z/z) | 9 | 20.19 (min −1.13) |
  | 102 | Pedestal | 341 | 170 | flat top with `sin⁴` edges (as code 16, M = ⌊N/3⌋) | 10 | 13.93 |
  | 103 | Sedge hat | 512 | 255.5 | `L(d)` (as Classic BP) | 18 | 73.28 |
  | 104 | Wide bump | 2048 | 1023.5 | as 102 | 5 | 6.96 |
  | 105 | Double cone | 1536 | 767.5 | `sin⁸(2πu)` (two bumps) | 5 | 11.29 |
  | 106 | Well | 768 | 383.5 | `−(1 − |i − N/2|/(N/2))²` (not normalised) | — | 0 (min −1) |
  | 107 | Wormhole | 1024 | 511.5 | `−sinc(4π(i − 512)/(N−1))·(sin²(πu))^p`, **p = 0.125 fitted to the executed table** (the exponent register is not visible in the decompilation) | 10 | 3.71 (min −20.9) |
  | 108 | Noise | 1024 | 511.5 | same table as 100, but the render takes a separate random path (below) | 7 | 14.745 |

- **Amount:** `x = clamp(0.001·p59 + 0.5·m, 0, 1)` (`m` = a per-voice modulation term read from the voice record —
  source **not confirmed**); **`a = 0.25·(11^x − 1)/10`** (x = 1 → exactly 0.25; `0x7ff514` = ln 11, `0x7d0514` = 0.1,
  `0x787318` = 0.25) **[DECOMP + EXEC]**. p59 = 150 → 0.0108; 500 → 0.0579; 850 → 0.167; 1000 → 0.25.
- **Track frequency motion (p64):** when on, `a` is multiplied by `min(1, Δ²)`, `Δ` = current cutoff position minus a
  smoothed copy of it (one-pole per control block, coefficient `exp(ln 0.01/(1.25·fs/block))`, i.e. 99 % in 1.25 s)
  **[DECOMP `+0x19C`, `+0x6D8`; EXEC ln 0.01]**. Manual: "the self oscillation only happens when there is
  cutoff-frequency motion" **[DOC]**.
- **Position and width:** resonance index `idx_i = o + (log₂ f_i − pos − p61/1200)·k` (octave scale, p60 = 0) or
  `idx_i = o + (f_i − f_c·2^(p61/1200))·k` (Hz scale, p60 = 1) **[DECOMP: offset `+0x684 − (pos + +0x6E4)·k` and
  `+0x684 − f_c·+0x6E0·k`; `+0x6E4 = p61/1200`, `+0x6E0 = 2^(p61/1200)`]**. The scale `k` (`+0x6EC`) and a constant
  `c` (`+0x6F0`) come from `FUN_00788060`:

  | p60 (scale) | p63 (adaptive width) | k | c |
  |---|---|---|---|
  | 0 (oct) | 0 | **1024 entries/octave** | −0.125 |
  | 0 (oct) | 1 | **94 718.57/f₀** (= 1024·440·2^(−27/12)/f₀) | 0.875 |
  | 1 (Hz) | 0 | **0.384 entries/Hz** | −1.5 |
  | 1 (Hz) | 1 | **71.0389/f₀** (= 0.384·440·2^(−15/12)/f₀) | 0.5 |

  `f₀` = voice fundamental in Hz (`+0xF4`) **[DECOMP + EXEC `hconst.txt`]** (same four constants as Harmor's
  `FUN_008744c0`, round 25 §1.3). **There is no separate resonance-width parameter: the width is fixed by the table
  length N and k** — e.g. Classic (N = 1024) spans **1 octave** in total at k = 1024 (half-height width of `sin⁶`
  ≈ 0.30 octave); Wide bump spans 2 octaves; with adaptive width the span is multiplied by `f₀/92.4986 Hz` (oct) or
  in Hz scale Classic spans 1024/0.384 = 2667 Hz (×`f₀/184.997` adaptive) **[derived]**. Manual: "Adaptive resonant-peak
  width. The resonant peak changes width as a function of cutoff frequency" **[DOC]** — the code divides by the *note*
  frequency f₀, not the cutoff (**divergence** with the manual's wording).
- **Gain** (`FUN_00784180`), per partial, multiplied on the amplitudes after the filter gain:

  > **G_i = max(0, 1 + R_i · max(0, (log₂(f_i/f₀) + c)·a))**, `R_i = T_res[idx_i]`

  So the boost also grows with the partial's distance in octaves from the fundamental (`log₂ k` for harmonic k), and with
  c = −0.125 (default oct, non-adaptive) **the fundamental is never boosted** **[DECOMP]**. Example (Classic LP,
  p56 = 500, f₀ = 110 Hz, f_c = 880 Hz, Classic resonance, p59 = 1000): harmonic 8 (= f_c) gets +21.3 dB, harmonic 10
  +2.0 dB, the others ≈ 0 **[model `hlfilt.py demo`]**.
- **Self-oscillation (p62):** special partial 0 sits at `f_c·2^(p61/1200)` (`+0x6E0` × 2^pos, set every block) and its
  amplitude is set each block to (gain-table entry `*(+0x570)+0x840`) × `a` × (an entry of a voice table)
  **[DECOMP]**; that the first factor is the `p62/256` gain of special partial 0 (round 21 §1.3) is **inference**, and
  the meaning of the last factor was **not confirmed**.
- **Noise (108):** separate path — per partial a uniform random value is drawn (`ippsRandUniform_Direct_32f` over
  `[0, 1 + q]`, `q = (4a)³·9999/+0x720`), thresholded and multiplied in, with a phase accumulator `+0x718`; not decoded
  further (**not confirmed**).
- Data: p59 > 0 in 75/139 channels; resonance types with p59 > 0: Classic 46, Wide bump 15, Double cone 4, Wormhole 4,
  Well 2, Noise 2, Sedge hat 1, Spring 1; p60 = 1 in 37; p63 = 1 in 22; p64 = 1 in 26; p61 ≠ 0 in 30; p62 ≠ 0 in 14
  **[DATA]**.

---

## 2. Effector (engine)

**What LYNX assumes** (`src/core/Effector.cpp`): Filter = 4-pole ladder with feedback (comment: "FL's cascade was not
tabulated"); Flanger = 5 ms line with delay `X²·5 ms`, line input `−(x + fb·delayed)`; Phaser = 6 all-passes with
`g = S·(1 + 0.5·LFO)…`; Reverb = LYNX reverb with Decay `0.75X'³`.

### 2.0 LFO (all units) **[DECOMP `FUN_180017ec0`]**

Phase `+0x30` (synced to the song, round 19 §1.0), increment `+0x38 = 1/(T·fs)` per call. Shape 0: `sin(2π·φ)`;
shape 1: `1 − 2φ`; shape 2: `+1` for φ ≤ 0.5, `−1` above (`0x180058440` = 0.5). The sine and the square read the phase
*before* it is advanced, the saw reads it *after* **[DECOMP]**. Filter and Reverb evaluate it **once per block** (and
then jump the phase by the rest of the block); Flanger and Phaser **every sample** **[DECOMP]**.

### 2.1 Filter (type 4) — exact cascade **[DECOMP `FUN_180019110`; EXEC `eunit.exe` + model `effx.py`]**

Per block: `X' = clamp(X + l·ModX, 0, 1)`, `Y' = clamp(Y + l·ModY, 0, 1)`, **`s = 2·(0.8·X' + 0.1)`**.

- **Low-pass (s ≤ 1):** `c = s³`; feedback `k = 0.7·Y' + 0.2`, and if `s > 0.845`: `k ← k·(1 − (s − 0.845)/0.155)²`
  (fades to 0 at s = 1); stage gain `g = 1 + 0.6·k`. **Four identical sections in series**, each section =
  **8 one-pole low-passes** `y_j ← y_j + c·(u_j − y_j)` in series, whose input is `u_0 = in − k·y_7[n−1]` (its own
  8th-stage output from the previous sample); the section output `y_7` is multiplied by `g` and feeds the next section
  (also after the 4th) **[DECOMP]**. Per section `H_s(z) = g·H₁⁸/(1 + k·z⁻¹·H₁⁸)`, `H₁ = c/(1 − (1−c)z⁻¹)`; total
  `H = H_s⁴` (32 poles). DC gain `((1 + 0.6k)/(1 + k))⁴` (Y = 0: −2.4 dB; Y = 1: −7.3 dB).
- **High-pass (s > 1):** `c = 0.5·(s − 1)³`; feedback `k = 6.2568·Y'`; **32 one-pole high-pass stages** in series
  (`lp_j ← lp_j + c·(v − lp_j)`, `v ← v − lp_j`), input `v_0 = in − k·lp_15[n−1]`, i.e. the feedback is taken from the
  **low-pass state of stage 16** (previous sample); no compensation gain **[DECOMP]**.
  `H = H_h³²/(1 + k·z⁻¹·H_l·H_h¹⁵)`, `H_l = c/(1 − (1−c)z⁻¹)`, `H_h = 1 − H_l`.
- The inactive branch's states are cleared every block; mix `(1 − w)·dry + w·wet`, `w` smoothed 0.01/sample
  **[DECOMP]**.
- **Verification:** DLL vs model on an impulse (X, Y) = (0.2, 0.5), (0.5, 0.8), (0.8, 0.3): max difference
  **6·10⁻⁸** **[EXEC + model]**. Closed-form response (44.1 kHz, LFO 0) **[model `effx.py table`]**: LP −3 dB ≈ 107 Hz
  (X 0.1), 339 Hz (0.2), 761 Hz (0.3), 1.7 kHz (0.4), 2.0 kHz (0.45) with Y = 0; Y = 1 adds a peak of +26 dB (X 0.1),
  +24 dB (0.2), +19 dB (0.3), +10 dB (0.4), relative to DC; HP −3 dB ≈ 95 Hz (X 0.6), 761 Hz (0.7), 2.6 kHz (0.8),
  6.1 kHz (0.9), 10.8 kHz (1.0) with Y = 0.

### 2.2 Flanger (type 2) — tape delay with feedback **[DECOMP `FUN_180019d60`, `FUN_180018700`, `FUN_180017f80`, `FUN_180017e20`, `FUN_180018120`, `FUN_180018520`, `FUN_180018560`, `FUN_1800181c0`; EXEC + model]**

- Two `CTapeDelay` objects (L, R) of length **`Lₜ = trunc(0.005·fs)`** samples (`0x180058318` = 0.005; 220 at 44.1 kHz)
  **[DECOMP]**.
- **Per sample:** `d = clamp(X² + l·ModX, 0, 1)`, `fb = −0.96·clamp(Y + l·ModY, 0, 1)` **[DECOMP]**. The delay setter
  turns `d` into a tape *speed* `v* = 1/D` with **`D = 2 + d·(Lₜ − 2)` samples**; the running speed `v` follows `v*`
  with a one-pole of coefficient **0.005 per sample** (so delay changes glide, with the resulting pitch bend); `v` is
  limited to ≤ 0.9 **[DECOMP]**.
- **Tape model** (our description): a write position `P` advances by `v` every sample; each sample writes
  (position, value) on the tape; the read returns the value written exactly one tape length earlier (`P − 1`), linearly
  interpolated between the two written samples that bracket it → a delay of **1/v samples** (= D in steady state)
  **[DECOMP]**. An internal triangle "wow" LFO exists but its depth is 0 in this use (`FUN_180018580`) **[DECOMP]**.
- **Feedback topology:** written value `= fb·r + (−x)`, where `r` is the tape read and `x` the input times the unit's
  activation ramp (the input enters **inverted**); the two filters inside the tape loop are neutral here (coefficients
  1 and 0 from the constructor) **[DECOMP]**. **Output = x + w·r** (dry kept at 1) **[DECOMP]**.
- Note for LYNX: the inverted input and the feedback sign give `w[n] = −x[n] − 0.96·Y'·w[n−D]`; LYNX writes
  `−(x + fb·delayed)` = `−x + 0.96·Y'·delayed` → **opposite feedback sign**; LYNX's delay is `d·5 ms` (FL: `2 + d·218`
  samples at 44.1 kHz, minimum 2 samples) and has no speed glide.
- **Verification:** DLL vs model (X = 0.5, Y = 0.5, including the glide from the constructor's d = 0.5): max difference
  **3.7·10⁻⁸** **[EXEC + model]**.

### 2.3 Phaser (type 3) **[DECOMP `FUN_18001a680` + `0x18001a7f0…0x18001a82d`; EXEC + model]**

- `S` = smoothed `X²` (raw X, floor 10⁻⁵, coefficient 0.001 per sample, initialised to X² on first use);
  `X' = clamp(X + l·ModX, 0, 1)`; **`g = S·(1 + 0.5·X')·(20000 − 50)/fs + 50/fs`**, then `g ≤ 0.499` and `g < 0.001 →
  0.001`; all-pass coefficient **`a = (1 − g)/(1 + g)`** **[DECOMP]**.
- **Feedback** `fb = 0.98·(0.6·clamp(Y + l·ModY, 0, 1))^(1/3)` (`powf` with exponent 1/3 at `0x18005835c`, 0.6 at
  `0x18005837c`) **[DECOMP]**.
- **Chain per channel:** `u = x − 10⁻⁶ + fb·y₆[n−1]`; six first-order all-passes in series, each `y = s − a·u`,
  `s ← u + a·y` (transfer `(z⁻¹ − a)/(1 − a·z⁻¹)`); `y₆` is both the wet signal and the feedback source; **output
  `x + w·y₆`** **[DECOMP]**.
- LYNX difference: LYNX uses `(1 + 0.5·LFO)`; FL uses `(1 + 0.5·X')` with `X' = X + LFO·ModX` clamped.
- **Verification:** DLL vs model (X = Y = 0.5): max difference **4.3·10⁻⁸** **[EXEC + model]**.

### 2.4 Reverb (type 6) — decay law **[DECOMP `FUN_18001a130` @ `0x18001a4c4…0x18001a5cb`, `FUN_18001cc30`, `FUN_18001dae0` @ `0x18001e7e2…0x18001e8af`, `FUN_18001c090`; EXEC `erev.exe`]**

- **Parameters written every block** (fxReverb slot 10, value in the 3rd argument): p9 = `Y'²`, **p8 = `0.75·X'³`**,
  p12 = `0.3·0.75·X'³ + 0.5`, p2 = 1, p1 = 0.1, p5 = 1, p4 = 1, p3 = 0, p10 = 0, p16 = 1 **[DECOMP]** (the earlier
  rounds' "Decay/level/HighDamping" reading is consistent; the fxReverb parameter *names* were not read).
- The setter stores each value and an integer version: **p8 → `D = ⌊199·v + 1.5⌋`** (`0x1800585b4` = 199,
  `0x180058420` = 1.5); p4 → `⌊99·v + 1.5⌋` (room size, = 100 here) **[DECOMP + EXEC]**.
- **Loop lengths** (`FUN_18001dae0`): `B = (room + 15)·fs/115` (= fs for room 100); four main delay lines of
  `B·{4453, 3720, 4217, 3163}/20000` samples (222.65, 186.0, 210.85, 158.15 ms) plus eight shorter all-pass/diffusion
  lines (`B·{142, 107, 379, 277, 672, 1800, 908, 2656}/20000`) **[DECOMP]**.
- **Decay coefficient** (`+0x290`): with `Σ` = sum of the four main lengths in samples,

  > **g = exp(ln(0.001)·(Σ/fs)/(0.1·D))^0.25 = 0.001^(τ̄/T)**, `T = 0.1·D` s, `τ̄ = Σ/(4·fs)` = 0.1944 s

  i.e. the usual "−60 dB in T seconds" loop gain for the mean loop length **[DECOMP: `0x180058634` = ln 0.001,
  `0x18005833c` = 0.1, exponent 0.25 at `0x180058350`; EXEC: g = 0.914366 for X = 1]**. A second decay coefficient
  uses p6 (= 10 by default → multiplier 1.0) and p7 (= 500) for a low-frequency decay ratio; with the defaults the ratio
  is 1 (`+0x2B8` = 0) **[EXEC]**.
- **Effector law:** **`T = 0.1·⌊149.25·X'³ + 1.5⌋` s** → X = 0: 0.1 s; 0.25: 0.3 s; 0.5: 2.0 s; 0.75: 6.4 s; 1: 15.0 s
  **[DECOMP + EXEC fields]**.
- **Measured** (Schroeder decay of the DLL's impulse response, −5 → −25 dB slope ×3, 44.1 kHz) **[EXEC +
  `rt60.py`]**: X = 0.25: 0.50 s (Y 0) / 0.51 s (Y 0.5); X = 0.5: 2.05 / 2.39 s; X = 0.75: 5.6 / 7.2 s; X = 1: 11.9 /
  15.5 s. The nominal T is the right order; Y changes the measured decay (p9 = Y'² is not a pure damping factor in this
  measurement) — the exact role of p9 and the diffusion/early-reflection network were **not decoded**.
- The dry signal stays at gain 1 and the reverb input is `w·x` (round 19 §1.6).

---

## 3. Fruity Love Philter

### 3.1 Pan law in the output routine **[DECOMP `FUN_00616620`, `FUN_0062afb0`/`FUN_0062cff0`, `FUN_0062d840`, `FUN_00629090`, call site `0x85bffb…0x85c16e`; EXEC `lppan.exe`]**

- Call: `pan = PAN/128 + 2·s_pan`, `vol = (+0x48)·s_vol`, **mode 0** (`R9 = 0`) **[DECOMP]**. No clamp of `pan` was
  found at the call site, in `FUN_00616620` or in the articulator `FUN_008543d0` **[DECOMP]**.
- **Mode-0 law** (pointer `0x8e3660`, set at init to `FUN_0062afb0` or `FUN_0062cff0`, both the same law):
  **`G_L = √2·vol·cos((1 + pan)·π/4)`, `G_R = √2·vol·sin((1 + pan)·π/4)`** (`0x8e34e4` = 1/cos(π/4) = √2, read at run
  time) → unity at the centre, +3 dB at the extremes **[DECOMP + EXEC]**.
- **Stereo fold:** the routine returns a 2×2 matrix `(LL, RR, R→L, L→R)`, applied by `FUN_00629090` as
  `outL = LL·inL + R→L·inR`, `outR = RR·inR + L→R·inL` **[DECOMP]**:
  - `pan = 0`: `LL = RR = vol`, no cross terms.
  - `pan < 0`: `f = 1 − G_R/vol`; **`LL = G_L·(1 − f/2)`, `R→L = G_L·f/2`, `RR = G_R`, `L→R = 0`.**
  - `pan > 0`: `f = 1 − G_L/vol`; **`RR = G_R·(1 − f/2)`, `L→R = G_R·f/2`, `LL = G_L`, `R→L = 0`.**
  - `vol = 0` → all zero.
  E.g. hard left: `LL = R→L = 0.7071·vol`, `RR = 0` (the right input is folded into the left output). Executed values
  for vol = 0.8 match these formulas exactly (`lppan.txt`, e.g. pan −0.5: LL 0.80547, RR 0.43296, R→L 0.23978)
  **[EXEC]**. Out-of-range pans (|pan| > 1) give negative gains (e.g. pan −1.5: RR = −0.433) **[EXEC]**.
- The four gains are ramped linearly across each *tick* block (`FUN_0062aee0`, slope limited by the host's maximum
  step `+0x38`), and jump without a ramp on the first tick after a reset (`+0x16C4`) **[DECOMP]**.
- Mode 1 (not used by Love Philter's call): linear balance (`G_side = vol`, other side `(1 − |pan|)·vol`)
  **[DECOMP `FUN_0062afe0`/`FUN_0062d060`]**.

### 3.2 Envelope curve type 5 ("pulse") **[DECOMP `FUN_00783ec0`, `FUN_007858b0`, `FUN_00786350`; constants `0x7859a0…0x7859c0`, mask `0x784a20` = 0x1B83, `0x7859c0` = 0x78]**

- At preparation, for points of types 3…6 (`0x78` mask) a count is computed from the point's tension `t` ∈ [−1, 1]:
  **`n = round(0.001·(1000001^(1 − 0.6·|t|) − 1))`** (t = 0 → 1000; 0.3 → 83; 0.5 → 16; 0.7 → 3; 0.9 → 1; 1 → 0)
  **[DECOMP]**.
- Type 5 stores `P = 2·(n + 1)` and `c = +0.5` (t ≥ 0) or `−0.5` (t < 0) **[DECOMP]**.
- **Value inside the segment** (x ∈ [0, 1] from the previous point to this one): **`v(x) = round(P·x − c) mod 2`**
  (round = `FUN_0040c520`, x87 round-to-nearest), and the level is `a + (b − a)·v` **[DECOMP]** → a **square wave of
  n + 1 full cycles** between the previous level `a` and the point's level `b`; t ≥ 0 starts on `a`, t < 0 starts on `b`.
  Tension 0 is *not* linearised for type 5 (it is not in the "zero tension → linear" mask `0x1B83`) → t = 0 gives 1001
  cycles **[DECOMP]**.
- Demo point ("Drumloop Masher", unit 8, volume, Δt = 0.25 beat, y = 0, t = +1.0, round 20 §1.7): n = 0, P = 2 → the
  level stays at the previous value (1) for the first half (0.125 beat) and is 0 for the second half **[derived]**.

### 3.3 LFO tension (pan LFO) **[DECOMP `FUN_007886b0`, `FUN_0060e450`, `FUN_0060e3c0`, `FUN_0060e610`, `FUN_00788b70`]**

- Confirms round 20 §1.5: `a = 0.05·(21^(|T|/128) − 1)`, **`q = sign(T)·(10¹⁸^a − 1)`** (`0x788808` = 21,
  `0x788818` = 0.05, `0x788820` = 10¹⁸, `0x8a46cc` = 1/128) **[DECOMP]**.
- **Where it is applied:** to the raw waveform `w` ∈ [−1, 1] (triangle with skew/pulse width) **before** the LFO
  amplitude envelope: `L = A(t)·W(φ)` (`FUN_00788b70`: `value = A·W`) **[DECOMP]**. With flag bit 2 clear (all demos):
  `W = sign(w)·S(|w|)`, `S(y) = 1 − ((1 + q)^(1 − y) − 1)/q` for q > 0, `S(y) = ((1 + |q|)^y − 1)/|q|` for q < 0,
  `S = |w|` for q = 0 → **mirrored about 0**; with bit 2 set: `W = 2·S((w + 1)/2) − 1` (shaped over the full
  −1…1 range) **[DECOMP]**. The plateaus of the pulse-width shape return exactly ±1 (unshaped) **[DECOMP]**.
- For the pan target: **`pan = PAN/128 + 2·d_L·A·W(φ)`**, then the law of §3.1. Demo pan LFOs: T = 34, 40, 42, 81 →
  q ≈ 12, 26, 34, 1.9·10⁵ (round 20); with q = 12, `S(0.5) = 0.78` (the triangle bulges towards ±1) **[derived]**.
- Which bit-2 value corresponds to the "Bipolar LFO tension" label remains **not confirmed** (round 20).

---

## 4. Not decoded / open in this round

- Harmless: the source of the per-voice resonance-modulation term `m`; the voice factor in the self-oscillation
  amplitude; the Noise (108) path in detail; who switches the table lookup to interpolation (quality); whether the
  tremolo counter `+0x330` is ever reset; the exponent of the Wormhole shape is fitted (0.125), not read.
- Effector Reverb: names/roles of the fxReverb parameters other than p8 (decay) and p4 (room); the diffusion and
  early-reflection network; why Y changes the measured decay.
- Love Philter: label of LFO flag bit 2; curve types 1, 2, 3, 4, 6…12 were seen in the evaluator but not restated here.

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| 1a | Harmless tremolo | complex tone 2.5 Hz, magnitude 1.25, rectified: `g_L = 1 + d(|1.25cos θ| − 1)`, `g_R = 1 + d(|1.25sin θ| − 1)`, `d = 0.001·p25`; 5 Hz per channel, L/R anti-phase; free-running; 0.1 s fade-in per new voice **[DECOMP]** |
| 1b | Harmless vibrato | `0.075·p41·r²·sin(2π·6t)` cents, global free-running 6 Hz phase, `r` linear 0→1 in 1/3 s per voice, per control block **[DECOMP]** |
| 1c | Harmless filter law | `F = T_type[(log₂ f − pos)·W + o]`, `W = 1024·2^(1−0.004·p56)`/oct; Hz scale `(f/f_c − 1)·0.9W + o`; 17 tables restated and matched to the DLL **[DECOMP + EXEC]** |
| 1d | Harmless resonance | `G = max(0, 1 + T_res[idx]·max(0, (log₂(f/f₀) + c)·a))`, `a = 0.25(11^x − 1)/10`, `idx = o + (log₂ f − pos − p61/1200)·k`; 9 shapes restated (Wormhole exponent fitted) **[DECOMP + EXEC]** |
| 1e | Harmless resonance width | no width parameter: k = 1024/oct (94 718.6/f₀ adaptive), 0.384/Hz (71.04/f₀ adaptive); span = N/k **[DECOMP + EXEC]** |
| 2a | Effector Filter | LP: 4 × (8 one-poles c = s³ with own feedback k = 0.7Y' + 0.2, faded above s = 0.845, gain 1 + 0.6k); HP: 32 one-pole HPs c = 0.5(s−1)³, feedback 6.2568Y' from LP state 16 **[DECOMP + EXEC bit-close]** |
| 2b | Effector Flanger | tape delay `D = 2 + d(⌊0.005fs⌋ − 2)` samples, speed glide 0.005/sample, written `−x + fb·r`, fb = −0.96Y', out `x + w·r` **[DECOMP + EXEC]** |
| 2c | Effector Phaser | `g = S(1 + 0.5X')·19950/fs + 50/fs` ∈ [0.001, 0.499], `a = (1−g)/(1+g)`, 6 all-passes, feedback `0.98·∛(0.6Y')` from stage 6, out `x + w·y₆` **[DECOMP + EXEC]** |
| 2d | Effector Reverb decay | `T = 0.1·⌊149.25X'³ + 1.5⌋` s; loop gain `0.001^(τ̄/T)`, τ̄ = 0.1944 s; measured EDC 0.5–15.5 s **[DECOMP + EXEC]** |
| 3a | Love Philter pan law | `G_L = √2·vol·cos((1+p)π/4)`, `G_R = √2·vol·sin(…)`, plus fold of the far channel into the near one with `f = 1 − G_far/vol`, halves; no clamp **[DECOMP + EXEC]** |
| 3b | Curve type 5 | square wave, `n + 1` cycles, `n = round(0.001(1000001^(1−0.6|t|) − 1))`, phase by sign of t **[DECOMP]** |
| 3c | Pan-LFO tension | applied to the raw waveform (mirrored, sign kept), before the amplitude envelope and depth: `pan = PAN/128 + 2·d_L·A·sign(w)·S_q(|w|)` **[DECOMP]** |

---

## 5. For LYNX

1. **Harmless tremolo** (`synthFromHarmless`): if p25 ≠ 0, add a post-pan stereo gain stage shared by all voices:
   `θ = 2π·2.5·t` (free-running), `gL = 1 + d·(|1.25·cos θ| − 1)`, `gR = 1 + d·(|1.25·sin θ| − 1)`, `d = p25/1000`;
   new voices blend in over 0.1 s (`(1 − r) + r·g`). Remove any mapping of p25 to the main LFO.
2. **Harmless vibrato:** if p41 ≠ 0, pitch offset `0.075·p41·r²·sin(2π·6·t)` cents with a global 6 Hz phase and a
   per-voice ramp `r = min(1, 3·t_note)`.
3. **Harmless filter, exact per note** (bake into the additive spectrum each control block): for each partial
   `f_k`, multiply by `F(f_k)` from the table of §1.3 at `idx = (log₂ f_k − pos)·W + o` (Hz scale: `(f_k/f_c − 1)·0.9W
   + o`), clamped to [0, N−1], truncated; then by `G(f_k)` of §1.4. Reference code: `tmp/r27/py/hlfilt.py`
   (`filter_gain`, `res_gain`; tables from `run/hshape.txt` or the closed forms in `table()`).
   **If a biquad must stay:** match the corner from the table (LP types: gain is exactly 1 up to `f_c` and falls over
   `N/W` octaves; Classic LP at p56 = 500 is −5.8 dB at +0.32 oct, −10.5 dB at +0.58 oct, −18 dB at +1 oct, −36 dB at
   +2 oct **[model]**), and for the resonance use a peak at `f_c·2^(p61/1200)` whose height is
   `1 + T_peak·(log₂(f_c/f₀) + c)·a` (T_peak = 14.745 for Classic) and whose width is the table span (Classic:
   ±0.5 octave, half-height ≈ 0.30 octave; ×f₀/92.5 Hz with adaptive width). Note the peak height depends on the note
   (log₂(f_c/f₀)) and is 0 when f_c is at or below ~f₀·2^0.125.
4. **Effector** (`core/Effector.cpp`): replace the Filter with the two exact cascades of §2.1 (`effx.py filt()`);
   fix the Flanger to the tape-delay law (`D = 2 + d·(⌊0.005fs⌋ − 2)` samples with 0.005/sample speed smoothing,
   linear interpolation) and the **feedback sign** (`w = −x + fb·r`, fb = −0.96Y'); in the Phaser use
   `(1 + 0.5·X')` with `X' = clamp(X + LFO·ModX)`; evaluate the Flanger/Phaser LFO per sample. Reverb: set the LYNX
   reverb's RT60 to `T = 0.1·⌊149.25X'³ + 1.5⌋` s (measured DLL decays in §2.4 for calibration).
5. **Love Philter** (`FlEffects.cpp`, pan articulator / auto-pan): output stage = the matrix of §3.1 with
   `p = PAN/128 + 2·s_pan` unclamped (`G_L = √2·vol·cos((1+p)π/4)`, `G_R = √2·vol·sin((1+p)π/4)`, fold `f/2` of the far
   input into the near output); pan LFO: shape the raw triangle with `sign(w)·S_q(|w|)` before multiplying by the LFO
   level `A` and depth; envelope curve type 5: square wave of `n + 1` cycles between the two point levels
   (`n` from §3.2), first half-cycle on the previous level for t ≥ 0.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Harmless `FUN_007886b0` | tremolo: `ippsTone_Direct_32fc` (1.25, 2.5/fs, phase from `+0x330`), Abs, −1, depth ramp, +1; vibrato phase advance | DECOMP |
| Harmless `FUN_00784a10` | vibrato `sin·0.075·p41·r²`; log/Hz partial arrays; filter lookup; resonance amount `(11^x−1)/10·0.25`, track-motion factor; `FUN_00784180` call; tremolo multiply and 0.1 s fade-in | DECOMP |
| Harmless `FUN_0077f170` | `2.5/fs`, `6/fs`, `3/fs`, `round(0.1·fs)`, smoothing coefficients | DECOMP + PE |
| Harmless `FUN_0077f630` cases 0x19, 0x29, 0x38, 0x3D | `0.001·p25`, `0.075·p41`, W, `p61/1200` and `2^(p61/1200)` | DECOMP |
| Harmless `FUN_0077ca30`, `FUN_0077c880`, `FUN_0077c0f0` + `hshape.txt` | 26 tables, origins, RMS normalisation, code = type / type + 100 | DECOMP + EXEC |
| Harmless `FUN_0077c540`, `FUN_0077c470`, `FUN_00784180` | clamp-and-truncate lookup; resonance gain formula | DECOMP |
| Harmless `FUN_00788060` + `hconst.txt` | k and c for (p60, p63); ln 11, ln 0.01, ln10/20, log₂ 440 at run time | DECOMP + EXEC |
| Harmless IPP dispatcher `FUN_005acdb0` (`ippmap.py`) | pointer → IPP name pairs | DECOMP |
| `blobs14.pkl` Harmless (139) | usage of p25, p41, p55…p64, filter/resonance types | DATA |
| Manual `…/plugins/Harmless.htm` | tremolo, vibrato, resonance, adaptive width, motion | DOC |
| Effector `FUN_180019110` + `eu_4_*.txt` | Filter cascades; DLL = model ≤ 6·10⁻⁸ | DECOMP + EXEC |
| Effector `FUN_180019d60`, `FUN_1800181c0`, `FUN_180017f80`, `FUN_180018520`, `FUN_180018700` + `eu_2_*.txt` | tape delay length/speed/glide, inverted input, feedback, dry + w·r | DECOMP + EXEC |
| Effector `FUN_18001a680` (+ asm `0x18001a7f0…`) + `eu_3_*.txt` | Phaser coefficient, ∛ feedback, 6 all-passes | DECOMP + EXEC |
| Effector `FUN_18001a130` asm, `FUN_18001cc30`, `FUN_18001dae0`, `FUN_18001c090` + `rv_*.bin` | reverb parameters, quantisation, loop lengths, decay coefficient; measured decays | DECOMP + EXEC |
| Love Philter `FUN_00616620`, `FUN_0062afb0`/`FUN_0062cff0`, `FUN_0062d840`, `FUN_00629090` + `lppan.txt` | sin/cos √2 law, fold matrix, mixer, run-time pointers | DECOMP + EXEC |
| Love Philter `FUN_00783ec0`, `FUN_007858b0`, `FUN_00786350` | curve type 5 count, period and evaluation | DECOMP |
| Love Philter `FUN_007886b0`, `FUN_0060e450`, `FUN_0060e610`, `FUN_00788b70` | LFO tension constants and application order | DECOMP |
