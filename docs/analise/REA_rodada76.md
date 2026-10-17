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
  Final.Pitch also adds the note's fine pitch (`+0x1e0`); an extra pan offset from the voice request (`+0x25c`, 0 for
  sequencer notes) is added and clamped to [−1, 1]; the velocity/release cross-modulation table (`+0x6ed`) and a layer
  parent add on top (rare; **not ported**).

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
  the target): the slope divides by `round(spt)` while the actual tick intervals are `round(A_{k+1}) − round(A_k)`.
- the same rule places the note events: a note on project tick k sounds from sample `round(k · spt)`, ties to even.

---

## 6. FL reference renders **[EXEC: `FL64.exe /R /Ewav <project>`]**

FL Studio renders a project from the command line (`FL64.exe /R /Ewav C:\...\x.flp` writes `x.wav` next to the
project and exits; 16-bit PCM, 44.1 kHz, no dither noise in silence, scale ×32767). Scripts: `fl.sh` (FL render),
`rl.sh` (LYNX `renderwav` of the same project, float), `py/flpw.py` (our FLP event reader/writer), `py/mk1.py` /
`py/mk2.py` (crafted projects from FL's own render test `Data/System/Render tests/Sampler.flp`: its sample path
replaced by our synthetic WAVs — a 441 Hz sine, an LCG noise — and its notes, 0xDB levels and 0xDA records patched),
`py/an1.py` (per-note L/R gain fit), `py/cmp.py` / `py/cmpn.py` (sample comparison in int16 codes, per note with lag).

| Project | What it exercises | Result (LYNX vs FL) |
|---|---|---|
| `t1` | 14 notes: velocities 1…128, note pans 0…128, default channel | gains within 0.0003 dB (the ×32767 scale); all samples ±1 code |
| `f0a`, `f0b` | fast LP (type 0), with and without resonance, three notes (velocity/pan) | ±1 code |
| `f1` … `f7` | 12 dB LP, BP, HP, band stop, LP×2, SVF, SVF×2 | ±1 code |
| `e1`, `e4` | volume envelope (delay/attack/hold/decay/sustain/release), notes of 47…49 ticks | ±1 code |
| `e2`, `e5` | cutoff envelope + volume LFO with the volume envelope **off** | ±1 code (see the note-off rule) |
| `e6` | 12 dB LP, volume + cutoff + **pitch** (600 cents) envelopes and their releases | ±1 code |
| `e7` | SVF, resonance envelope (negative amount), pan LFO, volume envelope with delay and LFO | ±1 code |

(The only other differences are in the last ~230 frames of each file, where LYNX's `renderwav` starts the pattern
again at the song end and FL does not — a property of the crafted projects' playlist, not of the voice.)

Behaviours found only through the renders (**[EXEC]**):

- **Sample end:** past the region's last frame the voice holds that frame and fades it out with the 441-entry end
  table `0.5·(1 + cos((i + 1)·π/442))` (the plugins' 10 ms end fade), the fade applied **after** the channel filter
  (before-filter fading differs by up to 2 %, after-filter matches every case). The table length at other sample rates
  is **not confirmed** (LYNX keeps 441 entries, as for the plugins).
- **Note-off:** with the volume envelope enabled the four articulators (volume, cutoff, resonance, pitch) release; with
  it disabled the note-off changes nothing — the sample plays on (one-shot) and the other envelopes hold their level
  (`e2`/`e5`). The decompiled release (`FUN_00d241e0`) releases records 1–4 unconditionally, so the gate sits in its
  caller (**not traced**).
- **Event placement:** with LYNX's old `floor` placement a note on tick 192 (sample 44100.0 in exact arithmetic) could
  land one sample early; after the change every note of every project lines up (lag 0).
- `Sampler.flp` itself (FL's render test) uses a sample with a `smpl` loop (frames 3099…4111) over keys C1…C7: FL loops
  the sample while the note is held and stops at the note-off; LYNX's native sampler does not read WAV loops yet, so
  this project still differs (**not ported**, see below).

## 7. LYNX port **[model]**

- `SamplerSettings::flChannel` (`core/Song.h`): the five 0xDA records, Mod X/Y, filter type and the project's PPQ (89
  values), filled by `FlpImport` for native sampler channels (a missing record keeps the plugins' default), saved in the
  project (`ProjectFile`), cleared when the envelope or filter is edited by hand (`ChannelSettings`).
- `SamplerInstrument` (`core/Instruments.cpp`), when `flChannel` is present: the voice is FL's — `flvp::Settings` built
  from the records in the plugins' snapshot layout (the host's rate ratio and ramp limit set), `flvp::Proc` per voice;
  initial volume by FL's velocity law (`flInitVolume`), note pan; the channel's volume and pan recovered from the
  rack's gains and applied **inside** the voice through `flvp::panLaw` (centre −3 dB); per tick: articulators, target
  gains, filter coefficients, gain and fast-LP ramps (`flTick`); per segment between ticks: FL's interpolators, the
  channel filter (`renderFast`/`renderBiquad`/`renderSvf`, second stages and gain correction), the ramped mix, the end
  fade (`renderFl`). Ticks fall on the song's grid: the note's tick `round(songSample/spt)`, then `round(k·spt)`.
- `Scheduler` (`emitOccurrences`): an event on tick t goes to sample `round(t/tps)` (ties to even), FL's placement.
- `tools/renderwav`: absolute Windows paths in a project (`C:\…`) map to `/mnt/c/…`.
- Test `fl_native_sampler_voice_matches_fl_renders`: FL's int16 codes from `f0b` and `e6` at 48 frames each (±1), the
  velocity law and a hard-left note against FL's measured gains.

## 8. WAV loop points **[EXEC: FL renders `l1`, `l1a…l3e`, `s0`]**

- A sample whose WAV has a `smpl` loop (first loop; end frame inclusive) loops in FL while the note lasts, with
  the defaults of FL's own `Sampler.flp` channel. Clearing bits of the sample-flag events 0x8A/0x8B/0x83 or of the
  0xD1/0xD7/0xDD records did not stop the looping; which setting turns it off ("use loop points") is **not confirmed**
  — LYNX loops whenever the file has a loop.
- After the note-off, a looping voice without volume envelope keeps reading the loop and fades out with the 441-entry
  end table (post-filter), so it is silent ≈ 10 ms after the note end; a voice without a loop plays on (§6).
- LYNX: `SampleData::loopStart/loopEnd` read from `smpl` by `tools/WavReader.h` and by the app's loader
  (`SampleLibrary`, JUCE's `Loop0Start/End` metadata); FL voices loop over `[loopStart, loopEnd)` and start the fade at
  the note-off (`offFade`). Result: `l1` notes at the root key ±1 code; FL's `Sampler.flp` (C1…C7 over a looped sample)
  ±1 code on the four lower notes. **Residual:** pitched notes show a short transient (≤ 4 samples, up to −44 dB below
  the peak) right after each loop wrap — the exact tap/position rule of FL's kernel at the wrap is **not reproduced**.

## 9. Pan-law mode 1 **[EXEC: FL render `p2`]**

The project event 0x17 holds FL's pan law: 0 = the circular sin/cos law (§3), 2 = the triangular law of
`FUN_006a39c0` (`vol·sin(π/4)` on the louder side, `(1 ∓ pan)·vol·sin(π/4)` on the other, x87). FL's render of `t1` with
0x17 = 2 matches LYNX ±1 code; 0x17 = 1 renders exactly like 0 (circular). LYNX: `flp::Project::panLaw`,
appended as the 90th value of `SamplerSettings::flChannel` (89-value projects keep the circular law). The pan law of
the mixer and of the other generators is unchanged (**not checked**).

## 10. Global LFOs **[EXEC: FL render `g1`]**

A channel LFO with the global flag (0x20; here the volume LFO, amount 80, speed 24000, notes at ticks 0, 100, 300)
has one phase for the channel: at song tick k it is `(k + 1)·increment` (32-bit wrap), whatever the note's start.
With `k·increment` or `(k − 1)·increment` LYNX is off by up to 2 % of the peak; with `(k + 1)` every note is ±1 code.
LYNX sets the global phase of the channel's records from the voice's song tick before stepping it (`flTick`).

## Where I stopped / next steps

- State (2026-10-16): branch feature/sampler76 (= origin/wip/sampler76) has the port, origin/main merged, the
  unit test `fl_native_sampler_voice_matches_fl_renders`; the last full build-local after the merge was still running
  (log `tmp/r76/build3.log`) when the round was paused. Next: confirm 0 failures, Windows build, selftest, land
  (`tmp/coord/land.sh sampler76` under the merge lock).

- The velocity/release cross-modulation table and layer parents are not ported.
- Other sample rates (end-fade length, rate ratio) are unverified: FL's command-line render uses 44.1 kHz here.
- FL's command-line render forwards to a running FL instance and then writes nothing; `fl.sh` waits until no
  `FL64.exe` runs and retries.
