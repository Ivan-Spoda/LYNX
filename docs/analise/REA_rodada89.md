# Round 89: why a real FL project plays much quieter in LYNX (level audit)

Branch `feature/level89`. English, clean-room: FL Studio's binaries were only read for data layouts (no code or
pseudocode copied). Trigger: a user's real project (newer FL, third-party VST plugins, own samples) plays much
quieter in LYNX than in FL. The project itself was not available yet (see `tmp/r89/ASK_USER.md`), so this round
covers every plausible cause found in LYNX and on the FL-vs-LYNX scoreboard of round 80.

## 1. Third-party plugins: the most likely cause for a real user project

Findings in LYNX before this round **[code read]**:

- **VST2 was not hosted at all.** JUCE 8 only hosts VST2 with Steinberg's VST2 SDK, which is no longer
  distributed, so `JUCE_PLUGINHOST_VST` was off. Every VST2 in an FL project (Serum 1, Sylenth1, Spire, many
  limiters/clippers and freebies are VST2-only) was "missing": an instrument channel played a built-in synth preset
  instead, an effect slot was **removed** (a master limiter/maximizer gone ⇒ the whole song several dB quieter).
- A VST2 found as VST3 (FL swaps them) got **no state** (`convertState` only carried VST3 → VST3): the plugin played
  its default patch at its default level, and nothing told the user.
- A plugin found but **failing to load** became a silent instrument / bypassed effect with the error dropped.
- A Fruity Wrapper blob that could not be parsed (e.g. a wrapper version newer than 12) was dropped silently: the
  report skipped "Fruity Wrapper" entries.
- Hosted plugins had **no play head**: JUCE gives them no tempo/position/transport, so tempo-synced effects
  (volume shapers, sidechain-pumping tools, LFO filters, synced delays) ran at a default 120 BPM or stopped —
  a ducking tool stuck "down" makes a channel or the master much quieter.

Changes:

- `src/engine/Vst2Format.{h,cpp}`: a VST2 host of our own (`juce::AudioPluginFormat` named "VST"), written from the
  public VST 2.4 binary interface (AEffect, dispatcher opcodes, host callback, VstEvents/VstMidiEvent, VstTimeInfo):
  scan (also shell plugins: one DLL, several IDs, via `effShellGetNextPlugin` / `audioMasterCurrentId`), instance
  with stereo/mono buses (extra outputs of multi-out synths are rendered and the first pair is used; extra inputs get
  silence), MIDI in, parameters (automation from the plugin, display text), programs, latency (`initialDelay`,
  `audioMasterIOChanged`), tail, time info from the host play head, process level (offline while exporting), editor
  in a child HWND with `effEditIdle`. Folders: Program Files\VSTPlugins, Steinberg\VSTPlugins, Common Files\VST2,
  Common Files\Steinberg\VST2, plus FL's own plugin folders (already in the suggested folders).
- **State**: the VST2 instance saves and restores **FL's own wrapper layout** (§2), so an FL project's VST2 state is
  handed to the plugin unchanged (`adoptFlPlugins` → `convertState`), with FL's rules: the chunk goes to
  `effSetChunk` as a bank (as a program only for version −6 states without "all programs"); raw parameters between
  `effBeginSetProgram`/`effEndSetProgram`, per program when all programs were saved; then the current program.
- `findForeign`: VST2 by its ID (`AEffect::uniqueID` = record 0x33), same-format preference for VST2.
- Hosted plugins (VST2, VST3, CLAP) get a play head (`TimelinePlayHead` in `PluginHost.cpp`) fed by a timeline the
  renderer publishes per chunk (`hostTimeline()` in `core/Instruments.h`): tempo (incl. tempo automation), PPQ
  position, bar start, time signature, playing.
- Warnings: `adoptFlPlugins(..., withoutState)` lists plugins found in the other format; `PluginHost` records load
  failures (`getLoadFailures`). Both go to the **Missing files** window (Plugins tab, opened automatically after
  the import) with what LYNX did ("Its default settings: the project saved them for the other format",
  "Silence: the plugin is installed but did not load"), to the import report, and the window's summary now reads
  "N plugin(s) will not sound as in FL Studio …". Unreadable wrapper blobs are counted in the import report
  ("Fruity Wrapper (plugin state not readable)").
- Test plugin `tools/testplugin/lynx_test_vst2.c` (a VST2 sine synth with a chunk state, built with zig) and
  `LYNX.exe --vst2test "LYNX Test Synth.dll" report.txt`: scan, import from an FL-layout VST2 state found by ID,
  render with two saved gains (level must follow the restored state ×4) and check the plugin read the song tempo.

## 2. FL's VST2 state layout per version [DECOMP, FLEngine_x64.dll of FL Studio 2026]

The state probe at `0xAC4920` (with its readers `0xAC45E0`, `0xAC4700`, `0xAC4780`) reads, all little-endian:

| field | present when |
|---|---|
| i32 version | always; accepted −9 … −1 (≥ 0 and 123456 = other/none) |
| u8 flags | v ≤ −6 (bit0 all programs; bit3 chunk plugin; bit1 an option) |
| i32 size (−2 ⇒ i64 size follows) | v ≤ −5 |
| i32 (meaning not confirmed) | v = −7 and size = 0 |
| i32 current program | v ≤ −6 |
| i32 current program | v = −5 or −4 (inside the chunk reader, before the size) |
| i32 size (−2 ⇒ i64) | −4 ≤ v ≤ −1 |
| chunk[size] | size > 0 |
| i32 n + n × f32 | v = −2, or v ≤ −5 with size 0 or −1 |
| i32 m + names (char[25]) | if ≥ 4 bytes remain; v ≤ −6 reads one name, older versions m names |

`parseFlVst2State`/`writeFlVst2State` (`core/FlWrapper`) implement this; every VST2 state in the installation's
demo projects decodes and writes back byte-identical (test `real FLPs`), plus synthetic −9/−6/−5 cases.

## 3. Level paths checked in LYNX

- Live playback and export run the same graph (`ChannelRackProcessor` → `RackRenderer`) and the same master
  `softClip` (linear up to 0.9, then a tanh knee below 1.0); the only offline differences are quality switches
  (`offlineRendering()`: resampler, Sytrus/Ogun/EQUO oversampling) — no gain stage differs. Device output (WASAPI/
  ASIO) gets the buffer unchanged. The preview player (browser) alone has a 0.8 gain.
- `PluginInstrument`/`PluginEffect`: stereo main bus requested, all buses enabled; mono plugins feed both sides;
  channel volume/pan applied after the plugin like any channel.

## 4. Scoreboard clusters (round 80 FL renders vs LYNX)

(filled in from the demo-song analysis below)

## Where I stopped / next steps

- Waiting for the user's .flp + plugin list (`tmp/r89/ASK_USER.md`).
