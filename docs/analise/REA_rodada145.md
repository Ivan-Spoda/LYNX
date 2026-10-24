# REA round 145 — Complete Access: SSL Native Vocalstrip 2 measured in full, Ridge Strip's Vocal strip voicing

Branch `feature/vox145` (from main after round 138). English-first; clean-room. Evidence markers: [DATA] read from her
project files, [EXEC] measured (our host / LYNX), [model] our reading, not confirmed.
THIRD-PARTY RULE: official trials only, black-box only — hosted in our own host (`uiview.exe`, the public VST3 API:
parameter names / ranges / texts, `getStateInformation` / `setStateInformation`, audio in and out), screenshots of the
vendor's window as a user sees it; nothing decompiled, patched or copied. No vendor app was opened this round (the
Complete Access licences were already active since round 138).

## 1. Which Complete Access plugins her projects use [DATA]

Plugin names in all 907 copied projects (`tmp/gf/all`, `proj2`, `other`; ASCII and UTF-16 strings of the .flp):

| Plugin (vendor) | slots | projects | LYNX before | this round |
|---|---|---|---|---|
| Fresh Air (Slate) | 1251 | 706 files | Aether dynamic model (round 124, −38 … −53 dB null on her 24 states) | unchanged |
| SSL Native Vocalstrip 2 (SSL) | 12 | `okkk` backups | Ridge Strip, **not translated** (the trial hung in round 124) | **measured, new voicing, translated** (§2) |
| Harrison 32Classic channel strip | 1 | `okkk` backup | none | Ridge Strip by role (§4) |
| LiquidSonics, sonible, other Slate / SSL | 0 | — | none | role entries (§4) |

## 2. SSL Native Vocalstrip 2 measured [EXEC]

The trial now loads (Complete Access active): 35 VST3 parameters with their own ranges and texts (`D:\lynx-work\vox145\
vs2_params.txt`), latency 0 (more with look-ahead, §2.5). Raw data on D: so it stays usable after the trial:
`D:\lynx-work\vox145\vs\in` (signals, `segs.json`, copies of her three takes), `vs\states` (her two states wrapped for the
VST3 trial and as found in the project), `vs\out\<campaign>\<tag>.wav` + `.txt` (commands, every control read back,
latency) + `index.tsv`, `vs\out\lynx_<campaign>` the same configurations through LYNX; screenshots in `shots\`.
Scripts: `tmp/vox145/py` (`mksig145*.py` signals, `vscamp.py` campaigns + runner, `ana145.py` / `tana.py` / `fiteq*.py`
/ `fitds.py` / `fitdp.py` analyses, `vsmodel.py`, `matched.py`, `lynxvs.py` LYNX side and comparison).
Campaigns (≈ 260 renders): static (4 thresholds × 4 ratios × knee, 1 kHz / 100 Hz / 5 kHz stairs −60 … 0 dBFS, make-up),
time (7 attacks, 6 releases on bursts; sine vs square; one channel), misc (expander, drive, gains), eq / eqfine (impulse
responses of the high-pass, notch and high band, every 2 dB of notch gain and every 1 dB of high-band gain), dsdp /
dsdet / dstime (de-esser and de-ploser on sibilance / plosive signals, stairs at 12 frequencies, alternating tones,
pink noise, look-ahead), order, her (her states on her takes and on stairs / bursts).

### 2.1 Control laws (its own parameter texts)
Thresholds −48 … 0 dB (compander, de-esser, de-ploser), expander −96 … 0, linear; attack 0.1 … 100 ms and release
0.5 … 2000 ms, linear; ratio 1 … 50 = 1 + 49 (250^v − 1) / 249 (max error 0.05 against its 200-step texts); make-up
0 … 48 dB; input / output −36 … +12 dB; de-esser 1 … 20 kHz, de-ploser 20 … 250 Hz, high-pass 30 … 300 Hz, notch
200 Hz … 10 kHz, high band 1 … 20 kHz (log); notch gain −36 … +12, high band ±12 dB. Process order: 24 steps =
the lexicographic permutations of De-esser, De-ploser, EQ, Compander (her 0.478 = DpCoEqDs).

### 2.2 Compander
- Compressor: **a peak detector on the louder channel** (sine and square of equal peak: the same reduction; a sine in
  one channel reduces both), branching one-pole in the linear domain, coefficients 1 − e^(−1 / (t · fs)) for the attack
  and release controls — the model reproduces every burst within 0.00–0.04 dB RMS. Static curve: textbook hard knee
  (threshold and ratio exact); soft knee = the quadratic knee, width 11.75 dB (RMS error 0.03 dB over 16 settings).
  No side-chain filter (100 Hz / 1 kHz / 5 kHz identical). Make-up is plain gain.
- Expander (before the compressor, active when its threshold is above −96): 5 dB of reduction per dB below the
  threshold + 1.16 dB (1:6), its own fast detector (0.1 ms / 25 ms; 0.74 dB RMS on bursts).
- Drive (after the make-up): **y = 1.5 u − u³ / 8 for |u| ≤ 2, ±2 beyond** — read sample by sample from a sine driven
  into it (memoryless, no oversampling); odd harmonics only, +3.5 dB at low level.
- `reset()` does not clear the detectors (a host detail: the first second of each render is left out of the nulls).

### 2.3 EQ (impulse responses)
- High-pass: the cookbook 2nd-order high-pass, Q 1 (0 dB at the corner, +1.25 dB at 1.41 × the corner): exact.
- Notch and high band: **peaking filters with the analog prototype's magnitude** (fits ≤ 0.01 dB below 5 kHz; the
  cookbook bilinear form is 0.4–1.5 dB off at high frequencies) and a Q that follows the gain. Notch: a measured table
  (boost: Q = 18.8 / A; cut: 3.4 at −2 dB, 12.0 at −18 dB, 8.5 at −36 dB), the same at 200 Hz … 10 kHz. The "high
  shelf" is a wide **bell** (Q = 0.852 · e^(−0.0561 |dB|), 0.43 at ±12 dB) — at 20 kHz it looks like a shelf.

### 2.4 De-esser and de-ploser
- **Level-independent**: the reduction for a steady tone is the same from −60 to 0 dBFS. The detector compares the
  level of the band with the level of the whole signal (both peak followers): de-esser band = 3rd-order Butterworth
  high-pass at the frequency, reduction 2/3 of the excess of (band − full) over the threshold; de-ploser band = a
  3rd-order low-pass at 0.78 / 0.84 / 0.92 × the frequency (50 / 100 / 250 Hz) with a passband lift of 8.3 / 5.2 /
  2.3 dB, reduction 3/4 of the excess. Times fitted on alternating tones: de-esser 1.5 / 40 ms, de-ploser 3 / 40 ms.
- SPLIT: out = LP(x) + g · (x − LP(x)) for the de-esser (3rd-order low-pass at the frequency; pink noise: 0.02–0.14
  error) and HP(x) + g · (x − HP(x)) for the de-ploser.
- Look-ahead delays the audio 1 ms (de-esser) / 10 ms (de-ploser) and the plugin reports it as latency — **also while
  the module is off** (her state: de-ploser off with look-ahead on = 441 samples at 44.1 kHz).

### 2.5 LYNX: Ridge Strip's Vocal strip voicing
`core/VoxStrip.h/.cpp` (LYNX's own code; the magnitude-matched peaking filter follows M. Vicanek, "Matched Second Order
Digital Filters", 2016). Ridge Strip gets 34 appended parameters (Vocal strip switch, input, output, process order, the
de-esser's and de-ploser's on / frequency / threshold / split / look-ahead / listen, the EQ's switches, frequencies and
gains, the compander's on / expander threshold / threshold / ratio / attack / release / make-up / soft knee / drive), in
the reference's own laws; older projects load the switch off = the previous sound. The voicing reports its look-ahead
latency. The editor shows the voicing's own panel (level, compander, de-esser and de-ploser side by side, equaliser,
order menu, GR meters) while it is on; a "Vocal strip voicing" switch at the bottom flips between the panels.
`translate = ssl-vocalstrip-2` reads the XML state (the A or B setting that plays) one to one; bypassAll = slot off.

LYNX vs the trial (null over the signal after the first second; `D:\lynx-work\vox145\vs\lynx1.txt`):

| Test | null |
|---|---|
| **her state on her takes (go / kwe / lu)** | **−66.9 / −61.5 / −60.4 dB** (level ±0.00 dB) |
| her state on stairs | −56 dB |
| compressor stairs, 32 settings + make-up | −53 … −70 dB |
| bursts, 7 attacks × 6 releases | −52 … −88 dB (two −42 dB: the trial's carried-over envelope) |
| drive, gains | below −140 dB |
| expander stairs / bursts | −47 … −63 / −28 … −52 dB |
| high-pass; notch (200 Hz … 5 kHz) | below −139 dB; −58 … −105 dB |
| notch at 10 kHz; high band 1 … 3 kHz; 5 … 20 kHz | −25 … −55; −33 … −62; −16 … −31 dB |
| de-esser / de-ploser broadband | −13 … −25 dB (level within 0.3–1.4 dB) |
| de-esser / de-ploser split | −1 … −22 dB (level within 0.1–2.2 dB) |

## 3. Auto-Tune Pro before the trial ends (2026-10-25 21:00) [EXEC]

Round 138's open item "tracking / input type on noisy or breathy voices": the official trial at Retune 0 on the
round-138 test signal with white noise at 20 / 10 dB SNR and with breath noise (1–8 kHz, −12 dB, following the
voice), Tracking 0 / 25 / 50 / 75 / 100 and the five Input Types, and two of her takes with noise at −40 dBFS
(`D:\lynx-work\vox138\at\out\track145`, configs `tmp/vox145/at/at145cfg.py`, analysis `anatr.py`): **neither
control changes the result** (median distance of the output from the nearest semitone 0.1 c clean, 0.1 c at 20 dB SNR,
1.1 c at 10 dB, 0.7 c with breath, for every setting). LYNX Tune on the same signals (`out\lynx_track145`): 0.3 /
1.6 / 3.5 / 4.6 c — the same notes, but its pitch estimate jitters more in noise, so the held note wobbles by a few
cents more. Not changed this round (LYNX Tune's response laws were fitted in round 138; a steadier estimate in noise is
the next item). The upper / lower tie (§3.1 of round 138) stays: LYNX Tune's `nearestNote` serves the Waves Tune role
too, which takes the lower note.

## 4. The rest of Complete Access by role
Not in her corpus and not measured: `[complete-access-*]` entries map Slate (FG-X 2, VBC, FG-116 / 76 / 2A / A, Infinity
EQ, AirEQ, FG-DS 902, gates, VerbSuite, Stellar Echo, Heatwave / Revival, MetaTune), SSL (4K G, Bus Compressor 2, G3,
X-Comp / ValveComp / EQ 2 / Limit / Gate / Echo / Delay, DeEss, FlexVerb / PlateVerb / SpringVerb / GateVerb, Fusion),
LiquidSonics (Lustrous Plates, Seventh Heaven, Cinematic Rooms, Illusion), sonible (smart:EQ / comp / limit / deess /
reverb, pure:, learn:) and Harrison (32C, MPC, De-Esser, gates, delays) to the LYNX effect for the same job at its
defaults (reverbs at a 25 % slot mix, delays at Echoform's tape echo), so a project that used them still plays a chain.

## Where I stopped
- Done: §1 inventory, §2 Vocalstrip 2 measured and Ridge's Vocal strip voicing + translator (tests
  `tests/Vox145Tests.inc`, pt-BR), §3 Auto-Tune tracking / input type in noise, §4 Complete Access role entries.
- Not done: the de-esser / de-ploser SPLIT detector (the split renders null at −1 … −22 dB: the reduction in split
  mode is smaller than the broadband law for the same tone; data in `vs\out\dsdp`, `dsdet`, `dstime`); the notch /
  high band within 0.3–1.5 dB above 10 kHz (the reference is neither bilinear nor exactly the analog prototype there);
  LYNX Tune's pitch jitter in noise (§3); Soundtoys leftovers (Decapitator ≥ 5 / Punish curve, EchoBoy Dual and the
  ping-pong kernel, PanMan's first ~200 ms after a reset — the existing round-136 renders start after the host's
  settle time, so the glide needs new renders; Soundtoys trial until 11-22); FabFilter (~11-16).
- Trials left: Complete Access Hub 2026-11-03 (iLok 11-13), FabFilter ~11-16, Soundtoys 11-22; Auto-Tune ends
  2026-10-25 21:00 (all of round 138's and this round's renders are on D:).
