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
- [x] Salvar/abrir projeto no formato próprio (`.dawproj`, JSON versionado)
- [x] Volume, pan e mute por canal; teclado do PC como teclado musical
- [x] Exportar para WAV (`Daw.exe --render`)
- [ ] Velocity no Piano Roll, seleção múltipla, copiar/colar, desfazer/refazer
- [ ] Solo por canal, renomear/colorir patterns
- [ ] Entrada MIDI e gravação de notas
- [ ] Exportar pela interface (hoje só pela linha de comando)

## Fase 3: arranjo
- [ ] Playlist: clips de pattern no tempo, trilhas, loop region
- [ ] Clips de áudio na playlist (streaming do disco)
- [ ] Importar a playlist do `.flp` (os itens já são lidos pelo parser)
- [ ] Automação (clips de automação)

## Fase 4: mixer e plugins
- [ ] Mixer com inserts, sends e roteamento (o FL tem 125+ inserts e 10 slots por trilha)
- [ ] Efeitos internos básicos (EQ, compressor, reverb, delay)
- [ ] Hosting de VST3 (JUCE) e CLAP; scan em processo separado
- [ ] Compensação de latência (PDC)
- [ ] Processamento multi-thread do grafo por camadas de dependência (como o FL, ver análise)
- [ ] Plugins em processo separado (sandbox), estilo ilbridge

## Fase 5: escalar
- [ ] Freeze/bounce de trilhas
- [ ] Render/export para WAV/MP3/FLAC
- [ ] ASIO (precisa do SDK da Steinberg) e WASAPI exclusivo
- [ ] UI na GPU para projetos enormes
