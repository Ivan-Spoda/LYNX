# Análise comportamental do FL Studio 2026: rodada 22 (REA) — *sidechain* no mixer (marcação no `.flp`, barramento, Limiter/Compressor/Vocodex) e numeração dos parâmetros no Fruity Wrapper (VST2/VST3/CLAP) + estado CLAP

> **Natureza deste documento.** Continuação de `REA_rodada20.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são regras e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> (ex.: "esta lista é a ordem de processamento porque é ordenada pela profundidade no grafo") estão explicitadas.

- **Alvos:**

  | Binário | SHA-256 | Linguagem | Funções-chave novas |
  |---|---|---|---|
  | `FLEngine_x64.dll` (cópia `tmp/r18/bin/FLEngine.dll`, projeto `/tmp/r18gh_FLE` sem auto-análise) | = rodada 18 | Delphi | listas de entrada/saída `FUN_012528f0`, envios `FUN_012521f0`, iterador de trilhas `FUN_012c68f0`/`FUN_012c6950`/`FUN_012c6970`/`FUN_012c6930`, host `TFruityHost` VMT `0x13141b8` slot 36 `FUN_0132baa0` e slot 37 `FUN_0132bb50`, despachante (trechos `0x1320f2e`, `0x1321053`, `0x1321c91`), registro de entrada lateral `FUN_012bdb60`/`FUN_01316a50`, mistura dos envios `FUN_01277980`, cópia lateral `FUN_01277bd0`, sequência `0x12772b2`, tabela lateral `FUN_00982ac0` |
  | Fruity Limiter | `ed81447b31db7e5085c33c039f198483af71a353c481e85272d09d48e038b082` (= rodada 7) | Delphi (`TTestPlug`, VMT `0x6d30c0`) | estado `FUN_006d3760` (slot 3), `ProcessParam` `FUN_006d4490` (slot 6), render `FUN_006d5d60` (slot 7), gerente de entrada lateral `FUN_006ce570` |
  | Fruity Compressor | `f40214923d930176d246f8d35ccdcd28adecc9812eece931a6a1e0bc670581d1` (= rodada 7) | Delphi (VMT `0x6bd5f8`) | estado `FUN_006bd9a0` (slot 3), render `FUN_006bee30` (slot 7), gerente `FUN_006bb680` |
  | Maximus | `acc7affbe4d070ce8b71c13de79f267dd1b257f4207a61a733388a6aec1abdc1` | Delphi (`TTestPlug`, VMT `0x83ee68`) | estado `FUN_0083f8f0`, render `FUN_00844380` (só para a busca negativa) |
  | Fruity Peak Controller | `e73ec8dacb16d4db24444caa6d2d9db0a4b79246693288db4fa0972b63ec2d27` | Delphi | (só a busca negativa) |
  | Vocodex / Fruity Vocoder | = rodada 20 | Delphi | render `FUN_00856350` (Vocodex) |
  | Fruity Wrapper (gerador) | `fa5a2f6fbccfc42d3382627887228e9cc8f702a676de284cf6b3eeeb0686d2e7` (= rodada 5; o do efeito `c600a15b…2c74` também não mudou) | Delphi | `ProcessParam` `FUN_00870830` (slot 6 das VMTs `0x8685d8`/`0x868840`), VST2 `FUN_007af910`, VST3 `FUN_00850390`/`FUN_0084a2c0`/`FUN_0084e0d0`/`FUN_00850dc0`/`FUN_0084e5e0`, CLAP `FUN_0085ee20`/`FUN_0085f560`/`FUN_0085d6b0`/`FUN_0085d5c0`/`FUN_0085f900`, extensões CLAP `FUN_0085dd20`/`FUN_0085ef50` |

- **Ferramentas:** Ghidra 12.1.4 *headless*; projetos novos `/tmp/r22gh_Lim`, `/tmp/r22gh_Comp`, `/tmp/r22gh_Max`
  (+ cópia `_Max2`), `/tmp/r22gh_Wrap` (+ cópia `_Wrap2`), todos com auto-análise; o FLEngine foi lido no projeto
  `/tmp/r18gh_FLE` **sem** auto-análise (funções criadas sob demanda). Scripts em `tmp/r18/gh/scripts/`,
  `tmp/r19/gh/scripts/` e `tmp/r22/gh/gv.sh`; buscas de bytes em `tmp/r22/py/scan120.py`, `inter.py`, `riprefs.py`,
  `hostcall.py`; dados com `tmp/r22/py/sc.py` (eventos do mixer), `scuse.py`/`scsum.txt` (consumidores de *sidechain*)
  e `wrapauto.py` (automação de parâmetros do *wrapper*). Saídas da decompilação em `tmp/r22/out/`. Manual oficial
  lido via web.
- **Data:** 2026-10-13.
- **Numeração de trilhas:** contagem dos `0xEC` a partir do Master = 0, que é a mesma do `FlpImport`/`Flp.cpp`
  (conferida pelo índice 0 do cabeçalho `0xD4` dos plugins de slot, que traz o número da trilha, e pelos
  `controlID` `0x7…` do `0xE1`).

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/arquivos da instalação |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/`
(abreviada como `…/html/` abaixo). As citações do manual vieram de leitura web resumida; os trechos citados
são curtos e devem ser reconferidos na página antes de virar texto de interface.

---

## 1. *Sidechain* no mixer **[DECOMP + DOC + DADOS]**

**O que o LYNX supõe / precisa:** hoje `Flp.cpp` lê o `0xEB` como lista de destinos e o `FlpImport.cpp` lê os
níveis do `0xE1` (`p = 0x2000 + d`) e **apaga os envios com nível 0** ("o template do FL liga todo insert às
trilhas de send com nível 0; isso não soa"). O `FlPatcher.cpp` ignora arestas para barramento > 0. Não há entrada
lateral nos efeitos. Faltava saber como o `.flp` marca um *sidechain*, qual barramento o efeito recebe e quais
efeitos nativos consomem.

### 1.1 Como o `.flp` marca o *sidechain*: não há *flag*; é uma rota com nível de envio 0 **[DADOS + DOC + DECOMP]**

- **[DADOS]** Nos 165 `.flp`, os bytes do `0xEB` só assumem **0 ou 1** (1 687 590 zeros, 58 266 uns): não existe
  valor nem bit específico de *sidechain* no evento de rotas.
- **[DOC `…/html/mixer.htm` (seção "Internal Mixer Track Routing & Sidechaining")]** "Sidechain to this track" é
  "just a macro to creates a send link with the send volume set to 0"; "Sidechains are created when the audio Send
  knob is set to 0%"; "If the knob is set to any value other than 0% then there is BOTH an audio and sidechain
  send"; "you can't create an audio send without also creating a sidechain send"; "you don't hear the sidechain
  send on the destination Track unless some plugin on the destination Track is receiving it".
- **[DECOMP `FUN_012521f0`]** O vetor de envios de cada trilha (`T+0x1344`, registros de `0x18` B com destino e
  ganho) é montado percorrendo os destinos d = 0…501 e incluindo **todo destino cujo byte de rota
  (`T+0x398 + 8d`, o que o `0xEB` preenche — rodada 2 §2.5) é ≠ 0**, sem olhar o nível.
- **[DECOMP `FUN_01277980`]** Na mistura, um envio cujo ganho atual **e** cujo ganho de rampa são ambos 0 é
  pulado: não soma nada no destino. Ou seja: rota com nível 0 = ligação só de *sidechain*, exatamente como diz o
  manual.
- **Consequência para o formato:** *sidechain* = byte `0xEB` = 1 para o destino **e** nível `0xE1`
  (`0x70000000 + (S<<22) + 0x2000 + D`) = 0. Qualquer rota (com ou sem nível) também conta como fonte de
  *sidechain* (§1.2).
- **[DADOS]** Rotas S→D com D ≠ Master nos 165 arquivos: **11 857 com nível 0**, 197 com nível > 0 e **32 142 sem
  registro de nível** no `0xE1`. Os sem registro aparecem, entre outros, nos arquivos antigos (classe `0x2…`,
  < 24.2.99), cujo `controlID` só codifica trilhas 0…63 (`0x2000_0000…0x2FC0_xxxx`); **o valor que o FL assume
  quando o registro falta não foi confirmado**.

### 1.2 Lista de entradas da trilha e índice do barramento **[DECOMP FLEngine]**

- **Construção (`FUN_012528f0`):** para cada trilha T, o motor cria duas listas de índices de trilha:
  - **entradas** (`T+0x120`) = `[T]` e, depois, cada trilha S que tem rota S→T, **na ordem do iterador de
    trilhas**;
  - **saídas** (`T+0x128`) = `[T]` e, depois, os destinos do vetor de envios de T (d crescente, §1.1).
- **Ordem do iterador (`FUN_012c68f0`, `FUN_012c6950`, `FUN_012c6970`, `FUN_012c6930`):** i = 0, 1, …, n − 1, com o
  último valor trocado por **501** (a trilha "Current"). Logo, as fontes aparecem em **ordem crescente de número
  de trilha, com a 501 por último**.
- **Leitura pelo plugin (`TFruityHost` slot 36, `FUN_0132baa0`):** recebe o *wrapper* do plugin e um índice k; se
  `k < tamanho(entradas da trilha do plugin)`, devolve o *buffer* da trilha `entradas[k]` (a cópia privada do
  consumidor, se houver, senão o *buffer* principal da fonte `+0x34`, via `FUN_00982ac0`) e um byte de estado da
  fonte (`+0x150`); senão devolve nulo.
- **Despachante do host** (trechos lidos na desmontagem):
  - `0x1320f2e`: para plugin de slot do mixer, índice 0 → devolve o tamanho da lista de **entradas**; índice ≠ 0 →
    tamanho da lista de **saídas**. Para os demais (canais): 0, ou "nº de trilhas − 2".
  - `0x1321053`: para `1 ≤ k ≤ tamanho − 1`, chama `FUN_01317270` com `entradas[k]` (nome da trilha que o seletor
    mostra — coerente com o manual: "Available Sidechain tracks are labeled by their Track name").
  - `0x1321c91`: cria a ação `TRegisterSideIOAction` (`FUN_01316a50`) só se `0 < k < tamanho`, e ela registra o
    consumidor na tabela lateral da trilha `entradas[k]`. `FUN_012bdb60` faz o mesmo ao enumerar as entradas que o
    plugin declara (consulta `0x37`), também exigindo k > 0.
- **Resultado:** **índice 0 = a própria trilha (entrada normal); índice k ≥ 1 = a k-ésima trilha que roteia para a
  trilha do plugin, em ordem crescente de número (501 por último)**, contando rotas com qualquer nível. Isso também
  fecha a questão da rodada 18 §5: no envio por "barramento k" dos plugins de slot, `lista_da_trilha[k]` é a lista
  de **saídas** = `[própria, destinos em ordem crescente]`.
- **Quando o áudio lateral é copiado (`0x12772b2`):** depois dos envios (`FUN_01277980`), `FUN_01277bd0` copia o
  *buffer* principal da trilha-fonte (`+0x34`, o mesmo que os envios usam) para o *buffer* de cada consumidor
  registrado, passando pela linha de atraso de PDC da entrada (método `+0x30`; compensação "plugin.align −
  fonte.outLat" da rodada 2 §1.4); depois vêm as saídas extras (`FUN_012777e0`). Como o envio sai do mesmo
  *buffer*, o sinal lateral está no mesmo ponto da cadeia que os envios (pós-fader, rodada 2 §2.1).
- **Ordem de processamento:** `FUN_012521f0` calcula para cada trilha uma profundidade (`+0xfc` = 1 + maior
  profundidade das trilhas que roteiam para ela) e monta uma lista global ordenada por profundidade (sem a 501).
  Que essa lista é a ordem em que as trilhas são processadas — e, portanto, que a fonte lateral é processada antes
  do consumidor no mesmo bloco — é **inferência** pelo uso; não foi lido o laço que a consome.

### 1.3 Fruity Limiter **[DECOMP + DOC + DADOS]**

- **Campo do estado:** o leitor (`FUN_006d3760`) zera `P+0x1C0` e lê 0xA4 bytes do *blob* para `P+0x120`
  (v ≥ 6; v5 lê 0x9D). O inteiro no offset **0xA4 do `0xD5`** (rodada 7 §2.1) cai em **`P+0x1C0`** = índice k da
  entrada lateral; em v5 ele fica 0.
- **Registro no host (`FUN_006ce570`, chamado no fim da carga):** pergunta ao host a contagem (opcode **0x32** do
  despachante), limita k a `[0, contagem − 1]`, desregistra o índice anterior e registra o novo (se ≠ 0). O número
  do opcode de registro (lido de um campo do gerente, `+0x1c`) **não foi identificado**.
- **Render (`FUN_006d5d60`, slot 7):**
  - o detector principal (`P+0x1C4`) roda sempre sobre a entrada da própria trilha;
  - a seção de compressão só roda com **`P+0x249` ligado**, e `ProcessParam` (`FUN_006d4490`, parâmetro 8 = Comp
    threshold) liga esse *flag* quando **threshold < 1000** (abaixo de 0 dB); com 1000 o limiar interno é fixado e a
    seção fica desligada;
  - com a seção ligada, o detector da compressão (`P+0x1CC`) recebe o ***buffer* lateral** se `k > 0` e o host
    devolver um *buffer* (slot 36 com o índice k); senão recebe a entrada da própria trilha;
  - o ganho de compressão resultante é combinado por amostra com o limitador: a decisão do teto (`P+0x208`) usa o
    nível do detector principal × o ganho de compressão, e o *gate* compara o nível do detector principal com
    `P+0x214`. Ou seja: **o *sidechain* só controla o compressor; limitador e *noise gate* continuam ouvindo a
    entrada da trilha.**
- **[DOC `…/html/plugins/Fruity%20Limiter.htm`]** O seletor SIDECHAIN fica na aba COMP; "Fruity Limiter's
  compression envelope will now be controlled by the sidechain Mixer track"; o máximo do THRESH (0 dB) desliga o
  compressor; "Sidechain options will not be available until one or more sidechain tracks are set in the Mixer".
- **[DADOS]** 343 instâncias com v ≥ 6; **175 com k ≠ 0** (k = 1: 146; 2: 16; 3: 10; 4: 1; 12: 2). Dessas,
  **159 têm Comp threshold < 1000** (o *sidechain* age) e 16 têm 1000 (o *sidechain* não tem efeito). Uma
  instância ("RawFL", trilha 31 "Pianos", k = 1) está numa trilha sem nenhuma fonte → o host devolve nulo e o
  detector usa a própria entrada.

### 1.4 Fruity Compressor **[DECOMP + DOC + DADOS]**

- **Campo do estado (`FUN_006bd9a0`):** só na **v3**, o `i32` do offset 0x20 (rodada 7 §5) vai para **`P+0x5D8`**;
  em v1/v2 fica 0. O mesmo gerente de entrada lateral (`FUN_006bb680`) registra o índice no host.
- **Render (`FUN_006bee30`, slot 7):** se `k > 0` e o host devolver o *buffer*, o detector usa **L e R do sinal
  lateral diretamente** (+1e−30 contra denormais) no lugar do sinal da própria trilha; o sinal próprio, quando é ele
  o usado, passa antes por um filtro de 1ª ordem (metade da diferença entre a entrada e um passa-tudo de 1ª ordem,
  coeficiente `P+0x154`, valor **não identificado**). Em seguida: módulo, máximo entre L e R, domínio log,
  *attack/release* e curva de ganho, aplicada ao áudio da própria trilha.
- **[DOC `…/html/plugins/Fruity%20Compressor.htm` (seção "Sidechain Input")]** "Fruity Compressor's compression
  envelope will now be controlled by the sidechain Mixer track".
- **[DADOS]** 88 instâncias (51 v2, 35 v3, 2 v1); **nenhuma com *sidechain*** (as 35 v3 têm o campo = 0).

### 1.5 Maximus, Fruity Peak Controller, Fruity Vocoder, Fruity Multiband Compressor **[DECOMP (busca) + DOC]**

- **Busca no código:** procurei, em cada DLL, a sequência "carrega o ponteiro do host em `+0xBC` e chama o método
  `+0x120`" (a forma exata da chamada no Limiter/Compressor/Vocodex). Encontrada no render do Limiter (`0x6d5f17`),
  do Compressor (`0x6bef82`) e duas vezes no do Vocodex (`0x85667d`, `0x856729`); nas demais DLLs só aparece uma
  ocorrência na mesma rotina de biblioteca comum a todas (`0x57xxxx`). **Não foi encontrada no Maximus, no Peak
  Controller, no Fruity Vocoder nem no Multiband Compressor.** O render do Maximus (`FUN_00844380`) decompilado
  também não chama o host para ler entrada. Isso é uma busca por padrão: "não lê entrada lateral" é a conclusão
  dela, não uma prova exaustiva.
- **Maximus [DOC `…/html/plugins/Maximus_Tutorials.htm`]:** "Strictly speaking, Maximus does not have a sidechain
  input"; o "sidechain" do tutorial é a banda MID em modo *MID master* ouvindo uma faixa de frequências.
- **Peak Controller [DOC `…/html/plugins/Fruity%20Peak%20Controller.htm`]:** analisa a trilha onde está ("Load the
  Peak Controller on the Mixer Track that has the audio signal"); o efeito de *sidechain* vem de ligar a saída de
  controle a parâmetros de outras trilhas (rodada 14 §4), e o manual remete ao Limiter para *sidechain* de áudio.
  Demos: 33 instâncias em 26 arquivos **[DADOS]**.
- **Fruity Vocoder:** sem entrada lateral (L = modulador, R = portadora da própria entrada — rodada 20 §4.1).
  1 instância.

### 1.6 Vocodex **[DECOMP rodada 20 + §1.2 + DADOS]**

- ModIn/CarIn (bloco `+0x170`, rodada 20 §4.2.1) são exatamente o índice k do §1.2 (o render chama o slot 36 do
  host com esse valor; 0 = própria entrada; CarIn < 0 = sintetizador interno). A regra "n-ésima trilha que roteia
  para o Vocodex, em ordem de índice", que na rodada 20 era só inferência dos dados, **agora está confirmada no
  host** (§1.2).
- **[DADOS]** 12 instâncias no mixer; 9 entradas laterais ≥ 1 em 6 arquivos; 8 resolvem para a trilha esperada
  pelo nome ("Mod"/"Car", "Modulador"/"Carrier", "Vocals"…); a do teste `Render tests/Vocodex.flp` (ModIn = 1) está
  numa trilha sem fonte → cai na própria entrada.

### 1.7 Uso nos 165 arquivos (para priorizar) **[DADOS `tmp/r22/py/scuse.py`, `scsum.txt`]**

**184 consumidores** (175 Limiter + 9 Vocodex) em **40 arquivos** (26 dos 46 `.flp` de `Demo projects/`, 13 templates
`Advanced`, 1 *render test*). Resolvendo k pela regra do §1.2, a fonte encontrada tem nome coerente em todos os
casos com fonte (ex.: k = 12 → "kick"/"Kick" nos dois projetos do SeamlessR; k = 4 → "7 Sidechain" no "Bien Duro";
templates `Advanced/*` → trilha 124 "SideChain"). Nível da rota fonte→consumidor: **0 em 143**, > 0 em 3, sem
registro em 36, sem fonte em 2.

| Arquivo | Consumidores (plugin: k) | Fonte(s) resolvida(s) |
|---|---|---|
| Demo songs: "Square a Saw – Lightning" | 22 Limiter k1 | 2 "kick" |
| Visual: "Treben – RawFL" | 21 Limiter k1 + Vocodex car k2 | 2 "GHOSTKICK", 16 "BASS 6" (1 sem fonte) |
| Visual: "Treben – All the same" | 22 Limiter k1 | 45 "SC" |
| Product: "Square a Saw – Oxygen" | 14 Limiter k1, 2 k2 | 1 "sidechain trigger", 17 "tevlo vocals" |
| "Eric Kauffmann – Exoplanet" | 12 Limiter k1 | 1 "GHOST" |
| "Electroconductor – SH-1 Rework" | 11 Limiter k1 | 1 "SC", 10 "Snare Bus" |
| "Astes – Bien Duro" | 6 k1, 1 k2, 1 k4 | 81 "7 Sidechain", 1/2 "1.1 Main" |
| "LollieVox – Madonna" | 6 k1, 1 k2 | 8 "SC" |
| "Ookay – Thief" e "NewStuff" | 4 k1, 1 k2 (cada) | 1 "KICK", 24 "Main Vox" |
| "SeamlessR – Bass Antics" / "JuiceBass – Menagerie" | Limiter k1/k12 + Vocodex mod/car | "Mod"/"Car", "Modulator"/"Carrier", "kick"/"Kick" |
| "Olbaid – Harmless Essentials", "Vocodex demo" | Vocodex | 72/73, 1 "Vocals" |
| Outros 12 demos (1–3 cada) | Limiter k1 | "Kick", "Deep Kick", "lead vox", "Chorus – Lead", "Drum Sidechain"… |
| Templates `Advanced/Electronic/*` (9) e `Instrumental/*` (4) | Limiter k1/k2/k3 | 124 "SideChain" (Jazz: 15 "Brass") |
| `System/Render tests/Vocodex.flp` | Vocodex mod k1 | sem fonte (cai na própria entrada) |

Fruity Compressor, Maximus, Peak Controller e o *wrapper*: **0 usos de entrada lateral**. O padrão dominante é
"Limiter com compressor (threshold < 0 dB) numa trilha, ouvindo uma trilha de *kick*/'SC' ligada com nível 0".

---

## 2. Fruity Wrapper — numeração dos parâmetros (VST2/VST3/CLAP) e estado CLAP **[DECOMP + DADOS]**

**O que o LYNX supõe:** `FlpImport.cpp` traduz `controlID` com `p ≥ 0x8000` para "parâmetro `p − 0x8000` do plugin";
`PluginHost::adoptFlPlugins`/`convertState` só leva estado de VST3 (registros 3/2 do §3.2 da rodada 5) e deixa
VST2→VST3 e CLAP no padrão. A rodada 5 deixou em aberto o que é `k` para VST3/CLAP e o formato do estado CLAP.

### 2.1 Entrada única: `ProcessParam` do *wrapper* (`FUN_00870830`, slot 6) **[DECOMP]**

- O FL fala com o *wrapper* como com qualquer plugin nativo: `ProcessParam(índice, valor, flags)`. O *wrapper*
  separa o índice assim:
  - **0x1000…0x107F**: vira mensagem MIDI de CC (`0xB0`, número = índice − 0x1000, valor 0…127);
  - **0x1080…0x108F**: vira mensagem de status `0xD0 + (índice − 0x1080)` (*channel pressure* do canal MIDI
    correspondente) com o valor;
  - **0 ≤ índice < `P+0x70C`** (contagem de parâmetros do plugin hospedado): parâmetro do plugin. Com o *flag* 0x20
    (caminho da automação, rodada 2 §2.7) o inteiro é dividido por **2³⁰** (constante `0x870aa0` = 1 073 741 824)
    → valor normalizado 0…1. Só escreve se o método de *flags* do plugin (VMT `+0x208`) disser "automatizável" (bit
    0) e não "só leitura" (bit 1); então chama o método de escrita (VMT `+0x1F0`) e guarda o valor numa tabela
    própria indexada pelo mesmo índice.
- Portanto **o `k` do `controlID` (`p − 0x8000`) é o índice que o *wrapper* recebe**, e o significado de k é dado
  pelo método `+0x1F0`/`+0x208` de cada formato (tabela de VMT: VST2 `0x7A77C8`, VST3 `0x841B90`, CLAP
  `0x859718`). Que a automação do `0xE3`/`0xE1` chega ao `ProcessParam` com índice = `p − 0x8000` é o caminho do
  *setter* universal da rodada 2; a ligação exata `p → índice` no FLEngine **não foi relida nesta rodada**.
  Se o FL também endereça os índices 0x1000…0x108F por `controlID` (`p = 0x9000…`) **não foi confirmado**.

### 2.2 VST2 (`TVSTPlugin`) **[DECOMP + DADOS]**

- Contagem: `P+0x70C = AEffect.numParams` (`FUN_007ad000`).
- Escrita (`FUN_007af910`, VMT `+0x1F0`): chama `AEffect.setParameter(índice, valor)` com o **mesmo índice**;
  *flags* constantes = 5 (todos automatizáveis). Nomes e textos vêm de `effGetParamName` (opcode 8, `FUN_007ac6e0`)
  e `effGetParamLabel` (opcode 6, `FUN_007ac650`), também pelo mesmo índice.
- **k = índice VST2 do parâmetro (0…numParams − 1).** [DADOS] Os 2 links de automação para *wrapper* nos 165
  arquivos ("JuiceBass – Menagerie": Fruity Fast LP k = 0, numParams = 2; Fruity Chorus k = 1, numParams = 12) estão
  dentro da faixa; os dados não têm como distinguir mais que isso.

### 2.3 VST3 (`TVST3Plugin`): k é um "slot" persistente; o mapa está no registro 4 **[DECOMP]**

- Lista de parâmetros (`FUN_0084e0d0`): `IEditController::getParameterCount` (VMT `+0x40`) e
  `getParameterInfo(i)` (`+0x48`) para i = 0…n − 1, guardados em `P+0x880` (registros de 0x318 B = tamanho de
  `Steinberg::Vst::ParameterInfo`; `id` no início, `flags` em `+0x314`), contagem em `P+0x87C`.
- Dois dicionários: `P+0x888` **ParamID → slot** e `P+0x890` **slot → posição na lista**; `P+0x70C` = nº de slots.
- **Atribuição (`FUN_00850dc0`):** para cada par (ParamID, slot) já existente, procura o ParamID na lista atual e
  liga o slot à posição; depois, **cada ParamID da lista que ainda não tem slot ganha o próximo slot livre
  (`P+0x70C`, incrementado), na ordem de `getParameterInfo`**. Numa instância nova (dicionários vazios, `P+0x70C` =
  0 — zerado em `FUN_00744640`, que pelo uso é a inicialização da classe base: inferência) isso dá slot = índice de `getParameterInfo`.
- **Gravação (registro 4 de `FUN_0084e5e0`):** `u32 N` = nº de slots + para cada slot o **ParamID** (−1 se o slot
  não tem parâmetro). **Carga:** recria os slots com esses ParamIDs e chama a atribuição acima: os slots salvos
  mantêm o ParamID mesmo que o plugin tenha reordenado/acrescentado parâmetros; ParamIDs novos vão para o fim; um
  ParamID que sumiu deixa o slot vazio (escrita ignorada).
- **Escrita (`FUN_00850390`, `+0x1F0`):** slot → posição → ParamID → `FUN_0084fad0` (envio do valor normalizado ao
  plugin). ***Flags* (`FUN_0084a2c0`, `+0x208`):** `kCanAutomate` (1) → bit 0, `kIsReadOnly` (2) → bit 1,
  `kIsBypass` (0x10000) → bit 3.
- **k = slot**; o ParamID real é `registro4[k]` (fecha a pendência da rodada 5 §4). Sem dados VST3 nos `.flp` →
  só **[DECOMP]**.

### 2.4 CLAP (`TCLAPPlugin`): k é o índice em `clap_plugin_params.get_info`, sem mapa salvo **[DECOMP]**

- Extensões obtidas por `get_extension` (`FUN_0085dd20`, `FUN_0085ef50`): `"clap.state"` → `P+0x900`,
  `"clap.params"` → `P+0x8E8` (entre outras: gui, note-ports, audio-ports, latency, render, thread-pool,
  timer-support, track-info, project-location, context-menu).
- Lista (`FUN_0085ee20`): `params.count()` e `params.get_info(i)` para i = 0…n − 1 em `P+0x950` (registros de
  0x528 B = `clap_param_info`: `id`, `flags`, `cookie`, nome, módulo, mín/máx/padrão); `P+0x70C` = n; também um
  índice `id → i` (`P+0x9A8`) para mudanças vindas do plugin.
- **Escrita (`FUN_0085f560`, `+0x1F0`):** monta um evento de **0x38 B tipo 5** (`CLAP_EVENT_PARAM_VALUE`) com
  `param_id = info[k].id`, o *cookie* e o valor **`mín + v·(máx − mín)`** (`FUN_00859330`); leitura pelo caminho
  inverso `(x − mín)/(máx − mín)` (`FUN_00859300` sobre `params.get_value`).
- ***Flags* (`FUN_0085d6b0`):** `CLAP_PARAM_IS_AUTOMATABLE` (0x20) → bit 0, `IS_READONLY` (0x8) → bit 1,
  `IS_BYPASS` (0x10) → bit 3.
- **k = posição do parâmetro na ordem de `get_info` no momento da carga.** O estado CLAP **não** guarda mapa de
  ParamIDs (§2.5): se o plugin mudar a ordem entre versões, a automação gravada aponta para outro parâmetro (isso é
  consequência direta do código, não foi testado). Comportamento após `params.rescan` **não confirmado**.

### 2.5 Estado CLAP (registro `0x35`, `FUN_0085f900`) **[DECOMP]**

- **Gravação:** chama `clap_plugin_state.save(plugin, ostream)` (`P+0x900`[0], com o `ostream` cuja função de escrita
  `FUN_0085f8c0` acumula num *buffer* em memória), escreve **`u32 0`** e em seguida **todos os bytes** gerados.
- **Carga:** lê e **ignora** o `u32` inicial, lê o resto do registro até o fim (`tamanho − posição`) e chama
  `clap_plugin_state.load(plugin, istream)` (`P+0x900`[1], leitura `FUN_0085f8e0`) sobre esses bytes.
- Formato: **`u32 0` + estado CLAP cru** (sem sub-registros, sem tamanho próprio — o tamanho é o do registro
  `0x35`). Fecha a pendência da rodada 5 §3.3. Sem dados CLAP nos `.flp` → só **[DECOMP]**.

### 2.6 O que os dados permitem verificar **[DADOS]**

- Os 36 *wrappers* dos 165 arquivos são todos VST2 (rodada 5); só 2 links de automação apontam para eles (§2.2).
  VST3 e CLAP: nenhuma instância, nenhuma automação — §2.3–§2.5 ficam só no código.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Marcação no `.flp` | **não há *flag***: `0xEB` só 0/1; *sidechain* = rota (`0xEB` = 1) com nível `0xE1` `p = 0x2000 + d` **= 0**; qualquer rota (com nível) também é fonte de *sidechain*; rota de nível 0 não soma áudio **[DECOMP+DOC+DADOS]**; valor assumido sem registro de nível **não confirmado** |
| 1b | Barramento visto pelo efeito | lista de entradas da trilha = `[própria, fontes em ordem crescente de nº de trilha (501 por último)]`; índice **0 = própria, k ≥ 1 = k-ésima fonte**; sinal = *buffer* da fonte no ponto dos envios, com PDC por aresta **[DECOMP]**; saídas = `[própria, destinos crescentes]` (fecha rodada 18 §5) |
| 1c | Fruity Limiter | campo `0xA4` (v ≥ 6) = k; só o **detector do compressor** usa o lateral, e só com Comp threshold < 1000; teto e *gate* usam a própria entrada **[DECOMP+DOC]**; 175 instâncias com k ≠ 0 (159 efetivas) **[DADOS]** |
| 1d | Fruity Compressor | campo `0x20` (v3) = k; detector = L/R do lateral sem o filtro de entrada **[DECOMP+DOC]**; 0 usos **[DADOS]** |
| 1e | Vocodex | ModIn/CarIn = k do 1b (confirmado no host); 9 entradas em 6 arquivos **[DECOMP+DADOS]** |
| 1f | Maximus, Peak Controller, Fruity Vocoder, Multiband | não leem entrada lateral (busca por padrão + DOC) |
| 1g | Prioridade | 184 consumidores em 40 arquivos; padrão "Limiter-compressor ducking por kick/SC ligado com nível 0" |
| 2a | Índice de parâmetro no *wrapper* | `ProcessParam(k)`: 0…n−1 = parâmetro; 0x1000…0x107F = CC MIDI; 0x1080…0x108F = *channel pressure*; automação normalizada por 2³⁰ **[DECOMP]** |
| 2b | VST2 | k = índice `setParameter` **[DECOMP+DADOS]** |
| 2c | VST3 | k = **slot persistente**; ParamID = registro 4 [k]; instância nova: ordem de `getParameterInfo`; novos ParamIDs no fim **[DECOMP]** |
| 2d | CLAP | k = posição em `get_info`; valor `mín + v·(máx − mín)`; evento `PARAM_VALUE`; sem mapa salvo **[DECOMP]** |
| 2e | Estado CLAP | `u32 0` (ignorado na carga) + bytes de `clap_plugin_state.save` até o fim do registro **[DECOMP]** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Importação das rotas:**
   - Não descartar as rotas de nível 0 como "inúteis": elas são as ligações de *sidechain*. Guardar, por trilha, a
     lista de **entradas** `[própria, fontes em ordem crescente, 501 por último]` construída a partir de **todas** as
     rotas do `0xEB` (com qualquer nível). Para o áudio continua valendo: nível 0 não soma nada no destino.
   - Ao interpretar o "barramento k" dos plugins de slot (Patcher, Frequency Splitter, Stereo Shaper), usar a lista
     de **saídas** `[própria, destinos crescentes]`.
2. **Fruity Limiter com *sidechain* (o caso que importa: 159 instâncias efetivas):** se o LYNX tiver entrada lateral
   num compressor, alimentar **só o detector da seção de compressão** com o *buffer* pós-fader da trilha
   `entradas[k]` (k = `i32` no offset 0xA4 do estado, v ≥ 6), apenas quando Comp threshold < 1000; teto do limitador
   e *gate* continuam com a própria entrada. Sem entrada lateral no LYNX, registrar no log "Limiter com *sidechain*
   de <nome da trilha-fonte>" para o usuário saber que o *ducking* foi perdido.
3. **Fruity Compressor:** ler o campo 0x20 só em v3; mesma resolução de k; nos demos é sempre 0 (baixa prioridade).
4. **Vocodex:** usar a mesma lista de entradas para ModIn/CarIn (a regra deixou de ser só empírica).
5. **PDC:** a aresta fonte→consumidor lateral entra na compensação como na rodada 2 §1.4 (atraso
   `plugin.align − fonte.outLat`), e a fonte precisa ser processada antes do consumidor (ver a inferência do §1.2).
6. **Automação de plugins de terceiros:**
   - VST2: `k` = índice do parâmetro (o JUCE expõe os parâmetros VST2 nessa mesma ordem — referência JUCE, código
     aberto).
   - VST3: ler o registro 4 do sub-contêiner VST3 (`u32 N` + N × `i32` ParamID) e traduzir `k → ParamID`; depois
     procurar o parâmetro do JUCE com esse ID. Sem registro 4, `k` = índice de `getParameterInfo`.
   - CLAP: `k` = índice de `get_info`; valor normalizado do FL vira `mín + v·(máx − mín)` no domínio CLAP.
   - Índices 0x1000…0x108F (se aparecerem) são MIDI, não parâmetros.
7. **Estado CLAP em `convertState`:** pular os 4 primeiros bytes do registro `0x35` e entregar o resto, sem
   alteração, ao `clap_plugin_state.load` do host CLAP do LYNX.

---

## Índice de evidências

| Evidência | O que mostra | Marca |
|---|---|---|
| 165 `.flp`: valores dos bytes do `0xEB` (`tmp/r22/py/ebvals.py`) | só 0/1 | DADOS |
| `0xE1` `p = 0x2000 + d` × rotas (`sc.py`, `sc2.py`) | 11 857 rotas nível 0, 197 > 0, 32 142 sem registro | DADOS |
| Manual "Internal Mixer Track Routing & Sidechaining" | *sidechain* = envio com volume 0; envio cria também *sidechain* | DOC `…/html/mixer.htm` |
| FLEngine `FUN_012521f0` | vetor de envios a partir do byte de rota, sem olhar nível; profundidade e lista ordenada | DECOMP |
| FLEngine `FUN_01277980` | envio com ganho e rampa 0 não é misturado | DECOMP |
| FLEngine `FUN_012528f0`; iterador `FUN_012c68f0`/`6950`/`6970`/`6930` | listas de entradas/saídas; ordem crescente com 501 por último | DECOMP |
| FLEngine `TFruityHost` VMT `0x13141b8` slot 36 `FUN_0132baa0`, slot 37 `FUN_0132bb50`; `FUN_00982ac0` | leitura da entrada k; *buffer* privado do consumidor | DECOMP |
| FLEngine despachante `0x1320f2e`, `0x1321053`, `0x1321c91`; `FUN_01316a50`; `FUN_012bdb60` | contagem, nome e registro das entradas (k > 0) | DECOMP |
| FLEngine `0x12772b2`, `FUN_01277bd0` | cópia lateral após os envios, com linha de PDC | DECOMP |
| Limiter `FUN_006d3760`, `FUN_006ce570`, `FUN_006d4490` (parâmetro 8 → `P+0x249`), `FUN_006d5d60` (`0x6d5ee3…0x6d5f2d`) | campo → `P+0x1C0`; opcode 0x32; detector do compressor lê o lateral | DECOMP |
| Manual Fruity Limiter | aba COMP, envelope de compressão controlado pelo *sidechain*, 0 dB desliga | DOC `…/html/plugins/Fruity%20Limiter.htm` |
| Compressor `FUN_006bd9a0`, `FUN_006bb680`, `FUN_006bee30` (constantes `0x6bf8c8` = 1e−30, `0x6bf8d0` = 0,5) | campo v3 → `P+0x5D8`; detector usa o lateral | DECOMP |
| Manual Fruity Compressor | seção "Sidechain Input" | DOC `…/html/plugins/Fruity%20Compressor.htm` |
| Busca "host `+0xBC` → chamada `+0x120`" (`hostcall.py`) em 7 DLLs; Maximus `FUN_00844380` | só Limiter, Compressor e Vocodex leem entrada lateral | DECOMP (busca) |
| Manuais Maximus (tutorial) e Peak Controller | Maximus sem entrada de *sidechain*; Peak Controller analisa a própria trilha | DOC `…/html/plugins/Maximus_Tutorials.htm`, `…/html/plugins/Fruity%20Peak%20Controller.htm` |
| Demos: Limiter `0xA4`, Compressor `0x20`, Vocodex ModIn/CarIn resolvidos pela regra (`scuse.py`, `scsum.txt`) | 184 consumidores, 40 arquivos, nomes coerentes | DADOS |
| Wrapper VMTs `0x8685d8`/`0x868840` slot 6 `FUN_00870830` (constante `0x870aa0` = 2³⁰) | faixas de índice, normalização, *flags* | DECOMP |
| Wrapper VST2 `FUN_007ad000`, `FUN_007af910`, `FUN_007ac650`, `FUN_007ac6e0`, `0x744dec` = 5 | k = índice `setParameter` | DECOMP |
| Wrapper VST3 `FUN_0084e0d0`, `FUN_00850dc0`, `FUN_00850390`, `FUN_0084a2c0`, `FUN_0084e5e0` (registro 4), dicionários `FUN_00857bb0`/`FUN_00858200`/`FUN_00857ff0`/`FUN_00858080` | slots persistentes, mapa ParamID | DECOMP |
| Wrapper CLAP `FUN_0085dd20`, `FUN_0085ef50` (strings `clap.*`), `FUN_0085ee20`, `FUN_0085f560`, `FUN_0085d6b0`, `FUN_0085d5c0`, `FUN_00859330`/`FUN_00859300` | lista por `get_info`, evento tipo 5, escala mín/máx | DECOMP |
| Wrapper CLAP `FUN_0085f900`, `FUN_0085f8c0`, `FUN_0085f8e0` | estado `u32 0` + bytes de `state.save` | DECOMP |
| Demos: `0xE3`/`0xE1` para *wrappers* (`wrapauto.py`) | 2 links, VST2, k < numParams | DADOS |

### Questões em aberto

- Valor que o FL assume para o nível de uma rota sem registro no `0xE1` (36 das fontes de *sidechain* dos demos
  estão nesse caso; para o *sidechain* não importa, para o áudio sim).
- Confirmar no laço de processamento que a lista ordenada por profundidade (`FUN_012521f0`) é a ordem de render e
  como o FL trata ciclos/fontes processadas depois do consumidor.
- Número do opcode com que o plugin registra a entrada lateral (campo `+0x1c` do gerente) e a ligação opcode ↔
  tratador no despachante do host (só os tratadores foram lidos).
- Fruity Compressor: valor do coeficiente `P+0x154` do filtro do detector.
- *Wrapper*: ligação exata `controlID p → índice do ProcessParam` relida no FLEngine; se `p = 0x9000…0x908F`
  endereça os CC/*channel pressure*; comportamento CLAP após `params.rescan`; validar §2.3–§2.5 com um projeto
  real com VST3 e CLAP (ex.: Surge XT), conferindo o registro 4 e o `u32 0` do CLAP.
- Busca de entrada lateral por padrão de instruções: uma chamada indireta montada de outra forma escaparia dela
  (Maximus, Peak Controller, Multiband não foram decompilados por inteiro).
