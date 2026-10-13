# Behavioural analysis of FL Studio 2026: round 22 (REA) — mixer *sidechain* (marking in the `.flp`, bus, Limiter/Compressor/Vocodex) and parameter numbering in the Fruity Wrapper (VST2/VST3/CLAP) + CLAP state

> **Nature of this document.** Continuation of `REA_rodada20.md`. *Clean-room* reverse engineering, behavioural
> only. No Image-Line code or pseudocode was copied: what is here are rules and behaviours described in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence. No encrypted content was
> touched (no FLEX package was opened).
>
> **Rule for this round (user request): no speculation.** Every value comes from decompilation
> (**[DECOMP]**), from the official Image-Line manual (**[DOC]**, with URL) or from the data (**[DATA]**). Where there
> was not enough evidence, the item is marked **"not confirmed"**. Inferences made *from* the code
> (e.g.: "this list is the processing order because it is sorted by depth in the graph") are made explicit.

- **Targets:**

  | Binary | SHA-256 | Language | New key functions |
  |---|---|---|---|
  | `FLEngine_x64.dll` (copy `tmp/r18/bin/FLEngine.dll`, project `/tmp/r18gh_FLE` without auto-analysis) | = round 18 | Delphi | input/output lists `FUN_012528f0`, sends `FUN_012521f0`, track iterator `FUN_012c68f0`/`FUN_012c6950`/`FUN_012c6970`/`FUN_012c6930`, host `TFruityHost` VMT `0x13141b8` slot 36 `FUN_0132baa0` and slot 37 `FUN_0132bb50`, dispatcher (snippets `0x1320f2e`, `0x1321053`, `0x1321c91`), side-input registration `FUN_012bdb60`/`FUN_01316a50`, send mixing `FUN_01277980`, side copy `FUN_01277bd0`, sequence `0x12772b2`, side table `FUN_00982ac0` |
  | Fruity Limiter | `ed81447b31db7e5085c33c039f198483af71a353c481e85272d09d48e038b082` (= round 7) | Delphi (`TTestPlug`, VMT `0x6d30c0`) | state `FUN_006d3760` (slot 3), `ProcessParam` `FUN_006d4490` (slot 6), render `FUN_006d5d60` (slot 7), side-input manager `FUN_006ce570` |
  | Fruity Compressor | `f40214923d930176d246f8d35ccdcd28adecc9812eece931a6a1e0bc670581d1` (= round 7) | Delphi (VMT `0x6bd5f8`) | state `FUN_006bd9a0` (slot 3), render `FUN_006bee30` (slot 7), manager `FUN_006bb680` |
  | Maximus | `acc7affbe4d070ce8b71c13de79f267dd1b257f4207a61a733388a6aec1abdc1` | Delphi (`TTestPlug`, VMT `0x83ee68`) | state `FUN_0083f8f0`, render `FUN_00844380` (only for the negative search) |
  | Fruity Peak Controller | `e73ec8dacb16d4db24444caa6d2d9db0a4b79246693288db4fa0972b63ec2d27` | Delphi | (only the negative search) |
  | Vocodex / Fruity Vocoder | = round 20 | Delphi | render `FUN_00856350` (Vocodex) |
  | Fruity Wrapper (generator) | `fa5a2f6fbccfc42d3382627887228e9cc8f702a676de284cf6b3eeeb0686d2e7` (= round 5; the effect one `c600a15b…2c74` did not change either) | Delphi | `ProcessParam` `FUN_00870830` (slot 6 of VMTs `0x8685d8`/`0x868840`), VST2 `FUN_007af910`, VST3 `FUN_00850390`/`FUN_0084a2c0`/`FUN_0084e0d0`/`FUN_00850dc0`/`FUN_0084e5e0`, CLAP `FUN_0085ee20`/`FUN_0085f560`/`FUN_0085d6b0`/`FUN_0085d5c0`/`FUN_0085f900`, CLAP extensions `FUN_0085dd20`/`FUN_0085ef50` |

- **Tools:** Ghidra 12.1.4 *headless*; new projects `/tmp/r22gh_Lim`, `/tmp/r22gh_Comp`, `/tmp/r22gh_Max`
  (+ copy `_Max2`), `/tmp/r22gh_Wrap` (+ copy `_Wrap2`), all with auto-analysis; FLEngine was read in project
  `/tmp/r18gh_FLE` **without** auto-analysis (functions created on demand). Scripts in `tmp/r18/gh/scripts/`,
  `tmp/r19/gh/scripts/` and `tmp/r22/gh/gv.sh`; byte searches in `tmp/r22/py/scan120.py`, `inter.py`, `riprefs.py`,
  `hostcall.py`; data with `tmp/r22/py/sc.py` (mixer events), `scuse.py`/`scsum.txt` (*sidechain* consumers)
  and `wrapauto.py` (*wrapper* parameter automation). Decompilation output in `tmp/r22/out/`. Official manual
  read via web.
- **Date:** 2026-10-13.
- **Track numbering:** count of the `0xEC` starting from Master = 0, which is the same as in `FlpImport`/`Flp.cpp`
  (checked via index 0 of the `0xD4` header of slot plugins, which carries the track number, and via the
  `controlID` `0x7…` of `0xE1`).

| Marker | Meaning |
|---|---|
| **[DECOMP]** | read in the decompiled/disassembled code |
| **[DOC]** | official Image-Line manual (URL cited) |
| **[DATA]** | checked in `.flp`/installation files |
| **not confirmed** | not enough evidence in this round |

Base of the manual URLs: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/`
(abbreviated as `…/html/` below). The manual quotes came from summarised web reading; the quoted excerpts
are short and must be rechecked on the page before becoming interface text.

---

## 1. Mixer *sidechain* **[DECOMP + DOC + DATA]**

**What LYNX assumes / needs:** today `Flp.cpp` reads `0xEB` as a list of destinations and `FlpImport.cpp` reads the
levels from `0xE1` (`p = 0x2000 + d`) and **deletes sends with level 0** ("the FL template connects every insert to
the send tracks with level 0; that makes no sound"). `FlPatcher.cpp` ignores edges to bus > 0. There is no side
input in the effects. It was still unknown how the `.flp` marks a *sidechain*, which bus the effect receives and which
native effects consume it.

### 1.1 How the `.flp` marks the *sidechain*: there is no *flag*; it is a route with send level 0 **[DATA + DOC + DECOMP]**

- **[DATA]** Across the 165 `.flp`, the `0xEB` bytes only take **0 or 1** (1,687,590 zeros, 58,266 ones): there is no
  *sidechain*-specific value or bit in the routing event.
- **[DOC `…/html/mixer.htm` (section "Internal Mixer Track Routing & Sidechaining")]** "Sidechain to this track" is
  "just a macro to creates a send link with the send volume set to 0"; "Sidechains are created when the audio Send
  knob is set to 0%"; "If the knob is set to any value other than 0% then there is BOTH an audio and sidechain
  send"; "you can't create an audio send without also creating a sidechain send"; "you don't hear the sidechain
  send on the destination Track unless some plugin on the destination Track is receiving it".
- **[DECOMP `FUN_012521f0`]** Each track's send vector (`T+0x1344`, records of `0x18` B with destination and
  gain) is built by walking destinations d = 0…501 and including **every destination whose route byte
  (`T+0x398 + 8d`, the one `0xEB` fills — round 2 §2.5) is ≠ 0**, without looking at the level.
- **[DECOMP `FUN_01277980`]** In mixing, a send whose current gain **and** whose ramp gain are both 0 is
  skipped: it adds nothing to the destination. That is: route with level 0 = *sidechain*-only link, exactly as the
  manual says.
- **Consequence for the format:** *sidechain* = byte `0xEB` = 1 for the destination **and** level `0xE1`
  (`0x70000000 + (S<<22) + 0x2000 + D`) = 0. Any route (with or without level) also counts as a
  *sidechain* source (§1.2).
- **[DATA]** Routes S→D with D ≠ Master across the 165 files: **11,857 with level 0**, 197 with level > 0 and **32,142 with no
  level record** in `0xE1`. The ones without a record appear, among others, in old files (class `0x2…`,
  < 24.2.99), whose `controlID` only encodes tracks 0…63 (`0x2000_0000…0x2FC0_xxxx`); **the value FL assumes
  when the record is missing was not confirmed**.

### 1.2 Track input list and bus index **[DECOMP FLEngine]**

- **Construction (`FUN_012528f0`):** for each track T, the engine creates two lists of track indices:
  - **inputs** (`T+0x120`) = `[T]` and, after it, each track S that has a route S→T, **in the order of the track
    iterator**;
  - **outputs** (`T+0x128`) = `[T]` and, after it, the destinations of T's send vector (increasing d, §1.1).
- **Iterator order (`FUN_012c68f0`, `FUN_012c6950`, `FUN_012c6970`, `FUN_012c6930`):** i = 0, 1, …, n − 1, with the
  last value swapped for **501** (the "Current" track). Hence the sources appear in **increasing order of track
  number, with 501 last**.
- **Read by the plugin (`TFruityHost` slot 36, `FUN_0132baa0`):** receives the plugin's *wrapper* and an index k; if
  `k < size(inputs of the plugin's track)`, returns the *buffer* of track `inputs[k]` (the consumer's private
  copy, if any, otherwise the source's main *buffer* `+0x34`, via `FUN_00982ac0`) and a state byte of the
  source (`+0x150`); otherwise returns null.
- **Host dispatcher** (snippets read in the disassembly):
  - `0x1320f2e`: for a mixer slot plugin, index 0 → returns the size of the **inputs** list; index ≠ 0 →
    size of the **outputs** list. For the others (channels): 0, or "number of tracks − 2".
  - `0x1321053`: for `1 ≤ k ≤ size − 1`, calls `FUN_01317270` with `inputs[k]` (name of the track the selector
    shows — consistent with the manual: "Available Sidechain tracks are labeled by their Track name").
  - `0x1321c91`: creates the `TRegisterSideIOAction` action (`FUN_01316a50`) only if `0 < k < size`, and it registers the
    consumer in the side table of track `inputs[k]`. `FUN_012bdb60` does the same when enumerating the inputs the
    plugin declares (query `0x37`), also requiring k > 0.
- **Result:** **index 0 = the track itself (normal input); index k ≥ 1 = the k-th track that routes to the
  plugin's track, in increasing number order (501 last)**, counting routes with any level. This also
  closes the question from round 18 §5: in the "bus k" send of slot plugins, `track_list[k]` is the
  **outputs** list = `[self, destinations in increasing order]`.
- **When the side audio is copied (`0x12772b2`):** after the sends (`FUN_01277980`), `FUN_01277bd0` copies the
  main *buffer* of the source track (`+0x34`, the same one the sends use) into the *buffer* of each registered
  consumer, passing through the input's PDC delay line (method `+0x30`; compensation "plugin.align −
  source.outLat" from round 2 §1.4); then come the extra outputs (`FUN_012777e0`). Since the send comes out of the same
  *buffer*, the side signal is at the same point in the chain as the sends (post-fader, round 2 §2.1).
- **Processing order:** `FUN_012521f0` computes for each track a depth (`+0xfc` = 1 + greatest
  depth of the tracks that route to it) and builds a global list sorted by depth (without 501).
  That this list is the order in which tracks are processed — and therefore that the side source is processed before
  the consumer in the same block — is an **inference** from usage; the loop that consumes it was not read.

### 1.3 Fruity Limiter **[DECOMP + DOC + DATA]**

- **State field:** the reader (`FUN_006d3760`) zeroes `P+0x1C0` and reads 0xA4 bytes of the *blob* into `P+0x120`
  (v ≥ 6; v5 reads 0x9D). The integer at offset **0xA4 of `0xD5`** (round 7 §2.1) lands in **`P+0x1C0`** = index k of the
  side input; in v5 it stays 0.
- **Registration with the host (`FUN_006ce570`, called at the end of loading):** asks the host for the count (dispatcher opcode
  **0x32**), clamps k to `[0, count − 1]`, unregisters the previous index and registers the new one (if ≠ 0). The number
  of the registration opcode (read from a manager field, `+0x1c`) **was not identified**.
- **Render (`FUN_006d5d60`, slot 7):**
  - the main detector (`P+0x1C4`) always runs on the track's own input;
  - the compression section only runs with **`P+0x249` set**, and `ProcessParam` (`FUN_006d4490`, parameter 8 = Comp
    threshold) sets that *flag* when **threshold < 1000** (below 0 dB); at 1000 the internal threshold is fixed and the
    section is off;
  - with the section on, the compression detector (`P+0x1CC`) receives the ***side* buffer** if `k > 0` and the host
    returns a *buffer* (slot 36 with index k); otherwise it receives the track's own input;
  - the resulting compression gain is combined per sample with the limiter: the ceiling decision (`P+0x208`) uses the
    main detector level × the compression gain, and the *gate* compares the main detector level with
    `P+0x214`. That is: **the *sidechain* only controls the compressor; the limiter and *noise gate* keep listening to the
    track input.**
- **[DOC `…/html/plugins/Fruity%20Limiter.htm`]** The SIDECHAIN selector is on the COMP tab; "Fruity Limiter's
  compression envelope will now be controlled by the sidechain Mixer track"; the THRESH maximum (0 dB) turns off the
  compressor; "Sidechain options will not be available until one or more sidechain tracks are set in the Mixer".
- **[DATA]** 343 instances with v ≥ 6; **175 with k ≠ 0** (k = 1: 146; 2: 16; 3: 10; 4: 1; 12: 2). Of these,
  **159 have Comp threshold < 1000** (the *sidechain* acts) and 16 have 1000 (the *sidechain* has no effect). One
  instance ("RawFL", track 31 "Pianos", k = 1) is on a track with no source at all → the host returns null and the
  detector uses its own input.

### 1.4 Fruity Compressor **[DECOMP + DOC + DATA]**

- **State field (`FUN_006bd9a0`):** only in **v3**, the `i32` at offset 0x20 (round 7 §5) goes to **`P+0x5D8`**;
  in v1/v2 it stays 0. The same side-input manager (`FUN_006bb680`) registers the index with the host.
- **Render (`FUN_006bee30`, slot 7):** if `k > 0` and the host returns the *buffer*, the detector uses **L and R of the side
  signal directly** (+1e−30 against denormals) instead of the track's own signal; the own signal, when it is the one
  used, first passes through a 1st-order filter (half the difference between the input and a 1st-order all-pass,
  coefficient `P+0x154`, value **not identified**). Then: absolute value, maximum of L and R, log domain,
  *attack/release* and gain curve, applied to the track's own audio.
- **[DOC `…/html/plugins/Fruity%20Compressor.htm` (section "Sidechain Input")]** "Fruity Compressor's compression
  envelope will now be controlled by the sidechain Mixer track".
- **[DATA]** 88 instances (51 v2, 35 v3, 2 v1); **none with *sidechain*** (the 35 v3 have the field = 0).

### 1.5 Maximus, Fruity Peak Controller, Fruity Vocoder, Fruity Multiband Compressor **[DECOMP (search) + DOC]**

- **Code search:** I searched, in each DLL, for the sequence "loads the host pointer at `+0xBC` and calls method
  `+0x120`" (the exact form of the call in Limiter/Compressor/Vocodex). Found in the render of the Limiter (`0x6d5f17`),
  of the Compressor (`0x6bef82`) and twice in that of Vocodex (`0x85667d`, `0x856729`); in the other DLLs only one
  occurrence appears, in the same library routine common to all (`0x57xxxx`). **It was not found in Maximus, Peak
  Controller, Fruity Vocoder or Multiband Compressor.** The decompiled Maximus render (`FUN_00844380`)
  also does not call the host to read input. This is a pattern search: "does not read side input" is its
  conclusion, not an exhaustive proof.
- **Maximus [DOC `…/html/plugins/Maximus_Tutorials.htm`]:** "Strictly speaking, Maximus does not have a sidechain
  input"; the tutorial's "sidechain" is the MID band in *MID master* mode listening to a frequency range.
- **Peak Controller [DOC `…/html/plugins/Fruity%20Peak%20Controller.htm`]:** analyses the track it is on ("Load the
  Peak Controller on the Mixer Track that has the audio signal"); the *sidechain* effect comes from linking the
  control output to parameters of other tracks (round 14 §4), and the manual refers to the Limiter for audio *sidechain*.
  Demos: 33 instances in 26 files **[DATA]**.
- **Fruity Vocoder:** no side input (L = modulator, R = carrier from its own input — round 20 §4.1).
  1 instance.

### 1.6 Vocodex **[DECOMP round 20 + §1.2 + DATA]**

- ModIn/CarIn (block `+0x170`, round 20 §4.2.1) are exactly the index k of §1.2 (the render calls host slot 36
  with that value; 0 = own input; CarIn < 0 = internal synthesizer). The rule "n-th track that routes
  to the Vocodex, in index order", which in round 20 was only an inference from the data, **is now confirmed in the
  host** (§1.2).
- **[DATA]** 12 instances in the mixer; 9 side inputs ≥ 1 in 6 files; 8 resolve to the track expected
  by name ("Mod"/"Car", "Modulador"/"Carrier", "Vocals"…); the one in the test `Render tests/Vocodex.flp` (ModIn = 1) is
  on a track with no source → falls back to its own input.

### 1.7 Usage across the 165 files (for prioritising) **[DATA `tmp/r22/py/scuse.py`, `scsum.txt`]**

**184 consumers** (175 Limiter + 9 Vocodex) in **40 files** (26 of the 46 `.flp` in `Demo projects/`, 13 templates
`Advanced`, 1 *render test*). Resolving k by the rule of §1.2, the source found has a consistent name in all
cases with a source (e.g.: k = 12 → "kick"/"Kick" in both SeamlessR projects; k = 4 → "7 Sidechain" in "Bien Duro";
templates `Advanced/*` → track 124 "SideChain"). Level of the source→consumer route: **0 in 143**, > 0 in 3, no
record in 36, no source in 2.

| File | Consumers (plugin: k) | Resolved source(s) |
|---|---|---|
| Demo songs: "Square a Saw – Lightning" | 22 Limiter k1 | 2 "kick" |
| Visual: "Treben – RawFL" | 21 Limiter k1 + Vocodex car k2 | 2 "GHOSTKICK", 16 "BASS 6" (1 with no source) |
| Visual: "Treben – All the same" | 22 Limiter k1 | 45 "SC" |
| Product: "Square a Saw – Oxygen" | 14 Limiter k1, 2 k2 | 1 "sidechain trigger", 17 "tevlo vocals" |
| "Eric Kauffmann – Exoplanet" | 12 Limiter k1 | 1 "GHOST" |
| "Electroconductor – SH-1 Rework" | 11 Limiter k1 | 1 "SC", 10 "Snare Bus" |
| "Astes – Bien Duro" | 6 k1, 1 k2, 1 k4 | 81 "7 Sidechain", 1/2 "1.1 Main" |
| "LollieVox – Madonna" | 6 k1, 1 k2 | 8 "SC" |
| "Ookay – Thief" and "NewStuff" | 4 k1, 1 k2 (each) | 1 "KICK", 24 "Main Vox" |
| "SeamlessR – Bass Antics" / "JuiceBass – Menagerie" | Limiter k1/k12 + Vocodex mod/car | "Mod"/"Car", "Modulator"/"Carrier", "kick"/"Kick" |
| "Olbaid – Harmless Essentials", "Vocodex demo" | Vocodex | 72/73, 1 "Vocals" |
| Other 12 demos (1–3 each) | Limiter k1 | "Kick", "Deep Kick", "lead vox", "Chorus – Lead", "Drum Sidechain"… |
| Templates `Advanced/Electronic/*` (9) and `Instrumental/*` (4) | Limiter k1/k2/k3 | 124 "SideChain" (Jazz: 15 "Brass") |
| `System/Render tests/Vocodex.flp` | Vocodex mod k1 | no source (falls back to its own input) |

Fruity Compressor, Maximus, Peak Controller and the *wrapper*: **0 uses of side input**. The dominant pattern is
"Limiter with compressor (threshold < 0 dB) on a track, listening to a *kick*/'SC' track linked with level 0".

---

## 2. Fruity Wrapper — parameter numbering (VST2/VST3/CLAP) and CLAP state **[DECOMP + DATA]**

**What LYNX assumes:** `FlpImport.cpp` translates `controlID` with `p ≥ 0x8000` to "plugin parameter `p − 0x8000`";
`PluginHost::adoptFlPlugins`/`convertState` only carries VST3 state (records 3/2 of round 5 §3.2) and leaves
VST2→VST3 and CLAP at default. Round 5 left open what `k` is for VST3/CLAP and the format of the CLAP state.

### 2.1 Single entry point: the *wrapper*'s `ProcessParam` (`FUN_00870830`, slot 6) **[DECOMP]**

- FL talks to the *wrapper* as to any native plugin: `ProcessParam(index, value, flags)`. The *wrapper*
  splits the index as follows:
  - **0x1000…0x107F**: becomes a MIDI CC message (`0xB0`, number = index − 0x1000, value 0…127);
  - **0x1080…0x108F**: becomes a status message `0xD0 + (index − 0x1080)` (*channel pressure* of the corresponding
    MIDI channel) with the value;
  - **0 ≤ index < `P+0x70C`** (parameter count of the hosted plugin): plugin parameter. With *flag* 0x20
    (automation path, round 2 §2.7) the integer is divided by **2³⁰** (constant `0x870aa0` = 1,073,741,824)
    → normalized value 0…1. It only writes if the plugin's *flags* method (VMT `+0x208`) says "automatable" (bit
    0) and not "read-only" (bit 1); then it calls the write method (VMT `+0x1F0`) and stores the value in its own
    table indexed by the same index.
- Therefore **the `k` of the `controlID` (`p − 0x8000`) is the index the *wrapper* receives**, and the meaning of k is given
  by the `+0x1F0`/`+0x208` method of each format (VMT table: VST2 `0x7A77C8`, VST3 `0x841B90`, CLAP
  `0x859718`). That automation from `0xE3`/`0xE1` reaches `ProcessParam` with index = `p − 0x8000` is the path of the
  universal *setter* from round 2; the exact `p → index` link in FLEngine **was not re-read in this round**.
  Whether FL also addresses indices 0x1000…0x108F via `controlID` (`p = 0x9000…`) **was not confirmed**.

### 2.2 VST2 (`TVSTPlugin`) **[DECOMP + DATA]**

- Count: `P+0x70C = AEffect.numParams` (`FUN_007ad000`).
- Write (`FUN_007af910`, VMT `+0x1F0`): calls `AEffect.setParameter(index, value)` with the **same index**;
  constant *flags* = 5 (all automatable). Names and texts come from `effGetParamName` (opcode 8, `FUN_007ac6e0`)
  and `effGetParamLabel` (opcode 6, `FUN_007ac650`), also by the same index.
- **k = VST2 index of the parameter (0…numParams − 1).** [DATA] The 2 automation links to a *wrapper* across the 165
  files ("JuiceBass – Menagerie": Fruity Fast LP k = 0, numParams = 2; Fruity Chorus k = 1, numParams = 12) are
  within range; the data cannot distinguish more than that.

### 2.3 VST3 (`TVST3Plugin`): k is a persistent "slot"; the map is in record 4 **[DECOMP]**

- Parameter list (`FUN_0084e0d0`): `IEditController::getParameterCount` (VMT `+0x40`) and
  `getParameterInfo(i)` (`+0x48`) for i = 0…n − 1, stored in `P+0x880` (records of 0x318 B = size of
  `Steinberg::Vst::ParameterInfo`; `id` at the start, `flags` at `+0x314`), count in `P+0x87C`.
- Two dictionaries: `P+0x888` **ParamID → slot** and `P+0x890` **slot → position in the list**; `P+0x70C` = number of slots.
- **Assignment (`FUN_00850dc0`):** for each existing (ParamID, slot) pair, looks up the ParamID in the current list and
  links the slot to the position; then **each ParamID in the list that does not yet have a slot gets the next free slot
  (`P+0x70C`, incremented), in `getParameterInfo` order**. In a new instance (empty dictionaries, `P+0x70C` =
  0 — zeroed in `FUN_00744640`, which by usage is the base class initialisation: inference) this gives slot = `getParameterInfo` index.
- **Save (record 4 of `FUN_0084e5e0`):** `u32 N` = number of slots + for each slot the **ParamID** (−1 if the slot
  has no parameter). **Load:** recreates the slots with those ParamIDs and calls the assignment above: saved slots
  keep their ParamID even if the plugin has reordered/added parameters; new ParamIDs go to the end; a
  ParamID that disappeared leaves the slot empty (write ignored).
- **Write (`FUN_00850390`, `+0x1F0`):** slot → position → ParamID → `FUN_0084fad0` (sends the normalized value to the
  plugin). ***Flags* (`FUN_0084a2c0`, `+0x208`):** `kCanAutomate` (1) → bit 0, `kIsReadOnly` (2) → bit 1,
  `kIsBypass` (0x10000) → bit 3.
- **k = slot**; the real ParamID is `record4[k]` (closes the open item from round 5 §4). No VST3 data in the `.flp` →
  only **[DECOMP]**.

### 2.4 CLAP (`TCLAPPlugin`): k is the index in `clap_plugin_params.get_info`, with no saved map **[DECOMP]**

- Extensions obtained via `get_extension` (`FUN_0085dd20`, `FUN_0085ef50`): `"clap.state"` → `P+0x900`,
  `"clap.params"` → `P+0x8E8` (among others: gui, note-ports, audio-ports, latency, render, thread-pool,
  timer-support, track-info, project-location, context-menu).
- List (`FUN_0085ee20`): `params.count()` and `params.get_info(i)` for i = 0…n − 1 in `P+0x950` (records of
  0x528 B = `clap_param_info`: `id`, `flags`, `cookie`, name, module, min/max/default); `P+0x70C` = n; also an
  index `id → i` (`P+0x9A8`) for changes coming from the plugin.
- **Write (`FUN_0085f560`, `+0x1F0`):** builds an event of **0x38 B type 5** (`CLAP_EVENT_PARAM_VALUE`) with
  `param_id = info[k].id`, the *cookie* and the value **`min + v·(max − min)`** (`FUN_00859330`); reading via the inverse
  path `(x − min)/(max − min)` (`FUN_00859300` over `params.get_value`).
- ***Flags* (`FUN_0085d6b0`):** `CLAP_PARAM_IS_AUTOMATABLE` (0x20) → bit 0, `IS_READONLY` (0x8) → bit 1,
  `IS_BYPASS` (0x10) → bit 3.
- **k = position of the parameter in `get_info` order at load time.** The CLAP state does **not** store a map of
  ParamIDs (§2.5): if the plugin changes the order between versions, the recorded automation points to another parameter (this is a
  direct consequence of the code, it was not tested). Behaviour after `params.rescan` **not confirmed**.

### 2.5 CLAP state (record `0x35`, `FUN_0085f900`) **[DECOMP]**

- **Save:** calls `clap_plugin_state.save(plugin, ostream)` (`P+0x900`[0], with the `ostream` whose write function
  `FUN_0085f8c0` accumulates into an in-memory *buffer*), writes **`u32 0`** and then **all the bytes**
  generated.
- **Load:** reads and **ignores** the initial `u32`, reads the rest of the record to the end (`size − position`) and calls
  `clap_plugin_state.load(plugin, istream)` (`P+0x900`[1], read `FUN_0085f8e0`) over those bytes.
- Format: **`u32 0` + raw CLAP state** (no sub-records, no size of its own — the size is that of record
  `0x35`). Closes the open item from round 5 §3.3. No CLAP data in the `.flp` → only **[DECOMP]**.

### 2.6 What the data allows verifying **[DATA]**

- The 36 *wrappers* in the 165 files are all VST2 (round 5); only 2 automation links point to them (§2.2).
  VST3 and CLAP: no instance, no automation — §2.3–§2.5 remain code-only.

---

## Summary table (item → result)

| # | Item | Result |
|---|---|---|
| 1a | Marking in the `.flp` | **there is no *flag***: `0xEB` only 0/1; *sidechain* = route (`0xEB` = 1) with level `0xE1` `p = 0x2000 + d` **= 0**; any route (with level) is also a *sidechain* source; a level-0 route does not add audio **[DECOMP+DOC+DATA]**; value assumed with no level record **not confirmed** |
| 1b | Bus seen by the effect | track input list = `[self, sources in increasing track-number order (501 last)]`; index **0 = self, k ≥ 1 = k-th source**; signal = source *buffer* at the sends point, with per-edge PDC **[DECOMP]**; outputs = `[self, increasing destinations]` (closes round 18 §5) |
| 1c | Fruity Limiter | field `0xA4` (v ≥ 6) = k; only the **compressor detector** uses the side signal, and only with Comp threshold < 1000; ceiling and *gate* use the own input **[DECOMP+DOC]**; 175 instances with k ≠ 0 (159 effective) **[DATA]** |
| 1d | Fruity Compressor | field `0x20` (v3) = k; detector = L/R of the side signal without the input filter **[DECOMP+DOC]**; 0 uses **[DATA]** |
| 1e | Vocodex | ModIn/CarIn = k from 1b (confirmed in the host); 9 inputs in 6 files **[DECOMP+DATA]** |
| 1f | Maximus, Peak Controller, Fruity Vocoder, Multiband | do not read side input (pattern search + DOC) |
| 1g | Priority | 184 consumers in 40 files; pattern "Limiter-compressor ducking by kick/SC linked with level 0" |
| 2a | Parameter index in the *wrapper* | `ProcessParam(k)`: 0…n−1 = parameter; 0x1000…0x107F = MIDI CC; 0x1080…0x108F = *channel pressure*; automation normalized by 2³⁰ **[DECOMP]** |
| 2b | VST2 | k = `setParameter` index **[DECOMP+DATA]** |
| 2c | VST3 | k = **persistent slot**; ParamID = record 4 [k]; new instance: `getParameterInfo` order; new ParamIDs at the end **[DECOMP]** |
| 2d | CLAP | k = position in `get_info`; value `min + v·(max − min)`; `PARAM_VALUE` event; no saved map **[DECOMP]** |
| 2e | CLAP state | `u32 0` (ignored on load) + bytes from `clap_plugin_state.save` up to the end of the record **[DECOMP]** |

---

## Recommendations for LYNX (only what was confirmed)

1. **Route import:**
   - Do not discard level-0 routes as "useless": they are the *sidechain* links. Store, per track, the
     **inputs** list `[self, sources in increasing order, 501 last]` built from **all** the
     `0xEB` routes (with any level). For audio it still holds: level 0 adds nothing to the destination.
   - When interpreting the "bus k" of slot plugins (Patcher, Frequency Splitter, Stereo Shaper), use the
     **outputs** list `[self, increasing destinations]`.
2. **Fruity Limiter with *sidechain* (the case that matters: 159 effective instances):** if LYNX has a side input
   in a compressor, feed **only the compression section's detector** with the post-fader *buffer* of track
   `inputs[k]` (k = `i32` at offset 0xA4 of the state, v ≥ 6), only when Comp threshold < 1000; the limiter ceiling
   and *gate* keep using the own input. Without side input in LYNX, log "Limiter with *sidechain*
   from <source track name>" so the user knows the *ducking* was lost.
3. **Fruity Compressor:** read field 0x20 only in v3; same resolution of k; in the demos it is always 0 (low priority).
4. **Vocodex:** use the same inputs list for ModIn/CarIn (the rule is no longer only empirical).
5. **PDC:** the side source→consumer edge enters the compensation as in round 2 §1.4 (delay
   `plugin.align − source.outLat`), and the source must be processed before the consumer (see the inference in §1.2).
6. **Third-party plugin automation:**
   - VST2: `k` = parameter index (JUCE exposes VST2 parameters in this same order — JUCE reference, open
     source).
   - VST3: read record 4 of the VST3 sub-container (`u32 N` + N × `i32` ParamID) and translate `k → ParamID`; then
     look up the JUCE parameter with that ID. Without record 4, `k` = `getParameterInfo` index.
   - CLAP: `k` = `get_info` index; FL's normalized value becomes `min + v·(max − min)` in the CLAP domain.
   - Indices 0x1000…0x108F (if they appear) are MIDI, not parameters.
7. **CLAP state in `convertState`:** skip the first 4 bytes of record `0x35` and hand the rest, unchanged,
   to `clap_plugin_state.load` of LYNX's CLAP host.

---

## Evidence index

| Evidence | What it shows | Marker |
|---|---|---|
| 165 `.flp`: values of the `0xEB` bytes (`tmp/r22/py/ebvals.py`) | only 0/1 | DATA |
| `0xE1` `p = 0x2000 + d` × routes (`sc.py`, `sc2.py`) | 11,857 routes level 0, 197 > 0, 32,142 with no record | DATA |
| Manual "Internal Mixer Track Routing & Sidechaining" | *sidechain* = send with volume 0; a send also creates a *sidechain* | DOC `…/html/mixer.htm` |
| FLEngine `FUN_012521f0` | send vector from the route byte, without looking at level; depth and sorted list | DECOMP |
| FLEngine `FUN_01277980` | send with gain and ramp 0 is not mixed | DECOMP |
| FLEngine `FUN_012528f0`; iterator `FUN_012c68f0`/`6950`/`6970`/`6930` | input/output lists; increasing order with 501 last | DECOMP |
| FLEngine `TFruityHost` VMT `0x13141b8` slot 36 `FUN_0132baa0`, slot 37 `FUN_0132bb50`; `FUN_00982ac0` | reading of input k; consumer's private *buffer* | DECOMP |
| FLEngine dispatcher `0x1320f2e`, `0x1321053`, `0x1321c91`; `FUN_01316a50`; `FUN_012bdb60` | count, name and registration of inputs (k > 0) | DECOMP |
| FLEngine `0x12772b2`, `FUN_01277bd0` | side copy after the sends, with PDC line | DECOMP |
| Limiter `FUN_006d3760`, `FUN_006ce570`, `FUN_006d4490` (parameter 8 → `P+0x249`), `FUN_006d5d60` (`0x6d5ee3…0x6d5f2d`) | field → `P+0x1C0`; opcode 0x32; compressor detector reads the side signal | DECOMP |
| Manual Fruity Limiter | COMP tab, compression envelope controlled by the *sidechain*, 0 dB turns it off | DOC `…/html/plugins/Fruity%20Limiter.htm` |
| Compressor `FUN_006bd9a0`, `FUN_006bb680`, `FUN_006bee30` (constants `0x6bf8c8` = 1e−30, `0x6bf8d0` = 0.5) | v3 field → `P+0x5D8`; detector uses the side signal | DECOMP |
| Manual Fruity Compressor | section "Sidechain Input" | DOC `…/html/plugins/Fruity%20Compressor.htm` |
| Search "host `+0xBC` → call `+0x120`" (`hostcall.py`) in 7 DLLs; Maximus `FUN_00844380` | only Limiter, Compressor and Vocodex read side input | DECOMP (search) |
| Maximus (tutorial) and Peak Controller manuals | Maximus has no *sidechain* input; Peak Controller analyses its own track | DOC `…/html/plugins/Maximus_Tutorials.htm`, `…/html/plugins/Fruity%20Peak%20Controller.htm` |
| Demos: Limiter `0xA4`, Compressor `0x20`, Vocodex ModIn/CarIn resolved by the rule (`scuse.py`, `scsum.txt`) | 184 consumers, 40 files, consistent names | DATA |
| Wrapper VMTs `0x8685d8`/`0x868840` slot 6 `FUN_00870830` (constant `0x870aa0` = 2³⁰) | index ranges, normalization, *flags* | DECOMP |
| Wrapper VST2 `FUN_007ad000`, `FUN_007af910`, `FUN_007ac650`, `FUN_007ac6e0`, `0x744dec` = 5 | k = `setParameter` index | DECOMP |
| Wrapper VST3 `FUN_0084e0d0`, `FUN_00850dc0`, `FUN_00850390`, `FUN_0084a2c0`, `FUN_0084e5e0` (record 4), dictionaries `FUN_00857bb0`/`FUN_00858200`/`FUN_00857ff0`/`FUN_00858080` | persistent slots, ParamID map | DECOMP |
| Wrapper CLAP `FUN_0085dd20`, `FUN_0085ef50` (strings `clap.*`), `FUN_0085ee20`, `FUN_0085f560`, `FUN_0085d6b0`, `FUN_0085d5c0`, `FUN_00859330`/`FUN_00859300` | list via `get_info`, type 5 event, min/max scaling | DECOMP |
| Wrapper CLAP `FUN_0085f900`, `FUN_0085f8c0`, `FUN_0085f8e0` | state `u32 0` + bytes from `state.save` | DECOMP |
| Demos: `0xE3`/`0xE1` for *wrappers* (`wrapauto.py`) | 2 links, VST2, k < numParams | DATA |

### Open questions

- Value FL assumes for the level of a route with no record in `0xE1` (36 of the demos' *sidechain* sources
  are in this case; for the *sidechain* it does not matter, for audio it does).
- Confirm in the processing loop that the depth-sorted list (`FUN_012521f0`) is the render order and
  how FL handles cycles/sources processed after the consumer.
- Number of the opcode with which the plugin registers the side input (manager field `+0x1c`) and the opcode ↔
  handler link in the host dispatcher (only the handlers were read).
- Fruity Compressor: value of the detector filter coefficient `P+0x154`.
- *Wrapper*: exact link `controlID p → ProcessParam index` re-read in FLEngine; whether `p = 0x9000…0x908F`
  addresses the CC/*channel pressure*; CLAP behaviour after `params.rescan`; validate §2.3–§2.5 with a
  real project with VST3 and CLAP (e.g.: Surge XT), checking record 4 and the CLAP `u32 0`.
- Side-input search by instruction pattern: an indirect call built a different way would escape it
  (Maximus, Peak Controller, Multiband were not fully decompiled).
