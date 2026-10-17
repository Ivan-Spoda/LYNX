# lynx-share.com

The project website: a single static page (`index.html`) plus `assets/`.
The download buttons read the newest GitHub release at runtime, so a new release needs no redeploy.

- Screenshots come from LYNX's own showcase project (`LYNX.exe --showcase Demo.lynx`, then
  `LYNX.exe --snapshot <prefix> Demo.lynx` with `LYNX_SNAPSHOT_SIZE=1920x1080`), never from Image-Line demo projects.
- `assets/media/demo.mp3` is `LYNX.exe --export Demo.lynx demo.wav`; `lynx-tour.mp4` is a slideshow of the
  screenshots over that audio (ffmpeg zoompan + xfade); `peaks.json` is its waveform (600 peaks).
- `?shot` in the URL disables the entrance animations (for headless screenshots).
- Publish with `scripts/deploy-site.sh` (Hostinger API token in `~/.config/hostinger/token`, never committed).
- The plugin section is data-driven: `FEATURED` (highlight cards, Exact/Near-exact only, `by` = the original's maker,
  so engines inspired by third-party plugins can be added) and `PLUGINS` (full grid), kept in sync with `docs/PLUGINS.md`.
- The "Inspired by the classics" section (`#classics`) is generated: `scripts/gen-inspired.py` reads the substitute
  table in `src/core/PluginSubstitutes.cpp` plus `docs/inspired-by.json` (descriptions EN/pt-BR, short names, work in
  progress) and rewrites the `INSPIRED` data block here and the matching sections of `README.md` and `docs/PLUGINS.md`.
  Rerun it whenever an engine lands (release step); `--check` fails when a block is stale. A new LYNX engine without an
  entry in `inspired-by.json` is printed as a warning: add its description there.
