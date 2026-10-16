# Behavioural analysis of FL Studio 2026: round 78 (REA) — Fruity Vocoder raw controls, one Delphi math replica

> **Nature of this document.** Follow-up of `REA_rodada66.md` §6–§7 and `REA_rodada70.md`. Round 66's Fruity Vocoder
> reference recording for its first state (32 bands, order 3, inverted, formant shift) differed from round 70's engine
> by up to 0.0148, and the test only printed the difference. This round finds which side was wrong, fixes it, and
> merges the two Delphi run-time math replicas the two rounds wrote in parallel. *Clean-room* reverse engineering,
> behavioural only: no Image-Line code or pseudocode was copied; what is here are behaviours in our own words.
> Addresses (`FUN_xxxxxxxx`) only trace the evidence. No Image-Line preset or audio data went into the repository.
>
> **Rule for this round: no speculation.** Every value comes from decompilation (**[DECOMP]**), from running the
> installation's DLL unmodified (**[EXEC]**), or is marked "not confirmed".

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled code (REA MCP, Ghidra 12.1.4 provider) |
| **[EXEC]** | result obtained by running the installation's own DLL (without modifying it) |

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `/home/kauan/.claude/jobs/f8789b49/tmp/r78/`.
- **Target:** `Plugins/Fruity/Effects/Fruity Vocoder/Fruity Vocoder_x64.dll`, SHA-256
  `bfc3bec327dda1f943cdd654220b9c0fa3dffa183080a4a8c56361fea63961b6` (= rounds 19/20/66/70).
- **Tools:** REA MCP (`open_binary`, `batch_decompile`, `procedure_pseudo_code`, `read_bytes`) on a copy of the DLL
  (`dll/fv.dll`); the EXEC harnesses of round 66 (`r66/run`, through `r66/py/lpx.py`) and round 70 (`r70/run`, through
  `r70/py/fx.py`, incl. its in-place `fcall` of the DLL's own math routines).
- **Drivers (our own code):** `py/a.py` (both harnesses on round 66's two golden states), `py/oor.py` (out-of-range
  controls, DLL vs LYNX), `py/mathdll.py` (the DLL's Exp / Ln / SinCos on wide ranges), `cpp/fvcmp` (LYNX engine,
  the source of `r70/cpp/fvcmp.cpp`), `cpp/fv_noclamp.cpp` (the hypothesis test), `cpp/cmpmath.cpp` / `cpp/cmp2.cpp`
  (replica vs replica), `cpp/mt` (replica vs the DLL's routines, source `r70/cpp/mt.cpp`), `cpp/lp_old` / `cpp/lp_new`
  (Love Philter engine before / after the merge, source `r66/cpp/lpcmp.cpp`).

---

## 1. The round-66 vocoder golden: the recording was right, the engine clamped **[EXEC + DECOMP]**

**Which side is wrong [EXEC].** The 8192-frame input of `fruity_vocoder_round66_goldens` and the two states (hex as
in the test) were run again through both DLL harnesses (`py/a.py`): round 66's and round 70's harness give the same
output to the bit for both states, and their samples at 777 / 1555 / 2222 / 3333 / 3999 / 4100 equal the goldens in
the test. The recording is the DLL's output. Round 70's engine on the same state file (`cpp/fvcmp`, built from the
current sources) differs from frame 6 on (max 0.0364 over the whole run) for state A, and is identical for state B.
So the engine was off, not the golden.

**Why.** State A's twelve controls are `3000, 50000, 100, 1, −700, 120, 5, 300, 0, 140, 60, 160`: the attack is
**5 ms**, below the knob range (10…1000 ms). `fvocoder::Engine::setState` clamped every control to its knob range, so
it ran with a 10 ms attack. With the clamp removed (`cpp/fv_noclamp.cpp`), state A is bit-identical over all 8192
frames.

**What the plugin does [DECOMP].**

- State load `FUN_006cf810`: after the band block and the levels, the 48 bytes of controls are read straight into the
  control array (`+0x120`); then, for each control, the value is read back (ProcessParam with flag 2) and set again
  (flags `0x11`). No range test on the way.
- ProcessParam `FUN_006d08c0` (VMT `0x6cf210` + `0x30` → `0x6d08c0`, read with `read_bytes`): with flag `0x20` (a
  host value in 0…2³⁰) the raw value is `min + Round(v·2⁻³⁰·(max − min))`, i.e. inside the range; with flag 1 the
  value given is stored as is into `+0x120 + 4·index` and the set-up `FUN_006d03a0` runs. Flag `0x10` only updates
  the editor's control; the returned value is not changed by it. So a raw value from a state (or from a
  non-normalised ProcessParam) is used unclamped.
- Set-up `FUN_006d03a0` uses the stored integers directly (band gain `wet·1500/width`, width factor, scale
  `Round(10000^(p/128) − 1) + 1`, attack/release `exp(ln 0.01/(ms·rate·0.001))`). Invert is tested as `> 0` and Hold
  as `≠ 0` (bank builder `FUN_006d00b0` / render `FUN_006d0fb0`), which LYNX already did.

**Wider check [EXEC, `py/oor.py`].** Nine more states with controls outside the knob ranges — attack 2 ms, release
3000 ms, width 300 % and 2 %, scale 200, formant +2000 / −1500 cents, invert 3, hold 2, FreqMax 80000, mix levels
200/190/250 — DLL vs the fixed engine: **9/9 bit-identical** (all finite).

**What LYNX does now.**

- `fvocoder::Engine::setState` keeps the raw controls; it clamps only to wide guard bounds that keep the arithmetic
  finite and defined (width ≥ 1 %, attack/release ≥ 1 ms, scale 0…512, FreqMin/Max 0…262144, formant ±120000,
  levels ±1280). What the plugin does outside those bounds (division by zero, integer-rounding overflow) is not
  modelled — **not confirmed** and not reachable from FL's knobs.
- The LYNX slot shows the controls as knobs in their ranges. New `fvocoder::rawFromKnob`: while a knob still shows
  the saved value (clamped to the knob range), the effect uses the saved raw value; once the user moves it, the knob's
  value. The mixer's band-level editor serialises the state through the same rule, so drawing levels no longer
  overwrites such a control with its clamped value.
- `fruity_vocoder_round66_goldens` is a hard exact check again (both states, all six samples, both channels);
  `fruity_vocoder_engine` gained four `rawFromKnob` checks.

---

## 2. One Delphi run-time math replica **[EXEC]**

Round 66 wrote `core/DelphiRuntime.h` (`lynx::delphi`, header-only: exp, expm1, ln, sincos, sin, cos, intPower,
power, sinh, tanh) for Love Philter; round 70 wrote `core/DelphiMath.{h,cpp}` (`lynx::dmath`: exp, ln, sinCos, sinh)
for the Fruity Vocoder. Both replicate the same Delphi Win64 RTL routines. Differences between them, found by running
both on 3 M random arguments per function (`cpp/cmpmath.cpp`):

| Function | Range where they differ | Which one matched the DLL |
|---|---|---|
| exp | results above 2¹⁰²³ (k > 1023) and subnormal results: round 66 fell back to the C library | round 70 (verified against the DLL in round 70) |
| ln | subnormal arguments: round 66 fell back to the C library | round 70 |
| SinCos | 5π/4 < \|x\| < 4194304: round 70 fell back to the C library | round 66 (its second reduction, verified against the Love Philter DLL in round 66) |

Everywhere else they were bit-identical (same algorithms, same constants; the e^(j/64) − 1 tables agree on
j = −24…24 and round 66's j = −45…45 table equals e^(j/64) − 1 rounded to a double plus remainder, checked with
70-digit decimal arithmetic).

**The merge.** `core/DelphiMath.{h,cpp}` (`lynx::dmath`) now holds the union: round 70's exp and ln (full range),
round 66's SinCos reduction above 5π/4, expm1, sin, cos, intPower, power and tanh; the table has j = −45…45. Built
without contraction (pragma in the `.cpp`), out of line. `core/DelphiRuntime.h` is removed; Love Philter calls
`dmath::` (same function names; `sincos` was not used there).

**Verification.**

- Unified replica vs both old replicas, 2 × 2 M random arguments per function over their verified ranges
  (`cpp/cmp2.cpp`: exp −760…720, ln 2⁻¹⁰⁷⁰…2¹⁰²⁰ incl. subnormals, SinCos up to ±4194304, expm1, tanh, sinh, power,
  intPower): **0 differences**.
- Unified replica vs the Fruity Vocoder DLL's own routines called in place (`py/mathdll.py` + `cpp/mt`, FTZ/DAZ as
  in the plugin): Exp 200 000 arguments (−760…720, incl. 700…710 and −746…−700), Ln 200 000 (2⁻¹⁰²¹…2¹⁰²³ and 0…4),
  SinCos 180 000 (up to ±4194304, and 3.9…20): **0 differences** — this also confirms the SinCos path above 5π/4 on
  the Vocoder's copy of the routine.
- Love Philter engine before vs after (`cpp/lp_old` vs `cpp/lp_new`) on the 43 installation states round 66 surveyed,
  4 s of signal: **43/43 bit-identical**, same run time.
- All goldens of the unit tests (Love Philter, Fruity Vocoder, `delphi_math_matches_plugin_runtime`) unchanged:
  `scripts/build-local.sh` with the FL Data directory, 0 failures (§3).

---

## 3. Test result

`scripts/build-local.sh "/mnt/c/Program Files/Image-Line/FL Studio 2026/Data"`: **1 167 600 checks, 0 failures**
(on main 763ec35 + this round); Windows build and `LYNX.exe --selftest` through `tmp/coord/land.sh`.

## 4. Open items

- Controls beyond the guard bounds of §1 (zero width or time, scale > 512, …): the plugin's arithmetic there
  (divisions by zero, `cvtsd2si` overflow) is not modelled — not confirmed, unreachable from FL's own knobs.
- The other open items of round 70 §8 stand (the word after the block in FL's output buffer; host blocks > 512).

## Summary table

| Item | Result |
|---|---|
| Round-66 golden vs round-70 engine | the golden is right; the engine clamped a 5 ms attack to 10 ms — fixed, test exact again **[EXEC + DECOMP]** |
| Raw controls outside knob ranges | used unclamped by the plugin; 9 extra states bit-identical **[EXEC + DECOMP]** |
| Delphi math replicas | merged into `core/DelphiMath`; 0 differences vs both old replicas and vs the DLL's routines **[EXEC]** |

## Evidence index

| Topic | Functions | Evidence |
|---|---|---|
| State load, ProcessParam, set-up | `FUN_006cf810`, `FUN_006d08c0` (VMT `0x6cf210` + `0x30`), `FUN_006d03a0` | REA `batch_decompile`, `procedure_pseudo_code`, `read_bytes` |
| Invert / hold tests | `FUN_006d00b0`, `FUN_006d0fb0` | round 70 dump `r70/out/fv.c` |
| Golden re-recording, out-of-range states | — | `py/a.py`, `py/oor.py` (both harnesses, EXEC) |
| Math routines | `FUN_00408810` Exp, `FUN_00409870` Ln, `FUN_004095e0` SinCos | `py/mathdll.py`, `cpp/mt` (EXEC, in place) |

## Where I stopped / next steps

Done: §1 fix + tests, §2 merge + verification, docs (`STATUS.md`, rounds 66/70 notes). Landing through
`tmp/coord/land.sh voc78`.
