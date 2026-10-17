# REA round 86: Edison (FL's audio editor) feature set → LYNX audio editor

Goal: give LYNX an audio editor window in the spirit of FL Studio's Edison: open it from an audio clip, a Sampler
channel, a Browser file or a mixer slot; view, select, mark and edit audio; record into it; send the result back to
the project. Clean-room: FL's form resources and strings were read only to learn *which* features and menus Edison
has; the window, the drawing and every algorithm in LYNX are LYNX's own (dark theme, red accent, LYNX's clean clip
waveform from `docs/analise/UI_STUDY_playlist_clips.md`). No Image-Line art, text blocks or code went into the repo.

## Sources

- [DATA] `Plugins/Fruity/Effects/Edison/Edison_x64.dll` (FL Studio 2026, 6 223 360 bytes). REA
  `inspect_pe_resources`: 147 resources, 57 RT_RCDATA, of which 34 are Delphi forms (`TPF0`). The forms were read with
  the round-81 binary-DFM reader (`tmp/r81/py/dfm.py`, component tree + Caption/Hint/Items only); output kept in
  `tmp/r86/dfm_all.txt`.
- [DATA] UTF-16/ANSI strings of the same DLL for the main window's button hints (the toolbar is built in code, not in a
  form): `tmp/r86/hints.txt`.

## 1. Edison's main window (what a user sees)

The main editor form is `TMEnvEditor` (the shared FL envelope/waveform editor, 683×131 design size): a paint box (the
wave view), a horizontal time scroller, a vertical level scroller and a global button strip. Edison adds a toolbar
(code-built; hints [DATA]):

| Group | Buttons (hint text, abridged) |
|---|---|
| Transport | Record (R), Play / pause (Space), Stop, Loop (L), Scrub, Mute input (I), "Slave playback to host" (H), "Scrolls to reach playback marker" (A) |
| Recording | "When / what to record": start immediately, when input is detected, when the host is playing; "Append recording to existing project"; "Maximum recording time" |
| Edit | Undo / undo history, Click-free (smooth) editing (C), Snap to grid, Step editing, Slide succeeding points/regions, Freeze editing / hide points |
| View | Display waveform / spectrum (S), view noise threshold, view regions / markers / loop, zoom out full |
| Quick tools | Fade in / declick in, Fade out / declick out, Normalize / normalize regions, Trim side noise / gate noise, Clean up (denoise), Time stretch / drum(loop) stretch, Tune loop / set loop, Add / remove marker / region, Medium auto-slicing |
| Out | Drag / copy sample / selection, Send to playlist as audio clip / to channel, Save as / save as WavPack, Run script |
| Info | Title, Tempo, Sample rate, Bit depth (press to convert), Channels (press to convert), Period snap |

## 2. Edison's main menu (`WAVOptionsMenu` in the form) [DATA]

- **File**: New, Load sample, Save sample as, Save as 24-bit WAV, Export regions (common / sampler use), Export display,
  recent.
- **Format**: Edit properties (sample properties form), precision 16-bit int / 32-bit float, Mono / Stereo.
- **Edit**: Undo backwards, Undo, Undo history, Undo mix (envelope/spectral), enable undo for large samples, Cut, Copy,
  Paste insert / replace / mix / mix (envelope) / mix (spectral) / stretch / replace (drum), Clear, Delete, Trim,
  Delete part after loop, Insert silence, Click-free editing.
- **Tools**: Envelope (an amp envelope drawn over the audio, mixed down later); Amp (Amp…, reverse polarity, normalize,
  lossy normalize, fade in/out, declick in/out); Waveform (Center = DC removal); Time (reverse, time stretch / pitch
  shift…, drum(loop) stretch, claw machine, scratch); Channels (swap, left→mono, right→mono, mono-if-mono); Synthesis
  (generate noise); Scripting; Noise gating (acquire threshold, gate, trim side noise, trim & slice); Spectral
  (convolution reverb, blur, equalize, denoise with noise profile, match spectrum, vocal denoise, deverb); Regions
  (normalize / declick all regions, perfect regions, tune loop).
- **Regions**: Add region, Add marker, Set loop, Set first downbeat, Delete, rename (all / quick / auto), assign
  trigger notes, auto-slice (dull / medium / sharp, small/medium/large grid), detect beats, detect pitch regions,
  zero-cross check, freeze all. Per-region menu: delete, clone, turn into marker, rename, loop type (normal / ping-pong),
  purpose (basic, beat, downbeat, CD track, CD index, single cycle), trigger note, select, zoom on.
- **View**: spectrum, dual view, display settings (fancy mode, colouring, channels mono/multi/left/right, spectrum
  precision 64…8192 bands, natural scale/weighting, enhanced frequency/time, draft), regions, loop, time format
  (samples / m:s:ms / bar:beat:tick / auto), scroller above, background gradient, invert grid.
- **Snap**: grid, regions, samples, zero-crossing, pitch period. **Select**: deselect, before / after selection,
  zoomed part, previous / next region. **Zoom**: in, out, out full, on selection, on left / right of selection.

## 3. Tool forms [DATA]

- `TMEAmpToolForm` "Amp": volume, left/right volume, panning (circular / triangular law), stereo separation; Reset,
  Preview, Accept.
- `TMEStretchToolForm` "Time stretcher / pitch shifter": method (Resample, élastique variants), pitch coarse/fine/mul,
  time mul / absolute length (ms), insert-or-paste-over, formant preservation.
- `TMEWAVPropForm` "Sample properties": title, file name, comments; format (16-bit int / 32-bit float), sample rate with
  a resample box and a preset list (8000 … 192000), channels (stereo, mono merged / left / right); sampler middle note,
  fine tune, key range; tempo, length in beats, tempo-sync.
- Others (not reimplemented in this round): Blur, Claw, Decimate, Denoise, Drum stretch, Env filter, EQ, Harm, Loop
  tuner, Reverb, Scale, Smooth, Vocal denoise, Deverb, MP3/OGG options, Settings ("Piano keyboard preview", "Slave
  playback to host").

## 4. What LYNX implements (round 86)

Core (`src/core/AudioEdit.{h,cpp}`, JUCE-free, tests `tests/AudioEditTests.inc`): a `Document` (audio 1-2 channels,
markers/regions, loop, title) and every edit as a function on a frame range, keeping markers, regions and the loop
attached to the audio (deleting drops markers inside and clips regions; inserting/stretching shifts or scales them):

| Edison | LYNX | Notes |
|---|---|---|
| Cut / Copy / Paste insert / replace / mix | `cut`, `copy`, `insert`, `pasteReplace`, `pasteMix` | pasted audio is converted to the document's channels and rate (`conform`) |
| Clear / Delete / Trim / Insert silence / Delete part after loop | `clear`, `erase`, `trim`, `insertSilence`, `deleteAfterLoop` | |
| Fade in / out (curve) | `fadeIn`, `fadeOut` with `Curve` linear / smooth (S) / slow start / fast start | LYNX's own curve set |
| Declick in / out | `declickIn`, `declickOut` | 4 ms smooth fade |
| Normalize | `normalize (targetDb)` | peak normalisation |
| Amp | `amp (gain, pan)` | far side unity, near side linear (LYNX's own law) |
| Reverse / Reverse polarity / Center | `reverse`, `invert`, `removeDc` | markers inside a reversed range are mirrored |
| Time stretch / pitch shift (Resample method) | `changeSpeed` | windowed-sinc (Blackman, 16 zero crossings, band-limited when shortening) |
| Time stretch / pitch shift (stretch methods) | `timeStretch`, `pitchShift` | LYNX's WSOLA (`core/TimeStretch`) — FL's élastique is not available; pitch = stretch + resample back |
| Sample properties › sample rate + Resample | `resample (rate)` | same duration and pitch, markers scaled |
| Channels: swap / left→mono / right→mono / mono | `swapChannels`, `toMono`, `toStereo` | |
| Regions: add marker / region / set loop / delete | `addMarker`, `addRegion`, `setLoop`, `removeMarkers` | |
| Snap to zero-crossing | `nearestZeroCrossing` | |
| Auto-slice (dull / medium / sharp) | `detectOnsets (sensitivity)` | own energy-flux detector; grid slicing in the UI |
| Undo / undo history | `History` (whole states, labels, memory bound) | |
| Spectrum view | `spectrogram` | own radix-2 FFT, Hann window, 0 dBFS = full-scale sine |
| Save (WAV) with regions/loop | `encodeWav` / `decodeWav` / `decodeWavMarkers` | 16/24-bit int, 32-bit float; `cue ` + `LIST/adtl` (`labl`, `ltxt`) + `smpl` |

Recording (`src/core/AudioCapture.h`): a lock-free ring the audio thread fills from one source — the sound card
input (`AudioEngine` device callback), the Master or a mixer insert (`RackRenderer`, after the fader; a silent insert
pushes silence so the timing holds). `MeterBank::capture` holds it; nothing is pushed while no source is chosen.
Start modes as Edison's: immediately, when the input is heard (−40 dBFS), when the song plays (stops with it).

Window (`src/ui/AudioEditor.{h,cpp}`, a resizable tool window, one per source):

- Toolbar: menu (File / Edit / Tools / Regions / View / Record), undo, redo, undo history, record + "what and when
  to record", play/stop (Space; the selection, the loop or from the cursor), loop, waveform/spectrogram (S), snap to
  zero crossings, zoom out full, save, Send (Playlist audio clip / new Sampler channel / replace the source channel's
  sample).
- Quick tools row: Cut, Copy, Paste, Delete, Trim, Silence, Fade in, Fade out + curve, Normalize, Amp…, Reverse,
  Invert, Center, Stretch…, Resample…, Marker, Region, Set loop. Tool windows (Amp, Time stretch / pitch shift,
  Resample, Insert silence, Normalize to) have Preview / Accept / Cancel like Edison's tool forms.
- View: ruler (time format auto / m:s / samples / bar:beat:tick) with marker flags, region bands and the loop bar
  (drag markers and loop ends), level scale (L/R, −6 dB guides), the waveform in the clean clip style (one filled
  min/max shape in a light tint of the audio colour; exact samples when zoomed in, a line and dots past one sample
  per pixel), or a spectrogram (log or linear frequency, 256…4096 bands, LYNX's own colour map: canvas → deep red
  → accent red → amber → white); overview strip; info bar (rate, channels, length, selection, peak, status).
- Mouse/keys: drag selects (Shift extends), double-click selects the region or the stretch between markers, Ctrl+wheel
  zooms, wheel scrolls; Ctrl+Z/Y/X/C/V/A/S, Del, Esc, Home/End, R, L, S, M, F, +/−. Audio files can be dropped in.
- The record button's question ("What would you like to record?", round 88) has a fourth choice, "Audio, into the
  audio editor" (FL's "Audio, into the Edison audio editor/recorder"): an empty editor records the audio input from
  the moment the song plays and stops with it; LYNX starts the song.
- Opens from: Playlist audio clip menu ("Edit in the audio editor"), Channel Rack sampler menu ("Edit the sample in
  the audio editor"), Browser file menu, mixer slot / add-slot menu ("Audio editor (record this insert)"), Edit menu
  ("Audio editor…") and the record menu ("Record into the audio editor…").
- Output: edited audio is written as a new WAV in `Music\LYNX\Edits` (a new name each time, so no cache shows stale
  audio) before it goes to the project; Save as WAV writes where the user chooses (16/24/32f) and tells the sample
  library to forget the old decode.

Not done (Edison features left out): envelope (amp/scratch envelope tools), spectral tools (blur, EQ, convolution
reverb, denoise, match spectrum, vocal denoise, deverb), noise gating, drum(loop) stretch, claw machine, loop tuner,
beat/pitch detection, trigger-note assignment, scripting, dual view, drag-out of a selection, MP3/OGG export,
"slave playback to host" (the editor plays through the preview output), unsaved-changes prompt on close.

## Where I stopped / next steps

- Windows build, snapshots and LOOK checked by the coordinator on landing; record-dialog target added.
- Left: the Edison features listed under "Not done" above.
