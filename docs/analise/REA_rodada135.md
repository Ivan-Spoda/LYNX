# REA round 135 — parity135: the ❌ / ⚠️ items of docs/PARITY.md

Branch `feature/parity135`, split in four sub-rounds (each with its own round doc):
`REA_rodada135f.md` (FabFilter / Slate / Antares roles), `REA_rodada135s.md` (Soundtoys roles + Voice Shift),
`REA_rodada135v.md` (Valhalla roles + the description line), `REA_rodada135w.md` (Waves roles + all of Echoform).
English-first, clean-room, black-box only for third-party plugins. Round 136 (vox136) tunes the DSP of the same
engines in parallel; coordination in `tmp/coord/r135-r136.md`.

## 1. What could be measured this round [EXEC]
- Soundtoys 5 trials (activated by the user, 30-day licence): Decapitator, PanMan, Little AlterBoy, EchoBoy, Little
  Plate, PrimalTap load in our host (`uiview.exe`): parameter tables (names, ranges, texts) and editor screenshots
  (`D:\lynx-work\parity135\st`). Her saved Soundtoys states were loaded into the trials through the plugin's own state
  API and their parameters read back (`get`) — the encoded payload is never decoded (135s §).
- Auto-Tune Pro (re-activated): Classic measured black-box (135f §).
- Waves: no licence on this PC — demo mode only (UI usable, audio mutes): controls / layout only.

## 2. Results
docs/PARITY.md "Round 135: summary": 149 ❌ rows → 36; every pair's config / design score before → after.
Biggest changes: a new engine (Voice Shift, the Little AlterBoy role, 696 slots in her projects), Ember's Decapitator
controls, Auto-pan's PanMan sources and display, Apex's loudness meter, Prism's six bands, Echoform's H-Delay /
ValhallaDelay / EchoBoy controls, LYNX Tune's Real-time page / Learn / Classic, the description line in every LYNX
editor.

## 3. Merge notes
- LYNX Tune: 135f's block (Learn MIDI, Classic) comes before 135w's (Speed link … Correction); the held-MIDI mask
  moved to meter output 5 (outputs 3 / 4 are 135w's input / output peaks).
- EffectKind: `formulaController` (round 133) then `voiceShift`; loops use `lastEffectKind`.

## Where I stopped
- Landed: see the final report (commits on main). Desktop pages `LYNX-paridade/` regenerated with the round-135
  screenshots (`--plugui` of the merged build) and the Soundtoys trial captures.
- Left (biggest first): EchoBoy Rhythm mode; PanMan rhythm-shape editor, Feel / Groove, envelope / trigger page;
  Facet 24 bands (needs `maxEffectParams` raised), Brickwall / 0 dB slopes; Prism free (overlapping) bands, phase
  modes; Crest Host-Sync / MIDI triggering; ValhallaDelay Era; Glow ANALOG; Ember Tone before the drive and fitted
  voicings (vox136); Voice Shift MIDI pitch control; Halo programs; Lattice per-mode LFOs.
