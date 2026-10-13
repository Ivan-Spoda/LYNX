# Análise comportamental do FL Studio 2026: rodada 20 (REA) — envelopes de *pattern* e LFO do Love Philter, Fruity Scratcher, grafos restantes do Patcher, Vocodex e Fruity Vocoder

> **Natureza deste documento.** Continuação de `REA_rodada19.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são fórmulas e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> (ex.: "esta rotina é `exp` porque é sempre chamada logo após multiplicar por ln 2") estão explicitadas no texto.

- **Alvos:**

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave novas |
  |---|---|---|---|
  | Fruity Love Philter | `b9f11d63…ba2ff547` (= rodadas 16…19) | Delphi | *tick* `FUN_00856690`/`FUN_00856570`, escala de tempo `FUN_00788080`, articulador VMT `0x853B18` (`FUN_008543d0`, `FUN_00854540`), troca de *pattern* `FUN_0085d050`, sincronismo `FUN_0085cc80`/`FUN_0085cfa0`, `TMEnvelope` VMT `0x77F9A8` (`FUN_00786350`, `FUN_00783ec0`, `FUN_00787ba0`, `FUN_00787fd0`), `TMLFO` VMT `0x77FB48` (`FUN_00788b70`, `FUN_007886b0`, `FUN_00788830`), onda `FUN_0060e450`/`FUN_0060e610` |
  | Fruity Scratcher | `Fruity Scratcher_x64.dll` `52c126ad3e8b684a24a9bff7b8bc77de73984732d14a3e882e01b00366b09b42`; `engine_x64.dll` `104a1f410f8018bea64f2ae1449ebf7e8383f744baf6be0278d0c34ee3d8ab1b` | Delphi (+ motor C++ só de timecode) | estado `FUN_00728d80`, `ProcessParam` `FUN_00729fe0`, aplicação `FUN_0072a630`, *reset* `FUN_00729f10`, render `FUN_0072add0`, carga do sample `FUN_0072be20`, DC `FUN_0072c660`/`FUN_0072c6d0` |
  | Patcher | `cb4212c4…c64b` (= rodada 14) | Delphi | mistura das entradas `FUN_0079bf20`, leitura do cabo `FUN_00952440`, atraso `FUN_0079f0b0`, IPP por nome `FUN_005da820` |
  | Fruity Vocoder | `Fruity Vocoder_x64.dll` `bfc3bec3…a63961b6` (= rodada 19) | Delphi (`TTestPlug`, VMT `0x6cf210`) | estado `FUN_006cf810` (slot 3), parâmetros `FUN_006d08c0` (slot 6), render `FUN_006d0fb0` (slot 7), preparo `FUN_006d03a0`/`FUN_006cff90`/`FUN_006d00b0`, biquad `FUN_005dbb00`/`FUN_005db530`, seguidor `FUN_006d0e70`, UI `FUN_006d4cf0`/`FUN_006d4d50`/`FUN_006d4a20`/`FUN_006d5070` |
  | Vocodex | `Vocodex_x64.dll` `43d10692…22ca714bb0` (SHA-256 completo `43d10692ea04457018138110a1a3035d99ed8b40a7f52e67a3109c22ca714bb0`) | Delphi (`TTestPlug`, VMT `0x84e438`) | construtor `FUN_0084f730`, estado `FUN_0084efb0` (slot 3), parâmetros `FUN_00853e80` (slot 6), render `FUN_00856350` (slot 7), bandas `FUN_00851dd0`, escala de frequência `FUN_0062cb20`/`FUN_0062cd90` |

- **Ferramentas:** Ghidra 12.1.4 *headless* (projetos anteriores abertos só para leitura: `/tmp/r17gh_LP`,
  `/tmp/r14gh_Patcher_x64.dll`; novos: `/tmp/r20gh_Scratch`, `/tmp/r20gh_Vocoder`, `/tmp/r20gh_Vocodex`; scripts do Vocodex/Vocoder em `tmp/r20/voc/`); scripts em `tmp/r18/gh/scripts/`; `tmp/r18/k.py`
  e `tmp/r18/vmtname.py` para constantes e VMTs; dados com `tmp/r16/lp.py`, `tmp/r20/py/lpenv.py`,
  `tmp/r20/py/pat.py` (port do classificador do `FlPatcher.cpp`) e `tmp/r20/py/sp.py` (redução série-paralelo).
  Manual oficial lido via web.
- **Data:** 2026-10-13.
- **Numeração de trilhas:** os pares (trilha, slot) dos §1 e §3 são os do *pickle* da rodada 14
  (`blobs14.pkl`); o §4 conta os `0xEC` a partir do Master = 0 e encontrou, para as trilhas com Vocoder/Vocodex,
  uma diferença de 1 em relação ao *pickle* (§4.1.5). Ao cruzar com o `.flp`, conferir pelo nome da trilha.

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/`.fst`/arquivos da instalação |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abreviada como `…/plugins/` abaixo). As citações do manual vieram de leitura web resumida; os trechos citados
são curtos e devem ser reconferidos na página antes de virar texto de interface.

---

## 1. Fruity Love Philter — envelopes de *pattern*, LFO dos articuladores e seguidor de envelope **[DECOMP + DADOS + DOC]**

**O que o LYNX supõe / precisa:** reproduzir os 9 *gates* rítmicos "envelope → volume" e os 7 + 7 LFOs → pan/cutoff
dos demos (rodada 16 §2.6). Faltavam: unidade de tempo dos pontos, qual *pattern* toca, *loop*, forma das curvas,
lei do LFO, sincronismo e o papel do seguidor de envelope.

### 1.1 Relógio de controle (*tick*) e escala de tempo **[DECOMP `FUN_00856690`, `FUN_00856570`, `FUN_00854e20`, render `FUN_0085aa10`]**

- Os articuladores **não são avaliados por amostra**: o render conta um *tick* a cada `N` amostras internas
  (contador `+0x169C`, recarregado com `+0x16A0`). Em cada *tick* percorre a lista de articuladores ativos
  (`+0x1760`), calcula a saída de cada um (mapas X/Y/IEF e depois envelope/LFO, §1.4) e o valor resultante é
  aplicado ao áudio com uma **rampa linear ao longo do bloco** até o próximo *tick* (`FUN_0062aee0`: passo
  `(alvo − anterior)/n` por amostra, limitado em módulo pelo campo `+0x38` do objeto `+0xCC`; o valor desse limite
  **não foi identificado**).
- `N` vem de `FUN_00856690(v)`, com `v` = **64** (HQ envelopes desligado) ou **8** (ligado, ou quando o bit 1 do
  valor do evento de host 1 liga `+0x16D2`) — tabela `0x8D1A08` = `{64, 8}` indexada por `+0x154 | +0x16D2`:
  **`N = max(4; 4·round(v·fs_int/44100/4))`**, onde `fs_int` = campo `+8` do objeto `+0xCC` (inicializado com
  `fs·oversampling` em `FUN_00854E20`; que seja a taxa interna é **inferência** por esse uso). A 44,1 kHz sem
  *oversampling*: **N = 64 amostras ≈ 1,45 ms** (HQ: 8 amostras ≈ 0,18 ms). O *tick* tem sempre ≈ 64/44100 s,
  qualquer que seja a taxa.
- Em `FUN_00856570`, com `τ = N/fs_int` (segundos por *tick*, pela dedução acima) e `BPM` = campo `+0x1690`
  (escrito pelo evento de host 0 em `FUN_00857990`; que seja o andamento em BPM é **inferência** pela fórmula):
  - escala "livre" = **`2·τ`**;
  - escala "tempo" = **`τ·BPM/60`** (= tempos por *tick*).
  Ambas são passadas a todos os envelopes e ao LFO de cada articulador (`FUN_00854540` → método 15 de
  `TMEnvelope` `FUN_00788080`), que **escolhe a escala "tempo" se o bit 0 das *flags* do envelope (TEMPO) estiver
  ligado**, senão a "livre".
- Em cada *tick* o envelope avança uma unidade (constante 1,0 em `0x85C2A0`) e a posição no segmento cresce
  `escala / (Δt·k)` (`FUN_00786350`; `k` = fator do *knob* A/D/S/R do segmento, abaixo). Logo:
  - **com TEMPO: Δt está em tempos (semínimas)** — um segmento de Δt = 1 dura exatamente 1 tempo;
  - sem TEMPO: um Δt = 1 dura `1/(2τ)` *ticks* = **0,5 s** (dedução das duas fórmulas; nenhum dado dos demos usa
    esse caso — **não conferido por render**).
- *Knobs* A/D/S/R do envelope (`+0x98…+0xA4`, i32, padrão 128/128/0/128) → fatores `v/128`
  (`FUN_00788250`, constante `PTR 0x8D3970` = 1/128) copiados para o estado ao iniciar (`FUN_00788300`).
  **Todos os envelopes ligados dos demos têm 128/128/0/128 → fator 1** **[DADOS]**.
- **Dados:** os 11 envelopes de *pattern* ligados e os 14 LFOs ligados das unidades ativas dos demos têm **todos o
  bit TEMPO ligado** (*flags* = 1, ou 3 em 2 LFOs) **[DADOS `tmp/r20/py/lpenv.py`]**.

### 1.2 Qual *pattern* toca, *loop* e sincronismo com a música **[DECOMP `FUN_0085d050`, `FUN_0085cc80`, `FUN_00787ba0`, `FUN_00787fd0`, `FUN_00786350` + DOC + DADOS]**

- **Só um *pattern* toca por articulador:** ao mudar o parâmetro global 5 (PatNum, recortado em 1…10;
  `FUN_008579e0` → `FUN_0085d050`) cada articulador passa a apontar (`+0x10`) para o envelope de índice PatNum
  (`+0x28 + 8·PatNum`). Os outros 9 envelopes não são avaliados. Manual: "Use the selector to the left of the tab
  to change Modulation pattern" e "MIDI notes C5 to E6 can be used to change patterns on-the-fly" **[DOC
  `…/plugins/Fruity%20Love%20Philter.htm`]**. **Dados:** PatNum = 1 nos 43 slots e só o envelope 1 está ligado
  **[DADOS]**.
- **Loop** (`FUN_00786350`): ao chegar ao ponto "fim de *loop*" (campo `+0x88`, preparado a partir do cabeçalho do
  envelope) o envelope **volta ao ponto de início de *loop*** (`+0x50`) se ele for ≥ 0; senão fica parado no
  último valor. Nos demos o cabeçalho é sempre `ligado = −1, decay = −1, loop = 0, sustain/fim = último ponto`
  **[DADOS]** → **o *pattern* inteiro repete em *loop***, com período `T = ΣΔt` (soma dos Δt dos pontos 1…n):
  **1 tempo** em 8 dos 9 envelopes de volume, 0,25 tempo em 1 ("Drumloop Masher"), 4 tempos no de cutoff
  ("LollieVox").
- **Sincronismo com a posição da música** (`FUN_0085cc80`): quando o evento de host 12 chega com valor 1
  (`+0x16D1`; que seja "tocando" é **inferência**) é chamado `FUN_0085cfa0`, que zera o contador de *tick* e
  reposiciona **todos os envelopes e LFOs** na posição atual da música: a posição vem de uma consulta ao host
  (código 0x24) convertida em amostras (`×fs_int×240 / (BPM·…)`; o formato exato da consulta **não foi
  confirmado**) e depois em *ticks*. O envelope é posicionado em `t·escala` (= tempos, com TEMPO) **reduzido ao
  intervalo de *loop*** (`FUN_00787fd0`: `t ← t − ⌊(t − T₀)/T_L⌋·T_L`, com T₀ = tempo do ponto de início e T_L
  = duração do *loop*). Conclusão: **com TEMPO, a fase do *pattern* é `posição da música (tempos) mod T`** a partir
  do início do *loop*.
- Na troca de *pattern* o reposicionamento é feito com a *flag* `+0x15E` (padrão 1 no construtor; é um dos 3 bytes
  de opção do bloco global — que seja "Sync pattern changes" é **inferência** pelo manual: "Envelopes are synced
  to song/pattern position when selected") e **só os envelopes** (o LFO não é reiniciado nesse caso).
- Fora do transporte (sem a *flag* `+0x16D1`) a posição de partida é 0 e o envelope corre livre a partir daí.

### 1.3 Valor do envelope e forma das curvas **[DECOMP `FUN_00786350`, `FUN_00785730`, `FUN_00783ec0` (`0x78411f…0x784196`), `FUN_0060e3c0`, método 0 `FUN_007800c0`/`FUN_00780090`]**

- **Centro:** o valor de cada ponto é `y − c`, com `c` = campo `+0x18` do envelope = **`modo × 0,5`**
  (`FUN_008544d0` → `FUN_00780090` → método 0; constante `0x7800B8` = 0,5). Articuladores aditivos (modo 1: **pan,
  cutoff, ressonância**) → valor `y − 0,5` ∈ [−0,5; 0,5]; multiplicativos (modo 0: **volume**, L/B/H, WS) →
  valor `y` ∈ [0; 1]. Esta chamada **não é feita para o LFO** (ver §1.5).
- **Interpolação** entre o ponto anterior (valor `a`) e o ponto atual (`b`), com posição `x` ∈ [0; 1] no
  segmento: `valor = a + (b − a)·v(x)`. A curva é a do **ponto de chegada**. Para o tipo de curva 0 (*single
  curve*, byte `+0xC` = 0; é o tipo de todos os pontos dos *gates* dos demos, salvo um) com tensão `t`:
  - `q = sinal(t)·(1000001^|t| − 1)` (constante `0x784998` = 1 000 001; `FUN_0040c090`/`FUN_0040ad30` = ln/exp
    por **inferência** pelo padrão `a^x`);
  - `t = 0` → **linear** `v = x`;
  - `t > 0` → **`v = 1 − ((1 + q)^(1 − x) − 1)/q`** (muda depressa no começo do segmento);
  - `t < 0` → **`v = ((1 + |q|)^x − 1)/|q|`** (muda devagar no começo).
  Ex.: t = 0,285 → q ≈ 50 → v(0,5) = 0,877; t = 0,67 → q ≈ 1,05·10⁴ → v(0,5) = 0,990; t = −0,365 → v(0,5) =
  0,074 (cálculo a partir da fórmula).
- Outros tipos (1 *double*, 2 *hold*, 3/4 *stairs*, 5 *pulse*, 6 *wave*, 7…12) têm ramos próprios; nos demos só
  aparece o **tipo 5** em 1 ponto ("Drumloop Masher", unidade 8) — a forma exata dele **não foi decodificada**.
- **Formato no arquivo** (confere com a rodada 11 §1.6): por ponto `f64 Δt`, `f64 y`, `f32 tensão`, 4 B (o 1.º
  byte = tipo de curva) **[DADOS]**.

### 1.4 Combinação no articulador **[DECOMP `FUN_0085aa10` (laço de *tick*), `FUN_008543d0`; complementa a rodada 18 §4]**

A cada *tick*, para cada articulador ativo: `s = base` (campo `+0x20` = o `f32` gravado: 1,0 nos alvos
multiplicativos, 0 nos aditivos) → aplica os mapas X, Y e IEF (cada um: modo 0 `s ← s·m`, modo 1 `s ← s + m`,
o mapa IEF ainda multiplicado pela profundidade do envelope `+0x24`) → **envelope** (se ligado): modo 1
`s ← s + E·d_E`, modo 0 `s ← s·E·d_E` → **LFO** (se ligado): modo 1 `s ← s + L·d_L`, modo 0
`s ← s·(1 + 2·L·d_L)` (constantes `0x8544C4` = 2, `0x8544C8` = 1). `d_E` = `+0x24` e `d_L` = `+0x28` (os dois
`f32` seguintes do arquivo). Nos demos, `d_E = d_L = 1` em todos os articuladores com envelope/LFO ligados, salvo
dois LFOs de cutoff (0,516 e 0,641) **[DADOS]**.

Aplicação no áudio (render): cutoff `c = CUT/256 + s_cut`; ressonância `r = RES/256 + s_res`; ganhos L/B/H =
parâmetro × `s`; **pan e volume** vão para `FUN_00616620(pan = PAN/128 + 2·s_pan, vol = (+0x48)·s_vol)`, onde
`+0x48` é o volume da unidade (rodada 16: `curve11(VOL/128)·curve11(Master/128)`) — a lei do pan dentro de
`FUN_00616620` **não foi decodificada**.

Consequências diretas (fórmulas acima):

- **Gate de volume** (modo 0, base 1, `d_E = 1`): **ganho da unidade × y(t)**, com `y(t)` = o envelope do
  *pattern* (valores 0…1 dos pontos, sem centro).
- **Envelope de cutoff** (modo 1): `c = CUT/256 + (y − 0,5)·d_E`, e para o cutoff `d_E` = ENV/128 (rodada 16).

### 1.5 LFO **[DECOMP `TMLFO` VMT `0x77FB48`: `FUN_00788b70` (avaliação), `FUN_007886b0` (forma), `FUN_00788830` (taxa), `FUN_00788a20`/`FUN_0060e870` (fase inicial), `FUN_00788aa0` (posicionamento), `FUN_0060e450`/`FUN_0060e610` (onda), construtor `FUN_00788410`]**

- **Saída: `L = A(t)·W(φ)`**, onde `A(t)` é o "envelope do LFO" (os pontos gravados no LFO) avaliado como em §1.3,
  mas com **centro fixo 0,5** (o construtor do `TMLFO` põe `+0x18 = 0,5` e o método 0 não é chamado para ele) →
  `A = y − 0,5` ∈ [−0,5; 0,5]; e `W` ∈ [−1; 1] é a onda abaixo. Dados: os LFOs dos demos têm 1–2 pontos de nível
  constante (`y` = 1,0 → A = 0,5; 0,625 → 0,125; 0,6875; 0,875; 0,9375; 0,75; 0,25; 0,0 → A = −0,5, onda
  invertida) **[DADOS]**.
- **Taxa** (`FUN_00788830`): incremento de fase por *tick* (fase de 32 bits, 2³² = 1 ciclo) =
  `round(min((10001^(S/65536) − 1)·(2³²/625)·escala; 0,1875·2³²))` (constantes `0x7888C8` = 10001,
  `0x7888D8` = 6 871 947,67 = 2³²/625, `0x7888E0` = 805 306 368), S = velocidade (`+0xD0`, 1.º *knob* do LFO no
  arquivo). Com TEMPO: **período `P = 625/(10001^(S/65536) − 1)` tempos** (sem TEMPO: metade disso em segundos,
  pela escala livre `2τ` — não conferido por render). Valores dos demos **[DADOS + cálculo]**: S = 45819 → **1
  tempo**; 40898 → **2**; 35989 → **4**; 31102 → **8**; 26259 → **16**; 21502 → **32 tempos**.
- **Forma** (`FUN_0060e450`/`FUN_0060e610`), com *skew* `k = S₂/256` (`+0xD8`, ±128 → ±0,5) e largura de pulso
  `w = S₃/128` (`+0xDC`): fase φ ∈ [0; 1) dividida em: descida linear de +1 a −1 até `φ₆`; patamar −1 até `φ₅`;
  subida de −1 a +1 até `φ₇`; patamar +1 até o fim. `φ₇ = 1 − w` (se `w > 1/128`, senão 1);
  `φ₅ = clamp((1 − w)/2 − k·(1 − |w|))`; `φ₆ = φ₅ + w` se `w < 1/128` (largura negativa = patamar embaixo), senão
  `φ₆ = φ₅`. **Com k = w = 0 (todos os LFOs dos demos): triângulo simétrico** que começa em +1, vale −1 em φ = 0,5.
- **Tensão** (`FUN_007886b0`): `a = (21^(|T|/128) − 1)·0,05`, `q = sinal(T)·(10¹⁸^a − 1)`; aplicada a `|W|` com a
  mesma família de curvas de §1.3 e o sinal preservado (bit 2 das *flags* desligado, caso de todos os demos); com o
  bit 2 ligado, aplicada a `(W + 1)/2` e reescalada. Demos: T = 0 (10 LFOs), 34, 40, 42, 81 (pan) → q ≈ 12, 26,
  34, 1,9·10⁵ (cálculo). Manual: "Bipolar LFO tension: The LFO shape is mirrored above and below 0" **[DOC]** — qual
  valor do bit corresponde ao rótulo **não foi confirmado**.
- **Fase inicial e sincronismo:** ao (re)iniciar, a fase vai para o ponto médio da subida (`FUN_0060e870`:
  `φ₅ + (φ₇ − φ₅)/2`, que no triângulo simétrico é **0,75 → W = 0 subindo**) **mais o deslocamento de fase**
  (`+0xE0`, 5.º inteiro do LFO no arquivo, em 2⁻³² ciclos; demos: 0 ou ±2³⁰ = ±¼ de ciclo). No início do transporte
  (§1.2) o LFO é reposicionado: **fase = fase inicial + (posição em *ticks*)·incremento** → **LFO travado na
  posição da música**. Na troca de *pattern* o LFO não é reiniciado.
- O bit 3 das *flags* zera o incremento ("Frozen LFO: The first LFO value is held" **[DOC]**). A fase só começa a
  andar depois do primeiro segmento de `A(t)` (atraso = Δt do 1.º ponto; 0 nos demos).
- **Profundidade:** `d_L` = 3.º `f32` do articulador (rodada 16: profundidade do envelope × `scurve11(Master
  LFO/128)`).
- **Consequências:** pan `= PAN/128 + 2·A·d_L·W(φ)` (ex.: "With You": A = 0,125 → ±0,25); cutoff
  `c = CUT/256 + A·d_L·W(φ)` (A = 0,5 → ±0,5 em `c`).

### 1.6 Seguidor de envelope de entrada (IEF) **[DECOMP + DADOS]**

- Os mapas IEF só entram no cálculo quando `+0x16B8 > 0`; o nível do seguidor (`+0x16B4`) é convertido em índice
  `round(1024·nível)` da tabela do mapa (constante `0x85C294` = 1024). Ataque/release: rodada 16 §2.4.
- **Dados:** nenhum mapa IEF não neutro nas 63 unidades ligadas dos demos (neutro = 0,5 aditivo / 1,0
  multiplicativo) **[DADOS]** → **o IEF é irrelevante para os demos**.

### 1.7 Os 9 + 1 envelopes e os 14 LFOs dos demos **[DADOS `tmp/r20/py/lpenv.py`; Δt em tempos]**

| Projeto (trilha, slot) | Unid. | Alvo | Pontos `(Δt; y; tensão)` | Leitura |
|---|---|---|---|---|
| "Atthar" (20,9), (21,6) | 1 | vol | (0;1) (0,059 / 0,055; 0,08 / 0; +0,285) (0,294 / 0,298; idem; 0) (0,647; 1; +0,67) | queda rápida, patamar, volta — período 1 tempo |
| "Atthar" (54,3) | 1 | vol | (0;1) (0,096;0,13;+0,285) (0,154;0,125;0) (0,75;1;+0,67) | idem |
| "With You" (26,0), (27,2), (40,1) | 1 | vol | (0;1) (≈0,33;≈0,31;+0,768) (≈0,67;1;+0,438) | *pump* de 1 tempo |
| "With You" (8,5) | 1 | vol | (0;1) (0,5;1) (0,5;1) | constante 1 (sem efeito) |
| "Drumloop Masher" (2,3) | 8 | vol | (0;1) (0,25;0; tipo 5, +1,0) (0;1) | 0,25 tempo, curva tipo 5 (não decodificada) |
| "SFX transitions" (2,6) | 1 | vol | (0;1) (0;0) (0,5;1;−0,365) (0,5;1) | corta, sobe em ½ tempo, mantém ½ |
| "LollieVox" (24,2) | 1 | cut | (0;1) (4;0,5) (0;1) | rampa de 4 tempos (−0,5·d_E → 0) |
| "Drumloop Masher" (2,3) | 3 | res | (0;0,5) (0;0,5) | nulo |
| LFO → pan (7): "With You" (8,5) u2, "Use Me" (8,6), "Masher" (2,3) u7, (2,4) u2/u3, "Lightning" (26,3), "Oxygen" (44,4) | | pan | S = 45819/31102/35989/21502/26259/40898 | 1…32 tempos |
| LFO → cutoff (7): "Masher" (2,3) u2…u6, (2,4) u2/u3 | | cut | S = 40898/45819/35989/26259 | 2, 1, 4, 16 tempos |

---

## 2. Fruity Scratcher — toca-discos sobre um sample, somado à entrada **[DECOMP + DADOS + DOC]**

**Onde está:** `Plugins/Fruity/Effects/Fruity Scratcher/` (é um **efeito**, não um gerador). Dois binários:
`Fruity Scratcher_x64.dll` (Delphi; classe do plugin **`TTestPlug`**, VMT `0x7285a8`, filha de
`TDelphiFruityPlug`; unidade `Scratcher\TestPlug.pas`) e `engine_x64.dll` (C++), que exporta só
`createTimecodeDecoder`, `destroyTimecodeDecoder`, `gotSignal`, `getPosition` e `process` — é o
**decodificador de timecode (DVS)**, usado apenas no modo vinil de controle **[DECOMP: exportações e
`FUN_1800010e0…`; não detalhado, irrelevante para o import]**.

**O que é** **[DOC `…/plugins/Fruity%20Scratcher.htm`]**: "vinyl turntable simulator"; carrega-se um sample e
o *scratch* é gravado em tempo real (prato arrastado com o mouse, ligado a controlador ou automatizado). Controles
citados: Smooth 1…4, Pan, Vol, Hold ("Stops the platter from turning … when linking the platter to a MIDI
controller"), Mute, prato, transporte "Play Backwards / Pause / Play Forward", SPD (padrão 1×), ACC, SEN, DVS
(Serato/Traktor/Timecode CD). O manual não dá unidades.

**Dados:** **nenhum demo usa o plugin.** Há 1 blob no teste de renderização
`Data/System/Render tests/Fruity Scratcher.flp` (trilha 1 do mixer, slot 0) e 4 `.fst` idênticos (o preset
padrão do banco de plugins) **[DADOS]**.

### 2.1 Parâmetros (Tag do DFM `TSynthEditorForm` = índice) **[DFM + DECOMP `FUN_0072a630`, `FUN_00729fe0`, *reset* `FUN_00729f10`]**

`FUN_00729fe0` tem a assinatura (índice, valor, *flags*) e o tratamento de *flags* do `ProcessParam` do SDK
(inferência pela forma); converte o valor do host (0…2³⁰, constante 2⁻³⁰) para o inteiro do controle e chama
`FUN_0072a630`, que guarda o **valor normalizado** em `+0x2E4 + 4i` (é o que vai para o estado) e o aplica:

| i | Controle (DFM) | Faixa do controle (padrão) | Valor interno (normalizado v) | Campo |
|---|---|---|---|---|
| 0 | Wave position (`WavePosWheel`, sem Tag → 0) | 0…30000 | `v` ∈ [0; 1] = posição no sample (ver §2.3) | `+0x134` → `+0x130` |
| 1 | Mute | caixa | **guardado invertido**: o `ProcessParam` grava `|host − 1|`; interno ≠ 0 → **audível** | `+0x16C` |
| 2 | Turntable speed (SPD) | 0…100 (50) | `v` | `+0x14C` |
| 3 | Turntable acceleration (ACC) | 0…100 (25) | `v` | `+0x150` |
| 4 | Pan | −128…128 (0) | `2v − 1` | `+0x140` |
| 5 | Volume | 0…255 (199; *reset* = 200/255) | `v` (ganho linear) | `+0x13C` |
| 6 | Turntable hold | caixa | booleano | `+0x16D` |
| 7 | Turntable sensitivity (SEN) | 0…100 (50) | `v`; o editor recebe `0,0025·v` (`+0x800`) — **o render não o lê** | `+0x148` |
| 8 | Transport | seletor de 3 (índice 1) | **inteiro = índice − 1** (o `ProcessParam` subtrai 1 e o texto soma 1) → **−1 / 0 / +1** | `+0x170` |
| 9…12 | Smooth 1…4 | 0…1000 (500, 908, 999, 400) | `v` | `+0x15C…+0x168` |
| 13 | Rotations (`RotationsDummyWheel`) | 0…18 (9) | inteiro | `+0x174` |

- **Transporte:** o render multiplica a velocidade-alvo por esse inteiro (§2.3) → −1 = para trás, 0 = parado,
  +1 = para frente **[DECOMP]**; coerente com a ordem do manual "Play Backwards / Pause / Play Forward". As dicas
  do DFM do seletor dizem `off / on / inverted` (divergência de rótulo; o código manda). Padrão = índice 1 =
  **parado** (o *reset* põe 0).
- O leitor de versões antigas (< `0xF4628`) grava inteiros e divide por 100 (SPD, SEN, ACC), 255 (Vol), 1000
  (Smooth) e faz `(p/128 + 1)/2` (Pan) **[DECOMP `FUN_00728d80`, constantes `0x7298f0…0x729918`]** — confirma as
  faixas acima.

### 2.2 Estado (`0xD5`) **[DECOMP `FUN_00728d80` (VMT slot 10, leitura/gravação) + DADOS 5/5]**

| Off | Tipo | Conteúdo |
|---|---|---|
| 0 | u32 | versão: grava **`0xF462A`**; os 5 blobs são **`0xF4628`** |
| 4 | 14 × f32 | **parâmetros 0…13 normalizados** (tabela §2.1; p8 e p13 como inteiros em *float*; p1 = interno "audível") |
| 60 | i32 n + n B | **caminho do sample em ANSI**; passa por uma chamada do host (código 0xE) antes de carregar — no blob do teste: `%FLStudioFactoryData%\Data\Patches\Packs\Loops\DL Breaker.wav` (a expansão da macro pelo host é inferência) |
| (v > `0xF4628`) | u8 | opção do editor (`+0x8EC`; usada pelo *dispatcher* para o tamanho da janela — inferência: "Toggle Scratcher size") |
| (v ≥ `0xF462A`) | i32 + u8 | `+0x204` = índice do timecode (−1 = desligado; ≠ −1 liga o modo DVS) + 1 *flag* do objeto de entrada (`+0x48E`; aplicado como filtro à entrada no modo DVS — que seja o "RIAA Preamp" do manual é inferência) |

- Para versões < `0xF462B` o leitor ainda tenta ler **56 B** para a tabela de inteiros `+0x2AC`; nenhum dos 5
  blobs tem esses bytes (terminam no caminho) — o efeito disso **não foi confirmado** (o laço seguinte recalcula a
  UI a partir do `ProcessParam`).
- **Dados** **[DADOS]**:

  | Origem | p0 | p1 | SPD | ACC | Pan | Vol | Hold | SEN | Transp. | Smooth 1…4 | Rot. | Sample |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | Render test (125 B) | **0,111** | 1 | 0,5 | 0,25 | 0,5 (0) | 0,78 | **1** | 0,5 | 0 (parado) | 0,5 / 0,908 / 0,999 / 0,4 | 9 | DL Breaker.wav |
  | 4 × `.fst` (64 B) | 0 | 1 | 0,5 | 0,25 | 0,5 | 0,78 | 0 | 0,5 | 0 | idem | 9 | nenhum (n = 0) |

### 2.3 DSP (`FUN_0072add0`, render do efeito, VMT slot 14) **[DECOMP]**

**Sample** (`FUN_0072be20`): pedido ao host com o formato **PCM 16 bits, 2 canais, 44 100 Hz** (estrutura
`WAVEFORMATEX` montada no código); convertido para *float* `/32768`; um quadro extra é anexado no fim para a
interpolação; `N` = nº de quadros. A troca de sample é aplicada no fim de um bloco. Se o host converte a taxa do
arquivo para 44,1 kHz **não foi confirmado** (é o formato pedido).

**Sem sample carregado:** a saída é a entrada (cópia).

**Leitura do sample (comum aos dois modos):** posição `φ` em "voltas" — **1 volta = o sample inteiro**; usa-se a
parte fracionária (o sample **dá a volta / faz *loop***); índice `x = frac(φ)·N`, quadro `⌊x⌋` (arredondamento
por `FUN_0072ab70`, *floor* por inferência pela construção) e interpolação linear com o seguinte. Cada canal passa
por **`y = (s − s_ant) + 0,99·y_ant`** (`FUN_0072c660`/`FUN_0072c6d0`: bloqueio de DC; dedução: corte ≈
`0,01·fs/2π` ≈ 70 Hz a 44,1 kHz) e por *flush* de desnormais (< 1e−14).

**Volume e pan:** alvo de volume = `Vol` (ou 0 se Mute), suavizado por amostra `g ← 0,99·g + 0,01·alvo`; pan
`p = 2v − 1` suavizado igual. **Balanço linear:** `p ≤ 0` → L·1, R·(1 + p); `p > 0` → L·(1 − p), R·1.

**Saída = entrada + sample processado** (o sinal que entra no slot passa com ganho 1; o Scratcher **soma**).
Exceção: no modo DVS (timecode ligado) a saída é só o sample, sem a entrada.

**Modo "motor" (Hold desligado, sem timecode)** — toca o sample como um toca-discos:

- velocidade-alvo por amostra **`s = (2/(N − 1))·SPD·T`** (T = −1/0/+1 do transporte; `+0x1A8 = 2/(N−1)`):
  **SPD = 0,5 → 1 quadro por amostra (1×)**; a lei é linear, **rate = 2·SPD** (0…2×), sem compensação da taxa do
  host no código lido;
- inércia: **`v ← a·(v − s) + s`** com **`a = 0,99999 − 0,0005·ACC`** (constante de tempo `1/(1 − a)` amostras:
  ACC = 0 → 100 000; 0,25 → 7 407 ≈ 0,17 s a 44,1 kHz; 1 → 1 961 ≈ 44 ms);
- `φ ← φ + v`, `φ` mantido em (0; 1]; a posição é publicada em p0 (o *idle* `FUN_0072a890` empurra `+0x130`
  para o parâmetro 0 com a *flag* de UI). **Neste modo automatizar p0 não tem efeito** (p0 é sobrescrito a cada
  amostra) — dedução do fluxo.

**Modo "scratch" (Hold ligado, sem timecode)** — a posição segue o parâmetro 0 (o prato):

- alvo `P = p0 + (Rot − 9)` (Rotations soma voltas inteiras, −9…+9); erro `e = P − φ`;
- `v ← c₂·v + ((1 − c₂)/(n₁ + 250))·e`; `d = S₄·v + (1 − c₃)·(1 − S₄)·e`; `φ ← φ + d`;
- **`n₁ = ⌊2500·S₁⌋`, `c₂ = 0,99 + 0,0099·S₂`, `c₃ = 0,99 + 0,00999·S₃`** (S = Smooth 1…4; padrões →
  n₁ = 1250, c₂ ≈ 0,99899, c₃ ≈ 0,99998, S₄ = 0,4); p0 é lido no início de cada bloco (`+0x130 ← +0x134` no fim do
  bloco anterior).
- Consequência (dedução): com p0 **constante** a posição converge e o sample pára num ponto → o bloqueio de DC
  leva a saída do sample a **zero**; só há som enquanto p0 se move.

### 2.4 Uso no teste de renderização e o que é preciso para reproduzir **[DADOS + DECOMP]**

- O `.flp` de teste tem **Hold = 1, transporte parado, p0 = 0,111** e, no pattern 1, um evento de controle
  `0xDF` com **259 registros** `{u32 tick, u32 controlID, i32 valor}` (formato da rodada 2 §2.7), todos com
  `controlID = 0x20408000` = `(1 << 22) + (0 << 16) + 0x8000 + 0` → **trilha 1, slot 0, parâmetro 0 (Wave
  position)** na codificação `trilha<<22 + slot<<16 + 0x8000+k` (o nibble alto aqui é **2**, não 7 como no `0xE1`;
  a diferença **não foi decodificada**). Valores: rampa **4 → 131 em 192 ticks e volta a 0 em 384 ticks** (PPQ =
  96 → 4 tempos). A escala desses valores para p0 **não foi confirmada**. Nenhum clipe de automação (`0xEA`).
- Ou seja, o uso "típico" visto é: **sample de referência + automação do prato (p0) com Hold ligado**. Um uso
  estático só produz som no modo motor (transporte ±1): o sample em *loop* a `2·SPD`×, com inércia, somado à
  entrada.

---

## 3. Patcher — os grafos dos demos que o LYNX ainda não cobre **[DECOMP + DADOS]**

**O que o LYNX faz** (`src/core/FlPatcher.cpp`): (a) cadeia em série pura no barramento 0, ganhos 1; (b) "ramos":
trecho em série a partir de IN, depois ramos que são cadeias simples até OUT, partindo de IN, de um nó com várias
saídas ou das bandas de um Frequency Splitter/Stereo Shaper; entradas de *sidechain* ignoradas. A classe do cabo é
lida do byte 24 do registro `1300`.

### 3.1 Como o Patcher mistura os cabos **[DECOMP `FUN_0079bf20`, `FUN_0079f0b0`, `FUN_00952440`, `FUN_0079ca50`, `FUN_0079b790`, `FUN_0079ef40`, resolvedor `FUN_005da820`]**

- **Soma com ganho linear (confirmado):** para cada porta de entrada de áudio, o 1.º cabo ativo escreve
  `destino = origem × ganho` e cada cabo seguinte faz **`destino += origem × ganho`**; sem cabo ativo o *buffer* é
  **zerado**. As três rotinas são da Intel IPP, resolvidas por nome em `FUN_005da820`: `ippsMulC_32f`
  (ponteiro `0x9F30B8`), `ippsAddProductC_32f` (`0x9F3320`) e `ippsZero_32f` (`0x9F2F28`) — a semântica é a
  documentada dessas funções da IPP (inferência pelo nome); o tamanho é `2·amostras` (estéreo intercalado).
  `ganho` = campo `+0x18` do cabo (nasce 1,0 em `FUN_0079ef40`). **Isto fecha o [HIP forte] da rodada 14 §1.5.**
- **Cabo desligado:** o byte `+0x1C` do cabo ≠ 0 faz o cabo ser ignorado na soma. Na leitura do arquivo
  (`FUN_00952440`) esse byte = **bit 0 das *flags*** (offset 20) do registro `1300`; o **bit 1** diz se o byte de
  classe (offset 24) é válido (convertido por `FUN_0094a150`, não lido) — **sem o bit 1, a classe deve vir da porta**
  (`FUN_0079b790` procura então a ligação pelas portas).
- **Compensação de atraso:** se a latência acumulada da origem for menor que a do destino, o cabo passa por uma
  linha de atraso da diferença (`FUN_0079f0b0` → `FUN_0079f480`/`FUN_0079f350`) antes da soma.
- **Dados:** nos slots do mixer, 571 cabos têm *flags* = 2 (classe válida) e **203 têm *flags* = 0**, dos quais
  **128 são cabos de controle** (porta de classe 2) com byte de classe = 0 — o LYNX os lê hoje como áudio. Nenhum
  cabo dos demos tem o bit 0 (desligado); nos `.fst` há 6 **[DADOS `tmp/r20/py/pat.py`]**.

### 3.2 Quais grafos ficam de fora **[DADOS: port fiel do classificador do `FlPatcher.cpp` em `tmp/r20/py/pat.py`]**

Dos 30 Patchers de slot do mixer em `blobs14.pkl` (24 em demos + 6 em *templates*): **série 4, ramos 12, não
cobertos 14** (12 blobs distintos, 7 topologias; os 4 Patchers de canal não têm IN/OUT de áudio — são geradores). Usando a classe
da porta (§3.1) o resultado é o mesmo. Depois de **remover os nós fora de todo caminho IN→OUT** (só alimentam
controladores/analisadores) e reduzir o grafo por **série** (nó com 1 entrada e 1 saída) e **paralelo** (cabos com as
mesmas pontas), **todos os 14 se reduzem a uma única expressão série-paralelo** **[DADOS `tmp/r20/py/sp.py`]**.
Notação: `→` série, `‖` paralelo somado, `×g` ganho do cabo, `saída1` = barramento 1 do nó.

| Projeto (trilha, slot) | Expressão IN ⇒ OUT | Nós fora do caminho | Por que o LYNX não cobre |
|---|---|---|---|
| "Bien Duro" (2,4), (3,4), (9,3), (12,2) (40 178 B; (3,4) = (12,2)) | `Limiter "Limiter 2" → ( PEQ2 "detector" ‖ Fruity parametric EQ "EQ 3" → PEQ2 "main" )` | `IN → Fruity parametric EQ → Limiter → 2 × PEQ2 → 2 × Peak Controller` (ramo de detecção) + 8 Formula, 5 X-Y, Control Surface, NoteBook | divisão depois de um nó interno; ramo morto |
| "Exoplanet" (101,0) | `Stereo Shaper "Mid Side Splitter" → ( saída1 → Reeverb 2 ‖ Reeverb 2 ) → Maximus` | 2 Limiters "MID"/"SIDE" sem cabo de entrada | junção antes do último nó; ramo da saída 1 |
| "With You" (1,0) | `( Stereo Shaper "Side" → PEQ2 "Side EQ" ‖ Stereo Shaper "Mid" → PEQ2 "Mid EQ" )` | Control Surface | Stereo Shaper no meio do ramo é tratado como divisor |
| "Stroll Up" (1,6) (103 210 B) | `( PanOMatic "dryin" ‖ Stereo Shaper "IN" → ( 4 × [PEQ2 "bandN" → Soft Clipper → PEQ2 "bandNp" → PanOMatic "bNout"] ) → Stereo Shaper "OUT" → PanOMatic "wetin" )` | X-Y-Z, Control Surface | aninhado; Stereo Shaper como nó comum |
| "Stroll Up" (1,2) (451 240 B) | igual na casca; cada banda N = `PEQ2 "bandN" → ( PanOMatic "bNdry" → PanOMatic "dryN" ‖ ( ×≈0,5 → PanOMatic → Flangus ‖ ×≈0,5 → PanOMatic → Flanger → PanOMatic ‖ ×≈0,5 → PanOMatic → Chorus → PanOMatic ) → PanOMatic "wetN" → Stereo Enhancer ) → PEQ2 "bandNout" → Limiter → PanOMatic "Nout"`, as 4 bandas → PanOMatic "merger" → Stereo Shaper "OUT" | X-Y-Z, Control Surface | 3 níveis de aninhamento; na banda 1 os cabos Flangus/Flanger/Chorus → soma têm ×0,333/0,328/0,328 |
| *Templates* Funk/Jazz/Metal/Rock (125,1) (28 690 B) | `( PanOMatic "dryin" ‖ PanOMatic "wetin" → Stereo Shaper "IN" → Limiter "Gater" → Stereo Shaper "OUT" )` | 4 X-Y-Z (mapeadores), NoteBook 2, Control Surface | cabos de controle sem bit 1 lidos como áudio; Stereo Shaper como nó comum |
| *Templates* Metal (12,0), Rock (13,0) (108 377 B) | `( Stereo Shaper → PEQ2 → WaveShaper → ( Stereo Shaper → Convolver → ×0,376 ‖ Stereo Shaper → PEQ2 → Convolver → ×0,199 ) → Delay 3 → ×2 → Delay 3 → ×2 → Stereo Shaper → PanOMatic "wet" ‖ PanOMatic "dry" )` | 6 Formula, X-Y-Z, Control Surface | idem + junção antes do último nó + ganhos ≠ 1 |

Observações sobre esses grafos **[DADOS]**:

- **Nenhum usa barramentos ≠ 0 nas entradas** (sem *sidechain*); a única saída ≠ 0 usada é a `saída1` do Stereo
  Shaper do "Exoplanet" (101) — pela rodada 17 §2, o barramento do parâmetro 6 do Stereo Shaper carrega
  `entrada − saída da matriz`.
- Nos casos "dry/wet" (Stroll Up, *templates*), os nós de entrada/saída são **PanOMatic e Stereo Shaper usados como
  ganho/matriz**; os parâmetros deles são movidos por cabos de controle vindos de X-Y-Z/Control Surface. O valor
  estático a usar é o que está no estado `5005` de cada nó (rodada 14 §1.7: o FL grava o valor corrente).
- **"Bien Duro" é um EQ dinâmico:** os Peak Controllers medem o ramo de detecção (`IN → EQ → Limiter → PEQ2`) e,
  via Formula/X-Y, movem parâmetros dos PEQ2 do caminho de áudio (PEQ2 "detector": parâmetros 6, 11, 12, 13; PEQ2
  "main": 12; "EQ 3": 28) — **não tem equivalente estático** além dos valores gravados.
- Os Limiters "MID"/"SIDE" do "Exoplanet" (101) não têm cabo de entrada: pela §3.1 a entrada deles é **zerada**; a
  saída deles vai a OUT (somando o que um Limiter produz com entrada nula — supostamente silêncio; **não
  conferido**).
- Bit 3 das *flags* do nó (`5000`) = 1 em todos os nós de "With You" (1,0): significado **não confirmado**.

### 3.3 Mapeamento para cadeias série/paralelo (procedimento) **[DECOMP §3.1 + DADOS §3.2]**

1. Classe do cabo: byte 24 só se o bit 1 das *flags* estiver ligado; senão a classe da porta de destino. Ignorar
   cabos com o bit 0 (desligados).
2. Montar as arestas de áudio no barramento 0 de destino; descartar nós que não estão em nenhum caminho IN→OUT.
3. Reduzir: série (nó com uma entrada e uma saída) e paralelo (arestas com as mesmas pontas = **soma**), levando o
   ganho de cada cabo como fator da aresta (`×g`); repetir até sobrar `IN ⇒ OUT`.
4. Frequency Splitter / Stereo Shaper só "dividem" quando alguma aresta sai deles por barramento ≠ 0; caso
   contrário são nós comuns da cadeia.
5. Os 14 casos acima reduzem completamente; um contêiner `parallel` aninhável (ramos = cadeias que podem conter
   outros `parallel`) com ganho por ramo cobre todos.

---

## 4. Vocodex e Fruity Vocoder — estado, bandas, seguidor de envelope e entradas **[DECOMP + DOC + DADOS]**

**O que o LYNX supõe:** nada (os dois são descartados; rodada 14 §6: "sem aproximação útil"; rodada 19 §6b: layout do
Fruity Vocoder só por inferência). No código do LYNX não há entrada de *sidechain* de áudio (busca por "sidechain"
em `src/` só acha um rótulo de rampa no `MixerComponent.cpp`) **[DADOS: repositório]**.

Convenções desta seção (inferências declaradas): nas duas DLLs `FUN_00408810`/`FUN_0040ad30` = `exp` e
`FUN_00409870`/`FUN_0040bf50` = `ln` (são chamadas com 2, 11, 101, 10000… e o resultado bate com os dados: ver a
verificação da escala do Vocodex em §4.2.3); os globais de execução foram lidos nas rotinas de inicialização
(`FUN_005e2260` no Vocoder, `FUN_0062f250` no Vocodex): `ln 2`, `ln 2/2`, `ln 2/12`, `ln 2/1200`, `ln 7`, `ln 11`.
`curve11(x) = (11^x − 1)/10` (1 exato quando x = 1), a mesma do Love Philter.

### 4.1 Fruity Vocoder (classe `TTestPlug`, VMT `0x6cf210`, instância `0x15190` B)

#### 4.1.1 Estado (`FUN_006cf810`, slot 3) — **confirma e corrige a rodada 19 §6b**

| Campo | Tipo | Conteúdo |
|---|---|---|
| versão | i32 | 2 (gravada); carga só se > 1 |
| bandas | i32 (`+0x15170`) | **N = 4, 8, 16, 32, 64 ou 128** (seletor = índice i, `N = 4 << i`, `FUN_006d4cf0`; rótulos "4…128" na tabela `0x723a00`); padrão 16 (construtor `FUN_006cfb20`) |
| ordem | i32 (`+0x15174`) | **ordem do filtro 1…3** (seletor = ordem − 1, `FUN_006d4d50`; rótulos "1/2/3" em `0x723a30`); padrão 2 |
| listen | i32 (`+0x15178`) | **0 = só modulador, 1 = só portadora, 2 = mistura** (`FUN_006d4a20` → `FUN_006cfe50`); padrão 2 |
| troca | u8 (`+0x1517c`) | **troca modulador/portadora** (`SwapModCarSelect`, `FUN_006d5070`) — é o "byte extra" da rodada 19 |
| níveis | N × f32 | **ganho por banda** do banco de análise (campo `+0x144` de cada banda; zerado para 1,0 antes da leitura) |
| parâmetros | 12 × i32 (`+0x120…+0x14c`) | tags 0…11 do DFM, valores crus |

Dados: os 10 blobs (1 demo + 9 `.fst`) seguem exatamente essa ordem (ordem = 2, listen = 2, troca = 0) **[DADOS]**.
Os índices de automação são os tags 0…11 (`FUN_006d08c0` só aceita índice < 12) **[DECOMP]**.

#### 4.1.2 Leis dos parâmetros (`FUN_006d03a0`, `FUN_006cff90`) **[DECOMP]**

| Tag | Nome | Lei |
|---|---|---|
| 0/1 | FreqMin / FreqMax (0…65 536) | **`f = 10 + 159,9·(101^(v/65536) − 1)` Hz** (0 → 10 Hz; 65 536 → 16 000 Hz); se min ≥ max, min = max − 1 Hz |
| 2 | FreqScale (0…128, 64) | **`S = int(10000^(v/128) − 1) + 1`** (0 → 1; 64 → 100; 128 → 10 000; a conversão para inteiro é `FUN_00409d00`, arredondamento não conferido) |
| 3 | Invert | inverte a ordem das bandas do banco da portadora (§4.1.3) |
| 4 | Formant (−1200…1200) | **centésimos** somados às bordas do banco da portadora |
| 5 | Bandwidth (5…200, 50) | largura relativa em % (fórmula em §4.1.3) e normalização de ganho `1500/BW` |
| 6/7 | Attack / Release (10…1000) | **milissegundos**: coeficiente por amostra **`c = 0,01^(1/(ms·0,001·fs))`** (polo que cai a −40 dB em `ms`) |
| 8 | Hold | ≠ 0 → congela os níveis (sem análise; usa o último envelope) |
| 9/10/11 | Modulator / Carrier / Wet level (0…160) | **`curve11(v/128)`** (128 → 1,0; 160 → 1,89) — só no modo listen 2; listen 0 → mod = 1, resto 0; listen 1 → car = 1, resto 0 |

#### 4.1.3 Bandas e filtros (`FUN_006d00b0`, `FUN_005dbb00`, `FUN_005db530`) **[DECOMP]**

- **Bordas** (i = 0…N): `f_i = fmin + (fmax − fmin)·((S + 1)^(i/N) − 1)/S` Hz, convertidas para centésimos
  `c_i = 1200·log2(f_i/440) + 5700` (`FUN_006cff40`; 0 = Dó 16,35 Hz). Banda k = [c_(k−1), c_k].
- **Dois bancos:** análise (`+0x150`, sinal do modulador) com as bordas cruas; síntese (`+0xA950`) com `c_i +
  Formant` e, com Invert, índice espelhado (`N − k`). A banda só existe se o centro dos **dois** bancos ficar em
  **[10 Hz; 20 000·fs/44 100)**.
- **Centro** = média das bordas em centésimos (média geométrica em Hz): `fc = 440·2^(((c_(k−1) + c_k)/2 − 5700)/1200)`.
- **Filtro**: passa-faixa RBJ de ganho de pico 0 dB (código tipo 2: `b0 = α/(1+α)`, `b1 = 0`, `b2 = −α/(1+α)`,
  `a1 = −2cos ω/(1+α)`, `a2 = (1−α)/(1+α)`), com **largura em oitavas** `α = sen ω·senh((ln 2/2)·BW_oct·ω/sen ω)`
  (global `ln 2/2`; `senh` = `FUN_0045d4d0` por inferência, calcula `exp(x)` e `exp(−x)`) e
  **`BW_oct = (BW/100)·ordem·(c_k − c_(k−1))/1200`**. A **ordem = nº de cópias idênticas em cascata** (1…3), nos dois
  bancos.
- Exemplo (padrão: 16 bandas, 10…16 000 Hz, S = 100): bordas 10, 63, 135, 230, 357, 526, 753, 1054, 1457, 1994,
  2711, 3668, 4944, 6648, 8921, 11 953, 16 000 Hz; centros 25, 92, 176, 287 … 13 829 Hz (calculado).

#### 4.1.4 Processamento (`FUN_006d0fb0`, slot 7) **[DECOMP]**

1. A entrada estéreo é separada: **L = modulador, R = portadora** (`FUN_005e0a60`); com o byte de troca, invertidos.
2. Por banda ativa (e não em Hold): o modulador passa pela cascata do banco de análise; o **seguidor**
   (`FUN_006d0e70`) faz, por amostra, `x = |y|·G_k` e `env ← x + (env − x)·c` com `c = c_att` se `x > env`, senão
   `c_rel`; abaixo de um limiar de denormal o envelope vai a 0 (a banda é marcada "silenciosa" e pulada).
   **`G_k = nível_k · Wet · 1500/BW`** (o Wet entra só aqui).
3. A portadora passa pela cascata do banco de síntese da mesma banda; a saída acumula **`Σ_k port_k · env_k`**
   (`FUN_005df640`).
4. Soma `mod·g_mod + port·g_car` (níveis do §4.1.2).
5. **A saída é mono: a mesma amostra vai para L e R** (`FUN_005e0aa0`).
- Coerente com o manual: "left channel is the modulator and the right channel is the carrier"; "processes a mono
  input and outputs stereo"; "L and R labels in the MIX section swap the assignments"; bandas "4 to 128"
  **[DOC `…/plugins/Fruity%20Vocoder.htm`]**. (O manual chama o Release de "Decay"; o DFM diz "Release".)

#### 4.1.5 Uso nos demos **[DADOS `tmp/r20/voc/all.py`]**

- 1 instância: "Edlan & Ella Noël – Song For You", trilha **9 "Crusty Piano"** (contagem de `0xEC` a partir de 0 =
  Master; o pickle da rodada 14 numera essa mesma trilha como 10), alimentada pelo canal FL Keys "Crusty Piano";
  **nenhuma outra trilha roteia para ela**. Estado: 16 bandas, ordem 2, listen 2, sem troca, níveis 1,0, FreqMin 0,
  FreqMax 65 536, Scale 64, Formant 0, BW 50, A 20 ms, R 100 ms, Mod 0, Car 0, **Wet 147 (→ 1,47)**.
  Ou seja: o piano vocoda a si mesmo (L do piano como modulador, R como portadora).
- `.fst` "Mono/Stereo tape simulation" (4 bandas, Formant −700, BW 95) e "Outside the club" (Formant −10, BW 142)
  são os únicos presets fora do padrão.

### 4.2 Vocodex (classe `TTestPlug`, VMT `0x84e438`, instância `0x528` B)

#### 4.2.1 Estado (`FUN_0084efb0`, slot 3) **[DECOMP + DADOS]**

| Bloco | Tamanho | Conteúdo |
|---|---|---|
| versão | u32 | gravada `0x00080003` → **8** (palavra alta); presets antigos 2, 3, 5; carga só se 1 < v < `0x80004` |
| **13 mapas** | var. | objetos de envelope (mesmo formato dos `.fnv` de `Data/Maps`): `i32` versão (3), `i32`, `i32 n`, n × {`f64` Δx, `f64` y, `f32` tensão, `u32` flags}, + 20 B finais |
| parâmetros | 20 × i32 (`+0x120…+0x16C`) | tags 0…19 do DFM (v < 3: 7 + 12 valores, com o tag 7 inserido = 58 000) |
| bloco | 0x24 B (`+0x170`; v4: 0x23, v < 5: −3) | ver abaixo |
| (v ≥ 4) | 13 B (`+0x1FC`) | bytes de opção (nos dados sempre `00 00 00 01 01 00…`; significado **não confirmado**) |
| tom da portadora | `u8 n` + n bytes | nome do preset do sintetizador interno (ex.: `"PWM"`, `"Ensemble"`, `"Robot saw"`); vazio = nenhum |
| notas | `i32 k` + k × i32 | **notas MIDI travadas no teclado** da portadora interna (padrão: 1 nota, 60) |

Bloco `+0x170` (nomes pela tabela de campos do `TSynthEditorForm` e pelo que a carga escreve em cada controle):
`i32` **modo Hold** (`HoldModeSel`; PEAK/RMS — qual valor é qual **não confirmado**), `i32` **ordem** (`OrderSelect`
= valor − 1; 1…4), `i32` **ordem do uníssono** (`FatOrderSelect` = valor + 1; 0 = desligado nos dados), `i32`
**nº de bandas** (5…100, padrão 47), `i32` (`DraftBtn` marcado quando o campo = 0), `i32` **ModIn**, `i32` **CarIn**,
`u8` **L-R encoding**, `u8` MIN TIMES, `u8` contorno→MOD, `u8` THREADED, `u8` (posto em 1), `u8` contorno→NOISE,
`u8` contorno de CAR, `u8` contorno de CONSONANTS.

Mapas (ordem fixa do construtor `FUN_0084f730`, nomes pelo manual): 0 ganho por banda, 1 pan por banda, 2 offset de
ganho, 3 nível de ruído do modulador, 4 *pass-through* do modulador, 5 Hold, 6 Attack, 7 Release, **8 distribuição
das bandas**, 9 largura de banda, 10 *pitch shift* do modulador, 11 mistura de saturação, 12 curva de saturação
**[DECOMP + DOC `…/plugins/Vocodex_parameters.htm`]**.

#### 4.2.2 Parâmetros (`FUN_00853e80`; tags = DFM) **[DECOMP]**

| Tag | Nome (DFM) | Lei |
|---|---|---|
| 0 | Wet (0…2000, 1000) | **`(7^(v/1000) − 1)/6`** (1000 → 1,0; 2000 → 8,0 = +18 dB) |
| 1 | Soundgoodizing (0…1000) | repassado ao Soundgoodizer interno (`SG.dll`, parâmetro 5) — lei **não lida** |
| 2 / 4 | Mod / Car pass-through (0…16 000) | **`curve11(v/12800)`** (12 800 → 1,0) |
| 3 / 7 | Mod HP / Car noise HP (0…65 536) | 0 = filtro desligado; senão **`f = 40·375^(v/65536)` Hz** (até 15 kHz; 55 000 → 5,78 kHz) |
| 5 | Car LP (0…65 536) | 65 536 = desligado; senão a mesma lei (28 000 → 503 Hz) |
| 6 | Carrier noise (0…12 800) | `0,5·(7^(v/12800) − 1)/6` (×4 com contorno CAR→NOISE) |
| 8 | Hold (0…10 000) | janela `(501^(v/10000) − 1)·0,5 ms` × mapa 5 (1666 → 0,91 ms; máx. 250 ms) |
| 9 | Attack (0…10 000) | `(501^(v/10000) − 1)·2 ms` × mapa 6 (1115 → 2,0 ms; máx. 1 s) |
| 11 | Release (0…10 000) | `(501^(v/10000) − 1)·4 ms` × mapa 7 (3500 → 31,2 ms; máx. 2 s) |
| 10 / 12 | Attack / Release curve (0…2000) | passam por `FUN_005fd490(2, v/1000)` (forma **não confirmada**) |
| 14 | Band width (5…1000) | fator **`0,5·(5^(v/1000) − 1)`** (349 → 0,377) |
| 15 | Flatness (0…100) | `v/100` (espalha as sub-bandas da ordem) |
| 17 | Mod pitch (−1200…1200) | **centésimos**: o banco de análise é multiplicado por `2^(−v/1200)` |
| 16, 18, 19 | Mod BW, uníssono pitch/pan | recalculam as bandas (`FUN_00851dd0`); leis **não lidas** |

Unidades em ms são dedução: o código multiplica por `0,001·fs` e usa o resultado como contagem de amostras
(atack/release/hold), cujo significado exato (constante de tempo ou tempo total, forma da curva) **não foi
confirmado**. Os mapas 5/6/7 multiplicam ×0,125…×8 (×1 no meio) **[DOC]**.

#### 4.2.3 Distribuição das bandas (`FUN_00851dd0`, `FUN_0062cb20`, `FUN_0062cd90`) **[DECOMP + DADOS]**

- Banda k (0…N − 1) lê o **mapa 8 em x = k/(N − 1)**; o y (0…1) vira Hz pela inversa da escala do objeto
  `+0x464`, montada no construtor com `a = 440·2^(−33/12) = 65,41 Hz` e `b = 440·2^(67/12) = 21 096 Hz`:
  **`y(f) = p + (1 − p)·ln(1 + (f − a)/(b − a)·b/a)/ln(1 + b/a)`** para f ≥ a, `y = p·f/a` abaixo, com
  `p = ln 2/ln(2b/a) = 3/28`. A frequência é limitada a **≥ 20 Hz**.
- **Verificação [DADOS]:** o mapa padrão (`Band distribution - default.fnv` e todos os blobs) vai de y = 0,1429 a
  0,9918, que pela fórmula são **82,4 Hz** (`440·2^(−29/12)`, o valor que o construtor usa para o 1º ponto) e
  **20 000 Hz** (2º ponto). O mapa "Bark scale (24 bands)" dá 60, 150, 250 … 13 753 Hz (centros de Bark). Logo o
  padrão de 47 bandas é uma reta em y entre 82 Hz e 20 kHz (meio = 1,28 kHz).
- Largura de cada banda: distância (em log2) aos vizinhos, média dos dois lados, × mapa 9 × fator do tag 14 ×
  0,25, limitada a **[1/60; 8]**; cada banda tem `ordem` sub-filtros, deslocados em ±0,5·Flatness·largura
  (ordens 3/4 com correções extras); o desenho do sub-filtro (`FUN_00626690`) **não foi lido**.

#### 4.2.4 Entradas (render `FUN_00856350`) **[DECOMP + DOC + DADOS]**

- **L-R encoding desligado:** modulador = entrada da própria trilha se **ModIn = 0**; senão é pedido ao host
  (método `+0x120` do host) o *buffer* lateral, voltando à entrada própria se não vier. Portadora: **CarIn < 0** →
  *buffer* interno zerado e preenchido pelo sintetizador interno (o `Sytrus_x64.dll` da pasta `Synthesizer`, via
  `TFPForwarder_Vocodex`; manual: "built on the Sytrus synthesis engine"); **CarIn = 0** → entrada própria; > 0 →
  *buffer* lateral do host.
- **L-R encoding ligado:** a entrada é separada por `FUN_008554b0` (L = modulador, R = portadora segundo o manual
  e o *hint* do DFM "Left (modulator) / right (carrier) interlaced input").
- Manual: "Value 0: the input uses audio passing through Vocodex's own Mixer track"; "---: external inputs are
  blocked"; com a portadora interna "CAR to ---"; o modulador é analisado em mono (L+R) **[DOC
  `…/plugins/Vocodex_inputs.htm`]**.
- **Qual trilha é a entrada lateral n** não foi lido no host; nos dados, as 4 instâncias com n ≥ 1 batem com "n-ésima
  trilha que roteia para a trilha do Vocodex, em ordem de índice" (ver tabela) — **inferência dos dados, não
  confirmado**.

| Demo (trilha, contagem `0xEC`) | Entram na trilha | ModIn / CarIn | Bandas / ordem | Wet / ModVol | Observação |
|---|---|---|---|---|---|
| "Ookay – Thief" 27 e "NewStuff" 27 | 24 "Main Vox" | 0 / **−1** | 47 / 2 | 2000 / 12 800 | portadora interna `"PWM"`, notas 51, 63, 66, 73 |
| "SeamlessR – Bass Antics" 3 "Voc" | 1 "Mod", 2 "Car" | 1 / 2 | 47 / 2 | 1000 / 0 | mapa 0 editado; ModPitch −1200 |
| "Olbaid – Harmless Essentials" 71 | 72 "Carrier", 73 "Modulador" | 2 / 1 | 47 / 2 | 1000 / 12 800 | |
| "JuiceBass – Menagerie" 7 | 5 "Carrier", 6 "Modulator" | 2 / 1 | 47 / **4** | 1000 / 0 | mapas 0 e 9 desenhados |
| "Treben – RawFL" 15 | 2 "GHOSTKICK", 16 "BASS 6" | 0 / 2 | 47 / 1 | 1268 / 13 400 | |
| "Treben – All the same" 27/29/30 | 45 "SC" | 0 / 0 | 14 e 32 / 3 | 1000 / 16 000 | Att = Rel = Hold = 0 |
| "Vocodex demo" 2 "VCX" / template "Vocoder" 2 | 1 "Vocals" / 1 "Voice in" | 1/0 e 0/0 | 47 e 24 (Bark) / 2–3 | ≈1000 | |

### 4.3 Dá para aproximar no LYNX? (só o que ficou confirmado)

- **Fruity Vocoder: sim, de forma fiel** — todas as leis necessárias estão no §4.1 (bordas, BP RBJ em cascata,
  seguidor com `0,01^(1/(ms·fs/1000))`, ganhos `curve11`, `G_k = nível·Wet·1500/BW`, saída mono) e a entrada é o
  próprio estéreo da trilha (L/R), sem precisar de *sidechain*. O único demo (Song For You) cabe nisso.
- **Vocodex: só parcialmente.** Confirmados: layout do estado, nº de bandas, escala de frequências (fórmula
  verificada contra os mapas), leis de Wet/pass-through/HP/LP, tempos A/R/H em ms (por dedução) e a seleção de
  entradas. Não confirmados: desenho do sub-filtro e forma das curvas de A/R, ganho/normalização final por banda,
  Soundgoodizer, uníssono. Além disso **todo uso nos demos depende de *sidechain*** (5 casos) ou do sintetizador
  interno com preset Sytrus (2 casos), e o LYNX não tem entrada lateral.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Love Philter: tempo dos pontos | *tick* de controle a cada `N = max(4, 4·round(v·fs_int/44100/4))` amostras (v = 64, ou 8 com HQ) ≈ 1,45 ms, rampa linear entre *ticks*; **com o bit TEMPO (todos os demos): Δt em tempos**; sem TEMPO, Δt = 1 → 0,5 s (dedução, não conferida) **[DECOMP+DADOS]** |
| 1b | Love Philter: *pattern*, *loop*, sincronismo | só o envelope de índice PatNum toca (demos: 1); *loop* do ponto 0 ao último → período `ΣΔt` (1 tempo em 8 de 9 gates); no início do transporte envelopes e LFOs são reposicionados na posição da música (fase = posição mod período) **[DECOMP+DADOS+DOC]** |
| 1c | Love Philter: curvas | valor `y − 0,5·modo`; tipo 0: `q = sinal(t)(1000001^|t| − 1)`, t > 0 → `1 − ((1+q)^(1−x) − 1)/q`, t < 0 → `((1+|q|)^x − 1)/|q|` **[DECOMP]**; tipo 5 (1 caso) não decodificado |
| 1d | Love Philter: combinação | modo 0 `s·E·d_E` e `s·(1 + 2L·d_L)`; modo 1 `s + E·d_E`, `s + L·d_L`; pan = `PAN/128 + 2·s_pan`; vol = ganho da unidade × `s_vol` **[DECOMP]** |
| 1e | Love Philter: LFO | `L = (y − 0,5)·W(φ)`; período `625/(10001^(S/65536) − 1)` tempos (demos: 1, 2, 4, 8, 16, 32); triângulo (com *skew*/largura de pulso/tensão), começa em W = 0 subindo + deslocamento; travado na posição da música **[DECOMP+DADOS]** |
| 1f | Love Philter: IEF | nenhum mapa IEF não neutro nos demos → irrelevante **[DADOS]** |
| 2 | Fruity Scratcher | efeito: **saída = entrada + sample** (16 bits/2 canais/44,1 kHz pedido ao host, em *loop*, bloqueio DC 0,99, Vol linear, pan balanço); estado `u32 0xF4628/0xF462A` + **14 f32** + caminho ANSI (+ u8, i32, u8); modo motor: rate `2·SPD·T` (T = −1/0/+1), inércia `a = 0,99999 − 0,0005·ACC`; modo Hold: posição segue p0 + (Rot − 9) por suavizador de 2ª ordem (Smooth 1…4); demos: **0 usos**; render test automatiza p0 por `0xDF` **[DECOMP+DADOS+DOC]**; escala do `0xDF`, conversão de taxa pelo host **não confirmadas** |
| 3a | Patcher: soma dos cabos | `destino = Σ origem·ganho` (IPP `MulC`/`AddProductC`), sem cabo → zero; bit 0 das *flags* = cabo desligado; bit 1 = byte de classe válido; compensação de atraso por cabo **[DECOMP]** |
| 3b | Patcher: grafos não cobertos | 14 slots (7 topologias); após podar nós fora de IN→OUT, **todos** reduzem a série-paralelo aninhado; 128 cabos de controle sem bit 1 são lidos como áudio pelo LYNX **[DADOS]** |
| 4a | Fruity Vocoder | estado `v2; N; ordem 1…3; listen 0/1/2; troca u8; N×f32 níveis; 12×i32` (confirmado no leitor); bordas `fmin + (fmax−fmin)((S+1)^(i/N)−1)/S` (10…16 000 Hz, `S = int(10000^(v/128)−1)+1`); BP RBJ com `BW_oct = BW/100·ordem·largura`, cascata de `ordem` cópias; seguidor `0,01^(1/(ms·fs/1000))`; `G = nível·Wet·1500/BW`; níveis `curve11(v/128)`; L = mod, R = port., saída mono **[DECOMP+DOC+DADOS]** |
| 4b | Vocodex | estado `v8; 13 mapas; 20×i32; bloco 0x24 (modo hold, ordem, uníssono, bandas, ModIn, CarIn, L-R, contornos); 13 B; nome do tom; notas`; bandas 5…100 em x = k/(N−1) do mapa 8 com a escala `a = 65,4 Hz, b = 21 096 Hz` (verificada: padrão 82 Hz…20 kHz); Wet `(7^(v/1000)−1)/6`; pass-through `curve11(v/12800)`; HP/LP `40·375^(v/65536)` Hz; A/R/H `(501^(v/10000)−1)·{2;4;0,5}` ms; entradas 0 = própria, −1 = sintetizador interno, n = lateral **[DECOMP+DOC+DADOS]**; filtros das sub-bandas e curvas **não confirmados** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Love Philter — gates e LFOs:**
   - Gate de volume: ganho da unidade × `y(t)`, com `t` = posição da música em tempos **mod `T = ΣΔt`**
     (a partir do ponto de início do *loop*), interpolação por ponto de chegada com a curva do §1.3 (linear se
     t = 0); avaliar a cada ≈ 1,45 ms com rampa linear, ou por amostra (diferença ≤ 1 *tick*).
   - Envelope de cutoff: `c = CUT/256 + (y − 0,5)·ENV/128`, depois a lei de Hz do tipo (rodadas 16/19).
   - LFO: período `625/(10001^(S/65536) − 1)` **tempos** (TEMPO ligado), triângulo simétrico (k = w = 0 nos
     demos) com fase inicial ¾ de ciclo (W = 0 subindo) + deslocamento `+0xE0/2³²`, travado na posição da música;
     amplitude `(y − 0,5)·d_L`; pan `+2·L`, cutoff `+L` (em `c`). Para os 4 LFOs de pan com tensão ≠ 0, aplicar a
     curva da tensão em `|W|` (q do §1.5).
   - Só o envelope do PatNum (1 em todos os demos); ignorar o IEF.
2. **Fruity Scratcher:** sem uso nos demos — importar como *bypass* (passar a entrada) com aviso, a menos que
   Transport ≠ 0 com Hold = 0 (aí: tocar o sample do caminho em *loop* a `2·SPD`×, sentido pelo sinal, com
   `y = Δs + 0,99·y`, ganho Vol linear e pan balanço, **somado** à entrada). Com Hold = 1 o som depende da automação
   de p0 (não reproduzir sem ela; p0 constante = silêncio do sample).
3. **Patcher:**
   - Ler a classe do cabo do byte 24 **só com o bit 1** das *flags*; caso contrário, usar a classe da porta. Pular
     cabos com o bit 0.
   - Podar nós fora de qualquer caminho IN→OUT e substituir as heurísticas "série"/"ramos" por uma redução
     série-paralelo recursiva (§3.3) com soma e ganho por cabo; tratar Stereo Shaper/Frequency Splitter como nó
     comum quando nenhuma aresta sai dele por barramento ≠ 0. Isso cobre os 14 slots restantes.
   - Controladores (Peak/Formula/X-Y/X-Y-Z/Control Surface) não entram no áudio; usar os valores gravados dos
     parâmetros ("Bien Duro" vira um EQ estático).
4. **Vocodex e Fruity Vocoder:**
   - **Fruity Vocoder:** implementar como efeito de inserção com o algoritmo do §4.1.4 (entrada estéreo da própria
     trilha: L modula, R é portadora, com o byte de troca), bordas pelo §4.1.3, BP RBJ em cascata de `ordem` cópias,
     seguidor por banda com os coeficientes de A/R em ms, ganho `nível·curve11(Wet/128)·1500/BW`, pass-through
     `curve11(Mod/128)`/`curve11(Car/128)` e listen 0/1/2; saída mono duplicada em L/R. Importar o estado pelo leitor
     do §4.1.1.
   - **Vocodex:** sem *sidechain* no LYNX, importar como *bypass* com aviso (como hoje); registrar no log ModIn/CarIn,
     nº de bandas e o nome do tom interno. Se o LYNX ganhar entrada lateral, o §4.2 dá o suficiente para um vocoder de
     canal aproximado (bandas pela escala do §4.2.3, A/R em ms, Wet e pass-through com HP), deixando explícito que o
     formato das sub-bandas, as curvas de A/R e o Soundgoodizer são aproximações.
   - Corrigir a numeração de trilhas usada no pickle da rodada 14 ao cruzar com o `.flp` (as trilhas com Vocodex/Vocoder
     estão 1 abaixo do número do pickle quando se conta o `0xEC` a partir do Master = 0).

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| LP `FUN_00856690`, `FUN_00856570`, `FUN_00854e20`, tabela `0x8D1A08` = {64, 8}, constantes `0x856670…0x856770` (96, 44100, 2, 60, 4) | *tick*, escalas livre (`2τ`) e tempo (`τ·BPM/60`) | DECOMP |
| LP `FUN_00854540`, `TMEnvelope` slot 15 `FUN_00788080` (bit 0 = TEMPO), `FUN_00857990` (evento 0 → BPM) | escolha da escala | DECOMP |
| LP render `FUN_0085aa10` (laço de *tick*, mapas X/Y/IEF, `+0x169C/+0x16A0`, 1,0 em `0x85C2A0`), `FUN_0062aee0` (rampa), `FUN_00616620` (pan/vol) | avaliação e aplicação | DECOMP |
| LP articulador VMT `0x853B18`: `FUN_008543d0` (2 e 1 em `0x8544C4/C8`), `FUN_008544d0`, `FUN_008541f0`, `FUN_008542e0` | combinação, centro, *reset*, carga | DECOMP |
| LP `FUN_0085d050` (PatNum → `+0x10`), `FUN_008579e0` (parâmetro 5), `FUN_0085cc80`/`FUN_0085cfa0` (sincronismo, consulta 0x24, 240), evento 12 → `+0x16D1` | *pattern* ativo, sincronismo | DECOMP |
| LP `FUN_00786350`, `FUN_00785f40`, `FUN_00785730`, `FUN_007857f0`, `FUN_00787ba0`, `FUN_00787fd0`, `FUN_00788250`/`FUN_00788300` (*knobs* /128), `FUN_00783ec0` (`0x78411F…0x784196`, 1 000 001), `FUN_0060e3c0`, `FUN_007800c0`/`FUN_00780090` (0,5) | avanço, *loop*, centro, curvas | DECOMP |
| LP `TMLFO` VMT `0x77FB48`: `FUN_00788410` (+0x18 = 0,5), `FUN_00788b70`, `FUN_007886b0` (21, 0,05, 10¹⁸, 1/256, 1/128), `FUN_00788830` (10001, 2³²/625, 805 306 368), `FUN_00788a20`/`FUN_0060e870`, `FUN_00788aa0`, `FUN_00788540`, `FUN_00788600`, `FUN_0060e450`/`FUN_0060e610` | LFO: taxa, forma, fase | DECOMP |
| 43 slots LP dos demos (`tmp/r20/py/lpenv.py`) | TEMPO em todos, PatNum = 1, pontos, tensões, velocidades, IEF neutro | DADOS |
| Manual Fruity Love Philter | *pattern*, notas C5–E6, *Sync pattern changes*, LFO *bipolar/frozen*, IEF | DOC `…/plugins/Fruity%20Love%20Philter.htm` |
| Scratcher `TTestPlug` VMT `0x7285a8`; `FUN_00728d80` (estado), `FUN_00729fe0` (`ProcessParam`), `FUN_0072a630` (aplica parâmetro), `FUN_00729f10` (*reset*), `FUN_00728760` (*dispatcher*), `FUN_0072a890` (*idle*), constantes `0x7298f0…0x729918`, `0x72a350…0x72a888` | estado, parâmetros, escalas | DECOMP |
| Scratcher `FUN_0072add0` (constantes `0x72bd48` = 2500, `0x72bd50` = 0,0099, `0x72bd58` = 0,99, `0x72bd60` = 0,00999, `0x72bd80` = 0,01, `0x72bd88` = 0,99999, `0x72bd90` = 0,0005), `FUN_0072c660`/`FUN_0072c6d0` (0,99), `FUN_0072ab70`, `FUN_0072be20` (formato 44 100/16/2, `/32768`, `2/(N−1)`) | render, modos motor/scratch | DECOMP |
| Scratcher `engine_x64.dll` (exportações de timecode) | DVS | DECOMP |
| `Render tests/Fruity Scratcher.flp` (blob 125 B, `0xDF` 259 registros, `controlID 0x20408000`) + 4 `.fst` | valores, automação de p0 | DADOS |
| DFM `TSynthEditorForm` do Scratcher | nomes, Tags, faixas | DADOS (DFM) |
| Manual Fruity Scratcher | função, controles | DOC `…/plugins/Fruity%20Scratcher.htm` |
| Patcher `FUN_0079bf20`, `FUN_005da820` (`ippsMulC_32f`, `ippsAddProductC_32f`, `ippsZero_32f`; ponteiros `0x9F30B8`, `0x9F3320`, `0x9F2F28`) | soma ponderada das entradas | DECOMP |
| Patcher `FUN_00952440`, `FUN_0079ca50`, `FUN_0079b790`, `FUN_0079ef40`, `FUN_0079f0b0` | bits das *flags* do cabo, ganho, atraso | DECOMP |
| 30 Patchers de slot em `blobs14.pkl` (`tmp/r20/py/pat.py`, `sp.py`) | classificação, redução série-paralelo, contagem de *flags* | DADOS |
| Fruity Vocoder `FUN_006cf810` (slot 3 da VMT `0x6cf210`), construtor `FUN_006cfb20`, `FUN_006d4cf0`/`FUN_006d4d50`/`FUN_006d4a20`/`FUN_006d5070`, tabelas `0x723a00`/`0x723a30`; 10 blobs | layout do estado, seletores | DECOMP+DADOS |
| Fruity Vocoder `FUN_006d03a0` (0,0078125; 1500; 120 000; 10 000; 44 100; 0,001; 0,01), `FUN_006cff90` (101; 159,9; 10), `FUN_006d00b0` (0,5; 10), `FUN_006cff40`/`FUN_006cfef0` (440; 5700), `FUN_005e2260` (ln 2, ln 11…) | leis de parâmetros e bandas | DECOMP |
| Fruity Vocoder `FUN_005dbb00`, `FUN_005db530` tipo 2 (1; −2), `FUN_0045d4d0` | BP RBJ com largura em oitavas | DECOMP |
| Fruity Vocoder `FUN_006d0fb0`, `FUN_006d0ef0`, `FUN_006d0e70` (via `FUN_006d1a70`), `FUN_005df640`, `FUN_005df500`, `FUN_005e0a60`, `FUN_005e0aa0` | processamento, seguidor, saída mono | DECOMP |
| Vocodex `FUN_0084efb0` (slot 3 da VMT `0x84e438`), `FUN_0084f730`, tabela de campos do `TSynthEditorForm` (`0x85a1f8`); 34 blobs (`vx.py`) | layout do estado, mapas, bloco `+0x170` | DECOMP+DADOS |
| Vocodex `FUN_00853e80` (0,001; 1/12 800; 1/6; 0,5; 375; 40; 65 536; 5), `FUN_0062f250` (ln 2, ln 7, ln 11) | leis dos parâmetros | DECOMP |
| Vocodex `FUN_00851dd0` (−33; 440; 20; 501; 0,001; 1/60; 8), `FUN_0062cb20`/`FUN_0062cd90` (2; 1); `Data/Maps/*.fnv` | bandas, escala, tempos | DECOMP+DADOS |
| Vocodex `FUN_00856350`, `FUN_008554b0` | seleção de entradas, L-R | DECOMP |
| Demos: `0xEB`/`0xEC`/`0xE1` (`tmp/r20/voc/all.py`) | trilhas e envios das instâncias | DADOS |
| Manual: Fruity Vocoder; Vocodex (parâmetros, entradas, portadora) | nomes, faixas dos mapas, roteamento | DOC `…/plugins/Fruity%20Vocoder.htm`, `…/plugins/Vocodex_parameters.htm`, `…/plugins/Vocodex_inputs.htm`, `…/plugins/Vocodex_carrier.htm` |

### Questões em aberto

- Love Philter: valor do limite de inclinação da rampa (`+0x38` do objeto `+0xCC`); escala "livre" (0,5 s por
  unidade) não conferida por render; forma do tipo de curva 5 (*pulse*) e demais tipos; lei do pan em
  `FUN_00616620`; formato da consulta de posição 0x24 ao host; rótulo do bit 2 (tensão bipolar) e significado do
  bit 1 das *flags* do LFO; nomes exatos dos eventos de host 0/1/12.
- Fruity Scratcher: escala dos valores do `0xDF` (0…131) → p0 e o nibble alto 2 do `controlID`; se o host
  reamostra o sample para 44,1 kHz e se a velocidade compensa a taxa do projeto; efeito da leitura extra de 56 B
  nas versões < `0xF462B`; significado exato dos bytes de UI/timecode.
- Patcher: saída de um Limiter com entrada zerada ("Exoplanet" 101); significado do bit 3 das *flags* do nó;
  conversão do byte de classe por `FUN_0094a150`.
- Fruity Vocoder: se `FUN_00409d00` arredonda ou trunca (S do FreqScale); o *buffer* exato que o banco de síntese
  filtra é deduzido pelo fluxo (o de análise usa o L, confirmado).
- Vocodex: desenho dos sub-filtros (`FUN_00626690`) e da largura final; forma das curvas de A/R (`FUN_005fd490`,
  `FUN_00606730`); significado PEAK/RMS dos valores 0/1; normalização de ganho por banda; Soundgoodizer interno;
  uníssono (tags 16/18/19); os 13 bytes `+0x1FC`; como o host numera as entradas laterais (a regra "ordem das
  trilhas" é só dos dados); versões de estado < 3 (presets antigos).
