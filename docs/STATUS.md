# Status

Last updated: 2026-10-14. Everything below is verified by the core tests (more than a million checks), the
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
- Generators: 3x Osc, FPC, DirectWave, FL Keys, Slicex/Fruity Slicer, Wave Traveller; DrumSynth Live on its
  engine (REA round 30: all 120 patches, one per key, morph, voice modes; recorded waves read from the user's own FL
  installation; within −138 dB of the reference model verified against FL's DLLs); Harmless on its additive engine
  (REA round 32: 528-partial bank, FL's full voice; within −151 dB of the reference model, which is within −60 dB of
  FL's DLL on 135 of 139 demo channels) and its effects rack (REA round 36: chorus, delay, reverb and the Maximus
  compressor with Harmless's own presets read from the user's FL installation; the model with the rack is within −60 dB
  of the DLL on 135 of 139 demo channels); Sytrus on its engine (REA round 31: the whole instance from the raw plugin
  state — operators, FM/RM matrix, articulators, unison, portamento, filters, effects chain; FL's render settings when
  exporting, its live ones when playing); Harmor, GMS, Kepler, Ogun/Autogun, MiniSynth, DX10, Fruit Kick, Toxic Biohazard,
  Morphine, PoiZone and BassDrum on the built-in synth; Sakura and Drumpad on physical models rebuilt from their
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
- **Harmor:** cutoff in log2 Hz (73.4 Hz–17.7 kHz) with keytracking from 130.81 Hz, filter types 0–18, part
  volume `(3^x − 1)/2` (up to +12 dB), level², filter envelope centred at 0.5 (±2·(hi − lo)·amt); exact timbre
  from the Timbre 1/2 curves, blend, local EQ (1/n) and per-harmonic phase; unison with FL's per-voice detune
  table, pan and gain; resonance peak from the type's table (Classic … Lowpass, Custom shapes) and the resonance mask
  (REA round 25).
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
- **Ogun:** exact initial spectrum of the timbre engine (seeded IPP-style generator, fullness/morph/pre-decay
  weights, Forced/Impulse maps and patterns, RMS normalisation, fixed seed-3 phases) on the spectral oscillator,
  with each partial's own decay `(t − 0.025)·B·E_dec + 0.025` (the spectrum evolves during the note); cutoff `44100·(c + c₀)` Hz, FL's Q/width, 20 s release, unison ±5(11^(P15/256) − 1) cents.
- **MiniSynth:** LFO periods, squared LFO depth, release, second voice in whole semitones + 7.5 cents.
- **Toxic Biohazard:** FM 2·cell (matrix direction read from the code), filter modes (p300), cutoff `2^(9u + 5)`,
  key-track 0.09·KT oct/semitone, envelope ±9·EGAmt oct; FL's 4-pole ladder (table, resonance scale R(u), input
  saturation, LP/BP/HP outputs); FL's filter envelope (time law `max(50x⁴, 0.00101)` s, exponential stages chosen
  per 128-sample block, within 1.6·10⁻⁸ of the DLL at 44.1/48/96 kHz, REA round 33).
- **Kepler:** LFO1 rate, vibrato and cutoff depth (peak), delay, BPM sync (period n/192 beats, REA round 33);
  Chorus I / II / I+II (FL's chain); chorus noise and gate are off for every state in FL's data (as LYNX).
  **BassDrum:** Peak `1 + 100x³·s⁴`.
  **Sakura:** ADSR, Tune, Mix, Hi-Cut. **BooBass:** 3-band EQ. **Drumpad:** Pitch and Mallet Amplitude.
- **Morphine:** average spectrum of the generators on the harmonics oscillator.
- **Love Philter:** articulator base, Mod X/Y → volume (with automation/links), filter types 4/5 (width in
  octaves), 7 (all-pass) and 11 (Mango); pattern envelopes on volume (rhythmic envelope effect, in beats, looped,
  song-locked, tension curves), pan LFO (auto-pan effect), cutoff and resonance envelope/LFO as live
  modulation of the EQ (top-level live automation, or links inside the parallel container with several units);
  FL's pan law (√2·cos/sin with the far side folded in), pan-LFO tension, pulse curves (type 5) as steps.
- **Patcher:** any graph in the demos reduced to series/parallel (nested containers), cables of the right class.
- **Sidechain:** level-0 routes are sidechain links; Fruity Limiter (compression section) and Fruity
  Compressor with a side input become LYNX's compressor listening to the source track.
- **Pitcher:** FL's DSP (REA round 30, bit-exact against the reference model): integer-lag AMDF detector,
  nearest-allowed-note correction with FL's laws (speed, Fine, A reference, the note mask stored in the state, Key
  Bypass, range, Pitch Reference L/independent/R), plain 4-head shifter (9-sample latency) and Formant/Gender
  PSOLA path (257-sample latency), Harmonize and MIDI masks; latency reported to PDC.
- **Fruity Vocoder:** FL's algorithm (band edges, cascaded band-passes, envelope followers, levels).
- **Frequency Splitter:** exact Linkwitz-Riley structure for every slope (6–96 dB/oct).
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
- Harmor: the resonance becomes an SVF Q with the same peak over the pass-band — Harmor's bump sits at
  fc·2^(p60/1200) with its own width (p59) and skirts (Classic −6 dB, Lowpass dip above), which the SVF cannot
  follow; Well, Noise, self-oscillation, Hz scale and adaptive width are left out; image/blur/prism/FX; with the volume envelope off FL keeps the note
  until it is stolen — LYNX uses a short declick release.
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
- Toxic: the 4-pole ladder is FL's (REA round 26) with fc kept in Hz at any rate (FL's table is built for 44.1 kHz);
  the filter envelope moves the cutoff on LYNX's 16-sample control tick (FL: every sample, quantised to 9/65535 oct);
  the re-trigger fade of a reused voice is left out.
- Kepler: the chorus is FL's (REA round 26, within −75 dB of the DLL) with the 0.5 Hz pre-filter high-pass; "Range"
  ×10 (Exo only); a synced LFO1 is locked to the song position at note start (FL runs it from the host's beat delta);
  old (version < 4) synced states use the stored rate, as FL's engine right after loading (whether FL re-sends the
  converted rate is not confirmed).
- Harmless: the rack compressor needs the user's FL installation (its four presets are read from it; otherwise it
  stays off and the import report says so); the chorus/delay use the x87 kernels FL picks on the test machine (another
  CPU may make FL round the chorus delay instead of truncating it — not confirmed); a fresh delay starts its feedback
  ramp at the stored value (FL's host call order not observed); Maximus features unused by the presets (RMS detector,
  saturation B, other curve segment types, linear-phase mode) are not decoded; the editor's quality
  (0/1) is used for playback and export alike (FL renders with quality 2; LYNX's engine does not tell instruments
  when it renders offline); FL seeds its random draws from the clock, LYNX from a fixed seed (spectral-random
  phases, Noise resonance and the LFO "voice random" are deterministic and differ from any FL render by design);
  the Freq phaser at quality 0/1 follows FL's law but not the error pattern of its 11-bit exponential; host
  answers for the release-time factor (p30) and release velocity (p46) are 1 and 0 (FL's not observed); Mod X/Y
  (Final.FCut/FRes) are 0; FL's per-channel max polyphony is not imported (LYNX's limit is its 96-record pool);
  the LFO/phaser phases follow song-position jumps but not the start of playback.
- Ogun: the evolving spectrum is 8 snapshots per note with linear crossfades; partials binned onto one harmonic
  share an energy-weighted decay; ALT DECAY morphs every harmonic towards its Decay EQ level (REA round 26); the
  per-partial release (which restarts from the current level) is one overall envelope; the read-start phase offset is not reproduced; articulators
  are at their neutral values, Seed 2 "Random" (new spectrum per note in FL) is one fixed draw, Seed 1 per key uses
  key C5, and with richness > 9 the inharmonic partials are binned (by energy) onto harmonics of f/4.
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
- Vocodex: the Soundgoodizer stage (SG > 0, all demo instances) is LYNX's Soundgoodizer approximation (preset A,
  amount = SG, then the presets' −0.2 dB master limiter) on the engine's output — FL hosts its own Soundgoodizer
  with `Data/SG.dat`, not decoded yet (on the "Vocodex demo" inputs FL's SG_x64.dll with SG.dat peaks at −0.39 dBFS
  and sits ≈1.7 dB above the approximation, median of 100 ms windows); the
  internal Sytrus carrier (CarIn = "---", Ookay and NewStuff) is not available, so that Vocodex passes its
  modulator unchanged (`vocodex::makeInternalCarrier` is the hook for it); map segments with tension ≠ 0 are read
  as straight lines (no demo uses them).
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
- Ogun's per-partial release and ALT DECAY, Vocodex's internal carrier synth (needs LYNX's exact Sytrus) and its
  Soundgoodizer stage (SG_x64.dll not decoded), Harmor's per-partial resonance and image synthesis.
