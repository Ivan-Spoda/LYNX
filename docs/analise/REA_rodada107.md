# REA round 107: vocals quieter in LYNX than in FL (user projects)

User report: whenever the user's girlfriend opens one of her vocal projects in LYNX, the voice is much quieter than
in FL Studio. Goal: find the cause, fix it generally, verify within ~0.5 dB of FL on the vocal sections.

## 1. Her projects and her setup

[DATA] The projects she opened in LYNX (LYNX `recentes.txt`): `kwe.flp` (FL 24.2.2), `luisa sonsa.flp` (FL 25.2.5),
`game over.flp` (FL 25.2.5), each in its own folder with the beat stems and her vocal takes (`untitled_<date>_Insert
N.wav`: 44.1 kHz stereo float, both channels identical, raw level about −20 dBFS RMS). Every vocal take goes to an
insert with the same vocal chain (her "PRINCIPAL" preset): Auto-Tune Pro, Waves SSL E-Channel, NS1, Renaissance
Compressor, DeEsser, CLA-2A, (Fresh Air), with sends to an FX insert (H-Delay, Doubler2, Reeverb 2, Filterverse,
Stereo Enhancer, Decapitator). `luisa sonsa` uses UADx Neve 1073 / 1176 / LA-2A, Pro-Q 4, Pro-DS, Decapitator,
ValhallaVintageVerb, Waves Tune and NS1 instead.

[DATA] LYNX's plugin list on her laptop (`%APPDATA%\LYNX\plugins.xml`): 136 plugins, but the only Waves entries are
"L4 Ultramaximizer" from `WaveShell1-VST3 16.3_x64.vst3`; `WaveShell1-VST3 16.0_x64.vst3` — the shell that holds her
whole Waves bundle — is in the BLACKLISTED list. So none of the Waves plugins of her vocal chain could be hosted:
LYNX played its substitutes (round 98) in their place, with the Waves settings not translated.

## 2. Cause 1: a Waves shell is "hung" for LYNX's plugin scan

[SRC JUCE 8.0.4, our own dependency] `VST3PluginFormat::findAllTypesForFile` has a fast path (the bundle's
`moduleinfo.json`) and a slow path that creates and initialises every audio-effect class of the module's factory to
read its bus layout. A Waves shell has no moduleinfo.json and declares one class per Waves plugin (hundreds for a
bundle). LYNX scans each file in a child process and killed it after a fixed 30 s, then blacklisted the file as hung.

Fix: the scan child first loads the module, asks its factory how many classes it has and writes that number next to
its result file; the parent then waits `30 s + 2 s × (classes − 1)` (capped at 45 min) instead of 30 s
(core/PluginScanPolicy.h). Files blacklisted by older versions are retried once (`scan-retry.txt` marker).

## 3. Cause 2 (the main one): FL's route levels follow the fader curve, up to +5.6 dB

[EXEC] FL 26.1.7 command-line renders on her laptop (`FL64.exe /R /Ewav`) of `kwe.flp` variants with only her vocal
takes audible (every other channel at volume 0), compared per 10 s window with LYNX renders of the same files:

    variant                                              FL 60-70 70-80 80-89 s     LYNX 60-70 70-80 80-89 s
    vocal chain as saved                                    -14.1 -13.4 -14.3          -27.8 -28.4 -29.2  (Waves substituted)
    vocal inserts emptied (no plugins at all)               -14.4 -13.5 -14.8          -25.9 -25.2 -26.5
    + FX insert emptied                                     -10.6  -9.8 -10.8          -22.0 -21.5 -22.5
    vocal channels routed straight to Master                -16.2 -16.0 -17.2          -16.5 -16.6 -17.6

With no plugin in the path the difference is a constant gain of 3.6 (11.1 dB; aligned at lag 0, same for every
take), and it disappears when the channels go straight to Master: it is in the routing. [DATA] Her vocal insert
(10, "VOZ 1") routes only to her FX insert (11), which routes to Master; both route levels (0xE1, parameter
0x2000 + destination; 0x1F40 + destination before FL 24.2.99) are 16000. LYNX read a route level as
`value / 12800` clamped to 1 (so 16000 = 0 dB) and had no level at all for the route to Master.

[EXEC] Same variant (FX insert emptied), one value changed at a time, FL level change in the vocal sections:
route 10→11 at 12800: −5.6 dB; at 6400: −18.3 dB (−12.7 dB from 12800); route 11→Master at 12800: −5.6 dB, at 6400:
−18.3 dB. The fader curve g = (11^(1.25·v/16000) − 1)/10 gives exactly +5.6 dB at 16000, 0 dB at 12800 and −12.7 dB
at 6400. So both route kinds are fader-curve levels with FL's fader range (0 dB at 80 %, +5.6 dB at the top).
(Insert faders 10795→12800 and 12245→12800 moved FL by +4.0 and +1.3 dB, the curve says +3.7 and +1.0; within
0.3 dB, not investigated further.)

Fix: `MixerSend::level` is a linear gain up to `maxFaderGain` (+5.6 dB) and the import maps a route level through
`faderGain (value / 16000)`; new `MixerTrack::masterLevel` (route to Master, same mapping, rendered by
RackRenderer, saved in .lynx as `masterLevel`). The mixer's send knob now works in fader positions (0 dB at 80 %,
text in dB), like FL's. Projects whose routes are at the default 12800 (or without level records, like the FL 26
song of round 92b) are unchanged.

## 4. Cause 3: a compressor substitute without makeup gain

On her laptop neither FL nor LYNX has Renaissance Compressor / DeEsser / Doubler (her projects were saved with
WaveShell 15.5, the laptop has 16.0, which lacks them): FL leaves those slots empty. LYNX plays its substitutes, and
the Renaissance Compressor one (LYNX Compressor at its defaults, −18 dB 4:1, no makeup, since the Waves settings are
not translated) took another 4.4 dB off the vocal (LYNX k3 → k4 renders). The compressor got an "Auto gain" switch
(slow in/out power match, ±12 dB) and the compressor substitutes (Renaissance Compressor, other Waves compressors,
Pro-C) turn it on: they shape dynamics but keep the level.

## 5. Other differences seen (not the vocal level)

- `kwe`: the beat stems' last clips (start offset 77 s into 106 s stems, 73.3 → 88.6 s) stop at 78 s in LYNX, play to
  the end in FL (FL −12.7 dB in 80–89 s without vocals, LYNX silent). Channel has 0xD7 TIME 140, mode 1, and the wav has
  `acid`/`smpl` chunks. Not investigated yet.
- `game over` / `luisa sonsa`: only 1–2 playlist tracks are enabled (byte 12 of 0xEE); her other tracks are muted in
  the file (only her mixdown / the beat play). LYNX does the same as the file says.

## Where I stopped / next steps
- Verifying all fixes on her laptop (kwe, game over, luisa) against FL; then land.
- Export report now lists plugin_found / plugin_missing lines and each insert's chain (Main.cpp runExport);
  `--find-plugins` runs the app's targeted plugin scan first. renderwav: LYNX_SUBSTITUTES=1 / LYNX_DISABLE="t.k".
