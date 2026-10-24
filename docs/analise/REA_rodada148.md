# REA round 148 — Auto-Tune's last day: LYNX Tune's held notes in noise; SSL 4K E measured

Branch `feature/vox148` (from main after rounds 145 / 146). English-first; clean-room. Evidence markers: [DATA] read
from her project files, [EXEC] measured (our host / LYNX), [model] our reading, not confirmed.
THIRD-PARTY RULE: official trials only, black-box only — hosted in our own host (`uiview.exe`, the public VST3 API:
parameter names / ranges / texts, `getStateInformation` / `setStateInformation`, audio in and out); nothing decompiled,
patched or copied. No vendor app was opened this round.

## 1. Auto-Tune Pro before the trial ends (2026-10-25 21:00): held notes in noise [EXEC]

Round 145 left one Auto-Tune item: on noisy / breathy held notes LYNX Tune's output wobbled by a few cents more than
the trial's. This round recorded the extra renders first (all on D: so they stay usable after the trial):
`D:\lynx-work\vox148\at\` — `in\` the signals (`mksig148.py`: held notes of a harmonic voice, 2 s each, MIDI 45 … 76
in tune and 20 c sharp; clean, with white noise 20 / 10 / 5 dB under the voice, with aspiration noise 1–8 kHz shaped by
the glottal cycle 10 dB under, with a real singer's pitch jitter (3 c rms, ~12 Hz wide) and the mixes of both),
copies of six of her takes and her states; `out\sus\`, `out\takes148\` the official trial at retune 0 / 20 / 50 ms
(and humanize 100, Classic mode, Tracking 100 on two of them); `out\lynx0_*` LYNX Tune before this round,
`out\lynx148_*` after, `out\x_*` the lab variants. Scripts: `tmp/vox148/at` (`mksig148.py`, `at148cfg.py`,
`run148.py`, `lynx148.py`, `anasus.py`, `anatake.py`, `hpitch.py`, `exp.py`).

### 1.1 Measuring a held note's wobble
YIN on the output reads the noise as pitch jitter (its own floor was 4–5 c on the aspiration signal), so the held
notes are measured with a harmonic-sum tracker (60 ms Hann frames every 5 ms, harmonics 1–8, a 0.25 c grid with a
parabola): its floor on the dry signals is 0.0–0.4 c. Wobble = the rms deviation of the note's pitch from its own mean,
0.45 s after the onset to 0.1 s before the end; also split into < 4 Hz, 4–15 Hz and > 15 Hz.

### 1.2 What the trial does
- Retune 0 removes the singer's jitter entirely (3 c rms in → 0.2 c out, at every band up to > 15 Hz); at retune
  50 ms the jitter goes through untouched (2.1 c out of 2.5) — the output follows the input's own cycles plus a
  smooth correction.
- Noise moves its output a little: 0.1–0.3 c at 20 dB SNR, 1.0–1.7 c at 10 dB, 3.7–4.0 c at 5 dB or with aspiration
  noise (less at slower retune). Tracking 100 and Humanize 100 change nothing; Classic mode halves it.

### 1.3 Why LYNX Tune wobbled, and the fix
The lab (env knobs on a copy of the shifter, not kept) separated the causes:
- LYNX Tune's wobble did **not** depend on the retune speed (2.3 c at 10 dB SNR for retune 0, 20 and 50) and was
  4–11 c on the lowest notes (110 Hz) — so it was not the pitch estimate but the PSOLA **marks**: each mark went to the
  highest peak of a 900 Hz low-pass within ±¼ period, and in noise that peak moved by a sample or two from period to
  period; every grain then carried its pulse a little early or late.
- A longer detector window (30 ms), a smoothed synthesis period, or an f0-dependent low-pass changed little; marks
  without any alignment were steady in synthetic noise but drifted on her takes (2.8–3.2 c at retune 0).
- **Adopted**: each mark goes where the waveform best repeats the period before the previous mark (cross-correlation
  of the low-passed signal over one period, the peak refined to a fraction of a sample with a parabola; the lag is
  measured from the previous mark's own fractional position). A whole period integrates the noise away; without the
  fraction of a sample the integer marks alone left ~2 c of jitter on high notes. The first mark of a phrase still
  takes the peak.
- The old "unshifted grains drift toward their marks" step switched on and off at 1.7 c of correction (a 0.5 c wobble
  on held notes whose correction hovers around zero): now faded in over the last 3.4 c and at most 0.05 % of a period per
  grain.
The shifter is shared by LYNX Tune and Voice Shift (`core/VocalPitch.cpp`, `Psola`).

### 1.4 Results (wobble, cents rms; median over the 16 notes, in brackets the four lowest)

| signal, retune | Auto-Tune Pro | LYNX before | LYNX now |
|---|---|---|---|
| clean, 0 / 20 / 50 | 0.0 (0.0) | 0.3 (0.3) | 0.0 (0.0) |
| noise 20 dB under, R 0 | 0.3 (0.6) | 1.3 (3.8) | 0.1 (0.1) |
| noise 10 dB under, R 0 / R 50 | 1.7 (1.9) / 1.0 (1.1) | 2.3 (8.3) / 2.3 (8.5) | 0.3 (0.5) / 0.5 (0.8) |
| noise 5 dB under, R 0 | 4.0 (3.4) | 3.3 (12.5) | 0.8 (1.2) |
| aspiration noise, R 0 / R 50 | 4.0 (3.8) / 2.4 (3.3) | 3.7 (10.9) / 3.8 (11.0) | 0.7 (1.2) / 1.0 (1.4) |
| singer's jitter, R 0 / R 20 / R 50 | 0.2 / 1.6 / 2.1 | 0.7 / 1.7 / 2.2 | 0.1 / 1.4 / 2.0 |
| jitter + aspiration, R 0 / R 50 | 3.9 / 2.9 | 3.1 / 3.8 | 0.8 / 2.3 |

LYNX Tune is now steadier than the trial in noise and keeps its jitter behaviour (removed at retune 0, kept at slow
retune). Her takes (retune 0, chromatic; the held notes where the trial's output sits on a semitone, 2–11 per take):
trial 0.5–0.6 c, LYNX before 0.7–1.2 c, now 0.4–0.8 c; at retune 20 / 50 all three within 1 c of each other (the
surviving vibrato dominates). Her own states on her takes, |pitch(LYNX) − pitch(trial)| (round 138 §3.8's measure):
median 2.4–5.2 c before → 1.7–4.3 c now, > 20 c 6.7–16.4 % → 5.2–15.4 % (every state better). The retune step
responses, vibrato, flex-tune and humanize campaigns of round 138 are unchanged within 2–4 ms / 1 c
(`D:\lynx-work\vox148\at\ana_retune_*.txt`, `ana_cmp_herstate.txt`).
Test: `tests/Vox148Tests.inc` (110 / 220 Hz held notes 20 dB above white noise at retune 0 and 100 ms: the output's
wobble stays at the dry signal's analysis floor, < 1.2 c).

## 2. SSL 4K E (Complete Access) measured; Ridge Strip's 4K voicing [EXEC]

Her corpus has no 4K state ([DATA] the 935 copied projects: "SSLChannel" = the Waves E-Channel, 2193 slots, already
Ridge's E voicing; "SSL Native Vocalstrip 2", 48 slots, round 145; no 4K B / E / G) — so this is for everyone else's
projects that used the 4K E, and for Ridge's own users. The trial (v1.7.1, 54 VST3 parameters with their own texts,
`D:\lynx-work\vox145\k4e_params.txt`) was hosted in `uiview.exe`; raw data `D:\lynx-work\vox148\k4\` (`in\` signals,
`out\<campaign>\` renders + read-backs, `states\<campaign>\` the trial's own saved state for every configuration,
`out\lynx_<campaign>\` the same through LYNX from those states, `null_<campaign>.tsv`), scripts `tmp/vox148/k4`
(`k4camp.py` campaigns, `k4states.py`, `k4eq.py` / `k4fit.py` / `k4fit2.py` / `k4tables.py` EQ, `k4dyn.py` /
`k4time.py` / `k4dynfit.py` / `k4gendyn.py` / `k4gatefit.py` dynamics, `k4mic.py` mic pre, `k4lynx.py` comparison,
`c/k4check.cpp`). Campaigns: eq (500: three colours × every band at 8 gains × 4–5 frequencies × 5 Qs, the bell types,
filters), dyn / dyn2 (210: compressor stairs at 7 thresholds × 5 ratios with and without auto make-up, make-up
offsets, 1 kHz bursts at 6 settings × fast / slow × 4–8 releases, mix, expander / gate stairs and bursts), drive (32:
VCA, mic pre 0 … 50 dB, trims, fader, her takes), vcaprobe (15: the VCA on pink / white noise at four levels).

### 2.1 What the 4K E does
- **EQ**: every band is one second-order section — fitted per render to 0.03 dB rms (median; 90 % under 0.15 dB) with
  the shelves as bilinear sections at 48 kHz and the bells following the analog prototype's magnitude (a bilinear
  bell is 0.2–0.4 dB off near Nyquist). The dial's printed gain is not the gain: at "+10" the LMF peaks at +7.8 dB
  (Brown), +8.4 dB (Black), +11.5 dB (Orange), at "+15" all three reach ±15–16 dB; the bells' Q narrows with the gain
  (Black most: the "proportional Q" colour), Black's LMF sits at 0.81 × the dial, Brown's at 1.04 ×, Orange 1.0 ×;
  the shelves' corner moves with the gain (LF "+15" at 100 Hz: 2.3 × (Brown), 3.7 × (Black), 3.5 × (Orange) the dial
  in the shelf's own midpoint). The bell types (LF / HF Bell) are wide bells (Q 0.27–1.0 by colour).
- **Filters**: high-pass 12 dB/oct (Q 0.75), its corner 23.7 / 38.1 / 70.2 / 135 / 321 Hz at the dial's 20 / 50 /
  100 / 200 / 350; low-pass 12 dB/oct (Q 0.72), 3.3 / 6.8 / 11.9 kHz at 3 / 5 / 8.44, almost nothing at 12 and above.
- **Compressor**: a feedback design like round 123's E-Channel — the threshold dial moves the curve by 0.83 dB per
  dB, soft knee 1.5–4.7 dB, the steady ratio a little under the dial's; slow attack 100–900 ms depending on the setting
  (deeper settings slower), fast ≈ instant; release ≈ the knob for short settings with a two-part tail for long ones;
  the mix blends the reduction **in dB** (50 % = half the dB at every level); the automatic make-up is the gain a
  signal far below the threshold gets (+0.4 … +9.4 dB over the grid), its offset moves it by 0.19–0.31 dB per dB.
- **Expander / gate**: threshold 23.3 dB under the console scale, the expander 1.13 dB of reduction per dB, the range
  dial's 5 / 10 / 20 / 40 = 7.8 / 17.1 / 26.7 / 47.3 dB deep, opening within 0.5 ms, closing over 117 ms × (release /
  0.1 s)^0.58 (gate) / 21 ms × (…)^0.6 (expander).
- **Mic pre** (Pre In + Mic): level-compensated drive, 0.744 dB of drive per Mic dB into a curve that first expands
  slightly (+0.5 … 0.8 dB) then clips at +11.8 dB over full scale (H3 → −10 dBc, square-like), the input trim *after*
  it; Pre In alone −0.27 dB and −104 dBFS of hiss.
- **Analogue VCA**: level unchanged, H2 −77 … −57 dBc and H3 −107 … −66 dBc for 1 kHz at −30 … −6 dBFS, −106.6 dBFS of
  hiss, and a 2nd-order **~3 Hz coupling high-pass** (found on pink noise: its 0.25-s levels moved by up to 4.8 dB
  while sines and white noise did not change — level-independent, 25° of phase at 10 Hz).

### 2.2 LYNX: Ridge Strip's 4K voicing
`core/VoiceMatch.h/.cpp` — the E-type channel model gets a `model` (1 = 4K) with its own EQ tables
(`Console4KTables.inc`), filters, compressor / gate laws (`Console4KDynamics.inc`, round 123's feedback model with the
fitted grid), mic pre, VCA (colour, hiss, coupling high-pass), compressor mix and auto make-up offset. Ridge Strip gets
5 appended parameters (4K voicing, EQ colour, Mic pre, Mic gain, Make-up offset; off = the old sound) and reuses the
E voicing's controls in the console's units plus Auto make-up, Analog (= Analogue VCA) and Comp. mix; the panel shows
a "4K console" switch, the three colours, Offset, Mic pre and Mic while it is on; the EQ display draws the console's
own curve (also for the E voicing now). `translate = ssl-vocalstrip-2` reads an SSL 4K E state too (XML, the A or B
setting that plays): every control except pan, width, the external key and the side-chain routings.

LYNX vs the trial, both from the trial's own saved state (null after the first 0.25 s, `null_*.tsv`):

| Test | null (median / 90 %) |
|---|---|
| EQ impulses, 500 settings | −42.6 / −19 dB (the worst: ±15 dB cuts / boosts above 12 kHz, 0.3–2 dB at 20 kHz) |
| VCA, mic pre, trims, fader (32) | −46.4 / −19 dB (mic pre −27 … −37, VCA on noise −50 … −53, her takes VCA only −48 … −50) |
| compressor / gate stairs and bursts (210) | −34.6 / −19 dB (gate / expander −33 … −71; compressor at deep settings −12 … −20, 0.5–1.3 dB less reduction) |
| her takes, compressor + gate + EQ | −15 … −21 dB, level within 0.5–1.2 dB |

The compressor is the open part: round 123's model fits each setting's stairs to 0.1 dB and its bursts to 0.2–1 dB,
but the fitted attack, release and static trade off against each other (fitted separately they disagree by up to 1 dB
on the stairs; three joint-fit attempts moved, not removed, the error). Not done: the 4K B and G (other consoles;
by role only), the external side chain.

## 3. FabFilter in her projects: Pro-DS translated (Pro-Q 4 re-checked) [DATA] [EXEC]

[DATA] FabFilter slots in the 935 copied projects: Pro-Q 4 166 mentions (4 distinct states), Pro-DS 74 (2 states),
Pro-Q 3 2 (1 state); no Pro-C, Pro-MB or Pro-L — so their stand-ins (Crest, Prism, Apex) carry no residue in her
projects (their parity is rounds 144 / 147 / 150's).
- **Pro-Q 4** (Facet EQ, round 124): re-rendered with today's LYNX against the trial's renders of round 124
  (`D:\lynx-test-archive\vm124x\out\pq_*`): her four states on her three takes null at −108 / −63 / −102 / −84 dB
  (unchanged since round 124).
- **Pro-DS** was not translated (round 124 measured it and handed it off; nothing landed): her 74 slots ran Hush at
  its defaults, **+2.3 … +3.8 dB null** against the trial on her takes (Hush cut 25–29 dB where Pro-DS cut ≤ 3.9 dB;
  doing nothing at all would have been −41 … −56 dB). Now:
  - [EXEC] its 20 state values are its 20 VST3 parameters in their own units (read through the trial's parameter
    API this round: `D:\lynx-work\vox148\pds\st\params.txt`): mode, threshold (−60 + 60 v dB), range dB, band
    processing, stereo link / mode, look-ahead ms / on, audition, side chain, high-pass / low-pass (2000 × 10^v Hz), …;
  - Hush De-esser gets a **Vocal voicing** (appended parameter; `core/VoiceMatch.h` `vocalds`): round 124's
    prototype, fitted on the trial's gain envelopes (8 kHz steps, her takes) — the 7–14 kHz band (4th-order high- and
    low-pass) into a power detector, a second detector that compares the band with the signal below it so only a band
    that is loud *and* stands out switches the reduction on (hysteresis 3.9 / −4.1 dB), range × (L − thr + 4.1) /
    (0.1 − thr), looked ahead by the look-ahead time inside a fixed 15 ms latency (720 samples at 48 kHz, as the
    trial), wide band = the whole signal × the gain; split band [model]: only the part above the high-pass.
  - `translate = pro-ds`: Single Vocal → the Vocal voicing, Allround → Hush's own engine in its allround mode.

| her state → take | before (Hush defaults) | now | (doing nothing) | GR frames: rms error / trial max / LYNX max |
|---|---|---|---|---|
| −43.7 dB → go / kwe / lu | +3.6 / +3.8 / +2.3 dB | **−51.7 / −56.0 / −49.1 dB** | −44.7 / −53.2 / −41.1 | 0.65–0.92 dB / −2.9 … −3.9 / −1.9 … −3.4 dB |
| −36.0 dB → go / kwe / lu | +3.6 / +3.8 / +2.3 dB | **−54.1 / −58.3 / −50.4 dB** | −46.1 / −56.0 / −42.5 | 0.53–0.91 dB / −2.1 … −3.4 / −1.2 … −2.9 dB |

(`tmp/vox148/pds/cmp.py`, `pq.py`; LYNX renders `D:\lynx-work\vox148\pds\out`, `pq\out`.) Test
`pro_ds_translates_onto_hush_vocal_voicing_round148` (her state's values, 720 samples of latency, −5.2 dB on a steady
8 kHz tone at −10 dBFS, pink noise untouched).

## Where I stopped
- Done: §1 (Auto-Tune renders recorded before the trial ends; LYNX Tune's marks).
- Next: SSL 4K E (Complete Access, until 2026-11-03), FabFilter residue (~11-16), Soundtoys leftovers (11-22).
