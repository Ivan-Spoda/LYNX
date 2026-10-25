# Round 157c (tune157): "the Auto-Tune is horrible" — a singer's v0.1.15 projects

2026-10-25. Report: a singer (the user's girlfriend) found Auto-Tune in LYNX v0.1.15 "horrible": "it MUFFLES the
voice, it seems to have no function (no pitch correction)". Her laptop logs show she hosts the **real** Auto-Tune Pro
in LYNX, so the hosting path came first; LYNX Tune (LYNX's own corrector, used when Auto-Tune is missing) second.

Evidence markers: [CODE] read in LYNX's source (file named); [TEST] proven by `tests/Tune157Tests.inc`; [RUN]
measured with LYNX builds (Windows build on this PC, or her own install over ssh, read-only, black-box);
[REF] rendered through the official Auto-Tune Pro trial (black-box hosting with our own VST3 host, her saved
states; nothing decompiled, patched or copied); [FL] FL Studio observed as a user (CLI render of her projects);
[DATA] her project / recording files (copied to `D:\lynx-work\gf157`); [model] LYNX's own design choice.

## 1. Hosting the real Auto-Tune Pro: it works

- [DATA] All 139 Auto-Tune slots in her projects are the same plugin: VST3 "Auto-Tune Pro", class id
  `415453563854413175746F2D54756E65`, in `Common Files\VST3\Antares\Auto-Tune Pro.vst3`. LYNX resolves it by that
  class id (not by name), so the newer `AutoTune.vst3` (a different product, different class id) on her laptop is
  never used for these slots; it scans fine on this PC (`--scan-one`), its empty scan entry on her laptop does not
  affect her projects.
- [RUN] This PC, v0.1.15 and main, her FLPs and her recovered `.lynx`: insert export with the real plugin chain
  tunes (median distance from the scale 21 → 4.3 cents), band levels within ±0.5 dB of FL's render of the same
  insert: not muffled.
- [RUN] Real-time path: new `--rtstress <project> <report> <seconds> --log <folder>` (logs the master output while the
  device plays, `src/Main.cpp`) at 48 kHz / 512 matched the offline export (same pitch statistics and bands).
- [RUN] Her laptop (her own install, Auto-Tune Pro 10.3.1), exporting her recovered project's Insert 1 with her
  plugins: Auto-Tune corrects (20 → 4.9 cents), not muffled; Auto-Tune alone on the insert also tunes.
- Conclusion: state restore, sample rate, bus layout and latency compensation of Auto-Tune Pro are fine in v0.1.15.

## 2. What she most likely heard: her dry microphone on the Master

- [DATA] Her new projects of the night (`untitled` 00h14 / 00h16): the **Master** has `"input": 0` (device inputs
  1-2) and Insert 1 (Auto-Tune, Waves, reverb) has `"input": 0, "inputMono": true` with the default "automatic"
  monitoring. [CODE] The Master plays its input straight away (`AudioEngine`, monitoring is ignored only when "off"),
  without any insert's effects; Insert 1 is heard only while armed. So while she sang she heard the dry mic from the
  Master over (or instead of) the processed voice: "no pitch correction", the effects "do nothing", and the dry
  close-mic sound reads as "muffled" next to the bright processed chain she is used to in FL.
- [FL] Her FL projects never route an input to the Master (FLP insert input event 0x9A is −1 on the Master).
- Fix [model]: choosing an audio input for a track (`mixerinput::showMenu`, `src/ui/MixerComponent.cpp`) now checks
  whether the Master plays the same device channel as an insert; if so it says so ("The same input plays twice") and
  offers **Take it off the Master** (pt-BR strings in `resources/i18n/pt-BR.txt`).
- Also seen on her laptop: ASIO output "SSL ASIO Driver 1" with input "ASIO4ALL v2" (two drivers, 44.1 kHz) in her
  `audio-device.xml`; opening Auto-Tune's editor crashed LYNX (OpenGL `SetPixelFormat`) — round 157a's area.
- [RUN] Her Waves RComp / DeEsser (WaveShell 15.5) are not installed on her laptop: LYNX substitutes its Compressor +
  Hush de-esser (−1.5 … −2.3 dB at 1–4 kHz vs passing them through, as FL does with a missing plugin). A possible
  small "duller" contribution; left as is (the substitute is what the project asks for), noted for the user.

## 3. LYNX Tune vs the trial on her takes

References [REF]: 90 fresh takes of 2026-10-24 (her FL session, each through the Auto-Tune state of its insert),
plus 129 phrases from four older projects (TWIN SWAG, kwe, game over, lu), all in `D:\lynx-work\tune157\ref`.
LYNX renders [RUN] with the same states translated by `PluginSubstitutes` (v0.1.15 release, main, this round).

- Pitch: LYNX Tune is as tight as the trial (median 5.1–5.4 cents from the scale vs the trial's 7.4), bands within ±0.5 dB,
  harmonics-to-noise 32.5 dB vs 33.1 (dry 33.9). Not the problem.
- Artifacts: the audible difference is rough bursts / clicks inside held notes. Counted as spectral-flux bursts that
  are not in the dry take (all 219 takes, 22.7 minutes of singing): trial 29.8 / min, v0.1.15 45.5 / min.
- [RUN] Cause, from a trace of the detector on the worst takes: **harmonic errors**. On her strong ~870 Hz formant
  YIN's first dip under the threshold is a harmonic's, so the detector read 1.5×, 2× or 3× f0 for a few estimates and
  the PSOLA grains shrank to a fraction of a period (the vertical streaks in v0.1.15's spectrograms, e.g.
  `ab\lu_i9_c13\spectrogram.png`). Fix (`src/core/VocalPitch.cpp`, `PitchDetector::setHarmonicGuard`, on in LYNX
  Tune only): a dip at m·τ (m = 1.5, 2, 3, 4) that is clearly deeper (< 0.6× and at least 0.03 below the first dip)
  wins when it continues the last voiced period, or, at a phrase's start, when it is much deeper (< 0.35×).
- Result per project (bursts / min, trial · v0.1.15 · round 157): 24-10 session 28.3 · 47.1 · 43.9; TWIN SWAG
  26.0 · 38.2 · 37.8; kwe 23.8 · 28.6 · 29.3; game over 38.1 · 50.7 · 47.5; lu 27.7 · 50.7 · 42.6. Overall
  45.5 → **42.3 / min (−7 %)**; pitch accuracy (median 5.3 → 5.1 cents) and harmonics-to-noise unchanged.
- Tried and dropped [RUN]: bridging 3–35 ms voicing dropouts inside notes (keeping the last period while YIN clarity
  ≥ 0.7). It helped one session (50.8 → 47.2 / min) but made TWIN SWAG and kwe worse and pitch-marked the start of
  consonants (round-95 transparency test, −60 dB residual) — not landed.
- [TEST] `tune_harmonic_guard_holds_the_fundamental_round157`: f0 290 Hz on a narrow 870 Hz formant, no frame on a
  multiple; the round-95 accuracy (82–700 Hz, three rates, < 1 cent) holds with the guard on; the round-147b range
  test (a clean 450 Hz tone) needed the 0.03 margin.

## 4. Listening files

`D:\lynx-work\tune157\ab\<take>\`: `1_dry`, `2_autotune` (trial, her state), `3_lynx_v0.1.15`, `4_lynx_new`,
`A_B_sequence.wav` (Auto-Tune, beep, v0.1.15, beep, new) and `spectrogram.png`, all aligned.

## Where I stopped

- Landed: harmonic guard, the Master double-monitoring warning, `--rtstress --log`.
- Remaining gap to the trial: ~12 bursts / min more, mostly at note onsets with fast scoops and the short voicing
  dropouts (needs a better unvoiced decision than YIN clarity, not a bridge), and on low notes (180–260 Hz, up to
  −3 dB there). Next: onset handling of the PSOLA marks; low-note grain size.
- Not done: importing FL's insert input (event 0x9A) into LYNX projects; a warning when a substitute replaces a plugin
  FL would simply skip; `--insert` export still lets master-direct channels through (minor, CLI only).
- Ask the user: did she hear it while singing (live) or on playback? Which Master input did she set, and why two ASIO
  drivers? Are Waves RComp / DeEsser meant to be on her laptop?
