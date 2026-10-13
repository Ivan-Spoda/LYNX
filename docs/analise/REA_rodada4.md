# Análise comportamental do FL Studio 2026: rodada 4 (REA) — 3x Osc e filtro do canal

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md`, `REA_rodada2.md` e
> `REA_rodada3.md`. Engenharia reversa *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo
> da Image-Line foi copiado: o que está aqui são descrições, layouts de dados e fórmulas escritas com
> palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a evidência.

- **Alvos:**
  - `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll` (SHA-256 `5d84d5ad…dff2`, Delphi, classe do plugin
    `TTestPlug`, VMT em `0x737e20`), Ghidra 12.1.4 via REA
  - `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`) para o filtro do canal
- **Validação de formato:** 165 `.flp` de `FL Studio 2026/Data`; **152 canais "3x Osc"** em 75 arquivos
  (o pedido falava em 47; o número real, contando arquivos de tutorial/`Plugin presets`, é 152)
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `P+0xNN` é um offset no objeto do plugin 3x Osc (`TTestPlug`); `V+0xNN` é um offset na voz
do canal no FLEngine; `C+0xNN` no objeto do canal (mesma convenção da rodada 3).

---

## 1. Onde fica o estado do 3x Osc **[DECOMP+DADOS]**

- O 3x Osc **não** está no FLEngine: é um plugin nativo separado (`3x Osc_x64.dll` + `engine_x64.dll`).
  No `.flp` ele aparece como qualquer plugin interno: `0xC9` = `"3x Osc"` (UTF-16LE), `0xD4` (cabeçalho de
  52 B), `0xCB` (nome do canal), e **`0xD5` = blob devolvido pelo `SaveRestoreState` do plugin**.
- `SaveRestoreState` = entrada 3 da VMT do `TTestPlug` = **`FUN_00738580`**. Ordem dos eventos dentro do
  canal: o `0xD5` vem **antes** de `0xDB`/`0xE5`/`0xDA` (estes continuam sendo do canal, ver §4).
- Tamanhos vistos nos demos: **92 B** (versão 12, 93 canais), **93 B** (versão 13, 2), **460 B**
  (versão 14, 57). O FL 2026 sempre grava versão 14 / 460 B.

### 1.1 Layout do blob `0xD5` (little-endian) **[DECOMP+DADOS]**

| Off | Tipo | Campo | Faixa (DFM) | Default |
|---|---|---|---|---|
| 0x00 | u32 | **versão** do estado (12, 13 ou 14; grava 14) | | 14 |
| 0x04 | 21 × i32 | **parâmetros 0…20** (tabela 1.2); vão para `P+0x120 + 4·i` | | |
| 0x58 | u8 | **Invert osc 1** (bool) → `P+0x174` | 0/1 | 0 |
| 0x59 | u8 | **Invert osc 2** → `P+0x175` | 0/1 | 0 |
| 0x5A | u8 | **Invert osc 3** → `P+0x176` | 0/1 | 0 |
| 0x5B | u8 | **"Oscillator 3 = amplitude modulation"** (AM) → `P+0x177` | 0/1 | 0 |
| 0x5C | u8 | **HQ** ("High quality / anti-aliased oscillators"), só o bit 0 → `P+0x208` (versão ≥ 13) | 0/1 | 0 |
| 0x5D | 3 B | preenchimento (sempre 0 nos 57 blobs v14) | | 0 |
| 0x60 | 364 B | **cópia do envelope/filtro do canal** (só versão 14; §1.4) | | |

Regras de leitura por versão (`FUN_00738580`, ramo `Save == 0`):

| Versão | O que é lido |
|---|---|
| < 10 | formato antigo de 4 campos por oscilador (40 B), convertido para o layout de 7 campos. Não aparece nos demos |
| 10, 11 | 80 B (parâmetros 0…19; *phase rand* = 0) + 4 bools. Versão 10: os 4 bools são zerados |
| 12, 13 | 89 B a partir de 0x04 (21 parâmetros + 4 bools + byte HQ; com 92 B o byte HQ não existe e fica 0) |
| ≥ 14 | 0x1C8 = 456 B a partir de 0x04. A cópia do canal (0x60…) só é aplicada se vieram ≥ 364 B nela |

Os nomes dos bools saem da tabela de campos do formulário `TSynthEditorForm` (offsets
`0x798 InvOsc1Btn`, `0x7C0 InvOsc2Btn`, `0x7F0 InvOsc3Btn`, `0x848 AMBtn`, `0x888 HqModeBtn`), que é
exatamente a ordem em que `FUN_00738580` repassa `P+0x174…0x177` e `P+0x208` para a UI **[DECOMP+STR]**.

### 1.2 Os 21 parâmetros **[DECOMP+STR+DADOS]**

O índice do parâmetro `i` se decompõe como `(i + 1) = 7·osc + campo` (`FUN_005b63d0(i+1, 7)` em
`FUN_00739060`, o `ProcessParam`). O oscilador 1 não tem "mix" (a posição do campo 0 dele é ocupada pela versão do blob,
por isso o arquivo começa com a versão: o layout em disco é literalmente `[versão][pan1]…`). Faixas e
nomes vêm do DFM (`Tag` de cada controle = índice do parâmetro).

| i | Off no `0xD5` | Osc | Campo | Faixa | Default | Unidade/uso |
|---|---|---|---|---|---|---|
| 0 | 0x04 | 1 | Pan | −64…+64 | 0 | pan/64 (§2.3) |
| 1 | 0x08 | 1 | Shape | 0…6 | 0 | §1.3 |
| 2 | 0x0C | 1 | Coarse | −24…+24 | 0 | semitons |
| 3 | 0x10 | 1 | Fine | −100…+100 | 0 | centésimos |
| 4 | 0x14 | 1 | Stereo phase offset (SPO) | −64…+64 | 0 | v/128 ciclo, só no canal R |
| 5 | 0x18 | 1 | Stereo detune | −50…+50 | 0 | centésimos, só no canal R |
| 6 | 0x1C | 2 | **Mix level** | 0…128 | 64 | v/128 |
| 7 | 0x20 | 2 | Pan | −64…+64 | 0 | |
| 8 | 0x24 | 2 | Shape | 0…6 | 0 | |
| 9 | 0x28 | 2 | Coarse | −24…+24 | 0 (patch novo: −12) | |
| 10 | 0x2C | 2 | Fine | −100…+100 | 0 | |
| 11 | 0x30 | 2 | SPO | −64…+64 | 0 | |
| 12 | 0x34 | 2 | Stereo detune | −50…+50 | 0 | |
| 13 | 0x38 | 3 | **Mix level** | 0…128 | 64 no DFM (patch novo: 32) | |
| 14 | 0x3C | 3 | Pan | −64…+64 | 0 | |
| 15 | 0x40 | 3 | Shape | 0…6 | 0 | |
| 16 | 0x44 | 3 | Coarse | −24…+24 | 0 (patch novo: −24) | |
| 17 | 0x48 | 3 | Fine | −100…+100 | 0 | |
| 18 | 0x4C | 3 | SPO | −64…+64 | 0 | |
| 19 | 0x50 | 3 | Stereo detune | −50…+50 | 0 | |
| 20 | 0x54 | — | **Stereo phase randomness** | 0…64 | 0 | §2.5 |

O "patch novo" (3OSC.flp, sem edições) é: osc2 mix 64 coarse −12, osc3 mix 32 coarse −24, todo o resto 0
**[DADOS]**. Todos os 152 canais respeitam as faixas acima (mínimos/máximos observados: pan −23…35,
coarse −24…24, fine −100…51, SPO −17…64, detune −50…50, mix 0…128, phase rand 0…64) **[DADOS]**.

O 3x Osc **não tem** parâmetro de "phase offset" absoluto nem de oitava separada: a oitava é o coarse
(±24 st = ±2 oitavas), e o único deslocamento de fase é o SPO (relativo entre L e R).

### 1.3 Formas de onda (Shape) **[DECOMP+DADOS, ordem 1…4 HIP forte]**

`ProcessParam`, campo 2 (`FUN_00739060`): para `shape < 6` o oscilador aponta para a tabela de ondas do
host `host+0x18 + shape·8` (as 10 tabelas de onda que o FL entrega aos plugins nativos); `shape = 6`
usa a amostra carregada pelo usuário (*custom*, via dispatcher 10) ou, se não houver, a tabela 3.
O código marca `shape == 5` como **ruído** (gerado com incremento de fase aleatório, não por tabela
periódica) e considera a tabela 0 "sem alias" (**senoide**).

| Valor | Forma | Evidência |
|---|---|---|
| 0 | **Senoide** | DECOMP (tabela 0 tratada como sem harmônicos); DADOS ("Sub Bass", "DNB SUB", "Sub Pad": 0) |
| 1 | **Triângulo** | HIP (ordem das tabelas do host; rodada 3: senoide, triângulo, quadrada, rampa…) |
| 2 | **Quadrada** | DADOS (canal "Square" e "Square Bot": 2) |
| 3 | **Dente de serra** | DADOS ("Sweep Down Saw": 3) |
| 4 | **Dente de serra arredondada** ("analog saw") | HIP (ordem das tabelas do host) |
| 5 | **Ruído** | DECOMP + DADOS (todos os "White Noise"/"WNoise": 5) |
| 6 | **Custom** (amostra do usuário; sem amostra = serra) | DECOMP |

Uso nos demos: 0 (201×), 3 (122×), 2 (50×), 4 (28×), 5 (28×), 1 (24×), 6 (3×).

### 1.4 A cópia do canal dentro do `0xD5` v14 **[DECOMP+DADOS]**

Os 364 B a partir de 0x60 são um "instantâneo" do envelope/filtro do canal guardado pelo próprio plugin
(gravado por `FUN_0072c390`, lido por `FUN_0072c2e0`, defaults em `FUN_0072c250`):

| Off (no blob) | Conteúdo |
|---|---|
| 0x060 | 5 registros de 68 B, **idênticos ao formato do `0xDA`** (Pan, Vol, ModX, ModY, Pitch; rodada 3 §1.2) |
| 0x1B4 | 8 B não usados (0) |
| 0x1BC | i32 **Mod X** (cutoff), default 256 |
| 0x1C0 | i32 **Mod Y** (ressonância), default 0 |
| 0x1C4 | 4 B não usados (0) |
| 0x1C8 | i32 **tipo de filtro** (0…7, §3.1) |

- Em 52 dos 57 blobs v14 os 5 registros são byte a byte iguais aos `0xDA` do canal, e os três inteiros
  batem com `modX/modY/tipo` do `0xDB`. Nos 5 restantes o canal foi editado depois (o instantâneo ficou
  velho) **[DADOS]**.
- Se o instantâneo foi carregado, o plugin marca `P+0x234 = 1` e deixa de pedir ao host o envelope
  padrão (dispatcher `0x1B`/`0x28` em `FUN_00737f90`). Serve para presets do plugin carregarem também o
  envelope e o filtro **[DECOMP para a marcação; HIP para o propósito]**.
- **Para importar, use sempre os eventos do canal (`0xDA`, `0xDB`, `0xE5`)**; o instantâneo é só
  plano B quando faltarem.

---

## 2. Como o 3x Osc soa: conversões **[DECOMP]**

### 2.1 Afinação

- Cada oscilador guarda `cents = coarse·100 + fine` (`FUN_00739060`, campos 3/4) e toca em
  `pitchDaVoz + cents` (a voz já inclui nota, pitch do canal e envelope de pitch do canal). O canal R soma
  ainda o **stereo detune** em centésimos: `pitchR = pitchDaVoz + cents + detune` (`FUN_0073a230`).
- Para o LYNX: `oitava = coarse / 12` (inteiro) e `semitom = coarse mod 12` se o sintetizador interno só
  tiver oitava; o FL permite qualquer semitom em ±24. `detune_cents = fine` (o *stereo detune* não tem
  equivalente mono; aproxime como meio detune para cada lado, ou descarte).

### 2.2 Mistura (níveis) — `FUN_00738f10`

```
g3 = mix3 / 128
resto = 1 − g3            (se AM desligado)
resto = 1                 (se AM ligado: o osc 3 não "rouba" nível)
g2 = resto · mix2 / 128
g1 = resto − g2
```

Ou seja, o osc 1 recebe o que sobra. Patch novo (mix2 = 64, mix3 = 32): g3 = 0,25, g2 = 0,375,
g1 = 0,375 (soma 1). "Invert osc N" troca o sinal de `gN` (XOR no bit de sinal do float). Oscilador com
ganho 0 só avança a fase, não é renderizado.

**Para o LYNX (nível 0…1 por oscilador):** `level1 = g1`, `level2 = g2`, `level3 = g3` (com sinal
negativo se invertido). Se o sintetizador do LYNX normaliza a soma, isso já sai normalizado.

### 2.3 Pan por oscilador — `FUN_00738ea0`

O plugin chama um serviço do host (`host->vtbl+0xF8`) com `pan/64` (−1…+1) e o ganho do oscilador, que
devolve os ganhos L/R. **[HIP]** a lei é o balanço linear usual do FL (lado oposto atenuado por
`1 − |p|`, lado do pan em ganho cheio); não verifiquei a função do host.

### 2.4 Fase estéreo (SPO)

Na criação da voz (`FUN_00739a40`) a fase L de cada oscilador começa em 0 e a fase R em
`SPO · 0x1FFFFFF` (fase de 32 bits), isto é, **SPO/128 de ciclo** (±64 = ±meio ciclo = ±180°). A fase é
reiniciada a cada nota (não há oscilador livre).

### 2.5 Stereo phase randomness (parâmetro 20)

Guardado como `v << 18`. Na criação da voz, às fases L e R de cada oscilador é somado
independentemente um valor aleatório uniforme de largura `v/64` ciclo, centrado em 0
(±`v/128` ciclo; v = 64 → fase totalmente aleatória). Como L e R recebem sorteios diferentes, isso
também abre o estéreo.

### 2.6 AM e HQ

- **AM** (`FUN_007389d0`, tabela `DAT_00791a18` = {−1, 2}): com AM ligado, o oscilador de índice 2
  (osc 3) é renderizado num modo de mistura diferente (modo 0x0C de `FUN_00739e90`) que **multiplica**
  o acumulado de osc 1 + osc 2 em vez de somar **[DECOMP para o modo; HIP para a fórmula exata, p.ex.
  `(o1·g1 + o2·g2)·(g3·o3)`]**.
- **HQ** (`P+0x208`) e a flag do host "render de alta qualidade" (`P+0x1FD`, dispatcher 1 bit 2)
  selecionam osciladores com anti-aliasing para formas ≠ senoide/ruído.
- O processamento estéreo só é ativado (`P+0x1FC`) se algum oscilador tiver pan, SPO ou detune ≠ 0, se
  houver *phase rand*, ou se houver ruído; caso contrário L = R.

### 2.7 Envelope e filtro

O 3x Osc **não tem envelope nem filtro próprios na DSP**. O som é moldado pelo canal:
volume pelo registro 1 do `0xDA` (rodada 3 §1), filtro pelo `0xDB` + registros 2/3 do `0xDA` (§3).
Os nomes "Note cutoff frequency" / "Note resonance" que o plugin expõe (`FUN_00739640`) são apenas os
rótulos das propriedades de nota Mod X / Mod Y.

---

## 3. Filtro do canal (Mod X / Mod Y) **[DECOMP]**

### 3.1 Tipos (último i32 do `0xDB`) **[STR+DECOMP]**

O combo `FTypeSelect` do `TPluginForm` (Channel settings) lista, na ordem dos valores:

| Valor | Nome na UI | Dica da UI | Implementação interna (tabela `0x01510E40`: tipo, ×2) |
|---|---|---|---|
| 0 | Fast LP | "Default fast 12dB low pass" | tipo 0 (filtro rápido de 2 polos, §3.3) |
| 1 | LP | 12 dB low pass | biquad LP |
| 2 | BP | 12 dB band pass | biquad BP |
| 3 | HP | 12 dB high pass | biquad HP |
| 4 | BS | 12 dB band stop | biquad notch |
| 5 | LPx2 | 24 dB low pass | biquad LP **duas vezes** em série |
| 6 | SVF LP | 12 dB low pass SVF | SVF |
| 7 | SVF LPx2 | 24 dB low pass SVF | SVF duas vezes |

Nos demos, dos canais 3x Osc v14 com filtro: tipos 0, 1, 2, 5, 6 e 7 aparecem **[DADOS]**.

### 3.2 Da `0xDB` até as entradas do filtro

1. Canal (`FUN_00fea140`): `cutN = (modX + modXAdj) / 256`, `resN = (modY + modYAdj) / 256`
   (modX/modY do `0xDB`, 0…256; ajustes do `0xE5`, normalmente 0). Gravado em `C+0x1A4/0x1A8`.
   Defaults: Mod X = 256 (aberto), Mod Y = 0.
2. Voz (`FUN_00d265e0`): `x = cutN + modX_da_nota`, `r = resN + modY_da_nota` (a nota soma o seu
   próprio Mod X/Y; com o valor padrão da nota a contribuição é 0 **[HIP para a escala da nota]**).
3. A cada tick de controle (`FUN_00d24650`): `x += envCut`, `r += envRes`, onde
   `envCut = y·envAmt/128 + l·lfoAmt/128` do registro 2 do `0xDA` e `envRes` idem do registro 3
   (rodada 3 §1.6). Ou seja, o envelope de cutoff com amount 128 percorre a faixa inteira de Mod X.

`x` e `r` são normalizados (1,0 = knob no máximo). Abaixo, `k = 44100 / taxaDeAmostragem`
(global `0x013EC894`, gravada em `FUN_006a0230`).

### 3.3 Fast LP (tipo 0) — `FUN_00981df0` + `FUN_006a05d0`

Coeficientes:
```
c  = 0                          se x ≤ 0
c  = 1                          se x ≥ 1
c  = min(1, (601^x − 1)/600 · k)  caso contrário
fb = 0                          se r ≤ 0
fb = 1 − (601^(1 − 0,625·min(r,1)) − 1)/600   caso contrário
```
(`ln 601` em `DAT_015F49E8`, calculado em `FUN_006a6890`; `1/600` em `0x013EC888`; `0,625` em
`0x0141E5DC`). Recorrência por amostra e por canal (estado `v`, `y`):
```
v ← v + (entrada − y)·c
y ← y + v          (saída = y)
v ← v · fb
```
Com `fb = 0` é um passa-baixas de 1 polo (`fc ≈ −fs/(2π)·ln(1 − c)`); com `fb` perto de 1 vira um
2 polos ressonante com pico em `ω ≈ acos(1 − c/2)` ≈ `√c` rad/amostra. Com Mod X = 256 e Mod Y = 0
(default do canal) é **bypass exato** (c = 1, fb = 0); o filtro só passa a ser processado na voz depois
que `c ≠ 1` ou `fb ≠ 0` (flag `V+0x234`). `c` é interpolado linearmente dentro do bloco.

Tabela de referência (44,1 kHz, r = 0):

| Mod X | x | c | fc aprox. (1 polo) |
|---|---|---|---|
| 256 | 1,00 | 1,000 | bypass |
| 192 | 0,75 | 0,202 | 1,6 kHz |
| 128 | 0,50 | 0,0392 | 280 Hz |
| 64 | 0,25 | 0,0066 | 46 Hz |
| 0 | 0 | 0 | silêncio (c = 0) |

### 3.4 LP / BP / HP / BS / LPx2 (tipos 1…5) — `FUN_00981c50` + `FUN_009815f0` + `FUN_00982020`

```
ω = ((51^x − 1)·0,0596 + 0,02)·k        rad/amostra, limitado a [0,02·k ; 3,0]
Q = 0,1                                 se r ≤ 0
Q = (51^(0,625·min(r,1)) − 1)·0,4 + 0,1   caso contrário
α = sin ω / (2Q)
```
(`ln 51` e `ln 51 · 80/128` calculados em `FUN_009823d0`.) Os coeficientes são os do *Audio EQ
Cookbook* (RBJ), normalizados por `a0 = 1 + α`:

| Tipo | b0 | b1 | b2 |
|---|---|---|---|
| LP | (1−cos ω)/2 | 1−cos ω | (1−cos ω)/2 |
| BP (pico 0 dB) | α | 0 | −α |
| HP | (1+cos ω)/2 | −(1+cos ω) | (1+cos ω)/2 |
| BS | 1 | −2 cos ω | 1 |

com `a1 = −2 cos ω`, `a2 = 1 − α` (todos ÷ `a0`), aplicado em forma direta I por canal.

- **Frequência em Hz** (independe da taxa, porque `k` compensa): `fc = ((51^x − 1)·0,0596 + 0,02) ·
  44100 / (2π)`. Mod X 0 → **140 Hz**; 64 → 840 Hz; 128 → **2,71 kHz**; 192 → 7,7 kHz; 256 → **21,06 kHz**
  (teto: ω = 3,0 a 44,1 kHz).
- **Q:** Mod Y 0 → 0,1 (bem amortecido: mesmo "aberto", o LP já atenua agudos); 64 → 0,44;
  128 → 1,07; 192 → 2,23; 256 → **4,37**.
- **LPx2:** o mesmo biquad LP aplicado duas vezes, e o ganho da voz é multiplicado por
  `1 − Q·(3,2 − ω)·0,046875` para compensar o pico duplo (`FUN_00d247a0`).

### 3.5 SVF LP / SVF LPx2 (tipos 6, 7) — `FUN_00981f20` + `FUN_00982140`

```
f = 0                         se x ≤ 0
f = min(1, (26^x − 1)·0,04·k)  caso contrário
d = 1                         se r ≤ 0
d = 1 − 0,8·r                 se 0 < r < 1
d = 0,2                       se r ≥ 1
por amostra:  lp ← lp + f·bp ;  bp ← bp + f·(entrada − lp − d·bp) ;  saída = lp
```
É a SVF de Chamberlin (`f ≈ 2 sin(π fc/fs)`): Mod X 256 → f = 1 → **≈ 7,35 kHz** a 44,1 kHz;
128 → f = 0,164 → ≈ 1,15 kHz; 0 → silêncio. **SVF LPx2** roda o mesmo filtro duas vezes; na segunda
passada o amortecimento vira `d + (1 − d)·0,25` (`FUN_00d247a0`, constante `0x013E7548`).

### Recomendações para o LYNX (importar "3x Osc")

1. Detecte o canal pelo `0xC9 == "3x Osc"` e leia o `0xD5`: `versão` em 0x00, 21 `i32` a partir de 0x04,
   bools em 0x58…0x5C (0x5C só se o blob tiver ≥ 93 B). Ignore o bloco de 0x60 se o canal tiver `0xDA`.
2. Por oscilador `n` (base `b = 0x04 + 28·(n−1)`, campos `[mix|—, pan, shape, coarse, fine, spo, det]`,
   onde para o osc 1 o primeiro campo é a versão):
   - `waveform = {0: sine, 1: triangle, 2: square, 3: saw, 4: saw (arredondada), 5: noise, 6: saw}`
   - `octave = trunc(coarse/12)`, `semitons_restantes = coarse − 12·octave` (some ao detune: `detune
     = fine + 100·semitons_restantes` se o LYNX só aceitar oitava + cents)
   - `level` = `g1/g2/g3` de §2.2 (negativo se *invert*); `pan = pan/64` se o LYNX tiver pan por osc.
3. ADSR: registro 1 (Volume) do `0xDA` com as conversões da rodada 3 §1. Se o envelope estiver
   desligado, ADSR "porta" (ataque 0, sustain 1, release curto).
4. Filtro: `cutoff_Hz` e `Q` pelas fórmulas de §3.4 quando o tipo for 1…5 (o LP do LYNX tem
   cutoff/ressonância). Para o tipo 0 (Fast LP, o default), Mod X = 256 e Mod Y = 0 significa **sem
   filtro**; para outros valores use `fc ≈ −fs/(2π)·ln(1 − c)` como aproximação, ou implemente a
   recorrência de §3.3 se quiser fidelidade. Tipos 6/7 → `fc ≈ fs/π · asin(f/2)`.
5. Envelope de cutoff (registro 2 do `0xDA`, se ligado): o amount ±128 desloca `x` em ±1,0, isto é,
   percorre a faixa inteira de Mod X. Converta para "oitavas de modulação" do LYNX calculando `fc(x)`
   nos dois extremos.

---

## 4. Plugins vizinhos (só reconhecimento, barato) **[DADOS]**

- **FLEX** (57 canais, `0xD5` de 2.707 B): começa com `u32 1, u32 1, u32 0x812, u32 0`, depois o nome da
  **biblioteca** (texto de tamanho fixo em 0x10, p.ex. "General Midi Library", "Olbaid Compendium") e o
  nome do **preset** em 0x411 ("Piano Dark", "4.9 Pad"…), seguidos dos parâmetros (macros). O som depende
  dos pacotes de amostras do FLEX: o LYNX só consegue mapear pelo nome do preset. Não decodificado.
- **Fruity DrumSynth Live** (8 canais, 19.467 B): `u32 4` (versão), `u32` índice do kit selecionado
  (ex.: 66, 65, 51) e a lista de nomes de kits ("808 Bongo H" … "909 Snare") em strings curtas, seguida
  dos parâmetros. Não decodificado; também não tem equivalente direto no sintetizador de 3 osciladores.

---

## 5. Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| 3x Osc `FUN_00738580` (VMT+3) | `SaveRestoreState`: versões, layout do `0xD5` | DECOMP |
| 3x Osc `FUN_00739060` | `ProcessParam`: decomposição `(i+1)=7·osc+campo`, shape/pitch/SPO/phase rand | DECOMP |
| 3x Osc `FUN_00738f10`, `FUN_00738ea0` | Mistura de níveis, inversão, pan via host | DECOMP |
| 3x Osc `FUN_00739a40` | Fases iniciais da voz (SPO, aleatoriedade) | DECOMP |
| 3x Osc `FUN_0073a230` | Render: detune no R, AM (modo 0x0C), ruído | DECOMP |
| 3x Osc `FUN_007389d0`, `DAT_00791a18` | AM: osc de índice 2 | DECOMP |
| 3x Osc `FUN_0072c2e0/0072c390/0072c250` | Instantâneo de envelope/filtro (364 B) | DECOMP |
| 3x Osc DFM `TSynthEditorForm` (`0x41D6FC`) | Tags, faixas e defaults dos controles | STR |
| FLEngine DFM `TPluginForm` (`0x1464BCC`) | Nomes e dicas dos 8 tipos de filtro; faixas Mod X/Y | STR |
| FLEngine `FUN_00fea140` | Mod X/Y do canal → `/256` | DECOMP |
| FLEngine `FUN_00d265e0`, `FUN_00d24650`, `FUN_00d247a0` | Soma nota+canal+envelope; despacho por tipo; ×2 | DECOMP |
| FLEngine tabela `0x01510E40` | Tipo do canal → (tipo interno, ×2) | DECOMP |
| FLEngine `FUN_00981df0` + `FUN_006a05d0` | Fast LP | DECOMP |
| FLEngine `FUN_00981c50` + `FUN_009815f0` + `FUN_00982020` | Biquads RBJ | DECOMP |
| FLEngine `FUN_00981f20` + `FUN_00982140` | SVF | DECOMP |
| FLEngine `FUN_006a6890`, `FUN_009823d0`, `FUN_006a0230` | Constantes ln 601, ln 51, 44100/fs | DECOMP |
| 152 canais 3x Osc nos demos | Faixas, versões, shapes × nomes, igualdade do instantâneo com `0xDA/0xDB` | DADOS |

### Pendências

- Lei exata de pan do host (`host->vtbl+0xF8`) e fórmula exata do modo AM (§2.3, §2.6).
- Confirmar a ordem triângulo (1) / serra arredondada (4) lendo o gerador de tabelas do host
  (`FUN_009cdfc0`, rodada 3) ou ouvindo.
- Escala do Mod X/Y por nota (`V+0x284/0x288`).
