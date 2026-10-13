# Roteiro de teste manual

O que os testes automáticos não cobrem: ouvir o som na placa e mexer com mouse/teclado. O executável
de teste fica em `C:\Users\kauan\lynx-test\LYNX.exe` (copiado a cada build).

## 1. Importar projetos do FL (5 min)
1. Abrir `Arquivo > Abrir` e escolher um demo em
   `C:\Program Files\Image-Line\FL Studio 2026\Data\Demo projects\Demo songs` (ex.: *LeriQ - SHINE*,
   *Bombs Away - I'm Awake*, *Sid De La Cruz - Secret Agent Rendezvous*).
2. Dar play no modo SONG. Conferir:
   - os canais 3x Osc (synth interno) soam parecidos com o FL (afinação, filtro, plucks, kicks feitos no 3x Osc);
   - EQs paramétricos, reverbs, delays e limiters do mixer estão ativos (abrir um slot "EQ paramétrico"
     mostra a curva importada);
   - a régua da Playlist mostra os marcadores e as mudanças de compasso (o *Secret Agent* tem 7/4 e 6/4);
   - o metrônomo acentua o primeiro tempo de cada compasso, inclusive nos trechos 7/4.
3. Exportar (`Ctrl+R`) e comparar o arquivo com o render do FL do mesmo projeto.

## 2. Synth e sampler (5 min)
- Num canal synth: `Presets do FL...` lista os 169 presets do 3x Osc; trocar alguns e tocar com o
  teclado do PC (`Z`..`M`, `Q`..`P`).
- `Modulação (envelopes e LFOs)...`: ligar um LFO de afinação (vibrato) e um de volume (tremolo).
- Botões Mono / Porta / AM e o knob Tempo porta.
- No Navegador (F8), em "Presets do 3x Osc (FL)", dar duplo clique num preset: cria um canal novo.
- Num sampler: filtro (tipo, corte, ressonância), Porta + Cut itself (glide de 808).
- Piano Roll: `Ctrl+P` marca notas com porta (traço no começo da nota).

## 3. Mixer (5 min)
- Menu da trilha (clique direito no nome) > `EQ da trilha...`: 9 knobs do EQ embutido.
- Adicionar "EQ paramétrico": `Abrir EQ paramétrico` -> arrastar alças, roda do mouse = Q, clique
  direito = tipo e inclinação.
- Menu do slot > `Presets do FL` (EQ, Limiter, Reeverb 2, Delays, Compressor, Stereo Shaper).
- Efeito "Utilitário" (ganho, balanço, matriz estéreo).

## 4. Plugins de terceiros
- `Plugins > Procurar plugins` e depois abrir um `.flp` que use VST3/CLAP pelo Fruity Wrapper: o
  plugin deve abrir com o mesmo preset do FL. Plugins que faltarem aparecem num aviso.

## 5. Áudio
- `Audio...`: os tipos "Windows Audio", "Windows Audio (Exclusive Mode)" e "Low Latency Mode"
  aparecem; conferir latência e estalos com um projeto pesado.

Anote o que soar diferente do FL (canal/efeito/trecho) que eu ajusto.
