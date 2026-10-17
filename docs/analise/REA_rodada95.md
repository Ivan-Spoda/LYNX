# Round 95 (pitch95) — LYNX Tune (own vocal pitch corrector) and the Newtone engine

> Two parts with different rules. **§1–3 (Newtone)** is FL-compatibility work: behavioural analysis of FL Studio's
> Newtone, clean-room as in every REA round (layouts and behaviour in our own words; no Image-Line or zplane code,
> pseudocode or data in the repository; markers [DECOMP], [DATA], [EXEC], [model], "not confirmed"). **§4–6 (LYNX Tune)**
> is an original LYNX design written **only from published DSP literature**: no third-party pitch-correction product
> (Antares, Waves, Celemony …) was opened, run, measured or reverse-engineered, not even as a black box.
> Scratch: `~/.claude/jobs/f8789b49/tmp/r95/`. Date: 2026-10-17.

## 1. Newtone in the projects that use it [DATA]

Newtone sits on the master of four of the girlfriend's vocal projects (tmp/gf: *Liwn Passar voz*, *ARTE MORTA*, *MINA
INSEGURA*, *PLAY WITH ME*). Decoding their states with the round-73 layout:

| Project | Option flags | Slave to host playback | Notes | Edits (Cur ≠ Orig) |
|---|---|---|---|---|
| Liwn Passar voz | 1 | off | 2 | none |
| ARTE MORTA | 2 | off | 13 | note 6 −1 st |
| MINA INSEGURA | 1 | off | 13 | note 10 +1 st |
| PLAY WITH ME | 2 | off | 13 | note 11 −1 st; two notes stretched by 1 sample |

None has the slave switch (flag 0x4) on, so in FL each of these Newtone inserts **passes its input through unchanged**
(REA_rodada73 §3 [EXEC]): the singer rendered the corrected take to audio and Newtone only keeps the edit session.
LYNX already reproduces this exactly; nothing in these projects depends on Newtone's pitch engine.

## Where I stopped / next steps
(kept up to date while the round runs)
- LYNX Tune: engine, routing of held notes, mixer keyboard strip, tests, docs — done (§4–6).
- Newtone engine: REA of the edit semantics running (sub-task; findings in tmp/r95/nt/FINDINGS.md), then the port.
