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

## 3. Auto-Tune Pro

Not installed. [EXEC] No Auto-Tune Pro plug-in anywhere on C: (VST3 / VST2 / AAX folders) as of 2026-10-21 00:30;
Auto-Tune Central's log ends with "user did not grant permission" at 23:00 — the install's Windows permission
(UAC) prompt was refused or timed out → tmp/tune120/ASK_USER.md. The scale-list order stays [model].

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

## Where I stopped
- Landed a4f0e55: §1 scale translation. Second landing: §2 Note Transition → retune, §2b engine refit + tests.
- Blocked on the user (tmp/tune120/ASK_USER.md): Auto-Tune Pro install (UAC prompt refused) — its scale-list order
  stays [model] (now supported by her Waves Tune state in luisa sonsa, E natural minor = Auto-Tune "E, 1").
- Next, once Auto-Tune Pro loads: `texts` of its Scale / Key parameters, the X0…X92 meaning of her ParamsData, its
  retune / humanize / flex-tune curves on in_test.wav (py/ana.py), Auto-mode screenshots → LYNX Tune editor in its
  workflow (flex-tune / natural vibrato would be new appended parameters).
- Ideas: compensate the detector lag on moving pitch (glide bias −8 c vs Waves −3 c); Waves Tune's Speed /
  Tolerance / Vibrato controls (unused in her states).
