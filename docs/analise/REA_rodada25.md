# Behavioural analysis of FL Studio 2026: round 25 (REA) — Harmor filter resonance

> **Nature of this document.** Continuation of `REA_rodada19.md` §2.2 (Harmor resonance), `REA_rodada23.md` §1
> (shaping curves, filter shapes, curve evaluation) and `REA_rodada18.md` §1.2 (cutoff). *Clean-room* reverse
> engineering, behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is here are
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No encrypted content was touched (no FLEX pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from
> running a function of the installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence,
> the item is marked **"not confirmed"**. Inferences made *from* the code are stated explicitly as **inference**.

- **Target:**

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Harmor | `Harmor_x64.dll` `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` (= rounds 11/18/19/23/24, re-hashed against the installation in this round) | Delphi (`TTestPlug`) | per-voice filter/resonance `FUN_00886eb0`; resonance width `FUN_008744c0`; shape/table builder `FUN_00874ed0` (+ frame helpers `FUN_00874d60`, `FUN_00874d30`, `FUN_00874e10`, end-trim `FUN_00874e40`/`FUN_0062d310`, zero cut `FUN_00629eb0`); table sampling `FUN_00874930` (+ `FUN_00874860`, interpolating pointer `0x94e128`); table dispatcher `FUN_008740e0`; rebuild-all `FUN_00878e40` (called from the part constructor `FUN_00870750` and from `FUN_0086f2f0`); exponent scaling `FUN_00882af0`, masked variant `FUN_008827d0`; amplitude multiply `FUN_00882820`; quality / routine pointers `FUN_00878f60`; `ProcessParam` `FUN_00879540` (cases 58–61, 70–73); state load `FUN_0086ed30`; voice pitch `FUN_0088ab10` (`0x88c5dd…0x88c683`); constants `FUN_00630170`, `FUN_0089a4d0`; IPP dispatcher `FUN_005bd1c0`; envelope step `FUN_007a1930` / coefficients `FUN_0079e680` (mode 9) |

- **Tools:** Ghidra 12.1.4 *headless* on the read-only projects `/tmp/r23gh` and `/tmp/r25gh` (program `hm` =
  Harmor). Scripts in `tmp/r25/scripts/` (those of round 23/24 + `InsGrep.java`, instruction-text search over an
  address range); *dumps* in `tmp/r25/out/`; constants read from the PE with `tmp/r18/k.py`. Our own numerical
  models in pure Python: `tmp/r25/py/restab.py` (tables), `resmodel.py` (resonance gain incl. mask),
  `demos.py` (per-demo table, output `tmp/r25/out/demos_table.md`), `hzscale.py` (Hz-scale rows,
  `tmp/r25/out/hz_table.txt`), over `tmp/r14/blobs14.pkl` via `tmp/r23/py/hm.py`. Execution harness
  `tmp/r25/run/tab2.c` → `tab2.exe` (our own C, built with Zig for Windows): it only loads the installation's
  `Harmor_x64.dll` and calls `FUN_00874ed0` (table builder) on a zeroed table record and `FUN_008744c0` on a
  zeroed fake part; it never returns through the DLL's finalisation (the first harness of this round, `tab.exe`, did
  and raised a Delphi "Runtime error 216" dialog at exit; `tab2.exe` installs a fault handler, flushes and ends with
  `TerminateProcess`). Tables: `tmp/r25/run/shape_<code>.txt`. Manual read via the web.
- **Date:** 2026-10-13.
- **Dataset:** 99 Harmor channels / 198 parts of `Demo projects/**` (same slice as round 23).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files (incl. the DFM resources) |
| **[EXEC]** | result obtained by calling a function of the installation's own DLL (without modifying it) |
| **not confirmed** | not enough evidence in this round |

Manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Harmor.htm`
(abbreviated **[DOC Harmor.htm]**; quotes come from a summarised web reading and must be rechecked before becoming
interface text).

**Conventions.** As in round 23: `FUN_0040ad30` = exp, `FUN_0040c090` = ln, `FUN_0040ba60` = cosine,
`FUN_0040bb50` = sine, `FUN_0040c520` = round. Runtime constants (`FUN_00630170`): `0x93b4f0` = ln 2,
`0x93b4f8` = 1/ln 2, **`0x93b510` = ln 2 / 12**, **`0x93b508` = ln 2 / 1200**, `0x93b540` = log₂ 440
**[DECOMP: ln(2.0)/12.0, then /100; ln(e⁰·440)·(1/ln 2)]**. `FUN_0089a4d0`: `0x94e110` = **X₀ = log₂(440·2^(−57/12))
= log₂ 16.352 Hz** (origin of Harmor's absolute frequency axis), `0x94e0fc`/`0x94e100` = log₂ of 73.416 Hz /
17 739.7 Hz (filter range, round 18), `0x94e108` = −log₂ 130.81 (keytracking reference) **[DECOMP]**.
"Entries" = positions in a resonance table; `L` = voice cutoff in log₂ Hz (round 18/19); `x` = log₂ of a partial's
frequency in Hz.

---

## 1. Where the resonance lives

### 1.1 Parameters and type codes **[DATA (DFM) + DECOMP `FUN_0086ed30`, `FUN_00874ed0`; DOC]**

- DFM (round 11 dump `tmp/r11/harmor.dfm.txt`): p57/p69 "Filter 1/2 resonance / noise amount" (0…1000);
  p58/p70 "resonance / noise scale" (switch `oct` / `Hz`); p59/p71 "resonance width / noise length" (0…1000,
  default 500); p60/p72 "resonance offset" (−2400…2400, wheel value 600, `DefaultValue` 0); p61/p73 "resonance
  adaptive width / free noise"; **p62 "Filter 1 resonance self-oscillation"** (0…2048; filter 1 only).
  Articulators 8/9 "res amount", 10/11 "res width", 12/13 "res offset" (round 19).
- **Resonance type** combo (`F1ResTypeSelect`/`F2ResTypeSelect`): Classic, Cuberdon, Pedestal, Sedge hat, Wide bump,
  Double cone, Well, Wormhole, Lowpass, —Special—, Noise, —, Custom shape 1, Custom shape 2.
- The type is stored in the part block's combo area: **misc+0x3C → part `+0x430`** (filter 1) and **misc+0x40 →
  `+0x4F0`** (filter 2); misc+0x34/+0x38 (filter types) go to `+0x3F8`/`+0x4B8` **[DECOMP `FUN_0086ed30`]**.
  The field is the *code* of the table record `+0x428` (filter 1; `+0x4E8` filter 2), the same record kind as the
  filter-shape record `+0x3F0`.
- Code ↔ name: the builder handles codes **100…108** as nine fixed shapes, **109 (0x6D)** only sets a flag (the
  "noise" path of the render tests this code), and **110/111** read the shaping envelopes **0x37/0x38 = "Resonance
  shape 1/2"** **[DECOMP `FUN_00874ed0`]**. That matches the combo in order (non-separator item k → code 100 + k):
  Classic 100 … Lowpass 108, Noise 109, Custom shape 1/2 110/111 (**inference** from the order plus the two
  anchored codes 109 and 110/111). The same pattern holds for the filter-shape combo: filter types 17/18 are
  "Custom shape 1/2" and read envelopes 0x35/0x36 **[DECOMP]**.
- Manual: Classic "Single narrow resonance peak"; Cuberdon…Double cone "Various resonance peak shapes"; Well &
  Wormhole "Cut frequencies at the cutoff frequency with resonant peaks either side of the hole"; Low pass "Provides a
  resonance peak followed by a dip"; Noise "Harmonics are sounded randomly"; offset "shifts the resonant frequency
  +/- 2400 cents, relative to the cutoff frequency"; adaptive width makes "the resonance sounds similar regardless of the
  note pitch"; self-oscillation "A sine wave is deliberately introduced at the resonant frequency" **[DOC Harmor.htm]**.

### 1.2 `ProcessParam` **[DECOMP `FUN_00879540`]**

| Param | Field (filter 1; +0xC0 for filter 2) | Value |
|---|---|---|
| p57/p69 | — (no case) | read directly in the render (§2) |
| p58, p61 (and p70, p73) | calls `FUN_008744c0` (§1.3) | — |
| p59/p71 | `+0x498` | **0.004·p59** |
| p60/p72 | `+0x490`, `+0x48C` | **p60/1200** (octaves) and **2^(p60/1200)** |
| p62 | float pointed to by part `+0x68` | **(p62/1024)²** — consumer **not traced** |

### 1.3 Width constant k₀ (`+0x49C`) — `FUN_008744c0` **[DECOMP + EXEC]**

`FUN_008744c0(part, filter)` works on the 0xC0-byte filter block that starts at `+0x3E8` (filter 1). It reads
**S = the table's "entries per unit" field `+0x448`** (= table record `+0x428` + 0x20) and writes **k₀ to `+0x49C`**
and a constant to `+0x4A0`:

| p58 (scale) | p61 (adaptive) | k₀ (`+0x49C`) | `+0x4A0` |
|---|---|---|---|
| 0 (oct) | 0 | **S** | −0.125 |
| 0 (oct) | ≥ 1 | **S · 440·2^(−27/12) = S · 92.4986** | 0.875 |
| 1 (Hz) | 0 | **S · 0.000375** | −1.5 |
| 1 (Hz) | ≥ 1 | **S · 0.000375 · 440·2^(−15/12) = S · 0.069374** | 0.5 |

Constants `0x8745f0` = −27, `0x874600` = −15, `0x8745f8` = 440, `0x874608` = 0.000375, used as
`exp(−27·ln2/12)` etc. **[DECOMP]**. Executing `FUN_008744c0` with S = 1024 gave exactly 1024 / 94 718.57 /
0.384 / 71.0389 and −0.125 / 0.875 / −1.5 / 0.5 **[EXEC]**.

- **S** is set by the table builder: **S = 1024 for every fixed shape (codes 100…108)** **[DECOMP `FUN_00874d60`
  writes 1024.0 to both `+0x20` and `+0x24`; EXEC header of every table]**; for **custom shapes (110/111) S = 0.25·N
  = 256** (N = 1024; `FUN_00874e10` with `N·[0x92b168]`, `0x92b168` = 0.25) **[DECOMP]**.
- **`+0x4A0`**: written only here; no reader found (decompiled search `0x860000…0x8A0000`, instruction search for
  `+ 0x4a0]` over the whole code) — role **not confirmed** (accesses through the block base `+0x3E8` as `+0xB8` were
  not searchable).

### 1.4 Voice field `+0x1B8` (used with p61) **[DECOMP `FUN_0088ab10` @ `0x88c5dd…0x88c683`]**

- `voice+0x1BC = log₂ 440 + ((voice+0x1AC)·0.01 + 60 − 57)/12`, then **`voice+0x1B8 = 2^(voice+0x1BC)`**
  (`0x92c438` = 0.01, `0x88f26c` = 60, `0x88f270` = 57, `0x88f278` = 1/12, `0x93b540` = log₂ 440).
- So `+0x1BC` is the voice pitch in log₂ Hz and **`+0x1B8` is the voice frequency in Hz**; `+0x1AC` is a pitch in
  cents (that it is "note·100 relative to note 60, incl. pitch modulation" is **inference** from the 0.01 and the 60).
  The cutoff keytracking uses the same `+0x1BC` (`(+0x1BC − log₂ 130.81)·p55/1000`, render `FUN_00886eb0`).
- Hence with **adaptive width on**, the width constant is **k₀/f₀ = S·92.4986/f₀**: the table step per octave shrinks
  as the note rises, i.e. the bump gets **wider in octaves proportionally to f₀/92.5 Hz** (equal to the non-adaptive
  width at f₀ = 92.4986 Hz = 440·2^(−27/12)) **[DECOMP]**; in Hz scale the reference is 440·2^(−15/12) = 184.997 Hz.
  In the demos **p61 = p73 = 0 in all 198 parts** **[DATA]**.

---

## 2. The render path (`FUN_00886eb0`, normal types) **[DECOMP]**

1. **Amount:** `r = p57·0.001·A₈` (A₈ = output of articulator 8/9, base 1). The block runs only if r > 0, the
   resonance record's flag bit (`+0x45C`) is set and the filter is active (round 19).
2. **Width:** `w = k₀′·2^(1 − 0.004·p59·A₁₀)` with `k₀′ = k₀` (p61 = 0) or `k₀/f₀` (p61 ≥ 1, §1.4).
3. **Sampling position** of each partial (table record `+0x428`, origin `o` = record `+0x14`):
   - **oct scale (p58 = 0):** `pos = o + (x − L − p60/1200 − 8·A₁₂)·w` → w is in **entries per octave**.
   - **Hz scale (p58 = 1):** `pos = o + (f − 2^L·2^(p60/1200)·2^(8·A₁₂))·w` with `f` the partial frequency in Hz
     (voice array `+0x190` instead of the log₂ array `+0x198`) → w is in **entries per Hz**.
   - `FUN_00874930`: the value is `T[pos]` with **linear interpolation** (quality > 0, pointer `0x94e128`), **`T[0]`
     below the table and `T[N−1]` beyond it**; in quality 0 (`FUN_00874860`) the position is clamped to [0, N−1] and
     truncated to an integer (no interpolation) **[DECOMP]**.
4. **Mask:** if the "Filter 1/2 resonance mask" record (`part − 0xA88 + (f + 0x3F)·0x38`, f = 0/1) has its flag set,
   its own table `M` is sampled at the partial's absolute log₂ frequency and the exponent becomes
   `M·T·r·ln2` (`FUN_008827d0`); otherwise `T·r·ln2` (`FUN_00882af0`). See §3.3.
5. **Gain:** the vector goes through the routine pointer `+0x1150` and the result multiplies the partial amplitudes
   (`FUN_00882820`). `FUN_00878f60` loads `+0x1150` from `0x92c4a0` → `0x93a1b0` (quality ≤ 0) or `0x92b768` →
   `0x93a1c0` (quality > 0); the IPP dispatcher `FUN_005bd1c0` pairs **`0x93a1b0` with the name
   `ippsExp_32f_A11`** and **`0x93a1c0` with `ippsExp_32f_A24`** (UTF-16 strings at `0x5c19c8` / `0x5c1a20`).
   **Confirmed: `+0x1150` = natural exp** (11- or 24-bit accuracy by quality) **[DECOMP + DATA (PE strings)]**.

**Hence, per partial:**

> **gain = 2^(r · M(f) · T(pos))**, i.e. **6.0206 · r · M · T dB**, multiplied on top of the filter-shape gain
> `F` (round 23 §1.8; same cutoff L, independent of the resonance).

There is no normalisation of the resonance anywhere in this path: a table value T = 6 at r = 1 is +36.1 dB on that
partial **[DECOMP]**. The resonance does **not** change the filter-shape table.

---

## 3. The tables

### 3.1 Fixed shapes 100…108 **[EXEC `FUN_00874ed0`; laws of 100 and 108 re-derived from DECOMP and checked]**

Each table is built over `u = i/(N−1)`, then (generic tail of `FUN_00874ed0`) values below a tiny threshold are set to
0, constant runs at both ends are trimmed (the origin `o` is shifted by the number of removed leading entries, so
the sampled curve is unchanged) and `T[N] = T[0]` is appended. Executed tables (header = record after the build):

| Code | Name | N | o | S | min | max (entry − o) | T[0] (below) | T[N−1] (above) |
|---|---|---|---|---|---|---|---|---|
| 100 | Classic | 1024 | 511.5 | 1024 | −1.000 | **6.000** (−0.5) | **−1** | **−1** |
| 101 | Cuberdon | 1536 | 767.5 | 1024 | −2.269 | 6.250 (+0.5) | 0 | 0 |
| 102 | Pedestal | 512 | 255.5 | 1024 | 0 | 5.800 (−85.5) | 0 | 0 |
| 103 | Sedge hat | 512 | 255.5 | 1024 | 0 | 6.900 (+0.5) | 0 | 0.027 |
| 104 | Wide bump | 1020 | 509.5 | 1024 | 0 | 5.400 (−170.5) | 0 | 0 |
| 105 | Double cone | 1532 | 765.5 | 1024 | 0 | 6.200 (−383.5) | 0 | 0 |
| 106 | Well | 682 | 340.5 | 1024 | −8.000 | 0 | 0 | 0 |
| 107 | Wormhole | 1024 | 511.5 | 1024 | −10.000 | 2.168 (−365.5) | 0 | 0 |
| 108 | Lowpass | 1024 | 511.5 | 1024 | −6.800 | **6.000** (−0.5) | **−0.8** | **−6.8** |

All nine: `eL = 1`, `eR = 0`, flag = 1 **[EXEC]**. Laws (our own statement, `h(u) = sin²(πu)`):

- **Classic (100): `T = 7·h(u) − 1`** — a raised-cosine bump of height 7 on a baseline of **−1** (so at r = 1
  every partial outside the bump is attenuated by 6.02 dB).
- **Lowpass (108): `T = 6.8·h(u) − 0.8`, and for i > N/2 additionally `− 6·cos²(πu)`** — same bump (peak 6.0 at the
  centre), baseline **−0.8 below**, falling to **−6.8 above** (a dip after the peak, as the manual says).
- Both re-derived laws match the executed tables to **≤ 2.4·10⁻⁷** (float rounding) **[DECOMP + EXEC]**
  (`restab.py`). The other seven are used from the executed tables; their closed forms were not transcribed.
- The peak of Classic/Lowpass sits exactly at `pos = o`, i.e. at **x = L + p60/1200 (+8·A₁₂)**: with the common
  p60 = 600 the peak is **half an octave above the cutoff**.

### 3.2 Custom shapes 110/111 **[DECOMP `FUN_00874ed0`; round 23 for the curve]**

- Envelope 0x37/0x38 (shaping "Resonance shape 1/2") rendered into **N = 1024** entries, **o = 0.5·(N−1) = 511.5**,
  eL = eR = 1, **S = 256**; the values are **multiplied by 24** (bit 7/8 of the mask `0x877f50` = 0x0180 selects the
  "×24" branch instead of squaring) **[DECOMP; `0x877e50` = 24.0]**.
- These envelopes have **centre 0.5** (mask `0x19490`, round 23), so **T = 24·(y − 0.5)** ∈ [−12, +12]
  (y = 1 → +72 dB at r = 1). With S = 256 and p59 = 500, the 1024 entries span **8 octaves** (±4 around the offset).
- A flat 0.5 curve gives T ≡ 0; the end-trim then reduces the table to one entry and the flag is cleared (the
  expected neutral value for codes 100–199 is 0) → **no resonance at all** **[DECOMP]**.
- **[DATA]** Resonance shape 1 is the flat 0.5 line in 192/198 parts, shape 2 in 196/198. Whether the 1024 render
  points sit at `i/(N−1)` or `i/N` of the curve was not checked (≤ 1 entry difference; **not confirmed**).

### 3.3 The resonance mask (always active with the default curve) **[DECOMP `FUN_008740e0`, `FUN_00874ed0`, `FUN_00878e40`, `FUN_007a1930`, `FUN_0079e680`; DATA]**

- `FUN_008740e0(part, k)` rebuilds table k; k = 6…10 build the five mask records with codes 400…800 (envelopes
  0x3D…0x41 = shaping "Filter 1 mask", "Filter 2 mask", **"Filter 1 resonance mask" (k = 8, code 600, record
  `part − 0xA88 + 0x3F·0x38`)**, **"Filter 2 resonance mask" (k = 9)**, "Phaser mask"). `FUN_00878e40` rebuilds
  all eleven tables and is called from the part constructor and from `FUN_0086f2f0` **[DECOMP]**.
- Mask table: **N = 2160**, **240 entries per octave** (S = N/9), envelope **squared** (centre 0 for these
  envelopes), origin `o = (N−1)·((−1 − X₀) + 1/480)/9` → entry 0 at ≈ log₂ f = X₀ + 1 (**32.7 Hz**), last entry at
  X₀ + 10 (**16.7 kHz**), clamped outside **[DECOMP; `0x877e40` = 9, `0x877e48` = −1, `0x877e30` = 1/480]**.
- Its flag is set whenever the table is not a constant 1.0 → the render then uses the masked exponent **[DECOMP]**.
- **[DATA]** The resonance masks carry the same non-flat curve in 196/198 (filter 1) and 198/198 (filter 2) parts:
  points (0; 0.3125) → (0.8148; 1.0) → (1; 0.6875), both segments **mode 9** with tension +1 and −1.
- Mode 9 (our statement of `FUN_007a1930`/`FUN_0079e680`): `f(t) = t − (t − g(t))·|T|` with `g = sin(πt/2)` for
  T > 0 and `g = 1 − cos(πt/2)` for T ≤ 0 (`0x92af98` = π/2; coefficient = |T|) **[DECOMP]**.
- **Resulting default mask M(f)** (`resmodel.mask_at`):

  | f (Hz) | ≤ 32.7 | 50 | 100 | 200 | 500 | 1 k | 2 k | 5 k | 5.26 k | 8 k | 12 k | ≥ 16.6 k |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | M | 0.098 | 0.163 | 0.298 | 0.460 | 0.683 | 0.831 | 0.940 | 1.000 | 1.000 | 0.902 | 0.676 | 0.473 |

  **The resonance (in dB) is scaled by M at each partial's absolute frequency**: a resonance placed at 100 Hz has
  30 % of the dB of the same setting at 5 kHz. (That this default curve was intended as "neutral" by the designers is
  not claimed; the code applies it.) The "Filter 1/2 mask" envelopes (for the filter shape itself) are the flat 1.0 line in all 198 parts → trimmed to one
  entry equal to the neutral 1.0, flag off (not active) **[DATA + DECOMP]**.

### 3.4 Gain vs octave distance (defaults p59 = 500, p60 = 600; oct scale; articulators neutral; mask **off**) **[model `resmodel.py`]**

w = 1024·2^(1 − 2) = **512 entries/octave**; Classic/Lowpass bumps span **±0.5 octave** around `L + 0.5 oct`.
Values in dB (multiply by M(f) for the masked result):

| dx = x − L (oct) | −2 | −1 | −0.5 | −0.25 | 0 | +0.25 | **+0.5** | +0.75 | +1 | +1.25 | +1.5 | +2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Classic r = 0.25 | −1.51 | −1.51 | −1.51 | +0.03 | +3.75 | +7.49 | **+9.03** | +7.49 | +3.75 | +0.03 | −1.51 | −1.51 |
| Classic r = 0.5 | −3.01 | −3.01 | −3.01 | +0.06 | +7.51 | +14.97 | **+18.06** | +14.97 | +7.51 | +0.06 | −3.01 | −3.01 |
| Classic r = 1 | −6.02 | −6.02 | −6.02 | +0.12 | +15.02 | +29.94 | **+36.12** | +29.94 | +15.02 | +0.12 | −6.02 | −6.02 |
| Lowpass r = 0.25 | −1.20 | −1.20 | −1.20 | +0.29 | +3.91 | +7.53 | **+9.03** | +6.20 | −0.62 | −7.43 | −10.24 | −10.24 |
| Lowpass r = 0.5 | −2.41 | −2.41 | −2.41 | +0.57 | +7.81 | +15.06 | **+18.06** | +12.41 | −1.23 | −14.86 | −20.47 | −20.47 |
| Lowpass r = 1 | −4.82 | −4.82 | −4.82 | +1.15 | +15.62 | +30.12 | **+36.12** | +24.82 | −2.47 | −29.72 | −40.94 | −40.94 |

Closed forms (our own, oct scale, `Δ = x − L − p60/1200`, `u = ½ + Δ·w/1023` clamped to [0, 1], `m = M(f)`):
- Classic: `dB = 6.0206·r·m·(7·sin²(πu) − 1)`; **peak above the skirt = 42.14·r·m dB**; **−3 dB width (oct) =
  (2/π)·asin(√(3/(42.14·r·m)))·1023/w**, defined for 42.14·r·m > 3. At p59 = 500, r = m = 1: **0.34 oct**.
- Lowpass: same peak (36.12·r·m dB absolute, 40.94·r·m above the lower skirt); lower half-width uses 6.8, upper uses
  12.8 in place of 7: `(1/π)·asin(√(3/(6.0206·c·r·m)))·1023/w` per side.

Peak / −3 dB width vs r and p59 (mask off; peak absolute dB / width oct):

| p59 | Classic r = 0.1 | 0.25 | 0.5 | 1 | Lowpass r = 0.1 | 0.25 | 0.5 | 1 |
|---|---|---|---|---|---|---|---|---|
| 0 | 3.61 / 0.316 | 9.03 / 0.176 | 18.06 / 0.121 | 36.12 / 0.082 | 3.61 / 0.268 | 9.03 / 0.154 | 18.06 / 0.105 | 36.12 / 0.074 |
| 250 | 3.61 / 0.637 | 9.03 / 0.355 | 18.06 / 0.242 | 36.12 / 0.168 | 3.61 / 0.539 | 9.03 / 0.311 | 18.06 / 0.213 | 36.12 / 0.148 |
| **500** | 3.61 / 1.273 | 9.03 / 0.715 | 18.06 / 0.488 | 36.12 / **0.340** | 3.61 / 1.080 | 9.03 / 0.621 | 18.06 / 0.428 | 36.12 / **0.299** |
| 750 | 3.61 / 2.551 | 9.03 / 1.430 | 18.06 / 0.980 | 36.12 / 0.684 | 3.61 / 2.162 | 9.03 / 1.242 | 18.06 / 0.857 | 36.12 / 0.600 |
| 1000 | 3.61 / 5.105 | 9.03 / 2.863 | 18.06 / 1.965 | 36.12 / 1.371 | 3.61 / 4.326 | 9.03 / 2.486 | 18.06 / 1.717 | 36.12 / 1.201 |

Other types at r = 1, p59 = 500, p60 = 600 (mask off): Cuberdon +37.6 dB / 0.23 oct at +0.5; Pedestal +34.9 / 0.46 at
+0.33; Sedge hat +41.5 / 0.07 at +0.5; Wide bump +32.5 / 0.85 at +0.17; Double cone +37.3 / 0.19 at −0.25 (two
peaks); Well: no boost (notch down to −48.2 dB); Wormhole +13.1 / 0.22 at −0.21 (notch to −60.2 dB).

### 3.5 Hz scale (p58 = 1)

`w = 0.000375·1024·2^(1 − 0.004·p59)` entries/Hz (p61 = 0): at p59 = 500, **0.192 entries/Hz** → the 1024-entry
Classic bump spans **±2 664 Hz** around `fc·2^(p60/1200)`, the same width in Hz for every cutoff **[DECOMP]**.
At r = 1 the −3 dB width is 176 entries ≈ **917 Hz** (p59 = 500) **[model]**.

---

## 4. The demos: parts with p57/p69 > 0 **[DATA + model `demos.py`, `hzscale.py`]**

Assumptions: articulators neutral (A₈ = A₁₀ = 1, A₁₂ = 0; no filter envelope), keytracking evaluated at the reference
note 130.81 Hz (KT has no effect there), cutoff from p52/p64 with the width-widened range of round 18, quality > 0
(interpolated), **resonance mask = default curve (all these parts)**. **75 (part, filter) entries** with resonance
> 0 in 54 distinct settings. Usage: filter 1 resonance type **Lowpass in 163/198 parts** (Classic 17, Custom 1 9,
Noise 3, others ≤ 2); filter 2 **Custom shape 2 in 178/198** (Classic 18) — with flat custom curves, i.e. no effect;
p58 = 1 in 5 filter-1 entries; p59 = 500 and p60 = 600 dominate (171 and 170/198) **[DATA]**.

Columns: peak = maximum of the resonance gain alone (dB, mask on) and its distance from the cutoff; width = −3 dB
width of that bump (blank when the bump rises < 3 dB above its skirts); "over pass-band" = (filter shape × resonance)
maximum near the bump minus the same product 2 octaves below the cutoff (LP-type filters only); Q = SVF Q with the same
peak (LYNX-side approximation, §6). Full table with file names: `tmp/r25/out/demos_table.md`.

| # | Demo (file / channel) | F | Filter | Res. type | p57 | p59 | p60 | fc Hz | peak dB @ oct | −3 dB oct | over pass-band dB | Q | peak, mask off |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Astes – Bien Duro / 4.6 Lead (part 0) | 1 | Classic LP | Lowpass | 138 | 500 | 0 | 1879 (KT 1) | +4.65 @ 0.00 | 0.90 | +5.09 | 1.72 | +4.99 |
| 2 | Bien Duro 4.6 Lead; Sacco Punchy Buzz; Square a Saw–Lightning arp bass / lead synth / arp (×6) | 2 | Classic LP | Classic | 585 | 500 | 0 | 1454 | +18.96 @ 0.02 | 0.47 | +21.07 | 11.3 | +21.13 |
| 3 | Bien Duro / 4.6 Lead (part 1) | 1 | Classic LP | Classic | 39 | 896 | 0 | 367 (KT 1) | +0.90 @ 0.37 | — | +0.81 | 0.92 | +1.41 |
| 5 | Atthar / Saw PhaseRN (part 1) | 1 | Crude LP | Lowpass | 282 | 500 | 600 | 4579 | +9.95 @ 0.47 | 0.58 | +9.32 | 2.88 | +10.19 |
| 7 | DEDZII – Night Terror / Dedziiboost | 1 | Crude LP | Lowpass | 738 | 221 | −173 | 1383 (KT 1) | +23.24 @ −0.14 | 0.17 | +25.35 | 18.5 | +26.66 |
| 8 | LollieVox / Blips | 1 | Crude HP | Lowpass | 504 | 104 | −1793 | 218 (KT 2) | +4.45 @ −1.49 | 0.31 | n/a | — | +18.20 |
| 12 | Mehdi – Never Out / Basic wobbling, Duck Duck Pluck | 1 | Crude LP | Lowpass | 747 | 660 | 0 | 833 (KT 0.65) | +21.53 @ 0.04 | 0.57 | +23.20 | 14.5 | +26.98 |
| 15 | Charles Deluxe – Performance (part 1) | 1 | Classic LP | Lowpass | 411 | 500 | 600 | 17740 | +5.08 @ 0.17 | — | +5.74 | 1.87 | +10.75 |
| 17 | Charles Deluxe (part 0); Square a Saw–Oxygen / talking bass | 1 | Classic LP | Classic | 138 | 500 | 600 | 166 | +2.52 @ 0.56 | — | +0.97 | 0.95 | +4.99 |
| 18 | SeamlessR – Bass Antics / Filthy | 2 | Phaser | Classic | 189 | 240 | −840 | 463 | +3.74 @ −0.69 | 0.60 | n/a | — | +6.83 |
| 19 | SeamlessR Lead, Lead #2; Oxygen supporting lead | 1 | Classic LP | Classic | 96 | 500 | 600 | 6278 (KT 1) | +3.00 @ 0.43 | 1.50 | +1.94 | 1.12 | +3.47 |
| 20/21 | Square a Saw–Lightning HW/HX vocal-chop channels | 1 / 2 | Classic LP / HP | Classic | 51 / 48 | 500 | 600 | 17740 / 73 | +0.62 / +0.54 | — | +0.67 / n/a | 0.90 | +1.32 / +1.73 |
| 23 | Lightning / yaaah bass | 1 | Custom 1 | Double cone | 721 | 928 | 600 | 401 | +26.45 @ 2.98 | 0.75 | n/a | — | +26.91 |
| 26 | Lightning / lead synth (part 1) | 1 | Classic LP | Lowpass | 832 | 500 | 600 | 241 (KT 0.38) | +17.82 @ 0.53 | 0.41 | +10.42 | 3.28 | +30.05 |
| 28/29 | Lightning / drop pad (parts 0/1) | 1 | Crude LP | Lowpass | 510/528 | 500 | 600 | 6071 | +16.2/+16.8 @ 0.44 | 0.45 | +16.7/+17.4 | 6.8/7.4 | +18.4/+19.1 |
| 31 | Lightning / pad | 1 | Classic LP | Lowpass | 294 | 500 | 0 | 562 (KT 0.3) | +7.56 @ 0.02 | 0.66 | +8.07 | 2.48 | +10.62 |
| 32 | Lightning / pad | 2 | Classic LP | Classic | 174 | 500 | 0 | 544 | +4.43 @ 0.04 | 1.09 | +4.80 | 1.66 | +6.29 |
| 42 | Oxygen / evil plucked bass | 1 | Custom 1 | Custom shape 1 | 537 | 200 | 216 | 2494 | +12.79 @ −0.05 | 0.14 | n/a | — | +13.31 |
| 43 | Oxygen / growl bass | 1 | Classic LP | Double cone | 757 | 1000 | −2400 | 3058 | +28.01 @ 0.93 | 0.76 | +21.31 | 11.6 | +28.26 |
| 46 | Menagerie / Emblem | 1 | Crude LP | Lowpass | 321 | 500 | 600 | 822 | +9.97 @ 0.51 | 0.57 | +8.54 | 2.63 | +11.60 |
| 47–49 | Menagerie / Arp, Lead, Riser | 1 | Crude LP | Lowpass | 141/135/174 | 500 | 600 | 59/58/263 | +1.3/+1.3/+3.9 | —/—/1.01 | +0.6/+0.6/+2.5 | 0.88/0.87/1.21 | +5.1/+4.9/+6.3 |
| 52–54 | Treben – All the same / Pad, Saw, Pluck | 1 | Crude LP | Lowpass | 188/128/204 | 500 | 0/0/−645 | 3434/1017/9733 | +6.7/+3.9/+7.1 | 0.72/1.02/0.70 | +7.4/+4.2/+8.1 | 2.30/1.53/2.48 | +6.8/+4.6/+7.4 |

(Rows not repeated here — small Classic/Lowpass amounts ≤ 0.1, custom curves with p57 = 33/52, the Hz rows, Noise
and flat custom curves — are in `demos_table.md`.)

- **Hz-scale rows** (`hz_table.txt`): Mehdi "Shine" part 0 (Classic, r = 1, p59 = 260, p60 = 576, fc 1879 Hz) →
  peak **+35.0 dB at 2.63 kHz**, −3 dB **2.40–2.87 kHz (0.26 oct)**; part 1 of the same channel has fc ≈ 4 Hz (bump
  centred at 5.6 Hz, its upper half spills into the audio band: masked peak +17.8 dB at 352 Hz); Lightning "arp bass"
  (Custom 1, r = 0.087) +4.7 dB at 4.2 kHz.
- **Flat custom curves** (Blips filter 2, Oxygen offbeat pluck filter 2, both Custom shape 2 with p69 > 0) → **no
  resonance** (§3.2). **Noise** (Razer Chroma Bass, Sacco Punchy Buzz part 1) → not modelled (§5).
- **The static cutoff is not the playing cutoff** in many of these parts (filter envelopes/LFOs, KT); because of the
  absolute-frequency mask, the dB numbers move with the cutoff. The "mask off" column is the cutoff-independent upper
  bound.

---

## 5. Not decoded / open in this round

- **Noise path** (code 109): the render takes a separate branch that derives a rate from p59 ("noise length") and the
  sample rate, draws pseudo-random values with an integer hash and applies them through several IPP routine pointers that were
  not named in this round — **not decoded**; only 2 demo entries use it.
- **Self-oscillation p62**: `ProcessParam` writes `(p62/1024)²` into a shared float; the consumer (the sine at the
  resonant frequency described by the manual) was **not traced**. p62 > 0 in a few demo parts (e.g. 894 in Mehdi
  "Shine", 6 in "Blips").
- `+0x4A0` (−0.125 / 0.875 / −1.5 / 0.5): no reader found — **not confirmed**.
- Closed forms of shapes 101–107 (only the executed tables are used); envelope-render sampling convention for the
  custom table (`i/(N−1)` vs `i/N`); exact meaning of voice `+0x1AC` (pitch in cents — inference).
- Articulator values in the demos (A₈/A₁₀/A₁₂ and the cutoff envelope) were taken as neutral, not read.

---

## Summary table (question → result)

| # | Question | Result | Evidence |
|---|---|---|---|
| 1a | k₀ `+0x49C` | **k₀ = S** (oct), **S·92.4986/f₀** (oct + adaptive), **0.000375·S** (Hz), **0.000375·S·184.997/f₀** (Hz + adaptive); S = 1024 (types 100–108), 256 (custom) | DECOMP + EXEC |
| 1b | voice `+0x1B8` | **voice frequency in Hz** (2^(+0x1BC), +0x1BC = log₂ 440 + (cents/100 + 3)/12) | DECOMP |
| 2 | `+0x1150` | **`ippsExp_32f_A11`** (quality ≤ 0) / **`ippsExp_32f_A24`** (quality > 0) → gain **2^(r·M·T)** | DECOMP + PE strings |
| 3a | table `+0x428` | per type: N, o, S, ends in §3.1; custom = 24·(y − 0.5), N 1024, o 511.5, S 256; sampled at `o + (x − L − p60/1200 − 8A₁₂)·w`, `w = k₀′·2^(1 − 0.004·p59·A₁₀)`; ends extend T[0]/T[N−1] | DECOMP + EXEC |
| 3b | effect of p58 | switches the axis from log₂ f (entries/octave) to f in Hz (entries/Hz, k₀ × 0.000375) and the offset to a frequency factor | DECOMP |
| 3c | default shape | filter 1 default in the data = **Lowpass (108)**, filter 2 = **Custom shape 2 with flat curve (no effect)**; DFM default index 0 = Classic | DATA |
| 3d | gain vs octaves | §3.4 table; Classic/Lowpass peak **36.12·r dB** at `L + p60/1200`, ±0.5 oct span at p59 = 500, skirt −6.02·r (Classic) / −4.82·r below, −40.94·r above (Lowpass) | model on EXEC tables |
| new | resonance mask | default "Filter 1/2 resonance mask" curve is active and scales the resonance dB by **M(f) = 0.10 … 1.0 … 0.47** (32.7 Hz … 5.26 kHz … 16.7 kHz) | DECOMP + DATA |
| 4 | demos | §4 table; peaks from +0.3 to +28 dB (mask on), −3 dB widths 0.14–1.5 oct | DATA + model |
| 5 | noise | not decoded | — |

---

## 6. For LYNX

LYNX today (`src/core/FlSynths.cpp`, `synthFromHarmor`): the Harmor filter becomes the 2-pole SVF with
`resonance = p57/1000` and `k = 1/Q = 2 − 1.9·res` (Q 0.5…10). What this round lets LYNX do (only confirmed laws; the
SVF mapping itself is a **LYNX-side approximation**, not from FL):

1. **Exact law, if LYNX ever applies Harmor's filter per partial** (spectral oscillator): multiply each partial by
   `2^(r·M(f)·T(pos))` with T from §3.1/§3.2 (tables `tmp/r25/run/shape_100…108.txt`, or the closed forms for Classic
   and Lowpass), `pos` and `w` from §2, M from §3.3 (`resmodel.py` is a reference implementation). This is static per
   note when the cutoff is static.
2. **SVF approximation (current architecture):**
   - Effective amount: `r_eff = (p57/1000)·M(f_b)` with **`f_b = fc·2^(p60/1200)`** (bump frequency).
   - Peak over the pass-band: **Δ = 42.14·r_eff dB (Classic) / 40.94·r_eff dB (Lowpass)** + the filter shape's own
     gain at f_b (round 23 §1.8; e.g. Crude LP at +0.5 oct with p54 = 500: −2.75 dB; Classic LP: −9.0 dB).
   - **Q = √(G²·(1 + √(1 − 1/G²))/2), G = 10^(Δ/20)** (2-pole LP peak = Q/√(1 − 1/(4Q²))), then
     `res = (2 − 1/Q)/1.9`, clamped to [0, 1] (Q > 10 saturates: rows 2, 7, 12, 43).
   - Known mismatches to log: (a) Harmor's bump sits at f_b, not at the LP corner — with p60 = 600 it is half an octave
     above the corner; an SVF peaks at its own cutoff; (b) Harmor's width is set by p59 independently of the peak
     (0.34 oct at r = 1, p59 = 500) while an SVF's −3 dB width is ≈ 1/(Q·ln 2) oct (0.13 oct at Q = 11); (c) Classic
     lowers everything outside the bump by 6.02·r_eff dB and Lowpass cuts above the bump by up to 40.9·r_eff dB — the
     SVF does neither; (d) Well/Wormhole/Double cone, Hz scale and adaptive width have no SVF equivalent.
   - Skip resonance (res = 0) for flat Custom shapes (§3.2) and for Noise (not decoded; log it).
3. Typical demo values (§4): most active settings are Lowpass type with p57 ≈ 0.1–0.3 on a Crude LP → +1…+10 dB over
   the pass-band, Q ≈ 0.9–2.9; strong ones (Dedziiboost, Basic wobbling, Bien Duro filter 2, growl bass) reach +21…+25 dB.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `FUN_00886eb0` (0.001 `0x92acb8`, 8.0 `0x887f00`, 1.0, ln 2, `+0x198`/`+0x190`, `+0x1B8`, `+0x45C`, `+0x430 == 0x6D`) | render path, position, width, gain | DECOMP |
| `FUN_008744c0` (−27, −15, 440, 0.000375, ln2/12) + `tab2.exe k0` | k₀ and `+0x4A0` | DECOMP + EXEC |
| `FUN_00879540` cases 0x3A/0x3B/0x3C/0x3E/0x46/0x47/0x48/0x49 (0.004, 1/1200, 1/1024, mask `0x87ca42` = 0x240) | parameters → fields | DECOMP |
| `FUN_0086ed30` (misc+0x34…+0x40 → `+0x3F8`, `+0x430`, `+0x4B8`, `+0x4F0`) | type storage | DECOMP |
| `FUN_00874ed0` codes 100–111, 400–800 (2π, 7, 6.8, 0.8, 3, 24 `0x877e50`, mask `0x877f50`, 2160, 9, −1, 1/480, 0.25) + `tab2.exe shape 0…16, 100…108` | tables | DECOMP + EXEC |
| `FUN_00874930`, `FUN_00874860` | interpolation / clamping | DECOMP |
| `FUN_00882af0`, `FUN_008827d0`, `FUN_00882820` | exponent, masked exponent, multiply | DECOMP |
| `FUN_00878f60` + `FUN_005bd1c0` (`0x93a1b0`/`0x93a1c0` ↔ `ippsExp_32f_A11`/`_A24`) | `+0x1150` = exp | DECOMP + DATA |
| `FUN_0088ab10` @ `0x88c5dd` (0.01, 60, 57, 1/12, log₂ 440) | voice `+0x1B8` = Hz | DECOMP |
| `FUN_00630170`, `FUN_0089a4d0` | ln2/12, ln2/1200, log₂ 440, X₀ = log₂ 16.35 Hz | DECOMP |
| `FUN_008740e0`, `FUN_00878e40` (callers `FUN_00870750`, `FUN_0086f2f0`) | mask tables always rebuilt | DECOMP |
| `FUN_007a1930`, `FUN_0079e680` (mode 9, π/2, |T|) | mask curve shape | DECOMP |
| DFM `tmp/r11/harmor.dfm.txt` (`F1ResTypeSelect`, `F1ResWheel`, `F1OscWheel`, tags 57–73) | names, ranges | DATA |
| 198 Harmor parts (`hm.py`, `survey.py`, `demos.py`, `hzscale.py`) | types, parameters, curves, per-demo numbers | DATA |
| Harmor manual | type descriptions, offset ±2400 cents, adaptive width, self-oscillation | DOC Harmor.htm |
