# Round 98 — LYNX substitutes for missing third-party plugins

User-facing description and the table format: `docs/PLUGIN_FALLBACK.md`. Code: `src/core/PluginSubstitutes.{h,cpp}`
(JUCE-free: table, matching, translation, lossless text form of FL's wrapper record), `PluginHost::adoptFlPlugins` /
`applySubstitutes` / `identityOf`, `EffectSlot::substituteFor` (Song.h), `.lynx` field `substituteFor` (+ `foreign`).

No third-party binary was opened, decompiled or run in this round. Evidence is the saved data in five real projects
(the gf92 set, read in place, never copied into the repo) and the vendors' public documentation.

## 1. What FL's wrapper records of real projects show [DATA]

Survey script: `tmp/r98/survey.py`, `ids.py`, `dump.py` (FL event 0xD5, wrapper records 0x32 type, 0x33 VST2 ID,
0x34 VST3 class ID, 0x35 state, 0x36 name, 0x37 path).

| Plugin (wrapper name) | Format | Identifier | State (record 0x35) |
|---|---|---|---|
| ValhallaVintageVerb | VST3 | class `7654535665657633616c68616c6c6176` | component = "VstW…CcnK…FBCh" header + `VC2!` + JUCE XML; root element `<ValhallaVintageVerb … Mix= PreDelay= Decay= Size= … ReverbMode=…>`, values 0..1 |
| WaveShell1-VST3 15.0/15.5/16.0 | VST3 | class per sub-plugin (ASCII-like: "UTSVCRrScompress" RComp, "LTSV2AcSla-2a st" CLA-2A, "STSVHCsSslchanne" SSLChannel, "HTSVDBhS-delay s" H-Delay, "ATSVSNnSs1 stere" NS1, "DTSVASdSeesser s" DeEsser, "WTSV2DdSoubler2 " Doubler) | component = binary header + `<PresetChunkXMLTree>`: `<PluginName>`, `<ActiveSetup>`, per setup `<Parameters Type="RealWorld">` whitespace-separated numbers, `*` for unused |
| Auto-Tune Pro | VST3 | class `415453563854413175746f2d54756e65` | `VC2!` + XML |
| Decapitator / EchoBoy / Little AlterBoy / PanMan (Soundtoys) | VST2 | `StDc` / `StEB` / `StLa` / `StPn` | FL VST2 v−9, chunk: text `WIDGET = …; VERSION = …; PRESET =<encoded>` (not readable) |
| Sausage Fattener | VST2 | `MYPL` | chunk (binary) |
| Fresh Air, Kickstart 2, Filterverse, Effectrix2, Analog Lab V | VST3 | class IDs in the table comments / survey output | vendor binary / zip / boost archive |

Consequences:
- **Waves**: the wrapper name and path are the shell's; the sub-plugin is identified by the class ID or by
  `<PluginName>` in the state. The previous name fallback of `findForeign` could match any plugin of the same shell
  file → fixed (shell: match by the state's PluginName, "X Stereo" first, never by file name).
- **Valhalla**: the JUCE XML is readable; `Mix` is documented by the vendor as the wet/dry balance 0–100 % [DOC];
  read as a linear crossfade [model], it becomes the slot dry/wet of a 100 %-wet Reeverb 2. Other attributes: their
  normalised→unit laws are not confirmed → not translated.
- **Waves RealWorld lists**: readable numbers, but which index is which control is **not confirmed** → no maps yet
  (the `waves:<n>` source is implemented and tested for when black-box measurements give the layout).
- **Soundtoys / Sausage Fattener**: encoded chunks → default presets.

## 2. Behaviour implemented

- `.flp` import: an effect slot whose plugin isn't found becomes the substitute (instead of being dropped) when the
  setting is on; the FL record (all fields, state byte for byte) stays in `substituteFor.foreign`.
- `.lynx` open (`MainComponent::openLynxProject`): substitutes whose original is now installed go back to it (FL
  records are then converted by `adoptFlPlugins`); plugin slots whose plugin is missing get substituted.
- `.lynx` save: `"substituteFor": { id, name, state, foreign }`, `foreign` = base64 of a versioned binary record
  (`foreignToText`), round trip byte-identical (tests + selftest `plugin_substitutes=OK`).
- Automation of the original's parameters is kept but ignored by the renderer while substituted.
- UI: mixer slot shows the original name + "LYNX" badge; the slot detail says "LYNX substitute for X"; Missing files
  lists "LYNX substitute: <effect> (the original plugin is used again once it is installed)"; Settings › General
  toggle (default on, `%APPDATA%\LYNX\substitutes.txt`); Quick add + mixer menu list the LYNX equivalents.

## 3. Where I stopped / next steps

- Instruments: entries accept `role = instrument` but the loader doesn't apply them (a missing instrument still gets
  the built-in synth by channel name).
- Parameter maps for Waves (RealWorld indices) and more Valhalla attributes need black-box measurements on licensed
  copies (render test signals, sweep one control at a time).
- pitch95 / vocal96 / ffmeas99 modules: add their entries (Auto-Tune, DeEsser, NS1, Doubler, Fresh Air…) to
  `builtInTable` when they land.
- Patcher (parallel container) children that are plugins are not substituted.
