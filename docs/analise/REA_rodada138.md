# REA round 138 — the trials that expire first: Auto-Tune Pro measured in full, LYNX Tune refit; round 136 landed

Branch `feature/vox138` (from `feature/vox136` after its merge with main). English-first; clean-room. Evidence
markers: [DATA] read from her project files, [EXEC] measured (our hosts / LYNX), [model] our reading, not confirmed.
THIRD-PARTY RULE: official trials only, black-box only — hosted in our own host (`uiview.exe`, the public VST3 API:
parameter names / ranges / texts, `getStateInformation` / `setStateInformation`, audio in and out), screenshots of the
vendors' windows as a user sees them; nothing decompiled, patched or copied.

## 0. Round 136 landed (merge with round 135)

Round 135 (parity135, landed first) and round 136 (vox136) had both appended parameters to the same engines and both
translated her Soundtoys states. The merge (`a5d38981`) keeps everything:
- **Parameters**: round 135's appended parameters first, round 136's after them (LYNX Tune: Learn MIDI … Correction,
  then Drive stage, Drive, Wide formant, Output; Echoform: Tempo source … Freeze, then Tape voicing; Ember: Punish …
  High cut (wide), then the Classic voicing's 11 — its switches renamed "Classic punish / thump / steep" so names stay
  unique; Auto-pan: Source … Analog (7–15), then the Rhythm voicing's 12 (16–27, "Rhythm rate / smoothing / analog /
  output"). Nothing has shipped with either layout, so no saved project changes meaning.
- **Auto-pan** keeps three engines: FL's LFO (default), round 135's panner sources, and round 136's measured Rhythm
  voicing (checked first). The Rhythm voicing now has its own panel (`withRhythmVoicing`: the panner's items show while
  it is off; a "Measured rhythm voicing" switch at the bottom flips them).
- **Soundtoys substitutes** keep round 136's measured models (Decapitator → Ember Classic, PanMan → Auto-pan Rhythm,
  Little AlterBoy → LYNX Tune's voice-shifter stage, EchoBoy → Echoform Tape); round 135's hash tables stay compiled
  (`SoundtoysStates.inc` in namespace `st135`, round 136's in `SoundtoysStates136.inc` / `st136`) and Voice Shift stays a
  LYNX effect of its own. The main session may prefer Voice Shift for Little AlterBoy (it has the original's one-screen
  workflow); round 136's numbers (REA_rodada136 §3) are the reason the measured model was kept for the sound.
- Tests adjusted to the merged layout (Parity128 Ember count 83, Parity135s AlterBoy entry, Auto-pan count).

## 1. Third-party trials on this PC and when they expire (2026-10-23 ~20:10–20:25)

Read from the vendors' own windows (screenshots: `D:\lynx-work\vox138\trials\`; table also in `tmp/vox138/trials.md`).

| Product (vendor app) | What the app shows | Expires |
|---|---|---|
| AutoTune Unlimited (Antares: Auto-Tune Pro 11.6, EFX+, Vocal EQ …; AutoTune Central + iLok) | "Activated, 1 of 2 activations available, Expires 25/10/2026"; iLok row "Subscription" 25/10/2026 21:00 (the older trial row, 22/10 21:00, is expired) | **2026-10-25 21:00** |
| Complete Access (Slate Digital, SSL, LiquidSonics, Harrison, sonible; Complete Access Hub) | "Complete Access Expires on 2026-11-03"; Slate was not activated (FG-X 2: "License … Not Found") → **activated in this round** with the Hub's own "Activate licenses" button ("2 licenses successfully activated"; sonible went to the iLok Cloud); iLok rows Slate 13/11 21:15, SSL 13/11 21:09 (the user activated "SSL 4K" — SSL's own 4K B / E / G; there is no UADx on this PC), LiquidSonics 13/11 21:11 (Lustrous Plates: "License Expires In 29 Days") | 2026-11-03 (Hub) / 2026-11-13 (iLok) |
| FabFilter (local evaluation) | Pro-Q 4: "You still have 24 days left in your evaluation period" | ~2026-11-16 |
| Soundtoys Bundle 5.5 (iLok) | Trial, 22/11/2026 13:03 | 2026-11-22 |
| Waves (Waves Central 17.1.3) | no licence on the device, "My products" empty, "All products" only offers Install ("If no license is present for a plugin, it will run in demo mode"); no free trial button → **demo mode** | — |
| Valhalla | "Demo version — reverb output will fade in and out every 45 seconds" | — |

Nothing was bought, subscribed or entered; the only clicks were the Hub's Activate, Close buttons and page tabs.
Waves Central started from PowerShell shows "cannot access PowerShell"; started through explorer.exe it works.

## 2. Auto-Tune Pro: the raw measurement archive (usable after the trial ends)

`D:\lynx-work\vox138\at\` (2.9 GB): `in\` the test signals (+ segment lists `segs_*.json`) and copies of six of her
takes (two each from game over, kwe, luisa sonsa), `states\` her saved states wrapped for the VST3 trial (`*.state`)
and as found in her projects (`*.bin`), `out\<campaign>\<tag>.wav` the official trial's output for every
configuration with `<tag>.txt` (the commands and every control read back, the reported latency) and `index.tsv`;
`out\lynx_*` the same configurations through LYNX Tune. Scripts: `tmp/vox138/py` (`mksig138.py` signals, `atcfg.py`
campaigns, `atcamp.py` runner, `lynxcamp.py` LYNX side, `ana138.py` analyses, `atwrap.py` state wrapper).

Campaigns (≈330 renders): retune 0 … 400 ms on three signals; humanize 0 … 100 × retune 0 / 5 / 20 / 50; Flex-Tune 0
… 100 × retune 0 / 20; the 15 modern and 29 classic scales, keys, detune, scale transpose; formant correction × transpose
−12 … +12 and throat length 50 … 180 % on vowels /a/ /i/ at 110 … 330 Hz; natural vibrato −12 … +12 dB, ignore vibrato,
created vibrato; tracking, input types, mix, latency (44.1 / 48 / 96 kHz, Latency Removal); Classic mode; her own states
on her takes; Auto-Tune EFX+ (retune, humanize, latency, scale, her one EFX+ state).

## 3. Auto-Tune Pro measured [EXEC]

### 3.1 Scales and her states
- In v11 the **classic "Scale" parameter alone does nothing** (all 29 classic scales render chromatic); the "Modern
  Scale" list rules (0 Chromatic, 1 Major, 2 Minor, 3 Harmonic Minor, 4 Jazz Melodic Minor, 5 Dorian, 6 Phrygian,
  7 Lydian, 8 Mixolydian, 9 Locrian, 10 Major Pentatonic, 11 Minor Pentatonic, 12 Blues, 13 Whole Tone, 14 Diminished).
  Every target set measured (C and two other keys) is the textbook scale.
- **Her saved states**: loading them sets Modern Scale. Old-format states (`<Scales>`) map the classic index by name
  (1 → Minor). States saved by a newer version carry `<ModernScales>` and their `ScaleGivenScale` is already a MODERN
  index: [EXEC] her 42 states with "2" load as **Minor** and the one with "1" as **Major** — round 113 / 120 read them as
  Chromatic / Minor. That was 43 of her 143 states (kwe and others) corrected in the wrong scale.
- A sung pitch exactly between two targets goes to the **upper** one (Waves Tune: the lower). Detune moves the targets
  by its cents (−40 → −39 c, +20 → +20 c). Scale Transpose alone changes nothing.

### 3.2 Retune speed R (chromatic; a held note 30 c sharp after 0.8 s in tune)
50 % / 90 % of the pull-back: R 0–2: 2 / 10–16 ms, 5: 4 / 22, 8: 8 / 30, 10: 12 / 36, 20: 20 / 66, 50: 50 / 158,
100: 98 / 320, 200: 198 / 638, 400: 396 / — ms → t50 ≈ R (a first-order pull, τ ≈ 1.44 R). Vibrato kept (±20 c):
R 5: 10–30 %, 20: 45–78 % (3 → 7 Hz), 50: 79–95 %. Notes from silence are pulled the same way (R 400: 19 of 30 c after
0.5 s). LYNX Tune through the translation (τ = 1.36 R − 3 ms): 2 / 22 (R 5), 4 / 32 (8), 24 / 64 (20), 54 / 152 (50),
100 / 300 (100) — the step response matches; it keeps a little less vibrato at R 5–20 (its pitch estimate lags a few
ms more), and τ 1.44 R was tried on her takes: no change, so the law stays.

### 3.3 Humanize H
A note held 0.8 s, then moved: t50 = R + H ms (R 0: 26 / 50 / 74 / 98 at H 25 … 100; R 20: 46 / 70 / 94 / 120). A note
moved after only 0.3 s gets part of it: R 20: 36 / 46 / 54 / 62, R 50: 70 / 84 / 102 / 120, R 5: 14 / 18 / 18 / 20, R 0:
none. Vibrato notes (±5 … 20 c) are not slowed in Modern mode; in **Classic mode and in EFX+ they are** (R 20 H 100: a
5 c wobble 104 / 388 ms vs 18 / 54). LYNX Tune: the slow-down now sets in between 0.275 and 0.375 s of steadiness
(fitted on all of these; was 0.2 … 0.6 s): 0.3 s cases 90 % at 228 / 392 ms (AT 312 / 448, was 144 / 290); the 0.8 s
cases unchanged (118 / 364 vs AT 120 / 380).

### 3.4 Flex-Tune F (from silence, d cents off) — confirms round 120
F 20: 37 / 16 / −3 c corrected at 40 / 45 / 50 c off; F 50: 24 / 19 / 14 / 9 / 4 at 25 … 45; F 90: ≤ 5 c. LYNX Tune: the
same within 1–3 c except the exact-half case (the upper / lower tie of §3.1).

### 3.5 Formant correction, transpose, throat
With Formant Correction on, the spectral envelope stays within ±0.5 st for Transpose −12 … +12 (vowels /a/, /i/,
110 … 330 Hz); off, it moves with the pitch. Throat length L moves the envelope by about −0.0146·(L − 100)^1.4 st above
100 % (−0.4 at 110, −1.3 at 125, −3.5 at 150, −6.7 at 180) and +0.03·(100 − L)^1.4 below (+0.5 at 90, +3.5 at 75) —
about half the −12·log2 (L / 100) the translation assumed. Classic mode: no formant correction (as the manual says).

### 3.6 Natural vibrato (R 20, ±20 … 100 c at 3 … 7 Hz)
The surviving vibrato is multiplied by ≈ 1.26 / 1.51 / 1.80 / 2.09 at +3 / +6 / +9 / +12 dB (≈ 10^(dB/34)) and by
≈ 0.77 / 0.66 / 0.68 / 0.82 at −3 / −6 / −9 / −12 dB (it never goes below about 2/3). LYNX Tune used 10^(dB/20)
(×1.94 / ×3.8 at +6 / +12, ×0.27 at −12) and now follows the measured law. Ignore Vibrato: the targets come from the
pitch without its vibrato (a ±100 c vibrato at R 0 is no longer turned into note jumps) — LYNX Tune's already does that.

### 3.7 Latency and the rest
2670 samples at 44.1 and 48 kHz, 5340 at 96 kHz (fixed in samples); Latency Removal: 112 samples. EFX+: 2780 / 112,
the same retune and humanize laws (Classic-style humanize). Classic mode: the same retune curve. Tracking and Input Type do not change the correction of clean harmonic notes
A2 … C6 (+30 c → corrected 29–32 c for every type and tracking 25 … 100; tracking 0 drops one note, A5).

### 3.8 Her takes with her own states (the real translation, LYNX vs the trial)
|pitch(LYNX) − pitch(trial)| over the frames voiced in both (5 ms hop), and the dry take for scale:

| state → take | median / mean / > 20 c | dry vs trial |
|---|---|---|
| game over (A minor, R 0 / 7.2) | 2.4–4.0 / 6.2–8.9 / 6.7–8.4 % | 20–23 / 23–26 / 50–56 % |
| kwe (C minor, R 5 / 0) | 3.2–5.2 / 8.1–11.1 / 13–16 % | 19–24 / 22–27 / 47–57 % |
| luisa sonsa (E minor, R 8.5) | 3.5–4.7 / 10.9–13.6 / 15.7 % | 21–27 / 23–28 / 52–63 % |

### 3.9 LYNX Tune's "LF excess" (round 136's open item)
At neutral settings LYNX Tune raises the 1/3-octave band at about half the fundamental by 2–4 dB (lu: 126–159 Hz, f0
≈ 290 Hz; go: 159–200 Hz, f0 ≈ 320 Hz; kw: 200–252 Hz, f0 ≈ 460 Hz) and loses 0.5–1.4 dB above 6 kHz; the trial is
flat within ±0.5 dB. Those bands sit 37–44 dB under the take's loudest band, so the rise is a residue at −35 dB or
less (period-doubling of the grains); stronger mark locking (a larger bias toward the predicted mark) halves it, a
lower peak-search filter or snapping the grains to their marks does not. Left as is (not audible on her takes); the
HF loss (−0.6 dB more than the trial at 10–12 kHz) is the item to revisit.

## 4. LYNX changes
- `autoTuneProKey`: states with `<ModernScales>` read the modern list (15 scales; whole tone / diminished through
  "More scales"); `autoTuneProNoteEdits` reads the modern 12-digit rows (§3.1).
- Throat length → formant shift with the measured law (§3.5).
- LYNX Tune: humanize ramp 0.275 … 0.375 s (§3.3); natural-vibrato gain 10^(dB/34), ≥ 2/3 below 0 dB (§3.6).
- Ember Classic: the E / N / T / P styles' fitted constants (§5).
- Tests: `tests/Vox138Tests.inc` (modern-scale states incl. her kwe state when present, the substitute's scale and
  notes, whole tone; the humanize ramp; the fitted styles), `Parity128cTests` natural vibrato and `Vmatch127Tests`
  throat to the measured laws.

## 5. Decapitator's styles E / N / T / P (Ember Classic voicing) [EXEC]
Round 136 measured the five styles on the official trial (stairs at six frequencies, bursts, her three takes at
drives 0 … 6; `D:\lynx-work\vox136\deca5_*.pkl`) but fitted only style A; E, N, T, P ran on the struct's default
constants. Fitted now with round 136's lab (`tmp/vox136/lab/decalab`, Nelder–Mead, 2 × 300 iterations, started from A's
constants with each style's measured colour FIR, the same 15 constants free): mean null over the signal set E −16.3,
N −16.6, T −12.1, P −12.5 dB (A: −25.2 dB; with A's curve they were −8.9 … −9.5 dB). Constants in
`EmberClassic.cpp` `styleTable`; her states all use style A, so this is for everyone else's Decapitator states and
the Classic style buttons.

High drive (style A, round 136's open item): with the low-drive constants the null falls from −17.5 dB at drive 5 to
−10.6 dB at drive 10. A refit on drives 6 / 8 / 10 (stairs, bursts) and her takes at 6 / 8 reaches −18.0 dB there (takes
at drive 6: go −22.9, kwe −24.7, lu −22.8 dB, were −17.4 / −21.9 / −20.7), but it loses the even harmonics at low
level (1 kHz at −30 dBFS, drive 6: H2 −53 dBc vs the trial's −28.5); with the bias terms held at the low-drive values
it reaches −17.2 dB and still H2 −42. Not adopted — the curve needs a level-dependent even-harmonic term that the
null alone does not fit (fits `Ahi1`, `Ahi2` in `D:\lynx-work\vox136\lab`).

## Where I stopped
- Done: §0 (round 136 merged and landed), §1 trials, §2 archive, §3 Auto-Tune Pro measured, §4, §5.
- Not done (round 136's list, in order): Decapitator at drive ≥ 5 (§5: tried, not adopted) and Punish's curve
  (data in `D:\lynx-work\vox136`, Soundtoys trial until 2026-11-22), EchoBoy's Dual and the ping-pong kernel's
  last −21 dB, PanMan's first ~200 ms after a reset, the RComp / CLA-2A / NS1 residue (Waves in demo mode: mute-masked
  measurements). Auto-Tune Pro: tracking / input type on noisy or breathy voices, the upper-note tie, LYNX Tune's HF loss.
- Next trials by expiry: Complete Access (Slate / SSL / LiquidSonics / sonible) 2026-11-03, FabFilter ~11-16,
  Soundtoys 11-22.
