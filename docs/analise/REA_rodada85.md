# REA round 85: FL pattern events (.flp 0xDF / 0xD8) → LYNX pattern event lanes

Round 83 gave LYNX pattern event lanes (the event editor) but did not import FL's own pattern events. This round reads
them from `.flp` files, maps their raw values to LYNX targets, and matches when and how FL applies them.

## 1. File format [DATA]

- Event 0xDF (data), inside a pattern's section (after 0x41): the pattern's events, 12-byte records
  `{u32 tick (file PPQ), u32 controlID, i32 value}` in file order. The controlID is the same address as automation
  links and stored values (0xE3 / 0xE1): channel `(channel << 16) | param`, mixer
  `0x70000000 + (track << 22) + (slot << 16) + param` (old files: class 0x2…/0x3…, normalised like the others).
- Event 0xD8 (data), project level, same records: "initialized controls" — the values FL resets the targets to when
  the song starts, before any pattern event (the tick field is 0 in every file seen).
- Survey of the installation's projects: 437 (pattern, target) pairs in the demo songs and performance projects
  (channel volume / pan, mixer volume / pan / slot mix, plugin parameters).
- A pattern can hold events and no notes; it still plays them (such patterns were dropped before).

## 2. Value scales [DATA] [EXEC]

The value is the parameter's raw integer as FL's parameter setter stores it; (raw − lo) / (hi − lo) is FL's normalised
value, the one FL's automation clips carry:

| target | lo … hi |
|---|---|
| channel volume, pan (params 0, 1) | 0 … 12800 (volume default 10000, pan centre 6400) |
| mixer volume (0x1FC0) | 0 … 16000 |
| mixer pan (0x1FC1) | −6400 … 6400 |
| slot dry/wet (0x1F01) | 0 … 12800 |
| plugin parameter (0x8000 + k) | the plugin's own control range: done for Reeverb 2, Reeverb, Convolver, LuxeVerb |

Other targets or plugins with unknown ranges are left out and counted in the import report ("Pattern events: value
scale not known (…)" / "target not reproduced"). Several events on one tick: the last one in the file wins (FL applies
them in order). Two lanes on one target in a pattern: the later wins.

## 3. Timing and the mixer's glide [EXEC]

Measured with FL's command-line renders of synthetic projects (a 441 Hz sine sample, one long note, events on the
channel volume / pan and the Master volume / pan; made by a script from the installation's "Sampler" render test, kept
in tmp/r85, not in the repo):

- An event takes effect on its own sample (the sample of its tick), not at the next audio block. LYNX now splits its
  render chunk where an event (or the edge of a pattern clip / the pattern loop) falls.
- A mixer track's fader and pan do not jump: each side's gain moves toward its new target by at most 0.001 per sample
  (a linear glide, 1000 samples for a full 0 → 1 change), measured on FL's renders of events on a track's volume and
  pan. LYNX's mixer gain now glides the same way (before: a linear ramp over one 128/512-sample chunk). This applies to
  every change of a track's gain, so automation clips and knob moves glide the same way.
- Channel volume / pan events go through the channel's existing smoothing (not changed in this round).

## 4. Tests
`flp_pattern_events_import` (synthetic `.flp`: 0xDF records on Master volume and channel pan, a duplicate tick, an
unknown plugin parameter, 0xD8 Master pan); `event_editor_playback` (round 83) still passes with the per-sample
timing.

## Where I stopped / next steps

- The run was cut by a power loss right after the timing / glide change; the coordinator committed it, added the
  test and this doc from the round's notes and logs.
- Not done: value ranges of the other plugins' parameters (each port knows its own ranges; add them to
  `flRawRange`), tempo events, the channel-level smoothing compared with FL's.
- Not confirmed: whether FL's 0.001-per-sample glide also applies to slot dry/wet and EQ (only volume and pan were
  measured).
