# Behavioural analysis of FL Studio 2026: round 37 (REA) — Ogun: the complete voice as a portable engine

> **Nature of this document.** Continuation of `REA_rodada12.md` §2 (format), `REA_rodada21.md` §2 (scales),
> `REA_rodada24.md` §1 (initial spectrum) and `REA_rodada26.md` §1 (ALT DECAY and release), with the same goal as
> rounds 32 (Harmless) and 34 (Harmor): describe Ogun's whole voice precisely enough for LYNX to port it as an exact
> instrument instead of the current approximation (`synthFromOgun` / the spectral oscillator in
> `src/core/FlGenerators.cpp`). *Clean-room* reverse engineering, behavioural only. No Image-Line (nor Intel IPP) code
> or pseudocode was copied: what is here are data layouts, formulas and behaviours described in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was touched
> (no FLEX pack was opened) and no Image-Line content (code, audio) was copied into the repository.
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the official Image-Line manual (**[DOC]**, with URL), from the data (**[DATA]**) or from running the installation's
> own DLL, unmodified (**[EXEC]**). Where there was not enough evidence, the item is marked **"not confirmed"**.
> Inferences made *from* the code are stated explicitly as **inference**. Results of our own numerical model (which
> implements only what is marked DECOMP/EXEC) are marked **[model]**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/`.fst`/installation files |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | output of our own model, which only implements DECOMP/EXEC facts |
| **not confirmed** | not enough evidence in this round |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Target:** `Plugins/Fruity/Generators/Ogun/Ogun_x64.dll`, SHA-256
  `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= rounds 12/21/24/26, re-hashed this round, copy in
  `r37/bin/`), Delphi, plugin VMT `0x8621f8`. It calls the installation's Intel IPP `Shared/dsp_ippv2_x64.dll`
  (`4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4`, = round 24). VMT slots used: 1 Dispatcher
  `FUN_00862770`, 3 SaveRestoreState `FUN_00864d00`, 5 ProcessEvent `FUN_00867c60` (→ `FUN_0077a420`), 6 ProcessParam
  `FUN_008684e0`, **8 Gen_Render `FUN_0086d450`**, **9 TriggerVoice `FUN_0086a8e0`**, **10 Voice_Release `FUN_0086c2a0`**,
  11 Voice_Kill `FUN_0086c420`, 12 Voice_ProcessEvent `FUN_0086c490` **[DECOMP + DATA (VMT read from the PE)]**.
- **Key functions of this round:** voice control/render `FUN_0086c6e0`; Gen_Render effect chain (EQ `FUN_0086d3a0`);
  timbre generator (object plugin `+0x230`) and timbre voice (voice `+0xe50`): sizing `FUN_008424b0`, rate
  `FUN_00842590`, phase table `FUN_008426e0`, map set/update span `FUN_00842960`, normalisation `FUN_008428c0`/
  `FUN_00842820`, voice start `FUN_00842ee0`, first frames `FUN_00843220`, frame step `FUN_00843300`, spectrum and
  decay/release set-up `FUN_00843680`, seed draw `FUN_00843590`, fullness shaping `FUN_00843470`, release
  `FUN_008447d0`, render `FUN_00844940`, crossfade `FUN_008446e0`/`FUN_008448b0`/`FUN_008448f0`, speed `FUN_00844660`,
  unison slot `FUN_00844680`, start-point search `FUN_00842e90`, table reader `FUN_00842b90`/`FUN_00842c60`/
  `FUN_00842de0`; per-partial map sampling `FUN_0086e6c0` (axis `FUN_008669f0`/`FUN_00866da0`); quality selector
  `FUN_00866840`, table oversampling `FUN_0086e620`, rates `FUN_00864800`, control block `FUN_00866510`, articulator
  rates `FUN_00866460`; filter `FUN_00867cc0`; timbre release trigger `FUN_0086c050`; voice cut `FUN_0086c390`;
  master level/tuning `FUN_0086a570`; parameter laws `FUN_008684e0`; plugin constructor (around `0x86790f`); tempo
  `FUN_00867c60`; effects set-up `FUN_0086a2d0` (chorus), `FUN_00869dd0` (delay), `FUN_0086a0a0` (reverb),
  `FUN_0086a670`/`FUN_0086a800` (EQ). **Articulator library** (`0x780000…0x790000`): articulator `FUN_0078b3e0`
  (load), `FUN_0078bae0`, `FUN_0078bb40` (static value), `FUN_0078b660` (note start), Ogun's per-block override
  `FUN_00862610`, its loader `FUN_008625b0`; curve object: load `FUN_00781bd0`, envelope extra `FUN_00789ba0`, LFO
  extra `FUN_0078a030`, map `FUN_0078b1e0`, coefficients `FUN_00785940`, wave count `FUN_007872f0`, point value
  `FUN_00787170`, free evaluation `FUN_00788a20`, map lookup `FUN_0078af50`, map table `FUN_0078ac70`, normalisation
  `FUN_00784e20`, envelope start `FUN_00789d30`/`FUN_00787970`, step `FUN_00787d80`, release `FUN_00787bc0`, offset
  setters `FUN_00789cf0`/`FUN_00789dd0`, knob scales `FUN_00789c70`, position skip `FUN_00789600`, rate
  `FUN_00789aa0`; LFO set-up `FUN_0078a0e0`, start `FUN_0078a530`, step `FUN_0078a5a0`, speed `FUN_0078a260`.
  **DSP library** (`0x5fb000…0x632000`): pan law `FUN_0062b4d0` (x87 path used on this CPU), stereo volume/pan matrix
  `FUN_00616910`, gain ramp `FUN_0062b400`, stereo mixer `FUN_00629550`, biquad `FUN_006256b0` (+ state rounding
  `FUN_006284e0`), biquad designer `FUN_00624cc0`, EQ band designer `FUN_00625290`, zero-order-hold upsampler
  `FUN_0062bf80`, 2:1 averager `FUN_0062bfd0`, float split `FUN_00611750`, interpolation kernels `FUN_0060fd20`
  (linear) / `FUN_00610250` (6-point), small-value flush `FUN_006284a0`, silence test `FUN_0062a080`, CPU-dispatch set-up
  `FUN_00627e80`; low level: integer power `FUN_004660b0`, round `FUN_0040c3e0` (banker's), truncation `FUN_0040c4d0`,
  ln `FUN_0040bf50`, exp `FUN_0040ad30`.
- **Tools:** Ghidra 12.1.4 headless (JDK 21, `MAXMEM=2G`), new project `/tmp/r37gh_Og`: full decompilation of
  `0x840000…0x870000` (`r37/out/plug_all.c`), `0x780000…0x790000` (`lib78.c`), `0x5fb000…0x632000` (`lib60.c`),
  `0x830000…0x840000` (`fx.c`) and single functions (`low.c`, `pe.c`, `rs.c`); copies with IPP import names
  substituted (`*_ipp.c`, made by our `r37/py/ippsub.py` from the dispatcher `FUN_005bc070`, `out/disp.c`); `objdump`
  for instruction-level checks (`out/trig.asm`, `out/d3680.asm`) and for `Shared/Reverb_x64.dll` (analysis copy and
  disassembly in `r37/rv/`, not decompiled); constants with `r37/py/k.py`. The effects (§1.15–1.19) were decoded by
  parallel sub-tasks from the same dumps, under the same rules.

**EXEC harness** (our own C, Zig 0.14.1 → Windows x64): `r37/run/og.c` → `og.exe`, derived from round 34's `hm.c`
(same safety measures: `SetErrorMode`, vectored fault handler that ends the process, the installation's DLL loaded
unmodified from `C:\Program Files\Image-Line\FL Studio 2026\…`, minimal fake host, state restored through the plugin's
own SaveRestoreState from a read-only `IStream`, `TerminateProcess` at the end, everything under `timeout`). New for
Ogun: the plugin asks the host, while it is being created, to *locate* its data files (host object returned by
dispatcher id 0x47, method slot 6, id 1020, ANSI relative names such as `Ogun\Data\Default.dat`); the harness answers
with the installation's `Plugins\Fruity\Generators\` folder (without this the constructor raises "Cannot open file")
**[EXEC + DECOMP `0x776040`, `0x867790…0x86790f`]**. Notes are triggered with the SDK `TVoiceParams` record
(`Init.Pitch = 100·key`, `Final.Pitch = 100·key − 6000 + offset`, Final.Vol/Pan/FCut/FRes as given); velocity, release
velocity and other per-voice host answers come through Voice_ProcessEvent (event 3 = velocity, 4 = release velocity,
float bits; others 0); `pe=1,n` sends ProcessEvent 1 (max polyphony). Host blocks are split at every event sample
(default block 512). Read-only peeks `arr=`/`peek=`/`pp=` read plugin/voice fields and arrays through pointer paths.
The fake host never calls Voice_Kill, so finished voices stay listed (see §1.14). No process or dialog was left behind.

**Model (pure Python + numpy, `python3 -I`):** `r37/py/ogmodel.py` (plugin, voice, timbre engine, envelopes, LFO, filter,
EQ, gains), `ogcurve.py` (point curves), `ogpatch.py` (state parser), `ogfx_chorus.py`, `ogfx_delay.py`,
`ogfx_reverb.py` (plugin effects), `ogos.py` (quality 2–4 kernels: bit-level emulation of the 32-point reader and the
2:1 decimator; `ogos_rcp_amd.npy` = `rcpps` results measured on this CPU), `ogos_fast.py` (the 32-point reader in
double, used by the model). Driver and
verification: `ogrun.py` (runs `og.exe`), `cmp.py` (runs DLL and model on the same events), `bat_voice.py` (demo
battery: `voice` = effects off, `eq` = EQ only, `full` = everything), `bat_synth.py` (synthetic feature battery),
`t_lfo.py`, `t_delay.py`, `t_chorus.py`, `t_reverb.py`, `t_os.py` (+ `run/og_os.c`: calls the DLL's reader and
decimator directly), `t_misc.py` (polyphony, qualities), `survey2.py` (data survey). Outputs in `r37/out/` (`bat_*.log`,
`t_*.log`). Data: `r14/blobs14.pkl` (the 20 Ogun channels of the installation's demo projects).

Manual pages: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Ogun.htm` and the
pages linked from it (`Ogun_synthesis.htm`, `Ogun_master.htm`), read in rounds 21/24; no new quotation is needed here.

---

## 0. Data survey: the 20 demo channels **[DATA, `r37/py/survey2.py`]**

Features per channel (q = quality P54; rich = Richness = P52 − 5 and M = 2^(P52−1) − 1 partials; s1/s2 = seeds
(`perkey` = Seed 1 per key, byte +0x20c), m = morph, f = fullness, pre = pre-decay, dec/rel = P6/P7 (10000 = infinite
decay); `art[...]` = articulators whose envelope (E, `Et` = tempo-based) or LFO (L) is switched on; effects from
§1.15–1.18):

| # | Channel | Voice features | Plugin effects |
|---|---|---|---|
| 0 | 3.3.1 Metal Drums: Drum | q0, rich9(M=8191), s1=49 s2=1 m=0.500 f=0.000 pre=0.538, dec=2548 rel=988, norm, logx | EQ |
| 1 | 3.3.1 Metal Drums: Click | q1, rich7(M=2047), s1=84 s2=random m=0.500 f=0.000 pre=0.276, dec=0 rel=1082, norm | EQ |
| 2 | 3.3.2 Tibetan | q0, rich10(M=16383), s1=none s2=random m=0.000 f=0.000 pre=0.000, dec=2280 rel=2120, norm, declick | - |
| 3 | 3.3.3 Chase | q0, rich11(M=32767), s1=perkey s2=random m=0.196 f=0.000 pre=0.000, dec=3233 rel=2480, nokeytrack | EQ |
| 4 | Bellish 11 | q1, rich2(M=63), s1=none s2=29 m=1.000 f=-1.000 pre=0.100, dec=5200 rel=1760, uni4, declick | reverb |
| 5 | High Chords | q1, rich11(M=32767), s1=none s2=random m=0.000 f=-0.680 pre=0.656, dec=8720 rel=2000, norm, art[vol:E], declick | chorus9 |
| 6 | Kick  | q0, rich4(M=255), s1=none s2=random m=0.000 f=0.064 pre=1.000, dec=160 rel=0, logx, filtLP/3 flat, art[vol:Et fcut:E fres:E], declick | EQ |
| 7 | Clap | q0, rich7(M=2047), s1=68 s2=random m=0.000 f=-1.000 pre=0.108, dec=1200 rel=3760, norm, logx, pat[3, 0, 0, 0], uni9, filtLP/3 flat, art[vol:Et fcut:Et pitch:E] | EQ, reverb |
| 8 | High Hat | q0, rich9(M=8191), s1=7 s2=random m=0.192 f=-0.808 pre=0.388, dec=4000 rel=0, logx, uni2, filtBP/2, art[vol:E fcut:E fres:E], nokeytrack, declick | EQ, reverb |
| 9 | HiHat | q0, rich11(M=32767), s1=none s2=random m=0.144 f=-0.904 pre=0.000, dec=3760 rel=520, norm, art[vol:E], nokeytrack, declick | - |
| 10 | Snare | q0, rich8(M=4095), s1=none s2=random m=0.000 f=1.000 pre=1.000, dec=inf rel=5240, norm, logx, pat[3, 0, 0, 0], uni9, filtLP/3 flat, art[vol:Et fcut:Et pitch:Et] | EQ |
| 11 | Swebass 1 | q0, rich5(M=511), s1=none s2=random m=1.000 f=0.000 pre=0.000, dec=4480 rel=840, ALT, logx, pat[1, 0, 4, 0], uni5, filtLP/3 flat, art[vol:E fcut:E], declick | EQ, chorus1, delay |
| 12 | Swebass 2 | q0, rich5(M=511), s1=none s2=random m=1.000 f=0.000 pre=0.000, dec=7480 rel=1440, ALT, logx, pat[1, 0, 4, 0], uni5, filtLP/3 flat, art[vol:E fcut:E], declick | EQ, chorus1, delay |
| 13 | Midnight Lady | q0, rich11(M=32767), s1=none s2=random m=0.536 f=0.728 pre=0.000, dec=7920 rel=2800, ALT, filtLP/2, art[vol:E fcut:E pitch:E] | EQ, reverb |
| 14 | Supersaw | q0, rich9(M=8191), s1=none s2=random m=1.000 f=0.768 pre=0.656, dec=6332 rel=5576, ALT, norm, uni5, filtLP/1 flat, art[vol:E fcut:E pitch:E], porta(lim4), declick | EQ, delay, reverb |
| 15 | Airguitar 3 | q0, rich10(M=16383), s1=none s2=random m=0.708 f=0.792 pre=1.000, dec=8280 rel=4337, uni2, filtBS/3 flat, art[vol:Et fcut:Et pitch:Et], declick | EQ, chorus7, delay, reverb |
| 16 | Trumpet | q0, rich2(M=63), s1=none s2=random m=0.000 f=1.000 pre=0.000, dec=0 rel=3800, ALT, logx, uni2, filtLP/1 flat, art[vol:Et fcut:EL fres:E pitch:Et], vel->env2, declick | EQ, chorus7, delay, reverb |
| 17 | Silverswirll | q0, rich10(M=16383), s1=none s2=random m=1.000 f=-0.680 pre=0.656, dec=2828 rel=787, norm, filtLP/2, art[vol:Et fcut:E pitch:E], vel->env1 | EQ, reverb |
| 18 | Padsaw | q0, rich8(M=4095), s1=none s2=random m=1.000 f=0.328 pre=1.000, dec=inf rel=4936, norm, uni4, filtLP/3, art[vol:E fcut:Et pitch:E], porta(lim4), declick | EQ, delay, reverb |
| 19 | Turbular Bells 2 | q0, rich11(M=32767), s1=none s2=random m=0.000 f=0.000 pre=0.000, dec=inf rel=10000, ALT, norm, pat[0, 4, 0, 0], uni7, filtLP/1 flat, art[vol:E fcut:E pitch:Et unipitch:E], declick | EQ |

Summary of the survey: all 20 use quality 0 or 1 (no output oversampling); richness 2…11 (63…32 767 partials); 18 use
Seed 2 = "Random" (a new spectrum per note whenever morph > 0: 11 channels); 1 uses Seed 1 per key; 6 use ALT DECAY;
5 use fixed patterns; 15 the volume envelope, 13 the filter-cutoff envelope, 9 the pitch envelope, 1 the unison-pitch
envelope, 1 an LFO (with a neutral amount curve), 2 the velocity options of the envelopes, 2 portamento; 13 the filter
(all four types); 16 the EQ, 4 an audible chorus (a fifth has mix 0), 6 the delay, 9 the reverb **[DATA]**.

---

## 1. Ogun — the voice

### 1.1 Structure and data flow **[DECOMP]**

- **Gen_Render** (`FUN_0086d450`), per host block of n samples (×2 at qualities 2–4, §1.19): tail counter (§1.14);
  every listed voice renders and *adds* into a plugin float32 stereo buffer (`FUN_0086c6e0`, list walked from the last
  voice to the first); global-LFO counters advance (none in the demos); 3-band EQ (§1.15); chorus (§1.16); delay
  (§1.17); reverb (§1.18); 2:1 decimation at qualities 2–4 (§1.19); the buffer is added to the host output.
- **Voice record** (0x10c0 bytes, `TriggerVoice`): host record pointer `+8` (Final.Pan `+0x14`, Vol `+0x18`, Pitch
  `+0x1c`, FCut `+0x20`, FRes `+0x24` are re-read live), state `+0x10` (1 playing, 5 released, 9 ending, 10 dead),
  control counter `+0x18`, note number `+0x30`, static articulator values `+0x80 + 8k`, per-articulator envelope/LFO
  states `+0xe8 + 0x108k` (output of articulator k at `+0x1e8 + 0x108k`), volume/pan gains `+0x34…+0x70`, timbre voice
  `+0xe50`, unison slots (0x3c each) at `*(+0xf08)`, filter stages `+0xf48 + 0x78i`, X/Y table indices `+0x10b0/+0x10b4`.
- **Control block** (`FUN_0086c6e0`): the voice runs in chunks; at each control-block boundary it evaluates the first
  five articulators (volume, filter cutoff, filter resonance, pitch, unison pitch), the volume/pan gain targets, pitch and
  unison pitch; between boundaries it renders the timbre (§1.7), the declick, the filter (§1.10) and mixes with gain
  ramps (§1.12). The other eight articulators are evaluated **once, at note start** **[DECOMP]**.

### 1.2 Rates, quality and control block **[DECOMP `FUN_00866840`, `FUN_0086e620`, `FUN_00864800`, `FUN_00866510`; EXEC]**

| Quality (P54) | Reader kernel (byte +0x268) | Table oversampling q | Frame step | Control nominal (+0x264) | Output oversampling |
|---|---|---|---|---|---|
| 0 | linear (1) | 0 | 50 ms | 96 | none |
| 1 | 6-point Hermite (2) | 0 | 50 ms | 96 | none |
| 2 | 6-point Hermite (2) | 1 | 50 ms | 48 | ×2 |
| 3 | 6-point Hermite (2) | 2 | 25 ms | 24 | ×2 |
| 4 | 32-point (6) | 2 | 25 ms | 8 | ×2 |

- Table oversampling is raised to 1 when Richness+5 (P52) < 10 and the internal rate < 88 200 Hz.
- **Rendering forces quality 4:** Dispatcher id 1 (process mode) with flag bit `0x10` sets plugin byte `+0x5e6`, and the
  quality selector then uses 4 whatever P54 says **[DECOMP `FUN_00862770` ~0x8630xx, `FUN_00866840`]**. FL sets this flag
  when it renders to a file (SDK process-mode flag "is rendering"; **inference** from the SDK flag name — not observed
  from FL). LYNX's offline render should use quality 4 to match FL's export.
- Internal rate fs' = fs × (2 at qualities 2–4); the "rate object" (`+0xcc`) holds fs' at `+8`, `44100/fs'` at `+0x20`,
  `2π/fs'` at `+0x28`, `0.4999·fs'` at `+0x30` (float) and `0.001·44100/fs'` at `+0x34` (float, gain-ramp limit)
  **[EXEC: peeks at 44.1 and 48 kHz]**.
- Control block: `r = nominal·fs'/44100`, **`B = 4·round(r/4)`** (min 4) samples (96 at 44.1 kHz quality 0/1, 104 at
  48 kHz); `ρ = B·nominal/(r·96)`; X/Y smoothing factor `0.01^(ρ/96)` (float) **[DECOMP]**.
- Frame step `hop = round(ms·0.001·fs')` (2205 at 44.1 kHz) **[DECOMP + EXEC]**.

### 1.3 State format (complete) **[DECOMP `FUN_00864d00`, `FUN_008625b0`, `FUN_0078bae0`, `FUN_0078b3e0`, `FUN_00781bd0`, `FUN_00789ba0`, `FUN_0078a030`, `FUN_0078b1e0`; DATA: the parser reaches the exact end of all 20 demo states]**

After the u32 version word and zlib (round 12): 64 × i32 `P[0…63]` (plugin `+0x12c + 4i`; older versions shorter as
round 12); 3 bytes → plugin `+0x358` (delay tempo-based, delay on, unused); 3 × i32 EQ band types (`+0x388 + 0x80k`);
the comment (v < 18: u32 byte count + bytes; v ≥ 18: 7-bit-varint character count + UTF-16); then **13 articulators**,
each:

| Part | Content |
|---|---|
| i32 | version (ignored) |
| 3 × f32 | base value (`+0x20`), envelope amount (`+0x24`), LFO amount (`+0x28`) |
| curve | envelope (type 1) |
| 16 B | 4 × i32 envelope knobs (attack, decay, sustain level, release; `+0x98`) |
| curve | LFO amount curve (type 2) |
| 16 B + 4 B | 4 × i32 LFO knobs (speed, tension, skew, pulse width; `+0xd0`) + i32 phase offset (`+0xe0`, curve version ≥ 3) |
| curve ×4 | key map, velocity map, **X map, Y map** (type 3) |

followed by **4 curves**: 13 Forced harmonics, 14 Impulse EQ, 15 Decay EQ, 16 Release EQ. A *curve* is: i32 type, i32
version (3), i32 n, n × (f64 dx, f64 y (kept as f32), f32 tension, u8 mode, u8 arpeggio, 2 B), then 5 × i32 (flags,
enabled, decay point, loop point, sustain point). The owner's configuration byte decides which header fields survive
(envelopes `0x2d`, LFOs `0x27`, maps `0x20`: maps lose all three points, LFOs the decay point, arpeggio bytes are cleared
everywhere) **[DECOMP + EXEC peeks of `+0x20` per object]**. Map curves are normalised to total length 1 (a first point
is inserted at x = 0 when missing) **[DECOMP `FUN_00784e20`]**.

Fixed per-articulator constants (constructor, **[EXEC peeks]**): combination mode (0 product, 1 sum) = 0 for volume, unison
pitch, unison phase, pre-decay, decay time, release time, morph; 1 for cutoff, resonance, pitch, fullness, rotation, EQ
shift. Curve centre (value subtracted from every point) = 0.5 for the additive ones and for every LFO curve, 0 otherwise;
envelope/LFO values are clamped to [0, 1] before the centre is removed.

Packed bytes used by the voice (plugin offset = state body offset + 0x12c): `+0x208` X/Y smoothing, `+0x209` cut a
playing voice on the same key, `+0x20a` (ask host event 6; result unused), `+0x20b` pitch follows the key, `+0x20c`
Seed 1 per key, `+0x20d` velocity → envelopes (bit 1: attack time, bit 2: start position), `+0x20e` declick, `+0x20f`
timbre release starts when the volume envelope leaves its decay point, `+0x210` legato/portamento, `+0x211` portamento
time follows the interval, `+0x212` portamento range mode, `+0x213` logarithmic partial axis, `+0x214…+0x223` the four
fixed patterns, `+0x224` ALT DECAY, `+0x225` normalise by the Impulse/Decay maps, `+0x226` 1/√h option, `+0x200` reverb
on, `+0x201` reverb tempo-based **[DECOMP]**.

### 1.4 Point curves **[DECOMP `FUN_00785940`, `FUN_007872f0`, `FUN_00787170`, `FUN_00788a20`, `FUN_00787d80`; EXEC through §1.21]**

Same family as Harmor's curve object (round 34 `hcurve.py`), with Ogun's own storage: per point a coefficient triple
(k₁, k₂, k₃) and a sign byte computed once (`FUN_00785940`), segment values in double:

- modes 0/1 (curve / double curve): `q = sign(T)·(1000001^|T| − 1)`, k₁ = q, k₂ = 1/|q|, k₃ = ln(|q| + 1); shape
  `1 − (e^{(1−t)k₃} − 1)k₂` (q > 0) or `(e^{t·k₃} − 1)k₂` (q < 0); mode 1 applies it to each half;
- modes 3–6 (stairs, smooth stairs, pulse, wave): wave count `N = round((1000001^{1 − 0.6|T|} − 1)·0.001)` (+1 for
  stairs with T ≥ 0); modes 7/8 (single/double "S"), 9 (sine-curved), 10 (monotone cubic through the neighbouring points),
  11/12 (hyperbolic) — the formulas are those of round 34 §1.12 with Ogun's k₁/k₂/k₃ in the places of Harmor's
  k10/k14/k18; mode 2 holds the start value; a zero tension in modes 0, 1, 7, 8, 9, 11, 12 makes the segment linear.
- **Point value** with an offset `m`: `y_i + w_i·m`, clamped to the curve's range when `m ≠ 0`, minus the centre. The
  weights `w_i` depend on the decay/loop/sustain points exactly as round 34 (`henv.py` `weights`) **[DECOMP]**.
- **Evaluation** is always *sequential*: a state (point index, segment fraction t, rate) is stepped by Δx; when t
  reaches 1 the next segment starts and the overshoot is carried over. Tables are built by stepping (`render`), and the
  per-partial maps by stepping to each partial's position (§1.7) **[DECOMP]**.

### 1.5 Articulators **[DECOMP `TriggerVoice`, `FUN_0086c6e0`, `FUN_0078bb40`, `FUN_0086d9e0`, `FUN_00862610`, `FUN_0078b660`; EXEC: static values of all 20 demo channels and per-block outputs equal the model to 10⁻⁶]**

- **Static value at note start** S_k = combine(key map at `note/120`, velocity map at the host velocity) by the
  articulator's mode (product / sum; mode ≥ 2 would be 2·product), as float. `note = Final.Pitch·0.01 + 60` (≥ 0).
- **X/Y tables:** each articulator's X and Y maps are rendered into 513-point float tables (step 1/512) when the map is
  not the neutral default **[DECOMP `FUN_0086d9e0`]**; the indices are `round(X + 256·Final.FCut)` and
  `round(Y + 256·Final.FRes)` clamped to 0…512, where X/Y are P3/P4 (smoothed per block towards the knob with factor
  `0.01^(ρ/96)` when byte `+0x208` is set). So the host's Final.FCut/FRes move the X/Y position.
- **Articulators 5…12** (unison phase, pre-decay, decay time, release time, fullness, morph, rotation, EQ shift): the
  static value combined with the X and Y table values, fixed for the whole note. Release time is evaluated at note-off
  with the release velocity (§1.9).
- **Articulators 0…4**, each control block: v = S_k ⊗ X-table ⊗ Y-table; if the envelope is on, the envelope steps
  (Δx = 1 per block, 0 for the first block) and v becomes `v + e·amt` (mode 1) or `v·e·amt` (mode 0); if the LFO is on,
  v becomes `v + l·amt_L` (mode 1) or `v·(1 + 2·l·amt_L)` (mode 0) **[DECOMP `FUN_00862610`]**.
- **Envelope time engine** (`FUN_00787970`/`FUN_00787d80`/`FUN_00787bc0`): rate per block = `2·ρ·96/44100` (seconds-
  based) or `ρ·96/44100·tempo/60` (header flag 1 = tempo-based; the tempo is plugin `+0x540`, set by ProcessEvent 0,
  140 when never sent) **[DECOMP `FUN_00866460`, `FUN_00867c60`; EXEC: Kick's tempo-based volume envelope]**. A segment
  of length dx lasts `dx·scale/rate` blocks, scale = attack knob/128 before the decay point, decay knob/128 after it,
  release knob/128 after note-off; at the decay point the offset m becomes the sustain knob/128 (so points after it move by
  `w_i·sustain`). Note-off: if there is a sustain point the state jumps to it and the offset is adjusted so that the
  value is continuous (`m += current − value(sustain point)`, unclamped), then the release segments run; the envelope
  ends at stage 9.
- **Velocity options** (`+0x20d`): bit 1 multiplies the attack scale by `2 − 2·velocity`; bit 2 advances the envelope at
  note start by `velocity` × the length up to the decay point **[DECOMP `FUN_00789600`; EXEC: Trumpet, synthetic]**.
- **Portamento** (`+0x210` ≠ (host event 2 = 14)) and a previous note exists: the pitch envelope's start is offset by the
  interval `d = previous pitch − this pitch` (cents/1200): offset m = d/1200 if the envelope has a decay point, else the
  sustain level; byte `+0x212` = 1 wraps d into ±1250 cents by octaves, 2…5 limits |d| to `(6 − mode)·100` cents;
  byte `+0x211` multiplies the attack scale by |d|/1200 **[DECOMP TriggerVoice; EXEC: Supersaw, Padsaw, synthetic]**.
- **LFO** (`FUN_0078a0e0`/`FUN_0078a530`/`FUN_0078a5a0`, `FUN_0060e880`/`FUN_0060ea40`): its curve is the *amount*
  envelope; output = amount × wave(φ), φ a 32-bit counter starting at the middle of the rising ramp plus the phase offset,
  advanced per block by `round(Δx'·inc)`, `inc = round(min((10001^{speed/65536} − 1)·6 871 947.6736·rate, 805 306 368))`
  (0 with header flag 8), where Δx' excludes the part of the step spent in the first (delay) segment. The wave is a
  ramp-down / hold / ramp-up shape with skew and pulse width (breakpoints in 2³² units), shaped by a tension curve
  `T = sign(t)·(1e18^{(21^{|t|/128} − 1)·0.05} − 1)`; header flag 4 selects the unipolar shaping. Global LFOs (header flag
  2) are **not modelled** (none in the demos) **[DECOMP; EXEC: 10 synthetic LFO variations −118…−130 dB]**.

### 1.6 Pitch and tuning **[DECOMP TriggerVoice, `FUN_0086c6e0`, `FUN_0086a570`]**

- Pitch articulator static value ×4 (octaves), minus `Final.Pitch/1200` when byte `+0x20b` is 0 (pitch does not follow
  the key). Per block: **cents = float(Final.Pitch + 100·P1 + P2 + art₃·1200)**; ratio = `exp(cents·ln2/1200)`; read
  speed = ratio·`float(33 488.074·2^q)`/fs' (33 488.074 = 440·2^{75/12}, round 24) — recomputed only when the cents
  value changes.
- Unison pitch: `S = float((11^{float(P15·art₄)/256} − 1)·5)` cents; sub-voice k ratio =
  `float(exp(det_k·float(S·ln2/1200)))` with det_k = ±|u|·d_k (round 21 table d = 0.877, 1.069, 0.929, 1.181, 1.031,
  1.129, 0.887, 1.117, 0.977; signs alternate, reversed for the upper half of odd counts).

### 1.7 The timbre engine **[DECOMP `FUN_008424b0`…`FUN_00844940`; EXEC: tables, spectra, positions and gains peeked equal to the model]**

- **Partials:** M = 2^(P52−1) − 1; FFT size N = 2^(P52+q); partial i (bin h = i + 1) sounds at **h·f·2^{7−P52}**
  (round 24): for P52 = 7 the partials are the harmonics 1…63 of the note; for P52 > 7 there are 2^{P52−7} − 1 inharmonic
  partials between consecutive harmonics, for P52 = 6 only even harmonics **[DECOMP + EXEC]**.
- **Seeds:** Seed k (P10/P11) → `|mix(v − 0x20cf)|` (round 24), 0 = none/random. Seed 1 per key: `|mix(P10 + key − 0x1f88)|`
  (constant 0x1e42). **Seed 2 = Random:** `|mix(counter − 0x200e)|` (constant 0x1dbc), counter `+0x244` incremented on
  every note (also when morph = 0); the counter is initialised **when the plugin is created** from the Delphi RTL random
  seed (`RandSeed`, global `0x88e010`; `Randomize` sets it from the clock) **[DECOMP `0x8677xx`, `FUN_00409fa0`]** — FL's
  draws cannot be reproduced; the harness value was read back for the comparisons.
- **Weights B** (per partial, float32): u = IPP uniform (round 24 §1.3) rotated by ρ = `round(art₁₁·M + 256·M) mod M`;
  fullness `f = clamp(P8/1000 + 2·art₉, −1, 1)` → `p = −sign(f)·(10000001^|f| − 1)·0.01`; shaping g(u) = u (p = 0),
  `((1+p)^u − 1)` (p > 0), `(1−p) − (1−p)^{1−u}` (p < 0), scaled by 1/|p| (IPP `Exp_32f_A11`); morph m = P9/1000·art₁₀:
  `B = m·g(u₂) + (1 − m)·[g(u₁) or 1 if Seed 1 = none]` (terms skipped for m = 0 / m ≥ 1), negatives → 0.
- **Forced map:** stored as `1 − F` (F = the map value, a constant-0 map is dropped) and applied as `B ← B·(1−F) + F`:
  **F = 1 forces the partial to full weight, F = 0 leaves it** — this corrects round 24 §1.5 ("1 − F·(1 − B)"), whose
  reading missed the `SubCRev` on the stored map **[DECOMP `FUN_00842960` 0x842a10, `FUN_00843680` 0x843ad8…0x843b2a; EXEC:
  Clap (Forced pattern, Seed 1) −94.0 dB with this law, +0.3 dB with round 24's; Snare is insensitive because its
  B = 1]**.
- **Per-partial maps** (`FUN_0086e6c0`): sampled sequentially at `x_i + 2⁻²⁴` (x_i = i/32767, or log₂(i+1)/15 with byte
  `+0x213`), lengths M (Forced) / min(1.5·M, 32768) (others); y clamped to [0, 1]; the three EQs as `E = (3^{2y} − 1)/2`
  (exactly 1 for y = 0.5); optional ×1/√(i+1) (`+0x226`, Forced/Impulse and Decay with ALT); fixed pattern v (k = 13…16):
  v < 5 → positions `i mod (v+1) = v`, v ≥ 5 → positions with `i mod (v−2) ≠ 0` receive **0 (Forced, Impulse) or 1
  (Decay, Release)**; trailing repeated values are trimmed down to M entries; reads beyond the end use the last value;
  EQ shift s = `round(clamp(art₁₂, −0.5, 0.5)·M)` reads the maps at i − s **[DECOMP + EXEC]**.
- **Pre-decayed level:** `D = 0.01^{1/(B/P + 0.001)}`, P = max(pre-decay·art₆, 0.001); normalised so that its RMS over
  M is `g` (1, or `0.5/r` with byte `+0x225`, r = the larger RMS of the Impulse and — with ALT — Decay maps) (round 24).
- **Band limit:** only partials below `trunc(M·2^q·0.907029478/speed)` (≤ M), recomputed only at note start; **update
  span** = [first, last) of partials with Impulse > 2⁻²⁴ (ALT: with |Impulse − Decay| > 2⁻²⁴), intersected with the band
  (with EQ shift ≠ 0: all partials below the band) **[DECOMP]**.
- **Spectrum frames** X_k (complex, phases from the fixed seed-3 table (cos θ, sin θ)/√M, round 24) and the laws of round
  26 §1.3/§1.4 (decay `d_h = 0.01^{1/(T·B·E_dec + T₀)}`, ALT `X ← X + c(Y − X)`, release from the frozen current table
  with `d_rel,h`, ALT switched off at release) — all re-read and confirmed by EXEC this round.
- **Tables:** each frame is turned into an N-sample float32 table by an unscaled inverse real FFT (sine of partial h with
  amplitude 2|X_h|); the reader holds the two latest tables as interleaved (new, old) pairs with 64 guard pairs and a
  base offset of 32 pairs **[DECOMP `FUN_00843300`, `FUN_00842ee0`]**.
- **Reader:** positions `pos + i·step` (double), integer part (truncated) masked to N − 1, fraction as float;
  quality 0 = linear interpolation, qualities 1–3 = the **6-point, 5th-order Hermite interpolator ("x-form")** on samples
  −2…+3 — identified numerically: its 18 polynomial coefficients equal that published interpolator's to float precision
  **[DECOMP `FUN_00610250` + numerical check]**; quality 4 = §1.19.
- **Crossfade and pan:** within each 50/25 ms frame the new table's gain rises from 0 and the old one's falls to 0 by
  `±g/hop` per sample (float32 accumulation), g = the sub-voice's L/R gain; output L = new·g_L↑ + old·g_L↓, R likewise;
  the first sub-voice writes, the others add **[DECOMP `FUN_008446e0`, `FUN_008448b0/8f0`]**.
- **Read start (j₀):** at the first render the reader starts at `phase·N + j₀ + 2N − 32` where **j₀ = index of the
  smallest |x|² in the first new table** (X₁) **[DECOMP `FUN_00843220`, `FUN_00842e90`; EXEC]**. The minimum of |x|² is
  ill-conditioned: two near-zero samples often differ by less than the FFT's float rounding (e.g. 2.4049·10⁻⁵ vs
  2.4052·10⁻⁵ against a table RMS of 0.6 and a model/DLL table difference of 10⁻⁶), so an independent implementation
  picks a different start for some notes (1–6 of the 60 demo voices per battery run); the result is a time shift of the whole
  table (same spectrum, different phase) — see §1.21.
- **Unison** (P12 = n sub-voices, `TriggerVoice` + `FUN_00844680`): n = 1: gains 0.3/0.3, no detune, phase 0. n ≥ 2:
  `u = 2k/(n−1) − 1`, phase offset `(k/n − (n−1)/(2n))·σ` (float), **σ = (1 − (101^{1 − P16·art₅/128} − 1)·0.01)^{P52}**
  (integer power); gain `0.3` (n = 2) or `0.3·(v + 2(1 − |u|)(1 − v))/√n` (v = P14/128); pan `u·P13/128` through the
  cos/sin law **without** the √2 (L = gain·cos((1+pan)π/4), R = gain·sin(…)) **[DECOMP; EXEC: slot fields peeked]**.
- **Silence:** after each render call, if every output sample is ≤ 10⁻⁴ (−80 dB) a counter (start `round(0.1·fs')`) is
  decreased by the call length, otherwise reset; at ≤ 0 the voice goes to state 9 (§1.14) **[DECOMP `FUN_00844940`]**.

### 1.8 Timbre decay and release times **[DECOMP TriggerVoice, `FUN_0086c050`]**

- Decay: `t_dec·fs' = ((21^{P6/10⁴} − 1)·997.5·art₇ + 50)·0.001·fs'` (float steps), or "infinite" for P6 = 10000.
- Release (note-off, or when the volume envelope passes its decay point if byte `+0x20f` is set — then with velocity 0.5):
  `t_rel·fs' = ((21^{P7/10⁴} − 1)·997.5·art₈ + 50)·0.001·fs'`, art₈ = release-time articulator at the **release velocity**
  (host event 4, clamped to [0, 1]) and the X/Y tables.

### 1.9 Note-off **[DECOMP `FUN_0086c2a0`]**

State 5; timbre release (§1.8) unless it is tied to the volume envelope; release of the envelope and LFO of the first five
articulators.

### 1.10 Filter **[DECOMP `FUN_00867cc0`, `FUN_00624cc0`, `FUN_006256b0`, `FUN_006284e0`, `FUN_0062bf80`, `FUN_0062bfd0`; EXEC: −121…−129 dB on all types]**

- Per chunk, from u = P17/256 + art₁ and r = P18/256 + art₂ (FLAT: r ← r·(u + 1)/2): the round-21 laws (`c`, Q / band
  width, `ω = (c + c₀)·2π·s`, s = 44100/fs', ×0.5 when fs' < 88 200, clamped to 2.849 517 rad; Q^{1/order} for LP/HP; band
  width ×order (BP) or /order (band stop) × ln2/2); RBJ designs normalised by a₀ (type map 0 LP, 1 BP (constant skirt),
  2 notch, 3 HP), the same coefficients for every stage.
- **Rate:** when fs' < 88 200 Hz the voice signal is upsampled ×2 by sample repetition, filtered, and decimated by
  averaging pairs (0.5·(y₂ₖ + y₂ₖ₊₁)).
- **Biquad:** direct form 1 in double per sample, float32 output; after every call (one chunk) the 8 state values are
  rounded to float32 and values below 2⁻²⁴ zeroed.

### 1.11 Declick **[DECOMP voice render]**

With byte `+0x20e`: for the first `round(96·(fs'/44100)²)` samples each channel is smoothed by `y += g·(x − y)` with g
rising linearly from 0 by 1/len per sample (float32).

### 1.12 Volume, pan, gains **[DECOMP `FUN_00616910`, `FUN_0062b400`, `FUN_00629550`; EXEC]**

- Per control block: vol = `float(Final.Vol·master)·art₀` (master §1.13), pan = clamp(Final.Pan, −1, 1). Pan = 0 →
  gains (vol, vol, 0, 0); otherwise `L = cos((1+pan)π/4)·vol·√2`, `R = sin(…)·vol·√2` and the opposite channel is folded
  in: pan < 0 → `g_RL = L·(1 − R/vol)/2`, `g_LL = L·(1 − (1 − R/vol)/2)`, `g_RR = R`; pan > 0 symmetric. State ≥ 9 → 0.
- Each of the four gains ramps linearly over the block from its previous value, the per-sample step limited to
  `0.001·44100/fs'` (a ramp that would end below 2⁻²⁴ ends at 0); the first block starts at the target. Mixer:
  `out_L += x_L·g_LL + x_R·g_RL`, `out_R += x_R·g_RR + x_L·g_LR`, gains advanced by float32 additions.

### 1.13 Output level and tuning **[DECOMP `FUN_0086a570`]**

master = `(3^{P0/128} − 1)/2` (exactly 1 at P0 = 128); master pitch `100·P1 + P2` cents (round 21). The voice level is
Final.Vol × master × volume articulator (§1.12); unison gains (0.3·…) are inside the timbre (§1.7). The host's
ComputeLRVol is **not** used: Final.Pan/Final.Vol are applied by Ogun itself.

### 1.14 Voice end, polyphony, same key **[DECOMP `FUN_0086c6e0`, TriggerVoice, `FUN_0086c390`, `FUN_0077a420`; EXEC]**

- State 9 (silence, §1.7, or a cut) → gain targets 0 → when all four gains and steps are 0 (and also when every enabled
  envelope/LFO of articulators 0–4 has finished) state 10 → Ogun asks the host to kill the voice.
- **Max polyphony** = ProcessEvent 1 (`+0xe0`): at a new note, if the listed voices ≥ the limit and the voices still
  playing (state < 9) ≥ the limit, the **newest** playing voice is cut (state 9, envelopes/LFOs to stage 9) **[EXEC:
  limits 1 and 2, −130 dB]**.
- Byte `+0x209`: a new note cuts playing voices on the same (rounded) key.
- Effect tail: Gen_Render keeps the effects running while voices are listed and for `fs'·100/44100` samples (+ the chorus
  line length, §1.16) after the list becomes empty; the fake host never kills, so in the harness the effects never stop.

### 1.15 EQ (3 bands) **[DECOMP `FUN_0086a670`, `FUN_0086a800`, `FUN_00625290`, `FUN_00624cc0`, `FUN_0086d3a0`; EXEC: −79…−134 dB on the 20 demo channels with EQ only — the voice floor of each channel]**

- Band k (P40+k level, P43+k frequency, P46+k width; type from the state, 0 low shelf, 1 peaking, 2 high shelf → designer
  6/7/8): level dB = P·0.01; frequency `(101^{P/65536} − 1)·159.9 + 10` Hz (float), clamped to `0.4999·fs'`; width
  `w = P/65536`; `A = 10^{dB/40}`.
- Peaking: `α = sin ω / (2·((1001^w − 1)·0.0999 + 0.1))`, RBJ peaking with A. Shelves: `β = √((A² + 1)/(1.9·w + 0.1) − (A − 1)²)`,
  RBJ low/high shelf with the `β·sin ω` term.
- A band runs when its level ≠ 0 (a band switched to 0 keeps running for `fs/5` samples, then its state is cleared);
  biquad of §1.10 (state rounded to float per host block) on the summed voices, bands in order 1, 2, 3.

### 1.16 Chorus (P19…P25) **[DECOMP `FUN_0086a2d0`, `FUN_00840470`, `FUN_008405d0`, `FUN_00840650`, `FUN_00840690`, `FUN_00840d30`, `FUN_00840d90`, `FUN_00840dd0`, `FUN_00840fd0`, `FUN_00840e60`, `FUN_00840f10`; EXEC: −121…−130 dB]**

- **Labels (two corrected from round 12):** P19 order (number of tap pairs; < 1 = off), P20 depth d = P/1024, P21 speed
  s = P/1024, P22 delay x = P/128, **P23 = spread** y = P/128 (tap phase offsets and per-tap jitter blend), **P24 = cross**
  c = P/128 (signed: gain of each tap into the opposite channel), P25 mix (signed, m = |P|/128).
- Dry gain (plugin `+0x350`) `1 − (5^m − 1)/4`; wet `sign(P25)·(1 − (5^{1−m} − 1)/4)` (0 when P25 = 0 — Trumpet's chorus
  has mix 0 and is inaudible).
- Set-up (fsr = 44100/fs): A = (11^d − 1)·109.25, B = (11^x − 1)·441, r = 101^{1−s} − 1, K = 500·2³¹/(75000·r + 50000),
  sp = (1001^y − 1)/1000. 2·order taps; tap i < order reads the left input (t = i, start phase 0), the others the right
  input (t = 2·order − 1 − i, start phase 2³¹); w = 1 − t/order; jitter jt = a 20-entry constant table (values about
  0.97…1.07, two entries ≈ 0.10; in the model as data **[DATA `0x8e2b7c`]**), jj = sp·jt² + (1 − sp); per tap: base =
  (A·w + 10)/fsr, fixed delay q = max((sp·B·w·jj + (1 − sp)·B)/fsr, 8), centre = base + q, phase increment
  `round(fsr·(sp·K·jj/(A·w + 10) + (1 − sp)·K/(A + 10)))`, phase offset `round(w·sp·2³¹/256)·256`; gains 1 (own channel)
  and c·w (other), all scaled by `(1 − (1001^{1 − 1/Σ|g|} − 1)/1000)·wet`.
- Per sample: delay = centre + x·(S − |x|) with `x = |φ·S/2³¹| − S/2`, S = √(4·base) (a parabolic triangle between q and
  q + 2·base), linear interpolation in the ring (written before reading), each tap added to both outputs; x87 arithmetic
  with 64-bit precision and truncation (control word 0x0F3F). Output: `buf = buf·dry + taps`.
- Ring length = round(max(centre + base)) + 8 (+ block); this number is added to the effect tail. With the tail counter
  idle only the phases advance. Nothing random or time-seeded. The AVX path was not examined.

### 1.17 Delay (P26…P30) **[DECOMP `FUN_00869dd0`, `FUN_0083ceb0`, `FUN_0083cd50`/`FUN_0083ccf0`, `FUN_0083d1b0`, `FUN_0083d1f0`/`FUN_0083d1d0`, `FUN_0083d340`/`d280`, `FUN_0083d460`/`d3b0`, `FUN_0083d590`/`d4d0`, `FUN_0062b400`; EXEC: −108…−131 dB]**

- P26 feedback = P/128 (float); P27 time in ticks: `round(ticks·fs·0.3125/tempo)` samples when body byte 0x100 (tempo-
  based) is set, else `round(ticks·fs/384)` (a fixed 120 BPM grid), ≥ 1; P28 stereo offset:
  `k = float((1001^{|P|/512} − 1)·50)`, offset `round(k·fs/100000)` samples, applied to L when P28 ≤ 0, to R when > 0;
  P29 wet = `sign(v)·(11^{|v|} − 1)·0.1` (exactly 1 when |v| = 1), v = P/128; P30 mode 0 normal, 1 inverted (outputs
  swapped), 2 ping-pong (outputs swapped and feedback crossed); body byte 0x101 = on.
- Circular buffer of stereo frames; while the tail is active the input of each channel is added at the write pointer plus
  its offset; every sample the frame at the read pointer is read and cleared, sent (per mode) to the wet buffer and fed
  back × g at the write pointer (`delay` frames ahead); g ramps per host block towards the feedback target with the
  ±0.001/sample limit of §1.12; results below 2⁻²⁴ flushed. Output `buf += wet·temp`.
- Line length = host maximum block (`+0x544`, Dispatcher 3) × output oversampling + offset + delay frames; the output does
  not depend on it as long as host blocks do not exceed the declared maximum (the model takes `maxblock`, default 512)
  **[DECOMP + EXEC: render-quality runs failed (−21 dB) until the line included the ×2 factor]**.

### 1.18 Reverb (P31…P39) **[DECOMP Ogun `FUN_0086a0a0`, `FUN_00840040`, `FUN_0083f480`/`FUN_0083f220`, `FUN_0083f650`, `FUN_0083f7c0`, `FUN_0083f5d0`, `FUN_0083f840`, `FUN_00840220`, `FUN_00629d70`; disassembly of `Shared/Reverb_x64.dll`; EXEC: −79…−131 dB, the worst two equal the voice floor of High Hat/Clap]**

- **The reverb DSP is not in Ogun:** Ogun loads `Shared\Reverb_x64.dll` (SHA-256
  `9881ff2ccd74227e454a3d5f1912568f4336abe06e68f3693162aa4d251c969f`), creates an engine with its `CreateReverbInstance`
  and only computes the coefficients itself; the engine's per-sample routine (`0x180002980`) was read with objdump.
- Input = L + R (mono, no halving); output a stereo pair × wet (P39/256) added to the signal; float32, no modulation, no
  randomness.
- Engine: +10⁻³⁰ anti-denormal; one-pole high cut; first-order all-pass low cut (`(x − ap)·0.5`, skipped when off); four
  input diffuser all-passes in series; predelay line; a two-half "figure-eight" tank (each half: feedback line × dec,
  colour shelf, + predelay output, damping one-pole, all-pass, delay × dec, all-pass, delay), 7 output taps per channel
  with gains `(1 − dec)·k + dec` (k = 0.6, 0.25, 0.7, 0.5, 0.2).
- Laws (fs = internal rate): low cut P31 Hz (19 = off) → `c = (tan(fπ/fs) − 1)/(tan + 1)`; high cut P32 / damping P37
  (×100 Hz, 221 = off) → `b = 2 − cos(f·200π/fs)`, `c = 1 − (b − √(b² − 1))`; predelay P33 → `round(P·fs/384)` samples
  (tempo-based: `round(P·fs·0.3125/tempo)`); room P34 → R = trunc(fs·(P + 15)/115), line lengths `trunc(L_j·R/20000)`
  and taps `trunc(t·R/20000)` from two constant tables (12 line lengths, 2 × 7 taps; **[DATA]**, in `ogfx_reverb.py`);
  diffusion P35 → all-pass coefficients `exp(ln 0.001·(len_a + len_b)/(2fs)/(P/100·k))` (k = 0.07, 0.35, 0.51); decay P36
  → `dec = exp(ln 0.001·S/(P·fs/10))^{1/4}` (S = sum of the four tank lines); colour P38 → shelf from a second decay with
  k = 0.2/0.6/1/1.5/3.

### 1.19 Qualities 2–4: output oversampling, 32-point reader **[DECOMP `FUN_00841ca0`, `FUN_00841d30`, `FUN_00841e70`, `FUN_00841e10`, `FUN_00841a40`, `FUN_00841f40`, `FUN_006145c0`, `FUN_006144d0`, `FUN_00842de0`, `FUN_00611590`, `FUN_006112b0`, `FUN_00611310`; EXEC: DLL kernels called directly by `r37/run/og_os.exe`, plus whole-plugin renders]**

- At qualities 2–4 the whole plugin (voices and effects) runs at fs' = 2·fs and Gen_Render decimates by 2 at the end:
  per stereo channel, in double, a **polyphase IIR half-band**: even input samples through three first-order all-passes
  (`y = x₋₁ + c·(x − y₋₁)`, c = 0.14738, 0.48247, 0.88301), odd samples through three others (c = 0.03915, 0.30265,
  0.67462), output `0.5·(A + B)` as float; all-pass outputs below 2⁻²⁴ are zeroed after each block; if the input block and
  all stored outputs are below 10⁻⁶ the stage is skipped and the first half of the input is passed on. Response: flat to
  0.45·Nyquist(fs'), −3 dB at 0.5, ≤ −104 dB from 0.6 **[DECOMP + EXEC: bit-exact over 26 blocks]**.
- Quality 4 reader = 2N-tap windowed sinc with N = 32 (point table `0x8c0470` = 1, 3, 8, 12, 16, 32): tap p + k
  (k = −31…32) weighted by `½·sin(b(f−k))/(π(f−k))·(0.36 + w + 0.64w²)`, w = cos(π(f−k)/N),
  `b = π·min(1, 1/|step|)` (cut-off follows the read speed), f = fraction (≥ 10⁻²⁵); the DLL evaluates it with a cos/sin
  recurrence in float32 and an `rcpps` + Newton reciprocal (CPU-specific in the last bits) **[DECOMP + EXEC: bit-exact
  when fed the DLL's sines; `ogos.py` emulates it, `ogos_fast.py` evaluates the same weights in double (≤ 7·10⁻⁷)]**.
- Everything else scales with fs' (control block, hop, envelope rates, filter without the extra ZOH stage, effect
  lengths) **[EXEC: whole-plugin renders at qualities 2, 3 and 4 match the model, §1.21]**.

### 1.20 Sources of non-determinism and precision limits **[DECOMP + EXEC]**

- **Seed 2 = Random:** the per-note counter starts from the Delphi RTL random seed copied when the plugin is created
  (§1.7). The verification reads the counter from the DLL (`+0x244`) and gives it to the model; in LYNX the draw is
  arbitrary by design (same law, different spectra).
- **Read start j₀** (§1.7): ill-conditioned argmin; the verification detects the DLL's choice from the reader position
  after the first block (an integer offset) and gives it to the model when it differs from the model's own choice.
- **IPP `Exp_32f_A11`** (≈ 11-bit accuracy) in the fullness shaping: B differs from an exact exp by up to ≈ 4·10⁻⁴
  absolute where `q − q^{1−u}` cancels; with a small pre-decay (P ≈ 0.001) D = 0.01^{1/(B/P+0.001)} magnifies it
  (synthetic: Seed 1 + fullness 0.2 + pre-decay 0 → −52 dB; fullness ±0.5 with pre-decay 0.3 → −84/−89 dB; the demos
  → −79…−98 dB on the affected channels). The model uses an exact exp; the IPP error pattern was not modelled.
  **[EXEC `t_bfloor.py`: with the DLL's own B array (peeked after the Forced step) injected into the model, one note of
  Metal Drums, Metal Drums Click, Clap and High Hat goes from −89.0/−98.0/−93.8/−79.0 dB to −129.0/−132.2/−128.9/−130.3 dB —
  the whole residual of those channels is this rounding]**.
- Everything else (envelopes, LFO counters, effects) is deterministic; there is no time seeding in the effects.

### 1.21 Verification against the DLL **[EXEC + model]**

Relative error `20·log₁₀(‖model − DLL‖/‖DLL‖)` over the whole render (worst of the two channels), host block 512 split at
events, states loaded through the DLL's own SaveRestoreState. Unless stated, the model is given two things that FL draws
at run time: the Seed-2 counter (read from `+0x244`) and, when it differs from the model's own choice, the read start j₀
(detected from the reader position after the first block; §1.7, §1.20).

**Demo channels** (`r37/out/bat_voice_final.log`, `bat_full_final.log`, `bat_full_48k_final.log`,
`bat_full_render.log` + `bat_full_render_fx2.log`, `bat_full_ownj0_final.log`): the **20 Ogun channels** of the
installation's demo projects, every parameter as saved, three overlapping notes (C5 velocity 1 0–20000, G5 velocity 0.6
8000–26000, G4 velocity 0.95 16000–30000 samples), 1 s; "voice only" = chorus, delay, reverb and EQ switched off in the
state; "full" = everything as saved; "quality 4" = the DLL put in render mode (Dispatcher 1, flag 0x10) and the model at
quality 4.

| # | Channel | voice only, 44.1 kHz | **full, 44.1 kHz** | full, 48 kHz | full, quality 4 (render) | full, 44.1 kHz, model's own j₀ |
|---|---|---|---|---|---|---|
| 0 | 3.3.1 Metal Drums: Drum | -89.1 | **-89.1** | -88.8 | -88.1 | -0.5 |
| 1 | 3.3.1 Metal Drums: Click | -98.2 | **-98.1** | -98.2 | -97.6 | -98.1 |
| 2 | 3.3.2 Tibetan | -121.4 | **-121.4** | -121.4 | -121.7 | -121.4 |
| 3 | 3.3.3 Chase | -131.2 | **-130.5** | -130.3 | -126.0 | -130.3 |
| 4 | Bellish 11 | -134.1 | **-130.3** | -130.6 | -130.6 | -130.3 |
| 5 | High Chords | -128.3 | **-127.7** | -127.7 | -128.8 | -2.3 |
| 6 | Kick | -114.0 | **-114.0** | -116.3 | -100.6 | -114.0 |
| 7 | Clap | -94.0 | **-93.1** | -92.9 | -92.4 | -93.1 |
| 8 | High Hat | -79.3 | **-79.1** | -79.1 | -79.2 | -3.1 |
| 9 | HiHat | -127.5 | **-126.2** | -129.3 | -127.2 | -1.6 |
| 10 | Snare | -130.6 | **-128.5** | -128.3 | -129.3 | -128.5 |
| 11 | Swebass 1 | -131.3 | **-121.0** | -120.8 | -117.2 | -121.0 |
| 12 | Swebass 2 | -130.1 | **-125.6** | -124.5 | -120.8 | -125.6 |
| 13 | Midnight Lady | -111.1 | **-111.8** | -111.8 | -110.2 | -111.3 |
| 14 | Supersaw | -127.6 | **-127.1** | -123.1 | -127.8 | -127.1 |
| 15 | Airguitar 3 | -121.3 | **-121.0** | -120.9 | -120.2 | -121.0 |
| 16 | Trumpet | -129.0 | **-126.6** | -125.0 | -125.6 | -126.6 |
| 17 | Silverswirll | -132.4 | **-125.3** | -124.6 | -120.5 | -125.3 |
| 18 | Padsaw | -130.1 | **-129.6** | -129.2 | -126.1 | -129.6 |
| 19 | Turbular Bells 2 | -115.6 | **-115.2** | -115.0 | -115.5 | -115.2 |

- **Result: 20 of 20 demo channels at −60 dB or better** with everything on, at 44.1 kHz (median −123.4 dB, worst
  −79.1 dB "High Hat"), at 48 kHz (20/20, median −122.3 dB) and at render quality 4 (20/20, median −120.7 dB); voice only
  20/20 (median −127.6 dB). The four channels below −100 dB (Metal Drums −89, Clap −93, Metal Drums Click −98, High Hat
  −79) owe their whole residual to IPP `Exp_32f_A11` in the fullness shaping (§1.20: with the DLL's B injected they reach
  −129…−132 dB).
- **With the model's own read start** (no j₀ given): 16/20 at −60 dB or better; the 4 others (Metal Drums Drum, High
  Chords, High Hat, HiHat in this run — the set changes with the random Seed-2 counter) differ only by the start offset of
  one to three notes (a time shift of the table; the spectra and envelopes are identical).
- Chorus, delay and reverb alone on the channels that use them (`t_chorus.log`, `t_delay.log`, `t_reverb.log`):
  −121…−130, −108…−131 and −79…−131 dB (the reverb's worst cases equal the voice floor of High Hat/Clap); EQ only on all 20
  channels: −79…−135 dB (`bat_eq_v1.log`).

**Synthetic cases** (`bat_synth_final.log`, `t_lfo.log`, `t_misc.log`; voice only, 3 notes, the render-test default state
or a demo channel with one feature changed):

| Group | Cases (name: dB) | Worst |
|---|---|---|
| Richness, quality 0/1 | base_q1 −129.0; base_q0 −129.0; rich1 −129.0; rich3 −128.6; rich6 −125.8; rich11 −118.7 | −118.7 |
| Seeds, fullness, morph, pre-decay | seed1_seed2_morph −134.9; seed2_random −85.6; full+_seed −89.2; full−_seed −84.0; full+_seed_pre1000 −97.4; predecay −95.9; **seed1_perkey −52.5** | −52.5 (IPP A11, §1.20) |
| Decay / release | dec_inf −137.3; dec0 −138.5; rel0 −129.0; rel_max −129.3; timbre_rel_by_env_kick −115.8; timbre_rel_by_note_kick −117.0; relvel_demo16 −129.0 | −115.8 |
| ALT, normalisation, maps | alt_on_demo2 −120.0; alt_off_demo13 −107.9; norm_off_demo13 −110.2; isqrt_demo7 −84.1; isqrt_alt_demo19 −118.3; logx_toggle_demo2 −121.4; linx_toggle_demo19 −121.4; pattern_imp4_demo9 −126.5; pattern_dec6_rel7_demo9 −127.4 | −84.1 |
| Unison | uni2 −128.8; uni3_pan −127.7; uni6 −127.3; uni9 −130.7 | −127.3 |
| Filter | flt_lp1 −129.2; flt_bp3_flat −121.0; flt_bs2 −125.5; flt_hp3 −126.2 | −121.0 |
| Envelopes, velocity, portamento, key | vel_attack_kick −112.0; vel_pos_kick −110.3; declick_off_demo6 −114.0; nokeytrack_demo16 −131.2; porta_wrap_demo14 −127.4; porta_nolimit_demo14 −127.6; legato_flag_demo18_off −130.3; same_key_twice_demo6 −115.0 | −110.3 |
| Host values | master_level_pitch −130.2; host_pan_vol −127.0; host_pitchofs −128.3; host_fcut_fres_xy −79.9; modx_y_demo1 −97.1 | −79.9 |
| LFO (amount, speed, tension ±, skew, pulse width ±, unipolar, tempo, phase) | −127.5; −120.1; −117.7; −129.1; −124.9; −123.9; −126.0; −124.5; −125.3; −129.5 | −117.7 |
| Polyphony | max polyphony 2 −130.0; max polyphony 1 −130.4 (DLL vs unlimited: −5.6 / −1.6 dB) | −130.0 |
| Rates, qualities | demo11 at 48 kHz −130.9; demo19 at 96 kHz −115.6; Trumpet quality 2 −132.2, quality 3 −133.1, render (q4) −137.1; Bellish render −137.1; Turbular Bells quality 2 −115.8; Clap quality 3 −93.7 | −93.7 |

---

## Summary table (question → result)

| # | Item | Result |
|---|---|---|
| O1 | Engine | inverse-FFT tables (N = 2^(P52+q)) per 50/25 ms frame, read with linear / 6-point Hermite / 32-point kernels, linear crossfade between frames; partial h at h·f·2^{7−P52} (inharmonic for richness > 2) **[DECOMP+EXEC]** |
| O2 | Spectrum | seeds (mix, per key, Random counter from RTL RandSeed), fullness shaping (IPP Exp A11), morph, **Forced: B ← B(1−F) + F (corrects round 24)**, pre-decay D, RMS normalisation, band limit, update span, 4 maps with patterns/1/√h/log axis/EQ shift **[DECOMP+EXEC]** |
| O3 | Per-partial time laws | hold: `0.01^{1/(T·B·E_dec+T₀)}` per frame; ALT: `X += c(Y − X)`; release from the frozen current table with Release EQ, ALT off **[DECOMP+EXEC]** (rounds 24/26 confirmed) |
| O4 | Read start | j₀ = argmin |x|² of the first table; ill-conditioned (near-ties below FFT rounding) **[DECOMP+EXEC]** |
| O5 | Unison | phase σ = (1 − (101^{1−P16·a/128} − 1)/100)^{P52}, gains 0.3·…, pan law without √2, detune table, ratios as float **[DECOMP+EXEC]** |
| O6 | Articulators | static (key/velocity maps) ⊗ X/Y tables (+256·Final.FCut/FRes), per-block envelopes for 0–4, others fixed at note start; envelope engine with knobs, tempo rate, velocity options, portamento; LFO counter/wave **[DECOMP+EXEC]** |
| O7 | Filter | round-21 laws, RBJ designs, 2× ZOH oversampling below 88.2 kHz, per-chunk float rounding of the state **[DECOMP+EXEC]** |
| O8 | Gains / level | Final.Vol × (3^{P0/128} − 1)/2 × volume articulator; own pan matrix (no host ComputeLRVol); ramps limited to 0.001·44100/fs' per sample; declick **[DECOMP+EXEC]** |
| O9 | Voice end / polyphony | silence −80 dB for 0.1 s, envelopes ended, gains zero; same-key cut; max polyphony cuts the newest playing voice **[DECOMP+EXEC]** |
| O10 | Effects | EQ, chorus (labels P23/P24 corrected), delay, reverb (engine in `Reverb_x64.dll`) **[DECOMP+EXEC]** |
| O11 | Quality | table q0–q4 with output ×2 at 2–4; FL's render flag forces quality 4 **[DECOMP]** (§1.19 for the kernels) |
| O12 | Verification | **20/20 demo channels at −60 dB or better** (all effects on; 44.1 kHz median −123.4 dB, worst −79.1; also 20/20 at 48 kHz and at render quality 4); 16/20 with the model's own read start; ~70 synthetic cases −52…−138 dB (worst = IPP A11, §1.20) **[EXEC + model]** (§1.21) |

---

## 2. For LYNX — implementation guide

Reference implementation: `r37/py/ogmodel.py` (+ `ogcurve.py`, `ogpatch.py`, `ogfx_chorus.py`, `ogfx_delay.py`,
`ogfx_reverb.py`, `ogos.py`, `ogos_fast.py`) — port it 1:1. Calling it:

```python
import sys; sys.path.insert(0, '/home/kauan/.claude/jobs/f8789b49/tmp/r37/py')
import ogmodel
pl = ogmodel.Plugin(state_blob, fs=44100, quality=None, tempo=140.0, maxblock=512)  # 0xD5 plugin state
y, voices = pl.run(nsamples, [(0, 'on', dict(key=60, vel=1.0, fvol=1.0, fpan=0.0, pofs=0.0, fcut=0.0, fres=0.0)),
                              (22050, 'off', 0)])        # y: (n, 2) float64; 'off' takes the index of the note-on
```
(`quality=4` reproduces FL's export; `pl.maxpoly = n` = max polyphony; `pl.host_kills = True` = finished voices are
removed as FL does, so the effect tail stops; `pl.counter` = Seed-2 counter; `pl.j0_shift` = optional read-start
overrides used only by the verification.)

1. **Replace `synthFromOgun` and the "spectral oscillator" Ogun path** (8 snapshots, binned partials, shared decay,
   morphing ALT, one release envelope, neutral articulators, fixed Seed-2 draw, key C5, binned inharmonics) by a dedicated
   `OgunInstrument` (pattern of `SakuraInstrument` / the Harmless and Harmor instruments of rounds 32/34) that parses the
   whole state (§1.3; `ogpatch.py` reaches the exact end of all demo states) and keeps it in the channel; Autogun states use
   the same format (round 12) and can use the same instrument.
2. **Timbre engine per voice** (§1.7): IPP uniform generator, seed mix, fullness shaping, morph, Forced (new law), D,
   normalisation, band limit, update span, the four per-partial maps (sequential curve sampling, patterns, 1/√h, log axis,
   EQ shift); spectrum frames every `hop` with the hold / ALT / release laws; **inverse real FFT into an N-sample float
   table per frame** (N = 2^(P52+q), up to 262 144 at quality 4 with richness 11), two tables kept as (new, old) with
   64 guard samples; the reader with linear (q0), 6-point Hermite (q1–3) or the 32-point kernel of §1.19 (q4); linear
   crossfade and unison pan gains over each frame; read start at the table's smallest |x|². Rendering partials as sines
   instead of tables is equivalent only up to the reader's interpolation error — use the tables for exactness. Cost: one
   FFT of N per 50 ms per voice (25 ms at q3/q4).
3. **Articulators** (§1.4–1.5): the point-curve evaluator (shared with the Harmor port: same modes; keep Ogun's double
   precision), static values from key/velocity maps, 513-point X/Y tables indexed by `X/Y + 256·Final.FCut/FRes`, per-block
   envelopes (rates `2·B/fs'` or tempo-based `B/fs'·tempo/60`; the plugin needs the host tempo), knobs, velocity options,
   portamento, LFO (counter + wave + amount curve). LYNX must send the project tempo (ProcessEvent 0 equivalent).
4. **Voice** (§1.6–1.14): pitch/unison per block, filter (with the 2× zero-order-hold oversampling below 88.2 kHz and the
   per-chunk float rounding of the biquad state), declick, volume/pan matrix with ramp limit, silence end, same-key cut,
   max polyphony; **apply Final.Vol/Final.Pan inside the instrument and do not apply the channel volume/pan a second
   time** (Ogun does not use the host's ComputeLRVol). Final.Pitch, Final.Vol/Pan and Final.FCut/FRes are read every
   control block (pitch bends and channel automation act during the note).
5. **Plugin effects** in this order: EQ (§1.15), chorus (§1.16), delay (§1.17), reverb (§1.18; LYNX needs its own port of
   the reverb engine of `Reverb_x64.dll` as described, with the two constant tables), then the 2:1 decimator at qualities
   2–4 (§1.19). Effect tail: `fs'·100/44100` samples + chorus line after the last voice is gone.
6. **Quality:** use the state's P54 for playback; for offline export FL forces quality 4 (§1.2) — LYNX's export should do
   the same if it tells instruments it is rendering (log the choice otherwise).
7. **Random:** Seed 2 "Random" needs any per-instance counter; FL's starts from its RTL random seed — log "Seed-2 random
   spectra differ from FL by design". The read start j₀ will occasionally differ from FL (§1.7, a time shift of the table).
8. Remove from STATUS.md the Ogun approximations listed in the task (snapshots, binning, shared decay, ALT morph, overall
   release, read-start offset, neutral articulators, fixed Seed-2 draw, key C5 for Seed 1 per key, binned inharmonics)
   only after the C++ port matches `ogmodel.py` on the 20 demo channels (states as saved, 44.1 and 48 kHz) to about
   −60 dB with the model's own j₀ and Seed-2 counter fed in.

---

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| VMT `0x8621f8` slots 1, 3, 5, 6, 8–12 | plugin interface | DECOMP + DATA |
| `0x776040`, `0x867790…0x86790f` (constructor) | data files located through the host object (id 1020) | DECOMP + EXEC |
| `FUN_00864d00`, `FUN_008625b0`, `FUN_0078bae0`, `FUN_0078b3e0`, `FUN_00781bd0`, `FUN_00789ba0`, `FUN_0078a030`, `FUN_0078b1e0`, `FUN_00784e20` | complete state layout, curve header semantics, map normalisation | DECOMP + DATA (parser reaches the end of 20/20 states) |
| `FUN_008684e0`, `FUN_0086a570` | parameter laws, seeds, master level/tuning | DECOMP |
| `FUN_00866840`, `FUN_0086e620`, `FUN_00864800`, `FUN_00866510`, `FUN_00866460`, `FUN_00862770` (dispatcher id 1 flag 0x10 → `+0x5e6`) | quality table, rates, control block, envelope rates, rendering → quality 4 | DECOMP + EXEC |
| `FUN_0086a8e0` (TriggerVoice, `out/trig.asm`) | note number, static articulators (key/note/120, velocity in xmm7), X/Y indices, porta, velocity options, timbre start arguments, unison set-up, filter set-up, max polyphony, same-key cut, last pitch | DECOMP + EXEC |
| `FUN_0086c6e0` | per-block articulators, gains, pitch/unison updates, declick, filter call, mixing, voice end | DECOMP + EXEC |
| `FUN_0086c2a0`, `FUN_0086c050`, `FUN_0086c390`, `FUN_0077a420` | note-off, timbre release time (release velocity, art₈), voice cut, max polyphony event | DECOMP + EXEC |
| `FUN_00862610`, `FUN_0078b660`, `FUN_0078bb40`, `FUN_0078af50`, `FUN_0078ac70`, `FUN_0086d9e0` | articulator combination, static value, map tables | DECOMP + EXEC (static values of 20 demos to 10⁻⁶) |
| `FUN_00785940`, `FUN_007872f0`, `FUN_0060e7f0`, `FUN_00787170`, `FUN_00788a20` | curve coefficients, point value, evaluation | DECOMP + EXEC |
| `FUN_00787970`, `FUN_00787d80`, `FUN_00787bc0`, `FUN_00789cf0`, `FUN_00789dd0`, `FUN_00789c70`, `FUN_00789600`, `FUN_00789aa0`, `FUN_00789d30` | envelope time engine, knobs, release, offsets, velocity position | DECOMP + EXEC |
| `FUN_0078a0e0`, `FUN_0078a530`, `FUN_0078a5a0`, `FUN_0078a260`, `FUN_0060e880`, `FUN_0060ea40`, `FUN_0060eca0` | LFO | DECOMP + EXEC (`t_lfo.py`) |
| `FUN_008424b0`, `FUN_00842590`, `FUN_008426e0`, `FUN_00842960` (0x842a10 `SubCRev`), `FUN_008428c0`, `FUN_00842820` | generator: sizes, speed constant, phase table, maps (Forced stored as 1 − F), span, normalisation | DECOMP + EXEC |
| `FUN_00842ee0`, `FUN_00843220`, `FUN_00843300`, `FUN_00843680` (`out/d3680.asm` 0x843ad8…0x843b2a), `FUN_00843590`, `FUN_00843470`, `FUN_008447d0` | timbre voice: start, j₀, frames, spectrum, Forced law, decay/ALT/release, seeds, fullness | DECOMP + EXEC |
| `FUN_00844940`, `FUN_008446e0`, `FUN_008448b0`, `FUN_008448f0`, `FUN_00844660`, `FUN_00844680`, `FUN_00842b90`, `FUN_00842c60`, `FUN_00842de0`, `FUN_00611750`, `FUN_0060fd20`, `FUN_00610250`, `FUN_006145c0`/`FUN_006146a0` (kernel table `0x8bf930`, sizes `0x8c0470`) | render loop, crossfade, reader, kernels, silence counter | DECOMP + EXEC |
| `FUN_0086e6c0`, `FUN_008669f0`, `FUN_00866da0` (fill constant `0x86eab8`) | per-partial map sampling and patterns | DECOMP + EXEC |
| `FUN_00867cc0`, `FUN_00624cc0`, `FUN_006256b0`, `FUN_006284e0`, `FUN_0062bf80`/`FUN_0062cb00`, `FUN_0062bfd0` | filter laws, designs, biquad, state rounding, 2× ZOH + averaging | DECOMP + EXEC |
| `FUN_00616910`, `FUN_0062b4d0`, `FUN_0062b400`, `FUN_00629550`, `FUN_00627e80` (dispatch, flag `0x8ea390[0x16]` = 0 here) | volume/pan matrix, pan law, ramps, mixer, CPU path | DECOMP + EXEC |
| `FUN_004660b0`, `FUN_0040c3e0`, `FUN_0040c4d0`, `FUN_00409fa0`/`FUN_00409ff0` (RandSeed `0x88e010`) | integer power, round, trunc, RTL random seed | DECOMP |
| `FUN_0086a670`, `FUN_0086a800`, `FUN_00625290`, `FUN_0086d3a0` | EQ | DECOMP + EXEC |
| chorus / delay / reverb functions of §1.16–1.18; `Shared/Reverb_x64.dll` `0x180002980`, `0x180003fa0` | effects | DECOMP + disassembly + EXEC |
| `r37/run/og.exe` + `cmp.py`, `bat_voice.py`, `bat_synth.py`, `t_lfo.py`, `t_chorus.py`, `t_delay.py`, `t_reverb.py`, `t_os.py` (`run/og_os.exe`), `t_misc.py`, `t_bfloor.py` (DLL's B injected) | §1.21, §1.20 | EXEC + model |
| `r14/blobs14.pkl` (20 demo channels), `out/survey2.txt` | demo states, §0 | DATA |

### Open questions

- IPP `Exp_32f_A11` error pattern in the fullness shaping (§1.20) — not modelled; it limits synthetic cases with fullness
  ≠ 0 and pre-decay ≈ 0 to about −52 dB (demos ≥ −79 dB).
- The read start j₀ cannot be reproduced bit-exactly without the IPP FFT's rounding (§1.7); a time shift when it differs.
- Global LFOs and global envelopes (curve header flag 2; state copied from a plugin-level record) — not modelled, unused in
  the demos.
- Host answers: Voice_ProcessEvent 2 (the legato/portamento test compares it with 14) and 6 (asked when byte `+0x20a` is
  0, value unused) — FL's values not observed (harness: 0); release velocity (event 4) was exercised with harness values.
- FL's process-mode flags during export (bit 0x10 "rendering" → quality 4) were read in the code but not observed from FL
  itself.
- Parameter changes during playback (effects re-allocation, chorus phase reset, reverb room change zeroing the lines) and
  the Reset dispatcher (id 2: clears delay/reverb/chorus/EQ, tail, last pitch) are not modelled.
- `rcpps` (quality-4 reader) and the IPP double sine are CPU/library-specific in the last float bits (§1.19).
- Autogun shares the state format (round 12) but its own DLL was not run in this round.
- The chorus and delay AVX paths (selected when `0x8ea390[0x16]` ≠ 0) were not examined (this machine uses the non-AVX
  paths); their arithmetic order may differ in the last bits.
