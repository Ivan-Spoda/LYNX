# Built-in plugins

LYNX ships its own native instruments and effects. Most of them are **clean-room re-implementations of the
native plugins of FL Studio**, written from scratch after a behavioural study of each plugin (decompilation
studied, the official manual and project data, then LYNX's engine checked against the original plugin running
unmodified in a test harness). How each one was decoded is documented round by round in
[`docs/analise/`](analise/); the full technical status is in [`docs/STATUS.md`](STATUS.md).

FL Studio's plugin names are used below only to say which plugin each LYNX engine is compatible with or inspired
by. No Image-Line code, audio or wave data is part of LYNX: where an engine needs a plugin's built-in waves,
samples, impulses or tables, LYNX reads them at run time from **your own** FL Studio installation (marked †).

## Status legend

| Status | Meaning |
|---|---|
| **Exact** | bit-identical to the original plugin's output on every state found in an FL Studio installation (demo projects and presets) and on synthetic tests |
| **Near-exact** | bit-identical on most states; the rest differ below about −100 dB (last-bit rounding of the original's math library) — inaudible |
| **Close** | the same algorithm; within −60 dB of the original on (almost) all tested states, typically −80 dB or better; the gaps are listed |
| **Approximate** | the plugin's saved settings are mapped onto a LYNX built-in effect or synth: similar sound, not the same engine |
| **Placeholder** | the state is kept and the plugin makes no sound / passes audio through, as the original does in FL Studio 2026 |

**How you get it:** *Insert* = can be added to any mixer insert (Mixer slot menu or the F8 quick-add palette).
*Import* = created when you open an FL Studio project (`.flp`) or drop an FL effect preset; the engine is kept
in the `.lynx` project and its controls stay editable and automatable.

## Summary

| Type | Exact | Near-exact | Close | Approximate | Placeholder | Entries |
|---|---:|---:|---:|---:|---:|---:|
| Generators | 12 | 10 | 7 | 1 | 2 | 32 |
| Effects | 34 | 10 | 1 | 8 | 1 | 54 |

(Some entries cover several FL plugins that share one LYNX engine, e.g. the sampler or Parametric EQ / EQ 2.)

## Generators

All generators below are created when an FL Studio project is opened. New projects start with LYNX's own
instruments (built-in synth, sampler, drums) or any VST3/CLAP plugin.

| LYNX engine | Compatible with / inspired by (FL Studio) | Status | Notes, what is missing |
|---|---|---|---|
| 3x Osc engine | 3x Osc | Exact | 239/239 non-HQ states bit-identical at 44.1/48/96 kHz; HQ oscillators within −137 dB; random start phases are per instrument (FL: per session) |
| FL Keys engine † | FL Keys | Exact | all demo states and 150-note stress runs bit-identical |
| DirectWave engine † | DirectWave | Exact | all 57 demo programs bit-identical with their own samples |
| DX10 engine | Fruity DX10 | Exact | 64/64 installation states and 12/12 project channels |
| SimSynth engine | SimSynth | Exact | all 76 installation states; automation not wired |
| Fruit Kick engine | Fruit Kick | Exact | release fades and all 8 filter types included |
| BassDrum engine † | BassDrum | Exact | 228/228 cases at 44.1 and 48 kHz |
| Toxic engine † | Toxic Biohazard | Exact | 3 demo channels, voice and effects; the step sequencer is ignored |
| Plucked! engine | Plucked! | Exact | for the same random seed (FL seeds from the clock) |
| BeepMap engine | BeepMap | Exact | 9 preset/render-test states; only uncompressed 24/32-bit BMPs |
| Drumaxx engine | Drumaxx | Exact | all 133 states on random notes; its pattern sequencer and separate outputs are not played |
| Slayer engine † | FL Slayer | Exact | 64 presets and 340 random parameter sets; the engine's 5 ms control-grid phase depends on FL's history |
| MiniSynth engine † | MiniSynth | Near-exact | 11/12 project states and 47/91 presets bit-identical, the rest −107…−147 dB |
| Kepler engine † | Kepler | Near-exact | default patch bit-exact, demo channels −124 dB; self-oscillating presets drift in phase; Kepler Exo falls back to the synth |
| PoiZone engine | PoiZone | Near-exact | 384/385 presets bit-identical with the delay off; delay filters median −79 dB |
| Transistor Bass engine † | Transistor Bass | Near-exact | 55/128 programs bit-exact, worst −108.6 dB; CC/pitch-bend not modelled |
| Sawer engine † | Sawer | Near-exact | 423 presets bit-identical with effects off; reverb −137 dB; modulation by aftertouch/bend not fed |
| Granulizer engine | Fruity Granulizer | Near-exact | within −128 dB on 100 random states; slice modes and tempo sync not played |
| Autogun engine † | Autogun | Near-exact | within −127 dB on the template states (Ogun engine + Soundgoodizer stage) |
| DrumSynth engine † | Fruity DrumSynth Live | Near-exact | all 120 patches; recorded waves need the exact engine DLL build; sends and Mod X/Y not modelled |
| BooBass engine † | BooBass | Near-exact | needs the exact engine DLL build (otherwise a saw approximation) |
| Sytrus engine | Sytrus | Close | median −117 dB on 268 demo channels, 265 within −60 dB; chaotic FM patches can't be sample-exact; states older than v17 use the synth conversion |
| Harmor engine † | Harmor | Close | 97/99 demo channels within −60 dB (median −80 dB) incl. image synthesis and the effects rack; resynthesis images and some filter shapes missing |
| Harmless engine † | Harmless | Close | 135/139 demo channels within −60 dB with the effects rack |
| Ogun engine | Ogun | Close | 20/20 demo channels within −60 dB; Random spectra are deterministic |
| GMS engine † | GMS | Close | model within −60 dB on 9/9 demo channels; FL's shared random generator makes noise/phases differ by design |
| Morphine engine † | Morphine | Close | model within −60 dB on 7/7 demo channels; release loops and unison not modelled |
| Sakura / Drumpad models | Sakura, Drumpad | Close | physical models rebuilt from the engines; Sakura's resonators within −72 dB; no CC sources |
| Sampler (FL voice) | Channel sampler, FPC, Slicex, Fruity Slicer, Wave Traveller | Near-exact | FL's interpolators and sampler voice (within one 16-bit code of FL's renders); per-plugin extras partly; WAV loop points not read |
| Built-in synth | FLEX | Approximate | FLEX packs are encrypted and never opened: a synth patch chosen by preset name |
| Silent channel | Wasp / Wasp XT | Placeholder | silent in FL Studio 2026 (64-bit placeholder); state kept |
| Silent channel | Fruity Dance | Placeholder | a visual plugin; state kept |

## Effects

| LYNX effect | Compatible with / inspired by (FL Studio) | How | Status | Notes, what is missing |
|---|---|---|---|---|
| Parametric EQ | Fruity Parametric EQ 2, Fruity Parametric EQ | Insert | Near-exact | 912/1139 states bit-identical, all within −166 dB; linear phase −127 dB |
| Fruity Compressor | Fruity Compressor | Insert | Exact | all 70 states, sidechain included |
| Fruity Limiter | Fruity Limiter | Insert | Near-exact | 101/272 bit-identical, all within −129 dB |
| Fruity Multiband Compressor | Fruity Multiband Compressor | Insert | Exact | all 55 states, linear-phase mode included |
| Reeverb 2 | Fruity Reeverb 2 | Insert | Exact | all 293 states, 22.05–96 kHz |
| Reeverb | Fruity Reeverb | Insert | Exact | its 7 demo states |
| LuxeVerb | LuxeVerb | Insert | Exact | all 23 states and 60 random ones up to 192 kHz |
| Fruity Delay 2 | Fruity Delay 2 | Insert | Exact | all 60 states |
| Fruity Delay 3 | Fruity Delay 3 | Insert | Exact | all 85 states and 110 random ones |
| Fruity Delay Bank | Fruity Delay Bank | Insert | Exact | all 52 states, 96 random ones |
| Multiband Delay | Multiband Delay | Insert | Exact | IIR crossovers; linear phase within −120 dB |
| Fruity Chorus | Fruity Chorus | Insert | Exact | start phase 0 (FL: clock-seeded) |
| Hyper Chorus | Hyper Chorus | Insert | Exact | |
| Vintage Chorus | Vintage Chorus | Insert | Exact | |
| Fruity Flanger | Fruity Flanger | Insert | Exact | |
| Fruity Flangus | Fruity Flangus | Insert | Exact | |
| Fruity Fast Dist | Fruity Fast Dist | Insert | Exact | |
| Fruity WaveShaper | Fruity WaveShaper | Insert | Exact | an editor-only toggle is not identified |
| Fruity Soft Clipper | Fruity Soft Clipper | Insert | Exact | |
| Fruity Blood Overdrive | Fruity Blood Overdrive | Insert | Exact | |
| Fruity PanOMatic | Fruity PanOMatic | Insert | Exact | starts settled (FL fades in a fresh instance) |
| Fruity Balance | Fruity Balance | Insert | Exact | |
| Stereo Shaper | Fruity Stereo Shaper | Insert | Exact | |
| Stereo Enhancer | Fruity Stereo Enhancer | Insert | Exact | |
| Transient Processor | Transient Processor | Insert | Exact | |
| Spreader | Spreader | Insert | Exact | |
| Fruity Fast LP | Fruity Fast LP | Insert | Exact | |
| Fruity Free Filter | Fruity Free Filter | Insert | Exact | |
| Fruity Filter | Fruity Filter | Insert | Exact | |
| Frequency Splitter | Frequency Splitter | Insert | Exact | IIR mode; FFT mode within −113 dB |
| Hardcore † | Hardcore | Insert | Exact | cabinet impulses from the installation |
| Vocoder | Fruity Vocoder | Insert | Exact | every state, 500 random runs |
| Love Philter | Fruity Love Philter | Import | Exact | all 86 states, live and export |
| Pitch Shifter | Pitch Shifter | Import | Exact | all 15 states and 70 random ones |
| EQUO | EQUO | Import | Exact | the send output to another track is not routed |
| Gross Beat | Gross Beat | Import | Exact | all 41 states; MIDI control of the slots and spline curves missing |
| Squeeze | Fruity Squeeze | Import | Near-exact | within 5·10⁻⁹ of the reference model, which is bit-exact with FL |
| Effector | Effector | Insert | Near-exact | all 12 algorithms; reverb −100 dB |
| Maximus † | Maximus | Import | Near-exact | 96/100 demo states within −139 dB; v15 linear-phase split and Master Mid keep an approximation |
| Soundgoodizer † | Soundgoodizer | Import | Near-exact | within −126 dB on every preset/amount |
| Convolver † | Fruity Convolver | Import | Near-exact | −127…−152 dB; states with a shaped EQ curve are approximated |
| Distructor † | Distructor | Import | Near-exact | bit-identical except the Harmor shapes and the cabinet |
| Vocodex † | Vocodex | Import | Near-exact | −136…−145 dB (internal Sytrus carrier −112 dB) |
| Vintage Phaser | Vintage Phaser | Import | Near-exact | within −121 dB |
| Pitch correction | Pitcher | Insert | Close | bit-exact with the reference model; MIDI mode / Harmonize get no MIDI in LYNX |
| Newtone / Newtime | Newtone, Newtime | Import | Placeholder | pass-through (as FL) + the source sample in slave mode; note/time edits not reproduced |
| Utility | Fruity Send, Transporter | Insert | Approximate | mapped to a utility stage |
| EQ | Fruity 7 Band EQ, Fruity Bass Boost | Insert | Approximate | settings mapped to LYNX's EQs |
| Delay | Fruity Delay (v1) | Insert | Approximate | |
| Chorus | Fruity Phaser | Insert | Approximate | |
| Utility | Fruity Center, Fruity Phase Inverter | Insert | Approximate | |
| Pitch shift / Frequency shifter | Frequency Shifter | Insert | Approximate | |
| Limiter | Emphasis | Insert | Approximate | not ported yet |
| Parametric EQ | Low Lifter | Insert | Approximate | harmonics generator missing; not ported yet |

**Windows.** Every native effect opens its own window (double-click its Mixer slot, or the **Editor** button over its
knobs). For the plugins studied so far — Fruity Limiter, Parametric EQ 2, Delay 3, Multiband Compressor, Reeverb 2,
LuxeVerb, Compressor, Filter, Fast Dist, Chorus, Flanger, Stereo Enhancer, Gross Beat, Delay 2, Reeverb, Flangus, Soft
Clipper, Blood Overdrive, PanOMatic, Stereo Shaper, Transient Processor, Free Filter, WaveShaper, Vintage Chorus, Hyper
Chorus, Balance, Squeeze, Soundgoodizer, and the 3x Osc, FL Keys, BooBass, Sytrus (main page) and Harmor pages
of the channel settings — the window has the same controls, sections, captions and arrangement as the FL plugin,
drawn in LYNX's own style, with a presets bar that lists the plugin's FL presets from your installation
([`docs/analise/UI_STUDY_plugins93.md`](analise/UI_STUDY_plugins93.md)); the other effects show their controls as a
grid of knobs.

Also on every insert: LYNX's own Filter, Distortion, Lo-fi, Reverb, Compressor, Limiter, Crossover, Volume gate,
Rhythmic envelope, Auto-pan and the parallel container (Patcher graphs from FL projects are reduced to it, each effect
inside running its own engine), plus any VST3/CLAP plugin. FL's Peak, Envelope and Keyboard Controllers are imported
as LYNX controllers.

---

FL Studio and the names of its plugins (3x Osc, Sytrus, Harmor, Fruity …) are trademarks of Image-Line Software.
LYNX is an independent project and is not affiliated with, endorsed or sponsored by Image-Line. The names are used
only to describe compatibility.
