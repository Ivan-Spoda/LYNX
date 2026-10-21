# REA round 124 (extras, vmatch124x): her corpus' other plugins with an official trial on this PC

Fork of round 124 (vmatch124). Scope: the third-party plugins of her corpus besides NS1 / Doubler / H-Delay / Fresh Air
(vmatch124), the SSL E-Channel / RComp / DeEsser / CLA-2A (vmatch123) and pitch correction (tune120) that have an
official trial installed here. Mid-round scope change from the coordinator: CLA-76, Pro-Q 4 and Pro-DS moved to
vmatch127. The Pro-Q 4 translation was already done and green, so it lands here (§2). Pro-DS stopped at the
measurements, which are handed off in §5.

Evidence markers: [DATA] read from her project files, [EXEC] black-box runs of the official trials in tools/fxmeasure
(host, feed signals, read outputs, read parameters back), [model] our reading. No third-party binary was opened,
decompiled or patched, and licensing was not touched. Spec dossiers used: docs/specs/valhalla-vintageverb.md and
minor-corpus-plugins.md (spec125). Scripts: `.scratch/` of the vmatch124x worktree, with copies in
tmp/vm124x (pds/, …). Captures: C:\Users\kauan\lynx-test\vm124x (out/ = references, lx/ = LYNX).

## 0. Provenance of the installs and what was measured

| Plugin (her corpus) | slots / main projects | install | measured here |
|---|---|---|---|
| ValhallaVintageVerb | 1106 / 29 | vendor demo (ProgramData\Valhalla DSP, LLC; round 100) | yes (§3) |
| FabFilter Pro-Q 4 | 83 / 5 | vendor trial (Program Files\FabFilter + Uninst.exe; round 99) | yes (§2) |
| FabFilter Pro-DS | 74 / 4 | vendor trial | measured, handed off (§5) |
| ValhallaDelay | 48 / 1 | vendor demo | yes (§6) |
| Waves S1 Imager | 12 / 2 | Waves Central (17.1 shell, activated) | yes (§4) |
| SSL Native Vocalstrip 2 | 12 / 0 (backups) | SSL installer (Program Files\Solid State Logic\PlugIns uninstallers) | **no: hangs on load (licence dialog?), tmp/vm124x/ASK_USER.md** |
| Harrison 32Classic | 1 / 0 (backup) | Harrison installer (uninstaller in Program Files\Harrison\PlugIns) | **no: hangs on load, same note** |
| ValhallaFutureVerb | 6 / 1 | vendor demo | yes (§6) |
| ValhallaSupermassive | 3 / 1 | vendor (free) | yes (§6) |
| Waves CLA-76 | 6 / 1 | Waves Central | moved to vmatch127 |

## 1. Harness

- `vm.py` / `verb.py`: `ref()` hosts the official plug-in in fxmeasure (Waves: a dummy shell plug-in first). It loads
  her raw component state with `loadcomponent`. `ref_demo()` does the same for the Valhalla demos and keeps every
  capture outside the periodic demo mute (45 s of processed audio, round 100 §0). `lynx()` runs the LYNX substitute
  through `fxlynx` with the same state (`SUBST_STATE`, `SUBST_NAME`), i.e. through LYNX's own substitute lookup and
  translation.
- Metrics: null (residual reference − LYNX, dB below the reference), level, 1/3-octave spectrum, 50 ms envelope. For
  reverbs, which cannot null (random modulation), the impulse response is compared instead: octave-band energy in
  10 time windows (0–10 … 3200–6400 ms), RT60 per octave (Schroeder on the STFT band envelope, T20), total energy and
  L/R correlation. Her take ("game over" lead vocal) is also run with her own mix: long-term 1/3-octave spectrum and
  50 ms envelope.
- Takes: her raw lead takes from gf/proj2 (game over, kwe, luisa sonsa), resampled to 48 kHz.

## 2. FabFilter Pro-Q 4 → Facet EQ (landed here; vmatch127 should not redo it)

[EXEC] The component state is "FFBS", version 1, 600 floats. They were read back through the official trial's
parameter API (`get` after `loadcomponent`) for her four states. Band b (0 … 23) sits at 23·b: used, enabled,
frequency (log2 Hz), gain (dB), Q (normalised: 0.025 · 1600^v), shape index (0 bell, 1 low shelf, 2 low cut,
3 high shelf, 4 high cut, 5 notch, 6 band pass, 7 tilt shelf, 8 flat tilt, 9 all pass), slope index (0 = 0, then 6, 12,
18, 24, 30, 36, 48, 72, 96 dB/oct, 10 = brickwall), stereo placement (2 = stereo), speakers, dynamic range, … Globals:
552 processing mode, 555 gain scale (1 = 100 %), 556 output level (FabFilter's level encoding). Facet EQ already uses
the same shape family, Q law and slopes (fitted to Pro-Q 4 in round 99). `fabfilter::fromProQ4` takes the first eight
used, enabled, stereo bands. `translate = pro-q4`.

[DATA] Her four states: a 153.5 Hz bell −1.5 dB Q 1 (64 slots); a low cut 508.8 Hz + high cut 9868 Hz, 12 dB/oct
(9); a low cut 82.4 Hz Q 0.47, 24 dB/oct (6); bells 116.8 Hz −4.56 dB Q 4.27 + 9899 Hz −0.12 dB Q 1.96 (4). All
zero latency.

| state | before (substitute flat): null on her 3 takes | after: null on her takes | after: white / pink noise |
|---|---|---|---|
| bell 153 Hz | −22 … −26 dB (spec RMS 0.6 dB) | −108 … −109 dB | −109 / −112 dB |
| LC 509 + HC 9.9k | +4.4 … −1.6 dB (spec RMS 5–14 dB) | −62 … −65 dB | −47 / −51 dB |
| LC 82 Hz 24 dB | −4 … −8 dB (spec RMS 4–8 dB) | −102 dB | −102 dB |
| bells 117 Hz + 9.9k | −25 … −31 dB (spec RMS 0.8 dB) | −84 … −85 dB | −65 / −73 dB |

Sweeps, one band through every shape × 4 frequencies × gains / Q / slopes, 144 settings: median worst
deviation 0.05 dB, median null −47 dB. The off-corpus residuals are in Facet's shapes, not in the translation:
24 dB/oct shelves and tilt (±0.8 dB at +6 dB, ±1.7 dB at −12 dB, the tilt up to ±4 dB near the corner), the
6 dB/oct high cut above 10 kHz (−1.1 dB at 16 kHz), and band pass / notch above 10 kHz (≈ 1.2 dB). Her states use
none of these. Golden test: `pro_q4_states_translate_onto_facet_and_match_the_reference_round124` (the reference's
exact |H| at 11 frequencies per state, within 0.1 dB).

## 3. ValhallaVintageVerb → Epoch Verb (Cathedral and Concert Hall refitted)

[DATA] 55 distinct settings and 1106 slots across the whole corpus. Cathedral / 1980s "Detuned & Big" has ~740 slots
and Concert Hall / 1980s "Big Shimmerspace Huge" ~241. "Default" (Concert Hall or Plate, 1970s) has 88, Sanctuary /
Now 22, and Palace, Chaotic Neutral and Room have a few each. Every setting was captured on the official demo
(`vv_cmp.py`, outside the demo's mute windows): a 16 s wet impulse response (Mix forced to 100 %) and her take with
her own mix.

[EXEC] What the reference does at her settings, and Epoch did not:
- Cathedral's mid-band RT is not proportional to Decay: 1.15 / 2.2 / 2.72 / 3.70 / 5.07 / 7.73 / 11.86 / 13.68 s at
  Decay 0.53 / 1.0 / 1.99 / 2.9 / 4.36 / 7.37 / 12.0 / 14.0 s. That is about +0.7 s below 8 s and ×1.0 at 12–14 s.
  Epoch's law (Decay × 1.3) was 12–45 % long at her decays. Concert Hall: 2.27 / 3.17 / 3.70 s at 1.78 / 2.67 / 3.28 s.
- Time–frequency: Epoch had a hole 25–50 ms after the impulse (−17 … −21 dB in every band, from its line lengths).
  Cathedral's energy density is flat for ~400 ms, then decays.
- The 1980s colour's loop band limit (14 kHz one-pole) made the 8–16 kHz tail die 2–3 × too fast (RT 8 kHz ratio
  0.69). The 1970s colour is a brick wall at ~12 kHz: −26 dB at 11.3 kHz, −43 at 12.7 kHz, −88 at 14.3 kHz
  (an internal rate of ~24 kHz [model]). Epoch had a first-order 10 kHz cut, so 16 kHz in her takes was 70 dB too loud.

LYNX (core/LynxVerbs.cpp): the engine gets a steep band limit of up to 16 Butterworth sections and two tone filters
(high shelf, bell), plus the existing low shelf. Epoch gets a per-mode voicing table (bass-multiplier exponent,
high-RT multiplier, loop-bandwidth scale, early-tap decay, three tone filters) and per-mode measured RT-per-Decay
knots. The 1970s colour is now a 16-section 11.8 kHz band limit with no first-order cut. Cathedral's and Concert
Hall's rows and voicing were fitted by coordinate descent (`.scratch/vv_fit.py`, harness `vfit` = LynxVerbs.cpp
with writable tables). The objective was the octave-band × 10-window energy grid plus RT per octave on 9 Cathedral
and 8 Concert Hall settings of hers. Other modes are unchanged. `tests/LynxVerbTests100.inc` (round 100's
measurements) still passes; Concert Hall's own pre-delay was set back to keep its 5.5 ms onset.

Before → after, all 55 settings weighted by slots (reference vs LYNX):

| metric | before | after |
|---|---|---|
| IR 1/3-octave energy spectrum, RMS of the difference | 5.27 dB | 2.86 dB |
| total wet energy, mean abs difference | 1.84 dB | 0.88 dB |
| RT60 per octave (125 Hz–8 kHz), RMS log error | 17.9 % | 8.4 % |
| her take, long-term 1/3-octave spectrum RMS | 2.31 dB | 1.48 dB |
| her take, level | 0.85 dB | 0.61 dB |
| her take, 50 ms envelope RMS (random modulation) | 2.38 dB | 2.53 dB |

Per main setting (RT ratio 125 … 8k LYNX / reference; take level / spectrum RMS):

| setting (slots) | before | after |
|---|---|---|
| Detuned & Big, Decay 4.36 s, mix 42 % (228) | RT 1.23 1.29 1.19 1.12 1.07 0.92 0.69; etot +1.9; take −0.17 / 0.82 dB | RT 1.13 1.21 1.06 0.98 0.99 1.00 0.97; etot −0.5; take −0.26 / 0.70 dB |
| Big Shimmerspace Huge, 2.67 s, 100 % (102) | RT 1.00 0.96 1.00 0.97 0.96 0.89 0.72; take −1.46 / 2.23 dB | RT 1.03 1.04 1.02 1.02 1.04 1.10 1.06; take +0.10 / 1.78 dB |
| Detuned & Big, 7.37 s, 62 % (86) | RT 1.40 1.41 1.30 1.23 1.08 0.91 0.65; take −0.49 / 1.88 | RT 1.16 1.08 1.06 1.00 0.97 0.98 1.05; take −1.54 / 1.54 |
| Detuned & Big, 1.99 s (82) | RT 1.14 1.18 1.07 1.01 0.99 0.89 0.73 | RT 1.06 1.17 1.05 1.00 0.98 0.99 0.93 |
| Default, Concert Hall 1970s (30) | etot −3.4; take −3.46 / 16.1 dB (16 kHz) | etot −2.8; take −2.26 / 4.23 dB |

Not matched (honest limits):
- No null is possible: both reverbs modulate their delay lines (VintageVerb's "dark and noisy" modulation is random).
  The 50 ms envelope of the take stays 2–2.5 dB RMS apart, set by the random modulation, not by level.
- Cathedral's bass (125–250 Hz) RT is still 6–20 % long, and its onset is 4.6 ms vs 1.4 ms.
- Not refitted: Plate / 1970s (29 slots: etot −6 dB, take −4.9 dB), Sanctuary / Now (22: −2.4 / −4.8 dB), Palace,
  Chaotic Neutral, Room. Same method; next round.

## 4. Waves S1 Imager → Stereo Shaper

[DATA] Her two states (6 slots each, in two main projects): `<PluginName>S1 Imager`, width 1.22 and 3.00, everything
else neutral. [EXEC] RealWorld layout (each control moved alone, the state saved): 0 gain dB, 1 width, 4 rotation
deg, 5 asymmetry deg, 13 swap, 14 / 15 invert L / R, 16 M/S input. Laws (white noise, least squares, 41 width points,
rotation, gain): a frequency-flat 2 × 2 matrix with no latency, out = gain · rotation · width · in.
- Width w (the plug-in keeps two decimals; v → w = v + 2v²): below 1, mid × 1 and side × w. Above 1, side / mid = w
  with mid = √(0.625 + 0.375 / w²), exact on every measured point (w = 3: L = 1.63299 L − 0.81650 R).
- Rotation φ: [cos φ, −sin φ; sin φ, cos φ]. Gain: −24 … 0 dB.
- Asymmetry is a different, non-symmetric matrix (measured at ±45° and ±90°, not modelled). Swap / invert / M/S input
  were not measured. All of these are neutral in her states. A state that uses them stays untranslated.

LYNX: FL's Stereo Shaper (LYNX's exact engine, a full 2 × 2 matrix) receives the matrix through its knob law
(|g| = (3^|x| − 1) / 2). `translate = waves-s1-imager`.

| state | before (no substitute: dry) | after |
|---|---|---|
| width 1.22 | null L −24.8 / R −13.1 dB, level −0.3 / −0.5 dB | null −84.8 / −84.7 dB (noise), −85.3 / −82.8 dB (take) |
| width 3.00 | null L −7.2 / R −2.0 dB, level −4.5 / −7.8 dB | null −92.1 / −92.0 dB (noise), −95.7 / −88.3 dB (take) |

Sweeps over width 0 / 0.37 / 0.8 / 1 / 1.5 / 2.2 / 3 × rotation 0 / −30 / +22.5° × gain 0 / −6 dB (42 settings), all
nulls ≤ −72.9 dB. Golden test: `waves_s1_imager_translates_onto_the_stereo_shaper_matrix_round124` (measured
matrices, within 2·10⁻⁴).

## 5. Hand-off to vmatch127: FabFilter Pro-DS (measured, nothing landed)

Scripts in tmp/vm124x/pds/ (pds_meas*.py, pds_ana.py, pds_fit.py, pds_look.py, proto_ds.cpp, vm.py). Captures:
C:\Users\kauan\lynx-test\vm124x\out\pds0, pds1 (her takes, stairs, bursts), pds2…pds6 (probes).
- [DATA] Her two states: "FabF" version 2, preset name "Default Setting", 20 floats. Single Vocal, wide band,
  threshold −43.71 / −36.0 dB, range 6 dB, lookahead 12 ms on, HP 7000 Hz, LP 14 kHz, stereo link 100 % mid.
  Units in the state: threshold normalised (dB = −60 + 60 v), range dB, lookahead ms, HP / LP normalised, levels
  normalised (0.5 = 0 dB).
- [EXEC] Latency 720 samples at 48 kHz (15 ms). Wide band: out = in(delayed) · g(t).
- Static law, 8 kHz sine at L dBFS peak: GR = range · (L − thr + 4.1) / (0.1 − thr). It switches on at
  L ≥ thr + 3.9 (a step of about 1 dB), keeps hysteresis down to thr − 4.1, and caps at range from −4 dBFS. Same law at
  thresholds −56 / −43.7 / −36 / −25 and ranges 3 / 6 / 12 / 24.
- Detector at −20 dBFS: 5 kHz −0.04 dB GR (below the switch), 5.5 kHz −2.5, 7 kHz −3.5, 8 kHz −3.8, 10 kHz −4.05,
  14 kHz −3.55, 16 kHz −3.0, 18 kHz −2.4. That is roughly a 4th-order HP at the HP frequency times a 4th-order LP at
  the LP frequency.
- Single Vocal gating: pink noise (−40 … −10 dBFS) never triggers it. A steady 8 kHz tone triggers it, alone or with a
  quieter tone that starts at the same time (300 Hz up to ~2–6 dB below the 8 kHz level). It does not trigger when the
  low tone was already playing before the 8 kHz onset, even 10 dB below. Once on, it stays on when a low tone is added.
  The decision is history-dependent. [model] It is an onset- or voice-relative classifier, not a level ratio.
- Timing, 8 kHz step −45 → −10 dBFS: GR starts ~11 ms before the step (lookahead) and reaches full at the step. It
  falls from ~7 ms before the step's end to ~10 ms after. A 3 ms burst peaks at −3.9 dB (full is −5.2).
- On her takes GR is sparse: max −3.8 dB, mean −0.04 dB. The current Hush defaults reach −7.8 dB (GR envelope
  0.40 dB RMS off, null −23 dB). The prototype (proto_ds: HP/LP detector, ratio gate HF − LF ≥ −7 dB with hysteresis,
  4 ms looked-ahead level, 2nd-order gain smoothing) reaches 0.06–0.19 dB RMS over all 2 ms frames and 0.6–1.0 dB
  RMS on the GR frames. Fitted parameters: `best 0.01,1.71,2.95,1.765,12.08,0.2683,-7.054,-2.263` (pds_fit.py order).

## 6. ValhallaDelay, Supermassive, FutureVerb (measured, not changed)

[EXEC] Baselines (`gen_cmp.py`; captures out/Valhal*, ValhallaS*, ValhallaF*):
- ValhallaDelay "Default" (Tape, 300 ms, 2 slots): echoes at the same times, but Echoform is −2 dB on the first echo
  and −1.2 dB per repeat more. "PITCH-Amethyst" (RevPitch +7 st, Ratio style, 1/1 note, feedback 95 %, 45 slots in
  "eu"): the reference's reversed grains start sounding 325 ms after the impulse; Echoform's reverse grains start at
  830 / 1660 ms. Echoform's reverse model does not follow the reference's grain timing. Not matched.
- Supermassive (Lyra 1/4 note, 3 slots): Nebula is 12 dB quieter on the wet path and nearly mono (correlation 0.94
  vs 0.45).
- FutureVerb (Room 0.4 s, 6 slots): Horizon misses the 10–25 ms energy and its RT is 1.2–1.75 × long. A fit of
  Horizon's Room row diverged (the reference's tail is too short for the RT estimate), so nothing was changed.

## 7. SSL Native Vocalstrip 2 and Harrison 32Classic

Both are installed through their vendors' installers, but neither opens in the measuring host. Each load hangs
for more than 90 s, most likely on a PACE / licence or registration dialog that waits for a click
(tmp/vm124x/ASK_USER.md). They stay on Ridge Strip, untranslated. Both are minor and only in backups.

## Tests

`tests/Match124xTests.inc`: Pro-Q 4 golden curves (4 states) and the S1 matrices (7 settings + the untranslated
case). `LynxVerbTests100` passes with the refitted Epoch.

## Where I stopped

- Landed: Pro-Q 4 → Facet (§2), S1 Imager → Stereo Shaper (§4), Epoch's Cathedral / Concert Hall refit, the 1970s
  brick wall, the voicing / RT-knot tables (§3).
- Next: refit Plate / 1970s and Sanctuary / Now with `vv_fit.py` (same harness), then Cathedral's bass RT and onset.
  For ValhallaDelay, measure the RevPitch grain timing (impulse and tone bursts at her setting) and model it in
  Echoform. Fix Nebula's level and width for Lyra. Fix Horizon's Room early energy (the fit needs an RT estimate
  for short tails).
- Needs the user: activate the SSL Vocalstrip 2 and Harrison 32C trials (ASK_USER.md).
- Hand-off: Pro-DS (§5) and CLA-76 go to vmatch127.
