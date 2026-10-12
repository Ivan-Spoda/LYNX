# Daw

DAW leve em C++20 + JUCE 8, com o fluxo de trabalho do FL Studio.
Veja `docs/ARQUITETURA.md`, `docs/ROADMAP.md` e `docs/ESTADO.md`.

## O que já funciona

- Channel Rack com step sequencer (clique liga, arrastar pinta, clique direito apaga)
- Várias patterns, BPM, metrônomo, posição em compasso:tempo:step
- Instrumentos internos sintetizados: kick, snare, hat, clap e synth polifônico
- Notas disparadas no sample exato; a UI nunca trava a thread de áudio
- Abrir projetos `.flp` do FL Studio (canais, patterns e notas; 165/165 demos do FL 2026 leem sem erro)

## Compilar

### Windows a partir do WSL/Linux, sem Visual Studio (Zig)

```
./scripts/build-windows-zig.sh      # gera build-win/Daw.exe
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

Também gera `build-local/flpdump` (mostra o conteúdo de um `.flp`) e `build-local/renderwav`
(renderiza uma pattern de um `.flp` para `.wav` com os instrumentos internos).
