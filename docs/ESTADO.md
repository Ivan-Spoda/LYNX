# Estado atual

Atualizado em 2026-10-13.

## Novidades de 2026-10-13 (verificadas por testes, autoteste e capturas fora da tela)
- **Exportar áudio pela interface** (botão Exportar / Ctrl+R): WAV 16/24/32f, FLAC, OGG; música inteira ou
  pattern; cauda até o som acabar; 16 bits com dither TPDF + noise shaping. `LYNX.exe --export proj saida.wav`
  usa o mesmo caminho.
- **Piano Roll**: seleção (Ctrl+arrastar, Shift+clique, Ctrl+A), mover/redimensionar em grupo, Ctrl+C/X/V,
  Ctrl+B duplicar, Ctrl+Q quantizar, setas (Shift = oitava/compasso), Delete, seletor de snap, faixa de velocity.
- **Patterns**: menu "..." na barra (renomear, cor, comprimento, clonar, remover); cores importadas do `.flp`.
- **Solo de canal**: clique direito (ou Ctrl+clique) no LED de mute.
- **Gravação**: botão Rec grava notas do teclado do PC e de todas as entradas MIDI na pattern atual (um passo
  de desfazer por tomada). MIDI toca o canal selecionado.
- **Playlist**: região de loop (arrastar na régua; clique direito apaga), seleção múltipla, copiar/colar/duplicar,
  "Tornar única", **vários arrangements** (combo no canto da Playlist).
- **Automação**: clique direito em qualquer knob/fader (canal, mixer, efeitos) ou "Automatizar parâmetro do
  plugin" → clip na Playlist + editor de pontos com curva (Alt+arrastar). Importada do `.flp` (1105 clips nos demos).
- **Mixer**: envios entre inserts com nível (botão de seta na base da faixa), "só para esta trilha", solo pelo
  caminho do sinal, curva do fader do FL, rampas de ganho. Roteamento/volumes/mute/solo importados do `.flp`.
- **PDC** por aresta (modelo do FL, ver `docs/analise/REA_rodada2.md`); o export descarta a latência inicial.
- **Multi-thread**: canais em paralelo e mixer por camadas; export ~2x mais rápido nos demos.

## Funcionando e verificado
- Áudio no app: o grafo estava sem conexões (os nós eram conectados antes de o grafo ter
  canais de saída). Corrigido e coberto pelo `LYNX.exe --selftest`.
- `LYNX.exe --render projeto.flp saida.wav 20` exporta um demo do FL com 17/17 samples reais.
- `LYNX.exe --snapshot prefixo projeto.flp` desenha a UI fora da tela em PNG: é assim que
  confiro a interface sem mexer na tela do usuário.
- `LYNX.exe` compila de forma cruzada do WSL com Zig 0.14.1 + JUCE 8.0.4, abre no Windows 11
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

## Descobertas sobre a playlist do .flp
- Nos clips de pattern, os 8 bytes em +24 são dois int32 em ticks (início e fim do trecho da
  pattern), não floats; -1 (que aparece como NaN se lido como float) = sem recorte. Nos clips de
  canal (áudio/automação) os mesmos bytes são floats.
- 51 dos 165 `.flp` instalados têm playlist com clips de pattern.
- Clips de áudio (clips de canal): os floats em +24/+28 são início/fim do trecho, ora em tempos,
  ora em milissegundos (no LeriQ - SHINE, ms). O FL não estica esses clips: a duração do sample
  bate com a do clip no tempo do projeto.

## Descobertas sobre os samples do FL
- Muitos samples de fábrica são WAV com format tag 0x674F/0x6750 e um stream Ogg Vorbis no
  chunk `data`; outros são arquivos Ogg puros com extensão `.wav`. `SampleLibrary` trata os dois.
- Caminhos usam `%FLStudioFactoryData%` (= pasta de instalação), `%FLStudioData%`
  (= instalação\Data) e `%FLStudioUserData%` (= Documentos\Image-Line\FL Studio).

## Ainda não verificado por um humano
- Ouvir o app tocando (o autoteste prova que o grafo gera áudio; falta confirmar na placa do usuário).
- Interações de mouse/teclado (arrastar notas, knobs, teclado do PC): conferidas só por leitura do código.

## Hosting de plugins
- O VST3 SDK que vem com o JUCE compila com Zig/MinGW; basta pôr
  `juce_audio_processors/format_types/VST3_SDK` no include path.
- Plugins são preparados numa taxa fixa; quando o dispositivo muda de taxa, o engine recria as
  instâncias. Latência de plugin (PDC) ainda não é compensada.

## Próximo passo
Streaming do disco para áudios longos, freeze/bounce, busca de plugins em processo separado, dry/wet por
slot e smart disable, time-stretch de clips de áudio.
