# Estado atual

Atualizado em 2026-10-12.

## Funcionando e verificado
- Áudio no app: o grafo estava sem conexões (os nós eram conectados antes de o grafo ter
  canais de saída). Corrigido e coberto pelo `Daw.exe --selftest`.
- `Daw.exe --render projeto.flp saida.wav 20` exporta um demo do FL com 17/17 samples reais.
- `Daw.exe --snapshot prefixo projeto.flp` desenha a UI fora da tela em PNG: é assim que
  confiro a interface sem mexer na tela do usuário.
- `Daw.exe` compila de forma cruzada do WSL com Zig 0.14.1 + JUCE 8.0.4, abre no Windows 11
  e mostra o Channel Rack (verificado por captura de tela da janela).
- Testes do núcleo: 75 mil verificações passando, incluindo leitura dos 165 `.flp` instalados
  com o FL Studio 2026 (versões 10.9 a 25.2).
- `renderwav` gera áudio de patterns importadas de `.flp`.

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

## Descobertas sobre os samples do FL
- Muitos samples de fábrica são WAV com format tag 0x674F/0x6750 e um stream Ogg Vorbis no
  chunk `data`; outros são arquivos Ogg puros com extensão `.wav`. `SampleLibrary` trata os dois.
- Caminhos usam `%FLStudioFactoryData%` (= pasta de instalação), `%FLStudioData%`
  (= instalação\Data) e `%FLStudioUserData%` (= Documentos\Image-Line\FL Studio).

## Ainda não verificado por um humano
- Ouvir o app tocando (o autoteste prova que o grafo gera áudio; falta confirmar na placa do usuário).
- Interações de mouse/teclado (arrastar notas, knobs, teclado do PC): conferidas só por leitura do código.

## Próximo passo
Fase 3: Playlist (arranjo) com importação da playlist do `.flp`, depois mixer.
