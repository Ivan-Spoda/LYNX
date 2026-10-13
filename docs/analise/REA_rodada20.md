# Behavioural analysis of FL Studio 2026: round 20 (REA) — Love Philter *pattern* envelopes and LFO, Fruity Scratcher, remaining Patcher graphs, Vocodex and Fruity Vocoder

> **Nature of this document.** Continuation of `REA_rodada19.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode was copied: what is here are formulas and behaviours described in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was
> touched (no FLEX package was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where
> there was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> (e.g. "this routine is `exp` because it is always called right after multiplying by ln 2") are made explicit in the text.

- **Targets:**

  | Plugin | DLL (SHA-256) | Language | New key functions |
  |---|---|---|---|
  | Fruity Love Philter | `b9f11d63…ba2ff547` (= rounds 16…19) | Delphi | *tick* `FUN_00856690`/`FUN_00856570`, time scale `FUN_00788080`, articulator VMT `0x853B18` (`FUN_008543d0`, `FUN_00854540`), *pattern* switch `FUN_0085d050`, sync `FUN_0085cc80`/`FUN_0085cfa0`, `TMEnvelope` VMT `0x77F9A8` (`FUN_00786350`, `FUN_00783ec0`, `FUN_00787ba0`, `FUN_00787fd0`), `TMLFO` VMT `0x77FB48` (`FUN_00788b70`, `FUN_007886b0`, `FUN_00788830`), waveform `FUN_0060e450`/`FUN_0060e610` |
  | Fruity Scratcher | `Fruity Scratcher_x64.dll` `52c126ad3e8b684a24a9bff7b8bc77de73984732d14a3e882e01b00366b09b42`; `engine_x64.dll` `104a1f410f8018bea64f2ae1449ebf7e8383f744baf6be0278d0c34ee3d8ab1b` | Delphi (+ C++ engine for timecode only) | state `FUN_00728d80`, `ProcessParam` `FUN_00729fe0`, application `FUN_0072a630`, *reset* `FUN_00729f10`, render `FUN_0072add0`, sample loading `FUN_0072be20`, DC `FUN_0072c660`/`FUN_0072c6d0` |
  | Patcher | `cb4212c4…c64b` (= round 14) | Delphi | input mixing `FUN_0079bf20`, cable reading `FUN_00952440`, delay `FUN_0079f0b0`, IPP by name `FUN_005da820` |
  | Fruity Vocoder | `Fruity Vocoder_x64.dll` `bfc3bec3…a63961b6` (= round 19) | Delphi (`TTestPlug`, VMT `0x6cf210`) | state `FUN_006cf810` (slot 3), parameters `FUN_006d08c0` (slot 6), render `FUN_006d0fb0` (slot 7), setup `FUN_006d03a0`/`FUN_006cff90`/`FUN_006d00b0`, biquad `FUN_005dbb00`/`FUN_005db530`, follower `FUN_006d0e70`, UI `FUN_006d4cf0`/`FUN_006d4d50`/`FUN_006d4a20`/`FUN_006d5070` |
  | Vocodex | `Vocodex_x64.dll` `43d10692…22ca714bb0` (full SHA-256 `43d10692ea04457018138110a1a3035d99ed8b40a7f52e67a3109c22ca714bb0`) | Delphi (`TTestPlug`, VMT `0x84e438`) | constructor `FUN_0084f730`, state `FUN_0084efb0` (slot 3), parameters `FUN_00853e80` (slot 6), render `FUN_00856350` (slot 7), bands `FUN_00851dd0`, frequency scale `FUN_0062cb20`/`FUN_0062cd90` |

- **Tools:** Ghidra 12.1.4 *headless* (previous projects opened read-only: `/tmp/r17gh_LP`,
  `/tmp/r14gh_Patcher_x64.dll`; new: `/tmp/r20gh_Scratch`, `/tmp/r20gh_Vocoder`, `/tmp/r20gh_Vocodex`; Vocodex/Vocoder scripts in `tmp/r20/voc/`); scripts in `tmp/r18/gh/scripts/`; `tmp/r18/k.py`
  and `tmp/r18/vmtname.py` for constants and VMTs; data with `tmp/r16/lp.py`, `tmp/r20/py/lpenv.py`,
  `tmp/r20/py/pat.py` (port of the `FlPatcher.cpp` classifier) and `tmp/r20/py/sp.py` (series-parallel reduction).
  Official manual read via the web.
- **Date:** 2026-10-13.
- **Track numbering:** the (track, slot) pairs in §1 and §3 are those of the round 14 *pickle*
  (`blobs14.pkl`); §4 counts the `0xEC` events starting from Master = 0 and found, for the tracks with Vocoder/Vocodex,
  an offset of 1 relative to the *pickle* (§4.1.5). When cross-checking with the `.flp`, verify by track name.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked against `.flp`/`.fst`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated as `…/plugins/` below). The manual quotations came from a summarised web reading; the quoted passages
are short and must be re-checked on the page before becoming interface text.

---

## 1. Fruity Love Philter — *pattern* envelopes, articulator LFO and envelope follower **[DECOMP + DATA + DOC]**

**What LYNX assumes / needs:** reproduce the 9 rhythmic "envelope → volume" *gates* and the 7 + 7 LFOs → pan/cutoff
of the demos (round 16 §2.6). Missing were: time unit of the points, which *pattern* plays, *loop*, curve shapes,
LFO law, synchronisation and the role of the envelope follower.

### 1.1 Control clock (*tick*) and time scale **[DECOMP `FUN_00856690`, `FUN_00856570`, `FUN_00854e20`, render `FUN_0085aa10`]**

- The articulators **are not evaluated per sample**: the render counts one *tick* every `N` internal samples
  (counter `+0x169C`, reloaded with `+0x16A0`). On each *tick* it walks the list of active articulators
  (`+0x1760`), computes the output of each one (X/Y/IEF maps and then envelope/LFO, §1.4) and the resulting value is
  applied to the audio with a **linear ramp across the block** up to the next *tick* (`FUN_0062aee0`: step
  `(target − previous)/n` per sample, limited in magnitude by field `+0x38` of object `+0xCC`; the value of that limit
  **was not identified**).
- `N` comes from `FUN_00856690(v)`, with `v` = **64** (HQ envelopes off) or **8** (on, or when bit 1 of the
  value of host event 1 sets `+0x16D2`) — table `0x8D1A08` = `{64, 8}` indexed by `+0x154 | +0x16D2`:
  **`N = max(4; 4·round(v·fs_int/44100/4))`**, where `fs_int` = field `+8` of object `+0xCC` (initialised with
  `fs·oversampling` in `FUN_00854E20`; that it is the internal rate is an **inference** from this use). At 44.1 kHz without
  *oversampling*: **N = 64 samples ≈ 1.45 ms** (HQ: 8 samples ≈ 0.18 ms). The *tick* is always ≈ 64/44100 s,
  whatever the rate.
- In `FUN_00856570`, with `τ = N/fs_int` (seconds per *tick*, by the deduction above) and `BPM` = field `+0x1690`
  (written by host event 0 in `FUN_00857990`; that it is the tempo in BPM is an **inference** from the formula):
  - "free" scale = **`2·τ`**;
  - "tempo" scale = **`τ·BPM/60`** (= beats per *tick*).
  Both are passed to all envelopes and to the LFO of each articulator (`FUN_00854540` → method 15 of
  `TMEnvelope` `FUN_00788080`), which **chooses the "tempo" scale if bit 0 of the envelope *flags* (TEMPO) is
  set**, otherwise the "free" one.
- On each *tick* the envelope advances one unit (constant 1.0 at `0x85C2A0`) and the position within the segment grows by
  `scale / (Δt·k)` (`FUN_00786350`; `k` = the segment's A/D/S/R *knob* factor, below). Therefore:
  - **with TEMPO: Δt is in beats (quarter notes)** — a segment with Δt = 1 lasts exactly 1 beat;
  - without TEMPO: a Δt = 1 lasts `1/(2τ)` *ticks* = **0.5 s** (deduced from the two formulas; no demo data uses
    this case — **not checked by render**).
- Envelope A/D/S/R *knobs* (`+0x98…+0xA4`, i32, default 128/128/0/128) → factors `v/128`
  (`FUN_00788250`, constant `PTR 0x8D3970` = 1/128) copied into the state at start (`FUN_00788300`).
  **All enabled envelopes in the demos have 128/128/0/128 → factor 1** **[DATA]**.
- **Data:** the 11 enabled *pattern* envelopes and the 14 enabled LFOs of the active units in the demos **all have the
  TEMPO bit set** (*flags* = 1, or 3 in 2 LFOs) **[DATA `tmp/r20/py/lpenv.py`]**.

### 1.2 Which *pattern* plays, *loop* and synchronisation with the song **[DECOMP `FUN_0085d050`, `FUN_0085cc80`, `FUN_00787ba0`, `FUN_00787fd0`, `FUN_00786350` + DOC + DATA]**

- **Only one *pattern* plays per articulator:** when global parameter 5 changes (PatNum, clipped to 1…10;
  `FUN_008579e0` → `FUN_0085d050`) each articulator starts pointing (`+0x10`) to the envelope with index PatNum
  (`+0x28 + 8·PatNum`). The other 9 envelopes are not evaluated. Manual: "Use the selector to the left of the tab
  to change Modulation pattern" and "MIDI notes C5 to E6 can be used to change patterns on-the-fly" **[DOC
  `…/plugins/Fruity%20Love%20Philter.htm`]**. **Data:** PatNum = 1 in all 43 slots and only envelope 1 is enabled
  **[DATA]**.
- **Loop** (`FUN_00786350`): on reaching the "loop end" point (field `+0x88`, prepared from the envelope
  header) the envelope **jumps back to the loop start point** (`+0x50`) if it is ≥ 0; otherwise it stays at the
  last value. In the demos the header is always `enabled = −1, decay = −1, loop = 0, sustain/end = last point`
  **[DATA]** → **the whole *pattern* repeats in a *loop***, with period `T = ΣΔt` (sum of the Δt of points 1…n):
  **1 beat** in 8 of the 9 volume envelopes, 0.25 beat in 1 ("Drumloop Masher"), 4 beats in the cutoff one
  ("LollieVox").
- **Synchronisation with the song position** (`FUN_0085cc80`): when host event 12 arrives with value 1
  (`+0x16D1`; that it means "playing" is an **inference**) `FUN_0085cfa0` is called, which resets the *tick* counter and
  repositions **all envelopes and LFOs** at the current song position: the position comes from a query to the host
  (code 0x24) converted to samples (`×fs_int×240 / (BPM·…)`; the exact format of the query **was not
  confirmed**) and then to *ticks*. The envelope is positioned at `t·scale` (= beats, with TEMPO) **reduced to the
  *loop* interval** (`FUN_00787fd0`: `t ← t − ⌊(t − T₀)/T_L⌋·T_L`, with T₀ = time of the start point and T_L
  = *loop* duration). Conclusion: **with TEMPO, the *pattern* phase is `song position (beats) mod T`** from the
  *loop* start.
- On a *pattern* switch the repositioning is done with *flag* `+0x15E` (default 1 in the constructor; it is one of the 3 option
  bytes of the global block — that it is "Sync pattern changes" is an **inference** from the manual: "Envelopes are synced
  to song/pattern position when selected") and **only the envelopes** (the LFO is not restarted in this case).
- Outside the transport (without *flag* `+0x16D1`) the starting position is 0 and the envelope runs freely from there.

### 1.3 Envelope value and curve shapes **[DECOMP `FUN_00786350`, `FUN_00785730`, `FUN_00783ec0` (`0x78411f…0x784196`), `FUN_0060e3c0`, method 0 `FUN_007800c0`/`FUN_00780090`]**

- **Centre:** the value of each point is `y − c`, with `c` = envelope field `+0x18` = **`mode × 0.5`**
  (`FUN_008544d0` → `FUN_00780090` → method 0; constant `0x7800B8` = 0.5). Additive articulators (mode 1: **pan,
  cutoff, resonance**) → value `y − 0.5` ∈ [−0.5; 0.5]; multiplicative ones (mode 0: **volume**, L/B/H, WS) →
  value `y` ∈ [0; 1]. This call **is not made for the LFO** (see §1.5).
- **Interpolation** between the previous point (value `a`) and the current point (`b`), with position `x` ∈ [0; 1] in the
  segment: `value = a + (b − a)·v(x)`. The curve is that of the **arrival point**. For curve type 0 (*single
  curve*, byte `+0xC` = 0; it is the type of all points of the demo *gates*, except one) with tension `t`:
  - `q = sign(t)·(1000001^|t| − 1)` (constant `0x784998` = 1,000,001; `FUN_0040c090`/`FUN_0040ad30` = ln/exp
    by **inference** from the `a^x` pattern);
  - `t = 0` → **linear** `v = x`;
  - `t > 0` → **`v = 1 − ((1 + q)^(1 − x) − 1)/q`** (changes quickly at the start of the segment);
  - `t < 0` → **`v = ((1 + |q|)^x − 1)/|q|`** (changes slowly at the start).
  E.g.: t = 0.285 → q ≈ 50 → v(0.5) = 0.877; t = 0.67 → q ≈ 1.05·10⁴ → v(0.5) = 0.990; t = −0.365 → v(0.5) =
  0.074 (computed from the formula).
- Other types (1 *double*, 2 *hold*, 3/4 *stairs*, 5 *pulse*, 6 *wave*, 7…12) have their own branches; in the demos only
  **type 5** appears in 1 point ("Drumloop Masher", unit 8) — its exact shape **was not decoded**.
- **File format** (matches round 11 §1.6): per point `f64 Δt`, `f64 y`, `f32 tension`, 4 B (the 1st
  byte = curve type) **[DATA]**.

### 1.4 Combination in the articulator **[DECOMP `FUN_0085aa10` (*tick* loop), `FUN_008543d0`; complements round 18 §4]**

On each *tick*, for each active articulator: `s = base` (field `+0x20` = the stored `f32`: 1.0 on multiplicative
targets, 0 on additive ones) → applies maps X, Y and IEF (each: mode 0 `s ← s·m`, mode 1 `s ← s + m`,
the IEF map additionally multiplied by the envelope depth `+0x24`) → **envelope** (if enabled): mode 1
`s ← s + E·d_E`, mode 0 `s ← s·E·d_E` → **LFO** (if enabled): mode 1 `s ← s + L·d_L`, mode 0
`s ← s·(1 + 2·L·d_L)` (constants `0x8544C4` = 2, `0x8544C8` = 1). `d_E` = `+0x24` and `d_L` = `+0x28` (the two
following `f32` in the file). In the demos, `d_E = d_L = 1` in all articulators with envelope/LFO enabled, except
two cutoff LFOs (0.516 and 0.641) **[DATA]**.

Application to the audio (render): cutoff `c = CUT/256 + s_cut`; resonance `r = RES/256 + s_res`; L/B/H gains =
parameter × `s`; **pan and volume** go to `FUN_00616620(pan = PAN/128 + 2·s_pan, vol = (+0x48)·s_vol)`, where
`+0x48` is the unit volume (round 16: `curve11(VOL/128)·curve11(Master/128)`) — the pan law inside
`FUN_00616620` **was not decoded**.

Direct consequences (formulas above):

- **Volume gate** (mode 0, base 1, `d_E = 1`): **unit gain × y(t)**, with `y(t)` = the *pattern*
  envelope (point values 0…1, no centre).
- **Cutoff envelope** (mode 1): `c = CUT/256 + (y − 0.5)·d_E`, and for the cutoff `d_E` = ENV/128 (round 16).

### 1.5 LFO **[DECOMP `TMLFO` VMT `0x77FB48`: `FUN_00788b70` (evaluation), `FUN_007886b0` (shape), `FUN_00788830` (rate), `FUN_00788a20`/`FUN_0060e870` (initial phase), `FUN_00788aa0` (positioning), `FUN_0060e450`/`FUN_0060e610` (waveform), constructor `FUN_00788410`]**

- **Output: `L = A(t)·W(φ)`**, where `A(t)` is the "LFO envelope" (the points stored in the LFO) evaluated as in §1.3,
  but with a **fixed centre of 0.5** (the `TMLFO` constructor sets `+0x18 = 0.5` and method 0 is not called for it) →
  `A = y − 0.5` ∈ [−0.5; 0.5]; and `W` ∈ [−1; 1] is the waveform below. Data: the demo LFOs have 1–2 points of constant
  level (`y` = 1.0 → A = 0.5; 0.625 → 0.125; 0.6875; 0.875; 0.9375; 0.75; 0.25; 0.0 → A = −0.5, inverted
  waveform) **[DATA]**.
- **Rate** (`FUN_00788830`): phase increment per *tick* (32-bit phase, 2³² = 1 cycle) =
  `round(min((10001^(S/65536) − 1)·(2³²/625)·scale; 0.1875·2³²))` (constants `0x7888C8` = 10001,
  `0x7888D8` = 6,871,947.67 = 2³²/625, `0x7888E0` = 805,306,368), S = speed (`+0xD0`, 1st LFO *knob* in the
  file). With TEMPO: **period `P = 625/(10001^(S/65536) − 1)` beats** (without TEMPO: half of that in seconds,
  through the free scale `2τ` — not checked by render). Demo values **[DATA + computation]**: S = 45819 → **1
  beat**; 40898 → **2**; 35989 → **4**; 31102 → **8**; 26259 → **16**; 21502 → **32 beats**.
- **Shape** (`FUN_0060e450`/`FUN_0060e610`), with *skew* `k = S₂/256` (`+0xD8`, ±128 → ±0.5) and pulse width
  `w = S₃/128` (`+0xDC`): phase φ ∈ [0; 1) divided into: linear descent from +1 to −1 up to `φ₆`; plateau at −1 up to `φ₅`;
  rise from −1 to +1 up to `φ₇`; plateau at +1 until the end. `φ₇ = 1 − w` (if `w > 1/128`, otherwise 1);
  `φ₅ = clamp((1 − w)/2 − k·(1 − |w|))`; `φ₆ = φ₅ + w` if `w < 1/128` (negative width = plateau at the bottom), otherwise
  `φ₆ = φ₅`. **With k = w = 0 (all demo LFOs): symmetric triangle** that starts at +1 and is −1 at φ = 0.5.
- **Tension** (`FUN_007886b0`): `a = (21^(|T|/128) − 1)·0.05`, `q = sign(T)·(10¹⁸^a − 1)`; applied to `|W|` with the
  same family of curves as §1.3 and the sign preserved (bit 2 of the *flags* clear, the case for all demos); with
  bit 2 set, applied to `(W + 1)/2` and rescaled. Demos: T = 0 (10 LFOs), 34, 40, 42, 81 (pan) → q ≈ 12, 26,
  34, 1.9·10⁵ (computed). Manual: "Bipolar LFO tension: The LFO shape is mirrored above and below 0" **[DOC]** — which
  bit value corresponds to the label **was not confirmed**.
- **Initial phase and synchronisation:** on (re)start, the phase goes to the midpoint of the rise (`FUN_0060e870`:
  `φ₅ + (φ₇ − φ₅)/2`, which in the symmetric triangle is **0.75 → W = 0 rising**) **plus the phase offset**
  (`+0xE0`, 5th LFO integer in the file, in 2⁻³² cycles; demos: 0 or ±2³⁰ = ±¼ cycle). At transport start
  (§1.2) the LFO is repositioned: **phase = initial phase + (position in *ticks*)·increment** → **LFO locked to the
  song position**. On a *pattern* switch the LFO is not restarted.
- Bit 3 of the *flags* zeroes the increment ("Frozen LFO: The first LFO value is held" **[DOC]**). The phase only starts
  moving after the first segment of `A(t)` (delay = Δt of the 1st point; 0 in the demos).
- **Depth:** `d_L` = 3rd `f32` of the articulator (round 16: envelope depth × `scurve11(Master
  LFO/128)`).
- **Consequences:** pan `= PAN/128 + 2·A·d_L·W(φ)` (e.g. "With You": A = 0.125 → ±0.25); cutoff
  `c = CUT/256 + A·d_L·W(φ)` (A = 0.5 → ±0.5 in `c`).

### 1.6 Input envelope follower (IEF) **[DECOMP + DATA]**

- The IEF maps only enter the computation when `+0x16B8 > 0`; the follower level (`+0x16B4`) is converted into the index
  `round(1024·level)` of the map table (constant `0x85C294` = 1024). Attack/release: round 16 §2.4.
- **Data:** no non-neutral IEF map in the 63 enabled units of the demos (neutral = 0.5 additive / 1.0
  multiplicative) **[DATA]** → **the IEF is irrelevant for the demos**.

### 1.7 The 9 + 1 envelopes and the 14 LFOs of the demos **[DATA `tmp/r20/py/lpenv.py`; Δt in beats]**

| Project (track, slot) | Unit | Target | Points `(Δt; y; tension)` | Reading |
|---|---|---|---|---|
| "Atthar" (20,9), (21,6) | 1 | vol | (0;1) (0.059 / 0.055; 0.08 / 0; +0.285) (0.294 / 0.298; same; 0) (0.647; 1; +0.67) | fast drop, plateau, return — period 1 beat |
| "Atthar" (54,3) | 1 | vol | (0;1) (0.096;0.13;+0.285) (0.154;0.125;0) (0.75;1;+0.67) | same |
| "With You" (26,0), (27,2), (40,1) | 1 | vol | (0;1) (≈0.33;≈0.31;+0.768) (≈0.67;1;+0.438) | 1-beat *pump* |
| "With You" (8,5) | 1 | vol | (0;1) (0.5;1) (0.5;1) | constant 1 (no effect) |
| "Drumloop Masher" (2,3) | 8 | vol | (0;1) (0.25;0; type 5, +1.0) (0;1) | 0.25 beat, type 5 curve (not decoded) |
| "SFX transitions" (2,6) | 1 | vol | (0;1) (0;0) (0.5;1;−0.365) (0.5;1) | cuts, rises over ½ beat, holds ½ |
| "LollieVox" (24,2) | 1 | cut | (0;1) (4;0.5) (0;1) | 4-beat ramp (−0.5·d_E → 0) |
| "Drumloop Masher" (2,3) | 3 | res | (0;0.5) (0;0.5) | null |
| LFO → pan (7): "With You" (8,5) u2, "Use Me" (8,6), "Masher" (2,3) u7, (2,4) u2/u3, "Lightning" (26,3), "Oxygen" (44,4) | | pan | S = 45819/31102/35989/21502/26259/40898 | 1…32 beats |
| LFO → cutoff (7): "Masher" (2,3) u2…u6, (2,4) u2/u3 | | cut | S = 40898/45819/35989/26259 | 2, 1, 4, 16 beats |

---

## 2. Fruity Scratcher — turntable over a sample, added to the input **[DECOMP + DATA + DOC]**

**Where it is:** `Plugins/Fruity/Effects/Fruity Scratcher/` (it is an **effect**, not a generator). Two binaries:
`Fruity Scratcher_x64.dll` (Delphi; plugin class **`TTestPlug`**, VMT `0x7285a8`, child of
`TDelphiFruityPlug`; unit `Scratcher\TestPlug.pas`) and `engine_x64.dll` (C++), which exports only
`createTimecodeDecoder`, `destroyTimecodeDecoder`, `gotSignal`, `getPosition` and `process` — it is the
**timecode decoder (DVS)**, used only in control-vinyl mode **[DECOMP: exports and
`FUN_1800010e0…`; not detailed, irrelevant for the import]**.

**What it is** **[DOC `…/plugins/Fruity%20Scratcher.htm`]**: "vinyl turntable simulator"; a sample is loaded and
the *scratch* is recorded in real time (platter dragged with the mouse, linked to a controller or automated). Controls
mentioned: Smooth 1…4, Pan, Vol, Hold ("Stops the platter from turning … when linking the platter to a MIDI
controller"), Mute, platter, transport "Play Backwards / Pause / Play Forward", SPD (default 1×), ACC, SEN, DVS
(Serato/Traktor/Timecode CD). The manual gives no units.

**Data:** **no demo uses the plugin.** There is 1 blob in the render test
`Data/System/Render tests/Fruity Scratcher.flp` (mixer track 1, slot 0) and 4 identical `.fst` (the default
preset of the plugin database) **[DATA]**.

### 2.1 Parameters (DFM `TSynthEditorForm` Tag = index) **[DFM + DECOMP `FUN_0072a630`, `FUN_00729fe0`, *reset* `FUN_00729f10`]**

`FUN_00729fe0` has the signature (index, value, *flags*) and the *flags* handling of the SDK `ProcessParam`
(inference from its shape); it converts the host value (0…2³⁰, constant 2⁻³⁰) to the control integer and calls
`FUN_0072a630`, which stores the **normalised value** at `+0x2E4 + 4i` (this is what goes into the state) and applies it:

| i | Control (DFM) | Control range (default) | Internal value (normalised v) | Field |
|---|---|---|---|---|
| 0 | Wave position (`WavePosWheel`, no Tag → 0) | 0…30000 | `v` ∈ [0; 1] = position in the sample (see §2.3) | `+0x134` → `+0x130` |
| 1 | Mute | checkbox | **stored inverted**: `ProcessParam` writes `|host − 1|`; internal ≠ 0 → **audible** | `+0x16C` |
| 2 | Turntable speed (SPD) | 0…100 (50) | `v` | `+0x14C` |
| 3 | Turntable acceleration (ACC) | 0…100 (25) | `v` | `+0x150` |
| 4 | Pan | −128…128 (0) | `2v − 1` | `+0x140` |
| 5 | Volume | 0…255 (199; *reset* = 200/255) | `v` (linear gain) | `+0x13C` |
| 6 | Turntable hold | checkbox | boolean | `+0x16D` |
| 7 | Turntable sensitivity (SEN) | 0…100 (50) | `v`; the editor receives `0.0025·v` (`+0x800`) — **the render does not read it** | `+0x148` |
| 8 | Transport | 3-way selector (index 1) | **integer = index − 1** (`ProcessParam` subtracts 1 and the text adds 1) → **−1 / 0 / +1** | `+0x170` |
| 9…12 | Smooth 1…4 | 0…1000 (500, 908, 999, 400) | `v` | `+0x15C…+0x168` |
| 13 | Rotations (`RotationsDummyWheel`) | 0…18 (9) | integer | `+0x174` |

- **Transport:** the render multiplies the target speed by this integer (§2.3) → −1 = backwards, 0 = stopped,
  +1 = forwards **[DECOMP]**; consistent with the manual order "Play Backwards / Pause / Play Forward". The DFM hints
  of the selector say `off / on / inverted` (label divergence; the code rules). Default = index 1 =
  **stopped** (the *reset* sets 0).
- The reader for old versions (< `0xF4628`) stores integers and divides by 100 (SPD, SEN, ACC), 255 (Vol), 1000
  (Smooth) and computes `(p/128 + 1)/2` (Pan) **[DECOMP `FUN_00728d80`, constants `0x7298f0…0x729918`]** — confirms the
  ranges above.

### 2.2 State (`0xD5`) **[DECOMP `FUN_00728d80` (VMT slot 10, read/write) + DATA 5/5]**

| Off | Type | Content |
|---|---|---|
| 0 | u32 | version: writes **`0xF462A`**; the 5 blobs are **`0xF4628`** |
| 4 | 14 × f32 | **parameters 0…13 normalised** (table §2.1; p8 and p13 as integers in *float*; p1 = internal "audible") |
| 60 | i32 n + n B | **sample path in ANSI**; goes through a host call (code 0xE) before loading — in the test blob: `%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav` (macro expansion by the host is an inference) |
| (v > `0xF4628`) | u8 | editor option (`+0x8EC`; used by the *dispatcher* for the window size — inference: "Toggle Scratcher size") |
| (v ≥ `0xF462A`) | i32 + u8 | `+0x204` = timecode index (−1 = off; ≠ −1 enables DVS mode) + 1 *flag* of the input object (`+0x48E`; applied as a filter to the input in DVS mode — that it is the manual's "RIAA Preamp" is an inference) |

- For versions < `0xF462B` the reader still tries to read **56 B** into the integer table `+0x2AC`; none of the 5
  blobs has these bytes (they end at the path) — the effect of this **was not confirmed** (the following loop recomputes the
  UI from `ProcessParam`).
- **Data** **[DATA]**:

  | Source | p0 | p1 | SPD | ACC | Pan | Vol | Hold | SEN | Transp. | Smooth 1…4 | Rot. | Sample |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | Render test (125 B) | **0.111** | 1 | 0.5 | 0.25 | 0.5 (0) | 0.78 | **1** | 0.5 | 0 (stopped) | 0.5 / 0.908 / 0.999 / 0.4 | 9 | DL Breaker.wav |
  | 4 × `.fst` (64 B) | 0 | 1 | 0.5 | 0.25 | 0.5 | 0.78 | 0 | 0.5 | 0 | same | 9 | none (n = 0) |

### 2.3 DSP (`FUN_0072add0`, effect render, VMT slot 14) **[DECOMP]**

**Sample** (`FUN_0072be20`): requested from the host in the format **16-bit PCM, 2 channels, 44,100 Hz** (`WAVEFORMATEX`
structure built in the code); converted to *float* `/32768`; one extra frame is appended at the end for
interpolation; `N` = number of frames. A sample change is applied at the end of a block. Whether the host converts the
file's rate to 44.1 kHz **was not confirmed** (it is the requested format).

**No sample loaded:** the output is the input (copy).

**Sample reading (common to both modes):** position `φ` in "turns" — **1 turn = the whole sample**; the
fractional part is used (the sample **wraps around / *loops***); index `x = frac(φ)·N`, frame `⌊x⌋` (rounding
by `FUN_0072ab70`, *floor* by inference from the construction) and linear interpolation with the next one. Each channel goes
through **`y = (s − s_prev) + 0.99·y_prev`** (`FUN_0072c660`/`FUN_0072c6d0`: DC blocking; deduction: cutoff ≈
`0.01·fs/2π` ≈ 70 Hz at 44.1 kHz) and through a denormal *flush* (< 1e−14).

**Volume and pan:** volume target = `Vol` (or 0 if Mute), smoothed per sample `g ← 0.99·g + 0.01·target`; pan
`p = 2v − 1` smoothed the same way. **Linear balance:** `p ≤ 0` → L·1, R·(1 + p); `p > 0` → L·(1 − p), R·1.

**Output = input + processed sample** (the signal entering the slot passes with gain 1; the Scratcher **adds**).
Exception: in DVS mode (timecode on) the output is only the sample, without the input.

**"Motor" mode (Hold off, no timecode)** — plays the sample like a turntable:

- target speed per sample **`s = (2/(N − 1))·SPD·T`** (T = −1/0/+1 from the transport; `+0x1A8 = 2/(N−1)`):
  **SPD = 0.5 → 1 frame per sample (1×)**; the law is linear, **rate = 2·SPD** (0…2×), with no compensation for the host
  rate in the code read;
- inertia: **`v ← a·(v − s) + s`** with **`a = 0.99999 − 0.0005·ACC`** (time constant `1/(1 − a)` samples:
  ACC = 0 → 100,000; 0.25 → 7,407 ≈ 0.17 s at 44.1 kHz; 1 → 1,961 ≈ 44 ms);
- `φ ← φ + v`, `φ` kept in (0; 1]; the position is published to p0 (the *idle* `FUN_0072a890` pushes `+0x130`
  to parameter 0 with the UI *flag*). **In this mode automating p0 has no effect** (p0 is overwritten every
  sample) — deduced from the flow.

**"Scratch" mode (Hold on, no timecode)** — the position follows parameter 0 (the platter):

- target `P = p0 + (Rot − 9)` (Rotations adds whole turns, −9…+9); error `e = P − φ`;
- `v ← c₂·v + ((1 − c₂)/(n₁ + 250))·e`; `d = S₄·v + (1 − c₃)·(1 − S₄)·e`; `φ ← φ + d`;
- **`n₁ = ⌊2500·S₁⌋`, `c₂ = 0.99 + 0.0099·S₂`, `c₃ = 0.99 + 0.00999·S₃`** (S = Smooth 1…4; defaults →
  n₁ = 1250, c₂ ≈ 0.99899, c₃ ≈ 0.99998, S₄ = 0.4); p0 is read at the start of each block (`+0x130 ← +0x134` at the end of the
  previous block).
- Consequence (deduction): with **constant** p0 the position converges and the sample stops at one point → the DC blocker
  brings the sample output to **zero**; there is only sound while p0 is moving.

### 2.4 Use in the render test and what is needed to reproduce it **[DATA + DECOMP]**

- The test `.flp` has **Hold = 1, transport stopped, p0 = 0.111** and, in pattern 1, a control event
  `0xDF` with **259 records** `{u32 tick, u32 controlID, i32 value}` (format from round 2 §2.7), all with
  `controlID = 0x20408000` = `(1 << 22) + (0 << 16) + 0x8000 + 0` → **track 1, slot 0, parameter 0 (Wave
  position)** in the encoding `track<<22 + slot<<16 + 0x8000+k` (the high nibble here is **2**, not 7 as in `0xE1`;
  the difference **was not decoded**). Values: ramp **4 → 131 over 192 ticks and back to 0 at 384 ticks** (PPQ =
  96 → 4 beats). The scaling of these values to p0 **was not confirmed**. No automation clip (`0xEA`).
- In other words, the "typical" use seen is: **reference sample + platter automation (p0) with Hold on**. A static
  use only produces sound in motor mode (transport ±1): the sample *looping* at `2·SPD`×, with inertia, added to the
  input.

---

## 3. Patcher — the demo graphs that LYNX does not cover yet **[DECOMP + DATA]**

**What LYNX does** (`src/core/FlPatcher.cpp`): (a) pure series chain on bus 0, gains 1; (b) "branches":
a series section starting from IN, then branches that are simple chains up to OUT, starting from IN, from a node with several
outputs or from the bands of a Frequency Splitter/Stereo Shaper; *sidechain* inputs ignored. The cable class is
read from byte 24 of record `1300`.

### 3.1 How the Patcher mixes cables **[DECOMP `FUN_0079bf20`, `FUN_0079f0b0`, `FUN_00952440`, `FUN_0079ca50`, `FUN_0079b790`, `FUN_0079ef40`, resolver `FUN_005da820`]**

- **Sum with linear gain (confirmed):** for each audio input port, the 1st active cable writes
  `destination = source × gain` and each following cable does **`destination += source × gain`**; with no active cable the *buffer* is
  **zeroed**. The three routines are from Intel IPP, resolved by name in `FUN_005da820`: `ippsMulC_32f`
  (pointer `0x9F30B8`), `ippsAddProductC_32f` (`0x9F3320`) and `ippsZero_32f` (`0x9F2F28`) — the semantics are the
  documented ones for those IPP functions (inference from the name); the length is `2·samples` (interleaved stereo).
  `gain` = cable field `+0x18` (created as 1.0 in `FUN_0079ef40`). **This closes the [strong HYP] of round 14 §1.5.**
- **Disabled cable:** cable byte `+0x1C` ≠ 0 makes the cable be ignored in the sum. When reading the file
  (`FUN_00952440`) that byte = **bit 0 of the *flags*** (offset 20) of record `1300`; **bit 1** says whether the class
  byte (offset 24) is valid (converted by `FUN_0094a150`, not read) — **without bit 1, the class must come from the port**
  (`FUN_0079b790` then looks up the connection through the ports).
- **Delay compensation:** if the accumulated latency of the source is lower than that of the destination, the cable goes through a
  delay line of the difference (`FUN_0079f0b0` → `FUN_0079f480`/`FUN_0079f350`) before the sum.
- **Data:** in the mixer slots, 571 cables have *flags* = 2 (valid class) and **203 have *flags* = 0**, of which
  **128 are control cables** (class 2 port) with class byte = 0 — LYNX currently reads them as audio. No
  demo cable has bit 0 (disabled); in the `.fst` there are 6 **[DATA `tmp/r20/py/pat.py`]**.

### 3.2 Which graphs are left out **[DATA: faithful port of the `FlPatcher.cpp` classifier in `tmp/r20/py/pat.py`]**

Of the 30 mixer-slot Patchers in `blobs14.pkl` (24 in demos + 6 in *templates*): **series 4, branches 12, not
covered 14** (12 distinct blobs, 7 topologies; the 4 channel Patchers have no audio IN/OUT — they are generators). Using the
port class (§3.1) the result is the same. After **removing the nodes outside every IN→OUT path** (they only feed
controllers/analysers) and reducing the graph by **series** (node with 1 input and 1 output) and **parallel** (cables with the
same endpoints), **all 14 reduce to a single series-parallel expression** **[DATA `tmp/r20/py/sp.py`]**.
Notation: `→` series, `‖` summed parallel, `×g` cable gain, `out1` = bus 1 of the node.

| Project (track, slot) | Expression IN ⇒ OUT | Nodes outside the path | Why LYNX does not cover it |
|---|---|---|---|
| "Bien Duro" (2,4), (3,4), (9,3), (12,2) (40,178 B; (3,4) = (12,2)) | `Limiter "Limiter 2" → ( PEQ2 "detector" ‖ Fruity parametric EQ "EQ 3" → PEQ2 "main" )` | `IN → Fruity parametric EQ → Limiter → 2 × PEQ2 → 2 × Peak Controller` (detection branch) + 8 Formula, 5 X-Y, Control Surface, NoteBook | split after an internal node; dead branch |
| "Exoplanet" (101,0) | `Stereo Shaper "Mid Side Splitter" → ( out1 → Reeverb 2 ‖ Reeverb 2 ) → Maximus` | 2 Limiters "MID"/"SIDE" with no input cable | join before the last node; output-1 branch |
| "With You" (1,0) | `( Stereo Shaper "Side" → PEQ2 "Side EQ" ‖ Stereo Shaper "Mid" → PEQ2 "Mid EQ" )` | Control Surface | Stereo Shaper in the middle of the branch is treated as a splitter |
| "Stroll Up" (1,6) (103,210 B) | `( PanOMatic "dryin" ‖ Stereo Shaper "IN" → ( 4 × [PEQ2 "bandN" → Soft Clipper → PEQ2 "bandNp" → PanOMatic "bNout"] ) → Stereo Shaper "OUT" → PanOMatic "wetin" )` | X-Y-Z, Control Surface | nested; Stereo Shaper as an ordinary node |
| "Stroll Up" (1,2) (451,240 B) | same outer shell; each band N = `PEQ2 "bandN" → ( PanOMatic "bNdry" → PanOMatic "dryN" ‖ ( ×≈0.5 → PanOMatic → Flangus ‖ ×≈0.5 → PanOMatic → Flanger → PanOMatic ‖ ×≈0.5 → PanOMatic → Chorus → PanOMatic ) → PanOMatic "wetN" → Stereo Enhancer ) → PEQ2 "bandNout" → Limiter → PanOMatic "Nout"`, the 4 bands → PanOMatic "merger" → Stereo Shaper "OUT" | X-Y-Z, Control Surface | 3 nesting levels; in band 1 the Flangus/Flanger/Chorus → sum cables have ×0.333/0.328/0.328 |
| Funk/Jazz/Metal/Rock *templates* (125,1) (28,690 B) | `( PanOMatic "dryin" ‖ PanOMatic "wetin" → Stereo Shaper "IN" → Limiter "Gater" → Stereo Shaper "OUT" )` | 4 X-Y-Z (mappers), NoteBook 2, Control Surface | control cables without bit 1 read as audio; Stereo Shaper as an ordinary node |
| Metal (12,0), Rock (13,0) *templates* (108,377 B) | `( Stereo Shaper → PEQ2 → WaveShaper → ( Stereo Shaper → Convolver → ×0.376 ‖ Stereo Shaper → PEQ2 → Convolver → ×0.199 ) → Delay 3 → ×2 → Delay 3 → ×2 → Stereo Shaper → PanOMatic "wet" ‖ PanOMatic "dry" )` | 6 Formula, X-Y-Z, Control Surface | same + join before the last node + gains ≠ 1 |

Remarks on these graphs **[DATA]**:

- **None uses buses ≠ 0 on the inputs** (no *sidechain*); the only output ≠ 0 used is `out1` of the Stereo
  Shaper in "Exoplanet" (101) — per round 17 §2, the bus of Stereo Shaper parameter 6 carries
  `input − matrix output`.
- In the "dry/wet" cases (Stroll Up, *templates*), the input/output nodes are **PanOMatic and Stereo Shaper used as
  gain/matrix**; their parameters are moved by control cables coming from X-Y-Z/Control Surface. The static
  value to use is the one in the `5005` state of each node (round 14 §1.7: FL stores the current value).
- **"Bien Duro" is a dynamic EQ:** the Peak Controllers measure the detection branch (`IN → EQ → Limiter → PEQ2`) and,
  via Formula/X-Y, move parameters of the PEQ2s on the audio path (PEQ2 "detector": parameters 6, 11, 12, 13; PEQ2
  "main": 12; "EQ 3": 28) — **it has no static equivalent** beyond the stored values.
- The "MID"/"SIDE" Limiters in "Exoplanet" (101) have no input cable: per §3.1 their input is **zeroed**; their
  output goes to OUT (adding whatever a Limiter produces with a null input — presumably silence; **not
  checked**).
- Bit 3 of the node *flags* (`5000`) = 1 on all nodes of "With You" (1,0): meaning **not confirmed**.

### 3.3 Mapping to series/parallel chains (procedure) **[DECOMP §3.1 + DATA §3.2]**

1. Cable class: byte 24 only if bit 1 of the *flags* is set; otherwise the class of the destination port. Ignore
   cables with bit 0 (disabled).
2. Build the audio edges on destination bus 0; discard nodes that are not on any IN→OUT path.
3. Reduce: series (node with one input and one output) and parallel (edges with the same endpoints = **sum**), carrying the
   gain of each cable as an edge factor (`×g`); repeat until only `IN ⇒ OUT` remains.
4. Frequency Splitter / Stereo Shaper only "split" when some edge leaves them through a bus ≠ 0; otherwise
   they are ordinary chain nodes.
5. The 14 cases above reduce completely; a nestable `parallel` container (branches = chains that may contain
   other `parallel`s) with per-branch gain covers all of them.

---

## 4. Vocodex and Fruity Vocoder — state, bands, envelope follower and inputs **[DECOMP + DOC + DATA]**

**What LYNX assumes:** nothing (both are dropped; round 14 §6: "no useful approximation"; round 19 §6b: Fruity
Vocoder layout only by inference). In the LYNX code there is no audio *sidechain* input (searching for "sidechain"
in `src/` only finds a ramp label in `MixerComponent.cpp`) **[DATA: repository]**.

Conventions for this section (declared inferences): in both DLLs `FUN_00408810`/`FUN_0040ad30` = `exp` and
`FUN_00409870`/`FUN_0040bf50` = `ln` (they are called with 2, 11, 101, 10000… and the result matches the data: see the
check of the Vocodex scale in §4.2.3); the runtime globals were read in the initialisation routines
(`FUN_005e2260` in the Vocoder, `FUN_0062f250` in the Vocodex): `ln 2`, `ln 2/2`, `ln 2/12`, `ln 2/1200`, `ln 7`, `ln 11`.
`curve11(x) = (11^x − 1)/10` (exactly 1 when x = 1), the same as in Love Philter.

### 4.1 Fruity Vocoder (class `TTestPlug`, VMT `0x6cf210`, instance `0x15190` B)

#### 4.1.1 State (`FUN_006cf810`, slot 3) — **confirms and corrects round 19 §6b**

| Field | Type | Content |
|---|---|---|
| version | i32 | 2 (written); loads only if > 1 |
| bands | i32 (`+0x15170`) | **N = 4, 8, 16, 32, 64 or 128** (selector = index i, `N = 4 << i`, `FUN_006d4cf0`; labels "4…128" in table `0x723a00`); default 16 (constructor `FUN_006cfb20`) |
| order | i32 (`+0x15174`) | **filter order 1…3** (selector = order − 1, `FUN_006d4d50`; labels "1/2/3" at `0x723a30`); default 2 |
| listen | i32 (`+0x15178`) | **0 = modulator only, 1 = carrier only, 2 = mix** (`FUN_006d4a20` → `FUN_006cfe50`); default 2 |
| swap | u8 (`+0x1517c`) | **modulator/carrier swap** (`SwapModCarSelect`, `FUN_006d5070`) — it is the "extra byte" of round 19 |
| levels | N × f32 | **per-band gain** of the analysis bank (field `+0x144` of each band; reset to 1.0 before reading) |
| parameters | 12 × i32 (`+0x120…+0x14c`) | DFM tags 0…11, raw values |

Data: the 10 blobs (1 demo + 9 `.fst`) follow exactly this order (order = 2, listen = 2, swap = 0) **[DATA]**.
The automation indices are tags 0…11 (`FUN_006d08c0` only accepts index < 12) **[DECOMP]**.

#### 4.1.2 Parameter laws (`FUN_006d03a0`, `FUN_006cff90`) **[DECOMP]**

| Tag | Name | Law |
|---|---|---|
| 0/1 | FreqMin / FreqMax (0…65,536) | **`f = 10 + 159.9·(101^(v/65536) − 1)` Hz** (0 → 10 Hz; 65,536 → 16,000 Hz); if min ≥ max, min = max − 1 Hz |
| 2 | FreqScale (0…128, 64) | **`S = int(10000^(v/128) − 1) + 1`** (0 → 1; 64 → 100; 128 → 10,000; the integer conversion is `FUN_00409d00`, rounding not checked) |
| 3 | Invert | reverses the band order of the carrier bank (§4.1.3) |
| 4 | Formant (−1200…1200) | **cents** added to the carrier bank edges |
| 5 | Bandwidth (5…200, 50) | relative width in % (formula in §4.1.3) and gain normalisation `1500/BW` |
| 6/7 | Attack / Release (10…1000) | **milliseconds**: per-sample coefficient **`c = 0.01^(1/(ms·0.001·fs))`** (pole that decays to −40 dB in `ms`) |
| 8 | Hold | ≠ 0 → freezes the levels (no analysis; uses the last envelope) |
| 9/10/11 | Modulator / Carrier / Wet level (0…160) | **`curve11(v/128)`** (128 → 1.0; 160 → 1.89) — only in listen mode 2; listen 0 → mod = 1, rest 0; listen 1 → car = 1, rest 0 |

#### 4.1.3 Bands and filters (`FUN_006d00b0`, `FUN_005dbb00`, `FUN_005db530`) **[DECOMP]**

- **Edges** (i = 0…N): `f_i = fmin + (fmax − fmin)·((S + 1)^(i/N) − 1)/S` Hz, converted to cents
  `c_i = 1200·log2(f_i/440) + 5700` (`FUN_006cff40`; 0 = C at 16.35 Hz). Band k = [c_(k−1), c_k].
- **Two banks:** analysis (`+0x150`, modulator signal) with the raw edges; synthesis (`+0xA950`) with `c_i +
  Formant` and, with Invert, mirrored index (`N − k`). The band only exists if the centre of **both** banks falls within
  **[10 Hz; 20 000·fs/44 100)**.
- **Centre** = mean of the edges in cents (geometric mean in Hz): `fc = 440·2^(((c_(k−1) + c_k)/2 − 5700)/1200)`.
- **Filter**: RBJ band-pass with 0 dB peak gain (code type 2: `b0 = α/(1+α)`, `b1 = 0`, `b2 = −α/(1+α)`,
  `a1 = −2cos ω/(1+α)`, `a2 = (1−α)/(1+α)`), with **width in octaves** `α = sin ω·sinh((ln 2/2)·BW_oct·ω/sin ω)`
  (global `ln 2/2`; `sinh` = `FUN_0045d4d0` by inference, it computes `exp(x)` and `exp(−x)`) and
  **`BW_oct = (BW/100)·order·(c_k − c_(k−1))/1200`**. The **order = number of identical copies in cascade** (1…3), in both
  banks.
- Example (default: 16 bands, 10…16,000 Hz, S = 100): edges 10, 63, 135, 230, 357, 526, 753, 1054, 1457, 1994,
  2711, 3668, 4944, 6648, 8921, 11,953, 16,000 Hz; centres 25, 92, 176, 287 … 13,829 Hz (computed).

#### 4.1.4 Processing (`FUN_006d0fb0`, slot 7) **[DECOMP]**

1. The stereo input is split: **L = modulator, R = carrier** (`FUN_005e0a60`); with the swap byte, reversed.
2. For each active band (and not in Hold): the modulator goes through the analysis bank cascade; the **follower**
   (`FUN_006d0e70`) computes, per sample, `x = |y|·G_k` and `env ← x + (env − x)·c` with `c = c_att` if `x > env`, otherwise
   `c_rel`; below a denormal threshold the envelope goes to 0 (the band is marked "silent" and skipped).
   **`G_k = level_k · Wet · 1500/BW`** (Wet enters only here).
3. The carrier goes through the synthesis bank cascade of the same band; the output accumulates **`Σ_k car_k · env_k`**
   (`FUN_005df640`).
4. Adds `mod·g_mod + car·g_car` (levels from §4.1.2).
5. **The output is mono: the same sample goes to L and R** (`FUN_005e0aa0`).
- Consistent with the manual: "left channel is the modulator and the right channel is the carrier"; "processes a mono
  input and outputs stereo"; "L and R labels in the MIX section swap the assignments"; bands "4 to 128"
  **[DOC `…/plugins/Fruity%20Vocoder.htm`]**. (The manual calls Release "Decay"; the DFM says "Release".)

#### 4.1.5 Use in the demos **[DATA `tmp/r20/voc/all.py`]**

- 1 instance: "Edlan & Ella Noël – Song For You", track **9 "Crusty Piano"** (counting `0xEC` from 0 =
  Master; the round 14 pickle numbers this same track as 10), fed by the FL Keys channel "Crusty Piano";
  **no other track routes to it**. State: 16 bands, order 2, listen 2, no swap, levels 1.0, FreqMin 0,
  FreqMax 65,536, Scale 64, Formant 0, BW 50, A 20 ms, R 100 ms, Mod 0, Car 0, **Wet 147 (→ 1.47)**.
  In other words: the piano vocodes itself (the piano's L as modulator, R as carrier).
- `.fst` "Mono/Stereo tape simulation" (4 bands, Formant −700, BW 95) and "Outside the club" (Formant −10, BW 142)
  are the only non-default presets.

### 4.2 Vocodex (class `TTestPlug`, VMT `0x84e438`, instance `0x528` B)

#### 4.2.1 State (`FUN_0084efb0`, slot 3) **[DECOMP + DATA]**

| Block | Size | Content |
|---|---|---|
| version | u32 | written `0x00080003` → **8** (high word); old presets 2, 3, 5; loads only if 1 < v < `0x80004` |
| **13 maps** | var. | envelope objects (same format as the `.fnv` in `Data/Maps`): `i32` version (3), `i32`, `i32 n`, n × {`f64` Δx, `f64` y, `f32` tension, `u32` flags}, + 20 trailing B |
| parameters | 20 × i32 (`+0x120…+0x16C`) | DFM tags 0…19 (v < 3: 7 + 12 values, with tag 7 inserted = 58,000) |
| block | 0x24 B (`+0x170`; v4: 0x23, v < 5: −3) | see below |
| (v ≥ 4) | 13 B (`+0x1FC`) | option bytes (always `00 00 00 01 01 00…` in the data; meaning **not confirmed**) |
| carrier tone | `u8 n` + n bytes | name of the internal synthesizer preset (e.g. `"PWM"`, `"Ensemble"`, `"Robot saw"`); empty = none |
| notes | `i32 k` + k × i32 | **MIDI notes latched on the keyboard** of the internal carrier (default: 1 note, 60) |

Block `+0x170` (names from the `TSynthEditorForm` field table and from what the loader writes to each control):
`i32` **Hold mode** (`HoldModeSel`; PEAK/RMS — which value is which **not confirmed**), `i32` **order** (`OrderSelect`
= value − 1; 1…4), `i32` **unison order** (`FatOrderSelect` = value + 1; 0 = off in the data), `i32`
**number of bands** (5…100, default 47), `i32` (`DraftBtn` checked when the field = 0), `i32` **ModIn**, `i32` **CarIn**,
`u8` **L-R encoding**, `u8` MIN TIMES, `u8` contour→MOD, `u8` THREADED, `u8` (set to 1), `u8` contour→NOISE,
`u8` CAR contour, `u8` CONSONANTS contour.

Maps (fixed order from constructor `FUN_0084f730`, names from the manual): 0 per-band gain, 1 per-band pan, 2 gain
offset, 3 modulator noise level, 4 modulator *pass-through*, 5 Hold, 6 Attack, 7 Release, **8 band
distribution**, 9 bandwidth, 10 modulator *pitch shift*, 11 saturation mix, 12 saturation curve
**[DECOMP + DOC `…/plugins/Vocodex_parameters.htm`]**.

#### 4.2.2 Parameters (`FUN_00853e80`; tags = DFM) **[DECOMP]**

| Tag | Name (DFM) | Law |
|---|---|---|
| 0 | Wet (0…2000, 1000) | **`(7^(v/1000) − 1)/6`** (1000 → 1.0; 2000 → 8.0 = +18 dB) |
| 1 | Soundgoodizing (0…1000) | passed to the internal Soundgoodizer (`SG.dll`, parameter 5) — law **not read** |
| 2 / 4 | Mod / Car pass-through (0…16,000) | **`curve11(v/12800)`** (12,800 → 1.0) |
| 3 / 7 | Mod HP / Car noise HP (0…65,536) | 0 = filter off; otherwise **`f = 40·375^(v/65536)` Hz** (up to 15 kHz; 55,000 → 5.78 kHz) |
| 5 | Car LP (0…65,536) | 65,536 = off; otherwise the same law (28,000 → 503 Hz) |
| 6 | Carrier noise (0…12,800) | `0.5·(7^(v/12800) − 1)/6` (×4 with CAR→NOISE contour) |
| 8 | Hold (0…10,000) | window `(501^(v/10000) − 1)·0.5 ms` × map 5 (1666 → 0.91 ms; max. 250 ms) |
| 9 | Attack (0…10,000) | `(501^(v/10000) − 1)·2 ms` × map 6 (1115 → 2.0 ms; max. 1 s) |
| 11 | Release (0…10,000) | `(501^(v/10000) − 1)·4 ms` × map 7 (3500 → 31.2 ms; max. 2 s) |
| 10 / 12 | Attack / Release curve (0…2000) | go through `FUN_005fd490(2, v/1000)` (shape **not confirmed**) |
| 14 | Band width (5…1000) | factor **`0.5·(5^(v/1000) − 1)`** (349 → 0.377) |
| 15 | Flatness (0…100) | `v/100` (spreads the sub-bands of the order) |
| 17 | Mod pitch (−1200…1200) | **cents**: the analysis bank is multiplied by `2^(−v/1200)` |
| 16, 18, 19 | Mod BW, unison pitch/pan | recompute the bands (`FUN_00851dd0`); laws **not read** |

Units in ms are a deduction: the code multiplies by `0.001·fs` and uses the result as a sample count
(attack/release/hold), whose exact meaning (time constant or total time, curve shape) **was not
confirmed**. Maps 5/6/7 multiply by ×0.125…×8 (×1 in the middle) **[DOC]**.

#### 4.2.3 Band distribution (`FUN_00851dd0`, `FUN_0062cb20`, `FUN_0062cd90`) **[DECOMP + DATA]**

- Band k (0…N − 1) reads **map 8 at x = k/(N − 1)**; the y (0…1) becomes Hz through the inverse of the scale of object
  `+0x464`, built in the constructor with `a = 440·2^(−33/12) = 65.41 Hz` and `b = 440·2^(67/12) = 21 096 Hz`:
  **`y(f) = p + (1 − p)·ln(1 + (f − a)/(b − a)·b/a)/ln(1 + b/a)`** for f ≥ a, `y = p·f/a` below, with
  `p = ln 2/ln(2b/a) = 3/28`. The frequency is limited to **≥ 20 Hz**.
- **Check [DATA]:** the default map (`Band distribution - default.fnv` and all blobs) goes from y = 0.1429 to
  0.9918, which by the formula are **82.4 Hz** (`440·2^(−29/12)`, the value the constructor uses for the 1st point) and
  **20,000 Hz** (2nd point). The "Bark scale (24 bands)" map gives 60, 150, 250 … 13,753 Hz (Bark centres). Hence the
  47-band default is a straight line in y between 82 Hz and 20 kHz (middle = 1.28 kHz).
- Width of each band: distance (in log2) to the neighbours, mean of both sides, × map 9 × tag 14 factor ×
  0.25, limited to **[1/60; 8]**; each band has `order` sub-filters, shifted by ±0.5·Flatness·width
  (orders 3/4 with extra corrections); the sub-filter design (`FUN_00626690`) **was not read**.

#### 4.2.4 Inputs (render `FUN_00856350`) **[DECOMP + DOC + DATA]**

- **L-R encoding off:** modulator = the track's own input if **ModIn = 0**; otherwise the side *buffer* is requested from the host
  (host method `+0x120`), falling back to the own input if it does not arrive. Carrier: **CarIn < 0** →
  internal *buffer* zeroed and filled by the internal synthesizer (the `Sytrus_x64.dll` in the `Synthesizer` folder, via
  `TFPForwarder_Vocodex`; manual: "built on the Sytrus synthesis engine"); **CarIn = 0** → own input; > 0 →
  host side *buffer*.
- **L-R encoding on:** the input is split by `FUN_008554b0` (L = modulator, R = carrier according to the manual
  and the DFM *hint* "Left (modulator) / right (carrier) interlaced input").
- Manual: "Value 0: the input uses audio passing through Vocodex's own Mixer track"; "---: external inputs are
  blocked"; with the internal carrier "CAR to ---"; the modulator is analysed in mono (L+R) **[DOC
  `…/plugins/Vocodex_inputs.htm`]**.
- **Which track is side input n** was not read in the host; in the data, the 4 instances with n ≥ 1 match "n-th
  track that routes to the Vocodex track, in index order" (see table) — **inference from the data, not
  confirmed**.

| Demo (track, `0xEC` count) | Feeding the track | ModIn / CarIn | Bands / order | Wet / ModVol | Remark |
|---|---|---|---|---|---|
| "Ookay – Thief" 27 and "NewStuff" 27 | 24 "Main Vox" | 0 / **−1** | 47 / 2 | 2000 / 12,800 | internal carrier `"PWM"`, notes 51, 63, 66, 73 |
| "SeamlessR – Bass Antics" 3 "Voc" | 1 "Mod", 2 "Car" | 1 / 2 | 47 / 2 | 1000 / 0 | map 0 edited; ModPitch −1200 |
| "Olbaid – Harmless Essentials" 71 | 72 "Carrier", 73 "Modulador" | 2 / 1 | 47 / 2 | 1000 / 12,800 | |
| "JuiceBass – Menagerie" 7 | 5 "Carrier", 6 "Modulator" | 2 / 1 | 47 / **4** | 1000 / 0 | maps 0 and 9 drawn |
| "Treben – RawFL" 15 | 2 "GHOSTKICK", 16 "BASS 6" | 0 / 2 | 47 / 1 | 1268 / 13,400 | |
| "Treben – All the same" 27/29/30 | 45 "SC" | 0 / 0 | 14 and 32 / 3 | 1000 / 16,000 | Att = Rel = Hold = 0 |
| "Vocodex demo" 2 "VCX" / template "Vocoder" 2 | 1 "Vocals" / 1 "Voice in" | 1/0 and 0/0 | 47 and 24 (Bark) / 2–3 | ≈1000 | |

### 4.3 Can it be approximated in LYNX? (only what was confirmed)

- **Fruity Vocoder: yes, faithfully** — all the required laws are in §4.1 (edges, cascaded RBJ BP,
  follower with `0.01^(1/(ms·fs/1000))`, `curve11` gains, `G_k = level·Wet·1500/BW`, mono output) and the input is the
  track's own stereo (L/R), with no need for a *sidechain*. The only demo (Song For You) fits this.
- **Vocodex: only partially.** Confirmed: state layout, number of bands, frequency scale (formula
  verified against the maps), Wet/pass-through/HP/LP laws, A/R/H times in ms (by deduction) and input
  selection. Not confirmed: sub-filter design and A/R curve shape, final per-band gain/normalisation,
  Soundgoodizer, unison. Moreover **every use in the demos depends on a *sidechain*** (5 cases) or on the internal
  synthesizer with a Sytrus preset (2 cases), and LYNX has no side input.

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Love Philter: point timing | control *tick* every `N = max(4, 4·round(v·fs_int/44100/4))` samples (v = 64, or 8 with HQ) ≈ 1.45 ms, linear ramp between *ticks*; **with the TEMPO bit (all demos): Δt in beats**; without TEMPO, Δt = 1 → 0.5 s (deduction, not checked) **[DECOMP+DATA]** |
| 1b | Love Philter: *pattern*, *loop*, synchronisation | only the envelope with index PatNum plays (demos: 1); *loop* from point 0 to the last → period `ΣΔt` (1 beat in 8 of 9 gates); at transport start envelopes and LFOs are repositioned at the song position (phase = position mod period) **[DECOMP+DATA+DOC]** |
| 1c | Love Philter: curves | value `y − 0.5·mode`; type 0: `q = sign(t)(1000001^|t| − 1)`, t > 0 → `1 − ((1+q)^(1−x) − 1)/q`, t < 0 → `((1+|q|)^x − 1)/|q|` **[DECOMP]**; type 5 (1 case) not decoded |
| 1d | Love Philter: combination | mode 0 `s·E·d_E` and `s·(1 + 2L·d_L)`; mode 1 `s + E·d_E`, `s + L·d_L`; pan = `PAN/128 + 2·s_pan`; vol = unit gain × `s_vol` **[DECOMP]** |
| 1e | Love Philter: LFO | `L = (y − 0.5)·W(φ)`; period `625/(10001^(S/65536) − 1)` beats (demos: 1, 2, 4, 8, 16, 32); triangle (with *skew*/pulse width/tension), starts at W = 0 rising + offset; locked to the song position **[DECOMP+DATA]** |
| 1f | Love Philter: IEF | no non-neutral IEF map in the demos → irrelevant **[DATA]** |
| 2 | Fruity Scratcher | effect: **output = input + sample** (16 bits/2 channels/44.1 kHz requested from the host, *looped*, DC blocker 0.99, linear Vol, balance pan); state `u32 0xF4628/0xF462A` + **14 f32** + ANSI path (+ u8, i32, u8); motor mode: rate `2·SPD·T` (T = −1/0/+1), inertia `a = 0.99999 − 0.0005·ACC`; Hold mode: position follows p0 + (Rot − 9) through a 2nd-order smoother (Smooth 1…4); demos: **0 uses**; render test automates p0 via `0xDF` **[DECOMP+DATA+DOC]**; `0xDF` scaling, rate conversion by the host **not confirmed** |
| 3a | Patcher: cable sum | `destination = Σ source·gain` (IPP `MulC`/`AddProductC`), no cable → zero; *flags* bit 0 = cable disabled; bit 1 = class byte valid; per-cable delay compensation **[DECOMP]** |
| 3b | Patcher: uncovered graphs | 14 slots (7 topologies); after pruning nodes outside IN→OUT, **all** reduce to nested series-parallel; 128 control cables without bit 1 are read as audio by LYNX **[DATA]** |
| 4a | Fruity Vocoder | state `v2; N; order 1…3; listen 0/1/2; swap u8; N×f32 levels; 12×i32` (confirmed in the reader); edges `fmin + (fmax−fmin)((S+1)^(i/N)−1)/S` (10…16,000 Hz, `S = int(10000^(v/128)−1)+1`); RBJ BP with `BW_oct = BW/100·order·width`, cascade of `order` copies; follower `0.01^(1/(ms·fs/1000))`; `G = level·Wet·1500/BW`; levels `curve11(v/128)`; L = mod, R = car., mono output **[DECOMP+DOC+DATA]** |
| 4b | Vocodex | state `v8; 13 maps; 20×i32; block 0x24 (hold mode, order, unison, bands, ModIn, CarIn, L-R, contours); 13 B; tone name; notes`; bands 5…100 at x = k/(N−1) of map 8 with the scale `a = 65.4 Hz, b = 21 096 Hz` (verified: default 82 Hz…20 kHz); Wet `(7^(v/1000)−1)/6`; pass-through `curve11(v/12800)`; HP/LP `40·375^(v/65536)` Hz; A/R/H `(501^(v/10000)−1)·{2;4;0.5}` ms; inputs 0 = own, −1 = internal synthesizer, n = side **[DECOMP+DOC+DATA]**; sub-band filters and curves **not confirmed** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Love Philter — gates and LFOs:**
   - Volume gate: unit gain × `y(t)`, with `t` = song position in beats **mod `T = ΣΔt`**
     (from the *loop* start point), interpolation by arrival point with the curve from §1.3 (linear if
     t = 0); evaluate every ≈ 1.45 ms with a linear ramp, or per sample (difference ≤ 1 *tick*).
   - Cutoff envelope: `c = CUT/256 + (y − 0.5)·ENV/128`, then the type's Hz law (rounds 16/19).
   - LFO: period `625/(10001^(S/65536) − 1)` **beats** (TEMPO on), symmetric triangle (k = w = 0 in the
     demos) with initial phase ¾ cycle (W = 0 rising) + offset `+0xE0/2³²`, locked to the song position;
     amplitude `(y − 0.5)·d_L`; pan `+2·L`, cutoff `+L` (in `c`). For the 4 pan LFOs with tension ≠ 0, apply the
     tension curve to `|W|` (q from §1.5).
   - Only the PatNum envelope (1 in all demos); ignore the IEF.
2. **Fruity Scratcher:** unused in the demos — import as *bypass* (pass the input through) with a warning, unless
   Transport ≠ 0 with Hold = 0 (then: play the sample from the path in a *loop* at `2·SPD`×, direction from the sign, with
   `y = Δs + 0.99·y`, linear Vol gain and balance pan, **added** to the input). With Hold = 1 the sound depends on the
   p0 automation (do not reproduce without it; constant p0 = sample silence).
3. **Patcher:**
   - Read the cable class from byte 24 **only with bit 1** of the *flags*; otherwise, use the port class. Skip
     cables with bit 0.
   - Prune nodes outside any IN→OUT path and replace the "series"/"branches" heuristics with a recursive
     series-parallel reduction (§3.3) with summing and per-cable gain; treat Stereo Shaper/Frequency Splitter as an ordinary
     node when no edge leaves it through a bus ≠ 0. This covers the 14 remaining slots.
   - Controllers (Peak/Formula/X-Y/X-Y-Z/Control Surface) do not enter the audio; use the stored parameter
     values ("Bien Duro" becomes a static EQ).
4. **Vocodex and Fruity Vocoder:**
   - **Fruity Vocoder:** implement as an insert effect with the algorithm from §4.1.4 (stereo input of the track
     itself: L modulates, R is the carrier, with the swap byte), edges per §4.1.3, RBJ BP in a cascade of `order` copies,
     per-band follower with the A/R coefficients in ms, gain `level·curve11(Wet/128)·1500/BW`, pass-through
     `curve11(Mod/128)`/`curve11(Car/128)` and listen 0/1/2; mono output duplicated to L/R. Import the state with the reader
     from §4.1.1.
   - **Vocodex:** with no *sidechain* in LYNX, import as *bypass* with a warning (as today); log ModIn/CarIn,
     number of bands and the internal tone name. If LYNX gains a side input, §4.2 gives enough for an approximate
     channel vocoder (bands from the §4.2.3 scale, A/R in ms, Wet and pass-through with HP), making it explicit that the
     sub-band shape, the A/R curves and the Soundgoodizer are approximations.
   - Correct the track numbering used in the round 14 pickle when cross-checking with the `.flp` (the tracks with Vocodex/Vocoder
     are 1 below the pickle number when `0xEC` is counted from Master = 0).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| LP `FUN_00856690`, `FUN_00856570`, `FUN_00854e20`, table `0x8D1A08` = {64, 8}, constants `0x856670…0x856770` (96, 44100, 2, 60, 4) | *tick*, free (`2τ`) and tempo (`τ·BPM/60`) scales | DECOMP |
| LP `FUN_00854540`, `TMEnvelope` slot 15 `FUN_00788080` (bit 0 = TEMPO), `FUN_00857990` (event 0 → BPM) | scale selection | DECOMP |
| LP render `FUN_0085aa10` (*tick* loop, X/Y/IEF maps, `+0x169C/+0x16A0`, 1.0 at `0x85C2A0`), `FUN_0062aee0` (ramp), `FUN_00616620` (pan/vol) | evaluation and application | DECOMP |
| LP articulator VMT `0x853B18`: `FUN_008543d0` (2 and 1 at `0x8544C4/C8`), `FUN_008544d0`, `FUN_008541f0`, `FUN_008542e0` | combination, centre, *reset*, loading | DECOMP |
| LP `FUN_0085d050` (PatNum → `+0x10`), `FUN_008579e0` (parameter 5), `FUN_0085cc80`/`FUN_0085cfa0` (sync, query 0x24, 240), event 12 → `+0x16D1` | active *pattern*, sync | DECOMP |
| LP `FUN_00786350`, `FUN_00785f40`, `FUN_00785730`, `FUN_007857f0`, `FUN_00787ba0`, `FUN_00787fd0`, `FUN_00788250`/`FUN_00788300` (*knobs* /128), `FUN_00783ec0` (`0x78411F…0x784196`, 1,000,001), `FUN_0060e3c0`, `FUN_007800c0`/`FUN_00780090` (0.5) | advance, *loop*, centre, curves | DECOMP |
| LP `TMLFO` VMT `0x77FB48`: `FUN_00788410` (+0x18 = 0.5), `FUN_00788b70`, `FUN_007886b0` (21, 0.05, 10¹⁸, 1/256, 1/128), `FUN_00788830` (10001, 2³²/625, 805,306,368), `FUN_00788a20`/`FUN_0060e870`, `FUN_00788aa0`, `FUN_00788540`, `FUN_00788600`, `FUN_0060e450`/`FUN_0060e610` | LFO: rate, shape, phase | DECOMP |
| 43 LP slots in the demos (`tmp/r20/py/lpenv.py`) | TEMPO in all, PatNum = 1, points, tensions, speeds, neutral IEF | DATA |
| Fruity Love Philter manual | *pattern*, notes C5–E6, *Sync pattern changes*, *bipolar/frozen* LFO, IEF | DOC `…/plugins/Fruity%20Love%20Philter.htm` |
| Scratcher `TTestPlug` VMT `0x7285a8`; `FUN_00728d80` (state), `FUN_00729fe0` (`ProcessParam`), `FUN_0072a630` (applies parameter), `FUN_00729f10` (*reset*), `FUN_00728760` (*dispatcher*), `FUN_0072a890` (*idle*), constants `0x7298f0…0x729918`, `0x72a350…0x72a888` | state, parameters, scales | DECOMP |
| Scratcher `FUN_0072add0` (constants `0x72bd48` = 2500, `0x72bd50` = 0.0099, `0x72bd58` = 0.99, `0x72bd60` = 0.00999, `0x72bd80` = 0.01, `0x72bd88` = 0.99999, `0x72bd90` = 0.0005), `FUN_0072c660`/`FUN_0072c6d0` (0.99), `FUN_0072ab70`, `FUN_0072be20` (format 44,100/16/2, `/32768`, `2/(N−1)`) | render, motor/scratch modes | DECOMP |
| Scratcher `engine_x64.dll` (timecode exports) | DVS | DECOMP |
| `Render tests/Fruity Scratcher.flp` (125 B blob, `0xDF` 259 records, `controlID 0x20408000`) + 4 `.fst` | values, p0 automation | DATA |
| Scratcher DFM `TSynthEditorForm` | names, Tags, ranges | DATA (DFM) |
| Fruity Scratcher manual | function, controls | DOC `…/plugins/Fruity%20Scratcher.htm` |
| Patcher `FUN_0079bf20`, `FUN_005da820` (`ippsMulC_32f`, `ippsAddProductC_32f`, `ippsZero_32f`; pointers `0x9F30B8`, `0x9F3320`, `0x9F2F28`) | weighted sum of the inputs | DECOMP |
| Patcher `FUN_00952440`, `FUN_0079ca50`, `FUN_0079b790`, `FUN_0079ef40`, `FUN_0079f0b0` | cable *flags* bits, gain, delay | DECOMP |
| 30 slot Patchers in `blobs14.pkl` (`tmp/r20/py/pat.py`, `sp.py`) | classification, series-parallel reduction, *flags* count | DATA |
| Fruity Vocoder `FUN_006cf810` (slot 3 of VMT `0x6cf210`), constructor `FUN_006cfb20`, `FUN_006d4cf0`/`FUN_006d4d50`/`FUN_006d4a20`/`FUN_006d5070`, tables `0x723a00`/`0x723a30`; 10 blobs | state layout, selectors | DECOMP+DATA |
| Fruity Vocoder `FUN_006d03a0` (0.0078125; 1500; 120,000; 10,000; 44,100; 0.001; 0.01), `FUN_006cff90` (101; 159.9; 10), `FUN_006d00b0` (0.5; 10), `FUN_006cff40`/`FUN_006cfef0` (440; 5700), `FUN_005e2260` (ln 2, ln 11…) | parameter and band laws | DECOMP |
| Fruity Vocoder `FUN_005dbb00`, `FUN_005db530` type 2 (1; −2), `FUN_0045d4d0` | RBJ BP with width in octaves | DECOMP |
| Fruity Vocoder `FUN_006d0fb0`, `FUN_006d0ef0`, `FUN_006d0e70` (via `FUN_006d1a70`), `FUN_005df640`, `FUN_005df500`, `FUN_005e0a60`, `FUN_005e0aa0` | processing, follower, mono output | DECOMP |
| Vocodex `FUN_0084efb0` (slot 3 of VMT `0x84e438`), `FUN_0084f730`, `TSynthEditorForm` field table (`0x85a1f8`); 34 blobs (`vx.py`) | state layout, maps, block `+0x170` | DECOMP+DATA |
| Vocodex `FUN_00853e80` (0.001; 1/12,800; 1/6; 0.5; 375; 40; 65,536; 5), `FUN_0062f250` (ln 2, ln 7, ln 11) | parameter laws | DECOMP |
| Vocodex `FUN_00851dd0` (−33; 440; 20; 501; 0.001; 1/60; 8), `FUN_0062cb20`/`FUN_0062cd90` (2; 1); `Data/Maps/*.fnv` | bands, scale, times | DECOMP+DATA |
| Vocodex `FUN_00856350`, `FUN_008554b0` | input selection, L-R | DECOMP |
| Demos: `0xEB`/`0xEC`/`0xE1` (`tmp/r20/voc/all.py`) | tracks and sends of the instances | DATA |
| Manual: Fruity Vocoder; Vocodex (parameters, inputs, carrier) | names, map ranges, routing | DOC `…/plugins/Fruity%20Vocoder.htm`, `…/plugins/Vocodex_parameters.htm`, `…/plugins/Vocodex_inputs.htm`, `…/plugins/Vocodex_carrier.htm` |

### Open questions

- Love Philter: value of the ramp slope limit (`+0x38` of object `+0xCC`); "free" scale (0.5 s per
  unit) not checked by render; shape of curve type 5 (*pulse*) and the other types; pan law in
  `FUN_00616620`; format of the 0x24 position query to the host; label of bit 2 (bipolar tension) and meaning of
  bit 1 of the LFO *flags*; exact names of host events 0/1/12.
- Fruity Scratcher: scaling of the `0xDF` values (0…131) → p0 and the high nibble 2 of `controlID`; whether the host
  resamples the sample to 44.1 kHz and whether the speed compensates for the project rate; effect of the extra 56 B read
  in versions < `0xF462B`; exact meaning of the UI/timecode bytes.
- Patcher: output of a Limiter with zeroed input ("Exoplanet" 101); meaning of bit 3 of the node *flags*;
  conversion of the class byte by `FUN_0094a150`.
- Fruity Vocoder: whether `FUN_00409d00` rounds or truncates (S of FreqScale); the exact *buffer* that the synthesis bank
  filters is deduced from the flow (the analysis one uses L, confirmed).
- Vocodex: design of the sub-filters (`FUN_00626690`) and of the final width; shape of the A/R curves (`FUN_005fd490`,
  `FUN_00606730`); PEAK/RMS meaning of values 0/1; per-band gain normalisation; internal Soundgoodizer;
  unison (tags 16/18/19); the 13 bytes `+0x1FC`; how the host numbers the side inputs (the "track
  order" rule comes only from the data); state versions < 3 (old presets).
