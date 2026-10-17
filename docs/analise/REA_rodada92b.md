# REA round 92b: FL channel level adjustments and stretched clip offsets (gf92 follow-ups)

Context: round gf92 (main 722a512) fixed FL 26's 88-byte playlist items and clip gain/fades for a real user project
(an FL 26.1.3 song, 130 BPM, 31 audio clips). Three differences were left; this round closes two, the third (the
Channel Rack on/off switch, event 0x00) landed with round 80.

## 1. Channel level adjustments (0xE5)

[DATA] 0xE5 (20 B) per channel: i32 pan adjustment (±6400, 0 = neutral), i32 volume adjustment (0…12800,
12800 = neutral), i32 ?, i32 mod X adjustment, i32 mod Y adjustment (REA_rodada3 §2). In the user's song nine audio
clip channels have a volume adjustment (4970…10077).

[EXEC] Decisive FL command-line render (`FL64.exe /R /Ewav`): the same project with only channel 5 audible, once with
its adjustment 7286 and once with 12800. Level ratio 0.303 (−10.4 dB; best-fit gain 0.3030, residual −29.7 dB, the
master limiter). A linear factor 7286/12800 would give −4.9 dB, FL's volume curve f(x) = (11^x − 1)/10 gives
−10.7 dB → the curve: gain = f(volume) · f(volAdj), as REA_rodada3 §2 [DECOMP] says. The earlier "linear fits her
render" impression came from comparing the whole intro, where other parts were still missing.

LYNX: the importer folds f(volAdj) into the channel volume (on the same curve) and adds panAdj/6400 to the pan.
Files older than 5.3.7 store the values at 1/100 scale (multiplied back). Not done: mod X/Y adjustments; a channel
volume automation clip replaces the volume and so drops the adjustment (not confirmed what FL does there).

## 2. Time-stretched audio clips: start offset

[DATA] For a channel with a stretch time (0xD7 TIME > 0) the playlist item's start/end offsets (+0x18/+0x1C) are in
the sample's own milliseconds, so end − start = clip length × stretch factor (e.g. 31 219 ms over 64 beats =
29 538 ms at 130 BPM, factor 1.0569; two clips of the same channel 2 beats apart differ by 2 beats × the same factor).
The importer only accepted offsets in beats or in timeline milliseconds and dropped them (a 788 ms start lost).
Now: offset = start / factor, factor = span / clip length in ms (accepted between 0.25 and 4).

## 3. Result on the user's project

Level per 10 s window (dB RMS), FL render on her laptop vs LYNX renderwav:

    FL (her laptop)     : -25.4 -23.6 -20.4 -12.9 -12.4 -12.9 -25.4 -25.1 -19.5 -12.6 -12.7 -12.4  overall -15.25
    FL (this machine)   : -25.4 -23.6 -20.4 -12.8 -12.3 -12.8 -25.4 -25.1 -19.5 -12.5 -12.6 -12.4  overall -15.22
    LYNX gf92 (722a512) : -20.0 -20.0 -18.4 -13.4 -12.9 -13.3 -20.1 -19.6 -18.1 -13.0 -13.3 -13.4  overall -15.28
    LYNX this round     : -25.3 -23.7 -20.4 -13.0 -12.5 -12.9 -25.4 -25.0 -19.6 -12.7 -12.8 -12.9  overall -15.39

Every window is now within 0.5 dB of FL (the intro was 5 dB too loud). Test:
`flp_channel_level_adjustments_and_stretched_clip_start`.

## Where I stopped / next steps

- Done and verified. Left from gf92: FLEX approximated, Newtone placeholder, third-party plugins (Valhalla Vintage
  Verb, Sausage Fattener, Kickstart 2) not available here; the last 10 s window is 0.5 dB quieter (not investigated).
- FL's command-line render stops at the "Problems loading the project" dialog when plugins/samples are missing;
  `tmp/gf/scripts/okloop.sh` + `okdlg.ps1` close it automatically during renders.
