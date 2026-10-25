# LYNX on Linux: audio

LYNX plays through **PipeWire** directly when it runs (Ubuntu 22.10+, Fedora 34+, Debian 12, current Arch / Manjaro
and most other current distributions), else through **JACK** (a running JACK server) or **ALSA**. Nothing extra has to
be installed: LYNX loads `libpipewire-0.3.so.0`, `libjack.so.0` and `libdbus-1.so.3` from the system only when they are
there.

## Choosing the driver

*Options › Audio settings › Driver*:

| Driver | When | Notes |
|---|---|---|
| **PipeWire (native)** | a PipeWire daemon answers | LYNX's own PipeWire client. Low latency, shared with every other program. Chosen automatically on the first start. |
| **JACK / PipeWire** | a JACK server runs, or PipeWire with `pipewire-jack` | JUCE's JACK client. Use it with a real JACK server (jackd / jackdbus). |
| **ALSA** | always | Talks to the sound card. `default` / `pipewire` share it; a `hw:` device takes the card for LYNX alone. |

A first start (no saved audio setup) opens the first of PipeWire → JACK → ALSA that has devices. A saved choice is kept.

## PipeWire (native)

- **Output device**: *System default* follows the output chosen in the desktop's sound settings, also when a device is
  plugged in or removed (WirePlumber moves LYNX's stream; LYNX does not stop). Choosing a named device pins LYNX to it;
  when that device is unplugged LYNX falls back to *System default* and keeps the buffer size.
- **Input device**: *(none)*, *System default* or a named source. Recording and mixer-insert inputs use it.
- **Buffer size**: 32–2048 samples. LYNX asks PipeWire for that quantum (`node.latency`); when another program asks for
  a smaller one PipeWire runs smaller (LYNX follows), when it runs a larger one LYNX processes it in blocks of the chosen size.
- **Sample rate**: 44.1–192 kHz. LYNX asks PipeWire to switch (`node.rate`); if the graph keeps its rate, PipeWire
  resamples LYNX's stream.
- **Routing**: LYNX appears as `LYNX` in `pw-link`, `qpwgraph`, Helvum or Carla (ports `LYNX:output_FL`,
  `LYNX:output_FR`, `LYNX:input_FL`…), so it can be wired to other programs.
- **Daemon restart** (`systemctl --user restart pipewire`, updates): LYNX reconnects within about two seconds.
- `LYNX_NO_PIPEWIRE=1` hides the native driver (for comparisons).
- MIDI devices reach LYNX through the ALSA sequencer, which PipeWire bridges (*Options › MIDI settings*).

## Real-time priority

The audio thread should run with real-time priority, or the sound can break up when the desktop is busy. LYNX asks for
it on its own, without root, in this order:

1. `SCHED_FIFO` directly when your limits allow it (`ulimit -r` above 0: the `audio` group / `limits.conf`, as JACK
   set-ups have);
2. **RealtimeKit** (`rtkit`, installed on most desktops; PipeWire uses it too);
3. the desktop portal's Realtime interface (Flatpak and other sandboxes).

*Options › Audio settings › Status* shows the result ("Audio thread: real-time priority (RealtimeKit, priority 20)").
When nothing allowed it, the line says so, and the first time the sound breaks up LYNX explains the fix once:

- install rtkit (`sudo apt install rtkit`, `sudo dnf install rtkit`, `sudo pacman -S rtkit`), **or**
- add yourself to the `audio` group with a real-time limit, e.g. `/etc/security/limits.d/audio.conf`:
  ```
  @audio   -  rtprio     95
  @audio   -  memlock    unlimited
  ```
  then `sudo usermod -aG audio $USER`,

and log out and in again. `LYNX_NO_RT=1` skips the request (for comparisons).

## Checking from a terminal

```
LYNX --devices out.txt                                     # driver types, devices, PipeWire version and defaults
LYNX --devicetest PipeWire "System default" out.txt "*"    # opens output + input for 1 s
LYNX --devicetest PipeWire "Loopback" out.txt "Loopback" --buffer 64 --roundtrip   # round trip through a loopback
LYNX --rtstress project.lynx out.txt 30 --type PipeWire --buffer 128 [--log dir] [--settingsshot audio.png]
```

`--rtstress` reports the device, the audio thread's priority, deadline misses, late calls and dropouts. A PipeWire
loopback for `--roundtrip`:
`pw-loopback --capture-props='media.class=Audio/Sink node.name=loop_sink node.description=Loopback' --playback-props='media.class=Audio/Source node.name=loop_source node.description=Loopback'`.

Measured in round 161 (`docs/analise/REA_rodada161a.md`): through a PipeWire loopback the round trip is exactly one
quantum (64 samples = 1.33 ms at 48 kHz, 128 = 2.67 ms, 256 = 5.33 ms); the demo project at 32/64/128/256 samples ran
without a missed deadline or dropout (4–10 % of one CPU core).
