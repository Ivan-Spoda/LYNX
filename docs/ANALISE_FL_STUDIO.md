# Análise comportamental do FL Studio 2026 (FLEngine_x64.dll)

> **Natureza deste documento.** Engenharia reversa *clean-room*, apenas comportamental, para orientar o
> projeto de uma DAW nova e independente (C++/JUCE). Nenhum código da Image-Line, nem pseudocódigo
> decompilado, foi copiado. Tudo abaixo é descrição, com palavras nossas, de comportamento, estruturas
> (conceitualmente) e algoritmos. Endereços e nomes de símbolos servem só como rastreabilidade da evidência.

- **Alvo:** `/home/kauan/projetos/daw-analise/FLEngine_x64.dll` (Delphi, PE x86-64, ~22 MB)
- **SHA-256:** `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02`
- **Ferramentas:** REA MCP + Ghidra 12.1.4 (59.803 procedimentos, 42.077 strings)
- **Data:** 2026-10-12

### Legenda de confiança

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings / nomes RTTI de classes e units Delphi |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `FUN_xxxxxxxx` = endereço da função no Ghidra; `G[0x15e....]` = ponteiro global da DLL
(variável de unit Delphi).

---

## 0. Visão geral da arquitetura

O motor é um único "god object" global com estado em variáveis de unit, não um grafo de objetos
encapsulado. O caminho quente é:

```
Driver (thread do ASIO / thread de mixagem própria / DirectSound)
  └─ FUN_00d0ece0  render(N amostras)  ── divide o buffer em sub-blocos nas fronteiras de tick do sequenciador
       ├─ FUN_00d0e840  avança 1 tick (posição da música, marcadores, contagem regressiva)
       └─ FUN_00d0eb10 → FUN_00d0d4c0  processa 1 sub-bloco:
            ├─ transporte/loop/marcadores, fade de start/stop
            ├─ geradores (canais do Channel Rack) → somados nos buffers das trilhas do mixer
            │     (geradores "threaded" rodam num grupo de tarefas paralelo)
            └─ FUN_00cf42f0  mixer: executa as "camadas" do mapa de threading do mixer
```

Um único `CRITICAL_SECTION` global (`G[0x15e3d40]`) envolve o render inteiro em todos os drivers e
também a reconstrução do mapa de threading do mixer. **[DECOMP]**

---

## 1. Núcleo de áudio: callback, threading, buffers, drivers

### 1.1 O laço de render dirigido por ticks — `FUN_00d0ece0` **[DECOMP]**

Comportamento:

1. Recebe uma posição-alvo absoluta em amostras (o contador acumulado do driver).
2. Mantém um contador de amostras corrente (`G[0x15e0b78]`) e a posição em amostras do **próximo tick**
   (`G[0x15e1a00]`).
3. A posição do próximo tick vem de um acumulador em **double** (`G[0x15e3450]`) somado a cada tick com
   "amostras por tick" (`G[0x15e2600]`, também double) e arredondado. Ou seja, o tempo de tick é
   fracionário e não acumula erro de arredondamento. Existe um modo alternativo em que o acumulador é
   multiplicado por um fator float e o resultado é deslocado `<<3` (posições em 1/8 de amostra?) **[HIP]**.
4. Quando o contador chega na fronteira do tick, incrementa o contador de ticks (`G[0x15e3aa0]`) e chama o
   processador de tick (`FUN_00d0e840`), que incrementa a posição da música em ticks (`G[0x15e3550]`),
   cuida do pré-roll/contagem regressiva (modo `G[0x15e09f8]==2`) e dos marcadores.
5. Renderiza um **sub-bloco** do ponto atual até `min(próximo tick, alvo)`. O tamanho do sub-bloco vai para
   `G[0x15e4dc8]`; se for maior que o máximo alocado, os buffers de trabalho são realocados na hora
   (`FUN_00d0ead0` → `FUN_00d0cee0`).
6. Depois de cada sub-bloco, o ponteiro do buffer do master avança `len*8` bytes (estéreo float32
   intercalado) (`FUN_00d0eb10`). No fim, o ponteiro volta para o início.

**Conclusão central:** o FL **não processa o buffer do driver de uma vez só**. Ele o fatia nas fronteiras
de tick do sequenciador (PPQ), e todos os eventos de nota e automação caem em fronteiras de sub-bloco.
É o modelo clássico do FL ("tick-accurate"), e explica por que o FL entrega buffers de tamanho variável
aos plugins (e por que existe a opção "fixed size buffers", que aumenta a latência; ver strings
`0x14890ae`).

No render offline há um detector de cauda: depois do fim da música, ele testa se o master caiu abaixo de um
limiar (dB×−96 escalado) para encerrar o render (`FUN_00d0eb10`). **[DECOMP]**

Proteção contra overflow: quando o contador de amostras passa de `0x7FA81BFE` (~2,14 bilhões), todos os
contadores de posição (int e double) são deslocados juntos dessa mesma quantidade (`FUN_012aef00`).
**[DECOMP]**

### 1.2 Processamento de um sub-bloco — `FUN_00d0d4c0` **[DECOMP]**

Ordem observada:

1. **Transporte:** trata o salto de posição pendente, os marcadores da playlist (registros de 0x34 bytes
   com posição e tipo: tipo 1 = marcador de início/loop, 3 = stop/pausa, 9/10 = punch-in/punch-out
   **[HIP para os nomes]**), a região de loop e o "loop de seleção".
2. **Fade de start/stop:** um envelope de ganho global com curva cossenoidal elevada a potências
   (`(1−cos)/2` elevado a 9, mapeado para 0,01…1) evita cliques ao iniciar ou parar. Fica protegido por um
   `CRITICAL_SECTION` próprio.
3. **Geradores (canais):** para cada canal, roda o plugin gerador num buffer próprio do canal e depois
   **soma** esse buffer no buffer da trilha de mixer de destino (campo do canal `+0x258`, limitado a
   0…500). Canais com várias saídas têm uma lista de saídas, cada uma com um offset de trilha. A soma em
   cada trilha é protegida por um **`CRITICAL_SECTION` por trilha** (trilha `+0x4c`), porque geradores
   rodando em threads diferentes podem escrever na mesma trilha. Depois da soma a trilha é marcada como
   "tem sinal" (`+0x148 = 2`).
4. **Geradores em paralelo:** se houver geradores marcados como "threaded", um grupo de tarefas é
   disparado (submit) antes do laço serial e aguardado (wait) depois (`DAT_016b8dd8`, vtable +0x10/+0x18).
5. **Mixer:** `FUN_00cf42f0` (ver §2).

### 1.3 Modelo de threading **[DECOMP]**

- **Pool de threads:** é obtido de uma fábrica externa (ponteiro de função `DAT_015f4648`, provavelmente
  exportada pelo executável hospedeiro) com contagem padrão = nº de threads lógicas da máquina
  (`FUN_00666b40`, `FUN_012c6f30`). Unit Delphi: **`QuickThreads`**, classe `TSTBTaskGroup` **[STR]**.
- **Contexto por worker:** um array (`G[0x15e0348]`) com um objeto de contexto por slot de worker
  (buffers de rascunho), criado na inicialização (`FUN_012c6f30`).
- **Grupo de tarefas:** uma estrutura com N slots de 0x20 bytes, um callback, um contexto e um **Event do
  Win32** (`CreateEventW`, auto-reset) para o join (`FUN_00666a60`, `FUN_00666960`).
  `submit+wait` = vtable +0x10 / +0x18 (`FUN_00666a30`).
- **Distribuição do trabalho (work claiming):** cada tarefa entra em laço fazendo *decremento atômico*
  (`LOCK dec`) de um contador global de itens restantes (`G[0x15e2f38]`). O valor obtido é o índice do
  item a processar, e o laço termina quando o contador fica negativo (`FUN_011fec40`, `FUN_011febf0`).
  Não há fila por thread: é um contador compartilhado ("self-scheduling").
- **Threads dedicadas observadas [STR]:** `TMixerThread` (ASIO com thread própria), `TMixerThread_DS`
  (DirectSound), `TRenderThread`/`TRenderWorker`/`TMixDownWorker`/`TRealtimeMasterWorker` (render
  para arquivo), `TMIDIThread`/`SyncThread`, `TWAVStreamingThread`, `TSampleLoaderThread`,
  `TBridgeReaderThread`/`TBridgeProcessingThread` (bridge), threads de browser/scan.
- **MMCSS:** a DLL importa dinamicamente `AvSetMmThreadCharacteristicsW` / `AvSetMmThreadPriority` e
  contém a string `"Pro Audio"` **[STR]**; o uso exato não foi rastreado.
- **Opções de usuário [STR]:** "Multithreaded generator processing", "Multithreaded mixer processing",
  "Allow threaded processing" (por plugin), "Allow smart disable", "Multithread plugins on this map".

### 1.4 Drivers

**Somente ASIO e DirectSound vivem no FLEngine.** Não há nenhuma string de WASAPI na DLL. O "FL Studio
ASIO" é um driver ASIO separado (o motor apenas o detecta e avisa: *"Your audio device is set to FL Studio
ASIO. This can cause inaccurate MIDI sync timing."*). **[STR]**

#### ASIO — callback `FUN_00c56ae0` (bufferSwitchTimeInfo) **[DECOMP]**

Registra o timestamp do bloco (tempo do sistema vindo do ASIOTime ou `timeGetTime`, mais o QPC). Depois
segue um de três modos:

| Modo | Condição | Comportamento |
|---|---|---|
| **A. Inline** | sem thread de mixagem | Dentro do callback do ASIO: pega o lock global, avança o contador em +bufferSize, aponta os ponteiros de E/S de cada trilha diretamente para a metade ativa do buffer duplo do ASIO, renderiza, chama `ASIOOutputReady` e limpa o master. |
| **B. Thread própria** (`TMixerThread`, `FUN_00c55740`) | thread de mixagem ativa | O callback faz `SignalObjectAndWait(evStart, evDone, timeout)`. A thread de mixagem acorda, renderiza e sinaliza. **Com timeout** (`DAT_016b8814`, configurável): se estourar, o callback reseta o estado e conta um underrun (`FUN_00c57950`, `FUN_01253980`). |
| **C. Buffer intermediário** | flag `DAT_016b87e8` | Copia ou converte entre buffers intermediários do FL e os buffers ASIO (função de conversão por formato), sinaliza a thread de mixagem para renderizar o **próximo** bloco de forma assíncrona e soma 1 buffer à latência de saída informada. |

Há também um guarda de reentrância/ocupado (`G[0x15e0430] > 0` → pula o render).

Abertura do ASIO — `FUN_00c57a00` **[DECOMP]**:

- Monta a lista de `ASIOBufferInfo` (0x18 bytes: isInput, canal, 2 ponteiros) só com os canais usados
  pelas trilhas do mixer: as saídas vêm em **pares estéreo** por trilha (`+0x370` = índice do par de
  saída, `+0x378` = entrada).
- Se várias trilhas mandam para a mesma saída física, a primeira usa uma rotina de "copiar" e as demais uma
  rotina de "somar" (ponteiros de função gravados em `+0x74`).
- Detecta os tipos de amostra float32/float64 (LSB/MSB) e marca os demais como "precisa converter".
- Registra no log: nome e versão do driver, `Buffer length: N frames`, latências de E/S e `Time out: X ms`.

#### DirectSound — `TMixerThread_DS` (`FUN_012aef00`) **[DECOMP]**

Buffer circular em duas metades. Espera por 2 eventos de notificação (`WaitForMultipleObjects`) ou,
alternativamente, faz polling do cursor de reprodução. Renderiza a metade livre e converte float → int16
(ou copia float32 quando o formato é 32 bits). Detecta underrun quando o cursor de reprodução cai dentro da
região recém-escrita.

#### Pull externo — `FUN_0125a760` **[DECOMP + HIP]**

Uma função "renderize N amostras agora" (limitada ao tamanho de buffer), usada quando o motor é dirigido de
fora. Hipótese: FL como plugin/ReWire ou hospedeiro externo.

### 1.5 Formato dos buffers **[DECOMP]**

- Buffers internos de trilha e de master: **float32 estéreo intercalado** (L,R,L,R…), tamanho
  `frames*2` floats.
- Tamanho máximo de sub-bloco: dinâmico, cresce sob demanda.
- Buffer do driver: definido pelo driver (ASIO) ou pela opção "Buffer length %dsmp (%dms)" (DirectSound).

### Implicações para a nossa DAW
- Adotar o **render fatiado por eventos** (sub-blocos nas fronteiras de tick ou de evento) é opcional. Em
  JUCE é mais limpo usar *sample-accurate* com `MidiBuffer` e offsets, mas o fatiamento simplifica a
  automação de parâmetros de plugins que só aceitam valores por bloco. Recomendação: sample-accurate por
  padrão, com fatiamento opcional por plugin.
- Acumular a posição do transporte em **double/int64** e calcular as fronteiras de tick sem acumular erro.
  Usar int64 desde o começo elimina o "rebase" de contadores que o FL precisa fazer.
- Evitar o **lock global** em torno do render: usar fila lock-free para mensagens UI→áudio.
- O desenho "callback do driver → handoff para uma thread de mixagem com timeout" é útil para drivers
  instáveis, mas custa latência ou jitter. Na JUCE o callback do `AudioIODevice` já é a thread de tempo
  real, então o recomendado é renderizar inline e usar workers para paralelismo.
- Somar em trilhas compartilhadas com `CRITICAL_SECTION` por trilha é simples mas pode causar contenção.
  Preferir buffers privados por gerador, somados pelo dono da trilha.
- Implementar a rampa de fade no start/stop do transporte (anti-clique).

---

## 2. Mixer

### 2.1 Estrutura da trilha **[DECOMP]**

- Array estático global de trilhas (`G[0x15e06a0]`), com **0x1478 bytes (5.240 B) por trilha**.
- Índices: o código limita destinos a **0…500** e trata o índice **501 (`0x1f5`)** como especial (fica fora
  da limpeza pós-mix e da camada normal). Interpretação: 0 = Master, 1…500 = inserts, 501 = trilha
  especial ("Current"/preview) **[HIP para o papel exato]**. FL 2026 suporta, portanto, até
  **500 inserts**.
- **10 slots de efeito por trilha** (array de 10 ponteiros em `+0x134c`). Uma flag em `+0x380` indica
  "slots habilitados" (`FUN_00cf3660`).
- Campos identificados: `+0x34` ponteiro do buffer de áudio; `+0x4c` lock da trilha; `+0x74` rotina de
  saída para o hardware; `+0xd0/+0xe4/+0xec` ponteiros de E/S do ASIO; `+0xfc` nível no grafo (ver
  abaixo); `+0x148` flag de sinal; `+0x1c0` objeto "processador" da trilha (método virtual
  `Process(len)`); `+0x370/+0x378` saída/entrada física.

### 2.2 Mapa de threading do mixer — `FUN_00cf3790` **[DECOMP]**

Algoritmo de escalonamento (nossa descrição):

1. Existe uma lista de trilhas ativas **ordenada por nível de dependência** (`+0xfc`), ou seja, uma ordem
   topológica do grafo de roteamento.
2. A lista é varrida e agrupada em **camadas (layers)**. Uma camada nova começa quando o nível aumenta, e
   também na primeira trilha com índice > 500.
3. Dentro de uma camada, as trilhas são agrupadas em **unidades (units)**. Uma trilha abre unidade nova
   apenas se tem plugins "paralelizáveis" e não tem saída física dedicada. Caso contrário, é anexada à
   unidade corrente e roda em série com ela.
4. Para cada camada é criado um grupo de tarefas com `min(nº de unidades, nº de workers)` tarefas.
5. O log diz literalmente: *"Mixer threading map rebuilt: N layer(s) / Layer i: M unit(s) / Unit j:
   track(s) …"*.

Execução (`FUN_00cf42f0` → `FUN_00cf4250` → `FUN_011fec40` → `FUN_00cf4430`):

- **As camadas rodam em sequência, com barreira entre elas**: submit + wait por camada.
- Dentro de uma camada, os workers pegam unidades pelo contador atômico. Cada unidade processa suas trilhas
  em série chamando o `Process(len)` virtual do objeto da trilha, com o contexto do worker injetado.
- Sem multithreading (mapa vazio), as trilhas rodam em série na ordem da lista.
- Ao final, as trilhas que receberam sinal têm o buffer zerado para o próximo bloco (exceto a 501).

### 2.3 Roteamento e sends **[STR + DECOMP parcial]**

- No FLP, o roteamento entre trilhas vem no evento `0xEB` (235) e os parâmetros de trilha em `0xEC` (236);
  ver §5. O nível topológico `+0xfc` é recalculado a partir desse grafo.
- Classes `TFXTrackSideIOList` e `TMixerImplementation` **[STR]**.

### 2.4 Compensação de latência (PDC) **[STR]**

- Menus e opções: "Plugin delay compensation", "Auto PDC", "Reset PDC", "Reset manual latency on all
  tracks", "Compensate for master latency", "Delay never bypass latency comp", "Trim PDC silence" no
  export.
- Cada trilha tem **latência manual + latência dos plugins**: `"%sms (manual) + %sms (plugin)"`.
- Opção "Delay the output of this track by the total mixer latency when it has an external output".
- Compensação de latência de gravação: "Compensate for latency when recording audio from an external
  device"; o diagnóstico mostra `"Latency: input: %dsmp, output: %dsmp, output + plugins: %dsmp"`.
- O algoritmo exato de PDC (onde ficam as linhas de atraso) **não foi decompilado**.

### Implicações para a nossa DAW
- Copiar a **ideia** do escalonamento por camadas topológicas: é simples, determinístico e funciona bem.
  Uma melhoria possível é trocar a barreira por camada por um escalonamento por dependência ("ready
  counters") para reduzir o tempo ocioso.
- Agrupar trilhas sem paralelismo útil em "unidades seriais" reduz o overhead de despacho.
- Planejar um limite alto de trilhas (FL: 500) com alocação dinâmica, em vez de um array estático de 5 KB
  por trilha.
- PDC: latência por nó = manual + soma dos plugins. Atrasar cada caminho até o máximo do ponto de soma e
  oferecer "trim PDC silence" no export.

---

## 3. Sequenciamento

### 3.1 Timebase e tempo **[DECOMP]**

- O **PPQ** vem do cabeçalho do FLP (`FLhd`, offset 12) e fica num global (`G[0x15e0db0]`). Um valor
  legado especial (16) é trocado pelo PPQ padrão atual. Há UI "Timebase (PPQ)" e o aviso de perda de dados
  ao reduzir o PPQ **[STR]**.
- **Tempo armazenado como inteiro BPM×1000.** O evento `0x9C` (156) define o tempo completo. O evento
  legado `0x42` (66) define a parte inteira (×1000, preservando a fração). Há também um evento de ajuste
  fino `0x5D` (93) **[DECOMP para 0x42/0x9C]**.
- Posição da música em **ticks inteiros** (`G[0x15e3550]`), incrementada 1 a 1 pelo processador de tick.

### 3.2 Channel Rack, patterns, notas **[DECOMP parcial]**

- Canais são objetos `TChannel` numa lista (`G[0x15e2218]`) com o plugin gerador, buffer de saída próprio
  e trilha de destino **[DECOMP]**. `TChannelSample`/`TChannelSamplePool` cuidam dos samples **[STR]**.
- Patterns ficam num array global com stride de 0xC0 bytes por pattern (`G[0x15e2aa0]`). Índice máximo de
  pattern no carregamento: **999** (evento `0x41` limitado a 999) **[DECOMP]**.
- Modos: `G[0x15e0ef0]==1` → modo Song (playlist), caso contrário modo Pattern **[DECOMP]**.
- **Registro de nota** (lido do evento `0xE0`/224): até FL 20.99 tem **24 bytes por nota**; em versões
  novas o tamanho é o tamanho nativo do registro corrente (`FUN_00f558e0`) **[DECOMP]**. A leitura
  confirma estes campos:
  - byte `+0x10` = *fine pitch* (versões < 3.1.6 recebem 120 = centro) **[DECOMP]**;
  - byte `+0x13`: o nibble baixo é convertido em versões < 5.3 (provavelmente canal MIDI/cor) **[DECOMP]**.
  - O layout completo (pos u32, flags u16, canal u16, duração u32, tecla u16, grupo u16, fine pitch u8,
    ?, release u8, canal MIDI u8, pan u8, velocity u8, mod X u8, mod Y u8) bate com a documentação
    pública da comunidade (PyFLP) e com os dois campos confirmados acima. **[HIP forte]**

### 3.3 Playlist e arrangements **[DECOMP parcial]**

- Existem **múltiplos arrangements**: o evento `0x63` (99) seleciona ou cria o arrangement corrente
  (*"FLP : Wrong arrangement index !"*). **[DECOMP]**
- Os itens da playlist vêm no evento `0xE9` (233), como array de registros: **32 bytes** até FL 20.99, e
  nas versões novas o tamanho nativo, com variantes de **60 (0x3C)** e **80 (0x50)** bytes conforme flags
  do arquivo. **[DECOMP]**
- Strings "Audio clips", "Automation clips", fades de clip (`TPlaylistFadesTest`), merge e chop de clips.
  Marcadores de tempo em registros de 0x34 bytes no arrangement (`song+0x21C48`). **[DECOMP]**

### 3.4 Automação **[STR]**
- Automation clips, `TAutomationDelay`, "Mapping formula" por clip, eventos de gravação de controle
  (`0xD8` CtrlRecChan, `0xDF` PatCtrlRecChan).

### Implicações para a nossa DAW
- Guardar o tempo como inteiro de alta resolução (o FL usa BPM×1000) ou em double. Para interoperar com
  FLP, converter `tempo_flp / 1000.0`.
- Usar um PPQ interno alto (ex.: 960) e reescalar ao importar FLP (PPQ do cabeçalho).
- O modelo de dados de importação é: canais → patterns (notas por canal) → playlist (clips de pattern,
  áudio ou automação) → arrangements.

---

## 4. Hospedagem de plugins

### 4.1 Formatos **[STR]**
- **VST2** ("Fruity Wrapper", `TVSTPluginEditor`, `TVSTPresetFile`), **VST3** (`TPluginFormatVST3`,
  `TVST3Plugin`, `TVST3ComponentHandler 1/2/3`, `TVST3ParameterChanges`, `TVST3EventList`, módulo
  `Vst3ModuleInfo` = leitura de `moduleinfo.json`, extensões próprias `il.VST3Extensions`,
  `VST3_IPSLHostCommands`/`IPSLContextInfo`), **CLAP** (`TPluginFormatCLAP`, `TCLAPPlugin`,
  `clap_entry`, extensões `clap.thread-pool`, `clap.thread-check`, `clap.latency`) e o formato nativo
  **FL** (`TPluginFormatFL`).
- A extensão `clap.thread-pool` implica que o host expõe o seu pool de threads a plugins CLAP.
- "Combine VST and VST3 versions of a plugin" (deduplicação no banco de plugins).

### 4.2 Bridging / processo separado **[STR + DECOMP parcial]**
- Executáveis `32bit\ilbridge.exe` e `64bit\ilbridge.exe`. A opção por plugin "Load plugin in separate
  process (bridged)" e "Make bridged" mostram que o bridge serve tanto para 32↔64 bits quanto para
  sandbox no mesmo número de bits.
- Unit `IPC.Bridge`: `TBridge`, `TBridgeShare` (base), `TBridgeMessageShare`, `TBridgePingShare`
  (heartbeat), `TBridgeDataShare`, `TBridgeReaderThread`, `TBridgeProcessingThread`,
  `TBridgeProcessingJob`, `TWaitingThreadInfo` (dicionário por thread-id para chamadas síncronas),
  `TBridgeWrapperProcessThread`/`Share` (processamento de áudio), `TSimpleBridgeEventBuffer`.
- **Memória compartilhada nomeada [DECOMP]:** nomes formatados como `"%s_%d_%s"` e `"%s_%d_data"`
  (prefixo, id numérico, sufixo). A criação faz `OpenFileMappingW` e, se não existir,
  `CreateFileMappingW` em pagefile com o tamanho pedido, seguido de `MapViewOfFile` (`FUN_00c83820`,
  `FUN_00c915b0`).
- Mensagens para a GUI: `PostMessage(PM_ProcessMessages)`, "Disconnect calls ExecuteOnGUIThread".

### 4.3 Scan **[STR]**
- Processo separado "FL Studio Plugin Scanner" (chave `Software\Image-Line\FL Plugin Scanner`),
  `TScanThread`, `TCopyPluginDBThread`, "fast plugin scan", banco de plugins
  ("Scanning plugin database (generators/effects)").

### 4.4 Execução por plugin **[STR]**
- Flags por plugin: "Allow plugin to be multi-threaded by FL Studio", "Allow smart disable" (desliga o
  plugin no silêncio), "fixed size buffers" (com aviso de latência extra), "Report latency".
- `TPlugMonitor`/`TPlugMeasureEntry`: medição de CPU por plugin.

### Implicações para a nossa DAW
- JUCE já cobre VST3 (e VST2 com SDK legado). Para **CLAP**, usar `clap-juce-extensions` ou um host CLAP
  próprio, e prever a exposição de `thread-pool`.
- Para o bridge, o desenho IPC do FL é: memória compartilhada nomeada para áudio e dados, canal de
  mensagens, ping/heartbeat e thread leitora + thread de processamento. Isso pode ser reproduzido com
  shared memory + semáforos/eventos e um processo `daw-bridge` por plugin ou por grupo.
- Fazer o scan num processo separado com banco persistente e modo "fast scan" (só arquivos novos).
- Implementar "smart disable" (detecção de silêncio + bypass), medição de CPU por plugin e a opção de
  buffer fixo por plugin.

---

## 5. Formato de arquivo .flp

Esta seção é a mais precisa: o leitor de eventos foi decompilado por completo.

### 5.1 Container **[DECOMP]** (`FUN_00f51fd0`, `FUN_00f54900`, `FUN_00f54d70`)

```
Offset  Tam  Conteúdo
0       4    "FLhd"
4       4    tamanho do bloco de cabeçalho (normalmente 6). O leitor pula para 8+tamanho
8       2    formato (u16 LE)
10      2    nº de canais (u16 LE)
12      2    PPQ (u16 LE); o valor legado 16 é substituído pelo PPQ padrão
--      4    "FLdt"
+4      4    tamanho do bloco de dados
+8      ...  sequência de eventos até o fim do arquivo
```

O arquivo precisa ter pelo menos 0x16 (22) bytes. Valores de **formato** vistos no código: `0x00` =
projeto (song), `0x10` = score, `0x18` = automação, `0x20` = preset de canal, `0x30–0x32` = presets de
plugin (`0x31`/`0x32` escolhidos pelo bit 0x10 das flags do evento 0xD4), `0x40` = preset de trilha de
mixer **[DECOMP para os valores; nomes vêm das mensagens "project/preset/score/automation"]**. Um alvo de
formato 0x20 aceita arquivo 0x30. Formato incompatível gera *"Project format is not compatible with
target, aborting"*.

### 5.2 Codificação de evento **[DECOMP]** (`FUN_00a9e7a0` e leitores `FUN_00a9e430/4d0/570/610`)

1. Lê **1 byte** = ID do evento.
2. **Novidade:** se o ID for **`0xAC` (172)**, lê mais **4 bytes (u32 LE)** que são o **ID estendido**.
3. A classe de tamanho sai do ID:

| ID (1 byte) | ID estendido (bits altos) | Payload |
|---|---|---|
| `0x00–0x3F` | `0x0xxxxxxx` | 1 byte (u8) |
| `0x40–0x7F` | `0x4xxxxxxx` | 2 bytes (u16 LE) |
| `0x80–0xBF` | `0x8xxxxxxx` | 4 bytes (u32 LE; valor tratado como int32 com sinal) |
| `0xC0–0xFF` | `0xCxxxxxxx` | tamanho variável: varint + bytes |

4. **Varint de tamanho:** bytes de 7 bits em little-endian (bit 7 = continua), no estilo LEB128 sem sinal.
5. Payloads < 1 MiB vão para um buffer interno de 1 MiB; os maiores são alocados no heap.
6. Um **filtro opcional** decide se o payload variável é lido ou pulado (seek). No load de projeto o
   filtro pula **apenas o evento `0xD5`** (dados de estado de plugin), que é lido depois sob demanda
   (`FUN_00f45ad0`).
7. Existe um modo opcional de pós-processamento dos bytes lidos (flag do leitor `+0x11`, `FUN_00a9ece0`):
   descriptografia ou checksum **[HIP]**.
8. **Texto:** depende da versão do arquivo. Se `versão > 11.5.4`, as strings são **UTF-16LE**; caso
   contrário, ANSI (flag do leitor `+0x10`) (`FUN_00a9e8e0`).

IDs estendidos vistos no dispatcher: `0xC0000100`, `0xC0000102` (texto variável, ex.: lista de strings
parseada linha a linha) e o ID `0x100` (fora da faixa de 1 byte, logo só alcançável via `0xAC`).

### 5.3 Versão **[DECOMP]**
O evento **`0xC7` (199)** é a string de versão (ex.: `"25.1.2.4567"`). Ela é convertida num inteiro
**`major*1.000.000 + minor*1.000 + patch`** e usada em dezenas de *gates* de compatibilidade:
8.1.0 (registros antigos), 11.5.4 (UTF-16), 12.9.0, 20.99.1 (tamanho dos registros de nota e playlist),
24.2.99 etc. Arquivo mais novo que o programa gera um aviso.

### 5.4 Tabela de eventos tratados pelo carregador de projeto

O dispatcher `FUN_00f46490` (3.753 linhas decompiladas) trata **181 IDs distintos**. Os nomes abaixo
seguem a nomenclatura pública da comunidade (FLP SDK/PyFLP). A coluna "Evidência" indica o que confirmamos
por decompilação.

| ID | Classe | Nome (comunidade) | Evidência neste binário |
|---|---|---|---|
| 0x00 | u8 | ChanEnabled | tratado |
| 0x09 | u8 | LoopActive | chama setter de loop |
| 0x0F | u8 | (flag de pattern/canal) | tratado |
| 0x15 (21) | u8 | ChanType (0…5) | **[DECOMP]** faixa 0–5 validada ("ABORT FLP_ChanType … out of range") |
| 0x16 (22) | u8 | TargetFXTrack | **[DECOMP]** "FLP_TargetFXTrack target track out of range" |
| 0x1B (27) | u8 | FX track flags | **[DECOMP]** bits 0x1/0x2/0x4/0x8/0x10 → flags da trilha; bit 0x10 desliga os slots |
| 0x32 (50) | u8 | (propriedade de canal) | tratado |
| 0x40 (64) | u16 | NewChan (índice do canal corrente) | **[DECOMP]** cria ou seleciona o canal |
| 0x41 (65) | u16 | NewPat (índice da pattern corrente) | **[DECOMP]** limitado a 999 |
| 0x42 (66) | u16 | Tempo (parte inteira, legado) | **[DECOMP]** tempo = v×1000 + fração antiga |
| 0x44 (68) | u16 | (máscara de 16 bits por pattern) | **[DECOMP]** 16 flags |
| 0x5D (93) | u16 | TempoFine | tratado |
| 0x63 (99) | u16 | Arrangement atual | **[DECOMP]** |
| 0x8A (138) | u32 | (dois campos de 16 bits) | **[DECOMP]** |
| 0x93 (147) | u32 | FXOutChanNum | **[DECOMP]** correção de índice em versões < 3.6.3 |
| 0x9C (156) | u32 | FineTempo (BPM×1000) | **[DECOMP]** |
| 0x9E (158) | u32 | (campo de pattern, +0x18) | **[DECOMP]** |
| 0xC4 (196) | texto | SampleFileName | **[DECOMP]** `/` → `\` |
| 0xC7 (199) | texto | Version | **[DECOMP]** |
| 0xD4 (212) | dados | NewPlugin (cabeçalho de 0x34 bytes; flags em +0x10, bit 0x10 = gerador/efeito) | **[DECOMP]** |
| 0xD5 (213) | dados | PluginParams (estado opaco do plugin) | **[DECOMP]** pulado e lido sob demanda |
| 0xE0 (224) | dados | PatNoteRecChan = **notas da pattern** (array de registros de nota) | **[DECOMP]** |
| 0xE9 (233) | dados | **Itens da playlist** (array) | **[DECOMP]** |
| 0xEB (235) | dados | Roteamento do mixer | **[STR/HIP]** |
| 0xEC (236) | dados | Parâmetros da trilha de mixer (inclui campo ofuscado por XOR com constante) | **[DECOMP]** |
| 0xED (237) | dados | (registro de 16 bytes) | tratado |
| 0xF0 (240) | texto | (lista de strings global) | **[DECOMP]** |
| 0xF6 (246) | texto | (nome/propriedade de canal) | tratado |
| 0xE2/0xE3 | dados | (faixa tratada junto, `id-0xE2`) | tratado |

Lista completa dos IDs de 1 byte vistos no dispatcher (hex):
`00–06, 09–24, 26–34, 40–50, 53–59, 5B–5F, 61–65, 67, 68, 80–84, 87, 8A–96, 98–9E, A0–A2, A4–AB, C0–CF,
D1, D2, D4, D5, D7–DD, DF, E0, E1, E4–E7, E9–F3, F5–FF`, mais o estendido `0x100` e
`0xC0000100`/`0xC0000102`.

### 5.5 Arrays de registros versionados **[DECOMP]** (`FUN_00f558e0`)
Os eventos de array (notas, playlist, controles) são lidos como blob e reinterpretados com um **tamanho de
registro que depende da versão**:

| Tipo | < 8.1.0 | 8.1.0 … < 20.99.1 | ≥ 20.99.1 |
|---|---|---|---|
| 0 (controle/automação) | conversor legado | 12 bytes | tamanho nativo atual |
| 1 (notas) | conversor legado | 24 bytes | tamanho nativo atual |
| 2 (playlist) | conversor legado | 32 bytes | 60 ou 80 bytes, ou o tamanho nativo, conforme flags |

Ou seja: para importar FLP, o **tamanho do registro deve ser derivado do tamanho do evento ÷ nº de
itens**, ou da versão, e não fixado no código.

### Implicações para a nossa DAW (importador de FLP)
- Implementar o leitor de eventos com **suporte ao escape `0xAC` + ID de 32 bits**, que provavelmente
  quebra parsers antigos da comunidade.
- Usar varint LEB128 para o tamanho, strings UTF-16LE quando `versão > 11.5.4`, e o PPQ do cabeçalho.
- Tamanho de registro de nota e de playlist por versão (24/32 bytes antigos; 60/80+ nos novos).
- Pular e guardar `0xD5` (estado de plugin) como blob opaco. Só dá para reaproveitá-lo quando o mesmo
  plugin VST/VST3/CLAP existir do nosso lado: o payload do wrapper contém o chunk do plugin, mas o formato
  interno do wrapper ainda precisa de estudo.
- Tempo = `u32 / 1000.0` BPM (evento 0x9C).

---

## 6. Perguntas em aberto

1. **Algoritmo de PDC:** onde ficam as linhas de atraso (por trilha, por send, por slot) e como "Auto PDC"
   calcula o atraso? Falta decompilar o cálculo do nível `+0xfc` e da latência acumulada.
2. **Processamento interno da trilha** (`objeto +0x1c0`, `Process` virtual): ordem pré/pós-fader dos 10
   slots, sends, panning law, mute/solo, smart disable. É preciso resolver a vtable.
3. **Trilha 501 e o limite de 500:** confirmar o papel (Current/preview) e se o número de inserts é
   configurável.
4. **Formato exato do registro de nota e de playlist nas versões ≥ 20.99** (60/80 bytes): mapear os campos
   novos (provavelmente cor, slide, porta, flags de clip, offsets de áudio).
5. **Eventos de roteamento (0xEB) e de parâmetros de trilha (0xEC):** layout completo, incluindo o campo
   ofuscado por XOR.
6. **Payload do 0xD5 / wrapper:** como o chunk do VST/VST3/CLAP fica encapsulado.
7. **Modo de 1/8 de amostra** no cálculo do próximo tick (`<<3`): é sub-sample timing?
8. **Fábrica do pool de threads** (`DAT_015f4648`): vem do executável `FL64.exe`? Qual a política de
   afinidade/prioridade e MMCSS dos workers?
9. **IPC do bridge:** protocolo de mensagens, sincronização de áudio (eventos/semáforos? spin?) e timeouts.
10. **WASAPI:** onde está implementado (no `FL64.exe`? no driver "FL Studio ASIO"?). Não está nesta DLL.
11. **Pós-processamento do leitor de eventos** (flag `+0x11`, `FUN_00a9ece0`): checksum, criptografia ou
    hash de integridade?

---

## Apêndice A — funções-chave (rastreabilidade)

| Endereço | Papel inferido | Confiança |
|---|---|---|
| `FUN_00c56ae0` | ASIO bufferSwitchTimeInfo (3 modos) | DECOMP |
| `FUN_00c55740` | Execute da `TMixerThread` (ASIO) | DECOMP |
| `FUN_00c57a00` | Criação de buffers ASIO, mapeamento de E/S das trilhas, log de latência | DECOMP |
| `FUN_012aef00` | Thread de mixagem DirectSound | DECOMP |
| `FUN_0125a760` | Render por pull externo | DECOMP |
| `FUN_00d0ece0` | Laço de render fatiado por ticks | DECOMP |
| `FUN_00d0e840` | Avanço de 1 tick (posição da música / contagem regressiva) | DECOMP |
| `FUN_00d0eb10` | Wrapper do sub-bloco + detector de cauda + avanço do ponteiro do master | DECOMP |
| `FUN_00d0d4c0` | Processa sub-bloco: transporte, fade, geradores, mixer | DECOMP |
| `FUN_00cf3790` | Reconstrói o mapa de threading do mixer (camadas/unidades) | DECOMP |
| `FUN_00cf42f0` | Executa o mixer (serial ou por camadas) | DECOMP |
| `FUN_00cf4250` | Despacha a próxima camada | DECOMP |
| `FUN_011fec40` / `FUN_011febf0` | Tarefas de worker (mixer / geradores), contador atômico | DECOMP |
| `FUN_012c6f30` | Init: pool de threads + contextos por worker | DECOMP |
| `FUN_00666a60` / `FUN_00666b40` | Grupo de tarefas (QuickThreads) | DECOMP |
| `FUN_00f51fd0` | Abre o FLP e valida o cabeçalho FLhd/FLdt | DECOMP |
| `FUN_00f46490` | Dispatcher de eventos do FLP (181 IDs) | DECOMP |
| `FUN_00a9e7a0` | Leitor de evento (ID + escape 0xAC + classe de tamanho) | DECOMP |
| `FUN_00a9e610` | Leitor de payload variável (varint) | DECOMP |
| `FUN_00f558e0` | Leitor de arrays de registros versionados | DECOMP |
| `FUN_00c83820` | Memória compartilhada nomeada (bridge) | DECOMP |
