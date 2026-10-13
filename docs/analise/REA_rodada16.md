# Análise comportamental do FL Studio 2026: rodada 16 (REA) — MIDI Out e o roteamento por porta, Fruity Love Philter (layout completo, índices de automação, Mod X/Y) e Morphine (árvore de chunks, parâmetros mestres, espectros)

> **Natureza deste documento.** Continuação de `REA_rodada15.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são descrições,
> layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais
> servem só para rastrear a evidência. Nenhum conteúdo cifrado foi tocado (todos os estados abaixo são blobs
> `0xD5` abertos — o do Love Philter é só comprimido com zlib — gravados pelos próprios plugins).

- **Alvos** (cópias locais em `tmp/r16/bin/`, originais em `Plugins/Fruity/Generators/<nome>/` e
  `Plugins/Fruity/Effects/Fruity Love Philter/`):

  | Plugin | DLL (SHA-256) | Linguagem | Estado (`SaveRestoreState`) / parâmetros |
  |---|---|---|---|
  | MIDI Out | `a87bf4c1…ef45` | Delphi (`TTestPlug`, VMT `0x6fd778`) | `FUN_006fde10` (VMT+3); despacho `FUN_006fdb80`; palavra de roteamento `FUN_006fec00` |
  | Fruity Keyboard Controller | `72ef38b5…0b3c` | Delphi (VMT `0x6fcbb0`) | `FUN_006fcff0` (VMT+3) |
  | Fruity Love Philter | `b9f11d63…f547` | Delphi (VMT `0x853df8`) | `FUN_008551e0` (VMT+3); `ProcessParam` `FUN_008579e0`; índice→alvo `FUN_008584e0`; texto `FUN_00858100`; nomes `FUN_00858680`; construtor `FUN_00856780`; coeficientes `FUN_00859100` |
  | Morphine | `decf72d6…d48e` | C++ (MSVC, `MorphineInstance`) | árvore de chunks: `MPR0` `FUN_18010eb40`, `MVC0` `FUN_180113460`, `MZH0` `FUN_18012ad30`, `MSP0` `FUN_1800ec160`, `MBP0` `FUN_1800ef900`; get/set de parâmetro `FUN_18010e340`/`FUN_18010dc50`; nomes `FUN_18010a2a0`; texto `FUN_180109ef0`; envelope mestre `FUN_1800fffa0`/`FUN_180100620` |

- **Ferramentas:** Ghidra 12.1.4 *headless* em `/tmp/r16gh_<dll>`, scripts em `tmp/r16/gh/scripts/` (os da
  rodada 15 + `DecompRange.java`, que decompila todas as funções de um intervalo de endereços). DFM com
  `tmp/r14/tools/dfm.py` + `tmp/r15/dfmc.py`; campos publicados com `tmp/r15/fields.py`; VMTs com
  `tmp/r14/tools/vmts.py` + `tmp/r15/vmt.py`.
- **Decodificadores de apoio (nossos, Python):** `tmp/r16/lp.py` (Love Philter completo: **96/96 blobs**
  — 43 slots + 53 `.fst`, versões 2, 5, 6, 10, 11, 12 — sem nenhum byte sobrando), `tmp/r16/lpstat.py`
  (o que cada articulador modula), `tmp/r16/lplinks.py` (links/automação que apontam para o Love Philter),
  `tmp/r16/mor.py` (árvore `MPR0` do Morphine: 7/7 canais + `.fst`), `tmp/r16/scanw.py` (portas MIDI do
  wrapper e do MIDI Out em todos os `.flp`).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/disassembly da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

**Convenções.** `curve11(x)` = `(11^x − 1)/10` (a curva de volume do FL, já no LYNX como `curve(11, x)`).
`scurve11(v)` = `sinal(v)·curve11(|v|)` (versão com sinal, usada quando o controle admite valor negativo =
inversão de fase). "Δx" = coordenada guardada como **incremento** em relação ao ponto anterior (é assim nos
envelopes/mapas do editor genérico da IL, ver rodada 11 §1.6).

### Contagem nos dados (prioridade) **[DADOS]**

| Plugin | Onde | Nos demos | Arquivos demo | `.fst` | Tamanho do `0xD5` |
|---|---|---|---|---|---|
| **Fruity Love Philter** | slots do mixer | **39** (43 em todo `Data`) | 16 | 53 | 837…1 427 B (zlib; 59…88 KB descomprimido) |
| Morphine | canais | 7 (4 arquivos) | 4 | 4 | 47…241 KB (`SSLF`…) |
| MIDI Out | canais | 3 (+1 template) | 2 | 14 | 383 B (v6/v7); presets Roland 675…3 377 B |
| Fruity Keyboard Controller | canais | 10 (todos em "Drumloop Masher") | 1 | 11 | 553 B (v2) / 501 B (v0) |
| Fruity Wrapper | slots do mixer | 23 (2 arquivos) | — | — | registro `0x01` sempre `−1, −1` |

Nenhum `.flp` de `Data` tem um **canal** com o Fruity Wrapper (todos os geradores dos demos são nativos), e os
23 wrappers do mixer têm as duas portas MIDI em `−1`. Ou seja: **nos demos, o MIDI Out não alimenta nenhum
plugin hospedado** — ele alimenta o **Pitcher** (modo MIDI) e, no template de vídeo, o ZGameEditor (§1.3).

---

## 1. MIDI Out e o caminho das notas até um plugin

### 1.1 Layout do `0xD5` do MIDI Out (versão 7) **[DECOMP `FUN_006fde10`, `FUN_006fec00`, `FUN_006feb10`, construtor `FUN_006fe4c0` + DADOS]**

| Offset | Tipo | Campo (objeto `P+…`) | Significado |
|---|---|---|---|
| 0 | u32 | — | **versão** (FL atual grava 7; demos 6 e 7; `.fst` de banco 4; presets Roland 2/4/5) |
| 4 | i32 | `P+0x120` | **canal MIDI, base 0** (a UI mostra +1; `ChanSelect` 1…16). Default de instância nova: um contador global que gira 0…14 **pulando o 9** (canal 10 de bateria) |
| 8 | i32 | `P+0x124` | sempre −1 nos dados; não usado na carga **[HIP: reservado]** |
| 12 | i32 | `P+0x128` | **Bank MSB** (CC 0), −1 = não enviar (`BankMSBSelect`) |
| 16 | i32 | `P+0x12C` | **Bank LSB** (CC 32), −1 = não enviar (`BankLSBSelect`) |
| 20 | i32 | `P+0x130` | **página** de controles exibida (`PageSelect`; controle `i` pertence à página `i div 9`) |
| 24 | i32 | `P+0x134` | **porta de saída** 0…255 (`PortSelect`) |
| 28 | u8 | `P+0x138` | "Send pan, volume and pitch" (`SendPanVolBox`) |
| 29 | u8 | `P+0x139` | "Map note color to MIDI channel" (`MapColToChanBox`) |
| 30 | 73 × i32 | `P+0x13C…` | `[0]` = **programa** (1…128; 0 = não enviar — o envio é `0xC0` com `programa−1`), `[1…72]` = valores atuais dos 72 controles (0…127) |
| 322 | registros | `P+0x260 + i·0x2C` | até 72 definições de controle: `u8 índice` (0xFF termina), 24 B (`tipo` CC/RPN/NRPN/AFT/Note, número, mín, máx, valor de note-off, …), 2 strings (nome longo, nome curto). Só grava os que diferem do padrão |
| … | 8 × string | — | nomes das 8 páginas ("Page 1"…"Page 8") |
| … | u32 (v > 5) | `P+0x1FC0` (bits) | opções "On reset" **invertidas** (bit = 1 → opção desligada): 1 programa, 2 pan, 4 volume, 8 pitch, 0x10 faixa de pitch bend, 0x20 cortar notas |

Versões antigas leem menos bytes no bloco de cabeçalho: v ≥ 4 → 26 B (até `P+0x139`), v 2…3 → 24 B, v < 2 → 20 B;
v < 7 copia o campo +8 de cada controle para +0x14.

**A palavra de roteamento** (`FUN_006fec00`): o plugin monta `canal + porta·0x1000000` e soma isso a cada
mensagem curta que envia ao host — ou seja, **a porta vai no byte alto da mensagem MIDI interna** e o canal no
nibble baixo do status. O host (FL) entrega a mensagem a todo destino cuja **porta de entrada** é igual a essa
porta. O despacho `FUN_006fdb80` também aceita do host (opcode 0x10) a troca de canal (`param_3 = 0`) e de
porta (`param_3 = 1`).

**Dados dos demos:**

| Arquivo / canal | v | canal | porta | Destino que escuta a mesma porta |
|---|---|---|---|---|
| Asher Postman — "ASHER POSTMAN" | 6 | 0 (ch 1) | 0 | nenhum plugin com porta 0 no projeto **[DADOS]** (canal "decorativo") |
| Astes — Bien Duro — "1.2 MIDI" | 7 | 0 (ch 1) | **0** | **Pitcher** do insert 4, slot 0 (porta MIDI 0) |
| Astes — Bien Duro — "1.3 MIDI" | 7 | 0 (ch 1) | **1** | **Pitcher** do insert 5, slot 0 (porta MIDI 1) |
| Template "Video chopping" — "Trigger Video" | 7 | 0 | 1 | ZGameEditor Visualizer (não verificado) |

O par Bien Duro fecha a hipótese da rodada 15 §1 sobre o **parâmetro 33 do Pitcher**: os dois Pitchers em modo
MIDI têm `p33 = 0,00638` e `0,0112` → `int(256·v) − 1` = **0** e **1**, exatamente as portas dos dois MIDI Out;
os outros 9 Pitchers do arquivo têm `p33 = 0` (→ −1, nenhuma porta) **[DADOS]**.

### 1.2 Onde o Fruity Wrapper guarda a porta de entrada **[DECOMP da rodada 5 + DADOS]**

Registro `0x01` do blob do wrapper (rodada 5 §2.2): `i32 porta de entrada MIDI` (`W+0x854`), `i32 porta de
saída MIDI` (`W+0x858`), `u8` opção, 11 B zero. −1 = nenhuma. **Não há canal MIDI no wrapper**: ele só filtra
pela porta; o canal segue dentro da mensagem e quem decide é o plugin hospedado (Kontakt multi, etc.).
Nos demos: 23 wrappers, todos `−1, −1` **[DADOS]** — não existe exemplo real de MIDI Out → VST nos dados.

Plugins nativos que escutam uma porta pelo **próprio estado** (não pelo wrapper): Pitcher (p33, acima). Para os
demais nativos a porta fica nas opções do *wrapper* nativo do FL, que não aparecem no `.flp` dos demos
**[HIP]** (o cabeçalho `0xD4` dos slots não muda com isso — conferido nos três arquivos acima).

### 1.3 Algoritmo de roteamento para o LYNX **[HIP: comportamento do FL, consistente com os dados]**

1. Para cada canal MIDI Out: `porta = i32 @24`, `canal = i32 @4` (0…15).
2. Destinos que escutam a porta `p`: (a) canais/slots com Fruity Wrapper cujo registro `0x01` tem
   `porta de entrada == p`; (b) slots com Pitcher cujo `int(256·p33) − 1 == p` (e botão MIDI ligado, p25).
3. As notas do *piano roll* do canal MIDI Out são entregues a todos esses destinos **com o canal MIDI
   `canal`** (ou, se "Map note color to MIDI channel" estiver ligado, com a cor da nota como canal). Não
   filtrar por canal no LYNX: repassar a mensagem com o canal e deixar o plugin decidir.
4. Programa/Bank (`@30`, `@12`, `@16`) são enviados no reset/início se ≥ 0 (programa > 0).
5. O MIDI Out em si não produz som: no LYNX, o canal deve ficar mudo e servir só de fonte de notas.

### 1.4 Fruity Keyboard Controller **[DECOMP `FUN_006fcff0` + DFM + DADOS]**

Não é roteamento de MIDI: é um **controlador interno** (como o Envelope Controller), cuja saída 0 é um valor
0…1 escolhido pela **última tecla tocada** no canal, com suavização; os links `0xE3` apontam para ela com
origem `canal<<16 | 0x8000` (rodada 14 §7.1).

| Offset (v2/v3) | Tipo | Significado |
|---|---|---|
| 0 | u32 | versão (3 no FL atual; demos 2; `.fst` antigos 0) |
| 4 | 2 × i32 | suavização: **ataque** e **release** (0…256, padrão 32; `SpeedAttWheel`/`SpeedRelWheel`; são os parâmetros 0 e 1). v0: só 1 i32 (o release copia o ataque) |
| 12 | 132 × f32 | **valor de cada tecla** 0…131 (v0: 120 teclas) |
| 540 | 2 × i32 | faixa de teclas selecionada no teclado da UI (sem efeito no som) |
| 548 | f32 | valor de *note off* (`AutoRelWheel`, 0…1) |
| 552 | u8 | *auto release* ligado (quando nenhuma tecla está presa, vai para o valor de note off) |
| 553 | u8 (v ≥ 3) | botão "link attack & release" |

Demo "Drumloop Masher": "Glitch" mapeia as teclas 48…83 numa rampa 0…1 e está ligado ao **Mod X** de dois Love
Philter (§2.6); "Filter", "Delay send N", "Verb send N" fazem o mesmo para outros parâmetros **[DADOS]**.
**LYNX:** tratar como fonte de controle: `saída = valores[nota]` na nota (ou `valorNoteOff` no note-off se o
auto release estiver ligado), filtrada por um suavizador de 1ª ordem com tempos ∝ ataque/release.

---

## 2. Fruity Love Philter

### 2.1 Envelope do blob **[DECOMP `FUN_008551e0`]**

- `u32` versão. Se > 0xFFFF, a versão é a palavra alta (o FL atual grava `0x000C0003` → **12**); dados: 2, 5,
  6, 10, 11, 12. Valor `0x7FFFFFFF` ou ≥ `0xC0004` = recusado.
- O resto do blob é um **fluxo zlib** (todas as versões). Tudo abaixo é o conteúdo descomprimido, little-endian.

### 2.2 Layout descomprimido **[DECOMP `FUN_008551e0`, `FUN_00855080`, construtor `FUN_00856780`; DADOS 96/96]**

| Bloco | Tamanho | Conteúdo |
|---|---|---|
| Globais | 6 × i32 (`P+0x128`) | parâmetros 0…5 (tabela §2.4) |
| Globais 2 | 33 B (v ≥ 11; v10: 32; v < 10: 31) (`P+0x140`) | `i32 Mod X`, `i32 Mod Y` (0…1024, padrão 512), `i32` (0), `f32` (≈ 0,968: coeficiente interno de suavização **[HIP]**), `i32` **unidade em solo** (−1 = nenhuma; com solo, as outras unidades ficam com volume 0), `i32` **HQ envelopes** (`DQEnvBtn` = valor > 0), `i32` **oversampling** (`DQOverSelect`, 0…4, padrão 1), `u8` **SMOOTH** (`ModSmoothBtn`), `u8` **CENTER** (DC global), `u8 u8 u8` opções de menu (*Sync pattern changes*, *Note input*, *HQ WaveShaper* **[ordem HIP]**) |
| (v ≥ 2) | `u8` + `u32` | `USE1` (envelope follower lê a unidade 1) e posição do `HeartWheel` (só UI) |
| Texto info | v < 6: `i32 n` + n bytes (RTF ANSI); v ≥ 6: `u8 n` + n × UTF-16 | caixa "Info / comments" ("Preset name\rPreset details") **[o caso n ≥ 255 não foi visto; HIP]** |
| **8 unidades** | ver §2.3 | |
| (v ≥ 12) | `u8` | `P+0x161` (opção; 0 nos dados) |

### 2.3 Unidade (×8, nesta ordem) **[DECOMP + DADOS]**

| Campo | Tipo | Conteúdo |
|---|---|---|
| params | 12 × i32 | parâmetros da unidade 0…11 (tabela §2.4) |
| modo | i32 (+0x30) | **ordem / modo** (`FOrderSelect`): 0 off, **1 single** (padrão), 2 dual, 3 triple, 4 alternate dual, 5 alternate triple |
| tipo | i32 (+0x34) | **tipo de filtro** (`FTypeSelect`), tabela §2.5 (padrão 0) |
| flags | 3 × u8 (+0x38…) | `+0x38` **ALT** (ressonância/largura alternativa; `FFlatBtn`), `+0x39` **unidade ligada** (`EnableBankN`; só a 1ª vem ligada), `+0x3A` **waveshaper ligado** (`WSEnableBtn`) |
| curva WS | envelope | curva do *waveshaper* (`kind 3`, padrão `(0,0) → Δ(1; 0,75)`) no formato de envelope da rodada 11 §1.6 |
| WS bipolar | u8 | botão unipolar/bipolar do waveshaper |
| **8 articuladores** | ver abaixo | alvos, nesta ordem: **0 pan, 1 volume, 2 cutoff, 3 ressonância, 4 low/gain/drive, 5 band, 6 high, 7 waveshaper** (tabela de nomes em `PTR_u_panning_008d19c0`) **[DECOMP]** |

**Articulador do Love Philter** (mesma família `TMEnvelope`/`TMLFO`/`TMMap` do Sytrus, rodada 11 §1.6, mas com
outras partes — `EnvSelect`: *pattern envelope, LFO, mod X mapping, mod Y mapping, input envelope follower
mapping*):

```
i32  versão (1)
f32  modo: 1,0 = alvo unipolar (multiplicador), 0,0 = bipolar (deslocamento)   [DADOS: pan/cutoff/res = 0; vol/low/band/high/ws = 1]
f32  profundidade do envelope    (no articulador de cutoff = p0/128, o knob ENV da unidade)
f32  profundidade do LFO         (= profundidade do envelope × scurve11(MainLFO/128))
10 × { envelope (kind 1) ; 4 × i32 knobs A/D/S/R (padrão 128,128,0,128) }   ← um por "pattern" 1…10
envelope do LFO (kind 2) ; 4 × i32 (velocidade 512…65536, tensão, skew, largura de pulso) ; + 1 i32 se a versão do LFO ≥ 3
3 × mapa (kind 3): X, Y, envelope follower
```

Os envelopes usam o cabeçalho de 20 B da rodada 11 (`flags`, **ligado** em +4, decay, loop, sustain). O
*pattern* ativo é o parâmetro global 5 (`PatNum`, 1…10): só o envelope desse índice toca.

Mapas X/Y/IEF: a curva é `f(entrada)` em Δx; neutro = **0,5** nos alvos bipolares e **1,0** nos unipolares
(todos os 63 articuladores "intocados" das unidades ligadas dos demos seguem isso) **[DADOS]**.

### 2.4 Índices de parâmetro (automação/links) **[DECOMP `FUN_008584e0`, `FUN_00856780`, `FUN_008579e0`, `FUN_00858100`]**

São **359** parâmetros (`< 0x167`). Os 8 primeiros são globais; depois cada unidade `u` (0…7) começa em
`base(u) = 8 + 44·u` e tem **12 parâmetros próprios** (a última unidade só 11 — ela não tem *to next*) seguidos
de **32 parâmetros de articulador** (8 articuladores × 4).

**Globais (k = 0…7):**

| k | Nome (DFM) | Faixa crua | Valor efetivo |
|---|---|---|---|
| 0 | Master level (`MainVolSlider`) | 0…160 (128) | ganho `curve11(v/128)`, multiplica o volume de saída de todas as unidades |
| 1 | Master LFO level | −128…128 (128) | `scurve11(v/128)`; escala a profundidade de LFO de todos os articuladores |
| 2 | Envelope follower level | 0…160 (128) | `curve11(v/128)` |
| 3 | Envelope follower attack | 0…16384 (4096) | **ms = 12,5·(81^(v/16384) − 1)** → 0…1000 ms (4096 → 25 ms) |
| 4 | Envelope follower release | 0…16384 (8192) | mesma fórmula (8192 → 100 ms) |
| 5 | Pattern number | 1…10 (1) | inteiro (recortado em 1…10) |
| **6** | **Mod X** | 0…1024 (512) | **x = v/1024** |
| **7** | **Mod Y** | 0…1024 (512) | **y = v/1024** |

**Unidade u, k = base(u) + i, i = 0…11** (Tag do DFM = i):

| i | Nome | Faixa crua (padrão) | Valor efetivo **[DECOMP `FUN_00858af0`]** |
|---|---|---|---|
| 0 | ENV (cutoff env/LFO/IEF amount) | 0…256 (128) | profundidade do envelope do articulador de **cutoff** = `v/128` |
| 1 | **CUT** cutoff | 0…256 (128) | `c = v/256` (0…1), somado à saída do articulador 2 |
| 2 | **RES** ressonância/largura | 0…256 (128) | `r = v/256`, somado à saída do articulador 3 |
| 3 | L (low / gain / drive) | 0…128 (128) | `curve11(v/128)` × articulador 4 |
| 4 | B (band) | 0…128 (0) | `curve11(v/128)` × articulador 5 |
| 5 | H (high) | 0…128 (0) | `curve11(v/128)` × articulador 6 |
| 6 | WS pre amp | 64…192 (128) | `v/128` |
| 7 | WS mix | 0…128 | `v/128` |
| 8 | PAN | −128…128 (0) | `v/128` |
| 9 | **VOL** saída | 0…160 (128) | `curve11(v/128) · curve11(Master/128)`; 0 se outra unidade estiver em solo |
| 10 | IN volume | −160…160 (128) | `scurve11(v/128)` (negativo = fase invertida) |
| 11 | **NEXT** (to next filter) | −160…160 (0) | ganho `scurve11(v/128)` com que a **saída desta unidade entra na próxima** (só conta se a próxima estiver ligada) |

**Unidade u, articuladores: k = base(u) + n_u + 4·a + f**, `n_u` = 12 (11 na unidade 8), `a` = alvo 0…7
(pan, vol, cut, res, low, band, high, ws), `f` = 0…3 = **velocidade, tensão, skew e largura de pulso do LFO**
desse articulador (o setter escreve em `LFO+0xD0+4f`; o texto de `f = 0` vem do editor de envelopes, os outros
são `v/máx` em %) **[DECOMP]**.

Exemplos usados nos demos **[DADOS `lplinks.py`]**: `k = 7` (Mod Y) — 7 links de Envelope Controller ("Basic
Sidechain" e afins) e 1 clipe; `k = 6` (Mod X) — 4 links (Keyboard Controller "Glitch", X-Y Controller) e
4 clipes; `k = 9` (unidade 1 CUT) — 10 clipes; `k = 10` (unidade 1 RES) — 1 clipe; `k = 17` (unidade 1 VOL) —
3 clipes. Nenhum dado aponta para articuladores (k ≥ 20).

### 2.5 Tipos de filtro e escalas **[DECOMP `FUN_00859100`, constantes `0x85a740…0x85a908`; Hz HIP]**

Tabela de nomes (`0x8d18c0`): 0 **Chocolate state variable**, 1 Lime low pass, 2 Lime high pass, 3 Vanilla low
pass, 4 Vanilla band pass, 5 Vanilla band stop, 6 Vanilla high pass, 7 Vanilla all pass, 8 Vanilla low shelf,
9 Vanilla peaking, 10 Vanilla high shelf, 11 Mango low pass. Uso (63 unidades ligadas nos 43 slots de `Data`): 0 (SVF) 36, 1 8, 3 6, 6 6, 2 2, 7 2, 4 1, 5 1, 11 1
**[DADOS]**.

Com `c` e `r` já somados ao articulador (0…1) e `k` = fator de taxa do oversampler (`unidade+0xC4`;
interpretado como `44100/fs_interno`, **[HIP]**):

| Tipo | Frequência | Ressonância / Q |
|---|---|---|
| 0 SVF (Chocolate) | coeficiente `g = ((51^c − 1)·0,0159 + 0,005)·k`, recortado em 0,625 (→ `fc ≈ fs·asin(g/2)/π` **[HIP]**) | amortecimento `d = 1 − 0,9·r` (r ≤ 0 → 1; r ≥ 1 → 0,2); ALT → `d²`. Saída = `L·LP + B·BP + H·HP` com L/B/H = parâmetros 3/4/5 |
| 1, 2 Lime LP/HP | `x = (76^c − 1)·0,01324` (≤ 1,6); `ω = (x + 0,007)·(π/2)·k` (≤ 0,4π) | `1001^r` (r ≥ 1 → 50) |
| 3 Vanilla LP | `x = (1501^c − 1)·0,000664667` (≈ 0…0,997) ; `ω = (x + 0,003)·2π·k` (≤ 2,8495 rad ≈ 0,4535·fs) | **Q = 0,1 + 0,02495·(2001^r − 1)** (0,1…50) |
| 4, 5 BP / notch | `x = (1501^c − 1)·0,00039993` (≤ 1,0001); `ω = (x + 0,0001)·2π·k` | largura `0,01 + 0,1199·(101^r − 1)` (≤ 12) |
| 6 HP | idem 4/5, mas recortado em 0,6001 | Q como o LP |
| 7 all pass | idem 4/5 | Q como o LP |
| 8, 10 shelves | idem 4/5 | inclinação `S = 0,1 + 0,45·(3^(1−r) − 1)` (r = 0 → 1; r = 1 → 0,1) |
| 9 peaking | idem 4/5 | `Q = 0,1 + 0,04995·(2001^(1−r) − 1)` (r = 0 → 100, estreito; r = 1 → 0,1, largo); ganho = valor do slot "low/gain" |
| 11 Mango LP | `x = (201^c − 1)·0,01495` (≤ 3,0) | outra família (`251^…`, polinômio de ajuste) **[não detalhado]** |

Lendo em Hz a 44,1 kHz (k = 1): Vanilla LP `c = 0` → 132 Hz, `c = 0,5` → ≈ 1,2 kHz, `c = 1` → recorte ≈ 20 kHz;
famílias 4…10 `c = 0` → ≈ 4 Hz, `0,5` → ≈ 670 Hz, `1` → recorte. Com ALT, nos tipos biquad, `r` é multiplicado
por `(c + 1)·0,5` antes do cálculo.

**Modo/ordem:** o número de estágios em cascata é `modo` para 1…3 (12, 24, 36 dB/oit nos biquads); 4/5
(*alternate*) usam 2/3 estágios com o cutoff de cada estágio deslocado **[HIP]**. Modo 0 = unidade não filtra
(só ganho/pan/WS).

### 2.6 Roteamento entre unidades e Mod X/Y **[DECOMP `FUN_0085d110`, processamento em `0x85b…` + DADOS]**

- Para cada unidade ligada `u`, a entrada é `in·IN(u) + saída(u−1)·NEXT(u−1)` (o segundo termo só se `u−1`
  estiver ligada). A saída de cada unidade, com VOL e PAN, é **somada** na saída do plugin. Com todos os NEXT em
  0 (99 % dos dados), as unidades estão **em paralelo**.
- Por amostra/bloco: `cutoff = p1/256 + art_cut`, `res = p2/256 + art_res`, ganhos L/B/H = `curve11(p/128) ·
  art`, volume e WS idem (multiplicadores).
- **Mod X / Mod Y não têm alvo fixo**: cada articulador tem um mapa X e um mapa Y; X/Y (0…1) passam pela curva
  e o resultado vira deslocamento (bipolar, neutro 0,5) ou multiplicador (unipolar, neutro 1). O "alvo de Mod X"
  é, portanto, o conjunto de articuladores cujo mapa X não é a reta neutra.

O que os dados mapeiam (63 unidades ligadas nos 43 slots de `Data`; 56 delas nos 39 slots dos demos) **[DADOS `lpstat.py`]**:

| Mapa não neutro | Ocorrências | Forma típica | Leitura |
|---|---|---|---|
| **Y → volume** | 8 | reta `(0,0) → Δ(1;1)` | **ganho = y**: o *sidechain*/ducking (Envelope Controller → k = 7) |
| X → cutoff | 14 | reta `0→1`, ou degrau em 0,5 | cutoff desloca `(x − 0,5)` (escala do mapa: −0,5…+0,5 em `c`) |
| Y → ressonância | 12 | reta `(0;0)→(1;0,625)` ou parecida | idem para `r` |
| X → pan | 7 | reta `0→1` | pan = `(x − 0,5)·2` somado |
| X → volume em degraus | 25 (+2 retas) | 2…6 degraus de 0/1 ao longo de x | **gate por faixa de X** (com o Keyboard Controller "Glitch" em X: cada tecla escolhe um padrão) |
| LFO → pan / cutoff | 7 / 7 | — | auto-pan / *wobble* |
| envelope → volume | 9 | pattern envelope ligado | gate rítmico (pattern 1) |

### 2.7 Aproximação no LYNX **[HIP; ver Recomendações]**

Uma unidade ligada com `modo ≥ 1` → `filter` (tipo pelo §2.5: 0 com L=128,B=H=0 → LP; L=0,H=128 → HP; B
dominante → BP; 3/1/11 → LP; 6/2 → HP; 4 → BP; 5 → notch; 7/8/9/10 → `parametricEq` banda correspondente),
`cutoff` = `c` em Hz pela linha do tipo, Q pelo §2.5, número de estágios = `modo`; depois `utility` com ganho
`curve11(VOL/128)·curve11(Master/128)` (e, se o mapa Y→volume existir, ganho multiplicado por `y`) e pan
`PAN/128`. Unidades em série (NEXT ≠ 0) → cadeia; em paralelo (todos NEXT = 0, mais de uma ligada) → contêiner
`parallel`. Unidade com `modo = 0` → só `utility`.

---

## 3. Morphine

### 3.1 Árvore de chunks **[DECOMP escritores `FUN_18010eb40`, `FUN_180113460`, `FUN_18012ad30`, `FUN_1800ec160`, `FUN_1800ef900`; DADOS 7/7]**

Os tags são inteiros de 4 caracteres gravados em little-endian, por isso aparecem invertidos no arquivo
(`"0roM"` = `Mor0`). Formato:

```
"SSLF" u32 tamanho_total                         (cabeçalho comum de estado C++ da IL)
"Mor0"                                           (tipo)
"MPR0"                                           (preset)
  4 × { i32 n ; MVC0 (n bytes) }                 geradores A, B, C, D
  i32 n ; MML0 (n bytes)                         linha de morph (breakpoints X/Y no tempo)
  i32 n ; MMD0 (n bytes)                         modulação: 4 × MEG0 (envelopes Eg1…Eg4)
  bloco de parâmetros globais (i32 0x5C4 + 0x5C0 B; §3.2)
MVC0: i32 n ; MZH0 (n bytes) ; i32 0x1C ; 6 × i32 (opções do gerador)
MZH0: i32 nº de zonas ; por zona { i32 largura/tecla, f32 nível, i32 n, MSP0 }
MSP0: i32 nº de quadros ; por quadro { i32 duração, f32 ganho, i32 n, MBP0 } ; i32 0x424 + 0x420 B (opções)
MBP0: 3 × [128 parciais em 16 grupos de 8: u8 máscara + um f32 por bit 1 (os bits 0 = valor 0)]
```

Atenção: o tamanho gravado antes do `MSP0` de cada zona e o `0x424` da cauda **incluem o próprio campo de
tamanho** (diferente dos outros chunks) **[DADOS: só assim os 7 blobs fecham]**.

**Parciais (`MBP0`)**: 3 vetores de 128 valores (`CBreakPoint+0x28/0x30/0x38`, todos 0 por padrão):
`[0]` = **amplitude** do harmônico n+1 (normalizada, pico 1,0), `[1]` = desvio de frequência (quase sempre 0;
um gerador do "Soft Pad" usa ≈ −0,54 constante → deslocamento **[HIP]**), `[2]` = **fase** em ciclos (−0,5…0,5)
**[HIP pela faixa]**. Cada gerador toca a **sequência de quadros** (um espectro que evolui no tempo): o `i32`
de cada quadro é a duração relativa (a soma dá múltiplos de 100) e o `f32` o ganho do quadro (envelope de
amplitude embutido). A escala absoluta de tempo depende de *Gen X: Rate* (não decodificada) **[HIP]**.

Gerador vazio = 1 quadro de duração 100 com tudo 0 (é o estado de C e D em todos os demos; B em 5 de 7).

Opções do `MVC0` (6 × i32): `[0]` f32 **Mix** do gerador (dados 1,0 / 0,425 / 0,5), `[1]` `int(2·v)` de um
seletor (**Loop mode**? 0 nos dados), `[2]` int, `[3]` u8 (1 nos geradores com dados), `[4]` f32, `[5]` f32
(1,0) **[só `[0]` é forte; resto HIP]**.

### 3.2 Bloco de parâmetros globais (`0x5C4` B) e índices **[DECOMP `FUN_18010e340`, `FUN_18010eb40`, padrões `FUN_1801099e0`]**

Layout (offset dentro do bloco, a partir do `i32 0x5C4`): `+4` 40 × f32 (objeto `+0x608…+0x6A4`), `+164`
f32 (`+0x6A8`), `+168` i32 (`+0x6AC`), `+172` 1028 B **nome do sample de ruído** (ex. `AmbientNoise_01.wav`),
`+1200` 256 B **nome do preset** (ex. `PAD Keepin it Simple FG`), `+1456` (`+0x6BC`), `+1460` (`+0x6B0`),
`+1464` (`+0x6B4`), `+1468` (`+0x6B8`), `+1472` (`+0x6C0`).

Os valores são **normalizados 0…1** (estilo VST), salvo Polyphony e Glide Mode. Índice `k` do parâmetro →
campo (`A[i]` = i-ésimo f32 do vetor de 40) e escala (texto `FUN_180109ef0` e DSP `FUN_1800fffa0`):

| k | Nome | Campo | Escala |
|---|---|---|---|
| 0 | Master Level | A0 | **dB = 48·v − 24** (0,5 → 0 dB) |
| 1 | Glide Time | A1 | 0…1 (% ) |
| 2 | Velocity Curve | A4 | % |
| 3 | Bend Range | A8 | `int(12·v)` semitons |
| 4 | Transpose | A5 | `int((v − 0,5)·48)` semitons |
| 5 | Fine Tune | A6 | `int((v − 0,5)·200)` centésimos |
| 6 | Overdrive | A7 | % |
| 7–10 | Chorus Depth / Wet / Rate / Enable | A13–A16 | rate Hz = `5v + 0,1` |
| 11–16 | Delay Time / Stereo / Modulation / Feedback / Wet / On | A17–A22 | tempo ms = `(0,95v + 0,05)²·1000` |
| 17–20 | Reverb Decay / Size / Mix / Enable | A23–A26 | % |
| 21–28 | Eq Band 1…8 | A27–A34 | dB = `(v − 0,5)·24` |
| 29 | Eq Enable | A35 | |
| **30–33** | **Master Attack / Decay / Sustain / Release** | A9–A12 | **A s = 15·v^3,5; D s = 30·v³; S = v² (amplitude); R s = 10·v³** (mín. 3 ms) — já com a modulação somada e recortada em 0…1 **[DECOMP `FUN_1800fffa0`, `FUN_180100620`]** |
| 34–36, 38 | Noise Mix / Pan / Mute / VelSens | A36–A39 | |
| 37 | Noise Mono Mode | `+1472` | |
| 39 | Noise Ratio | `+164` | `2^((v − 0,5)·4)` **[HIP: a função é pow2]** |
| 40–42 | Noise Flt Cutoff / Resonance / KeyTrk | `+1460`, `+1464`, `+1468` | cutoff Hz ≈ `35·2^(9v)` **[HIP]** |
| 43–98 | Gen A…D (Release Loop, Tempo Sync, Rate, VelTrk, KeyTrk, PWM…, Detune/Panning Range, Mix, Pan, Mute), Morph | sub-objetos (`MVC0`/`MSP0`/`MML0`) | — |
| 99–122 | Eg1…Eg4 (Release Loop, Tempo Sync, Rate, KeyTrk, VelTrk, Velocity) | `MEG0` | — |
| 124 | **Polyphony** | A3 | inteiro (padrão 31; dados 15, 11, 0 = mono **[HIP]**) |
| 125 | **Glide Mode** | A2 | inteiro (0/1/2) |
| 126, 127 | Noise Loop Mode / Noise Flt Mode | `+168`, `+1456` | inteiros |

**Não há filtro principal nem osciladores clássicos**: o timbre é o espectro aditivo (até 128 harmônicos) de
até 4 geradores misturados (e "morfados" por X/Y ao longo da linha `MML0`), mais ruído filtrado e efeitos.

### 3.3 O que os 7 canais dos demos têm **[DADOS `mor.py`]**

| Canal | Gerador(es) ativos | Espectro médio (ponderado por duração × ganho) | ADSR mestre |
|---|---|---|---|
| Cuebur "Synth 03" / "Lead Synth" (iguais) | A (mix 0,425), 99 quadros | 81 % da energia no 1º harmônico, 87 % nos ímpares, 12 harmônicos > 5 %, inclinação −1,3 → **quase seno/triângulo** | A 0, D 0,19 s, S 0,62, R 3 ms |
| DEDZII "Morphine" | A, 47 quadros | brilhante (centróide 14, 125 harmônicos), inclinação −0,6 → **serra/ruidoso** | — |
| LeriQ "Morphine #2" | A, 147 quadros | centróide 27, plano → **ruído/pad muito brilhante** | — |
| LeriQ "Morphine #4" | A com **25 zonas** de teclado, 11 quadros cada | inclinação −0,5…−1,1, 1º harmônico fraco → **serra suave** | — |
| LollieVox "Soft Pad" | A (mix 1,0, 85 quadros) + B (0,425, 393 quadros) | centróide 4…5, inclinação −0,7…−1 → **serra** | A 0,92 s, D 7,5 s, S 0,62, R 1,45 s |

---

## Recomendações para o LYNX

1. **MIDI Out → plugin (porta).** No import, ler o MIDI Out (`versão @0`, `canal @4`, `porta @24`, programa
   `@30`, banco `@12/@16`) e marcar o canal como **fonte de notas muda**. Montar a lista de destinos por porta:
   wrappers (canal ou slot) com `registro 0x01.portaEntrada == porta` e Pitchers com `int(256·p33) − 1 ==
   porta`. Na reprodução, copiar cada nota do canal MIDI Out para esses destinos, com o canal MIDI do MIDI Out
   (não filtrar por canal). Hoje `FlWrapper.cpp` descarta o registro `0x01`: ler os dois `i32` e guardar em
   `ForeignPlugin` (`midiInPort`, `midiOutPort`), mantendo o blob opaco para o *round-trip*.
2. **Pitcher em modo MIDI** (Bien Duro): quando o MIDI Out da mesma porta tiver notas, usar as notas como
   alvo de afinação (harmonização/correção para a nota MIDI). Se o LYNX não tiver isso, registrar no log "Pitcher
   segue o MIDI Out porta N" e passar o áudio reto.
3. **Keyboard Controller** → nova fonte de controle (como o Envelope Controller da rodada 15): `valor =
   teclas[últimaNota]`, suavizado; links `0xE3` com origem `canal<<16 | 0x8000`.
4. **Love Philter — parser.** Inflar zlib a partir do byte 4; versão = palavra alta se > 0xFFFF. Ler globais (6
   i32), bloco de 31/32/33 B, `u8 + u32` (v ≥ 2), texto (v < 6: i32+bytes; v ≥ 6: u8 + UTF-16), 8 unidades
   (12 i32, i32 modo, i32 tipo, 3 u8, envelope WS, u8, 8 articuladores) e o `u8` final (v ≥ 12). Articulador =
   `i32, 3 f32, 10 × (envelope + 4 i32), envelope LFO + 4 i32 (+1 se versão do LFO ≥ 3), 3 mapas`. Reaproveitar
   `readEnvelope` de `FlSytrus.cpp`. Validado em 96/96 blobs pelo `tmp/r16/lp.py`.
5. **Love Philter → efeitos do LYNX** (substitui o mapeamento cego `Fruity Love Philter → filter` de
   `FlpImport.cpp:91`): para cada unidade ligada, `filter`/`parametricEq` + `utility` como em §2.7.
   Escalas: cutoff pelo §2.5 com k = 1 (44,1 kHz); Q do LP/HP = `0,1 + 0,02495·(2001^r − 1)`; SVF: usar o tipo
   pelo maior de L/B/H e `Q ≈ 1/d` com `d = 1 − 0,9r`. Mais de uma unidade ligada com NEXT = 0 → `parallel`
   (cada ramo = unidade); NEXT ≠ 0 → em série, com o ganho NEXT no ramo. Unidade em solo → só ela.
6. **Love Philter — Mod X/Y e automação.** Guardar `x = ModX/1024`, `y = ModY/1024` e, por unidade, os mapas X/Y
   dos articuladores de **volume, cutoff, ressonância e pan** (os únicos usados nos demos). Na reprodução (ou
   na conversão de automação):
   - mapa **Y→volume** reta `0→1` (8 casos; *sidechain* por Envelope Controller em `k = 7`) → ganho do
     `utility` = `y` (com o link da rodada 15 dirigindo esse ganho);
   - mapa X→cutoff / Y→res retos → cutoff `c = p1/256 + (mapa(x) − 0,5)` (idem res);
   - mapa X→volume em degraus (gate) → ganho = `mapa(x)` avaliado na curva (Δx acumulado);
   - automação/link em `k`: 6/7 = Mod X/Y; `k = 8 + 44u + i` (i < 12) = parâmetro i da unidade u (tabela
     §2.4; normalizar o valor do link 0…1 para a faixa crua: ex. VOL `v = 160·valor`; CUT `256·valor`); `k ≥
     8 + 44u + 12` = LFO do articulador (pode ser ignorado: nenhum dado usa).
7. **Morphine → synth de 3 osciladores** (aproximação):
   - Ler `MPR0` (§3.1) e o bloco `0x5C4` (§3.2). Volume = `48·A0 − 24` dB; transpose `int((A5−0,5)·48)`; fine
     `int((A6−0,5)·200)`; **ADSR mestre** `A = 15·A9^3,5`, `D = 30·A10³`, `S = A11²`, `R = 10·A12³` s (mín.
     3 ms); `mono = A3 ≤ 1` **[HIP]**, `porta = A1 > 0` com tempo ∝ A1, bend range `int(12·A8)`.
   - Para cada gerador com algum quadro não nulo (até 3, pelo maior Mix): calcular o espectro médio dos quadros
     ponderado por `duração × ganho` e classificar: energia do 1º harmônico ≥ 0,7 e ≤ 6 harmônicos > 5 % →
     **seno**; ≥ 80 % da energia em ímpares → **quadrada** se a inclinação log-log ≥ −1,4, senão **triângulo**;
     caso contrário → **serra**. Nível do oscilador = Mix do gerador (`MVC0[0]`).
   - Brilho: centróide de energia do espectro `C`; uma serra ideal (1/n) dá `C ≈ 3,3`. Se `C < 3,3`, usar LP
     com *key tracking* `fc ≈ f0·8·C/3,3`; senão filtro aberto. Ruído (Noise Mix A36 > 0 e sem mute) →
     oscilador de ruído com LP em `35·2^(9·v40)` Hz **[HIP]**.
   - A evolução dos quadros (ganho por quadro) pode virar o envelope de filtro/volume do LYNX quando o ganho cai
     de forma monotônica (ex. "Lead Synth": 1,0 → 0,57). Registrar no log que o timbre é aproximado.
8. **Contagem de não suportados:** `Fruity Keyboard Controller` já é excluído do contador em
   `FlpImport.cpp:851`; incluir `MIDI Out` na mesma exceção (não é instrumento).

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| MIDI Out `FUN_006fde10` (VMT+3 de `TTestPlug` `0x6fd778`) | Layout v7: canal, banco, página, porta, flags, programa + 72 controles, definições, páginas, opções de reset | DECOMP |
| MIDI Out `FUN_006fec00` | Palavra de roteamento = canal + porta·2²⁴ | DECOMP |
| MIDI Out `FUN_006feb10`, `FUN_006fd900` | Program change (`0xC0`, programa−1), CC 0/32 de banco | DECOMP |
| MIDI Out `FUN_006fe4c0` | Padrões; canal inicial por contador global que pula o canal 10 | DECOMP |
| MIDI Out `FUN_006fdb80` | Host troca canal/porta (opcode 0x10) | DECOMP |
| DFM MIDI Out (`PortSelect`, `ChanSelect`, `BankMSB/LSB`, `PatchSelect`, `SendPanVolBox`, `MapColToChanBox`) | Nomes e faixas dos campos | STR |
| Bien Duro: MIDI Out porta 0/1 ↔ Pitcher p33 = 0,00638/0,0112 | Porta do Pitcher = `int(256·v) − 1`; roteamento por porta | DADOS |
| `scanw.py` (todo `Data`) | 23 wrappers, todos com portas −1; nenhum wrapper em canal | DADOS |
| Keyboard Controller `FUN_006fcff0` | Layout v0/v2/v3 (132 valores por tecla, suavização, note off) | DECOMP |
| Love Philter `FUN_008551e0` | Versão (palavra alta), zlib, globais, opções, texto, 8 unidades, byte final | DECOMP |
| Love Philter `FUN_00856780` | Padrões; numeração dos parâmetros (base 8 + 44u, 12/11 + 32) | DECOMP |
| Love Philter `FUN_008584e0`, `FUN_008579e0`, `FUN_00858100`, `FUN_00858680` | Índice → (unidade, articulador, campo); texto (ms do IEF `12,5·(81^(v/16384) − 1)`) | DECOMP |
| Love Philter `PTR_u_panning_008d19c0`, tabela `0x8d18c0` | Ordem dos 8 alvos de articulador; 12 nomes de filtro | DECOMP/STR |
| Love Philter `FUN_00858af0`, `FUN_00858e60`, `FUN_008590b0` | Escalas: `/256`, `/128`, `curve11`, Mod X/Y `/1024` | DECOMP |
| Love Philter `FUN_0085d110` + laço de processamento (`unidade+0x3C/+0x40`) | Entrada = IN + NEXT da unidade anterior | DECOMP |
| Love Philter `FUN_00859100` + constantes `0x85a740…` | Curvas de cutoff/Q por tipo | DECOMP (Hz HIP) |
| Love Philter `FUN_00855080` | Controles DQOver/HQ/SMOOTH/CENTER ↔ campos | DECOMP |
| 96 blobs Love Philter (`lp.py`) | Parse completo sem sobra, versões 2…12 | DADOS |
| `lpstat.py`, `lplinks.py` | Mapas X/Y usados; links/clipes em k = 6, 7, 9, 10, 17 | DADOS |
| Morphine `FUN_18010eb40`, `FUN_180113460`, `FUN_18012ad30`, `FUN_1800ec160`, `FUN_1800ef900`, `FUN_1800ef440` | Árvore `MPR0/MVC0/MZH0/MSP0/MBP0`, codificação dos parciais | DECOMP |
| Morphine `FUN_18010e340` (get) / `FUN_18010dc50` (set), `FUN_18010a2a0` (nomes), `FUN_180109ef0` (texto) | Índice → campo; escalas de nível, transpose, fine, EQ, delay, chorus | DECOMP |
| Morphine `FUN_1800fffa0`, `FUN_180100620` | ADSR mestre: `15v^3,5`, `30v³`, `v²`, `10v³` s | DECOMP |
| 7 canais + `.fst` Morphine (`mor.py`) | Árvore fecha; espectros médios | DADOS |

### Questões em aberto

- MIDI Out: campo `+8` (`P+0x124`, sempre −1); se o FL entrega as notas também a geradores nativos que tenham
  porta nas opções do wrapper nativo (onde isso fica salvo); o que "Map note color" faz com cores > 15.
- ZGameEditor Visualizer: onde guarda a porta MIDI (template "Video chopping", porta 1).
- Love Philter: fator `k` exato (`unidade+0xC4`, vem do objeto de oversampling) e se o SVF roda sobreamostrado;
  detalhes do Lime e do Mango; como o *alternate* espalha o cutoff entre estágios; a escala exata do
  deslocamento dos mapas bipolares (assumido ±0,5 em `c`); o LFO (período pela fórmula da rodada 11 §1.6?);
  ordem dos 3 bytes de menu do bloco global; texto info com ≥ 255 caracteres.
- Morphine: unidade de tempo dos quadros (`duração` relativa × *Gen Rate*); significado de `MBP0[1]` e das
  opções do `MVC0`/cauda do `MSP0`; zonas de teclado (`i32` = largura em teclas? soma ≠ 128 em "#4");
  `MML0`/`MMD0`/`MEG0` (morph X/Y e Eg1…4) não decodificados; papel da Polyphony = 0.
