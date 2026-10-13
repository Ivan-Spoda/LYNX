# Componentes de terceiros

O LYNX é distribuído sob a [GNU AGPL-3.0](LICENSE). Ele usa os componentes abaixo, cada um com a sua
própria licença:

| Componente | Uso | Licença |
|---|---|---|
| [JUCE 8](https://juce.com) | interface, áudio, hosting de plugins VST3 | AGPLv3 (ou licença comercial JUCE) |
| [CLAP](https://github.com/free-audio/clap) (headers em `third_party/clap`) | hosting de plugins CLAP | MIT |
| [WavPack](https://www.wavpack.com) (decodificador em `third_party/wavpack`) | leitura de samples `.wv` | BSD de 3 cláusulas (`third_party/wavpack/COPYING`) |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (incluído no JUCE) | hosting de plugins VST3 | conforme a licença distribuída com o JUCE |

VST é marca registrada da Steinberg Media Technologies GmbH. FL Studio é marca registrada da Image-Line
Software; o LYNX não tem relação com a Image-Line.
