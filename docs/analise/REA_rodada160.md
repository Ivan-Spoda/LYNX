# Round 160 (quality160): sound quality and performance of LYNX's signal path

2026-10-25. Context: a real user (Windows laptop, SSL 2 MKII at 48 kHz, v0.1.15) feared that LYNX's sound is
"generally bad". The worst cause was JUCE's MP3 decoder corrupting files (fixed in round 157b with minimp3). This round
checks the rest of the signal path with measurements, and fixes what the measurements show.

The first agent of this round died in a PC crash (BSOD) with all its work uncommitted and no round doc; its
measurement scripts were lost (only the A/B renders on D: survived). This document was reconstructed from the diff
(commit ab89628b, "WIP salvaged after crash") and the numbers below were **re-measured** after the crash.

Evidence markers: [MEAS] measured here (script/data named); [TEST] proven by a test in tests/Quality160Tests.inc
(build-local); [CODE] read in LYNX's source; [FL] rendered by FL Studio 2026 (files on D:).

Data: `D:\lynx-work\quality160\ab` (originals, LYNX old/new, FL Stretch Pro / élastique E3 generic / E3 mono renders
of a vocal and a drum loop at 0.8 / 1.25 / 1.5), `D:\lynx-work\quality160\fl` (FL renders of synthetic signals; the
synthetic inputs were lost with the crash).

## 1. Summary

| # | Problem found | Before | After | Status |
|---|---|---|---|---|
| 2 | Time-stretch (stretched samples, FL-imported "Stretch" channels, Newtone) was a basic WSOLA | drum onsets off by 20–210 ms (median); recorded takes (ratio 0.99998) rebuilt from windows: null +2.9 dB vs the ideal | Signalsmith Stretch (MIT) + LYNX attack splicing: onsets 0.8–2.9 ms (FL 0.8–4.0); takes resampled: null −77 dB | done |
| 3 | Master "soft clip" bent everything above −0.9 dBFS | THD 0.29 % on a −0.3 dBFS sine, 0.65 % at −0.1 dBFS, peaks −0.07…−0.16 dB | bit-exact up to 0 dBFS, overs clamped (NaN → 0) | done |
| 4 | Browser / editor preview read samples with linear interpolation | 44.1 kHz file on a 48 kHz device: 10 kHz −1.5 dB, image at 13.9 kHz −21 dBc | FL's 24-point sinc (same as the sampler) | done |
| 4 | Exports used the playback interpolation (24-pt) in most cases | quality chosen in prepare(), before the export switched to offline | render quality (32-pt) read per note | done |
| 5 | 16/24-bit export: dithered samples above −6 dBFS written 1 LSB low | every positive sample above half scale off by one LSB (asymmetric error) | quarter-LSB writer bias: every grid value written exactly | done |
| 6 | Denormals: FTZ/DAZ only around the rack | metronome, preview, plugin graph nodes, sandbox thread unprotected | whole audio callback + sandbox thread | done |
| 6 | `std::stable_sort` of note events on the audio thread allocates | heap allocation per block with events | in-place insertion sort | done |
| 7 | Dropouts on Windows shared mode, no hint | — | perf meter hint suggests the interface's own ASIO driver | done |

## 2. Time-stretch

[CODE] Before: `src/core/TimeStretch.cpp` was a plain WSOLA (46 ms Hann windows, 50 % overlap, ±12 ms similarity
search, mono similarity). Used by: sampler channels with "Stretch keeping pitch" (incl. every FL channel imported with
a stretch mode other than Resample — FL's recorded takes carry a stretch time equal to their own length, so ratio
≈ 0.99998), Newtone's warp rendering, and (via SampleLibrary::loadStretched) the playlist's tempo-following samples.

Now (`src/core/TimeStretch.*`):
- ratios within 0.1 % of 1 → Kaiser-windowed sinc resample (64 taps, β = 9) to the exact length (≤ 1.7 cents of
  pitch, no stretch artefacts);
- otherwise Signalsmith Stretch (third_party/signalsmith, MIT, unmodified; README there) with presets per method, and
  for every method LYNX's own attack splicing: detected attacks are copied back unstretched with
  short crossfades at the best-correlating point (no smeared/doubled hits, no pre-echo);
- three methods, chosen from FL's stretch mode on import (`stretchMethodForFlMode`) and in Channel settings (new combo
  "General / Drums / transients / Vocal / mono", saved in the project as `stretchMethod`, pt-BR strings):

| Method | Block / interval | Attack splice (rise, hold) | FL modes mapped |
|---|---|---|---|
| general | 120 ms / 20 ms | 9 dB, 20 ms | Stretch Pro, Stretch, E3 generic, Auto, E2 generic |
| transients | 60 ms / 10 ms | 6 dB, 30 ms | slice stretch, slice map, E2 transient |
| mono | 60 ms / 10 ms | 9 dB, 20 ms | E3 mono, E2 mono, E2 speech; Newtone |

(An overlap of 6 instead of Signalsmith's default 4 was chosen by the first agent: "lowers spectral error on chords and
voices by about a third and tonal noise by 20…40 dB" — that sweep was lost; the tonal-purity part is re-proven by
[TEST] q160_stretch_tonal_purity.)

### 2.1 Measurements vs FL Studio [MEAS] scratchpad `stretchmeas.py` on `D:\lynx-work\quality160\ab`

Log-spectral distance (dB) between the output and the original's spectrum at the mapped time (lower = closer to an
ideal stretch; 2048-pt STFT, 60 dB activity gate):

| Signal, ratio | LYNX old | LYNX new general | new drums | new mono | FL Stretch Pro | FL E3 generic | FL E3 mono |
|---|---|---|---|---|---|---|---|
| vocal 0.8 | 4.11 | **3.73** | 3.80 | 3.79 | 4.40 | 4.29 | 4.50 |
| vocal 1.25 | 4.25 | **3.63** | 3.68 | 3.69 | 3.72 | 3.70 | 4.20 |
| vocal 1.5 | 4.40 | **4.01** | 4.25 | 4.29 | 4.51 | 4.59 | 5.09 |
| drum loop 0.8 | 2.59 | 2.61 | 2.65 | 2.72 | 3.12 | 3.04 | 3.23 |
| drum loop 1.25 | 2.67 | **2.53** | 2.60 | 2.65 | 2.67 | 2.69 | 3.17 |
| drum loop 1.5 | 2.75 | 2.92 | 3.05 | 3.10 | 3.35 | 3.47 | 4.04 |

Drum-loop onset timing (median error of each original attack's mapped position, ms):

| ratio | LYNX old | LYNX new general | FL Stretch Pro | FL E3 generic |
|---|---|---|---|---|
| 0.8 | 209.5 | 0.8 | 1.2 | 0.9 |
| 1.25 | 19.8 | 2.0 | 0.8 | 1.3 |
| 1.5 | 26.5 | 2.9 | 2.4 | 4.0 |

All outputs have the exact length (ratio × input). So: the old WSOLA's spectrum was not far off but it **moved the
hits** (the "drums out of time / flamming" sound); the new stretcher keeps them where FL does, and its spectrum is at
least as close to the ideal as FL's in every case.

Near-unity (her recorded vocal take, ratio 0.99998), null against an ideal band-limited resample:
LYNX old **+2.9 dB** (i.e. completely different waveform: windows re-assembled), LYNX new **−77.0 dB**, FL Stretch
Pro +1.4 dB (FL really stretches it). [MEAS]

### 2.2 Tests [TEST]
- q160_stretch_near_unity_is_a_clean_resample: 1 kHz sine at 0.99998, error vs ideal < −80 dB; ratio 1 = copy.
- q160_stretch_keeps_clicks_sharp_and_on_time: click train × 0.8/1.5/2.0, general and drums: 10/10 clicks within
  2 samples, peak > 0.7, pre-echo < −30 dB.
- q160_stretch_tonal_purity: 1 kHz × 0.8/1.25 SINAD > 75 dB (general, mono), > 45 dB (drums).
- q160_stretch_short_sounds_and_mapping: 40 ms one-shot stretches to the right length; FL mode mapping; attack
  detection exact on clicks, none on a steady tone.

## 3. Master output protection [MEAS][TEST]

[CODE] Before: `softClip` on the device output and on every export: linear up to 0.9, tanh knee above.
[MEAS] (numpy model of the old curve, 1 kHz sine): −0.1 dBFS → THD 0.646 %, peak −0.156 dB; −0.3 dBFS → 0.291 %,
−0.074 dB; −0.5 dBFS → 0.090 %; −0.9 dBFS and below untouched. Any mastered/limited mix (peaks −0.1…−0.3 dBFS)
was distorted by LYNX itself. FL leaves the mix untouched up to full scale.

Now `clipOvers`: |x| ≤ 1 passes bit-exact, overs clamp to ±1, NaN → 0. The 32-bit float WAV export and the buffered
(normalising) export keep overs (`RenderOptions::keepOvers`), like FL. [TEST] output_protection_is_transparent_up_to_full_scale,
q160_master_protection_distortion_before_and_after (old curve changes a −0.3 dBFS sine by more than −60 dB; new
= identical).

## 4. Interpolation

- Preview player (Browser, audio editor preview): linear → FL's sampler kernel (`flrs`, 24-point sinc,
  liveQuality), with clamped/wrapped taps at the edges and around the loop point. [MEAS] linear read, 10 kHz tone of
  a 44.1 kHz file on a 48 kHz device: −1.49 dB, image at 13.9 kHz −21.3 dBc. [TEST]
  q160_preview_interpolation_44k1_file_on_48k_device (sinc SINAD > 60 dB, linear < 30 dB; actual numbers printed in
  the test log).
- Sampler during export: the export engine prepares instruments before switching to offline rendering, so the
  quality chosen in prepare() stayed at playback quality; now read per note (`offlineRendering() ? renderQuality :
  quality`). [TEST] q160_sampler_uses_render_quality_when_export_starts_after_prepare.

## 5. Dither / integer writers [CODE][TEST]

JUCE's writers convert float → int32 with round((2³¹−1)·x) and then drop the low bits (truncation). A value on the
k/2^(bits−1) grid lands just below k·2^(32−bits) once k passes half scale, and truncation writes k−1: every positive
dithered sample above −6 dBFS was written 1 LSB low. `Dither` now adds a quarter LSB (`writerBias()`), so truncation
gives exactly k for any k. [TEST] q160_dither_survives_the_writer_conversion (16 and 24 bit, 20 000 samples at
0.999 FS: 0 wrong), existing dither tests adjusted.

## 6. Real-time safety

- `ScopedFlushDenormals` for the whole device callback (was only around the rack: metronome, preview player, hosted
  plugin graph nodes ran without FTZ/DAZ) and for the plugin sandbox's audio thread (had none). [CODE]
- `EventBuffer::sortByOffset`: `std::stable_sort` allocates a temporary buffer — on the audio thread, every block
  with events. Now an in-place stable insertion sort (events arrive nearly sorted). [TEST]
  q160_event_sort_is_stable_and_allocation_free.

## 7. WASAPI dropouts

After over-deadline/late callbacks on Windows Audio (shared / low-latency) or DirectSound, the performance meter's
hint suggests the interface's own ASIO driver (e.g. "SSL ASIO Driver 1"), then LYNX ASIO, wrappers last
(`prefs::suggestedAsioDriver`, pt-BR string). [TEST] q160_asio_suggestion.

## 8. Not done yet (rest of the audit)

- Plugin sample-rate conversion: 44.1 kHz files and plugins on a 48 kHz device (where LYNX resamples, which kernel).
- Pan law vs FL, mixer summing.
- CPU timings of the new stretcher (load time of a project with many stretched channels) and of the preview sinc.
- A systematic scan of the audio thread for allocations / locks (beyond the event sort).
- Audio editor time-stretch path, playlist tempo-following clips: same stretcher, not separately measured.

## Where I stopped

- 2026-10-25 (resumed after the BSOD): WIP committed as ab89628b and pushed to origin/wip/quality160; event-sort test
  fixed (used private members), preview-interpolation test added, PreviewPlayer comment corrected (1.5 dB, not 2.1).
  Numbers in §2.1, §3, §4 re-measured. Next: build-local green → commit → Windows build → land batch 1; then §8.
