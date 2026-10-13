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
  LFOs do canal. Filtro do canal com os tipos passa-baixas/banda/altas/rejeita-faixa.
- **Filtro do canal no sampler** (corte/ressonância), importado do Mod X/Y do FL; envelopes de corte e
  de afinação também no sampler.
- **VSTs de terceiros do FL (REA rodada 5, `docs/analise/REA_rodada5.md`)**: o blob do "Fruity Wrapper"
  (canais e slots do mixer) é lido; ao importar, o LYNX procura o mesmo plugin entre os escaneados
  (VST3 pelo class ID, CLAP pelo id, ou pelo nome, que também cobre VST2 → VST3/CLAP do mesmo produto)
  e restaura o estado VST3. Os não achados aparecem num aviso; os canais ficam no synth interno.
  Conferido com `LYNX.exe --wrappertest "Surge XT.vst3" relatorio.txt`. VST2 o LYNX não hospeda.
- **Efeitos nativos do FL com os ajustes reais (REA rodada 7, `docs/analise/REA_rodada7.md`)**:
  Parametric EQ 2 → EQ paramétrico de 7 bandas (tipos, frequências, ganhos, larguras, inclinações
  "Steep" e bandas mutadas; 1124 slots nos demos), Limiter (limitador ou o compressor dele),
  Reeverb 2 (decaimento, abafamento, separação, seco/molhado), Delay 2/3 (tempo sincronizado,
  feedback, ping-pong, corte), Compressor (threshold, ratio, ganho, attack, release).
- **EQ embutido das trilhas do mixer** (3 bandas do FL, REA rodada 6) e **LFOs do canal**; porta e AM
  do 3x Osc; tipos de marcador conhecidos.
- **Geradores de sample do FL (REA rodadas 8–9)**: FPC vira um canal por pad (notas pela tecla,
  camadas de velocity, grupos de corte), DirectWave e FL Keys viram sampler multi-sample, Fruity
  Slicer/Slicex viram sampler fatiado (WAV embutido no .flp é extraído). Maximus, Soundgoodizer,
  Fast Dist, Fruity Filter, Flangus e Soft Clipper entram com os ajustes salvos.
- **Sintetizadores simples do FL (REA rodada 10)**: Harmless (160 canais nos demos), MiniSynth, DX10
  e Fruit Kick aproximados pelo synth interno (osciladores/uníssono, envelopes, filtro, LFO, porta).
- **FM no synth interno**: o osc 3 (calado) modula a fase dos osc 1 e 2, com índice que cai até um
  sustain (knobs "FM" e "Decaim. FM"). Usado no import do DX10, do GMS (Mod Kind FM) e do Sytrus
  (uma portadora com FM forte).
- **Rodada 13**: Kepler aproximado pelo synth (serra/pulso/sub/ruído, VCF com Env1, LFO); Wave Traveller
  toca o sample dele; FLEX escolhe o patch do synth pelo nome do preset/pacote. Os pacotes do FLEX são
  protegidos (cifrados) e o LYNX **não** os abre: as amostras são conteúdo licenciado para uso no FLEX.
- **Efeitos da rodada 14**: Gross Beat vira o novo "Gate de volume" (32 passos por compasso, mix,
  ataque/release; o slot de volume escolhido é amostrado), Frequency Splitter vira prateleiras no EQ
  paramétrico, Effector vira distorção/filtro/chorus/delay/reverb/largura conforme o algoritmo.
  Automações do FL seguem o slot certo e o mix (wet) do slot é automatizável.
- **Links de controlador**: o Peak Controller do FL vira um efeito que mede o pico da trilha, e o link dele
  vira uma automação "ao vivo" do alvo (lida a cada bloco): o *sidechain pumping* dos demos funciona.
  O Envelope Controller vira um canal "controlador" (sem som): cada nota dispara os envelopes dos 8
  articuladores e os links dele viram automações ao vivo com fonte no canal (ex.: *Basic Sidechain* do
  Future House cai na nota e volta em ~1/3 s). X-Y/Formula Controller e os alvos internos do Love Philter
  (Mod X/Y) ainda ficam de fora; o canal controlador ainda não tem editor próprio.
- **Patcher**: o grafo do estado é lido; uma cadeia simples em série vira slots seguidos no mixer e um
  Patcher usado como instrumento toca o gerador principal de dentro (ex.: Harmor dentro do Patcher).
  Patchers com ramos paralelos simples viram o contêiner "Patcher (paralelo)" (cada ramo uma cadeia de
  efeitos internos, somados com o ganho dos cabos). Os de M/S e multibanda (saídas extras) ainda ficam de fora.
- **Efeitos novos**: pitch shift e frequency shifter. Da rodada 15: Pitch Shifter e Frequency Shifter do FL
  viram esses efeitos; Emphasis vira limiter (ganho de entrada + teto); Low Lifter vira EQ paramétrico
  (corta-graves + prateleira); Send/Transporter viram ganho; Squeeze vira distorção leve; Vintage Phaser,
  chorus.
- **Sytrus (REA rodada 11)**: estado zlib (inflate próprio no núcleo) com a matriz FM/RM, 10 unidades e
  articuladores; as 3 portadoras mais fortes viram os osciladores, FM forte vira onda mais rica +
  envelope de filtro, RM vira AM, o primeiro filtro ativo (com o envelope de cutoff) vira o filtro.
  268/268 canais dos demos e 984/984 presets decodificam.
- **GMS, Ogun/Autogun, Sakura, DrumSynth Live (REA rodada 12)**: GMS completo (ondas pelo nome,
  mistura em cascata, AM/sync, uníssono, ladder, 2 envelopes, 2 LFOs, mono/porta; 306/306 presets);
  Ogun/Autogun e Sakura aproximações grosseiras; DrumSynth Live toca o sample 808/909 do FL de mesmo nome.
- **Presets do FL**: os 169 presets de canal do 3x Osc (botão "Presets do FL..." no synth) e 93
  presets de efeito (Parametric EQ 2, Limiter, Compressor, Reeverb 2, Delay 2/3, Stereo Shaper; menu
  do slot do mixer) são lidos dos `.fst` da instalação.
- **Sampler multi-sample** (zonas por tecla/velocity) e divisão de kits de bateria em um canal por
  pad (base para DirectWave/FPC); notas porta (glide de 808); Picker de patterns na Playlist.
- **Render mais leve**: PDC em uma passada, reverb por blocos, limiter O(1), FTZ/DAZ; o export do
  SHINE (110 s, 60 efeitos importados) caiu de 4,4 s para 2,4 s.
- **MIDI learn**: clique direito em knobs/faders/parâmetros de plugin → "Ligar a controlador MIDI".
- **Compasso** da música (menu da pattern), importado do `.flp`.

## Confirmado x aproximado (import do FL)
Tudo o que o import faz com plugins do FL vem dos documentos `docs/analise/REA_rodada*.md`. As rodadas 17, 18
e 19 verificaram as hipóteses (decompilação + manual oficial) e o código foi corrigido. Aplicado:
- Effector: efeito próprio (`core/Effector.cpp`) com os 12 algoritmos e os 12 parâmetros crus do FL — LFO preso
  à música, Mod Rate/Tempo Reduce, entrada/saída; Distort (cascata exata), Lo-fi, Delay (tempo do Mod Rate),
  Reverb (seco preservado), Stereo, Trancer, Stutter, Vowel e Ring com as leis lidas no código.
- Harmor: corte em log2 Hz (73,4 Hz…17,7 kHz) com keytrack a partir de 130,81 Hz, tipos 0…18, volume
  `(3^x − 1)/2` (até +12 dB), nível², envelope do filtro centrado em 0,5 com ±2·(hi − lo)·amt.
- Sytrus: FM 4·g ciclos; RM em anel `1 − |a| + a·mod`. Toxic: FM 2·célula (sentido da matriz lido no código),
  filtro p300 (LP/BP/HP), corte `2^(9u + 5)`, key-track 0,09·KT oct/st, envelope ±9·EGAmt oct.
- Harmless (rodada 21): faixa do corte com as margens por tipo, keytracking, envelope de filtro centrado (não
  adaptativo), LFO de corte unipolar, LFO preso à música (p87), máscara padrão = serra·(1 − m) + quadrada·m/2.
  Ogun: corte 44100·(c + c₀) Hz, Q/largura do FL, release 20 s, uníssono ±5(11^(P15/256) − 1) ct. MiniSynth:
  períodos do LFO, quantidade ao quadrado, release, 2ª voz em semitons inteiros + 7,5 ct.
- Kepler: LFO1 (taxa, vibrato e corte em picos ±, atraso). BassDrum: Peak `1 + 100x³·s⁴`. Sakura: ADSR, Tune,
  Mix, Hi-Cut. BooBass: EQ de 3 bandas. Drumpad: Pitch e Mallet Amplitude.
- Love Philter: base do articulador, Mod X/Y → volume (com automação/links), tipos 4/5 (largura em oitavas),
  7 (passa-tudo) e 11 (Mango); envelopes de pattern no volume (efeito "Envelope rítmico", em tempos, em loop,
  travado na música, curva de tensão) e LFO de pan (efeito "Auto-pan", triângulo travado na música).
- Patcher: qualquer grafo dos demos reduzido a série/paralelo (contêineres aninhados), cabos com a classe certa.

O que **ainda não tem confirmação** e por isso é uma aproximação declarada (ou ficou de fora):
- Harmor: ressonância (curva por harmônica sem equivalente); com o envelope de volume desligado o FL mantém a
  nota até roubo/redisparo — o LYNX usa um release curto de declick; imagem/blur/prism/FX.
- Effector: Filter (cascata do FL não tabelada: 4 polos com realimentação), Flanger/Phaser (topologia exata
  da realimentação), Reverb (decaimento/abafamento pelo reverb do LYNX).
- Sakura: cordas físicas viram serra + triângulo; decaimento da corda na tecla 60; Hi-Cut na saída, não no
  excitador; taxa do motor (48 kHz) não confirmada para o render.
- BooBass: fonte é uma amostra interna que não está no projeto (o LYNX usa uma serra). Drumpad: o modelo
  físico não é reproduzido.
- Toxic: o filtro do FL tem 4 polos (o do LYNX, 2); ressonância sem escala; envelope do filtro linear no LYNX.
- Kepler: chorus (o synth do LYNX não tem chorus); "Range" ×10 só no Exo; BPM Sync do LFO.
- Love Philter: Mango em Hz é inferido do polo; envelope/LFO de corte só com uma unidade (automação ao vivo), ressonância não aplicada; curva de
  envelope tipo 5 tratada como simples; lei do pan e tensão do LFO de pan; Mod X com várias unidades só no
  valor fixo.
- Fruity Vocoder: implementado com o algoritmo do FL (REA_rodada20 §4.1); Vocodex fica de fora (precisa de sidechain
  ou do synth interno).
- Harmless: cores do timbre e máscaras irregulares por formas básicas; "detune" p39 (espaçamento das parciais)
  não reproduzido; tremolo estéreo de 2,5 Hz (p25) e vibrato de 6 Hz (p41) fora; ressonância (largura do pico).
  Ogun: timbre e articuladores; o filtro em cascata vira 1 estágio. MiniSynth: velocidade → corte; LFO serra.
- Pitcher: sem equivalente estático (a correção depende do detector de altura): passa reto.
- Squeeze, Vintage Phaser: aproximações.
- FLEX: os pacotes são protegidos; o LYNX não os abre (patch do synth pelo nome do preset).

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
- Plugins em processo separado (opcional, `Opções > Plugins em processo separado`, padrão desligado):
  cada plugin roda num `LYNX.exe --lynx-plugin-host` filho. Áudio (até 8 canais × 4096), MIDI (até 1024
  eventos) e mudanças de parâmetro (até 512 por bloco, nos dois sentidos) passam por memória
  compartilhada com um par de eventos por bloco; estado e janela por mensagens (ChildProcessCoordinator).
  Se o filho não responder em 0,5 s ou fechar, o plugin fica mudo, um aviso aparece e o LYNX continua.
  A janela do plugin é do filho (sempre no topo). Verificado com `LYNX.exe --sandboxtest plugin.vst3
  relatorio.txt` (Surge XT: toca, 1 s em ~13 ms, parâmetro → estado → plugin no processo igual, filho
  travado → 0,5 s, mudo, marcado como morto). `LYNX_SANDBOX_LOG=arquivo` registra os passos.
- O VST3 SDK que vem com o JUCE compila com Zig/MinGW; basta pôr
  `juce_audio_processors/format_types/VST3_SDK` no include path.
- Plugins são preparados numa taxa fixa; quando o dispositivo muda de taxa, o engine recria as
  instâncias. A latência dos plugins entra no PDC.

## Próximo passo
Regressão: os 20 "Demo songs" do FL 2026 exportam sem erro (`LYNX.exe --export`), ~25x mais rápido
que o tempo real. Harmor (100 canais) entra como aproximação grosseira (partes A/B como serras, filtro,
envelopes, uníssono). Sem equivalente ainda (tocam com preset do synth pelo nome): FLEX (57; pelo nome do preset) (Morphine, PoiZone, BassDrum, Toxic, BooBass e Drumpad já entram aproximados). Efeitos sem equivalente mais comuns: Pitcher,
Pitcher (correção de afinação), Patcher em paralelo, Vocodex. Também em aberto: ASIO (licença do SDK
da Steinberg: GPLv3 ou proprietária).
