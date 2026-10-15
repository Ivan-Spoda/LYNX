# Behavioural analysis of FL Studio 2026: round 43 (REA) — Fruit Kick and BassDrum: complete engines as portable models

> **Nature of this document.** Continuation of `REA_rodada10.md` §4 (Fruit Kick layout and parameters),
> `REA_rodada15.md` §12.3 and `REA_rodada17.md` §7 / `REA_rodada18.md` §9 (BassDrum order and scales), with the same goal
> as rounds 37 (Ogun) and 39 (GMS/Morphine): describe both engines precisely enough for LYNX to port them as exact
> instruments instead of the approximations `synthFromFruitKick` (`src/core/FlSynths.cpp`) and `synthFromBassDrum`
> (`src/core/FlGenerators.cpp`). *Clean-room* reverse engineering, behavioural only: data layouts, formulas and
> behaviours described in our own words; no Image-Line code, pseudocode, audio or table data was copied into the
> repository. Addresses (`FUN_xxxxxxxx`) only trace the evidence. No encrypted content (FLEX) was touched.
>
> **Rule (user request): no speculation.** Markers as in round 39:

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line documentation |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | obtained by running the installation's own DLL, unmodified |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r43/`.
- **Targets** (copies in `bin/`):
  - `Plugins/Fruity/Generators/Fruit Kick/Fruit Kick_x64.dll`, SHA-256 `a93d8645…874122b` (= round 10; Delphi, image base
    0x400000, plugin VMT `0x72cff8`).
  - `Plugins/Fruity/Generators/BassDrum/engine_x64.dll`, SHA-256 `2fb1b2f8…12c25e8b` (= rounds 15/17/18; C++/MSVC, RTTI
    `CFLWrapper`, `CSynthesizer`, `BassDrumSynth`, `BassDrumGen`, `CWaveSample`), and `BassDrum_x64.dll` (`1d6abcfc…`), which
    renders identically in the harness **[EXEC]**; data `BassDrum/sounddata` (`7e02b6ef…`, a 1024-sample float WAV) and
    `BassDrum/Noise Samples/*.wav` (16-bit mono), read at run time.
  - Host side: `FLEngine_x64.dll` (22 MB, Delphi, base 0x400000) for the host wave tables, the host waveshaper and the
    host voice defaults.
- **Tools:** Ghidra 12.1.4 headless (projects in `~/ghproj-r43`, a path without dots: Ghidra refuses `.claude`),
  `gh/scripts/DecRange.java`, `gh/scripts/Vft.java` (RTTI vtables); full dumps `out/fk_72c_730.c`, `out/fk_71a_722.c`,
  `out/eng_all.c`; `objdump -M intel` (`out/fk.asm`, `out/eng.asm`, `out/fle.asm`) wherever float order mattered or
  Ghidra dropped CRT arguments; `py/rd.py` (constants), `py/fn.py`, `py/dis.sh`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `run/kd.exe` (copy of round 39 `gm.c`, same safety measures:
  `SetErrorMode`, vectored fault handler ending the process, DLL loaded unmodified, `TerminateProcess`, `timeout`).
  Additions: host `WaveTables[0]` at host+0x18 (the 16384-point sine, built like FL, §1.3), host slot 24 (vt+0xC0) = FL's
  waveshaper with the host's own curve table (§1.4), `tick=<n>` calls the Delphi plugin's per-tick slot 14, the host
  answers a plugin's Voice_Kill request by killing the voice after the render call (as FL does), dispatcher 0xE
  ("convert path") answers with the install's file of the same name (`KD_NOISEDIR`), ComputeLRVol in x87 extended
  precision like FL's host (`FLEngine 0x6a3990`). `run/fldump.exe` maps `FLEngine_x64.dll` without running its DllMain
  (`DONT_RESOLVE_DLL_REFERENCES`) and calls two pure table builders to dump the host tables; `run/pw.exe` calls the
  BassDrum engine's own CRT `powf`/`pow` on test inputs.
- **Models** (`python3 -I`, numpy): `py/fkmodel.py` (Fruit Kick + its voice framework), `py/bdmodel.py` (BassDrum
  wrapper + engine + CRT powf/sincosf ports). Drivers: `py/fkbat.py`, `py/fktick.py`, `py/fkcpp.py`, `py/bdbat.py`,
  `py/bdcmp.py`, `py/bdcpp.py`, `py/t_tables.py`, `py/goldens.py`. States: `st/` (`py/states.py`: 3 + 5 demo/template
  channels, 8 Fruit Kick and 52 BassDrum factory presets; 4 synthetic Fruit Kick snapshot states `st/syn_*.bin`).

---

## 0. Data survey **[DATA, `r41/py/survey41.py`, `py/states.py`, `py/chev.py`]**

| Plugin | Channels in `.flp` | Presets | Notes |
|---|---|---|---|
| Fruit Kick | 3 (Africa's Calling v1 with snapshot, Treben "GHOSTKICK" v0, render test v0) | 8 `.fst` (all v0) | GHOSTKICK's channel has its volume envelope on (decay 10032, sustain 0, release 1380, decay tension 128); Africa's Calling's snapshot has it on while its channel record has it off (§1.6) |
| BassDrum | 5 (Atthar ×2, Oxygen, render test, EDM-House template) | 52 `.fst` (tag `2kiK`) | noise paths: Mac absolute, old Windows absolute, `%FLStudioFactoryData%/…` |

Every channel filter (0xDB) is neutral (Mod X 256, Mod Y 0, type 0).

---

## 1. Fruit Kick

### 1.1 Structure **[DECOMP]**

Delphi plugin built on Image-Line's generic voice framework (the same one DX10 and 3x Osc carry, round 42/round 4):
VMT `0x72cff8`: 1 Dispatcher `FUN_0072d160`, 3 SaveRestoreState `FUN_0072d690`, 5 ProcessEvent `FUN_0072dc60`, 6
ProcessParam `FUN_0072dce0`, 8 Gen_Render `FUN_0072e750`, 9 TriggerVoice `FUN_0072e2f0`, 10 Voice_Release
`FUN_0072e4b0`, 11 Voice_Kill `FUN_0072e4d0`, **13 Voice_Render `FUN_0072e520`** (called by the framework), 14 per-tick
`FUN_0072e840`. Parameters P0…P5 at plugin `+0x120…+0x134` (Max freq, Min freq, Freq decay, Amp decay, Click,
Distortion; ranges in round 10 §4.2). State layout as round 10 §4.1, with one correction: in version 1 the 364-byte
snapshot starts at **byte 32** (after the version, the six parameters and 4 unused bytes), not 28.

### 1.2 Derived values (ProcessParam with flag 1, re-run for all parameters after a state load and on a rate change)

- `e4 = (float)(2247346493527.166 / fs)` (= 2³²·523.25 Hz/fs); `r = (float)(44100/fs)` **[DECOMP `FUN_0072d160` case 4]**.
- Phase increment of `c` cents: `Round(e4·exp(c·ln2/1200))` (`cvtsd2si`, round-half-even) **[DECOMP `FUN_0071b2c0`]**.
- Floor increment `= inc(P1 − 3600)` (integer) **[DECOMP `FUN_0072dcb0`]**.
- Freq decay coefficient: `e = exp((double)(1 − P2·0.00390625f)·ln 1001)`; `c = 0.9997f − (float)(e − 1)·9.7e−6f` (float
  ops); `fdec = Power(c, r)` **[objdump `0x72de40…`]**. Amp decay: `adec = Power((float)(0.99998 − (double)(float)(e − 1)·4.98e−6), r)`
  with `e` from P3. `Power(single, single)` (`FUN_005ea930`): 1 for y = 0, 0 for x = 0 < y, IntPower when y is an
  integer (at 44.1 kHz y = 1: the coefficient itself), else `(float)exp(y·ln x)`.
- Distortion: `d = P5·0.0078125f`, dry `= 1 − d`, wet `= (float)(d·3.3)`.

### 1.3 Voice **[DECOMP `FUN_0072e2f0`, `FUN_0072e520`; EXEC]**

- Start: phase `= P4 << 24` (32-bit); start cents `c = (P0 + Final.Pitch·0.5f) + 1200` in float (**the note tracks at
  half rate**: Final.Pitch = 100·(key − 60) + offset); clamped to ≥ `P1 − 3600` and ≤ 4800; increment `= (float)inc(c)`;
  amplitude 1.5.
- Per sample: `out = SINE[phase >> 18]·amp`; `phase += Round(inc)`; `amp ·= adec`; `inc ·= fdec` while
  `(float)floor < inc`, else `inc = floor`. SINE is **FL's host sine table** (host `WaveTables[0]`, host+0x18): 16384
  floats `(float)sin(2i·π/16384)` computed in double by FL's start-up code (`FLEngine FUN_009cdfc0`); dumped with
  `fldump.exe` and equal bit for bit to that formula **[DECOMP + EXEC, `py/t_tables.py`]**.
- The voice reports "finished" when `amp < 0.0002` (integer compare of the float bits).
- Distortion (P5 > 0): the mono block goes through the **host's waveshaper** (host slot 24, `FLEngine 0x132adc0`) with
  set 0, curve 10, dry/wet above and drive 0.1. The host routine (`0x6a5aa0`, SSE): `t = x·(0.1·8191)`, `j = trunc(t)`,
  linear interpolation of the int16 curve at `|j|` (clamped to 8191, odd symmetry), `out = x·dry + y·(wet/32768)`. When a
  host flag is clear (`0x16baff0`, set around what look like FL's file-render routines — inference) FL calls a
  non-interpolated variant (`0x6a5c20`); LYNX uses the interpolated one. The curve (`FUN_00b96cc0`, 2 sets × 10 curves ×
  8194 int16): set 0, curve r (1…10): for x = k/8191, `d = 20·log10 x` (−10000 for 0); if `d ≥ −25`: `d = (d + 25)·0.1·(11 − r) − 25`; then `d += 1.6·(r − 1)` if `d ≤ −1.6·(r − 1)`; value `= round(32767·10^(d/20))`, element 8192 repeats 8191 (set 1 uses `−(−d)^(0.8 + 0.02·(10 − r))` instead). Regenerated with this
  formula, all four checked curves of set 0 and three of set 1 equal FL's dump **[DECOMP + EXEC]**.

### 1.4 Plugin voice framework: levels, ramps, mixing **[DECOMP `FUN_007212b0`, `0x5f8a70`, `0x5f8c40`, `0x5fa150`, `0x5fa200`; EXEC]**

- Per block and voice: `L, R = Vol·cos((1 + pan)·π/4), Vol·sin(…)` in x87 extended precision (the framework's own copy of
  FL's law A, mode read at `0x795478` = 0), with `pan = Final.Pan + pan-LFO`, `Vol = Final.Vol × volume factor`.
- Ramp: on the voice's first block the gains are set directly (no ramp) when its volume envelope does not start at 0;
  later, a change ramps at most 0.001 per sample (target clipped to `prev + step·n`, snapped to 0 below 5.96e−8).
- Mixing into the accumulator: `acc += x·gain`; with a ramp the SSE mixer steps the gains by `2·step` every two frames
  (lanes `L, R, L + dL, R + dR`) and per frame in the `n mod 8` tail. Gen_Render copies the accumulator to the output
  (IPP `ippsCopy_32f`); with no voice it writes nothing.
- Velocity: TriggerVoice sends no "link velocity" event, and FL's host voice reset sets that flag on
  (`FLEngine 0xd23687`), so **Final.Vol = Init.Vol × channel volume** **[DECOMP]**.

### 1.5 Release and the end fade **[DECOMP `FUN_007219e0`, `FUN_00721a60`, `FUN_0071e740`, `FUN_0071def0`; EXEC]**

On Voice_Release, envelope targets 1–4 enter their release stage; if the **volume envelope is disabled**, a 10 ms end
fade starts at the next block: `t[i] = 0.5·(1 + cos((i + 1)·π/(N + 1)))`, `N = Round(0.01·fs)` (441 at 44.1 kHz),
multiplied into the stereo block, zeros after it, voice finished at the end. The same fade starts when the volume
envelope reaches its end stage (9). FL builds the table with IPP's 11-bit cosine (`ippsCos_32f_A11`, dumped table: up to
≈10⁻⁵ from the exact cosine), so the fade is the one place not bit-exact (§1.9). Max polyphony (`+0xe0`, ProcessEvent 1)
starts the same fade on the first voice not yet released.

### 1.6 Channel envelopes, LFOs, ticks **[DECOMP `FUN_0071fd90`, `FUN_0071f840`, `FUN_00720150`, `FUN_00720070`, `FUN_00721850`; EXEC]**

The framework keeps the five 68-byte channel records (pan, volume, cutoff, resonance, pitch; the 0xDA layout of round 3
§1.2) plus Mod X/Y and filter type: from the state's snapshot if present, else FL hands the channel's own records
(dispatcher 0x1B/0x28 → host 0x5A when no snapshot was loaded; that the host copies the channel records is an inference).
Times: `ticks(v) = 0` for v < 101, else `Round((double)(float)(1001^((float)(v/65536)) − 1)·E)` with
`E = (PPQ/4)/48·24576/1000` (synced) or `E·tempo/120` (default tempo 140, PPQ 96 until the host says otherwise). Tension:
`a = sign(t)·(float)(6347^|t/128| − 1)·0.1f`, `C = 1/|a|`, `K = ln(|a| + 1)`. Stage machine exactly as round 3 §1.5
(delay, attack, hold, decay to sustain with the inverse curve, release from the current level, end below 5.96e−8); LFO
32-bit phase stepped by `2³²/max(ticks, 3)`, plugin's own sine/triangle/square tables, delay and fade-in. Everything
advances **once per tick**: the host's per-tick callback sets a flag, and the next Gen_Render steps every voice (a new
voice also steps once on its first block). Volume factor `= y_vol·(lfo_vol·amount + 1)` (≥ 0); pan `= 2·lfo_pan·amount`.
Pitch/cutoff/resonance targets are computed but have no audible effect: Fruit Kick reads Final.Pitch only at the
trigger. The channel filter (types 0–7) is the one round 42 decoded for DX10 (§1.10): reused, not decoded again.

### 1.7 Verification **[EXEC + model]**

`py/fkbat.py`: 11 states × 6 note scenarios (single notes at keys 48/60/84 with offsets, pans, volumes, block sizes
333/512/1000, releases, 3-note polyphony) at 44.1 and 48 kHz: **every case without a release fade is bit-exact**
(35/66 per rate; the 31 others differ only inside the 10 ms fade, worst −83 dB, from IPP's cosine), including all
distortion presets. `py/fktick.py` with ticks every 230 samples: the snapshot envelope of Africa's Calling and four
synthetic snapshots (volume ADSR with tensions, synced envelope, volume/pan LFOs) are bit-exact outside the fade.

### 1.8 LYNX port **[model]**

`src/core/FruitKick.{h,cpp}`, `InstrumentKind::fruitkick`: bit-identical to `fkmodel.py` on 150 cases (`py/fkcpp.py`:
all states incl. synthetic, 5 scenarios with/without ticks, 44.1/48 kHz). The importer turns version-0 states into
version 1 with the channel's 0xDA/0xDB records as the snapshot. Ticks run at 96 PPQ of the song tempo from the
instrument's start.

### 1.9 Not exact / not confirmed

- The end fade's IPP 11-bit cosine (≤ −83 dB during 10 ms after a note-off).
- Global LFOs (flag 0x20) advance per tick callback; song-position jumps are not modelled.
- FL's tick grid follows the song position and the host's block split; LYNX ticks from the instrument start.
- Which waveshaper variant FL uses live vs when exporting (flag `0x16baff0`): **not confirmed**.
- The snapshot of a version-1 state vs the channel's own records when they disagree (Africa's Calling): the plugin
  keeps its snapshot [DECOMP]; whether FL overwrites it later is **not confirmed**.

### 1.10 Relation to round 42 (DX10)

DX10's state carries the same 364-byte block and DX10's DLL embeds the same voice framework, decoded completely in
round 42. After DX10 reached `main`, its processor (articulators, filter, x87 pan law, ramps) moved into
`src/core/FlVoiceProcessor.h`, used by both instruments; the importer builds Fruit Kick's snapshot with DX10's
`dx10Snapshot`. Checked against Fruit Kick's DLL with ticks every 230 samples: all 8 filter types (with cutoff
envelope + LFO and resonance LFO, `st/filt_t*.bin`) and the LFO/envelope states are bit-exact outside the end fade
(`py/fkexec_cpp.py`, C++ against EXEC). The shared x87 pan law (DX10's argument reduction above π/4) also fixed a
2.8e−21 difference of our Python model at positive pans.

---

## 2. BassDrum

### 2.1 Structure **[DECOMP, RTTI vtables `out/eng_vft.txt`]**

CFLWrapper (VMT `0x180068468`: 8 Gen_Render `FUN_180004810`, 9 TriggerVoice `FUN_180004980`, 10/11 release/kill),
the second wrapper interface (slot 10 = per-note tables `FUN_180003e60`), `BassDrumSynth` (CSynthesizer; 17 process
`FUN_18002b830`, 23 setParameter `FUN_18002ae90`, 35/36 state write/read `FUN_18002bce0`/`FUN_18002be70`, 57 voice
allocation `FUN_1800258d0`), `BassDrumGen` (voice; 2 start `FUN_180028ff0`, 4 reset `FUN_180028f80`, render
`FUN_180029910` → generator `FUN_180029440`).

### 2.2 State and parameters

`"SSLF"` + size + tag `1kiK`/`2kiK` (`ED1K`/`ED2K` are accepted but load no parameters) + 256-byte name + 1024-byte noise
path + 23 floats at byte 0x50C, in index order (round 15). The noise path: loaded directly, else by name from the
plugin's `Noise Samples` folder; paths starting with `%` (and failures) go through the host's path conversion
(dispatcher 0xE) **[DECOMP `FUN_18002be70`, `FUN_18002ab10`]**. Per-voice fields from setParameter (clamped 0…1):
level `powf(10, 2x − 1)`, base `powf(2, 4x + 4)` Hz, peak `(x·x)·(x·100)`, slide `(double)((x·x)·(x·20)) + 0.001`
(**MSVC reassociated these products**; the decompiler shows `x·x·x·c`), duration `x³`, attack x, Var. Base/Peak/Slide
`2x − 1` (indices 9, 10, 11), Var. Phase, Click Cut, Click Freq `powf(2, 14x)`, Click Amount `((x²)²·x)` in double, Butt
`x²`, Drive x, Noise A/D/S, Start, Pitch, Mix, Filter as stored **[DECOMP + objdump]**.

### 2.3 Note start **[objdump `FUN_180028ff0`]**

With `inv = 1024/N` (N = 1024 table samples) and `k = 1/44100` (float): drive `((x⁴))·2560`; gain `= level × velocity`;
duration step `1/(dur·44100 + 44.1)` (1 when duration = 0: silent); base increment `(base·inv)·k`; sweep depth
`inv·peak`, slide step `0.0002/slide`; attack smoother `1/(x⁸·100000 + 1)`; a **second oscillator** with base
`(powf(2, VarBase)·base)·inv·k`, depth `powf(2, VarPeak)·inv·peak`, slide step `0.0002/(powf(2, VarSlide)·slide)`,
starting `trunc(VarPhase²·350)` samples later; click start phase `powf(ClickCut, 0.125)·0.25 + 0.75` (deterministic: no
random), click step `ClickFreq·k`, click level `25·ClickAmount`; high-pass setup (42 Hz, Q `6·Butt² + 0.5`, voice rate);
noise: start counter `(2x − 1)³·4410` samples, read step `pow(2, (pitch − 0.5)·4)` (double), mix `2·mix²`, decay step
`1/(D⁶·44100 + 44.1)`, sustain S, attack (A ≥ 1e−4: from 0 towards 1 at `k/A⁶`; else straight into decay from 0.99),
lowpass coefficient `filter²`.

### 2.4 Generator, per internal (44.1 kHz) sample **[objdump `FUN_180029440`; EXEC]**

1. If the start counter is > 0 the oscillators wait (noise only); if < 0 the noise waits.
2. Click: while its phase < 1, `click = table(phase)·level`, phase += step.
3. Sweep `s ← s − s·slideStep`; `inc = clamp(((s²)²·depth + 1)·baseInc, 0, 0.5)`; phase (from 0.25) wraps at 1;
   `osc = table(phase + click)` (the click modulates the phase); the second oscillator likewise once its delay ran out.
4. Noise: linear read of the 16-bit sample (`s/32768`) at a double position, four one-pole lowpasses `(x − z)·a + z`,
   envelope `e += (target − e)·rate` switching to decay above 0.99, contribution `x·mix·e¹⁶`.
5. Duration: `g = 7d/(7d + 1)` while `d > 0` (d falls linearly from 1), smoothed `g ← (g·0.005 − g₀·0.005) + g₀`;
   attack `a ← (1 − a)·c + a`; output `(a_old²)·(g·osc)`.

Table read (`FUN_180028c90`): fractional phase → `x = N·frac`, linear interpolation with wrap to sample 0.

### 2.5 Voice render and output **[objdump `FUN_180029910`; EXEC]**

Linear resampler (2048-sample buffer, double position, `need = trunc(n·ratio + pos) − filled + 2`), ratio =
`clamp(+0x100, 0.25, 4)·44100/fs` with `+0x100 = pow(2, (SamplingRatio − 0.5)·4) × powf(2, T/12)` (T = the host's
per-note pitch offset, so keys transpose by resampling); chunks of ≤ 512. Then per output sample: `s ← (x − s)·0.7 + s`,
`h = x − s` through four rational saturators `h/(|h|·d + 1)`, `h/(h²·d + 1)`, `h/(|h³|·d + 1)`, `h/(d·h⁴ + 1)`
normalised by the same chain at 0.5, plus `s`; the 42 Hz high-pass (biquad with coefficients from the engine's own
`sincosf`, §2.8); × gain × 0.4. A voice ends after more than `trunc(0.02322·fs)` samples below 1e−5. Note-offs only mark
the voice released (BassDrumGen's release slot is empty): **notes never shorten**.

### 2.6 Wrapper and synth **[DECOMP `FUN_180004190`, `FUN_180003e60`, `FUN_1800258d0`; EXEC]**

As GMS's wrapper (round 39 §1.11): link velocity off (Final.Vol = channel volume), velocity `trunc(127·ln(1 + 10·Init.Vol)/ln 21)`
→ `vel/127`, gain exponent 1; per-note tables L/R (host ComputeLRVol, x87) and pitch offset T at every Gen_Render,
chunks of 512, host kill of ended voices of released keys. Voice choice: among the first `min(+0x38e4 = 64, +0x38ec)`
voices, the first free one, else the oldest released one, else the oldest — both searches start at voice 0. `+0x38ec`
comes from FL's max-polyphony event (value 0 → 99): **without that event the engine is monophonic** (voice 0 is always
retriggered); LYNX assumes FL sends it with 0, as round 39 did for GMS (**not confirmed**).

### 2.7 A mixing quirk **[DECOMP `FUN_18002b830` + EXEC]**

The synth mixes each voice as `R = x·g_R + L_old`, `L = x·g_L + L_old`: the right channel receives the left sum of the
earlier voices instead of its own. With one sounding voice nothing changes; with overlapping voices FL's right channel
carries the earlier voices' left gains. LYNX reproduces it.

### 2.8 The engine's CRT math **[objdump + EXEC `run/pw.exe`]**

- `powf` (`FUN_18003b740`, FMA path on AVX2 CPUs, chosen by `0x1800794c4`): log via a 257-entry table
  (`2/(1 + i/256)` and `ln(1 + i/256)`) and a cubic, `exp` via `2^(j/64)` and a cubic, with FMA steps; near 1
  (|x − 1| < 1/16) a series logarithm and the SSE exponential. Our port (`bdmodel.powf_msvc`, `bassdrum::powfMsvc`)
  equals the DLL on 30 000 random cases; correct rounding would differ in 2 of them — enough to change a demo preset
  (Bassdrum 01's `powf(2, 0.325)` path showed it).
- `sincosf` (`FUN_18004e750`): `(x, 1 − x²/2)` for |x| < 2⁻¹³, else a vector sine of x and of `|x| + 1.5707964f`
  (Cody–Waite reduction, odd polynomial); ported exactly.
- `pow` (double): libm's (8 of 5000 random cases differ in the last bit; used only for the noise read step and the
  sampling ratio).

### 2.9 Verification **[EXEC + model]**

`py/bdbat.py`: 57 states (5 project channels, 52 presets) × 4 scenarios (single notes at keys 48/60/72 with offsets,
velocities, pans, note-offs; 4 overlapping notes) with the noise samples and FL's max-polyphony event (value 0), at 44.1
and 48 kHz: **228/228 bit-exact at each rate**, with the model's own CRT powf port (no value taken from the DLL).
Before the powf port, Bassdrum 01 diverged at −66 dB (one last bit of `powf(2, 0.325)`); before the reassociated
products (§2.2) and the mixing quirk (§2.7), at −1.6 dB.

### 2.10 LYNX port **[model]**

`src/core/BassDrum.{h,cpp}`, `InstrumentKind::bassdrum`: bit-identical to `bdmodel.py` on 228/228 cases (`py/bdcpp.py`:
57 states × 2 scenarios × 44.1/48 kHz), hence to FL's DLL on the battery above (on Linux; the Windows build's libm `pow`,
`log`, `exp` may differ in rare last bits). The channel keeps
the state and the BassDrum folder (`samplePath`); `sounddata` and the noise sample are read at run time.

---

## 3. Open questions

- Fruit Kick: FL's tick timing; the end fade's IPP cosine; waveshaper variant when playing live.
- BassDrum: FL's max-polyphony event at channel creation (mono vs 64 voices); 24/32-bit integer noise samples (no factory
  sample uses them).
- BooBass (bonus): not examined this round.

## Evidence index

| Function / data | What | Mark |
|---|---|---|
| FK `FUN_0072d690`, `FUN_0072dce0`, `FUN_0072dcb0`, `FUN_0071b2c0` | state, conversions, increments | DECOMP |
| FK `FUN_0072e2f0`, `FUN_0072e520` | voice start and render | DECOMP + EXEC |
| FK `FUN_007212b0`, `FUN_00721470`, `FUN_00721850`, `FUN_0071f840`, `FUN_0071fd90`, `FUN_00720150`, `FUN_00720070` | framework levels, envelopes, LFOs | DECOMP + EXEC |
| FK `FUN_0071e740`, `FUN_0071def0` | end fade (IPP) | DECOMP + EXEC |
| FK `0x5f8a70`, `0x5f8c40`, `0x5fa150`, `0x5fa200` | mixers, ramp, pan law | objdump |
| FLEngine `FUN_009cdfc0`, `FUN_00b96cc0`, `0x132adc0`, `0x6a5aa0`, `0xd23687` | host sine, waveshaper tables and routine, link velocity default | DECOMP + EXEC (`fldump`) |
| BD `FUN_18002ae90`, `FUN_180028ff0`, `FUN_180029440`, `FUN_180029910`, `FUN_180028c90` | parameters, note start, generator, render, table read | objdump + EXEC |
| BD `FUN_18001d4e0`, `FUN_18001d5e0`, `FUN_180025140…250` | high-pass, resampler | objdump |
| BD `FUN_18002b830`, `FUN_1800258d0`, `FUN_180003e60`, `FUN_180004190` | synth mix, voice choice, wrapper | DECOMP + EXEC |
| BD `FUN_18003b740`, `FUN_18004e750` | CRT powf, sincosf | objdump + EXEC (`pw.exe`) |
