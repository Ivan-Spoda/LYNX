# Round 161a (linuxaudio161): "LYNX Audio" for Linux — native PipeWire driver and real-time priority

2026-10-25. Goal (the user's "lynx-jack" idea): on Linux, LYNX should get low-latency audio from PipeWire directly,
pick the best driver by itself, and run its audio thread with real-time priority without root — with nothing extra
for the user to install.

Evidence markers: [MEAS] measured here in WSL2 (Ubuntu 26.04, WSLg, no /dev/snd, no sudo) against a user-prefix
PipeWire 1.6.2 + WirePlumber 0.5.13 (`~/opt/pw-prefix`, config in `~/opt/pw-prefix/etc/pipewire/pipewire.conf.d/
lynx-test.conf`: a null sink as driver, pulse-tunnel sink/source to WSLg's PulseServer); [TEST] core test
`linux_audio_driver_choice_and_pipewire_helpers` (build-local); [CODE] read in the source.

## 1. What was built

| Part | Where | Notes |
|---|---|---|
| PipeWire device type "PipeWire" | `src/engine/PipeWireDevice.{h,cpp}` | `libpipewire-0.3.so.0` through dlopen/dlsym (no link-time dependency); vendored MIT headers (`third_party/pipewire`, 107 files of pipewire 1.6.2, only what is included) |
| Device list | same | registry listener: `Audio/Sink`, `Audio/Source`, `Audio/Duplex` nodes, their ports (channel positions), the `default` metadata (default sink/source) and `settings` (clock.rate). "System default" first (no `target.object`: WirePlumber links to the default and moves the stream when it changes or disappears) |
| Streams | same | playback `LYNX` (`Stream/Output/Audio`) + capture `LYNX` (`LYNX (input)`), F32 planar, `PW_STREAM_FLAG_RT_PROCESS` (callback on PipeWire's data thread), `node.latency` = buffer/rate, `node.rate` = 1/rate, same `node.group` for both (one driver per cycle, as pw-loopback). Input crosses to the output callback through a lock-free FIFO |
| Quantum handling | same | `pw_buffer.requested` (≥ 0.3.50) else `spa_io_position` duration × rate ratio; a larger graph quantum than the engine's block is processed in chunks of the block |
| Hot-plug | same | 2 Hz timer: when sinks/sources change, `callDeviceChangeListeners()` (the device manager drops a vanished named device and falls back to "System default"; the device's default buffer size is the last one opened so the user's buffer is kept) |
| Daemon restart | same | core error `-EPIPE` marks the connection broken; the type reconnects every 2 s and re-creates the open devices' streams on the new connection, playing on |
| Driver order | `core/Preferences.cpp` `preferredLinuxDriver`, `AudioEngine::choosePreferredDriver` | first start (no saved setup or it failed): PipeWire → JACK (running server) → ALSA → anything with devices |
| Real-time priority | `src/platform/LinuxRealtime.{h,cpp}` | each audio callback thread and worker is noted (once, RT-safe); the message thread promotes it: SCHED_FIFO via RLIMIT_RTPRIO → RealtimeKit (system bus) → desktop portal (session bus; first inside Flatpak). libdbus-1 through dlopen; RLIMIT_RTTIME set to rtkit's/portal's maximum first. Threads already SCHED_FIFO/RR (PipeWire's, JACK's) are reported as such |
| UI | `ui/SettingsPages.cpp`, `ui/MainComponent.cpp` | Driver "PipeWire (native)" + explanation; Status: output channel names, "Audio thread: real-time priority (…)" or the warning with the fix; the first dropout without RT shows a one-time hint (flag file `realtime-hint-shown`). pt-BR for all strings |
| Diagnostics | `Main.cpp` | `--devices` lists PipeWire (+ version/defaults line); `--devicetest … --roundtrip` (clicks out, detected on input 1, median delay); `--rtstress … --settingsshot file.png`; rtstress reports "audio thread: …" |
| Docs | `docs/LINUX.md` (new), `docs/PLATFORMS.md`, `README.md`, `THIRD_PARTY.md` | |

## 2. Measurements [MEAS]

Demo project (`Demo.lynx`), `--rtstress … --type PipeWire --no-window`, default output = null sink (timer driver),
48 kHz, Ryzen 5 3600; RT priority not available here (RLIMIT_RTPRIO 0, no rtkit daemon in WSL), so these numbers are
without real-time scheduling.

| Buffer | Callbacks (expected) | Over deadline | Late calls | Driver dropouts | Avg / max load | Process CPU (idle machine) |
|---|---|---|---|---|---|---|
| 32 | 30001 in 20 s (30000) | 0 | 0 | 0 | 2.8 % / 27.6 % | 9.5 % of one core |
| 64 | 15000 in 20 s (15000) | 0 | 0 | 0 | 2.1 % / 16.3 % | 6.4 % |
| 128 | 7500 in 20 s (7500) | 0 | 0 | 0 | 1.6 % / 5 % | 4.9 % |
| 256 | 3750 in 20 s (3750) | 0 | 0 | 0 | 1.5 % / 4.3 % | 4.1 % |

Same project at 128 through the other drivers (fresh profiles, `LYNX_NO_PIPEWIRE=1`): ALSA through the pipewire-alsa
plugin 6.1 % CPU, 6891 callbacks at 44.1 kHz, 0 misses; JACK through pipewire-jack could not get 128 (JUCE's JACK
client runs at the server's 1024 frames: 937 callbacks, 2.1 % CPU). The native driver is the only one here that gets
the asked-for quantum.

Round trip through a PipeWire loopback (`pw-loopback`, Audio/Sink → Audio/Source), `--devicetest PipeWire Loopback …
Loopback --roundtrip`: 11 of 11 clicks found, every one at exactly one quantum — 64 → 64 samples (1.33 ms), 128 → 128
(2.67 ms), 256 → 256 (5.33 ms). That is the graph's own one-cycle delay: LYNX's input FIFO adds nothing.
Reported input latency = block (FIFO) + PipeWire's delay; output latency = PipeWire's `pw_time.delay` (0 on the null
sink / loopback).

Behaviour checks:
- Ports: `pw-link -l` shows `LYNX:output_FL |-> null_sink:playback_FL`, `LYNX:output_FR |-> null_sink:playback_FR`.
- Follow the default: during a run on "System default" a new sink was created (`pw-cli create-node … object.linger`),
  made default with `wpctl set-default` → links moved to `hot_sink:playback_FL/FR`; the sink was destroyed → links back
  on `null_sink`. The device list changed twice (callDeviceChangeListeners), the device was never reopened, 5997 of
  6000 callbacks, 2 late calls at the two moves.
- Pinned device unplugged: run on "Loopback", pw-loopback killed at 6 s → LYNX fell back to "System default" (first
  version reopened at the default 256 frames; now keeps the user's buffer).
- Daemon restart: pipewire + wireplumber killed at 5 s, restarted at 8 s → LYNX reconnected and played on (5982 of
  7500 callbacks: ~4 s gap = the 3 s outage + reconnect), links back on the null sink.
- Real-time request: without rtkit/limits → "audio thread: normal priority" (the warning on the Audio page). With a fake
  `org.freedesktop.portal.Realtime` on the session bus (python, logs calls): LYNX read MaxRealtimePriority /
  RTTimeUSecMax and called `MakeThreadRealtimeWithPID(pid, tid, 20)` for PipeWire's data thread and the 7 workers;
  rtstress reported "audio thread: desktop portal, priority 20". The rtkit path uses the same DBus helpers on the
  system bus (rtkit answered ServiceUnknown here: not installable without root).
- Fallbacks from a fresh profile: PipeWire present → "PipeWire / System default"; `LYNX_NO_PIPEWIRE=1` with
  pipewire-jack's libjack on the path → "JACK / Null sink (test)", 469/469 callbacks at 1024; `LYNX_NO_PIPEWIRE=1`
  without JACK, ALSA through the pipewire-alsa plugin (ALSA_CONFIG_PATH) → "ALSA / Default ALSA Output (PipeWire)",
  1292/1292 callbacks at 512 / 44.1 kHz, 0 dropouts. ALSA-only still works.
- Real WSLg speakers (pulse tunnel) listed as an output; `--devicetest` opens "System default" + input "*" (2 input
  channels, 186/187 callbacks in 1 s).
- Settings › Audio rendered with the live PipeWire device (`--settingsshot`): driver, device, 128 samples (2.7 ms),
  output channels "Left, Right", the normal-priority warning.

## 3. Not done / limits

- **Native PipeWire MIDI ports**: MIDI still comes through the ALSA sequencer (PipeWire bridges it, so hardware and
  PipeWire MIDI devices appear there). A `LYNX` MIDI port in pw-link would need a pw_filter with UMP/MIDI 1 control
  sequences — left for later.
- Real-time priority could not be *granted* in this WSL (no rtkit daemon, no rtprio limit, no sudo): verified up to
  the DBus calls with a fake portal; on a desktop with rtkit it should report "RealtimeKit, priority 20".
- No measurement on real ALSA hardware (WSL has no /dev/snd); PipeWire's own device latency is not included in the
  loopback round trip.
- When a pinned device comes back after being unplugged, LYNX stays on "System default" (the saved setup was
  replaced by the fallback), as JUCE does for every driver.

## Where I stopped

- 2026-10-25: everything above implemented and measured; core tests (build-local with FL Data: 1752810 checks, 0
  failures) + pt-BR complete; Windows build OK (it needed the two new files in `scripts/build-windows-zig.sh`).
  Landing batch 1 via land.sh; then remove the worktree and the wip branch.
