# Third-party components

LYNX is distributed under the [GNU AGPL-3.0](LICENSE). It uses the components below, each under its own license:

| Component | Used for | License |
|---|---|---|
| [JUCE 8](https://juce.com) | UI, audio, VST3 plugin hosting | AGPLv3 (or a commercial JUCE license) |
| [CLAP](https://github.com/free-audio/clap) (headers in `third_party/clap`) | CLAP plugin hosting | MIT |
| [WavPack](https://www.wavpack.com) (decoder in `third_party/wavpack`) | reading `.wv` samples | 3-clause BSD (`third_party/wavpack/COPYING`) |
| [libogg 1.3.5 and libvorbis 1.3.7](https://xiph.org/vorbis/) (decoder in `third_party/vorbis`; `src/mdct.c` modified, see the "LYNX modification" comment) | decoding Ogg Vorbis samples the way FL Studio does | 3-clause BSD (`third_party/vorbis/COPYING.libogg`, `third_party/vorbis/COPYING.libvorbis`) |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (the copy bundled with JUCE 8.0.4) | VST3 plugin hosting | GPLv3 option of Steinberg's dual licence (`VST3_SDK/LICENSE.txt` in JUCE) |
| [ASIO SDK 2.3.4](https://www.steinberg.net/developers/) (interface headers in `third_party/asiosdk`) | ASIO audio drivers on Windows | GPLv3 option of Steinberg's dual licence (`third_party/asiosdk/LICENSE.txt`) |
| Libraries bundled inside JUCE's modules | text shaping, image and audio codecs, zip | HarfBuzz (MIT), SheenBidi (Apache-2.0), FLAC (BSD-3-Clause), Ogg/Vorbis (BSD-3-Clause), libpng (libpng/zlib licence), IJG libjpeg (IJG licence), zlib (zlib licence) |

Every component is available under an OSI-approved open-source licence, and LYNX uses each of them under that
licence; no proprietary or closed-source code is compiled into or shipped with LYNX. Where Steinberg offers a
proprietary licence as an alternative (VST 3 SDK, ASIO SDK), LYNX takes the GPLv3 option, which is compatible with
the AGPL-3.0. The release binaries are built from this repository by
[`.github/workflows/release.yml`](.github/workflows/release.yml); the build downloads JUCE 8.0.4 and Zig 0.14.1
from their official sources and checks their SHA-256.

VST and ASIO are trademarks of Steinberg Media Technologies GmbH. FL Studio is a registered trademark of
Image-Line Software; LYNX is not affiliated with Image-Line.
