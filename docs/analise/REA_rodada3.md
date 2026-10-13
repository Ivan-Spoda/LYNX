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
| 0x18 | Env **sustain** | 0…128 (nível = v/127) | 50 |
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
| `+0x4C` | Sustain = `v · (1/127)` |
| `+0x50` | Incremento do release = `−1/(ticks_release + 1)` |
| `+0x70` | Env amount = `v/127` (float) |
| `+0x7C/+0x88/+0x94` | Curvas de tensão (3 × 12 B: valor, `1/|a|`, `ln(|a|+1)`) |
| `+0x60` | Delay do LFO em ticks |
| `+0x54` | Incremento do fade-in do LFO = `1/(ticks + 1)` |
| `+0x64` | Incremento de fase do LFO = `2³² / max(ticks_período, 3)` (fase de 32 bits) |
| `+0x74` | LFO amount = `v/127` |
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
a = sinal(t) · (6347^(|t|/127) − 1) · 0,125     (t = 0 → reta, y = x)
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
| Volume | `ganho = y · (1 + l · lfoAmt/127)`, com piso 0. **O amount do envelope de volume é ignorado**; o envelope multiplica direto o ganho |
| Cutoff | `y · envAmt/127 + l · lfoAmt/127` (somado ao Mod X da voz) |
| Resonance | idem com os amounts do registro 3 (somado ao Mod Y) |
| Pitch | `y · envAmt + l · lfoAmt` em **centésimos** (amount cru, ±2400) |
| Panning | `2 · l · lfoAmt/127` (a parte do envelope é aplicada em outro ponto **[HIP]**) |

**LFO:** fase de 32 bits, índice da tabela = `fase >> 18` (tabelas de 16384 pontos geradas em
`FUN_009cdfc0`: senoide, triângulo, quadrada ±1, rampa −1…1, e outras). O shape 0/1/2 escolhe uma de três
entradas de `PTR_015e3168` (na UI do FL: senoide, triângulo e pulso **[HIP para a ordem]**). O LFO começa
depois do delay e entra com fade-in linear de `1/(n+1)` por tick.

### Recomendações para o LYNX (envelopes)

1. Importador: leia os 5 registros `0xDA` na ordem Pan, Vol, Cutoff, Res, Pitch. Para um ADSR de sampler,
   use o registro 1 (Volume) se `+0x04 ≠ 0`:
   `delay/attack/hold/decay/release = 0,064·(1000^(v/65536) − 1)` s (ou em beats ×0,128 se `flags&1`),
   `sustain = v/127`, tensões `v/128`.
2. Implemente o mesmo ADSR "DAHDSR" com tensão exponencial (fórmula acima). Um tick de controle de
   ~1–5 ms com interpolação linear de ganho por amostra é fiel ao FL; dá para avaliar por amostra se
   preferir.
3. Release a partir do nível atual (não do sustain) e decay medido de 1,0 até 0.
4. O envelope de volume multiplica o ganho da voz; os demais somam `amount·y` ao destino.
