# LYNX

DAW leve em C++20 + JUCE 8, com o fluxo de trabalho do FL Studio e foco em rodar bem em
qualquer PC, independente do tamanho do projeto.

> Projeto em desenvolvimento. Veja o que já existe em [`docs/ESTADO.md`](docs/ESTADO.md) e o
> plano em [`docs/ROADMAP.md`](docs/ROADMAP.md).

## O que já funciona

- **Channel Rack** com step sequencer, pan/volume/mute/solo por canal, swing, várias patterns
  (renomear, cor, comprimento, clonar), ajustes do sampler (afinação, envelope, ao contrário, cut,
  filtro do canal com envelopes de corte e afinação), synth interno de 3 osciladores (pan e
  semitom por oscilador, filtro LP/BP/HP/BS, envelopes de corte e afinação, modo mono)
- **Piano Roll**: notas com snap configurável, seleção múltipla, mover/redimensionar em grupo,
  copiar/colar/duplicar, quantizar, faixa de velocity, importar/exportar `.mid`
- **Playlist**: clips de pattern, áudio e automação; vários arrangements; região de loop;
  seleção múltipla, copiar/colar/duplicar, dividir, "tornar única"; trilhas com nome e mute;
  marcadores e mudanças de compasso na régua; normalizar clips de áudio
- **Automação**: qualquer knob/fader (canal, mixer, efeitos, parâmetros de plugins) vira um clip
  com editor de pontos e curvas
- **Mixer**: até 127 inserts, envios entre inserts com nível, solo pelo caminho do sinal, curva do
  fader do FL, 10 slots de efeito com dry/wet (EQ, compressor, delay, reverb, plugins)
- **Plugins VST3 e CLAP**: instrumentos e efeitos com a interface própria; busca em processo
  separado (um plugin que trave não derruba o LYNX); PDC; "smart disable"
- **Gravação**: notas (teclado do PC e MIDI) e áudio da entrada da placa
- **Exportar**: WAV 16/24/32f, FLAC, OGG, música ou pattern, com cauda e dither
- **Projetos**: formato próprio `.lynx` (JSON); importa `.flp` do FL Studio (canais, samples reais,
  patterns, arrangements, marcadores, mixer com roteamento, EQ das trilhas e efeitos nativos com os
  ajustes salvos, automação, 3x Osc, FPC, DirectWave, FL Keys, Slicex/Fruity Slicer, Harmless e outros
  synths aproximados, filtro/envelopes/LFOs do canal, VST3/CLAP de terceiros com estado quando estiverem
  instalados; um relatório mostra o que não tem equivalente) e `.mid`; presets `.fst` do 3x Osc e de
  efeitos do FL
- **Desempenho**: motor sem locks, canais e mixer em várias threads, áudios longos lidos do disco
  sob demanda
- **Desfazer/refazer** em todas as edições; **teclado do PC como piano** (layout do FL)

## Atalhos

| Tecla | Ação |
|---|---|
| Espaço | Play / Pause |
| F5 / F6 / F7 / F9 | Playlist / Channel Rack / Piano Roll / Mixer |
| L | Alterna Pattern / Song |
| Ctrl+Z / Ctrl+Y | Desfazer / refazer |
| Ctrl+N / Ctrl+O / Ctrl+S | Novo / Abrir / Salvar (Ctrl+Shift+S: salvar como) |
| Ctrl+R | Exportar áudio |
| `Z`..`M`, `Q`..`P` | Tocar o canal selecionado |
| Piano Roll / Playlist | Ctrl+A/C/X/V, Ctrl+B duplicar, Delete, Esc; no Piano Roll também Ctrl+Q e setas |

## Linha de comando

```
LYNX.exe projeto.lynx                          abre um projeto (.lynx, .flp ou .mid)
LYNX.exe --export projeto.flp saida.flac       exporta a música (wav/flac/ogg) como o botão Exportar
LYNX.exe --render projeto.flp saida.wav 20     exporta 20 s da pattern atual para WAV 24 bits
LYNX.exe --selftest relatorio.txt [demo.lynx]  autoteste sem placa de som
LYNX.exe --snapshot prefixo projeto.flp        desenha a interface (e as janelas) em PNG
LYNX.exe --plugintest plugin.vst3 rel.txt      testa um plugin VST3 ou CLAP
LYNX.exe --scantest pasta rel.txt              busca plugins numa pasta (processos separados)
LYNX.exe --wrappertest plugin.vst3 rel.txt     confere o import de um VST3 salvo pelo Fruity Wrapper do FL
LYNX.exe --dispositivos rel.txt                lista drivers e saídas de áudio (WASAPI, exclusivo, DirectSound)
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
src/ui/       interface: Channel Rack, Piano Roll, Playlist, Mixer, janelas
third_party/  headers do CLAP (MIT)
tests/        testes do núcleo
tools/        flpdump, renderwav
docs/         arquitetura, estado, roadmap e a análise comportamental do FL Studio
```

## Documentação
- `docs/ESTADO.md`: o que já funciona e o que falta; `docs/ROADMAP.md`: fases
- `docs/TESTE_MANUAL.md`: roteiro para ouvir/mexer no que os testes automáticos não cobrem
- `docs/analise/REA_rodada*.md`: o comportamento do FL Studio levantado com o REA (clean-room)

## Sobre o FL Studio

O LYNX é uma implementação independente. O FL Studio foi estudado para entender o
comportamento e o formato de arquivo (veja `docs/ANALISE_FL_STUDIO.md`); nenhum código da
Image-Line foi copiado.
