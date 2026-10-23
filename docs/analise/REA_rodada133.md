# Round 133 (REA + features) — issue #3: MIDI CC automation of plugins, built-in controllers, MIDI Out

Issue #3 (Ivan-Spoda) asks for FL's wrapper "MIDI CC" automation targets (VSL Synchron Pianos exposes few parameters
but answers CC64/CC1/CC11), MIDI Out routed to a plugin's MIDI input port, Patcher audio/MIDI I/O, the Formula
Controller (mixer) and the Envelope Controller (channel). This round studied FL's own binaries (clean-room: behaviour
and formats only, no code copied) and implemented what LYNX was missing.

Evidence markers: **[DECOMP]** FL's code read in Ghidra (headless, own project), **[DATA]** FL's preset/demo bytes,
**[DOC]** FL's manual, **[HYP]** inference.

---

## 1. MIDI CC / aftertouch as plugin "parameters" (FL's wrapper)

- The wrapper's `ProcessParam(index)` (REA_rodada22 §2.1) already told the numbering: **0x1000 + n = MIDI CC n**
  (n = 0…127) and **0x1080 + c = channel pressure of MIDI channel c + 1**; plugin parameters stay 0…n−1
  **[DECOMP, round 22]**. The issue's screenshot (VSL: "951/1094 MIDI CC #0 (Bank select MSB)" … "1094/1094 MIDI
  Channel 16 Aftertouch") shows the same list as the browser sees it: the plugin's 950 parameters, then 128 CCs, then
  16 aftertouch entries, with the GM names of the CCs **[DOC: issue screenshot]**.
- A `.flp` automation of such a target is a `controlID` `channel<<16 | 0x8000 + index`, so it reaches LYNX's
  `channelParam` with `param = 0x1000 + n` unchanged (test `flp_wrapper_midi_cc_automation_keeps_its_index`).
- LYNX (`core/MidiControls.h`): the same indices, plus **0x1090 + c = pitch bend of channel c + 1** (LYNX only; FL
  bends through the channel's pitch knob). `PluginInstrument` / `PluginEffect` turn a value change on those indices
  (automation clips, pattern event lanes, MIDI links, the UI) into one MIDI message before the next block, only when the
  7-bit (14-bit bend) value changes; nothing is written to the plugin's real parameters. The VST3 slot remap of an
  imported project leaves these indices alone.
- UI: every plugin's parameter menu (Channel Rack › channel menu, mixer slot menu) got **Automate MIDI controller (CC,
  aftertouch, pitch bend)** with the CCs in groups of 32 and their GM names, a "Common controllers" group (1, 7, 10,
  11, 64, 66, 67, 74), channel aftertouch and pitch bend; each item has Create automation clip / Edit events / Link to
  MIDI controller / Link to internal controller. The plugin parameter list itself is now paged by 64 and numbered (the
  old menu stopped at 512 entries: VSL's CCs were never reachable).

## 2. Envelope Controller

- Format and behaviour were known (REA_rodada15 §10) and imported already (read-only panel). New: the channel is
  **editable and can be created** (Channel Rack › + › Envelope Controller). Its 8 outputs each have an envelope
  (points, sustain point, curves), base, amount, bipolar/unipolar, seconds/beats and a key range.
- LYNX saves its own state (`"LYEC"`, version 1, `writeEnvelopeController`) in `ChannelDef::controllerState`; FL's
  bytes still load, and an imported controller converts to LYNX's state on its first edit (FL's LFO and maps, which
  LYNX doesn't play, are dropped then) **[HYP: acceptable, they were already ignored]**.

## 3. Formula Controller **[DECOMP `Fruity Formula Controller_x64.dll`]**

- **State** (the plugin's `0xD5`, all 20 factory presets parsed) **[DATA]**: `i32 version` (1, 2 or 4), `3 × i32`
  knobs a, b, c (0…65536), the formula as a **short string** (`u8 length` + ANSI), then the description (v ≤ 2:
  `i32 length` + RTF; v 4: UTF-16 text). LYNX reads version/knobs/formula, writes version 2 with an empty
  description.
- **Language**: the plugin embeds a generic expression parser (`TExpressionParser`); `FUN_006cf080` registers the
  operators with their arity and precedence and `FUN_006d1d80`/`FUN_006d2160` the plugin's own functions:
  - precedence 10: `!` (factorial of `Round(x)`, recursive while x > 1.1), `%` (× 0.01), unary `-` and `+`;
    20: `^` (Power); 30: `* / div mod fmod`; 40: `+ -`; 50: `> >= <= < <> = in`; 60: `not`; 70: `or and xor shr shl`.
  - `=`/`in` = `|a − b| < 1e-30`, `<>` the opposite, `>=` = `b ≤ a + 1e-30`, `<=` = `a ≤ b + 1e-30`; `div`/`mod`
    and `and or xor shl shr not` work on `Round()`ed 64-bit integers; `Round` is the FPU's round-half-even; `if(a,b,c)`
    = `Round(a) ≠ 0 ? b : c`; `Case(a,b,c)` = `a = 1.0 ? b : c`; `fmod(a,b)` = 0 when b = 0, else `a − Trunc(a/b)·b`.
  - `Inter(a,b,c)` = `Round(1000a)` within `[Round(1000b), Round(1000c)]` (b and c swapped when reversed),
    `Internoswap` without the swap; `Map(a,b,c)` = `a(c − b) + b`; `Fold(x)` folds `2x − 1` with a period-4 triangle
    and maps back to 0..1; `Warp(a,b)` adds b when a is outside 0..1 then takes `fmod(·, 1)`; `Sec`/`CoSec` return 1
    when the cosine/sine is exactly 0.
  - `Tension(x, r)` (single precision): r = 0 → x; r < 0 → `((1 − r)^x − 1)/(−r)`; r > 0 → `(r − ((1 + r)^(1−x) − 1))/r`.
  - `SeededRand(x)` = Numerical Recipes' **psdes** hash of the 64 bits of the double x (tables `0x71b9cc`/`0x71b9dc`
    = the psdes constants), mantissa → float in [0, 1). `Rand` = Delphi's `Random` (LCG 134775813·s + 1).
  - `SongTime` = the host's song position in ticks × 1/PPQ (quarter notes, whole ticks); `GetTempo` the host tempo;
    `Time` ms; `Date` days since 1899-12-30; `MouseX/Y` = cursor position over the screen size.
- **Output**: clipped to 0..1 **[DOC]**; LYNX evaluates once per audio block **[HYP: FL's refresh rate not isolated]**.
- LYNX: `EffectKind::formulaController` (`core/FormulaController.h`), knobs a/b/c are its parameters 0–2 (automatable,
  linkable), the formula lives in the slot's `state` (FL's bytes). Its editor: knobs, formula box with Compile and
  status, Examples menu, monitor. Imported from `.flp` (state + links `0x70000000 + track<<22 + slot<<16 + 0x8000`,
  REA_rodada14 §7.1) instead of being dropped as "silent".

## 4. MIDI Out → a plugin's MIDI input port

- Format and routing rule from REA_rodada16 §1 **[DECOMP + DATA]**: MIDI Out state `channel @4`, `port @24`, program
  `@30`; the wrapper's record `0x01` holds the plugin's `MIDI input port`; FL delivers the MIDI Out's messages to every
  plugin whose input port equals the port, with the MIDI Out's MIDI channel.
- Before this round LYNX copied the MIDI Out's notes into the target channel at import (static). Now:
  `InstrumentKind::midiOut` (silent), `PluginRef::midiInPort` (saved; imported from the wrapper), and the renderer
  routes the MIDI Out's notes live (patterns, Playlist, the keyboard) to the plugins on its port with
  `noteOnMidi/noteOffMidi` on the MIDI Out's channel; its CC/aftertouch/bend "parameters" (automation, links) reach the
  same plugins.
- UI: Channel Rack › + › MIDI Out (new ones take the next MIDI channel, skipping 10 as FL), its editor (channel, port,
  program, "Send to a plugin...", "Automate MIDI controller..."), and the plugin's Processing › MIDI input port.
- Program and bank (when set) are sent to the plugins on its port when playback starts **[HYP: FL sends them on
  reset/start, REA_rodada16 §1.3]**.
- Not yet: FL's 72 MIDI Out knobs/pages, MIDI Out to effect slots.

## 5. Patcher audio + MIDI I/O

Not started this round (lowest priority in the brief). LYNX still flattens imported Patcher graphs to series/parallel
containers (REA_rodada14/20); a user-editable Patcher with audio and MIDI pins is the next step.

---

## Where I stopped

- Landed: MIDI CC/aftertouch/pitch bend targets, Formula Controller, editable Envelope Controller, controller links
  from any knob, MIDI Out routing and the MIDI input port (see the commit list in the round report).
- Next: Patcher with audio/MIDI I/O; MIDI Out's knob pages; MIDI input port on mixer
  effects (Pitcher's own port, p33); FL's Envelope Controller LFO and maps; measuring FL's Formula Controller update
  rate (per tick vs per block).
