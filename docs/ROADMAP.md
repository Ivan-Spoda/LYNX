# Roadmap

Ordem pensada para ter algo usável o quanto antes e nunca quebrar a regra do motor de áudio
(sem alocação, locks ou I/O na thread de áudio).

## Fase 1: base tocável ✅
- [x] Transporte, motor de áudio, grafo JUCE
- [x] Modelo de música (canais, patterns, notas, PPQ 960)
- [x] Scheduler com precisão de sample
- [x] Instrumentos internos sintetizados
- [x] Channel Rack com step sequencer
- [x] Leitor de `.flp` + importação de canais/patterns/notas
- [x] Build Windows sem Visual Studio (Zig) e testes do núcleo

## Fase 2: compor de verdade
- [x] Sampler: `.wav`/`.flac`/`.ogg`/`.mp3`/`.aiff` por canal, inclusive os WAV-com-Ogg do FL
- [x] Projetos `.flp` importados tocam com os samples reais da instalação do FL
- [x] Piano Roll (criar, mover, redimensionar, apagar, snap, zoom, preview, ghost notes)
- [x] Salvar/abrir projeto no formato próprio (`.lynx`, JSON versionado)
- [x] Volume, pan e mute por canal; teclado do PC como teclado musical
- [x] Exportar para WAV (`LYNX.exe --render`)
- [x] Desfazer/refazer (Ctrl+Z / Ctrl+Y), um passo por gesto do mouse
- [x] Velocity no Piano Roll (faixa embaixo), seleção múltipla, copiar/colar/duplicar, quantizar, snap
- [x] Solo por canal (clique direito no LED), renomear/colorir/clonar patterns (cores importadas do .flp)
- [x] Entrada MIDI e gravação de notas (MIDI e teclado do PC)
- [x] Exportar pela interface: WAV 16/24/32f, FLAC, OGG, com cauda e progresso (Ctrl+R)

## Fase 3: arranjo
- [x] Playlist: clips de pattern no tempo, trilhas, modo PAT/SONG, seek pela régua
- [x] Importar a playlist do `.flp` (clips de pattern, com recorte de início/fim)
- [x] Clips de áudio na playlist (forma de onda, arrastar arquivo, recorte, retomada no meio após seek)
- [x] Importar clips de áudio do `.flp` (offsets em tempos ou em ms, detectado por clip)
- [x] Streaming do disco para áudios longos (cache mapeado em memória + prefetch)
- [x] Time-stretch/resample de clips de áudio (WSOLA mantendo a altura ou reamostrando)
- [x] Loop region, seleção múltipla, copiar/colar/duplicar clips, clips "únicos"
- [x] Vários arrangements (seletor na Playlist; importados do `.flp`)
- [x] Automação: clips na Playlist, editor de pontos com curva; volume/pan, efeitos e parâmetros de plugins
- [x] Importar automação do `.flp` (pontos, curva, alvo: volume/pan de canal e mixer, parâmetros)

## Fase 4: mixer e plugins
- [x] Mixer: Master + até 127 inserts, fader com a curva do FL (0,8 = 0 dB), pan, mute, solo, medidores estéreo, rampas sem estalo
- [x] Roteamento canal → insert (caixinha no Channel Rack, menu do mixer); importado do `.flp`
- [x] Envios entre inserts com nível, "só para esta trilha", solo pelo caminho do sinal; importados do `.flp`
- [x] 10 slots de efeito por insert (e no Master), com bypass, trocar, mover, remover
- [x] Efeitos internos: EQ de 3 bandas, compressor, delay sincronizado (ping-pong), reverb
- [x] Hosting de VST3: instrumentos nos canais e efeitos nos slots, janela do plugin, estado salvo no projeto
- [x] Busca de plugins com janela de progresso, lista salva em %APPDATA%\LYNX, "dead man's pedal"
- [x] Busca em processo separado, com lista negra
- [x] CLAP (formato próprio sobre o JUCE: parâmetros, estado, latência, interface)
- [x] Compensação de latência (PDC) por aresta, como o FL; export descarta a latência do início
- [x] Processamento multi-thread: canais em paralelo e mixer por camadas (sem locks)
- [ ] Plugins em processo separado (sandbox), estilo ilbridge

## Extras já feitos
- [x] Swing, ajustes do sampler, trilhas da Playlist com nome/mute, dividir clip
- [x] Importar/exportar MIDI, gravação de áudio da entrada
- [x] Dry/wet por slot e smart disable

## Ainda por fazer (além da fase 5)
- [x] Automação de tempo (mudança de compasso ainda não)
- [x] Propriedades de nota (pan, afinação fina, mute, slide) importadas do `.flp` (porta ainda não)
- [x] Time-stretch/resample de clips de áudio (WSOLA mantendo a altura ou reamostrando)
- [x] Renderizar insert para áudio (congelar)

## Fase 5: escalar
- [x] Renderizar insert para áudio (congelar)
- [x] Render/export para WAV/FLAC/OGG (MP3 não: o JUCE não codifica MP3)
- [x] WASAPI exclusivo e "Low Latency" (já vêm do JUCE; aparecem em Configurações de áudio,
      conferido com `LYNX.exe --dispositivos saida.txt`)
- [ ] ASIO (precisa do SDK da Steinberg; decisão de licença pendente)
- [x] UI na GPU: o JUCE 8 já desenha com Direct2D no Windows

## Fase 6: fidelidade ao FL (com o REA)
- [x] Curvas de volume/fader, swing, envelopes, time-stretch, cut groups (rodada 3)
- [x] 3x Osc → synth interno: osciladores, níveis, pan, semitons (rodada 4)
- [x] Filtro do canal (Mod X/Y, 8 tipos) no synth e no sampler
- [x] Envelopes de corte e de afinação do canal no synth
- [x] Efeitos nativos do FL nos slots do mixer viram efeitos internos parecidos
- [x] ... com os ajustes salvos: Parametric EQ 2, Limiter, Reeverb 2, Delay 2/3, Compressor (rodada 7)
- [x] EQ embutido das trilhas do mixer, LFOs do canal, porta, AM do 3x Osc (rodada 6)
- [x] Corte de graves/agudos e predelay no reverb; efeito de matriz estéreo (Stereo Shaper); Maximus
- [x] VSTs de terceiros do "Fruity Wrapper": achar o mesmo plugin instalado e restaurar o estado VST3 (rodada 5)
- [ ] Estado CLAP do wrapper e mapa de parâmetros VST3 para automação (formatos ainda em aberto)
- [x] Filtros BP/HP/BS do canal (tipos 2-4 do FL)
- [x] LFOs do canal, AM do 3x Osc
- [x] Marcadores da Playlist e mudanças de compasso (régua, grade, snap, contador; import do .flp)
- [x] Metrônomo seguindo as mudanças de compasso (modo song)
- [x] Notas "porta" (por nota e do canal, synth e sampler)
- [x] FPC, DirectWave, FL Keys, Fruity Slicer/Slicex como sampler (rodadas 8–9)
- [x] Harmless, MiniSynth, DX10, Fruit Kick aproximados pelo synth interno (rodada 10)
- [x] FM no synth interno (osc 3 modula a fase de osc 1 + 2, índice com decaimento)
- [x] Sytrus (matriz FM/RM, filtros, articuladores) aproximado pelo synth interno (rodada 11)
- [x] GMS; Ogun/Autogun e Sakura (grosseiros); DrumSynth Live por sample (rodada 12)
- [x] Harmor (aproximação grosseira: partes A/B, filtro, envelopes, uníssono)
- [x] Gross Beat (gate de volume), Frequency Splitter, Effector (rodada 14)
- [x] Links do Peak Controller (automação ao vivo)
- [x] Patcher: cadeia em série vira slots seguidos; Patcher instrumento usa o gerador de dentro
- [x] Pitch Shifter, Frequency Shifter, Emphasis, Low Lifter, Send, Transporter, Squeeze (rodada 15)
- [x] Envelope Controller (canal controlador + automação ao vivo)
- [x] Patcher em ramos paralelos simples (contêiner); links de X-Y Controller / Control Surface (valor fixo)
- [ ] Patcher M/S e multibanda, editor do canal controlador
- [x] Kepler, Wave Traveller; FLEX pelo nome do preset (os pacotes são protegidos e não são abertos)
- [x] BooBass; Drumpad pelo nome do patch (bateria do LYNX ou tambor no synth)
- [x] PoiZone, BassDrum, Toxic Biohazard (rodada 15)
- [ ] Morphine
