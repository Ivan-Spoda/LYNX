# Análise comportamental do FL Studio 2026: rodada 18 (REA) — escalas que o LYNX ainda supõe (Harmor, Sytrus, Effector, Love Philter, roteamento de envios no host/Patcher, GMS, Kepler, Toxic Biohazard, BassDrum, Sakura, BooBass e Drumpad)

> **Natureza deste documento.** Continuação de `REA_rodada17.md`. Engenharia reversa *clean-room*, apenas
> comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são fórmulas e
> comportamentos descritos com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para
> rastrear a evidência. Nenhum conteúdo cifrado foi tocado (nenhum pacote FLEX foi aberto).
>
> **Regra desta rodada (pedido do usuário): nada de especulação.** Cada valor vem de decompilação
> (**[DECOMP]**), do manual oficial da Image-Line (**[DOC]**, com URL) ou dos dados (**[DADOS]**). Onde não
> houve evidência suficiente, o item está marcado **"não confirmado"**. Inferências feitas *a partir* do código
> (ex.: "este campo é a taxa de amostragem porque é escrito pela rotina de taxa") estão explicitadas no texto.

- **Alvos** (cópias em `tmp/r18/bin/`; os já analisados foram conferidos por hash contra a instalação):

  | Plugin | DLL (SHA-256) | Linguagem | Funções-chave |
  |---|---|---|---|
  | Harmor | `23673ad8…499d5aba` (= rodada 11) | Delphi | `ProcessParam` `FUN_00879540` (VMT+6), faixa do filtro `FUN_00874240`, constantes `FUN_0089a4d0`/`FUN_00630170`, volume da parte `FUN_00873b00`, voz `FUN_0088ab10`, filtro por voz `FUN_00886eb0`, articulador `FUN_00898200`/`FUN_00897d90`, formas de filtro `FUN_00874ed0`, estado `FUN_0086ed30` |
  | Sytrus | `d52a97c5…8e944f15` (= rodada 11) | Delphi | montagem `FUN_0086dd40`, tabela do operador `FUN_0086ace0`, render do operador `FUN_00870580`/`FUN_00877270`, laço `FUN_00870820`, nível de MOD `FUN_00872290` |
  | Effector | wrapper `2f538ec9…512f72e5` + `engine_x64.dll` `4bfdc0f6…7dc2071a` | Delphi + motor C++ (`FXPlugin`/`FXInstance`) | `setParameter` `FUN_180017600`, texto `FUN_180017240`, processador `FUN_18001b7f0`…`FUN_18001be60`, unidades por tipo `FUN_180018c20` → `FUN_180019860` (Distort), `FUN_180019b60` (Crusher), `FUN_180019d60` (Flanger), `FUN_18001a680` (Phaser), `FUN_180019110` (Filter), `FUN_180018de0` (Echo), `FUN_18001a130` (Reverb), `FUN_18001abc0` (Panner); LFO `FUN_180017ec0`; linha de atraso `FUN_1800181c0` |
  | Fruity Love Philter | `b9f11d63…ba2ff547` (projeto `/tmp/r17gh_LP`) | Delphi | estado `FUN_008551e0`, construtor `FUN_00856780`, articulador: VMT `0x853b18` (slot 0 `FUN_008544d0`, carga `FUN_008542e0`, *reset* `FUN_008541f0`, envelope/LFO `FUN_008543d0`) |
  | `FLEngine_x64.dll` (host) | `6d9c5ab0…5d9b8e02` | Delphi | `TFruityHost` VMT `0x13141b8` (envio = slot 37 `FUN_0132bb50`), tabela de saídas laterais `FUN_00982ac0`/`FUN_00982b10`/`FUN_009827d0`, ação `TRegisterSideIOAction` (`FUN_01316a50`), construtores de *wrapper* `FUN_012bd800`/`FUN_012bdf90` |
  | GMS | `20c88eba…f5fcec9c` (= rodada 12) | C++ | render `FUN_1800f7530`, offset de fase `FUN_1800ef960`, leitura de tabela `FUN_1800ef9d0` |
  | Kepler (motor) | `baf37a1d…2afc63ab` (= rodada 13/17) | C++ | *setter* por voz `FUN_180022840`, voz `FUN_180023ab0`, *setter* global `FUN_180019a20`, render global `FUN_18001b940`, chorus `FUN_180001a60`, taxa `FUN_18001d300` |
  | Toxic Biohazard | `5c326637…f136cb46` (= rodada 15/17) | C++ | gerador `FUN_180038890` (tabela do filtro), voz `FUN_180038d00`, alvo do corte `FUN_1800395b0`, operadores `FUN_180029fa0`, rotas FM `FUN_180029cc0`/`FUN_180039b30`, matriz `FUN_18003e730` |
  | BassDrum (motor) | `2fb1b2f8…12c25e8b` (= rodada 15/17) | C++ | início de nota `FUN_180028ff0`, render `FUN_180029440`, tabela `FUN_180028c90`, criação `FUN_18002a63f`/`FUN_180028d50`, `setParameter` `FUN_18002ae90` |
  | Sakura (motor) | `e632487c…ae0f1bab` (= rodada 17) | C++ (`CSakuraSynth`/`CSakuraKey`) | `setParameter` `FUN_18002f4c0` (VMT `0x1800778b8` slot 23), nomes `FUN_18002eaa0`, voz por bloco `FUN_18002da50`, render `FUN_180031960` |
  | BooBass (motor) | `4e10efcd…db51705f` | C++ (`BooBassPlug`) | `ProcessParam` `FUN_1800023a0`, knobs `FUN_1800040e0`/`FUN_1800041c0`/`FUN_180004240`, amostra `FUN_1800042f0`, taxa `FUN_1800034f0` |
  | Drumpad (motor) | `136e8a49…3ccb3933` | C++ (`DrumpadSynth`/`DMPadGenerator`) | nomes `FUN_180023450`, estado `FUN_180022d00`/`FUN_180022de0`, início de nota `FUN_18001f4d0` |

- **Ferramentas:** Ghidra 12.1.4 *headless* (projetos novos `/tmp/r18gh_FLE` — importado **sem análise
  automática**, funções criadas sob demanda —, `/tmp/r18gh_EffEng`, `/tmp/r18gh_BooEng`, `/tmp/r18gh_DrumEng`;
  projetos anteriores abertos só para leitura); scripts em `tmp/r18/gh/scripts/` (os da rodada 17); utilitários
  novos em `tmp/r18/`: `k.py` (lê constantes e ponteiros de constantes num PE), `vmtdump.py`, `vmtname.py`
  (acha a VMT Delphi pelo nome da classe), `calls.py` (acha `CALL`/`JMP` diretos para um endereço),
  `riprefs.py` (referências RIP-relativas). Manual oficial lido via web.
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

## 1. Harmor (100 canais nos demos)

**O que o LYNX supõe** (`FlSytrus.cpp` `synthFromHarmor`): parte = serra; nível = `|p11|/1000 × mix A/B`;
envelope de volume desligado = portão `A 2 ms / S 1 / R 0,1 s`; corte dos filtros `73,4·2^(7,855·v/8192)` Hz;
quantidade do envelope ±1000 = ±7,9 oitavas, centrada no nível 0,5; tipos de filtro: 0/4/13 → LP, 1/16 → BP,
2 → rejeita-faixa, 3 → HP, 5…12 → LP; uníssono `(41^(p38/256) − 1)·1,875` centésimos.

### 1.1 Volume quando o articulador de volume está desligado **[DECOMP]**

- **Avaliação do articulador** (`FUN_00898200` monta, `FUN_00897d90` avalia, por voz): saída inicial = **base**
  (campo `+0x20` do articulador), que o código fixa em **1,0 quando o modo de combinação (`+8`) ≠ 1** e em
  **0,0 quando = 1**. Os componentes só entram numa lista se estiverem **ligados**: envelope (índice 0) só se o
  objeto do envelope tiver `+0x48 ≠ 0`; LFO (índice 1) idem; mapas 2…10 entram se não forem constantes (mapas
  constantes são "dobrados" na base). Combinação por componente: modo 0 → multiplica, modo 1 → soma, outros →
  multiplica por `2×valor`. O resultado é suavizado por um polo (`+0xA8`).
- Logo, **com o envelope de volume desligado, ele simplesmente não participa**: a saída do articulador de
  volume fica = base (1,0 no modo multiplicativo) × mapas não neutros. **Não existe ADSR padrão nem portão
  implícito** dentro do articulador.
- **Uso da saída** (`FUN_0088ab10`): o ganho em frequência da voz é **`(saída do art. 1)² × g12`**, onde `g12`
  é o "Frequency domain envelope volume" (p12, abaixo). **O nível é o quadrado da saída do articulador.**
- **Fim da voz** (`FUN_0088ab10`, final): a voz é encerrada pelo envelope de volume **somente se esse envelope
  estiver ligado** e tiver chegado ao estado final (`> 8`); com ele desligado, a voz só termina pelo estado
  interno de *release* do host/voz (`+0x214 ≥ 6`). O que acontece com o nível depois do *note-off* nesse caso
  (rampa, *declick*) **não foi confirmado**.
- **Mapeamentos globais 34…37** (articuladores "global env A/D/S/R"): na partida da nota, se as saídas desses
  quatro articuladores não forem `(1, 1, 0, 1)`, **para todo articulador cujo envelope esteja ligado** o código
  multiplica o ataque, o decay e o release do envelope pelas saídas 34, 35 e 37 e **soma** a saída 36 ao
  sustain. Eles **não criam** envelope onde não há; só escalam os ligados.
- Manual: "Many envelopes need to be activated with the switch in the lower-left corner before articulating the
  target" e "the voice ends when the volume envelope has ended" **[DOC `…/plugins/Harmor.htm`]** — coerente.

### 1.2 Frequência dos filtros 1/2 (p52/p64, 0…8192) → Hz **[DECOMP]**

- `ProcessParam` (caso 52/64, `FUN_00879540`): **`L = lo + v·(hi − lo)/8192`**, em **log2(Hz)** (campos do objeto
  de filtro `+0x80`, `+0x88`, resultado em `+0xA0`).
- Limites (`FUN_0089a4d0`, com `ln 2` e `log2` inicializados em `FUN_00630170`): **`lo₀ = log2(440·2^(−31/12))
  = log2(73,416 Hz)`** e **`hi₀ = log2(440·2^(64/12)) = log2(17 739,7 Hz)`** (amplitude 7,9163 oitavas).
- Correção pela largura (`FUN_00874240`, chamada pelo parâmetro 54/66 "Filter width"): com
  `W = (2/3)·2^(0,004·p54 − 1)` e as extensões `eL`, `eR` da forma do filtro (fração não nula da tabela da forma,
  ≤ 1, calculada em `FUN_00874ed0` só para algumas formas), **`lo = lo₀ − max(0, W·eL − 1)`** e
  **`hi = hi₀ + max(0, W·eR − 1)`**. No padrão (p54 = 500 → W = 4/3) e com `e = 1`, a faixa vira 58,3 Hz…
  22,3 kHz. Os valores de `eL/eR` por forma **não foram tabelados** (não confirmado).
- **Corte por voz** (`FUN_00886eb0`): `L_voz = L (suavizado) + KT·(P − log2 130,81) + termos de articulação`, e
  **`fc = 2^L_voz` Hz** (o `2^x` é calculado ali mesmo). `P` = log2 da frequência da voz (`log2 440 +
  (cents/100 + 3)/12`, `FUN_0088ab10`); `KT` = p55/1000 ("Key to filter frequency", padrão 1000). Ou seja, **o
  knob vale em Hz absolutos para uma nota de 130,81 Hz** e acompanha o teclado com fator KT.
- **Envelope do filtro**: o articulador 4/5 contribui `2·(hi − lo)·(saída)` e, se o envelope dele estiver
  ligado, mais `2·(hi − lo)·(p56/1000)·E·m` (E = nível do envelope; m = saída do articulador de mapeamento
  31/32 "F1/F2 env amount"). Em modo "adaptive" (p51) a fórmula é outra (interpolação até o limite). **A faixa
  efetiva de E (0…1 ou centrado) não foi confirmada**; portanto a centragem em 0,5 usada pelo LYNX **não está
  confirmada** e o fator correto da quantidade é **2·(hi − lo) ≈ 15,8 oitavas por unidade**, não 7,9.

### 1.3 Volume da parte (p11), p12 e uníssono (p38) **[DECOMP]**

- **p11 "Time domain volume"** (±2000, `FUN_00873b00`): `x = |p11|/1000`; **`g(x) = 1` se x = 1, senão
  `(3^x − 1)/2`**; ganho da parte = `g(MainVol/1000) · g(|p11|/1000) · sinal(p11) · k`, onde MainVol é o
  global 10011 e `k` é o campo `+0x1C` da parte (não identificado). p11 = 1000 → 1,0; 2000 → 4,0 (+12 dB);
  500 → 0,366 (−8,7 dB). A curva linear `p11/1000` do LYNX está errada fora de 0 e 1000.
- **p12 "Frequency domain envelope volume"** (0…2000): mesma curva `(3^(v/1000) − 1)/2` → multiplica o
  `(saída do articulador)²` do §1.1.
- **p38 "Unison pitch thickness"** (0…256, `ProcessParam` caso 38): **`s = (41^(v/256) − 1)·1,875`**; por voz,
  `s × (saída do articulador "unison pitch")² × deslocamento da distribuição` é **somado à variável de tom da
  voz, que está em centésimos** (`FUN_008859a0`, `FUN_0088ab10`). Confirma a escala do LYNX (centésimos);
  os deslocamentos por voz da distribuição (`FUN_00873e40`) não foram tabelados.

### 1.4 Ordem dos tipos de filtro **[DECOMP + DFM + DADOS]**

- O inteiro gravado no bloco de 0x4B B (bytes 52/56) é copiado tal qual para o código da forma do filtro
  (`FUN_0086ed30` → objeto `+0x3F8`) e o construtor de formas (`FUN_00874ed0`) trata os códigos **0…18**
  (19 formas = os itens do combo `F1TypeSelect` **sem os separadores**).
- Conferido pela forma gerada: 0 = curva que cai no alto (LP), 1 = curva simétrica no centro (BP), 2 = o
  complemento da 1 (BS), 3 = espelho da 0 (HP); 4…7 repetem o padrão com outra curva (Crude LP/BP/BS/HP).
- Lista: **0 Classic LP, 1 Classic BP, 2 Classic BS, 3 Classic HP, 4 Crude LP, 5 Crude BP, 6 Crude BS, 7 Crude HP,
  8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 12 Fin LP, 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP,
  17 Custom 1, 18 Custom 2** (nomes do DFM `TSynthEditorForm`). Dados: filtro 1 = 4 em 552 partes, 0 em 141,
  17 em 54; filtro 2 = 18 em 685 partes **[DADOS 429 blobs]**.
- Manual: "Low pass, Band pass, Band stop, High pass, Phaser, Custom shape 1 & 2" (ordem de alto nível)
  **[DOC `…/plugins/Harmor.htm`]**.
- **Erro no LYNX:** 5, 6 e 7 são passa-faixa, rejeita-faixa e passa-altas (Crude), não passa-baixas.

---

## 2. Sytrus (268 canais) — unidade da FM e fórmula do RM **[DECOMP]**

**O que o LYNX supõe:** FM com índice `4·g` **ciclos**; RM tratado como AM com o modulador no osc 3.

- **Render do operador** (`FUN_00870580`, `FUN_00877270`): a fase `φ` é um `double` em **amostras da tabela de
  onda**; a leitura usa `φ + M·mod` com interpolação linear e máscara `N − 1` (N = comprimento da tabela).
  `M` é o acumulador de FM do operador (zerado a cada amostra) e `mod` é o nível do articulador **MOD** do
  operador, rampado por bloco e limitado a 0…1 (`FUN_00872290`).
- **Escala da quantidade**: a montagem (`FUN_0086dd40`) põe na lista de FM do alvo a quantidade
  `4·g` (e `g·35/128` para a auto-realimentação), com `g = sinal(v)·(11^(|v|/256) − 1)/10`. Quando a tabela do
  operador alvo é (re)construída (`FUN_0086ace0`), **toda quantidade de FM que aponta para ele é multiplicada
  por N** e a fase é convertida de ciclos para amostras (`φ ·= N`).
- Portanto **o desvio de fase é `4·g·out_mod·mod` ciclos da onda do portador** (out_mod = saída do modulador,
  que já inclui o volume dele). Em radianos: `8π·g·out·mod`. **A suposição "4·g ciclos" do LYNX está
  confirmada** (com o fator MOD, que vale 1 por padrão — padrão do articulador não relido aqui).
- **RM** (mesmas funções): cada fonte RM `j` multiplica o acumulador de RM do alvo por
  **`(1 − |a_j| + a_j·out_j)`**, com `a_j = g` (sem o ×4). Na amostra seguinte o portador é multiplicado por
  **`1 − mod + mod·Π_j(1 − |a_j| + a_j·out_j)`**. Com a = 1 é o produto puro (anel); com a = 0,5 metade do sinal
  passa sem modulação. A ordem de render dos operadores implica um atraso de uma amostra para fontes renderizadas
  depois do alvo.
- Manual: "Ring Modulation is the process by which two input signals are multiplied together" e "if the value
  is negative … the modulation phase will be inverted" **[DOC `…/plugins/Sytrus.htm`]** (sem unidades).

---

## 3. Effector (26 slots) **[DECOMP + DOC]**

**O que o LYNX supõe** (`FlEffects.cpp` `effector`): tipo = `round(11·p0)`; cada tipo vira um efeito interno do
LYNX com X/Y mapeados "aproximadamente"; mix do slot = p2; bypass = p1 ≥ 0,5.

### 3.1 Parâmetros e tipos

- 12 parâmetros (`FUN_180017050`): 0 Effect Kind, 1 FX Bypass, 2 Wet Mix, 3 Param X, 4 Param Y, 5 Mod Rate,
  6 Tempo Reduce, 7 Mod Amount X, 8 Mod Amount Y, 9 Mod Shape, 10 Input Level, 11 Output Gain.
- **Tipo = `trunc(11·v + 0,5)`**: 0 Distort, 1 Crusher (Lo-fi), 2 Flanger, 3 Phaser, 4 Filter, 5 Echo (Delay),
  6 Reverb, 7 Panner (Stereo), 8 Trancer, 9 Stutter, 10 Vowel, 11 Ring (nomes internos, `FUN_180017240`). O
  LYNX acerta o índice.
- Gerais: **Input Level** = ganho `v²` na entrada; **Output Gain** = `10^((v − 0,5)·2,4)` (texto `(v − 0,5)·48` dB);
  **Mod Amount X/Y** = `sinal·(2v − 1)²`; **Mod Shape** `trunc(2v + 0,5)`: 0 seno, 1 serra `1 − 2φ`, 2 quadrada ±1;
  **Mod Rate** = valor da tabela `{0,125; 0,25; 0,375; 0,5; 0,75; 1; 2; 4; 8}` (rótulos "1/8"…"8/1") e
  **período T = valor·60/BPM s**; "Tempo Reduce" escala a taxa por `(1 − v)²`. X e Y efetivos =
  `clamp(X + LFO·ModX)` e idem Y.
- **Wet Mix** (p2) vai para cada unidade (`+0xADC`), suavizado por amostra (coef. 0,01). Bypass = p1 ≠ 0.

### 3.2 Leis de X, Y e mistura por tipo

| Tipo | X | Y | Mistura |
|---|---|---|---|
| **Distort** | `d = 0,95X + 0,05`; `k = 2048·d²` (saturação racional em cascata, com ganho de compensação calculado para que 0,5 saia 0,5) | pré-filtro passa-baixas de 1 polo com coeficiente `(0,9Y + 0,1)⁴`: só a parte grave é distorcida; o resto (entrada − grave) passa limpo | `(1 − w)·seco + w·molhado` |
| **Lo-fi (Crusher)** | taxa: segura cada amostra por `128X³ + 1` amostras | bits: `65536/(2048Y² + 1)` níveis (Y = 0 → 16 bits; Y = 1 → 32 níveis) | `(1 − w)·seco + w·molhado` |
| **Flanger** | posição de atraso `X² (+ LFO·ModX)` numa linha de **5 ms** | realimentação `−0,96·(Y + LFO·ModY)` | **`seco + w·molhado`** (entrada do atraso invertida) |
| **Phaser** | 6 passa-tudo de 1ª ordem por canal, `a = (1 − g)/(1 + g)`, `g = S·(1 + 0,5·X')·(20000 − 50)/fs + 50/fs` (S = X² suavizado), limitado a 0,001…0,499 | realimentação `0,98·(0,6·Y')^(1/3)` | `seco + w·molhado` |
| **Filter** | `s = 2(0,8X + 0,1)`: s ≤ 1 → passa-baixas, coef. de polo `s³`; s > 1 → passa-altas, coef. `0,5·(s − 1)³` | ressonância `0,7Y + 0,2` (reduzida perto do topo do LP); no HP `6,2568·Y` | `(1 − w)·seco + w·molhado` |
| **Delay (Echo)** | realimentação `X²` (também é o ganho da saída do eco) | filtro do eco: Y < 0,5 escurece (LP `0,99·2Y + 0,01`), Y > 0,5 corta graves (HP `0,225·(2Y − 1)`); Y = 0,5 neutro | `seco + w·eco` |
| **Reverb** | `Decay = 0,75·X'³` e nível do reverb `0,225·X'³ + 0,5` | **`HighDamping = Y'²`** | entrada do reverb = `w·entrada`; reverb com Dry = 0, ER mudo, RoomSize/Diffusion/HighCut = 1, Predelay = 0; **se o seco é preservado não foi confirmado** |
| **Stereo (Panner)** | `p = 2X − 1`: atraso de até **2 ms** no lado oposto (Haas) | quanto do pan vira diferença de nível (`Y·p`) | `(1 − w)·seco + w·molhado` |

- O **Filter** é uma cascata de 4 seções de 8 polos com realimentação (Ladder estendido) — a curva em Hz depende
  da taxa: com um único polo de coeficiente `c`, `fc ≈ −fs·ln(1 − c)/2π` (X = 0 → c = 0,008 ≈ 56 Hz a 44,1 kHz;
  X = 0,25 → c = 0,216 ≈ 1,7 kHz); o corte da cascata completa **não foi tabelado**.
- **Delay — tempo:** o tempo do eco **não é o X**: é o período do Mod Rate (`T = valor·60/BPM`), dividido por 2
  até caber em 1,5 s (linha de 1,5 s), mínimo 1,5 ms. **O LYNX usa X como tempo — errado.**
- Manual: Distort X "Distortion amount", Y "Tone control"; Lo-fi "Bit-depth reduces with increased height",
  "Sample rate reduces as increased to right"; Flanger/Phaser X "Depth", Y "Feedback"; Filter "Left of center =
  Low pass … Right of center = High pass", Y "Resonance amount"; Delay X "Feedback amount", Y "Echo filtering";
  Reverb X "Feedback", Y "Wet level"; Stereo X "Left/Right pan position", Y "Binaural effect level"
  **[DOC `…/plugins/Effector.htm`]**. **Divergência:** no Reverb o código liga Y ao *HighDamping*, não a um
  nível de mistura.

---

## 4. Fruity Love Philter (39 slots) — modo de combinação e envelope/LFO desligados **[DECOMP]**

**O que o LYNX supõe** (`parseLovePhilter`): o `f32` gravado no articulador é uma flag "unipolar".

- **O `f32` não é flag de modo.** A carga do articulador (`FUN_008542e0`) lê `versão` e **12 bytes em `+0x20`**:
  `+0x20` = **base** (valor inicial da saída), `+0x24` = profundidade do envelope, `+0x28` = profundidade do LFO.
- **O modo inteiro (`+8`) não é gravado no `.flp`**: o construtor (`FUN_00856780`) chama o método 0 da VMT
  (`FUN_008544d0`, que grava `+8` e reinicia) com **modo 1 (aditivo)** para os articuladores **0 (pan), 2
  (cutoff) e 3 (ressonância)** (máscaras `0x01` e `0x0C`); os demais (1 volume, 4 low/gain, 5 band, 6 high,
  7 waveshaper) ficam no **modo 0 (multiplicativo)**. O *reset* (`FUN_008541f0`) põe base = **0 no modo 1 e
  1,0 no modo 0**, profundidade do envelope 1,0 e do LFO 0,5.
- Assim, **"unipolar" (f32 = 1,0) ↔ modo 0 (multiplicar)** e **"bipolar" (f32 = 0,0) ↔ modo 1 (somar)** — mas a
  correspondência é por alvo (fixa no código); o valor do arquivo é a base, não o modo.
- **Método `+0x38`** (`FUN_008543d0`): envelope → só se o envelope estiver ligado (`+0x48 ≠ 0`):
  modo 1: `saída += env·prof_env`; senão `saída ·= env·prof_env`. LFO → só se ligado: modo 1:
  `saída += lfo·prof_lfo`; senão `saída ·= 1 + 2·lfo·prof_lfo`. **Desligados, não fazem nada (neutros).**
- Manual: não descreve o modo de combinação nem os articuladores desligados **[DOC `…/plugins/Fruity%20Love%20Philter.htm`]**.

---

## 5. Envio para "barramento k" (Frequency Splitter / Stereo Shaper) no host **[DECOMP]**

**O que o LYNX supõe** (rodada 17 §1.1/§2): destino de envio k = barramento k do Patcher.

- O envio dos plugins é o método 37 (`+0x128`) do objeto host. Em `FLEngine_x64.dll` o host concreto é a classe
  **`TFruityHost`** (VMT `0x13141b8`, filha do abstrato `TFruityPlugHost` `0x7b67d0`); o slot 37 é
  **`FUN_0132bb50`** (`(sender, k, &buffer)`; o bit 0 do 2º campo distingue pegar/liberar).
- **Cálculo da trilha de destino:**
  - se o *wrapper* do plugin tem `+0xA7 = 0` → trilha = **(trilha do wrapper `+0x258`) + k**, limitada a 0…500;
  - se `+0xA7 ≠ 0` → trilha = **`lista[min(k, n − 1)]`**, onde `lista` é um vetor de índices de trilha guardado no
    registro da trilha onde o plugin está (`trilha[+0x130 do wrapper] + 0x128`).
  - `+0xA7` é **0** no construtor de *wrapper* usado pelos canais (`FUN_012bd800`) e **1** no construtor que
    recebe (trilha, slot) (`FUN_012bdf90`, chamado de `0x12870e6`) — isto é, plugins de slot do mixer usam a
    lista da trilha **[DECOMP; o significado "lista de envios da trilha" é inferência]**.
- **Substituição pelo "side output":** antes de devolver o *buffer* da trilha, o host procura na tabela
  `+0x128` do *wrapper* (`FUN_00982ac0`/`FUN_00982b10`) uma entrada com **chave = k**; se existir e tiver
  *buffer* próprio, **o envio vai para esse *buffer* privado** em vez da trilha. Entradas são criadas por
  `FUN_009827d0` (chave k, dono) a partir da ação **`TRegisterSideIOAction`** (`FUN_01316a50`; criada no
  despacho do host em `0x1321d01` com o índice e as flags recebidas do plugin) e da enumeração de saídas da
  porta (`FUN_012b6400`, consulta 0x37 ao plugin; os plugins Fruity examinados não respondem a essa consulta).
  O *buffer* da entrada só é alocado quando ela tem um "consumidor" (`FUN_00982c90`).
- **Conclusão:** o mecanismo de saída lateral é **indexado pelo mesmo k do envio** (confirmado). **Que o
  Patcher conecte o consumidor da chave k ao pino de saída k do nó não foi lido no código** (o lado do
  Patcher não foi rastreado) → a igualdade "k = barramento k do Patcher" continua **não confirmada em código**,
  embora o mecanismo por chave k e os dados da rodada 14 sejam coerentes com ela.
- Manual: "Sends are Pre-Fader to the destination Mixer Track" e "destinations won't be available until you have
  created Mixer Track sends" **[DOC `…/plugins/Frequency%20Splitter.htm`]** (coerente com a lista da trilha).

---

## 6. GMS **[DECOMP]**

**O que o LYNX supõe** (rodada 12 §1.3): FM = `15·m` quartos de ciclo (HIP); saída do envelope = `e²`.

- **FM é PM com 15·m quartos de ciclo — confirmado.** O *buffer* de modulação recebe `m·15` (constante
  `0x18014d6b8` = 15) no modo FM (`FUN_1800f7530`); o deslocamento de fase é
  `int(osc2·m·15·4194303 + 268435392)·256` somado à fase de 32 bits (`FUN_1800ef960`), isto é,
  **`osc2·15·m·2^30` = osc2·15·m quartos de ciclo** (= 3,75·m·osc2 ciclos; o termo constante é um arredondamento
  de −2^14/2^32 ciclo). Tabela de 1024 pontos (`fase >> 22`) com *dither* aleatório no índice (`FUN_1800ef9d0`).
- **AM:** `osc1·(1 − 3·m·osc2)` (constante 3) — confirmado.
- **Sync:** um acumulador "mestre" de 32 bits soma o seu incremento; **quando ele dá a volta, a fase do osc1 é
  substituída pelo valor do mestre** (o resíduo após a volta, não zero). O incremento do osc1 com `(1 + 3m)` é o
  da rodada 12 (não relido aqui).
- **Amplificador:** a saída de amplitude por amostra é **`e²`** (`e` = valor do envelope) **+** contribuições de
  LFO/envelope com destino volume — confirmado em `FUN_1800f7530`.

---

## 7. Kepler **[DECOMP]**

**O que o LYNX supõe:** profundidades LFO→DCO/corte e taxas do chorus não modeladas.

- **LFO1 → DCO1** (painel k = 10 → interno 0; "Range" interno 1): `A = tabela(x)` com 11 pontos
  **{0; 0,00116; 0,00813; 0,01506; 0,02196; 0,02882; 0,05259; 0,07819; 0,12057; 0,17728; 0,21388}** (oitavas),
  **×10 se Range ≥ 0,5** (`FUN_180022840`). No tom da voz (`FUN_180023ab0`) entra **`2·L₁·A` oitavas**
  (`f = 440·2^(1,0005·Σ)·k`), com L₁ = valor do LFO1.
- **LFO1 → corte** (painel 23 → interno 0x2D): `A_c = tabela(x)` **{0; 0,0914; 0,38038; 0,662; 0,96846; 1,23732;
  1,48249; 1,85735; 2,27554; 2,66441; 2,82}** oitavas; entra no expoente do corte como **`2·L₁·A_c` oitavas**.
- **Amplitude de `L₁`** (faixa do LFO): **não confirmada** (o ponteiro do valor do LFO na voz não foi rastreado).
- **Chorus** (`FUN_18001b940` ao trocar de modo; processamento `FUN_180001a60`; `+0x1BDE0` = taxa de amostragem,
  escrita em `FUN_18001d300`):

  | Modo | LFO | Atraso (s) |
  |---|---|---|
  | I | triângulo 0…1, **0,4 Hz**, L/R defasados de 0,5 ciclo | `0,00108 + 0,00415·tri` (**1,08…5,23 ms**) |
  | II | triângulo, **0,6 Hz**, L/R 0,5 ciclo | mesmo |
  | I+II | **seno, 8 Hz**, L/R defasados 0,0286 ciclo | `0,00285 + 0,00052·tri` (**2,85…3,37 ms**) |

  A mistura seco/molhado do chorus não foi relida. Manual: "Emulates the Juno 6 Chorus I/II" **[DOC
  `…/plugins/Kepler.htm`]**.

---

## 8. Toxic Biohazard **[DECOMP]**

**O que o LYNX supõe:** índice de FM e corte em Hz não modelados (rodada 17: não confirmados).

- **FM:** cada operador tem fase de 32 bits; a leitura usa `fase + int(M·16777215)·256` → **`M` em ciclos**
  (`FUN_180029fa0`; tabela de 1024 pontos com interpolação cúbica). O acumulador é reiniciado em 20,0 (número
  inteiro de ciclos, sem efeito na fase). As rotas somam `M_alvo += q·out_fonte` (`FUN_180029cc0`), com
  **`q = 2·célula`** (`FUN_180039b30`) e `célula = (⌊100x⌋/100)²`. Logo **desvio de fase = 2·célula·out ciclos**
  (β = 4π·célula·|out| rad; `out` inclui envelope e nível do operador).
- **Corte do filtro (param 15):** o alvo por voz (`FUN_1800395b0`) é
  `u = corte + KT·(nota − 60)·0,01 + VelAmt·(vel − 1)·0,5 + EGAmt·env`, convertido em índice `u·65535` de uma
  tabela de 65536 coeficientes construída em `FUN_180038890` com **`fc = 2^(9u + 5)` Hz** (u = 0 → 32 Hz;
  0,5 → 724 Hz; 1 → 16 384 Hz). Os coeficientes são os do filtro "Moog" de 4 polos com afinação polinomial
  calculados para **fs = 44 100** (`f = 2fc/44100`, `k = 3,6f − 1,6f² − 1`, `p = (k + 1)/2`,
  `escala = e^((1 − p)·1,386249)`). O filtro roda em `FUN_180038d00` (4 estágios por canal + saturação).
- Manual: 4 modos (Bypass/LP/BP/HP), "Cutoff - Determines the cutoff frequency" (sem Hz) **[DOC
  `…/plugins/ToxicBio_filter.htm`]**.

---

## 9. BassDrum **[DECOMP + DADOS]**

**O que o LYNX supõe:** Peak = `base·2^(6·peak)`; clique não modelado.

- **N = 1024.** A tabela do oscilador vem do arquivo **`Plugins/Fruity/Generators/BassDrum/sounddata`**, lido na
  criação do motor (`FUN_18002a63f`); é um WAV *float* mono 44,1 kHz com **1024 amostras** **[DADOS]**; N =
  número de amostras (`FUN_180028c90` lê `N·frac(fase)`). Forma: quase senoidal (fundamental 0,855; harmônicas
  ≤ 0,02), pico −1/+0,70 **[DADOS]**.
- Com N = 1024, **`f(t) = base·(1 + 100·x³·s(t)⁴)`** (x = Peak; s = envelope de *slide* da rodada 17); o
  incremento usa `1/44100` fixo (se o motor roda na taxa do host, o tom escala por `fs/44100` — **não
  confirmado**).
- **Clique:** p14 FREQ → `f_clique = 2^(14x)` Hz (x = 0,5 → 128 Hz; 1 → 16 384 Hz); p15 LEVEL → `x⁵`, amplitude
  `25·x⁵`; o clique lê a **mesma tabela** a partir de uma fase aleatória entre 0,75 e 1,0 até a fase 1 (um
  trecho de ¼ de ciclo). p13 CUT → guardado cru (x); **uso não confirmado**.

---

## 10. Sakura **[DECOMP]**

**O que o LYNX supõe** (rodada 11 nota): ADSR tipo Harmless, LP ≈ Hi-Cut.

- **Índices** (`FUN_18002eaa0`): 0 Master Level, 1 (Tune), 2–5 **Amp A/D/S/R**, 6 Str1 Decay, 7 Str1 Decay Inv,
  8 Str1 Damp Offset, 9 Str1 Damp Amount, 10 Str1 Damp Shrp, 11–15 idem Str2, 16 Str Saturation, **17 Str Mix**,
  18 Str Stereo Sep, 19–23 Comb, 24/25 Lo-Cut, 26–28 Hi-Cut A/D/S, **29 Hi-Cut Cutoff, 30 Hi-Cut Res**,
  33 Hi-Cut Drive, 31/32 ruído, 34–37 EG A/D/S/R, 38 LFO Rate, 39–62 Resonators, 63–77 Delay/Chorus/Reverb.
- **Amp ADSR** (`FUN_18002f4c0`, voz `FUN_18002da50`, render `FUN_180031960`):
  - Ataque: `T_A = 35x⁵ + 0,001` s; alvo 5,0 com coeficiente `0,2236/(T_A·48000)` e saída **limitada a 1,0** →
    chega a 1,0 em ≈ **T_A** (pois `1 − e^(−0,2236) ≈ 0,2`);
  - Decay: `T_D = 50x⁵ + 0,001` s, polo para o sustain com coeficiente `8,82/(T_D·48000)`;
  - Sustain: **x²**;
  - Release: `T_R = 10x⁵ + 0,1` s, polo para 0 com `8,82/(T_R·48000)`; **a voz é morta após T_R**.
  - O envelope é aplicado **linear** (não ao quadrado). O código usa 48 000 como taxa nos coeficientes e no
    relógio do envelope; o comportamento em outras taxas **não foi confirmado**.
- **Tune (1):** `trunc(198x − 99)·0,01` **semitons** somados à nota (±0,99 st). Manual: "Transpose the tuning
  +/- 50 cents" **[DOC `…/plugins/Sakura.htm`]** — **divergência** (o código permite ±99 ct).
- **String decay (6):** realimentação da corda 1 = **`d^0,05`** (d = Decay + modulações, 0…1), com sinal
  invertido se "Decay Inv". Como a perda é por período, **T60 ≈ 60/(f·(−log10 d))** s (ex.: d = 0,9 → 1 310/f s).
  Amortecimento separado: `exp(2,57 − 0,8995·a³)·(2/44100)` (a = Damp Amount + mods) e `(1 − Offset)·…`.
- **String Mix (17):** **corda 1 ∝ (1 − mix), corda 2 ∝ mix** (lineares), × `0,05·10^(mod·…)` e pela tabela de
  pan da separação estéreo.
- **Hi-Cut (29/30):** coeficiente do filtro do excitador **`g = clamp((Cutoff + 0,5·env)², 0, 1)`**;
  realimentação/ressonância **`−10·Res·(1 − g)`**, limitada a ≥ −4. Envelope do Hi-Cut: ataque
  `(A + mod)²·0,5`, decay com coeficiente `0,0002/(3D³ + 0,001)`, sustain `S²`. **A conversão de `g` para Hz não
  foi confirmada** (o filtro do excitador no render não foi decodificado).

---

## 11. BooBass **[DECOMP]**

**O que o LYNX supõe:** Bass = nível de um seno 2 oitavas abaixo; Mid = nível de uma serra; Treble = corte
`300·2^(4·treble)` Hz.

- **Fonte sonora** (`FUN_1800042f0`): **amostra**: um ataque gravado (`+0xB0`) em *crossfade* para uma tabela de
  **16 384 amostras** em laço (`+0xA8`), tocada a `2^(cents/1200)`, × curva de amplitude tabelada. Não é
  oscilador analógico (a origem das tabelas não foi rastreada).
- **Os três knobs são um equalizador de 3 bandas** (0…65535 → x = v/65535; x < 0,1 → banda muda):
  - **Bass:** `10^((12x − 12)/20)` (−12…0 dB) e um segundo ganho `10^((24x − 24)/20)` para a camada saturada;
  - **Mid:** `10^((10x − 10)/20)` (−10…0 dB);
  - **Treble:** `10^((12x − 9)/20)` (−9…+3 dB).
- **Bandas:** dois filtros de 1 polo com coeficientes `0,025·44100/fs` e `0,155·44100/fs` (`FUN_1800034f0`) →
  grave = LP1, médio = LP2 − LP1, agudo = entrada − LP2 (cortes ≈ **178 Hz** e **1,18 kHz** a 44,1 kHz).
  Saída = `grave·B + médio·M + agudo·T`, repetida numa 2ª etapa com saturação na banda grave, ×3 e limitador de
  *slew* (`HighLimiter`). Manual: "Lets you equalize the low (bass) / middle / high (treble) frequencies"
  **[DOC `…/plugins/BooBass.htm`]**.
- **O mapeamento do LYNX (níveis de osciladores e corte) não corresponde** ao BooBass.

---

## 12. Drumpad **[DECOMP + DADOS]**

**O que o LYNX supõe:** só o nome do *patch*; timbre por palavras do nome.

- **Estado** (`FUN_180022d00`/`FUN_180022de0`): `"3PMD"` + **128 B de nome** + **38 `float` = parâmetros 0…37 na
  ordem do índice** (tamanho 0x11C = 284 B). No `.flp` (`SSLF` + tamanho + estado) os *floats* começam no
  **offset 140** **[DADOS: 975 blobs]**. A interface é desenhada pelo motor (o DFM do *wrapper* é genérico), então
  **não há Tags de DFM: o índice k é o próprio parâmetro**.
- **Índices** (`FUN_180023450`): 0/1 reservados, 2 Mallet Amplitude, 3 Mallet Decay, 4 Mallet Noise,
  5 Mallet Noise RP, 6 Membrane Decay, 7 Membrane Cutoff, 8 Tension, 9 Phase, 10 Material, 11 Size, 12 Shape,
  13 LOW Freq, 14 LOW Res, 15 SOF Freq, 16 SOF Decay, 17 SOF Level, 18 MID Freq, 19 MID Level, 20 MID Res,
  21 MID Freq Mod, 22 MID Level Mod, 23 MID Attack, 24 HIGH Freq, 25 HIGH Level, 26 HIGH Res, **27 Lo-Fi,
  28 Hold, 29 Pitch**, 30…37 reservados.
- **Escalas confirmadas** (`FUN_18001f4d0`; os valores passam por `FUN_1800229f0`, que pode aplicar modulação por velocidade — não lido): **Pitch = ×2^(2x − 1)** (±1 oitava; 0,5 = sem transposição);
  **Mallet Amplitude = 10^(−4·(1 − x))** (0 dB em 1; −80 dB em 0); Hold = `3x³ + 0,001` (unidade não
  confirmada); Membrane Size → inteiro `⌊512·(1 − x)⌋` (≤ 511); Shape → inteiro `128x` (1…128). As demais
  (decays, frequências da malha e dos filtros) **não foram confirmadas** — o modelo é uma malha física
  ("a mesh of inter-connected points" **[DOC `…/plugins/Drumpad.htm`]**), sem equivalente direto num sintetizador
  de 3 osciladores.

---

## Tabela-resumo (item → resultado)

| # | Item | Resultado |
|---|---|---|
| 2a | Harmor: volume com articulador desligado | componente ausente → saída = base (1,0 no modo multiplicativo) × mapas; ganho = **saída² × (3^(p12/1000)−1)/2**; voz não é encerrada pelo envelope; mapeamentos 34–37 escalam A/D/R (×) e S (+) só de envelopes ligados **[DECOMP]**; nível após *note-off* sem envelope **não confirmado** |
| 2b | Harmor: corte p52/p64 | **`fc = 2^(lo + v(hi−lo)/8192 + KT(P − log2 130,81))`**, lo/hi = log2(73,416)/log2(17 739,7) Hz alargados por `max(0, W·e − 1)` **[DECOMP]**; envelope ±`2(hi−lo)·amt` oitavas; centragem do envelope **não confirmada** |
| 2c | Harmor: p11, p38 | p11: `(3^(|v|/1000) − 1)/2` (1000 → 1,0; 2000 → 4,0) × sinal × MainVol; p38: `(41^(v/256) − 1)·1,875` **centésimos** **[DECOMP]** |
| 2d | Harmor: tipos | código 0…18 = combo sem separadores (0 Classic LP … 4 Crude LP, 5 Crude BP, 6 Crude BS, 7 Crude HP … 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP, 17/18 Custom) **[DECOMP+DFM+DADOS]** |
| 3 | Sytrus FM / RM | desvio de fase = **4·g·out·MOD ciclos** (confirma o LYNX); RM: portador × `1 − MOD + MOD·Π(1 − |a| + a·out)`, a = g **[DECOMP]** |
| 6 | Effector | tipo `round(11v)`; Distort `k=2048(0,95X+0,05)²` + tom `(0,9Y+0,1)⁴`; Lo-fi hold `128X³+1`, níveis `65536/(2048Y²+1)`; Flanger 5 ms, fb −0,96Y; Phaser 6 AP, fb `0,98(0,6Y)^(1/3)`; Filter LP `s³`/HP `0,5(s−1)³`; Delay fb X², filtro por Y, **tempo = Mod Rate**; Reverb Decay `0,75X³`, HighDamping Y²; Stereo Haas 2 ms; mistura: crossfade (Distort/Lo-fi/Filter/Stereo) ou seco + w·molhado (Flanger/Phaser/Delay) **[DECOMP]**; seco no Reverb **não confirmado** |
| 11 | Love Philter | f32 do arquivo = **base**; modo fixo por alvo: pan/cutoff/res = 1 (soma, base 0), demais = 0 (multiplica, base 1) → "unipolar" ↔ modo 0; envelope/LFO desligados = **neutros** **[DECOMP]** |
| 12 | Envio k → barramento | host: trilha = trilha+k (canal) ou `lista_da_trilha[k]` (slot do mixer); substituído por *buffer* lateral da chave **k** se registrado (`TRegisterSideIOAction`) **[DECOMP]**; ligação chave k ↔ pino k do Patcher **não confirmada** |
| 7 | GMS | FM = PM `15·m·osc2` quartos de ciclo (**confirmado**); AM `1 − 3m·osc2`; sync = fase do osc1 ← fase do mestre ao dar a volta; amp = `e²` (**confirmado**) **[DECOMP]** |
| 10 | Kepler | LFO1→DCO `2·L·A(x)` oitavas (A ≤ 0,214; ×10 com Range); LFO1→corte `2·L·A_c(x)` oitavas (A_c ≤ 2,82); chorus I 0,4 Hz / II 0,6 Hz (triângulo, 1,08–5,23 ms) / I+II 8 Hz seno (2,85–3,37 ms) **[DECOMP]**; amplitude do LFO **não confirmada** |
| 9 | Toxic | FM: **2·célula·out ciclos**; corte **`2^(9u+5)` Hz** (32 Hz…16,4 kHz), Moog 4 polos (fs 44,1 k) **[DECOMP]** |
| 8 | BassDrum | **N = 1024** (arquivo `sounddata`) → Peak `f = base·(1 + 100x³s⁴)`; clique FREQ `2^(14x)` Hz, LEVEL `25x⁵`, trecho de ¼ de ciclo da mesma tabela **[DECOMP+DADOS]**; CUT **não confirmado** |
| 1 | Sakura | Amp A `35x⁵+0,001` s, D `50x⁵+0,001` s (τ = T/8,82), S x², R `10x⁵+0,1` s (48 kHz no código); Tune ±0,99 st; decay da corda `d^0,05` por período; Mix linear; Hi-Cut `g=(c+0,5env)²`, res `−10·r(1−g)` **[DECOMP]**; Hi-Cut em Hz **não confirmado** |
| 4 | BooBass | amostra (ataque + laço de 16 384); knobs = EQ 3 bandas (B −12…0 dB, M −10…0 dB, T −9…+3 dB; cortes 1 polo ≈ 178 Hz / 1,18 kHz) **[DECOMP]** |
| 5 | Drumpad | estado `3PMD` + nome 128 B + 38 floats (k = índice; offset 140 no `.flp`); Pitch ×2^(2x−1), Mallet Amp 10^(4(x−1)) **[DECOMP+DADOS]**; demais escalas **não confirmadas** |

---

## Recomendações para o LYNX (só o que ficou confirmado)

1. **Harmor:** corte = `2^(log2 73,416 + p·7,9163/8192)·(f_nota/130,81)^(p55/1000)` (sem a correção de largura
   quando não se sabe a forma); tipos 5/6/7 → BP/BS/HP; volume da parte `(3^(|p11|/1000) − 1)/2·sinal`;
   com o envelope de volume desligado, nível constante (não aplicar o `A 2 ms/R 0,1 s` como envelope do FL —
   é só um *declick* do LYNX); quando ligado, aplicar o nível ao quadrado.
2. **Sytrus:** manter a FM em `4·g` ciclos; trocar o RM-como-AM por `portador × (1 − |a| + a·modulador)`.
3. **Effector:** Delay com tempo = período do Mod Rate (`valor·60/BPM`, dobrado até ≤ 1,5 s) e realimentação X²;
   Filter com X < 0,5 = LP e X > 0,5 = HP; Lo-fi com `128X³ + 1` e `65536/(2048Y² + 1)`; Flanger/Phaser/Delay
   somam o molhado ao seco (`seco + w·molhado`) em vez de *crossfade*; Output Gain `(v − 0,5)·48` dB.
4. **Love Philter:** tratar o `f32` como base e fixar o modo por alvo (pan/cutoff/res aditivos); envelope/LFO
   desligados = neutros.
5. **GMS:** FM do LYNX = PM com índice `15·m/4` ciclos (= 2π·3,75·m rad).
6. **Kepler:** vibrato LFO1→DCO `2·L·A(x)` oitavas e LFO1→corte `2·L·A_c(x)` oitavas (assumindo L = ±1 só como
   aproximação, ver questões em aberto); chorus I/II/I+II com os valores do §7.
7. **Toxic:** corte `2^(9u + 5)` Hz; FM com índice `2·célula` ciclos.
8. **BassDrum:** Peak `base·(1 + 100x³)` no início do *slide*; clique opcional `2^(14x)` Hz com `25x⁵`.
9. **Sakura:** ADSR do §10 (tempos em s) e Tune ±0,99 st.
10. **BooBass:** substituir o mapeamento por fonte fixa + EQ de 3 bandas com os ganhos do §11.
11. **Drumpad:** ler os 38 *floats* a partir do offset 140 e usar ao menos Pitch (`×2^(2x − 1)`) e Mallet Amplitude.

---

## Índice de evidências

| Função / dado | O que mostra | Marca |
|---|---|---|
| Harmor `FUN_00879540` (casos 11, 12, 38, 52/64, 54, 55, 56) | escalas p11/p12/p38, corte linear em log2 Hz, largura, KT, env amount | DECOMP |
| Harmor `FUN_0089a4d0`, `FUN_00630170` | limites log2(73,416)/log2(17 739,7); `ln 2`, `log2 440` | DECOMP |
| Harmor `FUN_00874240`, `FUN_00874ed0` | faixa alargada pela largura; formas 0…18 e extensões | DECOMP |
| Harmor `FUN_00886eb0`, `FUN_0088ab10` | corte por voz `2^L`, referência 130,81 Hz; ganho = saída² × p12; fim da voz | DECOMP |
| Harmor `FUN_00898200`, `FUN_00897d90`, `FUN_00873b00`, `FUN_008859a0`, `FUN_00873e40` | articulador (base, lista, modos); volume da parte; uníssono em centésimos | DECOMP |
| Harmor `FUN_0086ed30` + 429 blobs | tipo de filtro gravado = código da forma; contagens | DECOMP+DADOS |
| Sytrus `FUN_0086dd40`, `FUN_0086ace0`, `FUN_00870580`, `FUN_00877270`, `FUN_00872290` | FM ×4 ×N (ciclos), MOD 0…1, RM `1 − |a| + a·out` | DECOMP |
| Effector motor `FUN_180017050`, `FUN_180017240`, `FUN_180017600`, `FUN_18001b7f0…be60`, `FUN_180018c20`, `FUN_180019860`, `FUN_180019b60`, `FUN_180019d60`, `FUN_18001a680`, `FUN_180019110`, `FUN_180018de0`, `FUN_18001a130`, `FUN_18001d3f0`, `FUN_18001abc0`, `FUN_180017ec0`, `FUN_1800181c0`, `FUN_180018520` | parâmetros, tipos, leis de X/Y e mistura, LFO, atraso | DECOMP |
| Love Philter `FUN_008542e0`, `FUN_008541f0`, `FUN_008543d0`, `FUN_008544d0`, `FUN_00856780` (máscaras `0x8574a0…a2`) | base/profundidades no arquivo; modo fixo por alvo; envelope/LFO desligados | DECOMP |
| FLEngine `TFruityHost` VMT `0x13141b8`; `FUN_0132bb50`, `FUN_0132ba30`, `FUN_0132baa0`, `FUN_00982ac0`, `FUN_00982b10`, `FUN_009827d0`, `FUN_00982c90`, `FUN_01316a50` (`TRegisterSideIOAction`), `0x1321d01`, `FUN_012b6400`, `FUN_012bd800`, `FUN_012bdf90` | destino do envio; tabela lateral por chave k | DECOMP |
| GMS `FUN_1800f7530`, `FUN_1800ef960`, `FUN_1800ef9d0` (`0x18014d6b8` = 15, `0x18014d60c` = 3) | PM 15·m quartos de ciclo; AM; sync; `e²` | DECOMP |
| Kepler `FUN_180022840` (tabelas `0x180105ba0`, `0x180105c00`), `FUN_180023ab0`, `FUN_18001b940`, `FUN_180001a60`, `FUN_18001d300` | LFO→DCO/corte; chorus | DECOMP |
| Toxic `FUN_180029fa0`, `FUN_180029cc0`, `FUN_180039b30`, `FUN_18003e730`, `FUN_1800395b0`, `FUN_180038890`, `FUN_180038d00` | FM em ciclos ×2·célula; corte `2^(9u+5)` | DECOMP |
| BassDrum `FUN_18002a63f`, `FUN_180028d50`, `FUN_180028c90`, `FUN_180028ff0`, `FUN_18002ae90`; arquivo `sounddata` | N = 1024; clique | DECOMP+DADOS |
| Sakura `FUN_18002eaa0`, `FUN_18002f4c0`, `FUN_18002da50`, `FUN_180031960` | índices; ADSR; decay; mix; Hi-Cut | DECOMP |
| BooBass `FUN_1800023a0`, `FUN_1800040e0`, `FUN_1800041c0`, `FUN_180004240`, `FUN_1800042f0`, `FUN_1800034f0` | knobs = EQ 3 bandas; fonte amostrada | DECOMP |
| Drumpad `FUN_180023450`, `FUN_180022d00`, `FUN_180022de0`, `FUN_18001f4d0`; 975 blobs | layout do estado; índices; Pitch/Mallet | DECOMP+DADOS |
| Manual oficial (páginas citadas) | descrições, ordem de tipos, faixas qualitativas | DOC |

### Questões em aberto

- Harmor: nível após *note-off* com o envelope de volume desligado; faixa de E (0…1 ou centrado) no envelope do
  filtro; extensões `eL/eR` por forma; campo `+0x1C` da parte (fator extra do volume).
- Envio k: o lado do Patcher que liga o consumidor da chave k ao pino de saída k; conteúdo exato da lista
  `+0x128` da trilha (inferido como lista de envios).
- Effector: se o Reverb preserva o seco; corte em Hz da cascata do Filter.
- Kepler: amplitude do valor do LFO (`L`) e a mistura do chorus.
- BassDrum: uso do CUT do clique; se o motor roda a 44,1 kHz internamente.
- Sakura: Hi-Cut em Hz; comportamento dos coeficientes (48 kHz fixos) em outras taxas.
- BooBass: origem das tabelas de amostra; curva de amplitude.
- Drumpad: escalas dos decays, frequências e filtros da malha.
