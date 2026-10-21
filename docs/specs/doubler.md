# Waves Doubler (Doubler2 / Doubler4) — spec dossier (LYNX substitute: Gemini Doubler)

Round 125 (spec125). Clean-room dossier: built from the public Waves Doubler user guide, public history of ADT and
early pitch shifters, the literature on delay-line modulation and overlap-add pitch shifting, and LYNX's own
black-box measurements of the official trial ([REA round 119 §4](../analise/REA_rodada119.md)). No Waves binary was
opened, decompiled or instrumented. Tags: [DOC] vendor document · [PAPER] academic · [PATENT] · [MEAS] measurement ·
[model] our inference. All web sources accessed 2026-10-21.

**One-paragraph summary.** Waves Doubler replicates the input into 2 or 4 "voices", each with gain, pan, delay
(0–100 ms), feedback, detune (±100 c), an octave-down switch and a triangle pitch LFO, plus a direct path, shelving
EQ on the voices only, a feedback high-pass and a Range switch (80 / 20 Hz) that trades the lowest cleanly shifted
frequency for the voices' inherent latency (7 / 24 ms) [S1][DOC]. Its voices are **duration-preserving pitch
shifters** (a "harmonizer"-style delay-line shifter), not modulated delays [S1][DOC]. Her setting, however, is not
a double at all: voices at 78.2 / 100 ms, feedback 39 %, −1 / 0 cents, hard left / right, direct aligned, EQ off,
FL slot mix 0.31 — a **stereo slap-back / ping echo** whose sound is decided by timing, loop gain, and the
**6.67 ms aligned direct mixed with FL's dry signal** (a comb filter: notches at 75, 225, 375 … Hz, ≈ −8.4 dB if FL's
mix is a linear crossfade) [MEAS][model]. Gemini already reproduces the measured laws (0.25–0.93 dB RMS per octave on
her takes); the remaining work is the pitch-synchronous octave (her second state), Doubler4's voices 3–4, and a few
documented-vs-measured discrepancies (LFO phase/period, Range 20 Hz latencies, shelf "cut" mode below −24 dB).

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves Audio Ltd — Doubler | [S1][DOC] |
| Components | Two-Voice (Doubler2) and Four-Voice (Doubler4), each Mono, Mono-to-Stereo, Stereo | [S1][DOC]; a 2010 user review also names "Doubler 2 and Doubler 4" [S14] |
| Concept (manual) | "replicates the incoming audio signal to another 2 or 4 voices. Each voice can be changed in Gain, Pan, Delay and Tune (Pitch)"; recreates sounds "popularized in the 1980's by the use of digital hardware units that were designed to create harmonies but were more commonly used for pitch-shifted doubling" | [S1][DOC] |
| Key difference (manual) | "the Doubler's ability to detune while preserving audio duration"; unlike phasers / flangers, "the sound is decorrelated by the initial detune" — the effect "resembles that of a doubled take" | [S1][DOC] |
| Release year | not verified (no primary source found) | — |
| Version she uses | saved under WaveShell1-VST3 16.0 / 15.5; measured with the 17.x trial (Doubler2 Stereo) | [round 119 §1, §5](../analise/REA_rodada119.md) [MEAS] |

### Lineage (history)
- **ADT (Artificial/Automatic Double Tracking)**, Ken Townsend, EMI Abbey Road, spring 1966, invented for the
  Beatles (first used on *Revolver*): a second tape machine whose speed was varied by an oscillator, delaying the
  vocal by a few milliseconds and mixing it with the original — slight, wandering time and pitch offsets produce the
  impression of a second take [S9][S10][DOC/secondary].
- **Eventide H910 Harmonizer** (1975; development from 1974): the first commercial digital pitch shifter, ±1 octave,
  up to 112.5 ms delay, feedback ("regeneration"); used as much for doubling as for transposition; audible splice
  glitches, largely cured by the H949 (1977) [S11][S12][DOC/secondary]. This is the "1980's digital hardware" class
  the Doubler manual refers to [S1][model].
- **Waves Doubler**: a software version of the harmonizer-style doubler: per-voice small detunes + short delays +
  pan [S1].

### Her usage
- Inventory: **701 slots in 399 projects, 2 distinct states** (`tmp/r113/inventory_all.txt`); round 119 counts 1969
  Doubler slots in the wider scan — the two counts come from different scans and are not reconciled here [MEAS].
- State **Doubler_0 / Doubler_1** (identical; her main one), read back from the trial (`tmp/w119/wv/Doubler_read.tsv`)
  [MEAS]: Master 0.0 dB · Feedback HiPass 16 Hz · Range 80 Hz · AlignDirect Yes · Direct On, 0.0 dB, 0.0° ·
  EQ Low Off (−28.7 dB @ 841 Hz) · EQ High Off (−13.7 dB @ 3538 Hz) · Voice1 On, 0.0 dB, −45°, **78.2 ms**, fb **39 %**,
  octave Off, **−1 c**, depth 0, rate 1.0 · Voice2 On, 0.0 dB, +45°, **100.0 ms**, fb **39 %**, octave Off, **0 c**,
  depth 0, rate 1.0 · Source1 L, Source2 R.
- State **Doubler_2** (the "octave double"): EQ Low On −17.7 dB @ 111 Hz, EQ High On +3.7 dB @ 3830 Hz; Voice1
  −45°, 7.3 ms, 0 %, **octave On**, +6 c; Voice2 +45°, 10.0 ms, 0 %, **octave On**, −6 c; others as above [MEAS].
- Placement: her vocal FX bus, FL slot mix **0.31** ([round 119 §4](../analise/REA_rodada119.md)) [MEAS].

## 2. Signal flow

From the manual [S1][DOC] and the round 119 measurements [MEAS]; structure details marked [model].

```
            ┌── Source k (own side / L+R = (L+R)/2 / L−R) ──► (+)──► pitch-shifting delay line k ──┬──► gain_k ─► pan_k (equal power ±45°) ─┐
in L/R ──┬──┤                                                 ▲   (delay_k, detune_k, octave_k,    │                                       │
         │  │                                                 │    LFO depth/rate, Range window)   │                                       ▼
         │  │                                                 └── fb_k × HP(feedback low cut) ◄────┘                         Σ voices ─► Low shelf ─► High shelf ─┐
         │  └── (k = 1…2 or 1…4)                                                                                                                              │
         └──► Direct: on/off, gain, pan (stereo rotation for stereo input), delayed by the voices' inherent latency when Align is on ──────────────────────────(+)──► Master gain ─► out
```

- EQ shelves act on the voices, not on the direct signal [S1][DOC].
- Feedback is "the percentage of gain to be fed back to the input" of the voice, with its own high-pass
  [S1][DOC]; measured as %/100 per repeat, each repeat taking the full voice delay [MEAS]. Whether the loop is taken
  after the shifter (repeats re-detuned, as in Gemini and the H910's regeneration) is [model] (§8).
- The plugin reports **no latency** to the host even with Align on [MEAS]; in FL the delayed direct is therefore
  mixed against FL's own dry signal at the slot mix (§4.6).
- No saturation, noise or analog stage is documented or observed [S1][MEAS].

## 3. Controls

Manual ranges/defaults [S1][DOC]; laws [MEAS] from [round 119 §4](../analise/REA_rodada119.md); RealWorld indices
[MEAS] from `tmp/w119/wv/Doubler_0_probe.txt` and `translateWavesDoubler` (`src/core/PluginSubstitutes.cpp`).

| Control | Range | Unit | Default | Law / taper | Audible effect | RealWorld index |
|---|---|---|---|---|---|---|
| Master Output Gain | 0.0 … −∞ | dB | 0.0 | dB | whole output incl. direct | 0 |
| Direct On/Off | on/off | — | On | — | | 1 |
| Direct Gain | 0 … −48 | dB | −12 (manual text copied from voice; unverified for direct) | dB | | 2 |
| Direct Pan | −45 … +45 | deg | — | **stereo rotation** for stereo input: −45°: L′ = (L+R)/√2, R′ = (R−L)/√2 [MEAS] | image rotation | 3 |
| Align Direct | on/off | — | Off | delays direct by the voices' inherent latency: manual 7 ms (80 Hz) / 24 ms (20 Hz); measured **6.67 ms = 320 samples @ 48 kHz** at 80 Hz | allows 0 ms relative delay | 4 |
| Low shelf On / Gain / Freq | on/off; +12 … −24 (below −24 "the filter floor drops to make a cut filter"); 16 Hz … ? | dB / Hz | On (manual) | first-order shelf (measured on the high shelf; low mirrored [model]) | voices only | 9 / 10 / 11 |
| High shelf Freq / Gain / On | as above | Hz / dB | On (manual) | first order: corners at f0 and f0·max(g, 1/g) [MEAS] | voices only | 12 / 13 / 14 |
| Hi-Pass Feedback | 16 … 12000 | Hz | 16 | order unknown | thins repeats | 15 |
| Source 1 / 2 | L (own side), L+R, L−R | — | own side | L+R = (L+R)/2; L−R = difference [MEAS] | | 16 / 17 (3/4: unknown) |
| Range | 80 Hz / 20 Hz | — | 20 Hz (manual) | sets the shifter window / inherent latency | lowest clean frequency vs latency | 18 |
| Modulation Reset (ResetLFO) | trigger | — | — | restarts all LFOs in sync | | 19 |
| (unlabelled) | values 64, 20, 32, −60, −40, −20 in her state | — | — | unknown (display ranges? voice 3/4 sources?) | — | 20–25 |
| Voice On/Off | on/off | — | On | — | | 26 + 10(k−1) + 0 |
| Voice Gain | 0 … −48 | dB | −12 | dB | | +1 |
| Voice Pan | −45 … +45 | deg | V1 22.5, V2 −22.5, V3 45, V4 −45 | **equal power**: −45 = left only, 0 = −3 dB each side [MEAS] | | +2 |
| Voice Octaver | on/off | — | Off | one octave **down** [S1][MEAS] | | +3 |
| Voice Detune | −100 … +100 | cents | — | exact (±100 c → −100.3 / +100.2 c measured) | "2 to 12 cents tend to sound relatively in tune" [S1] | +4 |
| Voice Delay | 0 … 100 | ms | 8 | ms; manual: minimum 8 ms (80 Hz) / 14 ms (20 Hz) when not aligned, 0 when aligned | | +5 |
| Voice Feedback | 0 … 100 | % | 0 | gain %/100 per repeat (39 % → −8.2 dB) [MEAS]; manual warns > 25 % | repeats | +6 |
| Voice Mod Depth | −200 … +200 | cents | 0 | triangle around the nominal detune; positive starts **upward** [S1]; ±depth measured | vibrato / chorus | +7 |
| Voice Mod Rate | 0.1 … 200 (manual) | "Hz" | 1 | manual: "At 1Hz the modulation will complete a full cycle in 1 second"; **measured period = 2 / rate s** (1.0 → 2 s, 6.3 → 0.32 s) | | +8 |

Voice blocks start at 26 (voice 1), 36 (voice 2); a Doubler2 state also carries blocks at 46 and 56 (in her state
pan −45 at 48 and +45 at 58, delay 100 at 51 / 61) — presumably voices 3 / 4 stored but unused by the two-voice
component [MEAS for the values; model for the meaning] (§8 item 6).

## 4. Known curves & behaviour

### 4.1 Pitch shifting
- Detune exact to ±0.3 c over ±100 c [MEAS]; octave = one octave down [MEAS][S1].
- Range: "Detuning lower frequencies requires higher inherent latency … specifies the lowest frequency that can be
  detuned with no artifacts"; 80 Hz → 7 ms, 20 Hz → 24 ms inherent latency [S1][DOC]. In a two-tap delay-line
  shifter the inherent latency is about half the window, so the windows would be ≈ 14 ms / 48 ms [model]; Gemini uses
  13.3 / 53.3 ms (the 53.3 ms is not backed by a measurement — the manual's 24 ms suggests ≈ 48 ms) (§8 item 2).
- The reference's octave is **pitch-synchronous** (no fixed-rate splice sidebands); Gemini's fixed-window octave puts
  energy on 75 Hz-spaced lines around the target [MEAS] ([round 119 §4](../analise/REA_rodada119.md)). §5.3 explains
  the 75 Hz (window 13.33 ms, two taps).

### 4.2 Timing (Range 80 Hz) [MEAS]
- Align on: direct delayed 6.67 ms (320 samples @ 48 kHz); every pass through a voice takes **delay + 10.3 ms**
  (including each feedback repeat). Relative to the delayed direct that is delay + 3.6 ms — the manual says aligned
  relative delay can reach 0 (§8 item 3).
- Align off: a voice takes its delay alone (contradicts the manual's 8 ms minimum unless the measured delays were
  all ≥ 8 ms; §8 item 3).
- Reported plugin latency: 0.

### 4.3 Modulation
Triangle LFO on pitch, amplitude ±depth cents around the nominal detune [MEAS][S1]; period 2/rate s [MEAS] (manual
says 1/rate). Positive depth starts moving upward from the nominal detune [S1][DOC] → LFO phase starts at the
centre, rising. Mod Reset re-syncs all voices [S1].

### 4.4 EQ
First-order shelves: a high shelf of gain g at f0 has corners f0 and f0·max(g, 1/g); −12 dB @ 3538 Hz gave −2.1 dB at
3.5 kHz, −6.8 at 8 kHz, −11.9 at 16 kHz [MEAS]. Low shelf assumed mirrored (its measurement was muted by the demo)
[model]. Below −24 dB the shelf turns into a cut filter ("filter floor drops") [S1][DOC] — not modelled in Gemini.

### 4.5 Levels and panning
Equal-power voice pan over ±45° [MEAS]; direct pan is a rotation for stereo sources [S1][MEAS]; source L+R is
(L+R)/2 [MEAS].

### 4.6 Her state, worked out [MEAS + model]
- Direct: 6.67 ms late, unity. FL's slot mix 0.31 then gives (if FL's mix is a linear dry/wet crossfade [model]):
  `0.69·x(t) + 0.31·x(t − 6.67 ms)` → comb with peaks 0 dB at multiples of 150 Hz and notches −8.4 dB at 75, 225,
  375 … Hz, plus the wet voices at 0.31. This comb is a major part of "her sound" and depends on the 320-sample
  alignment being exact.
- Voice 1 (left): first echo 78.2 + 10.3 = **88.5 ms** after the input, repeats every 88.5 ms at −8.2 dB.
- Voice 2 (right): first echo **110.3 ms**, repeats every 110.3 ms at −8.2 dB.
- Detune −1 c: ratio 2^(−1/1200) = 0.999423; a two-tap shifter with a 13.3 ms window sweeps its window once per
  ≈ 23 s (a tap crossfade every ≈ 11.5 s) — the voice is essentially a static delay with a very slow crossfade;
  0 c is a pure delay [model]. So her Doubler is matched by timing, loop gain, pan and the direct/dry comb; the
  shifter's quality hardly matters for Doubler_0.
- Doubler_2 (octave voices at 7.3 / 10 ms, ±6 c, shelves on) is where the shifter quality matters.

## 5. Published science

### 5.1 Variable delay → pitch (Doppler) [S5][S6][PAPER]
A read tap at time-varying delay D(n) samples: `y(n) = x(n − D(n))`, fractional delay by interpolation. For slowly
varying D the instantaneous frequency ratio is

    ρ(n) = 1 − dD/dn

(a delay that grows by g samples per sample lowers pitch by the factor 1 − g) [S6]. Chorus and doubling as
modulated short delays with interpolated taps are treated by Dattorro [S4] and Smith [S5]. A continuously modulated
delay changes pitch only while D changes — the "phaser/flanger" behaviour the Doubler manual contrasts itself with
[S1].

### 5.2 Fixed-window two-tap ("rotating head") shifter — what Gemini does
To hold a constant ratio ρ with bounded delay, let the tap delay ramp as a sawtooth over a window W:
`D_i(n) = D0 + W·φ_i(n)`, `φ_i(n+1) = frac(φ_i(n) + (1 − ρ)/W)`, with two taps `φ_2 = φ_1 + ½`, and crossfade
weights that vanish at the wrap: power-complementary `w_i = sin(π φ_i)` (Gemini) or amplitude-complementary
`w_i = sin²(π φ_i)` (sum 1). Output `y = Σ_i w_i · x(n − D_i(n))`. Mean added delay `D0 + W/2` (the inherent
latency ≈ half the window). The splice rate per tap is `f_s = |1 − ρ| / W` (W in seconds); with two taps the output
is amplitude-modulated at `2 f_s`, giving sidebands at ±2f_s, ±4f_s … around each partial, and comb filtering where
the two taps (W/2 apart) overlap on correlated material [model, derived from the equations]. Octave down (ρ = ½),
W = 13.33 ms: `f_s = 37.5 Hz`, `2 f_s = 75 Hz` — exactly the 75 Hz lines measured on Gemini [MEAS]. This is the
H910 splice problem; the H949 "de-glitched" it [S11][S12].
The choice of W sets the lowest frequency shifted cleanly: a window shorter than ~one period of the lowest partial
cannot keep its waveform intact — hence the Doubler's Range switch (80 Hz ↔ short window, 20 Hz ↔ long window)
[S1][model].

### 5.3 Pitch-synchronous / correlation-aligned splicing
- **SOLA** (Roucos & Wilgus 1985): overlap-add frames at the position (within a search range) that maximises the
  normalised cross-correlation with the output so far, so splices join in phase [S7][PAPER].
- **Lent 1989**: pitch shifting of quasi-periodic sounds by windowing one-period segments and re-spacing them at the
  new period, without resampling (formant-preserving) [S8][PAPER, via secondary citations].
- **PSOLA** (Moulines & Charpentier 1990): pitch marks at the period; Hann windows two periods long centred on the
  marks; synthesis marks at the new period; overlap-add (TD-PSOLA) [S13][PAPER].
For a delay-line doubler, the practical form is: keep the two-tap structure but, at each wrap, **jump the tap by an
integer number of detected periods** (or by the lag that maximises correlation between the outgoing and incoming tap
over the crossfade), so the crossfade joins in phase and no fixed-rate AM appears [model, combining §5.2 with S7/S8].
Note the musical difference: a delay-line shifter (and SOLA-splice variants) **transposes formants** with the pitch
(the H910 "chipmunk/Darth Vader" effect [S11]); PSOLA / Lent preserve the envelope. Which one Waves' octave does is
unknown (§8 item 1).

### 5.4 Why detune + delay "doubles"
Two copies with a fixed delay produce a static comb (flanging/phasing); a small constant pitch offset makes the
relative phase of each partial rotate at `Δf = f·(ρ − 1)` (≈ 0.58 Hz at 1 kHz for 1 c), so the comb pattern moves
and the copy is decorrelated — the ADT principle (wandering time and pitch offsets) [S1][S9][model].

## 6. What LYNX does today

- Role: `[waves-doubler]` → **Gemini Doubler**, `waves = Doubler; Doubler2; Doubler4`, `translate = waves-doubler`;
  every control of the two-voice Doubler is translated one-to-one; voices 3–4 of Doubler4 are dropped
  (`src/core/PluginSubstitutes.cpp`).
- Engine (`src/core/VoiceTools.cpp`, namespace `gemini`): two voices, each a §5.2 shifter (two taps, `sin(πφ)`
  power-complementary crossfade, 4-point Hermite-interpolated delay line), windows 13.333 / 53.333 ms for Range
  80 / 20 Hz; triangle LFO with period 2/rate starting at the bottom of the triangle (lfo = 0 → −1); feedback =
  `fb · HP(last voice output)` added to the voice input (HP: biquad, Q 0.707, at the feedback cut); equal-power pan;
  source own / (own+other)/2 / (own−other)/2; direct rotated and delayed sr/150 = 6.67 ms when aligned; voices'
  mean delay = delay + 10.3 ms when aligned, delay alone otherwise; first-order shelves (bilinear, pre-warped corners)
  on the voice sum; parameter ranges: gain −48…0, pan ±45, delay 0…100, detune ±100, depth ±200, rate 0.1…400
  (log), shelf −30…+12 dB, frequency 16 Hz…21.357 kHz (log).
- Residual on her takes, her two states, reference − LYNX: level −0.20…+0.39 dB; octave bands 0.25–0.93 dB RMS;
  Doubler_2 (octave voices) +2…+6 dB at 125 Hz on one take [MEAS]
  ([round 119 §4](../analise/REA_rodada119.md)).

## 7. Implementation guidance for LYNX (ordered)

1. **Protect her main state (Doubler_0) first — it is timing, not pitch.** Verify with impulses that Gemini's
   direct is exactly 320 samples late at 48 kHz (and sr/150 at 44.1 / 96 kHz — confirm the reference scales by time,
   §8 item 4), voice 1 echoes at 88.5 ms, voice 2 at 110.3 ms, repeats at the same spacing at −8.2 dB, and that a
   0 c voice is a pure delay (two-tap phase must sit with one tap at weight 1 — reset φ to 0 when detune is exactly 0
   and depth 0, else a static two-tap comb appears). Pass: impulse arrival within ±1 sample, repeat levels within
   ±0.1 dB, and FL-mixed (0.31) output octave bands within ±0.3 dB of the reference.
2. **Pitch-synchronous octave (Doubler_2)**: keep the delay-line shifter but replace the blind wrap by a
   correlation-aligned splice: when a tap reaches the end of its window, choose its jump L within
   [W/2 − T_search, W/2 + T_search] maximising normalised cross-correlation between the two taps over the crossfade
   region (SOLA, §5.3), or use an f0 tracker (LYNX Tune's detector) and jump by k·T0. Use amplitude-complementary
   (`sin²`) fades once splices are in phase (the signals are then correlated), power-complementary otherwise.
   Pass: on a 110 Hz sawtooth and her takes, no sidebands at ±75 Hz above −40 dB re the fundamental; Doubler_2 octave
   bands within ±1 dB, the 125 Hz octave within ±1.5 dB (today +2…+6).
3. **Fix the documented behaviours**: LFO phase starts at the centre rising (Gemini: initialise lfo = 0.25) [S1];
   keep the measured 2/rate period unless §8 item 5 contradicts it; cap rate at 200 (manual) in the translator
   (the RealWorld value range is the arbiter); add Mod Reset (index 19) as a trigger.
4. **Range 20 Hz**: set the window from the measured inherent latency (manual 24 ms → W ≈ 48 ms) once §8 item 2 is
   measured; also the aligned direct delay for 20 Hz (likely 24 ms, not 6.67).
5. **Shelf "cut" mode**: below −24 dB make the shelf's floor drop to a cut (first-order high-/low-pass with the same
   corner) per the manual; measure the transition (§8 item 7). Her Doubler_0 stores −28.7 dB but with EQ off, so it
   is inaudible today; it matters for other users' projects.
6. **Doubler4 voices 3–4**: extend Gemini to 4 voices (append parameters so old projects load unchanged), translate
   blocks 46 and 56 and the voice 3/4 sources once probed (§8 item 6). Her states are Doubler2-sized; this is for
   completeness.
7. **Mono / Mono-to-Stereo components**: in a mono→stereo instance both voices read the same mono source; map
   `Source` accordingly when the saved component is mono (class ID tells the component).

## 8. Open questions to measure (official trial, black-box only)

1. **Octave splice strategy**: 110 Hz sawtooth and a sung vowel through voice 1 octave on, detune 0, depth 0, Range
   80 Hz; spectrum (sidebands at fixed Hz offsets = fixed window; none = pitch-synchronous) and spectral envelope
   (formants moved down = delay-line/SOLA; preserved = PSOLA-like). Decides §7 step 2's design.
2. **Range 20 Hz**: impulse with Align on/off, voice delay 0 and 20 ms; read direct delay and voice arrival.
   Decides the 20 Hz window and aligned direct delay (manual: 24 ms).
3. **Aligned offset**: voice delay 0, 1, 5, 10, 50 ms with Align on and off, impulses; arrival vs knob. Decides
   whether the measured +10.3 ms (relative +3.6 ms) is constant and whether the manual's 8 ms minimum (unaligned)
   clamps.
4. **Sample rate**: repeat item 3 at 44.1 and 96 kHz. Decides whether 6.67 ms / 10.3 ms are times or sample counts.
5. **LFO**: detune 0, depth +50 c, rate 1.0 and 10, a 1 kHz tone; instantaneous frequency vs time. Decides period
   (1/rate vs 2/rate), start phase/direction, and waveform; also with depth −50 c and after Mod Reset.
6. **Doubler4 layout**: probe a Doubler4 state the same way as round 119 (one value edited, parameters read back) to
   label RealWorld 20–25 and 46–65 (voices 3/4, sources 3/4).
7. **Shelf cut mode**: high shelf at −20, −24, −25, −30 dB, f0 3 kHz; magnitude response. Decides the cut-filter
   order/corner. Also the low shelf (muted in round 119).
8. **Feedback loop**: detune +20 c, feedback 50 %, delay 100 ms, impulse/tone burst: does each repeat get detuned
   further (loop after the shifter) and do the shelves act inside the loop? Also the feedback high-pass order
   (sweep with HP at 500 Hz).
9. **Interpolation / HF loss**: white noise through a voice with detune +3 c; voice spectrum vs input above 10 kHz.
   Decides the interpolator (linear vs Hermite vs band-limited).
10. **FL slot mix law**: confirm whether FL's mix knob is a linear dry/wet crossfade (null test with a gain plugin),
    since her sound depends on 0.69·dry + 0.31·(direct + voices).

Remember the trial's demo mute (~1 s of exact zeros every ~20 s): mask it as round 119 did.

## 9. Sources

1. Waves, "Waves Doubler — Software Audio Processor — User's Guide" — https://assets.wavescdn.com/pdf/plugins/doubler.pdf — accessed 2026-10-21 — [DOC]
2. Thomann, Waves Doubler product listing (4 voices, delay up to 100 ms with feedback, ±100 c detune, octave-down, shelves) — https://www.thomann.co.uk/waves_doubler.htm — accessed 2026-10-21 — [DOC, retailer]
3. Woodbrass, Waves Doubler description — https://woodbrass.com/products/waves-doubler-399723 — accessed 2026-10-21 — [DOC, retailer]
4. J. Dattorro, "Effect Design, Part 2: Delay-Line Modulation and Chorus", JAES 45(10), 1997 — https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf — accessed 2026-10-21 (scanned PDF; specific numbers not extracted) — [PAPER]
5. J. O. Smith III, "Chorus Effect", Physical Audio Signal Processing — https://ccrma.stanford.edu/~jos/pasp/Chorus_Effect.html — accessed 2026-10-21 — [PAPER]
6. J. O. Smith III, "Time-Varying Delay-Line Reads" / Doppler simulation (PASP; REALSIMPLE delay-variation notes) — https://ccrma.stanford.edu/~jos/pasp/Time_Varying_Delay_Line_Reads.html , https://ccrma.stanford.edu/realsimple/DelayVar/Variable_Delay_Lines.html — accessed 2026-10-21 — [PAPER]
7. S. Roucos, A. Wilgus, "High quality time-scale modification for speech", Proc. ICASSP 1985, pp. 493–496 (SOLA; via secondary descriptions, e.g. Dorran/Lawlor) — https://mural.maynoothuniversity.ie/9768/1/BL-High-Quality-1999.pdf — accessed 2026-10-21 — [PAPER]
8. K. Lent, "An efficient method for pitch shifting digitally sampled sounds", Computer Music Journal 13(4):65–71, 1989 (via citing patents, e.g. US 5,567,901) — https://patents.justia.com/patent/5567901 — accessed 2026-10-21 — [PAPER]
9. Wikipedia, "Automatic double tracking" — https://en.wikipedia.org/wiki/Automatic_double_tracking — accessed 2026-10-21 — [DOC, secondary]
10. Wikipedia, "Ken Townsend" — https://en.wikipedia.org/wiki/Ken_Townsend — accessed 2026-10-21 — [DOC, secondary]
11. Valhalla DSP, "Early pitch shifting: The Eventide H910 Harmonizer" (via search summary; page returned 403) — https://valhalladsp.com/?p=184 — accessed 2026-10-21 — [DOC, secondary]
12. Wikipedia, "Eventide, Inc" (H910 1975, H949 1977) — https://en.wikipedia.org/wiki/Eventide,_Inc — accessed 2026-10-21 — [DOC, secondary]
13. E. Moulines, F. Charpentier, "Pitch-synchronous waveform processing techniques for text-to-speech synthesis using diphones", Speech Communication 9(5–6):453–467, 1990 — https://courses.grainger.illinois.edu/ece420/5_PSOLA.pdf — accessed 2026-10-21 — [PAPER]
14. Audiofanzine user review of Waves Doubler (2010; "Doubler 2 and Doubler 4") — https://en.audiofanzine.com/plugin-chorus/waves/Doubler/user_reviews/r.5754.html — accessed 2026-10-21 — [DOC, third-party]
15. LYNX REA round 119 §4 (Doubler measured, Gemini, her states) and §5 — [../analise/REA_rodada119.md](../analise/REA_rodada119.md) — [MEAS]
16. LYNX source: `src/core/VoiceTools.cpp` (Gemini), `src/core/PluginSubstitutes.cpp` (`[waves-doubler]`, `translateWavesDoubler`) — [MEAS-backed code]
17. Her corpus and Doubler probes: `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`, `tmp/w119/wv/Doubler_read.tsv`, `tmp/w119/wv/Doubler_0_probe.txt`, `tmp/w119/py/db1.py`, `db2.py` — [MEAS]
