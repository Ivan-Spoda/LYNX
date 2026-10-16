# Round 83: Piano Roll tools (quantizer, chopper, flamer, LFO, claw machine, articulator, riff machine, chord progression) and the Event editor

Branch `feature/tools83`. English, clean-room: FL Studio's code was decompiled to learn **what** each tool does
(formulas, ranges, defaults); LYNX's code is its own and no Image-Line code or pseudocode is in the repository.
FL's score templates (`.fsc`) and chord progression presets are **read at run time from the user's installation**
(like GMS samples); nothing of them is committed.

Evidence markers: **[DECOMP]** decompiled/disassembled FL code, **[DATA]** Delphi form resources / installation
files, **[model]** our reading where the code alone does not settle it, **not confirmed** where unknown.

- Target: `FLEngine_x64.dll` of FL Studio 2026 (same build as rounds 69/72/76). Tools: Ghidra 12.1.4 headless on a
  copy of round 76's analysed project (`~/ghproj/r83`), our scripts `gh/scripts/DecAt.java`, `Xr.java`; our Python
  readers in `~/.claude/jobs/f8789b49/tmp/r83/py/`: `dfm.py` (binary DFM, all properties), `vmt.py` (Delphi
  published method table → handler addresses), `fields.py` (published field table → component offsets),
  `vdiff.py` (virtual slots a form overrides). Decompilations: `gh/d*.c, q*.c, s*.c, l*.c, t*.c, lg*.c, rm.c`.

## 0. How FL's tool forms are built [DECOMP]

All tools derive from `TPRBaseToolForm`. Each control's `OnChange` stores the values and calls the base
"update" (`0xdd2410`), which re-runs the tool's virtual **Process** (VMT slot `+0x390`) on a copy of the original
notes: a live preview; Accept keeps it. Other overridden slots: `+0x388` save state, `+0x398` init (e.g. next free
note group), `+0x3a0` reset to defaults, `+0x3a8` randomise ("throw dice" in the riff machine). A tool processes
the selected notes if there is a selection, else all notes (`note+0x13 & 0x80` against the form's selection flag).
Integer helpers: `FUN_0040c660` = round half to even (x64 `cvtsd2si`), wheel strengths are 0..128 with
`(a·s) / 128` rounding toward zero.

Tick units FL keeps for the current PPQ (static values at PPQ 96): `0x15d89c0` = 96 (one beat), `0x15d89c4` = 24
(one step), `0x15d89c8` = 96 (one beat), `0x15d89cc` = 4 (minimum note length, 1/24 beat) **[model: values at
PPQ 96; the writers were not traced]**.

## 1. Quantizer — `TPRQuantizeForm` (Process `0xbd4bb0`, per note `0xbd47e0`)

- [DATA] Controls: template (a score `.fsc`, default `Quantization\Default.fsc` = sixteen 1/16 notes), Start time
  strength 0..128 (default 64), Duration strength 0..128 (64), Sensitivity 12..192 (36), duration mode
  {Leave duration, Leave end time, Quantize duration, Quantize end time} (default 0), level strengths for pan,
  velocity, release, mod X, mod Y, pitch 0..128 (128).
- [DECOMP] The template repeats with period P = its length (end of the last note, a note counts at least 1 tick)
  rounded **up to whole beats**. Nearest template point to time t: search the template notes in the periods
  before, at and after `t div P`; candidate = period start + note start (+ note length − 1 when matching an end);
  keep the one with |t − candidate| **strictly** below the window (sensitivity × step / 48). No candidate: t stays.
- [DECOMP] start += (target − start) · strength / 128. Mode 1: keep the end (length = old end − new start + 1,
  minimum length); mode 2: length moves toward the matched template note's length; mode 3: the end moves toward
  the nearest template end (search with "end" points), then length = end − start + 1.
- [DECOMP] Levels (only with a matched template note): velocity target = template velocity × note velocity / 100;
  other levels target = template value + (note value − the level's default); value += (target − value) · strength
  / 128, clamped. A note whose velocity becomes 0 is removed.

## 2. Chopper — `TPRSliceForm` (Process `0xdea9e0`, pattern load `0xdea500`)

- [DATA] Controls: pattern (`Chopping\Default.fsc`), Time mul 1..7 → ×{1/8, 1/4, 1/2, 1, 2, 3, 4} (table
  `0x1527984`, default 4 = ×1), Absolute pattern (default on), Group notes, level strengths (default 128).
- [DECOMP] Loading: positions and lengths × time mul (rounded, length ≥ 1); period = the pattern's length × mul; a
  note of colour 12 sets the period to its (scaled) length and is dropped.
- [DECOMP] Each note (length > 0) is replaced by the pattern repeated from the note start (Absolute: from the
  previous period boundary of the song, so the pattern stays on the song grid) up to the note end. Each piece:
  length = min(pattern note length, note end − piece start); a pattern note of colour 4 lasts to the note end in
  the first round and is dropped in later rounds; pieces before the note start are cut at it; key = note key +
  pattern key − 60 (clamped 0..131); slide flag from the pattern note; levels blended like the quantizer.

## 3. Flamer — `TPRFlamForm` (Process `0xdebce0`)

- [DATA] Time −192..192 (default 192), Velocity 0..128 (85), Absolute, Before. Presets (table `0x1527c00`):
  Flam (−64, 85, absolute), Early flam (−64, 85, absolute, before), Double stroke (192, 85, relative),
  Early double stroke (−192, 85, relative, before).
- [DECOMP] Offset T: relative = time × step / 192 (integer); absolute = round(time × 100/192 ms × BPM × PPQ /
  60000). When `before ≠ (T < 0)` the original moves to start − T, else it stays; the added note starts at
  (original position) + T. Both are clamped at 0. The added note (only if it lands elsewhere) gets velocity ×
  velocity wheel / 128; if it comes first, it is shortened so it ends where the original starts.

## 4. LFO — `TPRLFOForm` (Process `0xde87c0`)

- [DATA] Start/End boxes: Value 0..65536 (32768), Range −65536..65536 (16384), Speed 0..40960 (20480); End
  enabled; "log" speed; Shape {Sine, Triangle, Square}; Phase 0..65535 (32768).
- [DECOMP] Speed exponent e = 7 − speed / 4096; period (ticks) = max(1, beat × 2^e) **[model: the exponent base
  is a run-time double, 2 matches the speed menu "1 step … 512 steps"]**. Over the time range [a, b), per tick:
  φ ← frac(φ); wave = sin(2πφ) | 1 − |4φ − 2| | 1 − 2·round(φ); value = min + (V + p·ΔV)·k + wave·(R + p·ΔR)·k
  with k = (max − min)/65536, p = (t − a)/(b − a); clamp; then φ += 1/period, and the period (or, with "log", the
  exponent) moves linearly toward the end value. Initial φ = phase/65536 + {0.25, 0.75, 0.5} by shape. In the
  Piano Roll each note gets the value at its start tick (notes before a get the value at a).

## 5. Claw machine — `TPRTripletForm` (Process `0xdef770`, time warp `0xdef670`)

- [DATA] Period {Beat, 2, 4, 8, 16 beats}; Trash every {1/4, 2/4, 3/4, 4/4, 1/3, 2/3, 3/3, 1/2, 2/2, (none)}
  (Reset: 4/4); Time dist −1024..1024 (0); Stretch to compensate (on); Remove short notes (on).
- [DECOMP] Slice s = P / n. Positions are rotated by k slices (k = n − a for "a/n") so the trashed slice is the last
  of the period; a note starting in it moves to the next period, an end inside it is pulled back; then every whole
  trashed slice before/inside the note is removed (start −= (start div P)·s, length −= ((length + start mod P)
  div P)·s) and the rotation is undone (negative starts are cut at 0).
- [DECOMP] Then a time warp: t' = t × (stretch ? n/(n−1) : 1); inside each period P the fraction f is bent by
  c = sign(d)·(101^|d| − 1)/2 (d = dist/1024): c > 0: f ← 1 − ((|c|+1)^(1−f) − 1)/|c|; c < 0: f ← ((|c|+1)^f − 1)/|c|;
  result rounded. Start and end are warped; length ≤ 0 (or below the minimum with "Remove short notes") deletes
  the note, else length ≥ minimum.

## 6. Articulator — `TPRLegatoForm` (Process `0xdd3550`)

- [DATA] Multiply 10..100 % (100), Variation 0..100 % (0), Gap 0..240 (0, in 1/960 beat), Seed, Use lengths, Chop
  chords, Only with selection. Presets: Legato 100 %, Portato 75 %, Staccato 20 % (table `0x15269e4`), Ensure small
  gap (100 %, gap 20, use lengths), Just chop chords.
- [DECOMP] Chop chords: at every distinct note start, notes that started earlier and are still sounding are cut
  there and continued by a copy. Then each note's room d = distance to the next note that starts at least the
  minimum length later; the last notes: their own length if they end on a bar line, else up to the next bar line
  **[model: `0x1062fd0` read as "next bar"]**. Length = min(base × multiply, d − gap) with base = d (or the note's
  own length with "Use lengths"), minus rand·variation·length (MT19937 seeded with the seed, rand = u32 / 2³²),
  rounded, at least the minimum. The notes of the last chord all take the last note's length.

## 7. Riff machine — `TPRScoreCreatorForm` (Process `0xdd1290`)

- [DECOMP] A chain of the other tools embedded as 8 steps (ids at `0x152678c`): Prog = chopper (patterns
  `Chopping\Riff machine step 1\`), Chord = chopper (`…step 2\`), Arp = arpeggiator (`Arpeggiator\Riff machine\`),
  Levels = randomizer, Mirror = flipper, Art = articulator, Groove = quantizer (`Quantization\Riff machine\`), Fit =
  limiter. Unless "Work on existing score", the pattern is replaced by one note of Length bars at key 48 before the
  steps run. "Throw dice" calls each tool's randomise slot (e.g. chopper: random pattern, time mul ×{1/2, 1, 2},
  random level strengths 64..128).
- LYNX builds the same chain from its own tools (its arpeggiator, randomiser, flip and limiter are LYNX designs).

## 8. Chord progression — `TPRChordToolForm`

- [DATA] The tool is a Python package (`System\Tools\Chord progression\*.pyc`: Markov model, rhythm, performance
  patterns) with presets in `Chords\presets.cpt` (Roman numerals, e.g. "I-V-vi-IV: Hopeful") and
  `rhythm_presets.cpt` (`>` hit, `-` hold, `` ` `` rest). The model was **not** decompiled; LYNX's generator is its own
  design (a small table of common diatonic moves, an "adventurous" control) and reads the installation's preset
  files at run time when present.

## 9. Event editor — `TEventEditForm`

- [DATA] FL's event editor is the Piano Roll form in "event" mode with Draw/Paint/Delete/Select/Zoom tools, a target
  control selector and step editing. FL stores pattern events as `.flp` event `0xDF` per pattern: 12-byte records
  {u32 tick, u16 parameter, u16 target, i32 value} (12 demo projects use it). The value scale is the parameter's own
  (e.g. 0..12800 for volumes) and the mixer target word is not decoded: **import not done** (not confirmed).

## Where I stopped / next steps

- (in progress) core algorithms + dialogs, then the Event editor.
