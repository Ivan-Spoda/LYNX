# REA round 127 — the remaining substitutes of her vocal projects (vmatch127)

Branch `feature/vmatch127`. English-first; clean-room. Scope (coordinator, round 123 brief): her projects (tmp/gf/all +
tmp/gf/proj2) must sound the same in LYNX when a plugin is missing. This round covers the corpus plugins not taken
by vmatch123 / vmatch124: PanMan, the Auto-Tune extras (EFX+, Detune / Transpose / Mix / Input Type, the corpus
count conflicts), FabFilter Pro-Q 4 / Pro-DS, CLA-76, Little AlterBoy, Decapitator, EchoBoy and the minor plugins
(docs/specs/).

Evidence markers: [DATA] read from her project files, [EXEC] measured (our hosts / LYNX), [model] our reading, not
confirmed. THIRD-PARTY RULE: official trials only, black-box only (hosted through the VST3 API, fed signals, outputs
measured, parameters read back through the plug-in's own parameter API); nothing decompiled, patched or copied.
Harness: `tmp/vm127/py/vm.py` (fxmeasure + fxlynx), renders on D: (`D:\lynx-work\vm127`, C: is nearly full).

## 0. Her states (step 0) and the two corpus-count conflicts

[DATA] Every saved state of the whole copy (907 + 3 .flp incl. autosaves) is in `tmp/vm124/st` (vm124's extraction):
Auto-Tune Pro 1838 states, Decapitator 63, Little AlterBoy 52, PanMan 50, EchoBoy 22, Waves Tune RT 11,
Auto-Tune EFX+ 6, Auto-Key 4, Pro-Q 4 4, Pro-DS 2, CLA-76 1.

- **Chromatic Auto-Tune slots.** The spec's "412-project inventory" (`tmp/r113/inventory_all.txt`, 16:02 on
  10-20) ran while `copyall.sh` was still copying: exactly 412 of today's 907 .flp files are older than it, the other
  495 arrived afterwards. The chromatic states live in files copied later. Re-run on the full copy with one regex
  (`ScaleGivenScale="(\d+)"`, every .flp): scale 1 (minor) 1835 slots, **scale 2 (chromatic) 296 + 2 (proj2)**,
  scale 0 (major) 27; the main projects alone: 78 / 42 / 2 (+ proj2 5 / 2 / 0). Round 120's 43 chromatic of 143
  is the main-project view. No conflict in the decoding — only in the snapshot.
- **Waves Tune Real-Time.** Same cause: the inventory saw 6 slots / 1 state; round 120's 59 slots came from
  `tmp/r113/wv` (the states extracted at round 113 time); the full copy has **80 slots, 11 states, 6 main slots in 4
  projects**.
- **Soundtoys payloads are not readable.** [DATA] The `PRESET =` text after its 20-digit header unpacks (6 bits per
  character, either bit order) to bytes with 7.5–7.9 bits of entropy per byte (Decapitator, PanMan, Little
  AlterBoy, EchoBoy) — compressed or encrypted, not a value list. Per the clean-room rule we stopped there: their
  settings can only come from the official plug-in (its parameter read-back), not from the bytes.

## 1. PanMan → Auto-pan: the pass-through bug

[DATA] `preset = Depth=0.5` mapped to an Auto-pan depth of `2·0.5 − 1 = 0`: the substitute was an exact
pass-through in all 386 slots. Round 113 meant "50 %" (it lowered the old 75 % value thinking the knob was the depth).
Fix: `Depth=0.75` (depth +0.5: the balance swings ±50 % once per beat, synced to the song position) and the comment
/ PLUGIN_FALLBACK row corrected. Test `panman_substitute_moves_the_balance_round127` renders two beats and checks
both channels swing between 1.0 and 0.5. Her real PanMan settings stay unknown until the trial is activated (§6).

## 2. Auto-Tune extras → LYNX Tune (translation only; the engine is untouched)

- **Auto-Tune Pro** (`translateAutoTunePro`): besides key / scale / retune / humanize / flex-tune, now X3 Detune
  (cents → LYNX Tune's A4 reference, which reaches ±39 c), X74 Transpose, X92 Mix, X10 Input Type (Soprano, Alto-Tenor,
  Low Male, Instrument, Bass Instrument → LYNX Tune's four ranges), X70 Formant Correction off → formants move with the
  pitch, X71 Throat Length (−12·log2(L/100) semitones of formant shift [model]), X11 Classic mode (formant
  correction, Flex-Tune and Transpose off, as the manual says) and X9 Tracking (50 = LYNX Tune's default [model for
  the rest]). [DATA] Her 143 main-project states all use Detune 0 / Transpose 0 / Mix 100 / Alto-Tenor / Throat 100 /
  Tracking 50, so her sound does not change; the 2 Classic states now lose formant correction and Flex-Tune as the
  original does, the one Formant-off state too.
- **Auto-Tune EFX+** (new `translateAutoTuneEfx`, reached through the same entry): [DATA] its 6 states are an
  `<AUTOTUNEEFXPLUS>` list of `<PARAM id value>` in real units. pScale uses Auto-Tune Pro's numbering (every pScale 2
  state has all twelve notes on; every pScale 1 state on key 11 holds exactly B minor's notes), the pNote switches
  are absolute pitch classes, and one state adds F to B minor — so the notes become LYNX Tune's note switches (a
  custom scale when they differ from the named scale). Retune (20 ms in 5 states, 221.6 ms in 1; Auto-Tune Pro's
  law, [model] for EFX+), humanize, pitch shift (−12 st in one state) → transpose, formant / throat, input type, mix
  (95 % in one). Before: the EFX+ slots got the gentle chromatic default (or the project key). `pitchCorrectorScale`
  also reads EFX+, so the project-key vote no longer overwrites its key.
- **Auto-Key** (52 slots, 4 states): an analyser (the manual describes no audio processing): left out, as FL does.
  Its LatestKeyType numbering is unverified, so it does not vote for the project key.
- Tests: `auto_tune_pro_extra_controls_translate_round127`, `auto_tune_efx_states_translate_round127`.

## 3. CLA-76 → Crest Compressor (measured)

[EXEC] Official trial (WaveShell1-VST3 17.1, "CLA-76 Stereo"), her only state loaded and read back through the
plug-in's parameter API: **Input −8.7 dB, Output −16.9 dB, Attack 4.30, Release 4.31, Ratio All, Analog Off,
Revision Blacky, Mix 41.3 %** — so RealWorld index 59 is the ratio (4 = all buttons) and 103 the Mix (the spec's
open question). The Waves trial still mutes ~1 s per 20 s in our host (demo); mute windows are masked out.

- Mix = an exact linear blend (her output = 0.5870·dry + 0.4130·wet, residual −147 dB); the dry path is exact.
- Wet path (1 kHz stairs, 3 dB steps): **+31.45 dB** below −42 dBFS, then a plateau (−9.7 dBFS out for −36 … −24 in),
  ~5:1 above, and a **32 dB gain-reduction ceiling** (−6 … 0 dBFS in → −5.9 … −0.5 out). Attack ≈ 1–2 ms to most of the
  reduction; release slow and accelerating (≈ 0.3 dB in the first 100 ms, then ~22 dB/s). H2 −43 … −51 dBc.
- Crest fitted to the wet path (Nelder–Mead over threshold, ratio, knee, range, attack, release, output, 6 styles;
  `tmp/vm127/py/clafit2.py`): **Bus, −43.7 dB, 7.1:1, knee 9.3 dB, range 31.9 dB, attack 5.9 ms, release 97 ms,
  +31.6 dB**, the 41.3 % as the slot's dry / wet. New entry `[waves-cla-76]` + `translate = waves-cla-76` (other
  settings move around her point by the manual's laws [model]).

| her take / signal | before (Compressor 2:1, auto gain): null / level / env RMS / 1/3-oct RMS | after |
|---|---|---|
| luisa (lu) | −11.5 dB / −0.98 / 13.5 / 2.99 | **−20.0 dB / −0.14 / 1.96 / 0.33** |
| game over (go) | −12.6 / +0.40 / 5.94 / 4.37 | **−22.9 / −0.11 / 2.76 / 1.08** |
| kwe | −8.0 / +2.19 / 4.72 / 3.11 | **−20.5 / −0.58 / 1.80 / 0.62** |
| pink −20 dBFS | −5.9 / +2.88 / 2.81 / 2.58 | **−16.5 / −0.21 / 0.63 / 0.35** |
| 1 kHz stairs | −10.4 / −2.13 / 16.5 / 8.8 | **−29.5 / −0.01 / 0.64 / 2.69** |

Not yet at the −30 dB target: the remaining error is the program-dependent release (Crest's release is one time
constant) and the plateau shape of all-buttons mode. Test `waves_cla76_translates_onto_crest_round127`.

## 4. FabFilter Pro-Q 4 / Pro-DS

[EXEC] The installs are official: `C:\Users\kauan\Downloads\fftotalbundlex64.exe` is Authenticode-signed by
FabFilter (valid), downloaded from cdn-b.fabfilter.com (Zone.Identifier), installed 10-17 02:37; the plug-in files
are byte-identical between `Program Files\FabFilter` and `Common Files\VST3` (the plug-in DLLs themselves are not
signed, the installer is). Her 4 Pro-Q 4 and 2 Pro-DS states were rendered through them (`D:`/`C:` vm127 out
`proq4_*`, `prods_*`: her takes, pink, impulse; Pro-Q 4 latency 0, Pro-DS 720 samples). **The translation itself was
already done in parallel by vmatch124x** (`origin/wip/vmatch124x` f261957: Pro-Q 4 → Facet, Pro-DS work in
progress in `tmp/vm124x/pds`), so it was not duplicated here.

## 5. Minor plugins

Maag EQ4's only state is flat (pass-through is exact); Harrison 32C's only slot is an empty state; Effectrix2 /
Bloom / Analog Lab V are not substitutable (patterns / instruments). God Particle and HG-2 have no trial on this PC.
No change this round.

## 6. Soundtoys (Decapitator, PanMan, Little AlterBoy, EchoBoy)

The user installed Soundtoys 5.5 (official; the installer is signed by SoundToys, Inc.). Loading any of them in our
host stops at a PACE window **"Software Activation: Decapitator V5" (Activate / Quit)** — licensing needs the user,
so `tmp/vmatch127/ASK_USER.md` asks for the trial activation. Until then their substitutes keep the defaults above
(PanMan fixed in §1).

## Where I stopped

- Landed: PanMan fix, Auto-Tune / EFX+ translation, CLA-76 → Crest (this doc).
- Next: once Soundtoys is activated — read back her 63 Decapitator / 50 PanMan / 52 AlterBoy / 22 EchoBoy states
  through the VST3 parameter API (her chunks are VST2 `StDc`/`StPn`/`StLa`/`StEB`; check that the VST3 build accepts
  them, else the VST2 DLLs in `C:\Program Files\VSTPlugins\Soundtoys`), measure, translate (Ember for Decapitator,
  Auto-pan for PanMan, Echoform for EchoBoy, a LYNX Tune-based shifter for AlterBoy).
- CLA-76: a two-stage (program-dependent) release in Crest would take her take from −20 toward −30 dB.
