# Estado atual

Atualizado em 2026-10-12.

## Funcionando e verificado
- `Daw.exe` compila de forma cruzada do WSL com Zig 0.14.1 + JUCE 8.0.4, abre no Windows 11
  e mostra o Channel Rack (verificado por captura de tela da janela).
- Testes do núcleo: 75 mil verificações passando, incluindo leitura dos 165 `.flp` instalados
  com o FL Studio 2026 (versões 10.9 a 25.2).
- `renderwav` gera áudio de patterns importadas de `.flp`.

## Ainda não verificado
- Reprodução de áudio no app com o mouse e teclado de um usuário real (a janela abre e o
  dispositivo de áudio inicia: o medidor de CPU sai de 0).
- Build com Visual Studio/CMake (não há MSVC nesta máquina).

## Descobertas sobre o formato .flp (além de docs/ANALISE_FL_STUDIO.md)
Confirmadas empiricamente nos demos:
- Registro de nota: 24 bytes em todas as versões, inclusive 25.x.
- Registro da playlist: 32 bytes (< 20.99.1), 60 bytes (20.99.1 até 24.1), 80 bytes (≥ 24.2.99).
- Playlist: 199 trilhas antes do FL 12.9; 500 a partir dele (índice de trilha invertido).
- Item da playlist: índice > base (20480) = pattern (índice − base); caso contrário, clip de canal.
- Trilha do mixer de um canal: evento 0x68 (u16). O 0x16 (u8) só em arquivos antigos.
- Nome do insert do mixer (0xCC) vem antes do 0xEC que abre aquele insert.
- Nome de canal: 0xCB; arquivos ≤ 11 usam 0xC0. Nome interno do plugin: 0xC9.
- A string de versão (0xC7) é sempre ASCII; os outros textos são UTF-16LE a partir da 11.5.4.

## Próximo passo
Fase 2 do roadmap: sampler (para os projetos importados soarem com os samples reais) e Piano Roll.
