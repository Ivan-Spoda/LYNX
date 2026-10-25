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
| 2 | Time-stretch (stretched samples, FL-imported "Stretch" channels, Newtone) was a basic WSOLA | drum onsets off by 20–210 ms (median); recorded takes (ratio 0.99998) rebuilt from windows: null +2.9 dB vs the ideal | Signalsmith Stretch (MIT) + LYNX attack splicing: onsets 0.6–1.5 ms median, ≤ 2.2 ms worst (FL 0.8–4.0 / up to 400); takes resampled: null −77 dB | done |
| 3 | Master "soft clip" bent everything above −0.9 dBFS | THD 0.29 % on a −0.3 dBFS sine, 0.65 % at −0.1 dBFS, peaks −0.07…−0.16 dB | bit-exact up to 0 dBFS, overs clamped (NaN → 0) | done |
| 4 | Browser / editor preview read samples with linear interpolation | 44.1 kHz file on a 48 kHz device: 10 kHz −1.5 dB, image at 13.9 kHz −21 dBc | FL's 24-point sinc (same as the sampler) | done |
| 4 | Exports used the playback interpolation (24-pt) in most cases | quality chosen in prepare(), before the export switched to offline | render quality (32-pt) read per note | done |
| 5 | 16/24-bit export: dithered samples above −6 dBFS written 1 LSB low | 33 % of a full-scale sine's samples off by one LSB | half-LSB writer bias: 0 wrong | done |
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

### 2.1 Measurements vs FL Studio [MEAS] `D:\lynx-work\quality160\tools\stretchmeas.py` on `D:\lynx-work\quality160\ab`

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

Drum-loop onset timing (error of each original attack's mapped position, ms, median / worst). "WIP" = the code as
salvaged from the crash; "final" = with the 15 ms pre-roll added after the crash (§2.2):

| ratio | LYNX old | LYNX WIP general | **LYNX final general** | FL Stretch Pro | FL E3 generic | FL E3 mono |
|---|---|---|---|---|---|---|
| 0.8 | 209.5 / 602 | 0.8 / 400 | **0.6 / 0.9** | 1.2 / 4.3 | 0.9 / 401 | 1.0 / 400 |
| 1.25 | 19.8 / 605 | 2.0 / 627 | **0.9 / 1.0** | 0.8 / 2.7 | 1.3 / 3.1 | 0.8 / 2.6 |
| 1.5 | 26.5 / 778 | 2.9 / 752 | **1.5 / 2.2** | 2.4 / 377 | 4.0 / 378 | 1.8 / 2.2 |

(A "worst" of several hundred ms = one hit not found at all near its place: smeared or doubled.) The final version's
spectral distance stays within ±0.03 dB of the WIP values in the table above.

All outputs have the exact length (ratio × input). So: the old WSOLA's spectrum was not far off but it **moved the
hits** (the "drums out of time / flamming" sound); the new stretcher keeps them where FL does, and its spectrum is at
least as close to the ideal as FL's in every case.

Near-unity (her recorded vocal take, ratio 0.99998), null against an ideal band-limited resample:
LYNX old **+2.9 dB** (i.e. completely different waveform: windows re-assembled), LYNX new **−77.0 dB**, FL Stretch
Pro +1.4 dB (FL really stretches it). [MEAS]

### 2.2 Pre-echo before attacks (fixed after the crash) [MEAS][TEST]

The salvaged code failed its own test q160_stretch_keeps_clicks_sharp_and_on_time: the spliced original started only
4 ms before each attack, but the spectral stretcher smears an attack backwards over up to half its analysis block.
Energy 1.5–30 ms before each click relative to the click (click train; general / drums):

| ratio | 4 ms pre-roll (WIP) | 15 ms pre-roll (final) |
|---|---|---|
| 0.8 | −37 / −34 dB | −47 / −51 dB |
| 1.5 | −29 / −30 dB | −37 / −41 dB |
| 2.0 | −33 / −29 dB | −39 / −38 dB |

The pre-roll never reaches back past the previous splice (attacks are ≥ 50 ms apart).

### 2.3 Cost [MEAS]

Offline, once per (file, ratio, method), in parallel on project load. On this PC (busy with other builds): the 4.9 s
stereo vocal takes 0.57–0.63 s to stretch (≈ 0.12 × real time). The near-unity resample was rewritten after the crash
as a polyphase table with two vectorised dot products per frame: 2 s of stereo in 13–25 ms instead of 46–84 ms (same
error: −116 / −107 / −94 dB vs the ideal at 1 / 10 / 18 kHz), i.e. about a second for a 3-minute take.

### 2.4 Tests [TEST]
- q160_stretch_near_unity_is_a_clean_resample: 1 kHz sine at 0.99998, error vs ideal < −80 dB; ratio 1 = copy.
- q160_stretch_keeps_clicks_sharp_and_on_time: click train × 0.8/1.5/2.0, general and drums: 10/10 clicks within
  2 samples, peak > 0.7, pre-echo < −35 dB.
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
dithered sample above −6 dBFS was written 1 LSB low. [MEAS] D:\lynx-work\quality160\tools\d0.cpp, d.cpp (the exact JUCE conversion,
200 000 samples of a 0.9999 FS sine): 66 743 samples (33 %) wrong at 16 and at 24 bits before; 0 after. `Dither` now
adds half an LSB (`writerBias()`), so truncation gives exactly k for any k. (The salvaged WIP used a quarter LSB,
which at 24 bits is below float resolution near full scale and was rounded away — its own test caught it.) [TEST] q160_dither_survives_the_writer_conversion (16 and 24 bit, 20 000 samples at
0.999 FS: 0 wrong), existing dither tests adjusted.

## 6. Real-time safety

- `ScopedFlushDenormals` for the whole device callback (was only around the rack: metronome, preview player, hosted
  plugin graph nodes ran without FTZ/DAZ) and for the plugin sandbox's audio thread (had none). [CODE]
- `EventBuffer::sortByOffset`: `std::stable_sort` allocates a temporary buffer — on the audio thread, every block
  with events. Now an in-place stable insertion sort (events arrive nearly sorted). [TEST]
  q160_event_sort_is_stable_and_allocation_free.
- Audit of every built-in unit [TEST] q160_audio_thread_allocations_and_cost_of_builtin_units: all 95 effects and
  38 instruments (all kinds but plugin, Slayer, MIDI Out, audio clip), prepared at 48 kHz, 100 blocks of 512 frames
  (instruments: two notes retriggered every 10 blocks) with a global `operator new` counter: **0 of 133 units
  allocate on the audio thread**. Costliest per 512-frame block (on this PC while three other builds were running, so
  upper bounds): sampler with ~6 overlapping voices 542 µs (5.1 % of the 10.7 ms budget; FL's 24-point sinc per voice,
  as FL), Vintage Phaser 498 µs, Prism Multiband 369 µs, LuxeVerb 245 µs, Quell 236 µs, Ember 223 µs; everything else
  ≤ 2.1 %. Nothing there explains dropouts at her ~1–8 % average load.

## 7. WASAPI dropouts

[LOG] Her audio-glitches.log (tmp/gf/logs157): always "Windows Audio" (shared WASAPI) at 48 kHz / 512 frames —
SSL 2 MKII: 6 driver dropouts; Realtek speakers: 11, 15, 30 — while LYNX's own callbacks were **never over deadline**
(over deadline 0 in every session; average load 1–8.5 %, one 97.5 % spike in three long sessions). So her dropouts
come from shared-mode WASAPI scheduling, not from LYNX's CPU use; the interface's ASIO driver avoids that path.

After over-deadline/late callbacks on Windows Audio (shared / low-latency) or DirectSound, the performance meter's
hint suggests the interface's own ASIO driver (e.g. "SSL ASIO Driver 1"), then LYNX ASIO, wrappers last
(`prefs::suggestedAsioDriver`, pt-BR string). [TEST] q160_asio_suggestion.

## 8. Not done yet (rest of the audit)

- Locks on the audio thread (the allocation scan above covers built-in units only; the engine/graph path around them,
  the JUCE side, is not covered by build-local tests).
- Plugin sample-rate conversion: none exists — [CODE] hosted plugins are prepared at the device rate
  (src/engine/PluginHost.cpp, ClapFormat, Vst2Format) and sample files of another rate are read with FL's sinc
  (sampler, audio clips) or the audio editor's Blackman-windowed sinc (16 zero crossings), so a 44.1 kHz file on her
  48 kHz SSL goes through a proper band-limited conversion everywhere (the preview player was the one exception, §4).
- Pan law: already matched to FL per instrument/mixer in earlier rounds (REA_rodada27 §3.1, FL's host pan laws in
  core/FlVoiceProcessor.h); not re-measured here.
- Audio editor time-stretch path, playlist tempo-following clips: same stretcher, not separately measured.

## Where I stopped

- 2026-10-25 (resumed after the BSOD): WIP committed as ab89628b and pushed to origin/wip/quality160. Fixed what the
  salvaged code's own tests caught (event-sort test used private members; stretch pre-echo −29 dB → 15 ms pre-roll;
  dither bias a quarter LSB → half), regenerated I18nData.h, added the preview-interpolation and RT-audit tests,
  rewrote the near-unity resampler (polyphase), merged origin/main (361c6903). Numbers in §2–§7 re-measured.
- Build-local of the merge: in progress; then a final build-local + Windows build of b9db574c+ and land batch 1
  (`land.sh quality160`). Then the "Sound quality" section of notes-0.1.16.md.
- Next (§8): locks on the engine side of the audio thread; anything else measured goes in a batch 2.
