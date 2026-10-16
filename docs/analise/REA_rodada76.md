# Behavioural analysis of FL Studio 2026: round 76 (REA) — the native sampler channel: velocity and pan laws, channel filter, envelope tick phase

> **Nature of this document.** Continuation of rounds 69 (§2–§3: the sampler voice and its articulators) and 72 (§9:
> where the per-voice levels are computed, where the channel filter starts). *Clean-room* reverse engineering,
> behavioural only: data layouts, formulas and behaviours in our own words; addresses (`FUN_xxxxxxxx`) and offsets only
> trace the evidence. No Image-Line code, audio or wave data was copied. No FLEX pack or encrypted content was touched.
>
> **No speculation.** **[DECOMP]** decompiled/disassembled code, **[DATA]** the installation's projects, **[EXEC]**
> the installation's code run unmodified, **[model]** our implementation (only DECOMP/EXEC facts), **not confirmed**
> where the evidence is missing.

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `~/.claude/jobs/f8789b49/tmp/r76/`.
- **Target:** `FLEngine_x64.dll`, SHA-256 `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` (the
  same build as rounds 69/72).
- **Tools:** Ghidra 12.1.4 headless on a copy of round 69's analysed project (`~/ghproj/r76`, program `flea`), our
  scripts `gh/scripts/DecAt.java` (decompile at addresses) and `gh/scripts/Xr.java` (references to addresses, with the
  containing function and instruction); `objdump -M intel` (`gh/all.asm`) for instruction-level checks and for
  searching writes to structure offsets; `py/pe.py` / `py/findptr.py` read constants and pointer slots. The REA MCP
  server was tried first, but its single Ghidra session is shared with a sibling round working on another binary, so
  this round used the headless project instead. Decompilations: `gh/d1.c` … `gh/d15.c`.

---

## 1. Note → initial levels **[DECOMP `FUN_00d29aa0`, `FUN_00d29bd0`; constants `0x13e755c/64/6c`, `0x15f49ec`]**

When the sequencer turns a pattern/playlist note into a voice request (`FUN_00ff56d0` → `FUN_00d29bd0` +
`FUN_00d29aa0` → `FUN_00d293d0`), the note's bytes become the voice's *initial levels* (5 floats: pan, volume, pitch,
filter X, filter Y — the same order as the plugins' `InitLevels`):

| Level | From the note | Law |
|---|---|---|
| pan | pan byte `p` (0…128, 64 = centre) | `(p − 64) · (1/64)` |
| volume | velocity byte `v` (0…128, 100 default) | `x = v · 0.01f`; `x == 1` → `1`, else `float((exp(double(x · ln11f)) − 1) · 0.1f)` |
| pitch | key | `key · 100` cents (fine pitch separately: `byte · 10 − 1200`) |
| filter X / Y | mod X / mod Y bytes (128 = centre) | `(b − 128) · (1/128)` |

`ln11f` is `float(ln 11)` computed at start-up (`FUN_006a6890`). So a note of velocity 100 has an initial volume of
exactly 1, velocity 128 gives `(11^1.28 − 1)/10 ≈ 2.15`, velocity 64 gives `(11^0.64 − 1)/10 ≈ 0.364` — the same law
as the channel volume knob (§2) and as the plugins' `InitLevels.Vol`. This answers round 72 §9's open item (the
scripting API's `0xd59d70` is the channel-volume getter, which uses the same law on `param/10000/1.28`).

## 2. Channel levels **[DECOMP `FUN_00fe9bf0`, `FUN_00fe9d20`, `FUN_00fe9fc0`, `FUN_00fea140`]**

The channel object keeps its levels at `+0x198 … +0x1a8` (pan, volume, pitch, filter X, filter Y):

- pan = `(pan param + pan offset − 6400) · (1/6400)` (param 0…12800);
- volume = `law(vol param / 12800) · law(second volume param / 12800)` with `law(x) = x == 1 ? 1 :
  float((exp(double(x·ln11f)) − 1)·0.1f)` — the knob law LYNX already uses (`channelGain`, 0.78 = param 10000);
- filter X / Y = `(param + offset) · (1/256)` (cutoff 256 = fully open, resonance 0), for channel kinds < 2;
- pitch = the channel's pitch knob in cents (+ the global pitch when "main pitch" is on).

The second volume parameter and the offsets are 0 / neutral in a plain channel (**not confirmed** which FLP event, if
any, sets them; LYNX ignores them).

## 3. Final levels and the pan law **[DECOMP `FUN_00d265e0`, `FUN_00d24ae0`, `FUN_006a3a00`, `FUN_006a3990`, `FUN_006a39c0`, `FUN_006a6890`]**

Per voice (`FUN_00d265e0`, at creation and whenever the channel or a slide changes them):

- Final.Pan = Init.Pan + channel pan, Final.Vol = Init.Vol · channel volume (the "linked" byte `+0x240` is set to 1 by
  the voice reset `0xd23687`; only the plugin voice API writes it otherwise), Final.Pitch / FCut / FRes = Init + channel;
  a non-zero per-note pan override replaces the pan clamped to [−1, 1]; the velocity/release cross-modulation table
  (`+0x6ed`) and a layer parent add on top (rare; **not ported**).

Per render block (`FUN_00d24ae0`): target L = Final.Vol · (volume articulator · (1 + LFO·amount)) and the pan law of
`(Final.Pan + pan articulator, target)` gives the target gains. The pan law is a function pointer chosen at start-up
by the "pan law" setting (`0x15f49a4`):

- **mode ≠ 1 (the default):** angle `t = (1 + pan)·π/4` (extended precision), `L = cos t · vol`, `R = sin t · vol` —
  the x87 routine `FUN_006a3990` (the SSE variant `FUN_006a6040` is only installed when the CPU flag
  `PTR_DAT_015e4c78[0x16]` is set, which it is not on this machine — round 47 §2.2 measured the x87 bits through the
  plugins' copy). This is exactly `flvp::panLaw` (LYNX, `core/FlVoiceProcessor.h`): **centre = −3 dB, hard side = 0 dB.**
- **mode 1:** `vol' = vol · sin(π/4)`; pan ≥ 0: `R = vol'`, `L = (1 − pan)·vol'`; pan < 0: `L = vol'`, `R = (1 + pan)·vol'`
  (`FUN_006a39c0`).

Which FLP value selects mode 1 is **not confirmed** (the setter `FUN_00f40840` is the settings dialog).

## 4. Channel filter **[DECOMP `FUN_00d24650`, `FUN_00d247a0`, `FUN_00981df0/c50/f20`, `FUN_009815f0`, `FUN_006a05d0`, `FUN_00d277d0`, table `0x1510e40`]**

The voice's filter kind and second-stage flag come from the channel's filter type through the table at `0x1510e40`:
type 0 → fast LP, 1 → 12 dB LP, 2 → band pass, 3 → high pass, 4 → band stop, 5 → LP ×2 (24 dB), 6 → SVF LP, 7 → SVF LP ×2
(type 8 → a kind the voice never renders). It is the same table, the same three coefficient routines (`ln 601` /
`ln 51` / `ln 26` laws, the `44100/fs` ratio `0x13ec894` written by `FUN_006a0230`) and the same render kernels as the
plugins' voice processor of rounds 42/47 (`flvp::filter`, LYNX `core/FlVoiceProcessor.h`), with:

- input X = Final.FCut + cutoff articulator, Y = Final.FRes + resonance articulator (channel `param/256` + note mod);
- the fast LP (kind 0) is active only once its coefficient differs from 1 or its feedback from 0; its coefficient
  ramps per sample from the previous tick's value with a slope over `round(samples per tick)` limited to
  `0.001 · 44100/fs` (`FUN_006a3810`), the first block starting at the new value;
- the second biquad pass scales the voice's target gains by `1 − q·(3.2 − f)·0.046875`; the second SVF pass uses
  `q + (1 − q)/4`.

## 5. Tick phase **[DECOMP `FUN_00d0ece0`, `FUN_00d0ead0`, `FUN_011e7a10`, `FUN_01311510`]**

FLEngine's sequencer splits every buffer at tick boundaries. A double accumulator `A` (`0x196ec68`) holds the next
tick's position in samples; the next tick is `round(A)` (`0x196ec70`); at a tick the flag `0x196ecc0` is set, the
tick's events (notes) are dispatched, `A += samples per tick` (`0x15d8980`, a double) and the voices render the
sub-block up to the next tick (or the buffer end; the block length is `0x196ebe4`). When playback starts at a whole
tick, `A` = the start sample (so the first tick is at once). Hence:

- a note's voice is created at a tick and its first render block is that tick's block (it steps its articulators at
  once, `FUN_00d24ae0`);
- the k-th following tick falls at `round(start + Σ samples-per-tick)` (repeated double addition), i.e. ticks are
  **song-aligned**, not counted from the note;
- each tick recomputes the slope; the gains keep moving by that slope until the next tick (they are not clamped at
  the target; the tick intervals alternate between ⌊spt⌉ ± 1 while the slope divides by `round(spt)`).

---

## Where I stopped / next steps

- Port: sampler voice levels (velocity law, flvp pan law with channel pan/volume inside the voice), the flvp channel
  filter, song-aligned ticks.
- FL reference renders (FL64.exe command-line render) — being tried.
