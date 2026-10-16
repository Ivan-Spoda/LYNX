# Behavioural analysis of FL Studio 2026: round 74 (REA) — IPP's float cosine (the plugin framework's end fade: Fruit Kick, DX10, 3x Osc) and Sytrus's main settings page

> **Nature of this document.** Continuation of `REA_rodada42.md` §3.8, `REA_rodada43.md` §1.5/§3 and
> `REA_rodada55.md` §6 (the last gap of Fruit Kick: the 10 ms end fade, built with IPP's cosine), and of
> `REA_rodada11.md` §1.3 / `REA_rodada31.md` (Sytrus's state) for the channel settings panel LYNX lacked for Sytrus.
> *Clean-room* reverse engineering, behavioural only. No Image-Line or Intel code or pseudocode was copied: what is
> here are data layouts, formulas and behaviours described in our own words. Addresses serve only to trace the
> evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation/disassembly (**[DECOMP]**), from the
> data (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Inferences are marked as
> such; what could not be established is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the disassembled code (`objdump -M intel`) |
| **[DATA]** | a value read from a DLL's data section or form resource, or from saved states |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r74/`.
- **Targets:**

  | File | SHA-256 |
  |---|---|
  | `Shared/dsp_ippv2_x64.dll` (FL's Intel IPP build) | `4dbb9e954f5cc3c83e7d40872a7d616d5e6fb6280b3d377260c9438ae551e0d4` (= rounds 24/31/35/69) |
  | `Plugins/Fruity/Generators/Fruit Kick/Fruit Kick_x64.dll` | `a93d86452f8cb35b94a9e101bf241a68316f7d0cb2da6366a79781c0b874122b` |
  | `Plugins/Fruity/Generators/Fruity DX10/Fruity DX10_x64.dll` | `3a9d58d7e19abcba66393caa6aeecfe2fab3dbc715c9e9256f4a08da24fde8d9` |
  | `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll` | `5d84d5ad2d8461edd43070d9a8a1147e8e8c308815ad8c91862533440d89dff2` (= round 47) |
  | `Plugins/Fruity/Generators/Sytrus/Sytrus_x64.dll` | `d52a97c55e36c2457305ce4f2d6eed08aabd12dbec02c9f5f55ce8788e944f15` (= rounds 11/23/31) |

- **Tools:** `objdump -M intel` on `bin/dsp_ippv2_x64.dll` (`out/k0.s`, `out/k4.s`, `out/k5.s`), `py/pe.py` /
  `py/cst.py` (dispatch tables and constant vectors read from the file).
- **EXEC harnesses (our own code, zig cc 0.14.1, x86_64-windows-gnu; installation DLLs loaded unmodified,
  `SetErrorMode`, fault handler, `TerminateProcess`, `timeout`):** `run/ic.c` (calls `ippsCos_32f_A11`, reports the
  dispatcher index and code pointers, optional `ippSetCpuFeatures`), `run/sw.c` (sweeps every float of a bit range
  through the DLL and through our restatement compiled into the same program, counts mismatches), round 55's
  `run/kd55.exe` (Fruit Kick with FL's host waveshaper forwarded from FLEngine), round 31's `run/syt.c` (Sytrus).
- **Drivers:** `py/fkrel.py` (Fruit Kick DLL vs LYNX's `fruitkickrender` with note releases), `py/cppcmp_r74.py`,
  `py/cppcmp2_r74.py` (DX10), `py/cppcmp47_r74.py` (3x Osc): rounds 42/47's C++-vs-model batteries with the model fed
  the DLL's own end-fade table (`r42/out/endtab_44100.npy`, dumped in round 42); `run/tab.c` (fade table vs the dumped
  tables); `cpp/st.cpp` (Sytrus settings read/write on raw states).

---

## 1. `ippsCos_32f_A11` **[DECOMP + EXEC]**

### 1.1 Who calls it

Fruit Kick, DX10 and 3x Osc resolve their IPP entry points by name from `Shared/dsp_ippv2_x64.dll` (UTF-16 name
table in each DLL, `ippsCos_32f_A11` among them) **[DATA]**; their shared voice processor builds the 10 ms end fade
with `ippsVectorSlope_32f`, `ippsMulC_32f`, `ippsCos_32f_A11`, `ippsAddC_32f`, `ippsMulC_32f` (round 42 §3.8).
Round 42 showed that everything but the cosine is reproduced by `x_i = float(i + 1)·k`, `k = float(π/(n + 1))`,
`t_i = (cos(x_i) + 1)·0.5` in float; the cosine was the only open part.

### 1.2 Dispatch

The export (`0x1810559e0`) jumps through a table indexed by a global CPU-class index (`0x182541000`, −1 until the
first call) **[DECOMP]**. Index → kernel, read at run time **[EXEC `ic.exe info`]**: 0 → `0x1810c7e20`, 3 →
`0x18110ce20`, **4 → `0x18115b5c0`** (the index on the test machine, an AVX2/FMA CPU), 5 → `0x1811652a0`.
`ippSetCpuFeatures` with masks up to `0x7fff` gives indices 0…3; with the AVX2 bit `0x8000` set, index 4 **[EXEC]**.
Index 5 (AVX-512) cannot run on the test CPU.

### 1.3 The algorithm (both paths) **[DECOMP; constants DATA]**

For each element, with `a = |x|` and every step in float:

1. `a > 10000` or NaN: another path (not decoded this round; the fade never gets there).
2. `s₀ = a + π/2` (float π/2 = `0x3fc90fdb`); if `s₀ == π/2` (a negligible), the result is exactly 1.
3. Quadrant: `s = s₀·(1/π) + 1.5·2²³` (rounding trick; `1/π` = `0x3ea2f983`); the lowest mantissa bit of `s`
   becomes the result's sign bit; `n = (s − 1.5·2²³) − 0.5`.
4. Reduction `r = a − n·π`:
   - FMA path (indices 4 and 5): two fused steps, `r = a − π₁·n` then `r = r − π₂·n` (one rounding each), with
     `π₁ = 0x40490fdb` (float π) and `π₂ = 0xb3bbbd2e` (≈ −8.74·10⁻⁸, its correction);
   - other path (indices 0–3): three separate multiply-subtracts with `0x40490000`, `0x3a7da000`, `0x34222169`.
5. Polynomial: `r² = r·r`, `rs = r` with the sign bit flipped by the quadrant parity;
   result `= rs·c₁ + (rs·r²)·(c₃ + c₅·r²)` with `c₁ = 0x3f7ff87c`, `c₃ = 0xbe29e73f`, `c₅ = 0x3bf84524`;
   the FMA path evaluates `c₃ + c₅·r²` and the final sum fused, the other path with separate operations.

The AVX-512 kernel (index 5) has the same fused sequence with explicit round-to-nearest **[DECOMP only, not run]**.

### 1.4 Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Our FMA restatement vs the DLL at index 4 (`sw.exe`) | every float with \|x\| ≤ 10000, both signs (2 × 1 176 256 513) | **0 mismatches** |
| Our non-FMA restatement vs the DLL forced to indices 0, 1, 2, 3 | the same 2 × 1 176 256 513 floats per index | **0 mismatches** |
| The FMA restatement vs indices 0–3 | 25 M floats in [0.125, 1] | 6.3 M differ (last bit): the two paths really differ |
| End-fade tables built with the restatement (`run/tab.c`) vs the tables round 42 dumped from the running plugin | 441 entries (dumped at 44.1 and at 48 kHz) | **identical** (the exact cosine: 438 of 441 differ) |

The fused multiply-add is done without hardware FMA or the C library's `fmaf`: the product of two floats is exact in
double, the sum's rounding error comes from a two-sum, and the double is rounded to odd before the float rounding,
which gives the correctly rounded result (`sw.exe` ran exactly this code, built by zig for Windows; the Linux build
gives the same tables).

---

## 2. Fruit Kick, DX10, 3x Osc: the end fade **[EXEC]**

- **Fruit Kick's fade length.** LYNX built Fruit Kick's table with `Round(0.01·fs)` entries (480 at 48 kHz). With
  releases at 48 kHz this differed from the DLL from the release on (−22…−29 dB): FL's table has **441 entries at
  every rate**, as round 42 found for DX10 (the framework builds it while its rate object still holds 44 100 Hz). With
  441 entries and IPP's cosine, every case is bit-identical (below).
- LYNX (`core/IppCos.h`): `ippcos::cos32fA11` takes the FMA path when the CPU reports AVX2 with the OS saving the AVX
  state, the other path otherwise — what IPP's dispatcher does by the masks of §1.2 (that IPP's own CPU detection also
  requires the OS flag is an inference). `ippcos::endFadeTable` builds the fade; Fruit Kick, DX10 and the shared voice
  processor (`flvp::endTable`: 3x Osc, BeepMap, Granulizer) use it.

| Check | Cases | Result |
|---|---|---|
| Fruit Kick DLL (FL's host waveshaper forwarded) vs LYNX `fruitkickrender`, with releases (`py/fkrel.py`) | 11 states × 3 release cases (one note at 1500; two overlapping notes, block 1000; key 84 at block 333) × 44.1/48 kHz, live and file-render waveshaper | **66/66 + 66/66 bit-identical** (before: −83…−86 dB at 44.1 kHz, −22…−29 dB at 48 kHz) |
| Round 55's battery (no release) | 11 states × 3 × 2 rates, live and render | 66/66 + 66/66 bit-identical (unchanged) |
| DX10 C++ vs round 42's model fed the DLL's table (`cppcmp_r74.py`, `cppcmp2_r74.py`) | 256 state jobs + 23 processor jobs | **all bit-identical** (the model matches the DLL bit for bit, round 42 §4) |
| 3x Osc C++ vs round 47's model fed the DLL's table (`cppcmp47_r74.py`) | 670 jobs | 650 bit-identical + 20 HQ-table jobs (equal to float rounding by design, round 47) |

So **Fruit Kick is bit-exact with FL's DLL including release fades**, on CPUs where IPP takes either path decoded here.

---

## 3. Sytrus: the "Main settings" page **[DATA + EXEC]**

LYNX's channel settings window had no panel for Sytrus (`docs/HANDOFF.md`). The plugin's form (round 11's DFM dump,
`r11/sytrus.dfm.txt`) lists the controls of the "Main settings" tab with their tags, ranges and defaults **[DATA]**;
each tag is an int of the 962-byte main block (`REA_rodada11` §1.3; index = offset/4), PORTA is byte 1 of the flag
block that follows (offset 970 for state versions ≥ 17):

| Control (form name) | Main int | Range (default) |
|---|---|---|
| Master level / Master LFO level / Master pitch | 0 / 1 / 2 | 0…160 (128) / −128…128 (128 in the default patch) / −24…24 (0) |
| Volume attack, decay, sustain, release | 165–168 | 0…256 (128), sustain −128…128 (0) |
| Filter attack, decay, sustain, release | 169–172 | as above |
| Unison order, panning, volume, pitch, sub level, phase, envelope variation | 173–179 | 1…9 (1), 0…128 (128), 16…128 (100), 0…256 (128), 0…96 (0), 0…128 (16), 0…192 (0) |
| Mod X / Mod Y | 190 / 191 | 0…512 (256) |
| PORTA | flag byte 1 | off/on |
| Oversampling factor (draft / rendering) | 195 / 196 | 0…6 in the form (0 / 3) |
| Extra high envelope accuracy (draft / rendering) | 193 / 194 | off/on (0 / 1) |

- The installation's default patch and three demo channels read back with exactly the form's defaults where they are
  untouched (`cpp/st`) **[DATA]**.
- LYNX writes an edited value into the decompressed state and stores it again as a zlib stream of stored blocks (the
  same writer the Ogun panel uses, moved to `core/Inflate`). **[EXEC]** FL's Sytrus DLL accepts such a state: the
  default patch re-stored unchanged renders bit-identically to the original, and with Master level 64 it renders the
  original × `(11^(64/128) − 1)/10` to float rounding (2.5·10⁻⁸) — the master-level law of round 11.
- LYNX's panel (`ui/ChannelSettings.cpp`, API `sytrus::settings / settingValue / setSetting`) shows these 25
  controls; values are clamped to the form's ranges, except the oversampling orders, which LYNX limits to 0…4 (its
  engine's range since round 31; orders 5–6 are not modelled). A change re-parses the patch (the voices restart).
- Not on the panel: the effects/EQ controls of the same page, the RANDOM / SMOOTH / GLOBAL PITCH / MONO KEY / OSC /
  CENTER / GIBBS OFF switches and the velocity "soften" wheel (their storage is partly decoded in round 31, but their
  control mapping was not checked this round).

---

## 4. What LYNX does

- `core/IppCos.h` (new, header-only): IPP's float cosine (both paths), a portable single-rounding multiply-add, the
  CPU check, the end-fade table.
- `core/FruitKick.cpp`: the fade is the framework's 441-entry table with IPP's cosine; `core/Dx10.cpp`,
  `core/FlVoiceProcessor.h` (3x Osc, BeepMap, Granulizer): IPP's cosine.
- `core/SytrusEngine.{h,cpp}`: `sytrus::settings()`, `settingValue`, `setSetting`; `core/Inflate.{h,cpp}`:
  `zlibStored` (moved from `core/Ogun.cpp`); `ui/ChannelSettings.cpp`: the Sytrus panel.
- Tests: `ipp_cos_and_end_fade` (DLL values of both paths, the multiply-add's halfway cases, the table),
  `sytrus_main_settings` (read/write/clamp, PORTA byte, master-level law on the engine, broken states).

## 5. Open items

- `ippsCos_32f_A11` for |x| > 10000 and NaN (another code path): not decoded; LYNX falls back to the C library there
  (no LYNX caller passes such arguments).
- The AVX-512 kernel is read, not run (no AVX-512 CPU here). Which index FL's process picks on CPUs that report AVX2
  but not FMA (none exist in practice) is not confirmed.
- The goldens of the Fruit Kick, 3x Osc and Granulizer tests now include IPP's cosine of the FMA path; on a CPU
  without AVX2 (the other path) their release-fade samples would differ in the last bits.
- Fruit Kick: FL's exact sample placement of the sequencer ticks (round 43 §3) remains an inference.
- Sytrus: oversampling orders 5–6 and the remaining switches of the main page (§3).

## Evidence index

| Function / data | What | Mark |
|---|---|---|
| `dsp_ippv2_x64.dll` `0x1810559e0`, table `0x18254a168`, index `0x182541000` | `ippsCos_32f_A11` dispatch | DECOMP + EXEC (`ic.exe`) |
| `0x18115b5c0` (index 4), `0x1811652a0` (index 5); constants `0x1820fadc0…0x1820fc0c0` | FMA path | DECOMP + EXEC (`sw.exe`) |
| `0x1810c7e20` (index 0; 3 = `0x18110ce20`); constants `0x181fc2f80…0x181fc3440` | non-FMA path | DECOMP + EXEC (`NOFMA=1 sw.exe`) |
| `ippSetCpuFeatures` masks `0x7fff` / `0xffff` | index 3 / 4 | EXEC |
| `r42/out/endtab_44100.npy`, `endtab_48000.npy` | the plugin's own fade tables | EXEC (round 42 dump) |
| Fruit Kick_x64 + FLEngine forwarded (`r55/run/kd55.exe`) | release cases | EXEC (`py/fkrel.py`) |
| Sytrus form `MainPanel` (`r11/sytrus.dfm.txt`) | main-page controls, ranges, defaults | DATA |
| Sytrus_x64 (`run/syt.exe`) on re-stored / edited states | stored-zlib acceptance, master-level law | EXEC |
