# Arquitetura

Objetivo: uma DAW com o fluxo de trabalho do FL Studio, leve o bastante para rodar
em qualquer PC independente do tamanho do projeto. Reimplementação clean-room:
o FL Studio é estudado para entender *comportamento*, nunca para copiar código.

## Regras do motor de áudio

1. A thread de áudio nunca aloca memória, nunca trava mutex, nunca faz I/O.
2. Comunicação UI → áudio só por atomics ou snapshots imutáveis trocados sem lock
   (`core/SnapshotExchange.h`): a UI edita o `Song`, publica uma cópia, e a thread de áudio
   devolve a anterior para a UI liberar.
3. Toda mudança estrutural (adicionar faixa, plugin, rota) é montada fora da
   thread de áudio e trocada de uma vez.

## Camadas

| Camada | Responsabilidade | Estado |
|---|---|---|
| `engine/Transport` | play/stop, BPM, posição em samples | feito |
| `engine/AudioEngine` | dispositivo de áudio + `AudioProcessorGraph` | feito |
| `engine/Metronome` | metrônomo liga/desliga | feito |
| `core/` (sem JUCE) | Song, Scheduler (precisão de sample), instrumentos internos, SnapshotExchange | feito |
| Channel Rack + Step Sequencer | patterns, steps, notas | feito |
| Piano Roll | edição de notas | — |
| Playlist | arranjo de patterns/áudio no tempo | — |
| Mixer | inserts, sends, rotas, efeitos | — |
| Plugin host | scan e hosting de VST3 (fora de processo depois) | — |
| Projeto | import de `.flp` (canais/patterns/notas) | feito |
| Projeto | salvar/abrir (formato próprio) | — |

## Escalabilidade ("roda em qualquer PC")

- Grafo processado em várias threads: cadeias independentes do mixer em paralelo.
- Samples longos lidos do disco em streaming, não carregados inteiros na RAM.
- Freeze/bounce de faixas pesadas para liberar CPU.
- UI desenhada na GPU e totalmente separada do áudio.
- Projeto carregado sob demanda.
