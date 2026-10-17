# REA round 105 (send piece) — FL's Fruity Send as an exact engine with a real send between inserts

> **Nature of this document.** Clean-room behavioural analysis of FL Studio 2026's **Fruity Send**
> (`Plugins/Fruity/Effects/Fruity Send/Fruity Send_x64.dll`, SHA-256 `03add028…4f09`, the same build round 15 read).
> Only layouts, formulas and behaviour in our own words; no Image-Line code, pseudocode or data is in the repository.
> Addresses (`FUN_xxxxxxxx`, Ghidra image base 0x400000) only trace the evidence. Markers: **[DECOMP]** read in the
> decompilation, **[EXEC]** measured on FL's own renders, **[model]** our modelling choice, "not confirmed" = open.
> Tools: Ghidra 12.1.4 headless (own scripts `DecAt`, `Vmt`, `Ptr`, `Refs` in `tmp/r105/send/gh/scripts`).

Round 15 (§9) mapped the plugin to LYNX's `utility` with a linear dry level and no send at all. This round decodes the
render path and gives LYNX a real Fruity Send (`EffectKind::fruitySend`, `core/FruityBasicFx.{h,cpp}`).

## 1. State and parameters **[DECOMP `FUN_006bd0f0` (SaveRestoreState), `FUN_006bd630` (ProcessParam)]**

The state is 16 bytes: three `i32` controls and the destination insert, stored back to back in the plugin object
(+0x120…+0x12C):

| i | Control | Range (default) | Internal value |
|---|---|---|---|
| 0 | Pan | −128…128 (0) | `p = float(v) · (1/128)`; marks the send gains dirty |
| 1 | Volume | 0…320 (256) | L11 law: `x = float(v)/256`; `x == 1` → 1, else `float((exp(double(x · float(ln 11))) − 1) · double(0.1f))` |
| 2 | Dry | 0…256 (256) | the same L11 law (**not** linear `v/256` as round 15 assumed; FL's text shows `v/256`) |
| — | Send to | insert index (0 = none) | not an automatable control; set from the editor's insert list |

On load the plugin re-announces its destination to the host (`FUN_006bd600` → `FUN_006ba080`): it asks the host for
the number of inserts (dispatcher 0x32), clamps the index to it, and registers / unregisters the route with the host
dispatcher (one call to remove the old destination, one to add the new one). So FL's mixer knows the plugin route,
which is what lets it order the destination after the source (§3).

## 2. Render **[DECOMP `FUN_006bdb80` (Eff_Render), `FUN_006bd4e0`, `FUN_005e0180`, `FUN_005df3d0`, `FUN_005df860`]**

Per host call (interleaved stereo, `n` frames):

1. **Only when an insert is chosen (index > 0):**
   - if the gains are dirty, four target gains are rebuilt from `p` and the volume `V` (float arithmetic):
     - `p < 0` (towards the left): `k = p + 1`; `g2 = ((1 − k) · 0.5) · V` (right → left), `g1 = k · V` (right → right),
       `g0 = V − g2` (left → left), `g3 = 0`;
     - `p ≥ 0`: `k = 1 − p`; `g3 = ((1 − k) · 0.5) · V` (left → right), `g0 = k · V`, `g1 = V − g3`, `g2 = 0`.
     So one side fades and half of what it loses is folded into the other side (fully left: `L = 0.5·L + 0.5·R`).
   - each current gain ramps to its target with the framework's limited-step ramp (start, per-frame delta
     `(target − current)/n` clipped to the host step at +0x34 = `0.001 · 44100/fs` per frame, the same ramp as
     Fruity Balance in REA_rodada58 §1.4; below 2⁻²⁴ it lands on 0);
   - the host hands the plugin the destination's buffer (host method at +0x128 with the plugin tag, the insert index
     and a lock flag 0), the plugin **adds** `L·g0 + R·g2` / `R·g1 + L·g3` (gains stepping per frame while ramping;
     the constant cases skip the zero terms) into it, and releases it (same call with flag 1).
2. **Always:** the signal continues on the plugin's own insert × the dry gain — copied unchanged when the dry gain
   is exactly 1, otherwise multiplied, **without** a ramp.

The send therefore taps the plugin's input (before the dry gain), at the slot's place in the chain.

## 3. Mixer behaviour

**[EXEC]** FL renders (`FL64.exe /R /Ewav`, 44.1 kHz, 16-bit) of `Data/System/Render tests/Fruity Send.flp` (a 3x Osc
note on insert 1, the Fruity Send in insert 1's first slot) patched by `tmp/r105/send/mk.py`, compared with §2's law
applied to the unsent render A (`tmp/r105/send/cmp.py`):

| Render | Change | Result vs the model |
|---|---|---|
| A | original (pan −54, no destination) | reference: the dry signal only |
| B | → insert 2, dry 0 | identical (max error 0), lag 0 |
| C | → insert 2, pan −64, volume 200, dry 0 | within 2.3·10⁻⁵ (16-bit rounding), lag 0 |
| D | → insert 2, pan +128, volume 320, dry 128 | within 4.8·10⁻⁵ (two roundings summed), lag 0 |
| E | slot moved to insert 3, channel on insert 3, → insert 2 (a lower index) | identical, lag 0 |
| F | pan 64, dry 200, no destination | within 2.3·10⁻⁵: the dry level follows the L11 law |

So FL delivers the send to the destination's input in the same block, also towards a lower-numbered insert (the
route registered in §1 orders the mixer), with no latency of its own. The test signal is mono (L = R), so these
renders confirm the summed levels of each side but cannot separate the cross terms `g2` / `g3` from the direct
ones; that split rests on the decompilation.

**LYNX model [model + DECOMP]:**
- The Fruity Send slot is a route: `Song::mixerOrder()` processes the destination after the source, `canRoute()`
  refuses a route that would close a cycle through it, solo keeps the source of a soloed destination and the
  destination of a soloed source audible.
- While the source insert runs, the slot's send goes to its own buffer (`RackRenderer` send taps, up to 64 per
  song); the destination pulls it like a route, in the same block. A muted / silent source, a bypassed slot, a slot
  skipped by smart-disable or a slot with mix 0 sends nothing. The slot's mix knob blends only the dry path.
- PDC: the send leaves after the enabled slots before it (`inAlign + their latencies`), and the edge is delayed by
  what is missing to reach the destination's alignment, like any route. Whether FL compensates plugin routes the
  same way is **not confirmed**.
- A Fruity Send on the Master sends nothing (every insert is processed before the Master; FL's own behaviour there
  is not confirmed). A destination that does not exist in the project sends nothing.

## 4. Window

FL Studio 2026's window body is 263 × 89: one row with **Dry** (knob), the **Send to** box (a tinted box showing the
destination, "…" when none; FL opens its list of inserts below it), **Pan** and **Volume** knobs, captions under
them. LYNX's page (`plugui::fruitySend`, own drawing) keeps that row; the box lists "None" and every insert by number
and name (inserts that would close a cycle are greyed).

## 5. Verification

- **[EXEC]** LYNX's own renders of the six FL projects (`renderwav`, rescaled to its reported peak) match FL's renders
  within the 16-bit rounding (≤ 5.8·10⁻⁵ over the note, lag 0; the peaks 0.2750 / 0.1515 / 0.5870 are FL's), and
  each matches §2's law applied to LYNX's unsent render A within 3·10⁻⁵ (1.3·10⁻⁴ for D, the 4-digit peak used to
  rescale). The send reaches insert 2 in the same block in both directions (B and E).

- Unit tests (`tests/FruitySendTests105.inc`): state import and texts, the gain law (centre, fully left, half right
  with volume 320, the ramp of a volume change), same-block delivery towards a higher and a lower insert, the
  destination's own effects, mute / bypass / solo, and the PDC plan with latent slots around the send.

## Where I stopped
- Engine, routing, PDC, import, save / load, window and tests written (`feature/send105`); the four new tests pass
  (30 checks). FL renders A–F and LYNX's renders matched (§3, §5).
- Next: full build-local with the FL Data dir, land (`tmp/coord/land.sh send105`).
- Open: the cross terms of the pan matrix are only confirmed by the decompilation (the FL test signal is mono);
  FL's PDC on plugin routes and a Fruity Send on the Master are not confirmed.
