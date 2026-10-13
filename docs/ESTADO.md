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
- **CLAP**: formato próprio sobre o JUCE; Surge XT e Surge XT Effects (CLAP) tocam, salvam estado e
  passam áudio (`--plugintest`). A busca de plugins roda em processo separado (`--scan-one`).
- **Streaming do disco**: áudios acima de 30 s vão para um cache em `%TEMP%\LYNX-cache` mapeado
  em memória, com prefetch à frente da reprodução (testado com um WAV de 60 s).
- **Mixer**: dry/wet por slot (knob na linha do slot), smart disable de plugins (4 s de silêncio).
- **Swing** (knob na barra), **ajustes do sampler** (menu do canal), **trilhas da Playlist** com nome e
  mute (LED; nomes importados do `.flp`), **dividir clip**, **MIDI** (abrir `.mid`; botão MIDI no
  Piano Roll), **dither** no WAV 16 bits.
- **Gravação de áudio** da entrada (menu "v" ao lado do Rec): o take vira clip na Playlist.
- **Automação de tempo** (clique direito no BPM), importada do `.flp` (`0x40000005`, 60..180 BPM);
  o export integra o tempo (testado: compasso a 120 e outro a 240 BPM).
- **Notas**: pan, afinação fina, mute (Ctrl+M) e **slide** (glide de 808) por nota, importados do
  `.flp`; a faixa do Piano Roll edita velocity/pan/afinação; acordes com um clique e destaque de escala.
- **Renderizar insert para áudio** (menu do insert) e `--export --insert N`.
- **Efeitos novos**: limiter (lookahead, com PDC), filtro LP/HP/BP, distorção, chorus; projeto novo já
  vem com limiter no Master.
- **Mixer**: separação estéreo, inverter fase, trocar L/R; cores dos inserts e dos canais (do `.flp`).
- **Importação do `.flp`**: volume, pan e afinação dos canais (`0xDB`), cores, nomes/mute das trilhas.
- **Navegador** (F8): pastas do FL e do usuário, prévia ao clicar, duplo clique cria sampler, arrastar.
- **Segurança**: "*" no título, "salvar alterações?", autosave a cada 2 min e salvamento de emergência
  se travar (`%APPDATA%\LYNX\autosave`).
- **Interface mais leve**: só o cursor e os medidores que mudaram são redesenhados.
- **Fiel ao FL (REA rodada 3, `docs/analise/REA_rodada3.md`)**: curva de volume do canal (78% = -5,2 dB),
  swing com a fórmula do FL (+ mix por canal), ADSR do sampler importado do envelope de volume (`0xDA`),
  time-stretch (reamostrando ou mantendo a altura com WSOLA), nota-raiz, cut groups, canal mono,
  trecho/start offset do sample. Os 30 primeiros demos exportam sem erro.
- **Synth interno de 3 osciladores** com ADSR, filtro ressonante e presets (808, Reese, pad...);
  cada oscilador tem oitava, semitom, afinação fina, pan e nível.
- **3x Osc do FL importado (REA rodada 4, `docs/analise/REA_rodada4.md`)**: o estado do plugin (`0xD5`,
  versões 10-14) vira os 3 osciladores (forma, coarse/fine, pan, níveis com a regra de mix do FL);
  envelope de volume do canal vira o ADSR e o filtro do canal (Mod X/Y + tipo, `0xDB`) vira corte/
  ressonância; envelopes de corte e de afinação do canal (registros 2 e 4 do `0xDA`, segmentos retos
  como a tensão 0 do FL) viram os envelopes de modulação do synth (kicks e plucks feitos no 3x Osc).
  Os 152 canais 3x Osc dos demos decodificam. Ainda não: AM do osc 3, inverter fase, SPO/phase rand,
  filtros BP/HP/BS, LFOs do canal.
- **Filtro do canal no sampler** (corte/ressonância), importado do Mod X/Y do FL; envelopes de corte e
  de afinação também no sampler.
- **VSTs de terceiros do FL (REA rodada 5, `docs/analise/REA_rodada5.md`)**: o blob do "Fruity Wrapper"
  (canais e slots do mixer) é lido; ao importar, o LYNX procura o mesmo plugin entre os escaneados
  (VST3 pelo class ID, CLAP pelo id, ou pelo nome, que também cobre VST2 → VST3/CLAP do mesmo produto)
  e restaura o estado VST3. Os não achados aparecem num aviso; os canais ficam no synth interno.
  Conferido com `LYNX.exe --wrappertest "Surge XT.vst3" relatorio.txt`. VST2 o LYNX não hospeda.
- **Render mais leve**: PDC em uma passada, reverb por blocos, limiter O(1), FTZ/DAZ; o export do
  SHINE (110 s, 60 efeitos importados) caiu de 4,4 s para 2,4 s.
- **MIDI learn**: clique direito em knobs/faders/parâmetros de plugin → "Ligar a controlador MIDI".
- **Compasso** da música (menu da pattern), importado do `.flp`.

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
- Gravação de áudio da entrada (depende de microfone/placa; o código foi revisado, não testado).
- Interface do plugin CLAP na janela (o hosting de áudio foi testado; a janela não, para não abrir
  nada na tela do usuário).
- Ouvir o app tocando (o autoteste prova que o grafo gera áudio; falta confirmar na placa do usuário).
- Interações de mouse/teclado (arrastar notas, knobs, teclado do PC): conferidas só por leitura do código.

## Hosting de plugins
- O VST3 SDK que vem com o JUCE compila com Zig/MinGW; basta pôr
  `juce_audio_processors/format_types/VST3_SDK` no include path.
- Plugins são preparados numa taxa fixa; quando o dispositivo muda de taxa, o engine recria as
  instâncias. A latência dos plugins entra no PDC.

## Próximo passo
Arpejador do canal (layout em hipótese no `0xD7`), envelopes de pitch/corte do sampler, porta, ASIO
(depende de decidir a licença do SDK da Steinberg: GPLv3 ou proprietária).
