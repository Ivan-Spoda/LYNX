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
Reading [model]: 0 = major, 1 = minor, 2 = chromatic (the key is irrelevant, hence left at its default). Other values
are not translated.

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
| Fresh Air | Aether | 2530-byte state: two floats 0..1 at 1290 / 1294 (0.33–0.60 in her slots) [DATA] | mid → exciter × 0.25, high → air shelf up to +6 dB [model gain] (was neutral) |
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

## 5. A/B switch

Right-click a substitute slot › "Bypass all LYNX substitutes of this project" (pt-BR "Desativar todos os substitutos
LYNX deste projeto"): every substitute of the project stops (`Song::substitutesBypassed`, saved in .lynx; the PDC
leaves their latency out); the slots show "(bypassed)". [EXEC] With the switch on, the render equals the render
without substitutes sample for sample (max |diff| 0) on the three projects. renderwav: `LYNX_SUBSTITUTES_BYPASS=1`;
experiments: `LYNX_PARAM="t.k.p=v"`, and the substituted slots are listed with their `t.k` positions.

## Where I stopped / next steps
- Done: §1 matching fixes, §2 Auto-Tune key/scale + project key, §3 gentle defaults + Fresh Air / DeEsser reads, §5
  switch, tests (`tests/Substitutes113Tests.inc`), PLUGIN_FALLBACK.md.
- To verify on her laptop with the new build: Auto-Tune Pro hosted from the Antares folder; the 16.0 shell described
  (or `scan-failures.log` saying why not) — needs her LYNX run in her own desktop session.
- Official trials of Auto-Tune Pro, Fresh Air, Waves and Soundtoys are being installed on this PC (2026-10-20): once
  they load headlessly, measure (fxmeasure) Auto-Tune Pro's scale list (confirms or corrects §2), Fresh Air's gain
  law, and the Waves RealWorld layouts (RComp, DeEsser, CLA-2A, SSL E-Channel) to translate them instead of the
  gentle defaults; FL renders with the real plugins as the reference.
