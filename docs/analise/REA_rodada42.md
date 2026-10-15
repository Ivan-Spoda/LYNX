# Behavioural analysis of FL Studio 2026: round 42 (REA) — Fruity DX10: the complete engine and its channel processor

> **Nature of this document.** Continuation of `REA_rodada10.md` §3 (DX10 state and parameter laws) and
> `REA_rodada17.md` §11 (FM depth), with the same goal as rounds 32 (Harmless), 37 (Ogun) and 39 (GMS/Morphine):
> describe the whole Fruity DX10 precisely enough for LYNX to play it as an exact instrument instead of the current
> approximation (`synthFromDX10` in `src/core/FlSynths.cpp`, a sine carrier with the synth's FM). *Clean-room* reverse
> engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are data layouts, formulas
> and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the
> evidence. No encrypted content was touched and no Image-Line content (code, audio, tables) was copied into the
> repository: the sine table and the processor's LFO tables are recomputed from their formulas.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line documentation (**[DOC]**), from the data (**[DATA]**) or from running the installation's own
> DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**. Inferences
> made *from* the code are stated explicitly as **inference**. Results of our own numerical model (which implements only
> what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line documentation (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target:** `Plugins/Fruity/Generators/Fruity DX10/Fruity DX10_x64.dll`, SHA-256
  `3a9d58d7e19abcba66393caa6aeecfe2fab3dbc715c9e9256f4a08da24fde8d9` (= rounds 10/17, re-hashed; copy in `r42/bin/`),
  Delphi, image base 0x400000, plugin VMT `0x7343f0`: slot 1 Dispatcher `FUN_00734570`, 3 SaveRestoreState
  `FUN_00734940`, 5 ProcessEvent `FUN_00734ee0`, 6 ProcessParam `FUN_00734f30`, **8 Gen_Render `FUN_00736a10`**,
  **9 TriggerVoice `FUN_00735e30`**, **10 Voice_Release `FUN_00736320`**, 11 Voice_Kill `FUN_00736370`,
  12 Voice_ProcessEvent `FUN_007363d0` (returns 0), **13 Voice_Render `FUN_007364a0`**, **14 NewTick `FUN_00736b00`**
  **[DECOMP + DATA (VMT read from the PE)]**. Constructor `FUN_00734ae0`; parameter laws `FUN_00735760`; voice
  stealing `FUN_00735d90`; pitch change `FUN_00736800`.
- **Shared Image-Line library inside the DLL** (the "plugin voice processor", also used by other native generators,
  e.g. Fruit Kick's 364-byte snapshot): settings object `FUN_00722ac0` (defaults `FUN_00722bc0`, snapshot load/save
  `FUN_00722c50`/`FUN_00722d00`, recompute `FUN_00722d80`), processor `FUN_00721a60` (block start `FUN_00721d10`, voice
  add `FUN_00721d70`, per-voice render `FUN_00721dc0`, mix out `FUN_00721e80`, tick `FUN_00721f60`/`FUN_00721ec0`,
  song position `FUN_00721f00`), per-voice processor `FUN_00722070` (control `FUN_00722250`/`FUN_00722830`, gains
  `FUN_00722290`, filter `FUN_00722450`), articulator settings `FUN_0071ff00` (ticks `FUN_0071fe70`, tension
  `FUN_0071fdb0` + `FUN_005f5ac0`), articulator start/step `FUN_007202c0`/`FUN_0071f9b0`, LFO tables `FUN_0071f4e0`,
  filter coefficients `FUN_00720cb0` (fast LP), `FUN_00720b10` + `FUN_007204b0` (biquads), `FUN_00720de0` (SVF),
  filter renders `FUN_005f9290` (fast LP, x87), `FUN_00720ee0` (biquad, x87), `FUN_00721000` (SVF, x87), declicker
  `FUN_0071e740`/`FUN_0071e060`/`FUN_0071e8b0`; DSP library: pan law `FUN_005fad90` → `FUN_005fad20` (x87), gain ramp
  `FUN_005fac70`, mixers `FUN_005f9590` (constant gains) / `FUN_005f9760` (ramped, SSE); unordered list delete
  `FUN_005ea710`; Delphi RTL: Round `FUN_00409fe0` (cvtsd2si), Trunc `FUN_0040a0d0`, Floor `FUN_0045fe40`, Power
  `FUN_0045fe90`, Exp `FUN_00408930`, Ln `FUN_00409b50`, SinCos `FUN_004098c0`.
- **Host table generator:** `FLEngine_x64.dll` (SHA-256 `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02`)
  `FUN_009cdfc0` (the host's `WaveTables`, round 6 §2.2), read with `objdump -M intel` only.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=4G`), project `/tmp/r42gh`, our script `r42/gh/scripts/DecRange.java`
  (decompile a VA range): `r42/out/plug.c` (0x734000…0x737000), `lib72.c` (0x71b000…0x72a000), `lib5f.c`, `f5f5.c`,
  `f5f6.c`, `f5f9.c`, `f45f.c`, `f5b6.c`, `list.c`; `objdump -M intel` of the whole text (`r42/out/all.asm`, `lib5.asm`)
  wherever the float/double/x87 operation order mattered; constants with `r42/py/k.py`.
- **EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r42/run/dx.c` → `dx.exe`, a copy of round 37's `og.c` (same
  safety measures: `SetErrorMode`, vectored fault handler ending the process, the installation's DLL loaded unmodified
  from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's own
  SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, `timeout`). Additions: the host's
  `WaveTables[0]` (host `+0x18`) points to a 16384-point sine table built like FLEngine's (§1.6; without it the voice
  render faults on a null table); `on=` sets `Init.Vol` from its third field; `hostkill` answers the plugin's host
  Voice_Kill requests by calling the plugin's own Voice_Kill after the block (as FL's host does); `tick=N` calls
  NewTick (VMT 14) at every multiple of N samples and splits host blocks there; `pef=0,<tempo>` (ProcessEvent 0 with
  float bits), `ppq=N` (Dispatcher 14 SetTimeSig with a TTimeSigInfo record), `fp=`/`fv=` change a note's
  Final.Pitch / Final.Vol+Pan mid-note, `pitchreset` restores each note's Final.Pitch before every NewTick (§3.6);
  `arr=`/`pp=` peeks as in round 37. No process or dialog was left behind; outputs were deterministic run to run.
- **Models** (pure Python + numpy, `python3 -I`; under `r42/py/`): `dxmodel.py` (plugin, voice, host interface, gains,
  mixer, end fade, run driver), `dxproc.py` (settings snapshot, articulators, LFO tables, filter coefficients and the
  three x87 filter renders in numpy `longdouble`). Drivers: `dxrun.py`, `bat.py`, `bat2.py`, `bat3.py`, `t_art.py`,
  `t_pan.py`, `t_tab.py`, `cppcmp.py`, `cppcmp2.py`; state helpers `snap.py`, `flpchan.py`; data `collect42.py`.
- **Data:** every DX10 0xD5 state of the installation (`r42/out/states42.pkl`, `collect42.py`): 75 (12 in `.flp`, 63 in
  `.fst`), 64 distinct; 73 of version 1 (92 B), 2 of version 2 (460 B, the demo songs "Song For You" and "SHINE").

---

## 0. Data survey **[DATA, `collect42.py`, `chan42.py`, `flpchan.py`]**

- Demo projects: 4 channels in 3 files ("Song For You": *Fruity DX10*; "SHINE": *Log Drum*; "Gimbal and Sinan":
  *DX10 #2* and *Log Drum*), plus the render test `System/Render tests/Fruity DX10.flp` and 7 template channels
  (Breakbeat, HipHop-Trap, Trap). 58 factory presets (`Patches/Plugin presets/Generators/Fruity DX10`) and the
  plugin-database entries.
- Channel settings (the "INS" envelopes/LFOs/filter, records 0xDA/0xDB): neutral in "Song For You", "SHINE", *DX10 #2*,
  the render test and Breakbeat; *Log Drum* of "Gimbal" uses Mod X 26 / Mod Y 9 with a cutoff envelope (amount +128);
  the HipHop-Trap/Trap synths use filter type 7 (SVF ×2), Mod X 88…255, volume and cutoff envelopes.
- Mod 2 is off (Init 0) in 48 states, Octave is −3…+2, Vibrato is used by 14 presets.

---

## 1. The voice

### 1.1 State **[DECOMP `FUN_00734940`; DATA]**

`u32 version` + 22 × i32 parameters (raw knob integers, stored at plugin `+0x1b8`). The plugin reads up to 456 bytes
after the version and accepts any version below 3 (signed compare) other than 0x7fffffff. When at least 364 bytes
follow the 92 (state ≥ 460 bytes; FL writes version 2), they are the **channel-settings snapshot** of §3.1 (loaded
into the processor's settings object; flag `+0x1b4`). Saving always writes version 2 with the current snapshot.
Each parameter then goes through ProcessParam (flag 0x11): normalised value `p = float(v − min)/float(max − min)` with
min/max of the editor knobs (all 0…65536 except Octave −3…3, DFM `r10/dx10.dfm.txt`) at `+0x210 + 4i`, then
`FUN_00735760`.

### 1.2 Parameter laws (`FUN_00735760`) **[DECOMP + objdump; EXEC (every state of §4)]**

`inv = float(1/fs)`, `r44 = float(44100/fs)`; double arithmetic rounded to float where stored.

| Field | Law |
|---|---|
| carrier base `D0` (`+0x12c`) | `float(inv · 8.175798915644 · 2^(floor(6.9·p21) − 2))` (octave −3…3 → 2^(−2…4)) |
| ratio k (Mod 1 `+0x138`, Mod 2 `+0x158`) | `float(coarse + fine) · 2^30`, coarse `= floor(40.1·p²)`; fine: p ≥ 0.5 → `round(8.9·p)` 4 → 0.25, 5 → 1/3, 6 → 0.5, 7 → 2/3, else 0.75 (floats); p < 0.5 → `float(0.2·p²)` |
| Init / Sustain levels (`+0x13c/+0x140`, `+0x15c/+0x160`) | `float(r44 · 0.0002 · p²)` (p5/p7, p16/p18) |
| velocity sensitivity S (`+0x14c`, `+0x16c`) | p9, p20 |
| vibrato depth (`+0x190`) | `float(0.001·p10²)` |
| attack coefficient (`+0x170`) | `float(1 − exp(−inv · exp(8 − 8·p0)))` |
| decay multiplier (`+0x174`) | p1 > 0.98 → 1; else `float(exp(−inv · exp(5 − 8·p1)))` |
| release multiplier (`+0x178`) | `float(exp(−inv · exp(5 − 5·p2)))` |
| Mod time / release coefficients (`+0x144/+0x148`, `+0x164/+0x168`) | `float(1 − exp(−inv·exp(6 − 7p)))`, `float(1 − exp(−inv·exp(5 − 8p)))` |
| waveform r (`+0x194`) | `float(0.5 − 3·p11²)` |
| Mod Thru (`+0x198`) | `float(0.25·p12²)` |
| vibrato LFO rate (`+0x184`) | `float(inv · 15707.4999… · p13²)` |

Plugin globals after the constructor: vibrato oscillator `s = 0`, `c = 1` (`+0x17c/+0x180`), mod wheel `+0x188 = 0`,
bend factor `+0x18c = 1` (never written afterwards) **[DECOMP]**.

### 1.3 TriggerVoice (`FUN_00735e30`) **[DECOMP + objdump; EXEC]**

1. Voice stealing (§1.7), then a 0x78-byte voice record appended to the plugin's voice list.
2. Pitch from **Final.Pitch** (cents, 0 = C5): `k = round(Final.Pitch)`, `frac = float(k − 100·(k/100))·0.01f` (C
   division), `note = trunc((Final.Pitch·0.01f − frac) + 60f)` clamped 0…127 (float operations);
   `ratio = float(exp((float(note) + frac)·ln2/12))` (constant 0.05776226505); carrier increment
   `D = (D0·bend)·ratio` (half-cycles per sample: the phase has period 2). Init.Pitch is not used for sound.
3. Velocity from **Init.Vol**: `x = min(Init.Vol·10f, 20.5511818f)`; `V = round(float(ln(x + 1)/ln(20.5511818 + 1))·127f)`
   clamped 0…1270 (so Init.Vol 1 → 99, the inference `Init.Vol = (11^(vel/100) − 1)/10` of round 28 gives V ≈
   vel·127/128).
4. Modulator levels: `ratio` is limited to 50 (as double compare), then
   `lvl = float(ratio·(double(S·(V − 64)) + 64))`; level `L = Init·lvl`, target `T = Sustain·lvl`, glide coefficient =
   Mod time; phase 0, phase increment `round(k·D)` taken modulo 2^32 (cvtsd2si 64-bit, low word); table gain 1.
5. Amplitude envelope: target `A = float((1.5 − p11)·0.0035000001·(V + 10))`, output gain `g = float(1.2/A)`, value 0,
   coefficient = attack, multiplier = decay; carrier phase φ = 0; vibrato counter 0.
6. The record is handed to the voice processor (§3).

So the peak level does not depend on velocity (g·A = 1.2); velocity and Waveform only change the modulation depth
and the waveform (mda DX10 heritage) **[DECOMP]**.

### 1.4 Voice_Render (`FUN_007364a0`) **[DECOMP + objdump; EXEC]**

Per call: if the host's Final.Pitch differs from the voice's copy, the pitch is recomputed (`FUN_00736800`: note =
`k/100 + 60`, `frac = float((k − 100·(k/100))/100.0)`, new `D` and both increments; the modulator levels are not
recomputed). Then per sample, in this float order:

1. every 101st sample of this voice (counter reloaded with 100), the **plugin-wide** vibrato oscillator steps:
   `s += rate·c; c −= rate·s`; the voice's offset = `c·(modwheel + vibrato)`; `d = D + offset` (the oscillator is
   shared, so its speed scales with the number of sounding voices and its values depend on how the host cuts blocks —
   DX10 behaviour);
2. `env += a·(A − env)`, then `A *= multiplier`;
3. `m1 = 1·sin[ph1 >> 18]`; `φ = ((φ + d) + L1·m1) + (L2·1)·sin[ph2 >> 18]` — **both modulators in parallel on the
   phase increment** (true FM); then `L1 += c1·(T1 − L1)`, `L2 += c2·(T2 − L2)`, `ph1 += inc1`, `ph2 += inc2`;
4. φ folded into [−1, 1] by repeated ±2 while |φ| > 1 (bit compare with 1.0);
5. output `(g·env)·(thru·m1 + (φ + (φ²·φ)·(r·(φ² − 1) − 1)))`, written to both channels.

After the block: if the envelope target `A` < 0.0002 (bit compare) the voice is **finished** (multiplier 1, flag set);
modulator levels below 2^-24 are zeroed with their targets.

### 1.5 Voice_Release (`FUN_00736320`) and Voice_Kill **[DECOMP; EXEC]**

Release: multiplier = release, target `A` = current envelope value, coefficient 1 (the envelope follows the target
exactly), both modulator targets 0 with their release coefficients. The processor's articulators are **not** released
(§3.4). Voice_Kill removes the record from the list (**unordered delete: the last record takes its place**,
`FUN_005ea710` with the list's ordered flag off) and frees the processor voice.

### 1.6 Host sine table **[DECOMP FLEngine `FUN_009cdfc0` + DX10 `0x7364d6`; EXEC]**

DX10 reads the host's `WaveTables[0]` (host object `+0x18`): 16384 floats `float(Sin(double(2i)·π/16384))` (Delphi's
software Sin; LYNX uses the C library's sine — equal after rounding to float in every case we compared, **not
confirmed** bit for bit against FL's RTL for all 16384 entries). The processor builds its own identical sine table for
its LFOs (`FUN_0071f4e0`).

### 1.7 Polyphony (`FUN_00735d90`) **[DECOMP; EXEC]**

ProcessEvent 1 stores the max polyphony M (`+0xe0`, 0 = unlimited). Before a new voice: if 0 < M ≤ voice count, the
unfinished voices are counted and the one with the lowest envelope target is finished (target 0) when the count
reaches M; it is removed at the end of the next block like any finished voice.

### 1.8 Gen_Render (`FUN_00736a10`) **[DECOMP; EXEC]**

With at least one voice: the processor's block start (`FUN_00721d10`: clears the mix buffer, takes pending settings
changes), each voice in list order through the processor (§3), the mix buffer copied to the output (ippsCopy), then,
last to first, every voice flagged finished (by DX10 or by the processor's end fade) is reported to the host
(Voice_Kill, host slot 5); FL's host then kills it (the harness does the same with `hostkill`).

---

## 2. Host interface

- **Levels.** DX10 never calls the host's ComputeLRVol: Final.Vol and Final.Pan go through the processor's own pan
  law (§3.5) **[DECOMP]**. DX10 sends no "link velocity" event, so FL's default applies — Final.Vol = channel volume ×
  Init.Vol when the voice's link flag is on (round 28 §4.1; FL's default link state is an **inference**, as for
  Harmless round 32 §1.16).
- **Tempo / PPQ.** ProcessEvent 0 carries the tempo as float bits (stored as double, all articulator times
  recomputed, `FUN_00722d80`); Dispatcher 14 (SetTimeSig) stores PPQ and PPQ/4 (defaults 140 BPM, PPQ 96)
  **[DECOMP + EXEC]**.
- **NewTick** (VMT 14) marks the next Gen_Render as a tick for the processor and advances global LFOs (§3.4).
- **Channel settings from the host.** ProcessParam indices 23…113 (runtime table `0x80062c`: base 23, 17 per
  articulator = the 17 fields of a 0xDA record, then Mod X/Y and type) are forwarded to the settings object; when the
  plugin has no snapshot it asks the host (Dispatcher 0x1b/0x28 → host 0x5a with that table) **[DECOMP + EXEC peek]**.
  That FL then sends the channel's own 0xDA/0xDB values is an **inference** (values not observed from FL).

---

## 3. The plugin voice processor (channel "INS" settings)

### 3.1 Settings snapshot **[DECOMP `FUN_00722c50`, `FUN_00722bc0`; DATA]**

364 bytes: five 0x44-byte records — **pan, volume, cutoff, resonance, pitch** — each exactly a 0xDA record (flags,
enabled, delay, attack, hold, decay, sustain, release, amount, LFO delay, LFO attack, LFO amount, LFO speed, LFO shape,
attack/decay/release tension); then **Mod X at 0x15c (default 256), Mod Y at 0x160, filter type at 0x168**. Defaults:
every record off with the editor defaults (100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0), the volume
record with flag 4 and release tension −101 **[DECOMP]**. The two version-2 demo states hold exactly the neutral
defaults **[DATA]**.

### 3.2 Articulator settings (`FUN_0071ff00`) **[DECOMP + objdump; EXEC]**

`ticks(v) = 0` if v < 101, else `round(float(exp(float(v·2^-16)·ln 1001) − 1)·E)`;
`E = ((PPQ/4)/48·24576)/1000` (= 0.128·PPQ) when the record's flag 1 (envelope, tempo-synced) — flag 2 for the LFO —
is set, else `E·tempo/120`. Fields: delay and hold ticks; attack step `float(1/(ticks + 1))`; decay and release steps
`float(−1/(ticks + 1))`; sustain `float(s)·(1/128)`; amount `float(a)·(1/128)`; tensions (attack, decay, release):
`t → v = float(sign)·float(exp(|t/128|·ln 6347) − 1)·0.1f`, stored with `1/|v|` and `ln(|v| + 1)`; LFO delay ticks,
fade step `float(1/(ticks + 1))`, phase increment `⌊2^32 / max(ticks(speed), 3)⌋`, LFO amount `float(a)/128`, table
by shape 0 sine / 1 triangle / 2 square (clamped).

LFO tables (`FUN_0071f4e0`): sine as §1.6; triangle `T[(i + 0x3000) & 0x3fff] = 4i/16384 − 1` for i < 8192 and
`1 − 4(i − 8192)/16384` above (rising through 0 at index 0); square +1 for the first half, −1 for the second
**[DECOMP]**.

### 3.3 Per-voice articulators (`FUN_007202c0`, `FUN_0071f9b0`) **[DECOMP + objdump; EXEC]**

Start: state 1 (attack) with position 0 — or position 1 and state hold/decay when the envelope is on with attack 100
(0 ticks) or off with flag 4 (the volume record's default: a constant 1) — or state 0 (delay) when on with a delay;
the value starts at the position; LFO delay counter = LFO delay ticks; LFO phase, fade and value 0.

Step (when the record is enabled): delay and hold count ticks; attack adds its step, clamps at 1 and goes to hold (or
decay without hold); the value is the position through the attack tension `y = 1 − (exp((1 − x)·L) − 1)/|v|` (v > 0)
or `(exp(x·L) − 1)/|v|` (v < 0), L = ln(|v| + 1); decay adds its (negative) step with the decay tension and, at or
below the sustain level, sets value = sustain, re-derives the position through the inverse curve and enters
**sustain (state 4), which DX10 never leaves** (no release reaches the processor: §3.4); any state past hold whose
value falls below 2^-24 becomes 0 with state 9. LFO (always): after its delay, phase += increment (or = the record's
global phase with flag 0x20), value = table[phase >> 18] × fade, the fade rising by its step up to 1.

### 3.4 Control values, ticks, release **[DECOMP `FUN_00722250`, `FUN_00722830`, `FUN_00721dc0`, `FUN_00721f60`; EXEC]**

The articulators step **once per Gen_Render that follows a NewTick** (tick flag cleared at the end of Gen_Render) and
once at each voice's first block. Then, in float:
- pan offset = LFO₀·LFOamount₀·2 (the pan **envelope** is not used here);
- volume factor = env₁·(LFO₁·LFOamount₁ + 1), negative → 0;
- cutoff offset = env₂·amount₂ + LFO₂·LFOamount₂; resonance offset likewise with record 3;
- pitch offset (cents) = env₄·float(raw amount₄) + LFO₄·float(raw LFO amount₄), **added to the host's Final.Pitch**
  (DX10 then re-tunes the voice, §1.4). FL must restore Final.Pitch before every tick for this not to accumulate: that
  restore is an **inference**; the harness emulates it (`pitchreset`) and the model then matches the DLL.
- the volume articulator reaching state 9 starts the end fade (§3.8).

DX10's Voice_Release does not touch the processor: channel envelopes are **never released** — a volume envelope stays
at its sustain level until DX10's own release ends the voice **[DECOMP + EXEC: state 4 kept after the release]**.

Global LFOs (flag 0x20): each NewTick adds the increment to the record's global phase — but only for records already
listed, and the list is built at the first Gen_Render with voices after a load (`FUN_00721d10` → `FUN_00721c80`), so
the first tick after loading does not advance it **[DECOMP + EXEC]**. A song-position change sets the global phase to
position × increment (`FUN_00721f00`) **[DECOMP]**; not modelled in LYNX (no data uses global LFOs).

### 3.5 Pan law and gains (`FUN_00722290`) **[DECOMP + EXEC]**

`pan = clamp(Final.Pan + pan offset, −1, 1)`, `vol = Final.Vol·volume factor`; mode 0 (the runtime mode, `0x79e478`)
uses `FUN_005fad20`: on the x87, `t = (1 + pan)·π/4`, `L = float(cos t · vol)`, `R = float(sin t · vol)` in extended
precision. The FPU reduces arguments above π/4 with its 66-bit π, so `cos t = sin(P/2 − t)` with
`P = round(π·2^64)/2^64`; at pan = +1 the left gain is 6.1230318e-17·vol, not 6.1232340e-17 **[EXEC: 360 random
pan/volume pairs, every gain bit-identical with this rule]**. The first block of a voice starts at the target gains
when its volume articulator starts past "attack" (`+0x170` = 2; else from 0). Afterwards each gain ramps over the
block: step = (target − previous)/n, limited to ±0.001 per sample (the target then becomes previous + n·step, and 0
if that is below 2^-24) **[DECOMP + EXEC]**. Mixing into the cleared buffer, voices in list order:
`out += x·g` per channel (constant gains, `FUN_005f9590`) or the SSE ramp (`FUN_005f9760`: lanes [L₀, R₀, L₁, R₁]
start at g and g + s, +2s per frame pair, the frames past the last multiple of 8 continue lane 0 with +s)
**[DECOMP + EXEC]**.

### 3.6 Filter (`FUN_00722450`) **[DECOMP + objdump; EXEC]**

Inputs `x = Final.FCut + float(Mod X)/256 + cutoff offset`, `y = Final.FRes + float(Mod Y)/256 + resonance offset`.
Type → kind (`DAT_0078cc6c`): 0 fast LP, 1 LP, 2 BP, 3 HP, 4 band stop, 5 = LP ×2, 6 SVF LP, 7 = SVF ×2.
- **Fast LP** (kind 0): `c = 0` (x ≤ 0), `1` (x ≥ 1), else `float((exp(float(x·ln 601)) − 1)·(1/600))`;
  feedback `q = 0` (y ≤ 0), else `1 − float((exp(float((1 − min(y,1)·0.625)·ln 601)) − 1)/600)`. Bypassed until
  c ≠ 1 or q ≠ 0 (then active for the voice's life). The coefficient ramps per sample from its previous value with
  the gain-ramp rule; render on the x87 at 64-bit precision: `v += (x − y)·c; y += v; v *= q; c += step`, states
  stored as doubles, flushed below 2^-24 (`FUN_005f9290`).
- **Biquads** (kinds 1…4): `f = float(((exp(float(x·ln 51)) − 1)·0.0596 + 0.02))` clamped to [0.02, 3];
  `Q = 0.1` (y ≤ 0) else `float((exp(float(min(y,1)·ln 51·80/128)) − 1)·0.4 + 0.1)`; `s, c = float(sin f), float(cos f)`,
  `α = s/(2Q)` (float), `d = 1/(α + 1)`: a1 = d·c·(−2), a2 = d(1 − α); LP b1 = d(1 − c), b0 = b2 = b1/2; BP b1 = 0,
  b0 = dα, b2 = −dα; HP b1 = −d(1 + c), b0 = b2 = −b1/2; band stop b1 = a1, b0 = b2 = d. Direct form I on the x87
  (`y = ((x₁b1 + x₂b2) − y₁a1 − y₂a2) + x·b0` in extended precision, states stored as floats, flushed below 2^-24).
  Type 5 runs a second identical biquad and scales both target gains by `float(1 − Q·(3.2 − f)·0.046875)`.
- **SVF** (kind 6): `f = float(exp(x·ln 26) − 1)·0.04` (0 for x ≤ 0, ≤ 1), `damp = 1` (y ≤ 0), `0.2` (y ≥ 1), else
  `1 − 0.8y`; x87: `low += band·f; band += (x − (band·damp + low))·f; out = low` with the state kept in extended
  precision through the block. Type 7 adds a second stage with `damp + (1 − damp)/4`.
Coefficients depend only on these inputs, **not on the sample rate** **[DECOMP + EXEC at 44.1 and 48 kHz]**.

### 3.7 Neutral state

Pan/volume/cutoff/resonance/pitch records off (volume with flag 4), LFO amounts 0, fast LP with Mod X ≥ 256 and
Mod Y ≤ 0: the processor reduces to the pan-law gains (that is what 64 of the 64 distinct states without a channel
context exercise).

### 3.8 End fade (`FUN_0071e740`, `FUN_0071e060` mode 3, `FUN_0071e8b0`) **[DECOMP + EXEC]**

Built by the constructor while the plugin's rate object still holds 44100 Hz, so the table has **441 entries at every
sample rate** (EXEC at 22.05/48/96 kHz): `0.5·(1 + cos((i + 1)·π/442))` computed with IPP (`ippsVectorSlope_32f`,
`ippsMulC_32f`, `ippsCos_32f_A11`, `ippsAddC_32f`, `ippsMulC_32f`), so its values differ from the exact cosine by up to
7.6e-5. When started (§3.4) the voice's (filtered) signal is multiplied by the table, then silenced; once the 441
entries are used the processor reports the voice finished.

---

## 4. Verification

| Test | Result |
|---|---|
| All 64 distinct installation states, 4 overlapping notes (velocities 0.4…2.06, pans, cents), 44.1 kHz/512 and 48 kHz/1000 blocks (`bat.py`, `out/bat_final.log`) | **128/128 bit-identical** to the DLL **[EXEC + model]** |
| 64 states × 4 configurations (44.1 k/333, 96 k/512 max-poly 3, 22.05 k/64 max-poly 1, 48 k/2048), random notes, Final.Pitch and Final.Vol/Pan changes mid-note (`bat2.py`, `out/bat2syn_c.log`) | **256/256 bit-identical** |
| Pan law: 360 random (pan, volume) pairs (`t_pan.py`) | 360/360 gains bit-identical |
| Processor: volume envelopes (tensions, tempo sync), LFOs (pan triangle delayed/faded, volume sine/square, synced, global), fast LP (with resonance, envelope + LFO), every filter type 1…7 with a cutoff envelope and a resonance LFO, pitch envelope + LFO, decaying volume envelope with the end fade (with the DLL's table) at 22.05/44.1/48/96 kHz, tempo 97.5/140/174 with PPQ 96/192/960 (`t_art.py`, `out/t_art_final.log`) | all bit-identical |
| The 12 project channels with their channel settings, project tempo and PPQ, ticks at the project rate (`bat3.py`) | **12/12 bit-identical** |
| LYNX's C++ port (`tools/dx10render`) vs the model: 256 neutral jobs (`cppcmp.py`) + 23 processor jobs (`cppcmp2.py`) | all bit-identical |

Residual vs FL by design: the end-fade table uses the exact cosine (−129 dB worst case on our decaying-envelope test,
−238 dB on another).

---

## 5. LYNX port (`src/core/Dx10.{h,cpp}`, InstrumentKind `dx10`)

- The importer keeps the plugin state; version-1 states get the channel's 0xDA/0xDB records appended as their
  snapshot (§2 inference), version-2 states keep theirs; `modelParams[0]` = the project's PPQ.
- Host side as FL: Init.Vol from velocity, Final.Vol = channel volume × Init.Vol, Final.Pan = note + channel pan,
  Final.Pitch = 100·(key − 60) + channel and note cents, max polyphony 1 for mono channels; NewTick on the song's PPQ
  grid while the transport plays (free-running at the same rate otherwise; FL's exact sample placement of ticks is
  **not confirmed**); finished voices removed at the end of the block.
- Filters are computed in `long double` with the x87 precision field forced to 64 bits around them; where long double
  is not the x87 format (e.g. MSVC, ARM) results are close but not bit-exact.
- Not modelled: global-LFO song-position jumps, Mod X/Y of notes (LYNX has none), FL's Final.Pitch restore timing
  (inference), host-side tick placement.

## Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| VMT `0x7343f0` | slots 1…14 | DECOMP + DATA |
| `FUN_00734940` | state load/save, snapshot condition | DECOMP |
| `FUN_00734f30` | parameter normalisation, forwarding 23…113 | DECOMP |
| `FUN_00735760` | parameter laws | DECOMP + objdump + EXEC |
| `FUN_00735e30`, `FUN_00736800` | trigger, pitch, velocity, levels, re-tune | DECOMP + objdump + EXEC |
| `FUN_007364a0` | voice render | DECOMP + objdump + EXEC |
| `FUN_00736320`, `FUN_00736370`, `FUN_005ea710` | release, kill, unordered delete | DECOMP + EXEC |
| `FUN_00735d90` | voice stealing | DECOMP + EXEC |
| `FUN_00736a10`, `FUN_00721d10/dc0/e80` | Gen_Render and processor flow | DECOMP + EXEC |
| `FUN_00722bc0`, `FUN_00722c50` | snapshot defaults and layout | DECOMP + DATA |
| `FUN_0071ff00`, `FUN_0071fe70`, `FUN_0071fdb0`, `FUN_005f5ac0` | articulator settings | DECOMP + EXEC |
| `FUN_007202c0`, `FUN_0071f9b0`, `FUN_0071f4e0` | articulator start, step, LFO tables | DECOMP + EXEC |
| `FUN_00722250`, `FUN_00722830`, `FUN_00721f60`, `FUN_00721ec0`, `FUN_00721c80` | control values, ticks, global LFOs | DECOMP + EXEC |
| `FUN_00722290`, `FUN_005fad90`, `FUN_005fad20`, `FUN_005fac70`, `FUN_005f9590`, `FUN_005f9760` | pan law, ramps, mixers | DECOMP + objdump + EXEC |
| `FUN_00722450`, `FUN_00720cb0`, `FUN_00720b10`, `FUN_007204b0`, `FUN_00720de0`, `FUN_005f9290`, `FUN_00720ee0`, `FUN_00721000`, `FUN_005f91a0` | filter | DECOMP + objdump + EXEC |
| `FUN_0071e740`, `FUN_0071e060`, `FUN_0071e8b0`, `FUN_0071b4c0` | end fade, table length at 44.1 kHz | DECOMP + EXEC |
| FLEngine `FUN_009cdfc0` | host sine table | DECOMP (objdump) |
| 75 states (`collect42.py`), 12 project channels (`flpchan.py`) | versions, usage, channel settings | DATA |
