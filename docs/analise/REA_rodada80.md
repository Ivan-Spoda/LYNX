# REA round 80: FL reference renders vs LYNX (scoreboard)

Goal: measure how close LYNX's offline render of each FL project in the installation is to FL Studio's own render of
the same project, so later rounds work on the biggest real differences instead of guesses. FL's renders are made by
FL itself from its command line and stay outside the repo (cache `C:\Users\kauan\lynx-test\flref`).

## 1. Tools (in the repo)

- `scripts/flref-fl.sh <list> [cache]` — renders each listed `.flp` with `FL64.exe /R /Ewav` into
  `<cache>/fl/<group>/<name>.wav`, one FL instance at a time (FL forwards a second command line to a running
  instance), with a per-project timeout. Existing renders are kept.
- `scripts/flref-lynx.sh` — the same projects through `tools/renderwav` (`LYNX_RAW` float output).
- `scripts/flref_compare.py` — `pair`, `batch` (Markdown scoreboard + JSON lines) and `culprit` (renders a project
  with one channel / insert at a time to find which one carries the error). Metrics: lag (cross-correlation of the
  mono sums, ±4096 samples), peak / RMS level difference, error-to-signal ratio (ESR) and ESR after the best single
  gain, the worst 1-second window, % of 50 ms windows within −40 / −60 dB, low / mid / high band levels, and two
  phase-blind measures for whole songs: the correlation of the 50 ms level envelopes (`env_corr`) and the mean
  third-octave long-term spectrum distance (`spec_db`). With any phase drift a whole song's ESR is ~0 dB even when it
  sounds the same, so these two say more for the demo songs.

## 2. Scoreboard (v3, 2026-10-16)

165 projects (Demo projects, System/Render tests, Templates); 133 compared, 31 silent in both, 1 unreadable.

| ESR bucket | projects |
|---|---|
| <= −60 dB (near exact) | 26 |
| −60 … −40 dB | 2 |
| −40 … −20 dB | 9 |
| −20 … −6 dB | 19 |
| > −6 dB | 65 |
| silent in one only | 12 |

[EXEC] Near exact (FL's int16 quantisation allows about −80 dB): the render tests of 3x Osc, BassDrum, DirectWave,
Drumsynth Live, Fruit Kick, DX10, and the effects Blood Overdrive, Convolver, Delay 2, Delay Bank, Fast Dist, Flangus,
Limiter, Multiband Compressor, Parametric EQ / EQ 2, Reeverb 2, Send, Soft Clipper, Squeeze, Vocoder, Waveshaper,
Maximus, Patcher (effect), Soundgoodizer, Transient Processor.

[EXEC] Demo songs / product demos (39 audible): median RMS difference −1.6 dB (LYNX quieter); 9 more than 3 dB quieter,
2 more than 3 dB louder. Most "> −6 dB" entries are whole songs where phase drift (sampler / synth start phase,
tempo-synced effects) makes the ESR meaningless; `env_corr` / `spec_db` are the numbers to follow there.
Performance-mode projects and templates render silent in FL's command line (no playlist content) — "silent: fl".

## 3. Fixes made from the scoreboard

### 3.1 Channel Rack switch
[DATA] An `.flp` channel's 0x00 byte event is its Channel Rack on/off switch (0 = off). FL does not play a switched-off
channel's notes; LYNX imported them as playing. Now imported as a muted channel (e.g. "Synth_dfr - Overcome - Sytrus"
has two).

### 3.2 FL 11 mixer slots, renderwav
[DATA] Files saved by FL 11 and older close an effect slot without the 0x62 slot-index event; the slot index is the
second i32 of the 0xD4 plugin record, and the next slot / route / insert event ends the slot. Before, such slots were
dropped (e.g. "Fruity Phase Inverter" render test, "nucleon - Steptime" insert 3 with 4 effects).
`tools/renderwav` reads Ogg samples and renders the whole song with the app's 1-second empty tail, so a song does not
wrap back to its start inside the tail (FL's exports don't).

### 3.3 3x Osc in FL's HQ render
[DECOMP] In the host's "HQ render" mode (on for FL's exports and command-line renders), 3x Osc renders every
oscillator that is neither noise nor the sine table as k = max(phase increment >> 22, 1) copies of itself, each at
gain g / k, copy j starting at the oscillator's phase + j · (increment / k) — a box filter against aliasing at high
pitches. The sine is not oversampled. LYNX's 3x Osc does the same while LYNX renders offline (exports), keeping the
live sound as before. Tests: `three_osc_host_hq_render_box_filters_aliasing_shapes`,
`flp_fl11_mixer_slots_and_disabled_channels`.

## Where I stopped / next steps

- The run was cut by a power loss while the envelope / spectrum metrics were being added; those metrics are in the
  script but the v3 scoreboard above predates them (re-run `batch` to fill `env_corr` / `spec_db`).
- Next: use `culprit` on the worst render tests (Low Lifter, Free Filter, Frequency Shifter, Fruity Center, Slicer)
  and on the quieter demo songs (overall level is one of the user's reports: "much quieter than in FL").
- Render tests silent in LYNX only: Granulizer and Scratcher render tests (not confirmed why for Granulizer; Scratcher
  needs FL's own Vorbis decoder, round 71).
