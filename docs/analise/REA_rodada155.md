# Round 155 (render155): exported files start with half-loaded plugins (yabridge)

2026-10-24. Bug report from a Linux user: "VSTs over yabridge tend to not load properly at the render stage, thus
creating a period at the start of the exported file that has half-loaded effects."

Evidence markers: [CODE] read in LYNX's source (file named); [TEST] proven by a test in `tests/Render155Tests.inc`;
[RUN] measured with the Windows build; [FL] FL Studio observed as a user / its binary's strings (no decompilation was
needed this round, nothing copied); [model] LYNX's own design choice.

## 1. Cause

- [CODE] Every export builds its **own engine** (`ExportJob`, `src/engine/Exporter.cpp`): `setSong` creates a fresh
  instance of every hosted plugin, prepares it and restores its saved state (`PluginHost.cpp`,
  `setStateInformation` right after `preparePlugin`), all on the message thread. `run()` then starts the render on the
  Export window's thread at once.
- [CODE] `renderOffline` (`src/engine/AudioEngine.cpp`) then switches every plugin to the offline mode
  (`setPluginRenderMode` → `setNonRealtime` + `prepareToPlay`, i.e. VST3 `setupProcessing` + re-activation) and renders
  sample 0 in the next instant.
- An offline render runs many times faster than real time: the first second of the file is rendered in a few tens of
  milliseconds of wall time after the plugins were created and re-activated.
- yabridge forwards the state, the re-activation and the parameter / latency notifications over IPC to the Windows
  plugin running in Wine; several plugins apply a restored state, or load what it refers to (samples, impulse
  responses, wavetables, oversampling buffers), on their own threads or GUI thread. The same happens natively with
  samplers that stream in the background. Until that work is done the plugin processes with its defaults: the
  "half-loaded effects" at the start of the file.
- [CODE] Two smaller contributors: (1) FL's initial values (`Song::initialValues`) were pushed by `play()` at the
  first rendered block, so a plugin started smoothing towards them inside the file; (2) the PDC trim was read from the
  latencies *before* the render, so a latency reported late was not trimmed.
- [FL] FL Studio renders from the project's live plugin instances (the ones that played; playback stops while the
  render runs), so its plugins have long finished loading. A search of `FLEngine_x64.dll`'s strings found no separate
  "wait for plugins" option ("Let the plugin know when rendering to audio file" is the "Notify about rendering mode"
  LYNX already has). LYNX keeps its separate export engine (exports never touch what is playing), so it has to give
  the fresh instances the time a real start would give them.

## 2. Fix: plugin warm-up before the render

`core/PluginWarmup.h` (policy, JUCE-free) + `AudioEngine::warmUpPlugins` (engine):

1. [model] Only when the song has hosted plugins (channels, inserts, inside Patchers; crashed ones skipped).
   Projects with only LYNX's own engines render exactly as before (they are ready when created).
2. FL's initial values are applied **before** the warm-up (`resetToInitialValues`), so the plugins settle on them.
3. Silent blocks are processed through the whole graph and thrown away, transport **stopped at the start
   position**: no note plays, and the rack evaluates automation clips / pattern events at that position every block,
   so the parameters the song starts with reach the plugins before the render.
4. It stops when all of these hold:
   - ≥ 0.5 s of audio processed (plugins that settle after a number of blocks),
   - ≥ 0.5 s of real time passed (work on the plugins' / bridge's own threads),
   - what the plugins report — latency and every parameter value (quantised to 1e-5) — has not changed for 0.3 s;
   or after 10 s (a plugin whose values never stop moving). After the minimum audio the blocks are paced to real time
   (sleeps of ≤ 10 ms when ahead), so the bridge gets wall time, not just blocks. The progress window's Cancel works
   during the warm-up.
5. A latency that changed gets its new PDC plan (`publishSong`, on the message thread as the 30 Hz timer does it; the
   Export window renders on a thread of its own), and the PDC trim uses the latency reported **after** the warm-up
   (`RenderOptions::trimPdc`).
6. LYNX's own units are re-prepared (`ChannelRackProcessor::prepareToPlay`: free-running LFOs, the delay
   compensation's lines, smart-disable counters), so they start the render as without the warm-up. The hosted plugins
   are **not** reset: in JUCE's VST3 host a reset re-activates the plugin, which is exactly what they had to settle
   from; they only received silence, so they carry no tail into the file.

Setting: Export window › "Plugin warm-up" (on by default; saved with the other export options as `pluginWarmUp`;
pt-BR "Aquecimento dos plugins"). `LYNX --export … --no-warmup` turns it off; the `--export` report prints
`warmup_blocks`, `warmup_ms`, `warmup_changes`, `warmup_timed_out`, `pdc_trim`.

Cost: an export with hosted plugins takes ~0.5–0.8 s longer (more only while a plugin is still changing, at most 10 s).

## 3. Tests

- [TEST] `render155_warm_up_waits_for_a_state_applied_after_n_blocks`: a fake plugin effect whose state (gain 0.5)
  lands after 40 blocks, after a drone instrument, rendered through the real `RackRenderer`. Without the warm-up the
  first sample is 2× the reference (half-loaded); with it every sample of the first block equals the reference (a
  plugin whose state is in place from the start).
- [TEST] `render155_warm_up_waits_for_a_state_applied_late_in_real_time`: the state lands 1.2 s of (simulated) wall
  time after creation whatever the number of blocks — the yabridge case. The warm-up lasts ≥ 1.2 + 0.3 s, stays paced
  to real time, and the first rendered samples are correct.
- [TEST] `render155_warm_up_minimums_timeout_and_cancel`: minimums, the 10 s timeout for a plugin that never settles,
  Cancel, parameter quantisation.
- [TEST] `render155_export_option_plugin_warm_up`: default on, saved and read back.
- [RUN] Windows, a real VST3 (see §4).

yabridge itself cannot be run on this machine (WSL, no Wine); the fake plugins model its two behaviours (late by
blocks, late by wall time).

## 4. Real VST3 check (Windows build)

[RUN] `LYNX.exe --export vst3.lynx out.wav [--no-warmup]` (Windows build of this branch, C:\Users\kauan\lynx-test\render155):
LYNX's Demo song with ValhallaSupermassive (VST3, free) first on the Master insert, then LYNX's Parametric EQ and
Limiter.

| | warm-up on | `--no-warmup` |
|---|---|---|
| `warmup_blocks` / `warmup_ms` / `warmup_changes` | 45 / 505 ms / 0 | 0 / 0 / 0 |
| render time (song 52 s) | 2058 ms | 1566 ms |
| `pdc_trim` | 66 samples | 66 samples |
| peak | 0.95787 | 0.95787 |
| two runs bit-identical | yes | yes |

- The warm-up costs half a second for a plugin that is ready at once (the minimums), and nothing changed during it.
- Warm vs cold files: bit-identical for the first 0.30 s, then the reverb tail differs slightly — Supermassive's
  free-running modulation has run 0.5 s longer (as it would in a real-time start). Expected; both are deterministic.
- The CLI render runs on the message thread (no message pump available: JUCE_MODAL_LOOPS_PERMITTED is 0), so there
  the warm-up only gives wall time to the plugins' own threads; the Export window renders on its own thread, so the
  message thread (JUCE's VST3 host callbacks, yabridge's replies handled there) keeps running during the warm-up.

## Where I stopped

- Landed: see the commit list in the final report.
- Not done: no per-plugin "needs longer warm-up" option (FL has none either); if a reporter still hears a bad start
  with a specific plugin, the next step is a longer minimum for bridged plugins only (`PluginRef::separateProcess`
  or a yabridge path in the plugin's file name) rather than for every export.
- Not tested with yabridge itself (no Wine here): ask the reporter to try 0.1.16 and, if it persists, send the
  `--export` report lines `warmup_*` for their project.
