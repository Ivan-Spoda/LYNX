# Parity audit: LYNX's "inspired by" plugins against the originals

Rounds 128–129 (2026-10-21). The user's question: "people tell me details are missing in the plugins — is it
configuration or design?" Round 114 scored only the layout. This audit checks **everything**, item by item:

- **Configuration**: every control, mode, menu option, switch, range / unit / default, per-band option, context
  menu, mouse gesture, side chain, oversampling / quality, latency, A/B, undo / redo, presets, MIDI learn, analyser
  options, meters, window size, help.
- **Design**: what is shown where (display contents, readouts, labels, sections, proportions), drawn in LYNX's own
  style (skin122 / skin126 own the look; this audit owns the *what* and *where*).

Sources: each original's official trial installed on this PC, hosted black-box in our own host (parameter lists read
through the public VST3 API — every control, range and option text — plus screenshots of each view as a user sees
it), the official manuals, and `docs/specs/*.md`. Nothing was decompiled; no graphics, presets or names were copied.
Reference screenshots stay outside git (`D:\lynx-work\parity128\ref`); the side-by-side pages are on the Desktop
(`LYNX-paridade`).

Legend: ✅ same · ⚠️ partial (note says what differs) · ❌ missing · — not applicable / deliberately left out.
Scores are 0–10 (10 = a user of the original finds every control and every piece of information where they expect).

Round 128 covers the FabFilter / Slate / Antares roles (below). Round 129 covers the Waves / SSL / Valhalla roles
(its own section).

---

# FabFilter / Slate / Antares roles (round 128)

| LYNX engine | Original (trial here) | Config before | Design before | Config after | Design after |
|---|---|---|---|---|---|
| Facet EQ | Pro-Q 4 (v4.1.3) | 6 | 8 | … | … |
| Apex Limiter | Pro-L 2 (v2.2.6) | 4 | 7 | … | … |
| Crest Compressor | Pro-C 3 (v3.0.2) | 6 | 7 | … | … |
| Prism Multiband | Pro-MB (v1.3.3) | 4 | 6 | … | … |
| Sentry Gate | Pro-G (v1.4.2) | 6 | 7 | … | … |
| Hush De-esser | Pro-DS (v1.3.2) | 4 | 6 | … | … |
| Ember Saturator | Saturn 2 (v2.1.3); Decapitator role (not installed) | 3 | 6 | … | … |
| Aether | Fresh Air (v1.1.7) | 8 | 8 | … | … |
| LYNX Tune | Auto-Tune Pro (v11.6, Auto mode) | 5 | 6 | … | … |

The "after" columns are filled in as round 128's batches land (see the per-plugin tables and
`docs/analise/REA_rodada128.md`).

## Shared by every editor (the frame around the controls)

All five FabFilter originals share one header and one bottom bar; Fresh Air and Auto-Tune have their own versions of
the same things. This is where most of the "missing details" were: things a user touches before any knob.

| Original feature | Where in the originals | LYNX before | Note |
|---|---|---|---|
| Undo / Redo buttons | header (FabFilter, Fresh Air, Auto-Tune) | ⚠️ | edits were undoable only from the main window's history (Ctrl+Z did nothing inside an editor window) |
| A/B compare + Copy (A→B) | header (FabFilter: A/B + Copy; Fresh Air: A > B) | ❌ | |
| Preset browser: name, previous / next, menu, save | header (all three) | ❌ | own engines had no presets at all (the generic editors only list FL presets) |
| Help | header (FabFilter "Help", Auto-Tune ⚙ / manual) | ⚠️ | the header shows a one-line gesture hint; no help menu |
| Resizable / full-screen window | FabFilter ⤢ corner + full-screen button, Fresh Air corner | ⚠️ | round 128: every display-first editor window resizes (70–200 %, scaled); Facet resizes with its own layout; no full-screen button |
| MIDI Learn | FabFilter bottom bar | ⚠️ | per control: right-click → "Link to controller" (LYNX's automation menu); no global learn mode |
| Right-click any control: type value / reset / automate | FabFilter: right-click menu | ✅ | right-click → Type value, Reset, automation / controller link |
| Double-click / Alt-click = reset, Ctrl-drag = fine, wheel | FabFilter | ✅ | same contract |
| Hover read-out of a value | FabFilter | ✅ | value tip while dragging / wheel; hint bar |
| Input / output level and pan (bottom bar "0.0 dB 0.0 dB") | FabFilter bottom bar | ⚠️ | in / out levels on some engines (see each table); no pan |
| Global bypass | FabFilter bottom bar (⏻), Fresh Air POWER, Auto-Tune ⏻ | ✅ | the slot's power LED in the header (Aether: the POWER button) |
| Oversampling menu | bottom bar | ⚠️ | per engine (see tables) |

## Facet EQ ← Pro-Q 4

Parameters of the original: 24 bands × (used, enabled, frequency 10 Hz–30 kHz, gain ±30 dB, Q 0.025–40, shape
(Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass), slope 0–96
dB/oct + Brickwall, stereo placement (Left, Right, Stereo, Mid, Side), dynamic range, dynamics on / auto, threshold,
attack, release, external side chain, side-chain filtering, spectral mode, density, tilt, solo) + processing mode,
resolution, character, gain scale, output level / pan / pan mode, phase invert, auto gain, analyser options, display
range.

| Original feature | LYNX | Note |
|---|---|---|
| Full-window spectrum + EQ curve, nodes on the curve | ✅ | |
| Double-click empty space = new band; drag = frequency / gain; wheel = Q | ✅ | |
| Bands | ⚠️ | 8 bands (original: 24) |
| Shapes | ⚠️ | Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf (no Flat Tilt, no All Pass) |
| Slopes | ⚠️ | 6–96 dB/oct; no 0 dB/oct, no Brickwall |
| Floating band panel: shape, slope, FREQ, GAIN, Q, band ◀ n ▶, close | ✅ | |
| Band panel: power button (bypass one band) | ✅ | round 128: the band's coloured power button (also in the node tip and the band menu); a band switched off keeps its settings and shows a hollow node |
| Band panel: stereo placement (L / R / Stereo / M / S) | ✅ | round 128: placement button in the panel and the band menu, an L / R / M / S tag beside the node; in the natural / linear phase modes a placed band runs minimum phase after the FIR |
| Dynamic EQ: range, threshold, attack, release | ⚠️ | attack / release are global (original: per band); no "auto" threshold |
| Dynamic EQ: external side chain per band, side-chain filtering | ⚠️ | one detector switch (input / side chain) for all bands |
| Spectral dynamics (density, tilt) | ❌ | Pro-Q 4 feature |
| Solo band (audition) | ✅ | |
| Hover tooltip next to a node (freq, gain, Q, power, solo) | ✅ | read-out box at the node with power (round 128), solo and remove |
| Right-click a node: shape, slope, dynamic range, solo, reset, remove | ✅ | |
| Processing: Zero Latency / Natural Phase / Linear Phase | ✅ | |
| Linear-phase resolution | ❌ | fixed |
| Character (Clean / Subtle / Warm) | ❌ | |
| Gain scale | ✅ | |
| Output level | ✅ | |
| Output pan / pan mode, phase invert | ✅ | round 128: PAN in the bottom bar (left / right or mid / side), polarity in OUT's right-click menu |
| Auto gain | ✅ | round 128: bottom-bar button; takes back the mean level change of the static curve |
| Analyser: off / pre / post / pre+post | ✅ | |
| Analyser range, resolution, speed, tilt, freeze, collisions, spectrum grab | ⚠️ | round 128: range 60 / 90 / 120 dB, speed (5 steps), tilt 0 – 6 dB/oct, freeze in the analyser menu (saved with the slot); no resolution, collisions or spectrum grab |
| Display range 3 / 6 / 12 / 30 dB | ✅ | saved with the slot since round 128 |
| EQ match (external spectrum) | ❌ | |
| Piano display (note names on the frequency axis) | ✅ | piano strip toggle at the bottom left; note name of the mouse position |
| Output meter with peak hold | ✅ | |
| Window size | ✅ | resizable |

## Apex Limiter ← Pro-L 2

Parameters of the original: Gain 0–30 dB, Style (Transparent, Punchy, Dynamic, Allround, Aggressive, Modern, Bus,
Safe), Lookahead 0–5 ms, Attack 0–10 s, Release 0–10 s, Channel Link Transients / Release 0–100 %, Oversampling
Off–32x, True Peak Limiting, Dithering Off / 16–24 bits, Noise Shaping (None, Basic, Optimized, Weighted), Filter DC
Offset, Side Chain Triggering, Unity Gain, Audition Limiting, Output Level −30…0 dBTP, meter scale (−16/−32/−48 dB,
K-12/14/20, Loudness), loudness meters (momentary / short term / integrated, target, recording).

| Original feature | LYNX | Note |
|---|---|---|
| Display = input / output level / gain reduction history, biggest reductions tagged | ✅ | |
| GAIN slider on the display's left edge | ✅ | |
| Output (ceiling) line, draggable | ✅ | |
| Style button / menu | ✅ | 8 styles (round 128: Clean, Glue, Punch, Dynamic, Dense, Loud, Bus, Safe — LYNX's own names and release laws) |
| LOOKAHEAD knob | ✅ | 0–5 ms |
| ATTACK knob | ✅ | round 128: 0–10 s (how slowly the slow release stage engages; 0 = at once, the old behaviour) |
| RELEASE knob | ✅ | |
| CHANNEL LINKING: TRANSIENTS and RELEASE knobs | ✅ | round 128: two knobs; the release is linked at least as much as the transients |
| Knob panel folds away («) | ✅ | |
| ADVANCED panel (»): channel link details, DC offset filter, side-chain triggering | ⚠️ | round 128: » fold-out with DC offset filter, unity gain, audition limiting; no side-chain triggering |
| True Peak Limiting (bottom bar) | ✅ | TP button + bar menu |
| Oversampling (bottom bar) | ❌ | (the 4x true-peak estimator is the only oversampling) |
| Dither + noise shaping (bottom bar) | ✅ | round 128: Off / 16–24 bits, TPDF; None / Basic / Optimized / Weighted error-feedback shaping |
| Output level read-out (bottom bar "Out: 0.0 dBTP") | ✅ | |
| Unity Gain (monitor at the same loudness) | ✅ | round 128 (Advanced) |
| Audition Limiting (hear what is removed) | ✅ | round 128 (Advanced) |
| Meters: output peak bars with holds + gain-reduction meter | ✅ | |
| Meter scale menu (−16/−32/−48 dB, K-12/14/20) | ✅ | round 128: next to TP; rescales the display and the meters |
| Loudness meter (LUFS momentary / short term / integrated, range, true peak) | ❌ | |
| True-peak / sample-peak metering switch | ❌ | |
| Display mode (Slow Down / Fast / Slow / Infinite / Off) | ❌ | |
| External side chain | ❌ | |

## Crest Compressor ← Pro-C 3

Parameters of the original: Style (Clean, Versatile, Smooth, Punch, Upward, TTM, Op-El, Vari-Mu, Classic, Opto,
Vocal, Mastering, Bus, Pumping), Threshold −60…0 dB + Auto Threshold (+ lock), Ratio 1–100:1, Knee 0–72 dB,
Range 0–60 dB, Attack 0.005–250 ms, Release 10 ms–2.5 s + Auto, Lookahead, Hold, Character (Off, Tube, Diode,
Bright) pre / post + drive, Wet / Dry gain and pan, Auto Gain, side chain (Internal, External, Host Sync, MIDI; level;
host trigger sync / offset / length), stereo link 0–100 % + mode (Mid, Side, M→S, S→M), side-chain EQ (6 bands, 10
shapes), audition side chain / triggering, Mix 0–200 %, input / output level and pan, oversampling Off–32x, maximum
lookahead, meter scale, knee display, input meter.

| Original feature | LYNX | Note |
|---|---|---|
| Full-window level display (input, output, gain-reduction line, threshold) | ✅ | |
| Knee / transfer panel sliding out from the left | ✅ | |
| THRESHOLD, RATIO, style button, ATTACK, RELEASE knobs floating on the display | ✅ | |
| AUTO threshold | ❌ | |
| AUTO release | ✅ | |
| KNEE, RANGE, LOOKAHEAD, HOLD small knobs | ✅ | |
| Styles | ⚠️ | 11 (no Smooth, TTM, Op-El, Mastering under those names) |
| Character Off / Tube / Diode / Bright, pre / post, drive | ✅ | |
| AUTO GAIN | ✅ | |
| DRY knob (dry level, parallel compression) | ⚠️ | LYNX has MIX (0–100 %); the original has a separate DRY gain and Mix up to 200 % |
| Wet / dry pan (mid / side balance) | ❌ | |
| SIDE CHAIN panel: listen, level, stereo link, detector EQ with nodes | ✅ | 3 EQ bands (original: 6) |
| Side chain Internal / External | ✅ | |
| Side chain Host Sync / MIDI triggering | ❌ | |
| Stereo link mode (Mid / Side / M→S / S→M) | ❌ | |
| Audition triggering | ❌ | |
| Oversampling (bottom bar) | ⚠️ | up to 8x (original 32x) |
| Lookahead maximum (bottom bar) | ✅ | |
| In / out levels (bottom bar) | ✅ | no pan |
| Meter scale (9/18/36/72/90 dB) | ❌ | fixed |
| Input level meter switch | ⚠️ | IN meter always shown |
| Output + gain-reduction meters with holds | ✅ | |

## Prism Multiband ← Pro-MB

Parameters of the original: up to 6 freely placed bands × (state, low / high crossover + slope 6–48 dB/oct, dynamics
mode compression / expansion, threshold −90…0, range ±30 dB, ratio, attack %, release %, knee 0–48 dB, lookahead
0–20 ms, level ±30 dB, pan, side-chain filtering band / free + low / high, side-chain input, stereo link + mode,
solo / mute) + mix 0–200 %, input / output, processing mode (Linear / Dynamic / Minimum phase), oversampling,
lookahead on, expert mode, display range, analyser options.

| Original feature | LYNX | Note |
|---|---|---|
| Spectrum window with band regions and nodes | ✅ | |
| Click / drag in empty space = create a band anywhere; bands with gaps | ❌ | 4 contiguous bands (switching one on = "adding") |
| Up to 6 bands | ❌ | 4 |
| Node: drag up / down = range, left / right = move band; edges = crossovers | ✅ | |
| Wheel = threshold | ✅ | |
| S / M buttons per band | ✅ | |
| Floating band panel: power, THRESHOLD, RANGE, ATTACK, RELEASE, OUTPUT, mode, RATIO, KNEE | ✅ | |
| LOOKAHEAD slider per band | ⚠️ | one global lookahead |
| EXPERT (»): side-chain filtering, external side chain, stereo link per band | ❌ | |
| Band output pan (mid / side) | ❌ | |
| Crossover slope per band 6–48 dB/oct | ⚠️ | one global slope 12 / 24 / 48 |
| Processing mode Linear / Dynamic / Minimum phase | ❌ | |
| Oversampling | ❌ | |
| Display range 3 / 6 / 12 / 30 dB | ⚠️ | 6 / 12 / 30 |
| Analyser off / pre / post / pre+post | ✅ | |
| Analyser resolution / speed / tilt / freeze | ❌ | |
| Mix, In, Out (bottom bar) | ✅ | |
| Output meter | ✅ | |

## Sentry Gate ← Pro-G

Parameters of the original: Threshold, Ratio, Range, Style (Classic, Clean, Vocal, Guitar, Upward; Ducking as an
expert style), Attack 0–1 s, Release 0–5 s, Hold 0–250 ms, Knee 0–30 dB, Lookahead 0–10 ms + on, upward threshold /
ratio, side chain (normal / external, per-channel gain and L/R mix, high / low pass, audition), wet / dry level +
pan, oversampling Off / 2x / 4x, expert mode, channel mode L/R or M/S, input / output.

| Original feature | LYNX | Note |
|---|---|---|
| THRESHOLD big knob with RATIO / RANGE under it | ✅ | |
| Display: level history behind the transfer curve, threshold dashed | ✅ | |
| Style button (5 styles) | ✅ | |
| Ducking style | ❌ | |
| ATTACK, RELEASE, HOLD, KNEE, LOOKAHEAD (+ on) | ✅ | |
| EXPERT strip: side chain In / Ext, high / low-pass range slider, Audition | ✅ | |
| EXPERT: side-chain GAIN + L/R MIX (two channels) | ❌ | |
| EXPERT: WET + PAN, DRY + PAN | ⚠️ | Dry and Output levels; no pan |
| Channel mode Left/Right or Mid/Side (bottom bar) | ❌ | |
| Oversampling (bottom bar) | ❌ | |
| In / Out levels (bottom bar) | ⚠️ | Out only |
| Level meter | ✅ | |

## Hush De-esser ← Pro-DS

Parameters of the original: Mode (Single Vocal, Allround), Threshold −∞…0 dB, Range 0–24 dB, Band Processing (Wide
/ Split), Stereo Link 0–100 % + Mid / Side, Lookahead 0–15 ms + on, Audition Triggering, side chain normal /
external, High-pass frequency 2–20 kHz, Low-pass frequency 2–20 kHz, Audition side chain, oversampling, input /
output level and pan.

| Original feature | LYNX | Note |
|---|---|---|
| Scrolling waveform with the removed part, threshold lines | ✅ | |
| THRESHOLD and RANGE big knobs, listen (headphones) button | ✅ | |
| Frequency slider with TWO handles (high-pass and low-pass of the detection band) | ✅ | round 128: left = where the band starts, right = where it ends (far right = off), 2–20 kHz |
| Audition (hear the detection band) | ✅ | |
| MODE: SINGLE VOCAL / ALLROUND | ✅ | round 128 (Allround: the sibilance must also stand out from the full-band level); LYNX's high-pass / band detection stays in the bottom bar |
| WIDE BAND / SPLIT BAND | ✅ | |
| SIDE CHAIN In / Ext | ✅ | round 128: Ext picks the source track (menu) |
| STEREO LINK knob with MID switch | ✅ | round 128 |
| LOOKAHEAD knob 0–15 ms with power button | ✅ | round 128 (the look-ahead is the latency; the mixer re-plans its delay compensation) |
| DISPLAY button (hide / show the waveform) | ✅ | round 128: the window shrinks without it |
| Oversampling (bottom bar) | ❌ | |
| In / Out levels (bottom bar) | ✅ | round 128: In ±12 dB, Out ±12 dB (no pan) |
| Release | — | Pro-DS has no release control; LYNX keeps RELEASE as an extra (bottom bar) |
| Input meter with threshold arrow, reduction meter | ✅ | |

## Ember Saturator ← Saturn 2 (and Decapitator's role)

Parameters of Saturn 2: up to 6 bands × (style — 28: Subtle / Clean / Warm / Broken Tube, Subtle / Clean / Warm /
Old Tape, 9 amps, Subtle / Gentle / Heavy Saturation, 3 transformers, Smudge, Breakdown, Foldback, Rectify, Destroy —,
drive + drive pan, feedback amount + frequency, dynamics −1…+1, tone Bass / Mid / Treble / Presence ±24 dB, mix,
level, pan, enabled, solo / mute, crossover + slope 6–48 dB/oct) + global input / output gain and pan, mix, channel
mode L/R or M/S, processing mode (minimum / linear phase), high-quality mode, unrestricted feedback, and the
modulation system (6 XY controllers, 6 XLFOs with 16 steps, 6 envelope generators, 4 envelope followers, 10 MIDI
sources, 50 modulation slots).

| Original feature | LYNX | Note |
|---|---|---|
| Spectrum with bands side by side, split by clicking / double-clicking | ✅ | |
| Drag a band's handle = drive, drag a crossover | ✅ | |
| Up to 6 bands | ⚠️ | 4 |
| Floating band panel: power, style button, MIX, FEEDBACK + FREQ, DYNAMICS, big DRIVE, TONE faders, LEVEL | ✅ | round 128: the same order — power / solo / mute, style, MIX, FEEDBACK, FREQ, DYNAMICS, DRIVE, BASS / MID / TREBLE / PRESENCE faders, LEVEL |
| 28 styles in 6 families | ⚠️ | 4 families (tube, tape, transistor, fuzz) |
| Per-band tone: Bass / Mid / Treble / Presence | ✅ | round 128: ±18 dB (Saturn ±24) on the band's saturated signal — LYNX's own curves (shelf 120 Hz, bell 800 Hz, shelf 3.5 kHz, bell 6 kHz); the global tilt / cuts stay as LYNX extras in the bottom bar |
| Per-band power, solo, mute | ✅ | round 128 (panel buttons, tags on the display) |
| Drive pan (L / R), band pan | ❌ | |
| Feedback amount / frequency | ✅ | round 128: a band-passed loop around the shaper (10 Hz – 1 kHz, loop gain up to 0.9) |
| Dynamics (compress / expand the band) | ✅ | round 128: −1 … +1, the band's level follows its input level |
| Modulation (+ Add Modulation: XLFO, EG, EF, MIDI, XY, slots) | ❌ | |
| Crossover slope (bottom bar) | ✅ | global (original per band) |
| Linear phase / minimum phase | ❌ | |
| High Quality mode (oversampling) | ✅ | "Oversampling 2x / 4x / 8x" |
| Channel mode M/S | ❌ | |
| In / Out gain + mix | ✅ | round 128: In (±24 dB) in the bottom bar; Out; Dry / wet |
| Auto gain | — | LYNX extra (Saturn has none; Decapitator has Auto) |
| Decapitator: Drive, 5 styles (A, E, N, T, P), Low Cut, Tone, High Cut, Thump, Steep, Mix, Output, Auto, Punish | ⚠️ | trial not installed; Ember covers drive / tone / cuts / mix / auto gain, no Thump / Steep / Punish |

## Aether ← Fresh Air

Parameters of the original: Mid Air 0–100 %, High Air 0–100 %, Bypass, Trim −12…0 dB.

| Original feature | LYNX | Note |
|---|---|---|
| Arc output meter on top | ✅ | |
| MID AIR and HIGH AIR big knobs | ✅ | |
| Link button between them | ✅ | |
| POWER (bypass) bottom left | ✅ | |
| TRIM small knob bottom right | ✅ | |
| Preset menu, save, undo / redo, A > B | ❌ | (shared header, above) |
| Extra settings (exciter / shelf frequency, character, mix) | — | LYNX extras in the bottom bar |

## LYNX Tune ← Auto-Tune Pro (Auto mode)

Auto-mode parameters of the original: Correction mode, Scale (29 legacy + 15 modern), Key, Detune ±100 cents, Retune
Speed 0–400 ms, Humanize, Flex-Tune, Natural Vibrato ±12 dB, Tracking, Input Type (Soprano, Alto-Tenor, Low Male,
Instrument, Bass Inst.), Classic mode, Formant Correction + Throat Length 50–180 %, Transpose ±12 (+ in scale),
Wet-Dry Mix, Create Vibrato (shape, rate, pitch, amplitude, formant, variation, onset delay / rate), Ignore Vibrato,
Learn Scale from MIDI, Target Notes via MIDI, MIDI octaves, Latency Removal, per-note Bypass / Remove, keyboard
edits (Remove / Bypass, Latched / Momentary), Hold, Auto Key.

| Original feature | LYNX | Note |
|---|---|---|
| Top strip: Tracking, Input Type, Key, Scale, Formant, Transpose, Detune, Mix | ✅ | Detune shown as the A4 frequency (440 Hz ± 100 c) |
| Retune Speed, Flex-Tune, Humanize big knobs | ✅ | |
| Natural Vibrato knob (±12 dB) | ❌ | |
| Centre: correction ring with the note name and cents | ✅ | |
| Hold (freeze the read-out) | ❌ | |
| Scales | ⚠️ | 14 (chromatic, major, minor, 9 modes / pentatonics / blues, custom); no Whole Tone / Diminished, no historical tunings |
| Auto Key (detect the key) | ❌ | |
| Learn (scale from MIDI) | ❌ | |
| Keyboard at the bottom: click a note = remove / bypass, latch | ⚠️ | 12 target-note switches (remove only); no Bypass state, no keyboard strip |
| Per-note Bypass (pass uncorrected) vs Remove | ❌ | |
| Classic mode | ❌ | |
| Formant Correction on / off | ✅ | |
| Throat Length | ⚠️ | "Formant shift" in semitones instead of a throat length % |
| Transpose ±12 | ✅ | |
| Mix | ✅ | |
| ADVANCED: Create Vibrato (shape, rate, depth, onset), Ignore Vibrato, scale detune | ❌ | |
| Target notes via MIDI | ✅ | "MIDI channel" |
| Low latency | ✅ | |
| Pitch graph | ✅ | LYNX extra in Auto mode (input vs corrected) |
| Graph mode (offline editing) | — | out of scope (a separate editor) |
| Harmony Player | — | out of scope |
| Preset browser, undo / redo | ⚠️ | FL-style preset header (FL presets only); shared header adds LYNX presets |
