# Behavioural analysis of FL Studio 2026: round 33 (REA) — Effector Reverb network, Toxic Biohazard filter envelope, Kepler chorus noise/gate and LFO BPM sync

> **Nature of this document.** Continuation of `REA_rodada27.md` §2.4 (Effector Reverb: only the decay law was
> known), `REA_rodada19.md` §4 / `REA_rodada26.md` §2 (Toxic Biohazard filter) and `REA_rodada26.md` §3 /
> `REA_rodada19.md` §3 (Kepler chorus, LFO1). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor
> CRT) code or pseudocode was copied: what is here are formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched (no FLEX
> pack was opened). Plugin states used as test input (Kepler demo channels and presets) were read from the user's
> own installation into the scratch directory only; nothing from them is reproduced here beyond the few parameter
> values quoted as evidence.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running a function of the
> installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not
> confirmed"**. Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical
> models (which implement only what is marked DECOMP/EXEC) are marked **[model]**.

- **Targets:**

  | Plugin | DLL (SHA-256, hashed against the installation in this round) | Language | Key functions |
  |---|---|---|---|
  | Effector (engine) | `Plugins/Fruity/Effects/Effector/engine_x64.dll` `4bfdc0f61ffc38f004e305bf98f8183cfcf0f0c4df29509e391ebec57dc2071a` (= rounds 14/18/19/27) | C++ (MSVC) | Effector reverb unit `FUN_18001a130`; `fxReverb` (VMT `0x180057fc0`): constructor `FUN_18001c640`, buffer sizing `FUN_18001c090`, state reset `FUN_18001ca20`, parameter setter (VMT slot 10) `FUN_18001cc30`, coefficient update `FUN_18001dae0`, level helper `FUN_18001dab0`, process (VMT slot 43) `FUN_18001e9f0`; CRT `FUN_18003ff50` (exp), `FUN_18002e2d0` (pow), `FUN_180041ab0` (tan), `FUN_18003fa50` (cos) |
  | Toxic Biohazard | `Plugins/Fruity/Generators/Toxic Biohazard/Toxic Biohazard_x64.dll` `5c326637be38fcea7148e170b83e6bec8600c87f00fa774eb610ed68f136cb46` (= rounds 15/18/19/26) | C++ (MSVC, `CToxicGenerator`) | per-sample render `FUN_180038d00`, per-block voice update `FUN_180039880`, amplitude-envelope stage `FUN_1800393b0`, filter-envelope stage + cutoff targets `FUN_1800395b0`, envelope time law `FUN_18003d480`, filter-EG parameter setter `FUN_180040080`, amplitude-EG parameter setter `FUN_18003e510`, voice start (parameter latch) `FUN_18003a3c0`, note-off latch `FUN_18003a660`, voice reset `FUN_18003a730`, parent getter `FUN_18003a720`, `CreatePlugInstance` `0x180002350` |
  | Kepler (engine) | `Plugins/Fruity/Generators/Kepler/engine_x64.dll` `baf37a1d8266aaafc55016de259be4b95f1a136a1fc13ba97514c72e2afc63ab` (= rounds 13/17/18/19/26) | C++ (MSVC) | `CreatePlugInstance` `0x180009da0`, plugin constructor `FUN_1800078c0` (synth object stored at plugin `+0x260`), synth constructor `FUN_1800166d0`, defaults initialiser `FUN_18001ed30`, default-value table `FUN_18001d900`, state loader `FUN_18001f170` (synth VMT slot 16), panel → internal map `FUN_180016300`, parameter forwarder `FUN_1800199b0`, deferred-change helper `FUN_180021ca0`, global *setter* `FUN_180019a20` (cases 0x30/0x31/0x45–0x47/0x57), block render `FUN_180019210`, per-sample global render `FUN_18001b940`, chorus constructor `FUN_180001770`; CRT `FUN_1800c15a0` (pow), `FUN_1800bfb00` (ln) |
  | Kepler (front-end) | `Plugins/Fruity/Generators/Kepler/Kepler_x64.dll` `9db78f5a67af7dd1c932c3e2e8d13114905e9482e6367e797245098849bc4d5b` | Delphi | engine loader `0x6d58d0` / `0x6d10e0`, forwarders `0x6d4290` (ProcessParam), `0x6d5790…0x6d57f0`, `0x6d5820`/`0x6d5870` (dispatcher) — static reading only |

- **Tools:** Ghidra 12.1.4 *headless* (`GHIDRA_HEADLESS_MAXMEM=3G`): new projects `/tmp/r33gh_Tox` (`Toxic.dll`) and
  `/tmp/r33gh_Kep` (`kepeng.dll`), imported once and then opened read-only; runner `tmp/r33/g.sh`, scripts
  `tmp/r33/scripts/` (copies of round 27's). The Effector analysis reused round 27's dumps (`tmp/r27/out/e1.c`,
  `e5.c`, `e6.c`) plus `objdump` disassembly of `FUN_18001dae0` and `FUN_18001e9f0` (`tmp/r33/out/dae0.asm`,
  `e9f0.asm`) for the CRT-call arguments the decompiler hides. Dumps: `tmp/r33/out/t1.c…t6.c` (Toxic), `k1.c…k7.c`,
  `b940.asm` (Kepler). Constants read from the PE with `tmp/r18/k.py`. Our own models (pure Python, `python3 -I`):
  `tmp/r33/py/efxrev.py`, `tmp/r33/py/toxeg.py`, `tmp/r33/py/kepsync.py`.
  **Execution harnesses** (our own C, built with Zig 0.14.1 for Windows; `SetErrorMode(SEM_FAILCRITICALERRORS|
  SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, vectored fault handler that ends the process, `TerminateProcess` at
  the end without the DLL's shutdown, run under `timeout`; no process or dialog was left behind):
  - `tmp/r33/run/erev33.c` → `erev33.exe` (extends round 27's `erev.c`): builds the Effector's `fxReverb` with its own
    constructor, sets the rate through VMT slot 6, writes the ten parameters exactly as the Effector reverb unit does
    every block, runs VMT slot 43 on an impulse (L+R or L only) or a 50 ms stereo noise burst, any rate and block
    size; writes input and output (`g_*.bin`) and dumps every derived field of the object (`d_*.txt`).
  - `tmp/r33/run/tox33b.c` → `tox33b.exe` (derived from round 28's `plug.c`): creates the Toxic plugin through its
    `CreatePlugInstance` with a stub host, sets parameters through the synth's `setParameter` (slot 23), plays a note
    (slots 1/2) and renders through `Gen_Render` **one sample per call**; the active voice is found by scanning the
    synth object for a pointer to an object whose parent pointer (`+8`) is the synth and whose elapsed time is
    non-zero; after every sample it logs the filter envelope (`+0x70`), its target (`+0x6c`), coefficient (`+0x74`),
    elapsed time (`+0x88`) and release counter (`+0x80`) (`tx_e1…e4.txt`). (The file also contains a hardware-
    breakpoint variant, `TOXBP`, which never fired and was not used.)
  - `tmp/r33/run/kep33.c` → `kep33.exe`: creates the Kepler engine plugin through its `CreatePlugInstance`, takes the
    synth from plugin `+0x260`, renders, then calls the engine's own state loader `FUN_18001f170` on each saved state
    (through a minimal read-only stream) and prints the chorus noise/gate and LFO1-sync fields before and after a
    render (`kep_a.txt`); also calls the two CRT helpers on test values.
  - `tmp/r33/run/kepfe.c` → `kepfe.exe`: attempt to run the **front-end** `Kepler_x64.dll` with a stub host; it
    faults inside the front-end's `CreatePlugInstance` (null interface pointer at RVA `0x2d1c64`, i.e. a host service
    the stub does not provide), so no front-end EXEC result exists in this round.
- **Date:** 2026-10-14.
- **Dataset:** `tmp/r14/blobs14.pkl` (Kepler: 4 demo channels, 281 `.fst` presets); test states extracted to
  `tmp/r33/run/kst/`.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by calling a function of the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

No manual page was read in this round.

---

## 1. Effector Reverb (unit type 6) — the complete network

### 1.1 What the Effector unit feeds the reverb **[DECOMP `FUN_18001a130`]** (confirms round 19 §1.6)

- Per sample the unit multiplies a copy of the input by `e·w` (`e` = enable fade, `w` = smoothed wet; both follow their
  targets with coefficient 0.01/sample), then calls the reverb's process routine with that copy as input and **the
  unit's own buffers as output**; the process routine **adds** to its output buffers → **`out = x + R(w·x)`**.
- Before each call the unit rewrites ten `fxReverb` parameters (value in parentheses, `X'`, `Y'` = X/Y after LFO
  modulation as in round 27 §2.0): p9 = `Y'²`, p8 = `0.75·X'³`, p12 = `0.3·0.75·X'³ + 0.5`, p2 = 1, p1 = 0.1, p5 = 1,
  p4 = 1, p3 = 0, p10 = 0, p16 = 1. All other parameters keep the values of the reverb's constructor (preset 0):
  **[EXEC field dump]** p0 → 0 (0 dB), p6 → 10, p7 → 500, p11 → −35, p13 → 100, p14/p15/p17 → 0.

### 1.2 Parameter quantisation and roles **[DECOMP `FUN_18001cc30`, `FUN_18001dae0`, `FUN_18001dab0`; EXEC field dump]**

Every parameter `v` is stored as the integer **`i = ⌊k·v + c⌋`** (float arithmetic, floor). The level helper maps an
integer in tenths of a dB to a gain: **`L(i) = 10^(i/200)`, and 0 for `i < −360`** (`0x180058318` = 0.005).
Laws (`fs` = the object's rate, `B` and the line lengths in §1.3):

| p | (k, c) | Role (behaviour read in the code) | Effector value |
|---|---|---|---|
| 0 | (421, −360.5) | input gain `L(i)` | 0 → 1.0 |
| 1 | (2981, 19.5) | input high-pass corner in Hz (off when `i ≤ 19`) | 0.1 → **317 Hz** |
| 2 | (216, 5.5) | input low-pass corner `100·i` Hz (open when `i ≥ 221`) | 1 → open |
| 3 | (250, 0.5) | pre-delay `i` ms → `max(1, fs·i/1000)` samples (buffer 250 ms) | 0 → 1 sample |
| 4 | (99, 1.5) | room size `i` (1…100): scales every line | 1 → 100 |
| 5 | (100, 0.5) | diffusion `d = 0.01·i` (all four all-pass gains 0 when `d = 0`) | 1 → 1.0 |
| 6 | (28, 2.5) | low-frequency decay ratio (enters `g₂`, §1.4) | default 10 → ratio 1 |
| 7 | (1975, 25.5) | low-frequency crossover Hz (§1.4) | default 500 |
| 8 | (199, 1.5) | decay time `T = 0.1·i` s | `0.75X'³` |
| 9 | (216, 5.5) | high-frequency damping corner `100·i` Hz (open when `i ≥ 221`) | `Y'²` |
| 10 | (421, −360.5) | dry gain `L(i)` | 0 → `i = −361` → **0** |
| 11 | (421, −360.5) | early-reflection gain `0.5·L(i)` | muted by p16 |
| 12 | (421, −360.5) | wet gain `0.3·L(i)` | `0.225X'³ + 0.5` |
| 13 | (200, 0.5) | stereo width `0.01·i` | default 100 → 1.0 |
| 14/15/16/17 | (1, 0.5) | mute input / dry / early reflections / wet (`i ≥ 1`) | p16 = 1 → **no early reflections** |

Hence, for the Effector: **wet gain `G = 0.3·10^(⌊421·(0.225X'³ + 0.5) − 360.5⌋/200)`** (0.05335 at X' = 0, 0.15744 at
X' = 1; e.g. `i = −150` → −25.5 dB) **[DECOMP + EXEC]**; **damping corner `f_d = 100·⌊216·Y'² + 5.5⌋` Hz** (500 Hz at
Y' = 0, 5 900 Hz at Y' = 0.5, open for `Y'² ≥ 0.9977`) **[DECOMP + EXEC]**; decay `T = 0.1·⌊149.25X'³ + 1.5⌋` s
(round 27).

### 1.3 Line lengths **[DECOMP `FUN_18001dae0`, `FUN_18001c090`; EXEC]**

- `B = ⌊(room + 15)·⌊fs⌋/115⌋` (integer arithmetic; `B = fs` for room 100). Each line length is
  **`L_k = max(1, ⌊B·N_k/20000⌋)`** with

  | k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | `N_k` | 142 | 107 | 379 | 277 | 672 | 4453 | 1800 | 3720 | 908 | 4217 | 2656 | 3163 |
  | role | AP1 | AP2 | AP3 | AP4 | tank A all-pass | tank A delay | tank A all-pass | tank A delay | tank B all-pass | tank B delay | tank B all-pass | tank B delay |
  | 44.1 kHz | 313 | 235 | 835 | 610 | 1481 | 9818 | 3969 | 8202 | 2002 | 9298 | 5856 | 6974 |

- Output tap offsets `o = ⌊B·M/20000⌋` with the `M` values of §1.5. Early-reflection tap line: power-of-two size
  (32 768 at 44.1 kHz), 8 + 8 taps from a table at `0x180055c08` scaled by `(room + 3)·fs/103·(1.5/44100)` — **unused
  by the Effector** (p16 mutes them), not decoded further.
- When the room size changes all lines are cleared **[DECOMP]**.

### 1.4 Coefficients **[DECOMP `FUN_18001dae0` + asm `0x18001e6b8…0x18001e9a0`; EXEC field dump]**

- **Decay** (round 27): `S = −6.907755·(L5 + L7 + L9 + L11)/fs`, **`g = (e^(S/(0.1·D)))^0.25`**, `D` = p8 integer.
- **Low-frequency decay section:** `g₂ = (e^(10·S/(i6·0.1·D)))^0.25`, `r = g₂/g`; mix `m = 0.5·(r − 1)`; crossover
  all-pass coefficient `a_LF = (t − r̂)/(t + r̂)`, `t = tan(π·i7/fs)`, `r̂ = min(r, 1)`. With the defaults (`i6 = 10`)
  `r = 1` → **`m = 0`**: the section has no effect in the Effector **[EXEC: m = 0]**.
- **Diffusion all-pass gains** (`d` = p5 diffusion, 1.0 here):
  `c1 = e^(−49.34111·(L0 + L1)/(fs·d))`, `c2 = e^(−31.398888·(L2 + L3)/(fs·d))`,
  `c3 = e^(−3.4538777·(L4 + L8)/fs·2.8571429/d)`, `c4 = e^(−3.4538777·(L6 + L10)/fs·1.9607843/d)`
  → at 44.1 kHz, room 100: **c1 = 0.541654, c2 = 0.357425, c3 = 0.458687, c4 = 0.221176** **[EXEC]**.
- **One-pole low-pass coefficient** for a corner `f` (p2 and p9): `b = 2 − cos(2πf/fs)`, **`a = 1 − (b − √(b² − 1))`**;
  `a = 1` (no filtering) when the integer is ≥ 221 **[DECOMP; EXEC: 0.548408 for 5 900 Hz]**.
- **Input high-pass:** `c_hp = (t − 1)/(t + 1)`, `t = tan(π·min(fs/2, f_hp)/fs)` (−0.955825 for 317 Hz at 44.1 kHz)
  **[EXEC]**.
- The helpers were identified by execution: `FUN_18003fa50` = cos, `FUN_180041ab0` = tan (model coefficients equal
  the dumped fields), `FUN_18003ff50` = exp, `FUN_18002e2d0` = pow **[EXEC]**.

### 1.5 Per-sample network **[DECOMP `FUN_18001e9f0`; asm confirms the all-pass coefficient pairs]**

Notation: an all-pass "(a, b)" on line `k` reads the stored value `s`, outputs `y = s − a·x` and stores `b·y + x`;
pointers run **downwards** and wrap to `L_k − 1`; a plain delay reads then writes the same position (delay `L_k`).
Two tiny constants (1e−30, 1e−20) are added where noted (denormal guards).

1. **Mono input:** `m = 0.5·(G₀·xL + G₀·xR) + 1e−30` (`G₀` = input gain).
2. **Input low-pass** (p2): `q ← q + (m − q)·a_bw` (open in the Effector).
3. **Input high-pass:** first-order all-pass `h ← c_hp·(q − h) + q₋₁`; output `v = 0.5·(q − h) + 1e−30` (bypassed if
   `c_hp = 0`) → a first-order high-pass at 317 Hz.
4. **Input diffusion:** four all-passes in series: line 0 (c1, c1), line 1 (**c2, c1**), line 2 (c1, c1), line 3
   (**c2, c1**) — on lines 1 and 3 the feed-forward gain is `c2` but the stored feedback uses `c1`.
5. **Pre-delay** (p3) → `u`; tank input `t_in = u + 1e−20`.
6. **Tank, half A:** `f = g·(line-11 output)`; LF section (`y_A = a_LF·(f − y_A) + f₋₁`, `z = m·(y_A + f) + f`);
   **damping** `p_A ← p_A + (z + t_in − p_A)·a_d`; all-pass line 4 (c3, c3); delay line 5; `×g`; all-pass line 6
   (c4, c4); **delay line 7 written then read one position later** (`L7 − 1` samples); `×g`.
7. **Tank, half B:** same structure with its own LF and damping states, the same `t_in` added: damping
   `p_B ← p_B + (z_B + t_in − p_B)·a_d`; all-pass line 8 (c3, c3); delay line 9; `×g`; all-pass line 10 (c4, c4);
   write line 11 (read by half A at the next sample, `L11` samples later). Both halves receive the **same** input
   (no left/right input split) — the stereo image comes only from the taps.
8. **Output taps** (read after this sample's pointer moves; index = pointer + offset, wrapped once). With
   **`α(c) = c + (1 − c)·g`** (tap weights depend on the decay):

   | out | taps (line: M, weight) |
   |---|---|
   | S_L | 9: 266, +1 · 9: 2974, −α(0.25) · 10: 1913, −α(0.7) · 11: 2496, −α(0.2) · 5: 1690, +α(0.6) · 6: 187, +1 · 7: 1566, +α(0.5) |
   | S_R | 5: 3627, +α(0.25) · 5: 353, +1 · 6: 1728, −α(0.7) · 7: 3173, −α(0.2) · 9: 1811, −α(0.6) · 10: 335, +1 · 11: 621, +α(0.5) |

9. **Wet gain smoothing:** `G_w` jumps to `G` on the first sample (its initial value −1000 is below −99), then
   `G_w ← G_w + (G − G_w)·0.001` per sample.
10. **Output (added to the output buffers):** `A_L = G_w·S_L + ER_L`, `A_R = G_w·S_R + ER_R` (ER = 0 here);
    `mid = A_L + A_R`, `side = (A_L − A_R)·width`; **`outL += dry·xL + mid + side`**, **`outR += dry·xR + mid − side`**
    → with width 1 and dry 0: **`outL += 2·G_w·S_L`, `outR += 2·G_w·S_R`**.

There is **no modulation** anywhere in the network (no LFO or random component is read in the process routine)
**[DECOMP]**. The structure (input diffusers, figure-of-eight tank of two all-pass/delay halves with damping, output
taps from inside the tank) is of the same family as the published Dattorro plate (J. Dattorro, *Effect Design*, 1997);
this similarity is an **inference** and plays no role in the model. The process routine also contains a pass-through
path used while the object is bypassed/finishing (`+0x144` counter, `+0x228` flag) and an early return when `+0x141`
is set; neither was taken in the runs.

### 1.6 Verification **[EXEC `erev33.exe` + model `py/efxrev.py`]**

The model implements §1.2–§1.5 in double precision and is compared sample by sample with the DLL's output (both
channels):

| X | Y | fs | block | input | length | peak | max \|DLL − model\| |
|---|---|---|---|---|---|---|---|
| 0 | 0 | 44 100 | 512 | impulse L+R | 4 s | 0.00205 | 1.9·10⁻⁸ (**−100.8 dB**) |
| 0.25 | 0 | 44 100 | 512 | impulse L | 4 s | 0.00104 | 9.6·10⁻⁹ (**−100.7 dB**) |
| 0.5 | 1 | 44 100 | 256 | noise burst | 4 s | 0.0700 | 1.7·10⁻⁸ (**−132.4 dB**) |
| 0.75 | 0.3 | 48 000 | 512 | noise burst | 4 s | 0.0455 | 2.8·10⁻⁸ (**−124.2 dB**) |
| 1 | 1 | 96 000 | 480 | impulse L | 4 s | 0.0402 | 8.0·10⁻⁹ (**−134.0 dB**) |
| 1 | 0 | 44 100 | 512 | noise burst | 4 s | 0.0301 | 2.5·10⁻⁷ (**−101.5 dB**) |
| 0.6 | 0.8 | 22 050 | 100 | impulse L+R | 4 s | 0.0266 | 4.0·10⁻⁹ (**−136.4 dB**) |
| 1 | 0.5 | 44 100 | 512 | impulse L+R | 3 s | 0.0442 | 9.9·10⁻⁹ (**−133.0 dB**) |

All cases are far below the −60 dB target; the residue is float32 rounding in the DLL **[model]**. This also explains
round 27's observation that Y changes the measured decay: Y sets the in-loop damping low-pass (500 Hz…open), which
shortens the broadband decay at low Y.

---

## 2. Toxic Biohazard — filter envelope

### 2.1 Time law of A/D/R **[DECOMP `FUN_18003d480`, `FUN_180040080`, `FUN_18003e510`; EXEC]**

- **`T(x) = max(50·x⁴, 0.00101)` seconds** (`0x18008c0e0` = 50.0, `0x18008be10` = 0.00101) — used for the filter EG
  attack, decay and release (params 19, 20, 22) and the amplitude EG attack and decay (params 3, 4 of its setter).
  Amplitude release: **`0.35·T(x) + 0.02` s** (`0x18008bf38`, `0x18008bea8`). Sustains: **`x²`** (params 21 and 5).
  E.g. x = 0.3 → 0.405 s, 0.4 → 1.28 s, 0.5 → 3.125 s, 1 → 50 s **[EXEC: latched voice fields equal these values]**.
- **Latching:** A, D and S are copied into the voice at note start (`FUN_18003a3c0`), R at note-off (`FUN_18003a660`);
  copies are float, and filter/amp A, D, R below 0.001 become 0.001 (amp release floor 0.01) **[DECOMP]**. Changing the
  knobs during a note therefore does not affect that note's envelope.

### 2.2 Envelope shape **[DECOMP `FUN_1800395b0`, `FUN_180039880`, `FUN_180038d00`]**

The envelope `e` (voice `+0x70`, 0…1) follows a target with a one-pole step **every sample**, while the target and
coefficient are chosen **once per 128-sample block** (the render calls the voice update when its block counter reaches
128; the first block starts at the first rendered sample):

- elapsed time `t_el` (double): 0 at note start, **incremented by 128/44100 after each block** — constant
  `0x18008bda0` = 1/44100, **independent of the sample rate**;
- release counter: −1 while held, set to 0 by note-off; the release stage starts at the **first block boundary after
  the note-off**.

| Stage (checked at the block start) | Target | Coefficient `k` per sample |
|---|---|---|
| released (counter > −1) | 0 | **`8.82/(R·fs)`** |
| `t_el ≥ A` | **S + 1e−7** | **`8.82/(D·fs)`** |
| `t_el < A` (attack) | **5.0** | **`8.82/(39.441975·A·fs)`** |
| voice being re-triggered (`+0x245` > 0) | 0 | `147/fs` |

Per sample: **`e ← e + (target − e)·k`, then `e ← min(e, 1)`** (float32). Consequences **[DECOMP; numbers model]**:
the attack is the start of an exponential towards 5 that is clipped at 1 — from 0 it reaches 1 after
`ln(1.25)/k ≈ 0.9979·A·fs` samples, i.e. ≈ A seconds at 44.1 kHz, then stays at 1 until the block where `t_el ≥ A`;
the decay is exponential towards S with time constant `D/8.82` (the distance falls by 76.6 dB in D seconds); the release
is exponential towards 0 with time constant `R/8.82` from the level reached at that block.
**Re-trigger:** if a voice that has already run is started again, `+0x245` = ⌊4·fs/44100⌋ blocks (`0x18008bcf8` =
4/44100) of fast fade to 0 (`147/fs`) are inserted, during which `t_el` does not advance **[DECOMP]**.

**Sample-rate dependence [EXEC]:** the voice rate `+0x1c` is the host rate (44 100, 48 000 and 96 000 in the runs).
Because the coefficients use `fs` but `t_el` counts 1/44100 units, at 96 kHz the attack *stage* ends after
`A·44100/96000` s while the curve would need ≈ A s to reach 1: with A = 0.08 s the filter envelope peaks at **0.495**
instead of 1 (`tx_e4`).

### 2.3 How it moves the ladder **[DECOMP `FUN_180038d00`, `FUN_1800395b0`; EXEC]** (completes round 26 §2.2)

- Per block: `E_A = EGAmt·65535` (voice `+0x78`), `EGAmt = sign(2x₂₃ − 1)·(2x₂₃ − 1)²` (round 19); cutoff ramp
  `U` (double, `+0x230`) moves linearly from its value to the block target `u·65535` over the block.
- Per sample, **before** updating `e` and `U`: **`i = trunc(e·E_A + U)`**, clamped to 0…65535; the ladder uses table
  entry `i` (round 26 §2.1: `fc = 2^(9i/65535 + 5)` Hz at 44.1 kHz). So the envelope adds **`9·EGAmt·e` octaves**
  to the cutoff, sample by sample, quantised to 1/65535 of 9 octaves.
- EXEC check: x₂₃ = 0.9 → `E_A` = 41 942.4 = 0.64·65535; cutoff 0.5 → `U` = 32 767.4999.

### 2.4 Verification **[EXEC `tox33b.exe` + model `py/toxeg.py`]**

The model (§2.1–§2.2, float32 steps) against the envelope logged after every sample by the DLL, LP mode, note 60,
velocity 100, amplitude release long enough (amp param 6 = 0.7 → 4.22 s) to keep the voice alive:

| fs | A, D, S, R (knob x) | note-off at | samples | max \|DLL − model\| |
|---|---|---|---|---|
| 44 100 | 0.3, 0.4, 0.5, 0.35 | 30 000 | 60 000 | **6.0·10⁻⁸** |
| 44 100 | 0.12, 0.25, 0.3, 0.25 | 9 000 | 30 000 | **1.8·10⁻⁷** |
| 48 000 | 0, 0.5, 1, 0.4 | 20 000 | 40 000 | **6.0·10⁻⁸** |
| 96 000 | 0.2, 0.15, 0.6, 0.3 | 30 001 | 60 000 | **6.0·10⁻⁸** (peak 0.495) |

i.e. the same values to within one or two float32 ulps, including the block-wise stage switching, the release start at
the next block boundary and the 1/44100 elapsed-time law.

---

## 3. Kepler — chorus noise/gate defaults and BPM sync

### 3.1 Where the state is parsed **[DECOMP front-end + engine; DATA]**

- The front-end `Kepler_x64.dll` contains neither the state tags `Kepler-1`/`Kepler-2` nor any parameter name
  ("Chorus Noise", "Noise Gate" exist only in the engine); it loads `engine_x64.dll` (UTF-16 literal at `0x6d5974`),
  obtains `CreatePlugInstance` (literal at `0x6d11e8`), keeps the engine plugin at `+0x120` and forwards calls to it
  (e.g. `0x6d4290` forwards ProcessParam to the engine's slot 6; `0x6d5790…0x6d57f0` forward slots 8–11)
  **[DECOMP]**. The engine's state loader `FUN_18001f170` parses `Kepler-1`/`Kepler-2` **[DECOMP]**.
  That the front-end passes the saved state bytes to the engine unchanged is an **inference**; it could not be executed
  with the stub host (§Tools).

### 3.2 Chorus Noise (0x46) and Noise Gate (0x47) **[DECOMP `FUN_18001ed30`, `FUN_18001f170`, `FUN_180019a20`; EXEC `kep33.exe`; DATA]**

- **Every** state load (`Kepler-1` and `Kepler-2`), and the synth constructor, first call the defaults initialiser,
  which (after setting all 258 parameters from the default table) explicitly sets **gate flag = false** and
  **parameter 0x46 = 0** → noise level `0.2·0³` = **0** (the chorus constructor's 0.01 is overwritten) **[DECOMP]**.
- `Kepler-1` layout (`ver`, `count`, `count` panel floats mapped by `FUN_180016300`; if `ver > 1`: 1 byte + 2 floats
  for internal 0x49 and 0x0B; **if `ver > 2`: gate byte, noise byte, oversampling byte**): the gate byte sets the gate
  flag directly (`> 0` → on); the **noise byte `> 0` sets parameter 0x46 to 0.09** (`0x1800ed620`) → level
  `0.2·0.09³` = **1.458·10⁻⁴**, otherwise 0 **[DECOMP]**. `Kepler-2` carries 0x46/0x47 among its 155 floats.
- **Data:** all 281 Kepler `.fst` presets are `Kepler-1` **version 2** (no gate/noise bytes) → noise 0, gate off; the
  4 demo channels are **version 4** with gate byte 0 and noise byte 0 **[DATA]**.
- **EXEC:** after creation and after loading `Default.fst`, a sync preset and all 4 demo states through the engine's
  own loader: parameter 0x46 = 0, 0x47 = 0, chorus noise level = 0, gate flag = 0, chorus sync byte = 0 (and chorus
  mode = 2/1/1/2 for the demos, as in round 26).
- **Result:** for every state in the data the chorus noise is **off** and the gate **off**; LYNX's chorus without
  noise/gate is exact for them. Whether the front-end (or FL's host) sets 0x46/0x47 after a load: **not confirmed**
  (no evidence that it does; not executable here).

### 3.3 Chorus LFO BPM sync **[DECOMP `FUN_180001770` (`0x1800019d2`), `FUN_18001b940` (`0x18001c67f`, `0x18001c752`); EXEC]**

The chorus object's tempo-sync byte (`+1`, global `+0x1BBB1`) is written only by the chorus constructor (0) and by the
two mode-change paths (0); no other instruction writes it **[DECOMP: instruction search over `.text`]**, and it reads
0 after every load **[EXEC]**. **The chorus LFO is therefore never tempo-synced in Kepler**; its sync branch is dead
code for this plugin and was not decoded.

### 3.4 LFO1 BPM sync (panel k6 → internal 0x31) **[DECOMP `FUN_180019a20` case 0x30/0x31 (`0x18001a46d…0x18001a501`), `FUN_18001b940` (`0x18001bbf8…0x18001bcb8`), `FUN_180019210`; EXEC]**

- Sync flag = `x₆ ≥ 0.5`. The rate knob `x` (k5 → 0x30) gives, besides the free rate of round 17,
  **`n = trunc((2^(7(1 − x)) − 1)·48.370079 + 1.1)`** and the factor **`κ = 192/n`** (LFO cycles per beat)
  (`0x1800ed9bc` = 7, `0x1800eda28` = 48.370079, `0x1800ed730` = 1.1, `0x1800ed948` = 192) → **period = n/192 beats**:
  x = 1 → 1/192 beat; x = 0.5 → n = 499 (2.60 beats); x = 0 → n = 6144 (32 beats) **[DECOMP + EXEC: κ for 6 states
  equals the formula, `py/kepsync.py`]**.
- While synced, per sample the global render takes the beat-position difference since the previous sample `Δ`,
  forms `d = Δ·κ` and, **only if 0 ≤ d < 1**, uses it as the phase increment (otherwise the previous increment is
  kept, e.g. across a transport jump) **[DECOMP]**. The beat position is the host song position advanced per sample
  by the tempo; **[EXEC]** with the stub host the increment was `3.17143·10⁻⁵ = 125/(60·44100)·κ`, i.e. one beat per
  `60·fs/BPM` samples at the wrapper's default 125 BPM (constructor writes 125.0 at plugin `+0x280`); the exact host
  field that feeds the tempo is an **inference**.
- **Old states (`Kepler-1` version < 4) with sync on:** the loader rewrites the stored rate as
  `x' = 1 − 0.2060993·ln(1 + 0.0206739·(m − 1))`, `m = trunc(9216/n_old + 0.1)`, `n_old = trunc((2^(7x) − 1)·48.370079 + 1.1)`,
  so that `n(x') = m` and the period `n/192 = 48/n_old` beats is kept **[DECOMP + EXEC: stored 0.837584913 for
  "Biobass", model 0.837584916]**. The loader writes this value **without** re-running the setter, so right after the
  load the engine's κ still comes from the **old** x (EXEC: κ = 192/1439 for "Biobass", i.e. 7.49 beats instead of
  the converted 59/192 = 0.31 beat). Which of the two FL plays (i.e. whether the host/front-end re-sends the parameter
  after loading) is **not confirmed**. 72 of the 281 presets have sync on **[DATA]**; demo "Pads - Kepler" (version 4)
  has sync on with x = 0.602 → n = 286 (1.49 beats) **[DATA + EXEC]**.

---

## 4. Not decoded / open in this round

- Effector Reverb: early-reflection network details (muted in the Effector), the low-frequency decay section with
  non-default p6/p7 (inactive in the Effector), the bypass/tail path of the process routine.
- Toxic: the amplitude envelope was read (§2.1 table entries, `FUN_1800393b0`) but only the filter envelope was
  verified by execution; the voice-end condition (release counter > amp release) was read, not executed.
- Kepler: whether the front-end or FL's host sets 0x46/0x47 (or re-sends the LFO rate) after a state load; the host
  field behind the beat clock; the free-running LFO1 rate table is round 17's (not re-read).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| 1a | Effector Reverb network | 1-pole LP (open) → 317 Hz first-order HP → 4 input all-passes (c1/c2 gains, lengths B·{142,107,379,277}/20000) → pre-delay (1 sample) → figure-of-eight tank (per half: damping 1-pole, all-pass c3, delay, ×g, all-pass c4, delay, ×g) → 7 + 7 taps with weights `c + (1 − c)g` → wet gain, `out += x + 2·G_w·S` **[DECOMP + EXEC]** |
| 1b | Roles of X and Y | X: decay `T = 0.1⌊149.25X'³ + 1.5⌋` s **and** wet gain `0.3·10^(⌊421(0.225X'³ + 0.5) − 360.5⌋/200)`; Y: in-loop damping corner `100⌊216Y'² + 5.5⌋` Hz **[DECOMP + EXEC]** |
| 1c | Modulation / early reflections | none / muted (p16 = 1) **[DECOMP + EXEC]** |
| 1d | Verification | model vs DLL ≤ 2.5·10⁻⁷, **−100.7…−136.4 dB** re peak over 8 cases (22.05–96 kHz) **[EXEC + model]** |
| 2a | Toxic EG time law | `T = max(50x⁴, 0.00101)` s; amp release `0.35T + 0.02`; S = x²; latched at note start/off **[DECOMP + EXEC]** |
| 2b | Toxic EG shape | per-sample one-pole; per-128-block stage: attack → 5.0 with `8.82/(39.441975·A·fs)` clipped at 1, decay → S with `8.82/(D·fs)`, release → 0 with `8.82/(R·fs)` from the next block; elapsed time in 1/44100 units **[DECOMP + EXEC ≤ 1.8·10⁻⁷]** |
| 2c | Toxic EG → ladder | `i = trunc(e·EGAmt·65535 + U)` per sample, before the updates → `+9·EGAmt·e` octaves **[DECOMP + EXEC]** |
| 3a | Kepler noise/gate | reset to noise 0 / gate off on every load; only `Kepler-1` v ≥ 3 bytes can turn them on (noise → 0.09 → level 1.458·10⁻⁴); all data: off **[DECOMP + EXEC + DATA]**; front-end role not confirmed |
| 3b | Chorus LFO sync | never enabled (sync byte only ever written 0) **[DECOMP + EXEC]** |
| 3c | LFO1 BPM sync | period `n/192` beats, `n = trunc((2^(7(1−x)) − 1)·48.370079 + 1.1)`; increment = beat delta·192/n when in [0, 1); old-state conversion keeps 48/n_old beats **[DECOMP + EXEC]** |

---

## 5. For LYNX

1. **Effector Reverb** (`src/core/Effector.cpp`, `reverbUnit`): replace the LYNX reverb call with the exact network of
   §1.5, per unit instance (state: 12 lines + pre-delay, two damping and two LF states, input LP/HP states, smoothed
   wet). Per block from `X'`, `Y'`: `D = ⌊199·(0.75X'³) + 1.5⌋` (float), `g` from §1.4; `i9 = ⌊216·Y'² + 5.5⌋`,
   `a_d = 1` if `i9 ≥ 221` else the one-pole formula at `100·i9` Hz; `G = 0.3·10^(⌊421·(0.225X'³ + 0.5) − 360.5⌋/200)`;
   lengths/offsets from `B = ⌊115·⌊fs⌋/115⌋` (room 100); c1…c4 with `d = 1`; HP 317 Hz; pre-delay 1 sample; no
   early reflections, no LF section (`m = 0`), width 1, dry 0. Keep the unit's existing `x + R(w·x)` mixing and add
   `2·G_w·S_L`, `2·G_w·S_R`. Reference code: **`tmp/r33/py/efxrev.py`** (`params(X, Y, fs)` returns every coefficient,
   `run()` the per-sample loop); it matches the DLL to −100 dB or better. The "≈ 0.6…10.5 s" limitation and the LYNX
   diffusion/Y approximations in `docs/STATUS.md` can then be removed.
2. **Toxic filter envelope** (`src/core/FlGenerators.cpp`, `synthFromToxic` + the Toxic voice): replace the linear
   filter envelope (`s.filterEnv` with `seconds(p[19])`…) by the law of §2.1–§2.2: latch `A = max(50x₁₉⁴, 0.00101)`
   (≥ 0.001), `D` likewise from x₂₀, `S = x₂₁²`, and `R` from x₂₂ at note-off; per 128-sample block choose
   (released → 0, `8.82/(R·fs)`; `t ≥ A` → `S + 1e−7`, `8.82/(D·fs)`; else → 5, `8.82/(39.441975·A·fs)`), with
   `t += 128/44100` per block and the release starting at the next block boundary; per sample `e += (target − e)·k`,
   `e = min(e, 1)`, and the ladder index `trunc(e·EGAmt·65535 + U)` computed before the update. Reference code:
   **`tmp/r33/py/toxeg.py`** (`tlaw`, `run`). The amplitude envelope uses the same machinery (attack/decay `T(x)`,
   release `0.35·T(x) + 0.02`, sustain `x²`; DECOMP only).
3. **Kepler** (`synthFromKepler`): the chorus noise and gate stay **off** for every `Kepler-1` v2 state and for v ≥ 3
   states whose noise/gate bytes are 0 (all data); if a v ≥ 3 state has a non-zero noise byte, the noise level is
   `0.2·0.09³` (filtered noise of round 26 §3.3 step 6), and a non-zero gate byte enables the gate of step 9. The
   chorus LFO never syncs. **LFO1 BPM sync** (k6 ≥ 0.5): phase increment per sample `= (BPM/(60·fs))·192/n` with `n`
   from §3.4 (`tmp/r33/py/kepsync.py`: `n_of`, `phase_inc`); for version-2 presets with sync, use the stored x as the
   engine does right after loading (or the converted x', see the open question in §3.4).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Effector `FUN_18001a130` (`tmp/r27/out/e1.c`) | unit: `e·w` input copy, ten parameter writes, process adds into the unit's buffers | DECOMP |
| Effector `FUN_18001cc30` | parameter → integer laws (k, c) for p0…p17 | DECOMP |
| Effector `FUN_18001dab0` + `0x180058318` | level helper `10^(i/200)`, 0 below −360 | DECOMP |
| Effector `FUN_18001dae0` + `dae0.asm` | B, line lengths, tap offsets, c1…c4, decay g, LF section, one-pole and HP coefficients, gains, mutes | DECOMP |
| Effector `FUN_18001e9f0` + `e9f0.asm` | per-sample network, all-pass gain pairs, taps and weights, wet smoothing, mid/side output | DECOMP |
| `erev33.exe` + `d_*.txt`, `g_*.bin`, `efxrev.py` | object fields after set-up; DLL output vs model (8 cases) | EXEC + model |
| Toxic `FUN_18003d480`, `FUN_180040080`, `FUN_18003e510` | `max(50x⁴, 0.00101)`; amp release `0.35T + 0.02`; sustain `x²` | DECOMP |
| Toxic `FUN_18003a3c0`, `FUN_18003a660`, `FUN_18003a730` | latching, 0.001 floors, release counter, re-trigger fade length | DECOMP |
| Toxic `FUN_1800395b0`, `FUN_1800393b0`, `FUN_180039880`, `FUN_180038d00` | stage choice per block, coefficients, elapsed time 1/44100, per-sample step and clip, index formula | DECOMP |
| `tox33b.exe` + `tx_e1…e4.txt`, `toxeg.py` | per-sample filter envelope vs model at 44.1/48/96 kHz; latched voice fields | EXEC + model |
| Kepler front-end `0x6d58d0`, `0x6d10e0`, `0x6d4290`, literals `0x6d5974`, `0x6d11e8` | engine loading and forwarding; no state tags or parameter names in the front-end | DECOMP |
| Kepler `FUN_18001ed30`, `FUN_18001d900`, `FUN_18001f170`, `FUN_180021ca0`, `FUN_1800199b0` | defaults on every load, `Kepler-1`/`-2` layouts, noise 0.09 / gate byte | DECOMP |
| Kepler `FUN_180019a20` cases 0x30/0x31/0x46/0x47, `FUN_18001b940`, `FUN_180019210` | sync factor 192/n, per-sample beat-delta increment, noise level 0.2x³ | DECOMP |
| Kepler `FUN_180001770`, `FUN_18001b940` writes to `+0x1BBB1` | chorus sync byte only ever 0 | DECOMP |
| `kep33.exe` + `kep_a.txt`, `kepsync.py` | noise/gate fields after creation and loads; κ for 6 states; converted rate; pow/ln helpers | EXEC + model |
| `blobs14.pkl` Kepler (4 demos, 281 presets) | versions, gate/noise bytes, sync usage | DATA |
