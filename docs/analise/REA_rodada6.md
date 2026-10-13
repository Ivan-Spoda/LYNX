# FL Studio 2026 behavioural analysis: round 6 (REA) — mixer EQ, channel LFO, 3x Osc AM, portamento and markers

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md` and of `REA_rodada2.md` …
> `REA_rodada5.md`. *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode
> has been copied: what is here are descriptions of format and behaviour written in
> our own words. Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence.

- **Targets:**
  - `FL Studio 2026/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA + full `objdump`
    of the same binary. It is the target of all addresses in §1, §2, §4 and §5.
  - `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll` (SHA-256 `5d84d5ad…dff2`), §3.
- **Format validation:** the 165 `.flp` files in `FL Studio 2026/Data` (1,186 mixer tracks with `0xE1`,
  17,215 `0xDA` records, 3,443 `0xDD` events, 314 `0x94` markers).
- **Date:** 2026-10-13

| Mark | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation/disassembly of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `T+0xNN` is an offset into a mixer track record (global array `PTR_DAT_015e06a0`,
stride `0x1478` per track, as in round 2); `B+0xNN` is an offset inside an EQ **band**
(`B = T+0x1D4 + band·0x88`); `C+0xNN` is the channel object; `V+0xNN` the channel voice (same convention as
rounds 3/4). "Tick" is the sequencer tick (project PPQ).

---

## 1. 3-band EQ built into every mixer track

### 1.1 Control IDs in `0xE1` **[DECOMP+DATA]**

The universal setter (`FUN_01046a70`) handles the block `p = 0x1FD0 … 0x1FE7` (24 ids) of the mixer class
`0x70000000 + (track << 22) + (slot << 16) + p`:

- `idx = p − 0x1FD0`; **band = `idx & 7`**, **quantity = `idx >> 3`** (0 = gain, 1 = frequency,
  2 = width). The code reserves 8 bands per quantity, but only 3 exist (the DSP loop goes from 0 to 2).
- The `slot` field is **ignored** for track parameters (the setter only uses `id & 0xFFFF` and the track). In the
  demos it is always **0** (not 0x3F) in the 1,186 × 12 records of `0x1FC0…0x1FE2` **[DATA]**.
- Raw value stored in `B+0x00` (gain), `B+0x04` (freq), `B+0x08` (width); every write calls the
  recalculation `FUN_0127b390(track, band)` under the mixer critical section.

| `p` | Band | Quantity | Range | Default | Seen in demos |
|---|---|---|---|---|---|
| `0x1FD0/1/2` | 0/1/2 | gain | **−1800…+1800** (`FUN_0104ef04`: text-edit limits) | 0 | −1474…+703 |
| `0x1FD8/9/A` | 0/1/2 | frequency | 0…65536 | 5777 / 33145 / 55825 | 4729…61849 |
| `0x1FE0/1/2` | 0/1/2 | width | 0…65536 | 17500 | 0…65536 |
| `0x1FC2` | — | stereo separation | ±64 | 0 | −64…+64 |

### 1.2 Conversions (`FUN_0127b390`) **[DECOMP]**

The derived fields live at `B+0x10…` as floats (Ghidra shows them as `int`, but they are `float`):

```
gain_dB   = v_gain · 0.01                                   → B+0x14   (±18 dB)
freq_Hz   = 10 + 159.9 · (101^(v_freq/65536) − 1)          → B+0x18   (10 Hz … 16 kHz)
bw        = v_width / 65536                                 → B+0x1C   (0 … 1)
active    = −1 if gain ≠ 0; otherwise = tail counter        → B+0x0C
```

- The base is **101** (constant `0x4059400000000000` passed to `ln`), not 100 as round 2
  §2.1 said. With 101 the defaults land on round numbers: 5777 → **90.3 Hz**, 33145 → **1500 Hz**,
  55825 → **8000 Hz**, 65536 → 16000 Hz **[DECOMP+DATA]**.
- The UI hint shows gain as `v·0.01` "dB", frequency in "Hz" and width as `v/65536` with 2
  decimals (`FUN_01046a70`, branch `0x1FD0`) **[DECOMP]**.

### 1.3 Shape of each band **[DECOMP]**

At track creation (`0x1287061…0x12870bc`) each band receives a **fixed type** in `B+0x10`
(the type is not saved in the FLP; it is always this one):

| Band | Type | Shape |
|---|---|---|
| 0 | 6 | **Low shelf** |
| 1 | 7 | **Peaking (bell)** |
| 2 | 8 | **High shelf** |

The coefficient computation (`FUN_0069d0e0` + `FUN_0069cb10`) is exactly the **RBJ "Audio EQ Cookbook"**,
with `w = 2π·f/fs` (the frequency is first limited to a maximum dependent on `fs`, `ctx+0x30`, **[HYP:
≈ Nyquist]**), `A = 10^(dB/40)`:

| Type | Width parameter | Formula |
|---|---|---|
| 6, 8 (shelves) | **slope** `S = 1.9·bw + 0.1` (0.1…2.0; default 0.607) | `2√A·α = sin w · √((A²+1)/S − (A−1)²)`, cookbook shelf coefficients |
| 7 (peak) | **Q = 0.0999·(1001^bw − 1) + 0.1** (0.1…100; default **0.632**) | `α = sin w/(2Q)`; `b0 = (1+αA)/(1+α/A)`, `b2 = (1−αA)/(1+α/A)`, `a1 = b1 = −2cos w/(1+α/A)`, `a2 = (1−α/A)/(1+α/A)` |
| 1…5 (not used by the mixer) | `Q = 18.9·bw + 0.1` | cookbook LP, BP (0 dB peak), notch, HP and all-pass |

The **larger** the width value, the **narrower** the bell (Q grows exponentially) and the
steeper the shelf.

### 1.4 Processing (`FUN_01276d00` → filter `FUN_0069d500`) **[DECOMP]**

- **Direct Form I biquad in `double`**, interleaved stereo, *in-place* on the track's `float` buffer.
  Per-band state: 8 doubles (`x[n−1]`, `x[n−2]`, `y[n−1]`, `y[n−2]` for L and R) at `B+0x20`;
  coefficients `b0, b1, b2, a1, a2` (a0 normalised) at `B+0x60…+0x78`. At the end of each block the state
  goes through a denormal *flush*.
- The bands run in series in the order **0 → 1 → 2** (low shelf, peak, high shelf).
- **Per-band bypass:** a band with gain 0 is not processed. When the gain is zeroed it keeps running for
  a "tail time" (counter `B+0x0C` = global `PTR_DAT_015e32e8`, the same one used for the track's tail *hold*;
  round 2 says `SR/5` **[HYP]**) and then the state is cleared. With all 3 bands at 0 dB the EQ
  costs nothing and does not alter the signal.
- There is no coefficient smoothing: changing a parameter recalculates the coefficients immediately.
- **Position in the chain** (confirming round 2 §2.1): the EQ is called in `FUN_01276ee0` **after** the 10
  effect slots (each with dry/wet) and the "post-FX" recording tap (`FUN_01277550`), and **before** the
  "post-EQ" tap (`FUN_012775a0`), the fader/pan/separation (`FUN_01276e40`) and the "post-fader" tap
  (`FUN_012775f0`).

### 1.5 Stereo separation (`0x1FC2`)

Already described in round 2 §2.2 **[DECOMP]**: `sep > 0` narrows with `s = (5^(sep/64) − 1)/4`; `sep < 0`
widens with `s = sep/64`; applied in the fader's 2×2 matrix, after the EQ.

---

## 2. Channel LFO (LFO fields of the `0xDA` records)

Complements round 3 §1. Fields of the 68 B record: `+0x00` flags, `+0x24` delay, `+0x28` attack
(fade-in), `+0x2C` amount, `+0x30` speed, `+0x34` shape.

### 2.1 Speed → period (`FUN_009ce9e0` + `FUN_009ce950` + `FUN_006614a0`) **[DECOMP+DATA]**

```
period_ticks = max( round((1000^(v/65536) − 1) · E), 3 )       (0 if v < 101, then becomes 3)
increment    = floor(2³² / period_ticks)                        (32-bit phase per tick)
E = 0.128·PPQ                (flag 0x02 on: tempo-synced)
E = 0.128·PPQ · BPM/120      (flag 0x02 off: absolute time)
```

Therefore:

- **Free (TEMPO off):** `T = 0.064 s · (1000^(v/65536) − 1)`, independent of BPM.
- **Synced (flag `0x02`):** `T = 0.128 beat · (1000^(v/65536) − 1)`.
- The synced values in the demos land on exact divisions, which confirms the scale **[DATA]**:
  10272 → 0.250 beat (1/16), 6393 → 0.123 (≈ 1/32), 15087 → 0.500, 20643 → 1.000, 26664 → 2.000,
  32950 (default) → 4.000 beats = 1 bar.
- The period is quantised to whole ticks and the LFO is sampled **once per tick** (resolution of
  ~5 ms at 96 PPQ / 120 BPM). Minimum of 3 ticks per cycle.
- LFO delay and attack use the same time curve (and the same flag `0x02`): delay in ticks; linear
  fade-in with an increment of `1/(ticks+1)` per tick.

### 2.2 Waveforms (`FUN_009cdfc0`, table `0x014255c0`) **[DECOMP]**

`shape` (0…2, *clamped*) indexes an array of 3 pointers to tables of 16384 floats. Order
**confirmed**: 

| `shape` | Table | Shape (phase 0 → 1) |
|---|---|---|
| 0 | `0x015f9860` | **Sine** `sin(2π·φ)`; starts at 0 rising |
| 1 | `0x01609860` | **Triangle** aligned with the sine: 0 → +1 (¼) → 0 (½) → −1 (¾) → 0 |
| 2 | `0x01619860` | **Pulse/square**: +1 in the 1st half, −1 in the 2nd |

Table index = `phase >> 18` (no interpolation). In the demos shape 0 dominates (46 of 48 active LFOs;
2 use shape 2) **[DATA]**.

### 2.3 Phase: per note or global (`FUN_009cedb0`, `FUN_009ce490`, `FUN_009ceec0`, `FUN_009cef00`) **[DECOMP]**

- **Default (flag `0x20` off): restarts per note.** On voice trigger: phase = 0, fade = 0, delay
  counter = delay ticks. On each tick: while the delay has not run out, it only decrements (output 0); then
  `phase += increment`, `l = table[phase>>18]`, and the fade-in rises by `1/(n+1)` up to 1, multiplying `l`.
  Note that the first sample already has the phase advanced by one step.
- **Global (flag `0x20`, "Global" button):** the phase comes from an accumulator of the **channel**, not of the voice:
  - on each playback tick all global LFOs advance by `increment` (`FUN_009ceec0`);
  - on position jumps (`FUN_009cef00`, called by the transport) the phase is **recalculated** as
    `position_in_ticks · increment (mod 2³²)`, i.e. the global LFO is **locked to the song position**
    (phase 0 at tick 0).
  - Delay and fade-in remain **per voice** even in global mode.
- In the demos: 27 synced LFOs vs 21 free; 12 global vs 36 per note **[DATA]**.

### 2.4 Application per destination (`FUN_00d24500`, round 3 §1.6) **[DECOMP]**

`l` = LFO output (±1, already multiplied by the fade-in), `a` = raw amount from the record:

| Destination (record) | Range of `a` | Effect |
|---|---|---|
| 0 Pan | ±64 | `pan += 2·l·a/128` (= `l·a/64`, ±1 = full pan) |
| 1 Volume | ±128 | `gain = env · (1 + l·a/128)`, floor 0 (a=128 → tremolo 0…2×) |
| 2 Cutoff (Mod X) | ±128 | `modX += l·a/128` (added to the voice's Mod X, scale −1…+1 = full range) |
| 3 Resonance (Mod Y) | ±128 | `modY += l·a/128` |
| 4 Pitch | ±2400 | `pitch += l·a` **cents** (±2 octaves) |

The resulting gain is linearly interpolated per sample between ticks (round 3 §1.4), which is why the
tremolo does not "step"; pitch/pan/filter change per tick.

---

## 3. 3x Osc: AM mode and shapes 1/4

### 3.1 AM: exact formula **[DECOMP]**

- `FUN_007389d0`: AM on stores in `P+0x1F4` the index `2` (table `{−1, 2}`): **only osc 3** can be a
  modulator. AM off = −1 (none).
- Voice render (`FUN_0073a230`): the 3 oscillators are rendered **in order 1, 2, 3** into the same buffer.
  The first audible oscillator **writes** (mode 11 → `FUN_00739dc0`/`00739ce0`), the following ones
  **add** (mode 10 → `FUN_00739d70`/`00739ca0`), and osc 3 with AM uses mode 12
  (`FUN_00739e10`/`00739d20`), which does per sample:

```
acc = acc − acc · (g3 · o3)          ⇔      output = (g1·o1 + g2·o2) · (1 − g3·o3)
```

- `g3 = mix3/128` and, with AM on, `rest = 1` (round 4 §2.2): `g2 = mix2/128`, `g1 = 1 − g2`. Hence
  osc 3 **adds** nothing to the signal; it is an **amplitude modulator with depth `g3`**:
  `g3 = 1` → gain varies from 0 to 2 (wave `o3` of ±1); `g3 = 0.25` → 0.75…1.25.
- "Invert osc 3" flips the sign of `g3` (`1 + |g3|·o3`), i.e. it inverts the phase of the modulation.
- If `g3 = 0` osc 3 only advances its phase: AM has no effect.
- Stereo: the same operation runs for the R channel (with osc 3's own detune/SPO).
- There are two sets of routines identical in semantics (SSE and x87), chosen by CPU; the x87 one computes the
  same `acc − acc·(g·o)`.

### 3.2 Shapes 1 and 4 **[HYP — unresolved]**

3x Osc reads the waveform tables from the **host** (`host+0x18 + shape·8`). The pointer arrays I found in
FLEngine (`0x014255c0`, `0x014255d8`, `0x01425600`) belong to the LFO/automation, not to the plugin host
(orders `sin,tri,sqr` / `sin,sqr,tri,saw,X` / `sin,sqr`); the array the host hands to plugins is
filled at runtime and was not located. It stays as in round 4: 1 = triangle, 4 = rounded
saw **[HYP]**, consistent with the data (2 = square, 3 = saw, 5 = noise).

---

## 4. Polyphony, mono and portamento: `0xDD` event + the note's "porta" bit

### 4.1 Layout of `0xDD` (9 B → `C+0x1EC`) **[DECOMP+STR+DATA]**

| Off | Type | Field | Notes |
|---|---|---|---|
| 0 | i32 | **max voices** (`C+0x1EC`) | 0 = unlimited; control `MaxPolySelect` (0…99) |
| 4 | i32 | **slide/porta time** (`C+0x1F0`, channel parameter `p = 6`) | knob `PolySlideWheel`: **0…1660, default 820** (FL 2026); old files store 500 |
| 8 | u8 | **flags** (`C+0x1F4`) | **bit 0 = Mono** (`MonophonicBox`), **bit 1 = Porta** (`GlideBox`) |

- The bits come from `FUN_00f6f240` (the shared `OnClick` of both checkboxes): `flags = mono | porta·2`;
  the field offsets `0x9D8`/`0x9D0` were matched to the names via the form's published field table
  **[DECOMP+STR]**.
- In the demos **[DATA]**: `(max, flags)` = (0,0) 1905×, (1,0) 1108×, (16,0) 272×, (64,0) 57×,
  (0,1) 42×, (0,3) 31×, (32,0) 13×, (0,2) 3×…; time 500 (3273×), 820 (87×), 760, 0, 324, 967…

### 4.2 Porta time conversion (`FUN_00fea220` → `FUN_0117bce0` → `FUN_0117bbc0` + `FUN_01312200`) **[DECOMP+STR]**

```
n          = round( (27^(v/1024) − 1) · 48/26 )      (unit: 1/48 of a step = 1/192 of a beat)
ticks      = n · (PPQ/4) / 48     (integer division)  → C+0x6E4
time       = n/192 beats          (scales with BPM)
```

| `v` | n | Time |
|---|---|---|
| 0 | 0 | no glide |
| 500 (old default) | 7 | 0.036 beat (≈ 18 ms at 120 BPM) |
| 820 (current default) | 24 | **½ step = 1/8 beat** |
| 1024 | 48 | 1 step |
| 1660 (max.) | 384 | **8 steps = 2 beats** |

The UI hint shows the value as `steps:ticks` (`FUN_011f2cb0`), which confirms the unit. The same curve
`27^(v/1024)` is used for other channel times (`C+0x2B8`, `C+0x2C4`, `C+0x21C`).

### 4.3 When a note glides (`FUN_00d237e0`) **[DECOMP]**

There is a glide when **all** of the conditions hold:

1. `(channel Porta flag) ≠ (note bit 0x10)` — the note's "porta" bit (byte 19 of the note record,
   round 2) **inverts** the channel's mode (string "Inverted porta"): channel without Porta + marked note = glide;
   channel with Porta + marked note = **no** glide;
2. there has already been a previous note in the same **group** (the low 4 bits of byte 19 = colour/MIDI channel 0…15;
   slot `C+0x398 + group·0x34`, sentinel `0x7FFFFFFF` = "no note yet");
3. `porta_ticks ≥ 1`;
4. the auxiliary value returned by `FUN_00d28910` for the note (stored in `V+0x1C8`) is 0 **[HYP: "slide
   note"/generated-note marker]** and the channel's echo/arpeggio block (`C+0x200`) is zeroed **[block
   name HYP]**.

### 4.4 How the glide moves (`FUN_00d23db0`, `FUN_00d26460`, `FUN_00d26430`, step at `0xd2ab90`) **[DECOMP]**

- The new voice **starts with the values of the last note triggered in the group** and goes **linearly** to
  its own values over `porta_ticks + 1` ticks: on each tick it adds `(target − start)/(ticks+1)`.
- The glide applies to the **5 per-note parameters** together: pan, velocity/volume, **pitch** (in
  cents ⇒ linear in semitones), Mod X and Mod Y.
- "Last note" is the last one **triggered** in the group, even if it has already ended (the slot is only updated
  on trigger). When there is no glide, the new note just writes its values into the slot.
- Advancement is per tick (~5 ms steps); the old voice is **not** cut by the glide itself — that depends
  on Mono mode.

### 4.5 Mono and voice limit (`FUN_00d293d0`, `FUN_00d2bd90`, `FUN_00d236d0`) **[DECOMP; mono details HYP]**

- **Mono off and max ≠ 0** (and channel is not of type 2): before triggering, it counts the channel's live
  voices; if `live ≥ max`, it releases the `live − max + 1` oldest ones (note-off/kill `FUN_00d243c0`).
- **Mono on** (bit 0): the limiter above does not run; the voice enters a list per **group** (colour/MIDI
  channel). If a voice already exists in the group, the new one is marked with state `−10` and the channel is notified
  (`FUN_00feaf70`) — mono *legato*/note-switch behaviour **[HYP for the detail]**. Notes with
  flags `0x90000` (internal) do not enter mono.
- **"Full porta"** (`PortaNoGateBox`, `C+0x2C2`, round 3 §4.2 `0x3E`): normally, if `C+0x2C4 < 1447`,
  the note's duration is limited to `C+0x6D8` ticks (same curve `27^(v/1024)`); with "Full porta" on,
  notes with bit `0x10` are **not** subject to that limit (`FUN_00d293d0`) **[DECOMP; limit name HYP]**.

---

## 5. Time markers: event `0x94` (`type << 24 | position`) **[STR+DATA]**

Two parallel arrays of 13 strings at `0x015d5068` (script name / short name), indexed by
type **[STR]**:

| Type | Internal name | Short name | In demos | Behaviour in FL |
|---|---|---|---|---|
| 0 | `normal` | Marker | 210 | simple marker (navigation) |
| 1 | `marker loop` | Loop | 1 | when reached, jumps back to the previous marker (repeats the section) **[HYP]** |
| 2 | `marker skip` | Skip | 5 | when reached, jumps to the next marker **[HYP]** |
| 3 | `marker pause` | Pause | 2 | pauses playback at the marker **[HYP]** |
| 4 | `loop` | Loop | 13 | start of the song loop region (paired with 11) **[HYP]** |
| 5 | `start` | Start | 13 | song start position **[HYP]** |
| 6 | `pattern length` | Length | 1 | changes the pattern/bar length from here on **[HYP]** |
| 7 | `unused` | Unused | 0 | reserved |
| 8 | `time signature` | TimeSig | 68 | time signature change (uses `0x21`/`0x22`) **[DATA]** |
| 9 | `start recording` | StartRec | 0 | *punch in* **[STR: "punch in"]** |
| 10 | `stop recording` | StopRec | 0 | *punch out* **[STR: "punch out"]** |
| 11 | `loop length` | LoopLen | 1 | loop end/length (paired with 4) **[HYP]** |
| 12 | `Key/Scale` | Key | 0 | key/scale change (FL 2025+) |

Events following each `0x94` **[DATA]**: `0x21` (numerator, e.g. 4), `0x22` (denominator, e.g. 4) —
stored for **all** types, but only meaningful for type 8 — and then `0xCD` (UTF-16 name, optional)
and/or `0x2E`. The position (low 24 bits) is in project PPQ ticks.

---

## Recommendations for LYNX

**Mixer EQ**
1. Importer: for each track read `p = 0x1FD0+b` (gain), `0x1FD8+b` (freq), `0x1FE0+b` (width),
   b = 0…2, ignoring the `slot` field. Convert with `dB = v/100`, `Hz = 10 + 159.9·(101^(v/65536) − 1)`,
   `bw = v/65536`.
2. Implement 3 RBJ biquads in series: low shelf (`S = 1.9·bw + 0.1`), peak
   (`Q = 0.0999·(1001^bw − 1) + 0.1`), high shelf (`S = 1.9·bw + 0.1`), `A = 10^(dB/40)`. Use `double`
   state. Skip the band when the gain is 0 (with a short tail before clearing the state). If the LYNX EQ
   is parametrised by Q on the shelves, convert S → Q via the cookbook relation
   `1/Q = √((A + 1/A)(1/S − 1) + 2)`.
3. Position: after the effect slots and before volume/pan/separation. Expose "post-EQ" as a
   tap point if LYNX has per-track recording.
4. Fix the frequency formula from round 2 (base 101, not 100).

**Channel LFO**
5. Importer: `period_s = 0.064·(1000^(v/65536) − 1)` (free) or `period_beats = 0.128·(...)`
   (`flags & 2`). Shape 0/1/2 = sine/triangle/pulse, all starting at phase 0 (sine and triangle
   rising from 0, pulse at +1). Delay and fade-in with the same time curve.
6. By default restart the phase on each note; with `flags & 0x20` derive the phase from the song position
   (`phase = ticks·inc`). Apply per destination according to §2.4 (volume multiplies, the others add).

**3x Osc**
7. With AM on: `out = (g1·o1 + g2·o2)·(1 − g3·o3)` with `g1 + g2 = 1`; osc 3 does not enter the sum.

**Portamento / mono**
8. `0xDD` importer: `maxVoices`, `portaBeats = round((27^(v/1024) − 1)·48/26)/192`,
   `mono = flags & 1`, `porta = flags & 2`.
9. Glide: decide per note with `channel_porta XOR note bit 0x10`; start from the values of the last note
   triggered in the same colour/MIDI-channel group and interpolate **linearly** (pitch in cents, and also
   pan/vel/ModX/ModY) over `portaBeats`. It can be per sample in LYNX (FL does it per tick).
10. Mono: one voice per group; without mono and with max > 0, steal the oldest voices.

**Markers**
11. Import types 0, 4/11 (loop), 5 (start) and 8 (time signature, with `0x21/0x22`); keep the
    others as markers with the type preserved for *round-trip*.

---

## 6. Open questions

1. Waveform tables handed by the host to native plugins (real order of 3x Osc shapes 1 and 4).
2. Exact value and semantics of the EQ "tail time" (`PTR_DAT_015e32e8`) and of the frequency ceiling
   (`ctx+0x30`) in the biquad computation.
3. Mono details (state `−10`, `FUN_00feaf70`): whether the old voice is cut or "retargeted"
   (legato), and when the glide's "no previous note" sentinel is reset (start of play? pattern
   change?).
4. Fine semantics of markers 1/2/3/6/11 (only confirmed by name).
5. `0x2E` following the markers.

---

## 7. Evidence index

| Function / data | What it does | Mark |
|---|---|---|
| FLEngine `FUN_01046a70` (branch `0x1FD0…0x1FE7`) | Decodes band/quantity, writes `T+0x1D4+b·0x88`, dB/Hz hints | DECOMP |
| FLEngine `0x0104eed3…0x0104ef3f` | EQ gain range ±1800 (text edit) | DECOMP |
| FLEngine `FUN_0127b390` | Gain·0.01 dB, freq `10+159.9·(101^x−1)`, bw = v/65536, tail counter | DECOMP |
| FLEngine `0x01287061…0x012870bc` (track creation) | Band types 6/7/8, defaults 0x1691 / 17500 | DECOMP |
| FLEngine `FUN_0069d0e0` + `FUN_0069cb10` | RBJ coefficients: types 1…8, Q/S from bw | DECOMP |
| FLEngine `FUN_0069d500` (via `PTR_DAT_015e4448`) | Stereo double DF1 biquad | DECOMP |
| FLEngine `FUN_01276d00` | Loop over the 3 bands, bypass at gain 0, tail hold | DECOMP |
| FLEngine `FUN_01276ee0` (+ `01277550/012775a0/012775f0/01276e40`) | Order FX → EQ → fader and taps | DECOMP |
| FLEngine `FUN_009cdfc0`, table `0x014255c0` | LFO sine/triangle/pulse tables | DECOMP |
| FLEngine `FUN_009ce9e0`, `FUN_009ce950`, `FUN_006614a0` | Speed → ticks → phase increment | DECOMP |
| FLEngine `FUN_009cedb0`, `FUN_009ce490` | Per-voice phase, delay, fade-in, global mode | DECOMP |
| FLEngine `FUN_009ceec0`, `FUN_009cef00`, `FUN_006614b0` | Global phase = position·increment | DECOMP |
| 48 active LFOs in the demos | Shapes, sync, global, speeds on exact divisions | DATA |
| 3x Osc `FUN_007389d0`, `DAT_00791a18` | AM → osc index 2 | DECOMP |
| 3x Osc `FUN_0073a230`, `0x739e90` (mode dispatch) | Render order, modes 10/11/12 | DECOMP |
| 3x Osc `FUN_00739e10` / `FUN_00739d20` | AM mode: `acc −= acc·g·o` | DECOMP |
| 3x Osc `0x73acc0` | SSE/x87 choice of the mixing routines | DECOMP |
| FLEngine `FUN_00f6f240` + field table (`0xb55c2d…`) | `0xDD` flags: bit 0 Mono, bit 1 Porta | DECOMP+STR |
| FLEngine DFM `TPluginForm` (`PolySlideWheel`, `GlideBox`, `MonophonicBox`, `MaxPolySelect`, `PortaNoGateBox`) | Ranges 0…1660 (820), 0…99, names | STR |
| FLEngine `FUN_00fea220`, `FUN_0117bbc0`, `FUN_01312200`, `FUN_011f2cb0` | Porta time → 1/48 step → ticks; hint in steps:ticks | DECOMP |
| FLEngine `FUN_00d293d0`, `FUN_00d2bd90` | Voice trigger, polyphony limiter | DECOMP |
| FLEngine `FUN_00d237e0`, `FUN_00d23db0`, `FUN_00d26460`, `FUN_00d26430`, `0x00d2ab90` | Glide condition (XOR), start at the group's last note, linear ramp per tick | DECOMP |
| FLEngine `FUN_00d265e0` | Order of the 5 per-note parameters (pan, vol, pitch, ModX, ModY) | DECOMP |
| FLEngine `FUN_00d236d0` | Per-group mono list | DECOMP |
| 3,443 `0xDD` events in the demos | Max/flags combinations and times | DATA |
| FLEngine `0x015d5068` (2 × 13 pointers) | Names of marker types 0…12 | STR |
| 314 `0x94` markers in the demos | Types used, following `0x21/0x22/0xCD/0x2E` events | DATA |
