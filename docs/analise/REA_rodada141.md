# Round 141 (REA + feature) — issue #3: FL's Patcher as a real graph, with audio and MIDI I/O

Issue #3 (Ivan-Spoda) asked, as its last unchecked item, for FL's **Patcher** with audio and MIDI inputs and outputs.
Until now LYNX flattened an imported Patcher into consecutive slots or nested series/parallel containers
(REA_rodada14 §1, REA_rodada20 §3) and could not create one. This round makes the Patcher a first-class, editable
plugin graph — in a mixer slot and as a Channel Rack generator — runs it the way FL does, imports FL's Patcher state
into the same graph, and gives it an editor.

Evidence markers: **[DECOMP]** FL's `Patcher_x64.dll` (FL Studio 2026, 7,056,384 bytes) read in Ghidra 12.1.4
(headless, own project `/mnt/d/lynx-work/patcher141/ghidra`), **[DATA]** FL's demo projects / presets (34 Patcher
states), **[TEST]** LYNX's tests, **[HYP]** inference. Clean-room: behaviour and formats only, no FL code copied.

---

## 1. What FL's Patcher does (the parts the runtime needs)

The state format was documented in rounds 14/20 (chunks, nodes `5000…5001`, ports `5009`, pins `1400`, cables
`1300`). This round re-read the engine to pin down the processing:

- **Same binary as rounds 14/20**: the IPP resolver `FUN_005da820` (ippsMulC_32f, ippsAddProductC_32f, ippsZero_32f)
  is at the same address, so the earlier function names still apply **[DECOMP]**.
- **Cable sum** (`FUN_0079bf20`, round 20 §3.1 confirmed): for every audio input port, the first active cable does
  `dst = src × gain`, the next ones `dst += src × gain`; a port with no active cable is zeroed. A cable whose byte
  `+0x1C` is set (FL's "disabled") is skipped **[DECOMP]**.
- **Delay compensation** (`FUN_0079bf20` → `FUN_0079f0b0`): the delay of a cable is
  `(dst.total − dst.own) − src.total`, clamped at 0 — the destination's *input* latency minus the source's output
  latency (node `+0x3C` = total latency, `+0x38` = its own) **[DECOMP]**.
- **Latency of a node** (`FUN_0079bc00`, recursive): `total = own + max(total of every source of every input port)`;
  the Patcher re-plans when the sum changes **[DECOMP]**.
- **Order** (`FUN_0079bd40`, recursive): `depth = 1 + max(depth of the sources)`; the process loop
  (`FUN_0079cd80`) runs the nodes by depth, and with its "threaded" flag it runs each depth layer in parallel. Per
  node (`FUN_0079d830`): gather the inputs, then the node's own vtable process (+0x78) **[DECOMP]**.
- **Control cables**: the same port/cable objects; a control output's value reaches the destination's parameter
  port. The exact smoothing / timing of the value inside FL was not traced (it lives in the per-plugin node vtables)
  **[HYP: LYNX applies the latest value once per block, like its mixer links]**.
- **Positions**: group `1600` holds `u64 node id, f32 x, f32 y` + 8 bytes (24-byte record, 386 in the demos); the
  first two belong to the I/O nodes **[DATA]**.
- **Pin names** seen in the demos: "Audio input" (30), "Audio output" (33), "Notes" (3), "7 Sidechain" (2, = the
  side-chain from insert 7) **[DATA]**. No demo uses extra outputs; FL names those pins after their insert
  ("Insert N", round 14 §1.4) **[HYP from the pin naming]**.

## 2. LYNX's model and runtime (`core/Patcher.h`, `core/Song.h`)

- `PatcherDoc` = nodes + wires + the I/O nodes' positions + `outputTracks` (the insert of each extra output). It is
  stored in `EffectSlot::patcher` (`EffectKind::patcher`) and `ChannelDef::patcher` (`InstrumentKind::patcher`).
- `PatchNode`: an **effect** (any `EffectSlot`: built-in, VST/VST3/CLAP plugin, another Patcher) or an **instrument**
  (a whole `ChannelDef`: built-in, plugin, Patcher), a label, x/y, and `busHeads` (extra outputs of FL's splitting
  plugins, §3).
- `PatchWire`: kind audio / MIDI / control, from (node or `patchFromHost`) + port, to (node or `patchToHost`) + port,
  linear gain, enabled, and a value map (`curve`) for control cables.
  - audio ports: node input 0 = main, 1 = side-chain (fed through `Effect::setSidechain`); node output 0 = main,
    k = bus k; host input 0 = the slot's input, 1 = the slot's side-chain (`EffectSlot::sidechain`); host output 0 =
    main, k = extra output k.
  - MIDI: "From LYNX" notes → instruments (the generator Patcher's channel notes, MIDI-channel aware for plugins).
  - control: a node's control output k (Peak Controller: 0 peak+LFO, 1 peak, 2 LFO; Formula Controller: 0; Envelope
    Controller channel: 0…7) → a parameter of the target (built-in: LYNX's parameter index; plugin: its parameter).
- `patcher::Graph` runs it as FL does (§1): cable sums with gains, disabled cables skipped, order by dependencies
  (Kahn; a cable closing a loop reads the source's previous block), **per-cable delay compensation** and the longest
  path as the slot's latency (PDC), node bypass and dry/wet like a mixer slot, control cables applied each block.
  Values (parameters, gains, on/off) reach a running graph without rebuilding it (`takeValues`, lock-free swap);
  a structural edit builds a new graph that **reuses the running nodes** with the same setup (plugins are not
  re-instantiated, reverb tails survive) — `patcher_keeps_running_nodes_across_snapshots` **[TEST]**.
- The rack (`RackRenderer.cpp`) builds the nodes with its own builders (plugins through the app's factories, samples,
  nested Patchers). A generator Patcher's **extra outputs go to mixer inserts** (`Instrument::auxOutput*`, default =
  the inserts after the channel's own, or `outputTracks`); an effect Patcher's first extra output reaches its insert
  like a Fruity Send (`fruitySendTarget`). Plugins inside Patchers are found on import (`adoptFlPlugins`, with LYNX
  substitutes), their states are captured on save and their render mode follows exports (`AudioEngine`).
- Saved in `.lynx` (`ProjectFile.cpp`: `"patcher"` object with nodes, wires `[kind, from, port, to, port, gain, on,
  curve…]`, `io`, `outputTracks`).

## 3. Import of FL's Patcher (`FlPatcher.cpp`, `FlpImport.cpp`)

- `parsePatcher` now also keeps every port and pin (with names), every cable (disabled ones too), node ids, labels
  (`5010`), flags, positions (`1600`) and the exposed parameters (`1100`).
- A mixer-slot Patcher becomes one `EffectKind::patcher` slot holding the whole graph (no more flattening, so no more
  slots lost at the 10-slot limit: one PanOMatic of "Bombs Away" ins. 69 is back) **[TEST]**. Its nodes go through
  the same converters as mixer slots (`effectFromFl`, Fruity Wrapper → plugin with its FL state, nested Patcher);
  nodes with no LYNX equivalent become labelled pass-throughs (reported). FL's bit 2 of the node flags = bypass.
- A generator Patcher channel becomes `InstrumentKind::patcher`: each instrument node is converted by a one-channel
  import of the same project (so 3x Osc, Sytrus, FPC, wrapped VSTs… come out exactly as on a normal channel).
- Splitter outputs: FL's Frequency Splitter bands / Stereo Shaper difference used by a cable become `busHeads`
  (`patcherBranchHead`, round 17); LYNX's own Frequency Splitter offers buses 1–3 (low, mid, high) in the editor.
- Control cables: FL's parameter index is mapped to LYNX's (`effectParamFromFl`) with its value conversion sampled
  into a 33-point curve; FL's Peak Controller outputs (peak, LFO, peak + LFO) map to LYNX's. Unmapped targets are
  reported.
- Pins: "N Sidechain" sets the slot's side-chain to insert N; "Insert N" extra outputs set `outputTracks`.
- Node names keep FL's old casings ("Fruity delay 2" in FL's own "Patcher (effect)" render test): they are matched
  case-insensitively (`canonicalFlEffectName`) — before this, such nodes were silently dropped by the flattening.
- Control cables whose source has no LYNX control output (X-Y controller, Control Surface: pass-through placeholders)
  are left out, so their targets keep the value FL saved instead of being pinned to 0 (that silenced both "Stroll Up"
  Patchers in a first pass) **[TEST]**.
- Labels and pin names are decoded as UTF-16 (v4) / Windows-1252 (v3) to UTF-8.
- FL's positions are spread ×2.0 / ×1.6 (FL's boxes are smaller; dense demo graphs sit ~60 px apart).
- Demos (all 24 mixer Patchers and the 3 generator Patchers of FL 2026's Demo projects): 280 nodes and 76 control
  cables imported; every graph runs finite and sounds with a noise input — `patcher_demo_graphs_import_and_run`
  **[TEST + DATA]**.

### 3.1 Against FL's own renders **[DATA: `FL64.exe /R /Ewav`, `/mnt/d/lynx-work/patcher141/render`]**

FL ships two render tests for the Patcher (`Data/System/Render tests/Patcher (effect).flp`, `Patcher (generator).flp`).
FL renders vs LYNX's `renderwav` (un-normalized, same length):

| Project | FL | LYNX | Note |
|---|---|---|---|
| Patcher (effect): 3x Osc → Patcher { Control Surface, Fruity Delay 2 (input pan 66, mode 2) } | L −19.90 / R −22.31 dB RMS, peak −5.71 / −10.12 | L −19.90 / R −22.31, peak −5.71 / −10.12 | identical to 0.01 dB |
| Patcher (generator): { Sytrus ← Notes, → out } | L −11.73 dB RMS, peak −2.41 | L −11.69, peak −2.36 | sample correlation 0.998 |

## 4. Editor (`ui/PatcherEditor.cpp`)

- Opens from the mixer slot (editor button / double-click) and from the Channel Rack (click the channel). New:
  **Mixer › Add › Patcher** and **Channel Rack › + › Patcher**.
- Canvas with the "From LYNX" / "To LYNX" nodes and the plugin nodes; drag from an output pin to an input pin to
  cable (audio = grey, notes = blue squares, control = teal); a control cable dropped on a node asks which parameter
  (built-in names, or the plugin's own list). Right-click: add effect / plugin effect / instrument / plugin
  instrument, fit to window; on a node: open editor, rename, bypass, delete; on a cable: gain (presets or a value),
  enable/disable, delete. Delete key removes the selection; wheel zooms; drag the background to pan. The "+ output"
  pin of "To LYNX" adds an extra output.
- Double-click a node: a plugin's own window, or a parameter panel for built-in effects. pt-BR strings added.

## 5. Not done / limits

- MIDI between nodes and out of the Patcher ("MIDI port N" pins), MIDI into effect plugins.
- Published parameters: FL's `1100` records become `PatcherDoc::published` (Patcher parameter k → node parameter,
  with the value map), and an imported automation of the Patcher slot's parameter k drives it (the rack binds it like a
  plugin parameter); LYNX's editor cannot publish new ones yet. Pattern-event automation of a Patcher still drops
  (FL's raw range of Patcher parameters not known). Control smoothing inside FL not traced (§1).
- Instrument nodes have no editor in the Patcher beyond a plugin's own window (built-in instruments keep their
  imported settings).
- Extra outputs of an *effect* Patcher: only the first one reaches an insert.

## Where I stopped

Runtime, import (graph, positions, control cables, side-chain, extra outputs, published parameters), save/load,
editor (`--patchershot prefix [project]` renders every Patcher's editor to PNG), selftest round trip and tests are in.
Next candidates: MIDI between nodes and the "MIDI port N" outputs, publishing parameters from LYNX's editor, a
parameter panel for instrument nodes, more than one extra output on an effect Patcher.
