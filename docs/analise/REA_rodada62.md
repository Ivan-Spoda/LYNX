# Behavioural analysis of FL Studio 2026: round 62 (REA) — Frequency Splitter

> **Nature of this document.** First decoding of FL's Frequency Splitter as a plug-in of its own. LYNX imported it
> until now as an approximation (shelves in a mixer slot; its own Linkwitz-Riley crossover for a Patcher branch).
> Round 50 (`REA_rodada50.md` §2.9, §5) had already decoded two pieces of the same `engine_x64.dll` that Parametric
> EQ 2 borrows (the HQ resampler and the linear-phase FFT convolver); this round decodes the splitter itself.
> *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was copied: what is here are
> data layouts, formulas and behaviours described in our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals
> serve only to trace the evidence. No FLEX pack was opened; no Image-Line preset or audio data was copied into the
> repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from the data
> (**[DATA]**) or from running the installation's own DLLs, unmodified (**[EXEC]**). Statements that rest on the
> Python model alone are marked **[model]**; inferences are marked as such; what could not be established is marked
> "not confirmed".
>
> **Scope note.** The round was planned as "Frequency Splitter + Maximus v15 / Master Mid". Only Frequency Splitter
> was completed. Maximus v15's linear-phase band split and Master Mid mode were **not** done in this round (§7).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DATA]** | checked in `.flp` / `.fst` files, or a value read from a DLL's data section |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |
| **[model]** | holds for the Python model built from the DECOMP facts (checked against EXEC where stated) |

- **Date:** 2026-10-15. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r62/`.
- **Targets:**

  | File | SHA-256 | Role |
  |---|---|---|
  | `Plugins/Fruity/Effects/Frequency Splitter/Frequency Splitter_x64.dll` | `e2505dc65283a5d5af248245488d4222a9136fcf93687e461ba50e6890e0e27c` | Delphi front-end (FL plug-in framework, no DSP of its own) |
  | `Plugins/Fruity/Effects/Frequency Splitter/engine_x64.dll` | `567fcd74ea6cc89ad85c83953321758cab103061ed908ce468cafeebf6ae9593` (= round 50) | C++ VST2 engine (export `VSTPluginMain`, effect name "HQ 3-Band Splitter") |
  | `Plugins/Fruity/Effects/Maximus/Maximus_x64.dll` | `acc7affbe4d070ce8b71c13de79f267dd1b257f4207a61a733388a6aec1abdc1` (= round 40) | imported for the Maximus part of the round; not analysed (§7) |

- **Tools:** headless Ghidra 12.1.4 (project `~/gh-r62`, scripts `gh/scripts/DecompAll.java`, `DecompList.java`,
  driver `gh/dl.sh`; front-end decompilations in `out/wrap1.c`…`out/wrap3.c`), `objdump -M intel` for the engine's
  biquad design (`asm/e33b0.asm`) where the order of double/float operations matters.
- **EXEC harness:** `run/fs.c` → `fs_<n>.exe` (zig cc 0.14.1, x86_64-windows-gnu; `run/build.sh`): the effect harness
  of rounds 35/38/51 (unmodified DLL, `SetErrorMode`, vectored fault handler, `TerminateProcess`, `timeout`; state
  restored through the plug-in's own SaveRestoreState, Dispatcher sample rate / block size, ProcessParam, Eff_Render),
  extended with `sendf=`: the host's "output buffer for destination k" request is answered with a buffer per k, and
  each one is written to `<prefix><k>.f32`, so the three band sends can be captured next to the main output. The
  harness also logs every host-dispatcher call (`#hd`), which is how the reported latency was read.
- **Drivers (our own code):** `py/survey.py` (every Frequency Splitter state of the installation → `data/fs/`, and the
  Maximus states inside Patcher → `data/mx/`), `py/fx.py` (runs the harness; test signals; error measure), `py/pe.py`
  (values at virtual addresses of a DLL file), `py/fsmodel.py` (Python model of the engine, built only from the DECOMP
  facts below), `py/t_model.py` / `py/t_fft.py` (model vs DLL, IIR / FFT states), `py/t_cpp.py` (LYNX's C++ engine vs
  the DLL on every state), `py/t_auto.py` (parameter changes mid-stream), `py/mkgold.py` (our own test states → the
  goldens of `tests/tests.cpp`, `data/gold.txt`), `cpp/fscmp.cpp` (LYNX's engine on a float file), `cpp/sfxin.cpp`
  (the unit test's input signal as a file).

Error measure everywhere: `20·log₁₀(‖LYNX − DLL‖ / ‖DLL‖)` over both channels, per output (summed output and each band
send), 44.1 kHz, blocks of 512 unless stated; "−∞" = bit-identical.

**Installation usage [DATA, `py/survey.py`]:** 31 states, 26 distinct: 17 mixer slots and 5 states inside Patcher in
4 demo projects, plus the 9 factory presets (`.fst`: Default, Dual band 500 Hz, Gentle low roll off, Isolate below
200 Hz, Isolate over 4 kHz, Low boost, Mastering curve, Steep 30 Hz Cut, Steep tri band). All are state version 4,
113 bytes. 6 use the FFT (linear-phase) mode, 25 the IIR mode; slopes 6 to 96 dB/oct all occur; 5 send bands to
Patcher destinations.

---

## 1. State **[DECOMP front-end `FUN_0073fac0` + DATA]**

Plug-in record `0xD5`, little-endian:

| Offset | Size | Content |
|---|---|---|
| 0x00 | i32 | version (FL writes 4; **5 and above are not read**) |
| 0x04 | 17 × i32 | the front-end's controls, in control order (below) |
| 0x48 | i32 | an editor value (not DSP) |
| 0x4C | i32 | read and ignored |
| 0x50 | i32 | 3 editor bits |
| 0x54 | i32 | 6 mute bits: bit 0…2 low/mid/high **send** mute, bit 3…5 low/mid/high **summed** mute |
| 0x58 | u8 | option bits (version ≥ 2; editor/display options) |
| 0x59… | i32 × 1 / × 5 | version ≥ 3 / ≥ 4: editor fields |

Controls (raw ranges as the front-end stores them): 0 low/mid frequency and 1 mid/high frequency (0…65536), 2 low/mid
slope and 3 mid/high slope (0…7), 4 mode (0 IIR, 1 FFT), 5 three bands (0/1), 6 FFT quality (0…5), 7 FFT cross-fade
time (0…65536), 8…10 low/mid/high send level and 11…13 low/mid/high summed level (−1800…1800, 0 = unity), 14…16 the
**send destination** of the low, mid and high band (0 = not sent; k = FL's destination k, the Patcher bus k). After the
load the front-end re-sends every control through ProcessParam and the mute bits to the engine.

Solos are not in the state: they are engine parameters only, off after a load.

## 2. Engine parameters and the IIR mode

### 2.1 Front-end → engine **[DECOMP front-end ProcessParam `FUN_00740e30`, render `FUN_007423b0`, unload `FUN_00740970` + EXEC]**

The Delphi plug-in loads `engine_x64.dll` from its own folder (it frees it on destruction) and hosts it as a VST2
effect. ProcessParam passes a control as engine parameter `setParameter(i, v)` with:
- controls 0/1: the two frequencies **sorted** (the lower one is always engine parameter 0), each
  `float(clamp((raw − min)·scale, 0, 1))` = `raw/65536`;
- slopes 2/3: `raw/7`; quality 6: `raw/5`; mode 4 and three-bands 5: the raw value as a float;
- 7…13: `float(clamp((raw − min)·scale, 0, 1))` (cross-fade `raw/65536`, levels `(raw + 1800)/3600`);
- the mute bits become engine parameters 14…19 (1.0 = muted). Engine parameters 20…25 (solos) are never sent by the
  load.

Eff_Render runs the engine's process on the input, then for each band whose destination is above 0 asks the host for
that destination's buffer and **adds** the band's send output to it; the plug-in's own output is the engine's summed
output **[DECOMP + EXEC: the harness sees one request per destination, `#getout k`]**.

### 2.2 Parameter laws **[DECOMP engine + EXEC]**

26 parameters, each 0…1:

| # | Meaning | Law |
|---|---|---|
| 0, 1 | low/mid, mid/high frequency | `f = float(2^(v·9.058894)) · 30` Hz (30 Hz … 16 kHz), the product `v·9.058894` in float |
| 2, 3 | slopes | the first k (0…7) with `v` below the float running sum `(k + 1)·0.125`, else the last → 6, 12, 18, 24, 36, 48, 72, 96 dB/oct (`FUN_1801e6440`) |
| 4 | mode | `trunc(v·1.998)`: 0 IIR, 1 FFT |
| 5 | bands | `v < 0.5` → 2 bands, else 3 |
| 6 | FFT quality | `trunc(v·5.994)` (0…5) |
| 7 | FFT cross-fade | `float(10^(2v))·0.002` s (2…200 ms) |
| 8…10 | send levels low/mid/high | `g = 2v` for `v < 0.5`, else `(v − 0.5)·13.886564 + 1` (0.5 = 0 dB, 1 = +18 dB, 0 = silent) |
| 11…13 | summed levels low/mid/high | same law |
| 14…16 / 17…19 | send / summed mutes | on when `v ≥ 0.5` |
| 20…22 / 23…25 | send / summed solos | on when `v ≥ 0.5` |

A frequency, slope, mode, band-count or quality change marks the filters "dirty" (only when the derived value really
changes); the next process call redesigns them (IIR) or loads a new response (FFT, §3.3). Levels, mutes and solos only
move gain targets (§2.5).

### 2.3 IIR crossovers **[DECOMP engine `FUN_1801e33b0` (asm `asm/e33b0.asm`), `FUN_1801e8190` + EXEC]**

A crossover at frequency `f` with slope `S` runs four float lanes in parallel — low-pass left/right, high-pass
left/right — through a cascade of sections. The section design (double, stored as float, recursive coefficients
negated so that `y = b₀x + b₁x₁ + b₂x₂ + a₁y₁ + a₂y₂`):
- first order (types 9/10): `t = float(tan(f·π/fs))`, `d = 1/(t + 1)`; low-pass `b₀ = b₁ = d·t`, high-pass
  `b₀ = d`, `b₁ = −d`; `a₁ = −d·(t − 1)`; `b₂ = a₂ = 0`;
- second order (types 0/1), the RBJ forms: `ω = f·2π/fs`, `α = (0.5/Q)·sin ω`, `d = 1/(1 + α)`; low-pass
  `b₀ = b₂ = d·(1 − cos ω)/2`, `b₁ = d·(1 − cos ω)`; high-pass `b₀ = b₂ = d·(1 + cos ω)/2`, `b₁ = −d·(1 + cos ω)`;
  `a₁ = −d·(−2 cos ω)`, `a₂ = −d·(1 − α)`.

Per sample and section, in float with this association: `y = (((x₁·b₁ + x·b₀) + b₂·x₂) + y₁·a₁) + a₂·y₂`.

| Slope | Sections (Q, as the engine's double constants) | High band |
|---|---|---|
| 6 | one first-order section | as is |
| 12 | 0.5 | **inverted** |
| 18 | all-pass structure (§2.4) | — |
| 24 | 0.707, 0.707 | as is |
| 36 | 0.5411960, 1.3065630, 0.707 | **inverted** |
| 48 | 0.5411960, 1.3065630, then the same two again | as is |
| 72 | 0.5176381, 0.7071068, 1.9318517, then the same three again | as is |
| 96 | 0.5098056, 0.6013449, 0.8999762, 2.5629153, then the same four again | as is |

The Q values are the Butterworth pole Q's of orders 2, 4, 6 and 8 (24 dB/oct uses `0.707`, not `1/√2`), so the even
slopes are Linkwitz-Riley cascades (a Butterworth filter squared); 12 and 36 dB/oct invert the high band so that the
two bands sum flat (our reading of the structure; the inversion itself is **[DECOMP]**).

### 2.4 The 18 dB/oct structure **[DECOMP `FUN_1801e8190`, slope 0x12 + EXEC]**

No biquads: the input is halved (`x/2` per channel) and runs through two all-pass filters in parallel:
- a first-order all-pass with `c₁ = float(cos ω/(1 + sin ω))`, `ω = f·2π/fs`:
  `y = (y_prev − x)·c₁ + x_prev`;
- a second-order all-pass with `t = cos ω/(1 + sin ω)`, `u = ((t + 1)/(1 − t))·0.5·(1 − cos ω)`,
  `k₁ = float((1 − u)/(1 + u))`, `k₂ = float((k₁ + 1)·cos ω)`:
  `y = ((y₁ − x₂)·k₂ + (x − y₂)·k₁) + x₁` (state y₁, y₂ previous outputs, x₁, x₂ previous inputs).

Low = second-order output + first-order output; high = first-order output − second-order output. (A third-order
Butterworth pair written as half the sum/difference of two all-passes — our reading, not needed for the port.)

### 2.5 Bands, phase compensation, gains, mutes and solos **[DECOMP engine process + EXEC]**

- **2 bands:** crossover A (frequency 0, slope 0) splits the input; low = A's low, mid = silence, high = A's high.
- **3 bands:** crossover A as above; crossover B (frequency 1, slope 1) splits A's high into mid and high; crossover C,
  identical to B, runs on A's low, and the low band is **C's low + C's high**. C thus gives the low band the same
  phase rotation that B gives mid and high, so the three bands sum to an all-pass response.
- Six gains — sends low/mid/high, summed low/mid/high — each glide at every sample **before** the sample is computed:
  `g += (target − g)·k`, `k = float(1 − 0.1^(200/fs))` (the constant is the double just below 0.1).
  `target = level(v)·factor`; per group (sends, summed), if any solo of the group is on the factor is 1 for the soloed
  bands and 0 for the others, otherwise 0 for muted bands and 1 for the rest. A mute change re-targets only its own
  band; a solo change re-targets the whole group (FL's order of side effects, kept).
- Outputs: each send = band × its send gain; summed = `(low·g₄ + mid·g₅) + high·g₆` per channel (float, in that
  order).
- A freshly created engine starts its gains at 0, so FL's plug-in fades in over the first milliseconds **[EXEC]**. The
  engine's constructor sets its own defaults in parameter order (frequency 0 = 0.05, frequency 1 = 0.25, low send
  level = 1.0, everything else 0) before any state arrives **[DECOMP]**.

### 2.6 Floating-point mode **[EXEC: harness MXCSR log]**

The harness never writes MXCSR, yet right after the plug-in is created the thread's MXCSR is `0x9FE0` and after the
last render `0x9FC0`: flush-to-zero and denormals-are-zero are on while the engine runs. Which of the two modules sets
them is not confirmed; LYNX processes the splitter under FTZ/DAZ.

## 3. The FFT (linear-phase) mode

### 3.1 Size and latency **[DECOMP + EXEC]**

`N = 512·2^q` (q = quality 0…5); below 44.1 kHz halved, at or above doubled `round(log2(fs/44100))` times, at most
65 536 — the rule of round 50 §5.1. The plug-in reports **3N/4** samples of latency to FL (host-dispatcher 30 in the
harness log: 1536 for quality 2, 768 for quality 1 at 44.1 kHz) **[EXEC]**; the IIR mode reports 0.

### 3.2 Magnitude tables **[DECOMP + EXEC]**

For each crossover a table of `N/2 + 1` bins at `f_k = k·float(fs/N)`:
- bin 0: 1 for the low table, 0 for the high table;
- otherwise `L = log2(f_k/f_c)` by the engine's approximation (exponent bits plus a polynomial in the mantissa taken as
  an integer), negated for the high table; `v = S·L` (S = slope in dB/oct);
  - `L ≥ 0` (the stop side): `v = −6 − v`, amplitude `2^(v/6)` (0 below −120);
  - `L < 0` (the pass side): `v = v − 6`, amplitude `1 − 2^(v/6)` (1 below −120);
  so each table is −6 dB at `f_c` (as "dB/6 = log₂ amplitude"), falls by `S` per octave beyond it and is the
  complement below it. `2^y` is the engine's cubic approximation on the fraction (`y < 0` as a reciprocal) — the
  same approximation round 50 §5.1 found for Parametric EQ 2's tables.

The low table uses frequency 0 / slope 0; with 3 bands a high table uses frequency 1 / slope 1.

### 3.3 Convolution, changes and start-up **[DECOMP + EXEC]**

- Each table becomes an N/2-tap zero-phase FIR and runs as overlap-add (block N/2, FFT N) exactly as in round 50
  §5.2; LYNX shares that convolver (`peqfl::FftConvolver`, moved out of Parametric EQ 2's linear-phase class).
- There are two convolver sets (low + high each). A new response (any change of §2.2 that marks the filters dirty) is
  taken at a sample only when no cross-fade is running: it goes to the idle set, which is cleared and primed with the
  last N input samples (a history ring), and becomes current; the output cross-fades linearly from the old set over
  `trunc(fadeTime·fs)` samples (§2.2 parameter 7).
- At creation both sets hold an empty response and the first load fades in from it (so FL's plug-in fades in from
  silence over the cross-fade time).

### 3.4 Bands **[DECOMP + EXEC]**

The input also goes into a 65 536-sample delay line read 3N/4 samples back (`d`, the delay matching the FIRs). Per
sample and set: low = the low FIR's output; with 3 bands high = the high FIR's output and mid = `(d − low) − high`;
with 2 bands high = `d − low` and mid = 0. During a cross-fade both sets run and every band is
`new·w' + old·w` with the fade position; then gains and outputs as in §2.5.

## 4. What LYNX does

- **`EffectKind::frequencySplitter`** (appended at the end of the enum; `core/FrequencySplitter.{h,cpp}`): the 26 engine
  parameters as slot parameters (editable and automatable in LYNX; FL automation of the controls maps one to one),
  plus three LYNX-only flags 26…28 ("Out: low/mid/high"). With all three off the effect outputs the **summed** output
  (FL's main output); with any on it outputs the sum of the selected bands' **sends**, low, mid, high in that order —
  the way FL adds the bands sent to one destination.
- **Mixer slots** (`FlEffects.cpp` `effectFromFl`): a readable state (§1: at least 0x48 bytes, version 0…4) becomes the
  exact effect with the summed output; otherwise the old shelf approximation stays and the import report lists the
  slot as "state not readable: approximated".
- **Patcher** (`patcherBranchHead`): bus 0 = the summed output; bus k = the bands whose send destination is k (flags
  26…28 set accordingly); a bus no band is sent to is not a branch. Unreadable states keep the previous crossover
  approximation.
- **Instance start:** a LYNX instance is prepared "settled" — gains at their targets and the FFT response loaded
  without the fade-in — because FL's mixer runs its plug-ins long before playback. The engine also has the "fresh"
  start of §2.5/§3.3 (used by the unit test against the DLL's goldens).
- **Latency:** 3N/4 in FFT mode, reported to LYNX's PDC. LYNX's parallel container (Patcher branches) now reports
  the **longest branch's** latency; the branches themselves are not aligned to each other (FL's Patcher PDC is not
  confirmed).
- The `.lynx` project file stores the kind as `"frequencySplitter"`; the mixer's effect menu offers it.
- Float discipline: no fused multiply-add (`fp-contract off`), FTZ/DAZ during process.

## 5. Verification **[EXEC]**

| Check | Cases | Result |
|---|---|---|
| Python model vs DLL, IIR states (`py/t_model.py`, music, 8000 samples, states' own destinations) | 25 IIR states | summed output bit-identical on all 25; sends of the 2 states that use destinations bit-identical (the one "−0.1 dB" the script prints is its own comparison artefact: that state sends low **and** high to destination 1 and the script compares one band against the sum) |
| Python model vs DLL, FFT states (`py/t_fft.py`, music, 12000 samples) | 6 FFT states (N = 2048) | −126.6 … −148.6 dB (one summed output −∞) |
| LYNX C++ vs DLL (`py/t_cpp.py`, music, 44.1 kHz / 512, 2 s), every band sent to its own destination 1/2/3 | 31 states (25 IIR + 6 FFT) × summed output + 3 sends | **IIR: all 25 bit-identical** on all four outputs. FFT: −123.8 … −149.8 dB (several outputs −∞; silent mids trivially) |
| LYNX C++ vs DLL (`py/t_cpp.py`, noise, 48 kHz / 333, 1 s) | 31 states × 4 outputs | **IIR: all 25 bit-identical**. FFT: −113.4 … −154.6 dB (worst: a mid send of a 3-band state) |
| Parameter changes mid-stream (`py/t_auto.py`: frequency, slopes, level, band count, mode IIR↔FFT, quality, cross-fade; DLL through the front-end's ProcessParam, LYNX through `setParameter`) | 4 sequences (44.1/512, 48/333) | IIR-only sequences bit-identical (both); sequences involving the FFT mode −131.5 … −143.2 dB |
| Reported latency | all states | LYNX = DLL (0 IIR, 1536 FFT quality 2 at 44.1/48 kHz; 768 for quality 1) |
| Unit test `frequency_splitter_engine` (`tests/tests.cpp`) | 7 own states (A–E IIR covering slopes 6…96, 2/3 bands, levels ±18 dB, mutes; F–G FFT, quality 1, cross-fades 30000/10000) from creation, goldens = DLL output (`py/mkgold.py`); plus the mixer slot and a Patcher bus vs the settled engine | IIR within 2·10⁻⁶, FFT within 2·10⁻⁵ (relative); mixer/Patcher paths identical to the engine |

**Verification level stated by this document:** the IIR mode (every slope, 2/3 bands, gains, mutes, solos,
parameter changes) is **bit-exact** against `Frequency Splitter_x64.dll`; the FFT mode is **not** bit-exact — it
leaves a residual of −113 … −155 dB (§6).

## 6. Open items / not bit-exact

- **FFT mode residual:** LYNX convolves in double; the engine uses a single-precision FFT (statically linked IPP, as
  in round 50 §5.4), whose rounding is not replicated. Residual −113 … −155 dB per output on the installation's 6 FFT
  states.
- Solos are engine parameters only (not in FL's state); LYNX exposes them as slot parameters but no FL project can
  set them.
- FL's Patcher latency compensation between branches is not confirmed; LYNX reports the longest branch and does not
  align the others.
- FL's own instance timing (when the plug-in is created relative to playback, hence whether a project starts with the
  gain fade-in / FFT fade-in) is not confirmed; LYNX assumes a long-running instance ("settled").
- A host sample-rate change after the state load: LYNX redesigns at the new rate; the DLL's behaviour in that order
  was not observed here (not confirmed).
- The editor fields of the state (0x48, 0x50, option bits, version ≥ 3 fields) are not used by LYNX (no DSP).

## 7. Maximus v15 linear-phase split / Master Mid — not done in this round

The round was planned to continue with the 4 Maximus demo states that LYNX still approximates (round 40 §8: the v15
linear-phase band split, which hands the split to Frequency Splitter's engine, or Master Mid mode). What exists is
only preparation: `Maximus_x64.dll` was imported into the Ghidra project, a first batch of its functions was
decompiled and disassembled (`out/mx1.c`…`out/mx4.c`, `asm/maximus_all.asm`), and one Maximus state inside a Patcher
was extracted by the survey (`data/mx/`). None of it was analysed, modelled or verified. **Nothing about Maximus v15's
linear-phase split or Master Mid is confirmed by this round**; LYNX's Maximus is unchanged (those 4 states keep the
approximation) and the work remains for a future round. The shared FFT convolver (§3.3) is now a separate class so
that such a round can reuse it.
