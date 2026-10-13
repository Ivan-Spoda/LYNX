# FL Studio 2026 behavioural analysis: round 5 (REA) — third-party plugins in the "Fruity Wrapper"

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md` and of `REA_rodada2.md` …
> `REA_rodada4.md`. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode
> was copied: what is here are format and behaviour descriptions written in our own
> words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence.

- **Targets:**
  - `Plugins/Fruity/Generators/Fruity Wrapper/Fruity Wrapper_x64.dll` (SHA-256 `fa5a2f6f…d2e7`, Delphi x64),
    Ghidra 12.1.4 via REA. It is the target of all the addresses below.
  - `Plugins/Fruity/Effects/Fruity Wrapper/Fruity Wrapper_x64.dll` (SHA-256 `c600a15b…2c74`): same code
    shifted by +0x1A0 (the `0x36/0x37/0x35` record writer appears at `0x871BB5…0x871CC5` instead of
    `0x871A15…0x871B25`). Everything that holds for the generator holds for the effect.
- **Format validation:** the 165 `.flp` files from `FL Studio 2026/Data` + the user's 2 autosaves.
  Only **36 instances** of the wrapper exist in the data (15 files: 23 in 2 "ZGameEditor Visualizer" demos,
  13 in `Data/System/Render tests`). All of them are **VST2 (effect)** of the Fruity *.dll files themselves from FL 11–13;
  **there is no VST3 or CLAP at all in the data**, so everything VST3/CLAP-specific below is only
  **[DECOMP]**. No VST3/CLAP is installed on the machine (`Common Files/VST3` and `/CLAP` do not exist).
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `W+0xNN` is an offset in the wrapper object (the plugin "host" inside the DLL, the `this` of
`FUN_00871110`); `P+0xNN` is an offset in the hosted plugin object (`W+0x5050`, of the classes
`TVSTPlugin` / `TVST3Plugin` / `TCLAPPlugin`). All integers are little-endian.

---

## 1. The FLP events **[DATA+DECOMP]**

A third-party plugin appears in the FLP exactly like a native plugin; what is "third-party" is the
*content* of `0xD5`:

| Event | Content for the wrapper |
|---|---|
| `0xC9` | Internal FL plugin name = **`"Fruity Wrapper"`** (UTF-16LE with NUL in files ≥ FL 12; ASCII + NUL in FL 11 files). It is the only event-level sign that it is a VST/VST3/CLAP/AU |
| `0xD4` | 52 B "new plugin" header (13 × u32), same as for native plugins (§1.1) |
| `0xCB` | Visible name given by the user (optional; in the data "Compressor", "Master Compressor" appear; in the render tests it is the VST name). **Absent** ⇒ the UI shows the name from record `0x36` of the blob |
| `0x80` | Colour |
| `0xD5` | **Wrapper state** (§2). Starts with `u32 version` and continues with tagged records |

Same sequence in channels (generator) and in mixer slots (effect; in the mixer the slot closes with `0x62`,
round 2). In the 36 cases the order was `0xC9, 0xD4, [0xCB], [0x9B], 0x80, 0xD5` **[DATA]**.

### 1.1 `0xD4` header (13 × u32) **[DATA; meanings HYP]**

For LYNX it is **not needed** to find the plugin. Values seen in the 36 wrappers:

| Index | Values | Likely reading |
|---|---|---|
| 0 | 0…36 (mixer) / 1 (render tests) | In mixer slots it matches the **track number** (the slot with `0xCB = "Master Compressor"` has 0 = master) **[strong HYP]** |
| 1 | 0…4 | **Slot index** in the track **[strong HYP]** |
| 2 | always 2 | ? |
| 3 | 0 | |
| 4 | 0x40 / 0x41 | **flags**; bit 0x10 distinguishes generator/effect (ANALISE §5.1) **[earlier DECOMP]** |
| 5…8 | 0, 0/5, … | ? |
| 9…12 | e.g. 64,176 / 208,320 / 16,128 | editor window position/size **[HYP]** |

---

## 2. The wrapper's `0xD5` blob **[DECOMP+DATA]**

Written by `FUN_00870c00` (the wrapper DLL's `SaveRestoreState`) + `FUN_00871110`; read by
`FUN_00870c00` + `FUN_00875aa0`.

### 2.1 Envelope

```
u32 version           FL 2026 writes 12 (0x0C). The 36 blobs in the demos have 8.
repeat until the end of 0xD5:
    u32 id
    u64 size           (unsigned; the reader always skips to payload_start + size)
    u8  payload[size]
```

- 36/36 blobs decompose **exactly** down to the last byte in this format **[DATA]**.
- Version rules at load (`FUN_00870c00`) **[DECOMP]**:
  - `version == -1` → ignored; `version ≥ 13` → rejected ("newer version" error).
  - `version < 5` → legacy fixed-layout format (`FUN_008757b0`, **not analysed**; does not appear in the data).
  - `5 ≤ version ≤ 12` → record format of this section.
- **String encoding** (records `0x36/0x37/0x38/0x3A`): **no terminator, no prefix** (the
  record size is the size in bytes). `version ≤ 8` → **ANSI** (system code page);
  `version ≥ 9` → **UTF-8** (the reader/writer goes through the conversion with code page 65001, `FUN_0041b300` /
  `FUN_0041b530` → `FUN_0041b140/0041b240`) **[DECOMP]**. The current writer always uses UTF-8.
- Unknown ids are **skipped** by the reader (it *seeks* to the end of the record after each one,
  `FUN_0074ac20`), so the list is extensible **[DECOMP]**.
- If the plugin cannot be loaded, FL **keeps the whole `0xD5`** and rewrites it byte for byte on the
  next save (`W+0xF6C`) **[DECOMP]**. LYNX should do the same (round-trip of the opaque blob).

### 2.2 Records (write order)

| Id | Size | Type | Meaning | Matters for LYNX? |
|---|---|---|---|---|
| `0x01` | 20 | struct | `i32 midiInPort` (`W+0x854`), `i32 midiOutPort` (`W+0x858`), `u8` option (written from a wrapper option; 0 is read as 2), 11 B zero. Data: `-1, -1, 0x0C` (35×) and `-1,-1,0x02` (1×) | no |
| `0x02` | 25 | struct | Wrapper options (`FUN_00874480`): `u8`, `u32`, `u32` (data: 561, 389 or 320/45…: probably editor position **[HYP]**), `u32 flags` at +9 ("Processing/Troubleshooting" bits: e.g. 0x400 = 32/64-bit bridge, 0x200000, 0x800000…), `u32` at +0x0D, `u8` at +0x11, `u32` at +0x12, `u16 flags2` at +0x16 | no |
| `0x1E` | 16 | 4 × u32 | `[0]` number of audio inputs (pins), `[1]` number of outputs, `[2..3]` = 0. Data: `1, 1, 0, 0` | optional (bus layout) |
| `0x1F` | 12·n | n × {u32 destination, u8 enabled, 3 B} | Routing of each plugin **input** (n = `[0]` of `0x1E`). Data: `{0, 1}` | no |
| `0x20` | 12·n | same | Routing of each **output** (n = `[1]` of `0x1E`; destination probably a mixer track offset **[HYP]**) | no |
| `0x32` | 16 | u32 + 12 B zero | **Format/type of the hosted plugin** (`P+0x98`), table §2.3 | **yes** |
| `0x33` | 4 | u32 | **VST2 `uniqueID`** (the `AEffect::uniqueID`, read via virtual method `P+vtbl 0x2C0`). Only VST2/FL/AU/default (not VST3/CLAP/DX) | **yes (VST2)** |
| `0x39` | 4 | u32 | Only for `TVSTPlugin`: value of the plugin's `effGetVstVersion` (opcode 58) (`P+0x9C4`). Used to convert VST2→VST3 state | no |
| `0x34` | 16 | raw GUID | **Class ID**: VST3 = class TUID/FUID; DirectX = CLSID. For VST2 it is only written if ≠ null GUID (`P+0x4B0`) | **yes (VST3)** |
| `0x3A` | var | string | **CLAP plugin id** (e.g. `com.vendor.plugin`) (`TCLAPPlugin+0x9B4`). Only types 11/12 | **yes (CLAP)** |
| `0x36` | var | string | **Plugin name** (VST2: `effGetEffectName`/bank name; VST3: derived from the **file name** without extension, not from the class name; CLAP/DX: descriptor name) | yes (fallback) |
| `0x37` | var | string | **Full path of the binary** (`.dll`, `.vst3`, `.clap`, `.component`) on the machine that saved (`P+0x2A6`) | yes (fallback) |
| `0x38` | var | string | Additional text (`P+0x3C`); only written if not empty; does not take part in the plugin lookup. Does not appear in the data **[HYP: vendor or alternative path]** | no |
| `0x35` | var | bytes | **Hosted plugin state**, produced by the plugin class's virtual method `SaveRestoreState` (VMT+0x1A8). Format depends on the type (§3) | **yes** |

Notes:
- In version 8 of the demos the order is always `01, 02, 1E, 1F, 20, 32, 33, 36, 37, 35` **[DATA]**; `0x34`
  was omitted (null GUID), `0x39` did not exist yet.
- The reader accepts any order; the type (`0x32`) and the identifiers are combined at the end
  (`FUN_00875aa0`) **[DECOMP]**.
- `0x37` is used by FL with `/` normalised (`FUN_0044acd0(…, '/')`) **[DECOMP; detail HYP]**.

### 2.3 Plugin types (`u32` of record `0x32`) **[DECOMP+STR]**

Table from `FUN_0074b090` (UI names):

| Value | Format | Role | Extension searched (`FUN_00871f90`) | Main identifier |
|---|---|---|---|---|
| 0 | VST2 | effect ("VST (fx)") | `.dll` | `0x33` uniqueID |
| 1 | DirectX | effect | `.dll` | `0x34` CLSID |
| 2 | native FL | effect | `.dll` | |
| 3 | native FL | generator | `.dll` | |
| 4 | VST2 | generator ("VSTi") | `.dll` | `0x33` uniqueID |
| 5 | DirectX | generator | `.dll` | `0x34` CLSID |
| 7 | **VST3** | effect | `.vst3` | `0x34` class ID |
| 8 | **VST3** | generator/instrument | `.vst3` | `0x34` class ID |
| 9 | AudioUnit | effect | `.component` | |
| 10 | AudioUnit | generator | `.component` | |
| 11 | **CLAP** | effect | `.clap` | `0x3A` id |
| 12 | **CLAP** | generator | `.clap` | `0x3A` id |

In the 36 blobs in the data: type 0 in all of them **[DATA]**.

### 2.4 How FL finds the plugin on the machine **[DECOMP]**

`FUN_00871f90` builds a query `{type, fileName, name, uniqueID, GUID, clapId}` and sends it to
FL (host dispatcher, opcode 7), which resolves it in the **database of scanned plugins**:

1. `fileName` = file name from `0x37` (without directory); if empty, `0x36`; plus the type's extension.
2. Tries first the architecture (32/64-bit) of the original binary and then the other one.
3. If nothing is found and there is a `uniqueID`, it walks the list of **"shell" plugins** (VST2 with several
   plugins in one `.dll`, key `Software\Image-Line\Shared\Plugins\Fruity wrapper\Shell plugins`) and takes the
   file whose name matches and which contains that ID.
4. FL **may swap VST2 ↔ VST3** of the same product: saved type 4 + found 7 ⇒ becomes 8; saved 8 + found 0
   ⇒ becomes 4. When a VST2 state goes to a VST3, the wrapper passes `uniqueID` and the `0x39` value to the
   VST3 ("There was an error setting the state of a VST2 plugin in the VST3 plugin") **[DECOMP+STR]**.
5. Failure ⇒ message "Something went wrong loading the %s plugin |%s|. Please make sure it's installed
   and registered properly." and the blob is preserved (§2.1).

In other words, **the absolute path is not the key**; the key is type + ID (uniqueID / class ID / clap id), with
the file name and the name as support.

---

## 3. The plugin state (record `0x35`)

### 3.1 VST2 (`TVSTPlugin`, VMT `0x7A77C8`, slot 0x1A8 = `FUN_007ad250`) **[DECOMP+DATA]**

Current format (version −9). In the 36 blobs it decomposes down to the last byte **[DATA]**.

```
i32  version            −9 (0xFFFFFFF7). Accepts −9…−1 and ≥0 (old formats, see below)
u8   flags              bit0 = "save all programs" (wrapper option 6)
                        bit1 = (option P+0x2E disabled)
                        bit2 = P+0x35
                        bit3 = the plugin declares effFlagsProgramChunks (0x20)
i32  markerOrSize       −2 ⇒ i64 size follows; otherwise it is the size itself (old versions wrote 0 here)
[i64 chunkSize]
if chunkSize > 0:                              -- plugin with chunk (most commercial VST2s)
    i32  currentProgram
    u8   chunk[chunkSize]                      -- result of effGetChunk(index = 0, isPreset = 0) → BANK
    (end)
else:                                           -- plugin without chunk: raw parameters
    i32  currentProgram
    i32  n                                     -- numParams, or numParams × numPrograms if bit0
    f32  values[n]                             -- 0…1, program-major order (prog 0 params 0..P−1, prog 1 …)
    i32  m                                     -- number of program names
    m × char[25]                               -- ANSI name, 24 chars + NUL, padded
```

Load details **[DECOMP]**:
- The chunk is applied with `effSetChunk` (opcode 24) **index 0 = bank**; only in version −6 files without the
  "all programs" option is it applied as a *preset* (index 1).
- The floats are applied with `setParameter(i, v)`; if `n == numParams × numPrograms` the wrapper switches
  program at each block. Afterwards, `currentProgram` is selected.
- Applying the floats happens between `effBeginSetProgram` (opcode 0x43 = 67) and `effEndSetProgram`
  (0x44 = 68).
- The magic value `123456` (0x1E240) in the version position means "nothing to restore".

Data: none of the 36 uses a chunk (they are parameter-based Fruity VSTs); `n` = 1…12, `m` = 1 **[DATA]**.

### 3.2 VST3 (`TVST3Plugin`, VMT `0x841B90`, slot 0x1A8 = `FUN_0084e5e0`) **[DECOMP]**

The `0x35` of a VST3 is **itself** a record container, with the same record header
(`u32 id, u64 size, payload`):

```
u32 version = 1        (the reader accepts 0 and 1)
records:
  id 1: 64 B  flags; bit0 of the first u32 = option P+0x2E (restored by virtual method 0x90, option 2)
  id 3: bytes  COMPONENT state  = IComponent::getState  (processor)
  id 2: bytes  CONTROLLER state = IEditController::getState
  id 4: bytes  parameter map: u32 N, then N × i32 ParamID (−1 = empty slot)
```

(write order: 1, 3, 2, 4; each only exists if the respective `getState` returned `kResultOk`.)

Restore order **[DECOMP]** (same as Steinberg's recommendation and JUCE's):
1. `IComponent::setState(id 3)`;
2. if there is a controller (and the flag `P+0x808` is not set): rewinds the stream to the start and
   `IEditController::setComponentState(id 3)`;
3. `IEditController::setState(id 2)`;
4. rebuilds FL's list of parameter "slots" from id 4.

The same trio of calls is redone in `FUN_0084c6c0` from saved copies (`P+0x920` component,
`P+0x918` controller) when the plugin is restarted.

### 3.3 CLAP (`TCLAPPlugin`, VMT `0x859718`, slot 0x1A8 = `FUN_0085f900`)

**Not decoded.** Only the method was identified. **[HYP]** it contains the raw `clap_plugin_state.save`,
possibly with a header like the VST3 one. No data to validate.

---

## 4. Automation of plugin parameters **[DATA+partial DECOMP]**

- Plugin parameter `controlID` (round 2 §2.7):
  - **channel**: `(channel << 16) | (0x8000 + k)` — 323 `0xE3` targets in the demos have this form
    (e.g. `0x00188034` = channel 0x18, parameter 0x34) **[DATA]**;
  - **mixer**: `0x70000000 + (track << 22) + (slot << 16) + 0x8000 + k` (192 targets) **[DATA]**;
    files < 24.2.99 use the old class `0x2…` (round 2).
- What `k` is for the wrapper:
  - **VST2**: the VST's parameter index (`setParameter(k, …)`) **[strong HYP]**;
  - **VST3**: FL **slot** index; the real `ParamID` is `map[k]` from record 4 of §3.2 **[DECOMP for the
    map; HYP for the link slot = k]**;
  - CLAP: not verified.
- Value: normalised by the universal setter (0…2³⁰ → 0…1 of the parameter) **[HYP, round 2]**.

---

## Recommendations for LYNX

### Parser for the wrapper's `0xD5`

```
if name(0xC9) != "Fruity Wrapper": not a wrapper.
r = Reader(blob)
version = r.u32()
if version < 5 or version > 12: keep opaque blob and exit
utf8 = (version >= 9)
info = {}
while r.remaining >= 12:
    id = r.u32(); size = r.u64()
    if size > r.remaining: stop (truncated blob) ; payload = r.bytes(size)
    case id:
      0x32: info.type     = u32(payload, 0)
      0x33: info.vst2Id   = u32(payload, 0)          # fourCC = bytes of the u32 in big-endian order
      0x34: info.classId  = payload[0:16]             # raw TUID
      0x3A: info.clapId   = str(payload, utf8)
      0x36: info.name     = str(payload, utf8)
      0x37: info.path     = str(payload, utf8)
      0x38: info.extra    = str(payload, utf8)
      0x1E: info.nIn, info.nOut = u32(payload,0), u32(payload,4)
      0x35: info.state    = payload
      others: ignore
ALWAYS keep the original blob to rewrite it if the plugin is not found.
```

`str(p, utf8)`: bytes without NUL; UTF-8 if `utf8`, otherwise Windows-1252 (ANSI) as an approximation.

### Finding the plugin (JUCE `KnownPluginList` / own scan)

1. Format by type: 0/4 → VST (VST2); 7/8 → VST3; 11/12 → CLAP; 1/5/9/10/2/3 → not supported
   (DirectX/AU/native FL) — preserve blob.
2. **VST2**: look for a `PluginDescription` with `pluginFormatName == "VST"` and `uniqueId == vst2Id`
   (in JUCE the VST2 `uniqueId` is the `AEffect::uniqueID` itself). Tie-break by the file name of
   `path` and by `name`. Shell plugins (Waves etc.) have the same `.dll` with several IDs: the ID decides.
3. **VST3**: compare `classId` with the class ID of the classes in the `.vst3`. In JUCE, compute
   `deprecatedUid = hash31(raw TUID bytes)` and compare with `PluginDescription::deprecatedUid`
   (JUCE uses `value = value·31 + byte` over the 16 bytes, `juce_VST3PluginFormat.cpp` line ~180).
   **[HYP]** the 16 bytes FL stores are the TUID in Windows memory order (COM/GUID layout), which is
   the same order as `uid->data()` in JUCE on Windows; if it does not match, also try the
   "normalised" version (`uniqueId`). Tie-break: `.vst3` file name (the VST3 `0x36` is the file name,
   not the class name).
4. **CLAP**: match by `clapId` (JUCE 8 does not host CLAP; this waits until there is a CLAP host).
5. Also accept the VST2 ↔ VST3 swap of the same product as plan B (FL does it), but without carrying the
   state (the VST2→VST3 conversion depends on plugin support).

### Restoring the state

- **VST2 with chunk** (`chunkSize > 0`): build an **opaque-chunk FXB** (`'CcnK'`, `'FBCh'`,
  version, `fxID = vst2Id`, `fxVersion`, `numPrograms`, 128 B reserved, `i32 size`, chunk; fields
  big-endian as in the VST2 SDK) and pass it to `VSTPluginFormat::loadFromFXBFile` / `setStateInformation`.
  Then `setCurrentProgram(currentProgram)` if it makes sense. (FL applies it as a **bank**, index 0.)
- **VST2 without chunk**: call `setParameter`/`AudioProcessorParameter::setValue` with the floats (already in
  0…1), per program if `n == numParams × numPrograms`, and select `currentProgram`.
- **VST3**: read the sub-container from §3.2 (`u32 version` + records) and build the XML that JUCE expects:
  `<VST3PluginState><IComponent>base64(id 3)</IComponent><IEditController>base64(id 2)</IEditController></VST3PluginState>`,
  converted with `AudioProcessor::copyXmlToBinary` and handed to `setStateInformation`. JUCE's
  application order (component → setComponentState → controller) is the same as FL's.
  Keep the id 4 map to translate automations (`k` → `ParamID`).
- **CLAP**: preserve the blob until it is decoded.

### Automation

Translate `controlID` with `p ≥ 0x8000` into "parameter `p − 0x8000` of the channel's/slot's plugin"; for VST3
go through the id 4 map. Normalised values 0…1.

---

## 5. Evidence index

| Function / data | What it does | Marker |
|---|---|---|
| `FUN_00870c00` | Wrapper `SaveRestoreState`: writes `u32 12` + records; on load dispatches by version (<5 legacy, 5…12 records, ≥13 rejected); preserves the blob if the plugin fails | DECOMP |
| `FUN_00871110` | Writer of records `01,02,1E,1F,20,32,33,39,34,3A,36,37,38,35` by type | DECOMP |
| `FUN_00875aa0` | Record reader; ANSI (≤8) / UTF-8 (≥9) rule; VST2↔VST3 swap; call to the locator | DECOMP |
| `FUN_0074a600/6f0/790/930/9c0/aa90/ac20` | Reader/writer of `u32 id + u64 size` records, strings, *seek* to the end of the record | DECOMP |
| `FUN_0041b140/0041b240` (via `0041b300/0041b530`) | Conversion with code page 65001 (UTF-8) | DECOMP |
| `FUN_0074b090` | Type table → "VST (fx)", "VSTi", "VST3 (gen)", "CLAP (fx)"… | DECOMP+STR |
| `FUN_00871f90` | Locator: extensions by type, host query (opcode 7), 32/64 architecture, shell plugin list | DECOMP |
| `FUN_00874480` | Builds record `0x02` (wrapper options/flags) | DECOMP |
| `FUN_00873070/00873100` | MIDI in/out ports of record `0x01` | DECOMP |
| `TVSTPlugin` VMT `0x7A77C8` → `FUN_007ad250` | VST2 state (version −9, chunk via `effGetChunk` opcode 23 / `effSetChunk` 24, or floats + 25 B names) | DECOMP |
| `0x7AAA2C…0x7AAA62` | `P+0x9C4` = `effGetVstVersion` (opcode 58) → record `0x39` | DECOMP |
| `TVST3Plugin` VMT `0x841B90` → `FUN_0084e5e0` | VST3 state: sub-records 1 (flags), 3 (component), 2 (controller), 4 (ParamID map) | DECOMP |
| `FUN_0084c6c0` | Reapplies component/controller (`P+0x920/0x918`) on restart | DECOMP |
| `TCLAPPlugin` VMT `0x859718` → `FUN_0085f900` | CLAP state (not read) | STR+DECOMP (identification only) |
| Strings "The component/controller state wasn't restored", "VST3 (gen)", "CLAP (fx)", "Shell plugins" | Context | STR |
| 36 `0xD5` blobs with `0xC9 = "Fruity Wrapper"` | v8 envelope, records and fixed sizes (`01`=20, `02`=25, `1E`=16, `1F/20`=12, `32`=16, `33`=4), VST2 −9 state down to the last byte | DATA |
| 323 + 192 `0xE3` targets | Plugin parameter `controlID` = `0x8000 + k` in channel and mixer | DATA |
| JUCE 8.0.4 `juce_VST3PluginFormat.cpp` (`getHashForRange`, `VST3PluginState`), `juce_VSTPluginFormat.h` (`loadFromFXBFile`) | LYNX side (open source, not Image-Line) | reference |

### Open items

- **No VST3/CLAP at all in the data**: validate §3.2/§3.3 by saving a project with a free VST3 and a free CLAP
  (e.g. Surge XT, which has both) and check: record `0x34` (TUID byte order versus
  `moduleinfo.json`), `0x3A`, VST3 sub-records 1/2/3/4.
- Legacy format (`version < 5`, `FUN_008757b0`) and CLAP state (`FUN_0085f900`).
- Exact meaning of record `0x38`, of the `0x01` byte, of the `0x02` fields and of the `0x1F/0x20` destinations.
- Confirm that the VST3 automation `k` indexes the id 4 map.
