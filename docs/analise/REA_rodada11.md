# Análise comportamental do FL Studio 2026: rodada 11 (REA) — Sytrus (e um resumo do Harmor)

> **Natureza deste documento.** Continuação de `REA_rodada10.md`. Engenharia reversa *clean-room*,
> apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são
> descrições, layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`),
> offsets e globais servem só para rastrear a evidência.

- **Alvos** (cópias locais em `tmp/r11/bin/`, originais em `Plugins/Fruity/Generators/<nome>/`):
  - `Sytrus/Sytrus_x64.dll` (SHA-256 `d52a97c5…4f15`, Delphi 12 x64, classe `TTestPlug`, VMT `0x860da0`)
  - `Harmor/Harmor_x64.dll` (SHA-256 `23673ad8…5aba`, Delphi 12 x64, classe `TTestPlug`, VMT `0x86dbd0`)
- **Dados:** os mesmos 165 `.flp` de `FL Studio 2026/Data` + todos os `.fst` de `Data/Patches`, seção de
  canais (antes do primeiro `0xEC`), `0xD5` após `0xC9`. Sytrus: **268 canais em 26 `.flp`** + 997 `.fst`
  (1265 blobs). Harmor: **100 canais em 21 `.flp`** + 329 `.fst` (429 blobs).
- **Parser de referência** (Python, nosso): `tmp/r11/syt.py` (Sytrus, valida os 1265 blobs com 0 byte
  sobrando), `tmp/r11/harm.py` (Harmor), `tmp/r11/ana.py` / `show.py` / `stats.py` (análise).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Método igual ao da rodada 10: VMT de `TTestPlug` achada pelo ponteiro "self" em VMT − 0xC8; entrada **[3]
= `SaveRestoreState`** (Sytrus `FUN_00862d70`, Harmor `FUN_0086f2f0`), **[6] = `ProcessParam`** (Sytrus
`FUN_00867930`), [4] = nomes/textos de parâmetro (Sytrus `FUN_00868ed0`). Ghidra 12.1.4 headless
(projeto `/tmp/r11gh`, scripts em `tmp/r11/gh/`). Constantes de tempo de execução (`ln 11`, `ln2/1200` …)
inicializadas em Sytrus `FUN_0062e5f0`.

---

## 1. Sytrus: visão geral do formato **[DECOMP+DADOS]**

Sytrus tem **10 "unidades"** no estado, cada uma num registro de 0x900 bytes a partir de `P+0x518`:
**0–5 = operadores 1–6, 6–8 = filtros 1–3, 9 = efeitos**. Cada unidade tem 10 parâmetros inteiros e uma
lista de **articuladores** (envelope + LFO + mapeamentos). O roteamento inteiro fica numa **matriz 9×9 dupla
(FM e RM)**.

### 1.1 Cabeçalho e compressão

| Off | Tipo | Conteúdo |
|---|---|---|
| 0 | u32 | versão. Se > 0xFFFF, a versão real é `valor >> 16` (o FL 2026 grava `03 00 19 00` = **0x00190003 → v25**). Nos dados: v5…v25 (`.flp`: v22 = 78, v23 = 9, v24 = 132, v25 = 49) |
| 4 | … | **v ≥ 5: o resto é um fluxo zlib** (`78 9C …`); descomprimido tem ~38–65 KB. v < 5: cru (não ocorre nos dados) |

O blob comprimido tem 977 B…13 KB (mediana 1,3 KB). Tudo a seguir é offset **dentro do fluxo
descomprimido**.

### 1.2 Layout do fluxo descomprimido (v25; desvios por versão entre parênteses)

| Ordem | Tamanho | Destino | Conteúdo |
|---|---|---|---|
| 1 | **962 B** (v<2: −162; v<1: −9) | `P+0x12C` | bloco principal, §1.3 |
| 2 | 4 (v>3) | `P+0x4F0` | inteiro (quase sempre 0) |
| 3 | 1 (v>5), 1 (v>6), 1 (v>12) | `P+0x4F4..0x4F6` | flags globais (0x4F6 é copiado para cada operador: qualidade do *band-limit* **[HIP]**) |
| 4 | 1/5/8/9 (v>13 / >14 / >17 / >18) | `P+0x4F7..` | flags: **byte 1 = PORTA** (botão `PortaBtn`, Tag 596 → parâmetro 0x254) **[DECOMP]**; byte 8 = curva alternativa do *unison pitch* (§1.8) |
| 5 | 3 × 23 | `P+0x5F30 + 0x38·k` | 3 unidades de delay: 5 × i32 (feedback, tempo, offset estéreo, volume, modo) + 3 bytes (ligado, tempo, serial) **[DECOMP+STR]** |
| 6 | 37 (v>10: 38; v<10: ausente) | `P+0x5FEC` | reverb: 9 × i32 + flags (padrões 75, 40, 0, 40, 100, 15, 40, 2, 25 = lowcut, highcut, predelay, room, diffusion, decay, damping, cor, wet) **[DECOMP]** |
| 7 | 4 + 3 × 16 (v ≤ 13: 3 × 12) | `P+0x61CC`, `P+0x6018` | modo do EQ + 3 bandas EQ (nível, freq, largura, tipo) |
| 8 | var. | — | **texto de info**: v ≥ 23: `u8 n` (255 → `u32 n`) + n caracteres UTF-16; v < 23: `i32 n` + n bytes (RTF) **[DECOMP]** |
| 9 | var. | 10 unidades | §1.4: para cada unidade u = 0…9: **10 × i32 parâmetros**; op (u < 6): bloco de 0x828 B; filtro (6–8): 0x30 B + curva do *waveshaper* + 1 B; depois os articuladores |
| 10 | 1 (v>23), 1 (v>24) | `P+0x500`, `P+0x5F18` | flags finais |

### 1.3 Bloco principal de 962 B (offsets relativos ao início do fluxo)

| Off | Conteúdo | Conversão |
|---|---|---|
| 0x000 | i32 **Master level** (Tag 0, 0…160, def 128) | **ganho = (11^(v/128) − 1)/10** (128 → 1,0; 160 → 1,9) **[DECOMP `FUN_0086a1d0`]** |
| 0x004 | i32 Master LFO level (Tag 1, ±128) | sinal·(11^(\|v\|/128) − 1)/10 (multiplica a profundidade de todos os LFOs) |
| 0x008 | i32 **Master pitch** (Tag 2, ±24) | **semitons** (interno ×100 = centésimos). Dados: 0 (187), −12 (43), −24 (14), +12 (6) |
| 0x00C | **matriz FM** 9 × 9 i32 | §1.5 |
| 0x150 | **matriz RM** 9 × 9 i32 | §1.5 |
| 0x294 | 8 × i32 (Tags 3–10) | Volume attack/decay/sustain/release, Filter attack/decay/sustain/release: escalas globais aplicadas aos envelopes (128 = ×1; sustain ±128 = deslocamento) **[STR; escala HIP]** |
| 0x2B4 | 7 × i32 (Tags 11–17) | **uníssono**: ordem (1–9), pan (0–128), volume (16–128), pitch (0–256), sub (0–96), fase (0–128), variação de envelope (0–192) — §1.8 |
| 0x2D0 | 9 × i32 | parâmetros da unidade de efeitos (pan, ordem do chorus, profundidade, velocidade, delay, spread, cross, volume, send) |
| 0x2F4 | i32 | −1 (seleção de *send* do FX) |
| 0x2F8 | 2 × i32 | Mod X / Mod Y (0…512, def 256) |
| 0x300 | 8 × i32 | estado interno (float 0,953…, flags) — ignorar |
| 0x320 | **162 bytes** | **"mudo" por célula** das duas matrizes (mesma indexação); byte ≠ 0 ⇒ a célula vale 0 **[DECOMP `FUN_008650d0`]** |

### 1.4 Unidades (10 parâmetros + blocos)

**Operadores (u = 0…5)** — `p[0..9]` (Tags do painel `OpPanel`):

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 0 | Starting phase | 0…256 (0) | fase inicial = v/256 ciclo |
| 1 | **Volume** | ±256 (256) | **ganho linear v/256** (negativo inverte a fase) **[DECOMP `FUN_00869e30`]** |
| 2 | **Pitch envelope/LFO amount** | 0…4800 (1200) | **centésimos**: alcance do articulador "pitch" (§1.6) |
| 3–9 | — | 0 | sem uso |

Bloco do operador (0x828 B, v ≥ 17; v 8–16: 0x428 com 128 harmônicas; v < 8: 0xE7 com 24) — offsets
relativos ao início do bloco:

| Off | Tipo | Conteúdo |
|---|---|---|
| 0x000 | i32 | **Freq ratio × 20000** (20000 = 1,0; 10000 = 0,5; 40000 = 2; 0 = só o offset). Dados (portadoras): 0,5 (199), 1 (144), 0,25 (120), 0 (92), 2 (22) **[DECOMP `FUN_0086a730` divide por 20000; DADOS: `Default.fst` = 20000]** |
| 0x004 | i32 | **Freq offset em Hz × 100** (±999999 → ±9999,99 Hz); com ratio 0 = **frequência fixa** **[DECOMP]** |
| 0x008 | i32 | (0/128; sem papel identificado) |
| 0x00C | 256 × f32 | **amplitude da harmônica k+1** (tabela "harmonics"). Padrão: só a 1ª = 1 (seno) |
| 0x40C | 256 × f32 | fase da harmônica k+1 (fração de meio ciclo) |
| 0x80C | 5 × i32 | **forma**: Shape (0…512), Tension (±128), Skew (0…128), Sine shaper (0…128), Pre-filter (0…128, def 128) **[DECOMP `FUN_00847df0`]** |
| 0x820 | 8 × u8 | flags: 0 Half, 1 Even, 2 Absolute (**[DECOMP]**); 3–7 = Declick/Pluck/Center/Global… (**[HIP]**, ordem não confirmada) |

**A forma de onda do operador** (`FUN_00847df0` + `FUN_00847be0`) **[DECOMP]**: a tabela é somada
harmônica a harmônica, mas cada "parcial" usa uma **forma base** escolhida por `Shape`:
`x = Shape/128` →
- `x = 0`: seno;
- `0 < x ≤ 1`: mistura seno → **triângulo** (peso x);
- `1 < x ≤ 2`: triângulo cujo pico desliza até virar **dente de serra** (x = 2 ⇒ 256 = serra);
- `2 < x ≤ 3`: serra → trapézio → **quadrada** (x = 3 ⇒ 384);
- `3 < x ≤ 4`: **pulso** cada vez mais estreito (largura `(1 − 0,75·(x−3))/2` do período; 512 ⇒ 12,5 %).

Depois vêm o *sine shaper* (dobra `sin(v·0,0368·s)`), a tensão (curva exponencial sobre a amplitude) e o
pré-filtro (corte de harmônicas por janela). Nos 1265 blobs, 6636 das 7590 tabelas são "só fundamental";
o timbre vem então de `Shape` (0 em 86 %; 256 = serra é o 2º valor mais comum) **[DADOS]**.

**Filtros (u = 6…8)** — `p[0..9]` (painel `FilterPanel`) **[DECOMP `FUN_00869e80` + STR]**:

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 0 | **Filter mode** (ordem) | 0…5 (1) | 0 OFF, 1 x1, 2 x2, 3 x3, 4 ALT x2 (dual/quad), 5 ALT x3 (triple/penta) — nº de estágios em cascata |
| 1 | **Cutoff envelope / LFO amount** | 0…256 (128) | `a = v/128` (0…2), profundidade do articulador "cutoff" |
| 2 | **Cutoff** | 0…256 (128) | `c = v/256` (0…1), posição normalizada (§1.7) |
| 3 | **Resonance / bw / feedback** | 0…256 (0) | `r = v/256` |
| 4 | Low / gain / drive | 0…128 (128) | ganho (11^(v/128) − 1)/10 |
| 5 | Band | 0…128 (0) | idem |
| 6 | High | 0…128 (0) | idem |
| 7 | Waveshaper pre amp | 64…192 (128) | v/128 |
| 8 | Waveshaper mix | 0…128 | v/128 |
| 9 | **To next filter** | ±256 (0) | sinal·(11^(\|v\|/256) − 1)/10 (filtro k → filtro k+1 em série) |

Bloco do filtro (0x30 B, v ≥ 9): +0x00 flags (HQ/FLAT), +0x04…+0x24 cópias em float dos parâmetros,
+0x28 i32 *waveshaper* ligado, **+0x2C i32 = tipo de filtro** (0…12) **[DECOMP+DADOS]**. Em seguida a
curva do *waveshaper* (um envelope §1.6, formato `TCustomMEnvelope`) e 1 byte (v ≥ 12: modo
uni/bipolar).

**Tipos de filtro** (tabela de nomes em `0x8FC850`, mesma ordem do índice) **[STR+DECOMP]**:
0 **Chocolate state variable** (usa Low/Band/High como mistura LP/BP/HP), 1 Lime low pass, 2 Lime high pass,
3 Vanilla low pass, 4 Vanilla band pass, 5 Vanilla band stop, 6 Vanilla high pass, 7 Vanilla all pass,
8 Vanilla low shelf, 9 Vanilla peaking, 10 Vanilla high shelf, 11 Mango low pass, 12 Cherry phaser.

**Efeitos (u = 9)**: os 10 `p` são sempre 0 (os parâmetros reais ficam em `0x2D0` do bloco principal).

**Articuladores**: depois dos blocos, cada unidade grava a sua lista fixa de articuladores (§1.6):
**operador = 6** (0 panning, 1 **volume**, 2 input modulation, 3 **pitch**, 4 phase, 5 plucked damp),
**filtro = 8** (0 panning, 1 volume, 2 **cutoff**, 3 resonance, 4 low/gain, 5 band, 6 high, 7 waveshaper),
**efeitos = 2** (panning, volume) **[STR (tabela `0x8FC9D0`) + DADOS (contagem que fecha os 1265 blobs)]**.

### 1.5 A matriz de modulação **[DECOMP `FUN_0086dd40`, `FUN_00865120`, `FUN_008652a0` + DADOS]**

`M[camada][linha][coluna]`, i32 ±256, camada 0 = **FM** (fase), camada 1 = **RM** (anel). **Linha =
destino**, coluna = origem:

| Linha (destino) | Colunas 0–5 | Coluna 6 | Coluna 7 | Coluna 8 |
|---|---|---|---|---|
| 0–5 (op 1–6) | FM/RM recebida do op j (j = linha ⇒ **feedback**) | **PAN** do op | **FX** (envio ao rack) | **OUT** (saída direta) |
| 6–8 (filtro 1–3) | **entrada de áudio** vinda do op j | PAN do filtro | FX | **OUT** |

- Conversão de célula: `g = sinal(v)·(11^(|v|/256) − 1)/10` (0…1). **FM**: índice = **4·g** (unidades de
  fase da tabela; **[HIP]** ciclos ⇒ ≈ 25·g rad); feedback (j = linha): **g·35/128**. **RM**: o portador é
  multiplicado por `(1 − |g|) + g·modulador`.
- PAN = v/512 (±0,5 **[HIP: ±0,5 = extremos L/R]**); OUT e FX usam a curva `g`.
- Ganho final de um operador na saída = `g(OUT) · p[1]/256 · envelope de volume`.
- Filtro k → filtro k+1 também por `p[9]` (*to next filter*).

Exemplos **[DADOS]**: `Sytrus/Default.fst` = só `M[0][0][8] = 256` (op 1 seno direto na saída);
`Default subtractive.fst` = op 1 com Shape 256 (serra) → `M[0][6][0]` → filtro 1 (Vanilla LP, x2,
cutoff 64/256, res 87/256, envelope de cutoff A 0 / D 2 s / S 0,5) → `M[0][6][8]` OUT.

### 1.6 Articuladores, envelopes e LFOs **[DECOMP `FUN_0078b4a0`, `FUN_0078bb60`, `FUN_00861180`, `FUN_00781be0`, `FUN_00789d10`, `FUN_0078a190`]**

O formato é o do editor de envelopes genérico da IL (classes `TCustomMEnvelope`/`TMEnvelope`/`TMLFO`/
`TMMap`, as mesmas do Harmor e, provavelmente, do Fruity Envelope Controller).

**Articulador** (`TOpArticulator`) = `i32 versão (3)` + `i32 flags` + `f32 profundidade do envelope` +
`f32 profundidade do LFO` + **8 partes**: envelope, LFO, mapa de teclado, mapa de velocidade, mapa Mod X,
mapa Mod Y, mapa aleatório, mapa de uníssono. (Para filtros, as duas profundidades do articulador de cutoff
são recalculadas a partir de `p[1]`; para todos, a do LFO é multiplicada pelo *Master LFO level*.)

**Envelope (cada parte)**:

| Campo | Tipo | Conteúdo |
|---|---|---|
| kind | i32 | 1 = envelope, 2 = LFO, 3 = mapa (curva) |
| versão | i32 | 3 no FL atual (0…2 em presets antigos) |
| n | i32 | nº de pontos |
| pontos | n × 24 B (v<2: 16 B; v<1: 12 B) | `f64 Δt` (tempo desde o ponto anterior), `f64 nível` (0…1), `f32 tensão` (±1, curvatura), 4 B flags (modo da curva: 0 single curve, 1 double, 2 hold, 3 stairs, 4 smooth stairs, 5 pulse, 6 wave, 7–12 variantes; arp break) |
| cabeçalho | 20 B | `+0` flags (bit 0 **TEMPO**, bit 1 **GLOBAL**, bit 2…: outros), `+4` **ligado** (LongBool: −1 = ligado, 0 = desligado), `+8` **ponto de decay**, `+12` **início do loop**, `+16` **ponto de sustain / fim do loop** (−1 = nenhum) |
| (envelope) | 4 × i32 | **knobs A/D/S/R** do articulador: tempo de ataque ×(v/128), decay ×(v/128), sustain +v/128, release ×(v/128) (padrão 128, 128, 0, 128) |
| (LFO) | 4 × i32 + i32 (v ≥ 3) | velocidade (512…65536), tensão (±128), skew (±128), largura de pulso (±112); + 1 i32 |

- **Ligado**: o campo `+4` é o *enable*: `−1` em 4 870 envelopes editados, `0` em 72 372 envelopes ainda no
  padrão (`(0,0) → (0,75; 1) → (1,5; 0,5) → (0,25; 0)`, sustain no ponto 2) **[DECOMP `FUN_00787a40`
  (0 ⇒ estado "parado") + DADOS]**. Um envelope de volume **desligado** = ganho constante enquanto a nota
  dura (com o *declick*) **[HIP]**.
- **Unidade de tempo**: `Δt` em **segundos**; com o bit TEMPO, em **tempos (beats)** — a escala de tempo é
  escolhida entre dois valores conforme esse bit **[DECOMP `FUN_00789c10`; unidade HIP]**. Coerente com os
  presets: medianas de ataque/release das portadoras com envelope ligado — Pad 1,99 s / 1,09 s; Brass 0,025 s
  / 0,06 s; Bass 0 / 0,2 s; Percussion decay 0,54 s com sustain 0 **[DADOS]**.
- **Leitura ADSR** (para o LYNX): `T_i = Σ Δt`; pico = ponto de nível máximo; **A** = T(pico) − T(0);
  **sustain** = nível do ponto de sustain; **D** = T(sustain) − T(pico); **R** = T(último) − T(sustain).
  Sem ponto de sustain = envelope "one-shot" (decai até o fim mesmo com a tecla presa).
- **LFO**: período = **625 / (10001^(v/65536) − 1)** (v = velocidade; 40898 → 2,0; 65536 → 0,0625;
  512 → ~8400) em segundos, ou em tempos com TEMPO **[DECOMP `FUN_0078a510`; unidade HIP]**. A forma do
  LFO é a própria curva de pontos (um ciclo). Ligado = mesmo campo `+4`.
- **Pitch** (articulador 3 do operador): nível 0,5 = sem desvio; desvio = **(nível − 0,5)·2·p[2] centésimos**
  **[HIP; DADOS: 296 envelopes de pitch ligados terminam em 0,5; típico = começa em 1,0 e cai para 0,5 =
  "queda de pitch"]**.

### 1.7 Cutoff em Hz e ressonância **[DECOMP `FUN_00870820`, `FUN_0086ebe0`; unidade final HIP]**

Por voz, o cutoff efetivo é **`c = p[2]/256 + saída do articulador de cutoff`** (mesma escala 0…1;
`unidade+0x890` suavizado + `voz+0x630`), e a ressonância `r = p[3]/256 + articulador`. O cálculo dos
coeficientes depende do **tipo**; cada curva é exponencial em `c`. Escrevendo em Hz a 44,1 kHz (o fator
de escala por voz em `voz+0x184` foi assumido = 1 à taxa nominal **[HIP]**):

| Tipos | Cutoff (Hz) | c = 0 / 0,25 / 0,5 / 0,75 / 1 | "Q" a partir de r |
|---|---|---|---|
| 0 Chocolate SVF | `22050·0,0396·(26^c − 1)` (índice numa tabela de 2048 coeficientes) | 0 / 1,1k / 3,6k / 9,2k / 21,8k | amortecimento `1 − 0,8·r` (1 → 0,2; FLAT ⇒ ao quadrado) ⇒ res LYNX ≈ 0,8·r |
| 1, 2 Lime LP/HP | `22050·(0,007 + 0,01324·(76^c − 1))`, teto 0,4·fs | 154 / 724 / 2,4k / 7,4k / 17,6k | `1 + 0,049·(1001^r − 1)` (1…50) |
| 3 Vanilla LP | `44100·(0,003 + 0,000665·(1501^c − 1))`, teto ω = 2,85 rad | 132 / 285 / 1,2k / 7,2k / 20k | `0,1 + 0,02495·(2001^r − 1)` (0,1…50) |
| 4–10 Vanilla BP/BS/HP/AP/shelf/peak | `44100·(0,0001 + 0,0004·(1501^c − 1))`, teto 20 kHz | 4 / 97 / 670 / 4,2k / 20k | idem Vanilla (shelf/peak usam outra curva p/ ganho) |
| 11 Mango LP, 12 Cherry phaser | curvas próprias (não tabeladas) | — | — |

Com mais estágios (`mode` 2–5) o Q é elevado à potência do nº de estágios antes de virar amortecimento.
Distribuição nos canais dos demos: c = 0,24 / 0,44 / 0,50 / 0,70 / 0,86 nos percentis 10/25/50/75/90
**[DADOS]**. Como o articulador soma direto em `c`, o envelope de cutoff com profundidade `a = p[1]/128`
desloca `c` em até `a` (Vanilla: Δc = 0,25 ≈ 2,6 oitavas) **[DECOMP para a soma; HIP para a escala de a]**.

### 1.8 Uníssono **[DECOMP `FUN_00871fd0` + DFM]**

- Ordem 1…9 (1 = sem uníssono). Nos 268 canais: 1 (164), 2 (43), 4 (18), 3 (17), 5 (10) …
- **Pitch**: `d = (11^(v/256) − 1)·5` centésimos (máx. 50 ct) ou, com a flag `P+0x4FF` (byte 8 de §1.2/4),
  `(41^(v/256) − 1)·1,875` (máx. 75 ct). 128 → ~11 ct. Cada voz recebe uma fração ±d.
- Pan: `v/128` de abertura; volume das vozes extras v/128; sub/phase/variação: ignorar no LYNX.

---

## 2. Estatística dos 268 canais Sytrus dos demos **[DADOS]** (`tmp/r11/stats.py`)

"Portadora" = op com OUT ≠ 0, ou op que alimenta um filtro cuja saída chega ao OUT (direto ou via *to next*).

| Medida | Resultado |
|---|---|
| Nº de portadoras | 0 (5), **1 (109)**, **2 (75)**, **3 (48)**, 4 (14), 5 (15), 6 (2) |
| FM forte (Σ\|g\| > 0,15 sobre portadoras) | 39 canais (21 sem filtro, 18 com) |
| FM leve (0 < Σ ≤ 0,15) | 44 |
| Sem FM | 185 (**152 com filtro**, 33 sem) |
| **"Simples"** (≤ 3 portadoras, FM ≤ 0,15, ≤ 1 filtro ativo) | **157 / 268 (59 %)** |
| RM em portadora / feedback | 23 / 11 |
| Uníssono > 1 | 104 |
| Porta ligado | 7 |
| Onda das portadoras | seno 239, **serra 134**, "rica" (tabela com muitas harmônicas) 57, triângulo 34, "escura" 32, poucas harmônicas 25, pulso 12, quadrada 9 |
| Ratios das portadoras | 1,0 (157), 0,5 (133), 0,25 (34), 0,125 (30), fixa (14), 2,0 (10), ≈0,502/1,002 (detune leve) |
| Filtros ativos (tipo, modo) | Vanilla BP (4) x1 36, Vanilla LP (3) x1 28, BP x2 24, Chocolate x0 24, Lime LP x1 22, Chocolate x1 21, BP x3 16, LP x2 16, Vanilla HP 12 |
| Envelope de volume das portadoras | ligado 431, desligado 115 |
| Envelope de cutoff dos filtros ativos | ligado 81, desligado 197 |
| A / D / S / R das portadoras (p10/25/50/75/90) | A 0 / 0 / 0 / 0,005 / 0,89 s; D 0,06 / 0,4 / 0,75 / 1,05 / 2,0 s; S 0 / 0 / 0 / 0,75 / 1,0; R 0 / 0 / 0,07 / 0,25 / 1,2 s |

Sanidade com os nomes de preset (`Plugin presets/Generators/Sytrus/<categoria>`; medianas das portadoras):
Bass → pitch −12, ratio 0,25, A 0, R 0,2 s; Pad → A 2 s, S 0,87, R 1,1 s; Percussion → S 0, D 0,5 s;
Organ → S 0,75; Brass → A 25 ms, S 1. "Bass/Sub bass" = 2 senos (ratio 0,5 e 1) direto na saída, sustain
no 1º ponto e release 0,2 s; "Bass/Deep" = op 1 (FM do op 2) → Vanilla LP x2 com cutoff 0 e envelope de
cutoff A 14 ms / D 8,4 s. Tudo coerente com as conversões acima.

---

## 3. Recomendações para o LYNX (Sytrus → sintetizador subtrativo de 3 osciladores)

Parser: `u32 versão` (`>>16` se > 0xFFFF) → zlib → bloco de 962 B → campos opcionais por versão → texto →
10 unidades (§1.2/§1.4) → articuladores (6/8/2 por unidade, 8 partes cada). Se algo não fechar, registre
no log e caia num patch padrão (seno + ADSR 0/0/1/0,1).

1. **Classificar as portadoras** (§2): para cada op `i`, `amp_i = g(M[0][i][8])·p1/256` (direto) +
   `Σ_f g(M[0][6+f][i])·g(M[0][6+f][8])·p1/256` (via filtro f). Ordenar por |amp| e **pegar as 3 maiores**
   → osc1…osc3 do LYNX (`level = min(1, |amp|)`, sinal negativo ignorado).
2. **Afinação**: `semitons = 12·log2(ratio)` (+ `Master pitch`), dividido em oitava + semitom; resto em
   centésimos (`detune`). Ratio 0 com offset f Hz: oscilador de frequência fixa — se o LYNX não suportar,
   use a nota mais próxima de C5 e marque no log. Offsets pequenos (≤ 2 Hz, comuns como "batimento"):
   `detune ≈ 1200·log2(1 + f/261,6)` ct.
3. **Forma de onda**:
   - Tabela só com a fundamental → pela `Shape`: 0 → `sine`; 1…191 → `triangle`; 192…319 → `saw`;
     320…447 → `square`; ≥ 448 → `square` (pulso estreito, sem PWM).
   - Tabela rica: ~1/k → `saw`; só ímpares ~1/k → `square`; ímpares ~1/k² → `triangle`; outras: `saw` se a
     energia acima da 4ª harmônica for alta, senão `triangle` (classificador em `tmp/r11/cls.py`).
   - **FM forte** sobre a portadora (Σ\|g\| ≥ 0,15, sem contar feedback): troque `sine` por **`saw`** se o
     modulador tiver ratio inteiro par/ímpar misto (≈ espectro completo), **`square`** se a razão
     modulador/portadora for 2 ou ímpar-sobre-par (espectro ímpar), e acrescente um **envelope de filtro**
     imitando o envelope de volume do modulador (abre o LP do LYNX `+4·Σg` oitavas, decay = decay do
     modulador). Feedback `g > 0,3` num seno → `saw`. Ratio não inteiro → som metálico: use o 3º oscilador
     como `sine` no ratio do modulador com nível `min(1, Σg)` (aproximação aditiva).
   - **RM** relevante (`|g| ≥ 0,2`): use o **AM do LYNX** (osc3 modula) com o modulador no osc3.
4. **Pan / nível**: pan do LYNX = `2·M[0][i][6]/512` (±1). Níveis de §3.1 × `Master level` (§1.3).
5. **Envelope de amplitude**: do envelope "volume" (articulador 1) da portadora mais alta, convertido como
   em §1.6 (A, D, S = nível no ponto de sustain relativo ao pico, R). Se estiver **desligado**:
   `A = 0, D = 0, S = 1, R = 0,01`. One-shot (sem sustain): `S = 0`, `D = tempo até o último ponto`. Bit
   TEMPO ligado: multiplique por `60/BPM`. Aplique os knobs do articulador (×v/128) e os globais "Volume
   A/D/S/R" (Tags 3–6) como multiplicadores **[HIP]**.
6. **Filtro**: use o **primeiro filtro ativo** (mode ≠ 0, com entrada de alguma portadora e OUT/next ≠ 0):
   - tipo: 1, 3, 11 → `LP`; 2, 6 → `HP`; 4 → `BP`; 5 → `notch`; 0 (Chocolate SVF) → pelo maior de
     Low/Band/High (`p4/p5/p6`) → LP/BP/HP; 7 (all pass), 8–10 (shelf/peak), 12 (phaser) → sem filtro
     (ou LP aberto) e log.
   - cutoff = tabela de §1.7 pelo tipo; ressonância LYNX: converta o "Q" de §1.7 com
     `res = clamp((Q − 0,707)/8, 0, 0,95)` (Vanilla r = 0 dá Q 0,1 = LP bem suave → res 0); `mode` 2…5
     (mais estágios) → resposta mais íngreme: no SVF de 12 dB do LYNX, compense descendo o cutoff ~10 % por
     estágio extra **[HIP]**.
   - portadoras que **não** passam pelo filtro: se forem minoria em nível, ignore-as; se forem maioria,
     não aplique o filtro (o LYNX tem um filtro só).
7. **Envelope de filtro**: do articulador "cutoff" (2) do filtro escolhido, se ligado: A/D/S/R como §1.6;
   amount em oitavas = `log2(fc(c + a·nível_pico) / fc(c + a·nível_inicial))` usando a curva do tipo
   (§1.7, a = p1/128); sustain do LYNX = nível de sustain relativo ao pico. Assim o LYNX reproduz a
   abertura real em Hz, inclusive nos patches com cutoff 0 e envelope forte ("Bass/Deep").
8. **Envelope de pitch**: do articulador "pitch" (3) da portadora principal, se ligado: amount (semitons) =
   `(nível inicial − 0,5)·2·p2/100`, decay = tempo até voltar a 0,5, attack/hold 0.
9. **LFOs** (4 no LYNX): para os articuladores com LFO ligado — volume da portadora → LFO de volume,
   cutoff do filtro → LFO de cutoff, pitch → LFO de pitch, panning → LFO de pan. Velocidade = 1/período
   (§1.6), forma `sine` (ou `triangle`/`square` se a curva de pontos tiver 3/2 pontos com degraus).
10. **Uníssono**: ordem ≥ 2 e só 1–2 portadoras ⇒ ocupe os osciladores livres com a mesma onda e detune
    `0, +d, −d` (d de §1.8, mínimo 5 ct), pan `±p_pan/128`.
11. **Mono / portamento**: porta = byte 1 de `P+0x4F7` (§1.2). Mono/legato vêm dos eventos de canal do FL
    (polifonia do canal), não do blob — continue aplicando-os como para os outros geradores.
12. **Efeitos internos** (chorus, 3 delays, reverb, EQ): ignorar e registrar no log (ou mapear para FX de
    canal, se existirem).
13. **Ordem de prioridade** para implementar: (a) parser + portadoras + ratio/pitch + onda pela Shape +
    envelope de volume (cobre os 157 patches "simples"); (b) filtro 1 + envelope de cutoff; (c) FM → brilho;
    (d) uníssono e LFOs.

---

## 4. Harmor (resumo) **[DECOMP+STR+DADOS]**

Harmor é o "irmão grande" do Harmless (rodada 10 §1): síntese aditiva com 2 partes (A/B), timbres
desenhados como curvas por harmônica, 2 filtros "de curva", phaser, harmonizador, *blur*, imagem/
ressíntese e rack de FX. Os nomes, faixas e defaults vêm do DFM `TSynthEditorForm` (`tmp/r11/harmor.dfm.txt`).

### 4.1 Layout do `0xD5` (`SaveRestoreState` = `FUN_0086f2f0`, partes em `FUN_0086ed30`)

**Não é comprimido.**

| Ordem | Conteúdo |
|---|---|
| u32 | versão: `03 00 VV 00` → v = `>>16` (dados: v19 = 44, v18 = 53, v16/17 = 3 nos `.flp`; v11/12 em presets antigos) |
| u32 | **máscara**: bit 0 = parte A presente, bit 1 = parte B, bit 2 = seção global (sempre 7 nos dados) |
| por parte | `u32 tamanho` do bloco (permite pular) + **386 × i32 parâmetros da parte** (v < 16: 385) + 0x30 B + 0x4B B (v < 11: 0x47) de combos/estado + **43 articuladores** (formato §1.6 com **11 partes**: envelope, LFO e 9 mapas) + **26 curvas de "shaping"** (envelopes simples) + objeto de imagem + string (arquivo da imagem). Em v < 16 o articulador 33 não é gravado |
| global | 53 × i32 (Tags 10000–10016 e 20000–20035, na ordem) + 6 × i32 + 0x3D B + texto (como §1.2/8) + 1 B flags |

Validação: com esse layout, os 682 blocos de parte v ≥ 16 fecham exatamente **[DADOS]**.

### 4.2 Parâmetros úteis (índice no array da parte = **Tag do DFM**) **[STR+DADOS]**

| i | Nome | Faixa (def.) | Uso no LYNX |
|---|---|---|---|
| 4 | Timbre 1 & 2 mix | 0…1000 | quase sempre 0 (só timbre 1) |
| 11 | **Part volume** | ±2000 (1000) | nível da parte = v/1000 |
| 34 | **Unison order** | 1…9 | uníssono |
| 37 / 38 | Unison panning (0…256) / pitch thickness (0…256, def 128) | | como Harmless §1.2 |
| 40 / 41 | **Freq multiplicator** (×10000) / divider | 10000…160000 / 1…9 | ratio da parte = (v40/10000)/v41 |
| 44 | Pitch articulator amount | ±6000 (6000) | alcance do envelope de pitch (ct) |
| 49, 50 | Portamento time / limit | | |
| 52 / 64 | **Filter 1 / 2 frequency** | 0…8192 | mesma escala do Harmless: `f ≈ 73,4·2^(7,855·v/8192)` Hz **[HIP: herdada]**; 8192 = aberto (41 % das partes) |
| 56 / 68 | Filter env amount | ±1000 | |
| 57 / 69 | Filter resonance | 0…1000 | `r = v/1000` |
| 74 | Filter 1 & 2 mix (paralelo ↔ série) | ±1000 | |
| 75 | Pluck decay | 0…1000 | 1000 = desligado |

Tipos de filtro: inteiros 13 e 14 do bloco de 0x4B B (`F1`, `F2`), índices **sem separadores** da lista do
DFM (0 Classic LP, 1 BP, 2 BS, 3 HP, 4 Crude LP … 13 Leaky LP, 14 Phaser, 15 Band phaser, 16 Flat BP,
17/18 Custom) — nos dados F1 = 4 (Crude LP, 60) / 0 (Classic LP, 29) **[HIP]**.

Globais (`g[i]` = Tag 10000 + i): 4 Legato, 5 Portamento, 10 **Part A & B mix** (0…1000, def 500),
11 **Main volume** (0…2000, def 1000), 12 Post-FX volume, **14 Main pitch** (±2400 ct; −2400 em 10 %).

Articuladores da parte (ordem do combo `ArtSelect`, sem separadores): 0 panning, **1 volume**, 2 FX/dry,
3 timbre mix, **4 filter 1 freq**, 5 filter 2 freq, … **24 pitch**, 25 vibrato, 26 unison pitch, …;
mapeamentos 31–42 (F1/F2 env amount, global env A/D/S/R, global LFO …). As 26 curvas de *shaping*
começam por local EQ, harmonic phase, **timbre 1 harmonic level**, **timbre 2 harmonic level** (nível por
harmônica, x = harmônica normalizada) **[STR]**.

Dados (100 canais): envelope de volume ligado em só 45/200 partes (o resto usa o gate + "global envelope"
dos mapeamentos); quando ligado: A 0 (mediana) / D 2 s / S 0,5 / R 0,74 s.

### 4.3 Aproximação crua no LYNX

- Uma parte = um oscilador `saw` (timbre padrão do Harmor = serra; se a curva "timbre 1 harmonic level"
  cair só nas ímpares → `square`), nível = part volume × (1 − mix A/B ou mix), pitch = Main pitch +
  `12·log2(ratio)`. Parte B no osc2 com o mesmo critério.
- Filtro 1 → LP/BP/HP/notch do LYNX pelo tipo; cutoff pela escala do Harmless; ressonância v/1000.
- Envelope de volume: articulador 1 se ligado (§1.6), senão `A 0 / S 1 / R 0,1`.
- Uníssono: como o Harmless (rodada 10 §1.7/2).
- Imagem, *blur*, *prism*, harmonizador, phaser e FX: ignorar e registrar.

---

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| Sytrus VMT `0x860da0` (`tmp/r11/vmt12.py`) | Localiza `TTestPlug` e as classes de envelope | DECOMP |
| `FUN_00862d70` (VMT+3) | `SaveRestoreState`: versão, zlib, 962 B, flags, delays, reverb, EQ, texto, 10 unidades, articuladores | DECOMP |
| `FUN_00867930` (VMT+6), `FUN_00868c90` | `ProcessParam` e o decodificador índice → (unidade, articulador, controle); offsets 0x12C/0x3C0/0x3E0/0x3FC/0x424/0x6018 | DECOMP |
| `FUN_008681e0`, `FUN_00868ed0` | Texto/nomes dos parâmetros; tabelas `0x8FC9D0` (articuladores), `0x8FC8C8` (modos de filtro), `0x8FC850` (tipos) | DECOMP+STR |
| `FUN_0062e5f0` | Constantes `ln 11`, `ln2/1200` … | DECOMP |
| `FUN_0086a1d0`, `FUN_00869e30`, `FUN_0086a730`, `FUN_00869e80` | Master vol/LFO/pitch; volume e pitch range do op; ratio/20000 e offset Hz·100; parâmetros do filtro | DECOMP |
| `FUN_008650d0`, `FUN_00864ff0`, `FUN_00865120`, `FUN_008652a0` | Indexação da matriz + mudo; curva de ganho; colunas PAN/FX/OUT; roteamento op→filtro | DECOMP |
| `FUN_0086dd40` | Montagem da voz: FM (×4), feedback (×35/128), RM, entradas de filtro | DECOMP |
| `FUN_00848780`, `FUN_008477f0`, `FUN_00847df0`, `FUN_00847be0`, `FUN_00847900` | Tabela de onda do op: harmônicas, Shape (seno→tri→serra→quadrada→pulso), tensão, sine shaper, band-limit | DECOMP |
| `FUN_0078b4a0`, `FUN_0078bb60`, `FUN_00861180` | Serialização dos articuladores (8 partes) | DECOMP |
| `FUN_00781be0`, `FUN_00789d10`, `FUN_0078a190`, `FUN_0078b2a0` | Serialização de envelope/LFO/mapa (pontos f64, cabeçalho de 20 B, knobs) | DECOMP |
| `FUN_007849c0`, `FUN_007849a0`, `FUN_00787a40`, `FUN_00789de0`, `FUN_00789c10` | Padrões do cabeçalho; ligado (+4); pontos decay/loop/sustain; knobs/128; bit TEMPO | DECOMP |
| `FUN_0078a510`, `FUN_00818400` | Velocidade do LFO → período; exibição em tempo/compasso com TEMPO | DECOMP |
| `FUN_00871fd0`, `FUN_00865a90` | Uníssono (pitch 11^/41^, pan, volume); padrões do reverb | DECOMP |
| `FUN_00872290`, `FUN_00870820`, `FUN_0086ebe0` | Suavização do cutoff/res; soma base + articulador; coeficientes por tipo de filtro (curvas 26^, 76^, 1501^; Q 2001^/1001^) | DECOMP |
| DFM `TSynthEditorForm` do Sytrus (`tmp/r11/sytrus.dfm.txt`) | Tags, faixas e defaults; partes do articulador | STR |
| 1265 blobs Sytrus (268 em `.flp`) | Layout fecha em todos; estatística §2; sanidade por categoria de preset | DADOS |
| Harmor VMT `0x86dbd0`; `FUN_0086f2f0`, `FUN_0086ed30` | Layout por parte/global, versões | DECOMP |
| DFM do Harmor (`tmp/r11/harmor.dfm.txt`) + 429 blobs | Índice = Tag; articuladores de 11 partes; estatística §4.2 | STR+DADOS |
| Scripts `tmp/r11/syt.py`, `ana.py`, `show.py`, `stats.py`, `cls.py`, `harm.py` | Parser, análise e estatística | DADOS |

### Perguntas em aberto

- Cutoff do Sytrus (§1.7): confirmar o fator por voz `voz+0x184` (= 44100/fs? 1/oversampling?), as
  curvas de Mango/Cherry e a escala da profundidade `a` do envelope de cutoff.
- Unidade do índice FM (ciclos × radianos) e referência de afinação do ratio 1,0 (nota MIDI ↔ Hz).
- Unidades de tempo dos envelopes (segundos sem TEMPO é o mais provável; confirmar com render).
- Como os knobs globais "Volume/Filter A/D/S/R" (Tags 3–10) e os knobs A/D/S/R do articulador escalam o
  tempo (linear v/128 assumido).
- Significado dos flags 3–7 do operador (declick, pluck, center, global) e do i32 em +0x008 do bloco do op.
- Filtro com `mode = 0` (OFF): passa sem filtrar ou silencia?
- Harmor: escala de frequência dos filtros (assumida igual ao Harmless), significado dos inteiros 15/16 do
  bloco 0x4B, conteúdo do objeto de imagem, e o timbre padrão (confirmar que é serra).
- Validação sugerida: renderizar no FL `Sytrus/Default subtractive`, `Bass/Sub bass`, `Bass/Deep`,
  `Pad/*` e comparar com o LYNX (envelope e espectro); variar o cutoff de 0 a 256 num patch de serra
  para medir a curva de Hz.
