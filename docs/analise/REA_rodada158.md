# Round 158 (sf158): LYNX Soundfont, a native SoundFont player with preset search (GitHub issue #6)

2026-10-25. Issue #6: "Add native soundfont player close to the FL soundfont player or like SFLT with a search
function". LYNX had no way to play `.sf2` files; FL projects using Fruity Soundfont Player were imported as a
placeholder synth.

Evidence markers: [DECOMPILE fn@addr] FL Studio's own `Soundfont Player_x64.dll` in a headless Ghidra project (FL
Studio itself may be analysed clean-room; no FL code or graphics copied); [DATA] bytes of FL's files (form resources,
saved states, constants); [EXEC] FL Studio 2026 run on this PC (under the FL lock, closed by PID); [CODE] LYNX source;
[TEST] a test in `tests/SoundFont158Tests.inc`; [WEB] public pages.

## 1. FL's Soundfont Player: saved state (event 0xD5)

Little-endian; the loader accepts versions 2…6, FL always writes 6 [DECOMPILE SaveRestoreState@0x766320].

| Offset | Type | Field | Since |
|---|---|---|---|
| 0 | i32 | version | 2 |
| 4 | i32 × 13 | parameters P0…P12 (table below) | 2 |
| 56 | string | font path: 1 length byte (0xFF → u32 length follows), UTF-8, no terminator; FL macros (`%FLStudioFactoryData%\Data\Patches\Soundfonts\…`) | 2 |
| +0 / +4 | i32, u8 | reverb "send to" mixer target (−1 none, internal = insert − 1), built-in on | 2 |
| +5 / +9 | i32, u8 | the same for chorus | 2 |
| +10 | u8 | a flag with no use found (default 1) | 2 |
| +11, +12 | u8, u8 | "Fix unexpected levels", "Velocity filter cutoff" | 3 |
| +13 | i32 | resampling 1 Crunchy, 4 Standard, 6 HQ, 8 Extreme HQ (others: Standard) | 3 |
| +17 | i32 | an engine argument (FL resets it to 1 on load) | 3 |
| +21 | i32 | **preset index** into the font's preset list sorted by bank, program (wins over P0/P1) | 3 |
| +25 | u8 | "EMU volume envelope" | 4 |
| +26, +27 | u8, u8 | reverb / chorus LEVEL boxes; FL's loader reads both into the reverb box, chorus stays on | 5 |
| +28 | i32 | program mode 0 Automatic, 1 Instrument, 2 Drum | 6 |

Both FL samples parse to the byte (v2: 68 bytes; v5 with a 62-character path: 147) [DATA][TEST].

| P | Control | Range (default) | Meaning |
|---|---|---|---|
| 0 | patch | 1…256 (1) | program + 1; > 128: drum bank (v2) |
| 1 | bank | 0…127 (0) | |
| 2, 3 | reverb / chorus LEVEL | 0…256 (128) | send multiplier (19^(v/128) − 1) / 18: 0, ×1 at 128, ×20 at 256 |
| 4 | MOD | 0…127 (0) | mod wheel |
| 5 | A | −1…5940 (−1) | volume attack, v ms |
| 6, 8 | D, R | −1…5940 (−1) | volume decay / release, v × 4 ms |
| 7 | S | −1…127 (−1) | sustain 1000 − 1000 (v/127)^0.2 cB |
| 9 | DEL | −1…5900 (−1) | vibrato LFO delay, v × 4 ms |
| 10 | AMT | −128…127 (−128) | vibrato depth \|v\| × 1000 / 127 cents |
| 11 | SPD | −1…127 (−1) | vibrato LFO rate ≈ 0.084 v Hz |
| 12 | CUT | −1…127 (−1) | cut-off v × 12000 / 127 + 1500 absolute cents |

At its minimum a control is "Disabled": the preset's own value plays; otherwise the value replaces the SoundFont
generator in every zone [DECOMPILE ProcessParam@0x764230, GetName@0x764d80]. "Envelope 2" is the SF2 volume
envelope, "LFO 2" the vibrato (pitch) LFO. Playback [DECOMPILE TriggerVoice@0x764f30, 0x74bc30, 0x741f20]: 512-voice
pool (FL's channel polyphony caps it), banks ≥ 127 play on MIDI channel 10 (drums in Automatic mode), resampling
Crunchy = linear, Standard = 8-tap sinc, HQ = 32-tap, Extreme HQ = 128-tap, EMU envelope scales initial attenuation
by 0.4, "Fix unexpected levels" ignores the level factors of linked stereo samples. The plugin has no MIDI input: FL
handles note-off and the pitch bend range. Editor [DATA form resource, EXEC screenshot]: 370 × 385 px, font button,
patch line `[0000:00]: name`, REVERB / CHORUS (send to, LEVEL box + knob, BUILT-IN), ENVELOPE 2 (A D S R sliders),
LFO 2 (DEL AMT SPD), MISC (CUT MOD), an options menu (resampling, program mode, velocity filter cut-off, EMU volume
envelope, fix unexpected levels). FL's manual describes the same controls.

## 2. SFLT

[WEB] SFLT is a free SoundFont player by Ash Taylor (estrobiologist): VST3 / CLAP and a native FL Studio build, in
beta; a file and preset browser with sorting and filtering, SF3 from v0.9, a multi mode. It was **not** downloaded,
run or analysed: only its public description shaped the browser's ideas (one search over every font's presets,
click to hear, sort by relevance). No name, graphic or text of it is used.

## 3. What LYNX does now

- **Engine** [CODE src/core/SoundFont*.cpp]: TinySoundFont 0.9 (MIT, `third_party/tinysoundfont`, modified to keep the
  reverb / chorus send generators) only parses and merges the preset / instrument zones; LYNX checks the RIFF hydra
  itself first (every index in range, damaged files rejected) and plays the voices with its own engine: SF2 volume and
  modulation envelopes, vibrato and modulation LFOs, resonant low-pass, loops (continuous / until release), key and
  velocity ranges, exclusive classes (4 ms choke), root key / tuning / scale tuning, attenuation and pan, sample-rate
  independent pitch. SF3 (Ogg Vorbis samples) through stb_vorbis (MIT). Samples kept as 16-bit (half the memory).
- **Real time**: fonts load on a background thread into a shared, cached library (keyed by path, size and date);
  the audio thread only reads an atomic pointer; settings arrive through a triple buffer; notes through a fixed
  queue. No allocation or lock in `render` [TEST soundfont158_realtime_render_does_not_allocate]. Offline export waits
  for the font.
- **Controls** (all automatable, MIDI-learnable): FL's overrides (A D S R, LFO DEL / AMT / SPD, CUT, with "Preset" at
  the minimum) with the same units, plus envelope-to-cut-off and resonance; reverb / chorus level (FL's multiplier
  curve), LEVEL box and built-in switch; volume, pitch bend range, polyphony (1…256, oldest voice stolen), transpose;
  resampling (linear, cubic, 8- and 16-tap windowed sinc), program mode (Automatic / Instrument / Drums one-shot),
  velocity curve, velocity → cut-off, EMU envelope, loops on / off. MIDI: sustain pedal, mod wheel, pitch bend + RPN 0,
  volume / expression / pan, CC 91 / 93 sends, bank select + program change, channel pressure, all notes / sound off.
- **Editor** (`src/ui/SoundFontEditor.cpp`): font button, bank:patch selector with previous / next, a description
  line, the sections above, and the preset browser: "This SoundFont" (bank:program, name, category) and "Search all"
  (every preset of every font in the folders, ranked: whole word in the name first, then prefix / substring,
  category, font name). Click previews (another font: rendered off the audio thread and played like a Browser
  sample; this font: the channel takes the preset and plays a short note), double-click loads, right-click menu,
  drag onto the Channel Rack = a new channel (or onto a LYNX Soundfont channel: load there).
- **Index**: FL's `Data\Patches\Soundfonts`, FL's user data folder, Documents, Music, Desktop, Downloads, the
  Browser's folders and the user's own list (`soundfont-folders.txt`, the Folders… menu) are scanned on a background
  thread (only the header chunks are read); the result is cached in `soundfont-index.txt` and re-read only for files
  whose size or date changed.
- **Everywhere else**: Channel Rack › add menu "LYNX Soundfont"; `.sf2` / `.sf3` dropped on the rack (a new channel,
  or into the LYNX Soundfont channel under the mouse); the Browser lists SoundFonts (click: preview the first preset,
  double-click / Enter: a new channel or into the selected one); Quick add (Ctrl+…) lists every indexed preset.
- **FL import**: "Fruity Soundfont Player" / "SoundFont Player" channels become LYNX Soundfont channels with the
  font path (FL macros expanded; Missing files relocates a moved font), the preset (v3+: index into the sorted list,
  else P0/P1), every override in FL's units, sends, resampling, program mode and options [TEST
  soundfont158_fl_state_import]. FL's own `.fst` states (plugin database, the two SoundFont tracks) read [DATA][TEST].

## 4. Not done / differs from FL

- "Send to" a mixer insert is read and kept but not played (LYNX's built-in reverb / chorus play instead when on).
- FL's built-in reverb / chorus were not analysed: LYNX uses its own (Freeverb-style, two-line chorus).
- FL's MOD knob (P4) is not imported (the mod wheel CC works live).
- Resampling qualities are LYNX's (Standard = cubic; FL: 8-tap sinc), "Fix unexpected levels" is stored only.
- No sound comparison with FL renders was made (no FL project with Soundfont Player was found among the test
  projects); the mapping comes from the decompiled code.
- FL automation clips of the Soundfont Player's parameters are not mapped to LYNX's parameters.

## 5. Tests

`tests/SoundFont158Tests.inc`: a tiny SF2 written by the test (2 key zones, velocity layers, loops, exclusive
classes, drum bank) — parsing, damaged / truncated files, note on / off and loops, velocity layers and presets,
exclusive classes + sustain pedal + polyphony, pitch bend and sample rates, real-time render without allocation,
settings round trip, the preset search index (scan, cache, ranking), the FL state and `.flp` import. FL's factory
fonts (`Default.sf2`, `STR_Ensemble.sf2`) load and play. `--opentest` opens the editor for the default channel, a
saved / reloaded one and one with NaN / huge / truncated settings.

## Where I stopped

See the end of the round's report (results filled in when landed).
