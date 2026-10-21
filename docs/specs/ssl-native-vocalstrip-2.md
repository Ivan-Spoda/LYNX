# SSL Native Vocalstrip 2 — spec dossier (LYNX substitute: Ridge Strip, untranslated)

Round 125 (spec125). Clean-room dossier, short (minor plugin in her corpus). Built from SSL's public user guides
and the readable XML state saved in her projects. No SSL binary was opened, decompiled or instrumented, and the
plugin was not measured (it is PACE-protected and not installed on the measuring PC).
Tags: [DOC] vendor document · [MEAS] our measurement / data read from her files · [model] our inference.
All web sources accessed 2026-10-21. Companion dossier: [ssl-e-channel.md](ssl-e-channel.md).

**Summary.** Vocalstrip 2 is SSL's own vocal channel strip: de-esser, de-ploser, 3-band EQ (HPF, notch / peak,
high shelf), and a "compander" (a fixed-ratio downward expander followed by a compressor with make-up and an
optional output drive), with user-set processing order [S1][S2][DOC]. Unlike the Waves plugins, its state is
**plain XML with named parameters in physical units**, so translating it needs no measurement of the layout
(§3). In her corpus it is rare (12 slots, all backup autosaves of a single project, 0 main projects), and only
**one** state does anything: a heavy vocal compressor (−33.2 dB threshold, 10.5:1, 25.5 ms / 1.06 s, +14 dB
make-up, −17.1 dB output, −3.4 dB input). LYNX maps it to Ridge Strip today with **no translation** (neutral).
Low priority; cheap win: translate the compander to Ridge's compressor (or to LYNX's Compressor) from the XML.

## 1. Identity

| Item | Value | Source |
|---|---|---|
| Vendor / product | Solid State Logic — SSL Native Vocalstrip 2 (part of the SSL Native range) | [S1][S2][DOC] |
| Version in her projects | `Version="6.9.4"` in the saved XML; VST3 `SSL Native Vocalstrip 2.vst3` | [MEAS] `tmp/vm124/st/SSL_Native_Vocalstrip_2_*.bin` |
| Blocks | "four configurable processing blocks: an SSL Compander, De-esser and De-ploser, and an application specific EQ" | [S1][DOC] |
| Engine | "64-bit floating point engine"; "Global latency-free bypass" | [S1][DOC] |
| Hardware lineage | none direct (an SSL software design; the compander is not a model of a console section) | [S1][DOC], [model] |
| Her usage | **12 slots, 12 files, 0 main projects, 2 distinct states**; state 0 in 11 slots, state 1 (factory default) in 1 slot, all in `okkk/Backup/okkk (autosaved at 21h0x)_2.flp` | [MEAS] `tmp/vm124/inv.txt`, `tmp/vm124/st/index.tsv` |

## 2. Signal flow

```
in -> input gain -> [ De-esser | De-ploser | EQ (HPF, notch, high shelf) | Compander ]  (order user-set)
                                                     Compander = expander (fixed ratio) -> compressor -> make-up
                                                                 -> optional Drive (harmonics, grows with make-up)
   -> output gain -> out
```
- Processing order is user-controlled ("Complete control over process order") [S1][DOC]; saved as `processOrder`
  (0..1; her state 0.478 — the permutation encoding is not documented) [MEAS] / [model].
- De-esser and de-ploser default to SPLIT (gain reduction only in the detected band), switchable to BROADBAND;
  optional Lookahead "increases latency" [S1][DOC].
- Compander: "First the signal enters a fixed ratio downwards expander … Turn the threshold to 0dB to stop the
  expander affecting the input signal." Then a compressor with "fully variable Ratio, Thresh, Attack, Release and
  Makeup controls, along with a choice of Hard or Soft Knee." "The output stage, which is sourced after the
  make-up gain, features an optional Drive … Its intensity is increased with the Makeup gain." [S1][DOC]

## 3. Controls (XML `PARAM id` → meaning) and her values

Documented ranges [S1][S2][DOC]: HPF 30–300 Hz "with a slight boost around its cut-off frequency"; notch
200 Hz–10 kHz, "12dB of boost and 36dB of attenuation with a high Q value"; high band ±12 dB over 1–20 kHz "with
a low Q value". Compressor/expander numeric ranges are not given in the guides (not verified).

| XML id | Meaning | State 0 (11 slots) | State 1 (default) |
|---|---|---|---|
| `bypassAll` | plug-in bypass | 0 | 0 |
| `inputGain` / `outputGain` | dB | **−3.40 / −17.10** | 0 / 0 |
| `compOn` | compressor block on | **1** | 0 |
| `compThreshold` | dB | **−33.2** | 0 |
| `compRatio` | :1 | **10.466** | 4 |
| `compAttack` | ms (unit [model]: value range 0.1…) | **25.5** | 0.1 |
| `compRelease` | ms [model] | **1063.6** | 100 |
| `compKnee` | 0 hard / 1 soft [model; guide: "Hard or Soft Knee"] | **1** | 0 |
| `compMakeup` | dB | **14.0** | 0 |
| `compDrive` | output drive on/off | 0 | 0 |
| `expThreshold` | dB (0 = off per guide) | −74.9 | −96 |
| `eqOn` | EQ block on | **0 (EQ off)** | 0 |
| `hpfOn` / `hpfFreq` | HPF on, Hz | 0 / 286.6 | 1 / 30 |
| `notchOn` / `notchFreq` / `notchGain` | notch/peak band | 0 / 5718 / −3.2 | 1 / 200 / 0 |
| `hsOn` / `hsFreq` / `hsGain` | high shelf | 1 / 1485.6 / +6.5 (inactive: `eqOn` = 0) | 1 / 1000 / 0 |
| `dsOn`, `dsFreq`, `dsThreshold`, `dsSplit`, `dsLookAhead`, `dsAudition` | de-esser | off (0), 19325 Hz, −20.7, 0, 0, 1 | off |
| `dpOn`, `dpFreq`, `dpThreshold`, `dpSplit`, `dpLookAhead`, `dpAudition` | de-ploser | off (0), 69.9 Hz, −19.9, 0, 1, 0 | off |
| `processOrder` | module order code | 0.478 | 0 |
| `fft` | analyser display | 0 | 0 |

The state also stores an A/B pair (`StateASelected="1"`; B holds factory defaults) and the last preset path
(`…\Presets\Vocalstrip2\Default Preset.xml`) [MEAS]. Only the A block plays.

**What state 0 actually does** [model, from the values and [S1]]: EQ, de-esser and de-ploser are off, so it is
a −3.4 dB trim → expander (threshold −74.9 dB, practically inactive on a vocal) → soft-knee compressor 10.5:1 at
−33.2 dB with 25.5 ms attack and ~1.06 s release → +14 dB make-up → −17.1 dB output. For a vocal peaking near
−10 dBFS that is ~20 dB of gain reduction and a net level change around −6…−10 dB — a strong, slow leveller.
These levels are inferred from the parameter values; the plugin was not measured.

## 4. Known curves & behaviour

Not measured. The guides give no detector type (peak / RMS), knee width, expander ratio or drive curve [S1][S2].
SSL's Channel Strip in the same Native range documents an RMS detector with soft knee by default and a peak /
hard-knee option [S2][DOC]; whether Vocalstrip's compander shares that design is not documented (not verified).

## 5. Published science

Generic: a feed-forward log-domain compressor with a soft knee and branching attack/release smoothing
(Giannoulis, Massberg, Reiss 2012) describes the documented controls fully [S3][PAPER]; the gain computer is

    y = x                                   for 2(x − T) < −W
    y = x + (1/R − 1)(x − T + W/2)² / (2W)    for 2|x − T| ≤ W
    y = T + (x − T)/R                        for 2(x − T) > W

with the downward expander as the mirror image below its threshold (fixed ratio, not documented).

## 6. What LYNX does today

`[ssl-native-channel]` in `src/core/PluginSubstitutes.cpp` maps "SSL Native Vocalstrip 2" (and Channel Strip /
4K E / 4K B) to **Ridge Strip with no `translate =`**, so the slot plays Ridge at its neutral defaults
([PLUGIN_FALLBACK.md](../PLUGIN_FALLBACK.md)). For state 0 that means no compression and a level difference of
roughly 6–10 dB versus the original [model].

## 7. Implementation guidance

1. Add a translator `ssl-vocalstrip-2` (XML read, like `autoTuneProParams` / `attributeIn` in
   PluginSubstitutes.cpp): parse the `<A>` block's `PARAM id/value` pairs.
2. Map `compOn`, `compThreshold`, `compRatio`, `compAttack`, `compRelease`, `compKnee`, `compMakeup`,
   `inputGain`, `outputGain` onto LYNX's **Compressor** (attack/release in ms, make-up, knee) or onto Ridge's
   compressor (Ridge has no attack control: it has a fixed 10 ms / fast 1 ms attack, ratio ≤ 20 (+∞), threshold
   −40…0 dBFS, release 0.1–4 s; 25.5 ms attack would be approximated). The Compressor is the better target.
3. EQ only when `eqOn` = 1 (HPF with a resonant bump → RBJ high-pass with Q ≈ 0.8–1 [model]; notch → peaking
   with high Q; high shelf → low-Q shelf); de-esser → Hush when `dsOn` = 1. All off in her state.
4. Pass criterion (after one black-box measurement, §8): static curve within 1 dB on 1 kHz sine stairs −60…0 dBFS,
   take level within 0.5 dB.

## 8. Open questions to measure (only if the user installs the official trial)

- [ ] Static curve at state 0 (1 kHz stairs, −60…0 dBFS, 3 dB steps): threshold reference (dBFS?), knee width,
      whether make-up is pure gain. Decides the threshold offset and knee.
- [ ] Attack / release (tone burst −30 → −10 dBFS): units of `compAttack` / `compRelease` (ms assumed).
- [ ] Detector peak vs RMS (sine vs square at equal peak).
- [ ] Expander ratio (set `expThreshold` −30, stairs below it).
- [ ] `processOrder` encoding (move one module, save, read the value).

## 9. Sources

1. Solid State Logic, "Vocalstrip 2 User Guide" — https://www.solidstatelogic.com/assets/uploads/downloads/Brochures/Vocalstrip%202%20User%20Guide.pdf — accessed 2026-10-21 — [DOC]
2. Solid State Logic, "SSL Native v6.5 User Guide" (Channel Strip and Vocalstrip 2 chapters) — https://www.solidstatelogic.com/assets/uploads/downloads/plug-ins/SSL%20Native%20v6.5%20-%20User%20Guide.pdf — accessed 2026-10-21 — [DOC]
3. D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012 — https://aes2.org/publications/elibrary-page/?id=16354 (JAES 60(6) pp. 399–408) — accessed 2026-10-21 — [PAPER]
4. Her corpus: `~/.claude/jobs/f8789b49/tmp/vm124/inv.txt`, `tmp/vm124/st/SSL_Native_Vocalstrip_2_{0,1}.bin`, `tmp/vm124/st/index.tsv` — [MEAS]
