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
- [ ] Velocity no Piano Roll, seleção múltipla, copiar/colar, desfazer/refazer
- [ ] Solo por canal, renomear/colorir patterns
- [ ] Entrada MIDI e gravação de notas
- [ ] Exportar pela interface (hoje só pela linha de comando)

## Fase 3: arranjo
- [x] Playlist: clips de pattern no tempo, trilhas, modo PAT/SONG, seek pela régua
- [x] Importar a playlist do `.flp` (clips de pattern, com recorte de início/fim)
- [ ] Clips de áudio na playlist (streaming do disco); os demos do FL usam para vocais e loops
- [ ] Loop region, seleção múltipla, copiar/colar clips, clips "únicos"
- [ ] Vários arrangements
- [ ] Automação (clips de automação)

## Fase 4: mixer e plugins
- [x] Mixer: Master + até 127 inserts, fader (0,8 = 0 dB), pan, mute, solo, medidores estéreo
- [x] Roteamento canal → insert (caixinha no Channel Rack, menu do mixer); importado do `.flp`
- [ ] Sends entre inserts e roteamento insert → insert (hoje todo insert vai para o Master)
- [x] 10 slots de efeito por insert (e no Master), com bypass, trocar, mover, remover
- [x] Efeitos internos: EQ de 3 bandas, compressor, delay sincronizado (ping-pong), reverb
- [ ] Hosting de VST3 (JUCE) e CLAP; scan em processo separado
- [ ] Compensação de latência (PDC)
- [ ] Processamento multi-thread do grafo por camadas de dependência (como o FL, ver análise)
- [ ] Plugins em processo separado (sandbox), estilo ilbridge

## Fase 5: escalar
- [ ] Freeze/bounce de trilhas
- [ ] Render/export para WAV/MP3/FLAC
- [ ] ASIO (precisa do SDK da Steinberg) e WASAPI exclusivo
- [ ] UI na GPU para projetos enormes
