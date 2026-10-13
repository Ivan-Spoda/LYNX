# Análise comportamental do FL Studio 2026: rodada 19 (REA) — o que a rodada 18 deixou em aberto (Effector tipos 8…11, Distort e Reverb; Harmor; Kepler; Toxic Biohazard; Sakura; Pitcher/Vocoder; Love Philter)

> **Natureza deste documento.** Continuação de `REA_rodada18.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são fórmulas e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> (ex.: "esta rotina é `exp` porque é sempre chamada logo após multiplicar por ln 2") estão explicitadas no texto.

- **Alvos** (todos já conferidos por hash nas rodadas anteriores, salvo o Fruity Vocoder):

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave novas |
  |---|---|---|---|
  | Effector | `engine_x64.dll` `4bfdc0f6…7dc2071a` (= rodada 18) | motor C++ | Trancer `FUN_180019f90`, Stutter `FUN_18001ae50`, Vowel `FUN_18001b140`, Ring `FUN_18001b5a0`, biquad `FUN_180014520`/`FUN_180014680`, Distort `FUN_180019860`, Reverb `FUN_18001a130` + `fxReverb` slot 43 `FUN_18001e9f0`, criação de *buffers* `FUN_180018700`, *reset* `FUN_180018a50`, fase sincronizada `FUN_180016d40` |
  | Harmor | `23673ad8…499d5aba` (= rodada 11/18) | Delphi | `TMEnvelope` (VMT `0x79a0d8`) `FUN_007a0c60`/`FUN_0079fef0`, centro `FUN_0079a7c0`/`FUN_0079a7f0`, `TArticulator` método 0 `FUN_007a4880`, conjunto de flags `FUN_00896fe0` (`0x89732c`), *note-off* `FUN_00885390`, morte de voz `FUN_008855e0`/`FUN_00883cd0`, *declick* `FUN_008635c0`/`FUN_008781f0`, ressonância em `FUN_00886eb0`/`FUN_00882af0`/`FUN_00882820` |
  | Kepler (motor) | `baf37a1d…2afc63ab` (= rodada 13/17/18) | C++ | construtor `FUN_1800166d0`, fontes `FUN_1800225f0`, LFO `FUN_18001b940`, tabela `FUN_180013d30`, *setter* `FUN_180019a20`, reinício `FUN_180018fe0`, chorus `FUN_180001a60`/`FUN_180001770` |
  | Toxic Biohazard | `5c326637…f136cb46` (= rodada 15/17/18) | C++ | *setter* `FUN_180040080`, suavização `FUN_180040ff0`/`FUN_1800406d0`, alvos `FUN_1800395b0`, render `FUN_180038d00`, LFO1 da voz `FUN_180039880` |
  | Sakura (motor) | `e632487c…ae0f1bab` (= rodada 17/18) | C++ | `FUN_18002f4c0`, `FUN_18002da50`, render `FUN_180031960` |
  | Pitcher | `965db573…a73d` (= rodada 14/15) | C++ | `setParam` `FUN_18001cab0`, processamento `FUN_18001e030`, alvo `FUN_18001ea80`, harmonia `FUN_18001f800`, máscara `FUN_18001d9c0`, render `FUN_18001ed60`/`FUN_18001f2b0` |
  | Fruity Vocoder | `bfc3bec3…a63961b6` | Delphi | só DFM + dados |
  | Fruity Love Philter | `b9f11d63…ba2ff547` (= rodada 16/17/18) | Delphi | coeficientes `FUN_00859100`, estágios `FUN_0085d5a0`, biquad RBJ `FUN_006249d0`, Mango `FUN_006261c0`/`FUN_00625ea0`, render `FUN_0085aa10` |

- **Ferramentas:** Ghidra 12.1.4 *headless* (projetos das rodadas anteriores abertos só para leitura:
  `/tmp/r18gh_EffEng`, `/tmp/r11gh_copy`, `/tmp/r13gh_kepeng_x64.dll`, `/tmp/r15gh_Toxic.dll`,
  `/tmp/r17gh_Sakura_eng`, `/tmp/r14gh_Pitcher_x64.dll`, `/tmp/r17gh_LP`); scripts em `tmp/r18/gh/scripts/` +
  `tmp/r19/gh/scripts/ScalarIns.java`; `tmp/r18/k.py` para constantes; `tmp/r19/scr/hmenv.py` e
  `tmp/r19/py/pt2.py`/`vc.py` para os dados; DFM com `tmp/r15/dfmc.py`. Manual oficial lido via web.
- **Data:** 2026-10-13.

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

## 1. Effector — tipos 8…11, curva do Distort e seco do Reverb **[DECOMP + DOC]**

**O que o LYNX supõe** (`FlEffects.cpp` `effector`): Trancer/Stutter/Vowel/Ring viram efeitos internos com X/Y
"aproximados"; Distort = saturação genérica; Reverb com mistura *crossfade*.

### 1.0 Estrutura comum (complemento da rodada 18) **[DECOMP]**

- O processador (`FUN_18001b970` → `FUN_18001ba60`) chama **as 12 unidades em série, a cada bloco**, sobre o mesmo
  *buffer*; cada unidade tem a flag `+0x28` = "(índice ≠ tipo escolhido) OU bypass" (`FUN_18001bb30`). Só a unidade
  do tipo escolhido processa; as outras passam o sinal intacto (Vowel/Ring/Reverb ainda fazem uma rampa de
  entrada/saída de coeficiente 0,01/amostra no campo `+0x2c`; Stutter, Vowel, Ring e Reverb se reiniciam,
  `FUN_180018a50`, ao terminar de desativar).
- Campos da unidade lidos aqui: `+0x8` taxa de amostragem; `+0xC` = X e `+0x10` = Y (já limitados a 0…1 pelos
  *setters* `FUN_180018bb0`/`FUN_180018bd0`); `+0x1C`/`+0x20` = Mod Amount X/Y; `+0x14` = valor do Mod Rate
  (× `(1 − TempoReduce)²`, `FUN_18001ba60`); `+0x38` = incremento de fase por amostra = **`1/(T·fs)`** com
  **`T = valor·60/BPM` s** (`FUN_180018c20`); `+0xADC/+0xAE0` = Wet Mix e sua versão suavizada (coef. 0,01).
- A fase `+0x30` do LFO é ressincronizada com a posição da música (`FUN_180016d40`: fase = frac(posição/valor),
  chamada por `FUN_180017010` e pelo `setParameter` do Mod Rate) **[DECOMP; que o argumento seja a posição em
  tempos é inferência pelo uso]**.

### 1.1 Trancer (tipo 8; manual "Trans") — `FUN_180019f90`

- **Não usa o LFO nem Mod Amount**: lê X e Y crus. Usa a fase `+0x30` (a mesma sincronizada à música) com período
  `T` do Mod Rate.
- Por período (fase φ de 0 a 1):
  - φ < 0,01: `e = 100·φ` (subida linear em 1 % do período);
  - φ ≥ 0,01: `u = (φ − 0,01)/0,99`, **`e = 1 − min(1, u⁴·K)`** com **`K = 50·(1 − X)² + 1`**.
  - `e` passa por um polo de coeficiente 0,01 por amostra (≈ 100 amostras); o ganho aplicado aos dois canais é
    **`g = 1 − e·(1 − Y²)`**.
- Leitura: a cada período o ganho cai para **`Y²`** (em ~1 % do período) e volta a 1 seguindo `u⁴`; a volta
  completa termina em `φ = 0,01 + 0,99·K^(−1/4)`: **X = 0 → 38 % do período, X = 0,5 → 53 %, X = 1 → 100 %**.
  Y = 0 → silêncio total no vale; Y = 1 → efeito nulo.
- **O Wet Mix não entra na saída** (o valor suavizado é atualizado mas não usado): a saída é `entrada × g`.
- Manual: "X - Release time. Y - Drop level" **[DOC `…/plugins/Effector.htm`]** — coerente.

### 1.2 Stutter (tipo 9; manual "Grain") — `FUN_18001ae50`

- Cria (`FUN_180018700`) dois *buffers* de **1,5 s** (`1,5·fs` amostras, L e R).
- **Gravação:** desde o último *reset* da unidade, as amostras de entrada são escritas em sequência até encher o
  *buffer* (1,5 s); depois disso a gravação para (o contador só volta a 0 no *reset* `FUN_180018a50`, chamado na
  troca de taxa/inicialização `FUN_18001b910`←`FUN_1800179a0`, e quando a unidade é desativada).
- **Reprodução:** uma posição avança 1 amostra por amostra e **volta a 0 a cada período `T` do Mod Rate** (fase
  própria `+0xAE4`, não sincronizada com a música) ou ao atingir 1,5 s. Se a posição já foi gravada, a saída é o
  conteúdo do *buffer* naquela posição; nas primeiras **192 amostras** de cada repetição há um *crossfade* linear
  com a continuação do segmento anterior. Se ainda não foi gravada (primeiro período após o *reset*), passa a
  entrada.
- Efeito resultante (inferência direta do fluxo acima): **repete, a cada período `T`, o trecho de áudio que começa
  no instante do *reset*/ativação** (até 1,5 s).
- Há um ramo de leitura reversa (flag `+0xB10`), mas ele depende do byte `+0` da unidade, que **só é escrito com 0**
  no código lido (construtor/*reset*) → reversão **inativa na prática / não confirmada**.
- **X e Y não são lidos** pela unidade; nem o LFO. Mistura: **`(1 − w)·seco + w·repetição`** (w = Wet Mix
  suavizado).
- Manual: "GRAIN … X & Y - Not used" **[DOC `…/plugins/Effector.htm`]** — coerente.

### 1.3 Vowel (tipo 10; manual "Vox") — `FUN_18001b140`

- `X' = clamp(X + LFO·ModX, 0, 1)`, `Y' = clamp(Y + LFO·ModY, 0, 1)` (LFO avaliado uma vez por bloco).
- **Banco de 8 passa-faixas biquad por canal, em paralelo** (`FUN_180014520` coeficientes / `FUN_180014680`
  processamento), todos com **Q = 12**, na forma "ganho de pico = Q" (b0 = sen ω/2, normalizado por 1 + α,
  α = sen ω/(2Q); a atribuição seno/cosseno é inferida pela forma das fórmulas).
- **Três conjuntos de frequências** (constantes `0x1800586a0/b0/c0`; os 4 últimos valores são comuns,
  `0x1800586d0`):

  | Conjunto | Bandas 1–4 (Hz) | Bandas 5–8 (Hz) |
  |---|---|---|
  | A | 250, 700, 2500, 3500 | 4200, 8500, 10000, 12000 |
  | B | 700, 1250, 2500, 3500 | idem |
  | C | 250, 2500, 2700, 3500 | idem |

- **X escolhe a vogal:** `p = 3·X'`, `n = ⌊p⌋`, `f = p − n`; frequências = interpolação linear
  **A→B (n = 0), B→C (n = 1), C→A (n = 2), A (X' = 1)** — ciclo fechado.
- **Y escala todas as frequências** ("throat size"): fator **`2^(2·0,93·Y' − 1)`** (Y = 0 → ×0,5; 0,5 → ×0,953;
  1 → ×1,815).
- Soma das 8 bandas × `(1 − 0,125·n)` (n = 0, 1, 2 → 1; 0,875; 0,75).
- Mistura: **`(1 − w)·seco + w·molhado`**, com `w` = Wet Mix suavizado × rampa de ativação.
- Nota numérica (derivada das fórmulas): com ganho de pico 12 por banda, o molhado pode ficar bem acima do nível
  de entrada perto dos formantes; nenhuma normalização adicional foi encontrada na unidade.
- Manual: "X - Formant filter, vowel sound (A,E,I,O,U). Y - Throat size" **[DOC]**. **Divergência:** o código tem
  **3** conjuntos de formantes (não 5 vogais). Os nomes das vogais de cada conjunto **não estão no código**
  (não confirmado).

### 1.4 Ring (tipo 11) — `FUN_18001b5a0`

- `X' = clamp(X + LFO·ModX, 0, 1)`; **frequência do portador `f = 2^(8,7·X' + 5)` Hz** (X = 0 → 32 Hz;
  0,5 → 653 Hz; 1 → 13 308 Hz), calculada uma vez por bloco.
- Portador **senoidal** (fase acumulada `+0x2D8` com incremento `f/fs`; seno de `2π·fase`, `FUN_180040dc0`).
- Saída: **`(1 − w)·x + w·x·sen(2π·fase)`** (mesmo `w` do Vowel). **Y não é lido.**
- Manual: "X - Modulator frequency Y - Not used" **[DOC]** — coerente.

### 1.5 Distort — curva de saturação exata (`FUN_180019860`)

- `d = clamp(0,95·X + 0,05, 0, 1)`, **`k = 2048·d²`** (X = 0 → 5,12; 1 → 2048). X e Y são lidos crus (sem LFO).
- Pré-filtro: polo único `lp ← lp + c·(x − lp)` com `c = (0,9·Y + 0,1)⁴`.
- **Cascata de 4 estágios racionais** aplicada a `lp` (s₀ = lp):
  1. `s₁ = s₀ / (1 + k·|s₀|)`
  2. `s₂ = s₁ / (1 + k·s₁²)`
  3. `s₃ = s₂ / (1 + k·|s₂|³)`
  4. `s₄ = s₃ / (1 + k·s₃⁴)`
- **Ganho de compensação `G = 0,5 / F(0,5)`**, onde `F` é a cascata acima (calculado por bloco), de modo que uma
  entrada de 0,5 sai 0,5.
- Molhado = **`G·F(lp) + (x − lp)`**; saída = `(1 − w)·x + w·molhado`.
- Consequência numérica (calculada a partir das fórmulas): a parte grave satura em torno de **±0,5**; X = 0 → G ≈
  3,97 e `G·F` = 0,157 / 0,256 / 0,56 para x = 0,05 / 0,1 / 1; X = 0,5 → G ≈ 284 e `G·F` ≈ 0,485 já em x = 0,05
  (quase onda quadrada de ±0,5).

### 1.6 Reverb — o seco é preservado (`FUN_18001a130` + `fxReverb` slot 43 `FUN_18001e9f0`)

- A unidade copia a entrada para *buffers* temporários multiplicados por **`w`** (Wet Mix suavizado × rampa de
  ativação) e chama o processamento do `fxReverb` (VMT `0x180057fc0`, slot 43 = `+0x158`) com **entrada = cópias
  escaladas e saída = os *buffers* originais**.
- O processamento **soma** na saída (`saída += amostra_reverb`), onde a amostra do reverb inclui
  `entrada·DryGain` (campo `+0x274`, posto em 0 pela unidade) + cauda.
- Logo **o sinal seco original permanece com ganho 1** e o reverb é somado: **`saída = x + Reverb(w·x)`**.
  Confirma a pergunta da rodada 18 (seco preservado; Wet Mix controla só o envio ao reverb).

---

## 2. Harmor — faixa de E, ressonância e nível após o *note-off*

**O que o LYNX supõe** (`FlSytrus.cpp` `synthFromHarmor`): envelope do filtro centrado no nível 0,5 com ±1000 =
±7,9 oitavas; ressonância não modelada com a lei do FL; envelope de volume desligado = portão `A 2 ms / S 1 / R 0,1 s`.

### 2.1 Faixa do nível E do envelope no corte por voz **[DECOMP + DADOS]**

- **Valor do envelope.** A classe é `TMEnvelope` (VMT `0x79a0d8`). O método 11 (`FUN_007a0c60`) interpola entre
  os pontos. O valor de cada ponto (`FUN_0079fef0`) é o `y` do ponto (+ deslocamento de tensão/modulação)
  **menos o campo `+0x18` do envelope** (*double*). Com modulação por ponto, o valor é limitado a
  `[+0x70, +0x78]`, que o construtor (`FUN_0079a650`) põe em **[0; 1]**. O resultado vai para `+0x34` do registro
  do articulador da voz — o mesmo campo que `FUN_00886eb0` lê como E.
- **`+0x18` é o "centro".** O método 0 de `TArticulator` (`FUN_007a4880`) grava o modo de combinação (`+8`) com
  uma *flag* e chama `FUN_0079a7c0(envelope, flag)`, que grava **`+0x18 = flag × 0,5`** (constante `0x79a7e8`,
  via `FUN_0079a7f0`).
- **Quem recebe a *flag*** (`FUN_00896fe0`, conjunto de bits `0x89732c`): bits {0, 2, **4, 5**, 12–15, 17, 18,
  23, 24, 27–30, 33, 36, 38–42}. Logo os articuladores **4/5 ("Filter 1/2 frequency") ficam em modo 1 (aditivo)
  com centro 0,5**; o 1 (Volume) e os 8/9 (ressonância) ficam em modo 0 (multiplicativo) com centro 0.
- **Dados:** em 682 partes Harmor dos demos, os `y` dos pontos dos envelopes dos articuladores 1, 4, 5 e 8 estão
  sempre entre 0,0 e 1,0 **[DADOS]**.
- **Conclusão: no corte, E = y − 0,5 ∈ [−0,5; +0,5]** (centrado). O termo da quantidade da rodada 18 fica
  `2·(hi − lo)·(p56/1000)·(y − 0,5)·m` = **±(hi − lo)·amt ≈ ±7,92 oct·amt** → **a suposição do LYNX (centro 0,5,
  ±1000 = ±7,9 oct) está confirmada para esse termo** (corrige a leitura "15,8 oct/unidade" da rodada 18, que
  não considerava o centro).
- **Termo extra da saída do articulador.** No início da voz a profundidade do envelope no registro (`+0x118`) é
  posta em 1 (modos 0/1) ou 2 (modo 2) (`FUN_00897c70`), e o avaliador (`FUN_00897d90`, modo 1) soma `E·1` à
  saída do articulador 4/5. Como a rodada 18 leu a saída do articulador com fator `2·(hi − lo)`, **inferência a
  partir do código:** com o envelope ligado o corte também se move em `2·(hi − lo)·E` (±7,92 oct) mesmo com
  p56 = 0. O fator `2·(hi − lo)` desse termo não foi relido nesta rodada (**não confirmado** de novo).

### 2.2 Ressonância p57/p69 **[DECOMP + DFM]**

- Nomes (DFM): 57/69 "Filter 1/2 resonance / noise amount"; 58/70 escala oct/Hz; 59/71 "resonance width / noise
  length"; 60/72 "resonance offset" (±2400); 61/73 "adaptive width / free noise". Articuladores: 8/9 "res
  amount", 10/11 "res width", 12/13 "res offset".
- `ProcessParam` **não tem caso** para 57/69; o valor é lido no render da voz (`FUN_00886eb0`):
  **`r = (p57/1000)·A₈`** (A₈ = saída do articulador 8/9, base 1, multiplicativo); só atua se `r > 0` e o filtro
  estiver ligado.
- **Caminho normal** (inteiro `+0x430` do filtro ≠ 0x6D): a curva "Resonance shape 1/2" (tabela `+0x428`,
  preparada por `FUN_008744c0` conforme p58/p61) é amostrada por harmônica na posição
  `c + (x − L − p60/1200 − 8·A₁₂)·w_r` (x = log2 da frequência da harmônica; L = corte da voz em log2 Hz;
  **p60 em centésimos**; A₁₂ aditivo = **8 oitavas por unidade**); largura `w_r = k₀·2^(1 − 0,004·p59·A₁₀)`
  (`k₀` = campo `+0x49C`, **não identificado**; com p61 ligado, `k₀` é dividido por `+0x1B8` da voz; na escala
  "Hz" de p58 o deslocamento vira fator `2^(p60/1200 + 8·A₁₂)` na frequência).
- **Ganho:** os valores amostrados `s` são multiplicados por **`r·ln 2`** (`FUN_00882af0`) e passam pela rotina
  do ponteiro `+0x1150`; o resultado multiplica as amplitudes das harmônicas (`FUN_00882820`). **Inferência:**
  `+0x1150` é um `exp` vetorial (sempre chamado após ×ln 2; o ponteiro é atribuído em `FUN_00878f60` e não foi
  conferido) → ganho de ressonância **`2^(r·s(x))`**.
- Caminho "noise" (`+0x430 = 0x6D`): gerador pseudoaleatório com taxa ligada a r — **não decodificado**. Pico em
  dB para r = 1 (depende da curva padrão da "Resonance shape", não tabelada) — **não confirmado**.

### 2.3 Nível depois do *note-off* com o envelope de volume desligado **[DECOMP]**

- **Note-off** (`FUN_00885390`): estado da voz (`+0x214`) = 6; envelopes/LFOs ativos entram em *release*
  (`FUN_007a09f0`); a marca de tempo da voz (`+0x40`) é deslocada de −2⁶³ (vira "a mais antiga").
- **Render** (`FUN_0088ab10`): a voz só se encerra sozinha (`FUN_008856b0` → estado 9) se o envelope de volume
  estiver **ligado e terminado**. Desligado, o articulador de volume não muda → **o nível fica constante após o
  *note-off*; não há rampa nem *release* implícito.**
- **A voz só termina se for morta** (`FUN_008855e0`, estado 9), chamada no disparo de nota (`FUN_00883cd0`) em
  dois casos: **roubo de voz** ao atingir a polifonia (`param_1[0x1c]`; as soltas saem primeiro pela marca
  −2⁶³) e **redisparo da mesma nota** (conforme o modo em `+0x10d4`).
- **No estado 9 há *declick***: o objeto `+0x660` da parte (`FUN_008635c0`) aplica uma rampa de saída
  configurada por `FUN_008781f0` → `FUN_00863450`/`FUN_00862d70` como **tipo 3, 10 ms** (`round(0,01·fs)`
  amostras; tabela com π e 0,5 — forma de cosseno **por inferência**); depois a voz vai ao estado 10 (livre).
- Morte de vozes pelo host (Voice_Kill) — **não rastreada**.

---

## 3. Kepler — amplitude e taxa do LFO1, mistura do chorus **[DECOMP + DADOS]**

### 3.1 Valor L₁ e sua amplitude

- Os ponteiros `voz+0x11B0`/`+0x11B8` são preenchidos no construtor global `FUN_1800166d0` (laço das 8 vozes):
  `+0x11B0` → **global `+0x1C724`**, `+0x11B8` → global `+0x1C720` (a tabela de fontes `FUN_1800225f0` dá os
  mesmos endereços: fonte 0x10 e 0xF).
- Na voz (`FUN_180023ab0`) o valor usado no tom (× `+0x11E8`) e no corte (× `+0x11E0`) é **`2·L₁`**, L₁ =
  `*(+0x1C724)`.
- O render global (`FUN_18001b940`) calcula por amostra **`+0x1C724 = R·G·0,5·W(φ)`**, com `W` = tabela de
  **2 170 pontos** interpolada (índice `φ·2170`; fase `+0x1C6C0` com incremento `+0x1C688`, volta em 1). A tabela
  é montada em `FUN_180013d30` a partir de 2 170 `int16` em `0x180252ED0` e **normalizada para exatamente −1…+1**.
  Forma **[DADOS, lida no PE]**: quase senoidal (fundamental 1,006; 2ª harmônica 0,031; 3ª 0,013).
- **Logo `2·L₁ = R·G·W(φ)` ∈ [−1; +1]**: as profundidades A(x) e A_c(x) da rodada 18 são o **pico** do desvio,
  simétrico (±A oitavas no tom, ±A_c oitavas no corte).
- Outras saídas do mesmo LFO (só para a matriz; o caminho fixo do painel não as usa): `+0x1C6A0` triângulo em
  [−0,5; 0,5]; `+0x1C698` = triângulo + 0,5; `+0x1C71C`/`+0x1C714` = essas × R·G.

### 3.2 Atraso, *gate* e taxa

- `R` (`+0x1388`) é uma rampa linear (soma `+0x1390` por amostra, limitada a 1). O parâmetro interno 0x32 (LFO1
  Delay, `FUN_180019a20`) dá `+0x1390 = 1/(D·fs)`, com D da tabela de 11 pontos em `0x180254100` (segundos —
  inferência, pois divide por fs): **{0; 0,229; 0,447; 0,547; 0,833; 1,0; 2,074; 4,775; 5,612; 6,774; 6,706}**,
  interpolada em 10·x. A rampa volta a 0 no *note-on* que leva o contador de notas a 1, se o Trig Mode (0x33)
  estiver desligado (`FUN_180018fe0`); se "LFO1 Retrig Phase Active" ≥ 0,5, a fase vai para "Phase".
- `G` (`+0x1C708`) vale 1, ou 0 sob certas condições do Trig Mode (0x33 e caso 0x51) — condição exata **não
  confirmada**.
- **Taxa:** permanece a lei da rodada 17 (`f = 21,739·2^(−T(x))` Hz, 0,125…21,7 Hz); nesta rodada só se
  confirmou que a fase avança por amostra com esse incremento. A lei do BPM Sync (interno 0x31) **não foi lida**.

### 3.3 Mistura do chorus (`FUN_180001a60`, objeto em global `+0x1BBB0`, construtor `FUN_180001770`)

- **Entrada em mono:** o byte 0 do objeto vale 1 no construtor (nenhum outro escritor encontrado) → **L e R (o seco
  inclusive) são substituídos por `(L + R)/2`** antes do chorus.
- **Saída: `out = molhado·√m·g + seco·√(1 − m)`**, com `m` (`+0x208`) = **0,5** (construtor e a cada troca de modo
  em `FUN_18001b940`), `g` (`+0x204`) = 1, suavização 0,01/amostra → **seco e molhado a 0,7071 (−3 dB) cada**.
- Realimentação do atraso (`+0x178` do objeto) = **0** em todos os modos. "Chorus Noise" (0x46): ruído filtrado
  somado ao molhado com nível `0,2·x³` (só se > 0); "Chorus Noise Gate" (0x47) booleano (x ≥ 0,5). Ganho final
  `+0x200` = 1.
- Troca de modo com *crossfade* (`+0x1BDF0`); com o modo Off o chorus não é chamado e o estéreo passa intacto.

---

## 4. Toxic Biohazard — modo do filtro e escalas em Hz **[DECOMP + DADOS + DOC]**

### 4.1 Seleção do modo

- **Parâmetro de índice 300** (faixa interna 294…339 do estado; *float* 300 do bloco que começa no offset 0x204C):
  no *setter* `FUN_180040080`, `int(v)` (valor cru), limitado a ≤ 3, gravado em `+0x3B78`. Dados: p300 = 1 no
  `.fst` padrão e em "Saw #2"; 0 em "Bombs Away" e "Saw" **[DADOS: 7 blobs]**.
- Saída do filtro (`FUN_1800395b0` define alvos; `FUN_180038d00` renderiza): 4 estágios
  `s_i = (x_i + x_i,ant)·a − b·s_i,ant`, entrada `u` = saturação de `(entrada − res·k·s4)`; saída
  `(c0·u + c1·s3 + c2·s4)·(1 − β) + β·entrada`, pesos seguidos com coeficiente 0,005/amostra.

  | p300 | c0 | c1 | c2 | β | Modo |
  |---|---|---|---|---|---|
  | 0 (ou outro) | 0 | 0 | 0 | 1 | **Bypass** (com β ≥ 0,999999 o filtro nem é calculado) |
  | 1 | 0 | 0 | 1 | ≈0 (1e−7) | **LP** = s4 (4 polos) |
  | 2 | 0 | 1 | −1 | ≈0 | **BP** = s3 − s4 |
  | 3 | 1 | 0 | −1 | ≈0 | **HP** = u − s4 |

  Ordem coerente com o manual "Bypass/LP/BP/HP" **[DOC `…/plugins/ToxicBio_filter.htm`]**.

### 4.2 Escalas (`FUN_180040080`; cópia/suavização `FUN_180040ff0`/`FUN_1800406d0`)

- 15 Cutoff → x; 16 Res → x (0…1 por voz); 17 KeyTrk → x; 18 VelTrk → x; 19/20/22 EG A/D/R →
  `FUN_18003d480(x)` (não lido); 21 Sus → x²; **23 EG Amnt → `sinal(2x − 1)·(2x − 1)²`** (−1…+1; 0,5 = 0);
  **24 Lfo1 Amnt → x³**.
- **Correção da rodada 18:** o termo "EGAmt·env" de `FUN_1800395b0` é na verdade **`Lfo1Amnt·LFO1`** (`+0x3BE4` =
  parâmetro 24; `voz+0x220` = valor do LFO1, escrito por bloco em `FUN_180039880`). O **envelope do filtro é
  aplicado por amostra no render**: `índice = u·65535 (em rampa no bloco) + env·EGAmt·65535`, recortado a 0…65535.
- Envelope (`voz+0x70`) **0…1**: ataque alvo 5,0 com coeficiente `8,82/(39,44·T_A·fs)`, limitado a 1; decay para
  S com `8,82/(T_D·fs)`; release para 0 com `8,82/(T_R·fs)`.

### 4.3 Em Hz (com `fc = 2^(9u + 5)`, u limitado a 0…1 → 32 Hz…16 384 Hz; 1 unidade de u = 9 oitavas)

- **EG:** `+9·EGAmt·env` oitavas (máx. ±9 oct; ex.: x = 0,735 → EGAmt = 0,221 → +1,99 oct com env = 1).
- **Key-track:** `KT·(nota − 60)·0,01·9` = **0,09·KT oitavas por semitom** (KT = 1 → 1,08 oct/oct), referência na
  nota 60 (que `FUN_180026a80` devolva a nota MIDI é inferência).
- **Velocidade:** `VelTrk·(vel − 1)·0,5·9` oitavas (até −4,5·VelTrk); a faixa de `vel` **não confirmada**.
- **LFO1:** `9·x³·LFO1` oitavas; amplitude do LFO1 (`FUN_180030110`) **não confirmada**.

---

## 5. Sakura — Hi-Cut em Hz **[DECOMP; conversão em Hz por dedução matemática]**

- **Topologia** (render `FUN_180031960`, 4 vozes SIMD; coeficientes em `FUN_18002da50`): **4 passa-baixas de
  1 polo em cascata** com realimentação negativa da saída saturada: 1º estágio `y1 += (r·ŷ4,ant + x − y1)·c`; demais
  `yi += (y(i−1) − yi)·c`; `ŷ4 = y4/(1 + D·y4²)` com **D = 1000·Drive⁴** (parâmetro 33 → `+0xFC`); `x` =
  excitação (ruído filtrado por um biquad); **`r = −10·Res·(1 − g)`, limitado a ≥ −4**.
- **Coeficiente: `c = g·E_hc`**, `g = clamp((Cutoff + 0,5·mod)², 0, 1)` (rodada 18), `E_hc` = envelope do Hi-Cut
  (`voz+0x178`): ataque alvo 1 com taxa `0,0002/T`, `T = (A + mod)²·0,5` (taxa 1 se T < 0,001); decay para `S²`
  com `0,0002/(3D³ + 0,001)`; após o *note-off* para **0** com taxa **0,002/amostra**. Sem envelope (E = 1) e sem
  modulação, **c = Cutoff²**.
- **Em Hz** (dedução do polo `1 − c`, não constante do código): por estágio **`f = −fs·ln(1 − c)/(2π)`** (c = 0,5
  → 0,110·fs ≈ 5,3 kHz a 48 k; c = 0,1 → 0,0168·fs ≈ 805 Hz; c = 0,01 → ≈ 77 Hz; c = 1 → sem filtragem). A cascata
  de 4 estágios iguais (sem ressonância, aproximação analógica para c pequeno) tem −3 dB em **≈ 0,435·f**.
- O coeficiente não depende de fs → o Hz escala com a taxa em que o motor roda; **se é a taxa do host ou 48 kHz
  fixos não foi confirmado**.

---

## 6. Pitcher — parâmetros pendentes e aproximação estática **[DECOMP + DOC + DADOS]**

Complementa `REA_rodada14.md` §3 e `REA_rodada15.md` §1. **Layout no `.flp` (inalterado):** `"SSLF"` + `u32 0x98`
+ `"1RTP"` + 33 `f32` (parâmetros 0…32, valores crus) + 12 B de máscara de notas (C…B) + `f32` do parâmetro 33.
Neste DLL `0x18002df70` = `2^x` e `0x18002c134` = log2 (**inferência pelos operandos**).

| i | Nome | Lei | Evidência |
|---|---|---|---|
| 0 | Correction Speed | `u = 1 − v`; u < 0,01 → `c = 1` (instantânea); senão `c = 8,82/(fs·(3u³ + 0,001))`, polo por amostra que leva a correção suavizada (`+0x1724/+0x1728`, semitons) ao alvo (`+0x1714`) → **τ = (3u³ + 0,001)/8,82 s** (−76 dB em `3u³ + 0,001` s). v = 0,9025 → τ ≈ 0,43 ms; v = 0,6 → τ ≈ 21,9 ms; v = 0 → 0,34 s. Sem altura detectada (`+0x16F4 = 0`) a correção suavizada é zerada. **Corrige a rodada 15** (que estimou 0,07 s para v = 0,6) | DECOMP `FUN_18001cab0` caso 0, `FUN_18001e030`; constantes 8,82 (`0x18005d608`), 3, 0,001, 0,01 |
| 1 | Stereo | só nas vozes de harmonia: L = `clamp(2·(0,5 − (pan − 0,5)·s), 0, 1)`, R = `clamp(2·(0,5 + (pan − 0,5)·s), 0, 1)`, s = p1 (que `pan` seja p17…20 é inferência) | DECOMP `FUN_18001f800`; DOC "Panning multiplier" |
| 2 | Gender | fator **`2^(2v − 1)`** (0,5…2; 0,5 = neutro) em `+0x16A0`, usado **só no caminho Formant** (`FUN_18001f2b0` → `FUN_18001f180`); o efeito interno **não decodificado** | DECOMP; DOC "Formant override control" |
| 3 | Fine Tune | `2v − 1` **semitons** somados à correção (±100 centésimos) | DECOMP `FUN_18001e030`; DOC "+/- 100 cents (1 semitone)" |
| 4 | Bypass All | fora do modo MIDI, correção-alvo = 0 | DECOMP `FUN_18001ea80` |
| 5…16 | Key C…B Bypass | se a classe da nota mais próxima tiver o botão ligado, correção = 0 (fora do MIDI) | DECOMP; DOC "…pass un-corrected" |
| 22 | **Formant** | escolhe o render `FUN_18001f2b0` (com formante/Gender) em vez de `FUN_18001ed60`; força reinício (`+0x16A4` → `FUN_18001d290`) | DECOMP; nome pelo DOC (inferência forte) |
| 23 | Note A Freq | `A = 55·2^((2v − 1)/12)` Hz (±1 st em torno de 440/8); nota detectada `n = 9 + 12·log2(f/A)`; v = 0,5 → Lá 440 Hz; faixa 415,3…466,2 Hz | DECOMP caso 0x17 (1/6, 1/12, 55); `FUN_18001ea80` |
| 24 | Pitch Reference | `int(2v)` → 0/1/2: de qual detector vem a correção de cada canal; com 0, L é copiado para R em `FUN_18001ed60`; ordem L/R/Independent pelo DOC (caso 2 não conferido) | DECOMP + DOC |
| 25 | **MIDI** | máscara de notas montada das **notas MIDI presas** (vetor de 128 em `+0x1068`) | DECOMP `FUN_18001d9c0`, `FUN_18001b800`, `FUN_18001b5e0` |
| 26 | **Octaves** | no MIDI, usa a nota absoluta (nota − 12) em vez da classe; na harmonia soma 12 × oitava | DECOMP; DOC "to the note AND octave played" |
| 27 | **Harmonize** | até **4 vozes** a partir das notas MIDI; sem ele, uma voz só | DECOMP `FUN_18001f800`, `FUN_18001e030`; DOC "Up to 4 MIDI notes" |
| 28/29 | Tônica / escala | como na rodada 15 (`FUN_18001d890`) | DECOMP |
| 30 | **Replace/Mix** | no Harmonize, ≠ 0 soma a entrada seca; = 0 a voz 0 tem ganho 1 | DECOMP; nome pelo DOC |
| 31 | **Vel** | ≠ 0 → ganho por voz × `2·vel²` (só com Octaves desligado) | DECOMP; DOC |
| 32 | Faixa | 220/170/110/80/55/25 Hz (rodada 15) | DECOMP; DOC "minimum detected frequency" |
| 33 | Porta MIDI | `int(256v) − 1` (constante −256) — confirma a rodada 16 | DECOMP |

**Algoritmo da correção** (`FUN_18001ea80`, em palavras nossas): por canal com altura detectada, calcula `n`;
procura na máscara de 128 notas a nota permitida mais próxima abaixo e acima; escolhe a mais próxima (decisão no
meio do intervalo); alvo = `nota − n` semitons; suaviza pelo polo de Correction Speed; soma Fine Tune; o render
aplica o deslocamento (no caminho Formant, conversão `2^(−s/12)`).

**Dados** **[DADOS: 65 blobs, 61 em projetos + 4 `.fst`]**: Speed 1,0 em 32, 0,9025 em 13, 0,6…0,8 nos demais;
Stereo = 1 em todos; Gender 0,5 em 54, ≈0,49/0,5025 em 9, 1,0 em 2; Fine 0,5 (±0,0025); Bypass All e Key Bypass
= 0 em todos; Formant ligado em 7; MIDI + Octaves + Harmonize juntos em 2 (Bien Duro); 30 = 31 = 0; Pitch Ref 0 em
64; Faixa 2 em 56, 3 em 9.

**Aproximação estática:** o código **não** aplica deslocamento fixo — a correção depende, por amostra, da altura
detectada, da nota permitida mais próxima e do polo. **Não há equivalente estático exato.** Identidade exata só
com Bypass All = 1 (fora do MIDI) e Fine = 0,5 (correção zero); se o render ainda colore o som nesse caso (ou com
Formant e Gender = 0,5) **não foi confirmado**. A única aproximação honesta sem detector é passar o sinal reto.
Uma implementação fiel exige detector (f_min pela tabela do p32), escolha da nota na máscara, polo `c` e
deslocador — o detector e o deslocador do FL **não foram decodificados**. Harmonize/MIDI dependem das notas da
porta (rodada 16).

### 6b. Fruity Vocoder — estado básico **[DFM + DADOS; layout por inferência]**

- Tags do DFM (`TSynthEditorForm`): 0 FreqMin (0…65536, padrão 0), 1 FreqMax (0…65536, 65536), 2 FreqScale
  (0…128, 64), 3 Invert, 4 Formant (−1200…1200, 0), 5 Bandwidth (5…200, 50), 6 Attack (10…1000, 20), 7 Release
  (10…1000, 100), 8 Hold, 9 Modulator level (0…160, 0), 10 Carrier level (0…160, 0), 11 Wet level (0…160, 128).
  Seletores fora dos tags: NumBands (6 opções, padrão índice 2), Filter order (3, padrão 2), Listen (3, padrão 2).
- Layout observado (10 blobs: 1 demo + 9 `.fst`, todos versão 2): `i32` versão = 2; `i32` **número de bandas**
  (4, 16 ou 32 nos dados); dois `i32` = 2 e 2 (compatíveis com Filter order/Listen — **ordem não confirmada**);
  1 byte (0; **não confirmado**); **N × `f32` = nível por banda**; **12 × `i32` = tags 0…11** (no preset Default:
  `0, 65536, 64, 0, 0, 50, 20, 100, 0, 0, 0, 128`). O leitor de estado **não foi decompilado** → layout é
  inferência dos dados + DFM.
- Manual: "left channel is the modulator and the right channel is the carrier"; bandas "4 to 128" **[DOC
  `…/plugins/Fruity%20Vocoder.htm`]**. Unidades de Attack/Release/BW/Formant e lei de FreqMin/Max em Hz **não
  confirmadas**. **Vocodex:** nada além da rodada 14 §6 (**não confirmado**).

---

## 7. Fruity Love Philter — tipos 4/5, 7 e 11 **[DECOMP]**

**Mapa tipo → biquad** (tabela `0x8d1e4c` → código passado a `FUN_006249d0`, que monta os coeficientes do "Audio
EQ Cookbook" (RBJ) normalizados por `1 + α`; conferido nos casos LP e passa-tudo; `FUN_005fc960` = SinCos):
3 → LP, **4 → BP**, **5 → notch**, 6 → HP, **7 → passa-tudo**, 8 → low shelf, 9 → peaking, 10 → high shelf.

### 7.1 Tipos 4/5 (Vanilla band pass / band stop)

- **Frequência:** `x = min((1501^c − 1)·0,00039993; 1,0001)`, `ω = clamp((x + 0,0001)·2π·k; 0; 2,8495)` com
  `k = 44100/fs_interno` → **`fc = (x + 0,0001)·44100` Hz** (c = 0 → 4,4 Hz; 0,5 → ≈ 670 Hz), até `0,4535·fs_interno`.
- **Largura em oitavas:** `BW = 0,01 + 0,1199·(101^r − 1)` (r ≤ 0 → 0,01; r ≥ 1 → **12 oct**), com
  `α = sen ω·senh((ln 2/2)·BW′·ω/sen ω)` — a fórmula RBJ de largura em oitavas (`FUN_00466a90` = senh; global
  `0x8e3500` = `Ln(2)/2`, inicializada em `0x62d8b3`).
- `BW′` por estágio = `BW·N` no BP e `BW/N` no notch (N = inteiro do modo `+0x30`; `FUN_0085d5a0` grava `+0xC0`);
  nos modos *alternate* 4/5, N vale 4/5 embora os estágios sejam 2/3 (é o que o código faz). Com ALT (`+0x38`):
  `r ← r·(c + 1)·0,5`.

### 7.2 Tipo 7 (Vanilla all pass)

- Frequência: mesma lei de 7.1. **`Q = 0,1 + 0,02495·(2001^r − 1)`** (0,1…50); multiestágio: `Q ← Q^(1/estágios)`
  (`+0xBC`, `FUN_005fc870` = potência). `α = sen ω/(2Q)`; passa-tudo RBJ
  `H(z) = [(1 − α) − 2cos ω·z⁻¹ + (1 + α)·z⁻²] / [(1 + α) − 2cos ω·z⁻¹ + (1 − α)·z⁻²]`. ALT: `r·(c + 1)·0,5`.

### 7.3 Tipo 11 (Mango low pass) (`FUN_006261c0` sem *drive*, `FUN_00625ea0` com *drive*)

- **4 polos de 1ª ordem em cascata por canal** (`y += p·(entrada − y)`); entrada do 1º = `in + k·fb`, `fb =
  y₄[n] + y₄[n−1]` da amostra anterior; saída = `g·(y₄[n] + y₄[n−1])`.
- Coeficientes (`FUN_00859100`, caso 11): `x = min((201^c − 1)·0,01495; 3,0)`; `w = min((x + 0,01)·0,5·k; 0,65)`;
  **`p = 1 − exp(−π·w·(1,873w³ + 0,4955w² − 0,649w + 0,9988))`** (global `0x8e3440` = `4·ArcTan(1)` = π).
  **Inferência:** igualando a `1 − e^(−2π·fc/fs)`, `fc ≈ (x + 0,01)·11025·polinômio(w)` Hz (c = 0 → ≈ 110 Hz;
  0,5 → ≈ 2,3 kHz; teto em w = 0,65 → ≈ 0,42·fs_interno).
- **Ressonância:** `R = r·(1,9 + 0,6·min(G; 1))`; multiestágio `R ← (R + 1)^(+0xBC) − 1` com
  `+0xBC = 1/(estágios/4 + 1)`; **`k = −R·(−3,9364w² + 1,8409w + 0,9968)`**.
- ***Drive*:** `G = (251^s − 1)·0,12`, s = parâmetro 3 do slot/128 (`+0x240 = int(+0xC)·0,0078125`, render
  `FUN_0085aa10`). Se G ≥ 0,001: cada polo usa `p/G·(S(2G·entrada) − S(2G·y))` com
  `S(u) = u·(u² + 3|u| + 6)/(|u|³ + 3u² + 6|u| + 12)`; `g = 0,5·(1 − k)/tanh(4/G)` (`FUN_00466b40` = tanh pela
  estrutura); com ALT, `k ×= tanh(4/G)`. Sem *drive*: `g = 0,5·(1 − k)`.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 1a | Effector Trancer (8) | ganho `1 − e·(1 − Y²)`; `e` sobe em 1 % do período e cai como `1 − min(1, u⁴·(50(1−X)² + 1))`; período = Mod Rate; **Wet Mix ignorado**; sem LFO **[DECOMP]** |
| 1b | Effector Stutter (9) | *buffer* de 1,5 s gravado desde o *reset*; a cada período repete o trecho inicial (*crossfade* 192 amostras); **X/Y não usados**; crossfade seco/molhado **[DECOMP+DOC]**; reverso inativo/não confirmado |
| 1c | Effector Vowel (10) | 8 biquads BP Q = 12 em paralelo; 3 conjuntos de formantes em ciclo A→B→C→A por `3X'`; Y escala `2^(1,86Y' − 1)`; crossfade **[DECOMP]**; divergência com o manual (5 vogais) |
| 1d | Effector Ring (11) | seno de **`2^(8,7X' + 5)` Hz** (32 Hz…13,3 kHz); `(1 − w)x + w·x·sen`; Y não usado **[DECOMP+DOC]** |
| 1e | Effector Distort | 4 estágios `s/(1 + k|s|)`, `s/(1 + ks²)`, `s/(1 + k|s|³)`, `s/(1 + ks⁴)`, `k = 2048d²`; `G = 0,5/F(0,5)`; molhado `G·F(lp) + (x − lp)` **[DECOMP]** |
| 1f | Effector Reverb | **seco preservado**: `saída = x + Reverb(w·x)` **[DECOMP]** |
| 2a | Harmor E | **E = y − 0,5 ∈ [−0,5; 0,5]** (art. 4/5 aditivos, centro 0,5) → ±7,92 oct·amt (confirma o LYNX) **[DECOMP+DADOS]** |
| 2b | Harmor ressonância | `r = p57/1000·A₈`; ganho por harmônica `2^(r·s(x))` (exp inferido), forma "Resonance shape", offset p60 em centésimos **[DECOMP]**; pico em dB **não confirmado** |
| 2c | Harmor *note-off* sem env. | nível **constante**; voz só sai por roubo/redisparo com *declick* de **10 ms** **[DECOMP]** |
| 3 | Kepler | `2·L₁ ∈ [−1; 1]` (tabela 2 170 pts quase senoidal) → A/A_c são picos ±; atraso por rampa (tabela D); chorus com entrada mono, seco/molhado **−3 dB cada** (`√0,5`), sem realimentação **[DECOMP+DADOS]**; BPM Sync **não confirmado** |
| 4 | Toxic | modo = **p300** (0 Bypass, 1 LP s4, 2 BP s3−s4, 3 HP u−s4); KT = 0,09·KT oct/st (ref. 60); EG `sinal(2x−1)(2x−1)²`·9 oct, env 0…1; Lfo1 `x³`·9 oct **[DECOMP+DADOS+DOC]** |
| 5 | Sakura Hi-Cut | 4 polos 1ª ordem, `c = g·E_hc`; por estágio `f = −fs·ln(1 − c)/2π`, −3 dB da cascata ≈ 0,435·f **[DECOMP + dedução]**; fs efetiva **não confirmada** |
| 6 | Pitcher | botões 22 Formant, 25 MIDI, 26 Octaves, 27 Harmonize, 30 Mix, 31 Vel; Speed τ = `(3(1−v)³ + 0,001)/8,82` s; Fine ±1 st; A = `55·2^((2v−1)/12)`; Gender `2^(2v−1)` **[DECOMP+DOC+DADOS]**; sem aproximação estática |
| 6b | Fruity Vocoder | versão, N bandas, 2 seletores, N níveis, 12 tags (inferência por dados + DFM) |
| 7 | Love Philter | 4/5: BP/notch RBJ com **largura 0,01…12 oct**; 7: passa-tudo RBJ Q 0,1…50; 11: Mango = 4 polos com realimentação e *drive* racional **[DECOMP]** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Effector:**
   - Trancer: ganho `1 − e·(1 − Y²)` com a forma do §1.1 e período do Mod Rate; não aplicar Wet Mix.
   - Stutter: repetir, a cada período, o trecho gravado desde o início da reprodução/ativação (até 1,5 s), com
     *crossfade* de 192 amostras; ignorar X/Y.
   - Vowel: banco de 8 BP Q = 12 com as frequências da tabela, interpolação cíclica por `3X'` e escala
     `2^(1,86Y' − 1)`; × `(1 − 0,125n)`.
   - Ring: `2^(8,7X' + 5)` Hz, seno.
   - Distort: a cascata de 4 estágios com `G = 0,5/F(0,5)` só na parte grave.
   - Reverb: somar o reverb ao seco (`x + Reverb(w·x)`) em vez de *crossfade*.
2. **Harmor:** manter o envelope do filtro centrado em 0,5 com ±7,92 oct por amt; com o envelope de volume
   desligado, nota segue soando após o *note-off* até roubo/redisparo (no LYNX, aplicar só um *declick* de 10 ms
   quando a voz for liberada pelo limite de polifonia).
3. **Kepler:** LFO1 bipolar (±1, quase seno) com profundidade = pico; chorus: somar L+R/2 na entrada, seco e
   molhado a −3 dB, sem realimentação.
4. **Toxic:** modo do filtro pelo p300 (0/1/2/3 = Bypass/LP/BP/HP); EG Amount `sinal(2x−1)(2x−1)²`·9 oct;
   key-track 0,09·KT oct/semitom a partir da nota 60.
5. **Sakura:** Hi-Cut como 4 polos com `c = Cutoff²`·envelope; para converter a Hz usar `−fs·ln(1 − c)/2π` por
   estágio (com a ressalva da fs).
6. **Pitcher:** sem equivalente estático — passar reto; registrar no log quando Harmonize/MIDI (27/25) ou
   Gender ≠ 0,5 com Formant (22) estiverem ligados.
7. **Love Philter:** tipos 4/5 com largura em oitavas (RBJ, 0,01…12 oct), 7 passa-tudo RBJ (Q 0,1…50), 11 Mango
   com o modelo do §7.3.

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| Effector `FUN_180018c20`, `FUN_18001ba60`, `FUN_18001bb30`, `FUN_180018bb0`/`FUN_180018bd0`, `FUN_180016d40`, `FUN_180017010` | despacho por tipo, unidades em série, campos X/Y/taxa, fase sincronizada | DECOMP |
| Effector `FUN_180019f90` (constantes 0,01; 1,0101; 50; 100) | Trancer | DECOMP |
| Effector `FUN_18001ae50`, `FUN_180018700` (1,5 s), `FUN_180018a50` (*reset*), `FUN_18001b910`, `FUN_1800179a0` | Stutter | DECOMP |
| Effector `FUN_18001b140` (tabelas `0x1800586a0…d0`, 3, 0,93, 0,125, Q 12), `FUN_180014520`, `FUN_180014680` | Vowel, biquad BP | DECOMP |
| Effector `FUN_18001b5a0` (8,7 e 5 em `0x18005854c`/`0x180058518`) | Ring | DECOMP |
| Effector `FUN_180019860` (0,95; 0,05; 2048; 0,9; 0,1) | Distort | DECOMP |
| Effector `FUN_18001a130`, `fxReverb` VMT `0x180057fc0` slot 43 `FUN_18001e9f0` | Reverb soma na saída | DECOMP |
| Manual Effector (Trans, Grain, Vox, Ring mod, Reverb, Distortion) | X/Y descritos | DOC `…/plugins/Effector.htm` |
| Harmor `FUN_007a0c60`, `FUN_0079fef0`, `FUN_0079a650`, `FUN_007a4880`, `FUN_0079a7c0`/`FUN_0079a7f0`, `FUN_00896fe0` (`0x89732c`), `FUN_00897c70`, `FUN_00897d90`; 682 partes | E centrado | DECOMP+DADOS |
| Harmor `FUN_00886eb0`, `FUN_008744c0`, `FUN_00882af0`, `FUN_00882820`, `FUN_00878f60`; DFM | ressonância | DECOMP |
| Harmor `FUN_00885390`, `FUN_0088ab10`, `FUN_008856b0`, `FUN_008855e0`, `FUN_00883cd0`, `FUN_008635c0`, `FUN_008781f0`, `FUN_00863450`, `FUN_00862d70` | *note-off*, morte, *declick* 10 ms | DECOMP |
| Kepler `FUN_1800166d0`, `FUN_1800225f0`, `FUN_180023ab0`, `FUN_18001b940`, `FUN_180013d30` (`0x180252ED0`), `FUN_180019a20` (`0x180254100`), `FUN_180018fe0`, `FUN_180001a60`, `FUN_180001770` | LFO1 ±1, atraso, chorus | DECOMP+DADOS |
| Toxic `FUN_180040080`, `FUN_180040ff0`, `FUN_1800406d0`, `FUN_1800395b0`, `FUN_180038d00`, `FUN_180039880`; 7 blobs | modo p300, escalas | DECOMP+DADOS |
| Sakura `FUN_18002f4c0`, `FUN_18002da50`, `FUN_180031960` | Hi-Cut 4 polos | DECOMP |
| Pitcher `FUN_18001cab0`, `FUN_18001e030`, `FUN_18001ea80`, `FUN_18001f800`, `FUN_18001d9c0`, `FUN_18001b5e0`, `FUN_18001b800`, `FUN_18001ed60`, `FUN_18001f2b0`, `FUN_18001c240`; 65 blobs | parâmetros, algoritmo | DECOMP+DADOS |
| Fruity Vocoder DFM + 10 blobs | layout do estado | DADOS (inferência) |
| Love Philter `0x8d1e4c`, `FUN_006249d0`, `FUN_00859100`, `FUN_0085d5a0`, `FUN_006261c0`, `FUN_00625ea0`, `FUN_0085aa10`, `0x8e3500`, `0x8e3440` | tipos 4/5/7/11 | DECOMP |
| Manual (Pitcher, Fruity Vocoder, Toxic filtro) | nomes, faixas | DOC |

### Questões em aberto

- Effector: quem escreve o byte de reverso do Stutter (nenhum escritor encontrado); nomes das vogais dos 3
  conjuntos; corte em Hz da cascata do Filter (rodada 18).
- Harmor: fator `2·(hi − lo)` do termo direto da saída do articulador 4/5 (não relido); `k₀` da largura de
  ressonância e curva padrão da "Resonance shape"; modo "noise"; `+0x1150` = `exp`; morte de vozes pelo host.
- Kepler: lei do BPM Sync do LFO; condição do *gate* G.
- Toxic: faixa de `vel`; amplitude do LFO1; tempos de `FUN_18003d480`.
- Sakura: taxa efetiva do motor (Hz absolutos do Hi-Cut).
- Pitcher: detector de altura e deslocador; efeito interno do Gender; se o render colore o som com correção
  zero; caso 2 do Pitch Reference.
- Fruity Vocoder: leitor de estado (ordem dos dois seletores, byte extra), unidades; Vocodex.
- Love Philter: correspondência exata em Hz do Mango (inferência pelo polo); significado do parâmetro 3 do slot
  no Mango.
