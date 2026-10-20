# REA round 113: "once the substitutes load, her voice sounds different from FL"

User report (round 111 brief, item c): on the girlfriend's laptop her vocal projects open with LYNX's substitute
plugins and the voice sounds odd compared with the same project in FL Studio. Projects: `game over`, `kwe`,
`luisa sonsa`, `TWIN SWAG` (her laptop, K:\LIWN-EP-ANTIGOS\IMPORTANTE!!\Presets) and copies in tmp/gf/proj2.

Evidence markers: [DATA] read from the project files / LYNX's own files on her laptop, [EXEC] renders and runs
(LYNX renderwav / LYNX.exe here and on her laptop, FL's command-line render `FL64.exe /R /Ewav` here), [model] our
reading, not confirmed. No third-party binary was opened, decompiled or measured; her unofficial copies were not
touched (LYNX itself was run on her laptop, hidden, at idle priority, to see what it hosts).

## 1. What actually plays on her laptop (the main finding)

[EXEC] LYNX 0.1.7 `--export` of her four projects on her laptop (export reports in tmp/r113/her/exp*.out):

| Project slot | Plugin | Her laptop, LYNX 0.1.7 | Why |
|---|---|---|---|
| every vocal insert, slot 1 | Auto-Tune Pro | **not loaded: "No compatible plug-in format exists" → the slot is bypassed (no pitch correction at all)** | her plugin list has two Auto-Tune Pro entries with the same class ID; LYNX picked the one whose file no longer exists (`…\VST3\Auto-Tune Pro.vst3`, 11.6, uninstalled; the installed one is `…\VST3\Antares\Auto-Tune Pro.vst3`) |
| SSL E-Channel, NS1, CLA-2A, H-Delay, Waves Tune RT | WaveShell1-VST3 16.0 | substitutes (Ridge Strip, Glow Leveler, Fruity Delay 3, LYNX Tune), NS1 left out | the 16.0 shell is still BLACKLISTED in `%APPDATA%\LYNX\plugins.xml` (written again today) |
| RComp, DeEsser, Doubler | WaveShell1-VST3 15.5 | substitutes (Compressor, Hush De-esser), Doubler left out | not installed on her laptop (FL leaves these slots empty too, round 107 §4) |
| Decapitator, EchoBoy, PanMan, Little AlterBoy, Fresh Air, Pro-Q 4, Pro-DS, UADx, ValhallaVintageVerb | — | hosted (same VST2 ID / VST3 class ID) | — |

So on her laptop the voice was **un-tuned** (Auto-Tune silently bypassed) and went through four uncalibrated
substitutes. That alone explains "the voice sounds different".

[EXEC] The 16.0 shell itself scans fine from LYNX's scan child in her SSH session: 106 classes (the class count is
reported after 2.4 s), description complete in 53–66 s, exit 0; the result lists SSLChannel, NS1, CLA-2A, H-Delay,
Waves Tune Real-Time (the project's class IDs hash to these entries) but not RComp / DeEsser / Doubler. Why it is
blacklisted in her desktop session is not confirmed (no GUI could be run there: she was using the laptop). The likely
cause [model]: the child hangs after writing its answer (the shell's shutdown in a desktop session), so the parent
kills it at the timeout and blacklists the whole file even though the description was complete.

Fixes (engine/PluginHost.cpp):
- `findForeign` skips plugin-list entries whose file is gone (class ID, same-file and name steps), so the installed
  Auto-Tune Pro is used.
- A Waves shell is never matched through "the file FL's database lists" / "the file the project saved": that file
  holds the whole bundle, and the first plugin of it would have been taken.
- The scan child ends right after writing its answer (`TerminateProcess`, no module unload); the parent keeps a
  complete answer even when it had to stop the child; every failure is logged with its reason and time in
  `%APPDATA%\LYNX\scan-failures.log`; files blacklisted by earlier versions get one more try (`scan-retry.txt` = 2).

## 2. LYNX Tune in Auto-Tune Pro's place: the key and scale

[DATA] Auto-Tune Pro's VST3 component state is `VC2!` + XML `<SessionChunkData>`: a `ParamsData` blob (JUCE
MemoryBlock base64 of an `<ActualParams>` list X0…X92, values in real units) and a `<Custom … ScaleGivenKey=
ScaleGivenScale= ScaleGivenDetune=>` element. In all seven states of her projects X2 = ScaleGivenKey and X1 =
ScaleGivenScale; X31 = 20 (the vendor's documented default retune speed is 20 ms).

    project     inserts     ScaleGivenKey  ScaleGivenScale
    game over   1, 9, 12    9 (A)          1
    kwe         10, 13      0 (C)          2
    luisa sonsa 9, 12       4 (E)          1

The scale list's order is not documented in the state. [EXEC] Pitch-class histograms of her raw takes (YIN, 512-hop,
tuning offset removed; tmp/r113/py/pcs.py):
- game over, "A, 1": A natural minor holds 82 % of the sung frames, A major 69 %; G sung 10.8 % vs G# 0.4 %.
- luisa sonsa, "E, 1": E minor 81 %, E major 70 %; G 8.7 % vs G# 2.3 %.
- kwe, "C, 2": C is the default key; no scale on C fits (C minor 70 %, C major 57 %, best fit F# major 73 %).
[EXEC] The same test on the 22 main projects of her whole corpus (48 projects copied with her consent to tmp/gf/all,
backups excluded; tmp/r113/py/scalefit.py, up to 8 takes × 60 s each): of the 21 projects saved with scale 1, 18 fit
the minor scale on their key better than the major one (e.g. "pokke", C: minor 0.77 vs major 0.48; "need me", F:
0.75 vs 0.41); the 3 others are close calls (G: 0.83 / 0.77, F#: 0.80 / 0.72, A#: 0.61 / 0.49). The only project
saved with scale 0 ("OIOIOI", C#) fits major (0.84 vs 0.57).
Reading [model, consistent with all of the above]: 0 = major, 1 = minor, 2 = chromatic (the key is irrelevant, hence
left at its default in `kwe`). Other values are not translated.

LYNX Tune used to start at C major, 20 ms for every Auto-Tune Pro: on `kwe` (chromatic) and `luisa` (E minor) it
pulled sung notes to wrong pitches. [EXEC] Frames moved by more than 50 cents (LYNX Tune alone vs no tuning):

    project      C major (before)   key/scale from the state   chromatic 50 ms + humanize 50 %
    kwe          36.4 %             22.7 % (chromatic, 20 ms)  21.0 %
    luisa sonsa  25.0 %             20.1 % (E minor)           15.2 %
    game over    12.2 %             12.2 % (A minor = C major's notes)  12.4 %

Now: `translate = auto-tune-pro` sets key, scale and 20 ms from the state; an Auto-Tune whose state can't be read
and Waves Tune Real-Time take the project's key (the most common key of the project's readable pitch correctors,
`substituteContextOf`); with no key anywhere the substitute corrects gently (chromatic, 50 ms, humanize 50 %: the
least change of the variants above that still tunes, distance to the nearest semitone 22 → 19 cents) instead of
forcing C major.

## 3. The other substitutes: what is read, what falls back

[DATA] Her vocal chain is the same preset in every project (identical states). Readable or not:

| Original | Substitute | State | Now |
|---|---|---|---|
| Auto-Tune Pro | LYNX Tune | readable XML | key, scale, 20 ms (§2) |
| Waves Tune Real-Time | LYNX Tune | Waves RealWorld list, meaning not confirmed | project key (E minor in luisa), gentle |
| SSL E-Channel | Ridge Strip | RealWorld list (57 values), meaning not confirmed | neutral (transparent, unchanged) |
| Renaissance Compressor | Compressor | RealWorld list, not confirmed | 2:1 above −15 dBFS, auto gain (was −18 dB 4:1) |
| DeEsser | Hush De-esser | RealWorld list; value 0 = 5506 is the only one in Hz | frequency from value 0 [model]; −20 dBFS, 4 dB range (was 6 kHz, −30 dBFS, 12 dB) |
| CLA-2A | Glow Leveler | RealWorld list (208 values), not confirmed | peak reduction 30 %, +2 dB (was 45 %, +3 dB) |
| Fresh Air | Aether | 2530-byte state: two floats 0..1 at 1290 / 1294 = Mid Air / High Air [DATA, EXEC] | air shelf gain + frequency from laws measured on the official trial (§3b; was neutral) |
| Decapitator / EchoBoy / PanMan | Ember / Fruity Delay 3 / Auto-pan | Soundtoys encoded preset | Ember drive +1.4 dB (was +5.4), Auto-pan depth 50 % (was 75 %), Delay 3 default |
| H-Delay | Fruity Delay 3 | RealWorld list (BPM, time in ms readable) | default; FL's slot mix is 0–7 % in her projects, so it barely plays |

[EXEC] Each substitute alone on `game over` (renderwav, every other substitute bypassed), octave-band difference
against no substitutes, level-normalised, 125 Hz … 16 kHz (spread = RMS of the eight bands):

    substitute        before                                      after
    Hush De-esser     -0.6 +0.1 +0.1 +0.1 -0.2 -0.9 -3.7 -4.8 (2.18) -0.6 +0.1 +0.1 +0.1 -0.2 -0.6 -1.5 -1.2 (0.76)
    Compressor        +4.3 +1.1 +0.1 -0.4 -0.4 +0.5 +2.3 +2.2 (1.95) +2.2 +0.5 +0.1 -0.2 -0.2 +0.3 +0.9 +0.9 (0.92)
    Glow Leveler      +2.1 +0.9 -0.1 -0.1 -0.4 +0.2 -0.1 +0.5 (0.85) +0.9 +0.4 -0.1 -0.0 -0.2 +0.0 +0.1 +0.4 (0.39)
    Ember Saturator   +1.1 +1.6 -0.2 -0.2 -0.0 +0.3 +1.5 -0.0 (0.88) +0.2 +1.1 -0.2 -0.1 +0.0 +0.1 +0.6 -0.3 (0.46)
    LYNX Tune         +1.0 +0.0 +0.1 +0.0 -0.3 -0.4 -0.7 -1.1 (0.61) (same: PSOLA)

## 3b. Fresh Air measured (official trial on this PC)

[EXEC] The official Fresh Air trial (v1.1.7, installed here 2026-10-20) in tools/fxmeasure, headless: its parameters
are "Mid Air", "High Air" (0–100 %), Bypass, Trim; setting Mid 0.3 / High 0.7 and saving the state puts 0.3 / 0.7 at
offsets 1290 / 1294 of the 2530-byte component state — the layout read from her projects. Pink noise at −20 dBFS RMS,
48 kHz, gain vs the input (dB):

                 1k    2k    3k    4k    6k    8k   10k   12k   16k   20k
    Mid 0.25   +0.7  +0.5  +1.0  +1.6  +2.2  +2.5  +2.6  +2.7  +2.7  +2.7
    Mid 0.5    +0.7  +0.5  +1.5  +2.6  +3.6  +4.0  +4.2  +4.3  +4.4  +4.4
    Mid 1      -0.0  -0.1  +2.5  +4.6  +6.2  +6.7  +7.0  +7.1  +7.2  +7.2
    High 0.25  +1.0  +0.9  +0.7  +0.5  +0.2  +1.0  +2.3  +3.3  +4.3  +4.6
    High 0.5   +1.4  +1.1  +0.7  +0.2  +0.0  +2.2  +4.7  +6.3  +7.5  +7.8
    High 1     +1.7  +1.2  +0.4  -0.5  +0.3  +4.6  +7.8  +9.5 +10.8 +11.1

Each response above 2 kHz (singles and the seven Mid/High pairs her corpus uses) was fitted by Aether's shelf (RBJ
high shelf, S = 0.6); least squares over the fits give gain = 10.345 m + 20.548 h − 2.32 m² − 7.75 h² − 11.673 m h dB
and log2 f = 12.8069 − 0.7108 m + 0.6746 h − 0.5369 m h (5–20 kHz), within 0.43–0.69 dB RMS of the measured curves
for her settings (up to 1.6 dB for High alone at 100 %, which she never uses). Her most common setting (0.29 / 0.25)
is +6.6 dB from 6.8 kHz. Fresh Air may be level-dependent ("dynamic"); only −20 dBFS was measured (not confirmed at
other levels). Scripts: tmp/r113/py/fafit.py, renders in C:\Users\kauan\lynx-test\r113\fa.

## 3c. The Waves vocal plugins measured (official trials on this PC)

[EXEC] Waves V17 trials (WaveShell1-VST3 17.1) in tools/fxmeasure, headless (`load <shell> 48000 512 <name>`;
`loadcomponent` puts a project's raw VST3 component state in). RealWorld layout: every control moved alone, the list
value that followed it (and its text) recorded (tmp/r113/wv/*_probe.txt, *_map.txt; py/wvmap.py, wvprobe.py). Her
states read back through the plugins (the same in every project of her corpus):

| Plugin (values) | Layout confirmed | Her state |
|---|---|---|
| RComp (28) | 0 release ms, 1 threshold dB, 2 gain dB, 8 attack ms, 9 ratio, 16 warm/smooth, 17 ARC/manual, 22 electro/opto, 27 trim | -15 dB, 2.95:1, 0.5 ms, 160 ms ARC, gain 0, warm, electro |
| DeEsser (15) | 0 freq Hz, 2 threshold dB, 10 monitor, 13 side chain (1 high pass, 2 band pass), 14 audio (0 split, 1 wideband) | 5506 Hz, -24.2 dB, high pass, split |
| CLA-2A (208) | 38 gain, 74 peak reduction, 112 comp/limit, 140 analog, 153 hi freq, 206 trim, 207 auto makeup | gain 48.96, peak reduction 62.6, comp |
| SSL E-Channel (57) | 0-3 comp thresh/ratio/fast/release, 5-9 expander, 11 dyn bypass, 13-24 EQ (LF/LMF/HMF/HF type, gain, freq, Q), 25 EQ bypass, 27/28 HP/LP, 29 filter split, 30 gain, 39 phase, 40 input | comp -1.3 dB 1.8:1 fast 0.35; HP 116 Hz; LF -6 dB 60 Hz shelf; HMF +3 dB 3.5 kHz Q 1.5; HF +4.8 dB 10.16 kHz shelf |

[EXEC] Each plugin at her state on sine stairs (1 and 6 kHz, -60 .. 0 dBFS peak, 3 dB steps) and her raw take
(py/wvmeas.py, stairtab.py); gain in dB at -60 / -24 / -18 / -12 / -6 / 0 dBFS:

    RComp      1k    0.0  -0.4  -1.8  -4.6  -8.5 -12.8    take -9.6 dB (no make-up at gain 0)
    DeEsser    6k    0.0  -0.0  -1.4  -5.6  -8.6 -10.2    1 kHz untouched
    CLA-2A     1k   16.0  12.4   7.9   3.3  -1.2  -5.8    6 kHz: 14.3 at -60 (its tone)
    SSL (EQ off) 1k  0.9   0.9   0.4  -0.7  -2.2  -4.0    +0.9 dB below the threshold

Substitutes fitted (grid search, LYNX's effect run on the same signals through tmp/r113/fxl/fxlynx; py/fitdyn.py):
- RComp -> Compressor: threshold - 5 dB, same ratio, attack, release 300 ms for ARC, gain as make-up, no auto gain:
  0.53 dB RMS on the stairs, take level -9.48 vs -9.56 dB.
- DeEsser -> Hush (split, high-pass detector): 0.81 x the frequency, threshold + 5 dB, 12 dB range (fixed; the
  original flattens about 10 dB down): 0.84 dB RMS (6 kHz weighted x3).
- CLA-2A -> Glow Leveler: gain knob -> level below the threshold (noise at -70 dBFS: 20 -> -18.5, 30 -> -3.0,
  40 -> +8.0, 48.96 -> +15.5, 60 -> +24.2, 70 -> +29.2, 80 -> +31.8 dB) + 1.5 dB; peak reduction
  0.40 + 0.021 x (knob - 40) (fitted on her take after RComp at peak reduction 40 / 62.6 / 75 / 85: 50 ms level
  envelope within 0.3-1.4 dB; Glow saturates above 75). On the stairs Glow's knee is softer (3 dB RMS), the take's
  envelope is what is matched.
Before (gentle defaults) these three moved her take by -0.2 / -0.1 / +1.0 dB where the originals do -9.6 / -0.05 /
+6.3 dB (CLA-2A after RComp): the defaults kept the level, the originals do not.

## 4. Whole vocal path: substitutes vs bypassed vs FL

[EXEC] Vocal-only variants (other channels at volume 0, playlist tracks unmuted), renderwav without normalisation
(`LYNX_RAW`), whole song. "byp" = the substitute slots off (= LYNX with the third-party slots empty).

    project  substitutes   level vs byp  band spread  >50 c pitch moves
    kwe      before        +0.44 dB      3.40 dB      36.8 %
             now           +1.44 dB      1.25 dB      23.9 %
    game     before        -1.01 dB      3.58 dB      14.4 %
             now           +0.43 dB      1.77 dB      13.8 %
    luisa    before        +1.04 dB      3.59 dB      27.0 %
             now           +1.49 dB      2.20 dB      22.1 %

Before, the substitutes took 5–7 dB off 16 kHz (de-esser) and moved up to 37 % of the sung frames by more than a
quarter tone (wrong key); now the spectral change is 1.3–2.2 dB RMS (luisa's +2 dB at 16 kHz is her Fresh Air) and the
pitch moves are those of a 20 ms correction in the right key.

[EXEC] FL here has none of the third-party plugins (it leaves the slots empty), so its render is the reference of the
"bypassed" path: `kwe` vocal variant, FL −22.3 / −14.8 / −13.7 dB vs LYNX (bypassed) −22.3 / −14.7 / −13.7 dB in
50–80 s, octave-band spread 0.03 dB; LYNX.exe 0.1.7 `--export` of the variant with the vocal and FX inserts emptied
equals FL to 0.0 dB in every window. The project's own path is exact; what differs is only what the third-party
plugins (or their substitutes) do. (Note: renderwav normalises its .wav to −1 dBFS; levels above use its raw output.)

## 4b. What her corpus uses (priorities for further substitute work)

[DATA] Third-party slots in her 24 main projects (tmp/r113/py/inventory.py; backups excluded): her vocal preset is
the same almost everywhere — Auto-Tune Pro (48 slots / 21 projects; key from the state now), CLA-2A (49), SSL
E-Channel, NS1, RComp, DeEsser (48 each; 2–3 distinct states in the whole corpus), H-Delay (47), Doubler (46),
ValhallaVintageVerb (42, translated since round 100), Fresh Air (41, read now), Filterverse (40), Little AlterBoy (39).
Then Analog Lab V (6, instrument), Decapitator (6), PanMan (4). With so few distinct Waves states, measuring the five
Waves vocal plugins once (official trials, fxmeasure) would translate her whole catalogue. NS1, Doubler, Filterverse
and Little AlterBoy have no LYNX role yet (left out, as FL does when they are missing).

## 5. A/B switch

Right-click a substitute slot › "Bypass all LYNX substitutes of this project" (pt-BR "Desativar todos os substitutos
LYNX deste projeto"): every substitute of the project stops (`Song::substitutesBypassed`, saved in .lynx; the PDC
leaves their latency out); the slots show "(bypassed)". [EXEC] With the switch on, the render equals the render
without substitutes sample for sample (max |diff| 0) on the three projects. renderwav: `LYNX_SUBSTITUTES_BYPASS=1`;
experiments: `LYNX_PARAM="t.k.p=v"`, and the substituted slots are listed with their `t.k` positions.

## Where I stopped / next steps
- Done: §1 matching fixes, §2 Auto-Tune key/scale + project key, §3 gentle defaults + Fresh Air / DeEsser reads, §5
  switch, tests (`tests/Substitutes113Tests.inc`), PLUGIN_FALLBACK.md.
- [EXEC] Verified on her laptop with this branch's LYNX.exe (hidden, idle priority, her settings restored after):
  `kwe` hosts Auto-Tune Pro from `…\VST3\Antares\` (before: bypassed); `game over --find-plugins` scans both Waves
  shells (16.0 and 16.3) in 88 s and then hosts Auto-Tune Pro, SSLChannel, NS1, CLA-2A, H-Delay, Fresh Air and the
  Soundtoys plugins; only RComp and DeEsser (not installed there, FL leaves them empty) are substitutes.
- Still to verify there: Auto-Tune Pro hosted from the Antares folder; the 16.0 shell described
  (or `scan-failures.log` saying why not) — needs her LYNX run in her own desktop session.
- Fresh Air: measured and translated (§3b). Official trials of Auto-Tune Pro, Waves and Soundtoys were still being
  installed on this PC (2026-10-20): once they load headlessly, measure (fxmeasure) Auto-Tune Pro's scale list
  (confirms or corrects §2) and the Waves RealWorld layouts (RComp, DeEsser, CLA-2A, SSL E-Channel) to translate them instead of the
  gentle defaults; FL renders with the real plugins as the reference.
