# Análise comportamental do FL Studio 2026: rodada 6 (REA) — EQ do mixer, LFO do canal, AM do 3x Osc, portamento e marcadores

> **Natureza deste documento.** Continuação de `docs/ANALISE_FL_STUDIO.md` e de `REA_rodada2.md` …
> `REA_rodada5.md`. Engenharia reversa *clean-room*, apenas comportamental. Nenhum código nem pseudocódigo
> da Image-Line foi copiado: o que está aqui são descrições de formato e de comportamento escritas com
> palavras nossas. Endereços (`FUN_xxxxxxxx`), offsets e globais servem só para rastrear a evidência.

- **Alvos:**
  - `FL Studio 2026/FLEngine_x64.dll` (SHA-256 `6d9c5ab0…8e02`), Ghidra 12.1.4 via REA + `objdump`
    completo do mesmo binário. É o alvo de todos os endereços das §1, §2, §4 e §5.
  - `Plugins/Fruity/Generators/3x Osc/3x Osc_x64.dll` (SHA-256 `5d84d5ad…dff2`), §3.
- **Validação de formato:** os 165 `.flp` de `FL Studio 2026/Data` (1.186 trilhas de mixer com `0xE1`,
  17.215 registros `0xDA`, 3.443 eventos `0xDD`, 314 marcadores `0x94`).
- **Data:** 2026-10-13

| Marca | Significado |
|---|---|
| **[DECOMP]** | Confirmado lendo a decompilação/desmontagem da(s) função(ões) citada(s) |
| **[STR]** | Inferido de strings, RTTI ou formulários DFM |
| **[DADOS]** | Confirmado estatisticamente nos `.flp` de demonstração |
| **[HIP]** | Hipótese (plausível, ainda não verificada) |

Convenção: `T+0xNN` é um offset no registro de uma trilha do mixer (array global `PTR_DAT_015e06a0`,
passo `0x1478` por trilha, como na rodada 2); `B+0xNN` é um offset dentro de uma **banda** do EQ
(`B = T+0x1D4 + banda·0x88`); `C+0xNN` é o objeto do canal; `V+0xNN` a voz do canal (mesma convenção das
rodadas 3/4). "Tick" é o tick do sequenciador (PPQ do projeto).

---

## 1. EQ de 3 bandas embutido em cada trilha do mixer

### 1.1 IDs de controle no `0xE1` **[DECOMP+DADOS]**

O setter universal (`FUN_01046a70`) trata o bloco `p = 0x1FD0 … 0x1FE7` (24 ids) da classe mixer
`0x70000000 + (trilha << 22) + (slot << 16) + p`:

- `idx = p − 0x1FD0`; **banda = `idx & 7`**, **grandeza = `idx >> 3`** (0 = ganho, 1 = frequência,
  2 = largura). O código reserva 8 bandas por grandeza, mas só 3 existem (o laço de DSP vai de 0 a 2).
- O campo `slot` é **ignorado** para parâmetros da trilha (o setter só usa `id & 0xFFFF` e a trilha). Nos
  demos ele vale sempre **0** (não 0x3F) nos 1.186 × 12 registros de `0x1FC0…0x1FE2` **[DADOS]**.
- Valor bruto gravado em `B+0x00` (ganho), `B+0x04` (freq), `B+0x08` (largura); toda escrita chama o
  recálculo `FUN_0127b390(trilha, banda)` sob a seção crítica do mixer.

| `p` | Banda | Grandeza | Faixa | Default | Visto nos demos |
|---|---|---|---|---|---|
| `0x1FD0/1/2` | 0/1/2 | ganho | **−1800…+1800** (`FUN_0104ef04`: limites da edição por texto) | 0 | −1474…+703 |
| `0x1FD8/9/A` | 0/1/2 | frequência | 0…65536 | 5777 / 33145 / 55825 | 4729…61849 |
| `0x1FE0/1/2` | 0/1/2 | largura | 0…65536 | 17500 | 0…65536 |
| `0x1FC2` | — | separação estéreo | ±64 | 0 | −64…+64 |

### 1.2 Conversões (`FUN_0127b390`) **[DECOMP]**

Os campos derivados ficam em `B+0x10…` como floats (o Ghidra os mostra como `int`, mas são `float`):

```
ganho_dB  = v_ganho · 0,01                                  → B+0x14   (±18 dB)
freq_Hz   = 10 + 159,9 · (101^(v_freq/65536) − 1)          → B+0x18   (10 Hz … 16 kHz)
bw        = v_largura / 65536                               → B+0x1C   (0 … 1)
ativo     = −1 se ganho ≠ 0; senão = contador de cauda      → B+0x0C
```

- A base é **101** (constante `0x4059400000000000` passada ao `ln`), não 100 como dizia a rodada 2
  §2.1. Com 101 os defaults caem em números redondos: 5777 → **90,3 Hz**, 33145 → **1500 Hz**,
  55825 → **8000 Hz**, 65536 → 16000 Hz **[DECOMP+DADOS]**.
- A dica da UI mostra o ganho como `v·0,01` "dB", a frequência em "Hz" e a largura como `v/65536` com 2
  casas (`FUN_01046a70`, ramo `0x1FD0`) **[DECOMP]**.

### 1.3 Forma de cada banda **[DECOMP]**

Na criação da trilha (`0x1287061…0x12870bc`) cada banda recebe um **tipo fixo** em `B+0x10`
(o tipo não é salvo no FLP; é sempre este):

| Banda | Tipo | Forma |
|---|---|---|
| 0 | 6 | **Low shelf** |
| 1 | 7 | **Peaking (sino)** |
| 2 | 8 | **High shelf** |

O cálculo dos coeficientes (`FUN_0069d0e0` + `FUN_0069cb10`) é exatamente o **RBJ "Audio EQ Cookbook"**,
com `w = 2π·f/fs` (a frequência é antes limitada a um máximo dependente de `fs`, `ctx+0x30`, **[HIP:
≈ Nyquist]**), `A = 10^(dB/40)`:

| Tipo | Parâmetro de largura | Fórmula |
|---|---|---|
| 6, 8 (shelves) | **slope** `S = 1,9·bw + 0,1` (0,1…2,0; default 0,607) | `2√A·α = sin w · √((A²+1)/S − (A−1)²)`, coeficientes de shelf do cookbook |
| 7 (peak) | **Q = 0,0999·(1001^bw − 1) + 0,1** (0,1…100; default **0,632**) | `α = sin w/(2Q)`; `b0 = (1+αA)/(1+α/A)`, `b2 = (1−αA)/(1+α/A)`, `a1 = b1 = −2cos w/(1+α/A)`, `a2 = (1−α/A)/(1+α/A)` |
| 1…5 (não usados pelo mixer) | `Q = 18,9·bw + 0,1` | LP, BP (pico 0 dB), notch, HP e all-pass do cookbook |

Quanto **maior** o valor de largura, **mais estreito** o sino (Q cresce exponencialmente) e mais
íngreme o shelf.

### 1.4 Processamento (`FUN_01276d00` → filtro `FUN_0069d500`) **[DECOMP]**

- **Biquad Direct Form I em `double`**, estéreo intercalado, *in-place* no buffer `float` da trilha.
  Estado por banda: 8 doubles (`x[n−1]`, `x[n−2]`, `y[n−1]`, `y[n−2]` para L e R) em `B+0x20`;
  coeficientes `b0, b1, b2, a1, a2` (a0 normalizado) em `B+0x60…+0x78`. No fim de cada bloco o estado
  passa por um *flush* de denormais.
- As bandas rodam em série na ordem **0 → 1 → 2** (low shelf, peak, high shelf).
- **Bypass por banda:** banda com ganho 0 não é processada. Ao zerar o ganho ela continua rodando por
  um "tempo de cauda" (contador `B+0x0C` = global `PTR_DAT_015e32e8`, o mesmo usado no *hold* de cauda da
  trilha; rodada 2 diz `SR/5` **[HIP]**) e depois o estado é zerado. Com as 3 bandas em 0 dB o EQ não
  custa nada e não altera o sinal.
- Não há suavização de coeficientes: mudar um parâmetro recalcula os coeficientes na hora.
- **Posição na cadeia** (confirmando a rodada 2 §2.1): o EQ é chamado em `FUN_01276ee0` **depois** dos 10
  slots de efeito (cada um com dry/wet) e do tap de gravação "pós-FX" (`FUN_01277550`), e **antes** do
  tap "pós-EQ" (`FUN_012775a0`), do fader/pan/separação (`FUN_01276e40`) e do tap "pós-fader"
  (`FUN_012775f0`).

### 1.5 Separação estéreo (`0x1FC2`)

Já descrita na rodada 2 §2.2 **[DECOMP]**: `sep > 0` estreita com `s = (5^(sep/64) − 1)/4`; `sep < 0`
alarga com `s = sep/64`; aplicada na matriz 2×2 do fader, depois do EQ.

---

## 2. LFO do canal (campos LFO dos registros `0xDA`)

Complementa a rodada 3 §1. Campos do registro de 68 B: `+0x00` flags, `+0x24` delay, `+0x28` attack
(fade-in), `+0x2C` amount, `+0x30` speed, `+0x34` shape.

### 2.1 Velocidade → período (`FUN_009ce9e0` + `FUN_009ce950` + `FUN_006614a0`) **[DECOMP+DADOS]**

```
ticks_período = max( round((1000^(v/65536) − 1) · E), 3 )      (0 se v < 101, depois vira 3)
incremento    = floor(2³² / ticks_período)                       (fase de 32 bits por tick)
E = 0,128·PPQ                (flag 0x02 ligada: sincronizado ao tempo)
E = 0,128·PPQ · BPM/120      (flag 0x02 desligada: tempo absoluto)
```

Logo:

- **Livre (TEMPO desligado):** `T = 0,064 s · (1000^(v/65536) − 1)`, independente do BPM.
- **Sincronizado (flag `0x02`):** `T = 0,128 beat · (1000^(v/65536) − 1)`.
- Os valores sincronizados dos demos caem em divisões exatas, o que confirma a escala **[DADOS]**:
  10272 → 0,250 beat (1/16), 6393 → 0,123 (≈ 1/32), 15087 → 0,500, 20643 → 1,000, 26664 → 2,000,
  32950 (default) → 4,000 beats = 1 compasso.
- O período é quantizado em ticks inteiros e o LFO é amostrado **uma vez por tick** (resolução de
  ~5 ms a 96 PPQ / 120 BPM). Mínimo de 3 ticks por ciclo.
- Delay e attack do LFO usam a mesma curva de tempo (e a mesma flag `0x02`): delay em ticks; fade-in
  linear com incremento `1/(ticks+1)` por tick.

### 2.2 Formas de onda (`FUN_009cdfc0`, tabela `0x014255c0`) **[DECOMP]**

`shape` (0…2, com *clamp*) indexa um array de 3 ponteiros para tabelas de 16384 floats. Ordem
**confirmada**: 

| `shape` | Tabela | Forma (fase 0 → 1) |
|---|---|---|
| 0 | `0x015f9860` | **Senoide** `sin(2π·φ)`; começa em 0 subindo |
| 1 | `0x01609860` | **Triângulo** alinhado com a senoide: 0 → +1 (¼) → 0 (½) → −1 (¾) → 0 |
| 2 | `0x01619860` | **Pulso/quadrada**: +1 na 1ª metade, −1 na 2ª |

Índice da tabela = `fase >> 18` (sem interpolação). Nos demos o shape 0 domina (46 de 48 LFOs ativos;
2 usam o shape 2) **[DADOS]**.

### 2.3 Fase: por nota ou global (`FUN_009cedb0`, `FUN_009ce490`, `FUN_009ceec0`, `FUN_009cef00`) **[DECOMP]**

- **Padrão (flag `0x20` desligada): reinicia por nota.** No disparo da voz: fase = 0, fade = 0, contador
  de delay = ticks de delay. A cada tick: enquanto o delay não acabou, só decrementa (saída 0); depois
  `fase += incremento`, `l = tabela[fase>>18]`, e o fade-in sobe `1/(n+1)` até 1 multiplicando `l`.
  Note que a primeira amostra já é com a fase avançada um passo.
- **Global (flag `0x20`, botão "Global"):** a fase vem de um acumulador do **canal**, não da voz:
  - a cada tick de reprodução todos os LFOs globais avançam `incremento` (`FUN_009ceec0`);
  - em saltos de posição (`FUN_009cef00`, chamado pelo transporte) a fase é **recalculada** como
    `posição_em_ticks · incremento (mod 2³²)`, isto é, o LFO global fica **travado à posição da música**
    (fase 0 no tick 0).
  - Delay e fade-in continuam **por voz** mesmo no modo global.
- Nos demos: 27 LFOs sincronizados vs 21 livres; 12 globais vs 36 por nota **[DADOS]**.

### 2.4 Aplicação por destino (`FUN_00d24500`, rodada 3 §1.6) **[DECOMP]**

`l` = saída do LFO (±1, já multiplicada pelo fade-in), `a` = amount cru do registro:

| Destino (registro) | Faixa de `a` | Efeito |
|---|---|---|
| 0 Pan | ±64 | `pan += 2·l·a/128` (= `l·a/64`, ±1 = pan total) |
| 1 Volume | ±128 | `ganho = env · (1 + l·a/128)`, piso 0 (a=128 → tremolo 0…2×) |
| 2 Cutoff (Mod X) | ±128 | `modX += l·a/128` (soma ao Mod X da voz, escala −1…+1 = faixa toda) |
| 3 Ressonância (Mod Y) | ±128 | `modY += l·a/128` |
| 4 Pitch | ±2400 | `pitch += l·a` **centésimos** (±2 oitavas) |

O ganho resultante é interpolado linearmente por amostra entre ticks (rodada 3 §1.4), por isso o
tremolo não "degrau"; pitch/pan/filtro mudam por tick.

---

## 3. 3x Osc: modo AM e formas 1/4

### 3.1 AM: fórmula exata **[DECOMP]**

- `FUN_007389d0`: AM ligado grava em `P+0x1F4` o índice `2` (tabela `{−1, 2}`): **só o osc 3** pode ser
  modulador. AM desligado = −1 (nenhum).
- Render da voz (`FUN_0073a230`): os 3 osciladores são renderizados **em ordem 1, 2, 3** no mesmo buffer.
  O primeiro oscilador audível **escreve** (modo 11 → `FUN_00739dc0`/`00739ce0`), os seguintes
  **somam** (modo 10 → `FUN_00739d70`/`00739ca0`), e o osc 3 com AM usa o modo 12
  (`FUN_00739e10`/`00739d20`), que faz por amostra:

```
acc = acc − acc · (g3 · o3)          ⇔      saída = (g1·o1 + g2·o2) · (1 − g3·o3)
```

- `g3 = mix3/128` e, com AM ligado, `resto = 1` (rodada 4 §2.2): `g2 = mix2/128`, `g1 = 1 − g2`. Logo o
  osc 3 **não soma** nada ao sinal; ele é um **modulador de amplitude com profundidade `g3`**:
  `g3 = 1` → ganho varia de 0 a 2 (onda `o3` de ±1); `g3 = 0,25` → 0,75…1,25.
- "Invert osc 3" troca o sinal de `g3` (`1 + |g3|·o3`), isto é, inverte a fase da modulação.
- Se `g3 = 0` o osc 3 só avança a fase: AM sem efeito.
- Estéreo: a mesma operação roda para o canal R (com detune/SPO próprios do osc 3).
- Há dois conjuntos de rotinas idênticas em semântica (SSE e x87), escolhidos por CPU; o x87 calcula o
  mesmo `acc − acc·(g·o)`.

### 3.2 Formas 1 e 4 **[HIP — não resolvido]**

O 3x Osc lê as tabelas de onda do **host** (`host+0x18 + shape·8`). Os arrays de ponteiros que achei no
FLEngine (`0x014255c0`, `0x014255d8`, `0x01425600`) são do LFO/automação, não do host de plugins
(ordens `sin,tri,sqr` / `sin,sqr,tri,saw,X` / `sin,sqr`); o array que o host entrega aos plugins é
preenchido em tempo de execução e não foi localizado. Fica como na rodada 4: 1 = triângulo, 4 = serra
arredondada **[HIP]**, coerente com os dados (2 = quadrada, 3 = serra, 5 = ruído).

---

## 4. Polifonia, mono e portamento: evento `0xDD` + bit "porta" da nota

### 4.1 Layout do `0xDD` (9 B → `C+0x1EC`) **[DECOMP+STR+DADOS]**

| Off | Tipo | Campo | Notas |
|---|---|---|---|
| 0 | i32 | **máx. vozes** (`C+0x1EC`) | 0 = ilimitado; controle `MaxPolySelect` (0…99) |
| 4 | i32 | **tempo de slide/porta** (`C+0x1F0`, parâmetro de canal `p = 6`) | knob `PolySlideWheel`: **0…1660, default 820** (FL 2026); arquivos antigos gravam 500 |
| 8 | u8 | **flags** (`C+0x1F4`) | **bit 0 = Mono** (`MonophonicBox`), **bit 1 = Porta** (`GlideBox`) |

- Os bits saem de `FUN_00f6f240` (o `OnClick` comum dos dois checkboxes): `flags = mono | porta·2`;
  os offsets de campo `0x9D8`/`0x9D0` foram casados com os nomes pela tabela de campos publicados do
  formulário **[DECOMP+STR]**.
- Nos demos **[DADOS]**: `(máx, flags)` = (0,0) 1905×, (1,0) 1108×, (16,0) 272×, (64,0) 57×,
  (0,1) 42×, (0,3) 31×, (32,0) 13×, (0,2) 3×…; tempo 500 (3273×), 820 (87×), 760, 0, 324, 967…

### 4.2 Conversão do tempo de porta (`FUN_00fea220` → `FUN_0117bce0` → `FUN_0117bbc0` + `FUN_01312200`) **[DECOMP+STR]**

```
n          = round( (27^(v/1024) − 1) · 48/26 )      (unidade: 1/48 de step = 1/192 de beat)
ticks      = n · (PPQ/4) / 48     (divisão inteira)   → C+0x6E4
tempo      = n/192 beats          (escala com o BPM)
```

| `v` | n | Tempo |
|---|---|---|
| 0 | 0 | sem glide |
| 500 (default antigo) | 7 | 0,036 beat (≈ 18 ms a 120 BPM) |
| 820 (default atual) | 24 | **½ step = 1/8 beat** |
| 1024 | 48 | 1 step |
| 1660 (máx.) | 384 | **8 steps = 2 beats** |

A dica da UI mostra o valor como `steps:ticks` (`FUN_011f2cb0`), o que confirma a unidade. A mesma curva
`27^(v/1024)` é usada para outros tempos do canal (`C+0x2B8`, `C+0x2C4`, `C+0x21C`).

### 4.3 Quando uma nota faz glide (`FUN_00d237e0`) **[DECOMP]**

Há glide quando **todas** as condições valem:

1. `(flag Porta do canal) ≠ (bit 0x10 da nota)` — o bit "porta" da nota (byte 19 do registro de nota,
   rodada 2) **inverte** o modo do canal (string "Inverted porta"): canal sem Porta + nota marcada = glide;
   canal com Porta + nota marcada = **sem** glide;
2. já houve uma nota anterior no mesmo **grupo** (os 4 bits baixos do byte 19 = cor/canal MIDI 0…15;
   slot `C+0x398 + grupo·0x34`, sentinela `0x7FFFFFFF` = "nenhuma nota ainda");
3. `ticks_porta ≥ 1`;
4. o valor auxiliar devolvido por `FUN_00d28910` para a nota (guardado em `V+0x1C8`) é 0 **[HIP: marca
   de "slide note"/nota gerada]** e o bloco de eco/arpejo do canal (`C+0x200`) está zerado **[nome do
   bloco HIP]**.

### 4.4 Como o glide anda (`FUN_00d23db0`, `FUN_00d26460`, `FUN_00d26430`, passo em `0xd2ab90`) **[DECOMP]**

- A voz nova **começa com os valores da última nota disparada no grupo** e vai **linearmente** até os
  seus próprios valores em `ticks_porta + 1` ticks: a cada tick soma `(alvo − início)/(ticks+1)`.
- O glide vale para os **5 parâmetros por nota** juntos: pan, velocidade/volume, **pitch** (em
  centésimos ⇒ linear em semitons), Mod X e Mod Y.
- "Última nota" é a última **disparada** no grupo, mesmo que já tenha terminado (o slot só é atualizado
  no disparo). Quando não há glide, a nota nova apenas grava seus valores no slot.
- O avanço é por tick (degraus de ~5 ms); a voz antiga **não** é cortada pelo glide em si — isso depende
  do modo Mono.

### 4.5 Mono e limite de vozes (`FUN_00d293d0`, `FUN_00d2bd90`, `FUN_00d236d0`) **[DECOMP; detalhes do mono HIP]**

- **Mono desligado e máx ≠ 0** (e canal não é do tipo 2): antes de disparar, conta as vozes vivas do
  canal; se `vivas ≥ máx`, solta as `vivas − máx + 1` mais antigas (note-off/kill `FUN_00d243c0`).
- **Mono ligado** (bit 0): o limitador acima não roda; a voz entra numa lista por **grupo** (cor/canal
  MIDI). Se já existe voz no grupo, a nova é marcada com estado `−10` e o canal é notificado
  (`FUN_00feaf70`) — comportamento de *legato*/troca de nota mono **[HIP para o detalhe]**. Notas com
  flags `0x90000` (internas) não entram no mono.
- **"Full porta"** (`PortaNoGateBox`, `C+0x2C2`, rodada 3 §4.2 `0x3E`): normalmente, se `C+0x2C4 < 1447`,
  a duração da nota é limitada a `C+0x6D8` ticks (mesma curva `27^(v/1024)`); com "Full porta" ligado,
  notas com o bit `0x10` ficam **sem** esse limite (`FUN_00d293d0`) **[DECOMP; nome do limite HIP]**.

---

## 5. Marcadores de tempo: evento `0x94` (`tipo << 24 | posição`) **[STR+DADOS]**

Dois arrays paralelos de 13 strings em `0x015d5068` (nome de script / nome curto), indexados pelo
tipo **[STR]**:

| Tipo | Nome interno | Nome curto | Nos demos | Comportamento no FL |
|---|---|---|---|---|
| 0 | `normal` | Marker | 210 | marcador simples (navegação) |
| 1 | `marker loop` | Loop | 1 | ao chegar, volta para o marcador anterior (repete a seção) **[HIP]** |
| 2 | `marker skip` | Skip | 5 | ao chegar, pula para o próximo marcador **[HIP]** |
| 3 | `marker pause` | Pause | 2 | pausa a reprodução no marcador **[HIP]** |
| 4 | `loop` | Loop | 13 | início da região de loop da música (par com o 11) **[HIP]** |
| 5 | `start` | Start | 13 | posição inicial da música **[HIP]** |
| 6 | `pattern length` | Length | 1 | muda o comprimento de pattern/compasso a partir daqui **[HIP]** |
| 7 | `unused` | Unused | 0 | reservado |
| 8 | `time signature` | TimeSig | 68 | mudança de fórmula de compasso (usa `0x21`/`0x22`) **[DADOS]** |
| 9 | `start recording` | StartRec | 0 | *punch in* **[STR: "punch in"]** |
| 10 | `stop recording` | StopRec | 0 | *punch out* **[STR: "punch out"]** |
| 11 | `loop length` | LoopLen | 1 | fim/comprimento do loop (par com o 4) **[HIP]** |
| 12 | `Key/Scale` | Key | 0 | mudança de tonalidade/escala (FL 2025+) |

Eventos que seguem cada `0x94` **[DADOS]**: `0x21` (numerador, ex. 4), `0x22` (denominador, ex. 4) —
gravados para **todos** os tipos, mas só significativos no tipo 8 — e depois `0xCD` (nome UTF-16, opcional)
e/ou `0x2E`. A posição (24 bits baixos) está em ticks do PPQ do projeto.

---

## Recomendações para o LYNX

**EQ do mixer**
1. Importador: para cada trilha leia `p = 0x1FD0+b` (ganho), `0x1FD8+b` (freq), `0x1FE0+b` (largura),
   b = 0…2, ignorando o campo `slot`. Converta com `dB = v/100`, `Hz = 10 + 159,9·(101^(v/65536) − 1)`,
   `bw = v/65536`.
2. Implemente 3 biquads RBJ em série: low shelf (`S = 1,9·bw + 0,1`), peak
   (`Q = 0,0999·(1001^bw − 1) + 0,1`), high shelf (`S = 1,9·bw + 0,1`), `A = 10^(dB/40)`. Use estado em
   `double`. Pule a banda quando o ganho for 0 (com uma cauda curta antes de zerar o estado). Se o EQ do
   LYNX for parametrizado por Q nos shelves, converta S → Q pela relação do cookbook
   `1/Q = √((A + 1/A)(1/S − 1) + 2)`.
3. Posição: depois dos slots de efeito e antes de volume/pan/separação. Exponha "pós-EQ" como ponto de
   tap se o LYNX tiver gravação por trilha.
4. Corrija a fórmula de frequência da rodada 2 (base 101, não 100).

**LFO do canal**
5. Importador: `período_s = 0,064·(1000^(v/65536) − 1)` (livre) ou `período_beats = 0,128·(...)`
   (`flags & 2`). Shape 0/1/2 = seno/triângulo/pulso, todos começando na fase 0 (seno e triângulo
   subindo de 0, pulso em +1). Delay e fade-in com a mesma curva de tempo.
6. Por padrão reinicie a fase em cada nota; com `flags & 0x20` derive a fase da posição da música
   (`fase = ticks·inc`). Aplique por destino conforme §2.4 (volume multiplica, os outros somam).

**3x Osc**
7. Com AM ligado: `out = (g1·o1 + g2·o2)·(1 − g3·o3)` com `g1 + g2 = 1`; osc 3 não entra na soma.

**Portamento / mono**
8. Importador do `0xDD`: `maxVoices`, `portaBeats = round((27^(v/1024) − 1)·48/26)/192`,
   `mono = flags & 1`, `porta = flags & 2`.
9. Glide: decida por nota com `porta_canal XOR bit 0x10 da nota`; parta dos valores da última nota
   disparada no mesmo grupo de cor/canal MIDI e interpole **linearmente** (pitch em cents, e também
   pan/vel/ModX/ModY) durante `portaBeats`. Pode ser por amostra no LYNX (o FL faz por tick).
10. Mono: uma voz por grupo; sem mono e com máx > 0, roube as vozes mais antigas.

**Marcadores**
11. Importe os tipos 0, 4/11 (loop), 5 (start) e 8 (fórmula de compasso, com `0x21/0x22`); guarde os
    demais como marcadores com o tipo preservado para *round-trip*.

---

## 6. Perguntas em aberto

1. Tabelas de onda entregues pelo host aos plugins nativos (ordem real dos shapes 1 e 4 do 3x Osc).
2. Valor e semântica exata do "tempo de cauda" do EQ (`PTR_DAT_015e32e8`) e do teto de frequência
   (`ctx+0x30`) do cálculo do biquad.
3. Detalhes do mono (estado `−10`, `FUN_00feaf70`): se a voz antiga é cortada ou "retargetada"
   (legato), e quando a sentinela "sem nota anterior" do glide é reiniciada (início do play? troca de
   pattern?).
4. Semântica fina dos marcadores 1/2/3/6/11 (só confirmados por nome).
5. `0x2E` que segue os marcadores.

---

## 7. Índice de evidências

| Função / dado | O que faz | Marca |
|---|---|---|
| FLEngine `FUN_01046a70` (ramo `0x1FD0…0x1FE7`) | Decodifica banda/grandeza, grava `T+0x1D4+b·0x88`, dicas dB/Hz | DECOMP |
| FLEngine `0x0104eed3…0x0104ef3f` | Faixa do ganho do EQ ±1800 (edição por texto) | DECOMP |
| FLEngine `FUN_0127b390` | Ganho·0,01 dB, freq `10+159,9·(101^x−1)`, bw = v/65536, contador de cauda | DECOMP |
| FLEngine `0x01287061…0x012870bc` (criação da trilha) | Tipos de banda 6/7/8, defaults 0x1691 / 17500 | DECOMP |
| FLEngine `FUN_0069d0e0` + `FUN_0069cb10` | Coeficientes RBJ: tipos 1…8, Q/S por bw | DECOMP |
| FLEngine `FUN_0069d500` (via `PTR_DAT_015e4448`) | Biquad DF1 double estéreo | DECOMP |
| FLEngine `FUN_01276d00` | Laço das 3 bandas, bypass com ganho 0, hold de cauda | DECOMP |
| FLEngine `FUN_01276ee0` (+ `01277550/012775a0/012775f0/01276e40`) | Ordem FX → EQ → fader e taps | DECOMP |
| FLEngine `FUN_009cdfc0`, tabela `0x014255c0` | Tabelas seno/triângulo/pulso do LFO | DECOMP |
| FLEngine `FUN_009ce9e0`, `FUN_009ce950`, `FUN_006614a0` | Velocidade → ticks → incremento de fase | DECOMP |
| FLEngine `FUN_009cedb0`, `FUN_009ce490` | Fase por voz, delay, fade-in, modo global | DECOMP |
| FLEngine `FUN_009ceec0`, `FUN_009cef00`, `FUN_006614b0` | Fase global = posição·incremento | DECOMP |
| 48 LFOs ativos nos demos | Shapes, sync, global, velocidades em divisões exatas | DADOS |
| 3x Osc `FUN_007389d0`, `DAT_00791a18` | AM → osc de índice 2 | DECOMP |
| 3x Osc `FUN_0073a230`, `0x739e90` (despacho de modo) | Ordem de render, modos 10/11/12 | DECOMP |
| 3x Osc `FUN_00739e10` / `FUN_00739d20` | Modo AM: `acc −= acc·g·o` | DECOMP |
| 3x Osc `0x73acc0` | Escolha SSE/x87 das rotinas de mistura | DECOMP |
| FLEngine `FUN_00f6f240` + tabela de campos (`0xb55c2d…`) | Flags `0xDD`: bit 0 Mono, bit 1 Porta | DECOMP+STR |
| FLEngine DFM `TPluginForm` (`PolySlideWheel`, `GlideBox`, `MonophonicBox`, `MaxPolySelect`, `PortaNoGateBox`) | Faixas 0…1660 (820), 0…99, nomes | STR |
| FLEngine `FUN_00fea220`, `FUN_0117bbc0`, `FUN_01312200`, `FUN_011f2cb0` | Tempo de porta → 1/48 step → ticks; dica em steps:ticks | DECOMP |
| FLEngine `FUN_00d293d0`, `FUN_00d2bd90` | Disparo de voz, limitador de polifonia | DECOMP |
| FLEngine `FUN_00d237e0`, `FUN_00d23db0`, `FUN_00d26460`, `FUN_00d26430`, `0x00d2ab90` | Condição de glide (XOR), início na última nota do grupo, rampa linear por tick | DECOMP |
| FLEngine `FUN_00d265e0` | Ordem dos 5 parâmetros por nota (pan, vol, pitch, ModX, ModY) | DECOMP |
| FLEngine `FUN_00d236d0` | Lista mono por grupo | DECOMP |
| 3.443 eventos `0xDD` nos demos | Combinações máx/flags e tempos | DADOS |
| FLEngine `0x015d5068` (2 × 13 ponteiros) | Nomes dos tipos de marcador 0…12 | STR |
| 314 marcadores `0x94` nos demos | Tipos usados, eventos `0x21/0x22/0xCD/0x2E` seguintes | DADOS |
