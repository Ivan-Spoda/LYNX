# Análise comportamental do FL Studio 2026: rodada 14 (REA) — Patcher, Gross Beat e os efeitos/controladores que o import ainda descarta

> **Natureza deste documento.** Continuação de `REA_rodada7.md` (estado dos efeitos nativos do mixer) e
> `REA_rodada12.md`. Engenharia reversa *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo
> da Image-Line foi copiado: o que está aqui são descrições, layouts de dados e fórmulas escritas com
> palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a evidência.

- **Alvos** (cópias locais em `tmp/r14/bin/`, originais em `Plugins/Fruity/Effects/<nome>/` e
  `Plugins/Fruity/Generators/<nome>/`):

  | Plugin | DLL (SHA-256) | Linguagem | Ponto de entrada do estado |
  |---|---|---|---|
  | Patcher | `cb4212c4…c64b` | Delphi | escrita `FUN_00955150`, leitura `FUN_009513d0`, nó `FUN_00952a70` |
  | Gross Beat | `9025d938…8f1a` | Delphi (`TTestPlug`, VMT `0x83f0f0`) | `SaveRestoreState` `FUN_0083f890` (VMT+3) |
  | Pitcher | `965db573…a73d` | C++ | escrita `FUN_18001d6e0`, leitura `FUN_18001d790` |
  | Effector | `2f538ec9…72e5` (+ `engine_x64.dll`) | Delphi + motor C++ | só DFM + dados |
  | Frequency Splitter | `e2505dc6…e27c` (+ `engine_x64.dll`) | Delphi + motor C++ | só DFM + dados |
  | Vocodex | `43d10692…4bb0` | Delphi | só DFM |
  | Fruity Peak / Envelope / Formula / X-Y Controller | `e73ec8da…2d27` / `e60f7462…c763` / `302779c8…76a7` / `c78e1ff2…c19b` | Delphi | DFM + dados |

- **Ferramentas:** Ghidra 12.1.4 *headless* em projetos próprios (`/tmp/r14gh_<dll>`), scripts em
  `tmp/r14/gh/scripts/` (`Decomp`, `DecompGrep`, `Xrefs`, `ScalarFind` — este último acha as funções que usam
  as constantes de *chunk*). DFM lidos por um parser próprio de `TPF0` (`tmp/r14/tools/dfm.py`; o `Tag` de cada
  controle é o índice do parâmetro). VMTs Delphi listadas por `tmp/r14/tools/vmts.py` (nome da classe em
  VMT−0x88).
- **Coleta:** `tmp/r14/collect14.py` → `tmp/r14/blobs14.pkl` = `(MIX, CH, FST)`: slots do mixer
  (`0xC9` → `0xD5` → `0x62`, depois do 1º `0xEC`), geradores de canal e presets `.fst`. Decodificadores de
  apoio: `tmp/r14/pdec.py`, `graph.py`, `flat2.py` (Patcher), `gbp.py` (Gross Beat).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/disassembly da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

### Contagem nos dados (prioridade) **[DADOS]**

Slots de efeito do mixer em `Data/Demo projects/**.flp` (entre parênteses: todos os `.flp` de `Data`,
incluindo `Templates/` e `System/`).

| Plugin | Slots nos demos | (todos) | Arquivos demo | `.fst` | Tamanho do `0xD5` |
|---|---|---|---|---|---|
| Pitcher | 39 | (61) | 7 | 4 | 160 B (`SSLF` + `1RTP` + floats) |
| Effector | 26 | (27) | 11 | 4 | 316 B (`SSLF` + `10XF` + floats) |
| Gross Beat | 30 | (32) | 15 | 13 | 7–44 KB (72 envelopes) |
| Patcher | 24 + 3 canais | (30 + 4) | 8 | **329** | 2 KB…450 KB (chunks) |
| Fruity Peak Controller | 20 | (33) | 13 | 7 | 42 B |
| Frequency Splitter | 17 | (17) | 3 | 13 | 113 B |
| Vocodex | 10 | (12) | 8 | 25 | ~1,2 KB |
| Control Surface / X-Y / Formula Controller | 3 / 1 / 2 | (17 / 19 / 2) | — | — | — |
| Fruity Envelope Controller | — | — | 4 canais | 26 | ~2–6 KB |

Além disso, **75 links de controlador** (eventos `0xE3` cuja origem é a saída de um plugin, §7.1) em 18
demos — hoje todos descartados pelo import.

---

## 1. Patcher **[DECOMP+DADOS]**

O Patcher é um grafo de plugins dentro de um slot (efeito) ou de um canal (gerador). O `0xD5` dele é uma
**sequência de chunks com tamanho de 64 bits**, e cada plugin contido guarda **o mesmo blob de estado que
teria como `0xD5` num slot do mixer** — ou seja, os decodificadores da rodada 7 (PEQ2, Limiter, Reeverb 2,
Delay 3…) servem sem mudança.

### 1.1 Envelope e versões **[DECOMP `FUN_007c97a0`/`FUN_007c9a50`/`FUN_007c9a70` + DADOS]**

```
u32 versão                        4 (atual, strings UTF-16) | 3 (strings ANSI) | 1 (templates "Patcherize")
repetir até o fim:
  u32 id  ·  u64 tamanho  ·  tamanho bytes de dados
```

- **id = 0 é um grupo**: o conteúdo dele é, de novo, uma sequência de chunks `id/tamanho/dados`. O escritor
  abre o grupo gravando um cabeçalho com tamanho 0 e, ao fechar, volta e escreve o tamanho real
  (`FUN_007c9a70`). Cada nó, pino, conexão e posição é um grupo.
- O chunk `0x3E9` (1001) com tamanho 0 marca o **fim do estado**.
- Strings: na versão 4 são UTF-16LE sem terminador; na versão 3 são **ANSI de 8 bits** (lidas como UTF-16
  viram caracteres CJK — foi o que apareceu nos 13 presets antigos). Detectar pela versão. **[DADOS]**
- Nos 363 blobs (34 de `.flp` + 329 `.fst`) o parser recursivo consome tudo sem sobra **[DADOS]**.

### 1.2 Chunks de nível superior **[DECOMP `FUN_00955150` (escrita), `FUN_009513d0` (leitura)]**

| id | Tamanho | Conteúdo |
|---|---|---|
| `1000` (0x3E8) | 64 | cabeçalho: `i64` próximo id livre (ids de nós/portas são alocados daqui), `i32` contador global, `u32` flags (bit 0, bit 1: opções do editor). Ignorável |
| `1002` (0x3EA) | 44 | estado do editor (nº de pinos visíveis, bits de opções, posições dos nós de E/S). Ignorável |
| `1003` (0x3EB) | 12 | só em v3: `i32` tipo (0…2) + posição de um nó de E/S (antigo). Ignorável |
| `1004` (0x3EC) | ~324 | layout da superfície de controle (`FUN_008256d0`). Ignorável |
| grupo `5000…5001` | var | **um nó (plugin)**, §1.3 |
| `1100` (0x44C) | 32 | **parâmetro exposto do Patcher**: `u64` id da porta de controle + `i32` índice do parâmetro do Patcher. Um por parâmetro que o usuário publicou (é por aqui que a automação "do Patcher" chega a um parâmetro interno) |
| grupo `1400…1401` | var | **pino de E/S do Patcher**, §1.4 |
| grupo `1300…1301` | var | **conexão**, §1.5 |
| `1500` (0x5DC) | 40 | vista do editor (`f32` zoom = 1,0, rolagem). Ignorável |
| grupo `1600` | 24 | posição de um nó: `u64` id do nó + `f32 x`, `f32 y`. Primeiro os dois nós de E/S, depois um por plugin. Ignorável |
| `1001` (0x3E9) | 0 | fim |

O leitor também aceita `0x47E` e `0x6A5` (não vistos nos dados).

### 1.3 Nó (grupo iniciado por `5000`) **[DECOMP `FUN_00955150` linhas do laço de nós, `FUN_00952a70`]**

| id | Tamanho | Conteúdo |
|---|---|---|
| `5000` (0x1388) | 40 | cabeçalho: `u32` **tipo de plugin** (2 = efeito, 3 = gerador — 8.665 / 687 nós nos dados), `u64` **id do nó**, `u32` flags (bit 0 visto em 477 nós; bit 2 = desativado, nunca visto), `u32` **nº de portas de entrada**, `u32` **nº de portas de saída**, depois 13 B de janela/UI |
| `5004` (0x138C) | var | identificador do plugin: em v4 o nome (`"Fruity Limiter"`); em v3 o caminho da DLL relativo à pasta do FL (`Plugins\Fruity\Effects\…\….dll`). Só gravado para alguns tipos |
| `5008` (0x1390) | var | **nome interno do plugin** (o mesmo do `0xC9`) — use este para escolher o decodificador. Vem com caixa variável (`"Fruity parametric EQ 2"`): comparar sem caixa |
| `5010` (0x1392) | var | nome exibido (rótulo do usuário, ex.: `"Limiter"`) |
| `5005` (0x138D) | var | **estado do plugin** — byte a byte o mesmo formato do `0xD5` daquele plugin no mixer (§1.7). Para VSTs o nome é `"Fruity Wrapper"` e o blob é o do wrapper (`REA_rodada5.md`) |
| `5007` (0x138F) | 40 | opções do nó: `f32` (sempre 1,0 nos dados), `i32` índice de preset (−1 = nenhum), `u32` flags, 3 × `i32` (padrão `0, 0x7FFFFFFF, 60`: faixa de notas e nota raiz **[HIP]**), 2 × `u32` de UI |
| `5006` (0x138E) | var | nome do preset carregado (só quando o índice ≥ 0; 16 casos, ex.: `"Mastering\20Hz + 18k…"`) |
| `5009` (0x1391) | 28 | **uma porta** (repetido): primeiro todas as entradas, depois todas as saídas, §1.3.1 |
| `0x3F2`/`0x3F6` | var / 84 | bloco opcional de um objeto auxiliar do nó (não visto nos dados) |
| `5001` (0x1389) | 0 | fim do nó |

#### 1.3.1 Porta (`5009`, 28 B) **[DECOMP + DADOS]**

| Off | Tipo | Campo |
|---|---|---|
| 0 | u32 | **classe**: 0 = áudio, 1 = MIDI/notas, 2 = controle (parâmetro) |
| 4 | u32 | **índice**: áudio = barramento (0 = principal, 1… = sidechain na entrada / saídas extras na saída); controle de entrada = **índice do parâmetro do plugin**; controle de saída = `0x10000000 + nº da saída de controle` |
| 8 | u64 | **id da porta** (é o que as conexões referenciam) |
| 16 | u32 | flags: bit 0 = **é entrada**; bit 1 = visível/em uso; bit 2 = outra opção; bit 3 = porta de parâmetro "fixa" |
| 20 | 8 | zero |

Padrões nos dados: `(0, …, flags 3)` entrada de áudio, `(0, …, 2)` saída de áudio, `(0, i ≥ 1, 0)` saídas extras
ocultas (os efeitos Fruity têm 1 entrada + **5 saídas**: principal + 4 auxiliares), `(2, p, 11)` parâmetro p
exposto como entrada, `(2, 0x1000000k, 10)` saída de controle k de um controlador **[DADOS]**.

### 1.4 Pinos de E/S do Patcher (grupo `1400`) **[DECOMP `FUN_00952560` + DADOS]**

`1400` (28 B): `u64` id da porta · `u32` classe (0 áudio, 1 MIDI) · `u32` flags · `u8` canais (2 = estéreo) ·
`i32` barramento. Segue `1402` com o nome e `1401` (fim).

- **flags bit 0 = 1** → pino que **traz sinal de fora** (vira uma *saída* dentro do grafo): `"Audio input"`
  (barramento 0, a entrada do slot), `"7 Sidechain"`/`"Master"` (barramento ≥ 1 = sidechain vindo daquela
  trilha), `"Notes"` (notas do canal, no Patcher gerador).
- **flags bit 0 = 0** → pino que **leva sinal para fora**: `"Audio output"` (barramento 0 = saída do slot),
  `"Insert N"` (barramento ≥ 1 = saídas auxiliares do Patcher para outras trilhas), `"MIDI port N"`.
- bit 1 = barramento principal (0); bit 2 = opção de exibição.

### 1.5 Conexão (grupo `1300`, 32 B) **[DECOMP `FUN_0079ca50`, `FUN_0079b730`, `FUN_00952440`, `FUN_0079b790` + DADOS]**

| Off | Tipo | Campo |
|---|---|---|
| 0 | u64 | **porta de destino** (uma entrada de nó ou um pino de saída do Patcher) |
| 8 | u64 | **porta de origem** (uma saída de nó ou um pino de entrada do Patcher) |
| 16 | f32 | **ganho do cabo** (padrão 1,0 ao criar a ligação) |
| 20 | u32 | flags: bit 0 = cabo desligado/mudo **[HIP]**; bit 1 = o byte seguinte é válido |
| 24 | u8 | classe do cabo (0 áudio, 1 MIDI, 2 controle), igual à das portas |
| 25 | 7 | zero |

- A ordem *destino, origem* vem do construtor da ligação, que sempre põe primeiro a ponta que não é saída
  (`FUN_0079b730`), e foi conferida nas 24.361 conexões: **100 %** têm entrada/pino-de-saída na 1ª posição e
  saída/pino-de-entrada na 2ª **[DECOMP+DADOS]**.
- Classe por contagem (pela porta de destino): controle 18.405, áudio 5.026, MIDI 930 **[DADOS]**.
- **Ganho:** o campo nasce em 1,0 (`FUN_0079ef40`); nos dados 23.770 cabos = 1,0 e o resto entre 0,15 e 2,0,
  sempre em cabos de áudio que somam ramos (ex.: "Twii": Harmor direto → saída × 1,32, em paralelo com a cadeia
  Reeverb→EQ × 1,0; "Bien Duro" ins. 57: banda do splitter → saída × 0,875, distorção → saída × 0,8). O uso
  do campo como multiplicador linear de amplitude não foi localizado no motor **[HIP forte]**.
- Várias conexões chegando à mesma entrada **somam** (é como o Patcher faz *merge*) **[HIP forte; padrão
  universal nos grafos paralelos]**.

### 1.6 Reconstrução do grafo (procedimento) **[DECOMP+DADOS]**

1. Ler todos os grupos; montar `porta → (nó, classe, índice, entrada?)` a partir dos `5009` e
   `porta → (pino, entrada-de-fora?, barramento, nome)` a partir dos `1400`.
2. Para cada `1300` de **classe 0 (áudio)**: aresta `nó(origem) --ganho--> nó(destino)`, guardando o
   barramento das duas pontas. Pino com bit 0 = nó virtual `IN` (barramento 0) ou `SC<n>`; sem bit 0 = `OUT`
   (barramento 0) ou `AUX<n>`.
3. Arestas de **classe 2 (controle)**: `saída de controle k do nó A → parâmetro p do nó B` (§7.2 para mapear
   em automação).
4. Arestas de classe 1 (MIDI): só interessam ao Patcher gerador (pino `Notes` → geradores).
5. Considerar só os nós que estão num caminho `IN → … → OUT` pelo barramento 0; nós fora disso são
   controladores, analisadores (Peak Controller alimentado por um EQ) ou bloco de notas.

### 1.7 Estado dos plugins internos = estado do mixer **[DADOS]**

Comparação de tamanho + 1ª palavra dos `5005` contra os `0xD5` de slots do mixer:

| Plugin | Dentro do Patcher | No mixer |
|---|---|---|
| Fruity Parametric EQ 2 | 354 B (v7/v8), 309 B (v3) | idem |
| Fruity Limiter | 169 B (v7), 168 B (v6) | idem |
| Fruity Reeverb 2 | 66 B (`0x2711`), 58 B | idem |
| Fruity Delay 3 | 108 B (v1) | idem |
| Stereo Shaper / PanOMatic | 36 B / 24 B | idem |
| Maximus | 1375/1351/1342 B | idem |
| Gross Beat | `0x00080003` … | idem |
| Fruity Wrapper (VST) | 396–400 B (v8) | idem |

### 1.8 Topologias encontradas **[DADOS `tmp/r14/flat2.py`]**

| Origem | Série | Paralelo | Sem caminho IN→OUT |
|---|---|---|---|
| Slots do mixer (todos os `.flp`) | 4 | 26 | — |
| Canais (Patcher gerador) | — | — | 4 (geradores internos → saída) |
| `.fst` | 22 | 123 | 184 |

Padrões dos demos:

- **Série** (4): "Bien Duro" ins. 9 (Flangus → Distructor), ins. 40 (Squeeze → PEQ2 → Transient Processor →
  Fast Dist → Limiter), ins. 55 (PEQ2 → Limiter); "Bombs Away" ins. 70 (Stereo Shaper → PEQ2 → WaveShaper →
  PEQ2). Só o ins. 55 tem todos os plugins já decodificados pelo LYNX.
- **M/S com Stereo Shaper** ("Africa's Calling" 31, "Inner Days" 21, "Exoplanet" 22/101, "With You" 1):
  `IN → Stereo Shaper`, cuja **saída 0 e saída 1** (barramentos diferentes!) vão para dois PEQ2 (às vezes +
  Limiter/Reeverb 2) que somam em `OUT`. Ou dois Stereo Shapers em paralelo a partir de `IN`.
- **Multibanda com Frequency Splitter** ("With You" 18/21, "Bien Duro" 9/18/57): saídas 1/2/3 do splitter
  (as bandas) → efeitos por banda → soma; às vezes uma banda vai para o **sidechain** (barramento 1) de um
  Limiter.
- **Templates** (`Funk/Jazz/Metal/Rock` ins. 125): `IN → PanOMatic → Stereo Shaper → Limiter → Stereo Shaper
  → OUT` em paralelo com `IN → PanOMatic → OUT` (mistura seca/molhada).
- "Stroll Up" ins. 1: 69 nós (4 bandas × flanger/chorus/flangus + 28 PanOMatics); não vale achatar.

Conclusão: o **achatamento em série cobre pouco**; o caso que mais rende é *"N ramos em paralelo, cada um
uma cadeia simples, somados na saída"* — o que no LYNX vira um *submix* (§Recomendações 1).

---

## 2. Gross Beat **[DECOMP+STR+DADOS]**

Gross Beat tem **36 slots de "time travel"** e **36 slots de volume**, cada um com um envelope de um compasso
desenhado em pontos; um parâmetro escolhe o slot ativo de cada tipo. O volume é um *gate*/trêmolo
sincronizado ao andamento — exatamente o que dá para aproximar no LYNX.

### 2.1 Layout do `0xD5` (`FUN_0083f890`) **[DECOMP+DADOS]**

| Off | Tamanho | Conteúdo |
|---|---|---|
| 0 | 4 | versão: o escritor grava `0x00090003`; o leitor usa a metade alta quando o valor ≥ 0x10000 (demos: `0x00080003` → **v8**; `.fst` antigos gravam 4 ou 8 direto). Recusa ≥ 0x90004 |
| 4 | 36 (v < 2: 32) | **9 × `i32` = parâmetros 0…8** (§2.2) |
| 40 | 9 (v < 3: 8; v < 5: −1) | bytes de opções (gatilho por MIDI, *click reduction*, compensação de ataque, DC, HQ… — `OptionsMenu` do DFM) **[STR; ordem HIP]** |
| … | 36 × slot | **slots de time travel** (0…35) |
| … | 36 × slot | **slots de volume** (0…35) |
| fim | 1 (só v ≥ 9) | flag extra |

**Slot:**

| Campo | Tamanho | Conteúdo |
|---|---|---|
| nome | 1 + n | *shortstring* ANSI (`"Empty"`, `"1/2 Bt Gate"`, `"Sidechain"`…). Em v < 4, nomes `"Slot N"` são apagados |
| sync | 15 | `i32` **sincronismo de gatilho**, `i32` **sincronismo de posição** (índices do combo: 0 Off, 1 ¼ beat, 2 ½ beat, 3 1 beat, 4 2 beats, **5 4 beats** — 5 em todos os slots dos dados), 7 bytes de opções do slot (link, *key hold*, *one-shot*…) **[STR; mapeamento HIP forte]** |
| envelope | 12 + 24·n + 20 | objeto de envelope da família `TCustomMEnvelope` (§2.3) |

Validação: o parser de `tmp/r14/gbp.py` percorre os 72 slots e termina **exatamente** no fim do blob
(38.283 B no `NewStuff.flp`) **[DADOS]**.

### 2.2 Parâmetros (índice = `Tag` do DFM `TSynthEditorForm`; conversões de `FUN_00840ed0`, `FUN_00841de0`, `FUN_00841fb0`) **[DECOMP+STR]**

| i | Nome | Faixa (padrão) | Conversão |
|---|---|---|---|
| 0 | Time slot | 0…35 | slot de time travel ativo |
| 1 | **Volume slot** | 0…35 | **slot de volume ativo** |
| 2 | Time mix | 0…256 (256) | `/256` (0 = sinal original sem time travel) |
| 3 | **Volume mix** | 0…256 (256) | `/256` (mistura entre ganho 1 e o envelope) |
| 4 | Volume smoothing attack | 0…10000 (1765) | `t_ms = 501^(v/10000) − 1` (0…500 ms; padrão ≈ 2 ms) |
| 5 | Volume smoothing release | 0…10000 (5000) | `t_ms = 2·(501^(v/10000) − 1)` (0…1000 ms; padrão ≈ 43 ms) |
| 6 | Volume envelope tension | −256…256 (0) | `/256`: curvatura global aplicada ao envelope de volume |
| 7 | Hold (bar repeat) | 0/1 | congela/repete o compasso |
| 8 | (sem controle no DFM) | 0…65536 | `v/65536 − 1`, exibido ×8 (deslocamento de tempo/escala) **[HIP]** |

Nos 30 slots dos demos o *volume slot* (`p1`) é 0 ("Empty") em 15, e o resto se espalha entre 3, 5, 9, 22,
24–26 e **28 ("Sidechain")**; o *volume mix* é 256 em 22 e 0 em 4 **[DADOS]**.

### 2.3 Envelope (`TCustomMEnvelope`, mesmo formato do Sytrus/FPC/clips — rodadas 2, 9 e 11) **[DADOS + DECOMP das rodadas anteriores]**

```
i32 tipo   (3 = "mapa": curva x→y desenhada; 1 = envelope ADSR; 2 = LFO)
i32 versão (3)
i32 n      (nº de pontos)
n × { f64 Δx · f64 y (0…1) · f32 tensão (−1…1) · u8 tipo de curva · u8 · u8 · i8 direção }
5 × i32    flags, e −1, −1, −1, −1 (pontos de sustain/loop: nenhum)
```

- **Δx em tempos (semínimas)**; em **todos** os 72 slots a soma dos Δx dá **4,0 = um compasso** em 4/4.
  Conferido pelos nomes: "2 Beat Gate" alterna 1/0 a cada 1,0; "1 Beat Gate" a cada 0,5; "1/2 Bt Gate" a cada
  0,25 **[DADOS]**.
- **Tipo de curva do segmento que termina no ponto** (byte 20): 0 single curve (com tensão; tensão 0 = reta),
  **2 hold** (degrau: mantém o valor anterior até o ponto — é o tipo dos *gates*), 5 pulse e 6 wave (um único
  segmento que oscila; ex.: "1/3 Bt Gate" = 2 pontos, segmento *pulse* com tensão 0,54; "Tremelo Slw/Fst" e
  "AM" = *wave*), 1/3/4/7… raros. Fórmulas em `REA_rodada2.md` §3.3. O byte 23 é o cache de direção (sinal da
  tensão; 2 = reta) — recalcular.
- **Volume:** `y` = ganho linear (1 = 0 dB, 0 = mudo) **[HIP forte]**. **Time travel:** `y = 1` = tocar
  normalmente; valores menores = ler de um ponto atrasado do buffer (`1 − y` proporcional ao atraso) — não
  aproximável no LYNX **[HIP]**.
- Slots de volume da fábrica: Empty, 2/1/½/⅓/¼/⅙/⅛ Beat Gate, Trance Gt 1–4, Saw Gate 1–2, 1st Step, 1st Stp
  Fade, 2 Step Gate, Off Beat, ¼ Dynamic, ¼ Swing, ¼ Gate Out/In, Spd Up / Slw Dn Gate, End Fade, End Gate,
  Tremelo Slw/Fst, Sidechain, Drum Loop (363 pontos), Copter, AM, Fade In, Fade Out, Fade Out In, Mute.

### 2.4 Sincronismo **[STR+HIP]**

Com *position sync* = 4 beats (o padrão em 100 % dos slots dos dados) a fase do envelope é
`(posição da música em tempos) mod 4`. *Trigger sync* só importa quando o slot é trocado (por automação do
parâmetro 1 ou por MIDI): a troca espera o próximo múltiplo. Ganho final =
`(1 − mix) + mix · env(fase)`, suavizado por um seguidor ataque/release com os tempos de §2.2.

---

## 3. Pitcher **[DECOMP `FUN_18001d6e0`/`FUN_18001d790`/`FUN_18001c240` + DADOS]**

Correção de afinação/harmonizador. `0xD5` = `"SSLF"` + `u32 0x98` + 152 B:

| Off | Conteúdo |
|---|---|
| 0 | tag `"1RTP"` (`0x50545231`) — ou `"1DTP"` na versão demo (ignorada pelo leitor normal) |
| 4 | **33 × `f32` = parâmetros 0…32** (valor normalizado do host) |
| 136 | 12 B de estado interno (não parâmetro) |
| 148 | `f32` = parâmetro 33 |

Nomes conhecidos (`FUN_18001c240`): 0 **Correction Speed**, 1 Stereo, 2 **Gender**, 3 Fine Tune, 4 **Bypass
All**, 5…16 *Key C…B Bypass* (escala), 17…20 *Voice 1–4 Pan*, 23 *Note A Freq*, 24 *Pitch Reference*; 21, 22,
25…33 sem nome no registro (modos/harmonização; nos dados 28 = 1/3/6 e 32 = 2, valores inteiros) **[DECOMP+DADOS]**.
Nos 39 slots dos demos: speed 0,6…1,0, gender 0,5 (neutro) quase sempre **[DADOS]**.

Aproximação: **nenhuma** com os efeitos internos — correção de afinação não tem equivalente; passar reto
(registrar no log). Se `Gender ≠ 0,5` ou houver harmonização (vozes) o som muda bastante; apenas avisar.

## 4. Effector **[STR+DADOS]**

Multiefeito de 12 algoritmos com modulação X/Y. `0xD5` = `"SSLF"` + `u32 0x134` + `"10XF"` + 64 `f32` (zerados
em todos os 31 blobs; reservados **[HIP]**) + **12 `f32` = parâmetros 0…11** (normalizados 0…1; o DFM tem
faixa 0…256):

| i | Parâmetro | Nos dados |
|---|---|---|
| 0 | **Effect kind** | `round(v·11)`: 0 Distort, 1 Lo-fi, 2 Flanger, 3 Phaser, 4 Filter, 5 Delay, 6 Reverb, 7 Stereo, 8 Trans, 9 Grain, 10 Vox, 11 Ring |
| 1 | FX bypass | 0 |
| 2 | **Wet mix** | 0,1…1 |
| 3 | **Param X** | — |
| 4 | **Param Y** | — |
| 5 | Mod rate | `round(v·8)` → ⅛, ¼, ⅜, ½, ¾, 1, 2, 4, 8 (/1 = compassos) |
| 6 | Tempo reduce | 0 |
| 7 / 8 | Mod amount X / Y | 0,5 = sem modulação |
| 9 | Mod shape | Off, On, Sine, Saw, Square **[STR; ordem HIP]** |
| 10 | Input level | 1,0 (0,58 num caso) |
| 11 | Output gain | 0,5 = 0 dB **[HIP]** |

Nos 26 slots dos demos: Reverb 11, Trans 8, Vox 4, Lo-fi 2, Distort 1 **[DADOS]**.

## 5. Frequency Splitter **[STR+DADOS]**

Divisor de 2/3 bandas com saídas extras (sends para trilhas do mixer / saídas 1–3 dentro do Patcher) e uma
"saída principal" com nível por banda. `0xD5` = 28 × `i32` + 1 byte (113 B):

| Palavra | Tag | Conteúdo |
|---|---|---|
| 0 | — | versão (4) |
| 1 | 0 | **Low/Mid freq** 0…65536 (19800) |
| 2 | 1 | **Mid/High freq** 0…65536 (48065) |
| 3 | 2 | Low/Mid slope (combo 0…7) |
| 4 | 3 | Mid/High slope (combo 0…7) |
| 5 | 4 | Linear phase (0/1) |
| 6 | 5 | **Number of bands** (0 = 2 bandas, 1 = 3 bandas) **[DADOS: presets "Dual band…" = 0, "Steep tri band" = 1]** |
| 7 | 6 | Linear phase quality |
| 8 | 7 | Linear phase mod speed |
| 9–11 | 8–10 | nível de envio Low/Mid/High, centésimos de dB (−1800…1800) |
| 12–14 | 11–13 | **nível na saída principal** Low/Mid/High, centésimos de dB (−1800 = mudo) |
| 15–27 | — | mutes/solos, trilhas de destino e UI **[HIP]** |

**Frequência** (ajuste aos presets "Isolate below 200Hz" = 19792, "Dual band 500Hz" = 29365, "Isolate over
4kHz" = 51069, "Steep 30Hz Cut" = 0): `f = 30 Hz · (16000/30)^(v/65536)`, erro < 1 % **[DADOS]**.

## 6. Vocodex **[STR]**

Vocoder (modulador = entrada; portadora = sidechain ou sintetizador interno). Blob começa como o Gross Beat
(`0x00080003`) e guarda 20 parâmetros (DFM: Soundgoodizing, Mod/Car pass-through level e filtros, *added
noise*, hold/attack/release das bandas, largura e *flatness* das bandas, pitch/unison do modulador…) e as
bandas. **Sem aproximação útil** no LYNX (precisa de portadora); importar como bypass com aviso.

---

## 7. Controladores internos (Peak, Envelope, Formula, X-Y) e os links **[DADOS+STR]**

### 7.1 Onde o link fica no `.flp` **[DADOS]**

Não há evento novo: o link de controlador usa o **mesmo `0xE3`** dos clipes de automação (registros de 20 B,
flags `8`, último campo `0x1D5` como nos clipes). A diferença está no **1º campo (origem)**: em vez de
`canal << 16`, ele é um **ControlId da saída do controlador**, com a parte baixa ≥ `0x8000`:

| Origem | Significado | Exemplo |
|---|---|---|
| `canal<<16 \| 0x8000+k` | saída k de um **gerador-controlador** no canal (Envelope Controller, Keyboard Controller) | `0x00018000` = canal 1, saída 0 ("Basic Sidechain") |
| `0x70000000 + trilha<<22 + slot<<16 + 0x8000+k` | saída k de um **efeito-controlador** num slot do mixer (Peak, X-Y, Formula, Control Surface) | `0x70458001` = trilha 1, slot 5, saída 1 (o Peak Controller do "With You") |
| `0x2…/0x3…` + mesma conta | forma antiga (soma `0x50000000`, como já faz `normalizeControlId`) | `0x39008000` = trilha 100, slot 0 (Control Surface do "Future House") |

O 2º campo repete a origem na forma antiga em alguns arquivos (ignorável). O 3º é o alvo (ControlId normal).
Nos demos: 57 links saindo de slots do mixer e 18 de canais; saídas usadas k = 0 (46), 1 (17), 2 (12) **[DADOS]**.
Hoje `Flp.cpp` lê `origem >> 16` como "canal do clipe" — esses registros caem num canal inexistente ou num canal
que não é clipe, e são descartados em silêncio.

### 7.2 Dentro do Patcher

Mesmo conceito, mas pelas conexões de classe 2 (§1.5): `porta de controle de saída (0x1000000k)` → `porta de
parâmetro p`. 18.405 dessas conexões nos dados (Formula/X-Y/Control Surface → PEQ2, PanOMatic…).

### 7.3 Fruity Peak Controller (42 B) **[STR+DADOS]**

10 × `i32` = Tags 0…9 + 2 bytes de opções:

| i | Parâmetro | Faixa (padrão) |
|---|---|---|
| 0 | Base level | 0…65536 (0) |
| 1 | **Peak amount** | −256…256 (179) |
| 2 | Peak tension | −128…128 |
| 3 | **Peak decay speed** | 5…128 (66) |
| 4 | LFO base level | 0…65536 |
| 5 | LFO amount | −256…256 |
| 6 | LFO tension | −128…128 |
| 7 | LFO speed | 200…65536 (32950) |
| 8 | LFO shape | combo |
| 9 | LFO phase | 0…65535 |

Saídas: 0, 1, 2 — provavelmente *peak*, *LFO* e *peak × LFO* **[HIP: a ordem não foi isolada]**. Uso típico
nos demos: Peak Controller num slot cujo áudio é o bumbo, ligado ao **volume de um Balance** em outra trilha
(ex.: "With You": trilha 1 slot 5 → trilha 7 slot 4, parâmetro 1 = volume do Balance) = *sidechain pumping*.

### 7.4 Fruity Envelope Controller (gerador, 2–6 KB) **[DADOS; layout parcial]**

Cabeçalho `0x00060003`, `i32 14`, 14 blocos de 11 `i32` (escalas de ataque/decay/sustain/release = 128, LFO
`0x9C40`…), depois **articuladores no formato do Sytrus** (`REA_rodada11.md` §1.6): `i32 3`, 3 floats
(profundidades), e partes com objetos `TCustomMEnvelope` — o envelope (tipo 1) seguido de LFO e mapas (tipo 3,
dois pontos planos). No "Basic Sidechain" o envelope é `0 → 0,754 em 0,237 → 1,0 em +0,092` (tensões −0,30 e
+0,18): o clássico *duck* que volta em ~⅓ s. Δx em **segundos** quando o bit TEMPO do rodapé está desligado
(como aqui), em tempos quando ligado. O envelope dispara a cada nota do canal (o padrão do canal é o "gatilho").

### 7.5 Formula / X-Y / Control Surface **[DADOS]**

- **Formula Controller:** `i32` versão (2/4) + 3 knobs `a, b, c` (0…65536) + texto da fórmula (UTF-16) + descrição
  RTF. Avaliar exige um interpretador de expressões; fora de escopo.
- **X-Y Controller** (77–81 B) e **Control Surface** (até 29 KB): valores atuais dos eixos/controles + UI. Um
  controle parado = valor constante: basta aplicar o valor atual da saída ao alvo uma vez.

---

## Recomendações para o LYNX

1. **Patcher em slot do mixer (prioridade 1).**
   - **Parser** (`src/flp/` ou `FlEffects.cpp`): implementar §1.1–1.5 (chunks `u32 id + u64 tamanho`, grupos id 0
     recursivos, strings UTF-16 em v4 / ANSI em v3). Produzir `PatcherGraph { nós[id → nome (5008),
     estado (5005), nIn, nOut], portas[id → (nó, classe, índice, entrada?)], pinos[id → (entrada-de-fora?,
     barramento, nome)], arestas[(origem, destino, ganho, classe)], parâmetros expostos (1100) }`.
   - **Achatamento em série:** se o subgrafo de áudio no barramento 0 for uma cadeia única
     `IN → n1 → … → nk → OUT` (cada nó com exatamente 1 entrada e 1 saída de áudio usadas, todas no
     barramento 0, ganhos 1,0), **substituir o slot do Patcher pelos k slots**, decodificando cada `5005` com o
     decodificador existente (`FlEffects.cpp`, comparação sem caixa). Se faltarem slots (máx. 10 por trilha),
     manter os primeiros e avisar. Ganho ≠ 1 num cabo da cadeia → multiplicar no `mix`/utilitário seguinte.
   - **Paralelo → submix:** quando `IN` se divide em R ramos que só se juntam em `OUT`, e cada ramo é uma
     cadeia simples: criar R trilhas auxiliares no mixer do LYNX (nome `"<trilha> · Patcher ramo r"`), cada uma
     com a cadeia do ramo; a trilha original manda (*send* pós-fader, nível = ganho do 1º cabo) para cada ramo e
     deixa de ir direto ao destino; os ramos roteiam para o destino original da trilha com o ganho do último cabo.
     Isso só é exato quando o Patcher é o **último** efeito da trilha e o fader está em 0 dB; senão, avisar.
     Ramos que saem pelo **barramento 1 de um Stereo Shaper / Frequency Splitter** (M/S, bandas) não são
     reproduzíveis com sends simples: aproximar o ramo por um EQ (para bandas, §5) ou descartar o ramo com aviso.
   - **Sidechain** (aresta para entrada com barramento ≥ 1) e nós fora de `IN→OUT` (Peak Controller,
     Formula, notebook): ignorar no áudio; as conexões de controle viram automação (item 4).
   - **Parâmetros expostos (1100):** guardar `índice do Patcher → (nó, parâmetro)` para redirecionar automações
     do FL que apontam para "Patcher parâmetro i" (ControlId do slot com `pluginParamBase + i`).
   - **Patcher gerador (canal):** escolher o nó gerador ligado a `OUT` com mais ganho (Harmor, Sytrus, 3x Osc…),
     importar como o gerador do canal e pendurar a cadeia de efeitos dele como FX do canal, se o LYNX tiver;
     senão, só o gerador.
2. **Gross Beat → gate de volume sincronizado (prioridade 2).** Ler o slot de volume `p1` (§2.1), o
   `volume mix` `p3/256`, ataque/release (§2.2). Se o slot for "Empty" (2 pontos y = 1) ou `mix = 0`, não criar
   nada. Caso contrário, duas opções:
   - **(preferida)** novo `EffectKind::volumeGate` (ou um modo do `utility`): tabela de até 64 pontos
     `(x em tempos, y, tensão, tipo)` sobre 4 tempos, fase = `posição em tempos mod 4` (com *position sync*
     ≠ 5, usar o período correspondente), curvas tipo 0 (exponencial pela tensão), 2 (hold), 5/6 (pulse/wave
     com nº de ciclos pela tensão; fallback: reta), ganho `(1 − mix) + mix·y`, suavização ataque/release
     (seguidor de 1 polo com τ = tempo/2,2).
   - **(sem efeito novo)** gerar um clipe de automação para o ganho de um `utility` inserido no lugar,
     repetindo o padrão a cada compasso durante a música (atenção: `utilityGainValue` vai só até −24 dB; para
     `y = 0` usar o mínimo e aceitar o "mudo" imperfeito).
   - O *time slot* (`p0`) só é tolerável quando `p2 = 0` ou o slot de tempo é plano (`y ≡ 1`); senão registrar
     "time travel do Gross Beat ignorado".
3. **Frequency Splitter → EQ.** Para a saída principal: `parametricEq` com *low shelf* em `f(p0)` com ganho
   `L − M`, *high shelf* em `f(p1)` com ganho `H − M` (3 bandas; em 2 bandas só o shelf em `f(p0)` com `L − H`) e
   ganho de saída `M` (dB = palavra/100; −1800 = mudo → −60 dB). Inclinação: combo 0…7 → 6·(k+1) dB/oct
   **[HIP]**, ou simplesmente shelf de 12 dB/oct. Os envios por banda para outras trilhas são perdidos (avisar).
4. **Links de controlador (`0xE3` com origem ≥ `0x8000`).** Em `Flp.cpp`, separar: se `origem & 0xFFFF ≥
   0x8000`, é `ControllerLink { origem: ControlId (canal ou trilha/slot), saída k, alvo }`, não um clipe.
   - Envelope Controller (canal): para cada nota do canal na playlist, carimbar o envelope (§7.4) como pontos
     de automação do alvo (valor = base + envelope; convertido pelo `effectParamFromFl` existente). Produz
     uma automação fiel para o caso "sidechain por padrão".
   - Peak Controller → volume (Balance/fader/utility) com *amount* negativo ou destino volume: aproximar por
     **ducking** — um clipe de automação que cai a cada nota do canal que alimenta a trilha do Peak Controller
     (decay = `p3`) **[HIP]**; ou, se o LYNX ganhar compressor com sidechain, usar isso.
   - X-Y / Control Surface / Formula sem automação própria: aplicar o valor atual uma vez ao parâmetro alvo.
5. **Effector:** mapear pelo `kind` (§4): Distort → `distortion` (drive ≈ X), Lo-fi → `distortion` (modo
   *bitcrush* se houver, senão drive baixo + `filter` LP com corte ∝ Y), Flanger/Phaser → `chorus` (atraso curto,
   realimentação ∝ Y), Filter → `filter` (corte ≈ X em escala log 20 Hz…20 kHz, ressonância ≈ Y), Delay → `delay`
   (tempo sincronizado ≈ X), Reverb → `reverb` (tamanho ≈ X, amortecimento ≈ Y), Stereo → `utility` (largura ≈ X);
   Trans/Grain/Vox/Ring → bypass com aviso. `mix = p2`; `p1 = 1` → slot desligado. Todas as escalas de X/Y são
   **[HIP]** (o motor é C++ em `engine_x64.dll`, não lido nesta rodada).
6. **Pitcher e Vocodex:** importar como slot desligado/bypass com mensagem; não há aproximação honesta.
7. **Validação sugerida:** (a) abrir "Bien Duro" ins. 55 no FL e no LYNX após o achatamento (PEQ2 → Limiter);
   (b) renderizar uma trilha com Gross Beat "1/2 Bt Gate" e comparar o envelope de amplitude; (c) conferir no FL
   se o ganho de um cabo do Patcher (clique direito no cabo) corresponde ao `f32` de §1.5.

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| Patcher `FUN_00955150` | Escreve o estado: 1000, 0x3EA, 0x3EB, 0x3EC, nós (5000…5001), 0x44C, pinos (0x578), conexões (0x514), 0x5DC, posições (0x640), 0x3E9 | DECOMP |
| Patcher `FUN_009513d0` | Lê o estado (despacho por id; v < 3 faz migração; v < 2 troca tipos 1/2 do 0x3EB) | DECOMP |
| Patcher `FUN_00952a70` | Lê um nó: nomes, estado, opções, portas; trata `"Fruity Wrapper"` | DECOMP |
| Patcher `FUN_007c97a0`, `FUN_007c9a50`, `FUN_007c9a70` | Chunk = `u32 id + u64 tamanho + dados`; grupo id 0 com tamanho corrigido ao fechar | DECOMP |
| Patcher `FUN_0079cbc0`, `FUN_0079ca50` | Enumeram as ligações para gravar (porta +8, porta +0x10, `f32` +0x18, bool +0x1C, classe) | DECOMP |
| Patcher `FUN_0079b730`, `FUN_0079ef40` | Criam a ligação: ordenam as pontas (não-saída primeiro) e iniciam o ganho em 1,0 | DECOMP |
| Patcher `FUN_00952440`, `FUN_0079b790` | Leem uma conexão e reconectam pelas classes das portas | DECOMP |
| Patcher `FUN_00952560` | Lê um pino (áudio/MIDI, nomes padrão "Audio input/output", "MIDI Port %d", "Notes") | DECOMP |
| Patcher `FUN_009529d0` | Lê um parâmetro exposto (0x44C) | DECOMP |
| 363 blobs Patcher (`pdec.py`, `graph.py`, `flat2.py`) | Parse completo, direção das conexões (100 %), topologias, estados internos = estados do mixer | DADOS |
| Gross Beat VMT `0x83f0f0` (`vmts.py`), `FUN_0083f890` | `SaveRestoreState`: versão, 9 params, opções, 2 × 36 slots (nome + 15 B + envelope) | DECOMP |
| Gross Beat `FUN_00840b20`, `FUN_00840ed0`, `FUN_00841de0`, `FUN_00841fb0` | `ProcessParam`, texto dos parâmetros, curva `501^(v/10000) − 1` ms, release ×2 | DECOMP |
| Gross Beat DFM `TSynthEditorForm` (`GrossBeat.dfm.txt`, `gb_items.txt`) | Tags 0–7, combos de sync, snap, opções | STR |
| 45 blobs Gross Beat (`gbp.py`) | 72 slots, soma Δx = 4, tipos de curva | DADOS |
| Pitcher `FUN_18001d6e0`, `FUN_18001d790`, `FUN_18001c240` | Tag `1RTP`, 33 + 1 floats, nomes dos parâmetros | DECOMP |
| DFM Effector / Frequency Splitter / Vocodex / Peak / Envelope Controller (`*.dfm.txt`) | Tags, faixas, padrões | STR |
| 31 Effector, 30 Frequency Splitter, 40 Peak Controller | Layouts, faixas, ajuste da curva de frequência | DADOS |
| Eventos `0xE3` dos demos | Origem = ControlId de saída de controlador (75 links) | DADOS |

### Questões em aberto

- Patcher: confirmar no FL que o `f32` do cabo é ganho linear (e qual a faixa da UI); significado do bit 0
  das flags do nó e do cabo; `0x3F2/0x3F6` e `0x47E/0x6A5` (não vistos); semântica das saídas auxiliares 1…4 dos
  efeitos Fruity fora do Frequency Splitter (o Stereo Shaper usa a saída 1 nos patches M/S — *mid* e *side*
  separados?).
- Gross Beat: ordem exata dos 9 bytes de opções e dos 7 bytes por slot; unidade do parâmetro 8; escala do
  atraso no time travel; como a tensão global (p6) se combina com a de cada ponto.
- Pitcher: nomes dos parâmetros 21, 22, 25…33 (modo MIDI, escala, harmonização).
- Effector: escalas de X/Y por algoritmo (motor C++), significado dos 64 floats iniciais.
- Frequency Splitter: tabela das inclinações e dos campos 15…27 (trilhas de destino dos envios).
- Peak Controller: qual saída (0/1/2) é peak, LFO e produto; curva do *decay speed*.
- Envelope Controller: o que são os 14 blocos de 11 inteiros e quantas saídas há (8 articuladores no blob do "Basic Sidechain").
