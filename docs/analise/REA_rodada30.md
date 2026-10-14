# Behavioural analysis of FL Studio 2026: round 30 (REA) — Fruity DrumSynth Live engine and Pitcher DSP

> **Nature of this document.** Continuation of `REA_rodada29.md` (and of `REA_rodada12.md` §3 for DrumSynth Live,
> `REA_rodada14/15/19.md` for Pitcher). *Clean-room* reverse engineering, behavioural only. No Image-Line code or
> pseudocode was copied: what is here are formulas and behaviours described in our own words. Addresses
> (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched (no FLEX
> pack was opened). No Image-Line audio data was copied into the repository or this document: for DrumSynth Live only
> the **location and format** of the recorded waveforms are given, so that LYNX can read them at run time from the
> user's own installation.
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
- **Tools:** Ghidra 12.1.4 headless (JDK 21): new projects `/tmp/r30gh_eng` (DrumSynth engine, full decompilation
  `r30/out/eng_all.c`, plugin code `r30/out/eng_user.c`) and `/tmp/r30gh_fe` (DrumSynth front-end, plugin range
  `r30/out/fe_plug.c`); existing project `/tmp/r14gh_Pitcher_x64.dll` (full decompilation `r30/out/pit_all.c`);
  `objdump` for disassembly (`r30/out/trig.s`, `r30/out/rend.s`); PE resource lister `r30/py/res.py`; DFM parser
  `r14/tools/dfm.py` (editor form → `r30/out/synthform.txt`).
- **Targets** (hashed in this round):

  | Plugin | DLL (SHA-256) | Language | Key functions |
  |---|---|---|---|
  | Fruity DrumSynth Live (engine) | `Plugins/Fruity/Generators/Fruity DrumSynth Live/drumsynth_eng_x64.dll` `9a5f168219c702514f07f3d568a348fc1438d9ffa287310467dd7887cc74c637` | C++ (MSVC; `TTestPlug` on `TCPPFruityPlug`) | `CreatePlugInstance` `0x180002c40`; constructor (+ "Waves" resource, wave table, noise seed) `FUN_180001060`; vtable `0x18005f460`: slot 1 Dispatcher `FUN_180001670`, 3 SaveRestoreState `FUN_180001830`, 6 ProcessParam `FUN_1800017b0`, **9 TriggerVoice `FUN_180001880`**, **10 Voice_Release `FUN_180002430`**, 11 Voice_Kill `FUN_180002380`, **13 Voice_Render `FUN_180002560`**; helpers: decay coefficient `FUN_180002a80`, level law `FUN_180002ae0`, frequency → filter coefficient `FUN_180002b50`; CRT (EXEC-consistent, see §1.9): `FUN_1800154c0` = exp, `FUN_180015970` = log, `FUN_1800158b8` = floor |
  | Fruity DrumSynth Live (front-end) | `…/Fruity DrumSynth Live_x64.dll` `bd4bf6916f94a2539cf3a8d735c11a1a6c260a622d65b188fb7bee740d259cab` | Delphi (`TFruityPlug` descendant, VMT `0x716100`) | `CreatePlugInstance` `0x7164a0`; constructor `FUN_00717b00`; VMT slots: 1 Dispatcher `FUN_00716500`, **3 SaveRestoreState `FUN_00717770`**, 4 GetName `FUN_00718c40`, **6 ProcessParam `FUN_00717ed0`**, **8 Gen_Render `FUN_00719660`**, **9 TriggerVoice `FUN_007192b0`**, 10 Voice_Release `FUN_00719540`, 11 Voice_Kill `FUN_00719580`; patch → engine `FUN_00719d70`; index maps `FUN_00719a60`, `FUN_00719b00`, `FUN_00719b40`; kit library `FUN_00714ec0`, `.dsl` reader `FUN_00715590`; RTL `FUN_00408930` = exp, `FUN_00409b50` = ln, `FUN_00409fe0` = Round |
  | Pitcher | `Plugins/Fruity/Effects/Pitcher/Pitcher_x64.dll` `965db573b967d8b0b12a00a060177d517becdd0aefe76bd1b0251aab0c13a73d` (= rounds 14/15/19) | C++ (MSVC; `CFLWrapper` + `PitcherSynth`, vtable `0x18005d340`) | setParam `FUN_18001cab0` (slot 23), defaults `FUN_18001d020`, constructor `FUN_18001c240`, buffer set-up `FUN_18001d0b0`, restart `FUN_18001d290`, process `FUN_18001d5e0` → **`FUN_18001e030`** (per-sample loop + detector front-end), **detector frame `FUN_18001e810`**, **correction target `FUN_18001ea80`**, **plain shifter `FUN_18001ed60`** + interpolator `FUN_18001ec70`, **formant (PSOLA) shifter `FUN_18001f2b0`** + grain overlap-add `FUN_18001f180`, **harmony `FUN_18001f800`**, scale `FUN_18001d890`, note mask `FUN_18001d9c0`, MIDI note in `FUN_180007a40`/`FUN_180007a80`, editor control handler `FUN_18001b3a0`; CRT (EXEC): `FUN_18002df70` = powf, `FUN_18002c134` = log2f, `FUN_180042200` = cos (EXEC-consistent) |
  | FL host (one function) | `FLEngine_x64.dll` `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` (= round 28) | Delphi | host object VMT `0x13141b8` slot 3 `0x132acd0` (integer-pan ComputeLRVol), constant pointer `0x15e3ed8` → `0x13e7564` |

Manual pages read: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20DrumSynth%20Live.htm`
(Pitcher: rounds 14/15/19). Quotes are short.

**EXEC harnesses** (our own C, Zig 0.14.1 → Windows x64). All of them call
`SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)` first, install a vectored fault
handler that ends the process, load the installation's DLL unmodified from `C:\Program Files\Image-Line\FL Studio 2026`,
drive it only through its own exported `CreatePlugInstance` and vtables with a minimal fake host, run under `timeout`,
and always end with `TerminateProcess` (DLL finalisation never runs). No process or dialog was left behind.
- `r30/run/dsl.c` → `dsl.exe`: the **DrumSynth engine** alone (round 28 `plug.c` pattern): Dispatcher SetBlockSize /
  SetSampleRate, its own ProcessParam for the 35 engine parameters (flag `REC_UpdateValue`), TriggerVoice with an SDK
  `TVoiceParams` record, Voice_Release, Voice_Render; prints the engine's noise seed (`+0x2C0`) before the first note.
- `r30/run/fed.c` → `fed.exe`: the **DrumSynth front-end** (round 29 `effd.c` pattern), which loads its own engine. Extra
  host pieces: dispatcher `0x47` returns a dummy interface whose file-search method (+0x10) adds the installation's
  `Data\Default.dsl` to the plugin's own list object through that object's own `TStrings.Add` (VMT `0x4b3318` + 0x78);
  a minimal read-only COM `IStream` feeds a project state blob to the plugin's own SaveRestoreState; host slot 3
  (integer-pan ComputeLRVol) is logged and returns L = R = 1; host slot 11 (add mono buffer to stereo with ramp) mixes
  and logs the gains it receives. Rendering through the plugin's own Gen_Render.
- `r30/run/pit.c` → `pit.exe`: **Pitcher** (round 28 `plug.c` pattern for the `CFLWrapper` framework): Dispatcher,
  the synth object's own setParameter (`PitcherSynth` vtable slot 23 = `FUN_18001cab0`), its own MIDI note on/off
  handlers (slots 1/2), the wrapper's own Eff_Render (slot 7); optional read-only peeks of synth fields per block, and
  direct calls of the DLL's own functions (`FUN_18001d890` scale mask, `FUN_18001d9c0` note mask, `FUN_18002df70`,
  `FUN_18002c134`) for the checks of §2.9.

**Models (pure Python, `python3 -I`, numpy; float32 emulated):** `r30/py/dsl_model.py` (DrumSynth: state parser, key →
patch, front-end → engine mapping incl. morph, engine trigger/render/release, level law, wave-resource loader),
`r30/py/pitcher_model.py` (Pitcher: detector, correction, scale/MIDI masks, harmony, plain shifter, formant shifter).
Drivers/batteries: `r30/py/run_dsl.py`, `battery_dsl.py` (→ `battery_dsl.out`), `battery_fe.py` (→ `battery_fe.out`),
`t_data.py` (→ `t_data.out`); `r30/py/run_pit.py` (→ `run_pit.out`), `battery_formant.py` (→ `battery_formant.out`),
`t_delay.py` (→ `t_delay.out`).

---

## 1. Fruity DrumSynth Live

### 1.1 Architecture **[DECOMP + DOC]**

- Two DLLs: the Delphi **front-end** (the FL plugin; editor, 120 patches, morph, levels/pan/sends, voice modes) loads
  the C++ **engine** `DrumSynth_Eng_x64.dll` through its exported `CreatePlugInstance` (constructor `FUN_00717b00`)
  and talks to it only through the engine's plugin vtable: ProcessParam (35 parameters), TriggerVoice,
  Voice_Release, Voice_Render (mono, per voice). The front-end's Gen_Render mixes the voices itself through FL's host.
- The manual: "DrumSynth Live supports 120 real-time patches, each on its own key from C0 to B9", "A DrumSynth
  preset saves the entire drumkit", "Most properties are read once per triggered voice"; engine credited to Paul
  Kellett (string in the front-end: "DrumSynth originally by mda") **[DOC + DATA]**.
- The front-end clears bit 21 (`0x200000`) of its own plug-in info flags at construction (`_DAT_0077015c &=
  0xffdfffff`) and its info record declares SDK version 1, 4560 parameters (= 120 × 38), 16 default voices
  **[DECOMP + DATA `0x770148`]**. Consistently, it reads the voice levels `Pan` and `Pitch` as **32-bit integers**
  (pitch in cents, §1.3). That FL therefore hands this plugin integer levels (SDK flag name "new voice params" for that
  bit) is **inference** — FL's own voice object stores floats (round 28 §4.1), and the conversion was not located.

### 1.2 State in the project (`0xD5` blob) and `.dsl` kits **[DECOMP `FUN_00717770`, `FUN_00715590` + DATA]**

Corrects round 12 §3 (the second word is the **editor's selected key**, not "the selected sound").

| Offset | Content |
|---|---|
| 0 | u32 version (written: 4; read: < 5 accepted) |
| 4 | u32 **key currently selected in the editor** (only scrolls/selects the editor; it does not choose the sound) |
| 8 | 120 Pascal strings (u8 length + bytes, ≤ 254): patch names by **slot** |
| … | 120 × 38 × int32 (`0x4740` bytes): the patch parameters by slot (§1.4) |
| … | (version > 2) u32 flags; bit 0 → front-end field `+0x146` (affects only the editor-update posts at note-on) |

- Version < 4: every patch's parameter 36 equal to −1 becomes 0. Version < 2 (old 24-patch kits): 24 names of 24 bytes,
  24 × 38 ints, morph target += 48, and the 24 patches are copied to all five 24-key groups.
- `.dsl` kit files (`Data\*.DSL` next to the plugin; `Default.dsl` first) use the same reader: u32 version, u32 (not
  used), then either 24 × 24-byte names (version < 2, 0x240 bytes) or 120 Pascal names, then the patches (`count × 152`
  bytes). `Default.dsl` (4232 B = 8 + 576 + 24·152) is version 1 **[DATA]**. LYNX does not need the `.dsl` files:
  the project state already contains all 120 patches.
- **Key → patch slot** (`FUN_00719b40`): key 48…71 → slot key − 48; key < 48 → slot key + 24; key 72…119 → slot = key;
  other keys → no patch (no voice). The inverse map (slot → key) is `FUN_00719b70`/`FUN_00719b00`. FL's note number is
  taken as `key = (Init.Pitch + 50) div 100` (§1.3) **[DECOMP]**.
- **[DATA]** "Mehran Abbasi – With You" (8 channels, v4) and the render test `Drumsynth Live.flp` (v3) (`t_data.out`):
  each channel plays a single key, and that key equals the saved selected key in all 8 demo channels (Closehat 66,
  Openhat 65, Clap 51, Percussion 72, Ride 70, Snare 71, Snare Time Gate 47) — the editor follows the last played key.
  The default kit repeats the same 24 sounds in each 24-key group, which is why round 12's "name of the selected
  index" happened to give the right sound name.

### 1.3 Front-end per-note behaviour **[DECOMP + EXEC]**

- **TriggerVoice** (`FUN_007192b0`): key as above; voice-mode cutting (below); loads the patch of that key into the
  engine (`FUN_00719d70`, with morph); rewrites the levels it passes to the engine: `Final.Pitch ← Final.Pitch −
  Init.Pitch + 6000`, `Init.Pitch ← 6000`; calls the engine's TriggerVoice with that record (Init.Vol, Final.Vol,
  Final.FCut, Final.FRes untouched).
- **Patch → engine** (`FUN_00719d70`): engine parameter 0 = 45000, parameters 2…33 = patch parameters 2…33 (24 is
  skipped; 15 = −1 is sent as 128), parameters 1 and 34 are never sent (0). **Morph:** if the patch's "Morph To" key
  (p34) is ≥ 0 and differs from the played key, each parameter k (2…33 except 15 and 24, which come from the source)
  becomes `Round(a + f·(b − a))` (a = source, b = target patch, float math, Delphi Round) with
  `m = min(1, p37/128)` and `f = 0.4m` (m < 0.25), `1.6m − 0.3` (0.25…0.75), `0.4m + 0.6` (m > 0.75) — exactly: f₁ =
  1.6m − 0.3, then `0.4m + 0.6` if f₁ > 0.9, then `0.4m` if the result < 0.1. p37 is the "Morph To Modulation" wheel
  (default 64 → f = 0.5) **[DECOMP + EXEC `battery_fe.out`: morph 20/100/128]**.
- **Voice_Release** (`FUN_00719540`): reloads the patch (with morph) into the engine, then the engine's Voice_Release.
- **Voice mode** (p24, editor "Mono/Poly/Group"): at note-on, unless the new note's mode is 1 (Poly), every held voice
  with (mode 0 and the same key) or (mode ≥ 2 and the same mode as the new note) is marked "cut". 0 = Mono, 1 = Poly,
  2…5 = Group 1…4 (order from the manual: "Mono, Poly, Group 1–4") **[DECOMP + DOC]**. A cut voice gets L = R = 0 in
  the next Gen_Render, i.e. it fades out through the host's per-block gain ramp, and is killed (host slot 5) when both
  ramp gains reached 0; the host's ramp law is **not confirmed**.
- **Gen_Render** (`FUN_00719660`), per voice and block: engine Voice_Render → mono buffer; host slot 3
  `ComputeLRVol(L, R, pan, vol)` with **`pan = Final.Pan + PAN`** (32-bit integer sum; PAN = patch p1, −64…64) and
  **`vol = Final.Vol × g(LEVEL)`**, `g(v) = (11^(v/128) − 1)/10` (exactly 1 at v = 128; `0x78046c` = ln 11 set by the
  RTL start-up `FUN_005eb840`); then host slot 11 adds it to the output with a gain ramp from the previous block's L/R
  (first block: no ramp). If p36 (send track) ≠ 0, the same buffer is also added to the host's buffer for track p36
  (host slot 37) with gains × `g(p35)`. When the engine reports the voice finished, host slot 5 kills it.
  **[DECOMP + EXEC: `fed.exe` logged pan 33 for Final.Pan 20 + PAN 13; vol 0.275505304 = 0.5·g(100), 0.5 = 0.5·g(128),
  0 = 0.5·g(0)]**.
- **FL's host slot 3** (FLEngine `0x132acd0`): converts the integer pan with `pan/64` (`0x13e7564` = 0.015625) and calls
  the same ComputeLRVol as slot 31 (`0x6a3a00`, round 28 §4.1: pan clamped to ±1, law A `L = vol·cos((1+pan)π/4)`,
  `R = vol·sin((1+pan)π/4)`) **[DECOMP]**.
- p36 = −1 (found in 117–119 patches of the v4 demo blobs) is sent to host slot 37 as track −1; what FL does with it
  is **not confirmed** (LYNX: treat as no send).

### 1.4 The 38 patch parameters (editor tags = parameter index) **[DECOMP + DFM]**

Front-end parameter `i` of slot `s` is plugin parameter `38·s + i`; the hint texts and ranges are from the editor form
`TSynthEditorForm` (`r30/out/synthform.txt`). Engine parameter k (2…33) = patch parameter k; the engine converts the
integer to a float `P[k]` in TriggerVoice (jump table `0x180002344/0x180002358`):
`P = v/65536` (k = 0, 1), `v·0.001` (4, 10), `v·0.007874` (5–9, 11–14, 17, 19–21, 33), `v·0.01` (23),
`v·0.01 + 0.5` (2, 3, 16, 25–31, 34); 15, 18, 22, 24, 32 are used as integers.

| i | Editor hint (range) | Engine meaning (P = scaled value, v = velocity term §1.5) |
|---|---|---|
| 0 | Patch Level (0…128) | front-end: `g(v)` into host vol (§1.3) |
| 1 | Patch Pan (−64…64) | front-end: added to Final.Pan |
| 2 | Tone Filter: Bass <> Treble (−50…50) | tone filter position t₀ |
| 3 | Osc1 Harmonics: Odd <> Even (−50…50) | waveshaper amount |
| 4 | Osc1 Frequency (0…1000) | Osc1 frequency x |
| 5 | Osc1 Bandwidth (0…127) | noise mode: bandwidth; tone mode: sweep start offset ("Sweep Up/Down" [DOC]) |
| 6 | Osc1 Sweep Time | sweep coefficient D(P6) |
| 7 | Osc1 Envelope Decay Time | D(P7) |
| 8 | Osc1 Output Level | level L₁ |
| 9 | Osc2 Envelope Shape: Exp <> Lin | envelope-2 target |
| 10 | Osc2 Frequency (0…1000) | Osc2 frequency x (+ FCut/2) |
| 11 | Osc2 Bandwidth | bandwidth |
| 12 | Osc2 Envelope Attack Time | attack |
| 13 | Osc2 Decay Time | decay |
| 14 | Osc2 Output Level | level L₂ |
| 15 | Waveform Select (−1…17) | recorded wave 0…17; −1 → none |
| 16 | Wave Tuning (−50…50) | wave rate |
| 17 | Waveform Mix: Noise <> Wave | wave/noise balance |
| 18 | Envelope Re-Trigger Count (0…100) | retrigger count |
| 19 | Envelope Re-Trigger Rate | retrigger period |
| 20 | Envelope Re-Trigger Decay | decay used while retriggering |
| 21 | Envelope Release Time | note-off law |
| 22 | Osc1 Phase Invert (switch) | sign of Osc1 filter feed/initial state |
| 23 | DC Click At Note-On (0…100) | initial filter state ("click") |
| 24 | Voice Mode: Mono/Poly/Group (0…5) | front-end only |
| 25…31 | Vel. > Osc1 Frequency, Osc1 Level, Osc2 Frequency, Osc2 Level, Waveform Tuning, Tone Filter, Osc1 Harmonics (−50…50) | velocity sensitivities |
| 32 | Osc1 Mode: Swept Tone / Noise Band (switch) | 0 = swept sine, 1 = noise band |
| 33 | Osc1 > Osc2 Ring Modulation (0…127) | ring amount |
| 34 | Morph To (key) | front-end only |
| 35 | Send level (0…128) | front-end: `g(v)` |
| 36 | Send track number | front-end |
| 37 | Morph To Modulation (0…128) | front-end: morph amount |

### 1.5 Engine: note-on set-up (`FUN_180001880`) **[DECOMP + EXEC]**

Notation: `fs` = sample rate (engine fields `+0x2B8` = fs, `+0x2BC` = 1/fs, set by Dispatcher 4); all arithmetic
float32 except where "double" is said; exp/log/floor are the CRT's.
- **Velocity term** `v = (2·Init.Vol − 1) + Final.FRes` (FL note velocity → Init.Vol, round 28 inference:
  velocity 100 → 1.0 → v = 1; FRes = note "Mod Y", the front-end's "Black knobs offset" name **[DECOMP `FUN_00718c40`]**).
- **Pitch ratio** `r = exp((P′·0.01 + 60 − 60)·ln2/12)` = 2^(P′/1200) with P′ = the engine's `Final.Pitch` (front-end:
  `Final.Pitch − Init.Pitch + 6000`). With FL's FinalLevels law of round 28 (`Final.Pitch = Init.Pitch + chan.Pitch`,
  chan.Pitch containing −100·root), P′ = channel pitch + fine in cents relative to root C5; the **played key does not
  transpose the drum** (inference for the FL side; the engine side is EXEC).
- **Level law** `G(x, s, v) = exp(6.9x − 4.7 + (3s − 1.5)·v)` (0 if x < 0.01): L₁ = G(P8, P26, v), L₂ = G(P14, P28, v).
  (x = 0.5 → 0.287, x = 1 → 9.03.)
- **Decay coefficient** `D(x) = 1 − exp(−e^(8.16 − 9.21x)/fs)` (double): a one-pole with time constant
  `τ = e^(9.21x − 8.16)` s (x = 0 → 0.29 ms, 0.5 → 28.6 ms, 1 → 2.86 s).
- **Frequency law → filter coefficient** `C(x, s, q)`: `f = min(16000, floor(16·e^(6.90777x + m³v))·r)` Hz with
  `m = 2.23(s − 0.5)` (so 16 Hz·1000^x, whole-Hz steps before r), `w = 2πf/fs`, `C = w − ((0.04 − 0.07q)·w + 0.3q)·w²`.
- **Osc1**:
  - noise mode (p32 ≠ 0): b = P5, `x₁ = min(1, 2b)`, damping `q = x₁²`, input gain `q·x₁`; coefficient
    `c = min(1.98 − q, C(P4, P25, q))`; if b > 0.5 the two integrator coefficients become `c(1+k)` and `c(1−k)`,
    `k = 0.999 − 4(1−b)²` (wider band); fixed frequency.
  - tone mode: q = 0, input gain 0 (the second section is not fed): an undamped resonator → **sine**; target
    coefficient `C(P4)` without velocity sensitivity (s = 0.5, q = 0), start coefficient `C(x_s, P25, 0)` with
    `x_s = 2·P5·P4` (P5 < 0.5) or `(2P5 − 1)(1 − P4) + P4`; the coefficient glides exponentially from the start to the
    target with D(P6) per sample (the sweep).
- **Click / phase** (p22, p23): the second section's initial state is `(c₀, c₀³ − 1)` with `c₀ = P23²` (for P23 > 0.9:
  `c₀ = 0.0036 ×` the second byte of the engine's current noise state, i.e. pseudo-random — manual: "At maximum, the
  offset is random"); with p23 = 0 the tone-mode resonator starts like a sine at phase 0. p22 negates Osc1's input
  gain and the two initial states. The same initial states seed Osc2's second section (not negated).
- **Osc2**: b = P11 as above (damping q₂, input gain q₂x₁, split for b > 0.5), coefficient
  `min(1.98 − q₂, C(FCut/2 + P10, P27, q₂))` (FCut = note "Mod X"; front-end name "Osc2 freq").
- **Envelope 2**: attack ramp from 0 to L₂ with increment `L₂/(fs·e^(8.5169·P12 − 9.21))` (attack time 0.1 ms…0.5 s;
  P12 ≤ 0.005 → instant); then decay toward the target `T = −L₂·E`, `E = e^(9·P9 − 7)` (shape), coefficient
  `c₂ = 1 − exp(e^(6.91 − 9.21·P13) · ln((0.03 + E)/(1 + E))/fs)` (double). Osc2 is silent once env2 ≤ 0.
- **Envelope 1**: starts at L₁, decays toward 1e-5 with D(P7) (the value used for a sample is the one before the update).
- **Retrigger** (p18 = n, clamped to 100): if n > 0, period `T_r = trunc(e^(8.5169·P19 − 9.21)·fs)` samples; while
  retriggering both decays use D(P20) (or 0 = hold when P20 ≥ 0.995); at each period end env1 := L₁, env2 := L₂ and the
  attack restarts; after the last one the normal decays are restored.
- **Noise / wave source** (§1.7): noise gain `gₙ = √fs·0.0047619·(1.0001 − P17)` (or `√fs·0.0047619` with no wave),
  wave gain `g_w = 3·P17`, wave step `trunc(min(8, 4^P16·e^(0.5·v·(2P29 − 1)³)·(22050/fs)·r)·4096)` in 1/4096 sample.
- **Tone filter** (p2, p30): `t = clamp(P2 + (P30 − 0.5)·v, 0, 1)`; t ≤ 0.5: one-pole low-pass at `20·e^(14t)` Hz
  (20 Hz…21.9 kHz) plus 1e-4 of the dry signal; t > 0.5: output = low-pass − input (an inverted one-pole high-pass) at
  `20·e^(14(t − 0.5))` Hz; coefficient `1 − exp(−2π·20·e^(14t′)/fs)` (double).
- **Waveshaper** (p3, p31): `d = clamp((2P3 − 1) + (2P31 − 1)·v, −1, 1)`; d ≥ 0: `y ↦ (1+d²)·y − a/4` plus `a·y²` for
  y ≤ 0 (a = 4.708d²); d < 0: `y ↦ (1+a)·y − a·y·|y|` (a = −6.6667d²).
- **Output gain** `G(45000/65536, 0.5, 0)·2·Final.Vol = 1.03858·2·Final.Vol`. Final.Vol is therefore applied twice
  (here and in the front-end's host vol, §1.3).

### 1.6 Engine: per-sample render (`FUN_180002560`) and note-off (`FUN_180002430`) **[DECOMP + EXEC]**

Per sample, in this order: retrigger counter; noise (+ wave) sample `n`; **Osc1**: coefficient glide; two cascaded
two-integrator sections (Chamberlin-type) sharing the coefficient: section 1 is driven by `n + n_prev`, section 2 by
`(input gain)·(u₁ + u₁,prev)` where u₁ is section 1's first integrator; Osc1 output `y₁` = section 2's first integrator;
shaped `(shaper(y₁))·env1`; **Osc2** (only while env2 > 0): same two sections on the same `n`, output `h₂`; adds
`env2 · h₂ · (1 + ring·(y₁ − 1))` (ring = P33); **hard clip to ±1** (manual: "High levels cause hard clipping");
tone filter; × output gain. Integrator update in our words: `u ← u − (q·u + w + in)·c`, `w ← w + u·c′` (c′ = c except
the split case). The voice reports "finished" when the retrigger count < 1 and env1 < 1e-4 and env2 < 1e-4.

Note-off: retriggering stops; if P21 < 0.95: env2 jumps to the attack value if it was still attacking; both decays
become `D(f·P7)` and `D(f·P13)` with `f = 0.5 + 0.5·P21` (the attack increment also becomes D(f·P13)). P21 ≥ 0.95
(release 121…127, as in all default patches) ignores note-off.

### 1.7 The recorded waveforms and the noise generator **[DECOMP + DATA + EXEC]**

- Resource **type `WAVES`, id 4003** (language 2057) of `drumsynth_eng_x64.dll`: 587 366 bytes of raw little-endian
  16-bit mono samples (293 683 samples). LYNX must read it **at run time from the user's installation** (do not ship it).
- 18 waves, table in the constructor (start, end, loop length) in units of 1/4096 sample: e.g. wave 0 = (`0x1a80d000`,
  `0x1fc62000`, `0x5455000`); the full table is `WAVE_TABLE` in `r30/py/dsl_model.py` (addresses only, no audio).
  Reading: position += step; when position > end, position −= loop length (so the wave **loops**); sample =
  linear interpolation between samples ⌊pos/4096⌋ and +1 with the 7-bit fraction `(pos >> 5) & 127`, /32768.
  At the default tuning (P16 = 0.5, velocity term 0, r = 1) the step is `2·22050/fs` source samples per output
  sample, i.e. exactly 1 at 44.1 kHz (that the recordings are 44.1 kHz material is inference).
- **Noise**: one 32-bit LCG **per plugin instance**, shared by all its voices: `s ← s·0x0BB38435 + 0x3619636B`; sample
  = (the float in [2, 4) with mantissa = low 23 bits of s) − 3, i.e. uniform in [−1, 1). Initial state =
  `0x56CE + Σ int16 at byte offsets 0, 254, 508, … < 0x8F5C0` of the WAVES resource (mod 2³²) = **4294572116** for this
  installation **[EXEC: identical in `dsl.exe`, `fed.exe` and the model]**. Each voice advances it once per rendered
  sample, in voice render order — so the exact noise of a note depends on everything that instance rendered before.

### 1.8 Default kit facts **[DATA `Data/Default.dsl`, render-test state]**

The 24 default sounds are in slots 0…23 (keys 48…71): 808 Bongo H/L/M, 808 Clap, Clave, Cowbell, Hat, Kick, Maracas,
Ride, Rim, Snare, Tom H/L/M, 909 Crash, Hat C/O/P, Kick L/S, Kick, Ride, Snare; slots 24…119 hold copies (keys 0…47,
72…119). Of them, only 808 Cowbell (wave 1), 808 Hat (wave 0), 808 Ride (wave 0), 909 Crash (5), 909 Hat C (3),
Hat O (4), Hat P (3) and 909 Ride (6) use a recorded wave; the other 16 are fully synthetic.

### 1.9 Verification **[EXEC + model]**

- **Engine** (`battery_dsl.out`, 24 default patches + 39 cases at 44.1/48/96 kHz: release, attack, retrigger counts
  incl. > 100, velocity 0.3/2.06 with all 7 sensitivities, pitch ±, FCut/FRes, click 50/95 (random), invert, tone LP/HP,
  even/odd harmonics, sweep, wide noise band, ring, envelope shape, waves 0/5 with tuning, two overlapping voices,
  12 random patches): worst relative error **−144 dB** (two voices; −166 dB or better for single voices, i.e. the
  9-digit text precision); the "voice finished" block matched in every case; the noise seed matched.
- **Front-end chain** (`battery_fe.out`, project state restored through SaveRestoreState, 12 cases: keys in three key
  groups, patch pan, velocity, channel pitch, release, morph 20/100/128, level 0/128): worst **−166 dB**; pan and vol
  passed to the host exactly as §1.3.

---

## 2. Pitcher — detector, shifters, correction, Harmonize and MIDI

Complements round 19 §6 (parameter table unchanged). All laws below are **[DECOMP]** and were checked against the DLL
with `pit.exe` (§2.9). Per-channel processing (2 channels), per sample unless said otherwise.

### 2.1 Buffers and sizes (`FUN_18001d0b0`, `FUN_18001d290`) **[DECOMP + EXEC peeks]**

- Range (p32) → minimum frequency `f_min` = 220/170/110/80/55/25 Hz. **Analysis window** `N = 4·⌊fs/f_min/4⌋` samples
  (fs 44.1 kHz, Range 2 → N = 400); plain-shifter delay line `L = 2N`; formant ring buffers `B = trunc(0.1·fs)` (4410)
  **[EXEC peeks: 400, 800, 4410]**. Changing Range rebuilds the buffers at the next block.
- Restart (Formant switch toggled, sample-rate set): delay/ring buffers cleared, read heads at `L − 10`, grain timers 0,
  voice gains 0.

### 2.2 Pitch detector (`FUN_18001e030` front, `FUN_18001e810`) **[DECOMP + EXEC]**

1. Pre-filter: `d ← d + 0.6·(x − d)`; `x − d` (a first-order high-pass) feeds **16 cascaded one-pole low-passes**
   `s ← s + 0.125·(in − s)` computed in double, each stage's input rounded to float; states with |s| < 1e-9 are zeroed.
   Output `y` (float).
2. AMDF: a history of the last N values of y; every sample, for every lag slot k, `A[k] += |H[k] − y|`, then the
   history shifts and y enters. Slot k corresponds to lag `N − k`.
3. Every N samples (non-overlapping frames): `A` is normalised by its maximum (all-zero → unvoiced); scanning from
   lag 2 upward (k = N−2 down to 1), the **first local minimum below 0.3** gives the period `P = N − k` samples
   (integer); `f = fs/P`; unvoiced if none, or if f < f_min or f > 1000 Hz. A is cleared.
- So FL's detector is an integer-lag, frame-based AMDF: pitch quantised to fs/P (at 44.1 kHz: 223.86 Hz for a 223.84 Hz
  sine; ~3 cents at 800 Hz), updated every N samples (9.1 ms at Range 110 Hz), latency one frame.

### 2.3 Correction target and smoothing (`FUN_18001ea80`, `FUN_18001e030`) **[DECOMP + EXEC]**

- Note number `n = 9 + 12·log2(f/A)` with `A = 55·2^((2·p23 − 1)/12)` (round 19), i.e. n = 0 at C 32.70 Hz (A = 55 Hz).
  Octave `o = trunc(n/12)`, class `n − 12o`.
- Nearest allowed note in the 128-entry mask (§2.6), starting at `round(n)`: if not allowed, the nearest allowed below
  and above are searched (bounded by 0 and 127); the lower one is chosen when `n < lower + (upper − lower)/2`.
  Target `t = chosen − n` semitones. Without MIDI mode, the key-bypass button of the class `round(class)` (12 → 0) or
  Bypass All sets t = 0. Unvoiced or empty mask: t = 0 **and the smoothed value is reset to 0**.
- Smoothing every sample: `s ← s + c·(t − s)` while voiced (s = 0 while unvoiced), c from Correction Speed (round 19:
  `c = 8.82/(fs·(3u³ + 0.001))`, u = 1 − p0; u < 0.01 → c = 1).
- Pitch Reference (p24, `int(2v)`): **0 → both channels use the left channel's correction, 1 → each channel its own,
  2 → both use the right channel's** (the opposite channel's read heads/grain timers are copied every sample when 0
  or 2) — corrects round 19's "order L/R/Independent from DOC": the code order is L, independent, R.
- Shift applied to the shifter of channel c: `Fine (2·p3 − 1) + s_ref(c)` semitones.

### 2.4 Plain shifter (Formant off; `FUN_18001ed60`, `FUN_18001ec70`) **[DECOMP + EXEC bit-exact]**

Per channel, 4 voices (voice 0 only, unless Harmonize), each with a read head R on the delay line of length L (the
input is written **after** all voices read, at the write index W, which then advances):
- Ratio `ρ = 2^(clamp(shift, −12, 12)/12)` (ρ = 1 while unvoiced).
- Read: 4-point cubic Hermite (Catmull-Rom) interpolation at R (between the 2nd and 3rd of the 4 points from ⌊R⌋).
- Splice: ρ < 1 and `(R − W) mod L ≤ 128` → jump **forward by one detected period**; ρ > 1 and `(R − W) mod L ≥ L − 128` →
  jump **back by one period**; on a jump the old head keeps reading and is **linearly cross-faded out over 128 samples**
  (weight k/128, k = 128…1). Then `R ← (R mod L) + ρ`.
- Voice gain smoothing `G ← G + 0.007·(g − G)` (g = 1 for voice 0 outside Harmonize); output = Σ G·y.
- With zero shift the plain path is an exact **9-sample delay** of the input once G has settled (EXEC-consistent model,
  `t_delay.out`) — answers round 19's open question: Pitcher with Bypass All is not an identity, it delays by 9
  samples (and fades in from silence after a restart).

### 2.5 Formant shifter (Formant on; `FUN_18001f2b0`, `FUN_18001f180`) — Formant and Gender **[DECOMP + EXEC bit-exact]**

A pitch-synchronous overlap-add (TD-PSOLA) with grain resampling:
- Every voice has a grain timer. When it expires:
  - **unvoiced or shift exactly 0**: a 256-sample periodic Hann grain `0.5(1 − cos(2πi/256))` taken from the input
    257…2 samples back is added to the output ring at the current position; next grain in 128 samples (50 % overlap →
    perfect reconstruction): an exact **257-sample delay** (`t_delay.out`); **Gender has no effect here**.
  - **voiced and shift ≠ 0**: with `len = trunc(2P)` and `h = len/2`, pitch mark = the largest strictly-positive local
    maximum of the pre-filtered signal y (§2.2) among `trunc(1.1·P)` positions going back from `h + 1` samples ago;
    grain = `len` samples of the **unfiltered input**, Hann-windowed `0.5(1 − cos(2πi/len))`, starting at
    mark − h − 70 (i.e. centred 70 samples before the mark); the grain is **resampled by linear interpolation with step γ = Gender factor `2^(2·p2 − 1)`**
    (γ > 1 shortens the grain → formants up; γ = 1 when Gender = 0.5) and added to the output ring; next grain after
    `trunc(P·2^(−shift/12))` samples (spacing → output pitch `f·2^(shift/12)`).
  - Grains are scaled by the voice's smoothed gain G (§2.4).
- Output = the ring value at the current position, which is then cleared; ring length B.
- So **Gender acts only together with a non-zero correction/harmony shift on voiced input**; Formant on its own
  (without Gender) preserves the formants (classic PSOLA), and its pass-through state is a 257-sample delay.

### 2.6 Note masks: scale, key bypass, MIDI (`FUN_18001d890`, `FUN_18001d9c0`, `FUN_180007a40`) **[DECOMP + EXEC]**

- Scale mask (12 classes, `+0x1670`): scale type (p29) 0 natural minor, 1 major, 2 {C, D♯, F, G}, 3 chromatic,
  4 tonic only, 1000 none; rotated up by the tonic p28 (round 15). It is rebuilt only by the editor (`FUN_18001b3a0`)
  and the constructor/state load, not by setParameter; its 12 bytes are also in the saved state (round 19 layout).
- 128-entry note mask: **MIDI off** → every octave of each allowed scale class; **MIDI on, Octaves off** → every octave
  of each pitch class currently held; **MIDI on, Octaves on** → only `note − 12` of each held MIDI note > 12 (absolute;
  n = 0 ↔ MIDI note 12). Held notes come from the plugin's MIDI input (velocity stored as vel/127), round 16 port routing.
  Turning Octaves or Harmonize on turns MIDI on; turning MIDI off turns both off (editor handler).
- **MIDI-controlled correction** (MIDI on, Harmonize off): the same correction as §2.3, but the allowed notes are the
  held ones (snap to the nearest held class / absolute note); key-bypass buttons and Bypass All are ignored.

### 2.7 Harmonize (`FUN_18001f800`) **[DECOMP + EXEC]**

Evaluated **once per host block** (at the start of the process call):
- Notes: the first 4 held notes in ascending MIDI order; Octaves off → their pitch classes (a class equal to one of the
  first three already taken is skipped), weight `2·vel²` (vel = velocity/127); Octaves on → `note − 12`, weight 0.
- For channel c and voice j (only if channel c is voiced): `shift = note_j − n_c` with n_c = the detected class (Octaves
  off, so shifts lie within ±12 and are not wrapped to the nearest octave) or `12·o + class` (Octaves on);
  gain `g = clamp(2·(0.5 ∓ (pan_j − 0.5)·Stereo), 0, 1)` (− for the left channel, + for the right; pan_j = p17+j
  "Voice 1…4 Pan", names in the constructor) × weight if Vel (p31) is on (with Octaves on that weight is 0 → silent).
- Replace/Mix (p30): 0 → voice 0 defaults to gain 1, shift 0 (dry through the shifter, replaced by the first note when
  a note is held); ≠ 0 → no default voice, and the **dry input is added** to the output.
- The voices go through the plain or the formant shifter (§2.4/2.5). Unvoiced channel → no voice active (only the
  default/dry).

### 2.8 Parameter details confirmed this round **[DECOMP]**

Gender (p2) = `2^(2v − 1)` (`FUN_18001cab0` case 2, powf(2, ·) at `0x18001cc88`); A reference (p23) = `55·2^(v/6 − 1/12)`
(case 0x17); Range table at `0x18005d670…0x18005d6a0`; MIDI port `~int(256v)` = `int(256v) − 1` for v ≥ 0 (case 0x21);
Fine = `2v − 1` semitones; Correction Speed as round 19.

### 2.9 Verification **[EXEC + model]**

`run_pit.out` (14 cases, 0.5 s each, host blocks of 512) and `battery_formant.out` (7 cases):
- **Bit-exact** (maximum error 0): sine/saw/vibrato correction at three speeds, Pitch Reference R, Fine, G-major scale,
  Harmonize with Octaves, Bypass All, Range 55 Hz, and **all 7 formant cases** (Gender 0.3/0.6/0.8, shift 0, unvoiced
  noise, stereo independent, formant + Harmonize).
- MIDI mask cases −150 dB, Harmonize by class −90/−98 dB, stereo "independent" −43 dB: traced to the CRT's `log2f`
  being 1 ulp away from the correctly rounded value used by the model (EXEC: `log2f(0.30430838)` = `0xbfdbb2cc` in the
  DLL vs `0xbfdbb2cd` correctly rounded), which shifts the target by ~2e-6 semitone and, much later, the time of one
  splice. Detector periods/frequencies matched in every frame checked (peeks of `+0x16F4/+0x171C/+0x1714/+0x1724`).
- **The FL detector and both shifters can therefore be described exactly** (§2.2–2.5); LYNX's current Pitcher
  (`EffectKind::pitchCorrect`) uses FL's parameter laws with its own detector/shifter.

---

## Summary table (question → result)

| Item | Result | Evidence |
|---|---|---|
| DrumSynth state format | u32 version, u32 **editor key**, 120 names, 120×38 int32, u32 flags | DECOMP + DATA |
| Which patch a note plays | key → slot map (48…71 → 0…23, <48 → +24, 72…119 → same); one patch per key | DECOMP + DATA + EXEC |
| Engine synthesis | 2 filtered noise/sine oscillators, wave+noise source, envelopes, retrigger, shaper, ring, clip, tone filter | DECOMP + EXEC (≤ −144 dB) |
| Parameter laws | §1.4/1.5 (levels, decays, Hz, attack, retrigger, tone cut-offs) | DECOMP + EXEC |
| Output level | engine ×1.03858·2·Final.Vol; host vol Final.Vol·(11^(L/128) − 1)/10; pan (Final.Pan + PAN)/64 | DECOMP + EXEC (front-end) |
| Morph | per-parameter rounding interpolation, f(p37) piecewise | DECOMP + EXEC |
| Recorded waves | resource WAVES/4003, int16, 18 looped regions | DECOMP + DATA + EXEC |
| Noise | per-instance LCG, seed from the wave resource (4294572116) | DECOMP + EXEC |
| FL integer levels for this plugin | flag bit 21 cleared; levels read as int | DECOMP; FL side **inference** |
| Send track −1 | not confirmed | — |
| Host gain ramp law | not confirmed | — |
| Pitcher detector | HP + 16× LP + integer-lag AMDF, frame N, 0.3 threshold, f_min…1000 Hz | DECOMP + EXEC (bit-exact) |
| Pitcher plain shifter | 4 heads, Hermite, period splices with 128-sample crossfade, 9-sample latency | DECOMP + EXEC (bit-exact) |
| Formant / Gender | TD-PSOLA, grains 2P, resampled by 2^(2·Gender−1); 257-sample latency; Gender only with shift | DECOMP + EXEC (bit-exact) |
| Harmonize | ≤ 4 held notes, per block, class or absolute, pans, Vel weight 2·vel², Replace/Mix | DECOMP + EXEC |
| MIDI mode | mask from held notes (class / absolute) | DECOMP + EXEC |
| Pitch Reference order | 0 = L, 1 = independent, 2 = R (corrects round 19) | DECOMP + EXEC |

---

## 3. For LYNX

### 3.1 DrumSynth Live (replace `drumSynthLiveSound` sample picking in `FlpImport.cpp`/`FlGenerators.cpp`)

1. **Import:** parse the blob (§1.2; versions 3/4; apply the v < 4 rule for p36). Keep all 120 patches and names; the
   second word is only the editor key (ignore it for sound selection).
2. **Per note:** `key` = the note's key (FL note number); `slot = keyToSlot(key)` (§1.2), none → silent. Engine
   parameters = §1.3 "patch → engine" (incl. morph when p34 ≠ key, f from p37). Velocity term
   `v = 2·InitVol − 1 + ModY` with `InitVol = (11^(vel/100) − 1)/10` (round 28 inference: vel 100 → v = 1) and ModY the
   note's Mod Y in −1…1 (default 0, **not confirmed** that FL's neutral is 0); `FCut` = Mod X likewise.
   Pitch ratio `r = 2^(c/1200)`, c = channel pitch/fine in cents relative to root C5 (the key does not transpose).
3. **Engine:** port `r30/py/dsl_model.py` (`Engine.trigger`, `Voice.render`, `Voice.release`) — every constant is there
   and was verified sample-exact; keep float32 arithmetic and the double-precision exp/log/floor where marked.
   One noise LCG per DrumSynth channel, seeded once at instance creation with the value derived from the user's
   `drumsynth_eng_x64.dll` WAVES resource (§1.7; fall back to any constant if the DLL is absent), advanced per voice
   per rendered sample.
4. **Waves:** at run time, read resource `WAVES`/4003 from the user's installation
   (`%FLStudio%\Plugins\Fruity\Generators\Fruity DrumSynth Live\drumsynth_eng_x64.dll`; `M.load_waves` shows how) and
   use `WAVE_TABLE`; if missing, use the noise only (wave gain 0) and log it. Never bundle the data.
5. **Mixing:** voice mono output × engine gain (already contains 2·Final.Vol) → pan/vol with
   `vol = Final.Vol·g(p0)`, `pan = clamp((FinalPanInt + p1)/64, −1, 1)`, law A (round 28) — with FinalPanInt the
   channel/note pan on the −64…64 scale (that scale for FL's side is **inference**). Send: if p36 > 0, also to that
   track with `g(p35)`. Voice modes per §1.3 (cut = fade within one block; LYNX may use a short linear ramp).
6. Note-off → `Voice.release` (most default patches ignore it: release ≥ 121).

### 3.2 Pitcher (`EffectKind::pitchCorrect`)

1. Replace LYNX's own detector with §2.2 (16 double one-pole stages at 0.125 after the 0.6 tracker; AMDF over N =
   4·⌊fs/f_min/4⌋; non-overlapping frames; first local minimum < 0.3 scanning from short lags; integer period;
   f ∈ [f_min, 1000 Hz]).
2. Target/smoothing exactly as §2.3 (mask search, midpoint rule, key bypass, reset when unvoiced); Pitch Reference
   0 = L, 1 = independent, 2 = R.
3. Shifter: Formant off → §2.4 (delay 2N, 4 heads, Hermite, period splices ±P with 128-sample linear crossfade, gain
   smoothing 0.007, ±12 semitone clamp); Formant on → §2.5 (PSOLA, Gender = grain resampling step 2^(2·p2−1),
   unvoiced/zero-shift → 256-Hann OLA). Report latency 9 / 257 samples to PDC if LYNX wants FL's alignment
   (FL's reported latency was not checked).
4. MIDI and Harmonize: masks per §2.6 from the notes arriving on the Pitcher's MIDI port (round 16 routing);
   harmony per §2.7, recomputed once per audio block; Replace/Mix adds dry.
5. Reference model: `r30/py/pitcher_model.py` (bit-exact against the DLL in 16 of 21 cases, the rest limited by the
   CRT's log2f rounding).

---

## Evidence index

| Source | What it established | Marker |
|---|---|---|
| Engine `FUN_180001060` | WAVES resource 4003, wave table, seed sum, defaults 44100 | DECOMP + EXEC |
| Engine `FUN_180001880` (`r30/out/trig.s`), jump table `0x180002344` | note-on set-up, parameter scaling | DECOMP + EXEC |
| Engine `FUN_180002560` (`r30/out/rend.s`), `FUN_180002430` | render, release | DECOMP + EXEC |
| Engine `FUN_180002a80/2ae0/2b50`, constants `0x18005f528…0x18005f630` | D, G, C laws | DECOMP + EXEC |
| Front-end `FUN_00717770`, `FUN_00715590`, `FUN_00714ec0` | state and `.dsl` formats | DECOMP + DATA + EXEC |
| Front-end `FUN_007192b0`, `FUN_00719d70`, `FUN_00719540`, `FUN_00719660` | per-note path, morph, mixing, voice modes | DECOMP + EXEC |
| Front-end `FUN_00717ed0`, `FUN_005eb840` | level law (11^(v/128) − 1)/10 | DECOMP + EXEC |
| Front-end `FUN_00719a60/9b00/9b40/9b70/9d50` | parameter index and key ↔ slot maps | DECOMP + EXEC |
| Front-end form `TSynthEditorForm` (`r30/out/synthform.txt`) | parameter names, ranges | DATA |
| Front-end PlugInfo `0x770148`, constructor flag mask | SDK 1, 4560 params, bit 21 cleared | DECOMP + DATA |
| FLEngine `0x132acd0`, `0x13e7564` | host slot 3 integer pan /64 | DECOMP |
| `r14/blobs14.pkl` + demo `.flp` notes (`t_data.out`) | key = selected key, versions, p36 = −1 | DATA |
| Pitcher `FUN_18001e030`, `FUN_18001e810`, `FUN_18001ea80` | detector, target, smoothing, reference | DECOMP + EXEC |
| Pitcher `FUN_18001ed60`, `FUN_18001ec70` | plain shifter | DECOMP + EXEC |
| Pitcher `FUN_18001f2b0`, `FUN_18001f180` | formant / Gender | DECOMP + EXEC |
| Pitcher `FUN_18001f800`, `FUN_18001d9c0`, `FUN_18001d890`, `FUN_180007a40`, `FUN_18001b3a0` | harmony, masks, MIDI | DECOMP + EXEC |
| Pitcher `FUN_18001d0b0`, `FUN_18001d290`, `FUN_18001cab0` | sizes, restart, parameter laws | DECOMP + EXEC |
| Batteries `battery_dsl.out`, `battery_fe.out`, `run_pit.out`, `battery_formant.out`, `t_delay.out` | verification | EXEC + model |

### Open questions

- FL side of DrumSynth's integer levels (where FL converts its float voice levels for an SDK-1 plugin; FL's pan scale
  for it), FL's neutral Mod X/Mod Y values, and the velocity → Init.Vol law (round 28 inference).
- Send to track −1 (v4 data) and the host's per-block gain-ramp law for cut voices.
- Pitcher's reported latency to FL (PDC) and the wrapper's MIDI-port note delivery timing (block vs sample).
- The `.dsl` second header word (read and ignored).
