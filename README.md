# LYNX

DAW leve em C++20 + JUCE 8, com o fluxo de trabalho do FL Studio e foco em rodar bem em
qualquer PC, independente do tamanho do projeto.

> Projeto em desenvolvimento. Veja o que já existe em [`docs/ESTADO.md`](docs/ESTADO.md) e o
> plano em [`docs/ROADMAP.md`](docs/ROADMAP.md).

## O que já funciona

- **Channel Rack** com step sequencer, pan/volume/mute por canal, várias patterns
- **Piano Roll**: criar, mover, redimensionar e apagar notas, snap, zoom, preview e ghost notes
- **Playlist** com clips de pattern e de áudio, modo PAT/SONG
- **Mixer** com inserts, medidores, mute/solo e 10 slots de efeito (EQ, compressor, delay, reverb)
- **Plugins VST3**: instrumentos e efeitos, com a interface própria de cada plugin
- **Desfazer/refazer** em todas as edições
- **Instrumentos**: sampler (`.wav`, `.flac`, `.ogg`, `.mp3`, `.aiff`) e bateria/synth sintetizados
- **Teclado do PC como piano** (layout do FL: `Z`..`M` e `Q`..`P`)
- **Projetos**: formato próprio `.lynx` (JSON) e importação de `.flp` do FL Studio com os samples
  reais da instalação do FL
- **Exportar para WAV** pela linha de comando
- Motor de áudio sem locks: notas no sample exato, a interface nunca trava o áudio

## Atalhos

| Tecla | Ação |
|---|---|
| Espaço | Play / Pause |
| F5 / F6 / F7 / F9 | Playlist / Channel Rack / Piano Roll / Mixer |
| L | Alterna Pattern / Song |
| Ctrl+Z / Ctrl+Y | Desfazer / refazer |
| Ctrl+N / Ctrl+O / Ctrl+S | Novo / Abrir / Salvar (Ctrl+Shift+S: salvar como) |
| `Z`..`M`, `Q`..`P` | Tocar o canal selecionado |

## Linha de comando

```
LYNX.exe projeto.lynx                         abre um projeto (.lynx ou .flp)
LYNX.exe --render projeto.flp saida.wav 20    exporta 20 s para WAV 24 bits
LYNX.exe --selftest relatorio.txt             autoteste sem placa de som
LYNX.exe --snapshot prefixo projeto.flp       desenha a interface em PNG
LYNX.exe --plugintest plugin.vst3 rel.txt     testa um plugin VST3
```

## Compilar

### Windows a partir do WSL/Linux, sem Visual Studio (Zig)

```
./scripts/build-windows-zig.sh      # gera build-win/LYNX.exe
```

Precisa do Zig 0.14.1 em `~/opt/zig-x86_64-linux-0.14.1` e do JUCE 8.0.4 em `~/opt/juce-8.0.4`
(ou defina `ZIG` e `JUCE_DIR`). O JUCE 8 não suporta MinGW oficialmente; o script contorna as
diferenças (veja `cmake/mingw/` e `src/platform/MingwFixes.cpp`).

### Windows com Visual Studio (build oficial)

Pré-requisitos: Visual Studio 2022 Build Tools (workload "Desktop development with C++"), CMake ≥ 3.22 e Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Testes do núcleo (qualquer compilador C++20, sem JUCE)

```
./scripts/build-local.sh ["pasta com arquivos .flp"]
```

Também gera `build-local/flpdump` (mostra o conteúdo de um `.flp`) e `build-local/renderwav`.

## Organização

```
src/core/     modelo, sequenciador, instrumentos, leitor de .flp (C++ puro, testável sem JUCE)
src/flp/      leitor do formato .flp
src/engine/   motor de áudio (JUCE): dispositivo, grafo, samples, arquivo de projeto
src/ui/       interface: Channel Rack, Piano Roll, janela principal
tests/        testes do núcleo
tools/        flpdump, renderwav
docs/         arquitetura, estado, roadmap e a análise comportamental do FL Studio
```

## Sobre o FL Studio

O LYNX é uma implementação independente. O FL Studio foi estudado para entender o
comportamento e o formato de arquivo (veja `docs/ANALISE_FL_STUDIO.md`); nenhum código da
Image-Line foi copiado.
