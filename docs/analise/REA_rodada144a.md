# REA round 144a (parity144a): UberMod DelaySync and Pro-Q 4 linear-phase resolution

2026-10-24. Sub-round of parity144 (`docs/PARITY.md`, REA_rodada137 "Where I stopped"). Evidence markers: [EXEC]
measured here on the official demo / trial, hosted black-box in `tools/fxmeasure` (public VST3 API only: parameters,
value texts, audio in / out, the component state a DAW saves); [model] LYNX's own choice. Nothing was decompiled; no
graphics, presets or names were copied. Raw files: `D:\lynx-work\parity144\a\` (`s1` / `s2` = UberMod impulses,
`pq1` = Pro-Q 4 states).

## 1. Lattice Mod: ValhallaUberMod's DelaySync [EXEC]

The demo's DelaySync value text is just 17 v ("0.00" … "17.00", continuous), so the steps were read from the audio:
an impulse at 20 ms, one tap (`type` 0), Delay 500 ms, Depth 0, Feedback 0, the value at (k + 0.5) / 17 and at
k / 17 ± 0.002, transport 120 BPM (and 240 BPM for the long ones). Tap time after the impulse:

| Index (floor 17 v) | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 120 BPM (ms) | 500 | 500 | 126 | 250 | 500 | 1001 | 2000 | 2750 | 188 | 376 | 751 | 1500 | 2750 | 84 | 167 | 334 | 667 | 1334 |
| 240 BPM (ms) | 500 | 500 | | | | | 1001 | 2000 | | | | | 1501 | | | | 667 | |
| Note | free | free | 1/16 | 1/8 | 1/4 | 1/2 | 1/1 | 2/1 | 1/16. | 1/8. | 1/4. | 1/2. | 1/1. | 1/16T | 1/8T | 1/4T | 1/2T | 1/1T |

The boundaries are k / 17 with a floor (3/17 − 0.002 still gives 1/16, 3/17 + 0.002 gives 1/8; the same at 2, 8, 13
and 17). Indices 0 and 1 are both the free delay (round 137's "two shortest notes" do not exist). The synced delay is
capped at 2750 ms (2/1 and 1/1. at 120 BPM; at 240 BPM they are 2000 / 1500 ms).

LYNX: a new appended parameter `lattice::delaySync` ("Delay sync") with exactly that law (`delaySyncIndex`,
`delaySyncBeats`, `syncedDelayMs`, cap 2750 ms); the menu next to Mode lists Free, 1/16 … 2/1, 1/16. … 1/1.,
1/16T … 1/1T. The older LYNX list (by length, 1/64 … 1/1) keeps its parameter for projects saved before: it is shown
in the same place only while it is set, and wins only while the new one is Free. `map = xml:DelaySync -> Delay sync`
(same value law) in `[valhalla-ubermod]`.

## 2. Facet EQ: Pro-Q 4's linear-phase resolution [EXEC]

Round 137 read the trial's latencies at 48 kHz: Low 3072, Medium 5120 (default), High 9216, Very High 17408, Maximum
66560 = 1024 + 2^11, 2^12, 2^13, 2^14, 2^16 (no 2^15). This round also read the component state after setting the
parameters: value 552 = processing mode index, **553 = resolution index (0 Low … 4 Maximum)**, **554 = character
index (0 Clean, 1 Subtle, 2 Warm)** (her states hold 1 / 0, i.e. Medium / Clean). With Character Subtle / Warm the
trial reports 26 more samples (Facet's character adds none).

LYNX: parameter 89 "Resolution" (appended; default Medium, value = the trial's normalised value r / 4). The FIR has
2 h taps, h = 2048 … 65536 (× 2 / × 4 at 96 / 192 kHz), run in blocks of 1024 (was 512): latency = 1024 + h, the
trial's numbers. Medium is the old 8192-tap FIR (the same response; the latency goes from 4608 to 5120, compensated by
PDC). The convolver, the design FFT, the spectrum and the delay buffers are sized in `prepare()` for the instance's
resolution only (Maximum ≈ 16 MB at 48 kHz); choosing another resolution in the editor builds a new instance
(`RackRenderer` does not reuse a Facet whose resolution changed); automation of it waits for the next prepare. FIRs
longer than 8192 taps read the magnitude from a 4096-point log-spaced grid (interpolated) instead of every bin, and
are redesigned at most every taps / 16 samples [model]. Editor: a "Linear phase resolution" section in the Processing
menu (each with its latency) and the resolution in the bottom bar. Pro-Q 4 states: 553 and 554 translated. Fixed on
the way: the bottom bar's separator was a double-encoded "Â·".

Test (tests/Parity144aTests.inc): the five latencies, a 40 Hz +12 dB Q 4 bell: Maximum within 0.3 dB of the analog
gain and closer than Low.

## Where I stopped

- Done: DelaySync law + translation; linear-phase resolution + translation (+ character).
- Not done: changing the resolution by automation re-sizes only at the next prepare; Pro-Q 4's 24 bands, spectral
  dynamics, EQ match; UberMod's per-mode LFO designs and 16-tap polarity.
