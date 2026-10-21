# REA round 120 — pitch correction for her vocal projects (LYNX Tune in Waves Tune / Auto-Tune Pro's place)

Branch `feature/tune120`. English-first; clean-room. Scope (round 119/120 brief): her vocal projects (tmp/gf/proj2)
use Auto-Tune Pro and Waves Tune Real-Time; LYNX Tune stands in for both when they are missing. Measure the official
trials black-box, translate her saved states, make LYNX Tune's editor follow Auto-Tune Pro's workflow, and fix LYNX
Tune's engine where the measurements show it is audibly off.

Evidence markers: [DATA] read from her project files, [EXEC] measured (our hosts / LYNX), [model] our reading, not
confirmed. THIRD-PARTY RULE: official trials only, black-box only (hosted, fed signals, outputs measured, UI
screenshotted as a user sees it); nothing decompiled, patched or copied.

## 1. Waves Tune Real-Time: what her states say

[DATA] 11 distinct Waves Tune Real-Time states in her corpus (tmp/r113/wv/Waves_Tune_Real-Time_*.bin, 59 slots);
in tmp/gf/proj2 only `luisa sonsa` has one (insert 1). Besides the RealWorld list, each setup (A and B) carries a
`<PluginSpecific TagName="Scale">` element holding a readable XML scale: its name, `<Root>` (0 = C … 11 = B) and the
twelve `<NoteStatus>` values above the root (`0` = a target note, `x` = excluded):

    states       active scale (SETUP_A)       RealWorld [5] [7] [8]   [20]  [21]
    0,5,6,7,8    Natural Minor, root 4 (E)      3   4   0   16.1–31.7  20.9–54.2
    1,3          Major, root 5 (F)              2   5   0   16.1       54.2
    2,10         Natural Minor, root 8 (G#)     3   6   1   42.7       16.2
    4            Natural Minor, root 11 (B)     3   1   1    0.1        0.1
    9            Major, root 0 (C)              2   2   0   21.4       54.2
    (SETUP_B of every state: RealWorld 5 = 1, 7 = 7, [20] 15, [21] 120 — the untouched second setup)

So luisa sonsa's Waves Tune is in **E natural minor** — the same key/scale its two Auto-Tune Pro slots name ("E, 1"),
an independent confirmation that Auto-Tune Pro's scale 1 = minor in her projects (round 113 §2 [model]).
RealWorld [5] follows the scale (2 = major, 3 = natural minor, 1 in the untouched setup); [20] / [21] are the only
other values that vary — the Speed / Note Transition times in ms ([EXEC] §2).

Translation (`translate = waves-tune`, `wavesTuneScale`): the active setup's root and note set become LYNX Tune's key
and scale (the named LYNX scale with exactly those notes, else LYNX Tune's custom scale with the same note switches);
these states also vote for the project key (`substituteContextOf`) like Auto-Tune Pro's; the retune comes from the
Note Transition time (§2). Tests: `tests/Tune120Tests.inc`.

## 2. Waves Tune Real-Time measured (official trial)

[EXEC] Hosted in fxmeasure (WaveShell1-VST3 17.1, "Waves Tune Real-Time Mono"). The first plugin our host creates from
the shell comes up as Q10 (round 119 §1): a DeEsser is loaded first, then Waves Tune is itself (143 parameters). Trial
in demo mode; the analysis skips silent frames. Parameters: Speed, Note Transition (0.1 … 800 ms each), Correction
(%), Reference (415–466 Hz), Tolerance time / cents, Vibrato depth / on, Scale Type (User, Chromatic, Major, Natural
Minor, Harmonic Minor, … 44 entries), Scale Root (17 names: C, C#, Db, …). The RealWorld list: [5] = Scale Type
index (1 chromatic, 2 major, 3 natural minor), [20] = Speed, [21] = Note Transition (defaults 15 / 120 ms; her state
loads back as 21.4 / 54.2 ms).

On the test signal of §4 (chromatic unless noted), time to 50 % / 90 % of the correction of a held note 30 c sharp:

    Note Transition   0.1      20.9     54.2      120       250 ms
    from silence      30/30    30/64    50/144    96/298    188/590
    mid-note drift    14/40    20/50    42/120    88/278    172/574
    vibrato kept      45 %     57 %     88 %      97 %      99 %     (5.5 Hz, ±40 c)

Speed 0.1 … 400 ms (Note Transition 120) changes none of these (only below ~5 ms: 82 % instead of 97 % of the
vibrato). So on sung notes Waves Tune's correction is a first-order pull toward the target with time constant = the
Note Transition time (+ ~12 ms detection): t50 ≈ 12 ms + 0.69 NT, t90 ≈ 12 ms + 2.3 NT — the same law as LYNX Tune's
retune. Instant note changes are followed at once; a sung slide passes through; on an octave glide the correction
averages −3 c; notes exactly between two targets go to the lower one. Her state (E natural minor, 21.4 / 54.2 ms):
every semitone E4…E5 goes to the nearest E-minor note — same choices as LYNX Tune in E minor.

Translation: Note Transition → LYNX Tune's retune (ms), humanize 0; Speed, Correction (100 % in every state),
tolerance and vibrato (off) are not translated.

## 2b. LYNX Tune engine refit (item 3)

LYNX Tune at retune = NT on the same signal (before → after this round):
- [EXEC] **A phrase started with the previous phrase's correction still applied** (the applied shift was never reset
  across silence): a note 30 c flat after a sharp phrase was first bent ~18 c further DOWN before being pulled up (50 %
  at 106 ms instead of 50). Now each phrase starts at the sung pitch: 50 % / 90 % = 52 / 134 ms at 54.2 (Waves 50 /
  144), 98 / 284 at 120 (Waves 96 / 298), 185 / 570 at 250 (Waves 188 / 590), symmetric for sharp and flat notes.
- [EXEC] **Glides were pulled back** against their direction: the held target was kept 0.1 semitone past the midpoint,
  biasing the correction of an octave glide by −15 c (Waves −3 c). Hysteresis 0.03 st: −8 c, and a note held exactly
  between two targets still does not flip. (The rest is detection lag on a moving pitch, not changed.)
- Mid-note drift and vibrato retention already matched (e.g. NT 54.2: drift 48 / 126 ms vs 42 / 120; vibrato 87 % vs
  88 %; NT 20.9: 58 % vs 57 %).

[EXEC] Her `luisa sonsa` takes (3 takes, 37 s, 2195 voiced 10 ms frames), Waves Tune with her own state vs LYNX Tune:

    LYNX Tune as translated                     |pitch − Waves| median / mean / frames > 20 c
    before (project key E minor, 50 ms, humanize 50 %)   6.9 / 13.2 c / 15.3 %
    now (her scale, retune 54.2 ms, refit engine)         3.4 / 10.1 c / 10.4 %
    (no correction at all)                               18.5 / 22.4 c / 46.0 %

Tests: `tests/Tune120Tests.inc` (phrase onset, retune curve vs the Waves numbers, glide bias, Note Transition read).
Scripts: tmp/tune120/py (mksig, ana, wtrun, sweep1, take), fx/tunerun; renders in C:\Users\kauan\lynx-test\tune120.

## 3. Auto-Tune Pro: install

The first install attempt failed at the Windows permission (UAC) prompt (Auto-Tune Central's log: "user did not grant
permission", 23:00). The user reinstalled it on 2026-10-21: `VST3\Auto-Tune Pro.vst3` v11.6.0.2336 (official trial),
hosted headless in fxmeasure and on screen in the look114 uiview host; it runs without a login dialog.

## 4. LYNX Tune measured on the same test signal

[EXEC] tmp/tune120/py/mksig.py writes a 32.5 s harmonic "voice" (20 harmonics) with: held notes 10 / 30 / 45 cents
sharp and 30 cents flat (step response), instant +2 / +5 semitone note changes and a 150 ms sung slide, an octave
glide in 2 s, ±40 cent vibrato at 5.5 Hz, every semitone E4…E5 at +20 c, and notes at exactly +50 c. ana.py tracks the
output (YIN, 2 ms hop). LYNX Tune before this round, chromatic, retune 20 ms (fx/tunerun):

    step +10/+30/+45 c   50 % of the correction at 30 ms, 90 % at 52–58 ms, final error < 0.2 c
    step −30 c           50 % at 50 ms, 90 % at 80 ms
    jump +2 / +5 st      output follows in 0–18 ms
    sung slide (150 ms)  output 10→90 % in 104 ms (input 120 ms)
    vibrato ±40 c        56 % of the vibrato kept
    octave glide         27 % of the time within 10 c of a semitone (input 19 %): the glide stays smooth

## 5. Auto-Tune Pro measured (official trial, black-box)

**Parameters and her states.** [EXEC] 182 parameters; the parameter ID is the number n of the `X<n>` entries in the
`ParamsData` of the state (JUCE base64 XML, real units): loading her states and reading the parameters back gives
exactly those values. The **Scale** list (its own texts): 0 Major, 1 Minor, 2 Chromatic, then historical tunings
(Pythagorean, Just, Meantone, … Harmonic) — **round 113's 0 = major / 1 = minor / 2 = chromatic is confirmed**; v11
also has "Modern Scale" (Chromatic, Major, Minor, Harmonic Minor, …), which her (older) states don't set. Key 0…11 =
C…B. [DATA] Her 143 Auto-Tune slots (tmp/tune120/at, py/atstates.py / atdecode.py), the controls that matter:

    X4  Retune Speed (ms)   0 … 14 (mostly 5.8 – 8.2; game over 0 = the hard effect). Round 113 read X31 (20 ms) —
                            that is "Object Retune Speed" of Graph mode, not the live one
    X61 Humanize            100 (137 slots), 65 / 68 / 30 / 0 in a few
    X90 Flex-Tune           20 (109), 22 (26), 0 – 18 in the rest
    X62 Natural Vibrato     0 dB everywhere;  X10 Input type Alto/Tenor;  X9 Tracking 50;  X70 Formant on (142)
    X1 / X2 / ScaleGivenScale / ScaleGivenKey   the scale and key (98 minor, 43 chromatic, 2 major)

**Measurements** (tmp/tune120/py: mksig / mksig2 test signals, atrun / sweep2 / sweep3, ana / ana2; latency 2670
samples reported and compensated). Chromatic, instant onset from silence ≈ 30 ms for every speed.
- Retune Speed R: a held note going 30 c sharp mid-note is pulled back along a first-order curve: 50 % / 90 % at 4/20
  (R 5), 10/30 (8), 22/64 (20), 50/162 (50), 98/318 (100), 198/638 ms (200) → time constant ≈ 1.36 R − 3 ms (t50 ≈ R).
  Vibrato ±40 c 5.5 Hz kept: 3 % (R 0), 20 % (5), 35 % (8), 68 % (20), 91 % (50).
- Humanize H: only a **sustained** note is slowed: a note held steady for 0.8 s, then 30 c sharp, is pulled back as if
  the speed were R + H ms (R 0: H 25/50/75/100 → 50 % at 26/50/74/100 ms; R 5 → 30/54/80/104; R 20 → 46/70/94/118).
  After only 0.3 s steady the slow-down is partial; a note with even a ±5 c vibrato is not "sustained" (no slow-down).
- Flex-Tune F: a note d cents from its target is corrected by min (d, (1 − F)/F × (50 − d)) cents (F 10 … 90, d 5 …
  50 c, e.g. F 50: d 30 → 20 c, d 45 → 5 c; F 90: 4.5 c at most); in a scale d counts in hundredths of the gap to
  the next target (E minor, F + 20 c = 80 c below F#: F 50 corrects 23 c, F 80 6 c, F 20 fully).
- Natural Vibrato (her states: 0 dB): ±12 dB roughly doubles / removes the vibrato that survives the correction —
  not reproduced (LYNX Tune has no such control yet).

**LYNX Tune refit** (core/LynxTune.cpp, same signals through fx/tunerun):
- new parameter **Flex-Tune** (appended, default 0 = unchanged) with the law above, gap-normalised in scales: F 20 /
  50 / 90 corrected fractions at 5 … 50 c match within 0.05 (e.g. F 50: 0.67 / 0.43 / 0.24 / 0.11 at 30 – 45 c vs
  0.67 / 0.43 / 0.25 / 0.11); E minor F+20 c: 65.40 vs 65.43 (F 50), 65.25 vs 65.26 (F 80).
- **Humanize** now follows steadiness instead of note age: a note counts as sustained once its pitch stays within 4 c
  of its own recent average (a single move — the singer settling elsewhere — keeps it; another move 30 – 150 ms
  later, i.e. a vibrato, resets it); the slow-down adds humanize × 135 ms to the time constant over 0.2 – 0.6 s of
  steadiness (was: up to +250 ms by note age, vibrato included). R 5 / H 100: 110 / 334 ms vs 104 / 336; R 20 / H 100:
  122 / 384 vs 118 / 384; R 0 / H 50: 58 / 168 vs 50 / 160; vibrato notes stay fast in both.
- **Translation** (`translate = auto-tune-pro`): key / scale as before, retune = 1.36 × X4 − 3 ms (LYNX's time
  constant), humanize = X61, flex-tune = X90 (`autoTuneProParams` decodes ParamsData).
- [EXEC] Her `luisa sonsa` takes (37 s) through Auto-Tune Pro with her own state (E minor, 8 ms, H 100, F 20) vs LYNX
  Tune, |pitch difference| over 2260 voiced 10 ms frames: as translated now median 4.5 c / mean 13.0 c / 15.1 % of
  frames > 20 c; the 0.1.9 translation (20 ms, no humanize / flex) 5.7 / 14.2 / 16.8 %; no correction 24.1 / 26.5 /
  58.1 %.

## 6. LYNX Tune's editor in the auto-mode workflow

Reference: Auto-Tune Pro's Auto mode screenshotted in our host (tmp/look114 uiview; kept in
C:\Users\kauan\lynx-test\tune120\ref, never in git): a top bar (tracking, input type, key, scale | formant,
transpose, detune, mix), the correction row (Retune Speed big on the left, Flex Tune, a round correction meter with
the note name in the centre, Natural Vibrato, Humanize big on the right) and a keyboard of the target notes at the
bottom. LYNX Tune now has the same layout logic in LYNX's own drawing (PluginLayout.cpp `lynxTune`, 800 × 548):
INPUT (tracking, input type, key, scale) and OUTPUT (transpose, A4, mix, low latency, MIDI channel) on top; the
CORRECTION row with RETUNE SPEED and HUMANIZE big, FLEX-TUNE and FORMANT (preserve + shift) beside them and the new
`pitchring` display in the centre (50 segments for ±100 c lit toward the applied correction, the target note's name,
the sung offset); LYNX's scrolling PITCH GRAPH (`pitchtrace`, the graph of round 103 without its read-out) below;
the TARGET NOTES as one keyboard octave. No shared component was added (two displays in PluginEditors.cpp).

## Where I stopped
- Landed a4f0e55: §1 scale translation. Second landing: §2 Note Transition → retune, §2b engine refit + tests.
- Third landing: §5 Auto-Tune Pro translation + Flex-Tune / Humanize refit, §6 editor.
- Next: Natural Vibrato (a vibrato gain after the correction); Auto-Tune's Graph mode is not reproduced.
- Ideas: compensate the detector lag on moving pitch (glide bias −8 c vs Waves −3 c); Waves Tune's Speed /
  Tolerance / Vibrato controls (unused in her states).
