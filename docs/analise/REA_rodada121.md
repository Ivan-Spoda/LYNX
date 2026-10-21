# REA round 121: opening the vocal projects with the Waves trials installed

Since the Waves trials (WaveShell1-VST3 17.1) were installed, LYNX loads the Waves slots of the girlfriend's vocal
projects (`tmp/gf/proj2`: kwe, game over) for real, and opening them got much slower: headless `open_ms` of kwe
1.2 s → 16–23 s (round 116), one GUI open hung ~4 min. Scope: find where the time goes and fix what is ours.

Evidence markers: [EXEC] runs of LYNX / our own probes on this PC, [code] reading LYNX / JUCE source, [model] our
reading. Third-party rule: the Waves shell was only *hosted* — its public VST3 entry points called and timed from our
side (tmp/load121/probe.cpp: `LoadLibrary`, `InitDll`, `GetPluginFactory`, `countClasses`, `createInstance`,
`initialize`); nothing of it was opened, disassembled or patched. Measurements in `C:\Users\kauan\lynx-test\load121`.

## Where I stopped

(in progress)
