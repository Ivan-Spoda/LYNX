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

## Where I stopped
- Done: §1 (Auto-Tune renders recorded before the trial ends; LYNX Tune's marks).
- Next: SSL 4K E (Complete Access, until 2026-11-03), FabFilter residue (~11-16), Soundtoys leftovers (11-22).
