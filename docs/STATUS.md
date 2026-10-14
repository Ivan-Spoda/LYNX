# Status

Last updated: 2026-10-13. Everything below is verified by the core tests (more than a million checks), the
app's self-test (`LYNX.exe --selftest`), off-screen UI snapshots (`--snapshot`) and a regression run that
exports the 30 demo projects shipped with FL Studio 2026.

## What works

### Editing
- **Channel Rack:** step sequencer, per-channel volume/pan/mute/solo, swing (FL formula, per-channel mix),
  multiple patterns (rename, colour, length, clone, remove), cut groups, mono channels, arpeggiator,
  PC keyboard as a piano (FL layout), MIDI input, note recording (one undo step per take).
- **Piano Roll:** selection (Ctrl+drag, Shift+click, Ctrl+A), group move/resize, Ctrl+C/X/V, Ctrl+B duplicate,
  Ctrl+Q quantize, arrow keys (Shift = octave/bar), snap selector, chords and scale highlighting, per-note
  velocity/pan/fine pitch/mute, slide (808 glide) and porta notes, `.mid` import/export.
- **Playlist:** pattern, audio and automation clips; multiple arrangements; loop region; multi-selection,
  copy/paste/duplicate, split, "make unique"; named/muted tracks; markers and time-signature changes on the
  ruler; normalize audio clips; pattern picker.
- **Automation:** right-click any knob/fader (channel, mixer, effects, plugin parameters) → a Playlist clip with
  a point/curve editor (Alt+drag for curves); tempo automation; MIDI learn on any parameter.
- **Mixer:** up to 127 inserts, sends with levels, "route to this track only", solo along the signal path, FL
  fader curve, gain ramps, stereo separation, phase invert, L/R swap, colours, built-in 3-band track EQ,
  10 effect slots with dry/wet, smart disable, per-edge PDC, **sidechain** (right-click a compressor slot →
  "Sidechain from"), render insert to audio.
- **Project safety:** "*" in the title, "save changes?", autosave every 2 minutes and an emergency save on crash
  (`%APPDATA%\LYNX\autosave`).

### Sound
- **Built-in synth:** 3 oscillators (sine, triangle, saw, square, noise and a band-limited **harmonics**
  oscillator built from an amplitude/phase table by IFFT), octave/semitone/fine/pan/level per oscillator,
  LP/BP/HP/BS filter with keytracking, cutoff and pitch envelopes, 4 LFOs, AM, FM with decaying index, ring
  modulation, kick pitch sweep, 3-band tone EQ, mono and portamento.
- **Sampler:** multi-sample zones (key/velocity), slices, reverse, cut, time-stretch (resample or WSOLA), root
  note, start offset, channel filter and envelopes; long audio streamed from a memory-mapped disk cache.
- **Built-in drums:** kick, snare, hat, clap.
- **Effects:** EQ, 7-band parametric EQ, compressor (with sidechain), limiter (lookahead, PDC), delay, reverb,
  chorus, filter, distortion, lo-fi, Effector (12 algorithms), vocoder, pitch correction, volume gate, rhythmic
  envelope, auto-pan, Linkwitz-Riley crossover, pitch shift, frequency shifter, utility and a nestable
  parallel container.
- **Plugins:** VST3 and CLAP (own CLAP host on top of JUCE); scanning always runs in separate processes with a
  blacklist; optionally every plugin runs in a separate process (*Options → Run plugins in a separate process*).
- **Export:** WAV 16/24/32f, FLAC, OGG; whole song or pattern; tail until silence; TPDF dither + noise shaping
  for 16-bit.
- **Performance:** lock-free engine, channels and mixer layers on worker threads, idle channels skipped, FTZ/DAZ.

### FL Studio import (`.flp`)
- Structure: channels, real samples (including Ogg Vorbis WAVs and WavPack), patterns, notes with
  slide/porta/pan/pitch, every arrangement, markers and time signatures, automation clips (1100+ in the demos),
  controller links (Peak Controller, Envelope/Keyboard Controller, X-Y/Control Surface as constants).
- Mixer: routing and levels, sidechain links (level-0 routes), track EQ, colours, names, and native effects with
  their saved settings: Parametric EQ 2, Limiter (incl. its compressor and sidechain), Compressor, Reeverb 2,
  Delay 2/3, Maximus, Soundgoodizer, Fast Dist, Fruity Filter, Flangus, Soft Clipper, Gross Beat, Effector,
  Love Philter, Frequency Splitter, Stereo Shaper, Balance/PanOMatic, Pitch Shifter, Frequency Shifter,
  Emphasis, Low Lifter, Send, Transporter, Squeeze, Pitcher, Fruity Vocoder; Patcher graphs of any shape are
  reduced to series/parallel chains.
- Generators: 3x Osc, FPC, DirectWave, FL Keys, Slicex/Fruity Slicer, Wave Traveller, DrumSynth Live (by
  sample); Sytrus, Harmor, Harmless, GMS, Kepler, Ogun/Autogun, MiniSynth, DX10, Fruit Kick, Toxic Biohazard,
  Sakura, Morphine, PoiZone, BassDrum, BooBass and Drumpad on the built-in synth.
- Third-party plugins in the Fruity Wrapper are matched to installed VST3/CLAP plugins (VST3 class ID, CLAP id
  or name, preferring the format FL used) and their state is restored (VST3 and CLAP); automations of a VST3
  follow FL's persistent parameter slots (record 4 → ParamID; skipped with the plugin sandbox on).
- FL presets: 169 3x Osc channel presets, ~2800 `.fst` synth presets and the effect presets of the supported
  effects.
- An import report lists what was converted, approximated or left out.

## Confirmed vs approximated (FL import)

Every conversion rule comes from the behavioural analysis in `docs/analise/REA_rodada*.md` (decompilation,
the official manual and project data). Rounds 17–24 verified the earlier hypotheses and the code was fixed
accordingly. Highlights of what is **confirmed and applied**:

- **Effector:** its own effect (`core/Effector.cpp`) with all 12 algorithms and FL's 12 raw parameters; LFO
  locked to the song, Mod Rate/Tempo Reduce, input/output gain; exact Distort cascade, Lo-fi, Delay (Mod Rate
  time), Reverb (dry kept), Stereo, Trancer, Stutter, Vowel and Ring.
- **Harmor:** cutoff in log2 Hz (73.4 Hz–17.7 kHz) with keytracking from 130.81 Hz, filter types 0–18, part
  volume `(3^x − 1)/2` (up to +12 dB), level², filter envelope centred at 0.5 (±2·(hi − lo)·amt); exact timbre
  from the Timbre 1/2 curves, blend, local EQ (1/n) and per-harmonic phase; unison with FL's per-voice detune
  table, pan and gain; resonance peak from the type's table (Classic … Lowpass, Custom shapes) and the resonance mask
  (REA round 25).
- **Sytrus:** FM depth 4·g cycles; ring modulation `1 − |a| + a·mod`; exact operator waveform (base shape,
  Half/Even, Skew, Sine shaper, normalisation, Tension, Absolute, Pre-filter) turned into a spectrum by FFT.
- **Harmless:** cutoff range with per-type margins, keytracking, centred non-adaptive filter envelope, one-sided
  cutoff LFO, song-locked LFO; exact timbre (12 colours incl. Custom, harmonic mask, detune ±1200/±2400,
  6-band timbre EQ, low-harmonics protection, Sub 1/3/4).
- **Ogun:** exact initial spectrum of the timbre engine (seeded IPP-style generator, fullness/morph/pre-decay
  weights, Forced/Impulse maps and patterns, RMS normalisation, fixed seed-3 phases) on the spectral oscillator,
  with each partial's own decay `(t − 0.025)·B·E_dec + 0.025` (the spectrum evolves during the note); cutoff `44100·(c + c₀)` Hz, FL's Q/width, 20 s release, unison ±5(11^(P15/256) − 1) cents.
- **MiniSynth:** LFO periods, squared LFO depth, release, second voice in whole semitones + 7.5 cents.
- **Toxic Biohazard:** FM 2·cell (matrix direction read from the code), filter modes (p300), cutoff `2^(9u + 5)`,
  key-track 0.09·KT oct/semitone, envelope ±9·EGAmt oct.
- **Kepler:** LFO1 rate, vibrato and cutoff depth (peak), delay. **BassDrum:** Peak `1 + 100x³·s⁴`.
  **Sakura:** ADSR, Tune, Mix, Hi-Cut. **BooBass:** 3-band EQ. **Drumpad:** Pitch and Mallet Amplitude.
- **Morphine:** average spectrum of the generators on the harmonics oscillator.
- **Love Philter:** articulator base, Mod X/Y → volume (with automation/links), filter types 4/5 (width in
  octaves), 7 (all-pass) and 11 (Mango); pattern envelopes on volume (rhythmic envelope effect, in beats, looped,
  song-locked, tension curves), pan LFO (auto-pan effect), cutoff and resonance envelope/LFO as live
  modulation of the EQ (top-level live automation, or links inside the parallel container with several units).
- **Patcher:** any graph in the demos reduced to series/parallel (nested containers), cables of the right class.
- **Sidechain:** level-0 routes are sidechain links; Fruity Limiter (compression section) and Fruity
  Compressor with a side input become LYNX's compressor listening to the source track.
- **Pitcher:** pitch correction with FL's laws (speed, Fine, A reference, the note mask stored in the state,
  Key Bypass, range).
- **Fruity Vocoder:** FL's algorithm (band edges, cascaded band-passes, envelope followers, levels).
- **Frequency Splitter:** exact Linkwitz-Riley structure for every slope (6–96 dB/oct).

**Still approximated or left out** (no confirmation yet):
- Harmor: the resonance becomes an SVF Q with the same peak over the pass-band — Harmor's bump sits at
  fc·2^(p60/1200) with its own width (p59) and skirts (Classic −6 dB, Lowpass dip above), which the SVF cannot
  follow; Well, Noise, self-oscillation, Hz scale and adaptive width are left out; image/blur/prism/FX; with the volume envelope off FL keeps the note
  until it is stolen — LYNX uses a short declick release.
- Effector: the Filter cascade (FL's is not tabulated), exact Flanger/Phaser feedback topology, Reverb decay.
- Sakura: physical strings become saw + triangle; string decay taken at key 60; Hi-Cut applied at the output.
- BooBass: the source is an internal sample not stored in the project (LYNX uses a saw). Drumpad: the physical
  mesh model is not reproduced.
- Toxic: FL's filter has 4 poles (LYNX's has 2); resonance scale; linear filter envelope.
- Kepler: chorus (the LYNX synth has none); "Range" ×10 (Exo only); LFO BPM sync.
- Harmless: the filter as a per-partial curve (LYNX uses a biquad); p39 outside ±1200/±2400; stereo tremolo
  (2.5 Hz) and vibrato (6 Hz); resonance width.
- Ogun: the evolving spectrum is 8 snapshots per note with linear crossfades; partials binned onto one harmonic
  share an energy-weighted decay; the per-partial release is one overall envelope; ALT DECAY's morph and the read-start phase offset are not reproduced, articulators
  are at their neutral values, Seed 2 "Random" (new spectrum per note in FL) is one fixed draw, Seed 1 per key uses
  key C5, and with richness > 9 the inharmonic partials are binned (by energy) onto harmonics of f/4.
- Sytrus: carriers an integer multiple above the main one join its exact spectrum (sharing its envelope); other
  carriers (detuned, panned, offset or FM-modulated) and the FM modulator use basic shapes.
- Love Philter: Mango Hz inferred from its pole; the cutoff/resonance modulation reaches the EQ one block late
  (control slot → live link); curve type 5; pan law and pan-LFO tension.
- Pitcher: the pitch detector (YIN) and shifter (two heads) are LYNX's own; Formant/Gender, Harmonize and MIDI
  modes are not implemented.
- Vocodex: becomes the LYNX vocoder with two side inputs (modulator/carrier from ModIn/CarIn); its band filters,
  per-band maps, carrier noise, HP/LP, Soundgoodizer and unison differ, the output level is not calibrated against FL,
  and the internal Sytrus carrier (CarIn = "---") is not available (only the modulator passes).
- Squeeze, Vintage Phaser: approximations.
- FLEX: its packs are encrypted; LYNX never opens them (the channel gets a synth patch by preset name).

## `.flp` format findings (besides `docs/ANALISE_FL_STUDIO.md`)
Confirmed empirically on the demo projects:
- Note record: 24 bytes in every version, including 25.x.
- Playlist record: 32 bytes (< 20.99.1), 60 bytes (20.99.1 to 24.1), 80 bytes (≥ 24.2.99).
- Playlist: 199 tracks before FL 12.9; 500 from then on (track index inverted).
- Playlist item: index > base (20480) = pattern (index − base); otherwise a channel clip.
- A channel's mixer track: event 0x68 (u16); 0x16 (u8) only in old files.
- A mixer insert's name (0xCC) comes before the 0xEC that opens that insert.
- Channel name: 0xCB; files ≤ 11 use 0xC0. Internal plugin name: 0xC9.
- The version string (0xC7) is always ASCII; other strings are UTF-16LE from 11.5.4 on.
- In pattern clips the 8 bytes at +24 are two int32 tick values (start and end of the pattern slice), not
  floats; −1 (NaN when read as float) = no slice. In channel clips (audio/automation) they are floats, in beats
  or milliseconds depending on the project. FL does not stretch audio clips.
- Many factory samples are WAV files with format tag 0x674F/0x6750 and an Ogg Vorbis stream in the `data`
  chunk; others are plain Ogg files with a `.wav` extension. Paths use `%FLStudioFactoryData%` (install folder),
  `%FLStudioData%` (install\Data) and `%FLStudioUserData%` (Documents\Image-Line\FL Studio).

## Plugin hosting
- Optional separate-process plugins: each plugin runs in a child `LYNX.exe --lynx-plugin-host`. Audio
  (up to 8 channels × 4096), MIDI (up to 1024 events) and parameter changes (up to 512 per block, both ways) go
  through shared memory with a pair of events per block; state and editor through messages. If the child does
  not answer within 0.5 s or exits, the plugin goes silent, a notice appears and LYNX keeps running. Verified with
  `LYNX.exe --sandboxtest plugin.vst3 report.txt`; `LYNX_SANDBOX_LOG=file` logs every step.
- The VST3 SDK bundled with JUCE builds with Zig/MinGW; plugins are prepared at a fixed rate and re-created
  when the device rate changes; plugin latency feeds the PDC.

## Not yet verified by a human
- Audio input recording (depends on the user's hardware; code reviewed, not tested).
- CLAP plugin editor windows (audio hosting tested; windows not opened to avoid touching the user's screen).
- Mouse/keyboard interactions are checked by code review and off-screen snapshots only.

## Open items
- ASIO (Steinberg SDK licensing).
- Ogun's per-partial release and ALT DECAY, Vocodex's internal carrier synth, Harmor's per-partial resonance and image synthesis.
