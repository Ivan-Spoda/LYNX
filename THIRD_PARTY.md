# Third-party components

LYNX is distributed under the [GNU AGPL-3.0](LICENSE). It uses the components below, each under its own license:

| Component | Used for | License |
|---|---|---|
| [JUCE 8](https://juce.com) | UI, audio, VST3 plugin hosting | AGPLv3 (or a commercial JUCE license) |
| [CLAP](https://github.com/free-audio/clap) (headers in `third_party/clap`) | CLAP plugin hosting | MIT |
| [WavPack](https://www.wavpack.com) (decoder in `third_party/wavpack`) | reading `.wv` samples | 3-clause BSD (`third_party/wavpack/COPYING`) |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | VST3 plugin hosting | as licensed in the copy shipped with JUCE |
| [ASIO SDK 2.3.4](https://www.steinberg.net/developers/) (interface headers in `third_party/asiosdk`) | ASIO audio drivers on Windows | GPLv3 option of Steinberg's dual licence (`third_party/asiosdk/LICENSE.txt`) |

VST and ASIO are trademarks of Steinberg Media Technologies GmbH. FL Studio is a registered trademark of
Image-Line Software; LYNX is not affiliated with Image-Line.
