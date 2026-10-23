# REA round 132 — issue #4 feature requests (BURNER1PA)

Branch `feature/feat132`. English-first; clean-room (FL Studio's own UI forms and strings read as data, no code
copied). Round 131 (bugs131) fixes the bugs of the same issue in parallel (window focus, MIDI input, pattern length,
song loop); this round stays out of those areas.

Evidence markers: **[DFM]** FL's form resources (Delphi DFM, the dump from round 81 `tmp/r81/dfm_all.txt`),
**[STR]** strings of `FLEngine_x64.dll` (FL Studio 2026), **[TEST]** core tests (tests/Feat132Tests.inc),
**[HYP]** our reading, not confirmed.

## 1. What FL does

| Request | FL Studio | Evidence |
|---|---|---|
| Audio editor in the View menu | FL's View menu lists its windows (Playlist, Piano roll, Mixer, Channel rack, Browser, project info, tempo tapper...) | [STR] "View Piano Roll", "View tempo tapper", "View channel rack" |
| Browser preview volume | The Browser has a sample preview volume slider (`forms.browser.samplepreviewvolslider`), hint "Set global sample preview volume", "Browser preview volume"; it is a global setting, not a project one | [STR] |
| Channel Rack piano-roll view | The rack's title bar has `IntSSBtn`, hint "Interactive step sequencer / piano roll overview", beside `GraphEditorBtn` (Ctrl+K "Graph editor") and `ShuffleSlider` ("Main swing"); options "Show complete piano roll preview", "Mini piano roll preview" | [DFM] TStepSeqForm, [STR] |
| Metronome volume | The metronome button has a popup menu (`MetronomePopup`: metronome sounds, precount); the level is set through the "Metronome target mixer track" / "Browser preview & metronome target mixer track" fader | [STR] |
| Tap tempo | `TTapTempoForm` "Tempo tapper": a 132×132 TAP button ("Tap me, or press any key on your MIDI keyboard"), a waiting gauge, "Apply tempo", "Sync" ("Resynchronize to taps"), two nudge buttons ("Nudge tempo to align time", Left/Right or pitch bend), "Mute" (mute all audio during the first taps) and "now"; states "waiting for more...", "Tapping...", "stabilized." | [DFM] [STR] |
| Gain knob for sample channels | FL's Sampler has no gain knob: the SMP page's "Precomputed effects" are Remove DC offset, Normalize, Reverse polarity, Fade stereo, Reverse, Swap stereo, IN/OUT fades, CROSSFADE, TRIM, SMP START/LENGTH; the level is the channel volume (0–125%) | [DFM] TPluginForm FXPanel |

## 2. What LYNX does now

1. **View › Audio editor** (and the assignable action `view.audioEditor`): opens the selected channel's sample when
   it is a Sampler / audio-clip channel with a sample, else an empty editor (Edit › Audio editor... stays).
   **View › Tempo tapper** too (`view.tempoTapper`).
2. **Browser preview volume**: a small knob at the end of the search row, 0–125% (100% = the old level), square-law
   gain (50% = −12 dB, `controls::utilityGain`) **[TEST]**; it acts on the preview playing now (PreviewPlayer's
   browser previews only — the audio editor and the start-up sound keep their own levels). Global, saved in
   `ui-options.txt` (`previewVolume`), like FL's global setting.
3. **Channel Rack piano-roll view**: a piano-roll button after the graph editor button and the swing knob in the rack's
   title bar (also in the rack's panel menu and the action `rack.pianoRollView`). On: every channel row shows the
   miniature piano roll (step patterns included, empty rows as an empty strip); a click selects the channel and opens
   its notes in the Piano Roll; drag-painting steps is off on those rows **[TEST]**. Off: the steps, with the
   miniature only for rows that have piano-roll notes (as before). Saved in `ui-options.txt` (`rackPianoRollView`).
4. **Metronome volume**: right-click on the metronome button → on/off, "Volume: …" (a callout with a knob),
   preset levels, count-in; Options › Metronome volume (…)... too. 0–125%, the click's level follows at once
   (`Transport::metronomeVolume`, Metronome.cpp), saved in `ui-options.txt` (`metronomeVolume`). LYNX has no
   metronome mixer track, so the knob replaces FL's "target mixer track" fader.
5. **Tempo tapper** (src/ui/TempoTapper.*): a tool window with a big TAP pad (click, or any key), the measured BPM,
   the state (waiting / tapping / stable — stable = at least four taps within 4% of their mean **[TEST]**), a gauge
   of the time left before the taps start over (2 s), "Apply tempo" (off = only measure; turning it on applies the
   measured tempo), "Sync" (during playback each tap puts the playhead on the nearest beat **[TEST]**), and two
   nudge buttons (or the Left/Right keys) that run the song 4% slower/faster while held, without undo steps
   **[TEST]**. The tempo display's right-click "Tempo tapper..." opens it (it replaces the old small tap callout).
6. **Sampler Gain knob**: Channel settings of a sample channel → "Gain", −24…+24 dB, default 0 dB (double-click),
   applied to the sample before the channel volume, in LYNX's sampler voices and in FL's imported native voices
   (`SamplerSettings::gainDb`, `samplerGain`) **[TEST]** (exact ×2 at +6.02 dB, ×¼ at −12.04 dB, with and without
   the channel filter). Saved in .lynx (`sampler.gainDb`, only when not 0). FL import: nothing to translate (FL has
   no such knob, §1), so imported projects keep 0 dB.

## 3. Not done / left

- FL's tapper also takes taps from MIDI keys and has "Mute" / "now" (mute the song during the first taps); LYNX's
  tapper takes mouse and computer-keyboard taps only (MIDI input is being reworked by round 131).
- Metronome sounds (FL's Met1/Met2 choices) are not offered; the click is LYNX's own synthesised one.
- FL's "Normalize" precomputed effect (byte 0x50 of the sampler record, REA_rodada3 [HYP]) is still not imported.

## Where I stopped

All six requests implemented with save/load and pt-BR strings; core tests in tests/Feat132Tests.inc. See the final
report of the round for the landed commits.
