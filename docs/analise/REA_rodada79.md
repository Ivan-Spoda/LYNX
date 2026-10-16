# Behavioural analysis of FL Studio 2026: round 79 (REA) — FL Slayer's host side: MIDI levels, automation, grid phase, noise seed

> **Nature of this document.** Follow-up of round 68 (FL Slayer, `REA_rodada68.md`), whose §9 listed what LYNX did not
> reproduce. *Clean-room* reverse engineering, behavioural only: no Image-Line or reFX code or pseudocode was copied;
> what is here are behaviours, formulas and data layouts in our own words. Addresses (`FUN_xxxxxxxx`, `0x…`) only trace
> the evidence.
>
> **Rule: no speculation.** Markers as in round 68: **[DECOMP]** (decompiled/disassembled code), **[DATA]**,
> **[EXEC]** (the installation's own DLL run unmodified), **[model]** (LYNX's engine), **not confirmed**, and
> **inference** for conclusions drawn from the code without a run.

- **Date:** 2026-10-16. Scratch directory: `/home/kauan/.claude/jobs/f8789b49/tmp/r79/` (paths below are relative to it).
- **Targets:** the same binaries as round 68 — `FL Slayer_x64.dll` (front-end, SHA-256 `2acdf8e9…9d79963`),
  `Slayer_Engine.dll` (32-bit engine, `84b64c43…24a9`), `System/Tools/Bridge/32bit/ilbridge.exe` (`4060d725…6355`) —
  and `FLEngine_x64.dll` (`6d9c5ab0…8e02`, the host; disassembly from round 69, `../r69/gh/fle.asm`).
- **Tools:** REA (Ghidra 12.1.4 provider) on the front-end and the bridge (`procedure_pseudo_code`,
  `batch_decompile`, `procedure_callers`, `search_strings`); `objdump -M intel` for the host, the front-end
  (`fe.s`) and the engine (`eng.s`); `py/pe.py`, `py/vt.py` (constants, VMT slots).
- **EXEC:** round 68's harness `../r68/run/sv.exe` (the installation's engine, 32-bit) driven by `py/autom.py` against
  `build-local/slayerrender` (LYNX's engine) with parameter changes and pitch bends during notes.

---

## 1. Summary

| §9 item of round 68 | Finding | LYNX now |
|---|---|---|
| MIDI pan / volume / pitch events | FL **does** send them to FL Slayer (its info flags carry 0x4000); they are the plugin's only channel volume / pan, and the channel pitch becomes a pitch bend | reproduced (§2) |
| parameter automation; edits restart the engine | ProcessParam → `setParameter` on the running engine (automation through REC_FromMIDI) | reproduced (§3), EXEC-verified |
| 5 ms grid phase | FL does **not** align the grid with the song: its phase is the number of samples the engine has processed since it was created in the bridge | not reproducible from the project (§4) |
| per-bridge-thread `rand` seed | every FL Slayer instance runs in its **own** bridge process; the seed is per engine, as in LYNX | already equal (§4.3) |
| 12 ASan-only test mismatches | GCC, not the sanitizers: GCC narrows long double→float stores into SSE operations that ignore the x87 control word | fixed for the ASan build (§5) |

## 2. The channel's levels reach FL Slayer as MIDI events **[DECOMP]**

### 2.1 Which plugins get them

FL Slayer's `TFruityPlugInfo` (front-end `0x8c765c`): SDK version 1, 29 parameters, flags **0x00604011** [DATA]. The
host copies `Info.Flags & 0x4000` into the channel's byte `+0x6ec` (`0x12bd708`, round 46 §5.2). Every channel level
updater tests that byte:

- **Volume** (`0xfe9d20`): the channel gain `+0x19c = g(V/12800)·g(W/12800)` (round 28 §4.1); with the byte set it then
  calls the plugin's ProcessEvent **3** with value `min(MulDiv(V, W, 1 280 000), 127)` and **flags = the gain as float
  bits** (times a parent-layer factor from `0xfe8ec0` when the channel belongs to a layer).
- **Pan** (`0xfe9bf0`): `p = pan + pan offset − 6400`; with the byte set ProcessEvent **2** with value
  `clamp(p/100 + 64, 0, 127)` and **flags = clamp(p/100, −64, 64)** (`/100` by multiply-shift, truncating toward zero);
  the float pan `+0x198` is stored either way.
- **Pitch** (`0xfe9fc0`): with the byte set ProcessEvent **4** with value `Round(pitch + fine)` (plus the global pitch
  when `+0x28f` is set) and the channel's voice pitch offset `+0x1a0` keeps only `−100·root`; without the byte the whole
  sum goes into `+0x1a0`, i.e. into every voice's Final.Pitch (round 28 §4.1).
- All three are pushed together by `0xfea1e0` (pan, volume, …), called among others at the end of the channel defaults
  (`0xfe9426`) and from the project loader path (`0xf6c9b1`); the volume updater is also called on knob changes.

### 2.2 What the front-end does with them

ProcessEvent (`0x8533d0`, from the disassembly; the decompiler typed the integer arguments as floats):
- event 0: the event value's bits as a float → tempo (double) → the bridged plugin;
- event **2**: the **flags** (integer pan −64…64) → `+0x158`, then the output gains are recomputed;
- event **3**: the **flags** read as a float × **2.0** (`0x8c76fc`) → `+0x15c`, then the output gains;
- event **4**: the value (cents) → pitch bend `MulDiv(cents, 8192, 1200) + 8192` (`0x5d5b80`: 64-bit multiply, signed
  divide truncating toward zero), 0 when below 1, at most 0x3fff, sent at once as MIDI `0xE0`.

The output gains (`FUN_00853170`): host slot 3 `ComputeLRVol(&L, &R, pan, volume)` — the integer-pan version (pan/64,
FL's law A `L = vol·cos((1 + pan)·π/4)`, `R = vol·sin(…)`, rounds 28/30). The constructor starts from pan 0 and volume
1.0, so before the host's first events the gains are cos(π/4) (round 68 §6.3).

### 2.3 FL does not scale the plugin's output by the channel volume

The channel gain `+0x19c` has exactly four float readers in FLEngine (`grep` over the full disassembly): the voice
FinalLevels builder (`0xd265fc`, `0xd2660e`), the layer factor (`0xfe8efa`), a ComputeLRVol for the channel's
`+0x358` object (`0xfe8fa2`) and the ProcessEvent 3 sender above. None of them scales a generator's rendered output.
So for FL Slayer, which reads no voice levels, the channel volume and pan act **only** through the front-end's gains
**[DECOMP; the absence of another multiplication is an inference from that complete reader list]**.

Consequence: at FL's default channel volume (10000/12800 → gain 0.5510) FL Slayer's output is
`engine × 2·0.5510·cos(π/4)` = **engine × 0.779**, where LYNX used `engine × 0.5510·cos(π/4)` (half: −6.02 dB).

### 2.4 LYNX **[model]**

`SlayerInstrument::render` recovers the channel gain and pan from LYNX's channel gains (as the Kepler port does),
converts the pan to FL's integer MIDI pan (`slayer::midiPan`) and applies `slayer::hostGains` (= `flvp::panLaw` of
pan/64 and twice the gain) instead of LYNX's own pan law. The channel pitch minus the root-note shift (kept by the
importer in `ChannelDef::modelParams[0]`) is sent as the front-end's pitch bend (`slayer::pitchBend`) whenever it
changes; the root shift stays in the key. Residual: LYNX's master pitch still reaches FL Slayer through the notes'
cents (FL: through the bend when `+0x28f` is set) — **not reproduced**.

## 3. Parameter automation and settings edits **[DECOMP + EXEC]**

- ProcessParam (`FUN_008534e0`): with **REC_FromMIDI** (0x20) the value is `min + Round(value · 2⁻³⁰ · (max − min))`
  (`0x853650` = 2⁻³⁰); with **REC_UpdateValue** (1) it is stored and, for the 28 engine parameters, sent as
  `setParameter(i, value/max)` (single-precision division) — for parameter 28 as MIDI CC 1. The bridge queues the call
  (`FUN_0075e650` → `FUN_00747550`); the engine only stores the value (and the latch flag), every chunk reads it.
- So FL never restarts the engine on a parameter change. That FL's automation clips reach ProcessParam through the
  REC_FromMIDI path with a value scaled to 2³⁰ is an **inference** (the FL-side call was not traced); LYNX applies
  `Round(value·max)` (round half to even, Delphi's `Round`).
- **EXEC** (`py/autom.py`, log `out/autom1.log`): random presets, 1–3 notes, 1–6 parameter changes (any of the 29,
  including mode, effect, amp and the mod wheel) and an optional pitch bend at random samples, 44.1/48 kHz, host
  blocks 64…1000: the installation's engine (`sv.exe`, `setParameter` before the block's MIDI) against LYNX's engine
  (`slayerrender param=`): see §6.
- **LYNX [model]:** `SlayerInstrument::setParameter` (automation, audio thread) and `configure` with a changed state of
  the same DLL (settings panel; message thread, handed over through atomics) send only the integers that changed; a new
  DLL or sample rate still rebuilds the engine. `RackRenderer` now binds channel-parameter automation to FL Slayer
  channels too.

## 4. The control grid and the noise seed

### 4.1 Nothing re-aligns the grid **[DECOMP]**

- The engine's chunk counter (`+0x821764`) is written only by the constructor tail (`0x10008298`, `FUN_100081b0`) and
  by `processReplacing` itself (`0x100089d8`, `0x10008e1f`): neither resume, sample rate, block size nor any MIDI
  message resets it.
- The front-end loads the engine only in its constructor (`FUN_008529c0`, whose only caller is `FUN_008521e0`); Flush
  (Dispatcher 2) is a bridge flush (`0x75c5e0`) plus CC 123, SetEnabled (`0x75e540`) only updates the bridge's
  processing info.
- Each `processReplacing` gets exactly FL's block (`FUN_0075dc90` passes FL's length to the bridge), and only while the
  bridge connection is up (`+0x7dc`).

So the grid's phase at any song position is the number of samples the engine has processed since the bridge created
it — including everything FL rendered while the project was open (FL keeps calling generators while the device runs)
— modulo the 5 ms chunk. **It is not a function of the project**, and two FL exports of the same project can place
the grid differently (**inference** from the above; not run in FL). LYNX keeps its deterministic choice (grid starting
one sample before the render, round 68 §9).

### 4.2 Note timing inside that grid

The bridged MIDI carries deltaFrames 0 (round 68), so a note starts at the first grid boundary at or after the start of
the FL block that carries it; LYNX splits its blocks at the note's sample, as FL does for TriggerVoice — unchanged.

### 4.3 The `rand` seed **[DECOMP]**

- The engine's `rand` (`0x1000a2e5`) is the statically linked MSVC runtime's: `_holdrand` in the per-thread data block
  (`+0x14`), LCG `·0x343fd + 0x269ec3`, initial value 1 per thread.
- Each bridged plugin launches its **own** bridge process: `FUN_0075d440` builds the command line
  `-wrapper -address "<address>" -processid <FL's pid>` (`FUN_007308c0`) and `FUN_00730c40` → `FUN_0072df80` calls
  `CreateProcessW` unconditionally. One FL Slayer = one `ilbridge.exe` = one engine in that process, so no other engine
  shares its runtime's `rand` state.
- Inside the bridge the plugin's calls run in a long-lived message loop thread (`TBridgeWrapperProcessThread`, loop at
  `0x65ff30`: wait, dispatch, repeat) — that the audio calls always run on that one thread is an **inference**.

So the seed is per engine, starting at 1 — what LYNX does. As with the grid, every noise sample the engine produced
before (notes played while the project was open) advances the sequence in FL; an export right after loading starts
at 1 like LYNX's.

## 5. The 12 ASan-only mismatches **[model]**

Round 68's ASan run (`../r68/asan/run2.log`: 12 Slayer failures, "energy … differs from FL's DLL") was built with
**g++** (`-O1 -fsanitize=address,undefined`); the normal builds use Zig's Clang. Bisect (`asan/`): every g++ build at
`-O1`/`-O2` differs from the Clang one, with or without the sanitizers, `-O0` does not; disabling `-finline` alone
removes it, and of the inline helpers only `F` (long double → float store) matters. GCC folds patterns such as
`(float)((long double) a · (long double) b)` of two floats into a single-precision SSE operation, which rounds with
MXCSR (to nearest) instead of the engine's truncating x87 mode (`-frounding-math` does not prevent it). Clang under
`#pragma STDC FENV_ACCESS ON` does not.

Fix: for GCC only, `F`/`D` store through `fstps`/`fstpl` in inline assembly. With it the g++ ASan build is
bit-identical to the Clang build on the 12 golden presets. Plain g++ `-O1`/`-O2` builds still differ on one or two
presets (`ELEKTRO Jungle` from sample 32 944: a string's loop filter in release) — another GCC code motion across the
control-word switches, **not traced**; LYNX's shipped and tested builds are Clang.

## 6. Verification

(filled in below as the runs finish)

## 7. What LYNX still does not reproduce

- The grid phase and the noise sequence's position, which in FL depend on the engine's history since it was loaded
  (§4) — not a property of the project.
- The master pitch's route (§2.4); a channel in a layer (the parent factor of ProcessEvent 3, `0xfe8ec0`).
- That FL's automation reaches ProcessParam through REC_FromMIDI scaled to 2³⁰ (§3) — inference.

## Evidence index

| Item | Where | Kind |
|---|---|---|
| plugin info flags 0x00604011 | front-end `0x8c765c` | DATA |
| MIDI flag byte, level updaters, ProcessEvent 2/3/4 values | FLEngine `0x12bd708`, `0xfe9bf0`, `0xfe9d20`, `0xfe9fc0`, `0xfea1e0` | DECOMP |
| readers of the channel gain | FLEngine `0xd265fc`, `0xd2660e`, `0xfe8efa`, `0xfe8fa2`, `0xfe9e75` | DECOMP |
| front-end ProcessEvent, gains, MulDiv | `0x8533d0`, `FUN_00853170`, `0x5d5b80`, `0x8c76fc` | DECOMP |
| ProcessParam, REC_FromMIDI scale | `FUN_008534e0`, `0x853650` | DECOMP |
| engine counter writes | engine `0x10008298`, `0x100089d8`, `0x10008e1f` | DECOMP |
| engine load / bridge calls | `FUN_008529c0` (caller `FUN_008521e0`), VMT `0x75b1c8`, `0x75c5e0`, `0x75e540`, `FUN_0075dc90` | DECOMP |
| one bridge process per instance | `FUN_0075d440`, `FUN_007308c0`, `FUN_00730c40`, `FUN_0072df80` (`CreateProcessW`) | DECOMP |
| `rand` per thread | engine `0x1000a2e5` | DECOMP |
| bridge message loop | `ilbridge.exe` `0x65ff30` | DECOMP |
| automation during notes | `py/autom.py`, `out/autom1.log` | EXEC + model |
| GCC narrowing | `asan/` bisect | model |

## Where I stopped / next steps

- (updated below)
