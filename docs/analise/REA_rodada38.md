# Behavioural analysis of FL Studio 2026: round 38 (REA) — Harmor's effects rack (and Maximus's RMS detector)

> **Nature of this document.** Continuation of `REA_rodada34.md` (Harmor's voice; §1.24 left the rack "structure only,
> not decoded") with material shared with `REA_rodada36.md` (Harmless's rack: chorus, delay, shared reverb, Maximus
> through SG.dll) and `REA_rodada31.md` §7 (the same chorus/delay library and reverb DLL in Sytrus). *Clean-room*
> reverse engineering, behavioural only. No Image-Line (nor Intel IPP) code or pseudocode was copied: what is here are
> data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the
> repository (Harmor's compression presets are read at run time from the user's installation).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation (**[DECOMP]**), from
> the data (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are stated
> explicitly as **inference**.

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp`/`.fst`/installation files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-14. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/`.
- **Targets:** `Plugins/Fruity/Generators/Harmor/Harmor_x64.dll` SHA-256 `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba`
  (= round 34); `Shared/SG_x64.dll` `ad567e65e2aac8314d23f2322f4d17e1b5529a09f64c43d596189582f006f6ac` (= rounds 35/36);
  `Shared/Reverb_x64.dll` (= rounds 31/36); Harmor's presets `Plugins/Fruity/Generators/Harmor/System/Compression/1…4.fst`
  (SHA-256 prefixes `3dc934f3…` (= Harmless's 1.fst), `d2ce196e…`, `0862ed91…`, `adb64e9a…`) **[DATA]**.
- **Tools:** round 34's Ghidra project `/tmp/r34gh_Hm` and dumps (`r34/out/plug_all_ipp.c`, `lib78_ipp.c`, `pp.c`), new
  decompilations `r38/out/helpers.c`, `h2.c`, `h3.c` (functions outside the round-34 ranges); round 36's SG project
  `/tmp/r36gh_SG` (`r38/out/sg_rms.c`, `sg_rms2.c`); `objdump` for the x87 code; constants with `r34/py/k.py`.
- **EXEC harness:** `r38/run/hm38.c` → `hm38.exe`, a copy of round 34's `hm.c` that also answers the plugin's host
  data-file requests (dispatcher `0x47` → method 6, request `0x3fc`: the requested name is relative to
  `Plugins\Fruity\Generators\`, e.g. `Harmor\System\Compression\3.fst`; dispatcher `0x1d` → the plugin folder) as round
  36 did for Harmless, plus `fp=`/`gd=` peeks of run-time pointers/globals and `nodata` to refuse the files. Same safety
  measures (unmodified DLL, `SetErrorMode`, fault handler, `TerminateProcess`, `timeout`).
- **Drivers:** `r38/py/h38.py` (runs hm38), `st38.py` (unit switches on a state), `rackcheck.py` (LYNX's C++ vs the DLL,
  rack on, per unit and all together; log `r38/out/rack2.log`); round 36's `r36/py/sgx.py` (SG standalone) for §6.

---

## 1. Where Charles Deluxe's level came from **[EXEC]**

The two Harmor channels of `Demo projects/Performance mode/Charles Deluxe - Performance Demo.flp` are demo states 28 and
29 of round 34's list. Both use the rack: 28 has the compressor (type 3); 29 the compressor (type 2), reverb and chorus
(29 also carries image data, not modelled). The DLL (round 34's three overlapping notes, Final.Vol = velocity):

| State | Rack on (files answered) | Rack off | Rack on, files refused |
|---|---|---|---|
| 28 | RMS 0.2406, peak 0.904 | RMS 0.4169, peak 2.215 | RMS 0.2276, peak 0.976 |
| 29 | RMS 0.2650, peak 0.975 | RMS 1.3698, peak 6.196 | RMS 0.2768, peak 0.976 |

So the +9 dB of the regression is the rack that LYNX left off (+4.8 dB and +14.3 dB on these two states): the
compressor presets limit the loud patches (both peak just under 0 dB in the DLL). Without the preset files the hosted SG
keeps its own default state, which also limits near 0.976 (round 36 found the same with Harmless). With the rack ported
(§2–§6) LYNX's output for state 28 is within −59.8 dB of the DLL (its voice alone is −58.2 dB), level difference 0.00 dB.

The song itself, played through the DLL with FL's host values (the 30 notes each channel has in the project at 130 BPM,
Final.Vol = channel gain 0.15277 / 0.177591 from the project's volume knobs 0.3867 / 0.4258, pan 0, note velocities as
stored, the channels' own plugin states, compression presets answered from the installation):

| Channel | DLL rack on | DLL rack off | LYNX rack on | LYNX rack off |
|---|---|---|---|---|
| 9 (state 28) | RMS 0.1538, peak 0.742 | RMS 0.0609, peak 0.484 | RMS 0.1538, peak 0.742 (−55.6 dB) | RMS 0.0609 (−54.0 dB) |
| 10 (state 29) | RMS 0.3063, peak 0.976 | RMS 0.2473, peak 1.351 | RMS 0.3188, peak 0.975 (−0.9 dB, image data) | RMS 0.5102 (+6.9 dB) |
| Sum | RMS 0.3311, peak 1.475 | RMS 0.2532, peak 1.318 | RMS 0.3472, peak 1.486 (+0.41 dB level) | |

At the song's quiet channel gains the compressor *raises* channel 9 (makeup gain), so the rack-on level is the right one in
both directions. Channel 10's voice is not modelled (image data), which is also why LYNX's rack-off render is louder than
the DLL's; with the rack on, the compressor brings it to within +0.35 dB of the DLL.

## 2. The rack in Gen_Render **[DECOMP + EXEC]**

- **Order:** the state's 6-integer global block (after the 53 globals; round 34's "x6") is the **unit order** (plugin
  `+0x28c8`). `FUN_00892e10` builds a callback list (`+0x28e0`, 16-byte entries, count `+0x2940`) from it, skipping
  absent units: **0 distortion** (`FUN_008944a0`, present when the type `+0x2cc0` > 0), **1 chorus** (`FUN_00893ec0`,
  object `+0x2958`), **2 delay** (`FUN_00893fc0`, `+0x2968`), **3 reverb** (`FUN_008942b0`, `+0x2a98`), **4 FX-send
  join** (`FUN_00894330`, always present), **5 compressor** (`FUN_008943f0`, the hosted plugin list `+0x2ce0 → +0x128`).
  The list runs after the voices and before the master volume (`FUN_007a4a90` on `+0x109c`).
- **FX send:** the records' dry output (`g·(1 − |FX|)`, round 34 §1.17) goes to the main buffer (`+0x60`) and their FX
  output (`g·FX`) to a second buffer (`+0x68`); unit 4 adds the second into the first. Units before unit 4 therefore
  process the dry part only. EXEC (plain state 36, reverb on): FX = 0 → the reverb changes the output by −15.7 dB; FX =
  1000 → identical to the rack off (−600 dB); FX = 1000 with the order `0,1,2,4,3,5` → −15.7 dB again.
- **Activity counter** (`+0x2944`): reloaded with `+0x2948` in every block that has records, otherwise decreased by the
  block length while positive; copied to `+0x294c` before the list. The distortion skips the block and the chorus only
  advances its phases while it is < 1; the delay, reverb and compressor set it to `0x7fffffff` after running (later units
  are then active). `+0x2948 = round(0.030·fs)` (0.530 s when the distortion is on) `+ 384 + chorus maximum delay`
  (the chorus ring) (`FUN_008937f0`, constants `0x893a58` = 0.001, 30/530) **[DECOMP]**.
- Parameter changes mark units dirty (ProcessParam `FUN_00879540`: globals 17–21 distortion, 22–31 reverb, 32–38
  chorus, 39–48 delay, 49–52 compressor) **[DECOMP `pp.c`]**.

## 3. Chorus, delay, reverb: the shared library fed from Harmor's globals **[DECOMP + DATA + EXEC]**

Global `gN` = plugin `+0xd48 + 4N`.

- **Chorus** (`FUN_008937f0`, library `FUN_007a8850/7a8a70/7a91b0/7a9170` = round 31 §7.3 / round 36's unit): on when
  g32 > 0; order N = g32, depth = g33/1024, speed = g34/1024, delay = g35/128, spread = g36/128, cross = g37/128 (the
  library's fields; Harmless takes them from fixed records); mix m = |g38|/1000: dry `1 − (5^m − 1)/4`, wet volume
  `sign(g38)·(1 − (5^(1−m) − 1)/4)` (0 when g38 = 0). Processed in 384-sample pieces; the ring is `384 + maxDelay`.
- **Delay** (`FUN_00892f90`, process `FUN_00893fc0`, library `FUN_007a5290` … = round 31 §7.4): on when g39 > 0; mode g40
  (0 normal, 1 outputs swapped, 2 ping-pong — the library's three process functions); feedback target g41/1000 through
  the library ramp `FUN_0062d390` (per call, limited to `0.001·44100/fs` per sample), **starting from the constructor's
  0.5** (EXEC: with g41 = 950 or 100, 0.5 matches at −77 dB, the target or 0 at −57/−30 dB); time g42 ≥ 0:
  `g42·0.3125/BPM` s, g42 < 0: `(2^(−g42/96) − 1)/15` s, `D = round(float(t)·fs)`; stereo offset
  `(2^(|g43|/64) − 1)·1.96078 ms` (`0x87c950/0x87c958`) on the right channel when g43 > 0, else on the left; input gain
  `vol³` (vol = g44/1000) with pan g45/128 (negated for modes 1/2) through the record pan law (`√2·vol³·cos/sin((1 + p)π/4)`,
  `FUN_007a4d50` + `FUN_0062d460`), a 15 ms ramp from 0 at state load (EXEC peek), snapped to its target while the
  counter is < 1; two biquads (`FUN_006268a0` design, `FUN_006270c0` double direct form, states rounded to float with a
  5.96e-8 flush per call): g46 ≠ 0 → high-pass, g47 ≠ 1050 → low-pass (types 4/1 from `0x9286dc`), at
  `16.3515987·2^(g/100)` Hz (run-time float `0x94e118`) clamped to `0.4999·fs`, α = sin ω (Q ½); on the input
  (g48 ≤ 0) or inside the loop on the ring before each read (g48 > 0). Ring `offset + 384 + D` frames, read in pieces up
  to the write pointer or the ring end (`FUN_007a5260`) — the feedback ramp is computed per piece.
- **Reverb** (`FUN_00893470`, wrapper `FUN_007a8420/7a8600`, coefficients `FUN_007a7c20` = Harmless's builder, DSP the
  shared `Reverb_x64.dll`): on when g22 > 0; the wrapper's fields: low cut `16.35·2^(g23/100)` Hz (g23 ≤ 0 off), high cut
  and damping `16.35·2^(g/100)/100` (hundreds of Hz; g24/g29 ≥ 1050 off), predelay g25 (≥ 0: `g25·0.3125/BPM` s, < 0:
  `(2^(−g25/64) − 1)/63` s), room g26, diffusion g27, decay `2^(g28/100)·0.1·fs` samples, colour g30; input `L + R`
  (`FUN_0062bb60`); wet `0.5·(g31/1000)³` through a ramp that starts from 0 at state load (EXEC peek: 0.11301 after the
  first 512 samples toward 0.11630).
- **Ramps** (`FUN_007a4cc0/4a90/4c00/49b0/4b10/4c70`): target L/R, current L/R, coefficient (the rate object's
  0.993062496 at 44.1 kHz = `exp(ln 0.01/(0.015·fs))`); snapped when both differ by < 5.96e-8; per sample `d ← d·c`,
  gain `target + d`.

## 4. The compressor **[DECOMP + EXEC]**

The compressor set-up (`plug_all_ipp.c:23855…`): type = global misc +52; n > 0 hosts `SG.dll` and loads
`Harmor\System\Compression\<n>.fst` through the data-file request; then re-sends Harmor's parameters 0x335…0x338
(**globals 49–52**) through ProcessParam, which forwards them to the hosted plugin as its parameters **5, 8, 21, 34**
(table `0x928280`) = Maximus's LMH mix and the low/mid/high **pre gains** (round 8 §1.2) **[DECOMP + DATA]**. LYNX:
round 36's Maximus with the preset's state patched at those four parameters. Preset 2 uses the RMS detector (§6).

## 5. The distortion **[DECOMP + EXEC]**

`FUN_008944a0` (process) and ProcessParam cases 0x315…0x319 (set-up); type = global misc +48 (demos: 1–7, 9).

- **Oversampling** always on: `2^clamp(precision + 1, 1, 3)` (2×/4×/8×; precision = global misc +28) through a cascade
  of 2× stages (`FUN_00863fd0`/`FUN_00864070`, kernels `0x863aa0`/`0x863c70`): the polyphase half-band of round 36's
  Maximus (coefficients 0.14737711, 0.03915160, 0.48246854, 0.30264685, 0.88300503, 0.67461592; down-sampler ×0.5; the
  all-pass outputs rounded to float with a flush after every call, `FUN_00629ef0`) **[DECOMP + EXEC table read]**.
- **Chain** at the oversampled rate: `dry = x·mix` (g20/1000, ramp `+0x2b10`); `x += bias` (ramp `+0x2ac8`, types 1–7);
  `x ·= drive` (ramp `+0x2aec`); `y = shape(x)`; `dry += y·wet` (ramp `+0x2b34`); optional 4th-order low-pass (two
  biquads at `16.35·2^(g21/100)` Hz, α = sin ω·0.70710678, off when g21 = 1100); down-sampling; then a DC stage
  (`FUN_0062a3c0`, x87: `v += (x − p)·c2`, `p += v`, `v ·= 0.96`, out `x − p`, `c2 = 2.2835e-5·44100/fs`). The four
  ramps are 15 ms at the oversampled rate and start at their targets (EXEC peek).
- **Shapes** (IPP `Tanh/Exp/Ln/Sqrt/Cbrt_32f` with 11-bit accuracy at precision 0, 24-bit above; `Sin_32f_A21`):
  1 tanh x; 2 `e^(tanh x) − 1`; 3 `tanh(eˣ − 1)`; 4 `tanh(x)·tanh(x)²`; 5 `sign(x)·ln(|x| + 1)`; 6 `∛tanh(x³)`;
  7 `sign(x)·sin√|x|`; 8 `tanh(x)·tanh(x + b)·tanh(x + 2b)`; 9 `sin x + b·x`; 10 round(x); ≥ 11 a logarithmic
  quantiser `sign(x)·expm1(round(c·ln(|x| + 1))/c)`.
- **Laws** (vol = `(3^v − 1)/2`, v = g19/1000; `W = vol·(1 − |g20/1000|)`; `d` below):
  types 1–3, 8: `d = 2^(g17/100 − 4)`, drive `π·d`, wet `W/(π·tanh d)`; 4: `d = 2^(g17/100 − 2)`, drive `π·d`, wet
  `W/(π·tanh(4d)³)`; 5: `d = 2^(0.025·g17 − 5)`, drive d, wet `W/(ln(d + 1)·(g17/1000 + 1))`; 6: `d = 2^(0.005·g17 − 3)`,
  drive `π·d`, wet `W/(π·tanh d)`; 7: `d = π·2^(0.002·g17 − 1)`, drive d², wet `W/√d`; 9: b = `2^(0.004·g18)`, drive
  `π·2^(7 − 0.007·g17)`, wet `W/(drive·b)`; 10: drive `2^(9 − 0.007·g17)`, wet `W/drive`; ≥ 11: `q = 1024·2^(0.02·g18)`,
  drive q, `c = 2^(10 − 0.007·g17)/ln(q + 1)`, wet `W/q`. Bias (types 1–8): `b = 0.002·g18`; type 8 `b²·drive`, else
  `0.75·b²`; the right channel negated when g18 ≤ 0 (an integer test on the float's bits) **[DECOMP; EXEC: state 0 drive
  4.23245859, bias ±0.0259470008, wet 0.360076278; state 97 drive 59.7141113, wet 0.169593021; g18 = ±188 flips the sign]**.

## 6. Maximus's RMS detector (preset 2) **[DECOMP + EXEC]**

Band parameter 19 (envelope mode) selects the detector class (`FUN_00841a40`): 0 → the peak window of round 36,
otherwise the "RMSLookahead" class (VMT `0x780020`; process `FUN_00780b90` + `FUN_00780ab0` → `FUN_00780a60`): per sample
`L² + R²`, a running sum over the last W samples kept **in an x87 register** across the block and stored as a double
(`fldl`/`fstpl`), output `float(sum·1/(2W))`, then `ippsThreshold_LT(…, 0)` and `ippsSqrt` (`r36/py/ippsg.py`); W as the
peak window's. SG's band reset (`FUN_00845940`) does not reset the detector. EXEC (SG standalone vs LYNX on the same
heavily distorted input, preset 2 with pre gains 1336/2000/1664): −70.5 dB with a float sum, **−101.6 dB** with the
extended-precision sum.

## 7. Verification **[EXEC + LYNX]**

`r38/py/rackcheck.py`: LYNX's C++ (`tools/harmorrender`, the state's rack, the DLL's MT state) vs the DLL with its rack on
(files answered), round 34's three notes, 1 s, per unit (that unit alone on) and all together:

- **82 of 99 demo channels within −60 dB with the whole rack** (70 within −70, 33 within −80; median −77.3 dB); the
  channels whose voice is modelled have their level within 0.00 dB. Per unit (channels with a modelled voice):
  distortion 34 cases median −77.6 dB, chorus 14 −78.2, delay 18 −79.9, reverb 33 −79.6, compressor 47 −78.3.
- **Distortion accuracy** is limited by IPP's 11-bit shapers at precision 0: the same states at precision 1/2 go from
  −67.8 to −72.8/−81.7 dB (state 30) and −71.8 to −82.2/−85.8 dB (18); distortion alone on state 37 at precision 2:
  −122.7 dB. At full drive (type 6, g17 = 1000) the shaper amplifies the voice residual by ~10 dB (clean state 36:
  −94.2 dB vs −104.6 dB for the voice), which keeps states 19 and 76 at −51.6/−58.9 dB.
- Not passing besides the round-34 voice exceptions (image data, arpeggio envelopes, noise resonance, 28/78 residuals):
  19 and 76 (above) and 67 (both renders silent; the reverb's 1e-30 input guard leaves only round-off).

## 8. Open items

- IPP's 11-bit `Tanh/Exp/Ln/Sqrt/Cbrt` error pattern (precision 0) and `ippsRound`'s tie rule (types ≥ 10, not used by the
  demos) are not reproduced.
- The x87 precision-control setting inside Gen_Render (the DC stage and the RMS sum use extended registers) is assumed
  to be 64-bit (**inference** from the matching results).
- FL's own loading sequence (whether the delay feedback and the reverb wet ramp start the same way when FL loads a project)
  is not confirmed; the harness loads the state before the rate is set.

## Evidence index

| Function / data | What it shows | Marker |
|---|---|---|
| `FUN_00892e10`, Gen_Render `plug_all_ipp.c:23224…`, `22712…` | unit list from the order block, activity counter | DECOMP |
| `FUN_00894330` + EXEC on state 36 | FX-send join | DECOMP + EXEC |
| `FUN_008937f0`, `FUN_007a8a70` | chorus mapping, tail reload | DECOMP |
| `FUN_00892f90`, `FUN_00893fc0`, `FUN_007a5290/5100/55d0/5260/5660/5790/58b0`, `FUN_0062d390`, `FUN_006268a0`, `FUN_006270c0` | delay | DECOMP + EXEC |
| `FUN_00893470`, `FUN_007a8420`, `FUN_007a7c20`, `FUN_0062bb60` | reverb | DECOMP + EXEC |
| `plug_all_ipp.c:23855…`, `pp.c:907…`, table `0x928280` | compressor preset and parameter forwarding | DECOMP + DATA |
| `FUN_008944a0`, `pp.c:707…890`, `FUN_00893dc0`, `FUN_00863fd0/4070/3aa0/3c70`, `FUN_0062a3c0`, constants `0x87c830…0x87c9c0` | distortion | DECOMP + EXEC |
| SG `FUN_00841a40`, VMT `0x780020`, `FUN_00780b90/0ab0/0a60`, `FUN_00845940` | RMS detector | DECOMP + EXEC |
| `r38/run/hm38.c`, `r38/py/rackcheck.py`, `r38/out/rack2.log` | §1, §7 | EXEC |
