# Análise comportamental do FL Studio 2026: rodada 3 (REA)

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md` e de `REA_rodada2.md`.
> Engenharia reversa *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi
> copiado: o que está aqui são descrições, layouts de dados e fórmulas escritas com palavras nossas.
> Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a evidência.

- **Alvo:** `~/projetos/daw-analise/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA
- **Validação de formato:** os 165 `.flp` de `FL Studio 2026/Data` (3.443 canais), parser próprio de eventos
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `C+0xNN` é um offset no objeto do canal (o `local_150` do dispatcher `FUN_00f46490`);
`S+0xNN` é um offset no objeto gerador/sampler do canal (`C+0x330`, que o dispatcher chama de
`local_150[0x66]`).

---

## 1. Envelopes e LFOs do canal: evento `0xDA`

### 1.1 Onde ficam **[DECOMP+DADOS]**

- Cada canal tem **5 registros `0xDA` de 68 B** (17.215 eventos = 5 × 3.443 canais nos demos; todos com
  68 B). A ordem é fixa e vem de uma tabela de nomes (`0x01425630`):

  | Índice | Alvo | Faixa do "amount" (tabela em `0x01425658`) |
  |---|---|---|
  | 0 | **Panning** | ±64 |
  | 1 | **Volume** | ±128 (o amount não é usado; ver §1.5) |
  | 2 | **Filter cutoff** (Mod X) | ±128 |
  | 3 | **Filter resonance** (Mod Y) | ±128 |
  | 4 | **Pitch** | ±2400 (centésimos: ±2 oitavas) |

- O dispatcher copia os 68 B crus para `S+0x198 + i·0xA0` (i = contador de `0xDA` dentro do canal). Os
  0xA0 B em memória são os 0x44 do arquivo seguidos de campos derivados (§1.3).
- Arquivos com registro < 68 B (antigos): se `flags & 0x08`, a tensão do ataque vira −101; se
  `flags & 0x40`, a tensão do decay vira −101; depois esses dois bits são apagados.
- Os mesmos campos são parâmetros automatizáveis: `controlID` de canal com
  `param = 0x1000 + (alvo << 8) + índice_do_dword` (ver §2.7 da rodada 2). Os índices 8 (env amount) e
  11 (LFO amount) são bipolares com a faixa da tabela acima (`FUN_01046a70`, ramo `0x1000…0x17FF`). Toda
  escrita chama o recálculo `FUN_009ce9e0`.

### 1.2 Layout do registro (68 B, todos `i32` little-endian) **[DECOMP+DADOS]**

Defaults vêm de `FUN_009cec20` (inicialização) e batem com o valor mais comum nos demos.

| Off | Campo | Faixa | Default |
|---|---|---|---|
| 0x00 | **flags** (ver tabela abaixo) | bits | 0 (Volume: 4) |
| 0x04 | **Envelope ligado** (0 = off, ≠0 = on; o FL grava −1) | bool | 0 |
| 0x08 | Env **delay** (pré-atraso) | 0…65536 (tempo, §1.4) | 100 (= 0) |
| 0x0C | Env **attack** | tempo | 20000 |
| 0x10 | Env **hold** | tempo | 20000 |
| 0x14 | Env **decay** | tempo | 30000 |
| 0x18 | Env **sustain** | 0…128 (nível = v/128) | 50 |
| 0x1C | Env **release** | tempo | 20000 |
| 0x20 | Env **amount** | ±faixa do alvo | 0 |
| 0x24 | LFO **delay** | tempo | 100 (= 0) |
| 0x28 | LFO **attack** (fade-in) | tempo | 20000 |
| 0x2C | LFO **amount** | ±faixa do alvo | 0 |
| 0x30 | LFO **speed** (período) | tempo | 32950 (= 2 s ou 1 compasso) |
| 0x34 | LFO **shape**: 0, 1, 2 (clamp) → tabela de ondas `PTR_015e3168` | 0…2 | 0 |
| 0x38 | Tensão do **attack** | −128…+128 | 0 |
| 0x3C | Tensão do **decay** | −128…+128 | 0 |
| 0x40 | Tensão do **release** | −128…+128 | 0 (Volume nos demos: −101) |

Bits de `flags` **[DECOMP]**:

| Bit | Significado |
|---|---|
| 0x01 | **Envelope em sincronia com o tempo** (botão TEMPO do envelope) |
| 0x02 | **LFO em sincronia com o tempo** |
| 0x04 | Com o envelope **desligado**, a voz começa direto em nível 1,0 (padrão do alvo Volume). Também: envelope ligado com attack = 100 (zero) começa em 1,0 |
| 0x08 / 0x40 | Só em arquivos antigos (tensão −101 implícita, ver acima) |
| 0x20 | **LFO global**: a fase vem de um relógio global (`FUN_009ceec0`/`FUN_009cef00`, avançado pela posição da música) em vez de reiniciar em cada nota |

Nos demos: flags do Volume = 4 em 3.406 canais; outros valores vistos 0x14, 0x05, 0x26, 0x27 **[DADOS]**.
Envelope ligado: 150 canais no Volume, 51 no cutoff, 30 no pitch **[DADOS]**.

### 1.3 Campos derivados em memória (`FUN_009ce9e0`) **[DECOMP]**

| Mem | Conteúdo |
|---|---|
| `+0x58` | Delay do envelope em **ticks** (int) |
| `+0x44` | Incremento do attack por tick = `1/(ticks_attack + 1)` |
| `+0x5C` | Hold em ticks |
| `+0x48` | Incremento do decay = `−1/(ticks_decay + 1)` |
| `+0x4C` | Sustain = `v/128` |
| `+0x50` | Incremento do release = `−1/(ticks_release + 1)` |
| `+0x70` | Env amount = `v/128` (float) |
| `+0x7C/+0x88/+0x94` | Curvas de tensão (3 × 12 B: valor, `1/|a|`, `ln(|a|+1)`) |
| `+0x60` | Delay do LFO em ticks |
| `+0x54` | Incremento do fade-in do LFO = `1/(ticks + 1)` |
| `+0x64` | Incremento de fase do LFO = `2³² / max(ticks_período, 3)` (fase de 32 bits) |
| `+0x74` | LFO amount = `v/128` |
| `+0x68` | Ponteiro da tabela de onda (16384 amostras) |

### 1.4 Conversão de tempo (knob → segundos) **[DECOMP]**

`FUN_009ce950`:

```
ticks(v) = 0                                   se v < 101
ticks(v) = round( (1000^(v/65536) − 1) · E )   caso contrário
E = (PPQ/4)/48 · 24576/1000 = 0,128 · PPQ      (envelope em sincronia, flag 0x01/0x02)
E = 0,128 · PPQ · BPM/120                      (sem sincronia: tempo absoluto)
```

O envelope e o LFO avançam **um passo por tick do sequenciador** (`FUN_00d0ece0` liga a flag global
`PTR_015e1820` em cada fronteira de tick; `FUN_00d24ae0` só chama o passo `FUN_00d24500` → `FUN_009ce490`
quando ela está ligada). Como há `PPQ·BPM/60` ticks por segundo, os tempos ficam:

- **Sem sincronia (padrão):** `T = 0,064 s · (1000^(v/65536) − 1)`, independente do BPM.
  - v = 20000 → 0,463 s; 30000 → 1,448 s; 32950 → 2,000 s; 65536 → 63,9 s.
- **Com sincronia:** `T = 0,128 beats · (1000^(v/65536) − 1)`. O default do LFO (32950) dá exatamente
  **4 beats = 1 compasso**, o que confirma a escala. A 120 BPM os dois modos coincidem.
- A resolução é **1 tick** (≈ 5,2 ms com PPQ 96 a 120 BPM). Dentro do tick o ganho resultante é
  interpolado linearmente por amostra (`FUN_006a3810` em `FUN_00d24ae0`), então não há degraus audíveis
  no volume.

### 1.5 Máquina de estados da voz (`FUN_009cedb0` início, `FUN_009ce490` passo, `FUN_009cecc0` note-off) **[DECOMP]**

Estado por alvo e por voz (0x24 B em `voz+0xA8 + i·0x24`): contador, fase do LFO, contador de delay do LFO,
**estágio**, posição linear `x`, saída do envelope `y`, fade do LFO, saída do LFO.

| Estágio | Comportamento por tick |
|---|---|
| 0 Delay | conta ticks até `+0x58`; depois vai para 1 |
| 1 Attack | `x += 1/(n+1)`; ao chegar a 1,0 vai para Hold (2) ou, se hold = 0, Decay (3). `y = curva_attack(x)` |
| 2 Hold | conta ticks até `+0x5C`; depois Decay |
| 3 Decay | `x −= 1/(n+1)`, `y = curva_decay(x)`; quando `y ≤ sustain`, crava `y = sustain`, recalcula `x` pela inversa da curva e vai para 4 |
| 4 Sustain | parado enquanto a nota está pressionada |
| 6 Release | entrado no note-off a partir de qualquer estágio < 7: `x` = inversa da curva de release aplicada ao nível atual; depois `x −= 1/(n+1)`, `y = curva_release(x)` |
| 9 Fim | quando o estágio > 2 e `y` cai abaixo de 5,96e-8 (≈ −144 dB) |

Consequências práticas:
- O **decay** percorre a rampa de 1,0 até 0; o tempo real até o sustain é `T_decay · (1 − s)` (curva
  linear). O **release** começa do nível atual, então leva `T_release · nível` (linear).
- Se o envelope está **desligado**, nada disso roda (a saída do envelope fica no valor inicial: 1,0 se
  `flags & 4`, senão 0).

**Curva de tensão** (`FUN_009ce890` + `FUN_006774b0`), com `t` = tensão (−128…128):

```
a = sinal(t) · (6347^(|t|/128) − 1) · 0,1     (t = 0 → reta, y = x)
K = ln(|a| + 1),  C = 1/|a|
t < 0:  y = C · (e^(K·x) − 1)                    (começa devagar, termina rápido)
t > 0:  y = 1 − C · (e^(K·(1−x)) − 1)            (começa rápido, termina devagar)
```

As duas formas passam por (0,0) e (1,1). O release padrão do Volume (−101) é bem côncavo para baixo,
parecido com um decaimento exponencial.

### 1.6 Como a saída é aplicada (`FUN_00d24500`) **[DECOMP]**

Por tick, para cada voz (y = saída do envelope, l = saída do LFO, já multiplicada pelo fade-in):

| Alvo | Modulação |
|---|---|
| Volume | `ganho = y · (1 + l · lfoAmt/128)`, com piso 0. **O amount do envelope de volume é ignorado**; o envelope multiplica direto o ganho |
| Cutoff | `y · envAmt/128 + l · lfoAmt/128` (somado ao Mod X da voz) |
| Resonance | idem com os amounts do registro 3 (somado ao Mod Y) |
| Pitch | `y · envAmt + l · lfoAmt` em **centésimos** (amount cru, ±2400) |
| Panning | `2 · l · lfoAmt/128` (a parte do envelope é aplicada em outro ponto **[HIP]**) |

**LFO:** fase de 32 bits, índice da tabela = `fase >> 18` (tabelas de 16384 pontos geradas em
`FUN_009cdfc0`: senoide, triângulo, quadrada ±1, rampa −1…1, e outras). O shape 0/1/2 escolhe uma de três
entradas de `PTR_015e3168` (na UI do FL: senoide, triângulo e pulso **[HIP para a ordem]**). O LFO começa
depois do delay e entra com fade-in linear de `1/(n+1)` por tick.

### Recomendações para o LYNX (envelopes)

1. Importador: leia os 5 registros `0xDA` na ordem Pan, Vol, Cutoff, Res, Pitch. Para um ADSR de sampler,
   use o registro 1 (Volume) se `+0x04 ≠ 0`:
   `delay/attack/hold/decay/release = 0,064·(1000^(v/65536) − 1)` s (ou em beats ×0,128 se `flags&1`),
   `sustain = v/128`, tensões `v/128`.
2. Implemente o mesmo ADSR "DAHDSR" com tensão exponencial (fórmula acima). Um tick de controle de
   ~1–5 ms com interpolação linear de ganho por amostra é fiel ao FL; dá para avaliar por amostra se
   preferir.
3. Release a partir do nível atual (não do sustain) e decay medido de 1,0 até 0.
4. O envelope de volume multiplica o ganho da voz; os demais somam `amount·y` ao destino.

---

## 2. Volume e pan do canal: curva de ganho

### 2.1 Onde ficam **[DECOMP+DADOS]**

| Fonte | Campo | Conteúdo |
|---|---|---|
| `0xDB` (24 B) → `C+0x16C` | `i32 pan, i32 vol, i32 pitch, i32 modX, i32 modY, i32 tipo_filtro` | pan 0…12800 (centro 6400), vol 0…12800, pitch em centésimos, Mod X (cutoff, default 256), Mod Y (res., default 0), tipo de filtro (0…7) |
| `0xE5` (20 B) → `C+0x184` | `i32 panAdj, i32 volAdj, i32 ?, i32 modXAdj, i32 modYAdj` | "ajustes de nível" do canal: panAdj ±6400 (0 = neutro), **volAdj 12800 = neutro** |
| Arquivos < 5.3.7 | | pan/vol (de `0xDB` e `0xE5`) gravados em escala /100 (o load multiplica por 100) |
| Eventos legados `0x02`/`0x03` | | vol/pan em byte, convertidos por `FUN_00f3ad20`/`FUN_00f3ad40` |

Como parâmetro (`controlID` de canal, §2.7 da rodada 2): `p=0` volume, `p=1` pan, `p=2/3` Mod X/Y,
`p=4` pitch (faixa −4800…+4800), `p=0x10…0x14` os ajustes de `0xE5` **[DECOMP, `FUN_01046a70`]**.

### 2.2 Fórmula (`FUN_00fe9d20`, conferida pela API de script `getChannelVolume` em `FUN_00d59d70`) **[DECOMP]**

**É a mesma curva do fader do mixer**, só que com unity no topo do curso do canal:

```
x = vol / 12800                       (0…1)
f(x) = 1                              se x == 1
f(x) = (11^x − 1) / 10                caso contrário
ganho_canal = f(vol/12800) · f(volAdj/12800)          (gravado em C+0x19C)
dB = 20·log10(ganho)                  (a API de script devolve exatamente isso no modo 1)
```

**Resposta direta:** volume **12800 (100 %) = 0 dB**. O default **10000 (78 %) NÃO é 0 dB: é −5,18 dB**
(ganho 0,551). Tabela:

| vol | % | ganho | dB |
|---|---|---|---|
| 12800 | 100 | 1,000 | 0,0 |
| 11200 | 87,5 | 0,715 | −2,9 |
| 10000 | 78,1 | 0,551 | **−5,2** |
| 8000 | 62,5 | 0,348 | −9,2 |
| 6400 | 50 | 0,232 | −12,7 |
| 3200 | 25 | 0,082 | −21,7 |
| 0 | 0 | 0 | −∞ |

Constantes: `1/12800` (`0x013e7584`), `ln 11` (calculado em `FUN_006a6890`, `DAT_015f49ec`) e `0,1`
(`0x013e7558`). O canal não passa de 0 dB pelo knob (o mixer vai até +5,6 dB).

**Pan efetivo** (`FUN_00fe9bf0`): `p = (pan − 6400 + panAdj) / 6400` em −1…+1, gravado em `C+0x198`.
Para MIDI/plugins, também envia `p·64` arredondado em −64…+64.

**Volume para plugins (MIDI CC7):** `FUN_00fe9d20` manda `vol·volAdj/0x138800` (≈ /1.280.000), clampado a 127.

### Recomendações para o LYNX (volume)

1. Importador: `ganhoCanal = f(vol/12800)·f(volAdj/12800)` com `f(x) = (11^x − 1)/10`. Não trate 10000 como
   0 dB; projetos do FL soam 5,2 dB mais baixos no canal por padrão, e isso precisa ser preservado.
2. Pan efetivo = pan de `0xDB` + panAdj de `0xE5`.

---

## 3. Swing (global e por canal)

### 3.1 Armazenamento **[DECOMP+DADOS]**

| Item | Onde | Faixa | Default |
|---|---|---|---|
| **Swing global** ("Main swing") | evento **`0x0B`** (u8) no cabeçalho do projeto; `controlID 0x40000001`; global `DAT_0196ec34` | **0…128** (128 = 100 %) | 0 (145 de 165 demos); valores vistos 16…66 |
| **Swing mix do canal** | evento **`0x61`** (u32) → `C+0x220`; parâmetro de canal `p = 12` | 0…128 | **128** (3.235 de 3.239 canais) |
| **"Truncate swing notes"** do canal | evento **`0x30`** (u8 bool) → `C+0x224` | 0/1 | 0 |

`FUN_01311070(v)` guarda o swing e pré-calcula `DAT_0196ec30 = (ticksPorStep · v) >> 8`, com
`ticksPorStep = PPQ/4` (16 avos). A API de script `getSwing` (`FUN_00d5dbf0`) devolve `valor/128`.

### 3.2 Aplicação na reprodução de notas (`FUN_00d0c220`) **[DECOMP]**

Para cada nota de pattern que vai disparar, com `p` = posição da nota em ticks dentro da pattern e
`S = PPQ/4`:

```
k  = floor((p + S/2) / S)               (step mais próximo)
se k é ímpar (contratempo de 16 avos):
    d = |p − k·S|                        (distância até a grade)
    w = S − 2·d                          (peso triangular: S na grade, 0 a ±S/2)
    atraso = (w · swingGlobal · swingMixCanal) >> 15        [ticks]
    se "truncate" do canal: duração −= w
```

- **A 100 % (128 × 128)**, uma nota exatamente no 16 avo ímpar atrasa `S·16384/32768 = S/2`, ou seja
  **meio 16 avo = uma fusa (1/32)**. Os pares de 16 avos ficam na proporção 3:1 (pontuado), e não 2:1
  (tercina).
- Notas fora da grade recebem atraso proporcional à proximidade do step ímpar; nos steps pares o atraso é
  zero. Assim o swing também vale para notas do piano roll (o FL avisa ao abrir projetos antigos, em que
  ele só valia para os steps: string `0xf519a8`).
- O atraso é em ticks inteiros (resolução de 1 tick).
- Na conversão steps→notas (`FUN_010012e0`) o step ímpar recebe `DAT_0196ec30` inteiro (só o global,
  sem o mix do canal) mais o *shift* individual do step.

### Recomendações para o LYNX (swing)

1. Importador: swing global de `0x0B`/128, mix por canal de `0x61`/128.
2. Implemente no agendador de notas, e não por step: `atraso = (S − 2·|p − k·S|) · sg · sc / 2`
   (sg, sc ∈ 0…1), aplicado só quando o step mais próximo é ímpar. Isso reproduz o FL para steps e notas
   livres.

---

## 4. Parâmetros do canal: evento `0xD7` e eventos pequenos relacionados

### 4.1 Carga **[DECOMP]**

O dispatcher copia até **168 B** de `0xD7` para `C+0x284` (logo, offset no evento = `C+0x284+off`).
Tamanhos vistos nos demos: 168 (1.855), 125 (758), 158, 157, 156, 112 (versões antigas são prefixos).
Conversões de versão no load:

- `< 10.?` (`0x1e8c50`) com `off 0x04 ≠ 0xFF`: preenche campos legados do sampler (`S+0x23C…0x254`).
- `< 11.5.13` (`0x2dd27c`): zera os bytes `0x3D…0x3F` e normaliza `0x3C` para bool.
- `< 11.?` (`0x2dda4a`): `0x34` e `0x40` passam por `FUN_0117bc50` e são limitados a **450…1447**.
- `< 5.3.7`: `C+0x170`/`C+0x16C` ×100 (vol/pan antigos).
- `< 6.1.5` (`0x5b916d`): modo de stretch 1…2 → +2; ≥ 3 → +4.
- `< 6.1.?` (`0x5b9d21`): zera `0x04…0x07`.
- `< 11.5.13` (`0xa7ec55`): **modo de stretch** remapeado pela tabela `{0,6,6,7,7,8,9,3,4,5}`
  (`0x014e01fb`), e o resultado 5 vira 6.
- Se `(flagsA9 & 1) == 0`: o campo `0x60` (tempo do stretch) estava gravado como **int** e é convertido
  para float.

### 4.2 Layout (168 B) **[marcas por linha]**

Defaults = valor mais comum nos demos. A coluna "Evidência" diz o que foi confirmado. Os nomes marcados
[HIP/com.] batem com a nomenclatura pública da comunidade (PyFLP) **e** com a distribuição dos dados, mas
não foram vistos sendo consumidos no código.

| Off | Tipo | Campo | Default | Evidência |
|---|---|---|---|---|
| 0x00 | i32 | ? (140 em audio clips, −1 nos demais) | −1 | [DADOS] não é o BPM do projeto |
| 0x04 | i32 | campo legado (gatilho da conversão < 10) | 0 | [DECOMP] |
| 0x08 | u8 | ? (1) | 1 | [DADOS] |
| 0x0A | u8 | também escrito pelo evento `0x10` | 0 | [DECOMP] |
| 0x0B | u8 bool | também escrito pelo evento `0x0E` (1 em geradores, 0 em audio/automation) | 1 | [DECOMP+DADOS] |
| 0x0C | i32 | também escrito pelo evento `0x8D` | −1 | [DECOMP] |
| 0x10 | i32 | 60 (nota C5) | 60 | [DADOS] provável nota-raiz do teclado [HIP] |
| 0x14…0x27 | 5×f32 | 1,0 cada | 1,0 | [DADOS] |
| 0x28 | i32 | **Arp: direção** (0 = off, 1 = up, …) | 0 | [HIP/com.+DADOS] |
| 0x2C | i32 | **Arp: range** (oitavas) | 1 (audio clips: 3) | [HIP/com.] |
| 0x30 | i32 | **Arp: acorde** (−1 = nenhum) | −1 | [HIP/com.] |
| 0x34 | i32 | **Arp: time** (escala 450…1447, mesma conversão do gate) | 1024 | [DECOMP: clamp] + [HIP/com.] |
| 0x38 | i32 | **Arp: gate** (0…48; 48 = 100 %) | 48 | [HIP/com.] (mesma faixa do arp do piano roll) |
| 0x3C | u8 | Arp: slide | 0 | [HIP/com.] |
| 0x3E | u8 | "Full porta" | 1 | [HIP/com.] |
| 0x3F | u8 | "Add root" | 0 | [HIP/com.] |
| 0x40 | i32 | **Gate do canal** (450…1447; **≥ 1447 = desligado**). Parâmetro de canal `p=9`. Quando ativo, a nota é encurtada para `pos + C+0x6D8` no disparo | 1447 | [DECOMP: `FUN_00d293d0`, `FUN_01046a70`] |
| 0x44, 0x48 | 2×i32 | (0, 256) "key region"? | | [HIP/com.] |
| 0x4C | i32 | parâmetro de canal `p=10` | 0 | [DECOMP] |
| 0x50 | u8 | Normalize (precomputed) | 0 | [HIP/com.+DADOS] |
| 0x51 | u8 | Inverter polaridade (precomputed) | 0 | [HIP/com.] |
| 0x53 | u8 | **Modo de declick** (0…5) | 0 | [HIP/com.+DADOS: 0,1,3,2,5] |
| 0x54 | i32 | Crossfade do loop | 0 | [HIP/com.] |
| 0x58 | i32 | Trim | 0 | [HIP/com.] |
| 0x5C | i32 | Arp: repeat | 1 | [HIP/com.] |
| **0x60** | f32 (ou i32, ver acima) | **Stretch TIME**: duração-alvo em **1/192 de beat** (0 = desligado) | 0 | **[DECOMP]** `FUN_00fea570`, `FUN_00d26890` |
| **0x64** | i32 | **Stretch PITCH** em centésimos (±1200) | 0 | **[DECOMP]** somado ao pitch da voz em `FUN_00d26890` |
| **0x68** | i32 | **Stretch MUL** bruto: fator = `2^(v/10000)` (±20000 → ×¼…×4; 0 → 1,0) | 0 | **[DECOMP]** `FUN_01186110` |
| **0x6C** | i32 | **Modo de stretch** (tabela §5.2) | 0 | **[DECOMP+STR]** |
| 0x70 | i32 | ? | 2 | [DADOS] |
| 0x74 | i64 | ? | −2 | [DADOS] |
| **0x80** | f64 | **Início da amostra** (fração 0…1 do comprimento) | 0,0 | [DADOS: 0,057…] [HIP forte] |
| **0x88** | f64 | **Fim/comprimento da amostra** (fração 0…1) | 1,0 | [DADOS: 0,39, 0,27…] [HIP forte] |
| **0x90** | f64 | **Start offset** de reprodução = `v/máx_do_knob` (parâmetro `p=13`) | 0,0 | **[DECOMP]** `FUN_01046a70` |
| 0x98 | i32 | ? | −1 | [DADOS] |
| 0x9C, 0x9D | u8 | flags (1,1) | | [DADOS] |
| **0xA0** | f64 | **Formant** 0…1 (0,5 = "preserve"); exibido como ±1200 c; parâmetro `p=15` = `v·2400` | 0,5 | **[DECOMP]** `FUN_01046a70` (texto "preserve") e `FUN_0067db90` |

### 4.3 Outros eventos do canal que a LYNX precisa **[DECOMP+DADOS]**

| Evento | Destino | Conteúdo |
|---|---|---|
| `0x00` (u8) | `C+0x32E` | Canal **ligado** (bool; parâmetro `p=7`) |
| `0x15` (u8) | tipo | ChanType 0…5 |
| `0x16` (u8/u16) | `C+0x258` | Trilha de mixer de destino (parâmetro `p=8`) |
| `0x30` (u8) | `C+0x224` | "Truncate swing notes" (§3) |
| `0x61` (u32) | `C+0x220` | Swing mix do canal 0…128 (§3) |
| **`0x84` (u32)** | `C+0x27C`, `C+0x280` | **Cut group** = u16 baixo, **cut by** = u16 alto. Quando os dois são iguais e ≠ 0, o canal "corta a si mesmo" (`FUN_00feaaf0`; string " - cut by self"). Nos demos: `0x00010001`, `0x00020002`… |
| **`0x87` (u32)** | `S+0x15C` | **Nota-raiz da amostra** (MIDI; só gravada quando ≠ default). Valores vistos 36, 48, 59, 72, 84 [HIP forte] |
| `0x8F` (u32) | flags do sampler | bit 0 → `S+0xBD`, bit 1 → `S+0xF9`, bit 2 → `S+0xFA`, bit 3 → `S+0x14D`, bit 8 → `S+0xF8`. Audio clips: 2 / 0x102; samplers: 10 [nomes HIP] |
| **`0xDD` (9 B)** | `C+0x1EC` | **Polifonia**: `i32 máx. vozes` (0 = ilimitado, 1 = mono), `i32 tempo de slide/porta` (parâmetro `p=6`), `u8 flags`. O disparo de voz chama o limitador `FUN_00d2bd90` quando máx ≠ 0 [DECOMP parcial] |
| `0xDB` (24 B) | `C+0x16C` | pan/vol/pitch/ModX/ModY/tipo de filtro (§2) |
| `0xE5` (20 B) | `C+0x184` | ajustes de nível (§2) |
| `0xD1` (20 B) | `C+0x200` | Bloco "echo/delay" do canal: `+0x04` recebe +6400 no load; `+0x0C` nº de ecos (4); `+0x10` tempo (144). O evento legado `0x82` deriva `feedback = (128/(n+1))·n` [HIP para os nomes] |
| `0xDA` ×5 | `S+0x198` | Envelopes/LFOs (§1) |

**Não localizado nesta rodada:** a flag "Reverse" do sampler e "Keep on disk" (`KeepOnDiskBox` existe no
DFM, mas não achamos o campo; candidatos: bits de `0x8F` ou bytes `0x50…0x53`/`0x9C` do `0xD7`).

---

## 5. Time-stretch de samplers e audio clips

### 5.1 Como a voz calcula a velocidade de leitura (`FUN_00d26890`) **[DECOMP]**

Definições (por voz):
- `len` = comprimento da amostra em frames (`sample+0xA4`); `SRs` = sample rate da amostra (`sample+0x7C`).
- `k` = `1/SR_saída` (fator global em `PTR_015e2e68+0x18`), então `SRs·k` é a razão de sample rate.
- `T` = tempo-alvo em amostras de saída = `(time·(PPQ/4)/48) · amostrasPorTick` = `time/192` beats.
- `mul = 2^(MUL/10000) · fatorDoEvento`, onde `fatorDoEvento` vem do item da playlist (ver §5.3; 1,0
  para notas comuns).
- `p` = pitch total em centésimos = pitch da nota/voz + pitch do canal + **Stretch PITCH** (`0x64`).
- `FUN_0067d9b0(r)` define a velocidade de leitura `r` (amostras da fonte por amostra de saída). Para
  `r > 1` o interpolador sinc baixa o corte para `π/r` e aumenta o número de pontos (máx. 512).
- `FUN_0067db00(p, r)` = reamostragem: velocidade `2^(p/1200)·r` (pitch e duração acoplados).
- `FUN_0067db90(p, τ, f)` = **stretch em tempo real** (élastique): pitch `2^(p/1200)` (clamp 0,01…32),
  razão de tempo τ (clamp 1e-5…32) e formant `2^((1−f)·2 − 1)` (f = 0,5 → sem alteração).

| Modo (`0x6C`) | TIME = 0 | TIME > 0 |
|---|---|---|
| **< 0** (Stretch, Stretch Pro: tempo real) | `r = SRs·k/mul`; élastique com pitch `p`, τ = `1/mul`, formant | `r = len/(T·mul)`; élastique com pitch `p`, τ = `len/(T·mul·SRs·k)`, formant → **a duração segue o BPM e o pitch fica independente** |
| **0** (Resample) | reamostragem `2^(p/1200)·SRs·k`, dividida por `mul` se MUL ≠ 0 ou fator ≠ 1 | `r = len/(T·mul)` → encaixa no tempo, **o pitch acompanha** (o pitch da nota é ignorado) |
| **≥ 1** (e3, slice, auto, e2: pré-calculados) | reamostragem `2^(p/1200)·SRs·k` (sem `mul`) | `r = len/(T·mul)` sobre o buffer já esticado offline |

Os modos ≥ 1 são renderizados **offline** (`FUN_00a981f0`, unit `DSP_Stretching`: élastique, ou fatias por
região para "slice"); daí o diálogo "Restretch all channels now?". Em tempo real só tocam o buffer
pronto. A mensagem "The sample rate of this sample is too high. Choose a different time stretching option."
vem desse caminho offline.

### 5.2 Valores do modo **[DECOMP+STR, mapeamento por deslocamento: HIP forte]**

A unit tem um array fixo de 12 nomes (`0x015ca520`): `stretch pro, stretch, resample, e3 generic, e3 mono,
slice stretch, slice map, auto, e2 generic, e2 transient, e2 mono, e2 speech`. Com "resample" = 0 (o único
modo em que o código divide a reamostragem por `mul`), o valor gravado é `índice − 2`:

| Valor | Modo | Tipo |
|---|---|---|
| −2 | Stretch Pro | tempo real, com formant |
| −1 | Stretch | tempo real |
| 0 | Resample | reamostragem |
| 1 | E3 generic | offline |
| 2 | E3 mono | offline |
| 3 | Slice stretch | offline por fatias |
| 4 | Slice map | offline por fatias |
| 5 | Auto | offline |
| 6…9 | E2 generic / transient / mono / speech | offline |

Isso bate com a conversão de arquivos antigos (os valores antigos 1…6 viram 6…9 = família E2; 7…9 viram
3…5 = slice/auto) e com os demos: audio clips usam 0 (452), 5 (123), −1 (83), 2 (17), −2 (14).

### 5.3 Audio clip na playlist **[DECOMP + HIP forte]**

- O audio clip é um canal `ChanType 4` com a amostra em `0xC4`; os campos de stretch são os do `0xD7`
  desse canal. Nos demos, 128 dos 705 audio clips têm TIME > 0 (ex.: 1536 = 8 beats, 3072 = 16 beats,
  6144 = 32 beats).
- Ao disparar a voz de um item da playlist (`FUN_00d293d0`), a voz do sampler recebe a **amostra do clip**
  e um **fator de velocidade por evento** (`voz+0x39C`, double, default 1,0; `FUN_00d27bc0`). Esse fator
  multiplica o `mul` do canal em `FUN_00d26890`.
- No item de playlist de 80 B, o `f64` em `+0x40` (§4.2 da rodada 2) é 1,0 em 10.481 itens e 1,6303 /
  1,2855 em 95 itens de audio clip que foram **redimensionados com stretch** ("Stretch audio when resizing
  audio clips"). Isso indica que `+0x40` é esse fator de stretch por clip **[HIP forte]**.
- Mapeamento tempo do clip → tempo da amostra (modo com TIME > 0):
  `amostraPos = posNoClip_amostras · len / (T · mul · fatorClip)`. Como `T` é recalculado com
  `amostrasPorTick`, uma mudança de BPM muda a velocidade de leitura: o clip continua ocupando
  `time/192 · mul · fator` beats.
- Os offsets de início/fim do item (`+0x18/+0x1C`) de audio clips **não** estão em ticks simples (num demo,
  fim = 1484,9 para um item de 385 ticks com fator 1,2855). A unidade ficou em aberto.

### Recomendações para o LYNX (stretch)

1. Importador: leia `0x60` (TIME, em 1/192 beat; int se `(flagsA9&1)==0`), `0x64` (pitch c), `0x68`
   (`mul = 2^(v/10000)`), `0x6C` (modo) e `0xA0` (formant). Converta: TIME > 0 → "warp para N beats".
2. Modelo de playback: `velocidade = len/(T·mul·fator)` quando há TIME; caso contrário `mul`. Modo 0
   reamostra (pitch acompanha); modos < 0 usam stretcher em tempo real (Rubber Band / SoundTouch no
   LYNX) com pitch e formant independentes; modos ≥ 1 podem ser tratados como "stretch de alta qualidade
   offline" ou mapeados para o mesmo stretcher.
3. Guarde o fator de stretch do clip (`+0x40` do item) separado do canal; ele só escala a duração.

---

## 6. Perguntas que continuam em aberto

1. Ordem exata das formas de onda do LFO (`PTR_015e3168[0..2]`; tabelas geradas em `FUN_009cdfc0`).
2. Onde o envelope de **pan** é aplicado (o passo `FUN_00d24500` só combina o LFO de pan).
3. Flags "Reverse" e "Keep on disk" do sampler; significado dos bits de `0x8F`.
4. Unidade dos offsets de início/fim (`+0x18/+0x1C`) do item de playlist quando o item é um audio clip.
5. Campos `0x00`, `0x10`, `0x14…0x27`, `0x70…0x7C` e `0x98…0x9D` do `0xD7`.
6. Confirmar no código que o `f64 +0x40` do item vira o fator `+0x68` do evento de disparo.

## Apêndice C: funções novas (rastreabilidade)

| Endereço | Papel inferido | Confiança |
|---|---|---|
| `FUN_00f46490` (ramo `0xDA`) | Copia o registro de envelope/LFO para `S+0x198+i·0xA0` | DECOMP |
| `FUN_009cec20` | Defaults do registro de envelope/LFO | DECOMP |
| `FUN_009ce9e0` | Recalcula campos derivados (ticks, incrementos, curvas) | DECOMP |
| `FUN_009ce950` | Knob → ticks: `(1000^(v/65536) − 1)·E`, zero se v < 101 | DECOMP |
| `FUN_009ce890` / `FUN_006774b0` | Curva de tensão exponencial | DECOMP |
| `FUN_009cedb0` | Estado inicial da voz (delay/attack/flag 4) | DECOMP |
| `FUN_009ce490` | Passo por tick: DAHDSR + LFO | DECOMP |
| `FUN_009cecc0` | Note-off: entra em release a partir do nível atual | DECOMP |
| `FUN_00d24500` | Combina env/LFO nos 5 destinos da voz | DECOMP |
| `FUN_00d24ae0` | Processamento da voz: passo do envelope só em fronteira de tick; ramp linear de ganho | DECOMP |
| `FUN_00d0ece0` | Laço de ticks do motor (liga a flag de tick `PTR_015e1820`) | DECOMP |
| `FUN_009ce3f0` / `FUN_013111e0` | Atualiza PPQ e PPQ/4 globais | DECOMP |
| `FUN_00fe9d20` | Ganho do canal `f(vol)·f(volAdj)`, `f(x) = (11^x−1)/10` | DECOMP |
| `FUN_00fe9bf0` | Pan efetivo do canal | DECOMP |
| `FUN_00d59d70` | API `getChannelVolume` (normalizado ou dB) | DECOMP |
| `FUN_006a6890` | Inicialização de constantes DSP (`ln 11`, `ln 2/12` etc.) | DECOMP |
| `FUN_01311070` | Define o swing global e o atraso máximo `S·v/256` | DECOMP |
| `FUN_00d0c220` | Disparo de notas de pattern com swing triangular e mix por canal | DECOMP |
| `FUN_010012e0` | Conversão steps → notas (swing + shift por step) | DECOMP |
| `FUN_00d5dbf0` | API `getSwing` (valor/128) | DECOMP |
| `FUN_01046a70` (ramo de canal) | IDs de parâmetro do canal 0…15, 0x10…0x14, 0x1000+ (env/LFO) | DECOMP |
| `FUN_00fea570` | Setter do TIME do stretch; recalcula `mul = 2^(MUL/10000)` | DECOMP |
| `FUN_01186110` | `2^(v/10000)` | DECOMP |
| `FUN_00d26890` | Velocidade de leitura da voz por modo/TIME/MUL/pitch/formant | DECOMP |
| `FUN_0067d9b0` / `FUN_0067db00` / `FUN_0067db90` | Taxa do interpolador, reamostragem com pitch, stretcher em tempo real | DECOMP |
| `FUN_00a981f0` | Stretch offline de uma amostra (élastique / por regiões) | DECOMP |
| `FUN_00d293d0` | Dispara voz a partir de evento/item de playlist (gate, polifonia, fator de stretch) | DECOMP |
| `FUN_00d27bc0` | Construtor da voz do sampler (fator de stretch = 1,0) | DECOMP |
