# Status

Last updated: 2026-10-14. Everything below is verified by the core tests (more than a million checks), the
app's self-test (`LYNX.exe --selftest`), off-screen UI snapshots (`--snapshot`) and a regression run that
exports the 30 demo projects shipped with FL Studio 2026.

## What works

### Editing
- **Channel Rack:** step sequencer, per-channel volume/pan/mute/solo, swing (FL formula, per-channel mix),
  multiple patterns (rename, colour, length, clone, remove), cut groups, mono channels, arpeggiator,
  PC keyboard as a piano (FL layout), MIDI input, note recording (one undo step per take). Step graph lanes
  (Ctrl+K: velocity, pan, release, fine pitch, Mod X/Y; drag, right-drag ramp, Alt+click reset), fill every
  2/4/8, Euclidean fill, rotate, sort channels, select unused.
- **Recording options:** count-in (Ctrl+P), wait for the first note (Ctrl+I), overdub/replace, record quantise
  following the global grid, a pattern clip placed in Song mode; switches next to the metronome in the main
  toolbar and the full set in the "what to record" menu and Options → Recording.
- **Quick-add palette (F8 / plugin-picker icon):** searchable instruments and effects (new channel or selected
  insert). **Undo history** (Ctrl+H, Edit menu): named steps, click to jump.
- **Piano Roll:** selection (Ctrl+drag, Shift+click, Ctrl+A), group move/resize, Ctrl+C/X/V, Ctrl+B duplicate,
  Ctrl+Q quantize, arrow keys (Shift = octave/bar), snap selector, chords and scale highlighting, per-note
  velocity/pan/fine pitch/mute, slide (808 glide) and porta notes, `.mid` import/export. Note tools
  (Arpeggiate, Strum, Flip, Randomise, Scale levels, Limit; Alt+A/S/Y/R/X/K), snap to scale (toolbar switch next
  to the scale box, also for incoming MIDI), zoom presets (PgUp/PgDn, Shift+1..5/0), vertical zoom.
- **Playlist:** pattern, audio and automation clips; multiple arrangements; loop region; multi-selection,
  copy/paste/duplicate, split, "make unique"; named/muted tracks; markers and time-signature changes on the
  ruler; normalize audio clips; pattern picker. Ripple editing (Shift+S) and ripple delete, zoom presets,
  inline automation-point editing (axis locks, typed values, re-range), track and clip menus.
- **Automation:** right-click any knob/fader (channel, mixer, effects, plugin parameters) → a Playlist clip with
  a point/curve editor (Alt+drag for curves); tempo automation; MIDI learn on any parameter.
- **Mixer:** up to 127 inserts, sends with levels, "route to this track only", solo along the signal path, FL
  fader curve, gain ramps, stereo separation, phase invert, L/R swap, colours, built-in 3-band track EQ,
  10 effect slots with dry/wet, smart disable, per-edge PDC, **sidechain** (right-click a compressor slot →
  "Sidechain from"), render insert to audio. Multi-select inserts (faders/pans move together), Alt+Left/Right
  reorder, link selected channels (Ctrl+L / Ctrl+Shift+L / Alt+L), "sidechain only" sends, per-insert record
  arm, effect slots dragged to reorder, all-effects switch, A/B compare per slot, effect presets dropped from the
  Browser.
- **Browser:** search (Ctrl+F: wildcards, AND/OR/NOT), Up/Down audition, Shift+Up/Down and Enter load into the
  selected channel, user folders.
- **Window menus:** each window's title-bar menu carries its own items (Playlist ripple/zoom, Channel Rack graph
  lanes/rotate/sort, Piano Roll tools/zoom/scale snap, Mixer move/link, Browser search/add folder).
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
- Generators: 3x Osc on its engine (REA round 47: the three oscillators on FL's host wave tables, stereo detune and
  phase, random start phases, AM, the custom wave from the channel's sample read from the user's installation, the HQ
  band-limited oscillators, and the plugin voice processor 3x Osc keeps, with its release path; bit-identical to FL's
  DLL through our reference model on all 239 distinct non-HQ states of the installation at 44.1/48/96 kHz and on 43
  processor cases; HQ within −137 dB), FPC, DirectWave, FL Keys, Slicex/Fruity Slicer, Wave Traveller; DrumSynth Live on its
  engine (REA round 30: all 120 patches, one per key, morph, voice modes; recorded waves read from the user's own FL
  installation; within −138 dB of the reference model verified against FL's DLLs); Harmless on its additive engine
  (REA round 32: 528-partial bank, FL's full voice; within −151 dB of the reference model, which is within −60 dB of
  FL's DLL on 135 of 139 demo channels) and its effects rack (REA round 36: chorus, delay, reverb and the Maximus
  compressor with Harmless's own presets read from the user's FL installation; the model with the rack is within −60 dB
  of the DLL on 135 of 139 demo channels); Sytrus on its engine (REA round 31: the whole instance from the raw plugin
  state — operators, FM/RM matrix, articulators, unison, portamento, filters, effects chain; FL's render settings when
  exporting, its live ones when playing); Harmor on its additive engine (REA round 34: both parts, 520-partial bank,
  module chain, articulators, and its effects rack from REA round 38; within −150 dB of the reference model on 88 of
  the 89 demo channels it runs, 82 of 99 demo channels within −60 dB of FL's DLL with the whole rack on); Ogun on its
  inverse-FFT engine (REA round 37: the whole voice, articulators and plugin effects from the raw state; within float
  rounding of the reference model — which is within −60 dB of FL's DLL on 20 of 20 demo channels — on all 20 demo
  channels and 55 synthetic cases); GMS and Morphine on their engines (REA round 39: bit-identical to the reference
  models on every demo channel, voice and full chain, at 44.1 and 48 kHz; the models are within −60 dB of FL's DLLs on
  9/9 GMS and 7/7 Morphine demo channels; GMS's waves and Morphine's noise samples read from the user's FL
  installation); MiniSynth on its engine (REA round 41: wrapper, 99 voices, the ten oscillator kinds on the 1024
  tables built from the user's `oscdata.dat`, the exact matrix filter, envelopes, LFO and its four effects; bit-identical
  to FL's DLL on 11 of the 12 states of FL's projects and 47 of its 91 presets, the rest within −107…−147 dB); Fruity
  DX10 on its engine with the channel processor it keeps (REA round 42: bit-identical to the reference model, which is
  bit-identical to FL's DLL on all 64 distinct installation states and on the 12 project channels with their channel
  envelopes/filter); Kepler on its engine (REA round 46: the whole Kepler-1 engine with the DLL's rounding mode, LFO
  shape and noise read from the user's own engine DLL; the default patch bit-exact against FL's DLL, the 4 demo channels
  within −124 dB, 273 of 283 preset/demo states within −99 dB); PoiZone on its engine (REA round 44: voices, gate, arpeggiator, chorus and tape delay from the raw state;
  bit-identical to FL's DLL on 384 of 385 presets with the delay off and on the render test, the delay's low/high-cut
  coefficients differ by the CRT's low-accuracy sincosf: median −79 dB, worst −48 dB); Fruit Kick and BassDrum on their engines (REA round 43: Fruit Kick bit-exact with FL's
  DLL outside the 10 ms release fade — FL's IPP cosine, ≤ −83 dB — with FL's host waveshaper and DX10's shared voice
  processor, all 8 filter types checked; BassDrum bit-exact on 228/228 cases at 44.1 and 48 kHz with the engine's own
  CRT powf/sincosf, its table and noise samples read from the user's installation); Toxic Biohazard on its engine (REA
  round 45: bit-identical to FL's DLL on the 3 demo channels, voice and full effects chain, at 44.1/48/96 kHz, the
  built-in waves read from the user's FL installation); Autogun on the built-in synth; Sakura and Drumpad on physical models rebuilt from their
  engines (REA round 28), BooBass on its engine with the multisample read from the user's own FL installation
  (REA round 29; within −138 dB of the reference renders verified against FL's DLLs).
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
  time), Stereo, Trancer, Stutter, Vowel and Ring; Filter, Flanger (tape delay) and Phaser exact — impulse
  responses within 6·10⁻⁸ of FL's own engine (REA round 27); Reverb exact (FL's own diffusion network, size and
  damping law; −100 dB or better against the DLL at 44.1/48/96 kHz, REA round 33).
- **Harmor:** its own instrument (`core/Harmor.cpp`, REA round 34; the channel keeps the plugin's raw state): per
  enabled part × unison voice a record with a 520-partial sine bank (self-oscillation, Sub 1/3/4, 516 harmonics,
  linear or logarithmic spacing, per-partial amplitude glide, partials entering after pitch drops), targets rebuilt
  every 2 ms control block from the two timbres through the part's user-ordered module chain — local EQ, prism
  (both modes, from level), harmonizer, filters 1/2 (shapes 0–7, 9, 14, custom 17/18, octave/Hz scale, key tracking,
  knob smoothing, envelope/adaptive envelope, serial/parallel mix) with resonance (100–108, custom 110/111, masks),
  phaser (200–210, incl. the frequency phaser), pluck, time and harmonic blur with mask, protection, clip modes 0–4,
  volume order — then subs below, self-oscillation, top fade, global EQ (with mix), auto-gain; the 43 articulators per
  part with FL's point curves (modes 0–12), envelopes (ADSR scales, sustain-level offset), LFOs (global phase,
  tension/skew/pulse width), key/velocity/X/Y/Z/random/unison maps and time smoothing; unison types 0–4 (random and Hz
  included) with the per-partial pitch curve; vibrato, stereo tremolo, legato/portamento/mono, master volume ramp;
  table reads nearest or interpolated by the precision setting. FL's level law (part gain·(Final.Vol + offset)·unison
  gain through √2·cos/sin, channel volume applied once — checked against the DLL at FL's default channel volume:
  0.000 dB). Bit-for-bit to float32 output rounding against the reference model on 88 of the 89 demo channels it
  runs (the 89th, −79 dB: numpy's float32 `exp`, which is not correctly rounded, in the per-partial unison curve) and
  on all ~31 synthetic feature cases at 44.1 and 48 kHz (the frequency phaser at precision 0: −96 dB, the same
  `exp`). The old spectral-oscillator conversion remains for unreadable states and for the synth's "FL presets" button.
  CPU: AVX bank kernel (8 partials per pass), running sine pairs across frequency changes (steps by complex rotation
  along the harmonic series), fast per-partial exp/log, cached k^m, idle records/channels skipped; "Square a Saw —
  Oxygen" (23 Harmor channels, 260 s) exports in 50 s, "JuiceBass — Menagerie" (8, 474 s) in 78 s.
  **Effects rack** (REA round 38, `hlfx::HarmorRack` in `core/HarmlessFx.cpp`, plugged into `harmor::FxHook`): the six
  units in the state's order — distortion (types 1–10 and the quantiser, 2×/4×/8× half-band oversampling by precision,
  bias/drive/dry/wet ramps, 4th-order post low-pass, DC stage), the shared library's chorus, delay (modes, feedback ramp
  from 0.5, stereo offset, pan law, in-path or in-loop high/low cut) and reverb (the shared Reverb_x64 DSP with Harmor's
  low/high cut, predelay, room, diffusion, decay, damping, colour, wet ramp), the records' FX send joining at unit 4,
  and the Maximus compressor with Harmor's own preset read from the user's installation (globals 49–52 → mix and the
  three pre gains; the RMS detector of preset 2 decoded); the activity counter (30/530 ms + the chorus ring). Against
  the DLL with its rack on: per unit median −78 dB (distortion −77.6, chorus −78.2, delay −79.9, reverb −79.6,
  compressor −78.3), level 0.00 dB on every channel whose voice is modelled.
- **Sytrus:** its own engine (`core/SytrusEngine.cpp`, REA round 31) driven by the plugin's saved state: 16 384-point
  operator tables with linear read-out and the 4-lane block reader, noise (shared LCG), plucked mode (IPP's uniform
  generator), band limiting (4 096 live / 16 384 export), CENTER/DECLICK, GLOBAL phase; the FM/RM matrix in FL's
  per-sample order (PM `4·g·out·MOD`, feedback `g·35/128`); every articulator (envelopes with all curve shapes 0–9,
  LFOs, key/velocity/unison/Mod X/Y/random maps, TEMPO, GLOBAL LFOs, global ADSR, velocity options) at FL's control
  tick; volume/MOD/pan ramps, output pan law and the voice-end ramp; unison (4 modes, sub layer, OSC grouping),
  portamento, GLOBAL PITCH, MONO KEY; the 3 filter units (13 types, ALT, HQ 2×, FLAT, waveshaper) and the post-voice
  chain (decimators, chorus, 3 delays, the shared reverb, 3-band EQ, DC stage). LYNX uses the state's *render*
  oversampling/tick settings while exporting (as FL does) and the live ones while playing. Against FL's own DLL on
  the 268 demo channels (one note, 0.5 s): median −116.9 dB live / −122.7 dB with the export settings; 265 / 264
  channels within −60 dB (268 / 267 when fed the DLL's own sine tables — the rest are chaotic FM patches); four
  overlapping notes with a key re-trigger (voice management, MONO KEY): 262/268 (REA round 31 §10).
  CPU: idle effects bypass themselves (exact), voices on the same tick grid are rendered in lockstep and the block
  reader runs in SSE2 — all bit-identical; "Overcome – Sytrus" (72 instances, several at 8× even live) plays in
  0.46× real time on one thread, 0.17× on four.
- **Harmless:** its own instrument (`core/Harmless.cpp`, REA round 32): the additive engine with up to 528 sine
  partials per voice record (self-oscillation, Sub 1/3/4, 524 harmonics; partial count and band limit per quality,
  10 ms amplitude glide), targets rebuilt every control block (88/44/8 samples at 44.1 kHz) in FL's order — colour ×
  6-band EQ, harmonic mask (+LFO), harmonizer (4 positions), per-partial filter (17 shapes, octave/Hz scale, key
  tracking, knob smoothing, tension-curve filter envelope), resonance (8 shapes, adaptive width, track motion,
  self-oscillation partial, Noise gates), phaser (10 tables, 3 scales, Freq type), pluck, low-harmonics protection;
  volume envelope with FL's smoothers and the 30 ms end/steal fade, all LFO shapes (global/retriggered, attack),
  6 Hz vibrato, stereo tremolo, unison (detune tables, pan, phase, attack, LFO offsets), portamento/mono/legato,
  grittiness, both phase-randomness modes; FL's level law (`0.75·(3^x − 1)/2`·Final.Vol·unison gain through the
  cos/sin pan law, channel volume applied once). Its effects rack (`core/HarmlessFx.cpp`, REA round 36) runs on the
  voice mix in FL's order: chorus (8 records of FL's chorus library, ⌊D⌋ taps), delay (normal / ping-pong with stereo
  offsets, tempo-synced time, feedback ramp), reverb (FL's shared plate reverb with 6 records) and the compressor =
  FL's Maximus engine (low cut, ×2 oversampling, Linkwitz–Riley crossovers, peak-window detector, √-scaled attack /
  release / release-2 stages, drawn-curve gain tables with tensions, lookahead, saturation, band mix) loaded with
  `Harmless/Data/Compression/<n>.fst` from the user's installation. The old synth approximation remains for unreadable
  states and for the synth's "FL presets" button.
- **Ogun:** its own instrument (`core/Ogun.cpp`, REA round 37; the channel keeps the plugin's raw state): per voice
  up to 32 767 partials (Richness) with seeded IPP-style weights, fullness shaping, morph, Forced (B·(1 − F) + F),
  Impulse/Decay/Release per-partial maps (sequential curve sampling, fixed patterns, 1/√h, logarithmic axis, EQ shift),
  pre-decay, RMS normalisation, band limit and update span; per 50/25 ms frame the spectrum follows the hold, ALT DECAY
  or release law and becomes an N-point table by an inverse real FFT (N = 2^(P52+q)); the two latest tables are read
  with the quality's kernel (linear, 6-point 5th-order Hermite, 32-point windowed sinc), crossfaded with the unison
  sub-voices' pan gains and phase spread (read start at the table's smallest sample), then declick, the 4-type ×
  1–3-stage filter (2× zero-order hold below 88.2 kHz), FL's volume/pan matrix with gain ramps, silence end, same-key
  cut and max polyphony; the 13 articulators (point curves modes 0–12, envelopes with ADSR knob scales, tempo sync,
  velocity options and portamento, LFOs with tension/skew/pulse width, key/velocity/X/Y maps); the plugin's 3-band EQ,
  chorus, delay (normal/inverted/ping-pong, stereo offset, tempo sync) and reverb (FL's shared plate reverb,
  `core/PlateReverb.h`, shared with Harmless's rack), and the 2:1 polyphase decimator of qualities 2–4. Level law
  `(3^(P0/128) − 1)/2`·Final.Vol through Ogun's own pan matrix (channel volume/pan applied once). LYNX plays the
  state's quality and exports at quality 4, as FL does when it renders. Against the round-37 reference model (own
  read start, same Seed-2 counter): bit-identical or within −125 dB (float rounding) on all 20 demo channels (44.1 and
  48 kHz, every effect on, and at the export quality 4) and on 55 synthetic cases (qualities 0–4, richness, unison,
  filter types, ALT DECAY, 1/√h, patterns, velocity options, portamento, host pan/volume/pitch/Mod X-Y, 48/96 kHz); the
  model itself is within −60 dB of FL's DLL on 20 of 20 demo channels. The core tests check the engine against model
  fingerprints: the 20 demo channels at the state's quality and at quality 4 (`ogun_demo_channels`) and 17 feature
  variants of a state written from scratch (`ogun_engine`). The old spectral-oscillator conversion remains for
  unreadable states, Autogun (another state layout), and the synth's "FL presets" button.
  CPU: one inverse FFT of N per voice and frame (split re/im radix-2 passes, cache-blocked, AVX when available), the
  quality-4 sinc reader vectorised with per-sub-voice tables, finished voices removed and idle channels skipped;
  "Electric Loop – The Ogun Dream" (14 Ogun channels, 96 s) exports at FL's quality 4 in ~15–22 s (the old
  approximation: 4.9 s), the other demos with Ogun channels within ±6 s of before.
- **MiniSynth:** its own instrument (`core/MiniSynth.cpp`, REA round 41): FL's wrapper (velocity from Init.Vol,
  per-note FinalLevels tables, engine calls of ≤ 512 samples in 32-sample blocks, voice kill), POLY/MONO/LEAD voice
  allocation with glide from the last note, the ten oscillator kinds as phased/detuned table saws with dithered reads on
  1024 band-limited tables built at run time from the user's `Minisynth\oscdata.dat`, noise, drift, the 4-pole filter
  integrated exactly per sample (4096 sub-steps by 12 matrix squarings) with its saturating first stage and DC blocker,
  both envelopes, the engine LFO (song-locked while the transport plays) and the effects (distortion + decimator, tempo
  delay, 6-stage phaser, chorus) — bit-identical to `rea-work/r41/py/msmodel.py` on all 12 installation states and 91
  presets, and bit-identical to FL's DLL on 11/12 and 47/91 (the others within −107…−147 dB: last-bit differences of
  the C runtime's sin/pow). Not reproducible by design: the random generator is process-global in FL (LYNX: one per
  instance, started like a fresh plugin). FL's max-polyphony value at channel creation is not confirmed (LYNX: 0 → 99
  voices). Without `oscdata.dat` the importer keeps the internal-synth approximation.
- **Toxic Biohazard:** its own instrument (`core/Toxic.cpp` + `core/ToxicFx.cpp`, REA round 45): FL's wrapper
  (velocity from Init.Vol, per-note level/pitch tables, engine chunks of 64), poly/mono voices with the 4 glide modes and
  stealing, six operators × up to 8 unison lanes on band-limited tables built from the 128-harmonic spectra (wave 0 from the
  state, waves 2…32 read from the user's `Toxic Biohazard_x64.dll`, SHA-256 checked), the FM matrix (operator i → j, one-sample
  delay), operator envelopes and Catmull-Rom level ramps, LFOs, the modulation slots (velocity/LFO sources), FL's ladder with
  its filter envelope, the master envelope, the output rack (distortion + DC blocker, two slots of delay/chorus/reverb/
  flanger/phaser/lo-fi, 8-band EQ, sleep logic, the DLL's CRT `powf/sinf/cosf/expf/tanf`) and the master gain —
  bit-identical to `r45/py/toxmodel.py`, which is bit-identical to FL's DLL on the 3 demo channels (voice and full chain,
  44.1/48/96 kHz) and 35 variants (features, rates, block sizes). The 2-operator conversion stays for states that need the DLL when it is not found.
- **Kepler:** its own instrument (`core/Kepler.cpp`, REA round 46; the channel keeps the raw state and the path of the
  user's `engine_x64.dll`, whose LFO-shape and noise tables are read at run time, SHA-256 checked): FL's wrapper
  (velocity from Init.Vol, same-key release, pan/pitch/X/Y expressions from the Final levels), 8 always-running voices
  with the band-limited DCO at 2× (saw, PWM pulse, sub, noise), the saturating 4-stage ladder, envelope or gate VCA,
  glide, key follow, the voice allocator, Hold and the internal arpeggiator (up, up/down, down; 1/2/4 octaves; free or
  synced), LFO1 (shape table, delay ramp, trig mode, BPM sync from the song position), the half-band decimator, the
  high-pass, the Juno chorus with its mode cross-fade and the main volume/pan — every float operation in the engine's
  order and rounding mode (toward zero in the render). Checked against FL's DLL (`kepler_engine`: the default patch
  bit-exact; `kepler_demo_channels`: the 4 demo channels). The synth conversion (`synthFromKepler`) stays the fallback
  (Kepler Exo / "Kepler-2" states, gate/noise/×4 bytes, DLL missing).
  **BassDrum** (fallback when the engine can't run): Peak `1 + 100x³·s⁴`.
  **Sakura:** ADSR, Tune, Mix, Hi-Cut. **BooBass:** 3-band EQ. **Drumpad:** Pitch and Mallet Amplitude.
- **GMS:** its own instrument (`core/Gms.cpp`, REA round 39 §1): FL's wrapper (velocity from Init.Vol, per-note
  FinalLevels tables, chunks of ≤ 512 samples, voice kill), the 99-voice engine with the MIDI matrix, 512
  band-limited tables per wave built from `GMS\Synth Waves\<name>.wav` of the user's installation, dithered
  oscillators with sync/FM/AM, unison, noise, slide, both LFOs (song-locked when synced and the transport plays), the
  4-iteration ladder with its 65 536-entry table, the ten effect slots (distortion, crusher, flanger, phaser, LP, HP,
  tape delay, plate reverb, panner, trancer) + 5-band output EQ and the master level/pan — bit-identical to
  `rea-work/r39/py/gmsmodel.py` on the 10 demo/render-test channels (voice and full chain, 44.1/48 kHz) and 33
  synthetic cases. Not reproducible by design: GMS's random generator is process-global in FL (shared by every
  instance), so noise, dither and the random start phases of fresh voices differ from any given FL render (LYNX starts
  one generator per instance like a freshly created plugin). FL's max-polyphony value at channel creation is not
  confirmed (LYNX uses 0 → 99 voices). Without the wave files the importer keeps the 3-oscillator approximation.
- **Fruity DX10:** its own instrument (`core/Dx10.cpp`, REA round 42): the voice (two parallel sine modulators on
  the host's sine table with Init → Sustain → release level glides, the Waveform shaper, Mod Thru, the 1.2/peak
  normalised amplitude envelope, the plugin-wide vibrato, voice stealing, re-tuning on Final.Pitch changes) and the
  plugin voice processor DX10 keeps: the channel's five INS articulators stepped per sequencer tick (never released,
  as in FL), the eight filter types on the x87 path, FL's x87 pan law with its gain ramps and the 441-sample end fade —
  bit-identical to our round-42 reference model (`r42/py/dxmodel.py` + `dxproc.py`), which matches the DLL bit for
  bit on 64/64 states, 12/12 project channels with their settings and the synthetic batteries. Version-1 states take
  the channel's 0xDA/0xDB records as their settings (FL's host hands them to the plugin: inference); the project's
  PPQ sets the tick rate.
- **PoiZone:** its own instrument (`core/PoiZone.cpp`, REA round 44; the channel keeps the plugin's raw state): FL's
  wrapper (velocity from Init.Vol, per-note FinalLevels tables, chunks of 64 samples), the 1 024-voice list (poly with
  stealing, mono legato with return to the nearest held key, 4 glide modes, unison 1–16 with detune/pan, octaver, analog
  drift), per-chunk envelopes/LFO/control values, the 8×-oversampled voices (saw/pulse phases, sync, ring, noise, soft
  clip, 4-stage filter LP/BP/HP, DC blocker; one voice or four SSE lanes with their own operation order and the reversed
  write-back of the oscillator levels), the decimator, the 16-step gate, the arpeggiator (4 modes, 1–4 octaves), the
  12 kHz shelves, the 3-line chorus and the stereo tape delay — bit-identical to `rea-work/r44/py/pzmodel.py`, which is
  bit-identical to FL's DLL on the render test, 384 of 385 presets with the delay off (one `powf` near-tie: −68 dB) and 10
  synthetic cases at 44.1/48 kHz. With the delay on, its low/high-cut coefficients use correctly rounded sin/cos where
  FL's CRT uses a low-accuracy vector sincosf (318 presets: median −79 dB, worst −48 dB; demo channel −72 dB). Not
  reproducible by design: PoiZone's three random generators are process-global in FL (LYNX starts one set per
  instance), so noise, drift, start phases, random LFO and random arpeggios differ from any given FL render; FL's
  max-polyphony value and its state/rate order at creation are not confirmed (LYNX: 64 voices, 1 when the channel is
  mono; state then rate).
- **3x Osc:** its own instrument (`core/ThreeOsc.cpp`, REA round 47) on the plugin voice processor shared with DX10
  (`core/FlVoiceProcessor.h`): the host's six wave tables recomputed from FLEngine's formulas (sine, triangle, square,
  saw, the exponential "rounded" shape, MT19937 noise), 32-bit phases with the DLL's pitch law, the right channel's
  detune and phase offset, Delphi-Random start phases and noise steps, the mix/invert/AM levels split by FL's host pan
  law, the DLL's x87 mixers (64-bit precision), the custom wave (the channel's sample, resampled to 16384 points), the
  HQ oscillators of `engine_x64.dll` (44 band-limited levels from an FFT, sinf sine, LCG noise), and the processor's
  release path (envelopes released from their value, the 10 ms end fade when the volume envelope is off), its rate
  ratio and ramp limit — bit-identical to our round-47 reference model (`r47/py/osc3model.py`) on 650 jobs (every state,
  4 rates/block layouts, random notes), which matches the DLL bit for bit on 239/239 non-HQ states at 44.1/48/96 kHz,
  43/43 processor cases and the custom wave. Every state version is normalised to version 14 with the channel's
  0xDA/0xDB records as its settings.
- **Morphine:** its own instrument (`core/Morphine.cpp`, REA round 39 §2): the whole Mor0 state tree, 32 voices
  (poly/stealing, mono legato with return to the nearest held key, 4 glide modes), control blocks of int(0.0029·fs),
  Eg1–4 with loops and the 12-slot matrix (incl. the Eg-amount destinations), master ADSR, 4 generators (keyboard
  zones, frame player, PWM) through the morph line into the 128-partial coupled-form bank with its Catmull-Rom
  envelope, the noise sample player (loop modes, filter) and the effects overdrive → chorus → delay → reverb → EQ —
  bit-identical to `rea-work/r39/py/mormodel.py` on the 7 demo channels (voice and full, 44.1/48 kHz) and 22
  synthetic cases. Not modelled (unused by FL's data): release loops, unison (hidden parameters). LYNX creates the
  instance at its playing rate, so the rate-change quirks of REA round 39 §2.13 do not apply; FL's max-polyphony value
  at creation is not confirmed (LYNX uses 0 → 32 voices). A missing noise sample leaves the noise silent (reported).
- **Love Philter:** articulator base, Mod X/Y → volume (with automation/links), filter types 4/5 (width in
  octaves), 7 (all-pass) and 11 (Mango); pattern envelopes on volume (rhythmic envelope effect, in beats, looped,
  song-locked, tension curves), pan LFO (auto-pan effect), cutoff and resonance envelope/LFO as live
  modulation of the EQ (top-level live automation, or links inside the parallel container with several units);
  FL's pan law (√2·cos/sin with the far side folded in), pan-LFO tension, pulse curves (type 5) as steps.
- **Patcher:** any graph in the demos reduced to series/parallel (nested containers), cables of the right class.
- **Sidechain:** level-0 routes are sidechain links; Fruity Limiter (compression section) and Fruity
  Compressor with a side input listen to the source track (their exact engines below).
- **Fruity Limiter and Fruity Compressor:** FL's exact engines as mixer effects (`core/FruityDynamics`, REA round 48),
  editable with FL's own controls. Limiter: gain, RMS compressor section (ratio/knee laws, curve stages, side input),
  peak window + look-ahead limiter with release/attack curves, noise gate, soft saturation or clip, silence bypass,
  x87-precision stages; look-ahead reported to PDC. Compressor: DC blocker, peak follower, the eight knee types incl.
  Vintage and "/R" dynamic release, makeup and the fixed 64-frame look-ahead brick wall. Compressor bit-identical to
  its DLL on all 70 unique installation states; Limiter bit-identical on 101 of 272 and within −129.5 dB on all (median
  −151 dB; the residual is IPP's vector log/exp/sqrt in the compressor section).
- **Pitcher:** FL's DSP (REA round 30, bit-exact against the reference model): integer-lag AMDF detector,
  nearest-allowed-note correction with FL's laws (speed, Fine, A reference, the note mask stored in the state, Key
  Bypass, range, Pitch Reference L/independent/R), plain 4-head shifter (9-sample latency) and Formant/Gender
  PSOLA path (257-sample latency), Harmonize and MIDI masks; latency reported to PDC.
- **Fruity Vocoder:** FL's algorithm (band edges, cascaded band-passes, envelope followers, levels).
- **Frequency Splitter:** exact Linkwitz-Riley structure for every slope (6–96 dB/oct).
- **Maximus and Soundgoodizer:** FL's exact Maximus engine as a mixer effect (`core/MaximusFx`, REA round 40) —
  RMS detector, saturation A/B, state versions 3–15, solo, legacy linear-phase FIR split, x87-precision smoothers;
  Soundgoodizer runs the older engine copy of its own DLL with the preset `a…d.dat` read from the user's installation
  at import and Amount as the band mix. Within −139 dB of Maximus_x64.dll on 96 of the 100 demo states (53
  bit-identical) and within −126 dB of Soundgoodizer_x64.dll on every preset/amount; latency reported to PDC.
- **Vocodex:** its own engine (`core/Vocodex.cpp`, REA round 35) built from FL's raw state, which the `.lynx`
  project keeps: band layout and the 13 per-band maps, 64-bit band-pass banks (orders 1–4, flatness, 2×
  oversampling above 11.05 kHz), PEAK/RMS followers, gate, saturation, double-precision smoother, unison, IPP-style
  carrier noise, consonant/carrier contour, mod/car pass-through filters and FL's gain ramps; ModIn/CarIn routing and
  L-R encoding. Bit-exact against the reference model (which is within −137 dB of FL's DLL) on all 77 comparison
  cases, the 10 demo instances included. Draft follows the stored button for playback and export alike (LYNX has
  no "HQ for all plugins" option): octave levels and 16-frame re-blocking, its 16 samples of latency reported to PDC.
- **Fruity Squeeze** and **Vintage Phaser:** FL's algorithms (REA round 29) — Squeeze within 5·10⁻⁹ of the reference model
  (which is bit-exact with FL's DLL), Vintage Phaser (V1/V2, ×4/×8 oversampling, noise, gate, delay) within −121 dB.

**Still approximated or left out** (no confirmation yet):
- Harmor (REA round 34 open items; `harmorFallbacks()` lists what a state uses): image/resynthesis data is ignored
  (the part plays its timbres; 8 demo channels carry images), arpeggio envelope points act as normal points (3 demo
  channels), the Noise resonance (type 109) is skipped, the blur variant of misc +0x46 runs as the normal blur (its one
  demo channel still lands at −85 dB), phaser/harmonizer mask curves, timbre blend modes ≠ 0 and non-neutral global
  envelope scales are ignored, filter shapes 8/10–13/15/16 and unknown resonance/phaser types switch that stage off,
  clip modes > 4 switch the clip off, the harmonic-randomness module is absent (never active in FL's data). In the
  rack, the distortion's shapers are exact functions where FL uses IPP's 11-bit `Tanh/Exp/Ln/Sqrt/Cbrt` at precision 0
  (−55…−72 dB against the DLL there, −82…−123 dB at precision 2; a full-drive type 6 also amplifies the voice
  residual by ~10 dB), and the compressor stays off when its preset file is not found (import report). FL seeds its Mersenne Twister from the clock, LYNX with a fixed value per
  instance: random unison, free/spectral phases and random maps are deterministic and differ from any FL render by
  design. Tempo-synced envelope/LFO rates, the phaser rate and the portamento time follow the song tempo (the
  reference was measured at 120 BPM). The polyphony limit is LYNX's (a pool of six notes' worth of records, 16–96;
  the oldest are stolen). Two demo channels stay at −58/−55 dB against the DLL as in the model (last-ulp `exp` amplified by positive phase randomness;
  an unexplained ~4 kHz residual).
- Sakura: mod wheel, pitch bend and channel pressure are 0 as matrix sources (LYNX sends no CCs to it); the LFO steps
  once per LYNX render block; with the resonators on the output is within −72 dB of the reference (their float32
  feedback amplifies rounding); strings longer than 9 600 samples (notes below ~20 Hz) are shortened.
- Drumpad: no key tracking (FL's DSP has none; the host's per-note pitch value is not confirmed).
- DrumSynth Live: the recorded waves (8 of the 24 default sounds use one) and the exact noise seed need the exact
  drumsynth_eng_x64.dll build (SHA-256 checked; otherwise those waves are silent and the noise starts from another
  seed); sends to other tracks (p35/p36), Mod X/Y and FL's link-velocity default are not modelled (Final.Vol = channel
  volume); a cut voice fades over 256 samples (FL's host ramp is not confirmed).
- BooBass: needs the exact BooBass_Eng_x64.dll build (SHA-256 checked; otherwise the saw approximation); keys below
  36 are clamped (FL reads outside its table there).
- Toxic Biohazard (REA round 45 §7): the step sequencer is ignored (off in every demo state); MIDI CC/aftertouch/bend
  sources of the modulation slots are 0 (LYNX sends none); the free-running operators' phase generator is process-global
  in FL (shared by every instance), LYNX starts one per instance like a fresh FL process, so random phases differ from a
  given FL render by design; FL's max-polyphony event value and its rate-before-state order at channel creation are taken
  from the GMS findings (inference).
- Kepler (REA round 46 §6–§7): single-ulp differences of the DLL library's pow inside the render remain (−125…−135 dB
  on the signal; self-oscillating patches — Resonance 1 — amplify them to audible phase differences of the
  self-oscillation, 10 of 277 presets, no demo); LYNX applies the channel volume as an output gain (how FL applies it
  to this plugin is not confirmed: the wrapper ignores Final.Vol and gets no MIDI-volume event); the free-running
  oscillators/LFO pause while LYNX skips an idle channel (FL's run from instance creation); old (version < 4) synced
  states get the converted LFO1 rate at the rate set-up after loading (whether FL re-sends it is not confirmed); the
  synth fallback keeps its earlier approximations (chorus, LFO1).
- Harmless: the rack compressor needs the user's FL installation (its four presets are read from it; otherwise it
  stays off and the import report says so); the chorus/delay use the x87 kernels FL picks on the test machine (another
  CPU may make FL round the chorus delay instead of truncating it — not confirmed); a fresh delay starts its feedback
  ramp at the stored value (FL's host call order not observed); Maximus's Master Mid mode, its v14+ linear-phase split and curve
  segment types other than 0/7 are not decoded (REA round 40); the editor's quality
  (0/1) is used for playback and export alike (FL renders with quality 2; LYNX's engine does not tell instruments
  when it renders offline); FL seeds its random draws from the clock, LYNX from a fixed seed (spectral-random
  phases, Noise resonance and the LFO "voice random" are deterministic and differ from any FL render by design);
  the Freq phaser at quality 0/1 follows FL's law but not the error pattern of its 11-bit exponential; host
  answers for the release-time factor (p30) and release velocity (p46) are 1 and 0 (FL's not observed); Mod X/Y
  (Final.FCut/FRes) are 0; FL's per-channel max polyphony is not imported (LYNX's limit is its 96-record pool);
  the LFO/phaser phases follow song-position jumps but not the start of playback.
- Ogun (REA round 37 §1.20 and open questions): Seed 2 "Random" uses a per-instance counter starting at 0 (FL's starts
  from its clock-seeded RTL random seed), so Random spectra are deterministic and differ from any given FL render; the
  read start j₀ (argmin of the first table) occasionally picks a neighbouring sample vs FL (a time shift of that
  note's table); IPP's 11-bit `exp` in the fullness shaping is evaluated exactly (up to −79 dB on the demos, −52 dB on
  the worst synthetic case vs the DLL); global LFOs/envelopes run per voice (none in FL's data); host answers 2/6 are 0,
  release velocity 0.5, Mod X/Y 0; parameter changes rebuild the instance (sounding notes stop); a voice pool grows on
  demand up to 64 voices per channel (the oldest is cut beyond that); FL's per-channel max polyphony is not imported
  (LYNX's mono setting = 1). Autogun stays on the synth approximation (its states mostly use another layout).
- 3x Osc (REA round 47 §5): HQ oscillators within −137…−168 dB of the DLL (LYNX builds the band-limited tables with a
  double FFT, FL with its own single-precision one; MSVC's sinf); the custom wave is resampled linearly (FL's resampler
  mode 6 not decoded); FL's host "HQ render" oversampling is not modelled; the random seed (start phases, noise steps)
  is per instrument, FL's is drawn per session and shared by all 3x Osc channels; the end fade uses the exact cosine;
  channel records win over a stale state snapshot (inference).
- Fruity DX10 (REA round 42 §4–5): the end-fade table uses the exact cosine (FL: IPP's 11-bit cosine; −129 dB worst
  case); FL's host restoring Final.Pitch before each tick (pitch articulator) and its exact sample placement of
  sequencer ticks are inferences (LYNX ticks on the song's PPQ grid while playing, free-running otherwise); global
  LFOs ignore song-position jumps; notes have no Mod X/Y; the filters are bit-exact only where long double is the x87
  format.
- Sytrus: chaotic FM patches (two-way FM loops, strong self-feedback) cannot be sample-exact — FL builds sine tables
  with IPP's tone generator (≈1 ulp from an exact sine) and the chaos amplifies it (statistically equivalent);
  random maps use a fixed per-instance seed (FL's is clock-seeded, so they are not reproducible in FL either); the
  noise generator is per instance (FL shares one across all Sytrus instances in the process); the mixer send of the
  FX bus (main int 0x2F4) is not routed; the GLOBAL envelope hand-over is exact only when the held voice rests on
  its sustain point (inside a segment FL's hand-over is not decoded: 3 demo channels); curve modes 10–12, the Mod X/Y
  SMOOTH coefficient and FL's polyphony setting (FPE_MaxPoly; LYNX: mono channels → 1, otherwise unlimited) are not
  reproduced; the FX tail length is inferred (100 samples + the chorus ring incl. one block); with the export
  settings a few demo patches still differ at −61…−80 dB (unison + full-noise operators into the FX bus, noise-heavy
  resonant Vanilla HP); the polyphony limit was not exercised against the DLL. States older than version 17 still use
  the old 3-oscillator conversion. Export with the render settings is CPU-heavy: most demo patches run their voices
  8× oversampled when FL exports, and LYNX does the same.
- Love Philter: Mango Hz inferred from its pole; the cutoff/resonance modulation reaches the EQ one block late
  (control slot → live link); pulse curves with more than 10 cycles.
- Pitcher: MIDI mode and Harmonize are implemented but LYNX effects receive no MIDI notes, so they behave as FL
  with nothing on the Pitcher's MIDI port (MIDI: no correction; Harmonize: dry voice / dry mix). The reported
  latency (9 / 257 samples) is the DSP's delay; FL's own PDC report is not confirmed.
- Vocodex: the Soundgoodizer stage (FL's engine with `Data/SG.dat`) and the internal Sytrus carrier (Ookay,
  NewStuff) need the user's FL installation at import (otherwise the old approximation / modulator pass-through and
  the import report says so); with the internal carrier the match is −112 dB (LYNX's Sytrus on that preset), the
  others −136…−145 dB; map segments with tension ≠ 0 are read as straight lines (no demo uses them).
- Maximus: 4 of the 100 demo states use the v15 linear-phase band split (Frequency Splitter's engine) or Master Mid
  mode is set — those keep the approximation (REA round 40 §8).
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

## Audio drivers
- Windows Audio (shared, exclusive and low-latency), DirectSound and **ASIO** (Steinberg ASIO SDK 2.3.4 headers,
  GPLv3 option). `LYNX.exe --devicetest "ASIO" "<driver>" report.txt` opens a driver, runs 1 s of silent callbacks
  and reports rate, block size and latency: "FL Studio ASIO" works (44.1 kHz, 512, 86 callbacks/s). On the test
  machine "Realtek ASIO" ends the process while loading (exit code 116) — Steinberg's own SDK host sample does the
  same, so it is the driver. In the Zig/MinGW build a driver that crashes while opening/closing is not contained
  (JUCE's `__try` guard needs MSVC; the CMake/MSVC build keeps it).

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
- Autogun's engine (state layout differs from Ogun's; DLL not run), Maximus's Master Mid mode and linear-phase
  split, Harmor's image synthesis, arpeggio envelopes and Noise resonance.
- Workflow P2 leftovers (`docs/WORKFLOW_FL.md`): per-segment automation curve shapes, clip colour/name and track
  colour/height (need model fields), F2/F3 search stepping, instrument drops on mixer inserts, the automation
  record filter, and the remaining note tools (chop, quantise, articulate, flam, period slicing, LFO).
