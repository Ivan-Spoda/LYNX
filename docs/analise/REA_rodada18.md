# FL Studio 2026 behavioural analysis: round 18 (REA) — scales LYNX still assumes (Harmor, Sytrus, Effector, Love Philter, send routing in the host/Patcher, GMS, Kepler, Toxic Biohazard, BassDrum, Sakura, BooBass and Drumpad)

> **Nature of this document.** Continuation of `REA_rodada17.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode was copied: what is here are formulas and
> behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> track the evidence. No encrypted content was touched (no FLEX pack was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where there
> was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> (e.g. "this field is the sample rate because it is written by the rate routine") are spelled out in the text.

- **Targets** (copies in `tmp/r18/bin/`; those already analysed were checked by hash against the installation):

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Harmor | `23673ad8…499d5aba` (= round 11) | Delphi | `ProcessParam` `FUN_00879540` (VMT+6), filter range `FUN_00874240`, constants `FUN_0089a4d0`/`FUN_00630170`, part volume `FUN_00873b00`, voice `FUN_0088ab10`, per-voice filter `FUN_00886eb0`, articulator `FUN_00898200`/`FUN_00897d90`, filter shapes `FUN_00874ed0`, state `FUN_0086ed30` |
  | Sytrus | `d52a97c5…8e944f15` (= round 11) | Delphi | setup `FUN_0086dd40`, operator table `FUN_0086ace0`, operator render `FUN_00870580`/`FUN_00877270`, loop `FUN_00870820`, MOD level `FUN_00872290` |
  | Effector | wrapper `2f538ec9…512f72e5` + `engine_x64.dll` `4bfdc0f6…7dc2071a` | Delphi + C++ engine (`FXPlugin`/`FXInstance`) | `setParameter` `FUN_180017600`, text `FUN_180017240`, processor `FUN_18001b7f0`…`FUN_18001be60`, units per type `FUN_180018c20` → `FUN_180019860` (Distort), `FUN_180019b60` (Crusher), `FUN_180019d60` (Flanger), `FUN_18001a680` (Phaser), `FUN_180019110` (Filter), `FUN_180018de0` (Echo), `FUN_18001a130` (Reverb), `FUN_18001abc0` (Panner); LFO `FUN_180017ec0`; delay line `FUN_1800181c0` |
  | Fruity Love Philter | `b9f11d63…ba2ff547` (project `/tmp/r17gh_LP`) | Delphi | state `FUN_008551e0`, constructor `FUN_00856780`, articulator: VMT `0x853b18` (slot 0 `FUN_008544d0`, load `FUN_008542e0`, *reset* `FUN_008541f0`, envelope/LFO `FUN_008543d0`) |
  | `FLEngine_x64.dll` (host) | `6d9c5ab0…5d9b8e02` | Delphi | `TFruityHost` VMT `0x13141b8` (send = slot 37 `FUN_0132bb50`), side-output table `FUN_00982ac0`/`FUN_00982b10`/`FUN_009827d0`, action `TRegisterSideIOAction` (`FUN_01316a50`), *wrapper* constructors `FUN_012bd800`/`FUN_012bdf90` |
  | GMS | `20c88eba…f5fcec9c` (= round 12) | C++ | render `FUN_1800f7530`, phase offset `FUN_1800ef960`, table read `FUN_1800ef9d0` |
  | Kepler (engine) | `baf37a1d…2afc63ab` (= round 13/17) | C++ | per-voice *setter* `FUN_180022840`, voice `FUN_180023ab0`, global *setter* `FUN_180019a20`, global render `FUN_18001b940`, chorus `FUN_180001a60`, rate `FUN_18001d300` |
  | Toxic Biohazard | `5c326637…f136cb46` (= round 15/17) | C++ | generator `FUN_180038890` (filter table), voice `FUN_180038d00`, cutoff target `FUN_1800395b0`, operators `FUN_180029fa0`, FM routes `FUN_180029cc0`/`FUN_180039b30`, matrix `FUN_18003e730` |
  | BassDrum (engine) | `2fb1b2f8…12c25e8b` (= round 15/17) | C++ | note start `FUN_180028ff0`, render `FUN_180029440`, table `FUN_180028c90`, creation `FUN_18002a63f`/`FUN_180028d50`, `setParameter` `FUN_18002ae90` |
  | Sakura (engine) | `e632487c…ae0f1bab` (= round 17) | C++ (`CSakuraSynth`/`CSakuraKey`) | `setParameter` `FUN_18002f4c0` (VMT `0x1800778b8` slot 23), names `FUN_18002eaa0`, per-block voice `FUN_18002da50`, render `FUN_180031960` |
  | BooBass (engine) | `4e10efcd…db51705f` | C++ (`BooBassPlug`) | `ProcessParam` `FUN_1800023a0`, knobs `FUN_1800040e0`/`FUN_1800041c0`/`FUN_180004240`, sample `FUN_1800042f0`, rate `FUN_1800034f0` |
  | Drumpad (engine) | `136e8a49…3ccb3933` | C++ (`DrumpadSynth`/`DMPadGenerator`) | names `FUN_180023450`, state `FUN_180022d00`/`FUN_180022de0`, note start `FUN_18001f4d0` |

- **Tools:** Ghidra 12.1.4 *headless* (new projects `/tmp/r18gh_FLE` — imported **without automatic
  analysis**, functions created on demand —, `/tmp/r18gh_EffEng`, `/tmp/r18gh_BooEng`, `/tmp/r18gh_DrumEng`;
  earlier projects opened read-only); scripts in `tmp/r18/gh/scripts/` (those from round 17); new
  utilities in `tmp/r18/`: `k.py` (reads constants and constant pointers in a PE), `vmtdump.py`, `vmtname.py`
  (finds the Delphi VMT by class name), `calls.py` (finds direct `CALL`/`JMP` to an address),
  `riprefs.py` (RIP-relative references). Official manual read via web.
- **Date:** 2026-10-13.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abbreviated as `…/plugins/` below). The manual quotations came from summarised web reading; the quoted excerpts
are short and must be re-checked on the page before becoming interface text.

---

## 1. Harmor (100 channels in the demos)

**What LYNX assumes** (`FlSytrus.cpp` `synthFromHarmor`): part = saw; level = `|p11|/1000 × mix A/B`;
volume envelope off = gate `A 2 ms / S 1 / R 0.1 s`; filter cutoff `73.4·2^(7.855·v/8192)` Hz;
envelope amount ±1000 = ±7.9 octaves, centred on level 0.5; filter types: 0/4/13 → LP, 1/16 → BP,
2 → band-reject, 3 → HP, 5…12 → LP; unison `(41^(p38/256) − 1)·1.875` cents.

### 1.1 Volume when the volume articulator is off **[DECOMP]**

- **Articulator evaluation** (`FUN_00898200` builds, `FUN_00897d90` evaluates, per voice): initial output = **base**
  (articulator field `+0x20`), which the code sets to **1.0 when the combination mode (`+8`) ≠ 1** and to
  **0.0 when = 1**. Components only enter a list if they are **on**: envelope (index 0) only if the
  envelope object has `+0x48 ≠ 0`; LFO (index 1) likewise; maps 2…10 enter if they are not constant (constant
  maps are "folded" into the base). Per-component combination: mode 0 → multiply, mode 1 → add, others →
  multiply by `2×value`. The result is smoothed by one pole (`+0xA8`).
- Hence, **with the volume envelope off, it simply does not take part**: the volume articulator output
  stays = base (1.0 in multiplicative mode) × non-neutral maps. **There is no default ADSR nor implicit
  gate** inside the articulator.
- **Use of the output** (`FUN_0088ab10`): the voice's frequency-domain gain is **`(output of art. 1)² × g12`**, where `g12`
  is the "Frequency domain envelope volume" (p12, below). **The level is the square of the articulator output.**
- **End of the voice** (`FUN_0088ab10`, end): the voice is terminated by the volume envelope **only if that envelope
  is on** and has reached the final state (`> 8`); with it off, the voice only ends through the host/voice internal
  *release* state (`+0x214 ≥ 6`). What happens to the level after *note-off* in that case
  (ramp, *declick*) **was not confirmed**.
- **Global mappings 34…37** ("global env A/D/S/R" articulators): at note start, if the outputs of these
  four articulators are not `(1, 1, 0, 1)`, **for every articulator whose envelope is on** the code
  multiplies the envelope's attack, decay and release by outputs 34, 35 and 37 and **adds** output 36 to the
  sustain. They **do not create** an envelope where there is none; they only scale the ones that are on.
- Manual: "Many envelopes need to be activated with the switch in the lower-left corner before articulating the
  target" and "the voice ends when the volume envelope has ended" **[DOC `…/plugins/Harmor.htm`]** — consistent.

### 1.2 Frequency of filters 1/2 (p52/p64, 0…8192) → Hz **[DECOMP]**

- `ProcessParam` (case 52/64, `FUN_00879540`): **`L = lo + v·(hi − lo)/8192`**, in **log2(Hz)** (fields of the filter
  object `+0x80`, `+0x88`, result in `+0xA0`).
- Limits (`FUN_0089a4d0`, with `ln 2` and `log2` initialised in `FUN_00630170`): **`lo₀ = log2(440·2^(−31/12))
  = log2(73.416 Hz)`** and **`hi₀ = log2(440·2^(64/12)) = log2(17 739.7 Hz)`** (span 7.9163 octaves).
- Width correction (`FUN_00874240`, called by parameter 54/66 "Filter width"): with
  `W = (2/3)·2^(0.004·p54 − 1)` and the extensions `eL`, `eR` of the filter shape (non-zero fraction of the shape table,
  ≤ 1, computed in `FUN_00874ed0` only for some shapes), **`lo = lo₀ − max(0, W·eL − 1)`** and
  **`hi = hi₀ + max(0, W·eR − 1)`**. At the default (p54 = 500 → W = 4/3) and with `e = 1`, the range becomes 58.3 Hz…
  22.3 kHz. The `eL/eR` values per shape **were not tabulated** (not confirmed).
- **Per-voice cutoff** (`FUN_00886eb0`): `L_voice = L (smoothed) + KT·(P − log2 130.81) + articulation terms`, and
  **`fc = 2^L_voice` Hz** (the `2^x` is computed right there). `P` = log2 of the voice frequency (`log2 440 +
  (cents/100 + 3)/12`, `FUN_0088ab10`); `KT` = p55/1000 ("Key to filter frequency", default 1000). That is, **the
  knob is in absolute Hz for a 130.81 Hz note** and tracks the keyboard with factor KT.
- **Filter envelope**: articulator 4/5 contributes `2·(hi − lo)·(output)` and, if its envelope is
  on, also `2·(hi − lo)·(p56/1000)·E·m` (E = envelope level; m = output of mapping articulator
  31/32 "F1/F2 env amount"). In "adaptive" mode (p51) the formula is different (interpolation up to the limit). **The
  effective range of E (0…1 or centred) was not confirmed**; therefore the 0.5 centring used by LYNX **is not
  confirmed** and the correct amount factor is **2·(hi − lo) ≈ 15.8 octaves per unit**, not 7.9.

### 1.3 Part volume (p11), p12 and unison (p38) **[DECOMP]**

- **p11 "Time domain volume"** (±2000, `FUN_00873b00`): `x = |p11|/1000`; **`g(x) = 1` if x = 1, otherwise
  `(3^x − 1)/2`**; part gain = `g(MainVol/1000) · g(|p11|/1000) · sign(p11) · k`, where MainVol is
  global 10011 and `k` is the part's field `+0x1C` (not identified). p11 = 1000 → 1.0; 2000 → 4.0 (+12 dB);
  500 → 0.366 (−8.7 dB). LYNX's linear curve `p11/1000` is wrong outside 0 and 1000.
- **p12 "Frequency domain envelope volume"** (0…2000): same curve `(3^(v/1000) − 1)/2` → multiplies the
  `(articulator output)²` of §1.1.
- **p38 "Unison pitch thickness"** (0…256, `ProcessParam` case 38): **`s = (41^(v/256) − 1)·1.875`**; per voice,
  `s × (output of the "unison pitch" articulator)² × distribution offset` is **added to the voice's pitch
  variable, which is in cents** (`FUN_008859a0`, `FUN_0088ab10`). Confirms LYNX's scale (cents);
  the per-voice distribution offsets (`FUN_00873e40`) were not tabulated.

### 1.4 Order of the filter types **[DECOMP + DFM + DATA]**

- The integer stored in the 0x4B B block (bytes 52/56) is copied as is into the filter shape code
  (`FUN_0086ed30` → object `+0x3F8`) and the shape builder (`FUN_00874ed0`) handles codes **0…18**
  (19 shapes = the items of the `F1TypeSelect` combo **without the separators**).
- Checked against the generated shape: 0 = curve that falls at the top (LP), 1 = curve symmetric around the centre (BP), 2 = the
  complement of 1 (BS), 3 = mirror of 0 (HP); 4…7 repeat the pattern with another curve (Crude LP/BP/BS/HP).
- List: **0 Classic LP, 1 Classic BP, 2 Classic BS, 3 Classic HP, 4 Crude LP, 5 Crude BP, 6 Crude BS, 7 Crude HP,
  8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 12 Fin LP, 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP,
  17 Custom 1, 18 Custom 2** (names from the DFM `TSynthEditorForm`). Data: filter 1 = 4 in 552 parts, 0 in 141,
  17 in 54; filter 2 = 18 in 685 parts **[DATA 429 blobs]**.
- Manual: "Low pass, Band pass, Band stop, High pass, Phaser, Custom shape 1 & 2" (high-level order)
  **[DOC `…/plugins/Harmor.htm`]**.
- **Error in LYNX:** 5, 6 and 7 are band-pass, band-reject and high-pass (Crude), not low-pass.

---

## 2. Sytrus (268 channels) — FM unit and RM formula **[DECOMP]**

**What LYNX assumes:** FM with index `4·g` **cycles**; RM treated as AM with the modulator on osc 3.

- **Operator render** (`FUN_00870580`, `FUN_00877270`): the phase `φ` is a `double` in **wavetable
  samples**; the read uses `φ + M·mod` with linear interpolation and mask `N − 1` (N = table length).
  `M` is the operator's FM accumulator (cleared every sample) and `mod` is the level of the operator's **MOD**
  articulator, ramped per block and limited to 0…1 (`FUN_00872290`).
- **Amount scale**: the setup (`FUN_0086dd40`) puts the amount
  `4·g` into the target's FM list (and `g·35/128` for self-feedback), with `g = sign(v)·(11^(|v|/256) − 1)/10`. When the
  target operator's table is (re)built (`FUN_0086ace0`), **every FM amount pointing to it is multiplied
  by N** and the phase is converted from cycles to samples (`φ ·= N`).
- Therefore **the phase deviation is `4·g·out_mod·mod` cycles of the carrier wave** (out_mod = modulator output,
  which already includes its volume). In radians: `8π·g·out·mod`. **LYNX's "4·g cycles" assumption is
  confirmed** (with the MOD factor, which is 1 by default — articulator default not re-read here).
- **RM** (same functions): each RM source `j` multiplies the target's RM accumulator by
  **`(1 − |a_j| + a_j·out_j)`**, with `a_j = g` (without the ×4). On the next sample the carrier is multiplied by
  **`1 − mod + mod·Π_j(1 − |a_j| + a_j·out_j)`**. With a = 1 it is the pure product (ring); with a = 0.5 half the signal
  passes unmodulated. The operators' render order implies a one-sample delay for sources rendered
  after the target.
- Manual: "Ring Modulation is the process by which two input signals are multiplied together" and "if the value
  is negative … the modulation phase will be inverted" **[DOC `…/plugins/Sytrus.htm`]** (no units).

---

## 3. Effector (26 slots) **[DECOMP + DOC]**

**What LYNX assumes** (`FlEffects.cpp` `effector`): type = `round(11·p0)`; each type becomes an internal LYNX
effect with X/Y mapped "approximately"; slot mix = p2; bypass = p1 ≥ 0.5.

### 3.1 Parameters and types

- 12 parameters (`FUN_180017050`): 0 Effect Kind, 1 FX Bypass, 2 Wet Mix, 3 Param X, 4 Param Y, 5 Mod Rate,
  6 Tempo Reduce, 7 Mod Amount X, 8 Mod Amount Y, 9 Mod Shape, 10 Input Level, 11 Output Gain.
- **Type = `trunc(11·v + 0.5)`**: 0 Distort, 1 Crusher (Lo-fi), 2 Flanger, 3 Phaser, 4 Filter, 5 Echo (Delay),
  6 Reverb, 7 Panner (Stereo), 8 Trancer, 9 Stutter, 10 Vowel, 11 Ring (internal names, `FUN_180017240`).
  LYNX gets the index right.
- General: **Input Level** = gain `v²` at the input; **Output Gain** = `10^((v − 0.5)·2.4)` (text `(v − 0.5)·48` dB);
  **Mod Amount X/Y** = `sign·(2v − 1)²`; **Mod Shape** `trunc(2v + 0.5)`: 0 sine, 1 saw `1 − 2φ`, 2 square ±1;
  **Mod Rate** = value from the table `{0.125, 0.25, 0.375, 0.5, 0.75, 1, 2, 4, 8}` (labels "1/8"…"8/1") and
  **period T = value·60/BPM s**; "Tempo Reduce" scales the rate by `(1 − v)²`. Effective X and Y =
  `clamp(X + LFO·ModX)` and likewise Y.
- **Wet Mix** (p2) goes to each unit (`+0xADC`), smoothed per sample (coef. 0.01). Bypass = p1 ≠ 0.

### 3.2 X, Y and mix laws per type

| Type | X | Y | Mix |
|---|---|---|---|
| **Distort** | `d = 0.95X + 0.05`; `k = 2048·d²` (cascaded rational saturation, with a make-up gain computed so that 0.5 comes out as 0.5) | 1-pole low-pass pre-filter with coefficient `(0.9Y + 0.1)⁴`: only the low part is distorted; the rest (input − low) passes clean | `(1 − w)·dry + w·wet` |
| **Lo-fi (Crusher)** | rate: holds each sample for `128X³ + 1` samples | bits: `65536/(2048Y² + 1)` levels (Y = 0 → 16 bits; Y = 1 → 32 levels) | `(1 − w)·dry + w·wet` |
| **Flanger** | delay position `X² (+ LFO·ModX)` in a **5 ms** line | feedback `−0.96·(Y + LFO·ModY)` | **`dry + w·wet`** (delay input inverted) |
| **Phaser** | 6 first-order all-passes per channel, `a = (1 − g)/(1 + g)`, `g = S·(1 + 0.5·X')·(20000 − 50)/fs + 50/fs` (S = smoothed X²), limited to 0.001…0.499 | feedback `0.98·(0.6·Y')^(1/3)` | `dry + w·wet` |
| **Filter** | `s = 2(0.8X + 0.1)`: s ≤ 1 → low-pass, pole coef. `s³`; s > 1 → high-pass, coef. `0.5·(s − 1)³` | resonance `0.7Y + 0.2` (reduced near the top of the LP); in HP `6.2568·Y` | `(1 − w)·dry + w·wet` |
| **Delay (Echo)** | feedback `X²` (also the echo output gain) | echo filter: Y < 0.5 darkens (LP `0.99·2Y + 0.01`), Y > 0.5 cuts lows (HP `0.225·(2Y − 1)`); Y = 0.5 neutral | `dry + w·echo` |
| **Reverb** | `Decay = 0.75·X'³` and reverb level `0.225·X'³ + 0.5` | **`HighDamping = Y'²`** | reverb input = `w·input`; reverb with Dry = 0, ER muted, RoomSize/Diffusion/HighCut = 1, Predelay = 0; **whether the dry is preserved was not confirmed** |
| **Stereo (Panner)** | `p = 2X − 1`: delay of up to **2 ms** on the opposite side (Haas) | how much of the pan becomes a level difference (`Y·p`) | `(1 − w)·dry + w·wet` |

- The **Filter** is a cascade of 4 sections of 8 poles with feedback (extended Ladder) — the curve in Hz depends
  on the rate: with a single pole of coefficient `c`, `fc ≈ −fs·ln(1 − c)/2π` (X = 0 → c = 0.008 ≈ 56 Hz at 44.1 kHz;
  X = 0.25 → c = 0.216 ≈ 1.7 kHz); the cutoff of the full cascade **was not tabulated**.
- **Delay — time:** the echo time **is not X**: it is the Mod Rate period (`T = value·60/BPM`), divided by 2
  until it fits in 1.5 s (1.5 s line), minimum 1.5 ms. **LYNX uses X as time — wrong.**
- Manual: Distort X "Distortion amount", Y "Tone control"; Lo-fi "Bit-depth reduces with increased height",
  "Sample rate reduces as increased to right"; Flanger/Phaser X "Depth", Y "Feedback"; Filter "Left of center =
  Low pass … Right of center = High pass", Y "Resonance amount"; Delay X "Feedback amount", Y "Echo filtering";
  Reverb X "Feedback", Y "Wet level"; Stereo X "Left/Right pan position", Y "Binaural effect level"
  **[DOC `…/plugins/Effector.htm`]**. **Divergence:** in the Reverb the code ties Y to *HighDamping*, not to a
  mix level.

---

## 4. Fruity Love Philter (39 slots) — combination mode and envelope/LFO off **[DECOMP]**

**What LYNX assumes** (`parseLovePhilter`): the `f32` stored in the articulator is a "unipolar" flag.

- **The `f32` is not a mode flag.** The articulator load (`FUN_008542e0`) reads `version` and **12 bytes at `+0x20`**:
  `+0x20` = **base** (initial output value), `+0x24` = envelope depth, `+0x28` = LFO depth.
- **The integer mode (`+8`) is not stored in the `.flp`**: the constructor (`FUN_00856780`) calls VMT method 0
  (`FUN_008544d0`, which writes `+8` and resets) with **mode 1 (additive)** for articulators **0 (pan), 2
  (cutoff) and 3 (resonance)** (masks `0x01` and `0x0C`); the others (1 volume, 4 low/gain, 5 band, 6 high,
  7 waveshaper) stay in **mode 0 (multiplicative)**. The *reset* (`FUN_008541f0`) sets base = **0 in mode 1 and
  1.0 in mode 0**, envelope depth 1.0 and LFO depth 0.5.
- Thus, **"unipolar" (f32 = 1.0) ↔ mode 0 (multiply)** and **"bipolar" (f32 = 0.0) ↔ mode 1 (add)** — but the
  correspondence is per target (fixed in the code); the file value is the base, not the mode.
- **Method `+0x38`** (`FUN_008543d0`): envelope → only if the envelope is on (`+0x48 ≠ 0`):
  mode 1: `output += env·env_depth`; otherwise `output ·= env·env_depth`. LFO → only if on: mode 1:
  `output += lfo·lfo_depth`; otherwise `output ·= 1 + 2·lfo·lfo_depth`. **When off, they do nothing (neutral).**
- Manual: does not describe the combination mode nor the articulators that are off **[DOC `…/plugins/Fruity%20Love%20Philter.htm`]**.

---

## 5. Send to "bus k" (Frequency Splitter / Stereo Shaper) in the host **[DECOMP]**

**What LYNX assumes** (round 17 §1.1/§2): send destination k = Patcher bus k.

- The plugins' send is method 37 (`+0x128`) of the host object. In `FLEngine_x64.dll` the concrete host is the class
  **`TFruityHost`** (VMT `0x13141b8`, child of the abstract `TFruityPlugHost` `0x7b67d0`); slot 37 is
  **`FUN_0132bb50`** (`(sender, k, &buffer)`; bit 0 of the 2nd field distinguishes get/release).
- **Computation of the destination track:**
  - if the plugin's *wrapper* has `+0xA7 = 0` → track = **(wrapper track `+0x258`) + k**, limited to 0…500;
  - if `+0xA7 ≠ 0` → track = **`list[min(k, n − 1)]`**, where `list` is a vector of track indices stored in the
    record of the track where the plugin sits (`track[+0x130 of the wrapper] + 0x128`).
  - `+0xA7` is **0** in the *wrapper* constructor used by channels (`FUN_012bd800`) and **1** in the constructor that
    receives (track, slot) (`FUN_012bdf90`, called from `0x12870e6`) — that is, mixer slot plugins use the
    track's list **[DECOMP; the meaning "track's send list" is an inference]**.
- **Replacement by the "side output":** before returning the track's *buffer*, the host looks in the *wrapper*'s
  `+0x128` table (`FUN_00982ac0`/`FUN_00982b10`) for an entry with **key = k**; if it exists and has its own
  *buffer*, **the send goes to that private *buffer*** instead of the track. Entries are created by
  `FUN_009827d0` (key k, owner) from the action **`TRegisterSideIOAction`** (`FUN_01316a50`; created in the
  host dispatch at `0x1321d01` with the index and flags received from the plugin) and from the enumeration of the
  port's outputs (`FUN_012b6400`, query 0x37 to the plugin; the Fruity plugins examined do not answer that query).
  The entry's *buffer* is only allocated when it has a "consumer" (`FUN_00982c90`).
- **Conclusion:** the side-output mechanism is **indexed by the same k as the send** (confirmed). **That the
  Patcher connects the consumer of key k to output pin k of the node was not read in the code** (the Patcher
  side was not traced) → the equality "k = Patcher bus k" remains **not confirmed in code**,
  although the key-k mechanism and the round 14 data are consistent with it.
- Manual: "Sends are Pre-Fader to the destination Mixer Track" and "destinations won't be available until you have
  created Mixer Track sends" **[DOC `…/plugins/Frequency%20Splitter.htm`]** (consistent with the track's list).

---

## 6. GMS **[DECOMP]**

**What LYNX assumes** (round 12 §1.3): FM = `15·m` quarter-cycles (HYP); envelope output = `e²`.

- **FM is PM with 15·m quarter-cycles — confirmed.** The modulation *buffer* receives `m·15` (constant
  `0x18014d6b8` = 15) in FM mode (`FUN_1800f7530`); the phase offset is
  `int(osc2·m·15·4194303 + 268435392)·256` added to the 32-bit phase (`FUN_1800ef960`), that is,
  **`osc2·15·m·2^30` = osc2·15·m quarter-cycles** (= 3.75·m·osc2 cycles; the constant term is a rounding
  of −2^14/2^32 cycle). 1024-point table (`phase >> 22`) with random *dither* on the index (`FUN_1800ef9d0`).
- **AM:** `osc1·(1 − 3·m·osc2)` (constant 3) — confirmed.
- **Sync:** a 32-bit "master" accumulator adds its increment; **when it wraps around, osc1's phase is
  replaced by the master's value** (the residue after the wrap, not zero). The osc1 increment with `(1 + 3m)` is the
  one from round 12 (not re-read here).
- **Amplifier:** the per-sample amplitude output is **`e²`** (`e` = envelope value) **+** contributions from
  LFO/envelope with volume destination — confirmed in `FUN_1800f7530`.

---

## 7. Kepler **[DECOMP]**

**What LYNX assumes:** LFO→DCO/cutoff depths and chorus rates not modelled.

- **LFO1 → DCO1** (panel k = 10 → internal 0; internal "Range" 1): `A = table(x)` with 11 points
  **{0; 0.00116; 0.00813; 0.01506; 0.02196; 0.02882; 0.05259; 0.07819; 0.12057; 0.17728; 0.21388}** (octaves),
  **×10 if Range ≥ 0.5** (`FUN_180022840`). Into the voice pitch (`FUN_180023ab0`) goes **`2·L₁·A` octaves**
  (`f = 440·2^(1.0005·Σ)·k`), with L₁ = LFO1 value.
- **LFO1 → cutoff** (panel 23 → internal 0x2D): `A_c = table(x)` **{0; 0.0914; 0.38038; 0.662; 0.96846; 1.23732;
  1.48249; 1.85735; 2.27554; 2.66441; 2.82}** octaves; it enters the cutoff exponent as **`2·L₁·A_c` octaves**.
- **Amplitude of `L₁`** (LFO range): **not confirmed** (the pointer to the LFO value in the voice was not traced).
- **Chorus** (`FUN_18001b940` on mode change; processing `FUN_180001a60`; `+0x1BDE0` = sample rate,
  written in `FUN_18001d300`):

  | Mode | LFO | Delay (s) |
  |---|---|---|
  | I | triangle 0…1, **0.4 Hz**, L/R offset by 0.5 cycle | `0.00108 + 0.00415·tri` (**1.08…5.23 ms**) |
  | II | triangle, **0.6 Hz**, L/R 0.5 cycle | same |
  | I+II | **sine, 8 Hz**, L/R offset by 0.0286 cycle | `0.00285 + 0.00052·tri` (**2.85…3.37 ms**) |

  The chorus dry/wet mix was not re-read. Manual: "Emulates the Juno 6 Chorus I/II" **[DOC
  `…/plugins/Kepler.htm`]**.

---

## 8. Toxic Biohazard **[DECOMP]**

**What LYNX assumes:** FM index and cutoff in Hz not modelled (round 17: not confirmed).

- **FM:** each operator has a 32-bit phase; the read uses `phase + int(M·16777215)·256` → **`M` in cycles**
  (`FUN_180029fa0`; 1024-point table with cubic interpolation). The accumulator is reset to 20.0 (an integer
  number of cycles, no effect on the phase). The routes add `M_target += q·out_source` (`FUN_180029cc0`), with
  **`q = 2·cell`** (`FUN_180039b30`) and `cell = (⌊100x⌋/100)²`. Hence **phase deviation = 2·cell·out cycles**
  (β = 4π·cell·|out| rad; `out` includes the operator's envelope and level).
- **Matrix direction** (complement, `FUN_18003e730` from round 17 + `FUN_180039b30`): the *setter* of target
  operator `t` writes parameters `109 + 6j + t` (j = 0…5) into fields `+0x40 + 4j` of `t`'s record; when building
  the routes, for target `t`, source `j` enters if operator `j` is on and cell `+0x40 + 4j` is
  greater than a threshold, with amount `2·cell`. Hence **parameter `109 + 6j + t` is "source j → target t"**
  (0-based) **[DECOMP]**; consistent with the data (modulators with Mix 0 appear as a source) **[DATA]**.
- **Filter cutoff (param 15):** the per-voice target (`FUN_1800395b0`) is
  `u = cutoff + KT·(note − 60)·0.01 + VelAmt·(vel − 1)·0.5 + EGAmt·env`, converted into index `u·65535` of a
  65536-coefficient table built in `FUN_180038890` with **`fc = 2^(9u + 5)` Hz** (u = 0 → 32 Hz;
  0.5 → 724 Hz; 1 → 16,384 Hz). The coefficients are those of the 4-pole "Moog" filter with polynomial tuning
  computed for **fs = 44,100** (`f = 2fc/44100`, `k = 3.6f − 1.6f² − 1`, `p = (k + 1)/2`,
  `scale = e^((1 − p)·1.386249)`). The filter runs in `FUN_180038d00` (4 stages per channel + saturation).
- Manual: 4 modes (Bypass/LP/BP/HP), "Cutoff - Determines the cutoff frequency" (no Hz) **[DOC
  `…/plugins/ToxicBio_filter.htm`]**.

---

## 9. BassDrum **[DECOMP + DATA]**

**What LYNX assumes:** Peak = `base·2^(6·peak)`; click not modelled.

- **N = 1024.** The oscillator table comes from the file **`Plugins/Fruity/Generators/BassDrum/sounddata`**, read at
  engine creation (`FUN_18002a63f`); it is a mono *float* 44.1 kHz WAV with **1024 samples** **[DATA]**; N =
  number of samples (`FUN_180028c90` reads `N·frac(phase)`). Shape: nearly sinusoidal (fundamental 0.855; harmonics
  ≤ 0.02), peak −1/+0.70 **[DATA]**.
- With N = 1024, **`f(t) = base·(1 + 100·x³·s(t)⁴)`** (x = Peak; s = *slide* envelope from round 17); the
  increment uses a fixed `1/44100` (if the engine runs at the host rate, the pitch scales by `fs/44100` — **not
  confirmed**).
- **Click:** p14 FREQ → `f_click = 2^(14x)` Hz (x = 0.5 → 128 Hz; 1 → 16,384 Hz); p15 LEVEL → `x⁵`, amplitude
  `25·x⁵`; the click reads the **same table** from a random phase between 0.75 and 1.0 up to phase 1 (a
  ¼-cycle segment). p13 CUT → stored raw (x); **use not confirmed**.

---

## 10. Sakura **[DECOMP]**

**What LYNX assumes** (round 11 note): Harmless-style ADSR, LP ≈ Hi-Cut.

- **Indices** (`FUN_18002eaa0`): 0 Master Level, 1 (Tune), 2–5 **Amp A/D/S/R**, 6 Str1 Decay, 7 Str1 Decay Inv,
  8 Str1 Damp Offset, 9 Str1 Damp Amount, 10 Str1 Damp Shrp, 11–15 same for Str2, 16 Str Saturation, **17 Str Mix**,
  18 Str Stereo Sep, 19–23 Comb, 24/25 Lo-Cut, 26–28 Hi-Cut A/D/S, **29 Hi-Cut Cutoff, 30 Hi-Cut Res**,
  33 Hi-Cut Drive, 31/32 noise, 34–37 EG A/D/S/R, 38 LFO Rate, 39–62 Resonators, 63–77 Delay/Chorus/Reverb.
- **Amp ADSR** (`FUN_18002f4c0`, voice `FUN_18002da50`, render `FUN_180031960`):
  - Attack: `T_A = 35x⁵ + 0.001` s; target 5.0 with coefficient `0.2236/(T_A·48000)` and output **limited to 1.0** →
    reaches 1.0 in ≈ **T_A** (since `1 − e^(−0.2236) ≈ 0.2`);
  - Decay: `T_D = 50x⁵ + 0.001` s, pole towards the sustain with coefficient `8.82/(T_D·48000)`;
  - Sustain: **x²**;
  - Release: `T_R = 10x⁵ + 0.1` s, pole towards 0 with `8.82/(T_R·48000)`; **the voice is killed after T_R**.
  - The envelope is applied **linearly** (not squared). The code uses 48,000 as the rate in the coefficients and in the
    envelope clock; the behaviour at other rates **was not confirmed**.
- **Tune (1):** `trunc(198x − 99)·0.01` **semitones** added to the note (±0.99 st). Manual: "Transpose the tuning
  +/- 50 cents" **[DOC `…/plugins/Sakura.htm`]** — **divergence** (the code allows ±99 ct).
- **String decay (6):** string 1 feedback = **`d^0.05`** (d = Decay + modulations, 0…1), with sign
  inverted if "Decay Inv". Since the loss is per period, **T60 ≈ 60/(f·(−log10 d))** s (e.g. d = 0.9 → 1,310/f s).
  Separate damping: `exp(2.57 − 0.8995·a³)·(2/44100)` (a = Damp Amount + mods) and `(1 − Offset)·…`.
- **String Mix (17):** **string 1 ∝ (1 − mix), string 2 ∝ mix** (linear), × `0.05·10^(mod·…)` and by the stereo
  separation pan table.
- **Hi-Cut (29/30):** exciter filter coefficient **`g = clamp((Cutoff + 0.5·env)², 0, 1)`**;
  feedback/resonance **`−10·Res·(1 − g)`**, limited to ≥ −4. Hi-Cut envelope: attack
  `(A + mod)²·0.5`, decay with coefficient `0.0002/(3D³ + 0.001)`, sustain `S²`. **The conversion of `g` to Hz was not
  confirmed** (the exciter filter in the render was not decoded).

---

## 11. BooBass **[DECOMP]**

**What LYNX assumes:** Bass = level of a sine 2 octaves below; Mid = level of a saw; Treble = cutoff
`300·2^(4·treble)` Hz.

- **Sound source** (`FUN_1800042f0`): **sample**: a recorded attack (`+0xB0`) *crossfading* into a looped table of
  **16,384 samples** (`+0xA8`), played at `2^(cents/1200)`, × tabulated amplitude curve. It is not an
  analogue oscillator (the origin of the tables was not traced).
- **The three knobs are a 3-band equaliser** (0…65535 → x = v/65535; x < 0.1 → band muted):
  - **Bass:** `10^((12x − 12)/20)` (−12…0 dB) and a second gain `10^((24x − 24)/20)` for the saturated layer;
  - **Mid:** `10^((10x − 10)/20)` (−10…0 dB);
  - **Treble:** `10^((12x − 9)/20)` (−9…+3 dB).
- **Bands:** two 1-pole filters with coefficients `0.025·44100/fs` and `0.155·44100/fs` (`FUN_1800034f0`) →
  low = LP1, mid = LP2 − LP1, high = input − LP2 (cutoffs ≈ **178 Hz** and **1.18 kHz** at 44.1 kHz).
  Output = `low·B + mid·M + high·T`, repeated in a 2nd stage with saturation in the low band, ×3 and a
  *slew* limiter (`HighLimiter`). Manual: "Lets you equalize the low (bass) / middle / high (treble) frequencies"
  **[DOC `…/plugins/BooBass.htm`]**.
- **LYNX's mapping (oscillator levels and cutoff) does not match** BooBass.

---

## 12. Drumpad **[DECOMP + DATA]**

**What LYNX assumes:** only the *patch* name; timbre from words in the name.

- **State** (`FUN_180022d00`/`FUN_180022de0`): `"3PMD"` + **128 B of name** + **38 `float` = parameters 0…37 in
  index order** (size 0x11C = 284 B). In the `.flp` (`SSLF` + size + state) the *floats* start at
  **offset 140** **[DATA: 975 blobs]**. The interface is drawn by the engine (the *wrapper*'s DFM is generic), so
  **there are no DFM Tags: index k is the parameter itself**.
- **Indices** (`FUN_180023450`): 0/1 reserved, 2 Mallet Amplitude, 3 Mallet Decay, 4 Mallet Noise,
  5 Mallet Noise RP, 6 Membrane Decay, 7 Membrane Cutoff, 8 Tension, 9 Phase, 10 Material, 11 Size, 12 Shape,
  13 LOW Freq, 14 LOW Res, 15 SOF Freq, 16 SOF Decay, 17 SOF Level, 18 MID Freq, 19 MID Level, 20 MID Res,
  21 MID Freq Mod, 22 MID Level Mod, 23 MID Attack, 24 HIGH Freq, 25 HIGH Level, 26 HIGH Res, **27 Lo-Fi,
  28 Hold, 29 Pitch**, 30…37 reserved.
- **Confirmed scales** (`FUN_18001f4d0`; the values go through `FUN_1800229f0`, which may apply velocity modulation — not read): **Pitch = ×2^(2x − 1)** (±1 octave; 0.5 = no transposition);
  **Mallet Amplitude = 10^(−4·(1 − x))** (0 dB at 1; −80 dB at 0); Hold = `3x³ + 0.001` (unit not
  confirmed); Membrane Size → integer `⌊512·(1 − x)⌋` (≤ 511); Shape → integer `128x` (1…128). The others
  (decays, mesh and filter frequencies) **were not confirmed** — the model is a physical mesh
  ("a mesh of inter-connected points" **[DOC `…/plugins/Drumpad.htm`]**), with no direct equivalent in a
  3-oscillator synthesiser.

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 2a | Harmor: volume with articulator off | absent component → output = base (1.0 in multiplicative mode) × maps; gain = **output² × (3^(p12/1000)−1)/2**; voice is not terminated by the envelope; mappings 34–37 scale A/D/R (×) and S (+) only of envelopes that are on **[DECOMP]**; level after *note-off* without envelope **not confirmed** |
| 2b | Harmor: cutoff p52/p64 | **`fc = 2^(lo + v(hi−lo)/8192 + KT(P − log2 130.81))`**, lo/hi = log2(73.416)/log2(17,739.7) Hz widened by `max(0, W·e − 1)` **[DECOMP]**; envelope ±`2(hi−lo)·amt` octaves; envelope centring **not confirmed** |
| 2c | Harmor: p11, p38 | p11: `(3^(|v|/1000) − 1)/2` (1000 → 1.0; 2000 → 4.0) × sign × MainVol; p38: `(41^(v/256) − 1)·1.875` **cents** **[DECOMP]** |
| 2d | Harmor: types | code 0…18 = combo without separators (0 Classic LP … 4 Crude LP, 5 Crude BP, 6 Crude BS, 7 Crude HP … 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP, 17/18 Custom) **[DECOMP+DFM+DATA]** |
| 3 | Sytrus FM / RM | phase deviation = **4·g·out·MOD cycles** (confirms LYNX); RM: carrier × `1 − MOD + MOD·Π(1 − |a| + a·out)`, a = g **[DECOMP]** |
| 6 | Effector | type `round(11v)`; Distort `k=2048(0.95X+0.05)²` + tone `(0.9Y+0.1)⁴`; Lo-fi hold `128X³+1`, levels `65536/(2048Y²+1)`; Flanger 5 ms, fb −0.96Y; Phaser 6 AP, fb `0.98(0.6Y)^(1/3)`; Filter LP `s³`/HP `0.5(s−1)³`; Delay fb X², filter by Y, **time = Mod Rate**; Reverb Decay `0.75X³`, HighDamping Y²; Stereo Haas 2 ms; mix: crossfade (Distort/Lo-fi/Filter/Stereo) or dry + w·wet (Flanger/Phaser/Delay) **[DECOMP]**; dry in the Reverb **not confirmed** |
| 11 | Love Philter | file f32 = **base**; mode fixed per target: pan/cutoff/res = 1 (add, base 0), others = 0 (multiply, base 1) → "unipolar" ↔ mode 0; envelope/LFO off = **neutral** **[DECOMP]** |
| 12 | Send k → bus | host: track = track+k (channel) or `track_list[k]` (mixer slot); replaced by side *buffer* of key **k** if registered (`TRegisterSideIOAction`) **[DECOMP]**; link key k ↔ Patcher pin k **not confirmed** |
| 7 | GMS | FM = PM `15·m·osc2` quarter-cycles (**confirmed**); AM `1 − 3m·osc2`; sync = osc1 phase ← master phase on wrap; amp = `e²` (**confirmed**) **[DECOMP]** |
| 10 | Kepler | LFO1→DCO `2·L·A(x)` octaves (A ≤ 0.214; ×10 with Range); LFO1→cutoff `2·L·A_c(x)` octaves (A_c ≤ 2.82); chorus I 0.4 Hz / II 0.6 Hz (triangle, 1.08–5.23 ms) / I+II 8 Hz sine (2.85–3.37 ms) **[DECOMP]**; LFO amplitude **not confirmed** |
| 9 | Toxic | FM: **2·cell·out cycles**; cutoff **`2^(9u+5)` Hz** (32 Hz…16.4 kHz), 4-pole Moog (fs 44.1 k) **[DECOMP]** |
| 8 | BassDrum | **N = 1024** (file `sounddata`) → Peak `f = base·(1 + 100x³s⁴)`; click FREQ `2^(14x)` Hz, LEVEL `25x⁵`, ¼-cycle segment of the same table **[DECOMP+DATA]**; CUT **not confirmed** |
| 1 | Sakura | Amp A `35x⁵+0.001` s, D `50x⁵+0.001` s (τ = T/8.82), S x², R `10x⁵+0.1` s (48 kHz in the code); Tune ±0.99 st; string decay `d^0.05` per period; linear Mix; Hi-Cut `g=(c+0.5env)²`, res `−10·r(1−g)` **[DECOMP]**; Hi-Cut in Hz **not confirmed** |
| 4 | BooBass | sample (attack + loop of 16,384); knobs = 3-band EQ (B −12…0 dB, M −10…0 dB, T −9…+3 dB; 1-pole cutoffs ≈ 178 Hz / 1.18 kHz) **[DECOMP]** |
| 5 | Drumpad | state `3PMD` + name 128 B + 38 floats (k = index; offset 140 in the `.flp`); Pitch ×2^(2x−1), Mallet Amp 10^(4(x−1)) **[DECOMP+DATA]**; other scales **not confirmed** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Harmor:** cutoff = `2^(log2 73.416 + p·7.9163/8192)·(f_note/130.81)^(p55/1000)` (without the width correction
   when the shape is unknown); types 5/6/7 → BP/BS/HP; part volume `(3^(|p11|/1000) − 1)/2·sign`;
   with the volume envelope off, constant level (do not apply `A 2 ms/R 0.1 s` as an FL envelope —
   it is only a LYNX *declick*); when on, apply the squared level.
2. **Sytrus:** keep FM at `4·g` cycles; replace RM-as-AM with `carrier × (1 − |a| + a·modulator)`.
3. **Effector:** Delay with time = Mod Rate period (`value·60/BPM`, halved until ≤ 1.5 s) and feedback X²;
   Filter with X < 0.5 = LP and X > 0.5 = HP; Lo-fi with `128X³ + 1` and `65536/(2048Y² + 1)`; Flanger/Phaser/Delay
   add the wet to the dry (`dry + w·wet`) instead of *crossfade*; Output Gain `(v − 0.5)·48` dB.
4. **Love Philter:** treat the `f32` as base and fix the mode per target (pan/cutoff/res additive); envelope/LFO
   off = neutral.
5. **GMS:** LYNX FM = PM with index `15·m/4` cycles (= 2π·3.75·m rad).
6. **Kepler:** vibrato LFO1→DCO `2·L·A(x)` octaves and LFO1→cutoff `2·L·A_c(x)` octaves (assuming L = ±1 only as an
   approximation, see open questions); chorus I/II/I+II with the values from §7.
7. **Toxic:** cutoff `2^(9u + 5)` Hz; FM with index `2·cell` cycles.
8. **BassDrum:** Peak `base·(1 + 100x³)` at the start of the *slide*; optional click `2^(14x)` Hz with `25x⁵`.
9. **Sakura:** ADSR from §10 (times in s) and Tune ±0.99 st.
10. **BooBass:** replace the mapping with a fixed source + 3-band EQ with the gains from §11.
11. **Drumpad:** read the 38 *floats* from offset 140 and use at least Pitch (`×2^(2x − 1)`) and Mallet Amplitude.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Harmor `FUN_00879540` (cases 11, 12, 38, 52/64, 54, 55, 56) | p11/p12/p38 scales, cutoff linear in log2 Hz, width, KT, env amount | DECOMP |
| Harmor `FUN_0089a4d0`, `FUN_00630170` | limits log2(73.416)/log2(17,739.7); `ln 2`, `log2 440` | DECOMP |
| Harmor `FUN_00874240`, `FUN_00874ed0` | range widened by the width; shapes 0…18 and extensions | DECOMP |
| Harmor `FUN_00886eb0`, `FUN_0088ab10` | per-voice cutoff `2^L`, reference 130.81 Hz; gain = output² × p12; end of voice | DECOMP |
| Harmor `FUN_00898200`, `FUN_00897d90`, `FUN_00873b00`, `FUN_008859a0`, `FUN_00873e40` | articulator (base, list, modes); part volume; unison in cents | DECOMP |
| Harmor `FUN_0086ed30` + 429 blobs | stored filter type = shape code; counts | DECOMP+DATA |
| Sytrus `FUN_0086dd40`, `FUN_0086ace0`, `FUN_00870580`, `FUN_00877270`, `FUN_00872290` | FM ×4 ×N (cycles), MOD 0…1, RM `1 − |a| + a·out` | DECOMP |
| Effector engine `FUN_180017050`, `FUN_180017240`, `FUN_180017600`, `FUN_18001b7f0…be60`, `FUN_180018c20`, `FUN_180019860`, `FUN_180019b60`, `FUN_180019d60`, `FUN_18001a680`, `FUN_180019110`, `FUN_180018de0`, `FUN_18001a130`, `FUN_18001d3f0`, `FUN_18001abc0`, `FUN_180017ec0`, `FUN_1800181c0`, `FUN_180018520` | parameters, types, X/Y and mix laws, LFO, delay | DECOMP |
| Love Philter `FUN_008542e0`, `FUN_008541f0`, `FUN_008543d0`, `FUN_008544d0`, `FUN_00856780` (masks `0x8574a0…a2`) | base/depths in the file; mode fixed per target; envelope/LFO off | DECOMP |
| FLEngine `TFruityHost` VMT `0x13141b8`; `FUN_0132bb50`, `FUN_0132ba30`, `FUN_0132baa0`, `FUN_00982ac0`, `FUN_00982b10`, `FUN_009827d0`, `FUN_00982c90`, `FUN_01316a50` (`TRegisterSideIOAction`), `0x1321d01`, `FUN_012b6400`, `FUN_012bd800`, `FUN_012bdf90` | send destination; side table by key k | DECOMP |
| GMS `FUN_1800f7530`, `FUN_1800ef960`, `FUN_1800ef9d0` (`0x18014d6b8` = 15, `0x18014d60c` = 3) | PM 15·m quarter-cycles; AM; sync; `e²` | DECOMP |
| Kepler `FUN_180022840` (tables `0x180105ba0`, `0x180105c00`), `FUN_180023ab0`, `FUN_18001b940`, `FUN_180001a60`, `FUN_18001d300` | LFO→DCO/cutoff; chorus | DECOMP |
| Toxic `FUN_180029fa0`, `FUN_180029cc0`, `FUN_180039b30`, `FUN_18003e730`, `FUN_1800395b0`, `FUN_180038890`, `FUN_180038d00` | FM in cycles ×2·cell; cutoff `2^(9u+5)` | DECOMP |
| BassDrum `FUN_18002a63f`, `FUN_180028d50`, `FUN_180028c90`, `FUN_180028ff0`, `FUN_18002ae90`; file `sounddata` | N = 1024; click | DECOMP+DATA |
| Sakura `FUN_18002eaa0`, `FUN_18002f4c0`, `FUN_18002da50`, `FUN_180031960` | indices; ADSR; decay; mix; Hi-Cut | DECOMP |
| BooBass `FUN_1800023a0`, `FUN_1800040e0`, `FUN_1800041c0`, `FUN_180004240`, `FUN_1800042f0`, `FUN_1800034f0` | knobs = 3-band EQ; sampled source | DECOMP |
| Drumpad `FUN_180023450`, `FUN_180022d00`, `FUN_180022de0`, `FUN_18001f4d0`; 975 blobs | state layout; indices; Pitch/Mallet | DECOMP+DATA |
| Official manual (cited pages) | descriptions, type order, qualitative ranges | DOC |

### Open questions

- Harmor: level after *note-off* with the volume envelope off; range of E (0…1 or centred) in the filter
  envelope; `eL/eR` extensions per shape; part field `+0x1C` (extra volume factor).
- Send k: the Patcher side that connects the consumer of key k to output pin k; exact contents of the track's
  `+0x128` list (inferred as the send list).
- Effector: whether the Reverb preserves the dry; Hz cutoff of the Filter cascade.
- Kepler: amplitude of the LFO value (`L`) and the chorus mix.
- BassDrum: use of the click's CUT; whether the engine runs at 44.1 kHz internally.
- Sakura: Hi-Cut in Hz; behaviour of the coefficients (fixed 48 kHz) at other rates.
- BooBass: origin of the sample tables; amplitude curve.
- Drumpad: scales of the decays, frequencies and filters of the mesh.
