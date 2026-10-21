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
other values that vary — presumably the speed / note-transition times (ms), not confirmed (§2).

Translation (`translate = waves-tune`, `wavesTuneScale`): the active setup's root and note set become LYNX Tune's key
and scale (the named LYNX scale with exactly those notes, else LYNX Tune's custom scale with the same note switches);
these states also vote for the project key (`substituteContextOf`) like Auto-Tune Pro's. Speed / note transition stay
at the entry's gentle preset until measured. Tests: `tests/Tune120Tests.inc`.

## 2. Waves Tune Real-Time hosted (official trial)

[EXEC] The bundle is installed (`Plug-Ins V17\Waves Tune Real-Time.bundle`) and WaveShell1-VST3 17.1 lists "Waves
Tune Real-Time Mono / Stereo", but hosted in fxmeasure both entries come up as a **Q10 equalizer**: its parameters are
"Band 1 On/Off … Precision Mode", the state it saves names `<PluginName>Q10`, and the test signal passes unchanged.
CLA-2A did the same before its trial was activated (round 114 §7), so the Waves Tune Real-Time trial is most likely
not activated on this PC → tmp/tune120/ASK_USER.md. Our GUI host (uiview) stalls while loading it.

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
- Landed (a4f0e55): §1 Waves Tune scale translation + tests (build-local 1 309 371 checks, 0 failures, selftest OK).
- Blocked on the user (tmp/tune120/ASK_USER.md): Auto-Tune Pro install (UAC prompt refused), Waves Tune Real-Time
  trial activation (loads as Q10).
- Next, once they load: (a) Auto-Tune Pro: `texts` of its Scale / Key parameters (scale-list order), the X0…X92
  meaning of her ParamsData (set each parameter, save the state), retune / humanize / flex-tune / natural vibrato on
  tmp/tune120 in_test.wav (py/ana.py), screenshots of Auto mode → LYNX Tune editor in its workflow (flex-tune and
  natural vibrato would be new LYNX Tune parameters, appended); (b) Waves Tune RT: probe RealWorld [5]/[7]/[20]/[21]
  (wvprobe.py method), measure speed / note transition → translate; (c) engine: compare LYNX Tune's retune curve
  (90 % at ~55 ms for "20 ms", exponential) with the originals' and refit `retuneMs` if they differ.
