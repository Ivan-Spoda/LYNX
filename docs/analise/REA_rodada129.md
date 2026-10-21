# REA round 129 — parity audit of the Waves / SSL / Valhalla / Soundtoys roles, and the originals' preset bar

Branch `feature/parity129`. English-first; clean-room; black-box only (official trials hosted in our own host, their
editors screenshotted as a user sees them; manuals; docs/specs). Round 128 (parity128) audits the FabFilter / Slate /
Antares roles in the same file. User question (2026-10-21): "people tell me details are missing in the plugins — is it
configuration or design?"

## 1. Audit [EXEC + manuals]
`docs/PARITY.md`, section "Waves / SSL / Valhalla / Soundtoys roles": one table per pair (original feature →
✅ / ⚠️ / ❌ / ➕ + note), scores before / after, the gaps ranked by what a user touches first. Answer to the user:
both — but the biggest single thing was the FRAME every original has and LYNX's engines lacked (undo / redo, A / B,
presets with ◀ ▶ and save); then per-plugin defaults, meters and a few missing switches.

Captures: Valhalla references from rounds 100 / 101, Waves from 113 / 119 plus this round's RComp. During this round
the Waves shell started to hang in our host (WavesPluginServer stuck, can't be stopped without admin) and every
Soundtoys 5 trial stops on its iLok activation window → `tmp/parity129/ASK_USER.md`.

## 2. The originals' preset bar for every LYNX engine (ui/PluginHeader.*, core/EffectPresets.*)
- Undo / redo of the plugin's own edits (per slot, for the session; every edit stays on the song's undo list too),
  A / B with copy to the other setting, preset name with ◀ ▶ and a menu: LYNX's factory presets (own settings and
  names, in folders), the user's presets (`<settings>/Presets/<engine>/*.lynxpreset`), Save preset…, Delete, Show
  the presets folder; FL Studio's own presets stay in that menu for the FL effects.
- In both editor families: the generic panels (PluginEditors.cpp header) and the display-first editors
  (ProEditorBase.h header) — so parity128's FabFilter-role editors get it too.
- The slot keeps `presetName`, `abOther`, `abShowingB` (saved in .lynx: `preset`, `abOther`, `abB`).
- Factory presets: a text table (core/EffectPresetTable.inc) written in the units the editor shows ("2.8 s",
  "Plate", "6 kHz"); `keep = …` lists parameters a preset leaves alone (LYNX Tune's key, scale and note switches).
  90 presets for 16 engines. Tests: tests/ParityTests129.inc.

## 3. Per-plugin changes (docs/PARITY.md "Round 129: what changed")
Glow (VU selector, trim, auto make-up [model]), Ridge (GAIN block meter + fader, dynamics before EQ by default),
Compressor (RComp-style panel + meters published), Quell (its meter was never published: `hasLiveMeters` now covers
the voice tools), Gemini (output meters), Echoform (phase L / R + H-Delay translation of RealWorld 8 / 9 — [DATA] her
40 H-Delay states all have 0 / 0, so her sound is unchanged —, tap tempo, section names), Atrium (Early / Late pages),
Nebula (CLEAR = a "momentary" toggle style; a flip of its value resets the network), Foil EQ defaults, Halo's low-cut
text, Lattice default taps; substitute re-routes (EchoBoy / PrimalTap → Echoform, Little Plate → Foil Plate, PanMan
depth +50 % — vmatch127 found the same PanMan bug independently). RComp → Crest was considered (Crest has knee, auto
release, opto style, warm character and meters) but not done: RComp → Compressor is fitted on her vocal chain (round
113 §3c), a re-route needs that refit (vmatch's domain).

## Where I stopped
- Done: §1–§3, tests/ParityTests129.inc (factory presets resolve, value text round trip, user preset files, tune
  presets keep the key, substitute fixes, Glow trim, compressor meters, Nebula clear, Echoform phase, Atrium pages).
- Waiting: the build queue (parity128's four worktrees hold the slots and the Windows lock), then land.
- Next (ranked in PARITY.md "Still missing"): Valhalla description line, Doubler4 voices 3–4, H-Delay own BPM /
  LoFi / Analog, Ridge Split / Dyn S.C. / Analog, Decapitator Punish / Thump / Steep.
