# Behavioural analysis of FL Studio 2026: round 29 (REA) — Sakura modulation and effects, Fruity Squeeze, Vintage Phaser, BooBass

> **Nature of this document.** Continuation of `REA_rodada28.md`. *Clean-room* reverse engineering, behavioural only.
> No Image-Line code or pseudocode was copied: what is here are formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched
> (no FLEX pack was opened). No Image-Line audio data was extracted into the repository or this document: for
> BooBass only the **location and format** of the embedded recordings are given, so that LYNX can read them at run
> time from the user's own installation.
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
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Tools:** Ghidra 12.1.4 headless (JDK 21) on the existing projects `/tmp/r28gh_sak` (full decompilation
  `r28/out/sak_all.c`), `/tmp/r15gh_Squeeze.dll`, `/tmp/r15gh_VPhaser.dll`, `/tmp/r15gh_VPhaser_eng.dll`
  (`r29/out/vph_all.c`), `/tmp/r18gh_BooEng` (`r29/out/boo_all.c`); `objdump` for disassembly; constants with
  `r28/py/kc.py`.
- **Execution harnesses** (our own C, Zig 0.14.1 → Windows x64). All of them call
  `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)` first, install a vectored fault
  handler that ends the process, load the installation's DLL unmodified, drive it only through its own exported
  `CreatePlugInstance` and vtables with a minimal fake host, run under `timeout`, and always end with
  `TerminateProcess` (DLL finalisation never runs). No process or dialog was left behind.
  - `r29/run/plug29.c` (Sakura; round 28's `plug.c` plus: host block size `PLUGBLK`, direct CRT calls `PLUGCALLF`/
    `PLUGCALLD`, a second sample-rate set `PLUGSR2`, the FL tempo event `PLUGTEMPO` through the wrapper's own
    ProcessEvent, pitch-bend and channel-pressure through the engine's own MIDI handlers).
  - `r29/run/boo.c` (BooBass), and the effect harnesses of §2 (see there).
- **Models (pure Python, `python3 -I`, numpy for the reverb):** `r29/py/sak/sakura29.py` (voice + matrix + EG + LFO),
  `r29/py/sak/sakura_fx.py` (tape delay, resonators, chorus, delay), `r29/py/sak/sakura_reverb.py` (reverb),
  `r29/py/squeeze_model.py`, `r29/py/vphaser_model.py`, `r29/py/boobass_model.py`; batteries listed per section.

- **Targets** (hashed in this round):

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Sakura (engine) | `Plugins/Fruity/Generators/Sakura/engine_x64.dll` `e632487ca6419ca7af2c90dbbf73df8ce56ec8fed30989c7ee598de8ae0f1bab` (= round 28); front-end `Sakura_x64.dll` `6087f71cc2e1856984ee9df67ce4c4a242a1002e32836e54f04b515ad2179697` not analysed | C++ (MSVC; `CSakuraSynth`, effect classes `CTapeDelay`, `CStringDelay`, `CChorus`, `fxReverb`) | EG `FUN_180018910` (start), `FUN_1800189b0` (tick), `FUN_180018b30`/`FUN_180018b50` (release), `FUN_180018b80` (segment); global LFO + 48 kHz driver `FUN_180030720`; matrix `FUN_18002e470`; per-chunk voice laws `FUN_18002da50`; note-on/off `FUN_18002d510`/`FUN_18002d830`; effect build `FUN_18002c450`, chain `FUN_18002c700`; resonators `FUN_180024f20`, `FUN_1800251e0`, `FUN_180025330`, `FUN_180025570`, `FUN_1800257e0`; tape delay `FUN_1800236f0`, `FUN_1800238f0`, `FUN_1800239b0`, setters `FUN_180023d50`/`23da0`/`23db0`/`23dc0`/`23de0`; chorus `FUN_180024ba0`, `FUN_180024a70`, `FUN_180024af0`, `FUN_180024b50`, `FUN_180024d50`; reverb `FUN_180025f40` (ctor), `FUN_1800258f0` (defaults), `FUN_180025990` (buffers), `FUN_180026530` (params), `FUN_1800273e0` (coefficients), `FUN_180028f50` (process), `FUN_180026320` (reset), `FUN_1800273b0` (dB); setParameter `FUN_18002f4c0`; wrapper ProcessEvent `FUN_180003410` (tempo); CRT (EXEC): `FUN_18005a040` = cos, `FUN_18005bbd0` = tanf, `FUN_18005a5c0` = cosf, `FUN_18005aac0` = expf, `FUN_180044590` = powf, `FUN_180042ef0` = pow |
  | Fruity Squeeze | `Plugins/Fruity/Effects/Fruity Squeeze/Fruity Squeeze_x64.dll` `ed797a2fea90fd3f88bf726790f27197750560423dd2bc0b223efee26d4857dd` (= round 15) | Delphi (`TTestPlug`, VMT `0x6d2480`) | Dispatcher `FUN_006d2760`, SaveRestoreState `FUN_006d30c0`, **ProcessParam `FUN_006d35c0`**, **Eff_Render `FUN_006d40c0`**, **filter `FUN_006d4620`**, relation `FUN_006d4580`, filter object ctor `FUN_006d0820`, rate setter `FUN_006d0ab0`, coefficient update `FUN_006d09a0`, cutoff mapper `FUN_006d0630`/`FUN_006d07d0`, plugin ctor `FUN_006d31b0`; RTL: `FUN_00409fd0` = Round, `FUN_005d1390` = Power, `FUN_005de7c0` = denormal flush, `FUN_005dfe00` = silence test |
  | Vintage Phaser (front-end) | `Plugins/Fruity/Effects/Vintage Phaser/Vintage Phaser_x64.dll` `3f7c558a02071a89de8a40d26b3bc20015b1aa919fc4335b3c7d20004afe0d0f` (= round 15) | Delphi (`TTestPlug`, VMT `0x6e9df8`) | Dispatcher `FUN_006ea1e0`, **SaveRestoreState `FUN_006ea8e0`** (+ v1 index table `0x742a30`), **ProcessParam `FUN_006eb1a0`**, Eff_Render `FUN_006edbf0` |
  | Vintage Phaser (engine) | `Plugins/Fruity/Effects/Vintage Phaser/engine_x64.dll` `43be6c11fd3556fef6e0155c42bc07fe58730e19e1a42918cf08d65ec2eae764` | C++ (MSVC; VST2 `AudioEffect` subclass `TestPlug`, exports `VSTPluginMain`) | VST `setParameter` `FUN_18001dc80`, names `FUN_18001d790`, display `FUN_180019a10`, processReplacing `FUN_18001d040`; phaser core object (plugin + 0xf0): ctor `FUN_18001a250`, **parameter laws `FUN_180018cd0`**, sample-rate set-up `FUN_180019560`, **main loop `FUN_180018210`**, **LFO/coefficients `FUN_180001190`**, **cores `FUN_180001660` (V1) / `FUN_1800176b0` (V2)**, decimator `FUN_180001940` + `FUN_180016ed0`, up-sampler `FUN_18001be70`/`FUN_18001bd20`/`FUN_18001bb30`, frequency/rate `FUN_180018a00`/`FUN_180018b80`, waveform `FUN_180017f90`, tension `FUN_18001cf50`/`FUN_18001ceb0`, biquad designer `FUN_18001b780`/`FUN_18001b1e0`, delay `FUN_18001c200` (+ set-up `FUN_18001cab0`, `FUN_18001ad70`); CRT: `FUN_180023b50` = powf, `FUN_1800224b0` = pow, `FUN_180021b80` = log, `FUN_1800214a0` = exp, `FUN_1800220b0` = logf, `FUN_180025690` = tan, `FUN_180024f90` = sinf, `FUN_180025e30` = tanf (identified from their arguments/results; all EXEC-consistent) |
  | BooBass (engine) | `Plugins/Fruity/Generators/BooBass/BooBass_Eng_x64.dll` `4e10efcd9f945860911e7cdff9c4065c4f193b58ddffc8304b45add1db51705f` (= round 18); Delphi front-end `BooBass_x64.dll` `75c996004cfb5118739d1c044d010c2dbdfc3cd08f749b702eb2f972b77b1e29` not analysed (it only contains the string `BooBass_Eng_x64.dll` and imports `CreatePlugInstance`) | C++ (MSVC; `BooBassPlug` on the `TCPPFruityPlug` framework, helper classes `WaveTable`, `HighLimiter`; `MonoMode` present but never called) | plugin vtable `0x180063630` (slot 1 Dispatcher `FUN_180002270`, 6 ProcessParam `FUN_1800023a0`, 8 Gen_Render `FUN_180002420`, 9 TriggerVoice `FUN_180002790`, 10 Voice_Release `FUN_1800028f0`, 11 Voice_Kill `FUN_180002920`); `CreatePlugInstance` `0x1800029a0`; sample-rate set-up + table build `FUN_1800034f0`; table decoder `FUN_1800051f0`; zone/pitch at note-on `FUN_180003db0`, per-block pitch `FUN_180003f90`; note start `FUN_180004e60`; envelope set-up `FUN_180002c80`/`FUN_180002d80`, tick `FUN_180005460`, note-off `FUN_180002e10`; knobs `FUN_1800040e0`/`FUN_1800041c0`/`FUN_180004240`; per-sample voice (normal) `FUN_1800042f0`, (HQ) `FUN_180004640`, block loop `FUN_1800049f0`; 6-point interpolator `FUN_180004f70`; CRT `FUN_1800393d0` = pow(double, double), `FUN_18003aa70` = sin |

Manual pages read: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Squeeze.htm`, `…/plugins/Vintage%20Phaser.htm`, `…/plugins/BooBass.htm` (round 18), `…/plugins/Sakura.htm` (round 28) — abbreviated `…/plugins/`; quotes are short.

**Effect harnesses (§2, §3)** (our own C, Zig 0.14.1 → Windows x64, same rules:
`SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, vectored fault handler that ends the
process, `TerminateProcess` at the end so no DLL finalisation runs, run under `timeout`; no process or window was left
behind):
- `r29/run/effd.c` → `effd.exe`: loads a **Delphi effect DLL unmodified**, creates it with its exported
  `CreatePlugInstance` and a fake host (all host methods return 0; host query `0x47` returns a dummy interface whose
  methods return 0; host field `+0x70` points to a scratch buffer, which Squeeze uses), sets the rate/block size through the
  plugin's own Dispatcher (`FPD_SetSampleRate` = 4, `FPD_SetBlockSize` = 3), parameters through its own ProcessParam
  (raw integer control values, flag `REC_UpdateValue`), renders through its own Eff_Render. Used for **Squeeze** and for
  the **Vintage Phaser front-end** (which loads its own `engine_x64.dll`).
- `r29/run/vsth.c` → `vsth.exe`: loads the **Vintage Phaser `engine_x64.dll` unmodified** through `VSTPluginMain`
  with a minimal VST2 host callback (version 2400; `getTimeInfo` returns tempo 120 or `VSTTEMPO`, flags from `VSTFLAGS`),
  `effOpen`, `effSetSampleRate`, `effSetBlockSize`, `effMainsChanged(1)`, the engine's own `setParameter`, its own
  `processReplacing`; optional read-only peeks of engine fields (`PEEK`) and of module data (`GPEEK`).
  (Note: Windows processes started from WSL only see environment variables listed in `WSLENV`.)

**Models (pure Python, `python3 -I`):** `r29/py/squeeze_model.py`, `r29/py/vphaser_model.py`; drivers
`run_sq.py`, `run_vp.py`; batteries `battery_sq.py` (→ `battery_sq.out`), `battery_vp.py` (→ `battery_vp.out`), front-end
normalisation check `t_front.py` (→ `t_front.out`), demo-project states `t_data.py` (→ `t_data.out`), sync `t_sync.py`.

**BooBass:** Ghidra project `/tmp/r18gh_BooEng` (round 18), full decompilation `r29/out/boo_all.c`, linear disassembly
`r29/out/boo.dis` (objdump), constants with `r28/py/kc.py`, table descriptors with `r29/py/tabs.py`.
**EXEC harness** `r29/run/boo.c` → `boo.exe` (our own C, Zig 0.14.1 → Windows x64, derived from round 28 `plug.c`):
`SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)`, vectored fault handler that
terminates, loads the installation's `BooBass_Eng_x64.dll` unmodified, creates the plugin with its exported
`CreatePlugInstance` and a fake host, and drives it only through the plugin's own vtable (Dispatcher SetBlockSize /
SetSampleRate / ProcessMode, ProcessParam, TriggerVoice with a `TVoiceParams` record, Voice_Release, Gen_Render). The
fake host's slot 11 (the "add mono buffer to stereo output with L/R ramp" method the engine calls) **records the
engine's mono voice buffer**; slot 31 (ComputeLRVol) returns L = R = 1. Always ends with `TerminateProcess`, run under
`timeout`; no process or dialog was left behind. **Model** `r29/py/boobass_model.py` (pure Python, float32 emulated);
comparison `r29/py/test_boo.py`, `battery_boo.py` (results in `battery_boo.txt`), `long_boo.py`.

---

## 1. Sakura — modulation sources, matrix and effects

Continuation of round 28 §1 (voice laws unchanged and re-verified). Everything below is **[DECOMP]** in the engine
`engine_x64.dll` and **[EXEC]**-verified with `plug29.exe` unless marked otherwise.

### 1.1 Modulation envelope "EG" (params 34–37; `FUN_180018910`, `FUN_1800189b0`, `FUN_180018b30`, `FUN_180018b50`, `FUN_180018b80`) **[DECOMP + EXEC]**

- Times as stored by `setParameter` (round 28): A = `10x³ + 0.001` s, D = `50x⁵ + 0.001` s, S = `x²`, R = `50x⁵ + 0.1` s;
  A and D are floored at 0.001 s when the envelope starts.
- The EG is a per-voice object evaluated **once per internal chunk** (the voice set-up `FUN_18002da50`, ≤ 48 samples
  at 48 kHz, i.e. a 1 ms staircase). Its phase is advanced *before* the value is computed: `φ += n/(48000·T)`.
- **Shape: straight lines** (the curve helper `FUN_180018b80` applies a power curve only when a per-stage curve value
  is non-zero; the three curve fields at EG `+0x68/+0x70/+0x78` are zeroed by `FUN_180018910` and never written
  elsewhere — **inference** from the absence of writers, confirmed by EXEC):
  - attack: from the start level to 1 in A seconds (start level = 0 for a voice whose EG is idle, which is always the
    case for a fresh/killed voice; otherwise the EG's current output);
  - decay: from 1 to S in D seconds, then holds S (if S = 0 the EG ends there);
  - release (note-off, `FUN_18002d830`): R is captured, then from the current output to 0 in R seconds.
  - Output clamped at ≥ 0 and stored as float32.
- Matrix timing: within `FUN_18002da50` the **pitch destination (3) is evaluated before the EG/LFO-amount update**, so
  pitch uses the previous chunk's EG and LFO values; every other destination uses the current chunk's values.

### 1.2 LFO (param 38; global, `FUN_180030720`) **[DECOMP + EXEC]**

- Rate `f = 10x³ + 0.01` Hz (stored float). **One LFO for the whole synth** (not per voice), phase φ in [0, 1):
  - φ = 0 when the engine is reset (sample-rate set, `FUN_180030660`); **not reset at note-on** (free running);
  - **once per host render call** (Gen_Render block of n host samples at host rate fs): value
    `LFO = cos(2π·φ)` (`FUN_18005a040` = CRT `cos`, EXEC: cos(0) = 1, cos(1) = 0.5403), then `φ += n·f/fs` (float32),
    `φ −= 1` when φ > 1. So the LFO is a **cosine** starting at +1, sampled once per host buffer (EXEC with blocks of
    256 and 100 host samples both match).
- Per voice: matrix source 2 = `LFO × m₀`, m₀ = sum of destination 0 (default 1 when no slot targets destination 0).

### 1.3 Matrix sources and destinations — complete (`FUN_18002e470`, `FUN_18002da50`) **[DECOMP + EXEC]**

Slot k (0…7): source = int(P[79+4k]), amount = P[80+4k]² (negated when P[81+4k] ≠ 0), destination = int(P[82+4k]).
`m_d` = Σ amount·source over the slots with that destination and a non-zero source; if none, `m_0 = m_1 = 1`, other
destinations 0.

| Source | Value | Evidence |
|---|---|---|
| 1 EG | §1.1 | EXEC (`eg_*` cases) |
| 2 LFO | `cos(2πφ)·m₀` | EXEC (`lfo_*`) |
| 3 mod wheel | CC1·(1/127) (synth `+0x126c`, MIDI handler slot 4) | EXEC (`modwheel_pitch`) |
| 4 pitch bend | the float passed to the engine's pitch-bend handler (slot 3, `+0x1468`) | EXEC with the handler; the wrapper/FL scaling of that float **not confirmed** |
| 5 channel pressure | value·(1/127) (slot 6, `+0x166c`) | EXEC (`pressure_level`) |
| 6 velocity | vel/127 | EXEC (`vel_level`) |
| 7 note | note·0.0125 | EXEC (`note_hicut`) |
| 8 release gate | 0 while held, 1 after note-off | EXEC (`release_gate`) |

| Dest. | Law (x = knob value, m = m_d) |
|---|---|
| 0 | LFO depth (multiplies source 2) |
| 1 | string level `F = 10^(2m)·0.05` (default m = 1 → 5) |
| 2 | stereo sep: `s₁ = clamp(Sep + m)`, `s₂ = clamp(1 − Sep + m)` |
| 3 | pitch: step × `2^(m/6)` (1 unit = 2 semitones), uses the previous chunk's sources |
| 4 | Lo-Cut: `u = clamp(LoCut + 0.5m)` → `f = 2^(9.3u + 5)` Hz |
| 5 | Hi-Cut: `g = clamp((Cutoff + 0.5m)²)` |
| 6 | Hi-Cut attack: `T_a = clamp(Attack + m)²·0.5` s |
| 7 | Hi-Cut decay: `d = clamp(Decay + m)` → rate `0.0002/(3d³ + 0.001)` |
| 8 / 9 / 10 | string 1 / string 2 / both decay: `fb₁ = clamp(Decay₁ + m₈ + m₁₀)^0.05`, `fb₂ = clamp(Decay₂ + m₉ + m₁₀)^0.05` |
| 11 / 12 / 13 | string 1 / string 2 / both damp amount: `a₁ = clamp(Damp₁ + m₁₁ + m₁₃)`, `a₂ = clamp(Damp₂ + m₁₂ + m₁₃)` |
| 14 | noise rate: `θ = 0.996·(1 − clamp(Rate + m))^0.25` |

All destinations are re-evaluated per internal chunk (≤ 48 samples). The release in the EXEC harness happens at the
start of the host block that contains the note-off (round 28's model released at the next 48-sample chunk; corrected).

### 1.4 Effects: order and rate (`FUN_18002c450` build, `FUN_18002c700` chain) **[DECOMP + EXEC]**

`voices (48 kHz) → linear resampler → [host rate fs] HP 32 Hz Q 1 → resonators → chorus → reverb → delay → master
gain × CC7 × CC10 pan`. Each effect runs only when its "ON" is set; **all effects run at the host sample rate** and are
(re)built whenever the sample rate is set. Several effects use a common delay element:

**"Tape" delay element** (`CTapeDelay` `FUN_1800236f0`/`FUN_1800239b0`; the resonator's `CStringDelay`
`FUN_180025330`/`FUN_180025570` is the same idea without modulation). A tape of N samples per lap, two pages (current
lap and previous lap) storing each written sample together with its tape position (stamp). Per sample: read the
previous lap at the current position by linear interpolation between stamps; advance the position by the speed
(`1/(p·(N − 2) + 2)` per sample for a "time" p in [0, 0.99]; minimum `1/N`, maximum 0.2); write the input (+ feedback)
stamped with the new position; at a lap (position ≥ 1) the pages swap. At constant speed this is a **fractional
delay of `p·(N − 2) + 3` samples with linear interpolation**. `CTapeDelay` adds: speed × `(1 + depth·t²(3 − 2t))`
(t = triangle LFO 0…1…0, rate in Hz/fs), speed smoothing `+0.005·(target − current)` per sample, wet smoothing
`+0.01·(target − current)` per sample (from 0), output = read × wet, written value = input + feedback × read.
Initial tape positions are per-instance constants (below); an internal LP/HP pair exists but its enable flags are
never set (**inference**: no writer found).

### 1.5 Resonator bank (params 39–62; `FUN_180024f20`, `FUN_1800251e0`, `FUN_1800257e0`) **[DECOMP + EXEC]**

- Frq k → `p_k = min(0.99, 1 − Frq^0.1)`; Fdbk k → `fb_k = 0.9999·√Fdbk`; On k → enable (and clear both units).
- Each resonator = two string-delay units with N = trunc(0.1·fs) (100 ms): A at time p_k, B at `1.05·p_k`, each a
  comb `w = x + fb·z`, `z += 0.5·(r − z)` (1-pole LP, coefficient 0.5, in the loop), output = the delayed read r.
  So the A unit's period is `p(N − 2) + 3` samples (≈ `0.1·(1 − Frq^0.1)` s; e.g. Frq 0.5 → 6.7 ms ≈ 150 Hz at 48 kHz).
- Signal: `x = L + R`; `y_k = B_k(−A_k(x))`; resonators 1, 3, 5, 7 weighted L 0.75 / R 1.25, resonators 2, 4, 6, 8
  L 1.25 / R 0.75; each sum through a 2-pole HP 32 Hz Q 1 and **added to the dry signal**.

### 1.6 Chorus (params 70–73; `CChorus` `FUN_180024ba0`, `FUN_180024a70`, `FUN_180024af0`, `FUN_180024b50`, `FUN_180024d50`) **[DECOMP + EXEC]**

- Three tape delays with p = 0.5, no feedback, lengths `N = trunc(k·fs·0.02f)` for k = 1.1, 0.854, 1 (the 0.02 is a
  float constant promoted to double, 0.0199999995…, so at 48 kHz N = 1055, 819, 959 → delays 529.5, 411.5, 481.5
  samples ≈ 11.0, 8.6, 10.0 ms), initial tape positions 0, 0.467, 0.778.
- Rate `r = 5·x + 0.1` Hz × 0.896 / 1 / 0.689; Depth: LFO depth `x³`; Wet: `x` (linear).
- `x = L + R`; `L += b + a`, `R += b + c` (a, b, c = the three lines in the order above).
- **Wet sign [DECOMP + EXEC]:** the Wet setter stores `−x` internally and gives `+x` to the lines; every rebuild of
  the effect (each sample-rate change) re-applies the stored value, so **the chorus polarity flips at every
  sample-rate change after the parameter was set** (EXEC: same rate re-sent → no rebuild, +x; one change 44.1→48 kHz →
  −x). If FL loads the state before the first sample-rate set, the chorus starts with −x. Which sequence FL uses:
  **not confirmed**.

### 1.7 Delay (params 63–69, 112; `FUN_18002c700`) **[DECOMP + EXEC]**

- Per channel three tape delays **in series-additive form** `y ← y + Dₖ(y)`, k = 1, 2, 3:
  L lengths 2fs, 0.63·2fs, 0.41·2fs with initial positions 0, 0.27, 0.4; R lengths 2fs, 0.76·2fs, 0.54·2fs with
  0.3, 0.62, 0.87.
- Time: `p = Time² + 0.01` (max 0.99) on every line → main line delay ≈ `(Time² + 0.01)·2` s; shorter lines scale by
  0.63/0.41 (L) and 0.76/0.54 (R).
- Feedback **`−0.99·Fdbk²`** (inverted polarity); Mod Speed: LFO `20·Speed²` Hz (×1.1 on lines 2 of each channel);
  Mod Depth `Depth⁴`; Wet: line 1 `Wet²`, line 2 `Wet²·Blur`, line 3 `Wet²·Blur²`.
- Speed smoothing starts from the p = 0.5 speed of the construction, so the first enabled block glides toward the set
  time (0.005 per sample).
- **Index 112 (no name): tempo sync** 1…9 → `p = 0.5/(BPM·r)`, r = 1/15, 1/30, 1/45, 1/60, 1/120, 1/180, 1/240,
  1/480, 1/960, i.e. delay `= 2·p` s = 15, 30, 45, 60, 120, 180, 240, 480, 960 / BPM seconds (1/16, 1/8, 3/16, 1/4,
  1/2, 3/4 of a bar, 1, 2, 4 bars of 4 beats — **inference** for the musical names), clamped by p ≤ 0.99. BPM comes
  from FL's tempo event (wrapper ProcessEvent `FUN_180003410`, event 0, float value) via synth `+0xc28`
  (EXEC: tempo event 120 and 97 BPM, sync 4 and 2, match).

### 1.8 Reverb (params 74–77; class `fxReverb`) **[DECOMP + EXEC]**

An 18-parameter plate-style reverb (`FUN_180025f40` constructor, `FUN_1800258f0` defaults, `FUN_180026530` parameter
conversion, `FUN_180025990` buffers, `FUN_1800273e0` coefficients, `FUN_180028f50` process, `FUN_180026320` reset).
Internal parameters are converted to integers `floor(x·a + b)`; Sakura fixes all of them except three:

| Reverb param | Sakura value | Integer / meaning |
|---|---|---|
| Input (0) | default 0.8575 | 0 → 0 dB |
| Low cut (1) | default 0.01878 | 75 Hz (1st-order HP) |
| High cut (2) | **Color³** | `floor(216x³ + 5.5)`·100 Hz (500 Hz…22.1 kHz; 221 = off) |
| Predelay (3) | 0 | 0 ms (1-sample buffer) |
| Room size (4) | default 0.495 | 50 |
| Diffusion (5) | 1.0 (set at build) | 100 % |
| Bass multiplier (6) | default 0.2857 | 10 → ×1.0 |
| Crossover (7) | default 0.2405 | 500 Hz |
| Decay (8) | **0.6·Decay³** | `T60 = floor(119.4x³ + 1.5)/10` s (0.1…12 s) |
| High damping (9) | 1.0 (set at build) | 221 = off |
| Dry (10) | default 0.8575 | 0 dB |
| ER level (11) | default 0.774 | −3.5 dB (×0.5 inside) |
| Reverb level (12) | **0.999·Mix + 0.001** | `floor(421(0.999x + 0.001) − 360.5)`/10 dB, ×0.3 inside; Mix 0 → silent, Mix 1 → +6 dB |
| Width (13) | 0.5 | 100 % |
| mutes (14–17) | 0 | off |

Signal flow per sample (fs = host rate, all float32; full detail in `py/sak/sakura_reverb.py`):
1. `m = (gin·R + gin·L)/2`; one-pole LP (High cut: coefficient `1 − (b − √(b² − 1))`, `b = 2 − cos(2π·f/fs)`);
   first-order HP (Low cut: `(x − AP(x))/2`, all-pass coefficient `(tan(πf/fs) − 1)/(tan(πf/fs) + 1)`).
2. Four input all-passes (lengths `S·{142, 107, 379, 277}/20000`, `S = (room + 15)·fs/115`, integer arithmetic) with
   gains `g₁ = exp(−49.34·(L₀ + L₁)/(diff·fs))`, `g₂ = exp(−31.40·(L₂ + L₃)/(diff·fs))` (units 2 and 4 use g₂ on the
   input side and g₁ on the feedback side); the output of the first unit feeds the early-reflection buffer.
3. Predelay, then a two-half tank: each half takes the other half's end × g, a bass shelf (first-order all-pass at
   the crossover, coefficient `(t − r)/(t + r)`, t = tan(π·f_x/fs), r = min(g_bass/g, 1); shelf gain `(g_bass/g − 1)/2`),
   the damping LP (High damping, off in Sakura), a decay all-pass (`g₃ = exp(−3.454·2.857·(L₄ + L₈)/(fs·diff))`),
   a delay, × g, a second all-pass (`g₄ = exp(−3.454·1.961·(L₆ + L₁₀)/(fs·diff))`) and a delay; tank lengths
   `S·{672, 4453, 1800, 3720 | 908, 4217, 2656, 3163}/20000`; decay gain
   `g = exp(−6.9078·(L₅ + L₇ + L₉ + L₁₁)/(fs·T60))^(1/4)`.
4. Output: 7 taps per side read from the tank delays, offsets `S·k/20000`. With `κ(w) = (1 − g)·w + g`:
   left = `+1·[266] − κ(0.25)·[2974] − κ(0.7)·[1913] − κ(0.2)·[2496] + κ(0.6)·[1690] + 1·[187] + κ(0.5)·[1566]`,
   right = `+κ(0.25)·[3627] + 1·[353] − κ(0.7)·[1728] − κ(0.2)·[3173] − κ(0.6)·[1811] + 1·[335] + κ(0.5)·[621]`
   (which tank delay each tap reads is in the model, `Reverb.process`); × the reverb level (smoothed 0.001/sample,
   set directly on the first sample).
5. Early reflections: 8 tap positions per side are computed (`trunc(k·S_E/29400)`, `S_E = (room + 3)·fs/103`,
   k from tables at `0x180074190`/`0x1800741d0`), but **only the first tap of each side gets a non-zero gain**
   (`0.5·m/T_L0`, `0.36·m/T_R0`, m = min(T_L0, T_R0)); × ER level/2.
6. `L_out = dry·gin·L + (w_L + w_R) + (w_L − w_R)·width`, `R_out = dry·gin·R + (w_L + w_R) − (w_L − w_R)·width`.

So in Sakura "Reverb ON" with Mix 0 still adds the ER tap (−3.5 dB × 0.5) to the dry signal.

### 1.9 Verification against the DLL **[EXEC + model]**

Voice and matrix (`py/sak/battery_mod.py`, DLL vs `sakura29.render(post=True)`, lag 1, float32 DLL vs float64 model):

| Case | rel. RMS error |
|---|---|
| EG → level / level with release / pitch / pitch inv / Hi-Cut inv / Lo-Cut / damp / decay inv / sep | ≤ 3.7·10⁻⁴ |
| LFO → pitch (host blocks 256 and 100) / level / sep; EG → LFO amount (dest 0) | ≤ 8.8·10⁻⁴ |
| velocity → level, note → Hi-Cut, release gate → damp, EG → noise rate, EG/vel → Hi-Cut A/D | ≤ 4.3·10⁻⁴ |
| four slots on destinations 8, 9, 11, 12 | 6.4·10⁻⁴ |
| mod wheel → pitch, pressure → level, pitch bend handler → pitch | ≤ 3.7·10⁻⁴ |

Effects (`py/sak/battery_fx.py`, DLL with effect ON vs model applied to the DLL's own effect-OFF output):

| Case | rel. RMS error |
|---|---|
| resonators (1 and 3 on) 48 kHz / 44.1 kHz | ≤ 2.9·10⁻⁴ |
| chorus (2 settings, 48 kHz; 44.1 kHz) | ≤ 6.7·10⁻⁷ |
| chorus after one sample-rate change (wet −x) | 5.7·10⁻⁷ |
| delay free (2 settings) / tempo sync 4 @ 120 BPM, 2 @ 97 BPM / 44.1 kHz | ≤ 2.1·10⁻⁶ |
| reverb (3 settings, 48 kHz; 44.1 kHz) | ≤ 4.2·10⁻⁸ |
| full chain: matrix (EG → Hi-Cut, LFO → pitch, vel → level) + 2 resonators + chorus + reverb + delay, note-off (`battery_all.py`) | 3.4·10⁻⁴ |

---

## 2. Fruity Squeeze

### 2.1 State and parameter laws (`FUN_006d35c0`, DFM) **[DECOMP + DATA + EXEC]**

`0xD5` = i32 version 0 + 12 × i32 raw control values, index order (round 15 §6). The DSP uses `v/max` with the control
maxima of the DFM (not `(v − min)/(max − min)`):

| Idx | Control (DFM max, default) | Engine value |
|---|---|---|
| 0 | Gain (160, 127) | `gain = v/127` (float) |
| 1 | Mix (128, 64) | `dry = (128 − v)/128`, `wet = v/128` |
| 2 | Steps (32768, 1; min 1) | number of levels `Lq = 32768^(1 − v/32768)` (float); quantiser active when `Lq < 32768` — **note: the default v = 1 gives Lq ≈ 32757, so a light quantisation is always on** |
| 3 | Filter on (1) | flag |
| 4 | Filter type (4) | 0 low-pass, 1 high-pass, 2 band-pass, 3 notch, 4 peak |
| 5 | Filter resonance (65536, 32768) | `r = v/65536` |
| 6 | Filter cutoff (65536, 32768) | `x = v/65536` |
| 7 | Filter position (1) | 0 pre (before quantiser/puncher), 1 post |
| 8 | Relation (3) | 0 free; 1 Impact := Preserve; 2 Impact := 2·Preserve; 3 Impact := Preserve ÷ 2 (integer shift) — applied to the raw values whenever Relation or Preserve changes (`FUN_006d4580`) |
| 9 | Impact (44100, 0) | `I = (44101^(v/44100) − 1)·fs/44100` samples (fs = host rate) |
| 10 | Preserve (44100, 7500) | `P = (44101^(v/44100) − 1)·fs/44100` samples |
| 11 | Amount (65536, 0) | `a = v/65536` (linear gain in the impact phase) |

Manual: Impact = "The number of samples that are forced by the Puncher section to the nominal value"; Preserve = "The
number of samples that are left unaffected by the Puncher section" **[DOC]**. The manual describes only a low-pass and a
"Squarize" knob; the DLL has the five filter types above and the "Steps" quantiser (**[DECOMP]** wins).

### 2.2 Signal path (`FUN_006d40c0`) **[DECOMP + EXEC]**

Per block (stereo interleaved float32):
1. **Silence bypass:** if every sample of the block has |x| ≤ 10⁻⁶, the output is the input copied unchanged (gain and
   mix are *not* applied) (`FUN_005dfe00`).
2. Copy to a work buffer w; **pre filter** if Filter on and position pre.
3. **Quantiser** if `Lq < 32768`: `w = round(w·Lq)/Lq` (round half to even, float32).
4. **Puncher** if `I > 0`: a two-state sample counter; state P ("preserve") passes w unchanged for as long as its counter
   ≤ P (counter +1 per sample), then switches to state I ("impact") where w is multiplied by `a` while its counter ≤ I,
   then back. At each switch the fractional overshoot `φ` (counter − length) becomes the new state's counter, and the
   switch sample uses the blended gain `a·φ + (1 − φ)` (P→I) or `a·(1 − φ) + φ` (I→P). Counters/state are reset only by
   `FPD_Flush` (dispatcher 2) and at creation (start in state P, counter 0). So the puncher is a periodic, non-signal-
   dependent square gate: about `P + 1` samples at unity, `I + 1` samples at gain `a`.
5. **Post filter** if Filter on and position post.
6. `out = (in·dry + w·wet)·gain`.

### 2.3 Filter (`FUN_006d4620`, `FUN_006d09a0`, `FUN_006d07d0`, `FUN_006d0820`) **[DECOMP + EXEC]**

Chamberlin state-variable filter per channel, **run twice per input sample with the same input** (2× oversampling by
repetition); each tap is the mean of its two values (×0.5 each). Per pass, with states low ℓ and band b:
`t = x − q·b`, `ℓ += f·b`, `h = t − ℓ`, `b += f·h`; outputs low ℓ, band b, high h, notch t (= ℓ + h), peak `ℓ − h`.
Values with |v| < 2⁻²⁴ (`DAT_00716fe4`) at the input and at `t` are flushed to 0.
- Cutoff `Fc = 50·(F_hi/50)^x` Hz, clamped to [50, F_hi], `F_hi = 0.75·44100 = 33075` (log interpolation computed in
  float: `exp(ln 50 + x·(ln F_hi − ln 50))`).
- `f = 2·sin(π·min(Fc/(2·44100), 0.25))`; `q = min(2, 2/f − f/2, 2·(1 − r^0.25))`.
- **The design sample rate is always 44100:** the rate setter `FUN_006d0ab0` stores its own previous rate back
  (the host rate argument is ignored), so the object keeps the 44100 of its constructor (`DAT_006d3500`). At other host
  rates the actual cutoff scales by fs/44100 (EXEC: the model with a fixed 44100 matches the DLL bit-exactly at 48 and 96
  kHz; with the host rate it does not). Filter states are never cleared (not even by Flush).

### 2.4 Verification **[EXEC + model]**

`battery_sq.py` (`squeeze_model.render` vs `effd.exe` + the installation DLL; 8192-sample sine/noise/saw/impulse, block 512):

| Case | rel. RMS error |
|---|---|
| default; wet only; gain 160 mix 0; steps 12144 / 30000 | 0 (bit-exact) |
| LP pre, HP pre, BP, notch, peak, LP post + steps, cutoff min, impulse res 1 | 0 |
| cutoff max (x = 1) | 1.3·10⁻⁶ (libm sin/exp rounding) |
| puncher impact/preserve/amount; relation =, ×2, ÷2 | 0 |
| all sections + mix 90 | 0 |
| host 48 kHz (LP + puncher), 96 kHz (HP) | 0 |
| **[DATA]** demo "DEDZII - NIGHT TERROR" insert state (gain 127, mix 64, steps 12144, LP post x = 0.872, preserve 7500, impact 0) | 0 |

---

## 3. Vintage Phaser

### 3.1 Front-end ↔ engine **[DECOMP + EXEC]**

The Delphi front-end does no DSP: Eff_Render de-interleaves, calls the engine's `processReplacing`, re-interleaves
(`FUN_006edbf0`). ProcessParam (`FUN_006eb1a0`) forwards every parameter i (0…35) to the engine's VST `setParameter(i, x)`
with **`x = clamp((raw − min)/(max − min), 0, 1)`**, min/max of the DFM control (table §3.2). EXEC: front-end with raw
values vs engine with x computed this way: **bit-identical** in 6 mixed settings (`t_front.out`). Inside the engine, VST
indices 22 and 23 are swapped onto the core's slots (`FUN_18001dc80`); below, indices are the **front-end/VST** ones.

**State (`FUN_006ea8e0`)**: version 3 = i32 3 + 36 × i32 raw values in index order + 1 byte (an editor toggle, no DSP);
version 2 = same without the byte; **versions < 3 set 33 (Engine), 34 (Drive) and 35 (Invert) to raw 0, i.e. old presets
load as engine V1, drive minimum, feedback not inverted**; version 1 stores 32 values in the order
`0,1,2,3,4,6,8,11,7,10,21,22,23,5,12,13,14,15,16,17,18,19,20,25,26,27,28,29,30,31,32,9` (table `0x742a30`) and takes 24
(noise gate) from the control default. **[DECOMP]** (v1/v2 loading not executed).

### 3.2 Parameters (`FUN_180018cd0`, `FUN_180018a00`, `FUN_180018b80`, DFM) **[DECOMP + EXEC]**

x = normalised value.

| Idx | Control (DFM min…max) | Law |
|---|---|---|
| 0 / 1 | Min / Max frequency (125…2048000) | `1.25·2^(14x)` Hz (1.25 Hz…20.48 kHz); the two are sorted, so min > max is allowed |
| 2 | Feedback (0…6000) | `fb = 0.9x`, negated if 35 ≥ 0.5 |
| 3 | Manual (switch) | x ≥ 0.5 → LFO off, fixed frequency from 4 |
| 4 | Manual frequency (125…2048000) | `1.25·2^(14x)` Hz |
| 5 | HQ (switch) | oversampling ×8 if x ≥ 0.5, else ×4 (always oversampled) |
| 6 | LFO speed (1…245760) | free: `rate = lo·(hi/lo)^x`, lo/hi = sorted(`R29`, `R30`); synced: see §3.4 |
| 7 | Tempo sync (switch) | |
| 8 | LFO waveform | x < 0.5 triangle, else sine |
| 9 | Tension (0…6000) | `k = 0.1·(e^(8.7557·x) − 1)` (x = 0 → off); see §3.4 |
| 10 | Start phase (0…6000) | cycles; used only with tempo sync |
| 11 | L/R phase difference (0…6000) | cycles added to the right LFO phase |
| 12 / 13 | Delay on / keep pitch | switches (13: x > 0.5) |
| 14 / 15 | Delay mix / stereo (0…6000) | x / s |
| 16 | Delay time (1…768) | free: `x·fs` samples (= (raw − 1)/767 s); synced: `1 + trunc(767x)` steps of 1/192 beat (= raw steps) |
| 17 | Delay tempo sync | |
| 18 | Delay feedback (0…6000) | x |
| 19 / 20 | Delay low-pass (3200…204800) / high-pass (2000…204800) | `320·2^(6x)` Hz / `20·2^(10x)` Hz |
| 21 | Mix (0…100) | m |
| 22 | Mono input | x ≥ 0.5 → L = R = (L + R)/2 |
| 23 | Noise (0…6000) | x = 0 → off, else level `0.35·10^(2.5x − 4.5)`; gate threshold 8·level |
| 24 | Noise gate | switch |
| 25 / 26 | Colour low-shelf gain (0…6000) / frequency (5000…650000) | `30x` dB / `50 + 600x` Hz |
| 27 / 28 | Input HP / Feedback HP (2000…80000) | `195x³ + 5` Hz (5…200 Hz) |
| 29 / 30 | Rate frequency 1 / 2 (25…150000) | `149.975x⁴ + 0.025` Hz |
| 31 / 32 | LFO max depth scale / pull-down (0…6000) | §3.4 |
| 33 | Engine | x ≥ 0.5 → V2, else V1 |
| 34 | Drive (5000…650000) | V2: `10^(1.5x − 0.75)` (−15…+15 dB); V1: 1 |
| 35 | Invert feedback | switch |

Front-end defaults after creation (raw, `effd` log): 0 = 896070, 1 = 1588996, 2 = 3000, 4 = 125, 6 = 1, 15 = 3000,
16 = 1, 19 = 3200, 20 = 2000, 21 = 50, 26 = 5000, 27 = 2000, 28 = 2000, 29 = 25, 30 = 150000, 31 = 6000, 33 = 1,
34 = 5000, all others 0 **[EXEC]**.

### 3.3 Signal path per host sample (`FUN_180018210`) **[DECOMP + EXEC]**

```
LFO/coefficient update → (mono) → colour low shelf → input HP → × g → up-sample ×4/×8 (windowed sinc)
→ per oversampled sample: phaser core (V1 or V2) and dry/wet mix m → [V2 only: IIR half-band decimation]
→ take the first of the OS samples → × 1/g → + noise → noise gate → delay → out
```
- **Shelf** = RBJ cookbook low shelf, `A = 10^(dB/40)`, `α = sin w/(2Q)`, **Q = 0.5**, at host rate (EXEC: designer
  coefficients match the formula to float precision). **Input HP** = bilinear one-pole high-pass: `t = tan(πF/fs)`,
  `b0 = 1/(1 + t)`, `b1 = −b0`, `a1 = (1 − t)/(1 + t)`. Both run as float32 direct-form-I biquads. The manual places the
  colour stage "after the phaser" **[DOC]**; in the code it is **before** it **[DECOMP + EXEC]**.
- **Drive gain g** (V2): input × g_old, then `g += (target − g)·(1 − 0.05^(20/fs))`, output × 1/g_new (so drive only
  changes the saturation of the V2 core). V1: g = 1.
- **Up-sampler** (`FUN_18001be70`): FIR of `88·OS + 1` taps, `h[i] = sinc(i/OS − 44)·max(0, sin(π·i/(88·OS)))`,
  polyphase overlap-add; latency 44 host samples. EXEC: table read from the running DLL equals the formula (max error
  1.7·10⁻⁸).
- **Mix** per oversampled sample: `y_mix = m·y_core + (1 − m)·x_up`.
- **Decimation**: V2 runs the oversampled block through a 4-lane, 6-section IIR (two all-pass branches, lanes (L,R) ×
  branch; coefficients `.rdata 0x180031650…0x1800318c0`, `FUN_180016ed0`) and, for ×8, a further z⁻²-all-pass half-band
  stage (`0x1800316b0…0x180031760`) on samples 0 and 4; the output is sample 0 of the block. **V1 has no decimation
  filter: it takes sample 0 of the oversampled block directly** **[DECOMP + EXEC]**.

### 3.4 LFO and sweep (`FUN_180001190`, `FUN_180017f90`, `FUN_18001ceb0`) **[DECOMP + EXEC]**

Once per host sample, from the phase φ ∈ [0, 1) (double):
- waveform `w(φ)`: triangle `2φ + 0.5` (φ < 0.25), `1.5 − 2φ` (φ < 0.75), `2φ − 1.5` — i.e. starts at the centre, rising;
  sine `(sin 2πφ + 1)/2`. Left uses φ, right uses `frac(φ + P11)`.
- tension: if k ≠ 0, `w ← 1 − (e^((1 − w)·ln(1 + k)) − 1)/k` (k from param 9, always ≥ 0 with the front-end range).
- depth `D = (1 − x6)(1 − x31) + x31`, offset `O = (1 − D) − x32·x6⁴`; exponent
  `e = (D·w + O)·log2(fmax/fmin)`, **frequency `f = fmin·2^e`** (2^e by a cubic approximation of 2^frac times 2^int:
  `((0.0790646598r + 0.224328086)r + 0.69659853)r + 0.999862194`). So the sweep is exponential between fmin and fmax;
  at high LFO speeds Scale (31 < 1) shrinks the depth and Pull (32) lowers the whole sweep (manual: "the smaller the LFO's
  output amplitude gets as its speed increases" **[DOC]**).
- φ advances by `rate/fs` (free) and wraps.
- **Tempo sync:** `steps = trunc((2^(7(1 − x6)) − 1)·48.370079 + 1.1)`, LFO period `steps/192` beats; increment
  `tempo/60/period/fs`; once the host transport has gone from ppq < 0 to ppq ≥ 0, φ is set at every block to
  `frac(ppq/period + x10)`.
- LFO off (Manual): f = manual frequency for both channels.

### 3.5 Phaser cores **[DECOMP + EXEC]**

**V1 ("original engine")** (`FUN_180001660`, `0x400 ≠ 1`): per oversampled sample and channel,
`u = x + fb·y_prev` → one-pole **feedback high-pass** (formula of §3.3 at fs·OS, cutoff param 28) → **4 cascaded
first-order all-passes** (double precision) `y = a·(x − y₁) + x₁`, `a = (tan(πf/(fs·OS)) − 1)/(tan(πf/(fs·OS)) + 1)`,
f = the LFO frequency of that channel. Linear; y_prev is the 4th stage output.

**V2 ("OTA simulation")** (`FUN_1800176b0`): 4 zero-delay-feedback (TPT) one-pole all-pass stages with a saturating
non-linearity, solved with one global feedback loop. Per channel:
- `x_c = f·π/(fs·OS)`, `c = x_c(2.47168851 − 0.189759687x_c²)/(2.46740103 − x_c²)` (rational tan), `G = c/(1 + c)`,
  `A = 2G − 1`, `B = 1 − G`; feedback-HP coefficient `G₂` from the same formula at `195x₂₈³ + 5` Hz;
  `K = 1/(1 − A⁴·fb)`.
- stage states s₁…s₄, HP state s_h: predicted loop output
  `S = (((2s₁B·A + 2s₂B)·A + 2s₃B)·A + 2s₄B) − s_h·(1 − G₂)`; first stage input `v₁ = (S·fb + x)·K − s₁`.
- each stage k: `σ = T(clamp(v_k, −5, 5))`, `T(v) = v(v⁶ + 378v⁴ + 17325v² + 135135)/(28v⁶ + 3150v⁴ + 62370v² + 135135)`
  (Padé tanh); `ℓ = s_k + G·σ`; `s_k ← ℓ + G·σ`; stage output `y_k = ℓ − B·σ` (= s_k(old) + (2G − 1)σ); next input
  `v_{k+1} = y_k − s_{k+1}`.
- output `y₄`; HP state `s_h ← s_h + 2G₂(y₄ − s_h)`.
The tan/tanh constants are runtime-initialised vectors at `0x18003bd20…0x18003bdd0` (read from the running DLL
**[EXEC]**).

So the Vintage Phaser is a **4-stage** phaser in both engines (manual: no stage count **[DOC]**), V2 with
drive-dependent tanh saturation in every stage.

### 3.6 Noise, gate, delay **[DECOMP + EXEC]**

- **Noise** (after the core, before gate/delay): per channel a 32-bit LCG (`s ← s·0x0BB38435 + 0x3619636B`, signed ×2⁻³¹;
  same constants as Sakura's) filtered by an RBJ low shelf **+18 dB, Q 0.5 at `280·2^(3.215·w)` Hz** (w = that channel's
  shaped LFO value, 0 when the LFO is off → 280 Hz), redesigned every sample, × level. Seeds: left = a global counter
  (initial **239894** at DLL load), right = left + 0x1099e; the global advances by 0xb66ad per engine instance — so the
  exact noise sequence depends on how many instances were created before (statistically equivalent otherwise).
- **Gate** (24): open (gain 1, hold counter = 440 samples) whenever |out L|, |out R|, |noise L + in L| or |noise R + in R|
  ≥ 8·noise level; after the hold, gain × 0.99965 per sample (`0x3f7fe931`). With noise off the threshold is 0 → always
  open.
- **Delay** (12): 65536-sample ring per channel; write `in + fb·HP(LP(tap))`; tap: linear-interpolated, delay time
  smoothed per sample by `1 − 0.1^(10/fs)` (pitch glides on time changes); **keep pitch** (13) instead cross-fades between
  two fixed integer taps over 100 ms (`1/(0.1·fs)` per sample) when the time changes. LP/HP are bilinear one-poles
  (tanf(πF/fs)). **Output: `L = mix·tapL·g(1 − s) + g(s)·L_in`, `R = mix·tapR·g(s) + g(1 − s)·R_in`** with
  `g(u) = (2.58557272 − 1.17114568u)·u` (g(0.5) = 1, g(1) = 1.414, g(0) = 0) — the unfiltered tap is output, the
  filters act only in the feedback path. Sync time uses the host tempo.

### 3.7 Verification **[EXEC + model]**

`battery_vp.py` (`vphaser_model.render` vs `vsth.exe` + installation engine, 4096 samples, block 512, VST indices):

| Case | rel. RMS error |
|---|---|
| dry path (mix 0): V2 ×4, V1, V2 ×8 | 0 (bit-exact) |
| V1 manual f, fb 0 / fb 0.7 + FB-HP | 0 |
| V2 manual f, fb 0 / invert / HQ fb 0.5 | 0 |
| V2 fb 0.95, drive max | 9.1·10⁻⁷ |
| colour shelf + input HP / mono input | 2.2·10⁻⁶ / 4.0·10⁻⁶ |
| LFO triangle (defaults), V1, sine + phase diff, tension 0.5 / 1.0, depth scale + pull-down, rate min/max, swapped min/max | ≤ 2.6·10⁻⁶ |
| noise (LFO / manual), gate | ≤ 2.7·10⁻⁶ |
| delay: basic, stereo + LP/HP, synced, keep pitch | ≤ 1.9·10⁻⁶ |
| LFO tempo sync (playing, stopped, 93 BPM) | ≤ 2.3·10⁻⁶ |
| host 48 / 96 kHz (LFO + tension + noise) | ≤ 8.0·10⁻⁷ |
| front-end raw values vs engine normalised (6 settings, `t_front.py`) | 0 |
| **[DATA]** demo "Stroll Up" insert state (v3, preset "Lowgrounder"), front-end vs model | 4.1·10⁻⁷ |

Residuals come from libm (exp/log/pow/tan) differences in the LFO, tension, shelf design; the arithmetic order of the
DLL's float32 biquads had to be reproduced exactly (tree-shaped sums) for bit-exactness.

---

---

## 4. BooBass — sampled bass engine

### 4.1 The "internal sample": where it lives and its format **[DECOMP + DATA]**

The sound data is **compiled into `BooBass_Eng_x64.dll` itself, in the `.data` section** — not a PE resource and not a
separate file in the installation. It is a **6-zone multisample**; each zone has an *attack* recording and a
*single-cycle loop*, both **signed 16-bit mono PCM, little-endian**, raw (no header).

- Two descriptor arrays in `.data` (VA = image base `0x180000000` + RVA), 6 entries of 16 bytes each:
  `{u64 VA of the int16 data, u64 VA of an i32 holding the byte count}` —
  **attacks at RVA `0xCB920`**, **loops at RVA `0xCB980`**. The VAs stored in the file are relative to the preferred
  base `0x180000000` (they are relocated at load time; subtract the base when reading the file).
- Per-zone constants: **root pitch** i32 cents at RVA `0xCB9E0`, **attack trim** f32 dB at RVA `0xCB9F8`,
  **loop trim** f32 dB at RVA `0xCBA10`.

| Zone | root (cents above C0) | attack: RVA / file offset / samples | loop: RVA / file offset / samples | attack dB / loop dB |
|---|---|---|---|---|
| 0 | 1100 (B0) | `0x7A000` / `0x77A00` / 25 066 | `0x863E0` / `0x83DE0` / 297 | 0.666 / 4.0 |
| 1 | 1600 (E1) | `0xA3FC0` / `0xA19C0` / 27 087 | `0xB1370` / `0xAED70` / 389 | −0.333 / 3.5 |
| 2 | 2300 (B1) | `0x86640` / `0x84040` / 30 322 | `0x95330` / `0x92D30` / 259 | −0.75 / 3.0 |
| 3 | 2800 (E2) | `0xB1680` / `0xAF080` / 30 802 | `0xC0730` / `0xBE130` / 194 | −1.5 / 3.25 |
| 4 | 3500 (B2) | `0x95540` / `0x92F40` / 29 795 | `0xA3E10` / `0xA1810` / 129 | −2.0 / 3.5 |
| 5 | 4000 (E3) | `0xC08C0` / `0xBE2C0` / 22 456 | `0xCB840` / `0xC9240` / 97 | −3.0 / 3.0 |

- **Attack sample rate = 16 kHz** **[DECOMP]**: the attack read step is `2^((p − root)/1200)·16000/fs`.
  **[DATA, throwaway analysis in `r29/py/anal.py`, nothing written out]**: the attack periods measured at 16 kHz are
  515, 388, 258, 194, 129 samples (31.07, 41.24, 62.02, 82.47, 124.0 Hz) and 290 ≈ 3 × 97 for zone 5 — matching the root
  notes B0, E1, B1, E2, B2, E3 within the measurement resolution; the loops are one period each.
- **Loop cycle → 16 384-point wavetable** **[DECOMP `FUN_1800034f0`]**: at the first sample-rate set-up each loop cycle
  (int16 × 2⁻¹⁵) is resampled *periodically* to exactly 16 384 points with a 4-point Catmull-Rom cubic (read position
  advancing by `n/16384` in float32), multiplied by **−0.5·10^(−loopdB/20)** (note the **polarity inversion**), and
  stored with 4 guard points on each side. Attack samples are converted to float (× 2⁻¹⁵) and multiplied by
  **+0.5·10^(−attackdB/20)**, with 4 guard copies of the first/last sample (`FUN_1800051f0`).
- Throwaway SHA-256 (first 16 hex digits) of the raw int16 bytes, for validation only — attacks:
  `d8c959c6e22ae4d9`, `48f410aa0ac6031c`, `3ec16540e650835a`, `5fd30eb5108a68ef`, `61f6fd537b39286a`, `043385e41a6a0c89`;
  loops: `e6fa8a588e4b7408`, `0072cec6a76b17ed`, `43d7d1c33553dc90`, `e260ab5f58c7471f`, `aa6d99676da578eb`,
  `a15ea59a776237b3`.
- No audio data was copied into the LYNX repository or docs; the model reads it from the installation DLL at run time.

### 4.2 Pitch and zone selection **[DECOMP + EXEC]**

- The engine takes FL's per-voice **`FinalLevels.Pitch`** (cents, `TVoiceParams + 0x1C`) and forms
  **`p = trunc(Pitch + 2400)`**, clamped to **≤ 9600** (no lower clamp). `p` is cents above
  **16.3547 Hz** (= 267 955.84/16 384; C0 = 16.3516 Hz, +0.33 cent).
- **Zone** (at note-on only): the zone whose root is nearest to `p` (start from the highest root ≤ p, or zone 0; move up
  one zone if the next root is strictly closer; ties go to the lower zone). Zone boundaries: p ≤ 1350 → 0, ≤ 1950 → 1,
  ≤ 2550 → 2, ≤ 3150 → 3, ≤ 3750 → 4, else 5.
- **Loop step** (16 384-point table, per output sample): `inc = 2^(p/1200)·267955.84/fs` → loop frequency
  `16.3547·2^(p/1200)` Hz. **Attack step**: `2^((p − root)/1200)·16000/fs` attack samples per output sample (the attack is
  transposed, not time-stretched). Both are recomputed at the start of every Gen_Render block from the current
  FinalLevels.Pitch (so FL pitch slides/bends follow), but the zone and the decay length stay as chosen at note-on.
- With FL's convention from round 28 §4 (`Final.Pitch = 100·(key − root) + channel pitch/fine`, root C5 = 60 by
  default; **inference** there), a key k sounds at **`65.41·2^((k − 60)/12)` Hz** — i.e. **two octaves below** the key's
  standard MIDI frequency (C5 → C2). EXEC: pitch 0 → p = 2400 (zone 2), output matches the model to 1.6·10⁻⁹.
- **Keys giving p < 0** (k < 36 with the default root): the decay length (§4.4) is then read from memory *before* the
  engine's 30 000-entry table. EXEC (44.1 kHz, `BOOPEEK`): p = −1…−4 read float bit patterns (≈ 10⁹ samples → the decay
  stage practically never ends), p = −5…−10 read 0 (no decay stage), p = −11…−16 read the attack lengths, p ≤ −17 read
  heap pointers (run-dependent). The DLL does not crash and keeps sounding. FL's behaviour for those keys is therefore
  **undefined in the engine** (not a designed law).

### 4.3 Oscillator: attack → loop **[DECOMP + EXEC]**

Per output sample (normal quality, `FUN_1800042f0`):
1. **Loop read:** with phase φ (0 ≤ φ < 16 384, reset to 0 at note-on): take `v₀ = W[⌊φ⌋]`, advance `φ += inc` (wrap by
   16 384), then `loop = v₀ + (W[⌊φ⌋] − v₀)·frac(φ)` (linear interpolation from the *previous* integer point to the new
   one, weighted by the new fraction — reproduce literally).
2. **Attack + crossfade:** while the attack position `a < L − 1` (L = attack length): `s` = linear interpolation of the
   attack at `a`; `a += attack step`; `x = a/(L − 1)` (after the increment); output `s + (loop − s)·x`. So the attack is
   **linearly crossfaded into the loop over the whole attack length** (≈ 1.4…1.9 s at the root pitch). After the attack
   only the loop sounds (x = 1).
3. × **envelope gain** (§4.4), then values with |v| < 5.96·10⁻⁸ are set to 0.

**HQ mode** (`FUN_180004640`, selected when the host's ProcessMode value has bits 0–1 ≠ 0, i.e. FL's HQ modes):
the loop value is the **box-filtered average of the table over [φ, φ + inc]** (first partial sample through the 6-point
interpolator of `FUN_180004f70`, whole samples summed, last partial sample weighted, divided by inc), the attack is
read with the same 6-point interpolator, and the EQ gain ramp (§4.5) is applied every sample instead of every 16;
otherwise identical. EXEC-verified (below).

### 4.4 Amplitude envelope, velocity, polyphony **[DECOMP + EXEC]**

- **Envelope value e** (linear segments, counts in samples, `FUN_180004e60` → `FUN_180002c80`, tick `FUN_180005460`):
  e = 0 at the very first sample; then **jump to 1**; **linear to 0.9375 over D samples**,
  `D = trunc(fs/(4.088681640625·2^(p/1200)))` (= **4 loop periods**); **linear to 0 over S samples**,
  `S = trunc(16·L·fs/44100)` (L = the zone's attack length, i.e. 8.1…11.2 s); then the voice ends (stage 4) after a
  further R samples. **Note-off:** from wherever e is (if still before the release stage), **linear to 0 over
  R = trunc(fs/55)** samples (18.2 ms); the voice is freed at the end of the Gen_Render block in which the envelope
  finishes (the host's slot 5 is then called with the voice handle).
- **Gain law:** `g(e)` = linear interpolation in a 49-point table indexed by `48·clamp(e, 0, 1)`:
  `T[i] = 10^((i − 48)/20)` for i = 2…47, `T[48] = 1`, and **`T[0] = T[1] = 0`** (one 8-byte store at `0x1800036e9`
  clears both; the decompiler shows only T[0]). So e maps to −(48 − 48e) dB (piecewise-linear in amplitude between
  whole dB), the 0.9375 plateau is −3 dB, and the linear e-ramps are **dB-linear (exponential) decays** reaching
  silence at e = 1/48.
- **Velocity:** TriggerVoice reads only FinalLevels Pan, Vol, Pitch (`+0x14/+0x18/+0x1C`); **InitLevels (and so the
  note velocity) are not read by the engine**. The voice level is FL's `ComputeLRVol(pan, Final.Vol)` (host slot 31)
  applied by the host when it adds the mono buffer (slot 11) — the host-side ramp of slot 11 is FL code, **not
  confirmed** here. The pan given to ComputeLRVol is smoothed per block: `pan ← 0.5·Final.Pan + 0.5·pan`.
- **Monophonic:** one voice. A new TriggerVoice while a voice sounds asks the host to kill the old voice (slot 5) and
  restarts zone, attack position, loop phase (0) and envelope; **EQ, saturation, slew and post-filter states are not
  reset** (EXEC retrigger case matches). No glide/legato (the `MonoMode` class and `FUN_1800031b0`, `FUN_180004cc0` have
  no callers).

### 4.5 Signal path after the oscillator (`FUN_1800042f0`, `FUN_1800049f0`, `FUN_180002420`) **[DECOMP + EXEC]**

Knobs (ProcessParam index 0/1/2, value 0…65535, `x = v/65535`, applied with REC_UpdateValue; defaults 0.5 set by
`CreatePlugInstance`; a band is **muted (gain 0) when x < 0.1**):

| Knob | Main gain | Extra |
|---|---|---|
| Bass (0) | `G_B = 10^((12x − 12)/20)` (−12…0 dB) | saturation mix `G_S = 10^((24x − 24)/20)` |
| Mid (1) | `G_M = 10^((10x − 10)/20)` (−10…0 dB) | (a value `1.5 − G_M` is stored but never read) |
| Treble (2) | `G_T = 10^((12x − 9)/20)` (−9…+3 dB) | — |

Per sample, with `c₁ = 0.025·44100/fs`, `c₂ = 0.155·44100/fs` (one-pole low-passes ≈ 178 Hz and 1.18 kHz at 44.1 kHz):
1. **EQ 1:** `l₁ += (x − l₁)c₁`, `l₂ += (x − l₂)c₂`; `y = G_B·l₁ + G_M·(l₂ − l₁) + G_T·(x − l₂)`.
2. **EQ 2 + saturation:** `l₃ += (y − l₃)c₁`, `l₄ += (y − l₄)c₂`; if |l₃| > 1 the **state** l₃ is set to ±1;
   `v = l₃`; low band `b = v + G_S·(sin(πv/2) − v)`;
   `t = 3·(G_B·b + G_M·(l₄ − v) + G_T·(y − l₄))`.
3. **Slew limiter:** `z += clamp(t − z, ±(x_f + 0.0328125)·44100/fs)`, x_f = crossfade position of §4.3 (rises from ≈ 0
   to 1 during the attack, then 1): the allowed slope is small at note start and ≈ 1.03 per sample afterwards. z is the
   voice output.
4. **Gain smoothing:** a knob change is ramped linearly across the next Gen_Render block (step `Δ/n·16` every 16
   samples in normal mode, `Δ/n` every sample in HQ); at note-on the current gains jump to the targets.
5. **Post low-pass (`HighLimiter` object at plugin `+0x368`):** in Gen_Render, after flushing |v| < 5.96·10⁻⁸ to 0,
   **four cascaded first-order sections** `yₖ = a(xₖ + xₖ₋₁) − b·yₖ₋₁` with `w = 0.18·44100/fs`,
   `b = 3.6w − 1.6w² − 1`, `a = (1 + b)/2` (unity DC gain; at 44.1 kHz b = −0.40384, pole ≈ 5.64 kHz per section, cascade
   −3 dB ≈ 2.56 kHz; 2.51 kHz at 48 kHz, 2.22 kHz at 96 kHz — the coefficient law is a fixed polynomial in 44100/fs, so
   the cutoff drifts with the rate). (Round 18 called the slew limiter "HighLimiter"; the class of that name is this
   4-section filter.)
6. Mono buffer → host slot 11 with ComputeLRVol(pan, Final.Vol) (§4.4).

The engine has **no internal oversampling or fixed internal rate**: everything runs at the host rate, with the constants
above scaled by 44100/fs.

### 4.6 Verification against the DLL **[EXEC + model]**

`py/battery_boo.py`: `boo.exe` mono voice buffer vs `boobass_model.render` (block 256, lag 0):

| Case | rel. RMS error |
|---|---|
| pitch 0 (C2), default knobs, 1 s | 1.7·10⁻⁹ |
| pitch −2400 / −1300 / −750 / 1200 / 2400 / 4850.5 / 7200 (clamp), all six zones | ≤ 1.8·10⁻⁸ |
| knobs max / min (muted) / bass only / mid only / treble only / 0.1 edge | ≤ 1.6·10⁻⁹ |
| note-off at 8192 (release + voice end) | 1.4·10⁻⁹ |
| retrigger (mono) / pitch change mid-note / knob ramp mid-note / block 100 | ≤ 5.3·10⁻⁹ |
| HQ mode (×3 cases) | ≤ 8.7·10⁻⁸ |
| fs 22 050 / 48 000 / 96 000 | ≤ 9.2·10⁻⁸ |
| 3 s note; 9 s full decay at pitch 2400 (natural end: last non-zero sample 350 823 in both) | ≤ 1.6·10⁻⁷ |

Residuals are float32 transcendental rounding (pow/sin) only.

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| S1 | Sakura EG | linear A/D/S/R (A 10x³+0.001, D 50x⁵+0.001, S x², R 50x⁵+0.1 s), stepped per ≤ 48-sample chunk, start 0 **[DECOMP+EXEC]** |
| S2 | Sakura LFO | one global `cos(2πφ)`, rate 10x³+0.01 Hz, updated once per host block, φ = 0 at reset, free running **[DECOMP+EXEC]** |
| S3 | Sakura matrix | all 8 sources and 15 destinations decoded and verified (≤ 8.8·10⁻⁴); pitch uses the previous chunk's sources; pitch-bend scaling on the FL side **not confirmed** **[DECOMP+EXEC]** |
| S4 | Sakura resonators | 8 × (two 100 ms tape combs in series, p = 1 − Frq^0.1, ×1.05, fb 0.9999√Fdbk, LP 0.5) on L+R, alternate 0.75/1.25 weights, HP 32 Hz, added to dry **[DECOMP+EXEC]** |
| S5 | Sakura chorus | 3 tape lines (≈ 11/8.6/10 ms), triangle-smoothstep speed LFO 5x+0.1 Hz, depth x³, wet x; polarity flips at every sample-rate rebuild **[DECOMP+EXEC]** |
| S6 | Sakura delay | 3 series-additive tape lines per channel (2 s, 0.63/0.41 or 0.76/0.54), time²+0.01, feedback −0.99·Fdbk², blur levels, tempo sync table **[DECOMP+EXEC]** |
| S7 | Sakura reverb | 18-parameter plate (`fxReverb`), Sakura sets Mix → level, Decay → T60 0.1…12 s, Color → high cut; bit-level match (4·10⁻⁸) **[DECOMP+EXEC]** |
| SQ1 | Squeeze path | silence bypass → [pre SVF] → quantiser `round(x·Lq)/Lq`, `Lq = 32768^(1 − v/32768)` → puncher (periodic P/I gate, gain a in I) → [post SVF] → `(dry·in + wet·w)·v/127` **[DECOMP+EXEC, bit-exact]** |
| SQ2 | Squeeze filter | Chamberlin SVF ×2 repeated, f = 2 sin(π·min(Fc/88200, 0.25)), Fc = 50·661.5^x, q = min(2, 2/f − f/2, 2(1 − r^¼)); designed at 44.1 kHz always; LP/HP/BP/notch/peak **[DECOMP+EXEC]** |
| VP1 | Vintage Phaser structure | shelf (Q 0.5) → HP → ×drive → sinc up-sample ×4/×8 → 4-stage phaser (V1 linear all-passes / V2 ZDF tanh all-passes, global feedback 0.9x with HP) → mix → (V2) IIR decimation → ÷drive → noise → gate → delay **[DECOMP+EXEC]** |
| VP2 | Vintage Phaser sweep | f = fmin·2^((D·w + O)·log2(fmax/fmin)), triangle/sine, tension, L/R offset, rate lo·(hi/lo)^x **[DECOMP+EXEC]** |
| VP3 | Vintage Phaser parameter scaling | x = (raw − min)/(max − min) with the DFM ranges; laws in §3.2 **[DECOMP+EXEC]** |
| B1 | BooBass data | 6-zone int16 multisample (attack 16 kHz + one loop cycle) inside `BooBass_Eng_x64.dll` `.data`, descriptors at RVA `0xCB920`/`0xCB980` **[DECOMP+DATA]** |
| B2 | BooBass playback | `p = min(trunc(Pitch+2400), 9600)` cents above 16.3547 Hz, nearest zone, attack crossfaded linearly into the 16 384-point loop, dB-linear envelope, mono, no velocity **[DECOMP+EXEC]** |
| B3 | BooBass signal path | 3-band EQ (1-pole splits 0.025/0.155·44100/fs) twice, sin saturation of the low band, ×3, slew limiter, 4-section post LP; ≤ 1.6·10⁻⁷ vs DLL **[DECOMP+EXEC]** |

---

## 5. For LYNX

### 5.1 Sakura (`PhysicalModels.cpp` `SakuraInstrument`)

Reference: `r29/py/sak/sakura29.py` (`render`), `sakura_fx.py`, `sakura_reverb.py`, `battery_all.py` (`chain`).
1. **Matrix** — replace "level destination only" by the full law of §1.3: per voice, per chunk of ≤ 48 internal
   samples, compute `m_d` for d = 0…14 from the 8 slots (`amount²`, invert = negate; `m₀ = m₁ = 1` when unrouted) and
   apply the destination table; evaluate destination 3 (pitch) with the previous chunk's EG/LFO values.
2. **EG** (per voice): linear A/D/S/R with `A = 10x³ + 0.001`, `D = 50x⁵ + 0.001`, `S = x²`, `R = 50x⁵ + 0.1` s,
   stepped once per chunk (`φ += n/(48000·T)` before evaluation); start from 0 for a fresh voice.
3. **LFO** (one per instrument): `cos(2πφ)`, `φ += n·(10x³ + 0.01)/fs` once per host block (value taken before the
   increment), φ = 0 at reset, never reset by notes. If LYNX renders in fixed blocks, use the host block size it
   actually has (FL's depends on its buffer); log the block-size dependence.
4. **Effects** after the 32 Hz HP, at the host rate, in this order: resonators (§1.5), chorus (§1.6), reverb (§1.8),
   delay (§1.7), then the master gain. Implement the common "tape delay" element of §1.4 once (fractional delay
   `p(N − 2) + 3` samples with stamp interpolation, speed smoothing 0.005, wet smoothing 0.01, triangle-smoothstep
   speed modulation) and build resonator, chorus and delay from it; port the reverb 1:1 (float32 is not required:
   the float64 port will differ by ~10⁻⁷). Chorus wet sign: use `+Wet` (state loaded after the sample rate) and log
   that FL flips it at each later sample-rate change (§1.6).
5. Delay sync (index 112): `p = 0.5/(BPM·r)` with the table of §1.7, BPM from the project tempo.
6. Remove "EG/LFO shapes, the other destinations, resonators, chorus, reverb and delay were not decoded" from STATUS
   "Still approximated" once the C++ port matches the Python reference (same parameters, 48 kHz) to about −60 dB.

### 5.2 Fruity Squeeze and Vintage Phaser

Reference implementations: `r29/py/squeeze_model.py` (`Squeeze.block`, `render`) and
`r29/py/vphaser_model.py` (`VPhaser.tick`, `block_start`, `render`) — bit-exact or ≤ 4·10⁻⁶ vs the DLLs.

1. **Fruity Squeeze → new `squeeze` effect** (replace distortion + filter): read the 12 ints; per block: if all
   |x| ≤ 1e-6 pass through; `gain = v0/127`, `dry = (128 − v1)/128`, `wet = v1/128`; `Lq = 32768^(1 − v2/32768)`,
   quantise with `round_half_even(x·Lq)/Lq` when `Lq < 32768`; puncher with `P = (44101^(v10/44100) − 1)·fs/44100`,
   `I = (44101^(v9/44100) − 1)·fs/44100` (I recomputed from P by Relation v8: 1 → I_raw = P_raw, 2 → 2·P_raw,
   3 → P_raw >> 1), amount `v11/65536`, counters as in §2.2; SVF filter (v3 on, v4 type, v7 pre/post) with
   `Fc = 50·(33075/50)^(v6/65536)`, `f = 2 sin(π·min(Fc/88200, 0.25))`, `q = min(2, 2/f − f/2, 2(1 − (v5/65536)^0.25))`,
   two passes per sample, mean of the two outputs; coefficients computed for 44.1 kHz regardless of the project rate.
2. **Vintage Phaser → new `vintagePhaser` effect** (replace the chorus approximation): read the version and 36 ints;
   for versions < 3 force 33 = 34 = 35 = 0 (engine V1); for version 1 remap with the table of §3.1. Normalise with the
   ranges of §3.2 and implement §3.3–3.6: shelf Q 0.5 (+30x dB at 50 + 600x Hz), one-pole HP `195x³ + 5` Hz, drive
   `10^(1.5x − 0.75)` (V2), ×4/×8 windowed-sinc up-sampling (`88·OS + 1` taps), V1 = 4 linear first-order all-passes
   with feedback `0.9x` through a one-pole HP, V2 = 4 TPT all-passes with Padé-tanh on each stage difference and the
   global ZDF feedback of §3.5, mix per oversampled sample, V2 half-band decimation (coefficient vectors of §3.3,
   or any good half-band if exactness is not needed), LFO of §3.4 (`f = fmin·2^((D·w + O)·log2(fmax/fmin))`), noise,
   gate, delay of §3.6. A simpler first step that keeps the timbre: run at the host rate without oversampling only for
   V1 (linear; **inference**: without oversampling only the all-pass frequency warping and the sinc latency of 44 samples
   change) — log it as an approximation.
3. Remove "Squeeze, Vintage Phaser: approximations" from STATUS.md only after the C++ port matches the Python references
   (same parameters, 44.1 kHz) to about −60 dB.

### 5.3 BooBass

Replace `synthFromBooBass` (saw + EQ approximation) by a dedicated monophonic voice:
1. **Load the data at run time from the user's installation** (never ship it): open
   `<FL>/Plugins/Fruity/Generators/BooBass/BooBass_Eng_x64.dll`, check its SHA-256 equals
   `4e10efcd…db51705f` (otherwise fall back to the current approximation and log it), parse the PE section table
   (RVA → file offset), read the 6 + 6 descriptors at RVA `0xCB920`/`0xCB980` (`u64 VA`, `u64 VA of i32 bytes`;
   subtract the image base `0x180000000`), the int16 LE data, the roots at `0xCB9E0` and the trims at `0xCB9F8`/`0xCBA10`.
   Sanity checks: roots {1100,1600,2300,2800,3500,4000}, sample counts as in §4.1.
2. **Prepare (per sample rate):** attack[z] = int16/32768 · 0.5·10^(−attdB/20); loop[z] = periodic Catmull-Rom
   resampling of one cycle to 16 384 points (step n/16384) · (−0.5)·10^(−loopdB/20); gain table T (T[0] = T[1] = 0).
3. **Note-on:** `p = min(trunc(FinalPitchCents + 2400), 9600)` with FinalPitchCents = 100·(key − root) + channel
   pitch/fine (§4.2, round 28 §4) — i.e. key k sounds `65.41·2^((k−60)/12)` Hz; pick the nearest zone; φ = 0, a = 0;
   envelope 0 → 1 → (0.9375 over D = fs/(4.0887·2^(p/1200))) → (0 over 16·L·fs/44100) → end. For p < 0 the DLL is
   undefined; LYNX must pick a behaviour (e.g. clamp the decay index at 0) and **log it as a LYNX choice**.
4. **Per sample:** §4.3 (linear-interpolated loop and attack, linear crossfade over the attack), × g(e), EQ 1, EQ 2
   with sin saturation and ×3, slew limit `(x_f + 0.0328125)·44100/fs`, then the 4-section post LP (`w = 0.18·44100/fs`).
   Note-off: linear release over fs/55 samples. Mono: retrigger restarts oscillator/envelope only.
5. **Output level:** multiply the mono voice by FL's ComputeLRVol(pan, Final.Vol) as for Sakura/Drumpad (round 28 §4.4:
   `Vol·cos((1+pan)π/4)`, `Vol·sin((1+pan)π/4)`); velocity has no effect inside the engine.
6. Knobs from the `0xD5` state (round 10 §5: u32 version 1 + 3 × i32): gains of §4.5, muted below x = 0.1.
7. Reference implementation: `r29/py/boobass_model.py` (`BooBass`, `render`) — port 1:1 and compare at 44.1/48 kHz
   to the Python output (target ≤ −120 dB, as the model vs DLL residual is ~10⁻⁸).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| Sakura `FUN_180018910`, `FUN_1800189b0`, `FUN_180018b30`, `FUN_180018b50`, `FUN_180018b80` | EG shape and timing | DECOMP + EXEC |
| Sakura `FUN_180030720` (`0x180030789…0x1800307f0`), `FUN_18005a040` | global LFO: cos, per-host-block update | DECOMP + EXEC |
| Sakura `FUN_18002e470`, `FUN_18002da50`, MIDI handlers `FUN_180007a20…7b30` | matrix sources/destinations, order of evaluation | DECOMP + EXEC |
| Sakura `FUN_18002f4c0` cases 39–77, 112; names `FUN_18002eaa0` | FX parameter laws | DECOMP |
| Sakura `FUN_18002c450`, `FUN_18002c700` | FX build, order, delay laws and sync table | DECOMP + EXEC |
| Sakura `FUN_1800236f0`, `FUN_1800239b0`, `FUN_180025330`, `FUN_180025570`, `FUN_1800257e0` | tape/string delay elements | DECOMP + EXEC |
| Sakura `FUN_180024f20`, `FUN_1800251e0` | resonator bank | DECOMP + EXEC |
| Sakura `FUN_180024ba0`…`FUN_180024d50`, constant `0x180077d70` (float 0.02) | chorus, wet sign | DECOMP + EXEC |
| Sakura `FUN_180025f40`, `FUN_1800258f0`, `FUN_180025990`, `FUN_180026530`, `FUN_1800273e0`, `FUN_180028f50`, `FUN_180026320`, tables `0x180074190`/`0x1800741d0` | reverb | DECOMP + EXEC |
| Sakura `FUN_180003410`, `FUN_1800078c0` | tempo event → delay sync | DECOMP + EXEC |
| `plug29.exe` + `battery_mod.py`, `battery_fx.py`, `battery_all.py` | tables §1.9 | EXEC |
| Squeeze `FUN_006d35c0`, consts `0x6d39d8…0x6d39fc` | parameter laws | DECOMP |
| Squeeze `FUN_006d40c0`, consts `0x6d4518…0x6d4538` | silence bypass, order, quantiser, puncher, mix | DECOMP |
| Squeeze `FUN_006d4620`, `FUN_006d09a0`, `FUN_006d07d0`, `FUN_006d0630`, `FUN_006d0820`, `FUN_006d0ab0` (`mov eax,[rbx+0x30]; mov [rbx+0x30],eax`) | SVF, coefficient laws, 44.1 kHz design rate | DECOMP + EXEC |
| Squeeze `FUN_006d2760`, `FUN_006d4580` | Flush resets puncher; relation | DECOMP |
| VPhaser front `FUN_006eb1a0`, `FUN_006edbf0`, `FUN_006ea8e0`, table `0x742a30` | normalisation, pass-through render, state versions | DECOMP + EXEC |
| VPhaser engine `FUN_18001dc80`, `FUN_180018cd0` (jump table `0x180019358`) | index swap 22/23, parameter laws | DECOMP + EXEC |
| `FUN_180018210`, `FUN_180001190`, `FUN_180001660`, `FUN_1800176b0`, `FUN_180001940`, `FUN_180016ed0` | per-sample path, LFO, V1/V2 cores, decimation | DECOMP + EXEC |
| `FUN_18001be70`/`FUN_18001bd20` | up-sampler table and ring | DECOMP + EXEC (table peek) |
| `FUN_18001b1e0`/`FUN_18001b780` | RBJ low shelf, one-pole HP | DECOMP + EXEC (coefficient peek) |
| `FUN_18001c200`, `FUN_18001cab0`, `FUN_18001ad70` | delay | DECOMP + EXEC |
| runtime vectors `0x18003bd00…0x18003bdd0`; `.rdata 0x180031650…0x1800318c0`; seed `0x18003aa00` = 239894 | tan/tanh constants, decimator coefficients, noise seed | EXEC / DECOMP |
| demo states (blobs14.pkl [0]: NIGHT TERROR / Stroll Up) | real-world settings reproduced | DATA + EXEC |
| manual pages (Fruity Squeeze, Vintage Phaser) | control meanings; shelf position stated differently | DOC |
| vtable `0x180063630`, `CreatePlugInstance` `0x1800029a0`, `FUN_180001460` | plugin interface, defaults (knobs 0.5, rate 44 100) | DECOMP |
| `FUN_1800034f0`, `FUN_1800051f0`; `.data` `0xCB920`/`0xCB980`/`0xCB9E0`/`0xCB9F8`/`0xCBA10` | table build: int16 decode, trims, loop cubic resampling to 16 384, polarity, dB/gain table (store `0x1800036e9`), decay table | DECOMP + DATA |
| `FUN_180003db0`, `FUN_180003f90`, `FUN_180002790`, constants 2400, 9600, 1200, 16000, 267955.84 | zone choice, steps, Final.Pitch use | DECOMP |
| `FUN_180004e60`, `FUN_180002c80`, `FUN_180002d80`, `FUN_180005460`, `FUN_180002e10`; 4.088681640625, 0.9375, 55 | envelope | DECOMP |
| `FUN_1800040e0`, `FUN_1800041c0`, `FUN_180004240`, `FUN_1800023a0` | knob laws, 0.1 mute, /65535 | DECOMP |
| `FUN_1800042f0`, `FUN_180004640`, `FUN_180004f70`, `FUN_1800049f0`, `FUN_180002420`, `FUN_180002270` | oscillator, EQ, saturation (sin), ×3, slew, gain ramps, post LP, HQ switch | DECOMP |
| `boo.exe` + `battery_boo.py`, `long_boo.py`, `BOOPEEK` | table §4.6, out-of-range p | EXEC |
| `anal.py` (throwaway) | root pitches/period lengths of the embedded recordings | DATA |

### Open questions

- Sakura: the FL-side scaling of the pitch-bend value given to the engine (source 4); the order in which FL sets the sample rate and loads the state (decides the chorus polarity, §1.6); the musical names of the delay sync values (§1.7, inference).
- Squeeze: none for the DSP. (The manual's "Squarize"/LP-only description is outdated relative to the DLL.)
- Vintage Phaser: loading of version 1/2 states was read but **not executed**; the global noise-seed counter depends on
  instance creation order in FL (exact noise sequence **not reproducible**, only its law); the 1-byte v3 field is an editor
  toggle (meaning **not confirmed**); FL's real ppq/tempo delivery to the engine through the front-end's VST host was
  not executed (our host supplied them directly).
- BooBass: what the Delphi front-end `BooBass_x64.dll` adds (it was not analysed: whether it
forwards host calls/params unchanged); FL's host-side behaviour of slot 11 (L/R ramp across the block) and the
semantics of FinalLevels.Pitch (round 28 inference); FL's real behaviour for keys with p < 0 (engine reads outside its
table).
