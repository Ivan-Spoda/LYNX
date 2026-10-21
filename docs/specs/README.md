# LYNX spec dossiers: the plugins LYNX substitutes in her vocal projects

Round 125 (spec125). A clean-room knowledge base, one dossier per third-party plugin whose role a LYNX "substitute"
plays when the girlfriend's FL Studio vocal projects are opened without that plugin
([PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md)). Each dossier tells an implementing agent what the original is, how it
is built, what every control does, what the published science says about modelling it, what LYNX does today, how
to make the substitute sound the same, and what is still unknown.

## How to use this knowledge base (for another AI agent)

1. **Start with priority.** The table below is ordered by how much her corpus uses each plugin
   (412 projects; `tmp/r113/inventory_all.txt`). Her vocal chain is the same preset almost everywhere:
   Auto-Tune Pro → SSL E-Channel → NS1 → RComp → DeEsser → CLA-2A on the vocal insert; H-Delay, Doubler,
   ValhallaVintageVerb, Fresh Air, Little AlterBoy, Filterverse around it. Only 1–3 distinct states exist for most
   Waves plugins, so matching her ONE setting per plugin matters more than matching the whole control range.
2. **Read a dossier in order.** §1 identity and her real state values → §2 signal flow → §3 controls (with the state
   index each control is saved at) → §4 curves → §5 science (equations) → §6 what LYNX does now and its measured
   error → §7 implementation guidance (ordered steps, pass criteria) → §8 open measurements.
3. **Trust the tags.** Every claim carries a source and a tag:
   `[DOC]` vendor manual / page · `[PAPER]` academic · `[PATENT]` · `[SCHEMATIC]` hardware schematic / service manual ·
   `[MEAS]` measurement (ours: linked REA round; others: URL) · `[model]` an inference, with what it rests on.
   Our own black-box measurements of the official trials (`docs/analise/REA_rodada113.md`, `119`, `120`) outrank a
   vendor's marketing text; `[model]` lines are hypotheses to test, not facts.
4. **Measure, don't guess.** The "open questions" checklists name the test signal and what each measurement
   decides. Measurements are black-box only: host the official trial (tools/fxmeasure), feed signals, read the
   outputs and the parameters back, screenshot the editor as a user sees it.
5. **Clean-room rules (hard).** Never decompile, disassemble, patch, instrument or debug a third-party plugin binary
   (Waves, Slate, Antares, Soundtoys, UAD, FabFilter, Valhalla, …); never bypass licensing; never copy their presets,
   graphics, wavetables or code; never use leaked or cracked material or unofficial copies. LYNX is AGPL: take
   ideas from open-source DSP only where the licence fits, and write your own code.
6. **Where the code is.** Substitutes: `src/core/` (Ridge Strip, Glow Leveler, Hush De-esser, Aether, Ember,
   Compressor, Echoform in `EchoSuite.cpp`, Quell / Gemini in `VoiceTools.cpp`, LYNX Tune in `LynxTune.cpp`);
   the mapping table and translators: `src/core/PluginSubstitutes.cpp` and the state translators it names.

## Dossiers

| Priority | Plugin (her slots) | LYNX substitute | Dossier |
|---|---|---|---|
| 1 | Waves CLA-2A (733) | Glow Leveler | [cla-2a.md](cla-2a.md) |
| 2 | Antares Auto-Tune Pro (727; + EFX+ 17, Auto-Key 52) | LYNX Tune | [auto-tune-pro.md](auto-tune-pro.md) |
| 3 | Waves SSL E-Channel (727) | Ridge Strip | [ssl-e-channel.md](ssl-e-channel.md) |
| 4 | Waves NS1 (727) | Quell Denoiser | [ns1.md](ns1.md) |
| 5 | Waves Renaissance Compressor (727) | Compressor | [renaissance-compressor.md](renaissance-compressor.md) |
| 6 | Waves DeEsser (727) | Hush De-esser | [deesser.md](deesser.md) |
| 7 | Slate Digital Fresh Air (727) | Aether | [fresh-air.md](fresh-air.md) |
| 8 | Waves H-Delay (712) | Echoform | [h-delay.md](h-delay.md) |
| 9 | ValhallaVintageVerb (708; + ValhallaDelay, FutureVerb) | Epoch Verb | [valhalla-vintageverb.md](valhalla-vintageverb.md) |
| 10 | Waves Doubler (701) | Gemini Doubler | [doubler.md](doubler.md) |
| 11 | Soundtoys Little AlterBoy (696) | none yet | [little-alterboy.md](little-alterboy.md) |
| 12 | Polyverse Filterverse (695) | none yet | [filterverse.md](filterverse.md) |
| 13 | Waves Tune Real-Time (59 in round 120's count) | LYNX Tune | [waves-tune-real-time.md](waves-tune-real-time.md) |
| 14 | Soundtoys PanMan (60; 385 incl. autosaves) | Auto-pan | [panman.md](panman.md) |
| 15 | Soundtoys Decapitator (6; 888 incl. autosaves) | Ember Saturator | [decapitator.md](decapitator.md) |
| 16 | Soundtoys EchoBoy (1; 235 incl. autosaves) | Fruity Delay 3 (Echoform proposed) | [echoboy.md](echoboy.md) |
| 17 | SSL Native Vocalstrip 2 (12) | Ridge Strip | [ssl-native-vocalstrip-2.md](ssl-native-vocalstrip-2.md) |
| 18 | Waves CLA-76 (6) | Compressor | [cla-76.md](cla-76.md) |
| 19 | FabFilter Pro-Q 4, Pro-DS (6 each) | Facet EQ, Hush De-esser | [fabfilter-pro-q4-pro-ds.md](fabfilter-pro-q4-pro-ds.md) |
| 20 | God Particle, HG-2, Maag EQ4, Harrison 32C, Effectrix2, Bloom, Analog Lab V | various / none | [minor-corpus-plugins.md](minor-corpus-plugins.md) |

Slot counts: the 412-project inventory (`tmp/r113/inventory_all.txt`) unless noted; some dossiers also count the
full copy with autosaves, which ranks the Soundtoys plugins much higher. The progress feed for implementing agents
is `~/.claude/jobs/f8789b49/tmp/specs-ready.md`.

## Measurements this base builds on

- [REA round 113](../analise/REA_rodada113.md) — her corpus, the Waves RealWorld state layouts (RComp, DeEsser,
  CLA-2A, SSL E-Channel), Fresh Air measured, first substitute fits.
- [REA round 119](../analise/REA_rodada119.md) — H-Delay, NS1, Doubler measured; her whole vocal chain against the
  official trials in series.
- [REA round 120](../analise/REA_rodada120.md) — Waves Tune Real-Time and Auto-Tune Pro measured (retune, humanize,
  flex-tune laws); LYNX Tune refit.
- [REA round 121](../analise/REA_rodada121.md) — opening her projects with the Waves trials installed.
