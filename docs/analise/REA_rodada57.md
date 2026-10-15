# Behavioural analysis of FL Studio 2026: round 57 (REA) — Autogun and Transistor Bass: exact engines

> **Nature of this document.** Continuation of `REA_rodada12.md` §2 (Ogun/Autogun format) and `REA_rodada37.md` (Ogun's
> complete voice), with the same goal as rounds 37 (Ogun), 39 (GMS, Morphine) and 46 (Kepler): describe both plugins
> precisely enough for LYNX to play them on exact engines instead of the current fallbacks (Autogun: the
> spectral-oscillator conversion `synthFromOgun`; Transistor Bass: no handler, reported unsupported and played on the
> default synth patch). *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied:
> what is here are data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets
> and globals serve only to trace the evidence. No encrypted content was touched and no Image-Line content (code, audio,
> preset data) was copied into the repository: Autogun's `SG.dat` is read at run time from the user's installation.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the official
> Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's own DLL,
> unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Inferences made
> *from* the code are stated as **inference**. Results of our own implementation (which implements only DECOMP/EXEC facts)
> are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own implementation, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets** (copies in `r57/bin/`):
  - `Plugins/Fruity/Generators/Autogun/Autogun_x64.dll`, SHA-256
    `47b8a70b9b55e6a749ca26e9b9867ef34bc2b6f5866cf1e2240702b12d115e01` (Delphi, the same code base as Ogun);
    `Plugins/Fruity/Generators/Ogun/Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6`
    (= round 37) for the comparison; `Shared/SG_x64.dll` `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac`
    (= rounds 35/36/40); `Plugins/Fruity/Generators/Autogun/Data/SG.dat`
    `f67dd6a1472bdca169208b9ee8dc7a77d6315faac3e59809f83244b55d89ee53` (byte-identical to Vocodex's `SG.dat`, round 40).
  - Transistor Bass: see §2.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM` 3–4 G), projects `~/ghproj/r57_ag`, `~/ghproj/r57_og`
  (scripts `r57/gh/imp.sh`, `r57/gh/g.sh`, `r57/gh/scripts/DecompAll.java`); decompilations `r57/out/ag_plug.c`
  (Autogun `0x847000…0x890000`), `r57/out/og_plug.c` (Ogun `0x845000…0x879000`), `r57/out/ag_lib77.c`, `r57/out/ag_str.c`;
  `objdump -M intel` of both DLLs (`r57/dis/ag.asm`, `og.asm`, `sg.asm`) and their `.pdata` function tables. **Function
  matching** (our `r57/py/fmatch.py`, `fmap.py`, `fdiff.py`, `fcalls.py`, `fmatch_sg.py`): every `.pdata` function of both
  DLLs disassembled, addresses and displacements masked, hashed and aligned in address order.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r57/run/ag.c` → `ag.exe`, a copy of round 37's `og.c` (same
  safety measures: `SetErrorMode`, vectored fault handler that ends the process, the installation's DLL loaded unmodified
  from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's own
  SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Addition: it logs the file name
  of the `SG_x64.dll` module the plugin loaded. It runs either DLL (`agrun.exec_dll(…, dll=…)`). No process or dialog was
  left behind; outputs were deterministic run to run (−630 dB) apart from the Seed-2 counter (§1.6).
- **Model / reference:** for Autogun the reference is LYNX's Ogun port itself (`src/core/Ogun.cpp`, which matches our
  round-37 model `ogmodel.py` to float rounding) plus LYNX's Maximus engine (`src/core/HarmlessFx.cpp`, round 40):
  `r57/cpp/ogunrender` (built from the worktree with the new `autogun=1 sg=…` options) and `r57/cpp/mxapply` (our scratch
  tool: LYNX's `MaximusProcessor` on an `f32` file). Drivers in `r57/py/`: `agrun.py` (runs `ag.exe`), `agconv.py`
  (Autogun ⇄ Ogun state layout), `ogpatch.py` (round-37 parser copy with the two fixes of §1.2), `cmp_ag.py` (DLL vs
  LYNX, Seed-2 counter and read start j₀ from the DLL as in round 37), `bat_ag.py`, `bat_ag2.py`, `t_sg.py`, `t_sg2.py`,
  `t_ratio.py`, `t_vpk.py`, `t_kn.py`, `t_peek.py`, `gold_ag.py`. Logs in `r57/out/`.

---

## 0. Data survey **[DATA, `r57/py/survey57.py`, `r57/out/blobs57.pkl`]**

No project under `Demo projects` uses Autogun or Transistor Bass. The installation's other projects hold 9 Autogun
channels (5 distinct states), and no Transistor Bass channel at all (Transistor Bass is covered through its presets, §2):

| Project | Channel | State | Version | Notes |
|---|---|---|---|---|
| `System/Render tests/Autogun.flp` | Autogun | 2 403 B | 18 | "Wide whistle 3": Seed 2 = Random, fullness −0.288, comment of 129 characters |
| `Templates/Advanced/Electronic/` Breakbeat, Chillout-Ambient, Drum & Bass, EDM-House, Electro | Pad / Pad 5 | 43 615 B (identical) | 19 | "Harsh murmur", −24 st, unison 5, filter on, Magic Dust 430 |
| `Templates/Advanced/Electronic/Chillout.flp` | Pad 5 | 171 641 B | 19 | "Whispery splat" |
| `Templates/Advanced/Electronic/HipHop-Trap.flp` | Pad | 9 787 B | 19 | "New ageThump 3" |
| `Templates/Advanced/Electronic/Trap.flp` | Pad | 9 780 B | 18 | the same patch saved as version 18 |

All nine use Magic Dust = 430 (the default) **[DATA]**.

---

## 1. Autogun

### 1.1 Autogun is Ogun's code with three additions **[DECOMP: function matching; EXEC]**

- Of Autogun's 18 293 `.pdata` functions, 18 031 are instruction-for-instruction identical to Ogun functions once
  addresses are masked; the address-order alignment pairs the rest. Every function of the voice and timbre engine
  that round 37 documents — TriggerVoice (`0x882a90` ≡ Ogun `0x86a8e0`), voice control/render (`0x884890` ≡ `0x86c6e0`),
  Voice_Release, Voice_Kill, the quality selector, rates, control block, table oversampling, the timbre generator
  (`0x842…0x844…`), the articulator library, filter, chorus/delay/reverb/EQ set-ups, the decimator — is identical,
  including the targets of all their calls (`fcalls.py`: 31/31, 17/17, … call targets map onto each other).
- What differs: (a) the master level (§1.3), (b) a sub-plugin host and one extra stage at the end of Gen_Render (§1.4),
  (c) the state loader and the post-load parameter refresh (§1.2, §1.5), plus user-interface code (random-pad browser,
  Magic Dust knob, settings in the registry).
- **EXEC proof of the decomposition:** with Magic Dust = 0 the Autogun DLL's output equals the Ogun DLL's output on the
  same state × 0.25 **bit for bit** (residual −629 dB, `t_ratio.py`) for single notes; with Magic Dust > 0 it equals
  (0.25 × Ogun) → `SG_x64.dll`'s engine with `SG.dat` and band mix = Magic Dust (§1.4). The one place where "Ogun × 0.25"
  is not exact is the gain-ramp limit (§1.3).

### 1.2 State layout **[DECOMP `FUN_0087cb90` (SaveRestoreState), `FUN_0077cd30`; DATA; EXEC]**

Autogun's state is Ogun's (round 37 §1.3) with one addition:

- **From version 19 on, 4 bytes follow the comment**, before the first articulator: a 32-bit value stored at plugin
  `+0x610`. The constructor initialises the same field from the registry (`HKCU\Software\Image-Line\Shared\Plugins\
  Autogun`, section `Environment`, value `Seed`) and the destructor writes it back; it is the random-pad browser's seed
  and is not read by the audio code (**inference** from its only uses: constructor, destructor, loader, saver and the
  browser). Version 18 states (render test, Trap template) do not have it **[DECOMP + DATA: the parser reaches the exact
  end of 9/9 states]**.
- **Comment length** (shared reader `FUN_0077cd30`, identical in Ogun): one byte = number of UTF-16 characters; the byte
  0xFF means "a 32-bit count follows". Round 37's parser (`ogpatch.py`) and LYNX's port read a 7-bit variable-length
  integer instead, which agrees for comments shorter than 128 characters (every Ogun demo) but fails on the render-test
  state ("Wide whistle 3…", 129 characters, stored as the single byte 0x81). Corrected in both (`r57/py/ogpatch.py`,
  `parseState` in `Ogun.cpp`); Ogun's results are unchanged (no Ogun state in the corpus has ≥ 128 characters).
- The Ogun DLL cannot load a version-19 Autogun state as is (its loader reads the 4 extra bytes as the first
  articulator's version); `agconv.to_ogun` removes them for the comparisons.

### 1.3 Master level **[DECOMP `FUN_00882700` (Ogun `FUN_0086a570`), constant `0x8827b8` = 0.25; EXEC]**

Autogun computes Ogun's master gain (round 37 §1.12: `x = P0/128`, gain = 1 if x = 1, else `float((3^x − 1)/2)` in
Ogun's float/double steps) and stores `float(gain · 0.25)` (a double multiplication by 0.25, exact in float). Because the
gain is applied *before* the per-sample gain-ramp limit (0.001 per sample, round 37 §1.13), Autogun's ramps saturate less
often than Ogun's at the same settings: with the vel→env attack option and velocity 0.7 the Ogun DLL's ramp is clipped
at 0.001/sample from block 8 on while Autogun's is not (`t_vpk.py`: target gains 0.00184… vs 0.00735…, deltas
1.9·10⁻⁵ vs 7.7·10⁻⁵ … 0.001), so for such notes Autogun ≠ 0.25 × Ogun (−17 dB) — LYNX models it by scaling the master
gain, not the output, and matches the DLL (§1.7).

### 1.4 Magic Dust: the hosted Soundgoodizer **[DECOMP `FUN_00885600`, `FUN_0087edd0`, `FUN_0085bc90`, `FUN_0085c550`, `FUN_0085ca80`, `FUN_0088d930`; EXEC]**

- **Loading:** the constructor creates a small plugin host (`FUN_0085bc90`, object at plugin `+0x550`), asks it for
  `"SG.dll"` (`FUN_0085c550` appends `_x64.dll`, loads the module and calls its `CreatePlugInstance`) and, if a
  sub-plugin was created, loads `Autogun\Data\SG.dat` (path from the host's data-file query, id 1020) into it through
  `FUN_0085ca80`. EXEC: the module loaded in the harness is `C:\Program Files\Image-Line\FL Studio 2026\Shared\SG_x64.dll`
  (logged with `GetModuleFileName`). The host forwards sample rate and block size to it (dispatcher fields
  `+0x100…+0x110` of the host object, set in Autogun's dispatcher `FUN_0087a690`).
- **Parameter:** P63 ("Magic dust", 0…1000, default 430, plugin `+0x228`). The knob handler `FUN_0088d930` writes P63 and
  sets the sub-plugin's parameter 5 (the Soundgoodizer band mix) to the same value through its ProcessParam (flag 1).
  After a state load the state-to-UI refresh `FUN_0087c8b0` sets the knob (with notification) to P63, so the loaded P63
  reaches parameter 5 **[DECOMP; EXEC: P63 = 1, 100, 430, 1000 each match a Maximus run with band mix = P63, §1.7]**.
  Automation of P63 through ProcessParam updates `+0x228` only (the stage's on/off test, below), not the sub-plugin's
  parameter 5 (**inference** from `FUN_008684e0` being identical to Ogun's) — not modelled, LYNX does not automate P63.
- **Rendering:** Gen_Render (`FUN_00885600`) is Ogun's (`FUN_0086d450`) — voices, EQ, chorus, delay, reverb, the 2:1
  decimator at qualities 2–4 — followed by: if P63 > 0, every hosted sub-plugin's Eff_Render (VMT `+0x38`) in place on
  the host-rate output buffer of the block (host block length; `SG_x64.dll` itself splits blocks longer than 512 frames,
  round 40). With P63 = 0 the stage is skipped (EXEC: output = 0.25 × Ogun, −629 dB).
- **Which engine:** the shared build (`Shared/SG_x64.dll` = Maximus) loaded with `SG.dat` (a Maximus state of version 7).
  EXEC (`t_sg.py`, `t_sg2.py`): Autogun DLL vs (Ogun DLL × 0.25 → LYNX's `MaximusProcessor`, shared build, `SG.dat`, band
  mix P63, host blocks) −142.8…−146.1 dB at 44.1/48/96 kHz and blocks 256/512/1024; with the Soundgoodizer build −17…−20
  dB; with the right engine but at 44.1 kHz instead of the host rate −4 dB. The stage adds the engine's lookahead (153
  samples at 44.1 kHz); Autogun does not report it to the host (no latency answer in its dispatcher) **[DECOMP]**.

### 1.5 Post-load parameter refresh **[DECOMP `FUN_008649e0` (Ogun) vs `FUN_0087c8b0` + `FUN_0088a320` (Autogun)]**

After a state load Ogun re-applies every automatable parameter through ProcessParam (get with flag 2, set with flags
0x11): P0…P48 and all 13 articulators' 8 knob parameters. Autogun re-applies only parameters 0…88 (P0…P48 and the knob
parameters of articulators 0–4). For articulator envelopes the knob scales (`FUN_00789c70`) are also set by the loader,
and the peeked scales of all 13 articulators are equal in both DLLs after loading (`t_kn.py`) **[EXEC]**; for the LFO
knobs of articulators 5–12 (whose re-application recomputes the LFO speed) a difference is possible but **not
confirmed** — no Autogun or Ogun state in the installation has an LFO on articulators 5–12. Not modelled.

### 1.6 Seed 2 "Random" **[DECOMP constructor; EXEC]**

As in Ogun, the per-note Seed-2 counter (`+0x244`) starts from the Delphi RTL random seed when the plugin is created
(`Randomize` = clock); Autogun's constructor additionally consumes random numbers for its random-pad browser, so the
counter differs per run and between the two DLLs. Of the installation's states only the render test uses Seed 2 =
Random. LYNX keeps round 37's choice (counter 0 per instance); the comparisons read the DLL's counter back.

### 1.7 Verification against the DLL **[EXEC + model; `r57/out/bat_ag2.log`, `cmp_ag1.log`, `bat_ag.log`]**

LYNX's engine (`ogunrender autogun=1 sg=SG.dat`) against `Autogun_x64.dll` on the same events, the DLL's Seed-2 counter
and read starts j₀ fed in (round 37 §1.21); three overlapping notes (C3 1.0, G3 0.7, C4 0.9, 2 s), worst channel:

| State | 44.1 kHz, state's quality | 44.1 kHz, quality 4 | 48 kHz, state's quality | 48 kHz, quality 4 |
|---|---|---|---|---|
| Templates "Pad" (Breakbeat & 4 others) | −128.9 dB | −127.2 dB | −128.7 dB | −127.3 dB |
| Chillout "Pad 5" | −127.7 dB | −128.8 dB | −128.0 dB | −129.4 dB |
| HipHop-Trap "Pad" (v19) | −128.2 dB | −128.3 dB | −128.1 dB | −128.9 dB |
| Trap "Pad" (v18) | −128.2 dB | −128.3 dB | −128.1 dB | −128.9 dB |
| Render test "Wide whistle 3" (Seed 2 Random, fullness −0.288) | −71.5 dB | −71.5 dB | −70.7 dB | −71.1 dB |

Synthetic variants of the Trap pad (44.1 kHz): Magic Dust 0 / 1 / 100 / 1000 → −126.5 / −127.9 / −128.2 / −128.0 dB;
master level 64 and 200 (with Magic Dust 700) → −127.8 / −128.1 dB; quality 0 / 2 / 3 → −128.4 / −128.6 / −128.7 dB (`bat_ag3.log`). Without the
counter and j₀ fed in, the four fixed-spectrum states are unchanged (−127…−131 dB, `cmp_ag1.log`, `gold_ag.txt`) since
they do not use Seed 2 Random and their read starts agree.

The render-test state's residual is Ogun's own precision limit, not Autogun's: the Ogun DLL against our round-37 model on
the same state (converted, `t_ogm.py`) gives −76 dB too — fullness ≠ 0 with IPP's `Exp_32f_A11` (round 37 §1.20).

### 1.8 LYNX port **[model]**

- `InstrumentKind::autogun` (appended to the enum), `makeAutogunInstrument()` = the Ogun instrument in its Autogun mode:
  state parsed with the §1.2 layout, master × 0.25 inside the voice gain (§1.3), then the Magic Dust stage — LYNX's
  `hlfx::MaximusProcessor` (shared build) with `SG.dat` read at run time from the path in `ChannelDef::samplePath`
  (`%FLStudioFactoryData%\Plugins\Fruity\Generators\Autogun\Data\SG.dat`, resolved by the importer), band mix = P63, run
  in place on each host block when P63 > 0. Without the file the stage is skipped (logged as a fallback in the reports).
  While the instrument is idle the stage keeps processing silence so its envelopes relax as in FL.
- The importer plays every readable Autogun state (9/9) on it; the spectral-oscillator conversion stays only as the
  fallback for unreadable states. The channel settings panel shows Ogun's knobs plus "Magic dust".
- `ogun::renderReference` / `tools/ogunrender` take `autogun` and the `SG.dat` bytes; core tests:
  `autogun_template_channels` (DLL goldens for the four fixed-spectrum states at the state's quality and quality 4; the
  render-test state as an engine fingerprint; the instrument against the reference renderer; idle after the tail) and
  the import checks (9 channels usable, imported as Autogun, `SG.dat` found, sounding).
- Not modelled (as above): automation of P63 (sub-plugin parameter), the possible LFO-knob refresh difference of §1.5,
  FL's Seed-2 counter, and the Ogun limits of round 37 §1.20.

---

## 2. Transistor Bass — the complete engine as a portable model

> Same rules as the rest of this document: clean-room, behaviour only, markers **[DECOMP]**, **[DATA]**, **[EXEC]**,
> **[DOC]**, **[model]**, **not confirmed**. Addresses serve only to trace the evidence. No Image-Line code, audio or wave
> data was copied into the repository: the recorded oscillator cycles are read at run time from the user's own DLL.

- **Targets** (copies in `r57/bin/`):
  - `Plugins/Fruity/Generators/Transistor Bass/engine_x64.dll`, SHA-256
    `4cc72470063a0a9f8f126102415cbb1ef2b91abb2995cd9fbdf2c8e676c6907f` (C++/MSVC, framework `d1framework` as Kepler's,
    RTTI `FLWrapperPlug`, `MyPlugin`, `TB303Engine`, `TB303Engine1`, `TB303Engine2`, `MyArpeggiator`, `ILDelay3`,
    `fxReverb`, `ParameterSmoother`; PDB path `...Transistor_Bass_Engine\...\TB303.pdb`), image base 0x180000000.
  - `Plugins/Fruity/Generators/Transistor Bass/Transistor Bass_x64.dll` (Delphi front-end): only its state chunks and
    forwarders were read (`FUN_006de3a0`; Gen_Render etc. forward through the engine object at wrapper `+0x128`).
- **Tools:** Ghidra 12.1.4 headless (projects `~/ghproj/r57_te`, `~/ghproj/r57_tb`), full decompilation
  `r57/out/te_all.c` split per function into `r57/tb/fn/`, annotated disassembly (`r57/py/fasm.py`, constants resolved)
  in `r57/tb/asm/`; every float operation order of the port was taken from the disassembly. CRT routines identified by
  calling them in the DLL (`r57/run/callf.exe`): `0x1800b9dd0` pow, `0x1800bb470` powf, `0x1800b9140` expf,
  `0x1800bc8b0` sinf, `0x1800bd1a0` tan, `0x1800bd940` tanf, `0x1800b8c40` cosf, `0x1800b86c0` cos, `0x1800bc340` sin,
  `0x1800be0d8` tanh, `0x1800b94a0` log, `0x1800b99d0` logf **[EXEC]**.
- **EXEC harness:** `r57/run/tb.c` (copy of round 46 `kp.c`; same safety measures; loads the installation's wrapper DLL
  unmodified, which loads `engine_x64.dll`); `on4=` events add Final.FCut/FRes and the host's colour/MIDI-channel
  answer (Voice_ProcessEvent 2); `pef=0,bpm` sends the tempo; peeks `arr=-1,…,128>250>OFF` read MyPlugin fields,
  `128>250>3c470>OFF` the engine. Drivers: `r57/py/tbrun.py`, `tbcmp.py` (DLL vs LYNX), `bat_tb.py` (all 128 programs),
  `t_seqoff.py` (synthetic battery), `gold_tb.py` (test fingerprints), `tbstate.py` (state parser and v5/engine/patch
  writer). Reference implementation: the C++ port itself (`src/core/TransistorBass.cpp`, tool `tools/tbassrender.cpp`).

### 2.1 Data survey **[DATA]**

No `.flp` of the installation contains a Transistor Bass channel. States: the three presets of
`Patches/Plugin presets/Generators/Transistor Bass` (`Empty`, `Factory programs`, `Sequencer off`) and the plugin
database entry (= `Factory programs`); all are wrapper version 1, engine-stream version 3 (→ engine "V1 legacy").
`Factory programs`: 96 named patches + sequences (program *k* = patch *k* + sequence *k*, transpose 0), sequencer on,
"AF" on; reverb on in 74, distortion in 67, delay in 59, "303 Pulse" in none. A fresh instance saved by the harness
is wrapper version 2, stream version 5, engine 1 ("V2 extended"), sequencer off **[EXEC]**.

### 2.2 State **[DECOMP `FUN_006de3a0`, `FUN_18000ce40`, `FUN_18000a070`, `FUN_180009af0` + DATA]**

Wrapper: u32 version (1 or 2; > 2 refused), then chunks `{u32 id, u64 size, bytes}`: id 1 = 8 bytes of editor state,
id 2 = the engine stream `{u32 0, u64 size, data}`. Engine data (version *v*, little-endian):
i16 v; i32 current program; i32 (UI); u8 AF; 128 × program {i16, string, i32 sequence, i32 patch, i32 transpose};
128 × patch pair {string, 100 × f32} × 2 (stored copy, **working copy** — the second is what plays); 128 × sequence
{i16 ver, string, i16, i32 length, 32 × step {i16 sv, u8 slide, i32 note, u8 gate, f64 (unused), f32 velocity,
[u8 accent if sv = 1]}, f64 internal BPM, u8 internal-BPM flag, f64 shuffle, [f64 gate length if ver > 0, else 0.66]};
i32 sequencer on (stored 0/4); v ≥ 2: i32 sequence transpose, i32, f32, f32 (the last, the MIDI volume, is read and
discarded); v ≥ 3: i32 oversampling (4); v ≥ 5: i32 engine (0 V1 legacy, 1 V2 extended cutoff, 2 V2 TB303 cutoff;
absent → 0). Strings are i32 length + bytes. Parameters 0…41 of the current program's working patch: tuning, waveform,
cutoff, key follow, resonance, env mod, decay, accent, volume, HP, min decay, PW, LFO rate, LFO→PW, reverb on, reverb
low cut, high cut, predelay, predelay feedback, size, diffusion, decay, high damping, width, dry, ER, wet, distortion on,
dist HP, drive, tone, dist volume, delay on, BPM sync, amount, time, right tap, feedback, tone, stereo, VCA smooth,
303 Pulse (names: `FUN_18000e9c0`).

### 2.3 Installation data read at run time **[DECOMP `FUN_180012070`, `FUN_1800124e0` + EXEC]**

Two sets of 16-bit mono RIFF/WAVE single cycles embedded in the engine DLL: `0x1800FEAF0` (count 57, 192 kHz, used by
V1) and `0x18015B2F0` (count 55, 44.1 kHz, V2), each `{i32 size, RIFF bytes}`. Each is converted to float x/32768,
then `(float)(x − 0.63)·6.3157892` (f32), the mean is taken **in double** and subtracted (result rounded to float), and
a guard point `t[n] = t[0]` is appended. LYNX's decoded tables equal the DLL's in memory bit for bit **[EXEC]**.

### 2.4 Band-limited saw tables **[DECOMP `FUN_180001250`, `FUN_180012d70`, `FUN_180012890` + EXEC]**

Computed at the first instance (no data): a 1024-point falling ramp `1 − i/512`; a 64-tap kernel at 16 384 phases,
`k[p][j] = sinc(π·max(−32, j − 32 − p/16384)/2^¼)·w²/2^¼` with `w = ½ − ½cos(2π·max(0, j − p/16384)/64)`; 48 levels,
each `n_k = max(2, round(n_{k−1}/2^¼))` points resampled from the previous level (tap sum in double, taps
`src[(i − 32 + j) mod n]`); every level is then upsampled ×16 with the same kernel; one guard point before, two after.
The level-0 table equals the DLL's in memory (16 387 points) **[EXEC]**.

### 2.5 Engine "V1 legacy" (`TB303Engine1`: process `FUN_1800140c0`, parameters `FUN_180013b40`, rate `FUN_180013760`) **[DECOMP + EXEC]**

Per host sample (render with MXCSR |= 0x8040, round to nearest): LFO `sinf(2π·φ)`, φ += (9.9·rate + 0.1)/fs; pitch
glide (coefficients `1 − 0.1^(1/(fs·t))`, t = 0.1 ms normal, 50 ms slide) → note + bend − 64 + 24 + 4 + 24·tuning +
expression + sequence transpose, clamped to 0…144 and normalised; decay envelope ×`0.1^(1/(fs·d))` per sample
(d = accent ? min-decay : decay law `p6²·(2.16 − m) + m`, m = 0.185·(1 + p10)); accent `acc²·env·r` with r =
1 − (1 − res)/2, a smoothed accent bump (rise 50 ms / fall 200 ms); PW = 0.5 + 0.45·p11 + LFO·0.24·p13 clamped to
0.001…0.99900001; oscillator table level from the pitch (48 levels), phase increment `powf(2, 12x)·8.1758/fs₄`;
cutoff `2^(128/12·((1 − 0.31·em²)·(0.58·c^1.5 + 0.1) + (0.16 + 0.54·em²)·env + 0.23·bump − 0.02 + (kf − 0.5)·x + X))·
8.1758` Hz into the diode ladder (`FUN_180011db0`: polynomial g and k in ωc = f/(√2·fs₄), feedback through a 160 Hz
one-pole high-pass, output gain ((k/17 − 1)·r + 1)(r + 1)·2, resonance clamped Y + res); then **4× per sample**:
saw − shifted saw (pulse) blended to saw by "waveform" (or, with 303 Pulse, the recorded cycle at its own phase blended
with the saw), ladder, VCA (gate on: target max(accent shape `((2.6 − 0.74)·acc + 0.74)·env + 0.74`, a 1 s ×0.1
sustain curve), attack/release 5…50 ms by "VCA smooth"), one-pole high-pass 10…400 Hz ("HP"), volume ×0.75·p8,
distortion (§2.7); two 6-coefficient half-band allpass decimators (4→2→1).

### 2.6 Engine "V2" (`TB303Engine2`: process `FUN_180016210`, parameters `FUN_180015b00`, rate `FUN_1800154f0`) **[DECOMP + EXEC]**

Same oscillators, ladder (input negated), high-pass, distortion, decimators. Different control: cutoff/resonance/env-mod
laws from 5-point tables (TB303 range) or linear (×0.7, "extended"); decay coefficients precomputed from an 11-point
decay table scaled by the min-decay ratio; an accent circuit (two RC paths, time constants from resonance and accent,
`FUN_180014990`) adds to the cutoff; the VCA follows max(env A, 3·acc·env B, min(1, 1.75·acc·env C) when the gate is off)
through a 0.3 ms / 10 ms follower and a "VCA smooth" low-pass; three attack/decay/release envelopes (`FUN_180014d00`).
Quirk reproduced: the sample-rate call recomputes both decay coefficients for decay = 0, so after a rate change the
decay knob has no effect until parameter 6 or 10 is set again (a program change in sequencer mode does) **[DECOMP + EXEC]**.

### 2.7 Distortion (`FUN_180001b80`, set-up `FUN_1800018d0`, `FUN_1800019d0`) **[DECOMP + EXEC]**

At 4×: gain `1 + 2305·d³` (d = drive²), high-passes 7.2 Hz, 60 Hz and "HP" (10 + 3000·v² Hz), a low-pass from the drive
taper (16 000, 5000, 2500, 1500, 900, 600 Hz), slew limit 300 000/fs₄ per sample, clamp ±4.5, tanh, "Tone" low-pass
(475 + 19 000·v² Hz), output `(1.004104 − 0.981905·(1 − e^(−11.83419·d)))·4^(2v−1)·(1 + HP)`.

### 2.8 Delay (`ILDelay3`, per sample `FUN_180008a20`, MyPlugin glue in `FUN_18000d780`) **[DECOMP + EXEC]**

One 2^18-point line, mono send ((L + R)/2·amount), feedback through a 10 Hz high-pass (coefficients at 44.1 kHz
regardless of the rate), a 2×-iterated state-variable **low-pass** "Tone" 270…22 000 Hz (filter mode 0, set by the
plugin's constructor), a soft clipper ×0.1 → ×10.0296 with a level follower, a 65-point line, left/right taps read as
crossfades of two tap sets (time changes, checked at each block start, fade over 0.3 s; first use starts a fade from
right-tap 0), 20 Hz output high-passes and the stereo matrix (0.5 + 0.5·stereo). Time: free `0.01 + 0.49·v` s or synced
`4·T[round(17v)]` beats at the current tempo (sequence BPM when "internal BPM").

### 2.9 Reverb (`fxReverb`: process `FUN_180007100`, coefficients `FUN_180005420`, controls `FUN_180004800`) **[DECOMP + EXEC]**

The shared plate's line proportions and taps scaled by ((size + 15)·fs/115)/20000, four input diffusers, predelay with
feedback, figure-eight tank with two-band decay (crossover shelf), damping and all-passes, early reflections (8 taps per
side; only the first pair gets a weight), wet smoothing (0.001/sample), width matrix, dry/ER/wet in dB laws; all float.
Buffers are zeroed at the rate call, filled with a 1e-30 constant by the plugin's reset (patch change), cleared when the
room size changes.

### 2.10 Plugin, notes and sequencer (`FLWrapperPlug` + `MyPlugin` + `MyArpeggiator`) **[DECOMP + EXEC]**

Wrapper: MIDI velocity `trunc(clamp(127·ln(10·Init.Vol + 1)/ln 21))`; a note on a key already held first releases it;
per-note expressions at each Gen_Render when changed: X = Final.FCut (cutoff), Y = Final.FRes (V1 resonance), pitch =
(Final.Pitch − 100·(key − 60))/100 semitones, pan `p = (Final.Pan + 1)/2` → gains √(1 − p), √p (1, 1 before the first
note). **Final.Vol is not read** (as Kepler). Channel 12 (MIDI channel 13) notes set the sequence transpose.
Sequencer off: notes play the engine directly; a note while another is held slides (legato); velocity ≥ 110 is an accent.
Sequencer on: a note selects program = key (patch re-applied; a different patch resets voice, delay and reverb) and
starts the sequence (16th notes, tempo/15 steps per second, host tempo default 125; shuffle; per step gate, slide (holds
the note into the next step), accent, velocity; gate length 0.66 step; note = step note + program transpose); releasing
the key stops it. Block render: engine sample × pan gains, delay, MIDI volume (1), then reverb on the block.

The manual describes the same split **[DOC, `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Transistor%20Bass.htm`]**:
with the sequencer on, input notes trigger the sequence linked to the current program (notes on MIDI channel 13
transpose it) and the sequencer follows FL's tempo unless "Internal BPM" is selected; with it off, the piano roll plays
the synth directly; overlapping notes slide.

### 2.11 Verification **[EXEC + model]**

DLL vs LYNX (same state, notes, 512-sample host blocks):
| Set | Cases | Bit-exact | Median | Worst |
|---|---|---|---|---|
| All 128 programs of `Factory programs` (sequencer, FX), V1, 44.1 kHz | 128 | 55 | −131.4 dB | −110.1 dB |
| same, engine V2 extended | 128 | 55 | −131.5 dB | −112.2 dB |
| same, engine V2 TB303 | 128 | 55 | −131.7 dB | −108.6 dB |
| same, V1 at 48 kHz | 128 | 57 | −132.1 dB | −111.1 dB |
| synthetic sequencer-off (accents, legato, retrigger, pan/pitch/X/Y, fast notes) × 3 engines × 303 Pulse × FX | 84 | 24 | −134.5 dB | −113.3 dB |
| sequencer extras (channel-13 transpose, program switch, same key, expressions, `Empty`, tempo 97.5) | 10 | 2 | | −121.9 dB |
| block sizes 256/1000, 96 kHz | 9 | 3 | | −117.9 dB |

The residual: the only differing values found in per-sample traces are the CRT's pow/powf results (single ulp; the
DLL's powf(2, x) differs from the correctly rounded value on 18 of 200 000 inputs, pow(0.1, x) on 19 of 100 000)
**[EXEC]**; LYNX uses exp2 / extended pow. The Linux (gcc, clang) and Windows (zig) builds of the port render
identically. Core test `transistorbass_engine`: 10 DLL fingerprints (energies + samples, tolerance 1e-5).

### 2.12 LYNX port **[model]**

- `InstrumentKind::transistorbass`, `makeTransistorBassInstrument()` (`src/core/TransistorBass.{h,cpp}`); the importer
  maps the plugin name "Transistor Bass" to it when the state is readable and the user's
  `Plugins\Fruity\Generators\Transistor Bass\engine_x64.dll` is the analysed build (SHA-256 checked; its path goes to
  `ChannelDef::samplePath`, the recorded cycles of §2.3 are read from it at run time); otherwise the channel stays
  unsupported, as before. The instrument mirrors Kepler's host interface: LYNX velocity → Init.Vol → MIDI velocity
  (§2.10), note and channel pan through the plugin's pan law, the song tempo, the channel volume as an output gain.
- `tbass::renderReference` / `tools/tbassrender.cpp` drive the engine like the EXEC harness; core test
  `transistorbass_engine` (state checks, 10 DLL fingerprints, the instrument plays and goes idle).

### 2.13 Open questions

- Order of FL's sample-rate call and state load (matters for V2's decay quirk §2.6) — **not confirmed**; LYNX follows
  the harness (state, then rate).
- How FL applies the channel volume to this plugin (Final.Vol unread; MIDI volume event not observed) — **not confirmed**;
  LYNX applies its channel volume as an output gain.
- Parameter automation (the plugin's 16-sample parameter smoother), MIDI CC/pitch-bend events, the AF editor flag:
  not modelled. The f64 per-step field and the second i32 after the sequence transpose: role not confirmed (unused by
  the audio path).

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `.pdata` of both DLLs, `fmatch.py`/`fmap.py`/`fdiff.py`/`fcalls.py` | 18 031/18 293 functions identical; voice/timbre/effects code and call targets identical | DECOMP |
| `FUN_0087cb90`, `FUN_0077cd30` | v19 4-byte seed after the comment; comment length byte / 0xFF + i32 | DECOMP + DATA (9/9 states to the exact end) |
| `FUN_00882700`, `0x8827b8` | master × 0.25 | DECOMP + EXEC |
| `FUN_00885600` vs `FUN_0086d450` | Gen_Render = Ogun's + hosted sub-plugins when P63 > 0 | DECOMP + EXEC |
| `FUN_0087edd0`, `FUN_0085bc90`, `FUN_0085c550`, `FUN_0085ca80`, `FUN_0088d930`, `FUN_0087c8b0` | SG host, `SG_x64.dll` + `SG.dat`, P63 → parameter 5 | DECOMP + EXEC (module path logged) |
| `FUN_008649e0` vs `FUN_0088a320` | post-load refresh of 153 vs 89 parameters | DECOMP; EXEC (knob scales equal) |
| `t_ratio.py`, `t_sg.py`, `t_sg2.py`, `t_vpk.py`, `bat_ag.py` | decomposition 0.25 × Ogun → SG; rate/block checks; ramp limit | EXEC |
| `cmp_ag.py`, `bat_ag2.py`, `gold_ag.py` | LYNX vs DLL | EXEC + model |
| Transistor Bass: `FUN_006de3a0` (wrapper), `FUN_18000ce40`, `FUN_18000a070`, `FUN_180009af0` | state chunks and engine stream | DECOMP + DATA |
| `FUN_180012070`, `FUN_1800124e0`; `FUN_180001250`, `FUN_180012d70`, `FUN_180012890` | recorded cycles; band-limited tables | DECOMP + EXEC (tables equal in memory) |
| `FUN_1800140c0`, `FUN_180013b40`, `FUN_180013760`, `FUN_180011db0` | engine V1, ladder | DECOMP + EXEC |
| `FUN_180016210`, `FUN_180015b00`, `FUN_1800154f0`, `FUN_180014990`, `FUN_180014d00` | engine V2, accent circuit, envelopes | DECOMP + EXEC |
| `FUN_180001b80`, `FUN_180008a20`, `FUN_18000d780`, `FUN_180007100`, `FUN_180005420`, `FUN_180004800` | distortion, delay, reverb | DECOMP + EXEC |
| `r57/run/tb.c`, `tbcmp.py`, `bat_tb.py`, `t_seqoff.py`, `gold_tb.py` | LYNX vs DLL batteries (§2.11) | EXEC + model |
