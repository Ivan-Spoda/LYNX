# Análise comportamental do FL Studio 2026: rodada 8 (REA) — estados nativos que ficaram abertos

> **Natureza deste documento.** Continuação direta de `REA_rodada7.md` (mesmo método, mesmas marcas,
> mesmas leis de curva `Lk`). Engenharia reversa *clean-room*, apenas comportamental: nenhum código nem
> pseudocódigo da Image-Line foi copiado; o que segue são layouts de dados, fórmulas e descrições escritas
> com palavras nossas. Endereços (`FUN_xxxxxxxx`) e offsets servem só para rastrear a evidência.

- **Alvos** (Delphi, Ghidra 12.1.4 via REA; `objdump` para trechos curtos e para o `FLEngine_x64.dll`):

  | Plugin | DLL (SHA-256) | VMT do plugin | `SaveRestoreState` | Texto / `ProcessParam` |
  |---|---|---|---|---|
  | Maximus | `acc7affb…bdc1` | `0x83ee68` | `FUN_0083f8f0` | `FUN_00842d70` / `FUN_00842480` |
  | Soundgoodizer | `fe3f0c13…2ac2` | `0x835de8` | `FUN_00836960` | — / `FUN_00838510` |
  | Fruity Fast Dist | `cba6ef46…682a` | `0x6ba958` | `FUN_006baf30` | `FUN_006bb5e0` / `FUN_006bb260` |
  | Fruity Filter | `492af2fb…9d4a` | `0x6cb550` | `FUN_006cb920` | `FUN_006cc140` / `FUN_006cbd10` |
  | Fruity Flangus | `cc83092f…3d73` | `0x6b6510` | `FUN_006b6b20` | `FUN_006b7330` / `FUN_006b6fb0` |
  | Fruity Soft Clipper | `50aac728…72c6` | `0x6be3f0` | (2 × i32 cru) | `FUN_006bee60` |
  | Transient Processor | `3d0475da…d053` | — | (u32 + 9 × i32 + extras) | `FUN_006edf70` |
  | `FLEngine_x64.dll` (host) | `6d9c5ab0…8e02` | `TFruityHost` `0x13141b8` | — | slot `+0xC0` = distorção rápida |

- **Dados:** os mesmos 165 `.flp` de `FL Studio 2026/Data` (seção do mixer). Slots: Maximus **125**,
  Delay 3 110, Soundgoodizer **97**, Flangus 56, Fast Dist 53, Transient Processor 47, Filter 32,
  Soft Clipper 28. Os 4 presets do Soundgoodizer (`Soundgoodizer/Data/Presets/{a,b,c,d}.dat`) também
  foram lidos — são estados completos do Maximus.
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/desmontagem da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenções da rodada 7: little-endian, `i32` com sinal, `P+0xNN` = offset no objeto do plugin, "parâmetro *i*"
= índice do host (= `Tag` do DFM). `Lk(x) = (k^x − 1)/(k − 1)`. Lei de tempo `T(x) = 2·(501^x − 1)` ms
(x = v/10000), a mesma do Fruity Limiter.

---

## 1. Maximus (125 slots) **[DECOMP+STR+DADOS]**

Multibanda de 3 bandas (low / mid / high) + uma seção **master** com a mesma estrutura de banda. Não há
knob de threshold/ratio: a curva de compressão de cada banda é um **envelope desenhado** (editor de
envelopes do Edison) que mapeia nível de entrada → nível de saída. É esse envelope que carrega
"threshold", "ratio" e "ceiling".

### 1.1 Layout do `0xD5`

| Off | Tipo | Campo |
|---|---|---|
| 0x000 | u32 | cabeçalho: `0x000F0003` (58 slots), `0x000A0003` (45), `0x0000000A` (12), `0x000E0003` (9), `0x000B0003` (1). Se > 0xFFFF a **versão é a palavra alta** (15, 10, 14, 11); senão é o próprio valor. O FL grava `0x000F0003` |
| 0x004 | 59 × i32 | **parâmetros 0…58** (tabela 1.2) → `P+0x12C` |
| 0x0F0 | 59 × i32 | estado reserva (A/B compare); ignorar |
| 0x1DC | 0x22 B | estado de UI: u32 velocidade do monitor (50), u32 **banda selecionada no editor** (0…3), u32, 8 × u8 botões do visual, solo etc. |
| 0x1FE | — | fim do bloco fixo para v ≥ 7 (v < 7: 0x1F9 B; v < 6: 0x1F5; v < 5: 0x1F1; v < 4: 0x1EE; v < 3: formato antigo de 3 bandas lido em pedaços) |
| 0x1FE | u32, u32, u8, 5 × u32 | **só v ≥ 14**: opções (`P+0x328`, `P+0x330`; byte de flags: bit 0 → `P+0x32C`, bit 1, …, **bit 6 → escala da curva**, ver 1.4; 5 opções de visual). Demos: `(2, 0, 6, (3,1,2,2,2))` |
| +29 | u32 | **só v ≥ 15**: `P+0x858` (1) |
| a seguir | 8 × envelope | por banda b = 0…3 (low, mid, high, master): **curva de compressão** (`P+0x3A4+0x100·b`) e um segundo envelope (`P+0x3AC+0x100·b`) |

Tamanhos: v10 = 1.342 B, v14 = 1.371 B, v15 = 1.375 B com 3 pontos por envelope; cada ponto a mais
soma 24 B (1.351…1.462 nos demos). O parser bateu byte a byte em 124/125 blobs (o único v11 tem 4 B a
mais antes dos envelopes **[HIP: campo extra de v11–13]**).

**Envelope serializado** (`FUN_00781bd0`) **[DECOMP]**:

| Off | Tipo | Campo |
|---|---|---|
| +0 | u32 | identificador do tipo de envelope (3) |
| +4 | u32 | versão do envelope (3) |
| +8 | u32 | N = número de pontos |
| +12 | N × 24 B | por ponto: `f64` **Δx** (posição relativa ao ponto anterior), `f64` **y**, `f32` **tensão** (curvatura do segmento que chega no ponto, ≈ −1…+1), 4 B de flags (byte +3 = forma do segmento: 2 = reto, 1 / 0xFF = curvo; byte +1 = seleção) |
| +12+24N | 5 × i32 | marcadores (laço/sustain), normalmente `0, −1, −1, −1, −1` |

O eixo x é **cumulativo** (soma dos Δx), ambos os eixos em 0…1.

### 1.2 Parâmetros (`FUN_00842440` divide o índice: < 7 global; senão banda = (i−7) div 13, controle = 7 + (i−7) mod 13)

Globais (0…6):

| i | Off | Nome (DFM) | Faixa | Default | Conversão |
|---|---|---|---|---|---|
| 0 | 0x04 | Low band frequency | 0…65536 | 19800 | `Hz = 30·533,33^(v/65536)` (30 Hz…16 kHz; default **200 Hz**) |
| 1 | 0x08 | Low band slope | 0/1 | 1 | 12 dB / **24 dB** por oitava |
| 2 | 0x0C | High band frequency | 0…65536 | 48065 | mesma lei (default **3,0 kHz**) |
| 3 | 0x10 | High band slope | 0/1 | 1 | 12 / 24 dB |
| 4 | 0x14 | LMH lookahead delay | 0…10000 | 1115 | `T` ms (default 2,0 ms) |
| 5 | 0x18 | **LMH mix** | 0…1000 | 1000 | `v/1000`: mistura processado/não-processado **de cada banda** (1.3) |
| 6 | 0x1C | Low cut | −1…65536 | 32768 | `Hz = 5·16^(v/65536)` (5…80 Hz; default 20 Hz); **v < 0 = desligado** |

Por banda, `base = 0x20 + 0x34·b` (b = 0 low, 1 mid, 2 high, **3 master**):

| j | Off | Nome | Faixa | Default | Conversão |
|---|---|---|---|---|---|
| 7 | +0x00 | **Mode** | 0…3 | 0 | 0 on, 1 *compression off* (só ganhos/saturação), 2 muted, 3 off (banda passa sem processamento) |
| 8 | +0x04 | **Pre gain** | 0…2000 | 1000 | `L7(v/1000)` linear (1000 = 0 dB, 2000 = ×8 = **+18,1 dB**, 0 = −∞) |
| 9 | +0x08 | **Post gain** | 0…2000 | 1000 | `L7(v/1000)` |
| 10 | +0x0C | Stereo separation | −1000…1000 | 0 | `v/1000` |
| 11 | +0x10 | Soft saturation threshold | −1000…1000 | 0 | 0 = sem saturação; v < 0 **modo A**, v > 0 **modo B**; quantidade `|v|/1000` |
| 12 | +0x14 | Soft saturation ceiling | 0…2000 | 1000 | `L7(v/1000)` |
| 13 | +0x18 | **Attack** | 0…10000 | 1115 | `T` ms (default 2,0 ms); nas bandas 0…2, **0 = "Default"** (herda) |
| 14 | +0x1C | Release 2 | 0…10000 | 0 | `2·T` ms; 0 = "Default" (desligado) |
| 15 | +0x20 | Attack / release 2 curve | 1…8 | 2 | inteiro |
| 16 | +0x24 | **Release** | 0…10000 | 5000 | `2·T` ms (default **85,5 ms**) |
| 17 | +0x28 | Release curve | 1…8 | 3 | inteiro |
| 18 | +0x2C | Sustain (janela de pico) | 0…10000 | 2882 | `T` ms (default 10 ms) |
| 19 | +0x30 | Envelope mode | 0/1 | 0 | peak / RMS |

Conversões lidas no texto do parâmetro (`FUN_00842d70`, strings `"%sms"`, `"%dHz"`, `"No saturation"`,
`"saturation mode A/B"`, `"Default"`) e na unidade de inicialização (`FUN_00852530` monta
`ln 533,33/(ln 2·65536)` e `ln 16/(ln 2·65536)`; `FUN_0062ddf0` monta ln 2, ln 601, ln 11, ln 3, ln 7)
**[DECOMP]**. `ProcessParam` guarda `L7(pre)` em `P+0x380+0x100·b+0x10` e `L7(post)` em `+0x14`
**[DECOMP]**.

### 1.3 Fluxo de sinal **[DECOMP parcial + HIP]**

entrada → low cut (opcional) → divisão em 3 bandas (crossovers 0/2, inclinação 1/3) → por banda:
pre gain → separação estéreo → **compressão pela curva** (detector peak/RMS com attack, release,
release 2 e janela "sustain"; atraso de lookahead "LMH delay") → saturação → post gain → **mistura LMH**
→ soma → banda **master** (mesma cadeia) → saída.

A mistura LMH é, por banda com modo < 2, `saída = mix·processada + (1 − mix)·banda original` (ganho da
banda aplicado aos dois termos) — lido no render (`0x845300…0x845383`) **[DECOMP]**. Modo 2 silencia a
banda, modo 3 deixa passar crua **[HIP: pelo nome e pelos desvios de fluxo]**.

### 1.4 A curva de compressão **[DECOMP]**

`FUN_00846490` transforma o envelope de cada banda numa tabela de ganho de 32.769 entradas:

- Nível de entrada linear `ℓ = i/8192` (0…4, ou seja, até **+12 dBFS**).
- Coordenada no editor: `c = log₃(1 + 2ℓ) / 2` ⇔ `ℓ = (3^(2c) − 1) / 2`. Logo **c = 0,5 é 0 dBFS**
  e c = 1 é +12 dB; c = 0,25 ≈ −8,7 dB, 0,1 ≈ −18 dB, 0,05 ≈ −25 dB.
- Saída: `y = envelope(c)` (pontos interpolados com a tensão do segmento), nível de saída
  `(3^(2y) − 1)/2 · s`, com `s = 0,975` (−0,22 dB) ou `1,0` quando o bit 6 das flags v ≥ 14 está ligado
  (nunca nos demos).
- A tabela guarda `saída/entrada` (ganho estático); o detector com attack/release lê essa tabela.

Curvas típicas (x cumulativo):

| Pontos (x, y) | Significado |
|---|---|
| (0,0) (0,5;0,5) (1,0;0,5) — **default** | linear até 0 dB, depois plano: **limitador brickwall a −0,22 dBFS** |
| (0,0) (0,5;0,5) (1,0;0,6) | linear até 0 dB, acima ≈ **4,4:1** até +12 dB → teto +2,5 dB |
| (0,0) (0,35;0,35) (1,0;0,6) | joelho ≈ **−4,7 dB**, ≈ 2,2:1, teto +2,5 dB (preset B) |
| (0,0) (0,4;0,4) (1,0;0,5) | joelho ≈ −2,4 dB, ≈ 4,9:1, teto −0,2 dB (preset C) |
| (0,0) (0,15;0,15) (1,0;0,15) | limitador a ≈ −14 dB (efeito "esmagado") |

O segundo envelope de cada banda (`+0x3AC`) é o default em 123/125 slots **[DADOS]**; função exata
**[HIP: curva secundária/“gate”; ignorável]**.

### 1.5 Uso nos demos **[DADOS]**

- Master: modo 0 em 85, **1 (compression off) em 30**, 3 em 10. Curva master: default (brickwall
  −0,22 dB) em 82; "4,4:1 acima de 0 dB" em 16; o resto são curvas únicas (joelhos −3,9…−13,7 dB).
- Master pre/post quase sempre 0 dB; os poucos ≠ 0 vão de −10,8 a +13,8 dB. Release master: 85,5 ms
  (82), 117 ms, 32 ms. Saturação master: threshold −100/−16/−12 (modo A, 1…10 %) em ~35 slots, ceiling
  992 (−0,15 dB) em 21.
- Bandas: modo 0 em ~90 por banda (com a curva default = limitador de banda), 3 (off) em 14/20 nas
  low/high; pre gains até +18 dB (2000) em 9 slots da banda mid.
- Crossovers: 200 Hz / 3 kHz em ~95 %; LMH mix 1000 em 110, 294 (≈ 29 %) em 9; low cut 20 Hz em 74,
  desligado em 31.

---

## 2. Soundgoodizer (97 slots) **[DECOMP+DADOS]**

O DLL é o **motor do Maximus inteiro** com outro painel (`SynthPanel2`: `MixWheel` "Amount" 0…1000,
default 600; `PresetSel` A/B/C/D) **[STR]**.

- **Layout do `0xD5` (12 B):** `u32 versão (3)`, `i32 preset (0…3 = A…D)` → `P+0x334`,
  `i32 amount (0…1000)` → `P+0x338` (`FUN_00836960`) **[DECOMP]**.
- Ao carregar, `FUN_00836ac0` lê `Data/Presets/<a|b|c|d>.dat` (um estado de Maximus v4 de 1.333 B) e
  depois **força o parâmetro 5 (LMH mix) = amount** **[DECOMP]**. Ou seja: *amount* é a mistura
  processado/original de cada banda; a seção master continua 100 % ativa.
- Demos: preset A em 75, B 10, C 10, D 2; amount 600 (default) em 22, mediana 270, faixa 30…1000.

### 2.1 O que cada preset faz (lido dos `.dat`) **[DADOS]**

Todos: crossovers 200 Hz / 3 kHz (C: 200 Hz / 1,9 kHz), attack 2 ms, release 85,5 ms (low: 137 ms).

| Preset | Bandas (pre / post, curva) | Master |
|---|---|---|
| **A** | low +5,0/+5,6 dB, mid +6,4/0, high +6,9/+2,9; low/high em limitador 0 dB com saturação A 100 %; mid joelho −2,3 dB ≈ 2,6:1 | curva "4,4:1 acima de 0 dB", saturação A 20 %, release 2 |
| **B** | as três em *compression off*: só post +8,4 / +10,4 / +9,2 dB (+ saturação) | joelho −3,9 dB, ≈ 2,2:1, teto +2,5 dB, saturação A 30 % |
| **C** | pre +8,5 / +12,7 / +11,3 dB, post +1,5 / +2,8 / +2,9 dB, limitadores de banda | joelho −2,4 dB, ≈ 4,9:1, teto −0,2 dB |
| **D** | só a mid: pre +14,1 dB em "4,4:1 acima de 0 dB"; low e high **off** | brickwall −0,22 dB |

Resumo: é um "maximizador": empurra cada banda +5…+14 dB contra limitadores/compressores de banda,
mistura com o original pela *amount* e passa por um compressor/limitador master.

---

## 3. Fruity Fast Dist (53 slots) **[DECOMP+DADOS]**

Layout: **5 × i32 sem versão** (20 B) → `P+0x120`: `pre amp` (64…192), `threshold` (1…10), `type`
(0 = A, 1 = B), `mix` (0…128), `post` (0…128).

### 3.1 Ganhos (`FUN_006bb260`)

- `pre = L11(v/128)` (128 = 0 dB, 192 = **+11,0 dB**, 64 = −12,7 dB).
- `post = L11(v/128)`, `m = L11(mix/128)`.
- Coeficientes: **wet = m·post**, **dry = (post − wet)·pre·post** (o *post* aparece ao quadrado no termo
  seco — é assim mesmo no código) **[DECOMP: `0x6bb45e…0x6bb48d`]**.

### 3.2 DSP: tabela no host (`FLEngine`, slot `+0xC0` do `TFruityHost`) **[DECOMP]**

O render (`FUN_006bb7c0`) chama o host com (tipo, threshold, buffer, n, dry, wet, pre). O host
(`0x132ADC0`) escolhe uma tabela de 8.192 amostras `i16` por (tipo, threshold) e roda `0x6A2BF0`:

```
idx = |x|·pre·8191 (truncado, com interpolação linear da fração; acima de 8191 satura no fim da tabela)
y   = dry·x + wet·sign(x)·T[tipo][thr](idx)/32767
```

As tabelas são construídas uma vez (`0xB96CC0`), em dB, com `X = 20·log10(i/8191)`:

- **Tipo A** (k₁ = (11 − t)/10, k₂ = 1,6·(t − 1)): se `X ≥ −25`: `Y = −25 + k₁·(X + 25)`; depois, se
  `Y ≤ −k₂`, `Y += k₂`. É uma **compressão instantânea por amostra** (potência `x^k₁` no topo de 25 dB)
  com ganho de compensação k₂: t = 1 é linear; **t = 10 é quase onda quadrada** (k₁ = 0,1). Nível de
  saída a 0 dBFS de entrada: **−0,9·(t − 1) dB** (t = 10 → −8,1 dB).
- **Tipo B** (p = 0,8 + 0,02·(10 − t)): `Y = −(−X)^p`. Expande o nível de sinais baixos para cima
  (−20 dB → −11 dB em t = 10), mantém 0 dB em 0 dB. Mais suave e com mais "ruído levantado".
- Entrada acima de 0 dBFS (após *pre*) fica presa no último valor → **clipping duro** nesse nível.

Curvas (saída linear para entrada 0,01 / 0,05 / 0,1 / 0,25 / 0,5 / 1):

| t | Tipo A | Tipo B |
|---|---|---|
| 1 | 0,010 0,050 0,100 0,250 0,500 1,000 | 0,014 0,060 0,114 0,267 0,512 1,0 |
| 5 | 0,021 0,104 0,166 0,288 0,436 0,661 | 0,041 0,115 0,181 0,339 0,560 1,0 |
| 10 | 0,052 0,262 0,313 0,343 0,367 0,394 | 0,111 0,210 0,282 0,431 0,616 1,0 |

Ajuste por mínimos quadrados para `tanh(D·x)/tanh(D)` (forma normalizada): tipo A t = 4…10 →
D = 2,2 / 4,5 / 6,8 / 9,2 / 12,8 / 17,8 / **22,8 dB**; t ≤ 3 → 0 dB. Tipo B t = 6…10 → 0,8…4,2 dB.

### 3.3 Demos **[DADOS]**

pre 128 (38) / **192 (13)**; threshold **10 em 51/53**; tipo A em 50; mix 128 em 24 (o resto 29…92);
post 128 (41) / 97 (9, = −5,8 dB).

---

## 4. Fruity Filter (32 slots) **[DECOMP+DADOS]**

Layout: `u32 versão (1/2)` + **6 × i32** (`P+0x120`: cutoff, reso, LP, BP, HP, x2) + **u8 "Remove DC
offset"** só na v2 (`FUN_006cb920`; ausente = desligado) **[DECOMP]**.

- **Filtro de estado variável (Chamberlin)** por canal (`FUN_006cc3b0`):
  `low += f·band; high = in − low − q·band; band += f·high; out = gLP·low + gBP·band + gHP·high`.
- **Cutoff:** `f = L26(v/1024)` (`(26^x − 1)·0,04`) **[DECOMP]**, coeficiente direto do SVF, *sem*
  correção de taxa de amostragem: `fc = fs/π · asin(f/2)`. Em 44,1 kHz: v = 134 → 149 Hz, 331 → 524 Hz,
  512 → 1,15 kHz, 768 → 2,98 kHz, **1024 → 7,35 kHz (fs/6)**; em 48 kHz tudo escala por 48/44,1.
  Ou seja, "totalmente aberto" ainda é um passa-baixas em ~7–8 kHz.
- **Resonance:** amortecimento `q = 1 − 0,8·(v/1024)` → **Q = 1/q = 1…5** (default 205 → Q 1,19).
- **Saídas LP/BP/HP:** ganho de cada uma `L11(v/1024)` (1024 = 1,0; 512 = 0,232); somadas.
- **x2:** um segundo SVF idêntico em série, mesmo `f`, **amortecimento fixo 1 (Q = 1)**, alimentado pela
  mistura do primeiro e misturado de novo com os mesmos ganhos.
- Os coeficientes andam em rampa de 128 amostras (`FUN_006cc2e0`); o remove-DC é uma rotina do runtime.
- Demos: LP 1024 sozinho em 25, HP 1024 sozinho em 7, BP nunca; cutoff 1024 em 15 e 0 em 8 (início de
  automação), resto 134…372; reso 0 em 23; x2 em 2; remove DC ligado em 10/12 dos v2.

---

## 5. Fruity Flangus (56 slots) **[DECOMP; período exato do LFO HIP]**

Layout: **8 × i32 sem versão** (32 B) → `P+0x120`: order, depth, speed, delay, spread, cross, dry, wet.
`FUN_006b6ec0` passa ao motor: order (inteiro), depth/1024, speed/1024, delay/128, spread/128,
cross/128; `FUN_006b3ef0` calcula as vozes. Taxa de referência 44,1 kHz, reescalada por `44100/fs`.

- **Vozes:** `2·order` vozes; as `order` primeiras são do canal esquerdo, as outras do direito com LFO
  defasado 180°. A voz *i* tem peso `k = 1 − i/order` (1, …, 1/order).
- **Depth:** `D = L11(depth) · 1092,5` amostras = **24,8 ms·L11(depth/1024)** (512 → 5,7 ms;
  1024 → 24,8 ms). Profundidade da voz `Dv = D·k + 10` amostras.
- **Delay (base):** `B = L11(delay/128) · 100 ms` (8 → 1,6 ms; 128 → 100 ms), mínimo 8 amostras; atraso
  central da voz = `Dv + B`.
- **Speed:** `P = 75000·(101^(1 − speed/1024) − 1) + 50000`; incremento de fase `500·2³¹/P/Dv` por amostra
  → **f ≈ 250·44100 / (P·Dv)** Hz (com `Dv` em amostras) **[DECOMP para a fórmula; HIP: ciclo = 2³²]**.
  A velocidade do LFO é inversamente proporcional à profundidade: o knob controla a *velocidade de
  varredura*. Defaults (depth 512): speed 768 → **0,20 Hz**, 892 → 0,38 Hz, 632 → 0,10 Hz, 1024 → 0,84 Hz.
- **Spread:** `w = L1001(spread/128)` (`(1001^x − 1)/1000`); mistura entre taxas/atrasos iguais (w = 0)
  e taxas aleatorizadas por voz com uma tabela de 20 fatores ≈ 1 ± 0,07 (w = 1, default).
- **Cross:** cada voz entra no próprio canal com ganho 1 e no oposto com `cross·k` (com sinal); depois
  todas são normalizadas por `1 − L1001(1 − 1/Σ|g|)`.
- **Dry / wet:** `sign(v)·L11(|v|/128)` (negativo inverte fase). **Não há realimentação.**
- Demos: order 4 (36), 2 (9); depth 512 (36), 86 (9, ≈ 0,7 ms); speed 768/892/632; delay 8 (38) / 0 (14);
  spread 128 (41) / 0 (10); cross −32 (35); dry 128 (32) / 96 (13); wet 96/48/70/36/128.

---

## 6. Fruity Soft Clipper (28 slots) **[DECOMP+DADOS]** — confirmação

- `0xD5` = 2 × i32: threshold (1…127) e post (0…160). Threshold `t = v/128` linear; post `L11(v/128)`
  (confirmado: `FUN_006bee60` e `FUN_005dfd00`, teto fixo 1,0).
- **Curva** (`FUN_005dfd40` / variante SSE `FUN_005e1ad0`): abaixo de `t` é linear; acima,
  `|y| = t + (1 − t)·(1 − exp(−(|x| − t)/(1 − t)))`, sinal preservado. Joelho exponencial com derivada 1 em
  `t` e assíntota em **1,0 (0 dBFS)**. O post gain é aplicado depois.
- Ajuste a `tanh` (entradas até +6 dB): o drive equivalente é ~0 dB e o nível cai 1,3…3,8 dB
  (t = 127 → −1,3 dB, 100 → −1,5, 64 → −2,0, 1 → −3,8).
- Demos: threshold 100 (16), 1 (5); post 128 (20), 132…150.

---

## 7. Transient Processor (47 slots) e Delay 3

### 7.1 Transient Processor **[DECOMP texto + STR + DADOS; DSP no `engine_x64.dll` não lido]**

Layout (49 B): `u32 versão (1)` + 9 × i32 (parâmetros 0…8) + `i32` (5/7) + `i32` (12800) + `u8`
**[HIP: opções de menu]**.

| i | Nome | Faixa | Default | Texto (`FUN_006edf70`) |
|---|---|---|---|---|
| 0 | Attack | 0…160 | 80 | `(v − 80)/80` → −100…+100 % |
| 1 | Drive | 0…160 | 0 | `v/160` % |
| 2 | Release | 0…160 | 80 | `(v − 80)/80` |
| 3 | Gain | 0…160 | 80 | `(v/160 − 0,5)·18` dB (±9 dB) |
| 4/5 | Attack / release shape | 0…2 | 1 | Sharp / Mid / Slow |
| 6 | Split frequency | 0…160 | 80 | `5 + 995·(v/160)²` Hz (default 253,8 Hz) |
| 7 | Split balance | 0…160 | 80 | dry = `min(1, 2(1−n))`, wet = `min(1, 2n)`, n = v/160 |
| 8 | Effect | 0/1 | 1 | Off / On |

Demos: attack 160 (+100 %) em 8, 80 em 6; release mediano ~60 (−25 %); gain 80; split 80 em 32.

### 7.2 Fruity Delay 3 — sem avanço

O texto e o DSP estão no `engine_x64.dll` (C++, sem strings de unidade). A estatística dos 27 slots
sem sync (tempo 22, 1, 0, 91…550) não bastou para fixar a unidade. Continua pendente (rodada 7, 4.1).

---

## Recomendações para o LYNX

Fórmulas internas atuais (de `src/core/Effects.cpp`): distorção `drive = v·36 dB` (tanh com
compensação `1/tanh(max(drive,1))`), `tone = 800·25^v Hz` (LP), mix, `saída = (v − 0,5)·24 dB`;
chorus `rate = 0,05·100^v Hz`, `depth = v·8 ms` (varredura sobre base fixa de 12 ms), mix
(`saída = in + (atrasado − in)·mix·0,5`), `feedback = v·0,7`; filtro tipo LP/HP/BP (`floor(3v)`),
`cutoff = 20·1000^v Hz`, `Q = 0,5·24^v`, mix; compressor `thr = −60+60v`, `ratio = 1+19v`,
`attack = 0,1·1000^v ms`, `release = 10·100^v ms`, `makeup = 24v dB`; limiter `teto = −12+12v`,
`ganho = 24v dB`, `release = 10·100^v ms`. Inversas úteis: `v = ln(x/lo)/ln(hi/lo)`.

1. **Maximus → Limiter (+ Compressor quando a curva master tiver joelho).** A seção **master**
   (banda 3) representa o efeito global; as bandas são multibanda e não têm equivalente.
   - Master modo 3 → ignorar o slot. Modo 1 → só ganho: limiter com teto 0 dB (v = 1) e
     `ganho = pre_dB + post_dB` (se negativo, levar ao volume da pista).
   - Modo 0: calcular a curva master como em 1.4 (pontos cumulativos, interpolação linear em c; a tensão
     pode ser ignorada numa primeira versão) e extrair: **teto** = maior nível de saída em dB (inclui
     −0,22 dB da escala 0,975 e, se houver saturação, `L7(satCeil)`), **joelho** = primeiro nível em que
     saída < entrada − 0,5 dB, **ratio** = Δentrada/Δsaída entre o joelho e +12 dB.
   - Limiter: `teto v = (min(teto_dB + post_dB, 0) + 12)/12`; `ganho v = pre_dB/24` (pre do master
     `20·log10 L7(v/1000)`, 0…+18 dB cabe); `release v = ln(ms/10)/ln(100)` com
     `ms = 4·(501^(rel/10000) − 1)`. Curva default → teto −0,22 dB (v = 0,982).
   - Se joelho < −0,5 dB e ratio finita: Compressor antes do limiter com `thr v = (joelho + 60)/60`,
     `ratio v = (r − 1)/19`, `attack v = ln(ms/0,1)/ln(1000)` com `ms = 2·(501^(att/10000) − 1)`
     (default 2 ms → v = 0,43), release como acima, makeup 0.
   - Bandas com modo 0/1 e ganho ≠ 0 dB: somar ao ganho do limiter o ganho médio
     `mix·(pre + post)` das bandas não-off **[HIP, aproximação grosseira]**; low cut ligado → opcional
     HP 20 Hz (LYNX não tem no limiter).
2. **Soundgoodizer → Compressor + Limiter**, com aproximação por preset e `a = amount/1000`
   **[HIP]**: `ratio_eff = 1/(1 − a·(1 − 1/r_banda))`, `makeup = a·(pre + post)` médios das bandas.
   Para a = 0,6:

   | Preset | thr (dB) | ratio | makeup (dB) | Limiter depois |
   |---|---|---|---|---|
   | A | −6 | 2,5 | +5,3 | teto 0 dB (curva master 4,4:1 acima de 0 dB → usar ratio 4 com thr 0 se houver 2º compressor) |
   | B | −10,5 | 2,2 | +6,6 | teto 0 dB |
   | C | −10,8 | 2,5 | +7,9 | teto −0,2 dB |
   | D | −14 | 1,9 | +8,5 | teto −0,2 dB |

   attack 2 ms (v = 0,43), release 85 ms (v = 0,47). Em `v`: `thr v = (thr+60)/60`, `ratio v = (r−1)/19`,
   `makeup v = dB/24`. O mapeamento atual (`Soundgoodizer → compressor`) pode ficar, só preenchendo esses
   valores; ideal seria encadear um limiter (−0,2 dB).
3. **Fast Dist → Distorção.**
   - `drive v = (20·log10 L11(pre/128) + D(t, tipo)) / 36`, com D da tabela 3.2 (t = 10 tipo A →
     22,8 dB; pre 192 → +11 dB → v = 0,94). Tom: **v = 1** (o Fast Dist não filtra).
   - Mix/saída: com `wet = m·post`, `dry = (post − wet)·pre·post` e `S = 10^(−0,9(t−1)/20)` (tipo A; tipo B
     S = 1): `G = wet·S + dry`, `mix v = wet·S/G`, `saída v = 0,5 + 20·log10(G)/24`. Default (mix 128,
     post 128, t = 10): mix 1, saída −8,1 dB → **v = 0,16**.
4. **Fruity Filter → Filtro.** Tipo = maior entre os ganhos `L11(LP/1024)`, `L11(BP/1024)`, `L11(HP/1024)`
   (LP → 0, HP → 1/3, BP → 2/3 do `floor(3v)`); se LP e HP forem ambos ≥ 0,5, é um notch/all-pass que
   o LYNX não tem → manter LP. `cutoff Hz = fs/π·asin(L26(v/1024)/2)` → `v = ln(Hz/20)/ln(1000)`
   (1024 → 7,35 kHz → v = 0,855; **não** forçar 1,0 como o código atual faz — o FL abafa mesmo aberto).
   `Q = 1/(1 − 0,8·reso/1024)` → `v = ln(Q/0,5)/ln 24` (reso 0 → Q 1 → v = 0,218). x2 → subir o Q
   ×1,3 **[HIP]** ou cascatear dois biquads se o LYNX ganhar ordem. Mix = 1.
5. **Flangus → Chorus.** `rate v = ln(f/0,05)/ln(100)` com `f` da seção 5 (defaults → 0,20 Hz →
   v = 0,30); `depth v = min(1, 2·D_ms/8)` **[HIP: varredura = 2·Dv]** (default 5,7 ms → 1,0);
   `feedback v = 0`; mix: `r = |wet_g|/|dry_g|` (ganhos `L11(|v|/128)`), `mix v = min(1, 2r/(1+r))`
   (dry 128 / wet 96 → r = 0,50 → v = 0,67; dry 0 → 1). O atraso base do Flangus (1,6 ms default) é
   bem menor que os 12 ms fixos do LYNX — se o chorus ganhar um parâmetro "base", usar `B + Dv`.
6. **Soft Clipper → Distorção:** `drive v = 0` (não 0,15 como hoje), tom 1, mix 1,
   `saída v = 0,5 + (20·log10 L11(post/128) − c(t))/24`, com `c` ≈ 1,5 dB (t ≥ 100), 2,0 (t ≈ 64),
   3,8 (t = 1). Melhor ainda: implementar a curva exata de 6 como modo "soft clip" da distorção
   (o `SoftClip.h` do LYNX já existe; parametrizar o joelho por `t`).
7. **Transient Processor:** sem efeito interno equivalente; ignorar com aviso, ou aplicar só
   `Gain` (±9 dB) no volume da pista.

---

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| Maximus `FUN_0083f8f0` (VMT+3) | `SaveRestoreState`: cabeçalho, 59 + 59 i32, bloco de UI, extras v14/v15, 8 envelopes | DECOMP |
| Maximus `FUN_00842440`, `FUN_00842480` | Índice → (banda, controle); `ProcessParam` (L7 para pre/post) | DECOMP |
| Maximus `FUN_00842d70` | Texto: Hz (`30·533,33^x`, `5·16^x`), ms (`T`, `2T`), saturação A/B, "Default" | DECOMP |
| Maximus `FUN_00852530`, `FUN_0062ddf0` | Constantes ln 533,33, ln 16, ln 2, ln 3, ln 7, ln 11, ln 601 | DECOMP |
| Maximus `FUN_00781bd0` | Serialização do envelope (Δx, y, tensão, flags, marcadores) | DECOMP |
| Maximus `FUN_00846490` | Envelope → tabela de ganho (`ℓ = (3^(2c)−1)/2`, escala 0,975/1,0) | DECOMP |
| Maximus render `0x845300…0x845383` | Mistura LMH por banda (mix·processada + (1−mix)·original) | DECOMP |
| Maximus DFM `SynthPanel` | Tags 0…19, faixas, defaults, listas (modo, slope, peak/RMS) | STR |
| Soundgoodizer `FUN_00836960`, `FUN_00836ac0` | Layout 12 B; carrega `Presets/*.dat` e força LMH mix = amount | DECOMP |
| Soundgoodizer `Presets/{a,b,c,d}.dat` | Estados Maximus v4 dos presets | DADOS |
| Fast Dist `FUN_006bb260`, `FUN_006bb7c0` | L11 de pre/mix/post; dry/wet; chamada ao host | DECOMP |
| `FLEngine` `0x132ADC0`, `0x6A2BF0`, `0xB96CC0` | Seleção de tabela, waveshaper interpolado, construção das curvas A/B | DECOMP |
| Filter `FUN_006cb920`, `FUN_006cbf10`, `FUN_006cc3b0` | Layout v1/v2; L26 do cutoff, Q, ganhos L11; SVF ×1/×2 | DECOMP |
| Flangus `FUN_006b6ec0`, `FUN_006b3ef0` (+ `0x6b3e30`, `0x6b3eb0`) | Escalas; vozes, profundidade, base, velocidade, spread, cross | DECOMP |
| Soft Clipper `FUN_006bee60`, `FUN_005dfd00`, `FUN_005dfd40`, `FUN_005e1ad0` | t = v/128, teto 1; curva exponencial | DECOMP |
| Transient Processor `FUN_006edf70`, `FUN_006edee0` | Unidades de texto; dry/wet do split | DECOMP |
| 165 `.flp` (seção do mixer) | Contagens, versões, curvas, valores típicos | DADOS |

### Pendências

- Maximus: forma exata da tensão nos segmentos do envelope (`FUN_00788a20`), o papel do segundo
  envelope por banda, saturação A/B, campo extra de v11–13.
- Flangus: confirmar o ciclo do LFO (2³² vs 2³¹) e a forma da onda (triangular **[HIP]**).
- Delay 3 (unidade do tempo sem sync, leis de feedback/cutoff) e DSP do Transient Processor: ambos no
  `engine_x64.dll` C++, ainda não abertos.
