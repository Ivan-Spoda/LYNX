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

## Where I stopped / next steps
- Waiting for her laptop (offline since ~14:25) to (a) time the WaveShell 16.0 scan, (b) render FL vs LYNX.
- Export report now lists plugin_found / plugin_missing lines and each insert's chain (Main.cpp runExport).
- renderwav: LYNX_SUBSTITUTES=1 / LYNX_DISABLE="t.k" for substitute experiments.
