# Waves H-Delay Hybrid Delay — spec dossier (LYNX substitute: Echoform)

Round 125 (spec125). Clean-room dossier: built only from Waves' public product pages, secondary summaries of the
public H-Delay user guide, Waves forum posts, academic papers on BBD / tape delay modelling, and LYNX's own
black-box measurements of the official trial ([REA round 119 §2](../analise/REA_rodada119.md); this round re-reads
the round-119 recordings, `C:\Users\kauan\lynx-test\w119\hd`, without running the plugin again). No Waves binary
was opened, decompiled or instrumented; licensing was not touched. Tags: [DOC] vendor document · [PAPER] academic ·
[PATENT] · [SCHEMATIC] · [MEAS] measurement · [model] our inference (with what it rests on). All web sources
accessed 2026-10-21.

**One-paragraph summary.** H-Delay is Waves' "hybrid" (analog character + digital control) delay of the H-series,
advertised for "old school PCM42-style effects", slapback, ping-pong and tempo-synced modulation [S1][S2][DOC]:
1–3500 ms or note values, feedback 0–200 %, ping-pong, phase L/R, HP/LP filters, triangle-LFO modulation, a LoFi
switch and an "Analog" selector (Off, 1–4; default 2) [S3][DOC-secondary]. Her corpus: 712 slots / 399 projects /
25 distinct states, almost always **host-synced 1/2 or 1/4 note, ping-pong, feedback 60, mix 100, Analog 2, no
filters, no modulation** ([round 119 §2](../analise/REA_rodada119.md)). Measured: linear loop laws (ping-pong pair
gain 0.93·(fb/100)², stereo 0.98·fb/100), flat echoes (±0.5 dB, 30 Hz–20 kHz), equal-power mix [MEAS]. The
re-read of the round-119 recordings adds three facts that matter for "identical": (1) **a level-dependent gain
acts on every pass through the loop, with Analog Off as well as Mod2** (−3.2 / −4.0 dB on a −6 dBFS impulse vs a
−26 dBFS one; the second pass of a loud echo loses another ~3 dB) — so it is a loop limiter / compander, not part
of the "analog" colour; (2) Mod2 adds a ±0.5 dB low-up/high-down tilt, broadband hiss at ≈ −85 dBFS RMS and
level-independent 2nd-harmonic distortion (≈ −60 dB), while Analog Off is bit-exact flat and noiseless but still
makes 3rd harmonics that rise with level; (3) **LoFi is a steep low-pass inside the loop** (≈ −5 dB at 6 kHz,
−22 dB at 8 kHz, −57 dB at 10 kHz per pass) [MEAS]. Echoform today has no loop compression stage, so its right
(second-pass) echoes are 3–4 dB too loud on her loud takes ([round 119 §2](../analise/REA_rodada119.md)).

---

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Waves Audio — H-Delay Hybrid Delay ("the complete toolbox for classic delay effects") | [S1][DOC] |
| Series concept | H-Series: "mixing tools that model the character of analog gear and enhance it with digital possibilities"; "analog behavior meets digital precision" | [S2][DOC] |
| Named lineage | "old school PCM42-style effects like filtering, flanging and phasing" — Lexicon PCM42 (1982), a digital delay whose time modulation works by varying the sampling rate, up to 4.8 s with the memory option | [S1][S2][DOC]; [S11][S12][DOC] |
| Guide | H-Delay user guide (18 pp., 2009); not on waves.com at access time; reseller-hosted copies exist; facts below come from a secondary summary that cites it | [S3][DOC-secondary] |
| Components | Mono, Stereo, Mono-to-Stereo (M>S) | [S3] |
| Version used here | WaveShell1-VST3 17.1 (official trial); her projects reference shells 16.0 / 15.5 | [round 119 §1, §5](../analise/REA_rodada119.md) [MEAS] |
| Latency | not published; not recorded in round 119 | — |

**Her usage** [MEAS]: 712 slots in 399 projects, **25 distinct states**
(`~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt`). Round 119 (a different de-duplication over her project
files) reports 70 distinct state blobs with: host sync in 61 (1/2 ×37, 1/4 ×22), own BPM in 9, ping-pong in 68,
feedback 60 in 38, mix 100 in 60, and **Analog = Mod2, LoFi off, filters off, modulation off in all**; FL's slot
mix is 0–7 % in her projects ([round 119 §2](../analise/REA_rodada119.md)). So the target is essentially ONE
voicing: ping-pong, fb 60, wet only, Analog 2, at 1/2 or 1/4 note — blended at a few % by FL.

## 2. Signal flow

Measured structure (ping-pong, her state) [MEAS] plus our reading [model]:

```
 L ─┐                                   ┌──────────── loop (per left→right pair: 0.93·(fb/100)²) ───────────┐
    ├─ (L+R) mono sum ─► [N: level-dependent gain, every pass] ─► delay T ─► LEFT echo (t = T) ──► [N] ─► delay T ─► RIGHT echo (2T, ≈ −0.3 dB small-signal)
 R ─┘                         ▲                                                                              │
                              └──────────────── feedback ·fb ◄───────────────────────────────────────────────┘
                     [LoFi LP — inside the loop, applied each pass]          [HP / LP filters on the delayed path, S3]
 echoes ─► [Analog Mod2 colour: +0.5 dB < 250 Hz, −0.5 dB > 8 kHz, once] ─► + hiss (≈ −85 dBFS) ─► Output dB ─► equal-power Mix ─► out
 dry ───────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- Ping-pong: the mono sum feeds the LEFT echo at T; the right repeats it at 2T at nearly the same level
  (−0.3 dB) [MEAS]; start side selectable (RealWorld 45) [MEAS]; the guide says ping-pong "requires Feedback above 1"
  and that reversing phase on one side can cancel mono sources [S3].
- Stereo mode: each side on its own loop, 0.98·fb/100 per repeat [MEAS].
- Nonlinearity N (§4.3): in the loop (each pass), present with Analog Off [MEAS, this round].
- Analog colour / noise: outside the loop for the tilt (the right echo shows the same −0.5 dB tilt, not double)
  [MEAS]; whether the hiss recirculates is unknown (§8). A review and the vendor page say the Analog modes can also
  act on the dry, undelayed signal [S4 search summary][DOC] — irrelevant at her mix 100, relevant for mix < 100.

## 3. Controls

Ranges / defaults from the guide summary [S3] unless marked; state indices = RealWorld list read back from the
official trial ([round 119 §2](../analise/REA_rodada119.md)) [MEAS].

| Control | Range | Unit | Default | Law / taper | Audible effect | State mapping (RealWorld) |
|---|---|---|---|---|---|---|
| Sync | Host / BPM / MS | — | Host | — | tempo source | 1 (0 BPM, 1 host, 2 ms) |
| BPM | own tempo | BPM | — | — | used when Sync = BPM | 2 |
| Delay (time) | 1–3500 ms or note value | ms / note | 1/8 dotted (Host) | MS↔tempo switch shows nearest value [S3] | echo time; changing it while signal is in the line gives tape-style pitch shift [S3] | 3 = ms (also the computed time in BPM mode); 4 = note: 3 per length (triplet, straight, dotted) from 1/64; 18 = 1 bar, 19 = 2 bars |
| Dual (newer versions) | L / R independent times | — | — | feedback / mod / filters shared | — | not located |
| Feedback | 0–200 | % | 60 | measured: stereo loop 0.98·fb/100 per repeat; ping-pong 0.93·(fb/100)² per L→R pair [MEAS] | repeats; > 100 % builds up [S3] (limited by N, §4.3) | 7 |
| Ping-Pong | on / off (+ start side) | — | off | — | alternating L/R | 10 (1 ping-pong, 0 stereo); 45 start side |
| Phase L / R | normal / inverted | — | off | polarity [S3] | width / cancellation | 8 / 9 |
| Mix (Dry/Wet) | 0–100 | % | 100 | **equal-power** (25 %: dry −0.7 dB, wet −8.3 dB) [MEAS] | balance | 6 |
| Output | −18 … +18 | dB | 0 | dB on the output [S3] | level | 14 |
| HiPass | Off, 20 Hz–20 kHz | Hz | Off | slope unpublished [S3] | thins echoes (delayed path only) | 17 (19 = off) |
| LoPass | 20 Hz–20 kHz, Off | Hz | Off | slope unpublished [S3] | darkens echoes | 15 (20100 = off) |
| Filter link | on / off | — | — | moves HP and LP together [S3] | — | not located |
| Mod depth | 0–100 | % | 0 | triangle LFO on delay time [S3] | chorus / flange / vibrato | 19 |
| Mod rate | 0.1 … (summary says 6000; unverified) | Hz (also BPM / note per concept section) | 0.1 | — | — | 20 |
| LoFi | on / off | — | off | "reduced frequency range of early digital delays" [S3]; measured steep LP ~6–8 kHz in the loop (§4.4) [MEAS] | dark, early-digital echoes | 21 |
| Analog | Off, 1, 2, 3, 4 | — | 2 | 1–2 "early analogue delay", 3–4 "tape" (forum user, not staff) [S6]; staff: Off "will eliminate the white noise" [S6][DOC] | colour, noise, harmonics | 23 (0 off, 2 = Mod2) |
| Tap | tap pad | — | — | last two taps; snaps to note (Host), sets BPM (BPM), sets ms (MS) [S3] | — | — |
| MIDI Learn | all controls but meter | — | — | — | — | — |

## 4. Known curves & behaviour

All [MEAS] items: official trial in tools/fxmeasure (demo mode; exact-zero mute windows masked), 48 kHz, her
state `H-Delay_0` (ping-pong, 1/2 note at 120 BPM = 1000 ms, fb 60, mix 100, Mod2) and variants
([round 119 §2](../analise/REA_rodada119.md); recordings `w119/hd/H-Delay_0_<variant>_*.wav`). Variant tags
re-read this round: `her`, `an0` (identified as Analog Off: bit-exact flat echo, digital-zero noise floor),
`stereo`, `lofi`, `mix50`, `fb90`, `fb100`. The exact setting behind some other tags (`a0`, `f*`) was not logged
and they are not used.

### 4.1 Loop gain and timing (linear part)
- Ping-pong pair gain = 0.93·(fb/100)²: fb 30 / 60 / 90 → −20.9 / −9.5 / −2.7 dB [MEAS].
- Stereo: 0.98·fb/100 per repeat (fb 60: −4.6 dB; re-read: −4.5…−4.7 dB per repeat at −26 dBFS) [MEAS].
- First echo over the mono sum: +1.1 dB (analog modes), +2.5 dB (Off); stereo mode: −1.9 / −0.4 dB over its own
  side (`translateWavesHDelay` comment, round 119) [MEAS].
- Echoes flat ±0.5 dB 30 Hz–20 kHz [MEAS] — i.e. **no BBD-style band limiting** in Mod2 (contrast §5.1).
- Mix: equal power [MEAS] (mix50: echo −3.0 dB vs mix 100, re-read).

### 4.2 Spectral colour per mode (first echo, 0.5-amplitude impulse into both inputs, re 1 kHz) [MEAS, this round]

```
              50 Hz  100   250   2k    4k    6k    8k    10k   12k   16k   20k
  Mod2 L (T)  +0.6  +0.3  +0.5  -0.3  -0.4  -0.4  -0.5  -0.5  -0.5  -0.5  -0.5
  Mod2 R (2T) +0.7  +0.3  +0.5  -0.3  -0.4  -0.4  -0.4  -0.4  -0.4  -0.4  -0.4
  Off   L / R  0.0   0.0   0.0   0.0   0.0   0.0   0.0   0.0   0.0   0.0   0.0
  LoFi  L (T) +0.5  +0.5  +0.4  -0.3  -0.6  -5.0 -21.7 -56.6 -61.5 -66.7 -71.1
  LoFi  R (2T)+0.5  +0.4  +0.4  -0.3  -0.6  -8.7 -38.5 -78.8 -80.9 -83.3 -84.4
```
- Mod2's tilt is applied once (R ≈ L) → outside the loop [MEAS/model].
- LoFi's low-pass compounds on the second pass → **inside the loop**. Up to 8 kHz it is close to an 8th-order
  Butterworth at ≈ 5.8 kHz (−22.4 dB at 8 kHz predicted vs −21.7 measured); above 8 kHz it is steeper (elliptic /
  brick-wall-like) [model]. A forum user describes LoFi as a low-pass "starting around 5 kHz and fully cut by
  8.5 kHz" [S5][MEAS-third-party].

### 4.3 Level dependence (the loop nonlinearity N) [MEAS, this round]

Impulses (peak re input peak; L echoes at T, 3T, 5T…; R at 2T, 4T…):

```
                         L1 (T)   R1 (2T)   L2 (3T)   L3 (5T)   L4 (7T)   L5 (9T)
  her, −26 dBFS impulse  +1.0     +0.5      −8.6      −17.7     −26.7     −34.9     (pair: −9.6 −9.1 −9.0 −8.2)
  her,  −6 dBFS impulse  −2.2     −5.1      −14.8     −24.7     −33.9     −42.8     (pair: −12.6 −9.9 −9.2 −8.9)
  Off, −26 dBFS          +2.4     +1.9      −7.2      −16.3     −25.3     −34.2
  Off,  −6 dBFS          −1.6     −4.6      −14.4     −24.3     −33.5     −42.5
  stereo, −26 / −6 dBFS  −1.9 / −4.2 first echo; then −4.7 / −5.9, −4.5 / −5.2 … per repeat (→ −4.6 linear)
  fb 100, −26 / −6 dBFS  pair decay −1.0, −0.8, −0.7, −0.8 / −5.0, −3.2, −2.3, −1.8 (→ −0.6 linear)
```

1 kHz bursts (0.25 s; strongest 0.2 s window of the first echo; gain re input sine RMS) and harmonics re H1:

```
  input dBFS    −40    −34    −28    −16    −10     −4
  Mod2 gain     +1.8   +3.0   +3.0   +1.6   +0.1   −2.3 dB      (−22 dBFS row hit a demo mute; dropped)
  Mod2 H2/H3    −65/−66 −64/−59 −58/−54 −46/−43 −41/−39 −37/−35 dB
  Off  gain     +2.9   +4.1   +4.0   +2.3   +0.6   −2.0 dB
  Off  H2/H3    —/−76  —/−68  —/−61  —/−50  —/−44  —/−39 dB     (H2 < −140 dB: perfectly odd-symmetric)
```

Readings:
- The compression is **the same with Analog Off and Mod2** (≈ 5–6 dB from −34 to −4 dBFS; 3–4 dB on a −6 dBFS
  impulse) → it is part of the delay engine, not of the Analog colour. Round 119 attributed it to Mod2; the Off
  data correct that [MEAS].
- It acts on **single-sample impulses** and **on each pass** (loud first echoes lose 3 dB more on the next pass;
  decays converge to the linear law as the level falls) → instantaneous or very fast (peak) gain, in the loop
  [MEAS/model]. A slow RMS compressor would not react to a lone impulse [model].
- The gain keeps **rising as the level falls** down to ≈ −30 dBFS (pair loss 9.6 → 8.2 dB as the echo decays from
  −26 to −35 dBFS), and the −40 dBFS burst reads ~1 dB lower than −34 in both modes — the low end is not settled
  (§8) [MEAS].
- Off: pure odd distortion whose H3/H1 grows ≈ 1 dB per dB (not the 2 dB/dB of a static cubic) — consistent with
  a smooth limiter / soft-knee gain function, not a polynomial clipper [model].
- Mod2 adds **even** harmonics at ≈ −65…−58 dB that barely change between −40 and −22 dBFS — "level-independent"
  distortion like the BBD nonlinearity of Raffel & Smith (§5.1) [MEAS/model].
- Feedback ≥ 100 % stays bounded because of N [S3][MEAS: fb 100 decays at −0.6 dB per pair at low level].

### 4.4 Noise and hum [MEAS]
- Mod2 noise floor (no input, first 0.5 s): ≈ −85 dBFS RMS each side; spectrum rising with frequency (octave
  shares re total: 63 Hz −19, 250 Hz −17, 1 kHz −11, 4 kHz −9, 16 kHz −5 dB); no distinct 50/60 Hz line in this
  short window [this round]. Stereo mode ≈ −84 dBFS. LoFi: ≈ −88 dBFS, band-limited below 8 kHz (the noise passes
  the LoFi LP → noise is injected before / inside the LP). Off: digital zero.
- Round 119 reported "hum / hiss (~−85 dBFS)" and, on her takes, "the reference's 60 Hz hum harmonics make 125 Hz
  +2…+15 dB" over LYNX ([round 119 §2](../analise/REA_rodada119.md)) — the hum may be signal-dependent (e.g. only
  with input) or need a longer window to resolve; open (§8).
- Users: "the ANALOG knob @ 2 is creating a huge amount of hiss; @ 1, 3 and 4 … no hiss" (Gearspace, title/snippet)
  [S7][MEAS-third-party]; Waves staff: Analog Off eliminates the white noise [S6][DOC].

### 4.5 Modulation, time changes
- LFO: triangle on delay time; depth 0–100, rate from 0.1 Hz [S3]. Unused by her (depth 0 in all states) [MEAS].
- Time changes are tape-like (pitch glides) [S3]; unmeasured.

## 5. Published science

### 5.1 Bucket-brigade delays (the "early analogue delay" lineage of Analog 1–2 per [S6])
Raffel & Smith, DAFx-10 [S8][PAPER]:
- Delay `τ = N / (2 f_cp)` (N stages, clock f_cp); e.g. 4096 stages at 300 ms → f_cp = 6826 Hz → input band-limit
  3413 Hz. Anti-aliasing 3rd-order + reconstruction 3rd + 2nd-order Sallen-Key filters (R = 10 kΩ typical), cutoff
  1/3–1/2 of f_cp, as low as 1.5 kHz in some echo units → modelled as one ~8th-order IIR.
- Companding with 570/571-series chips: ratio 2; expander `f(x) = avg(|x|)·x`, feedback compressor
  `f(x) = x / avg(|f(x)|)`; averager = full-wave rectifier + one-pole `y[n] = x[n]·T/(RC+T) + y[n−1]·RC/(RC+T)`,
  τ = 10 kΩ·C_rect with C_rect 0.22–1 µF → **2.2–10 ms** [S8][S9 DOC datasheet].
- BBD SNR ≈ 60 dB near full level; insertion gain 0…2 dB, falling to −4…−6 dB at Nyquist.
- Nonlinearity "about 1 % THD per 1024 stages", `THD = 1.01^(N/1024) − 1`, not level dependent; harmonics fall
  off with order (4th+ negligible); fitted by `f(x) = x − a x² − b x³ + a` (|x| < 1; constant clip outside), with
  a = 1/8, b = 1/18 — **the a·x² term gives exactly the level-insensitive 2nd harmonic Mod2 shows** [model].
Holters & Parker, DAFx-18 [S10][PAPER]: BBD as a fixed-length, variable-rate delay line; input/output filters in
partial fractions `H(s) = Σ r_m/(s − p_m)` discretised by a modified impulse-invariant transform evaluated at the
BBD clock instants, output as a zero-order hold (sinc(f/f_BBD) roll-off); avoids extra interpolation. Juno-60
chorus filters: 5th-order low-passes, poles e.g. −46580, −55482 ± 25082i, −26292 ± 59437i (input). An open
JUCE implementation exists in chowdsp_utils (check its licence before reuse; LYNX is AGPL) [S10][S13].
Relevance: H-Delay's Mod2 echoes are flat to 20 kHz (§4.1), so its "analog" mode is **not** a band-limited BBD
model; the BBD papers matter for the compander / nonlinearity / noise, not for the filters [MEAS/model].

### 5.2 Tape delay (Analog 3–4 lineage per [S6])
- Echoplex model: circular buffer with read/write pointers for the tape transport, fluctuating delay, anti-alias
  filter whose cutoff follows the head motion (Arnardottir, Abel & Smith, AES 125th conv., 2008) [S14][PAPER].
- Tape-like delay-time modulation (pitch glides on time change) at constant cost (Zavalishin & Parker, DAFx-18)
  [S15][PAPER] — the behaviour the guide describes for time changes [S3].

### 5.3 Early digital delays (PCM42 lineage)
- Lexicon PCM42 (1982): real-time delay / looper locked to tempo, up to 4.8 s with memory option, delay
  modulation by varying the sampling rate [S11][S12][DOC]. LoFi's "reduced frequency range of early digital
  delays" [S3] matches a steep anti-alias low-pass of a low-rate converter (our §4.2 LoFi curve) [model].

### 5.4 Loop limiting / compression
- Feedback paths with a compressor inside or outside the compander vary between units [S8]; a limiter inside the
  loop is what keeps feedback > 100 % bounded [model]. Gain-computer equations (threshold, ratio, knee) and
  detector smoothing: Giannoulis, Massberg & Reiss, JAES 2012 [S16][PAPER].

## 6. What LYNX does today

- Substitute: **Echoform** (`src/core/EchoSuite.cpp`, params `src/core/EchoSuite.h`): two-time delay with styles
  (single, ratio, quad, dual, ping-pong), 12 modes (tape, lofi, hifi, bucket, analog, digital, clean, …), age,
  drive, diffusion, filters, modulation; round 119 added **Output** (−18…+18 dB) and **Pairs** (H-Delay-style
  ping-pong: right repeats the left at full level, loop gain once per pair) [code].
- Translation `translate = waves-h-delay` (`translateWavesHDelay`, `src/core/PluginSubstitutes.cpp`) [code]:
  host sync → note sync (follows the project tempo); BPM / ms → the time in index 3 (clamped 1–3500 ms);
  feedback → `loop = ping-pong ? 0.93 f² : 0.98 f`, Echoform feedback = loop/2; mix; output = index 14 + level
  offset (ping-pong: −1.4 dB with analog — "calibrated on her takes" — / +2.5 without; stereo −4.4 / −0.4);
  mode **clean**, age / drive / diffusion / ducking 0; HP/LP; modulation depth √(d/100) and rate; LoFi and Analog
  colour are **not** modelled.
- Measured residual on her three takes (reference − LYNX): left −1.1 / +1.7 / −0.7 dB, right −4.3 / +0.1 /
  −3.0 dB; octave bands within ±1.5 dB above 250 Hz; 125 Hz +2…+15 dB (hum)
  ([round 119 §2](../analise/REA_rodada119.md)) [MEAS]. The right-side error is the missing second pass through N
  (§4.3: loud echoes lose ~3 dB more on the R pass) [model]; the −1.4 dB static offset stands in for N on the left.
- Echoform's clean mode still runs its shaper at drive 0.3 inside the character stage [code] — check it is
  transparent at her levels before adding N.

## 7. Implementation guidance for LYNX

Target: her one voicing (ping-pong, fb 60, mix 100, Mod2, 1/2 or 1/4 note). Ordered:

1. **Add a loop dynamics stage N to Echoform** (new mode "hybrid" or a flag set by the translator; leave other
   modes untouched): a memoryless-or-fast gain `y = x·g(|x|_peak)` applied **on every pass** (before the write
   into the line, so the R echo of the pair gets it a second time). Fit g to §4.3: small-signal gain flat at
   ≲ −30 dBFS, ≈ −1.4 dB at −16, −2.9 at −10, −5.3 at −4 dBFS (Mod2 re −34 dBFS), and the impulse data (−3.2 dB
   at −6 dBFS peak first pass). Start with a smooth limiter `g = 1 / (1 + (|x|/x0)^k)^(1/k)` or a soft-knee gain
   computer on an envelope with ~0.1 ms attack / 5–20 ms release, and fit (x0, k, times) by least squares to the
   burst table, the impulse decays (her, stereo, fb 100) and the H3 growth of Off (≈ 1 dB/dB). Then **remove the
   −1.4 / −4.4 dB static offsets** from the translator (they compensated the missing N); recompute the
   small-signal offsets (+1.1 dB analog / +2.5 off over the mono sum).
2. **Mod2 colour outside the loop**: low shelf +0.5 dB (≈ 300–500 Hz corner) and high shelf −0.5 dB (≈ 3–6 kHz),
   fitted to the §4.2 row; plus even-harmonic term `x − a·x²` (Raffel form) with a set so H2 ≈ −62 dB at
   −30 dBFS (level-insensitive, so apply it after a normalising compander, as in BBD units, or as a fixed-gain
   term on a level-normalised copy) [model]. Verify H2/H3 within ±3 dB at −34…−4 dBFS.
3. **Mod2 noise**: white-ish hiss shaped to the §4.4 octave shares (+ ~3 dB/oct tilt), ≈ −85 dBFS RMS per side,
   uncorrelated L/R (check §8), injected before the LoFi LP; recirculating or not per §8 item 4. Do not add
   hum until §8 item 3 confirms its frequency and level.
4. **LoFi** (unused by her, cheap): in-loop 8th-order low-pass ≈ 5.8 kHz followed by a steeper stop band (an
   elliptic 6–8 kHz, or Echoform's `Butter8` + an extra 4th-order at ~8.5 kHz) fitted to §4.2 (both passes).
5. **Analog 1 / 3 / 4**: unmeasured; until measured, map them to Mod2's N with their own noise = 0 (users report
   no hiss on 1 / 3 / 4 [S7]) and flag in the translator comment.
6. **Pass criteria** (her state + her three takes in `tmp/w119/q`; impulses and 1 kHz bursts as in
   `tmp/w119/py/hdmeas.py`): every echo of the first 5 pairs within ±0.5 dB at −26 and −6 dBFS impulse levels;
   burst gain within ±0.5 dB at −40…−4 dBFS; H2 / H3 within ±3 dB; noise floor within ±2 dB per octave;
   her takes: L and R echo levels within ±0.5 dB, octave bands 125 Hz–16 kHz within ±1 dB.

## 8. Open questions to measure (official trial, black-box only)

- [ ] 1. **Where N sits and how fast it is**: 1 kHz bursts stepping −30 → −6 → −30 dBFS inside one burst (Off and
  Mod2) → attack / release of the gain; single impulses at −40…0 dBFS in 3 dB steps → static curve g(peak).
- [ ] 2. **Low-level end**: bursts and impulses at −60, −50, −40, −34 dBFS with long gaps (mask demo mutes) →
  is there expansion below −34 dBFS (the −40 dBFS reading)?
- [ ] 3. **Hum**: 10 s of silence through Mod2 (and with a −30 dBFS 1 kHz tone) at 1 Hz resolution → 50 or 60 Hz,
  harmonic levels, signal-dependent or constant (explains the 125 Hz +2…+15 dB on her takes).
- [ ] 4. **Noise in the loop?** silence at fb 0 vs fb 90 → noise level difference (in-loop noise grows by
  10·log10(1/(1−g²)) for loop gain g).
- [ ] 5. **Analog 1, 3, 4**: the §4.2 / §4.3 / §4.4 set for each mode (spectrum, gain vs level, H2/H3, noise),
  and the dry path at mix 50 % (does Analog colour the dry? [S4]).
- [ ] 6. **Stereo correlation of the noise** (L/R coherence of the silence recording).
- [ ] 7. **Latency** reported by the plugin and the direct-path alignment at mix 50 %.
- [ ] 8. **Filters**: HP / LP slopes and whether they are in the loop (impulse, HP 500 Hz / LP 3 kHz, compare T
  and 2T echoes).
- [ ] 9. **Modulation law**: depth % → ms of delay swing; rate units; LFO phase between sides (sine-tone
  vibrato depth at known settings).
- [ ] 10. **Time-change behaviour** (tape-like glide speed) when the host tempo changes — only matters if her
  projects automate tempo.
- [ ] 11. **Sample rate**: repeat §4.2–4.3 at 44.1 kHz (her projects' rate) to confirm the laws are rate-independent.

## 9. Sources

1. Waves, *H-Delay Hybrid Delay* product page — https://www.waves.com/plugins/h-delay-hybrid-delay — accessed 2026-10-21 — [DOC]
2. Waves, "Presenting the H-Series" — https://www.waves.com/presenting-the-h-series — accessed 2026-10-21 — [DOC]; see also https://www.waves.com/h-series-plugins-overview
3. J. Nilo, "Waves H-Delay explained" (control reference citing the Waves H-Delay user guide) — https://josephnilo.com/blog/waves-h-delay-explained/ — accessed 2026-10-21 — [DOC-secondary]; the guide itself (reseller copy, not read: HTTP 403) — https://www.fullcompass.com/common/files/34416-HDelayHybirdDelayPluginDataSheet.pdf
4. MusicRadar, "Waves H-Delay review" (Analog modes "add distortion to different parts of the frequency spectrum" and also treat the dry signal — via search summary; page body not retrievable) — https://www.musicradar.com/reviews/tech/waves-h-delay-215344 — accessed 2026-10-21 — [DOC] (press)
5. Waves forum, "'Analog' in H-Delay is NOT 'just noise', it shapes harmonics!" (JunkyardSam, 16 Jul 2021) — https://forum.waves.com/t/analog-in-h-delay-is-not-just-noise-it-shapes-harmonics/3696 — accessed 2026-10-21 — [MEAS-third-party]
6. Waves forum, "H-Delay is causing a constant white noise" (Adi.Waves, staff, 24 Feb 2025; simon.a.billington, user, 6 Mar 2025) — https://forum.waves.com/t/h-delay-is-causing-a-constant-white-noise/10931 — accessed 2026-10-21 — [DOC] (staff) / user statement
7. Gearspace, "Noisy Waves H-Delay" (search snippet; page 403) — https://gearspace.com/threads/noisy-waves-h-delay.1241348/ — accessed 2026-10-21 — [MEAS-third-party]
8. C. Raffel, J. O. Smith, "Practical Modeling of Bucket-Brigade Device Circuits", DAFx-10, Graz — https://dafx.de/paper-archive/2010/DAFx10/RaffelSmith_DAFx10_P42.pdf — accessed 2026-10-21 — [PAPER]
9. onsemi, NE570 compandor datasheet — https://www.onsemi.com/download/data-sheet/pdf/ne570-d.pdf — accessed 2026-10-21 — [DOC] (cited for the 2:1 compander; τ = 10 kΩ·C from [8])
10. M. Holters, J. D. Parker, "A Combined Model for a Bucket Brigade Device and its Input and Output Filters", DAFx-18, Aveiro — https://www.dafx.de/paper-archive/2018/papers/DAFx2018_paper_12.pdf — accessed 2026-10-21 — [PAPER]
11. Vintage Digital, Lexicon PCM 42 (released 1982; up to 4.8 s with memory option) — https://www.vintagedigital.com.au/lexicon-pcm-42/ (search summary; page 403) — accessed 2026-10-21 — [DOC]
12. PSP Audioware, Lexicon PSP 42 (PCM42 variable-sampling-rate delay line) — https://www.pspaudioware.com/products/lexicon-psp-42 — accessed 2026-10-21 — [DOC]
13. chowdsp_utils BBD module (Holters–Parker derived) — https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils/namespacechowdsp_1_1BBD.html — accessed 2026-10-21 — [DOC] (open source; check licence)
14. S. Arnardottir, J. S. Abel, J. O. Smith III, "A Digital Model of the Echoplex Tape Delay", AES 125th Convention, paper 7649, 2008 — https://aes2.org/publications/elibrary-page/?id=14800 — accessed 2026-10-21 — [PAPER] (abstract)
15. V. Zavalishin, J. D. Parker, "Efficient emulation of tape-like delay modulation behavior", DAFx-18 — https://www.dafx.de/paper-archive/details/HeLd0OAtZr6hPw_63LViZg — accessed 2026-10-21 — [PAPER] (abstract)
16. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012 — https://aes2.org/publications/elibrary-page/?id=16354 — accessed 2026-10-21 — [PAPER]
17. LYNX REA round 119 §2 (H-Delay layout and laws), §5 (whole chain) — [../analise/REA_rodada119.md](../analise/REA_rodada119.md) — [MEAS]; recordings `C:\Users\kauan\lynx-test\w119\hd\H-Delay_0_*` re-read this round (numpy; impulse peaks, burst windows, FFT harmonics, silence spectra)
18. LYNX source: `translateWavesHDelay` in `src/core/PluginSubstitutes.cpp`; Echoform in `src/core/EchoSuite.cpp` / `.h`; inventory `~/.claude/jobs/f8789b49/tmp/r113/inventory_all.txt` — [code / MEAS]
