# REA round 135f — FabFilter / Slate / Antares roles: the PARITY.md gaps users touch

Sub-round of parity135 (branch `feature/parity135f`). English-first, clean-room. No third-party binary was opened
in this sub-round: the gaps come from docs/PARITY.md (round 128's black-box captures of the official trials and
their manuals) [MANUAL]; every behaviour below is LYNX's own code, checked by tests/Parity135fTests.inc [EXEC].
Every new parameter is appended (saved projects keep their meaning) and defaults to the old sound / view.

## 1. Apex Limiter ← Pro-L 2
- **Loudness meter** (core/Loudness.h, header-only): ITU-R BS.1770-4 K-weighting (coefficients derived for any rate;
  at 48 kHz equal to the standard's table [EXEC]), momentary 400 ms / short term 3 s every 100 ms, integrated with the
  −70 LUFS absolute and −10 LU relative gates (0.1 LU histogram: unlimited length, no allocation), loudness range after
  EBU Tech 3342 (−20 LU gate, 10th–95th percentile), 4x true peak (48-tap windowed sinc). EBU test cases [EXEC]:
  1 kHz at −23 dBFS = −23.0 LUFS M / S / I at 44.1 / 48 / 96 kHz; −36 / −23 / −36 → I = −23; −20 / −30 → LRA 10 LU.
- Editor: LOUDNESS button → panel (short-term / momentary bars, target line −23 … −9 LUFS, integrated, short term,
  momentary, range, peak, Sample / True peak tabs, Reset). The engine publishes the values on meters 1, 4–7.
- **Peak meter** (sample / true peak) drives the output meter, its hold and the peak read-out.
- **Display modes**: Fast (10 ms a pixel, the old view), Slow (30 ms), Slow down (the span grows to the left),
  Infinite (an editor-side history of 0.5 s columns, ~11 minutes), Off.
- **Oversampling** Off / 2x / 4x / 8x: a second limiter core runs at the high rate between the shared half-band
  oversampler's up and down paths (the "capture then return" use of mixsuite::Oversampler, as Crest does); its latency
  is padded to whole base-rate samples, so PDC stays exact (a click comes out after exactly the reported latency
  [EXEC]). Dither runs at the base rate after the down path; a final clip at the ceiling.
- **Side-chain triggering**: the required gain comes from the slot's side chain (× the same gain), the safety clip
  still holds the programme.

## 2. Oversampling in Sentry and Hush (Pro-G / Pro-DS bottom bars)
The detector stays at the base rate; the gain (and, in Hush, the band it acts on) is upsampled with the audio and
applied at the high rate (output = input + band × (gain − 1)). Blocks over 1024 samples are split. With 4x the
result equals the base-rate one delayed by the oversampler's latency to better than −20 dB [EXEC]. Hush's measured
classic voicing (vmatch123) is left without oversampling. Sentry's bottom bar trades its duplicate look-ahead switch
(the switch stays under the knob) for the menu.

## 3. Prism Multiband ← Pro-MB
- 1–6 contiguous bands (a band count; 4 = the old engine sample for sample [EXEC: the existing transparency and
  dynamics tests pass unchanged]); bands 5–6, crossovers 4–5, their look-aheads, six band pans and the external side
  chain are appended (param index of band b ≥ 4: 52 + 10 (b − 4) + field).
- Double-click the empty display: a new band there (the band under the mouse splits at that frequency; the new one
  copies its settings); band menu: Split / Remove the band. Pro-MB states translate up to six bands.
- Band PAN (left / right balance of the band's output), external side chain (the detectors hear the side chain split
  by the same crossovers).
- Left: free (overlapping) band edges, per-band slopes, linear / minimum phase modes, oversampling, side-chain
  filtering and per-band stereo link.

## 4. Crest ← Pro-C 3
Wet pan / dry pan (left / right balance of each path) in the side-chain panel; centred = the old code path exactly.
Left: Host Sync / MIDI triggering, audition triggering, 6 side-chain EQ bands, 32x.

## 5. Facet EQ ← Pro-Q 4
- Flat Tilt (eight first-order shelves 1.25 octaves apart: a constant g / 5 dB per octave, 0 dB at the band frequency)
  and All Pass (bilinear 2nd-order all-pass; the analog prototype for the FIR modes). Stored at shape values 1/16 and
  3/16, between the old k / 8 values, so saved shapes keep their meaning; Pro-Q 4 states now translate both.
- Character Clean / Subtle / Warm (Processing menu): own soft-knee output curves, unity below about −20 dBFS.
- Left: 24 bands (Facet has 89 of the 96 parameter slots of a slot; more bands need a bigger parameter array),
  0 dB/oct and Brickwall slopes, spectral dynamics, EQ match, linear-phase resolution.

## 6. LYNX Tune ← Auto-Tune Pro
- LEARN SCALE: while on, the pitch classes held on the MIDI channel (engine meter 3, a 12-bit mask) build the custom
  scale in the editor (switching it on starts a new one); learning notes are not used as targets.
- CLASSIC: the correction glides at a constant rate (one semitone per retune time) — LYNX's own law.
- Params appended in one block commented "round 135f" (sub-round w appends Waves Tune RT params after it).

## Where I stopped
- Done: §1–§6 with tests (tests/Parity135fTests.inc), pt-BR strings, PARITY.md rows + "135" score columns.
- Not done (ranked): Prism free band edges / phase modes / oversampling; Facet 24 bands (needs maxEffectParams > 96)
  and Brickwall; Crest Host Sync / MIDI triggering; Ember's Saturn rows (6 bands, modulation, M/S) — left alone so
  sub-round s can edit Ember for Decapitator.
