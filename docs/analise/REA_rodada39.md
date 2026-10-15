# Behavioural analysis of FL Studio 2026: round 39 (REA) — GMS and Morphine: complete engines as portable models

> **Nature of this document.** Continuation of `REA_rodada12.md` §1/§5 (GMS and Morphine formats), `REA_rodada16.md` §3
> (Morphine chunk tree and parameters) and `REA_rodada18.md` §6 (GMS modulation scales), with the same goal as rounds 32
> (Harmless), 34 (Harmor) and 37 (Ogun): describe both engines precisely enough for LYNX to port them as exact
> instruments instead of the current approximations (`synthFromGms` and `synthFromMorphine` in
> `src/core/FlGenerators.cpp`). *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was
> copied: what is here are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`),
> offsets and globals serve only to trace the evidence. No encrypted content was touched (no FLEX pack was opened) and
> no Image-Line content (code, audio, wave or noise samples) was copied into the repository.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line documentation (**[DOC]**, with URL), from the data (**[DATA]**) or from running the
> installation's own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not
> confirmed"**. Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical model
> (which implements only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line documentation (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (copies in `r39/bin/`, re-hashed this round, identical to rounds 12/16/18):
  - `Plugins/Fruity/Generators/GMS/GMS_x64.dll`, SHA-256 `20c88eba48ab3a80b1524b28f9303065a0cb9a198f70d88ac287deccf5fcec9c`
    (C++/MSVC, RTTI: `CFLWrapper`, `CPluginRenderer`, `CSynthesizer`, `GMSynth`, `GMSynthVoice`, `MFXTapeDelay`,
    `fxReverb`), image base 0x180000000; data `GMS/Synth Waves/*.wav` (29 files, read at run time).
  - `Plugins/Fruity/Generators/Morphine/Morphine_x64.dll`, SHA-256
    `decf72d6a27ec6a6576f4708a20eba33cd949fa73866a9ad0cc7cbfcf4d3d48e` (C++/MSVC, RTTI: `CFLWrapper`, `CMorSynth`,
    `CMorGenerator`, `CZoneGen`, `CResynth`, `CMorphLineProcessor`, `CModProcessor`, `CChorus`, `CDelay`, `CTapeDelay`,
    `CReverb`); data `Morphine/Noise Samples/*.wav` (read at run time).
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=2G`, one instance at a time through `flock`), projects
  `/tmp/r39gh_GMS`, `/tmp/r39gh_Morphine`; full decompilations `r39/out/gms_c0_110.c` (0x1800c0000…0x180110000),
  `r39/out/gms_000_0c0.c` (0x180001000…0x1800c0000), `r39/out/mor_c0_140.c` (0x1800c0000…0x180140000); RTTI vtables
  `r39/out/gms_vft.txt`, `r39/out/mor_vft.txt`; `objdump -M intel` wherever Ghidra dropped arguments of CRT calls
  (`powf`, `logf`, `sin`, vector `cos`) or the float operation order mattered; constants with `r39/py/k.py`,
  `r39/py/kfun.py`. The GMS effects and the whole of Morphine were decoded by two parallel sub-tasks from the same dumps,
  under the same rules.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r39/run/gm.c` → `gm.exe`, a copy of round 37's `og.c` (same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified
  from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's own
  SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Additions: `on3=` gives
  `Init.Vol` directly (both wrappers derive the MIDI velocity from it), `pef=0,<tempo>` sends ProcessEvent 0 with float
  bits, `pe=1,0` ProcessEvent 1 (max polyphony), `panlaw` = host ComputeLRVol law A (round 28 §4.1); variant
  `run/gm_sp.c` also answers host Dispatcher 0x24 (mixing time) for the song-position tests; `run/gfx.c` creates GMS and
  calls the DLL's own effect routine `FUN_1800ff500` directly on test signals (like round 37's `og_os.c`). Peeks
  `arr=-1,…,228>…` read engine fields through the plugin's `+0x228` pointer; `arr=-2,…` reads module globals by RVA.
  No process or dialog was left behind; outputs were deterministic run to run.
- **Models** (pure Python + numpy, `python3 -I`; under `r39/py/`): GMS `gmsmodel.py` (wrapper, engine, voice, filter,
  master, effect-rack driver), `gmspatch.py` (state), `gmsfx.py` + `gmseq.py` + `gmsrev.py` (effect objects, EQ, reverb);
  Morphine `mormodel.py`, `morpatch.py`, `morfx.py`. Drivers and verification: GMS `gmrun.py`, `gcmp.py`, `gbat.py`
  (demo battery), `gsyn.py` (synthetic battery), `t_waves.py`, `t_sens.py`, `gfxrun.py`, `fxcmp.py`, `t_gmsfx.py`;
  Morphine `mor_cmp.py`, `mor_bat.py`, `mor_t_fx.py`, `mor_t_noise.py`, `mor_t_syn.py`, `mor_survey.py`, `mor_peek.py`.
  Logs in `r39/out/` (`gbat_*.log`, `gsyn.log`, `t_gmsfx.log`, `t_waves.log`, `t_sens.log`, `mor_bat_*.log`, `mor_t_*.log`).
- **Data:** `r14/blobs14.pkl` (0xD5 states of the installation's projects): GMS — 9 demo channels + 1 render-test channel
  (`System/Render tests/GMS.flp`); Morphine — 7 demo channels. In the 30-song LYNX regression set GMS appears in 5 songs and
  Morphine in 4 (task statement).

---

## 0. Data survey

### 0.1 GMS: the 9 demo channels + render test **[DATA, `r39/py/gsurvey.py`]**

Mod kind = trunc(2·P51) (0 sync, 1 FM, 2 AM); U = unison voices `1 − trunc(−15·P41)`; filter type `trunc(2·P15)`;
effects = slots with P[147+k] ≠ 0, EQ = P206.

| # | Channel | Waves (osc 1/2/3) | Voice features | Effects |
|---|---|---|---|---|
| 0 | Future House: FUTURE Sub | Square Retro / Saw Mean / Saw Mean | mono, U1, sync m 0, noise 0.20, osc2 phase retrig, env1→cutoff, vel→cutoff | Distortion |
| 1 | Future House: FUTURE Bass | Keys / Triangle / Saw Retro | mono, U4 stereo 1, **FM** m 0.44, noise 0.61, env1→cutoff, vel→cutoff | Distortion |
| 2 | Time: GMS | Dirt / Saw Retro / Saw Mean | poly, U2, mix2 0.55 mix3 0.72, noise 0.18, env1→cutoff | Reverb |
| 3 | Time: Plucks 2 | Saw / Square / Saw | poly, **U13**, **FM** m 0.37, env1+env2→cutoff, **LFO1 triangle → cutoff**, amp velocity | Crusher, HiPass, Delay, Reverb, EQ |
| 4 | Africa's Calling: GMS | Funky / Digital / Saw | mono, U16 detune 0.6 stereo 1, filter **type 2**, res 0.66, keytrack 1, env1+env2→cutoff, osc2 phase retrig, P199 garbage 10267 | — |
| 5 | Africa's Calling: Synth 02 | Saw Mean / Sine / Digital | poly, U8, **FM** m 0.47, **LFO1/LFO2 tempo-synced → cutoff**, osc2 invert, amp velocity | Distortion, Crusher, Reverb |
| 6 | Song For You: GMS | Saw / Saw / Saw | poly, U8 stereo 0.41, **slide** 0.55 | Reverb, EQ |
| 7 | Never Out: Disco Fuzz Te | Funky / Sine / Saw Retro | poly, env1→cutoff, env2 destination 3 ("LFO": multiplies LFO2, whose amount is 0), **LFO1 sine → pitch** (vibrato), amp velocity | Distortion, Crusher |
| 8 | Never Out: Bass Impacter | Sine / Triangle / Saw | mono, U6, **sync** m 0.46, filter keytrack 1, env1→**pitch**, amp velocity | Distortion, Phaser, HiPass, Delay, Reverb, EQ |
| 9 | Render test GMS.flp | Beast / Sine / Saw | poly, U2, **unison phase retrig** 0.51, noise 0.16, vel→cutoff | — |

All three modulation kinds, all three filter types (0 in 8 channels, 2 in one), mono (4) and poly (6), unison 1…16,
slide, noise, both LFOs (free and synced) and envelope destinations cutoff, pitch and "LFO" occur; the velocity row of
the matrix is used only toward the cutoff (3 channels; the mod-wheel/aftertouch rows hold 0 or garbage) **[DATA]**.

### 0.2 Morphine: the 7 demo channels

See §2.0.

---

## 1. GMS — the voice

### 1.1 Structure and data flow **[DECOMP; RTTI class names from the PE]**

- GMS is a C++ plugin built on Image-Line's generic wrapper (`CFLWrapper` → `CPluginRenderer`/`CSynthesizer` →
  `GMSynth`, voices `GMSynthVoice`, effect objects `FXControl`-like records with `MFXTapeDelay`/`fxReverb`), the same wrapper
  family as Sakura/Drumpad (round 28 §4.2). Wrapper VMT `0x18014a0b0`: slot 1 Dispatcher `FUN_1800c7d90`, 3
  SaveRestoreState `FUN_1800c82f0`, 5 ProcessEvent `FUN_1800c8760`, 6 ProcessParam `FUN_1800c8800`, **8 Gen_Render
  `FUN_1800c9b60`**, **9 TriggerVoice `FUN_1800c9cd0`**, **10 Voice_Release `FUN_1800c9df0`**, 11 Voice_Kill
  `FUN_1800c9e10`. Engine VMT `GMSynth` `0x18014c940`: slot 1 note-on wrapper `FUN_1800ccd70`, 2 note-off `FUN_1800ccdb0`,
  10 set sample rate `FUN_1800f12d0`, 16 set "playing" `FUN_1800f32e0`, **17 process `FUN_1800f32f0`**, 23/27 set/get
  parameter, 35/36 preset write/read `FUN_1800f43d0`/`FUN_1800f4e60`, **38 voice start `FUN_1800f5250`**, **39 voice
  release `FUN_1800f58e0`**, 53 polyphony `FUN_1800ed930`.
- **Gen_Render** (`FUN_1800c9b60`): per-note host levels (`FUN_1800c91b0`, §1.11), then `FUN_1800c94e0`: optional host
  time (§1.10), then the block is cut into **chunks of at most 512 samples** (engine buffers of 0x200 floats; EXEC: host
  blocks of 1000/2048 match only with this split), each chunk = one call of the engine's process; finally voices whose
  note is no longer held and whose engine voice ended are killed in the host (`FUN_1800c9c20`).
- **Engine process** (`FUN_1800f32f0`), per chunk of n samples:
  1. `FUN_1800f5f00`: LFO rates/phases (§1.8), effect-slot parameters (§1.13), output-EQ parameters; marks the notes that
     have an active voice; if at least one voice is active: control values (`FUN_1800f68f0`, §1.4) then the voice render
     (`FUN_1800f7530`, §1.5–1.7).
  2. The 4-pole filter for every active voice, two lanes per voice (left/right unison sums), processed four lanes at a
     time with SSE but each lane independent (§1.9); each lane's output, times the voice's smoothed level, is **added**
     to the output buffer, voices in list order.
  3. The ten effect slots in fixed order 0…9 (§1.13), each with the output EQ when it is the EQ carrier.
  4. Master level and pan (§1.12).
- **Voice record** (0x2510 bytes, 99 records, list at engine `+0x44a0`): flags `+0x204c` released / `+0x204d` active,
  note `+0x2050`, transposed note `+0x2054`, glide pitch `+0x2058` (float, MIDI note), velocity `+0x205c` (0…1),
  per-unison-slot phases/increments `+0x2074…+0x2270` (16 slots × osc1, sync master, osc2, osc3), per-slot gains
  `+0x2274…+0x2470`, table pointers `+0x2478/+0x2480/+0x2488`, amp envelope `+0x2490…+0x24a8`, envelope 1/2
  `+0x24ac…+0x24d0`, resonance `+0x24d4`, cutoff offset `+0x24d8`, filter states `+0x24dc…+0x24f8`, level smoother
  `+0x24fc`/target `+0x2500`, LFO values `+0x2504/+0x2508`; per-chunk buffers `+0x48` (left), `+0x848` (right),
  `+0x1048` (cutoff), `+0x1848` (level) **[DECOMP + EXEC peeks]**.

### 1.2 State and parameters **[DECOMP `FUN_1800f4e60`; DATA]**

Format as round 12 §1.1 (`"SSLF"` + size + `Syn4`/`Syn3` preset: 7 × 512 path slots, `Syn4` 3 UI ints, **212 f32
parameters**, 512-byte name). Re-checked here: the engine keeps every value **as stored** (parameter objects of 0x94
bytes, value at `+4`, read by `FUN_1800cd7a0`); there is no clamping on load. The garbage found in old presets at
indices ≥ 197 (e.g. 6.05e13 at 202, 1.8e28 at 204, 10267.3 at 199 in "Bass 4 MC") is inert: destinations are converted
with `cvttss2si`, which gives INT_MIN for out-of-range values, and INT_MIN (or 10267) matches no destination code
(§1.3) **[DECOMP + EXEC: demo channels with such values match the model]**. Index meanings are round 12 §1.2; the laws
below supersede round 12 §1.3 wherever they differ.

**Reading a parameter** (`FUN_1800f50c0`): `v = P[i]`; for each of the three matrix rows (velocity: destination P199,
amount P198, source = the engine's current velocity `+0x4804`; mod wheel: P202/P201, source CC1 `+0x3a14`; aftertouch:
P204/P203, source `+0x3a08`): if the destination code (truncated) maps to `i`, `v ← clamp(v + (2·amount − 1)·source,
0, 1)` (computed as `((a − 0.5) + a − 0.5)·s + v` in float). Destination code → parameter (`FUN_1800ef810`): 1 → 12
cutoff, 2 → 13 resonance, 4 → 50 modulation, 6 → 20 env1 attack, 7 → 22 env1 decay, **8 and 12 → 16 env1 amount**
(code 12 does not reach env2's amount), 10 → 21 env2 attack, 11 → 23 env2 decay, 14 → 34 LFO1 rate, 15 → 28 LFO1
amount, 17 → 35 LFO2 rate, 18 → 29 LFO2 amount, 20 → 4 amp attack, 21 → 5 amp decay **[DECOMP + EXEC]**. The master
level/pan (P0/P1), the raw cutoff used for smoothing (P12) and the output-EQ values are read without the matrix.

### 1.3 Notes, velocity, voice allocation **[DECOMP `FUN_1800c9cd0`, `FUN_1800ccd70`, `FUN_1800f5250`, `FUN_1800f53e0`, `FUN_1800f58e0`, `FUN_1800ed930`; EXEC]**

- **TriggerVoice**: sends voice event 6 ("link velocity") with value 0; `note = trunc(Init.Pitch·0.01)` clamped 0…127;
  MIDI velocity `vel7 = trunc(127·ln(1 + 10·Init.Vol)·0.3284587)` clamped 0…127 (0.3284587 = 1/ln 21; same function as
  Sakura/Drumpad, round 28 §4.2); if the note's wrapper slot holds another host voice, that note is first released in the
  engine and the old host voice killed. The voice handle returned to the host is the note number; Voice_Release and
  Voice_Kill therefore act on **every engine voice of that note** **[DECOMP + EXEC]**.
- The engine stores `vel7/127` (×0.007874016 in float) in a per-note table (`+0x1068`), and velocity 0 means note-off.
  Note-on makes it the engine's current velocity `+0x4804` (the matrix source) and copies it into the voice.
- **Voice choice** (`FUN_1800f5250`): mono (P40 ≠ 0) always uses voice 0 (retrigger, §1.5). Poly: among the first
  `min(maxpoly, npoly)` voices, the **last inactive** one; if none, the one with the smallest start counter (the oldest).
  `maxpoly` = 99 (wrapper info); `npoly` (`+0x38ec`) is **0 until the host sends ProcessEvent 1 (max polyphony)** — then
  value 0 → 99, value k → k (`FUN_1800ed930`). Without that event GMS plays nothing in poly mode **[EXEC]**; FL's value
  for a channel with default polyphony is not observed (the model assumes 0 → 99, **inference**).
- **Transposed note** `n' = note − trunc(24 − 48·P2) − 12·(trunc(−4·P44) + 2)` (float32 products; P2 = 0.5 and P44 = 0.5
  give 0) **[DECOMP + EXEC]**. Round 12's octave law `trunc(4·x) − 2` is equivalent only where truncation of a negative
  value does not matter: e.g. P44 = 0.3 → −1 octave here.
- **Start** (`FUN_1800f53e0`): amp envelope target 5.0 with the attack rate, the **current amp-envelope value is kept**
  (0 for a fresh voice, see §1.14); envelopes 1/2 restart from 0 with target 5.0; LFO values cleared; level smoother
  initialised to `P8²` if it holds the "unset" marker −1 (then it glides to the real level, §1.5); glide start =
  previous note (`+0x3a28`) when Slide (P11) is on and a previous note exists; unison phases: with P47 ≠ 0 every slot
  k = 0…15 gets the same value for its four phases, `phase_ofs(k·(4·P46))` (§1.6) — the product is a float, not an
  integer; else, with P53 ≠ 0, a flag asks for random start offsets (§1.6); LFO k phase reset to 0 when P26/P27 ≠ 0.
- **Release** (`FUN_1800f58e0`): every voice with this note: amp target 0, current and release rate
  `c_R = 8.82/((25·x³ + 0.001)·fs)`, `x = (float)(0.55·P7 + 0.1)` (double then float); the released flag is set.

### 1.4 Per-chunk control values (`FUN_1800f68f0`) **[DECOMP + objdump `0x1800f6980…0x1800f7300`; EXEC]**

Once per chunk, for each active voice in list order (the engine velocity `+0x4804` is first set to the voice's velocity,
so the velocity matrix is per voice):
- Amounts: env1 `P16²`, env2 `P17²`, LFO1 `P28²`, LFO2 `P29²`, each negated when its invert switch (P18, P19, P30, P31)
  is on. Destinations `trunc(3·P24)`, `trunc(3·P25)` (envelopes: 0 cutoff, 1 modulation, 2 pitch, 3 "LFO" = multiplies
  the LFO of the same number), `trunc(3·P38)`, `trunc(3·P39)` (LFOs: 0 cutoff, 1 modulation, 2 pitch, 3 volume); shapes
  `trunc(3·P32)`, `trunc(3·P33)`; filter type `(char)trunc(2·P15)`; osc2 phase offset `phase_ofs(2·P52)`; modulation kind
  `trunc(2·P51)` (0 sync, 1 FM, 2 AM).
- **End test**: if the amp target is 0 (released) and the amp envelope < 1e-4, the voice is reset (§1.14) and skipped.
- Key tracking `kt = P14·(g − 57)·(1/128)` with `g` = the glide pitch **before** this chunk's update (on the first chunk
  of a note without slide: the previous note of that voice) — 0 when P14 = 0; cutoff offset `+0x24d8 = (P12_matrix −
  P12_raw) + kt`; resonance `= P13` (matrix).
- Level target `200·db(50·P8 − 25)` (× voice velocity when P200 ≠ 0), `db(x) = 0 if x < −80 else powf(10, 0.05·x)`
  (`FUN_1800ef900`); the factor 200 is undone by the 0.005 in the slot gains.
- Transposed note recomputed (§1.3); without slide the glide pitch is set to it.
- **Pitch** (semitones, float32, in this order): `p = trunc(12·P205)·bend + glide`; `p += (24·((d₂=2)·a₂·e₂ +
  (d₁=2)·a₁·e₁)) + (12·((l₂=2)·L₂ + (l₁=2)·L₁))`; `p −= 57`; `p += T[note]` (host pitch offset, §1.11); e and L are the
  envelope/LFO values left by the previous chunk. Base frequency `f₀ = powf(2, p/12)·220` (p/12 as `p·0.0833333358`).
- Oscillator offsets `o_k = (float)(trunc(100·P_cents − 50)·0.01 + trunc(72·P_semi − 36))` (osc1 P55/P56, osc2 P57/P58,
  osc3 P59/P60); `f_k = powf(2, o_k/12)·f₀`.
- **Sync** (kind 0): `m = clamp(P50 + (d₁=1)·a₁·e₁ + ((l₂=1)·L₂ + (l₁=1)·L₁ + (d₂=1)·a₂·e₂), 0, 1)` — P50 *not* squared
  here — and osc1 runs at `(3m + 1)·f₁`; FM/AM: factor 1.
- **Tables**: for each oscillator one of 512 band-limited tables (§1.6): index `clamp(trunc((0.5/f)·fs), 0, 511)` when
  `f > fs/1024`, else 511 (`FUN_1800ef980`); osc1 uses the sync frequency.
- **Unison** `U = clamp(1 − trunc(−15·P41), 1, 16)`; spread `s = ((float)(0.4·P42))²`; slot u = 0…15 (all 16 are
  computed): detune `d_u = powf(2, (u/(U−1) − 0.5)·s)` (1 when U = 1); increments (`FUN_1800ef930`)
  `inc = (u32)(int64)((double)(f/fs)·4294967295)` with f clamped to [0, 0.4999·fs]: osc1 `(3m+1)f₁·d_u`, sync master
  `f₁·d_u`, osc2 `f₂·d_u`, osc3 `f₃·d_u`.
- **Gains** per slot: pan sign σ = +1 (even u), −1 (odd u), 0 for u = 0 when U is odd; `gL = clamp(1 − σ·P43)·T_L[note]`,
  `gR = clamp(1 + σ·P43)·T_R[note]` (host levels §1.11), normaliser `0.005/((U − 1)·0.15 + 1)`; mix weights with
  `a = P48²`, `b = P49²`, `n = P45²`: osc1 `(1−a)(1−b)(1−n)`, osc2 `(1−b)·a·(1−n)`, osc3 `(1−n)·b`, noise `n`
  (round 12 §1.3 confirmed).
- **Random start offsets** (flag from §1.3): for every slot u = 0…15, `r = (rng() + 1) & 511`; osc1 phase = sync master
  phase = `r·inc₁`, osc2 phase = `r·inc₂` (32-bit wrap); osc3 keeps its phase. 16 draws per voice start, whatever U is.

### 1.5 Envelopes, glide, level, modulation buffers (`FUN_1800f7530`) **[DECOMP; EXEC: peeked values equal the model]**

Per sample, float32, each `x ← x + (t − x)·c`:
- **Amp envelope**: attack target 5.0 with `c_A = 8.82/((50·P4³ + 0.001)·fs)`; when `trunc(x) ≠ 0` (x ≥ 1): x = 1,
  target = sustain `P6`, rate `c_D = 8.82/((50·P5⁴ + 0.001)·fs)`; release §1.3. Output `amp = x²` + LFO values with
  destination 3, clamped ≥ 0. Real times: attack to 1 ≈ 1.265·P4³ s; decay/release time constants T/8.82 (round 12).
- **Envelopes 1/2**: attack target 5.0, `c = 8.82/((300·A⁵ + 0.001)·fs)`; at x ≥ 1: x = 1, target 0,
  `c = 8.82/((70·D⁵ + 0.001)·fs)` with `D = 0.8·P_decay + 0.2` (computed as `D⁴·(70·D)`).
- **Glide**: `g ← g + (n' − g)·k`, `k = 8.82/((2·P10³ + 0.001)·fs)`.
- **Level smoother**: `λ ← λ + (target − λ)·0.01` → per-sample level buffer used after the filter.
- **Shared buffers** (computed once per chunk by the first active voice; the LFOs advance only while a voice is active):
  smoothed raw cutoff `c ← c + (P12 − c)·0.01`, smoothed modulation `μ ← μ + (P50² − μ)·0.01` (both start at their
  target the first time), LFO1/LFO2 waves (§1.8).
- **Modulation per sample** (FM/AM only): `M = max(0, μ + (d₁=1)e₁a₁ + (d₂=1)e₂a₂ + (l₁=1)L₁ + (l₂=1)L₂)` × 15 (FM) or
  × 3 (AM); `L_k = LFO_k·amount_k` (× env_k when env k has destination 3).
- **Cutoff per sample** = smoothed cutoff + `+0x24d8` + `(d=0)` envelope and LFO terms (unclamped; the filter clamps).

### 1.6 Oscillators and wave tables **[DECOMP `FUN_1800f1570`, `FUN_1800dfa30`, `FUN_1800ef9d0`, `FUN_1800ef960`; EXEC: tables peeked equal the model to 5·10⁻⁷]**

- **Tables**: each oscillator's `.wav` (`GMS\Synth Waves\<base name>.wav`, "Devils Horn" → "Double Spike"; channel 0
  of stereo files; 16-bit /32768 or float) is resampled to 1024 points with a 4-point cubic (`FUN_1800dfa30`) at
  positions `i·len/1024`, reading `p[j..j+3]` around `j = trunc(pos)` and interpolating **between `p[j+1]` and `p[j+2]`**
  (so a 1024-sample file is shifted by one sample; reads past the end give 0); then a 1024-point spectrum, magnitude/1024
  and phase of harmonics 1…512; table k (0…511) = Σ_{h=1}^{k+1} A_h·cos(2π·h·i/1024 + φ_h), accumulated in float32 one
  harmonic at a time (double products) **[EXEC: tables 0, 1, 10, 511 of three waves equal to 4.8·10⁻⁷]**.
- **Read** (`FUN_1800ef9d0`): `table[((rng() & 0x3fffff) + phase) >> 22]` — 32-bit phase, 10-bit index, **random
  dither of the 22 fractional bits** (no interpolation).
- **Per sample, per unison slot u (outer loop), sample (inner loop)**, four RNG draws in this order:
  1. osc2 = read(T₂, ph₂ + ofs₂), negated if P54;
  2. osc1: FM: `read(T₁, phase_ofs(ph₁, osc2·M))` with `phase_ofs(b, x) = b + 256·trunc(4194303·x + 268435392)`
     (32-bit; = x·2^30, i.e. x quarter-cycles, round 18 confirmed); sync: master `+= inc_m`, on wrap-around osc1's phase
     is replaced by the master's new value, then read; AM: `read(T₁, ph₁)·(1 − osc2·M)`;
  3. osc3 = read(T₃, ph₃);
  4. noise = `(float)rng()·2⁻³² − 0.5`.
  Left buffer `+= ` in the order `((noise·g_nL + buf) + osc3·g_3L) + (osc1·g_1L + osc2·g_2L)` (right likewise); then
  phases advance by their increments.
- After all slots: left and right buffers × the amp buffer (§1.5).

### 1.7 RNG and initial phases **[DECOMP `FUN_1800ef8c0`, `FUN_1800fe890`, `FUN_1800f12d0`; EXEC: global state peeked]**

- One **process-global** 32-bit LCG `s ← s·0x0bb38435 + 0x3619636b` (initial value 0 in the image), shared by every GMS
  instance in the process. Users: table dither (3 per sample per slot), noise (1), random start offsets (16 per start),
  and the voice reset `FUN_1800fe890` (per slot: osc1 = master phase = draw, 30 skipped draws, osc2 = draw, 30 skipped,
  osc3 = draw, 30 skipped → 16 × 93 draws per voice).
- The constructor resets all 99 voices and so does every sample-rate change (`FUN_1800f12d0`; the wrapper forwards
  Dispatcher id 4 only when the rate differs): after creation at 44.1 kHz the state has advanced 2 × 147 312 draws, 3 ×
  147 312 when the host then sets 48 kHz **[EXEC: peeked state 326288944 after the first 512-sample chunk = draw
  296 688]**. Voice phases are those left by the last reset (random), so a fresh voice without P47/P53 starts from them.
- Consequence for FL: the sequence depends on every GMS instance created and played before in the same process — FL's
  exact noise/dither/phase draws are not reproducible (log "random phases differ from FL by design"); the model
  reproduces a fresh instance exactly.

### 1.8 LFOs **[DECOMP `FUN_1800efb10`, `FUN_1800ef9f0`, `FUN_1800efa80`, `FUN_1800f5f00`, `FUN_1800f7530`; EXEC]**

Two engine-wide LFOs (not per voice), float phase φ ∈ [0, 1):
- Shapes (`FUN_1800ef9f0`): 0 `sin(2πφ)` (double sine), 1 triangle `2φ` folded to `2 − 2φ` above 1 (0…1), 2 falling saw
  `1 − φ`, 3 square `1` for φ < 0.5 else `0`.
- Rate (`FUN_1800efb10`, per chunk): sync mode `trunc(2·P36)`: 0 free, `f = (float)(25·clamp(1 − P34)² + 0.01)` Hz;
  1/2 tempo-synced, `v = {1/8, 1/4, 3/8, 1/2, 3/4, 1, 2, 4, 8}[clamp(trunc(9·P34))]` beats (×4 in mode 2), `f = 1/(60v/tempo)`;
  increment `(float)(f/fs)`. Per sample: value of the current φ, then `φ ← φ + inc`, `φ −= trunc(φ)`.
- Retrigger (P26/P27) resets φ to 0 at every note start. **Song lock**: while the host is playing (Dispatcher 12 → engine
  flag `+0x4470`), a synced LFO with retrigger off takes `φ = (4·beats·0.25)/v` at each chunk (beats = host position,
  §1.10) — not wrapped, so for shapes 1–3 the first sample of each chunk can fall outside the normal range until the
  wrap after that sample **[DECOMP + EXEC: −132 dB with a triangle at song position 13.37 beats]**.
- Amount `P28²`/`P29²` (sign from invert), destinations §1.4; envelope k with destination 3 multiplies LFO k.

### 1.9 Filter **[DECOMP `FUN_1800f32f0`, `FUN_1800efbc0`, cutoff table in the GMSynth constructor `0x1800f0ed2…0x1800f0f7a`; EXEC: −118…−136 dB incl. resonance 1.0, all three types]**

- Coefficient table (65 536 pairs, built once): `g_i = powf(2, i·1.4913369e-4)·0.0005`, `k_i = (float)((g_i·7.5 + 5)·
  0.756525)`; per sample index `clamp(trunc(cutoff·65536), 0, 65535)` (cutoff in the 0…1 parameter scale incl.
  modulation; g spans 0.0005…0.437, ≈ 9.77 octaves) — **the table does not depend on the sample rate**.
- Per lane (voice left, voice right), per sample, **four iterations with the same input x**: `s = x − (k·res)·y₄`,
  `s ← s/(0.04·s² + 1)`, `y₁ += (s − y₁)·g`, `y₂ += (y₁ − y₂)·g`, `y₃ += (y₂ − y₃)·g`, `y₄ += (y₃ − y₄)·g`
  (res = P13 via matrix, unsquared).
- Output by type `(char)trunc(2·P15)`: 0 → `y₄`, 1 → `y₃ − y₄`, 2 → `s − y₄` (s of the fourth iteration); times the
  per-sample level (§1.5), added to the output. States live in the voice and are cleared only by the voice reset.

### 1.10 Host time **[DECOMP `FUN_1800c7d90` ids 12/13, `FUN_1800c94e0`; EXEC with `run/gm_sp.c`]**

When the host has signalled "playing" (Dispatcher 12 value ≠ 0, applied at the next chunk), every Gen_Render asks the
host for the mixing time (host Dispatcher 0x24, index 0 = beats per the FL SDK — **inference** from the SDK constant
name) and passes it to the engine; after each chunk it adds `tempo·(1/60)·n·(1/fs)`. Only the synced-LFO lock (§1.8) and
the effect LFOs (§1.13) use it.

### 1.11 Host levels per note **[DECOMP `FUN_1800c91b0`, engine `+0x20`/`+0x220`/`+0x420`; EXEC]**

Same wrapper code as Sakura/Drumpad (round 28 §4.2): at every Gen_Render, for each held note the host's ComputeLRVol
(`Final.Pan`, `Final.Vol`) gives `T_L`, `T_R`, and `T[note] = (float)(Final.Pitch·0.01 − note + 60)`; notes without
a held voice inherit the previous held note's values (the first held note's values are copied downwards). GMS uses
`T_L/T_R` in the slot gains and `T` in the pitch (§1.4), so **FL's channel volume, pan, pitch knob, pitch bends and note
pan reach GMS only through these tables** — LYNX must not apply them again. Final.FCut/FRes are not used.

### 1.12 Master level and pan **[DECOMP + objdump `0x1800f4040…0x1800f41e0`; EXEC]**

`vol = db(80·P0 − 60)` (raw P0, no matrix; 0.75 → 0 dB), targets `L = max(clamp(1 − P1)·vol, 1e-4)`,
`R = max(clamp(P1)·vol, 1e-4)` (centre: −6 dB each side). If a voice was rendered or an effect is still active, each
sample `gL += (L − gL)·0.01`, `gR += (R − gR)·0.01`, output × g; otherwise the gains jump to the targets. Start-up quirk
reproduced by the model: both gains hold the marker −1 after a sample-rate set; the first active chunk sets `gL = L`, then
— because the right marker is still negative — `gL = R`, and `gR = 0` (so the right channel fades in over ~100 samples)
**[DECOMP + EXEC]**.

### 1.13 Effect slots and output EQ **[DECOMP `FUN_1800fefe0`, `FUN_1800ff500`, per-type processors; EXEC: the DLL's own `FUN_1800ff500` called directly by `run/gfx.exe`, 103 cases bit-exact]**

Ten effect objects (0x840 bytes, `FUN_1800fefe0(obj, k)`) at GMSynth `+0x39b8`, kind = slot k: 0 Distortion, 1 Crusher,
2 Flanger, 3 Phaser, 4 LowPass, 5 HiPass, 6 Delay, 7 Reverb, 8 Panner, 9 Trancer. `FUN_1800f32f0` calls
`FUN_1800ff500(fx, L, R, n, tempo)` for k = 0…9 in place on the summed, filtered voices, before the master gain.

**Parameter push** (`FUN_1800f5f00`, every chunk): enabled ⇔ P[147+k] ("Glue") ≠ 0 (or the slot is selected in the UI
and engine byte `+0x448b` is set); off byte `+0x24` = not enabled. **Only while enabled** (the enable flag left in AL by
`FUN_1800ff4d0` gates it, asm `0x1800f60e5`) the engine also pushes X = clamp P[117+k], Y = clamp P[127+k], LFO amount
`±(2·P[177+k] − 1)²`, shape `clamp(trunc(3·P[157+k]), 0, 4)`, rate `{1/8, 1/4, 3/8, 1/2, 3/4, 1, 2, 4, 8}[trunc(9·P[137+k])]`
beats (×4 when P[167+k] ≠ 0) and, while the host plays, the LFO phase `frac(beats/rate)`. The output EQ (P206 on, bands
P207…P211) is pushed to object 9 (clamped, a band redesigned only when its value changed). In `FUN_1800f32f0` a flag
starts as "a voice was rendered in this chunk" and is OR-ed with each object's `active` byte after it runs; **an object
is forced off (`FUN_1800ff4d0(fx, 1)`) only while that flag is still 0** — so a running delay or reverb tail keeps the
later slots alive **[DECOMP + EXEC: integration in `gmsmodel.Rack`]**.

**Common object behaviour** (`FUN_1800ff500`, `FUN_1800fea00`): nothing runs unless fs ≥ 1 and tempo ≥ 1; LFO period
`rate·60/tempo` s, increment `1/(fs·period)` (float, stored double); LFO = amount × (0 `sin(2π·old phase)`, 1 triangle
`2p`/`2 − 2p`, 2 ramp `p`, 3/4 square `p > 0.5`); Distortion, Crusher, LP and HP read it once per call and then advance
the phase by n − 1 more increments; Flanger, Phaser, Panner per sample; Trancer has its own phase. Wet crossfade per
sample `fade += ((off ? 0 : 1) − fade)·0.01` (starts at 0, ≈5 ms fade-in); when off, Distortion/Crusher/Flanger/Phaser/
LP/HP keep running while fade ≥ 1e-4, Delay and Reverb while their tail counter is positive, Panner and Trancer stop at
once. Reset `FUN_1800ff2c0` (sample-rate set, Dispatcher 2): clears filter/all-pass/EQ/delay states, fade, phase and tail
(not the tape-delay read positions); a changed rate re-creates the delay/reverb objects.

**Biquads** (LP `FUN_1800e4eb0`/`FUN_1800e4fb0`, HP `FUN_1800e4cc0`/`FUN_1800e4dc0`): `w = f·2π/fs` (float), `(s, c)` from the
DLL's own packed sinf kernel (`0x18012a460`: reduction by π in 4 parts, odd 4-coefficient polynomial; cos as
`sin(|w| + π/2)` with the addition in float; small |w| → w and 1 − w²/2), `a = (1 − c)/(s/q + 2)` (LP) or
`(1 + c)/(s/q + 2)` (HP), `b1 = c·(1/(s + q))·(−2q)`, `b2 = (q − s)/(s + q)`; LP `y = ((x1 + x1 + x) + x2)·a − y1·b1 − b2·y2`,
HP `y = (x − 2x1 + x2)·a − y1·b1 − b2·y2`; values within ±1e-9 flushed to 0 **[DECOMP + EXEC]**.

| Slot | Law **[DECOMP + EXEC, bit-exact]** |
|---|---|
| 0 Distortion (`FUN_1801000b0`) | `d = clamp(X + lfo)`, `k = (d²)²·1024`, `s = x/(|x|·k + 1)·0.5·(k + 1)`, then LP biquad f = 2^(4.28·Y + 10) Hz, q = 0.99; out = fade·LP(s) + (1 − fade)·x |
| 1 Crusher (`FUN_180100340`) | `c = clamp(1 − X + lfo)`; sample-and-hold of L/R for `64c² + 3` samples (float down-counter `+0x568`, re-samples when ≤ 0); same LP and crossfade as Distortion |
| 2 Flanger (`FUN_180100590`) | 6 tape lines of `trunc(0.002·fs)` samples, 3 in series per channel; time `1 − X − lfo` per sample, feedback −0.5·Y, gain −fade; out = x + chain |
| 3 Phaser (`FUN_180100e50`) | depth s smoothed (0.001, double) toward max(X², 1e-5); `w = clamp((s + lfo·s·0.5)·(hi − lo) + lo, 0.001, 0.499)` with lo = 50/fs, hi = 20000/fs; all-pass `a = (1 − w)/(w + 1)`, 6 first-order stages per channel on `x − 1e-6 + fb·last`, `fb = powf(0.6Y, 1/3)·0.98`; out = x + fade·wet |
| 4 LowPass / 5 HiPass (`FUN_1800ffc10`) | `v = clamp(X + lfo)`, `q = 2Y² + 1`; LP f = 2^(8.28v + 6), HP f = 2^(7.424v + 5) Hz; 3 cascaded biquads per channel; crossfaded |
| 6 Delay (`FUN_1800ff9d0`, `MFXTapeDelay`) | 2 tape lines of `trunc(1.5·fs)`; delay = LFO period (rate·60/tempo s, i.e. tempo-synced), passed as max(2/3·period, 0.01) and clamped to 0.99 inside (`d·(len − 2) + 2` samples); feedback X², input gain fade·X²; feedback path LP coefficient min(2Y, 1)², subtracted one-pole HP coefficient max(2Y − 1, 0)²; out = x + tape; tail `1.5·fs` samples after the last |in| or |wet| > 1e-4 |
| tape line (`FUN_1800fed00`/`FUN_1800fef90`) | samples stored with a position stamp in [0, 1); speed `1/((d(1 − 2/len) + 2/len)·len)` clamped to [1/len, 0.2]; the read head scans the previous cycle and interpolates linearly; at wrap the halves swap carrying the last 3 entries |
| 7 Reverb (`FUN_180100950`, class `fxReverb`) | plate network: input one-pole LP (+ optional HP), 4 input all-passes (142, 107, 379, 277 / 20000 × rate), pre-delay, two tank halves [damping shelf, LP, all-pass 672/908, delay 4453/4217, all-pass 1800/2656, delay 3720/3163], decay^(1/4) per stage, 7 + 7 output taps (266, 2974, 1913, 2496, 1690, 187, 1566 / 353, 3627, 1728, 3173, 1811, 335, 621), mid/side width; output added. Parameters quantised `floor(v·a + b)` (`FUN_180102170`); GMS pushes decay 0.75X³, damping Y², level 0.3 + 0.225X³, HP 0.1, LP 1, diffusion 1, size 1, pre-delay 0, dry −∞, early reflections off; level smoothing 0.001; tail 0.25·fs. **The reverb object always runs at 44.1 kHz** (re-created at the AudioEffect default rate, never told the host rate) **[DECOMP + EXEC 48/96 kHz]** |
| 8 Panner (`FUN_180101310`) | 2 tape lines of `trunc(0.002·fs)`, feedback 0, gain 1; `v = 2(X + lfo)`; L time clamp(v − 1), R time clamp(1 − v); L gain clamp(2(1 − (v − 1)Y)), R gain clamp(2(v − 1)Y + 2); output **replaces** the input |
| 9 Trancer (`FUN_1801007e0`) | runs only when on and rate > 0; gate from its own phase p: p < 0.01 → p·100, else `1 − min(1, ((p − 0.01)·1.0101)⁴·(50(1 − X)² + 1))`; smoothed 0.01; gain `1 − g·(1 − Y²)` |
| Output EQ (`FUN_1800e5050`/`FUN_1800e50a0`/`FUN_1800e5440`) | 5 bands, gain (P − 0.5)·24 dB, 80/220/1200/6000/12000 Hz, bandwidths 1/2.5/2.8/2.5/2 octaves; band 0 low shelf, 1–3 peaking, 4 high shelf (cookbook forms, A = 10^(dB/20), α from sinh(0.3465736·BW·w/sin w), w = f·6.283185/fs clamped to [0, 3], normalised by a0); runs after slot 9's processor, independently of its off byte; each band processes the whole chunk in turn |

Demo usage **[DATA §0]**: Distortion 5, Crusher 3, Phaser 1, HiPass 2, Delay 2, Reverb 5, EQ 3; Flanger, LowPass, Panner and
Trancer are unused but were verified too.

### 1.14 Voice end, polyphony, same key **[DECOMP `FUN_1800f68f0`, `FUN_1800fe890`, `FUN_1800c9c20`, `FUN_1800c9cd0`; EXEC]**

- A voice ends at the first chunk where it is released (amp target 0) and its amp envelope is below 1e-4; the reset
  (`FUN_1800fe890`) clears flags, note (−1), filter states, envelopes (amp, 1, 2 → 0), sets the level smoother marker −1 and
  draws new random phases for all 16 slots (§1.7). A mono voice is never reset while notes keep arriving: retriggers keep the
  amp envelope value, the level smoother, the filter states and the phases (unless P47/P53) **[DECOMP + EXEC "mono + slide
  legato" −135.9 dB]**.
- After each Gen_Render the wrapper kills the host voice of every held note whose engine voices are all inactive and whose
  velocity entry is 0 (released).
- Stealing (§1.3) restarts the stolen voice without a reset (its amp envelope value, level smoother, filter and phases
  continue) **[DECOMP + EXEC "max polyphony 2" −135.4 dB]**. A second note-on of a held key releases the old engine voice(s)
  of that key first.


### 1.15 Precision limits **[EXEC + model]**

- FM: the modulation index goes through `trunc(4194303·x + 2^28 − 64)` and the 10-bit read; float-level differences of x
  occasionally move a read by one table step → −90…−97 dB on FM patches (−89.8 dB with an LFO-modulated FM index).
- The effect chain is ill-conditioned in float32: the 3-stage HiPass at 32 Hz, the low shelf of the output EQ and the
  Delay/Reverb feedback amplify round-off differences. Scaling the model's own voice sum by (1 + 2·10⁻⁷) changes the full
  chain output of Plucks 2 / Bass Impacter / Song For You by −65.9 / −65.4 / −81.0 dB (`t_sens.py`, `out/t_sens.log`) —
  this is the floor of the "full" column below, not a model error (each effect alone is bit-exact on identical input,
  §1.13).
- The global RNG (§1.7): the model reproduces a fresh instance; in FL the draws depend on everything played before.

### 1.16 Verification against the DLL **[EXEC + model]**

Relative error `20·log₁₀(rms(model − DLL)/rms(DLL))`, worst channel, host block 512, `panlaw` (Final.Vol 1, pan 0),
tempo 140, ProcessEvent 1 = 0. **Demo battery** (`gbat.py`): 4 notes (C5 vel 100 0–30000, E5 vel 80 11025–40000, G5 vel 110
22050–52000, C4 vel 127 60000–70000 samples; Init.Vol from round 28's law), 2 s; "voice" = effect slots and EQ switched
off in the state, "full" = state as saved.

| # | Channel | voice 44.1 kHz | voice 48 kHz | full 44.1 kHz | full 48 kHz | full 44.1 kHz, block 1000 |
|---|---|---|---|---|---|---|
| 0 | FUTURE Sub | −140.0 | −140.1 | −125.0 | −131.9 | −125.2 |
| 1 | FUTURE Bass | −93.4 | −97.1 | −94.8 | −95.6 | −82.5 |
| 2 | Time: GMS | −136.1 | −135.1 | −135.9 | −135.1 | −135.9 |
| 3 | Plucks 2 | −130.1 | −130.2 | −69.0 | −67.0 | −67.0 |
| 4 | Africa's Calling: GMS | −131.5 | −131.3 | −131.5 | −131.3 | −131.3 |
| 5 | Synth 02 | −134.2 | −134.3 | −123.0 | −128.8 | −123.0 |
| 6 | Song For You: GMS | −136.0 | −135.5 | −81.9 | −81.0 | −82.2 |
| 7 | Disco Fuzz Te | −134.5 | −136.0 | −132.9 | −134.7 | −133.1 |
| 8 | Bass Impacter | −102.8 | −110.6 | −67.0 | −65.8 | −67.3 |
| 9 | Render test | −132.9 | −132.7 | −132.9 | −132.7 | −132.9 |

**GMS: 10/10 channels (9/9 demo channels) at −60 dB or better in every column** (logs `gbat_nofx_44k.log`,
`gbat_nofx_48k.log`, `gbat_fx_44k.log`, `gbat_fx_48k.log`, `gbat_fx_44k_b1000.log`). The weaker full-chain values are the
precision floor of §1.15 (FM in #1; HiPass/EQ/Delay in #3, #6, #8).

**Synthetic battery** (`gsyn.py`, `out/gsyn.log`, effects off, 3 notes, 1 s): **30/30 cases at −60 dB or better**: filter
types 1/2 −132.7/−133.0; resonance 1.0 + keytrack −118.2; AM −135.3; FM +7 st −96.6; sync + env1→mod −135.3; osc2 invert +
phase offset −135.3; noise 0.5 −135.1; unison 16 detune 1 stereo 1 −135.8; unison 5 phase retrig −134.4; LFO1 sine→pitch
−135.1; LFO1 triangle sync→cutoff retrig −134.9; LFO2 saw sync×4→volume inverted −134.7; LFO2 square→mod (FM) −89.8;
env2→LFO fade −135.5; env1 inverted→pitch −136.0; long ADSR −134.2; velocity→cutoff + amp velocity −132.5; velocity→amp
attack (code 20) −135.3; poly slide −135.6; mono slide legato −135.9; octave/pitch/cents −134.2; Final.Vol 0.551 pan −0.5
−134.9; Final.Pitch +37/−120 cents −135.6; max polyphony 2 (stealing) −135.4; same-key retrigger −136.2; master level/pan
−135.2; 48 kHz −128.8; blocks 128/1000 −133.2/−133.5. Song-position lock (§1.8, `gm_sp.exe`): DLL playing vs stopped
differs by −0.1 dB (i.e. completely), model vs DLL playing −131.8 dB. Wave tables of non-1024-sample, stereo, float and
renamed waves equal to ≤ 3.6·10⁻⁷ (`t_waves.log`). Effects: 103 direct-call cases bit-exact (`t_gmsfx.log`, §1.13).

---

## 2. Morphine — the complete engine

- **Framework:** the same `CFLWrapper` as GMS, Sakura and Drumpad; the engine is `CMorSynth`, held at plugin `+0x228`.
- **Wrapper VMT** `0x1801732d0` **[DECOMP + DATA]**: 1 Dispatcher `FUN_1800c3b30`, 3 SaveRestoreState `FUN_1800c4090`,
  5 ProcessEvent `FUN_1800c4500`, 8 Gen_Render `FUN_1800c5900`, 9 TriggerVoice `FUN_1800c5a70`, 10 Voice_Release
  `FUN_1800c5b90`, 11 Voice_Kill `FUN_1800c5bb0`; per-block host levels `FUN_1800c4f50` (second VMT, slot 10).
- **Engine (CMorSynth VMT `0x180176650`):** process `FUN_180112de0`; sample rate `FUN_180112d50` → `FUN_1800e8350`;
  state load `FUN_180112c60` → `FUN_180112ad0` → `FUN_18010ea30`/`FUN_18010f060`; parameter handler `FUN_180111ec0`;
  note on `FUN_1800e93d0` → `FUN_1800e8580` (poly) / `FUN_1800e8b70` (mono); note off `FUN_1800e9430`; master output gain
  `FUN_1800e9160`; random generator `FUN_1800e93a0`.
- **Voice (`CMorGenerator`, VMT `0x180176088`):** start `FUN_1800fffa0`, release `FUN_180100620`, reset `FUN_1801000e0`,
  render `FUN_1801006a0`, modulation update `FUN_180100cd0`, amplitude `FUN_180100c20`, resynthesis update
  `FUN_1801001c0`, noise set-up `FUN_180100300`, noise render `FUN_180100880`, pitch `FUN_1800e9e90`/`FUN_1800e9e50`,
  glide `FUN_1800e9d90`, unison L/R `FUN_1800e9f30`.
- **Modulation (`CModProcessor`):** `FUN_1800fbed0`, `FUN_1800fb820`, `FUN_1800fb9d0`, matrix `FUN_1800fbbc0`,
  `FUN_1800fbfd0`, `FUN_1800fbff0`; **Eg1–4:** `FUN_1800f7880`, `FUN_1800f7930`, `FUN_1800f7b10`, `FUN_1800f3e20`;
  **master ADSR:** `FUN_1800da760`, `FUN_1800da830`, `FUN_1800daa00`, `FUN_1800da9b0`, `FUN_1800da9d0`; **morph line
  (`CMorphLineProcessor`):** `FUN_180105390`, `FUN_1801054b0`, `FUN_180105990`, `FUN_180105df0`, `FUN_180105940`,
  `FUN_180101e20`, `FUN_180101b60`; **generator (`CZoneGen`):** `FUN_18012c500`, `FUN_18012c570`, `FUN_18012c850`,
  `FUN_18012cd10`, `FUN_18012d020`, `FUN_18012d370`, `FUN_18012cc80`, `FUN_18012c7a0`; **frame player:** `FUN_1800eef20`,
  `FUN_1800eb970`; **PWM:** `FUN_1800f8460`, `FUN_1800f83b0`, `FUN_1800f8c40`; **resynthesis (`CResynth`):**
  `FUN_1801220c0`, `FUN_180122ae0`, `FUN_180121b40`, bank render `FUN_180115860`; **noise:** interpolator `FUN_1800da140`,
  filter `FUN_180113b70`/`FUN_180113df0`.
- **Effects host `FUN_1800ff630`:** overdrive `FUN_1800ff800`/`FUN_1800ff7e0`; chorus `FUN_1800f2bc0`, `FUN_1800f2a10`,
  `FUN_1800f2960`, `FUN_1800f29c0`, `FUN_1800f28e0`; tape line `FUN_1800e9550`, `FUN_1800e9810`, `FUN_1800e9bb0`,
  `FUN_1800e9750`; delay `FUN_1800f3000`, `FUN_1800f2e90`, `FUN_1800f3080`, `FUN_1800f2d70`…`FUN_1800f2e50`; reverb
  `FUN_180125020`, `FUN_1801247d0`, `FUN_180124650`, `FUN_1801246c0`, `FUN_180124e50`, `FUN_180124d00`/`d70`/`cd0`;
  high-pass biquad `FUN_1800df680`/`FUN_1800df780`; EQ `FUN_1800f7e70`, `FUN_1800f7ba0`, `FUN_1800f7db0`, `FUN_1800dfa60`,
  `FUN_1800dfe00`.

### 2.0 Data survey: the 7 demo channels **[DATA, `r39/py/mor_survey.py`, `out/mor_survey.txt`]**

Field meanings are those of §2.2. "Eg1→pitch gated" = the matrix also routes CC1 (mod wheel) to "Eg1 amount", so with the
mod wheel at 0 the Eg1→pitch route has no effect.

| # | Channel | Generators / zones / frames | Voice | Modulation | Noise | Effects |
|---|---|---|---|---|---|---|
| 0 | Africa's Calling: Synth 03 | A only, 1 zone, 99 frames (duration 1), ping-pong loop on frames 96–98, partial pan used | mono (A3 = 0), glide mode 2, T = 0.039 s | Eg1→pitch gated by CC1 | none | chorus, delay, EQ |
| 1 | Africa's Calling: Lead Synth | same state as #0, master 0.687 | as #0 | as #0 | none | chorus, delay, EQ |
| 2 | NIGHT TERROR: Morphine | A, 1 zone, 137 frames, loop 50–136 mode 3, detune range 1 | poly 32 (A3 = −1) | Eg1 (bipolar) → pitch, active | none | chorus, delay, reverb, EQ |
| 3 | SHINE: Morphine | A, 47 frames, rate 0.055, loop 16–20, PWM mode 2, phase 0.485 | mono | Eg1 gated by CC1 | WhiteThin.wav, mix 0.42, loop mode 3, filter off | chorus, delay, reverb |
| 4 | SHINE: Morphine #4 | A, **25 keyboard zones** (275 frames, duration 12) | poly 32 | Eg1 gated by CC1 | none | none |
| 5 | SHINE: Morphine #2 | A, 147 frames, **tempo-synced**, forward loop 96–148, PWM mode 2 | mono | none | none | chorus, reverb, EQ, **overdrive 0.215** |
| 6 | Madonna: Soft Pad | A (85 frames) + B (393 frames), **morph line A→B** over 506 units | poly 16, velocity curve 0.385 | Eg1 gated by CC1 | AmbientNoise_01.wav, mix 0.505, loop 0, filter off | chorus, delay, EQ |

Every channel uses generator A, one morphs to B; mono 4, glide 1, zones 1, tempo sync 1, PWM 2, noise 2; effects: chorus
6, delay 4, reverb 3, EQ 5, overdrive 1. No demo uses release loops, the noise filter, PWM mode 1, unison (hidden
parameters) or MIDI CC/aftertouch/bend sources **[DATA]**.

### 2.1 Structure and data flow **[DECOMP]**

- **Per host block (`FUN_180112de0`):** every voice in the list renders and adds into the engine's float stereo buffer;
  then the effects (§2.11); then a per-sample master gain (§2.12).
- **Per voice (`FUN_1801006a0`)** the work runs in control blocks of **B = int(fs·0.0029)** samples (127 at 44.1 kHz, 139
  at 48 kHz) **[DECOMP + EXEC peek of voice `+0xf8`]**.
- **On the voice's first render:** modulation updated with B = 1, resynthesis set up with B = 1, one sample rendered into a
  discarded buffer.
- **At every block start:** modulation update; master ADSR advanced by B (amplitude); noise set-up; resynthesis update at
  blocks 1 and 2, then every third block (5, 8, 11, …) — the first update ramps over B, later ones over 3B.
- **Between block starts:** the partial bank and the noise render sample by sample.

### 2.2 State format (completes round 16 §3.1) **[DECOMP readers; DATA: the parser reaches the exact end of 7/7 states]**

- **Frame records (MSP0 entries):** `f32 gain` clamped to 0…1 (asm `maxss`/`minss`, not an int).
- **MBP0 arrays** (|x| < 1e-9 stored as 0, `FUN_1800ef5c0`): `[0]` = amplitude stored as a **cube root** (cubed at render,
  §2.7), `[1]` = **pan offset**, `[2]` = **detune in harmonic numbers**. (Round 16 read `[1]`/`[2]` as frequency
  deviation/phase — corrected here.)
- **Zone:** `i32 key width` (min 1), `f32 level` (0…1); zones are chosen by subtracting widths from the note number.
- **MSP0 option block (0x424 bytes, `FUN_1800ec5d0`):** w1 loop start, w2 loop end (time units), w3 release-loop flag,
  w4 loop mode, w5 rate, w6 key-track, w7 velocity-track, w8 tempo-sync flag, then 128 floats of per-partial velocity
  sensitivity (`+0x60`) and 128 floats of per-partial gain (`+0x68`).
- **MVC0 options:** PWM rate (f32), PWM mode (int ×0.5), PWM phase (f32), PWM free (bool), detune range (f32), pan range
  (f32) (`FUN_1801135b0`).
- **MML0** (morph line, `FUN_180102450`): points (X, Y, i32 duration); options (0x54 bytes): loop start/end/mode, tempo
  sync, mix A–D, pan A–D, mute A–D, rate / key-track / velocity-track, a word at `+0x80` ("solo" mode 1–4).
- **MEG0** (Eg, `FUN_1800f46e0`): points (level 0…1, i32 duration); options: loop start/end, release-loop flag, velocity,
  loop mode, rate, tempo sync, velocity-track, key-track, bipolar flag.
- **MMD0 matrix** (0x94 bytes): 12 slots of (source 0…136, destination 0…47, f32 amount 0…1).
- **Global block** (parameter index → field, `FUN_18010e340`): Master Level A0 (`+0x608`), Glide A1, Glide Mode A2,
  Polyphony A3, Velocity Curve A4, Transpose A5, Fine A6, Overdrive A7, Bend A8, master ADSR A9–A12, chorus A13–A16,
  delay A17–A22, reverb A23–A26, EQ bands A27–A34, EQ on A35, noise mix/pan/mute/velocity-sensitivity A36–A39; noise
  ratio (block offset 164), noise loop mode (168, a float read as int), noise sample name (172), preset name (1200), noise
  filter mode (1456, float read as int), cutoff (1460), resonance (1464), key-track (1468), mono (1472).
- **Hidden parameters** 0x8d–0x90 (unison and an analyser) are not saved in presets; unison stays at 1 voice
  **[DECOMP + EXEC peek `+0x38f0` = 1]**.

### 2.3 Wrapper, levels and polyphony event **[DECOMP + EXEC]**

- note = trunc(Init.Pitch/100); MIDI velocity = trunc(127·ln(1 + 10·Init.Vol)/ln 21) (round 28 §4.2); the engine gets
  vel/127 **[EXEC: 6 Init.Vol values match]**.
- Per host block the wrapper writes per-note tables: L/R = host ComputeLRVol(Final.Pan, Final.Vol), pitch offset =
  Final.Pitch·0.01 − note + 60, FCut/FRes (unused by the DSP) **[DECOMP `FUN_1800c4f50`]**.
- **ProcessEvent 1 (max polyphony)** sets the host voice limit; value 0 → 32 (`FUN_1800e8080`). Without it the limit stays
  0 and every note takes the first voice of the list **[DECOMP `FUN_1800e80a0` + EXEC: no event = model with limit 0 at
  −122.6 dB; with limit 32 the error is +4.2 dB]**. Whether/what FL sends at creation is **not confirmed**; all tests send
  `pe=1,0`.

### 2.4 Voices, polyphony, mono, glide **[DECOMP `FUN_1800e8580`, `FUN_1800e8b70`, `FUN_1800e9430`; EXEC]**

- 32 voice objects in list order; polyphony = int(A3) + 1 (below 1 or above 32 → 32); A3 == 0 → mono.
- Velocity curve: vel' = powf(vel, 5·A4).
- Random number per note: LCG with seed 2222 (`.data`), `s = s·0xbb38435 + 0x3619636b` (the same constants as GMS's LCG),
  used as s·2⁻³²; the random-detune scale (`+0x38cc`) is 0 in every state, so it has no audible effect **[EXEC peek]**.
- Poly allocation: first free voice among min(polyphony, host limit); if none, steal the oldest released voice, else the
  oldest active one. A retrigger keeps the voice's control-block phase; the ADSR restarts from its last value unless it
  had finished.
- Mono (`FUN_1800e8b70`): only the first voice; a note arriving while that voice is active and not released changes only
  the target note (legato: glide continues, no envelope restart); at note-off, if another key is held, the voice returns to
  the **nearest held key** (glide rate 12/(fs·T)), otherwise it releases.
- Glide: T = A1³ s (< 1e-4 → 1e-4; < 0.001 s = no glide); start = last note played; modes 0/1: rate 12/(fs·T) semitones per
  sample; modes 2/3: |note − start|/(fs·T) (constant time); modes 1/3 do not glide when the voice was free or released;
  applied at every resynthesis update (position += rate·B′).

### 2.5 Modulation **[DECOMP `FUN_1800fbbc0`, `FUN_1800fb9d0`, `FUN_1800f7b10`, `FUN_1800f3e20`; EXEC]**

- Sources: 1 key/120, 2 curved velocity, 3 pitch bend, 4 channel pressure, 5–8 Eg1–4, ≥ 9 MIDI CC (n − 9; CC7 = 1 and
  CC10 = 0.5 at reset, the others 0).
- Each slot adds clamp(src·(2a − 1), −1, 1) to its destination; a destination never written reads 0 (internal "unset" =
  −1000). The matrix is recomputed after the Eg evaluation of each block, so Egs read the **previous** block's matrix.
- Destinations: 1 amplitude (×10^m); 2 pan (partials and noise); 3 pitch ×2^(m·|m|) octaves; 4/5/7/6 master
  attack/decay/sustain/release; 8–12 noise mix, pan, ratio, cutoff, resonance; 13 + 4g … 16 + 4g generator g rate, PWM
  rate, detune range, pan range; 29 morph rate; 30/31 morph X/Y; 32 + 2g, 33 + 2g mix and pan of generator g; 40 + 2k
  Eg(k+1) **amount** (default 1); 41 + 2k Eg(k+1) rate. Knob + modulation = clamp(knob + m, 0, 1).
- Eg: position advances first by (1/fs)·M·2^((2kt − 1)(key − 48)/12 + 8·(rate + (2vt − 1)(vel − 0.5)) − 4)·B, M = 100 (or
  tempo·0.41667 when synced); value = linear breakpoint interpolation through the loop mapping (`FUN_1800eb970`, S/E loop
  start/end, L = E − S): mode 1 p − trunc((p − S)/L)·L when p > E, mode 2 mirror, mode 3 ping-pong. Output: bipolar flag →
  2v − 1; × (1 − (1 − vel)·Velocity); × amount.

### 2.6 Master ADSR **[DECOMP]**

A = max(15·√a·a³, 0.003) s; D = max(30·d³, 0.003); S = s²; R = max(10·r³, 0.003) (round 16's laws confirmed); stage times
floored at 1 ms inside the envelope (`FUN_1800da760`); attack linear, decay/release shaped `t′ = 1 − 10^(−2t)·(1 − t)`
(`FUN_1800daa00`); evaluated once per block after advancing by B; decay ending at S = 0 or release ending sets "done", the
voice is freed at the second following block; amplitude = env·10^(mod₁).

### 2.7 Generators, zones, frames, PWM **[DECOMP `FUN_18012c850` and callees; EXEC]**

- Zone level L = sign·|level|^(1/3).
- Frame player (evaluated before advancing): frame i at the loop-mapped position, f = position within frame / duration;
  with a next frame weights 1 − f and f, gains gainᵢ(1 − f) and gainⱼ·f; without one, frame i alone; advance: position +=
  B′·2^(same rate law as §2.5)·100/fs (or tempo·0.41667/fs when synced).
- Per partial k: amp = ((vel − 1)·vs[k] + 1)·(L·(gAᵢ·ampᵢ[k] + gBⱼ·ampⱼ[k]))³·gain[k]; pan = pr²·L·(interpolated pan);
  ratio = (k + 1) + dr²·L·(interpolated detune).
- PWM mode = int(2·mode + 0.5): φₖ += ratioₖ·(rate·0.25/fs)·B′ wrapped to [0, 1); mode 2: amp × (1 − cos 2πφ); mode 1: amp
  × cos 2πφ; initial φₖ = frac((k + 1)·phase) on every note unless "free".

### 2.8 Morph line and generator combination **[DECOMP `FUN_1801054b0`, `FUN_180105990`]**

X and Y from the breakpoint line at the morph position (before advancing) + modulation; weights A = (1 − X)Y, B = XY,
C = X(1 − Y), D = (1 − X)(1 − Y); mix gains = mix²·weight (mute → 0); amp = Σ gains·amp_g; pan = Σ w_g·(pan_g + 2·genpan_g − 1);
ratio = Σ w_g·ratio_g; morph position += rate·(100/fs)·B′.

### 2.9 Resynthesis bank **[DECOMP `FUN_1801220c0`, `FUN_180122ae0`, `FUN_180115860`; EXEC]**

- Per update, per partial: f = max(ratio·f₀·pitch-mod factor, 1), amplitude 0 above 0.4535·fs; p = clamp((panmod + pan +
  1)/2); gL = (1 − p)·amp·T_L, gR = p·amp·T_R, each × taper float((1 − f/(0.4535fs))·0.6 + 0.4); w = 2·sin(π·f/fs) (float);
  a double phase accumulator advances by f·B′/fs.
- Partials whose old and new gains are all below 1e-6 are dropped; a reappearing partial restarts from (cos 2πφ,
  sin 2πφ); active partials are compacted (double buffers); gain and w deltas spread over B′.
- Per sample (float32): g += dg; X −= w·Y; Y += w·X; w += dw; output = (Σ g·X)·env, with env a **Catmull-Rom** cubic over
  the last four block amplitudes, two blocks behind the newest.
- Fundamental (Hz) = 220·2^((p − 57)/12), p = transpose + fine + glided note + FL pitch offset (+ bend × range).

### 2.10 Noise **[DECOMP `FUN_180100300`, `FUN_180100880`, `FUN_1800da140`, `FUN_180113b70`/`df0`; EXEC]**

Sample = the WAV from `Noise Samples` (int16/32768, + 4 zero samples); loop points from `smpl`, else 0…length (both
clamped to length − 1); gain 2·knob⁴ (0 if muted), pan from knob and pan modulation, × (1 − (1 − vel)·sens) × env; gains
ramp linearly per block (no Catmull-Rom); speed 2^((ratio − 0.5)·4)·sr_sample/fs with 4-point interpolation; loop modes
0–3 with a per-voice direction state; filter modes 1/2/3 (LP/BP/HP biquads): ω = min(2^((pitch − 57)·0.1579·kt +
9·cutoff)·219.91/fs, 3.1), Q = clamp(30·res² + 1, 1, 31).

### 2.11 Effects (in this order) **[DECOMP + EXEC]**

- **Overdrive:** d → 16·p³ smoothed 0.01 per sample; when d > 0.01: y = 1 − 10^(−d·x)(1 − x) for x ≥ 0, else
  10^(d·x)(x + 1) − 1; output y·((1 − d/16)⁸·0.7 + 0.3).
- **Tape line** (chorus and delay): each sample written with its tape time; write speed base·(1 + depth·(3t² − 2t³)),
  t = triangle LFO, clamped to [1/N, 0.2]; two buffers swap when tape time passes 1, the previous cycle read back with linear
  interpolation (3 entries carried across a swap); output y·wet (wet smoothed 0.01), feedback y·fb + x, base speed smoothed
  0.005.
- **Chorus:** 3 lines of N = int(0.02·fs·{1.1, 0.854, 1}), initial tape positions {0, 0.467, 0.778}; input L + R;
  L += c + a, R += c + b; depth p³, wet −p², rate (5p + 0.1)·{0.896, 1, 0.689} Hz.
- **Delay:** 2 lines of N = 2fs, positions 0/0.7, LFO rates 1/0.876 Hz; time t = (0.95T + 0.05)²/2; stereo ±(s − 0.5)·t;
  modulation 0.02·p²; feedback 0.84·p; wet p²; "Delay On" resets the lines.
- **Reverb** (per channel): 50 Hz high-pass, 8 parallel combs (L 1016, 1288, 1177, 1456, 1322, 1591, 1457, 1717; R 1216,
  1088, 1377, 1256, 1522, 1391, 1657, 1517; × fs/40091), 4 all-passes (249, 210, 198, 145 / 244, 220, 178, 155, gain
  0.5); feedback p^¼·0.67 + 0.3; damping min(p^¼·0.8 + 0.2, 1); mix 0.2·p³.
- **EQ:** 8 RBJ bands at 62.5 Hz·2^k (low shelf, 6 peaks, high shelf), 1 octave, A = 10^(g/20), g = ±(2v − 1)²·12 dB.
- **No bypass:** the "silence" bypass in `FUN_1800ff630` never triggers (its counter `+0x584` is only ever written with
  0) **[DECOMP + objdump]**.

### 2.12 Master output **[DECOMP `FUN_1800e9160`, `FUN_180111ec0`; EXEC]**

Gain target = level·CC7·0.5, level = 10^(2.4·A0 − 1.2), pan neutral; per sample g += (target − g)·0.005; start value 0.5;
a sample-rate **change** sets g to the current target **[EXEC peeks]**. (This equals round 16's display law 48·A0 − 24 dB, times
the fixed 0.5.)

### 2.13 Precision limits and quirks **[EXEC + model]**

- The EQ and the reverb's 50 Hz high-pass amplify float round-off (poles near z = 1): the DLL's own float32 EQ differs from
  a float64 EQ by −77 dB, and a 1·10⁻⁷ input perturbation gives −77 dB — the floor of the "full" column below.
- The reverb high-pass coefficients come from an SVML-style float sin/cos ~1 ulp off; the coefficients the DLL produced at
  44.1, 48, 88.2 and 96 kHz were peeked and are used by the model (other rates fall back to correctly rounded math, about
  −78 dB on the reverb).
- Sample-rate-change quirk: a rate change re-applies the chorus's already-transformed wet value (wet becomes −(p²)²) and
  resets the overdrive smoother; the harness sets the rate after loading the state, so this applies there at 48 kHz.

### 2.14 Verification against the DLL **[EXEC + model]**

3 overlapping notes (keys 60/64/67, Init.Vol 1.0/0.5/2.0, note-offs at 0.68/1.13/1.59 s), 3.0 s, host block 512,
`panlaw`, tempo 120, `pe=1,0`; error 20·log₁₀(rms(model − DLL)/rms(DLL)), worst channel (`out/mor_bat_voice44.log`,
`mor_bat_full44.log`, `mor_bat_full48.log`).

| # | Channel | 44.1 kHz, voice (effects off) | 44.1 kHz, full | 48 kHz, full |
|---|---|---|---|---|
| 0 | Synth 03 | −124.7 | −77.7 | −75.6 |
| 1 | Lead Synth | −124.7 | −77.7 | −75.6 |
| 2 | NIGHT TERROR | −113.6 | −75.7 | −74.7 |
| 3 | SHINE | −120.7 | −95.8 | −93.8 |
| 4 | SHINE #4 | −117.8 | −117.8 | −134.6 |
| 5 | SHINE #2 | −120.6 | −79.3 | −79.6 |
| 6 | Soft Pad | −127.6 | −77.1 | −75.5 |

**Morphine: 7/7 demo channels at −60 dB or better in every column.** Re-checked independently with another note pattern
(C5 Init.Vol 1.0 0–30000, G5 Init.Vol 0.5 11025–50000, 1.5 s): SHINE #4 −121.9 dB, SHINE −96.2 dB. Single effects on the
demo channels that use them: chorus −122.5/−114.3, delay −124.6/−113.7, overdrive −120.6, reverb −84.7/−95.3/−101.7, EQ
−77.1…−80.6 dB; noise alone −100…−137 dB (loop modes 1/2/3, ratio, pan, mono, filters LP/BP/HP); synthetic cases −110…−134
dB (poly 2 with stealing, mono glide modes 0/1/3, poly glide mode 2, tempo 140, transpose/fine/bend, velocity curve,
ADSR, FinalLevels vol/pan/pitch, master level, PWM mode 1/free/phase, Eg1 tempo sync/velocity/key-track/loop modes 1 and
2/unipolar, 48 kHz); overdrive 0.8 on top of the saved effects −75.9 dB; host blocks 100/1024 the same −77.1 dB
(`out/mor_t_*.log`).

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| G1 | GMS structure | CFLWrapper → GMSynth; chunks ≤ 512; per chunk: control values, per-sample envelopes, 3 wavetable oscillators + noise per unison slot, 4-pole ladder per voice lane, 10 effect slots + EQ, master **[DECOMP+EXEC]** |
| G2 | Wave tables | 512 band-limited cosine-sum tables of 1024 points per wave, built from a 4-point cubic resampling (one-sample shift) and the spectrum; dithered 10-bit read, no interpolation **[DECOMP+EXEC]** |
| G3 | RNG | one process-global LCG (×0x0bb38435 + 0x3619636b) for dither, noise, start offsets and voice-reset phases; 2 × 147 312 draws after creation at 44.1 kHz (3× at other rates) **[DECOMP+EXEC]** |
| G4 | Modulation laws | sync ×(3m+1) with unsquared P50; FM = x quarter-cycles with x = osc2·15·M; AM ×(1 − 3·M·osc2); pitch 24 st per unit of envelope, 12 st per LFO unit; matrix code 12 → env1 amount **[DECOMP+EXEC]** |
| G5 | Envelopes / glide / level | exponential followers 8.82/(T·fs); amp output e²; env1/2 A 300x⁵, D 70(0.8x+0.2)⁵; release 25(0.55x+0.1)³; level 200·db(50x−25) smoothed 0.01, starting from P8² **[DECOMP+EXEC]** |
| G6 | Filter | 65 536-entry (g, k) table independent of fs; 4 iterations per sample; saturation s/(0.04s²+1); outputs y4 / y3−y4 / s−y4 **[DECOMP+EXEC]** |
| G7 | LFOs | engine-wide; free 25(1−x)²+0.01 Hz or synced 1/8…8 beats (×4); song-position lock while playing (unwrapped first sample) **[DECOMP+EXEC]** |
| G8 | Host interface | velocity from Init.Vol; per-note L/R (ComputeLRVol) and pitch tables; max-polyphony event required (0 → 99); voice handle = note **[DECOMP+EXEC]** |
| G9 | Effects | 10 slots in fixed order (Distortion … Trancer) + 5-band EQ on slot 9; laws in §1.13; reverb always at 44.1 kHz internally **[DECOMP+EXEC, 103 cases bit-exact]** |
| G10 | Master | db(80·P0 − 60), linear pan, 0.01 smoother with the start-up marker quirk **[DECOMP+EXEC]** |
| G11 | GMS verification | **9/9 demo channels (+ render test) at ≤ −60 dB** at 44.1 and 48 kHz, voice only (−93…−140) and full (−66…−135); 30/30 synthetic voice cases (−90…−136); effects bit-exact **[EXEC+model]** |
| M1 | Morphine state | complete tree; MBP0 = amplitude (cube root), pan, detune **[DECOMP+DATA]** |
| M2 | Morphine voice | partial bank of coupled-form float oscillators updated every 3 control blocks of int(0.0029·fs), Catmull-Rom envelope, zones/frames/morph/PWM, matrix with 47 destinations, master ADSR **[DECOMP+EXEC]** |
| M3 | Morphine effects | overdrive, tape-line chorus/delay, freeverb-style reverb, 8-band RBJ EQ, no bypass **[DECOMP+EXEC]** |
| M4 | Morphine verification | **7/7 demo channels at ≤ −60 dB** at 44.1 and 48 kHz (voice −114…−128, full −75…−135); EQ/reverb float floor ≈ −77 dB **[EXEC+model]** |

---

## 3. For LYNX — implementation guide

### 3.1 GMS

Reference implementation: `r39/py/gmsmodel.py` (+ `gmspatch.py`, `gmsfx.py`, `gmseq.py`, `gmsrev.py`) — port it 1:1.

```python
import sys; sys.path.insert(0, '/home/kauan/.claude/jobs/f8789b49/tmp/r39/py')
import gmsmodel
pl = gmsmodel.GmsPlugin(state_blob, fs=44100, maxblock=512, tempo=140.0)   # 0xD5 blob ("SSLF"...)
y = pl.run(nsamples, [(0, 'on', dict(key=60, ivol=1.0, fvol=1.0, fpan=0.0, pofs=0.0)),
                      (22050, 'off', 0)], songpos=None)      # y: (n, 2) float64; 'off' takes the note-on index
```
(`ivol` = Init.Vol, `fvol`/`fpan` = Final.Vol/Pan, `pofs` = Final.Pitch offset in cents; `songpos` = song position in
beats at sample 0 while the transport plays, None = stopped; `pl.eng.npoly2` = max polyphony from ProcessEvent 1;
`GmsPlugin(..., fx=False)` = voice only; `GmsEngine(..., rng_state=s)` starts the global LCG elsewhere. Comparison with
the DLL: `gcmp.run(blob, [(on, key, vel, off[, pofs]), …], n, fs, blk, …)` → (dll, model, log, plugin), `gcmp.err`.)

1. **Replace `synthFromGms`** (3 LYNX oscillators, LYNX filter, ADSR and LFO mapping of round 12) by a dedicated
   `GmsInstrument` (pattern of the Sakura/Harmless/Harmor/Ogun instruments) that keeps the 212 raw floats (no
   sanitising is needed: out-of-range destination codes are inert, §1.2) and the three wave base names; load the waves at
   run time from the installation's `GMS\Synth Waves` (never copy them into the repository) and build the 512 tables per
   wave as §1.6 (one-off cost: 512 × 1024 cosines per wave; cache per wave).
2. **Engine** as §1.1: chunks ≤ 512; per-chunk control values in voice-list order (§1.4); per-sample envelopes, glide,
   level and modulation (§1.5); oscillators per unison slot with the 4-draws-per-sample dithered read (§1.6); the
   process-global LCG with the voice-reset draws (§1.7); LFOs (§1.8); 4-iteration ladder (§1.9); effect slots + EQ
   (§1.13, `gmsfx.py`); master (§1.12). Keep float32 arithmetic and the given operation orders (exactness, not sound).
3. **Host levels:** compute Final.Vol/Pan/Pitch per note as round 28 §4.4 and feed them through the per-note tables of
   §1.11; **do not apply LYNX's channel volume/pan/pitch a second time**. Send the tempo, a max-polyphony event (value 0 →
   99 voices, **inference** about FL's value), and, while playing, the song position in beats (synced LFOs with
   retrigger off and the effect LFOs lock to it).
4. **Velocity:** `vel7 = trunc(127·ln(1 + 10·Init.Vol)/ln 21)` from LYNX's Init.Vol (round 28 inference
   `Init.Vol = (11^(vel/100) − 1)/10`).
5. **Random:** one LCG per LYNX process (FL's is per process and shared by all GMS instances); FL's actual draws are not
   reproducible — log "GMS noise/dither/phase draws differ from FL by design".
6. **Automation:** parameters are read every chunk (≤ 512 samples) through the matrix, except values latched at note
   start (envelope rates, glide start, unison/start phases) and the effect parameters, which are pushed only while the
   slot is enabled.
7. Remove the GMS approximation entry from STATUS.md only after the C++ port matches `gmsmodel.py` on the 9 demo channels
   (+ render test) at 44.1 and 48 kHz to about −60 dB with the same RNG start.

### 3.2 Morphine

Reference implementation: `r39/py/mormodel.py` + `morfx.py` + `morpatch.py` — port them 1:1.

```python
import sys; sys.path.insert(0, '/home/kauan/.claude/jobs/f8789b49/tmp/r39/py')
import morpatch, mormodel
f, c, blob = morpatch.demos()[3]                                  # or any Morphine 0xD5 blob
p = mormodel.prep(morpatch.Patch(blob))
syn = mormodel.Synth(p, fs=44100, tempo=120.0, maxblock=512)       # options: g0=, rate_change=, syn.hostmax
y = syn.run(n, [(0, 'on', dict(key=60, ivol=1.0, fvol=1.0, fpan=0.0, pofs=0.0)), (22050, 'off', 0)])   # (n, 2)
```
(`ivol` = Init.Vol; `'off'` takes the note-on index. Comparison with the DLL: `mor_cmp.run(blob, [(on, key, ivol, off),
…], n, fs)` → (dll, model), `mor_cmp.err(m, d)`.)

1. **Replace `synthFromMorphine`** (today: the generators' average spectrum on LYNX's harmonics oscillator with a generic
   ADSR) by a `MorphineInstrument` that parses the whole tree (§2.2) and keeps it in the channel.
2. **Voice** exactly as §2.3–§2.10: control block B = int(fs·0.0029), the 1-sample pre-roll, resynthesis updates at blocks
   1, 2, 5, 8, …; partial gains = cubed amplitudes; Nyquist taper; drop/restart rule at 1e-6; float32 coupled
   oscillators; Catmull-Rom amplitude two blocks late; the LCG with seed 2222; voice stealing and mono legato/return to the
   nearest held key; **the Eg-amount destinations** (they silence Eg1 in 4 demo channels while the mod wheel is 0).
3. **Levels:** Final.Vol/Final.Pan through law A per note inside the instrument (no second channel volume/pan), then the
   master target level·0.5 with the 0.005 smoother. Send the tempo and behave as if max polyphony 0 was received (→ 32
   voices; FL's value **not confirmed**, log it).
4. **Noise** sample read at run time from the installation's `Noise Samples` (never bundled).
5. **Effects** in the order overdrive, chorus, delay, reverb, EQ; use the peeked reverb high-pass coefficients for
   44.1/48/88.2/96 kHz; the rate-change chorus quirk (wet −p⁴) depends on the host setting the rate after the state load —
   LYNX should use −p² and log it.
6. Accept criterion: the C++ port against `mormodel.py` at ≤ −60 dB on the 7 demo states (44.1 and 48 kHz); then remove the
   Morphine line from STATUS.md.

### 3.3 Port status (LYNX)

Both engines are ported 1:1 (`src/core/Gms.cpp`, `src/core/Morphine.cpp`; `InstrumentKind::gms` / `morphine`; the
importer keeps the raw 0xD5 state in `ChannelDef::pluginState` and the wave folder / noise sample path of the user's
installation in `ChannelDef::samplePath`). Verification harness (our own, outside the repository: `rea-work/gmport/`,
`rea-work/morport/`, driving `build-local/gmsrender` and `build-local/morphinerender` against the models with the same
events) **[model]**:

| Engine | Cases | C++ vs model |
|---|---|---|
| GMS | 10 demo/render-test channels × {voice, full chain} at 44.1 kHz and full chain at 48 kHz, 33 synthetic cases (all of `gsyn.py` + song-position lock, all ten effect slots, flanger/panner/trancer/LP with song position) | bit-identical (difference 0) |
| Morphine | 7 demo channels × {voice 44.1 kHz, full 44.1 kHz, full 48 kHz}, 22 synthetic cases (all of `mor_t_syn.py` and `mor_t_noise.py`), host block 100 | bit-identical (difference 0) |

`tests/tests.cpp` (`gms_engine`, `morphine_engine`) checks states written from scratch against golden samples of the
models and the instruments against the reference renderers; with FL's Data folder it also imports every demo channel
on the exact engines and compares two demo channels (Song For You: GMS, SHINE: Morphine) with model goldens.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| GMS wrapper VMT `0x18014a0b0` (`FUN_1800c7d90`, `FUN_1800c82f0`, `FUN_1800c8760`, `FUN_1800c8800`, `FUN_1800c9b60`, `FUN_1800c9cd0`, `FUN_1800c9df0`, `FUN_1800c9e10`) | plugin interface, tempo/max-poly events, velocity from Init.Vol, voice handle = note | DECOMP + EXEC |
| GMS `FUN_1800c94e0`, `FUN_1800df980`, `FUN_1800c9c20`, `FUN_1800c91b0` | chunking ≤ 512, host time, host voice kill, per-note level/pitch tables | DECOMP + EXEC |
| `GMSynth` VMT `0x18014c940`; `FUN_1800ccd70`, `FUN_1800ccdb0`, `FUN_1800f5250`, `FUN_1800f53e0`, `FUN_1800f58e0`, `FUN_1800ed930`, `FUN_1800f12d0`, `FUN_1800f32e0` | notes, velocity table, allocation, start/release, polyphony, rate set, playing flag | DECOMP + EXEC |
| `FUN_1800f50c0`, `FUN_1800ef810`, `FUN_1800cd7a0` | parameter read with MIDI matrix, destination codes, raw values | DECOMP + EXEC |
| `FUN_1800f5f00`, `FUN_1800f68f0` (objdump `0x1800f6980…0x1800f7300`) | per-chunk LFO rates, effect push, control values, pitch, unison, gains, start offsets, voice end | DECOMP + EXEC |
| `FUN_1800f7530` (objdump `0x1800f7c90…0x1800f7f00`) | envelopes, glide, level, modulation buffers, oscillator loop, summation order | DECOMP + EXEC |
| `FUN_1800f1570`, `FUN_1800dfa30`, `FUN_1800ed6c0`, `FUN_1800ed8b0` | wave resampling and table synthesis | DECOMP + EXEC (`t_waves.log`) |
| `FUN_1800ef8c0`, `FUN_1800ef8e0`, `FUN_1800ef9d0`, `FUN_1800ef960`, `FUN_1800ef930`, `FUN_1800ef980`, `FUN_1800fe890` | LCG, noise, dithered read, phase offset, increments, table choice, voice reset | DECOMP + EXEC (RNG state peeked) |
| `FUN_1800efb10`, `FUN_1800ef9f0`, `FUN_1800efa80` | LFO rate, shapes, sync values, song lock | DECOMP + EXEC (`gm_sp.exe`) |
| `FUN_1800f32f0` (objdump `0x1800f3fe0…0x1800f4280`), `FUN_1800efbc0`, constructor `0x1800f0ed2…0x1800f0f7a` | ladder filter, coefficient table, effect loop and flags, master | DECOMP + EXEC |
| `FUN_1800ef900`, `FUN_1801168b0` (powf), `FUN_18012da50` (logf), `FUN_180113f90` (sin), `FUN_18012a440` (vector cos) | dB law, CRT calls whose arguments Ghidra dropped | DECOMP (objdump) |
| GMS effects: `FUN_1800fefe0`, `FUN_1800ff110`, `FUN_1800ff240`, `FUN_1800ff2c0`, `FUN_1800ff500`, `FUN_1800fea00`, asm `0x1800f60e5` | object layout, reset, dispatch, LFO, push gating | DECOMP + EXEC (`gfx.exe`) |
| `FUN_1801000b0`, `FUN_180100340`, `FUN_180100590`, `FUN_180100e50`, `FUN_1800ffc10`, `FUN_1800ff9d0`, `FUN_180101310`, `FUN_1801007e0` | the ten effect processors | DECOMP + EXEC |
| `FUN_1800feae0`, `FUN_1800fe960`, `FUN_1800fec70`, `FUN_1800fef90`, `FUN_1800fed00` | tape delay line | DECOMP + EXEC |
| `FUN_1800e4eb0`/`e4fb0`, `FUN_1800e4cc0`/`e4dc0`, `FUN_18012a3f0` + kernel `0x18012a460` | biquads, packed sinf | DECOMP + EXEC |
| `FUN_1800e5050`, `FUN_1800e50a0`, `FUN_1800e5440`; tables `0x18014d820`/`830` | output EQ | DECOMP + EXEC |
| `FUN_180100950`, `FUN_180101530`, `FUN_1801015d0`, `FUN_180101f60`, `FUN_180102170`, `FUN_180102ff0`, `FUN_180103020`, `FUN_180103f30` (`out/rev_proc.asm`); tables `0x18014a000`/`0x18014a040` | GMS reverb | DECOMP + EXEC |
| Morphine `FUN_18010f060`, `FUN_1801135b0`, `FUN_18012afa0`, `FUN_1800ec5d0`, `FUN_1800f0390`, `FUN_180102450`, `FUN_1800fc8d0`, `FUN_1800f46e0` | state layout | DECOMP + DATA |
| Morphine `FUN_180111ec0`, `FUN_18010e340`, `FUN_18010a2a0` | parameter laws / fields / names | DECOMP |
| Morphine `FUN_1800c5a70`, `FUN_1800c4f50`, `FUN_1800c4500`, `FUN_1800e8080`, `FUN_1800e80a0` | wrapper, levels, max-polyphony event | DECOMP + EXEC |
| Morphine `FUN_1800e8580`, `FUN_1800e8b70`, `FUN_1800e9430`, `FUN_1800e93a0`, `FUN_1800e9d90` | allocation, mono, glide, random | DECOMP + EXEC |
| Morphine `FUN_1801006a0`, `FUN_180100cd0`, `FUN_180100c20`, `FUN_1801001c0` | voice block structure | DECOMP + EXEC |
| Morphine `FUN_1800fb820`, `FUN_1800fb9d0`, `FUN_1800fbbc0`, `FUN_1800f7b10`, `FUN_1800f3e20`, `FUN_1800eb970` | modulation, Egs, loops | DECOMP (asm) + EXEC |
| Morphine `FUN_1800da760`/`830`/`daa00` | master ADSR | DECOMP + EXEC |
| Morphine `FUN_18012c850`, `FUN_1800eef20`, `FUN_18012d020`/`d370`, `FUN_1800f8460`, `FUN_1800f83b0` | zones, frames, partial arrays, PWM | DECOMP + EXEC |
| Morphine `FUN_1801054b0`, `FUN_180105990`, `FUN_180101b60` | morph line | DECOMP + EXEC |
| Morphine `FUN_1801220c0`, `FUN_180122ae0`, `FUN_180115860` | resynthesis bank | DECOMP + EXEC |
| Morphine `FUN_180100300`, `FUN_180100880`, `FUN_1800da140`, `FUN_180113b70`/`df0` | noise | DECOMP + EXEC |
| Morphine `FUN_1800ff630` … `FUN_1800dfe00` (§2 header list) | effects | DECOMP (asm) + EXEC |
| `r14/blobs14.pkl`, `out/mor_survey.txt`, `py/gsurvey.py` | demo states, §0 / §2.0 | DATA |
| `run/gm.exe`, `run/gm_sp.exe`, `run/gfx.exe`; `gbat.py`, `gsyn.py`, `t_waves.py`, `t_sens.py`, `t_gmsfx.py`, `mor_bat.py`, `mor_t_*.py` | §1.15–1.16, §2.13–2.14 | EXEC + model |

### Open questions

- **FL's host answers not observed from FL itself:** the max-polyphony event value at channel creation (GMS: without it no
  poly voice sounds; Morphine: all notes share voice 1); FL's mixing-time format for host Dispatcher 0x24 (beats assumed
  from the SDK constant name, **inference**); the velocity → Init.Vol law (round 28 inference).
- **GMS RNG:** process-global and shared by all GMS instances; FL's sequence (and so noise, dither and the random phases of
  fresh voices) cannot be reproduced exactly.
- **GMS:** pitch-bend events to the engine's own bend (`+0x3a10`, range P205) and the mod-wheel/aftertouch matrix rows were
  not exercised (FL delivers bends through Final.Pitch, §1.11); parameter changes during playback were not tested beyond the
  per-chunk reading; the AVX branch of the packed sinf (`0x18012ada0`) was not examined (this CPU uses the SSE path); the
  reverb's early-reflection path is unreachable from GMS; tape-delay read positions persist across resets (the model starts
  them at 0, as after construction).
- **Morphine:** release loops (generator and Eg "Release Loop", `FUN_1800ebc50`/`ebd80`, `FUN_1800f43a0`) not modelled
  (unused by the demos); MIDI CC/pressure/bend sources supported but tested only at 0; unison (hidden parameters 0x8d–0x90)
  unreachable from saved states and not verified; reverb high-pass coefficients at rates other than 44.1/48/88.2/96 kHz use
  correctly rounded math (≈ −78 dB on the reverb).
