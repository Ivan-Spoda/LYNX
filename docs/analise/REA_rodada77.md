# Behavioural analysis of FL Studio 2026: round 77 (REA) — FL's Ogg Vorbis decoding (Ogg-in-WAV samples)

> **Nature of this document.** Follow-up of round 72 §6 (DirectWave: "a stand-alone libvorbis decode of one demo
> file skipped a 1024-frame packet that oggio decodes", "only ~15 % of the samples bit-equal"). *Clean-room* reverse
> engineering, behavioural only: layouts, formulas and behaviours in our own words; addresses (`FUN_xxxxxxxx`) only
> trace the evidence. No Image-Line code or audio was copied. FL's decoder turned out to be Xiph's libvorbis (BSD),
> so LYNX now uses that open-source library itself, with one documented change (§4).
>
> **No speculation.** **[DECOMP]** decompiled/disassembled code, **[DATA]** the installation's files, **[EXEC]** the
> installation's DLL run unmodified, **[model]** our implementation, **not confirmed** where the evidence is missing.

- **Date:** 2026-10-16. Scratch directory (paths below are relative to it): `~/.claude/jobs/f8789b49/tmp/r77/`.
- **Target:** `Shared/oggio_x64.dll`, SHA-256 `ab65d34d6898c495a9aa81440f885a409cdc3d0052f4ee4f9deb637f012b1928`
  (C++/MSVC; exports `OggioLoadFile`, `OggioLoadFileW`, `?LoadOggFromStream@@…`, `OggioFreeInfo`,
  `OggioLoadFLACFromMemory`, `OggioSave*`). Its strings name **"Xiph.Org libVorbis I 20150105"** (libvorbis
  1.3.5) and **"reference libFLAC 1.3.1"**; `System/Texts/Licenses/ogg vorbis.txt` is Xiph's BSD licence.
- **Tools:** Ghidra 12.1.4 headless (private project `~/ghproj/r77`, scripts `gh/scripts/DecAt.java`; outputs
  `gh/exports.c`, `gh/dec2.c`, `gh/mdct.c`, `gh/init.c`), `objdump -M intel` (`gh/all.asm`), `fnmap.py` /
  `callgraph.py` (call sites → `.pdata` functions). (The shared REA MCP session was being switched to another round's
  binary by a parallel agent, so the decompilation ran in a private headless project.)
- **EXEC harness** `run/og.c` → `run/og.exe` (our own code, Zig cross-compile): loads `oggio_x64.dll` unmodified,
  calls `OggioLoadFile(path, info, 0)` for each file of a list and writes the planar float output or its FNV-1a 64
  hash (`run/og.0*.txt`: all 3 137 files).
- **Comparison decoders** `vorbis/ogt.cpp` (our own test code): vorbisfile (`ov_read_float`, as JUCE) and a plain
  packet loop; built against JUCE's libvorbis 1.3.7, Xiph's 1.3.5 (`src/libvorbis-1.3.5`) and a 1.3.5 copy with the
  §4 change (`src/lv135fl`); `cmpf.py`, `pages.py` (RIFF chunks and Ogg pages), `survey77.py`.

---

## 1. What FL decodes **[DATA + EXEC]**

The installation has **3 137** WAV files whose format tag is Ogg Vorbis (3 055 × `0x674F`, 82 × `0x6750`;
`oggwav.txt`), FL's factory samples, FL Keys' Grand Piano wavetable, etc. Their Ogg pages (`survey77.py`):

| Files | Ogg logical streams | |
|---|---|---|
| 2 777 | one (Vorbis) | |
| 67 | Vorbis + a second stream that starts before the first audio page | |
| 173 | Vorbis + a second stream whose first (BOS) page comes **in the middle of the Vorbis pages** | e.g. `FL Piano (11).wav`, `Close Grand 33.wav` |
| 120 | as above, and the second stream **ends (EOS page) before the Vorbis stream does** | e.g. `Grv Perc 08.wav`, `FX Blackhole.wav` |

The second stream (serial `0x10` in the piano files) carries raw 16-bit PCM after a 32-byte header; in the 120 files
its EOS page often is an empty page with serial `0xFFFFFFFF` and flags BOS+EOS. Its meaning is **not confirmed**
(it is not decoded by either decoder).

## 2. FL's decode path **[DECOMP]**

- `OggioLoadFile` (`0x1800856b0`) → `FUN_1800857a0`: unless the info's flag byte `+0x28` bit 2 is set, a path ending
  in `.flac` goes to the FLAC loader (`FUN_180083810`); otherwise the **whole file** is opened with `fopen` and
  `LoadOggFromStream` (`0x180085990`) runs. The RIFF header and trailing chunks of an Ogg-in-WAV are not parsed:
  libogg's page sync skips them as non-Ogg bytes.
- `LoadOggFromStream` decodes **twice** with `FUN_180085ba0`: once without a destination to count the frames, then
  into `channels` buffers of that many floats (it rejects streams with other than 1 or 2 channels, result 3). The
  info record (packed): `+0` sample rate, `+4` channels, `+8` frames (int), `+0xc` `float**` planar buffers, `+0x14`
  / `+0x1c` title / description strings (Vorbis comments TITLE, DESCRIPTION or COMMENT), `+0x2c` frames (int64),
  0x90 bytes; `OggioFreeInfo` frees it.
- `FUN_180085ba0` is libvorbis' **plain packet loop** (the order of libvorbis' `decoder_example.c`), with libogg's
  sync/stream functions inlined: 4096-byte `fread`s into the sync buffer; the first page found gives the stream serial
  (`ogg_stream_init`), its first packet must be the identification header (`FUN_18008abd0` =
  `vorbis_synthesis_headerin`), two more header packets follow from later pages, then `vorbis_synthesis_init`
  (`FUN_1800879b0`), and for every page: `ogg_stream_pagein` (`FUN_180029be0`, refuses pages of another serial),
  every packet → `vorbis_synthesis` (`FUN_180090a80`) → if 0, `vorbis_synthesis_blockin` (`FUN_1800881a0`) → copy
  `vorbis_synthesis_pcmout` in chunks of `4096 / channels` frames → `vorbis_synthesis_read`. A lacing value flagged
  as a hole (`0x400`) skips that packet. **The loop ends after the page whose header has the EOS flag** — checked on
  the last page returned by the sync layer, *whatever its serial*. Then the function starts over at the next page
  (a chained stream): when that page's first packet is not a Vorbis identification header, decoding ends; the frame
  count keeps accumulating across chained streams.
- vorbisfile (`ov_*`) is linked in the DLL but not on this path.

## 3. Why LYNX's decode differed

### 3.1 The missing / extra frames **[EXEC + model]**

LYNX's app decoded these files with JUCE's `OggVorbisAudioFormat`, i.e. **vorbisfile**: length `ov_pcm_total`,
samples from `ov_read_float`. In `scan1.txt` (vorbisfile vs plain loop, every file):

- **295 files**: vorbisfile returns fewer frames than its own `ov_pcm_total` — 1 024 or 2 048 frames in 119 files,
  other amounts elsewhere — exactly the 293 files with a mid-stream BOS page of the second stream (vorbisfile takes a
  BOS page of another serial as a chain boundary, clears its decoder state and so drops the samples of the block
  overlap, then resumes), plus `Piano Electric (3).wav` and `Piano Rhodes (5).wav`, where the second stream starts
  right after the Vorbis headers and vorbisfile returns **no audio at all**. JUCE's reader zero-fills what
  `ov_read_float` did not deliver, so in the app these samples were shifted by the lost block and ended in silence (or
  were silent). This is round 72's "skipped 1024-frame packet" (e.g. `OSTR C3.wav` of the "string accents" demo, and
  `Piano Rhodes (1…5).wav` of the "Rhodes" demo).
- **120 files**: FL itself stops early (the second stream's EOS page, §2), e.g. `Grv Perc 08.wav`: FL keeps
  **17 728** of the 20 691 frames the Vorbis stream holds; vorbisfile/LYNX had the longer sample.

With the plain loop including the "EOS of any serial" rule, every file has FL's frame count.

### 3.2 The ~4·10⁻⁷ value differences **[DECOMP + EXEC]**

- Xiph's libvorbis 1.3.5 and JUCE's 1.3.7, built here, decode **bit-identically** to each other (`one_135` vs
  `one_loop`); FL's output differs from both in ~87 % of the samples by up to 2.7·10⁻⁷ (`FL Piano (11).wav`).
- The MDCT code (`FUN_180098800` backward, `FUN_180099930` forward, `FUN_180098da0` butterflies, `FUN_180099410` /
  `FUN_180099700` / `FUN_180099850` 32/16/8-point butterflies) performs libvorbis' operations in libvorbis' order
  (plain scalar SSE, no reassociation): **not** the source of the difference.
- `FUN_180099d70` is libvorbis' `mdct_init`; it builds the trig table with angles formed in **single precision**:
  `cvtsi2ss k` · (`π` as float `3.1415927` / `cvtdq2ps n`) with `mulss`, then `cvtps2pd` → CRT `cos` / `sin`
  (`0x1800a7ea0` / `0x1800a8d50`) in double → `cvtsd2ss` (`gh/all.asm` `0x180099ea0…0x180099fc2`); libvorbis'
  source forms `(M_PI/n)*(4*i)` in double. The third table section multiplies by 0.5 in double as in the source; the
  forward scale is `4.0f / (float) n`. (Most likely an MSVC floating-point mode demoting the double expression; the
  build options are **not confirmed**.) A few table entries therefore round differently, and every MDCT output moves
  by an ulp or so.
- **No other difference:** libvorbis 1.3.5 with only this table change (`src/lv135fl/lib/mdct.c`) and the §2 loop
  decodes **all 3 137 files bit-identically** to FL's `oggio_x64.dll` (FNV-1a 64 of the planar float output and frame
  count, `hash_fl2.txt` vs `run/og.0*.txt`). Window shapes are libvorbis' static tables, the floor-1 dB table is
  static, residue/coupling are plain float code, so nothing else depends on the compiler.
- Not a fixed-point decoder (Tremor), not stb_vorbis: floats come straight out of libvorbis' float pipeline;
  `oggio` does no float → integer conversion (its callers do: e.g. FL Keys keeps 16-bit values `round(x·32767)`,
  round 69 §8.2).

## 4. LYNX implementation **[model]**

- `third_party/vorbis/`: Xiph's **libogg 1.3.5** (`bitwise.c`, `framing.c`, headers) and **libvorbis 1.3.7**
  (the decoder's files plus the encoder-side files `block.c` links against), unmodified except
  `src/mdct.c` → `mdct_init`, where the angles are formed as `(double)((float)k * ((float)M_PI / (float)d))`
  (marked "LYNX modification"), and a hand-written `include/ogg/config_types.h` (replaces libogg's generated one).
  BSD licences in `COPYING.libogg` / `COPYING.libvorbis`; listed in `THIRD_PARTY.md`. Compiled as C with
  `-ffp-contract=off` (no FMA contraction, so the operation order stays libvorbis').
- `src/core/OggVorbis.{h,cpp}` (our own code): `isOggWav`, `isOggStream`, `decode` — FL's §2 loop over the whole
  file, mono/stereo only, chained streams appended.
- App: `SampleLibrary` decodes Ogg-in-WAV files and `.ogg` files with it (an in-memory reader) before falling back to
  JUCE's readers, so every sampler, DirectWave zone, FL Keys wavetable, FPC pad etc. gets FL's exact samples.
- `tools/oggdecode` prints the hash / frames / channels / rate of each file (`build-local/oggdecode`); the sweep of
  all 3 137 installation files with LYNX's own build (`hash_lynx.txt`) matches FL's EXEC hashes for **3 137 / 3 137**.
- Test `ogg_vorbis_decodes_like_fl`: header detection, an invalid file, and (with the FL Data directory) four files
  against FL's hashes — a plain one, a multiplexed one, an early-EOS one and an FL Keys wavetable.

## Where I stopped / next steps

(updated below as the round progresses)
