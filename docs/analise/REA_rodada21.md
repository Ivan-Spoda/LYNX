# Análise comportamental do FL Studio 2026: rodada 21 (REA) — escalas do Harmless, do Ogun e do MiniSynth que o LYNX usa

> **Natureza deste documento.** Continuação de `REA_rodada10.md` (Harmless, MiniSynth), `REA_rodada12.md` (Ogun)
> e `REA_rodada18.md` §1 (Harmor, mesma família do Harmless). Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são fórmulas e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> ou da teoria de sinais (ex.: "a serra ±1 tem harmônica n de amplitude 2/(πn)") estão explicitadas como
> **inferência**.

- **Alvos:**

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave |
  |---|---|---|---|
  | Harmless | `Harmless_x64.dll` `4100bf85ad29dc665051a73ef62017272a25aefbda1cf2f1ace39e6be376440b` (= rodada 10) | Delphi (`TTestPlug`, VMT `0x7791d0`) | `ProcessParam` `FUN_0077f630`, voz/render `FUN_00784a10`, *note-on* (uníssono, envelope) bloco `0x783…`/`0x7840xx` do mesmo projeto, cor do timbre `FUN_00787990` + `FUN_007873c0`, *detune* `FUN_00787ce0`, número de harmônicas `FUN_00787c30`, aplicação da cor/EQ `FUN_00787b40`, máscara `FUN_00783f90`/`FUN_00783f00`, LHP `FUN_00784120`, subs `FUN_00784110`, faixa do filtro `FUN_0077c170`, formas do filtro `FUN_0077ca30` (+ `FUN_0077c940`/`FUN_0077c970`/`FUN_0077c540`), *setup* por taxa `FUN_0077f170`, base do LFO `FUN_0077f0c0`, evento de tempo `FUN_0077f5e0`, *dispatcher* `FUN_007793e0`, portamento `FUN_00784630`; biblioteca: envelope `FUN_005f5f60`/`FUN_005f5f30`/`FUN_005f5fe0`/`FUN_005f6120`, curva `FUN_005fd4d0`, constantes `FUN_00614c80`, taxa de amostragem `FUN_00612f00` |
  | Ogun (e Autogun) | `Ogun_x64.dll` `c2c717df2a44217e5f3a706fc566fc19f5ce9f1e111f3bd4a4bb7c1516a1e4c6` (= rodada 12) | Delphi (VMT `0x8621f8`) | `ProcessParam` `FUN_008684e0`, mestre `FUN_0086a570`, *note-on* `FUN_0086a8e0`, *release* `FUN_0086c050`, filtro `FUN_00867cc0`, motor de timbre `FUN_00842ee0`/`FUN_00843680`/`FUN_008447d0`, constantes `FUN_0062dec0`/`FUN_00605d70` |
  | MiniSynth | `MiniSynth_x64.dll` `e00ca0194eccd4987f00a1976f3da3403c7ba78cfb5bf771f2fbf1ed0848ec85` (= rodada 10) | C++ (`CMiniSynth`) | `set` de parâmetro `FUN_180023c20`, LFO `FUN_1800249b0`, render `FUN_180025a40`, afinação `FUN_180022250`, osciladores `FUN_180021d10`, filtro `FUN_1800213e0`, envelopes `FUN_1800229c0`/`FUN_180022440` |

- **Ferramentas:** Ghidra 12.1.4 *headless* (novos: `/tmp/r21gh_Harmless`, `/tmp/r21gh_Mini`; reaproveitado só para
  leitura: `/tmp/r12gh_Ogun_x64.dll`); scripts em `tmp/r18/gh/scripts/` (`DecompRange`, `Decomp`, `Xrefs`,
  `DecompGrep`); `tmp/r18/k.py` para constantes; *dumps* em `tmp/r21/out/` (`h_range.c`, `o_range.c`, `m*.c`);
  dados com `tmp/r21/py/hl.py`, `og.py`, `ms.py` sobre `tmp/r14/blobs14.pkl`; `objdump` (`tmp/r10/*.asm`) para
  conferir instruções. Manual oficial lido via web.
- **Data:** 2026-10-13.
- **Conjunto de dados:** canais em `Demo projects/**` do *pickle* da rodada 14: **139 Harmless** (18 arquivos;
  137 *blobs* distintos), **20 Ogun**, **10 MiniSynth**. (A contagem "56" citada no pedido não foi reproduzida com
  este recorte; os números abaixo são deste recorte.)

| Marca | Significado |
|---|---|
| **[DECOMP]** | lido no código decompilado/desmontado |
| **[DOC]** | manual oficial da Image-Line (URL citada) |
| **[DADOS]** | conferido em `.flp`/`.fst`/arquivos da instalação |
| **não confirmado** | sem evidência suficiente nesta rodada |

Base das URLs do manual: `https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/`
(abreviada como `…/plugins/` abaixo). As citações do manual vieram de leitura web resumida; os trechos citados
são curtos e devem ser reconferidos na página antes de virar texto de interface.

**Convenções de leitura** (valem para os três plugins): `exp`, `ln` e o arredondamento foram identificados pelas
rotinas de biblioteca — no Harmless `FUN_00408930` = exp, `FUN_00409b50` = ln, `FUN_00409610` = seno,
`FUN_004088f0` = parte fracionária e **`FUN_00409fe0` = arredondamento ao inteiro mais próximo** (é uma única
instrução `cvtsd2si`, conferida no `objdump`) **[DECOMP]**. As constantes de *runtime* foram resolvidas pelos
inicializadores: no Harmless `0x7ff4e0` = ln 2, `0x7ff514` = ln 11, `0x7ff51c` = ln 3, `0x7ff52c` = ln 10 / 20,
`0x7ff500` = ln 2 / 12, `0x7ff530` = log₂ 440 (`FUN_00614c80`) e `0x7ff2b8` = **ln 0,01** (`FUN_005f6120`); no
Ogun `0x8f952c` = ln 3, `0x8f9508` = ln 2 / 1200 (`FUN_0062dec0`) e `0x8f92c8` = ln 0,01 (`FUN_00605d70`)
**[DECOMP]**.

---

## 1. Harmless (139 canais nos demos)

**O que o LYNX supõe** (`src/core/FlSynths.cpp`, `synthFromHarmless`): forma = serra (cores 5/6 → quadrada,
7 → triângulo; máscara ≥ 0,5 → quadrada; entre 0,05 e 0,5 → serra `1 − m` + quadrada `m`; cores 8–10 → três
serras ±7 ct); sub 1 = seno −1 oitava com nível `0,5·min(1, v/1024)`; uníssono ≥ 2 → 3 osciladores a 1/3 com
`±max(5, (41^(v67/256) − 1)·1,875)` ct e pan `±v66/128`; afinação mestre = p38/100 semitons; ataque
`0,05·(101^x − 1)` s; decay `1,5·(0,11995·(1001^x − 1) + 0,05)` s (conversão −40 → −60 dB) ou sustain 1 se 1000;
release `1,5·(0,0249·(101^x − 1) + 0,01)` s; corte `73,4·2^(7,855·v/8192)` Hz sem *keytracking*; ressonância
`0,9·x59 (+0,1 se p62 > 0)`; envelope de filtro = `x52·7,9` oitavas (não adaptativo) ou a faixa restante
(adaptativo), tempos `0,12·(1001^x − 1)` s (20 s se 1000), *sustain* 0; LFO com período `64/2^(|v89|/1000)`
tempos, formas 0/1/8 → seno, 3/6 → triângulo, 2/4/5 → pulso; destinos pitch (p42, ct), corte (`6·x58` oitavas,
**bipolar**) e volume (p25); portamento `(41^|x| − 1)/26,667` s; ganho `0,75·(3^x − 1)/2 ÷ 0,75`.

### 1.1 Cor do timbre (combo `HarmColSelect`, inteiro do estado `0x1A4`) → amplitudes das harmônicas **[DECOMP `FUN_00787990`, `FUN_007873c0`]**

A cada mudança de cor o plugin recalcula uma tabela de **528 parciais**: 4 parciais especiais (§1.3) seguidas
das harmônicas `n = 1…524`. Para cada harmônica, `FUN_007873c0` devolve uma amplitude `A(n)` e uma fase `φ(n)`
(fase guardada em ciclos — o valor 0,25 aparece como "um quarto"; a unidade é **inferência**). Escrevendo
`n` = número da harmônica (o código trabalha com `i = n − 1`):

| Cor | Nome (DFM, rodada 10) | Amplitude `A(n)` | Fase |
|---|---|---|---|
| 0 | Saw | `1/n` | 0 |
| 1 | Bright saw | `√0,5 · 2/(n + 1)` = `√2/(n + 1)` | 0 |
| 2 | Brighter saw | `2/(n + 3)` | 0 |
| 3 | Dephased saw | `1/n` | 0,25 nas harmônicas pares |
| 4 | Dephased saw 2 | `1/n` | 0,5 quando `n ≡ 2 (mod 4)` |
| 5 | Squaretooth | ímpares `1,1/n`; pares `1,1/(n·(n − 1))` | pares 0,25 |
| 6 | Squaretooth 2 | ímpares `1,1/n`; pares `1,1/(n·(n − 1)^0,2)` | pares 0,25 |
| 7 | Mole | `0,29167·(7^(1/n) − 1)` (n = 1 → 1,75; n = 2 → 0,48) | pares 0,75 |
| 8 | Double saw | `0,5/n`, mais `1/(n + 1)` nas pares | 0 |
| 9 | Triple saw | `1/(3n)`, mais `2/(3(n + 1))` nas pares, mais `4/(3(n + 3))` quando `n ≡ 0 (mod 4)` | 0 |
| 10 | Triple saw 2 | `0,5/n`, mais `0,5/(n + 1)` nas pares, mais `0,5/(n + 3)` quando `n ≡ 0 (mod 4)` | 0 |
| 11 | Custom | amplitude e fase lidas da tabela do estado (524 amplitudes + 524 fases = os 1048 floats da rodada 10 §1.5) | da tabela |

O manual confirma que a cor escolhe "pre-computed additive waveforms" e que *Custom* ressintetiza um arquivo de
ciclo único **[DOC `…/plugins/Harmless.htm`]**; o código do editor abre o diálogo "Open single-cycle audio file"
quando o último item é escolhido (`FUN_00792270`/`FUN_007922e0`) **[DECOMP]**.

Depois da cor, `FUN_00787b40` multiplica as amplitudes pelos 6 ganhos do EQ de timbre (janelas cossenoidais em
oitavas da série harmônica, `FUN_0077b450`/`FUN_0077b2d0`; ganho de banda `(3^g − 1)/2`, `g = (v + 1024)/1024`,
como na rodada 10) **[DECOMP]**.

**Dados:** cor 0 em 78/139, Custom (11) em 33, 1 e 8 em 5 cada, 5 em 4, outras ≤ 3 **[DADOS]**.

### 1.2 Máscara harmônica, *mask mix*, LHP e *detune* **[DECOMP `FUN_0077f630` casos 1, 11…23, 24, 39; `FUN_00783f90`, `FUN_00783f00`, `FUN_00784120`, `FUN_00787ce0`]**

- **Ganho de máscara** (p11…p22): `g_j = (11^x − 1)/10` com `x = v/1000` (sinal preservado) **[DECOMP]**.
- **Aplicação:** a partir da 1ª harmônica (as 4 parciais especiais ficam de fora), a harmônica `n` é multiplicada
  por **`g_((n−1) mod 12)·m + (1 − m)`**, onde `m` = *mask mix* (p23, `x`) **mais** o LFO (p24): `m ← m + U·a24`
  com `U ∈ [0, 1]` o LFO **unipolar** (§1.7) e `a24 = x24` limitado para `m` ficar em [0, 1] **[DECOMP]**.
  A máscara se repete a cada 12 harmônicas — confirma o manual ("cut the first 12 harmonics and their 12×
  multiples", **[DOC `…/plugins/Harmless.htm`]**).
- **Com a máscara padrão** (1, 0, 1, 0…; 15 dos 34 canais com m > 0 usam exatamente esse padrão **[DADOS]**):
  harmônicas ímpares ×1 e pares ×(1 − m). **Inferência (série de Fourier):** numa serra ±1 a harmônica n vale
  `2/(πn)` e numa quadrada ±1 as ímpares valem `4/(πn)`; logo "serra·(1 − m) + quadrada·(m/2)" reproduz
  exatamente o espectro (ímpares 1, pares 1 − m). O LYNX usa quadrada com nível `m` (dobra as ímpares na mistura)
  e troca para quadrada pura a partir de m = 0,5. Os outros 19 padrões de máscara dos demos (ex.: "um em cada 3",
  máscaras irregulares) **não** são serra/quadrada.
- **Low harmonics protection** (p1): `w = (11^(v/1024) − 1)/10`; para as 8 primeiras posições da tabela (as 4
  especiais + harmônicas 1–4) a amplitude volta para o valor sem máscara por `masc + (sem_masc − masc)·p_k·w`,
  com `p_k = ((1 + cos(π·log₂ r_k / log₂ 5))/2)^5` (r_k = razão de frequência da posição; 1 para r ≤ 1) — na
  prática protege a fundamental e as subs (Sub 1 recebe 1; Sub 3, 0,44; n = 2, 0,08; n = 3, 0,0006) **[DECOMP `FUN_0077b450`, `FUN_00784120`]**.
- **Detune** (p39, ±2400): **não é desafinação de uníssono**. Muda o espaçamento das parciais: a harmônica k fica
  em `f·(1 + (k − 1)·D)` com **`D = 2^(p39/1200)`** (a fundamental não se move); as subs 3/4 vão para
  `f·(1 + 0,5D)` e `f·(1 + 1,5D)` **[DECOMP `FUN_00787ce0`; constantes 1/1200, 1,0, 0,5, 1,5]**. Manual: "Changes
  harmonic tuning, ±24 semitones" **[DOC]**. Ex.: p39 = 1200 → parciais em f, 3f, 5f…; p39 = −1200 → f, 1,5f, 2f…
  O número de harmônicas geradas é `round(limite/(f·D)) + 4`, no máximo 528 (`FUN_00787c30`) **[DECOMP]**.
  **Dados:** p39 ≠ 0 em 19/139 (1200 ×9, −1200 ×5, −2400 ×3, 2400 ×1, 25 ×1) **[DADOS]**. O LYNX ignora p39.

### 1.3 Sub-harmônicas e as 4 parciais especiais **[DECOMP construtor `FUN_0077b800`, `FUN_00787990`, `FUN_0077f630` casos 2–4 e 62, `FUN_00784110`]**

- A tabela de razões de frequência começa com **{1,0; 0,5; 1,5; 2,5}** e segue com 1, 2, 3 … 524 (rampa).
- Ganhos dessas 4 posições (`param+0x580`): posição 0 = **p62/256** ("resonance self-oscillation"), posições
  1…3 = **p2/1024, p3/1024, p4/1024** (Sub 1/3/4, lineares 0…2) **[DECOMP]**.
- Amplitude-base: a posição 1 (Sub 1, razão 0,5) usa a amplitude da harmônica 1 da cor; as posições 2 e 3
  usam a cor avaliada em n = 1,5 e n = 2,5 **[DECOMP `FUN_00787990`]**. Portanto **Sub 1 = seno uma oitava abaixo,
  com amplitude (p2/1024)·A(1)** (= p2/1024 na cor Saw). Manual: "Sub 1 sits an octave below the root. Sub 3 and
  Sub 4 are harmonics of Sub 1" **[DOC]**.
- A máscara não atinge as subs (começa depois delas) **[DECOMP]**.
- **Inferência (Fourier):** com a serra ±1 do LYNX a fundamental vale 2/π ≈ 0,637; para manter a proporção do FL
  o seno da sub deveria ter nível `0,637·p2/1024` relativo à serra de nível 1 (até 1,27 com p2 = 2048). O LYNX usa
  `0,5·min(1, p2/1024)`.
- **Dados:** p2 > 0 em 19/139; p3 > 0 em 14; p4 > 0 em 12 **[DADOS]**.

### 1.4 Afinação mestre e uníssono **[DECOMP `FUN_0077f630` casos 38, 65–70, 99; `FUN_00784a10`; *note-on* `0x783ec0…0x784140` em `h_range.c`]**

- **Master pitch** (p38): o plugin guarda `p38 − 1200` e soma na variável de tom da voz, que está em
  **centésimos**; a frequência é `440·2^((c/100 + 3)/12)` (`log₂ 440` + …/12) **[DECOMP]**. O −1200 fixo faz uma
  nota com `c` = 0 soar a 261,6 Hz (**inferência**: supõe que o tom do host vem em centésimos relativos ao C5 do
  FL; coerente com o C5 = dó central). Ou seja, **p38 = transposição pura em centésimos** — confirma o LYNX.
  Manual: "Main pitch control, ±24 semitones" **[DOC]**.
- **Ordem** (p65): `n` subvozes; ganho por subvoz **`1 − (3^(1 − 1/n) − 1)/2`** (1 para n = 1) **[DECOMP]**.
- **Posição** de cada subvoz k = 0…n − 1: `u = 2k/(n − 1) − 1` ∈ [−1, 1].
- **Pan** (p66): `pan_k = u·p66/128`, somado ao pan do host **[DECOMP]** → confirma o `±p66/128` do LYNX nas pontas.
- **Afinação** (p67): `S = 1,875·(41^(p67/256) − 1)` (p67 = 128 → 10,1; 256 → 75); desvio da subvoz =
  **`±|u|·d_k·S` centésimos**, somado à variável de tom em centésimos (com p99 o valor ainda é multiplicado pela
  velocidade) **[DECOMP]**. O sinal alterna por paridade de k (e inverte na metade de cima quando n é ímpar).
  `d_k` vem de uma tabela de 9 valores por tipo (p70) **[DECOMP, tabela `0x7ee5c8`]**:

  | p70 | Tipo | d₀…d₈ |
  |---|---|---|
  | 0 | Classic | 0,877 1,069 0,929 1,181 1,031 1,129 0,887 1,117 0,977 |
  | 1 | Uniform | 1,0 (todos) |
  | 2 | Blurred | 0,8905 1,0764 0,9116 1,1025 1,0341 1,1669 0,854 1,2063 1,0 (e fase extra exponencial) |

  Para n = 3: desvios `+d₀S`, 0, `−d₂S` — o "0, ±S" do LYNX é a aproximação do tipo Uniform.
- **Fase** (p68): `k/n·p68/128`; **variação** (p69) multiplica o tempo de ataque da subvoz por
  `2^((|u| − 1)·3·p69/128)` (as do centro atacam mais rápido) **[DECOMP]**.
- **Dados:** n = 1 em 41/139, 2 em 24, 3 em 18, 5 em 16, 7 em 14, 9 em 14; p70 = 2 em 90; p66 = 128 em 95;
  p67 = 128 em 30, 0 em 19 **[DADOS]**.
- Nível total: o FL soma `n` subvozes com ganho `1 − (3^(1−1/n) − 1)/2` cada; o LYNX usa 3 osciladores a 1/3.
  **Inferência (soma de vozes descorrelacionadas):** o RMS do FL cresce como `√n·ganho(n)` (n = 3: 0,80) e o do
  LYNX como `√3/3` = 0,58 (≈ −2,9 dB para n = 3).

### 1.5 Envelope de volume **[DECOMP `FUN_0077f630` casos 26–30, `FUN_0077f170`, voz `FUN_00784a10`, `FUN_005f5f60`, `FUN_005f5fe0`, `FUN_005f5f30`]**

- **Tempos** (convertidos em amostras com a taxa do host): ataque `A = 0,05·(101^x − 1)` s; decay
  `D = 0,11995·(1001^x − 1) + 0,05` s, **p27 ≥ 1000 = infinito**; release `R = 0,0249·(101^x − 1) + 0,01` s
  (`x = v/1000`) **[DECOMP]** → confirmam as fórmulas do LYNX.
- **Decay e release**: suavizador de 1 polo com coeficiente **`exp(ln 0,01 / N)`** por amostra (N = tempo em
  amostras) que persegue zero → **queda exponencial que chega a 1 % (−40 dB) em N** e continua (não há
  *sustain*: decai para o silêncio) **[DECOMP]**. Decay infinito = coeficiente 1 (segura o nível) **[DECOMP
  `FUN_005f5f30`]**. Logo o fator **1,5** do LYNX (tempo a −60 dB de uma exponencial = 1,5 × tempo a −40 dB) está
  **correto**, e `p27 = 1000 → sustain 1` também.
- **Ataque**: rampa **linear** até 1. Para `A ≥ 0,2 s` vai de 0 a 1 em A. Para `A < 0,2 s` (`param+0x5D0` =
  0,2·fs) a rampa tem inclinação `1/(0,2 s)` e **começa no nível `(1 − A/0,2)²`**, durando `A·(2 − A/0,2)`; A = 0
  começa em 1. Manual: "The first few ms set the starting level" **[DECOMP + DOC `…/plugins/Harmless.htm`]**.
- **Suavizador de saída** (objeto `+0x144`): todo o envelope passa por um segundo filtro de 1 polo (1 % em
  `min(A, 30 ms)` durante o ataque, **80 ms** depois do ataque, 30 ms no release) **[DECOMP; constantes 0,03 e
  0,08]**. Efeito audível só em decays/releases curtos (≤ ~0,1 s).
- Velocidade → ataque (p29): A × `(1 − min(1; 1,125·vel))`; velocidade → release (p30) multiplica R por um valor
  do host (não identificado) **[DECOMP]**.
- **Dados:** p27 = 1000 (decay infinito) em 81/139; p26 = 0 em 96; p28 = 0 em 26, 250 em 22 **[DADOS]**.

### 1.6 Filtro **[DECOMP `FUN_0077f630` casos 47–58, `FUN_0077c170`, `FUN_0077ca30`, `FUN_0077c940`/`FUN_0077c970`, `FUN_0077c540`, voz `FUN_00784a10`]**

O filtro do Harmless é uma **curva tabelada aplicada às amplitudes das parciais** (rodada 10 §1.3); o LYNX o
aproxima por um biquad. O que ficou confirmado:

- **Domínio:** posição em **log₂ Hz**; a curva é posta sobre `log₂(f_parcial)` com índice
  `(log₂ f − pos)·W + centro`, onde **`W = 1024·2^(1 − 0,004·p56)`** pontos por oitava (p56 = 500 → 512) **[DECOMP]**.
- **Faixa:** `lo = log₂(440·2^(−31/12)) − max(0, M·eL − 1)`, `hi = log₂ 17000 + max(0, M·eR − 1)`, com
  `M = 682,67/W = (2/3)·2^(0,004·p56 − 1)` **[DECOMP `FUN_0077c170`; 17 000 = `0x4684d000`; −31, 440]** — a mesma
  regra do Harmor (rodada 18 §1.2). As extensões por tipo vêm de `FUN_0077c940` **[DECOMP]**:

  | Tipo (combo, sem separadores) | Pontos da tabela | centro / eL / eR |
  |---|---|---|
  | 0 Classic LP, 4 Crude LP, 8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 13 Leaky LP | 2048 / 1024 / 682 / 1024 / 1365 / 1024 / 1024 | 0 / 1 / 0 |
  | 1 Classic BP, 2 Classic BS, 5 Crude BP, 6 Crude BS, 14 Phaser, 15 Band phaser, 16 Flat BP | 4096 / 4096 / 2048 / 2048 / 4096 / 4096 / 2048 | 0,5 / 1 / 1 |
  | 3 Classic HP, 7 Crude HP | 2048 / 1024 | 1 / 0 / 1 |
  | 12 Fin LP | 4096 | 0,5 / 1 / 0 |

- **Corte (p54):** `pos = lo + (p54/8192)·(hi − lo)` e **`fc = 2^pos` Hz** **[DECOMP]**. Com o Classic LP e p56 = 500
  (M = 4/3): `lo = log₂ 58,3`, `hi = log₂ 17000`, **`fc = 58,3·2^(8,19·p54/8192)`** (0 → 58 Hz; 4096 → 995 Hz;
  8192 → 17 kHz). A fórmula do LYNX (`73,4·2^(7,855·v/8192)`) é a versão sem margem (válida para p56 ≤ 250 nos
  passa-baixas). **p56 = 500 em 57/139** canais **[DADOS]**.
- **Forma do Classic LP** (tipo 0, centro 0): ganho 1 abaixo de `fc`; acima, `10^(−72·t/20)·(1 − t^16)` com
  `t` ∈ [0, 1] ao longo dos 2048 pontos → **−72 dB em 2048/W oitavas** (p56 = 500: 4 oitavas, ≈ −18 dB/oitava;
  p56 = 0: −72 dB/oitava; p56 = 1000: ≈ −4,5 dB/oitava) **[DECOMP `FUN_0077ca30` caso 0; −72 e ln10/20]**.
  Os outros tipos não foram tabelados (não confirmado).
- **Keytracking (p57):** `pos += (log₂ f_voz − 7)·p57/1000` → **referência 2⁷ = 128 Hz**, fator 1 = 100 %
  (constante −7,0 em `param+0x6F8`) **[DECOMP]**. O limite inferior também acompanha (`lo + termo de tecla`).
  **Dados:** p57 = 1000 em 43/139, ≠ 0 em 51 **[DADOS]**. **O LYNX não define `keyTrack` para o Harmless.**
- **Envelope de filtro** (p48 ataque, p49 decay): ataque linear 0 → 1 em `0,12·(1001^x − 1)` s (**p48 ≥ 1000: não
  sobe nunca**); decay linear 1 → 0 em `0,12·(1001^x − 1)` s (**p49 ≥ 1000: segura em 1**) **[DECOMP casos 48/49,
  incrementos por amostra]**; depois a curva de tensão (p50 ataque, p51 decay) com `q = sinal(t)(1000001^|t| − 1)`:
  t > 0 → `1 − ((1+q)^(1−E) − 1)/q`, t < 0 → `((1+|q|)^E − 1)/|q|` — **a mesma curva do Love Philter** (rodada
  20 §1.3) **[DECOMP `FUN_005fd4d0`, 1000001]**. Padrão p51 = −300 (63/139 canais): `q = −62,1`; o nível cai à
  metade com 16 % do tempo.
- **Quantidade (p52) — modo não adaptativo** (p47 = 0): **`pos += (E − 0,5)·x52·(hi − lo)`** — o envelope é
  **centrado**: E = 0 abaixa meia quantidade, E = 1 sobe meia quantidade **[DECOMP: `(E − 0,5)` com 0,5 em
  `0x787300`]**. O LYNX soma `x52·7,9` oitavas sem o −0,5 (erro de centro). Usado em **11/139** canais **[DADOS]**.
- **Modo adaptativo** (p47 = 1, 128/139 canais): `x52 > 0` → `pos += (hi − pos)·E·x52`; `x52 < 0` →
  `pos −= (pos − lo_kt)·E·|x52|` **[DECOMP]**. Confirma o LYNX (que usa `log₂(17000/fc)` e `log₂(fc/73,4)`; difere
  só pelas margens e pelo termo de tecla no limite inferior). Manual: "Alt limits modulation to the range from the
  current cutoff to the frequency limit" **[DOC]**.
- Velocidade → quantidade (p53): multiplica x52 pela velocidade **[DECOMP]**.
- **Ressonância:** p59 (`x`), p61 deslocamento (`v/1200` oitavas), p62 auto-oscilação (`v/256`, ganho da parcial
  especial 0) **[DECOMP]**. A largura do pico e a conversão para um Q de biquad **não foram decodificadas** — a
  regra `0,9·x59 (+0,1)` do LYNX é aproximação sem base no código.
- Ordem dos tipos: confirmada pelas extensões da tabela acima (LP/BP-BS/HP) e coincide com a do Harmor
  **[DECOMP]**; o *switch* do LYNX (1/5/14/15/16 → BP, 2/6 → BS, 3/7 → HP, resto LP) está coerente.
- p55 (escala oct/Hz) troca o eixo da curva para frequência linear (`W·0,9/f_c`) **[DECOMP]**; 14/139 canais.

### 1.7 LFO **[DECOMP `FUN_0077f630` casos 24, 42, 58, 86–89; `FUN_0077f0c0`, `FUN_0077f5e0`, `FUN_007793e0` (evento 0xD), voz `FUN_00784a10`]**

- **Taxa** (p89): incremento de fase por amostra = `sinal(v)·2^(|v|/1000)·BPM/(3840·fs)` ciclos, com o BPM vindo do
  evento de tempo (`param+0x2CC`, padrão 120) **[DECOMP; 3840 = `0xF00`]** → **período = 64/2^(|v|/1000) tempos**
  (3000 → 8; 6000 → 1) — confirma o LYNX; v = 0 → parado; v < 0 → fase andando para trás.
- **Global** (p87): com p87 > 0 a voz copia a fase do LFO global, que é **reposicionada pela posição da música**
  (fase = frac(posição·taxa)) no evento 0xD do *dispatcher*; sem p87 a fase é da voz **[DECOMP]**. 112/139 canais
  têm p87 = 1 **[DADOS]**. O LYNX não liga `global`.
- **Formas** (p86; `φ` = fase em ciclos, "round" = inteiro mais próximo) **[DECOMP]**:

  | p86 | Nome | Valor L ∈ [−1, 1] |
  |---|---|---|
  | 0 | Sine | `sin(2π(φ + 0,5))` = −sin(2πφ) |
  | 1 | Smooth saw | tabela interna (não decodificada) |
  | 2 | Smooth pulse | `sinal(s)·(1 − (1 − |s|)^6)`, s = o seno acima |
  | 3 | Saw | `2·(ψ − round ψ)`, ψ = φ − 0,5 (rampa subindo) |
  | 4 | Pulse | ±1 conforme a paridade de `round(2φ − 0,5)` |
  | 5 | Stairs | `(round(3φ − 0,5) mod 3) − 1` (degraus −1, 0, 1) |
  | 6 | Triangle | `|4·(d − round(d − 0,5)) − 2| − 1`, d = φ + 0,25 |
  | 7 | Sickle | `2u³ − 1`, u = fase em [0, 1) |
  | 8 | Multisine | `(sin θ + sin(0,2(θ + 7)))/2`, θ = 2π(φ + 0,5) |
  | 9…13 | Filter env / Filter freq / Voice random / Velocity / Pitch | fontes, não osciladores |

- **Ataque** (p88): rampa linear de 0 a 1 em `0,05·(101^x − 1)` s; o LFO é multiplicado pelo **quadrado** dessa
  rampa **[DECOMP]**.
- **Saídas:** bipolar `L·r²` e **unipolar `U = (L·r² + 1)/2`** **[DECOMP]**.
- **Destinos e unidades** **[DECOMP]**:
  - **Pitch** (p42): bipolar, `±12·(101^|x| − 1)` **centésimos** somados ao tom (máx. ±1200 ct) — confirma o LYNX.
  - **Corte** (p58): **unipolar**: `pos += U·A`, com `A = 6·x58` **oitavas**, limitado para não passar de `hi`
    (A > 0) ou `lo` (A < 0). O LYNX aplica ±A bipolar; o equivalente é **deslocar o corte de A/2 e modular ±A/2**.
  - **Mask mix** (p24): unipolar (§1.2).
  - **Não há destino de volume** no LFO principal. O p25 ("Stereo tremolo depth", DFM) alimenta um **tremolo
    estéreo próprio com oscilador fixo de 2,5 Hz** (`param+0x7A4` = 2,5/fs) aplicado na saída global
    (`FUN_00604a90`) **[DECOMP]**; a forma exata da aplicação não foi decodificada. O manual lista os alvos do LFO
    como "harmonic mask mix, pitch, filter cutoff, phaser width and offset, harmonizer" **[DOC]**. O mapeamento
    p25 → LFO de volume do LYNX, com o período do LFO principal, está **errado**.
  - **Vibrato** (p41): oscilador próprio de **6 Hz** (`6/fs`), profundidade `0,075·v` **centésimos**, com entrada
    gradual em 1/3 s (`3/fs` por amostra, ao quadrado) **[DECOMP]**.
- **Dados:** p86 = 0 em 102/139; p89 = 3000 em 67; p42 ≠ 0 em 55; p58 ≠ 0 em 31; p25 ≠ 0 em 32 **[DADOS]**.

### 1.8 Portamento, mono e volume mestre **[DECOMP `FUN_0077f630` casos 36, 43–45; `FUN_00784630`]**

- **Tempo** (p45): `T = (41^|x| − 1)/26,667` s, convertido em blocos de controle **[DECOMP]** — confirma o LYNX.
- **Forma:** p45 > 0 → aproximação **exponencial** (coeficiente `exp(ln 0,01/blocos)`: 99 % do intervalo em T);
  p45 < 0 ("inverted") → rampa **linear** em `T` **[DECOMP; o limiar é 0,0 em `0x784838` como float]**.
- Em um dos caminhos de *note-on* a diferença de altura é reduzida em passos de 1200 ct enquanto passar de
  1250 ct (desliza no máximo ~1 oitava); a condição exata **não foi confirmada**.
- p43 = portamento, p44 = monofônico (manual: "legato: monophonic mode that slides between overlapping notes")
  **[DOC]**. Dados: p44 = 1 em 45/139, p43 = 1 em 52, p45 = 400 em 58 **[DADOS]**.
- **Volume mestre** (p36, 0…2000): `x = v/1000`; ganho **`0,75·(3^x − 1)/2`** (x = 1 tratado exatamente como 0,75)
  **[DECOMP; 0,75 em `0x781170`, ln 3]** — confirma a curva do LYNX. A normalização "0,75 = 0 dB" é escolha do
  LYNX (o nível absoluto do Harmless frente a outros geradores não foi medido nesta rodada).

---

## 2. Ogun (20 canais nos demos)

**O que o LYNX supõe** (`src/core/FlGenerators.cpp`, `synthFromOgun`): forma escolhida por *fullness*/*richness*
(seno/triângulo/serra + parcial +1 oitava se fullness > 300); afinação `P1 + P2/100` st; decay/release
`((21^(v/10000) − 1)·997,5 + 50)` ms, 10000 = infinito (release infinito → 30 s), ataque 0; uníssono → cópias a
±10 ct com pan `±P13/128` e nível ×0,6; filtro só com ordem > 0: LP/HP `c = 0,000665(1501^u − 1) + 0,003`
(HP ≤ 0,6), BP/BS `c = min(1,0001; 0,0004(1501^u − 1))`, **`fc = 22050·c·0,9^(ordem − 1)`**, ressonância
`min(0,95; P18/256)`; ganho `(3^(P0/128) − 1)/2`.

### 2.1 Nível, afinação **[DECOMP `FUN_0086a570`]**

- **Master level** (P0): `x = P0/128`; ganho **`(3^x − 1)/2`** (x = 1 → 1,0 exato) **[DECOMP; ln 3 em `0x8f952c`]**
  — confirma o LYNX (0 → 0; 128 → 1; 256 → 4). O manual descreve a faixa como "0 to 200%" **[DOC
  `…/plugins/Ogun_master.htm`]** (rótulo de interface; a curva é a do código).
- **Afinação:** `100·P1 + P2` centésimos **[DECOMP]**; manual: coarse ±24 st, fine ±50 ct **[DOC
  `…/plugins/Ogun_master.htm`]** — confirma o LYNX.

### 2.2 Decay e release do timbre **[DECOMP `FUN_008684e0` casos 5–9, `FUN_0086a8e0`, `FUN_0086c050`, `FUN_00842ee0`, `FUN_00843680`]**

- O `ProcessParam` guarda `P6·0,0001` (ou −1 se **P6 = 10000** = infinito) e `P7·0,0001`; na nota, o tempo em
  amostras é **`((21^(P·0,0001) − 1)·997,5·a + 50)·0,001·fs`**, onde `a` é a saída do articulador "decay/release
  time" (1 quando desligado) **[DECOMP; 21, 997,5, 50, 0,001]** — confirma a fórmula do LYNX.
- **Release não tem infinito:** P7 = 10000 dá `(20·997,5 + 50)` ms = **20,0 s** **[DECOMP: o teste de −1 só existe
  para o decay]**. O LYNX usa 30 s.
- O que o tempo significa: o motor de timbre (FFT, quadros) monta as taxas de queda por harmônica dividindo pelo
  tempo, multiplicando pelos mapas de *Decay/Release EQ* e por **ln 0,01**, e exponenciando **[DECOMP
  `FUN_00843680`, constante `0x8f92c8` = ln 0,01]**. **Inferência:** cada harmônica cai 40 dB (para 1 %) no tempo
  × fator do mapa. A normalização por quadro e os mapas por harmônica **não foram seguidos até o fim** — o fator
  1,5 (−40 → −60 dB) para o LYNX fica como **inferência, não confirmado**.
- Manual: "DEC: Timbre decay time, combined with the per-harmonic Decay mapping"; "REL: … combined with the
  Release mapping"; o envelope de volume (articulador) "must be turned on with 'Enable Envelope / LFO', which is
  off by default" **[DOC `…/plugins/Ogun_synthesis.htm`]**. Os articuladores dos 20 canais **não foram decodificados**
  (ataque real não confirmado).

### 2.3 Timbre (forma de onda) **[DOC + DECOMP parcial]**

- *Richness*: "Number of active harmonics, from 31 to 32,767 in 11 steps" **[DOC `…/plugins/Ogun_synthesis.htm`]**.
  **Inferência:** 31…32767 em 11 passos = `2^(r + 4) − 1` com r = 1…11 = `P52 − 5`.
- *Fullness*: "Blends from the seeded spectrum at −100 to the full spectrum at +100, limited by Rich" **[DOC]**;
  internamente `P8/1000` + articulador, limitado a [−1, 1] **[DECOMP]**.
- *Seeds* passam por um *hash* inteiro (rodada 12) e geram o espectro **[DECOMP]**; a lei de amplitude do "full
  spectrum" **não foi decodificada**. Portanto a escolha seno/triângulo/serra do LYNX é **heurística sem base
  confirmada**.

### 2.4 Uníssono **[DECOMP `FUN_0086a8e0`, tabela `0x8e7b00`]**

- Mesma geometria do Harmless: `u = 2k/(n − 1) − 1`, sinais alternados, tabela `d_k` = a do tipo Classic do
  Harmless (0,877 1,069 0,929 1,181 1,031 1,129 0,887 1,117 0,977).
- **Afinação** (P15): `S = 5·(11^(P15·a/256) − 1)` **centésimos** (P15 = 128 → 11,6 ct; 256 → 50 ct;
  `a` = articulador "unison pitch"); subvoz em `±|u|·d_k·S` ct (razão `exp(… · ln2/1200)`) **[DECOMP; 11, 5,
  ln 2/1200]**. O LYNX usa ±10 ct fixos.
- **Pan** (P13): `u·P13/128` **[DECOMP]** — confirma o LYNX.
- **Volume** (P14): por subvoz `0,3` para n ≤ 2; para n ≥ 3, `0,3·(v + 2(1 − |u|)(1 − v))/√n` com `v = P14/128`
  **[DECOMP; 0,3, 2, 1/√n]**.
- **Fase** (P16) espalha as fases iniciais **[DECOMP]**.
- **Dados:** n = 1 em 9/20, 2 em 3, 4 em 2, 5 em 3, 7 em 1, 9 em 2; P15 = 256 em 5 **[DADOS]**.

### 2.5 Filtro **[DECOMP `FUN_00867cc0` + *note-on* `FUN_0086a8e0` + taxa `FUN_00612f00` (Harmless, mesma biblioteca)]**

- Ordem (P50): 0 = sem filtro; 1/2/3 = **cascata de 1/2/3 biquads RBJ iguais** com o **mesmo** corte; nos LP/HP
  cada estágio usa `Q^(1/ordem)` **[DECOMP: `param+0xF30` = ordem − 1, `+0xF34` = 1/ordem, potência]**. **Não há o
  fator 0,9^(ordem − 1)** que o LYNX aplica.
- **Escala de frequência:** `ω = (c + c₀)·2π·s`, limitado a **2,8495 rad**, com `s = 44100/fs` (o campo `+0x20`
  do objeto de taxa vale `44100/fs` — `FUN_00612f00` do Harmless; **inferência**: o Ogun usa a mesma biblioteca,
  a estrutura `+0xCC` é a mesma). Como `ω = 2π·fc/fs`, isso dá **`fc = 44100·(c + c₀)` Hz**, independente de `fs`
  (com sobreamostragem, `s` é dividido por 2 — `param+0x59C`). A hipótese `s = 0,5` da rodada 12 está
  **corrigida**: o LYNX usa 22050 e dá metade da frequência.

  | Tipo (P49) | `c` (u = P17/256) | c₀ | Limite de c |
  |---|---|---|---|
  | 0 Low pass | `0,0006646667·(1501^u − 1)` | 0,003 | — |
  | 1 Band pass, 2 Band stop | `0,00039993·(1501^u − 1)` | 0,0001 | 1,0001 |
  | 3 High pass | `0,00039993·(1501^u − 1)` | 0,0001 | 0,6001 |

  **[DECOMP: constantes `0x868378…0x8683e0`]**. O LYNX usa para o HP a fórmula do LP (errado).
- **Ressonância** (P18, `r = P18/256`): com **FLAT** (P51 ≠ 0) usa `r·(u + 1)/2` **[DECOMP]**. LP/HP:
  `Q = 0,1` (r ≤ 0), `50` (r ≥ 1), senão `0,02495·(2001^r − 1) + 0,1`; BP/BS: largura `0,01` (r ≤ 0), `12` (r ≥ 1),
  senão `0,1199·(101^r − 1) + 0,01` **[DECOMP]**. A tradução para o 0…1 do LYNX não tem base no código (o
  `min(0,95, r)` atual não reproduz Q = 0,1 em r = 0).
- **Dados (13/20 canais com ordem > 0):** **[DADOS + cálculo com as fórmulas acima]**

  | Canal | Tipo/ordem | u | fc FL | fc LYNX hoje |
  |---|---|---|---|---|
  | Kick | LP/3 | 0,242 | 275 Hz | 112 Hz |
  | Clap | LP/3 | 0,719 | 5,7 kHz | 2,3 kHz |
  | High Hat | BP/2 | 0,922 | 14,9 kHz | 6,7 kHz |
  | Snare | LP/3 | 0,289 | 346 Hz | 140 Hz |
  | Swebass 1 / 2 | LP/3 | 0,469 / 0,410 | 1007 / 692 Hz | 408 / 280 Hz |
  | Midnight Lady | LP/2 | 0,816 | 11,6 kHz | 5,2 kHz |
  | Supersaw | LP/1 | 0,375 | 558 Hz | 279 Hz |
  | Airguitar 3 | BS/3 | 1,0 | 20 kHz (limite) | 10,7 kHz |
  | Trumpet | LP/1 | 0,844 | 14,1 kHz | 7,1 kHz |
  | Silverswirll | LP/2 | 0,672 | 4,1 kHz | 1,8 kHz |
  | Padsaw | LP/3 | 0,246 | 280 Hz | 114 Hz |
  | Turbular Bells 2 | LP/1 | 1,0 | 20 kHz (limite) | 20 kHz |

---

## 3. MiniSynth (10 canais nos demos)

**O que o LYNX supõe** (`synthFromMiniSynth`): Kind `int(9p)`; SAW+SAW/SQR+SQR → 2ª forma com `10·(0,2 + m)` ct,
SQR+SAW → serra com `10·m` ct; supersaw → 3 serras `±1200·log₂(1 + 0,05m)` ct; transposição `int((p − 0,5)·24)`;
corte `2^(4 + 10,3F)`; ressonância `0,99p`; envelopes com `k = 0,95p + 0,05`; release com `k` aplicado de novo;
quantidade do envelope de filtro `10·(2p − 1)` oitavas; LFO: tabela de períodos {1; 1,5; 2; 3; 4; 8; 16; 32; 64;
16; 32; 64} tempos, quantidade linear `4·a` oitavas / `12·a` semitons; portamento `v2·0,5` tempos.

### 3.1 Parâmetros confirmados **[DECOMP `FUN_180023c20`]**

- Voices (p0): `int(2p)` → 0: *flag* poly; 1: mono; 2: *flag* lead (`+0x39CC/+0x39CD`) **[DECOMP]**; rótulos
  Poly/Mono/Lead **[DOC `…/plugins/MiniSynth.htm`]**. Todos os 10 canais: 0 **[DADOS]**.
- Transpose (p1): `int((p − 0,5)·24)` semitons **[DECOMP]** — confirma o LYNX.
- Slide (p2): coeficiente por **bloco de 32 amostras** `k = 0,9·(1 − p)^4 + 0,01`; a nota "deslizante" anda
  `(nota − atual)·k` por bloco (exponencial) **[DECOMP `FUN_180022250`, `FUN_180025a40` (blocos ≤ 32)]**.
  **Inferência:** tempo a 99 % = `32·ln 0,01/ln(1 − k)/fs` (p = 0: ~1,4 ms; p = 1: 0,33 s a 44,1 kHz). Manual: "The
  page has no dedicated slide control. Portamento happens automatically in Mono (always) and Lead (when notes
  overlap)" **[DOC]**. Em poly a nota começa na própria altura (**não confirmado**). p2 = 0 em todos os demos.
- OSC Kind (p5): `int(9p)` (0…9) **[DECOMP]** — confirma. Noise (p7): `p²` — confirma. Peak (p9): `0,99p` — confirma.
- LFO Shape (p17) e Dest (p18): `int(2p)` **[DECOMP]**.
- **LFO Rate** (p19): `int(11p)` → **R = 0,25; 0,375; 0,5; 0,75; 1; 2; 4; 8; 16; 64; 128; 256** **[DECOMP: doubles
  `0x3fd0…0x4070` em `+0x3A28`]**.
- LFO Amount (p20) e EGF Amount (p16): `2p − 1` **[DECOMP]**.
- Envelopes (p12–15, p21–24): ver §3.4.

### 3.2 Osciladores **[DECOMP `FUN_180021d10` + `objdump` `0x180021d75…0x180021da7`]**

- **Razão `r` da 2ª voz** (Kinds SAW+SAW, SQR+SAW, SQR+SQR): `r = 2^(trunc(24·m' − 12)/12 + 0,00625)`, com
  `m' = OSC Mod (+ 12·L se o LFO vai para OSCMOD)` → **semitons inteiros de −12 a +12 mais 7,5 ct fixos**
  (`0,00625·1200`) **[DECOMP: 24, 12, 1/12, 0,00625]**. O manual fala em "±24 semitones" **[DOC]**; o código
  limita a ±12 (vale o código). O LYNX usa alguns centésimos — errado.
- SAW+SAW: 2ª serra em `f·r`; SQR+SAW: quadrada em f + serra em `f·r`; SQR+SQR: 2ª quadrada em `f·r` **[DECOMP]**.
- SUPERSAW: 8 serras em `f·((k·a − b)·m + c)` (as constantes da rodada 10: `1 + (0,0125k − 0,05)m`) **[DECOMP]**.
- SAW SPREAD: `0,5f(1 − 0,01m)`, `f`, `2f(1 + 0,01m)`; SPREAD 2: `1,5f(1 − 0,01m)` no lugar de `0,5f` **[DECOMP]**
  — confirma a escolha de oitavas/quinta do LYNX (o desvio de ≤ 1 % é ignorado lá).
- Afinação: `f = 220·2^((nota_deslizante + transpose + deriva + 12·L(FRQ) − 57)/12)`; deriva aleatória suavizada
  (`0,01` por bloco) **[DECOMP `FUN_180022250`]**.
- **Dados:** Kind 5 (supersaw) em 7/10, Kind 3 (SQR+SAW) em 2, Kind 0 em 1 **[DADOS]**.

### 3.3 Filtro **[DECOMP `FUN_1800213e0`]**

- `oitavas = 4 + 10,3·F + Kbd·(nota − 48)/12 + 14·Vel·(vel − 0,5) + 0,103·deriva (+ 4·L se LFO → FLT)
  (+ 4·MW se MW → Filter)`; `fc = 2^(oitavas + envelope)` limitado a 20 kHz **[DECOMP: 4, 10,3, 48, 1/12, 14, 0,5,
  0,103, 20000]** — confirma o corte do LYNX.
- **Velocidade → corte:** p11 = 1,0 em 3/10 canais (Riser, Energic Pads ×2) **[DADOS]** → ±7 oitavas entre vel 0 e 1;
  o LYNX ignora. Kbd = 0 em todos.
- A forma do envelope (rodada 10: `fc·(1 + (2^(10a) − 1)·E)`) **não foi reverificada** nesta rodada; o pico
  (+10a oitavas em E = 1) coincide com o `10·(2p − 1)` do LYNX.

### 3.4 Envelopes **[DECOMP `FUN_1800229c0`, `FUN_180022440`, `FUN_180023c20` casos 12–15, 21–24]**

- Taxa por amostra: `0,0002/(K·k⁴)` com `k = 0,95·p + 0,05`, `K = 500` (ataque) ou `50` (decay/release);
  p < 0,001 → instantâneo **[DECOMP]** — confirma a rodada 10.
- **Release de amplitude** (p24): o código chama a mesma rotina com `p' = 0,95·p + 0,05` e multiplica a taxa por 10
  → `k = 0,95·p' + 0,05` (dois mapeamentos) **[DECOMP: 0,95, 0,05, 10]**. **O LYNX aplica o mapeamento três vezes**
  (`tau(0,95·k(v) + 0,05)` e `tau` aplica `k` de novo): para p = 0,56 dá 0,59 s contra 0,52 s do FL (−60 dB).
- Sustain = p (p14, p23). A troca de estágio é por amostra; as taxas **não** dependem de `fs` no código
  (**inferência**: tempos escalam com 44,1 kHz/fs).

### 3.5 LFO **[DECOMP `FUN_1800249b0`, `FUN_180025a40`]**

- **Período = R tempos**: a fase anda `(1/60)/R·BPM/fs` por amostra; com sincronismo, `fase = frac(posição em
  tempos / R)` (a posição avança `n/fs·BPM/60` por bloco) **[DECOMP: 1/60 em `0x180064358`]**. **Inferência:** o
  método `+0x60` do host devolve o BPM (é o único fator que, com 1/60, dá tempos). O LYNX usa 4R para os índices
  0…8 e valores trocados para 9…11.
- **Formas:** 0 seno `sin(2πφ)`; 1 serra **descendo** `2(1 − φ) − 1`; 2 quadrada `+1` para φ < 0,5 **[DECOMP]**.
- **Quantidade ao quadrado:** `L = sinal(a)·a²·onda` (a = 2p − 1, + mod wheel se MW → LFO), limitado a |a| ≤ 1
  **[DECOMP]**. Destinos: corte `4·L` oitavas; afinação `12·L` semitons; OSCMOD soma `12·L` ao OSC Mod
  **[DECOMP]**.
- **Dados:** 7/10 canais com LFO → FRQ, |a| = 0,07…0,10 → **±0,06…0,13 st no FL**, contra ±0,84…1,24 st no LYNX;
  todos com R = 4 tempos (o LYNX usa 16) **[DADOS + cálculo]**. Riser: LFO → OSCMOD (ignorado pelo LYNX).

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Harmless: cor do timbre | 12 leis de amplitude/fase por harmônica (§1.1), 528 parciais (4 especiais + 524) **[DECOMP]** |
| 1b | Harmless: máscara/LHP/detune | ganho `g·m + 1 − m` com período 12; LHP protege a fundamental e as subs; p39 = espaçamento das parciais `2^(p39/1200)` (não é uníssono) **[DECOMP+DOC]** |
| 1c | Harmless: sub | Sub 1 = parcial em f/2 com `(p2/1024)·A(1)`, fora da máscara **[DECOMP+DOC]** |
| 1d | Harmless: afinação/uníssono | p38 em ct (confirma); ganho `1 − (3^(1−1/n) − 1)/2`; pan `u·p66/128` (confirma); desvio `±|u|·d_k·1,875(41^(p67/256) − 1)` ct (confirma a unidade) **[DECOMP]** |
| 1e | Harmless: envelope | A/D/R confirmados; decay/release = exponencial a −40 dB (×1,5 do LYNX correto); ataque < 0,2 s começa em `(1 − A/0,2)²`; suavizador 30/80 ms **[DECOMP+DOC]** |
| 1f | Harmless: filtro | `fc = 2^(lo + v/8192·(hi − lo))` com margens por tipo/largura (Classic LP padrão: 58,3·2^(8,19v/8192)); **keytracking ref. 128 Hz** (LYNX ignora); envelope não adaptativo **centrado** `(E − 0,5)·x·(hi − lo)`; adaptativo confirmado; tempos `0,12(1001^x − 1)` s com 1000 = nunca sobe / segura; curvas de tensão 1000001 **[DECOMP+DOC]**; Q da ressonância não confirmado |
| 1g | Harmless: LFO | período `64/2^(|v|/1000)` tempos (confirma); global = posição da música; 9 formas; ataque ao quadrado; pitch bipolar em ct (confirma); **corte unipolar** `U·6x` oitavas; p25 = tremolo próprio de 2,5 Hz (não é o LFO); vibrato 6 Hz **[DECOMP+DOC]** |
| 1h | Harmless: porta/volume | `(41^|x| − 1)/26,667` s (confirma), exponencial (p45 > 0) ou linear (p45 < 0); volume `0,75(3^x − 1)/2` (confirma) **[DECOMP]** |
| 2a | Ogun: nível/afinação | `(3^(P0/128) − 1)/2` e `100·P1 + P2` ct confirmados **[DECOMP+DOC]** |
| 2b | Ogun: decay/release | fórmula ms confirmada; **release 10000 = 20 s** (não infinito); queda a −40 dB = inferência **[DECOMP]** |
| 2c | Ogun: uníssono | desvio `±|u|·d_k·5(11^(P15/256) − 1)` ct; pan `u·P13/128`; volume por subvoz §2.4 **[DECOMP]** |
| 2d | Ogun: filtro | **`fc = 44100·(c + c₀)` Hz** (LYNX usa 22050), sem fator 0,9^(ordem−1); HP com a lei de BP/BS (c ≤ 0,6001); Q/largura por r; FLAT `r(u+1)/2` **[DECOMP]** |
| 2e | Ogun: timbre | richness `2^(r+4) − 1` harmônicas (DOC + inferência); espectro das *seeds* não decodificado |
| 3a | MiniSynth: osciladores | 2ª voz em `trunc(24m − 12)` st + 7,5 ct (manual diz ±24; código ±12) **[DECOMP+DOC]** |
| 3b | MiniSynth: envelopes | `0,0002/(K·k⁴)` confirmado; release com 2 mapeamentos (LYNX usa 3) **[DECOMP]** |
| 3c | MiniSynth: LFO | período = R tempos (R = 0,25…16, 64, 128, 256); quantidade `sinal(a)·a²`; serra descendente **[DECOMP]** |
| 3d | MiniSynth: filtro | lei do corte confirmada; Vel → ±7 oitavas (3 canais) ignorada pelo LYNX **[DECOMP+DADOS]** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Harmless (`synthFromHarmless`):**
   - **Keytracking:** `synth.keyTrack = p57/1000` (referência do FL = 128 Hz; a do LYNX é a tecla 48 = 130,8 Hz,
     diferença de 0,03 oitava).
   - **Corte:** usar a faixa com margens: `lo = log₂ 73,42 − max(0, M·eL − 1)`, `hi = log₂ 17000 + max(0, M·eR − 1)`,
     `M = (2/3)·2^(0,004·p56 − 1)`, com (eL, eR) = (1, 0) para os LP, (1, 1) para BP/BS/phasers, (0, 1) para HP,
     (1, 0) para o Fin LP; `fc = 2^(lo + p54/8192·(hi − lo))`.
   - **Envelope de filtro não adaptativo (p47 = 0):** baixar o corte em `0,5·x52·(hi − lo)` oitavas e usar quantidade
     `x52·(hi − lo)`. p49 ≥ 1000 → sustain 1 no envelope de filtro; p48 ≥ 1000 → envelope sem efeito (fica em 0).
   - **LFO de corte:** unipolar — somar `A/2` (A = 6·x58 oitavas) ao corte e usar profundidade `A/2`.
   - **p25:** não mapear para o LFO de volume com o período do LFO principal; se quiser aproximar, LFO de volume
     próprio de **2,5 Hz** (período 0,4 s, sem *sync*) — a profundidade exata não foi confirmada; caso contrário,
     ignorar e registrar no log.
   - **Global:** `lfo.global = p87 != 0`.
   - **Máscara padrão (1,0,1,0…)**: serra nível `1 − m` + quadrada nível `m/2` em toda a faixa (sem troca para
     quadrada pura em m ≥ 0,5); só aplicar essa regra quando os 12 ganhos forem o padrão ímpar.
   - **Detune p39 ≠ 0:** o espaçamento das parciais é `2^(p39/1200)`; com ±1200/±2400 o timbre deixa de ser serra
     (19/139 canais) — registrar no log; a aproximação por forma básica não está coberta pelo código.
   - **Uníssono:** manter pan `±p66/128` e desvio `±S` (S = 1,875(41^(p67/256) − 1) ct); o ganho de compensação do FL
     é `1 − (3^(1−1/n) − 1)/2` por subvoz.
   - Manter: afinação mestre, ataque/decay/release (×1,5 está correto), período do LFO, pitch LFO em ct, portamento,
     curva do volume mestre.
2. **Ogun (`synthFromOgun`):**
   - **Corte:** `fc = 44100·(c + c₀)` Hz com (c, c₀) da tabela do §2.5, limitado a `2,8495/(2π)·fs`; **remover**
     `0,9^(ordem − 1)`; HP com `c = min(0,6001; 0,00039993(1501^u − 1))`, `c₀ = 0,0001`.
   - **Release** P7 = 10000 → 20 s (não 30 s).
   - **Uníssono:** desvio `±5·(11^(P15/256) − 1)` ct em vez de ±10 ct.
   - Manter: ganho `(3^(P0/128) − 1)/2`, afinação, fórmula de tempo do decay (P6 = 10000 = infinito).
3. **MiniSynth (`synthFromMiniSynth`):**
   - **Período do LFO** (tempos) = `{0,25; 0,375; 0,5; 0,75; 1; 2; 4; 8; 16; 64; 128; 256}[int(11p)]`.
   - **Quantidade do LFO:** `4·sinal(a)·a²` oitavas (FLT) e `12·sinal(a)·a²` semitons (FRQ).
   - **Release de amplitude:** `k = 0,95·(0,95p + 0,05) + 0,05` (só dois mapeamentos): trocar
     `tau ((float) (0.95 * k (v[24]) + 0.05))` por `tau ((float) (0.95 * v[24] + 0.05))`.
   - **SAW+SAW / SQR+SAW / SQR+SQR:** 2ª voz `trunc(24m − 12)` semitons + 7,5 ct.
   - **Velocidade → corte:** se o LYNX tiver velocidade no filtro, `14·p11·(vel − 0,5)` oitavas.

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| Harmless `FUN_0077f630` (constantes `0x781068…0x781190`: 101, 1001, 0,05, 0,11995, 0,0249, 0,01, 0,12, 0,11975, 41, 26,667, 12, 65, −0,0125, 0,075, 1,875, 0,75, 6, 1000001, 3, 1/1024, 1/256, 1/128) | leis de todos os parâmetros usados pelo LYNX | DECOMP |
| Harmless `FUN_00614c80`, `FUN_005f6120` (ln 2, ln 11, ln 3, ln10/20, ln2/12, log₂ 440, ln 0,01) | constantes de *runtime* | DECOMP |
| Harmless `FUN_00787990`, `FUN_007873c0` (0,5; 1; 0,25; 1,1; 0,2; 7; 0,29167; 0,75; 3; 0,125; √0,5) | cores do timbre | DECOMP |
| Harmless `FUN_0077b800` (razões 1; 0,5; 1,5; 2,5 + rampa), `FUN_0077b450`/`FUN_0077b2d0`, `FUN_00787b40`, `FUN_00784110`, `FUN_00784120` | parciais especiais, subs, EQ de timbre, LHP | DECOMP |
| Harmless `FUN_00787ce0` (1/1200, 0,5, 1,5), `FUN_00787c30` | *detune* = espaçamento, nº de harmônicas | DECOMP |
| Harmless `FUN_00783f90`, `FUN_00783f00` | máscara com período 12, mix + LFO unipolar | DECOMP |
| Harmless *note-on* (`h_range.c` l. 3940–4075; tabela `0x7ee5c8`; 2, 1/128, 3, 1,125, 0,2·fs) | uníssono (pan, desvio, ganho, fase, variação), ataque | DECOMP |
| Harmless `FUN_005f5f60`, `FUN_005f5f30`, `FUN_005f5fe0`, voz l. 5700–5770, `FUN_0077f170` (0,03; 0,01; 0,1; 0,2; 0,08) | envelope: exponencial a 1 %, suavizadores | DECOMP |
| Harmless `FUN_0077c170` (682,67; 1; 0,5), construtor (17 000, 440, −31), `FUN_0077ca30` (−72, 2048…), `FUN_0077c940`/`FUN_0077c970`, `FUN_0077c540` | faixa, margens e formas do filtro | DECOMP |
| Harmless voz `FUN_00784a10` l. 330–380 (−7,0 em `+0x6F8`, 0,5) | keytracking, envelope centrado/adaptativo, LFO de corte unipolar | DECOMP |
| Harmless voz l. 120–270 (2π, 0,5, 2, 3, 0,25, 4, 7, 0,2, 1/3600), `FUN_0077f0c0` (3840), `FUN_0077f5e0`, `FUN_007793e0` (evento 0xD) | formas, taxa e sincronismo do LFO, pitch em ct | DECOMP |
| Harmless `FUN_0077f170` (2,5/fs, 6/fs, 3/fs), `FUN_00604a90` | tremolo estéreo e vibrato com osciladores próprios | DECOMP |
| Harmless `FUN_00784630` (0,0 como float, 1250, 1200, −1) | forma do portamento | DECOMP |
| Harmless `FUN_00612f00` (1/fs, 44100/fs, 2π) | campos do objeto de taxa | DECOMP |
| `objdump` Harmless `0x409fe0` (`cvtsd2si`) | arredondamento ao mais próximo | DECOMP |
| Ogun `FUN_008684e0`, `FUN_0086a570` (1/128, ln 3, 1/1000, 0,0001) | nível, afinação, armazenamento de decay/release | DECOMP |
| Ogun `FUN_0086a8e0` (21, 997,5, 50, 0,001, 11, 5, ln2/1200, 0,3, 2, 1/√n, tabela `0x8e7b00`) | tempos do timbre, uníssono | DECOMP |
| Ogun `FUN_0086c050` (mesma fórmula, sem infinito) | release | DECOMP |
| Ogun `FUN_00842ee0`, `FUN_00843680`, `FUN_008447d0`, `FUN_00605d70` (ln 0,01) | uso dos tempos no motor de timbre | DECOMP (parcial) |
| Ogun `FUN_00867cc0` (1501, 0,000665, 0,00039993, 0,003, 0,0001, 1,0001, 0,6001, 2π, 2,8495, 2001, 0,02495, 0,1, 50, 101, 0,1199, 0,01, 12) | filtro por tipo, Q, ordem | DECOMP |
| Ogun *note-on* (`+0xF2C` tipo, `+0xF30` ordem − 1, `+0xF34` 1/ordem, `+0xF3C` = `+0x20` do objeto de taxa, ×0,5 com `+0x59C`) | escala `s` do filtro | DECOMP |
| MiniSynth `FUN_180023c20` (24, 0,5, 0,9, 0,01, 9, 0,99, 11, tabela de R, 0,95, 0,05, 10) | parâmetros | DECOMP |
| MiniSynth `FUN_1800249b0`, `FUN_180025a40` (1/60, 2π, 0,5) | LFO: período, formas, quantidade ao quadrado | DECOMP |
| MiniSynth `FUN_180021d10` + `objdump` (12, 24, 1/12, 0,00625) | razão da 2ª voz | DECOMP |
| MiniSynth `FUN_180022250` (220, 57, 1/12, 12, 0,01) | afinação, deriva, glide | DECOMP |
| MiniSynth `FUN_1800213e0` (4, 10,3, 48, 14, 0,103, 20000) | corte | DECOMP |
| MiniSynth `FUN_1800229c0`, `FUN_180022440` (0,0002, 500, 50, 0,95, 0,05) | envelopes | DECOMP |
| 139 Harmless, 20 Ogun, 10 MiniSynth de `Demo projects` (`tmp/r21/py/hl.py`, `og.py`, `ms.py`) | distribuições citadas | DADOS |
| Manual Harmless | cor, máscara, subs, pitch/detune, ataque, filtro (alt/adaptive), alvos do LFO, legato | DOC `…/plugins/Harmless.htm` |
| Manual Ogun | faixas de nível/afinação; DEC/REL com mapas, richness, envelope desligado por padrão | DOC `…/plugins/Ogun_master.htm`, `…/plugins/Ogun_synthesis.htm`, `…/plugins/Ogun.htm` |
| Manual MiniSynth | modos de voz, slide automático, ±24 st (divergente do código), LFO sincronizado | DOC `…/plugins/MiniSynth.htm` |

### Questões em aberto

- Harmless: forma da cor *Smooth saw* do LFO (tabela) e da curva dos tipos de filtro além do Classic LP; largura do
  pico de ressonância (p59/p61/p62) e uma tradução para Q; efeito do *pluck* (p31, 53/139 canais com valor < 1000;
  `param+0x600` = `0,11975(1001^x − 1)`, `FUN_00784860` não decodificada); forma do tremolo estéreo (p25) e sua
  profundidade; condição do "salto de oitava" no portamento; valor devolvido pelo host para velocidade → release
  (p30); o nível absoluto do Harmless frente aos outros geradores; o efeito audível do *detune* p39 numa
  aproximação por formas básicas.
- Ogun: normalização das taxas de queda por quadro no motor FFT (confirmar o −40 dB); mapas *Decay/Release EQ* e
  articuladores dos 20 canais (volume, ataque real); lei do espectro "full" e das *seeds*; lei de pan de `0x8f9660`;
  confirmar no próprio Ogun que o campo `+0x20` do objeto de taxa vale `44100/fs` (lido no Harmless, mesma
  biblioteca).
- MiniSynth: se o glide existe em modo poly; forma do envelope de filtro (rodada 10, não reverificada); estrutura
  exata do filtro (a rotina não parece uma *ladder* simples de 4 polos — matriz de estados); dependência das taxas
  com `fs`; o termo constante 0,07 somado à mistura do oscilador em `FUN_180022440`.
