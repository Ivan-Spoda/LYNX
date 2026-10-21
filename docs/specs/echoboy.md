# Soundtoys EchoBoy — spec dossier (LYNX substitute: Fruity Delay 3)

Round 125 (spec125). Short clean-room dossier from the vendor's user guide (the original Pro Tools-era guide; the
v5 guide was not retrievable), the vendor's store page, LYNX's source and her project data. No Soundtoys binary was
opened, decompiled or instrumented; the encoded preset payload was not decoded.
Tags: [DOC] vendor document · [PAPER] academic · [MEAS] measurement · [DATA] her projects / LYNX files ·
[model] our inference. All web sources accessed 2026-10-21.

**Summary.** EchoBoy is a character delay: four echo modes (Single, Dual, Ping-Pong, Rhythm with up to 16 taps),
Echo Time in ms or note values (with dotted/triplet, groove, feel), Feedback, Mix, Saturation and HighCut whose
effect **depends on the Echo Style** — a menu of ~30 voicings modelled on tape, BBD and digital units (Master/Studio
Tape = Ampex ATR-102 at 30/15 ips, EchoPlex EP-3, Roland RE-201 Space Echo, EHX Memory Man, Boss DM-2, TelRay oil
can, CE-1 chorus …) [S1 pp.22–27][DOC], [S2][DOC]. A Style Editor exposes the guts: 3-band EQ with per-repeat
"Decay", saturation type, diffusion (loop/post), wobble (wow) with shapes and L/R sync [S1 pp.48–54][DOC]. Her
corpus: 1 slot in the 412-project inventory, 235 slots / 22 states in the full 907-file copy [DATA]. The state is
encoded; LYNX plays Fruity Delay 3 at its defaults. **Recommendation:** route EchoBoy to **Echoform** (LYNX's
character delay with tape/BBD/digital modes, wow/flutter, diffusion, drive), not Fruity Delay 3.

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Soundtoys EchoBoy (her states: `WIDGET = EchoBoy; VERSION = 4; SAVED_BY_VERSION = 5.0.1`) | [DATA] |
| Format in her projects | VST2, ID `StEB` | [S4][DATA] |
| Origin | SoundToys (formerly Wave Mechanics; the team behind Eventide H3000 / DSP4000 algorithms) — first released as a Pro Tools TDM/RTAS/AudioSuite plug-in | [S1 pp.8–10][DOC] |
| Styles today | "30 built-in styles modeled on vintage gear … EchoPlex, Space Echo, Memory Man, DM-2, and the TelRay oil can delay"; "Studio Tape … inspired by our Ampex ATR-102" | [S2][DOC] |

**Her usage [DATA].** Inventory: 1 slot / 1 project / 1 state. Full copy (`tmp/gf/all`): 235 slots, 22 states;
the most used in `twin swag` (51 + 41 slots) and `II` (29).

## 2. Signal flow

```
in ─► Input level (−24…+24, "analog" input stage, echo path only) ─► [Ping-Pong / Rhythm: L+R summed to mono]
   ─► delay line(s) (Single: per-channel; Dual: two times; Ping-Pong: L feeds R; Rhythm: 1 line, ≤16 taps)
   ─► style chain in the loop: EQ low/mid/high (gain + per-repeat Decay), saturation (type + amount), diffusion
      (loop or post), wobble (pitch modulation) ─► Feedback (+ Dual: Feedback Mix cross-feed, FB balance)
   ─► Output level (echo only) ─► Width / L-R Offset / Balance ─► Mix with the dry signal ─► out
```
Input/Output levels act on the echo only; the dry signal is untouched [S1 p.26][DOC]. Mix 12 o'clock = 50/50
[S1 p.26][DOC].

## 3. Controls (front panel; Pro Tools-era guide)

| Control | Range | Default | Behaviour | Source |
|---|---|---|---|---|
| Echo Style | ~30 styles (v1 list: Master Tape, Studio Tape, EchoPlex, Space Echo, Tube Tape, Cheap Tape, Memory Man, DM-2, TelRay, Binsonette, Telephone, AM/FM Radio, Shortwave, Transmitter, Digital/Analog Delay, Digital/Analog/CE-1 Chorus, Vibrato, Saturated, Fat, Distressed, Limited, Distorted, Queeked, Ambient, Diffused, Splattered, Verbed) | Studio Tape in the screenshots | sets the whole voicing | [S1 pp.22–23][DOC] |
| Echo Mode | Single, Dual, Ping-Pong, Rhythm | n/d | separate algorithms | [S1 p.24][DOC] |
| Echo Time | ms ("Time") or note ("Note / Dot / Trip"); Rhythm adds "Patt" | n/d | ms changes pitch like an analog delay; note changes crossfade | [S1 pp.27–28][DOC] |
| Tempo | 30…300 BPM, tap or MIDI/host sync | 120 | only for note values | [S1 pp.29–30][DOC] |
| Mix | Dry … Wet | n/d | 12 o'clock = 50/50 | [S1 p.26][DOC] |
| Feedback | Min … Max | n/d | can run away near max | [S1 pp.26–27][DOC] |
| Saturation | Less … More | 12 o'clock | style-dependent (Studio Tape: odd harmonics + HF compression, "automatic de-essing" of the echo) | [S1 p.27, p.51][DOC] |
| HighCut | Min … Max | n/d | style-dependent darkening | [S1 p.27][DOC] |
| Prime Numbers | on/off | n/d | detunes repeat times to avoid resonance | [S1 p.29][DOC] |
| Groove / Feel | shuffle…swing / rushin…draggin | centre | swing of alternate repeats; global pre-delay ± | [S1 pp.30–31][DOC] |
| Input / Output | −24 … +24 dB | n/d | echo path only | [S1 pp.25–26][DOC] |
| Tweak (Single) | Accent (bipolar), Width (min…max, past 3 o'clock = out-of-phase), L/R Offset 0…25 ms (default 8 ms) | | | [S1 pp.32–34][DOC] |
| Tweak (Dual) | Accent 1/2, Width, FB Mix (min…cross), Balance, FB Balance, L/R Offset | | | [S1 pp.35–36][DOC] |
| Style Edit | EQ Low/Mid/High Freq, Gain ±18 dB (knob labels −40…+18), Decay, Mid Res; Saturation type (Clean, Tape, Warm, Pump, Dirt, Hard/Soft/Warm/Bright Limit); Diffusion, Size, Loop/Post; Wobble, Rate, Shape (Triangle, Sine, Square, Random Walk, Random S/H), Sync | | | [S1 pp.48–54][DOC] |

State mapping: none (encoded preset) [DATA].

## 4. Known behaviour worth matching

- Ping-Pong: first repeat on L after Ping time, then R after Ping + Pong, alternating; input always mono [S1 p.37][DOC].
- Rhythm mode: up to 16 taps on a 1–8-beat pattern, grid 1/2 … 1/64; Shape (Decay, Reverse, Swell, Fade, Nonlin);
  Feedback repeats the whole pattern [S1 pp.38–46][DOC].
- Per-repeat tone change ("Decay" of each EQ band) = an EQ inside the feedback loop; "Gain" = EQ on the first
  repeat [S1 pp.49–50][DOC].
- Not documented: maximum delay time per mode (depends on format), latency, the exact curves of each style [model].

## 5. Published science (brief)

Tape echo = delay line + nonlinear record/playback (hysteresis, HF self-erasure, head bump) + wow/flutter
(slow random + periodic modulation of read position, fractional-delay interpolation) + loop filtering; BBD echo =
anti-alias/reconstruction low-pass around a clocked sample-and-hold line with compander noise [S3][PAPER]. For
modelling choices and equations see LYNX's existing delay work ([round 101](../analise/REA_rodada101.md) for
ValhallaDelay → Echoform, [round 119 §2](../analise/REA_rodada119.md) for H-Delay).

## 6. What LYNX does today

`[soundtoys-echoboy]` → `Fruity Delay 3`, `vst2 = StEB`, names EchoBoy / EchoBoyJr; **no preset, no translation**
(Fruity Delay 3 defaults) [DATA, `src/core/PluginSubstitutes.cpp`]. No measurement exists (no Soundtoys trial,
[round 114](../analise/REA_rodada114.md)). Note that FL's own slot mix still applies on top.

## 7. Implementation guidance for LYNX

Priority: **low-medium** (beat projects, not her vocal chain; one state per project).
1. Switch the substitute to **Echoform** (tape/BBD/digital characters, wow/flutter, diffusion, drive, filters, sync,
   ping-pong) with a conservative default: Studio-Tape-like (tape mode, low age, 1/4 note at host tempo, feedback
   ≈ 25 %, mix ≈ 25–30 %) — or keep it silent-ish (mix low) since a wrong delay time is worse than none.
2. Only if states become readable (§8): translate Mode → Echoform pairs/ping-pong, Time/Note/Dot/Trip → sync + note,
   Feedback, Mix, HighCut, Saturation → drive, Style family → Echoform character (tape: Master/Studio/Tube/Cheap
   Tape, EchoPlex, Space Echo, Binsonette; BBD: Memory Man, DM-2, Analog Delay/Chorus; digital: Digital Delay/Chorus;
   lo-fi: Telephone/AM/FM/Shortwave/Transmitter).
3. Pass criteria: echo times within 1 ms, repeat-level decay within 1 dB over 5 repeats, octave-band spectrum of the
   echo tail within 1 dB for the dominant style.

## 8. Open questions to measure (official trial, black-box only)

1. Is the saved blob a reversible encoding of plain values (single-control changes → blob diff)? If not, render-fit.
2. Defaults of a fresh v5 instance (style, mode, time, feedback, mix).
3. Per dominant style: impulse response of the first 5 repeats (time, level, spectrum), THD of the echo at −18/−6 dBFS
   input, wow depth/rate.
4. Latency and maximum delay time per mode.

## 9. Sources

1. SoundToys, *EchoBoy — The Ultimate Echo Machine*, user guide (Pro Tools TDM/RTAS era) — https://barryrudolph.com/recall/manuals/echoboy.pdf — accessed 2026-10-21 — [DOC]
2. Soundtoys store page, EchoBoy ("30 built-in styles", "Studio Tape … inspired by our Ampex ATR-102", Rhythm mode up to 16 repeats) — https://store.soundtoys.com/product/echoboy — accessed 2026-10-21 (search-result text; page fetch refused) — [DOC]
3. V. Välimäki, S. Bilbao, J. O. Smith, J. S. Abel, J. Pakarinen, D. Berners, "Virtual Analog Effects", in U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley, 2011, ch. 12 (tape and BBD delay modelling) — https://doi.org/10.1002/9781119991298.ch12 (DOI not re-verified) — accessed 2026-10-21 — [PAPER]
4. LYNX round 98 (Soundtoys VST2 IDs, encoded presets), round 114 (no Soundtoys trial) — [../analise/REA_rodada98.md](../analise/REA_rodada98.md), [../analise/REA_rodada114.md](../analise/REA_rodada114.md) — [DATA]
5. LYNX source: `src/core/PluginSubstitutes.cpp` (`[soundtoys-echoboy]`), `src/core/EchoSuite.cpp` (Echoform) — [DATA]
