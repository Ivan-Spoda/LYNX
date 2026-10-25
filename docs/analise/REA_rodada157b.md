# Round 157b (mp3157): MP3 samples missing, silent or sounding terrible

2026-10-25. Bug report from a real user on v0.1.15 (Windows): (a) "projects don't open the MP3s" (projects with
.mp3 samples / audio clips load them missing or silent); (b) "when I add an MP3 by hand it doesn't open with good
quality, it sounds terrible".

Evidence markers: [CODE] read in LYNX's or JUCE's source (file named); [TEST] proven by a test in
`tests/Mp3157Tests.inc`; [RUN] measured (Linux harnesses on the decoders, or the Windows build); [FL] FL Studio 2026
observed as a black box: its own command-line renders (`FL64.exe /R /Ewav`) of projects pointing to known MP3s, no
decompilation was needed, nothing copied; [USER] her files (read-only copies on D:, never in git); [model] LYNX's own
design choice.

Corpus: her 40 MP3s (Downloads, recordings, `RODEIOS.mp3`; 44.1 and 48 kHz, CBR/VBR, LAME/Lavf/YouTube rips, with and
without Xing/LAME headers) and 26 synthetic files encoded with the vendored LAME (`/mnt/d/lynx-work/mp3157/corpus`):
CBR 128/320, VBR, ABR, no tag, 48/32 kHz, MPEG-2 22/24/16 kHz, MPEG-2.5 11/8 kHz, mono, free format 400 kbps, a 2 MB
ID3v2 picture + ID3v1, ID3v2.4 footer + APEv2, junk before the first frame, a 0.1 s file, VBR without a header, two
ID3v2 tags, a cut file, ffmpeg/Lavf CBR, VBR 48 k and without a Xing frame. References: ffmpeg 6 and mpg123 1.31.

## 1. Causes

### JUCE's MP3 decoder (both bugs)

LYNX decoded MP3 with JUCE 8's `MP3AudioFormat` (a port of an old mpg123-era decoder,
`juce_audio_formats/codecs/juce_MP3AudioFormat.cpp`), registered by `registerBasicFormats`.

- [RUN] **Wrong samples on most files.** Null test against ffmpeg (aligned): every stereo synthetic file −47…−77 dB
  with error peaks of −7…−31 dB (whole granules wrong: 3–29 "bad" 1152-sample frames per 12 s file, the audible
  clicks/"crunch"); her files: `RODEIOS.mp3` −74.6 dB (peak −28.7 dB, 7 bad frames), the 48 kHz LAME files −66…−80 dB
  (peaks −16…−28 dB, up to 102 bad frames), `(FREE) R&B Sexy Drill…` **−11.7 dB, peak +6.7 dB**: from 73 s to the end
  (2400 frames) garbage. Only a few files (mono, some YouTube rips) were clean (< −100 dB).
- [RUN] **Wrong lengths.** MPEG-2/2.5 files (22.05/24/16/11/8 kHz) decoded at **twice** their length (c08: 532224
  frames for 264600); a VBR file without a header was cut at 62% (329472 of 531072: the length was guessed from the
  first frame's bitrate); every file got the encoder delay and padding plus an extra frame (no gapless, see §3).
- [RUN] **Files not opened or decoded as noise.** Free-format streams and files with junk before the first frame
  were refused (`createReaderFor` null: the sample loads **missing**); a file with **two ID3v2 tags** (some taggers
  write them) decoded the second tag as audio: +66 dB null, +113 dB peaks (full-scale noise; a limiter or the
  clipper makes that a silent-looking or destroyed clip).

That is (b) for files where a frame goes wrong, and the most likely (a): a project's MP3 that JUCE cannot open loads
as a missing sample, one whose tag is decoded as noise or whose length is half/double sounds broken or out of place.

### (b) her report: the new channel was routed into her vocal effect bus

- [USER] Her recovered project (`recovered 2026-10-25 00-07-59.lynx`, the "RODEIOS" sample channel she added to
  TWIN SWAG): the new channel has `mixer=2`. Insert 2 there is **her vocal FX return** (H-Delay, ValhallaVintageVerb,
  Decapitator), fed by a send from Insert 1, with no channel of its own.
- [CODE] `Song::nextFreeInsert` (`src/core/Song.cpp`) picked the first insert **no channel** targets, so an
  imported FL project's send-only effect buses looked free: every sample she added by hand (browser drop, Playlist
  drop, audio editor, MIDI import...) played through delay + reverb + saturation. That is "doesn't open with good
  quality, it sounds terrible" even with a perfect decoder.

### Not the cause

- [CODE] The sampler and audio clips resample with FL's 24-point sinc (`core/FlResampler`, unchanged, tested by
  earlier rounds); 44.1 kHz on a 48 kHz device goes through it like any WAV, no MP3-specific path. Her `audio-device.xml` says ASIO
  44100; the glitch log also shows Windows Audio at 48000.
- [RUN] Paths: the Windows build loaded `II.flp`'s MP3 clip from `C:\Users\Inliw\Downloads\...` names (accents,
  spaces, `&`) with `missing_samples=0`; her MP3s exist where her projects point. No path or cache bug was found; the
  caches are re-keyed anyway (§2) so nothing decoded by JUCE survives.

## 2. Fix

1. [model] `third_party/minimp3` (CC0, upstream `ea99364f`), with **one** marked change (`minimp3.h`, "LYNX"): a
   granule whose Huffman data runs past its end is silenced. Unpatched minimp3 played such a frame as a loud burst
   (peak 1.27 at frame 2791 of "R&B Sexy Drill"); mpg123 reports "dequantization failed", ffmpeg "overread, skip"
   and conceals it. With the patch that file matches ffmpeg at −132.9 dB.
2. `core/Mp3Decoder.{h,cpp}` (no JUCE): every ID3v2 tag at the start skipped (not just one), ID3v1 / "TAG+" / APEv2 /
   Lyrics3 v2 at the end (APEv2 done by LYNX: minimp3's own skip removes 32 bytes too many, the tag size already
   counts the footer, which cut the last frame of a file with an APE tag); the Xing/Info/LAME frame detected and not decoded; frame-by-frame decoding where a frame at
   another sample rate is dropped, a damaged frame in mid-stream keeps its place as silence (the rest stays in time),
   mono↔stereo changes are converted. Two timings: `Timing::flStudio` (what LYNX loads, §3) and `Timing::gapless`
   (LAME header delay/padding removed; padding only when the stream has all its announced frames). `looksLikeMp3`
   sniffs content and never takes WAV/AIFF/FLAC/Ogg/WavPack/MIDI for MP3.
3. `engine/Mp3Format.{h,cpp}`: `lynx::Mp3AudioFormat` (".mp3 .mp2 .mpga", read-only) registered **first** by
   `registerAudioFormats` (SampleLibrary, waveform cache). `JUCE_USE_MP3AUDIOFORMAT=0` in CMake and the zig Windows
   build, so JUCE's decoder is not even compiled.
4. Caches: the streamed-sample disk cache key (files > 30 s) and the waveform cache key get `minimp3-fl-1` for MP3
   files, so nothing decoded by JUCE is reused.
5. `Song::nextFreeInsert`: an insert is used when it has effects, a send, a non-Master output, an audio input, a custom
   name, is a send target, or is linked from a Playlist track, besides being a channel's target; when every insert is
   used a new channel goes to Master (FL's default) instead of the last insert.
6. `tools/WavReader.h` (renderwav, tests) reads MP3 with the same decoder; `--export` accepts `--rate N`.

## 3. FL Studio's MP3 timing (imported projects keep FL's positions)

[FL] Measured with FL's own renders of `II.flp` variants whose audio clip / sample path was rewritten to synthetic
MP3s carrying an impulse at a known sample, compared with the gapless decode:

| File | FL lead (samples at the file's rate) | = |
|---|---|---|
| LAME CBR/VBR with Xing+LAME header, 44.1 k | 2257 | 1152 (header frame as silence) + 1105 (delay + 529) |
| no Xing frame (`RODEIOS.mp3`-like) | 1105 | delay kept, no header frame |
| 48 k LAME | 2257 @48 k (2074 in the 44.1 k render) | same |
| MPEG-2 22.05 k | 1681 | 576 (MPEG-2 header frame) + 1105 |

- FL's render = gain × (lead zeros + gapless decode), null −93 dB: FL plays the **raw** stream (delay and padding kept)
  and the Xing/Info frame as **one frame of silence**; it does not trim gapless. FL fails on free format, hangs on junk
  before the first frame, and for two ID3v2 tags falls back to a gapless decoder (lag 0); LYNX decodes all three.
- JUCE's reader dropped the header frame: tagged MP3s played 1152 samples (26 ms at 44.1 kHz) **earlier** than in FL;
  untagged files (like `RODEIOS.mp3`) had FL's timing. `Timing::flStudio` gives FL's lead exactly, so imported
  projects are fixed and old LYNX projects with untagged MP3s keep their timing; tagged ones move 26 ms to where FL
  has them (they were wrong).

## 4. Measurements (before = JUCE, after = LYNX/minimp3; null against ffmpeg, aligned)

| File | before null / peak | before length | after null / peak (gapless) | after length |
|---|---|---|---|---|
| c01 CBR 128 | −55.0 / −12.0 dB | +1872 | −134.6 / −109.6 dB | exact |
| c06 CBR 128 48 k | −53.9 / −9.5 dB | +1152 | −135.4 / −110.0 dB | exact |
| c08 MPEG-2 22 k | −50.3 / −9.6 dB | ×2 | −134.3 / −110.0 dB | exact |
| c11 MPEG-2.5 11 k | −47.5 / −9.3 dB | ×2 | −134.1 / −111.3 dB | exact |
| c15 free format 400 k | not opened | — | decodes (ffmpeg fails, no reference) | 531072 (raw) |
| c18 junk before | not opened | — | decodes (ffmpeg only reads half) | exact |
| c20 VBR, no header | −64.8 / −21.1 dB | 62% | −133.5 / −109.5 dB | exact |
| c22 two ID3v2 | **+66.6 / +113.1 dB** | +141264 | −134.6 / −109.6 dB | exact |
| `RODEIOS.mp3` | −74.6 / −28.7 dB, 7 bad frames | +3456 | −130.2 / −103.3 dB | exact |
| `(FREE) R&B Sexy Drill…` | **−11.7 / +6.7 dB**, 2400 bad frames | +4608 | −132.9 dB | exact |
| `DJ ORBITAL…` (48 k) | −66.1 / −17.1 dB, 102 bad frames | +1280 | −129.3 dB | exact |
| her 40 files | 23 with bad frames, 29 with error peaks > −60 dB | 39 longer than ffmpeg (+1 frame; tagged: delay + padding kept) | all −128…−154 dB, 0 bad frames | all exact |

(Length "exact" = gapless length equal to ffmpeg's; LYNX itself loads with FL's timing, i.e. the header frame and
delay in front.)

## 5. Windows build check

[RUN] New `LYNX.exe` (`OUT=build-win-mp3157`), `--export` of the `II.flp` variants FL rendered in §3, compared with
FL's own renders (FL renders 16-bit, hence the −82…−87 dB floor):

| Project's MP3 | lag vs FL | null vs FL | onset LYNX / FL |
|---|---|---|---|
| c01 CBR 128 (Xing+LAME) | 0 | −85.9 dB | 2284 / 2284 (v0.1.15: 1132, 1152 early) |
| c05 no tag | 0 | −82.5 dB | 1132 / 1133 |
| c06 48 k | 0 | −86.5 dB | 2096 / 2097 |
| c08 MPEG-2 22 k | 0 | −84.6 dB | 3398 / 3398 |
| c20 VBR, no header | 0 | −82.1 dB | 1114 / 1131 |
| c15 free format | — | FL renders silence | LYNX plays it |
| c22 two ID3v2 | 2257 (FL's gapless fallback) | −82.8 dB aligned | LYNX keeps the FL timing of the same stream with one tag |
| c18 junk before | — | FL hangs | LYNX: 2284, like c01 |

- Her real `II.flp` (YouTube rip, no Xing frame): `missing_samples=0`, same render as v0.1.15 to −137 dB (that file was
  one JUCE decoded cleanly); a copy in `C:\...\são é\` renders identically (−139 dB), `missing_samples=0`.
- `RODEIOS.flp` (her beat, samples from her laptop): `--rate 48000` writes a 48 kHz file.

## 6. Tests

[TEST] `tests/Mp3157Tests.inc`:
- `mp3_decoder_is_minimp3_exact_with_fl_timing_and_gapless`: 9 LAME encodes (CBR 128/320, VBR, mono, no tag, VBR no
  tag, MPEG-2 22 k, MPEG-2.5 11 k, 48 k) of a known signal. Gapless is bit-exact to minimp3's own `mp3dec_load_buf`,
  tagged files have the exact original length at lag 0, FL timing = header frame + 1105 in front with the same
  samples, untagged files lag 1105; ID3v2 ×2 / junk / APE / ID3v1 wrapping gives the same samples; free format
  decodes; a cut file decodes; junk and WAV are not taken for MP3.
- `mp3_audio_clip_from_flp_renders_with_fl_timing`: an `flp::Project` with an audio clip on a temp MP3 → `importFlp`
  → `RackRenderer` at 48 kHz: the impulse lands at (22050 + 2257) × 48000/44100 ± 2.
- `new_channel_insert_skips_effect_buses`: a send-only effect bus, a named insert and a Playlist-linked insert are
  skipped; all used → Master.

## Where I stopped

- Done: decoder swapped (JUCE's MP3 reader no longer compiled), FL timing measured and matched, caches re-keyed,
  new channels no longer routed into effect buses. build-local with the FL Data dir: 1748607 checks, 0 failures;
  Windows build OK, exports checked against FL's renders (§5).
- Her `recovered…lynx`/TWIN SWAG: the RODEIOS channel already saved on Insert 2 stays there (a saved routing is the
  project's); she has to move it to a free insert once (or delete and re-add the sample). New channels pick a free one.
- Not done: FL-exact behaviour for its own failure cases (free format → silence, two ID3v2 → gapless) is deliberately
  not copied; LYNX plays those files. MP3 seeking for streamed files is whole-file decode (an MP3 is decoded once into
  memory / the streamed-sample cache); a 10-minute 48 kHz stereo file takes ~230 MB as float until cached.
- Not checked on her laptop (read-only access, nothing installed there): she should confirm with v0.1.16 that her
  MP3s sound right and that new samples land on a clean insert.
