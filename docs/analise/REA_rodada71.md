# Behavioural analysis of FL Studio 2026: round 71 (REA) — Hardcore, Distructor, Multiband Delay (missing native effects)

> **Nature of this document.** Decoding of native FL effects that LYNX imported only approximately or dropped:
> Hardcore (dropped until now), Distructor (imported as a generic distortion) and Multiband Delay (unmapped). *Clean-room*
> reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts,
> formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to
> trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the repository (the
> Hardcore cabinet impulses are read from the user's installation at import time).
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as such;
> what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r71/`.
- **Targets (Hardcore):**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Hardcore/Hardcore_x64.dll` | `adcd8ded370decab57cdd2ada210501e02e8f392a3ed07bbcb5b0ab89526957c` | Hardcore (MSVC C++) |
  | `Plugins/Fruity/Generators/Toxic Biohazard/Toxic Biohazard_x64.dll` | (round 45) | comparison of the shared tape line / reverb |
  | `Shared/ildsp_x64.dll` | `b96354e1ee4f096208432e9a31fd9838c9cc5cf36d4ac078f9cf5c94faaf1760` | comparison of the CRT math |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r71`, `gh/*.sh`, `out/hc*.c`), `objdump -M intel` (`hc/asm_*.txt`),
  `py/fcmp.py` (instruction-stream comparison of two functions, operands' addresses masked).
- **EXEC harness:** `run/fx.c` (round 61's effect harness, unmodified), driven by `py/fx.py`; `hc/hc.py` compares the DLL
  with LYNX's engine (`hc/cpp/hccmp.cpp`, `hc/cpp/dbg.cpp` for internal state); test scripts `hc/t1.py … t11.py`;
  `hc/teststate.py` builds the states and goldens of `tests/HardcoreTests.inc`.
- **Installation usage [DATA]** (`py/survey.py`): Hardcore 2 slots (render test, *Treben – Umziky*); Distructor 4 slots
  (DEDZII *Night Terror*, Edlan & Ella Noël *Song For You*, Sid De La Cruz *Secret Agent Rendezvous* ×2); Fruity Squeeze
  2 slots (already exact since round 29); Fruity Scratcher 1 (render test); Multiband Delay none (presets only).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels; "−∞" = bit-identical.

---

## 1. Hardcore

### 1.1 Plug-in and state **[DECOMP + DATA]**

`Hardcore_x64.dll` is an MSVC C++ build of a third-party guitar multi-effect wrapped for FL (classes `CFLWrapper` →
`HardcoreSynth`, a VST-style `AudioEffect`; pedals `CPedal_*`; the reverb is the `fxReverb` class of Toxic
Biohazard's rack, REA_rodada45 §5.5, and the tape line is the same `CTapeDelay`). Entry `CreatePlugInstance`
`0x180002340`; FL state I/O `FUN_180002f90`; the inner chunk reader `FUN_18002ad90`, writer `FUN_18002aca0`;
`setParameter` `FUN_18002a010`; sample-rate set-up `FUN_18002a5a0`; block process `FUN_18002a740`.

State (plugin record `0xD5`): `"SSLF"`, u32 size (0x1CC), then the chunk: `"1RCH"` (or `"DRCH"`, the demo build's tag),
**50 × f32** parameter values, 256 bytes of preset name (ANSI, zero padded). The reader calls `setParameter` for
indices 0…49 **in order**, which matters: box knobs (13…37) arrive before the box types (45…49), and a type change
builds its pedal and hands it the five stored knobs. Defaults (`FUN_18002a570`): box switches 39…43 = 1, EQ switch and
box types = 0, everything else 0.5.

| Index | Parameter | Value law |
|---|---|---|
| 0, 1 | input / output level | target `powf(10, v·2.4 − 1.2)` (±24 dB); the gain moves `g += (target − g)·0.001` per sample; it jumps to the target at the first block after a rate change |
| 2 | EQ on | ≠ 0; a change clears the 16 EQ filters at the next block |
| 3…10 | EQ bands 1…8 | `y = (v − 0.5) + v − 0.5`, gain `(float)(±y²·18.0)` dB (sign of y) |
| 11 | cabinet | `(int)v`: 0 = none, 1…20 = the installation's `Imp\Model A…T.wav` (two names carry a space before `.wav`) |
| 13 + 5b + k | box b knob k | handed to the pedal of box b |
| 39…43 | box on | 0 bypasses the box; a change marks the pedal for a reset before its next block |
| 45…49 | box type | `(int)(float)(int)v`, clamped to −1…11: 0 empty, 1 distortion "Model A", 2 chorus, 3 flanger, 4 phaser, 5 reverb, 6 delay, 7 noise gate, 8 equalizer, 9 compressor, 10 modulator, 11 tube drive |
| 12, 38, 44 | — | stored, unused |

FL's `ProcessParam` takes 0…256 integers (`×1/256`, clamped) and only calls `setParameter` when the value changes
(`FUN_1800034a0`) **[DECOMP + EXEC]**.

Installation use **[DATA]**: 2 mixer slots — the render test `System/Render tests/Hardcore.flp` (box 1 empty, box 5 delay,
cabinet 4, preset "80s lead") and *Treben – Umziky – All the same* (box 1 distortion Model A, cabinet off).

### 1.2 Block **[DECOMP `FUN_18002a740` + asm]**

1. Per sample, each channel through a second-order high-pass at 30 Hz, Q 1 (the `FUN_18001d050` design, see §1.4), times
   the input gain (both channels with the gain before its update).
2. Boxes 1…5 in series: a pedal marked for reset is reset first; a pedal runs only when its box is on.
3. Cabinet convolution (§1.5).
4. EQ (when on): band by band over the whole block, each band left then right per sample.
5. Output gain (gain before its update).

The FL wrapper cuts host blocks into chunks of (buffer size − 4) samples; no result depends on the chunking
**[EXEC: 512 vs 256-sample host blocks identical]**.

### 1.3 Shared building blocks **[DECOMP + asm + EXEC]**

- **CRT math.** The statically linked `powf`, `sinf`, `cosf`, `expf` and the packed float sine pair are the same code as
  `Shared/ildsp_x64.dll`'s (instruction-for-instruction comparison), FMA path chosen at run time (the flag is set on
  this machine): LYNX uses the re-creations of `core/IldspMath.h` (REA_rodada60 §4.5). `tanf` (no FMA path) equals Toxic's
  `0x18006fa20`. Using Toxic's plain re-creations instead left 1-ulp differences in the phaser (cosf of small phases)
  and tube drive.
- **Biquads with a design cache**, all float with an explicit double for ω = f·2π/fs:
  - low-pass `FUN_18001d240` and high-pass `FUN_18001d050` (s, c from the packed sine pair): `inv = 1/(s + q)`,
    `a = (1 ∓ c)/(s/q + 2)`, `b1 = (c·inv)·(q·−2)`, `b2 = (q − s)·inv`; ticks
    `y = ((2x₁ + x + x₂)·a − y₁b1) − b2·y₂` and `y = ((x − 2x₁) + x₂)·a − y₁b1 − b2·y₂`;
  - band-pass `FUN_18001d420`, designed in **double** with a double sine pair (sin x, sin(|x| + π/2)):
    `d = 1/((0.5/q)·s + 1)`, `b0 = (float)((float)(s·0.5)·d)`, `a1 = (float)((float)(2c)·d)`,
    `b2 = (float)((float)(1 − s/(float)(2q))·d)`; tick `y = ((x − x₂)·b0 + y₁·a1) − b2·y₂`;
  - the cookbook peaking/shelf filter with a dirty flag (`FUN_18001d660` / `FUN_18001da00`) = Toxic's EQ band.
  - Every input and output below ±1e−9 is flushed to 0.
- **Tape line** (`CTapeDelay`, `FUN_180025ff0`) = Toxic's (REA_rodada45 §FX.3), with two optional internal filters that
  Hardcore never switches on.

### 1.4 The eleven pedals **[DECOMP + asm; every formula EXEC-verified]**

Knob k of a box is v_k (0…1). "x = L + R" is the mono sum.

| Type | Set-up / laws | Per sample |
|---|---|---|
| 1 Distortion "Model A" | k0 drive: `k = 99·v₀² + 1`, pre-gain `1/(8v₀ + 1)`; k1 comb length `(int)(n₂·v₁)` clamped below n₂ (n₂ = (int)((double)(8fs)·0.0002)); k2 high-pass Q `30v₂ + 1` at 85 Hz; k3 tone: peaking EQ 2500 Hz, bandwidth 3, gain `(v₃ − 0.5)·20` dB; band-passes 200 Hz and 1 kHz (Q 0.99) at the host rate; low-pass 500 Hz, Q 4; comb 1 = (int)((double)(8fs)·0.001) samples | a = (float)(BP200(x)·0.05), b = BP1k(x)·5; 4-point cubic interpolation between the two previous inputs of each path at t = 0, ⅛ … ⅞ (8× oversampling); each value soft clipped `1 − 10^(−k·v)(1 − v)` / `10^(k·v)(v + 1) − 1`; comb 1 feeds `b − 0.25·old`, mix `(float)(0.7·old + a)`; comb 2 (when its length ≠ 0) `m − 0.3·old`; low-pass (run at 8× without redesign) × pre-gain; after the 8 steps: high-pass, tone EQ; mono out |
| 2 Chorus | three tapes of `int(fs·0.02)·{1.1, 0.854, 1}` (truncated) samples, phases 0, 0.467, 0.778; rate `(float)(5v₁ + 0.1)`·{0.896, 1, 0.689}/fs; gain −v₂²; depth v₀³ | ticks tape 1, 3, 2 on x; L += y₂ + y₁, R += y₂ + y₃ |
| 3 Flanger | two tapes of (uint)(0.005fs) samples, phases 0 / 0.5; time `clamp(v₀², 0.01, 1)`; rate `v₁²·(10v₁) + 0.01` Hz; gain `−0.1 − 0.9v₂`; feedback −0.1; depth 1 | L += tape₁(L), R += tape₂(R) |
| 4 Phaser | lo = 50/fs, hi = 20000/fs; K = v₂²·(v₂/2); frequency `max(v₁², 1e−5)`; mix `max(v₃, 1e−5)`; phase step `(float)((v₀·88200)·v₀/fs)·0.0005` | Toxic's phaser (REA_rodada45 table, type 4) with a fixed feedback 0.454875678, float smoothers (0.001) and the phase wrapped at 2π in float; the reset also clears the LFO phase |
| 5 Reverb | `fxReverb` index 8 ← (float)(v₀³·0.6), 9 ← \|1 − v₁\|, 12 ← (float)(0.999v₂ + 0.001); reset sets 2 and 5 to 1 | in place; unlike Toxic's build **all early-reflection taps have gains**: left taps 1102, 2904, 3784, 4873, 5878, 7470, 9062 (7 used), right 837, 1423, 2865, 3938, 5278, 5978, 7532, 9263 (scaled by ((p₄+3)·fs/103)·3.40136e−5), gains min(L₀, R₀)·{0.5, 0.36}/tap, summed with the signs + + − + + + + (left) and + − + + − + + − (right) |
| 6 Delay | two 1-second tapes (phases 0 / 0.5); time clamp(v₀, 0.1, 1); feedback (float)(−0.95·v₁²); gain −((float)(0.9v₂·v₂) + 0.1); speed 1 Hz, depth (double)0.01f | as the flanger |
| 7 Noise gate | threshold `(float)pow(10, (1 − v₂)·−3.2)`; attack/release `(float)(8.82/((double)(float)(2v² + 0.05)·fs))` (v₀, v₁); hold (uint)(0.1fs) samples | per channel: below the threshold count samples, after the hold `g −= g·rel`, otherwise `g += (1 − g)·att`; g ≥ 1e−7; out = g·x |
| 8 Equalizer | low shelf 300 Hz, peaks 700 Hz / 1.8 kHz, high shelf 7 kHz, bandwidth 0.985; gain ±y²·10 dB (y as the master EQ) | band by band |
| 9 Compressor | threshold as the gate's (v₀); slope `1/((1 − v₁)·30 + 1)`; attack 8.82f/((2v₂² + 0.1)·fs), release 8.82f/((4.8v₃² + 0.2)·fs) | per channel: t = thr/((\|x\| − thr)·slope + thr) above the threshold, else 1; `env += (float)coef·(t − env)` (attack while t < 1); out = env·x |
| 10 Modulator | tape of (uint)(0.01fs) samples, time clamp(v₀², 0.01, 1), gain 1, no feedback, depth 1, speed r = 30v₃² Hz; LFO step r/fs | lfo = (float)cos(2π·phase); y = tape(x); `y·(1 − a·lfo·lfo²)`; L = (1 − p·lfo)·…, R = (p·lfo + 1)·…; a, p glide to v₁, v₂ (×0.001 per sample) |
| 11 Tube drive | drive `(float)((double)(v₂²·(10v₂)) + 0.01)`; low shelf 200 Hz (bw 2.5) gain (1 − v₀)·−28; high shelf 7 kHz (bw 2) gain (1 − v₁)·−28; low-pass 4 kHz Q 0.5; output gain 0.5/(1 − 10^(−4·drive)(1 − drive/2)) | x = lowshelf((R + L)·drive) → low-pass → soft clip (k = 8) → minus a slow follower (`f += 0.01·(x − f)` in double) → high shelf × output gain; mono out |

### 1.5 Cabinet **[DECOMP `FUN_180026f00`, `FUN_180027260`, WAV reader `FUN_18001c890` + DATA]**

- The file is read by the plug-in's own RIFF reader: float32 copied; integer PCM scaled by 2⁻¹⁵ (16 bits),
  **1.1920930376163597e−07** (24 bits — one ulp above 2⁻²³) or 2⁻³¹ (32 bits). The installation's 20 files: A…L mono float32,
  M…T stereo 24-bit, all 44.1 kHz.
- Samples × (float)(file rate / host rate); length `count = (int)(frames·host/file rate)`; taps = 4·(count/4 + 1), zero
  padded; tap i = linear interpolation at `i·(1/count)·(frames − 1)` × 0.2 (mono files feed both channels).
- Convolution in time, accumulated so that every output sums the **oldest input first**
  (`y = x[n−T+1]·h[T−1] + … + x[n]·h[0]`, float, no FMA). A rate change reloads the file; cabinet 0 unloads it.

### 1.6 Verification **[EXEC]**

Harness `run/fx.c` (rounds 50/61), `hc/hc.py` (DLL vs LYNX's engine through `hc/cpp/hccmp.cpp`), `hc/t*.py`:

| Test | Result |
|---|---|
| defaults, levels, EQ (all bands) | −∞ (bit-identical) |
| each of the 11 pedals alone, 2 random knob sets each (22 runs, 1 s of music) | −∞ ×22 |
| the 20 cabinets (5 checked by name: A, D, I, J, T) and cabinets inside random chains | −∞ |
| 24 random full chains (random types/switches/knobs/cabinet/EQ) at 44.1/48/96/22.05 kHz, host blocks 512/256/1024/100 | −∞ ×24 |
| automation: 10 runs × 12 random parameter changes (levels, EQ, knobs, box switches) | −∞ ×10 |
| switching each pedal type off/on, knob changes, type changes, cabinet off, EQ off mid-stream | −∞ ×11 |
| the 2 installation states (render test, Treben), 2 s of music | −∞ ×2 |

Not confirmed: the 8-bit and 64-bit WAV paths of the reader (not used by the installation; LYNX treats them as
unreadable); what the "demo" chunk tag changes in the registered build (it resets to defaults there).

### 1.7 LYNX

- `src/core/HardcoreFx.{h,cpp}`: `EffectKind::hardcore`; the engine above; slot params = FL's 50 values (cabinet / 20,
  types / 11); `EffectSlot::state` = the installation's 20 impulse files (≈290 kB, packed `"LXHC"`) so that the cabinet
  stays switchable in a saved project without FL. Import: `effectFromFl ("Hardcore", …, data)` reads them through the
  FL data reader; without the installation the engine runs with the cabinet off (import report note).
- `src/core/IlFxKit.h`: Toxic Biohazard's CRT re-creations, biquads, tape line and `fxReverb` moved out of
  `ToxicFx.cpp` and shared (templated on the CRT routines; Toxic keeps its plain ones, its tests unchanged).
- Tests: `tests/HardcoreTests.inc` (`hardcore_exact`): 3 states of our own (all 11 pedals, EQ on/off) + 2 cabinet
  states (installation files) against DLL goldens, the import/mixer path, parameter round trip.

<!-- DISTRUCTOR -->
<!-- MBD -->

---

## 4. Fruity Scratcher — not ported **[DATA + DECOMP]**

Round 20 §2 decoded the engine. Its only installation use (the render test) loads
`%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav`, which is an **Ogg Vorbis WAV (format 0x674F)**: the
plug-in asks the host for the sample already converted to 16-bit / 2 channels / 44.1 kHz (host interface methods at
+0x68 load, +0x70 data, +0x78 free; `FUN_0072be20`), so the result depends on FL's own Vorbis decoder and converter,
and the movement comes from `0xDF` pattern control events whose scaling is not confirmed. Neither can be reproduced
exactly, so the Scratcher stays unported (LYNX drops it with a report entry, as before).

## 5. Fruity Squeeze

Already LYNX's exact `EffectKind::squeeze` (round 29 §2); both installation slots import to it. Nothing to do.
