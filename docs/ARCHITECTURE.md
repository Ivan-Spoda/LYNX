# Architecture

Goal: a DAW with the FL Studio workflow, light enough to run on any PC whatever the project size.
Clean-room reimplementation: FL Studio is studied to understand its *behaviour*, never to copy code.

## Audio engine rules

1. The audio thread never allocates memory, never locks a mutex and never does I/O.
2. UI → audio communication only through atomics or immutable snapshots swapped without locks
   (`core/SnapshotExchange.h`): the UI edits the `Song`, publishes a copy, and the audio thread hands the
   previous one back for the UI to free.
3. Every structural change (adding a track, plugin or route) is built off the audio thread and swapped in at once.

## Layers

| Layer | Responsibility |
|---|---|
| `core/` (no JUCE) | `Song` model, sample-accurate `Scheduler`, built-in instruments and effects, `RackRenderer` (chunked rendering, mixer layers, PDC, automation), `.flp` import |
| `flp/` | low-level `.flp` event reader |
| `engine/AudioEngine` | audio device, transport, snapshot publishing, undo/redo |
| `engine/PluginHost`, `ClapFormat`, `PluginSandbox` | VST3/CLAP scanning and hosting, optional separate-process plugins |
| `engine/ProjectFile` | `.lynx` (JSON) save/load |
| `engine/SampleLibrary`, `StreamPrefetcher`, `WavPackFormat` | sample loading (incl. FL's Ogg-in-WAV and WavPack), disk streaming |
| `engine/Exporter` | offline rendering to WAV/FLAC/OGG |
| `ui/` | Channel Rack, Piano Roll, Playlist, Mixer, channel/plugin/EQ/automation windows, browser |

## Scalability ("runs on any PC")

- The renderer processes channels in parallel and mixer tracks in dependency layers on a worker pool.
- Notes start on their exact sample: rendering is split into chunks at every event.
- Long samples are streamed from a memory-mapped disk cache instead of being loaded whole.
- Idle channels (no voices, no notes in the chunk) are skipped; silent effects are smart-disabled.
- Tracks can be frozen (rendered to audio).
- The UI is drawn on the GPU (Direct2D) and fully separated from audio.
