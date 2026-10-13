# Análise comportamental do FL Studio 2026: rodada 7 (REA) — estado salvo dos efeitos nativos do mixer

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md` e das rodadas 2–5 (a rodada 6,
> feita em paralelo, trata do EQ/LFO da pista do mixer no FLEngine e não é tocada aqui). Engenharia reversa
> *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está
> aqui são descrições, layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`),
> offsets e globais servem só para rastrear a evidência.

- **Alvos** (todos em `Plugins/Fruity/Effects/<nome>/`, Delphi, Ghidra 12.1.4 via REA):

  | Plugin | DLL (SHA-256) | VMT do plugin | `SaveRestoreState` | Texto do parâmetro |
  |---|---|---|---|---|
  | Fruity Parametric EQ 2 | `871b8396…54f4` | `0x6ebb68` | `FUN_006ec340` | `FUN_006efda0` |
  | Fruity Limiter | `ed81447b…b082` | `0x6d30c0` | `FUN_006d3760` | `FUN_006d5380` |
  | Fruity Reeverb 2 | `bc24b17d…97a1` | `0x6d4338` | `FUN_006d4920` | `FUN_006d5480` |
  | Fruity Delay 3 | `b9d64b09…fbd1` (+ `engine_x64.dll` `0b05f78f…8891`) | `0x6e87d0` | `FUN_006e8da0` | delegado ao motor C++ |
  | Fruity Delay 2 | `d9a7674d…f93d` | `0x6bf030` | `FUN_006bfa00` | `FUN_006c0490` |
  | Fruity Compressor | `f4021492…81d1` | `0x6bd5f8` | `FUN_006bd9a0` | `FUN_006be220` |
  | Fruity Soft Clipper | `50aac728…72c6` | `0x6be3f0` | — | `ProcessParam` `FUN_006bee60` |

  A VMT segue a mesma ordem da rodada 4 (entrada 3 = `SaveRestoreState`, 4 = `GetName` → texto do
  parâmetro, 6 = `ProcessParam`). Os formulários DFM (`TSynthEditorForm`) foram lidos por um parser
  próprio de `TPF0`: o `Tag` de cada controle é o índice do parâmetro, `Min/Max/DefaultValue` a faixa.
- **Validação de formato:** 165 `.flp` de `FL Studio 2026/Data`, seção do mixer (depois do 1º `0xEC`):
  `0xC9` (nome) → `0xD5` (blob) → `0x62` (fecha o slot). Números reais de slots: **PEQ2 1.138**
  (1.117 com nome UTF-16 + 21 com nome **em ASCII** num `.flp` antigo — o importador precisa aceitar os dois),
  **Limiter 346**, **Reeverb 2 333**, PanOMatic 193, Delay 3 110, Compressor 88, Stereo Shaper 77,
  Delay 2 62, Balance 57, Flangus 56, Fast Dist 53, Filter 32, Soft Clipper 28.
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenções: tudo little-endian; `i32` com sinal; `P+0xNN` = offset no objeto do plugin; "parâmetro *i*" = índice
do host (= `Tag` do controle no DFM). Padrão comum a quase todos: **o blob é uma cópia crua do vetor de
parâmetros inteiros do plugin** (`P+0x120…`), às vezes precedido de uma versão; o valor salvo é o mesmo
inteiro que o knob mostra internamente (faixa do DFM), e a conversão para unidade real é feita depois.

### Leis de curva compartilhadas **[DECOMP]**

Todos os plugins Fruity estáticos têm a mesma unidade de inicialização (`FUN_005f41c0` no PEQ2,
`FUN_005e22b0` no Limiter) que pré-calcula `ln 2`, `1/ln 2`, `ln 2 / 2`, `ln 601`, `ln 11`, `ln 3`, `ln 7`,
`ln 10 / 20`, `ln 2 / 12`. Daí saem as leis "exponenciais normalizadas" que aparecem abaixo:

```
Lk(x) = (k^x − 1) / (k − 1)        x = valor/escala, Lk(0) = 0, Lk(1) = 1
```

- `L11` ("volume do FL"): ganho linear = `(11^x − 1)/10`. Usado em Reeverb 2 (dry/ER/wet), Soft Clipper
  (post gain); provavelmente também Delay 2/Balance/PanOMatic **[HIP]**.
- `L7`: ganho = `(7^x − 1)/6` (Limiter: gain, comp threshold, noise gain/threshold).
- `L3`: ganho = `(3^x − 1)/2` (Limiter: ceiling, saturation).
- Tempo `T(x) = 2·(501^x − 1)` ms (Limiter).

---

## 1. Fruity Parametric EQ 2 (1.138 slots) **[DECOMP+STR+DADOS]**

### 1.1 Layout do `0xD5`

| Off | Tipo | Campo | Versões |
|---|---|---|---|
| 0x000 | u32 | **versão** (grava 8; demos: 8 ×490, 3 ×328, 6 ×199, 7 ×100, 2 ×21) | todas |
| 0x004 | 36 × i32 | **parâmetros 0…35** (estado atual, tabela 1.2) → `P+0x120` | todas |
| 0x094 | 36 × i32 | **estado reserva (A/B compare)**, mesmo layout; ignorar | todas |
| 0x124 | u32 | flags de UI: bit 0 → `P+0x361`, bits 1+ → `P+0x360` | todas |
| 0x128 | u32 | `P+0x364` (0 ou 1) | todas |
| 0x12C | u8 | `P+0x368` (checkbox de UI) | todas |
| 0x12D | u32 | **Monitor** (0 off, 1 input, 2 output; sempre 2) | todas |
| 0x131 | u32 | bit 0 → `P+0x2078` | v ≥ 3 |
| 0x135 | u32 | `P+0x36C` (2 quando ausente) | v ≥ 4 |
| 0x139 | u32 | `P+0x374` (0) | v ≥ 4 |
| 0x13D | u8 | bits: 0 → `P+0x370` (**linear phase** **[HIP]**), 1 → `P+0x37C` = **HQ / oversampling ×2** (a taxa do analisador é multiplicada por `1+bit`), 2…7 → opções de visualização | v ≥ 4 |
| 0x13E | u32 | **máscara de bandas ligadas** (bit *b* = banda *b+1*; padrão `0x7F`); é o botão "Mute / solo" de cada banda | v ≥ 4 |
| 0x142 | u32 | `P+0x212C` (1) | v ≥ 4 |
| 0x146 | u32 | `P+0x20EC` (0) | v ≥ 5 |
| 0x14A | 5 × u32 | opções de visualização (`P+0x2100, 0x2104, 0x2108, 0x20FC, 0x2110`; demos `3,1,2,2,2`) | v ≥ 6 |
| 0x15E | u32 | `P+0x380` (0) | v ≥ 7 |

Tamanhos: v2 = 305, v3 = 309, v6 = 350, v7/v8 = 354 B — batem exatamente com a soma acima nos 1.138 blobs.
Para v < 4 todas as bandas contam como ligadas. A leitura dos 36 parâmetros **não depende da versão**.

### 1.2 Os 36 parâmetros

| i | Off | Campo | Faixa (DFM) | Default (DFM) | Unidade |
|---|---|---|---|---|---|
| 0…6 | 0x04…0x1C | **ganho** da banda 1…7 | −1800…1800 | 0 | `dB = v/100` (±18 dB) |
| 7…13 | 0x20…0x38 | **frequência** da banda 1…7 | 0…65536 | 5777, 13024, 19904, 33145, 42327, 49307, 55825 | `Hz = 20·1000^(v/65536)` |
| 14…20 | 0x3C…0x54 | **largura** (BW) | 0…65536 | 40124 (b1, b7), 25412 (b2…b6) | `w = v/65536` (texto: `w·100 %`) |
| 21…27 | 0x58…0x70 | **tipo** | 0…7 | 5, 6, 6, 6, 6, 6, 7 | tabela 1.3 |
| 28…34 | 0x74…0x8C | **ordem / inclinação** | −3…+3 | 0 | tabela 1.4 |
| 35 | 0x90 | **Main level** | −1800…1800 | 0 | `dB = v/100` |

- Frequência: `FUN_006fbf60` define o passo `log2(1000)/65536`, `FUN_006f49c0`/`FUN_006fbf20` fazem a ida
  `v = log2(Hz/20) / passo`, e `FUN_006ef120` a volta `Hz = 20·exp(v·passo·ln 2)`. Faixa **20 Hz…20 kHz
  logarítmica**. Defaults do DFM: 36,8 Hz, 125 Hz, 397 Hz, 658 Hz, 1,87 kHz, 3,57 kHz, 7,19 kHz.
  Muitos demos antigos usam a grade uniforme 10923…54613 (= 63 Hz, 200 Hz, 632 Hz, 2 kHz, 6,3 kHz…).
- Ganho: texto com 2 casas + "dB" (`×0,01`, constante `0x3C23D70A`) **[DECOMP]**.

### 1.3 Tipos **[DECOMP+STR]**

Tabela de nomes em `0x74E858`; o tipo da UI é remapeado para um tipo interno (`0x74E838`):

| UI | Nome | Interno | Usa ganho? | Largura → parâmetro do biquad |
|---|---|---|---|---|
| 0 | Disabled | 0 | — | banda ignorada |
| 1 | Low pass (= "high cut") | 1 | não | `Q = 0,1 + 9,9·(1 − w)³` |
| 2 | Band pass | 2 | não | `BW_oct = (0,1 + 4,9·w)·k_bp` |
| 3 | High pass (= "low cut") | 4 | não | `Q = 0,1 + 9,9·(1 − w)³` |
| 4 | Notch | 3 | não | `BW_oct = (0,1 + 4,9·w)·k_notch` |
| 5 | Low shelf | 6 | sim | `S = 1/3 + 1,7193·(1 − w)` |
| 6 | Peaking | 7 | sim | `BW_oct = 0,1 + 4,9·w` |
| 7 | High shelf | 8 | sim | `S = 1/3 + 1,7193·(1 − w)` |

### 1.4 Ordem (inclinação) **[DECOMP+STR]**

Texto (`0x74E8B0`, indexado de −3 a +3): `Steep 8, Steep 6, Steep 4, 2, Gentle 4, Gentle 6, Gentle 8`.
O número é a ordem do filtro (2 = um biquad, 12 dB/oit). Com `n = |ordem|`, a banda usa **n+1 biquads em
cascata**; ordem negativa liga o modo *steep* (`FUN_006ef120`: `P+0x384+0x1F8·banda`).

- **Gentle** (n > 0): LP/HP usam `Q^(1/(n+1))` em cada estágio (`FUN_005d1aa0` = potência); peaking e shelf
  dividem o ganho: cada estágio recebe `ganho/(n+1)`.
- **Steep** (n < 0): LP/HP multiplicam o Q por uma tabela tipo Butterworth por estágio (`0x74E758`; n=1:
  1,983/0,749; n=2: 2,643/1,082/0,694; n=3: 3,576/1,142/1,045/0,655); shelves usam outra tabela (`0x74E718`);
  peaking espalha os estágios em frequência (deslocamentos em oitavas `0x74E798`, ex. n=3: −0,775, −0,3,
  +0,3, +0,775 × BW) com ganho corrigido (`0x74E7D8`). Detalhe só necessário para fidelidade fina.
- Band pass / notch: `k_bp = 0,708·√(n+1)` e `k_notch = 1/k_bp` (`P+0x1EC/0x1F0`, derivados de
  `10^(3/20)/10^(6/20)`) **[DECOMP]**.

### 1.5 Matemática do filtro (`FUN_006ee6e0`) **[DECOMP]**

São os **biquads do "Audio EQ Cookbook" (RBJ)**, com `ω = 2π·f/fs` (limitado a ≈ 0,999π; 0,7π para
peaking normal; 0,8π para os estágios do peaking steep):

- LP/HP: `α = sin ω /(2Q)`.
- BP/notch/peaking: forma de largura em oitavas, `α = sin ω · sinh( (ln 2 / 2)·BW_oct·ω / sin ω )`.
- Peaking/shelf: `A = 10^(ganho_dB/40)` (`0,5·ln10/20` no expoente).
- Shelf: `α = (sin ω / 2)·√( (A + 1/A)(1/S − 1) + 2 )`; com `w` do default (40124 → 0,612) dá **S = 1,0**.
- Peaking/shelf com ganho exatamente 0: a banda conta 0,2 s e depois é ignorada (bypass).
- Main level: ganho de saída em dB (`v/100`) **[HIP: aplicado como 10^(dB/20) no fim]**.
- Há um caminho alternativo "linear phase" (`FUN_006f1c60`/`FUN_006f1c80`, FFT) usado quando o flag está
  ligado; a resposta em magnitude é a mesma **[HIP]**.

Equivalências úteis para o LYNX (que trabalha com Q): peaking `Q ≈ 1 / (2·sinh(ln2/2 · BW_oct))`
(default 2 oit → Q 0,667; w = 0 → 0,1 oit → Q 14,4; w = 1 → 5 oit → Q 0,18). Shelf: `1/Q = √((A+1/A)(1/S−1)+2)`.

### 1.6 Uso nos demos **[DADOS]**

- Bandas efetivamente ativas (tipo ≠ 0, bit da máscara ligado, e ganho ≠ 0 para peaking/shelf): **Peaking
  2.026, High pass 473, High shelf 458, Low shelf 372, Low pass 256**, Notch 4, Band pass 1.
- Bandas ativas por instância: 0 → 40, 1 → 156, 2 → 353, 3 → 169, 4 → 142, 5 → 104, 6 → 85, 7 → 89.
- Ordem: 0 em 92%; **−3 ("Steep 8") 537 vezes** (quase sempre HP na banda 1 e LP na banda 7), −1 40, +3 28.
- Frequências medianas: HP 152 Hz (20…3,98 kHz), LP 16,1 kHz, low shelf 126 Hz, high shelf 6,6 kHz,
  peaking 1,05 kHz. Ganhos de −18 a +18 dB; main level 0 em 988/1.138 (faixa −15,3…+18 dB).
- Máscara ≠ 0x7F em 16 instâncias (bandas mutadas que **não** devem soar).

---

## 2. Fruity Limiter (346 slots) **[DECOMP+STR+DADOS]**

### 2.1 Layout

| Off | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | versão (7 ×190, 6 ×156; grava 7) |
| 0x04 | 18 × i32 | **parâmetros 0…17** (tabela 2.2) |
| 0x4C | 18 × i32 | estado reserva (A/B); ignorar |
| 0x94 | i32 | velocidade de rolagem do visual (50) |
| 0x98 | i32 | **aba visível** do painel (0/1: LIMIT/COMP) **[HIP: só UI; as três seções processam sempre]** |
| 0x9C | 4 × u8 | botões do visual (input/output/envelopes/marcadores) |
| 0xA0 | i32 | `P+0x1BC` (0) |
| 0xA4 | i32 | **entrada de sidechain** (0…3) |
| 0xA8 | u8 | só v7: bit 0 → `P+0x277` **[HIP: opção de menu]** |

v ≥ 6 lê 0xA4 bytes após a versão; v5 lia 0x9D (não aparece nos demos).

### 2.2 Parâmetros e conversões (`FUN_006d5380`)

| i | Off | Nome (DFM) | Faixa | Default | Conversão |
|---|---|---|---|---|---|
| 0 | 0x04 | Gain | 0…2000 | 1000 | `L7(v/1000)` linear → dB (0 = −∞, 1000 = 0 dB, 2000 = ×8 = **+18,1 dB**) |
| 1 | 0x08 | Soft saturation threshold | 0…1000 | 1000 | `L3(v/1000)` (1000 = 0 dB) |
| 2 | 0x0C | Limiter **ceiling** | 0…2000 | 1000 | `L3(v/1000)` (1000 = 0 dB, 2000 = ×4 = **+12 dB**) |
| 3 | 0x10 | Limiter attack | 0…10000 | 1115 | `T(v/10000)` ms (0…1000; default **2,0 ms**) |
| 4 | 0x14 | Limiter attack curve | 1…8 | 3 | inteiro (tensão da curva) |
| 5 | 0x18 | Limiter release | 0…10000 | 5000 | `2·T(v/10000)` ms (0…2000; default **85,5 ms**) |
| 6 | 0x1C | Limiter release curve | 1…8 | 3 | inteiro |
| 7 | 0x20 | Limiter peak window ("SUSTAIN") | 0…10000 | 2882 | `T` ms (default **10,0 ms**) |
| 8 | 0x24 | Comp threshold | 0…1000 | 1000 | `L7(v/1000)` (1000 = 0 dB; piso fixo) |
| 9 | 0x28 | Comp **ratio** | −1000…1000 | 0 | v > 0: `r = 1 + 0,38·(51^(v/1000) − 1)` → "r:1" (até **20:1**); v < 0: `r = 1 + 0,06·(51^(|v|/1000) − 1)` → "1:r" (até 1:4, expansão); 0 = 1:1 |
| 10 | 0x2C | Comp knee | −1000…1000 | 0 | `v/10` % |
| 11 | 0x30 | Comp attack | 0…10000 | 0 | `T` ms |
| 12 | 0x34 | Comp release | 0…10000 | 6666 | `2·T` ms (default **248 ms**) |
| 13 | 0x38 | Comp curve | 1…8 | 1 | inteiro |
| 14 | 0x3C | Comp RMS window | 0…10000 | 650 | `T` ms (default **1,0 ms**) |
| 15 | 0x40 | Noise gate gain | 0…1000 | 1000 | `L7` |
| 16 | 0x44 | Noise gate threshold | 0…1000 | 100 | `L7` (0 = gate desligado) |
| 17 | 0x48 | Noise gate release | 0…10000 | 4187 | `2·T` ms (default **50 ms**) |

`T(x) = 2·(501^x − 1)` ms (`FUN_006d3dc0`, constante 501,0); ganhos são exibidos em dB por
`20·log10` (`FUN_005e0ce0`); "ms"/"dB"/"%"/"1:"/":1" são as strings em `0x6D594C…`.

### 2.3 Uso nos demos **[DADOS]**

- Parâmetros alterados do default: noise threshold 346 (todos 0 = gate off), comp threshold 222,
  ratio 200, ceiling 159, attack 145, gain 136, release do comp 93.
- Ceiling 2000 (+12 dB, limitador praticamente desligado) em 85; gain 2000 (+18 dB) em 12.
- Ratio 1000 (20:1) em 73; comp threshold típico 64…280 (≈ −33…−18 dB). Knee e curvas quase sempre default.
- Sidechain (0xA4) ≠ 0 em ~150 instâncias (131 com entrada 1, 9 com 2, 10 com 3) — compressão com "ducking";
  o LYNX não tem sidechain, então esses slots soarão diferentes de qualquer jeito.

---

## 3. Fruity Reeverb 2 (333 slots) **[DECOMP+STR+DADOS]**

### 3.1 Layout

| Off | Tipo | Campo |
|---|---|---|
| 0x00 | u32 | versão: **10001** (271 slots, 66 B) ou **10000** (62 slots, 58 B); < 10000 = formato antigo sem versão (lê 0x35 B do início) |
| 0x04 | 15 × i32 (v10001) / 13 × i32 (v10000) | parâmetros 0…14 / 0…12 |
| após | u8 | **tempo-based predelay** (`P+0x15C`) |
| após | u8 | **Mid/side input** (`P+0x15D`) |

Para v10000, Mod Speed = 684 e Mod Depth = 0.

### 3.2 Parâmetros (`FUN_006d5480`, `FUN_006d5160`)

| i | Nome | Faixa | Default | Conversão |
|---|---|---|---|---|
| 0 | Low cut | 19…3000 | 75 | Hz; **< 20 = Off** |
| 1 | High cut | 5…221 | 40 | `v/10` kHz (0,5…22 kHz); **≥ 221 = Off** |
| 2 | Predelay | 0…384 | 0 | sem sync: `v·1000/384` ms (0…1000 ms); com sync: `v/48` tempos (48 ticks/tempo, até 8 tempos) |
| 3 | Room size | 1…100 | 50 | inteiro |
| 4 | Diffusion | 0…100 | 100 | % |
| 5 | Decay | 1…200 | 15 | `v/10` s (0,1…20 s) |
| 6 | High damping | 5…221 | 40 | como High cut (kHz, 221 = Off) |
| 7 | Bass multiplier | 20…300 | 100 | `v/100` (×0,2…×3) |
| 8 | Crossover | 25…2000 | 500 | Hz |
| 9 | Stereo separation | −64…64 | 0 | `s = v/64`; entra numa matriz 2×2 de mistura do wet; com M/S ligado os termos cruzados trocam de sinal **[DECOMP; sentido exato HIP]** |
| 10 | Dry | 0…160 | 128 | ganho `L11(v/128)` (128 = 0 dB, 160 = +5,6 dB) |
| 11 | Early reflections | 0…160 | 128 (knob inicial 64) | ganho `L11(v/256)` (128 → −12,7 dB) |
| 12 | Wet | 0…160 | 128 (knob inicial 64) | ganho `L11(v/256)` (64 → −21,7 dB) |
| 13 | Mod speed | 0…2048 | 684 | `v/2048` (%) |
| 14 | Mod depth | 0…2048 | 0 | `v/2048` (%) |

A escala 1/256 de ER/wet (contra 1/128 do dry) foi lida diretamente das constantes (`0x715234` = 0,00390625,
`0x715230` = 0,0078125) **[DECOMP]**; a base `ln 11` vem da tabela compartilhada **[HIP forte]**.
O texto mostra ER/wet como `v/128` %, então "100 %" na UI ≈ −12,7 dB reais.

### 3.3 Uso nos demos **[DADOS]**

Low cut 75 Hz em 71 (Off em 25; até 2,49 kHz); high cut 4 kHz em 97 (Off em 14); predelay 0 em 227
(com sync: 18, 27, 48 ticks); room 50/100; decay 1,5 s (53), 2,2 s, 4,5 s; damping 4 kHz; bass ×1,0;
crossover 500 Hz; separação 0 ou negativa (−40, −64); dry 128 em 225 e **0 em 39** (reverb em send);
wet espalhado (64…160). Mod depth > 0 em 81 das 271 instâncias v10001.

---

## 4. Delays

### 4.1 Fruity Delay 3 (110 slots) **[DECOMP layout + STR + DADOS; unidades HIP]**

Layout: `u32 versão (1)` + **26 × i32** (`P+0x180`, 0x68 B) = 108 B. O DSP e o texto ficam num motor C++
separado (`engine_x64.dll`, classes `FLDelay3Plugin`/`ILDelay3`, strings de terceiros), então as unidades
abaixo vêm do DFM e da estatística.

| i | Nome (DFM) | Faixa | Default | Leitura |
|---|---|---|---|---|
| 0 | Input wet | 0…6000 | 6000 | normalizado `v/6000` |
| 1 | Delay type | 0…3 | 0 | 0 Mono ("Fake stereo"), 1 True stereo, 2 **Ping pong**, 3 Off |
| 2 | Tempo sync | 0/1 | — | |
| 3 | Keep pitch | 0/1 | — | |
| 4 | **Time** | 1…768 | 80 | sync = 1: **ticks de 1/96 de tempo** (96 = 1/4, 144 = 1/4 pontuada, 192 = 1/2, 384 = 1 compasso) **[DADOS]**; sync = 0: unidade desconhecida **[HIP]** |
| 5 | Time R/L offset | 0…6000 | 3000 | 3000 = centro |
| 6 | Time smooth | 0…1000 | 500 | |
| 7 | Stereo spread | 0…6000 | 6000 | |
| 8–10 | Mod rate / time / cutoff | 0…6000 | 3000/0/0 | |
| 11–12 | Diffusion level / spread | 0…6000 | 0/3000 | |
| 13 | Distortion type | 0/1 | 0 | Limit / Sat |
| 14 | **Feedback** | 0…6000 | 3000 | `v/6000` **[HIP: nível 0…100 %]** |
| 15 | Filter mode | 0…2 | 0 | LP / HP / BP **[STR/HIP]** |
| 16 | **Feedback cutoff** | 0…6000 | 3000 | 6000 = máximo ("Max Hz") **[HIP log 20 Hz…20 kHz]** |
| 17 | Feedback resonance | 0…6000 | 3000 | |
| 18–20 | Distortion threshold/hardness/symmetry | 0…6000 | 0 | |
| 21 | Rate reduction | 0…60000 | 60000 | 60000 = off |
| 22 | Bits reduction | 0…6000 | 6000 | 6000 = off |
| 23 | **Output wet** | 0…6000 | 6000 | |
| 24 | Output tone | −3000…3000 | 0 | |
| 25 | **Output dry** | 0…6000 | 6000 | 0 em 19 slots (send) |

Demos: tipo 2 (ping pong) 59, 1 (true stereo) 46; sync ligado 80/110; feedback 0…5610 (mediana ~3000).

### 4.2 Fruity Delay 2 (62 slots) **[DECOMP+STR+DADOS]**

**Sem versão**: 8 × i32 crus (32 B).

| i | Off | Nome | Faixa | Default | Conversão |
|---|---|---|---|---|---|
| 0 | 0x00 | Input pan | −128…128 | 0 | `v/128` |
| 1 | 0x04 | Input volume | 0…160 | 128 | texto `v/160` %; ganho **[HIP: `L11(v/128)`]** |
| 2 | 0x08 | Dry | 0…160 | 128 | idem |
| 3 | 0x0C | Feedback | 0…127 | 64 | `v/127` (texto em %) |
| 4 | 0x10 | **Time** | 1…768 | 144 | **sempre sincronizado**: `v/48` tempos (48 ticks/tempo; 144 = 3 tempos, até 16 tempos) |
| 5 | 0x14 | Time stereo offset | −512…512 | 0 | `0,5·(1001^(|v|/512) − 1)` %, sinal = esquerda/direita |
| 6 | 0x18 | Feedback mode | 0…2 | 0 | Normal / Invert / **Ping pong** |
| 7 | 0x1C | Cutoff | 0…128 | 128 | `v/128` (128 = aberto) **[HIP: mesma lei do filtro do canal]** |

Demos: modo ping pong em 53/62; time 192/144/384/96; feedback mediano 64.

---

## 5. Fruity Compressor (88 slots) **[DECOMP+STR+DADOS]**

Layout: `u32 versão (1, 2, 3)` + **6 × i32** (`P+0x124`) + `u32 flags` (v ≥ 2) + `i32 sidechain` (v3).
Tamanhos 28/32/36 B.

| i | Off | Nome | Faixa | Default | Conversão |
|---|---|---|---|---|---|
| 0 | 0x04 | Threshold | −600…0 | 0 | `v/10` dB |
| 1 | 0x08 | Ratio | 4…300 | 10 | `v/10` : 1 (0,4…30) |
| 2 | 0x0C | Gain | −300…300 | 0 | `v/10` dB |
| 3 | 0x10 | Attack | 0…4000 | 150 | `v/10` ms (0…400; default 15 ms) |
| 4 | 0x14 | Release | 1…4000 | 200 | `v` ms |
| 5 | 0x18 | Type | 0…7 | 0 | Hard, Medium, Vintage, Soft, Hard/R, Medium/R, Vintage/R, Soft/R (joelho; "/R" **[HIP: detecção RMS/lenta]**) |
| — | 0x1C | flags | bit 0 | | liga **lookahead**: latência reportada 0 ou **64 amostras** (`FUN_006bdb90`, tabela `0x70E378`) |
| — | 0x20 | sidechain | 0…n | | só v3 |

Demos: threshold −13 dB (15), −20 dB, −35 dB; ratio 10:1, 8:1, 6,1:1; gain +13 dB; tipos Hard 39,
Vintage 18, Hard/R 17; lookahead ligado em 71/86.

---

## 6. Extras (layout + faixas; conversões parcialmente HIP)

| Plugin | Layout do `0xD5` | Campos (faixa, default) | Notas |
|---|---|---|---|
| **Fruity Balance** (57) | 2 × i32, sem versão | pan −128…128 (0); volume 0…320 (256) | volume 256 = 0 dB **[HIP: `L11(v/256)`]**; demos: pan sempre 0 |
| **Fruity Soft Clipper** (28) | 2 × i32 | threshold 1…127 (100); post gain 0…160 (128) | threshold = `v/128` linear (100 → −2,1 dB) **[DECOMP]**; post = `L11(v/128)` **[DECOMP]** |
| **Fruity Fast Dist** (53) | 5 × i32 | pre amp 64…192 (128); threshold 1…10 (10); tipo 0/1 (A/B) **[HIP]**; mix 0…128; post 0…128 | |
| **Fruity Filter** (32) | u32 versão (1/2) + 6 × i32 (+ u8 em v2) | cutoff 0…1024 (1024); reso 0…1024 (205); LP, BP, HP 0…1024 (mix de cada saída); x2 0/1; byte v2 = "Remove DC offset" **[HIP]** | |
| **Fruity PanOMatic** (193) | 6 × i32 | pan 0…256 (128 = centro); volume 0…320 (256); LFO shape 0 sine/1 tri/2 square; LFO target 0 off/1 pan/2 vol; LFO amount 0…128 (64); LFO speed 0…55000 (27500) | 163/193 com LFO off → é um "pan+volume" |
| **Fruity Stereo Shaper** (77) | u32 0 + 6 × i32 + u32 0 + u32 | R→L, L, R, L→R (±25600; L/R 12800); delay ±4096; dephasing ±4096; FX position (0 pre / 1 post) | 12800 = ganho 1,0 linear (8076 = −4 dB, presets M/S) **[DADOS]** |
| **Fruity Flangus** (56) | 8 × i32 | order 1…9 (4); depth 0…1024 (512); speed 0…1024 (768); delay 0…128 (8); spread 0…128 (128); cross ±128; dry ±128; wet ±128 | mapeia no chorus |
| Fruity WaveShaper (73) | u16/u16 + parâmetros + pontos da curva em `double` | pre amp 64…192, mix 0…128, post 0…128 no início | não decodificado |

Também presentes e não decodificados: Maximus (125, ~1,3 kB), Soundgoodizer (97, 12 B: preset A–D +
amount **[HIP]**), Transient Processor (47), Love Philter, Multiband Compressor, Delay Bank, Gross Beat,
Patcher, LuxeVerb.

---

## Recomendações para o LYNX

Regra geral: identificar pelo `0xC9` (aceitar UTF-16LE **e** ASCII), ler o `0xD5`, converter para unidades
reais com as fórmulas acima e só então para o 0…1 dos parâmetros internos (`peq::hzValue`,
`peq::gainValue`, `peq::qValue` etc.). Sempre ignorar o estado reserva (A/B).

1. **Parametric EQ 2 → "EQ paramétrico" (7 bandas, já existe em `src/core/Effects.*`).** É o mapeamento
   que mais rende (1.138 slots, ~3.600 bandas ativas).
   - Tipo: 0 → `off`; 1 → `highCut`; 2 → `bandPass`; 3 → `lowCut`; 4 → `notch`; 5 → `lowShelf`;
     6 → `peak`; 7 → `highShelf`. Banda com bit 0 na máscara (v ≥ 4) → `off`.
   - Frequência `20·1000^(v/65536)`; ganho `v/100` dB (±18 cabe exatamente na faixa do LYNX).
   - Q: LP/HP `0,1 + 9,9(1−w)³`; peaking/BP/notch `1/(2·sinh(0,3466·BW_oct))` com `BW_oct = 0,1 + 4,9w`
     (BP: ×0,708, notch: ×1,413); shelf: calcular `S = 1/3 + 1,7193(1−w)` e passar para Q com
     `1/Q = √((A+1/A)(1/S−1)+2)`, `A = 10^(dB/40)` — ou, melhor, adicionar ao `peq` uma variante de shelf
     parametrizada por *S* e de peaking por largura em oitavas, que reproduz o FL sem aproximação.
   - Inclinação: o FL tem ordem **por banda**; o LYNX tem um `slopeParam` global (1, 2, 4 biquads).
     Precisa virar parâmetro por banda (stages = |ordem|+1, ou ao menos 1/2/4 com |ordem| 0 → 1, 1 → 2,
     2…3 → 4). "Steep 8" em HP/LP (537 bandas) ≈ 4 biquads Butterworth (48 dB/oit). Para peaking/shelf
     com ordem ≠ 0 (≈ 40 casos), cascatear com ganho dividido é suficiente.
   - Main level → parâmetro "Saída" (`outputParam`), `v/100` dB.
2. **Limiter → Limiter (lookahead) + Compressor.** O Fruity Limiter roda *gain → compressor → limitador →
   gate* ao mesmo tempo; a aba salva é só visual.
   - Ceiling `L3(v/1000)` → "Teto" (se ≥ +0 dB e ganho 0 dB, o limitador é praticamente transparente);
     Gain `L7(v/1000)` → "Ganho"; Release `4·(501^(v/10000)−1)` ms → "Release".
   - Se ratio > 0 e threshold < 1000: instanciar também o **Compressor** interno antes do limiter
     (threshold `20·log10 L7`, ratio pela fórmula 2.2, attack/release `T`/`2T`, knee `v/10` %).
   - Noise gate (threshold > 0) não tem equivalente; ignorar (demos: sempre 0). Sidechain: ignorar e
     avisar no log.
3. **Reeverb 2 → Reverb (Freeverb).** Sala ← `room/100` combinado com decay (o Freeverb não tem decay em
   segundos: aproximar `roomsize` pelo RT60 = decay·bass mult nos graves); "Abafar" ← high damping
   (kHz → 0…1, 221 = sem abafamento); "Largura" ← `1 − |sep|/64` **[HIP]**; Mix: o efeito interno é
   wet/dry, então usar `wet_lin = L11(wet/256) + 0,5·L11(er/256)` e `dry_lin = L11(dry/128)`; dry = 0
   (39 slots) indica reverb em send → mix 100 % wet. Low cut/high cut: aplicar um HP/LP no wet se o LYNX
   ganhar esses parâmetros (low cut ligado em 308 dos 333 slots); predelay idem (novo parâmetro barato).
4. **Delay 2/Delay 3 → Delay.** Tempo em tempos de nota: Delay 2 `v/48`, Delay 3 (sync) `v/96`; ping pong
   = modo 2 nos dois. Feedback: Delay 2 `v/127`, Delay 3 `v/6000`. Mix: Delay 2 dry `L11(dry/128)` e wet
   pela entrada; Delay 3 output wet/dry `v/6000`. "Abafar" ← cutoff (Delay 2 `v/128`, Delay 3 `v/6000`,
   invertidos). Delay 3 sem sync: manter o default até decodificar o motor.
5. **Compressor → Compressor.** Mapeamento direto e exato: threshold `v/10` dB, ratio `v/10`, makeup
   `v/10` dB, attack `v/10` ms, release `v` ms; tipos Soft/Medium sugerem joelho suave. O flag de
   lookahead (64 amostras) pode ser ignorado.
6. **Extras:** Soft Clipper e Fast Dist → Distorção (drive pelo threshold/pre amp, saída pelo post gain);
   Fruity Filter → Filtro (LP se `lp ≥ hp`, cutoff `v/1024` **[HIP: lei do corte]**); Balance e PanOMatic
   sem LFO → ganho/pan da pista (ou um efeito utilitário "ganho/pan" novo, trivial); Flangus → Chorus
   (velocidade/profundidade/mix); Stereo Shaper → precisa de um efeito "matriz estéreo" novo
   (4 ganhos + delay), barato de implementar e usado em 77 slots.
7. **Efeitos internos novos sugeridos**, em ordem de retorno: (a) inclinação por banda + shelf por *S* no
   EQ paramétrico; (b) low cut / high cut / predelay no Reverb; (c) noise gate e sidechain só se o
   roadmap pedir; (d) matriz estéreo (Stereo Shaper) e ganho/pan utilitário.

---

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| PEQ2 `FUN_006ec340` (VMT+3) | `SaveRestoreState`: versões 2…8, layout 0x000…0x161 | DECOMP |
| PEQ2 `FUN_006ec2a0` / `FUN_006ec2f0` | Empacota/desempacota a máscara de 7 bandas | DECOMP |
| PEQ2 `FUN_006efda0` | Texto: ganho ×0,01 dB, Hz, largura %, nomes de tipo/ordem | DECOMP |
| PEQ2 `FUN_006fbf60`, `FUN_006fbf20`, `FUN_006f49c0` | Escala de frequência 20·1000^(v/65536) | DECOMP |
| PEQ2 `FUN_006ef120` | Parâmetros → banda (remapeio de tipo, ordem, ganho/estágio) | DECOMP |
| PEQ2 `FUN_006ee6e0`, `FUN_005d1aa0` | Coeficientes RBJ por tipo, Q/BW/S, cascata | DECOMP |
| PEQ2 tabelas `0x74E718…0x74E8B0` | Remapeio, nomes, multiplicadores steep | DECOMP |
| PEQ2 `FUN_005f41c0` | Constantes compartilhadas (ln 2, ln 3, ln 7, ln 11, ln 601, ln10/20) | DECOMP |
| PEQ2 DFM `TSynthEditorForm` | Tags 0…35, faixas, defaults, hints | STR |
| Limiter `FUN_006d3760` | `SaveRestoreState` (v5…7) | DECOMP |
| Limiter `FUN_006d5380`, `FUN_006d3dc0`, `FUN_005e0ce0`, `FUN_005e22b0` | Leis L7/L3/T, ratio, knee, dB | DECOMP |
| Reeverb 2 `FUN_006d4920` | `SaveRestoreState` (10000/10001/antigo) | DECOMP |
| Reeverb 2 `FUN_006d5480`, `FUN_006d5160`, `FUN_006d5030` | Unidades; dry `L11(v/128)`, ER/wet `L11(v/256)`, matriz de separação/M-S | DECOMP |
| Delay 3 `FUN_006e8da0`, `FUN_006e9740` | Layout v1 + 26 parâmetros; texto delegado ao `engine_x64.dll` | DECOMP |
| Delay 2 `FUN_006bfa00`, `FUN_006c0490` | 8 parâmetros crus; tempo em 1/48 de tempo; offset 1001^x | DECOMP |
| Compressor `FUN_006bd9a0`, `FUN_006be220`, `FUN_006bdb90` | Layout v1…3; unidades /10; tipos; lookahead 64 | DECOMP |
| Soft Clipper `FUN_006bee60` | threshold v/128, post L11 | DECOMP |
| DFMs de Delay 3/2, Compressor, Balance, Fast Dist, Filter, PanOMatic, Stereo Shaper, Flangus, WaveShaper | Tags, faixas, defaults | STR |
| 165 `.flp` (seção do mixer) | Contagens, versões, tamanhos, valores típicos | DADOS |

### Pendências

- Delay 3: unidade do tempo sem sync e leis de feedback/cutoff (estão no motor C++ `engine_x64.dll`,
  `FLDelay3Plugin`; procurar o método de texto na vtable `+0x1D8` do objeto em `P+0x178`).
- PEQ2: confirmar que o caminho *linear phase* (bit 0 de 0x13D) só muda a fase e onde o *main level* é
  aplicado; significado dos campos de UI 0x124/0x128/0x135/0x142.
- Limiter: piso do comp threshold (`DAT_0073E5D0`, BSS) e a forma exata das curvas 1…8.
- Reeverb 2: confirmar `ln 11` na base de ER/wet (global BSS `0x73746C`) e o sentido da separação.
- Leis de volume de Balance/PanOMatic/Delay 2 (assumido `L11`), cutoff do Fruity Filter, Soundgoodizer.
