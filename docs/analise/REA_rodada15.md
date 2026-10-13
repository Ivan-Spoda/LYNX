# Análise comportamental do FL Studio 2026: rodada 15 (REA) — efeitos que o import ainda descarta, Envelope Controller, X-Y/Control Surface e a ordem dos parâmetros de PoiZone, Toxic Biohazard e BassDrum

> **Natureza deste documento.** Continuação de `REA_rodada14.md` (Patcher, Gross Beat, controladores) e
> `REA_rodada13.md` (geradores menores). Engenharia reversa *clean-room*, apenas comportamental. Nenhum código
> nem pseudocódigo da Image-Line foi copiado: o que está aqui são descrições, layouts de dados e fórmulas
> escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a
> evidência. Nenhum conteúdo cifrado foi tocado nesta rodada (todos os estados abaixo são blobs `0xD5`
> abertos, gravados pelos próprios plugins).

- **Alvos** (cópias locais em `tmp/r15/bin/`, originais em `Plugins/Fruity/Effects/<nome>/` e
  `Plugins/Fruity/Generators/<nome>/`):

  | Plugin | DLL (SHA-256) | Linguagem | Estado (`SaveRestoreState`) / texto dos parâmetros |
  |---|---|---|---|
  | Pitch Shifter | `e7dc07f6…d10c` | Delphi | `FUN_0072c630` (VMT+3) / `FUN_0072d5b0` |
  | Frequency Shifter | `5aab5d96…59ca` + `Engine_x64.dll` `a7774536…d8fa` | Delphi + motor C++ | `FUN_006dd080` / motor `dispatch` (op 5 = nome, op 6 = texto `FUN_1800020a0`) |
  | Emphasis | `e64ca492…5e48` | Delphi | `FUN_00706090` / `FUN_00706ba0` |
  | Low Lifter | `a492a7aa…fa96` | Delphi | `FUN_006c6d50` / `FUN_006c7490` |
  | Fruity Squeeze | `ed797a2f…57dd` | Delphi | `FUN_006d30c0` / `FUN_006d3a50` |
  | Transporter | `12d1116e…adda` | Delphi | `FUN_00713760` / `FUN_00713f40` |
  | Vintage Phaser | `3f7c558a…0d0f` (+ `engine_x64.dll`) | Delphi + motor C++ | `FUN_006ea8e0` / `FUN_006eb7f0` |
  | Fruity Send | `03add028…4f09` | Delphi | `FUN_006bd0f0` / `FUN_006bd930` |
  | Pitcher | `965db573…a73d` | C++ | (rodada 14) + `setParam` `FUN_18001cab0`, escala `FUN_18001d890` |
  | Fruity Envelope Controller | `e60f7462…c763` | Delphi | `FUN_008ad310` / `FUN_008aef80`; voz `FUN_008af660`; *tick* `FUN_008afec0` |
  | Fruity X-Y Controller | `c78e1ff2…c19b` | Delphi | `FUN_006c5d20` / `ProcessParam` `FUN_006c6650` |
  | Control Surface | `ad086c77…ed6c` | Delphi | `FUN_00854f70` → escritor `FUN_00855120` |
  | PoiZone (motor) | `engine_x64.dll` `1208a111…e518` | C++ | estado `FUN_180031020`, nomes `FUN_180030920`, texto `FUN_18002f810` |
  | Toxic Biohazard | `5c326637…cb46` | C++ | nomes `FUN_18003b6e0`, estado `FUN_180041b00` (classe `ToxicSynth`) |
  | BassDrum (motor) | `engine_x64.dll` `2fb1b2f8…5e8b` | C++ | estado `FUN_18002bce0`, nomes no construtor `FUN_18002a380`, texto `FUN_18002ace0` |

- **Ferramentas:** Ghidra 12.1.4 *headless* em projetos próprios (`/tmp/r15gh_<dll>`), scripts em
  `tmp/r15/gh/scripts/` (`Decomp`, `Dis`, `StrRefs`, `ScalarFind`, `DecompGrep` das rodadas 12–14, mais `Sym`
  — lista símbolos/vtables RTTI). DFM lidos com `tmp/r14/tools/dfm.py` + filtro `tmp/r15/dfmc.py` (Tag = índice
  do parâmetro). Campos publicados das classes Delphi (offset → nome do controle) com `tmp/r15/fields.py`
  (tabela de campos em VMT−0xA0). VMT de `TTestPlug` com `tmp/r14/tools/vmts.py` + `tmp/r15/vmt.py`.
- **Coleta:** `tmp/r14/blobs14.pkl` (slots do mixer, canais, `.fst`). Decodificadores de apoio desta rodada:
  `tmp/r15/dump.py` (blob → palavras), `tmp/r15/envctl.py` (Envelope Controller completo), `tmp/r15/cs.py`
  (chunks do Control Surface), `tmp/r15/count.py` (contagem).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/disassembly da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

**Convenções.** `x` = valor normalizado do parâmetro, `(v − mín)/(máx − mín)` com a faixa do controle no DFM
(é assim que todos os plugins Delphi desta rodada calculam o texto: `(v − ctrl+0x3A8)·ctrl+0x3CC`, recortado em
0…1). `lerp(a, b, c)` = `a + (b − a)·x^(1/c)` (forma usada por Emphasis/Low Lifter: `exp(ln(x)/c)`).
`curve11(x)` = `(11^x − 1)/10` — a curva de volume do FL, que o LYNX já tem como `curve(11, x)` em
`FlEffects.cpp`.

### Contagem nos dados (prioridade) **[DADOS]**

Slots do mixer em `Data/Demo projects/**.flp` (entre parênteses: todos os `.flp` de `Data`).

| Plugin | Slots nos demos | (todos) | Arquivos demo | `.fst` | Tamanho do `0xD5` |
|---|---|---|---|---|---|
| Pitcher | 39 | (61) | 7 | 4 | 160 B |
| **Pitch Shifter** | **14** | (15) | 6 | 31 | 75 B |
| Frequency Shifter | 4 | (5) | 3 | 13 | 52 B |
| Emphasis | 4 (todos no Master) | (5) | 4 | 26 | 64 B (v8) / 67 B (v10) |
| Low Lifter | 3 | (4) | 2 | 12 | 82 B |
| Fruity Squeeze | 1 | (1) | 1 | 10 | 52 B |
| Transporter | 1 | (2) | 1 | 11 | 110 B (v8) / 85–94 B |
| Vintage Phaser | 1 | (1) | 1 | 21 | 149 B (v3) / 132 B (v1) |
| Fruity Send | 1 | (1) | 1 | 4 | 16 B |
| Fruity X-Y Controller | 1 | (19) | 1 | 9 | 74–81 B |
| Control Surface | 3 | (17) | 3 | 4 | 80 B…29 KB |
| Fruity Formula Controller | 2 | (2) | 1 | 23 | — |

Geradores (canais em `.flp` dos demos): Fruity Envelope Controller 4 (+26 `.fst`), Toxic Biohazard 3, BassDrum 3
(+2 em templates), PoiZone 1 (+389 `.fst`).

---

## 1. Pitcher — os parâmetros que faltavam (21, 22, 25…33) **[DECOMP `FUN_18001cab0`, `FUN_18001d020`, `FUN_18001d890` + DADOS]**

Complementa `REA_rodada14.md` §3. Os 34 `f32` do estado são valores **crus** (não normalizados): 0…1 para os
contínuos, inteiros para os seletores. O `setParam` (`FUN_18001cab0`) e a tabela de padrões (`FUN_18001d020`)
dão o significado:

| i | Nome (registro / dedução) | Valor | Efeito no motor |
|---|---|---|---|
| 0 | Correction Speed | 0…1 (padrão 1) | coeficiente de correção ∝ `8,82 / (sr·(3·(1−v)³ + 0,001))`: v = 1 → correção instantânea; v = 0,6 → constante de tempo ≈ 0,07 s **[DECOMP; unidade HIP]** |
| 2 | Gender | 0…1 (0,5) | deslocamento de formante (0,5 = neutro) |
| 3 | Fine Tune | 0…1 (0,5) | `2v − 1` (±1, fração de semitom **[HIP]**) |
| 4 | Bypass All | 0/1 | liga/desliga a correção |
| 5…16 | Key C…B Bypass | 0/1 | máscara manual de notas (recalcula a máscara de escala) |
| 17…20 | Voice 1–4 Pan | 0…1 (0,5) | pan das vozes de harmonia |
| 21 | — | sempre 0 nos dados | não tratado pelo `setParam` (só armazenado) |
| 22 | (botão) | 0/1 | liga um modo e força reinício interno — candidato: **Harmonize** **[HIP; STR `BTN Harmonize.tga`]** |
| 23 | Note A Freq | 0…1 (0,5) | frequência do Lá de referência (0,5 = 440 Hz **[HIP]**) |
| 24 | Pitch Reference | 0…1 | `int(2v)` → 0/1/2 |
| 25 | (botão) | 0/1 | quando 1, a máscara de escala **não** é recalculada pelos parâmetros 28/29 → notas vêm do MIDI: **MIDI** **[HIP forte; STR `BTN Midi.tga`]** |
| 26, 27 | (botões) | 0/1 | duas opções booleanas (candidatas: **Formant**, *stereo*/outra) **[HIP; STR `BTN Formant.tga`]** |
| 28 | **Tônica da escala** | inteiro 0…11 (C = 0) | rotaciona a máscara da escala (`FUN_18001d890`) **[DECOMP]** |
| 29 | **Tipo de escala** | inteiro (padrão 3) | 0 = **menor natural**, 1 = **maior**, 2 = C, D♯, F, G (4 notas), 3 = **cromática** (todas), 4 = só a tônica, 1000 = nenhuma **[DECOMP]** |
| 30, 31 | — | sempre 0 | não tratados |
| 32 | **Faixa de voz** | inteiro 0…5 (padrão 2) | frequência mínima de detecção: 220, 170, **110**, 80, 55, 25 Hz (soprano → muito grave) **[DECOMP]** |
| 33 | Porta MIDI | 0…1 | `int(256·v) − 1` (−1 = nenhuma) **[HIP; STR "MIDI Input Port Number:"]** |

Os 12 bytes entre os floats (offset 136 do bloco) são a **máscara de notas resultante** (1 byte por classe de
altura, C…B). Conferido: tônica 6 + escala 0 ⇒ máscara `0 1 1 0 1 0 1 0 1 1 0 1` = **Fá♯ menor** (13 slots
dos demos); tônica 3/escala 0 = Ré♯ menor; tônica 4/escala 0 = Mi menor; tônica 7/escala 1 = Sol maior
**[DADOS]**.

**Aproximação no LYNX:** continua sem equivalente (correção de afinação por detecção). Se `Gender = 0,5`,
`Bypass All = 0` e o modo MIDI/harmonia estiver desligado (22 = 25 = 0), passar o sinal reto **sem aviso
alarmante** (num vocal já afinado o efeito audível é pequeno). Com harmonia/MIDI ligados, registrar no log.

---

## 2. Pitch Shifter **[DECOMP `FUN_0072c630`, `FUN_0072d5b0`, `FUN_0072cda0` + STR + DADOS]**

Deslocador de afinação com dois motores: **FFT** (com preservação/deslocamento de formantes) e **granular**
(grãos com duração, densidade e aleatoriedade), e um *delay* opcional antes/depois do deslocamento.

### 2.1 Layout do `0xD5` (75 B) **[DECOMP + DADOS]**

| Off | Tipo | Conteúdo |
|---|---|---|
| 0 | i32 | versão (7; o leitor aceita 6 e 7) |
| 4 | 13 × i32 | **parâmetros 0…12** (Tag do DFM) |
| 56 | i32 | posição do controle *Formant correction* (cópia do parâmetro 11) |
| 60 | u8 | **Tempo sync** do delay (checkbox `TempoSyncCheckBox`) |
| 61 | i32 | **motor** (`PluginTypeSelect`): 0 = FFT, 1 = GRANULAR |
| 65 | i32 | qualidade do detector de altura (`PitchDetectorCombo`): 0 = rápida, 1 = precisa |
| 69 | i32 | velocidade do espectro (500/1000/2000 = rápida/média/lenta — só UI) |
| 73 | u8, u8 | espectro ligado, monocromático (só UI) |

Os nomes dos campos vêm da tabela de campos publicados de `TSynthEditorForm` (offsets 0x6F8, 0x718, 0x810,
0x818) **[DECOMP+STR]**. Versão 6 não tem os 3 últimos campos.

### 2.2 Parâmetros e conversões (`FUN_0072d5b0`) **[DECOMP]**

| i | Nome | Faixa (padrão) | Valor real |
|---|---|---|---|
| 0 | **Pitch** | −1200…1200 (0) | `24x − 12` semitons = **v/100 semitons** (centésimos) |
| 1 | **Formant** | −1200…1200 (0) | idem, deslocamento de formante (só no modo FFT) |
| 2 | Analysis channel | 0…2 | LEFT / RIGHT / LEFT+RIGHT |
| 3 | **Mix** | 0…2400 (2400) | `100x` % de sinal processado |
| 4 | Duration (grão) | 2000…15000 (8500) | `20 + 130x` ms → **20…150 ms** (padrão 85 ms) |
| 5 | Density | 100…500 (200/300) | `100 + 400x` % (= v %) — sobreposição dos grãos |
| 6 | Jitter | 0…100 | % (aleatoriedade da posição do grão) |
| 7 | Rand | 0…100 | % (aleatoriedade de afinação/tempo do grão **[HIP]**) |
| 8 | Delay | 1…768 (768) | sem *tempo sync*: `1 + 999x` ms; com sync: `v/48` *steps* (768 = 16 steps = 1 compasso) |
| 9 | Feedback | 0…100 | % (realimentação do delay) |
| 10 | Delay mode | 0…3 | OFF / POST (delay depois do shift) / PRE (antes) / NO SHIFT (só delay) |
| 11 | Formant correction | 0…65536 (65536) | `100x` % (quanto o motor FFT preserva o envelope espectral) |
| 12 | Pitch mode | 0…3 | RELATIVE / ABSOLUTE C4 / C5 / C6 (absoluto: força a altura detectada para a nota **[HIP]**) |

O `ProcessParam` redesenha a visualização quando muda o parâmetro 0, o 1 no modo FFT ou o 4 no modo granular
— confirma quais parâmetros pertencem a cada motor **[DECOMP]**.

### 2.3 Nos demos (14 slots) **[DADOS]**

Pitch: 0 (5×, provavelmente automatizados), ±12 st (6×), +8, +2, +10,15, −5. Motor: FFT 7, granular 7. Delay
mode = OFF em **todos**. Formant ≠ 0 em 4 (−3, −12, −1,65, −5 st). Mix < 100 % em 4 (27 %, 34 %, 51 %, 60 %).

### 2.4 Comportamento para implementar (algoritmo) **[HIP: descrição do efeito audível, não do código]**

- **Granular (modo 1):** buffer circular; grãos de `D` ms com janela de Hann lidos a uma taxa `r = 2^(st/12)`;
  um grão novo começa a cada `D/(densidade/100)` ms (densidade 200 % = 2 grãos sobrepostos); *jitter* desloca o
  início de leitura aleatoriamente até `jitter% · D`; *rand* varia levemente `r`/duração por grão. É o mesmo
  princípio de duas cabeças em *crossfade* do `PitchShift` atual do LYNX (janela = `D`).
- **FFT (modo 0):** *phase vocoder* com deslocamento de bins por `r` e, independentemente, deslocamento do
  envelope espectral por `2^(formant/12)`; *formant correction* = quanto do envelope original é reaplicado após o
  deslocamento (100 % = timbre preservado, 0 % = efeito "Esquilo"). Sem FFT no LYNX, aproximar pelo granular
  com janela ~60 ms.
- **Delay:** linha de atraso com realimentação `fb%`, aplicada antes (PRE) ou depois (POST) do shift.
- **Mix:** `saída = (1 − m)·seco + m·processado`.

---

## 3. Frequency Shifter **[DECOMP `FUN_006dd080` + motor `dispatch`, `FUN_1800020a0`, `FUN_1800023c0`, `FUN_180001fd0` + DADOS]**

Desloca todas as componentes por **Hz fixos** (banda lateral única) ou faz *ring modulation*; LFO de fase
L/R, realimentação e modo sincronizado ao andamento.

### 3.1 Layout do `0xD5` (52 B) **[DECOMP + DADOS]**

`i32` versão (4) · **10 × i32 = parâmetros 0…9** · `i32` flags (bit 0 = botão *Linked mode* das formas) ·
`i32` trilha de *sidechain* (`SideChainSelect`, 0 = nenhuma).

### 3.2 Parâmetros (nomes do motor, op 5) e conversões (op 6 / `setParam`) **[DECOMP]**

O motor recebe `x` normalizado.

| i | Nome | Faixa (padrão) | Valor real |
|---|---|---|---|
| 0 | **Mix** | 0…10000 (10000) | ganho úmido `min(1, 2x)`, seco `min(1, 2 − 2x)` (0,5 = os dois inteiros; 1 = só processado) |
| 1 | **Type** | 0…2 (1) | 0 = *Frequency Shifter HQ*, 1 = *Frequency Shifter*, 2 = **Ring Modulator** (`x < 1/3`, `< 2/3`, resto) |
| 2 | **Frequency** | −70000…70000 (0) | ver abaixo |
| 3 | L/R Phase Offset | 0…10000 | `0,25·x` ciclo (0…90°) entre os canais |
| 4, 5 | L / R Shape | 0…10000 (10000) | mistura `x` da banda deslocada com `1 − x` da banda espelhada (1 = só uma banda lateral) **[HIP no significado]** |
| 6 | Feedback | 0…10000 (5000) | `1,005·(2x − 1)` (±100 %; 0,5 = sem realimentação) |
| 7 | Mono↔Stereo | 0…10000 (10000) | `(1 − x)·0,5` de espalhamento |
| 8 | **Range** | 0…2 (1) | 0 = **20 kHz**, 1 = **200 Hz**, 2 = **Tempo** |
| 9 | Left Phase Offset (start phase) | 0…10000 (5000) | `x + 0,5` ciclo |

**Frequência** (`FUN_180001fd0`): com `s = 2x − 1` (−1…1), `|f| = 0,1 Hz · 2^(K·|s|)`, sinal de `s`; **K = 17,6096**
na faixa 20 kHz (máx. = 20 000 Hz) e **K = 10,9658** na faixa 200 Hz (máx. = 200 Hz); `s = 0` → 0 Hz. Na faixa
*Tempo*, o valor é um período sincronizado mostrado como `compassos:48avos` (`(1 − x)` → tempo **[HIP]**). No
*Ring Modulator* só a magnitude conta (0,1 Hz·2^(K·x)). Conferido nos presets: "+800 Hz" (51542, faixa 0) →
800,2 Hz; "−800 Hz" (−51537) → −799,9 Hz; padrão (70000, faixa 1) → +200 Hz **[DADOS]**.

### 3.3 Nos demos **[DADOS]**

"Bien Duro" ins. 17: −71 Hz (faixa 200 Hz), 100 % úmido; ins. 65: +561 Hz (faixa 20 kHz); "Africa's Calling"
ins. 45: +8,0 Hz (o clássico "phaser lento"); "Madonna" ins. 21: mix 0 = seco (bypass efetivo). Todos tipo 0
(HQ), feedback 0, sem sidechain.

### 3.4 Algoritmo **[HIP: comportamento clássico do efeito]**

Sinal analítico por transformada de Hilbert (par de filtros passa-tudo em quadratura), multiplicado por
`e^{i2πft}`; saída = parte real (uma banda lateral). *Ring*: `x·cos(2πft)` (duas bandas). Realimentação: soma da
saída atrasada de 1 bloco curto à entrada. O `FreqShift` atual do LYNX implementa exatamente o caso
"tipo 0/1, uma banda".

---

## 4. Emphasis (maximizador do Master) **[DECOMP `FUN_00706090`, `FUN_00706ba0`, `FUN_007043f0`, `FUN_00705110` + DFM + DADOS]**

### 4.1 Layout do `0xD5` **[DECOMP]**

`i32` versão (escritor: 10; demos 8) · **13 × i32 = parâmetros 0…12** · `i32` estado de UI (`+0x870`) ·
`i32` (`+0x878`, v ≥ 7) · `u8` (v ≥ 9) · `u8`, `u8` (v ≥ 10: *Instant recovery* e *DC block*, Tags 26/27 do DFM
**[HIP na ordem]**). Em versões < 8 o parâmetro 2 é somado de 1 e recortado em 1…4.

### 4.2 Parâmetros **[DECOMP+STR]**

| i | Nome | Faixa | Valor real |
|---|---|---|---|
| 0 | Bypass | 0/1 | |
| 1 | Delta | 0/1 | ouvir só a diferença |
| 2 | Envelope order | 1…4 | inteiro |
| 3 | True peak | 0/1 | |
| 4 | Channel mode | 0/1 | L/R ou M/S |
| 5 | Emphasis mode | 0…3 | Versatile / Loud / Transient / Steady |
| 6 | **Input gain** | 0…10000 | **`24x` dB** (linear em dB: `lerp(0, 24, 1)`) |
| 7 | Linking | 0…10000 | `100x` % |
| 8 | Emphasis amount | 0…10000 | `100x` % (0 = "OFF") |
| 9 | **Output ceiling** | 0…10000 | **`20·log10(curve11(x))` dB** (x = 1 → 0 dB; 0,9308 → −1,6 dB; 0,99 → −0,23 dB) |
| 10 | Level matching | 0/1 | |
| 11 | Oversampling | 0…4 | OFF / 2× / 4× / 8× / 16× |
| 12 | Hardness | 0…9 | inteiro |

(A constante `K` da curva do teto é inicializada em tempo de execução; `(e^{K·x} − 1)·0,1` só vale 1 em x = 1
com `K = ln 11`, isto é, a mesma `curve11` dos faders do FL **[DECOMP+cálculo]**.)

### 4.3 Nos demos (4 Masters) **[DADOS]**

| Demo | Input | Teto | Modo |
|---|---|---|---|
| Bien Duro | +1,5 dB | −1,6 dB | Versatile |
| NIGHT TERROR | +6,0 dB | −0,23 dB | Versatile, M/S |
| With You | +2,1 dB | −0,29 dB | Steady |
| Use Me | +5,0 dB | −0,20 dB | Versatile |

**Aproximação:** `limiter` do LYNX com `params[1] = inputDb/24` (= **x direto**) e
`params[0] = (tetoDb + 12)/12`; release padrão. É fiel no essencial (ganho de entrada + teto).

---

## 5. Low Lifter (realce de graves) **[DECOMP `FUN_006c6d50`, `FUN_006c7490`, `FUN_006c54c0` + DFM + DADOS]**

### 5.1 Layout (82 B) **[DECOMP]**

`i32` versão (6) · **15 × i32 = parâmetros 0…14** · `u8`, `u8`, `i32`, `i32`, `i32`, `i32` (estado de UI/
visual; nos dados `196609, 196608, 131072, 131072`).

### 5.2 Parâmetros **[DECOMP]**

| i | Nome | Valor real |
|---|---|---|
| 0 | Enabled | 0/1 |
| 1 | **Dry/Wet** | `100x` % |
| 2 | **Low band gain** | `lerp(−120, 12, 7,275)` dB (0 = mudo; 0,4999 → 0 dB) |
| 3 | Harmonics decay slope | `lerp(−60, 0, 3,105)` dB |
| 4 | Lookahead | 0/1 |
| 5 | Invert harmonics polarity | 0/1 |
| 6 | **Pre-shaping boost** | `18x` dB (ganho antes do gerador de harmônicos = "drive") |
| 7 | Diffuser mix | `x` (0…1) |
| 8 | Diffuser coefficient | `−0,99 + 1,98x` |
| 9 | **High band gain** | `lerp(−120, 0, 5)` dB (1 → 0 dB) |
| 10 | **Crossover cutoff** | `lerp(20, 120, 0,4989)` Hz ≈ `20 + 100x²` Hz |
| 11 | **Low cut** | mesma escala, 20…120 Hz |
| 12 | Multiband processing | 0/1 |
| 13, 14 | High band / Low band ligados | 0/1 |

Comportamento **[HIP, pelo nome dos controles]**: a banda abaixo do *crossover* passa por um gerador de
harmônicos (saturação) com *pre-boost*, cujos harmônicos decaem com a inclinação dada; um corta-graves em *low
cut*; difusor (passa-tudo) opcional; soma das bandas com os ganhos; mistura seca/molhada.

### 5.3 Demos **[DADOS]**

"Bien Duro" ins. 32/41: mix 75 %/100 %, *pre-boost* +12,2 dB, inclinação −18 dB, *crossover* 80 Hz, *low cut*
48/38 Hz, multibanda; "Song For You" ins. 3: *pre-boost* +9,4 dB, *crossover* 72 Hz, sem difusor.

**Aproximação:** `parametricEq` com banda 1 = *low cut* (passa-altas 12 dB/oct) em `f(p11)` (se > 21 Hz),
banda 2 = *low shelf* em `f(p10)` com ganho `= lowGain − highGain + realce`, onde
`realce ≈ 0,25·preBoostDb·mix` (o gerador de harmônicos soa como +2…+3 dB de graves percebidos **[HIP]**), e
saída = `highGain`. Opcional: um `distortion` leve antes não é recomendado (saturaria a faixa toda).

---

## 6. Fruity Squeeze **[DECOMP `FUN_006d30c0`, `FUN_006d3a50` + DFM + DADOS]**

`0xD5` (52 B) = `i32` versão (0) + **12 × i32 = parâmetros 0…11**:

| i | Nome | Faixa (padrão) | Valor real |
|---|---|---|---|
| 0 | Gain | 0…160 (127) | ganho linear `v/127` (127 = 0 dB; 160 = +2 dB) |
| 1 | **Mix** | 0…128 (64) | seco `(128 − v)/128`, úmido `v/128` |
| 2 | **Steps** | 1…32768 (1) | nº de níveis de quantização `= 32768^(1 − v/32768)` (1 → 32768 níveis = limpo; 12144 → ≈ 700 níveis ≈ 9,5 bits) |
| 3 | Filter | 0/1 | liga o filtro |
| 4 | Filter type | 0…4 | LOW PASS / HIGH PASS / BAND PASS / NOTCH / PEAK |
| 5, 6 | Filter res. / freq. | 0…65536 | `x` (sem unidade no texto) |
| 7 | Filter position | 0/1 | pre / post |
| 8 | Relation | 0…3 | none / impact = preserve / ×2 / ÷2 |
| 9, 10 | Impact / Preserve | 0…44100 | `44101^x − 1` amostras (× taxa/44100) |
| 11 | Amount | 0…65536 | `x` (exibido em dB) |

Demo ("NIGHT TERROR" ins. 41): steps ≈ 700 níveis, filtro LP pós em `x = 0,87`, mix 50 %. **Aproximação:**
`distortion` com *drive* baixo (quantização leve ≈ ruído de 9 bits) + `filter` LP (corte `20·1000^0,87` Hz
**[HIP]**), mix 0,5.

## 7. Transporter **[DECOMP `FUN_00713760`, `FUN_00713f40`, `FUN_007125f0` + DFM]**

Repetidor/"looper" de trechos detectados por transientes (laços A/B, reverso, oitava acima/abaixo
probabilísticos). `0xD5` = `i32` versão (8) + **22 × i32 = parâmetros 0…21** (v < 8: 20) + `u8`, `u8` +
`i32` trilha de envio de saída + `i32` trilha de entrada + `i32`, `i32`.

Parâmetros: 0 **Dry level**, 1 Loop level A, 2 Loop level B (0…12800; dB = `20·log10(curve11(x))`), 3–5
vieses reverse / octave shift / octave down-up (`2x − 1`), 6 spread/auto-pan, 7 min. length A (`0,02 + 1,98x` s),
8 A/B ratio (`1 + 9x`), 9 transient sensitivity, 10 bouncing rate, 11 ignore transients, 12 flush, 13 loop mode
(Auto/Bouncing/Adaptive/Tempo), 14 bypass, 15–17 update loop A/B/AB, 18 probabilístico/determinístico, 19/20
tempo ratio length/trigger (1/1…1/16 quintina), 21 ignore bias **[DECOMP+STR]**.

Demo ("Song For You" ins. 44): dry −3,3 dB, laços −15/−14 dB. **Sem aproximação**: importar como `utility`
com o ganho do *dry level* (o que soa "por baixo" do efeito) e registrar no log.

## 8. Vintage Phaser **[DECOMP `FUN_006ea8e0`, `FUN_006eb7f0`, `FUN_006eb680` + DFM]**

`0xD5` = `i32` versão (3) + **36 × i32 = parâmetros 0…35** + `u8` (v1: 32 parâmetros). Conversões principais:
frequências 0 (mín.), 1 (máx.), 4 (manual) = `1,25 Hz · 2^(14x)` (até 20,5 kHz); 2 feedback = `99x` %; 6
velocidade do LFO = interpolação exponencial entre as frequências dos parâmetros 29/30 (*rate frequency* 1/2),
ou sincronizada (7); 21 mix = `100x` % seco/úmido; 15 *delay stereo* (centro/esq./dir.); 16 tempo do delay `x`
ms ou passos; 23 ruído em dB; 33 motor V1/V2. Demo único ("Stroll Up" ins. 6, preset "Lowgrounder").
**Aproximação:** `chorus` com profundidade alta, *feedback* = `p2·0,99`, velocidade convertida e mix = `p21`
**[HIP; soa diferente — phaser ≠ chorus]**.

## 9. Fruity Send **[DECOMP `FUN_006bd0f0`, `FUN_006bd930`]**

`0xD5` (16 B) = **3 × i32 = parâmetros 0…2** + `i32` trilha de destino (−1 = nenhuma):

| i | Nome | Faixa (padrão) | Valor real |
|---|---|---|---|
| 0 | Pan do envio | −128…128 | `v/128` |
| 1 | **Volume do envio** | 0…320 (256) | `curve11(v/256)` (256 = 0 dB; 320 ≈ +5,6 dB) |
| 2 | **Dry level** | 0…256 (256) | ganho linear `v/256` do sinal que segue na trilha |

Demo ("Madonna" ins. 1): dry 197 → **−2,3 dB**, destino −1 (não envia nada). **Aproximação:** `utility` com
ganho `20·log10(dry/256)`; se houver destino ≥ 0, criar um *send* pós-efeito (no ponto do slot) para essa trilha
com nível `curve11(vol/256)` e pan `p0/128`.

---

## 10. Fruity Envelope Controller **[DECOMP `FUN_008ad310`, `FUN_008af660`, `FUN_008afec0`, `FUN_008ae740`, `FUN_008b11c0`, `FUN_008b0f90`, `FUN_007e0170` + DFM + DADOS]**

Gerador "controlador": não produz áudio. Tem **8 articuladores** (envelope + LFO + mapas, o mesmo
`TArticulator`/`TCustomMEnvelope` do Sytrus, `REA_rodada11.md` §1.6); **a saída k do controlador é o
articulador k** (k = 0…7), e é isso que os links `0xE3` com origem `canal<<16 | 0x8000+k` referenciam
(`REA_rodada14.md` §7.1).

### 10.1 Layout completo do `0xD5` **[DECOMP `FUN_008ad310` + DADOS: os 30 blobs consomem exatamente]**

```
u32 versão              0x00060003 (escritor; o leitor usa a metade alta se ≥ 0x10000) | 6 | 5 (antigo)
i32 × N  parâmetros     N = 90 (v ≥ 6) = 8 articuladores × 11 + Mod X + Mod Y;  N = 44 (v < 6) = 4 × 11
i32  articulador exibido na UI (0…7)
i32  parte mostrada no editor (0 = envelope, 1 = LFO, …) (só v ≥ 1)
i32  flags: bit 0 = Freeze (editor), bit 1 = Slide (editor), bit 2 = saída contínua (ver §10.4)
i32  posição do combo ModeSelect (UI)
repetir 8× (v < 6: 4×):
  i32 flags do articulador (só v ≥ 2): bit 0 = modo BIPOLAR (soma); 0 = UNIPOLAR (produto)
  articulador (formato do Sytrus):
     i32 versão (3) · f32 base · f32 nível do envelope · f32 nível do LFO      (= parâmetros/256)
     envelope (kind 1) + 4 × i32 A/D/S/R
     LFO (kind 2) + 4 × i32 (velocidade, tensão, skew, largura de pulso) + i32 (versão ≥ 3)
     5 mapas (kind 3): teclado, velocidade, Mod X, Mod Y, aleatório     (v < 6: 2 mapas)
  i32 nota mínima, i32 nota máxima          (padrão 0 e 256 = todas as teclas)
```

Cada objeto envelope/LFO/mapa é o `TCustomMEnvelope` já descrito (`kind`, `versão`, `n`, `n × {f64 Δx, f64 y,
f32 tensão, 4 B tipo de curva}`, 5 × i32: flags (bit 0 TEMPO), **ligado** (−1/0), ponto de decay, início do
laço, sustain/fim do laço) — o leitor `readEnvelope` de `FlSytrus.cpp` serve sem mudança.

### 10.2 Os 11 parâmetros de cada articulador (índice = 11·k + s) **[DECOMP `FUN_008ae740`]**

| s | Parâmetro | Faixa (padrão) | Uso |
|---|---|---|---|
| 0 | **Base level** | 0…256 (128) | valor de repouso, `/256` |
| 1 | **Envelope level** | −256…256 (256) | profundidade do envelope, `/256` |
| 2 | **LFO level** | −256…256 (128) | profundidade do LFO, `/256` |
| 3…6 | Env A / D / S / R | (128, 128, 0, 128) | escalas do envelope (as mesmas do Sytrus: tempos ×v/128, sustain +v/128) |
| 7…10 | LFO speed / tension / skew / PW | (40000 ou 40898, 0, 0, 0) | velocidade = período `625/(10001^(v/65536) − 1)` (s ou tempos com TEMPO) |
| — | 88 / 89 | **Mod X / Mod Y** globais | 0…65536 (32768) | entrada dos mapas Mod X/Y = `v/65536` + o X/Y da nota |

### 10.3 Disparo e cálculo da saída **[DECOMP `FUN_008af660` (TriggerVoice), `FUN_008afca0` (Release), `FUN_008afec0` (NewTick)]**

- **Disparo:** cada nota do **próprio canal** do controlador (padrões/piano roll) cria uma "voz". Para cada
  articulador cuja faixa de notas contém a tecla (§10.1, `mín ≤ nota ≤ máx`), o articulador é **retirado das
  vozes anteriores** e passa a seguir só a nota nova — ou seja, **cada articulador é monofônico, última nota
  vence**, e reinicia envelope e LFO. A voz guarda os valores dos mapas de teclado (`nota/120`), velocidade
  (curva logarítmica da velocity, 0…1) e aleatório, calculados uma vez no disparo.
- **Soltar a nota** (`Voice_Release`) manda envelope e LFO para a fase de release (salta o ponto de sustain).
  A voz morre quando envelope e LFO terminam.
- **A cada *tick* do sequenciador** (`NewTick`, não por amostra) e para cada articulador ativo, o valor é
  montado a partir de `v = base`:
  1. envelope ligado: `v += env(t)·nívelEnv`;
  2. LFO ligado: modo bipolar → `v += lfo(t)·nívelLFO`; modo unipolar → `v *= 1 + 2·lfo(t)·nívelLFO`;
  3. cada mapa não-neutro (teclado, Mod X, Mod Y) com a sua entrada: bipolar → `v += mapa`, unipolar →
     `v *= mapa`;
  4. combina com os fatores de velocidade/aleatório da voz (mesma regra);
  5. **recorta em 0…1** e, se mudou, envia ao host como valor da saída k (escala 2³⁰ = 1,0).
  No modo bipolar, envelope, LFO e mapas são **centrados em 0,5** (o articulador passa 0,5 como "neutro" para
  as partes — `FUN_008b0f90` → `FUN_007d1600`), então o que soma é `(y − 0,5)`; no unipolar o neutro é 0 (para
  somas) / 1 (para produtos) **[DECOMP; o deslocamento exato dentro de cada parte HIP forte]**.
- **Sem nota ativa:** se o bit 2 das flags globais estiver ligado ("Continuous output" **[HIP pelo menu]**), a saída
  é recalculada continuamente só com base + mapas Mod X/Y (é o que fazem os presets "Smart X/XY"); senão,
  **a saída fica parada no último valor**.
- **Unidades de tempo:** iguais às do Sytrus: `Δx` dos pontos em **segundos**, ou em **tempos (semínimas)**
  quando o bit TEMPO das flags do envelope está ligado **[HIP forte; mesma classe `TMEnvelope`]**.

### 10.4 Os 4 canais dos demos **[DADOS `tmp/r15/envctl.py`]**

| Demo / canal | Articuladores usados | Leitura |
|---|---|---|
| "Future House" / *Basic Sidechain* | 0–3, **unipolares**, bases 14/0/25/128 (/256) | art. 0: `0 → 0,754 em 0,237 s → 1,0 em +0,092 s` (tensões −0,30/+0,18), sem sustain: a cada nota cai para ≈ 0,05 e volta a 1 em ~⅓ s (*ducking*); art. 1 igual com 0,47 s; art. 2/3 rampas 0→1 em 0,35/0,40 s |
| "Madonna" / *Harmony Notes* | 0, bipolar | envelope ligado `0 → 1 → 1 → 0` (porta: 1 enquanto a nota dura) |
| "FlashyWaves" (ZGE) | 0, bipolar | ataque instantâneo a 0,75, cai a 0 em 0,6 s |
| "Steptime" (v5, 4 articuladores) | 0 | `0 → 1 → 1 (0,8 s) → 0` |

### 10.5 Como transformar em fonte de modulação no LYNX

Ver Recomendações §2. Resumo: para cada link `0xE3` com origem `canal<<16 | 0x8000+k`, o valor do alvo no
instante `t` é `clamp(fórmula de §10.3 com o articulador k)`, onde o envelope é reiniciado em cada nota do canal
do controlador (`t − início da nota`, release no fim da nota). O resultado (0…1) passa pela curva do link, como
o Peak Controller já faz.

---

## 11. Fruity X-Y Controller e Control Surface: onde está o valor atual

### 11.1 X-Y Controller **[DECOMP `FUN_006c5d20`, `FUN_006c6650` + DFM + DADOS]**

`0xD5` = `i32` versão (2) + **10 × i32 = parâmetros 0…9** + `u8` + *shortstring* nome da grade + `i32` +
5 × `i32` (v < 2: `u8` + `i32` a mais antes do bloco final).

| i | Parâmetro | Faixa (padrão) |
|---|---|---|
| 0 | **X level (posição atual)** | 0…65536 (32768) |
| 1, 2 | X min / X max | 0…65536 (0 / 65536) |
| 3 | X tension | −128…128 |
| 4 | **Y level (posição atual)** | 0…65536 (32768) |
| 5, 6 | Y min / Y max | |
| 7 | Y tension | |
| 8, 9 | Speed / Acceleration (suavização do movimento) | 1024…2048 / 256…1024 |

O `ProcessParam` trata os parâmetros 0–7 como **2 eixos × 4** (`eixo = i div 4`) **[DECOMP]** → **saída 0 = X,
saída 1 = Y** **[HIP forte]**. Valor constante a aplicar: `mín + (máx − mín)·tensão(level/65536)` com
mín/máx/65536; com tensão 0, linear. No demo ("Drumloop Masher" ins. 1): X = 0,5, Y = 0.

### 11.2 Control Surface **[DECOMP `FUN_00855120`, `FUN_008534b0` + DADOS]**

`0xD5` = `i32` versão (1; −1 = formato antigo) + chunks `u32 id · u64 tamanho · dados` (mesmo envelope do
Patcher): `2000` (64 B, cabeçalho), `0x7D2` (tamanho da superfície), `0x7D3`, e por controle:

| id | Conteúdo |
|---|---|
| `0x834` (32 B) | tipo do controle no 1º `i32` (3 = botão, 2 = knob/fader, 0 = pad/2D… **[HIP]**) |
| `0x837` | nome (UTF-16) |
| `0x838` (16 B) | retângulo (4 × f32) |
| **`0x836`** (12 B, um por valor do controle) | **`f32` valor atual (0…1)**, 4 B do 2º atributo do valor, **`i32` índice da saída publicada** (−1 = não publicada) |
| `0x839`/`0x83A`/`0x83B` | propriedades, cores, "published" (texto) |
| `0x835` | fim do controle |

O índice no 3º campo é procurado na tabela de saídas publicadas do plugin (`FUN_008534b0`) — é o **k** do link
`0x7…8000+k`. Demos: "Future House" botão *Transition FX* = saída 0, valor 0; "Delay 3"/"Transient Processor":
botão ON/OFF = saída 0, valor 1,0 **[DADOS]**.

---

## 12. Ordem dos parâmetros de PoiZone, Toxic Biohazard e BassDrum

### 12.1 PoiZone **[DECOMP motor: `FUN_180031020` (estado), `FUN_180030920` (nomes), `FUN_18002f810` (texto)]**

Estado = `"SSLF"` + `u32 0x2B4` + tag `"Ver4"` + **156 × f32 = parâmetros 0…155 na ordem do índice** (o escritor
chama `getParameter(i)` em laço) + 64 B nome do preset. **Confirmado: float k = parâmetro k.** Nomes (0…73) e
conversões (`x` = valor 0…1):

| i | Nome | Conversão | i | Nome | Conversão |
|---|---|---|---|---|---|
| 0 | OscA Shape | 0 = Saw, > 0 = Pulse | 26 | LFO Amount | `200x − 100` % |
| 1 | OscA PW | `100x` % | 27 | LFO Retrig | on/off |
| 2 | OscA Sync | on/off | 28 | **LFO Rate** | `30·x⁵` Hz |
| 3 | OscA Ring | on/off | 29, 30 | Unison Detune / Pan | % |
| 4 | OscB Shape | 0 = Saw, > 0 = Pulse | 31 | Delay ON/OFF | |
| 5 | OscB PW | % | 32, 33 | Delay Time L/R | `(1 + ⌊7x⌋)/4` (quartos) |
| 6 | **OscB Pitch** | `⌊48x⌋ − 24` semitons | 34–38 | Delay Fdbk / Mod / Lo-Cut / Hi-Cut / Wet | % |
| 7 | OscB Detune | `50x` centésimos | 39 | Chorus ON/OFF | |
| 8 | **Osc Balance** | % (A↔B) | 40 | Chorus Rate | `5x` Hz (+ mín.) |
| 9 | Noise Balance | % | 41, 42 | Chorus Depth / Wet | % |
| 10 | **EG Amount** | `200x − 100` % | 43 | MW Amount | ±100 % |
| 11, 12, 14 | **EG Attack / Decay / Release** | `5 + 49995·x⁴` ms | 44 | **Monophonic** | on/off |
| 13 | EG Sustain | % | 45 | Octaver | on/off |
| 15 | Gain | `30x − 20` dB | 46 | **Glide Time** | % |
| 16 | Fine Tune | `200x − 100` centésimos | 47, 48 | Gate Mix / Smooth | % |
| 17 | Transpose | `⌊48x⌋ − 24` semitons | 49–64 | Gate Step 1…16 | on/off |
| 18 | **Flt. Cutoff** | % (sem unidade no texto; **[HIP]** `20·1000^x` Hz) | 65 | **EG Dest** | `⌊4x⌋`: Cutoff / Frq A / Frq AB / PW A / PW AB |
| 19 | Flt. Reso | % | 66 | **Filter Mode** | `⌊2x⌋`: LP / BP / HP |
| 20, 21 | Flt. KbdTrk / VelTrk | % | 67 | LFO Dest | `⌊5x⌋`: Cutoff / Frq A / Frq AB / PW A / PW AB / Pan |
| 22 | **Amp. Attack** | `1 + 4999·x⁴` ms | 68 | LFO Shape | `⌊4x⌋`: Saw / Pulse / Triangle / Sine / Random |
| 23 | **Amp. Decay** | `1 + 19999·x⁴` ms | 69, 70 | ARP Mode / Range | |
| 24 | Amp. Sustain | % | 71 | ModWheel Dest | |
| 25 | **Amp. Release** | `15 + 9985·x⁴` ms | 72, 73 | Glide Held / Glide Time Static | |

74…155: estado do arpejador/sequência (−1 = passo vazio) — não são parâmetros nomeados **[DADOS]**.
Conferido no preset padrão: pitch/transpose/fine = 0,5 (centro), gate 1 0 1 0 1 1 1 0…, cutoff 1, sustain 1
**[DADOS]**.

### 12.2 Toxic Biohazard **[DECOMP `FUN_18003b6e0` (nomes), `FUN_180041b00` (estado) + DADOS]**

Estado (10 050 B) = `"SSLF"` + `u32 0x273A` + tag `"TBi2"` + 64 B de cabeçalho (inteiros de 16/32 bits:
seleções de forma de onda/UI) + **8 192 B de formas de onda desenhadas** (8 × 2 × 2 × 64 f32) + **340 × f32 de
parâmetros a partir do offset 0x204C do blob** (os 294 primeiros são os parâmetros nomeados, na ordem do
índice; 294…339 são estado interno) + 294 B (um byte por parâmetro, 0xFF nos dados: mapeamento de CC MIDI
**[HIP]**) + 128 B nome do preset. Nomes por índice:

| Índices | Parâmetros |
|---|---|
| 0–10 | Gain, Distortion, Transpose, **Master Atk / Dec / Sus / Rel**, Vel. Curve, Unison Dtn, Unison Pan, Glide Time |
| 11–14 | EGF to Pitch, Lfo2 to Pitch, Lfo1 Speed, Lfo2 Speed |
| 15–24 | **Flt Cutoff**, Flt Res, Flt KeyTrk, Flt VelTrk, Flt EG Atk/Dec/Sus/Rel, Flt Eg Amnt, Flt Lfo1 Amnt |
| 25–30 | O1…O6 Enable |
| 31–36 / 37–42 | O1…O6 **Coarse** / **Fine** (0,5 = centro) |
| 43–48 / 49–54 / 55–60 | O1…O6 Frq Shift / Free Run / Phase |
| 61–66 / 67–72 | O1…O6 VelSens / Init |
| 73–78 / 79–84 / 85–90 / 91–96 | O1…O6 **Atk / Dec / Sus / Rel** |
| 97–102 / 103–108 | O1…O6 Pan / **Mix** (nível de saída do operador) |
| 109–144 | **matriz de FM** "Oi Fm j" (j = 1…6, i = 1…6; índice `109 + 6·(j−1) + (i−1)`) |
| 145–156 | LFO1 → Osc1…6, LFO2 → Osc1…6 |
| 157–162 | Seq Play, Seq Swing, Seq P-P, Seq Rnd, Seq Dual, Seq Steps |
| 163–226 | Seq Ptrn 0…63 |
| 227–235 | EQ On, EQ Band 1…8 |
| 236–293 | FX: Fx1/Fx2 Enable, Flanger (delay, fdbk, depth, rate, mix), Chorus, Reverb (decay, damp, hi-cut, mix), Delay (time, fdbk, rate, depth, blur, mix), LoFi (bits, smp, flt), Phaser (frq, rate, dpth, fdbk, mix) — dois de cada |

Conferido: Enable 1/0 nos operadores usados, Coarse/Fine 0,5, matriz quase toda 0, EQ bands 0,5 **[DADOS]**. O
plugin não tem texto com unidades (só nomes); as escalas ficam **[HIP]**: Coarse `round((x−0,5)·48)`
semitons/razão, tempos `~10·x³` s.

### 12.3 BassDrum **[DECOMP motor: `FUN_18002bce0` (estado), `FUN_18002a380` (nomes), `FUN_18002ace0` (texto)]**

Estado = `"SSLF"` + `u32 0x560` + tag (`"1kiK"` nos dados; o escritor atual grava `"2kiK"`) + 256 B nome do preset
+ 1024 B caminho da amostra de ruído + **23 × f32 = parâmetros 0…22 na ordem do índice** (blob 0x50C).
**A ordem da rodada 13 estava errada**; a certa:

| i | Nome | Conversão | i | Nome | Conversão |
|---|---|---|---|---|---|
| 0 | **Output Level** | `40(x − 0,5)` dB | 12 | Var. Phase | % |
| 1 | Sampling Ratio | `2^(4(x − 0,5))` (0,25…4) | 13 | Click Cut | % |
| 2 | **Duration** | % | 14 | Click Freq | % |
| 3 | Butt | % | 15 | **Click Amount** | % |
| 4 | **Drive** | % | 16 | Noise A | % |
| 5 | Attack | % | 17 | Noise D | % |
| 6 | **Base** (frequência final) | `2^(4x + 4)` Hz (16…256 Hz) | 18 | Noise S | % |
| 7 | **Peak** (frequência inicial) | % **[HIP: mesma escala relativa]** | 19 | Noise Start | `200(x − 0,5)` % |
| 8 | **Slide Time** | % | 20 | Noise Pitch | `48(x − 0,5)` semitons |
| 9 | Var. Base | `200(x − 0,5)` % | 21 | **Noise Mix** | % |
| 10 | Var. Peak | `200(x − 0,5)` % | 22 | Noise Filter | % |
| 11 | Var. Slide | `200(x − 0,5)` % | | | |

Preset "Bassdrum 01": saída +0,6 dB, *base* `2^(4·0,305+4)` = 37 Hz, *peak* 0,374, *slide* 0,27, *duration*
0,625, click 1,0, noise mix 0,66 **[DADOS]**.

---

## Recomendações para o LYNX

1. **Pitch Shifter → `EffectKind::pitchShift` (já existe) (prioridade 1, 14 slots).** Em `FlEffects.cpp`:
   ler §2.1; `p = i32(4)` centésimos → `st = trunc(p/100)`, `ct = p − 100·st`;
   `params[0] = 0,5 + st/48`, `params[1] = 0,5 + ct/200`, `params[2] = i32(16)/2400` (mix),
   `params[3] = clamp01((D − 20)/100)` com `D = 20 + 130·(i32(20) − 2000)/13000` ms no modo granular e
   `D = 60 ms` no modo FFT (byte 61 = 0). **Manter o slot mesmo com pitch 0** (5 demos — o parâmetro 0 costuma
   ser automatizado). `effectParamFromFl("Pitch Shifter", 0)` → `params[0]` com `v ↦ 0,5 + round(48v − 24)/48`
   e o resto em `params[1]` (o FL normaliza −1200…1200 em 0…1). Formant ≠ 0 e *delay mode* ≠ 0 → log
   ("formante ignorado"/"delay do Pitch Shifter ignorado"); se *delay mode* ≠ 0, alternativa: inserir um `delay`
   depois, tempo `1 + 999x` ms ou `v/48` steps.
2. **Envelope Controller → fonte de modulação ao vivo (prioridade 1; os 18 links de canal dos demos, `REA_rodada14.md` §7.1).**
   - Parser novo (`FlGenerators.cpp` ou arquivo próprio) seguindo §10.1, reaproveitando `readEnvelope` de
     `FlSytrus.cpp`; produzir `EnvControllerState { articuladores[8] { bipolar, base, envLevel, lfoLevel, knobs
     ADSR, lfoSpeed, env, lfo, mapas, noteLo, noteHi }, modX, modY, continuous }`.
   - Em `Flp.cpp`/`FlpImport.cpp`, aceitar `ControllerLink` cuja origem seja **canal** (`origem & 0xFFFF ≥ 0x8000`,
     `canal = origem >> 16`, `k = (origem & 0xFFFF) − 0x8000`) quando o canal for um Envelope Controller.
   - Implementação recomendada (fiel e barata): estender `Automation::LiveSource` com
     `kind = channelEnvelope`, guardando o índice do canal e `k`; no motor, a cada bloco, avaliar o articulador
     k a partir da **última nota do canal** dentro da faixa (`t` desde o note-on; release no note-off),
     com §10.3 (envelope em segundos, ou em tempos se o bit TEMPO), recortar 0…1 e aplicar a curva do link.
     O canal do controlador não toca som (instrumento "nenhum"), mas as notas dele precisam chegar ao avaliador.
   - Alternativa sem motor novo: "carimbar" o envelope como pontos de automação do alvo em cada nota do canal
     na playlist (amostrar a cada 1/64 de tempo; ligar ao valor final até a próxima nota). Cobre o caso
     *sidechain* do "Future House" (art. 0–3 → volumes) de forma idêntica.
   - Antes da primeira nota, usar o **valor final do envelope** (para o *ducking* = 1,0) — o FL mantém o último
     valor enviado.
3. **Frequency Shifter → `EffectKind::freqShift` (já existe).** `Hz = ±0,1·2^(K·|2x−1|)`, K por faixa (§3.2);
   em `params[0]`: `s = sign(Hz)·sqrt(min(|Hz|, 2000)/2000)`, `v = 0,5 + s/2` (inverte o `fshift::hz` atual,
   ±2000 Hz). Mix: o LYNX faz *crossfade*; usar `úmido/(úmido + seco)` com `úmido = min(1, 2x)`,
   `seco = min(1, 2 − 2x)` (x = 0 → slot desligado). Tipo 2 (ring) e faixa *Tempo* → log; feedback ≠ 0,5 → log.
4. **Emphasis → `limiter`** (4 Masters): `params[1] = p6/10000`, `params[0] = (20·log10(curve11(p9/10000)) +
   12)/12`. Bypass (p0 = 1) → slot desligado.
5. **Low Lifter → `parametricEq`** como em §5.3 (low cut + low shelf no *crossover* + saída), `mix` = p1.
6. **Fruity Send → `utility`** com ganho `dry/256` (e *send* se destino ≥ 0). **Fruity Squeeze →
   `distortion` + `filter`** (§6). **Transporter → `utility`** com o *dry level*. **Vintage Phaser → `chorus`**
   (§8). Todos com mensagem no relatório de import.
7. **X-Y Controller / Control Surface → valor constante.** Para links de efeito-controlador que não sejam Peak
   Controller: X-Y → saída 0 = `p0/65536` mapeado por min/max (p1, p2), saída 1 = `p4/65536` (p5, p6);
   Control Surface → procurar o `0x836` cujo 3º campo = k e usar o `f32` valor. Aplicar uma vez ao alvo (via a
   curva do link) como valor inicial do parâmetro, sem automação.
8. **PoiZone → `SynthSettings`** (agora com ordem confirmada): osc1 = OscA (`p0 = 0` → saw, senão square com
   PW `p1`), osc2 = OscB com `semitones = ⌊48·p6⌋ − 24`, `detune = 50·p7` ct, níveis pelo balanço `p8`
   (`osc1 = 1 − p8`, `osc2 = p8` **[HIP no sentido]**), osc3 = noise com nível `p9`; transposição global
   `⌊48·p17⌋ − 24` + fino `200·p16 − 100` ct; ADSR de amplitude p22–p25 (ms ÷ 1000); cutoff `20·1000^p18` Hz
   **[HIP]**, ressonância p19, `filterType` por p66 (LP 0, BP 1, HP 2); se `p65 = 0` (destino Cutoff),
   `filterEnv` = ADSR p11–p14 com `amount = (2·p10 − 1)·4` oitavas **[HIP na escala]**; LFO: destino p67,
   forma p68, taxa `30·p28⁵` Hz, quantidade `2·p26 − 1`; `mono = p44 ≥ 0,5`, `porta = p46 > 0`. O *gate* de
   16 passos (p47 mix > 0) pode virar um `volumeGate` no canal, se o LYNX tiver FX de canal.
9. **BassDrum → mesmo esquema do Fruit Kick (rodada 10 §4), agora com a ordem certa**: seno com afinação
   indo de *peak* para *base* (`2^(4·p6+4)` Hz) em `slide time` (p8), *decay* por `duration` (p2), clique por
   `click amount` (p15), ruído por `noise mix` (p21) com ADS p16–p18, *drive* p4, ganho `40(p0 − 0,5)` dB.
   (O LYNX ainda não decodifica o BassDrum; não usar a ordem hipotética da rodada 13.)
10. **Toxic Biohazard → FM de 2 operadores** no synth do LYNX: escolher o operador "portadora" com maior `Oi Mix`
    (103–108) e o modulador com maior `Fm` para ela na matriz (109–144); osc1 = seno da portadora
    (`Coarse` p31+i → razão), osc3 = seno do modulador, `fm = valor da matriz · 4` **[HIP]**, `fmDecay` pelo
    *decay* do modulador (p79+j); ADSR mestre p3–p6; cutoff p15. Precisão baixa; registrar no log.

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| Pitch Shifter `FUN_0072c630` (VMT+3) | Versão 7, 13 params (0x34 B), correção, tempo sync, motor, detector, UI | DECOMP |
| Pitch Shifter `FUN_0072d5b0`, constantes 24/12/130/20/4/100/999 | Texto dos parâmetros (semitons, ms, %, modos) | DECOMP |
| Pitch Shifter tabela de campos de `TSynthEditorForm` (`fields.py`) | 0x6F8 PluginTypeSelect, 0x718 CorrectionWheel, 0x810 TempoSyncCheckBox, 0x818 PitchDetectorCombo | STR |
| Pitcher `FUN_18001cab0`, `FUN_18001d020`, `FUN_18001d890` | setParam (velocidade, fine, faixa de voz 220…25 Hz, porta MIDI), padrões, máscara de escala por tônica/tipo | DECOMP |
| Frequency Shifter `FUN_006dd080` | Versão 4, 10 params, flags, trilha de sidechain | DECOMP |
| Frequency Shifter motor `dispatch`, `FUN_1800020a0`, `FUN_1800023c0`, `FUN_180001fd0` | Nomes, texto (mix seco/úmido, tipos, faixas), frequência `0,1·2^(K·|s|)` com K = 17,61 / 10,97 | DECOMP |
| Emphasis `FUN_00706090`, `FUN_00706ba0`, `FUN_007043f0`, `FUN_00705110` | Layout v10, input `24x` dB, teto `curve11` | DECOMP |
| Low Lifter `FUN_006c6d50`, `FUN_006c7490`, `FUN_006c54c0` | Layout v6, escalas `lerp` (dB, Hz) | DECOMP |
| Squeeze `FUN_006d30c0`, `FUN_006d3a50` | 12 params, steps `32768^(1−x)`, tipos de filtro, relação | DECOMP |
| Transporter `FUN_00713760`, `FUN_00713f40`, `FUN_007125f0` | 22 params + trilhas, níveis `curve11` | DECOMP |
| Vintage Phaser `FUN_006ea8e0`, `FUN_006eb7f0`, `FUN_006eb680` | 36 params, frequência `1,25·2^(14x)` | DECOMP |
| Fruity Send `FUN_006bd0f0`, `FUN_006bd930` | 3 params + trilha, volume `curve11(v/256)`, dry `v/256` | DECOMP |
| Envelope Controller `FUN_008ad310` | Layout completo (v6: 90 params; v5: 44; flags; 8 articuladores + faixa de notas) | DECOMP |
| Envelope Controller `FUN_008ae740` | Parâmetro `11k + s` → base/env/LFO (`/256`), A/D/S/R, LFO; 88/89 = Mod X/Y `/65536` | DECOMP |
| Envelope Controller `FUN_008af660`, `FUN_008afb60`, `FUN_008afa80` | Disparo por nota na faixa; articulador monofônico (última nota) | DECOMP |
| Envelope Controller `FUN_008afec0` (NewTick), `FUN_008b11c0`, `FUN_008b0f90`, `FUN_007e0170` | Soma/produto por modo, ordem das partes, neutro 0,5 no bipolar, recorte 0…1, envio da saída k | DECOMP |
| 30 blobs Envelope Controller (`envctl.py`) | Parse completo sem sobra (v6 e v5) | DADOS |
| X-Y Controller `FUN_006c5d20`, `FUN_006c6650` | Layout v2; params 0–7 = 2 eixos × (level, min, max, tensão) | DECOMP |
| Control Surface `FUN_00854f70`, `FUN_00855120`, `FUN_008534b0` | Chunks por controle; `0x836` = (valor, atributo, saída publicada) | DECOMP |
| PoiZone motor `FUN_180031020`, `FUN_180030920`, `FUN_18002f810`, `FUN_18002e310`, `FUN_18002e340` | `Ver4` + 156 f32 em ordem; nomes 0…73; escalas `x⁴`, `x⁵`, seletores | DECOMP |
| Toxic `FUN_18003b6e0`, `FUN_180041b00`, vtables `ToxicSynth`/`ToxicInstance` (`Sym`) | 294 nomes; layout (cabeçalho, ondas, 340 f32, CC, nome) | DECOMP |
| BassDrum motor `FUN_18002bce0`, `FUN_18002a380`, `FUN_18002ace0` | 23 f32 em ordem a 0x504 do chunk; nomes; escalas | DECOMP |

### Questões em aberto

- Pitcher: confirmar quais dos botões 22/25/26/27 são *Harmonize*, *MIDI* e *Formant*; unidade exata de *Fine
  Tune* e *Note A Freq*; o motor das vozes de harmonia.
- Pitch Shifter: tempo exato do delay com *tempo sync* (o texto diz `passos:48avos` — confirmar se o passo é a
  semicolcheia); efeito do *Pitch mode* absoluto.
- Frequency Shifter: significado preciso de *L/R Shape* e o período na faixa *Tempo*.
- Low Lifter / Emphasis: o processamento em si (só as escalas foram lidas); a ordem dos 3 bytes finais do
  Emphasis v10.
- Envelope Controller: deslocamento exato de cada parte no modo unipolar (o neutro do LFO no produto); se o
  *tick* do `NewTick` muda com o PPQ do projeto (afeta só a resolução, não os tempos); nome oficial do bit 2
  das flags.
- Control Surface: tabela completa dos tipos de controle (`0x834`) e o 2º campo do `0x836`.
- X-Y: confirmar a ordem das saídas (0 = X) com um link real nos demos (o único demo não tem link).
- PoiZone: escala do *cutoff* (o texto é só %) e do *EG Amount* em oitavas; sentido do *Osc Balance*.
- Toxic Biohazard: escalas (o plugin não exibe unidades); papel das 46 floats internas 294…339 e do cabeçalho
  de 64 B (formas de onda selecionadas por operador).
- BassDrum: escala do *Peak* (texto em %, possivelmente relativa à *Base*), *Duration* em segundos.
