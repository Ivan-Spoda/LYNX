# FL Studio 2026 behavioural analysis: round 3 (REA)

> **Nature of this document.** Continuation of `docs/ANALISE_FL_STUDIO.md` and of `REA_rodada2.md`.
> *Clean-room* reverse engineering, behavioural only. No Image-Line code or pseudocode was
> copied: what is here are descriptions, data layouts and formulas written in our own words.
> Addresses (`FUN_xxxxxxxx`), offsets and globals serve only to trace the evidence.

- **Target:** `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA
- **Format validation:** the 165 `.flp` files from `FL Studio 2026/Data` (3,443 channels), our own event parser
- **Date:** 2026-10-13

| Marker | Meaning |
|---|---|
| **[DECOMP]** | Confirmed by reading the decompilation of the cited function(s) |
| **[STR]** | Inferred from strings, RTTI or DFM forms |
| **[DATA]** | Statistically confirmed in the demo `.flp` files |
| **[HYP]** | Hypothesis (plausible, not yet verified) |

Convention: `C+0xNN` is an offset in the channel object (the `local_150` of the dispatcher `FUN_00f46490`);
`S+0xNN` is an offset in the channel's generator/sampler object (`C+0x330`, which the dispatcher calls
`local_150[0x66]`).

---

## 1. Channel envelopes and LFOs: event `0xDA`

### 1.1 Where they live **[DECOMP+DATA]**

- Each channel has **5 `0xDA` records of 68 B** (17,215 events = 5 × 3,443 channels in the demos; all of them
  68 B). The order is fixed and comes from a name table (`0x01425630`):

  | Index | Target | "amount" range (table at `0x01425658`) |
  |---|---|---|
  | 0 | **Panning** | ±64 |
  | 1 | **Volume** | ±128 (the amount is not used; see §1.5) |
  | 2 | **Filter cutoff** (Mod X) | ±128 |
  | 3 | **Filter resonance** (Mod Y) | ±128 |
  | 4 | **Pitch** | ±2400 (cents: ±2 octaves) |

- The dispatcher copies the raw 68 B to `S+0x198 + i·0xA0` (i = `0xDA` counter within the channel). The
  0xA0 B in memory are the file's 0x44 followed by derived fields (§1.3).
- Files with a record < 68 B (old ones): if `flags & 0x08`, the attack tension becomes −101; if
  `flags & 0x40`, the decay tension becomes −101; then these two bits are cleared.
- The same fields are automatable parameters: channel `controlID` with
  `param = 0x1000 + (target << 8) + dword_index` (see §2.7 of round 2). Indices 8 (env amount) and
  11 (LFO amount) are bipolar with the range from the table above (`FUN_01046a70`, branch `0x1000…0x17FF`). Every
  write calls the recalculation `FUN_009ce9e0`.

### 1.2 Record layout (68 B, all `i32` little-endian) **[DECOMP+DATA]**

Defaults come from `FUN_009cec20` (initialisation) and match the most common value in the demos.

| Off | Field | Range | Default |
|---|---|---|---|
| 0x00 | **flags** (see table below) | bits | 0 (Volume: 4) |
| 0x04 | **Envelope enabled** (0 = off, ≠0 = on; FL writes −1) | bool | 0 |
| 0x08 | Env **delay** (pre-delay) | 0…65536 (time, §1.4) | 100 (= 0) |
| 0x0C | Env **attack** | time | 20000 |
| 0x10 | Env **hold** | time | 20000 |
| 0x14 | Env **decay** | time | 30000 |
| 0x18 | Env **sustain** | 0…128 (level = v/128) | 50 |
| 0x1C | Env **release** | time | 20000 |
| 0x20 | Env **amount** | ±target range | 0 |
| 0x24 | LFO **delay** | time | 100 (= 0) |
| 0x28 | LFO **attack** (fade-in) | time | 20000 |
| 0x2C | LFO **amount** | ±target range | 0 |
| 0x30 | LFO **speed** (period) | time | 32950 (= 2 s or 1 bar) |
| 0x34 | LFO **shape**: 0, 1, 2 (clamp) → waveform table `PTR_015e3168` | 0…2 | 0 |
| 0x38 | **Attack** tension | −128…+128 | 0 |
| 0x3C | **Decay** tension | −128…+128 | 0 |
| 0x40 | **Release** tension | −128…+128 | 0 (Volume in the demos: −101) |

`flags` bits **[DECOMP]**:

| Bit | Meaning |
|---|---|
| 0x01 | **Envelope synced to tempo** (envelope TEMPO button) |
| 0x02 | **LFO synced to tempo** |
| 0x04 | With the envelope **disabled**, the voice starts directly at level 1.0 (default for the Volume target). Also: envelope enabled with attack = 100 (zero) starts at 1.0 |
| 0x08 / 0x40 | Only in old files (implicit −101 tension, see above) |
| 0x20 | **Global LFO**: the phase comes from a global clock (`FUN_009ceec0`/`FUN_009cef00`, advanced by the song position) instead of restarting on each note |

In the demos: Volume flags = 4 in 3,406 channels; other values seen 0x14, 0x05, 0x26, 0x27 **[DATA]**.
Envelope enabled: 150 channels on Volume, 51 on cutoff, 30 on pitch **[DATA]**.

### 1.3 Derived fields in memory (`FUN_009ce9e0`) **[DECOMP]**

| Mem | Content |
|---|---|
| `+0x58` | Envelope delay in **ticks** (int) |
| `+0x44` | Attack increment per tick = `1/(ticks_attack + 1)` |
| `+0x5C` | Hold in ticks |
| `+0x48` | Decay increment = `−1/(ticks_decay + 1)` |
| `+0x4C` | Sustain = `v/128` |
| `+0x50` | Release increment = `−1/(ticks_release + 1)` |
| `+0x70` | Env amount = `v/128` (float) |
| `+0x7C/+0x88/+0x94` | Tension curves (3 × 12 B: value, `1/|a|`, `ln(|a|+1)`) |
| `+0x60` | LFO delay in ticks |
| `+0x54` | LFO fade-in increment = `1/(ticks + 1)` |
| `+0x64` | LFO phase increment = `2³² / max(ticks_period, 3)` (32-bit phase) |
| `+0x74` | LFO amount = `v/128` |
| `+0x68` | Waveform table pointer (16384 samples) |

### 1.4 Time conversion (knob → seconds) **[DECOMP]**

`FUN_009ce950`:

```
ticks(v) = 0                                   if v < 101
ticks(v) = round( (1000^(v/65536) − 1) · E )   otherwise
E = (PPQ/4)/48 · 24576/1000 = 0.128 · PPQ      (envelope synced, flag 0x01/0x02)
E = 0.128 · PPQ · BPM/120                      (not synced: absolute time)
```

The envelope and the LFO advance **one step per sequencer tick** (`FUN_00d0ece0` sets the global flag
`PTR_015e1820` at each tick boundary; `FUN_00d24ae0` only calls the step `FUN_00d24500` → `FUN_009ce490`
when it is set). Since there are `PPQ·BPM/60` ticks per second, the times become:

- **Not synced (default):** `T = 0.064 s · (1000^(v/65536) − 1)`, independent of BPM.
  - v = 20000 → 0.463 s; 30000 → 1.448 s; 32950 → 2.000 s; 65536 → 63.9 s.
- **Synced:** `T = 0.128 beats · (1000^(v/65536) − 1)`. The LFO default (32950) gives exactly
  **4 beats = 1 bar**, which confirms the scale. At 120 BPM the two modes coincide.
- The resolution is **1 tick** (≈ 5.2 ms with PPQ 96 at 120 BPM). Within the tick the resulting gain is
  linearly interpolated per sample (`FUN_006a3810` in `FUN_00d24ae0`), so there are no audible steps
  in the volume.

### 1.5 Voice state machine (`FUN_009cedb0` start, `FUN_009ce490` step, `FUN_009cecc0` note-off) **[DECOMP]**

State per target and per voice (0x24 B at `voice+0xA8 + i·0x24`): counter, LFO phase, LFO delay counter,
**stage**, linear position `x`, envelope output `y`, LFO fade, LFO output.

| Stage | Per-tick behaviour |
|---|---|
| 0 Delay | counts ticks up to `+0x58`; then goes to 1 |
| 1 Attack | `x += 1/(n+1)`; on reaching 1.0 goes to Hold (2) or, if hold = 0, Decay (3). `y = attack_curve(x)` |
| 2 Hold | counts ticks up to `+0x5C`; then Decay |
| 3 Decay | `x −= 1/(n+1)`, `y = decay_curve(x)`; when `y ≤ sustain`, pins `y = sustain`, recomputes `x` via the inverse of the curve and goes to 4 |
| 4 Sustain | stationary while the note is held |
| 6 Release | entered on note-off from any stage < 7: `x` = inverse of the release curve applied to the current level; then `x −= 1/(n+1)`, `y = release_curve(x)` |
| 9 End | when stage > 2 and `y` drops below 5.96e-8 (≈ −144 dB) |

Practical consequences:
- The **decay** traverses the ramp from 1.0 down to 0; the actual time to sustain is `T_decay · (1 − s)` (linear
  curve). The **release** starts from the current level, so it takes `T_release · level` (linear).
- If the envelope is **disabled**, none of this runs (the envelope output stays at the initial value: 1.0 if
  `flags & 4`, otherwise 0).

**Tension curve** (`FUN_009ce890` + `FUN_006774b0`), with `t` = tension (−128…128):

```
a = sign(t) · (6347^(|t|/128) − 1) · 0.1     (t = 0 → straight line, y = x)
K = ln(|a| + 1),  C = 1/|a|
t < 0:  y = C · (e^(K·x) − 1)                    (starts slow, ends fast)
t > 0:  y = 1 − C · (e^(K·(1−x)) − 1)            (starts fast, ends slow)
```

Both shapes pass through (0,0) and (1,1). The default Volume release (−101) is strongly concave downward,
similar to an exponential decay.

### 1.6 How the output is applied (`FUN_00d24500`) **[DECOMP]**

Per tick, for each voice (y = envelope output, l = LFO output, already multiplied by the fade-in):

| Target | Modulation |
|---|---|
| Volume | `gain = y · (1 + l · lfoAmt/128)`, floored at 0. **The volume envelope amount is ignored**; the envelope multiplies the gain directly |
| Cutoff | `y · envAmt/128 + l · lfoAmt/128` (added to the voice's Mod X) |
| Resonance | same with the amounts of record 3 (added to Mod Y) |
| Pitch | `y · envAmt + l · lfoAmt` in **cents** (raw amount, ±2400) |
| Panning | `2 · l · lfoAmt/128` (the envelope part is applied elsewhere **[HYP]**) |

**LFO:** 32-bit phase, table index = `phase >> 18` (16384-point tables generated in
`FUN_009cdfc0`: sine, triangle, ±1 square, −1…1 ramp, and others). Shape 0/1/2 picks one of three
entries of `PTR_015e3168` (in the FL UI: sine, triangle and pulse **[HYP for the order]**). The LFO starts
after the delay and comes in with a linear fade-in of `1/(n+1)` per tick.

### Recommendations for LYNX (envelopes)

1. Importer: read the 5 `0xDA` records in the order Pan, Vol, Cutoff, Res, Pitch. For a sampler ADSR,
   use record 1 (Volume) if `+0x04 ≠ 0`:
   `delay/attack/hold/decay/release = 0.064·(1000^(v/65536) − 1)` s (or in beats ×0.128 if `flags&1`),
   `sustain = v/128`, tensions `v/128`.
2. Implement the same "DAHDSR" ADSR with exponential tension (formula above). A control tick of
   ~1–5 ms with per-sample linear gain interpolation is faithful to FL; it can be evaluated per sample if
   preferred.
3. Release from the current level (not from sustain) and decay measured from 1.0 down to 0.
4. The volume envelope multiplies the voice gain; the others add `amount·y` to the destination.

---

## 2. Channel volume and pan: gain curve

### 2.1 Where they live **[DECOMP+DATA]**

| Source | Field | Content |
|---|---|---|
| `0xDB` (24 B) → `C+0x16C` | `i32 pan, i32 vol, i32 pitch, i32 modX, i32 modY, i32 filter_type` | pan 0…12800 (centre 6400), vol 0…12800, pitch in cents, Mod X (cutoff, default 256), Mod Y (res., default 0), filter type (0…7) |
| `0xE5` (20 B) → `C+0x184` | `i32 panAdj, i32 volAdj, i32 ?, i32 modXAdj, i32 modYAdj` | channel "level adjustments": panAdj ±6400 (0 = neutral), **volAdj 12800 = neutral** |
| Files < 5.3.7 | | pan/vol (from `0xDB` and `0xE5`) stored at /100 scale (the load multiplies by 100) |
| Legacy events `0x02`/`0x03` | | vol/pan as a byte, converted by `FUN_00f3ad20`/`FUN_00f3ad40` |

As a parameter (channel `controlID`, §2.7 of round 2): `p=0` volume, `p=1` pan, `p=2/3` Mod X/Y,
`p=4` pitch (range −4800…+4800), `p=0x10…0x14` the `0xE5` adjustments **[DECOMP, `FUN_01046a70`]**.

### 2.2 Formula (`FUN_00fe9d20`, checked against the script API `getChannelVolume` in `FUN_00d59d70`) **[DECOMP]**

**It is the same curve as the mixer fader**, only with unity at the top of the channel's travel:

```
x = vol / 12800                       (0…1)
f(x) = 1                              if x == 1
f(x) = (11^x − 1) / 10                otherwise
channel_gain = f(vol/12800) · f(volAdj/12800)          (stored at C+0x19C)
dB = 20·log10(gain)                   (the script API returns exactly this in mode 1)
```

**Direct answer:** volume **12800 (100 %) = 0 dB**. The default **10000 (78 %) is NOT 0 dB: it is −5.18 dB**
(gain 0.551). Table:

| vol | % | gain | dB |
|---|---|---|---|
| 12800 | 100 | 1.000 | 0.0 |
| 11200 | 87.5 | 0.715 | −2.9 |
| 10000 | 78.1 | 0.551 | **−5.2** |
| 8000 | 62.5 | 0.348 | −9.2 |
| 6400 | 50 | 0.232 | −12.7 |
| 3200 | 25 | 0.082 | −21.7 |
| 0 | 0 | 0 | −∞ |

Constants: `1/12800` (`0x013e7584`), `ln 11` (computed in `FUN_006a6890`, `DAT_015f49ec`) and `0.1`
(`0x013e7558`). The channel does not go above 0 dB via the knob (the mixer goes up to +5.6 dB).

**Effective pan** (`FUN_00fe9bf0`): `p = (pan − 6400 + panAdj) / 6400` in −1…+1, stored at `C+0x198`.
For MIDI/plugins, it also sends `p·64` rounded in −64…+64.

**Volume for plugins (MIDI CC7):** `FUN_00fe9d20` sends `vol·volAdj/0x138800` (≈ /1,280,000), clamped to 127.

### Recommendations for LYNX (volume)

1. Importer: `channelGain = f(vol/12800)·f(volAdj/12800)` with `f(x) = (11^x − 1)/10`. Do not treat 10000 as
   0 dB; FL projects sound 5.2 dB quieter at the channel by default, and this must be preserved.
2. Effective pan = pan from `0xDB` + panAdj from `0xE5`.

---

## 3. Swing (global and per channel)

### 3.1 Storage **[DECOMP+DATA]**

| Item | Where | Range | Default |
|---|---|---|---|
| **Global swing** ("Main swing") | event **`0x0B`** (u8) in the project header; `controlID 0x40000001`; global `DAT_0196ec34` | **0…128** (128 = 100 %) | 0 (145 of 165 demos); values seen 16…66 |
| **Channel swing mix** | event **`0x61`** (u32) → `C+0x220`; channel parameter `p = 12` | 0…128 | **128** (3,235 of 3,239 channels) |
| Channel **"Truncate swing notes"** | event **`0x30`** (u8 bool) → `C+0x224` | 0/1 | 0 |

`FUN_01311070(v)` stores the swing and precomputes `DAT_0196ec30 = (ticksPerStep · v) >> 8`, with
`ticksPerStep = PPQ/4` (16ths). The script API `getSwing` (`FUN_00d5dbf0`) returns `value/128`.

### 3.2 Application during note playback (`FUN_00d0c220`) **[DECOMP]**

For each pattern note about to fire, with `p` = note position in ticks within the pattern and
`S = PPQ/4`:

```
k  = floor((p + S/2) / S)               (nearest step)
if k is odd (16th off-beat):
    d = |p − k·S|                        (distance to the grid)
    w = S − 2·d                          (triangular weight: S on the grid, 0 at ±S/2)
    delay = (w · globalSwing · channelSwingMix) >> 15        [ticks]
    if channel "truncate": duration −= w
```

- **At 100 % (128 × 128)**, a note exactly on the odd 16th is delayed by `S·16384/32768 = S/2`, i.e.
  **half a 16th = a 32nd note (1/32)**. Pairs of 16ths end up in a 3:1 ratio (dotted), not 2:1
  (triplet).
- Off-grid notes get a delay proportional to their proximity to the odd step; on even steps the delay is
  zero. So swing also applies to piano roll notes (FL warns when opening old projects, in which
  it only applied to steps: string `0xf519a8`).
- The delay is in whole ticks (1-tick resolution).
- In the steps→notes conversion (`FUN_010012e0`) the odd step receives the whole `DAT_0196ec30` (global only,
  without the channel mix) plus the step's individual *shift*.

### Recommendations for LYNX (swing)

1. Importer: global swing from `0x0B`/128, per-channel mix from `0x61`/128.
2. Implement it in the note scheduler, not per step: `delay = (S − 2·|p − k·S|) · sg · sc / 2`
   (sg, sc ∈ 0…1), applied only when the nearest step is odd. This reproduces FL for steps and free
   notes.

---

## 4. Channel parameters: event `0xD7` and related small events

### 4.1 Loading **[DECOMP]**

The dispatcher copies up to **168 B** of `0xD7` to `C+0x284` (hence, offset in the event = `C+0x284+off`).
Sizes seen in the demos: 168 (1,855), 125 (758), 158, 157, 156, 112 (older versions are prefixes).
Version conversions at load:

- `< 10.?` (`0x1e8c50`) with `off 0x04 ≠ 0xFF`: fills legacy sampler fields (`S+0x23C…0x254`).
- `< 11.5.13` (`0x2dd27c`): zeroes bytes `0x3D…0x3F` and normalises `0x3C` to bool.
- `< 11.?` (`0x2dda4a`): `0x34` and `0x40` go through `FUN_0117bc50` and are limited to **450…1447**.
- `< 5.3.7`: `C+0x170`/`C+0x16C` ×100 (old vol/pan).
- `< 6.1.5` (`0x5b916d`): stretch mode 1…2 → +2; ≥ 3 → +4.
- `< 6.1.?` (`0x5b9d21`): zeroes `0x04…0x07`.
- `< 11.5.13` (`0xa7ec55`): **stretch mode** remapped via the table `{0,6,6,7,7,8,9,3,4,5}`
  (`0x014e01fb`), and the result 5 becomes 6.
- If `(flagsA9 & 1) == 0`: field `0x60` (stretch time) was stored as **int** and is converted
  to float.

### 4.2 Layout (168 B) **[markers per row]**

Defaults = most common value in the demos. The "Evidence" column says what was confirmed. Names marked
[HYP/com.] match the community's public naming (PyFLP) **and** the data distribution, but
were not seen being consumed in the code.

| Off | Type | Field | Default | Evidence |
|---|---|---|---|---|
| 0x00 | i32 | ? (140 in audio clips, −1 in the others) | −1 | [DATA] it is not the project BPM |
| 0x04 | i32 | legacy field (trigger for the < 10 conversion) | 0 | [DECOMP] |
| 0x08 | u8 | ? (1) | 1 | [DATA] |
| 0x0A | u8 | also written by event `0x10` | 0 | [DECOMP] |
| 0x0B | u8 bool | also written by event `0x0E` (1 in generators, 0 in audio/automation) | 1 | [DECOMP+DATA] |
| 0x0C | i32 | also written by event `0x8D` | −1 | [DECOMP] |
| 0x10 | i32 | 60 (note C5) | 60 | [DATA] likely keyboard root note [HYP] |
| 0x14…0x27 | 5×f32 | 1.0 each | 1.0 | [DATA] |
| 0x28 | i32 | **Arp: direction** (0 = off, 1 = up, …) | 0 | [HYP/com.+DATA] |
| 0x2C | i32 | **Arp: range** (octaves) | 1 (audio clips: 3) | [HYP/com.] |
| 0x30 | i32 | **Arp: chord** (−1 = none) | −1 | [HYP/com.] |
| 0x34 | i32 | **Arp: time** (scale 450…1447, same conversion as the gate) | 1024 | [DECOMP: clamp] + [HYP/com.] |
| 0x38 | i32 | **Arp: gate** (0…48; 48 = 100 %) | 48 | [HYP/com.] (same range as the piano roll arp) |
| 0x3C | u8 | Arp: slide | 0 | [HYP/com.] |
| 0x3E | u8 | "Full porta" | 1 | [HYP/com.] |
| 0x3F | u8 | "Add root" | 0 | [HYP/com.] |
| 0x40 | i32 | **Channel gate** (450…1447; **≥ 1447 = off**). Channel parameter `p=9`. When active, the note is shortened to `pos + C+0x6D8` on trigger | 1447 | [DECOMP: `FUN_00d293d0`, `FUN_01046a70`] |
| 0x44, 0x48 | 2×i32 | (0, 256) "key region"? | | [HYP/com.] |
| 0x4C | i32 | channel parameter `p=10` | 0 | [DECOMP] |
| 0x50 | u8 | Normalize (precomputed) | 0 | [HYP/com.+DATA] |
| 0x51 | u8 | Invert polarity (precomputed) | 0 | [HYP/com.] |
| 0x53 | u8 | **Declick mode** (0…5) | 0 | [HYP/com.+DATA: 0,1,3,2,5] |
| 0x54 | i32 | Loop crossfade | 0 | [HYP/com.] |
| 0x58 | i32 | Trim | 0 | [HYP/com.] |
| 0x5C | i32 | Arp: repeat | 1 | [HYP/com.] |
| **0x60** | f32 (or i32, see above) | **Stretch TIME**: target duration in **1/192 of a beat** (0 = off) | 0 | **[DECOMP]** `FUN_00fea570`, `FUN_00d26890` |
| **0x64** | i32 | **Stretch PITCH** in cents (±1200) | 0 | **[DECOMP]** added to the voice pitch in `FUN_00d26890` |
| **0x68** | i32 | Raw **Stretch MUL**: factor = `2^(v/10000)` (±20000 → ×¼…×4; 0 → 1.0) | 0 | **[DECOMP]** `FUN_01186110` |
| **0x6C** | i32 | **Stretch mode** (table §5.2) | 0 | **[DECOMP+STR]** |
| 0x70 | i32 | ? | 2 | [DATA] |
| 0x74 | i64 | ? | −2 | [DATA] |
| **0x80** | f64 | **Sample start** (fraction 0…1 of the length) | 0.0 | [DATA: 0.057…] [strong HYP] |
| **0x88** | f64 | **Sample end/length** (fraction 0…1) | 1.0 | [DATA: 0.39, 0.27…] [strong HYP] |
| **0x90** | f64 | Playback **start offset** = `v/knob_max` (parameter `p=13`) | 0.0 | **[DECOMP]** `FUN_01046a70` |
| 0x98 | i32 | ? | −1 | [DATA] |
| 0x9C, 0x9D | u8 | flags (1,1) | | [DATA] |
| **0xA0** | f64 | **Formant** 0…1 (0.5 = "preserve"); displayed as ±1200 c; parameter `p=15` = `v·2400` | 0.5 | **[DECOMP]** `FUN_01046a70` (text "preserve") and `FUN_0067db90` |

### 4.3 Other channel events LYNX needs **[DECOMP+DATA]**

| Event | Destination | Content |
|---|---|---|
| `0x00` (u8) | `C+0x32E` | Channel **enabled** (bool; parameter `p=7`) |
| `0x15` (u8) | type | ChanType 0…5 |
| `0x16` (u8/u16) | `C+0x258` | Destination mixer track (parameter `p=8`) |
| `0x30` (u8) | `C+0x224` | "Truncate swing notes" (§3) |
| `0x61` (u32) | `C+0x220` | Channel swing mix 0…128 (§3) |
| **`0x84` (u32)** | `C+0x27C`, `C+0x280` | **Cut group** = low u16, **cut by** = high u16. When both are equal and ≠ 0, the channel "cuts itself" (`FUN_00feaaf0`; string " - cut by self"). In the demos: `0x00010001`, `0x00020002`… |
| **`0x87` (u32)** | `S+0x15C` | **Sample root note** (MIDI; only written when ≠ default). Values seen 36, 48, 59, 72, 84 [strong HYP] |
| `0x8F` (u32) | sampler flags | bit 0 → `S+0xBD`, bit 1 → `S+0xF9`, bit 2 → `S+0xFA`, bit 3 → `S+0x14D`, bit 8 → `S+0xF8`. Audio clips: 2 / 0x102; samplers: 10 [names HYP] |
| **`0xDD` (9 B)** | `C+0x1EC` | **Polyphony**: `i32 max voices` (0 = unlimited, 1 = mono), `i32 slide/porta time` (parameter `p=6`), `u8 flags`. Voice triggering calls the limiter `FUN_00d2bd90` when max ≠ 0 [partial DECOMP] |
| `0xDB` (24 B) | `C+0x16C` | pan/vol/pitch/ModX/ModY/filter type (§2) |
| `0xE5` (20 B) | `C+0x184` | level adjustments (§2) |
| `0xD1` (20 B) | `C+0x200` | Channel "echo/delay" block: `+0x04` receives +6400 at load; `+0x0C` number of echoes (4); `+0x10` time (144). The legacy event `0x82` derives `feedback = (128/(n+1))·n` [HYP for the names] |
| `0xDA` ×5 | `S+0x198` | Envelopes/LFOs (§1) |

**Not located in this round:** the sampler's "Reverse" flag and "Keep on disk" (`KeepOnDiskBox` exists in the
DFM, but we did not find the field; candidates: bits of `0x8F` or bytes `0x50…0x53`/`0x9C` of `0xD7`).

---

## 5. Time-stretch of samplers and audio clips

### 5.1 How the voice computes the read speed (`FUN_00d26890`) **[DECOMP]**

Definitions (per voice):
- `len` = sample length in frames (`sample+0xA4`); `SRs` = sample's sample rate (`sample+0x7C`).
- `k` = `1/SR_output` (global factor at `PTR_015e2e68+0x18`), so `SRs·k` is the sample-rate ratio.
- `T` = target time in output samples = `(time·(PPQ/4)/48) · samplesPerTick` = `time/192` beats.
- `mul = 2^(MUL/10000) · eventFactor`, where `eventFactor` comes from the playlist item (see §5.3; 1.0
  for ordinary notes).
- `p` = total pitch in cents = note/voice pitch + channel pitch + **Stretch PITCH** (`0x64`).
- `FUN_0067d9b0(r)` sets the read speed `r` (source samples per output sample). For
  `r > 1` the sinc interpolator lowers the cutoff to `π/r` and increases the number of points (max. 512).
- `FUN_0067db00(p, r)` = resampling: speed `2^(p/1200)·r` (pitch and duration coupled).
- `FUN_0067db90(p, τ, f)` = **real-time stretch** (élastique): pitch `2^(p/1200)` (clamp 0.01…32),
  time ratio τ (clamp 1e-5…32) and formant `2^((1−f)·2 − 1)` (f = 0.5 → unchanged).

| Mode (`0x6C`) | TIME = 0 | TIME > 0 |
|---|---|---|
| **< 0** (Stretch, Stretch Pro: real time) | `r = SRs·k/mul`; élastique with pitch `p`, τ = `1/mul`, formant | `r = len/(T·mul)`; élastique with pitch `p`, τ = `len/(T·mul·SRs·k)`, formant → **duration follows the BPM and pitch is independent** |
| **0** (Resample) | resampling `2^(p/1200)·SRs·k`, divided by `mul` if MUL ≠ 0 or factor ≠ 1 | `r = len/(T·mul)` → fits to tempo, **pitch follows** (note pitch is ignored) |
| **≥ 1** (e3, slice, auto, e2: precomputed) | resampling `2^(p/1200)·SRs·k` (no `mul`) | `r = len/(T·mul)` over the buffer already stretched offline |

Modes ≥ 1 are rendered **offline** (`FUN_00a981f0`, unit `DSP_Stretching`: élastique, or per-region slices
for "slice"); hence the dialog "Restretch all channels now?". In real time they only play the ready
buffer. The message "The sample rate of this sample is too high. Choose a different time stretching option."
comes from that offline path.

### 5.2 Mode values **[DECOMP+STR, mapping by offset: strong HYP]**

The unit has a fixed array of 12 names (`0x015ca520`): `stretch pro, stretch, resample, e3 generic, e3 mono,
slice stretch, slice map, auto, e2 generic, e2 transient, e2 mono, e2 speech`. With "resample" = 0 (the only
mode in which the code divides the resampling by `mul`), the stored value is `index − 2`:

| Value | Mode | Type |
|---|---|---|
| −2 | Stretch Pro | real time, with formant |
| −1 | Stretch | real time |
| 0 | Resample | resampling |
| 1 | E3 generic | offline |
| 2 | E3 mono | offline |
| 3 | Slice stretch | offline by slices |
| 4 | Slice map | offline by slices |
| 5 | Auto | offline |
| 6…9 | E2 generic / transient / mono / speech | offline |

This matches the conversion of old files (old values 1…6 become 6…9 = E2 family; 7…9 become
3…5 = slice/auto) and the demos: audio clips use 0 (452), 5 (123), −1 (83), 2 (17), −2 (14).

### 5.3 Audio clip in the playlist **[DECOMP + strong HYP]**

- The audio clip is a `ChanType 4` channel with the sample in `0xC4`; the stretch fields are those of that
  channel's `0xD7`. In the demos, 128 of the 705 audio clips have TIME > 0 (e.g.: 1536 = 8 beats, 3072 = 16 beats,
  6144 = 32 beats).
- When triggering the voice of a playlist item (`FUN_00d293d0`), the sampler voice receives the **clip sample**
  and a **per-event speed factor** (`voice+0x39C`, double, default 1.0; `FUN_00d27bc0`). This factor
  multiplies the channel's `mul` in `FUN_00d26890`.
- In the 80 B playlist item, the `f64` at `+0x40` (§4.2 of round 2) is 1.0 in 10,481 items and 1.6303 /
  1.2855 in 95 audio clip items that were **resized with stretch** ("Stretch audio when resizing
  audio clips"). This indicates that `+0x40` is that per-clip stretch factor **[strong HYP]**.
- Clip time → sample time mapping (mode with TIME > 0):
  `samplePos = posInClip_samples · len / (T · mul · clipFactor)`. Since `T` is recomputed with
  `samplesPerTick`, a BPM change changes the read speed: the clip keeps occupying
  `time/192 · mul · factor` beats.
- The item's start/end offsets (`+0x18/+0x1C`) for audio clips are **not** in plain ticks (in one demo,
  end = 1484.9 for a 385-tick item with factor 1.2855). The unit remains open.

### Recommendations for LYNX (stretch)

1. Importer: read `0x60` (TIME, in 1/192 beat; int if `(flagsA9&1)==0`), `0x64` (pitch c), `0x68`
   (`mul = 2^(v/10000)`), `0x6C` (mode) and `0xA0` (formant). Convert: TIME > 0 → "warp to N beats".
2. Playback model: `speed = len/(T·mul·factor)` when there is a TIME; otherwise `mul`. Mode 0
   resamples (pitch follows); modes < 0 use a real-time stretcher (Rubber Band / SoundTouch in
   LYNX) with independent pitch and formant; modes ≥ 1 can be treated as "high-quality offline
   stretch" or mapped to the same stretcher.
3. Store the clip's stretch factor (item `+0x40`) separately from the channel; it only scales the duration.

---

## 6. Questions that remain open

1. Exact order of the LFO waveforms (`PTR_015e3168[0..2]`; tables generated in `FUN_009cdfc0`).
2. Where the **pan** envelope is applied (the step `FUN_00d24500` only combines the pan LFO).
3. Sampler "Reverse" and "Keep on disk" flags; meaning of the `0x8F` bits.
4. Unit of the start/end offsets (`+0x18/+0x1C`) of the playlist item when the item is an audio clip.
5. Fields `0x00`, `0x10`, `0x14…0x27`, `0x70…0x7C` and `0x98…0x9D` of `0xD7`.
6. Confirm in the code that the item's `f64 +0x40` becomes the factor `+0x68` of the trigger event.

## Appendix C: new functions (traceability)

| Address | Inferred role | Confidence |
|---|---|---|
| `FUN_00f46490` (branch `0xDA`) | Copies the envelope/LFO record to `S+0x198+i·0xA0` | DECOMP |
| `FUN_009cec20` | Envelope/LFO record defaults | DECOMP |
| `FUN_009ce9e0` | Recomputes derived fields (ticks, increments, curves) | DECOMP |
| `FUN_009ce950` | Knob → ticks: `(1000^(v/65536) − 1)·E`, zero if v < 101 | DECOMP |
| `FUN_009ce890` / `FUN_006774b0` | Exponential tension curve | DECOMP |
| `FUN_009cedb0` | Initial voice state (delay/attack/flag 4) | DECOMP |
| `FUN_009ce490` | Per-tick step: DAHDSR + LFO | DECOMP |
| `FUN_009cecc0` | Note-off: enters release from the current level | DECOMP |
| `FUN_00d24500` | Combines env/LFO into the voice's 5 destinations | DECOMP |
| `FUN_00d24ae0` | Voice processing: envelope step only at tick boundary; linear gain ramp | DECOMP |
| `FUN_00d0ece0` | Engine tick loop (sets the tick flag `PTR_015e1820`) | DECOMP |
| `FUN_009ce3f0` / `FUN_013111e0` | Updates global PPQ and PPQ/4 | DECOMP |
| `FUN_00fe9d20` | Channel gain `f(vol)·f(volAdj)`, `f(x) = (11^x−1)/10` | DECOMP |
| `FUN_00fe9bf0` | Channel effective pan | DECOMP |
| `FUN_00d59d70` | API `getChannelVolume` (normalised or dB) | DECOMP |
| `FUN_006a6890` | DSP constant initialisation (`ln 11`, `ln 2/12` etc.) | DECOMP |
| `FUN_01311070` | Sets the global swing and the maximum delay `S·v/256` | DECOMP |
| `FUN_00d0c220` | Pattern note triggering with triangular swing and per-channel mix | DECOMP |
| `FUN_010012e0` | Steps → notes conversion (swing + per-step shift) | DECOMP |
| `FUN_00d5dbf0` | API `getSwing` (value/128) | DECOMP |
| `FUN_01046a70` (channel branch) | Channel parameter IDs 0…15, 0x10…0x14, 0x1000+ (env/LFO) | DECOMP |
| `FUN_00fea570` | Stretch TIME setter; recomputes `mul = 2^(MUL/10000)` | DECOMP |
| `FUN_01186110` | `2^(v/10000)` | DECOMP |
| `FUN_00d26890` | Voice read speed by mode/TIME/MUL/pitch/formant | DECOMP |
| `FUN_0067d9b0` / `FUN_0067db00` / `FUN_0067db90` | Interpolator rate, resampling with pitch, real-time stretcher | DECOMP |
| `FUN_00a981f0` | Offline stretch of a sample (élastique / by regions) | DECOMP |
| `FUN_00d293d0` | Triggers a voice from a playlist event/item (gate, polyphony, stretch factor) | DECOMP |
| `FUN_00d27bc0` | Sampler voice constructor (stretch factor = 1.0) | DECOMP |
