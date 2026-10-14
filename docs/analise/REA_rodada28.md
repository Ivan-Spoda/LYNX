# Behavioural analysis of FL Studio 2026: round 28 (REA) — Sakura string model and Drumpad mesh model

> **Nature of this document.** Continuation of `REA_rodada18.md` §10/§12 and `REA_rodada19.md` §5. *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched (no FLEX pack was opened; no preset decryption).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical models (which
> implement only what is marked DECOMP/EXEC) are marked **[model]**.

- **Targets** (copies in `tmp/r28/bin/`, hashed in this round):

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Sakura (engine) | `Plugins/Fruity/Generators/Sakura/engine_x64.dll` `e632487ca6419ca7af2c90dbbf73df8ce56ec8fed30989c7ee598de8ae0f1bab` (= rounds 17/18/19); front-end `Sakura_x64.dll` `6087f71cc2e1856984ee9df67ce4c4a242a1002e32836e54f04b515ad2179697` not analysed | C++ (MSVC; `CSakuraSynth`/`CSakuraKey`, Image-Line `CFLWrapper` framework) | `setParameter` `FUN_18002f4c0`, names `FUN_18002eaa0`, voice constructor `FUN_18002d0c0`, **note-on `FUN_18002d510`**, release `FUN_18002d830`, voice reset `FUN_18002d870`, **per-block voice set-up `FUN_18002da50`**, **4-voice SIMD render `FUN_180031960`**, block driver `FUN_1800343e0`, synth render + 48 kHz→host resampler `FUN_180030720`, modulation sum `FUN_18002e470`, master gain `FUN_1800315e0`/`FUN_180031640`, output HP + FX `FUN_18002c700`, resonator bank `FUN_180024f20`, voice allocation `FUN_180031740`, synth constructor `FUN_18002e670`, plugin descriptor static init `FUN_180001230`; CRT: `FUN_180042ef0` = pow(double), `FUN_180044590` = powf, `FUN_180057960` = sincos |
  | Drumpad (engine) | `Plugins/Fruity/Generators/Drumpad/engine_x64.dll` `136e8a49727cfdb6abf3b558e952ce6b143d3d798117f27778c2133c3ccb3933` (= round 18); front-end `Drumpad_x64.dll` `28a0a642ad6aafe3d59fb71588b296934ac42ffa0626bd29228bc7ed921da361` not analysed | C++ (MSVC; `DrumpadSynth`/`DMPadGenerator`, same framework) | names `FUN_180023450`, parameter getter with velocity morph `FUN_1800229f0`, **voice parameter read `FUN_18001f4d0`**, **voice start (mesh weights) `FUN_18001fca0`**, voice reset/sample rate `FUN_1800201c0`, **38 kHz render `FUN_180020460`**, 38 kHz→host resampler `FUN_180020f50`, per-voice pitch/gain `FUN_180022b30`, HP designer `FUN_180018bf0`, RBJ shelf/peak designer `FUN_180018d50`, note allocation `FUN_18001dda0`, synth constructor `FUN_180022690`; CRT: `FUN_1800306a0` = pow(double), `FUN_180031d40` = powf, `FUN_1800217b0` = pow(float, int) |

- **Tools:** Ghidra 12.1.4 *headless* (JDK 21, heap 3 GB) on **new** projects `/tmp/r28gh_sak` and `/tmp/r28gh_drum`
  (imported with automatic analysis). Runners `tmp/r28/g.sh` / `tmp/r28/gm.sh`, scripts in `tmp/r28/scripts/` (round
  26's plus `DumpAll.java`); full decompilations `tmp/r28/out/sak_all.c`, `tmp/r28/out/drum_all.c`; constants read
  from the PE with `tmp/r28/py/kc.py` and `tmp/r18/k.py`.
  **Execution harness** (our own C, Zig 0.14.1 → Windows x64): `tmp/r28/run/plug.c` → `plug.exe`. It
  `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, installs a vectored fault
  handler that ends the process, loads the installation's `engine_x64.dll` unmodified, creates the plugin through its
  exported `CreatePlugInstance` with a minimal fake FL host (all host methods return 0; the path query returns a dummy
  string; the host "compute L/R volume" method returns L = R = 1), sets the sample rate through the plugin dispatcher,
  writes parameters through the engine's own `setParameter`, sends note on/off and controller 121 through the
  engine's own MIDI handlers, sets the polyphony through the engine's own setters, and renders through the plugin's
  `Gen_Render`. It always ends with `TerminateProcess` (DLL finalisation never runs), is run under `timeout`, and no
  process or dialog was left behind. Python driver `tmp/r28/py/runplug.py`.
  **Our models** (pure Python, `python3 -I`): `tmp/r28/py/sakura_model.py`, `tmp/r28/py/drumpad_model.py`;
  comparisons `tmp/r28/py/test_sak.py`, `battery_sak.py`, `test_drum.py`, `battery_drum.py`.
- **Date:** 2026-10-14.
- **Dataset:** `tmp/r14/blobs14.pkl` (Sakura 5 channels, Drumpad 11 + 1 "DrumPad").

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

Manual pages read in this round: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Sakura.htm`
and `…/plugins/Drumpad.htm` (abbreviated `…/plugins/`). Quotes are short and come from a summarised web reading.

**Correction to round 18 §10.** The string damping is `50^(2.57 − 0.8995·a³)·(2/44100)`, **not** `exp(…)`: the CRT
`pow` receives base 50.0 (`0x180077f38`) in the first register **[DECOMP: `FUN_18002da50` @ `0x18002deed`]**.
**Correction to round 19 §5 (open point):** Sakura runs internally at a **fixed 48 kHz** and resamples linearly to the
host rate (§1.10); Drumpad runs at a **fixed 38 kHz** (§2.8).

---

## 1. Sakura — the string voice

### 1.1 Parameter storage (`setParameter` `FUN_18002f4c0`, names `FUN_18002eaa0`) **[DECOMP]**

Index → stored value (x = 0…1). Names in quotes are the engine's own parameter names.

| Idx | Name | Stored as |
|---|---|---|
| 0 | "Master Level" | `10^(4.8x − 1.8)` (`+0x3bb8`) |
| 1 | Tune | `trunc(198x − 99)` clamped ±99, × 0.01 semitone (`+0x3c60`) |
| 2–5 | "Amp Attack/Decay/Sustain/Release" | `35x⁵ + 0.001`, `50x⁵ + 0.001`, `x²`, `10x⁵ + 0.1` (s) |
| 6 / 11 | "Str1/Str2 Decay" | x |
| 7 / 12 | "Str1/Str2 Decay Inv" | flag (x ≠ 0) |
| 8 / 13 | "Str1/Str2 Damp Offset" | x² |
| 9 / 14 | "Str1/Str2 Damp Amount" | x |
| 10 / 15 | "Str1/Str2 Damp Shrp" | x² |
| 16 | "Str Saturation" | x² (used as `100·(x²)³`) |
| 17 | "Str Mix" | x |
| 18 | "Str Stereo Sep" | x |
| 19 | "Comb Ratio" | x |
| 20 / 21 | "Comb Fdbk" / "Comb Fdbk Inv" | √x / flag |
| 22 / 23 | "Comb Level" / "Comb Level Inv" | x / flag |
| 24 / 25 | "Lo-Cut Cutoff" / "Lo-Cut Res" | x / x |
| 26–28 | "Hi-Cut Attack/Decay/Sustain" | x / x / x |
| 29 / 30 | "Hi-Cut Cutoff" / "Hi-Cut Res" | x / x |
| 31 | "Exc Noise Level" | `1 − x` |
| 32 | "Exc Noise Rate" | x |
| 33 | "Hi-Cut Drive" | `1000·x⁴` |
| 34–37 | "EG Attack/Decay/Sustain/Release" | `10x³ + 0.001`, `50x⁵ + 0.001`, `x²`, `50x⁵ + 0.1` |
| 38 | "LFO Rate" | `10x³ + 0.01` |
| 39–62 | "Resonator Frq/Fdbk/On 1…8" | resonator bank (§1.11) |
| 63–77 | Delay / Chorus / Reverb | FX (§1.11) |
| 79–110 | modulation matrix, 8 slots × (source, amount, invert, destination) | read by `FUN_18002e470` (§1.9) |
| 111 | (no name; integer) | string-2 length mode 0…6 (§1.3) |
| 113 | (no name) | octave: `trunc(6x)` → ×1/8, 1/4, 1/2, 1, 2, 4, 8 (§1.3) |

The `.fst/.flp` state is `SSLF` + size + `Skr1` + 146 floats; floats 0…113 are the parameters by index
**[DATA, 5 blobs; round 12]** (floats 114…145 are not parameters: the blobs hold non-parameter bit patterns there).
Manual: "Transpose the main pitch +/- 3 octaves", "Frequency ratio of String 1 to String 2" (S2 Ratio)
**[DOC `…/plugins/Sakura.htm`]** — these are indices 113 and 111.

### 1.2 Architecture **[DECOMP, EXEC]**

Per voice (up to 99, rendered four at a time with SIMD; `FUN_1800343e0`):

```
noise/click source → Lo-Cut (2-pole HP) → Hi-Cut (4-pole resonant LP, enveloped, saturated feedback) = exciter E
E → [string 1: lanes 0 (near) and 1 (far)]  [string 2: lanes 2 (near) and 3 (far)]   (4 delay-line loops)
lane 0 + comb A (fed by lane 1) → string-1 output;  lane 2 + comb B (fed by lane 3) → string-2 output
→ linear L/R mix (Mix, Stereo Sep) × 5 → step-to-sample linear interpolation × amp envelope
→ sum of voices → output HP 32 Hz → resonators / chorus / reverb / delay (if on) → master level × CC7, CC10 pan
→ linear resampling 48 kHz → host rate
```

There is no fractional delay or all-pass: **pitch is set by clocking a fixed integer-length delay line at a
fractional rate** (§1.3), and the voice output is linearly interpolated between the last two loop steps.

### 1.3 Pitch, delay lengths and loop clock (`FUN_18002d510`, `FUN_18002da50`) **[DECOMP, EXEC]**

- Note frequency `f = 220·2^((note − 57)/12)` (note 69 = 440 Hz), then × the octave factor of index 113.
- Base length `N = trunc(48000/f)`.
- Index 111 ("S2 Ratio") chooses the lengths of the string-1 lanes (L1) and string-2 lanes (L2):
  0 → `L2 = ⌊N/4⌋, L1 = 4·L2`; 1 → `L2 = ⌊N/3⌋, L1 = 3·L2`; 2 → `L2 = ⌊N/2⌋, L1 = 2·L2`; 3 → `L1 = L2 = N`;
  4 → `L1 = N, L2 = 2N`; 5 → `L1 = N, L2 = 3N`; 6 → `L1 = N, L2 = 4N`; both clamped to ≤ 48 000, L2 ≥ 1. So string 2
  sounds 4, 3, 2, 1, ½, ⅓, ¼ × the pitch of string 1, always an exact integer ratio of lengths.
- Loop clock: each output sample (48 kHz) advances an accumulator by
  `step = (f·L1/48000)·2^((T_note + tune)/12 + m_pitch/6)` and runs the loops once per integer crossing
  (normally `step ≤ 1`). `T_note` is a per-note pitch offset written by the plugin wrapper from FL's voice
  `FinalLevels.Pitch` (`FUN_180003e60`: `Pitch/100 − note + 60` semitones); `m_pitch` is the modulation sum of
  destination 3 (§1.9; one unit = 1/6 octave = 2 semitones, e.g. pitch bend with amount 1).
- **EXEC check:** A4 rendered by the DLL measures **439.7 Hz** at host rates 44.1 and 96 kHz (the loop filters add
  phase delay that is not compensated); the model reproduces the DLL sample by sample (§1.12).

### 1.4 Exciter (`FUN_18002da50` + `FUN_180031960`) **[DECOMP, EXEC]**

Per 48 kHz sample, per voice:
1. **Source.** A uniform value `u ∈ [0, 1)` from a 32-bit LCG (`s ← s·0x0BB38435 + 0x3619636B`, `u = s·2⁻³²`; one
   global generator, four draws per sample for the four SIMD lanes). Sample-and-hold: the held value `h` is replaced
   by `u` only when `u > θ`, `θ = 0.996·(1 − clamp(Rate + m14))^0.25` (Rate = index 32). Source value
   `x = (1 − L)²·h + L`, with L = index 31 as stored (`1 − x_param`): at **Exc Noise Level = 0 the source is the
   constant 1** (a step at note-on — the "click"); at 1 it is the held noise (manual: "balance between a single click"
   and noise **[DOC]**).
2. **Lo-Cut:** 2-pole high-pass, `f_lc = 2^(9.3·u_lc + 5)` Hz (32 Hz…20.2 kHz), `u_lc = clamp(Lo-Cut + 0.5·m4)`,
   `Q = 1 + 10·Res`, `w = 2π f_lc/48000`: `y = b·(x − 2x₋₁ + x₋₂) − a1·y₋₁ − a2·y₋₂`,
   `b = (1 + cos w)/(2 + sin w/Q)`, `a1 = −2Q·cos w/(Q + sin w)`, `a2 = (Q − sin w)/(Q + sin w)`.
   (This is a cookbook-like design whose b uses `sin w/Q` while a1/a2 use `sin w/Q` doubled relative to the textbook
   form; reproduce it literally.)
3. **Hi-Cut:** four 1-pole stages with coefficient `c = g·E_hc`; stage 1 input `y + r·ẑ₄`, stages 2–4 chained,
   and the 4th stage state is the saturated value `ẑ₄ ← v/(1 + D·v²)` with `v = ẑ₄ + (s₃ − ẑ₄)·c`.
   `g = clamp((Cutoff + 0.5·m5)², 0, 1)`, `r = −10·Res·(1 − g)` floored at −4, `D = 1000·Drive⁴` (round 19 §5, now
   EXEC-verified). Hi-Cut envelope `E_hc` (per sample `E += (target − E)·rate`): attack while elapsed time < `T_a`,
   `T_a = (Attack + m6)²·0.5` s, target 1, rate `0.0002/T_a` (rate 1 if `T_a < 0.001`); then target `Sustain²`, rate
   `0.0002/(3·(Decay + m7)³ + 0.001)`; after note-off target 0, rate 0.002. Exciter `E = ẑ₄`.

### 1.5 The four loops (`FUN_180031960`) **[DECOMP, EXEC]**

Per loop step, for each lane j (lengths L1, L1, L2, L2; damping/decay of string 1 for lanes 0–1, string 2 for 2–3):
1. **Injection (DC-free exciter):** `μ_j += (E − μ_j)/L_j`; inject `d_j = E − μ_j` (a 1-pole high-pass of E whose
   time constant is one period of that lane).
2. **Read** `r_j` = delay output (oldest sample).
3. **Loop filter** (two cascaded 1-pole low-passes with the same coefficient `c_j`, plus a "sharpness" feedback of
   the previous output): `σ_j += ((h_j·z_j + r_j) − σ_j)·c_j`; `v_j = z_j + (σ_j − z_j)·c_j`;
   **saturation** `z_j = v_j/(1 + S·v_j²)`, `S = 100·Saturation⁶`.
4. **Write** `fb_j·z_j + d_j` into the delay.

Laws:
- **Damping coefficient:** `c_near = min(1, 50^(2.57 − 0.8995·a³)·2/44100)`, a = clamp(Damp Amount + modulation);
  `c_far = min(1, (1 − Offset²)·50^(…)·2/44100)` — the far node (lane 1/3) is damped harder by Damp Offset
  (manual: "Damping offset for the far string node" **[DOC]**). a = 0 → c = 1 (no damping); a = 1 → c ≈ 0.0312.
- **Sharpness:** `h = −0.5·Shrp²·(1 − c)` (stored x², so `h = −0.5·x_param²·(1 − c)`).
- **Feedback:** `fb = clamp(Decay + modulation)^0.05`, negated if "Decay Inv" (round 18), smoothed by
  `fb += (target − fb)·0.05` per step (the smoother is re-initialised to the target at every block while the voice
  is sounding, flag `+0x4c`).
- Strings 1 and 2 have separate Decay/Inv/Damp/Offset/Shrp; Saturation is common.

### 1.6 Comb ("pickup") and stereo mix **[DECOMP, EXEC]**

- Two feedback combs: A (length `max(1, trunc(L1·Ratio))`) fed by **lane 1**, B (length `trunc(L2·Ratio)`) fed by
  **lane 3**: per step `out_A = r₀ + λ·combA[k]`, then `combA[k] = combA[k]·φ + r₁` (B likewise with r₂, r₃);
  `φ = ±√Comb Fdbk` (Inv), `λ = ±Comb Level` (Inv). With Level 0 the string output is lane 0 (lane 2) alone.
- Gains (smoothed by 0.001 per step): string 1 `L = (1 − s)·F₁·T_L`, `R = s·F₁·T_R`, `s = clamp(Sep + m2)`;
  string 2 `L = (1 − s₂)·F₂·T_L`, `R = s₂·F₂·T_R`, `s₂ = clamp(1 − Sep + m2)`; `F₁ = (1 − Mix)·10^(2·m1)·0.05`,
  `F₂ = Mix·10^(2·m1)·0.05`. When no matrix slot targets destination 1, `m1 = 1` → **F = 5·(1 − Mix) / 5·Mix**.
  Sep 0.5 → both strings centred; Sep 0 → string 1 left, string 2 right. `T_L/T_R` are per-note gains written by the
  wrapper from the host's "compute L/R volume" of FL's voice FinalLevels (Pan, Vol) (`FUN_180003e60`) — the host law
  itself **not confirmed** (our harness uses 1).
- Voice output per 48 kHz sample: linear interpolation between the previous and the current loop-step output with
  the accumulator's fractional part, × amp envelope.

### 1.7 Amp envelope and voice end **[DECOMP, EXEC]**

Round 18 values confirmed by EXEC: attack target 5.0 at rate `4.6587·10⁻⁶/T_A` per sample, value capped at 1; once
elapsed ≥ T_A (compared in float32) target `S`, rate `8.82/(T_D·48000)`; release target 0, rate `8.82/(T_R·48000)`,
voice killed when time since release > T_R, or after > 1000 consecutive samples with |L|,|R| < 10⁻⁸. Targets and
rates are recomputed every internal block of 48 samples (elapsed time advances by 48/48000 s per block).

### 1.8 Output stage **[DECOMP, EXEC]**

Voices summed → 2-pole HP at **32 Hz, Q 1** (same design as the Lo-Cut, per channel, `FUN_18002c700`) → resonators,
chorus, reverb, delay (each only when switched on) → × master `10^(4.8·x₀ − 1.8)·CC7` (x₀ = 0.375 → 0 dB), CC10 pan
(centre = unity on both sides) (`FUN_1800315e0`, `FUN_180031640`).

### 1.9 Modulation matrix (`FUN_18002e470`) **[DECOMP]**

8 slots, each (source = index 79+4k, amount = index 80+4k, invert = 81+4k, destination = 82+4k). Contribution =
`amount²` (negated if invert) × source; summed per destination. If **no** slot with a non-zero source targets
destination 0 or 1, the sum is **1** (otherwise 0 when unrouted).
- Sources: 1 EG (EG A/D/S/R, indices 34–37), 2 LFO × (sum of destination 0), 3 mod wheel (CC1), 4 pitch bend (−1…1),
  5 channel pressure, 6 note velocity (vel/127), 7 note number × 0.0125, 8 release gate (0 while held, 1 after
  note-off).
- Destinations (as used in `FUN_18002da50`): 0 LFO amount, 1 level (`10^(2·m)·0.05`), 2 stereo sep, 3 pitch (2^(m/6)),
  4 Lo-Cut, 5 Hi-Cut cutoff (×0.5), 6/7 Hi-Cut attack/decay, 8/9/10 string 1/2/both decay, 11/12/13 string 1/2/both
  damp, 14 noise rate.
- The EG and LFO shapes (`FUN_180018910`, `FUN_1800189b0`, global LFO phase in `FUN_180030720`) **not decoded**.
  **[DATA]** all five dataset presets use the matrix (e.g. velocity → level, pitch bend → pitch amount 1).

### 1.10 Internal rate (`FUN_180030720`) **[DECOMP, EXEC]**

The synth renders into an internal buffer in chunks of up to 48 host samples, `ceil(n·48000/fs)` internal samples
each, and reads it by linear interpolation with step `48000/fs`. All coefficients above are therefore evaluated at
48 kHz regardless of the host rate (EXEC: same 439.7 Hz at 44.1/96 kHz; the output is the 48 kHz stream delayed by
one sample at fs = 48 kHz).

### 1.11 Resonators and FX — structure only **[DECOMP]**

Resonator bank (`FUN_180024f20`): 8 resonators, each enabled by its "On"; each is two units in series fed by L+R, the
second at the first's frequency × a constant (`0x180077e98`), outputs alternately weighted towards L/R, then summed
through a filter and added to the dry signal. Frequency/feedback laws, chorus, reverb and delay **not decoded**.

### 1.12 Verification against the DLL **[EXEC + model]**

`py/battery_sak.py`: the DLL rendered through `plug.exe` vs `sakura_model.render(post=True)` (FX/resonators off, no
matrix routing), relative RMS error after a fixed 1-sample lag (the resampler):

| Case | rel. RMS error |
|---|---|
| base (click exciter, decay 0.95, damp 0.3) | 3.4·10⁻⁴ |
| noise rate 0.3 / noise+click mix | 3.3·10⁻⁴ / 2.4·10⁻⁴ |
| Lo-Cut 0.4, Res 0.5 | 1.6·10⁻⁴ |
| Hi-Cut envelope + res / drive 0.7 | 3.0·10⁻⁴ / 4.8·10⁻³ |
| damp amount/offset/sharpness | 1.2·10⁻³ |
| decay inv (both strings) | 4.4·10⁻⁴ |
| saturation 0.8 | 1.4·10⁻³ |
| mix/sep, comb, comb inv | ≤ 1.3·10⁻³ |
| S2 ratio modes 0,1,2,4,5,6 | ≤ 6.6·10⁻⁴ |
| octave ↓/↑, tune | ≤ 1.1·10⁻³ |
| ADSR, release at note-off | ≤ 7.9·10⁻⁴ |

Residuals are float32 (DLL) vs float64 (model) rounding.

---

## 2. Drumpad — the "mesh" voice

### 2.1 Parameters (`FUN_18001f4d0`, names `FUN_180023450`) **[DECOMP]**

Every value is read through `FUN_1800229f0`, which morphs with the voice velocity v′ (§2.2): four slots, index
30+k holds (target index − 1), index 34+k the value at full velocity; `value = (1 − v′)·P[i] + v′·P_alt`.
State layout (round 18): `SSLF` + size + `3PMD` + 128 B name + 38 floats (index order) **[DATA]**.

| Idx | Name | Law |
|---|---|---|
| 0 | (reserved) | noise retrigger flag (x ≠ 0) |
| 1 | (reserved) | MID envelope retrigger count `trunc(3x)` (0…3) |
| 2 | Mallet Amplitude | `A = 10^(−4(1 − x))` |
| 3 | Mallet Decay | per-sample coefficient `κ = (500(1 − x)⁴ + 1)·10⁻⁴` |
| 4 | Mallet Noise | noise share N = x |
| 5 | Mallet Noise RP | noise seed `trunc(x·4294967295)` (used when idx 0 ≠ 0) |
| 6 | Membrane Decay | loop feedback `fb = −x^0.3` |
| 7 | Membrane Cutoff | loop 1-pole coefficient `c = x²` |
| 8 | Membrane Tension | weight-pattern step `0.5·x⁴` |
| 9 | Membrane Phase | weight-pattern start phase x |
| 10 | Membrane Material | weight exponent (m = 2x − 1): `e = 1 + 20m⁴` (m ≥ 0) or `1/(1 + 20m⁴)` |
| 11 | Membrane Size | `size = min(511, trunc(512(1 − x)))` |
| 12 | Membrane Shape | `shape = clamp(trunc(128x), 1, 128)` |
| 13 / 14 | LOW Freq / Res | HP on iff x ≠ 0; `f = 2^(11x + 2)` Hz; `Q = 8x + 1` |
| 15 / 16 / 17 | SOF Freq / Decay / Level | §2.6 |
| 18 / 19 / 20 | MID Freq / Level / Res | `f = 2^(9x + 5)`; `(x − 0.5)·32`; bandwidth `(1 − x)·4 + 0.5` |
| 21 / 22 / 23 | MID Freq Mod / Level Mod / Attack | `(x − 0.5)·32`; `(x − 0.5)·48`; `κ_M = (1000(1 − x)⁴ + 1)·10⁻⁵` |
| 24 / 25 / 26 | HIGH Freq / Level / Res | `f = 2^(4.1x + 8)`; `(x − 0.5)·36`; bandwidth `(1 − x)·2 + 0.5` |
| 27 | Lo-Fi | off if x < 0.01, else `levels = trunc((1 − x)⁵·65536 + 4)` |
| 28 | Hold | `3x³ + 0.001` s |
| 29 | Pitch | `2^(2x − 1)` |

### 2.2 Velocity **[DECOMP + EXEC]**

`v′ = (vel/127)^0.5`: the note-on passes vel·(1/127) to the voice allocation, which raises it to the power stored at
synth `+0x38d8`; the Drumpad synth constructor (`FUN_180022690`) sets it to **0.5** (EXEC: the value read back is 0.5;
the model matches the DLL at vel 30, 100 and 127, §2.9). Velocity has no other effect than the morph slots.

### 2.3 Mallet (exciter) **[DECOMP, EXEC]**

At note start the envelope is set to A; per 38 kHz sample `e ← e + (10⁻⁷ − e)·κ`, then
`exc = ((u − 0.5)·N + (1 − N))·e`, u = LCG value as in Sakura (same constants, per-voice state; reseeded from
index 5 only when index 0 ≠ 0, otherwise it continues from the voice's previous state — initial value not
determined). So the mallet is a decaying exponential pulse (DC part `1 − N`) plus centred noise.

### 2.4 The "mesh": a bank of up to 512 comb loops **[DECOMP, EXEC]**

The voice owns 128 delay "rings" k = 0…127 of `512 − 4k` steps, each step holding 4 lanes; the end-of-ring logic
shifts lane j by j positions, so **lane j of ring k is a loop of length `512 − (4k + j)` samples** — i.e. one loop
for every length 1…512, indexed by `i = 4k + j`. Per 38 kHz sample, for the active rings
(`k₀ = trunc(size/4)` up to `ceil((size + shape)/4)`, max 128):
- read the loop output `r_i`; accumulate `out += w_i·r_i`;
- `λ_i += (r_i − λ_i)·c` (Membrane Cutoff²);
- write `λ_i·fb + exc` (fb = −Decay^0.3: **negative feedback**, so loop i resonates at odd multiples of
  `38000/(2·(512 − i))` Hz).
- **Weights** (rebuilt only when Phase/Tension/Material/Size/Shape change; the loop contents are not cleared at a new
  note): walking i from 511 down to 0 with phase ϕ starting at Phase and advancing by `0.5·Tension⁴` per i (wrapped
  to (0, 1]): `x = 2ϕ − 1` folded into [−0.5, 0.5] (x > 0.5 → 1 − x; x < −0.5 → −1 − x), `w_i = sign(2x)·|2x|^e`
  for `size ≤ i ≤ min(size + shape, 511)`, else 0.
- So **Size** sets the longest active loop (`512 − size` samples, lowest resonance), **Shape** how many consecutive
  shorter loops take part (1…128), **Tension/Phase** the sign/amplitude pattern across loop lengths (a folded
  triangle), **Material** the curvature of that pattern. Manual: "a mesh of inter-connected points"
  **[DOC `…/plugins/Drumpad.htm`]** — in the code the loops are independent; there is no coupling between them
  (**[DECOMP]**).

### 2.5 Filters after the mesh (order) **[DECOMP, EXEC]**

`mesh sum → SOF → LOW → HIGH → MID → clip ±1 → × hold → Lo-Fi`.
- **LOW:** the same HP design as Sakura's Lo-Cut, at fs 38 000 (`FUN_180018bf0`).
- **HIGH:** RBJ cookbook **high shelf**, bandwidth form, `A = 10^(dB/20)` (the cookbook uses dB/40, so the shelf
  gain is 2× the parameter dB — **inference** from the formula), `w = min(3, 2πf/38000)`,
  `α = sin w·sinh(ln2/2·BW·w/sin w)`; enabled when |dB| > 0.01.
- **MID:** RBJ **peaking** (same designer, same A law), redesigned every 6 samples with an envelope `ε` (1 at note
  start, `ε −= ε·κ_M` per sample, re-armed to 1 when < 0.01 up to `trunc(3·P1)` times): gain
  `dB = (1 − ε)·LevelMod + Level`, frequency `(1 + FM·(1 − ε))·f` if FM ≥ 0 else `f/(1 − FM·(1 − ε))`, clamped to
  32…12 000 Hz; on when Level ≠ 0.5 or Level Mod ≠ 0.5.
- **Hold:** gain g (1.0 after reset) `g += (1 − g)·0.1` while t < Hold, then `g −= g·0.001` per sample.
- **Lo-Fi:** `trunc(levels·x)/levels`.

### 2.6 SOF (self-oscillating filter) **[DECOMP, EXEC]**

On when Level ≠ 0. Three 1-pole stages + output stage, coefficient
`γ = c₀/((1 + 333.33·c₀) − 333.33·c₀·ε_S)`, `c₀ = clamp(6/38000·2^(9.3·(0.5·Freq + 0.1) + 5))`, sweep envelope
`ε_S` from 1 down by `ε_S·(62.5·Decay⁴ + 1)·10⁻⁵` per sample (manual: "Down-sweep starting frequency/time"
**[DOC]**). Input `a = x − 100000·y₄`, saturated `a/(0.005|a| + 1)`, stages chained with γ, `y₄ += (s₃ − y₄)·γ`;
output `x + Level⁸·0.5·y₄(previous)`. The −10⁵ feedback makes it very sensitive to arithmetic precision: the model
matches the DLL only when the SOF is evaluated in float32 (§2.9).

### 2.7 Pitch and output **[DECOMP, EXEC]**

The 38 kHz stream is read by linear interpolation with step `(38000/fs)·Pitch·2^(clamp(12·bend + T_v, −12, 12)/12)`
(`FUN_180020f50`, `FUN_180022b30`), × the per-voice L/R gains (from the host's "compute L/R volume", smoothed by
0.001). `T_v` = the wrapper's per-note pitch value (`FinalLevels.Pitch/100 − note + 60`). **The note number itself is
not used by the DSP**; whether FL's `FinalLevels.Pitch` makes `T_v` follow the key is **not confirmed** (it depends
on the host convention, which was not analysed; FL SDK references found on the web do not state the unit).
EXEC with the harness (`T_v = 0`): output gain 1 (the synth-level 0.5·CC7 function `FUN_18001e300` is not on this
path), output identical in L and R.

### 2.8 Internal rate **[DECOMP, EXEC]**

`0x18005c738` = 38 000: all coefficients use 38 kHz, the time step is 1/38000 (`0x18005c4e8`), and the resampler
step is 38000/fs (EXEC at 44.1 kHz matches).

### 2.9 Verification against the DLL **[EXEC + model]**

`py/battery_drum.py` (`drumpad_model.render` vs `plug.exe`, lag 0):

| Case | rel. RMS error |
|---|---|
| 6 dataset presets (War Drum Lo 01, Hi Bongo 01, Ethnic Tuned 01, Indie Perc 02, DnB Conga 01/02), noise off | ≤ 9.8·10⁻⁵ |
| LOW / HIGH / MID / Lo-Fi / Hold / Pitch / mesh params / mallet | ≤ 8.7·10⁻⁴ |
| velocity 30 / 100 / 127 (morph slots) | ≤ 1.0·10⁻⁴ |
| SOF (float32 model) | 5.1·10⁻⁴, 6.6·10⁻⁵ |
| noise with retrigger seed | 8.0·10⁻⁵ |
| host 44.1 kHz | 7.8·10⁻⁵ |

Not matched: noise **without** retrigger (unknown initial generator state — statistically equivalent only).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| S1 | Sakura rate | fixed 48 kHz internal, linear resampling to host **[DECOMP+EXEC]** |
| S2 | Sakura pitch | integer loop `N = trunc(48000/f)` clocked at `f·L1/48000` per sample; no all-pass; S2 ratio 4,3,2,1,½,⅓,¼ by integer lengths **[DECOMP+EXEC]** |
| S3 | Sakura exciter | click (constant 1) ↔ sample-and-hold noise (θ = 0.996(1 − Rate)^0.25); 2-pole HP 2^(9.3u+5) Hz; 4-pole LP g = Cutoff², r = −10·Res(1 − g), drive 1000x⁴; per-lane DC-blocked injection **[DECOMP+EXEC]** |
| S4 | Sakura loop | 2× 1-pole LP c = min(1, 50^(2.57 − 0.8995a³)·2/44100), far lane × (1 − Offset²); sharpness −0.5x²(1 − c); saturation v/(1 + 100x⁶v²); feedback ±Decay^0.05 **[DECOMP+EXEC]** |
| S5 | Sakura pickup/mix | out = near lane + Level·comb(far lane, length Ratio·L, fb √Fdbk); L/R linear pan by Sep; ×5·(1 − Mix)/5·Mix **[DECOMP+EXEC]** |
| S6 | Sakura output | HP 32 Hz Q 1, master 10^(4.8x − 1.8) **[DECOMP+EXEC]** |
| S7 | Sakura matrix | 8 slots, amount², sources/destinations listed; EG/LFO shapes **not confirmed** |
| S8 | Sakura resonators/FX | structure only; laws **not confirmed** |
| D1 | Drumpad rate | fixed 38 kHz internal, linear resampling **[DECOMP+EXEC]** |
| D2 | Drumpad exciter | e = 10^(−4(1−x)) decaying by (500(1−x)⁴+1)·10⁻⁴ per sample; DC (1 − N) + noise N **[DECOMP+EXEC]** |
| D3 | Drumpad "mesh" | ≤ 512 independent comb loops of length 512 − i, LP x², feedback −Decay^0.3, folded-triangle weights (Phase/Tension), exponent (Material), range (Size/Shape) **[DECOMP+EXEC]** |
| D4 | Drumpad filters | SOF → LOW HP → HIGH shelf → MID peak (enveloped) → clip → hold → lo-fi **[DECOMP+EXEC]** |
| D5 | Drumpad pitch | ×2^(2x−1), bend ±12 st, host per-note value; **no key tracking in the DSP**; host value semantics **not confirmed** |
| D6 | Drumpad velocity | v′ = √(vel/127), only through the 4 morph slots **[DECOMP+EXEC]** |

---

## 3. For LYNX

Reference implementations (verified against the DLL): `tmp/r28/py/sakura_model.py` (`render`) and
`tmp/r28/py/drumpad_model.py` (`Voice.tick`, `render`). Both are per-voice and can be ported 1:1 to C++.

1. **Sakura (`synthFromSakura`, new physical voice instead of saw + triangle).** Per voice, run at 48 kHz (or at the
   host rate with the same per-sample constants and a 48 kHz→fs linear resampler if exact timbre is wanted):
   - note-on: `f = 220·2^((note − 57)/12)·oct(P113)`, `N = trunc(48000/f)`, lengths by P111 (§1.3), comb lengths
     `trunc(L·P19)`, zero all buffers; `step = f·L1/48000·2^(tune/12)`;
   - exciter of §1.4 (sample-and-hold LCG noise / click, HP, 4-pole LP with envelope/res/drive);
   - four loops of §1.5 with damping `min(1, 50^(2.57 − 0.8995a³)·2/44100)`, far lanes × (1 − Offset²),
     sharpness `−0.5·Shrp²·(1 − c)`, saturation `v/(1 + 100·Sat⁶·v²)`, feedback `±Decay^0.05`, injection `E − μ`,
     `μ += (E − μ)/L`;
   - combs and linear stereo gains of §1.6 (× 5, Mix, Sep); linear interpolation between loop steps; amp ADSR of
     §1.7; sum → 2-pole HP 32 Hz Q 1 → × `10^(4.8·P0 − 1.8)`.
   - Implement at least the matrix default (destination 1 = 1 → gain 5) and, if possible, the routings velocity →
     level (`10^(2·amount²·vel)·0.05`) and pitch bend → pitch (`2^(amount²·bend/6)`), which all dataset presets use;
     EG/LFO destinations need the undecoded shapes (log the approximation).
   - Resonators, chorus, reverb, delay: keep off and log (not decoded).
2. **Drumpad (`drumpadParams`, new voice instead of the name-based drum).** Per voice at 38 kHz: mallet of §2.3,
   comb bank of §2.4 (512 loops of length `512 − i`, only `size ≤ i ≤ size + shape`, weights computed at note-on),
   SOF (in float32), LOW HP, HIGH shelf and MID peak (RBJ with `A = 10^(dB/20)`, redesign MID every 6 samples),
   clip ±1, hold gain, lo-fi; read with linear interpolation at `step = 38000/fs·2^(2·P29 − 1)` (plus pitch bend);
   velocity morph with `v′ = √(vel/127)`. Do **not** add key tracking (the DSP has none; host-side per-note pitch not
   confirmed). Keep the loop contents across notes of the same voice if retriggering a voice (the code does not clear
   them).
3. Remove the STATUS.md "Still approximated" items for these two plugins only after the C++ port matches the Python
   reference (same parameters, note 60, 48 kHz) to about −60 dB.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Sakura `FUN_18002f4c0`, `FUN_18002eaa0`, `FUN_18002f490` | parameter storage and names | DECOMP |
| Sakura `FUN_18002d510` (+ `0x180077f48` 57, `0x180077f60` 220, `0x180077fa0` 48000, octave table) | note frequency, lengths, S2 modes, comb lengths, step base, injection coefficient 1/L | DECOMP |
| Sakura `FUN_18002da50` (pow base 50 @ `0x18002deed`, 0.25 @ `0x18002db96`, 0.05 @ `0x18002dfc2`) | per-block laws: step, noise threshold, Lo-Cut, Hi-Cut env/g/r, damping, decay, gains, envelopes | DECOMP |
| Sakura `FUN_180031960` | exciter, ladder, loops, combs, mix, interpolation, envelope, silence counter | DECOMP |
| Sakura `FUN_1800343e0`, `FUN_180030720`, `FUN_1800315e0`, `FUN_180031640`, `FUN_18002c700`, `FUN_180019d50` | voice grouping/kill, 48 kHz internal + resampler, master gain, output HP 32 Hz | DECOMP |
| Sakura `FUN_18002e470`, `FUN_180007a20…7b30` | modulation matrix; MIDI handlers (pitch bend slot 3, CC slot 4) | DECOMP |
| Sakura wrapper `CreatePlugInstance`, `FUN_180002360`, `FUN_180002a40`, `FUN_180004810`, `FUN_180004980`, `FUN_180003e60` | FL plugin interface; per-note tables from VoiceParams | DECOMP |
| Drumpad `FUN_18001f4d0`, `FUN_1800229f0`, `FUN_180023450` | parameter laws, velocity morph, names | DECOMP |
| Drumpad `FUN_18001fca0`, `FUN_180020460` | ring allocation, weights, mallet, loops, SOF, filters, hold, lo-fi | DECOMP |
| Drumpad `FUN_180020f50`, `FUN_180022b30`, `FUN_1800201c0`, `0x18005c738` | 38 kHz, resampler, pitch, L/R gains | DECOMP |
| Drumpad `FUN_180018bf0`, `FUN_180018d50`, `FUN_18001dda0`, `FUN_180022690` | HP and RBJ designers; velocity power 0.5 | DECOMP |
| `plug.exe` + `battery_sak.py`, `battery_drum.py` | DLL output vs models (tables §1.12, §2.9) | EXEC |
| `blobs14.pkl` Sakura/Drumpad | state layouts, matrix use, test presets | DATA |
| `…/plugins/Sakura.htm`, `…/plugins/Drumpad.htm` | control descriptions quoted | DOC |

### Open questions

- Sakura: EG and LFO shapes; resonator, chorus, reverb and delay laws; FL host "compute L/R volume" law.
- Both: semantics of FL's `FinalLevels.Pitch` (decides whether Drumpad follows the key through the host value).
- Drumpad: initial noise generator state when the noise retrigger is off.
