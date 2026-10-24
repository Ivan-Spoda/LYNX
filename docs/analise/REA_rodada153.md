# REA round 153 (fltech153): what's new in FL Studio (web research) → LYNX

2026-10-24. Research of the features Image-Line added in FL Studio 21, 2024, 2025 and 2026, plus the long-standing
workflow features people rely on; each checked against LYNX's code; the top missing items implemented clean-room.

Evidence markers: [DOC] Image-Line's official manual / release notes (URLs below; read as data, behaviour only);
[CODE] checked in LYNX's source (file named); [model] LYNX's own design choice. Nothing of FL was decompiled in this
round (the manual described enough); no code, graphics, sounds or presets were copied.

Sources ([DOC]):
- N21 = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new_21.htm
- N24 = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new_24.htm
- N25 = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new_25.htm
- N26 = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_new.htm
- WN  = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/WhatsNew.htm (changelog 25.1.3 … 26.1.7)
- PRM = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_menu.htm
- CPT = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/pianoroll_chordprogression.htm
- PL  = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/playlist.htm
- CR  = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/channelrack.htm
- PAT = https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/menu_patterns.htm
- MusicRadar review of FL Studio 20 (Arrangements): https://www.musicradar.com/reviews/image-line-fl-studio-20

Legend: LYNX ✅ has it / ⚠️ partial / ❌ missing / ⛔ out of scope (online service, AI model or licence). Priority =
user value / effort: **H** high, **M** medium, **L** low. "153" = done in this round.

## 1. Feature table

### FL Studio 21 (2022-12 … 2023-10)

| Feature | What it does | LYNX | Priority |
|---|---|---|---|
| Audio clip fades and gain (N21) | fade-in/out and gain handles on audio clips, optional auto-crossfades | ⚠️ fades + gain (`Clip::fadeIn/fadeOut/gain`); no automatic crossfades | M |
| [+] add track button (N21) | adds an instrument or audio Playlist track | ✅ track mode menu, "Can't create new audio track" flow (`PlaylistComponent.cpp`) | — |
| Sequential sample drop (N21) | Shift+drop of several samples places them one after another | ❌ | L |
| Chop pattern/automation clips (N21) | clip Chop works on every clip type | ✅ "Chop to grid" (P8) | — |
| Select overlapping (N21, 24.2) | selects every layer but the lowest; "stacked" = 100 % covered | ✅ **153** (Playlist ruler menu → Select; `clipops::overlappingClips / stackedClips`) | H |
| Project folders (N21) | per-project Recorded / Rendered / Sliced folders | ⚠️ project safety folders (`core/ProjectSafety`) | L |
| Auto-save every minute, undone recordings to the bin (N21) | | ✅ autosave + backups (`ProjectSafety`) | — |
| Themes (N21) | colour themes | ❌ one LYNX theme (by design, `docs/WORKFLOW_FL.md` §4) | L |
| Browser tags and favourites (N21) | tags, starred items, boolean search, "Locate file" | ⚠️ STARRED tab, search, locate; no tags | M |
| Browser preview from the click (N21) | right/Shift+click previews from the clicked position | ✅ preview strip (`BrowserComponent::PreviewStrip`) | — |
| Mixer multi-select, "render selected tracks" (N21) | | ⚠️ selection and freeze per insert; no batch render of the selection | M |
| Automation clip → events (N21) | | ❌ | L |
| Patcherize (N21, 25.2 FX) | wraps a channel / effect chain in a Patcher | ❌ (Patcher exists, `core/Patcher.cpp`) | M |
| Snap to scale (21.1) | placed and moved notes snap to the scale | ✅ (R14, `setScaleSnap`) | — |
| Piano roll Python scripts (21.1) | user scripts that edit notes | ❌ (LYNX ships the scripts' tools natively: Euclidean, humanize, note repeat) | L |
| Coloured (spectral) waveforms (21.1) | waveform colour by frequency | ✅ (`core/WaveAnalysis`, `WaveformView`) | — |
| External MIDI clock sync (21.1) | FL follows an external clock | ❌ | M |
| Extract stems (21.2), "Remix a song" (26.1) | AI split into drums / bass / instruments / vocals | ⛔ needs a neural model (Demucs: MIT code and weights, but ~80 MB of weights and an inference runtime such as ONNX Runtime, MIT); not this round | — |
| FL Cloud sounds, mastering, distribution (21.2) | online services | ⛔ (online) | — |
| Double-click a pattern clip → Piano roll, Alt → rack (21.2) | | ✅ (P10) | — |
| Scale highlighting saved with the project (21.2) | | ⚠️ the scale is published to the engine, not stored in `.lynx` | L |

### FL Studio 2024 (24.1, 24.2)

| Feature | What it does | LYNX | Priority |
|---|---|---|---|
| Chord progression tool (24.1, CPT) | generate / analyse / rework chord progressions | ✅ (`prtools::generateProgression`, Alt+P) — LYNX's own generator, no AI | — |
| Note repeat (24.1) | repeat tool | ✅ (`NoteTool::noteRepeat`) | — |
| Make unique on drag Shift+V (24.1) | | ✅ (`EditorPolish.h`) | — |
| Browser tempo/key sync on drop (24.1) | samples with tempo metadata fit the project | ❌ (no metadata read); **Detect tempo 153** covers the manual case | M |
| Per-channel swing truncation (24.1) | | ❌ | L |
| Welcome window (24.1) | start-up quick actions | ✅ first-start wizard + new project dialog (`SetupWizard`, `NewProjectDialog`) | — |
| CLAP (24.1) | | ✅ | — |
| "Switch selected audio clips" stretch mode (24.1) | | ❌ | L |
| Ghost notes from overlapping patterns (24.2) | outlined ghosts of the clips playing at the same time | ✅ (ghost button tooltip, `PianoRollComponent`) | — |
| Snap MIDI input to scale (24.2) | | ✅ (`AudioEngine::setInputScale`) | — |
| Pattern colour without the rename dialog (24.2) | | ✅ "Change color", "Random color" | — |
| Mixer per-track low-latency monitoring (24.2) | PDC off while monitoring that track | ❌ | M |
| Select routed sources (24.2) | | ✅ "Channels routed to this track" | — |
| Tap tempo "Apply tempo" (24.2) | | ✅ (`TempoTapper`) | — |
| Metronome "Pulse" sound, SMPTE time (24.2) | | ❌ | L |
| Browser "current project" samples (24.2) | | ✅ PROJECT tab | — |
| Per-plugin window scaling (24.2) | | ❌ | L |

### FL Studio 2025 (25.1, 25.2)

| Feature | What it does | LYNX | Priority |
|---|---|---|---|
| Loop Starter (25.1/25.2) | genre-based starter from FL Cloud loops and one-shots with generated steps | ⛔ content from FL Cloud; a local variant (own folders + generated steps) is possible later | L |
| Gopher (25.1) | AI assistant | ⛔ (AI service) | — |
| Dynamic mixer tracks, up to 500 (25.1) | | ⚠️ "Insert multiple…"; 127 inserts | L |
| Unroute tracks routed to the selection (25.1) | | ✅ | — |
| Undo track deletion (25.1) | | ✅ (snapshot undo) | — |
| Audio clip properties dialog (25.1) | stretch, pitch, reverse | ✅ (`ui/ClipProperties`) | — |
| Pitch selected clips ±1, reverse (25.1) | | ⚠️ reverse; no clip-level pitch keys | L |
| AI Deverb / Declipper / Denoiser | | ⛔ (AI models) | — |
| Colourful channel controls (25.1) | rack controls tinted with the channel colour | ⚠️ name buttons are tinted (C21) | L |
| Sampler loop start/end knobs (25.2) | | ⚠️ the file's own loop is read (`SampleData::loopStart`) | L |
| Number keys mute visible channels only (25.2) | | ✅ (C9 acts on the rows shown) | — |
| Fruity Slicer 2, Emphasis, Emphasizer (25.x) | new plugins | ❌ (r154 owns FL plugins' DSP) | — |

### FL Studio 2026 (26.1)

| Feature | What it does | LYNX | Priority |
|---|---|---|---|
| Chord detection panel (26.1) | names the chord of MIDI / typing keyboard / selected notes | ✅ **153** (Piano Roll toolbar readout, `prtools::detectChord`) | H |
| Chord stamp top-down / bottom-up (26.1) | the clicked key is the chord's top note | ❌ | M |
| Custom key labels (26.1) | rename piano roll keys (drum maps) | ❌ | M |
| Stretch snaps by default (26.1) | | ⚠️ | L |
| Per-clip pan, normalise clip gain (26.1) | | ⚠️ normalise; no clip pan | M |
| Collapse / expand all groups (26.1) | | ⚠️ per group | L |
| Performance mode markers (26.1) and Performance mode (≤20) | clip launching by columns | ❌ (large) | M |
| Sort channels by type (26.1) | | ✅ | — |
| Audio Logger (26.1) | keeps recording the Master output | ❌ | M |
| FL Cloud backups (26.1) | | ⛔ (online); local backups ✅ | — |
| Browser inline waveforms (26.1) | | ❌ (preview strip only) | L |
| MIDI recording follows the tempo map (26.1) | | ✅ (recording in ticks) | — |

### Long-standing features (≤ FL 20, still current)

| Feature | What it does | LYNX | Priority |
|---|---|---|---|
| Playlist arrangements + merge (PL) | | ✅ (P20, "Arrangements and merging…") | — |
| Piano roll Tools: quantize, legato, articulate, chop, glue, arpeggiate, strum, flam, claw, riff machine, limit, flip, randomise, scale levels, LFO (PRM) | | ✅ all (`KeyMap` pianoRoll.*, `NoteToolDialogs`) | — |
| Stamp chords (PRM) | | ✅ chord combo | — |
| **Note colours** (PRM: Change colour Alt+C, Select by colour Shift+C; the colour is the MIDI channel) | 16 colour groups per note | ✅ **153** (was ❌: the `.flp` byte was read but dropped) | H |
| **Select menu** (PRM): invert Shift+I, odd, random, muted, overlapping, stacked, by colour | | ✅ **153** (was ⚠️: velocity only) | H |
| Note group / ungroup Shift+G / Alt+G (PRM) | notes move together | ❌ | M |
| Piano roll time markers (PRM) | | ❌ (Playlist markers only) | L |
| Snap: line / cell / markers (PRM) | | ⚠️ (R11) | L |
| Playlist tools and edit menu (PL) | | ✅ (P1 … P17) | — |
| Consolidate / render to audio (PL) | | ✅ "Consolidate track" | — |
| Make unique (PL) | | ✅ | — |
| **Detect tempo** (PL: audio clip Sample menu) | estimate the sample's BPM | ✅ **153** (was ❌) | H |
| Fit to tempo / time-warp (PL) | | ✅ fit; ❌ time-warp | — |
| Split by channel (PAT) | | ✅ | — |
| Track groups (PL) | | ✅ | — |
| Song markers, time signatures, marker actions (PL) | | ✅ (subset of actions) | — |
| Channel Rack: clone, sort, groups, colour, zip (CR) | | ✅ except zip / colour gradient | L |
| Burn MIDI to pattern (CR) | | ❌ | L |
| Step sequencer graph editor (CR) | | ✅ (C4) | — |

## 2. Implemented in this round

### 2.1 Note colours [DOC PRM] [CODE]
- `Note::colour` 0..15 (`core/Song.h`, `noteColourCount`). FL stores it in the low nibble of each note record's
  "extra" byte (`flp::Note::extra`, bits 0–3; 0x10 porta, 0x20 muted): the importer now keeps it
  (`FlpImport.cpp`). Saved in `.lynx` as the note array's 14th element (only when not 0; older files load with the first colour, shown as "Colour 1").
- MIDI files: a note's MIDI channel becomes its colour on import and the colour is the channel on export (as FL).
- Piano Roll: a colour swatch in the toolbar sets the colour of new notes (pencil, brush, drum brush, chord stamp);
  its menu colours the selection and selects by colour. Keys (FL's): **Alt+C** colour the selection, **Shift+C**
  select the notes of the selection's colour. Notes are drawn in their group's hue (LYNX's own 16-hue wheel;
  colour 1 keeps LYNX's note red), the velocity still sets the brightness; the velocity lane uses the same colours.
- The undo history compares colours (`core/History.cpp`).

### 2.2 Select menu [DOC PRM] [CODE]
Piano Roll "…" → Select: All, Invert (**Shift+I**), Deselect, Odd notes (every other note in time order, starting
with the first [model]), Even notes, Random (half), Muted, Overlapping (every layer except the first of an
overlapping run on the same key), Stacked (notes completely inside another note of the same key; of exact duplicates
all but the first — deleting them changes nothing audible), Same colour. Maths in `core/NoteTools.cpp`
(`invertSelection`, `selectOdd`, `selectRandom`, `selectMuted`, `selectOverlapping`, `selectStacked`,
`selectByColour`, `setColour`). Playlist ruler menu → Select: Overlapping / Stacked / Muted clips per track
(`core/ClipOps.h`).

### 2.3 Chord readout [DOC N26] [model]
A label at the right of the Piano Roll toolbar names the chord of the keys held on MIDI inputs and the typing piano
(`AudioEngine::heldInputKeys`, a lock-free 128-bit mask updated where the notes arrive, cleared by panic), or, when
fewer than two keys are held, of the selected notes. `prtools::detectChord`: up to 10 keys; exact interval
templates (major, minor, dim, aug, sus2/4, 6, m6, 7, maj7, m7, m7b5, dim7, m(maj7), 7sus4, add9, madd9, 7#5, 7b5,
maj7#5, 9, maj9, m9, 7b9, 7#9, 6/9, 9sus4, m11, 13, sevenths without the fifth, power chord); the lowest key is
preferred as the root (C-E-G-A = C6, not Am7/C), otherwise a slash chord over the bass.

### 2.4 Detect tempo [DOC PL] [model]
Audio clip menu → Detect tempo → a range (70–140, 90–180, 60–200 BPM; the range decides the octave, as FL's range
choice). `core/TempoDetect.cpp` (LYNX's own method): an onset curve from three band energies (one-pole splits at
150 Hz and 2 kHz, log-energy rises per 5 ms hop, local mean removed, ±15 ms blur), its autocorrelation with a
4-harmonic comb and a mild preference for the range's middle, then a fine search over 16 periods with fractional-lag
correlation; a loop of whole beats (≥ 4, within 0.2 %) snaps to the tempo its length gives exactly. The dialog
offers: fit the clip to the project tempo (stretch, keep pitch), use it as the project tempo, or rounded to a whole
BPM (FL 2024's "ignore decimals"). Tests: drum loops at 87 / 100 / 128 / 140 BPM within 0.05 BPM; 8-beat loops at
93 / 127 / 85.5 exact; silence → none.

## 3. Next candidates (by priority)
M: chord stamp top-down mode; custom key labels; note grouping; Audio Logger; per-clip pan; external MIDI clock;
per-track low-latency monitoring; Patcherize; browser tags; batch render of selected mixer tracks; performance mode.
⛔ stays out: FL Cloud (sounds, mastering, backups, Loop Starter content), Gopher and the AI tools; stem separation
only with a bundled open model (Demucs, MIT) — a separate decision because of the download size.

## Where I stopped
(updated at the end of the round)
