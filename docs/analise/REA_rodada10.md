# Análise comportamental do FL Studio 2026: rodada 10 (REA) — sintetizadores nativos simples (Harmless, MiniSynth, DX10, Fruit Kick, BooBass, Drumpad)

> **Natureza deste documento.** Continuação de `REA_rodada4.md` (3x Osc). Engenharia reversa *clean-room*,
> apenas comportamental. Nenhum código nem pseudocódigo da Image-Line foi copiado: o que está aqui são
> descrições, layouts de dados e fórmulas escritas com palavras nossas. Endereços (`FUN_xxxxxxxx`),
> offsets e globais servem só para rastrear a evidência.

- **Alvos** (cópias locais em `tmp/r10/bin/`, originais em `Plugins/Fruity/Generators/<nome>/`):
  - `Harmless/Harmless_x64.dll` (SHA-256 `4100bf85…440b`, Delphi, classe `TTestPlug`, VMT `0x7791d0`)
  - `Minisynth/MiniSynth_x64.dll` (SHA-256 `e00ca019…ec85`, **C++**, classe `CMiniSynth`, origem FL Studio Mobile)
  - `Fruity DX10/Fruity DX10_x64.dll` (SHA-256 `3a9d58d7…e8d9`, Delphi, VMT `0x7343f0`)
  - `Fruit Kick/Fruit Kick_x64.dll` (SHA-256 `a93d8645…122b`, Delphi, VMT `0x72cff8`)
  - `BooBass/BooBass_x64.dll`, `Drumpad/Drumpad_x64.dll` (só formato + DFM)
- **Dados:** 165 `.flp` de `FL Studio 2026/Data` + todos os `.fst` de `Data/Patches` (presets de canal,
  mesmo formato de eventos). Coleta só da seção de canais (antes do primeiro `0xEC`), `0xD5` após `0xC9`.
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp`/`.fst` |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

### Contagem nos dados

| Plugin (`0xC9`) | Canais em `.flp` | Total com `.fst` | Tamanhos do `0xD5` |
|---|---|---|---|
| Harmless | **160** (25 arquivos) | 594 | 436/437 B (sem tabela custom), 4628/4629 B (com), 428 B (v4/v5) |
| MiniSynth | 12 | 19 | 214/228/242 B (varia com o nome do preset) |
| Fruity DX10 (+ "DX10") | 12 | 75 | 92 B (v1), 460 B (v2) |
| Fruit Kick / "Fruit kick" | 3 | 15 | 32 B (v0), 400 B (v1) |
| BooBass | 3 | 7 | 16 B |
| Drumpad / "DrumPad" | 11 | 976 | 300 B / 292 B |

### Nota geral: como achar o `SaveRestoreState` nestes Delphi

Nos plugins Delphi 12 x64 deste FL, o ponteiro "self" da VMT fica em **VMT − 0xC8** (não −0xB0 como no
Delphi antigo). Procurando um qword que aponte para o próprio endereço + 0xC8 perto da string `TTestPlug`,
a VMT aparece e as entradas seguem a ordem do SDK: **[0] DestroyObject, [1] Dispatcher, [2] Idle,
[3] SaveRestoreState, [4] GetName, [5] ProcessEvent, [6] ProcessParam, [8] Gen_Render, [9] TriggerVoice**
(script `tmp/r10/findself.py`) **[DECOMP]**. É o mesmo "entrada 3" da rodada 4.

---

## 1. Harmless **[DECOMP+STR+DADOS]**

Harmless é um sintetizador **aditivo** (as harmônicas de uma serra são somadas e moldadas por máscaras,
EQ de timbre e um "filtro" que é uma curva aplicada no domínio das harmônicas), mas a interface e o uso nos
demos são de um subtrativo clássico: oscilador serra/quadrada, uníssono, filtro com envelope, ADR de volume,
LFO, portamento e um rack de efeitos (chorus/delay/reverb/compressor).

### 1.1 Layout do `0xD5` (`SaveRestoreState` = `FUN_0077adb0`)

| Off | Tipo | Campo |
|---|---|---|
| 0x000 | u32 | **versão** (o FL 2026 grava 12; nos dados: 4, 5, 9, 10, 11, 12) |
| 0x004 | 100 × i32 | **parâmetros 0…99** (`P+0x128 + 4·i`); versões < 9 gravam só 99 (396 B) e o 99 fica 0 |
| 0x194 | 7 × i32 | (versão ≥ 4) estado dos combos sem índice de parâmetro, §1.4 |
| 0x1B0 | i32 `n` | (versão ≥ 8) tamanho da tabela de harmônicas custom: 0 ou 524 (`0x20C`) |
| 0x1B4 | `n` × 8 B | tabela custom (só se `n > 0`; §1.5) |
| fim | u8 | (versão ≥ 12) flag de interface (`P+0x798`); se 0 o plugin recalcula algo da escala do editor. Ignorar **[HIP]** |

Daí os tamanhos: 4 + 400 + 28 + 4 + 1 = **437** (v12 sem tabela), 437 + 4192 = **4629** (v12 com tabela),
436/4628 (v9–v11, sem o byte final), 428 (v4/v5: 4 + 396 + 28).

Conversões de versões antigas feitas na leitura **[DECOMP]**:
- versão < 5: no combo de compressão o valor 3 vira 4 (inseriram um item); se o parâmetro 86
  (forma do LFO) > 9, soma 1 (inseriram "Filter frequency").
- versão < 6: o parâmetro 61 (offset de ressonância) é multiplicado por 1,2.
- versão < 10: a tabela custom é deslocada 4 posições (formato antigo).
- versão < 11 com o parâmetro 47 = 0: o parâmetro 58 é reescalado pela faixa do filtro.

Nos 160 canais de `.flp`: v12/437 B = 102, v12/4629 B = 35, v11/436 B = 16, v11/4628 B = 7 **[DADOS]**.

### 1.2 Parâmetros (Tag do DFM `TSynthEditorForm` = índice) e conversões

Nomes = dica (*hint*) do controle no DFM **[STR]**; fórmulas de `ProcessParam` (`FUN_0077f630`) e do texto
mostrado na UI (`FUN_00781200`) **[DECOMP]**. Constantes inicializadas em `FUN_00614c80`: `ln 2`, `ln 11`,
`ln 3`, `ln 2 / 12` **[DECOMP]**. Abaixo `x = v/1000` salvo indicação; "faixa" e "def." vêm do DFM.

**Timbre / harmônicas**

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 0 | Phase randomness (sem Tag no DFM ⇒ 0) | −1000…1000 (0) | 0 = "Constant"; > 0 = "running" (v/10 %); < 0 = "spectral random" (|v|/10 %). Interno `sinal·x²` |
| 1 | Low harmonics protection | 0…1024 (256) | g = v/1024; ganho = (11^g − 1)/10 (0…1) |
| 2, 3, 4 | Sub harmonic 1 / 3 / 4 | 0…2048 (0) | ganho linear v/1024 (0…2) |
| 5…10 | Timbre EQ bandas 1…6 | −1024…1024 (0) | g = (v+1024)/1024; ganho = (3^g − 1)/2 → −1024 = mudo, 0 = 1,0 (0 dB), +1024 = 4,0 (+12 dB) |
| 11…22 | Harmonic mask 1/12…12/12 | 0…1000 (DFM: alterna 1000/0) | ganho = (11^x − 1)/10 (0…1). Padrão 1,0,1,0… = só harmônicas ímpares |
| 23 | Harmonic mask mix | 0…1000 (500) | x (0 = sem máscara, 1 = máscara cheia) |
| 24 | Mask mix LFO amount | ±1000 | x |
| 25 | Stereo tremolo depth | 0…1000 | x |
| 39 | Detuning | ±2400 (0) | centésimos; papel exato não verificado (dados: 0, ±1200, 2400) **[HIP]** |
| 40 | Grittiness | ±1000 | interno `−0,0125·sinal·(65^|x| − 1)` |

Com a máscara padrão (ímpares), "mask mix" leva a serra para uma **quadrada**: é o "odd/even" do Harmless
**[DECOMP para a fórmula; HIP para "máscara repete a cada 12 harmônicas"]**.

**Volume / pluck / global**

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 26 | Volume env attack | 0…1000 (0) | **t = 0,05·(101^x − 1) s** (0…5 s), rampa linear |
| 27 | Volume env decay | 16…1000 (500; dados: 1000) | **1000 = infinito** (segura o nível); senão **t = 0,11995·(1001^x − 1) + 0,05 s**, queda exponencial até −40 dB (decai para silêncio: **não há sustain**) |
| 28 | Volume env release | 0…1000 (250) | **t = 0,0249·(101^x − 1) + 0,01 s** (0,01…2,5 s), exponencial até −40 dB |
| 29, 30 | VEL → attack / release vel → release | bool | |
| 31 | Pluck decay | 0…1000 (500; dados: 1000) | 1000 = desligado; senão t = 0,11975·(1001^x − 1) s |
| 32…35 | VEL→pluck, alt. pluck, pluck usa máscara, pluck só no release | bool | |
| 36 | Master volume | 0…2000 (1000) | x ∈ [0,2]; **ganho = 0,75·(3^x − 1)/2** → 1000 = 0,75 (−2,5 dB), 2000 = 3,0 (+9,5 dB) |
| 37 | VEL → volume | bool (dados: 1 em 58 %) | |
| 38 | **Master pitch** | ±2400 (0) | **centésimos** de transposição. Dados: baixos usam −2400/−1200 **[DADOS]** |
| 41 | Pitch vibrato depth | 0…1000 | interno v·0,075 (máx. 75; **[HIP]** centésimos) |
| 42 | Pitch LFO amount | ±1000 | **±(101^|x| − 1)·12 centésimos** (máx. ±1200 = ±1 oitava) **[DECOMP; unidade HIP]** |
| 43 | Portamento mode | bool | |
| 44 | Monophonic | bool | |
| 45 | Portamento / legato time | ±1000 (0; dados: 400) | **t = (41^|x| − 1)/26,667 s** (400 → 0,13 s; 1000 → 1,5 s). Negativo = "Inverted porta" **[STR+HIP]** |
| 46 | VEL → porta time | bool | |

Tabela de referência (s):

| v | attack (26) | decay (27) | release (28) | env. filtro (48/49) | porta (45) |
|---|---|---|---|---|---|
| 0 | 0 | 0,05 | 0,010 | 0 | 0 |
| 100 | 0,029 | 0,17 | 0,025 | 0,12 | 0,017 |
| 250 | 0,109 | 0,61 | 0,064 | 0,56 | 0,057 |
| 500 | 0,45 | 3,7 | 0,24 | 3,7 | 0,20 |
| 750 | 1,54 | 21 | 0,78 | 21 | 0,57 |
| 1000 | 5,0 | ∞ | 2,5 | ∞ | 1,5 |

Envelope **[DECOMP]** (`FUN_00784a10` + `FUN_005f5f60`): ataque linear; decay e release são multiplicações
por `exp(ln(0,01)/N)` por amostra, ou seja, atingem **1 % (−40 dB) em N amostras** = o tempo da tabela.
Há ainda uma rampa de *declick* fixa de 80 ms quando uma voz é roubada.

**Filtro**

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 47 | Adaptive filter envelope mode | bool (dados: 1 em 86 %) | muda a escala do env amount (abaixo) |
| 48 | Filter env attack | 0…1000 (0) | 1000 = infinito; senão t ≈ 0,12·(1001^x − 1) s |
| 49 | Filter env decay | 0…1000 (500) | idem (decai até 0; sem sustain) |
| 50, 51 | Attack / decay slope | ±1000 (decay: −300) | curvatura **[HIP]** |
| 52 | **Filter env amount** | ±1000 (0; DFM valor 500) | a = x. Não adaptativo: deslocamento = a·(faixa total, ≈ 7,9 oitavas); adaptativo: a sem escala (fração da faixa restante) **[DECOMP para a escala; HIP para o significado adaptativo]** |
| 53 | VEL → env amount | bool | |
| 54 | **Filter frequency** | 0…8192 (4096) | posição na escala log: `f ≈ 73,4 Hz · 2^(7,855·v/8192)` (§1.3) |
| 55 | Filter scale | 0 = oitava, 1 = Hz | |
| 56 | Filter width | 0…1000 (500) | largura da curva = 1024·2^(1 − 0,004·v) (interno) |
| 57 | **Key to filter frequency** | ±2000 (1000) | keytracking = x (1000 = 100 %) |
| 58 | Filter LFO amount | ±1000 | interno x·6 (**[HIP]** oitavas) |
| 59 | Resonance amount | 0…1000 (0) | x |
| 60 | Resonance scale | 0 = oitava, 1 = Hz | |
| 61 | Resonance offset / noise length | ±2400 | v/1200 oitavas (centésimos) |
| 62 | Resonance self-oscillation | 0…2048 | v/256 |
| 63, 64 | Adaptive width, track freq motion | bool | |

**Uníssono, phaser, harmonizador, LFO, efeitos**

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 65 | **Unison order** | 1…9 (1) | nº de vozes; ganho de compensação = 1 − (3^(1−1/n) − 1)/2 (n=2: 0,63; n=9: 0,17) |
| 66 | Unison panning | 0…128 (128) | v/128 |
| 67 | **Unison pitch thickness** | 0…256 (128) | (41^(v/256) − 1)·1,875 → 128 ≈ 10, 256 = 75 (**[HIP]** centésimos de espalhamento) |
| 68 | Unison phase | 0…128 (64) | v/128 |
| 69 | Unison variation | 0…128 | v/128 |
| 70 | Unison type | 0 Classic, 1 Uniform, 2 Blurred | |
| 71…78 | Phaser mix, scale (oct/Hz/harmonic "use for PWM"), width (v·0,0001), width LFO (v·0,0005), offset (mostrado v·0,36), offset LFO, motion speed (como o LFO), key→offset | | |
| 79…85 | Harmonizer mix, width, width LFO, VEL, strength (1,331·2^(v/256)), type (Oct up, Oct up +1, Oct up +2, Harm up, Harm up +1), position (Pre-filter, Pre-phaser, Pre-pluck, Final) | | |
| 86 | **LFO shape / source** | 0…13 | 0 Sine, 1 Smooth saw, 2 Smooth pulse, 3 Saw, 4 Pulse, 5 Stairs, 6 Triangle, 7 Sickle, 8 Multisine, 9 Filter envelope, 10 Filter frequency, 11 Voice random, 12 Velocity, 13 Pitch |
| 87 | Global / retriggered LFO | bool | |
| 88 | LFO attack | 0…1000 | t = 0,05·(101^x − 1) s |
| 89 | **LFO speed** | ±8000 (3000) | **período = 64 / 2^(|v|/1000) tempos** (3000 → 8 tempos; 6000 → 1 tempo; 8000 → 1/4 tempo). 0 = "Still"; negativo = mesmo período com sinal invertido (fase ao contrário). A UI mostra em passos:ticks com 48 ticks/passo |
| 90 | Chorus type | 0…9 | 0 Off, 1 Classic, 2 Thick, 3 Deep, 4 Flanger, 5 Alt flanger, 6 Stereoized, 7 Modulation, 8 Surround, 9 Vibration |
| 91 | Chorus mix | 0…1000 (500) | x |
| 92 | Delay type | 0…3 | 0 Off, 1 Classic, 2 Ping-pong, 3 Stereoized |
| 93, 94 | Delay input, feedback | 0…1000, 0…900 | x |
| 95 | **Delay time** | 0…384 (144) | **ticks a 192 por tempo** (144 = 3/16, 192 = 1/4) |
| 96 | Reverb type | 0…6 | 0 Off, 1 Classic, 2 Large hall, 3 Cathedral, 4 The venue, 5 Drum room, 6 Small studio |
| 97 | Reverb wet | 0…1000 (500) | x |
| 98 | Compression level | 0…1000 (500) | x |
| 99 | VEL → unison thickness | bool | |

**Os índices dos combos ignoram os separadores `-` / `-Special` do DFM** (o combo de efeito 0…9 tem 10
itens úteis; o de filtro 0…16 tem 17) **[DECOMP+DADOS]**: confirmado porque os tipos 3 e 7 (Classic HP e
Crude HP) recebem a mesma configuração de "passa-altas" em `FUN_0077ca30`.

### 1.3 O filtro em Hz **[DECOMP, margens HIP]**

O filtro do Harmless é uma **curva tabelada** (escolhida pelo combo, gerada em `FUN_0077ca30`) aplicada às
amplitudes das harmônicas. A posição da curva vive numa escala log2 de frequência:

- extremos base (construtor, `0x77bb43`): `fBaixo = 440·2^(−31/12) ≈ 73,4 Hz`, `fAlto = 17 000 Hz`
  (≈ 7,86 oitavas). `FUN_0077c170` alarga essas pontas por uma margem que depende da forma e da largura
  (param 56), para o filtro poder fechar/abrir totalmente.
- `posição = lo + (v/8192)·(hi − lo)`. Aproximação sem margens: **`f ≈ 73,4·2^(7,855·v/8192)` Hz**
  (0 → 73 Hz; 2048 → 286 Hz; 4096 → 1,1 kHz; 6144 → 4,4 kHz; 8192 → 17 kHz).
- Keytracking (57) padrão 100 %: a posição acompanha a nota **[HIP: referência C5]**.

Tipos (tail[0], §1.4): 0 Classic LP, 1 Classic BP, 2 Classic BS, 3 Classic HP, 4 Crude LP, 5 Crude BP,
6 Crude BS, 7 Crude HP, 8 Hill LP, 9 Leg LP, 10 Hybrid LP, 11 Snake LP, 12 Fin LP, 13 Leaky LP,
14 Phaser, 15 Band phaser, 16 Flat band pass. Nos 160 canais: 0 (69), 4 (47), 1 (11), 9 (7), 16 (5)…

### 1.4 Os 7 inteiros em 0x194 (combos com `Tag = −1`) **[DECOMP+STR+DADOS]**

Mapeados pelos campos publicados do formulário (`FilterTypeSelect @0x6F8`, `ResTypeSelect @0x700`,
`PhaserTypeSelect @0x770`, `CompTypeSelect @0x9F8`, `HarmColSelect @0x778`, `ThreadedBox @0x7D8`,
`QualitySelect @0xA28`):

| Off | Campo | Valores (sem separadores) | Dados (594) |
|---|---|---|---|
| 0x194 | Tipo de filtro | 0…16 (§1.3) | 0–16 |
| 0x198 | Tipo de ressonância | 0 Classic, 1 Spring, 2 Pedestal, 3 Sedge hat, 4 Wide bump, 5 Double cone, 6 Well, 7 Wormhole, 8 Noise | 0–8 |
| 0x19C | Tipo de phaser | 0 Classic, 1 Triangle, 2 Eggs, 3 Deep, 4 Deeper, 5 Condom, 6 Twins, 7 Cascade, 8 Box, 9 Freq | 0–9 |
| 0x1A0 | Compressão | 0 Off, 1 Limiting, 2 Warming, 3 Heating, 4 Distortion | 0–4 |
| 0x1A4 | **Timbre color** | 0 Saw, 1 Bright saw, 2 Brighter saw, 3 Dephased saw, 4 Dephased saw 2, 5 Squaretooth, 6 Squaretooth 2, 7 Mole, 8 Double saw, 9 Triple saw, 10 Triple saw 2, 11 **Custom** | 0 (298), 11 (155) |
| 0x1A8 | Threaded (bool) | | 1 (583) |
| 0x1AC | Qualidade | 0/1 | |

### 1.5 Tabela custom (timbre color = Custom)

`n = 524` entradas de 8 B = 1048 floats. A primeira metade começa em 1,0 e cai ~1/k (1,0; 0,525; 0,353;
0,266…) = **amplitude da harmônica k**; a segunda metade é outra curva por harmônica (fase?) **[HIP]**.
Para o LYNX: se as amplitudes forem ~1/k, trate como serra; se só ímpares forem relevantes, quadrada.

### 1.6 Estatística dos 160 canais em `.flp` **[DADOS]**

- Timbre color: Saw 90, Custom 37, outros 33. Mask mix > 0 em 37.
- Uníssono: ordem 1 (46), 2 (30), 3 (18), 5 (21), 7 (14), 9 (16)…
- Volume decay infinito em 102/160 (= sustain cheio); pluck desligado (1000) em 103.
- Mono 46, portamento 53. Adaptive env 146.
- Efeitos: chorus ligado 84, delay ligado 90, reverb ligado 117; phaser mix > 0 em 63, harmonizador 42.
- Exemplos (presets): "Bass Psy 1" A 0 / D 1,35 s / R 0,06 s, corte 4096 (~1,1 kHz), env 752, mono, pitch −2400;
  "EpicPad NUC" A 0,73 s / D ∞ / R 0,50 s, uníssono 9; "WobbleBass NUC" res 873, mono. Coerente com as
  conversões acima.

### 1.7 Como aproximar no sintetizador do LYNX

1. **Osciladores** (timbre color + máscara + sub):
   - Saw / Bright / Dephased / Custom ~1/k → `saw`. Squaretooth → osc1 `saw` + osc2 `square` (níveis 0,5).
     Double/Triple saw → 2–3 `saw` com detune leve (±7 ct). Mole → `triangle` **[HIP]**.
   - Mask mix m (23) com a máscara padrão ímpar: `level(saw) = 1 − m`, `level(square) = m`
     (ou só `square` se m ≥ 0,5 e não houver osciladores livres).
   - Sub harmonic 1 (2) > 0 → osc3 `sine` uma oitava abaixo, nível = v/1024 (limite 1).
   - Transposição: `pitch (38)/100` semitons (divida em oitava + semitom).
2. **Uníssono** (65 ≥ 2): use os 3 osciladores com a mesma forma, detune `0, +d, −d` com
   `d = (41^(v67/256) − 1)·1,875` centésimos (mínimo 5 ct), pan `±v66/128`. Nível total × ganho de
   compensação (§1.2). Se a ordem for 1, os osciladores livres ficam para máscara/sub.
3. **Envelope de amplitude:** attack = tabela (26). Se decay (27) = 1000 → `decay = 0, sustain = 1`;
   senão `sustain = 0` e `decay = t27` (se o ADSR do LYNX for exponencial por constante de tempo,
   use `τ = t27/4,6`). Release = t28 (idem `τ = t28/4,6`).
4. **Filtro:** tipos LP (0, 4, 8–13) → `lowpass`; BP (1, 5, 16) → `bandpass`; BS (2, 6) → `notch`;
   HP (3, 7) → `highpass`; Phaser/Band phaser (14, 15) → `bandpass` largo. Cutoff Hz = §1.3; ressonância
   LYNX = `0,9·x59` (+0,1 se self-osc 62 > 0). Keytrack = x57.
5. **Envelope de filtro:** attack t48, decay t49, sustain 0, release = release de volume; amount em
   oitavas ≈ `x52 · 7,9` (não adaptativo) ou `x52 · log2(17000/fc)` se positivo e adaptativo **[HIP]**.
6. **LFO:** forma (86) 0/1/8 → sine, 3/6 → tri, 2/4/5 → pulse; período = 64/2^(|v89|/1000) tempos
   (em beats no LYNX). Destinos: pitch (42, em semitons = cents/100), cutoff (58, oitavas ≈ 6·x),
   volume (25, tremolo). Delay/fade-in do LYNX = LFO attack (88). Vibrato (41) → LFO de pitch extra
   ~5 Hz com profundidade `0,075·v` ct **[HIP]**.
7. **Mono/portamento:** mono = 44; porta ligado se 43; tempo em beats = `t45 · BPM/60`.
8. **Volume:** ganho do canal × `0,75·(3^(v36/1000) − 1)/2`.
9. **Efeitos (90–98):** o LYNX não tem rack no sintetizador; se houver FX de canal/mixer, mapeie
   delay (tipo 2 = ping-pong, tempo = v95/192 tempos, feedback x94, mix x93) e reverb (wet x97); senão
   ignore e registre no log de importação.

---

## 2. MiniSynth **[DECOMP+STR+DADOS]**

MiniSynth é o sintetizador do **FL Studio Mobile** (C++, classe `CMiniSynth`, tabelas de onda em
`oscdata.dat`). Não usa DFM: a lista de parâmetros vem do construtor (`FUN_180022a30`), que registra
43 nomes por índice.

### 2.1 Layout do `0xD5` (contêiner "SSLF") **[DADOS+DECOMP]**

| Off | Conteúdo |
|---|---|
| 0x00 | `"SSLF"` + u32 tamanho do resto |
| 0x08 | assinatura `99 46 4C 53 0D 0A 1A 0A` ("\x99FLS\r\n\x1a\n", estilo PNG) |
| 0x10 | chunk `"HEAD"` + u32 tamanho: u16 1, u16 1, u16 `L`, `L` caracteres UTF-16 = **nome do preset** |
| … | chunk `"DATA"` + u32 tamanho (0xB0 = 176): tag `"0rev"` + **43 floats 0…1** |

O tamanho total varia só com o nome ("Default" → 228 B, "Default Preset" → 242 B, vazio → 214 B).

### 2.2 Parâmetros (float p ∈ [0,1]; `FUN_180023c20` = set, `FUN_180023580` = texto, `FUN_1800234b0` = default)

| i | Nome | Def. | Conversão |
|---|---|---|---|
| 0 | Voices Mode | 0,375 | `int(2p)`: 0 POLY, 1 MONO, 2 LEAD (mono legato **[HIP]**) |
| 1 | Transpose | 0,5 | `int((p − 0,5)·24)` semitons (±12) |
| 2 | Slide Time | 0 | coeficiente de glide por bloco `0,9·(1−p)^4 + 0,01` (p=0: instantâneo; p=1: lento) |
| 3 | MW Dest | 0 | 0 Filter, 1 LFO |
| 4 | MW Amount | 0 | p (mod wheel → cutoff ±4 oitavas·p) |
| 5 | **OSC Kind** | 0 | `int(9p)` (§2.3) |
| 6 | OSC Mod | 0 | p (largura/detune/espalhamento, depende do Kind) |
| 7 | OSC Noise | 0 | nível de ruído = p² |
| 8 | **FLT Freq** | 1 | §2.4 |
| 9 | FLT Peak | 0 | ressonância = 0,99·p |
| 10 | FLT Kbd | 0 | keytracking p (oitavas por oitava, referência nota 48) |
| 11 | FLT Vel | 0 | ±7 oitavas·p·(vel − 0,5)·2 |
| 12–15 | EGF Atk / Dec / Sus / Rel | 0 / 1 / 1 / 1 | §2.5 (sustain = p) |
| 16 | EGF Amnt | 0,5 | a = 2p − 1 (bipolar) |
| 17 | LFO Shape | 0 | `int(2p)`: 0 SINE, 1 SAW, 2 SQUARE |
| 18 | LFO Dest | 0 | `int(2p)`: 0 FLT (cutoff ±4 oit·amt), 1 FRQ (pitch ±12 st·amt), 2 OSCMOD (soma 12·amt ao OSC Mod) |
| 19 | LFO Rate | 0,25 | `int(11p)` → 1/4, 3/8, 1/2, 3/4, 1/1, 2/1, 4/1, 8/1, 16/1, 4 Bars, 8 Bars, 16 bars (valor interno 0,25…256; **[HIP]** período em passos de 1/16; o rótulo do índice 9 é sobrescrito de "2 Bars" para "4 Bars") |
| 20 | LFO Amnt | 0,5 | 2p − 1 |
| 21–24 | AMP Atk / Dec / Sus / Rel | 0 / 1 / 1 / 0,07 | §2.5 |
| 25 | DIST (liga) | 0 | bool |
| 26 | Overdrive | 0 | p |
| 27 | Decimator | 0 | p |
| 28–32 | DELAY liga, Time (`(int(3p)+1)/4`: 1/4, 2/4, 3/4, 1/1), Fdbk, Filter, Mix | | |
| 33–36 | CHORUS liga, ModRate, ModDepth, Mix | | |
| 37–42 | PHASER liga, ModSpeed (tabela de 12 valores 0,25…256), ModWidth, Fdbk, Mix, Freq | | |

### 2.3 Osciladores (`FUN_180021d10`) **[DECOMP]**

Todos são somas de **serras** de tabela (com mip-maps) com ganho ±1 e fase/frequência relativas; `m` = OSC Mod:

| Kind | Nome | Estrutura |
|---|---|---|
| 0 | SAW | serra + (serra − serra defasada de `1−m` ciclo) → serra com componente de pulso (m=0: serra pura) |
| 1 | SAW+SAW | 2 serras: f e f·r(m) (r = desafinação dependente de m **[HIP]**) |
| 2 | PULSE | serra − serra defasada (`1 − m`) = **pulso com PWM** |
| 3 | SQR+SAW | quadrada (serra − serra a ½ ciclo) + serra em f·r(m) |
| 4 | SQR+SQR | 2 quadradas: f e f·r(m) |
| 5 | SUPERSAW | **8 serras**, ganho 0,5, frequências `f·(1 + (0,0125·k − 0,05)·m)`, k = 0…7 (−5 %…+3,75 % · m) |
| 6 | BELL | 6 serras na mesma frequência com fases espalhadas de `m/2` ciclo |
| 7 | DEAF SAW | 2 serras somadas defasadas de `1 − m` |
| 8 | SAW SPREAD | serras em f/2·(1 − 0,01m), f e 2f·(1 + 0,01m) |
| 9 | SAW SPREAD 2 | idem com 1,5f·(1 − 0,01m) no lugar de f/2 |

Afinação (`FUN_180022250`): `f = 220·2^((nota_glide + transpose + drift + LFO − 57)/12)` (nota MIDI;
drift aleatório de ±0,08 st por voz) **[DECOMP]**.

### 2.4 Filtro (`FUN_1800213e0`) **[DECOMP]**

Passa-baixas de **4 polos em cascata (ladder, 24 dB/oit)** com realimentação `−4,05·peak` (auto-oscila
perto de peak = 1). Cutoff em oitavas acima de 1 Hz:

`oct = 4 + 10,3·F + Kbd·(nota − 48)/12 + 14·Vel·(vel − 0,5) + 0,103·drift (+ 4·LFO se dest FLT) (+ 4·MW)`
→ **`fc = 2^oct` Hz** (F = 0 → 16 Hz; 0,5 → 570 Hz; 0,75 → 3,4 kHz; 1 → 20 kHz, limitado a 20 kHz).
Envelope: `fc ← fc·(1 + (2^(10·a) − 1)·envF)` (a = EGF Amnt bipolar; a = +1 abre até +10 oitavas).

### 2.5 Envelopes (`FUN_1800229c0`, `FUN_180022440`) **[DECOMP]**

Por amostra: `env += (alvo − env)·taxa`. Ataque persegue alvo 4,0 e passa para decay quando env > 1;
decay persegue o sustain. Com `k = 0,95p + 0,05`:
- taxa de ataque = `0,0002/(500·k⁴)` → **t_ataque ≈ 0,288·500·k⁴/0,0002 amostras** (p=0,25: 0,11 s;
  0,5: 1,24 s; 1: 16 s a 44,1 kHz); p < 0,001 → instantâneo.
- decay / release do filtro: constante de tempo **τ = 50·k⁴/0,0002 amostras** (p=0,5: 0,43 s; 1: 5,7 s).
- release de amplitude: τ = 1/10 disso com `k' = 0,95k + 0,05` (p=0,07 → ~1 ms; p=0,5 → 0,05 s;
  p=1 → 0,57 s).
- As taxas não são corrigidas pela taxa de amostragem **[HIP: tempos escalam com 44,1 kHz/fs]**.

### 2.6 Dados (19 instâncias) e aproximação no LYNX

Kinds usados: SAW (9), SUPERSAW (8), SQR+SAW (2); todos POLY; filtro aberto em 10, peak ≤ 0,15 **[DADOS]**.

- SAW → osc1 `saw`. PULSE → osc1 `square` (sem PWM no LYNX). SQR+SAW → osc1 `square` + osc2 `saw`.
  SAW+SAW / SQR+SQR → duas formas iguais com ±10 ct. SUPERSAW → 3 `saw` com detune
  `0, ±1200·log2(1 + 0,05·m)` ct. SAW SPREAD → `saw` em −1, 0, +1 oitava. Noise → osc3 `noise`, nível p².
- Filtro `lowpass`, `cutoff = 2^(4 + 10,3·F)` Hz, ressonância `0,99·peak`, keytrack = Kbd.
- Envelopes: converta com as fórmulas acima (τ → tempo do LYNX: decay ≈ 4,6·τ para −40 dB).
- Filter env amount em oitavas = `10·(2p − 1)`.
- LFO: dest FLT → LFO de cutoff (4·amt oitavas), FRQ → LFO de pitch (12·amt semitons).
- Mono = Voices ≥ 1; portamento só se Slide > 0.

---

## 3. Fruity DX10 **[DECOMP+STR+DADOS]**

DX10 é um FM de 2 operadores no estilo do "DX10" da mda (portado e estendido pela IL com um segundo
modulador). Nomes, *tags* e faixas do DFM `TSynthEditorForm`.

### 3.1 Layout do `0xD5` (`SaveRestoreState` = `FUN_00734940`)

| Off | Conteúdo |
|---|---|
| 0x00 | u32 versão: 1 (92 B) ou 2 (460 B); ≥ 3 é ignorado |
| 0x04 | **22 × i32** parâmetros (`P+0x1B8`) |
| 0x5C | (v2) 364 B: o mesmo instantâneo de envelope/filtro do canal do 3x Osc (rodada 4 §1.4), aplicado só se vierem ≥ 364 B |

Valor normalizado `p = (v − min)/(max − min)`: todos 0…65536 exceto Octave (−3…3).

### 3.2 Parâmetros e conversões (`FUN_00735760`, texto em `FUN_00735220`)

`fs` = taxa de amostragem. "τ" é a constante de tempo do filtro de 1 polo que forma o envelope.

| i | Nome (DFM) | Conversão |
|---|---|---|
| 0 | Attack | **τ = e^(8p − 8) s** (p=0: 0,3 ms; 0,5: 18 ms; 1: 1 s) |
| 1 | Decay | p > 0,98 → **sem decay (sustain cheio)**; senão decaimento multiplicativo com **τ = e^(8p − 5) s** (0,5: 0,37 s) — decai a zero |
| 2 | Release | **τ = e^(5p − 5) s** (0: 7 ms; 1: 1 s) |
| 3 | Mod 1 Coarse | parte inteira da razão = `floor(40,1·p²)` (0…40) |
| 4 | Mod 1 Fine | p < 0,5 → `+0,2·p²`; senão `round(8,9p)`: 4 → +0,25, 5 → +1/3, 6 → +0,5, 7 → +2/3, 8–9 → +0,75 |
| 5 | Mod 1 Init | índice inicial ∝ `0,0002·p²·44100/fs` |
| 6 | Mod 1 Time (decay) | τ = e^(7p − 6) s |
| 7 | Mod 1 Sustain | índice de sustentação ∝ 0,0002·p² |
| 8 | Mod 1 Release | τ = e^(8p − 5) s |
| 9 | Mod 1 Velocity | sensibilidade p |
| 10 | Vibrato | profundidade 0,001·p² |
| 11 | Waveform | "riqueza" = 0,5 − 3p² (realimentação/forma do modulador **[HIP]**) |
| 12 | Mod Thru | mistura direta do modulador na saída = 0,25·p² |
| 13 | LFO Rate | **25·p² Hz** (texto da UI "%7.2f") |
| 14–20 | Mod 2: Coarse, Fine, Init, Time, Sus, Rel, Vel | como 3–9 |
| 21 | Octave ("COARSE") | −3…+3. Afinação base = `8,1758 Hz·2^(oct+1)·2^(nota/12)` → com 0 o DX10 soa **uma oitava acima** da nota MIDI (herança do mda) **[DECOMP; HIP que "nota" é o nº MIDI]** |

Razão final do modulador = coarse + fine (ex.: preset padrão 9,02; "Thick Bass" 1,00; "Clunk Bass" 2,00;
"Robot" 0,33) **[DADOS]**. A topologia exata (mod 2 em série ou paralelo) não foi lida **[HIP]**.

### 3.3 Dados (75 instâncias, 12 em `.flp`)

v1/92 B = 73, v2/460 B = 2. Attack 0 em 49; Octave 0 (32), −1 (20), 2 (10); Mod 2 desligado (Init 0) em 48.

### 3.4 Aproximação no LYNX (sem FM)

- osc1 `sine` (portadora) com oitava = `Octave + 1`.
- Brilho do FM → forma do osc2 pela razão R e índice inicial I (p5): R ≈ 1 → `saw`; R ≈ 2 → `square`;
  R ≥ 3 inteiro → `sine` no harmônico R (semitons = 12·log2 R) com nível ∝ p5; R não inteiro → osc2 `sine`
  desafinado (som metálico). Nível osc2 ≈ `min(1, 2·p5)`.
- O decaimento do índice (Mod Time p6 → Sus p7) vira **envelope de filtro**: LP com cutoff alto,
  amount = `+4·p5` oitavas, decay ≈ 4,6·τ6, sustain = p7/p5.
- Mod Thru → osc3 `sine` na frequência do modulador, nível 0,25·p12².
- Amplitude: attack ≈ 3·τ0 (para chegar ~95 %), decay = 4,6·τ1 com sustain 0 (ou sustain 1 se p1 > 0,98),
  release 4,6·τ2.
- Vibrato: LFO de pitch, `25·p13²` Hz.

---

## 4. Fruit Kick **[DECOMP+STR+DADOS]**

### 4.1 Layout (`SaveRestoreState` = `FUN_0072d690`)

| Versão | Layout |
|---|---|
| 0 (32 B) | u32 0 + 6 × i32 parâmetros + 1 i32 sem uso (o leitor lê 28 B; lixo como 4504291) |
| 1 (400 B) | u32 1 + 6 × i32 + 372 B: instantâneo de canal de 364 B (como §3.1) se ≥ 364 B |

### 4.2 Parâmetros (Tag = índice; `ProcessParam` `FUN_0072dce0`, `TriggerVoice` `FUN_0072e2f0`)

| i | Nome | Faixa (def.) | Conversão |
|---|---|---|---|
| 0 | Max freq | −2400…3600 (0) | pitch inicial (ct) = `max + pitchDaNota + 1200`, limitado a [pitch final, 4800] |
| 1 | Min freq | −1200…1200 (−600) | pitch final (ct) = `min − 3600`, fixo (não segue a nota) |
| 2 | Freq decay | 0…256 (64) | coef. por amostra `0,9997 − (1001^(1−v/256) − 1)·9,7e−6` (ajustado à fs) → τ: 0 → 2 ms, 64 → 11 ms, 128 → 38 ms, 256 → 76 ms |
| 3 | Amp decay | 0…256 (128) | coef. `0,99998 − (1001^(1−v/256) − 1)·4,98e−6` → τ: 64 → 25 ms, 128 → 0,13 s, 256 → 1,13 s |
| 4 | Click | 0…64 (32) | nível do clique |
| 5 | Distortion | 0…128 (0) | d = v/128: seco ×(1 − d), drive ×3,3d |

Os "ct" são relativos à referência de pitch do host **[HIP: 0 ct = C5 = 523,25 Hz ⇒ min −600 → ~46 Hz]**.
Amplitude inicial 1,5. Presets: Default (0, −600, 64, 128, 32, 0), Low (−2400, −832, 143, 127, 0, 0),
Punchy (624, −8, 42, 86, 64, 20) **[DADOS]**.

### 4.3 LYNX

osc1 `sine`; pitch envelope com attack 0, decay ≈ 4,6·τ(freq decay), amount (semitons) =
`(início − fim)/100`; ADSR de volume attack 0, decay ≈ 4,6·τ(amp decay), sustain 0; click → osc3 `noise`
com nível `v/64·0,3` e envelope muito curto (se o LYNX não tiver envelope por oscilador, use AM rápido ou
ignore). Distorção: sem equivalente.

---

## 5. BooBass **[STR+DADOS]**

`0xD5` = 16 B: u32 versão 1 + 3 × i32 0…65535 (centro 32767): **[0] Bass, [1] Mid, [2] Treble** (Tags 0, 1, 2
do DFM). O motor (baixo modelado) não foi analisado. LYNX: osc1 `saw` + osc2 `sine` −1 oitava, LP com
`cutoff ≈ 300·2^(4·treble)` Hz, ressonância 0,2, nível do sine ∝ Bass, decay curto **[HIP]**.

## 6. Drumpad **[STR+DADOS]**

Plugin de bateria física do FL Mobile. `0xD5` = `"SSLF"` + u32 tamanho (0x11C) + tag `"3PMD"` + nome do
patch em ASCII num campo fixo (ex.: "War Drum Lo 01\0 War Drum 3") + **37 floats 0…1** a partir de 0x90
(292/300 B). O DFM lista 37 controles (Tags 1…37: Retrig, Mallet amp/decay/noise/noise RP, Membrane
decay/cutoff/tension/phase/material/size/shape, LOW freq/reso, SOF freq/decay/level, MID freq/level/res/…,
HIGH freq/level/res, Lo-Fi, Hold, Pitch, 4 fontes/valores de velocidade); a ordem float↔Tag não foi
confirmada **[HIP: float k = Tag k+1]**. 961 dos 975 são presets `.fst`; só 11 canais em `.flp`. Sem
mapeamento útil para 3 osciladores: use um "tom" genérico (sine com pitch envelope + noise) ou renderize.

---

## Recomendações para o LYNX

1. **Despachar pelo `0xC9`** (comparando sem diferenciar maiúsculas: "Fruit kick"/"Fruit Kick",
   "Drumpad"/"DrumPad", "DX10"/"Fruity DX10") e, para todos, **continuar aplicando os eventos de canal**
   (`0xDA` envelope/LFO de canal, `0xDB` filtro do canal, rodadas 3/4): estes geradores são hospedados como
   o 3x Osc. Os instantâneos de 364 B dentro do DX10 v2 e Fruit Kick v1 só servem de plano B.
2. **Harmless primeiro** (160 canais): parser = `u32 versão` + `100 × i32` (99 se versão < 9) + `7 × i32`
   (se versão ≥ 4) + tabela custom opcional; ignore o byte final. Aplique as conversões de §1.2 e o
   mapeamento de §1.7. Ganho rápido de fidelidade: uníssono → 3 osciladores desafinados, envelope ADR
   (decay 1000 = sustain cheio) e filtro em Hz pela escala log 73 Hz…17 kHz.
3. **MiniSynth:** ler o contêiner SSLF genérico (HEAD = nome, DATA = `"0rev"` + floats) — o mesmo
   contêiner serve para o Drumpad e provavelmente outros plugins do FL Mobile. Mapear Kind → formas (§2.6)
   e o filtro ladder → `lowpass` com `fc = 2^(4 + 10,3·F)`.
4. **DX10:** não há FM no LYNX; a aproximação "seno + osc harmônico + envelope de filtro imitando o índice"
   (§3.4) é o melhor custo/benefício. Se o LYNX ganhar FM de 2 operadores, o mapeamento direto é trivial
   (razão = coarse + fine, índice com envelope init→sus).
5. **Fruit Kick:** mapeia bem: seno + envelope de pitch + decay de amplitude.
6. **Registrar no log de importação** o que foi descartado (FX internos do Harmless, phaser/harmonizador,
   Drumpad, efeitos do MiniSynth), para o usuário saber por que soa diferente.
7. **Validação sugerida:** renderizar no FL os presets "Bass Psy 1", "EpicPad NUC" (Harmless), "Default"
   (MiniSynth), "Thick Bass" (DX10), "Default" (Fruit Kick) e comparar envelope/espectro com o LYNX —
   em especial a referência de pitch do Harmless (pitch −2400 nos baixos) e do Fruit Kick.

## Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| `tmp/r10/findself.py` | Localiza VMT (self em VMT−0xC8) | DECOMP |
| Harmless `FUN_0077adb0` (VMT+3) | `SaveRestoreState`: versões, 100 params, 7 combos, tabela custom, byte final | DECOMP |
| Harmless `FUN_0077f630` (VMT+6) | `ProcessParam`: conversões de todos os parâmetros | DECOMP |
| Harmless `FUN_00781200` | Texto dos valores (ms, "Infinite", "Still", "Unison order", %/dB) | DECOMP |
| Harmless `FUN_00614c80` | Constantes ln2, ln11, ln3, ln2/12 | DECOMP |
| Harmless construtor `0x77bb43…` | Faixa do filtro 73,4 Hz…17 kHz | DECOMP |
| Harmless `FUN_0077c170`, `FUN_0077ca30` | Escala log do filtro; curvas por tipo (tipos 3/7 = HP) | DECOMP |
| Harmless `FUN_00784a10`, `FUN_005f5f60` | Render da voz; envelopes exponenciais a −40 dB | DECOMP |
| Harmless DFM `TSynthEditorForm` + tabela de campos | Tags, faixas, defaults, itens dos combos; offsets 0x6F8…0xA28 | STR |
| MiniSynth `FUN_180022a30` | Construtor: 43 nomes de parâmetros | DECOMP |
| MiniSynth `FUN_180023c20`, `FUN_1800234b0`, `FUN_180023580` | Set/default/texto dos parâmetros | DECOMP |
| MiniSynth `FUN_180021d10` | Estrutura dos 10 tipos de oscilador | DECOMP |
| MiniSynth `FUN_180022250`, `FUN_1800213e0` | Afinação (220 Hz @ 57); filtro ladder e cutoff | DECOMP |
| MiniSynth `FUN_1800229c0`, `FUN_180022440` | Taxas e forma dos envelopes | DECOMP |
| DX10 `FUN_00734940` (VMT+3), `FUN_00734f30` (VMT+6) | Layout e normalização | DECOMP |
| DX10 `FUN_00735760`, `FUN_00735660/690`, `FUN_00735220` | Conversões, razões, texto | DECOMP |
| Fruit Kick `FUN_0072d690`, `FUN_0072dce0`, `FUN_0072dcb0`, `FUN_0072e2f0` | Layout, conversões, pitch inicial/final | DECOMP |
| DFMs de DX10, Fruit Kick, BooBass, Drumpad | Nomes/Tags/faixas | STR |
| 594 Harmless, 19 MiniSynth, 75 DX10, 15 Fruit Kick, 7 BooBass, 976 Drumpad (`.flp` + `.fst`) | Versões, tamanhos, faixas, uso | DADOS |
| Scripts `tmp/r10/collect.py`, `hstat.py` | Coleta e estatística | DADOS |

### Pendências

- Harmless: papel do parâmetro 39 ("Detuning"), unidade exata do vibrato (41) e do LFO de filtro (58),
  semântica do envelope "adaptativo", referência de pitch do keytracking e do master pitch.
- MiniSynth: função de desafinação r(m) (`FUN_1800371f0`) e unidade do LFO Rate.
- DX10: topologia do segundo modulador e uso exato de "Waveform".
- Fruit Kick: referência de pitch do host (`FUN_0071b2c0`); escala do pitch da nota (`PTR_DAT_00785c50`).
- Drumpad: ordem float↔Tag e modelo de síntese.
