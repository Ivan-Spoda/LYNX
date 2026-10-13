# Análise comportamental do FL Studio 2026: rodada 23 (REA) — espectros estáticos exatos: timbres das partes do Harmor e forma de onda do operador do Sytrus

> **Natureza deste documento.** Continuação de `REA_rodada11.md` (estados do Sytrus e do Harmor; descrição da
> "Shape" do operador no §1.4), `REA_rodada18.md` §1 e `REA_rodada19.md` §2 (Harmor). Engenharia reversa
> *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui
> são fórmulas e comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem
> só para rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> ou da teoria de sinais (ex.: "a série de Fourier de uma serra ±1 tem harmônica n de amplitude 2/(πn)") estão
> explicitadas como **inferência**.

- **Alvos:**

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave |
  |---|---|---|---|
  | Sytrus | `Sytrus_x64.dll` `d52a97c55e36c2457305ce4f2d6eed08aabd12dbec02c9f5f55ce8788e944f15` (= rodada 11, conferido contra a instalação) | Delphi (`TTestPlug`) | gerador da tabela de onda do operador `FUN_00847df0`, forma base `FUN_00847be0`, pré-filtro `FUN_00847900`, cópia do registro `FUN_008477f0`, *cache* de formas `FUN_00848780`, tamanho da tabela `FUN_00848a50`/`FUN_00848980`, curva de tensão `FUN_0060ec90`, conversão do pré-filtro `FUN_00869c90`, estado `FUN_00862d70`, despachante IPP `FUN_005bc580` |
  | Harmor | `Harmor_x64.dll` `23673ad84b11321c8133084e61ae53c3aa72e437586295083c3a06dd499d5aba` (= rodada 11/18, conferido) | Delphi (`TTestPlug`) | construção dos timbres `FUN_0088f310`, mistura T1/T2 `FUN_00885b70` + `FUN_00885af0`, EQ local `FUN_00889ec0`, tabela por eixo log `FUN_00888ed0`, razões das parciais `FUN_0088fcf0`, ordem de processamento `FUN_008737e0`, *scope* do timbre `FUN_0088f880`, timbre aleatório `FUN_00896c60`, construtor da parte `FUN_00870750`, formas de filtro `FUN_00874ed0`, filtro por voz `FUN_00886eb0`, amostragem de tabela `FUN_00874930`, *hint* do editor `FUN_008726e0`, `ProcessParam` `FUN_00879540`, voz `FUN_0088ab10`, banco de osciladores `FUN_00784840`/`FUN_00783ba0`; envelope genérico (`TMEnvelope`, VMT `0x79a0d8`): render em tabela `FUN_007a2640`/`FUN_007a26f0`, passo `FUN_007a1930`, valor do ponto `FUN_0079fef0`, coeficientes por ponto `FUN_0079e680`, nº de ondas `FUN_007a0070`, carga `FUN_0079a810`, constantes `FUN_00630170`, despachante IPP `FUN_005bd1c0` |

- **Ferramentas:** Ghidra 12.1.4 *headless* sobre cópias só-leitura dos projetos da rodada 11 (`/tmp/r23gh` e
  `/tmp/r23gh2`, copiados de `/tmp/r11gh_copy`/`_copy2`; programa `hm` = Harmor, `sy` = Sytrus, hashes acima);
  scripts em `tmp/r23/scripts/` (os das rodadas 18/19 + `DGR.java`, busca no decompilado restrita a uma faixa de
  endereços); `tmp/r18/k.py` para constantes; *dumps* em `tmp/r23/out/`; modelos numéricos nossos em Python puro
  (`tmp/r23/py/sywave.py`, `hspec.py`) e scripts de dados (`sy.py`, `hm.py`, `t2.py`…`t9.py`) sobre
  `tmp/r14/blobs14.pkl`. Manual oficial lido via web.
- **Data:** 2026-10-13.
- **Conjunto de dados** (canais em `Demo projects/**` do *pickle* da rodada 14): **99 canais Harmor** (20 arquivos,
  93 *blobs* distintos, **198 partes**; versões de estado v18 = 53, v19 = 44, v16 = 2) e **265 canais Sytrus**
  (1590 operadores). A contagem "36 canais" citada no pedido não foi reproduzida com este recorte; os números abaixo
  são deste recorte.

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/`.fst`/arquivos da instalação |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abreviada como `…/plugins/`). As citações do manual vieram de leitura web resumida; devem ser reconferidas na
página antes de virar texto de interface.

**Convenções de leitura.** Rotinas de biblioteca (iguais nas duas DLLs): `FUN_0040c090` = ln, `FUN_0040ad30` = exp,
`FUN_0040bb50` = seno, `FUN_0040ba60` = cosseno (mesma estrutura de quadrantes do seno, núcleo trocado —
**inferência** a partir do código), `FUN_0040c520` = arredondamento ao inteiro mais próximo **[DECOMP]**.
As chamadas indiretas vão para a biblioteca Intel IPP; os nomes vêm dos despachantes, que pareiam cada ponteiro com
a *string* do nome da função (Sytrus `FUN_005bc580`, Harmor `FUN_005bd1c0`) **[DECOMP]**: p. ex. Sytrus
`0x90d640` = `ippsTone_Direct_32f`, `0x90d3f0` = `ippsAbs_32f_I`, `0x90d0c8` = `ippsMulC_32f_I`; Harmor
`0x93a2c0` = `ippsSqr_32f_I`, `0x93a0e8` = `ippsAddC_64f_I`, `0x93a090` = `ippsMul_32f_I`, `0x93a070` =
`ippsSub_32f`, `0x93a6a8` = `ippsThreshold_LT_32f_I`, `0x93a278` = `ippsSin_64f_A26`. Constantes de *runtime* do
Harmor (`FUN_00630170`): `0x93b4f0` = ln 2, `0x93b4f8` = 1/ln 2, `0x93b53c` = ln 10 / 20 **[DECOMP]**.

**Convenção do LYNX** (`src/core/Song.h`, `src/core/Instruments.cpp`): o oscilador "spectral" soma
`(2/π)·spectrum[h]·sen(2π(h·t + spectrumPhase[h]))`, h = 1…256; logo `spectrum[h] = 1/h`, fase 0 é a serra ±1.
Neste documento **A(n)** é sempre a amplitude **física** do seno da harmônica n no plugin; para o LYNX,
`spectrum[n] = A(n)·π/2` reproduz o mesmo nível, e `spectrum[n] = A(n)` reproduz a mesma forma com nível ×2/π.

---

## 1. Harmor: espectro estático de uma parte

**O que o LYNX supõe** (`src/core/FlSytrus.cpp`, `synthFromHarmor`): cada parte = oscilador `saw`; nível =
`g(|p11|)·g(p12)·mix A/B`; corte/envelope/uníssono como nas rodadas 18/19. Os timbres desenhados, a fase por
harmônica, a mistura T1/T2 e o EQ local são ignorados.

### 1.1 Onde ficam as curvas no estado **[DECOMP `FUN_0086ed30`, `FUN_00870750` + DADOS]**

Layout do bloco da parte (complementa a rodada 11 §4.1; todos os itens em sequência, sem offsets fixos porque os
articuladores têm tamanho variável):

| Ordem | Conteúdo | Uso nesta rodada |
|---|---|---|
| 1 | 386 × i32 parâmetros (índice = Tag do DFM) | p4 *Timbre 1 & 2 mix* (0…1000), p5/p6/p7 *Sub-harmonic 1/3/4* (0…2048), p42/p43 *Harmonic detuning multiplicator/divider* |
| 2 | **0x30 B = 12 × i32: ordem de processamento** → parte `+0xA6C` | 192/198 partes = `0,1,…,11` (identidade) **[DADOS]** |
| 3 | 0x4B B de combos → parte (`+0x60`, `+0x668`, `+0x66C`, `+0x100`, `+0x104`, `+0xC0`, …) | **misc+0x00 = modo de mistura dos timbres** (`+0x60`); **misc+0x1C = modo das parciais** (`+0xE0`); misc+0x34/+0x38 = tipos de filtro 1/2 (rodada 18) |
| 4 | 43 articuladores (11 partes cada) | articulador 3 = *Timbre 1 & 2 mix* |
| 5 | **26 envelopes de "shaping"** (formato da rodada 11 §1.6, `kind` = 3) | na memória: envelope de índice `0x2B + s` em parte `+0x670 + 8·(0x2B + s)` |

Ordem dos 26 envelopes de *shaping* (combo `ArtSelect` do DFM, sem separadores) **[DFM + DECOMP]**:
**s = 0 Local EQ**, **1 Harmonic phase**, **2 Timbre 1 harmonic level**, **3 Timbre 2 harmonic level**,
4 Harmonic randomness, 5 Harmonic protection, 6 Harmonic clipping, 7 Harmonic prism, 8 Harmonic unison pitch,
9 Image formant mutation, 10/11 Filter shape 1/2, 12/13 Resonance shape 1/2, 14 Pluck shape, 15 Phaser shape,
16 Harmonizer distribution, **17 Global EQ**, 18–25 máscaras. Conferência no código: a rotina dos timbres lê o
envelope `0x2D + t` (t = 0/1) e o da fase lê `0x2C`; o EQ local lê `0x2B`; o EQ global (código de forma 300) lê
`0x3C` **[DECOMP `FUN_0088f310`, `FUN_00889ec0`, `FUN_00874ed0`]**.

Pontos de cada envelope (arquivo): `f64 Δx`, `f64 y`, `f32 tensão`, `u8 modo`, 2 B, `i8 sinal` (o último byte é
recalculado na carga como o sinal da tensão) **[DECOMP `FUN_0079a810`, `FUN_0079e680`]**.

**Valores padrão** (construtor `FUN_00870750`): Local EQ, T1, T2 e Global EQ = 0,25; Harmonic phase e
randomness = 0,5; demais = 1,0; o T2 padrão é carregado de um arquivo de envelope do sistema
(`System\….fnv`) **[DECOMP]**. Nos dados: T1 = reta 0,25 em 170/198 partes; Phase = reta 0,5 em 171; Local EQ =
reta 0,25 em 194; T2 = (0; 0,2685) → (1; 0) com modo 6 e tensão −0,164 em 195 **[DADOS]**.

**Centro do envelope.** O valor usado é `y − c`. Na construção, `c = 0,5` para os envelopes cujo bit está ligado na
máscara `0x19490` (índice − 0x28): bits 4, 7, 10, 12, 15, 16 → **Harmonic phase (0x2C) tem c = 0,5**; T1, T2,
Local EQ e Global EQ têm **c = 0** **[DECOMP `FUN_00870750`, `FUN_0079a7c0`, `FUN_0079fef0`]**.

### 1.2 Avaliação da curva (comum a todas as curvas de *shaping*) **[DECOMP `FUN_007a2640`, `FUN_007a1930`, `FUN_0079e680`, `FUN_007a0070`]**

- O comprimento total (soma dos Δx) é normalizado para o comprimento do envelope (1,0 nos dados) **[DECOMP
  `FUN_0079db40`; DADOS: Σ Δx = 1,0]**.
- No segmento do ponto j−1 ao ponto j, com `t ∈ [0, 1)` a posição relativa, o valor é
  **`y_{j−1} + f(t)·(y_j − y_{j−1})`**, onde `f` é dado pelo **modo e pela tensão T do ponto de chegada j**:

| Modo | f(t) |
|---|---|
| 0 (curva simples), T = 0 | `t` (linear) |
| 0, T > 0 | `1 − ((k+1)^(1−t) − 1)/k`, com **`k = 1000001^|T| − 1`** |
| 0, T < 0 | `((k+1)^t − 1)/k`, mesmo k |
| 2 (hold) | 0 (mantém `y_{j−1}`) |
| 5 (pulse) | `round(t·2(N+1) − σ/2) mod 2`, σ = sinal de T |
| 6 (wave), T < 0 | **`(sen(2π(t·(N + ½) + ¾)) + 1)/2`** |
| 6 (wave), T ≥ 0 | onda triangular `|2·frac' − 1|` com `N + ½` períodos (se N = 0 e T > 0: linear) |
| 7 | `t − (u⁶ − u)·T`, u = t (T ≤ 0) ou 1 − t (T > 0) |

  com **`N = round((1000001^(1 − 0,6|T|) − 1)·0,001)`** para os modos 3–6 (+1 no modo 3 com T ≥ 0). Exemplo do
  T2 padrão: T = −0,164 → N = 257 **[DECOMP + cálculo]**. Modos 1, 3, 4, 8–12 existem no código mas não aparecem
  nas curvas de timbre/fase dos demos (contagem dos modos dos pontos: 0 em 20 388 segmentos, 6 em 200, 7 em 2,
  5 em 1) **[DADOS]**; suas fórmulas não foram transcritas aqui.

### 1.3 Timbres 1 e 2 → nível por harmônica **[DECOMP `FUN_0088f310`; DOC]**

- A curva é amostrada em **516 pontos igualmente espaçados**: a harmônica **n (1…516) fica em `x = (n − 1)/515`**
  (eixo linear). O editor mostra o mesmo eixo: "Harmonic %d" com `n = round(515·x) + 1` **[DECOMP `FUN_0088f310`,
  `FUN_008726e0`]**. Curvas desenhadas a partir de ondas carregadas têm exatamente um ponto por harmônica (516
  pontos espaçados de 1/515 em 8 partes de T1) **[DADOS]**.
- Lei do nível: **`G_t(n) = (4·y_t(n))²`** (y = 0,25 → 1 = 0 dB; y = 1 → 16 = +24,1 dB; y = 0 → −∞). O editor
  mostra o mesmo valor em dB com `20·log10((4y)²)` **[DECOMP `FUN_0088f310` (fator 4,0 ao quadrado),
  `FUN_008726e0` + `FUN_0062e320`]**.
- A tabela tem 4 posições especiais antes das harmônicas (índices 0–3; a harmônica n fica no índice n + 3): índice 1
  repete o valor da harmônica 1, índices 2 e 3 são a curva lida nas posições fracionárias "harmônica 1,5" e
  "2,5" (configuração *Around fundamental*) **[DECOMP]**. São as subs, multiplicadas por `p5/1024`, `p6/1024`,
  `p7/1024` (`ProcessParam` casos 5–7, fator 1/1024) **[DECOMP `FUN_00879540`, `FUN_00885b70` → `FUN_00882670`]**;
  frequências das subs: 0,5·f, (1 + 0,5d)·f, (1 + 1,5d)·f (ou 0,25·f e 0,125·f em *Below fundamental*)
  **[DECOMP `FUN_0088fcf0`, `FUN_00870750`]**. Nos dados p5/p6/p7 ≠ 0 em só 5/2/1 partes **[DADOS]**.
- **Modo de mistura** (misc+0x00; combo *Timbre blending mode*): antes do *crossfade*, T2 é substituído por
  1 Subtract → `max(G₁ − G₂, 0)`; 2 Multiply → `G₁·G₂`; 3 Maximum; 4 Minimum (funções `FUN_00882f60`/`FUN_00882f90`,
  nomes pelo combo); 5 Pluck → T2 multiplicado por outra tabela e a mistura forçada a 0 (detalhe **não
  confirmado**); 0 Fade → sem mudança **[DECOMP `FUN_0088f310`, `FUN_00885b70`]**. Nos dados: 0 em 198/198
  **[DADOS]**.
- **Mistura** (*crossfade* linear nos ganhos): **`G(n) = (1 − m)·G₁(n) + m·G₂(n)`**, com
  **`m = min(1, (p4/1000)·A₃)`**, A₃ = saída do articulador 3 (*Timbre 1 & 2 mix*; base 1, multiplicativo); `m ≤ 0`
  → só T1, `m = 1` → só T2 **[DECOMP `FUN_00885b70`, `FUN_00885af0`; `ProcessParam` caso 4 = `p4·0,001`]**.
  Manual: "Standard cross-fade from Timbre 1 to Timbre 2" **[DOC `…/plugins/Harmor.htm`]**. Dados: p4 = 0 em
  167 partes, 1000 em 14, intermediário em 17 **[DADOS]**.

### 1.4 EQ local: aqui está a "calibração Browniana" 1/n **[DECOMP `FUN_00889ec0`, `FUN_00888ed0`, `FUN_008737e0`; DOC]**

- A curva *Local EQ* é amostrada em **eixo logarítmico**: a harmônica n fica em **`x = log2(n)/9`** (9 oitavas
  no comprimento da curva) **[DECOMP `FUN_00888ed0`: posição = ln(n)·(1/ln 2)·L/9]**.
- Com **misc+0x1C ≥ 1** (196/198 partes): **`E(n) = (4·y_L(log2(n)/9))² / n`**; as posições especiais usam
  divisores 1,5 e 2,5. Com misc+0x1C = 0 (2 partes): `E(n) = y_L²` sem divisão **[DECOMP]**.
- O EQ local é o módulo 0 da cadeia de processamento (`+0xA6C`); quando ele é o primeiro da cadeia (192/198
  partes) é multiplicado direto na tabela do timbre; senão é aplicado na sua posição da cadeia (`FUN_00886E00`
  multiplica pela mesma tabela) — em ambos os casos o ganho por harmônica é o mesmo **[DECOMP `FUN_008737e0`,
  `FUN_0088f310`, `FUN_00886e00`]**.
- Logo, com Local EQ padrão (0,25): `E(n) = 1/n` e **uma linha reta de timbre gera a serra**, como diz o manual:
  "Envelope values are scaled relative to the slope of the Brownian noise spectrum" e "Timbre 1 and Timbre 2
  generate a Saw and Square wave respectively" **[DOC `…/plugins/Harmor.htm`]**. Conferência indireta: a função
  "timbre aleatório" normaliza a energia por `Σ (g/n)²` e grava `y = √g/4`, isto é, trata a amplitude física como
  `g/n` **[DECOMP `FUN_00896c60`]**.
- misc+0x1C também escolhe as razões das parciais (§1.5); seu nome na interface **não foi identificado**.

### 1.5 Frequências das parciais **[DECOMP `FUN_0088fcf0`, `ProcessParam` casos 42/43]**

- `d = p42/(p43·10000)` (padrão 10000/1 → 1,0; 196/198 partes) **[DECOMP + DADOS]**.
- misc+0x1C ≥ 1: **`r(n) = 1 + (n − 1)·d`** (harmônicas exatas com d = 1; "detuning" esticando a série como no
  Harmless). misc+0x1C = 0: `r(n) = 2^((n−1)·d/8)` (parciais espaçadas de 1/8 de oitava) **[DECOMP]**.
- O número de parciais por voz é limitado por uma frequência máxima (`FUN_0088fbc0`); o valor desse limite **não
  foi tabelado** (para 256 harmônicas no LYNX só importa em notas agudas).

### 1.6 Fase por harmônica **[DECOMP `FUN_0088f310`, `FUN_0088ab10`, `FUN_00784840`, `FUN_00783ba0`; DOC]**

- A curva *Harmonic phase* é amostrada no mesmo eixo linear (`x = (n − 1)/515`), menos o centro 0,5, mais 1,0
  (`ippsAddC_64f_I`), e copiada como fase inicial (em ciclos) de cada oscilador da voz **[DECOMP]**.
- O banco de osciladores soma `A·sen(2π·frac(fase))` com a fase avançando `f/fs` por amostra (`ippsSin_64f_A26`
  sobre `2π·(fase − round(fase))`) → **unidade = ciclos, convenção seno** **[DECOMP `FUN_00783ba0`, `FUN_00784840`]**.
- Portanto **`φ(n) = (y_φ((n − 1)/515) − 0,5) mod 1`** ciclos, compartilhada pelos dois timbres ("they share the
  same harmonic phase information" **[DOC `…/plugins/Harmor.htm`]**). Padrão (0,5) → φ = 0.
- Há um ramo que soma fase aleatória (`ippsRandUniform_Direct_64f`) na partida da nota; o parâmetro que o liga
  **não foi identificado** (não confirmado).

### 1.7 Fórmula final (caso estático, sem modulação, sem imagem)

Para a harmônica n = 1…256 de uma parte (antes de volume da parte/articulador de volume das rodadas 18/19):

**`A(n) = [(1 − m)·(4y₁(xₙ))² + m·(4y₂(xₙ))²] · (4y_L(log2(n)/9))² / n · EQg(fₙ) · F(fₙ)`**,
**`φ(n) = (y_φ(xₙ) − 0,5) mod 1`**, com `xₙ = (n − 1)/515`, misc+0x1C = 1 e modo de mistura 0.

`EQg` = EQ global (envelope 0x3C; tabela por frequência absoluta, padrão 0,25 → ganho `(4·0,25)² = 1` na faixa
útil; o mapeamento exato em Hz e as rampas nas pontas **não foram transcritos**) **[DECOMP `FUN_00874ed0` código
300]**; `F` = filtros (§1.8). Módulos não lineares ou dependentes do tempo (clipping, blur, prism, harmonizador,
phaser, pluck, imagem) ficam fora — valores dos demos no §1.9.

**Verificação nos dados** (modelo `tmp/r23/py/hspec.py`; os valores são A(n) antes de filtros/volume) **[DADOS +
fórmula acima]**:

| Parte (arquivo · canal · parte) | Curvas | A(1…8) | φ(1…4) |
|---|---|---|---|
| Astes – Bien Duro · 4.6 Lead · 0 (138 partes iguais a este caso) | padrão, m = 0 | 1; 0,5; 0,3333; 0,25; 0,2; 0,1667; 0,1429; 0,125 (= 1/n, serra) | 0; 0; 0; 0 |
| Right Night feeling · Harmor Bell · 1 | padrão, p4 = 1000 (só T2) | 1,1535; 0; 0,3845; 0; 0,2307; 0; 0,1648; 0 (quadrada: ímpares `1,1535/n`) | 0… |
| Bien Duro · 4.4 Glass pluck · 0 | T padrão, fase desenhada, p4 = 332 | 1,051; 0,334; 0,3503; 0,167; 0,2102; 0,1113; 0,1501; 0,0835 | 0,734; 0,723; 0,975; 0,986 |
| Bien Duro · 4.5.2 Wasp high · 0 | T1 e fase desenhados (516 pontos), m = 0 | 0,9888; 0,552; 0,3607; 0,255; 0,1965; 0,1544; 0,13; 0,1118 | 0,968; 0,938; 0,043; 0,179 |
| Bien Duro · 4.6 Lead · 1 | T1 desenhado (y₁(1) = 0,5) | 4,0; 1,9652; 1,2871; 0,9483; 0,7451; 0,6097; 0,5131; 0,4407 | 0… |

Classificação das 198 partes: T1/fase/Local EQ padrão com m = 0 em **138**; T1 desenhado em 28; fase desenhada em
27; Local EQ desenhado em 4; p4 > 0 em 31 **[DADOS `tmp/r23/py/t9.py`]**.

**Inferência (Fourier):** como `Σ sen(nx)/n` tem pico π/2 e `Σ_ímpares sen(nx)/n` tem pico π/4, a serra padrão
(`1/n`) tem pico ≈ 1,571 e a quadrada padrão (ímpares `1,1535/n`) tem pico `1,1535·π/4 ≈ 0,906` — os níveis das
duas formas padrão não são iguais.

### 1.8 Formas dos filtros 1/2 por harmônica (caso estático) **[DECOMP `FUN_00874ed0`, `FUN_00874930`, `FUN_00886eb0`, `FUN_00874240`, `FUN_00874d60`, `FUN_00874d30`]**

- Cada forma é uma tabela `T[i]`, i = 0…N−1, com **posição de origem `o`** e extensões `(eL, eR)` (usadas nas margens
  da faixa de corte, rodada 18 §1.2). O ganho da parcial de frequência `fₙ` é
  **`F(fₙ) = T(o + (log2 fₙ − L)·s)`**, com interpolação linear entre entradas, `T[0]` abaixo de 0 e `T[N−1]` acima
  do fim; **`s = 1024·2^(1 − 0,004·p54·A_w)`** entradas por oitava (p66 no filtro 2; A_w = articulador
  6/7 *Filter width*, base 1); L = log2 do corte da voz (rodada 18 §1.2 e rodada 19 §2.1). Com p54 = 500 (155/198
  partes): s = 512/oitava **[DECOMP]**. Inferência: `s = 682,67/W` com o W da rodada 18.
- Tabelas (x = i/(N−1); "dB" via `ln 10/20`):

  | Tipo | N | T[i] | o | (eL, eR) |
  |---|---|---|---|---|
  | 0 Classic LP | 2048 | `10^(−72x/20)·(1 − x¹⁶)` | 0 | (1, 0) |
  | 1 Classic BP | 4096 | a mesma lei com `x = 1 − |1 − 2·((i + N/2) mod N)/N|` (pico no centro) | (N−1)/2 | (1, 1) |
  | 2 Classic BS | 4096 | `1 −` (lei do BP) | (N−1)/2 | (1, 1) |
  | 3 Classic HP | 2048 | lei do LP com `x → 1 − x` | N−1 | (0, 1) |
  | 4 Crude LP | 1024 | **`cos⁴(πx/2)`** | 0 | (1, 0) |
  | 5 Crude BP | 2048 | `sen⁴(πx)` | (N−1)/2 | (1, 1) |
  | 6 Crude BS | 2048 | `cos⁴(πx)` | (N−1)/2 | (1, 1) |
  | 7 Crude HP | 1024 | `sen⁴(πx/2)` | N−1 | (0, 1) |

  (As formas 4–7 usam o cosseno da biblioteca — ver convenções.) As formas 8–18 (Hill, Leg, Hybrid, Snake, Fin,
  Leaky, phasers, Flat BP, Custom) **não foram transcritas**. As tabelas passam por um corte de zeros
  (`FUN_00629eb0`) e uma remoção de trechos constantes nas pontas que só ajusta `o` (`FUN_00874e40`) **[DECOMP]**.
- **Dependência da nota:** `log2 fₙ − L = log2 n + log2 f₀ − L₀ − KT·(log2 f₀ − log2 130,81)`. Só com **KT = 1** o
  ganho vira função de n apenas; nos dados **p55 (KT) = 0 em 182/198 partes** (KT = 1 em 7) **[DADOS]** — o
  filtro do Harmor nos demos é fixo em Hz e **não** pode ser embutido num espectro estático por canal.
- A ressonância multiplica por `2^(r·s(x))` (rodada 19 §2.2, **inferência** sobre o `exp`) e não entra no caso
  estático com p57 = 0.

### 1.9 Uníssono, *prism*, *blur* e outros nos demos **[DFM + DADOS; leis não confirmadas]**

| Parâmetro (Tag) | Padrão (DFM) | Demos (198 partes) | Efeito no espectro estático |
|---|---|---|---|
| p34 *Unison order* | 1 | 1 em 120; 2 em 31; 3 em 15; 9 em 11… | cópias desafinadas (rodada 18 §1.3); cada cópia tem o mesmo A(n) |
| p26 *Prism amount* | 0 | 0 em 175 | desloca as frequências das parciais; com 0, sem efeito (**inferência** pelo nome/curva); lei **não confirmada** |
| p23 *Harmonic blur amount*; p21/p22 *Time blur* | 0 | p23 = 0 em 189; p21 = 0 em 197; p22 = 0 em 198 | lei **não confirmada** |
| p27 *Harmonizer mix* | 0 | 0 em 161 | **não confirmado** |
| p75 *Pluck decay* | — | 1000 em 170 | rodada 11: 1000 = desligado |
| p8 *Harmonic protection*, p9 *Clipping threshold* | 256 / 1024 | p8 = 0 em 188; p9 = 1024 em 188 | `ProcessParam`: p8 → `(e^{c·v/1024} − 1)·c'`; efeito **não confirmado** |
| imagem (ressíntese) | — | presente em partes de "Square a Saw – … Resynthesized" (não contado) | multiplica A(n) por uma coluna da imagem que varia no tempo (`FUN_00882850`) — fora do caso estático |

---

## 2. Sytrus: forma de onda do operador

**O que o LYNX supõe** (`synthFromSytrus` + `waveOf`): tabela só com a fundamental → `sine`/`triangle`/`saw`/`square`
por faixas de Shape; tabela rica com forma padrão → oscilador "spectral" com `spectrum = |a|·π/2` e
`spectrumPhase = ph·0,5 (+0,5 se a < 0)`.

### 2.1 Onde ficam os campos **[DECOMP `FUN_00862d70`, `FUN_008477f0`, `FUN_00847df0`]**

O bloco do operador (0x828 B, rodada 11 §1.4) é lido para o registro da unidade em `+0x40`; o gerador de forma copia
`+0x4C…` para o seu objeto. Correspondência (offsets do bloco):

| Bloco | Campo | Objeto |
|---|---|---|
| 0x00C | 256 × f32 amplitudes a_k | +0x34 |
| 0x40C | 256 × f32 fases p_k | +0x434 |
| 0x80C…0x81C | Shape, Tension, Skew, Sine shaper, Pre-filter (i32) | +0x834…+0x844 |
| 0x820 / 0x821 / 0x822 | **Half / Even ("half silent") / Absolute** | +0x848 / +0x849 / +0x84A |
| — | byte global `P+0x4F6` copiado para cada operador na carga | +0x84B (**janela de Gibbs**, §2.6) |

Os bytes 0x823–0x827 do bloco não são lidos pelo gerador de forma **[DECOMP]** (seu significado continua
**não confirmado**). Nomes dos controles: DFM `OpPanel` (`ShapeHalfBtn` "Half", `ShapeHalfSilentBtn` "Even",
`ShapeAbsBtn` "Absolute", `ShapeShapeWheel` 0…512, `ShapeTensionWheel` ±128, `ShapeSkewWheel` 0…128,
`ShapeSineWheel` 0…128, `ShapeFilterWheel` 0…128 padrão 128) **[STR]**. Há também `ShapeNoiseWheel` ("Noise"),
cujo campo **não foi localizado** no gerador.

### 2.2 Tabela e passos **[DECOMP `FUN_00848a50`, `FUN_00848980`, `FUN_00847df0`]**

- A forma é calculada numa tabela de **N = 2¹⁴ = 16 384 amostras** por ciclo (mais uma amostra de guarda) e guardada
  num *cache* compartilhado **[DECOMP]**.
- Ordem: (1) soma das parciais com a forma base; (2) normalização de pico; (3) Tension; (4) Absolute;
  (5) Pre-filter; (6) zera valores com |v| < 5,96·10⁻⁸ **[DECOMP]**.

### 2.3 A forma base B por Shape (`FUN_00847be0`) **[DECOMP + verificação numérica]**

Escrevendo θ ∈ [0, 1) a fase em ciclos e `x = Shape/128`:

| Faixa | m (peso da forma "w") | d (fração de subida) | t (trapézio) | p (pulso) |
|---|---|---|---|---|
| x ≤ 1 | x | 0,5 | 0 | 0 |
| 1 < x ≤ 2 | 1 | (2 − x)/2 | 0 | 0 |
| 2 < x ≤ 3 | 1 | 0 | x − 2 | 0 |
| x > 3 | 1 | 0 | 1 | x − 3 |

- **p > 0 (pulso):** `B = +1` para `θ < D`, `−1` caso contrário, **`D = (1 − 0,75p)/2`** (512 → 12,5 %).
- **p = 0:** `B = (1 − m)·sen(2πθ) + m·w(θ)`, com `u = (θ + d/2) mod 1` e
  `w = 2u/d − 1` se `u < d` (subida de −1 a +1), senão `w₀ = 1 − 2(u − d)/(1 − d)` (descida de +1 a −1);
  com t > 0 (só quando d = 0): **`w = ±t + (1 − t)·w₀`**, sinal + para `u < ½`, − para `u ≥ ½`.
  Casos: 0 = seno; 128 = triângulo (zero em θ = 0, pico em ¼); 256 = **serra descendente** `1 − 2θ`;
  384 = quadrada (+1 na primeira metade).
- **Séries de Fourier** (**inferência**, conferida contra o modelo numérico do código para Shape = 0, 64, 128, 160,
  192, 256, 300, 384, 448, 512 com erro < 10⁻⁴ — `tmp/r23/py/test1.py`): `B = Σ_j b_j·sen(2πjθ)` com
  - triângulo deslizante (0 < d ≤ ½): `tri_j = 2·sen(πjd)/(π²j²·d(1 − d))`; d → 0: `2/(πj)` (serra);
  - quadrada: `sq_j = 4/(πj)` nas ímpares;
  - **`b_j = m·[(1 − t)·tri_j + t·sq_j] + (1 − m)·[j = 1]`** (o sinal de `b_j` vira fase ½);
  - pulso: `B = (2D − 1) + Σ (2/(πj))·[(1 − cos 2πjD)·sen 2πjθ + sen 2πjD·cos 2πjθ]`.

### 2.4 Soma das parciais, Half/Even, Skew e Sine shaper **[DECOMP `FUN_00847df0`]**

- **Normal:** `W(θ) = Σ_{k=1}^{256} a_k · S(B(c_k))`, com **`c_k = frac(k·θ + p_k)`** — a fase da tabela de
  harmônicas está **em ciclos da própria harmônica** (o código usa o período 2 e soma `2·p_k`). Parciais com
  `a_k = 0` são puladas; a negativo inverte.
- **Half:** cada harmônica percorre só a **primeira metade** de B esticada a um ciclo inteiro:
  `B(c/2)` com `c = frac(k·θ + 2p_k)` (a fase passa a valer 2p_k ciclos).
- **Even (half silent):** um ciclo inteiro de B na primeira metade do ciclo da harmônica e **zero** na segunda:
  `c = frac(k·θ + p_k/2)`; `B(2c)` se `c ≤ ½`, senão 0. **Half + Even:** `B(c')` com c' na primeira metade e zero na
  segunda.
- **Skew** (0…128): `a_s = (21^(Skew/128) − 1)/20`, `k_s = 10⁹^(a_s) − 1`; a fase local `φ = 2c ∈ [0, 2]` é
  dobrada em torno do centro 1: `δ = |φ − 1|`, **`δ' = 1 − ((k_s + 1)^(1−δ) − 1)/k_s`**, `φ' = 1 ∓ δ'` (− para
  φ ≤ 1), e B é avaliada em φ'/2. Skew = 0 → sem efeito.
- **Sine shaper** (0…128): **`S(v) = sen(v · 1,5π · SineShaper/128)`** (constante 0,0368155 = 1,5π/128) aplicado
  ao valor de cada parcial antes de multiplicar por a_k; 0 → `S(v) = v`.
- Com Shape = Skew = Sine = 0 e sem Half/Even, o código usa um gerador de seno direto (`ippsTone_Direct_32f`, que é
  cosseno, com fase `(2p_k + 1,5)·π`) — **equivalente** a `sen(2π(kθ + p_k))` **[DECOMP + identidade cos(x + 3π/2)
  = sen x]**. Isso confirma a unidade da fase.

### 2.5 Normalização, Tension e Absolute **[DECOMP `FUN_00847df0`, `FUN_0062a570`, `FUN_0060ec90`]**

- **Pico:** `P = max|W|` (`FUN_0062a570`); se `P > 5,96·10⁻⁸`, **`W ← W/P`** — a onda do operador sai sempre com
  pico 1, qualquer que seja a soma das amplitudes **[DECOMP]**.
- **Tension** (±128): `a_T = (21^(|T|/128) − 1)/20`, **`k_T = sinal(T)·(K^(a_T) − 1)`** com K = 9,99999984·10¹⁷
  (≈ 10¹⁸, constante em `0x848758`); cada amostra vira `sinal(v)·C(|v|)` com
  `C(u) = 1 − ((k+1)^(1−u) − 1)/k` (k > 0) ou `C(u) = ((|k|+1)^u − 1)/|k|` (k < 0) **[DECOMP]**.
- **Absolute:** `W ← |W|` (`ippsAbs_32f_I`), depois da tensão **[DECOMP]**.

### 2.6 Pre-filter e "Gibbs" **[DECOMP `FUN_00847900`, `FUN_00869c90`]**

- **`M = round((10^(10·(0,65 + 0,35·Pre/128)) − 1)·1,6384·10⁻⁶)`**: Pre = 0 → 5; 25 → 25; 64 → 291;
  96 → 2185; 116 → 7697; **≥ 117 → > 8192 = sem filtro** (padrão 128 → 16 384).
- Se `M ≤ N/2`: FFT real da tabela (formato "Pack", **inferência** pelo padrão de índices), **zera as harmônicas
  ≥ M** (mantém DC e 1…M−1) e, se o byte do objeto `+0x84B` estiver ligado, multiplica a harmônica j por
  **`sinc(j/(M − 1)) = sen(πj/(M−1))/(πj/(M−1))`** (fatores σ de Lanczos); FFT inversa **[DECOMP]**.
- `+0x84B` vem do byte global `P+0x4F6` (gravado no estado a partir da v13) **[DECOMP `FUN_00862d70`]**; a
  associação desse byte ao botão global **GIBBS OFF** ("Attenuate ringing ripples", DFM) é **inferência** (o efeito
  do código é exatamente atenuar o fenômeno de Gibbs). Nos demos: 1 em 24/265 canais **[DADOS]**.

### 2.7 Verificação nos dados **[DADOS + modelo `tmp/r23/py/sywave.py`]**

Nos 1590 operadores dos demos: Shape/Tension/Skew/Sine/Pre = (0,0,0,0,128) em 1235; Shape 256 em 95; 128 em 21;
384 em 11; Tension/Skew/Sine/Pre fora do padrão em 138; Half em 3, Even em 3, Absolute em 5. Tabelas ricas: 124
com forma padrão, 87 com forma não padrão **[DADOS `t2.py`]**. Fases armazenadas: distribuídas uniformemente em
[0, 1] (3314 zeros; máximo 1,0) — compatível com **ciclos**, não "meio ciclo" **[DADOS]**. Exemplos (A = amplitude
do seno de cada harmônica da onda do operador, já normalizada; fase em ciclos):

| Operador | Forma | A(1…6) | φ(1…3) |
|---|---|---|---|
| Steptime · Angela · op 2 | Pre = 25 (M = 25), Gibbs = 1, tabela 1; 0,395; 0,131… | 0,8532; 0,3342; 0,1095; 0,046; 0,0234; 0,0139 (harmônicas ≥ 25 = 0) | 0,272; 0,045; 0,82 (= p_k) |
| With You · Synth Pad · op 6 | Shape 128 (triângulo) sobre a₁ = 1, a₈ = 0,574… | 0,5608; 0; 0,0623; 0; 0,0224; 0,0015 | 0; ½; ½ |
| Exoplanet · Cello · op 1 | Tension 6, Sine 47, Even | 0,0689; 0,1421; 0,0938; 0,1477; 0,0668; 0,1617 | 0,553; 0,566; 0,574 |

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Harmor: onde estão as curvas | 26 envelopes de *shaping* no fim do bloco da parte; s = 0 Local EQ, 1 Harmonic phase, 2/3 Timbre 1/2 level, 17 Global EQ; memória `+0x670 + 8(0x2B+s)` **[DECOMP+DFM]** |
| 1b | Harmor: eixo x | timbres e fase: **harmônica n em x = (n−1)/515** (516 pontos); Local EQ: **x = log2(n)/9** **[DECOMP]** |
| 1c | Harmor: lei do nível | **`(4y)²`** (0,25 = 0 dB); EQ local `(4y_L)²/n` = calibração Browniana; flat → serra **[DECOMP+DOC]** |
| 1d | Harmor: mistura | `G = (1−m)G₁ + mG₂`, `m = min(1, p4/1000·A₃)`; modos Subtract/Multiply/Max/Min alteram G₂ antes; 198/198 = Fade **[DECOMP+DADOS]** |
| 1e | Harmor: fase | `φ(n) = (y_φ − 0,5) mod 1` ciclos, convenção seno, comum aos dois timbres **[DECOMP+DOC]** |
| 1f | Harmor: curvas | interpolação pelo modo/tensão do ponto de chegada; tensão `k = 1000001^|T| − 1`; modo 6 com `N + ½` ondas, `N = round((1000001^(1−0,6|T|) − 1)/1000)` → T2 padrão = quadrada exata (N = 257) **[DECOMP]** |
| 1g | Harmor: parciais | `r(n) = 1 + (n−1)·p42/(p43·10⁴)` (misc+0x1C = 1; 196/198) **[DECOMP+DADOS]** |
| 1h | Harmor: filtros | `F = T(o + (log2 f − L)·1024·2^(1−0,004 p54))`; tabelas 0–7 transcritas (Classic: −72 dB·x·(1−x¹⁶); Crude: cos⁴/sen⁴) **[DECOMP]**; KT = 0 em 182/198 → dependem da nota **[DADOS]**; formas 8–18 **não confirmadas** |
| 1i | Harmor: prism/blur/uníssono/harmonizer | valores neutros na maioria dos demos **[DADOS]**; leis **não confirmadas** |
| 2a | Sytrus: forma base | tabela §2.3 (seno→triângulo→triângulo deslizante→serra→trapézio→quadrada→pulso `D = (1−0,75p)/2`) + séries de Fourier fechadas **[DECOMP + inferência verificada]** |
| 2b | Sytrus: soma | `Σ a_k·S(B(frac(kθ + p_k)))`, Half/Even/Skew/Sine shaper como §2.4; tabela de 16 384 amostras **[DECOMP]** |
| 2c | Sytrus: pós-soma | normaliza pico a 1; Tension `K = 10¹⁸`; Abs; Pre-filter `M` e janela σ (Gibbs) **[DECOMP]** |
| 2d | Sytrus: fase da tabela de harmônicas | **ciclos da harmônica** (não meio ciclo); Half → 2p, Even → p/2 **[DECOMP+DADOS]** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Sytrus (`synthFromSytrus`, bloco do oscilador "spectral", `FlSytrus.cpp` ~l. 525–543):**
   - **Fase:** trocar `spectrumPhase = ph·0,5 (+0,5 se a < 0)` por **`spectrumPhase = ph (+0,5 se a < 0)`** (fase em
     ciclos da harmônica).
   - **Nível:** a onda do operador do FL tem pico 1. Calcular `P = max_θ |Σ a_k·sen(2π(kθ + p_k))|` e usar
     **`spectrum[k] = |a_k|/P · π/2`**.
   - **Generalizar** o caminho exato para qualquer forma: gerar a tabela de 16 384 amostras com §2.3–§2.6 (forma
     base, Half/Even, Skew, Sine shaper, normalização, Tension, Absolute, Pre-filter/Gibbs) e tirar as harmônicas
     1…256 por DFT (amplitude `|c_k|`, fase `arg(c_k)/2π + ¼` para a convenção seno do LYNX); descartar o DC.
     Isso substitui as faixas de Shape de `waveOf` também para a tabela "só fundamental" (Shape 256 = serra
     descendente exata, 384 = quadrada, 448–512 = pulsos 31,25 %…12,5 % com DC).
   - Exigir também Half/Even/Absolute = 0 no atalho atual "forma padrão" (hoje só `form` é conferido).
2. **Harmor (`synthFromHarmor`):** substituir a serra pelo oscilador "spectral" com
   **`A(n) = [(1−m)(4y₁(xₙ))² + m(4y₂(xₙ))²]·(4y_L(log2 n/9))²/n`**, **`φ(n) = (y_φ(xₙ) − 0,5) mod 1`**, `xₙ = (n−1)/515`,
   `m = min(1, p4/1000)` (articulador 3 ignorado no caso estático), avaliando as curvas pelo §1.2 (modos 0, 2, 5, 6,
   7; registrar no log os demais). `spectrum[n] = A(n)·π/2` mantém o nível físico do Harmor; `spectrum[n] = A(n)`
   mantém a convenção "serra = 1/n" do LYNX (−3,9 dB).
   - Aplicar os modos de mistura 1–4 (§1.3) antes do *crossfade*; com modo 5, usar só T1.
   - Só aplicar o caminho espectral quando misc+0x1C = 1 e p42/p43 = 10000/1 (196/198); senão registrar no log.
   - Manter o filtro do LYNX (KT = 0 na maioria): o espectro estático **não** deve incluir F(fₙ). Para KT = 1 (7
     partes), a forma do filtro pode ser embutida com o §1.8 (só tipos 0–7).
   - Subs (p5–p7) não cabem na série harmônica do LYNX (f/2, 1,5f, 2,5f); registrar no log quando ≠ 0.
3. Manter: corte, envelope de corte, volume da parte, uníssono (rodadas 18/19).

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| Sytrus `FUN_00847df0` (constantes `0x848700…0x848770`: 1, 2, 3, 21, 1, 0,05, 10⁹, 1,5π/128, 2, 1,5, π, ≈10¹⁸; `PTR 0x8ff8c0` = 1/128) | soma das parciais, Half/Even, Skew, Sine shaper, pico, Tension, Abs | DECOMP |
| Sytrus `FUN_00847be0` (0, 1, 0,75, π, 2, 1) | forma base por Shape | DECOMP |
| Sytrus `FUN_00847900`, `FUN_00869c90` (0,35, 0,65, 10¹⁰, 1, 1,6384·10⁻⁶, π) | Pre-filter M e fatores σ | DECOMP |
| Sytrus `FUN_008477f0`, `FUN_00862d70` (registro `+0x40` = bloco; `+0x868` ← `P+0x4F6`) | campos do bloco → objeto | DECOMP |
| Sytrus `FUN_00848a50`/`FUN_00848980` (ordem 14) | tabela de 16 384 amostras | DECOMP |
| Sytrus `FUN_0060ec90`, `FUN_0062a570`, `FUN_00628bc0` | curva de tensão; pico; corte de denormais | DECOMP |
| Sytrus `FUN_005bc580` | nomes IPP (`ippsTone_Direct_32f`, `ippsAbs_32f_I`, `ippsMulC_32f_I`…) | DECOMP |
| DFM Sytrus `OpPanel` | nomes/faixas Half, Even, Absolute, Shape, Tension, Skew, Sine, Pre-filter, Noise; GIBBS OFF | STR |
| 265 canais Sytrus (`tmp/r23/py/sy.py`, `t2.py`, `t3.py`) | distribuição de formas/flags/fases; espectros de exemplo | DADOS |
| Modelo `tmp/r23/py/sywave.py` + `test1.py` | séries de Fourier fechadas = modelo do código | inferência verificada |
| Harmor `FUN_0086ed30`, `FUN_00870750` (máscara `0x19490`, padrões 0,25/0,5/1,0) | layout da parte, centros e padrões das curvas | DECOMP |
| Harmor `FUN_0088f310` (516 amostras, 4,0², `ippsAddC_64f_I` 1,0, modos de mistura) | timbres e fase | DECOMP |
| Harmor `FUN_00885b70`, `FUN_00885af0`, `ProcessParam` caso 4 (×0,001) | *crossfade* T1/T2 | DECOMP |
| Harmor `FUN_00889ec0`, `FUN_00888ed0` (9 oitavas, 1/ln 2, divisão por n) | EQ local com 1/n | DECOMP |
| Harmor `FUN_008737e0`, `FUN_00886e00`, `FUN_0088f880` | cadeia de processamento; EQ local sempre aplicado; *scope* = T × EQ local | DECOMP |
| Harmor `FUN_0088fcf0`, `ProcessParam` casos 42/43 | razões das parciais | DECOMP |
| Harmor `FUN_00896c60` | normalização `Σ(g/n)²` e `y = √g/4` | DECOMP |
| Harmor `FUN_008726e0` (515, 4, 0,5), `FUN_0062e320` (20·log10) | eixo e escala mostrados no editor | DECOMP |
| Harmor `FUN_007a2640`, `FUN_007a1930`, `FUN_0079fef0`, `FUN_0079e680` (1000001, 0,5, 2π), `FUN_007a0070` (1000001, 0,6, 0,001), `FUN_0079a810`, `FUN_0079db40` | avaliação das curvas | DECOMP |
| Harmor `FUN_00784840`, `FUN_00783ba0` (2π), `FUN_00784740` | banco de senos, fase em ciclos | DECOMP |
| Harmor `FUN_00874ed0` (2048/4096/1024, −72, ln10/20, cos/sen), `FUN_00874d60`, `FUN_00874d30`, `FUN_00874930`, `FUN_00886eb0`, `FUN_00874240` (0,004, 2/3) | formas dos filtros e amostragem por log-frequência | DECOMP |
| Harmor `FUN_00630170` (ln 2, 1/ln 2, ln 10/20) | constantes de *runtime* | DECOMP |
| Harmor `FUN_005bd1c0` | nomes IPP | DECOMP |
| 198 partes Harmor (`tmp/r23/py/hm.py`, `t5.py`…`t9.py`, `hspec.py`) | padrões, distribuições, espectros de exemplo | DADOS |
| Manual Harmor | serra/quadrada por padrão, calibração Browniana, fase compartilhada, *crossfade* | DOC `…/plugins/Harmor.htm` |

### Questões em aberto

- Harmor: fórmulas dos modos de curva 1, 3, 4, 8–12 (não usados nas curvas de timbre/fase dos demos); nome de
  interface de misc+0x1C; o modo de mistura 5 (*Pluck*) em detalhe; mapeamento em Hz e rampas do EQ global
  (código 300); formas de filtro 8–18; o parâmetro que liga a fase aleatória na partida da nota; leis de *prism*,
  *blur*, *harmonic protection/clipping*, harmonizador e *unison pitch* por harmônica; a frequência máxima que limita
  o número de parciais; a parcial especial de índice 0; contribuição da imagem (ressíntese) por parte; calibração
  absoluta de saída do Harmor frente aos outros geradores.
- Sytrus: campo do *Noise* do operador e bytes 0x823–0x827 do bloco; confirmação na interface de que `P+0x4F6` é o
  botão GIBBS OFF; *band-limit* em tempo real (botão BAND LIMIT) e seu efeito sobre harmônicas acima de N/2 na
  tabela de 16 384 amostras (o gerador não limita a banda antes da soma).
